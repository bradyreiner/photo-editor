import { Sequence, useVideoConfig } from "remotion";
import { Video } from "@remotion/media";
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
import {
  ExpeditionPoster,
  NightThumbnail,
  MountainThumbnail,
  WeddingCover,
} from "./Artwork";
import { Spectral } from "./Spectral";
import { AppWordmark } from "../app-kit/AppElements";
import {
  Cue,
  Dock,
  Slider,
  NativeResponse,
  asset,
  filmStyle,
} from "./StoryboardShared";
import { sceneSamples } from "../studio-film/scene-data";

export const ThumbnailStoryboard = () => {
  const t = useTime(),
    second = smooth(t, 3.7, 4.5),
    enter = smooth(t, 6.7, 8);
  return (
    <>
      <Spectral t={t + 91} style={{ opacity: 0.2 * (1 - enter) }} />
      <div
        style={{
          ...A,
          left: 80,
          top: 600,
          width: 920,
          height: 517.5,
          translate: `${-1100 * second}px 0`,
          rotate: `${-5 * second}deg`,
        }}
      >
        <NightThumbnail />
      </div>
      <div
        style={{
          ...A,
          left: 80 + 1100 * (1 - second),
          top: 600,
          width: 920,
          height: 517.5,
          scale: 1 + 0.16 * enter,
          opacity: 1 - enter,
        }}
      >
        <MountainThumbnail />
      </div>
      <Tag
        style={{ ...A, left: 80, top: 1300, color: lime, opacity: 1 - enter }}
      >
        16:9 / LIVE TYPE + PHOTOGRAPH + GRAPHICS
      </Tag>
      <div
        style={{
          ...A,
          left: 80,
          top: 1370,
          fontFamily: font.serif,
          fontSize: 88,
          fontStyle: "italic",
          opacity: 1 - enter,
        }}
      >
        A different format.
        <br />A different idea.
      </div>
      <div
        style={{
          ...A,
          left: 60,
          top: 620,
          width: 960,
          height: 600,
          overflow: "hidden",
          opacity: enter,
        }}
      >
        <div style={{ ...A, left: -60, top: 0, width: 1080, height: 600 }}>
          <Image src="IMG_5071.jpg" />
        </div>
      </div>
      <Cue id="thumbnails" />
    </>
  );
};

export const CarouselStoryboard = () => {
  const t = useTime(),
    { fps } = useVideoConfig(),
    split = smooth(t, 0.8, 2.4),
    crop = 48 * smooth(t, 3.7, 5.1) * (1 - smooth(t, 6.2, 6.8)),
    preview = smooth(t, 7, 7.6) * (1 - smooth(t, 9.3, 10)),
    swipe = smooth(t, 7.8, 9),
    all = smooth(t, 9.5, 10.7),
    enter = smooth(t, 12.35, 14);
  const w = mix(mix(480, 560, preview), 220, all),
    gap = mix(24 * split * (1 - preview), 20, all),
    left = mix(mix((1080 - 960 - 24 * split) / 2, 260, preview), 60, all),
    top = 620,
    h = w * 1.25;
  return (
    <>
      <div
        style={{
          ...A,
          left,
          top,
          width: mix(mix(960 + gap, 560, preview), 980, all),
          height: h,
          overflow: "hidden",
          opacity: 1 - enter,
        }}
      >
        {[0, 1].map((i) => (
          <div
            key={i}
            style={{
              ...A,
              left: i * (w + gap) - preview * swipe * w,
              top: 0,
              width: w,
              height: h,
              overflow: "hidden",
            }}
          >
            <div
              style={{
                ...A,
                left: -i * w - 60 + crop,
                top: 0,
                width: 2 * w + 120,
                height: h,
              }}
            >
              <Image src="IMG_5071.jpg" />
            </div>
            <Tag
              style={{
                ...A,
                left: 14,
                top: 14,
                color: paper,
                background: ink,
                padding: 6,
                opacity: split * (1 - preview),
              }}
            >
              0{i + 1}
            </Tag>
          </div>
        ))}
      </div>
      <div
        style={{
          ...A,
          left: 60 + 2 * (w + gap),
          top,
          width: w,
          height: h,
          opacity: all * (1 - enter),
        }}
      >
        <ExpeditionPoster />
      </div>
      <div
        style={{
          ...A,
          left: mix(780, 0, enter),
          top: mix(620, 620, enter),
          width: mix(220, 1080, enter),
          height: mix(275, 607.5, enter),
          opacity: all,
          overflow: "hidden",
        }}
      >
        <Image src="studio/arrival-first.jpg" />
        <Sequence
          name="Moving fourth canvas / source clock into teaser"
          from={10 * fps}
          durationInFrames={4 * fps}
          layout="none"
        >
          <Video
            src={asset("refine/arrival.mp4")}
            muted
            objectFit="cover"
            style={{
              position: "absolute",
              inset: 0,
              width: "100%",
              height: "100%",
            }}
          />
        </Sequence>
        <Tag
          style={{ ...A, left: 14, top: 14, color: paper, opacity: 1 - enter }}
        >
          04 / VIDEO
        </Tag>
      </div>
      <Dock top={1450} style={{ opacity: 1 - enter }}>
        <Tag style={{ color: lime, marginBottom: 20 }}>
          {all > 0.5
            ? "ORDERED MIXED-MEDIA EXPORT"
            : preview > 0.5
              ? "SWIPE PREVIEW"
              : "SHARED PHOTO / LINKED POSITION"}
        </Tag>
        {all > 0.5 ? (
          <div style={{ fontSize: 36 }}>01.png · 02.png · 03.png · 04.mp4</div>
        ) : preview > 0.5 ? (
          <div style={{ textAlign: "center", fontSize: 40 }}>● ━ ●</div>
        ) : (
          <Slider
            label="Position X"
            value={crop}
            text={`${Math.round(crop)} px`}
            min={-100}
            max={100}
            cursor={t > 3.7 && t < 5.1}
          />
        )}
      </Dock>
      <Cue id="carousel" />
    </>
  );
};

