#if NMEA_ENABLE
#include "nmea_gps.h"

extern "C" {
  #include "minmea.h"
}

// ================= Internal GPS state =================
struct GpsState {
  bool     valid       = false;
  double   lat         = 0.0;
  double   lon         = 0.0;
  float    speedKmh    = 0.0f;

  bool     timeValid   = false;
  uint8_t  hour        = 0;
  uint8_t  minute      = 0;
  uint8_t  second      = 0;

  uint16_t year        = 0;
  uint8_t  month       = 0;
  uint8_t  day         = 0;

  uint8_t  satsUsed    = 0;
  uint8_t  fixQuality  = 0;
  uint8_t  fixType     = 0;
  float    hdop        = 0.0f;

  uint32_t lastMs      = 0;
};

static GpsState g_gps;

// Line collector (Option B)
static char g_line[96];
static int  g_len = 0;

// Protect against torn reads/writes (double can tear)
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static inline void lock()   { portENTER_CRITICAL(&g_mux); }
static inline void unlock() { portEXIT_CRITICAL(&g_mux);  }

static inline void process_line(const char* line, uint32_t nowMs) {
  if (!line || line[0] != '$') return;

  // Verify checksum (recommended)
  if (!minmea_check(line, true)) return;

  // Update freshness
  lock();
  g_gps.lastMs = nowMs;
  unlock();

  switch (minmea_sentence_id(line, false)) {

    case MINMEA_SENTENCE_RMC: {
      struct minmea_sentence_rmc rmc;
      if (!minmea_parse_rmc(&rmc, line)) break;

      //const bool ok = (rmc.valid == 'A');
      const bool ok = rmc.valid;
      const double lat = minmea_tocoord(&rmc.latitude);
      const double lon = minmea_tocoord(&rmc.longitude);

      const float speedKnots = (float)minmea_tofloat(&rmc.speed);
      const float speedKmh   = speedKnots * 1.852f;

      const uint8_t hh = (uint8_t)rmc.time.hours;
      const uint8_t mm = (uint8_t)rmc.time.minutes;
      const uint8_t ss = (uint8_t)rmc.time.seconds;

      // NMEA year is typically 2-digit -> assume 2000+
      const uint8_t dd = (uint8_t)rmc.date.day;
      const uint8_t mo = (uint8_t)rmc.date.month;
      const uint16_t yy = (uint16_t)(2000 + rmc.date.year);

      lock();
      g_gps.valid = ok;
      g_gps.lat = lat;
      g_gps.lon = lon;
      g_gps.speedKmh = speedKmh;

      g_gps.timeValid = true;
      g_gps.hour = hh;
      g_gps.minute = mm;
      g_gps.second = ss;

      g_gps.day = dd;
      g_gps.month = mo;
      g_gps.year = yy;
      unlock();
    } break;

    case MINMEA_SENTENCE_GGA: {
      struct minmea_sentence_gga gga;
      if (!minmea_parse_gga(&gga, line)) break;

      const uint8_t fixQ = (uint8_t)gga.fix_quality;
      const uint8_t sats = (uint8_t)gga.satellites_tracked;
      const float hd     = (float)minmea_tofloat(&gga.hdop);

      lock();
      g_gps.fixQuality = fixQ;
      g_gps.satsUsed   = sats;
      if (hd > 0.0f) g_gps.hdop = hd;

      // Fallback time if RMC not received yet
      if (!g_gps.timeValid) {
        g_gps.timeValid = true;
        g_gps.hour   = (uint8_t)gga.time.hours;
        g_gps.minute = (uint8_t)gga.time.minutes;
        g_gps.second = (uint8_t)gga.time.seconds;
      }
      unlock();
    } break;

    case MINMEA_SENTENCE_GSA: {
      struct minmea_sentence_gsa gsa;
      if (!minmea_parse_gsa(&gsa, line)) break;

      const uint8_t fixT = (uint8_t)gsa.fix_type;
      const float hd     = (float)minmea_tofloat(&gsa.hdop);

      lock();
      g_gps.fixType = fixT;
      if (hd > 0.0f) g_gps.hdop = hd;
      unlock();
    } break;

    default:
      break;
  }
}

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
    g_len = 0; // overflow -> drop
  }
}

// ================= Public API =================
void nmea_begin() {
  lock();
  g_gps = GpsState{};
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

  last = g_gps.lastMs;
  unlock();

  const uint32_t now = millis();
  out.ageMs = (last == 0) ? 0 : (now - last);
  return true;
}
#endif