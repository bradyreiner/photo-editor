import { staticFile, Sequence, useVideoConfig } from "remotion";
import { AppIcon } from "../app-kit/AppElements";
import {
  A,
  Image,
  Tag,
  ink,
  lime,
  paper,
  mix,
  out,
  smooth,
  useTime,
} from "./Kit";
import { font } from "./Type";
import { WeddingCover } from "./Artwork";
import { LayeredDesign } from "./MotionProof";
import { ExpeditionPoster } from "./Artwork";
import { Cue, Dock, Slider, NativeResponse } from "./StoryboardShared";
import { Curve } from "../studio-film/Core";

export const WeddingStoryboard = () => {
  const t = useTime(),
    pull = smooth(t, 0.1, 1.4),
    sync = smooth(t, 7.5, 10.8),
    curate = smooth(t, 4.6, 5.4),
    finish = smooth(t, 11.5, 13.3);
  return (
    <>
      <div
        style={{
          ...A,
          left: mix(0, 80, pull),
          top: mix(0, 540, pull),
          width: mix(1080, 440, pull),
          height: mix(1920, 720, pull),
          overflow: "hidden",
          opacity: 1 - finish,
        }}
      >
        <WeddingCover imageSrc="campaign/wedding-bride-01.jpg" />
      </div>
      {[0, 1].map((i) => (
        <div
          key={i}
          style={{
            ...A,
            left: 80 + i * 480,
            top: finish > 0 ? mix(680, 530, finish) : 550,
            width: 440,
            height: 710,
            overflow: "hidden",
            opacity: pull,
            border: "2px solid #d7ff48",
            translate: `0 ${60 * (1 - out(t, 1.3 + i * 0.16, 2.2 + i * 0.16))}px`,
          }}
        >
          <Image src={`rebuild/wedding/0${i + 1}/batch-before.png`} />
          <Image
            src={`rebuild/wedding/0${i + 1}/batch-after.png`}
            style={{ ...A, inset: 0, opacity: sync }}
          />
          <Tag
            style={{
              ...A,
              left: 18,
              top: 18,
              background: "#0b0d12b0",
              padding: 10,
              color: paper,
            }}
          >
            BRIDAL / 00{i + 1}
          </Tag>
          <div
            style={{ ...A, left: 18, bottom: 18, color: lime, fontSize: 36 }}
          >
            ★★★★★
          </div>
        </div>
      ))}
      <div
        style={{
          ...A,
          left: 80,
          top: 1300,
          width: 920,
          display: "flex",
          gap: 20,
          opacity: pull * (1 - finish),
        }}
      >
        {[0, 1, 2, 3].map((i) => (
          <div
            key={i}
            style={{
              position: "relative",
              width: 215,
              height: 190,
              overflow: "hidden",
              opacity: i === 3 ? 1 - 0.8 * curate : 1,
              scale: i === 3 ? 1 - 0.12 * curate : 1,
            }}
          >
            <Image
              src={`campaign/wedding-bride-0${(i % 2) + 1}.jpg`}
              style={{
                scale: 1.7,
                objectPosition: i < 2 ? "50% 70%" : "50% 35%",
              }}
            />
            <Tag
              style={{
                ...A,
                left: 8,
                bottom: 8,
                fontSize: 18,
                background: ink,
              }}
            >
              DETAIL / 0{(i % 2) + 1}
            </Tag>
          </div>
        ))}
      </div>
      <Dock top={1530} style={{ opacity: pull }}>
        <Tag style={{ color: lime, marginBottom: 14 }}>
          {finish > 0.5
            ? "CLIENT GALLERY / CONSISTENT FINISH"
            : t < 7
              ? "IMPORT · RATE · CULL"
              : "SYNC SETTINGS / 2 SELECTED ORIGINALS"}
        </Tag>
        {t < 7 ? (
          <div
            style={{
              fontSize: 39,
              display: "flex",
              alignItems: "center",
              gap: 20,
            }}
          >
            <AppIcon name={curate > 0.5 ? "check" : "image"} size={36} />
            {curate > 0.5
              ? "Keep the selected originals."
              : "Review the shoot. Keep the details."}
          </div>
        ) : (
          <Slider
            label="Warmth"
            value={6 * sync}
            text={`+${Math.round(6 * sync)}`}
            min={-100}
            max={100}
            cursor={t < 11}
          />
        )}
      </Dock>
      <Cue id="wedding" />
    </>
  );
};

