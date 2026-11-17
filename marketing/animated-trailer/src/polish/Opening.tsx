import { Video } from "@remotion/media";
import { staticFile, useCurrentFrame, useVideoConfig } from "remotion";
import {
  AppIcon,
  AppInspectorSlider,
  AppWordmark,
} from "../app-kit/AppElements";
import {
  A,
  C,
  Copy,
  Grain,
  land,
  lerp,
  mono,
  p,
  Plate,
  smoothPath,
  aperture,
} from "../launch/design";
import { CampaignArtwork } from "./Artwork";
import { WeddingImage } from "./Shared";

// RGB wavefronts physically resolve into the editor's tone curve. The same
// portrait then becomes a layered canvas before the camera reaches the timeline.
export const CampaignOpening = () => {
  const { fps } = useVideoConfig(),
    t = useCurrentFrame() / fps;
  const enter = p(t, 2.4, 3.5),
    design = p(t, 5.25, 6.1),
    timeline = p(t, 8, 8.9),
    gather = p(t, 10.5, 12);
  const titleOut = 1 - p(t, 2.15, 2.65),
    mainOut = 1;
  const sourceToDesign = p(t, 5.4, 6.5),
    layerSpread = 0;
  const waveMorph = p(t, 2.2, 3.7),
    exposure = p(t, 3.7, 4.8),
    draw = p(t, 0, 0.85);
  const phase = Math.min(t, 2.2) * 1.45;
  const channels = [C.brandRed, C.brandGreen, C.brandBlue];
  return (
    <Plate>
      <div
        style={{
          ...A,
          left: 80,
          top: 250,
          opacity: titleOut,
        }}
      >
        <div
          style={{
            fontSize: 33,
            letterSpacing: 3,
            color: C.textMuted,
            marginBottom: 30,
          }}
        >
          INTRODUCING
        </div>
        <div style={{ height: 192 }} />
        <div
          style={{
            fontSize: 70,
            fontWeight: 500,
            lineHeight: 1.05,
            letterSpacing: -2.5,
            marginTop: 48,
          }}
        >
          Professional photo
          <br />& video editing.
        </div>
      </div>
      <div
        style={{
          ...A,
          left: lerp(80, 104, enter) - 1040 * gather,
          top: lerp(318, 455, enter),
          zIndex: 5,
          opacity: mainOut,
        }}
      >
        <AppWordmark width={lerp(760, 158, enter)} />
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 140,
          opacity: enter * (1 - p(t, 10.3, 10.65)),
        }}
      >
        <Copy t={t} start={2.65} size={96}>
          <div>One workspace.</div>
          <div>Every part of the edit.</div>
        </Copy>
      </div>
      <div
        style={{
          ...A,
          left: 80 - 1040 * gather,
          top: 440,
          width: 920,
          height: 1240,
          opacity: enter * mainOut,
          clipPath: `path('${aperture(920, 1240, enter, t)}')`,
          perspective: 1900,
        }}
      >
        <div
          style={{
            ...A,
            inset: 0,
            border: `1px solid ${C.lineSoft}`,
            background: C.ink950,
            transform: `rotateY(${Math.sin((design + timeline) * Math.PI) * -3}deg)`,
          }}
        >
          <div
            style={{
              height: 70,
              display: "flex",
              alignItems: "center",
              justifyContent: "space-between",
              padding: "0 24px",
              borderBottom: `1px solid ${C.lineSoft}`,
              fontSize: 23,
            }}
          >
            <div style={{ width: 158 }} />
            <div style={{ display: "flex", gap: 30 }}>
              {["Library", "Edit", "Boards", "Timeline"].map((label, i) => (
                <span
                  key={label}
                  style={{
                    color: (timeline > 0.5 ? i === 3 : i === 1)
                      ? C.brandPaper
                      : C.textMuted,
                  }}
                >
                  {label}
                </span>
              ))}
            </div>
          </div>
          <div
            style={{
              ...A,
              left: 0,
              top: 70,
              width: 920,
              height: 1150,
              overflow: "hidden",
            }}
          >
            <div
              style={{
                ...A,
                inset: 0,
                opacity: 1 - timeline,
                translate: `${-1040 * timeline}px 0`,
              }}
            >
              {(["base", "type", "subject", "graphics"] as const).map(
                (plane, i) => (
                  <div
                    key={plane}
                    style={{
                      ...A,
                      inset: 0,
                      translate: `${(i - 1.5) * 65 * layerSpread}px ${(i - 1.5) * 55 * layerSpread}px`,
                      transform: `rotateY(${(i - 1.5) * 6 * layerSpread}deg)`,
                      opacity: plane === "base" ? 1 : design,
                      filter:
                        plane === "base"
                          ? `brightness(${1 + 0.08 * exposure})`
                          : "none",
                    }}
                  >
                    <CampaignArtwork
                      t={Math.max(0, (t - 5.25) * 2)}
                      sourceToDesign={sourceToDesign}
                      plane={plane}
                      paintOverride={0}
                    />
                  </div>
                ),
              )}
              <div
                style={{
                  ...A,
                  left: 610,
                  top: 620,
                  width: 270,
                  height: 450,
                  background: "#181818ef",
                  border: `1px solid ${C.lineSoft}`,
                  opacity: 1 - p(t, 5.1, 5.55),
                }}
              >
                <div style={{ padding: 22, fontSize: 25 }}>
                  Tone curve · RGB
                </div>
                <svg width="220" height="240" style={{ marginLeft: 25 }}>
                  <path
                    d="M0 0V240H220 M55 0V240 M110 0V240 M165 0V240 M0 60H220 M0 120H220 M0 180H220"
                    fill="none"
                    stroke={C.lineSoft}
                  />
                </svg>
                <div style={{ marginLeft: 10 }}>
                  <AppInspectorSlider
                    label="Exposure"
                    width={250}
                    value={0.35 * exposure}
                    valueText={`+${(0.35 * exposure).toFixed(2)}`}
                    from={-5}
                    to={5}
                    scale={1}
                    showCursor={t > 3.7 && t < 4.8}
                  />
                </div>
              </div>
              <div
                style={{
                  ...A,
                  left: 36,
                  top: 870,
                  width: 500,
                  background: "#181818ee",
                  padding: 25,
                  opacity: design,
                  fontSize: 27,
                  lineHeight: 1.65,
                  border: `1px solid ${C.lineSoft}`,
                }}
              >
                <div style={{ display: "flex", gap: 16, alignItems: "center" }}>
                  <AppIcon name="layers" size={28} />
                  Editable layers
                </div>
                <div style={{ color: C.textMuted }}>T / FORM.</div>
                <div>Subject / Live mask</div>
                <div style={{ color: C.textMuted }}>Original photograph</div>
              </div>
            </div>
            <div
              style={{
                ...A,
                inset: 0,
                translate: `${1040 * (1 - timeline)}px 0`,
                background: C.ink950,
              }}
            >
              <div
                style={{
                  ...A,
                  left: 25,
                  top: 100,
                  width: 870,
                  height: 490,
                  overflow: "hidden",
                }}
              >
                <Video
                  src={staticFile("media/range/skate.mp4")}
                  playbackRate={0.5}
                  muted
                  objectFit="cover"
                  style={{ width: "100%", height: "100%" }}
                />
              </div>
              <div
                style={{
                  ...A,
                  left: 25,
                  top: 630,
                  fontFamily: mono,
                  fontSize: 23,
                  color: C.textMuted,
                }}
              >
                SEQUENCE / 3840 × 2160 / Rec.709
              </div>
              {[0, 1, 2, 3].map((i) => (
                <div
                  key={i}
                  style={{
                    ...A,
                    left: 25,
                    top: 710 + i * 83,
                    width: 870,
                    height: 70,
                    background: i < 2 ? "#394658" : "#25322b",
                    border: `1px solid ${C.lineStrong}`,
                    translate: `${land(t, 8.4 + i * 0.08, 9.15 + i * 0.08, 180, 0)}px 0`,
                  }}
                >
                  <div style={{ padding: "22px 25px", fontSize: 23 }}>
                    {
                      [
                        "TITLE / MOTION",
                        "PICTURE / SKATE",
                        "AMBIENCE",
                        "MUSIC",
                      ][i]
                    }
                  </div>
                </div>
              ))}
              <div
                style={{
                  ...A,
                  left: lerp(180, 750, p(t, 8.9, 10.5)),
                  top: 690,
                  height: 365,
                  width: 2,
                  background: C.brandPaper,
                }}
              />
            </div>
          </div>
        </div>
      </div>
      <svg
        width="1080"
        height="1920"
        style={{
          ...A,
          inset: 0,
          opacity: (1 - p(t, 4.9, 5.4)) * (1 - gather),
          pointerEvents: "none",
        }}
      >
        <defs>
          <filter id="intro-caustic">
            <feGaussianBlur stdDeviation="6" />
          </filter>
        </defs>
        {channels.flatMap((color, ch) =>
          Array.from({ length: 5 }, (_, ray) => {
            const pts = Array.from({ length: 81 }, (_, i) => {
              const u = i / 80,
                offset = ch - 1,
                r = ray - 2;
              const wave =
                0.34 * Math.sin(u * 9 + phase) +
                0.2 * Math.sin(u * 14 - phase + r * 0.12);
              return {
                x: lerp(-80 + 1240 * u, 715 + 220 * u, waveMorph),
                y: lerp(
                  1000 +
                    310 * wave +
                    offset * 19 * Math.cos(u * 7 + phase) +
                    r * 24 * Math.sin(u * 6 - phase),
                  1442 - 240 * (u * 0.65 + Math.pow(u, 0.7) * 0.35),
                  waveMorph,
                ),
              };
            });
            const d = smoothPath(pts);
            return (
              <g key={`${color}-${ray}`} style={{ mixBlendMode: "screen" }}>
                <path
                  d={d}
                  stroke={color}
                  strokeWidth={9}
                  opacity={0.12 * (1 - waveMorph)}
                  filter="url(#intro-caustic)"
                  fill="none"
                />
                <path
                  d={d}
                  pathLength="1"
                  stroke={color}
                  strokeWidth={lerp(1.8, 1.2, waveMorph)}
                  strokeDasharray="1"
                  strokeDashoffset={1 - draw}
                  fill="none"
                  opacity={0.7}
                />
              </g>
            );
          }),
        )}
      </svg>
      <div
        style={{
          ...A,
          inset: 0,
          opacity: 1,
          translate: `${1040 * (1 - gather)}px 0`,
        }}
      >
        <div style={{ ...A, left: 80, top: 150 }}>
          <Copy t={t} start={10.8} size={102}>
            <div>For every</div>
            <div>kind of shoot.</div>
          </Copy>
        </div>
        <div
          style={{
            ...A,
            left: 80,
            top: 595,
            fontSize: 29,
            color: C.textMuted,
            letterSpacing: 1,
          }}
        >
          IMPORT → CULL → DEVELOP → DELIVER
        </div>
        {[
          "range/product",
          "range/portrait",
          "range/interior",
          "range/food",
          "wedding-bride-01",
          "wedding-bride-02",
        ].map((name, i) => (
          <div
            key={name}
            style={{
              ...A,
              left: 80 + (i % 3) * 318,
              top: 735 + Math.floor(i / 3) * 485,
              width: 285,
              height: i < 3 ? 428 : 360,
              overflow: "hidden",
            }}
          >
            <WeddingImage name={name} />
          </div>
        ))}
      </div>
      <Grain />
    </Plate>
  );
};
