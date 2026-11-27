import { execFileSync, spawnSync } from "node:child_process";
import { copyFileSync, existsSync, mkdtempSync, readdirSync, writeFileSync, rmSync, rmdirSync } from "node:fs";
import { createRequire } from "node:module";
import { tmpdir } from "node:os";
import { join, dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = dirname(fileURLToPath(import.meta.url));
const out = join(root, "../../Free-VST-Client/catalog/packages");
const cli = join(root,"node_modules/@remotion/cli/remotion-cli.js");
const temp = mkdtempSync(join(tmpdir(),"luma-render-"));
const reuse = process.argv[2] === "--from-frames" ? resolve(process.argv[3]) : undefined;
const bundledFfmpeg = join(root,"node_modules/@remotion/compositor-win32-x64-msvc/ffmpeg.exe");
const browserEncoder = reuse || (process.platform === "win32" && spawnSync(bundledFfmpeg,["-version"]).status !== 0);

// Chromium's WebCodecs encoder is an alternative on workstations where the packaged FFmpeg
// cannot start. Both paths use the exact same Remotion frames and the same 30 fps time base.
async function encodeFrames(folder, target) {
  const requireLauncher = createRequire(join(root,"../../Free-VST-Client/package.json"));
  const { chromium } = requireLauncher("playwright-core");
  const executablePath = [process.env.CHROME_PATH,
    join(process.env.ProgramFiles || "", "Google/Chrome/Application/chrome.exe"),
    join(process.env["ProgramFiles(x86)"] || "", "Microsoft/Edge/Application/msedge.exe")].find(p => p && existsSync(p));
  if (!executablePath) throw new Error("Se necesita Chrome o Edge con codificación H.264 para el render alternativo.");
  const frames = readdirSync(folder).filter(v => /^element-\d+\.jpe?g$/i.test(v)).sort();
  if (frames.length !== 360) throw new Error("La presentación requiere los 360 fotogramas de Remotion.");
  const browser = await chromium.launch({executablePath,headless:true});
  try {
    const page = await browser.newPage();
    const origin = "http://localhost:31325";
    await page.route(origin+"/**", async route => {
      const name = new URL(route.request().url()).pathname.slice(1);
      if (name === "") return route.fulfill({contentType:"text/html",body:"<!doctype html><meta charset=utf-8>"});
      if (name === "encoder.mjs") return route.fulfill({contentType:"application/javascript",
        path:join(root,"node_modules/mediabunny/dist/bundles/mediabunny.min.mjs")});
      if (frames.includes(name)) return route.fulfill({contentType:"image/jpeg",path:join(folder,name)});
      return route.fulfill({status:404,body:""});
    });
    await page.goto(origin);
    const encoded = await page.evaluate(async names => {
      const {Output,Mp4OutputFormat,BufferTarget,CanvasSource}=await import("/encoder.mjs");
      const canvas=document.createElement("canvas");canvas.width=900;canvas.height=1200;
      const context=canvas.getContext("2d",{alpha:false});
      const target=new BufferTarget();
      const output=new Output({format:new Mp4OutputFormat({fastStart:"in-memory"}),target});
      const source=new CanvasSource(canvas,{codec:"avc",bitrate:3000000,keyFrameInterval:2});
      output.addVideoTrack(source,{frameRate:30});
      await output.start();
      for(const [frame,name] of names.entries()){
        const bitmap=await createImageBitmap(await (await fetch("/"+name)).blob());
        context.drawImage(bitmap,0,0);bitmap.close();
        await source.add(frame/30,1/30);
      }
      await output.finalize();
      return await new Promise((done,fail)=>{
        const r=new FileReader();r.onload=()=>done(r.result.split(",")[1]);r.onerror=fail;
        r.readAsDataURL(new Blob([target.buffer],{type:"video/mp4"}));
      });
    },frames);
    writeFileSync(target+".mp4",Buffer.from(encoded,"base64"));
    copyFileSync(join(folder,frames[168]),target+".jpg");
  } finally { await browser.close(); }
}
try {
  for (const theme of ["light","dark"]) {
    const target = join(out,"zuena-luma-stage"+(theme==="dark"?"-oscuro":""));
    const props = join(temp,theme+".json");
    writeFileSync(props,JSON.stringify({theme}));
    const run = (kind,extension,options) => execFileSync(process.execPath,
      [cli,kind,"src/index.ts","ZuenaLumaStage",target+extension,"--props="+props,...options],{cwd:root,stdio:"inherit"});
    if (browserEncoder) {
      const frames=join(reuse || join(root,"out"),"luma-frames-"+theme);
      if (!reuse) execFileSync(process.execPath,[cli,"render","src/index.ts","ZuenaLumaStage",frames,
        "--sequence","--image-format=jpeg","--jpeg-quality=94","--concurrency=4","--props="+props],{cwd:root,stdio:"inherit"});
      await encodeFrames(frames,target);
    } else {
      run("render",".mp4",["--codec=h264","--muted","--crf=24","--concurrency=3"]);
      run("still",".jpg",["--frame=168","--image-format=jpeg","--jpeg-quality=90"]);
    }
  }
} finally {
  for(const theme of ["light","dark"]) rmSync(join(temp,theme+".json"),{force:true});
  rmdirSync(temp);
}