export const GaussianStoryboard = () => {
  const t = useTime(),
    strip = smooth(t, 7.1, 8.2),
    { fps } = useVideoConfig(),
    sample = sceneSamples[Math.min(359, Math.floor((t + 1) * 30))];
  return (
    <>
      <div
        style={{
          ...A,
          left: 0,
          top: 480,
          width: 1080,
          height: 900,
          scale: 1 - 0.18 * strip,
          translate: `0 ${-65 * strip}px`,
          mixBlendMode: "screen",
        }}
      >
        <Video
          src={asset("studio/gaussian-camera.mp4")}
          trimBefore={fps}
          muted
          objectFit="contain"
          style={filmStyle}
        />
      </div>
      <Dock top={1440} style={{ opacity: 1 - strip }}>
        <Tag style={{ color: lime, marginBottom: 20 }}>
          PHOTOGRAPHIC GAUSSIAN / NATIVE CAMERA RENDER
        </Tag>
        {t < 3.4 ? (
          <Slider
            label="Camera yaw"
            value={sample.yawDegrees}
            text={`${sample.yawDegrees.toFixed(1)}°`}
            min={-90}
            max={90}
            cursor
          />
        ) : (
          <Slider
            label="Focus distance"
            value={sample.focusDistance}
            text={`${sample.focusDistance.toFixed(2)} u`}
            min={0.5}
            max={3}
            cursor
          />
        )}
      </Dock>
      <div
        style={{
          ...A,
          left: 80,
          top: 1410,
          width: 920,
          display: "flex",
          gap: 15,
          opacity: strip,
        }}
      >
        {[60, 120, 200, 270, 320].map((n) => (
          <div
            key={n}
            style={{
              width: 172,
              height: 194,
              border: "1px solid #56626b",
              overflow: "hidden",
            }}
          >
            <Image src={`studio/scene/${String(n).padStart(4, "0")}.png`} />
          </div>
        ))}
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1670,
          fontSize: 38,
          color: lime,
          opacity: strip,
        }}
      >
        Saved stills + camera move → creative assets
      </div>
      <Cue id="gaussian" />
    </>
  );
};

