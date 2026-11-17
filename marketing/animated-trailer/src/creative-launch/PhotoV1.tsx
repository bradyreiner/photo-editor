import { NativeResponse } from "../rebuild/StoryboardShared";
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
  Native,
  At,
  Slider,
  Label,
  Pills,
  Type,
  Footer,
  PaperGrain,
  font,
  lime,
  paper,
} from "./Kit";

const photoCopy = [
  [
    "Start with the\ncamera original.",
    "RAW photo editing, built into a complete desktop studio.",
  ],
  [
    "Find your\nexposure.",
    "Recover the frame. Shape the light. Keep the original.",
  ],
  [
    "More room in\nevery shadow.",
    "Highlights and shadows, with precise control over the finish.",
  ],
  [
    "Color, from\nthe ground up.",
    "A bespoke color engine. White balance, tone and color working together.",
  ],
  [
    "Shape the\nwhole image.",
    "Master and RGB tone curves. Make the contrast your own.",
  ],
  [
    "Make the blue\nyour blue.",
    "Hue, saturation and luminance. Refine one color at a time.",
  ],
  [
    "Right down\nto the pixels.",
    "Native-resolution detail. An sRGB workflow from edit to delivery.",
  ],
  [
    "The frame\nyou had in mind.",
    "From your RAW original to a finished photograph.",
  ],
];
const Curve = ({ u }: { u: number }) => (
  <svg viewBox="0 0 340 280" style={{ width: 340, height: 280 }}>
    <rect width="340" height="280" rx="12" fill="#10171de8" />
    {[1, 2, 3].map((n) => (
      <g key={n} stroke="#b8cbd12c">
        <path d={`M${n * 85} 0V280`} />
        <path d={`M0 ${n * 70}H340`} />
      </g>
    ))}
    <path d="M20 260L320 20" stroke="#ffffff32" fill="none" />
    <path
      d={`M20 260C95 ${mix(190, 250, u)} 203 ${mix(105, -18, u)} 320 20`}
      stroke={lime}
      strokeWidth="4"
      fill="none"
    />
    <circle cx={mix(110, 105, u)} cy={mix(175, 204, u)} r="7" fill={paper} />
    <circle cx={mix(230, 230, u)} cy={mix(90, 45, u)} r="7" fill={paper} />
  </svg>
);
export const PhotoChapter = () => {
  const { t, portrait } = useShot();
  const beat = Math.min(7, Math.floor(t / 4)),
    s = t - beat * 4,
    u = smooth(s, 0.7, 3.2);
  const stage = [0, 0, 1, 2, 3, 4, 4, 4][beat];
  const amount = beat === 0 ? 0 : beat >= 6 ? 1 : u;
  const native =
    beat === 1
      ? { src: "desktop-launch/raw-native.mp4", trim: 18 }
      : beat === 4
        ? { src: "desktop-action/tone-curve.mp4", trim: 12 }
        : beat === 5
          ? { src: "desktop-action/color-edit.mp4", trim: 0 }
          : null;
  const nativeAlpha = native ? (1 - smooth(s, 1.7, 2.6)) * out(s, 0, 0.18) : 0;
  const focus = beat === 7 ? smooth(s, 0.3, 1.5) : 0;
  return (
    <>
      <Light t={t + 12} opacity={0.34} />
      <Caption
        chapter="01 / PHOTO EDITOR"
        title={photoCopy[beat][0]}
        detail={photoCopy[beat][1]}
        time={s}
        focus={focus}
        serif={beat === 7}
      />
      <Stage focus={focus}>
        <Surface
          style={{
            top: 40,
            height: 667,
            transform: `perspective(1700px) rotateY(${mix(-5, 0, out(t, 0, 1.5))}deg) scale(${beat === 0 ? mix(0.86, 1, out(s, 0, 1.1)) : 1})`,
            boxShadow: "0 35px 100px #0009",
          }}
        >
          <NativeResponse stage={stage} u={amount} family="raw-colour" />
          {beat === 0 && (
            <div
              style={{
                ...A,
                inset: 0,
                background:
                  "repeating-linear-gradient(90deg,transparent 0px,transparent 165px,#0b0d12 166px,#0b0d12 174px)",
                opacity: 1 - smooth(s, 0.2, 1.4),
              }}
            />
          )}
          {beat === 6 && (
            <div
              style={{
                ...A,
                left: 340,
                top: 85,
                width: 540,
                height: 540,
                overflow: "hidden",
                border: "2px solid #f1efdf",
                borderRadius: 270,
                boxShadow: "0 20px 60px #0008",
                transform: `scale(${out(s, 0.25, 1.3)})`,
              }}
            >
              <Pic src="studio/raw-colour/native-detail-lift.png" />
              <div
                style={{
                  ...A,
                  inset: 0,
                  background:
                    "repeating-linear-gradient(0deg,transparent 0px,transparent 23px,#ffffff18 24px),repeating-linear-gradient(90deg,transparent 0px,transparent 23px,#ffffff18 24px)",
                  opacity: smooth(s, 1.1, 2.2) * 0.6,
                }}
              />
            </div>
          )}
          {beat === 7 && (
            <div
              style={{
                ...A,
                inset: 0,
                clipPath: `inset(0 ${100 * smooth(s, 0.4, 2.4)}% 0 0)`,
              }}
            >
              <Pic src="studio/raw-colour/0-0.png" />
            </div>
          )}
          {beat === 7 && (
            <div
              style={{
                ...A,
                left: `${(1 - smooth(s, 0.4, 2.4)) * 100}%`,
                top: 0,
                bottom: 0,
                width: 2,
                background: paper,
                opacity: 1 - smooth(s, 2.35, 2.65),
              }}
            />
          )}
          {beat === 4 && (
            <div
              style={{
                ...A,
                right: 25,
                bottom: 30,
                transform: `translateY(${30 * (1 - out(s, 1.8, 2.4))}px)`,
                opacity: smooth(s, 1.8, 2.4),
              }}
            >
              <Curve u={u} />
            </div>
          )}
        </Surface>
        <Label
          style={{ ...A, left: 0, top: 0, fontSize: 18, opacity: 1 - focus }}
        >
          IMG_4969.CR2 <span style={{ color: "#a1aab5" }}> / 6024 × 4020</span>
        </Label>
        {beat === 1 && (
          <div
            style={{
              ...A,
              left: 110,
              top: 710,
              background: "#121921",
              padding: 20,
              borderRadius: 12,
              transform: `translateY(${30 * (1 - out(s, 1.7, 2.3))}px)`,
              opacity: smooth(s, 1.7, 2.3),
            }}
          >
            <Slider
              label="Exposure"
              value={1.4 * u}
              text={`+${(1.4 * u).toFixed(2)} EV`}
            />
          </div>
        )}
        {beat === 2 && (
          <div style={{ ...A, left: 85, top: 710, display: "flex", gap: 40 }}>
            <Pills
              items={[
                `HIGHLIGHTS −${Math.round(35 * u)}`,
                `SHADOWS +${Math.round(35 * u)}`,
              ]}
            />
          </div>
        )}
        {beat === 3 && (
          <div
            style={{
              ...A,
              left: 0,
              top: 730,
              display: "flex",
              gap: 22,
              alignItems: "center",
            }}
          >
            {["CAMERA RAW", "LINEAR LIGHT", "LOOKSY COLOR", "sRGB"].map(
              (x, i) => (
                <div
                  key={x}
                  style={{
                    display: "flex",
                    alignItems: "center",
                    gap: 20,
                    opacity: out(s, i * 0.3, i * 0.3 + 0.6),
                  }}
                >
                  <Label
                    style={{ fontSize: 16, color: i === 2 ? lime : paper }}
                  >
                    {x}
                  </Label>
                  {i < 3 && <span style={{ color: lime }}>→</span>}
                </div>
              ),
            )}
          </div>
        )}
        {beat === 5 && (
          <div style={{ ...A, left: 90, top: 710, display: "flex", gap: 12 }}>
            {[
              "#fb775c",
              "#e6c55a",
              "#9fca54",
              "#57c7ca",
              "#528cff",
              "#c17dfa",
            ].map((c, i) => (
              <div
                key={c}
                style={{
                  width: 80,
                  height: 44,
                  borderRadius: 30,
                  background: c,
                  transform: `translateY(${-16 * Math.sin(u * Math.PI) * (i === 4 ? 1 : 0)}px)`,
                  border: i === 4 ? "3px solid white" : "3px solid transparent",
                }}
              />
            ))}
          </div>
        )}
        {native && nativeAlpha > 0.001 && (
          <At name="Actual photo editor controls" from={beat * 4} seconds={2.6}>
            <Native
              src={native.src}
              video
              trim={native.trim}
              style={{
                top: 70,
                opacity: nativeAlpha,
                transform: `scale(${mix(0.97, 1, out(s, 0, 0.6))})`,
              }}
            />
          </At>
        )}
        {beat === 7 && (
          <Pills
            items={["ORIGINAL", "FINISHED / NATIVE ENGINE"]}
            style={{ ...A, left: 0, top: 730, opacity: 1 - focus }}
          />
        )}
      </Stage>
      <Footer
        left="YOUR ORIGINAL. YOUR DECISIONS."
        right={
          portrait
            ? "LOOKSY / PHOTO"
            : "CUSTOM COLOR ENGINE · NATIVE RESOLUTION"
        }
      />
    </>
  );
};

