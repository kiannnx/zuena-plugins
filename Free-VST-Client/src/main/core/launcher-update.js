'use strict';
// Autoactualización del launcher: lee launcher.json del repositorio de publicación, compara versiones,
// descarga el .zip verificado con SHA-256, lo extrae junto a la instalación y reemplaza la carpeta
// cuando la app se cierra (un script de PowerShell espera a que termine el proceso).

const { fsp } = require('./real-fs'); // sin intercepción de .asar
const path = require('node:path');
const os = require('node:os');
const { spawn } = require('node:child_process');
const { compareVersions, isAllowedUrl } = require('./catalog');
const { download } = require('./downloader');
const { extractZip } = require('./unzip');

const VERSION_RE = /^\d+\.\d+\.\d+$/;
const SHA_RE = /^[a-f0-9]{64}$/i;

/** Valida el manifiesto publicado. Devuelve null si no es utilizable. */
function parseManifest(raw) {
  if (!raw || raw.schema !== 1 || !VERSION_RE.test(raw.version)) return null;
  const w = raw.downloads?.win32;
  if (!w || !isAllowedUrl(w.url) || !SHA_RE.test(w.sha256) || !Number.isInteger(w.size) || w.size <= 0) return null;
  const notes = Array.isArray(raw.notes) ? raw.notes.filter((n) => typeof n === 'string').slice(0, 8).map((n) => n.slice(0, 200)) : [];
  return { version: raw.version, published: typeof raw.published === 'string' ? raw.published.slice(0, 20) : null, notes, win32: { url: w.url, sha256: w.sha256.toLowerCase(), size: w.size } };
}

async function fetchManifest(url, timeoutMs = 8000) {
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), timeoutMs);
  try {
    const res = await fetch(url, { signal: ctrl.signal, headers: { 'Cache-Control': 'no-cache' } });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return parseManifest(await res.json());
  } finally {
    clearTimeout(t);
  }
}

/** Hay actualización si el manifiesto trae una versión mayor que la actual. */
function isNewer(manifest, current) {
  return !!manifest && compareVersions(manifest.version, current) > 0;
}

/** Busca la carpeta de la app (la que contiene `exeName`) dentro de lo extraído. */
async function findAppRoot(dir, exeName, depth = 0) {
  const items = await fsp.readdir(dir, { withFileTypes: true });
  if (items.some((d) => d.isFile() && d.name.toLowerCase() === exeName.toLowerCase())) return dir;
  if (depth >= 2) return null;
  for (const d of items) {
    if (!d.isDirectory()) continue;
    const r = await findAppRoot(path.join(dir, d.name), exeName, depth + 1);
    if (r) return r;
  }
  return null;
}

/**
 * Descarga y prepara la versión nueva junto a `appRoot` (`<appRoot>.nuevo`). Devuelve la ruta preparada.
 * No reemplaza nada mientras la app sigue abierta.
 */
async function prepareUpdate({ manifest, appRoot, exeName, workDir, signal, onProgress }) {
  const zip = path.join(workDir, `launcher-${manifest.version}.zip`);
  await fsp.mkdir(workDir, { recursive: true });
  await download({ url: manifest.win32.url, dest: zip, sha256: manifest.win32.sha256, size: manifest.win32.size, signal, onProgress });
  const unpack = path.join(workDir, `launcher-${manifest.version}`);
  await fsp.rm(unpack, { recursive: true, force: true });
  try {
    await extractZip(zip, unpack);
  } finally {
    await fsp.rm(zip, { force: true }).catch(() => {});
  }
  const found = await findAppRoot(unpack, exeName);
  if (!found) throw new Error('El paquete del launcher no contiene la aplicación');
  const staged = `${appRoot}.nuevo`;
  await fsp.rm(staged, { recursive: true, force: true });
  await fsp.cp(found, staged, { recursive: true });
  await fsp.rm(unpack, { recursive: true, force: true }).catch(() => {});
  return staged;
}

/**
 * Lanza un PowerShell oculto que espera a que termine `pid`, cambia la carpeta por la preparada
 * (con vuelta atrás si algo falla) y vuelve a abrir la app. Las rutas viajan en un JSON, no en la línea de comandos.
 */
async function applyOnExit({ pid, appRoot, staged, exePath }) {
  const dir = await fsp.mkdtemp(path.join(os.tmpdir(), 'zuena-update-'));
  const cfg = path.join(dir, 'update.json');
  const script = path.join(dir, 'update.ps1');
  await fsp.writeFile(cfg, JSON.stringify({ pid, appRoot, staged, old: `${appRoot}.anterior`, exePath }), 'utf8');
  await fsp.writeFile(script, `﻿$ErrorActionPreference = 'Stop'
$c = Get-Content -Raw -Encoding UTF8 -LiteralPath '${cfg.replace(/'/g, "''")}' | ConvertFrom-Json
for ($i = 0; $i -lt 300; $i++) { if (-not (Get-Process -Id $c.pid -ErrorAction SilentlyContinue)) { break }; Start-Sleep -Milliseconds 200 }
Start-Sleep -Milliseconds 400
try {
  if (Test-Path -LiteralPath $c.old) { Remove-Item -LiteralPath $c.old -Recurse -Force }
  Rename-Item -LiteralPath $c.appRoot -NewName (Split-Path -Leaf $c.old)
  try { Rename-Item -LiteralPath $c.staged -NewName (Split-Path -Leaf $c.appRoot) }
  catch { Rename-Item -LiteralPath $c.old -NewName (Split-Path -Leaf $c.appRoot); throw }
  Remove-Item -LiteralPath $c.old -Recurse -Force -ErrorAction SilentlyContinue
} catch { }
Start-Process -FilePath $c.exePath
Remove-Item -LiteralPath (Split-Path -Parent $MyInvocation.MyCommand.Path) -Recurse -Force -ErrorAction SilentlyContinue
`, 'utf8');
  // `cmd /c start` crea un proceso independiente que sobrevive al cierre de la app. Un PowerShell
  // "detached" sin consola no llega a arrancar, y un hijo común muere con el proceso padre.
  const line = `/d /s /c "start "" /min powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File "${script}""`;
  const child = spawn('cmd.exe', [line], { windowsVerbatimArguments: true, detached: true, stdio: 'ignore', windowsHide: true });
  child.unref();
}

module.exports = { parseManifest, fetchManifest, isNewer, prepareUpdate, applyOnExit, findAppRoot };
