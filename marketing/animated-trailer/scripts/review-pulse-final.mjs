import { bundle } from '@remotion/bundler';
import { openBrowser, selectComposition, renderStill } from '@remotion/renderer';
import { mkdir } from 'node:fs/promises';
import path from 'node:path';

const serveUrl = await bundle({ entryPoint: path.resolve('src/index.ts') });
const browserInstance = await openBrowser('chrome');
const composition = await selectComposition({ serveUrl, id: 'Looksy-Launch-Motion-v6', browserInstance, port: 3023 });
await mkdir('out/pulse/frames', { recursive: true });
const seconds = [19.2,24.8,25,25.5,26,44.8,47.1,50,76.5,84,104,104.4,105,125.98];
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
