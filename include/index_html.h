#pragma once
#include <Arduino.h>

static const uint8_t INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>ESP32-C3 Status</title>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <link rel="icon" href="/favicon.ico">
  <link rel="stylesheet" href="/style.css?v=2">
</head>
<body>
  <div class="wrap">
    <header class="top">
      <div class="title">STATUS REPORT</div>
      <div class="sub">Web Server on ESP32-C3 with UM980</div>
      <div class="status-inline">
        <span class="dot" id="dot"></span>
        <span id="state">connecting…</span>
      </div>
  </div>
    </header>

    <section class="card">
      <div class="grid">
        <div class="kv">
          <div class="k">Uptime</div>
          <div class="v" id="uptime">—</div>
        </div>
        <div class="kv">
          <div class="k">Signal Power</div>
          <div class="v mono" id="rssi">—</div>
        </div>
      </div>      

      <div class="divider"></div>
      
      <div class="actions">
        <button id="restartBtn" class="btn danger">Restart</button>
        <div class="note" id="note">Polling /api/status…</div>
      </div>
      
      <div class="divider"></div>
      
      <div class="cat">
        <div class="catTitle">Device</div>
        <div class="catBody">
          <div class="line"><span class="lk">CPU</span><span class="lv mono" id="cpu">—</span></div>
          <div class="line"><span class="lk">Build</span><span class="lv mono" id="build">—</span></div>
        </div>
      </div>

      <div class="cat">
        <div class="catTitle">Memory</div>
        <div class="catBody">
          <div class="line"><span class="lk">Free</span><span class="lv mono" id="heap_free">—</span></div>
          <div class="line"><span class="lk">Min Free</span><span class="lv mono" id="heap_min">—</span></div>
          <div class="line"><span class="lk">Max Alloc</span><span class="lv mono" id="heap_max">—</span></div>
        </div>
      </div>

      <div class="cat">
        <div class="catTitle">Internet</div>
        <div class="catBody">
          <div class="line"><span class="lk">Reachable</span><span class="lv mono emoji" id="reach">—</span></div>
        </div>
      </div>

      <div class="cat">
        <div class="catTitle">Bluetooth</div>
        <div class="catBody">
          <div class="line"><span class="lk">Connected</span><span class="lv mono emoji" id="ble_connected">—</span></div>
          <div class="line ble-metric"><span class="lk">MTU</span><span class="lv mono" id="ble_mtu">—</span></div>
          <div class="line ble-metric"><span class="lk">NMEA</span><span class="lv mono" id="ble_txBytes">—</span></div>
          <div class="line ble-metric"><span class="lk">NTRIP</span><span class="lv mono" id="ble_rxBytes">—</span></div>
        </div>
      </div>

      <div class="cat">
        <div class="catTitle">GPS</div>
        <div class="catBody">
          <div class="line"><span class="lk">Validity</span><span class="lv mono emoji" id="gps_valid">—</span></div>
          <div class="line"><span class="lk">Fix type</span><span class="lv mono" id="gps_fix">—</span></div>
          <div class="line"><span class="lk">Sat in Use</span><span class="lv mono" id="gps_sats">—</span></div>
          <div class="line"><span class="lk">UTC</span><span class="lv mono" id="gps_utc">—</span></div>
          <div class="line"><span class="lk">Lat</span><span class="lv mono" id="gps_lat">—</span></div>
          <div class="line"><span class="lk">Lon</span><span class="lv mono" id="gps_lon">—</span></div>
          <div class="line"><span class="lk">Speed</span><span class="lv mono" id="gps_speed">—</span></div>
          <div class="line"><span class="lk">HDOP</span><span class="lv mono" id="gps_hdop">—</span></div>
          <div class="line"><span class="lk">Age</span><span class="lv mono" id="gps_age">—</span></div>
        </div>
      </div>


      <div class="cat">
        <div class="catTitle">Wi-Fi (in <span id="wmode">—</span> mode)</div>
        <div class="catBody">
          <div class="line"><span class="lk">SSID</span><span class="lv mono" id="ssid">—</span></div>
          <div class="line"><span class="lk">IP</span><span class="lv mono" id="ip2">—</span></div>
          <div class="line"><span class="lk">Gateway</span><span class="lv mono" id="gw">—</span></div>
          <div class="line"><span class="lk">DNS</span><span class="lv mono" id="dns">—</span></div>
          <div class="line"><span class="lk">Subnet</span><span class="lv mono" id="subnet">—</span></div>
          <div class="line"><span class="lk">Broadcast</span><span class="lv mono" id="bcast">—</span></div>
          <div class="line"><span class="lk">MAC</span><span class="lv mono" id="mac">—</span></div>
        </div>
      </div>

      <div class="cat">
        <div class="catTitle">HTTP</div>
        <div class="catBody">
          <div class="line"><span class="lk">Port</span><span class="lv mono" id="port">—</span></div>
          <div class="line"><span class="lk">Requests</span><span class="lv mono" id="reqs">—</span></div>
          <div class="line"><span class="lk">Prev Req Age</span><span class="lv mono" id="age">—</span></div>
        </div>
      </div>

      <div class="cat">
        <div class="catTitle">App</div>
        <div class="catBody">
          <div class="line"><span class="lk">State</span><span class="lv mono" id="app_state">—</span></div>
          <div class="line"><span class="lk">Notes</span><span class="lv mono" id="app_notes">—</span></div>
        </div>
      </div>

    </section>

    <footer class="foot">
    </footer>
  </div>

