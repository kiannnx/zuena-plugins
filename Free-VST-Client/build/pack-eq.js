'use strict';
// Empaqueta el bundle compilado de ZUENA EQ.vst3 en catalog/packages/zuena-eq-<version>-win32.zip,
// y actualiza catalog/catalog.json con versión, tamaño y SHA-256 reales.
// La versión sale SOLO de plugins/ZuenaEQ/CMakeLists.txt (VERSION de juce_add_plugin): subirla ahí y
// reempaquetar es todo lo que hace falta para que el launcher ofrezca "Actualizar".
// El resto de la ficha del catálogo (textos, etiquetas, otros plugins) se conserva tal cual.
// Uso: node build/pack-eq.js

const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');
const { createZip } = require('../src/main/core/zip');
const { buildManifest, manifestEntryName } = require('../src/main/core/bundle-info');

const CLIENT_ROOT = path.join(__dirname, '..');
const REPO_ROOT = path.join(CLIENT_ROOT, '..');
const PACKAGES_DIR = path.join(CLIENT_ROOT, 'catalog', 'packages');
const CATALOG_PATH = path.join(CLIENT_ROOT, 'catalog', 'catalog.json');
const products = {
  'zuena-eq': { target: 'ZuenaEQ', name: 'ZUENA EQ' },
  'zuena-luma': { target: 'ZuenaLuma', name: 'ZUENA Luma' },
};

/** El nombre de target procede de la lista cerrada de productos. */
function readCmakeVersion(text, target = 'ZuenaEQ') {
  const block = new RegExp('juce_add_plugin\\(\\s*' + target + '\\b([\\s\\S]*?)\\)').exec(text);
  const m = block && /^\s*VERSION\s+(\d+\.\d+\.\d+)\s*$/m.exec(block[1]);
  if (!m) throw new Error(`No se encontró VERSION en juce_add_plugin(${target})`);
  return m[1];
}

async function collectBundleEntries(dir, prefix) {
  const entries = [{ name: `${prefix}/` }];
  const items = (await fs.readdir(dir, { withFileTypes: true })).sort((a, b) => a.name.localeCompare(b.name));
  for (const it of items) {
    const full = path.join(dir, it.name);
    const rel = `${prefix}/${it.name}`;
    if (it.isDirectory()) entries.push(...(await collectBundleEntries(full, rel)));
    else if (it.isFile()) entries.push({ name: rel, data: await fs.readFile(full) });
  }
  return entries;
}


