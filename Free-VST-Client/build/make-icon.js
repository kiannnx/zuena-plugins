'use strict';
// Genera build/icon.png (512) y build/icon.ico (PNG embebidos 16–256) renderizando un SVG con Electron.
// Uso: npx electron build/make-icon.js

const { app, BrowserWindow } = require('electron');
const fs = require('node:fs');
const path = require('node:path');

// Logo oficial de ZUENA (brand/logo.svg): punto negro, Z blanca. No se redibuja ni lleva efectos.
const SVG = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" role="img" aria-label="ZUENA"><circle cx="32" cy="32" r="32" fill="#000"/><path d="M19 20h26L19 44h26" fill="none" stroke="#fff" stroke-width="6.5" stroke-linecap="round" stroke-linejoin="round"/></svg>';

function ico(pngs) {
  const header = Buffer.alloc(6 + 16 * pngs.length);
  header.writeUInt16LE(0, 0);
  header.writeUInt16LE(1, 2);
  header.writeUInt16LE(pngs.length, 4);
  let offset = header.length;
  pngs.forEach(({ size, data }, i) => {
    const e = 6 + i * 16;
    header.writeUInt8(size >= 256 ? 0 : size, e);
    header.writeUInt8(size >= 256 ? 0 : size, e + 1);
    header.writeUInt16LE(1, e + 4);
    header.writeUInt16LE(32, e + 6);
    header.writeUInt32LE(data.length, e + 8);
    header.writeUInt32LE(offset, e + 12);
    offset += data.length;
  });
  return Buffer.concat([header, ...pngs.map((p) => p.data)]);
}

app.whenReady().then(async () => {
  const win = new BrowserWindow({ width: 512, height: 512, show: false, transparent: true, frame: false, useContentSize: true, webPreferences: { offscreen: true } });
  await win.loadURL(`data:text/html,${encodeURIComponent(`<html><body style="margin:0;background:transparent">${SVG.replace('<svg ', '<svg width="512" height="512" ')}</body></html>`)}`);
  await new Promise((r) => setTimeout(r, 300));
  const img = await win.webContents.capturePage({ x: 0, y: 0, width: 512, height: 512 });
  const out = __dirname;
  fs.writeFileSync(path.join(out, 'icon.png'), img.toPNG());
  const sizes = [256, 128, 64, 48, 32, 16];
  fs.writeFileSync(path.join(out, 'icon.ico'), ico(sizes.map((size) => ({ size, data: img.resize({ width: size, height: size, quality: 'best' }).toPNG() }))));
  console.log('icon.png + icon.ico generados');
  app.quit();
});