<script>
const $ = (id) => document.getElementById(id);

let valeurAvg = null;  // exponential moving average

function setBleConnected(ok = false) {
  const el = document.getElementById("ble_connected");
  el.textContent = "";
  el.classList.remove("ok", "bad");
  if (ok) {el.classList.add("ok");} 
  else {el.classList.add("bad");}
}
function setInternetConnected(ok = false) {
  const el = document.getElementById("reach");
  el.textContent = "";
  el.classList.remove("ok", "bad");
  if (ok) {el.classList.add("ok");} 
  else {el.classList.add("bad");}
}  
function setGPSConnected(ok = false) {
  const el = document.getElementById("gps_valid");
  el.textContent = "";
  el.classList.remove("ok", "bad");
  if (ok) {el.classList.add("ok");} 
  else {el.classList.add("bad");}
}

function updateColorRssi(valeur) {
  const el = $("rssi");
  if (!el) return;

  // Accept number OR numeric string
  const n = (typeof valeur === "number") ? valeur : Number(valeur);

  if (!Number.isFinite(n)) {
    el.textContent = "—";
    el.classList.remove("good","ok","bad","dead");
    el.classList.add("unknown");
    valeurAvg = null;
    return;
  }

  el.textContent = n + " dBm";

  // Smooth
  if (valeurAvg === null) valeurAvg = n;
  valeurAvg = 0.7 * valeurAvg + 0.3 * n;

  el.classList.remove("good","ok","bad","dead","unknown");

  if (valeurAvg >= -50)      el.classList.add("good");
  else if (valeurAvg >= -65) el.classList.add("ok");
  else if (valeurAvg >= -75) el.classList.add("bad");
  else                       el.classList.add("dead");
}

function fmtBytes(n){
  if (!Number.isFinite(n)) return "—";
  if (n < 1024) return n + " B";
  if (n < 1024*1024) return (n/1024).toFixed(1) + " KB";
  return (n/(1024*1024)).toFixed(2) + " MB";
}
function fmtUptime(ms){
  const s = Math.floor(ms/1000);
  if (s < 60) return s + " s";
  const m = Math.floor(s/60);
  if (m < 60) return m + " m " + (s%60) + " s";
  const h = Math.floor(m/60);
  return h + " h " + (m%60) + " m";
}

function safe(v, fallback="—"){
  return (v === undefined || v === null || v === "") ? fallback : v;
}

function safe_ok(v, fallback=false){
  return (v === undefined || v === null || v === "") ? fallback : v;
}

