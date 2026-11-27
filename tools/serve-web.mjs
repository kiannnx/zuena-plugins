// Vista previa local de la página de descarga (web/descarga) tal como la sirve GitHub Pages:
// launcher.json y catalog.json salen de la copia local del repositorio público.
// Uso: node tools/serve-web.mjs [puerto]   → http://127.0.0.1:4310
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { dirname, extname, join, normalize, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const ROOTS = [join(ROOT, 'web', 'descarga'), join(ROOT, 'publish', 'zuena-plugins-releases')];
const TYPES = { '.html': 'text/html; charset=utf-8', '.css': 'text/css', '.js': 'text/javascript', '.json': 'application/json', '.svg': 'image/svg+xml', '.jpg': 'image/jpeg', '.png': 'image/png', '.woff2': 'font/woff2', '.txt': 'text/plain; charset=utf-8' };
const PORT = Number(process.argv[2]) || 4310;

createServer(async (req, res) => {
  const rel = normalize(decodeURIComponent(new URL(req.url, 'http://x').pathname)).replace(/^[\\/]+/, '') || 'index.html';
  for (const base of ROOTS) {
    const file = join(base, rel.endsWith(sep) || rel.endsWith('/') ? `${rel}index.html` : rel);
    if (!file.startsWith(base)) break; // sin salir de la carpeta
    try {
      const body = await readFile(file);
      res.writeHead(200, { 'Content-Type': TYPES[extname(file)] ?? 'application/octet-stream', 'Cache-Control': 'no-cache' });
      return res.end(body);
    } catch { /* siguiente carpeta */ }
  }
  res.writeHead(404);
  return res.end('No encontrado');
}).listen(PORT, '127.0.0.1', () => console.log(`Página de descarga: http://127.0.0.1:${PORT}`));
