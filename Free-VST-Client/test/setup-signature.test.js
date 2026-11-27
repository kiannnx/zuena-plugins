'use strict';

const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const { appendPayload, verifySetup } = require('../build/setup');

test('el instalador conserva el ZIP verificable al agregar la tabla Authenticode', async () => {
  const scratch = await fs.mkdtemp(path.join(os.tmpdir(), 'zuena-sign-test-'));
  try {
    const stub = path.join(scratch, 'stub.exe');
    const zip = path.join(scratch, 'launcher.zip');
    const setup = path.join(scratch, 'setup.exe');
    const payload = Buffer.from('Contenido de prueba para el ZIP');
    const pe = 128;
    const optional = pe + 24;
    const certificateDirectory = optional + 144;
    const header = Buffer.alloc(512);
    header.writeUInt16LE(0x5a4d);
    header.writeUInt32LE(pe, 0x3c);
    header.writeUInt32LE(0x00004550, pe);
    header.writeUInt16LE(0x8664, pe + 4);
    header.writeUInt16LE(240, pe + 20);
    header.writeUInt16LE(0x20b, optional);
    header.writeUInt32LE(16, optional + 108);
    await fs.writeFile(stub, header);
    await fs.writeFile(zip, payload);
    await appendPayload(stub, zip, setup);
    const reference = { size: payload.length, sha256: createHash('sha256').update(payload).digest('hex') };
    await verifySetup(setup, reference);

    // Réplica del formato, sin certificado real. La confianza la comprueba SignTool.
    const unsigned = await fs.readFile(setup);
    assert.equal(unsigned.length % 8, 0);
    unsigned.writeUInt32LE(unsigned.length, certificateDirectory);
    unsigned.writeUInt32LE(16, certificateDirectory + 4);
    const table = Buffer.alloc(16);
    table.writeUInt32LE(table.length);
    table.writeUInt16LE(0x0200, 4);
    table.writeUInt16LE(0x0002, 6);
    await fs.writeFile(setup, Buffer.concat([unsigned, table]));
    await verifySetup(setup, reference);

    const tampered = Buffer.from(unsigned);
    tampered[tampered.length - 49] ^= 1;
    await fs.writeFile(setup, Buffer.concat([tampered, table]));
    await assert.rejects(verifySetup(setup, reference), /no contiene la descarga portátil/);

    unsigned.writeUInt32LE(unsigned.length - 1, certificateDirectory);
    await fs.writeFile(setup, Buffer.concat([unsigned, table]));
    await assert.rejects(verifySetup(setup, reference), /Tabla de certificados PE inválida/);
  } finally {
    await fs.rm(scratch, { recursive: true, force: true });
  }
});