async function refresh(){
  try {
    const r = await fetch('/api/status', { cache:'no-store' });
    if (!r.ok) throw new Error("http " + r.status);
    const s = await r.json();

    $('uptime').textContent = fmtUptime(s.device?.uptime_ms ?? NaN);
    updateColorRssi(s.wifi?.rssi_dbm);

    $('cpu').textContent   = safe((s.device?.cpu_mhz !== undefined) ? (s.device.cpu_mhz + " MHz") : undefined);
    $('build').textContent = safe(s.device?.build);

    $('heap_free').textContent = fmtBytes(s.memory?.heap_free);
    $('heap_min').textContent  = fmtBytes(s.memory?.heap_min_free);
    $('heap_max').textContent  = fmtBytes(s.memory?.heap_max_alloc);

    setBleConnected(safe_ok(s.ble?.connected));
    $('ble_mtu').textContent    = safe(s.ble?.mtu);
    $('ble_txBytes').textContent   = fmtBytes(s.ble?.txBytes);
    $('ble_rxBytes').textContent= fmtBytes(s.ble?.rxBytes);
    
    setGPSConnected(safe_ok(s.gps?.valid));
    $('gps_fix').textContent   = safe(
      (s.gps?.fix_type && s.gps?.fix_quality) ? (s.gps.fix_type + " / " + s.gps.fix_quality) : undefined
    );
    $('gps_sats').textContent  = safe(s.gps?.sats_used);
    $('gps_hdop').textContent  = safe((s.gps?.hdop !== undefined) ? s.gps.hdop : undefined);

    $('gps_lat').textContent   = safe((s.gps?.lat !== undefined) ? s.gps.lat : undefined);
    $('gps_lon').textContent   = safe((s.gps?.lon !== undefined) ? s.gps.lon : undefined);

    $('gps_speed').textContent = safe((s.gps?.speed_kmh !== undefined) ? (s.gps.speed_kmh + " km/h") : undefined);

    const utc = (s.gps?.date_utc && s.gps?.time_utc) ? (s.gps.date_utc + " " + s.gps.time_utc) : undefined;
    $('gps_utc').textContent   = safe(utc);

    $('gps_age').textContent   = safe((s.gps?.age_ms !== undefined) ? (s.gps.age_ms + " ms") : undefined);


    $('wmode').textContent = safe(s.wifi?.mode);
    $('ssid').textContent  = safe(s.wifi?.ssid);
    $('ip2').textContent   = safe(s.wifi?.ip);
    $('gw').textContent    = safe(s.wifi?.gw);
    $('dns').textContent   = safe(s.wifi?.dns);
    $('subnet').textContent= safe(s.wifi?.subnet);
    $('bcast').textContent = safe(s.wifi?.broadcast);
    $('mac').textContent = safe(s.wifi?.mac);

    $('port').textContent = safe(s.http?.port);
    $('reqs').textContent = safe(s.http?.req_total);
    $('age').textContent  = safe((s.http?.prev_req_age_ms !== undefined) ? (s.http.prev_req_age_ms + " ms") : undefined);

    setInternetConnected(safe_ok(s.internet?.reach));    

    $('app_state').textContent = safe(s.app?.state);
    $('app_notes').textContent = safe(s.app?.notes);

    $('state').textContent = "online";
    $('dot').className = "dot ok";
    $('note').textContent = "Last update: " + new Date().toLocaleTimeString();

  } catch (e) {
    $('state').textContent = "offline";
    $('dot').className = "dot bad";
    $('note').textContent = "Failed to reach device";

    updateColorRssi(NaN);

  }
}

$('restartBtn').addEventListener('click', async () => {
  if (!confirm("Restart ESP32-C3 now?")) return;
  $('note').textContent = "Restarting…";
  try { await fetch('/api/restart', { method:'POST' }); } catch(e) {}
  setTimeout(refresh, 1500);
});

refresh();
setInterval(refresh, 1000);
</script>

</body>
</html>
)HTML";

static const size_t INDEX_HTML_LEN = sizeof(INDEX_HTML) - 1;
