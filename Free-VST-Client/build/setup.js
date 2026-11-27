'use strict';

const { execFileSync } = require('node:child_process');
const { createHash } = require('node:crypto');
const fs = require('node:fs/promises');
const { createReadStream } = require('node:fs');
const path = require('node:path');
const { version } = require('../package.json');

const source = path.join(__dirname, 'setup');
const dist = path.join(__dirname, '..', 'dist');
const zipName = (v) => `ZUENA-Plugins-${v}-win-x64.zip`;
const setupName = (v) => `ZUENA-Plugins-${v}-Setup-x64.exe`;

function compile(output, files, console = false) {
  if (process.platform !== 'win32') throw new Error('El instalador se compila en Windows.');
  const framework = path.join(process.env.SystemRoot, 'Microsoft.NET', 'Framework64', 'v4.0.30319');
  execFileSync(path.join(framework, 'csc.exe'), [
    '/nologo', '/codepage:65001', '/warnaserror+', '/optimize+', '/platform:x64',
    ...(console ? ['/target:exe', '/main:Zuena.Setup.InstallChecks'] : ['/target:winexe']),
    `/out:${output}`, `/win32icon:${path.join(__dirname, 'icon.ico')}`,
    ...['System.IO.Compression', 'System.Drawing', 'System.Windows.Forms'].map((name) => `/reference:${path.join(framework, name + '.dll')}`),
    ...files,
  ], { stdio: 'inherit' });
}

// Alinea el final a 8 bytes para que SignTool agregue su tabla sin insertar relleno tras el pie.
async function appendPayload(stub, zip, output) {
  await fs.copyFile(stub, output);
  const file = await fs.open(output, 'a');
  try {
    const stubSize = (await file.stat()).size;
    const zipSize = (await fs.stat(zip)).size;
    const padding = (8 - (stubSize + zipSize) % 8) % 8;
    if (padding) await file.writeFile(Buffer.alloc(padding));
    const hash = createHash('sha256');
    let size = 0n;
    for await (const chunk of createReadStream(zip)) {
      await file.writeFile(chunk);
      hash.update(chunk);
      size += BigInt(chunk.length);
    }
    if (size !== BigInt(zipSize)) throw new Error('El ZIP cambió durante el empaquetado.');
    const footer = Buffer.alloc(48);
    footer.write('ZUENAS01');
    footer.writeBigInt64LE(size, 8);
    hash.digest().copy(footer, 16);
    await file.writeFile(footer);
    await file.sync();
  } finally {
    await file.close();
  }
}

async function buildSetup(zip, output, releaseVersion) {
  if (!/^\d+\.\d+\.\d+$/.test(releaseVersion)) throw new Error('Versión de launcher inválida.');
  const scratch = await fs.mkdtemp(path.join(path.dirname(output), '.zuena-setup-build-'));
  try {
    const info = path.join(scratch, 'Version.cs');
    const stub = path.join(scratch, 'setup.exe');
    const candidate = path.join(scratch, 'installer.exe');
    await fs.writeFile(info, [
      'using System.Reflection;',
      '[assembly: AssemblyTitle("Instalador de ZUENA Plugins")]',
      '[assembly: AssemblyProduct("ZUENA Plugins")]',
      '[assembly: AssemblyCompany("ZUENA")]',
      `[assembly: AssemblyVersion("${releaseVersion}.0")]`,
      `[assembly: AssemblyFileVersion("${releaseVersion}.0")]`,
    ].join('\r\n'));
    compile(stub, [path.join(source, 'Wizard.cs'), path.join(source, 'Install.cs'), info]);
    await appendPayload(stub, zip, candidate);
    await fs.rename(candidate, output);
    console.log(`Instalador: ${output}`);
  } finally {
    await fs.rm(scratch, { force: true, recursive: true });
  }
}

async function verifySetup(installer, zip) {
  const file = await fs.open(installer, 'r');
  try {
    const { size } = await file.stat();
    const footerOffset = (await payloadEnd(file, size)) - 48;
    const zipOffset = footerOffset - zip.size;
    const footer = Buffer.alloc(48);
    if (zipOffset <= 0) throw new Error('El instalador está incompleto.');
    await file.read(footer, 0, footer.length, footerOffset);
    const hash = createHash('sha256');
    for await (const chunk of file.createReadStream({ start: zipOffset, end: footerOffset - 1, autoClose: false })) {
      hash.update(chunk);
    }
    if (footer.toString('ascii', 0, 8) !== 'ZUENAS01' || footer.readBigInt64LE(8) !== BigInt(zip.size)
        || footer.subarray(16).toString('hex') !== zip.sha256 || hash.digest('hex') !== zip.sha256) {
      throw new Error('El instalador no contiene la descarga portátil publicada.');
    }
  } finally {
    await file.close();
  }
}

async function payloadEnd(file, size) {
  async function read(length, position) {
    const buffer = Buffer.alloc(length);
    if (position < 0 || position > size - length
        || (await file.read(buffer, 0, length, position)).bytesRead !== length) {
      throw new Error('Cabecera PE incompleta.');
    }
    return buffer;
  }
  const dos = await read(64, 0);
  const pe = dos.readUInt32LE(0x3c);
  if (dos.readUInt16LE(0) !== 0x5a4d || pe < 64) throw new Error('Cabecera PE inválida.');
  const coff = await read(24, pe);
  const optionalSize = coff.readUInt16LE(20);
  if (coff.readUInt32LE(0) !== 0x00004550 || coff.readUInt16LE(4) !== 0x8664 || optionalSize < 152)
    throw new Error('El instalador debe ser un PE x64.');
  const optional = await read(optionalSize, pe + 24);
  if (optional.readUInt16LE(0) !== 0x20b) throw new Error('Cabecera PE32+ inválida.');
  if (optional.readUInt32LE(108) < 5) return size;
  const certificate = optional.readUInt32LE(144);
  const length = optional.readUInt32LE(148);
  if (!certificate && !length) return size;
  if (certificate < pe + 24 + optionalSize || certificate % 8 || length < 8 || certificate + length !== size)
    throw new Error('Tabla de certificados PE inválida.');
  return certificate;
}

if (require.main === module) {
  const zip = process.argv[2] ? path.resolve(process.argv[2]) : path.join(dist, zipName(version));
  buildSetup(zip, path.join(dist, setupName(version)), version).catch((error) => {
    console.error(error.message);
    process.exitCode = 1;
  });
}

module.exports = { appendPayload, buildSetup, compile, verifySetup, zipName, setupName };
