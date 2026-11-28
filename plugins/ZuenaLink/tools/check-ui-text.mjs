// EB-14 en ZUENA Link: todo texto con caracteres fuera de ASCII (tildes, «», ·, …) que llega a JUCE tiene
// que pasar por utf() (juce::String::fromUTF8). Un literal crudo se interpreta como ASCII y sale roto.
// Revisa la interfaz y el procesador; ignora comentarios.
import { readFileSync } from 'node:fs';

const files = ['../Source/UI/Editor.cpp', '../Source/Processor.cpp', '../Source/State/Parameters.cpp'];
let bad = 0;
for (const rel of files) {
  const text = readFileSync(new URL(rel, import.meta.url), 'utf8');
  text.split('\n').forEach((line, i) => {
    const code = line.replace(/\/\/.*$/, '');
    for (const m of code.matchAll(/"((?:[^"\\]|\\.)*)"/g)) {
      if (!/[^\x00-\x7F]/.test(m[1])) continue;
      const before = code.slice(0, m.index);
      if (/(utf|fromUTF8)\(\s*$/.test(before)) continue;
      console.error(`EB-14: ${rel.replace('../', '')}:${i + 1} literal sin utf(): "${m[1]}"`);
      bad++;
    }
  });
}
process.exitCode = bad ? 1 : 0;
if (!bad) console.log('EB-14 OK: todos los textos con tildes o símbolos de ZUENA Link pasan por utf()');
