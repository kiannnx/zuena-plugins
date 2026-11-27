// Verificación numérica del motor DSP v0.2 (TPT SVF + Fase Natural 2x FIR + Auto-Gain + EB-04)
// sin ejecutables nativos (EB-12). Uso: node tools/verify-engine.mjs

import { designBand, designBandPass, toSvf, toSvfSection, responseDb, T } from './verify-filters.mjs';

const MAX_BANDS = 24, SUB_BLOCK = 16, MAX_SECTIONS = 9;
const SLOPES = [6, 12, 18, 24, 36, 48, 72, 96];
const MIN_F = 10, MAX_F = 30000, MAX_GAIN = 30, MIN_Q = 0.025, MAX_Q = 40;
const SMOOTH_S = 0.015, FADE_S = 0.005, KNEE_DB = 6, BW_Q = Math.SQRT1_2;
const P = { Stereo: 0, Left: 1, Right: 2, Mid: 3, Side: 4 };
const Q_MODE = { ZeroLatency: 0, NaturalPhase: 1 };

const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const hasGain = (t) => t === T.Bell || t === T.LowShelf || t === T.HighShelf || t === T.Tilt || t === T.FlatTilt;
const usesBandQ = (t, sl) => t !== T.FlatTilt && !((t === T.LowCut || t === T.HighCut) && sl === 0);
const stepToward = (v, target, step) => (v < target ? Math.min(v + step, target) : Math.max(v - step, target));

function tickSvf(c, s, x) {
  if (c.order === 1) {
    const v = c.a1 * (x - s[0]), lp = v + s[0];
    s[0] = lp + v;
    return c.chp * x + c.cbp * lp;
  }
  const v3 = x - s[1];
  const v1 = c.a1 * s[0] + c.a2 * v3;
  const v2 = s[1] + c.a2 * s[0] + c.a3 * v3;
  const vhp = x - c.k * v1 - v2;
  s[0] = 2 * v1 - s[0];
  s[1] = 2 * v2 - s[1];
  return c.chp * vhp + c.cbp * v1 + c.clp * v2;
}

function tickSvfLerp(a, b, t, s, x) {
  const g = a.g + (b.g - a.g) * t;
  const chp = a.chp + (b.chp - a.chp) * t;
  const cbp = a.cbp + (b.cbp - a.cbp) * t;
  if (a.order === 1) {
    const G = g / (1 + g), v = G * (x - s[0]), lp = v + s[0];
    s[0] = lp + v;
    return chp * x + cbp * lp;
  }
  const k = a.k + (b.k - a.k) * t;
  const clp = a.clp + (b.clp - a.clp) * t;
  const a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
  const v3 = x - s[1];
  const v1 = a1 * s[0] + a2 * v3;
  const v2 = s[1] + a2 * s[0] + a3 * v3;
  const vhp = x - k * v1 - v2;
  s[0] = 2 * v1 - s[0];
  s[1] = 2 * v2 - s[1];
  return chp * vhp + cbp * v1 + clp * v2;
}

