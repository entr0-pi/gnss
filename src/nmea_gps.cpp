#if NMEA_ENABLE
#include "nmea_gps.h"   // Your public snapshot struct + function declarations

// minmea is a small, fast NMEA parser library (C).
// We include it as C to avoid C++ name mangling issues.
extern "C" {
  #include "minmea.h"
}

#include <math.h>

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

  // ---- Freshness tracking ----
  // Timestamp (millis) of the last valid NMEA sentence we accepted (checksum OK + parsed OK).
  uint32_t lastMs      = 0;
};

// Global instance holding the latest state.
static GpsState g_gps;

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

  // Reset the line collector state too.
  g_len = 0;
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

  // Capture last update timestamp for freshness computation.
  last = g_gps.lastMs;

  unlock();

  // Compute "age" in milliseconds:
  // - If we've never seen a valid sentence, last == 0 -> ageMs = 0
  // - Otherwise ageMs = now - last
  const uint32_t now = millis();
  out.ageMs = (last == 0) ? 0 : (now - last);

  return true;
}
#endif
