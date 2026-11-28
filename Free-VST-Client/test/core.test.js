'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');

const { validateCatalog, loadCatalog, compareVersions, isAllowedUrl, isBundledUrl, resolveBundledUrls } = require('../src/main/core/catalog');
const { download } = require('../src/main/core/downloader');
const { installPackage, uninstallFiles, uninstallObsModules, isInside } = require('../src/main/core/installer');
const { obsPluginsDir } = require('../src/main/core/paths');
const { createZip } = require('../src/main/core/zip');
const { startDemoServer } = require('../src/main/core/demo-server');
const { startPackageServer } = require('../src/main/core/package-server');

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
// Carpetas temporales de cada test: se borran todas al terminar (antes quedaban en %TEMP%).
const tmpDirs = [];
const tmp = async () => { const d = await fs.mkdtemp(path.join(os.tmpdir(), 'zuena-test-')); tmpDirs.push(d); return d; };
test.after(() => Promise.all(tmpDirs.map((d) => fs.rm(d, { recursive: true, force: true }).catch(() => {}))));

function serve(routes) {
  return new Promise((resolve) => {
    const s = http.createServer((req, res) => {
      const r = routes[req.url];
      if (!r) { res.writeHead(404); return res.end(); }
      if (r.redirect) { res.writeHead(302, { Location: r.redirect }); return res.end(); }
      res.writeHead(200, { 'Content-Length': r.body.length });
      if (r.slow) {
        let off = 0;
        const pump = () => {
          if (res.destroyed) return;
          if (off >= r.body.length) return res.end();
          res.write(r.body.subarray(off, (off += 16384)));
          setTimeout(pump, 5);
        };
        return pump();
      }
      return res.end(r.body);
    });
    s.listen(0, '127.0.0.1', () => resolve({ base: `http://127.0.0.1:${s.address().port}`, close: () => s.close() }));
  });
}

const pkg = (name = 'Test Plugin') => createZip([
  { name: `${name}.vst3/` },
  { name: `${name}.vst3/Contents/` },
  { name: `${name}.vst3/Contents/Resources/` },
  { name: `${name}.vst3/Contents/Resources/a.txt`, data: 'hola' },
]);

test('catálogo: valida, descarta entradas inválidas y elige destacado', () => {
  const { catalog, errors } = validateCatalog({
    schema: 1,
    featured: 'nope',
    plugins: [
      { id: 'ok', name: 'OK', version: '1.0.0', status: 'coming-soon' },
      { id: 'bad id', name: 'X', version: '1.0.0' },
      { id: 'nodl', name: 'Y', version: '1.0.0', status: 'available' },
      { id: 'http', name: 'Z', version: '1.0.0', downloads: { win32: { url: 'http://evil.com/a.zip', sha256: 'a'.repeat(64), size: 1 } } },
      { id: 'ok', name: 'dup', version: '1.0.0', status: 'coming-soon' },
    ],
  });
  assert.deepEqual(catalog.plugins.map((p) => p.id), ['ok']);
  assert.equal(catalog.featured, 'ok');
  assert.equal(errors.length, 4);
  assert.throws(() => validateCatalog({ plugins: [] }));
});

test('catálogo integrado del launcher es válido', async () => {
  const raw = JSON.parse(await fs.readFile(path.join(__dirname, '..', 'catalog', 'catalog.json'), 'utf8'));
  const { catalog, errors } = validateCatalog(raw);
  assert.deepEqual(errors, []);
  assert.ok(catalog.plugins.length >= 1);
});

test('urls y versiones', () => {
  assert.ok(isAllowedUrl('https://x.com/a'));
  assert.ok(isAllowedUrl('http://127.0.0.1:9/a'));
  assert.ok(!isAllowedUrl('http://x.com/a'));
  assert.ok(!isAllowedUrl('file:///c:/a'));
  assert.equal(compareVersions('1.10.0', '1.9.9'), 1);
  assert.equal(compareVersions('1.0.0', '1.0.0'), 0);
  assert.equal(compareVersions('0.9.0', '1.0.0'), -1);
});

test('catálogo: remoto caído → caché → integrado', async () => {
  const dir = await tmp();
  const bundledPath = path.join(dir, 'b.json');
  const cachePath = path.join(dir, 'c.json');
  await fs.writeFile(bundledPath, JSON.stringify({ schema: 1, plugins: [{ id: 'bundled', name: 'B', version: '1.0.0', status: 'coming-soon' }] }));
  const remote = { schema: 1, plugins: [{ id: 'remote', name: 'R', version: '1.0.0', status: 'coming-soon' }] };
  const srv = await serve({ '/cat.json': { body: Buffer.from(JSON.stringify(remote)) } });
  const r1 = await loadCatalog({ url: `${srv.base}/cat.json`, cachePath, bundledPath });
  assert.equal(r1.source, 'remote');
  const r2 = await loadCatalog({ url: `${srv.base}/missing.json`, cachePath, bundledPath });
  assert.equal(r2.source, 'cache');
  assert.equal(r2.catalog.plugins[0].id, 'remote');
  await fs.rm(cachePath);
  const r3 = await loadCatalog({ url: `${srv.base}/missing.json`, cachePath, bundledPath });
  assert.equal(r3.source, 'bundled');
  assert.ok(r3.warning);
  srv.close();
});

