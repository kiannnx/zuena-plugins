// Iconos SVG monocromos (currentColor). Marca ZUENA: sin degradados ni brillos de color en iconos ni logo.

const line = (d, extra = '') => `<svg viewBox="0 0 48 48" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="3.2" stroke-linecap="round" stroke-linejoin="round" ${extra}>${d}</svg>`;

export const sectionIcon = {
  featured: () => line('<path d="M24 7l5 10.4 11.4 1.6-8.2 8 2 11.3L24 33l-10.2 5.3 2-11.3-8.2-8L19 17.4z"/>'),
  catalog: () => line('<rect x="7" y="7" width="14" height="14" rx="4"/><rect x="27" y="7" width="14" height="14" rx="4"/><rect x="7" y="27" width="14" height="14" rx="4"/><rect x="27" y="27" width="14" height="14" rx="4"/>'),
  installed: () => line('<circle cx="24" cy="24" r="17"/><path d="M16.5 24.5l5 5 10-11"/>'),
  downloads: () => line('<path d="M24 7v21m0 0l-8-8m8 8l8-8"/><path d="M9 31v4a5 5 0 005 5h20a5 5 0 005-5v-4"/>'),
  settings: () => line('<path d="M8 14h20M36 14h4M8 34h4M20 34h20"/><circle cx="32" cy="14" r="4"/><circle cx="16" cy="34" r="4"/>'),
};

export const pluginIcon = {
  wave: () => line('<path d="M5 24h5l4-10 6 22 6-28 6 24 4-8h7"/>'),
  mic: () => line('<rect x="17" y="5" width="14" height="24" rx="7"/><path d="M11 23a13 13 0 0026 0M24 36v7M17 43h14"/>'),
  knob: () => line('<circle cx="24" cy="24" r="15"/><path d="M24 24l7-8"/><path d="M8 38l3-3M40 38l-3-3" stroke-opacity=".6"/>'),
  spark: () => line('<path d="M24 4v10M24 34v10M4 24h10M34 24h10M10 10l7 7M31 31l7 7M38 10l-7 7M17 31l-7 7"/>'),
  echo: () => line('<circle cx="14" cy="24" r="5"/><path d="M24 14a14 14 0 010 20M31 9a21 21 0 010 30M38 5a27 27 0 010 38" stroke-opacity=".75"/>'),
  filter: () => line('<path d="M5 14h18c6 0 8 20 20 20"/><path d="M5 34h10" stroke-opacity=".6"/>'),
};

// Destacados de la ficha (campo highlights[].icon del catálogo).
export const featureIcon = {
  dynamic: () => line('<path d="M5 30c5 0 6-12 11-12s6 18 11 18 6-14 11-14 5 8 5 8"/><path d="M38 6v10M34 10l4-4 4 4" stroke-opacity=".7"/>'),
  phase: () => line('<path d="M4 24c4-12 10-12 14 0s10 12 14 0 10-12 14 0"/><path d="M4 24c4 12 10 12 14 0s10-12 14 0 10 12 14 0" stroke-opacity=".45"/>'),
  stereo: () => line('<circle cx="18" cy="24" r="11"/><circle cx="30" cy="24" r="11" stroke-opacity=".7"/>'),
  spectrum: () => line('<path d="M8 40V30M15 40V20M22 40V12M29 40V18M36 40V26M42 40V32"/>'),
  gain: () => line('<path d="M9 33a16 16 0 1130 0"/><path d="M24 26l8-9"/><circle cx="24" cy="26" r="2.5" fill="currentColor"/>'),
  compare: () => line('<rect x="5" y="9" width="16" height="30" rx="5"/><rect x="27" y="9" width="16" height="30" rx="5" stroke-opacity=".7"/><path d="M10 31l3-12 3 12M11.5 26h3M32 19h4a3 3 0 010 6h-4zm0 6h5a3 3 0 010 6h-5z" stroke-width="2.4"/>'),
  bands: () => line('<path d="M4 30c6 0 8-14 14-14s8 20 14 20 8-10 12-10"/><circle cx="18" cy="16" r="3" fill="currentColor"/><circle cx="32" cy="36" r="3" fill="currentColor"/>'),
  voice: () => line('<rect x="17" y="5" width="14" height="24" rx="7"/><path d="M11 23a13 13 0 0026 0M24 36v7"/>'),
  tempo: () => line('<path d="M17 42h14L27 7h-6z"/><path d="M24 30l11-15"/>'),
  drag: () => line('<path d="M24 6v36M6 24h36M19 11l5-5 5 5M19 37l5 5 5-5M11 19l-5 5 5 5M37 19l5 5-5 5"/>'),
};

export const ui = {
  search: () => line('<circle cx="21" cy="21" r="12"/><path d="M30 30l11 11"/>'),
  close: () => line('<path d="M14 14l20 20M34 14L14 34"/>'),
  chevronL: () => line('<path d="M29 12L17 24l12 12"/>'),
  chevronR: () => line('<path d="M19 12l12 12-12 12"/>'),
  play: () => '<svg viewBox="0 0 48 48" aria-hidden="true"><path d="M17 11v26l21-13z" fill="currentColor"/></svg>',
  pause: () => '<svg viewBox="0 0 48 48" aria-hidden="true"><rect x="14" y="11" width="7" height="26" rx="2" fill="currentColor"/><rect x="27" y="11" width="7" height="26" rx="2" fill="currentColor"/></svg>',
  folder: () => line('<path d="M5 14a4 4 0 014-4h9l4 5h17a4 4 0 014 4v17a4 4 0 01-4 4H9a4 4 0 01-4-4z"/>'),
  trash: () => line('<path d="M8 13h32M19 13V8h10v5M12 13l2 27h20l2-27"/>'),
  check: () => line('<path d="M10 25l9 9 19-20"/>'),
  info: () => line('<circle cx="24" cy="24" r="18"/><path d="M24 22v12M24 15v1"/>'),
  warn: () => line('<path d="M24 6L4 41h40z"/><path d="M24 19v10M24 35v1"/>'),
  bell: () => line('<path d="M12 33V21a12 12 0 0124 0v12l4 4H8z"/><path d="M20 41a4 4 0 008 0"/>'),
  wifi: () => line('<path d="M6 19a26 26 0 0136 0M12 26a17 17 0 0124 0M18 33a8 8 0 0112 0"/><circle cx="24" cy="39" r="1.5" fill="currentColor"/>'),
  wifiOff: () => line('<path d="M6 19a26 26 0 0110-6M30 13a26 26 0 0112 6M18 33a8 8 0 0112 0M7 7l34 34"/>'),
  min: () => line('<path d="M14 24h20"/>', 'stroke-width="2.4"'),
  max: () => line('<rect x="14" y="14" width="20" height="20" rx="2"/>', 'stroke-width="2.4"'),
  restore: () => line('<rect x="13" y="18" width="17" height="17" rx="2"/><path d="M19 13h14a2 2 0 012 2v14"/>', 'stroke-width="2.4"'),
  x: () => line('<path d="M15 15l18 18M33 15L15 33"/>', 'stroke-width="2.4"'),
};
