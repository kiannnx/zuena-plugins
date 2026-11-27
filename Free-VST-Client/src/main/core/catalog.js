'use strict';
// Validación y carga del catálogo de plugins. Sin dependencias de Electron.

const fs = require('node:fs/promises');

const ID_RE = /^[a-z0-9][a-z0-9-]{1,63}$/;
const VERSION_RE = /^\d+\.\d+\.\d+$/;
const SHA_RE = /^[a-f0-9]{64}$/i;
const COLOR_RE = /^#[0-9a-f]{6}$/i;
const BUNDLED_RE = /^bundled:\/\/([a-z0-9][a-z0-9._-]{0,80}\.(?:zip|mp4|webm|jpg|png))$/i;
const PLATFORMS = ['win32', 'darwin'];
const ICONS = ['wave', 'mic', 'knob', 'spark', 'echo', 'filter'];
// Íconos de los destacados de la ficha (los dibuja el renderer).
const FEATURE_ICONS = ['dynamic', 'phase', 'stereo', 'spectrum', 'gain', 'compare', 'bands', 'voice', 'tempo', 'drag'];
const IMAGE_RE = /\.(jpg|png)$/i;
const VIDEO_RE = /\.(mp4|webm)$/i;

function isLoopbackUrl(u) {
  return u.protocol === 'http:' && ['127.0.0.1', 'localhost', '[::1]'].includes(u.hostname);
}

/** URL de paquete o demo incluido junto al catálogo integrado (sin rutas ni '..'). */
function isBundledUrl(str) {
  return typeof str === 'string' && BUNDLED_RE.test(str);
}

/** Acepta https:// y http:// solo en loopback (modo demo / paquetes locales / tests). */
function isAllowedUrl(str) {
  try {
    const u = new URL(str);
    return u.protocol === 'https:' || isLoopbackUrl(u);
  } catch {
    return false;
  }
}

/** Convierte las URLs `bundled://<archivo>` a la URL local servida por el proceso principal. */
function resolveBundledUrls(catalog, baseUrl) {
  const mapUrl = (u) => {
    const m = typeof u === 'string' && BUNDLED_RE.exec(u);
    return m ? `${baseUrl}/${m[1]}` : u;
  };
  return {
    ...catalog,
    plugins: catalog.plugins.map((p) => ({
      ...p,
      downloads: Object.fromEntries(Object.entries(p.downloads).map(([os, d]) => [os, { ...d, url: mapUrl(d.url) }])),
      gallery: p.gallery.map((g) => ({ ...g, src: mapUrl(g.src) })),
      media: Object.fromEntries(Object.entries(p.media ?? {}).map(([k, u]) => [k, mapUrl(u)])),
    })),
  };
}

function compareVersions(a, b) {
  const pa = String(a).split('.').map(Number);
  const pb = String(b).split('.').map(Number);
  for (let i = 0; i < 3; i++) {
    const d = (pa[i] || 0) - (pb[i] || 0);
    if (d !== 0) return d > 0 ? 1 : -1;
  }
  return 0;
}

function str(v, max, field, errors, required = true) {
  if (v === undefined || v === null || v === '') {
    if (required) errors.push(`${field}: obligatorio`);
    return '';
  }
  if (typeof v !== 'string') {
    errors.push(`${field}: debe ser texto`);
    return '';
  }
  return v.slice(0, max);
}

/**
 * Valida y normaliza un catálogo. Las entradas inválidas se descartan (con error
 * registrado) en vez de tumbar todo el catálogo.
 * @returns {{catalog: object, errors: string[]}}
 */
