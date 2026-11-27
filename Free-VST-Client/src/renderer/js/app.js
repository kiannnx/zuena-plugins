import { sectionIcon, pluginIcon, featureIcon, ui } from './icons.js';
import { esc, html, frag, $, $$, setAccent, bytes, eta, ago, compareVersions, EASE_OUT, reducedMotion } from './util.js';
import { visualKind, visualSvg, deviceControls, mountVisuals } from './visuals.js';
import { startOnboarding } from './onboarding.js';

const hub = window.hub;

const SECTIONS = [
  { id: 'featured', label: 'Destacado', t: ['#3a3e48', '#16181d'] },
  { id: 'catalog', label: 'Catálogo', t: ['#4b8dff', '#1546d8'] },
  { id: 'installed', label: 'Instalados', t: ['#9a6bff', '#4b1fd6'] },
  { id: 'downloads', label: 'Descargas', t: ['#ff9a8f', '#f0466e'] },
  { id: 'settings', label: 'Ajustes', t: ['#ffffff', '#dfe8f8'], light: true },
];
const FILTERS = [
  { id: 'all', label: 'Todos' },
  { id: 'available', label: 'Disponibles' },
  { id: 'installed', label: 'Instalados' },
  { id: 'soon', label: 'Próximamente' },
];
const ACTIVE_PHASES = ['download', 'install'];

const S = {
  section: 'featured',
  catalog: { plugins: [], featured: null },
  meta: { source: 'bundled', warning: null, errors: [] },
  app: { installed: {}, watch: [], notifications: [], platform: 'win32', demo: false },
  jobs: [],
  query: '',
  filter: 'all',
  wallIndex: 0,
  sheetId: null,
  catalogLoading: true,
};

// ===================================================================== modelo

const pluginById = (id) => S.catalog.plugins.find((p) => p.id === id);
const activeJob = (id) => S.jobs.find((j) => j.pluginId === id && ACTIVE_PHASES.includes(j.phase));
const jobPct = (j) => (j.total ? Math.min(100, Math.round((j.received / j.total) * 100)) : 0);

function status(p) {
  const job = activeJob(p.id);
  if (job) return { kind: 'busy', job };
  const inst = S.app.installed[p.id];
  if (inst) {
    if (p.status === 'available' && p.downloads[S.app.platform] && (!inst.version || compareVersions(p.version, inst.version) > 0)) {
      return { kind: inst.version ? 'update' : 'installed-ext', inst };
    }
    return { kind: 'installed', inst };
  }
  if (p.status === 'coming-soon') return { kind: 'soon' };
  if (!p.downloads[S.app.platform]) return { kind: 'unsupported' };
  return { kind: 'available' };
}

function chipFor(p) {
  const s = status(p);
  switch (s.kind) {
    case 'busy': return `<span class="chip chip--busy">${s.job.phase === 'install' ? 'Instalando…' : `Descargando ${jobPct(s.job)}%`}</span>`;
    case 'update': return '<span class="chip chip--update">Actualización disponible</span>';
    case 'installed':
    case 'installed-ext': return '<span class="chip chip--ok">Instalado</span>';
    case 'soon': return '<span class="chip chip--soon">Próximamente</span>';
    case 'unsupported': return '<span class="chip">No disponible en tu sistema</span>';
    default: return '<span class="chip chip--ok">Gratis</span>';
  }
}

/** Acción principal de un plugin. null cuando no hay ninguna acción posible. */
function primaryAction(p) {
  const s = status(p);
  switch (s.kind) {
    case 'busy': return { label: 'Ver progreso', run: () => go('downloads') };
    case 'update': return { label: `Actualizar a v${p.version}`, primary: true, run: () => install(p.id) };
    case 'installed-ext': return { label: 'Reinstalar', primary: true, run: () => install(p.id) };
    case 'installed': return { label: 'Mostrar en carpeta', icon: ui.folder(), run: () => act(() => hub.revealPlugin(p.id)) };
    case 'soon': return S.app.watch.includes(p.id)
      ? { label: 'Te vamos a avisar', run: () => toggleWatch(p.id) }
      : { label: 'Avisarme cuando salga', primary: true, icon: ui.bell(), run: () => toggleWatch(p.id) };
    case 'unsupported': return null;
    default: return { label: 'Instalar gratis', primary: true, run: () => install(p.id) };
  }
}

function filteredPlugins() {
  const q = S.query.trim().toLowerCase();
  return S.catalog.plugins.filter((p) => {
    const k = status(p).kind;
    if (S.filter === 'available' && !['available', 'update', 'installed-ext', 'busy'].includes(k)) return false;
    if (S.filter === 'installed' && !['installed', 'update', 'installed-ext'].includes(k)) return false;
    if (S.filter === 'soon' && k !== 'soon') return false;
    if (!q) return true;
    return [p.name, p.tagline, p.category, ...p.tags].some((t) => t.toLowerCase().includes(q));
  });
}

const updatesCount = () => S.catalog.plugins.filter((p) => status(p).kind === 'update').length;

// ===================================================================== acciones

async function act(fn, okMsg) {
  try {
    const r = await fn();
    if (okMsg) toast('success', okMsg);
    return r;
  } catch (e) {
    toast('error', cleanErr(e));
    return undefined;
  }
}
const cleanErr = (e) => String(e?.message ?? e).replace(/^Error invoking remote method '[^']+': (Error: )?/, '');

async function install(id) {
  const p = pluginById(id);
  const jobId = await act(() => hub.install(id));
  if (jobId) toast('info', `Descargando ${p.name}…`);
}

/** «Novedades» de la versión disponible (changelog del catálogo), desplegable en la fila de Instalados. */
function newsFor(p) {
  const entry = p?.changelog?.find((c) => c.version === p.version);
  if (!entry?.items?.length) return '';
  return `<details class="row__news"><summary>Novedades de v${esc(p.version)}</summary><ul>${entry.items.map((i) => `<li>${esc(i)}</li>`).join('')}</ul></details>`;
}

/** Espera a que un trabajo de instalación termine (listo, error o cancelado). */
function waitJob(jobId, timeoutMs = 15 * 60 * 1000) {
  const end = Date.now() + timeoutMs;
  return new Promise((resolve) => {
    const tick = () => {
      const j = S.jobs.find((x) => x.id === jobId);
      if (j && !ACTIVE_PHASES.includes(j.phase)) return resolve(j.phase);
      if (Date.now() > end) return resolve('timeout');
      setTimeout(tick, 300);
    };
    tick();
  });
}

/** Actualiza todos los plugins con versión nueva, de a uno: en Program Files cada instalación puede pedir
    permiso de administrador y así no se abren varios avisos a la vez. */
async function updateAll() {
  if (S.updatingAll) return;
  const pending = S.catalog.plugins.filter((p) => status(p).kind === 'update');
  if (!pending.length) return;
  S.updatingAll = true;
  renderData();
  let ok = 0;
  try {
    for (const p of pending) {
      const jobId = await act(() => hub.install(p.id));
      if (!jobId) continue;
      toast('info', `Actualizando ${p.name} a v${p.version}…`);
      if ((await waitJob(jobId)) === 'done') ok += 1;
    }
  } finally {
    S.updatingAll = false;
    renderData();
  }
  if (ok === pending.length) toast('success', ok === 1 ? `${pending[0].name} actualizado` : `${ok} plugins actualizados`);
  else toast('error', `Se actualizaron ${ok} de ${pending.length}. Revisá la pestaña Descargas.`);
}

async function uninstall(id) {
  const p = pluginById(id);
  const name = p?.name ?? id;
  const ok = await confirmModal(`¿Desinstalar ${name}?`, `Se borran los archivos del plugin de la carpeta VST3. Los proyectos de tu DAW que lo usen lo van a mostrar como faltante.`, 'Desinstalar');
  if (!ok) return;
  const st = await act(() => hub.uninstall(id), `${name} desinstalado`);
  if (st) applyAppState(st);
}

async function toggleWatch(id) {
  const list = await act(() => hub.toggleWatch(id));
  if (!list) return;
  S.app.watch = list;
  const p = pluginById(id);
  toast('info', list.includes(id) ? `Te vamos a avisar cuando ${p.name} esté disponible` : `Ya no vas a recibir avisos de ${p.name}`);
  renderData();
}

async function reloadCatalog(showToast = true) {
  S.catalogLoading = true;
  const res = await act(() => hub.loadCatalog());
  S.catalogLoading = false;
  if (!res) return;
  S.catalog = res.catalog;
  S.meta = { source: res.source, warning: res.warning, errors: res.errors };
  applyAppState(res.state, false);
  S.wallIndex = Math.min(S.wallIndex, Math.max(0, filteredPlugins().length - 1));
  if (res.warning) toast('error', res.warning);
  else if (showToast) toast('success', `Catálogo actualizado · ${res.catalog.plugins.length} plugins`);
  renderData();
}

const darkMedia = window.matchMedia('(prefers-color-scheme: dark)');

function effectiveDark(pref = S.app.theme) {
  return pref === 'dark' || (pref === 'system' && darkMedia.matches);
}

function applyTheme(pref = S.app.theme) {
  const dark = effectiveDark(pref);
  document.documentElement.dataset.scheme = dark ? 'dark' : 'light';
  syncStageTheme();
  const btn = $('#btn-theme');
  if (btn) {
    btn.textContent = dark ? 'Claro' : 'Oscuro';
    btn.title = dark ? 'Cambiar a modo claro' : 'Cambiar a modo oscuro';
  }
}

function applyAppState(st, render = true) {
  S.app = st;
  S.jobs = st.jobs ?? S.jobs;
  applyTheme(st.theme);
  renderNotifications();
  if (render) renderData();
}

function go(section) {
  if (!SECTIONS.some((s) => s.id === section) || section === S.section) return;
  S.section = section;
  renderDock();
  renderScenesVisibility();
  renderHero(true);
  if (section === 'settings') renderSettings();
}

// ===================================================================== dock

function buildDock() {
  const dock = $('#dock');
  dock.innerHTML = '';
  for (const s of SECTIONS) {
    const b = frag(html`<button class="tile${s.light ? ' tile--light' : ''}" data-section="${s.id}" aria-label="${s.label}">
      <span class="tile__icon">${sectionIcon[s.id]()}</span><span class="tile__label">${s.label}</span></button>`);
    b.style.setProperty('--t1', s.t[0]);
    b.style.setProperty('--t2', s.t[1]);
    b.addEventListener('click', () => go(s.id));
    dock.append(b);
  }
  renderDock();
}

