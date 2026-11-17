import { Video } from "@remotion/media";
import {
  AbsoluteFill,
  Easing,
  interpolate,
  Sequence,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import {
  Brand,
  Curve,
  Image,
  LightContours,
  MaskArt,
  palette,
} from "./StyleFrames";

const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
const settle = Easing.bezier(0.12, 0.88, 0.24, 1);
const move = Easing.bezier(0.72, 0, 0.2, 1);
const k = (time: number, start: number, end: number, a = 0, b = 1) =>
  interpolate(time, [start, end], [a, b], { ...clamp, easing: move });

const OpeningCopy = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = frame / fps;
  return (
    <>
      <Brand />
      <div
        style={{
          position: "absolute",
          left: 80,
          top: 232,
          fontSize: 113,
          lineHeight: 1.04,
          letterSpacing: -7,
          fontWeight: 800,
          clipPath: `inset(0 0 ${k(t, 0.05, 0.65, 100, 0)}% 0)`,
          translate: `0px ${k(t, 0.05, 0.65, 45, 0) - k(t, 3.1, 3.65, 0, 340)}px`,
          opacity: 1 - k(t, 3.1, 3.6),
        }}
      >
        Introducing
        <br />
        Looksy<span style={{ color: palette.blue }}>.</span>
      </div>
      <div
        style={{
          position: "absolute",
          left: 88,
          top: 510,
          fontSize: 44,
          opacity: k(t, 0.45, 0.85) * (1 - k(t, 3.1, 3.45)),
        }}
      >
        Photo, video and design.
      </div>
      <div
        style={{
          position: "absolute",
          left: 80,
          top: 232,
          fontSize: 108,
          lineHeight: 1.05,
          letterSpacing: -6,
          fontWeight: 800,
          opacity: k(t, 3.2, 3.55) * (1 - k(t, 6.45, 6.85)),
          clipPath: `inset(${k(t, 3.2, 3.9, 100, 0)}% 0 0 0)`,
          translate: `0px ${k(t, 3.2, 3.9, 70, 0)}px`,
        }}
      >
        One workspace.
        <br />
        From source
        <br />
        to finish<span style={{ color: palette.blue }}>.</span>
      </div>
      <div
        style={{
          position: "absolute",
          left: 80,
          top: 232,
          fontSize: 113,
          lineHeight: 1.04,
          letterSpacing: -7,
          fontWeight: 800,
          opacity: k(t, 6.7, 7.05),
          clipPath: `inset(${k(t, 6.65, 7.3, 100, 0)}% 0 0 0)`,
          translate: `0px ${k(t, 6.65, 7.3, 70, 0)}px`,
        }}
      >
        Keep
        <br />
        the detail<span style={{ color: palette.blue }}>.</span>
      </div>
      <div
        style={{
          position: "absolute",
          left: 88,
          top: 510,
          fontSize: 44,
          opacity: k(t, 7.1, 7.5),
        }}
      >
        RAW editing. Native resolution.
      </div>
    </>
  );
};

