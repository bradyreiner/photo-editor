import { bundle } from '@remotion/bundler';
import { openBrowser, selectComposition, renderStill } from '@remotion/renderer';
import { mkdir } from 'node:fs/promises';
import path from 'node:path';

const serveUrl = await bundle({ entryPoint: path.resolve('src/index.ts') });
const browserInstance = await openBrowser('chrome');
const composition = await selectComposition({ serveUrl, id: 'Looksy-Studio-Film', browserInstance, port: 3023 });
await mkdir('out/studio/frames', { recursive: true });
const seconds=[0,1,3.8,7.4,9.5,13.98,14,17,19,23,26,29.5,32.5,35,37.7,39.98,40,42,45.5,48.6,50.7,52.5,54.5,56.5,59.98,60,62.5,66.7,69,72,74.9,78,81,83.98,84,86,88.5,92,94.5,98,101.5,103.98,104,105.5,107,109.5,111.3,113.8,115.5,117.7,120.3,123,125,128.5,131.5,134.5,137.5,139.98,140,143.1,146.3,148.8,151.5,155.5,157.98,158,161.5,165.8,170.5,173.3,175.5,178.5,179.98,180,183,185.5,187.5,191.98];
let index = 0;
try {
  await Promise.all(Array.from({ length: 4 }, async (_, worker) => {
    while (index < seconds.length) {
      const second = seconds[index++];
      const frame = Math.min(11519, Math.round(second * 60));
      await renderStill({ serveUrl, composition, browserInstance, frame, port: 3023 + worker,
        output: `out/studio/frames/${String(frame).padStart(5,'0')}.jpg`, imageFormat: 'jpeg', jpegQuality: 90, scale: .5 });
      process.stdout.write(`${second}s `);
    }
  }));
} finally { await browserInstance.close({ silent: true }); }
