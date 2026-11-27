'use strict';
// E2E de la GUÍA DE INICIO: aparece la primera vez, recorre los pasos (idioma y tema, conexión, avisos,
// carpeta), aplica el tema en vivo, guarda los permisos elegidos y no vuelve a aparecer al reabrir.
// Uso: node e2e/onboarding.js [ruta\a\ZUENA Plugins.exe]

const { _electron: electron } = require('playwright-core');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');

const launch = (userData) => electron.launch({
  ...(process.argv[2] ? { executablePath: path.resolve(process.argv[2]), args: [] } : { args: [path.join(__dirname, '..')] }),
  env: { ...process.env, FREEVST_USER_DATA: userData },
});
const closeApp = async (app) => {
  const proc = app.process();
  await Promise.race([app.close().catch(() => {}), new Promise((r) => setTimeout(r, 5000))]);
  if (proc.exitCode === null) proc.kill();
};

(async () => {
  const userData = await fs.mkdtemp(path.join(os.tmpdir(), 'zuena-e2e-onb-'));
  const ok = (m) => console.log(`✔ ${m}`);
  let app = await launch(userData);
  let win = await app.firstWindow();
  const errors = [];
  win.on('pageerror', (e) => errors.push(e.message));
  try {
    const ob = win.locator('.ob.is-open');
    await ob.waitFor({ timeout: 20000 });
    await ob.locator('.ob__title', { hasText: 'bienvenida' }).waitFor();
    await win.waitForTimeout(600);
    await win.screenshot({ path: path.join(__dirname, 'shots', '20-guia-bienvenida.png') });
    // El teclado global no navega por detrás mientras la guía está abierta
    await win.keyboard.press('ArrowRight');
    assert.equal(await win.locator('.tile[data-section="featured"].is-active').count(), 1);
    ok('la guía aparece al primer arranque y bloquea la navegación de fondo');

    await ob.locator('[data-ob="next"]').click(); // → idioma y apariencia
    await ob.locator('.ob__title', { hasText: 'tuyo' }).waitFor();
    assert.equal(await ob.locator('[data-lang="es"].is-active').count(), 1);
    assert.equal(await ob.locator('.ob-choice[disabled]', { hasText: 'English' }).count(), 1, 'inglés visible como próximamente, no como opción falsa');
    await ob.locator('[data-theme-pick="dark"]').click();
    await win.waitForFunction(() => document.documentElement.dataset.scheme === 'dark');
    await win.waitForTimeout(500);
    await win.screenshot({ path: path.join(__dirname, 'shots', '21-guia-apariencia-oscuro.png') });
    ok('tema oscuro aplicado en vivo desde la guía');

    await ob.locator('[data-ob="next"]').click(); // → conexión
    await ob.locator('.ob__title', { hasText: 'al día' }).waitFor();
    const online = ob.locator('.switch[data-k="online"]');
    assert.equal(await online.isChecked(), true, 'la conexión viene propuesta');
    await online.uncheck(); // en el test no se sale a internet
    await win.screenshot({ path: path.join(__dirname, 'shots', '22-guia-conexion.png') });
    await ob.locator('[data-ob="back"]').click();
    await ob.locator('.ob__title', { hasText: 'tuyo' }).waitFor();
    await ob.locator('[data-ob="next"]').click();
    assert.equal(await ob.locator('.switch[data-k="online"]').isChecked(), false, 'la elección se conserva al volver atrás');
    ok('permiso de conexión: se puede negar y la elección se conserva');

    await ob.locator('[data-ob="next"]').click(); // → avisos
    await ob.locator('.ob__title', { hasText: 'avisamos' }).waitFor();
    await ob.locator('[data-ob="next"]').click(); // → carpeta
    await ob.locator('.ob__title', { hasText: 'su lugar' }).waitFor();
    assert.match(await ob.locator('.ob-path').textContent(), /VST3$/);
    await ob.locator('[data-ob="next"]').click(); // → listo (sin paso de instalación: no es la app empaquetada)
    await ob.locator('.ob__title', { hasText: 'explorar' }).waitFor();
    assert.equal(await ob.locator('.ob__count').textContent(), '6 / 6');
    await ob.locator('[data-ob="next"]').click();
    await win.locator('.ob').waitFor({ state: 'detached' });
    ok('pasos completos: bienvenida → apariencia → conexión → avisos → carpeta → listo');

    const saved = JSON.parse(await fs.readFile(path.join(userData, 'settings.json'), 'utf8'));
    assert.equal(saved.onboarded, true);
    assert.equal(saved.online, false);
    assert.equal(saved.desktopNotify, true);
    assert.equal(saved.theme, 'dark');
    assert.equal(saved.language, 'es');
    ok('permisos y preferencias guardados (sin conexión, avisos sí, tema oscuro, español)');

    // Ajustes refleja lo elegido y permite volver a ver la guía
    await win.locator('.tile[data-section="settings"]').click();
    const set = win.locator('.scene.is-active');
    assert.equal(await set.locator('.switch[data-set="online"]').isChecked(), false);
    assert.equal(await set.locator('.switch[data-set="desktopNotify"]').isChecked(), true);
    await win.waitForTimeout(500);
    await win.screenshot({ path: path.join(__dirname, 'shots', '23-ajustes-conexion.png') });
    ok('Ajustes muestra conexión y avisos con lo elegido en la guía');

    assert.deepEqual(errors, []);
    await closeApp(app);
    app = await launch(userData);
    win = await app.firstWindow();
    await win.locator('.hero__inner:not(.is-leaving) .hero__title').waitFor({ timeout: 20000 });
    await win.waitForTimeout(1200);
    assert.equal(await win.locator('.ob').count(), 0, 'la guía no vuelve a aparecer');
    ok('al reabrir, la guía no vuelve a aparecer');
    await closeApp(app);
    console.log('\nE2E guía de inicio OK');
  } catch (e) {
    console.log(`✖ ${e.message}`);
    await win.screenshot({ path: path.join(__dirname, 'shots', 'zz-guia-fallo.png') }).catch(() => {});
    await closeApp(app);
    process.exitCode = 1;
  } finally {
    await fs.rm(userData, { recursive: true, force: true }).catch(() => {});
  }
})();
