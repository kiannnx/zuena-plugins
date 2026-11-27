import React from "react";
import { AbsoluteFill, Easing, interpolate, useCurrentFrame } from "remotion";
import { Logo } from "./Logo";
import { font, ThemeContext, type Theme } from "./brand";
import trace from "./luma-trace.json";

// Each triplet is input VU, output VU and optical gain reduction measured
// from the C++ DSP at 48 kHz, averaged visually at the composition's 30 fps.
const frames = trace.length;
const x = (i: number) => 82 + (736 * i) / (frames - 1);
const y = (r: number) => 892 + Math.min(18, r) * 5;
const path = trace.map((sample, i) => `${i ? "L" : "M"} ${x(i).toFixed(1)} ${y(sample[2]).toFixed(1)}`).join(" ");
const tick = (t: number, r: number) => {
  const angle = (-58 + t * 116) * Math.PI / 180;
  return [450 + Math.sin(angle) * r, 735 - Math.cos(angle) * r];
};
const fade = (frame: number, a: number, b: number) =>
  interpolate(frame, [a,b], [0,1], {extrapolateLeft:"clamp",extrapolateRight:"clamp",easing:Easing.inOut(Easing.cubic)});

export const LumaStage: React.FC<{theme:Theme}> = ({theme}) => {
  const frame = useCurrentFrame();
  const dark = theme === "dark";
  const paper = dark ? "#151818" : "#f4f3ee";
  const ink = dark ? "#f2f1e9" : "#1c2423";
  const muted = dark ? "#b8bfba" : "#52605a";
  const rule = dark ? "#3e4641" : "#c8cec6";
  const accent = dark ? "#ffd28a" : "#96511e";
  const measured = trace[frame % frames];
  const opening = Math.min(fade(frame,1,24), 1-fade(frame,338,359));
  const middle = fade(frame,118,135) * (1-fade(frame,251,267));
  const end = fade(frame,252,268) * (1-fade(frame,332,353));
  const first = Math.max(0,1-middle-end);
  const needle = tick(1-measured[2]/24,313);
  const mono = "ui-monospace, Consolas, monospace";
  return <ThemeContext.Provider value={theme}>
    <AbsoluteFill style={{backgroundColor:paper,color:ink,fontFamily:font}}>
      <div style={{position:"absolute",inset:0,background:`radial-gradient(ellipse at 88% 38%, ${dark?"#ffd28a12":"#ffd28a42"},transparent 54%),radial-gradient(ellipse at 12% 79%, ${dark?"#5ff0ff08":"#b9a8ff26"},transparent 52%)`}}/>
      <div style={{position:"absolute",top:82,left:74,display:"flex",alignItems:"center",gap:18}}>
        <Logo size={43}/><span style={{letterSpacing:7,fontWeight:600,fontSize:24}}>ZUENA</span>
      </div>
      <div style={{position:"absolute",top:199,left:70,right:58}}>
        <div style={{fontSize:21,color:muted,letterSpacing:4.1,fontWeight:600}}>COMPRESOR ÓPTICO</div>
        <div style={{fontSize:144,lineHeight:1.09,letterSpacing:-10,fontWeight:400,marginLeft:-6}}>Luma</div>
      </div>
      <svg width="900" height="1200" viewBox="0 0 900 1200" style={{position:"absolute",inset:0}}>
        <defs>
          <linearGradient id="plate" x1="0" y1="0" x2="0" y2="1" gradientUnits="objectBoundingBox">
            <stop stopColor={dark?"#40453e":"#383d38"}/><stop offset="1" stopColor="#1e2421"/>
          </linearGradient>
          <clipPath id="scope"><rect x="80" y="879" width={x(frame)-78} height="110"/></clipPath>
        </defs>
        <g transform={`translate(0 ${(1-opening)*6})`}>
          <rect x="61" y="383" width="778" height="395" rx="24" fill="url(#plate)"/>
          <rect x="66" y="388" width="768" height="385" rx="20" fill="none" stroke="#767b6a" strokeOpacity=".45"/>
          <path d={`M ${tick(0,318).join(" ")} A 318 318 0 0 1 ${tick(1,318).join(" ")}`} fill="none" stroke="#e1e0c9" strokeWidth="1.4"/>
          {Array.from({length:49},(_,i)=>{
            const major=i%8===0, a=tick(i/48,315), b=tick(i/48,major?286:i%2?306:299), c=tick(i/48,340);
            return <g key={i}>
              <line x1={a[0]} y1={a[1]} x2={b[0]} y2={b[1]} stroke="#e3e2d1" strokeWidth={major?3:1.4}/>
              {major && <text x={c[0]} y={c[1]+6} fill="#eeeede" textAnchor="middle" fontSize="21" fontFamily={mono}>{24-i/2}</text>}
            </g>;
          })}
          <text x="450" y="617" fill="#e5e5d9" textAnchor="middle" letterSpacing="5" fontSize="19" fontFamily={font}>REDUCCIÓN</text>
          <line x1="451" y1="738" x2={needle[0]+2} y2={needle[1]+3} stroke="#0c120f" strokeWidth="7" opacity="0.65"/>
          <line x1="450" y1="735" x2={needle[0]} y2={needle[1]} stroke="#ffd28a" strokeLinecap="round" strokeWidth="4"/>
          <circle cx="450" cy="735" r="10" fill="#e5c08b"/>
          <text x="91" y="746" fill="#c7cabd" fontSize="16" letterSpacing="2.5">OPTO</text>
          <text x="807" y="746" textAnchor="end" fill="#f2dfbc" fontSize="25" fontFamily={mono}>{(measured[2]<0.05?0:-measured[2]).toFixed(1)} dB</text>
        </g>
        <text x="81" y="847" fill={muted} fontFamily={mono} fontSize="19" letterSpacing="1.5">MEMORIA DE LA CÉLULA</text>
        <text x="820" y="847" textAnchor="end" fill={muted} fontFamily={mono} fontSize="17">0 — 12 s</text>
        {[0,6,12,18].map(v=><line key={v} x1="82" y1={y(v)} x2="818" y2={y(v)} stroke={rule} strokeWidth="1"/>)}
        <path d={path} fill="none" stroke={muted} opacity=".28" strokeWidth="2.2"/>
        <path d={path} fill="none" stroke={accent} strokeWidth="4.5" clipPath="url(#scope)"/>
        <circle cx={x(frame)} cy={y(measured[2])} r="5.5" fill={accent}/>
      </svg>
      {[["Cada golpe.",first],["Cada nota sostenida.",middle],["Recuperación a dos tiempos.",end]].map(([label,alpha])=>
        <div key={String(label)} style={{position:"absolute",top:1026,left:80,fontSize:45,letterSpacing:-1.8,fontWeight:400,
          color:ink,opacity:Number(alpha)*opening,transform:`translateY(${(1-Number(alpha))*9}px)`}}>{label}</div>)}
    </AbsoluteFill>
  </ThemeContext.Provider>;
};
