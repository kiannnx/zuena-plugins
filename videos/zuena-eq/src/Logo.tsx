import React from "react";
import { useTheme } from "./brand";

/**
 * Logo ZUENA: punto negro con Z blanca; sobre fondo oscuro se usa la versión invertida
 * (punto blanco, Z negra). `draw` (0–1) traza la Z. Geometría intacta.
 */
export const Logo: React.FC<{ size: number; draw?: number }> = ({ size, draw = 1 }) => {
  const dark = useTheme() === "dark";
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" role="img" aria-label="ZUENA">
      <circle cx="32" cy="32" r="32" fill={dark ? "#fff" : "#000"} />
      <path
        d="M19 20h26L19 44h26"
        fill="none"
        stroke={dark ? "#000" : "#fff"}
        strokeWidth={6.5}
        strokeLinecap="round"
        strokeLinejoin="round"
        pathLength={1}
        strokeDasharray={1}
        strokeDashoffset={1 - draw}
      />
    </svg>
  );
};
