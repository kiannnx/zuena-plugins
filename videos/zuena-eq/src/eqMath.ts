// Magnitud analógica de los tipos de banda que aparecen en el vídeo (prototipos clásicos).
// Solo para dibujar: el plugin usa diseño matched-z, que sigue estas mismas curvas.

type C = { re: number; im: number };
const c = (re: number, im = 0): C => ({ re, im });
const add = (a: C, b: C): C => c(a.re + b.re, a.im + b.im);
const mul = (a: C, b: C): C => c(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
const abs = (a: C) => Math.hypot(a.re, a.im);
const scale = (a: C, k: number): C => c(a.re * k, a.im * k);

/** Polinomio de 2.º orden a·s² + b·s + d evaluado en s. */
const poly = (s: C, a: number, b: number, d: number) => add(add(scale(mul(s, s), a), scale(s, b)), c(d));

export type Band =
  | { kind: "lowcut"; freq: number }
  | { kind: "bell"; freq: number; gainDb: number; q: number }
  | { kind: "highshelf"; freq: number; gainDb: number };

export function bandDb(b: Band, hz: number): number {
  const s = c(0, hz / b.freq);
  if (b.kind === "lowcut") {
    return 20 * Math.log10(abs(mul(s, s)) / abs(poly(s, 1, Math.SQRT2, 1)));
  }
  const A = 10 ** (b.gainDb / 40);
  if (b.kind === "bell") {
    return 20 * Math.log10(abs(poly(s, 1, A / b.q, 1)) / abs(poly(s, 1, 1 / (A * b.q), 1)));
  }
  const q = Math.SQRT1_2;
  return 20 * Math.log10((A * abs(poly(s, A, Math.sqrt(A) / q, 1))) / abs(poly(s, 1, Math.sqrt(A) / q, A)));
}

export const totalDb = (bands: Band[], hz: number) => bands.reduce((acc, b) => acc + bandDb(b, hz), 0);

/** Frecuencia → x en [0, width] con escala logarítmica 20 Hz – 20 kHz. */
export const freqToX = (hz: number, width: number) => (Math.log10(hz / 20) / 3) * width;