function validateCatalog(raw, { allowBundled = true } = {}) {
  const checkUrl = (u) => isAllowedUrl(u) || (allowBundled && isBundledUrl(u));
  const errors = [];
  if (!raw || typeof raw !== 'object' || raw.schema !== 1 || !Array.isArray(raw.plugins)) {
    throw new Error('Catálogo inválido: se esperaba { "schema": 1, "plugins": [...] }');
  }
  const seen = new Set();
  const plugins = [];
  raw.plugins.forEach((p, i) => {
    const errs = [];
    const where = `plugins[${i}]`;
    if (!p || typeof p !== 'object') {
      errors.push(`${where}: no es un objeto`);
      return;
    }
    if (typeof p.id !== 'string' || !ID_RE.test(p.id)) errs.push('id inválido');
    else if (seen.has(p.id)) errs.push(`id duplicado "${p.id}"`);
    const name = str(p.name, 60, 'name', errs);
    const tagline = str(p.tagline, 120, 'tagline', errs, false);
    const description = str(p.description, 1200, 'description', errs, false);
    const category = str(p.category, 40, 'category', errs, false) || 'Efecto';
    if (typeof p.version !== 'string' || !VERSION_RE.test(p.version)) errs.push('version debe ser X.Y.Z');
    const status = p.status === 'coming-soon' ? 'coming-soon' : 'available';
    const accent = Array.isArray(p.accent) && p.accent.length === 2 && p.accent.every((c) => COLOR_RE.test(c))
      ? p.accent : ['#4f7cff', '#8a5cff'];
    const icon = ICONS.includes(p.icon) ? p.icon : 'wave';
    const tags = Array.isArray(p.tags) ? p.tags.filter((t) => typeof t === 'string').slice(0, 6).map((t) => t.slice(0, 24)) : [];
    const bundle = typeof p.bundle === 'string' && /^[^\\/:*?"<>|]+\.vst3$/i.test(p.bundle) ? p.bundle : null;

    const downloads = {};
    if (p.downloads && typeof p.downloads === 'object') {
      for (const os of PLATFORMS) {
        const d = p.downloads[os];
        if (!d) continue;
        if (!checkUrl(d.url)) { errs.push(`downloads.${os}.url debe ser https`); continue; }
        if (typeof d.sha256 !== 'string' || !SHA_RE.test(d.sha256)) { errs.push(`downloads.${os}.sha256 inválido`); continue; }
        if (!Number.isInteger(d.size) || d.size <= 0) { errs.push(`downloads.${os}.size inválido`); continue; }
        downloads[os] = { url: d.url, sha256: d.sha256.toLowerCase(), size: d.size };
      }
    }
    if (status === 'available' && Object.keys(downloads).length === 0) errs.push('available sin downloads válidos');


    // Vídeo de presentación y póster de la ficha (opcionales; el vídeo solo con póster).
    const media = {};
    if (p.media && typeof p.media === 'object') {
      if (checkUrl(p.media.poster) && IMAGE_RE.test(p.media.poster)) media.poster = p.media.poster;
      if (media.poster && checkUrl(p.media.video) && VIDEO_RE.test(p.media.video)) media.video = p.media.video;
      // Variante para el modo oscuro: solo si existe la clara y trae vídeo + póster.
      if (media.video && checkUrl(p.media.posterDark) && IMAGE_RE.test(p.media.posterDark)
        && checkUrl(p.media.videoDark) && VIDEO_RE.test(p.media.videoDark)) {
        media.posterDark = p.media.posterDark;
        media.videoDark = p.media.videoDark;
      }
    }

    // Contenido de la ficha. Todo es texto plano (el renderer escapa); lo inválido se descarta sin tumbar el plugin.
    const text = (v, max) => (typeof v === 'string' ? v.trim().slice(0, max) : '');
    const headline = text(p.headline, 140);
    const summary = text(p.summary, 400);
    const highlights = (Array.isArray(p.highlights) ? p.highlights : [])
      .filter((h) => h && text(h.title, 60) && text(h.text, 240))
      .slice(0, 6)
      .map((h) => ({ icon: FEATURE_ICONS.includes(h.icon) ? h.icon : 'bands', title: text(h.title, 60), text: text(h.text, 240) }));
    const gallery = (Array.isArray(p.gallery) ? p.gallery : [])
      .filter((g) => g && checkUrl(g.src) && IMAGE_RE.test(g.src))
      .slice(0, 8)
      .map((g) => ({ src: g.src, caption: text(g.caption, 90) }));
    const specs = (Array.isArray(p.specs) ? p.specs : [])
      .filter((s) => Array.isArray(s) && s.length === 2 && text(s[0], 40) && text(s[1], 80))
      .slice(0, 14)
      .map(([k, v]) => [text(k, 40), text(v, 80)]);
    const changelog = (Array.isArray(p.changelog) ? p.changelog : [])
      .filter((c) => c && VERSION_RE.test(c.version) && Array.isArray(c.items))
      .slice(0, 5)
      .map((c) => ({ version: c.version, date: text(c.date, 20), items: c.items.map((i) => text(i, 200)).filter(Boolean).slice(0, 8) }))
      .filter((c) => c.items.length);

    if (errs.length) {
      errors.push(`${where} (${p.id ?? '?'}): ${errs.join('; ')}`);
      return;
    }
    seen.add(p.id);
    plugins.push({
      id: p.id, name, tagline, description, category, version: p.version, status, accent, icon, tags, bundle, downloads, media,
      headline, summary, highlights, gallery, specs, changelog,
    });
  });
  const featured = plugins.some((p) => p.id === raw.featured) ? raw.featured : plugins[0]?.id ?? null;
  return { catalog: { schema: 1, updated: typeof raw.updated === 'string' ? raw.updated : null, featured, plugins }, errors };
}

async function fetchJson(url, timeoutMs) {
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), timeoutMs);
  try {
    const res = await fetch(url, { signal: ctrl.signal, headers: { 'Cache-Control': 'no-cache' } });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return await res.json();
  } catch (e) {
    throw new Error(e.name === 'AbortError' ? 'tiempo de espera agotado' : e.message);
  } finally {
    clearTimeout(t);
  }
}

/**
 * Orden de carga: remoto (si hay URL) → caché local → catálogo integrado.
 * @returns {Promise<{catalog: object, source: 'remote'|'cache'|'bundled', warning: string|null, errors: string[]}>}
 */
async function loadCatalog({ url, cachePath, bundledPath, timeoutMs = 8000 }) {
  let warning = null;
  if (url) {
    if (!isAllowedUrl(url)) {
      warning = 'La URL del catálogo debe empezar por https://';
    } else {
      try {
        const raw = await fetchJson(url, timeoutMs);
        const { catalog, errors } = validateCatalog(raw, { allowBundled: false });
        await fs.writeFile(cachePath, JSON.stringify(raw), 'utf8').catch(() => {});
        return { catalog, source: 'remote', warning: null, errors };
      } catch (e) {
        warning = `No se pudo descargar el catálogo (${e.message}).`;
      }
    }
    try {
      const { catalog, errors } = validateCatalog(JSON.parse(await fs.readFile(cachePath, 'utf8')), { allowBundled: false });
      return { catalog, source: 'cache', warning: `${warning} Mostrando la última copia guardada.`, errors };
    } catch {
      /* sin caché: sigue al integrado */
    }
  }
  const { catalog, errors } = validateCatalog(JSON.parse(await fs.readFile(bundledPath, 'utf8')), { allowBundled: true });
  return { catalog, source: 'bundled', warning, errors };
}

module.exports = { validateCatalog, loadCatalog, compareVersions, isAllowedUrl, isLoopbackUrl, isBundledUrl, resolveBundledUrls };
