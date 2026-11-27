// Visuales por tipo de plugin (EQ, compresor, delay, voz, genérico) para el dispositivo destacado,
// las tarjetas del catálogo y la carátula de las fichas sin vídeo. Iridiscente solo como luz.
//
// Rendimiento: el SVG se genera ESTÁTICO. `mountVisuals()` le da vida con:
//  - animaciones de compositor (Web Animations sobre transform/opacity) para aguja, pulsos, cabezal y repeticiones;
//  - un único bucle que actualiza la curva del EQ y el punto del compresor a ≤ 60 fps,
// y todo se pausa cuando el visual no se ve (otra escena, ficha encima, ventana minimizada).
// SMIL se descartó: corría en el hilo principal a la frecuencia del monitor aunque estuviera oculto.

const KINDS = ['eq', 'compressor', 'delay', 'voice', 'riser', 'generic'];

/** Tipo de visual: campo `visual` del catálogo o, si falta, inferido de la categoría y el ícono. */
export function visualKind(p) {
  if (KINDS.includes(p.visual)) return p.visual;
  const c = `${p.category ?? ''}`.toLowerCase();
  if (/prueba/.test(c)) return p.icon === 'echo' ? 'delay' : p.icon === 'spark' ? 'riser' : 'generic';
  if (/ecualiz|\beq\b/.test(c)) return 'eq';
  if (/compres|limit/.test(c)) return 'compressor';
  if (/delay|eco|reverb/.test(c)) return 'delay';
  if (/voz|vocal|transici/.test(c)) return 'voice';
  return { filter: 'eq', knob: 'compressor', echo: 'delay', mic: 'voice', spark: 'riser' }[p.icon] ?? 'generic';
}

const W = 370;
const H = 115;
let uid = 0;
const f1 = (n) => n.toFixed(1);
/** Especificaciones de movimiento por id de SVG, consumidas por mountVisuals(). */
const specs = new Map();

// ------------------------------------------------------------------ EQ
function bandDb(b, hz) {
  const w = hz / b.f;
  // |a·s² + b·s + d| con s = jw: prototipos analógicos clásicos (mismo modelo que el vídeo de Remotion)
  const mag = (a, bq, d, s) => Math.hypot(d - a * s * s, bq * s);
  if (b.t === 'lc') return 20 * Math.log10((w * w) / Math.hypot(1 - w * w, Math.SQRT2 * w));
  const A = 10 ** (b.g / 40);
  if (b.t === 'bell') return 20 * Math.log10(mag(1, A / b.q, 1, w) / mag(1, 1 / (A * b.q), 1, w));
  const k = Math.sqrt(A) / Math.SQRT1_2;
  return 20 * Math.log10((A * mag(A, k, 1, w)) / mag(1, k, A, w));
}
const xOf = (hz) => 10 + (Math.log10(hz / 20) / 3) * (W - 20);
const yOf = (db) => H / 2 - db * 4.1;
const EQ_POINTS = 90;
const EQ_X = Array.from({ length: EQ_POINTS + 1 }, (_, i) => xOf(20 * 10 ** ((3 * i) / EQ_POINTS)));
function eqState(dynGain) {
  const bands = [
    { t: 'lc', f: 38 },
    { t: 'bell', f: 120, g: 3.5, q: 0.9 },
    { t: 'bell', f: 640, g: dynGain, q: 1.8 },
    { t: 'hs', f: 6000, g: 4.5 },
  ];
  const total = (hz) => bands.reduce((a, b) => a + bandDb(b, hz), 0);
  const ys = EQ_X.map((_, i) => yOf(Math.max(-13, total(20 * 10 ** ((3 * i) / EQ_POINTS)))));
  const nodes = [38, 120, 640, 6000].map((hz) => [xOf(hz), yOf(Math.max(-13, total(hz)))]);
  return { ys, nodes };
}
const eqPath = (ys) => `M${ys.map((y, i) => `${f1(EQ_X[i])},${f1(y)}`).join(' L')}`;
function spectrum(seed, lift) {
  const pts = [];
  for (let i = 0; i <= 60; i += 1) {
    const x = 10 + (i / 60) * (W - 20);
    const bump = Math.exp(-(((i - 14) / 7) ** 2)) * 18 + Math.exp(-(((i - 34) / 9) ** 2)) * 10;
    const tilt = (i / 60) * 26;
    const noise = Math.sin(i * 1.7 + seed) * 3 + Math.sin(i * 4.3 + seed * 2) * 2;
    pts.push(`${f1(x)},${f1(Math.min(H - 2, 70 + tilt - bump - noise - lift))}`);
  }
  return `M10,${H} L${pts.join(' L')} L${W - 10},${H} Z`;
}
const BAND = ['#52dcf2', '#7eb6ff', '#af9fff', '#ff91ca'];
// Estados de la banda dinámica (dB) y sus tiempos dentro del ciclo de 4,6 s
const EQ_CYCLE = { dur: 4600, times: [0, 0.35, 0.7, 1] };
let eqStates = null;
const getEqStates = () => (eqStates ??= [eqState(-7), eqState(-2.2), eqState(-5.2), eqState(-7)]);