const pairs = [
  ["studio/color/0-0.png", "studio/color/0-4.png"],
  ["studio/batch-5103/batch-before.png", "studio/batch-5103/batch-after.png"],
  ["studio/batch-5111/batch-before.png", "studio/batch-5111/batch-after.png"],
];
export const AgentStoryboard = () => {
  const t = useTime(),
    query = "yellow jacket in the mountains",
    typed = query.slice(0, Math.floor(query.length * smooth(t, 0.3, 2.5))),
    agent = smooth(t, 4, 5.2),
    apply = smooth(t, 7.6, 10.5),
    undo = smooth(t, 11.2, 11.6) * (1 - smooth(t, 12.2, 12.6)),
    manual = smooth(t, 14.3, 16.2),
    adjust = 0.8 * apply * (1 - undo) - 0.08 * manual;
  return (
    <>
      <Dock top={450} style={{ opacity: 1 - agent }}>
        <Tag style={{ color: lime, marginBottom: 18 }}>
          LOCAL LIBRARY SEARCH
        </Tag>
        <div style={{ fontSize: 35 }}>⌕ {typed}</div>
      </Dock>
      {pairs.map((p, i) => (
        <div
          key={i}
          style={{
            ...A,
            left: mix(80 + i * 316, i === 0 ? 80 : 1200 + i * 60, agent),
            top: mix(760, 570, agent),
            width: mix(288, 600, agent),
            height: mix(432, 900, agent),
            overflow: "hidden",
            opacity: i === 0 ? 1 : 1 - agent,
          }}
        >
          {i === 0 ? (
            <NativeResponse stage={0} u={adjust / 1.4} />
          ) : (
            <>
              <Image src={p[0]} />
              <Image
                src={p[1]}
                style={{ ...A, inset: 0, opacity: adjust / 0.8 }}
              />
            </>
          )}
        </div>
      ))}
      <div
        style={{
          ...A,
          left: 720,
          top: 590,
          width: 300,
          padding: 25,
          background: "#141e29",
          border: "1px solid #55718d",
          opacity: agent,
          borderRadius: 13,
        }}
      >
        <Tag style={{ color: lime }}>OPTIONAL LOCAL MODEL</Tag>
        <div style={{ fontSize: 38, lineHeight: 1.3, marginTop: 30 }}>
          Exposure
          <br />
          +0.80 EV
        </div>
        <div style={{ fontSize: 28, color: "#aebdca", marginTop: 20 }}>
          3 selected originals
        </div>
        <div style={{ fontSize: 30, marginTop: 40 }}>
          {apply > 0.99 ? "✓ Applied" : "Review → Apply"}
        </div>
      </div>
      <Dock top={1510} style={{ opacity: agent }}>
        <Tag style={{ color: lime, marginBottom: 18 }}>
          EDITABLE ADJUSTMENT / FULL HISTORY
        </Tag>
        <Slider
          label="Exposure"
          value={adjust}
          text={`+${adjust.toFixed(2)} EV`}
          cursor={t > 14.3 && t < 16.2}
        />
        <div style={{ fontSize: 30, color: undo > 0.5 ? paper : "#aebdca" }}>
          Undo · Redo · Refine by hand
        </div>
      </Dock>
      <div
        style={{
          ...A,
          left: 80,
          top: 1770,
          fontSize: 41,
          color: lime,
          opacity: smooth(t, 14, 14.3),
        }}
      >
        No image generation.
      </div>
      <Cue id="agent" />
    </>
  );
};

export const ClosingStoryboard = () => {
  const t = useTime(),
    brand = smooth(t, 3.2, 4.6);
  return (
    <>
      <Spectral
        t={t + 192}
        gather={smooth(t, 3, 5)}
        style={{ opacity: 0.55 * brand }}
      />
      <div
        style={{
          ...A,
          inset: 0,
          opacity: 1 - brand,
          translate: `0 ${-100 * brand}px`,
        }}
      >
        <div style={{ ...A, left: 80, top: 500, width: 430, height: 540 }}>
          <WeddingCover imageSrc="campaign/wedding-bride-01.jpg" />
        </div>
        <div style={{ ...A, left: 570, top: 500, width: 430, height: 540 }}>
          <ExpeditionPoster />
        </div>
        <div style={{ ...A, left: 80, top: 1090, width: 920, height: 517.5 }}>
          <NightThumbnail>
            <Video
              src={asset("refine/night.mp4")}
              muted
              objectFit="cover"
              style={{
                position: "absolute",
                inset: 0,
                width: "100%",
                height: "100%",
              }}
            />
          </NightThumbnail>
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 490,
          opacity: brand,
          translate: `0 ${90 * (1 - out(t, 3.5, 4.5))}px`,
        }}
      >
        <AppWordmark width={850} />
        <div
          style={{
            fontFamily: font.serif,
            fontSize: 120,
            lineHeight: 1.05,
            marginTop: 85,
          }}
        >
          Photo. Design.
          <br />
          <span style={{ fontStyle: "italic", color: lime }}>Film. Yours.</span>
        </div>
        <Tag style={{ marginTop: 95 }}>PROFESSIONAL TOOLS. ROOM TO PLAY.</Tag>
      </div>
      {t < 3.5 && <Cue id="close" />}
    </>
  );
};
