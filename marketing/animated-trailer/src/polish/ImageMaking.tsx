import { useId } from "react";
import {
  CanvasImage,
  Sequence,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { AppIcon, AppInspectorSlider } from "../app-kit/AppElements";
import {
  A,
  asset,
  C,
  Copy,
  Grain,
  land,
  lerp,
  mono,
  p,
  Photo,
  Plate,
} from "../launch/design";
import { CampaignArtwork, CampaignPreview, TextMaskArtwork } from "./Artwork";
import { subjectContour } from "./subject-contour";
import { TypographyFields } from "../launch/NativeDetails";

const NativeStrip = ({
  children,
  top = 1430,
  opacity = 1,
}: {
  children: React.ReactNode;
  top?: number;
  opacity?: number;
}) => (
  <div
    style={{
      ...A,
      left: 80,
      top,
      width: 920,
      background: C.ink900,
      padding: "24px 22px",
      boxSizing: "border-box",
      opacity,
    }}
  >
    {children}
  </div>
);

export const PixelShot = ({ t }: { t: number }) => {
  // Begin at the exact crop and 155% loupe position of ColorDetailShot.
  const enter = p(t, 0, 1.25),
    leave = p(t, 2.9, 4),
    zoom = enter * (1 - leave),
    k = lerp(lerp(1.55, 19, enter), 1, leave),
    focusX = lerp(lerp(591.48387, 566.5, enter), 600, leave),
    focusY = lerp(lerp(940.26648, 1009.5, enter), 900, leave),
    pictureHeight = lerp(870, 1380, enter);
  const painted = Math.floor(18 * p(t, 1.25, 1.8));
  const erased = Math.floor(18 * p(t, 1.9, 2.15) * (1 - p(t, 2.3, 2.45)));
  const cleared = t >= 2.72;
  const cursorIndex = Math.min(17, Math.max(0, t < 1.9 ? painted : erased));
  return (
    <Plate>
      <CanvasImage
        src={asset("range/portrait-1200")}
        style={{ ...A, width: 1, height: 1, opacity: 0 }}
      />
      <div
        style={{
          ...A,
          left: 80,
          top: lerp(390, 230, enter),
          width: 920,
          height: pictureHeight,
          overflow: "hidden",
        }}
      >
        <svg
          width={920}
          height={pictureHeight}
          viewBox="0 0 1200 1800"
          preserveAspectRatio="xMidYMid slice"
        >
          <defs>
            <pattern
              id="source-pixels"
              width={1}
              height={1}
              patternUnits="userSpaceOnUse"
            >
              <path
                d="M1 0H0V1"
                stroke="white"
                opacity={0.2}
                strokeWidth={0.035}
                fill="none"
              />
            </pattern>
          </defs>
          <g
            transform={`translate(600 900) scale(${k}) translate(${-focusX} ${-focusY})`}
          >
            <image
              href={asset("range/portrait-1200")}
              width={1200}
              height={1800}
              style={{ imageRendering: k > 4 ? "pixelated" : "auto" }}
            />
            {Array.from({ length: 18 }, (_, i) => (
              <rect
                key={i}
                x={564 + (i % 6)}
                y={1008 + Math.floor(i / 6)}
                width={1}
                height={1}
                fill={C.brandBlue}
                opacity={!cleared && i < painted && i >= erased ? 1 : 0}
              />
            ))}
            {t > 1.25 && t < 2.2 && (
              <circle
                cx={564.5 + (cursorIndex % 6)}
                cy={1008.5 + Math.floor(cursorIndex / 6)}
                r={0.7}
                fill="none"
                stroke="white"
                strokeWidth={0.06}
              />
            )}
            <rect
              x={530}
              y={970}
              width={80}
              height={80}
              fill="url(#source-pixels)"
              opacity={p(zoom, 0.4, 0.9)}
            />
          </g>
          <g
            fill="none"
            stroke="white"
            strokeWidth={1.5}
            opacity={p(zoom, 0.3, 0.8)}
          >
            <path d="M580 900H620M600 880V920" />
            <rect x={545} y={845} width={110} height={110} />
          </g>
        </svg>
      </div>
      <NativeStrip
        top={1430}
        opacity={land(t, 0.75, 1.15) * (1 - p(t, 3.5, 4))}
      >
        <div
          style={{
            display: "flex",
            alignItems: "center",
            justifyContent: "space-between",
            fontSize: 34,
          }}
        >
          <span>Pixel layer</span>
          <span style={{ fontFamily: mono }}>{Math.round(k * 100)}%</span>
        </div>
        <div
          style={{
            display: "flex",
            gap: 40,
            marginTop: 23,
            fontSize: 31,
            color: C.textMuted,
          }}
        >
          <span style={{ color: t < 1.9 ? C.brandPaper : C.textMuted }}>
            Brush
          </span>
          <span
            style={{ color: t >= 1.9 && t < 2.3 ? C.brandPaper : C.textMuted }}
          >
            Erase
          </span>
          <span style={{ color: t >= 2.3 ? C.brandPaper : C.textMuted }}>
            Undo
          </span>
        </div>
      </NativeStrip>
      <div style={{ ...A, left: 80, top: 1700, opacity: 1 - p(t, 3.5, 4) }}>
        <Copy t={t} start={0.2} size={104}>
          <div>Every pixel.</div>
        </Copy>
      </div>
      <Grain />
    </Plate>
  );
};

export const MaskShot = ({ t }: { t: number }) => {
  const select = land(t, 0.2, 1.2),
    isolate = p(t, 2.2, 3.4) * (1 - p(t, 5.35, 6)),
    feather = p(t, 3.5, 4.6, 0, 22);
  return (
    <Plate>
      <div
        style={{
          ...A,
          left: 80,
          top: 120,
          display: "flex",
          gap: 22,
          alignItems: "center",
          fontSize: 38,
        }}
      >
        <AppIcon name="layers" size={36} />
        Subject mask
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 230,
          width: 920,
          height: 1380,
          overflow: "hidden",
        }}
      >
        <Photo
          name="range/portrait-1200"
          style={{ filter: `brightness(${1 - isolate * 0.55})` }}
        />
        <CanvasImage
          src={staticFile("media/range/portrait-subject.png")}
          style={{
            ...A,
            inset: 0,
            width: "100%",
            height: "100%",
            opacity: isolate,
            filter: `blur(${feather * 0.08}px)`,
          }}
        />
        <svg
          width="100%"
          height="100%"
          viewBox="0 0 1200 1800"
          style={{ ...A, inset: 0 }}
        >
          <path
            d={subjectContour}
            fill={C.brandRed}
            opacity={select * (1 - p(t, 1.9, 2.7)) * 0.24}
          />
          <path
            d={subjectContour}
            fill="none"
            stroke="white"
            strokeWidth={2.1}
            strokeDasharray="4 5"
            strokeDashoffset={-t * 5}
            opacity={select}
          />
          <circle
            cx={lerp(610, 648, p(t, 3.5, 4.6))}
            cy={lerp(935, 1110, p(t, 3.5, 4.6))}
            r={30}
            fill="none"
            stroke="white"
            strokeWidth={2}
            opacity={land(t, 3.3, 3.5) * (1 - p(t, 4.7, 5))}
          />
        </svg>
      </div>
      <NativeStrip top={1430}>
        <AppInspectorSlider
          label="Feather"
          value={feather}
          valueText={`${Math.round(feather)} px`}
          from={0}
          to={100}
          scale={2.8}
          width={313}
          press={p(t, 3.4, 3.5) * (1 - p(t, 4.6, 4.7))}
          showCursor={t > 3.5 && t < 4.7}
        />
      </NativeStrip>
      <div
        style={{
          ...A,
          left: 80,
          top: 1700,
          fontSize: 65,
          fontWeight: 500,
          letterSpacing: -2,
        }}
      >
        Select. Refine. Mask.
      </div>
      <Grain />
    </Plate>
  );
};

