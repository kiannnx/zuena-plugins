// Verificación numérica del diseño de filtros y parametrización TPT SVF sin ejecutables nativos
// (EB-12 / EB-13). Réplica línea a línea de Source/Core/FilterDesign.cpp (10 tipos + toSvf).
// Uso: node tools/verify-filters.mjs   (sale con código 1 si falla)

import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const PI = Math.PI;
const MAX_POLE_W = 0.98 * PI;
const MAX_FIT_W = 0.92 * PI;
const BW_Q = Math.SQRT1_2;
const MIN_Q = 0.025, MAX_Q = 40, MIN_F = 10, MAX_F = 30000, MAX_GAIN = 30;
export const T = {
  Bell: 0, LowShelf: 1, HighShelf: 2, LowCut: 3, HighCut: 4,
  Notch: 5, BandPass: 6, Tilt: 7, FlatTilt: 8, AllPass: 9,
};
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

function mag2(a, w) {
  const w2 = w * w;
  const nr = a.n0 - a.n2 * w2, ni = a.n1 * w, dr = a.d0 - a.d2 * w2, di = a.d1 * w;
  return (nr * nr + ni * ni) / (dr * dr + di * di);
}
const sec = (n2, n1, n0, d2, d1, d0) => ({ n2, n1, n0, d2, d1, d0 });

function addCut(p, hp, w0, q, order) {
  const pairs = Math.floor(order / 2), res = q / BW_Q;
  for (let k = 0; k < pairs; k++) {
    let qk = 1 / (2 * Math.sin(((2 * k + 1) * PI) / (2 * order)));
    if (k === 0) qk *= res;
    p.s.push(hp ? sec(1, 0, 0, 1, w0 / qk, w0 * w0) : sec(0, 0, w0 * w0, 1, w0 / qk, w0 * w0));
  }
  if (order % 2 === 1) p.s.push(hp ? sec(0, 1, 0, 0, 1, w0) : sec(0, 0, w0, 0, 1, w0));
}

function addFlatTilt(p, wPivot, gainDb, wScale) {
  const pivots = [22, 140, 900, 5800];
  const alpha = 10 ** (gainDb / 160);
  for (const hz of pivots) {
    const wk = hz * wScale, wz = wk / alpha, wp = wk * alpha;
    p.s.push(sec(0, 1 / wz, 1, 0, 1 / wp, 1));
  }
  let m2 = 1;
  for (const a of p.s) m2 *= mag2(a, wPivot);
  p.gain = 1 / Math.sqrt(Math.max(m2, 1e-30));
}

function makeProto(type, w0, gainDb, q, slope, wScale = 1) {
  q = clamp(q, MIN_Q, MAX_Q);
  gainDb = clamp(gainDb, -2 * MAX_GAIN, 2 * MAX_GAIN);
  const A = 10 ** (gainDb / 40), sA = Math.sqrt(A), w02 = w0 * w0;
  const p = { s: [], gain: 1 };
  switch (type) {
    case T.Bell: p.s.push(sec(1, (A * w0) / q, w02, 1, w0 / (A * q), w02)); break;
    case T.LowShelf: p.s.push(sec(A, (A * sA * w0) / q, A * A * w02, A, (sA * w0) / q, w02)); break;
    case T.HighShelf: p.s.push(sec(A * A, (A * sA * w0) / q, A * w02, 1, (sA * w0) / q, A * w02)); break;
    case T.Tilt: p.s.push(sec(A, (sA * w0) / q, w02, 1, (sA * w0) / q, A * w02)); break;
    case T.FlatTilt: addFlatTilt(p, w0, gainDb, wScale); break;
    case T.Notch: p.s.push(sec(1, 0, w02, 1, w0 / q, w02)); break;
    case T.BandPass: p.s.push(sec(0, w0 / q, 0, 1, w0 / q, w02)); break;
    case T.LowCut: addCut(p, true, w0, q, clamp(Math.trunc(slope / 6), 1, 16)); break;
    case T.HighCut: addCut(p, false, w0, q, clamp(Math.trunc(slope / 6), 1, 16)); break;
    case T.AllPass: p.s.push(sec(1, -w0 / q, w02, 1, w0 / q, w02)); break;
  }
  return p;
}

