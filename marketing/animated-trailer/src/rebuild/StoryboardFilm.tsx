import { Sequence, useVideoConfig } from "remotion";
import { Video } from "@remotion/media";
import { A, Image, Tag, lime, paper, mix, smooth, useTime } from "./Kit";
import { font } from "./Type";
import {
  Cue,
  Dock,
  Slider,
  MatteWalk,
  MovieFrame,
  filmStyle,
  asset,
} from "./StoryboardShared";
import { audioData } from "./StoryboardAudio";

export const TeaserStoryboard = () => {
  const { fps } = useVideoConfig();
  return (
    <>
      <MovieFrame>
        <Sequence
          name="Arrival / continue carousel source"
          durationInFrames={2 * fps}
          layout="none"
        >
          <Video
            src={asset("refine/arrival.mp4")}
            trimBefore={4 * fps}
            muted
            objectFit="cover"
            style={filmStyle}
          />
          <div
            style={{
              ...A,
              left: 65,
              bottom: 65,
              fontFamily: font.display,
              fontSize: 150,
              color: paper,
            }}
          >
            NEPAL.
          </div>
        </Sequence>
        <Sequence
          name="Ridge / the journey"
          from={2 * fps}
          durationInFrames={2 * fps}
          layout="none"
        >
          <Video
            src={asset("MVI_5051-2.3-5.9.mp4")}
            muted
            objectFit="cover"
            style={filmStyle}
          />
        </Sequence>
        <Sequence
          name="Walk / layered title"
          from={4 * fps}
          durationInFrames={3.9 * fps}
          layout="none"
        >
          <WalkShot />
        </Sequence>
        <Sequence
          name="Night / finish the teaser"
          from={7.9 * fps}
          durationInFrames={2.1 * fps}
          layout="none"
        >
          <Video
            src={asset("refine/night.mp4")}
            muted
            objectFit="cover"
            style={filmStyle}
          />
          <div
            style={{
              ...A,
              left: 65,
              bottom: 65,
              fontFamily: font.serif,
              fontStyle: "italic",
              fontSize: 91,
              color: paper,
            }}
          >
            A little further.
          </div>
        </Sequence>
      </MovieFrame>
      <Cue id="teaser" />
    </>
  );
};
const WalkShot = () => {
  const t = useTime();
  return <MatteWalk source={t} motion={smooth(t, 0.3, 2.8)} />;
};

