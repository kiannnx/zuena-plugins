'use strict';

const { app, BrowserWindow, ipcMain, shell, dialog, Menu, nativeTheme, Notification } = require('electron');
const path = require('node:path');
const os = require('node:os');
const fsp = require('node:fs/promises');
const { pathToFileURL } = require('node:url');

const { loadCatalog, isAllowedUrl, resolveBundledUrls, compareVersions } = require('./core/catalog');
const { download } = require('./core/downloader');
const { installPackage, uninstallFiles } = require('./core/installer');
const { runElevated } = require('./core/elevate');
const { JsonStore } = require('./core/store');
const { defaultVst3Dir } = require('./core/paths');
const { readBundleVersion } = require('./core/bundle-info');
const { startDemoServer } = require('./core/demo-server');
const { startPackageServer } = require('./core/package-server');
const dist = require('./core/distribution');
const launcherUpdate = require('./core/launcher-update');
const uninstallCore = require('./core/uninstall');
const stats = require('./core/stats');

const DEMO = process.argv.includes('--demo') || process.env.FREEVST_DEMO === '1';
const ROOT = path.join(__dirname, '..', '..');
const RENDERER_INDEX = path.join(ROOT, 'src', 'renderer', 'index.html');
const RENDERER_URL_PREFIX = pathToFileURL(path.join(ROOT, 'src', 'renderer')).href;
const MAX_NOTIFICATIONS = 50;
const CHECK_EVERY_MS = 6 * 60 * 60 * 1000; // revisión periódica con la app abierta
const CHECK_ON_FOCUS_MS = 30 * 60 * 1000; // al volver a la ventana, si pasó más de media hora
const STATS_EVERY_MS = 12 * 60 * 60 * 1000; // estadísticas: como mucho cada 12 h, o al cambiar los plugins
// Destino de las estadísticas: la suite E2E lo apunta a un servidor local para no tocar los datos reales.
const STATS_OPTS = process.env.FREEVST_STATS_URL ? { base: process.env.FREEVST_STATS_URL } : {};

let win = null;
let demoServer = null;
let packageServer = null;
let settings;
let installedStore;
let catalog = { plugins: [], featured: null };
let catalogMeta = { source: 'bundled', warning: null, errors: [] };
const jobs = new Map();
let jobSeq = 0;
let launcherInfo = { update: null, phase: 'idle', received: 0, total: 0, message: '', staged: null };
let checking = null;

// Carpeta de datos aislada (la usa la suite E2E para no tocar los datos reales del usuario).
if (process.env.FREEVST_USER_DATA) app.setPath('userData', path.resolve(process.env.FREEVST_USER_DATA));
// --uninstall: lo abre «Aplicaciones instaladas» de Windows. No compite por la instancia única: si el launcher
// está abierto, se avisa en lugar de cerrarse en silencio.
const UNINSTALL = process.argv.includes('--uninstall');
const primaryInstance = app.requestSingleInstanceLock();
if (!UNINSTALL && !primaryInstance) app.quit();
app.setAppUserModelId(dist.APP_USER_MODEL_ID); // necesario para las notificaciones de Windows

const userFile = (name) => path.join(app.getPath('userData'), DEMO ? name.replace('.json', '-demo.json') : name);
const sandboxDir = () => path.join(app.getPath('userData'), 'demo-VST3');
const defaultInstallDir = () => (DEMO ? sandboxDir() : defaultVst3Dir());
const installDir = () => settings.get('installDir') || defaultInstallDir();
const send = (channel, payload) => { if (win && !win.isDestroyed()) win.webContents.send(channel, payload); };

// ---------- Notificaciones ----------
function notify(level, text, { desktop = false } = {}) {
  if (desktop) desktopNotify(text);
  const list = settings.get('notifications');
  list.unshift({ id: `${Date.now()}-${Math.random().toString(36).slice(2, 7)}`, time: Date.now(), level, text, read: false });
  list.length = Math.min(list.length, MAX_NOTIFICATIONS);
  settings.save();
  send('notif:update', list);
}

