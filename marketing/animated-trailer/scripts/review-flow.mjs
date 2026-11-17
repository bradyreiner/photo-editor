import { bundle } from '@remotion/bundler';
import { openBrowser, selectComposition, renderStill } from '@remotion/renderer';
import { mkdir } from 'node:fs/promises';
import path from 'node:path';

const serveUrl = await bundle({ entryPoint: path.resolve('src/index.ts') });
const browserInstance = await openBrowser('chrome');
const composition = await selectComposition({ serveUrl, id: 'Looksy-Launch-Flow-v7', browserInstance, port: 3023 });
await mkdir('out/flow/frames', { recursive: true });
const seconds = [0,1,2.5,4.5,6.5,8.98,9,12,15,19.2,21.8,23.7,25.5,28.5,32.98,33,36,38.98,39,42.5,45,49,51.5,54.98,55,57,59,62,64.5,66.5,67.98,68,69,70.4,72,73.8,74.8,76.5,78,81.5,83.5,86.5,89.5,92.5,95.7,98.5,100,102.98,103,107,110.98,111,112.3,117.98,118,122,126,130,131.98,132,134,137.98];
let index = 0;
try {
  await Promise.all(Array.from({ length: 4 }, async (_, worker) => {
    while (index < seconds.length) {
      const second = seconds[index++];
      const frame = Math.min(8279, Math.round(second * 60));
      await renderStill({ serveUrl, composition, browserInstance, frame, port: 3023 + worker,
        output: `out/flow/frames/${String(frame).padStart(5,'0')}.jpg`, imageFormat: 'jpeg', jpegQuality: 90, scale: .5 });
      process.stdout.write(`${second}s `);
    }
  }));
} finally { await browserInstance.close({ silent: true }); }
