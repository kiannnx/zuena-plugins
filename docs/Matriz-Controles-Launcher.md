---
titulo: Matriz de Controles — ZUENA Plugins
tags: [frontend, launcher, auditoria, eb-01, eb-02]
estado: verificado
version: 0.1.0
actualizado: 2026-11-26
---

# Matriz de Controles — ZUENA Plugins

> [!abstract] Función
> Cruce 1:1 de cada control del launcher con su acción real y el test E2E que la ejecuta. Auditoría contra [[Lista-Errores-Basicos#EB-01]] y [[Lista-Errores-Basicos#EB-02]]. Arquitectura: [[Arquitectura-SDK-y-Portal]]. Decisión: [[02-Launcher-ZUENA-Plugins]].

## Barra superior

| Control | Acción | Verificado por |
|---|---|---|
| `Buscar` / `Ctrl+F` | Abre buscador; filtra el muro del catálogo en vivo | E2E `Búsqueda filtra y limpia` |
| `✕` del buscador | Limpia la búsqueda | E2E idem |
| `Notificaciones` + badge | Panel con historial; badge = no leídas | E2E `Ajustes, apariencia oscura, ayuda y notificaciones` |
| `Marcar leídas` / `Borrar todo` | Persisten en `settings.json` | E2E idem |
| `Oscuro` / `Claro` (`#btn-theme`) | Alterna entre Modo Claro y Modo Oscuro oficial ZUENA (invierte logo y tokens `--z-*`) | E2E `Ajustes, apariencia oscura, ayuda y notificaciones` |
| `Ayuda` | Modal con rutas VST3, re-escaneo por DAW y sección «Si Windows bloquea el plugin» (SmartScreen / Smart App Control) | E2E idem (comprueba la sección) |
| Icono red | Estado real `navigator.onLine` | Render + eventos `online/offline` |
| Reloj | Hora local, alineado al segundo | Render |
| `—` `▢` `✕` / doble clic barra | Minimizar / maximizar-restaurar / cerrar | E2E `Controles de ventana`, `Cerrar cierra la app` |

## Dock (← → del teclado recorren las secciones)

| Tile | Sección | Badge |
|---|---|---|
| Destacado | Plugin destacado del catálogo + dispositivo 3D | — |
| Catálogo | Muro curvo de tarjetas | — |
| Instalados | Lista con acciones | nº de actualizaciones |
| Descargas | Cola con progreso | nº de descargas en curso |
| Ajustes | Carpeta, catálogo, info | — |

## Secciones

| Sección | Control | Acción | Verificado por |
|---|---|---|---|
| Destacado | `Instalar gratis` / `Actualizar a vX` / `Reinstalar` / `Mostrar en carpeta` / `Avisarme cuando salga` ↔ `Te avisaremos ✓` | Según estado del plugin | E2E `Destacado: Avisarme ↔ Te avisaremos` |
| Destacado | `Ver detalles` y clic en el dispositivo 3D | Abre la ficha | E2E `Destacado: Ver detalles…` |
| Catálogo | Filtros `Todos` `Disponibles` `Instalados` `Próximamente` | Filtran el muro | E2E `Catálogo: muro, filtros…` |
| Catálogo | `‹` `›`, rueda, arrastre, `,` `.` | Giran el muro | E2E (botones) |
| Catálogo | Tarjeta lateral / central | Centra / abre ficha | E2E instalación |
| Catálogo | `Ver todo el catálogo` (sin resultados) · `Limpiar búsqueda` | Reset de filtro/búsqueda | E2E |
| Ficha | Acción principal · `Reinstalar` · `Desinstalar` · `Cancelar descarga` · `✕`/Esc/clic fuera | Ver nombre | E2E instalación, cancelación |
| Ficha · panel derecho | Vídeo de fondo en bucle (claro u oscuro según el tema) · botón ❚❚/▶ | Arranca solo y silenciado (no con movimiento reducido); pausa/reanuda; cambia de variante al cambiar el tema; se libera al cerrar | E2E normal pasos 1–3, 7 y 9 |
| Ficha · Galería | Miniaturas → galería ampliada · ‹ › · ← → · Esc · clic en el fondo | Abre la captura a tamaño real; Esc y el clic en el fondo cierran solo la imagen, no la ficha | E2E normal paso 6 |
| Ficha · barra inferior | `Instalar gratis` / `Actualizar` / `Reinstalar` / `Mostrar en carpeta` / `Desinstalar` / `Cancelar descarga` + estado | Mismas acciones que antes; estado con tamaño y verificación SHA-256 | E2E demo `Instalar paquete demo desde la ficha` |
| Instalados / Destacado | `Actualizar a vX.Y.Z` | Aparece cuando el catálogo trae una versión mayor que la del bundle en disco; avisa una vez en notificaciones | E2E `e2e/update.js` |
| Instalados | `Actualizar` · ⓘ · 📁 · 🗑 (con confirmación) | Reinstala · ficha · Explorador · desinstala | E2E `Instalados…` |
| Instalados | `Abrir carpeta VST3` · `Buscar actualizaciones` · `Explorar catálogo` | Explorador · recarga catálogo · navega | E2E `Instalados…` |
| Descargas | `Cancelar` · `Reintentar` · `Mostrar` | Por fila | E2E `Paquete alterado…`, `Descargas…` |
| Descargas | `Cancelar todas` · `Limpiar finalizadas` · `Ir al catálogo` | Hero | E2E `Descargas: Cancelar todas` |
| Ajustes | Apariencia `Claro` · `Oscuro` · `Automático` | Guarda `theme` en `settings.json` y actualiza esquema | E2E `Ajustes, apariencia oscura…` |
| Ajustes | `Cambiar…` · `Abrir carpeta` · `Restablecer predeterminada` | Diálogo nativo · Explorador · reset | E2E `Ajustes…` |
| Ajustes | URL catálogo + `Guardar y recargar` / Enter · `Usar catálogo integrado` | Valida https, recarga, fallback | E2E `normal.js` |
| Ajustes | `Guía de instalación` · `Recargar catálogo` | Modal · recarga | E2E `Ajustes…` |

## Funciones backend → control

| Función (`src/main`) | Control | ¿Visible? |
|---|---|---|
| `loadCatalog` remoto/caché/integrado + `package-server` (`bundled://`) | Ajustes (fuente actual, avisos, entradas descartadas) y descarga local de `ZUENA EQ` | ✅ |
| `download` progreso/velocidad/ETA/cancelación | Descargas + ficha | ✅ |
| Verificación SHA-256 | Texto "Verificado (SHA-256 ✓)" y error explícito | ✅ |
| `installPackage` + elevación UAC | Mensaje "Instalado con permisos de administrador" | ✅ |
| `uninstallFiles` | 🗑 + confirmación | ✅ |
| Detección de bundle instalado externamente | Chip "Instalado" + `Reinstalar` | ✅ |
| Watch-list "Avisarme" | Notificación al pasar a disponible | ✅ |
| Notificaciones persistentes | Panel + badge | ✅ |
| Guía de inicio | Empezar · idioma · tema · conexión · avisos (Probar aviso) · carpeta · instalar en el equipo · Atrás | Guarda permisos y preferencias; tema en vivo; instala y relanza desde %LOCALAPPDATA% | E2E `e2e/onboarding.js` |
| Ajustes · Conexión | Interruptores conexión y avisos · Buscar ahora · Probar aviso · Ver la guía de inicio | Cambia la fuente del catálogo en vivo; revisa catálogo y launcher | E2E guía (estado) · manual |
| Guía de inicio · Conexión · Ajustes · Conexión (0.2.3) | Interruptor «Ayudar con estadísticas anónimas» / «Estadísticas anónimas» (apagado por defecto) + aviso único a quien ya hizo la guía | Activar: ping anónimo a Supabase (`zuena_plugins_ping`) al arrancar, cada 12 h o al cambiar plugins. Apagar: borra lo enviado (`zuena_plugins_forget`) y cambia el identificador. Ver [[Web-Descarga-y-Panel]] | `test/core.test.js` (3 tests `stats:`) + E2E `e2e/stats.js` |
| Ajustes · Launcher | Descargar actualización · Reiniciar y actualizar · Instalar en este equipo | Descarga verificada, reemplazo al cerrar, instalación desde copia portátil | Tests unitarios de `launcher-update` + prueba real del reemplazo |
| Catálogo · Carrusel | Arrastre · rueda · ‹ › · puntos · ← → Inicio Fin · clic | Mueve/abre; el clic tras arrastrar ya no se pierde | E2E demo `Catálogo: muro…` + prueba de carrusel |
| Instalados · Actualizar todo (0.2.4) | Botón «Actualizar todo (N)» en el encabezado cuando hay versiones nuevas | Actualiza cada plugin de a uno (un solo aviso de administrador a la vez) y resume el resultado | E2E `update.js` |
| Instalados · Novedades (0.2.4) | Desplegable «Novedades de vX» en la fila con actualización | Muestra el changelog de esa versión desde el catálogo | E2E `update.js` |
| Apertura (0.2.4) | Animación de marca al abrir (sin controles) | Punto y Z, nombre y luz iridiscente; solo transform/opacity, no bloquea clics, se quita sola; con movimiento reducido, fundido corto | E2E `update.js` (se quita del DOM) |
| Windows · Aplicaciones instaladas (0.2.4) | Entrada «ZUENA Plugins» con botón Desinstalar | `--uninstall`: confirma, quita accesos y entrada y borra la carpeta al cerrar; no borra los plugins VST3. La escriben el instalador y el launcher al arrancar | E2E `install-self.js` · `test:setup` · `core.test.js` |
| Modo demo | Badge `MODO DEMO` + nota en Ajustes | ✅ |

> [!check] Resultado de auditoría
> 0 controles sin acción (EB-01) · 0 funciones sin control (EB-02) · E2E: 17 pasos modo demo + 9 modo normal + 6 actualización · 0 errores de consola (2026-11-26).
> Launcher 0.2.3 (2026-11-26): `npm test` 29/29 · `npm run e2e` (demo, normal, actualización, guía y estadísticas) en verde.
