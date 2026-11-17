import { Video } from "@remotion/media";
import { Sequence, useVideoConfig } from "remotion";
import { AppIcon, AppOpticalField } from "../app-kit/AppElements";
import {
  abs,
  asset,
  contour,
  enter,
  mix,
  Panel,
  Photo,
  Pose,
  T,
  windowAt,
  brandAccentAt,
} from "./system";
import { BoardArtwork, PosterArtwork } from "./Artwork";

export const MovingPicture = ({ t }: { t: number }) => {
  const { fps } = useVideoConfig();
  return (
    <>
      <Sequence
        from={56 * fps}
        durationInFrames={6 * fps}
        layout="none"
        name="Nepal walking shot"
      >
        <Video
          muted
          objectFit="cover"
          loop
          src={asset("IMG_1207-2-7.mp4")}
          style={{ width: "100%", height: "100%" }}
        />
      </Sequence>
      <Sequence
        from={62 * fps}
        durationInFrames={6 * fps}
        layout="none"
        name="Nepal street shot"
      >
        <Video
          muted
          objectFit="cover"
          loop
          src={asset("IMG_1268-2-7.mp4")}
          style={{
            width: "100%",
            height: "100%",
            filter: `blur(${mix(t, 63, 64, 0, 3) - mix(t, 64.5, 65.4, 0, 3)}px)`,
          }}
        />
      </Sequence>
      <Sequence
        from={68 * fps}
        durationInFrames={7 * fps}
        layout="none"
        name="Nepal sunrise shot"
      >
        <Video
          muted
          objectFit="cover"
          loop
          src={asset("MVI_5051-2.3-5.9.mp4")}
          style={{ width: "100%", height: "100%" }}
        />
      </Sequence>
      <div
        style={{
          ...abs,
          left: 35,
          top: 24,
          fontFamily: "Menlo,monospace",
          fontSize: 14,
          color: "white",
          textShadow: "0 1px 3px #0008",
        }}
      >
        PROGRAM / THE HIGH ROUTE
      </div>
      <div
        style={{
          ...abs,
          left: 0,
          right: 0,
          top: "37%",
          textAlign: "center",
          fontFamily: "Manrope",
          fontWeight: 800,
          fontSize: 68,
          letterSpacing: -3,
          color: "white",
          textShadow: "0 2px 8px #0005",
          translate: `${enter(t, 58.4, 60, -80, 0)}px 0`,
          opacity: mix(t, 58.4, 59.3) * (1 - mix(t, 62, 63)),
        }}
      >
        A LITTLE FURTHER.
      </div>
    </>
  );
};
const SoundWave = ({
  width,
  height = 40,
  shift = 0,
}: {
  width: number;
  height?: number;
  shift?: number;
}) => (
  <svg
    width={width}
    height={height}
    viewBox={`0 0 ${width} ${height}`}
    style={{ display: "block" }}
  >
    {Array.from({ length: Math.floor(width / 4) }, (_, i) => {
      const h =
        4 +
        (Math.sin(i * 0.93 + shift) ** 2 * 0.7 +
          Math.sin(i * 0.16 + shift) ** 2 * 0.3) *
          (height - 5);
      return (
        <path
          key={i}
          d={`M${i * 4 + 2} ${(height - h) / 2}V${(height + h) / 2}`}
          stroke="#7fb98d"
          strokeWidth={1.4}
        />
      );
    })}
  </svg>
);
export const TimelineScene = ({ t }: { t: number }) => {
  const show = windowAt(t, 55.5, 74);
  if (!show) return null;
  const curveProgress = mix(t, 64, 68);
  const curveX =
    3 * (1 - curveProgress) ** 2 * curveProgress * 110 +
    3 * (1 - curveProgress) * curveProgress ** 2 * 90 +
    curveProgress ** 3 * 300;
  const curveY =
    (1 - curveProgress) ** 3 * 115 +
    3 * (1 - curveProgress) ** 2 * curveProgress * 115 +
    3 * (1 - curveProgress) * curveProgress ** 2 * 15 +
    curveProgress ** 3 * 15;
  const unfolding = enter(t, 56, 58),
    playhead = mix(t, 58, 71.7, 135, 925),
    trim = mix(t, 59.5, 61, 0, 45);
  return (
    <>
      <Panel
        x={40}
        y={1100}
        w={1000}
        style={{
          opacity: show,
          transformOrigin: "50% 0",
          transform: `perspective(1600px) rotateX(${(1 - unfolding) * 58}deg)`,
          clipPath: `path('${contour(1000, 550, 1 - unfolding, t / 11, Math.max(0.1, unfolding))}')`,
          height: 550,
          padding: 22,
        }}
      >
        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: 15,
            fontSize: 20,
            marginBottom: 24,
          }}
        >
          <AppIcon name="timeline" size={22} />
          <span>THE HIGH ROUTE / MASTER</span>
          <span
            style={{
              marginLeft: "auto",
              fontFamily: "Menlo",
              fontSize: 18,
              color: T.textMuted,
            }}
          >
            3840 × 2160 · 24 fps
          </span>
        </div>
        <svg width={950} height={38}>
          {Array.from({ length: 25 }, (_, i) => (
            <g key={i} transform={`translate(${105 + i * 34} 0)`}>
              <path d={`M0 ${i % 4 === 0 ? 12 : 22}V34`} stroke="#585858" />
              {i % 4 === 0 && (
                <text y={10} fontSize={11} fill="#b5b5b5" fontFamily="Menlo">
                  00:{String(i).padStart(2, "0")}
                </text>
              )}
            </g>
          ))}
        </svg>
        {[0, 1, 2, 3].map((row) => (
          <div
            key={row}
            style={{
              height: 94,
              position: "relative",
              borderTop: "1px solid #393939",
              display: "flex",
              alignItems: "center",
            }}
          >
            <div style={{ width: 88, fontSize: 16, color: T.textMuted }}>
              {["V2 / TITLE", "V1 / VIDEO", "A1 / SOUND", "A2 / MUSIC"][row]}
            </div>
            {row === 0 && (
              <div
                style={{
                  ...abs,
                  left: 135,
                  top: 18,
                  width: 350 + mix(t, 60.5, 61.8, 0, 70),
                  height: 48,
                  background: "#3a4967",
                  padding: "14px 16px",
                  fontSize: 15,
                }}
              >
                A LITTLE FURTHER.{" "}
                <span style={{ float: "right" }}>
                  <AppIcon name="keyframe" size={16} />
                </span>
              </div>
            )}
            {row === 1 &&
              ["IMG_1207-2-7", "IMG_1268-2-7", "IMG_5071"].map((name, i) => (
                <div
                  key={name}
                  style={{
                    ...abs,
                    left: 110 + i * 275 + (i === 1 ? trim : 0),
                    top: 8,
                    width: 265 - (i === 0 ? trim : 0),
                    height: 76,
                    background: "#354546",
                    overflow: "hidden",
                    clipPath: `path('${contour(265, 76, 1 - unfolding, t / 11, 1)}')`,
                  }}
                >
                  <div style={{ display: "flex", width: "100%", height: 56 }}>
                    {[0, 1, 2, 3].map((j) => (
                      <div key={j} style={{ width: "25%", height: "100%" }}>
                        <Photo name={name} />
                      </div>
                    ))}
                  </div>
                  <div
                    style={{
                      padding: "3px 8px",
                      fontSize: 10,
                      background: "#304b50",
                    }}
                  >
                    {name.replace("-2-7", "")}.mov
                  </div>
                </div>
              ))}
            {row >= 2 && (
              <div
                style={{
                  ...abs,
                  left: 110,
                  top: 17,
                  width: 820,
                  height: 58,
                  background: row === 2 ? "#243d31" : "#283c30",
                  overflow: "hidden",
                }}
              >
                <SoundWave width={820} height={58} shift={row} />
                {row === 3 && (
                  <svg width={820} height={58} style={{ ...abs, inset: 0 }}>
                    <path
                      d={`M0 40L160 40L240 ${mix(t, 66.5, 68, 40, 22)}L650 22L820 45`}
                      stroke="#e2e2e2"
                      strokeWidth={1.4}
                      fill="none"
                    />
                    {[160, 240, 650].map((x, i) => (
                      <path
                        key={x}
                        d={`M${x} ${i === 0 ? 40 : 22}l4 4-4 4-4-4Z`}
                        fill="#f3f3f3"
                      />
                    ))}
                  </svg>
                )}
              </div>
            )}
          </div>
        ))}
        <div
          style={{
            ...abs,
            left: playhead,
            top: 85,
            width: 2,
            height: 425,
            background: T.brandPaper,
          }}
        >
          <div
            style={{
              width: 10,
              height: 10,
              background: T.brandPaper,
              rotate: "45deg",
              marginLeft: -4,
            }}
          />
        </div>
      </Panel>
      <div
        style={{
          ...abs,
          left: 80,
          top: 1685,
          display: "flex",
          gap: 28,
          fontSize: 25,
          color: T.textMuted,
          opacity: show,
        }}
      >
        <span>Tracks</span>
        <span>Motion & keyframes</span>
        <span>Effects</span>
        <span>Audio</span>
      </div>
      <div
        style={{
          ...abs,
          left: 80,
          top: 1740,
          fontSize: 20,
          color: T.textMuted,
          opacity: show,
        }}
      >
        4K output · Rec.709 · Sony S-Log2 / S-Log3 input
      </div>
      <Panel
        x={680}
        y={780}
        w={340}
        style={{
          opacity: windowAt(t, 64, 69),
          padding: 18,
          boxShadow: "0 12px 40px #0008",
        }}
      >
        <div style={{ fontSize: 19, marginBottom: 16 }}>
          Motion / easing curve
        </div>
        <svg width={300} height={130} viewBox="0 0 300 130">
          <path d="M0 115H300M0 5V115" stroke="#585858" fill="none" />
          <path
            d="M0 115C110 115 90 15 300 15"
            stroke="#f3f3f3"
            strokeWidth={2}
            fill="none"
          />
          <circle cx={curveX} cy={curveY} r={5} fill={T.brandBlue} />
        </svg>
        <div style={{ fontSize: 15, color: T.textMuted }}>
          Title position · Blur amount
        </div>
      </Panel>
    </>
  );
};