const photos = [
  "IMG_4969.jpg",
  "IMG_5106-developed.jpg",
  "IMG_5071.jpg",
  "IMG_4744.jpg",
  "IMG_5103.jpg",
  "IMG_4856.jpg",
  "IMG_5111.jpg",
  "IMG_5124.jpg",
  "IMG_4870.jpg",
  "IMG_5125.jpg",
  "IMG_5053.jpg",
  "IMG_5077.jpg",
];
export const LibraryChapter = () => {
  const { t } = useShot();
  const beat = t < 5 ? 0 : t < 9 ? 1 : t < 14 ? 2 : t < 18 ? 3 : 4,
    s = t - [0, 5, 9, 14, 18][beat];
  const titles = [
    "Bring the\nwhole shoot.",
    "Keep the\nkeepers.",
    "Remember the scene.\nFind the frame.",
    "One day.\nA whole story.",
    "A consistent\nfinish, faster.",
  ];
  const details = [
    "Batch import your photo and video library. Start with everything in one place.",
    "Cull, rate and select before the detailed edit.",
    "Visual AI search helps you find photographs by what is in them.",
    "Wedding galleries, travel stories, client work. Make a considered selection.",
    "Batch adjustments across a selection. Refine individual photographs when they need it.",
  ];
  return (
    <>
      <Light t={t + 44} opacity={0.25} />
      <Caption
        chapter="02 / LIBRARY & WORKFLOW"
        title={titles[beat]}
        detail={details[beat]}
        serif={beat === 3}
        time={s}
        focus={beat === 3 ? smooth(s, 1.1, 2.4) : 0}
      />
      <Stage focus={beat === 3 ? 0.65 * smooth(s, 1.1, 2.4) : 0}>
        {beat < 3 && (
          <div style={{ ...A, inset: 0, perspective: 1800 }}>
            {photos.map((src, i) => {
              const selected = (beat === 2 ? [2, 4, 9] : [0, 2, 4, 9]).includes(
                  i,
                ),
                isSearch = beat === 2;
              const x = (i % 4) * 248,
                y = Math.floor(i / 4) * 228;
              return (
                <div
                  key={src}
                  style={{
                    ...A,
                    left: x,
                    top: y + 45,
                    width: 232,
                    height: 205,
                    overflow: "hidden",
                    borderRadius: 10,
                    background: "#121821",
                    transform: `translate(${beat === 0 ? (1 - out(s, i * 0.045, 1 + i * 0.045)) * (i % 2 ? -550 : 550) : 0}px,${beat === 0 ? 150 * (1 - out(s, i * 0.045, 1.2 + i * 0.045)) : 0}px) rotate(${beat === 0 ? mix((i - 6) * 5, 0, out(s, 0, 1.6)) : 0}deg) scale(${beat === 1 ? (selected ? 1 : 0.92) : 1})`,
                    opacity:
                      isSearch && !selected
                        ? mix(1, 0.14, smooth(s, 0.5, 1.6))
                        : 1,
                    boxShadow:
                      selected && beat > 0
                        ? `0 0 0 3px ${lime}`
                        : "0 15px 25px #0004",
                  }}
                >
                  <Pic src={src} style={{ height: 165 }} />
                  <Label
                    style={{
                      ...A,
                      bottom: 12,
                      left: 12,
                      fontSize: 13,
                      color: selected && beat > 0 ? lime : "#b2bac3",
                    }}
                  >
                    {beat > 0 && selected
                      ? "★★★★★  /  PICK"
                      : "IMG_" + String(4969 + i * 13) + "  ·  ORIGINAL"}
                  </Label>
                </div>
              );
            })}
          </div>
        )}
        {beat === 1 && s < 2.3 && (
          <At name="Actual culling interaction" from={5} seconds={2.3}>
            <Native
              src="desktop-launch/cull-native.mp4"
              video
              trim={0}
              style={{ opacity: 1 - smooth(s, 1.6, 2.3) }}
            />
          </At>
        )}
        {beat === 2 && (
          <div
            style={{
              ...A,
              left: 90,
              top: 730,
              width: 800,
              background: "#161e2b",
              padding: 25,
              borderRadius: 50,
              border: "1px solid #94a9b255",
              fontFamily: font.sans,
              color: paper,
              fontSize: 29,
              opacity: out(s, 0.3, 0.9),
            }}
          >
            <span style={{ color: lime, paddingRight: 20 }}>⌕</span> mountains
            and hikers{" "}
            <span
              style={{
                float: "right",
                fontFamily: font.mono,
                fontSize: 13,
                color: "#919aa5",
                paddingTop: 10,
              }}
            >
              VISUAL SEARCH
            </span>
          </div>
        )}
        {beat === 3 && (
          <div style={{ ...A, inset: 0 }}>
            {[
              "campaign/wedding-couple-developed.jpg",
              "campaign/wedding-dawn-developed.jpg",
              "campaign/wedding-lace-detail.jpg",
            ].map((src, i) => (
              <div
                key={src}
                style={{
                  ...A,
                  left: [0, 490, 750][i],
                  top: [40, 380, 140][i],
                  width: [700, 480, 245][i],
                  height: [470, 320, 520][i],
                  overflow: "hidden",
                  transform: `translateY(${90 * (1 - out(s, i * 0.18, i * 0.18 + 0.8))}px) rotate(${[-3, 4, 7][i]}deg)`,
                  opacity: out(s, i * 0.12, i * 0.12 + 0.6),
                  border: "12px solid #eae5d8",
                  boxShadow: "0 20px 70px #0008",
                }}
              >
                <Pic src={src} />
              </div>
            ))}
            <Type
              serif
              size={80}
              style={{ ...A, left: 20, top: 635, rotate: "-4deg" }}
            >
              A gallery worth keeping.
            </Type>
          </div>
        )}
        {beat === 4 && (
          <div style={{ ...A, inset: 0 }}>
            {["01", "02"].map((id, i) => (
              <div
                key={id}
                style={{
                  ...A,
                  left: i * 495,
                  top: 60 + i * 60,
                  width: 465,
                  height: 630,
                  overflow: "hidden",
                  borderRadius: 10,
                  transform: `rotate(${i ? 4 : -4}deg)`,
                }}
              >
                <Pic src={`rebuild/wedding/${id}/batch-before.png`} />
                <Pic
                  src={`rebuild/wedding/${id}/batch-after.png`}
                  style={{
                    clipPath: `inset(0 ${100 * (1 - smooth(s, 0.5 + i * 0.4, 2 + i * 0.4))}% 0 0)`,
                  }}
                />
                <div
                  style={{
                    ...A,
                    bottom: 0,
                    padding: 22,
                    background: "#10151dbb",
                    width: "100%",
                  }}
                >
                  <Label style={{ fontSize: 17 }}>
                    SHARED GRADE / EDITABLE
                  </Label>
                </div>
              </div>
            ))}
            <Pills
              items={["BATCH ADJUSTMENTS", "INDIVIDUAL CONTROL"]}
              style={{ ...A, left: 25, top: 750 }}
            />
          </div>
        )}
      </Stage>
      <PaperGrain opacity={0.09} />
      <Footer
        left={
          beat === 2
            ? "FIND THE FRAME YOU REMEMBER."
            : "LESS SORTING. MORE CREATING."
        }
        right="LOOKSY / LIBRARY"
      />
    </>
  );
};
