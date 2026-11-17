import { bundle } from '@remotion/bundler';
import { openBrowser, selectComposition, renderStill } from '@remotion/renderer';
import { mkdir } from 'node:fs/promises';
import path from 'node:path';

const serveUrl = await bundle({ entryPoint: path.resolve('src/index.ts') });
const browserInstance = await openBrowser('chrome');
const composition = await selectComposition({ serveUrl, id: 'Looksy-Launch-Motion-v6', browserInstance, port: 3023 });
await mkdir('out/pulse/frames', { recursive: true });
const seconds = [0,1,3,5,7,8.5,9.98,10,10.3,12.5,15.5,18,19.2,21,25,28,31.98,32,34,36,38.98,39,41,44.5,46.7,47.5,48.5,50,52.98,53,55,58,61,64.98,65,65.5,68,71.5,73.5,76.5,80,84,87.98,88,91,96.98,97,97.4,98.4,103.98,104,104.4,105,109,112,115,118.98,119,119.7,121,124,125.98];
let index = 0;
try {
  await Promise.all(Array.from({ length: 4 }, async (_, worker) => {
    while (index < seconds.length) {
      const second = seconds[index++];
      const frame = Math.min(7559, Math.round(second * 60));
      await renderStill({ serveUrl, composition, browserInstance, frame, port: 3023 + worker,
        output: `out/pulse/frames/${String(frame).padStart(5,'0')}.jpg`, imageFormat: 'jpeg', jpegQuality: 90, scale: .5 });
      process.stdout.write(`${second}s `);
    }
  }));
} finally { await browserInstance.close({ silent: true }); }
