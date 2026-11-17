import { AbsoluteFill, useCurrentFrame, useVideoConfig } from "remotion";
import { Light, ink, smooth } from "./Kit";

// A single travelling matte covers the actual edit. The light field keeps the
// global clock; there is no independent flare that flashes on and disappears.
export const TransitionVeil = ({ at }: { at: number }) => {
  const { fps, width: w, height: h } = useVideoConfig();
  const globalTime = useCurrentFrame() / fps,
    t = globalTime - at;
  if (t < -0.5 || t > 0.5) return null;
  const q = smooth(t, -0.5, 0.5),
    x = (-1.5 + 2.6 * q) * w,
    r = x + 1.4 * w,
    c = 0.06 * w;
  const d = `M${x} 0C${x - c} ${h * 0.3} ${x + c} ${h * 0.7} ${x} ${h}L${r} ${h}C${r + c} ${h * 0.7} ${r - c} ${h * 0.3} ${r} 0Z`;
  return (
    <AbsoluteFill
      style={{
        background: ink,
        clipPath: `path('${d}')`,
        pointerEvents: "none",
        zIndex: 40,
      }}
    >
      <Light t={globalTime} opacity={0.42} gather={0.18} />
    </AbsoluteFill>
  );
};
