import { spawn } from "node:child_process";
import fs from "node:fs/promises";
const formats = process.argv.slice(2).length
  ? process.argv.slice(2)
  : ["landscape", "portrait"];
const run = (args) =>
  new Promise((resolve, reject) => {
    const p = spawn("ffmpeg", args, { stdio: ["ignore", "ignore", "pipe"] });
    let output = "";
    p.stderr.on("data", (d) => (output += d.toString()));
    p.on("close", (code) =>
      code === 0 ? resolve(output) : reject(new Error(output)),
    );
  });
for (const format of formats) {
  const prefix = `out/creative-launch/${format}`;
  await run([
    "-v",
    "warning",
    "-y",
    "-i",
    `${prefix}-picture.mp4`,
    "-i",
    `${prefix}-photo-finish.mp4`,
    "-i",
    `${prefix}-type-reveal.mp4`,
    "-i",
    `${prefix}-keyframes.mp4`,
    "-i",
    "public/audio/creative-launch-master.wav",
    "-filter_complex",
    "[0:v]trim=end_frame=1200,setpts=PTS-STARTPTS[a];[1:v]setpts=PTS-STARTPTS[b];[0:v]trim=start_frame=1320:end_frame=2640,setpts=PTS-STARTPTS[c];[2:v]setpts=PTS-STARTPTS[d];[0:v]trim=start_frame=2700:end_frame=4260,setpts=PTS-STARTPTS[e];[3:v]setpts=PTS-STARTPTS[f];[0:v]trim=start_frame=4440,setpts=PTS-STARTPTS[g];[a][b][c][d][e][f][g]concat=n=7:v=1:a=0,scale=in_color_matrix=bt601:out_color_matrix=bt709:in_range=tv:out_range=tv[v]",
    "-map",
    "[v]",
    "-map",
    "4:a:0",
    "-c:v",
    "libx264",
    "-preset",
    "fast",
    "-crf",
    "17",
    "-pix_fmt",
    "yuv420p",
    "-color_primaries",
    "bt709",
    "-color_trc",
    "iec61966-2-1",
    "-colorspace",
    "bt709",
    "-color_range",
    "tv",
    "-c:a",
    "aac",
    "-b:a",
    "256k",
    "-ar",
    "48000",
    "-movflags",
    "+faststart",
    "-t",
    "216",
    `${prefix}.mp4`,
  ]);
  console.log(`Finished ${prefix}.mp4`);
}
await fs.writeFile(
  "out/creative-launch/finish-receipt.json",
  JSON.stringify(
    {
      corrections: [
        "RAW before/after transition at 00:40–00:44 ends on the finished edit; comparison labels and divider fade before the final hero frame.",
        "Clean opaque native-control aperture at 01:28–01:30 removes poster crossfade ghosting.",
        "Keyframe marker and knots follow the exact cubic curve at 02:22–02:28.",
      ],
      frame_ranges: [
        [1200, 1319],
        [2640, 2699],
        [4260, 4439],
      ],
      source_frames: 6480,
      output_frames: 6480,
      duration: 216,
      final_encode:
        "H264 CRF 17 / fast / yuv420p; AAC 256kbps stereo 48kHz; faststart",
      color:
        "Rec.709 primaries and matrix, original sRGB transfer retained. Matrix converted from Remotion v4 default BT.601 encoding; explicitly tagged, not merely relabeled.",
      music: "public/audio/creative-launch-master.wav",
    },
    null,
    2,
  ) + "\n",
);
