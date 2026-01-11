#if NMEA_ENABLE
#include "nmea_gps.h"

extern "C" {
  #include "minmea.h"
}

// ================= Internal GPS state =================
struct GpsState {
  // ---- Position validity + kinematics ----
  bool     valid       = false;
  double   lat         = 0.0;
  double   lon         = 0.0;
  float    speedKmh    = 0.0f;

  // ---- Time (UTC) ----
  bool     timeValid   = false;
  uint8_t  hour        = 0;
  uint8_t  minute      = 0;
  uint8_t  second      = 0;

  // ---- Date (UTC) ----
  uint16_t year        = 0;
  uint8_t  month       = 0;
  uint8_t  day         = 0;

  // ---- Fix quality / satellites / dilution ----
  uint8_t  satsUsed    = 0;
  uint8_t  fixQuality  = 0;
  uint8_t  fixType     = 0;
  float    hdop        = 0.0f;

  // ---- Freshness tracking ----
  uint32_t lastMs      = 0;

  // ---- NEW: Satellites in view (GSV) + used list (GSA) ----
  static constexpr uint8_t MAX_SATS = NmeaGpsSnapshot::MAX_SATS;

  uint8_t    satsInView = 0;
  NmeaSatInfo sats[MAX_SATS];

  uint16_t usedPrn[16];
  uint8_t  usedCount = 0;

  uint32_t lastGsvMs = 0;   // last time we processed any GSV
  uint32_t lastGsaMs = 0;   // last time we processed any GSA
};

static GpsState g_gps;

// ================= Line collector =================
// GSV lines can be longer than 82 chars; give more headroom.
static char g_line[128];
static int  g_len = 0;

// ================= Concurrency protection =================
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static inline void lock()   { portENTER_CRITICAL(&g_mux); }
static inline void unlock() { portEXIT_CRITICAL(&g_mux);  }

// ================= Helpers (must be called while locked) =================
static inline bool prn_is_used_nolock(uint16_t prn) {
  for (uint8_t i = 0; i < g_gps.usedCount; i++) {
    if (g_gps.usedPrn[i] == prn) return true;
  }
  return false;
}

static inline void clear_sats_nolock() {
  g_gps.satsInView = 0;
  for (uint8_t i = 0; i < GpsState::MAX_SATS; i++) {
    g_gps.sats[i] = NmeaSatInfo{0,0,0,-1,false};
  }
}

static inline void add_or_update_sat_nolock(uint16_t prn, uint8_t elev, uint16_t az, int8_t snr) {
  if (prn == 0) return;

  // Update existing if already present
  for (uint8_t i = 0; i < g_gps.satsInView; i++) {
    if (g_gps.sats[i].prn == prn) {
      g_gps.sats[i].elevDeg = elev;
      g_gps.sats[i].azDeg   = az;
      g_gps.sats[i].snrDbHz = snr;
      g_gps.sats[i].used    = prn_is_used_nolock(prn);
      return;
    }
  }

  // Append new
  if (g_gps.satsInView < GpsState::MAX_SATS) {
    const uint8_t idx = g_gps.satsInView++;
    g_gps.sats[idx].prn      = prn;
    g_gps.sats[idx].elevDeg  = elev;
    g_gps.sats[idx].azDeg    = az;
    g_gps.sats[idx].snrDbHz  = snr;
    g_gps.sats[idx].used     = prn_is_used_nolock(prn);
  }
}

static inline void mark_used_flags_nolock() {
  for (uint8_t i = 0; i < g_gps.satsInView; i++) {
    const uint16_t prn = g_gps.sats[i].prn;
    g_gps.sats[i].used = prn_is_used_nolock(prn);
  }
}

static inline void add_used_prn_nolock(uint16_t prn) {
  if (prn == 0) return;
  for (uint8_t i = 0; i < g_gps.usedCount; i++) {
    if (g_gps.usedPrn[i] == prn) return; // already present
  }
  if (g_gps.usedCount < (uint8_t)(sizeof(g_gps.usedPrn)/sizeof(g_gps.usedPrn[0]))) {
    g_gps.usedPrn[g_gps.usedCount++] = prn;
  }
}

