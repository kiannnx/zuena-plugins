import React from "react";
import { AbsoluteFill, Interactive, interpolate, useCurrentFrame } from "remotion";
import { clamp, easeOut, easeSpring, font, useLayout, usePalette } from "../brand";
import { Band, freqToX, totalDb } from "../eqMath";

const RANGE_DB = 12;
const GRID_HZ = [50, 100, 200, 500, 1000, 2000, 5000, 10000];
const LABELS: [number, string][] = [[100, "100"], [1000, "1k"], [10000, "10k"]];

export const CurveScene: React.FC = () => {
  const frame = useCurrentFrame();
  const c = usePalette();
  const { portrait, width } = useLayout();
  const W = portrait ? width - 110 : 1120;
  const H = portrait ? 560 : 380;
  const yOf = (db: number) => H / 2 - (db / RANGE_DB) * (H / 2 - 20);

  const p = (start: number) => interpolate(frame, [start, start + 30], [0, 1], { ...clamp, easing: easeSpring });
  // La banda de 285 Hz es dinámica: a partir del cuadro 80 "respira" con la señal.
  const breathe = interpolate(frame, [80, 100], [0, 1], clamp) * 2.2 * Math.sin((frame - 80) / 7);

  const bands: Band[] = [
    { kind: "lowcut", freq: interpolate(p(8), [0, 1], [8, 48]) },
    { kind: "bell", freq: 285, gainDb: -6.5 * p(22) + breathe, q: 1.6 },
    { kind: "highshelf", freq: 6500, gainDb: 4.5 * p(36) },
  ];
  // Los nodos se apoyan sobre la curva compuesta, como en el plugin.
  const nodes = [
    { hz: 48, at: 8 },
    { hz: 285, at: 22 },
    { hz: 6500, at: 36 },
  ].map((n) => ({ ...n, db: totalDb(bands, n.hz) }));

  const pts: string[] = [];
  for (let i = 0; i <= 240; i++) {
    const hz = 20 * 10 ** ((3 * i) / 240);
    pts.push(`${freqToX(hz, W).toFixed(1)},${yOf(Math.max(-40, totalDb(bands, hz))).toFixed(1)}`);
  }
  const curve = `M${pts.join(" L")}`;
  const area = `${curve} L${W},${yOf(0)} L0,${yOf(0)} Z`;
  const draw = interpolate(frame, [0, 26], [0, 1], { ...clamp, easing: easeOut });

  return (
    <AbsoluteFill name="Curva" style={{ backgroundColor: c.paper, fontFamily: font, alignItems: "center", justifyContent: "center" }}>
      <Interactive.Div
        name="Texto"
        style={{
          position: "absolute",
          top: portrait ? 150 : 70,
          width: portrait ? W : undefined,
          textAlign: "center",
          fontSize: portrait ? 72 : 60,
          lineHeight: 1.05,
          fontWeight: 600,
          letterSpacing: "-0.035em",
          color: c.ink,
          opacity: interpolate(frame, [4, 24], [0, 1], { ...clamp, easing: easeOut }),
          translate: interpolate(frame, [4, 28], ["0px 16px", "0px 0px"], { ...clamp, easing: easeOut }),
        }}
      >
        {portrait ? (
          <>
            24 bandas.
            <br />
            Cada una, dinámica.
          </>
        ) : (
          "24 bandas. Cada una, dinámica."
        )}
      </Interactive.Div>
      <svg width={W} height={H + 40} viewBox={`0 0 ${W} ${H + 40}`} style={{ marginTop: portrait ? 200 : 90, overflow: "visible" }}>
        <defs>
          <linearGradient id="luz" x1="0" x2="1" y1="0" y2="0">
            {c.iri.map((col, i) => <stop key={col} offset={i / 3} stopColor={col} />)}
          </linearGradient>
          <filter id="blur" x="-20%" y="-50%" width="140%" height="200%"><feGaussianBlur stdDeviation="18" /></filter>
          <clipPath id="marco"><rect x={0} y={-30} width={W} height={H + 30} /></clipPath>
        </defs>
        {GRID_HZ.map((hz) => (
          <line key={hz} x1={freqToX(hz, W)} x2={freqToX(hz, W)} y1={0} y2={H} stroke={c.line} strokeWidth={1.5} />
        ))}
        {[-6, 0, 6].map((db) => (
          <line key={db} x1={0} x2={W} y1={yOf(db)} y2={yOf(db)} stroke={c.line} strokeWidth={db === 0 ? 2 : 1.5} />
        ))}
        {LABELS.map(([hz, t]) => (
          <text key={hz} x={freqToX(hz, W)} y={H + 34} textAnchor="middle" fontSize={26} fill={c.muted} fontFamily={font}>{t}</text>
        ))}
        <g clipPath="url(#marco)">
          <path d={area} fill="url(#luz)" opacity={0.55 * draw} filter="url(#blur)" />
          <path d={area} fill="url(#luz)" opacity={0.16 * draw} />
          <path d={curve} fill="none" stroke={c.ink} strokeWidth={4} strokeLinejoin="round" pathLength={1} strokeDasharray={1} strokeDashoffset={1 - draw} />
        </g>
        {nodes.map((n, i) => {
          const s = interpolate(frame, [n.at, n.at + 18], [0, 1], { ...clamp, easing: easeSpring });
          const ring = i === 1 ? interpolate(frame, [80, 100], [0, 1], clamp) * (0.5 + 0.5 * Math.abs(Math.sin((frame - 80) / 7))) : 0;
          return (
            <g key={n.hz} transform={`translate(${freqToX(n.hz, W)} ${yOf(n.db)}) scale(${s})`}>
              {i === 1 && <circle r={34} fill="none" stroke={c.iri[1]} strokeWidth={4} opacity={ring} />}
              <circle r={15} fill={c.surface} stroke={c.band[i]} strokeWidth={6} />
            </g>
          );
        })}
      </svg>
    </AbsoluteFill>
  );
};
