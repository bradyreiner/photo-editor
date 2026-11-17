import { GalleryDelivery, PhotoDelivery } from "./BuyerScenes";
import { BeforeAfter, PixelProof } from "./ProofTools";
import { nativeCurve } from "./recipe-data";
import {
  A,
  Pic,
  useShot,
  smooth,
  out,
  mix,
  Light,
  Caption,
  Stage,
  Surface,
  Slider,
  Label,
  Pills,
  Type,
  Footer,
  font,
  lime,
  paper,
} from "./Kit";
import { photoPhases, phaseAt, curveState, detailZoom } from "./State";

export const Plate = ({
  folder,
  prefix,
  progress,
  count = 8,
}: {
  folder: string;
  prefix: string;
  progress: number;
  count?: number;
}) => {
  const n = Math.max(0, Math.min(count, progress * count)),
    a = Math.floor(n),
    b = Math.min(count, a + 1);
  return (
    <>
      <Pic src={`${folder}/${prefix}${a}.png`} />
      {a !== b && (
        <Pic src={`${folder}/${prefix}${b}.png`} style={{ opacity: n - a }} />
      )}
    </>
  );
};
export const TonePlot = ({ s }: { s: number }) => {
  const { shadow, highlight, progress } = curveState(s),
    n = progress * 16,
    a = Math.floor(n),
    b = Math.min(16, a + 1),
    f = n - a;
  const path = nativeCurve.x
    .map(
      (x, i) =>
        `${i ? "L" : "M"}${20 + x * 320} ${20 + (1 - mix(nativeCurve.frames[a][i], nativeCurve.frames[b][i], f)) * 260}`,
    )
    .join(" ");
  const p = { x: 20 + 0.22 * 320, y: 20 + (1 - (0.22 - 0.06 * shadow)) * 260 },
    q = { x: 20 + 0.55 * 320, y: 20 + (1 - (0.55 + 0.05 * highlight)) * 260 };
  const travel = smooth(s, 2.8, 3.45),
    cx = s < 2.8 ? p.x : mix(p.x, q.x, travel),
    cy = s < 2.8 ? p.y : mix(p.y, q.y, travel);
  return (
    <svg viewBox="0 0 360 330" width="360" height="330">
      <rect width="360" height="330" rx="12" fill="#111820f2" />
      {[0, 1, 2, 3, 4].map((i) => (
        <g key={i} stroke="#bfccd328">
          <path d={`M${20 + i * 80} 20V280`} />
          <path d={`M20 ${20 + i * 65}H340`} />
        </g>
      ))}
      <path d="M20 280L340 20" stroke="#aeb9c337" />
      <path d={path} stroke={paper} strokeWidth="3" fill="none" />
      <circle cx={p.x} cy={p.y} r="6" fill={lime} />
      <circle cx={q.x} cy={q.y} r="6" fill={lime} />
      <g
        transform={`translate(${cx} ${cy})`}
        opacity={out(s, 0.2, 0.6) * (1 - smooth(s, 5.9, 6.4))}
      >
        <path
          d="M0 0L0 26L7 20L14 32L20 29L13 17L24 17Z"
          fill="white"
          stroke="#0d1723"
          strokeWidth="2"
        />
      </g>
      <text x="20" y="310" fill={lime} fontFamily={font.mono} fontSize="13">
        MASTER / TWO EDITABLE POINTS
      </text>
    </svg>
  );
};
const copy = [
  [
    "Start with the\ncamera original.",
    "RAW photo editing. Your camera data, with room to refine.",
  ],
  [
    "Find your\nexposure.",
    "Recover the frame. Shape the light. Keep the original.",
  ],
  [
    "Protect the highlights.\nOpen the shadows.",
    "Refine the bright and dark regions independently.",
  ],
  [
    "Color, from\nthe ground up.",
    "A custom color engine, from RAW development to color-managed output.",
  ],
  [
    "Shape the\nwhole image.",
    "Move a shadow point. Lift the midtones. The photograph follows.",
  ],
  [
    "Right down\nto the pixels.",
    "Check the fine detail. Refine selections at the original image resolution.",
  ],
  [
    "One color.\nYour call.",
    "Refine orange hues, saturation and luminance while the surrounding greens remain.",
  ],
  [
    "Finish for\nthe way you work.",
    "Native-resolution exports. JPEG, PNG and TIFF. Color-managed delivery.",
  ],
];
export const PhotoChapter = () => {
  const { t, portrait } = useShot(),
    { phase, s, duration } = phaseAt(t, photoPhases),
    u = smooth(s, 0.7, phase === 1 ? 4.5 : 3.8),
    curve = curveState(s),
    stage = phase <= 3 ? Math.max(0, phase - 1) : 3,
    amount = phase === 0 ? 0 : phase >= 4 ? 1 : u,
    zoom = detailZoom(s);
  if (phase === 7) return <PhotoDelivery s={s} />;
  const toolIn = out(s, 0, 0.45),
    toolOut = 1 - smooth(s, duration - 0.35, duration);
  return (
    <>
      <Light
        t={12 + t}
        opacity={phase === 1 ? 0.19 + 0.22 * u : 0.3}
        gather={phase === 4 ? 0.2 * curve.progress : 0}
      />
      <div
        style={{ opacity: phase === 5 && s < 5 ? 1 - smooth(s, 4.6, 4.95) : 1 }}
      >
        <Caption
          chapter="01 / PHOTO EDITOR"
          title={
            phase === 5 && s >= 5
              ? "Refine the edge.\nOne pixel at a time."
              : copy[phase][0]
          }
          detail={
            phase === 5 && s >= 5
              ? "Add or erase a single selection pixel. Keep neighboring pixels untouched."
              : copy[phase][1]
          }
          time={phase === 5 && s >= 5 ? s - 5 : s}
          serif={phase === 7}
        />
      </div>
      <Stage>
        {phase < 5 && (
          <>
            <Surface
              style={{
                top: 40,
                height: 667,
                transform: `perspective(1800px) rotateY(${mix(-4, 0, out(t, 0, 1.2))}deg)`,
                boxShadow: "0 30px 90px #0008",
              }}
            >
              {phase === 4 ? (
                <Plate
                  folder="creative-polish-v2/curve"
                  prefix="curve-"
                  count={16}
                  progress={curve.progress}
                />
              ) : (
                <Plate
                  folder="studio/raw-colour"
                  prefix={`${stage}-`}
                  progress={amount}
                />
              )}{" "}
              {phase >= 1 && (
                <BeforeAfter
                  before={
                    phase === 4
                      ? "creative-polish-v2/curve/curve-0.png"
                      : `studio/raw-colour/${stage}-0.png`
                  }
                  s={s}
                  finish={phase === 4 ? 5.8 : phase === 1 ? 4.9 : 4.1}
                />
              )}
              {phase === 4 && (
                <div
                  style={{
                    ...A,
                    right: 24,
                    bottom: 25,
                    opacity: out(s, 0, 0.45) * toolOut,
                    transform: `translateY(${18 * (1 - toolIn)}px)`,
                  }}
                >
                  <TonePlot s={s} />
                </div>
              )}
            </Surface>
            <Label style={{ ...A, left: 0, top: 0, fontSize: 17 }}>
              IMG_4969.CR2 / 6024 × 4020
            </Label>
            {phase === 1 && (
              <div
                style={{
                  ...A,
                  left: 95,
                  top: 710,
                  background: "#111a24",
                  padding: 22,
                  borderRadius: 12,
                  opacity: toolIn * toolOut,
                  transform: `translateY(${18 * (1 - toolIn)}px)`,
                }}
              >
                <Slider
                  label="Exposure"
                  value={1.4 * u}
                  text={`+${(1.4 * u).toFixed(2)} EV`}
                  cursor={s > 0.7 && s < 4.5}
                />
              </div>
            )}
            {phase === 2 && (
              <div
                style={{
                  ...A,
                  left: 0,
                  top: 725,
                  opacity: toolIn * toolOut,
                  transform: `translateY(${18 * (1 - toolIn)}px)`,
                }}
              >
                <Pills
                  items={[
                    `HIGHLIGHTS −${Math.round(35 * u)}`,
                    `SHADOWS +${Math.round(35 * u)}`,
                  ]}
                />
              </div>
            )}
            {phase === 3 && (
              <div
                style={{
                  ...A,
                  left: 10,
                  top: 720,
                  display: "flex",
                  gap: 18,
                  alignItems: "center",
                  opacity: toolIn * toolOut,
                  transform: `translateY(${18 * (1 - toolIn)}px)`,
                }}
              >
                {["CAMERA RAW", "LINEAR LIGHT", "LOOKSY COLOR", "sRGB"].map(
                  (name, i) => (
                    <div
                      key={name}
                      style={{
                        display: "flex",
                        alignItems: "center",
                        gap: 16,
                        opacity: out(s, i * 0.2, 0.5 + i * 0.2),
                      }}
                    >
                      <Label
                        style={{ fontSize: 16, color: i === 2 ? lime : paper }}
                      >
                        {name}
                      </Label>
                      {i < 3 && <span style={{ color: lime }}>→</span>}
                    </div>
                  ),
                )}
              </div>
            )}
          </>
        )}
        {phase === 5 && (
          <>
            <div
              style={{
                ...A,
                left: 0,
                top: 40,
                width: 1000,
                height: 667,
                opacity: 1 - zoom,
              }}
            >
              <Pic src="creative-polish-v2/curve/curve-16.png" />
            </div>
            <div
              style={{
                ...A,
                left: mix(((6024 * 0.57 - 480) / 6024) * 1000, 0, zoom),
                top: mix(40 + ((4020 * 0.59 - 480) / 4020) * 667, -35, zoom),
                width: mix(
                  (960 / 6024) * 1000,
                  portrait ? 960 : 960 / 1.1,
                  zoom,
                ),
                height: mix(
                  (960 / 4020) * 667,
                  portrait ? 960 : 960 / 1.1,
                  zoom,
                ),
                overflow: "hidden",
                borderRadius: mix(2, 14, zoom),
                border: "1px solid #d4e2e988",
                boxShadow: "0 35px 90px #0008",
              }}
            >
              <Plate
                folder="creative-polish-v2/detail"
                prefix="detail-"
                progress={smooth(s, 2.3, 4.8)}
              />
            </div>
            <PixelProof s={s} />
            <div
              style={{
                ...A,
                left: 20,
                top: portrait ? 840 : 740,
                opacity: out(s, 2, 2.5) * (1 - smooth(s, 5, 6)),
              }}
            >
              <Pills
                items={[
                  "960 × 960 SOURCE PIXELS",
                  `SHARPEN ${Math.round(32 * smooth(s, 2.3, 4.8))}`,
                  `NOISE ${Math.round(24 * smooth(s, 2.3, 4.8))}`,
                ]}
              />
            </div>
          </>
        )}
        {phase === 6 && (
          <>
            <Surface
              style={{
                left: 40,
                top: 0,
                width: 530,
                height: 795,
                transform: `rotate(${mix(-4, 0, out(s, 0, 1.2))}deg)`,
              }}
            >
              <Plate
                folder="creative-polish-v2/flower"
                prefix="mix-"
                progress={smooth(s, 1.1, 4.9)}
              />
              <BeforeAfter
                before="creative-polish-v2/flower/mix-0.png"
                s={s}
                finish={5.7}
                height={795}
                width={530}
              />
            </Surface>
            <div
              style={{
                ...A,
                left: 590,
                top: 135,
                width: 405,
                padding: 22,
                borderRadius: 15,
                background: "#121b25f5",
                border: "1px solid #a7b5bf44",
              }}
            >
              <Label
                style={{ fontSize: 17, marginBottom: 25, color: "#ff9b38" }}
              >
                COLOR MIX / ORANGES
              </Label>
              {[
                ["Hue", -8],
                ["Saturation", 16],
                ["Luminance", 8],
              ].map(([name, max]) => (
                <div
                  key={String(name)}
                  style={{
                    marginBottom: 28,
                    fontFamily: font.sans,
                    color: paper,
                    fontSize: 21,
                  }}
                >
                  {name}
                  <span style={{ float: "right", color: lime }}>
                    {Number(max) > 0 ? "+" : ""}
                    {Math.round(Number(max) * smooth(s, 1.1, 4.9))}
                  </span>
                  <div
                    style={{
                      position: "relative",
                      height: 4,
                      background:
                        name === "Saturation"
                          ? "linear-gradient(90deg,#6f6f6f,#ff941c)"
                          : name === "Luminance"
                            ? "linear-gradient(90deg,#151515,#eee8d8)"
                            : "linear-gradient(90deg,#d95425,#ffc25d)",
                      marginTop: 18,
                      borderRadius: 4,
                    }}
                  >
                    <div
                      style={{
                        ...A,
                        left: `${50 + Number(max) * smooth(s, 1.1, 4.9) * 0.5}%`,
                        marginLeft: -8,
                        top: -6,
                        width: 16,
                        height: 16,
                        borderRadius: 20,
                        background: paper,
                        boxShadow: "0 0 0 3px #0b0d1255",
                      }}
                    />
                  </div>
                </div>
              ))}
              <Label style={{ fontSize: 12, color: "#a8b4be", marginTop: 35 }}>
                IMG_4869.CR2 / CAMERA RAW
              </Label>
            </div>
          </>
        )}
        {phase === 7 && (
          <>
            <Surface style={{ left: 0, top: 40, width: 760, height: 507 }}>
              <Pic src="creative-polish-v2/curve/curve-16.png" />
            </Surface>
            <div
              style={{
                ...A,
                left: 650,
                top: 330,
                width: 300,
                height: 450,
                overflow: "hidden",
                borderRadius: 12,
                boxShadow: "0 25px 60px #0009",
                transform: `translateY(${90 * (1 - out(s, 0.3, 1.2))}px) rotate(5deg)`,
              }}
            >
              <Pic src="creative-polish-v2/flower/mix-8.png" />
            </div>
            <div style={{ ...A, left: 30, top: 610, display: "flex", gap: 16 }}>
              {["JPEG", "PNG", "TIFF"].map((format, i) => (
                <div
                  key={format}
                  style={{
                    width: 165,
                    height: 120,
                    background: "#15202d",
                    border: "1px solid #8ca0b755",
                    borderRadius: 12,
                    padding: 22,
                    transform: `translateY(${80 * (1 - out(s, 1.1 + i * 0.18, 2 + i * 0.18))}px)`,
                    opacity: out(s, 1 + i * 0.18, 1.6 + i * 0.18),
                  }}
                >
                  <Label style={{ fontSize: 25 }}>{format}</Label>
                  <div
                    style={{
                      fontFamily: font.mono,
                      color: "#b3c0ce",
                      fontSize: 12,
                      marginTop: 18,
                    }}
                  >
                    {format === "JPEG"
                      ? "CLIENT GALLERY"
                      : format === "PNG"
                        ? "DESIGN & WEB"
                        : "ARCHIVE & PRINT"}
                  </div>
                </div>
              ))}
            </div>
          </>
        )}
      </Stage>
      <Footer
        left="YOUR ORIGINAL. YOUR DECISIONS."
        right="CUSTOM COLOR ENGINE · NATIVE RESOLUTION"
      />
    </>
  );
};

