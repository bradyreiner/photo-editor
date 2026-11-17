import {
  AbsoluteFill,
  CanvasImage,
  Sequence,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { Audio, Video } from "@remotion/media";
import { AppWordmark } from "../app-kit/AppElements";
import { font } from "../rebuild/Type";
import { smooth, out, paper, ink } from "../rebuild/Kit";
import { DesktopLight } from "./Light";

const native = (name: string) => staticFile(`media/desktop-launch/${name}`);
const ease = (t: number, a: number, b: number) => smooth(t, a, b);
const lerp = (a: number, b: number, x: number) => a + (b - a) * x;

// Camera moves operate on the captured app, including its real sliders,
// controls and photographic response. No control is redrawn in this film.
const NativeImage = ({ src }: { src: string }) => (
  <CanvasImage
    src={native(src)}
    style={{ width: "100%", height: "100%", objectFit: "fill" }}
  />
);

const RawRecording = () => {
  const { fps } = useVideoConfig();
  return (
    <>
      <Sequence
        name="Native RAW original / window establish"
        durationInFrames={6 * fps}
      >
        <Video
          muted
          src={native("raw-native.mp4")}
          trimBefore={10 * fps}
          style={{ width: "100%", height: "100%" }}
        />
      </Sequence>
      <Sequence
        name="Native RAW exposure slider / real recorded interaction"
        from={6 * fps}
        durationInFrames={6 * fps}
      >
        <Video
          muted
          src={native("raw-native.mp4")}
          trimBefore={18 * fps}
          style={{ width: "100%", height: "100%" }}
        />
      </Sequence>
      <Sequence
        name="Native RAW response / waiting trimmed out"
        from={12 * fps}
        durationInFrames={5 * fps}
      >
        <Video
          muted
          src={native("raw-native.mp4")}
          trimBefore={29 * fps}
          style={{ width: "100%", height: "100%" }}
        />
      </Sequence>
      <Sequence
        name="Native RAW settled full-resolution photograph"
        from={17 * fps}
        durationInFrames={8 * fps}
      >
        <NativeImage src="raw-editor.png" />
      </Sequence>
    </>
  );
};

const NativeCamera = ({ portrait }: { portrait: boolean }) => {
  const { fps } = useVideoConfig();
  const t = useCurrentFrame() / fps;
  const rect = portrait
    ? { x: 54, y: 580, w: 972, h: 1140 }
    : { x: 76, y: 294, w: 1768, h: 722 };
  const fit = Math.min(rect.w, rect.h * 1.7088);
  // Establish the whole desktop. Move toward the native RAW slider, keep the
  // changing photograph visible, then settle on the finished image.
  const into = ease(t, 8.7, 11.2),
    back = ease(t, 20.5, 23.0);
  const zoom = portrait
    ? lerp(1, 2.8, into) * (1 - 0.29 * back)
    : lerp(1, 1.48, into) * (1 - 0.16 * back);
  const fx =
    lerp(0.5, portrait ? 0.78 : 0.68, into) * (1 - back) +
    (portrait ? 0.34 : 0.43) * back;
  const fy = portrait ? lerp(0.5, 0.49, into) : 0.49;
  const iw = fit,
    ih = fit / 1.7088;
  const arrive = out(t, 3.25, 5.0);
  return (
    <div
      style={{
        position: "absolute",
        left: rect.x,
        top: rect.y,
        width: rect.w,
        height: rect.h,
        overflow: "hidden",
        borderRadius: portrait ? 27 : 21,
        opacity: arrive,
        translate: `0 ${80 * (1 - arrive)}px`,
        background: "#080a0d",
        border: "1px solid #ffffff16",
        boxShadow: "0 35px 110px #000b",
      }}
    >
      <div
        style={{
          position: "absolute",
          width: iw,
          height: ih,
          left:
            iw * zoom < rect.w
              ? (rect.w - iw * zoom) / 2
              : Math.max(
                  rect.w - iw * zoom,
                  Math.min(0, rect.w / 2 - fx * iw * zoom),
                ),
          top:
            ih * zoom < rect.h
              ? (rect.h - ih * zoom) / 2
              : Math.max(
                  rect.h - ih * zoom,
                  Math.min(0, rect.h / 2 - fy * ih * zoom),
                ),
          scale: zoom,
          transformOrigin: "0 0",
        }}
      >
        <Sequence
          name="Captured desktop / native RAW session"
          from={4 * fps}
          durationInFrames={25 * fps}
        >
          <RawRecording />
        </Sequence>
        {t < 4 ? <NativeImage src="raw-original.png" /> : null}
      </div>
      {portrait && t > 11.7 && t < 22 ? (
        <div
          style={{
            position: "absolute",
            left: 24,
            bottom: 22,
            fontFamily: font.mono,
            fontSize: 22,
            color: "#d4d8e0",
            background: "#080a0ed9",
            padding: "12px 17px",
            borderRadius: 12,
          }}
        >
          Looksy desktop / native RAW editor
        </div>
      ) : null}
    </div>
  );
};

const Headline = ({ portrait }: { portrait: boolean }) => {
  const t = useCurrentFrame() / useVideoConfig().fps;
  const x = portrait ? 80 : 92;
  const headline =
    t < 8
      ? "The desktop\nphoto + video studio."
      : t < 15
        ? "Start with\nthe RAW."
        : t < 23
          ? "A custom\ncolor engine."
          : "Every pixel.\nOn your terms.";
  const start = t < 8 ? 3.8 : t < 15 ? 8 : t < 23 ? 15 : 23;
  const p = out(t, start, start + 0.55);
  return (
    <div
      style={{
        position: "absolute",
        left: x,
        top: portrait ? 174 : 66,
        width: portrait ? 920 : 1690,
        opacity: p,
        translate: `0 ${22 * (1 - p)}px`,
        color: paper,
      }}
    >
      <div
        style={{
          fontFamily: font.mono,
          fontSize: portrait ? 25 : 23,
          letterSpacing: 1.5,
          color: "#c5cbd5",
          marginBottom: portrait ? 26 : 16,
        }}
      >
        {t < 8 ? "INTRODUCING LOOKSY" : "01 / THE PHOTO EDITOR"}
      </div>
      <div
        style={{
          fontFamily: t >= 23 ? font.serif : font.sans,
          fontStyle: t >= 23 ? "italic" : "normal",
          fontWeight: 500,
          fontSize: portrait ? 100 : 83,
          lineHeight: 0.99,
          letterSpacing: portrait ? -4 : -3,
          whiteSpace: portrait ? "pre-line" : "normal",
        }}
      >
        {portrait ? headline : headline.replace("\n", " ")}
      </div>
      <div
        style={{
          fontFamily: font.sans,
          fontSize: portrait ? 37 : 31,
          lineHeight: 1.3,
          color: "#b8c3ce",
          marginTop: portrait ? 30 : 17,
        }}
      >
        {t < 8
          ? "Built from the ground up."
          : t < 15
            ? "Camera RAW. Native resolution."
            : t < 23
              ? "Exposure. Color. Detail."
              : "6024 × 4020 source pixels."}
      </div>
    </div>
  );
};

const OpeningBrand = ({ portrait }: { portrait: boolean }) => {
  const { width, height, fps } = useVideoConfig(),
    t = useCurrentFrame() / fps;
  const enter = out(t, 0, 0.85),
    exit = ease(t, 2.8, 4.0);
  const letters = [
    "P",
    "h",
    "o",
    "t",
    "o",
    ".",
    " ",
    "V",
    "i",
    "d",
    "e",
    "o",
    ".",
  ];
  return (
    <div
      style={{
        position: "absolute",
        inset: 0,
        opacity: 1 - exit,
        translate: `0 ${-60 * exit}px`,
        display: "flex",
        alignItems: "center",
        justifyContent: "center",
      }}
    >
      <div
        style={{
          width: portrait ? 920 : 1550,
          translate: `0 ${30 * (1 - enter)}px`,
          opacity: enter,
        }}
      >
        <AppWordmark width={portrait ? 740 : 970} />
        <div
          style={{
            marginTop: 42,
            fontFamily: font.serif,
            fontStyle: "italic",
            fontSize: portrait ? 96 : 103,
            color: paper,
            lineHeight: 1.1,
          }}
        >
          {letters.map((letter, i) => (
            <span
              key={i}
              style={{
                display: "inline-block",
                whiteSpace: "pre",
                translate: `0 ${38 * (1 - out(t, 0.58 + i * 0.035, 1.18 + i * 0.035))}px`,
                opacity: out(t, 0.58 + i * 0.035, 0.88 + i * 0.035),
              }}
            >
              {letter}
            </span>
          ))}
        </div>
        <div
          style={{
            marginTop: 26,
            fontFamily: font.mono,
            fontSize: portrait ? 25 : 27,
            letterSpacing: 2,
            color: "#c4cad0",
          }}
        >
          MADE FOR YOUR DESKTOP.
        </div>
      </div>
      <div
        style={{
          position: "absolute",
          left: width * 0.05,
          top: height * 0.07,
          fontFamily: font.mono,
          fontSize: 22,
          color: "#b9c0cb",
        }}
      >
        LOOKSY / INTRODUCING
      </div>
    </div>
  );
};

const ClosingBrand = ({ portrait }: { portrait: boolean }) => {
  const { fps } = useVideoConfig(),
    t = useCurrentFrame() / fps;
  const p = out(t, 28, 29.1);
  return (
    <AbsoluteFill
      style={{
        opacity: p,
        justifyContent: "center",
        padding: portrait ? 80 : 110,
        boxSizing: "border-box",
        background: `rgba(8,10,15,${0.88 * p})`,
      }}
    >
      <AppWordmark width={portrait ? 650 : 780} />
      <div
        style={{
          fontFamily: font.serif,
          fontStyle: "italic",
          fontSize: portrait ? 102 : 114,
          marginTop: 45,
          lineHeight: 1.02,
          color: paper,
        }}
      >
        A whole studio.
        <br />
        Built around your work.
      </div>
      <div
        style={{
          fontFamily: font.mono,
          fontSize: portrait ? 25 : 23,
          color: "#c4cbd6",
          marginTop: 35,
        }}
      >
        OPENING / NATIVE CAPTURE DIRECTION
      </div>
    </AbsoluteFill>
  );
};

export const NativeOpening = () => {
  const { width, height, fps } = useVideoConfig(),
    f = useCurrentFrame(),
    t = f / fps;
  const portrait = height > width;
  return (
    <AbsoluteFill style={{ background: ink, overflow: "hidden" }}>
      <DesktopLight
        t={t * 0.78}
        gather={0.25 * ease(t, 27, 32)}
        style={{ opacity: lerp(0.88, 0.23, ease(t, 3, 6)) }}
      />
      <div
        style={{
          position: "absolute",
          inset: 0,
          background:
            "linear-gradient(180deg,#080b1377 0%,transparent 45%,#080a10bb 100%)",
        }}
      />
      <NativeCamera portrait={portrait} />
      {t >= 3.8 && t < 28.2 ? <Headline portrait={portrait} /> : null}
      <OpeningBrand portrait={portrait} />
      <ClosingBrand portrait={portrait} />
      <Audio
        src={staticFile("audio/sun-in-your-eyes.mp3")}
        volume={(frame) =>
          0.69 * ease(frame / fps, 0, 0.5) * (1 - ease(frame / fps, 30.2, 32))
        }
      />
      <Sequence
        name="Optical arrival / restrained transition sound"
        from={3.5 * fps}
        durationInFrames={2 * fps}
      >
        <Audio
          src={staticFile("audio/foley/quick-air-woosh.mp3")}
          volume={0.07}
        />
      </Sequence>
      <Sequence
        name="Native control accent"
        from={11.5 * fps}
        durationInFrames={fps}
      >
        <Audio
          src={staticFile("audio/foley/interface-click.mp3")}
          volume={0.13}
        />
      </Sequence>
    </AbsoluteFill>
  );
};