export const carouselPose = (t: number, pose: Pose): Pose => {
  if (t < 74 || t > 86) return pose;
  const zoomOut = mix(t, 82.5, 84.9);
  const pan = mix(t, 75.2, 77.2, 0, 1) + mix(t, 78, 80, 0, 1);
  return {
    ...pose,
    x: 72 - pan * (580 + 28) * (1 - zoomOut),
    w: 580 + (168 - 580) * zoomOut,
    h: 725 + (210 - 725) * zoomOut,
    y: 520 + 260 * zoomOut,
  };
};
export const CarouselScene = ({ t, pose }: { t: number; pose: Pose }) => {
  const show = windowAt(t, 72.5, 88.2);
  if (!show) return null;
  const accent = brandAccentAt(t);
  return (
    <>
      {[1, 2, 3, 4].map((index) => (
        <div
          key={index}
          style={{
            ...abs,
            left: pose.x + index * (pose.w + 28),
            top: pose.y,
            width: pose.w,
            height: pose.h,
            opacity: show,
            transformOrigin: "0 0",
            transform: `perspective(2400px) rotateY(${enter(t, 73 + index * 0.18, 74.5 + index * 0.18, -50, 0)}deg)`,
            overflow: "hidden",
            background: T.ink900,
          }}
        >
          <BoardArtwork index={index} accent={accent} moving={index === 2} />
        </div>
      ))}
      <div style={{ ...abs, left: 72, top: 1430, width: 936, opacity: show }}>
        <div style={{ fontSize: 27, marginBottom: 22 }}>
          One design. Across every board.
        </div>
        <div
          style={{ display: "flex", gap: 18, fontSize: 18, color: T.textMuted }}
        >
          <span>Photo + video</span>
          <span>Reusable layouts</span>
          <span>Linked styles</span>
        </div>
        <div
          style={{
            marginTop: 32,
            display: "flex",
            gap: 13,
            alignItems: "center",
          }}
        >
          {[T.brandRed, T.brandGreen, T.brandBlue].map((c, i) => (
            <div
              key={c}
              style={{
                width: 33,
                height: 33,
                background: c,
                borderRadius: 3,
                border:
                  t > 80 && i === 2
                    ? "2px solid white"
                    : "2px solid transparent",
                scale: 1,
              }}
            />
          ))}
          <span style={{ fontSize: 18, color: T.textMuted, marginLeft: 14 }}>
            Brand kit · Apply to all boards
          </span>
        </div>
        <div style={{ marginTop: 36, display: "flex", gap: 12 }}>
          {[0, 1, 2, 3, 4].map((i) => (
            <div
              key={i}
              style={{
                width: 82,
                height: 104,
                position: "relative",
                overflow: "hidden",
                border: `2px solid ${i === Math.floor(mix(t, 75, 81, 0, 4)) ? "#f3f3f3" : "transparent"}`,
              }}
            >
              <BoardArtwork index={i} accent={accent} />
            </div>
          ))}
        </div>
      </div>
      <div
        style={{
          ...abs,
          left: 72,
          top: 1710,
          opacity: show,
          fontSize: 21,
          color: T.textMuted,
        }}
      >
        Align · Repeat · Reorder · Preview · Export panels
      </div>
    </>
  );
};