const photos = [
  "IMG_4969",
  "IMG_5071",
  "IMG_4779",
  "IMG_4653",
  "IMG_4748",
  "IMG_4867",
  "IMG_4788",
  "IMG_5053",
  "IMG_5077",
  "IMG_5106",
  "IMG_5125",
  "IMG_5203",
];
const wedding = ["wedding-walk", "wedding-embrace", "wedding-play"];
export const LibraryChapter = () => {
  const { t } = useShot(),
    { phase, s } = phaseAt(t, [0, 5, 11, 17, 24, 30]);
  const titles = [
    "Bring the\nwhole shoot.",
    "Keep the\nkeepers.",
    "Remember the scene.\nFind the frame.",
    "One day.\nA whole story.",
    "One finish.\nAcross the set.",
  ];
  const details = [
    "Batch import your photographs and footage. Keep the work together.",
    "Compare nearby frames. Rate, pick and cull before the detailed edit.",
    "Visual AI search finds what is in the frame, beyond a filename.",
    "From the quiet moments to the celebration. Make a considered wedding gallery.",
    "Apply adjustments across a selection. Refine each photograph when it needs it.",
  ];
  if (phase === 3 || phase === 4)
    return <GalleryDelivery s={s} batch={phase === 4} />;
  return (
    <>
      <Light t={62 + t} opacity={0.27} />
      <Caption
        chapter="02 / LIBRARY & WORKFLOW"
        title={titles[phase]}
        detail={details[phase]}
        time={phase === 5 && s >= 5 ? s - 5 : s}
        serif={phase === 3}
      />
      <Stage>
        {(phase === 0 || phase === 2) && (
          <>
            {photos.map((id, i) => {
              const match = [
                  "IMG_5071",
                  "IMG_5053",
                  "IMG_5077",
                  "IMG_4788",
                ].includes(id),
                search = phase === 2;
              return (
                <div
                  key={id}
                  style={{
                    ...A,
                    left: (i % 4) * 248,
                    top: 35 + Math.floor(i / 4) * 230,
                    width: 232,
                    height: 210,
                    background: "#17202b",
                    borderRadius: 9,
                    overflow: "hidden",
                    boxShadow:
                      search && match
                        ? `0 0 0 2px ${lime}`
                        : "0 15px 40px #0005",
                    opacity:
                      search && !match
                        ? mix(1, 0.14, smooth(s, 1, 2.2))
                        : out(s, i * 0.035, 0.6 + i * 0.035),
                    transform: `translateY(${phase === 0 ? 80 * (1 - out(s, i * 0.035, 0.9 + i * 0.035)) : 0}px) rotate(${phase === 0 ? mix((i - 6) * 3, 0, out(s, 0, 1.2)) : 0}deg)`,
                  }}
                >
                  <Pic
                    src={`creative-polish-v2/${id}.jpg`}
                    style={{ height: 173 }}
                  />
                  <Label
                    style={{
                      ...A,
                      left: 12,
                      bottom: 11,
                      fontSize: 12,
                      color: search && match ? lime : "#c1c7d0",
                    }}
                  >
                    {id}.CR2
                  </Label>
                </div>
              );
            })}
            {phase === 2 && (
              <div
                style={{
                  ...A,
                  left: 75,
                  top: 740,
                  width: 830,
                  border: "1px solid #94a6bd88",
                  borderRadius: 40,
                  background: "#141e2e",
                  padding: "20px 28px",
                  fontFamily: font.sans,
                  color: paper,
                  fontSize: 27,
                  opacity: out(s, 0.2, 0.7),
                }}
              >
                <span style={{ color: lime, marginRight: 20 }}>⌕</span>
                {"mountains in warm light".slice(0, Math.floor(s * 16))}
                <span
                  style={{
                    float: "right",
                    fontFamily: font.mono,
                    fontSize: 12,
                    color: "#94a4b7",
                    paddingTop: 8,
                  }}
                >
                  VISUAL AI SEARCH
                </span>
              </div>
            )}
          </>
        )}
        {phase === 1 && (
          <>
            {["IMG_5053", "IMG_5071", "IMG_5077"].map((id, i) => (
              <div
                key={id}
                style={{
                  ...A,
                  left: i * 325,
                  top: 80 + (i === 1 ? -20 : 20),
                  width: 300,
                  height: 520,
                  borderRadius: 12,
                  background: "#15202d",
                  overflow: "hidden",
                  opacity: i === 1 ? 1 : mix(1, 0.3, smooth(s, 1.8, 3.2)),
                  transform: `translateY(${i !== 1 ? 80 * smooth(s, 2.3, 3.8) : 0}px) scale(${i === 1 ? mix(1, 1.08, smooth(s, 1.3, 2.7)) : 1})`,
                  boxShadow:
                    i === 1 && s > 1.8
                      ? `0 0 0 3px ${lime}`
                      : "0 20px 50px #0007",
                }}
              >
                <Pic
                  src={`creative-polish-v2/${id}.jpg`}
                  style={{ height: 440 }}
                />
                <Label
                  style={{
                    ...A,
                    left: 20,
                    top: 465,
                    fontSize: 17,
                    color: i === 1 ? lime : paper,
                  }}
                >
                  {s > 2
                    ? i === 1
                      ? "★★★★★ / PICK"
                      : "REVIEW NEXT"
                    : id + ".CR2"}
                </Label>
              </div>
            ))}
            <Pills
              items={["COMPARE", "RATE", "PICK", "CULL"]}
              style={{ ...A, left: 140, top: 735 }}
            />
          </>
        )}
        {phase === 3 && (
          <>
            {wedding.map((name, i) => (
              <div
                key={name}
                style={{
                  ...A,
                  left: [0, 600, 520][i],
                  top: [30, 150, 500][i],
                  width: [750, 330, 480][i],
                  height: [500, 500, 320][i],
                  border: "10px solid #eee8d8",
                  overflow: "hidden",
                  boxShadow: "0 30px 70px #0009",
                  transform: `translateY(${70 * (1 - out(s, i * 0.2, 0.9 + i * 0.2))}px) rotate(${[-4, 6, 3][i]}deg)`,
                  opacity: out(s, i * 0.15, 0.6 + i * 0.15),
                }}
              >
                <Pic src={`creative-polish-v2/${name}.jpg`} />
              </div>
            ))}
            <Type
              serif
              size={68}
              style={{ ...A, left: 10, top: 675, rotate: "-3deg" }}
            >
              A gallery worth keeping.
            </Type>
          </>
        )}
        {phase === 4 && (
          <>
            {wedding.map((name, i) => (
              <div
                key={name}
                style={{
                  ...A,
                  left: i * 326,
                  top: 60 + i * 20,
                  width: 305,
                  height: 600,
                  overflow: "hidden",
                  borderRadius: 9,
                  transform: `rotate(${[-3, 0, 3][i]}deg)`,
                  boxShadow: "0 25px 60px #0009",
                }}
              >
                <Pic src={`creative-polish-v2/${name}/batch-before.png`} />
                <Pic
                  src={`creative-polish-v2/${name}/batch-after.png`}
                  style={{
                    clipPath: `inset(0 ${100 * (1 - smooth(s, 1 + i * 0.17, 3.5 + i * 0.17))}% 0 0)`,
                  }}
                />
                <Label
                  style={{
                    ...A,
                    left: 0,
                    right: 0,
                    bottom: 0,
                    padding: 18,
                    background: "#0e1721d9",
                    fontSize: 13,
                  }}
                >
                  SHARED GRADE / EDITABLE
                </Label>
              </div>
            ))}
            <Pills
              items={["EXPOSURE +0.15", "WARMTH +6", "HIGHLIGHTS −20"]}
              style={{ ...A, left: 15, top: 735 }}
            />
          </>
        )}
      </Stage>
      <Footer
        left={
          phase === 2
            ? "FIND THE FRAME YOU REMEMBER."
            : "LESS SORTING. MORE CREATING."
        }
        right="LOOKSY / LIBRARY"
      />
    </>
  );
};
