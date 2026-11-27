// Publica ZUENA Plugins en internet para que los launchers de los usuarios lo reciban:
//  1. Sube cada plugin (zip + vídeo + capturas) a una release de GitHub `<id>-v<versión>`.
//  2. Empaqueta el launcher, lo sube a `launcher-v<versión>` y escribe launcher.json (autoactualización).
//  3. Escribe catalog.json con URLs https y lo empuja al repositorio PÚBLICO de publicación,
//     que contiene SOLO binarios y catálogo (el código fuente sigue privado).
//
// Uso:  node tools/publish.mjs                 (publica todo)
//       node tools/publish.mjs --dry-run       (muestra qué haría, sin subir nada)
//       node tools/publish.mjs --plugins-only  (plugins y catálogo; el launcher publicado no cambia)
//       node tools/publish.mjs --web-only      (solo la página de descarga en GitHub Pages)
//       node tools/publish.mjs --installer-only (instalador para el launcher ya publicado)
// La página de descarga (web/descarga) se publica siempre en la raíz del repositorio público.
// Requisitos: `gh` autenticado. Antes: tools\build.cmd, `npm run pack:eq -- --all` y el render de vídeos.
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
import { existsSync, readFileSync, statSync, writeFileSync, mkdirSync, rmSync, renameSync, cpSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const CLIENT = join(ROOT, 'Free-VST-Client');
const PACKAGES = join(CLIENT, 'catalog', 'packages');
const { RELEASES_REPO: REPO, PRODUCT } = require(join(CLIENT, 'src', 'main', 'core', 'distribution.js'));
const { validateCatalog } = require(join(CLIENT, 'src', 'main', 'core', 'catalog.js'));
const { buildSetup, verifySetup, setupName, zipName } = require(join(CLIENT, 'build', 'setup.js'));
const PUB = join(ROOT, 'publish', 'zuena-plugins-releases');
const DRY = process.argv.includes('--dry-run');
// Solo plugins y catálogo: no empaqueta ni sube el launcher (útil si el launcher local tiene trabajo sin terminar).
const PLUGINS_ONLY = process.argv.includes('--plugins-only');
const WEB_ONLY = process.argv.includes('--web-only');
const INSTALLER_ONLY = process.argv.includes('--installer-only');
const WEB = join(ROOT, 'web', 'descarga');
const WEB_FILES = ['index.html', 'site.css', 'site.js', 'assets', '.nojekyll'];
const [OWNER, NAME] = REPO.split('/');
const PAGES_URL = `https://${OWNER.toLowerCase()}.github.io/${NAME}/`;

// Con stdio 'inherit' execFileSync devuelve null: se normaliza a texto vacío.
const run = (cmd, args, opts = {}) => (execFileSync(cmd, args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], ...opts }) ?? '').trim();
const gh = (...args) => run('gh', args);
const sh = (cmd, args, opts) => (DRY ? console.log(`  [simulado] ${cmd} ${args.join(' ')}`) : run(cmd, args, opts));
const sha256 = (file) => createHash('sha256').update(readFileSync(file)).digest('hex');
const assetUrl = (tag, file) => `https://github.com/${REPO}/releases/download/${tag}/${encodeURIComponent(file)}`;
const releaseExists = (tag) => { try { gh('release', 'view', tag, '-R', REPO, '--json', 'tagName'); return true; } catch { return false; } };

