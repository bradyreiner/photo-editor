import React, { useId } from "react";
import { Audio, Video } from "@remotion/media";
import {
  AbsoluteFill,
  CanvasImage,
  Easing,
  Freeze,
  interpolate,
  Sequence,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { DeskIllustration, DeskOpening } from "./DeskOpening";
import {
  Brand,
  Image,
  Layers,
  LightContours,
  MaskArt,
  palette,
  subjectPath,
} from "./StyleFrames";

const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
const ease = Easing.bezier(0.7, 0, 0.2, 1);
const out = Easing.bezier(0.16, 1, 0.3, 1);
const r = (t: number, a: number, b: number, x = 0, y = 1) =>
  interpolate(t, [a, b], [x, y], { ...clamp, easing: ease });
const media = (name: string) => staticFile(`media/${name}`);
const abs: React.CSSProperties = { position: "absolute" };
const useSeconds = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  return frame / fps;
};

const Heading = ({
  children,
  subtitle,
  dark = false,
}: {
  children: React.ReactNode;
  subtitle?: string;
  dark?: boolean;
}) => {
  const t = useSeconds();
  return (
    <div
      style={{
        ...abs,
        left: 80,
        top: 190,
        color: dark ? palette.paper : palette.ink,
        translate: `0px ${interpolate(t, [0, 0.65], [45, 0], { ...clamp, easing: out })}px`,
        opacity: r(t, 0, 0.16),
      }}
    >
      <div
        style={{
          fontWeight: 800,
          fontSize: 96,
          lineHeight: 1.04,
          letterSpacing: -5.5,
        }}
      >
        {children}
      </div>
      {subtitle && (
        <div
          style={{
            fontSize: 39,
            marginTop: 27,
            letterSpacing: -0.7,
            fontWeight: 500,
          }}
        >
          {subtitle}
        </div>
      )}
    </div>
  );
};

const Surface = ({
  children,
  dark = false,
}: {
  children: React.ReactNode;
  dark?: boolean;
}) => (
  <AbsoluteFill
    style={{
      background: dark ? palette.ink : palette.paper,
      color: dark ? palette.paper : palette.ink,
      fontFamily: "Manrope",
      overflow: "hidden",
    }}
  >
    <Brand light={dark} />
    {children}
  </AbsoluteFill>
);

