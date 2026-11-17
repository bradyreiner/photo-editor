import {bundle} from '@remotion/bundler';
import {openBrowser,selectComposition,renderStill} from '@remotion/renderer';
import fs from 'node:fs/promises';
import path from 'node:path';
const seconds=process.argv.slice(2).map(Number);
const times=seconds.length?seconds:[5,10.7,14,19.2,27,31,38,48,56,60.5,64,69,74.3,79.4,85,92,100,107,114,121,132,140,145,152,157,163,171,178,189,202,213];
const serveUrl=await bundle({entryPoint:path.resolve('src/index.ts')});
const browserInstance=await openBrowser('chrome');
try{
 for(const format of ['Landscape','Portrait']){
  const composition=await selectComposition({serveUrl,id:`Looksy-Creative-Launch-${format}`,browserInstance,inputProps:{withMusic:false},port:3041});
  let next=0;
  await fs.mkdir(`out/creative-launch/stills/${format.toLowerCase()}`,{recursive:true});
  await Promise.all(Array.from({length:3},async(_,w)=>{while(next<times.length){const t=times[next++];await renderStill({serveUrl,composition,browserInstance,port:3042+w,inputProps:{withMusic:false},frame:Math.round(t*30),imageFormat:'jpeg',jpegQuality:94,scale:.5,output:`out/creative-launch/stills/${format.toLowerCase()}/${String(Math.round(t*30)).padStart(5,'0')}.jpg`});process.stdout.write(`${format} ${t}\n`);}}));
 }
}finally{await browserInstance.close({silent:true});}