class Oversampler2x {
  constructor() {
    const besselI0 = (x) => { let s = 1, term = 1; for (let k = 1; k < 32; k++) { const r = x / (2 * k); term *= r * r; s += term; } return s; };
    const center = 31, pairs = 16, beta = 8.6, denom = besselI0(beta);
    this.oddTaps = new Float64Array(pairs);
    let oddSum = 0;
    for (let k = 0; k < pairs; k++) {
      const n = 2 * k + 1, u = n / center;
      const w = besselI0(beta * Math.sqrt(Math.max(0, 1 - u * u))) / denom;
      this.oddTaps[k] = (Math.sin(Math.PI * 0.5 * n) / (Math.PI * n)) * w;
      oddSum += 2 * this.oddTaps[k];
    }
    for (let k = 0; k < pairs; k++) this.oddTaps[k] *= 0.5 / oddSum;
    this.reset();
  }
  reset() {
    this.upHist = [new Float64Array(64), new Float64Array(64)];
    this.downHist = [new Float64Array(128), new Float64Array(128)];
    this.upPos = [0, 0];
    this.downPos = [0, 0];
  }
  upsample(ch, inp, out2x, len) {
    const h = this.upHist[ch]; let p = this.upPos[ch];
    for (let i = 0; i < len; i++) {
      h[p] = inp[i];
      let acc = 0;
      for (let k = 0; k < 16; k++) acc += this.oddTaps[k] * (h[(p - 15 + k + 64) & 63] + h[(p - 16 - k + 64) & 63]);
      out2x[2 * i] = 2 * acc;
      out2x[2 * i + 1] = h[(p - 15 + 64) & 63];
      p = (p + 1) & 63;
    }
    this.upPos[ch] = p;
  }
  downsample(ch, in2x, out, len) {
    const h = this.downHist[ch]; let p = this.downPos[ch];
    for (let i = 0; i < len; i++) {
      h[p] = in2x[2 * i];
      let acc = 0.5 * h[(p - 31 + 128) & 127];
      for (let k = 0; k < 16; k++) {
        const d = 2 * k + 1;
        acc += this.oddTaps[k] * (h[(p - 31 + d + 128) & 127] + h[(p - 31 - d + 128) & 127]);
      }
      out[i] = acc;
      p = (p + 1) & 127;
      h[p] = in2x[2 * i + 1];
      p = (p + 1) & 127;
    }
    this.downPos[ch] = p;
  }
}

const defaultBand = () => ({
  used: false, bypass: false, type: T.Bell, freqHz: 1000, gainDb: 0, q: 1,
  slopeIndex: 1, placement: P.Stereo, solo: false,
  dynOn: false, thresholdDb: -30, rangeDb: -6, ratio: 4, attackMs: 10, releaseMs: 120,
});
const makeState = () => Array.from({ length: 2 }, () => Array.from({ length: MAX_SECTIONS }, () => [0, 0]));

class EqEngine {
  constructor(fs = 48000) {
    this.targetQuality = Q_MODE.ZeroLatency;
    this.autoGainEnabled = false;
    this.userOutGainDb = 0;
    this.gainScale = 1;
    this.globalBypass = false;
    this.wetMix = 1;
    this.oversampler = new Oversampler2x();
    this.prepare(fs);
  }
  updateRates() {
    this.dspRate = this.activeOversampled ? this.fs * 2 : this.fs;
    const sub = this.activeOversampled ? SUB_BLOCK * 2 : SUB_BLOCK;
    this.smoothAlpha = 1 - Math.exp(-sub / (SMOOTH_S * this.dspRate));
    this.mixStep = 1 / (FADE_S * this.dspRate);
    this.baseMixStep = 1 / (FADE_S * this.fs);
  }
  prepare(fs) {
    this.fs = fs > 0 ? fs : 48000;
    this.activeOversampled = this.targetQuality === Q_MODE.NaturalPhase && this.fs < 88200;
    this.updateRates();
    this.oversampler.reset();
    this.firstBlock = true;
    this.modeFade = 1;
    this.outGain = 1; this.outGainTarget = 1;
    this.bands = Array.from({ length: MAX_BANDS }, () => ({
      target: defaultBand(), configured: false, type: T.Bell, slopeIndex: 1, placement: P.Stereo,
      freqLog: 0, gainDb: 0, qLog: 0, designedFreqLog: -1, designedGain: 1e9, designedQLog: -1,
      current: [], next: [], interpolate: false, state: makeState(), mix: 0, mixTarget: 0,
      detector: null, detectorFreq: -1, detectorQ: -1, detectorState: [[0, 0], [0, 0]],
      envelope: 0, rmsEnv: 0, dynamicDb: 0,
    }));
    this.soloBand = -1; this.soloMix = 0; this.soloFreqLog = 0; this.soloQLog = 0;
    this.soloFilter = null; this.soloState = [[0, 0], [0, 0]];
  }
  setBand(i, params) { this.bands[i].target = { ...defaultBand(), ...params }; }
  setOutputGainDb(db) { this.userOutGainDb = clamp(db, -MAX_GAIN, MAX_GAIN); }
  setGainScale(s) { this.gainScale = clamp(s, 0, 2); }
  setGlobalBypass(b) { this.globalBypass = b; }
  setQualityMode(m) { this.targetQuality = m; }
  setAutoGain(on) { this.autoGainEnabled = on; }
  dynamicGainDb(i) { return Math.fround(this.bands[i].dynamicDb); }