const LayerContent = ({ t }: { t: number }) => {
  const split = land(t, 0.9, 1.7) * (1 - p(t, 2.0, 3.25));
  const format = p(t, 0, 0.8);
  const artTop = lerp(230, 300, format);
  const artHeight = lerp(1380, 1150, format);
  return (
    <>
      <div
        style={{
          ...A,
          left: 80,
          top: 140,
          fontSize: 42,
          opacity: land(t, 0.35, 0.85),
        }}
      >
        Image layers. Editable type.
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: artTop,
          width: 920,
          height: artHeight,
          transform: `perspective(2000px) rotateY(${-split * 16}deg) rotateZ(${-split * 3}deg)`,
        }}
      >
        <CampaignArtwork t={t} plane="base" sourceToDesign={format} />
        <CampaignArtwork t={t} plane="graphics" sourceToDesign={format} />
      </div>
      <div
        style={{
          ...A,
          left: 80 + split * 250,
          top: artTop - split * 100,
          width: 920,
          height: artHeight,
          transform: `perspective(2000px) rotateY(${-split * 18}deg) rotateZ(${split * 3}deg)`,
        }}
      >
        <CampaignArtwork t={t} plane="type" sourceToDesign={format} />
      </div>
      <div
        style={{
          ...A,
          left: 80 - split * 190,
          top: artTop + split * 110,
          width: 920,
          height: artHeight,
          transform: `perspective(2000px) rotateY(${-split * 28}deg)`,
        }}
      >
        <CampaignArtwork t={t} plane="subject" sourceToDesign={format} />
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1530,
          width: 920,
          padding: "20px 26px",
          boxSizing: "border-box",
          background: C.ink900,
          opacity: land(t, 0.5, 1.0),
        }}
      >
        {[
          ["text", "FORM. / editable type"],
          ["image", "Graphics / editable layer"],
          ["layers", "Subject / live mask"],
          ["image", "Original photograph"],
        ].map(([icon, label], i) => (
          <div
            key={label}
            style={{
              display: "flex",
              alignItems: "center",
              gap: 20,
              height: 55,
              fontSize: 29,
              background:
                Math.floor(p(t, 1, 4, 0, 3)) === i ? C.ink800 : "transparent",
              padding: "0 13px",
            }}
          >
            <AppIcon name={icon} size={27} />
            {label}
          </div>
        ))}
      </div>
    </>
  );
};

