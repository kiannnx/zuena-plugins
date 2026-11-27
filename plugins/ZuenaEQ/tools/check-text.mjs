// Regresión EB-14: JUCE interpreta los literales `const char*` como ASCII, así que todo literal
// con acentos o símbolos (á, é, ±, ·) dentro de Source/ tiene que pasar por zeq::utf8() o
// juce::String::fromUTF8(). Uso: node check-text.mjs [carpeta Source]
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, dirname, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = process.argv[2] ?? join(dirname(fileURLToPath(import.meta.url)), '..', 'Source');
const files = [];
const walk = (dir) => {
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) walk(p);
    else if (/\.(h|hpp|cpp)$/.test(name)) files.push(p);
  }
};
walk(root);

const wrapper = /(utf8|fromUTF8)\s*\(\s*$/;
const problems = [];
for (const file of files) {
  const text = readFileSync(file, 'utf8');
  // Recorre el archivo saltando comentarios y detectando literales de cadena.
  let i = 0, line = 1;
  while (i < text.length) {
    const c = text[i];
    if (c === '\n') { line++; i++; continue; }
    if (c === '/' && text[i + 1] === '/') { while (i < text.length && text[i] !== '\n') i++; continue; }
    if (c === '/' && text[i + 1] === '*') {
      i += 2;
      while (i < text.length && !(text[i] === '*' && text[i + 1] === '/')) { if (text[i] === '\n') line++; i++; }
      i += 2; continue;
    }
    if (c === "'") { i++; while (i < text.length && text[i] !== "'") i += text[i] === '\\' ? 2 : 1; i++; continue; }
    if (c === '"') {
      const start = i;
      i++;
      while (i < text.length && text[i] !== '"') i += text[i] === '\\' ? 2 : 1;
      const literal = text.slice(start, i + 1);
      i++;
      if (/[^\x00-\x7F]/.test(literal) && !wrapper.test(text.slice(Math.max(0, start - 40), start)))
        problems.push(`${relative(root, file)}:${line}: ${literal}`);
      continue;
    }
    i++;
  }
}

if (problems.length) {
  console.log('EB-14: literales con texto no ASCII sin utf8():');
  for (const p of problems) console.log('  ' + p);
  process.exit(1);
}
console.log(`EB-14 OK: ${files.length} archivos sin literales no ASCII sueltos`);
