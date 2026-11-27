// Guía de inicio: se muestra la primera vez. Pide los permisos que la app necesita (conexión a internet
// para el catálogo y las actualizaciones, avisos de Windows), explica la carpeta de plugins y, si la app
// se abrió desde una copia descargada, ofrece instalarla en el equipo.
import { esc, html, frag, $, $$, reducedMotion } from './util.js';
import { ui } from './icons.js';

const LOGO = '<svg viewBox="0 0 64 64" aria-hidden="true"><circle cx="32" cy="32" r="32" fill="var(--logo-bg)"/><path d="M19 20h26L19 44h26" fill="none" stroke="var(--logo-fg)" stroke-width="6.5" stroke-linecap="round" stroke-linejoin="round"/></svg>';
const em = (s) => esc(s).replace(/\*([^*]+)\*/g, '<em>$1</em>');
const glyph = {
  hello: '<svg viewBox="0 0 48 48" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><path d="M4 30c6 0 8-14 14-14s8 20 14 20 8-10 12-10"/><circle cx="18" cy="16" r="3" fill="currentColor"/><circle cx="32" cy="36" r="3" fill="currentColor"/></svg>',
  net: '<svg viewBox="0 0 48 48" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><circle cx="24" cy="24" r="18"/><path d="M6 24h36M24 6c6 5 8 12 8 18s-2 13-8 18c-6-5-8-12-8-18s2-13 8-18z"/></svg>',
  bell: '<svg viewBox="0 0 48 48" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><path d="M12 33V21a12 12 0 0124 0v12l4 4H8z"/><path d="M20 41a4 4 0 008 0"/></svg>',
  folder: '<svg viewBox="0 0 48 48" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><path d="M5 14a4 4 0 014-4h9l4 5h17a4 4 0 014 4v17a4 4 0 01-4 4H9a4 4 0 01-4-4z"/></svg>',
  install: '<svg viewBox="0 0 48 48" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><rect x="6" y="8" width="36" height="26" rx="4"/><path d="M18 42h12M24 34v8M24 14v11m0 0l-5-5m5 5l5-5"/></svg>',
  look: '<svg viewBox="0 0 48 48" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><circle cx="24" cy="24" r="17"/><path d="M24 7a17 17 0 000 34z" fill="currentColor"/></svg>',
  done: '<svg viewBox="0 0 48 48" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><circle cx="24" cy="24" r="18"/><path d="M16 24.5l5.5 5.5 11-12"/></svg>',
};

const toggleRow = (key, title, text, on) => html`<label class="ob-opt">
  <span class="ob-opt__text"><span class="ob-opt__title">${esc(title)}</span><span class="ob-opt__desc">${esc(text)}</span></span>
  <input type="checkbox" class="switch" data-k="${key}"${on ? ' checked' : ''} /></label>`;

/**
 * Abre la guía. `state` es el estado de la app (S.app). Resuelve cuando termina (o cuando la app se
 * reinicia para abrirse desde la carpeta instalada).
 */