function eqSvg(id, compact) {
  const s = getEqStates();
  const grid = [50, 100, 200, 500, 1000, 2000, 5000, 10000].map((hz) => `<line x1="${f1(xOf(hz))}" x2="${f1(xOf(hz))}" y1="6" y2="${H - 6}"/>`).join('')
    + [-6, 0, 6].map((db) => `<line x1="10" x2="${W - 10}" y1="${f1(yOf(db))}" y2="${f1(yOf(db))}"${db === 0 ? ' class="vz-zero"' : ''}/>`).join('');
  const d0 = eqPath(s[0].ys);
  const [nx, ny] = s[0].nodes[2];
  specs.set(id, { kind: 'eq' });
  return `
    <defs>
      <linearGradient id="${id}l" x1="0" x2="1"><stop offset="0" stop-color="#5ff0ff"/><stop offset=".4" stop-color="#b9a8ff"/><stop offset=".7" stop-color="#ffb3e1"/><stop offset="1" stop-color="#ffd28a"/></linearGradient>
      <linearGradient id="${id}s" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#9fe9ff" stop-opacity=".34"/><stop offset="1" stop-color="#9fe9ff" stop-opacity="0"/></linearGradient>
      <filter id="${id}g" x="-10%" y="-60%" width="120%" height="220%"><feGaussianBlur stdDeviation="5"/></filter>
    </defs>
    ${compact ? '' : `<g class="vz-grid">${grid}</g>`}
    <path class="vz-spec" fill="url(#${id}s)" d="${spectrum(1, 0)}"/>
    <path class="vz-spec vz-spec--b" fill="url(#${id}s)" d="${spectrum(2.6, 4)}" opacity="0"/>
    <path class="vz-curve" d="${d0}" fill="none" stroke="url(#${id}l)" stroke-width="7" opacity=".55" filter="url(#${id}g)"/>
    <path class="vz-curve" d="${d0}" fill="none" stroke="#fff" stroke-width="2.4" stroke-linejoin="round"/>
    ${s[0].nodes.map(([x, y], i) => `<g class="vz-node${i === 2 ? ' vz-node--dyn' : ''}">
      ${i === 2 ? `<circle class="vz-ring" cx="${f1(nx)}" cy="${f1(ny)}" r="11" fill="none" stroke="#b9a8ff" stroke-width="1.6" opacity=".7"/>` : ''}
      <circle cx="${f1(x)}" cy="${f1(y)}" r="5.2" fill="#14171c" stroke="${BAND[i]}" stroke-width="2.6"/>
    </g>`).join('')}`;
}