const MiniTimeline = ({
  trim = 0,
  head = 0,
}: {
  trim?: number;
  head?: number;
}) => {
  const widths = [(8 - 2 * trim) * 25, 3.6 * 25, 6 * 25, 6 * 25, 5 * 25],
    names = ["ARRIVAL", "RIDGE", "WALK", "CITY", "NIGHT"];
  return (
    <div
      style={{
        ...A,
        left: 45,
        top: 1190,
        width: 990,
        height: 250,
        padding: 22,
        background: "#151b24",
        border: "1px solid #516070",
        borderRadius: 12,
        overflow: "hidden",
      }}
    >
      <Tag style={{ fontSize: 22, color: "#b8c5d3" }}>
        NEPAL / 3840 × 2160 · 30 FPS
      </Tag>
      <div style={{ position: "relative", height: 170, marginTop: 16 }}>
        {["TYPE", "MASK", "PICTURE", "LOCATION", "SCORE"].map((n, i) => (
          <Tag key={n} style={{ ...A, left: 0, top: i * 30 + 2, fontSize: 15 }}>
            {n}
          </Tag>
        ))}
        <div
          style={{
            ...A,
            left: 100 + (11.6 - 2 * trim) * 25,
            top: 0,
            width: 150,
            height: 24,
            background: "#73719d",
            fontSize: 16,
            paddingLeft: 6,
          }}
        >
          OFF THE MAP / ◇
        </div>
        <div
          style={{
            ...A,
            left: 100 + (11.6 - 2 * trim) * 25,
            top: 30,
            width: 150,
            height: 24,
            background: "#4e8177",
            fontSize: 16,
            paddingLeft: 6,
          }}
        >
          Subject mask
        </div>
        <div style={{ ...A, left: 100, top: 60, display: "flex", gap: 0 }}>
          {widths.map((w, i) => (
            <div
              key={i}
              style={{
                width: w,
                height: 27,
                flexShrink: 0,
                background: [
                  "#38616a",
                  "#526b75",
                  "#375b67",
                  "#5b6375",
                  "#55545f",
                ][i],
                borderRight: "2px solid #111",
                fontSize: 14,
                padding: 5,
                whiteSpace: "nowrap",
              }}
            >
              {names[i]}
            </div>
          ))}
        </div>
        {["location", "score"].map((name, i) => (
          <div
            key={name}
            style={{
              ...A,
              left: 100,
              top: 90 + i * 30,
              width: 780,
              height: 25,
              background: i ? "#393b55" : "#315347",
            }}
          >
            <svg width={780} height={25}>
              {(name === "score"
                ? audioData.scoreWave
                : audioData.locationWave
              ).map((v, j) => (
                <line
                  key={j}
                  x1={(j * 780) / 180}
                  x2={(j * 780) / 180}
                  y1={12.5 - Math.min(1, v * 8) * 11}
                  y2={12.5 + Math.min(1, v * 8) * 11}
                  stroke="#b7d5c5"
                />
              ))}
            </svg>
          </div>
        ))}
        <div
          style={{
            ...A,
            left: 100 + head * 25,
            top: -8,
            height: 165,
            width: 2,
            background: paper,
          }}
        />
      </div>
    </div>
  );
};

export const TimelineStoryboard = () => {
  const t = useTime(),
    { fps } = useVideoConfig(),
    trim = smooth(t, 2.8, 5.8),
    head =
      t < 2.5
        ? 26.7 + t
        : t < 6.5
          ? 8 - 2 * trim
          : t < 9
            ? 6 + (t - 6.5)
            : 9.6 + t - 9;
  return (
    <>
      <MovieFrame
        style={{
          left: 45,
          top: 540,
          width: 990,
          height: 557,
          scale: mix(1.09, 1, smooth(t, 0, 1.6)),
          transformOrigin: "50% 50%",
        }}
      >
        <Sequence
          name="Continue the finished night shot"
          durationInFrames={2.5 * fps}
          layout="none"
        >
          <Video
            src={asset("refine/night.mp4")}
            trimBefore={2.1 * fps}
            muted
            objectFit="cover"
            style={filmStyle}
          />
        </Sequence>
        <Sequence
          name="Trim / true source endpoint"
          from={2.5 * fps}
          durationInFrames={4 * fps}
          layout="none"
        >
          <Image
            src={`studio/trim/${String(Math.round(59 * (1 - trim))).padStart(3, "0")}.jpg`}
          />
        </Sequence>
        <Sequence
          name="Play the new boundary"
          from={6.5 * fps}
          durationInFrames={2.5 * fps}
          layout="none"
        >
          <Video
            src={asset("MVI_5051-2.3-5.9.mp4")}
            muted
            objectFit="cover"
            style={filmStyle}
          />
        </Sequence>
        <Sequence
          name="Select walking/title composition"
          from={9 * fps}
          durationInFrames={3 * fps}
          layout="none"
        >
          <WalkShot />
        </Sequence>
      </MovieFrame>
      <MiniTimeline trim={trim} head={head} />
      <Dock top={1490}>
        <Tag style={{ color: lime, marginBottom: 15 }}>
          {t < 6.5
            ? "RIPPLE TRIM / LIVE SOURCE ENDPOINT"
            : "LAYERED PICTURE + SOUND"}
        </Tag>
        {t < 6.5 ? (
          <Slider
            label="Out point"
            value={26 - 2 * trim}
            text={`00:00:${Math.round(26 - 2 * trim)}`}
            min={0}
            max={30}
            cursor={t > 2.8 && t < 5.8}
          />
        ) : (
          <div style={{ fontSize: 36 }}>
            Title · Mask · Picture · Location · Score
          </div>
        )}
      </Dock>
      <Cue id="timeline" />
    </>
  );
};