const TypeContent = ({ t }: { t: number }) => {
  return (
    <>
      <TextMaskArtwork t={t} />
      <div style={{ ...A, left: 80, top: 1330 }}>
        <Copy t={t} start={0.1} size={103}>
          <div>Editable type.</div>
          <div>Text masks.</div>
        </Copy>
      </div>
      <NativeStrip top={1600}>
        <TypographyFields t={t} />
      </NativeStrip>
    </>
  );
};

// The adjacent canvases share one camera across the sequence boundary.
const CanvasTravel = ({
  t,
  children,
}: {
  t: number;
  children: React.ReactNode;
}) => {
  const { fps } = useVideoConfig();
  const id = useId().replace(/:/g, "");
  const travel = p(t, 5.55, 6.55, 0, 1080);
  const blur = Math.min(
    6,
    Math.abs(travel - p(t - 1 / fps, 5.55, 6.55, 0, 1080)) * 0.1,
  );
  return (
    <>
      <svg width={0} height={0} style={A}>
        <defs>
          <filter
            id={id}
            x="-5%"
            y="-5%"
            width="110%"
            height="110%"
            colorInterpolationFilters="sRGB"
          >
            <feGaussianBlur stdDeviation={`${blur} 0`} />
          </filter>
        </defs>
      </svg>
      <div
        style={{
          ...A,
          left: 0,
          top: 0,
          width: 2160,
          height: 1920,
          translate: `${-travel}px 0`,
          filter: blur > 0.15 ? `url(#${id})` : undefined,
        }}
      >
        {children}
      </div>
    </>
  );
};
export const LayerShot = ({ t }: { t: number }) => (
  <Plate>
    <CanvasTravel t={t}>
      <div style={{ ...A, left: 0, top: 0, width: 1080, height: 1920 }}>
        <LayerContent t={t} />
      </div>
      <div style={{ ...A, left: 1080, top: 0, width: 1080, height: 1920 }}>
        <TypeContent t={0} />
      </div>
    </CanvasTravel>
    <Grain />
  </Plate>
);
export const TypeShot = ({ t }: { t: number }) => (
  <Plate>
    <CanvasTravel t={t + 6}>
      <div style={{ ...A, left: 0, top: 0, width: 1080, height: 1920 }}>
        <LayerContent t={6} />
      </div>
      <div style={{ ...A, left: 1080, top: 0, width: 1080, height: 1920 }}>
        <TypeContent t={t} />
      </div>
    </CanvasTravel>
    <Grain />
  </Plate>
);

export const PaintShot = ({ t }: { t: number }) => {
  const draw = p(t, 0.4, 1.7),
    undo = p(t, 2.35, 2.6) * (1 - p(t, 3.15, 3.4)),
    paint = draw * (1 - undo);
  const q = 1 - draw,
    cx =
      q * q * q * 65 +
      3 * q * q * draw * 230 +
      3 * q * draw * draw * 400 +
      draw * draw * draw * 725,
    cy =
      q * q * q * 1138 +
      3 * q * q * draw * 1010 +
      3 * q * draw * draw * 1265 +
      draw * draw * draw * 1100;
  return (
    <Plate>
      <div style={{ ...A, left: 80, top: 140, fontSize: 42 }}>
        Paint. Shapes. Your own touch.
      </div>
      <div style={{ ...A, left: 80, top: 300, width: 920, height: 1150 }}>
        <CampaignArtwork t={8} paintOverride={paint} />
        <svg
          width={920}
          height={1150}
          viewBox="0 0 1200 1500"
          style={{ ...A, inset: 0 }}
        >
          <circle
            cx={cx}
            cy={cy}
            r={28}
            fill="none"
            stroke="white"
            strokeWidth={2.5}
            opacity={land(t, 0.2, 0.4) * (1 - p(t, 1.75, 2))}
          />
        </svg>
      </div>
      <NativeStrip top={1530}>
        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: 20,
            fontSize: 34,
          }}
        >
          <AppIcon name="layers" size={33} />
          Paint layer
        </div>
        <div
          style={{
            display: "flex",
            gap: 26,
            alignItems: "center",
            marginTop: 28,
            fontSize: 30,
          }}
        >
          <span style={{ width: 40, height: 40, background: C.brandBlue }} />
          <span>Brush / 25 px</span>
          <span
            style={{
              marginLeft: "auto",
              color: undo > 0.5 ? C.brandPaper : C.textMuted,
            }}
          >
            Undo
          </span>
          <span style={{ color: t > 3.15 ? C.brandPaper : C.textMuted }}>
            Redo
          </span>
        </div>
      </NativeStrip>
      <Grain />
    </Plate>
  );
};

