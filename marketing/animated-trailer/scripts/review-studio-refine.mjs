import { bundle } from '@remotion/bundler';
import { openBrowser, selectComposition, renderStill } from '@remotion/renderer';
import { mkdir } from 'node:fs/promises';
import path from 'node:path';

const serveUrl = await bundle({ entryPoint: path.resolve('src/index.ts') });
const browserInstance = await openBrowser('chrome');
const composition = await selectComposition({ serveUrl, id: 'Looksy-Studio-Film', browserInstance, port: 3023 });
await mkdir('out/studio/frames', { recursive: true });
const seconds=[34.8,36.2,37.3,38.2,39.5,65.7,68.7,71.4,72.8,74.7,76.6,81,115.4,117.2,120.5,123,137.7,143.2,146.8,157,183,188];
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
