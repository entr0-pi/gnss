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

function fmtMeters(m){
  if (!Number.isFinite(m) || m <= 0) return "—";
  if (m < 0.05) return (m*1000).toFixed(0) + " mm";
  if (m < 1.0)  return (m*100).toFixed(1) + " cm";
  return m.toFixed(2) + " m";
}

function safe(v, fallback="—"){
  return (v === undefined || v === null || v === "") ? fallback : v;
}

function safe_ok(v, fallback=false){
  return (v === undefined || v === null || v === "") ? fallback : v;
}

function clamp(n, min, max){
  return Math.min(Math.max(n, min), max);
}

function toRad(deg){
  return (deg * Math.PI) / 180;
}

function skyplotPosition(observer, target){
  if (!observer || !target) return null;
  const lat1 = toRad(observer.lat);
  const lon1 = toRad(observer.lon);
  const lat2 = toRad(target.lat);
  const lon2 = toRad(target.lon);
  if (![lat1, lon1, lat2, lon2].every(Number.isFinite)) return null;

  const dLon = lon2 - lon1;
  const sinLat1 = Math.sin(lat1);
  const cosLat1 = Math.cos(lat1);
  const sinLat2 = Math.sin(lat2);
  const cosLat2 = Math.cos(lat2);

  const centralAngle = Math.acos(clamp(sinLat1 * sinLat2 + cosLat1 * cosLat2 * Math.cos(dLon), -1, 1));
  const elevation = 90 - (centralAngle * 180 / Math.PI);
  if (elevation <= 0) return null;

  const y = Math.sin(dLon) * cosLat2;
  const x = cosLat1 * sinLat2 - sinLat1 * cosLat2 * Math.cos(dLon);
  const bearing = (Math.atan2(y, x) * 180 / Math.PI + 360) % 360;

  return { bearing, elevation };
}

function updateSkyplot(satellites, observer){
  const plot = $("skyplot");
  const legend = $("skyplot_legend");
  const empty = $("skyplot_empty");
  if (!plot || !legend || !empty) return;

  plot.innerHTML = "";
  legend.innerHTML = "";

  const labels = [
    { cls: "n", text: "N" },
    { cls: "e", text: "E" },
    { cls: "s", text: "S" },
    { cls: "w", text: "W" }
  ];
  labels.forEach(({ cls, text }) => {
    const el = document.createElement("div");
    el.className = `skyplot-label ${cls}`;
    el.textContent = text;
    plot.appendChild(el);
  });

  if (!Array.isArray(satellites) || satellites.length === 0 || !observer) {
    empty.style.display = "flex";
    return;
  }

  const constellationStyles = {
    GPS: { label: "GPS", className: "gps", rotate: "0deg" },
    GALILEO: { label: "Galileo", className: "galileo", rotate: "0deg" },
    GLONASS: { label: "GLONASS", className: "glonass", rotate: "0deg" },
    BEIDOU: { label: "BeiDou", className: "beidou", rotate: "45deg" },
    QZSS: { label: "QZSS", className: "qzss", rotate: "0deg" },
    SBAS: { label: "SBAS", className: "sbas", rotate: "0deg" }
  };

  const seen = new Set();
  let plotted = 0;

  satellites.forEach((sat) => {
    const position = skyplotPosition(observer, { lat: sat.lat, lon: sat.lon });
    if (!position) return;

    const key = (sat.constellation || "").toUpperCase();
    const style = constellationStyles[key] || { label: key || "Other", className: "other", rotate: "0deg" };
    seen.add(style.className);

    const radius = (90 - position.elevation) / 90;
    const r = radius * 48 + 2;
    const angle = toRad(position.bearing - 90);
    const x = 50 + r * Math.cos(angle);
    const y = 50 + r * Math.sin(angle);

    const signal = Number(sat.signal_power);
    const size = Number.isFinite(signal) ? clamp(6 + (signal - 20) * 0.35, 8, 16) : 10;

    const marker = document.createElement("div");
    marker.className = `skyplot-marker ${style.className}`;
    marker.style.left = `${x}%`;
    marker.style.top = `${y}%`;
    marker.style.width = `${size}px`;
    marker.style.height = `${size}px`;
    marker.style.setProperty("--marker-rotate", style.rotate);
    marker.title = `${style.label} • ${Number.isFinite(signal) ? signal.toFixed(1) + " dB-Hz" : "—"}`;
    plot.appendChild(marker);
    plotted += 1;
  });

  empty.style.display = plotted === 0 ? "flex" : "none";

  Array.from(seen).forEach((className) => {
    const styleEntry = Object.values(constellationStyles).find((item) => item.className === className);
    const legendItem = document.createElement("div");
    legendItem.className = "skyplot-legend-item";
    const dot = document.createElement("span");
    dot.className = `skyplot-legend-dot ${className}`;
    const text = document.createElement("span");
    text.textContent = styleEntry ? styleEntry.label : "Other";
    legendItem.appendChild(dot);
    legendItem.appendChild(text);
    legend.appendChild(legendItem);
  });
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
    $('ble_uart2bleDrops').textContent = fmtBytes(s.ble?.uart2bleDrops);
    $('ble_ble2uartDrops').textContent = fmtBytes(s.ble?.ble2uartDrops);

    const gpsOk = safe_ok(s.gps?.valid);
    setIcon("gps_valid", gpsOk);
    setIcon("gps_valid2", gpsOk);

    $('gps_fix').textContent   = safe((s.gps?.fix_type && s.gps?.fix_quality) ? (s.gps.fix_type + " / " + s.gps.fix_quality) : undefined);
    $('gps_fix2').textContent   = safe((s.gps?.fix_type && s.gps?.fix_quality) ? (s.gps.fix_type + " / " + s.gps.fix_quality) : undefined);
    $('gps_sats').textContent  = safe(s.gps?.sats_used);
    $('gps_hdop').textContent  = safe((s.gps?.hdop !== undefined) ? s.gps.hdop : undefined);
    $('gps_hacc').textContent  = fmtMeters(s.gps?.hacc_m);
    $('gps_hacc2').textContent  = fmtMeters(s.gps?.hacc_m);
    $('gps_vacc').textContent  = fmtMeters(s.gps?.vacc_m);
    $('gps_vacc2').textContent  = fmtMeters(s.gps?.vacc_m);
    $('gps_accsrc').textContent= safe(s.gps?.acc_source);
    $('gps_lat').textContent   = safe((s.gps?.lat !== undefined) ? s.gps.lat : undefined);
    $('gps_lon').textContent   = safe((s.gps?.lon !== undefined) ? s.gps.lon : undefined);
    $('gps_speed').textContent = safe((s.gps?.speed_kmh !== undefined) ? (s.gps.speed_kmh + " km/h") : undefined);
    const utc = (s.gps?.date_utc && s.gps?.time_utc) ? (s.gps.date_utc + " " + s.gps.time_utc) : undefined;
    $('gps_utc').textContent   = safe(utc);
    $('gps_age').textContent   = safe((s.gps?.age_ms !== undefined) ? (s.gps.age_ms + " ms") : undefined);
    updateSkyplot(s.gps?.satellites, { lat: s.gps?.lat, lon: s.gps?.lon });

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
    updateSkyplot([], null);
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