// ------------------------------------------------------------------ Compresor (VU + curva de transferencia)
const COMP = { dur: 3400, cx: 88, cy: 104 };
function compressorSvg(id) {
  const { cx, cy } = COMP;
  const r = 78;
  const ang = (v) => (-50 + (v + 20) * (100 / 23)) * (Math.PI / 180); // −20 … +3 VU
  const pt = (v, rr) => [cx + Math.sin(ang(v)) * rr, cy - Math.cos(ang(v)) * rr];
  const ticks = [-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3].map((v) => {
    const [x1, y1] = pt(v, r - 2);
    const [x2, y2] = pt(v, r - (v % 5 === 0 || v === 0 || v === -7 ? 12 : 8));
    return `<line x1="${f1(x1)}" y1="${f1(y1)}" x2="${f1(x2)}" y2="${f1(y2)}" class="${v > 0 ? 'vz-red' : ''}"/>`;
  }).join('');
  const arc = (from, to, rr) => {
    const [x1, y1] = pt(from, rr);
    const [x2, y2] = pt(to, rr);
    return `M${f1(x1)},${f1(y1)} A${rr},${rr} 0 0 1 ${f1(x2)},${f1(y2)}`;
  };
  const labels = [[-20, '20'], [-10, '10'], [-5, '5'], [0, '0'], [3, '+3']].map(([v, t]) => {
    const [x, y] = pt(v, r - 22);
    return `<text x="${f1(x)}" y="${f1(y + 3)}">${t}</text>`;
  }).join('');
  // Curva de transferencia con rodilla suave (relación 4:1 desde −24 dB)
  const gx = 206;
  const gy = 8;
  const gs = 98;
  const io = (inDb) => {
    const t = -24;
    const k = 8;
    if (inDb < t - k / 2) return inDb;
    if (inDb > t + k / 2) return t + (inDb - t) / 4;
    return inDb + ((1 / 4 - 1) * (inDb - t + k / 2) ** 2) / (2 * k);
  };
  const map = (i, o) => [gx + ((i + 48) / 48) * gs, gy + gs - ((o + 48) / 48) * gs];
  const tp = [];
  for (let i = -48; i <= 0; i += 2) tp.push(map(i, io(i)).map(f1).join(','));
  const curve = `M${tp.join(' L')}`;
  specs.set(id, { kind: 'compressor' });
  return `
    <defs>
      <radialGradient id="${id}vu" cx="50%" cy="100%" r="90%"><stop offset="0" stop-color="#ffd28a" stop-opacity=".22"/><stop offset=".7" stop-color="#ffd28a" stop-opacity=".05"/><stop offset="1" stop-color="#ffd28a" stop-opacity="0"/></radialGradient>
      <linearGradient id="${id}l" x1="0" x2="1"><stop offset="0" stop-color="#5ff0ff"/><stop offset=".55" stop-color="#b9a8ff"/><stop offset="1" stop-color="#ffb3e1"/></linearGradient>
      <filter id="${id}g" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="4"/></filter>
    </defs>
    <rect x="6" y="6" width="166" height="103" rx="12" fill="url(#${id}vu)" class="vz-panel"/>
    <path d="${arc(-20, 3, r - 2)}" class="vz-arc"/>
    <path d="${arc(0, 3, r - 2)}" class="vz-arc vz-red"/>
    <g class="vz-ticks">${ticks}</g>
    <g class="vz-labels">${labels}</g>
    <text x="${cx}" y="76" class="vz-vu">VU</text>
    <line x1="${cx}" y1="${cy}" x2="${cx}" y2="${cy - r + 8}" class="vz-needle" transform="rotate(-30 ${cx} ${cy})"/>
    <circle cx="${cx}" cy="${cy}" r="5" class="vz-hub"/>
    <rect x="${gx}" y="${gy}" width="${gs}" height="${gs}" rx="8" class="vz-panel"/>
    <line x1="${gx}" y1="${gy + gs}" x2="${gx + gs}" y2="${gy}" class="vz-diag"/>
    <path d="${curve}" fill="none" stroke="url(#${id}l)" stroke-width="6" opacity=".55" filter="url(#${id}g)"/>
    <path class="vz-tcurve" d="${curve}" fill="none" stroke="#fff" stroke-width="2.2"/>
    <circle class="vz-dot" r="4.2" fill="#fff" cx="0" cy="0" transform="translate(${tp[8].replace(',', ' ')})"/>
    <text x="${gx + gs / 2}" y="${gy + gs + 2}" class="vz-axis" dy="-4">ENTRADA</text>
    <text x="${gx + gs + 12}" y="${gy + gs / 2}" class="vz-axis" transform="rotate(90 ${gx + gs + 12} ${gy + gs / 2})">SALIDA</text>`;
}

