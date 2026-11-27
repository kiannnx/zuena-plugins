'use strict';
// E2E de SINCRONIZACIÓN plugin ↔ launcher (EB-20): con un ZUENA EQ viejo en la carpeta VST3
// (copiado a mano, sin registro del launcher), el launcher lee la versión del bundle en disco,
// ofrece "Actualizar a v<catálogo>", avisa en notificaciones y deja instalada la versión nueva.
// Uso: node e2e/update.js [ruta\a\ZUENA Plugins.exe]

const { _electron: electron } = require('playwright-core');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { buildManifest, MANIFEST_REL, readBundleVersion } = require('../src/main/core/bundle-info');

(async () => {
  const catalog = JSON.parse(await fs.readFile(path.join(__dirname, '..', 'catalog', 'catalog.json'), 'utf8'));
  const latest = catalog.plugins.find((p) => p.id === 'zuena-eq').version;
  const [maj, min, pat] = latest.split('.').map(Number);
  const older = pat > 0 ? `${maj}.${min}.${pat - 1}` : min > 0 ? `${maj}.${min - 1}.0` : '0.0.1';

  const userData = await fs.mkdtemp(path.join(os.tmpdir(), 'zuena-e2e-update-'));
  const vstDir = path.join(userData, 'VST3');
  const bundle = path.join(vstDir, 'ZUENA EQ.vst3');
  await fs.mkdir(path.join(bundle, 'Contents', 'x86_64-win'), { recursive: true });
  await fs.mkdir(path.dirname(path.join(bundle, MANIFEST_REL)), { recursive: true });
  await fs.writeFile(path.join(bundle, 'Contents', 'x86_64-win', 'ZUENA EQ.vst3'), 'dll vieja');
  await fs.writeFile(path.join(bundle, MANIFEST_REL), buildManifest({ id: 'zuena-eq', version: older }));
  await fs.writeFile(path.join(userData, 'settings.json'), JSON.stringify({ installDir: vstDir }));

  const app = await electron.launch({
    ...(process.argv[2] ? { executablePath: path.resolve(process.argv[2]), args: [] } : { args: [path.join(__dirname, '..')] }),
    env: { ...process.env, FREEVST_USER_DATA: userData, FREEVST_ONBOARDED: '1' },
  });
  const errors = [];
  const win = await app.firstWindow();
  win.on('pageerror', (e) => errors.push(e.message));
  win.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  const ok = (m) => console.log(`✔ ${m}`);
  try {
    const hero = '.hero__inner:not(.is-leaving)';
    await win.locator(`${hero} .hero__title`, { hasText: 'ZUENA EQ' }).waitFor({ timeout: 15000 });
    await win.locator(`${hero} .pill--primary`, { hasText: `Actualizar a v${latest}` }).waitFor({ timeout: 10000 });
    ok(`detecta v${older} en disco y ofrece "Actualizar a v${latest}"`);

    await win.locator('.tile[data-section="installed"]').click();
    await win.locator('.scene.is-active .row__meta', { hasText: `v${older}` }).waitFor();
    await win.locator('.scene.is-active .chip--update', { hasText: `v${latest} disponible` }).waitFor();
    ok('Instalados muestra la versión en disco y la disponible');

    const notes = JSON.parse(await fs.readFile(path.join(userData, 'settings.json'), 'utf8')).notifications;
    assert.ok(notes.some((n) => n.text.includes(`${latest} disponible`) && n.text.includes(older)), 'falta la notificación de actualización');
    ok('notificación "disponible" registrada una vez');

    await win.locator('.scene.is-active [data-a="update"]').click();
    await win.locator('.scene.is-active .row__meta', { hasText: `v${latest}` }).waitFor({ timeout: 30000 });
    assert.equal(await readBundleVersion(bundle, 'zuena-eq'), latest);
    const dll = await fs.stat(path.join(bundle, 'Contents', 'x86_64-win', 'ZUENA EQ.vst3'));
    assert.ok(dll.size > 3_000_000, 'la DLL no se reemplazó');
    assert.deepEqual(await fs.readdir(vstDir), ['ZUENA EQ.vst3'], 'quedaron restos de la actualización');
    ok(`actualizado: el bundle en disco declara v${latest} y la DLL es la nueva`);

    await win.locator('.tile[data-section="featured"]').click();
    await win.locator(`${hero} .chip--ok`, { hasText: 'Instalado' }).waitFor();
    ok('Destacado pasa a "Instalado"');

    assert.deepEqual(errors, []);
    ok('sin errores de consola');
    await app.close();
    console.log('\nE2E actualización OK');
  } catch (e) {
    console.log(`✖ ${e.message}`);
    await win.screenshot({ path: path.join(__dirname, 'shots', 'zz-update-fallo.png') }).catch(() => {});
    await app.close().catch(() => {});
    process.exitCode = 1;
  } finally {
    await fs.rm(userData, { recursive: true, force: true }).catch(() => {});
  }
})();