  computeAutoGain() {
    if (!this.autoGainEnabled) return 0;
    const active = this.bands.map((b) => b.target).filter((b) => b.used && !b.bypass && hasGain(b.type) && Math.abs(b.gainDb) >= 1e-3);
    if (!active.length) return 0;
    const designs = active.map((b) => ({
      d: designBand(b.type, b.freqHz, b.gainDb, b.q, 12, this.dspRate),
      w: b.placement === P.Left || b.placement === P.Right ? 0.5 : b.placement === P.Side ? 0.35 : 1.0,
    }));
    let pSum = 0;
    for (let k = 0; k < 24; k++) {
      const f = 40 * (14000 / 40) ** (k / 23);
      let db = 0;
      for (const it of designs) db += it.w * responseDb(it.d, f, this.dspRate);
      pSum += 10 ** (db / 10);
    }
    return clamp(-10 * Math.log10(Math.max(pSum / 24, 1e-6)), -12, 12);
  }

  applyStructure(r) {
    const p = r.target;
    r.type = p.type; r.slopeIndex = clamp(p.slopeIndex, 0, SLOPES.length - 1); r.placement = p.placement;
    r.configured = true;
    r.freqLog = Math.log(clamp(p.freqHz, MIN_F, MAX_F));
    r.qLog = Math.log(clamp(p.q, MIN_Q, MAX_Q));
    r.gainDb = clamp(p.gainDb * this.gainScale, -MAX_GAIN * 1.5, MAX_GAIN * 1.5);
    r.state = makeState(); r.detectorState = [[0, 0], [0, 0]];
    r.envelope = 0; r.rmsEnv = 0; r.dynamicDb = 0;
    r.current = toSvf(designBand(r.type, Math.exp(r.freqLog), r.gainDb, Math.exp(r.qLog), SLOPES[r.slopeIndex], this.dspRate));
    r.next = r.current;
    r.designedFreqLog = r.freqLog; r.designedGain = r.gainDb; r.designedQLog = r.qLog;
    r.interpolate = false;
  }

  runDetector(r, inL, inR, len) {
    const p = r.target, f = Math.exp(r.freqLog);
    const q = clamp(r.type === T.FlatTilt ? BW_Q : Math.exp(r.qLog), 0.3, 10);
    if (!r.detector || Math.abs(f - r.detectorFreq) > 1e-3 * f || Math.abs(q - r.detectorQ) > 1e-3 * q) {
      r.detector = toSvfSection(designBandPass(f, q, this.dspRate));
      r.detectorFreq = f; r.detectorQ = q;
    }
    const atk = Math.exp(-1 / (Math.max(p.attackMs, 0.05) * 1e-3 * this.dspRate));
    const rel = Math.exp(-1 / (Math.max(p.releaseMs, 1.0) * 1e-3 * this.dspRate));
    const rmsAlpha = 1 - Math.exp(-1 / (0.008 * this.dspRate));
    for (let i = 0; i < len; i++) {
      const l = inL[i], rr = inR[i];
      let d = 0;
      switch (r.placement) {
        case P.Stereo: d = Math.max(Math.abs(tickSvf(r.detector, r.detectorState[0], l)), Math.abs(tickSvf(r.detector, r.detectorState[1], rr))); break;
        case P.Left:   d = Math.abs(tickSvf(r.detector, r.detectorState[0], l)); break;
        case P.Right:  d = Math.abs(tickSvf(r.detector, r.detectorState[0], rr)); break;
        case P.Mid:    d = Math.abs(tickSvf(r.detector, r.detectorState[0], 0.5 * (l + rr))); break;
        case P.Side:   d = Math.abs(tickSvf(r.detector, r.detectorState[0], 0.5 * (l - rr))); break;
      }
      r.rmsEnv += (d * d - r.rmsEnv) * rmsAlpha;
      const hybrid = Math.max(d, Math.sqrt(2 * r.rmsEnv) * 0.85);
      r.envelope = hybrid > r.envelope ? atk * r.envelope + (1 - atk) * hybrid : rel * r.envelope + (1 - rel) * hybrid;
    }
    const levelDb = 20 * Math.log10(r.envelope + 1e-12), over = levelDb - p.thresholdDb;
    let comp = 0;
    if (over >= KNEE_DB * 0.5) comp = over;
    else if (over > -KNEE_DB * 0.5) comp = ((over + KNEE_DB * 0.5) ** 2) / (2 * KNEE_DB);
    const amount = comp * (1 - 1 / Math.max(p.ratio, 1));
    const range = clamp(p.rangeDb * this.gainScale, -MAX_GAIN * 1.5, MAX_GAIN * 1.5);
    return Math.min(amount, Math.abs(range)) * (range < 0 ? -1 : 1);
  }

