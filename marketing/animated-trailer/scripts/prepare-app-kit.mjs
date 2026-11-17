import { readFile, writeFile, mkdir, copyFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import path from "node:path";
import { fileURLToPath } from "node:url";
const project = fileURLToPath(new URL("../", import.meta.url));
const repo = path.resolve(project, "../..");
const qml = "apps/desktop/qml/components/";
const read = async (p) => readFile(path.join(repo, p), "utf8");
await mkdir(path.join(project, "src/app-kit"), { recursive: true });
await mkdir(path.join(project, "public/media"), { recursive: true });
const sourceFiles = [
  qml + "Theme.qml",
  "apps/desktop/qml/workspaces/StudioWorkspace.qml",
  qml + "InspectorSlider.qml",
  qml + "ToneCurveEditor.qml",
  qml + "StudioAdjustmentSlider.qml",
  qml + "AppIcon.qml",
  qml + "OpticalLightField.qml",
  qml + "OpticalActivityIndicator.qml",
  qml + "LooksyLoadingScene.qml",
  qml + "ProgressStrip.qml",
  qml + "CompletionCaustic.qml",
  qml + "BrandWordmark.qml",
  qml + "LooksyMark.qml",
  "apps/desktop/shaders/optical_light.frag",
  "apps/desktop/assets/brand/looksy-logo-dark.png",
  "apps/desktop/assets/looksy-rgb-orb.png",
];
const manifest = [];
for (const source of sourceFiles) {
  const buffer = await readFile(path.join(repo, source));
  manifest.push({
    source,
    sha256: createHash("sha256").update(buffer).digest("hex"),
  });
}
const theme = await read(qml + "Theme.qml");
const colors = {};
for (const name of [
  "brandInk",
  "brandPaper",
  "brandRed",
  "brandGreen",
  "brandBlue",
  "brandCoral",
  "ink950",
  "ink900",
  "ink850",
  "ink800",
  "lineStrong",
  "lineSoft",
  "textMain",
  "textMuted",
  "opticalLight",
]) {
  const line = theme
    .split("\n")
    .find((line) => line.includes("property color " + name + ":"));
  const literals = line?.match(/#[A-Fa-f0-9]+/g);
  if (!literals?.length) throw new Error("No literal theme token " + name);
  colors[name] = literals[literals.length - 1];
}
await writeFile(
  path.join(project, "src/app-kit/tokens.ts"),
  "// Generated from current Theme.qml by prepare-app-kit.mjs.\nexport const appTokens = " +
    JSON.stringify(colors, null, 2) +
    " as const;\n",
);
let shader = await read("apps/desktop/shaders/optical_light.frag");
shader = shader.replace(
  "#version 440",
  "#version 300 es\nprecision highp float;",
);
shader = shader.replace(
  /layout\(std140, binding = 0\) uniform buf \{[\s\S]*?\} ubuf;/,
  "uniform float qt_Opacity;\nuniform vec2 resolution;\nuniform float lightPhase;\nuniform vec4 parameters;",
);
shader = shader.replace("layout(location = 0) in vec2 uv;", "in vec2 uv;");
shader = shader.replaceAll("ubuf.", "");
await writeFile(
  path.join(project, "src/app-kit/optical-shader.ts"),
  "// Generated WebGL2 adapter. Original optical-light equations are unchanged.\nexport const opticalFragmentShader = " +
    JSON.stringify(shader) +
    ";\n",
);
const icons = await read(qml + "AppIcon.qml");
const names = [
  "text",
  "check",
  "keyframe",
  "curve",
  "search",
  "adjust",
  "layers",
  "carousel",
  "timeline",
  "reset",
  "image",
  "visible",
];
let cases = "";
for (const name of names) {
  const start = icons.indexOf('case "' + name + '":');
  if (start < 0) throw new Error("Missing icon " + name);
  const end = icons.indexOf("break", start);
  const body = icons.slice(start, end + 5);
  if (body.includes("Qt.")) throw new Error("Icon needs Qt adapter " + name);
  cases += body + ";\n";
}
const helper = `export function paintAppIcon(ctx:CanvasRenderingContext2D,name:string,size:number,color:string){
const w=size,h=size,midX=w/2,midY=h/2;
ctx.clearRect(0,0,size,size);ctx.strokeStyle=color;ctx.fillStyle=color;
ctx.lineWidth=Math.max(1.15,size*.085);ctx.lineCap='round';ctx.lineJoin='round';
const line=(c:CanvasRenderingContext2D,x1:number,y1:number,x2:number,y2:number)=>{c.beginPath();c.moveTo(x1,y1);c.lineTo(x2,y2);c.stroke();};
const rect=(c:CanvasRenderingContext2D,x:number,y:number,a:number,b:number,fill:boolean)=>{c.beginPath();c.rect(x,y,a,b);if(fill)c.fill();else c.stroke();};
const circle=(c:CanvasRenderingContext2D,x:number,y:number,r:number,fill:boolean)=>{c.beginPath();c.arc(x,y,r,0,Math.PI*2);if(fill)c.fill();else c.stroke();};
const poly=(c:CanvasRenderingContext2D,pts:number[][],fill:boolean)=>{c.beginPath();c.moveTo(pts[0][0],pts[0][1]);for(const p of pts.slice(1))c.lineTo(p[0],p[1]);c.closePath();if(fill)c.fill();else c.stroke();};
switch(name){${cases}}
}\n`;
await writeFile(
  path.join(project, "src/app-kit/icon-painter.ts"),
  "// Icon drawing commands copied directly from AppIcon.qml.\n" + helper,
);
for (const [source, dest] of [
  ["apps/desktop/assets/brand/looksy-logo-dark.png", "looksy-logo-dark.png"],
  ["apps/desktop/assets/looksy-rgb-orb.png", "looksy-rgb-orb.png"],
])
  await copyFile(
    path.join(repo, source),
    path.join(project, "public/media", dest),
  );
await writeFile(
  path.join(project, "app-kit-source-map.json"),
  JSON.stringify(
    {
      kind: "App-source-derived Remotion adapters; not Qt runtime captures",
      generated_on: "2026-10-23",
      sources: manifest,
      adaptations: [
        "Qt 440 shader interface to WebGL2 uniforms; optical equations retained",
        "AppIcon canvas paths copied with typed browser helpers",
        "InspectorSlider geometry/state port; film values and cursor are authored",
        "Current app palette and original logo/orb assets",
        "Browser system font substituted for Qt platform UI font",
      ],
    },
    null,
    2,
  ) + "\n",
);
console.log(
  "Prepared app-derived theme, original brand assets, 12 exact icon paths, optical shader and source hashes.",
);
