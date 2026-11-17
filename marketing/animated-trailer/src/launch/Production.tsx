import { Video } from "@remotion/media";
import {
  CanvasImage,
  Sequence,
  interpolateColors,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import {
  AppIcon,
  AppInspectorSlider,
  AppOpticalField,
} from "../app-kit/AppElements";
import {
  A,
  asset,
  C,
  Copy,
  Grain,
  land,
  lerp,
  Meta,
  mono,
  p,
  Photo,
  Plate,
  pop,
  Wipe,
} from "./design";
import { CampaignArtwork, CampaignPreview, TextMaskArtwork } from "./Artwork";
import { sourceWaveforms, projectMusicWaveform } from "./waveforms";
import { NativeMixer } from "./NativeDetails";

const ProgramPicture = ({ t, tail = false }: { t: number; tail?: boolean }) => {
  const { fps } = useVideoConfig(),
    blur =
      ((p(t, 11.5, 12.4, 0, 0.9) - p(t, 13.2, 14, 0, 0.9)) * 18 * 920) / 3840;
  if (tail)
    return (
      <Video
        objectFit="cover"
        src={staticFile("media/MVI_5051-2.3-5.9.mp4")}
        trimBefore={Math.round((7 - 1 / fps) * 0.5 * fps)}
        playbackRate={0.05}
        muted
        style={{ width: "100%", height: "100%" }}
      />
    );
  return (
    <>
      <Sequence durationInFrames={8 * fps} layout="none">
        <Video
          objectFit="cover"
          src={staticFile("media/launch-1207.mp4")}
          muted
          style={{ width: "100%", height: "100%" }}
        />
      </Sequence>
      <Sequence from={8 * fps} durationInFrames={7 * fps} layout="none">
        <Video
          objectFit="cover"
          src={staticFile("media/launch-1268.mp4")}
          muted
          style={{
            width: "100%",
            height: "100%",

            filter: `blur(${blur}px)`,
          }}
        />
      </Sequence>
      <Sequence from={15 * fps} durationInFrames={7 * fps} layout="none">
        <Video
          objectFit="cover"
          src={staticFile("media/MVI_5051-2.3-5.9.mp4")}
          playbackRate={0.5}
          muted
          style={{ width: "100%", height: "100%" }}
        />
      </Sequence>
      <div
        style={{
          ...A,
          left: 0,
          right: 0,
          top: "39%",
          fontFamily: "Manrope",
          fontWeight: 800,
          fontSize: 64,
          letterSpacing: -4,
          textAlign: "center",
          color: "white",
          textShadow: "0 2px 5px #0005",
          opacity: land(t, 2, 2.5) * (1 - p(t, 7.3, 8)),
          translate: `${land(t, 2, 3.1, -260, 0)}px 0`,
        }}
      >
        A DIFFERENT PERSPECTIVE.
      </div>
      <div
        style={{
          ...A,
          left: 0,
          right: 0,
          top: "39%",
          fontFamily: "Manrope",
          fontWeight: 800,
          fontSize: 82,
          letterSpacing: -4,
          textAlign: "center",
          color: "white",
          opacity: land(t, 8.6, 9) * (1 - p(t, 14.3, 15)),
          translate: `${land(t, 8.6, 10.5, -300, 0)}px 0`,
        }}
      >
        NEPAL.
      </div>
    </>
  );
};
const Waveform = ({
  values,
  width,
  height = 90,
  color = "#9eaaa1",
}: {
  values: number[];
  width: number;
  height?: number;
  color?: string;
}) => (
  <svg
    width={width}
    height={height}
    viewBox={`0 0 ${width} ${height}`}
    preserveAspectRatio="none"
  >
    <path
      d={values
        .map(
          (v, i) =>
            `M${(i / (values.length - 1)) * width} ${height * 0.5 - v * height * 0.43}V${height * 0.5 + v * height * 0.43}`,
        )
        .join(" ")}
      fill="none"
      stroke={color}
      strokeWidth={1.1}
    />
  </svg>
);

export const ProductionShot = ({
  t,
  tail = false,
  programStill,
}: {
  t: number;
  tail?: boolean;
  programStill?: string;
}) => {
  const unfold = p(t, 0, 1.4),
    editor = land(t, 1.4, 2.5),
    keyWindow = p(t, 8.4, 9.1) * (1 - p(t, 11, 11.5)),
    fxWindow = p(t, 11.3, 11.9) * (1 - p(t, 14.2, 14.7));
  const titleWidth = lerp((9.5 / 22) * 780, (8 / 22) * 780, p(t, 5, 6)),
    playhead = 220 + (t / 22) * 780;
  const curveTime = Math.max(0, Math.min(1, (t - 8.6) / (10.5 - 8.6))),
    bx = 520 * curveTime,
    by = 180 - 165 * land(t, 8.6, 10.5);
  const trimFocus = p(t, 2.8, 3.6) * (1 - p(t, 6.2, 6.9));
  const editFocus = p(t, 8.3, 8.8) * (1 - p(t, 14.4, 14.8));
  const audioFocus = p(t, 14.8, 15.6) * (1 - p(t, 17.3, 18));
  const cinema = p(t, 18, 19.4);
  const cameraScale =
    1 + 0.85 * trimFocus + 0.28 * editFocus + 0.4 * audioFocus + cinema;
  const cameraX =
    -489 * trimFocus - 245 * editFocus - 174 * audioFocus - 300 * cinema;
  const cameraY =
    -1255 * trimFocus - 250 * editFocus - 1159 * audioFocus - 500 * cinema;
  const copyOpacity = 1 - Math.min(1, (cameraScale - 1) * 5);
  const gain = p(t, 15.5, 16.7, -6, 0);
  return (
    <Plate>
      <div style={{ ...A, left: 80, top: 108, opacity: editor * copyOpacity }}>
        <Copy t={t} start={1.4} size={108}>
          <div>A whole</div>
          <div>production.</div>
        </Copy>
      </div>
      <div
        style={{
          ...A,
          inset: 0,
          transformOrigin: "0 0",
          scale: cameraScale,
          opacity: 1 - audioFocus,
          translate: `${cameraX}px ${cameraY}px`,
        }}
      >
        <div
          style={{
            ...A,
            left: 80,
            top: lerp(340, 440, unfold),
            width: 920,
            height: lerp(1150, 518, unfold),
            overflow: "hidden",
          }}
        >
          <div style={{ ...A, inset: 0, opacity: 1 - p(t, 0.3, 1.1) }}>
            <CampaignPreview />
          </div>
          <div style={{ ...A, inset: 0, opacity: p(t, 0.5, 1.4) }}>
            {programStill ? (
              <CanvasImage
                src={staticFile(programStill)}
                style={{ width: "100%", height: "100%", objectFit: "cover" }}
              />
            ) : (
              <ProgramPicture t={t} tail={tail} />
            )}
          </div>
        </div>
        <div
          style={{
            ...A,
            left: 0,
            top: 1030,
            width: 1080,
            height: 650,
            background: C.ink900,
            opacity: editor * (1 - cinema),
            translate: `0 ${land(t, 1.4, 2.5, 160, 0)}px`,
          }}
        >
          <div
            style={{
              display: "flex",
              justifyContent: "space-between",
              height: 90,
              padding: "27px 80px",
              fontSize: 27,
              borderBottom: "1px solid #393939",
            }}
          >
            <span style={{ opacity: copyOpacity }}>NEPAL / MASTER</span>
            <span style={{ fontFamily: mono }}>3840 × 2160 / 30 fps</span>
          </div>
          <div
            style={{
              ...A,
              left: 220,
              top: 112,
              width: 780,
              fontFamily: mono,
              fontSize: 21,
              color: C.textMuted,
              display: "flex",
              justifyContent: "space-between",
            }}
          >
            {[0, 5, 10, 15, 20].map((n) => (
              <span key={n}>00:{String(n).padStart(2, "0")}</span>
            ))}
          </div>
          {["V3 / TITLES", "V2 / PICTURE", "A1 / SOUND", "A2 / MUSIC"].map(
            (name, i) => (
              <div
                key={name}
                style={{
                  ...A,
                  left: 0,
                  top: 157 + i * 112,
                  width: 1080,
                  height: 102,
                  borderTop: "1px solid #393939",
                }}
              >
                <div
                  style={{
                    ...A,
                    left: 45,
                    top: 34,
                    fontFamily: mono,
                    fontSize: 23,
                    color: C.textMuted,
                  }}
                >
                  {name}
                </div>
              </div>
            ),
          )}
          <div
            style={{
              ...A,
              left: 220,
              top: 168,
              width: titleWidth,
              height: 78,
              background: "#394658",
              border: "1px solid #8295ad",
              overflow: "hidden",
              fontSize: 22,
              padding: "22px 16px",
              boxSizing: "border-box",
              whiteSpace: "nowrap",
              textOverflow: "ellipsis",
            }}
          >
            A DIFFERENT PERSPECTIVE.
            <div
              style={{
                ...A,
                left: 6,
                right: 6,
                bottom: 8,
                height: 1,
                background: "#758aa4",
              }}
            />
            {t > 9 && <AppIcon name="keyframe" size={20} color="white" />}
          </div>
          <div
            style={{
              ...A,
              left: 220 + (8 / 22) * 780,
              top: 168,
              width: (7 / 22) * 780,
              height: 78,
              background: "#394658",
              border: "1px solid #8295ad",
              fontSize: 22,
              padding: "22px 16px",
              boxSizing: "border-box",
              whiteSpace: "nowrap",
              overflow: "hidden",
              textOverflow: "ellipsis",
            }}
          >
            NEPAL.
          </div>
          {[
            ["IMG_1207-2-7", 0, 8],
            ["IMG_1268-2-7", 8, 7],
            ["IMG_5071", 15, 7],
          ].map(([name, start, duration]) => (
            <div
              key={name}
              style={{
                ...A,
                left: 220 + (Number(start) / 22) * 780,
                top: 280,
                width: (Number(duration) / 22) * 780 - 2,
                height: 99,
                overflow: "hidden",
                background: "#30373f",
                border: "1px solid #7d8794",
              }}
            >
              <div style={{ height: 62, display: "flex" }}>
                {[0, 1, 2].map((i) => (
                  <div key={i} style={{ width: "34%", height: "100%" }}>
                    <Photo name={String(name)} />
                  </div>
                ))}
              </div>
              <div
                style={{ fontFamily: mono, fontSize: 17, padding: "7px 9px" }}
              >
                {name}
              </div>
            </div>
          ))}
          <div
            style={{
              ...A,
              left: 220,
              top: 392,
              width: 780,
              height: 96,
              background: "#25322b",
              overflow: "hidden",
            }}
          >
            <Waveform
              values={[
                ...sourceWaveforms["1207"]
                  .slice(0, 280)
                  .map((v) => v * 10 ** (gain / 20)),
                ...sourceWaveforms["1268"].slice(0, 260),
              ]}
              width={780}
              height={93}
            />
          </div>
          <div
            style={{
              ...A,
              left: 220,
              top: 504,
              width: 780,
              height: 96,
              background: "#28352e",
              overflow: "hidden",
            }}
          >
            <Waveform
              values={projectMusicWaveform}
              width={780}
              height={93}
              color="#8d9d91"
            />
          </div>
          <div
            style={{
              ...A,
              left: playhead,
              top: 140,
              width: 2,
              height: 470,
              background: "white",
            }}
          >
            <div
              style={{
                width: 12,
                height: 12,
                background: "white",
                translate: "-5px -3px",
                rotate: "45deg",
              }}
            />
          </div>
          {t > 5 && t < 6.3 && (
            <div
              style={{
                ...A,
                left: 220 + titleWidth - 4,
                top: 168,
                width: 9,
                height: 78,
                background: "white",
              }}
            />
          )}
        </div>
        <div
          style={{
            ...A,
            left: 80,
            top: 1740,
            fontSize: 39,
            letterSpacing: -1,
            opacity: editor * copyOpacity,
          }}
        >
          Tracks. Keyframes. Effects. Sound.
        </div>
        <Meta
          style={{
            ...A,
            left: 80,
            top: 1820,
            fontSize: 25,
            opacity: editor * copyOpacity,
          }}
        >
          Rec.709 OUTPUT / sRGB / SONY S-LOG2 & S-LOG3 INPUT OPTIONS
        </Meta>
        {keyWindow > 0.001 && (
          <div
            style={{
              ...A,
              left: 330,
              top: 1180,
              width: 670,
              height: 385,
              padding: "28px 34px",
              boxSizing: "border-box",
              background: C.ink850,
              border: "1px solid #585858",
              opacity: keyWindow,
              translate: `0 ${(1 - keyWindow) * 80}px`,
            }}
          >
            <div style={{ fontSize: 30 }}>Motion / easing</div>
            <svg
              width={520}
              height={210}
              viewBox="0 0 520 210"
              style={{ marginTop: 32, overflow: "visible" }}
            >
              <path d="M0 180H520M0 10V180" fill="none" stroke="#585858" />
              <path
                d="M0 180C114.4 15 187.2 15 520 15"
                fill="none"
                stroke="white"
                strokeWidth={3}
              />
              <circle cx={bx} cy={by} r={8} fill={C.brandPaper} />
            </svg>
            <div style={{ fontSize: 24, color: C.textMuted }}>
              Title position / editable keyframes
            </div>
          </div>
        )}
        {fxWindow > 0.001 && (
          <div
            style={{
              ...A,
              left: 255,
              top: 1200,
              width: 720,
              padding: "30px 22px",
              boxSizing: "border-box",
              background: C.ink850,
              border: "1px solid #585858",
              opacity: fxWindow,
            }}
          >
            <div style={{ fontSize: 34, margin: "0 0 20px 12px" }}>
              Blur / effect amount
            </div>
            <AppInspectorSlider
              label="Amount"
              value={p(t, 11.5, 12.4, 0, 0.9) - p(t, 13.2, 14, 0, 0.9)}
              valueText={`${Math.round((p(t, 11.5, 12.4, 0, 0.9) - p(t, 13.2, 14, 0, 0.9)) * 100)}%`}
              from={0}
              to={1}
              defaultValue={0.3}
              scale={2.1}
              width={313}
              press={
                p(t, 11.4, 11.5) * (1 - p(t, 12.4, 12.5)) +
                p(t, 13.1, 13.2) * (1 - p(t, 14, 14.1))
              }
              showCursor={(t > 11.5 && t < 12.5) || (t > 13.2 && t < 14.1)}
            />
          </div>
        )}
      </div>
      <div style={{ ...A, inset: 0, opacity: audioFocus }}>
        <NativeMixer t={t} />
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 115,
          fontSize: 110,
          fontWeight: 600,
          letterSpacing: -5,
          opacity: audioFocus,
        }}
      >
        Sound.
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1550,
          fontSize: 110,
          fontWeight: 600,
          letterSpacing: -5,
          lineHeight: 0.98,
          opacity: cinema,
        }}
      >
        Moving
        <br />
        pictures.
      </div>
      <Grain />
    </Plate>
  );
};