/** Aviso nativo de Windows, solo si el usuario lo permitió y la ventana no está al frente. */
function desktopNotify(text) {
  if (!settings.get('desktopNotify') || !Notification.isSupported()) return;
  if (win && !win.isDestroyed() && win.isFocused() && !win.isMinimized()) return;
  const n = new Notification({ title: 'ZUENA Plugins', body: text, icon: path.join(ROOT, 'build', 'icon.png') });
  n.on('click', () => { if (win && !win.isDestroyed()) { if (win.isMinimized()) win.restore(); win.show(); win.focus(); } });
  n.show();
}

// ---------- Estado de instalación ----------
async function exists(p) {
  try { await fsp.access(p); return true; } catch { return false; }
}

/**
 * Mapa id → {version, files, external}. Limpia registros cuyos archivos ya no existen.
 * La versión sale del manifiesto dentro del bundle instalado (la verdad en disco); el registro
 * del launcher solo se usa si el bundle no trae manifiesto (paquetes anteriores a 0.3.1).
 */
async function computeInstalled() {
  const reg = installedStore.get('plugins');
  const out = {};
  let dirty = false;
  for (const [id, rec] of Object.entries(reg)) {
    const present = [];
    for (const f of rec.files) if (await exists(f)) present.push(f);
    if (present.length === 0) { delete reg[id]; dirty = true; continue; }
    const onDisk = await readBundleVersion(present[0], id);
    if (onDisk && onDisk !== rec.version) { rec.version = onDisk; dirty = true; }
    out[id] = { version: onDisk ?? rec.version, files: present, external: false, installedAt: rec.installedAt };
  }
  for (const p of catalog.plugins) {
    if (out[p.id] || !p.bundle) continue;
    const f = path.join(installDir(), p.bundle);
    if (await exists(f)) out[p.id] = { version: await readBundleVersion(f, p.id), files: [f], external: true, installedAt: null };
  }
  if (dirty) await installedStore.save();
  return out;
}

const publicJob = (j) => ({
  id: j.id, pluginId: j.pluginId, name: j.name, version: j.version, phase: j.phase, received: j.received,
  total: j.total, bytesPerSec: j.bytesPerSec, message: j.message, startedAt: j.startedAt, finishedAt: j.finishedAt,
});
const broadcastJobs = () => send('jobs:update', [...jobs.values()].map(publicJob));

async function fullState() {
  return {
    demo: DEMO,
    platform: process.platform,
    appVersion: app.getVersion(),
    installDir: installDir(),
    defaultInstallDir: defaultInstallDir(),
    catalogUrl: settings.get('catalogUrl'),
    officialCatalogUrl: dist.OFFICIAL_CATALOG_URL,
    catalogSource: catalogMeta.source,
    online: !!settings.get('online'),
    desktopNotify: !!settings.get('desktopNotify'),
    stats: !!settings.get('stats'),
    onboarded: !!settings.get('onboarded'),
    lastCheck: settings.get('lastCheck') || 0,
    launcher: {
      packaged: app.isPackaged,
      installed: app.isPackaged && dist.isInstalledCopy(),
      installRoot: dist.installRoot(),
      ...publicLauncher(),
    },
    theme: settings.get('theme') || 'light',
    watch: settings.get('watch'),
    notifications: settings.get('notifications'),
    installed: await computeInstalled(),
    jobs: [...jobs.values()].map(publicJob),
  };
}
const broadcastState = async () => send('state:changed', await fullState());

// ---------- Catálogo ----------
/** URL efectiva: la que el usuario configuró, o la oficial si permitió la conexión a internet. */
function catalogUrl() {
  if (DEMO) return demoServer.url;
  return settings.get('catalogUrl') || (settings.get('online') ? dist.OFFICIAL_CATALOG_URL : '');
}

async function refreshCatalog() {
  const url = catalogUrl();
  const res = await loadCatalog({
    url,
    cachePath: userFile('catalog-cache.json'),
    bundledPath: path.join(ROOT, 'catalog', 'catalog.json'),
  });
  catalog = res.source === 'bundled' && packageServer ? resolveBundledUrls(res.catalog, packageServer.base) : res.catalog;
  catalogMeta = { source: res.source, warning: res.warning, errors: res.errors };
  const watch = settings.get('watch');
  const ready = watch.filter((id) => catalog.plugins.some((p) => p.id === id && p.status === 'available' && p.downloads[process.platform]));
  if (ready.length) {
    settings.data.watch = watch.filter((id) => !ready.includes(id));
    for (const id of ready) notify('success', `${catalog.plugins.find((p) => p.id === id).name} ya está disponible para descargar.`, { desktop: true });
    await settings.save();
  }
  await notifyUpdates();
  return { catalog, ...catalogMeta, state: await fullState() };
}