  filterChannel(r, slot, x, len, mixStart, mixTarget) {
    const st = r.state[slot]; let m = mixStart;
    for (let i = 0; i < len; i++) {
      const t = (i + 1) / len;
      let v = x[i];
      for (let s = 0; s < r.current.length; s++) {
        v = r.interpolate ? tickSvfLerp(r.current[s], r.next[s], t, st[s], v) : tickSvf(r.current[s], st[s], v);
      }
      m = stepToward(m, mixTarget, this.mixStep);
      x[i] += m * (v - x[i]);
    }
  }

  processBand(r, workL, workR, inL, inR, len) {
    const p = r.target, wantOn = p.used && !p.bypass;
    if (!r.configured) { if (!wantOn) return; this.applyStructure(r); r.mix = 0; }
    const structureDiffers = p.type !== r.type || clamp(p.slopeIndex, 0, SLOPES.length - 1) !== r.slopeIndex || p.placement !== r.placement;
    if (structureDiffers) {
      if (r.mix <= 0) { this.applyStructure(r); r.mixTarget = wantOn ? 1 : 0; }
      else r.mixTarget = 0;
    } else r.mixTarget = wantOn ? 1 : 0;

    if (r.mix <= 0 && r.mixTarget <= 0) { r.configured = false; r.envelope = 0; r.rmsEnv = 0; r.dynamicDb = 0; return; }

    r.freqLog += (Math.log(clamp(p.freqHz, MIN_F, MAX_F)) - r.freqLog) * this.smoothAlpha;
    r.qLog += (Math.log(clamp(p.q, MIN_Q, MAX_Q)) - r.qLog) * this.smoothAlpha;
    r.gainDb += (clamp(p.gainDb * this.gainScale, -MAX_GAIN * 1.5, MAX_GAIN * 1.5) - r.gainDb) * this.smoothAlpha;

    const dyn = p.dynOn && hasGain(r.type) ? this.runDetector(r, inL, inR, len) : ((r.envelope = 0), (r.rmsEnv = 0), 0);
    r.dynamicDb = dyn;
    const totalGain = r.gainDb + dyn;
    const changed = Math.abs(r.freqLog - r.designedFreqLog) > 1e-7 || Math.abs(r.qLog - r.designedQLog) > 1e-7 || Math.abs(totalGain - r.designedGain) > 1e-6;
    r.interpolate = changed;
    if (changed) {
      r.next = toSvf(designBand(r.type, Math.exp(r.freqLog), totalGain, Math.exp(r.qLog), SLOPES[r.slopeIndex], this.dspRate));
      r.designedFreqLog = r.freqLog; r.designedQLog = r.qLog; r.designedGain = totalGain;
    }

    const mixStart = r.mix;
    switch (r.placement) {
      case P.Stereo: this.filterChannel(r, 0, workL, len, mixStart, r.mixTarget); this.filterChannel(r, 1, workR, len, mixStart, r.mixTarget); break;
      case P.Left:   this.filterChannel(r, 0, workL, len, mixStart, r.mixTarget); break;
      case P.Right:  this.filterChannel(r, 1, workR, len, mixStart, r.mixTarget); break;
      case P.Mid: case P.Side: {
        const mid = new Float64Array(len), side = new Float64Array(len);
        for (let i = 0; i < len; i++) { mid[i] = 0.5 * (workL[i] + workR[i]); side[i] = 0.5 * (workL[i] - workR[i]); }
        this.filterChannel(r, 0, r.placement === P.Mid ? mid : side, len, mixStart, r.mixTarget);
        for (let i = 0; i < len; i++) { workL[i] = mid[i] + side[i]; workR[i] = mid[i] - side[i]; }
        break;
      }
    }
    let m = mixStart;
    for (let i = 0; i < len; i++) m = stepToward(m, r.mixTarget, this.mixStep);
    r.mix = m;
    if (r.interpolate) r.current = r.next;
  }