function renderDock() {
  for (const b of $$('.tile')) {
    const active = b.dataset.section === S.section;
    b.classList.toggle('is-active', active);
    b.setAttribute('aria-current', active ? 'page' : 'false');
    const n = b.dataset.section === 'downloads' ? S.jobs.filter((j) => ACTIVE_PHASES.includes(j.phase)).length
      : b.dataset.section === 'installed' ? updatesCount()
        : b.dataset.section === 'settings' && ['available', 'ready', 'error'].includes(S.app.launcher?.phase) ? 1 : 0;
    let badge = $('.tile__count', b);
    if (n > 0) {
      if (!badge) { badge = frag('<span class="tile__count"></span>'); b.append(badge); }
      badge.textContent = n;
      badge.title = b.dataset.section === 'downloads' ? `${n} en curso` : `${n} actualizaciones`;
    } else badge?.remove();
  }
}

// ===================================================================== hero

const pillHtml = (a, i) => `<button class="pill${a.primary ? ' pill--primary' : ''}${a.danger ? ' pill--danger' : ''}${a.active ? ' is-active' : ''}${a.sm ? ' pill--sm' : ''}" data-act="${i}"${a.disabled ? ' disabled' : ''}>${a.icon ?? ''}${esc(a.label)}</button>`;

function bindActions(root, actions) {
  $$('[data-act]', root).forEach((b) => b.addEventListener('click', () => actions[Number(b.dataset.act)]?.run()));
}

function heroModel() {
  const plugins = S.catalog.plugins;
  switch (S.section) {
    case 'featured': {
      const p = pluginById(S.catalog.featured);
      if (!p) return { eyebrow: 'Destacado', title: S.catalogLoading ? 'Cargando…' : 'Sin plugins', sub: S.catalogLoading ? 'Leyendo el catálogo de plugins gratuitos.' : 'El catálogo está vacío.', actions: [] };
      const prim = primaryAction(p);
      return {
        eyebrow: `Destacado · ${p.category}`, title: p.name, sub: p.tagline || p.description, chip: chipFor(p),
        actions: [...(prim ? [prim] : []), { label: 'Ver detalles', run: () => openSheet(p.id) }],
      };
    }
    case 'catalog': {
      const avail = plugins.filter((p) => p.status === 'available').length;
      return {
        eyebrow: 'Catálogo', title: S.query ? `“${S.query}”` : 'Todo gratis',
        sub: S.query ? `${filteredPlugins().length} resultados en el catálogo.` : `${plugins.length} plugins · ${avail} listos para descargar. Sin registro y sin pagos.`,
        actions: [
          ...FILTERS.map((f) => ({ label: f.label, sm: true, active: S.filter === f.id, run: () => setFilter(f.id) })),
          ...(S.query ? [{ label: 'Limpiar búsqueda', icon: ui.close(), run: () => setQuery('') }] : []),
        ],
      };
    }
    case 'installed': {
      const n = Object.keys(S.app.installed).length;
      const u = updatesCount();
      return {
        eyebrow: 'Tu estudio', title: 'Instalados',
        sub: n ? `${n} plugin${n === 1 ? '' : 's'} en tu carpeta VST3${u ? ` · ${u} con actualización` : ''}.` : 'Todavía no instalaste ningún plugin.',
        meta: [['Instalados', n], ['Actualizaciones', u]],
        actions: [
          ...(u ? [{ label: S.updatingAll ? 'Actualizando…' : `Actualizar todo (${u})`, primary: true, run: updateAll }] : []),
          { label: 'Abrir carpeta VST3', icon: ui.folder(), run: () => act(() => hub.openInstallDir()) },
          { label: 'Buscar actualizaciones', run: () => reloadCatalog() },
        ],
      };
    }
    case 'downloads': {
      const active = S.jobs.filter((j) => ACTIVE_PHASES.includes(j.phase)).length;
      const finished = S.jobs.length - active;
      return {
        eyebrow: 'Cola', title: 'Descargas',
        sub: active ? `${active} en curso. Podés seguir navegando mientras se instalan.` : 'No hay descargas en curso.',
        meta: [['En curso', active], ['Finalizadas', finished]],
        actions: [
          ...(active ? [{ label: 'Cancelar todas', danger: true, run: cancelAll }] : []),
          ...(finished ? [{ label: 'Limpiar finalizadas', run: () => act(() => hub.clearFinishedJobs()) }] : []),
          ...(!active ? [{ label: 'Ir al catálogo', primary: true, run: () => go('catalog') }] : []),
        ],
      };
    }
    default:
      return {
        eyebrow: 'Preferencias', title: 'Ajustes', sub: 'Carpeta de instalación, origen del catálogo e información del launcher.',
        actions: [
          { label: 'Guía de instalación', run: openHelp },
          { label: 'Recargar catálogo', run: () => reloadCatalog() },
        ],
      };
  }
}

function heroEl() {
  const m = heroModel();
  const el = frag(html`<div class="hero__inner">
    <div class="hero__eyebrow">${esc(m.eyebrow)}</div>
    <h1 class="hero__title">${esc(m.title)}</h1>
    <p class="hero__sub">${esc(m.sub)}</p>
    ${m.chip ? `<div class="hero__chip" style="margin-top:14px">${m.chip}</div>` : ''}
    ${m.meta ? `<div class="hero__meta">${m.meta.map(([k, v]) => `<div><b>${v}</b>${esc(k)}</div>`).join('')}</div>` : ''}
    ${m.actions.length ? `<div class="hero__actions">${m.actions.map(pillHtml).join('')}</div>` : ''}
  </div>`);
  bindActions(el, m.actions);
  return el;
}

function renderHero(animate = false) {
  const hero = $('#hero');
  const next = heroEl();
  const old = $$('.hero__inner', hero);
  if (!animate || reducedMotion()) {
    old.forEach((o) => o.remove());
    hero.append(next);
    return;
  }
  old.forEach((o) => {
    if (o.classList.contains('is-leaving')) return o.remove();
    o.classList.add('is-leaving');
    o.animate([{ opacity: 1, transform: 'none' }, { opacity: 0, transform: 'translateY(-16px)' }], { duration: 220, easing: 'ease-in', fill: 'forwards' })
      .finished.finally(() => o.remove());
  });
  hero.append(next);
  [...next.children].forEach((c, i) => c.animate(
    [{ opacity: 0, transform: 'translateY(22px)' }, { opacity: 1, transform: 'none' }],
    { duration: 680, delay: 140 + i * 70, easing: EASE_OUT, fill: 'backwards' },
  ));
}

// ===================================================================== escenas

function buildScenes() {
  const root = $('#scenes');
  root.innerHTML = '';
  for (const s of SECTIONS) {
    const sc = frag(`<div class="scene" data-scene="${s.id}"><div class="scene__glow"></div><div class="scene__content"></div></div>`);
    root.append(sc);
  }
  const content = (id) => $(`.scene[data-scene="${id}"] .scene__content`);
  content('catalog').replaceWith(buildWall());
  setAccent($('.scene[data-scene="catalog"] .scene__glow'), ['#5ff0ff', '#b9a8ff']);
  setAccent($('.scene[data-scene="installed"] .scene__glow'), ['#b9a8ff', '#ffb3e1']);
  setAccent($('.scene[data-scene="downloads"] .scene__glow'), ['#ffb3e1', '#ffd28a']);
  setAccent($('.scene[data-scene="settings"] .scene__glow'), ['#5ff0ff', '#ffd28a']);
  renderScenesVisibility();
}

function renderScenesVisibility() {
  const idx = SECTIONS.findIndex((s) => s.id === S.section);
  $$('.scene').forEach((sc) => {
    const i = SECTIONS.findIndex((s) => s.id === sc.dataset.scene);
    sc.classList.toggle('is-active', i === idx);
    sc.classList.toggle('is-before', i < idx);
    sc.inert = i !== idx;
  });
}

// --- Destacado
function renderFeatured() {
  const sc = $('.scene[data-scene="featured"]');
  const p = pluginById(S.catalog.featured);
  const slot = $('.scene__content', sc);
  if (!p) {
    slot.replaceChildren();
    return;
  }
  setAccent($('.scene__glow', sc), p.accent);
  if (slot.dataset.pid === p.id) return;
  slot.dataset.pid = p.id;
  const kind = visualKind(p);
  const el = frag(html`<div class="feature3d" data-kind="${kind}">
    <div class="feature3d__aura"></div>
    <div class="feature3d__shadow"></div>
    <div class="feature3d__float">
      <button class="device art device--${kind}" title="Ver detalles de ${esc(p.name)}">
        <span class="device__aurora"><span></span><span></span></span>
        <span class="device__cat">${esc(p.category)}</span>
        <span class="device__name">${esc(p.name)}</span>
        <span class="device__icon">${pluginIcon[p.icon]()}</span>
        <span class="device__screen">${visualSvg(kind)}</span>
        <span class="device__controls">${deviceControls(kind)}</span>
      </button>
      <span class="orb orb--1"></span><span class="orb orb--2"></span><span class="orb orb--3"></span>
    </div>
  </div>`);
  setAccent(el, p.accent);
  $('.device', el).addEventListener('click', () => openSheet(p.id));
  bindTilt(sc, el);
  slot.replaceChildren(el);
  mountVisuals(el);
}

/** Inclinación suave del dispositivo siguiendo el mouse (solo transform; nada con movimiento reducido). */
let tiltBound = null;
function bindTilt(scene, root) {
  const target = $('.feature3d__float', root);
  if (tiltBound) scene.removeEventListener('pointermove', tiltBound);
  if (reducedMotion()) return;
  let raf = 0;
  tiltBound = (e) => {
    if (raf) return;
    raf = requestAnimationFrame(() => {
      raf = 0;
      const r = scene.getBoundingClientRect();
      const nx = (e.clientX - r.left) / r.width - 0.5;
      const ny = (e.clientY - r.top) / r.height - 0.5;
      target.style.setProperty('--tilt-y', `${(nx * 10).toFixed(2)}deg`);
      target.style.setProperty('--tilt-x', `${(-ny * 7).toFixed(2)}deg`);
    });
  };
  scene.addEventListener('pointermove', tiltBound);
  scene.addEventListener('pointerleave', () => { target.style.setProperty('--tilt-y', '0deg'); target.style.setProperty('--tilt-x', '0deg'); });
}