const PhotoTools = () => {
  const t = useSeconds();
  const id = useId().replace(/:/g, "");
  const open = r(t, 0, 1);
  const exposure = r(t, 1.1, 2.2, 0, 0.16) - r(t, 2.7, 3.1, 0, 0.06);
  const mask = r(t, 5.2, 6.5) * (1 - r(t, 7.9, 8.5));
  const w = 280 + open * 260;
  const h = w * 1.5;
  return (
    <Surface>
      <LightContours
        style={{ left: 190, top: 440, rotate: "-20deg", opacity: 0.4 }}
      />
      <Heading subtitle="Exposure. Tone. Color. Detail.">
        Every adjustment.
        <br />
        Your call<span style={{ color: palette.blue }}>.</span>
      </Heading>
      <div
        style={{
          ...abs,
          left: 72 + open * 8,
          top: 600,
          width: w,
          height: h,
          transform: `perspective(2000px) rotateY(${-open * 5}deg) rotateZ(${-open * 3}deg)`,
          boxShadow: "15px 22px 0 #4465FF",
        }}
      >
        <Image
          name="IMG_5106.jpg"
          style={{
            filter: `brightness(${1 + exposure}) saturate(${1 + r(t, 3.3, 4.1, 0, 0.06)})`,
          }}
        />
        <svg
          viewBox="0 0 1200 1800"
          width="100%"
          height="100%"
          style={{ ...abs, inset: 0 }}
        >
          <defs>
            <clipPath id={id}>
              <path d={subjectPath} />
            </clipPath>
          </defs>
          <image
            href={media("IMG_5106.jpg")}
            width="1200"
            height="1800"
            clipPath={`url(#${id})`}
            style={{ filter: `brightness(${1 + exposure + mask * 0.06})` }}
          />
          <path
            d={subjectPath}
            fill="none"
            stroke={palette.green}
            strokeWidth="6"
            pathLength="1"
            strokeDasharray="1"
            strokeDashoffset={1 - r(t, 5.1, 6.5)}
            opacity={mask}
          />
        </svg>
      </div>
      <div
        style={{
          ...abs,
          left: 650 + (1 - open) * 500,
          top: 600,
          width: 345,
          padding: "30px 27px",
          boxSizing: "border-box",
          background: palette.ink,
          color: palette.paper,
        }}
      >
        <div style={{ fontSize: 25, fontWeight: 800, marginBottom: 32 }}>
          Photo controls
        </div>
        {[
          "Exposure",
          "White balance",
          "Highlights / shadows",
          "Color mix",
          "Detail / sharpening",
        ].map((label, i) => (
          <div key={label} style={{ marginBottom: 35 }}>
            <div
              style={{
                fontSize: 22,
                marginBottom: 17,
                display: "flex",
                justifyContent: "space-between",
              }}
            >
              {label}
              <span style={{ color: palette.green, fontSize: 19 }}>
                {i === 0 ? `+${(exposure * 2.2).toFixed(2)}` : ""}
              </span>
            </div>
            <div
              style={{ height: 3, background: "#414A38", position: "relative" }}
            >
              <div
                style={{
                  ...abs,
                  left: `${44 + (i === 0 ? exposure * 130 : Math.sin(i * 2) * r(t, 2.4 + i * 0.2, 3.5 + i * 0.2) * 13)}%`,
                  top: -7,
                  width: 16,
                  height: 16,
                  borderRadius: "50%",
                  background: i === 0 ? palette.green : palette.paper,
                }}
              />
            </div>
          </div>
        ))}
        <div style={{ fontSize: 22, marginTop: 10 }}>Tone curve</div>
        <svg
          width="290"
          height="210"
          viewBox="0 0 290 210"
          style={{ marginTop: 17 }}
        >
          <g stroke="#FFFFFF16">
            {[0, 1, 2, 3, 4].map((i) => (
              <g key={i}>
                <path d={`M${i * 72} 0V205`} />
                <path d={`M0 ${i * 51}H290`} />
              </g>
            ))}
          </g>
          <path
            d={`M0 205C85 203 76 ${170 - r(t, 3.1, 4.2, 0, 24)} 135 ${116 - r(t, 3.1, 4.2, 0, 20)}S230 32 290 0`}
            stroke={palette.green}
            strokeWidth="4"
            fill="none"
          />
          <circle
            cx="135"
            cy={116 - r(t, 3.1, 4.2, 0, 20)}
            r="7"
            fill="white"
          />
        </svg>
      </div>
      <div
        style={{
          ...abs,
          left: 80,
          top: 1560,
          fontSize: 39,
          lineHeight: 1.35,
          opacity: r(t, 5.3, 6),
        }}
      >
        Targeted masks.
        <br />
        Native-resolution source.
      </div>
      <div
        style={{
          ...abs,
          right: 85,
          bottom: 110,
          fontSize: 24,
          color: "#7C8373",
        }}
      >
        IMG_5106.CR2 / 4000 × 6000
      </div>
    </Surface>
  );
};

