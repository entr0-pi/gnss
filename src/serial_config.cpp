/*
 * serial_config: NVS key/value store driven over USB serial.
 * ==========================================================
 *
 * Integrated version of the standalone arduino.cpp sketch, implementing the
 * protocol in docs/SERIAL.md. Tags such as [G4] refer to node ids in its
 * flowchart "Firmware receiver, policy, and effects".
 *
 * Wire format   STX(0x02) + JSON + '#' + CRC16 as 4 uppercase hex + ETX(0x03)
 * CRC           CRC16-CCITT: poly 0x1021, init 0xFFFF, no reflection,
 *               no final XOR, computed over the JSON bytes only.
 * Operations    hello, set, get, erase. set/get/erase carry "ns" and "key".
 *               Any other op is refused with invalid_request. Every reply to
 *               a request whose seq could be parsed echoes that seq.
 *
 * Policy        Namespaces: gnss / wifi / ntrip only. NTRIP lockout keys are
 *               hidden (forbidden). Immutable namespaces refuse set/erase
 *               (read_only). set only writes a key config_bootstrap() already
 *               seeded, with the same NVS type. No key table: the seeded NVS
 *               entries are the schema.
 *
 * Effects       Values are only stored; nothing is applied here. ntrip is
 *               picked up by configMonitorTask, gnss and wifi at the next boot.
 *
 * Logging       Shares Serial with logger.cpp. Build with GLOBAL_LOG_LEVEL=0
 *               (app.h warns otherwise). This file does not log on purpose.
 *
 * Memory        Three static 512-byte buffers (RX, TX scratch, reply cache).
 *               No Arduino String objects. JSON documents are short-lived.
 */

#include "app.h"

#if SERIAL_CONFIG_ENABLE

#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>

#include "nvs_keys.h"
#include "serial_config.h"

