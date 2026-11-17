import { smooth, mix } from "./Kit";

export const photoPhases = [0, 4, 10, 15, 20, 27, 38, 45, 50];
export const canvasPhases = [0, 6, 14, 19, 31, 41, 50, 58, 68];
export const phaseAt = (t: number, edges: number[]) => {
  let phase = 0;
  while (phase < edges.length - 2 && t >= edges[phase + 1]) phase++;
  return {
    phase,
    s: t - edges[phase],
    start: edges[phase],
    duration: edges[phase + 1] - edges[phase],
  };
};
export const curveState = (s: number) => {
  const shadow = smooth(s, 0.9, 2.6),
    highlight = smooth(s, 3.5, 5.4);
  return { shadow, highlight, progress: (shadow + highlight) / 2 };
};
export const brushState = (s: number) => {
  const u = smooth(s, 1.4, 4.3);
  return { u, x: mix(-120, 1100, u), y: mix(955, 400, u), size: 180 };
};
export const mixerState = (s: number) => {
  const u = smooth(s, 0.8, 2) * (1 - smooth(s, 4.5, 5.7));
  return { musicDb: -12 * u, locationGain: u };
};
export const filmShots = [
  {
    id: "arrival",
    source: "creative-polish-v2/arrival-12.mp4",
    offset: 1,
    duration: 3.7,
    thumb: "studio/arrival-first.jpg",
    label: "ARRIVAL",
  },
  {
    id: "ridge",
    source: "MVI_5051-2.3-5.9.mp4",
    offset: 0,
    duration: 3.2,
    thumb: "studio/ridge-thumb.jpg",
    label: "RIDGELINE",
  },
  {
    id: "city",
    source: "creative-polish-v2/city-10.mp4",
    offset: 0,
    duration: 2.7,
    thumb: "studio/city-delivery.jpg",
    label: "KATHMANDU",
  },
  {
    id: "night",
    source: "refine/night.mp4",
    offset: 2.6,
    duration: 2.4,
    thumb: "studio/night-thumb.jpg",
    label: "AFTER DARK",
  },
];
export const editModel = (trim: number) => {
  let at = 0;
  return filmShots.map((shot, i) => {
    const duration = shot.duration - (i === 2 ? 0.7 * trim : 0);
    const clip = { ...shot, start: at, end: at + duration, duration };
    at += duration;
    return clip;
  });
};
export const sourceAt = (clock: number, trim = 0) => {
  const edit = editModel(trim);
  const clip = edit.find((c) => clock < c.end) || edit[edit.length - 1];
  return {
    ...clip,
    sourceTime:
      clip.offset +
      Math.max(0, Math.min(clip.duration - 0.04, clock - clip.start)),
  };
};
// Monotonic camera/title easing. The graph, marker, readouts and artwork use
// this same value; the curve is sampled from it rather than drawn separately.
export const motionValue = (x: number) => {
  const u = Math.max(0, Math.min(1, x));
  return u * u * u * (10 + u * (-15 + 6 * u));
};

export const detailZoom = (s: number) =>
  smooth(s, 0.2, 2.2) * (1 - smooth(s, 10, 10.8));