// --- Catálogo (carrusel)
// Coverflow que se adapta al ancho disponible: hasta dos tarjetas por lado, siempre dentro del área.
// Arrastre que sigue al puntero y se encaja según la velocidad, rueda acumulada (sirve para trackpads),
// flechas y puntos debajo, teclado (← → Inicio Fin con el foco en el carrusel) y anuncio para lectores.
const cardEls = new Map();
let wallEl;
let wallTrack;
let wallCount;
let wallDots;
let wallLive;
// Escalas: central 1, vecina .8, segunda .62. La vecina queda entera al lado de la central; la segunda asoma.
const WALL_S1 = 0.8;
const WALL_S2 = 0.62;
const wallGeo = { step1: 230, step2: 60, cardW: 248, show2: true };
let wallDrag = null;
let suppressClick = false;

function buildWall() {
  const wall = frag(html`<div class="scene__content wall" role="region" aria-roledescription="carrusel" aria-label="Catálogo de plugins">
    <div class="wall__viewport"><div class="wall__track"></div></div>
    <div class="wall__controls">
      <button class="wall__nav wall__nav--l" title="Anterior" aria-label="Plugin anterior">${ui.chevronL()}</button>
      <div class="wall__dots" role="tablist" aria-label="Elegir plugin"></div>
      <button class="wall__nav wall__nav--r" title="Siguiente" aria-label="Plugin siguiente">${ui.chevronR()}</button>
    </div>
    <div class="wall__count" aria-hidden="true"></div>
    <div class="sr-only" aria-live="polite"></div>
  </div>`);
  wallEl = wall;
  wallTrack = $('.wall__track', wall);
  wallCount = $('.wall__count', wall);
  wallDots = $('.wall__dots', wall);
  wallLive = $('.sr-only', wall);
  $('.wall__nav--l', wall).addEventListener('click', () => moveWall(-1));
  $('.wall__nav--r', wall).addEventListener('click', () => moveWall(1));
  wallDots.addEventListener('click', (e) => {
    const b = e.target.closest('[data-i]');
    if (b) goWall(Number(b.dataset.i));
  });

  // Rueda acumulada: un "clic" de rueda o un gesto de trackpad mueven una tarjeta, sin saltos dobles.
  let wheelAcc = 0;
  let wheelLock = 0;
  wall.addEventListener('wheel', (e) => {
    if (e.target.closest('.wall__controls')) return;
    e.preventDefault();
    const now = performance.now();
    if (now < wheelLock) return;
    wheelAcc += Math.abs(e.deltaX) > Math.abs(e.deltaY) ? e.deltaX : e.deltaY;
    if (Math.abs(wheelAcc) < 40) return;
    moveWall(wheelAcc > 0 ? 1 : -1);
    wheelAcc = 0;
    wheelLock = now + 260;
  }, { passive: false });

  // Arrastre: las tarjetas siguen al puntero; al soltar se encaja según posición y velocidad.
  wall.addEventListener('pointerdown', (e) => {
    if (e.button !== 0 || e.target.closest('.wall__controls')) return;
    suppressClick = false;
    wallDrag = { id: e.pointerId, x0: e.clientX, x: e.clientX, t: performance.now(), v: 0, moved: false };
  });
  window.addEventListener('pointermove', (e) => {
    const d = wallDrag;
    if (!d || e.pointerId !== d.id) return;
    const now = performance.now();
    d.v = (e.clientX - d.x) / Math.max(1, now - d.t); // px/ms
    d.x = e.clientX;
    d.t = now;
    if (!d.moved && Math.abs(d.x - d.x0) > 6) {
      d.moved = true;
      wallEl.classList.add('is-dragging');
    }
    if (d.moved) layoutWall(dragPosition(d));
  });
  const endDrag = (e) => {
    const d = wallDrag;
    if (!d || (e && e.pointerId !== d.id)) return;
    wallDrag = null;
    wallEl.classList.remove('is-dragging');
    if (!d.moved) return;
    suppressClick = true; // el clic que llega tras soltar no abre la ficha
    const n = filteredPlugins().length;
    // Inercia: se proyectan 120 ms de movimiento (px) y se pasan a tarjetas; como mucho una tarjeta extra.
    const fling = Math.max(-1, Math.min(1, (d.v * 120) / wallGeo.step1));
    const pos = dragPosition(d) - fling;
    S.wallIndex = Math.max(0, Math.min(n - 1, Math.round(pos)));
    layoutWall();
  };
  window.addEventListener('pointerup', endDrag);
  window.addEventListener('pointercancel', endDrag);

  // Teclado con el foco dentro del carrusel
  wall.addEventListener('keydown', (e) => {
    const n = filteredPlugins().length;
    const map = { ArrowLeft: S.wallIndex - 1, ArrowRight: S.wallIndex + 1, Home: 0, End: n - 1 };
    if (!(e.key in map) || !n) return;
    e.preventDefault();
    e.stopPropagation();
    goWall(map[e.key], true);
  });

  new ResizeObserver(() => { measureWall(); layoutWall(); }).observe(wall);
  return wall;
}

/** Posición fraccional durante el arrastre, con resistencia en los extremos. */
function dragPosition(d) {
  const n = filteredPlugins().length;
  let pos = S.wallIndex - (d.x - d.x0) / wallGeo.step1;
  if (pos < 0) pos = -Math.min(0.5, -pos * 0.35);
  if (pos > n - 1) pos = n - 1 + Math.min(0.5, (pos - (n - 1)) * 0.35);
  return pos;
}

/** Espaciado según el ancho real: la segunda tarjeta lateral siempre cabe dentro del área. */
function measureWall() {
  if (!wallEl) return;
  const w = wallEl.clientWidth;
  const cardW = w < 640 ? 196 : w < 800 ? 220 : 248;
  const half = w / 2 - 18;
  wallGeo.cardW = cardW;
  // Vecina entera: su borde interior empieza 14 px después del borde de la central.
  wallGeo.step1 = Math.min(cardW / 2 + (cardW * WALL_S1) / 2 + 14, half - (cardW * WALL_S1) / 2);
  // Segunda: lo que quepa hasta el borde del área (con el desvanecido), como mucho 42 % de tarjeta.
  const room2 = half - wallGeo.step1 - (cardW * WALL_S2) / 2;
  wallGeo.step2 = Math.max(0, Math.min(cardW * 0.42, room2));
  wallGeo.show2 = room2 > cardW * 0.12;
  wallEl.style.setProperty('--card-w', `${cardW}px`);
}

function goWall(i, focus = false) {
  const n = filteredPlugins().length;
  const next = Math.max(0, Math.min(n - 1, i));
  if (next !== S.wallIndex) {
    S.wallIndex = next;
    layoutWall();
  }
  if (focus) cardEls.get(filteredPlugins()[next]?.id)?.focus({ preventScroll: true });
}

function moveWall(delta) {
  goWall(S.wallIndex + delta);
}

function cardEl(p) {
  const el = frag(html`<button class="pcard" data-id="${esc(p.id)}">
    <span class="pcard__art art" data-kind="${visualKind(p)}"><span class="pcard__chip"></span><span class="pcard__icon">${pluginIcon[p.icon]()}</span><span class="pcard__viz">${visualSvg(visualKind(p), { compact: true })}</span></span>
    <span class="pcard__body"><span class="pcard__cat">${esc(p.category)}</span><span class="pcard__name">${esc(p.name)}</span><span class="pcard__tag">${esc(p.tagline || p.category)}</span></span>
    <span class="pcard__shine"></span>
  </button>`);
  setAccent(el, p.accent);
  el.addEventListener('click', () => {
    if (suppressClick) { suppressClick = false; return; }
    const idx = filteredPlugins().findIndex((x) => x.id === p.id);
    if (idx === S.wallIndex) openSheet(p.id);
    else goWall(idx);
  });
  return el;
}

function renderWall() {
  const list = filteredPlugins();
  const ids = new Set(list.map((p) => p.id));
  for (const [id, el] of cardEls) {
    if (!ids.has(id) || !pluginById(id)) { el.remove(); cardEls.delete(id); }
  }
  for (const p of list) {
    if (!cardEls.has(p.id)) {
      const el = cardEl(p);
      el.style.opacity = '0';
      el.style.transform = 'translate3d(0, 60px, -200px) scale(.9)';
      wallTrack.append(el);
      cardEls.set(p.id, el);
    }
    const chip = $('.pcard__chip', cardEls.get(p.id));
    const c = chipFor(p);
    if (chip.innerHTML !== c) chip.innerHTML = c;
  }
  S.wallIndex = Math.max(0, Math.min(S.wallIndex, list.length - 1));
  wallDots.innerHTML = list.map((p, i) => `<button class="wall__dot" role="tab" data-i="${i}" aria-label="${esc(p.name)}" title="${esc(p.name)}"></button>`).join('');
  wallEl.classList.toggle('is-single', list.length <= 1);
  let empty = $('.wall .empty');
  if (list.length === 0) {
    if (!empty) {
      empty = frag(html`<div class="empty" style="position:absolute;inset:0;display:flex;flex-direction:column;justify-content:center">
        <div class="empty__art art">${ui.search()}</div>
        <div class="empty__title">Sin resultados</div>
        <p class="empty__text">Ningún plugin coincide con la búsqueda o el filtro.</p>
        <div><button class="pill pill--primary">Ver todo el catálogo</button></div></div>`);
      setAccent($('.empty__art', empty), ['#5ff0ff', '#b9a8ff']);
      $('.pill', empty).addEventListener('click', () => { S.filter = 'all'; setQuery(''); });
      wallEl.append(empty);
    }
  } else empty?.remove();
  measureWall();
  requestAnimationFrame(() => layoutWall());
}

