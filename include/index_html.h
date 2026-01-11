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
  <link rel="stylesheet" href="/style.css?v=11">
</head>
<body>
  <div class="wrap">
    <header class="top">
      <div class="title">STATUS REPORT</div>
      <div class="sub">Web Server on ESP32-C3 with UM980</div>
      <!-- Dot online -->
      <div class="status-inline">
        <span class="dot" id="dot"></span>
        <span id="state">connecting…</span>
      </div>
      <!-- Tabs -->
      <nav class="tabs" aria-label="Sections">
        <button class="tabBtn isActive" data-tab="tab_status" type="button">Overview</button>
        <button class="tabBtn" data-tab="tab_gps" type="button">GPS</button>
        <button class="tabBtn" data-tab="tab_device" type="button">Device</button>
        <button class="tabBtn" data-tab="tab_ble" type="button">Bluetooth</button>
        <button class="tabBtn" data-tab="tab_wifi" type="button">WiFi</button>

      </nav>
    </header>

    <section class="card">

      <!-- ===================== TAB 1: STATUS + ICONS ===================== -->
      <div class="tabPanel isActive" id="tab_status">
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

        <div class="cat">
          <div class="catBody">
            <div class="line"><span class="lk">Bluetooth Connected</span><span class="lv mono emoji" id="ble_connected">—</span></div>
            <div class="line"><span class="lk">GPS Connected</span><span class="lv mono emoji" id="gps_valid">—</span></div>
            <div class="line"><span class="lk">GPS Quality</span><span class="lv mono emoji" id="gps_fix">—</span></div>
          </div>
        </div>

        <div class="divider"></div>

        <div class="actions">
          <button id="restartBtn" class="btn danger">Restart</button>
          <div class="note" id="note">Polling /api/status…</div>
        </div>
      </div>

      <!-- ===================== TAB 2: GPS ===================== -->
      <div class="tabPanel" id="tab_gps">
        <div class="cat">
          <div class="catTitle">GPS</div>
          <div class="catBody">
            <div class="line"><span class="lk">Validity</span><span class="lv mono emoji" id="gps_valid2">—</span></div>
            <div class="line"><span class="lk">Fix type</span><span class="lv mono" id="gps_fix2">—</span></div>
            <div class="line"><span class="lk">Sat in Use</span><span class="lv mono" id="gps_sats">—</span></div>
            <div class="line"><span class="lk">UTC</span><span class="lv mono" id="gps_utc">—</span></div>
            <div class="line"><span class="lk">Lat</span><span class="lv mono" id="gps_lat">—</span></div>
            <div class="line"><span class="lk">Lon</span><span class="lv mono" id="gps_lon">—</span></div>
            <div class="line"><span class="lk">Speed</span><span class="lv mono" id="gps_speed">—</span></div>
            <div class="line"><span class="lk">HDOP</span><span class="lv mono" id="gps_hdop">—</span></div>
            <div class="line"><span class="lk">Age</span><span class="lv mono" id="gps_age">—</span></div>
          </div>
        </div>

        <div class="divider"></div>

        <div class="cat">
          <div class="catTitle">Skyplot</div>
          <div class="catBody">
            <div class="skyGrid">
              <div class="skyBox">
                <svg id="skyplot" class="skySvg" viewBox="-110 -110 220 220" aria-label="Skyplot"></svg>
                <div class="skyHint">
                  <span class="pill used">Used</span>
                  <span class="pill">In view</span>
                  <span class="pill faint">No SNR</span>
                </div>
              </div>

              <div>
                <div class="satTableTitle">Satellites</div>
                <div id="satTable" class="satTable">—</div>
              </div>
            </div>
          </div>
        </div>

      </div>

      <!-- ===================== TAB 3: DEVICE ===================== -->
      <div class="tabPanel" id="tab_device">

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

      </div>

      <!-- ===================== TAB 4: MEMORY + BLUETOOTH ===================== -->
      <div class="tabPanel" id="tab_ble">

        <div class="cat">
          <div class="catTitle">Bluetooth</div>
          <div class="catBody">
            <div class="line"><span class="lk">Connected</span><span class="lv mono emoji" id="ble_connected2">—</span></div>
            <div class="line ble-metric"><span class="lk">MTU</span><span class="lv mono" id="ble_mtu">—</span></div>
            <div class="line ble-metric"><span class="lk">NMEA</span><span class="lv mono" id="ble_txBytes">—</span></div>
            <div class="line ble-metric"><span class="lk">NTRIP</span><span class="lv mono" id="ble_rxBytes">—</span></div>
          </div>
        </div>

      </div>

      <!-- ===================== TAB 5: WiFi ==================== -->
      <div class="tabPanel" id="tab_wifi">

        <div class="cat">
          <div class="catTitle">Internet</div>
          <div class="catBody">
            <div class="line"><span class="lk">Reachable</span><span class="lv mono emoji" id="reach2">—</span></div>
          </div>
        </div>
      
        <div class="cat">
          <div class="catTitle">Wi-Fi</div>
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

      </div>

    </section>

    <footer class="foot"></footer>
  </div>

