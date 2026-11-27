'use strict';
// E2E EN LÍNEA (necesita internet): el launcher con la conexión permitida lee el catálogo
// oficial de GitHub, descarga los plugins desde las releases públicas y no ofrece una
// actualización del launcher cuando la versión publicada es la misma que la instalada.
// Perfil y carpeta VST3 aislados: no toca la configuración ni los plugins del usuario.
// Uso:  node e2e/online.js [ruta al .exe empaquetado]

const { _electron: electron } = require('playwright-core');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');

(async () => {
  const userData = await fs.mkdtemp(path.join(os.tmpdir(), 'zuena-e2e-online-'));
  const testVstDir = path.join(userData, 'VST3-online');
  await fs.mkdir(testVstDir, { recursive: true });
  // Como si el usuario hubiera terminado la guía de inicio permitiendo la conexión.
  await fs.writeFile(path.join(userData, 'settings.json'), JSON.stringify({
    onboarded: true, online: true, desktopNotify: false, installDir: testVstDir,
  }));

  const app = await electron.launch({
    ...(process.argv[2] ? { executablePath: path.resolve(process.argv[2]), args: [] } : { args: [path.join(__dirname, '..')] }),
    env: { ...process.env, FREEVST_USER_DATA: userData },
  });
  const errors = [];
  const win = await app.firstWindow();
  win.on('pageerror', (e) => errors.push(e.message));
  win.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  const ok = (m) => console.log(`✔ ${m}`);
  const state = () => win.evaluate(() => window.hub.getState());
  try {
    await win.locator('.hero__inner:not(.is-leaving) .hero__title').waitFor({ timeout: 20000 });
    // Sondeo desde Node: waitForFunction no espera promesas (una función async siempre «cumple»).
    for (let i = 0; (await state()).catalogSource !== 'remote'; i += 1) {
      if (i > 20) throw new Error(`fuente del catálogo: ${(await state()).catalogSource}`);
      await win.waitForTimeout(1000);
    }
    ok('catálogo oficial en línea (GitHub)');

    await win.locator('.tile[data-section="settings"]').click();
    await win.locator('.scene.is-active', { hasText: 'Catálogo oficial en línea' }).first().waitFor();
    ok('Ajustes muestra la fuente «Catálogo oficial en línea»');

    // El launcher consulta su manifiesto a los 4 s: con la misma versión no debe ofrecer nada.
    await win.waitForTimeout(6000);
    const st = await state();
    assert.equal(st.launcher.update ?? null, null, `no hay actualización del launcher (v${st.appVersion})`);
    assert.equal(await win.locator('[data-a="launcher-download"]').count(), 0);
    ok(`sin actualización del launcher: v${st.appVersion} es la publicada`);

    // ZUENA Luma desde su ficha: vídeo y descarga desde las releases de GitHub.
    await win.locator('.tile[data-section="catalog"]').click();
    await win.locator('.pcard[data-id="zuena-luma"]').click();
    await win.waitForTimeout(700);
    await win.locator('.pcard[data-id="zuena-luma"]').click();
    const sheet = win.locator('#sheet.is-open');
    await sheet.locator('.sheet__name', { hasText: 'ZUENA Luma' }).waitFor();
    await win.waitForFunction(() => {
      const v = document.querySelector('#sheet .stage__video');
      return v && /^https:\/\/github\.com\/.+\/releases\/download\//.test(v.currentSrc) && v.readyState >= 2;
    }, null, { timeout: 30000 });
    await win.waitForFunction(() => {
      const imgs = [...document.querySelectorAll('#sheet .shot img')];
      return imgs.length > 0 && imgs.every((i) => i.complete && i.naturalWidth > 0 && /^https:\/\/github\.com\//.test(i.currentSrc));
    }, null, { timeout: 30000 });
    const hd = await win.evaluate(() => [...document.querySelectorAll('#sheet .shot img')].map((i) => `${i.naturalWidth}×${i.naturalHeight}`));
    ok(`vídeo y capturas HD de la ficha cargados desde GitHub (${hd.join(', ')})`);
    await sheet.locator('.sheet__foot .pill', { hasText: 'Instalar gratis' }).click();
    await sheet.locator('.sheet__foot .chip--ok', { hasText: 'Instalado' }).waitFor({ timeout: 120000 });
    const luma = JSON.parse(await fs.readFile(path.join(testVstDir, 'ZUENA Luma.vst3', 'Contents', 'Resources', 'zuena-plugin.json'), 'utf8'));
    ok(`ZUENA Luma v${luma.version} descargado de GitHub, verificado e instalado`);
    await win.keyboard.press('Escape');
    await win.locator('#sheet').waitFor({ state: 'hidden' });

    // ZUENA EQ desde Destacado.
    await win.locator('.tile[data-section="featured"]').click();
    await win.locator('.hero__inner:not(.is-leaving) .hero__title', { hasText: 'ZUENA EQ' }).waitFor({ timeout: 15000 });
    await win.locator('.hero__inner:not(.is-leaving) .pill', { hasText: 'Instalar gratis' }).click();
    await win.locator('.hero__inner:not(.is-leaving) .chip--ok', { hasText: 'Instalado' }).waitFor({ timeout: 120000 });
    const eq = JSON.parse(await fs.readFile(path.join(testVstDir, 'ZUENA EQ.vst3', 'Contents', 'Resources', 'zuena-plugin.json'), 'utf8'));
    ok(`ZUENA EQ v${eq.version} descargado de GitHub, verificado e instalado`);

    const installed = (await state()).installed;
    assert.ok(Object.keys(installed).length >= 2, 'ambos figuran como instalados');
    assert.deepEqual(errors, [], `errores en consola: ${errors.join(' | ')}`);
    ok('sin errores en consola');
  } finally {
    await app.close().catch(() => {});
    await fs.rm(userData, { recursive: true, force: true }).catch(() => {});
  }
})().catch((e) => { console.error(`✘ ${e.message}`); process.exit(1); });
