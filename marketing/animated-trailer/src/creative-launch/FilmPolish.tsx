import { DeliveryProof } from "./BuyerScenes";
import { Freeze, useVideoConfig } from "remotion";
import {
  A,
  Pic,
  Film,
  useShot,
  smooth,
  out,
  mix,
  Light,
  Caption,
  Stage,
  At,
  Label,
  Pills,
  Footer,
  font,
  lime,
  paper,
} from "./Kit";
import { editModel, sourceAt, motionValue, mixerState, phaseAt } from "./State";
import { audioPeaks } from "./audio-data";

const clockCode = (time: number) => {
  const frame = Math.floor(time * 30);
  return `00:${String(Math.floor(time / 60)).padStart(2, "0")}:${String(Math.floor(time % 60)).padStart(2, "0")}:${String(frame % 30).padStart(2, "0")}`;
};
export const EditPreview = ({
  clock,
  trim = 0,
  title = true,
  textSize = 65,
  titleBottom = 35,
  insetScale = 1,
}: {
  clock: number;
  trim?: number;
  title?: boolean;
  textSize?: number;
  titleBottom?: number;
  insetScale?: number;
}) => {
  const { fps } = useVideoConfig(),
    shot = sourceAt(clock, trim);
  return (
    <>
      <Freeze frame={Math.round(shot.sourceTime * fps)}>
        <Film src={shot.source} />
      </Freeze>
      <div
        style={{
          ...A,
          inset: 0,
          background:
            "linear-gradient(to bottom,#080c13 0%,#080c13 5%,transparent 5%,transparent 95%,#080c13 95%)",
          pointerEvents: "none",
        }}
      />
      {title && (
        <div
          style={{
            ...A,
            left: 40 * insetScale,
            right: 40 * insetScale,
            bottom: titleBottom,
            color: paper,
            textShadow: `0 ${2 * insetScale}px ${20 * insetScale}px #0009`,
          }}
        >
          <div
            style={{
              fontFamily: font.mono,
              fontSize: textSize * 0.16,
              letterSpacing: insetScale,
            }}
          >
            {shot.id === "ridge"
              ? "ABOVE THE EVERYDAY"
              : shot.id === "city"
                ? "KATHMANDU / AFTERNOON"
                : "27° N / A FIELD FILM"}
          </div>
          <div
            style={{
              fontFamily: font.serif,
              fontStyle: "italic",
              fontSize: textSize,
              letterSpacing: -insetScale,
              marginTop: 8 * insetScale,
            }}
          >
            {shot.id === "ridge"
              ? "A little further."
              : shot.id === "city"
                ? "Down another street."
                : "After the ridgeline."}
          </div>
        </div>
      )}
    </>
  );
};