const Design = () => {
  const t = useSeconds();
  const id = useId().replace(/:/g, "");
  const enter = r(t, 0, 1.1);
  const type = r(t, 1.5, 2.4);
  const brush = r(t, 3.2, 4.2) - r(t, 4.6, 4.95) + r(t, 5.2, 5.8);
  const fan = Math.sin(r(t, 0.2, 3.0) * Math.PI);
  return (
    <Surface dark>
      <LightContours
        color={palette.green}
        style={{ left: -550, top: 450, rotate: "20deg" }}
      />
      <Heading dark subtitle="Masks. Text. Brushes. Layers.">
        Build the
        <br />
        composition<span style={{ color: palette.green }}>.</span>
      </Heading>
      <div
        style={{
          ...abs,
          left: 80 + enter * 97,
          top: 600 + enter * 30,
          width: 540 + enter * 210,
          height: 810 + enter * 315,
          perspective: 2200,
        }}
      >
        <div
          style={{
            width: "100%",
            height: "100%",
            transformStyle: "preserve-3d",
            transform: `rotateY(${-5 - fan * 24}deg) rotateZ(${-3 - fan * 5}deg)`,
          }}
        >
          <div
            style={{
              ...abs,
              inset: 0,
              background: palette.blue,
              transform: `translate3d(${-20 - fan * 65}px,25px,${-40 - fan * 80}px) rotate(-3deg)`,
            }}
          />
          <svg
            width="100%"
            height="100%"
            viewBox="0 0 1200 1800"
            style={{ ...abs, inset: 0 }}
          >
            <image href={media("IMG_5106.jpg")} width="1200" height="1800" />
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
            width="100%"
            height="100%"
            viewBox="0 0 1200 1800"
            style={{
              ...abs,
              inset: 0,
              transform: `translate3d(${(1 - type) * -340}px,0,${2 + fan * 30}px)`,
              opacity: type,
            }}
          >
            <text
              x="600"
              y="1045"
              textAnchor="middle"
              fill={palette.paper}
              fontFamily="Manrope"
              fontSize="310"
              letterSpacing="-23"
              fontWeight="800"
            >
              NEPAL
            </text>
          </svg>
          <svg
            width="100%"
            height="100%"
            viewBox="0 0 1200 1800"
            style={{
              ...abs,
              inset: 0,
              transform: `translateZ(${4 + fan * 40}px)`,
            }}
          >
            <defs>
              <clipPath id={id}>
                <path d={subjectPath} />
              </clipPath>
            </defs>
            <image
              href={media("IMG_5106.jpg")}
              width="1200"
              height="1800"
              clipPath={`url(#${id})`}
            />
            <path
              d={subjectPath}
              fill="none"
              stroke={palette.green}
              strokeWidth="4"
              opacity={fan * 0.8}
            />
          </svg>
          <svg
            width="100%"
            height="100%"
            viewBox="0 0 1200 1800"
            style={{ ...abs, inset: 0, transform: "translateZ(7px)" }}
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
      <div
        style={{
          ...abs,
          right: 40,
          top: 1475 + (1 - r(t, 2, 3)) * 250,
          rotate: "5deg",
          opacity: r(t, 2, 2.4),
        }}
      >
        <Layers />
      </div>
    </Surface>
  );
};

