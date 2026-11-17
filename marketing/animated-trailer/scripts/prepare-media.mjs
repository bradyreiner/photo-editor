import { copyFile, mkdir, readFile, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";
import { createHash } from "node:crypto";

const project = fileURLToPath(new URL("../", import.meta.url));
const repo = path.resolve(project, "../..");
const copies = [
  [
    "marketing/renders/nepal-review/4k/clips/IMG_1268-2-7.mp4",
    "IMG_1268-2-7.mp4",
    "Silent 1280px review excerpt from 3840x2160 source; not a Looksy export",
  ],
  [
    "marketing/renders/nepal-review/clips/MVI_5051-2.3-5.9.mp4",
    "MVI_5051-2.3-5.9.mp4",
    "Silent 1280px review excerpt from original Nepal camera footage; not a Looksy export",
  ],
  ...[
    "IMG_5103",
    "IMG_5111",
    "IMG_5125",
    "IMG_4856",
    "IMG_4744",
    "IMG_5053",
    "IMG_5124",
    "IMG_5077",
  ].map((name) => [
    `marketing/renders/nepal-review/detail/${name}.jpg`,
    `${name}.jpg`,
    "Embedded camera JPEG preview for illustrated library; not a Looksy edit or search result",
  ]),
  [
    "marketing/renders/nepal-review/4k/clips/IMG_1207-2-7.mp4",
    "IMG_1207-2-7.mp4",
    "Silent 1280px review excerpt from the 3840x2160 source; illustrative footage, not a Looksy export",
  ],
  [
    "marketing/renders/nepal-review/detail/IMG_5071.jpg",
    "IMG_5071.jpg",
    "Embedded camera JPEG preview; not a Looksy RAW render",
  ],
  [
    "marketing/renders/nepal-review/detail/IMG_5106.jpg",
    "IMG_5106.jpg",
    "Embedded camera JPEG preview; vector mask is art direction only",
  ],
  [
    "marketing/renders/nepal-review/detail/IMG_4870.jpg",
    "IMG_4870.jpg",
    "Embedded camera JPEG preview",
  ],
  [
    "marketing/renders/nepal-review/detail/IMG_4969.jpg",
    "IMG_4969.jpg",
    "Embedded camera JPEG preview",
  ],
  [
    "marketing/renders/nepal-review/4k/clips/IMG_1207-2-7.jpg",
    "IMG_1207-2-7.jpg",
    "Review poster from 3840x2160 Downloads/IMG_1207.mov; not an app export",
  ],
  [
    "marketing/renders/nepal-review/4k/clips/IMG_1268-2-7.jpg",
    "IMG_1268-2-7.jpg",
    "Review poster from 3840x2160 Downloads/IMG_1268.mov; not an app export",
  ],
  [
    "apps/desktop/assets/brand/looksy-wordmark-light.png",
    "looksy-wordmark-light.png",
    "Existing Looksy wordmark for light backgrounds",
  ],
  [
    "apps/desktop/assets/brand/looksy-wordmark-dark.png",
    "looksy-wordmark-dark.png",
    "Existing Looksy wordmark for dark backgrounds",
  ],
];

await mkdir(path.join(project, "public/media"), { recursive: true });
const manifest = [];
for (const [source, destination, provenance] of copies) {
  const input = path.join(repo, source);
  await copyFile(input, path.join(project, "public/media", destination));
  const hash = createHash("sha256")
    .update(await readFile(input))
    .digest("hex");
  manifest.push({
    source,
    destination: `public/media/${destination}`,
    sha256: hash,
    provenance,
  });
}
await writeFile(
  path.join(project, "media-manifest.json"),
  `${JSON.stringify(manifest, null, 2)}\n`,
);
console.log(
  `Prepared ${manifest.length} local review assets. Originals unchanged.`,
);
