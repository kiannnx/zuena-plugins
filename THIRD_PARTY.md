# Componentes de terceros

ZUENA Plugins usa estos componentes. Cada uno se rige por su propia licencia.

| Componente | Uso | Licencia |
|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) 8.0.17 | Base de los plugins (audio, VST3, interfaz) | AGPLv3 (opción de código abierto de JUCE) |
| [Catch2](https://github.com/catchorg/Catch2) 3.16.1 | Pruebas en C++ | Boost Software License 1.0 |
| [Electron](https://www.electronjs.org/) | Launcher | MIT |
| [yauzl](https://github.com/thejoshwolfe/yauzl) | Extracción de paquetes en el launcher | MIT |
| [Playwright](https://playwright.dev/) | Pruebas de punta a punta (solo desarrollo) | Apache 2.0 |
| [@electron/packager](https://github.com/electron/packager) | Empaquetado del launcher (solo desarrollo) | BSD 2-Clause |
| [GSAP](https://gsap.com/) y ScrollTrigger | Animaciones de la página de descarga | Licencia estándar de GSAP (gratuita); ver `web/descarga/assets/vendor/LICENCIA-GSAP.txt` |
| [Remotion](https://www.remotion.dev/) | Vídeos de presentación (solo desarrollo) | Licencia de Remotion; ver su sitio |
| Inter, Inter Tight, Instrument Serif, JetBrains Mono | Tipografías | SIL Open Font License 1.1 |

La lista completa de dependencias de Node y sus versiones está en `Free-VST-Client/package-lock.json` y `videos/zuena-eq/package-lock.json`.
