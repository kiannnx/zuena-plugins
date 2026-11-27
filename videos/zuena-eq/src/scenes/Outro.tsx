import React from "react";
import { AbsoluteFill, Interactive, interpolate, useCurrentFrame } from "remotion";
import { clamp, easeOut, easeSpring, font, useLayout, usePalette } from "../brand";
import { Logo } from "../Logo";

export const Outro: React.FC<{ version: string }> = ({ version }) => {
  const frame = useCurrentFrame();
  const c = usePalette();
  const { portrait } = useLayout();

  return (
    <AbsoluteFill name="Cierre" style={{ backgroundColor: c.paper, fontFamily: font, alignItems: "center", justifyContent: "center" }}>
      <Interactive.Div
        name="Luz"
        style={{
          position: "absolute",
          width: 900,
          height: portrait ? 520 : 300,
          borderRadius: "50%",
          background: `linear-gradient(90deg, ${c.iri[0]}, ${c.iri[1]}, ${c.iri[2]})`,
          filter: "blur(110px)",
          opacity: interpolate(frame, [0, 40], [0, c.glow * 0.9], { ...clamp, easing: easeOut }),
        }}
      />
      <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 26 }}>
        <Interactive.Div name="Logo" style={{ scale: interpolate(frame, [0, 20], [0.6, 1], { ...clamp, easing: easeSpring, output: "perceptual-scale" }) }}>
          <Logo size={portrait ? 120 : 96} />
        </Interactive.Div>
        <Interactive.Div
          name="Gratis"
          style={{
            fontSize: portrait ? 112 : 104,
            lineHeight: 1.02,
            textAlign: "center",
            fontWeight: 600,
            letterSpacing: "-0.045em",
            color: c.ink,
            opacity: interpolate(frame, [8, 28], [0, 1], { ...clamp, easing: easeOut }),
            translate: interpolate(frame, [8, 32], ["0px 22px", "0px 0px"], { ...clamp, easing: easeOut }),
          }}
        >
          {portrait ? (
            <>
              Gratis en
              <br />
              ZUENA Plugins
            </>
          ) : (
            "Gratis en ZUENA Plugins"
          )}
        </Interactive.Div>
        <Interactive.Div
          name="Versión"
          style={{
            fontSize: 42,
            color: c.muted,
            letterSpacing: "-0.01em",
            opacity: interpolate(frame, [22, 42], [0, 1], { ...clamp, easing: easeOut }),
          }}
        >
          {`ZUENA EQ v${version} · VST3 · Windows`}
        </Interactive.Div>
      </div>
    </AbsoluteFill>
  );
};
