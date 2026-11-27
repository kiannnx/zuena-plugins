// ZUENA Plugins · landing de descarga.
// 1) Datos: lee launcher.json y catalog.json del mismo repositorio (GitHub Pages los sirve junto a la página).
//    Todo texto del catálogo se inserta con textContent: son datos, nunca HTML.
// 2) Movimiento: GSAP + ScrollTrigger (incluidos en assets/vendor). Solo transform y opacity.
//    Con movimiento reducido no se crea ninguna animación: todo queda visible y quieto.
// 3) Aviso de cookies y ventanas legales (términos, privacidad).

const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const el = (tag, cls, text) => { const n = document.createElement(tag); if (cls) n.className = cls; if (text != null) n.textContent = text; return n; };
const isHttps = (u) => typeof u === 'string' && /^https:\/\//.test(u);
const fmtSize = (b) => (b >= 1e6 ? `${(b / 1048576).toFixed(0)} MB` : `${Math.max(1, Math.round(b / 1024))} KB`);
const fmtDate = (d) => {
  const t = Date.parse(`${d}T12:00:00`);
  return Number.isNaN(t) ? '—' : new Intl.DateTimeFormat('es-AR', { day: 'numeric', month: 'long', year: 'numeric' }).format(t);
};
const newer = (a, b) => {
  const x = String(a).split('.').map(Number); const y = String(b).split('.').map(Number);
  for (let i = 0; i < 3; i += 1) { if ((x[i] || 0) !== (y[i] || 0)) return (x[i] || 0) > (y[i] || 0); }
  return false;
};
function withEmphasis(text) {
  const f = document.createDocumentFragment();
  String(text).split(/\*([^*]+)\*/).forEach((part, i) => f.append(i % 2 ? el('em', null, part) : document.createTextNode(part)));
  return f;
}
async function getJson(url) {
  const r = await fetch(url, { cache: 'no-cache' });
  if (!r.ok) throw new Error(`${url}: ${r.status}`);
  return r.json();
}
const store = {
  get(k) { try { return localStorage.getItem(k); } catch { return null; } },
  set(k, v) { try { localStorage.setItem(k, v); } catch { /* almacenamiento bloqueado: el aviso vuelve a mostrarse */ } },
};

// ---------- Datos ----------
function renderLauncher(m) {
  const assetPath = (asset) => {
    if (!isHttps(asset?.url) || !/^[0-9a-f]{64}$/.test(asset.sha256 ?? '')
      || !Number.isSafeInteger(asset.size) || asset.size <= 0) return '';
    try { return decodeURIComponent(new URL(asset.url).pathname); } catch { return ''; }
  };
  const zip = m?.downloads?.win32;
  const portablePath = assetPath(zip);
  if (!portablePath) throw new Error('launcher.json sin descarga válida para Windows');
  const setup = m?.installers?.win32;
  const setupPath = assetPath(setup);
  const hasSetup = setupPath.toLowerCase().endsWith('.exe');
  const win = hasSetup ? setup : zip;
  const portable = $('#download-portable');
  portable.href = zip.url;
  portable.hidden = !hasSetup;
  $('#steps-setup').hidden = !hasSetup;
  $('#steps-portable').hidden = hasSetup;
  $$('[data-download]').forEach((a) => { if (a.getAttribute('href') !== '#descargar') a.href = win.url; });
  $$('[data-download-label]').forEach((s) => {
    s.textContent = hasSetup ? 'Descargar instalador x64' : 'Descargar ZIP portátil x64';
  });
  $$('[data-download-meta]').forEach((s) => { s.textContent = `Acceso anticipado · Windows 10 y 11 · 64 bits · ${fmtSize(win.size)}`; });
  $('#v-version').textContent = `${m.version} · acceso anticipado`;
  $('#v-date').textContent = fmtDate(m.published);
  $('#v-size').textContent = fmtSize(win.size);
  $('#v-sha').textContent = win.sha256;
  $('#v-sha').title = win.sha256;
  const vt = `https://www.virustotal.com/gui/file/${win.sha256}`;
  $('#vt-link').href = vt;
  $('#vt-link-2').href = vt;
  const file = (hasSetup ? setupPath : portablePath).split('/').pop();
  if (file) $$('[data-download-name]').forEach((s) => { s.textContent = file; });
  const copy = $('#copy-sha');
  copy.hidden = !navigator.clipboard;
  copy.addEventListener('click', async () => {
    try { await navigator.clipboard.writeText(win.sha256); copy.textContent = 'Copiado'; } catch { copy.textContent = 'No se pudo copiar'; }
    setTimeout(() => { copy.textContent = 'Copiar'; }, 1800);
  });
  $('#news-version').textContent = m.version;
  $('#news-list').replaceChildren(...(m.notes ?? []).map((n) => el('li', null, n)));
  if (newer(m.version, '0.2.2')) {
    $('#faq-data .faq__body p').textContent = 'Nada tuyo. Lee el catálogo público en GitHub para saber si hay novedades. Desde la versión 0.2.3 podés activar estadísticas anónimas: un número al azar por instalación, la versión del launcher y de Windows, el idioma y qué plugins tenés. Vienen apagadas y, si las apagás, se borra lo enviado.';
  }
}

