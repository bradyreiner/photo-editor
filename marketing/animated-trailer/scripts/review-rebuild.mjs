import {bundle} from '@remotion/bundler';
import {openBrowser,selectComposition,renderStill} from '@remotion/renderer';
import path from 'node:path';
import {mkdir} from 'node:fs/promises';
const serveUrl=await bundle({entryPoint:path.resolve('src/index.ts')});
const browserInstance=await openBrowser('chrome');
try{
 const c=await selectComposition({serveUrl,id:'Looksy-Motion-Rebuild',browserInstance,port:3023});
 await mkdir('out/rebuild/frames',{recursive:true});
 const seconds=[.8,1.8,3.1,4.5,6.2,8.5,10.8,13,14.8,16.3,18,19.5,21.3,23.1,25.2,26.8,28.8,30.9,33.3,35.5,37.8,40.3,41.9];let n=0;
 await Promise.all(Array.from({length:3},async(_,w)=>{while(n<seconds.length){const t=seconds[n++];await renderStill({serveUrl,composition:c,browserInstance,port:3023+w,frame:Math.round(t*60),output:`out/rebuild/frames/${String(Math.round(t*60)).padStart(4,'0')}.jpg`,imageFormat:'jpeg',jpegQuality:93,scale:.5});process.stdout.write(`${t} `)}}));
 const board=await selectComposition({serveUrl,id:'Looksy-New-Design-Board',browserInstance,port:3023});await renderStill({serveUrl,composition:board,browserInstance,port:3023,frame:0,output:'out/rebuild/design-board.jpg',imageFormat:'jpeg',jpegQuality:95,scale:1});
}finally{await browserInstance.close({silent:true})}