test('descarga: progreso, redirección y SHA-256 correcto', async () => {
  const body = crypto.randomBytes(600_000);
  const srv = await serve({ '/r': { redirect: '/f' }, '/f': { body, slow: true } });
  const dir = await tmp();
  const seen = [];
  const res = await download({
    url: `${srv.base}/r`, dest: path.join(dir, 'f.zip'), sha256: sha(body), size: body.length, onProgress: (p) => seen.push(p.received),
  });
  assert.equal(res.bytes, body.length);
  assert.ok(seen.length >= 2, 'emite progreso');
  assert.deepEqual(await fs.readFile(path.join(dir, 'f.zip')), body);
  srv.close();
});

test('descarga: checksum incorrecto se rechaza y no deja archivos', async () => {
  const body = Buffer.from('contenido');
  const srv = await serve({ '/f': { body } });
  const dir = await tmp();
  await assert.rejects(download({ url: `${srv.base}/f`, dest: path.join(dir, 'f.zip'), sha256: '0'.repeat(64), size: body.length }), { code: 'CHECKSUM' });
  assert.deepEqual(await fs.readdir(dir), []);
  srv.close();
});

test('descarga: cancelación limpia el parcial', async () => {
  const body = crypto.randomBytes(3_000_000);
  const srv = await serve({ '/f': { body, slow: true } });
  const dir = await tmp();
  const ctrl = new AbortController();
  setTimeout(() => ctrl.abort(), 60);
  await assert.rejects(download({ url: `${srv.base}/f`, dest: path.join(dir, 'f.zip'), sha256: sha(body), size: body.length, signal: ctrl.signal }), { code: 'CANCELED' });
  assert.deepEqual(await fs.readdir(dir), []);
  srv.close();
});

test('descarga: rechaza http no local y 404', async () => {
  await assert.rejects(download({ url: 'http://example.com/a.zip', dest: 'x', sha256: '0'.repeat(64) }), { code: 'BAD_URL' });
  const srv = await serve({});
  await assert.rejects(download({ url: `${srv.base}/nope`, dest: path.join(await tmp(), 'x'), sha256: '0'.repeat(64) }), { code: 'HTTP' });
  srv.close();
});

test('instalación y desinstalación de un bundle .vst3', async () => {
  const dir = await tmp();
  const zip = path.join(dir, 'p.zip');
  await fs.writeFile(zip, pkg());
  const target = path.join(dir, 'VST3');
  const r = await installPackage({ zipPath: zip, targetDir: target, stagingDir: path.join(dir, 'stage') });
  assert.equal(r.elevated, false);
  assert.deepEqual(r.files, [path.join(target, 'Test Plugin.vst3')]);
  assert.equal(await fs.readFile(path.join(target, 'Test Plugin.vst3', 'Contents', 'Resources', 'a.txt'), 'utf8'), 'hola');
  await assert.rejects(fs.access(path.join(dir, 'stage')), 'limpia staging');
  const again = await installPackage({ zipPath: zip, targetDir: target, stagingDir: path.join(dir, 'stage') });
  assert.deepEqual(again.files, r.files, 'reinstalar sobrescribe');
  await uninstallFiles({ files: r.files, targetDir: target });
  assert.deepEqual(await fs.readdir(target), []);
});

test('instalación: paquete sin .vst3 o zip corrupto falla sin tocar el destino', async () => {
  const dir = await tmp();
  const target = path.join(dir, 'VST3');
  const noVst = path.join(dir, 'a.zip');
  await fs.writeFile(noVst, createZip([{ name: 'readme.txt', data: 'x' }]));
  await assert.rejects(installPackage({ zipPath: noVst, targetDir: target, stagingDir: path.join(dir, 's') }), { code: 'NO_VST3' });
  const bad = path.join(dir, 'b.zip');
  await fs.writeFile(bad, 'no soy un zip');
  await assert.rejects(installPackage({ zipPath: bad, targetDir: target, stagingDir: path.join(dir, 's') }), { code: 'ZIP' });
  await assert.rejects(fs.access(target));
});

test('extracción: archivos grandes (> 64 KB) no cuelgan (regresión EB-07)', { timeout: 15000 }, async () => {
  const dir = await tmp();
  const zip = path.join(dir, 'big.zip');
  const payload = crypto.randomBytes(5 * 1024 * 1024);
  await fs.writeFile(zip, createZip([{ name: 'Big.vst3/' }, { name: 'Big.vst3/payload.bin', data: payload }]));
  const r = await installPackage({ zipPath: zip, targetDir: path.join(dir, 'VST3'), stagingDir: path.join(dir, 's') });
  assert.deepEqual(await fs.readFile(path.join(r.files[0], 'payload.bin')), payload);
});