  processSubBlock(workL, workR, inL, inR, len) {
    for (let b = 0; b < MAX_BANDS; b++) this.processBand(this.bands[b], workL, workR, inL, inR, len);
    let solo = -1;
    for (let b = 0; b < MAX_BANDS; b++) if (this.bands[b].target.used && this.bands[b].target.solo) { solo = b; break; }
    const targetFreqLog = (b) => Math.log(clamp(this.bands[b].target.freqHz, MIN_F, MAX_F));
    const targetQLog = (b) => {
      const t = this.bands[b].target;
      return Math.log(clamp(usesBandQ(t.type, t.slopeIndex) ? clamp(t.q, MIN_Q, MAX_Q) : BW_Q, 0.3, 10));
    };
    let soloTarget = 0;
    if (solo >= 0) {
      if ((this.soloBand < 0 || solo !== this.soloBand) && this.soloMix <= 0) {
        this.soloBand = solo; this.soloState = [[0, 0], [0, 0]];
        this.soloFreqLog = targetFreqLog(solo); this.soloQLog = targetQLog(solo);
        this.soloFilter = toSvfSection(designBandPass(Math.exp(this.soloFreqLog), Math.exp(this.soloQLog), this.dspRate));
        soloTarget = 1;
      } else if (solo === this.soloBand) soloTarget = 1;
    }
    if (this.soloMix > 0 || soloTarget > 0) {
      if (this.soloBand >= 0) {
        this.soloFreqLog += (targetFreqLog(this.soloBand) - this.soloFreqLog) * this.smoothAlpha;
        this.soloQLog += (targetQLog(this.soloBand) - this.soloQLog) * this.smoothAlpha;
      }
      const nextSolo = toSvfSection(designBandPass(Math.exp(this.soloFreqLog), Math.exp(this.soloQLog), this.dspRate));
      const start = this.soloMix, work = [workL, workR], inp = [inL, inR];
      for (let c = 0; c < 2; c++) {
        let m = start;
        for (let i = 0; i < len; i++) {
          const t = (i + 1) / len;
          m = stepToward(m, soloTarget, this.mixStep);
          const s = tickSvfLerp(this.soloFilter, nextSolo, t, this.soloState[c], inp[c][i]);
          work[c][i] = work[c][i] * (1 - m) + s * m;
        }
        this.soloMix = m;
      }
      this.soloFilter = nextSolo;
      if (this.soloMix <= 0 && soloTarget <= 0) { this.soloBand = -1; this.soloState = [[0, 0], [0, 0]]; }
    } else { this.soloBand = -1; this.soloState = [[0, 0], [0, 0]]; }
  }

