# ZUENA Plugins

Plugins de audio VST3 gratuitos y su launcher para Windows. Código abierto bajo [GNU AGPL v3](LICENSE).

| Producto | Qué es |
|---|---|
| **ZUENA EQ** | Ecualizador dinámico de 24 bandas con fase natural 2×, analizador de espectro y ficha por banda |
| **ZUENA Luma** | Compresor óptico con modelos Clásico y Precisión, ganancia automática y curva en pantalla |
| **ZUENA Plugins** (launcher) | Instala y actualiza los plugins, con catálogo en línea y avisos de versiones nuevas |

## Descargar

La versión oficial se descarga desde la [página de ZUENA Plugins](https://kiannnx.github.io/zuena-plugins-releases/) o desde las [releases](https://github.com/kiannnx/zuena-plugins-releases/releases). El instalador guía la instalación del launcher, y desde ahí se instalan y actualizan los plugins.

## Compilar

Requisitos: Windows 10/11 x64, Visual Studio 2022 (C++ de escritorio, incluye CMake y Ninja), Node.js 20 o superior.

```powershell
git clone --recursive https://github.com/kiannnx/zuena-plugins.git
cd zuena-plugins
tools\build.cmd
```

`tools\build.cmd` configura y compila en `build\release` (C++20, MSVC, `/W4 /WX`). Salidas principales:

| Objetivo | Resultado |
|---|---|
| `ZuenaEQ_VST3`, `ZuenaLuma_VST3` | Plugins VST3 en `build\release\plugins\<Plugin>\<Plugin>_artefacts\Release\VST3` |
| `run_tests`, `luma_tests` | Pruebas del motor (Catch2) |
| `zeq_snapshot` | Auditoría de la interfaz del EQ (`zeq_snapshot audit`), capturas y medición (`bench`) |
| `luma_qa` | Auditoría del editor y del VST3 de Luma (`luma_qa editor`, `luma_qa host <vst3>`) |

Launcher:

```powershell
cd Free-VST-Client
npm ci
npm start          # ejecutar
npm test           # pruebas unitarias
npm run e2e        # pruebas de punta a punta con la app real
npm run package    # empaqueta los plugins compilados y el launcher
```

Con Smart App Control activado, Windows puede bloquear ejecutables recién compilados. `tools\sandbox-build.ps1` y `tools\sandbox-tests.ps1` compilan y prueban dentro de Windows Sandbox.

## Estructura

| Carpeta | Contenido |
|---|---|
| `plugins/ZuenaEQ` | EQ: motor DSP (`Source/Core`), estado (`Source/State`), interfaz (`Source/UI`), pruebas y herramientas |
| `plugins/ZuenaLuma` | Compresor Luma con la misma separación |
| `Free-VST-Client` | Launcher Electron: proceso principal, interfaz, catálogo y pruebas |
| `tools` | Compilación, pruebas en Sandbox, galerías y publicación |
| `videos` | Vídeos de presentación (Remotion) |
| `web` | Página de descarga |
| `docs` | Matrices de controles: cada parámetro, su control y la prueba que lo verifica |
| `third_party` | JUCE y Catch2 como submódulos, en sus versiones oficiales |

Reglas de código: C++20, separación estricta entre DSP, estado e interfaz, y nada de reservar memoria, bloquear ni escribir a disco en el hilo de audio. Cada control visible tiene que hacer algo verificable y cada parámetro tiene que tener un control.

## Licencia y marca

El código se publica bajo la [GNU Affero General Public License v3](LICENSE). Podés estudiarlo, modificarlo y redistribuirlo. Si distribuís una versión modificada, publicá su código bajo la misma licencia.

El nombre **ZUENA**, el logo y la identidad visual no forman parte de la licencia. Las versiones modificadas tienen que usar otro nombre y otro logo.

Componentes de terceros y sus licencias: [THIRD_PARTY.md](THIRD_PARTY.md).

---

**English.** Free VST3 audio plugins (ZUENA EQ, ZUENA Luma) and their Windows launcher, open source under the GNU AGPL v3. Build with Visual Studio 2022 using `tools\build.cmd` after cloning with `--recursive`. The ZUENA name and logo are not covered by the license; modified versions must use a different name and logo.