function matchFirst(a) {
  const wp = Math.min(a.d0 / a.d1, MAX_POLE_W);
  const pole = Math.exp(-wp);
  const s = { b0: 0, b1: 0, b2: 0, a1: -pole, a2: 0 };
  const W = Math.sqrt(mag2(a, 0)) * (1 + s.a1), V = Math.sqrt(mag2(a, PI)) * (1 - s.a1);
  s.b0 = 0.5 * (W + V); s.b1 = 0.5 * (W - V);
  return s;
}

function matchSecond(a, wFit) {
  const wp = Math.sqrt(a.d0 / a.d2), zeta = a.d1 / (2 * Math.sqrt(a.d0 * a.d2)), w = Math.min(wp, MAX_POLE_W);
  const e = Math.exp(-zeta * w);
  const s = { b0: 0, b1: 0, b2: 0, a1: 0, a2: e * e };
  s.a1 = zeta <= 1 ? -2 * e * Math.cos(w * Math.sqrt(1 - zeta * zeta)) : -2 * e * Math.cosh(w * Math.sqrt(zeta * zeta - 1));
  const A0 = (1 + s.a1 + s.a2) ** 2, A1 = (1 - s.a1 + s.a2) ** 2, A2 = -4 * s.a2;
  const wf = clamp(wFit, 1e-6, MAX_FIT_W), sh = Math.sin(0.5 * wf), phi1 = sh * sh, phi0 = 1 - phi1, phi2 = 4 * phi0 * phi1;
  const Dwf = A0 * phi0 + A1 * phi1 + A2 * phi2;

  if (a.n0 === 0 && a.n1 === 0 && a.n2 > 0) {
    const b0 = Math.sqrt(Math.max(mag2(a, wf) * Dwf, 0)) / (4 * phi1);
    return { ...s, b0, b1: -2 * b0, b2: b0 };
  }

  const B0 = mag2(a, 0) * A0, B1 = mag2(a, PI) * A1;
  const B2 = (mag2(a, wf) * Dwf - B0 * phi0 - B1 * phi1) / phi2;
  const W = Math.sqrt(Math.max(B0, 0)), V = Math.sqrt(Math.max(B1, 0)), S = 0.5 * (W + V);
  s.b1 = 0.5 * (W - V);

  let rMin = 0;
  if (a.n2 > 0 && a.n0 > 0) {
    const wz = Math.min(Math.sqrt(a.n0 / a.n2), MAX_POLE_W);
    const zetaz = a.n1 / (2 * Math.sqrt(a.n0 * a.n2));
    rMin = S * Math.tanh(zetaz * wz);
  }
  const r = Math.max(Math.sqrt(Math.max(S * S + B2, 0)), rMin);
  s.b0 = 0.5 * (S + r); s.b2 = 0.5 * (S - r);
  if (Math.abs(s.b2) > Math.abs(s.b0)) [s.b0, s.b2] = [s.b2, s.b0];
  return s;
}
const match = (a, wFit) => (a.d2 === 0 ? matchFirst(a) : matchSecond(a, wFit));

function invert(s) {
  const k = 1 / s.b0;
  return { b0: k, b1: s.a1 * k, b2: s.a2 * k, a1: s.b1 * k, a2: clamp(s.b2 * k, -0.999999, 0.999999) };
}

function designNotch(w0, q) {
  const s = matchSecond(makeProto(T.Notch, w0, 0, q, 12).s[0], w0);
  const c = Math.cos(clamp(w0, 1e-6, 0.999 * PI)), k = (1 + s.a1 + s.a2) / (2 - 2 * c);
  return { ...s, b0: k, b1: -2 * c * k, b2: k };
}