function renderCatalog(c) {
  const list = $('#plugin-list');
  const plugins = (c?.plugins ?? []).filter((p) => p && typeof p.name === 'string');
  if (!plugins.length) { list.replaceChildren(el('p', 'empty', 'El catálogo no está disponible ahora. Lo vas a ver completo dentro del launcher.')); return; }
  const safe = (v, d) => (/^#[0-9a-f]{6}$/i.test(v ?? '') ? v : d);
  list.replaceChildren(...plugins.map((p) => {
    const card = el('article', 'plugin');
    const art = el('div', 'plugin__art');
    const [a1, a2] = Array.isArray(p.accent) ? p.accent : [];
    art.style.setProperty('--a1', safe(a1, '#b9a8ff'));
    art.style.setProperty('--a2', safe(a2, '#5ff0ff'));
    const fallback = () => art.append(el('span', 'plugin__glow'), el('span', 'plugin__name-art', p.name));
    const poster = p.media?.posterDark || p.media?.poster;
    if (isHttps(poster)) {
      const img = el('img');
      img.src = poster; img.alt = `Presentación de ${p.name}`; img.loading = 'lazy'; img.decoding = 'async';
      img.addEventListener('error', () => { img.remove(); fallback(); });
      art.append(img);
    } else fallback();
    const body = el('div', 'plugin__body');
    const top = el('div', 'plugin__top');
    top.append(el('span', 'plugin__cat', p.category ?? 'Plugin'),
      p.status === 'available' ? el('span', 'chip chip--ok', 'Disponible') : el('span', 'chip chip--soon', 'Próximamente'));
    const h = el('h3');
    h.append(p.headline ? withEmphasis(p.headline) : document.createTextNode(p.name));
    body.append(top, h, el('p', null, p.headline ? `${p.name} · ${p.tagline ?? ''}` : (p.tagline ?? '')));
    if (p.status === 'available' && p.version) body.append(el('span', 'plugin__ver', `v${p.version} · VST3 · Windows`));
    card.append(art, body);
    return card;
  }));
  if (c.updated) $('#foot-updated').textContent = `Catálogo actualizado el ${fmtDate(c.updated)}.`;
}

function detectOs() {
  const p = navigator.userAgentData?.platform || navigator.platform || navigator.userAgent;
  if (!/win/i.test(p)) $('#os-note').hidden = false;
}

// ---------- Cookies y ventanas legales ----------
function setupLegal() {
  const banner = $('#cookie');
  if (store.get('zp-aviso-cookies') !== 'visto') banner.hidden = false;
  $('[data-cookie-ok]').addEventListener('click', () => { store.set('zp-aviso-cookies', 'visto'); banner.hidden = true; });
  $$('[data-cookie-reopen]').forEach((b) => b.addEventListener('click', () => { banner.hidden = false; $('[data-cookie-ok]').focus(); }));

  const open = (id) => {
    const d = document.getElementById(id);
    if (!d || typeof d.showModal !== 'function') return;
    $$('dialog[open]').forEach((o) => { if (o !== d) o.close(); });
    if (!d.open) d.showModal();
  };
  $$('[data-dialog]').forEach((b) => b.addEventListener('click', () => open(b.dataset.dialog)));
  $$('dialog.legal').forEach((d) => {
    $('[data-close]', d).addEventListener('click', () => d.close());
    d.addEventListener('click', (e) => { if (e.target === d) d.close(); }); // clic en el fondo
  });
  const fromHash = () => {
    if (location.hash === '#terminos') open('dlg-terminos');
    if (location.hash === '#privacidad') open('dlg-privacidad');
  };
  window.addEventListener('hashchange', fromHash);
  fromHash();

  // «¿Cómo sé si tiene virus?» desde el bloque de descarga: abre la pregunta.
  $$('[data-open-faq]').forEach((a) => a.addEventListener('click', () => { const d = document.getElementById(a.dataset.openFaq); if (d) d.open = true; }));
  $$('[data-year]').forEach((s) => { s.textContent = String(new Date().getFullYear()); });
}

// ---------- Movimiento ----------
function splitWords(node) {
  // Envuelve cada palabra de un párrafo en <span class="w"> (respeta <em> como bloque).
  const parts = [];
  node.childNodes.forEach((n) => {
    if (n.nodeType === Node.TEXT_NODE) {
      n.textContent.split(/(\s+)/).forEach((t) => parts.push(/^\s+$/.test(t) || !t ? document.createTextNode(t) : el('span', 'w', t)));
    } else if (n.nodeType === Node.ELEMENT_NODE) {
      n.textContent.split(/(\s+)/).forEach((t) => {
        if (/^\s+$/.test(t) || !t) { parts.push(document.createTextNode(t)); return; }
        const w = el('span', 'w'); w.append(el(n.tagName.toLowerCase(), null, t)); parts.push(w);
      });
    }
  });
  node.replaceChildren(...parts);
}

let catalogReady;
function setupMotion() {
  const top = $('#top');
  const onScroll = () => top.classList.toggle('is-scrolled', window.scrollY > 8);
  window.addEventListener('scroll', onScroll, { passive: true });
  onScroll();

  if (!window.gsap || !window.ScrollTrigger) return; // sin librería: página quieta y completa
  const { gsap, ScrollTrigger } = window;
  gsap.registerPlugin(ScrollTrigger);
  gsap.defaults({ ease: 'power3.out', duration: 0.9 });
  splitWords($('.manifesto__text'));

  const mm = gsap.matchMedia();
  mm.add({ motion: '(prefers-reduced-motion: no-preference)', desktop: '(min-width: 961px)' }, (ctx) => {
    const { motion, desktop } = ctx.conditions;
    // «Cómo funciona»: el paso activo se marca siempre (es estado, no movimiento).
    const steps = $$('.how__step'); const screens = $$('.how__screen'); const rail = $('.how__rail span');
    const show = (i) => {
      steps.forEach((s, k) => s.classList.toggle('is-active', k === i));
      screens.forEach((s, k) => s.classList.toggle('is-active', k === i));
      if (!motion) gsap.set(rail, { scaleX: (i + 1) / steps.length });
    };
    if (!motion) {
      // Sin movimiento: el paso cambia por posición, sin fijar ni animar.
      steps.forEach((s, i) => ScrollTrigger.create({ trigger: s, start: 'top 60%', end: 'bottom 40%', onToggle: (st) => st.isActive && show(i) }));
      return undefined;
    }
    document.documentElement.classList.add('motion');

    // 1 · Entrada del héroe
    const intro = gsap.timeline({ defaults: { ease: 'power4.out' } });
    intro.from('.hero__title .w', { yPercent: 110, opacity: 0, duration: 1.1, stagger: 0.06 })
      .from('.hero__in', { y: 18, opacity: 0, duration: 0.8, stagger: 0.08 }, '-=0.7')
      .from('.hero__stage', { y: 80, opacity: 0, duration: 1.4, ease: 'expo.out' }, '-=0.8');

    // El dispositivo se endereza y crece mientras se baja (la entrada mueve el contenedor, no el dispositivo).
    gsap.fromTo('.hero__device', { rotationX: 22, scale: 0.9 }, {
      rotationX: 0, scale: 1, ease: 'none',
      scrollTrigger: { trigger: '.hero__stage', start: 'top 85%', end: 'top 15%', scrub: 0.6 },
    });
    gsap.to('.hero__glow', { scale: 1.25, opacity: 0.35, ease: 'none', scrollTrigger: { trigger: '.hero__stage', start: 'top 60%', end: 'bottom top', scrub: true } });
    gsap.to('.scroll-cue', { opacity: 0, ease: 'none', scrollTrigger: { trigger: '.hero', start: 'top top', end: '+=160', scrub: true } });
    // Luz de fondo con paralaje lento.
    gsap.to('.orb--1', { yPercent: 40, ease: 'none', scrollTrigger: { trigger: 'body', start: 'top top', end: 'bottom bottom', scrub: 1 } });
    gsap.to('.orb--2', { yPercent: -60, xPercent: -20, ease: 'none', scrollTrigger: { trigger: 'body', start: 'top top', end: 'bottom bottom', scrub: 1 } });
    gsap.to('.orb--3', { yPercent: 70, ease: 'none', scrollTrigger: { trigger: 'body', start: 'top top', end: 'bottom bottom', scrub: 1 } });

    // 2 · Manifiesto: cada palabra se enciende con el scroll.
    gsap.fromTo('.manifesto__text .w', { opacity: 0.12 }, {
      opacity: 1, ease: 'none', stagger: 0.1,
      scrollTrigger: { trigger: '.manifesto', start: 'top 70%', end: 'bottom 60%', scrub: true },
    });

    // 3 · Cómo funciona: fijado en escritorio, los pantallazos se cruzan con el scroll.
    if (desktop) {
      const tl = gsap.timeline({
        scrollTrigger: {
          trigger: '.how', start: 'top top', end: '+=1800', pin: true, scrub: 0.5,
          onUpdate: (st) => {
            const i = Math.min(steps.length - 1, Math.floor(st.progress * steps.length * 0.999));
            steps.forEach((s, k) => s.classList.toggle('is-active', k === i));
          },
        },
      });
      gsap.set(screens, { autoAlpha: 0, yPercent: 6, scale: 0.97 });
      gsap.set(screens[0], { autoAlpha: 1, yPercent: 0, scale: 1 });
      screens.forEach((s) => s.classList.add('is-active')); // la visibilidad la maneja GSAP
      tl.to(rail, { scaleX: 1, ease: 'none', duration: 3 }, 0)
        .to(screens[0], { autoAlpha: 0, yPercent: -6, scale: 0.97, duration: 0.4 }, 0.8)
        .to(screens[1], { autoAlpha: 1, yPercent: 0, scale: 1, duration: 0.4 }, 0.85)
        .fromTo('.verify-card__bar span', { scaleX: 0 }, { scaleX: 1, ease: 'none', duration: 0.6 }, 1.0)
        .from('.verify-card__ok', { autoAlpha: 0, y: 8, duration: 0.25 }, 1.55)
        .to(screens[1], { autoAlpha: 0, yPercent: -6, scale: 0.97, duration: 0.4 }, 1.8)
        .to(screens[2], { autoAlpha: 1, yPercent: 0, scale: 1, duration: 0.4 }, 1.85)
        .from('.daw-card li', { autoAlpha: 0, x: -16, stagger: 0.12, duration: 0.3 }, 2.1);
    } else {
      steps.forEach((s, i) => ScrollTrigger.create({ trigger: s, start: 'top 65%', end: 'bottom 35%', onToggle: (st) => st.isActive && show(i) }));
    }

    // 4 · Plugins: desplazamiento horizontal en escritorio, entrada escalonada en pantallas chicas.
    catalogReady.then(() => ctx.add(() => {
      const cards = $$('.plugin');
      if (!cards.length) return;
      const track = $('.plugins__track');
      const dist = () => Math.max(0, track.scrollWidth - $('.plugins__viewport').clientWidth + 48);
      // Fila horizontal solo si los plugins no entran a lo ancho; si entran, cuadrícula con entrada escalonada.
      if (desktop) $('.plugins').classList.add('is-horizontal');
      if (desktop && dist() < 120) $('.plugins').classList.remove('is-horizontal');
      if (desktop && $('.plugins').classList.contains('is-horizontal')) {
        gsap.to(track, {
          x: () => -dist(), ease: 'none',
          scrollTrigger: { trigger: '.plugins', start: 'top top', end: () => `+=${dist() + 400}`, pin: true, scrub: 0.6, invalidateOnRefresh: true },
        });
        gsap.from(cards, { y: 60, opacity: 0, stagger: 0.1, scrollTrigger: { trigger: '.plugins', start: 'top 75%' } });
      } else if (desktop) {
        // Las tarjetas suben a distinto ritmo mientras se baja: profundidad sin fijar la sección.
        gsap.fromTo(cards, { y: (i) => 80 + i * 70, opacity: 0.2 }, {
          y: 0, opacity: 1, ease: 'none', stagger: 0.08,
          scrollTrigger: { trigger: '.plugins__track', start: 'top 95%', end: 'top 35%', scrub: 0.6 },
        });
      } else {
        gsap.from(cards, { y: 40, opacity: 0, stagger: 0.12, scrollTrigger: { trigger: '.plugins__track', start: 'top 85%' } });
      }
      ScrollTrigger.refresh();
      return () => $('.plugins').classList.remove('is-horizontal');
    }));

    // Resto: entradas escalonadas por lote al aparecer.
    gsap.set('.reveal', { y: 32, opacity: 0 });
    ScrollTrigger.batch('.reveal', {
      start: 'top 88%',
      onEnter: (batch) => gsap.to(batch, { y: 0, opacity: 1, stagger: 0.08, overwrite: true }),
    });
    // Bloque de descarga: el halo crece al llegar.
    gsap.fromTo('.download__card', { scale: 0.96 }, { scale: 1, ease: 'none', scrollTrigger: { trigger: '.download', start: 'top 90%', end: 'top 40%', scrub: true } });

    return () => document.documentElement.classList.remove('motion');
  });

  // Las fuentes cambian alturas: se recalculan las posiciones al cargar.
  document.fonts?.ready.then(() => ScrollTrigger.refresh());
  window.addEventListener('load', () => ScrollTrigger.refresh());
}

// ---------- Arranque ----------
document.documentElement.classList.add('js');
detectOs();
setupLegal();
catalogReady = getJson('catalog.json').then(renderCatalog).catch(() => renderCatalog(null));
setupMotion();
getJson('launcher.json').then(renderLauncher).catch(() => {
  $$('[data-download-label]').forEach((s) => { s.textContent = 'Ver descargas en GitHub'; });
  $('#news-list').replaceChildren(el('li', null, 'Las notas de cada versión están en las releases de GitHub.'));
});
