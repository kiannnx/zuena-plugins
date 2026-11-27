'use strict';
// E2E: abre el launcher real (Electron) en MODO DEMO con datos aislados y ejercita
// cada control de la UI (anti EB-01). Guarda capturas en e2e/shots/.
// Uso: npm run e2e   (o  node e2e/run.js <ruta-al-exe-empaquetado>)

const { _electron: electron } = require('playwright-core');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const SHOTS = path.join(__dirname, 'shots');
const exe = process.argv[2] ? path.resolve(process.argv[2]) : undefined;

const steps = [];
async function step(name, fn) {
  const t = Date.now();
  try {
    await fn();
    steps.push(`✔ ${name} (${Date.now() - t} ms)`);
    console.log(steps.at(-1));
  } catch (e) {
    console.log(`✖ ${name}\n  ${e.message}`);
    throw e;
  }
}

(async () => {
  const userData = await fs.mkdtemp(path.join(os.tmpdir(), 'zuena-e2e-'));
  await fs.mkdir(SHOTS, { recursive: true });
  const app = await electron.launch({
    ...(exe ? { executablePath: exe, args: ['--demo'] } : { args: [ROOT, '--demo'] }),
    env: { ...process.env, FREEVST_USER_DATA: userData, FREEVST_ONBOARDED: '1' },
  });
  const errors = [];
  const win = await app.firstWindow();
  win.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  win.on('pageerror', (e) => errors.push(e.message));
  const shot = (n) => win.screenshot({ path: path.join(SHOTS, `${n}.png`) });
  const sandbox = path.join(userData, 'demo-VST3');
  const exists = (p) => fs.access(p).then(() => true, () => false);
  const tile = (id) => win.locator(`.tile[data-section="${id}"]`);
  const count = (t) => win.waitForFunction((x) => document.querySelector('.wall__count')?.textContent === x, t, { timeout: 5000 });
  const heroBtn = (t) => win.locator('.hero__inner:not(.is-leaving) .pill', { hasText: t });
  const pickedDir = path.join(userData, 'carpeta-elegida');
  // Intercepta Explorador y diálogos nativos: registra la llamada en vez de abrir ventanas reales.
  const shellCalls = () => app.evaluate(() => globalThis.__shellCalls);

  try {
    await app.evaluate(({ shell, dialog }, picked) => {
      globalThis.__shellCalls = [];
      shell.openPath = async (p) => { globalThis.__shellCalls.push(['openPath', p]); return ''; };
      shell.showItemInFolder = (p) => { globalThis.__shellCalls.push(['showItemInFolder', p]); };
      dialog.showOpenDialog = async () => ({ canceled: false, filePaths: [picked] });
    }, pickedDir);
    await win.setViewportSize({ width: 1360, height: 820 }).catch(() => {});
    await step('arranca y carga catálogo demo', async () => {
      await win.waitForSelector('.tile', { timeout: 15000 });
      assert.equal(await win.locator('.tile').count(), 5);
      await win.locator('.hero__inner:not(.is-leaving) .hero__title', { hasText: 'VoxTransit' }).waitFor({ timeout: 10000 });
      assert.equal(await win.locator('#demo-badge').isVisible(), true);
      await win.waitForTimeout(1200);
      const box = await win.locator('.scene.is-active .device').boundingBox();
      assert.ok(box && box.width > 200, 'EB-11: el arte 3D del destacado es visible');
      await shot('01-destacado');
    });

    await step('Destacado: Avisarme ↔ Te vamos a avisar', async () => {
      await heroBtn('Avisarme cuando salga').click();
      await heroBtn('Te vamos a avisar').waitFor();
      await heroBtn('Te vamos a avisar').click();
      await heroBtn('Avisarme cuando salga').waitFor();
    });

    await step('Destacado: Ver detalles y clic en dispositivo abren la ficha', async () => {
      await heroBtn('Ver detalles').click();
      await win.locator('#sheet.is-open .sheet__name', { hasText: 'VoxTransit' }).waitFor();
      await win.keyboard.press('Escape');
      await win.locator('#sheet').waitFor({ state: 'hidden' });
      await win.locator('.scene.is-active .device').click({ force: true }); // flota en bucle: nunca "estable"
      await win.locator('#sheet.is-open').waitFor();
      await win.locator('.sheet__close').click();
      await win.locator('#sheet').waitFor({ state: 'hidden' });
    });

    await step('Catálogo: muro, filtros y navegación', async () => {
      await tile('catalog').click();
      await win.locator('.hero__inner:not(.is-leaving) .hero__title', { hasText: 'Todo gratis' }).waitFor();
      await win.waitForTimeout(900);
      await count('1 / 5');
      await shot('02-catalogo');
      await win.locator('.wall__nav--r').click();
      await count('2 / 5');
      await win.locator('.wall__nav--l').click();
      await count('1 / 5');
      await heroBtn('Próximamente').click();
      await count('1 / 1');
      await heroBtn('Disponibles').click();
      await count('1 / 4');
      await heroBtn('Instalados').click();
      await win.locator('.wall .empty').waitFor();
      await win.locator('.wall .empty .pill').click();
      await count('1 / 5');
      await heroBtn('Todos').click();
    });

    await step('Búsqueda filtra y limpia', async () => {
      await win.keyboard.press('Control+f');
      await win.locator('#search-pop.is-open').waitFor();
      await win.locator('#search-input').fill('tape');
      await count('1 / 1');
      await win.locator('#search-clear').click();
      await count('1 / 5');
      await win.keyboard.press('Escape');
      await win.locator('#btn-search').click();
      await win.locator('#search-input').fill('zzz');
      await heroBtn('Limpiar búsqueda').click();
      await count('1 / 5');
      await win.mouse.click(700, 400);
    });

    await step('Instalar paquete demo desde la ficha (descarga → SHA-256 → instalación)', async () => {
      await win.locator('.wall__nav--r').click();
      await win.waitForTimeout(800);
      await win.locator('.pcard.is-center').click();
      await win.locator('#sheet.is-open .sheet__name', { hasText: 'Demo Air Riser' }).waitFor();
      await win.waitForTimeout(500);
      await shot('03-ficha');
      await win.locator('.sheet__foot .pill', { hasText: 'Instalar gratis' }).click();
      await win.locator('.sheet__foot .chip--busy').waitFor({ timeout: 5000 });
      await win.waitForTimeout(700);
      await shot('04-ficha-descargando');
      await win.locator('.sheet__foot .chip--ok', { hasText: 'Instalado' }).waitFor({ timeout: 30000 });
      assert.ok(await exists(path.join(sandbox, 'Demo Air Riser.vst3', 'Contents', 'Resources', 'LEEME.txt')), 'archivo instalado en la carpeta demo');
    });

    await step('Ficha sin vídeo: carátula, secciones y sin demo de audio', async () => {
      const sheet = win.locator('#sheet.is-open');
      await sheet.locator('.stage__art').waitFor();
      assert.equal(await sheet.locator('.stage__video').count(), 0);
      assert.equal(await sheet.locator('.stage__toggle').count(), 0);
      for (const h of ['Lo esencial', 'Especificaciones', 'Novedades', 'Acerca de']) {
        await sheet.locator('.pd__h', { hasText: h }).waitFor();
      }
      assert.equal(await sheet.locator('.shots').count(), 0, 'sin galería no hay sección vacía');
      assert.equal(await sheet.locator('.ab, audio').count(), 0, 'la demo de audio ya no existe');
      assert.equal(await sheet.locator('.pd__headline em').textContent(), 'recorre');
      await win.keyboard.press('Escape');
      await win.locator('#sheet').waitFor({ state: 'hidden' });
    });

    await step('Descargas: fila completada + acción Mostrar + limpiar', async () => {
      await tile('downloads').click();
      await win.locator('.scene.is-active .row', { hasText: 'Demo Air Riser' }).waitFor();
      await win.waitForTimeout(900);
      await shot('05-descargas');
      await win.locator('.scene.is-active .row .pill', { hasText: 'Mostrar' }).click();
      await win.waitForTimeout(200);
      assert.ok((await shellCalls()).some(([fn, p]) => fn === 'showItemInFolder' && p.endsWith('Demo Air Riser.vst3')));
      await heroBtn('Ir al catálogo').click();
      await win.locator('.tile.is-active[data-section="catalog"]').waitFor();
      await tile('downloads').click();
    });

    await step('Paquete alterado: rechazado por SHA-256, nada instalado', async () => {
      await tile('catalog').click();
      while (await win.locator('.wall__nav--r').isEnabled()) await win.locator('.wall__nav--r').click();
      await win.waitForTimeout(800);
      await win.locator('.pcard.is-center').click();
      await win.locator('#sheet.is-open .sheet__name', { hasText: 'archivo alterado' }).waitFor();
      await win.locator('.sheet__foot .pill', { hasText: 'Instalar gratis' }).click();
      await win.locator('.toast', { hasText: 'SHA-256' }).waitFor({ timeout: 15000 });
      await win.keyboard.press('Escape');
      assert.equal(await exists(path.join(sandbox, 'Demo Corrupto.vst3')), false);
      await tile('downloads').click();
      await win.locator('.scene.is-active .row', { hasText: 'SHA-256' }).waitFor();
      await win.locator('.scene.is-active .row .pill', { hasText: 'Reintentar' }).first().click();
      await win.waitForFunction(() => [...document.querySelectorAll('.scene.is-active .row')].filter((r) => r.textContent.includes('SHA-256')).length === 2, null, { timeout: 15000 });
      await heroBtn('Limpiar finalizadas').click();
      await win.locator('.scene.is-active .empty').waitFor();
    });

    await step('Cancelar una descarga en curso', async () => {
      await tile('catalog').click();
      await win.locator('.wall__nav--l').click();
      await win.locator('.wall__nav--l').click();
      await win.waitForTimeout(800);
      const name = await win.locator('.pcard.is-center .pcard__name').textContent();
      assert.equal(name, 'Demo Tape Stop');
      await win.locator('.pcard.is-center').click();
      await win.locator('.sheet__foot .pill', { hasText: 'Instalar gratis' }).click();
      await win.locator('.sheet__foot .pill', { hasText: 'Cancelar descarga' }).click();
      await win.locator('.toast', { hasText: 'cancelada' }).waitFor({ timeout: 10000 });
      await win.keyboard.press('Escape');
      assert.equal(await exists(path.join(sandbox, 'Demo Tape Stop.vst3')), false);
    });

    await step('Descargas: Cancelar todas', async () => {
      await tile('catalog').click();
      await win.locator('.wall__nav--r').click();
      await win.waitForTimeout(800);
      assert.equal(await win.locator('.pcard.is-center .pcard__name').textContent(), 'Demo Dub Echo');
      await win.locator('.pcard.is-center').click();
      await win.locator('.sheet__foot .pill', { hasText: 'Instalar gratis' }).click();
      await win.locator('.sheet__foot .chip--busy').waitFor();
      await win.keyboard.press('Escape');
      await tile('downloads').click();
      await heroBtn('Cancelar todas').click();
      await win.locator('.toast', { hasText: 'Descarga de Demo Dub Echo cancelada' }).waitFor({ timeout: 10000 });
      assert.equal(await exists(path.join(sandbox, 'Demo Dub Echo.vst3')), false);
    });

    await step('Instalados: lista, detalles y desinstalar con confirmación', async () => {
      await tile('installed').click();
      await win.locator('.scene.is-active .row', { hasText: 'Demo Air Riser' }).waitFor();
      await win.waitForTimeout(900);
      await shot('06-instalados');
      await win.locator('.scene.is-active .row [data-a="reveal"]').click();
      await heroBtn('Abrir carpeta VST3').click();
      await heroBtn('Buscar actualizaciones').click();
      await win.locator('.toast', { hasText: 'Catálogo actualizado' }).waitFor();
      await win.waitForTimeout(200);
      const calls = await shellCalls();
      assert.ok(calls.some(([fn, p]) => fn === 'openPath' && p === sandbox), 'Abrir carpeta VST3');
      assert.ok(calls.filter(([fn]) => fn === 'showItemInFolder').length >= 2, 'Mostrar en carpeta');
      await win.locator('.scene.is-active .row [data-a="info"]').click();
      await win.locator('#sheet.is-open').waitFor();
      await win.keyboard.press('Escape');
      await win.locator('.scene.is-active .row [data-a="remove"]').click();
      await win.locator('#modal.is-open').waitFor();
      await win.locator('#modal [data-m="no"]').click();
      assert.ok(await exists(path.join(sandbox, 'Demo Air Riser.vst3')));
      await win.locator('.scene.is-active .row [data-a="remove"]').click();
      await win.locator('#modal [data-m="yes"]').click();
      await win.locator('.scene.is-active .empty').waitFor({ timeout: 10000 });
      assert.equal(await exists(path.join(sandbox, 'Demo Air Riser.vst3')), false);
      await win.locator('.scene.is-active .empty .pill', { hasText: 'Explorar catálogo' }).click();
      await win.locator('.hero__inner:not(.is-leaving) .hero__title', { hasText: 'Todo gratis' }).waitFor();
    });

    await step('Ajustes, apariencia oscura, ayuda y notificaciones', async () => {
      await tile('settings').click();
      await win.locator('.scene.is-active .field__value').first().waitFor();
      assert.ok((await win.locator('.scene.is-active .field__value').first().textContent()).includes('demo-VST3'));
      await win.waitForTimeout(900);
      await shot('07-ajustes');
      await win.locator('#btn-theme').click();
      await win.waitForFunction(() => document.documentElement.dataset.scheme === 'dark');
      await shot('07b-modo-oscuro');
      await win.locator('.scene.is-active [data-theme="light"]').click();
      await win.waitForFunction(() => document.documentElement.dataset.scheme === 'light');
      await win.locator('.scene.is-active [data-theme="dark"]').click();
      await win.waitForFunction(() => document.documentElement.dataset.scheme === 'dark');
      await win.locator('#btn-theme').click();
      await win.waitForFunction(() => document.documentElement.dataset.scheme === 'light');
      await win.locator('.scene.is-active [data-a="choose"]').click();
      await win.locator('.scene.is-active .field__value', { hasText: 'carpeta-elegida' }).waitFor();
      await win.locator('.scene.is-active [data-a="open"]').click();
      await win.waitForTimeout(200);
      assert.ok((await shellCalls()).some(([fn, p]) => fn === 'openPath' && p === pickedDir), 'Abrir carpeta elegida');
      await win.locator('.scene.is-active [data-a="reset"]').click();
      await win.locator('.scene.is-active .field__value', { hasText: 'demo-VST3' }).waitFor();
      await win.locator('.toast', { hasText: 'restablecida' }).waitFor();
      await heroBtn('Recargar catálogo').click();
      await win.locator('.toast', { hasText: 'Catálogo actualizado' }).waitFor();
      await heroBtn('Guía de instalación').click();
      await win.locator('#modal.is-open .modal__title', { hasText: 'Guía' }).waitFor();
      await win.locator('#modal.is-open [data-help="unsigned"]', { hasText: 'Si Windows bloquea el plugin' }).waitFor();
      await shot('08-ayuda');
      await win.locator('#modal [data-m="ok"]').click();
      await win.locator('#btn-help').click();
      await win.locator('#modal [data-m="settings"]').click();
      await win.locator('#btn-notif').click();
      await win.locator('#notif-pop.is-open .notif').first().waitFor();
      await shot('09-notificaciones');
      await win.locator('#notif-read').click();
      await win.locator('#notif-badge').waitFor({ state: 'hidden' });
      await win.locator('#notif-clear').click();
      await win.locator('.notif-empty').waitFor();
      await win.keyboard.press('Escape');
    });

    await step('Teclado ← → recorre el dock', async () => {
      await win.locator('body').click({ position: { x: 300, y: 300 } });
      await win.keyboard.press('ArrowRight');
      await win.locator('.tile.is-active[data-section="featured"]').waitFor();
      await win.keyboard.press('ArrowLeft');
      await win.locator('.tile.is-active[data-section="settings"]').waitFor();
    });

    await step('Controles de ventana', async () => {
      await win.locator('#win-max').click();
      await win.waitForFunction(() => document.querySelector('#win-max').title === 'Restaurar');
      assert.equal(await app.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows()[0].isMaximized()), true);
      await win.locator('#win-max').click();
      await win.waitForFunction(() => document.querySelector('#win-max').title === 'Maximizar');
      await win.locator('#win-min').click();
      await win.waitForTimeout(300);
      assert.equal(await app.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows()[0].isMinimized()), true);
      await app.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows()[0].restore());
    });

    await step('Sin errores de consola ni desplazamientos de layout', async () => {
      assert.deepEqual(errors, []);
      const scroll = await win.evaluate(() => [document.scrollingElement.scrollLeft, document.querySelector('.stage').scrollLeft, document.querySelector('.scenes').scrollLeft]);
      assert.deepEqual(scroll, [0, 0, 0], 'EB-10: la UI no se desplaza');
    });

    await step('Cerrar cierra la app', async () => {
      const closed = app.waitForEvent('close');
      await win.locator('#win-close').click();
      await closed;
    });
    console.log(`\nE2E OK · ${steps.length} pasos · capturas en ${SHOTS}`);
  } catch (e) {
    await shot('zz-fallo').catch(() => {});
    console.log('Errores de consola:', errors);
    await app.close().catch(() => {});
    process.exitCode = 1;
  } finally {
    await fs.rm(userData, { recursive: true, force: true }).catch(() => {});
  }
})();
