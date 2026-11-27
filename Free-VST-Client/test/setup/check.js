'use strict';

const { execFileSync } = require('node:child_process');
const fs = require('node:fs/promises');
const path = require('node:path');
const { setTimeout: pause } = require('node:timers/promises');
const { appendPayload, compile, setupName } = require('../../build/setup');
const { version } = require('../../package.json');

async function main() {
  const dist = path.resolve(__dirname, '../../dist');
  const scratch = await fs.mkdtemp(path.join(dist, '.zuena-setup-test-'));
  try {
    const harness = path.join(scratch, 'InstallChecks.exe');
    compile(harness, ['Install.cs', 'Wizard.cs'].map((file) => path.join(__dirname, '../../build/setup', file))
      .concat(path.join(__dirname, 'InstallChecks.cs')), true);
    async function run(...args) {
      const limit = Date.now() + 30_000;
      for (;;) {
        try {
          execFileSync(harness, args, { stdio: 'inherit', timeout: 90_000 });
          return;
        } catch (error) {
          if (error.code !== 'UNKNOWN' || Date.now() >= limit) throw error;
          await pause(3000);
        }
      }
    }
    const zip = path.join(scratch, 'invalid.zip');
    const invalid = path.join(scratch, 'invalid.exe');
    await run('fixture', zip);
    await appendPayload(harness, zip, invalid);
    const setup = path.join(dist, setupName(version));
    const damaged = path.join(scratch, 'damaged.exe');
    await fs.copyFile(setup, damaged);
    const file = await fs.open(damaged, 'r+');
    try {
      const lastByte = (await file.stat()).size - 1;
      const value = Buffer.alloc(1);
      await file.read(value, 0, 1, lastByte);
      value[0] ^= 1;
      await file.write(value, 0, 1, lastByte);
    } finally {
      await file.close();
    }
    await run('install', setup, scratch, invalid, damaged);
  } finally {
    await fs.rm(scratch, { recursive: true, force: true });
  }
}

main().catch((error) => {
  console.error(error.message);
  process.exitCode = 1;
});
