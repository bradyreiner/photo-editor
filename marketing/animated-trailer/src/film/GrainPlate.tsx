import { useLayoutEffect, useRef } from "react";
// A cached, deterministic monochrome grain plate. Texture is generated once;
// the editable effect amount is composited by the film, not recomputed noise.
export const GrainPlate = ({ amount }: { amount: number }) => {
  const canvas = useRef<HTMLCanvasElement>(null);
  useLayoutEffect(() => {
    const ctx = canvas.current?.getContext("2d");
    if (!ctx) return;
    const image = ctx.createImageData(400, 600);
    let seed = 47;
    const rand = () => {
      seed ^= seed << 13;
      seed ^= seed >>> 17;
      seed ^= seed << 5;
      return (seed >>> 0) / 4294967296;
    };
    for (let i = 0; i < image.data.length; i += 4) {
      const noise = (rand() + rand() + rand() + rand() - 2) * 29;
      const v = Math.max(0, Math.min(255, 128 + noise));
      image.data[i] = v;
      image.data[i + 1] = v;
      image.data[i + 2] = v;
      image.data[i + 3] = 255;
    }
    ctx.putImageData(image, 0, 0);
  }, []);
  return (
    <canvas
      ref={canvas}
      width={400}
      height={600}
      style={{
        position: "absolute",
        inset: 0,
        width: "100%",
        height: "100%",
        opacity: amount,
        mixBlendMode: "soft-light",
        pointerEvents: "none",
      }}
    />
  );
};
