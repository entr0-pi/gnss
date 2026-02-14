#if NMEA_ENABLE
#include "parsing_nmea.h"   // Your public snapshot struct + function declarations
#include "app.h"

// minmea is a small, fast NMEA parser library (C).
// We include it as C to avoid C++ name mangling issues.
extern "C" {
  #include "minmea.h"
}

#include <math.h>
#include <string.h>

// ================= Internal GPS state =================
//
// This module is fed raw UART bytes (NMEA stream).
// It reconstructs NMEA lines, parses a few sentence types (RMC/GGA/GSA),
// and maintains a "latest known state" snapshot that other code (web UI)
// can read safely.
//
// Concurrency note:
// - Bytes are fed from another task (UART RX task).
// - Snapshots are read from the web server context.
// - Because `double` can tear on 32-bit architectures, we protect state with a critical section.
struct GpsState {
  // ---- Position validity + kinematics ----
  bool     valid       = false;  // True when RMC says fix is valid (library-dependent)
  double   lat         = 0.0;    // Decimal degrees, +north
  double   lon         = 0.0;    // Decimal degrees, +east
  float    speedKmh    = 0.0f;   // Ground speed in km/h (from RMC speed in knots)

  // ---- Time (UTC) ----
  bool     timeValid   = false;  // True once we've captured a time from RMC or GGA
  uint8_t  hour        = 0;
  uint8_t  minute      = 0;
  uint8_t  second      = 0;

  // ---- Date (UTC) ----
  uint16_t year        = 0;      // Stored as full year (e.g., 2026)
  uint8_t  month       = 0;      // 1..12
  uint8_t  day         = 0;      // 1..31

  // ---- Fix quality / satellites / dilution ----
  uint8_t  satsUsed    = 0;      // Satellites used/tracked (from GGA)
  uint8_t  fixQuality  = 0;      // GGA fix quality (0=no fix, 1=GPS, 2=DGPS, etc.)
  uint8_t  fixType     = 0;      // GSA fix type (1=no fix, 2=2D, 3=3D)
  float    hdop        = 0.0f;   // Horizontal dilution of precision (GGA or GSA)

  float    hAcc_m      = 0.0f;   // horizontal accuracy estimate (m)
  float    vAcc_m      = 0.0f;   // vertical accuracy estimate (m)
  uint8_t  accSource   = 0;      // 0=none, 1=GST, 2=HDOP-est
  uint32_t accLastMs   = 0;      // millis timestamp of last accuracy update

  // ---- Satellite details (from GSV) ----
  // Committed from a staging buffer once a full epoch of GSV messages has been received.
  uint8_t     satCount = 0;
  NmeaSatInfo sats[NMEA_MAX_SATS];

  // ---- Freshness tracking ----
  // Timestamp (millis) of the last valid NMEA sentence we accepted (checksum OK + parsed OK).
  uint32_t lastMs      = 0;
};

// Global instance holding the latest state.
static GpsState g_gps;

// Map NMEA talker ID to constellation index.
// GP=GPS, GL=GLONASS, GA=Galileo, GB/BD=BeiDou, anything else=Other.
static inline uint8_t talker_to_constellation(char t0, char t1) {
  if (t0 == 'G') {
    switch (t1) {
      case 'P': return 0;   // GPS
      case 'L': return 1;   // GLONASS
      case 'A': return 2;   // Galileo
      case 'B': return 3;   // BeiDou
    }
  }
  if (t0 == 'B' && t1 == 'D') return 3;  // BeiDou alternate talker
  return 4;  // Other / unknown
}

// ================= Line collector =================
//
// We are parsing from raw bytes, not from a line-oriented stream.
// This tiny "collector" builds a NMEA line:
// - Start when '$' appears
// - Accumulate until '\n'
// - On '\n' terminate with '\0' and parse
//
// g_line size must be big enough for the longest sentence you care about.
// Typical NMEA lines are < 82 chars, but some can be longer; 96 gives headroom.
static char g_line[96];
static int  g_len = 0;

// ================= Last GGA storage =================
//
// Store the last valid GGA sentence for NTRIP GGA header transmission.
// The line is stored without CRLF; CRLF is added at send time if needed.
static char g_last_gga[96] = {0};