test('extracción: zip-slip (../) rechazado sin escribir fuera', async () => {
  const dir = await tmp();
  const zip = path.join(dir, 'evil.zip');
  await fs.writeFile(zip, createZip([{ name: 'ok.vst3/' }, { name: '../../evil.txt', data: 'x' }]));
  await assert.rejects(installPackage({ zipPath: zip, targetDir: path.join(dir, 'VST3'), stagingDir: path.join(dir, 's', 'stage') }), { code: 'ZIP' });
  await assert.rejects(fs.access(path.join(dir, 'evil.txt')));
  await assert.rejects(fs.access(path.join(dir, 'VST3')));
});

test('ZUENA Link: el paquete con fuente de OBS instala el VST3 y el módulo de OBS, y los desinstala', async () => {
  const dir = await tmp();
  const zip = path.join(dir, 'link.zip');
  await fs.writeFile(zip, createZip([
    { name: 'ZUENA Link.vst3/' },
    { name: 'ZUENA Link.vst3/Contents/' },
    { name: 'ZUENA Link.vst3/Contents/x86_64-win/' },
    { name: 'ZUENA Link.vst3/Contents/x86_64-win/ZUENA Link.vst3', data: 'vst3' },
    { name: 'obs-studio/' },
    { name: 'obs-studio/zuena-link/' },
    { name: 'obs-studio/zuena-link/bin/' },
    { name: 'obs-studio/zuena-link/bin/64bit/' },
    { name: 'obs-studio/zuena-link/bin/64bit/zuena-link.dll', data: 'obs' },
    { name: 'obs-studio/zuena-link/data/' },
  ]));
  const target = path.join(dir, 'VST3');
  const obs = path.join(dir, 'ProgramData', 'obs-studio', 'plugins');
  const r = await installPackage({ zipPath: zip, targetDir: target, obsDir: obs, stagingDir: path.join(dir, 'stage') });
  assert.deepEqual(r.files, [path.join(target, 'ZUENA Link.vst3')]);
  assert.deepEqual(r.obs, [path.join(obs, 'zuena-link')]);
  assert.equal(await fs.readFile(path.join(obs, 'zuena-link', 'bin', '64bit', 'zuena-link.dll'), 'utf8'), 'obs');
  await assert.rejects(fs.access(path.join(target, 'obs-studio')), 'la fuente de OBS no queda en la carpeta VST3');
  // Reinstalar (actualizar) reemplaza el módulo sin duplicarlo.
  const again = await installPackage({ zipPath: zip, targetDir: target, obsDir: obs, stagingDir: path.join(dir, 'stage') });
  assert.deepEqual(again.obs, r.obs);
  // Sin carpeta de OBS (paquetes de otros plugins o modo sin OBS) se instala solo el VST3.
  const plain = await installPackage({ zipPath: zip, targetDir: path.join(dir, 'V2'), stagingDir: path.join(dir, 'stage') });
  assert.deepEqual(plain.obs, []);
  await uninstallObsModules({ dirs: r.obs, obsDir: obs });
  assert.deepEqual(await fs.readdir(obs), []);
  // Solo carpetas de módulo directamente dentro de la carpeta de plugins de OBS.
  await assert.rejects(uninstallObsModules({ dirs: [obs], obsDir: obs }), { code: 'PATH' });
  await assert.rejects(uninstallObsModules({ dirs: [path.join(obs, 'zuena-link', 'bin')], obsDir: obs }), { code: 'PATH' });
  await assert.rejects(uninstallObsModules({ dirs: [path.join(dir, 'otra')], obsDir: obs }), { code: 'PATH' });
  // Un módulo mal armado (sin la DLL con su nombre) no se instala.
  await fs.writeFile(zip, createZip([{ name: 'X.vst3/' }, { name: 'obs-studio/x/' }, { name: 'obs-studio/x/x.dll', data: '1' }]));
  const odd = await installPackage({ zipPath: zip, targetDir: target, obsDir: obs, stagingDir: path.join(dir, 'stage') });
  assert.deepEqual(odd.obs, []);
  assert.equal(obsPluginsDir({ ProgramData: 'D:\\PD' }), path.join('D:\\PD', 'obs-studio', 'plugins'));
});

test('desinstalación: rechaza rutas fuera de la carpeta o que no son .vst3', async () => {
  const dir = await tmp();
  await assert.rejects(uninstallFiles({ files: [path.join(dir, '..', 'x.vst3')], targetDir: dir }), { code: 'PATH' });
  await assert.rejects(uninstallFiles({ files: [path.join(dir, 'x.dll')], targetDir: dir }), { code: 'PATH' });
  assert.ok(isInside(path.join('a', 'b'), path.join('a', 'b', 'c.vst3')));
  assert.ok(!isInside(path.join('a', 'b'), path.join('a', 'bc', 'c.vst3')));
});

