'use strict';
// Empaqueta ZUENA Plugins como aplicación de Windows x64 en dist/.
// Uso: npm run package

const { packager } = require('@electron/packager');
const fs = require('node:fs/promises');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');

/** Reutiliza el zip de Electron ya descargado por `npm install` (evita otra descarga de ~110 MB). */
async function cachedElectronZipDir() {
  const version = require('electron/package.json').version;
  const cache = process.env.ELECTRON_CACHE
    || (process.env.LOCALAPPDATA ? path.join(process.env.LOCALAPPDATA, 'electron', 'Cache') : null);
  if (!cache) return undefined;
  for (const d of await fs.readdir(cache).catch(() => [])) {
    const dir = path.join(cache, d);
    const zip = path.join(dir, `electron-v${version}-win32-x64.zip`);
    if (await fs.access(zip).then(() => true, () => false)) return dir;
  }
  return undefined;
}

(async () => {
  const electronZipDir = await cachedElectronZipDir();
  const [appPath] = await packager({
    ...(electronZipDir ? { electronZipDir } : {}),
    dir: ROOT,
    out: path.join(ROOT, 'dist'),
    name: 'ZUENA Plugins',
    executableName: 'ZUENA Plugins',
    platform: 'win32',
    arch: 'x64',
    icon: path.join(__dirname, 'icon.ico'),
    overwrite: true,
    asar: { unpack: '**/catalog/packages/**' },
    prune: true,
    appCopyright: 'ZUENA',
    win32metadata: { ProductName: 'ZUENA Plugins', FileDescription: 'ZUENA Plugins: plugins VST3 gratuitos', CompanyName: 'ZUENA' },
    ignore: [/^\/e2e($|\/)/, /^\/test($|\/)/, /^\/dist($|\/)/, /^\/build\/.*\.js$/, /^\/build\/setup($|\/)/, /^\/README\.md$/],
  });
  // Smart App Control (EB-22): el packager reescribe los recursos del .exe (icono, metadatos y hash del
  // app.asar), así que cada build es un ejecutable sin reputación y Windows lo bloquea. Se usa el
  // electron.exe oficial sin modificar (renombrar no cambia su hash); el icono ZUENA lo aporta el acceso directo.
  const stockExe = path.join(path.dirname(require.resolve('electron/package.json')), 'dist', 'electron.exe');
  await fs.copyFile(stockExe, path.join(appPath, 'ZUENA Plugins.exe'));
  await fs.copyFile(path.join(__dirname, 'icon.ico'), path.join(appPath, 'ZUENA Plugins.ico'));
  await fs.writeFile(
    path.join(appPath, 'Probar en MODO DEMO.cmd'),
    '@echo off\r\nrem Abre ZUENA Plugins con paquetes de prueba locales (no toca tu carpeta VST3 real).\r\nstart "" "%~dp0ZUENA Plugins.exe" --demo\r\n',
  );
  console.log(`Empaquetado en: ${appPath}`);
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
