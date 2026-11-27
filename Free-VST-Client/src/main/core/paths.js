'use strict';

const os = require('node:os');
const path = require('node:path');

/** Carpeta VST3 estándar del sistema. */
function defaultVst3Dir(platform = process.platform, env = process.env) {
  if (platform === 'win32') {
    const common = env.CommonProgramFiles || path.join(env.ProgramFiles || 'C:\\Program Files', 'Common Files');
    return path.join(common, 'VST3');
  }
  if (platform === 'darwin') return path.join(os.homedir(), 'Library', 'Audio', 'Plug-Ins', 'VST3');
  return path.join(os.homedir(), '.vst3');
}

module.exports = { defaultVst3Dir };
