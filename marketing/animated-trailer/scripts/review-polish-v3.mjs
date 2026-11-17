import {bundle} from '@remotion/bundler';
import {openBrowser,selectComposition,renderStill} from '@remotion/renderer';
import fs from 'node:fs/promises';
import path from 'node:path';
const times=process.argv.slice(2).map(Number);
const seconds=times.length?times:[5,14.5,20,26,31,35.4,37.4,43.5,51.5,57,65,70,77,82,89,95,102,108,114,120,127,134,146,155,163,170,179,188.5,191.3,197.5,202,208,214,219,224,232,239,247,253,260,266,280,285];
const serveUrl=await bundle({entryPoint:path.resolve('src/index.ts')});
const browserInstance=await openBrowser('chrome');
try{for(const format of ['Landscape','Portrait']){
 const composition=await selectComposition({serveUrl,id:`Looksy-Creative-Launch-${format}`,browserInstance,inputProps:{withMusic:false},port:3041});let next=0;
 await fs.mkdir(`out/creative-polish-v3/stills/${format.toLowerCase()}`,{recursive:true});
 await Promise.all(Array.from({length:3},async(_,w)=>{while(next<seconds.length){const t=seconds[next++];await renderStill({serveUrl,composition,browserInstance,inputProps:{withMusic:false},port:3042+w,frame:Math.round(t*30),imageFormat:'jpeg',jpegQuality:94,scale:.5,output:`out/creative-polish-v3/stills/${format.toLowerCase()}/${String(Math.round(t*30)).padStart(5,'0')}.jpg`});process.stdout.write(`${format} ${t}\n`);}}));
}}finally{await browserInstance.close({silent:true});}
