// Verificación estática de contrato UI <-> APVTS (EB-01, EB-02, EB-03) sin binarios sin firmar (EB-12).
// Comprueba los 365 parámetros APVTS (24*15 + 5 globales), todos los botones/perillas de barra superior,
// barra inferior y ficha flotante, atajos de teclado y presupuesto horizontal de layout.
// Uso: node tools/verify-ui-contract.mjs

import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const src = join(dirname(fileURLToPath(import.meta.url)), '..', 'Source');
const read = (rel) => readFileSync(join(src, rel), 'utf8');

const paramsH = read('State/Parameters.h');
const paramsCpp = read('State/Parameters.cpp');
const cardCpp = read('UI/BandCard.cpp');
const accessH = read('UI/BandAccess.h');
const editorCpp = read('UI/PluginEditor.cpp');
const editorH = read('UI/PluginEditor.h');
const graphCpp = read('UI/EqGraph.cpp');
const themeH = read('UI/Theme.h');

let checks = 0, failures = 0;
function check(ok, msg) {
  checks++;
  if (!ok) {
    failures++;
    console.log(`FAIL ${msg}`);
  }
}

// 1. Los 15 BandParam están conectados en la UI (EB-02)
const enumMatch = paramsH.match(/enum class BandParam\s*:\s*int\s*\{([^}]+)\}/);
const bandParams = enumMatch[1].split(',').map((s) => s.trim()).filter((s) => s && s !== 'Count');
check(bandParams.length === 15, `BandParam tiene 15 parámetros por banda (encontrados ${bandParams.length})`);

const uiAll = cardCpp + '\n' + accessH + '\n' + graphCpp;
for (const bp of bandParams) {
  check(uiAll.includes(`BandParam::${bp}`), `EB-02: BandParam::${bp} conectado en la UI`);
}

