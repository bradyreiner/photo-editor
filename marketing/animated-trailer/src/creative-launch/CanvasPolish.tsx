import { Pointer } from "./ProofTools";
import { staticFile } from "remotion";
import { CampaignPoster, CampaignThumbnail } from "./Canvas";
import { PanoramaArtwork } from "../desktop-launch/Panorama";
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
  Label,
  Pills,
  Type,
  Footer,
  font,
  lime,
  paper,
} from "./Kit";
import { canvasPhases, phaseAt, brushState } from "./State";

export const HikerPoster = ({
  type = 1,
  word = "FURTHER.",
  titleColor = paper,
  shadow = 0,
  cutoutVisible = 1,
  frameHeight = 1250,
  foregroundFront = 1,
  build = 1,
}: {
  type?: number;
  word?: string;
  titleColor?: string;
  shadow?: number;
  cutoutVisible?: number;
  frameHeight?: number;
  foregroundFront?: number;
  build?: number;
}) => (
  <div
    style={{
      ...A,
      width: 1000,
      height: frameHeight,
      overflow: "hidden",
      background: "#142537",
    }}
  >
    <Pic src="studio/subject-lift.png" style={{ opacity: build }} />
    <div
      style={{
        ...A,
        inset: 0,
        background:
          "linear-gradient(180deg,#061a2a50,transparent 50%,#071522b0)",
        opacity: build,
      }}
    />
    <div
      style={{
        ...A,
        left: 65,
        top: 50,
        opacity: build,
        right: 65,
        borderTop: "2px solid #edf0df99",
        paddingTop: 18,
        fontFamily: font.mono,
        fontSize: 18,
        color: paper,
        display: "flex",
        justifyContent: "space-between",
      }}
    >
      <span>FIELD NOTES / NEPAL</span>
      <span>27° N / 2026</span>
    </div>
    <div
      style={{
        ...A,
        left: 55,
        top: 220,
        color: paper,
        fontFamily: font.serif,
        fontStyle: "italic",
        fontSize: 130,
        transform: `translateY(${70 * (1 - type)}px)`,
        opacity: type,
      }}
    >
      A little
    </div>
    {foregroundFront <= 0.5 && (
      <Pic
        src="creative-polish-v2/subject-lift-alpha.png"
        style={{ opacity: cutoutVisible }}
      />
    )}
    <div
      style={{
        ...A,
        left: 42,
        top: 605,
        fontFamily: font.display,
        fontSize: 220,
        lineHeight: 1,
        color: titleColor,
        textShadow: `${5 * shadow}px ${12 * shadow}px ${16 * shadow}px rgba(0,0,0,${0.75 * shadow})`,
        letterSpacing: -4,
        transform: `translateX(${-300 * (1 - type)}px)`,
        opacity: type,
      }}
    >
      {word}
    </div>
    {foregroundFront > 0.5 && (
      <Pic
        src="creative-polish-v2/subject-lift-alpha.png"
        style={{ opacity: cutoutVisible }}
      />
    )}
    <div
      style={{
        ...A,
        left: 65,
        right: 65,
        bottom: 45,
        opacity: build,
        borderTop: "2px solid #edf0df88",
        paddingTop: 22,
        fontFamily: font.mono,
        color: paper,
        fontSize: 16,
        display: "flex",
        justifyContent: "space-between",
      }}
    >
      <span>GO FIND YOUR OWN WAY.</span>
      <span>↗ KEEP THE CURIOSITY</span>
    </div>
  </div>
);

