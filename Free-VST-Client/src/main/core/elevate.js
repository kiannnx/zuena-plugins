'use strict';
// Operaciones de archivo con permisos de administrador (Windows, UAC).
// Las rutas viajan en un JSON temporal, nunca interpoladas en la línea de comandos.

const { execFile } = require('node:child_process');
const fsp = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');

const PS_SCRIPT = `param([string]$OpsFile)
$ErrorActionPreference = 'Stop'
try {
  $ops = Get-Content -Raw -Encoding UTF8 -LiteralPath $OpsFile | ConvertFrom-Json
  foreach ($o in $ops) {
    if ($o.op -eq 'replace') {
      $parent = Split-Path -Parent $o.to
      if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
      $bak = $o.to + '.zuena-old'
      if (Test-Path -LiteralPath $bak) { Remove-Item -LiteralPath $bak -Recurse -Force }
      # Renombrar primero: si el DAW tiene la DLL cargada falla sin borrar nada (EB-19).
      if (Test-Path -LiteralPath $o.to) {
        try { Rename-Item -LiteralPath $o.to -NewName (Split-Path -Leaf $bak) }
        catch { throw ((Split-Path -Leaf $o.to) + ' está en uso. Cerrá tu DAW y probá de nuevo.') }
      }
      try { Copy-Item -LiteralPath $o.from -Destination $o.to -Recurse -Force }
      catch {
        if (Test-Path -LiteralPath $o.to) { Remove-Item -LiteralPath $o.to -Recurse -Force }
        if (Test-Path -LiteralPath $bak) { Rename-Item -LiteralPath $bak -NewName (Split-Path -Leaf $o.to) }
        throw
      }
      if (Test-Path -LiteralPath $bak) { Remove-Item -LiteralPath $bak -Recurse -Force }
    } elseif ($o.op -eq 'remove') {
      if (Test-Path -LiteralPath $o.path) {
        $bak = $o.path + '.zuena-old'
        if (Test-Path -LiteralPath $bak) { Remove-Item -LiteralPath $bak -Recurse -Force }
        try { Rename-Item -LiteralPath $o.path -NewName (Split-Path -Leaf $bak) }
        catch { throw ((Split-Path -Leaf $o.path) + ' está en uso. Cerrá tu DAW y probá de nuevo.') }
        Remove-Item -LiteralPath $bak -Recurse -Force
      }
    } elseif ($o.op -eq 'mkdir') {
      New-Item -ItemType Directory -Path $o.path -Force | Out-Null
    }
  }
  exit 0
} catch {
  $_.Exception.Message | Out-File -Encoding UTF8 -LiteralPath ($OpsFile + '.err')
  exit 2
}
`;

class ElevationError extends Error {
  constructor(code, message) {
    super(message);
    this.code = code;
  }
}

const psQuote = (s) => `'${String(s).replace(/'/g, "''")}'`;

/**
 * Ejecuta `ops` en un PowerShell elevado. Muestra el diálogo UAC de Windows.
 * @param {({op:'replace', from:string, to:string}|{op:'remove', path:string}|{op:'mkdir', path:string})[]} ops
 */
async function runElevated(ops) {
  if (process.platform !== 'win32') {
    throw new ElevationError('UNSUPPORTED', 'Sin permisos de escritura en la carpeta de instalación. Elegí otra carpeta en Ajustes.');
  }
  const dir = await fsp.mkdtemp(path.join(os.tmpdir(), 'zuena-elev-'));
  const script = path.join(dir, 'ops.ps1');
  const opsFile = path.join(dir, 'ops.json');
  try {
    await fsp.writeFile(script, '﻿' + PS_SCRIPT, 'utf8');
    await fsp.writeFile(opsFile, JSON.stringify(ops), 'utf8');
    const inner = [
      `$p = Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait -PassThru -WindowStyle Hidden`,
      `-ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',${psQuote(`"${script}"`)},'-OpsFile',${psQuote(`"${opsFile}"`)});`,
      'exit $p.ExitCode',
    ].join(' ');
    const code = await new Promise((resolve) => {
      execFile('powershell.exe', ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-Command', inner],
        { windowsHide: true }, (err) => resolve(err ? (typeof err.code === 'number' ? err.code : 1) : 0));
    });
    if (code === 0) return;
    if (code === 2) {
      const msg = await fsp.readFile(`${opsFile}.err`, 'utf8').catch(() => 'error desconocido');
      throw new ElevationError('FAILED', `La operación con permisos de administrador falló: ${msg.replace(/^﻿/, '').trim()}`);
    }
    throw new ElevationError('DENIED', 'Se necesita permiso de administrador para escribir en la carpeta VST3 y fue rechazado.');
  } finally {
    await fsp.rm(dir, { recursive: true, force: true }).catch(() => {});
  }
}

module.exports = { runElevated, ElevationError };