export function startOnboarding({ hub, state, toast, onDone, onTheme }) {
  const choice = { online: true, stats: false, desktopNotify: true, desktop: true, startMenu: true, theme: state.theme || 'light', language: 'es' };
  const canInstall = !!state.launcher?.packaged && !state.launcher?.installed && state.platform === 'win32';
  const steps = [
    {
      id: 'hello', glyph: glyph.hello, eyebrow: 'ZUENA Plugins',
      title: 'Te damos la *bienvenida*.',
      body: () => `<p class="ob-text">Plugins de audio gratis, con la estética y el cuidado de ZUENA. Elegís uno, se descarga verificado y queda listo en tu DAW.</p>
        <ul class="ob-list"><li>Gratis, sin cuenta ni registro</li><li>Se instalan solos en la carpeta VST3</li><li>Te avisamos cuando hay versiones nuevas</li></ul>`,
      primary: 'Empezar',
    },
    {
      id: 'look', glyph: glyph.look, eyebrow: 'Idioma y apariencia',
      title: 'Hacelo *tuyo*.',
      body: () => `<div class="ob-label">Idioma</div>
        <div class="ob-choices" role="radiogroup" aria-label="Idioma">
          <button class="ob-choice is-active" role="radio" aria-checked="true" data-lang="es"><span class="ob-choice__title">Español</span><span class="ob-choice__desc">Rioplatense</span></button>
          <button class="ob-choice" role="radio" aria-checked="false" disabled title="La traducción al inglés está en camino"><span class="ob-choice__title">English</span><span class="ob-choice__desc">Próximamente</span></button>
        </div>
        <div class="ob-label">Tema</div>
        <div class="ob-choices ob-choices--3" role="radiogroup" aria-label="Tema">
          ${[['light', 'Claro', 'Papel y tinta'], ['dark', 'Oscuro', 'Para sesiones de noche'], ['system', 'Automático', 'Sigue a Windows']].map(([v, t, d]) => `
          <button class="ob-choice ob-theme${choice.theme === v ? ' is-active' : ''}" role="radio" aria-checked="${choice.theme === v}" data-theme-pick="${v}">
            <span class="ob-swatch ob-swatch--${v}" aria-hidden="true"><span></span><span></span></span>
            <span class="ob-choice__title">${t}</span><span class="ob-choice__desc">${d}</span></button>`).join('')}
        </div>`,
      primary: 'Continuar',
    },
    {
      id: 'net', glyph: glyph.net, eyebrow: 'Permiso 1 de 2 · Conexión',
      title: 'Conectate para estar *al día*.',
      body: () => `${toggleRow('online', 'Buscar plugins y actualizaciones en internet', 'Lee el catálogo público de ZUENA en GitHub y descarga solo lo que elijas. No pide cuenta ni envía datos tuyos.', choice.online)}
        ${toggleRow('stats', 'Ayudar con estadísticas anónimas', 'Un número al azar por instalación, la versión del launcher y de Windows, y qué plugins tenés. Nada personal. Si lo apagás, se borra lo enviado.', choice.stats)}
        <p class="ob-note">Sin conexión, el launcher funciona con los plugins que trae incluidos.</p>`,
      primary: 'Continuar',
    },
    {
      id: 'bell', glyph: glyph.bell, eyebrow: 'Permiso 2 de 2 · Avisos',
      title: 'Te *avisamos* cuando haya novedades.',
      body: () => `${toggleRow('desktopNotify', 'Avisos de Windows', 'Un aviso cuando salga un plugin nuevo o una actualización, aunque la ventana esté en segundo plano.', choice.desktopNotify)}
        <button class="pill pill--sm" data-ob="test">${ui.bell()}Probar aviso</button>`,
      primary: 'Continuar',
    },
    {
      id: 'folder', glyph: glyph.folder, eyebrow: 'Carpeta de plugins',
      title: 'Tus plugins van a *su lugar*.',
      body: () => `<div class="ob-path" title="${esc(state.installDir)}">${esc(state.installDir)}</div>
        <p class="ob-text">Es la carpeta que escanean todos los DAW. ${state.platform === 'win32' && state.installDir === state.defaultInstallDir ? 'Al instalar o desinstalar, Windows te va a pedir permiso de administrador: es normal y se pide una vez por operación.' : ''}</p>
        <p class="ob-note">Podés cambiarla cuando quieras en Ajustes.</p>`,
      primary: 'Continuar',
    },
    ...(canInstall ? [{
      id: 'install', glyph: glyph.install, eyebrow: 'Este equipo',
      title: 'Instalá ZUENA Plugins en *este equipo*.',
      body: () => `<p class="ob-text">Se copia a tu carpeta de programas, sin permisos de administrador, y se abre desde ahí. Así puede actualizarse solo.</p>
        ${toggleRow('desktop', 'Acceso directo en el escritorio', 'Para abrirlo con un doble clic.', choice.desktop)}
        ${toggleRow('startMenu', 'En el menú Inicio', 'También necesario para que Windows muestre los avisos con el nombre de la app.', choice.startMenu)}
        <div class="ob-progress" hidden><span class="ob-progress__bar"></span><span class="ob-progress__text">Copiando archivos…</span></div>`,
      primary: 'Instalar y abrir', secondary: 'Seguir sin instalar',
    }] : []),
    {
      id: 'done', glyph: glyph.done, eyebrow: 'Todo listo',
      title: 'Ya podés *explorar*.',
      body: () => `<ul class="ob-tips">
          <li><b>Destacado</b><span>El plugin del momento, con su ficha y su vídeo.</span></li>
          <li><b>Catálogo</b><span>Arrastrá las tarjetas, usá la rueda o las flechas ← →.</span></li>
          <li><b>Ajustes</b><span>Conexión, avisos, carpeta y actualizaciones.</span></li></ul>`,
      primary: 'Explorar',
    },
  ];

  const root = frag(html`<div class="ob" role="dialog" aria-modal="true" aria-labelledby="ob-title">
    <div class="ob__card">
      <aside class="ob__art art"><span class="ob__aura"></span><span class="ob__logo">${LOGO}</span><span class="ob__glyph"></span>
        <ol class="ob__rail" aria-hidden="true">${steps.map(() => '<li></li>').join('')}</ol></aside>
      <section class="ob__main">
        <div class="ob__step" aria-live="polite"></div>
        <footer class="ob__foot">
          <button class="pill pill--ghost" data-ob="back">Atrás</button>
          <span class="ob__count"></span>
          <span class="ob__actions"><button class="pill" data-ob="secondary" hidden></button><button class="pill pill--primary" data-ob="next"></button></span>
        </footer>
      </section>
    </div></div>`);
  document.body.append(root);
  document.body.classList.add('onboarding-open');
  requestAnimationFrame(() => root.classList.add('is-open'));
  let i = 0;
  let busy = false;

  const finish = async () => {
    const st = await hub.completeOnboarding({ online: choice.online, stats: choice.stats, desktopNotify: choice.desktopNotify, language: choice.language });
    root.classList.remove('is-open');
    document.body.classList.remove('onboarding-open');
    setTimeout(() => root.remove(), reducedMotion() ? 0 : 420);
    onDone?.(st);
  };

  function render(dir = 1) {
    const s = steps[i];
    const step = $('.ob__step', root);
    const body = frag(`<div class="ob__pane${dir < 0 ? ' from-left' : ''}"><div class="ob__eyebrow">${esc(s.eyebrow)}</div>
      <h1 class="ob__title" id="ob-title">${em(s.title)}</h1><div class="ob__body">${s.body()}</div></div>`);
    step.replaceChildren(body);
    $('.ob__glyph', root).innerHTML = s.glyph;
    $('.ob__glyph', root).classList.remove('is-in');
    void $('.ob__glyph', root).offsetWidth;
    $('.ob__glyph', root).classList.add('is-in');
    $$('.ob__rail li', root).forEach((li, k) => { li.classList.toggle('is-done', k < i); li.classList.toggle('is-cur', k === i); });
    $('.ob__count', root).textContent = `${i + 1} / ${steps.length}`;
    $('[data-ob="back"]', root).style.visibility = i === 0 || s.id === 'done' ? 'hidden' : 'visible';
    const next = $('[data-ob="next"]', root);
    next.textContent = s.primary;
    const sec = $('[data-ob="secondary"]', root);
    sec.hidden = !s.secondary;
    sec.textContent = s.secondary ?? '';
    $$('.switch', body).forEach((sw) => sw.addEventListener('change', () => { choice[sw.dataset.k] = sw.checked; }));
    // Tema: se aplica en vivo mientras se elige (el cambio se guarda en Ajustes al instante).
    $$('[data-theme-pick]', body).forEach((b) => b.addEventListener('click', async () => {
      choice.theme = b.dataset.themePick;
      $$('[data-theme-pick]', body).forEach((x) => { x.classList.toggle('is-active', x === b); x.setAttribute('aria-checked', String(x === b)); });
      try { const st = await hub.setTheme(choice.theme); onTheme?.(st); } catch (e) { toast?.('error', e.message); }
    }));
    $('[data-ob="test"]', body)?.addEventListener('click', async () => {
      try { await hub.testNotification(); toast?.('info', 'Aviso de prueba enviado a Windows'); } catch (e) { toast?.('error', e.message); }
    });
    setTimeout(() => next.focus({ preventScroll: true }), 60);
  }

  async function install() {
    busy = true;
    const prog = $('.ob-progress', root);
    prog.hidden = false;
    $$('.ob__foot button', root).forEach((b) => { b.disabled = true; });
    const off = hub.onInstallProgress?.((m) => { $('.ob-progress__text', root).textContent = m; });
    try {
      await hub.completeOnboarding({ online: choice.online, stats: choice.stats, desktopNotify: choice.desktopNotify, language: choice.language });
      await hub.installSelf({ desktop: choice.desktop, startMenu: choice.startMenu });
      $('.ob-progress__text', root).textContent = 'Listo. Abriendo desde la carpeta instalada…';
    } catch (e) {
      off?.();
      busy = false;
      prog.hidden = true;
      $$('.ob__foot button', root).forEach((b) => { b.disabled = false; });
      toast?.('error', e.message);
    }
  }

  root.addEventListener('click', async (e) => {
    const b = e.target.closest('[data-ob]');
    if (!b || busy) return;
    const s = steps[i];
    if (b.dataset.ob === 'back' && i > 0) { i -= 1; render(-1); }
    if (b.dataset.ob === 'secondary') { i += 1; render(1); }
    if (b.dataset.ob === 'next') {
      if (s.id === 'install') return install();
      if (s.id === 'done') return finish();
      i += 1;
      render(1);
    }
    return undefined;
  });
  root.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') e.stopPropagation(); // la guía no se cierra por accidente
  });
  render(1);
}