const layers = [
  "Typography",
  "Subject cutout",
  "Color adjustment",
  "Original photograph",
];
export const EditableLayers = ({
  selected = 1,
  cutoutVisible = 1,
  reorder = 1,
  dragging = false,
}: {
  selected?: number;
  cutoutVisible?: number;
  reorder?: number;
  dragging?: boolean;
}) => (
  <div
    style={{
      width: 350,
      padding: 22,
      background: "#121c28f2",
      border: "1px solid #a5b6c54a",
      borderRadius: 12,
    }}
  >
    <Label style={{ fontSize: 16, marginBottom: 16 }}>
      LAYERS / {dragging ? "DRAG TO REORDER" : "EDITABLE"}
    </Label>
    <div style={{ position: "relative", height: 232 }}>
      {layers.map((name, i) => (
        <div
          key={name}
          style={{
            ...A,
            left: 0,
            right: 0,
            top: (i === 0 ? reorder : i === 1 ? 1 - reorder : i) * 58,
            height: 58,
            boxSizing: "border-box",
            padding: "15px 10px",
            borderRadius: 5,
            background: i === selected ? "#354b5c" : "#121c28",
            borderBottom: "1px solid #9dafc024",
            fontFamily: font.sans,
            fontSize: 21,
            color: paper,
            display: "flex",
            justifyContent: "space-between",
            zIndex: dragging && i === 0 ? 2 : 0,
            boxShadow: dragging && i === 0 ? "0 7px 16px #0007" : "none",
          }}
        >
          <span>
            {dragging && i === 0 ? "⋮⋮ " : ""}
            {name}
          </span>
          <span
            style={{
              color: i === selected ? lime : "#8c9ba8",
              opacity: i === 1 ? 0.3 + 0.7 * cutoutVisible : 1,
            }}
          >
            ◉
          </span>
        </div>
      ))}
    </div>
  </div>
);

const TextLayerDemo = ({ s }: { s: number }) => {
  const morph = smooth(s, 0.1, 1.1),
    word = "FURTHER.".slice(0, Math.max(0, Math.floor((s - 1.5) * 11))),
    color = smooth(s, 4, 4.7) * (1 - smooth(s, 6, 6.5)),
    shadow = smooth(s, 6.6, 7.6),
    layerMove = smooth(s, 8.7, 10),
    cutout = layerMove > 0.5 ? 1 : 0;
  return (
    <>
      <div
        style={{
          ...A,
          left: 210,
          top: -15,
          transform: `scale(${mix(0.53, 0.6, morph)}) rotate(${-3 * morph}deg)`,
          transformOrigin: "0 0",
        }}
      >
        <HikerPoster
          type={1}
          word={word}
          titleColor={color > 0.5 ? lime : paper}
          shadow={shadow}
          foregroundFront={cutout}
          frameHeight={mix(1500, 1250, morph)}
          build={smooth(s, 0.2, 1.2)}
        />
        {s > 1.2 && s < 7.8 && (
          <div
            style={{
              ...A,
              left: 35,
              top: 598,
              width: 900,
              height: 230,
              border: "2px solid #edf0df99",
              opacity: 1 - smooth(s, 7.4, 7.8),
            }}
          >
            {[0, 1, 2, 3].map((i) => (
              <div
                key={i}
                style={{
                  ...A,
                  left: i % 2 ? "100%" : -5,
                  top: i > 1 ? "100%" : -5,
                  width: 10,
                  height: 10,
                  background: paper,
                }}
              />
            ))}
          </div>
        )}
      </div>
      {s < 7.8 ? (
        <div
          style={{
            ...A,
            left: 620,
            top: 200,
            width: 340,
            padding: 25,
            borderRadius: 12,
            background: "#111c29f5",
            border: "1px solid #a9bbc977",
            opacity: out(s, 1, 1.5) * (1 - smooth(s, 7.4, 7.8)),
            boxShadow: "0 20px 70px #0008",
          }}
        >
          <Label style={{ fontSize: 15 }}>TEXT / FURTHER.</Label>
          <div
            style={{
              fontFamily: font.sans,
              fontSize: 22,
              color: paper,
              marginTop: 25,
            }}
          >
            Font <span style={{ float: "right" }}>Anton</span>
          </div>
          <div
            style={{
              fontFamily: font.sans,
              fontSize: 22,
              color: paper,
              marginTop: 25,
            }}
          >
            Size <span style={{ float: "right" }}>220 px</span>
          </div>
          <div
            style={{
              fontFamily: font.sans,
              fontSize: 22,
              color: paper,
              marginTop: 25,
              display: "flex",
              alignItems: "center",
              justifyContent: "space-between",
            }}
          >
            Fill{" "}
            <span
              style={{
                display: "inline-block",
                width: 60,
                height: 30,
                background: color > 0.5 ? lime : paper,
                borderRadius: 4,
              }}
            />
          </div>
          <div
            style={{
              fontFamily: font.sans,
              fontSize: 22,
              color: paper,
              marginTop: 25,
            }}
          >
            Shadow{" "}
            <span style={{ float: "right", color: lime }}>
              {Math.round(shadow * 100)}%
            </span>
          </div>
          <div
            style={{
              height: 3,
              background: "#42576a",
              marginTop: 20,
              position: "relative",
            }}
          >
            <div
              style={{ width: shadow * 100 + "%", height: 3, background: lime }}
            />
            <div
              style={{
                ...A,
                left: shadow * 100 + "%",
                top: -6,
                width: 15,
                height: 15,
                borderRadius: 15,
                background: paper,
              }}
            />
          </div>
          <Pointer
            x={
              s < 6.6 ? mix(330, 25, smooth(s, 5.6, 6.3)) : mix(25, 365, shadow)
            }
            y={mix(186, 272, smooth(s, 5.6, 6.3))}
            opacity={smooth(s, 3.5, 3.8) * (1 - smooth(s, 7.5, 7.8))}
          />
        </div>
      ) : (
        <div
          style={{
            ...A,
            left: 620,
            top: 250,
            opacity: out(s, 7.8, 8.25),
            transform: `translateY(${18 * (1 - out(s, 7.8, 8.25))}px)`,
          }}
        >
          <EditableLayers selected={0} reorder={layerMove} dragging />
          <Pointer
            x={140}
            y={86 + 58 * layerMove}
            press={smooth(s, 8.5, 8.7) * (1 - smooth(s, 10, 10.2))}
          />
        </div>
      )}
      <Label style={{ ...A, left: 20, top: 780, fontSize: 17 }}>
        {s < 3.5
          ? "WRITE IT. STYLE IT."
          : s < 7.8
            ? "FILL. SHADOW. LIVE TYPE."
            : cutout < 0.5
              ? "DRAG TYPE BELOW THE SUBJECT"
              : "TYPE BELOW SUBJECT / TEXT BEHIND"}
      </Label>
    </>
  );
};

