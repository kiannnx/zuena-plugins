'use strict';
// Manifiesto de versión que viaja DENTRO de cada bundle .vst3 (Contents/Resources/zuena-plugin.json).
// Lo escribe build/pack-eq.js al empaquetar y lo lee el launcher para saber qué versión hay instalada
// en disco, aunque el plugin se haya copiado a mano o actualizado fuera del launcher.

const fsp = require('node:fs/promises');
const path = require('node:path');

const MANIFEST_REL = path.join('Contents', 'Resources', 'zuena-plugin.json');
const VERSION_RE = /^\d+\.\d+\.\d+$/;

function manifestEntryName(bundleName) {
  return `${bundleName}/Contents/Resources/zuena-plugin.json`;
}

function buildManifest({ id, version }) {
  if (!VERSION_RE.test(version)) throw new Error(`Versión inválida: ${version}`);
  return Buffer.from(`${JSON.stringify({ schema: 1, id, version }, null, 2)}\n`, 'utf8');
}

/** Versión X.Y.Z declarada por el bundle instalado, o null si no tiene manifiesto válido. */
async function readBundleVersion(bundlePath, expectedId) {
  try {
    const raw = JSON.parse(await fsp.readFile(path.join(bundlePath, MANIFEST_REL), 'utf8'));
    if (!raw || raw.schema !== 1 || typeof raw.version !== 'string' || !VERSION_RE.test(raw.version)) return null;
    if (expectedId && raw.id !== expectedId) return null;
    return raw.version;
  } catch {
    return null;
  }
}

module.exports = { MANIFEST_REL, manifestEntryName, buildManifest, readBundleVersion };
