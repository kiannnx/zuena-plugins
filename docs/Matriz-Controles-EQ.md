---
titulo: Matriz de Controles — ZUENA EQ
tags: [frontend, ui, eq, auditoria, apvts, ergonomia]
estado: verificado
version: 0.3.0
actualizado: 2026-11-26
---

# Matriz de Controles — ZUENA EQ

> [!abstract] Función
> Cruce 1:1 entre los **365 parámetros APVTS** de ZUENA EQ v0.3.0, cada control visual de la barra superior, barra inferior y ficha flotante, y todos los gestos de ratón y atajos de teclado. Auditoría automática: `verify-ui-contract.mjs` (CTest `verify_ui_contract`, 78/78 OK) y `zeq_snapshot audit` (`ui_audit`). Diseño en [[03-ZUENA-EQ]] y [[02-Frontend-Ergonomia-y-Modo-Oscuro]].

## Parámetros por banda (24 bandas × 15 = 360)

ID: `b<N>_<sufijo>`, N = 1…24.

| Sufijo | Parámetro | Control principal | Atajos / gestos rápidos | Visible cuando | Verificado |
|---|---|---|---|---|---|
| `used` | Banda activa | Doble clic o `Ctrl+clic` en vacío · `+ Banda` (barra inf.) · `Eliminar` (ficha) | Tecla `N` o `+` · clic derecho en vacío › Crear · `Supr` | Siempre | ✅ |
| `bypass` | Bypass | Botón `Bypass` (ficha) | `Alt+clic` o doble clic en nodo o ficha inferior · tecla `B` | Siempre | ✅ |
| `type` | Tipo de filtro (10 tipos) | Desplegable `Tipo` · clic en icono de curva (ficha) | Tecla `T` / `Shift+T` · menú contextual | Siempre | ✅ |
| `freq` | Frecuencia | Arrastre horizontal del nodo · perilla `Frecuencia` | Flechas `←`/`→` (`Shift` = 10 cents) · `Alt+arrastre` (bloquea eje) · escribir nota (`A4`, `F#2`) | Siempre | ✅ |
| `gain` | Ganancia | Arrastre vertical del nodo · perilla `Ganancia` · botón `± Inv` | Flechas `↑`/`↓` (`Shift` = 0.1 dB) · tecla `I` (invierte) · `Ctrl+clic` (0 dB) | Bell, Low/High Shelf, Tilt, Flat Tilt | ✅ |
| `q` | Q | Rueda sobre el nodo · perilla `Q` | `Ctrl+Alt+arrastre` vertical (sin rueda) · clic derecho en perilla (valores rápidos) | Todo salvo Flat Tilt y 6 dB/oct | ✅ |
| `slope` | Pendiente | Desplegable `Pendiente` (ficha) | `Alt + rueda` sobre nodo de corte · menú contextual | Low Cut / High Cut | ✅ |
| `place` | Estéreo / L / R / Mid / Side | Desplegable `Estéreo` (ficha) | Menú contextual › Estéreo | Siempre | ✅ |
| `solo` | Solo (exclusivo) | Botón `Solo` (ficha) | Tecla `S` · `Alt+Shift+arrastre` o modo `Audición` (solo temporal al mover) | Siempre | ✅ |
| `dyn` | Dinámica | Botón `Dinámica` (ficha) | Tecla `D` · `Alt + rueda` activa y ajusta rango | Tipos con ganancia | ✅ |
| `thr` | Umbral | Perilla `Umbral` | Rueda / clic derecho / entrada numérica | Dinámica activa | ✅ |
| `range` | Rango | Perilla `Rango` · tirador secundario del nodo | `Alt + rueda` sobre el nodo | Dinámica activa | ✅ |
| `ratio` | Ratio | Perilla `Ratio` | Rueda / clic derecho / entrada numérica | Dinámica activa | ✅ |
| `atk` | Ataque | Perilla `Ataque` | Rueda / clic derecho / entrada numérica | Dinámica activa | ✅ |
| `rel` | Relajación | Perilla `Relajación` | Rueda / clic derecho / entrada numérica | Dinámica activa | ✅ |

