#pragma once
#include <Arduino.h>

static const uint8_t STYLE_CSS[] PROGMEM = R"CSS(

@keyframes blink {
  50% { opacity: 0.4; }
}

:root{
  --bg:#0b0f14;
  --stroke:rgba(255,255,255,.08);
  --text:#e8eef6;
  --muted:rgba(232,238,246,.72);
  --mono: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;
  --sans: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif;
  --glow: 0 0 0 1px rgba(255,255,255,.06), 0 20px 60px rgba(0,0,0,.5);
}
*{ box-sizing:border-box; }
body{
  margin:0;
  min-height:100vh;
  font-family:var(--sans);
  color:var(--text);
  background:
    radial-gradient(900px 500px at 10% 10%, rgba(45,125,255,.18), transparent 60%),
    radial-gradient(700px 450px at 90% 20%, rgba(255,59,48,.10), transparent 55%),
    var(--bg);
  padding:18px;
}
.wrap{
  max-width:600px;
  margin:0 auto; 
}
.top{ 
  padding:6px 4px 14px;
}
.badge{
  display:inline-block;
  letter-spacing:.14em;
  font-size:12px;
  font-weight:800;
  color:rgba(255,255,255,.82);
  background:rgba(18, 43, 17, 0.06);
  border:1px solid var(--stroke);
  border-radius:999px;
  padding:6px 10px;
}
.title{ 
  margin-top:10px; 
  font-size:26px; 
  font-weight:900;
}
.sub{
  margin-top:4px; 
  color:var(--muted); 
  font-size:13px;
}
.card{
  background: linear-gradient(180deg, rgba(255,255,255,.04), rgba(255,255,255,.02));
  border:1px solid var(--stroke);
  border-radius:16px;
  box-shadow: var(--glow);
  padding:16px;
}
.grid{
  display:grid;
  grid-template-columns: 1fr 1fr;
  gap:12px;
}
.kv{
  padding:12px;
  border:1px solid var(--stroke);
  background: rgba(0,0,0,.18);
  border-radius:14px;
}
.k{ font-size:12px; color:var(--muted); }
.v{ margin-top:6px; font-size:18px; font-weight:900; }
.mono{ font-family:var(--mono); }

.divider{
  height:1px;
  background: rgba(255,255,255,.08);
  margin:14px 2px;
}
.cat{ margin-top:10px; }
.catTitle{
  font-size:12px;
  color:rgba(255,255,255,.78);
  letter-spacing:.08em;
  font-weight:900;
  margin:8px 2px;
  text-transform:uppercase;
}
.catBody{
  border:1px solid var(--stroke);
  background: rgba(0,0,0,.14);
  border-radius:14px;
  padding:10px 12px;
}
.line{
  display:flex;
  align-items:center;
  justify-content:space-between;
  gap:10px;
  padding:6px 0;
  border-bottom:1px solid rgba(255,255,255,.06);
}
.line:last-child{ border-bottom:0; }
.lk{ color:var(--muted); font-size:13px; }
.lv{ font-weight:800; font-size:13px; }

#rssi.good { color: #2ecc71; font-weight:bold; animation: blink 1s infinite; }
#rssi.ok   { color: #f1c40f ; font-weight:bold; animation: blink 1s infinite; }
#rssi.bad  { color: #e67e22; font-weight:bold; animation: blink 1s infinite; }
#rssi.dead { color: #e74c3c; font-weight:bold; animation: blink 1s infinite; }
#rssi.unknown { color: #95a5a6 !important; animation: blink 1s infinite; }

.actions{
  display:flex;
  gap:12px;
  align-items:center;
  margin-top:14px;
}
.btn{
  appearance:none;
  border:0;
  border-radius:14px;
  padding:12px 14px;
  font-weight:900;
  font-size:15px;
  color:white;
  background: rgba(45,125,255,.95);
  box-shadow: 0 10px 30px rgba(45,125,255,.18);
  cursor:pointer;
}
.btn:active{ transform: translateY(1px); }
.btn.danger{
  background: rgba(255,59,48,.95);
  box-shadow: 0 10px 30px rgba(255,59,48,.16);
}
.note{
  flex:1;
  color:var(--muted);
  font-size:13px;
}
.foot{
  margin-top:12px;
  display:flex;
  gap:8px;
  align-items:center;
  color:var(--muted);
  font-size:13px;
  padding:0 4px;
}

.status-inline{
  margin-top: 6px;
  display: flex;
  align-items: center;
  gap: 8px;

  font-size: 12px;
  color: var(--muted);
}
  