export const EffectsShot = ({ t }: { t: number }) => {
  const amount =
    p(t, 0.5, 1.7, 0, 24) * (1 - p(t, 2.4, 2.8)) + p(t, 3.2, 3.6, 0, 24);
  return (
    <Plate>
      <div style={{ ...A, left: 80, top: 110, opacity: 1 - p(t, 4.5, 5) }}>
        <Copy t={t} start={0} size={108}>
          <div>Build the look.</div>
        </Copy>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: land(t, 0, 0.5, 300, 340),
          width: 920,
          height: 1150,
          overflow: "hidden",
        }}
      >
        <CampaignPreview diffusion={amount} grain={p(t, 3.7, 4.4, 0, 0.075)} />
      </div>
      <NativeStrip top={land(t, 0, 0.5, 1530, 1560)} opacity={1 - p(t, 4.5, 5)}>
        <div
          style={{
            display: "flex",
            justifyContent: "space-between",
            fontSize: 34,
            marginBottom: 18,
          }}
        >
          <span>Optical diffusion</span>
          <span
            style={{ color: t > 2.4 && t < 3.2 ? C.brandPaper : C.textMuted }}
          >
            Bypass
          </span>
        </div>
        <AppInspectorSlider
          label="Amount"
          value={amount}
          valueText={`${Math.round(amount)}%`}
          from={0}
          to={100}
          scale={2.8}
          width={313}
          press={p(t, 0.4, 0.5) * (1 - p(t, 1.7, 1.8))}
          showCursor={t > 0.5 && t < 1.8}
        />
      </NativeStrip>
      <Grain />
    </Plate>
  );
};

const PixelScene = () => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <PixelShot t={f / fps} />;
};
const MaskScene = ({ rate = 1 }: { rate?: number }) => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <MaskShot t={(f / fps) * rate} />;
};
const LayerScene = () => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <LayerShot t={f / fps} />;
};
const TypeScene = () => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <TypeShot t={f / fps} />;
};
const PaintScene = () => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <PaintShot t={f / fps} />;
};
const EffectsScene = ({ rate = 1 }: { rate?: number }) => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <EffectsShot t={(f / fps) * rate} />;
};
export const ImageMaking = ({ paced = false }: { paced?: boolean }) => {
  const { fps } = useVideoConfig();
  return (
    <>
      <Sequence
        name="Source pixels / paint / erase / undo"
        durationInFrames={4 * fps}
      >
        <PixelScene />
      </Sequence>
      <Sequence
        name="Subject selection / refine / local mask"
        from={4 * fps}
        durationInFrames={(paced ? 4 : 6) * fps}
      >
        <MaskScene rate={paced ? 1.5 : 1} />
      </Sequence>
      <Sequence
        name="Layered campaign / original / type / mask / image"
        from={(paced ? 8 : 10) * fps}
        durationInFrames={6 * fps}
      >
        <LayerScene />
      </Sequence>
      <Sequence
        name="Editable typography / actual image-filled text"
        from={(paced ? 14 : 16) * fps}
        durationInFrames={4 * fps}
      >
        <TypeScene />
      </Sequence>
      <Sequence
        name="Paint / editable layer / undo / redo"
        from={(paced ? 18 : 20) * fps}
        durationInFrames={4 * fps}
      >
        <PaintScene />
      </Sequence>
      <Sequence
        name="Editable effect / amount / bypass / grain"
        from={(paced ? 22 : 24) * fps}
        durationInFrames={(paced ? 4 : 5) * fps}
      >
        <EffectsScene rate={paced ? 1.25 : 1} />
      </Sequence>
    </>
  );
};