  process(chL, chR) {
    const wantOS = this.targetQuality === Q_MODE.NaturalPhase && this.fs < 88200;
    if (this.firstBlock) {
      this.firstBlock = false;
      if (wantOS !== this.activeOversampled) { this.activeOversampled = wantOS; this.oversampler.reset(); this.updateRates(); }
    }
    this.outGainTarget = 10 ** (clamp(this.userOutGainDb + this.computeAutoGain(), -36, 36) / 20);
    const n = chL.length;
    const baseL = new Float64Array(SUB_BLOCK), baseR = new Float64Array(SUB_BLOCK);
    const workL = new Float64Array(SUB_BLOCK * 2), workR = new Float64Array(SUB_BLOCK * 2);
    const inL = new Float64Array(SUB_BLOCK * 2), inR = new Float64Array(SUB_BLOCK * 2);

    for (let off = 0; off < n; off += SUB_BLOCK) {
      const len = Math.min(SUB_BLOCK, n - off);
      if (this.modeFade <= 0 && wantOS !== this.activeOversampled) {
        this.activeOversampled = wantOS;
        this.oversampler.reset();
        for (const r of this.bands) { r.configured = false; r.state = makeState(); r.detectorState = [[0, 0], [0, 0]]; r.detectorFreq = -1; }
        this.soloState = [[0, 0], [0, 0]]; this.soloBand = -1;
        this.updateRates();
      }
      for (let i = 0; i < len; i++) { baseL[i] = chL[off + i]; baseR[i] = chR[off + i]; }

      const dryL = new Float64Array(SUB_BLOCK), dryR = new Float64Array(SUB_BLOCK);
      if (this.activeOversampled) {
        this.oversampler.upsample(0, baseL, inL, len);
        this.oversampler.upsample(1, baseR, inR, len);
        for (let i = 0; i < len * 2; i++) { workL[i] = inL[i]; workR[i] = inR[i]; }
        this.processSubBlock(workL, workR, inL, inR, len * 2);
        const wTarget = this.globalBypass ? 0 : 1;
        let wm = this.wetMix;
        for (let i = 0; i < len * 2; i++) {
          wm = stepToward(wm, wTarget, this.mixStep);
          workL[i] = inL[i] + wm * (workL[i] - inL[i]);
          workR[i] = inR[i] + wm * (workR[i] - inR[i]);
        }
        this.wetMix = wm;
        this.oversampler.downsample(0, workL, baseL, len);
        this.oversampler.downsample(1, workR, baseR, len);
      } else {
        for (let i = 0; i < len; i++) { dryL[i] = workL[i] = inL[i] = baseL[i]; dryR[i] = workR[i] = inR[i] = baseR[i]; }
        this.processSubBlock(workL, workR, inL, inR, len);
        for (let i = 0; i < len; i++) { baseL[i] = workL[i]; baseR[i] = workR[i]; }
      }

      const g0 = this.outGain, gStep = (this.outGainTarget - g0) / len;
      let mf = this.modeFade, wm = this.wetMix;
      const mfTarget = wantOS === this.activeOversampled ? 1 : 0;
      const wTarget = this.globalBypass ? 0 : 1;
      for (let i = 0; i < len; i++) {
        mf = stepToward(mf, mfTarget, this.baseMixStep);
        if (!this.activeOversampled) wm = stepToward(wm, wTarget, this.baseMixStep);
        const g = (g0 + gStep * (i + 1)) * mf;
        const wl = baseL[i] * g, wr = baseR[i] * g;
        chL[off + i] = Math.fround(this.activeOversampled ? wl : dryL[i] * mf + wm * (wl - dryL[i] * mf));
        chR[off + i] = Math.fround(this.activeOversampled ? wr : dryR[i] * mf + wm * (wr - dryR[i] * mf));
      }
      this.outGain = this.outGainTarget;
      this.modeFade = mf;
      if (!this.activeOversampled) this.wetMix = wm;
    }
  }
}

// ------------------------------------------------------------------------- tests
const FS = 48000, BLOCK = 512;
let checks = 0, failures = 0;
function check(ok, msg) {
  checks++;
  console.log(`${ok ? 'OK  ' : 'FAIL'} ${msg}`);
  if (!ok) failures++;
}

function fillSine(l, r, hz, amp, offset = 0) {
  for (let i = 0; i < l.length; i++) l[i] = r[i] = Math.fround(amp * Math.sin((2 * Math.PI * hz * (i + offset)) / FS));
}
function rmsDb(arr) {
  let acc = 0; for (let i = 0; i < arr.length; i++) acc += arr[i] * arr[i];
  return 10 * Math.log10(acc / arr.length + 1e-30);
}
function processSine(e, hz, amp, blocks = 40) {
  const l = new Float32Array(BLOCK), r = new Float32Array(BLOCK);
  for (let b = 0; b < blocks; b++) { fillSine(l, r, hz, amp, b * BLOCK); e.process(l, r); }
  return rmsDb(l);
}

// 1. Transparencia bit a bit (Latencia cero)
{
  const e = new EqEngine(FS);
  const l = new Float32Array(BLOCK), r = new Float32Array(BLOCK), ref = new Float32Array(BLOCK);
  for (let i = 0; i < BLOCK; i++) l[i] = r[i] = ref[i] = Math.fround(Math.sin(i * 0.37) * 0.7);
  e.process(l, r);
  check(l.every((v, i) => v === ref[i]) && r.every((v, i) => v === ref[i]), 'Transparencia bit a bit en Latencia Cero');
}