/** Coloca las tarjetas. `pos` fraccional durante el arrastre; por defecto, la tarjeta actual. */
function layoutWall(pos = S.wallIndex) {
  const list = filteredPlugins();
  const { step1, step2 } = wallGeo;
  list.forEach((p, i) => {
    const el = cardEls.get(p.id);
    if (!el) return;
    const off = i - pos;
    const a = Math.abs(off);
    const s = Math.sign(off);
    const x = s * (a <= 1 ? a * step1 : step1 + (Math.min(a, 2.6) - 1) * step2);
    const scale = a <= 1 ? 1 - (1 - WALL_S1) * a : WALL_S1 - (WALL_S1 - WALL_S2) * Math.min(1, a - 1);
    const rot = -s * Math.min(a, 1.4) * 16;
    const z = -Math.min(a, 2.6) * 90;
    el.style.transform = `translate3d(${x.toFixed(1)}px, 0, ${z.toFixed(1)}px) rotateY(${rot.toFixed(2)}deg) scale(${scale.toFixed(3)})`;
    const fadeFrom = wallGeo.show2 ? 1.6 : 1.05;
    el.style.opacity = a > 2.5 ? '0' : String(Math.max(0, Math.min(1, 1 - Math.max(0, a - fadeFrom) / 0.9)).toFixed(3));
    el.style.setProperty('--dim', String((Math.min(a, 2) * 0.14).toFixed(3)));
    el.style.zIndex = String(100 - Math.round(a * 10));
    el.style.pointerEvents = a > 2.2 ? 'none' : '';
    const center = Math.round(pos) === i;
    el.tabIndex = center ? 0 : -1;
    el.setAttribute('aria-hidden', a > 2.2 ? 'true' : 'false');
    const wasCenter = el.classList.contains('is-center');
    el.classList.toggle('is-center', center && !wallDrag?.moved);
    if (center && !wasCenter && !wallDrag?.moved) {
      const shine = $('.pcard__shine', el);
      shine.style.animation = 'none';
      void shine.offsetWidth;
      shine.style.animation = '';
    }
  });
  const n = list.length;
  $('.wall__nav--l', wallEl).disabled = S.wallIndex <= 0;
  $('.wall__nav--r', wallEl).disabled = S.wallIndex >= n - 1;
  $$('.wall__dot', wallDots).forEach((d, i) => {
    d.classList.toggle('is-active', i === S.wallIndex);
    d.setAttribute('aria-selected', String(i === S.wallIndex));
  });
  wallCount.textContent = n ? `${S.wallIndex + 1} / ${n}` : '';
  const cur = list[S.wallIndex];
  const said = cur ? `${cur.name}, ${S.wallIndex + 1} de ${n}` : '';
  if (wallLive && wallLive.textContent !== said && pos === S.wallIndex) wallLive.textContent = said;
}

function setFilter(f) {
  S.filter = f;
  S.wallIndex = 0;
  renderWall();
  renderHero(false);
}

function setQuery(q) {
  S.query = q;
  $('#search-input').value = q;
  S.wallIndex = 0;
  renderWall();
  if (S.section === 'catalog') renderHero(false);
}

// --- Instalados
function renderInstalled() {
  const slot = $('.scene[data-scene="installed"] .scene__content');
  const entries = Object.entries(S.app.installed);
  if (!entries.length) {
    const el = frag(html`<div class="panel glass"><div class="empty">
      <div class="empty__art art">${sectionIcon.installed()}</div>
      <div class="empty__title">Tu carpeta está esperando</div>
      <p class="empty__text">Instalá un plugin del catálogo y va a aparecer acá, listo para abrir en tu DAW.</p>
      <button class="pill pill--primary">Explorar catálogo</button></div></div>`);
    setAccent($('.empty__art', el), ['#b9a8ff', '#ffb3e1']);
    $('.pill', el).addEventListener('click', () => go('catalog'));
    slot.replaceChildren(el);
    return;
  }
  const panel = frag(html`<div class="panel glass">
    <div class="panel__head"><span class="panel__title">Plugins instalados</span><span class="chip">${entries.length}</span></div>
    <div class="panel__body"></div></div>`);
  const body = $('.panel__body', panel);
  entries.forEach(([id, inst], i) => {
    const p = pluginById(id);
    const s = p ? status(p) : { kind: 'installed' };
    const name = p?.name ?? id;
    const ver = inst.version ? `v${inst.version}` : 'versión desconocida';
    const row = frag(html`<div class="row">
      <span class="row__icon art">${pluginIcon[p?.icon ?? 'wave']()}</span>
      <div style="min-width:0">
        <div class="row__name">${esc(name)} ${s.kind === 'update' ? `<span class="chip chip--update">v${esc(p.version)} disponible</span>` : ''}</div>
        <div class="row__meta">${esc(ver)}${inst.installedAt ? ` · instalado ${esc(ago(inst.installedAt))}` : inst.external ? ' · detectado en la carpeta' : ''}</div>
        <div class="row__meta row__path" title="${esc(inst.files[0])}">${esc(inst.files[0])}</div>
        ${s.kind === 'update' ? newsFor(p) : ''}
      </div>
      <div class="row__actions">
        ${s.kind === 'update' ? '<button class="pill pill--primary pill--sm" data-a="update">Actualizar</button>' : ''}
        ${s.kind === 'busy' ? `<span class="chip chip--busy">${s.job.phase === 'install' ? 'Instalando…' : `${jobPct(s.job)}%`}</span>` : ''}
        ${p ? `<button class="icon-btn" data-a="info" title="Detalles">${ui.info()}</button>` : ''}
        <button class="icon-btn" data-a="reveal" title="Mostrar en carpeta">${ui.folder()}</button>
        <button class="icon-btn icon-btn--danger" data-a="remove" title="Desinstalar"${s.kind === 'busy' ? ' disabled' : ''}>${ui.trash()}</button>
      </div></div>`);
    row.style.setProperty('--i', i);
    setAccent(row, p?.accent ?? ['#b9a8ff', '#5ff0ff']);
    $$('[data-a]', row).forEach((b) => b.addEventListener('click', () => {
      const a = b.dataset.a;
      if (a === 'update') install(id);
      else if (a === 'info') openSheet(id);
      else if (a === 'reveal') act(() => hub.revealPlugin(id));
      else if (a === 'remove') uninstall(id);
    }));
    body.append(row);
  });
  slot.replaceChildren(panel);
}

// --- Descargas
const jobRows = new Map();
function renderDownloads() {
  const slot = $('.scene[data-scene="downloads"] .scene__content');
  if (!S.jobs.length) {
    jobRows.clear();
    if ($('.empty', slot)) return;
    const el = frag(html`<div class="panel glass"><div class="empty">
      <div class="empty__art art">${sectionIcon.downloads()}</div>
      <div class="empty__title">Nada descargando</div>
      <p class="empty__text">Cuando instales un plugin, acá vas a ver el progreso de descarga, la verificación y la instalación.</p>
      <button class="pill pill--primary">Ir al catálogo</button></div></div>`);
    setAccent($('.empty__art', el), ['#ffb3e1', '#ffd28a']);
    $('.pill', el).addEventListener('click', () => go('catalog'));
    slot.replaceChildren(el);
    return;
  }
  let panel = $('.panel', slot);
  if (!panel || $('.empty', slot)) {
    panel = frag('<div class="panel glass"><div class="panel__head"><span class="panel__title">Actividad</span></div><div class="panel__body"></div></div>');
    slot.replaceChildren(panel);
    jobRows.clear();
  }
  const body = $('.panel__body', panel);
  const ordered = [...S.jobs].sort((a, b) => b.startedAt - a.startedAt);
  for (const [id, row] of jobRows) if (!S.jobs.some((j) => j.id === id)) { row.remove(); jobRows.delete(id); }
  ordered.forEach((j, i) => {
    let row = jobRows.get(j.id);
    if (!row) {
      const p = pluginById(j.pluginId);
      row = frag(html`<div class="row">
        <span class="row__icon art">${pluginIcon[p?.icon ?? 'wave']()}</span>
        <div style="min-width:0">
          <div class="row__name">${esc(j.name)} <span class="row__meta" style="margin:0">v${esc(j.version)}</span></div>
          <div class="row__meta" data-r="text"></div>
          <div class="progress"><span class="progress__fill"></span></div>
        </div>
        <div class="row__actions" data-r="actions"></div></div>`);
      row.style.setProperty('--i', i);
      setAccent(row, p?.accent ?? ['#b9a8ff', '#5ff0ff']);
      jobRows.set(j.id, row);
    }
    updateJobRow(row, j);
    if (body.children[i] !== row) body.insertBefore(row, body.children[i] ?? null);
  });
}

function jobText(j) {
  switch (j.phase) {
    case 'download': {
      const left = eta(j.total - j.received, j.bytesPerSec);
      return `Descargando · ${bytes(j.received)} de ${bytes(j.total)} · ${jobPct(j)}%${j.bytesPerSec ? ` · ${bytes(j.bytesPerSec)}/s` : ''}${left ? ` · ${left}` : ''}`;
    }
    case 'install': return 'Verificado con SHA-256 · instalando en la carpeta VST3…';
    case 'done': return j.message;
    case 'canceled': return 'Cancelada';
    default: return `Error: ${j.message}`;
  }
}

function updateJobRow(row, j) {
  const text = $('[data-r="text"]', row);
  const t = jobText(j);
  if (text.textContent !== t) text.textContent = t;
  const bar = $('.progress', row);
  const fill = $('.progress__fill', row);
  bar.classList.toggle('progress--error', j.phase === 'error' || j.phase === 'canceled');
  bar.classList.toggle('progress--done', j.phase === 'done');
  fill.classList.toggle('is-indeterminate', j.phase === 'install');
  if (j.phase !== 'install') fill.style.transform = `scaleX(${j.phase === 'download' ? jobPct(j) / 100 : 1})`;
  else fill.style.transform = '';
  const actions = $('[data-r="actions"]', row);
  if (actions.dataset.phase === j.phase) return;
  actions.dataset.phase = j.phase;
  actions.innerHTML = '';
  const add = (label, cls, fn) => {
    const b = frag(`<button class="pill pill--sm ${cls}">${esc(label)}</button>`);
    b.addEventListener('click', fn);
    actions.append(b);
  };
  if (j.phase === 'download') add('Cancelar', 'pill--danger', () => act(() => hub.cancel(j.id)));
  if (j.phase === 'done') add('Mostrar', '', () => act(() => hub.revealPlugin(j.pluginId)));
  if (j.phase === 'error' || j.phase === 'canceled') add('Reintentar', 'pill--primary', () => install(j.pluginId));
}

