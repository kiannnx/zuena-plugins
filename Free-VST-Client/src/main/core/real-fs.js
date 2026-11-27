'use strict';
// Dentro de Electron, `fs` trata cualquier archivo `.asar` como una carpeta virtual: copiar,
// extraer o renombrar el `resources/app.asar` del launcher falla con «Invalid package».
// `original-fs` es el fs de Node sin esa intercepción. Fuera de Electron (tests) es el fs normal.
// Usarlo en todo lo que mueve archivos del propio launcher (instalar, actualizar, extraer).

const fs = process.versions.electron ? require('original-fs') : require('node:fs');

module.exports = { fs, fsp: fs.promises };