// ------------------------------------------------------------------ Delay (repeticiones que decaen)
function delaySvg(id) {
  const taps = Array.from({ length: 7 }, (_, i) => {
    const x = 34 + i * 48;
    const h = 84 * 0.74 ** i;
    return `<rect class="vz-tap" x="${x - 4}" y="${f1(H / 2 - h / 2)}" width="8" height="${f1(h)}" rx="4" fill="url(#${id}t)"/>`;
  }).join('');
  specs.set(id, { kind: 'delay' });
  return `
    <defs><linearGradient id="${id}t" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff"/><stop offset="1" stop-color="#b9a8ff" stop-opacity=".5"/></linearGradient></defs>
    <line x1="10" x2="${W - 10}" y1="${H / 2}" y2="${H / 2}" class="vz-zero"/>
    ${taps}
    <path d="M30,${H / 2 - 44} Q190,${H / 2 - 38} 350,${H / 2 - 4}" class="vz-env"/>`;
}

// ------------------------------------------------------------------ Voz / riser (onda con envolvente y cabezal)
function waveSvg(id, kind) {
  const n = 64;
  const bars = Array.from({ length: n }, (_, i) => {
    const t = i / (n - 1);
    const env = kind === 'voice' ? 0.18 + 0.82 * Math.sin(Math.PI * Math.min(1, t * 1.15)) ** 0.7 * (0.55 + 0.45 * t) : 0.08 + 0.92 * t ** 2.2;
    const h = Math.max(3, (H - 22) * env * (0.55 + 0.45 * Math.abs(Math.sin(i * 1.93) * Math.cos(i * 0.71))));
    const x = 12 + t * (W - 24);
    return `<rect x="${f1(x - 1.6)}" y="${f1(H / 2 - h / 2)}" width="3.2" height="${f1(h)}" rx="1.6"/>`;
  }).join('');
  specs.set(id, { kind: 'wave' });
  return `
    <defs>
      <linearGradient id="${id}w" x1="0" x2="1"><stop offset="0" stop-color="#5ff0ff"/><stop offset=".5" stop-color="#b9a8ff"/><stop offset="1" stop-color="#ffb3e1"/></linearGradient>
      <linearGradient id="${id}p" x1="0" x2="1"><stop offset="0" stop-color="#fff" stop-opacity="0"/><stop offset="1" stop-color="#fff" stop-opacity=".22"/></linearGradient>
    </defs>
    <g fill="url(#${id}w)" opacity=".9">${bars}</g>
    <g class="vz-head" transform="translate(${f1(W * 0.62)} 0)">
      <rect x="-60" y="4" width="60" height="${H - 8}" fill="url(#${id}p)"/>
      <line x1="0" x2="0" y1="4" y2="${H - 4}" stroke="#fff" stroke-width="1.6"/>
    </g>`;
}

/** SVG estático del tipo de plugin. `compact` quita la grilla (tarjetas pequeñas). */
export function visualSvg(kind, { compact = false } = {}) {
  uid += 1;
  const id = `vz${uid}`;
  const body = kind === 'eq' ? eqSvg(id, compact)
    : kind === 'compressor' ? compressorSvg(id)
      : kind === 'delay' ? delaySvg(id)
        : waveSvg(id, kind === 'voice' ? 'voice' : 'riser');
  if (compact) specs.delete(id); // miniaturas: siempre estáticas
  return `<svg class="vz vz--${kind}" data-vz="${id}" viewBox="0 0 ${W} ${H}" preserveAspectRatio="xMidYMid meet" aria-hidden="true" focusable="false">${body}</svg>`;
}