async function pack(PLUGIN_ID, catalog) {
  const { target, name } = products[PLUGIN_ID];
  const bundle = name + '.vst3';
  // ZUENA_BUILD_DIR: carpeta de compilación alternativa (p. ej. build/sandbox-release, compilada en Windows Sandbox).
  const BUILD_DIR = process.env.ZUENA_BUILD_DIR ? path.resolve(REPO_ROOT, process.env.ZUENA_BUILD_DIR) : path.join(REPO_ROOT, 'build', 'release');
  const VST3_BUNDLE = path.join(BUILD_DIR, 'plugins', target, target+'_artefacts', 'Release', 'VST3', bundle);
  const CMAKE_PATH = path.join(REPO_ROOT,'plugins',target,'CMakeLists.txt');
  const VERSION = readCmakeVersion(await fs.readFile(CMAKE_PATH, 'utf8'), target);
  const ZIP_NAME = `${PLUGIN_ID}-${VERSION}-win32.zip`;
  const dll = path.join(VST3_BUNDLE, 'Contents', 'x86_64-win', bundle);
  await fs.access(dll);
  // EB-21: la versión de archivo de la DLL tiene que coincidir con CMake (si no, el build está viejo).
  if (process.platform === 'win32') {
    const fileVersion = execFileSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
      '(Get-Item -LiteralPath $env:ZEQ_DLL).VersionInfo.FileVersion'], { encoding: 'utf8', env: { ...process.env, ZEQ_DLL: dll } }).trim();
    if (fileVersion !== VERSION) {
      throw new Error(`La DLL compilada es v${fileVersion} y CMakeLists.txt dice v${VERSION}. Ejecutá tools\\build.cmd antes de empaquetar.`);
    }
  }
  await fs.mkdir(PACKAGES_DIR, { recursive: true });
  const manifestName = manifestEntryName(bundle);
  const entries = (await collectBundleEntries(VST3_BUNDLE, bundle)).filter((e) => e.name !== manifestName);
  if (!entries.some((e) => e.name === bundle+'/Contents/Resources/')) entries.push({ name: bundle+'/Contents/Resources/' });
  entries.push({ name: manifestName, data: buildManifest({ id: PLUGIN_ID, version: VERSION }) });
  const zipBuf = createZip(entries);
  const zipPath = path.join(PACKAGES_DIR, ZIP_NAME);
  await replaceFile(zipPath, zipBuf);

  const sha256 = crypto.createHash('sha256').update(zipBuf).digest('hex');
  const entry = catalog.plugins.find((p) => p.id === PLUGIN_ID);
  if (!entry) throw new Error(`El catálogo no tiene la ficha "${PLUGIN_ID}"`);
  entry.version = VERSION;
  entry.status = 'available';
  entry.downloads = { ...(entry.downloads ?? {}), win32: { url: `bundled://${ZIP_NAME}`, sha256, size: zipBuf.length } };
  delete entry.demos;
  // Vídeo de fondo de la ficha (videos/zuena-eq, `npm run render`), claro y oscuro. Sin render, la ficha usa la carátula.
  const exists = (f) => fs.access(path.join(PACKAGES_DIR, f)).then(() => true, () => false);
  const stage = PLUGIN_ID + '-stage';
  if (await exists(stage+'.mp4') && await exists(stage+'.jpg')) {
    entry.media = { video: 'bundled://'+stage+'.mp4', poster: 'bundled://'+stage+'.jpg' };
    if (await exists(stage+'-oscuro.mp4') && await exists(stage+'-oscuro.jpg')) {
      Object.assign(entry.media, { videoDark: 'bundled://'+stage+'-oscuro.mp4', posterDark: 'bundled://'+stage+'-oscuro.jpg' });
    }
  } else delete entry.media;
  // Galería: solo las imágenes que existen (tools/eq-gallery.ps1 las genera desde capturas reales).
  if (Array.isArray(entry.gallery)) {
    const present = [];
    for (const g of entry.gallery) {
      const file = /^bundled:\/\/(.+)$/.exec(g.src)?.[1];
      if (file && await fs.access(path.join(PACKAGES_DIR, file)).then(() => true, () => false)) present.push(g);
      else console.warn(`Galería: falta ${g.src}, se omite`);
    }
    entry.gallery = present;
  }
  console.log(`${name} v${VERSION} (desde CMakeLists.txt)`);
  console.log(`Paquete listo: ${zipPath} (${zipBuf.length} B, sha256=${sha256.slice(0, 16)}…)`);
}

async function replaceFile(file, contents) {
  const temp = file+'.'+process.pid+'.tmp';
  try { await fs.writeFile(temp, contents); await fs.rename(temp, file); }
  finally { await fs.rm(temp, {force:true}); }
}

if (require.main !== module) {
  module.exports = { readCmakeVersion };
} else (async () => {
  const option = process.argv[2] || 'zuena-eq';
  const ids = option === '--all' ? Object.keys(products) : [option];
  const catalog = JSON.parse(await fs.readFile(CATALOG_PATH, 'utf8'));
  for (const id of ids) {
    if (!Object.hasOwn(products, id)) throw new Error('Producto desconocido: '+id);
    await pack(id, catalog);
  }
  catalog.updated = new Date().toISOString().slice(0, 10);
  await replaceFile(CATALOG_PATH, JSON.stringify(catalog, null, 2)+'\n');
  const current = new Set(catalog.plugins.flatMap(p => Object.values(p.downloads || {}).map(d => d.url)));
  for (const file of await fs.readdir(PACKAGES_DIR)) {
    if (ids.some(id => file.startsWith(id+'-')) && /^.+-\d+\.\d+\.\d+-win32\.zip$/.test(file) && !current.has('bundled://'+file))
      await fs.rm(path.join(PACKAGES_DIR,file));
  }
  console.log(`Catálogo actualizado: ${CATALOG_PATH}`);
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