// ================= Sentence processor =================
static inline void process_line(const char* line, uint32_t nowMs) {
  if (!line || line[0] != '$') return;
  if (!minmea_check(line, true)) return;

  lock();
  g_gps.lastMs = nowMs;
  unlock();

  switch (minmea_sentence_id(line, false)) {

    case MINMEA_SENTENCE_RMC: {
      struct minmea_sentence_rmc rmc;
      if (!minmea_parse_rmc(&rmc, line)) break;

      const bool ok = rmc.valid;

      const double lat = minmea_tocoord(&rmc.latitude);
      const double lon = minmea_tocoord(&rmc.longitude);

      const float speedKnots = (float)minmea_tofloat(&rmc.speed);
      const float speedKmh   = speedKnots * 1.852f;

      const uint8_t hh = (uint8_t)rmc.time.hours;
      const uint8_t mm = (uint8_t)rmc.time.minutes;
      const uint8_t ss = (uint8_t)rmc.time.seconds;

      const uint8_t  dd = (uint8_t)rmc.date.day;
      const uint8_t  mo = (uint8_t)rmc.date.month;
      const uint16_t yy = (uint16_t)(2000 + rmc.date.year);

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

    case MINMEA_SENTENCE_GGA: {
      struct minmea_sentence_gga gga;
      if (!minmea_parse_gga(&gga, line)) break;

      const uint8_t fixQ = (uint8_t)gga.fix_quality;
      const uint8_t sats = (uint8_t)gga.satellites_tracked;
      const float   hd   = (float)minmea_tofloat(&gga.hdop);

      lock();
      g_gps.fixQuality = fixQ;
      g_gps.satsUsed   = sats;
      if (hd > 0.0f) g_gps.hdop = hd;

      if (!g_gps.timeValid) {
        g_gps.timeValid = true;
        g_gps.hour   = (uint8_t)gga.time.hours;
        g_gps.minute = (uint8_t)gga.time.minutes;
        g_gps.second = (uint8_t)gga.time.seconds;
      }
      unlock();
    } break;

    // ---- UPDATED: GSA now extracts the "used satellites" list ----
    case MINMEA_SENTENCE_GSA: {
      struct minmea_sentence_gsa gsa;
      if (!minmea_parse_gsa(&gsa, line)) break;

      const uint8_t fixT = (uint8_t)gsa.fix_type;
      const float   hd   = (float)minmea_tofloat(&gsa.hdop);

      lock();

      g_gps.fixType = fixT;
      if (hd > 0.0f) g_gps.hdop = hd;

      // NEW: start a new "GSA cycle" only if there's a time gap
      const bool newCycle = (g_gps.lastGsaMs == 0) || ((nowMs - g_gps.lastGsaMs) > 1500);
      if (newCycle) {
        g_gps.usedCount = 0;
      }
      g_gps.lastGsaMs = nowMs;

      // Merge PRNs from this sentence
      for (int i = 0; i < 12; i++) {
        add_used_prn_nolock((uint16_t)gsa.sats[i]);
      }

      // Apply used flags to satellites currently in view
      mark_used_flags_nolock();

      unlock();
    } break;


    // ---- NEW: GSV satellites-in-view (azimuth/elevation/SNR) ----
    case MINMEA_SENTENCE_GSV: {
      struct minmea_sentence_gsv gsv;
      if (!minmea_parse_gsv(&gsv, line)) break;

      lock();

      // NEW: detect a new "GSV cycle" by time gap (covers multiple talkers: GP/GL/GA/BD...)
      const bool newCycle = (g_gps.lastGsvMs == 0) || ((nowMs - g_gps.lastGsvMs) > 1500);
      if (newCycle) {
        clear_sats_nolock();
      }
      g_gps.lastGsvMs = nowMs;

      for (int i = 0; i < 4; i++) {
        const uint16_t prn = (uint16_t)gsv.sats[i].nr;
        if (prn == 0) continue;

        const uint8_t  elev = (uint8_t)gsv.sats[i].elevation;
        const uint16_t az   = (uint16_t)gsv.sats[i].azimuth;

        const int snr_raw = gsv.sats[i].snr;
        const int8_t snr = (snr_raw > 0 && snr_raw < 100) ? (int8_t)snr_raw : (int8_t)-1;

        add_or_update_sat_nolock(prn, elev, az, snr);
      }

      // Keep "used" flags consistent with whatever GSA PRNs we have so far
      mark_used_flags_nolock();

      unlock();
    } break;

    default:
      break;
  }
}

// ================= Byte feeder =================
static inline void feed_byte(uint8_t b, uint32_t nowMs) {
  const char c = (char)b;

  if (c == '$') {
    g_len = 0;
    g_line[g_len++] = c;
    return;
  }

  if (g_len == 0) return;
  if (c == '\r') return;

  if (c == '\n') {
    g_line[g_len] = 0;
    process_line(g_line, nowMs);
    g_len = 0;
    return;
  }

  if (g_len < (int)sizeof(g_line) - 1) {
    g_line[g_len++] = c;
  } else {
    g_len = 0; // overflow -> drop and resync
  }
}

// ================= Public API =================
void nmea_begin() {
  lock();
  g_gps = GpsState{};
  clear_sats_nolock();
  unlock();
  g_len = 0;
}

void nmea_feed_bytes(const uint8_t* data, size_t len, uint32_t nowMs) {
  if (!data || len == 0) return;
  for (size_t i = 0; i < len; i++) feed_byte(data[i], nowMs);
}

bool nmea_get_snapshot(NmeaGpsSnapshot& out) {
  uint32_t last = 0;

  lock();

  out.valid      = g_gps.valid;
  out.lat        = g_gps.lat;
  out.lon        = g_gps.lon;
  out.speedKmh   = g_gps.speedKmh;

  out.satsUsed   = g_gps.satsUsed;
  out.fixQuality = g_gps.fixQuality;
  out.fixType    = g_gps.fixType;
  out.hdop       = g_gps.hdop;

  out.timeValid  = g_gps.timeValid;
  out.hour       = g_gps.hour;
  out.minute     = g_gps.minute;
  out.second     = g_gps.second;

  out.year       = g_gps.year;
  out.month      = g_gps.month;
  out.day        = g_gps.day;

  // ---- NEW: copy satellites table ----
  out.satsInView = g_gps.satsInView;
  if (out.satsInView > NmeaGpsSnapshot::MAX_SATS) out.satsInView = NmeaGpsSnapshot::MAX_SATS;

  for (uint8_t i = 0; i < out.satsInView; i++) out.sats[i] = g_gps.sats[i];
  for (uint8_t i = out.satsInView; i < NmeaGpsSnapshot::MAX_SATS; i++) out.sats[i] = NmeaSatInfo{0,0,0,-1,false};

  last = g_gps.lastMs;

  unlock();

  const uint32_t now = millis();
  out.ageMs = (last == 0) ? 0 : (now - last);
  return true;
}
#endif