test('EB-34: permisos denegados activan la elevación y un error de disco no', async (t) => {
  const dir = await tmp();
  const zip = path.join(dir, 'p.zip');
  await fs.writeFile(zip, pkg());
  const target = path.join(dir, 'ro');
  const bundle = path.join(target, 'Test Plugin.vst3');
  const options = { zipPath: zip, targetDir: target, stagingDir: path.join(dir, 's') };
  const writeFile = fs.writeFile;
  let errorCode = 'EACCES';
  let probes = 0;
  t.mock.method(fs, 'writeFile', async (file, ...args) => {
    if (typeof file === 'string' && path.dirname(file) === target && path.basename(file).startsWith('.zuena-probe-')) {
      probes++;
      throw Object.assign(new Error('Escritura rechazada por el sistema'), { code: errorCode });
    }
    return writeFile(file, ...args);
  });
  const elevate = t.mock.fn(async (ops) => {
    assert.deepEqual(ops.map(({ op, to }) => [op, to]), [['replace', bundle]]);
    for (const { from, to } of ops) await fs.cp(from, to, { recursive: true });
  });

  await assert.rejects(installPackage(options), { code: 'PERMISSION' });
  for (const code of ['EACCES', 'EPERM']) {
    errorCode = code;
    const installed = await installPackage({ ...options, runElevated: elevate });
    assert.equal(installed.elevated, true);
    assert.deepEqual(installed.files, [bundle]);
    assert.equal(await fs.readFile(path.join(bundle, 'Contents', 'Resources', 'a.txt'), 'utf8'), 'hola');
  }
  errorCode = 'ENOSPC';
  await assert.rejects(installPackage({ ...options, runElevated: elevate }), { code: 'DIR' });
  assert.equal(probes, 4);
  assert.equal(elevate.mock.callCount(), 2);
});

test('modo demo: catálogo válido, paquete instalable y paquete alterado rechazado', async () => {
  const demo = await startDemoServer();
  const dir = await tmp();
  const res = await loadCatalog({ url: demo.url, cachePath: path.join(dir, 'c.json'), bundledPath: path.join(dir, 'none.json') });
  assert.deepEqual(res.errors, []);
  assert.equal(res.source, 'remote');
  const p = res.catalog.plugins.find((x) => x.id === 'demo-tape-stop');
  const dl = p.downloads.win32;
  const zip = path.join(dir, 'p.zip');
  await download({ url: dl.url, dest: zip, sha256: dl.sha256, size: dl.size });
  const r = await installPackage({ zipPath: zip, targetDir: path.join(dir, 'VST3'), stagingDir: path.join(dir, 's') });
  assert.equal(path.basename(r.files[0]), 'Demo Tape Stop.vst3');
  const broken = res.catalog.plugins.find((x) => x.id === 'demo-corrupto').downloads.win32;
  await assert.rejects(download({ url: broken.url, dest: path.join(dir, 'b.zip'), sha256: broken.sha256, size: broken.size }), { code: 'CHECKSUM' });
  demo.close();
});

test('paquete integrado de ZUENA EQ: servidor local, descarga SHA-256 e instalación real', async () => {
  assert.ok(isBundledUrl('bundled://zuena-eq-0.1.0-win32.zip'));
  assert.ok(!isBundledUrl('bundled://../evil.zip'));
  assert.ok(!isBundledUrl('bundled://sub/a.zip'));

  const raw = JSON.parse(await fs.readFile(path.join(__dirname, '..', 'catalog', 'catalog.json'), 'utf8'));
  const remoteCheck = validateCatalog(raw, { allowBundled: false });
  assert.ok(!remoteCheck.catalog.plugins.some((p) => p.id === 'zuena-eq'), 'catálogo remoto no acepta bundled://');

  const localCheck = validateCatalog(raw, { allowBundled: true });
  assert.deepEqual(localCheck.errors, []);
  assert.equal(localCheck.catalog.featured, 'zuena-eq');

  const pkgSrv = await startPackageServer(path.join(__dirname, '..', 'catalog', 'packages'));
  const dir = await tmp();
  try {
    const resolved = resolveBundledUrls(localCheck.catalog, pkgSrv.base);
    const eq = resolved.plugins.find((p) => p.id === 'zuena-eq');
    assert.ok(eq && eq.status === 'available');
    const dl = eq.downloads.win32;
    const zip = path.join(dir, 'zuena-eq.zip');
    await download({ url: dl.url, dest: zip, sha256: dl.sha256, size: dl.size });
    const r = await installPackage({ zipPath: zip, targetDir: path.join(dir, 'VST3'), stagingDir: path.join(dir, 's') });
    assert.equal(path.basename(r.files[0]), 'ZUENA EQ.vst3');
    const dllStat = await fs.stat(path.join(r.files[0], 'Contents', 'x86_64-win', 'ZUENA EQ.vst3'));
    assert.ok(dllStat.size > 3_000_000, 'el binario real del VST3 está instalado');
  } finally {
    pkgSrv.close();
  }
});

// ---------- Sincronización de versiones plugin ↔ launcher ----------
const { buildManifest, manifestEntryName, readBundleVersion } = require('../src/main/core/bundle-info');
const { readCmakeVersion } = require('../build/pack-eq');

