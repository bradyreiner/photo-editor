import {bundle} from '@remotion/bundler';
import {openBrowser,selectComposition,renderMedia} from '@remotion/renderer';
import path from 'node:path';
const serveUrl=await bundle({entryPoint:path.resolve('src/index.ts')});
const browser=await openBrowser('chrome');
try {
 for(const format of ['Landscape','Portrait']) {
  const composition=await selectComposition({serveUrl,id:`Looksy-Creative-Launch-${format}`,browserInstance:browser,inputProps:{withMusic:false}});
  let last=-1;
  console.log(`Start ${format} / 8940 frames`);
  await renderMedia({serveUrl,composition,puppeteerInstance:browser,inputProps:{withMusic:false},outputLocation:`out/creative-polish-v4/${format.toLowerCase()}-picture.mp4`,codec:'h264',crf:17,x264Preset:'fast',muted:true,concurrency:5,overwrite:true,onProgress:({renderedFrames,encodedFrames})=>{const percent=Math.floor(renderedFrames/8940*20)*5;if(percent>last){last=percent;console.log(`${format} ${percent}% / rendered ${renderedFrames} / encoded ${encodedFrames}`);}}});
  console.log(`Done ${format}`);
 }
} finally {await browser.close({silent:true});}
