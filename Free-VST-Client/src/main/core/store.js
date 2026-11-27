'use strict';
// Almacén JSON con escritura atómica (tmp + rename) y escrituras serializadas.

const fsp = require('node:fs/promises');
const path = require('node:path');

class JsonStore {
  constructor(file, defaults) {
    this.file = file;
    this.defaults = defaults;
    this.data = structuredClone(defaults);
    this.chain = Promise.resolve();
  }

  async load() {
    try {
      const raw = JSON.parse(await fsp.readFile(this.file, 'utf8'));
      this.data = { ...structuredClone(this.defaults), ...(raw && typeof raw === 'object' ? raw : {}) };
    } catch {
      this.data = structuredClone(this.defaults);
    }
    return this.data;
  }

  get(key) {
    return this.data[key];
  }

  set(key, value) {
    this.data[key] = value;
    return this.save();
  }

  save() {
    const snapshot = JSON.stringify(this.data, null, 2);
    this.chain = this.chain.then(async () => {
      await fsp.mkdir(path.dirname(this.file), { recursive: true });
      const tmp = `${this.file}.${process.pid}.tmp`;
      await fsp.writeFile(tmp, snapshot, 'utf8');
      await fsp.rename(tmp, this.file);
    }).catch(() => {});
    return this.chain;
  }
}

module.exports = { JsonStore };
