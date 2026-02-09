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
  return Math.min(max, Math.max(min, n));
}

function renderSkyplot(satellites){
  const markers = $("skyplot_markers");
  if (!markers) return;
  markers.innerHTML = "";

  if (!Array.isArray(satellites) || satellites.length === 0) return;

  const classMap = {
    GPS: "constellation-gps",
    GLONASS: "constellation-glonass",
    Galileo: "constellation-galileo",
    BeiDou: "constellation-beidou"
  };

  satellites.forEach((sat) => {
    const azimuth = Number(sat?.azimuth);
    const elevation = Number(sat?.elevation);
    const power = Number(sat?.signal_power ?? sat?.power_dbhz ?? sat?.power);
    if (!Number.isFinite(azimuth) || !Number.isFinite(elevation)) return;

    const theta = (azimuth - 90) * (Math.PI / 180);
    const zenithAngle = (90 - elevation) * (Math.PI / 180);
    const normalized = clamp(Math.tan(zenithAngle / 2), 0, 1);
    const radial = normalized * 45;
    const x = clamp(50 + radial * Math.cos(theta), 4, 96);
    const y = clamp(50 + radial * Math.sin(theta), 4, 96);
    const size = clamp(8 + ((Number.isFinite(power) ? power : 30) - 20) / 40 * 6, 8, 14);

    const marker = document.createElement("span");
    marker.className = `skyplot-marker ${classMap[sat?.constellation] || "constellation-other"}`;
    marker.style.left = `${x}%`;
    marker.style.top = `${y}%`;
    marker.style.width = `${size}px`;
    marker.style.height = `${size}px`;
    const powerLabel = Number.isFinite(power) ? `${power} dBHz` : "— dBHz";
    marker.title = `${sat?.constellation || "Unknown"} • ${powerLabel} • Az ${azimuth.toFixed(1)}° El ${elevation.toFixed(1)}°`;
    markers.appendChild(marker);
  });
}

function setConfigNote(message, ok = true) {
  const note = $("cfg_note");
  if (!note) return;
  note.textContent = message;
  note.style.color = ok ? "" : "#e74c3c";
}

function setWifiConfigNote(message, ok = true) {
  const note = $("wifi_cfg_note");
  if (!note) return;
  note.textContent = message;
  note.style.color = ok ? "" : "#e74c3c";
}

function setWifiInputsEnabled(enabled) {
  const ids = ["wifi_ssid", "wifi_pass", "wifi_dhcp", "wifi_ip", "wifi_gw", "wifi_subnet", "wifi_dns"];
  ids.forEach((id) => {
    const el = $(id);
    if (!el) return;
    if (el.type === "checkbox") {
      el.disabled = !enabled;
    } else {
      el.readOnly = !enabled;
    }
  });
}

function updateWifiStaticInputs(dhcp) {
  const staticIds = ["wifi_ip", "wifi_gw", "wifi_subnet", "wifi_dns"];
  staticIds.forEach((id) => {
    const el = $(id);
    if (!el) return;
    el.readOnly = !!dhcp;
  });
}

async function loadConfig(){
  try {
    const r = await fetch('/api/config', { cache:'no-store' });
    if (!r.ok) throw new Error("http " + r.status);
    const cfg = await r.json();

    if ($("cfg_rx")) $("cfg_rx").value = cfg.rx_pin ?? "";
    if ($("cfg_tx")) $("cfg_tx").value = cfg.tx_pin ?? "";
    if ($("cfg_baud")) $("cfg_baud").value = cfg.baud ?? "";

    // Handle locked state
    const isLocked = cfg.locked === true;
    const saveBtn = $("saveConfigBtn");

    if (isLocked) {
      // Make inputs readonly
      if ($("cfg_rx")) $("cfg_rx").readOnly = true;
      if ($("cfg_tx")) $("cfg_tx").readOnly = true;
      if ($("cfg_baud")) $("cfg_baud").readOnly = true;

      // Hide save button
      if (saveBtn) saveBtn.style.display = "none";

      // Show lock message
      setConfigNote("🔒 Configuration locked (compile-time flags)", true);
    } else {
      // Normal mode: editable
      if ($("cfg_rx")) $("cfg_rx").readOnly = false;
      if ($("cfg_tx")) $("cfg_tx").readOnly = false;
      if ($("cfg_baud")) $("cfg_baud").readOnly = false;

      // Show save button
      if (saveBtn) saveBtn.style.display = "";

      setConfigNote("Loaded from device");
    }
  } catch (e) {
    setConfigNote("Failed to load config", false);
  }
}