export const CarouselArtwork = ({
  index,
  accent = C.brandBlue,
  moving = false,
}: {
  index: number;
  accent?: string;
  moving?: boolean;
}) => {
  const { fps } = useVideoConfig();
  if (index === 0) return <CampaignArtwork t={8} accent={accent} />;
  if (index === 4)
    return (
      <>
        <TextMaskArtwork t={4} />
        <svg
          width="100%"
          height="100%"
          viewBox="0 0 1200 1500"
          style={{ ...A, inset: 0 }}
        >
          <g fill={C.brandPaper} fontFamily={mono} fontSize={27}>
            <text x={64} y={100}>
              NEPAL / ANOTHER PERSPECTIVE
            </text>
            <text x={1136} y={1450} textAnchor="end">
              05 / 05
            </text>
          </g>
          <rect x={64} y={1355} width={200} height={12} fill={accent} />
        </svg>
      </>
    );
  const photo = ["IMG_5071", "IMG_1268-2-7", "IMG_4856", "IMG_4969"][index - 1],
    light = index % 2 === 1;
  const lines = [
    ["FIRST", "LIGHT."],
    ["TAKE", "IT IN."],
    ["KEEP", "LOOKING."],
    ["IN", "BETWEEN."],
  ][index - 1];
  return (
    <>
      <svg
        width="100%"
        height="100%"
        viewBox="0 0 1200 1500"
        preserveAspectRatio="xMidYMid meet"
        style={{ ...A, inset: 0 }}
      >
        <rect
          width={1200}
          height={1500}
          fill={light ? C.brandPaper : C.ink900}
        />
        <image
          href={asset(photo)}
          x={64}
          y={475}
          width={1072}
          height={790}
          preserveAspectRatio="xMidYMid slice"
        />
        <g
          fill={light ? C.brandInk : C.brandPaper}
          fontFamily="Manrope"
          fontSize={180}
          fontWeight={800}
          letterSpacing={-10}
        >
          <text x={58} y={225}>
            {lines[0]}
          </text>
          <text x={58} y={400}>
            {lines[1]}
          </text>
        </g>
        {index === 1 ? (
          <rect x={64} y={1320} width={330} height={22} fill={accent} />
        ) : index === 2 ? (
          <rect
            x={52}
            y={463}
            width={1096}
            height={814}
            fill="none"
            stroke={accent}
            strokeWidth={24}
          />
        ) : (
          <circle cx={1050} cy={1340} r={48} fill={accent} />
        )}
        <g
          fill={light ? C.brandInk : C.brandPaper}
          fontFamily={mono}
          fontSize={27}
          letterSpacing={1}
        >
          <text x={64} y={1450}>
            NEPAL / FIELD STUDY
          </text>
          <text x={1136} y={1450} textAnchor="end">
            0{index + 1} / 05
          </text>
        </g>
      </svg>
      {moving && index === 2 && (
        <div
          style={{
            ...A,
            left: "5.333%",
            top: "31.667%",
            width: "89.333%",
            height: "52.667%",
            overflow: "hidden",
          }}
        >
          <Sequence durationInFrames={14 * fps} layout="none">
            <Video
              objectFit="cover"
              src={staticFile("media/launch-1268.mp4")}
              playbackRate={0.75}
              muted
              style={{ width: "100%", height: "100%" }}
            />
          </Sequence>
        </div>
      )}
    </>
  );
};