export const RawStoryboard = () => {
  const t = useTime(),
    stage = t < 4 ? 0 : t < 7 ? 1 : t < 10 ? 2 : t < 14 ? 3 : 4,
    starts = [0, 4, 7, 10, 14],
    ends = [4, 7, 10, 14, 17],
    u = smooth(t, starts[stage] + 0.4, ends[stage] - 0.4),
    compare = smooth(t, 17, 17.4) * (1 - smooth(t, 18.8, 19.2));
  return (
    <>
      <NativeResponse stage={stage} u={u} />
      <div
        style={{
          ...A,
          inset: 0,
          background:
            "linear-gradient(#0b0d12ca,transparent 30%,transparent 66%,#0b0d12bc)",
        }}
      />
      {compare > 0 && (
        <Image
          src="studio/color/0-0.png"
          style={{
            ...A,
            inset: 0,
            clipPath: `inset(0 ${100 - 50 * compare}% 0 0)`,
          }}
        />
      )}
      <Tag
        style={{
          ...A,
          left: 80,
          top: 450,
          color: paper,
          background: "#0b0d1280",
          padding: "10px 14px",
        }}
      >
        IMG_5106.CR2 / 4020 × 6024 ORIGINAL
      </Tag>
      <Dock top={1450}>
        <Tag style={{ color: lime, marginBottom: 18 }}>
          LOOKSY CUSTOM COLOR ENGINE / sRGB
        </Tag>
        {stage === 0 ? (
          <Slider
            label="Exposure"
            value={1.4 * u}
            text={`+${(1.4 * u).toFixed(2)} EV`}
            cursor
          />
        ) : stage === 1 ? (
          <>
            <Slider
              label="Highlights"
              value={-35 * u}
              text={`${Math.round(-35 * u)}`}
              min={-100}
              max={100}
              cursor
            />
            <Slider
              label="Shadows"
              value={35 * u}
              text={`+${Math.round(35 * u)}`}
              min={-100}
              max={100}
            />
          </>
        ) : stage === 2 ? (
          <Slider
            label="Temperature"
            value={16 * u}
            text={`+${Math.round(16 * u)}`}
            min={-100}
            max={100}
            cursor
          />
        ) : stage === 3 ? (
          <div style={{ display: "flex", alignItems: "center", gap: 40 }}>
            <Curve amount={u} />
            <div style={{ fontSize: 35 }}>
              RGB · Luma
              <br />
              Red · Green · Blue
            </div>
          </div>
        ) : (
          <>
            <Tag style={{ marginBottom: 18 }}>COLOR MIX / BLUES</Tag>
            <Slider
              label="Saturation"
              value={18 * u}
              text={`+${Math.round(18 * u)}%`}
              min={-100}
              max={100}
              cursor
            />
          </>
        )}
      </Dock>
      <Cue id="raw" />
    </>
  );
};

export const PixelStoryboard = () => {
  const t = useTime(),
    zoom = smooth(t, 0.3, 1.8),
    edit = smooth(t, 3.2, 4.5),
    scale = mix(1, 6, zoom),
    size = (1120 / 960) * scale,
    ox = (1080 - 1120 * scale) / 2,
    oy = (1120 - 1120 * scale) / 2,
    gx = ox + Math.round((500 - ox) / size) * size,
    gy = oy + Math.round((630 - oy) / size) * size;
  return (
    <>
      <div
        style={{
          ...A,
          left: 0,
          top: 480,
          width: 1080,
          height: 1120,
          overflow: "hidden",
        }}
      >
        <Image
          src="studio/color/native-detail.png"
          style={{ imageRendering: "pixelated", scale: mix(1, 6, zoom) }}
        />
        <svg
          viewBox="0 0 1080 1120"
          width="100%"
          height="100%"
          style={{ ...A, inset: 0 }}
        >
          <defs>
            <pattern
              id="native-grid"
              x={ox}
              y={oy}
              width={size}
              height={size}
              patternUnits="userSpaceOnUse"
            >
              <path
                d={`M${size} 0H0V${size}`}
                fill="none"
                stroke="#e5e9db80"
                strokeWidth={0.7}
              />
            </pattern>
            <mask id="native-pixel-brush">
              <rect
                x={gx}
                y={gy}
                width={size * 3}
                height={size * 3}
                fill="white"
              />
            </mask>
          </defs>
          <image
            href={staticFile("media/studio/color/native-detail-lift.png")}
            x={-2700}
            y={-2800}
            width={6480}
            height={6720}
            opacity={edit}
            mask="url(#native-pixel-brush)"
            preserveAspectRatio="xMidYMid slice"
          />
          <rect
            width={1080}
            height={1120}
            fill="url(#native-grid)"
            opacity={smooth(t, 1.7, 2.2)}
          />
          <rect
            x={gx}
            y={gy}
            width={size * 3}
            height={size * 3}
            fill="none"
            stroke={paper}
            strokeWidth={2}
            opacity={smooth(t, 2.3, 2.6)}
          />
        </svg>
      </div>
      <Dock top={1600}>
        <Tag style={{ color: lime }}>NATIVE SOURCE PIXELS / 3 PX BRUSH</Tag>
        <div style={{ fontSize: 36, marginTop: 20 }}>
          Original-size detail. Precise control.
        </div>
      </Dock>
      <Cue id="pixel" />
    </>
  );
};