## Parámetros globales APVTS (5)

| ID | Tipo | Control | Gestos extra | Verificado |
|---|---|---|---|---|
| `out_gain` | Float (`±30 dB`) | Perilla `Salida` + medidor de pico (`dBFS`) | Rueda, `Ctrl+clic` reset, menú rápido | ✅ |
| `gain_scale` | Float (`0–200 %`) | Perilla `Escala` (escala proporcionalmente todas las ganancias y rangos) | Menú rápido `50 / 75 / 100 / 125 / 150 %` | ✅ |
| `proc_mode` | Choice (`Latencia cero` / `Fase natural`) | Selector `Modo de fase` | Conmutación sin clics (5 ms) | ✅ |
| `auto_gain` | Bool | Botón `Auto` | Compensación perceptual `1/f` | ✅ |
| `global_bypass` | Bool | Botón `Bypass` (barra superior) | Fundido de 5 ms alineado en latencia | ✅ |

## Herramientas ergonómicas y navegación

| Función | Control / Gesto | Verificado |
|---|---|---|
| Vista previa fantasma de curva | Mover el cursor por el gráfico en vacío sugiere tipo (`Low Cut`, `Shelf`, `Bell`, `High Cut`) antes de crear | ✅ |
| Selección múltiple y edición en grupo | Arrastrar rectángulo de lazo en vacío · `Shift+clic` · `Ctrl+A` (mueve frec/ganancia/Q, bypass o borra en grupo) | ✅ |
| Navegación rápida en ficha flotante | Botones `‹` / `›` · arrastrar cabecera para fijar posición · botón `×` para cerrar · tecla `Tab` / `1..9` | ✅ |
| Ficha de banda (diseño 0.3.5) | Encabezado: icono del filtro (clic = siguiente tipo), «Banda N», nota, `‹` `›` y `×`. Selectores `Tipo` / `Pendiente` / `Estéreo` a lo ancho. Perillas grandes `Frecuencia` / `Ganancia` / `Q` con el arco en el color de la banda. Columna de acciones `Bypass`, `Solo`, `Dinámica`, `± Inv` y `Eliminar` aparte. Sección `DINÁMICA` con medidor en vivo de la ganancia aplicada (±18 dB) y cinco perillas. Mismos parámetros que antes: no se agregó ni quitó ninguno | ✅ `zeq_snapshot audit` 100 % y 70 % en Windows Sandbox |
| Audición automática al mover | Botón `Audición` (barra inferior) o `Alt+Shift+arrastre` (activa Solo mientras se sujeta el nodo o `Frecuencia`) | ✅ |
| Congelar analizador de espectro | Botón `Congelar` (barra inferior) · tecla `F` · clic derecho en vacío | ✅ |
| Respuesta de fase (`±180°`) | Botón `Fase` (barra inferior) · tecla `P` | ✅ |
| Modo Claro / Modo Oscuro oficial ZUENA | Botón `Oscuro` (barra inferior) · tecla `M` · clic derecho en vacío (`ui_dark` + preferencia global) | ✅ |
| Guía interactiva de atajos | Botón `?` (barra inferior) · teclas `?` o `H` | ✅ |
| Tamaño de la interfaz (70 / 80 / 90 / 100 / 115 / 130 %) | Botón `Tamaño` con el porcentaje (barra inferior) › menú. Escala todo el contenido como un bloque (componente raíz), sin tocar la escala del host. Primera apertura: el más grande que entra en la pantalla; después, preferencia global `scale=` en `ZuenaEQ.settings` (v0.3.3) | ✅ `zeq_snapshot audit` (`auditScale`) |
| Presets de fábrica y A / B | Selector `Presets` · botones `A`, `B`, `A→B` / `B→A` | ✅ |
