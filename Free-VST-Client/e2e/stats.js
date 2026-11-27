'use strict';
// E2E de las ESTADÍSTICAS ANÓNIMAS: sin permiso no se envía nada; al activarlas llega un ping con
// solo campos acotados; al apagarlas se pide borrar esa instalación y cambia el identificador.
// Todo contra un servidor local (catálogo + funciones RPC falsas): no toca la base real ni internet.

const { _electron: electron } = require('playwright-core');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;

(async () => {
  const userData = await fs.mkdtemp(path.join(os.tmpdir(), 'zuena-e2e-stats-'));
  const catalog = await fs.readFile(path.join(ROOT, 'catalog', 'catalog.json'));
  const calls = [];
  const server = http.createServer((req, res) => {
    if (req.url === '/catalog.json') { res.writeHead(200, { 'Content-Type': 'application/json' }); return res.end(catalog); }
    let body = '';
    req.on('data', (c) => { body += c; });
    return req.on('end', () => {
      calls.push({ url: req.url, key: req.headers.apikey, body: body ? JSON.parse(body) : null });
      res.writeHead(204);
      res.end();
    });
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  // Usuario que ya hizo la guía en una versión anterior (sin el permiso de estadísticas).
  await fs.writeFile(path.join(userData, 'settings.json'), JSON.stringify({
    onboarded: true, online: true, catalogUrl: `${base}/catalog.json`, installDir: path.join(userData, 'VST3'),
  }));

  const app = await electron.launch({
    ...(process.argv[2] ? { executablePath: path.resolve(process.argv[2]), args: [] } : { args: [ROOT] }),
    env: { ...process.env, FREEVST_USER_DATA: userData, FREEVST_STATS_URL: `${base}/rpc` },
  });
  const errors = [];
  const win = await app.firstWindow();
  win.on('pageerror', (e) => errors.push(e.message));
  win.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  const ok = (m) => console.log(`✔ ${m}`);
  const rpc = () => calls.filter((c) => c.url.startsWith('/rpc/'));
  const state = () => win.evaluate(() => window.hub.getState());
  try {
    await win.locator('.hero__inner:not(.is-leaving) .hero__title').waitFor({ timeout: 15000 });
    await win.waitForTimeout(5000); // pasa el arranque (revisión a los 4 s) sin permiso
    assert.equal(rpc().length, 0, 'sin permiso no se envía nada');
    const st0 = await state();
    assert.equal(st0.stats, false);
    assert.ok(st0.notifications.some((n) => n.text.includes('estadísticas anónimas')), 'aviso único para quien ya hizo la guía');
    ok('apagadas por defecto: ningún envío y un aviso único de dónde activarlas');

    await win.locator('.tile[data-section="settings"]').click();
    const sw = win.locator('.scene.is-active .switch[data-set="stats"]');
    await sw.waitFor();
    assert.equal(await sw.isChecked(), false);
    await sw.click();
    await win.locator('.toast', { hasText: 'estadísticas anónimas están activadas' }).waitFor();
    const ping = rpc().find((c) => c.url === '/rpc/zuena_plugins_ping');
    assert.ok(ping, 'llega el ping al activar');
    assert.ok(ping.key.startsWith('sb_publishable_'));
    assert.deepEqual(Object.keys(ping.body).sort(), ['p_install_id', 'p_launcher_version', 'p_locale', 'p_os', 'p_plugins']);
    assert.match(ping.body.p_install_id, UUID_RE);
    assert.equal(ping.body.p_launcher_version, require('../package.json').version);
    const sent = JSON.stringify(ping.body);
    assert.ok(!sent.includes(os.userInfo().username) && !sent.includes(os.hostname()), 'sin usuario ni nombre del equipo');
    ok(`activarlas envía un ping anónimo (${sent.length} bytes)`);

    await sw.click();
    await win.locator('.toast', { hasText: 'se borró lo enviado' }).waitFor();
    const forget = rpc().find((c) => c.url === '/rpc/zuena_plugins_forget');
    assert.ok(forget, 'apagarlas pide borrar');
    assert.equal(forget.body.p_install_id, ping.body.p_install_id);
    const st1 = await state();
    assert.equal(st1.stats, false);
    ok('apagarlas borra esa instalación del servidor');

    // Reactivar usa un identificador nuevo: lo anterior no queda enlazado.
    await sw.click();
    await win.locator('.toast', { hasText: 'activadas' }).last().waitFor();
    const pings = rpc().filter((c) => c.url === '/rpc/zuena_plugins_ping');
    assert.equal(pings.length, 2);
    assert.notEqual(pings[1].body.p_install_id, ping.body.p_install_id);
    ok('reactivarlas usa un identificador nuevo');

    assert.deepEqual(errors, []);
    ok('sin errores de consola');
    await app.close();
    console.log('\nE2E estadísticas OK');
  } catch (e) {
    console.log(`✖ ${e.message}`);
    await win.screenshot({ path: path.join(__dirname, 'shots', 'zz-stats-fallo.png') }).catch(() => {});
    await app.close().catch(() => {});
    process.exitCode = 1;
  } finally {
    server.close();
    await fs.rm(userData, { recursive: true, force: true }).catch(() => {});
  }
})();