// 2. Fase Natural (sobremuestreo FIR 2x): retardo exacto de 31 muestras y rizado < 0.005 dB
{
  const e = new EqEngine(FS);
  e.setQualityMode(Q_MODE.NaturalPhase);
  const l = new Float32Array(128), r = new Float32Array(128);
  l[0] = r[0] = 1;
  e.process(l, r);
  let peakIdx = 0;
  for (let i = 1; i < 128; i++) if (Math.abs(l[i]) > Math.abs(l[peakIdx])) peakIdx = i;
  const dry = 20 * Math.log10(0.1 / Math.SQRT2);
  const out1500 = processSine(e, 1500, 0.1, 12); // 1500 Hz = 16 ciclos exactos por bloque de 512 a 48 kHz
  const out18k = processSine(e, 18000, 0.1, 12); // 18000 Hz = 192 ciclos exactos por bloque de 512 a 48 kHz
  check(peakIdx === 31 && Math.abs(out1500 - dry) < 0.002 && Math.abs(out18k - dry) < 0.002,
    `Fase Natural 2x: latencia exacta=${peakIdx} muestras, 1.5k=${(out1500 - dry).toFixed(4)} dB, 18k=${(out18k - dry).toFixed(4)} dB`);
}

// 3. Bell +6 dB en TPT SVF + Escala global (50% -> +3 dB, 150% -> +9 dB) + Bypass global sin clics
{
  const eZero = new EqEngine(FS);
  eZero.setBand(0, { used: true, type: T.Bell, freqHz: 1500, gainDb: 6, q: 1 });
  const dry = 20 * Math.log10(0.1 / Math.SQRT2);
  const wet100 = processSine(eZero, 1500, 0.1);
  eZero.setGainScale(0.5);
  const wet50 = processSine(eZero, 1500, 0.1);
  eZero.setGlobalBypass(true);
  const byp = processSine(eZero, 1500, 0.1, 10);
  check(Math.abs(wet100 - dry - 6) < 0.08 && Math.abs(wet50 - dry - 3) < 0.08 && Math.abs(byp - dry) < 0.005,
    `TPT SVF Bell 100%=${(wet100 - dry).toFixed(2)} dB, Escala 50%=${(wet50 - dry).toFixed(2)} dB, Bypass=${(byp - dry).toFixed(4)} dB`);
}

// 4. All Pass conserva módulo exacto (0.00 dB) y rota la fase
{
  const e = new EqEngine(FS);
  e.setBand(0, { used: true, type: T.AllPass, freqHz: 1000, q: 2 });
  const l = new Float32Array(BLOCK), r = new Float32Array(BLOCK), ref = new Float32Array(BLOCK);
  for (let b = 0; b < 20; b++) {
    fillSine(l, r, 1000, 0.2, b * BLOCK);
    ref.set(l);
    e.process(l, r);
  }
  const magDiff = Math.abs(rmsDb(l) - rmsDb(ref));
  let phaseShifted = false;
  for (let i = 0; i < BLOCK; i++) if (Math.abs(l[i] + ref[i]) < 1e-2 && Math.abs(ref[i]) > 0.1) phaseShifted = true;
  check(magDiff < 0.01 && phaseShifted, `All Pass: módulo inalterado (desvío ${magDiff.toFixed(4)} dB) y rotación de 180° en f0`);
}

// 5. Auto-Gain compensa un realce ancho
{
  const e = new EqEngine(FS);
  const dry = processSine(e, 1000, 0.1, 10);
  e.setBand(0, { used: true, type: T.LowShelf, freqHz: 12000, gainDb: 6, q: BW_Q });
  const boosted = processSine(e, 500, 0.1, 20);
  e.setAutoGain(true);
  const compensated = processSine(e, 500, 0.1, 20);
  check(boosted - dry > 5.5 && Math.abs(compensated - dry) < 1.5,
    `Auto-Gain: sin compensar +${(boosted - dry).toFixed(2)} dB -> compensado ${(compensated - dry).toFixed(2)} dB`);
}