// ================= Concurrency protection =================
//
// ESP32 is 32-bit; `double` (64-bit) can be written/read in two halves ("torn") if accessed
// concurrently. To avoid torn reads, we wrap reads/writes to g_gps with a critical section.
//
// Note:
// - portENTER_CRITICAL disables interrupts on the current core.
// - It is fast, but keep the protected region small.
// - We only protect assignments / copies; the heavier parsing happens outside the lock.
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static inline void lock()   { portENTER_CRITICAL(&g_mux); }
static inline void unlock() { portEXIT_CRITICAL(&g_mux);  }

// ================= GSV staging buffers =================
// Accumulate per-constellation GSV data across all messages in a sequence.
// When msg_nr == total_msgs, we commit that constellation into the live list.

static struct {
  uint8_t     count;
  uint8_t     total_msgs;
  uint32_t    lastUpdateMs;
  NmeaSatInfo sats[NMEA_MAX_SATS];
} g_gsv_buf[5] = {};

static inline int find_sat_idx(uint8_t cons, int16_t nr) {
  for (uint8_t i = 0; i < g_gsv_buf[cons].count; i++) {
    if (g_gsv_buf[cons].sats[i].nr == nr) return i;
  }
  return -1;
}

static inline void buffer_sat(uint8_t cons, const struct minmea_sat_info& in) {
  if (in.nr == 0) return;
  if (in.snr == 0) return;
  int idx = find_sat_idx(cons, (int16_t)in.nr);
  if (idx >= 0) {
    NmeaSatInfo& s = g_gsv_buf[cons].sats[idx];
    s.elevation = (int16_t)in.elevation;
    s.azimuth = (int16_t)in.azimuth;
    s.snr = (int16_t)in.snr;
    return;
  }
  if (g_gsv_buf[cons].count >= NMEA_MAX_SATS) return;
  NmeaSatInfo& s = g_gsv_buf[cons].sats[g_gsv_buf[cons].count++];
  s.nr = (int16_t)in.nr;
  s.elevation = (int16_t)in.elevation;
  s.azimuth = (int16_t)in.azimuth;
  s.snr = (int16_t)in.snr;
  s.constellation = cons;
}

static inline void rebuild_live_sat_list(uint32_t nowMs) {
  lock();
  g_gps.satCount = 0;
  for (uint8_t cons = 0; cons < 5; cons++) {
    const uint32_t age = nowMs - g_gsv_buf[cons].lastUpdateMs;
    if (g_gsv_buf[cons].count == 0) continue;
    if (g_gsv_buf[cons].lastUpdateMs == 0 || age > NMEA_GSV_STALE_MS) continue;

    const uint8_t can_copy =
        (g_gps.satCount + g_gsv_buf[cons].count <= NMEA_MAX_SATS)
            ? g_gsv_buf[cons].count
            : (uint8_t)(NMEA_MAX_SATS - g_gps.satCount);
    if (can_copy == 0) break;
    memcpy(&g_gps.sats[g_gps.satCount],
           g_gsv_buf[cons].sats,
           can_copy * sizeof(NmeaSatInfo));
    g_gps.satCount += can_copy;
  }
  unlock();
}

