import React, { useId } from "react";
import {
  AbsoluteFill,
  Easing,
  interpolate,
  Sequence,
  spring,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import {
  Brand,
  Curve,
  Image,
  Index,
  Layers,
  LightContours,
  palette,
  subjectPath,
} from "./StyleFrames";

const ease = Easing.bezier(0.68, 0, 0.22, 1);
const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
const a: React.CSSProperties = { position: "absolute" };
const source = (name: string) => staticFile(`media/${name}`);
const ramp = (t: number, from: number, to: number) =>
  interpolate(t, [from, to], [0, 1], { ...clamp, easing: ease });

const Pop = ({
  at,
  children,
  style,
  weight = 1,
}: {
  at: number;
  children: React.ReactNode;
  style?: React.CSSProperties;
  weight?: number;
}) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const p = spring({
    frame: frame - at * fps,
    fps,
    config: { damping: 17, stiffness: 160, mass: weight },
  });
  return (
    <div
      style={{
        ...a,
        ...style,
        opacity: interpolate(
          frame,
          [at * fps, (at + 0.12) * fps],
          [0, 1],
          clamp,
        ),
        translate: `0px ${(1 - p) * 100}px`,
        scale: `${1 - (1 - p) * 0.025} ${1 + (1 - p) * 0.08}`,
      }}
    >
      {children}
    </div>
  );
};

const LivingCurve = () => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const p = ramp(f / fps, 1.8, 2.7);
  return (
    <div style={{ position: "relative", width: 440, height: 448 }}>
      <Curve />
      <svg
        width="372"
        height="298"
        style={{ ...a, left: 34, top: 81, background: palette.ink }}
        viewBox="0 0 372 298"
      >
        <g stroke="#FFFFFF1C">
          {[0, 1, 2, 3, 4].map((i) => (
            <React.Fragment key={i}>
              <path d={`M${i * 92} 0V282`} />
              <path d={`M0 ${i * 70}H368`} />
            </React.Fragment>
          ))}
        </g>
        <path d="M0 282L368 0" stroke="#FFFFFF3D" strokeDasharray="5 7" />
        {[palette.red, palette.green, "#E9EDFF"].map((color, i) => (
          <path
            key={color}
            d={`M0 280C${75 + i * 8} ${280 - p * 4} ${105 + i * 4} ${200 + (1 - p) * 70} 165 ${156 + (1 - p) * 28}S270 ${48 + i * 7} 368 3`}
            fill="none"
            stroke={color}
            strokeWidth={i === 2 ? 5 : 3}
          />
        ))}
        <circle
          cx="165"
          cy={156 + (1 - p) * 28}
          r={29 + Math.sin(p * Math.PI) * 16}
          fill="white"
          opacity=".08"
        />
        <circle
          cx="165"
          cy={156 + (1 - p) * 28}
          r="11"
          fill={palette.ink}
          stroke="white"
          strokeWidth="4"
        />
      </svg>
    </div>
  );
};