const InpaintProof = ({ s }: { s: number }) => {
  const draw = smooth(s, 0.9, 2.7),
    fill = smooth(s, 4.1, 4.9),
    compare = smooth(s, 6, 6.7) * (1 - smooth(s, 8.3, 8.9)),
    zoom = smooth(s, 8.5, 9.7),
    radius = 26;
  return (
    <>
      <div
        style={{
          ...A,
          left: 20,
          top: 10,
          width: 950,
          height: 750,
          overflow: "hidden",
          borderRadius: 15,
          boxShadow: "0 25px 70px #0008",
        }}
      >
        <div
          style={{
            ...A,
            left: 475 - 978 * 0.93,
            top: 375 - 1532 * 0.93,
            width: 1800 * 0.93,
            height: 2700 * 0.93,
            transform: `scale(${mix(1, 0.7, zoom)})`,
            transformOrigin: `${978 * 0.93}px ${1532 * 0.93}px`,
          }}
        >
          <Pic src="refine/surf.jpg" />
          <Pic src="refine/surf-final.png" style={{ opacity: fill }} />
          <Pic src="refine/surf.jpg" style={{ opacity: compare }} />
          <div
            style={{
              ...A,
              inset: 0,
              background: "#c7a2ff",
              maskImage: `url(${staticFile("media/refine/surf-mask.png")})`,
              maskSize: "100% 100%",
              maskMode: "luminance",
              clipPath: `inset(0 0 ${100 * (1 - (1396 + 272 * draw) / 2700)}% 0)`,
              opacity: 0.68 * draw * (1 - smooth(s, 4, 4.8)),
            }}
          />
          {s > 0.8 && s < 3.2 && (
            <div
              style={{
                ...A,
                left: 978 * 0.93 - radius,
                top: (1396 + 272 * draw) * 0.93 - radius,
                width: radius * 2,
                height: radius * 2,
                border: "2px solid white",
                borderRadius: 100,
                boxShadow: "0 0 0 1px #132033",
              }}
            />
          )}
        </div>
        <div
          style={{
            ...A,
            left: 20,
            top: 20,
            padding: "10px 14px",
            background: "#111b29ec",
            opacity: compare,
          }}
        >
          <Label style={{ fontSize: 16, color: paper }}>
            BEFORE / ORIGINAL RESTORED
          </Label>
        </div>
        <div
          style={{
            ...A,
            right: 20,
            top: 20,
            padding: "10px 14px",
            background: "#111b29ec",
            opacity: fill * (1 - compare),
          }}
        >
          <Label style={{ fontSize: 16 }}>AFTER / OBJECT REMOVED</Label>
        </div>
        <div
          style={{
            ...A,
            left: 25,
            bottom: 25,
            padding: "14px 20px",
            borderRadius: 8,
            background:
              s < 3.4 ? "#132332e8" : s < 4.8 ? "#543568e8" : "#152a21e8",
            border: "1px solid #c6d4e744",
          }}
        >
          <Label style={{ fontSize: 18 }}>
            {s < 0.9
              ? "1 / CHOOSE THE DISTRACTION"
              : s < 3.4
                ? "2 / PAINT THE SELECTION"
                : s < 4.8
                  ? "3 / INPAINT SELECTION"
                  : compare > 0.5
                    ? "COMPARE ORIGINAL / HOLD"
                    : "LOCAL FILL / OUTSIDE PIXELS RETAINED"}
          </Label>
        </div>
        {s > 2.8 && s < 4.3 && (
          <Pointer
            x={315}
            y={690}
            press={smooth(s, 3.3, 3.4) * (1 - smooth(s, 3.5, 3.7))}
          />
        )}
      </div>
      <Label style={{ ...A, left: 20, top: 790, fontSize: 15 }}>
        SELECT → INPAINT → COMPARE. KEEP THE ORIGINAL.
      </Label>
    </>
  );
};