export const CarouselShot = ({ t }: { t: number }) => {
  const zoom = p(t, 8.1, 9.5),
    pan = p(t, 1.3, 2.3) + p(t, 3.0, 4.0) + p(t, 4.7, 5.7) + p(t, 6.4, 7.4),
    reorder = pop(t, 11.6, 12.8);
  const accent = interpolateColors(
    t,
    [10.1, 10.7],
    [C.brandBlue, C.brandCoral],
  );
  return (
    <Plate>
      <div style={{ ...A, left: 80, top: 110 }}>
        <Copy t={t} start={0.6} size={104}>
          <div>A carousel.</div>
          <div>Designed as one.</div>
        </Copy>
      </div>
      {[0, 1, 2, 3, 4].map((index) => {
        const position =
          index === 1
            ? lerp(1, 2, reorder)
            : index === 2
              ? lerp(2, 1, reorder)
              : index;
        const column = position < 3 ? position : position - 3,
          gridX = position < 3 ? 80 + column * 325 : 242 + column * 325,
          gridY = position < 3 ? 600 : 1015;
        return (
          <div
            key={index}
            style={{
              ...A,
              left: lerp(80 + (index - pan) * 948, gridX, zoom),
              top: lerp(430, gridY, zoom),
              width: lerp(920, 270, zoom),
              height: lerp(1150, 337.5, zoom),
              overflow: "hidden",
              transformOrigin: "50% 50%",
              transform: `perspective(1800px) rotateY(${land(t, index * 0.08, 1 + index * 0.08, -25, 0)}deg) rotateZ(${Math.sin(Math.PI * reorder) * (index === 1 ? -5 : index === 2 ? 5 : 0)}deg)`,
              zIndex: index === 1 ? 3 : 2,
            }}
          >
            <CarouselArtwork
              index={index}
              accent={accent}
              moving={index === 2}
            />
          </div>
        );
      })}
      <div
        style={{
          ...A,
          left: 80,
          top: 1470,
          width: 920,
          opacity: p(t, 8, 9.5),
          background: C.ink900,
          padding: "28px 28px 30px",
          boxSizing: "border-box",
        }}
      >
        <div
          style={{
            fontSize: 34,
            display: "flex",
            gap: 20,
            alignItems: "center",
          }}
        >
          <AppIcon name="carousel" size={34} />
          Brand styles / linked boards
        </div>
        <div
          style={{
            display: "flex",
            gap: 18,
            alignItems: "center",
            marginTop: 28,
          }}
        >
          {[C.brandBlue, C.brandCoral, C.brandPaper].map((c) => (
            <div
              key={c}
              style={{
                width: 46,
                height: 46,
                background: c,
                border: `2px solid ${t > 10.4 && c === C.brandCoral ? "white" : "transparent"}`,
              }}
            />
          ))}
          <span style={{ fontSize: 28, color: C.textMuted, marginLeft: 15 }}>
            Apply across the set
          </span>
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          right: 80,
          top: 1760,
          display: "flex",
          justifyContent: "space-between",
          fontSize: 34,
          opacity: p(t, 9, 10),
        }}
      >
        <span>Layout / reorder / preview</span>
        <span>Export 5 boards →</span>
      </div>
      <Grain />
    </Plate>
  );
};

