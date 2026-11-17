import { CanvasImage, Sequence, staticFile, useVideoConfig } from "remotion";
import { Video } from "@remotion/media";
import { audioData } from "./audio-data";
import { teaserCuts } from "./edit-data";
import {
  A,
  Pointer,
  Bar,
  Caption,
  Control,
  fast,
  Icon,
  mix,
  muted,
  Panel,
  Photo,
  s,
  Tag,
  Title,
  useTime,
  window,
} from "./Core";

const movie = { width: "100%", height: "100%" };
const Walk = ({
  source = 0,
  motion = 0,
  mask = 1,
}: {
  source?: number;
  motion?: number;
  mask?: number;
}) => {
  const n = String(
    Math.min(184, Math.max(0, Math.floor(source * 30))),
  ).padStart(3, "0");
  return (
    <div style={{ ...A, inset: 0, overflow: "hidden" }}>
      <div
        style={{
          ...A,
          inset: 0,
          scale: 1 + 0.12 * motion,
          transformOrigin: "43% 46%",
        }}
      >
        <CanvasImage
          src={staticFile(`media/studio/hike/${n}.jpg`)}
          style={movie}
        />
        <div
          style={{
            ...A,
            left: 190 + 90 * motion,
            top: 165,
            fontSize: 117,
            lineHeight: 0.87,
            fontWeight: 700,
            letterSpacing: -5,
            color: "#f7efdc",
          }}
        >
          TAKE THE
          <br />
          LONG WAY.
        </div>
        <CanvasImage
          src={staticFile(`media/studio/hike/${n}.png`)}
          style={{ ...A, inset: 0, ...movie, opacity: mask }}
        />
      </div>
    </div>
  );
};

const Program = ({ t }: { t: number }) => {
  const { fps } = useVideoConfig(),
    cut1 = Math.min(2, teaserCuts[0]),
    cut2 = teaserCuts[1],
    cut3 = teaserCuts[2],
    grade = s(t, 20.5, 23),
    mono = window(t, 24, 25.5),
    move = s(t, 15, 18);
  return (
    <>
      <Sequence
        name="Arrival / finish the carousel's moving frame"
        durationInFrames={Math.round(cut1 * fps)}
        layout="none"
      >
        <Video
          src={staticFile("media/refine/arrival.mp4")}
          trimBefore={6 * fps}
          muted
          objectFit="cover"
          style={movie}
        />
        <div
          style={{
            ...A,
            left: 260,
            top: 100,
            color: "#f7efdc",
            fontSize: 155,
            fontWeight: 600,
            letterSpacing: -6,
          }}
        >
          NEPAL
        </div>
        <div
          style={{
            ...A,
            left: 266,
            top: 285,
            color: "white",
            fontSize: 28,
            letterSpacing: 5,
          }}
        >
          A FIELD FILM
        </div>
      </Sequence>
      <Sequence
        name="The ridge / original camera footage"
        from={Math.round(cut1 * fps)}
        durationInFrames={Math.round(cut2 * fps) - Math.round(cut1 * fps)}
        layout="none"
      >
        <Video
          src={staticFile("media/MVI_5051-2.3-5.9.mp4")}
          muted
          objectFit="cover"
          style={movie}
        />
      </Sequence>
      <Sequence
        name="After dark / location footage"
        from={Math.round(cut2 * fps)}
        durationInFrames={Math.round(cut3 * fps) - Math.round(cut2 * fps)}
        layout="none"
      >
        <Video
          src={staticFile("media/refine/night.mp4")}
          muted
          objectFit="cover"
          style={movie}
        />
        <div
          style={{
            ...A,
            left: 260,
            bottom: 100,
            fontSize: 47,
            letterSpacing: 3,
            color: "white",
          }}
        >
          THERE IS ALWAYS MORE TO SEE.
        </div>
      </Sequence>
      <Sequence
        name="Walking / moving subject matte and live title"
        from={Math.round(cut3 * fps)}
        durationInFrames={10 * fps - Math.round(cut3 * fps)}
        layout="none"
      >
        <Walk source={t - cut3} />
      </Sequence>
      <Sequence
        name="Trim / exact source endpoint follows the out point"
        from={10 * fps}
        durationInFrames={4 * fps}
        layout="none"
      >
        <CanvasImage
          src={staticFile(
            `media/studio/trim/${String(Math.round(59 * (1 - s(t, 10.4, 11.9)))).padStart(3, "0")}.jpg`,
          )}
          style={movie}
        />
      </Sequence>
      <Sequence
        name="Animate and composite / shared parameter and clock"
        from={14 * fps}
        durationInFrames={6 * fps}
        layout="none"
      >
        <Walk source={t - 14} motion={move} mask={1 - window(t, 18.4, 19.3)} />
      </Sequence>
      <Sequence
        name="Color and effect demonstration / original footage"
        from={20 * fps}
        durationInFrames={6 * fps}
        layout="none"
      >
        <Video
          src={staticFile("media/launch-1268.mp4")}
          trimBefore={2 * fps}
          muted
          objectFit="cover"
          style={movie}
        />
        <div style={{ ...A, inset: 0, opacity: grade }}>
          <Video
            src={staticFile("media/studio/city-grade.mp4")}
            muted
            objectFit="cover"
            style={movie}
          />
        </div>
        <div style={{ ...A, inset: 0, opacity: mono }}>
          <Video
            src={staticFile("media/studio/city-mono.mp4")}
            muted
            objectFit="cover"
            style={movie}
          />
        </div>
      </Sequence>
      <Sequence
        name="Audio mix / night ambience"
        from={26 * fps}
        durationInFrames={5 * fps}
        layout="none"
      >
        <Video
          src={staticFile("media/refine/night.mp4")}
          muted
          objectFit="cover"
          style={movie}
        />
        <div
          style={{
            ...A,
            left: 100,
            bottom: 75,
            fontSize: 62,
            fontWeight: 600,
            color: "white",
            letterSpacing: -2,
          }}
        >
          Keep the moment.
        </div>
      </Sequence>
      <Sequence
        name="Final reframe / same master, vertical version"
        from={31 * fps}
        durationInFrames={5 * fps}
        layout="none"
      >
        <Video
          src={staticFile("media/refine/hike.mp4")}
          muted
          objectFit="cover"
          style={movie}
        />
        <div
          style={{
            ...A,
            left: 440,
            right: 440,
            bottom: 80,
            fontSize: 54,
            lineHeight: 1.05,
            color: "white",
            fontWeight: 600,
            textShadow: "0 2px 12px #0008",
            opacity: s(t, 33.6, 34.4),
          }}
        >
          Take the
          <br />
          long way.
        </div>
      </Sequence>
    </>
  );
};