export const DeliveryScene = ({ t }: { t: number }) => {
  const show = windowAt(t, 86.7, 95);
  if (!show) return null;
  const progress = mix(t, 89, 92.4);
  return (
    <>
      <div
        style={{
          ...abs,
          left: 650,
          top: 550,
          width: 355,
          height: 532.5,
          opacity: show,
          overflow: "hidden",
          rotate: "3deg",
        }}
      >
        <Photo name="IMG_5106" />
        <PosterArtwork t={48} />
      </div>
      <div
        style={{
          ...abs,
          left: 72,
          top: 1150,
          width: 450,
          height: 254,
          opacity: show,
          overflow: "hidden",
        }}
      >
        <Photo name="IMG_1207-2-7" />
        <div
          style={{
            ...abs,
            left: 20,
            bottom: 20,
            fontSize: 30,
            fontWeight: 700,
          }}
        >
          THE HIGH ROUTE
        </div>
      </div>
      <div
        style={{
          ...abs,
          left: 570,
          top: 1190,
          display: "flex",
          gap: 8,
          opacity: show,
        }}
      >
        {[0, 1, 2, 3, 4].map((i) => (
          <div
            key={i}
            style={{
              position: "relative",
              width: 80,
              height: 100,
              overflow: "hidden",
            }}
          >
            <BoardArtwork index={i} />
          </div>
        ))}
      </div>
      <Panel x={72} y={1470} w={936} style={{ opacity: show, padding: 24 }}>
        {[
          ["Photo", "4000 × 6000 · sRGB"],
          ["Design", "Custom dimensions · PNG"],
          ["Film", "3840 × 2160 · Rec.709"],
          ["Carousel", "Selected panels · 1080 × 1350"],
        ].map(([label, detail], i) => (
          <div
            key={label}
            style={{
              height: 47,
              display: "flex",
              alignItems: "center",
              gap: 16,
              fontSize: 20,
              borderBottom: i < 3 ? "1px solid #393939" : "none",
            }}
          >
            <span style={{ width: 125 }}>{label}</span>
            <span style={{ color: T.textMuted, fontSize: 18 }}>{detail}</span>
            <span style={{ marginLeft: "auto" }}>
              {progress > (i + 1) / 4 && (
                <AppIcon name="check" size={22} color={T.opticalLight} />
              )}
            </span>
          </div>
        ))}
        <div style={{ height: 4, marginTop: 20 }}>
          <AppOpticalField
            kind={2}
            width={888}
            height={4}
            progress={progress}
            active={progress < 1}
          />
        </div>
      </Panel>
    </>
  );
};
