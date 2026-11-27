import React from "react";
import { Composition, Folder } from "remotion";
import { fontsReady } from "./brand";
import { LumaStage } from "./LumaStage";
import { CurveScene } from "./scenes/CurveScene";
import { Features } from "./scenes/Features";
import { Intro } from "./scenes/Intro";
import { Outro } from "./scenes/Outro";
import { PROMO_FRAMES, PromoProps, ZuenaEqPromo } from "./ZuenaEqPromo";

const landscape = { width: 1280, height: 720, fps: 30 } as const;
// Panel derecho de la ficha del launcher (≈ 3:4): el vídeo de fondo en bucle.
const portrait = { width: 900, height: 1200, fps: 30 } as const;
const waitFonts = async () => {
  await fontsReady;
  return {};
};
const props: PromoProps = { version: "0.3.1", theme: "light" };

export const RemotionRoot: React.FC = () => (
  <>
    <Folder name="Escenas">
      <Composition id="Intro" component={Intro} durationInFrames={90} {...landscape} calculateMetadata={waitFonts} />
      <Composition id="Curva" component={CurveScene} durationInFrames={150} {...landscape} calculateMetadata={waitFonts} />
      <Composition id="Funciones" component={Features} durationInFrames={120} {...landscape} calculateMetadata={waitFonts} />
      <Composition id="Cierre" component={Outro} durationInFrames={90} {...landscape} defaultProps={{ version: "0.3.1" }} calculateMetadata={waitFonts} />
    </Folder>
    {/* La versión y el tema llegan desde render.mjs (versión de catalog.json). */}
    <Composition id="ZuenaEqPromo" component={ZuenaEqPromo} durationInFrames={PROMO_FRAMES} {...landscape} defaultProps={props} calculateMetadata={waitFonts} />
    <Composition id="ZuenaEqStage" component={ZuenaEqPromo} durationInFrames={PROMO_FRAMES} {...portrait} defaultProps={props} calculateMetadata={waitFonts} />
    <Composition id="ZuenaLumaStage" component={LumaStage} durationInFrames={360} {...portrait} defaultProps={{ theme: "light" as const }} calculateMetadata={waitFonts} />
  </>
);