// ================= Sentence processor =================
//
// Called with a full NMEA line (null-terminated) that starts with '$'.
// - Validates checksum (recommended)
// - Identifies sentence type
// - Parses only the sentences we care about
// - Updates the global GPS state (atomically via lock/unlock)
static inline void process_line(const char* line, uint32_t nowMs) {
  // Basic sanity: must start with '$'
  if (!line || line[0] != '$') return;

  // Verify NMEA checksum (recommended in noisy serial environments).
  // Second parameter 'true' means strict checking.
  if (!minmea_check(line, true)) return;

  // Update freshness timestamp: "we successfully received a valid NMEA sentence now".
  lock();
  g_gps.lastMs = nowMs;
  unlock();

  // Identify sentence type.
  // minmea_sentence_id(..., false) means: do not "strictly" require talker ID matches
  // beyond what minmea expects; you previously used false.
  switch (minmea_sentence_id(line, false)) {

    // ---------------- RMC: Recommended Minimum Navigation Information ----------------
    // Contains: validity, lat/lon, speed, time, date.
    case MINMEA_SENTENCE_RMC: {
      struct minmea_sentence_rmc rmc;
      if (!minmea_parse_rmc(&rmc, line)) break;

      // Validity:
      // Many NMEA streams use 'A' (valid) / 'V' (void).
      // Here you use `rmc.valid` directly (bool-ish in your build/config).
      // If you want the classic behavior, you could use: (rmc.valid == 'A')
      //const bool ok = (rmc.valid == 'A');
      const bool ok = rmc.valid;

      // Convert fixed-point lat/lon to decimal degrees.
      const double lat = minmea_tocoord(&rmc.latitude);
      const double lon = minmea_tocoord(&rmc.longitude);

      // Convert speed:
      // - NMEA RMC speed is in knots
      // - 1 knot = 1.852 km/h
      const float speedKnots = (float)minmea_tofloat(&rmc.speed);
      const float speedKmh   = speedKnots * 1.852f;

      // UTC time fields from RMC.
      const uint8_t hh = (uint8_t)rmc.time.hours;
      const uint8_t mm = (uint8_t)rmc.time.minutes;
      const uint8_t ss = (uint8_t)rmc.time.seconds;

      // Date fields from RMC (usually DDMMYY).
      // minmea returns year as 0..99; here we assume 2000+.
      const uint8_t  dd = (uint8_t)rmc.date.day;
      const uint8_t  mo = (uint8_t)rmc.date.month;
      const uint16_t yy = (uint16_t)(2000 + rmc.date.year);

      // Commit all updates atomically to avoid torn reads.
      lock();
      g_gps.valid     = ok;
      g_gps.lat       = lat;
      g_gps.lon       = lon;
      g_gps.speedKmh  = speedKmh;

      g_gps.timeValid = true;
      g_gps.hour      = hh;
      g_gps.minute    = mm;
      g_gps.second    = ss;

      g_gps.day       = dd;
      g_gps.month     = mo;
      g_gps.year      = yy;
      unlock();
    } break;

    // ---------------- GGA: Global Positioning System Fix Data ----------------
    // Contains: fix quality, number of satellites, HDOP, and time.
    case MINMEA_SENTENCE_GGA: {
      struct minmea_sentence_gga gga;
      if (!minmea_parse_gga(&gga, line)) break;

      const uint8_t fixQ = (uint8_t)gga.fix_quality;
      const uint8_t sats = (uint8_t)gga.satellites_tracked;
      const float   hd   = (float)minmea_tofloat(&gga.hdop);

      // Store the validated GGA line for NTRIP GGA header transmission.
      // The line is already null-terminated and checksum-validated.
      strncpy(g_last_gga, line, sizeof(g_last_gga) - 1);
      g_last_gga[sizeof(g_last_gga) - 1] = '\0';

      lock();
      g_gps.fixQuality = fixQ;
      g_gps.satsUsed   = sats;

      // Only overwrite hdop if the parsed value is positive (guard against invalid/empty fields).
      if (hd > 0.0f) g_gps.hdop = hd;

      // Fallback accuracy estimate from HDOP (very rough):
      // hAcc ≈ HDOP * 5m, vAcc ≈ hAcc * 1.5
      // Only apply if we do NOT already have GST-derived accuracy.
      if (hd > 0.0f && g_gps.accSource != 1) {
        const float h = hd * 5.0f;
        const float v = h * 1.5f;
        g_gps.hAcc_m    = h;
        g_gps.vAcc_m    = v;
        g_gps.accSource = 2;      // HDOP-est
        g_gps.accLastMs = nowMs;
      }

      // Fallback time source:
      // If we haven't received an RMC yet, we can still populate UTC time from GGA.
      if (!g_gps.timeValid) {
        g_gps.timeValid = true;
        g_gps.hour   = (uint8_t)gga.time.hours;
        g_gps.minute = (uint8_t)gga.time.minutes;
        g_gps.second = (uint8_t)gga.time.seconds;
      }
      unlock();
    } break;

    // ---------------- GSA: GNSS DOP and Active Satellites ----------------
    // Contains: fix type and DOP values (PDOP/HDOP/VDOP).
    case MINMEA_SENTENCE_GSA: {
      struct minmea_sentence_gsa gsa;
      if (!minmea_parse_gsa(&gsa, line)) break;

      const uint8_t fixT = (uint8_t)gsa.fix_type;
      const float   hd   = (float)minmea_tofloat(&gsa.hdop);

      lock();
      g_gps.fixType = fixT;

      // Only overwrite hdop if the parsed value is positive (guard against invalid/empty fields).
      if (hd > 0.0f) g_gps.hdop = hd;

      // Fallback accuracy estimate from HDOP (very rough):
      // hAcc ≈ HDOP * 5m, vAcc ≈ hAcc * 1.5
      // Only apply if we do NOT already have GST-derived accuracy.
      if (hd > 0.0f && g_gps.accSource != 1) {
        const float h = hd * 5.0f;
        const float v = h * 1.5f;
        g_gps.hAcc_m    = h;
        g_gps.vAcc_m    = v;
        g_gps.accSource = 2;      // HDOP-est
        g_gps.accLastMs = nowMs;
      }
      unlock();
    } break;

    // ---------------- GST: GPS Pseudorange Noise Statistics ----------------
    // Contains: standard deviations for latitude/longitude/altitude (meters).
    // This is the best NMEA source for accuracy to display in the UI.
    case MINMEA_SENTENCE_GST: {
      struct minmea_sentence_gst gst;
      if (!minmea_parse_gst(&gst, line)) break;

      const float sigma_lat = (float)minmea_tofloat(&gst.latitude_error_deviation);
      const float sigma_lon = (float)minmea_tofloat(&gst.longitude_error_deviation);
      const float sigma_alt = (float)minmea_tofloat(&gst.altitude_error_deviation);

      if (sigma_lat > 0.0f && sigma_lon > 0.0f) {
        const float h = sqrtf((sigma_lat * sigma_lat) + (sigma_lon * sigma_lon));
        const float v = (sigma_alt > 0.0f) ? sigma_alt : 0.0f;

        lock();
        g_gps.hAcc_m    = h;
        g_gps.vAcc_m    = v;
        g_gps.accSource = 1;       // GST
        g_gps.accLastMs = nowMs;
        unlock();
      }
    } break;

    // ---------------- GSV: Satellites in View ----------------
    // Contains: per-satellite PRN, elevation, azimuth, and SNR.
    // Multi-message: each message carries up to 4 satellites; sequences repeat
    // per constellation (GP, GL, GA, GB).
    case MINMEA_SENTENCE_GSV: {
      struct minmea_sentence_gsv gsv;
      if (!minmea_parse_gsv(&gsv, line)) break;

      // Derive constellation from the raw talker ID in the NMEA line
      // (e.g. $GPGSV → 'G','P' → GPS).
      const uint8_t cons = talker_to_constellation(line[1], line[2]);

      if (gsv.msg_nr == 1) {
        g_gsv_buf[cons].count = 0;
        g_gsv_buf[cons].total_msgs = (uint8_t)gsv.total_msgs;
      }

      // Accumulate satellites from this GSV message (up to 4 per message).
      for (int i = 0; i < 4; i++) {
        if (gsv.sats[i].nr == 0) continue; // empty slot
        buffer_sat(cons, gsv.sats[i]);
      }

      if (gsv.msg_nr == gsv.total_msgs && gsv.total_msgs > 0) {
        g_gsv_buf[cons].lastUpdateMs = nowMs;
        rebuild_live_sat_list(nowMs);
      }
    } break;

    // Any other sentence type is ignored.
    default:
      break;
  }
}