const ColorWorld = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = frame / fps;
  const leave = ramp(t, 3.05, 4.55);
  const arrival = spring({
    frame: frame - 0.25 * fps,
    fps,
    config: { damping: 19, stiffness: 110, mass: 1.4 },
  });
  return (
    <AbsoluteFill
      style={{
        background: palette.paper,
        color: palette.ink,
        fontFamily: "Manrope",
        transformOrigin: "710px 1465px",
        transform: `translate(${-leave * 170}px, ${-leave * 380}px) scale(${1 + leave * 3.8}) rotate(${-leave * 14}deg)`,
        opacity: 1 - ramp(t, 4.15, 4.65),
      }}
    >
      <LightContours
        style={{ top: 430, left: 250, rotate: `${-22 + t * 0.7}deg` }}
      />
      <Brand />
      <Index number="01" />
      <Pop
        at={0.08}
        style={{
          left: 80,
          top: 232,
          fontWeight: 800,
          fontSize: 113,
          lineHeight: 1.02,
          letterSpacing: -7,
        }}
      >
        Color with
      </Pop>
      <Pop
        at={0.24}
        style={{
          left: 80,
          top: 347,
          fontWeight: 800,
          fontSize: 113,
          lineHeight: 1.02,
          letterSpacing: -7,
        }}
      >
        precision<span style={{ color: palette.blue }}>.</span>
      </Pop>
      <Pop
        at={0.5}
        style={{ left: 88, top: 506, fontSize: 44, letterSpacing: -0.6 }}
      >
        RAW editing. At native resolution.
      </Pop>
      <div
        style={{
          ...a,
          left: -40,
          top: 690 + (1 - arrival) * 350,
          width: 1160,
          height: 770,
          transform: `perspective(2000px) rotateY(${-8 + (1 - arrival) * 36 - leave * 30}deg) rotateZ(${-7 - (1 - arrival) * 12}deg)`,
          boxShadow: "24px 38px 0 #4465FF",
          filter: `saturate(${1 + ramp(t, 1.8, 2.7) * 0.08})`,
        }}
      >
        <Image name="IMG_5071.jpg" />
        <div
          style={{
            ...a,
            left: 34,
            top: 30,
            background: palette.paper,
            padding: "13px 20px",
            fontSize: 24,
          }}
        >
          IMG_5071.CR2 <span style={{ opacity: 0.5 }}> / 6000 × 4000</span>
        </div>
        <div
          style={{
            ...a,
            left: 467,
            top: 302,
            width: 140,
            height: 140,
            border: "2px solid #F2F1ED",
            outline: "9px solid #F2F1ED1A",
            opacity: ramp(t, 1.1, 1.4),
          }}
        />
      </div>
      <svg
        viewBox="0 0 1080 1920"
        width="1080"
        height="1920"
        fill="none"
        style={{ ...a, inset: 0 }}
      >
        <g
          strokeLinecap="round"
          strokeDasharray="2200"
          strokeDashoffset={2200 * (1 - ramp(t, 0.5, 1.55))}
        >
          <path
            d="M-110 1190C100 1090 91 1530 290 1517C560 1502 490 1183 768 1214C990 1236 1068 1152 1170 1045"
            stroke={palette.red}
            strokeWidth="49"
          />
          <path
            d="M-104 1225C126 1143 108 1571 307 1545C570 1510 514 1223 775 1251C990 1276 1091 1190 1178 1088"
            stroke={palette.green}
            strokeWidth="21"
          />
          <path
            d="M-110 1261C116 1170 130 1607 330 1578C570 1542 536 1260 785 1292C996 1320 1100 1227 1190 1126"
            stroke={palette.blue}
            strokeWidth="13"
          />
        </g>
      </svg>
      <Pop
        at={0.8}
        style={{ right: 85, top: 1260, rotate: "7deg" }}
        weight={1.5}
      >
        <LivingCurve />
      </Pop>
      <Pop
        at={1.1}
        style={{
          left: 88,
          bottom: 125,
          fontSize: 44,
          lineHeight: 1.25,
          letterSpacing: -1,
        }}
      >
        Tone curves.
        <br />
        Local adjustments.
      </Pop>
    </AbsoluteFill>
  );
};

// A deterministic 2D optical field: nested deformed contours and screen-blended
// wave ribbons. Shape, phase, and camera scale share the composition clock.
const OpticalBridge = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = frame / fps;
  const enter = ramp(t, 3.7, 4.55);
  const exit = ramp(t, 5.25, 6.3);
  const phase = (t - 4) * 1.8;
  const path = (r: number, offset: number) => {
    const points = Array.from({ length: 97 }, (_, j) => {
      const theta = (j / 96) * Math.PI * 2;
      const radius =
        r *
        (1 +
          0.18 * Math.sin(theta * 3 + phase + offset) +
          0.09 * Math.cos(theta * 5 - phase));
      return `${j === 0 ? "M" : "L"}${540 + Math.cos(theta) * radius} ${960 + Math.sin(theta) * radius * 1.35}`;
    });
    return `${points.join(" ")}Z`;
  };
  return (
    <AbsoluteFill
      style={{
        opacity: enter * (1 - exit),
        background: palette.ink,
        clipPath: `circle(${enter * 1800}px at 65% 74%)`,
        overflow: "hidden",
      }}
    >
      <svg
        viewBox="0 0 1080 1920"
        width="1080"
        height="1920"
        style={{
          ...a,
          inset: 0,
          transform: `rotate(${phase * 12}deg) scale(${1.5 - enter * 0.4 + exit * 1.6})`,
          isolation: "isolate",
        }}
        fill="none"
      >
        {Array.from({ length: 20 }, (_, n) => (
          <path
            key={n}
            d={path(110 + n * 49, n * 0.07)}
            stroke={[palette.blue, palette.green, palette.red][n % 3]}
            strokeWidth={n % 4 === 0 ? 5 : 1.5}
            opacity={0.24 + (n % 4) * 0.14}
            style={{ mixBlendMode: "screen" }}
          />
        ))}
        {["#FF0000", "#00FF00", "#0000FF"].map((color, i) => (
          <path
            key={color}
            d={`M-300 ${1450 + i * 65}C${230 + Math.sin(phase) * 120} ${1640 - i * 100} ${80 + i * 110} ${400 + Math.cos(phase) * 180} ${540 + i * 38} ${710 + Math.sin(phase + i * 0.3) * 100}S${910 + i * 70} ${1360 - i * 70} 1430 ${310 + i * 100}`}
            stroke={color}
            strokeWidth={65 - i * 9}
            strokeLinecap="round"
            style={{ mixBlendMode: "screen" }}
          />
        ))}
        <path d={path(250, 0.2)} stroke="white" strokeWidth="3" opacity=".8" />
      </svg>
    </AbsoluteFill>
  );
};