<script>
const $ = (id) => document.getElementById(id);

let valeurAvg = null;  // exponential moving average

function setIcon(id, ok = false) {
  const el = document.getElementById(id);
  if (!el) return;
  el.textContent = "";
  el.classList.remove("ok", "bad");
  el.classList.add(ok ? "ok" : "bad");
}

function updateColorRssi(valeur) {
  const el = $("rssi");
  if (!el) return;

  const n = (typeof valeur === "number") ? valeur : Number(valeur);

  if (!Number.isFinite(n)) {
    el.textContent = "—";
    el.classList.remove("good","ok","bad","dead");
    el.classList.add("unknown");
    valeurAvg = null;
    return;
  }

  el.textContent = n + " dBm";

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

function clamp(n, a, b){ return Math.max(a, Math.min(b, n)); }

// Convert sat data (az/elev) to skyplot X/Y
// az: degrees from North clockwise. elev: 0..90 (90 is zenith)
// We map radius: r = (90 - elev) so:
//   elev=90 -> r=0 (center)
//   elev=0  -> r=90 (outer ring)
function skyXY(azDeg, elevDeg){
  const az = (Number(azDeg) || 0) * Math.PI / 180.0;
  const elev = clamp(Number(elevDeg) || 0, 0, 90);
  const r = (90 - elev); // 0..90

  // SVG: +x to right, +y down
  // Want North at top => y negative for North
  const x = r * Math.sin(az);
  const y = -r * Math.cos(az);
  return {x, y, r};
}

function satDotRadius(snr){
  const n = Number(snr);
  if (!Number.isFinite(n) || n < 0) return 3.0;
  // map 0..60 => 3..8
  return 3.0 + clamp(n, 0, 60) * (5.0/60.0);
}

function drawSkyBase(svg){
  // Clear
  while (svg.firstChild) svg.removeChild(svg.firstChild);

  // Rings at elev 0/30/60/90 => r 90/60/30/0
  const rings = [90, 60, 30];
  rings.forEach(r => {
    const c = document.createElementNS("http://www.w3.org/2000/svg","circle");
    c.setAttribute("cx","0"); c.setAttribute("cy","0"); c.setAttribute("r", String(r));
    c.setAttribute("class","skyRing");
    svg.appendChild(c);
  });

  // Crosshair
  const l1 = document.createElementNS("http://www.w3.org/2000/svg","line");
  l1.setAttribute("x1","-90"); l1.setAttribute("y1","0");
  l1.setAttribute("x2","90");  l1.setAttribute("y2","0");
  l1.setAttribute("class","skyAxis");
  svg.appendChild(l1);

  const l2 = document.createElementNS("http://www.w3.org/2000/svg","line");
  l2.setAttribute("x1","0");  l2.setAttribute("y1","-90");
  l2.setAttribute("x2","0");  l2.setAttribute("y2","90");
  l2.setAttribute("class","skyAxis");
  svg.appendChild(l2);

  // Cardinal labels
  const labels = [
    {t:"N", x:0,   y:-98},
    {t:"E", x:98,  y:4},
    {t:"S", x:0,   y:108},
    {t:"W", x:-98, y:4},
  ];
  labels.forEach(o => {
    const tx = document.createElementNS("http://www.w3.org/2000/svg","text");
    tx.textContent = o.t;
    tx.setAttribute("x", String(o.x));
    tx.setAttribute("y", String(o.y));
    tx.setAttribute("class","skyCard");
    tx.setAttribute("text-anchor","middle");
    svg.appendChild(tx);
  });

  // Elev labels (optional small)
  const elevLbl = [
    {t:"30°", y:-60},
    {t:"60°", y:-30},
  ];
  elevLbl.forEach(o => {
    const tx = document.createElementNS("http://www.w3.org/2000/svg","text");
    tx.textContent = o.t;
    tx.setAttribute("x","6");
    tx.setAttribute("y", String(o.y));
    tx.setAttribute("class","skyElev");
    svg.appendChild(tx);
  });
}

function renderSkyplot(sats){
  const svg = $("skyplot");
  const table = $("satTable");
  if (!svg || !table) return;

  const list = Array.isArray(sats) ? sats : [];

  drawSkyBase(svg);

  // Dots
  list.forEach(s => {
    const prn = Number(s?.prn);
    if (!Number.isFinite(prn) || prn <= 0) return;

    const elev = Number(s?.elev);
    const az   = Number(s?.az);
    const snr  = Number(s?.snr);
    const used = !!s?.used;

    const p = skyXY(az, elev);
    const g = document.createElementNS("http://www.w3.org/2000/svg","g");
    g.setAttribute("class", used ? "sat used" : "sat");

    const c = document.createElementNS("http://www.w3.org/2000/svg","circle");
    c.setAttribute("cx", String(p.x));
    c.setAttribute("cy", String(p.y));
    c.setAttribute("r", String(satDotRadius(snr)));
    c.setAttribute("class", (Number.isFinite(snr) && snr >= 0) ? "satDot" : "satDot faint");
    g.appendChild(c);

    const tx = document.createElementNS("http://www.w3.org/2000/svg","text");
    tx.textContent = String(prn);
    tx.setAttribute("x", String(p.x));
    tx.setAttribute("y", String(p.y + 3)); // optical alignment
    tx.setAttribute("text-anchor","middle");
    tx.setAttribute("class","satLbl");
    g.appendChild(tx);

    // Tooltip
    const title = document.createElementNS("http://www.w3.org/2000/svg","title");
    const snrTxt = (Number.isFinite(snr) && snr >= 0) ? (snr + " dB-Hz") : "—";
    title.textContent = `PRN ${prn} | elev ${elev}° | az ${az}° | SNR ${snrTxt}` + (used ? " | USED" : "");
    g.appendChild(title);

    svg.appendChild(g);
  });

  // Table (sorted: used first, then snr desc)
  const sorted = list
    .filter(s => Number.isFinite(Number(s?.prn)) && Number(s.prn) > 0)
    .slice()
    .sort((a,b) => {
      const au = a?.used ? 1 : 0, bu = b?.used ? 1 : 0;
      if (au !== bu) return bu - au;
      const as = Number.isFinite(Number(a?.snr)) ? Number(a.snr) : -1;
      const bs = Number.isFinite(Number(b?.snr)) ? Number(b.snr) : -1;
      return bs - as;
    });

  if (sorted.length === 0) {
    table.textContent = "—";
    return;
  }

  let html = `<div class="satRow satHead">
    <div>PRN</div><div>El</div><div>Az</div><div>SNR</div><div>Used</div>
  </div>`;

  sorted.forEach(s => {
    const prn = Number(s.prn);
    const elev = Number(s.elev);
    const az = Number(s.az);
    const snr = Number(s.snr);
    const used = !!s.used;

    const snrTxt = (Number.isFinite(snr) && snr >= 0) ? snr : "—";
    html += `<div class="satRow ${used ? "isUsed" : ""}">
      <div class="mono">${prn}</div>
      <div class="mono">${Number.isFinite(elev) ? elev : "—"}</div>
      <div class="mono">${Number.isFinite(az) ? az : "—"}</div>
      <div class="mono">${snrTxt}</div>
      <div class="mono">${used ? "✅" : ""}</div>
    </div>`;
  });

  table.innerHTML = html;
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

    // Update icons in BOTH places (tab 1 + tab 2/3 duplicates)
    const bleOk = safe_ok(s.ble?.connected);
    setIcon("ble_connected", bleOk);
    setIcon("ble_connected2", bleOk);

    $('ble_mtu').textContent      = safe(s.ble?.mtu);
    $('ble_txBytes').textContent  = fmtBytes(s.ble?.txBytes);
    $('ble_rxBytes').textContent  = fmtBytes(s.ble?.rxBytes);

    const gpsOk = safe_ok(s.gps?.valid);
    setIcon("gps_valid", gpsOk);
    setIcon("gps_valid2", gpsOk);

    $('gps_fix').textContent   = safe((s.gps?.fix_type && s.gps?.fix_quality) ? (s.gps.fix_type + " / " + s.gps.fix_quality) : undefined);
    $('gps_fix2').textContent   = safe((s.gps?.fix_type && s.gps?.fix_quality) ? (s.gps.fix_type + " / " + s.gps.fix_quality) : undefined);
    $('gps_sats').textContent  = safe(s.gps?.sats_used);
    $('gps_hdop').textContent  = safe((s.gps?.hdop !== undefined) ? s.gps.hdop : undefined);
    $('gps_lat').textContent   = safe((s.gps?.lat !== undefined) ? s.gps.lat : undefined);
    $('gps_lon').textContent   = safe((s.gps?.lon !== undefined) ? s.gps.lon : undefined);
    $('gps_speed').textContent = safe((s.gps?.speed_kmh !== undefined) ? (s.gps.speed_kmh + " km/h") : undefined);
    const utc = (s.gps?.date_utc && s.gps?.time_utc) ? (s.gps.date_utc + " " + s.gps.time_utc) : undefined;
    $('gps_utc').textContent   = safe(utc);
    $('gps_age').textContent   = safe((s.gps?.age_ms !== undefined) ? (s.gps.age_ms + " ms") : undefined);
    // Skyplot (satellites)
    renderSkyplot(s.gps?.sats);

    const reachOk = safe_ok(s.internet?.reach);
    setIcon("reach", reachOk);
    setIcon("reach2", reachOk);

    $('ssid').textContent  = safe(s.wifi?.ssid);
    $('ip2').textContent   = safe(s.wifi?.ip);
    $('gw').textContent    = safe(s.wifi?.gw);
    $('dns').textContent   = safe(s.wifi?.dns);
    $('subnet').textContent= safe(s.wifi?.subnet);
    $('bcast').textContent = safe(s.wifi?.broadcast);
    $('mac').textContent   = safe(s.wifi?.mac);

    $('port').textContent = safe(s.http?.port);
    $('reqs').textContent = safe(s.http?.req_total);
    $('age').textContent  = safe((s.http?.prev_req_age_ms !== undefined) ? (s.http.prev_req_age_ms + " ms") : undefined);

    $('app_state').textContent  = safe(s.app?.state);
    $('app_notes').textContent  = safe(s.app?.notes);

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

// Restart
$('restartBtn').addEventListener('click', async () => {
  if (!confirm("Restart ESP32-C3 now?")) return;
  $('note').textContent = "Restarting…";
  try { await fetch('/api/restart', { method:'POST' }); } catch(e) {}
  setTimeout(refresh, 1500);
});

// Tabs logic (simple show/hide)
(function initTabs(){
  const btns = Array.from(document.querySelectorAll(".tabBtn"));
  const panels = Array.from(document.querySelectorAll(".tabPanel"));

  function activate(tabId){
    panels.forEach(p => p.classList.toggle("isActive", p.id === tabId));
    btns.forEach(b => b.classList.toggle("isActive", b.dataset.tab === tabId));
  }

  btns.forEach(b => b.addEventListener("click", () => activate(b.dataset.tab)));
  activate("tab_status");
})();

refresh();
setInterval(refresh, 1000);
</script>

</body>
</html>
)HTML";

static const size_t INDEX_HTML_LEN = sizeof(INDEX_HTML) - 1;
