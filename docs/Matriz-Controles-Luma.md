---
titulo: Matriz de controles — ZUENA Luma
tags: [ui, luma, compresor, matriz, eb-01, eb-02]
estado: activo
version: 0.3.0
actualizado: 2026-11-26
---

# Matriz de controles — ZUENA Luma

> [!abstract] Contrato UI ↔ parámetros
> Cada parámetro de `Source/State/Parameters.h` tiene un control visible y cada control mueve un parámetro real (anti `EB-01` / `EB-02`). Relacionado: [[04-ZUENA-Luma]], [[Lista-Errores-Basicos]].

| Parámetro (`id`) | Rango | Control | Acción | Verificado por |
|---|---|---|---|---|
| `reduction` | 0–100 % | Perilla grande «Reducción» | Baja el umbral (−2 a −42 dB) | `luma_dsp` curva estática |
| `makeup` | −18 a +24 dB | Perilla grande «Ganancia» | Ganancia de la rama comprimida | `luma_dsp` mezcla y ganancia |
| `highpass` | 0–300 Hz | Perilla «Paso alto» | Paso alto del detector (0 = apagado) | `luma_dsp` detector |
| `emphasis` | 0–9 dB | Perilla «Énfasis» | Sensibilidad del detector por encima de 3,5 kHz | `luma_dsp` detector |
| `recovery` | 50–200 % | Perilla «Recuperación» | Escala las dos constantes de recuperación | `luma_dsp` dos tiempos |
| `mix` | 0–100 % | Perilla «Mezcla» | Mezcla paralela sin latencia | `luma_dsp` mezcla |
| `limit` | Compress / Limit | Botón «Compress / Limit» | 3:1 o 12:1 | `luma_dsp` curva estática |
| `external` | Interno / externo | Botón «Key interno / externo» | Detector desde el bus sidechain (sin bus: entrada) | `luma_dsp` sidechain · `luma_vst3` |
| `bypass` | Sí / no | Botón «Bypass» (también el bypass del host) | Salida idéntica a la entrada | `luma_dsp` bypass |
| `meter` | Reducción / Entrada / Salida | Selector del vúmetro | Cambia la lectura de la aguja | `luma_editor` |
| `attack` | 1–40 ms | Perilla «Ataque» | Ajusta el tiempo de respuesta de la rama rápida. Valor original 8 ms | `luma_dsp` |
| `stereoLink` | 0–100 % | Perilla «Enlace L/R» | Interpola la reducción independiente y la reducción común | `luma_dsp` enlace L/R |
| `range` | 0–40 dB | Perilla «Rango» | Limita la reducción de ganancia por canal | `luma_dsp` · `luma_vst3` |
| `detectorModel` | Clásico / Precisión | Selector «Modelo del detector» | Clásico abre sesiones previas con su detector original. Precisión usa filtros calibrados, RMS de dos polos por canal y rodilla de tercer orden de continuidad | `luma_dsp` · `luma_editor` |
| `autoMakeup` (0.4.0) | Apagado / encendido | Botón «Ganancia auto» bajo la perilla Ganancia (encendido muestra «Auto +X dB») | Suma la reducción estática de una señal de −18 dBFS RMS (0 VU) a la ganancia manual. Apagado por defecto: los proyectos anteriores suenan igual | `luma_dsp` (12 combinaciones, ±0,1 dB) · `luma_editor` (botón ↔ parámetro y automatización) |
| Curva (0.4.0) | Solo lectura | Panel «Curva» en la pantalla | Curva estática entrada → salida con la misma fórmula del motor (`staticReductionDb`), umbral y punto de trabajo en vivo | `luma_editor` (capturas claro/oscuro) |
| Escucha | Activada / apagada | Botón «Escuchar» | Monitoriza el detector filtrado. El bypass y la exportación tienen prioridad. Se reinicia al cerrar el editor o cargar un estado | `luma_editor` · `luma_dsp` |
| Presets | 4 de fábrica | Selector «Preset» | Aplica los 9 diales y Limit como gestos | `luma_editor` |
| Tema | Claro / oscuro | Botón de tema | Guarda `dark` en el proyecto | `luma_editor` |

> [!check] Estado
> 14 parámetros automatizables. El panel mide picos de muestra en entrada y salida y dibuja la reducción óptica de L/R. Los parámetros de las versiones 0.1.0 y 0.2.0 conservan su identidad y sus valores guardados.