// 2. Los 5 parámetros globales de Parameters.h están registrados en createLayout() y vinculados en PluginEditor (EB-02 / EB-03)
const globalIds = [...paramsH.matchAll(/inline const juce::String (\w+)\s*\{\s*"([^"]+)"\s*\}/g)];
check(globalIds.length === 5, `5 parámetros globales en Parameters.h (encontrados ${globalIds.length})`);
for (const [, sym, id] of globalIds) {
  check(paramsCpp.includes(sym), `Parámetro global ${sym} ("${id}") registrado en createLayout()`);
  check(editorCpp.includes(`params::${sym}`), `EB-02/03: Parámetro global ${sym} ("${id}") vinculado en PluginEditor.cpp`);
}

// 3. Todos los controles de BandCard.h tienen attachment o acción real en BandCard.cpp (EB-01 / EB-03)
const cardControls = [
  'prevBtn', 'nextBtn', 'closeBtn', 'type', 'slope', 'placement',
  'bypass', 'solo', 'dynamic', 'invertGain', 'remove',
  'freq', 'gain', 'q', 'threshold', 'range', 'ratio', 'attack', 'release',
];
for (const c of cardControls) {
  const bound = cardCpp.includes(`, ${c},`) || cardCpp.includes(`${c}.attach`) || cardCpp.includes(`${c}.onClick`);
  check(bound, `EB-01/03: BandCard::${c} tiene attachment o acción real`);
}

// 4. Todos los controles de PluginEditor.h tienen attachment o acción real en PluginEditor.cpp (EB-01)
const editorControls = [
  'presetBox', 'qualityBox', 'slotABtn', 'slotBBtn', 'copyAbBtn',
  'analyzerButtons', 'rangeButtons', 'undo', 'redo',
  'autoGainBtn', 'globalBypassBtn', 'gainScale', 'output',
  'addBandBtn', 'autoListenBtn', 'freezeBtn', 'phaseBtn', 'darkBtn', 'helpBtn',
];
for (const c of editorControls) {
  const bound = editorCpp.includes(`${c}.onClick`) || editorCpp.includes(`${c}.onChange`)
    || editorCpp.includes(`, ${c},`) || editorCpp.includes(`${c}.attach`) || editorCpp.includes(`${c}[`);
  check(bound, `EB-01: PluginEditor::${c} tiene attachment o acción real`);
}

// 5. Atajos de teclado, gestos ergonómicos y Modo Oscuro oficial ZUENA ("Punto")
for (const token of ['selectAllBands', 'toggleBypassSelection', 'toggleDynamicSelection', 'nudgeSelection', 'lassoActive', 'drawGhostPreview', 'setDarkMode']) {
  check((editorCpp + '\n' + graphCpp).includes(token), `Ergonomía: ${token} implementado`);
}
for (const hex of ['0xff101112', '0xff191b1d', '0xff222528', '0xfff1f1ef', '0xffa6a9ad']) {
  check(themeH.includes(hex), `Modo Oscuro ZUENA (brand-system.css): token ${hex} presente en Theme.h`);
}

// 6. La barra vive en el componente raíz, en píxeles lógicos antes del escalado EB-29.
const minWidth = Number(editorH.match(/\bkMinW\s*=\s*(\d+)\b/)?.[1]);
const margin = Number(editorCpp.match(/\bkMargin\s*=\s*(\d+)\b/)?.[1]);
const header = editorCpp.match(/auto header = r\.removeFromTop\s*\(\s*kHeader\s*\)\.reduced\s*\(\s*kMargin\s*,\s*0\s*\);([\s\S]*?)auto footer\s*=/)?.[1] ?? '';
const sum = (values) => values.reduce((total, value) => total + value, 0);
const staticWidths = [...header.matchAll(/header\.removeFrom(?:Left|Right)\s*\(\s*(\d+)\s*\)/g)].map(([, width]) => Number(width));
const groups = ['analyzer', 'range'].map((group) => {
  const widthList = header.match(new RegExp(`\\b${group}Widths\\[\\]\\s*=\\s*\\{([\\d,\\s]+)\\}`))?.[1] ?? '';
  const widths = (widthList.match(/\d+/g) ?? []).map(Number);
  const count = Number(editorH.match(new RegExp(`std::array<juce::TextButton,\\s*(\\d+)>\\s+${group}Buttons\\b`))?.[1]);
  const loop = header.match(new RegExp(`i < ${group}Buttons\\.size\\(\\)[^\\n]*\\s*\\{([^}]+)\\}`))?.[1] ?? '';
  const gap = Number(loop.match(/header\.removeFromLeft\s*\(\s*(\d+)\s*\)/)?.[1]);
  const usesWidths = new RegExp(`header\\.removeFromLeft\\s*\\(\\s*${group}Widths\\[i\\]\\s*\\)`).test(loop);
  check(widths.length > 0 && widths.length === count && Number.isFinite(gap) && usesWidths,
    `EB-33: anchos y separación del grupo ${group} coinciden con los botones del editor`);
  // staticWidths ya incluye una iteración del espacio entre botones.
  return sum(widths) + (count - 1) * gap;
});
const usedWidth = 2 * margin + sum(staticWidths) + sum(groups);
check(staticWidths.length > 0 && minWidth >= usedWidth,
  `EB-33: la barra superior cabe en el ancho lógico mínimo (${minWidth} px >= ${usedWidth} px requeridos)`);

// 7. EB-17: el tema es global al proceso; cada editor compara contra SU modo aplicado (nunca impone su
//    proyecto sobre el global en cada fotograma, que hacía parpadear dos instancias en modos distintos).
const frameBody = (editorCpp.match(/void ZuenaEqEditor::frame\(\)\s*\{([\s\S]*?)\n\}/) || [])[1] || '';
check(frameBody.length > 0, 'EB-17: cuerpo de ZuenaEqEditor::frame() encontrado');
check(/treeDark\s*!=\s*appliedDark/.test(frameBody), 'EB-17: frame() compara ui_dark del proyecto contra appliedDark');
check(/theme::isDark\(\)\s*!=\s*appliedDark/.test(frameBody), 'EB-17: frame() sigue el modo global si otra instancia lo cambió');
check(!/treeDark\s*!=\s*theme::isDark\(\)/.test(frameBody), 'EB-17: frame() no impone el proyecto sobre el tema global');
check(/appliedDark\s*=\s*dark/.test(editorCpp), 'EB-17: setDarkMode() registra appliedDark');
// 8. EB-15: temporizador de respaldo si el host no envía vblank
check(/startTimerHz\s*\(/.test(editorCpp) && /void ZuenaEqEditor::timerCallback\(\)[\s\S]*?frame\(\)/.test(editorCpp), 'EB-15: temporizador de respaldo llama a frame()');

console.log(`Contrato UI <-> APVTS v0.3: ${checks} comprobaciones, ${failures} fallas (365 parámetros, barra=${usedWidth}/${minWidth} px)`);
process.exitCode = failures ? 1 : 0;