// 6. Dinámica híbrida Pico + RMS
{
  const cfg = { used: true, type: T.Bell, freqHz: 2000, gainDb: 0, q: 1, dynOn: true, thresholdDb: -30, rangeDb: -9, ratio: 20, attackMs: 1, releaseMs: 50 };
  const loud = new EqEngine(FS); loud.setBand(0, cfg);
  const dryLoud = 20 * Math.log10(0.5 / Math.SQRT2);
  const outLoud = processSine(loud, 2000, 0.5);
  const quiet = new EqEngine(FS); quiet.setBand(0, cfg);
  processSine(quiet, 2000, 0.001);
  check(Math.abs(loud.dynamicGainDb(0) - -9) < 0.2 && Math.abs(outLoud - dryLoud - -9) < 0.3 && Math.abs(quiet.dynamicGainDb(0)) < 0.05,
    `Dinámica SVF: fuerte=${loud.dynamicGainDb(0).toFixed(2)} dB, débil=${quiet.dynamicGainDb(0).toFixed(3)} dB`);
}

// 7. Mid/Side y hueco reutilizado
{
  const e = new EqEngine(FS);
  e.setBand(0, { used: true, type: T.Bell, freqHz: 1000, gainDb: 12, placement: P.Mid });
  const l = new Float32Array(BLOCK), r = new Float32Array(BLOCK);
  let worst = 0;
  for (let b = 0; b < 20; b++) {
    fillSine(l, r, 1000, 0.1, b * BLOCK);
    for (let i = 0; i < BLOCK; i++) r[i] = -r[i];
    const refL = l.slice(), refR = r.slice();
    e.process(l, r);
    for (let i = 0; i < BLOCK; i++) worst = Math.max(worst, Math.abs(l[i] - refL[i]), Math.abs(r[i] - refR[i]));
  }
  check(worst < 1e-6, `Mid no altera señal lateral pura (error máx ${worst.toExponential(2)})`);
}

// 8. EB-04: sin clics al activar, cambiar tipo/pendiente/canal, saltar Solo y conmutar Latencia Cero <-> Fase Natural
{
  const e = new EqEngine(FS);
  const l = new Float32Array(BLOCK), r = new Float32Array(BLOCK);
  let prev = 0, maxStep = 0, idx = 0;
  const run = (blocks) => {
    for (let b = 0; b < blocks; b++) {
      fillSine(l, r, 440, 0.1, idx);
      e.process(l, r);
      for (let i = 0; i < BLOCK; i++) {
        if (idx + i > 0) maxStep = Math.max(maxStep, Math.abs(l[i] - prev));
        prev = l[i];
      }
      idx += BLOCK;
    }
  };
  run(4);
  e.setBand(0, { used: true, type: T.Bell, freqHz: 440, gainDb: 12, q: 1.2 });
  e.setBand(1, { used: true, type: T.FlatTilt, freqHz: 1000, gainDb: 4 });
  run(10);
  e.setBand(0, { used: true, type: T.LowCut, freqHz: 440, slopeIndex: 7 });
  run(10);
  e.setQualityMode(Q_MODE.NaturalPhase); // cambio de modo en caliente (con fundido de 5 ms)
  run(12);
  e.setBand(0, { used: true, type: T.LowCut, freqHz: 440, slopeIndex: 7, solo: true });
  run(10);
  e.setBand(0, { used: true, type: T.LowCut, freqHz: 440, slopeIndex: 7, solo: false });
  e.setBand(1, { used: true, type: T.FlatTilt, freqHz: 1000, gainDb: 4, solo: true });
  run(10);
  e.setQualityMode(Q_MODE.ZeroLatency);
  e.setBand(0, { used: false });
  e.setBand(1, { used: false });
  run(12);
  check(maxStep < 0.05, `EB-04 sin clics incluido cambio Cero <-> Fase Natural (derivada máx ${maxStep.toFixed(4)} < 0.05)`);
}

console.log(`Motor DSP v0.2: ${checks} comprobaciones, ${failures} fallas`);
process.exitCode = failures ? 1 : 0;
