import React from "react";
import { linearTiming, TransitionSeries } from "@remotion/transitions";
import { fade } from "@remotion/transitions/fade";
import { Theme, ThemeContext } from "./brand";
import { CurveScene } from "./scenes/CurveScene";
import { Features } from "./scenes/Features";
import { Intro } from "./scenes/Intro";
import { Outro } from "./scenes/Outro";

export type PromoProps = { version: string; theme: Theme };

// 90 + 150 + 120 + 90 − 3 × 15 de fundido = 405 cuadros (13,5 s a 30 fps)
export const PROMO_FRAMES = 405;

export const ZuenaEqPromo: React.FC<PromoProps> = ({ version, theme }) => (
  <ThemeContext.Provider value={theme}>
    <TransitionSeries>
      <TransitionSeries.Sequence name="Intro" durationInFrames={90}>
        <Intro />
      </TransitionSeries.Sequence>
      <TransitionSeries.Transition presentation={fade()} timing={linearTiming({ durationInFrames: 15 })} />
      <TransitionSeries.Sequence name="Curva" durationInFrames={150}>
        <CurveScene />
      </TransitionSeries.Sequence>
      <TransitionSeries.Transition presentation={fade()} timing={linearTiming({ durationInFrames: 15 })} />
      <TransitionSeries.Sequence name="Funciones" durationInFrames={120}>
        <Features />
      </TransitionSeries.Sequence>
      <TransitionSeries.Transition presentation={fade()} timing={linearTiming({ durationInFrames: 15 })} />
      <TransitionSeries.Sequence name="Cierre" durationInFrames={90}>
        <Outro version={version} />
      </TransitionSeries.Sequence>
    </TransitionSeries>
  </ThemeContext.Provider>
);