.dot{
  width:10px; height:10px;
  border-radius:50%;
  background: rgba(255,255,255,.25);
  border:1px solid var(--stroke);
}
.dot.ok{ background: rgba(56,214,124,.95); }
.dot.bad{ background: rgba(255,59,48,.95); }

.emoji.ok::before {
  content: "✅";
}
.emoji.bad::before {
  content: "❌";
}

.tabs{
  display:flex;
  gap:8px;
  margin-top:10px;
  flex-wrap:wrap;
}
.tabBtn{
  border:1px solid rgba(255,255,255,.15);
  background:rgba(255,255,255,.06);
  color:inherit;
  padding:8px 12px;
  border-radius:10px;
  cursor:pointer;
  user-select:none;
}
.tabBtn.isActive{
  background:rgba(219, 142, 78, 0.65);
  border-color:rgba(255,255,255,.35);
}

.tabPanel{ display:none; }
.tabPanel.isActive{ display:block; }

/* ================= SKY PLOT ================= */

.skyGrid{
  display:grid;
  grid-template-columns: 1fr;
  gap:12px;
}
@media (min-width: 520px){
  .skyGrid{ grid-template-columns: 1fr 1fr; align-items:start; }
}

.skyBox{
  position:relative;
  width:100%;
  max-width:420px;
  margin:0 auto;
}

.skySvg{
  width:100%;
  height:auto;
  aspect-ratio: 1 / 1;
  display:block;
  border:1px solid rgba(255,255,255,.10);
  border-radius:16px;
  background: rgba(0,0,0,.18);
}

.skyRing{
  fill:none;
  stroke: rgba(255,255,255,.10);
  stroke-width: 1;
}

.skyAxis{
  stroke: rgba(255,255,255,.12);
  stroke-width: 1;
}

.skyCard{
  fill: rgba(255,255,255,.70);
  font-size: 10px;
  font-weight: 900;
  font-family: var(--mono);
}

.skyElev{
  fill: rgba(255,255,255,.45);
  font-size: 9px;
  font-family: var(--mono);
}

.satDot{
  fill: rgba(45,125,255,.65);
  stroke: rgba(255,255,255,.25);
  stroke-width: 1;
}
.satDot.faint{
  fill: rgba(255,255,255,.18);
  stroke: rgba(255,255,255,.12);
}

.sat.used .satDot{
  fill: rgba(56,214,124,.70);
  stroke: rgba(56,214,124,.35);
}

.satLbl{
  fill: rgba(255,255,255,.92);
  font-size: 9px;
  font-weight: 900;
  font-family: var(--mono);
  pointer-events: none;
}

.skyHint{
  display:flex;
  gap:8px;
  flex-wrap:wrap;
  margin-top:10px;
  justify-content:center;
}

.pill{
  display:inline-block;
  padding:4px 8px;
  border-radius:999px;
  border:1px solid rgba(255,255,255,.14);
  background: rgba(255,255,255,.06);
  font-size: 12px;
  color: rgba(255,255,255,.78);
  font-family: var(--mono);
}
.pill.used{
  border-color: rgba(56,214,124,.35);
  background: rgba(56,214,124,.12);
}
.pill.faint{
  border-color: rgba(255,255,255,.10);
  background: rgba(255,255,255,.04);
  color: rgba(255,255,255,.55);
}

.satTableTitle{
  font-size:12px;
  letter-spacing:.08em;
  font-weight:900;
  text-transform:uppercase;
  color:rgba(255,255,255,.78);
  margin:2px 2px 8px;
}

.satTable{
  border:1px solid rgba(255,255,255,.10);
  border-radius:14px;
  overflow:hidden;
  background: rgba(0,0,0,.10);
}

.satRow{
  display:grid;
  grid-template-columns: 1.1fr .9fr .9fr 1.1fr .8fr;
  gap:8px;
  padding:8px 10px;
  border-bottom:1px solid rgba(255,255,255,.06);
  font-size:12px;
  align-items:center;
}
.satRow:last-child{ border-bottom:0; }

.satHead{
  background: rgba(255,255,255,.05);
  color: rgba(255,255,255,.70);
  font-weight:900;
  letter-spacing:.06em;
  text-transform:uppercase;
  font-size:11px;
}

.satRow.isUsed{
  background: rgba(56,214,124,.06);
}

)CSS";

static const size_t STYLE_CSS_LEN = sizeof(STYLE_CSS) - 1;