export const MotionStoryboard = () => {
  const t = useTime(),
    { fps } = useVideoConfig(),
    move = smooth(t, 0.8, 4.2),
    grade = smooth(t, 6.4, 8.1),
    mono = smooth(t, 8.4, 8.65) * (1 - smooth(t, 9.35, 9.6));
  return (
    <>
      <MovieFrame style={{ top: 540 }}>
        <Sequence
          name="Keyframes / replay selected shot from keyframe A"
          durationInFrames={6 * fps}
          layout="none"
        >
          <MatteWalk source={t} motion={move} />
        </Sequence>
        <Sequence
          name="Native-engine video color and monochrome"
          from={6 * fps}
          durationInFrames={4 * fps}
          layout="none"
        >
          <Video
            src={asset("launch-1268.mp4")}
            trimBefore={2 * fps}
            muted
            objectFit="cover"
            style={filmStyle}
          />
          <div style={{ ...A, inset: 0, opacity: grade }}>
            <Video
              src={asset("studio/city-grade.mp4")}
              muted
              objectFit="cover"
              style={filmStyle}
            />
          </div>
          <div style={{ ...A, inset: 0, opacity: mono }}>
            <Video
              src={asset("studio/city-mono.mp4")}
              muted
              objectFit="cover"
              style={filmStyle}
            />
          </div>
        </Sequence>
      </MovieFrame>
      <MiniTimeline trim={1} head={t < 6 ? 9.6 + t : 15.6 + t - 6} />
      <Dock top={1490}>
        {t < 6 ? (
          <>
            <Tag style={{ color: lime }}>MOTION / EDITABLE KEYFRAMES</Tag>
            <Slider
              label="Position X"
              value={450 * move}
              text={`${Math.round(450 * move)} px`}
              min={-900}
              max={900}
              cursor
            />
            <div
              style={{
                display: "flex",
                alignItems: "center",
                justifyContent: "space-between",
              }}
            >
              <span style={{ fontSize: 30 }}>
                Scale {Math.round(100 + 12 * move)}%
              </span>
              <svg width={330} height={60}>
                <path
                  d={Array.from(
                    { length: 65 },
                    (_, i) =>
                      `${i ? "L" : "M"}${4 + (322 * i) / 64} ${56 - 52 * smooth(i / 64, 0, 1)}`,
                  ).join(" ")}
                  fill="none"
                  stroke={lime}
                  strokeWidth={2}
                />
                <circle
                  cx={4 + 322 * Math.max(0, Math.min(1, (t - 0.8) / 3.4))}
                  cy={56 - 52 * move}
                  r={5}
                  fill={paper}
                />
              </svg>
            </div>
          </>
        ) : (
          <>
            <Tag style={{ color: lime }}>COLOR + EFFECTS / EDITABLE STACK</Tag>
            <Slider
              label="Look strength"
              value={100 * grade}
              text={`${Math.round(100 * grade)}%`}
              min={0}
              max={100}
              cursor
            />
            <div style={{ fontSize: 30 }}>
              Monochrome: {mono > 0.5 ? "enabled" : "disabled"}
            </div>
          </>
        )}
      </Dock>
      <Cue id="motion" />
    </>
  );
};