const Production = () => {
  const t = useSeconds();
  const play = r(t, 0.5, 11, 0, 910);
  const trim = r(t, 1.3, 2.2, 0, 90);
  const titles = r(t, 2.8, 3.6);
  const clip =
    t < 4.4
      ? "IMG_1207-2-7.mp4"
      : t < 7.9
        ? "IMG_1268-2-7.mp4"
        : "MVI_5051-2.3-5.9.mp4";
  return (
    <Surface>
      <Heading subtitle="Picture. Motion. Color. Audio.">
        Build the
        <br />
        whole production<span style={{ color: palette.blue }}>.</span>
      </Heading>
      <div
        style={{
          ...abs,
          left: 80,
          top: 570,
          width: 920,
          height: 518,
          background: palette.ink,
          boxShadow: "16px 23px 0 #4465FF",
          transform: `perspective(2200px) rotateY(${r(t, 0, 1, 13, 0)}deg) rotateZ(${r(t, 0, 1, -5, 0)}deg)`,
        }}
      >
        <Video
          key={clip}
          name="Finished sequence picture"
          src={media(clip)}
          muted
          loop
          objectFit="cover"
          style={{
            width: "100%",
            height: "100%",
            filter: `saturate(${1 + r(t, 5, 5.8, 0, 0.08)})`,
          }}
        />
        <div
          style={{
            ...abs,
            left: 45,
            bottom: 42,
            color: "white",
            opacity: titles,
            translate: `0px ${(1 - titles) * 40}px`,
          }}
        >
          <div style={{ fontSize: 49, fontWeight: 800, letterSpacing: -2 }}>
            A LITTLE FURTHER.
          </div>
          <div style={{ fontSize: 23, marginTop: 7 }}>NEPAL / THE JOURNEY</div>
        </div>
        <div
          style={{
            ...abs,
            left: 25,
            top: 23,
            background: "#101112B0",
            color: "white",
            padding: "10px 15px",
            fontSize: 21,
          }}
        >
          PROGRAM / sequence 01
        </div>
      </div>
      <div
        style={{
          ...abs,
          left: 45,
          top: 1185,
          width: 1040,
          background: palette.ink,
          color: "white",
          padding: "25px 25px 31px",
          boxSizing: "border-box",
          transform: `rotate(${r(t, 0, 1.2, 5, 0)}deg)`,
        }}
      >
        <div
          style={{
            display: "flex",
            justifyContent: "space-between",
            fontSize: 24,
            marginBottom: 22,
          }}
        >
          <span>NEPAL / MASTER</span>
          <span>00:00:{String(Math.floor(t)).padStart(2, "0")}</span>
        </div>
        <div style={{ height: 340, position: "relative", paddingLeft: 60 }}>
          <div
            style={{ ...abs, left: 0, top: 32, color: "#8C987D", fontSize: 16 }}
          >
            V2
          </div>
          <div
            style={{
              ...abs,
              left: 0,
              top: 112,
              color: "#8C987D",
              fontSize: 16,
            }}
          >
            V1
          </div>
          <div
            style={{
              ...abs,
              left: 0,
              top: 216,
              color: "#8C987D",
              fontSize: 16,
            }}
          >
            A1
          </div>
          <div
            style={{
              ...abs,
              left: 60 + 100 * titles,
              top: 8,
              width: 400 * titles,
              height: 51,
              overflow: "hidden",
              background: palette.blue,
              fontSize: 20,
              whiteSpace: "nowrap",
              padding: "14px 20px",
              boxSizing: "border-box",
            }}
          >
            TITLE / A little further
          </div>
          <div
            style={{ display: "flex", gap: 7, position: "relative", top: 76 }}
          >
            <div
              style={{
                width: 360 - trim,
                height: 102,
                border: "3px solid #4465FF",
                overflow: "hidden",
                display: "flex",
                flexShrink: 0,
              }}
            >
              {[0, 1, 2].map((i) => (
                <div key={i} style={{ width: 120, height: 102, flexShrink: 0 }}>
                  <Image name="IMG_1207-2-7.jpg" />
                </div>
              ))}
            </div>
            <div
              style={{
                width: 295 + trim,
                height: 102,
                border: "3px solid #4465FF",
                overflow: "hidden",
                display: "flex",
                flexShrink: 0,
              }}
            >
              {[0, 1, 2, 3].map((i) => (
                <div key={i} style={{ width: 120, height: 102, flexShrink: 0 }}>
                  <Image name="IMG_1268-2-7.jpg" />
                </div>
              ))}
            </div>
            <div
              style={{
                width: 320,
                height: 102,
                border: "3px solid #4465FF",
                overflow: "hidden",
              }}
            >
              <Image name="IMG_5071.jpg" />
            </div>
          </div>
          <svg
            width="928"
            height="70"
            viewBox="0 0 928 70"
            style={{ ...abs, top: 199, left: 60, background: "#2A3E25" }}
          >
            {Array.from({ length: 175 }, (_, n) => {
              const h =
                6 + Math.abs(Math.sin(n * 1.72) * Math.cos(n * 0.18)) * 48;
              return (
                <rect
                  key={n}
                  x={n * 5.3}
                  y={(70 - h) / 2}
                  width="2.6"
                  height={h}
                  fill={palette.green}
                />
              );
            })}
            <path
              d={`M0 50L180 50L300 ${50 - r(t, 6.3, 7.2, 0, 24)}L700 31L928 31`}
              fill="none"
              stroke="white"
              strokeWidth="2"
            />
          </svg>
          <div
            style={{
              ...abs,
              top: -9,
              left: 60 + play,
              width: 3,
              height: 298,
              background: palette.red,
            }}
          >
            <div
              style={{
                width: 14,
                height: 14,
                background: palette.red,
                rotate: "45deg",
                translate: "-6px 0px",
              }}
            />
          </div>
          <div
            style={{
              ...abs,
              bottom: 0,
              left: 60,
              display: "flex",
              gap: 40,
              fontSize: 20,
              color: "#A8B69A",
            }}
          >
            <span>◇ Keyframes</span>
            <span>Effects</span>
            <span>Color</span>
            <span>Audio mix</span>
          </div>
        </div>
      </div>
      <div
        style={{ ...abs, left: 80, bottom: 110, fontSize: 36, lineHeight: 1.3 }}
      >
        From first cut
        <br />
        to finished sequence.
      </div>
    </Surface>
  );
};

