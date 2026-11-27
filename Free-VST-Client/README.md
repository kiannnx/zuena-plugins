# ZUENA Plugins

Launcher de escritorio que descarga e instala nuestros plugins VST3 gratuitos. Verifica cada paquete con SHA-256 antes de tocar la carpeta de plugins.

## Ejecutar

| Objetivo | Cómo |
|---|---|
| Abrir la app empaquetada | `dist\ZUENA Plugins-win32-x64\ZUENA Plugins.exe` |
| Probar el flujo completo con paquetes de prueba | `dist\ZUENA Plugins-win32-x64\Probar en MODO DEMO.cmd` |
| Ejecutar desde el código | `npm start` |
| Ejecutar desde el código en modo demo | `npm run demo` |

El modo demo levanta un servidor local en `127.0.0.1` con tres paquetes de prueba y uno con checksum incorrecto. Instala en una carpeta aislada (`%APPDATA%\ZUENA Plugins\demo-VST3`), nunca en tu carpeta VST3 real.

## Desarrollo

```powershell
npm install
npx install-electron --no   # solo si el binario de Electron no se descargó
npm test                    # 16 tests unitarios: catálogo, descarga, SHA-256, instalación, zip-slip, elevación
npm run pack:eq             # empaqueta el VST3 compilado de ZUENA EQ + demo A/B y actualiza catalog/catalog.json
npm run e2e                 # maneja la app real: cada botón, modo demo + normal (instala ZUENA EQ), capturas en e2e/shots
npm run package             # genera dist/ZUENA Plugins-win32-x64 (ejecuta pack:eq antes)
```

Para probar la build empaquetada en vez del código fuente:

```powershell
node e2e/run.js "dist\ZUENA Plugins-win32-x64\ZUENA Plugins.exe"
```

## Cómo funciona la instalación

1. Carga `catalog.json`: URL remota (Ajustes) → última copia en caché → catálogo integrado en la app.
2. Descarga el `.zip` por HTTPS con progreso y cancelación; comprueba tamaño y SHA-256.
3. Extrae en una carpeta temporal y rechaza rutas que escapan de ella (zip-slip) y enlaces simbólicos.
4. Copia cada bundle `*.vst3` a la carpeta de instalación. Si Windows deniega la escritura (la carpeta por defecto `C:\Program Files\Common Files\VST3`), pide permiso de administrador con UAC solo para esa copia.
5. Registra los archivos instalados para poder actualizarlos, mostrarlos y desinstalarlos.

## Publicar un plugin

Añade una entrada al catálogo y aloja el archivo. Las descargas deben usar `https://`.

```json
{
  "id": "mi-plugin",
  "name": "Mi Plugin",
  "version": "1.0.0",
  "status": "available",
  "category": "Efecto",
  "icon": "wave",
  "accent": ["#1f6bff", "#8a5cff"],
  "bundle": "Mi Plugin.vst3",
  "tagline": "Una línea",
  "description": "Texto largo",
  "tags": ["Reverb"],
  "downloads": {
    "win32": { "url": "https://…/mi-plugin-1.0.0-win.zip", "sha256": "<64 hex>", "size": 1234567 }
  },
  "demos": { "before": "https://…/seco.mp3", "after": "https://…/procesado.mp3" }
}
```

- `status`: `available` o `coming-soon` (muestra el botón "Avisarme cuando salga").
- `icon`: `wave`, `mic`, `knob`, `spark`, `echo` o `filter`.
- El `.zip` debe contener el bundle `.vst3` en la raíz o dentro de una única carpeta.
- Checksum: `Get-FileHash mi-plugin.zip -Algorithm SHA256`.
- Las entradas inválidas se descartan y se listan en Ajustes; el resto del catálogo carga igual.

## Estructura

```
src/main/core/   catálogo, descarga, unzip, instalador, elevación, store, servidor demo (sin Electron, con tests)
src/main/main.js ventana, IPC, trabajos de instalación, notificaciones
src/preload/     la única API que la UI puede llamar (window.hub)
src/renderer/    UI: index.html, css/styles.css, js/app.js
catalog/         catálogo integrado en la app
test/  e2e/      tests unitarios y end-to-end
build/           generador de icono y script de empaquetado
```