const Timeline = ({ t }: { t: number }) => {
  const trim = s(t, 10.4, 11.9),
    move = s(t, 15, 18),
    headSeconds =
      t < 10
        ? 16.6 + Math.max(0, t - teaserCuts[2])
        : t < 14
          ? 8 - 2 * trim
          : t < 20
            ? 14.6 + (t - 14)
            : t < 26
              ? 20.6 + (t - 20)
              : 26.6 + (t - 26),
    titleX = 120 + (16.6 - 2 * trim) * 26;
  return (
    <Panel
      style={{
        ...A,
        left: 40,
        top: 1180,
        width: 1000,
        height: 302,
        overflow: "hidden",
        opacity: 1 - s(t, 31, 32),
        translate: `0 ${100 * (1 - fast(t, 8.1, 10))}px`,
        scale: 1 + 0.045 * window(t, 10.2, 13.8),
        transformOrigin: "0 50%",
      }}
    >
      <div
        style={{
          padding: "17px 22px",
          fontSize: 25,
          display: "flex",
          alignItems: "center",
          gap: 20,
          borderBottom: "1px solid #354043",
        }}
      >
        <Icon name="play" size={25} />
        NEPAL / MASTER
        <span style={{ marginLeft: "auto", color: muted, fontSize: 22 }}>
          3840 × 2160 · 30 fps
        </span>
      </div>
      <div
        style={{
          ...A,
          left: 120,
          right: 20,
          top: 63,
          display: "flex",
          justifyContent: "space-between",
          color: muted,
          fontSize: 18,
          fontFamily: "Menlo",
        }}
      >
        {[0, 5, 10, 15, 20, 25, 30].map((second) => (
          <span
            key={second}
            style={{ position: "absolute", left: second * 26 }}
          >{`00:${String(second).padStart(2, "0")}`}</span>
        ))}
      </div>
      {["TYPE", "MASK", "VIDEO", "LOCATION", "MUSIC"].map((x, i) => (
        <Tag
          key={x}
          style={{ ...A, left: 14, top: 104 + i * 36, fontSize: 14 }}
        >
          {x}
        </Tag>
      ))}
      <div
        style={{
          ...A,
          left: titleX,
          top: 96,
          width: 156,
          height: 29,
          background: "#777191",
          borderRadius: 3,
          fontSize: 13,
          whiteSpace: "nowrap",
          padding: "6px 8px",
        }}
      >
        TAKE THE LONG WAY. ◇
      </div>
      <div
        style={{
          ...A,
          left: titleX,
          top: 132,
          width: 156,
          height: 29,
          background: "#608a85",
          borderRadius: 3,
          fontSize: 13,
          whiteSpace: "nowrap",
          padding: "6px 8px",
        }}
      >
        Subject mask / editable
      </div>
      <div style={{ ...A, left: 120, top: 168, display: "flex", gap: 0 }}>
        {[
          "studio/arrival-thumb.jpg",
          "studio/ridge-thumb.jpg",
          "studio/night-thumb.jpg",
          "IMG_1207-2-7.jpg",
          "IMG_1268-2-7.jpg",
          "studio/night-thumb.jpg",
        ].map((src, i) => {
          const w = [
            (8 - 2 * trim) * 26,
            3.6 * 26,
            5 * 26,
            6 * 26,
            6 * 26,
            5 * 26,
          ][i];
          return (
            <div
              key={`${src}-${i}`}
              style={{
                flexShrink: 0,
                position: "relative",
                width: w,
                height: 34,
                overflow: "hidden",
                outline: i === 0 ? "2px solid #d3e4e4" : undefined,
              }}
            >
              <Photo src={src} />
              <div style={{ ...A, inset: 0, background: "#1113" }} />
              {i === 0 && (
                <div
                  style={{
                    ...A,
                    right: 0,
                    top: 0,
                    bottom: 0,
                    width: 4,
                    background: "#e4eeee",
                  }}
                />
              )}
            </div>
          );
        })}
      </div>
      <div
        style={{
          ...A,
          left: 120,
          top: 209,
          width: 850,
          height: 28,
          background: "#254138",
        }}
      >
        <Wave width={850} kind="location" />
      </div>
      <div
        style={{
          ...A,
          left: 120,
          top: 245,
          width: 790,
          height: 28,
          background: "#35374c",
        }}
      >
        <Wave width={790} kind="score" />
      </div>
      <div
        style={{
          ...A,
          left: 120 + 26 * headSeconds,
          top: 61,
          bottom: 0,
          width: 2,
          background: "#f0f3ed",
        }}
      />
      {t > 14 && t < 20 && (
        <svg width={156} height={29} style={{ ...A, left: titleX, top: 96 }}>
          <path
            d={Array.from(
              { length: 65 },
              (_, i) =>
                `${i === 0 ? "M" : "L"}${26 + (78 * i) / 64} ${25 - 21 * s(i / 64, 0, 1)}`,
            ).join(" ")}
            stroke="#c4ece1"
            fill="none"
            strokeWidth={1.5}
          />
          <circle
            cx={26 + 78 * Math.max(0, Math.min(1, (t - 15) / 3))}
            cy={25 - 21 * move}
            r={4}
            fill="white"
          />
        </svg>
      )}
    </Panel>
  );
};
const Wave = ({
  width = 800,
  kind = "score",
}: {
  width?: number;
  kind?: "score" | "location";
}) => {
  const values =
      kind === "score" ? audioData.scoreWave : audioData.locationWave,
    max = Math.max(...values, 0.001);
  return (
    <svg width={width} height={28}>
      <path
        d={values
          .map(
            (v, i) =>
              `M${(i / (values.length - 1)) * width} ${14 - (v / max) * 12}V${14 + (v / max) * 12}`,
          )
          .join(" ")}
        stroke="#abc3bc"
        strokeWidth={1.6}
      />
    </svg>
  );
};

