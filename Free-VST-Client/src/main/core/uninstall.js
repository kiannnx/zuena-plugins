'use strict';
// Entrada de «Aplicaciones instaladas» de Windows y desinstalación del launcher.
// - La entrada vive en HKCU (sin permisos de administrador) y la escribe tanto el instalador como el
//   launcher al arrancar (así DisplayVersion sigue a las autoactualizaciones).
// - Desinstalar = el mismo ZUENA Plugins.exe con --uninstall: quita accesos y entrada, y borra su carpeta
//   con un script que espera a que el proceso termine. Los plugins VST3 del usuario no se tocan.
// FREEVST_UNINSTALL_KEY redirige la clave (pruebas). Se usa reg.exe: sin módulos nativos.

const path = require('node:path');
const os = require('node:os');
const { execFile, spawn } = require('node:child_process');
const { fsp } = require('./real-fs');

const PRODUCT = 'ZUENA Plugins';
const DEFAULT_KEY = `HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${PRODUCT}`;
const SOURCE_URL = 'https://github.com/kiannnx/zuena-plugins';

const uninstallKey = (env = process.env) => (env.FREEVST_UNINSTALL_KEY ? `HKCU\\${env.FREEVST_UNINSTALL_KEY}` : DEFAULT_KEY);

/** Valores de la entrada: [nombre, tipo, dato]. */
function entryValues({ installDir, version, sizeKb }) {
  const exe = path.join(installDir, `${PRODUCT}.exe`);
  return [
    ['DisplayName', 'REG_SZ', PRODUCT],
    ['DisplayVersion', 'REG_SZ', version],
    ['Publisher', 'REG_SZ', 'ZUENA'],
    ['DisplayIcon', 'REG_SZ', path.join(installDir, `${PRODUCT}.ico`)],
    ['InstallLocation', 'REG_SZ', installDir],
    ['UninstallString', 'REG_SZ', `"${exe}" --uninstall`],
    ['URLInfoAbout', 'REG_SZ', SOURCE_URL],
    ['NoModify', 'REG_DWORD', '1'],
    ['NoRepair', 'REG_DWORD', '1'],
    ...(sizeKb ? [['EstimatedSize', 'REG_DWORD', String(Math.round(sizeKb))]] : []),
  ];
}

function run(file, args) {
  return new Promise((resolve, reject) => {
    execFile(file, args, { windowsHide: true }, (err, stdout) => (err ? reject(err) : resolve(stdout)));
  });
}

async function folderSizeKb(dir) {
  let total = 0;
  const walk = async (d) => {
    for (const e of await fsp.readdir(d, { withFileTypes: true })) {
      const p = path.join(d, e.name);
      if (e.isDirectory()) await walk(p);
      else total += (await fsp.stat(p)).size;
    }
  };
  try { await walk(dir); } catch { return 0; }
  return total / 1024;
}

/** Escribe (o actualiza) la entrada de Windows. */
async function writeEntry({ installDir, version, env = process.env }) {
  if (process.platform !== 'win32') return false;
  const key = uninstallKey(env);
  const sizeKb = await folderSizeKb(installDir);
  for (const [name, type, data] of entryValues({ installDir, version, sizeKb })) {
    await run('reg.exe', ['add', key, '/v', name, '/t', type, '/d', data, '/f']);
  }
  return true;
}

async function removeEntry(env = process.env) {
  if (process.platform !== 'win32') return;
  try { await run('reg.exe', ['delete', uninstallKey(env), '/f']); } catch { /* ya no estaba */ }
}

async function readEntry(env = process.env) {
  try {
    const out = await run('reg.exe', ['query', uninstallKey(env)]);
    const values = {};
    for (const line of out.split(/\r?\n/)) {
      const m = /^\s+(\S+)\s+REG_\w+\s+(.*)$/.exec(line);
      if (m) values[m[1]] = m[2].trim();
    }
    return values;
  } catch {
    return null;
  }
}

/** Script que espera a que el proceso termine y borra la carpeta de la app (y a sí mismo). */
function removalScript({ pid, installDir }) {
  return [
    '@echo off',
    ':wait',
    `tasklist /FI "PID eq ${pid}" 2>nul | find "${pid}" >nul && (timeout /t 1 /nobreak >nul & goto wait)`,
    `rmdir /s /q "${installDir}"`,
    '(goto) 2>nul & del "%~f0"',
    '',
  ].join('\r\n');
}

/**
 * Desinstala la copia instalada: accesos directos, entrada de Windows y, al cerrar, la carpeta.
 * Solo actúa si `installDir` es la carpeta estándar de instalación (nunca borra otra cosa).
 */
async function uninstall({ installDir, expectedDir, pid, shortcuts, env = process.env }) {
  if (path.resolve(installDir).toLowerCase() !== path.resolve(expectedDir).toLowerCase()) {
    throw new Error('Solo se puede desinstalar la copia instalada en el equipo.');
  }
  for (const link of shortcuts) await fsp.rm(link, { force: true }).catch(() => {});
  await removeEntry(env);
  const script = path.join(os.tmpdir(), `zuena-desinstalar-${pid}.cmd`);
  await fsp.writeFile(script, removalScript({ pid, installDir }), 'utf8');
  // Mismo método que el reemplazo de la autoactualización: cmd /c start sobrevive al cierre de la app.
  const child = spawn('cmd.exe', [`/d /s /c "start "" /min "${script}""`], {
    windowsVerbatimArguments: true, detached: true, stdio: 'ignore', windowsHide: true,
  });
  child.unref();
  return script;
}

module.exports = { DEFAULT_KEY, uninstallKey, entryValues, writeEntry, removeEntry, readEntry, removalScript, uninstall };
