// Tokens de la marca ZUENA (sistema "Punto") para los vídeos.
// Iridiscente solo como luz: brillos y halos, nunca relleno de superficies ni del logo.
import { loadFont } from "@remotion/fonts";
import { createContext, useContext } from "react";
import { Easing, staticFile, useVideoConfig } from "remotion";

export type Theme = "light" | "dark";

const iri = ["#5ff0ff", "#b9a8ff", "#ffb3e1", "#ffd28a"] as const;

const palettes = {
  light: {
    paper: "#f5f5f3",
    surface: "#fbfbfa",
    ink: "#181a1c",
    muted: "#62666a",
    line: "rgba(26, 30, 34, 0.13)",
    iri,
    // Colores de banda del plugin (Theme.h, modo claro)
    band: ["#22b8cf", "#5a9dff", "#8f7cf0", "#bd72e6"] as const,
    glow: 0.42,
  },
  dark: {
    paper: "#101112",
    surface: "#191b1d",
    ink: "#f1f1ef",
    muted: "#a6a9ad",
    line: "rgba(238, 241, 244, 0.14)",
    iri,
    band: ["#52dcf2", "#7eb6ff", "#af9fff", "#d894fa"] as const,
    glow: 0.3,
  },
};

export type Palette = (typeof palettes)["light"];

export const ThemeContext = createContext<Theme>("light");
export const usePalette = (): Palette => palettes[useContext(ThemeContext)] as Palette;
export const useTheme = () => useContext(ThemeContext);

/** Vertical (panel de la ficha del launcher) o apaisado (vídeo clásico). */
export const useLayout = () => {
  const { width, height } = useVideoConfig();
  return { portrait: height > width, width, height };
};

export const font = "Inter, system-ui, sans-serif";

// Prisma Motion: salida suave y resorte con rebote leve
export const easeOut = Easing.bezier(0.2, 0.8, 0.2, 1);
export const easeSpring = Easing.bezier(0.34, 1.56, 0.64, 1);

export const fontsReady = Promise.all([
  loadFont({ family: "Inter", url: staticFile("inter-latin-400.woff2"), weight: "400" }),
  loadFont({ family: "Inter", url: staticFile("inter-latin-600.woff2"), weight: "600" }),
  loadFont({ family: "Inter", url: staticFile("inter-latin-ext-400.woff2"), weight: "400" }),
]);

export const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
