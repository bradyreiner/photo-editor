import {bundle} from '@remotion/bundler';
import {openBrowser,selectComposition,renderMedia} from '@remotion/renderer';
import fs from 'node:fs/promises';
import path from 'node:path';
const ranges=[[2370,2760],[5520,6240],[6960,7560],[7770,7980],[8400,8940]],out='out/creative-polish-v4/review-patches';
await fs.mkdir(out,{recursive:true});await fs.writeFile(`${out}/ranges.json`,JSON.stringify(ranges));
const serveUrl=await bundle({entryPoint:path.resolve('src/index.ts')});const browser=await openBrowser('chrome');
try {for(const format of ['Landscape','Portrait']) {
 const composition=await selectComposition({serveUrl,id:`Looksy-Creative-Launch-${format}`,browserInstance:browser,inputProps:{withMusic:false}});
 for(const [start,end] of (process.argv.includes("--gallery-only")?ranges.filter(x=>x[0]===2370):process.argv.includes("--agent-only")?ranges.filter(x=>x[0]===7770):ranges.filter(x=>x[0]!==7770&&x[0]!==2370))){console.log(`Start ${format} ${start}-${end}`);await renderMedia({serveUrl,composition,puppeteerInstance:browser,inputProps:{withMusic:false},frameRange:[start,end-1],outputLocation:`${out}/${format.toLowerCase()}-${start}-${end}.mp4`,codec:'h264',crf:17,x264Preset:'fast',muted:true,concurrency:2,overwrite:true});console.log(`Done ${format} ${start}-${end}`);}
}} finally {await browser.close({silent:true});}
