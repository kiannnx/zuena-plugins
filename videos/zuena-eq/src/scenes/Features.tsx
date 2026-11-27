import React from "react";
import { AbsoluteFill, Interactive, interpolate, useCurrentFrame } from "remotion";
import { clamp, easeOut, font, useLayout, usePalette } from "../brand";

// Solo funciones que ZUENA EQ tiene de verdad (ver Matriz-Controles-EQ).
const ITEMS: [string, string][] = [
  ["Fase Natural 2x", "31 muestras de latencia"],
  ["Mid / Side", "y Estéreo, L, R por banda"],
  ["Auto-Gain", "y escala global 0–200 %"],
  ["Analizador", "con picos y congelado"],
];

export const Features: React.FC = () => {
  const frame = useCurrentFrame();
  const c = usePalette();
  const { portrait } = useLayout();

  return (
    <AbsoluteFill name="Funciones" style={{ backgroundColor: c.paper, fontFamily: font, justifyContent: "center", paddingLeft: portrait ? 96 : 150 }}>
      <div style={{ display: "flex", flexDirection: "column", gap: portrait ? 52 : 34 }}>
        {ITEMS.map(([title, sub], i) => {
          const at = 6 + i * 12;
          return (
            <Interactive.Div
              key={title}
              name={title}
              style={{
                display: "flex",
                alignItems: "baseline",
                whiteSpace: "nowrap",
                gap: 26,
                opacity: interpolate(frame, [at, at + 18], [0, 1], { ...clamp, easing: easeOut }),
                translate: interpolate(frame, [at, at + 24], ["-36px 0px", "0px 0px"], { ...clamp, easing: easeOut }),
              }}
            >
              <span
                style={{
                  flex: "none",
                  width: 22,
                  height: 22,
                  borderRadius: "50%",
                  background: c.ink,
                  boxShadow: `0 0 28px 6px ${c.iri[i]}`,
                  translate: "0px -6px",
                }}
              />
              <span style={{ display: "flex", flexDirection: portrait ? "column" : "row", alignItems: portrait ? "flex-start" : "baseline", gap: portrait ? 10 : 26 }}>
                <span style={{ fontSize: portrait ? 72 : 64, fontWeight: 600, letterSpacing: "-0.035em", color: c.ink, lineHeight: 1.05 }}>{title}</span>
                <span style={{ fontSize: portrait ? 42 : 40, color: c.muted, letterSpacing: "-0.015em" }}>{sub}</span>
              </span>
            </Interactive.Div>
          );
        })}
      </div>
    </AbsoluteFill>
  );
};