async function cancelAll() {
  for (const j of S.jobs) if (j.phase === 'download') await act(() => hub.cancel(j.id));
}

// --- Ajustes
/** Estado del propio launcher: versión, actualización disponible/descargando/lista e instalación. */
function launcherBlock(st) {
  const l = st.launcher ?? {};
  if (st.demo) return '';
  const pct = l.total ? Math.round((l.received / l.total) * 100) : 0;
  const notes = l.update?.notes?.length ? `<ul class="lu__notes">${l.update.notes.map((n) => `<li>${esc(n)}</li>`).join('')}</ul>` : '';
  let body = `<div class="lu__line"><span class="lu__ver">ZUENA Plugins v${esc(st.appVersion)}</span>${st.online ? '<span class="chip chip--ok">Al día</span>' : ''}</div>`;
  if (l.update && ['available', 'download', 'ready', 'error'].includes(l.phase)) {
    const action = l.phase === 'download'
      ? `<div class="progress"><span class="progress__fill" style="transform:scaleX(${pct / 100})"></span></div><div class="lu__pct">Descargando · ${pct}%</div>`
      : l.phase === 'ready' ? '<button class="pill pill--primary pill--sm" data-a="launcher-restart">Reiniciar y actualizar</button>'
        : `<button class="pill pill--primary pill--sm" data-a="launcher-download">${l.phase === 'error' ? 'Reintentar' : 'Descargar actualización'}</button>${l.phase === 'error' ? `<div class="note">${esc(l.message)}</div>` : ''}`;
    body = `<div class="lu__line"><span class="lu__ver">ZUENA Plugins v${esc(st.appVersion)}</span><span class="chip chip--update">v${esc(l.update.version)} disponible</span></div>${notes}<div class="field__actions">${action}</div>`;
  }
  if (l.packaged && !l.installed) {
    body += `<div class="note note--info">Esta copia se abrió desde una carpeta descargada. Instalala en el equipo para tener accesos directos y actualizaciones automáticas.</div>
      <div class="field__actions"><button class="pill pill--sm" data-a="install-self">Instalar en este equipo</button></div>`;
  }
  return `<div class="lu">${body}</div>`;
}

function renderSettings() {
  const slot = $('.scene[data-scene="settings"] .scene__content');
  if (slot.contains(document.activeElement) && document.activeElement.tagName === 'INPUT') return;
  const st = S.app;
  const custom = st.installDir !== st.defaultInstallDir;
  const src = { remote: st.catalogUrl ? 'Servidor personalizado' : 'Catálogo oficial en línea (GitHub)', cache: 'Copia guardada (sin conexión)', bundled: 'Catálogo integrado' }[S.meta.source];
  const curTheme = st.theme || 'light';
  const panel = frag(html`<div class="panel glass"><div class="panel__body" style="padding-top:12px">
    <div class="field">
      <div class="field__label">Apariencia</div>
      <div class="field__actions">
        <button class="pill pill--sm${curTheme === 'light' ? ' is-active' : ''}" data-theme="light">Claro</button>
        <button class="pill pill--sm${curTheme === 'dark' ? ' is-active' : ''}" data-theme="dark">Oscuro</button>
        <button class="pill pill--sm${curTheme === 'system' ? ' is-active' : ''}" data-theme="system">Automático</button>
      </div>
      <div class="field__help">Tokens oficiales del sistema Punto de ZUENA; el logo invierte sus tonos en modo oscuro.</div>
    </div>
    <div class="field" data-field="online">
      <div class="field__label">Conexión y actualizaciones</div>
      <label class="set-row"><span><span class="set-row__title">Buscar plugins y actualizaciones en internet</span>
        <span class="set-row__desc">Catálogo público de ZUENA en GitHub. Sin cuenta y sin datos tuyos.</span></span>
        <input type="checkbox" class="switch" data-set="online"${st.online ? ' checked' : ''}${st.demo ? ' disabled' : ''} /></label>
      <label class="set-row"><span><span class="set-row__title">Estadísticas anónimas</span>
        <span class="set-row__desc">Un número al azar por instalación, la versión del launcher y de Windows, y qué plugins tenés. Nada personal. Si lo apagás, se borra lo enviado.</span></span>
        <input type="checkbox" class="switch" data-set="stats"${st.stats ? ' checked' : ''}${st.demo ? ' disabled' : ''} /></label>
      <label class="set-row"><span><span class="set-row__title">Avisos de Windows</span>
        <span class="set-row__desc">Plugins nuevos y actualizaciones, aunque la ventana esté en segundo plano.</span></span>
        <input type="checkbox" class="switch" data-set="desktopNotify"${st.desktopNotify ? ' checked' : ''} /></label>
      <div class="field__actions">
        <button class="pill pill--sm" data-a="check"${st.online && !st.demo ? '' : ' disabled'}>Buscar ahora</button>
        <button class="pill pill--sm" data-a="test-notif">${ui.bell()}Probar aviso</button>
        <button class="pill pill--sm" data-a="guide">Ver la guía de inicio</button>
      </div>
      <div class="field__help">${st.online ? (st.lastCheck ? `Última revisión: ${esc(ago(st.lastCheck))}. Se revisa al abrir, al volver a la ventana y cada 6 horas.` : 'Se revisa al abrir, al volver a la ventana y cada 6 horas.') : 'Sin conexión: se usan los plugins incluidos en el launcher.'}</div>
      ${launcherBlock(st)}
    </div>
    <div class="field">
      <div class="field__label">Carpeta de instalación VST3</div>
      <div class="field__value">${esc(st.installDir)}</div>
      ${st.demo ? '<div class="note note--info">Modo demo: los paquetes de prueba se instalan en una carpeta aislada, nunca en tu carpeta VST3 real.</div>'
        : !custom && st.platform === 'win32' ? '<div class="field__help">Carpeta estándar que todos los DAW escanean. Windows te va a pedir permiso de administrador (UAC) al instalar o desinstalar.</div>' : ''}
      <div class="field__actions">
        <button class="pill pill--sm" data-a="choose">${ui.folder()}Cambiar…</button>
        <button class="pill pill--sm" data-a="open">Abrir carpeta</button>
        ${custom ? '<button class="pill pill--sm" data-a="reset">Restablecer predeterminada</button>' : ''}
      </div>
    </div>
    <div class="field">
      <div class="field__label">Origen del catálogo</div>
      ${st.demo ? '<div class="field__value">Servidor demo local (127.0.0.1)</div>'
        : html`<input class="text-input" id="catalog-url" placeholder="https://… /catalog.json (vacío = catálogo oficial)" value="${esc(st.catalogUrl)}" spellcheck="false" />
      <div class="field__actions"><button class="pill pill--sm pill--primary" data-a="save-url">Guardar y recargar</button>
      ${st.catalogUrl ? '<button class="pill pill--sm" data-a="clear-url">Usar catálogo oficial</button>' : ''}</div>`}
      <dl class="kv"><dt>Fuente actual</dt><dd>${esc(src)}</dd><dt>Plugins</dt><dd>${S.catalog.plugins.length}</dd>
      ${S.meta.errors.length ? `<dt>Entradas descartadas</dt><dd>${S.meta.errors.length}</dd>` : ''}</dl>
      ${S.meta.warning ? `<div class="note">${esc(S.meta.warning)}</div>` : ''}
      ${S.meta.errors.length ? `<div class="note">${S.meta.errors.slice(0, 4).map(esc).join('<br>')}</div>` : ''}
    </div>
    <div class="field">
      <div class="field__label">Acerca de</div>
      <dl class="kv"><dt>ZUENA Plugins</dt><dd>v${esc(st.appVersion)}</dd><dt>Sistema</dt><dd>${st.platform === 'win32' ? 'Windows' : st.platform === 'darwin' ? 'macOS' : esc(st.platform)}</dd>
      <dt>Modo</dt><dd>${st.demo ? 'Demo (paquetes de prueba locales)' : 'Normal'}</dd><dt>Precio</dt><dd>Gratis</dd></dl>
    </div>
  </div></div>`);
  const on = (a, fn) => $(`[data-a="${a}"]`, panel)?.addEventListener('click', fn);
  $$('[data-theme]', panel).forEach((b) => b.addEventListener('click', async () => {
    const r = await act(() => hub.setTheme(b.dataset.theme));
    if (r) applyAppState(r);
  }));
  $$('[data-set]', panel).forEach((sw) => sw.addEventListener('change', async () => {
    if (sw.dataset.set === 'online') {
      const r = await act(() => hub.setOnline(sw.checked));
      if (!r) { sw.checked = !sw.checked; return; }
      S.catalog = r.catalog;
      S.meta = { source: r.source, warning: r.warning, errors: r.errors };
      applyAppState(r.state);
      toast(r.warning ? 'error' : 'success', r.warning ?? (sw.checked ? 'Conectado al catálogo en línea' : 'Sin conexión: usando los plugins incluidos'));
    } else if (sw.dataset.set === 'stats') {
      const r = await act(() => hub.setStats(sw.checked));
      if (!r) { sw.checked = !sw.checked; return; }
      applyAppState(r);
      toast('success', sw.checked ? 'Gracias. Las estadísticas anónimas están activadas' : 'Estadísticas apagadas: se borró lo enviado desde este equipo');
    } else {
      const r = await act(() => hub.setDesktopNotify(sw.checked));
      if (r) applyAppState(r);
    }
  }));
  on('check', async () => {
    const r = await act(() => hub.checkOnline());
    if (!r) return;
    S.catalog = r.catalog;
    S.meta = { source: r.source, warning: r.warning, errors: r.errors };
    applyAppState(r.state);
    toast(r.warning ? 'error' : 'success', r.warning ?? 'Catálogo y versión del launcher revisados');
  });
  on('test-notif', () => act(() => hub.testNotification(), 'Aviso de prueba enviado a Windows'));
  on('guide', async () => {
    const r = await act(() => hub.resetOnboarding());
    if (r) startOnboarding({ hub, state: r, toast, onTheme: (x) => applyAppState(x), onDone: async (next) => { applyAppState(next); await reloadCatalog(false); } });
  });
  on('launcher-download', () => act(() => hub.downloadLauncherUpdate()));
  on('launcher-restart', () => act(() => hub.restartToUpdate()));
  on('install-self', () => act(() => hub.installSelf({ desktop: true, startMenu: true })));
  on('choose', async () => { const r = await act(() => hub.chooseInstallDir()); if (r) applyAppState(r); });
  on('open', () => act(() => hub.openInstallDir()));
  on('reset', async () => { const r = await act(() => hub.resetInstallDir(), 'Carpeta restablecida'); if (r) applyAppState(r); });
  const saveUrl = async (v) => {
    const r = await act(() => hub.setCatalogUrl(v));
    if (!r) return;
    S.catalog = r.catalog;
    S.meta = { source: r.source, warning: r.warning, errors: r.errors };
    $('#catalog-url')?.blur();
    applyAppState(r.state);
    toast(r.warning ? 'error' : 'success', r.warning ?? `Catálogo cargado · ${r.catalog.plugins.length} plugins`);
  };
  on('save-url', () => saveUrl($('#catalog-url', panel).value));
  on('clear-url', () => saveUrl(''));
  $('#catalog-url', panel)?.addEventListener('keydown', (e) => { if (e.key === 'Enter') saveUrl(e.target.value); });
  slot.replaceChildren(panel);
}