export const ColorStoryboard = () => {
  const t = useTime(),
    { fps } = useVideoConfig();
  return (
    <>
      <MovieFrame style={{ top: 650 }}>
        <Image src="rebuild/city-last.jpg" />
        <Sequence
          name="Continue grade / pause on export review"
          durationInFrames={2 * fps}
          layout="none"
        >
          <Video
            src={asset("studio/city-grade.mp4")}
            trimBefore={4 * fps}
            muted
            objectFit="cover"
            style={filmStyle}
          />
        </Sequence>
      </MovieFrame>
      <Tag style={{ ...A, left: 80, top: 510, color: lime }}>
        ACTUAL SOURCE / REC.709
      </Tag>
      <Dock top={1370}>
        <Tag style={{ color: lime, marginBottom: 25 }}>
          {t < 3.4
            ? "MANAGED OUTPUT"
            : t < 6.6
              ? "SUPPORTED INPUT TRANSFORMS"
              : "DELIVERY / CODEC + OUTPUT"}
        </Tag>
        <div style={{ fontFamily: font.sans, fontSize: 45, lineHeight: 1.5 }}>
          {t < 3.4 ? (
            <>
              Photographs → sRGB
              <br />
              Video masters → Rec.709
            </>
          ) : t < 6.6 ? (
            <>
              Sony S-Gamut / S-Log2
              <br />
              Sony S-Gamut3 / S-Log3
            </>
          ) : (
            <>
              ProRes · Rec.709
              <br />
              3840 × 2160 master
            </>
          )}
        </div>
      </Dock>
      <Cue id="color" />
    </>
  );
};

export const SoundDeliveryStoryboard = () => {
  const t = useTime(),
    { fps } = useVideoConfig(),
    vertical = smooth(t, 3.8, 5.3),
    gain = smooth(t, 0.8, 2.8),
    a = audioData.meters[Math.min(149, Math.floor(t * 30))];
  return (
    <>
      <MovieFrame
        style={{
          top: 550,
          left: mix(0, 280, vertical),
          width: mix(1080, 520, vertical),
          height: mix(607.5, 924, vertical),
        }}
      >
        <Sequence
          name="Location sound / original ambience"
          durationInFrames={4 * fps}
          layout="none"
        >
          <Video
            src={asset("refine/night.mp4")}
            muted
            objectFit="cover"
            style={filmStyle}
          />
        </Sequence>
        <Sequence
          name="Separate vertical reframe"
          from={4 * fps}
          durationInFrames={4 * fps}
          layout="none"
        >
          <Video
            src={asset("refine/hike.mp4")}
            muted
            objectFit="cover"
            style={filmStyle}
          />
        </Sequence>
      </MovieFrame>
      <Dock top={1230} style={{ opacity: 1 - vertical }}>
        <Tag style={{ color: lime, marginBottom: 20 }}>
          LOCATION / SCORE / EFFECTS / MASTER
        </Tag>
        <Slider
          label="Score"
          value={-6 - 6 * gain}
          text={`${(-6 - 6 * gain).toFixed(1)} dB`}
          min={-60}
          max={6}
          cursor
        />
        <div style={{ display: "flex", gap: 25 }}>
          {["Location", "Score", "Master"].map((n, i) => (
            <div key={n} style={{ width: 235, fontSize: 26 }}>
              {n}
              <div
                style={{
                  height: 55,
                  marginTop: 15,
                  display: "flex",
                  alignItems: "flex-end",
                  gap: 5,
                }}
              >
                {[0, 1].map((j) => (
                  <div
                    key={j}
                    style={{
                      width: 28,
                      height:
                        55 *
                        (i === 0
                          ? a.location[j]
                          : i === 1
                            ? a.score[j]
                            : a.master[j]),
                      background: lime,
                    }}
                  />
                ))}
              </div>
            </div>
          ))}
        </div>
      </Dock>
      <Dock top={1550} style={{ opacity: vertical }}>
        <Tag style={{ color: lime }}>WIDE MASTER + SEPARATE SOCIAL VERSION</Tag>
        <div style={{ fontSize: 36, marginTop: 20 }}>
          Films. YouTube. Reels. TikTok.
        </div>
      </Dock>
      <Cue id="delivery" />
    </>
  );
};
