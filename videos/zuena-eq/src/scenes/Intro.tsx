import React from "react";
import { AbsoluteFill, Interactive, interpolate, useCurrentFrame } from "remotion";
import { clamp, easeOut, easeSpring, font, useLayout, usePalette } from "../brand";
import { Logo } from "../Logo";

export const Intro: React.FC = () => {
  const frame = useCurrentFrame();
  const c = usePalette();
  const { portrait } = useLayout();

  return (
    <AbsoluteFill name="Intro" style={{ backgroundColor: c.paper, fontFamily: font, alignItems: "center", justifyContent: "center" }}>
      <Interactive.Div
        name="Halo iridiscente"
        style={{
          position: "absolute",
          width: 760,
          height: 760,
          borderRadius: "50%",
          background: `conic-gradient(from 200deg, ${c.iri[0]}, ${c.iri[1]}, ${c.iri[2]}, ${c.iri[3]}, ${c.iri[0]})`,
          filter: "blur(120px)",
          opacity: interpolate(frame, [10, 60], [0, c.glow], { ...clamp, easing: easeOut }),
          rotate: interpolate(frame, [0, 90], ["0deg", "40deg"], clamp),
        }}
      />
      <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 34 }}>
        <Interactive.Div
          name="Logo"
          style={{ scale: interpolate(frame, [0, 24], [0, 1], { ...clamp, easing: easeSpring, output: "perceptual-scale" }) }}
        >
          <Logo size={portrait ? 170 : 150} draw={interpolate(frame, [14, 42], [0, 1], { ...clamp, easing: easeOut })} />
        </Interactive.Div>
        <Interactive.Div
          name="Título"
          style={{
            fontSize: portrait ? 132 : 124,
            fontWeight: 600,
            letterSpacing: "-0.045em",
            color: c.ink,
            lineHeight: 1,
            opacity: interpolate(frame, [30, 52], [0, 1], { ...clamp, easing: easeOut }),
            translate: interpolate(frame, [30, 56], ["0px 28px", "0px 0px"], { ...clamp, easing: easeOut }),
          }}
        >
          ZUENA EQ
        </Interactive.Div>
        <Interactive.Div
          name="Subtítulo"
          style={{
            fontSize: 48,
            fontWeight: 400,
            letterSpacing: "-0.02em",
            color: c.muted,
            textAlign: "center",
            opacity: interpolate(frame, [44, 66], [0, 1], { ...clamp, easing: easeOut }),
            translate: interpolate(frame, [44, 70], ["0px 18px", "0px 0px"], { ...clamp, easing: easeOut }),
          }}
        >
          Ecualizador dinámico VST3
        </Interactive.Div>
      </div>
    </AbsoluteFill>
  );
};