/** Avisa una sola vez por versión cuando el catálogo trae una versión mayor que la instalada. */
async function notifyUpdates() {
  const installed = await computeInstalled();
  const seen = settings.get('updateNotified') ?? {};
  let changed = false;
  for (const p of catalog.plugins) {
    const inst = installed[p.id];
    if (!inst?.version || p.status !== 'available' || !p.downloads[process.platform]) continue;
    if (compareVersions(p.version, inst.version) > 0 && seen[p.id] !== p.version) {
      seen[p.id] = p.version;
      changed = true;
      notify('info', `${p.name} ${p.version} disponible (tenés la ${inst.version}).`, { desktop: true });
    }
  }
  if (changed) await settings.set('updateNotified', seen);
}

// ---------- Conexión: catálogo en línea y actualización del launcher ----------
const publicLauncher = () => ({
  update: launcherInfo.update, phase: launcherInfo.phase, received: launcherInfo.received, total: launcherInfo.total, message: launcherInfo.message,
});
const broadcastLauncher = () => send('launcher:update', publicLauncher());

async function checkLauncher() {
  if (DEMO || !settings.get('online')) return;
  try {
    const m = await launcherUpdate.fetchManifest(dist.LAUNCHER_MANIFEST_URL);
    if (!launcherUpdate.isNewer(m, app.getVersion())) return;
    if (launcherInfo.phase === 'idle' || launcherInfo.update?.version !== m.version) {
      launcherInfo = { ...launcherInfo, update: m, phase: launcherInfo.staged ? launcherInfo.phase : 'available' };
      broadcastLauncher();
    }
    if (settings.get('launcherNotified') !== m.version) {
      await settings.set('launcherNotified', m.version);
      notify('info', `ZUENA Plugins ${m.version} disponible. Actualizalo desde Ajustes.`, { desktop: true });
    }
  } catch {
    /* sin conexión o manifiesto inválido: se reintenta en la próxima revisión */
  }
}

/** Revisión en línea: catálogo (plugins nuevos y actualizaciones) y versión del launcher. */
function checkOnline({ force = false } = {}) {
  if (DEMO || !settings.get('online')) return Promise.resolve(null);
  if (!force && Date.now() - (settings.get('lastCheck') || 0) < CHECK_ON_FOCUS_MS) return Promise.resolve(null);
  if (checking) return checking;
  checking = (async () => {
    try {
      const res = await refreshCatalog();
      await settings.set('lastCheck', Date.now());
      send('catalog:changed', res);
      await checkLauncher();
      pingStats().catch(() => {});
      return res;
    } finally {
      checking = null;
    }
  })();
  return checking;
}

async function downloadLauncherUpdate() {
  const m = launcherInfo.update;
  if (!m) throw new Error('No hay una versión nueva del launcher');
  if (!app.isPackaged) throw new Error('La actualización solo funciona en la app instalada');
  if (['download', 'ready'].includes(launcherInfo.phase)) return publicLauncher();
  launcherInfo = { ...launcherInfo, phase: 'download', received: 0, total: m.win32.size, message: '' };
  broadcastLauncher();
  try {
    launcherInfo.staged = await launcherUpdate.prepareUpdate({
      manifest: m, appRoot: dist.runningRoot(), exeName: path.basename(process.execPath),
      workDir: path.join(app.getPath('userData'), 'updates'),
      onProgress: (p) => { launcherInfo.received = p.received; launcherInfo.total = p.total; broadcastLauncher(); },
    });
    launcherInfo.phase = 'ready';
  } catch (e) {
    launcherInfo.phase = 'error';
    launcherInfo.message = e.message;
  }
  broadcastLauncher();
  return publicLauncher();
}