function ensureRepo() {
  try {
    gh('repo', 'view', REPO, '--json', 'name');
  } catch {
    console.log(`Creando el repositorio público ${REPO} (solo binarios y catálogo)…`);
    sh('gh', ['repo', 'create', REPO, '--public', '--description', 'ZUENA Plugins: catálogo y descargas del launcher (solo binarios).']);
  }
  if (!existsSync(join(PUB, '.git'))) {
    mkdirSync(dirname(PUB), { recursive: true });
    if (DRY) { console.log(`  [simulado] gh repo clone ${REPO} ${PUB}`); return; }
    try {
      gh('repo', 'clone', REPO, PUB);
    } catch {
      // Repositorio recién creado y vacío: se inicializa localmente.
      mkdirSync(PUB, { recursive: true });
      run('git', ['init', '-b', 'main'], { cwd: PUB });
      run('git', ['remote', 'add', 'origin', `https://github.com/${REPO}.git`], { cwd: PUB });
    }
  } else if (!DRY) {
    try { run('git', ['pull', '--ff-only'], { cwd: PUB }); } catch { /* repo vacío todavía */ }
  }
  // GitHub no crea releases en un repositorio vacío: hace falta un primer commit para las etiquetas.
  if (!DRY) {
    let hasCommit = true;
    try { run('git', ['rev-parse', '--verify', 'HEAD'], { cwd: PUB }); } catch { hasCommit = false; }
    if (!hasCommit) {
      writeFileSync(join(PUB, 'README.md'), '# ZUENA Plugins · descargas\n\nCatálogo y descargas del launcher de ZUENA Plugins (solo binarios).\n');
      run('git', ['add', 'README.md'], { cwd: PUB });
      run('git', ['-c', 'user.name=ZUENA Plugins', '-c', 'user.email=releases@zuena.invalid', 'commit', '-m', 'Inicio del repositorio de publicación'], { cwd: PUB });
      run('git', ['branch', '-M', 'main'], { cwd: PUB });
      run('git', ['push', '-u', 'origin', 'main'], { cwd: PUB });
    }
  }
}

/** Sube los archivos a la release (la crea si no existe). Archivos con el mismo nombre se reemplazan. */
function upload(tag, title, notes, files) {
  if (!releaseExists(tag)) sh('gh', ['release', 'create', tag, '-R', REPO, '--title', title, '--notes', notes || title]);
  sh('gh', ['release', 'upload', tag, '-R', REPO, '--clobber', ...files]);
}

function publishPlugins(catalog) {
  const remote = structuredClone(catalog);
  for (const p of remote.plugins) {
    const files = new Set();
    const toRemote = (u, tag) => {
      const m = /^bundled:\/\/(.+)$/.exec(u ?? '');
      if (!m) return u;
      files.add(join(PACKAGES, m[1]));
      return assetUrl(tag, m[1]);
    };
    const tag = `${p.id}-v${p.version}`;
    if (p.downloads?.win32) p.downloads.win32.url = toRemote(p.downloads.win32.url, tag);
    if (p.media) for (const k of Object.keys(p.media)) p.media[k] = toRemote(p.media[k], tag);
    if (Array.isArray(p.gallery)) p.gallery = p.gallery.map((g) => ({ ...g, src: toRemote(g.src, tag) }));
    if (!files.size) continue;
    for (const f of files) if (!existsSync(f)) throw new Error(`Falta el archivo ${f}`);
    const notes = (p.changelog?.find((c) => c.version === p.version)?.items ?? []).map((i) => `- ${i}`).join('\n');
    console.log(`Plugin ${p.name} v${p.version} → release ${tag} (${files.size} archivos)`);
    upload(tag, `${p.name} v${p.version}`, notes, [...files]);
  }
  remote.updated = new Date().toISOString().slice(0, 10);
  const { errors } = validateCatalog(remote, { allowBundled: false });
  if (errors.length) throw new Error(`El catálogo publicado no valida:\n${errors.join('\n')}`);
  if (JSON.stringify(remote).includes('bundled://')) throw new Error('Quedaron URLs bundled:// en el catálogo publicado');
  return remote;
}

