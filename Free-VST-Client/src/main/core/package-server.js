'use strict';
// Servidor local (127.0.0.1) para los paquetes .zip, vídeos e imágenes incluidos en catalog/packages/.
// Permite que el catálogo integrado pase exactamente por el mismo flujo de descarga,
// progreso, verificación SHA-256 e instalación que un servidor remoto HTTPS.

const http = require('node:http');
const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const { isInside } = require('./installer');

const MIME = {
  '.zip': 'application/zip',
  '.mp4': 'video/mp4',
  '.webm': 'video/webm',
  '.jpg': 'image/jpeg',
  '.png': 'image/png',
};

function startPackageServer(packagesDir) {
  const root = path.resolve(packagesDir);
  return new Promise((resolve, reject) => {
    const server = http.createServer(async (req, res) => {
      try {
        const rawName = decodeURIComponent(new URL(req.url, 'http://127.0.0.1').pathname).replace(/^\/+/, '');
        if (!rawName || rawName.includes('/') || rawName.includes('\\')) {
          res.writeHead(404);
          return res.end();
        }
        const filePath = path.resolve(root, rawName);
        if (!isInside(root, filePath)) {
          res.writeHead(403);
          return res.end();
        }
        const st = await fsp.stat(filePath);
        if (!st.isFile()) {
          res.writeHead(404);
          return res.end();
        }
        const type = MIME[path.extname(filePath).toLowerCase()] ?? 'application/octet-stream';
        const range = req.headers.range;
        if (range && /^bytes=\d*-\d*$/.test(range)) {
          const [sStr, eStr] = range.slice(6).split('-');
          const start = sStr ? Number(sStr) : 0;
          const end = eStr ? Math.min(Number(eStr), st.size - 1) : st.size - 1;
          if (start >= st.size || start > end) {
            res.writeHead(416, { 'Content-Range': `bytes */${st.size}` });
            return res.end();
          }
          res.writeHead(206, {
            'Content-Type': type,
            'Content-Length': end - start + 1,
            'Content-Range': `bytes ${start}-${end}/${st.size}`,
            'Accept-Ranges': 'bytes',
            'Cache-Control': 'no-store',
          });
          return fs.createReadStream(filePath, { start, end }).pipe(res);
        }
        res.writeHead(200, {
          'Content-Type': type,
          'Content-Length': st.size,
          'Accept-Ranges': 'bytes',
          'Cache-Control': 'no-store',
        });
        return fs.createReadStream(filePath).pipe(res);
      } catch {
        if (!res.headersSent) res.writeHead(404);
        return res.end();
      }
    });
    server.on('error', reject);
    server.listen(0, '127.0.0.1', () => {
      resolve({ base: `http://127.0.0.1:${server.address().port}`, close: () => server.close() });
    });
  });
}

module.exports = { startPackageServer };