const Carousel = () => {
  const t = useSeconds();
  const open = r(t, 0, 1.6);
  return (
    <Surface>
      <Heading subtitle="Carousel layouts.">
        One story.
        <br />
        Across the whole set<span style={{ color: palette.blue }}>.</span>
      </Heading>
      <div
        style={{
          ...abs,
          left: 80,
          top: 620,
          width: 920,
          height: 880,
          perspective: 2200,
        }}
      >
        <div
          style={{
            ...abs,
            left: 0,
            top: 0,
            width: 420,
            height: 630,
            background: palette.blue,
            color: "white",
            padding: 35,
            boxSizing: "border-box",
            transform: `rotateY(${(1 - open) * -50}deg) rotateZ(${-7 * open}deg)`,
          }}
        >
          <div
            style={{
              fontSize: 95,
              fontWeight: 800,
              lineHeight: 0.93,
              letterSpacing: -6,
            }}
          >
            NE
            <br />
            PAL
          </div>
          <div style={{ fontSize: 20, marginTop: 215 }}>01 / THE JOURNEY</div>
        </div>
        <div
          style={{
            ...abs,
            left: 250 + open * 60,
            top: 90 + open * 35,
            width: 420,
            height: 630,
            transform: `rotateY(${(1 - open) * 70}deg) rotateZ(${open * 2}deg)`,
          }}
        >
          <Image name="IMG_4870.jpg" />
        </div>
        <div
          style={{
            ...abs,
            left: 490 + open * 120,
            top: 190 + open * 60,
            width: 420,
            height: 630,
            transform: `rotateY(${(1 - open) * 80}deg) rotateZ(${open * 8}deg)`,
          }}
        >
          <Image name="IMG_4969.jpg" />
        </div>
        <svg
          width="1080"
          height="900"
          viewBox="0 0 1080 900"
          style={{ ...abs, left: -80, top: 0 }}
        >
          <path
            d="M-60 751C150 615 368 808 515 684S869 465 1150 698"
            fill="none"
            stroke={palette.red}
            strokeWidth="25"
            pathLength="1"
            strokeDasharray="1"
            strokeDashoffset={1 - r(t, 0.7, 2.4)}
          />
        </svg>
      </div>
      <div
        style={{ ...abs, left: 80, bottom: 120, fontSize: 39, lineHeight: 1.3 }}
      >
        One shared design.
        <br />A complete panel sequence.
      </div>
    </Surface>
  );
};

const Deliver = () => {
  const t = useSeconds();
  const enter = r(t, 0, 0.65);
  const done = r(t, 1.9, 2.6);
  return (
    <Surface>
      <Heading subtitle="Dimensions. Formats. sRGB.">
        Deliver at the
        <br />
        size you need<span style={{ color: palette.blue }}>.</span>
      </Heading>
      <div
        style={{
          ...abs,
          left: 80,
          top: 600,
          width: 920,
          height: 1010,
          translate: `0px ${(1 - enter) * 160}px`,
        }}
      >
        <div
          style={{
            ...abs,
            left: 0,
            top: 0,
            width: 360,
            height: 540,
            rotate: "-4deg",
          }}
        >
          <MaskArt />
        </div>
        <div
          style={{
            ...abs,
            right: 0,
            top: 95,
            width: 520,
            height: 293,
            rotate: "4deg",
          }}
        >
          <Image name="IMG_1207-2-7.jpg" />
        </div>
        <div
          style={{
            ...abs,
            left: 45,
            top: 595,
            width: 830,
            padding: "30px 35px",
            background: palette.ink,
            color: "white",
            boxSizing: "border-box",
          }}
        >
          <div style={{ fontSize: 27, fontWeight: 800, marginBottom: 25 }}>
            Delivery settings
          </div>
          {[
            "Photo / dimensions + sRGB",
            "Video / size + format",
            "Carousel / panel sequence",
          ].map((line, i) => (
            <div
              key={line}
              style={{
                padding: "17px 0",
                borderTop: "1px solid #FFFFFF25",
                fontSize: 27,
                display: "flex",
                justifyContent: "space-between",
              }}
            >
              <span>{line}</span>
              <span
                style={{
                  color: palette.green,
                  opacity: r(t, 1.5 + i * 0.22, 1.8 + i * 0.22),
                }}
              >
                ✓
              </span>
            </div>
          ))}
        </div>
      </div>
      <div
        style={{
          ...abs,
          left: 80,
          bottom: 105,
          fontSize: 39,
          color: palette.blue,
          opacity: done,
        }}
      >
        Your work. Ready to deliver.
      </div>
    </Surface>
  );
};

