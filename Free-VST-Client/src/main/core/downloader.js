'use strict';
// Descarga con progreso, redirecciones, cancelación y verificación SHA-256.

const fs = require('node:fs');
const fsp = require('node:fs/promises');
const http = require('node:http');
const https = require('node:https');
const crypto = require('node:crypto');
const { isAllowedUrl } = require('./catalog');

const MAX_REDIRECTS = 5;
const PROGRESS_INTERVAL_MS = 100;

class DownloadError extends Error {
  constructor(code, message) {
    super(message);
    this.code = code;
  }
}

function request(url, signal, redirects = 0) {
  return new Promise((resolve, reject) => {
    if (!isAllowedUrl(url)) return reject(new DownloadError('BAD_URL', 'Solo se permiten descargas https://'));
    const u = new URL(url);
    const lib = u.protocol === 'https:' ? https : http;
    const req = lib.get(u, { headers: { 'User-Agent': 'ZUENA-Plugins', Accept: '*/*' } }, (res) => {
      const { statusCode, headers } = res;
      if (statusCode >= 300 && statusCode < 400 && headers.location) {
        res.resume();
        if (redirects >= MAX_REDIRECTS) return reject(new DownloadError('REDIRECTS', 'Demasiadas redirecciones'));
        const next = new URL(headers.location, u).toString();
        return request(next, signal, redirects + 1).then(resolve, reject);
      }
      if (statusCode !== 200) {
        res.resume();
        return reject(new DownloadError('HTTP', `El servidor respondió ${statusCode}`));
      }
      resolve(res);
    });
    req.setTimeout(20000, () => req.destroy(new DownloadError('TIMEOUT', 'El servidor no responde')));
    req.on('error', (e) => reject(e instanceof DownloadError ? e : new DownloadError('NETWORK', `Error de red: ${e.message}`)));
    if (signal) {
      if (signal.aborted) req.destroy(new DownloadError('CANCELED', 'Descarga cancelada'));
      signal.addEventListener('abort', () => req.destroy(new DownloadError('CANCELED', 'Descarga cancelada')), { once: true });
    }
  });
}

/**
 * Descarga `url` a `dest`. Escribe primero en `dest.part` y solo renombra si
 * tamaño y SHA-256 coinciden. En cualquier error borra el parcial.
 * @param {{url: string, dest: string, sha256: string, size?: number, signal?: AbortSignal,
 *          onProgress?: (p: {received: number, total: number, bytesPerSec: number}) => void}} opts
 */
async function download({ url, dest, sha256, size, signal, onProgress }) {
  const part = `${dest}.part`;
  const hash = crypto.createHash('sha256');
  let received = 0;
  try {
    const res = await request(url, signal);
    const total = Number(res.headers['content-length']) || size || 0;
    if (size && total && total !== size) {
      res.resume();
      throw new DownloadError('SIZE', `Tamaño inesperado (${total} B, se esperaban ${size} B)`);
    }
    const out = fs.createWriteStream(part);
    const started = Date.now();
    let lastEmit = 0;
    await new Promise((resolve, reject) => {
      let settled = false;
      const fail = (e) => {
        if (settled) return;
        settled = true;
        out.destroy();
        if (signal?.aborted) return reject(new DownloadError('CANCELED', 'Descarga cancelada'));
        return reject(e instanceof DownloadError ? e : new DownloadError('NETWORK', `Error de red: ${e.message}`));
      };
      res.on('data', (chunk) => {
        received += chunk.length;
        hash.update(chunk);
        if (size && received > size) {
          res.destroy(new DownloadError('SIZE', 'El archivo es más grande de lo anunciado'));
          return;
        }
        const now = Date.now();
        if (onProgress && now - lastEmit >= PROGRESS_INTERVAL_MS) {
          lastEmit = now;
          onProgress({ received, total, bytesPerSec: received / Math.max((now - started) / 1000, 0.001) });
        }
      });
      res.on('error', fail);
      res.on('aborted', () => fail(new DownloadError('NETWORK', 'Conexión interrumpida')));
      out.on('error', (e) => fail(new DownloadError('DISK', `No se pudo escribir en disco: ${e.message}`)));
      out.on('finish', resolve);
      res.pipe(out);
    });
    if (size && received !== size) throw new DownloadError('SIZE', `Descarga incompleta (${received}/${size} B)`);
    const digest = hash.digest('hex');
    if (digest !== String(sha256).toLowerCase()) {
      throw new DownloadError('CHECKSUM', 'La verificación SHA-256 falló: el archivo está dañado o fue alterado');
    }
    onProgress?.({ received, total: received, bytesPerSec: 0 });
    await fsp.rename(part, dest);
    return { path: dest, bytes: received, sha256: digest };
  } catch (e) {
    await fsp.rm(part, { force: true }).catch(() => {});
    throw e;
  }
}

module.exports = { download, DownloadError };