// ---------- Estadísticas anónimas (solo con permiso) ----------
/** Envía el ping si el usuario lo permitió. Sin conexión o con error, se reintenta en la próxima revisión. */
async function pingStats({ force = false } = {}) {
  if ((DEMO && !process.env.FREEVST_STATS_URL) || !settings.get('online') || !settings.get('stats')) return false;
  if (!stats.isInstallId(settings.get('installId'))) await settings.set('installId', stats.newInstallId());
  const ping = stats.buildPing({
    installId: settings.get('installId'), appVersion: app.getVersion(), platform: process.platform,
    osRelease: os.release(), locale: settings.get('language'), installed: await computeInstalled(),
  });
  const sig = JSON.stringify([ping.p_launcher_version, ping.p_plugins]);
  if (!force && sig === settings.get('statsSig') && Date.now() - (settings.get('statsLast') || 0) < STATS_EVERY_MS) return false;
  try {
    await stats.sendPing(ping, STATS_OPTS);
    settings.data.statsLast = Date.now();
    settings.data.statsSig = sig;
    await settings.save();
    return true;
  } catch {
    return false;
  }
}

/** Apagar las estadísticas borra lo enviado por esta instalación y cambia el identificador. */
async function setStats(on) {
  const was = !!settings.get('stats');
  await settings.set('stats', !!on);
  if (on && !was) await pingStats({ force: true });
  if (!on && was && stats.isInstallId(settings.get('installId'))) {
    const old = settings.get('installId');
    settings.data.installId = stats.newInstallId();
    settings.data.statsSig = '';
    await settings.save();
    await stats.forget(old, STATS_OPTS).catch(() => {});
  }
}

// ---------- Instalación ----------
async function startInstall(pluginId) {
  const plugin = catalog.plugins.find((p) => p.id === pluginId);
  if (!plugin) throw new Error('Plugin no encontrado en el catálogo');
  if (plugin.status !== 'available') throw new Error(`${plugin.name} aún no está disponible`);
  const dl = plugin.downloads[process.platform];
  if (!dl) throw new Error(`${plugin.name} no tiene versión para este sistema operativo`);
  for (const j of jobs.values()) {
    if (j.pluginId === pluginId && ['download', 'install'].includes(j.phase)) throw new Error(`${plugin.name} ya se está instalando`);
  }
  const job = {
    id: `job-${++jobSeq}`, pluginId, name: plugin.name, version: plugin.version, phase: 'download', received: 0,
    total: dl.size, bytesPerSec: 0, message: '', startedAt: Date.now(), finishedAt: null, controller: new AbortController(),
  };
  jobs.set(job.id, job);
  broadcastJobs();
  runJob(job, plugin, dl);
  return job.id;
}

async function runJob(job, plugin, dl) {
  const dlDir = path.join(app.getPath('userData'), 'downloads');
  const zipPath = path.join(dlDir, `${plugin.id}-${plugin.version}.zip`);
  try {
    await fsp.mkdir(dlDir, { recursive: true });
    await download({
      url: dl.url, dest: zipPath, sha256: dl.sha256, size: dl.size, signal: job.controller.signal,
      onProgress: (p) => { Object.assign(job, p); broadcastJobs(); },
    });
    job.phase = 'install';
    job.message = 'Instalando…';
    broadcastJobs();
    const previous = installedStore.get('plugins')[plugin.id];
    const res = await installPackage({
      zipPath, targetDir: installDir(), stagingDir: path.join(app.getPath('temp'), `zuena-stage-${plugin.id}`), runElevated,
    });
    const stale = (previous?.files ?? []).filter((f) => !res.files.includes(f));
    if (stale.length) await uninstallFiles({ files: stale, targetDir: path.dirname(stale[0]), runElevated }).catch(() => {});
    installedStore.get('plugins')[plugin.id] = { version: plugin.version, files: res.files, installedAt: Date.now() };
    await installedStore.save();
    job.phase = 'done';
    job.message = res.elevated ? 'Instalado con permisos de administrador' : 'Instalado';
    notify('success', `${plugin.name} ${plugin.version} instalado en ${installDir()}`);
    pingStats().catch(() => {});
  } catch (e) {
    job.phase = e.code === 'CANCELED' ? 'canceled' : 'error';
    job.message = e.message;
    if (job.phase === 'error') notify('error', `${plugin.name}: ${e.message}`);
  } finally {
    job.finishedAt = Date.now();
    await fsp.rm(zipPath, { force: true }).catch(() => {});
    broadcastJobs();
    broadcastState();
  }
}