const OptionalAgent = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = frame / fps;
  let source = 15.2;
  let title: React.ReactNode = (
    <>
      A hand,
      <br />
      when you want it<span style={{ color: palette.blue }}>.</span>
    </>
  );
  let subtitle = "Optional local AI agent.";
  if (t < 5) source = 15.2 + t * 0.52;
  else if (t < 9) {
    source = 10.5 + (t - 5) * 1.08;
    title = (
      <>
        Find the frame
        <br />
        you remember<span style={{ color: palette.blue }}>.</span>
      </>
    );
    subtitle = "AI search. Your existing media.";
  } else if (t < 15) {
    source = 18.8 + (t - 9) * 0.55;
    title = (
      <>
        Your direction.
        <br />
        Help with repeat work<span style={{ color: palette.blue }}>.</span>
      </>
    );
    subtitle = "Review a sample. Apply to the batch.";
  } else {
    source = 22.6 + (t - 15) * 0.85;
    title = (
      <>
        Still yours
        <br />
        to change<span style={{ color: palette.blue }}>.</span>
      </>
    );
    subtitle = "Native edits. Layers. History.";
  }
  return (
    <AbsoluteFill>
      <Freeze frame={source * fps}>
        <DeskOpening mode="agent" />
      </Freeze>
      <div
        style={{
          ...abs,
          left: 0,
          right: 0,
          top: 125,
          height: 380,
          background: palette.paper,
        }}
      />
      <div
        style={{
          ...abs,
          left: 80,
          top: 175,
          fontFamily: "Manrope",
          color: palette.ink,
        }}
      >
        <div
          style={{
            fontSize: t >= 9 && t < 15 ? 82 : 92,
            fontWeight: 800,
            lineHeight: 1.03,
            letterSpacing: -5,
          }}
        >
          {title}
        </div>
        <div style={{ fontSize: 37, marginTop: 27, letterSpacing: -0.6 }}>
          {subtitle}
        </div>
      </div>
      {t < 5 && (
        <div
          style={{
            ...abs,
            left: 72,
            bottom: 150,
            fontFamily: "Manrope",
            fontSize: 31,
            lineHeight: 1.4,
            color: "#6D7763",
          }}
        >
          AI processing on your computer.
          <br />
          Your editing tools. Your source media.
        </div>
      )}
    </AbsoluteFill>
  );
};