export const CanvasChapter = () => {
  const { t } = useShot(),
    { phase, s } = phaseAt(t, canvasPhases),
    lift = smooth(s, 0.8, 3.2),
    bypass = smooth(s, 4.1, 4.35) * (1 - smooth(s, 5.15, 5.4)),
    brush = brushState(s);
  const copy = [
    [
      "Go beyond\nthe whole frame.",
      "Separate the subject so you can refine the light or build a new composition.",
    ],
    [
      "Your subject.\nIts own light.",
      "Lift the subject. Bypass the adjustment to compare. Keep the background exactly where it was.",
    ],
    [
      "Keep the subject.\nChange the canvas.",
      "A real transparent cutout. Ready to join a new composition.",
    ],
    [
      "Make it\na poster.",
      "Turn the same photograph into a poster. Keep the type, subject and adjustments editable.",
    ],
    [
      "Leave out\nthe distraction.",
      "Select what goes. Inpaint the selected region. Compare the clean result with the original.",
    ],
    [
      "Build the campaign.\nKeep the pieces.",
      "Build a product campaign with cutout artwork, brush texture and bold typography.",
    ],
    [
      "Every element.\nYour decision.",
      "Change the type. Adjust a layer. Keep the whole composition editable.",
    ],
    [
      "A poster today.\nA thumbnail next.",
      "Recompose the campaign as a thumbnail or a graphic for your website.",
    ],
  ];
  const focus =
    phase === 3
      ? 0
      : phase === 5
        ? smooth(s, 5.2, 6.4) * (1 - smooth(s, 7.6, 8.8))
        : phase === 7
          ? smooth(s, 1.4, 2.8) * (1 - smooth(s, 8.5, 9.8))
          : 0;
  const format =
    phase === 6
      ? s < 2.5
        ? "serif"
        : s < 4.6
          ? "sans"
          : "display"
      : "display";
  const product =
    phase === 6
      ? 1 - 0.38 * smooth(s, 5.1, 6.1) * (1 - smooth(s, 6.4, 7.5))
      : 1;
  return (
    <>
      <Light t={92 + t} opacity={phase > 4 ? 0.24 : 0.32} />
      <Caption
        chapter="03 / CANVAS & COMPOSITING"
        title={copy[phase][0]}
        detail={copy[phase][1]}
        time={s}
        serif={phase === 3}
        focus={focus}
      />
      <Stage focus={focus}>
        {phase < 3 && (
          <>
            <div
              style={{
                ...A,
                left: 210,
                top: -15,
                width: 530,
                height: 795,
                overflow: "hidden",
                borderRadius: 12,
                background:
                  "repeating-conic-gradient(#16212b 0% 25%,#263644 0% 50%) 50% / 40px 40px",
                boxShadow: "0 30px 90px #0008",
                transform:
                  phase === 2
                    ? `scale(${1 + 0.35 * smooth(s, 0.4, 2) * (1 - smooth(s, 3.3, 4.8))})`
                    : phase === 1
                      ? `scale(${1 + 0.22 * smooth(s, 0.2, 1.3) * (1 - smooth(s, 6.3, 7.8))})`
                      : "none",
                transformOrigin: "49% 64%",
              }}
            >
              <Pic
                src={
                  phase === 2
                    ? "studio/subject-lift.png"
                    : "studio/color/4-8.png"
                }
                style={{ opacity: phase === 2 ? 1 - smooth(s, 0.8, 2) : 1 }}
              />
              {phase === 1 && (
                <Pic
                  src="studio/subject-lift.png"
                  style={{ opacity: lift * (1 - bypass) }}
                />
              )}
              <div
                style={{
                  ...A,
                  inset: 0,
                  background: "#bcffb4",
                  maskImage: `url(${staticFile("media/studio/subject.png")})`,
                  maskSize: "100% 100%",
                  maskMode: "alpha",
                  opacity:
                    phase === 0
                      ? 0.42 * smooth(s, 0.7, 2) * (1 - smooth(s, 5.5, 6))
                      : 0,
                  mixBlendMode: "screen",
                }}
              />
              {phase === 2 && (
                <Pic src="creative-polish-v2/subject-lift-alpha.png" />
              )}
            </div>
            {phase === 0 && s > 1.3 && (
              <div
                style={{
                  ...A,
                  left: 760,
                  top: 50,
                  width: 200,
                  opacity: out(s, 1.3, 1.8) * (1 - smooth(s, 5.5, 6)),
                }}
              >
                <Label style={{ fontSize: 15, marginBottom: 15 }}>
                  ACTUAL MASK
                </Label>
                <div
                  style={{
                    position: "relative",
                    width: 180,
                    height: 400,
                    overflow: "hidden",
                    background: "#05090d",
                    border: "1px solid #a8bed055",
                    borderRadius: 9,
                  }}
                >
                  <Pic
                    src="studio/subject.png"
                    style={{
                      left: -150,
                      top: -250,
                      width: 480,
                      height: 720,
                      objectFit: "fill",
                      filter: "brightness(0) invert(1)",
                    }}
                  />
                </div>
                <Label style={{ fontSize: 12, marginTop: 15 }}>
                  WHITE = SELECTED
                </Label>
              </div>
            )}
            {phase === 1 && (
              <div
                style={{
                  ...A,
                  left: 620,
                  top: 225,
                  width: 365,
                  opacity: out(s, 0, 0.45) * (1 - smooth(s, 7.65, 8)),
                  transform: `translateY(${18 * (1 - out(s, 0, 0.45))}px)`,
                  padding: 22,
                  background: "#111b27f5",
                  border: "1px solid #c5d0d34d",
                  borderRadius: 12,
                }}
              >
                <Label style={{ fontSize: 16 }}>
                  SUBJECT / MASKED EXPOSURE
                </Label>
                <div
                  style={{
                    fontFamily: font.sans,
                    fontSize: 29,
                    color: paper,
                    marginTop: 24,
                  }}
                >
                  Exposure{" "}
                  <span style={{ float: "right", color: lime }}>
                    +{(0.55 * lift).toFixed(2)} EV
                  </span>
                </div>
                <div
                  style={{
                    position: "relative",
                    height: 4,
                    background: "#4d6378",
                    marginTop: 25,
                  }}
                >
                  <div
                    style={{
                      width: `${50 + 13.75 * lift}%`,
                      height: 4,
                      background: lime,
                    }}
                  />
                  <div
                    style={{
                      ...A,
                      left: `${50 + 13.75 * lift}%`,
                      top: -7,
                      width: 18,
                      height: 18,
                      borderRadius: 20,
                      background: paper,
                    }}
                  />
                </div>
                <div
                  style={{
                    marginTop: 25,
                    padding: "14px 18px",
                    background: bypass > 0.5 ? "#3c4b5a" : "#263c20",
                    border: "1px solid #9cb78388",
                    borderRadius: 7,
                    fontFamily: font.mono,
                    fontSize: 15,
                    color: paper,
                  }}
                >
                  {bypass > 0.5 ? "BYPASS ON" : "BYPASS OFF / EDIT ACTIVE"}
                </div>
                <Pointer
                  x={160}
                  y={170}
                  opacity={smooth(s, 3.7, 4) * (1 - smooth(s, 5.5, 5.8))}
                />
                <div
                  style={{
                    fontFamily: font.mono,
                    fontSize: 12,
                    color: "#a5b7c3",
                    marginTop: 25,
                  }}
                >
                  {bypass > 0.5
                    ? "BYPASSED / COMPARE THE SUBJECT"
                    : "ACTIVE / BACKGROUND UNCHANGED"}
                </div>
              </div>
            )}
            {phase === 2 && (
              <Pills
                items={["TRANSPARENT BACKGROUND", "SOURCE PIXELS RETAINED"]}
                style={{ ...A, left: 140, top: 780, opacity: out(s, 1.4, 2) }}
              />
            )}
          </>
        )}
        {phase === 3 && <TextLayerDemo s={s} />}
        {phase === 4 && <InpaintProof s={s} />}
        {phase === 5 && (
          <>
            <div
              style={{
                ...A,
                left: 210,
                top: -10,
                transform: `perspective(1800px) rotateY(${mix(12, 0, out(s, 0, 1.6))}deg) rotateZ(-3deg) scale(.6)`,
                transformOrigin: "0 0",
              }}
            >
              <CampaignPoster build={out(s, 0.1, 1.4)} brush={brush.u} />
              {s > 1.2 && s < 4.8 && (
                <svg
                  style={{ ...A, inset: 0, width: 1000, height: 1250 }}
                  viewBox="0 0 1000 1250"
                >
                  <circle
                    cx={brush.x}
                    cy={brush.y}
                    r={brush.size / 2}
                    fill="none"
                    stroke={paper}
                    strokeWidth="3"
                  />
                  <circle cx={brush.x} cy={brush.y} r="5" fill={paper} />
                </svg>
              )}
            </div>
            <div
              style={{
                ...A,
                left: 25,
                top: 660,
                opacity: out(s, 1.1, 1.6) * (1 - smooth(s, 4.9, 5.5)),
              }}
            >
              <Pills items={["BRUSH / TEXTURED", "SIZE 180", "PAINT LAYER"]} />
            </div>
          </>
        )}
        {phase === 6 && (
          <>
            <div
              style={{
                ...A,
                left: 180,
                top: -10,
                transform: "scale(.6) rotate(-3deg)",
                transformOrigin: "0 0",
              }}
            >
              <CampaignPoster
                typeface={format}
                tracking={format === "display" ? -7 : 0}
                brushOpacity={product}
              />
            </div>
            <div
              style={{
                ...A,
                left: 605,
                top: 255,
                width: 370,
                padding: 22,
                background: "#111b28f5",
                border: "1px solid #aebdca55",
                borderRadius: 12,
              }}
            >
              <Label style={{ fontSize: 16 }}>
                {s < 5 ? "SELECTED TYPE / STAY" : "LAYERS / PAINT"}
              </Label>
              {(s < 5
                ? ["serif", "sans", "display"]
                : ["Typography", "Product cutout", "Paint layer"]
              ).map((name) => (
                <div
                  key={name}
                  style={{
                    fontFamily: font[name as keyof typeof font] || font.sans,
                    fontSize: s < 5 ? 34 : 25,
                    color: (s < 5 ? name === format : name === "Paint layer")
                      ? lime
                      : paper,
                    padding: "14px 5px",
                    borderBottom: "1px solid #c0cbd32b",
                    background: (
                      s < 5 ? name === format : name === "Paint layer"
                    )
                      ? "#334856"
                      : "transparent",
                  }}
                >
                  {s < 5 ? "STAY" : name}
                </div>
              ))}
              <div
                style={{
                  fontFamily: font.mono,
                  fontSize: 13,
                  color: "#b8c8d1",
                  marginTop: 25,
                }}
              >
                PAINT OPACITY / {Math.round(product * 100)}%
              </div>
            </div>
          </>
        )}
        {phase === 7 && (
          <>
            <div
              style={{
                ...A,
                left: 0,
                top: 80,
                width: 1000,
                height: 562,
                transform: `perspective(1400px) rotateY(${mix(-10, 0, out(s, 0, 1.4))}deg)`,
                boxShadow: "0 35px 90px #0008",
              }}
            >
              <CampaignThumbnail build={out(s, 0.1, 1.9)} />
            </div>
            <Pills
              items={[
                "16:9 / THUMBNAIL",
                "WEBSITE GRAPHIC",
                "EDITABLE TYPE & LAYERS",
              ]}
              style={{
                ...A,
                left: 20,
                top: 720,
                opacity: out(s, 1.3, 2) * (1 - focus),
              }}
            />
          </>
        )}
      </Stage>
      <Footer
        left={
          phase > 4
            ? "POSTERS. THUMBNAILS. YOUR NEXT IDEA."
            : "MASKS. CUTOUTS. TEXT. RETAINED CONTROL."
        }
        right="LOOKSY / CANVAS"
      />
    </>
  );
};