// ===================================================================== ficha de detalle

/** Texto del catálogo con *énfasis* → <em> (serif itálica). Se escapa ANTES: no entra HTML del catálogo. */
const emph = (s) => esc(s).replace(/\*([^*]{1,60})\*/g, '<em>$1</em>');
const paragraphs = (s) => String(s ?? '').split(/\n{2,}/).map((t) => t.trim()).filter(Boolean);
const fmtDate = (iso) => {
  const d = new Date(`${iso}T12:00:00`);
  return Number.isNaN(d.getTime()) ? iso : d.toLocaleDateString('es', { day: 'numeric', month: 'short', year: 'numeric' });
};
const isDark = () => document.documentElement.dataset.scheme === 'dark';
/** Vídeo y póster del panel según el tema (la variante oscura es opcional). */
const stageMedia = (p) => (isDark() && p.media?.videoDark
  ? { video: p.media.videoDark, poster: p.media.posterDark }
  : { video: p.media?.video, poster: p.media?.poster });

const gallery = { items: [], index: 0 };

function openSheet(id) {
  const p = pluginById(id);
  if (!p) return;
  S.sheetId = id;
  const body = $('#sheet-body');
  const dl = p.downloads[S.app.platform];
  const media = stageMedia(p);
  const facts = [['Versión', `v${p.version}`], ['Tamaño', dl ? bytes(dl.size) : '—'], ['Precio', 'Gratis']];
  let n = 0;
  const section = (key, title, inner) => {
    n += 1;
    return html`<section class="pd__section pd-in" style="--i:${n}" aria-labelledby="pd-h-${key}">
      <h2 class="pd__h" id="pd-h-${key}"><span class="pd__num">${String(n).padStart(2, '0')}</span>${title}</h2>${inner}</section>`;
  };
  const about = paragraphs(p.description);

  body.innerHTML = html`
    <div class="pd">
      <div class="pd__scroll">
        <header class="pd__head pd-in" style="--i:0">
          <div class="pd__eyebrow"><span class="pd__spark"></span>${esc(p.category)} · VST3</div>
          <h1 class="pd__title sheet__name">${esc(p.name)}</h1>
          <p class="pd__headline">${p.headline ? emph(p.headline) : esc(p.tagline || '')}</p>
          ${p.summary ? `<p class="pd__summary">${esc(p.summary)}</p>` : ''}
          <dl class="pd__facts">${facts.map(([k, v]) => `<div class="pd__fact"><dt>${esc(k)}</dt><dd>${esc(v)}</dd></div>`).join('')}</dl>
        </header>
        ${p.highlights.length ? section('feat', 'Lo esencial', `<div class="feats">${p.highlights.map((h, i) => html`
          <article class="feat" style="--k:${i}">
            <span class="feat__icon">${(featureIcon[h.icon] ?? featureIcon.bands)()}</span>
            <h3 class="feat__title">${esc(h.title)}</h3>
            <p class="feat__text">${esc(h.text)}</p>
          </article>`).join('')}</div>`) : ''}
        ${p.gallery.length ? section('gal', 'Galería', `<div class="shots">${p.gallery.map((g, i) => html`
          <button class="shot${i === 0 ? ' shot--hero' : ''}" data-shot="${i}" aria-label="Ampliar: ${esc(g.caption || `imagen ${i + 1}`)}">
            <img src="${esc(g.src)}" alt="${esc(g.caption)}" loading="lazy" decoding="async" draggable="false">
            ${g.caption ? `<span class="shot__cap">${esc(g.caption)}</span>` : ''}
          </button>`).join('')}</div>`) : ''}
        ${p.specs.length ? section('spec', 'Especificaciones', `<dl class="specs">${p.specs.map(([k, v]) => `<div class="spec"><dt>${esc(k)}</dt><dd>${esc(v)}</dd></div>`).join('')}</dl>`) : ''}
        ${p.changelog.length ? section('news', 'Novedades', `<ol class="changes">${p.changelog.map((c) => html`
          <li class="change${c.version === p.version ? ' is-current' : ''}">
            <div class="change__head"><span class="change__v">v${esc(c.version)}</span>
              ${c.version === p.version ? '<span class="chip chip--ok">Versión actual</span>' : ''}
              ${c.date ? `<time class="change__date" datetime="${esc(c.date)}">${esc(fmtDate(c.date))}</time>` : ''}</div>
            <ul class="change__list">${c.items.map((it) => `<li>${esc(it)}</li>`).join('')}</ul>
          </li>`).join('')}</ol>`) : ''}
        ${about.length || p.tags.length ? section('about', 'Acerca de', html`
          ${about.map((t) => `<p class="pd__about">${esc(t)}</p>`).join('')}
          ${p.tags.length ? `<div class="pd__tags">${p.tags.map((t) => `<span class="chip">${esc(t)}</span>`).join('')}</div>` : ''}`) : ''}
      </div>
      <div class="sheet__foot"></div>
    </div>
    <aside class="stage${media.video ? ' has-video' : ''}" aria-label="Presentación de ${esc(p.name)}">
      ${media.video
        ? `<video class="stage__video" src="${esc(media.video)}" poster="${esc(media.poster)}" muted loop playsinline preload="auto" disablepictureinpicture disableremoteplayback aria-hidden="true"></video>`
        : `<div class="stage__art art" data-kind="${visualKind(p)}"><span class="device__aurora"><span></span><span></span></span><span class="stage__viz">${visualSvg(visualKind(p))}</span><span class="stage__icon">${pluginIcon[p.icon]()}</span></div>`}
      <button class="icon-btn stage__close sheet__close" title="Cerrar (Esc)" aria-label="Cerrar">${ui.close()}</button>
      <div class="stage__bar">
        <span class="stage__label">${media.video ? 'Presentación' : esc(p.category)}</span>
        ${media.video ? `<button class="stage__toggle" aria-label="Pausar presentación" title="Pausar">${ui.pause()}</button>` : ''}
      </div>
    </aside>
    <div class="lightbox" hidden role="dialog" aria-modal="true" aria-label="Galería de ${esc(p.name)}">
      <figure class="lightbox__fig"><img class="lightbox__img" alt=""><figcaption class="lightbox__cap"><span data-lb="cap"></span><span class="lightbox__n" data-lb="n"></span></figcaption></figure>
      <button class="icon-btn lightbox__close" aria-label="Cerrar imagen" title="Cerrar (Esc)">${ui.close()}</button>
      <button class="lightbox__nav lightbox__nav--prev" data-lb="prev" aria-label="Imagen anterior">${ui.chevronL()}</button>
      <button class="lightbox__nav lightbox__nav--next" data-lb="next" aria-label="Imagen siguiente">${ui.chevronR()}</button>
    </div>`;

  setAccent(body, p.accent);
  mountVisuals(body);
  document.body.classList.add('sheet-open');
  $('.stage__close', body).addEventListener('click', closeSheet);
  setupStage(body);
  gallery.items = p.gallery;
  gallery.index = 0;
  $$('[data-shot]', body).forEach((b) => b.addEventListener('click', () => openLightbox(Number(b.dataset.shot))));
  $$('.shot img', body).forEach((img) => img.addEventListener('error', () => img.closest('.shot')?.classList.add('is-broken'), { once: true }));
  const lb = $('.lightbox', body);
  $('.lightbox__close', lb).addEventListener('click', closeLightbox);
  $('[data-lb="prev"]', lb).addEventListener('click', () => stepLightbox(-1));
  $('[data-lb="next"]', lb).addEventListener('click', () => stepLightbox(1));
  lb.addEventListener('click', (e) => { if (e.target === lb) closeLightbox(); });
  refreshSheet();
  showOverlay($('#sheet'));
}

/** Vídeo de fondo en bucle: arranca solo (salvo movimiento reducido) y se puede pausar. */
function setupStage(body) {
  const v = $('.stage__video', body);
  const toggle = $('.stage__toggle', body);
  if (!v || !toggle) return;
  const sync = () => {
    const paused = v.paused;
    toggle.innerHTML = paused ? ui.play() : ui.pause();
    toggle.setAttribute('aria-label', paused ? 'Reproducir presentación' : 'Pausar presentación');
    toggle.title = paused ? 'Reproducir' : 'Pausar';
    $('.stage', body).classList.toggle('is-paused', paused);
  };
  v.addEventListener('play', sync);
  v.addEventListener('pause', sync);
  v.addEventListener('error', () => { $('.stage', body).classList.add('is-broken'); toggle.hidden = true; }, { once: true });
  toggle.addEventListener('click', () => (v.paused ? v.play().catch(() => {}) : v.pause()));
  if (!reducedMotion()) v.play().catch(() => sync());
  sync();
}

/** Cambia el vídeo del panel al tema actual sin cerrar la ficha. */
function syncStageTheme() {
  const p = S.sheetId && pluginById(S.sheetId);
  const v = $('#sheet-body .stage__video');
  if (!p || !v) return;
  const m = stageMedia(p);
  if (v.getAttribute('src') === m.video) return;
  const wasPlaying = !v.paused;
  v.poster = m.poster;
  v.src = m.video;
  if (wasPlaying) v.play().catch(() => {});
}