test('pack-eq lee la VERSION de juce_add_plugin(ZuenaEQ) y coincide con el catálogo integrado', async () => {
  const cmake = await fs.readFile(path.join(__dirname, '..', '..', 'plugins', 'ZuenaEQ', 'CMakeLists.txt'), 'utf8');
  const v = readCmakeVersion(cmake);
  assert.match(v, /^\d+\.\d+\.\d+$/);
  const cat = JSON.parse(await fs.readFile(path.join(__dirname, '..', 'catalog', 'catalog.json'), 'utf8'));
  const eq = cat.plugins.find((p) => p.id === 'zuena-eq');
  assert.equal(eq.version, v, 'catalog.json desfasado: ejecutá `npm run pack:eq` tras cambiar la versión en CMake');
  assert.equal(eq.downloads.win32.url, `bundled://zuena-eq-${v}-win32.zip`);
  const zip = await fs.readFile(path.join(__dirname, '..', 'catalog', 'packages', `zuena-eq-${v}-win32.zip`));
  assert.equal(zip.length, eq.downloads.win32.size);
  assert.equal(sha(zip), eq.downloads.win32.sha256);
  assert.throws(() => readCmakeVersion('juce_add_plugin(Otro VERSION 1.0.0)'));
});

test('ZUENA Luma: versión de CMake, paquete y ficha publicada coinciden', async () => {
  const cmake = await fs.readFile(path.join(__dirname, '..', '..', 'plugins', 'ZuenaLuma', 'CMakeLists.txt'), 'utf8');
  const v = readCmakeVersion(cmake, 'ZuenaLuma');
  const cat = JSON.parse(await fs.readFile(path.join(__dirname, '..', 'catalog', 'catalog.json'), 'utf8'));
  const luma = cat.plugins.find((p) => p.id === 'zuena-luma');
  assert.equal(luma.version, v, 'catalog.json desfasado: ejecutá `npm run pack:luma`');
  assert.equal(luma.status, 'available');
  assert.equal(luma.bundle, 'ZUENA Luma.vst3');
  assert.equal(luma.downloads.win32.url, `bundled://zuena-luma-${v}-win32.zip`);
  const zip = await fs.readFile(path.join(__dirname, '..', 'catalog', 'packages', `zuena-luma-${v}-win32.zip`));
  assert.equal(zip.length, luma.downloads.win32.size);
  assert.equal(sha(zip), luma.downloads.win32.sha256);
  const { catalog, errors } = validateCatalog(cat);
  assert.deepEqual(errors, []);
  const p = catalog.plugins.find((x) => x.id === 'zuena-luma');
  assert.ok(p.media.video && p.media.videoDark, 'vídeo de fondo claro y oscuro');
  for (const g of p.gallery) await fs.access(path.join(__dirname, '..', 'catalog', 'packages', g.src.replace('bundled://', '')));
});

test('el manifiesto dentro del bundle instalado da la versión en disco', async () => {
  const dir = await tmp();
  const zipPath = path.join(dir, 'p.zip');
  await fs.writeFile(zipPath, createZip([
    { name: 'Demo.vst3/' },
    { name: 'Demo.vst3/Contents/x86_64-win/Demo.vst3', data: 'bin' },
    { name: manifestEntryName('Demo.vst3'), data: buildManifest({ id: 'demo', version: '1.2.3' }) },
  ]));
  const target = path.join(dir, 'VST3');
  const res = await installPackage({ zipPath, targetDir: target, stagingDir: path.join(dir, 'st') });
  assert.equal(await readBundleVersion(res.files[0], 'demo'), '1.2.3');
  assert.equal(await readBundleVersion(res.files[0], 'otro-id'), null);
  assert.equal(await readBundleVersion(path.join(target, 'NoExiste.vst3')), null);
  assert.throws(() => buildManifest({ id: 'x', version: '1.2' }));
  await fs.rm(dir, { recursive: true, force: true });
});

test('EB-19: si el DAW bloquea el bundle, actualizar o desinstalar falla sin romper la versión instalada', async () => {
  const dir = await tmp();
  const target = path.join(dir, 'VST3');
  const mk = async (name, body) => {
    const z = path.join(dir, name);
    await fs.writeFile(z, createZip([{ name: 'Lock.vst3/' }, { name: 'Lock.vst3/bin.dll', data: body }]));
    return z;
  };
  const v1 = await installPackage({ zipPath: await mk('v1.zip', 'v1'), targetDir: target, stagingDir: path.join(dir, 's1') });
  const bundle = v1.files[0];
  const realRename = fs.rename;
  fs.rename = async (from, to) => {
    if (path.resolve(from) === path.resolve(bundle)) { const e = new Error('busy'); e.code = 'EBUSY'; throw e; }
    return realRename(from, to);
  };
  try {
    await assert.rejects(
      installPackage({ zipPath: await mk('v2.zip', 'v2'), targetDir: target, stagingDir: path.join(dir, 's2') }),
      (e) => e.code === 'BUSY',
    );
    assert.equal(await fs.readFile(path.join(bundle, 'bin.dll'), 'utf8'), 'v1');
    await assert.rejects(uninstallFiles({ files: [bundle], targetDir: target }), (e) => e.code === 'BUSY');
    assert.equal(await fs.readFile(path.join(bundle, 'bin.dll'), 'utf8'), 'v1');
  } finally {
    fs.rename = realRename;
  }
  await installPackage({ zipPath: await mk('v3.zip', 'v3'), targetDir: target, stagingDir: path.join(dir, 's3') });
  assert.equal(await fs.readFile(path.join(bundle, 'bin.dll'), 'utf8'), 'v3');
  assert.deepEqual((await fs.readdir(target)).sort(), ['Lock.vst3']);
  await fs.rm(dir, { recursive: true, force: true });
});

