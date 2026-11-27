'use strict';
// Distribución del launcher: dónde vive la versión instalada, instalación desde una copia portátil
// (el .zip descargado) y accesos directos con el identificador de notificaciones de Windows.

const { fsp } = require('./real-fs'); // sin intercepción de .asar
const path = require('node:path');

/** Repositorio público con SOLO binarios y catálogo (el código fuente es privado). */
const RELEASES_REPO = 'kiannnx/zuena-plugins-releases';
const OFFICIAL_CATALOG_URL = `https://raw.githubusercontent.com/${RELEASES_REPO}/main/catalog.json`;
const LAUNCHER_MANIFEST_URL = `https://raw.githubusercontent.com/${RELEASES_REPO}/main/launcher.json`;
const APP_USER_MODEL_ID = 'com.zuena.plugins';
const PRODUCT = 'ZUENA Plugins';

/** Carpeta de instalación estándar por usuario (no pide permisos de administrador). */
function installRoot(env = process.env) {
  return path.join(env.LOCALAPPDATA || '', 'Programs', PRODUCT);
}

/** Carpeta de la app que se está ejecutando (la que contiene el .exe). */
function runningRoot(execPath = process.execPath) {
  return path.dirname(execPath);
}

const same = (a, b) => path.resolve(a).toLowerCase() === path.resolve(b).toLowerCase();

/** true si la app corre desde su ubicación instalada; false si es una copia portátil (p. ej. Descargas). */
function isInstalledCopy(execPath = process.execPath, env = process.env) {
  return same(runningRoot(execPath), installRoot(env));
}

async function exists(p) {
  try { await fsp.access(p); return true; } catch { return false; }
}

/**
 * Copia la app portátil a la carpeta de instalación (reemplazo atómico por renombrado) y devuelve
 * la ruta del .exe instalado. No toca nada fuera de %LOCALAPPDATA%\Programs\ZUENA Plugins.
 */
async function installFromPortable({ execPath = process.execPath, env = process.env, onProgress = () => {} } = {}) {
  const src = runningRoot(execPath);
  const dest = installRoot(env);
  if (same(src, dest)) return path.join(dest, path.basename(execPath));
  const tmp = `${dest}.nuevo`;
  const old = `${dest}.anterior`;
  onProgress('Copiando archivos…');
  await fsp.rm(tmp, { recursive: true, force: true });
  await fsp.mkdir(path.dirname(dest), { recursive: true });
  await fsp.cp(src, tmp, { recursive: true });
  onProgress('Instalando…');
  await fsp.rm(old, { recursive: true, force: true });
  if (await exists(dest)) {
    try {
      await fsp.rename(dest, old);
    } catch (e) {
      await fsp.rm(tmp, { recursive: true, force: true });
      throw new Error(e.code === 'EBUSY' || e.code === 'EPERM'
        ? 'Hay otra copia de ZUENA Plugins abierta desde la carpeta de instalación. Cerrala y probá de nuevo.'
        : `No se pudo reemplazar la instalación anterior: ${e.message}`);
    }
  }
  try {
    await fsp.rename(tmp, dest);
  } catch (e) {
    if (await exists(old)) await fsp.rename(old, dest).catch(() => {});
    throw e;
  }
  await fsp.rm(old, { recursive: true, force: true }).catch(() => {});
  return path.join(dest, path.basename(execPath));
}

/** Accesos directos (escritorio y menú Inicio) con el identificador de notificaciones de la app. */
function createShortcuts({ shell, exePath, desktop = true, startMenu = true, env = process.env }) {
  if (process.platform !== 'win32') return [];
  const root = path.dirname(exePath);
  const icon = path.join(root, `${PRODUCT}.ico`);
  const link = { target: exePath, cwd: root, description: PRODUCT, appUserModelId: APP_USER_MODEL_ID, icon, iconIndex: 0 };
  const made = [];
  const targets = [];
  if (desktop) targets.push(path.join(env.USERPROFILE || '', 'Desktop', `${PRODUCT}.lnk`));
  if (startMenu) targets.push(path.join(env.APPDATA || '', 'Microsoft', 'Windows', 'Start Menu', 'Programs', `${PRODUCT}.lnk`));
  for (const t of targets) {
    if (shell.writeShortcutLink(t, 'create', link)) made.push(t);
  }
  return made;
}

/**
 * Windows solo muestra las notificaciones de una app sin instalador si hay un acceso directo en el
 * menú Inicio con su AppUserModelID. Lo crea o lo corrige si falta o apunta a otro .exe.
 * Devuelve 'ok' | 'created' | 'updated' | 'failed'.
 */
function ensureStartMenuShortcut({ shell, exePath, env = process.env }) {
  if (process.platform !== 'win32') return 'ok';
  const lnk = path.join(env.APPDATA || '', 'Microsoft', 'Windows', 'Start Menu', 'Programs', `${PRODUCT}.lnk`);
  let current = null;
  try { current = shell.readShortcutLink(lnk); } catch { /* no existe */ }
  if (current && current.appUserModelId === APP_USER_MODEL_ID
    && path.resolve(current.target || '').toLowerCase() === path.resolve(exePath).toLowerCase()) return 'ok';
  const made = createShortcuts({ shell, exePath, desktop: false, startMenu: true, env });
  return made.length ? (current ? 'updated' : 'created') : 'failed';
}

module.exports = {
  RELEASES_REPO, OFFICIAL_CATALOG_URL, LAUNCHER_MANIFEST_URL, APP_USER_MODEL_ID, PRODUCT,
  installRoot, runningRoot, isInstalledCopy, installFromPortable, createShortcuts, ensureStartMenuShortcut,
};
