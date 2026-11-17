import {bundle} from '@remotion/bundler';
import {openBrowser,selectComposition,renderMedia} from '@remotion/renderer';
import fs from 'node:fs/promises';
import path from 'node:path';
const ranges=[[345,1875],[2745,4005],[4785,4815],[5505,5535],[7545,7575],[7965,8205],[8385,8415]];
const out='out/creative-polish-v3/flow-patches';
await fs.mkdir(out,{recursive:true});
await fs.writeFile(`${out}/ranges.json`,JSON.stringify(ranges));
const serveUrl=await bundle({entryPoint:path.resolve('src/index.ts')});
const browser=await openBrowser('chrome');
try {
 for(const format of ['Landscape','Portrait']) {
  const composition=await selectComposition({serveUrl,id:`Looksy-Creative-Launch-${format}`,browserInstance:browser,inputProps:{withMusic:false}});
  for(const [start,end] of ranges) {
   const outputLocation=`${out}/${format.toLowerCase()}-${start}-${end}.mp4`;
   console.log(`Start ${format} ${start}-${end}`);
   await renderMedia({serveUrl,composition,puppeteerInstance:browser,inputProps:{withMusic:false},frameRange:[start,end-1],outputLocation,codec:'h264',crf:17,x264Preset:'fast',muted:true,concurrency:3,overwrite:true});
   console.log(`Done ${format} ${start}-${end}`);
  }
 }
} finally {await browser.close({silent:true});}
