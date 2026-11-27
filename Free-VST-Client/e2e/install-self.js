'use strict';
// E2E de la APP EMPAQUETADA: «Instalar en el equipo» desde una copia portable (zip descomprimido).
// Copia la app con su resources/app.asar (regresión EB-26: el fs de Electron trata .asar como
// carpeta y la copia fallaba), crea los accesos con el AppUserModelID y abre la copia instalada.
// LOCALAPPDATA, APPDATA y USERPROFILE apuntan a una carpeta temporal: no toca el sistema.
// Uso:  node e2e/install-self.js "dist\ZUENA Plugins-win32-x64\ZUENA Plugins.exe"

const { _electron: electron } = require('playwright-core');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const exe = path.resolve(process.argv[2] || path.join(__dirname, '..', 'dist', 'ZUENA Plugins-win32-x64', 'ZUENA Plugins.exe'));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const procsUnder = (root) => {
  const out = execFileSync('powershell.exe', ['-NoProfile', '-Command',
    `Get-Process 'ZUENA Plugins' -ErrorAction SilentlyContinue | ForEach-Object { $_.Path }`], { encoding: 'utf8' });
  return out.split(/\r?\n/).filter((p) => p && p.toLowerCase().startsWith(root.toLowerCase()));
};
const killUnder = (root) => {
  try {
    execFileSync('powershell.exe', ['-NoProfile', '-Command',
      `Get-Process 'ZUENA Plugins' -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '${root.replace(/'/g, "''")}*' } | Stop-Process -Force`]);
  } catch { /* ya cerrada */ }
};