// ------------------------------------------------------------------ Motor de movimiento
// Un solo repintado por cuadro y por visual: Chromium no compone en la GPU las animaciones de los
// elementos INTERNOS de un SVG (las repinta en el hilo principal a la frecuencia del monitor, con
// filtro incluido). Por eso todo lo que se mueve dentro del SVG lo escribe este motor, a la cadencia
// que cada tipo necesita, con un reloj propio que se congela mientras el visual no se ve.
const ease = (t) => (t < 0.5 ? 4 * t * t * t : 1 - (-2 * t + 2) ** 3 / 2); // suave entrada/salida
const lerp = (a, b, t) => a + (b - a) * t;
const wave01 = (t, period) => 0.5 - 0.5 * Math.cos((2 * Math.PI * t) / period); // 0 → 1 → 0
const live = new Set();
let loopOn = false;
let timer = 0;

/** ¿Se ve este visual? Escena activa sin ficha encima, o dentro de la ficha abierta; nunca con la app en reposo. */
function visible(svg) {
  if (!svg.isConnected || document.documentElement.classList.contains('is-idle')) return false;
  const sheet = svg.closest('#sheet');
  if (sheet) return !sheet.hidden;
  if (document.body.classList.contains('sheet-open')) return false;
  const scene = svg.closest('.scene');
  return !scene || scene.classList.contains('is-active');
}

/** Interpola entre estados con tiempos clave y curva suave. */
function segment(times, p) {
  let i = 0;
  while (i < times.length - 2 && p > times[i + 1]) i += 1;
  return [i, ease((p - times[i]) / (times[i + 1] - times[i]))];
}

const NEEDLE = { deg: [-46, -14, -34, -4, -26, -46], at: [0.3, 0.8, 0.52, 0.95, 0.66, 0.3], times: [0, 0.18, 0.42, 0.6, 0.82, 1] };

function draw(e) {
  const t = e.time;
  if (e.kind === 'eq') {
    const s = getEqStates();
    const [i, k] = segment(EQ_CYCLE.times, (t % EQ_CYCLE.dur) / EQ_CYCLE.dur);
    const d = eqPath(s[i].ys.map((y, j) => lerp(y, s[i + 1].ys[j], k)));
    e.curves.forEach((c) => c.setAttribute('d', d));
    const ny = f1(lerp(s[i].nodes[2][1], s[i + 1].nodes[2][1], k));
    e.dyn.forEach((c) => c.setAttribute('cy', ny));
    const pulse = wave01(t, 1150); // anillo de la banda dinámica
    e.ring.setAttribute('r', f1(lerp(8, 13, pulse)));
    e.ring.setAttribute('opacity', (0.8 - 0.7 * pulse).toFixed(3));
    e.spec.setAttribute('opacity', wave01(t, 1900).toFixed(3)); // el espectro “respira”
  } else if (e.kind === 'compressor') {
    // La aguja y el punto comparten ciclo: el punto recorre la curva hasta donde comprime.
    const [i, k] = segment(NEEDLE.times, (t % COMP.dur) / COMP.dur);
    e.needle.setAttribute('transform', `rotate(${lerp(NEEDLE.deg[i], NEEDLE.deg[i + 1], k).toFixed(2)} ${COMP.cx} ${COMP.cy})`);
    const pt = e.path.getPointAtLength(e.length * lerp(NEEDLE.at[i], NEEDLE.at[i + 1], k));
    e.dot.setAttribute('transform', `translate(${f1(pt.x)} ${f1(pt.y)})`);
  } else if (e.kind === 'delay') {
    e.taps.forEach((tap, i) => {
      const p = ((((t - i * 240) % 2800) + 2800) % 2800) / 2800;
      tap.setAttribute('opacity', (p < 0.12 ? 0.25 + 0.75 * (p / 0.12) : 1 - 0.75 * ((p - 0.12) / 0.88)).toFixed(3));
    });
  } else {
    e.head.setAttribute('transform', `translate(${f1(12 + ((t % 3600) / 3600) * (W - 24))} 0)`);
  }
}