test('catálogo: contenido de la ficha (destacados, galería, specs, novedades, vídeo claro/oscuro)', () => {
  const dl = { win32: { url: 'https://x.com/a.zip', sha256: 'a'.repeat(64), size: 10 } };
  const long = 'x'.repeat(500);
  const { catalog, errors } = validateCatalog({
    schema: 1,
    plugins: [{
      id: 'ficha', name: 'Ficha', version: '1.2.3', downloads: dl,
      headline: 'Una *prueba* <b>segura</b>', summary: long,
      highlights: [
        { icon: 'phase', title: 'Uno', text: 'Texto uno' },
        { icon: 'desconocido', title: 'Dos', text: 'Texto dos' },
        { title: '', text: 'sin título' },
        'no es objeto',
      ],
      gallery: [
        { src: 'https://x.com/a.jpg', caption: 'A' },
        { src: 'https://x.com/b.gif', caption: 'formato no permitido' },
        { src: 'javascript:alert(1)//.png', caption: 'esquema no permitido' },
        { src: 'http://evil.com/c.png', caption: 'http no loopback' },
      ],
      specs: [['Formato', 'VST3'], ['solo uno'], ['', 'sin clave'], ['k', long]],
      changelog: [
        { version: '1.2.3', date: '2026-11-26', items: ['Uno', '', 'Dos'] },
        { version: 'mal', items: ['x'] },
        { version: '1.0.0', items: [] },
      ],
      media: { video: 'https://x.com/v.mp4', poster: 'https://x.com/p.jpg', videoDark: 'https://x.com/d.mp4', posterDark: 'https://x.com/d.exe' },
      demos: { before: 'https://x.com/a.wav', after: 'https://x.com/b.wav' },
    }],
  });
  assert.deepEqual(errors, []);
  const p = catalog.plugins[0];
  assert.equal(p.headline, 'Una *prueba* <b>segura</b>', 'texto plano: el renderer escapa');
  assert.equal(p.summary.length, 400);
  assert.deepEqual(p.highlights.map((h) => h.icon), ['phase', 'bands']);
  assert.deepEqual(p.gallery.map((g) => g.caption), ['A']);
  assert.deepEqual(p.specs, [['Formato', 'VST3'], ['k', 'x'.repeat(80)]]);
  assert.deepEqual(p.changelog, [{ version: '1.2.3', date: '2026-11-26', items: ['Uno', 'Dos'] }]);
  assert.deepEqual(p.media, { video: 'https://x.com/v.mp4', poster: 'https://x.com/p.jpg' }, 'variante oscura incompleta descartada');
  assert.equal('demos' in p, false, 'la demo de audio ya no se publica');
});

// ---------- Autoactualización del launcher ----------
const launcherUpdate = require('../src/main/core/launcher-update');
const dist = require('../src/main/core/distribution');

test('launcher.json: valida el manifiesto y compara versiones', () => {
  const ok = { schema: 1, version: '0.3.0', notes: ['Uno', 2, 'x'.repeat(300)], downloads: { win32: { url: 'https://github.com/a/b/releases/download/launcher-v0.3.0/z.zip', sha256: 'A'.repeat(64), size: 10 } } };
  const m = launcherUpdate.parseManifest(ok);
  assert.equal(m.version, '0.3.0');
  assert.deepEqual(m.notes.map((n) => n.length), [3, 200]);
  assert.equal(m.win32.sha256, 'a'.repeat(64));
  assert.equal(launcherUpdate.isNewer(m, '0.2.0'), true);
  assert.equal(launcherUpdate.isNewer(m, '0.3.0'), false);
  assert.equal(launcherUpdate.isNewer(m, '1.0.0'), false);
  for (const bad of [null, { ...ok, schema: 2 }, { ...ok, version: '1.0' }, { ...ok, downloads: { win32: { ...ok.downloads.win32, url: 'http://evil.com/z.zip' } } }, { ...ok, downloads: { win32: { ...ok.downloads.win32, sha256: 'x' } } }]) {
    assert.equal(launcherUpdate.parseManifest(bad), null);
  }
});

