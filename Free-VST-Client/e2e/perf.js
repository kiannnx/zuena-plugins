'use strict';
// Benchmark de rendimiento del launcher real: por escena mide CPU de cada proceso (incluye GPU),
// cuadros por segundo, cuadros lentos y trabajo de estilo/layout del renderer.
// Uso: node e2e/perf.js [ruta\a\ZUENA Plugins.exe] [--json salida.json]

const { _electron: electron } = require('playwright-core');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');

const SECONDS = 5;

(async () => {
  const exe = process.argv[2] && !process.argv[2].startsWith('--') ? process.argv[2] : null;
  const jsonOut = process.argv.includes('--json') ? process.argv[process.argv.indexOf('--json') + 1] : null;
  const userData = await fs.mkdtemp(path.join(os.tmpdir(), 'zuena-perf-'));
  const t0 = Date.now();
  const app = await electron.launch({
    ...(exe ? { executablePath: path.resolve(exe), args: [] } : { args: [path.join(__dirname, '..')] }),
    env: { ...process.env, FREEVST_USER_DATA: userData, FREEVST_ONBOARDED: '1' },
  });
  const win = await app.firstWindow();
  await win.locator('.hero__inner:not(.is-leaving) .hero__title').waitFor({ timeout: 20000 });
  const startup = Date.now() - t0;
  await app.evaluate(({ BrowserWindow }) => { const w = BrowserWindow.getAllWindows()[0]; w.setContentSize(1360, 820); w.center(); });
  await win.waitForTimeout(3000); // calentamiento: entradas animadas y ventana acomodada
  const cdp = await win.context().newCDPSession(win);
  await cdp.send('Performance.enable');

  const metrics = async () => Object.fromEntries((await cdp.send('Performance.getMetrics')).metrics.map((m) => [m.name, m.value]));
  const cpu = () => app.evaluate(({ app: a }) => a.getAppMetrics().map((m) => ({ type: m.type, cpu: m.cpu.percentCPUUsage, mem: m.memory.workingSetSize })));

  async function measure(label, setup) {
    await setup();
    await win.waitForTimeout(1500);
    // 1) CPU y trabajo del hilo principal SIN contador de cuadros (un rAF propio inflaría estilos y tareas).
    await cpu(); // reinicia la ventana de percentCPUUsage
    const m0 = await metrics();
    await win.waitForTimeout(SECONDS * 1000);
    const procs = await cpu();
    const m1 = await metrics();
    // 2) Fluidez: cuadros por segundo y cuadros lentos, medido aparte.
    const times = await win.evaluate((ms) => new Promise((resolve) => {
      const t = [];
      const end = performance.now() + ms;
      const tick = (x) => { t.push(x); if (x < end) requestAnimationFrame(tick); else resolve(t); };
      requestAnimationFrame(tick);
    }), 3000);
    const dts = times.slice(1).map((t, i) => t - times[i]);
    const byType = {};
    for (const p of procs) {
      byType[p.type] ??= { cpu: 0, memMB: 0 };
      byType[p.type].cpu += p.cpu;
      byType[p.type].memMB += p.mem / 1024;
    }
    const r = {
      escena: label,
      fps: +(dts.length / 3).toFixed(1),
      lentos: dts.filter((d) => d > 25).length,
      peorMs: +Math.max(...dts).toFixed(1),
      cpuRenderer: +(byType.Tab?.cpu ?? 0).toFixed(1),
      cpuGPU: +(byType.GPU?.cpu ?? 0).toFixed(1),
      cpuTotal: +procs.reduce((a, p) => a + p.cpu, 0).toFixed(1),
      tareasMs: +(((m1.TaskDuration - m0.TaskDuration) * 1000) / SECONDS).toFixed(1),
      estilos: Math.round((m1.RecalcStyleCount - m0.RecalcStyleCount) / SECONDS),
      layouts: Math.round((m1.LayoutCount - m0.LayoutCount) / SECONDS),
      memMB: Math.round(procs.reduce((a, p) => a + p.mem, 0) / 1024),
    };
    console.log(JSON.stringify(r));
    return r;
  }

  const tile = (id) => win.locator(`.tile[data-section="${id}"]`).click();
  const results = [];
  results.push(await measure('Destacado', () => tile('featured')));
  results.push(await measure('Catálogo', () => tile('catalog')));
  results.push(await measure('Instalados', () => tile('installed')));
  results.push(await measure('Ajustes', () => tile('settings')));
  results.push(await measure('Ficha con vídeo', async () => {
    await tile('featured');
    await win.waitForTimeout(600);
    await win.locator('.hero__inner:not(.is-leaving) .pill', { hasText: 'Ver detalles' }).click();
    await win.locator('#sheet.is-open').waitFor();
  }));
  results.push(await measure('Ventana minimizada', async () => {
    await win.keyboard.press('Escape');
    await win.waitForTimeout(400);
    await app.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows()[0].minimize());
  }));
  await app.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows()[0].restore());
  console.log(`arranque hasta destacado visible: ${startup} ms`);
  if (jsonOut) await fs.writeFile(jsonOut, JSON.stringify({ startup, results }, null, 2));
  // Cierre garantizado: una instancia huérfana seguiría consumiendo CPU y falsearía la próxima medición.
  const proc = app.process();
  await Promise.race([app.close().catch(() => {}), new Promise((r) => setTimeout(r, 5000))]);
  if (proc.exitCode === null) proc.kill();
  await fs.rm(userData, { recursive: true, force: true }).catch(() => {});
})().catch((e) => { console.error(e); process.exit(1); });