function tick(now) {
  loopOn = false;
  let wait = Infinity;
  for (const e of live) {
    if (!e.svg.isConnected) { live.delete(e); continue; }
    if (!visible(e.svg)) { e.prev = null; continue; } // reloj congelado: retoma donde quedó
    if (e.prev != null) e.time += now - e.prev;
    e.prev = now;
    if (now - e.last >= e.every - 2) {
      draw(e);
      e.last = now;
    }
    wait = Math.min(wait, e.every - (now - e.last));
  }
  clearTimeout(timer);
  // El bucle se despierta solo cuando a algún visual le toca dibujar; sin nada visible, revisa cada 250 ms.
  if (wait < Infinity) timer = setTimeout(schedule, Math.max(0, wait - 2));
  else if (live.size) timer = setTimeout(schedule, 250);
}
function schedule() {
  if (loopOn) return;
  loopOn = true;
  requestAnimationFrame(tick);
}

// Cadencia por tipo: lo lento a 30 fps (pasos de menos de medio píxel), lo rápido a 60.
const EVERY = { eq: 1000 / 30, delay: 1000 / 30, compressor: 1000 / 60, wave: 1000 / 60 };

/** Da vida a los visuales dentro de `root`. No hace nada con movimiento reducido. */
export function mountVisuals(root) {
  if (matchMedia('(prefers-reduced-motion: reduce)').matches) return;
  root.querySelectorAll('svg[data-vz]').forEach((svg) => {
    const spec = specs.get(svg.dataset.vz);
    specs.delete(svg.dataset.vz);
    if (!spec || svg.dataset.vzLive) return;
    svg.dataset.vzLive = '1';
    const e = { svg, kind: spec.kind, time: 0, prev: null, last: -Infinity, every: EVERY[spec.kind] };
    if (spec.kind === 'eq') {
      e.curves = [...svg.querySelectorAll('.vz-curve')];
      e.dyn = [...svg.querySelectorAll('.vz-node--dyn circle')];
      e.ring = svg.querySelector('.vz-ring');
      e.spec = svg.querySelector('.vz-spec--b');
    } else if (spec.kind === 'compressor') {
      e.path = svg.querySelector('.vz-tcurve');
      e.length = e.path.getTotalLength();
      e.dot = svg.querySelector('.vz-dot');
      e.needle = svg.querySelector('.vz-needle');
      e.needle.style.removeProperty('transform');
      e.needle.style.removeProperty('transform-origin');
      e.dot.style.removeProperty('transform');
    } else if (spec.kind === 'delay') {
      e.taps = [...svg.querySelectorAll('.vz-tap')];
    } else {
      e.head = svg.querySelector('.vz-head');
      e.head.style.removeProperty('transform');
    }
    live.add(e);
  });
  schedule();
}

const knob = (label, rot) => `<span class="dknob"><span class="knob" style="--rot:${rot}deg"></span>${label ? `<span class="dknob__label">${label}</span>` : ''}</span>`;

/** Fila de controles del dispositivo, con los nombres reales de cada plugin cuando existen. */
export function deviceControls(kind) {
  switch (kind) {
    case 'eq':
      return `<span class="dctl dctl--eq"><span class="dctl__bands">${BAND.map((c, i) => `<span class="dband${i === 2 ? ' is-active' : ''}" style="--c:${c}">${i + 1}</span>`).join('')}<span class="dband dband--add">+</span></span>
        <span class="dctl__knobs">${knob('Frecuencia', -48)}${knob('Ganancia', 38)}${knob('Q', -12)}</span></span>`;
    case 'compressor':
      return `<span class="dctl"><span class="dctl__knobs dctl__knobs--wide">${knob('Reducción', 40)}${knob('Ganancia', -18)}${knob('Recuperación', 8)}${knob('Mezcla', 130)}</span><span class="dpill">Limit</span></span>`;
    case 'delay':
      return `<span class="dctl"><span class="dctl__knobs dctl__knobs--wide">${knob('Tiempo', -30)}${knob('Repeticiones', 50)}${knob('Mezcla', 20)}</span></span>`;
    case 'voice':
      return `<span class="dctl dctl--modes">${['Riser', 'Reverse', 'Tape stop', 'Dub throw'].map((m, i) => `<span class="dmode${i === 0 ? ' is-active' : ''}">${m}</span>`).join('')}</span>`;
    default:
      return `<span class="dctl"><span class="dctl__knobs dctl__knobs--wide">${[-120, -40, 30, 95, 150].map((r) => knob('', r)).join('')}</span></span>`;
  }
}
