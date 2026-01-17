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