test('launcher: descarga verificada y prepara la versión nueva junto a la instalación', async () => {
  const dir = await tmp();
  const appRoot = path.join(dir, 'ZUENA Plugins');
  await fs.mkdir(appRoot, { recursive: true });
  await fs.writeFile(path.join(appRoot, 'ZUENA Plugins.exe'), 'viejo');
  const zip = createZip([
    { name: 'ZUENA Plugins/' },
    { name: 'ZUENA Plugins/ZUENA Plugins.exe', data: 'nuevo' },
    { name: 'ZUENA Plugins/resources/app.asar', data: 'asar' },
  ]);
  const srv = await serve({ '/z.zip': { body: zip } });
  try {
    const manifest = launcherUpdate.parseManifest({ schema: 1, version: '9.9.9', downloads: { win32: { url: `${srv.base}/z.zip`, sha256: sha(zip), size: zip.length } } });
    const staged = await launcherUpdate.prepareUpdate({ manifest, appRoot, exeName: 'ZUENA Plugins.exe', workDir: path.join(dir, 'work') });
    assert.equal(staged, `${appRoot}.nuevo`);
    assert.equal(await fs.readFile(path.join(staged, 'ZUENA Plugins.exe'), 'utf8'), 'nuevo');
    assert.equal(await fs.readFile(path.join(staged, 'resources', 'app.asar'), 'utf8'), 'asar');
    assert.equal(await fs.readFile(path.join(appRoot, 'ZUENA Plugins.exe'), 'utf8'), 'viejo', 'la instalación actual no se toca hasta cerrar');
    const bad = { ...manifest, win32: { ...manifest.win32, sha256: '0'.repeat(64) } };
    await assert.rejects(launcherUpdate.prepareUpdate({ manifest: bad, appRoot, exeName: 'ZUENA Plugins.exe', workDir: path.join(dir, 'work2') }), { code: 'CHECKSUM' });
  } finally {
    srv.close();
    await fs.rm(dir, { recursive: true, force: true });
  }
});

test('distribución: carpeta instalada y detección de copia portátil', () => {
  const env = { LOCALAPPDATA: 'C:\\Users\\X\\AppData\\Local' };
  assert.equal(dist.installRoot(env), path.join(env.LOCALAPPDATA, 'Programs', 'ZUENA Plugins'));
  assert.equal(dist.isInstalledCopy(path.join(env.LOCALAPPDATA, 'Programs', 'ZUENA Plugins', 'ZUENA Plugins.exe'), env), true);
  assert.equal(dist.isInstalledCopy('C:\\Users\\X\\Downloads\\ZUENA Plugins\\ZUENA Plugins.exe', env), false);
  assert.match(dist.OFFICIAL_CATALOG_URL, /^https:\/\/raw\.githubusercontent\.com\/kiannnx\/zuena-plugins-releases\/main\/catalog\.json$/);
});

test('menú Inicio: crea o corrige el acceso directo con el AppUserModelID (avisos de Windows)', { skip: process.platform !== 'win32' }, () => {
  const links = new Map();
  const shell = {
    readShortcutLink: (p) => { if (!links.has(p)) throw new Error('no existe'); return links.get(p); },
    writeShortcutLink: (p, _op, l) => { links.set(p, l); return true; },
  };
  const env = { APPDATA: 'C:\\Users\\x\\AppData\\Roaming', USERPROFILE: 'C:\\Users\\x' };
  const exePath = 'C:\\Users\\x\\AppData\\Local\\Programs\\ZUENA Plugins\\ZUENA Plugins.exe';
  assert.equal(dist.ensureStartMenuShortcut({ shell, exePath, env }), 'created');
  const [[where, link]] = [...links];
  assert.match(where, /Start Menu\\Programs\\ZUENA Plugins\.lnk$/);
  assert.equal(link.appUserModelId, dist.APP_USER_MODEL_ID);
  assert.equal(dist.ensureStartMenuShortcut({ shell, exePath, env }), 'ok', 'no reescribe si ya está bien');
  links.set(where, { ...link, appUserModelId: '' });
  assert.equal(dist.ensureStartMenuShortcut({ shell, exePath, env }), 'updated');
  assert.equal(links.get(where).appUserModelId, dist.APP_USER_MODEL_ID);
});

// ---------- Estadísticas anónimas ----------
const stats = require('../src/main/core/stats');

test('stats: el ping solo lleva campos acotados y descarta plugins o versiones inválidos', () => {
  const id = stats.newInstallId();
  assert.ok(stats.isInstallId(id));
  const ping = stats.buildPing({
    installId: id, appVersion: '0.2.3', platform: 'win32', osRelease: '10.0.26300', locale: 'es',
    installed: { 'zuena-eq': { version: '0.3.2', files: ['C:\ruta\privada'] }, 'MAL ID': { version: '1.0.0' }, 'zuena-luma': { version: 'x' } },
  });
  assert.deepEqual(ping, { p_install_id: id, p_launcher_version: '0.2.3', p_os: 'win32-10.0', p_locale: 'es', p_plugins: { 'zuena-eq': '0.3.2' } });
  assert.ok(!JSON.stringify(ping).includes('privada'), 'no se envían rutas');
  assert.throws(() => stats.buildPing({ installId: 'no-es-uuid', appVersion: '0.2.3' }));
});