const Mixer = ({ t }: { t: number }) => {
  const score = mix(-6, -12, s(t, 27, 29));
  const meter =
    audioData.meters[Math.max(0, Math.min(149, Math.floor((t - 26) * 30)))];
  return (
    <Panel
      style={{
        ...A,
        left: 40,
        top: 1180,
        width: 1000,
        height: 490,
        padding: 30,
        opacity: window(t, 25.9, 31.1),
      }}
    >
      <Tag>AUDIO / PICTURE, MUSIC, LOCATION SOUND</Tag>
      <div style={{ display: "flex", gap: 28, marginTop: 36 }}>
        {["Location", "Score", "Effects", "Master"].map((name, i) => (
          <div
            key={name}
            style={{
              width: 214,
              position: "relative",
              height: 337,
              borderRight: i === 3 ? undefined : "1px solid #384043",
            }}
          >
            <div style={{ fontSize: 29 }}>{name}</div>
            <div
              style={{
                ...A,
                left: 49,
                top: 58,
                width: 3,
                height: 185,
                background: "#526064",
              }}
            />
            <div
              style={{
                ...A,
                left: 28,
                top: 76 + (i === 1 ? -score * 6 : 46),
                width: 46,
                height: 16,
                borderRadius: 3,
                background: "#d6e2df",
              }}
            />
            {[0, 1].map((c) => (
              <div
                key={c}
                style={{
                  ...A,
                  left: 116 + c * 17,
                  top: 60,
                  width: 11,
                  height: 185,
                  background: "#26302e",
                  overflow: "hidden",
                }}
              >
                <div
                  style={{
                    ...A,
                    bottom: 0,
                    left: 0,
                    right: 0,
                    height: `${meter[(["location", "score", "effects", "master"] as const)[i]][c] * 100}%`,
                    background:
                      "linear-gradient(to top,#6aab8c,#c0d6a3 85%,#e6c991)",
                  }}
                />
              </div>
            ))}
            <div
              style={{
                ...A,
                left: 20,
                top: 267,
                fontSize: 28,
                fontFamily: "Menlo",
              }}
            >
              {i === 1 ? score.toFixed(1) : [-9, -6, -12, -1][i].toFixed(1)} dB
            </div>
            <div
              style={{ ...A, left: 20, top: 314, fontSize: 20, color: muted }}
            >
              {i === 1
                ? "Volume automation"
                : i === 3
                  ? "Stereo output"
                  : "Gain · Pan · Effects"}
            </div>
          </div>
        ))}
      </div>
      <Pointer
        x={321}
        y={170 - score * 6}
        press={1}
        opacity={window(t, 26.9, 29.2)}
      />
    </Panel>
  );
};