const lightboxEl = () => $('#sheet-body .lightbox');
const lightboxOpen = () => { const lb = lightboxEl(); return !!lb && !lb.hidden; };

function renderLightbox() {
  const lb = lightboxEl();
  const g = gallery.items[gallery.index];
  if (!lb || !g) return;
  const img = $('.lightbox__img', lb);
  img.src = g.src;
  img.alt = g.caption || '';
  $('[data-lb="cap"]', lb).textContent = g.caption || '';
  $('[data-lb="n"]', lb).textContent = `${gallery.index + 1} / ${gallery.items.length}`;
  const many = gallery.items.length > 1;
  $$('.lightbox__nav', lb).forEach((b) => { b.hidden = !many; });
}
function openLightbox(i) {
  const lb = lightboxEl();
  if (!lb || !gallery.items[i]) return;
  gallery.index = i;
  renderLightbox();
  lb.hidden = false;
  requestAnimationFrame(() => lb.classList.add('is-open'));
  $('.lightbox__close', lb).focus({ preventScroll: true });
}
function closeLightbox() {
  const lb = lightboxEl();
  if (!lb || lb.hidden) return;
  lb.classList.remove('is-open');
  const done = () => { if (!lb.classList.contains('is-open')) lb.hidden = true; };
  if (reducedMotion()) done(); else setTimeout(done, 220);
  $(`[data-shot="${gallery.index}"]`, $('#sheet-body'))?.focus({ preventScroll: true });
}
function stepLightbox(d) {
  if (!gallery.items.length) return;
  gallery.index = (gallery.index + d + gallery.items.length) % gallery.items.length;
  renderLightbox();
}

function refreshSheet() {
  if (!S.sheetId) return;
  const p = pluginById(S.sheetId);
  const foot = $('#sheet-body .sheet__foot');
  if (!p || !foot) return;
  const s = status(p);
  const actions = [];
  const prim = primaryAction(p);
  if (s.kind === 'busy') {
    if (s.job.phase === 'download') actions.push({ label: 'Cancelar descarga', danger: true, run: () => act(() => hub.cancel(s.job.id)) });
  } else if (prim) actions.push(prim);
  if (s.kind === 'installed' && p.status === 'available' && p.downloads[S.app.platform]) actions.push({ label: 'Reinstalar', run: () => install(p.id) });
  if (['installed', 'update', 'installed-ext'].includes(s.kind)) {
    if (s.kind !== 'installed') actions.push({ label: 'Mostrar en carpeta', icon: ui.folder(), run: () => act(() => hub.revealPlugin(p.id)) });
    actions.push({ label: 'Desinstalar', danger: true, icon: ui.trash(), run: () => uninstall(p.id) });
  }
  const sig = JSON.stringify([s.kind, s.job?.phase, actions.map((a) => a.label)]);
  if (foot.dataset.sig !== sig) {
    foot.dataset.sig = sig;
    const dl = p.downloads[S.app.platform];
    const note = s.kind === 'unsupported' ? 'Todavía no hay versión para tu sistema operativo.'
      : s.kind === 'soon' ? 'Te avisamos en notificaciones cuando esté disponible.'
        : dl ? `Gratis · ${bytes(dl.size)} · verificado con SHA-256` : '';
    foot.innerHTML = html`
      ${s.kind === 'busy' ? '<div class="sheet__progress-label"><span data-r="t"></span><span data-r="p"></span></div><div class="progress" style="margin-bottom:14px"><span class="progress__fill"></span></div>' : ''}
      <div class="sheet__status">${chipFor(p)}${note ? `<span class="sheet__note">${esc(note)}</span>` : ''}</div>
      <div class="sheet__actions">${actions.map(pillHtml).join('')}</div>`;
    bindActions(foot, actions);
  }
  if (s.kind === 'busy') {
    $('[data-r="t"]', foot).textContent = s.job.phase === 'install' ? 'Instalando…' : `Descargando · ${bytes(s.job.received)} de ${bytes(s.job.total)}`;
    $('[data-r="p"]', foot).textContent = s.job.phase === 'install' ? '' : `${jobPct(s.job)}%`;
    const fill = $('.progress__fill', foot);
    fill.classList.toggle('is-indeterminate', s.job.phase === 'install');
    fill.style.transform = s.job.phase === 'install' ? '' : `scaleX(${jobPct(s.job) / 100})`;
  }
  const chip = $('.sheet__status .chip', foot);
  const c = chipFor(p);
  if (chip && chip.outerHTML !== c) chip.outerHTML = c;
}

function closeSheet() {
  if (!S.sheetId) return;
  S.sheetId = null;
  const v = $('#sheet-body .stage__video');
  if (v) {
    // Libera el decodificador: el vídeo no sigue corriendo con la ficha cerrada.
    v.pause();
    v.removeAttribute('src');
    v.load();
  }
  gallery.items = [];
  document.body.classList.remove('sheet-open');
  hideOverlay($('#sheet'));
}

// ===================================================================== overlays

function showOverlay(o) {
  o.hidden = false;
  requestAnimationFrame(() => requestAnimationFrame(() => o.classList.add('is-open')));
}
function hideOverlay(o) {
  o.classList.remove('is-open');
  const done = () => { if (!o.classList.contains('is-open')) o.hidden = true; };
  if (reducedMotion()) return done();
  setTimeout(done, 320);
}
let modalResolve = null;
function openModal(markup, wide = false) {
  const body = $('#modal-body');
  body.className = `modal glass${wide ? ' modal--wide' : ''}`;
  body.innerHTML = markup;
  showOverlay($('#modal'));
  return body;
}
function closeModal(result = false) {
  hideOverlay($('#modal'));
  modalResolve?.(result);
  modalResolve = null;
}
function confirmModal(title, text, okLabel) {
  const body = openModal(html`<div class="modal__title">${esc(title)}</div><p class="modal__text">${esc(text)}</p>
    <div class="modal__actions"><button class="pill" data-m="no">Cancelar</button><button class="pill pill--primary" data-m="yes">${esc(okLabel)}</button></div>`);
  $('[data-m="no"]', body).addEventListener('click', () => closeModal(false));
  $('[data-m="yes"]', body).addEventListener('click', () => closeModal(true));
  setTimeout(() => $('[data-m="yes"]', body).focus(), 50);
  return new Promise((r) => { modalResolve = r; });
}
function openHelp() {
  const body = openModal(html`<div class="modal__title">Guía de instalación</div>
    <p class="modal__text">ZUENA Plugins descarga cada plugin, comprueba su huella SHA-256 y copia el archivo <code>.vst3</code> a la carpeta que escanea tu DAW.</p>
    <table class="help-table"><thead><tr><th>Sistema</th><th>Carpeta VST3</th></tr></thead><tbody>
      <tr><td>Windows</td><td><code>C:\\Program Files\\Common Files\\VST3</code></td></tr>
      <tr><td>macOS</td><td><code>/Library/Audio/Plug-Ins/VST3</code> o <code>~/Library/Audio/Plug-Ins/VST3</code></td></tr></tbody></table>
    <h3>Después de instalar, volvé a escanear los plugins en tu DAW</h3>
    <table class="help-table"><tbody>
      <tr><td>Ableton Live</td><td>Configuración › Plug-Ins › Volver a escanear</td></tr>
      <tr><td>FL Studio</td><td>Options › Manage plugins › Find plugins</td></tr>
      <tr><td>Reaper</td><td>Preferences › Plug-ins › VST › Re-scan</td></tr>
      <tr><td>Bitwig / Studio One / Cubase</td><td>Se detecta al reiniciar el programa</td></tr></tbody></table>
    <h3>Permisos</h3>
    <p class="modal__text">La carpeta estándar de Windows es del sistema: al instalar o desinstalar, Windows te va a pedir confirmación de administrador. Si preferís evitarlo, elegí otra carpeta en Ajustes y agregala a las rutas de plugins de tu DAW.</p>
    <h3 data-help="unsigned">Si Windows bloquea el plugin</h3>
    <p class="modal__text">Los plugins de ZUENA son gratis y no llevan firma digital de editor. En algunas PC, Windows lo nota:</p>
    <table class="help-table"><tbody>
      <tr><td>«Windows protegió tu PC»</td><td>Tocá <b>Más información</b> › <b>Ejecutar de todas formas</b>.</td></tr>
      <tr><td>Tu DAW no muestra ZUENA EQ aunque está instalado</td><td>Puede ser <b>Smart App Control</b>, que bloquea el código sin firma. Revisalo en Seguridad de Windows › Control de aplicaciones y navegador › Smart App Control.</td></tr></tbody></table>
    <p class="modal__text">Desactivar Smart App Control es decisión tuya: en Windows 11 no se puede volver a activar sin reinstalar el sistema.</p>
    <div class="modal__actions"><button class="pill" data-m="settings">Ir a Ajustes</button><button class="pill pill--primary" data-m="ok">Entendido</button></div>`, true);
  $('[data-m="ok"]', body).addEventListener('click', () => closeModal());
  $('[data-m="settings"]', body).addEventListener('click', () => { closeModal(); go('settings'); });
}

// ===================================================================== toasts y notificaciones

const LEVEL_ICON = { success: ui.check, error: ui.warn, info: ui.info };
function toast(level, text) {
  const t = frag(`<div class="toast"><span class="notif__icon notif__icon--${level}">${LEVEL_ICON[level]()}</span><span>${esc(text)}</span></div>`);
  const box = $('#toasts');
  box.append(t);
  while (box.children.length > 4) box.firstElementChild.remove();
  setTimeout(() => { t.classList.add('is-leaving'); setTimeout(() => t.remove(), 320); }, level === 'error' ? 6500 : 3800);
}