// ================= Byte feeder =================
//
// Feed a single byte from the NMEA stream.
// This function maintains the line collector state and calls process_line()
// whenever a full line ending with '\n' is formed.
static inline void feed_byte(uint8_t b, uint32_t nowMs) {
  const char c = (char)b;

  // '$' marks the start of a new NMEA sentence. Reset the collector.
  if (c == '$') {
    g_len = 0;
    g_line[g_len++] = c;
    return;
  }

  // If we haven't seen '$' yet, ignore everything.
  if (g_len == 0) return;

  // Ignore carriage return (NMEA lines often end with "\r\n").
  if (c == '\r') return;

  // '\n' terminates the line: finalize buffer, parse, then reset collector.
  if (c == '\n') {
    g_line[g_len] = 0;           // null-terminate
    process_line(g_line, nowMs); // parse and update state
    g_len = 0;                   // ready for next sentence
    return;
  }

  // Normal character: append if space remains.
  // Keep one byte for '\0' terminator.
  if (g_len < (int)sizeof(g_line) - 1) {
    g_line[g_len++] = c;
  } else {
    // Overflow: drop the sentence (likely malformed/too long) and resync.
    g_len = 0;
  }
}

// ================= Public API =================
//
// These functions are called by the rest of your project:
//
// - nmea_begin():       reset internal state at boot
// - nmea_feed_bytes():  feed raw UART bytes from GNSS
// - nmea_get_snapshot():copy the latest state into an output struct (thread-safe-ish)