export const FilmStudio = () => {
  const t = useTime(),
    unfold = s(t, 8, 10),
    vertical = s(t, 32, 34.5),
    editor = unfold * (1 - s(t, 30.8, 32)),
    w = mix(mix(1080, 1000, unfold), 570, vertical),
    h = mix(mix(820, 562.5, unfold), 1013, vertical),
    x = (1080 - w) / 2,
    y = mix(540, 560, unfold),
    scale = Math.max(w / 1280, h / 720),
    move = s(t, 15, 18),
    grade = s(t, 20.5, 23);
  return (
    <>
      <Bar mode="Timeline / complete productions" />
      <Title
        t={t}
        b={8}
        lines={["Make the", "whole film."]}
        detail="A real timeline. Layered picture and sound."
      />
      <Title
        t={t}
        a={8.1}
        b={14}
        lines={["Build the story.", "Shape every cut."]}
        detail="Trim. Ripple. Arrange. Keep it connected."
      />
      <Title
        t={t}
        a={14.1}
        b={20}
        lines={["Give it", "your movement."]}
        detail="Keyframes. Curves. Compositing. Full control."
      />
      <Title
        t={t}
        a={20.1}
        b={26}
        lines={["Your footage.", "Your look."]}
        detail="sRGB. Rec.709. Log workflows."
      />
      <Title
        t={t}
        a={26.1}
        b={31}
        lines={["Sound is", "part of the story."]}
        detail="Separate tracks. Editable levels. A complete mix."
      />
      <Title
        t={t}
        a={31.1}
        b={36.4}
        lines={["Full film.", "Vertical cut."]}
        detail="Cinema. YouTube. Reels. TikTok."
      />
      <div
        style={{
          ...A,
          left: x,
          top: y,
          width: w,
          height: h,
          overflow: "hidden",
          boxShadow: "0 30px 90px #0007",
        }}
      >
        <div
          style={{
            ...A,
            left: (w - 1280 * scale) / 2,
            top: (h - 720 * scale) / 2,
            width: 1280,
            height: 720,
            scale,
            transformOrigin: "0 0",
          }}
        >
          <Program t={t} />
        </div>
        {editor > 0.1 && (
          <Tag
            style={{
              ...A,
              left: 22,
              top: 22,
              color: "white",
              background: "#10171ab8",
              padding: "9px 13px",
              fontSize: 23,
            }}
          >
            {t < 20
              ? "PROGRAM / 4K MASTER"
              : t < 26
                ? "COLOR / sRGB · Rec.709 · LOG WORKFLOWS"
                : "PROGRAM / COMPLETE SOUND MIX"}
          </Tag>
        )}
      </div>
      <div style={{ opacity: editor }}>
        <Timeline t={t} />
      </div>
      <Panel
        style={{
          ...A,
          left: 40,
          top: 1512,
          width: 1000,
          minHeight: 210,
          padding: "24px 35px",
          opacity: editor * (1 - window(t, 25.8, 31.1)),
        }}
      >
        {t < 14 ? (
          <>
            <Tag>TRIM / LIVE SOURCE ENDPOINT</Tag>
            <Control
              label="Out point"
              value={mix(26, 24, s(t, 10.4, 11.9))}
              text={`00:00:${Math.round(mix(26, 24, s(t, 10.4, 11.9)))}`}
              min={0}
              max={30}
              cursor={t > 10.4 && t < 11.9}
            />
          </>
        ) : t < 20 ? (
          <>
            <Tag>MOTION / MATCHED KEYFRAMES AND IMAGE</Tag>
            <Control
              label="Position X"
              value={270 * move}
              text={`${Math.round(270 * move)} px`}
              min={-900}
              max={900}
              cursor={t > 15 && t < 18}
            />
            <div style={{ fontSize: 30, color: muted, marginTop: 10 }}>
              Scale {Math.round(100 + 12 * move)}% · Smooth easing
            </div>
            <svg
              width={270}
              height={64}
              style={{ position: "absolute", right: 45, bottom: 24 }}
            >
              <path
                d={Array.from(
                  { length: 65 },
                  (_, i) =>
                    `${i === 0 ? "M" : "L"}${4 + (260 * i) / 64} ${60 - 54 * s(i / 64, 0, 1)}`,
                ).join(" ")}
                stroke="#bdd8d7"
                fill="none"
                strokeWidth={2}
              />
              <circle
                cx={4 + 260 * Math.max(0, Math.min(1, (t - 15) / 3))}
                cy={60 - 54 * move}
                r={4}
                fill="white"
              />
            </svg>
          </>
        ) : (
          <>
            <Tag>COLOR AND EFFECTS / EDITABLE STACK</Tag>
            <Control
              label="Look strength"
              value={grade * 100}
              text={`${Math.round(grade * 100)}%`}
              min={0}
              max={100}
              cursor={t > 20.5 && t < 23}
            />
            <div style={{ fontSize: 31, color: muted, marginTop: 10 }}>
              Black & white / {t > 24 && t < 25.5 ? "Enabled" : "Disabled"}
            </div>
          </>
        )}
      </Panel>
      <Mixer t={t} />
      <Panel
        style={{
          ...A,
          left: 80,
          top: 1620,
          width: 920,
          padding: 30,
          opacity: vertical,
        }}
      >
        <Tag>EXPORT / FULL-RESOLUTION MASTER + SOCIAL VERSION</Tag>
        <div
          style={{
            display: "flex",
            gap: 22,
            fontSize: 35,
            marginTop: 25,
            alignItems: "center",
          }}
        >
          <Icon name="check" size={32} />
          4K film + 9:16 vertical edit
        </div>
      </Panel>
      <Caption opacity={editor} top={1770}>
        Full productions and social content. One editor.
      </Caption>
    </>
  );
};