test('stats: sendPing y forget llaman a las funciones del servidor con la clave publicable', async () => {
  const calls = [];
  const s = http.createServer((req, res) => {
    let body = '';
    req.on('data', (c) => { body += c; });
    req.on('end', () => { calls.push({ url: req.url, key: req.headers.apikey, body: JSON.parse(body) }); res.writeHead(204); res.end(); });
  });
  await new Promise((r) => s.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${s.address().port}/rpc`;
  try {
    const id = stats.newInstallId();
    await stats.sendPing(stats.buildPing({ installId: id, appVersion: '0.2.3', platform: 'win32', osRelease: '10.0.1', installed: {} }), { base });
    await stats.forget(id, { base });
    assert.deepEqual(calls.map((c) => c.url), ['/rpc/zuena_plugins_ping', '/rpc/zuena_plugins_forget']);
    assert.ok(calls.every((c) => c.key.startsWith('sb_publishable_')));
    assert.equal(calls[1].body.p_install_id, id);
  } finally {
    s.close();
  }
});

test('stats: un error del servidor se informa (y el launcher lo reintenta después)', async () => {
  const s = http.createServer((_q, res) => { res.writeHead(500); res.end(); });
  await new Promise((r) => s.listen(0, '127.0.0.1', r));
  try {
    await assert.rejects(stats.forget(stats.newInstallId(), { base: `http://127.0.0.1:${s.address().port}/rpc` }), /500/);
  } finally {
    s.close();
  }
});

test('EB-30: catálogo publicado, manifiesto y página de descarga apuntan al dueño actual del repositorio', async () => {
  const dist = require('../src/main/core/distribution');
  const root = path.join(__dirname, '..', '..');
  const files = [
    path.join(root, 'publish', 'zuena-plugins-releases', 'catalog.json'),
    path.join(root, 'publish', 'zuena-plugins-releases', 'launcher.json'),
    path.join(root, 'web', 'descarga', 'index.html'),
  ];
  let checked = 0;
  for (const f of files) {
    let text;
    try { text = await fs.readFile(f, 'utf8'); } catch { continue; } // sin copia local del repo público
    // Solo las direcciones del repositorio de publicación (el perfil del autor es otro enlace).
    const urls = (text.match(/https:\/\/(?:github\.com|raw\.githubusercontent\.com)\/[^\s"')]+/g) ?? [])
      .filter((u) => u.includes('zuena-plugins-releases'));
    for (const u of urls) {
      assert.ok(u.includes(`/${dist.RELEASES_REPO}/`) || u.endsWith(`/${dist.RELEASES_REPO}`), `${path.basename(f)}: ${u} no usa ${dist.RELEASES_REPO}`);
      checked += 1;
    }
  }
  assert.ok(checked > 0, 'no se encontró ninguna dirección para comprobar');
});

test('EB-32: la página de descarga pide su CSS y su JS con versión (sin mezclar caché vieja con HTML nuevo)', async () => {
  const html = await fs.readFile(path.join(__dirname, '..', '..', 'web', 'descarga', 'index.html'), 'utf8');
  assert.match(html, /href="site\.css\?v=\d+"/, 'site.css sin ?v=');
  assert.match(html, /src="site\.js\?v=\d+"/, 'site.js sin ?v=');
});

// ---------- Desinstalación y «Aplicaciones instaladas» ----------
const uninstallCore = require('../src/main/core/uninstall');

test('Aplicaciones instaladas: entrada completa y clave redirigible para pruebas', () => {
  const v = Object.fromEntries(uninstallCore.entryValues({ installDir: 'C:\\X\\ZUENA Plugins', version: '1.2.3', sizeKb: 2048 })
    .map(([n, t, d]) => [n, { t, d }]));
  assert.equal(v.DisplayName.d, 'ZUENA Plugins');
  assert.equal(v.DisplayVersion.d, '1.2.3');
  assert.equal(v.UninstallString.d, '"C:\\X\\ZUENA Plugins\\ZUENA Plugins.exe" --uninstall');
  assert.equal(v.DisplayIcon.d, 'C:\\X\\ZUENA Plugins\\ZUENA Plugins.ico');
  assert.equal(v.NoModify.t, 'REG_DWORD');
  assert.equal(v.EstimatedSize.d, '2048');
  assert.match(uninstallCore.uninstallKey({}), /^HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ZUENA Plugins$/);
  assert.equal(uninstallCore.uninstallKey({ FREEVST_UNINSTALL_KEY: 'Software\\T' }), 'HKCU\\Software\\T');
});

test('Desinstalar: el script espera al proceso y borra solo la carpeta de la app', async () => {
  const script = uninstallCore.removalScript({ pid: 4321, installDir: 'C:\\U\\AppData\\Local\\Programs\\ZUENA Plugins' });
  assert.match(script, /PID eq 4321/);
  assert.match(script, /rmdir \/s \/q "C:\\U\\AppData\\Local\\Programs\\ZUENA Plugins"/);
  assert.equal((script.match(/rmdir/g) || []).length, 1);
  await assert.rejects(uninstallCore.uninstall({ installDir: 'C:\\Otra', expectedDir: 'C:\\U\\ZUENA Plugins', pid: 1, shortcuts: [] }),
    /Solo se puede desinstalar la copia instalada/);
});
