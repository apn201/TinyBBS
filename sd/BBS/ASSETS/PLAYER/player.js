// TinyBBS music player: tracker modules (MOD, S3M, XM, IT) with libopenmpt.
// Served by the board from /BBS/ASSETS/PLAYER and loaded by the terminal page
// the first time a caller plays a tune. Nothing comes from the Internet.
//
// Plain HTTP pages may not use AudioWorklet, so this renders with a
// ScriptProcessorNode, which every browser still supports.
//
// window.TinyPlayer.open(url, name, size, sendKey)  play a tune
// window.TinyPlayer.close()                          stop and hide
// window.TinyPlayer.isOpen()                         shown?
// window.TinyPlayer.togglePause()
(function () {
  'use strict';
  const BASE = '/player/';
  let lib = null, loading = null;
  let ctx = null, node = null, mod = 0, bufL = 0, bufR = 0, paused = false, ended = false;
  let sendKey = () => {}, raf = 0, title = '', file = '', size = '', meta = {};
  let root = null, screen = null, bar = null, tap = null;

  function loadLibrary() {
    if (loading) return loading;
    loading = new Promise((resolve, reject) => {
      window.libopenmpt = { locateFile: f => BASE + f, onRuntimeInitialized() { lib = window.libopenmpt; resolve(); } };
      const s = document.createElement('script');
      s.src = BASE + 'libopenmpt.js';
      s.onerror = () => { loading = null; reject(new Error('The player library is not on the card.')); };
      document.head.appendChild(s);
    });
    return loading;
  }
  function cstr(text) {  // a C string in the library's memory; caller frees
    const n = text.length * 3 + 1, p = lib._malloc(n);
    window.stringToUTF8(text, p, n);
    return p;
  }
  function take(p) { const s = p ? window.UTF8ToString(p) : ''; if (p) lib._openmpt_free_string(p); return s; }
  function metadata(key) { const k = cstr(key); const s = take(lib._openmpt_module_get_metadata(mod, k)); lib._free(k); return s; }

  function build() {
    if (root) return;
    const css = document.createElement('style');
    css.textContent =
      '#tp{position:fixed;inset:0;z-index:20;background:#000;color:#aaa;display:flex;flex-direction:column;' +
      'font:clamp(9px,1.9vw,16px)/1.25 monospace;user-select:none}' +
      '#tp[hidden]{display:none}#tp pre{margin:0;padding:6px 8px;flex:1;overflow:hidden;white-space:pre}' +
      '#tp .bar{background:#0aa;color:#000;padding:4px 8px;white-space:pre;overflow:hidden}' +
      '#tp .hd{color:#5ff}#tp .cur{background:#00a;color:#fff}#tp .vu{color:#5f5}#tp .pk{color:#ff5}#tp .dim{color:#555}' +
      '#tp .tap{position:absolute;inset:0;display:none;align-items:center;justify-content:center;background:#000c;color:#ff5;font-size:150%}';
    document.head.appendChild(css);
    root = document.createElement('div'); root.id = 'tp'; root.hidden = true;
    screen = document.createElement('pre');
    bar = document.createElement('div'); bar.className = 'bar';
    tap = document.createElement('div'); tap.className = 'tap'; tap.textContent = 'TAP TO PLAY';
    root.append(screen, bar, tap);
    document.body.appendChild(root);
    // Taps: left third previous, right third next, middle pause.
    root.addEventListener('click', e => {
      if (ctx && ctx.state !== 'running') { ctx.resume(); tap.style.display = 'none'; return; }
      const f = e.clientX / innerWidth;
      if (f < 0.3) sendKey('p'); else if (f > 0.7) sendKey('n'); else togglePause();
    });
  }
  function hint(text) {
    bar.textContent = ' ' + (text || (file + '  ' + size + '    N next  P prev  SPACE ' + (paused ? 'play ' : 'pause') + '  ESC back'));
  }

  function stopTune() {
    if (node) { node.disconnect(); node.onaudioprocess = null; node = null; }
    if (lib && mod) { lib._openmpt_module_destroy(mod); mod = 0; }
    cancelAnimationFrame(raf);
  }
  function render(e) {
    const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
    if (!mod || paused || ended) { L.fill(0); R.fill(0); return; }
    const frames = L.length;
    const got = lib._openmpt_module_read_float_stereo(mod, ctx.sampleRate, frames, bufL, bufR);
    const heap = window.HEAPU8.buffer;  // may have been replaced by memory growth
    L.set(new Float32Array(heap, bufL, frames));
    R.set(new Float32Array(heap, bufR, frames));
    if (got < frames) { L.fill(0, got); R.fill(0, got); ended = true; setTimeout(() => sendKey('n'), 1500); }
  }

  const esc = s => s.replace(/[&<>]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c]));
  const clock = t => Math.floor(t / 60) + ':' + String(Math.floor(t % 60)).padStart(2, '0');
  function draw() {
    raf = requestAnimationFrame(draw);
    if (!mod) return;
    const cols = 80, rowsVisible = 15;
    const channels = lib._openmpt_module_get_num_channels(mod);
    const order = lib._openmpt_module_get_current_order(mod), pattern = lib._openmpt_module_get_current_pattern(mod);
    const row = lib._openmpt_module_get_current_row(mod), rows = lib._openmpt_module_get_pattern_num_rows(mod, pattern);
    const width = channels <= 4 ? 16 : channels <= 6 ? 11 : 8;
    const shown = Math.min(channels, Math.floor((cols - 4) / (width + 1)));
    const lines = [];
    lines.push('<span class="hd">TINYTRAX</span>  ' + esc(title.slice(0, 50)));
    lines.push('<span class="dim">' + esc((meta.type + '  ' + channels + ' channels' + (meta.artist ? '  by ' + meta.artist : '')).slice(0, 78)) + '</span>');
    lines.push('Pos ' + String(order).padStart(3, '0') + '/' + String(meta.orders).padStart(3, '0') + '  Pat ' + String(pattern).padStart(3, '0') +
      '  Row ' + String(row).padStart(2, '0') + '  BPM ' + Math.round(lib._openmpt_module_get_current_estimated_bpm(mod)) +
      '  ' + clock(lib._openmpt_module_get_position_seconds(mod)) + ' / ' + clock(meta.duration) + (paused ? '  <span class="pk">PAUSED</span>' : '') + (ended ? '  <span class="pk">END</span>' : ''));
    lines.push('');
    // VU meters, one bar per shown channel.
    let vu = '    ';
    for (let c = 0; c < shown; c++) {
      const v = Math.min(1, lib._openmpt_module_get_current_channel_vu_mono(mod, c));
      const n = Math.round(v * width);
      vu += '<span class="vu">' + '█'.repeat(n) + '</span>' + '<span class="dim">' + '░'.repeat(width - n) + '</span> ';
    }
    lines.push(vu);
    lines.push('');
    for (let i = 0; i < rowsVisible; i++) {
      const r = row - 7 + i;
      if (r < 0 || r >= rows) { lines.push(''); continue; }
      let text = String(r).padStart(2, '0') + '  ';
      for (let c = 0; c < shown; c++) text += take(lib._openmpt_module_format_pattern_row_channel(mod, pattern, r, c, width, 1)).padEnd(width).slice(0, width) + ' ';
      lines.push(r === row ? '<span class="cur">' + esc(text.padEnd(cols)) + '</span>' : esc(text));
    }
    if (shown < channels) lines.push('<span class="dim">    (' + (channels - shown) + ' more channels not shown)</span>');
    screen.innerHTML = lines.join('\n');
  }

  async function open(url, name, bytes, keySender) {
    sendKey = keySender || sendKey;
    build();
    root.hidden = false;
    file = name; size = bytes; paused = false; ended = false;
    stopTune();
    screen.textContent = '\n  Loading ' + name + ' ...';
    hint(' TINYTRAX  ' + name + '  loading...');
    try {
      if (!ctx) ctx = new (window.AudioContext || window.webkitAudioContext)();
      ctx.resume();
      const [res] = await Promise.all([fetch(url), loadLibrary()]);
      if (!res.ok) throw new Error('The board did not send the tune.');
      const data = new Uint8Array(await res.arrayBuffer());
      if (root.hidden || file !== name) return;  // closed or moved on meanwhile
      const p = lib._malloc(data.length);
      window.HEAPU8.set(data, p);
      mod = lib._openmpt_module_create_from_memory2(p, data.length, 0, 0, 0, 0, 0, 0, 0);
      lib._free(p);
      if (!mod) throw new Error('This tune cannot be played.');
      lib._openmpt_module_set_repeat_count(mod, 0);
      title = metadata('title') || name;
      meta = { type: metadata('type_long') || metadata('type'), artist: metadata('artist'),
               orders: lib._openmpt_module_get_num_orders(mod), duration: lib._openmpt_module_get_duration_seconds(mod) };
      if (!bufL) { bufL = lib._malloc(4096 * 4); bufR = lib._malloc(4096 * 4); }
      node = ctx.createScriptProcessor(4096, 0, 2);
      node.onaudioprocess = render;
      node.connect(ctx.destination);
      tap.style.display = ctx.state === 'running' ? 'none' : 'flex';
      hint();
      draw();
    } catch (err) {
      screen.textContent = '\n  ' + name + '\n\n  ' + err.message;
      hint(name + '    N next  P prev  ESC back');
    }
  }
  function close() {
    stopTune();
    if (root) root.hidden = true;
  }
  function togglePause() {
    if (!mod) return;
    paused = !paused;
    hint();
  }
  window.TinyPlayer = { open, close, togglePause, isOpen: () => !!root && !root.hidden };
})();