function designAllPass(w0, q) {
  const w = clamp(w0, 1e-6, 0.995 * PI), g = Math.tan(0.5 * w), k = 1 / clamp(q, MIN_Q, MAX_Q);
  const d0 = 1 + g * (g + k), a1 = (2 * (g * g - 1)) / d0, a2 = (1 + g * (g - k)) / d0;
  return { b0: a2, b1: a1, b2: 1, a1, a2 };
}

export function designBand(type, freqHz, gainDb, q, slope, fs) {
  const wScale = (2 * PI) / fs;
  const w0 = clamp(freqHz, MIN_F, MAX_F) * wScale;
  if (type === T.Notch) return [designNotch(w0, q)];
  if (type === T.AllPass) return [designAllPass(w0, q)];
  let inv = false, g = gainDb;
  if ((type === T.Bell || type === T.LowShelf) && gainDb < 0) { g = -gainDb; inv = true; }
  else if ((type === T.HighShelf || type === T.Tilt) && gainDb > 0) { g = -gainDb; inv = true; }
  const p = makeProto(type, w0, g, q, slope, wScale);
  const isShelf = type === T.LowShelf || type === T.HighShelf || type === T.Tilt;
  const d = p.s.map((a) => match(a, isShelf && a.n2 > 0 && a.n0 > 0 ? Math.sqrt(a.n0 / a.n2) : w0));
  if (inv) d[0] = invert(d[0]);
  if (p.gain !== 1 && d.length > 0) { d[0].b0 *= p.gain; d[0].b1 *= p.gain; d[0].b2 *= p.gain; }
  return d;
}

export function designBandPass(freqHz, q, fs) {
  const w0 = (2 * PI * clamp(freqHz, MIN_F, MAX_F)) / fs;
  return match(makeProto(T.BandPass, w0, 0, q, 12).s[0], w0);
}

export function toSvfSection(b) {
  if (b.a2 === 0 && b.b2 === 0) {
    const dm = Math.max(1 - b.a1, 1e-15), dp = Math.max(1 + b.a1, 1e-15);
    const g = dp / dm, chp = (b.b0 - b.b1) / dm, cbp = (b.b0 + b.b1) / dp - chp;
    return { order: 1, g, k: 0, a1: g / (1 + g), a2: 0, a3: 0, chp, cbp, clp: 0 };
  }
  const dp = Math.max(1 + b.a1 + b.a2, 1e-18), dm = Math.max(1 - b.a1 + b.a2, 1e-18);
  const root = Math.sqrt(dp * dm), g = Math.sqrt(dp / dm), k = (2 * (1 - b.a2)) / root;
  const a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
  return { order: 2, g, k, a1, a2, a3, chp: (b.b0 - b.b1 + b.b2) / dm, cbp: (2 * (b.b0 - b.b2)) / root, clp: (b.b0 + b.b1 + b.b2) / dp };
}

export const toSvf = (d) => d.map(toSvfSection);

export function responseDb(d, f, fs) {
  const w = (2 * PI * f) / fs, c1 = Math.cos(w), s1 = -Math.sin(w), c2 = Math.cos(2 * w), s2 = -Math.sin(2 * w);
  let m = 1;
  for (const s of d) {
    const nr = s.b0 + s.b1 * c1 + s.b2 * c2, ni = s.b1 * s1 + s.b2 * s2;
    const dr = 1 + s.a1 * c1 + s.a2 * c2, di = s.a1 * s1 + s.a2 * s2;
    m *= Math.sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
  }
  return 20 * Math.log10(Math.max(m, 1e-15));
}

export function analogDb(type, f, g, q, slope, ef) {
  const p = makeProto(type, clamp(f, MIN_F, MAX_F), g, q, slope, 1);
  let m2 = p.gain * p.gain;
  for (const a of p.s) m2 *= mag2(a, ef);
  return 10 * Math.log10(Math.max(m2, 1e-30));
}

