
const load = window.BOARDS_DATA ? Promise.resolve(window.BOARDS_DATA)
  : fetch('boards.json').then(r => r.json());

load.then(({boards}) => {
  const $ = id => document.getElementById(id);
  const q = $('q'), list = $('list'), out = $('result'), chipsEl = $('chips'), count = $('count');
  const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const CHIPS = [['all','All'],['esp32','ESP32'],['esp32s3','ESP32-S3'],['esp32c3','ESP32-C3']];
  const RELEASES = 'https://github.com/entr0-pi/gnss/releases';
  const BASE = RELEASES + '/latest/download/';          // always the newest release
  let chip = 'all', active = -1, shown = [];

  boards.sort((a,b) => a.TARGET_LABEL.localeCompare(b.TARGET_LABEL));
  chipsEl.innerHTML = CHIPS.map(([k,l]) => `<button class="chip${k==='all'?' on':''}" data-k="${k}">${l}</button>`).join('');
  chipsEl.addEventListener('click', e => {
    const k = e.target.dataset.k; if (!k) return;
    chip = k;
    [...chipsEl.children].forEach(c => c.classList.toggle('on', c.dataset.k === k));
    render(); q.focus();
  });

  const hl = (text, term) => {
    if (!term) return esc(text);
    const i = text.toLowerCase().indexOf(term.toLowerCase());
    return i < 0 ? esc(text) : esc(text.slice(0,i)) + '<mark>' + esc(text.slice(i, i+term.length)) + '</mark>' + esc(text.slice(i+term.length));
  };

  function render() {
    const term = q.value.trim().toLowerCase();
    shown = boards.filter(b => (chip === 'all' || b.TARGET_CHIP === chip) &&
      (!term || (b.TARGET_LABEL + ' ' + b.TARGET_BOARD + ' ' + (b.VENDOR||'')).toLowerCase().includes(term)));
    count.textContent = `${shown.length} board${shown.length===1?'':'s'}`;
    list.innerHTML = shown.length
      ? shown.slice(0,100).map((b,i) => `<div class="opt" role="option" data-i="${i}"><span>${hl(b.TARGET_LABEL, term)}</span><small>${esc((b.MCU||b.TARGET_CHIP))}</small></div>`).join('')
      : '<div class="empty">No board found. Try another name or chip filter.</div>';
    active = -1;
  }
  let closeTimer;
  const open  = () => { clearTimeout(closeTimer); list.classList.add('open'); q.setAttribute('aria-expanded','true'); };
  const close = () => { list.classList.remove('open'); q.setAttribute('aria-expanded','false'); };
  const setActive = i => {
    const items = [...list.querySelectorAll('.opt')]; if (!items.length) return;
    active = (i + items.length) % items.length;
    items.forEach((el,k) => el.classList.toggle('active', k === active));
    items[active].scrollIntoView({block:'nearest'});
  };

  // Flash test: >= 4MB OK, < 4MB KO, missing/unparseable -> verify manually
  const FLASH_MIN_MB = 4;
  function flashCheck(v) {
    const m = String(v ?? '').trim().match(/^([\d.]+)\s*([KMG])B?$/i);
    if (!m) return {cls:'unk', icon:'?', title:'No flash information', sub:`Verify flash \u2265 ${FLASH_MIN_MB}MB`};
    const mb = parseFloat(m[1]) * {K:1/1024, M:1, G:1024}[m[2].toUpperCase()];
    return mb >= FLASH_MIN_MB
      ? {cls:'ok',  icon:'\u2713', title:'Flash OK', sub:`${v} (\u2265 ${FLASH_MIN_MB}MB required)`}
      : {cls:'ko',  icon:'\u2715', title:'Flash KO', sub:`${v} is below the ${FLASH_MIN_MB}MB required`};
  }

  // ---- SHA-256 check popup: hash of the downloaded file vs the SHA-256 GitHub shows on the release page ----
  const dlg = $('vd'), vbody = $('vbody');
  let vfile = '', vhash = '';
  async function sha256(file) {
    if (!(window.crypto && crypto.subtle)) throw new Error('nocrypto');
    const d = await crypto.subtle.digest('SHA-256', await file.arrayBuffer());
    return [...new Uint8Array(d)].map(x => x.toString(16).padStart(2, '0')).join('');
  }
  const res = (cls, icon, title, sub) => `<div class="res ${cls}"><span>${icon}</span><div>${title}${sub ? `<small>${sub}</small>` : ''}</div></div>`;
  function vUpdate() {
    const box = $('vcmp'); if (!vhash) { box.innerHTML = ''; return; }
    const raw = ($('vexp').value || '').trim().toLowerCase();
    const m = raw.match(/(?<![0-9a-f])[0-9a-f]{64}(?![0-9a-f])/);   // accepts "sha256:<hash>" or a whole pasted line
    if (m)         box.innerHTML = m[0] === vhash
      ? res('ok', '✓', 'Match: the file is identical to the one published on GitHub', 'You can flash it.')
      : res('ko', '✕', 'NO match: do not flash this file', 'Download it again from the release page and retry.');
    else if (raw)  box.innerHTML = res('unk', '?', 'No SHA-256 found in the pasted text', 'A SHA-256 is 64 hexadecimal characters.');
    else           box.innerHTML = res('unk', '?', 'Paste the SHA-256 from GitHub (step 2) to compare');
  }
  function openVerify(f) {
    vfile = f; vhash = '';
    const cm = /_(esp32(?:s3|c3)?)(?:_|\.bin)/i.exec(f);
    const chipKey = cm ? cm[1].toLowerCase() : '';
    const chipLabel = {esp32: 'ESP32', esp32s3: 'ESP32-S3', esp32c3: 'ESP32-C3'}[chipKey] || 'your chip';
    const addr = chipKey === 'esp32' ? '0x1000' : '0x0';      // bootloader offset: ESP32 0x1000, S3/C3 0x0
    vbody.innerHTML = `
      <p class="dfile"><code>${esc(f)}</code></p>
      <ol class="steps">
        <li><b>Download the file</b><a class="dlbtn" href="${BASE}${encodeURIComponent(f)}" target="_blank" rel="noopener noreferrer">⬇ Download ${esc(f)}</a></li>
        <li><b>Copy the SHA-256 from GitHub</b>
          <p>Open the <a href="${RELEASES}/latest" target="_blank" rel="noopener noreferrer">latest release page</a>, find <code>${esc(f)}</code> in the Assets list, and copy the <code>sha256:…</code> value shown next to it. Paste it here:</p>
          <textarea id="vexp" rows="2" placeholder="sha256:…"></textarea></li>
        <li><b>Select the file you downloaded</b>
          <p>Your browser computes its SHA-256 locally. Nothing is uploaded.</p>
          <label class="pick"><input type="file" id="vf" accept=".bin">Choose file</label>
          <div id="vres"></div><div id="vcmp"></div></li>
        <li class="flashstep"><b>Flash it</b>
          <div class="addr">⚠ <strong>Bootloader address for ${esc(chipLabel)}: <code>${addr}</code></strong>.
            These files start at the bootloader, so write the file at <code>${addr}</code> whichever method you use. A wrong address gives a board that does not boot.</div>
          <p class="dl-mode">Put the board in download mode if it is not detected: hold <strong>BOOT</strong> while plugging it in (or hold BOOT and tap RESET).</p>
          <div class="ftabs" role="tablist">
            <button type="button" class="ftab" role="tab" aria-selected="true"  data-ft="f1">esptool (Python)</button>
            <button type="button" class="ftab" role="tab" aria-selected="false" data-ft="f2">Flash Download Tool</button>
            <button type="button" class="ftab" role="tab" aria-selected="false" data-ft="f3">Launchpad (web)</button>
          </div>
          <div class="fpanel" id="f1">
            <div class="cmd"><code>pip install esptool</code><button type="button" class="copy" data-copy="pip install esptool">Copy</button></div>
            <div class="cmd"><code>esptool --port COM5 write-flash ${addr} ${esc(f)}</code><button type="button" class="copy" data-copy="esptool --port COM5 write-flash ${addr} ${esc(f)}">Copy</button></div>
            <p class="small">Replace <code>COM5</code> with your port (Linux <code>/dev/ttyUSB0</code> or <code>/dev/ttyACM0</code>, macOS <code>/dev/cu.usbmodem…</code>). With esptool 4.x the command is <code>esptool.py --port COM5 write_flash ${addr} ${esc(f)}</code>.</p>
          </div>
          <div class="fpanel" id="f2" hidden>
            <p class="small">Espressif's desktop flasher, <strong>Windows only</strong>.</p>
            <a class="dlbtn" href="https://dl.espressif.com/public/flash_download_tool.zip" target="_blank" rel="noopener noreferrer">⬇ Flash Download Tool (zip)</a>
            <ul>
              <li>Open the tool, set <strong>ChipType</strong> to <strong>${esc(chipLabel)}</strong> and <strong>WorkMode</strong> to <strong>Develop</strong>.</li>
              <li>Add your <code>.bin</code> file and type the address <code>${addr}</code> next to it. Tick its checkbox.</li>
              <li>Tick <strong>DoNotChgBin</strong> so the file is written exactly as published.</li>
              <li>Choose the <strong>COM</strong> port and baud rate, then click <strong>START</strong>.</li>
            </ul>
          </div>
          <div class="fpanel" id="f3" hidden>
            <p class="small">Espressif's web flasher. Chrome or Edge on a computer, nothing to install.</p>
            <a class="dlbtn" href="https://espressif.github.io/esp-launchpad/" target="_blank" rel="noopener noreferrer">Open ESP Launchpad ↗</a>
            <ul>
              <li>Click <strong>Connect</strong> and pick your board's port.</li>
              <li>Use the <strong>DIY</strong> mode (your own firmware image), not the built-in examples.</li>
              <li>Set <strong>Flash Address</strong> to <code>${addr}</code> and choose your <code>.bin</code> file. <strong>Do not keep another address</strong>: Launchpad flashes the file wherever you tell it.</li>
              <li>Click <strong>Flash</strong> and wait until it finishes, then reset the board.</li>
            </ul>
          </div></li>
      </ol>`;
    dlg.showModal();
  }
  vbody.addEventListener('input', e => { if (e.target.id === 'vexp') vUpdate(); });
  vbody.addEventListener('change', async e => {
    if (e.target.id !== 'vf' || !e.target.files[0]) return;
    const f = e.target.files[0], box = $('vres');
    box.innerHTML = '<p>Computing SHA-256…</p>'; vhash = '';
    try { vhash = await sha256(f); }
    catch { box.innerHTML = res('ko', '✕', 'SHA-256 unavailable', 'Your browser needs a secure context (https, localhost or a local file).'); return; }
    box.innerHTML = `${f.name === vfile ? '' : res('unk', '!', 'Unexpected file name', `${esc(f.name)} is not ${esc(vfile)}.`)}
      <div class="hash"><div class="k">SHA-256 of your file (${(f.size / 1024).toFixed(1)} KB)</div><code>${vhash}</code></div>`;
    vUpdate();
  });
  vbody.addEventListener('click', e => {
    const t = e.target.closest('.ftab');
    if (t) {
      vbody.querySelectorAll('.ftab').forEach(x => x.setAttribute('aria-selected', String(x === t)));
      vbody.querySelectorAll('.fpanel').forEach(p => p.hidden = p.id !== t.dataset.ft);
      return;
    }
    const c = e.target.closest('[data-copy]');
    if (c) { navigator.clipboard?.writeText(c.dataset.copy); const l = c.textContent; c.textContent = 'Copied ✓'; setTimeout(() => c.textContent = l, 1200); }
  });
  $('vx').addEventListener('click', () => dlg.close());
  dlg.addEventListener('click', e => { if (e.target === dlg) dlg.close(); });

  // ---- Help popup: how to find chip / flash size / USB type ----
  const hd = $('hd');
  function showTab(id) {
    hd.querySelectorAll('.tab').forEach(t => t.setAttribute('aria-selected', String(t.dataset.tab === id)));
    hd.querySelectorAll('.panel').forEach(p => p.hidden = p.id !== id);
  }
  function openHelp(tab) { showTab(tab || 'p1'); hd.showModal(); hd.scrollTop = 0; }
  document.addEventListener('click', e => { if (e.target.closest('[data-help]')) openHelp(); });
  hd.addEventListener('click', e => {
    if (e.target === hd) return hd.close();
    const t = e.target.closest('.tab'); if (t) return showTab(t.dataset.tab);
    const c = e.target.closest('[data-copy]');
    if (c) { navigator.clipboard?.writeText(c.dataset.copy); const l = c.textContent; c.textContent = 'Copied ✓'; setTimeout(() => c.textContent = l, 1200); }
  });
  $('hx').addEventListener('click', () => hd.close());

  const copyBtn = (t, label='Copy') => `<button class="copy" data-copy="${esc(t)}">${label}</button>`;
  const spec = (k,v) => v ? `<div class="spec"><div class="k">${k}</div><div class="v">${esc(v)}</div></div>` : '';
  const fwRow = (icon, label, f) => `<div class="fw"><div class="l"><b>${icon} ${label}</b><code>${esc(f)}</code></div>
      <div class="acts"><button class="dlbtn" data-verify="${esc(f)}">\u2B07 Download</button></div></div>`;

  function show(b) {
    q.value = b.TARGET_LABEL; close();
    out.style.display = 'block';
    if (!b.BINARY_AVAILABLE) {
      out.innerHTML = `<div class="status no"><span class="big">✕</span><div>Not supported yet<span class="name">${esc(b.TARGET_LABEL)} has no firmware binary.</span></div></div>`;
      return;
    }
    const sfx = b.BINARY_MOST_LIKELY, low = b.BINARY_CONFIDENCE === 'low';
    const ble = `gnss_BLE${sfx}`, wifi = `gnss_WIFI${sfx}`;
    const others = (b.BINARY_AVAILABLE_SUFFIXES||[]).filter(s => s !== sfx);
    const port = b.BINARY_MOST_LIKELY_PORT && b.BINARY_MOST_LIKELY_PORT !== 'n/a' ? b.BINARY_MOST_LIKELY_PORT.toUpperCase() : null;
    const offset = b.TARGET_CHIP === 'esp32' ? '0x1000' : '0x0';
    const f = flashCheck(b.FLASH);
    out.innerHTML = `
      <div class="status ok"><span class="big">✓</span><div>Compatible<span class="name">${esc(b.TARGET_LABEL)}</span></div></div>
      <div class="specs">
        ${spec('Chip', b.MCU || b.TARGET_CHIP)}${spec('Frequency', b.FREQUENCY)}${spec('Flash', b.FLASH)}
        ${spec('RAM', b.RAM)}${spec('Vendor', b.VENDOR)}${spec('Bootloader offset', offset)}
      </div>
      <div class="flash ${f.cls}"><span>${f.icon}</span><div>${f.title}<small>${esc(f.sub)}</small></div></div>
      <h2>Firmware${port ? `<span class="badge${low?' low':''}">${port}${low?' · best guess':''}</span>` : ''}</h2>
      ${fwRow('🔵', 'BLE only', ble)}
      ${fwRow('📶', 'Wi-Fi', wifi)}
      <div class="note verify"><b>🔐 Verify before flashing.</b> <b>Download</b> opens a short guide to check your file's SHA-256 against the one GitHub shows on the <a href="${RELEASES}/latest" target="_blank" rel="noopener noreferrer">release page</a>.</div>
      <div class="note"><b>⚙️ Next: configure over USB.</b> After flashing, set up your board with the <a href="https://github.com/entr0-pi/arduino-nvs-desktop" target="_blank" rel="noopener noreferrer">ESP32 NVS Key-Pair Manager</a> desktop app. No reflash needed.</div>
      ${low && others.length ? `<div class="note">Port type is a best guess for this board. If it does not boot, try <code>${esc(others[0])}</code> instead. <button type="button" class="linkbtn" data-help>How to check your USB type</button></div>` : ''}`;
  }

  out.addEventListener('click', e => {
    const v = e.target.closest('[data-verify]'); if (v) return openVerify(v.dataset.verify);
    const t = e.target.closest('[data-copy]'); if (!t) return;
    navigator.clipboard?.writeText(t.dataset.copy);
    const lbl = t.textContent; t.textContent = 'Copied ✓'; setTimeout(() => t.textContent = lbl, 1200);
  });
  list.addEventListener('mousedown', e => {
    const o = e.target.closest('.opt'); if (o) show(shown[+o.dataset.i]);
  });
  q.addEventListener('input', () => { render(); open(); });
  q.addEventListener('focus', () => { render(); open(); });
  q.addEventListener('blur', () => { closeTimer = setTimeout(close, 120); });
  q.addEventListener('keydown', e => {
    if (e.key === 'ArrowDown') { open(); setActive(active + 1); e.preventDefault(); }
    else if (e.key === 'ArrowUp') { setActive(active - 1); e.preventDefault(); }
    else if (e.key === 'Enter') { const b = shown[active >= 0 ? active : (shown.length === 1 ? 0 : -1)]; if (b) show(b); }
    else if (e.key === 'Escape') close();
  });
  render();
}).catch(() => {
  const o = document.getElementById('result');
  o.style.display = 'block';
  o.innerHTML = '<div class="status no"><span class="big">✕</span><div>Could not load boards.json<span class="name">Serve this folder over http (python -m http.server) or open the standalone version.</span></div></div>';
});