async function uninstall(pluginId) {
  const inst = (await computeInstalled())[pluginId];
  if (!inst) throw new Error('El plugin no está instalado');
  const name = catalog.plugins.find((p) => p.id === pluginId)?.name ?? pluginId;
  await uninstallFiles({ files: inst.files, targetDir: path.dirname(inst.files[0]), runElevated });
  delete installedStore.get('plugins')[pluginId];
  await installedStore.save();
  notify('info', `${name} desinstalado`);
  pingStats().catch(() => {});
  return fullState();
}

// ---------- IPC ----------
function handle(channel, fn) {
  ipcMain.handle(channel, async (event, ...args) => {
    if (!event.senderFrame?.url.startsWith(RENDERER_URL_PREFIX)) throw new Error('Origen IPC no permitido');
    return fn(...args);
  });
}

function registerIpc() {
  handle('state:get', fullState);
  handle('catalog:load', refreshCatalog);
  handle('plugin:install', (id) => startInstall(String(id)));
  handle('job:cancel', (jobId) => {
    const j = jobs.get(String(jobId));
    if (j && j.phase === 'download') j.controller.abort();
    return true;
  });
  handle('jobs:clearFinished', () => {
    for (const [id, j] of jobs) if (!['download', 'install'].includes(j.phase)) jobs.delete(id);
    broadcastJobs();
    return true;
  });
  handle('plugin:uninstall', (id) => uninstall(String(id)));
  handle('dir:open', async () => {
    await fsp.mkdir(installDir(), { recursive: true }).catch(() => {});
    const err = await shell.openPath(installDir());
    if (err) throw new Error(err);
    return true;
  });
  handle('plugin:reveal', async (id) => {
    const inst = (await computeInstalled())[String(id)];
    if (!inst) throw new Error('El plugin no está instalado');
    shell.showItemInFolder(inst.files[0]);
    return true;
  });
  handle('dir:choose', async () => {
    const r = await dialog.showOpenDialog(win, {
      title: 'Carpeta de instalación de plugins VST3', defaultPath: installDir(), properties: ['openDirectory', 'createDirectory'],
    });
    if (r.canceled || !r.filePaths[0]) return null;
    await settings.set('installDir', r.filePaths[0]);
    notify('info', `Carpeta de instalación cambiada a ${r.filePaths[0]}`);
    return fullState();
  });
  handle('dir:reset', async () => {
    await settings.set('installDir', null);
    return fullState();
  });
  handle('settings:setCatalogUrl', async (url) => {
    const v = String(url ?? '').trim();
    if (v && !isAllowedUrl(v)) throw new Error('La URL debe empezar por https://');
    await settings.set('catalogUrl', v);
    return refreshCatalog();
  });
  handle('settings:setTheme', async (theme) => {
    const t = ['light', 'dark', 'system'].includes(theme) ? theme : 'light';
    await settings.set('theme', t);
    nativeTheme.themeSource = t;
    if (win && !win.isDestroyed()) {
      const dark = t === 'dark' || (t === 'system' && nativeTheme.shouldUseDarkColors);
      win.setBackgroundColor(dark ? '#101112' : '#f5f5f3');
    }
    return fullState();
  });
  handle('watch:toggle', async (id) => {
    const list = settings.get('watch');
    const i = list.indexOf(String(id));
    if (i >= 0) list.splice(i, 1); else list.push(String(id));
    await settings.save();
    return list;
  });
  handle('notif:markRead', async () => {
    settings.get('notifications').forEach((n) => { n.read = true; });
    await settings.save();
    return settings.get('notifications');
  });
  handle('notif:clear', async () => {
    await settings.set('notifications', []);
    return [];
  });
  handle('onboarding:complete', async (opts) => {
    const o = opts && typeof opts === 'object' ? opts : {};
    settings.data.online = !!o.online;
    settings.data.desktopNotify = !!o.desktopNotify;
    settings.data.language = o.language === 'en' ? 'en' : 'es';
    settings.data.onboarded = true;
    settings.data.statsAsked = true;
    await settings.save();
    await setStats(!!o.stats);
    checkOnline({ force: true }).catch(() => {});
    return fullState();
  });
  handle('onboarding:reset', async () => { await settings.set('onboarded', false); return fullState(); });
  handle('settings:setOnline', async (on) => {
    await settings.set('online', !!on);
    const res = await refreshCatalog();
    if (on) checkOnline({ force: true }).catch(() => {});
    return res;
  });
  handle('settings:setStats', async (on) => { await setStats(!!on); return fullState(); });
  handle('settings:setDesktopNotify', async (on) => { await settings.set('desktopNotify', !!on); return fullState(); });
  handle('notif:test', () => {
    if (!Notification.isSupported()) throw new Error('Este sistema no admite avisos de escritorio');
    const n = new Notification({ title: 'ZUENA Plugins', body: 'Así te vamos a avisar cuando haya plugins nuevos o actualizaciones.', icon: path.join(ROOT, 'build', 'icon.png') });
    n.show();
    return true;
  });
  handle('online:check', async () => { await checkOnline({ force: true }); return { state: await fullState(), catalog, ...catalogMeta }; });
  handle('launcher:download', () => downloadLauncherUpdate());
  handle('launcher:restart', async () => {
    if (launcherInfo.phase !== 'ready' || !launcherInfo.staged) throw new Error('La actualización todavía no está lista');
    await launcherUpdate.applyOnExit({ pid: process.pid, appRoot: dist.runningRoot(), staged: launcherInfo.staged, exePath: process.execPath });
    setTimeout(() => app.quit(), 150);
    return true;
  });
  handle('app:installSelf', async (opts) => {
    if (!app.isPackaged) throw new Error('La instalación en el equipo solo funciona en la app empaquetada');
    const o = opts && typeof opts === 'object' ? opts : {};
    const exe = await dist.installFromPortable({ onProgress: (m) => send('app:installProgress', m) });
    dist.createShortcuts({ shell, exePath: exe, desktop: o.desktop !== false, startMenu: o.startMenu !== false });
    await settings.save();
    setTimeout(() => { app.relaunch({ execPath: exe, args: [] }); app.exit(0); }, 600);
    return { exe };
  });
  handle('app:shortcuts', (opts) => {
    if (!app.isPackaged) throw new Error('Solo en la app empaquetada');
    const o = opts && typeof opts === 'object' ? opts : {};
    return dist.createShortcuts({ shell, exePath: process.execPath, desktop: !!o.desktop, startMenu: !!o.startMenu });
  });
  handle('win:minimize', () => win.minimize());
  handle('win:toggleMax', () => (win.isMaximized() ? win.unmaximize() : win.maximize()));
  handle('win:close', () => win.close());
}

