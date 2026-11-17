import { useCurrentFrame, useVideoConfig } from "remotion";
import { AppInspectorSlider } from "../app-kit/AppElements";
import { A, C, Copy, Grain, land, Meta, p, Photo, Plate } from "./design";

export const ColorDetailShot = ({ t }: { t: number }) => {
  const color = p(t, 0.35, 1.4),
    detail = p(t, 2.2, 3.4),
    inspect = p(t, 1.65, 2.15);
  return (
    <Plate>
      <div style={{ ...A, left: 80, top: 130, opacity: 1 - p(t, 3.6, 4) }}>
        <Copy t={t} start={0} size={114}>
          <div>Color. Detail.</div>
        </Copy>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 390,
          width: 920,
          height: 870,
          overflow: "hidden",
        }}
      >
        <div
          style={{
            ...A,
            inset: 0,
            transformOrigin: "48% 60%",
            scale: 1 + inspect * 0.55,
          }}
        >
          <Photo />
          <Photo
            name="IMG_5106-color"
            style={{ ...A, inset: 0, opacity: color }}
          />
          <Photo
            name="IMG_5106-developed"
            style={{ ...A, inset: 0, opacity: detail }}
          />
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1280,
          width: 920,
          padding: "24px 22px",
          boxSizing: "border-box",
          background: C.ink900,
          opacity: 1 - p(t, 3.6, 4),
        }}
      >
        {inspect < 0.5 ? (
          <>
            <div
              style={{
                fontSize: 35,
                margin: "0 12px 20px",
                display: "flex",
                alignItems: "center",
                gap: 20,
              }}
            >
              Color Mix{" "}
              <span
                style={{
                  width: 28,
                  height: 28,
                  borderRadius: "50%",
                  background: C.brandBlue,
                  display: "inline-block",
                }}
              />
              Blue
            </div>
            <AppInspectorSlider
              label="Hue"
              value={-5 * color}
              valueText={`${Math.round(-5 * color)}%`}
              scale={2.8}
              width={313}
            />
            <AppInspectorSlider
              label="Saturation"
              value={18 * color}
              valueText={`+${Math.round(18 * color)}%`}
              scale={2.8}
              width={313}
              press={land(t, 0.2, 0.35) * (1 - p(t, 1.4, 1.5))}
              showCursor={t > 0.35 && t < 1.5}
            />
            <AppInspectorSlider
              label="Luminance"
              value={-8 * color}
              valueText={`${Math.round(-8 * color)}%`}
              scale={2.8}
              width={313}
            />
          </>
        ) : (
          <>
            <div style={{ fontSize: 35, margin: "0 12px 20px" }}>
              Sharpening / radius 0.8 px
            </div>
            <AppInspectorSlider
              label="Amount"
              value={40 * detail}
              valueText={`+${Math.round(40 * detail)}%`}
              from={0}
              to={100}
              scale={2.8}
              width={313}
              press={land(t, 2.05, 2.2) * (1 - p(t, 3.4, 3.5))}
              showCursor={t > 2.2 && t < 3.5}
            />
            <div
              style={{
                fontSize: 29,
                margin: "12px 12px 4px",
                color: C.textMuted,
              }}
            >
              Noise Reduction
            </div>
            <AppInspectorSlider
              label="Luminance"
              value={12 * detail}
              valueText={`+${Math.round(12 * detail)}%`}
              from={0}
              to={100}
              scale={2.8}
              width={313}
            />
            <AppInspectorSlider
              label="Color"
              value={8 * detail}
              valueText={`+${Math.round(8 * detail)}%`}
              from={0}
              to={100}
              scale={2.8}
              width={313}
            />
          </>
        )}
      </div>
      <Meta
        style={{
          ...A,
          left: 80,
          top: 1815,
          fontSize: 27,
          opacity: 1 - p(t, 3.6, 4),
        }}
      >
        COLOR RANGES / LOUPE / SHARPENING / NOISE REDUCTION
      </Meta>
      <Grain />
    </Plate>
  );
};
export const ColorDetailScene = () => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <ColorDetailShot t={f / fps} />;
};