export const SyncedTimeline = ({
  clock,
  trim,
}: {
  clock: number;
  trim: number;
}) => {
  const edit = editModel(trim),
    scale = 70;
  return (
    <div
      style={{
        ...A,
        inset: 0,
        background: "#171d24",
        border: "1px solid #66768488",
        borderRadius: 13,
        overflow: "hidden",
        boxShadow: "0 35px 80px #0009",
      }}
    >
      <div
        style={{
          ...A,
          left: 0,
          top: 0,
          width: 1000,
          height: 42,
          overflow: "hidden",
        }}
      >
        <Pic
          src="creative-polish-v2/timeline-native.png"
          style={{ top: -25, width: 1000, height: 585, objectFit: "fill" }}
        />
        <div
          style={{
            ...A,
            right: 0,
            width: 95,
            height: 42,
            background: "#101316",
          }}
        />
      </div>
      <div
        style={{
          ...A,
          left: 22,
          top: 60,
          right: 22,
          fontFamily: font.sans,
          color: paper,
          fontSize: 21,
          display: "flex",
          justifyContent: "space-between",
        }}
      >
        <span>AFTER THE RIDGELINE</span>
        <span style={{ fontFamily: font.mono, fontSize: 13, color: "#9aafbe" }}>
          PROGRAM / {clockCode(clock)}
        </span>
      </div>
      <div
        style={{
          ...A,
          left: 25,
          top: 105,
          width: 700,
          height: 394,
          overflow: "hidden",
          background: "#090e15",
          borderRadius: 7,
        }}
      >
        <EditPreview clock={clock} trim={trim} textSize={59} />
      </div>
      <div
        style={{
          ...A,
          left: 750,
          top: 105,
          width: 230,
          height: 394,
          background: "#111820",
          padding: 18,
          borderRadius: 8,
          color: paper,
          fontFamily: font.sans,
        }}
      >
        <Label style={{ fontSize: 15 }}>INSPECTOR</Label>
        <div style={{ fontSize: 21, marginTop: 25 }}>Transform</div>
        {[
          ["Position", "0 / 0"],
          ["Scale", "100%"],
          ["Opacity", "100%"],
        ].map(([name, value]) => (
          <div
            key={name}
            style={{
              fontSize: 16,
              marginTop: 25,
              display: "flex",
              justifyContent: "space-between",
            }}
          >
            <span>{name}</span>
            <span style={{ color: lime }}>{value}</span>
          </div>
        ))}
        <div
          style={{
            fontFamily: font.mono,
            fontSize: 12,
            color: "#8fa5b8",
            marginTop: 35,
          }}
        >
          LAYERED VIDEO
          <br />
          <br />
          TITLES + AUDIO
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 0,
          top: 520,
          width: 1000,
          height: 42,
          overflow: "hidden",
        }}
      >
        <Pic
          src="creative-polish-v2/timeline-native.png"
          style={{ top: -389, width: 1000, height: 585, objectFit: "fill" }}
        />
      </div>
      <div
        style={{
          ...A,
          left: 110,
          top: 575,
          width: 850,
          height: 27,
          fontFamily: font.mono,
          color: "#a9bac8",
          fontSize: 13,
          display: "flex",
          justifyContent: "space-between",
        }}
      >
        {[0, 3, 6, 9, 12].map((n) => (
          <span key={n}>{String(n).padStart(2, "0")}:00</span>
        ))}
      </div>
      <div
        style={{
          ...A,
          left: 20,
          top: 615,
          width: 76,
          fontFamily: font.mono,
          fontSize: 13,
          color: "#adbdc8",
        }}
      >
        V2 TITLE
        <br />
        <br />
        <br />
        V1 VIDEO
        <br />
        <br />
        <br />
        A1 MUSIC
      </div>
      <div
        style={{
          ...A,
          left: 110,
          top: 607,
          width: edit[3].end * scale,
          height: 26,
          background: "#5c526e",
          border: "1px solid #a9a0bf",
          borderRadius: 3,
          fontFamily: font.mono,
          color: paper,
          fontSize: 12,
          padding: 5,
        }}
      >
        AFTER THE RIDGELINE / TEXT
      </div>
      {edit.map((clip, i) => (
        <div
          key={clip.id}
          style={{
            ...A,
            left: 110 + clip.start * scale,
            top: 642,
            width: clip.duration * scale - 3,
            height: 62,
            overflow: "hidden",
            border: "1px solid #a4b3c2",
            borderRadius: 3,
          }}
        >
          <Pic src={clip.thumb} />
          <div
            style={{
              ...A,
              left: 0,
              right: 0,
              bottom: 0,
              padding: 4,
              fontFamily: font.mono,
              fontSize: 11,
              color: paper,
              background: "#0c1722dc",
            }}
          >
            {clip.label}
          </div>
          {i === 2 && (
            <div
              style={{
                ...A,
                right: 0,
                top: 0,
                bottom: 0,
                width: 6,
                background: lime,
              }}
            />
          )}
        </div>
      ))}
      <div
        style={{
          ...A,
          left: 110,
          top: 714,
          width: edit[3].end * scale,
          height: 48,
          background: "#234d49",
          border: "1px solid #6caca1",
          borderRadius: 3,
        }}
      >
        <Wave samples={audioPeaks.timeline} gain={1} height={48} />
      </div>
      <div
        style={{
          ...A,
          left: 110 + clock * scale,
          top: 574,
          bottom: 20,
          width: 2,
          background: lime,
        }}
      />
      {trim > 0 && trim < 1 && (
        <svg
          width="28"
          height="36"
          style={{ ...A, left: 110 + edit[2].end * scale, top: 660 }}
        >
          <path
            d="M0 0V25L7 19L14 33L20 29L12 17H25Z"
            fill="white"
            stroke="#09121b"
          />
        </svg>
      )}
    </div>
  );
};