const OutputWorld = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = frame / fps;
  const reveal = interpolate(t, [0.35, 1.3], [0, 1], {
    ...clamp,
    easing: settle,
  });
  const focus = k(t, 3.6, 4.6);
  const raw = k(t, 6.3, 7.6);
  return (
    <div style={{ position: "absolute", inset: 0, perspective: 2600 }}>
      <LightContours
        style={{
          top: 450,
          left: 170,
          rotate: `${-22 + t * 0.15}deg`,
          opacity: 0.6,
        }}
      />
      <div
        style={{
          position: "absolute",
          left: 72 - focus * 22 - raw * 800,
          top: 705 + focus * 120 + (1 - reveal) * 300,
          width: 515,
          height: 773,
          transform: `rotateY(${-8 + focus * 8}deg) rotateZ(${-6 + focus * 3}deg) scale(${0.93 + reveal * 0.07})`,
          boxShadow: "15px 22px 0 #4465FF",
          opacity: 1 - raw,
        }}
      >
        <MaskArt />
      </div>
      <div
        style={{
          position: "absolute",
          left: 555 - focus * 18 - raw * 300,
          top: 875 - focus * 55 + (1 - reveal) * 450,
          width: 610,
          height: 343,
          transform: `rotateY(${-8 + focus * 8}deg) rotateZ(${5 - focus * 5}deg)`,
          boxShadow: "14px 20px 0 #101112",
          opacity: 1 - raw,
        }}
      >
        <Video
          name="Nepal finished-video example"
          src={staticFile("media/IMG_1207-2-7.mp4")}
          muted
          loop
          objectFit="cover"
          style={{ width: "100%", height: "100%" }}
        />
        <div
          style={{
            position: "absolute",
            left: 24,
            bottom: 21,
            color: "white",
            fontSize: 23,
          }}
        >
          NEPAL / THE JOURNEY
        </div>
      </div>
      <div
        style={{
          position: "absolute",
          left: 520 - focus * 25 + raw * 700,
          top: 1330 - focus * 80 + (1 - reveal) * 560,
          display: "flex",
          gap: 14,
          transform: `rotateZ(${4 - focus * 4}deg)`,
          opacity: 1 - raw,
        }}
      >
        <div
          style={{
            width: 175,
            height: 260,
            background: palette.blue,
            color: "white",
            padding: "25px 17px",
            boxSizing: "border-box",
            fontSize: 38,
            fontWeight: 800,
            lineHeight: 1,
          }}
        >
          NE
          <br />
          PAL
          <span
            style={{
              display: "block",
              marginTop: 120,
              fontSize: 12,
              fontWeight: 500,
            }}
          >
            01 / THE JOURNEY
          </span>
        </div>
        <div style={{ width: 175, height: 260 }}>
          <Image name="IMG_4870.jpg" />
        </div>
        <div style={{ width: 175, height: 260 }}>
          <Image name="IMG_4969.jpg" />
        </div>
      </div>
      <svg
        viewBox="0 0 1080 1920"
        width="1080"
        height="1920"
        style={{ position: "absolute", inset: 0, opacity: 1 - raw }}
        fill="none"
      >
        <path
          d="M-100 1530C260 1695 219 1218 520 1218S945 1680 1170 1483"
          stroke={palette.red}
          strokeWidth="30"
          pathLength="1"
          strokeDasharray="1"
          strokeDashoffset={1 - k(t, 0.75, 1.8)}
        />
        <path
          d="M-100 1568C280 1740 233 1250 526 1250S945 1715 1170 1520"
          stroke={palette.green}
          strokeWidth="15"
          pathLength="1"
          strokeDasharray="1"
          strokeDashoffset={1 - k(t, 0.9, 1.95)}
        />
      </svg>
      <div
        style={{
          position: "absolute",
          left: -40 + (1 - raw) * 1200,
          top: 720,
          width: 1160,
          height: 770,
          transform: `perspective(2000px) rotateY(${-8 - (1 - raw) * 28}deg) rotateZ(${-7 + (1 - raw) * 7}deg)`,
          boxShadow: "24px 38px 0 #4465FF",
          opacity: raw,
        }}
      >
        <Image name="IMG_5071.jpg" />
        <div
          style={{
            position: "absolute",
            left: 34,
            top: 30,
            background: palette.paper,
            padding: "13px 20px",
            fontSize: 24,
          }}
        >
          IMG_5071.CR2 <span style={{ opacity: 0.5 }}> / 6000 × 4000</span>
        </div>
      </div>
      <div
        style={{
          position: "absolute",
          right: 85,
          top: 1300 + k(t, 7.5, 8.4, 240, 0),
          rotate: "7deg",
          opacity: k(t, 7.5, 8.1),
        }}
      >
        <Curve />
      </div>
      <div
        style={{
          position: "absolute",
          left: 88,
          bottom: 120,
          fontSize: 44,
          lineHeight: 1.25,
          opacity: k(t, 8.25, 8.7),
        }}
      >
        Tone curves.
        <br />
        Local adjustments.
      </div>
    </div>
  );
};

export const OpeningStudy = () => {
  const { fps } = useVideoConfig();
  return (
    <AbsoluteFill
      style={{
        background: palette.paper,
        color: palette.ink,
        fontFamily: "Manrope",
        overflow: "hidden",
      }}
    >
      <Sequence
        name="Source to photo, video and design"
        durationInFrames={12 * fps}
      >
        <OutputWorld />
      </Sequence>
      <Sequence
        name="Introduce the app and workflow value"
        durationInFrames={12 * fps}
      >
        <OpeningCopy />
      </Sequence>
    </AbsoluteFill>
  );
};