const Ending = () => {
  const t = useSeconds();
  const { fps } = useVideoConfig();
  const reverse = r(t, 0, 2.2, 7, 0);
  const push = r(reverse, 3.6, 7);
  const w = 860 + 220 * push;
  const h = 630 + 1290 * push;
  const bezel = 22 * (1 - push);
  return (
    <Surface>
      <Freeze frame={reverse * fps}>
        <DeskIllustration />
      </Freeze>
      <div
        style={{
          ...abs,
          left: 110 * (1 - push) - bezel,
          top: 570 * (1 - push) - bezel,
          width: w + bezel * 2,
          height: h + bezel * 2,
          background: palette.ink,
          padding: bezel,
          borderRadius: 22 * (1 - push),
          boxSizing: "border-box",
          overflow: "hidden",
          transform: `perspective(2200px) rotateY(${(1 - push) * -7}deg) rotateX(${(1 - push) * 3}deg)`,
        }}
      >
        <svg
          width="100%"
          height="100%"
          viewBox="0 0 1080 640"
          preserveAspectRatio="xMidYMid slice"
          style={{ background: palette.paper }}
        >
          <image
            href={media("looksy-wordmark-light.png")}
            x="35"
            y="26"
            width="135"
            height="46"
          />
          <svg x="30" y="105" width="235" height="420" viewBox="0 0 1200 1800">
            <MaskArt />
          </svg>
          <image
            href={media("IMG_1207-2-7.jpg")}
            x="300"
            y="105"
            width="500"
            height="281"
          />
          <image
            href={media("IMG_4870.jpg")}
            x="834"
            y="105"
            width="195"
            height="281"
            preserveAspectRatio="xMidYMid slice"
          />
          <text
            x="303"
            y="450"
            fontFamily="Manrope"
            fontSize="30"
            fill={palette.ink}
          >
            PHOTO / FILM / CAROUSEL
          </text>
          <path
            d="M310 506L329 524 365 483"
            fill="none"
            stroke={palette.blue}
            strokeWidth="7"
          />
          <text
            x="391"
            y="520"
            fontFamily="Manrope"
            fontSize="27"
            fill={palette.blue}
          >
            DELIVERY COMPLETE
          </text>
        </svg>
      </div>
      <div
        style={{
          ...abs,
          left: 80,
          top: 170,
          opacity: r(t, 1, 1.7),
          fontWeight: 800,
          fontSize: 110,
          letterSpacing: -6,
        }}
      >
        Looksy<span style={{ color: palette.blue }}>.</span>
        <div
          style={{
            fontWeight: 500,
            fontSize: 38,
            letterSpacing: -0.7,
            marginTop: 24,
          }}
        >
          Your whole creative workflow.
        </div>
      </div>
    </Surface>
  );
};

export const FullTrailer = () => {
  const { fps } = useVideoConfig();
  return (
    <AbsoluteFill style={{ background: palette.paper }}>
      {[
        "IMG_5106.jpg",
        "IMG_4870.jpg",
        "IMG_4969.jpg",
        "IMG_1207-2-7.jpg",
        "IMG_1268-2-7.jpg",
        "looksy-wordmark-light.png",
      ].map((name) => (
        <CanvasImage
          key={name}
          src={media(name)}
          style={{ ...abs, width: 1, height: 1, opacity: 0 }}
        />
      ))}
      <Sequence
        name="01 Desk, product, batch import and manual selection"
        durationInFrames={12 * fps}
      >
        <DeskOpening mode="manual" />
      </Sequence>
      <Sequence
        name="02 Full photo controls and local masks"
        from={12 * fps}
        durationInFrames={9 * fps}
      >
        <PhotoTools />
      </Sequence>
      <Sequence
        name="03 Layered composition, masked text and brushes"
        from={21 * fps}
        durationInFrames={7 * fps}
      >
        <Design />
      </Sequence>
      <Sequence
        name="04 Multitrack video, titles, color, motion and audio"
        from={28 * fps}
        durationInFrames={12 * fps}
      >
        <Production />
      </Sequence>
      <Sequence
        name="05 Carousel canvas and panels"
        from={40 * fps}
        durationInFrames={5 * fps}
      >
        <Carousel />
      </Sequence>
      <Sequence
        name="06 Manual workflow complete, output settings"
        from={45 * fps}
        durationInFrames={4 * fps}
      >
        <Deliver />
      </Sequence>
      <Sequence
        name="07 Optional local agent, search, batch review and native revisions"
        from={49 * fps}
        durationInFrames={20 * fps}
      >
        <OptionalAgent />
      </Sequence>
      <Sequence
        name="08 Return to the desk with finished delivery"
        from={69 * fps}
        durationInFrames={3 * fps}
      >
        <Ending />
      </Sequence>
      <Audio
        name="A Game — Michael Ramir C / Mixkit"
        src={staticFile("audio/a-game-michael-ramir-c.mp3")}
        trimBefore={8 * fps}
        durationInFrames={72 * fps}
        volume={(frame) =>
          interpolate(
            frame,
            [0, 0.3 * fps, 69.7 * fps, 72 * fps],
            [0, 0.65, 0.65, 0],
            clamp,
          )
        }
      />
    </AbsoluteFill>
  );
};
