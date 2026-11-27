'use strict';
// Copia el launcher empaquetado (dist/ZUENA Plugins-win32-x64) a %LOCALAPPDATA%\Programs\ZUENA Plugins,
// que es adonde apunta el acceso directo del escritorio. Así el launcher instalado siempre trae el
// catálogo y los paquetes recién generados.
// Uso: node build/install-local.js   (lo llama `npm run release`)

const fs = require('node:fs/promises');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const SRC = path.join(__dirname, '..', 'dist', 'ZUENA Plugins-win32-x64');
const DEST = path.join(process.env.LOCALAPPDATA || '', 'Programs', 'ZUENA Plugins');

function launcherRunning() {
  try {
    const out = execFileSync('tasklist', ['/FI', 'IMAGENAME eq ZUENA Plugins.exe', '/NH'], { encoding: 'utf8' });
    return out.toLowerCase().includes('zuena plugins.exe');
  } catch {
    return false;
  }
}

(async () => {
  if (process.platform !== 'win32' || !process.env.LOCALAPPDATA) throw new Error('Solo para Windows');
  await fs.access(path.join(SRC, 'ZUENA Plugins.exe'));
  if (launcherRunning()) throw new Error('ZUENA Plugins está abierto. Cerralo y volvé a ejecutar el comando.');
  const tmp = `${DEST}.nuevo`;
  const old = `${DEST}.anterior`;
  await fs.rm(tmp, { recursive: true, force: true });
  await fs.cp(SRC, tmp, { recursive: true });
  await fs.rm(old, { recursive: true, force: true });
  if (await fs.access(DEST).then(() => true, () => false)) await fs.rename(DEST, old);
  await fs.rename(tmp, DEST);
  await fs.rm(old, { recursive: true, force: true });
  // El acceso directo del escritorio apunta al .exe y toma el icono ZUENA del .ico al lado.
  const lnk = path.join(process.env.USERPROFILE || '', 'Desktop', 'ZUENA Plugins.lnk');
  const ps = [
    '$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:ZLNK)',
    '$s.TargetPath = Join-Path $env:ZDEST "ZUENA Plugins.exe"',
    '$s.WorkingDirectory = $env:ZDEST',
    '$s.IconLocation = (Join-Path $env:ZDEST "ZUENA Plugins.ico") + ",0"',
    '$s.Description = "ZUENA Plugins"',
    '$s.Save()',
  ].join('; ');
  execFileSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', ps], { env: { ...process.env, ZLNK: lnk, ZDEST: DEST } });
  console.log(`Launcher actualizado en: ${DEST}`);
  console.log(`Acceso directo: ${lnk}`);
})().catch((e) => {
  console.error(e.message);
  process.exit(1);
});
