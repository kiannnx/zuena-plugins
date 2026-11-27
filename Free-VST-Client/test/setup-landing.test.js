'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const web = path.join(__dirname, '../../web/descarga');
const html = fs.readFileSync(path.join(web, 'index.html'), 'utf8');
const source = fs.readFileSync(path.join(web, 'site.js'), 'utf8').split('// ---------- Arranque ----------')[0];
const portable = { url: 'https://example.com/ZUENA-Plugins-0.2.3-win-x64.zip', sha256: 'a'.repeat(64), size: 100 };
const installer = { url: 'https://example.com/ZUENA-Plugins-0.2.3-Setup-x64.exe', sha256: 'b'.repeat(64), size: 200 };

function render(installers) {
  const nodes = new Map();
  function element(selector) {
    if (!nodes.has(selector)) {
      nodes.set(selector, {
        addEventListener() {},
        getAttribute(name) { return this[name]; },
        replaceChildren() {},
      });
    }
    return nodes.get(selector);
  }
  const document = { querySelector: element, querySelectorAll: (selector) => [element(selector)] };
  const manifest = { version: '0.2.2', published: '2026-11-26', downloads: { win32: portable }, installers };
  vm.runInNewContext(source + '\nrenderLauncher(manifest);', { document, navigator: {}, URL, manifest });
  return element;
}

test('instalador: la landing usa el enlace, la huella y la guía del EXE cuando está publicado', () => {
  const view = render({ win32: installer });
  assert.equal(view('[data-download]').href, installer.url);
  assert.equal(view('#v-sha').textContent, installer.sha256);
  assert.equal(view('#vt-link').href.split('/').pop(), installer.sha256);
  assert.equal(view('[data-download-name]').textContent, installer.url.split('/').pop());
  assert.equal(view('#download-portable').href, portable.url);
  assert.equal(view('#download-portable').hidden, false);
  assert.equal(view('#steps-setup').hidden, false);
  assert.equal(view('#steps-portable').hidden, true);
  assert.match(html, /id="steps-setup" hidden/);
  assert.match(html, /id="download-portable" hidden/);
});

test('instalador: el manifiesto anterior o un instalador inválido mantienen una descarga portátil coherente', () => {
  for (const assets of [
    undefined, { win32: { ...installer, sha256: 'inválido' } }, { win32: { ...installer, url: 'https://' } },
  ]) {
    const view = render(assets);
    assert.equal(view('[data-download]').href, portable.url);
    assert.equal(view('#v-sha').textContent, portable.sha256);
    assert.equal(view('#download-portable').hidden, true);
    assert.equal(view('#steps-setup').hidden, true);
    assert.equal(view('#steps-portable').hidden, false);
  }
});