const Wave = ({
  samples,
  gain,
  height = 64,
}: {
  samples: number[];
  gain: number;
  height?: number;
}) => (
  <svg viewBox={`0 0 1000 ${height}`} width="100%" height={height}>
    {samples.map((v, i) => (
      <rect
        key={i}
        x={(i * 1000) / samples.length}
        y={height / 2 - Math.min(height / 2 - 3, v * gain * height * 0.85)}
        width="3"
        height={Math.max(2, Math.min(height - 6, v * gain * height * 1.7))}
        fill="#c1e3df"
      />
    ))}
  </svg>
);

export const FilmChapter = () => {
  const { t, portrait, width, height } = useShot(),
    { phase, s, start } = phaseAt(t, [0, 12, 24, 34, 42, 48, 56, 60, 68]);
  const motionTime = Math.max(0, Math.min(1, (s - 0.5) / 3.5));
  const p = motionValue(motionTime),
    trim = smooth(s, 3.5, 6.5),
    edit = editModel(trim);
  const timelineClock =
    s < 1.2
      ? 11.95
      : s < 3.5
        ? mix(11.95, 0, smooth(s, 1.2, 2.5))
        : s < 6.8
          ? edit[2].end - 0.04
          : mix(7.45, 11.25, smooth(s, 7, 11.5));
  const mixer = mixerState(s);
  const effectSplit = 0.38 * (1 - smooth(s, 6.5, 7.3));
  const reveal = smooth(s, 0, 1.2),
    stageScale = portrait ? 1 : 1.1;
  const copy = [
    [
      "Make the\nwhole film.",
      "Footage. Titles. Color. Sound. One finished edit.",
    ],
    [
      "Build the edit.\nKeep it moving.",
      "Layer footage, titles and audio. Trim a clip; ripple the following shots into place.",
    ],
    [
      "Motion, with\nprecise control.",
      "Position, scale, opacity and easing. The animation follows your keyframes.",
    ],
    [
      "Build the look.\nLayer the effects.",
      "A native color grade and a monochrome effect. The same footage, visibly transformed.",
    ],
    [
      "Picture meets\nsound.",
      "Pull down the music and hear the location. Shape the mix alongside the edit.",
    ],
    [
      "The right pipeline.\nThe right delivery.",
      "sRGB and Rec.709. Sony S-Log2 and S-Log3 input support. 4K and ProRes export.",
    ],
    [
      "Change your\nperspective.",
      "Gaussian scenes with a moving camera. Another dimension in the creative toolbox.",
    ],
    [
      "A full film.\nA social cut.",
      "Landscape productions and independently framed vertical videos.",
    ],
  ];
  if (phase === 5) return <DeliveryProof s={s} />;
  if (phase === 0)
    return (
      <>
        <EditPreview
          clock={Math.min(11.96, t)}
          textSize={portrait ? 100 : 130}
          titleBottom={portrait ? 230 : 35}
        />
        <div
          style={{
            ...A,
            left: portrait ? 80 : 100,
            top: portrait ? 160 : 75,
            color: paper,
          }}
        >
          <Label style={{ color: paper }}>05 / MAKE THE WHOLE FILM.</Label>
          <div
            style={{
              fontFamily: font.sans,
              fontSize: portrait ? 37 : 32,
              marginTop: 20,
              opacity: 1 - smooth(s, 3.4, 4.5),
            }}
          >
            Footage. Titles. Color. Sound. One finished edit.
          </div>
        </div>
      </>
    );
  return (
    <>
      <Light t={184 + t} opacity={0.28} />
      <Caption
        chapter="05 / VIDEO TIMELINE"
        title={copy[phase][0]}
        detail={copy[phase][1]}
        time={phase === 1 ? s - 1 : s}
        serif={phase === 6}
      />
      <Stage>
        {phase === 1 && (
          <>
            <div
              style={{
                ...A,
                inset: 0,
                transform: "none",
              }}
            >
              <SyncedTimeline clock={timelineClock} trim={trim} />
            </div>
            {s > 4 && (
              <Label
                style={{
                  ...A,
                  right: 25,
                  top: 480,
                  fontSize: 15,
                  padding: 12,
                  background: "#142532e8",
                }}
              >
                RIPPLE TRIM / −{(0.7 * trim).toFixed(2)} s
              </Label>
            )}
          </>
        )}
        {phase === 2 && (
          <>
            <div
              style={{
                ...A,
                left: 0,
                top: 10,
                width: 1000,
                height: 562,
                overflow: "hidden",
                borderRadius: 11,
              }}
            >
              <At
                from={start}
                seconds={10}
                name="Keyframed title over the same source"
              >
                <Film src="creative-polish-v2/city-10.mp4" />
              </At>
              <div style={{ ...A, inset: 0, background: "#0b162b25" }} />
              <div
                style={{
                  ...A,
                  left: mix(430, 65, p),
                  top: mix(340, 150, p),
                  width: 500,
                  height: 205,
                  transform: `scale(${mix(0.75, 1, p)})`,
                  transformOrigin: "0 0",
                  opacity: p,
                  border: "1px solid #e8eddfb3",
                  padding: 12,
                }}
              >
                <div
                  style={{
                    fontFamily: font.display,
                    fontSize: 145,
                    lineHeight: 1,
                    color: paper,
                  }}
                >
                  NEPAL
                </div>
                <Label style={{ fontSize: 18, color: paper, marginTop: 15 }}>
                  A FIELD FILM
                </Label>
                {[0, 1, 2, 3].map((i) => (
                  <div
                    key={i}
                    style={{
                      ...A,
                      left: i % 2 ? "100%" : -4,
                      top: i > 1 ? "100%" : -4,
                      width: 8,
                      height: 8,
                      background: paper,
                    }}
                  />
                ))}
              </div>
            </div>
            <svg
              style={{ ...A, left: 0, top: 595, width: 1000, height: 175 }}
              viewBox="0 0 1000 200"
            >
              <path d="M30 175V15M30 175H970" stroke="#657789" />
              <path
                d={Array.from(
                  { length: 129 },
                  (_, i) =>
                    `${i ? "L" : "M"}${30 + (i / 128) * 940} ${175 - motionValue(i / 128) * 160}`,
                ).join(" ")}
                stroke={lime}
                strokeWidth="3"
                fill="none"
              />
              <circle
                cx={30 + motionTime * 940}
                cy={175 - p * 160}
                r="8"
                fill={paper}
              />
            </svg>
            <Label style={{ ...A, left: 15, top: 780, fontSize: 15 }}>
              X {Math.round(mix(430, 65, p))} / Y {Math.round(mix(340, 150, p))}{" "}
              · SCALE {Math.round(mix(75, 100, p))}% · OPACITY{" "}
              {Math.round(p * 100)}%
            </Label>
          </>
        )}
        {phase === 3 && (
          <>
            <div
              style={{
                ...A,
                left: 0,
                top: 35,
                width: 1000,
                height: 562,
                overflow: "hidden",
                borderRadius: 11,
              }}
            >
              <At
                from={start}
                seconds={8}
                name="Real native-engine color and effect results"
              >
                <Film src="creative-polish-v2/city-10.mp4" />
                <div
                  style={{
                    ...A,
                    inset: 0,
                    opacity: smooth(s, 1, 2),
                    clipPath: `inset(0 0 0 ${100 * effectSplit}%)`,
                  }}
                >
                  <Film src="creative-polish-v2/city-grade-10.mp4" />
                </div>
                <div
                  style={{
                    ...A,
                    inset: 0,
                    opacity: smooth(s, 4.1, 5.1),
                    clipPath: `inset(0 0 0 ${100 * effectSplit}%)`,
                  }}
                >
                  <Film src="creative-polish-v2/city-mono-10.mp4" />
                </div>
              </At>
              <div
                style={{
                  ...A,
                  left: effectSplit * 1000,
                  top: 0,
                  bottom: 0,
                  width: 2,
                  background: paper,
                  opacity: 1 - smooth(s, 6.5, 7.3),
                }}
              />
              <Label
                style={{
                  ...A,
                  left: 18,
                  top: 20,
                  fontSize: 16,
                  color: paper,
                  padding: 10,
                  background: "#09111fce",
                  opacity: 1 - smooth(s, 6.5, 7.3),
                }}
              >
                BEFORE
              </Label>
              <Label
                style={{
                  ...A,
                  right: 18,
                  top: 20,
                  fontSize: 16,
                  padding: 10,
                  background: "#09111fce",
                }}
              >
                NATIVE EFFECTS
              </Label>
            </div>
            <div
              style={{
                ...A,
                left: 50,
                top: 640,
                width: 900,
                display: "flex",
                gap: 20,
              }}
            >
              {[
                ["COLOR GRADE", smooth(s, 1, 2)],
                ["MONOCHROME", smooth(s, 4.1, 5.1)],
              ].map(([label, active]) => (
                <div
                  key={String(label)}
                  style={{
                    padding: 22,
                    width: 420,
                    border: "1px solid #a7b6c94a",
                    borderRadius: 12,
                    background: "#14202c",
                    fontFamily: font.mono,
                    color: paper,
                    fontSize: 19,
                  }}
                >
                  <span
                    style={{
                      display: "inline-block",
                      width: 17,
                      height: 17,
                      borderRadius: 20,
                      background: Number(active) > 0 ? lime : "#495563",
                      marginRight: 18,
                    }}
                  />
                  {label}
                  <span style={{ float: "right", color: lime }}>
                    {Math.round(Number(active) * 100)}%
                  </span>
                </div>
              ))}
            </div>
            <Label style={{ ...A, left: 50, top: 745, fontSize: 15 }}>
              GRADE: EXPOSURE +0.25 / CONTRAST +14 / VIBRANCE +12
            </Label>
          </>
        )}
        {phase === 4 && (
          <>
            <div
              style={{
                ...A,
                left: 70,
                top: 0,
                width: 860,
                height: 484,
                overflow: "hidden",
                borderRadius: 10,
              }}
            >
              <At
                from={start}
                seconds={6}
                name="Footage with its actual location audio"
              >
                <Film src="creative-polish-v2/city-10.mp4" />
              </At>
            </div>
            {[
              ["MUSIC", audioPeaks.music, 10 ** (mixer.musicDb / 20)],
              ["LOCATION", audioPeaks.location, mixer.locationGain],
            ].map(([name, samples, gain], i) => (
              <div
                key={String(name)}
                style={{
                  ...A,
                  left: 20,
                  top: 510 + i * 125,
                  width: 960,
                  height: 104,
                  padding: 15,
                  background: i ? "#204e43" : "#294668",
                  borderRadius: 9,
                }}
              >
                <Label style={{ fontSize: 15, color: paper }}>
                  {name}{" "}
                  <span style={{ float: "right" }}>
                    {i
                      ? mixer.locationGain > 0.05
                        ? "ON / 0 dB"
                        : "MUTED"
                      : `${mixer.musicDb.toFixed(1)} dB`}
                  </span>
                </Label>
                <Wave
                  samples={samples as number[]}
                  gain={Number(gain)}
                  height={64}
                />
              </div>
            ))}
            <div
              style={{
                ...A,
                left: 35 + (s / 6) * 930,
                top: 510,
                height: 230,
                width: 2,
                background: lime,
              }}
            />
          </>
        )}
        {phase === 5 && (
          <>
            <div
              style={{
                ...A,
                left: 15,
                top: 30,
                width: 970,
                height: 545,
                overflow: "hidden",
                borderRadius: 10,
              }}
            >
              <At
                from={start}
                seconds={6}
                name="Rec.709 source and delivery settings"
              >
                <Film src="creative-polish-v2/city-grade-10.mp4" />
              </At>
              <div
                style={{
                  ...A,
                  left: 30,
                  top: 28,
                  padding: 20,
                  width: 425,
                  background: "#101923ee",
                  border: "1px solid #8a9dae6b",
                  borderRadius: 10,
                }}
              >
                <Label style={{ fontSize: 14 }}>CURRENT SOURCE / Rec.709</Label>
                {s > 1 &&
                  s < 4.5 &&
                  [
                    "sRGB",
                    "Rec.709",
                    "Sony S-Log2 / S-Gamut",
                    "Sony S-Log3 / S-Gamut3",
                  ].map((v, i) => (
                    <div
                      key={v}
                      style={{
                        fontFamily: font.sans,
                        fontSize: 21,
                        color: i === 1 ? lime : paper,
                        padding: "11px 0",
                        borderBottom: "1px solid #a2b5c526",
                        opacity: out(s, 1 + i * 0.1, 1.4 + i * 0.1),
                      }}
                    >
                      {v}
                    </div>
                  ))}
              </div>
            </div>
            <Pills
              items={["3840 × 2160", "30 FPS", "PRORES / MOV", "COLOR-MANAGED"]}
              style={{ ...A, left: 20, top: 640 }}
            />
          </>
        )}
        {phase === 6 && (
          <div
            style={{
              ...A,
              left: 0,
              top: 25,
              width: 1000,
              height: 665,
              overflow: "hidden",
              borderRadius: 14,
            }}
          >
            <At from={start} seconds={6} name="Genuine Gaussian camera render">
              <Film src="studio/gaussian-camera.mp4" trim={3} />
            </At>
            <Label style={{ ...A, left: 25, bottom: 25, fontSize: 17 }}>
              GAUSSIAN SCENE / CAMERA MOTION
            </Label>
          </div>
        )}
        {phase === 7 && (
          <>
            <div
              style={{
                ...A,
                left: 0,
                top: 65,
                width: 830,
                height: 467,
                overflow: "hidden",
                borderRadius: 12,
                transform: "perspective(1500px) rotateY(-7deg)",
                boxShadow: "0 30px 70px #0008",
              }}
            >
              <At from={start} seconds={8} name="Landscape film delivery">
                <Film src="creative-polish-v2/arrival-12.mp4" />
                <div
                  style={{
                    ...A,
                    left: 30,
                    bottom: 35,
                    fontFamily: font.serif,
                    fontStyle: "italic",
                    fontSize: 65,
                    color: paper,
                  }}
                >
                  A little further.
                </div>
              </At>
            </div>
            <div
              style={{
                ...A,
                left: 690,
                top: 230,
                width: 260,
                height: 462,
                overflow: "hidden",
                border: "5px solid #c4d0d4",
                borderRadius: 18,
                transform: `translateY(${90 * (1 - out(s, 0.3, 1.3))}px) rotate(5deg)`,
                boxShadow: "0 25px 70px #0009",
              }}
            >
              <At
                from={start}
                seconds={8}
                name="Separately framed social delivery"
              >
                <Film src="creative-polish-v2/arrival-12.mp4" />
                <div
                  style={{
                    ...A,
                    left: 20,
                    right: 20,
                    top: 55,
                    fontFamily: font.display,
                    fontSize: 52,
                    lineHeight: 1,
                    color: paper,
                  }}
                >
                  A LITTLE
                  <br />
                  FURTHER.
                </div>
              </At>
            </div>
            <Pills
              items={[
                "FULL PRODUCTIONS",
                "SOCIAL CUTS",
                "HIGH-RESOLUTION DELIVERY",
              ]}
              style={{ ...A, left: 10, top: 745 }}
            />
          </>
        )}
      </Stage>
      {phase === 1 && s < 1.2 && (
        <div
          style={{
            ...A,
            left: mix(0, (portrait ? 40 : 650) + 26 * stageScale, reveal),
            top: mix(0, (portrait ? 710 : 120) + 106 * stageScale, reveal),
            width: mix(width, 700 * stageScale, reveal),
            height: mix(height, 394 * stageScale, reveal),
            borderRadius: 7 * stageScale * reveal,
            overflow: "hidden",
            zIndex: 10,
          }}
        >
          <EditPreview
            clock={11.95}
            textSize={mix(portrait ? 100 : 130, 59 * stageScale, reveal)}
            titleBottom={mix(portrait ? 230 : 35, 35 * stageScale, reveal)}
            insetScale={mix(1, stageScale, reveal)}
          />
        </div>
      )}
      <Footer
        left={
          phase === 6
            ? "HOUSEPLANT / MARCEL PADILLA / CC BY 4.0"
            : "FROM FIRST CUT TO FINAL FRAME."
        }
        right="COLOR · MOTION · LAYERS · AUDIO"
      />
    </>
  );
};
