// Renderiza el vídeo de fondo de la ficha de ZUENA EQ (vertical, claro y oscuro) y sus pósters.
// La versión sale de Free-VST-Client/catalog/catalog.json (que a su vez sale de CMake).
// Uso: node render.mjs   (o `npm run render`)
import { execFileSync } from 'node:child_process';
import { readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const packages = join(here, '..', '..', 'Free-VST-Client', 'catalog', 'packages');
const catalog = JSON.parse(readFileSync(join(packages, '..', 'catalog.json'), 'utf8'));
const version = catalog.plugins.find((p) => p.id === 'zuena-eq').version;
const remotion = (args) => execFileSync(process.execPath, [join(here, 'node_modules', '@remotion', 'cli', 'remotion-cli.js'), ...args], { cwd: here, stdio: 'inherit' });

for (const theme of ['light', 'dark']) {
  // Las props van en un archivo: evita problemas de comillas en la línea de comandos de Windows.
  const propsFile = join(tmpdir(), `zuena-eq-props-${theme}.json`);
  writeFileSync(propsFile, JSON.stringify({ version, theme }));
  const suffix = theme === 'dark' ? '-oscuro' : '';
  const video = join(packages, `zuena-eq-stage${suffix}.mp4`);
  const poster = join(packages, `zuena-eq-stage${suffix}.jpg`);
  remotion(['render', 'src/index.ts', 'ZuenaEqStage', video, '--codec=h264', '--crf=27', '--muted', `--props=${propsFile}`]);
  remotion(['still', 'src/index.ts', 'ZuenaEqStage', poster, '--frame=70', '--image-format=jpeg', '--jpeg-quality=86', `--props=${propsFile}`]);
  rmSync(propsFile, { force: true });
  console.log(`ZUENA EQ v${version} (${theme}): ${video} (${(statSync(video).size / 1024).toFixed(0)} KB)`);
}
// El vídeo apaisado anterior ya no se usa en el launcher.
for (const old of ['zuena-eq-intro.mp4', 'zuena-eq-poster.jpg']) rmSync(join(packages, old), { force: true });