async function publishLauncher() {
  const pkg = JSON.parse(readFileSync(join(CLIENT, 'package.json'), 'utf8'));
  const version = pkg.version;
  const notes = pkg.zuena?.releaseNotes ?? [];
  console.log(`Empaquetando el launcher v${version}…`);
  if (!DRY) run(process.execPath, [join(CLIENT, 'build', 'package.js')], { cwd: CLIENT, stdio: 'inherit' });
  const appDir = join(CLIENT, 'dist', `${PRODUCT}-win32-x64`);
  const zip = join(CLIENT, 'dist', zipName(version));
  const installer = join(CLIENT, 'dist', setupName(version));
  if (!DRY) {
    rmSync(zip, { force: true });
    // Carpeta "ZUENA Plugins" dentro del zip: al descomprimir queda lista para abrir.
    // El bsdtar de Windows no admite -s: se renombra la carpeta un momento y se restaura siempre.
    const named = join(CLIENT, 'dist', PRODUCT);
    rmSync(named, { recursive: true, force: true });
    renameSync(appDir, named);
    try {
      run('tar', ['-a', '-c', '-f', zip, '-C', join(CLIENT, 'dist'), PRODUCT]);
    } finally {
      renameSync(named, appDir);
    }
    await buildSetup(zip, installer, version);
  }
  const tag = `launcher-v${version}`;
  const manifest = DRY ? null : {
    schema: 1, version, published: new Date().toISOString().slice(0, 10), notes,
    downloads: { win32: assetInfo(tag, zipName(version)) },
    installers: { win32: assetInfo(tag, setupName(version)) },
  };
  console.log(`Launcher v${version} → release ${tag}${DRY ? '' : ` (${(manifest.downloads.win32.size / 1048576).toFixed(1)} MB)`}`);
  upload(tag, `${PRODUCT} ${version}`, notes.map((n) => `- ${n}`).join('\n'), [zip, installer]);
  return { manifest, url: assetUrl(tag, setupName(version)), version };
}

function assetInfo(tag, name) {
  const file = join(CLIENT, 'dist', name);
  return { url: assetUrl(tag, name), sha256: sha256(file), size: statSync(file).size };
}

async function publishInstaller() {
  const manifest = JSON.parse(readFileSync(join(PUB, 'launcher.json'), 'utf8'));
  const catalog = JSON.parse(readFileSync(join(PUB, 'catalog.json'), 'utf8'));
  const { version } = manifest;
  if (!/^\d+\.\d+\.\d+$/.test(version)) throw new Error('Versión de launcher inválida.');
  const tag = `launcher-v${version}`;
  const name = setupName(version);
  const zip = assetInfo(tag, zipName(version));
  if (zip.sha256 !== manifest.downloads.win32.sha256 || zip.size !== manifest.downloads.win32.size)
    throw new Error('El ZIP local debe coincidir con el ZIP del launcher publicado.');
  const installer = join(CLIENT, 'dist', name);
  if (!existsSync(installer)) {
    throw new Error('Compilá primero el instalador con npm run package:setup.');
  }
  await verifySetup(installer, zip);
  sh('gh', ['release', 'upload', tag, '-R', REPO, '--clobber', installer]);
  publishWeb();
  if (DRY) { console.log('\nSimulación terminada: no se subió nada.'); return; }
  manifest.installers = { ...manifest.installers, win32: assetInfo(tag, name) };
  writeFileSync(join(PUB, 'launcher.json'), `${JSON.stringify(manifest, null, 2)}\n`);
  writeFileSync(join(PUB, 'README.md'), readme({ manifest, version }, catalog));
  commitAndPush(['launcher.json', 'README.md', ...WEB_FILES], `Instalador de ${PRODUCT} ${version}`);
  console.log(`Instalador publicado: ${manifest.installers.win32.url}`);
}

function readme(launcher, catalog) {
  const setup = launcher.manifest.installers?.win32;
  const zip = launcher.manifest.downloads.win32.url;
  const download = setup
    ? `**[Instalador de ZUENA Plugins ${launcher.version}](${setup.url})** · [ZIP portátil](${zip})`
    : `**[ZUENA Plugins ${launcher.version} · ZIP portátil](${zip})**`;
  const steps = setup
    ? '1. Abrí el instalador y elegí tus accesos directos.\n2. Abrí ZUENA Plugins desde el menú Inicio para seguir la guía.'
    : '1. Descomprimí el ZIP y abrí **ZUENA Plugins.exe**.\n2. Seguí la guía de inicio para configurar el launcher.';
  return `# ZUENA Plugins · descargas

Este repositorio publica **solo** los archivos que necesita el launcher de ZUENA Plugins: el catálogo,
las descargas de los plugins y las versiones del propio launcher. El código fuente no está acá.

## Descargar el launcher (Windows 10/11, x64)

${download} · página de descarga: ${PAGES_URL}

${steps}

## Plugins

${catalog.plugins.map((p) => `- **${p.name}** v${p.version} · ${p.status === 'available' ? 'disponible' : 'próximamente'}`).join('\n')}

Actualizado: ${catalog.updated}
`;
}

