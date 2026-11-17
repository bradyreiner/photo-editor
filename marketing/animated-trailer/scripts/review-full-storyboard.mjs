import {bundle} from '@remotion/bundler';
import {openBrowser,selectComposition,renderStill} from '@remotion/renderer';
import {mkdir} from 'node:fs/promises';
import path from 'node:path';
const serveUrl=await bundle({entryPoint:path.resolve('src/index.ts')});
const browserInstance=await openBrowser('chrome');
try{
 const composition=await selectComposition({serveUrl,id:'Looksy-Full-Storyboard',browserInstance,port:3023});await mkdir('out/rebuild/full/frames',{recursive:true});
 const seconds=[10.8,12,15,18,22,26,28,31,35,39,43,47.9,48,51,55,59.9,61.8,64.5,66.8,69.5,71.5,73.9,75.5,79,83,87,91.9,94,98.8,99.98,100,104,108.5,112,113.98,114,118.5,123.98,124,128,132,135.98,136,140,144,145.98,146,150,154.5,158.5,162.5,163.98,164,168.5,173.98,174,177,180,184,187.5,190,193.5,196.8,200.8];let ix=0;
 await Promise.all(Array.from({length:4},async(_,worker)=>{while(ix<seconds.length){let t=seconds[ix++];await renderStill({serveUrl,composition,browserInstance,port:3023+worker,frame:Math.round(t*60),imageFormat:'jpeg',jpegQuality:91,scale:.5,output:`out/rebuild/full/frames/${Math.round(t*60).toString().padStart(5,'0')}.jpg`});process.stdout.write(`${t} `)}}));
}finally{await browserInstance.close({silent:true})}
