'use strict';
// E2E del MODO NORMAL: catálogo integrado con ZUENA EQ disponible, instalación real en
// carpeta temporal aislada, carpeta VST3 estándar y origen de catálogo configurable.

const { _electron: electron } = require('playwright-core');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');

(async () => {
  const userData = await fs.mkdtemp(path.join(os.tmpdir(), 'zuena-e2e-normal-'));
  const testVstDir = path.join(userData, 'VST3-normal');

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
    await app.evaluate(({ dialog }, picked) => {
      dialog.showOpenDialog = async () => ({ canceled: false, filePaths: [picked] });
    }, testVstDir);

    await win.locator('.hero__inner:not(.is-leaving) .hero__title', { hasText: 'ZUENA EQ' }).waitFor({ timeout: 15000 });
    // Cualquier acción: si el usuario ya tiene ZUENA EQ en su carpeta VST3 real, la principal es "Mostrar en carpeta".
    await win.locator('.hero__inner:not(.is-leaving) .pill').first().waitFor();
    assert.equal(await win.locator('#demo-badge').isVisible(), false);
    ok('arranca en modo normal con ZUENA EQ listo');

    // Cada plugin dibuja su tipo: el destacado (EQ) muestra una curva de EQ con nodos.
    await win.locator('.feature3d[data-kind="eq"] .device--eq .vz--eq').waitFor();
    assert.equal(await win.locator('.device--eq .vz-node').count(), 4);
    assert.deepEqual(await win.locator('.device--eq .dknob__label').allTextContents(), ['Frecuencia', 'Ganancia', 'Q']);
    // Rendimiento: la curva se mueve solo mientras se ve; en otra escena o minimizada queda quieta.
    // Firma del estado animado: curva + anillo de la banda dinámica (el anillo late todo el tiempo;
    // la curva tiene tramos de reposo suave en los que no cambia ni una décima de píxel).
    const curveD = () => win.evaluate(() => {
      const svg = document.querySelector('.device--eq .vz');
      return `${svg.querySelector('.vz-curve').getAttribute('d')}|${svg.querySelector('.vz-ring').getAttribute('r')}|${svg.querySelector('.vz-ring').getAttribute('opacity')}`;
    });
    // Varias muestras: el pulso es simétrico y dos lecturas sueltas pueden coincidir a ambos lados de un pico.
    const samples = async () => {
      const seen = new Set();
      for (let i = 0; i < 6; i += 1) { seen.add(await curveD()); await win.waitForTimeout(110); }
      return seen.size;
    };
    assert.ok(await samples() > 1, 'la curva del EQ se anima en Destacado');
    await win.locator('.tile[data-section="settings"]').click();
    await win.waitForTimeout(700);
    assert.equal(await samples(), 1, 'la curva queda quieta con Destacado oculto');
    await win.locator('.tile[data-section="featured"]').click();
    await win.waitForTimeout(700);
    await app.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows()[0].minimize());
    await win.waitForFunction(() => document.documentElement.classList.contains('is-idle'), null, { timeout: 3000 });
    await win.waitForTimeout(300);
    assert.equal(await samples(), 1, 'la curva queda quieta con la ventana minimizada');
    await app.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows()[0].restore());
    await win.waitForFunction(() => !document.documentElement.classList.contains('is-idle'), null, { timeout: 3000 });
    await win.waitForTimeout(300);
    assert.ok(await samples() > 1, 'la curva retoma al restaurar la ventana');
    ok('animación solo cuando se ve: Destacado sí, otra escena no, minimizada no, al volver retoma');
    await win.waitForTimeout(900);
    await win.screenshot({ path: path.join(__dirname, 'shots', '14-destacado-eq.png') });
    await win.locator('.tile[data-section="catalog"]').click();
    const kinds = await win.locator('.pcard .pcard__art').evaluateAll((els) => Object.fromEntries(els.map((e) => [e.closest('.pcard').dataset.id, e.dataset.kind])));
    for (const [id, kind] of Object.entries({ 'zuena-eq': 'eq', 'zuena-luma': 'compressor', voxtransit: 'voice' })) {
      if (id in kinds) assert.equal(kinds[id], kind, `visual de ${id}`);
    }
    assert.ok(await win.locator('.pcard .vz').count() >= 2, 'miniaturas dibujadas');
    await win.waitForTimeout(900);
    await win.screenshot({ path: path.join(__dirname, 'shots', '15-catalogo-tipos.png') });
    await win.locator('.tile[data-section="featured"]').click();
    ok(`visual por tipo: ${Object.entries(kinds).map(([k, v]) => `${k}=${v}`).join(', ')}`);

    await win.locator('.tile[data-section="settings"]').click();
    const dir = await win.locator('.scene.is-active .field__value').first().textContent();
    assert.match(dir, /Common Files\\VST3$/);
    ok(`carpeta por defecto: ${dir}`);

    // Cambia temporalmente la carpeta de destino para probar la descarga + instalación real sin tocar el sistema.
    await win.locator('.scene.is-active [data-a="choose"]').click();
    await win.locator('.scene.is-active .field__value', { hasText: 'VST3-normal' }).waitFor();
    await win.locator('.tile[data-section="featured"]').click();
    await win.locator('.hero__inner:not(.is-leaving) .pill', { hasText: 'Instalar gratis' }).click();
    await win.locator('.hero__inner:not(.is-leaving) .chip--ok', { hasText: 'Instalado' }).waitFor({ timeout: 20000 });
    const dllStat = await fs.stat(path.join(testVstDir, 'ZUENA EQ.vst3', 'Contents', 'x86_64-win', 'ZUENA EQ.vst3'));
    assert.ok(dllStat.size > 3_000_000);
    ok(`ZUENA EQ descargado, verificado e instalado (${(dllStat.size / (1024 * 1024)).toFixed(2)} MB)`);

    // ---------------- ZUENA Luma: ficha con compresor y vídeo, instalación real ----------------
    await win.locator('.tile[data-section="catalog"]').click();
    await win.locator('.pcard[data-id="zuena-luma"]').click();
    await win.waitForTimeout(700);
    await win.locator('.pcard[data-id="zuena-luma"]').click();
    const lumaSheet = win.locator('#sheet.is-open');
    await lumaSheet.locator('.sheet__name', { hasText: 'ZUENA Luma' }).waitFor();
    await win.waitForFunction(() => { const v = document.querySelector('#sheet .stage__video'); return v && /zuena-luma-stage/.test(v.currentSrc) && !v.paused; }, null, { timeout: 10000 });
    await lumaSheet.locator('.sheet__foot .pill', { hasText: 'Instalar gratis' }).click();
    await lumaSheet.locator('.sheet__foot .chip--ok', { hasText: 'Instalado' }).waitFor({ timeout: 30000 });
    const lumaDll = await fs.stat(path.join(testVstDir, 'ZUENA Luma.vst3', 'Contents', 'x86_64-win', 'ZUENA Luma.vst3'));
    assert.ok(lumaDll.size > 1_000_000);
    const lumaManifest = JSON.parse(await fs.readFile(path.join(testVstDir, 'ZUENA Luma.vst3', 'Contents', 'Resources', 'zuena-plugin.json'), 'utf8'));
    assert.equal(lumaManifest.id, 'zuena-luma');
    await win.screenshot({ path: path.join(__dirname, 'shots', '16-ficha-luma.png') });
    await win.keyboard.press('Escape');
    await win.locator('#sheet').waitFor({ state: 'hidden' });
    await win.locator('.tile[data-section="featured"]').click();
    ok(`ZUENA Luma: ficha con vídeo, descarga, verificación e instalación (v${lumaManifest.version})`);

    // ---------------- Ficha de producto de ZUENA EQ ----------------
    await win.locator('.hero__inner:not(.is-leaving) .pill', { hasText: 'Ver detalles' }).click();
    const sheet = win.locator('#sheet.is-open');
    await sheet.locator('.sheet__name', { hasText: 'ZUENA EQ' }).waitFor();
    const videoState = () => win.evaluate(() => {
      const v = document.querySelector('#sheet .stage__video');
      return v && { t: v.currentTime, d: v.duration, paused: v.paused, loop: v.loop, muted: v.muted, w: v.videoWidth, h: v.videoHeight, src: v.currentSrc, attr: v.getAttribute('src'), rs: v.readyState };
    });

    // 1) Vídeo de fondo: arranca solo, en bucle, silenciado y vertical.
    await win.waitForFunction(() => { const v = document.querySelector('#sheet .stage__video'); return v && !v.paused && v.currentTime > 0.5; }, null, { timeout: 10000 });
    let vs = await videoState();
    assert.ok(vs.loop && vs.muted, 'bucle y silencio');
    assert.equal(`${vs.w}x${vs.h}`, '900x1200');
    assert.match(vs.src, /zuena-eq-stage\.mp4$/);
    ok('vídeo de fondo en bucle, automático y silenciado (900×1200)');

    // 2) Pausa / reanuda desde el botón del panel.
    await sheet.locator('.stage__toggle').click();
    await win.waitForFunction(() => document.querySelector('#sheet .stage__video').paused);
    // `paused` cambia al instante; la etiqueta llega con el evento 'pause' un instante después.
    await win.waitForFunction(() => document.querySelector('#sheet .stage__toggle').getAttribute('aria-label') === 'Reproducir presentación', null, { timeout: 3000 });
    await sheet.locator('.stage__toggle').click();
    await win.waitForFunction(() => !document.querySelector('#sheet .stage__video').paused);
    // 3) Al llegar al final vuelve a empezar.
    await win.evaluate(() => { const v = document.querySelector('#sheet .stage__video'); v.currentTime = v.duration - 0.3; });
    await win.waitForFunction(() => { const v = document.querySelector('#sheet .stage__video'); return !v.paused && v.currentTime < 2; }, null, { timeout: 8000 });
    ok('pausa, reanuda y vuelve a empezar al terminar');

    // 4) Secciones con contenido del catálogo y sin demo de audio.
    const heads = await sheet.locator('.pd__h').allTextContents();
    assert.deepEqual(heads.map((h) => h.replace(/\s+/g, ' ').trim()), ['01Lo esencial', '02Galería', '03Especificaciones', '04Novedades', '05Acerca de']);
    assert.equal(await sheet.locator('.feat').count(), 6);
    assert.equal(await sheet.locator('.spec').count(), 8);
    assert.equal(await sheet.locator('.pd__headline em').textContent(), 'escucha');
    await sheet.locator('.change.is-current .chip', { hasText: 'Versión actual' }).waitFor();
    assert.equal(await sheet.locator('.ab, audio').count(), 0, 'la demo de audio ya no existe');
    const fonts = await win.evaluate(() => ['300 40px "Inter Tight"', 'italic 400 30px "Instrument Serif"', '500 14px "JetBrains Mono"'].map((f) => document.fonts.check(f)));
    assert.deepEqual(fonts, [true, true, true], 'tipografías cargadas');
    ok('secciones 01–05, destacados, especificaciones, novedades y tipografías');

    // 5) Galería: las 4 capturas cargan.
    await sheet.locator('.shots').scrollIntoViewIfNeeded();
    await win.waitForFunction(() => [...document.querySelectorAll('#sheet .shot img')].every((i) => i.complete && i.naturalWidth > 0), null, { timeout: 8000 });
    assert.equal(await sheet.locator('.shot').count(), 4);
    await win.waitForTimeout(400);
    await win.screenshot({ path: path.join(__dirname, 'shots', '10-ficha-galeria.png') });

    // 6) Galería ampliada: abrir, navegar con teclado y botones, Esc solo cierra la imagen.
    await sheet.locator('.shot').first().click();
    const lb = sheet.locator('.lightbox.is-open');
    await lb.waitFor();
    // Capturas en HD (2x): la ampliada tiene al menos 2720 px de ancho.
    await win.waitForFunction(() => document.querySelector('#sheet .lightbox__img').naturalWidth >= 2720);
    assert.equal(await lb.locator('[data-lb="n"]').textContent(), '1 / 4');
    await win.keyboard.press('ArrowRight');
    assert.equal(await lb.locator('[data-lb="n"]').textContent(), '2 / 4');
    assert.equal(await lb.locator('[data-lb="cap"]').textContent(), 'Modo oscuro');
    await win.keyboard.press('ArrowLeft');
    await win.keyboard.press('ArrowLeft');
    assert.equal(await lb.locator('[data-lb="n"]').textContent(), '4 / 4');
    await lb.locator('[data-lb="next"]').click();
    assert.equal(await lb.locator('[data-lb="n"]').textContent(), '1 / 4');
    await win.screenshot({ path: path.join(__dirname, 'shots', '11-ficha-lightbox.png') });
    await win.keyboard.press('Escape');
    await sheet.locator('.lightbox').waitFor({ state: 'hidden' });
    assert.equal(await win.locator('#sheet').isVisible(), true, 'Esc cierra la imagen, no la ficha');
    await sheet.locator('.shot').nth(2).click();
    await sheet.locator('.lightbox.is-open').waitFor();
    await sheet.locator('.lightbox').click({ position: { x: 24, y: 24 } }); // fondo oscuro de la galería, dentro de la ficha
    await sheet.locator('.lightbox').waitFor({ state: 'hidden' });
    assert.equal(await win.locator('#sheet').isVisible(), true, 'clic en el fondo cierra solo la imagen');
    ok('galería ampliada: teclado, botones, Esc y clic afuera');

    // 7) Cambiar a modo oscuro con la ficha abierta cambia el vídeo sin cortar.
    await sheet.locator('.pd__scroll').evaluate((el) => { el.scrollTop = 0; });
    await win.evaluate(() => document.querySelector('#btn-theme').click());
    await win.waitForFunction(() => /stage-oscuro\.mp4$/.test(document.querySelector('#sheet .stage__video').currentSrc), null, { timeout: 8000 });
    await win.waitForFunction(() => { const v = document.querySelector('#sheet .stage__video'); return !v.paused && v.currentTime > 0.3; }, null, { timeout: 8000 });
    await win.waitForTimeout(500);
    await win.screenshot({ path: path.join(__dirname, 'shots', '12-ficha-oscuro.png') });
    await win.evaluate(() => document.querySelector('#btn-theme').click());
    await win.waitForFunction(() => /stage\.mp4$/.test(document.querySelector('#sheet .stage__video').currentSrc), null, { timeout: 8000 });
    await win.waitForTimeout(500);
    await win.screenshot({ path: path.join(__dirname, 'shots', '10-ficha.png') });
    ok('el vídeo sigue al tema claro / oscuro');

    // 8) Ventana al mínimo (1100×700): nada se desborda ni queda tapado.
    const prevSize = await app.evaluate(({ BrowserWindow }) => {
      const w = BrowserWindow.getAllWindows()[0];
      const s = w.getContentSize();
      w.unmaximize();
      w.setContentSize(1100, 700);
      return s;
    });
    await win.waitForTimeout(600);
    const fit = await win.evaluate(() => {
      const r = (sel) => document.querySelector(sel).getBoundingClientRect();
      const sc = document.querySelector('#sheet .pd__scroll');
      const sheetR = r('#sheet-body');
      const foot = r('#sheet .sheet__foot');
      const pills = [...document.querySelectorAll('#sheet .sheet__foot .pill')].map((b) => b.getBoundingClientRect());
      return {
        inView: sheetR.left >= 0 && sheetR.top >= 0 && sheetR.right <= innerWidth && sheetR.bottom <= innerHeight,
        noHScroll: sc.scrollWidth <= sc.clientWidth + 1,
        footInside: foot.bottom <= sheetR.bottom + 1 && pills.every((p) => p.right <= foot.right + 1 && p.bottom <= foot.bottom + 1),
        stageW: Math.round(r('#sheet .stage').width),
      };
    });
    assert.deepEqual({ ...fit, stageW: fit.stageW >= 340 }, { inView: true, noHScroll: true, footInside: true, stageW: true }, JSON.stringify(fit));
    await win.screenshot({ path: path.join(__dirname, 'shots', '13-ficha-1100x700.png') });
    await app.evaluate(({ BrowserWindow }, [w, h]) => BrowserWindow.getAllWindows()[0].setContentSize(w, h), prevSize);
    await win.waitForTimeout(300);
    ok('ficha entra completa en la ventana mínima (1100×700)');

    // 9) Cerrar libera el vídeo.
    await win.keyboard.press('Escape');
    await win.locator('#sheet').waitFor({ state: 'hidden' });
    vs = await videoState();
    // currentSrc conserva la URL vieja en Chromium; lo que importa es que no haya src ni datos cargados.
    assert.ok(!vs || (vs.paused && !vs.attr && vs.rs === 0), `vídeo detenido y liberado al cerrar: ${JSON.stringify(vs)}`);
    ok('cerrar la ficha detiene y libera el vídeo');

    await win.locator('.tile[data-section="settings"]').click();
    await win.locator('.scene.is-active [data-a="reset"]').click();
    await win.locator('.scene.is-active .field__value', { hasText: /Common Files\\VST3$/ }).waitFor();

    await win.locator('#catalog-url').fill('http://example.com/catalog.json');
    await win.locator('[data-a="save-url"]').click();
    await win.locator('.toast', { hasText: 'https://' }).waitFor();
    ok('rechaza URL de catálogo no https');

    await win.locator('#catalog-url').fill('https://127.0.0.1:1/catalog.json');
    await win.locator('#catalog-url').press('Enter');
    await win.locator('.toast', { hasText: 'No se pudo descargar el catálogo' }).waitFor({ timeout: 15000 });
    await win.locator('.scene.is-active dd', { hasText: 'Catálogo integrado' }).waitFor();
    ok('URL caída → cae al catálogo integrado con aviso');

    await win.locator('[data-a="clear-url"]').click();
    await win.locator('.toast', { hasText: 'Catálogo cargado' }).waitFor();
    assert.equal(await win.locator('[data-a="clear-url"]').count(), 0);
    ok('"Usar catálogo integrado" limpia la URL');

    assert.deepEqual(errors, []);
    ok('sin errores de consola');
    await app.close();
    console.log('\nE2E modo normal OK');
  } catch (e) {
    console.log(`✖ ${e.message}`);
    await win.screenshot({ path: path.join(__dirname, 'shots', 'zz-normal-fallo.png') }).catch(() => {});
    await app.close().catch(() => {});
    process.exitCode = 1;
  } finally {
    await fs.rm(userData, { recursive: true, force: true }).catch(() => {});
  }
})();