/** Copia la página de descarga a la raíz del repositorio público y activa GitHub Pages (rama main, raíz). */
function publishWeb() {
  if (DRY) { console.log(`  [simulado] página de descarga → ${PAGES_URL}`); return; }
  for (const f of ['index.html', 'site.css', 'site.js']) cpSync(join(WEB, f), join(PUB, f));
  rmSync(join(PUB, 'assets'), { recursive: true, force: true });
  cpSync(join(WEB, 'assets'), join(PUB, 'assets'), { recursive: true });
  writeFileSync(join(PUB, '.nojekyll'), '');
  let enabled = true;
  try { gh('api', `repos/${REPO}/pages`); } catch { enabled = false; }
  if (!enabled) gh('api', '-X', 'POST', `repos/${REPO}/pages`, '-f', 'source[branch]=main', '-f', 'source[path]=/');
}

function commitAndPush(files, message) {
  run('git', ['add', ...files], { cwd: PUB });
  if (!run('git', ['status', '--porcelain'], { cwd: PUB })) return;
  run('git', ['-c', 'user.name=ZUENA Plugins', '-c', 'user.email=releases@zuena.invalid', 'commit', '-m', message], { cwd: PUB });
  run('git', ['push', '-u', 'origin', 'main'], { cwd: PUB });
}

(async () => {
  if ([PLUGINS_ONLY, WEB_ONLY, INSTALLER_ONLY].filter(Boolean).length > 1)
    throw new Error('Elegí un solo modo de publicación.');
  if (INSTALLER_ONLY && DRY) { await publishInstaller(); return; }
  try { gh('auth', 'status'); } catch { throw new Error('gh no está autenticado: ejecutá `gh auth login`'); }
  ensureRepo();
  if (INSTALLER_ONLY) { await publishInstaller(); return; }
  if (WEB_ONLY) {
    publishWeb();
    if (DRY) { console.log('\nSimulación terminada: no se subió nada.'); return; }
    const published = JSON.parse(readFileSync(join(PUB, 'launcher.json'), 'utf8'));
    const current = JSON.parse(readFileSync(join(PUB, 'catalog.json'), 'utf8'));
    writeFileSync(join(PUB, 'README.md'), readme({ manifest: published, version: published.version }, current));
    commitAndPush(['README.md', ...WEB_FILES], 'Página de descarga');
    console.log(`\nPágina de descarga: ${PAGES_URL} (la primera vez GitHub Pages tarda 1–2 minutos)`);
    return;
  }
  const catalog = JSON.parse(readFileSync(join(CLIENT, 'catalog', 'catalog.json'), 'utf8'));
  const remote = publishPlugins(catalog);
  let launcher;
  if (PLUGINS_ONLY) {
    // Se mantiene el launcher ya publicado: su manifiesto sigue igual y el README apunta a él.
    const published = JSON.parse(readFileSync(join(PUB, 'launcher.json'), 'utf8'));
    launcher = { manifest: published, url: published.installers?.win32?.url ?? published.downloads.win32.url, version: published.version };
    console.log(`Launcher: se mantiene v${launcher.version} publicado (--plugins-only)`);
  } else {
    launcher = await publishLauncher();
  }
  publishWeb();
  if (DRY) { console.log('\nSimulación terminada: no se subió nada.'); return; }
  writeFileSync(join(PUB, 'catalog.json'), `${JSON.stringify(remote, null, 2)}\n`);
  if (!PLUGINS_ONLY) writeFileSync(join(PUB, 'launcher.json'), `${JSON.stringify(launcher.manifest, null, 2)}\n`);
  writeFileSync(join(PUB, 'README.md'), readme(launcher, remote));
  commitAndPush(['catalog.json', 'launcher.json', 'README.md', ...WEB_FILES], `Catálogo ${remote.updated} · launcher ${launcher.version}`);
  console.log(`\nPublicado en https://github.com/${REPO}`);
  console.log(`Página de descarga: ${PAGES_URL}`);
  console.log(`Descarga del launcher: ${launcher.url}`);
})().catch((e) => { console.error(e.message); process.exit(1); });