const PortraitPlanes = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = frame / fps;
  const id = useId().replace(/:/g, "");
  const settle = spring({
    frame: frame - 5.7 * fps,
    fps,
    config: { damping: 26, stiffness: 65, mass: 1.8 },
  });
  const orbit = ramp(t, 5.65, 7.5);
  const fan = 1 - ramp(t, 7.2, 8.2);
  const type = spring({
    frame: frame - 7.5 * fps,
    fps,
    config: { damping: 18, stiffness: 150, mass: 1.2 },
  });
  const mask = ramp(t, 6.6, 7.6) * (1 - ramp(t, 8.4, 8.9));
  const brush = ramp(t, 8.65, 9.3);
  return (
    <div
      style={{
        ...a,
        left: 177,
        top: 630,
        width: 750,
        height: 1125,
        perspective: 2300,
        opacity: ramp(t, 5.5, 5.95),
      }}
    >
      <div
        style={{
          width: "100%",
          height: "100%",
          transformStyle: "preserve-3d",
          transform: `translate(${(1 - settle) * 780}px, ${(1 - settle) * 120}px) rotateY(${-64 + orbit * 54}deg) rotateX(${(1 - orbit) * 15}deg) rotateZ(${-16 + orbit * 13}deg) scale(${0.76 + settle * 0.24})`,
        }}
      >
        <div
          style={{
            ...a,
            inset: 0,
            border: "2px solid #4465FF",
            transform: `translate3d(${-44 - fan * 80}px,40px,${-90 - fan * 120}px) rotateZ(-6deg)`,
          }}
        />
        <div
          style={{
            ...a,
            inset: 0,
            background: palette.blue,
            transform: `translate3d(-18px,21px,${-35 - fan * 60}px) rotateZ(-3deg)`,
          }}
        />
        <svg
          viewBox="0 0 1200 1800"
          width="750"
          height="1125"
          style={{ ...a, inset: 0 }}
        >
          <defs>
            <clipPath id={`${id}-bg`}>
              <path
                d={`M0 0H1200V1800H0Z ${subjectPath}`}
                clipRule="evenodd"
                fillRule="evenodd"
              />
            </clipPath>
          </defs>
          <image
            href={source("IMG_5106.jpg")}
            width="1200"
            height="1800"
            clipPath={`url(#${id}-bg)`}
          />
          <text
            x="61"
            y="1728"
            fill={palette.paper}
            fontFamily="Manrope"
            fontSize="26"
            letterSpacing="6"
          >
            A LITTLE FURTHER.
          </text>
        </svg>
        <svg
          viewBox="0 0 1200 1800"
          width="750"
          height="1125"
          style={{
            ...a,
            inset: 0,
            transform: `translate3d(${(1 - type) * -220}px,0,${fan * 65 + 2}px)`,
            opacity: ramp(t, 7.5, 7.75),
          }}
        >
          <text
            x="600"
            y="1045"
            textAnchor="middle"
            fill={palette.paper}
            fontFamily="Manrope"
            fontWeight="800"
            fontSize="310"
            letterSpacing="-23"
          >
            NEPAL
          </text>
        </svg>
        <svg
          viewBox="0 0 1200 1800"
          width="750"
          height="1125"
          style={{
            ...a,
            inset: 0,
            transform: `translate3d(${fan * 110}px,${fan * -25}px,${fan * 180 + 4}px)`,
          }}
        >
          <defs>
            <clipPath id={`${id}-subject`}>
              <path d={subjectPath} />
            </clipPath>
          </defs>
          <image
            href={source("IMG_5106.jpg")}
            width="1200"
            height="1800"
            clipPath={`url(#${id}-subject)`}
          />
          <path
            d={subjectPath}
            stroke={palette.green}
            fill="none"
            strokeWidth="5"
            pathLength="1"
            strokeDasharray="1"
            strokeDashoffset={1 - ramp(t, 6.6, 7.6)}
            opacity={mask}
          />
        </svg>
        <svg
          viewBox="0 0 1200 1800"
          width="750"
          height="1125"
          style={{ ...a, inset: 0, transform: "translateZ(7px)" }}
        >
          <path
            d="M925 1590C1080 1530 1100 1470 1070 1437C1043 1405 927 1469 919 1543C910 1618 1044 1652 1180 1553"
            stroke={palette.red}
            strokeWidth="32"
            fill="none"
            strokeLinecap="round"
            pathLength="1"
            strokeDasharray="1"
            strokeDashoffset={1 - brush}
          />
        </svg>
      </div>
    </div>
  );
};