export const CarouselChapter = () => {
  const { t, portrait, width } = useShot(),
    split = smooth(t, 6, 9),
    pan = mix(-110, 110, smooth(t, 9.5, 14.5)),
    sc = portrait ? 0.27 : 0.36,
    pw = 1080 * sc,
    ph = 1350 * sc,
    gap = 36 * split;
  return (
    <>
      <Light t={160 + t} opacity={0.3} />
      <div
        style={{
          ...A,
          left: portrait ? 70 : 82,
          top: portrait ? 160 : 75,
          width: portrait ? 940 : 1700,
        }}
      >
        <Label>04 / CAROUSEL</Label>
        <Type size={portrait ? 105 : 100} style={{ marginTop: 25 }}>
          {t < 6
            ? "Some ideas need more room."
            : t < 16
              ? "Move one photo. Across every canvas."
              : "One story. Ready to swipe."}
        </Type>
        <div
          style={{
            fontFamily: font.sans,
            fontSize: portrait ? 36 : 32,
            color: "#bcc9d0",
            marginTop: 22,
          }}
        >
          A shared image and typography across three social canvases.
        </div>
      </div>
      <div
        style={{
          ...A,
          left: (width - (3 * pw + 2 * gap)) / 2,
          top: portrait ? 750 : 370,
          perspective: 1800,
        }}
      >
        {[0, 1, 2].map((i) => (
          <div
            key={i}
            style={{
              ...A,
              left: i * (pw + gap),
              top: i === 1 ? -8 * split : 8 * split,
              width: pw,
              height: ph,
              overflow: "hidden",
              borderRadius: 8 * split,
              boxShadow: split ? "0 30px 60px #0008" : "none",
              transform: `rotateY(${(i - 1) * -6 * split}deg) rotateZ(${(i - 1) * 1.2 * split}deg)`,
            }}
          >
            <div
              style={{
                ...A,
                left: -i * pw,
                top: 0,
                width: 3240,
                height: 1350,
                transform: `scale(${sc})`,
                transformOrigin: "0 0",
              }}
            >
              <PanoramaArtwork imagePan={pan} imageZoom={1.12} />
            </div>
          </div>
        ))}
      </div>
      <div
        style={{ ...A, left: portrait ? 90 : 420, top: portrait ? 1330 : 915 }}
      >
        <Pills
          items={
            t < 16
              ? [
                  "ONE SHARED PHOTO",
                  `POSITION X ${Math.round(pan)}`,
                  "LINKED COMPOSITION",
                ]
              : ["01.PNG", "02.PNG", "03.PNG", "3 × 1080 × 1350"]
          }
        />
      </div>
      {t > 16 && (
        <div
          style={{
            ...A,
            left: portrait ? 95 : 82,
            top: portrait ? 1460 : 860,
            opacity: out(t, 16, 17),
            fontFamily: font.mono,
            fontSize: 24,
            color: paper,
          }}
        >
          01 ━━━ 02 ━━━ 03{" "}
          <span
            style={{
              display: "inline-block",
              translate: `${20 * smooth(t, 17, 20)}px 0`,
            }}
          >
            →
          </span>
        </div>
      )}
      <Footer
        left="A CONTINUOUS STORY, ACROSS THE SEAMS."
        right="LOOKSY / CAROUSEL"
      />
    </>
  );
};
