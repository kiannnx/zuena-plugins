'use strict';
// Instalación / desinstalación de bundles .vst3 desde un paquete .zip.

const fsp = require('node:fs/promises');
const path = require('node:path');
const { extractZip } = require('./unzip');

const PERMISSION_CODES = new Set(['EPERM', 'EACCES']);

class InstallError extends Error {
  constructor(code, message) {
    super(message);
    this.code = code;
  }
}

/** true si `child` está dentro de `parent` (sin escapar con ..). */
function isInside(parent, child) {
  const rel = path.relative(path.resolve(parent), path.resolve(child));
  return rel !== '' && !rel.startsWith('..') && !path.isAbsolute(rel);
}

/** Busca bundles .vst3 en la raíz del paquete o dentro de una única carpeta contenedora. */
async function findBundles(root) {
  const scan = async (dir) => (await fsp.readdir(dir, { withFileTypes: true }))
    .filter((d) => d.name.toLowerCase().endsWith('.vst3'))
    .map((d) => path.join(dir, d.name));
  let found = await scan(root);
  if (found.length === 0) {
    const dirs = (await fsp.readdir(root, { withFileTypes: true })).filter((d) => d.isDirectory());
    if (dirs.length === 1) found = await scan(path.join(root, dirs[0].name));
  }
  return found;
}

async function probeWritable(dir) {
  try {
    await fsp.mkdir(dir, { recursive: true });
    const probe = path.join(dir, `.zuena-probe-${process.pid}-${Date.now()}`);
    await fsp.writeFile(probe, '');
    await fsp.rm(probe, { force: true });
    return true;
  } catch (e) {
    if (PERMISSION_CODES.has(e.code)) return false;
    throw new InstallError('DIR', `No se puede usar la carpeta de instalación: ${e.message}`);
  }
}

const LOCK_CODES = new Set(['EBUSY', 'EPERM', 'EACCES']);
const exists = (p) => fsp.access(p).then(() => true, () => false);
/** Qué programa tiene el archivo abierto: la fuente de OBS la carga OBS; los .vst3, el DAW. */
const busyHint = (p) => (/obs-studio/i.test(p) ? 'Cerrá OBS y probá de nuevo.' : 'Cerrá tu DAW y probá de nuevo.');
const MODULE_RE = /^[a-z0-9][a-z0-9-]{0,63}$/;

/**
 * Módulos de OBS dentro del paquete: carpeta obs-studio/<módulo>/bin/64bit/<módulo>.dll (más data/).
 * Están junto a los .vst3, en la raíz o en la misma carpeta contenedora.
 */
async function findObsModules(root) {
  const candidates = [path.join(root, 'obs-studio')];
  const dirs = (await fsp.readdir(root, { withFileTypes: true })).filter((d) => d.isDirectory() && d.name !== 'obs-studio');
  if (dirs.length === 1) candidates.push(path.join(root, dirs[0].name, 'obs-studio'));
  for (const base of candidates) {
    if (!(await exists(base))) continue;
    const found = [];
    for (const d of await fsp.readdir(base, { withFileTypes: true })) {
      if (!d.isDirectory() || !MODULE_RE.test(d.name)) continue;
      if (await exists(path.join(base, d.name, 'bin', '64bit', `${d.name}.dll`))) found.push(path.join(base, d.name));
    }
    return found;
  }
  return [];
}

/**
 * Aparta un bundle existente renombrándolo. En Windows el renombrado falla si el DAW tiene la DLL
 * cargada, así que el bundle instalado queda intacto en vez de borrarse a medias (EB-19).
 */
async function moveAside(p) {
  if (!(await exists(p))) return null;
  const aside = `${p}.zuena-old`;
  await fsp.rm(aside, { recursive: true, force: true });
  try {
    await fsp.rename(p, aside);
  } catch (e) {
    if (LOCK_CODES.has(e.code)) throw new InstallError('BUSY', `${path.basename(p)} está en uso. ${busyHint(p)}`);
    throw e;
  }
  return aside;
}

async function applyDirect(ops) {
  for (const o of ops) {
    if (o.op === 'replace') {
      await fsp.mkdir(path.dirname(o.to), { recursive: true });
      const aside = await moveAside(o.to);
      try {
        await fsp.cp(o.from, o.to, { recursive: true, force: true, errorOnExist: false });
      } catch (e) {
        await fsp.rm(o.to, { recursive: true, force: true }).catch(() => {});
        if (aside) await fsp.rename(aside, o.to).catch(() => {});
        throw e;
      }
      if (aside) await fsp.rm(aside, { recursive: true, force: true }).catch(() => {});
    } else if (o.op === 'remove') {
      const aside = await moveAside(o.path);
      if (aside) await fsp.rm(aside, { recursive: true, force: true });
    }
  }
}