namespace {

// ---------------------------------------------------------------------------
// Protocol constants (SERIAL.md "Wire format and messages"; 500 ms = [T0])
// ---------------------------------------------------------------------------
constexpr uint8_t  kStx            = 0x02;
constexpr uint8_t  kEtx            = 0x03;
constexpr uint8_t  kSep            = '#';
constexpr size_t   kCrcHexLen      = 4;
constexpr size_t   kMaxFrame       = 512;                        // STX..ETX inclusive
constexpr size_t   kMaxBody        = kMaxFrame - 2;              // bytes between STX and ETX
constexpr size_t   kMaxJson        = kMaxFrame - 3 - kCrcHexLen; // minus STX, '#', CRC, ETX
constexpr size_t   kMaxKeyLen      = 15;                         // NVS limit (16 incl. NUL)
constexpr size_t   kMaxStringLen   = 256;                        // longest string value, bytes
constexpr uint32_t kFrameTimeoutMs = 500;

static_assert(SERIAL_CONFIG_RX_BUFFER >= 2 * kMaxFrame,
              "RX buffer must hold at least two full frames");

// Error codes (SERIAL.md "Errors and events across the boundary")
constexpr char kErrFrame[]       = "frame_error";
constexpr char kErrCrc[]         = "crc_mismatch";
constexpr char kErrBadJson[]     = "bad_json";
constexpr char kErrMissingSeq[]  = "missing_seq";
constexpr char kErrNotSynced[]   = "not_synced";
constexpr char kErrReplay[]      = "replay_detected";
constexpr char kErrInvalid[]     = "invalid_request";
constexpr char kErrNvsWrite[]    = "nvs_write_failed";
constexpr char kErrNotFound[]    = "not_found";
constexpr char kErrUnsupported[] = "unsupported_value";
constexpr char kErrForbidden[]   = "forbidden";
constexpr char kErrReadOnly[]    = "read_only";

// ---------------------------------------------------------------------------
// Namespace policy [VNS, VHIDE, VIMM]
// ---------------------------------------------------------------------------
constexpr const char* kNamespaces[] = {
  nvs_keys::gnss::kNamespace,
  nvs_keys::wifi::kNamespace,
  nvs_keys::ntrip::kNamespace,
};

struct HiddenKey { const char* ns; const char* key; };

// get/set/erase refused: runtime state written by the NTRIP client, not config.
constexpr HiddenKey kHiddenKeys[] = {
  {nvs_keys::ntrip::kNamespace, nvs_keys::ntrip::lockout::kAttempts},
  {nvs_keys::ntrip::kNamespace, nvs_keys::ntrip::lockout::kAbandoned},
  {nvs_keys::ntrip::kNamespace, nvs_keys::ntrip::lockout::kHash},
};

bool isKnownNamespace(const char* ns) {
  for (const char* known : kNamespaces) {
    if (strcmp(ns, known) == 0) return true;
  }
  return false;
}

bool isHiddenKey(const char* ns, const char* key) {
  for (const HiddenKey& h : kHiddenKeys) {
    if (strcmp(ns, h.ns) == 0 && strcmp(key, h.key) == 0) return true;
  }
  return false;
}

// Namespaces whose NVS content the firmware ignores (build flags, app.h).
bool isImmutable(const char* ns) {
#if IMMUTABLE_UART
  if (strcmp(ns, nvs_keys::gnss::kNamespace) == 0) return true;
#endif
#if IMMUTABLE_WIFI
  if (strcmp(ns, nvs_keys::wifi::kNamespace) == 0) return true;
#endif
#if IMMUTABLE_NTRIP
  if (strcmp(ns, nvs_keys::ntrip::kNamespace) == 0) return true;
#endif
  (void)ns;
  return false;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

// Frame assembly [E0-E9, T0-T1]
struct RxState {
  uint8_t  buf[kMaxBody];
  size_t   len     = 0;
  bool     inFrame = false;
  uint32_t startMs = 0;
};
RxState rx;

// Sequence / replay protection [G1, G3, G6, G7]
struct SessionState {
  bool     synced        = false;  // a hello was received since boot
  uint32_t expectedRxSeq = 0;      // lowest seq accepted for a new command
};
SessionState session;

// Reply to the last accepted command, resent verbatim when the desktop
// retries the same seq because our reply was lost [G4-G5, Z2].
struct ReplyCache {
  uint8_t  frame[kMaxFrame];
  size_t   len   = 0;
  uint32_t seq   = 0;
  bool     valid = false;
};
ReplyCache cache;

uint8_t txBuf[kMaxFrame];  // scratch frame for replies that are not cached

// ---------------------------------------------------------------------------
// CRC16-CCITT. Bitwise on purpose: identical to the Go implementation and
// fast enough for frames of at most 512 bytes.
// ---------------------------------------------------------------------------
uint16_t crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= static_cast<uint16_t>(*data++) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

// Returns the value of one hex digit (either case), or -1.
int hexValue(uint8_t c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Parses the 4-hex-digit CRC field. A field of the wrong length or with
// non-hex characters is treated like a CRC mismatch.
bool parseCrcField(const uint8_t* p, size_t len, uint16_t& out) {
  if (len != kCrcHexLen) return false;
  uint16_t v = 0;
  for (size_t i = 0; i < len; ++i) {
    const int n = hexValue(p[i]);
    if (n < 0) return false;
    v = static_cast<uint16_t>((v << 4) | n);
  }
  out = v;
  return true;
}

// Writes crc as 4 uppercase hex digits.
void writeCrcHex(uint8_t* out, uint16_t crc) {
  static const char kHex[] = "0123456789ABCDEF";
  out[0] = kHex[(crc >> 12) & 0xF];
  out[1] = kHex[(crc >> 8) & 0xF];
  out[2] = kHex[(crc >> 4) & 0xF];
  out[3] = kHex[crc & 0xF];
}

// ---------------------------------------------------------------------------
// Reply transmission [Z0-Z2]
// ---------------------------------------------------------------------------

// [Z1] Frames `doc` into `out` (at least kMaxFrame bytes) and returns the
// length. A reply that would not fit is replaced by a short unsupported_value
// error that keeps seq, so the frame never overflows.
size_t buildFrame(const JsonDocument& doc, uint8_t* out) {
  JsonDocument fallback;
  const JsonDocument* src = &doc;
  if (measureJson(doc) > kMaxJson) {
    if (!doc["seq"].isNull()) fallback["seq"] = doc["seq"];
    fallback["status"]  = "error";
    fallback["message"] = kErrUnsupported;
    src = &fallback;
  }

  size_t pos = 0;
  out[pos++] = kStx;
  const size_t jsonLen =
      serializeJson(*src, reinterpret_cast<char*>(out + pos), kMaxJson + 1);
  const uint16_t crc = crc16(out + pos, jsonLen);
  pos += jsonLen;
  out[pos++] = kSep;
  writeCrcHex(out + pos, crc);
  pos += kCrcHexLen;
  out[pos++] = kEtx;
  return pos;
}

// Sends a reply that is NOT cached (hello [G2], sequence and frame errors).
void sendReply(const JsonDocument& doc) {
  const size_t len = buildFrame(doc, txBuf);
  Serial.write(txBuf, len);
}

// Sends the result of an accepted command and caches it as the reply to
// `seq` [Z2]. The frame is built directly in the cache, so nothing is copied.
void sendAndCacheReply(const JsonDocument& doc, uint32_t seq) {
  cache.len   = buildFrame(doc, cache.frame);
  cache.seq   = seq;
  cache.valid = true;
  Serial.write(cache.frame, cache.len);
}

// Frame-level error: the request's seq is unknown, so none is echoed [FX1-FX3].
void sendError(const char* code) {
  JsonDocument resp;
  resp["status"]  = "error";
  resp["message"] = code;
  sendReply(resp);
}

// Sequence-level error that echoes the request's seq, not cached [GX1, GX2].
void sendError(const char* code, uint32_t seq) {
  JsonDocument resp;
  resp["seq"]     = seq;
  resp["status"]  = "error";
  resp["message"] = code;
  sendReply(resp);
}

// Command error. `message` is the machine code; `detail` (optional) is a
// human-readable reason. Both are string literals, stored by pointer.
void setError(JsonDocument& resp, const char* code, const char* detail = nullptr) {
  resp["status"]  = "error";
  resp["message"] = code;
  if (detail) resp["detail"] = detail;
}

// ---------------------------------------------------------------------------
// Value types
// ---------------------------------------------------------------------------
enum class ValueType : uint8_t { Invalid, Int, Uint, Float, Bool, String };

// Float is still recognised so that it is refused with unsupported_value
// [PTYPE-VUNSUP] rather than invalid_request.
ValueType parseValueType(const char* s) {
  if (strcmp(s, "int") == 0)    return ValueType::Int;
  if (strcmp(s, "uint") == 0)   return ValueType::Uint;
  if (strcmp(s, "float") == 0)  return ValueType::Float;
  if (strcmp(s, "bool") == 0)   return ValueType::Bool;
  if (strcmp(s, "string") == 0) return ValueType::String;
  return ValueType::Invalid;
}

// NVS type that Preferences uses for each supported protocol type.
PreferenceType nvsTypeOf(ValueType t) {
  switch (t) {
    case ValueType::Int:    return PT_I32;
    case ValueType::Uint:   return PT_U32;
    case ValueType::Bool:   return PT_U8;
    case ValueType::String: return PT_STR;
    default:                return PT_INVALID;
  }
}

// Protocol type of a stored NVS entry; Invalid for types outside the four.
ValueType valueTypeOf(PreferenceType t) {
  switch (t) {
    case PT_I32: return ValueType::Int;
    case PT_U32: return ValueType::Uint;
    case PT_U8:  return ValueType::Bool;
    case PT_STR: return ValueType::String;
    default:     return ValueType::Invalid;
  }
}

// detail strings for a type mismatch, one per expected (stored) type.
const char* typeMismatchDetail(ValueType expected) {
  switch (expected) {
    case ValueType::Int:    return "type mismatch: int expected";
    case ValueType::Uint:   return "type mismatch: uint expected";
    case ValueType::Bool:   return "type mismatch: bool expected";
    case ValueType::String: return "type mismatch: string expected";
    default:                return "type mismatch";
  }
}

bool isValidKey(const char* key) {
  const size_t n = strlen(key);
  return n >= 1 && n <= kMaxKeyLen;
}

// The JSON value must already have the declared type: no silent coercion
// (e.g. "115200" as uint would otherwise be stored as 0) [PMATCH].
bool valueMatchesType(ValueType t, JsonVariantConst v) {
  switch (t) {
    case ValueType::Int:   return v.is<int32_t>();
    case ValueType::Uint:  return v.is<uint32_t>();
    case ValueType::Bool:  return v.is<bool>();
    case ValueType::String:
      return v.is<const char*>() && strlen(v.as<const char*>()) <= kMaxStringLen;
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// Integration hook
// ---------------------------------------------------------------------------

// Called after a key was persisted [P2]. Intentionally empty: the protocol
// only stores values. The firmware picks them up through its existing paths:
// ntrip via configMonitorTask (~5 s, while online), gnss and wifi at the next
// boot. If it is ever filled, keep it short: it runs before the reply is
// sent, and the desktop gives up on an attempt after 1 s.
void onKeySet(const char* ns, const char* key, ValueType type, JsonVariantConst value) {
  (void)ns;
  (void)key;
  (void)type;
  (void)value;
}

// ---------------------------------------------------------------------------
// NVS operations [PEXIST-P4]. `prefs` is open on the request's namespace.
// ---------------------------------------------------------------------------

// [PEXIST, PTYPE, PMATCH, P2] Writes only a key that is already seeded, with
// its stored type, then checks the byte count Preferences reports.
// Limitation: an empty string reports 0 bytes on success and on failure,
// so a failed write of "" is not detected.
void doSet(Preferences& prefs, const char* ns, const char* key, ValueType type,
           JsonVariantConst value, JsonDocument& resp) {
  if (!prefs.isKey(key)) {  // [PEXIST]
    setError(resp, kErrInvalid, "unknown key");
    return;
  }

  const ValueType stored = valueTypeOf(prefs.getType(key));
  if (type == ValueType::Float || stored == ValueType::Invalid) {  // [PTYPE]
    setError(resp, kErrUnsupported);
    return;
  }

  if (stored != type) {  // [PMATCH]
    setError(resp, kErrInvalid, typeMismatchDetail(stored));
    return;
  }
  if (!valueMatchesType(type, value)) {
    setError(resp, kErrInvalid, "invalid value");
    return;
  }

  size_t expected = 0;
  size_t written  = 0;
  switch (type) {
    case ValueType::Int:
      expected = sizeof(int32_t);
      written  = prefs.putInt(key, value.as<int32_t>());
      break;
    case ValueType::Uint:
      expected = sizeof(uint32_t);
      written  = prefs.putUInt(key, value.as<uint32_t>());
      break;
    case ValueType::Bool:
      expected = sizeof(uint8_t);
      written  = prefs.putBool(key, value.as<bool>());
      break;
    case ValueType::String: {
      const char* s = value.as<const char*>();
      expected = strlen(s);
      written  = prefs.putString(key, s);
      break;
    }
    default:
      break;  // unreachable: checked above
  }

  if (written != expected) {
    setError(resp, kErrNvsWrite);
    return;
  }
  resp["status"] = "ok";
  onKeySet(ns, key, type, value);
}

// [P3] Reads the value with the getter that matches its stored type:
// missing → not_found, any other NVS type or oversized string →
// unsupported_value.
void doGet(Preferences& prefs, const char* key, JsonDocument& resp) {
  switch (prefs.getType(key)) {
    case PT_INVALID:
      setError(resp, kErrNotFound);
      return;
    case PT_I32:
      resp["type"]  = "int";
      resp["value"] = prefs.getInt(key);
      break;
    case PT_U32:
      resp["type"]  = "uint";
      resp["value"] = prefs.getUInt(key);
      break;
    case PT_U8:
      resp["type"]  = "bool";
      resp["value"] = prefs.getBool(key);
      break;
    case PT_STR: {
      char buf[kMaxStringLen + 1];
      // Returns 0 when the stored string is longer than the buffer.
      if (prefs.getString(key, buf, sizeof(buf)) == 0) {
        setError(resp, kErrUnsupported, "string longer than 256 bytes");
        return;
      }
      char* copy = buf;  // non-const char*: ArduinoJson copies it (buf dies on return)
      resp["type"]  = "string";
      resp["value"] = copy;
      break;
    }
    default:
      setError(resp, kErrUnsupported);  // blob/I8/U16/I64/... not used by config
      return;
  }
  resp["status"] = "ok";
}

// [P4] Idempotent: removing a missing key still reports ok. config_bootstrap()
// re-seeds the namespace at the next boot (see docs/SERIAL.md: that re-seed
// rewrites every key of the namespace with its default).
void doErase(Preferences& prefs, const char* key, JsonDocument& resp) {
  prefs.remove(key);
  resp["status"] = "ok";
}

// [V0-PCLOSE] Validates and executes an accepted command, filling `resp`.
void executeCommand(const char* op, const char* ns, const char* key,
                    const JsonDocument& req, JsonDocument& resp) {
  const bool isSet   = strcmp(op, "set") == 0;
  const bool isGet   = strcmp(op, "get") == 0;
  const bool isErase = strcmp(op, "erase") == 0;

  // [V0] op and required fields. apply, restart or any other op ends here.
  if (!(isSet || isGet || isErase)) {
    setError(resp, kErrInvalid, "unknown op");
    return;
  }
  if (!isValidKey(key)) {
    setError(resp, kErrInvalid, "invalid key");
    return;
  }
  ValueType type = ValueType::Invalid;
  if (isSet) {
    type = parseValueType(req["type"] | "");
    if (type == ValueType::Invalid) {
      setError(resp, kErrInvalid, "invalid type");
      return;
    }
  }

  if (!isKnownNamespace(ns)) {  // [VNS]
    setError(resp, kErrInvalid, "unknown namespace");
    return;
  }
  if (isHiddenKey(ns, key)) {  // [VHIDE]
    setError(resp, kErrForbidden);
    return;
  }
  if (!isGet && isImmutable(ns)) {  // [VIMM]
    setError(resp, kErrReadOnly, "fixed by firmware build");
    return;
  }

  Preferences prefs;
  if (!prefs.begin(ns, /*readOnly=*/isGet)) {  // [P0-PX]
    setError(resp, kErrNvsWrite, "namespace open failed");
    return;
  }

  if (isSet)      doSet(prefs, ns, key, type, req["value"], resp);
  else if (isGet) doGet(prefs, key, resp);
  else            doErase(prefs, key, resp);

  prefs.end();  // [PCLOSE]
}

// ---------------------------------------------------------------------------
// Command handling: sequence checks [G0-G7], then execution
// ---------------------------------------------------------------------------
void handleCommand(const JsonDocument& req) {
  const uint32_t seq = req["seq"].as<uint32_t>();
  const char*    op  = req["op"] | "";

  // [G0-G2] hello always succeeds: it (re)starts the session at the
  // desktop's seq. Not cached, since repeating it is harmless.
  if (strcmp(op, "hello") == 0) {
    session.expectedRxSeq = seq + 1;
    session.synced        = true;
    cache.valid           = false;
    JsonDocument resp;
    resp["seq"]    = seq;
    resp["op"]     = "hello";
    resp["status"] = "ok";
    sendReply(resp);
    return;
  }

  // [G3-GX1] After a reboot, refuse commands until the desktop says hello.
  if (!session.synced) {
    sendError(kErrNotSynced, seq);
    return;
  }

  // [G4-G5] Retry of the last accepted command: our reply was lost.
  if (cache.valid && seq == cache.seq) {
    Serial.write(cache.frame, cache.len);
    return;
  }

  // [G6-GX2] Older than anything we may still accept: stale or replayed.
  if (seq < session.expectedRxSeq) {
    sendError(kErrReplay, seq);
    return;
  }

  // [G7] Accept. Seq may jump forward (the desktop gave up on a request),
  // so there is no gap error.
  session.expectedRxSeq = seq + 1;

  const char* ns  = req["ns"] | "";
  const char* key = req["key"] | "";
  JsonDocument resp;
  resp["seq"] = seq;
  resp["op"]  = op;
  resp["ns"]  = ns;
  resp["key"] = key;
  executeCommand(op, ns, key, req, resp);
  sendAndCacheReply(resp, seq);  // [Z0-Z2] ok and command errors are both cached
}

// ---------------------------------------------------------------------------
// Frame handling [F1-F3] (F0, the end of the frame, is in pumpSerial)
// ---------------------------------------------------------------------------
void handleFrame(const uint8_t* body, size_t len) {
  // [F1-FX1] Split on the LAST '#'. The CRC field never contains '#', and the
  // JSON may.
  size_t sep = len;
  while (sep > 0 && body[sep - 1] != kSep) --sep;
  if (sep == 0) {
    sendError(kErrFrame);
    return;
  }
  const size_t payloadLen = sep - 1;

  // [F2-FX2]
  uint16_t rxCrc = 0;
  if (!parseCrcField(body + sep, len - sep, rxCrc) ||
      rxCrc != crc16(body, payloadLen)) {
    sendError(kErrCrc);
    return;
  }

  // [F3-FX3]
  JsonDocument req;
  if (deserializeJson(req, reinterpret_cast<const char*>(body), payloadLen)) {
    sendError(kErrBadJson);
    return;
  }
  if (!req["seq"].is<uint32_t>()) {
    sendError(kErrMissingSeq);
    return;
  }

  handleCommand(req);  // [G0]
}

// ---------------------------------------------------------------------------
// Byte-level receiver [E0-E9, T0-T1]
// ---------------------------------------------------------------------------
void pumpSerial() {
  while (Serial.available() > 0) {
    const uint8_t b = static_cast<uint8_t>(Serial.read());  // [E0]

    if (b == kStx) {  // [E1-E2] STX always starts a fresh frame
      rx.len     = 0;
      rx.inFrame = true;
      rx.startMs = millis();
      continue;
    }
    if (!rx.inFrame) continue;  // [E4/E9-E5] noise, boot log, stray ETX

    if (b == kEtx) {  // [E3-F0]
      rx.inFrame = false;
      handleFrame(rx.buf, rx.len);
      continue;
    }
    if (rx.len >= kMaxBody) {  // [E6-E7] oversize: drop silently
      rx.inFrame = false;
      continue;
    }
    rx.buf[rx.len++] = b;  // [E8]
  }

  // [T0-T1] Checked after draining the available bytes: an unfinished frame
  // must not block the next one.
  if (rx.inFrame && millis() - rx.startMs > kFrameTimeoutMs) rx.inFrame = false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

// [B0] Session starts unsynced with an empty reply cache. Serial is already
// begun by main.cpp (with SERIAL_CONFIG_RX_BUFFER set before begin()).
void serial_config_setup() {
  rx      = RxState{};
  session = SessionState{};
  cache.valid = false;
}

void serial_config_poll() {
  pumpSerial();
}

#endif  // SERIAL_CONFIG_ENABLE