const MaskWorld = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = frame / fps;
  return (
    <AbsoluteFill
      style={{
        fontFamily: "Manrope",
        color: palette.paper,
        background: palette.ink,
        opacity: ramp(t, 4.95, 5.6),
      }}
    >
      <LightContours
        color={palette.green}
        style={{
          left: -580,
          top: 440,
          transform: `rotate(${20 + (1 - ramp(t, 6, 8.2)) * 18}deg)`,
        }}
      />
      <div style={{ opacity: ramp(t, 6.3, 6.8) }}>
        <Brand light />
        <Index number="02" light />
      </div>
      <Pop
        at={7.1}
        style={{
          left: 80,
          top: 232,
          fontSize: 113,
          lineHeight: 1.02,
          fontWeight: 800,
          letterSpacing: -7,
        }}
      >
        Keep it
      </Pop>
      <Pop
        at={7.32}
        style={{
          left: 80,
          top: 347,
          fontSize: 113,
          lineHeight: 1.02,
          fontWeight: 800,
          letterSpacing: -7,
          color: palette.green,
        }}
      >
        editable.
      </Pop>
      <Pop
        at={7.65}
        style={{ left: 88, top: 504, fontSize: 44, color: "#BCBDB6" }}
      >
        Layers. Masks. Pixel editing.
      </Pop>
      <PortraitPlanes />
      <svg
        width="1080"
        height="1920"
        viewBox="0 0 1080 1920"
        fill="none"
        style={{ ...a, inset: 0, opacity: ramp(t, 7.6, 8.1) }}
      >
        <path
          d="M1130 684C1030 645 979 728 1019 825C1073 956 969 963 955 1052C942 1137 1058 1116 1148 1209"
          stroke={palette.green}
          strokeWidth="40"
          pathLength="1"
          strokeDasharray="1"
          strokeDashoffset={1 - ramp(t, 7.2, 8.5)}
        />
        <path
          d="M-155 1653C73 1520 24 1390 92 1343C181 1283 204 1445 104 1488"
          stroke={palette.blue}
          strokeWidth="55"
          pathLength="1"
          strokeDasharray="1"
          strokeDashoffset={1 - ramp(t, 7.7, 8.6)}
        />
        <path
          d="M-135 1695C105 1555 55 1400 112 1380"
          stroke={palette.red}
          strokeWidth="10"
          pathLength="1"
          strokeDasharray="1"
          strokeDashoffset={1 - ramp(t, 8, 8.7)}
        />
        <path
          d="M126 1150V1202H178M131 1577V1525H183"
          stroke={palette.paper}
          strokeWidth="3"
        />
      </svg>
      <Pop
        at={8.3}
        style={{ right: 50, top: 1462, rotate: "5deg" }}
        weight={1.5}
      >
        <Layers />
      </Pop>
    </AbsoluteFill>
  );
};

export const MotionStudy = () => {
  const { fps } = useVideoConfig();
  return (
    <AbsoluteFill style={{ background: palette.paper, overflow: "hidden" }}>
      <Sequence name="Color — curve — camera push" durationInFrames={4.7 * fps}>
        <ColorWorld />
      </Sequence>
      <Sequence
        name="Layer orbit — masked type — result"
        durationInFrames={12 * fps}
      >
        <MaskWorld />
      </Sequence>
      <Sequence name="RGB optical passage" durationInFrames={6.4 * fps}>
        <OpticalBridge />
      </Sequence>
    </AbsoluteFill>
  );
};