void nmea_begin() {
  // Reset all GPS fields to defaults.
  lock();
  g_gps = GpsState{};
  unlock();

  // Reset the line collector and GSV staging buffer.
  g_len = 0;
  memset(g_gsv_buf, 0, sizeof(g_gsv_buf));
  // Reset last GGA storage
  g_last_gga[0] = '\0';
}

void nmea_feed_bytes(const uint8_t* data, size_t len, uint32_t nowMs) {
  // Defensive checks: null pointer or empty input means nothing to do.
  if (!data || len == 0) return;

  // Feed each byte to the collector.
  // nowMs is passed through so we can stamp freshness at the moment of parsing.
  for (size_t i = 0; i < len; i++) feed_byte(data[i], nowMs);
}

bool nmea_get_snapshot(NmeaGpsSnapshot& out) {
  // We'll copy g_gps into out while locked, then compute ageMs outside the lock.
  uint32_t last = 0;
  const uint32_t now = millis();

  // Rebuild live satellite list, skipping stale constellations.
  rebuild_live_sat_list(now);

  lock();

  // Copy core navigation fields.
  // Note: you intentionally keep `valid` as is, even though GNSS may report RMC validity
  // differently during RTK. Your HTML logic handles that interpretation.
  out.valid      = g_gps.valid; // GNSS considers non valid when RTK. Response changed in html to include RTK in the validaty domain
  out.lat        = g_gps.lat;
  out.lon        = g_gps.lon;
  out.speedKmh   = g_gps.speedKmh;

  // Copy fix/satellite fields.
  out.satsUsed   = g_gps.satsUsed;
  out.fixQuality = g_gps.fixQuality;
  out.fixType    = g_gps.fixType;
  out.hdop       = g_gps.hdop;
  out.hAcc_m     = g_gps.hAcc_m;
  out.vAcc_m     = g_gps.vAcc_m;
  out.accSource  = g_gps.accSource;

  // Copy time/date fields.
  out.timeValid  = g_gps.timeValid;
  out.hour       = g_gps.hour;
  out.minute     = g_gps.minute;
  out.second     = g_gps.second;

  out.year       = g_gps.year;
  out.month      = g_gps.month;
  out.day        = g_gps.day;

  // Copy satellite details.
  out.satCount = g_gps.satCount;
  if (g_gps.satCount > 0) {
    memcpy(out.sats, g_gps.sats, g_gps.satCount * sizeof(NmeaSatInfo));
  }

  // Capture last update timestamp for freshness computation.
  last = g_gps.lastMs;

  unlock();

  // Compute "age" in milliseconds:
  // - If we've never seen a valid sentence, last == 0 -> ageMs = 0
  // - Otherwise ageMs = now - last
  out.ageMs = (last == 0) ? 0 : (now - last);

   return true;
}

bool nmea_get_last_gga(String& out) {
  lock();
  out = String(g_last_gga);
  unlock();
  return out.length() > 0;
}
#endif