// ---------- Ventana ----------
function createWindow() {
  const pref = settings.get('theme');
  const startDark = pref === 'dark' || (pref === 'system' && nativeTheme.shouldUseDarkColors);
  win = new BrowserWindow({
    width: 1360, height: 820, minWidth: 1100, minHeight: 700,
    frame: false, show: false, backgroundColor: startDark ? '#101112' : '#f5f5f3', title: 'ZUENA Plugins',
    icon: path.join(ROOT, 'build', 'icon.png'),
    webPreferences: {
      preload: path.join(ROOT, 'src', 'preload', 'preload.js'),
      contextIsolation: true, nodeIntegration: false, sandbox: true, spellcheck: false,
    },
  });
  win.once('ready-to-show', () => win.show());
  // Al volver a la ventana se relee el disco: refleja plugins actualizados o borrados fuera del launcher.
  win.on('focus', () => { broadcastState().catch(() => {}); checkOnline().catch(() => {}); });
  win.on('maximize', () => send('win:maximized', true));
  win.on('unmaximize', () => send('win:maximized', false));
  // Reposo: con la ventana minimizada u oculta el renderer pausa animaciones y vídeo.
  win.on('minimize', () => send('win:visible', false));
  win.on('hide', () => send('win:visible', false));
  win.on('restore', () => send('win:visible', true));
  win.on('show', () => send('win:visible', true));
  win.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  win.webContents.on('will-navigate', (e) => e.preventDefault());
  win.loadFile(RENDERER_INDEX);
}

app.on('second-instance', () => {
  if (!win) return;
  if (win.isMinimized()) win.restore();
  win.focus();
});