// ------------------------------------------------------------------------- verificación
if (process.argv[1] && resolve(fileURLToPath(import.meta.url)) === resolve(process.argv[1])) {
  const rates = [44100, 48000, 96000, 192000];
  const freqs = [10, 20, 50, 200, 1000, 5000, 8000, 12000, 16000, 19000];
  const gains = [-30, -12, -3, 3, 12, 30];
  const qs = [0.1, 0.5, BW_Q, 1, 2, 10, 40];
  const slopes = [6, 12, 18, 24, 36, 48, 72, 96];
  let failed = false;

  const TOL_BY_TYPE = {
    Bell: 0.45, LowShelf: 0.85, HighShelf: 0.85, LowCut: 0.45, HighCut: 0.75,
    Notch: 0.30, BandPass: 0.55, Tilt: 0.85, FlatTilt: 0.15, AllPass: 0.001,
  };
  const SHELF_TYPES = [T.LowShelf, T.HighShelf, T.Tilt, T.FlatTilt];
  const isReal = (type, f, g, q, fs) =>
    f <= 0.2 * fs && q >= 0.3 && q <= (SHELF_TYPES.includes(type) ? 2 : 10) && Math.abs(g) <= 24;
  const audible = (type, ana) => ![T.LowCut, T.HighCut, T.BandPass, T.Notch].includes(type) || ana >= -12;

  for (const [name, type] of Object.entries(T)) {
    let worstLo = { e: 0 }, worstHi = { e: 0 }, worstReal = { e: 0 };
    for (const fs of rates) for (const f of freqs) for (const g of gains) for (const q of qs) for (const sl of slopes) {
      const cut = type === T.LowCut || type === T.HighCut;
      if (!cut && sl !== 12) continue;
      if (cut && q > 10) continue;
      const d = designBand(type, f, g, q, sl, fs);
      const svf = toSvf(d);
      for (let i = 0; i < d.length; i++) {
        const s = d[i], sv = svf[i];
        const stable = Math.abs(s.a2) < 1 && Math.abs(s.a1) < 1 + s.a2 && sv.g > 0 && sv.k >= 0;
        if (!stable || !Object.values(s).every(Number.isFinite) || !Object.values(sv).every(Number.isFinite)) {
          console.log(`INESTABLE/NaN ${name} fs=${fs} f=${f} g=${g} q=${q} sl=${sl}`, s, sv);
          failed = true;
        }
      }
      for (let k = 0; k <= 300; k++) {
        const ef = 10 * 2000 ** (k / 300);
        if (ef > 0.25 * fs) break;
        const ana = analogDb(type, f, g, q, sl, ef);
        if (ana < -24 || (type === T.Notch && ana < -20)) continue;
        const dig = responseDb(d, ef, fs), err = Math.abs(dig - ana);
        const at = `fs=${fs} f=${f} g=${g} q=${q} sl=${sl} ef=${ef.toFixed(0)} dig=${dig.toFixed(2)} ana=${ana.toFixed(2)}`;
        const w = f <= 0.2 * fs ? worstLo : worstHi;
        if (err > w.e) Object.assign(w, { e: err, at });
        if (isReal(type, f, g, q, fs) && audible(type, ana) && err > worstReal.e) Object.assign(worstReal, { e: err, at });
      }
    }
    const ok = worstReal.e <= TOL_BY_TYPE[name];
    if (!ok) failed = true;
    console.log(`${ok ? 'OK  ' : 'FAIL'} ${name.padEnd(9)} uso real: ${worstReal.e.toFixed(4)} dB  ${worstReal.at ?? ''}`);
    console.log(`     ${''.padEnd(9)} extremo <=fs/5: ${worstLo.e.toFixed(3)} dB · >fs/5: ${worstHi.e.toFixed(3)} dB`);
  }
  process.exitCode = failed ? 1 : 0;
}