export const RetouchStoryboard = () => {
  const t = useTime(),
    zoom = smooth(t, 0.7, 2.3) * (1 - smooth(t, 5.5, 7.1)),
    mark = smooth(t, 2.3, 2.65) * (1 - smooth(t, 3.7, 4)),
    fill = smooth(t, 4, 4.7);
  return (
    <>
      <svg
        width={1080}
        height={820}
        viewBox={`${mix(87.805, 453, zoom)} ${mix(0, 755, zoom)} ${mix(1264.39, 270, zoom)} ${mix(960, 205, zoom)}`}
        style={{ ...A, left: 0, top: 520 }}
      >
        <image
          href={staticFile("media/studio/raw-colour/4-8.png")}
          width={1440}
          height={960}
        />
        <image
          href={staticFile("media/studio/retouch-final.png")}
          width={1440}
          height={960}
          opacity={fill}
        />
        <image
          href={staticFile("media/studio/retouch-overlay.png")}
          width={1440}
          height={960}
          opacity={mark * 0.8}
        />
      </svg>
      <Dock top={1450}>
        <Tag style={{ color: lime }}>RETOUCH / LOCAL INPAINTING</Tag>
        <div style={{ fontSize: 38, marginTop: 25 }}>
          {fill > 0.99
            ? "✓ Distraction removed."
            : mark > 0.2
              ? "Paint the area."
              : "Find the distraction in context."}
        </div>
        <div style={{ fontSize: 32, color: "#aab3c0", marginTop: 20 }}>
          Surrounding source pixels remain intact.
        </div>
      </Dock>
      <Cue id="retouch" />
    </>
  );
};

export const DesignStoryboard = () => {
  const t = useTime(),
    { fps } = useVideoConfig(),
    off = smooth(t, 14.5, 14.8) * (1 - smooth(t, 15.9, 16.2)),
    serif = t > 11.8 && t < 13.2;
  return (
    <>
      {t < 3.5 && (
        <div style={{ ...A, left: 100, top: 510, width: 880, height: 1100 }}>
          <ExpeditionPoster build={out(t, 0.3, 1.6)} />
        </div>
      )}
      <Sequence
        name="Layer camera and live tracking"
        from={3.5 * fps}
        durationInFrames={7.5 * fps}
      >
        <LayeredDesign />
      </Sequence>
      {t >= 11 && (
        <>
          <div style={{ ...A, left: 100, top: 510, width: 880, height: 1100 }}>
            <ExpeditionPoster
              foreground={1 - off}
              tracking={3}
              mapTypeface={serif ? font.serif : font.display}
            />
          </div>
          <Dock top={1640}>
            <Tag style={{ color: lime }}>TEXT / THE MAP</Tag>
            <div style={{ fontSize: 35, marginTop: 15 }}>
              Typeface: {serif ? "Instrument Serif" : "Anton"} · Subject mask{" "}
              {off > 0.5 ? "hidden" : "visible"}
            </div>
          </Dock>
        </>
      )}
      <div
        style={{
          ...A,
          left: 0,
          top: 0,
          width: 1080,
          height: 460,
          background: ink,
        }}
      />
      <Cue id="design" />
    </>
  );
};