(async () => {
  if (process.platform !== 'win32') { console.log('- solo Windows'); return; }
  const base = await fs.mkdtemp(path.join(os.tmpdir(), 'zuena-e2e-install-'));
  const fake = { LOCALAPPDATA: path.join(base, 'Local'), APPDATA: path.join(base, 'Roaming'), USERPROFILE: path.join(base, 'Usuario') };
  for (const d of [...Object.values(fake), path.join(fake.USERPROFILE, 'Desktop'), path.join(fake.APPDATA, 'Microsoft', 'Windows', 'Start Menu', 'Programs')]) {
    await fs.mkdir(d, { recursive: true });
  }
  const userData = path.join(base, 'perfil');
  await fs.mkdir(userData, { recursive: true });
  await fs.writeFile(path.join(userData, 'settings.json'), JSON.stringify({ onboarded: true, online: false, desktopNotify: false }));
  const installed = path.join(fake.LOCALAPPDATA, 'Programs', 'ZUENA Plugins');
  const ok = (m) => console.log(`✔ ${m}`);

  // Entrada de «Aplicaciones instaladas» en una clave de prueba (nunca la real del usuario).
  const testKey = 'Software\\ZUENA-Pruebas\\Uninstall\\ZUENA Plugins';
  const regEnv = { FREEVST_UNINSTALL_KEY: testKey };
  const readKey = () => require('../src/main/core/uninstall').readEntry(regEnv);
  const app = await electron.launch({ executablePath: exe, args: [], env: { ...process.env, ...fake, ...regEnv, FREEVST_USER_DATA: userData } });
  try {
    const win = await app.firstWindow();
    await win.locator('.hero__title').first().waitFor({ timeout: 20000 });
    const st = await win.evaluate(() => window.hub.getState());
    assert.equal(st.launcher.packaged, true);
    assert.equal(st.launcher.installed, false, 'la copia portable no figura como instalada');
    ok('copia portable: ofrece instalar en el equipo');
    // La app se cierra al terminar y abre la copia instalada: el resultado se mira en disco.
    win.evaluate(() => window.hub.installSelf({ desktop: true, startMenu: true })).then((r) => console.log('  installSelf:', JSON.stringify(r)), (e) => console.log('  installSelf falló:', e.message));
    await app.waitForEvent('close', { timeout: 120000 }).catch(() => {});
  } finally {
    await app.close().catch(() => {});
  }
  try {
    const asar = await fs.stat(path.join(installed, 'resources', 'app.asar'));
    const orig = await fs.stat(path.join(path.dirname(exe), 'resources', 'app.asar'));
    assert.ok(asar.isFile() && asar.size === orig.size, `resources/app.asar copiado entero (${asar.size} de ${orig.size} bytes)`);
    await fs.access(path.join(installed, 'ZUENA Plugins.exe'));
    const left = (await fs.readdir(path.dirname(installed))).filter((n) => /\.(nuevo|anterior)$/.test(n));
    assert.deepEqual(left, [], 'sin carpetas temporales');
    ok(`instalada en %LOCALAPPDATA%\\Programs\\ZUENA Plugins (app.asar ${(asar.size / 1048576).toFixed(1)} MB)`);
    for (const lnk of [path.join(fake.USERPROFILE, 'Desktop', 'ZUENA Plugins.lnk'), path.join(fake.APPDATA, 'Microsoft', 'Windows', 'Start Menu', 'Programs', 'ZUENA Plugins.lnk')]) {
      await fs.access(lnk);
    }
    ok('accesos directos en el escritorio y el menú Inicio');
    let running = [];
    for (let i = 0; i < 30 && !running.length; i += 1) { await sleep(1000); running = procsUnder(installed); }
    assert.ok(running.length, 'la copia instalada se abre sola');
    ok('se abre la copia instalada');

    // La copia instalada se registra en «Aplicaciones instaladas» con su versión y su desinstalador.
    const version = require('../package.json').version;
    let entry = null;
    for (let i = 0; i < 20 && !entry?.DisplayVersion; i += 1) { await sleep(500); entry = await readKey(); }
    assert.equal(entry?.DisplayName, 'ZUENA Plugins');
    assert.equal(entry?.DisplayVersion, version);
    assert.ok(entry?.UninstallString?.includes('--uninstall') && entry.UninstallString.toLowerCase().includes(installed.toLowerCase()), 'UninstallString');
    ok(`registrada en «Aplicaciones instaladas» (v${entry.DisplayVersion})`);

    // Desinstalar desde Windows: con la app cerrada, quita carpeta, accesos y entrada; no toca los plugins.
    killUnder(base);
    await sleep(1500);
    const child = require('node:child_process').spawn(path.join(installed, 'ZUENA Plugins.exe'), ['--uninstall'], {
      env: { ...process.env, ...fake, ...regEnv, FREEVST_USER_DATA: userData, FREEVST_UNINSTALL_YES: '1' }, stdio: 'ignore',
    });
    await new Promise((r) => child.on('exit', r));
    let gone = false;
    for (let i = 0; i < 30 && !gone; i += 1) { await sleep(500); gone = !(await fs.access(installed).then(() => true, () => false)); }
    assert.ok(gone, 'la carpeta de la app se borra al desinstalar');
    for (const lnk of [path.join(fake.USERPROFILE, 'Desktop', 'ZUENA Plugins.lnk'), path.join(fake.APPDATA, 'Microsoft', 'Windows', 'Start Menu', 'Programs', 'ZUENA Plugins.lnk')]) {
      assert.equal(await fs.access(lnk).then(() => true, () => false), false, `queda ${path.basename(lnk)}`);
    }
    assert.equal(await readKey(), null, 'la entrada de Windows se quita');
    ok('desinstalar quita la carpeta, los accesos directos y la entrada de Windows');
  } finally {
    require('node:child_process').spawnSync('reg.exe', ['delete', 'HKCU\\Software\\ZUENA-Pruebas', '/f'], { stdio: 'ignore' });
    killUnder(base);
    await sleep(1500);
    await fs.rm(base, { recursive: true, force: true }).catch(() => {});
  }
})().catch((e) => { console.error(`✘ ${e.message}`); process.exit(1); });
