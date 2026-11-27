import { readFileSync } from 'node:fs';

const ui = readFileSync(new URL('../Source/UI/Editor.cpp', import.meta.url), 'utf8');
const calls = /\b(drawText|setButtonText|setTitle|setTooltip|addItem)\s*\(\s*"([^"\n]*)"/g;
const invalid = [...ui.matchAll(calls)].filter(([, , text]) => /[^\x00-\x7F]/.test(text));
for (const [, call, literal] of invalid)
  console.error(`EB-14: ${call} recibe "${literal}" sin decodificar UTF-8`);
process.exitCode = invalid.length > 0 ? 1 : 0;
if (!invalid.length)
  console.log('EB-14 OK: las llamadas directas de texto en Luma respetan UTF-8');