/** Desinstalación pedida por Windows (o por la prueba E2E con FREEVST_UNINSTALL_YES=1). */
async function runUninstall() {
  const title = 'Desinstalar ZUENA Plugins';
  if (!app.isPackaged || !dist.isInstalledCopy()) {
    dialog.showMessageBoxSync({ type: 'info', title, message: 'Esta copia no está instalada en el equipo.', detail: 'Para quitarla, borrá su carpeta.' });
    return;
  }
  if (!primaryInstance) {
    dialog.showMessageBoxSync({ type: 'warning', title, message: 'Cerrá ZUENA Plugins y volvé a intentar.' });
    return;
  }
  const confirmed = process.env.FREEVST_UNINSTALL_YES === '1' || dialog.showMessageBoxSync({
    type: 'question', title, buttons: ['Desinstalar', 'Cancelar'], defaultId: 0, cancelId: 1, noLink: true,
    message: '¿Desinstalar ZUENA Plugins?',
    detail: 'Se quitan el launcher, sus accesos directos y su entrada en Windows.\nLos plugins de tu carpeta VST3 no se borran: tus proyectos los pueden seguir usando.',
  }) === 0;
  if (!confirmed) return;
  const env = process.env;
  await uninstallCore.uninstall({
    installDir: dist.runningRoot(), expectedDir: dist.installRoot(), pid: process.pid,
    shortcuts: [path.join(env.USERPROFILE || '', 'Desktop', 'ZUENA Plugins.lnk'),
      path.join(env.APPDATA || '', 'Microsoft', 'Windows', 'Start Menu', 'Programs', 'ZUENA Plugins.lnk')],
  });
}

app.whenReady().then(async () => {
  if (UNINSTALL) {
    try { await runUninstall(); } catch (e) { dialog.showErrorBox('Desinstalar ZUENA Plugins', e.message); }
    app.exit(0);
    return;
  }
  Menu.setApplicationMenu(null);
  settings = new JsonStore(userFile('settings.json'), {
    installDir: null, catalogUrl: '', theme: 'light', watch: [], notifications: [], updateNotified: {},
    // Guía de inicio: la conexión a internet y los avisos se piden ahí; hasta entonces todo es local.
    onboarded: process.env.FREEVST_ONBOARDED === '1', online: false, desktopNotify: true, lastCheck: 0, launcherNotified: '',
    // Estadísticas anónimas: apagadas hasta que el usuario las active (guía de inicio o Ajustes).
    stats: false, installId: '', statsLast: 0, statsSig: '', statsAsked: false,
  });
  installedStore = new JsonStore(userFile('installed.json'), { plugins: {} });
  await Promise.all([settings.load(), installedStore.load()]);
  nativeTheme.themeSource = ['light', 'dark', 'system'].includes(settings.get('theme')) ? settings.get('theme') : 'light';
  const unpackedPkgs = `${ROOT}.unpacked`;
  const packagesDir = (await exists(path.join(unpackedPkgs, 'catalog', 'packages')))
    ? path.join(unpackedPkgs, 'catalog', 'packages')
    : path.join(ROOT, 'catalog', 'packages');
  packageServer = await startPackageServer(packagesDir);
  if (DEMO) demoServer = await startDemoServer();
  registerIpc();
  createWindow();
  // Copia instalada: el acceso del menú Inicio con el AppUserModelID habilita los avisos de Windows.
  if (app.isPackaged && dist.isInstalledCopy()) {
    try { dist.ensureStartMenuShortcut({ shell, exePath: process.execPath }); } catch { /* sin acceso directo: solo avisos internos */ }
    // Entrada en «Aplicaciones instaladas» (desinstalar desde Windows), con la versión que corre ahora.
    uninstallCore.writeEntry({ installDir: dist.runningRoot(), version: app.getVersion() }).catch(() => {});
  }
  setInterval(() => { checkOnline({ force: true }).catch(() => {}); }, CHECK_EVERY_MS).unref?.();
  // Al arrancar: el catálogo lo pide la interfaz; la versión del launcher se revisa aparte.
  setTimeout(() => { checkLauncher().catch(() => {}); pingStats().catch(() => {}); settings.set('lastCheck', Date.now()); }, 4000);
  // Quien ya hizo la guía antes de 0.2.3 no vio el permiso: se le cuenta una sola vez dónde está.
  if (settings.get('onboarded') && !settings.get('statsAsked')) {
    await settings.set('statsAsked', true);
    notify('info', 'Nuevo: podés ayudarnos con estadísticas anónimas, sin cuenta ni datos personales. Activalas en Ajustes › Conexión.');
  }
});

app.on('window-all-closed', () => {
  for (const j of jobs.values()) j.controller.abort();
  demoServer?.close();
  packageServer?.close();
  app.quit();
});