export const DeliveryShot = ({ t }: { t: number }) => {
  const done = p(t, 1, 6.2);
  return (
    <Plate>
      <div style={{ ...A, left: 80, top: 130 }}>
        <Copy t={t} start={0.1} size={109}>
          <div>Full resolution.</div>
          <div>Ready to deliver.</div>
        </Copy>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 480,
          width: 420,
          height: 630,
          overflow: "hidden",
          opacity: land(t, 0.2, 0.8),
        }}
      >
        <Photo />
      </div>
      <div
        style={{
          ...A,
          left: 580,
          top: 480,
          width: 420,
          height: 525,
          opacity: land(t, 0.3, 1),
        }}
      >
        <CampaignArtwork t={8} />
      </div>
      <div
        style={{
          ...A,
          left: 580,
          top: 1045,
          width: 420,
          height: 236,
          overflow: "hidden",
          opacity: land(t, 0.4, 1.2),
        }}
      >
        <Video
          objectFit="cover"
          src={staticFile("media/launch-1207.mp4")}
          muted
          style={{ width: "100%", height: "100%" }}
        />
        <div
          style={{
            ...A,
            left: 20,
            bottom: 20,
            fontSize: 27,
            fontFamily: "Manrope",
            fontWeight: 800,
          }}
        >
          NEPAL.
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1150,
          width: 420,
          height: 130,
          display: "flex",
          gap: 8,
          opacity: land(t, 0.5, 1.4),
        }}
      >
        {[0, 1, 2, 3, 4].map((i) => (
          <div key={i} style={{ position: "relative", width: 78, height: 98 }}>
            <CarouselArtwork index={i} />
          </div>
        ))}
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1360,
          width: 920,
          padding: "20px 26px 30px",
          boxSizing: "border-box",
          background: C.ink900,
        }}
      >
        {[
          ["Photo", "4000 × 6000 / sRGB"],
          ["Design", "1200 × 1500 / PNG"],
          ["Film", "3840 × 2160 / Rec.709"],
          ["Carousel", "5 boards / 1080 × 1350"],
        ].map(([label, detail], i) => (
          <div
            key={label}
            style={{
              height: 75,
              display: "flex",
              alignItems: "center",
              gap: 24,
              borderBottom: i < 3 ? "1px solid #393939" : "none",
              fontSize: 31,
            }}
          >
            <AppIcon
              name={done >= (i + 1) / 4 ? "check" : "image"}
              size={28}
              color={done >= (i + 1) / 4 ? C.brandPaper : C.textMuted}
            />
            <span style={{ width: 160 }}>{label}</span>
            <span
              style={{ fontFamily: mono, fontSize: 27, color: C.textMuted }}
            >
              {detail}
            </span>
          </div>
        ))}
        <div style={{ height: 6, marginTop: 26 }}>
          <AppOpticalField
            width={868}
            height={6}
            kind={2}
            progress={done}
            active={done < 1}
          />
        </div>
      </div>
      <Grain />
    </Plate>
  );
};

export const ProductionScene = () => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <ProductionShot t={f / fps} />;
};
export const CarouselScene = () => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  const t = f / fps;
  return (
    <Plate>
      {t < 0.85 && (
        <div style={{ ...A, inset: 0, opacity: 1 - p(t, 0, 0.55) }}>
          <ProductionShot t={22 - 1 / fps} tail />
        </div>
      )}
      <Wipe amount={p(t, 0, 0.85)} phase={t + 22}>
        <CarouselShot t={t} />
      </Wipe>
    </Plate>
  );
};
export const DeliveryScene = () => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <DeliveryShot t={f / fps} />;
};
