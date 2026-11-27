'use strict';
// Servidor local del MODO DEMO: publica un catálogo y paquetes de PRUEBA reales
// (zip + SHA-256) en 127.0.0.1 para ejercitar descarga → verificación → instalación
// sin depender de internet. Los paquetes no son plugins de audio: solo contienen
// la estructura de un bundle .vst3 y un LEEME.

const http = require('node:http');
const crypto = require('node:crypto');
const { createZip } = require('./zip');

const THROTTLE_BYTES_PER_SEC = 2.5 * 1024 * 1024;

function demoPackage(name, version, sizeMb) {
  const bundle = `${name}.vst3`;
  const filler = crypto.createHash('sha256').update(name).digest();
  const big = Buffer.alloc(Math.round(sizeMb * 1024 * 1024));
  for (let i = 0; i < big.length; i++) big[i] = filler[i % 32] ^ (i & 0xff);
  return createZip([
    { name: `${bundle}/` },
    { name: `${bundle}/Contents/` },
    { name: `${bundle}/Contents/Resources/` },
    { name: `${bundle}/Contents/Resources/LEEME.txt`, data: `PAQUETE DE PRUEBA de ZUENA Plugins (${name} v${version}).\nNo es un plugin de audio: solo valida descarga, verificacion e instalacion.\n` },
    { name: `${bundle}/Contents/Resources/payload.bin`, data: big },
  ]);
}

const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

// Ficha completa sin vídeo ni galería: ejercita la carátula de respaldo y las secciones de texto.
const DEMO_DETAIL = {
  headline: 'Un paquete de prueba que *recorre* todo el flujo.',
  summary: 'Sirve para ver la ficha completa y probar descarga, verificación e instalación sin internet.',
  highlights: [
    { icon: 'tempo', title: 'Descarga con progreso', text: 'Velocidad limitada a propósito para ver la barra y poder cancelar.' },
    { icon: 'compare', title: 'Verificación SHA-256', text: 'El launcher compara la huella antes de instalar.' },
  ],
  specs: [['Formato', 'VST3 (estructura de prueba)'], ['Contenido', 'Sin audio']],
  changelog: [{ version: '1.0.0', items: ['Primera versión del paquete de prueba.'] }],
};

function buildDemo(base) {
  const files = new Map();
  const defs = [
    { id: 'demo-air-riser', name: 'Demo Air Riser', icon: 'spark', accent: ['#5ff0ff', '#b9a8ff'], mb: 6, tagline: 'Paquete de prueba · subida de aire', tags: ['Prueba', 'Entrada'] },
    { id: 'demo-tape-stop', name: 'Demo Tape Stop', icon: 'knob', accent: ['#ffd28a', '#ffb3e1'], mb: 4, tagline: 'Paquete de prueba · frenada de cinta', tags: ['Prueba', 'Salida'] },
    { id: 'demo-dub-echo', name: 'Demo Dub Echo', icon: 'echo', accent: ['#b9a8ff', '#ffb3e1'], mb: 8, tagline: 'Paquete de prueba · eco dub', tags: ['Prueba', 'Salida'] },
  ];
  const plugins = [{
    id: 'voxtransit', name: 'VoxTransit', status: 'coming-soon', version: '0.1.0', icon: 'mic',
    accent: ['#5ff0ff', '#b9a8ff'], category: 'Transiciones vocales', bundle: 'VoxTransit.vst3',
    tagline: 'Transiciones vocales sincronizadas al BPM',
    description: 'Capturá una voz desde tu pista y convertila en entradas y salidas: risers, reverse swells, tape stops y dub throws. Arrastrá el resultado directo a tu DAW.',
    tags: ['Voz', 'Transiciones', 'Drag & Drop'],
  }];
  defs.forEach((d, i) => {
    const zip = demoPackage(d.name, '1.0.0', d.mb);
    files.set(`/pkg/${d.id}.zip`, zip);
    plugins.push({
      id: d.id, name: d.name, version: '1.0.0', status: 'available', icon: d.icon, accent: d.accent,
      category: 'Paquete de prueba', bundle: `${d.name}.vst3`, tagline: d.tagline, tags: d.tags,
      description: 'Paquete de PRUEBA servido localmente por el modo demo. Sirve para comprobar el flujo completo: descarga con progreso, verificación SHA-256, instalación, apertura de carpeta y desinstalación. No contiene un plugin de audio.',
      downloads: { win32: { url: `${base}/pkg/${d.id}.zip`, sha256: sha(zip), size: zip.length }, darwin: { url: `${base}/pkg/${d.id}.zip`, sha256: sha(zip), size: zip.length } },
      ...(i === 0 ? DEMO_DETAIL : {}),
    });
  });
  const broken = demoPackage('Demo Corrupto', '1.0.0', 1);
  files.set('/pkg/demo-corrupto.zip', broken);
  plugins.push({
    id: 'demo-corrupto', name: 'Prueba: archivo alterado', version: '1.0.0', status: 'available', icon: 'filter',
    accent: ['#62666a', '#a6a9ad'], category: 'Paquete de prueba',
    tagline: 'Debe FALLAR la verificación SHA-256', tags: ['Prueba', 'Seguridad'],
    description: 'Este paquete se publica con un SHA-256 incorrecto a propósito. El launcher debe rechazarlo y no instalar nada.',
    downloads: { win32: { url: `${base}/pkg/demo-corrupto.zip`, sha256: '0'.repeat(64), size: broken.length }, darwin: { url: `${base}/pkg/demo-corrupto.zip`, sha256: '0'.repeat(64), size: broken.length } },
  });
  files.set('/catalog.json', Buffer.from(JSON.stringify({ schema: 1, updated: new Date().toISOString(), featured: 'voxtransit', plugins })));
  return files;
}

/** Arranca el servidor demo en un puerto libre de 127.0.0.1. */
function startDemoServer() {
  return new Promise((resolve, reject) => {
    let files = null;
    const server = http.createServer((req, res) => {
      const buf = files?.get(new URL(req.url, 'http://x').pathname);
      if (!buf) { res.writeHead(404); return res.end(); }
      const type = req.url.endsWith('.json') ? 'application/json' : 'application/zip';
      res.writeHead(200, { 'Content-Type': type, 'Content-Length': buf.length, 'Cache-Control': 'no-store' });
      if (type !== 'application/zip') return res.end(buf);
      const chunk = 64 * 1024;
      const delay = (chunk / THROTTLE_BYTES_PER_SEC) * 1000;
      let off = 0;
      const pump = () => {
        if (res.destroyed) return;
        if (off >= buf.length) return res.end();
        res.write(buf.subarray(off, off + chunk));
        off += chunk;
        setTimeout(pump, delay);
      };
      pump();
    });
    server.on('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const base = `http://127.0.0.1:${server.address().port}`;
      files = buildDemo(base);
      resolve({ url: `${base}/catalog.json`, base, close: () => server.close() });
    });
  });
}

module.exports = { startDemoServer, demoPackage };