/** Aplica ops directamente; si no hay permisos, usa `runElevated`. */
async function applyOps(ops, targetDir, runElevated) {
  const writable = await probeWritable(targetDir);
  if (writable) {
    try {
      await applyDirect(ops);
      return { elevated: false };
    } catch (e) {
      if (e instanceof InstallError) throw e;
      if (e.code === 'EBUSY') throw new InstallError('BUSY', 'El plugin está en uso. Cerrá tu DAW y probá de nuevo.');
      if (!PERMISSION_CODES.has(e.code)) throw new InstallError('IO', `Error de archivos: ${e.message}`);
    }
  }
  if (!runElevated) throw new InstallError('PERMISSION', 'Sin permisos de escritura en la carpeta de instalación.');
  await runElevated(ops);
  return { elevated: true };
}

/**
 * @param {{zipPath: string, targetDir: string, stagingDir: string, obsDir?: string, runElevated?: Function}} o
 *   obsDir: carpeta de plugins de OBS; si el paquete trae una fuente de OBS, se instala ahí.
 * @returns {Promise<{files: string[], obs: string[], elevated: boolean}>}
 */
async function installPackage({ zipPath, targetDir, stagingDir, obsDir, runElevated }) {
  const staging = path.resolve(stagingDir);
  await fsp.rm(staging, { recursive: true, force: true });
  await fsp.mkdir(staging, { recursive: true });
  try {
    try {
      await extractZip(zipPath, staging);
    } catch (e) {
      throw new InstallError('ZIP', `El paquete está dañado o no es un .zip válido (${e.message})`);
    }
    const bundles = await findBundles(staging);
    if (bundles.length === 0) throw new InstallError('NO_VST3', 'El paquete no contiene ningún plugin .vst3');
    const ops = bundles.map((from) => ({ op: 'replace', from, to: path.join(targetDir, path.basename(from)) }));
    for (const o of ops) {
      if (!isInside(targetDir, o.to)) throw new InstallError('PATH', 'Ruta de destino no permitida');
    }
    const { elevated } = await applyOps(ops, targetDir, runElevated);
    for (const o of ops) {
      try {
        await fsp.access(o.to);
      } catch {
        throw new InstallError('VERIFY', `No se encontró ${path.basename(o.to)} tras la instalación`);
      }
    }
    const obs = [];
    let obsElevated = false;
    const modules = obsDir ? await findObsModules(staging) : [];
    if (modules.length) {
      const obsOps = modules.map((from) => ({ op: 'replace', from, to: path.join(obsDir, path.basename(from)) }));
      for (const o of obsOps) {
        if (!isInside(obsDir, o.to)) throw new InstallError('PATH', 'Ruta de destino no permitida');
      }
      obsElevated = (await applyOps(obsOps, obsDir, runElevated)).elevated;
      for (const o of obsOps) {
        const dll = path.join(o.to, 'bin', '64bit', `${path.basename(o.to)}.dll`);
        if (!(await exists(dll))) throw new InstallError('VERIFY', 'No se encontró la fuente de OBS tras la instalación');
        obs.push(o.to);
      }
    }
    return { files: ops.map((o) => o.to), obs, elevated: elevated || obsElevated };
  } finally {
    await fsp.rm(staging, { recursive: true, force: true }).catch(() => {});
  }
}

/** Elimina los bundles instalados. Solo borra rutas .vst3 dentro de `targetDir`. */
async function uninstallFiles({ files, targetDir, runElevated }) {
  const safe = files.filter((f) => f.toLowerCase().endsWith('.vst3') && isInside(targetDir, f));
  if (safe.length !== files.length) throw new InstallError('PATH', 'Registro de instalación con rutas no permitidas');
  const existing = [];
  for (const f of safe) {
    try {
      await fsp.access(f);
      existing.push(f);
    } catch {
      /* ya no existe */
    }
  }
  if (existing.length === 0) return { elevated: false };
  return applyOps(existing.map((p) => ({ op: 'remove', path: p })), targetDir, runElevated);
}

/** Quita módulos de OBS instalados. Solo carpetas de módulo directamente dentro de `obsDir`. */
async function uninstallObsModules({ dirs, obsDir, runElevated }) {
  const safe = dirs.filter((d) => isInside(obsDir, d) && path.dirname(path.resolve(d)) === path.resolve(obsDir)
    && MODULE_RE.test(path.basename(d)));
  if (safe.length !== dirs.length) throw new InstallError('PATH', 'Registro de instalación con rutas no permitidas');
  const existing = [];
  for (const d of safe) if (await exists(d)) existing.push(d);
  if (existing.length === 0) return { elevated: false };
  return applyOps(existing.map((p) => ({ op: 'remove', path: p })), obsDir, runElevated);
}

module.exports = { installPackage, uninstallFiles, uninstallObsModules, findBundles, findObsModules, isInside, InstallError };
