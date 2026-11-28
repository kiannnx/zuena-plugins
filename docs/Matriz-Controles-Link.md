---
titulo: Matriz de controles — ZUENA Link
tags: [ui, link, obs, streaming, matriz, eb-01, eb-02]
estado: activo
version: 0.1.0
actualizado: 2026-11-28
---

# Matriz de controles — ZUENA Link

> [!abstract] Contrato UI ↔ parámetros y ajustes
> Cada parámetro de `plugins/ZuenaLink/Source/State/Parameters.h` y cada ajuste guardado en el estado del plugin tiene un control visible, y cada control cambia algo real y verificable (anti `EB-01` / `EB-02`). Relacionado: [[05-ZUENA-Link]], [[Lista-Errores-Basicos]].

## Parámetros automatizables

| Parámetro (`id`) | Rango | Control | Acción | Verificado por |
|---|---|---|---|---|
| `obs` | Sí / no (sí por defecto) | Interruptor de la fila «OBS directo» | Toma el canal compartido y manda la pista a la fuente «ZUENA Link» de OBS; apagado lo suelta | `link_editor` (interruptor ↔ parámetro, «Apagado») · `link_vst3` (apagado: el receptor deja de recibir) |
| `channel` | Link 1 … Link 8 | Menú de canal (marca «· en uso» los que tiene otra pista) | Elige el canal compartido; la fuente de OBS elige el mismo | `link_editor` (menú → Link 8) · `link_platform` (dos pistas en un canal, cambio de canal) |
| `device` | Sí / no | Interruptor de la fila «Dispositivo» (elegir una salida lo enciende) | Abre la salida WASAPI elegida y le manda la pista | `link_editor` · `link_platform` (WASAPI abre y sostiene la salida, 0 cortes) |
| `send` | −60 a +12 dB (0 dB por defecto) | Deslizador «Envío» con valor en dB y medidor L/R | Nivel de lo que se envía, con rampa sin escalones; la pista no cambia | `link_editor` (deslizador → parámetro) · `link_vst3` (−6 dB llegan a OBS como −6 dB) |
| `mono` | Sí / no | Botón «Mono» | Suma L y R en el envío; la pista sigue en estéreo | `link_editor` · `link_vst3` (L = −R se anula en el envío mono) |

## Ajustes guardados en el proyecto (no automatizables)

| Ajuste | Control | Acción | Verificado por |
|---|---|---|---|
| Salida (`deviceId`, `deviceName`) | Menú de salida: «Recomendadas para transmitir» primero (cables virtuales, mezcladores y placas con loopback), después «Otras salidas»; una salida guardada que no está conectada aparece como «(no conectada)» | Elige el endpoint de Windows; se reconecta sola si vuelve | `link_editor` (menú → dispositivo guardado) · `link_core` (rutas reconocidas) |
| Modo (`exclusive`) | Menú «Compartido / Exclusivo» | Compartido de baja latencia (IAudioClient3) o exclusivo con el período mínimo del hardware | `link_editor` (menú ↔ ajuste) |
| Par de canales (`pair`) | Menú «Canales 1-2, 3-4…» (activo si la salida tiene más de dos canales) | Manda L/R a ese par de la placa | `link_editor` · revisión de `WasapiOutput::writeFrames` |
| Formato (`recordFormat`) | Menú «WAV 16 bits / 24 bits / 32 bits flotante» | Formato de la grabación (16 bits con dither TPDF) | `link_editor` (menú → 32 bits flotante) · `link_core` (lectura bit a bit) |
| Carpeta (`recordFolder`) | Botón «Carpeta» (selector de carpetas de Windows) | Dónde se graban los WAV (por defecto `Música\ZUENA Link`) | `link_editor` (acción conectada) · `link_platform` (carpeta imposible → error claro) |
| Tema (`dark`) | Botón «Tema claro / Tema oscuro» | Cambia el tema del panel | `link_editor` (capturas clara y oscura) |

## Acciones

| Control | Acción | Verificado por |
|---|---|---|
| Botón de grabar (círculo rojo; cuadrado mientras graba) | Empieza y termina la grabación del envío en WAV desde el hilo de servicio | `link_editor` (graba, muestra tiempo y tamaño, deja un WAV) · `link_platform` (lo grabado es lo enviado) |
| Botón «Abrir» | Muestra la última grabación en el Explorador (o abre la carpeta) | `link_editor` (acción conectada) |

## Estados en vivo

| Fila | Estados | Origen |
|---|---|---|
| OBS directo | Apagado · Esperando a OBS (con la instrucción para agregar la fuente) · Conectado a OBS · X ms · N cortes · Link N lo usa otra pista (programa) · Exportando · Otra pista también pide Link N | `LinkEngine::tickObs` + lo que publica el receptor en el canal |
| Dispositivo | Apagado · Elegí una salida · «X» no está conectada · Abriendo · kHz · ms · modo · cortes · error de Windows y «Reintentando…» · consejo de qué elegir en OBS o Streamlabs | `WasapiOutput::Status` + `DeviceRoutes` |
| Grabar WAV | Listo para grabar · Grabando · tiempo · tamaño · Último archivo · errores (disco lleno, carpeta) · picos recortados | `Recorder::Status` |

> [!check] Cobertura
> 5 / 5 parámetros y 6 / 6 ajustes con control visible y acción comprobada. Textos con tildes verificados por `tools/check-ui-text.mjs` (`EB-14`).
