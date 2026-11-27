'use strict';
// Extracción de .zip con yauzl v3. Protege contra zip-slip (rutas absolutas o con ..),
// enlaces simbólicos y bombas de descompresión.

const { fs, fsp } = require('./real-fs'); // sin intercepción de .asar
const path = require('node:path');
const { pipeline } = require('node:stream/promises');
const yauzl = require('yauzl');

const MAX_TOTAL_BYTES = 2 * 1024 ** 3; // 2 GB descomprimidos
const MAX_ENTRIES = 20000;

const openZip = (file) => new Promise((resolve, reject) => {
  yauzl.open(file, { lazyEntries: true, strictFileNames: true, validateEntrySizes: true }, (err, zip) => (err ? reject(err) : resolve(zip)));
});

/** Extrae `zipPath` dentro de `destDir` (debe existir o se crea). */
async function extractZip(zipPath, destDir) {
  const root = path.resolve(destDir);
  await fsp.mkdir(root, { recursive: true });
  const zip = await openZip(zipPath);
  let total = 0;
  let count = 0;
  try {
    await new Promise((resolve, reject) => {
      const fail = (e) => { zip.close(); reject(e); };
      zip.on('error', fail);
      zip.on('end', resolve);
      zip.on('entry', async (entry) => {
        try {
          if (++count > MAX_ENTRIES) throw new Error('demasiadas entradas');
          const name = entry.fileName.replace(/\\/g, '/');
          const target = path.resolve(root, name);
          const rel = path.relative(root, target);
          if (!rel || rel.startsWith('..') || path.isAbsolute(rel) || name.startsWith('/')) throw new Error(`ruta no permitida: ${name}`);
          const mode = (entry.externalFileAttributes >>> 16) & 0o170000;
          if (mode === 0o120000) throw new Error(`enlace simbólico no permitido: ${name}`);
          if (name.endsWith('/')) {
            await fsp.mkdir(target, { recursive: true });
          } else {
            total += entry.uncompressedSize;
            if (total > MAX_TOTAL_BYTES) throw new Error('el contenido descomprimido es demasiado grande');
            await fsp.mkdir(path.dirname(target), { recursive: true });
            const rs = await new Promise((res, rej) => zip.openReadStream(entry, (e, s) => (e ? rej(e) : res(s))));
            await pipeline(rs, fs.createWriteStream(target));
          }
          zip.readEntry();
        } catch (e) {
          fail(e);
        }
      });
      zip.readEntry();
    });
  } finally {
    zip.close();
  }
}

module.exports = { extractZip };
