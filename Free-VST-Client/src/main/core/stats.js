'use strict';
// Estadísticas anónimas del launcher (solo con permiso del usuario).
// Se envía: un identificador aleatorio de esta instalación, la versión del launcher, el sistema
// (plataforma y versión de Windows), el idioma y los plugins instalados con su versión.
// No se envía: nombre, correo, IP guardada, rutas, nombre del equipo ni nada de tus proyectos.
// El servidor (Supabase) solo acepta dos llamadas: registrar el ping y borrar esta instalación.

const crypto = require('node:crypto');

const STATS_URL = 'https://dyymeqjqyjfqkxtbxmvg.supabase.co/rest/v1/rpc';
// Clave publicable: pública por diseño; solo permite llamar a las dos funciones de abajo.
const STATS_KEY = 'sb_publishable_iN7wc3JXb2Hx76iFkQt53Q_kmDNCgXL';
const TIMEOUT_MS = 8000;
const VERSION_RE = /^\d+\.\d+\.\d+$/;
const ID_RE = /^[a-z0-9-]{1,40}$/;
const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;

const newInstallId = () => crypto.randomUUID();
const isInstallId = (v) => typeof v === 'string' && UUID_RE.test(v);

/** Cuerpo del ping. Solo campos acotados y validados; nada que identifique a la persona. */
function buildPing({ installId, appVersion, platform, osRelease, locale, installed }) {
  if (!isInstallId(installId)) throw new Error('Identificador de instalación inválido');
  const plugins = {};
  for (const [id, rec] of Object.entries(installed ?? {})) {
    if (ID_RE.test(id) && VERSION_RE.test(rec?.version ?? '')) plugins[id] = rec.version;
  }
  const major = String(osRelease ?? '').split('.').slice(0, 2).join('.');
  return {
    p_install_id: installId,
    p_launcher_version: VERSION_RE.test(appVersion) ? appVersion : '0.0.0',
    p_os: `${String(platform).slice(0, 12)}-${major}`.slice(0, 32),
    p_locale: locale === 'en' ? 'en' : 'es',
    p_plugins: plugins,
  };
}

async function call(fn, body, { base = STATS_URL, key = STATS_KEY, fetchImpl = fetch } = {}) {
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), TIMEOUT_MS);
  try {
    const res = await fetchImpl(`${base}/${fn}`, {
      method: 'POST',
      headers: { apikey: key, 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
      signal: ctrl.signal,
    });
    if (!res.ok) throw new Error(`Estadísticas: respuesta ${res.status}`);
    return true;
  } finally {
    clearTimeout(t);
  }
}

const sendPing = (ping, opts) => call('zuena_plugins_ping', ping, opts);
const forget = (installId, opts) => call('zuena_plugins_forget', { p_install_id: installId }, opts);

module.exports = { STATS_URL, newInstallId, isInstallId, buildPing, sendPing, forget };