async function loadWifiConfig(){
  try {
    const r = await fetch('/api/wifi_config', { cache:'no-store' });
    if (!r.ok) throw new Error("http " + r.status);
    const cfg = await r.json();

    if ($("wifi_ssid")) $("wifi_ssid").value = cfg.ssid ?? "";
    if ($("wifi_pass")) $("wifi_pass").value = cfg.pass ?? "";
    if ($("wifi_dhcp")) $("wifi_dhcp").checked = !!cfg.dhcp;
    if ($("wifi_ip")) $("wifi_ip").value = cfg.ip ?? "";
    if ($("wifi_gw")) $("wifi_gw").value = cfg.gw ?? "";
    if ($("wifi_subnet")) $("wifi_subnet").value = cfg.subnet ?? "";
    if ($("wifi_dns")) $("wifi_dns").value = cfg.dns ?? "";

    const isLocked = cfg.locked === true;
    const saveBtn = $("saveWifiConfigBtn");

    if (isLocked) {
      setWifiInputsEnabled(false);
      if (saveBtn) saveBtn.style.display = "none";
      setWifiConfigNote("🔒 Configuration locked (compile-time flags)", true);
    } else {
      setWifiInputsEnabled(true);
      if (saveBtn) saveBtn.style.display = "";
      updateWifiStaticInputs(!!cfg.dhcp);
      setWifiConfigNote("Loaded from device");
    }
  } catch (e) {
    setWifiConfigNote("Failed to load WiFi config", false);
  }
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
    const satsView = Number.isFinite(s.gps?.satellites_in_view)
      ? s.gps.satellites_in_view
      : (Array.isArray(s.gps?.satellites) ? s.gps.satellites.length : null);
    $('gps_sats_view').textContent = safe(satsView);
    renderSkyplot(s.gps?.satellites);

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
    if ($("gps_sats_view")) $("gps_sats_view").textContent = "—";
    renderSkyplot(null);
  }
}

// Restart
$('restartBtn').addEventListener('click', async () => {
  if (!confirm("Restart ESP32-C3 now?")) return;
  $('note').textContent = "Restarting…";
  try { await fetch('/api/restart', { method:'POST' }); } catch(e) {}
  setTimeout(refresh, 1500);
});

const saveConfigBtn = $("saveConfigBtn");
if (saveConfigBtn) {
  saveConfigBtn.addEventListener("click", async () => {
    const rx = Number.parseInt($("cfg_rx").value, 10);
    const tx = Number.parseInt($("cfg_tx").value, 10);
    const baud = Number.parseInt($("cfg_baud").value, 10);

    if (!Number.isFinite(rx) || !Number.isFinite(tx) || !Number.isFinite(baud)) {
      setConfigNote("Enter valid RX, TX, and baud values.", false);
      return;
    }

    setConfigNote("Saving…");
    try {
      const resp = await fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ rx_pin: rx, tx_pin: tx, baud })
      });
      const payload = await resp.json().catch(() => ({}));
      if (!resp.ok) {
        setConfigNote(payload.error || "Failed to save config", false);
        return;
      }
      setConfigNote("Saved and applied.");
      if (payload.config) {
        $("cfg_rx").value = payload.config.rx_pin ?? rx;
        $("cfg_tx").value = payload.config.tx_pin ?? tx;
        $("cfg_baud").value = payload.config.baud ?? baud;
      }
      // Apply changes by restarting the device.
      try { await fetch('/api/restart', { method: 'POST' }); } catch (e) {}
    } catch (e) {
      setConfigNote("Failed to reach device", false);
    }
  });
}

const wifiDhcp = $("wifi_dhcp");
if (wifiDhcp) {
  wifiDhcp.addEventListener("change", () => {
    updateWifiStaticInputs(!!wifiDhcp.checked);
  });
}

const saveWifiConfigBtn = $("saveWifiConfigBtn");
if (saveWifiConfigBtn) {
  saveWifiConfigBtn.addEventListener("click", async () => {
    const ssid = $("wifi_ssid")?.value?.trim() ?? "";
    const pass = $("wifi_pass")?.value ?? "";
    const dhcp = !!$("wifi_dhcp")?.checked;
    const ip = $("wifi_ip")?.value?.trim() ?? "";
    const gw = $("wifi_gw")?.value?.trim() ?? "";
    const subnet = $("wifi_subnet")?.value?.trim() ?? "";
    const dns = $("wifi_dns")?.value?.trim() ?? "";

    if (!ssid) {
      setWifiConfigNote("SSID is required.", false);
      return;
    }

    if (!dhcp && (!ip || !gw || !subnet || !dns)) {
      setWifiConfigNote("Static IP requires IP, gateway, subnet, and DNS.", false);
      return;
    }

    setWifiConfigNote("Saving…");
    try {
      const resp = await fetch('/api/wifi_config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid, pass, dhcp, ip, gw, subnet, dns })
      });
      const payload = await resp.json().catch(() => ({}));
      if (!resp.ok) {
        setWifiConfigNote(payload.error || "Failed to save WiFi config", false);
        return;
      }
      setWifiConfigNote("Saved. Restarting…");
      // Apply changes by restarting the device.
      try { await fetch('/api/restart', { method: 'POST' }); } catch (e) {}
    } catch (e) {
      setWifiConfigNote("Failed to reach device", false);
    }
  });
}

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
loadConfig();
loadWifiConfig();
setInterval(refresh, 1000);
