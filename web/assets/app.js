'use strict';
(function () {
  const $ = (id) => document.getElementById(id);
  const q = new URLSearchParams(location.search);
  let token = q.get('token') || '';
  try { if (token) localStorage.setItem('pmToken', token); else token = localStorage.getItem('pmToken') || ''; } catch (e) {}
  const tq = () => (token ? '?token=' + encodeURIComponent(token) : '');

  let info = null, last = null, es = null, esOk = false;
  let toastTimer = null;

  function banner(msg) { const b = $('banner'); b.textContent = msg || ''; b.classList.toggle('show', !!msg); }

  async function api(method, path, body) {
    const opt = { method, headers: {} };
    if (token) opt.headers['X-Pacemaker-Token'] = token;
    if (body !== undefined) { opt.headers['Content-Type'] = 'application/json'; opt.body = JSON.stringify(body); }
    let r;
    try { r = await fetch(path, opt); } catch (e) { banner('Cannot reach the Pacemaker: ' + e.message); throw e; }
    if (r.status === 401) { askToken(); throw new Error('token required'); }
    let j = null;
    try { j = await r.json(); } catch (e) {}
    if (!r.ok) { const m = (j && j.error) || ('HTTP ' + r.status); const err = new Error(m); err.status = r.status; throw err; }
    return j;
  }
  function askToken() {
    const d = $('tokDlg');
    if (d.open) return;
    if (d.showModal) d.showModal();
    $('tokForm').onsubmit = () => { token = $('tokIn').value; try { localStorage.setItem('pmToken', token); } catch (e) {} location.reload(); };
  }
  function toast(msg, bad) {
    if (bad) { banner(msg); clearTimeout(toastTimer); toastTimer = setTimeout(() => render(last), 4000); }
  }

  // ------------------------------------------------------------------ rendering
  const dots = $('dots');
  function ensureDots(n) {
    while (dots.children.length < n) dots.appendChild(document.createElement('i'));
    while (dots.children.length > n) dots.removeChild(dots.lastChild);
  }
  function pill(el, cls, text) { el.textContent = ''; const s = document.createElement('span'); s.className = 'pill ' + cls; s.textContent = text; el.appendChild(s); }
  function setIfIdle(el, v) { if (document.activeElement !== el) { if (el.type === 'checkbox') el.checked = !!v; else el.value = v; } }

  function render(s) {
    if (!s) return;
    $('bpm').textContent = s.tempo.valid && s.stateId !== 0 ? s.tempo.bpm.toFixed(1) : '--';
    const st = $('state'); st.textContent = s.state; st.className = 'stateword st-' + s.state;
    $('songName').textContent = s.song.name ? s.song.name : '';
    const c = Math.round(s.confidence * 100);
    $('confBar').style.width = c + '%'; $('confVal').textContent = c + '%';
    $('confMeter').setAttribute('aria-valuenow', c);
    $('confBar').className = c < 35 ? 'clip' : (c < 60 ? 'hot' : '');
    $('bar').textContent = s.stateId === 0 ? '-' : s.bar; $('beat').textContent = s.stateId === 0 ? '-' : s.beat;
    $('meter').textContent = s.beatsPerBar + '/' + s.beatUnit;
    $('nudge').textContent = (s.nudgeMs > 0 ? '+' : '') + s.nudgeMs.toFixed(0) + ' ms';
    const fl = [];
    if (s.relock) fl.push('relock');
    if (s.octaveAmbiguity) fl.push('octave unsure');
    if (s.standby) fl.push('standby (other unit publishes)');
    $('flags').textContent = fl.join(' / ');
    ensureDots(s.beatsPerBar);
    for (let i = 0; i < dots.children.length; ++i) {
      const on = s.stateId !== 0 && i + 1 === s.beat;
      dots.children[i].className = (on ? 'on ' : '') + (i === 0 ? 'one' : '');
    }
    const f = $('bFollow'); f.setAttribute('aria-pressed', s.follow ? 'true' : 'false'); f.firstChild.textContent = s.follow ? 'Follow: ON' : 'Follow: OFF';
    if (s.deviceLost) banner('AUDIO DEVICE LOST - holding last tempo'); else if ($('banner').textContent.indexOf('AUDIO') === 0) banner('');

    // inputs
    const box = $('inputs');
    while (box.children.length < s.inputs.length) {
      const row = document.createElement('div'); row.className = 'row';
      const a = document.createElement('span'); a.className = 'lbl';
      const m = document.createElement('div'); m.className = 'meter'; m.appendChild(document.createElement('div'));
      const v = document.createElement('span'); v.className = 'val';
      row.append(a, m, v); box.appendChild(row);
    }
    while (box.children.length > s.inputs.length) box.removeChild(box.lastChild);
    s.inputs.forEach((ch, i) => {
      const row = box.children[i];
      row.children[0].textContent = 'Ch ' + (ch.ch + 1) + ' ' + ch.role + (ch.mute ? ' (muted)' : '');
      const w = Math.max(0, Math.min(1, (ch.db + 60) / 60));
      const bar = row.children[1].firstChild; bar.style.width = (w * 100) + '%';
      bar.className = ch.db > -1 ? 'clip' : (ch.db > -12 ? 'hot' : '');
      row.children[2].textContent = ch.db <= -100 ? '-inf dB' : ch.db.toFixed(0) + ' dB';
    });

    // outputs
    const o = s.outputs;
    setIfIdle($('oLink'), o.link.enabled); setIfIdle($('oMidi'), o.midi.enabled); setIfIdle($('oOsc'), o.osc.enabled); setIfIdle($('oSync'), o.sync.enabled);
    setIfIdle($('offMidi'), o.midi.offsetMs); setIfIdle($('offOsc'), o.osc.offsetMs); setIfIdle($('offSync'), o.sync.offsetMs);
    if (!o.link.compiled) pill($('sLink'), 'off', 'not built in');
    else if (!o.link.enabled) pill($('sLink'), 'off', 'off');
    else pill($('sLink'), o.link.peers > 0 ? 'ok' : 'warn', o.link.peers + (o.link.peers === 1 ? ' peer' : ' peers') + (o.link.publisher ? ', publishing' : ''));
    $('tLink').textContent = o.link.error || ('quantum ' + o.link.quantum);
    if (!o.midi.enabled) pill($('sMidi'), 'off', 'off');
    else if (!o.midi.connected) pill($('sMidi'), 'bad', o.midi.error || o.midi.detail || 'port not found');
    else pill($('sMidi'), 'ok', 'jitter ' + (o.midi.jitterUs / 1000).toFixed(2) + ' ms');
    if (!o.osc.enabled) pill($('sOsc'), 'off', 'off'); else pill($('sOsc'), o.osc.errors ? 'warn' : 'ok', o.osc.sent + ' sent');
    $('tOsc').textContent = o.osc.host + ':' + o.osc.port;
    pill($('sSync'), o.sync.enabled ? 'ok' : 'off', o.sync.enabled ? 'on' : 'off');
    const ps = $('portSel');
    if (document.activeElement !== ps && ps.dataset.cur !== o.midi.port && ps.options.length) { ps.value = o.midi.port; ps.dataset.cur = o.midi.port; }

    // songs
    const ul = $('songs');
    if (ul.dataset.sig !== JSON.stringify(s.song.index) + '|' + ul.children.length) markSong(s.song.index);

    // setup form
    const p = s.params;
    if (!$('setup').contains(document.activeElement)) {
      $('fProfile').value = p.profile === 'rehearsal' ? 'rehearsal' : 'stage'; $('fRef').value = p.referenceBpm;
      $('fPattern').value = p.pattern; $('fNum').value = p.meterNum; $('fDen').value = String(p.meterDen);
    }
    const cn = $('conn'); cn.textContent = esOk ? 'Live' : 'Polling'; cn.className = 'conn ' + (esOk ? 'ok' : 'bad');
  }

  function markSong(idx) {
    const ul = $('songs');
    for (let i = 0; i < ul.children.length; ++i) {
      const b = ul.children[i].firstChild;
      if (i === idx) b.setAttribute('aria-current', 'true'); else b.removeAttribute('aria-current');
    }
    ul.dataset.sig = JSON.stringify(idx) + '|' + ul.children.length;
  }
  async function loadSongs() {
    const m = await api('GET', '/api/songs'); const ul = $('songs'); ul.textContent = '';
    m.songs.forEach((sg, i) => {
      const li = document.createElement('li'), b = document.createElement('button'); b.type = 'button';
      const a = document.createElement('span'), z = document.createElement('span');
      a.textContent = (i + 1) + '. ' + sg.name; z.textContent = (sg.bpm > 0 ? sg.bpm + ' BPM  ' : '') + sg.meterNum + '/' + sg.meterDen;
      b.append(a, z); b.onclick = () => api('POST', '/api/song/' + i).catch((e) => toast(e.message, true));
      li.appendChild(b); ul.appendChild(li);
    });
    $('noSongs').style.display = m.songs.length ? 'none' : '';
    markSong(m.current);
  }

  // ------------------------------------------------------------------ live status
  function openStream() {
    if (es) es.close();
    es = new EventSource('/api/events' + tq());
    es.addEventListener('status', (e) => { esOk = true; last = JSON.parse(e.data); render(last); });
    es.onerror = () => { esOk = false; if (last) render(last); };
  }
  function poll() {
    if (esOk) return;
    api('GET', '/api/status').then((s) => { last = s; render(s); }).catch(() => {});
  }

  // ------------------------------------------------------------------ actions
  const act = (name) => api('POST', '/api/action/' + name).catch((e) => toast(e.message, true));
  document.querySelectorAll('[data-act]').forEach((b) => {
    b.onclick = () => act(b.dataset.act).then(() => { if (/Song/.test(b.dataset.act)) setTimeout(loadSongs, 150); });
  });
  $('bFollow').onclick = () => act('follow');
  const keys = { t: 'tap', d: 'downbeatNow', r: 'relock', h: 'half', x: 'double', f: 'follow', '[': 'nudgeMinus', ']': 'nudgePlus', n: 'nextSong', p: 'prevSong' };
  document.addEventListener('keydown', (e) => {
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    const t = e.target.tagName; if (t === 'INPUT' || t === 'SELECT' || t === 'TEXTAREA') return;
    const a = keys[e.key.toLowerCase()]; if (a) { e.preventDefault(); act(a); }
  });

  const patch = (obj) => api('POST', '/api/state/patch', obj).catch((e) => toast(e.message, true));
  $('oLink').onchange = (e) => patch({ session: { link: { enabled: e.target.checked } } });
  $('oMidi').onchange = (e) => patch({ session: { midi: { enabled: e.target.checked } } });
  $('oOsc').onchange = (e) => patch({ session: { osc: { enabled: e.target.checked } } });
  $('oSync').onchange = (e) => patch({ session: { sync: { enabled: e.target.checked } } });
  const num = (el) => { const v = parseFloat(el.value); return isFinite(v) ? v : 0; };
  $('offMidi').onchange = (e) => patch({ session: { midi: { offsetMs: num(e.target) } } });
  $('offOsc').onchange = (e) => patch({ session: { osc: { offsetMs: num(e.target) } } });
  $('offSync').onchange = (e) => patch({ session: { sync: { offsetMs: num(e.target) } } });
  $('portSel').onchange = (e) => { e.target.dataset.cur = e.target.value; patch({ session: { midi: { port: e.target.value } } }); };

  function sendSetup() {
    api('POST', '/api/params', {
      profile: $('fProfile').value, referenceBpm: num($('fRef')), pattern: $('fPattern').value,
      meterNum: Math.round(num($('fNum'))), meterDen: parseInt($('fDen').value, 10)
    }).catch((e) => toast(e.message, true));
  }
  ['fProfile', 'fRef', 'fNum', 'fDen'].forEach((id) => { $(id).onchange = sendSetup; });
  $('fPattern').onchange = () => {
    const pi = info && info.patterns.find((p) => p.key === $('fPattern').value);
    if (pi) { $('fNum').value = pi.beats; $('fDen').value = String(pi.unit); }
    sendSetup();
  };

  // ------------------------------------------------------------------ calibration
  let calTimer = null, lbFinishing = false;
  const txt = (id, t, cls) => { const e = $(id); e.textContent = t; e.className = 'res' + (cls ? ' ' + cls : ''); };
  async function calPoll() {
    let c; try { c = await api('GET', '/api/calibration'); } catch (e) { return; }
    const lb = c.loopback;
    if (lb.active) txt('lbRes', 'Capturing clicks...');
    else if (lb.ready && !lbFinishing) {
      lbFinishing = true;
      try {
        const r = await api('POST', '/api/calibration/loopback/finish');
        txt('lbRes', (r.pass ? 'PASS' : 'FAIL') + ': ' + r.clicksDetected + '/' + r.clicksTotal + ' clicks, round trip ' + r.roundTripSamples +
          ' samples, hidden in/out ' + r.hiddenInputSamples + '/' + r.hiddenOutputSamples + ' samples. ' + (r.message || ''), r.pass ? 'pass' : 'fail');
      } catch (e) { txt('lbRes', e.message, 'fail'); }
      lbFinishing = false;
    }
    const ta = c.tapAlong;
    if (!ta.available) txt('taRes', 'Not available on this device (no host tempo grid).');
    else if (ta.running) txt('taRes', ta.count + ' / ' + ta.needed + ' beats, median ' + ta.medianMs.toFixed(1) + ' ms');
    else if (ta.accepted) txt('taRes', 'Accepted: input latency ' + ta.inputLatencyMs.toFixed(1) + ' ms', 'pass');
    const hw = c.hardware;
    if (hw.running) txt('hwRes', hw.haveEstimate ? 'Measured click offset ' + hw.measuredMs.toFixed(1) + ' ms (apply sets MIDI offset ' + (-hw.measuredMs).toFixed(1) + ' ms)' : 'Collecting click/hit pairs...');
    else txt('hwRes', 'MIDI clock offset is ' + hw.midiClockOffsetMs.toFixed(1) + ' ms');
    if (!lb.active && !lb.ready && !ta.running && !hw.running) { clearInterval(calTimer); calTimer = null; }
  }
  function watchCal() { calPoll(); if (!calTimer) calTimer = setInterval(calPoll, 700); }
  const run = (p, body) => api('POST', p, body).then(watchCal).catch((e) => toast(e.message, true));
  $('lbStart').onclick = () => run('/api/calibration/loopback/start', { channel: parseInt($('lbCh').value, 10) || 0 });
  $('taStart').onclick = () => run('/api/calibration/tapalong/start');
  $('taAccept').onclick = () => run('/api/calibration/tapalong/accept');
  $('hwStart').onclick = () => run('/api/calibration/hardware/start', { role: $('hwRole').value });
  $('hwApply').onclick = () => run('/api/calibration/hardware/apply');

  $('dlStart').onclick = () => api('POST', '/api/driftlog/start').then((r) => { $('dlInfo').textContent = r.started ? 'Logging to ' + r.path : 'Could not open a drift log file.'; }).catch((e) => toast(e.message, true));
  $('dlStop').onclick = () => api('POST', '/api/driftlog/stop').then((r) => { $('dlInfo').textContent = 'Stopped. Last file: ' + (r.path || 'none'); }).catch((e) => toast(e.message, true));

  // Links that open in a new tab or download cannot send headers, so the token goes in the query string.
  if (token) for (const id of ['lnkMap', 'lnkCsv', 'lnkMetro', 'lnkReport']) $(id).href += tq();

  // ------------------------------------------------------------------ trace canvas
  async function drawTrace() {
    try {
      const t = await api('GET', '/api/trace'); const cv = $('trace'), g = cv.getContext('2d');
      g.clearRect(0, 0, cv.width, cv.height);
      const v = t.values; let mx = 0.001; for (const x of v) if (x > mx) mx = x;
      g.strokeStyle = '#5cc8ff'; g.lineWidth = 2; g.beginPath();
      v.forEach((x, i) => {
        const px = i / (v.length - 1) * cv.width, py = cv.height - 4 - (x / mx) * (cv.height - 8);
        if (i) g.lineTo(px, py); else g.moveTo(px, py);
      });
      g.stroke();
    } catch (e) {}
  }

  // ------------------------------------------------------------------ init
  async function init() {
    try {
      info = await api('GET', '/api/info');
      if (info.title) { document.title = info.title; document.querySelector('h1').textContent = info.title; }
      info.patterns.forEach((p) => { const o = document.createElement('option'); o.value = p.key; o.textContent = p.label; $('fPattern').appendChild(o); });
      info.roles.forEach((r) => { const o = document.createElement('option'); o.value = r; o.textContent = r; $('hwRole').appendChild(o); });
      $('hwRole').value = 'trigger';
      for (let i = 0; i < 8; ++i) { const o = document.createElement('option'); o.value = i; o.textContent = 'Input ' + (i + 1); $('lbCh').appendChild(o); }
      const ports = await api('GET', '/api/ports'); const ps = $('portSel');
      const none = document.createElement('option'); none.value = ''; none.textContent = '(none)'; ps.appendChild(none);
      ports.midiOut.forEach((n) => { const o = document.createElement('option'); o.value = n; o.textContent = n; ps.appendChild(o); });
      if (ports.current && !ports.midiOut.includes(ports.current)) {
        const o = document.createElement('option'); o.value = ports.current; o.textContent = ports.current + ' (missing)'; ps.appendChild(o);
      }
      await loadSongs();
      last = await api('GET', '/api/status'); render(last);
    } catch (e) { return; }
    openStream();
    setInterval(poll, 500);
    setInterval(drawTrace, 500); drawTrace();
    setInterval(() => api('GET', '/api/songs').then((m) => { if ($('songs').children.length !== m.songs.length) loadSongs(); }).catch(() => {}), 5000);
    watchCal();
  }
  init();
})();