function renderNotifications() {
  const list = S.app.notifications ?? [];
  const unread = list.filter((n) => !n.read).length;
  const badge = $('#notif-badge');
  badge.hidden = unread === 0;
  badge.textContent = unread > 9 ? '9+' : String(unread);
  $('#notif-list').innerHTML = list.length
    ? list.map((n) => `<li class="notif${n.read ? '' : ' is-unread'}"><span class="notif__icon notif__icon--${n.level}">${(LEVEL_ICON[n.level] ?? ui.info)()}</span><div>${esc(n.text)}<span class="notif__time">${esc(ago(n.time))}</span></div></li>`).join('')
    : '<li class="notif-empty">No tenés notificaciones.</li>';
  $('#notif-read').hidden = unread === 0;
  $('#notif-clear').hidden = list.length === 0;
}

function togglePop(id, force) {
  const pop = $(id);
  const open = force ?? !pop.classList.contains('is-open');
  for (const other of ['#search-pop', '#notif-pop']) {
    if (other !== id && $(other).classList.contains('is-open')) togglePop(other, false);
  }
  pop.classList.toggle('is-open', open);
  const btn = id === '#search-pop' ? $('#btn-search') : $('#btn-notif');
  btn.classList.toggle('is-open', open);
  if (id === '#search-pop' && open) setTimeout(() => $('#search-input').focus(), 30);
  if (id === '#notif-pop' && !open && S.app.notifications?.some((n) => !n.read)) {
    hub.markNotificationsRead().then((l) => { S.app.notifications = l; renderNotifications(); }).catch(() => {});
  }
}

// ===================================================================== render global

function renderData() {
  renderDock();
  renderFeatured();
  renderWall();
  renderInstalled();
  renderDownloads();
  renderSettings();
  renderHero(false);
  refreshSheet();
}

// ===================================================================== sistema (reloj, red, ventana)

function startClock() {
  const el = $('#clock');
  const tick = () => {
    el.textContent = new Date().toLocaleTimeString('es', { hour: '2-digit', minute: '2-digit' });
    setTimeout(tick, 1000 - (Date.now() % 1000));
  };
  tick();
}
function renderNet() {
  const el = $('#net');
  const on = navigator.onLine;
  el.innerHTML = on ? ui.wifi() : ui.wifiOff();
  el.classList.toggle('is-off', !on);
  el.title = on ? 'Conectado a internet' : 'Sin conexión: no se puede descargar';
  el.setAttribute('aria-label', el.title);
}
function setMaxIcon(max) {
  const b = $('#win-max');
  b.innerHTML = max ? ui.restore() : ui.max();
  b.title = max ? 'Restaurar' : 'Maximizar';
}

function bindChrome() {
  $('#win-min').innerHTML = ui.min();
  $('#win-close').innerHTML = ui.x();
  setMaxIcon(false);
  $('#win-min').addEventListener('click', () => hub.win.minimize());
  $('#win-max').addEventListener('click', () => hub.win.toggleMaximize());
  $('#win-close').addEventListener('click', () => hub.win.close());
  $('.topbar').addEventListener('dblclick', (e) => { if (!e.target.closest('button')) hub.win.toggleMaximize(); });
  hub.onMaximized(setMaxIcon);

  $('.search__icon').innerHTML = ui.search();
  $('#search-clear').innerHTML = ui.close();
  $('#btn-search').addEventListener('click', () => togglePop('#search-pop'));
  $('#btn-notif').addEventListener('click', () => togglePop('#notif-pop'));
  $('#btn-theme').addEventListener('click', async () => {
    const next = effectiveDark() ? 'light' : 'dark';
    const r = await act(() => hub.setTheme(next));
    if (r) applyAppState(r);
  });
  darkMedia.addEventListener('change', () => { if (S.app.theme === 'system') applyTheme('system'); });
  $('#btn-help').addEventListener('click', openHelp);
  $('#notif-read').addEventListener('click', async () => {
    const l = await act(() => hub.markNotificationsRead());
    if (l) { S.app.notifications = l; renderNotifications(); }
  });
  $('#notif-clear').addEventListener('click', async () => {
    const l = await act(() => hub.clearNotifications());
    if (l) { S.app.notifications = l; renderNotifications(); }
  });
  const input = $('#search-input');
  input.addEventListener('input', () => {
    setQuery(input.value);
    if (S.section !== 'catalog') go('catalog');
  });
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') togglePop('#search-pop', false);
  });
  $('#search-clear').addEventListener('click', () => { setQuery(''); input.focus(); });

  document.addEventListener('pointerdown', (e) => {
    if (!e.target.closest('.pop, #btn-search, #btn-notif')) {
      if ($('#search-pop').classList.contains('is-open')) togglePop('#search-pop', false);
      if ($('#notif-pop').classList.contains('is-open')) togglePop('#notif-pop', false);
    }
  });
  $('#sheet').addEventListener('pointerdown', (e) => { if (e.target === e.currentTarget) closeSheet(); });
  $('#modal').addEventListener('pointerdown', (e) => { if (e.target === e.currentTarget) closeModal(false); });

  document.addEventListener('keydown', (e) => {
    if (document.body.classList.contains('onboarding-open')) return undefined;
  const typing = ['INPUT', 'TEXTAREA'].includes(document.activeElement?.tagName);
    if (e.key === 'Escape') {
      if (!$('#modal').hidden) return closeModal(false);
      if (lightboxOpen()) return closeLightbox();
      if (S.sheetId) return closeSheet();
      if ($('#search-pop').classList.contains('is-open')) return togglePop('#search-pop', false);
      if ($('#notif-pop').classList.contains('is-open')) return togglePop('#notif-pop', false);
      return undefined;
    }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'f') { e.preventDefault(); return togglePop('#search-pop', true); }
    if (lightboxOpen() && (e.key === 'ArrowLeft' || e.key === 'ArrowRight')) {
      e.preventDefault();
      return stepLightbox(e.key === 'ArrowRight' ? 1 : -1);
    }
    if (typing || S.sheetId || !$('#modal').hidden) return undefined;
    if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
      e.preventDefault();
      const i = SECTIONS.findIndex((s) => s.id === S.section);
      const n = SECTIONS[(i + (e.key === 'ArrowRight' ? 1 : -1) + SECTIONS.length) % SECTIONS.length];
      go(n.id);
      $(`.tile[data-section="${n.id}"]`).focus({ preventScroll: true });
    }
    if (S.section === 'catalog' && (e.key === ',' || e.key === '.')) moveWall(e.key === '.' ? 1 : -1);
    return undefined;
  });
  window.addEventListener('online', renderNet);
  window.addEventListener('offline', renderNet);
}

// ===================================================================== reposo (ventana minimizada u oculta)

/** En reposo se pausan todas las animaciones y el vídeo de la ficha; al volver se retoma donde estaba. */
let stageWasPlaying = false;
function setIdle(idle) {
  const root = document.documentElement;
  if (root.classList.contains('is-idle') === idle) return;
  root.classList.toggle('is-idle', idle);
  const v = $('#sheet-body .stage__video');
  if (!v) return;
  if (idle) {
    stageWasPlaying = !v.paused;
    v.pause();
  } else if (stageWasPlaying && S.sheetId) {
    v.play().catch(() => {});
  }
}

// ===================================================================== arranque

const prevPhase = new Map();
function onJobs(list) {
  for (const j of list) {
    const before = prevPhase.get(j.id);
    if (before && before !== j.phase) {
      if (j.phase === 'done') toast('success', `${j.name} ${j.version} instalado`);
      else if (j.phase === 'error') toast('error', `${j.name}: ${j.message}`);
      else if (j.phase === 'canceled') toast('info', `Descarga de ${j.name} cancelada`);
    }
    prevPhase.set(j.id, j.phase);
  }
  S.jobs = list;
  renderDock();
  renderDownloads();
  refreshSheet();
  const changedStatus = S.section === 'featured' || S.section === 'downloads' || S.section === 'catalog';
  if (changedStatus) renderHero(false);
  for (const p of S.catalog.plugins) {
    const chip = cardEls.get(p.id) && $('.pcard__chip', cardEls.get(p.id));
    if (chip) { const c = chipFor(p); if (chip.innerHTML !== c) chip.innerHTML = c; }
  }
}

/** Animación de apertura: al terminar se quita del DOM (no queda una capa a pantalla completa en el compositor). */
function finishIntro() {
  const intro = $('#intro');
  if (!intro) return;
  const done = () => intro.remove();
  intro.addEventListener('animationend', (e) => { if (e.animationName === 'intro-out') done(); });
  setTimeout(done, 3000); // respaldo si la ventana arranca oculta y no corren las animaciones
}

async function init() {
  if (!hub) {
    document.body.innerHTML = '<p style="padding:40px;font:16px system-ui">ZUENA Plugins tiene que abrirse desde la aplicación de escritorio.</p>';
    return;
  }
  finishIntro();
  bindChrome();
  buildDock();
  buildScenes();
  startClock();
  renderNet();
  const st = await hub.getState();
  $('#demo-badge').hidden = !st.demo;
  applyAppState(st, false);
  renderHero(true);
  hub.onJobs(onJobs);
  hub.onState((s) => applyAppState(s));
  hub.onVisibility?.((visible) => setIdle(!visible));
  document.addEventListener('visibilitychange', () => setIdle(document.hidden));
  hub.onNotifications((l) => { S.app.notifications = l; renderNotifications(); });
  // Revisión en línea (al volver a la ventana o cada 6 h): catálogo nuevo sin recargar a mano.
  hub.onCatalog?.((res) => {
    S.catalog = res.catalog;
    S.meta = { source: res.source, warning: res.warning, errors: res.errors };
    applyAppState(res.state, false);
    S.wallIndex = Math.min(S.wallIndex, Math.max(0, filteredPlugins().length - 1));
    renderData();
  });
  hub.onLauncher?.((l) => {
    const before = S.app.launcher?.phase;
    S.app.launcher = { ...S.app.launcher, ...l };
    if (l.phase === 'ready' && before !== 'ready') toast('success', `ZUENA Plugins ${l.update.version} listo: reiniciá para actualizar`);
    if (l.phase === 'error' && before !== 'error') toast('error', `No se pudo descargar la actualización: ${l.message}`);
    renderDock();
    if (S.section === 'settings') renderSettings();
  });
  await reloadCatalog(false);
  renderHero(true);
  if (!st.onboarded && !st.demo) {
    startOnboarding({
      hub, state: S.app, toast, onTheme: (x) => applyAppState(x),
      onDone: async (next) => { applyAppState(next); await reloadCatalog(false); },
    });
  }
}

init();
