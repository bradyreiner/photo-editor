import { useId } from "react";
import { AppIcon, AppInspectorSlider } from "../app-kit/AppElements";
import { sampleAppCurve } from "../app-kit/curve-sampling";
import {
  A,
  asset,
  C,
  Copy,
  Grain,
  land,
  lerp,
  LightMaterial,
  Lockup,
  Meta,
  mono,
  p,
  Photo,
  Plate,
  sans,
  smoothPath,
  aperture,
  pop,
} from "./design";

const images = [
  "IMG_5071",
  "IMG_5106",
  "IMG_4870",
  "IMG_4969",
  "IMG_5103",
  "IMG_5111",
  "IMG_4856",
  "IMG_5125",
  "IMG_4744",
  "IMG_5053",
  "IMG_5124",
  "IMG_5077",
];
const ContactSheet = ({
  t,
  scale = 1,
  flowTime,
}: {
  t: number;
  scale?: number;
  flowTime?: number;
}) => (
  <div
    style={{
      width: 1200,
      height: 1450,
      position: "relative",
      transformOrigin: "0 0",
      scale,
    }}
  >
    {images.map((name, i) => {
      const selected = [1, 4, 5].includes(i),
        arrive = land(t, 0.12 + i * 0.06, 0.9 + i * 0.06);
      const column = i % 3,
        row = Math.floor(i / 3);
      const settle =
        flowTime === undefined
          ? 1
          : land(flowTime, 0.025 + i * 0.027, 0.72 + i * 0.027);
      const q = 1 - settle;
      const endX = 26 + column * 396 + 185,
        endY = 24 + row * 350 + 160;
      // Photos leave the original logo's RGB period, open into curved depth
      // paths, then become the native contact sheet. The carrier is the media.
      const flowX =
        q * q * q * 892 +
        3 * q * q * settle * (892 + (column - 1) * 170) +
        3 * q * settle * settle * (endX + (column - 1) * 250) +
        settle ** 3 * endX -
        185;
      const flowY =
        q * q * q * 623 +
        3 * q * q * settle * (560 + (row - 1.5) * 90) +
        3 * q * settle * settle * (endY - 200) +
        settle ** 3 * endY -
        160;
      const size =
        flowTime === undefined
          ? 1
          : lerp(0.16, 1, pop(flowTime, 0.025 + i * 0.027, 0.8 + i * 0.027));
      return (
        <div
          key={name}
          style={{
            ...A,
            left: flowTime === undefined ? 26 + column * 396 : flowX,
            top: flowTime === undefined ? 24 + row * 350 : flowY,
            width: 370,
            height: 320,
            overflow: "hidden",
            translate:
              flowTime === undefined ? `0 ${(1 - arrive) * 90}px` : undefined,
            opacity:
              flowTime === undefined
                ? arrive
                : land(flowTime, 0.025 + i * 0.027, 0.13 + i * 0.027),
            transform:
              flowTime === undefined
                ? undefined
                : `perspective(1800px) rotateX(${-q * 38}deg) rotateY(${q * (column - 1) * 48}deg) rotateZ(${q * ((column - 1) * 16 + (row - 1.5) * 8)}deg) scale(${size})`,
            boxShadow:
              flowTime === undefined
                ? undefined
                : `${-q * 12}px ${q * 4}px 0 ${C.brandRed}, ${q * 12}px ${-q * 4}px 0 ${C.brandBlue}`,
            border: `3px solid ${selected && t > 1.8 ? C.brandPaper : "transparent"}`,
          }}
        >
          <Photo
            name={name}
            style={{
              filter: `brightness(${1 + p(t, 2.3, 3.4, 0, selected ? 0.11 : 0)})`,
            }}
          />
          <div
            style={{
              ...A,
              bottom: 0,
              left: 0,
              right: 0,
              padding: "9px 12px",
              fontFamily: mono,
              fontSize: 16,
              color: C.textMuted,
              background: "#101010e8",
            }}
          >
            {name}.CR2
          </div>
          {selected && t > 1.8 && (
            <div
              style={{
                ...A,
                top: 13,
                right: 13,
                background: C.brandPaper,
                color: C.brandInk,
                padding: 7,
              }}
            >
              <AppIcon name="check" color={C.brandInk} size={22} />
            </div>
          )}
        </div>
      );
    })}
  </div>
);

export const DeskShot = ({ t }: { t: number }) => {
  const drive = p(t, 1.3, 4.35),
    iris = p(t, 3.8, 4.9);
  const s = lerp(0.82, 1.28, drive),
    tx = lerp(-160, -484, drive),
    ty = lerp(440, -35, drive),
    r = lerp(-9, 0, drive);
  return (
    <Plate>
      <div
        style={{
          ...A,
          inset: 0,
          background: "linear-gradient(135deg,#222426,#050505 75%)",
        }}
      />
      <div
        style={{
          ...A,
          left: tx,
          top: ty,
          width: 1600,
          height: 1600,
          transformOrigin: "0 0",
          scale: s,
          rotate: `${r}deg`,
        }}
      >
        <svg
          width={1600}
          height={1750}
          style={{ ...A, inset: 0 }}
          viewBox="0 0 1600 1750"
        >
          <path d="M-100 1050H1750V1900H-100Z" fill="#181818" />
          <path d="M-100 1050H1750" stroke="#393939" strokeWidth={3} />
          <path
            d="M720 1000L690 1135H1045L1015 1000"
            fill="#242424"
            stroke="#585858"
            strokeWidth={3}
          />
          <rect
            x={175}
            y={160}
            width={1250}
            height={855}
            rx={24}
            fill="#050505"
            stroke="#484848"
            strokeWidth={5}
          />
          <rect
            x={192}
            y={178}
            width={1216}
            height={814}
            rx={8}
            fill="#101010"
          />
          <circle cx={800} cy={1002} r={3} fill="#585858" />
          <g transform="translate(-120 1200) rotate(-15)">
            <path
              d="M0 90H70L115 25H360L400 90H510V420H0Z"
              fill="#101010"
              stroke="#484848"
              strokeWidth={5}
            />
            <rect
              x={190}
              y={48}
              width={105}
              height={35}
              rx={6}
              fill="#242424"
            />
            <circle
              cx={245}
              cy={250}
              r={158}
              fill="#050505"
              stroke="#585858"
              strokeWidth={7}
            />
            <circle
              cx={245}
              cy={250}
              r={137}
              fill="#181818"
              stroke="#383838"
              strokeWidth={14}
            />
            <circle
              cx={245}
              cy={250}
              r={111}
              fill="#050505"
              stroke="#484848"
              strokeWidth={3}
            />
            <path
              d="M145 230A105 105 0 0 1 265 145"
              stroke={C.brandBlue}
              strokeWidth={5}
              fill="none"
            />
            <path
              d="M165 220A80 80 0 0 1 252 171"
              stroke={C.brandGreen}
              opacity={0.6}
              strokeWidth={3}
              fill="none"
            />
            <path
              d="M298 333A100 100 0 0 0 342 220"
              stroke={C.brandRed}
              opacity={0.7}
              strokeWidth={4}
              fill="none"
            />
            {Array.from({ length: 24 }, (_, i) => (
              <path
                key={i}
                d="M245 101V111"
                stroke="#777"
                strokeWidth={2}
                transform={`rotate(${i * 15} 245 250)`}
              />
            ))}
          </g>
          <g transform="translate(515 1260) rotate(5)">
            <rect
              width={670}
              height={230}
              rx={10}
              fill="#242424"
              stroke="#484848"
              strokeWidth={3}
            />
            {Array.from({ length: 60 }, (_, i) => (
              <rect
                key={i}
                x={17 + (i % 15) * 42}
                y={18 + Math.floor(i / 15) * 44}
                width={34}
                height={33}
                rx={3}
                fill="#101010"
                stroke="#393939"
              />
            ))}
            <rect
              x={155}
              y={193}
              width={330}
              height={20}
              rx={4}
              fill="#101010"
            />
          </g>
          <g transform="translate(1300 1165) rotate(13)">
            <path d="M0 0H140L180 40V260H0Z" fill="#393939" />
            <path
              d="M18 21H118M18 34H118M18 47H118"
              stroke="#9c9c9c"
              strokeWidth={7}
            />
            <rect x={15} y={78} width={150} height={110} fill="#101010" />
            <text
              x={30}
              y={120}
              fontFamily={sans}
              fill="#f3f3f3"
              fontWeight={700}
              fontSize={34}
            >
              RAW
            </text>
            <text x={30} y={153} fontFamily={mono} fill="#b5b5b5" fontSize={15}>
              NEPAL / 01
            </text>
          </g>
        </svg>
        <div
          style={{
            ...A,
            left: 220,
            top: 210,
            width: 1160,
            height: 735,
            overflow: "hidden",
          }}
        >
          <div
            style={{
              height: 64,
              display: "flex",
              alignItems: "center",
              justifyContent: "space-between",
              borderBottom: "1px solid #393939",
              padding: "0 18px",
            }}
          >
            <Lockup width={145} />
            <Meta style={{ fontSize: 17 }}>NEPAL / SOURCE</Meta>
          </div>
          <div
            style={{
              ...A,
              left: 0,
              top: 75,
              width: 1160,
              height: 510,
              overflow: "hidden",
            }}
          >
            <ContactSheet t={t + 0.7} scale={0.965} />
          </div>
          <div
            style={{
              ...A,
              left: 20,
              top: 630,
              right: 20,
              display: "flex",
              justifyContent: "space-between",
            }}
          >
            <Meta style={{ fontSize: 21 }}>236 RAW PHOTOS</Meta>
            <Meta style={{ fontSize: 21 }}>18 VIDEO CLIPS</Meta>
          </div>
          <svg width={1120} height={10} style={{ ...A, top: 700, left: 20 }}>
            <path d="M0 5H1120" stroke="#303030" strokeWidth={2} />
            <path
              d={`M0 5H${1120 * p(t, 0.4, 3.2)}`}
              stroke={C.brandPaper}
              strokeWidth={3}
            />
          </svg>
        </div>
      </div>
      {iris > 0 && (
        <svg width={1080} height={1920} style={{ ...A, inset: 0 }}>
          <path
            d={`M0 0H1080V1920H0Z M${540 + (1 - iris) * 1500} 960a${(1 - iris) * 1500 + 0.01} ${(1 - iris) * 1500 + 0.01} 0 1 0 ${-2 * (1 - iris) * 1500} 0a${(1 - iris) * 1500 + 0.01} ${(1 - iris) * 1500 + 0.01} 0 1 0 ${2 * (1 - iris) * 1500} 0`}
            fill={C.brandInk}
            fillRule="evenodd"
          />
        </svg>
      )}
      <Grain />
    </Plate>
  );
};

export const IdentityShot = ({ t }: { t: number }) => {
  const k = land(t, 0, 1.2),
    leave = p(t, 2.75, 3.8),
    angle = land(t, 0, 1.9, -14, 0);
  return (
    <Plate>
      <div
        style={{
          ...A,
          inset: 0,
          scale: lerp(1.65, 1, k),
          rotate: `${angle}deg`,
          opacity: 1 - leave,
        }}
      >
        <LightMaterial t={t + 7} form="brand-upper" width={280} />
        <LightMaterial t={t + 4} form="brand-lower" width={195} />
      </div>
      <div
        style={{
          ...A,
          left: 110,
          top: 765,
          width: 860,
          translate: `0 ${land(t, 0.4, 1.4, 100, 0)}px`,
          opacity: land(t, 0.4, 0.8),
        }}
      >
        <Lockup width={860} />
        <div
          style={{
            fontSize: 60,
            letterSpacing: -2,
            marginTop: 45,
            fontWeight: 500,
          }}
        >
          Photo. Design. Video.
        </div>
      </div>
      <Grain />
    </Plate>
  );
};

export const LibraryShot = ({ t }: { t: number }) => {
  const away = p(t, 3.7, 5),
    pick = land(t, 0.7, 1.7);
  return (
    <Plate>
      <div
        style={{
          ...A,
          left: 110,
          top: 765,
          width: 860,
          opacity: 1 - land(t, 0, 0.5),
        }}
      >
        <Lockup width={860} />
        <div
          style={{
            fontSize: 60,
            letterSpacing: -2,
            marginTop: 45,
            fontWeight: 500,
          }}
        >
          Photo. Design. Video.
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 105,
          right: 80,
          display: "flex",
          justifyContent: "space-between",
          opacity: land(t, 0.35, 0.9) * (1 - away),
        }}
      >
        <Meta style={{ fontSize: 30 }}>NEPAL COLLECTION</Meta>
        <Meta style={{ fontSize: 30 }}>236 PHOTOS</Meta>
      </div>
      <div
        style={{
          ...A,
          left: -60,
          top: 200,
          width: 1320,
          height: 1390,
          overflow: "hidden",
          transformOrigin: "50% 30%",
          rotate: `${lerp(4, 0, pick)}deg`,
          scale: 1 + away * 0.4,
          translate: `${-away * 115}px ${-away * 200}px`,
          opacity: 1 - away,
        }}
      >
        <ContactSheet t={t + 1} scale={1.1} flowTime={t} />
      </div>
      <div
        style={{
          ...A,
          left: 0,
          top: 1630,
          width: 1080,
          height: 290,
          background: C.ink900,
          opacity: land(t, 0.7, 1.2) * (1 - away),
          padding: "38px 80px",
          boxSizing: "border-box",
        }}
      >
        <div
          style={{
            display: "flex",
            gap: 22,
            alignItems: "center",
            fontSize: 44,
            fontWeight: 500,
          }}
        >
          {t > 0.8 && <AppIcon name="check" size={34} />}
          {t > 0.8 ? "3 selected" : "Select originals"}
        </div>
        <div
          style={{
            display: "flex",
            justifyContent: "space-between",
            marginTop: 35,
            fontSize: 32,
            color: C.textMuted,
          }}
        >
          <span>Batch import</span>
          <span>Rate & cull</span>
          <span style={{ color: t > 2.3 ? C.brandPaper : C.textMuted }}>
            Sync edits
          </span>
        </div>
      </div>
      {away > 0.001 && (
        <div
          style={{
            ...A,
            left: lerp(404, 80, away),
            top: lerp(226, 230, away),
            width: lerp(407, 920, away),
            height: lerp(352, 1380, away),
            overflow: "hidden",
            clipPath: `path('${aperture(lerp(407, 920, away), lerp(352, 1380, away), 1 - 0.08 * Math.sin(Math.PI * away), t)}')`,
          }}
        >
          <Photo />
          <div style={{ ...A, inset: 0, border: "2px solid #f3f3f3" }} />
        </div>
      )}
      <Grain />
    </Plate>
  );
};

export const RawShot = ({ t }: { t: number }) => {
  const id = useId().replace(/:/g, "");
  const develop = p(t, 1.2, 2.8),
    exposure = 0.65 * develop,
    wb = p(t, 3.2, 4.3, 0, 24),
    curve = p(t, 7.15, 9.3),
    macro = p(t, 6.1, 7.1);
  const channel = Math.sin(Math.PI * p(t, 0, 1.45));
  const points = [
    { x: 0, y: 0 },
    { x: 0.28, y: 0.28 - curve * 0.09 },
    { x: 0.72, y: 0.72 + curve * 0.09 },
    { x: 1, y: 1 },
  ];
  const samples = sampleAppCurve(points);
  return (
    <Plate>
      <div style={{ ...A, inset: 0, translate: `0 ${-1700 * macro}px` }}>
        <div style={{ ...A, left: 80, top: 115 }}>
          <Meta style={{ fontSize: 31 }}>RAW DEVELOPMENT</Meta>
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
            style={{
              filter: `brightness(${0.86 + develop * 0.18}) saturate(${1 + wb * 0.002})`,
              opacity: 1 - channel,
            }}
          />
          {channel > 0.001 &&
            [C.brandRed, C.brandGreen, C.brandBlue].map((color, i) => (
              <svg
                key={color}
                width="100%"
                height="100%"
                viewBox="0 0 1200 1800"
                style={{
                  ...A,
                  inset: 0,
                  mixBlendMode: "screen",
                  opacity: channel,
                  translate: `${(i - 1) * channel * 100}px 0`,
                }}
              >
                <defs>
                  <filter id={`${id}${i}`} colorInterpolationFilters="sRGB">
                    <feColorMatrix
                      type="matrix"
                      values={
                        i === 0
                          ? "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0"
                          : i === 1
                            ? "0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 1 0"
                            : "0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 1 0"
                      }
                    />
                  </filter>
                </defs>
                <image
                  href={asset("IMG_5106")}
                  width={1200}
                  height={1800}
                  filter={`url(#${id}${i})`}
                />
              </svg>
            ))}
        </div>
        <div
          style={{
            ...A,
            left: 80,
            top: 1060,
            background: "#181818fa",
            padding: "25px 22px",
            width: 920,
            boxSizing: "border-box",
          }}
        >
          <AppInspectorSlider
            label="Exposure"
            value={exposure}
            valueText={`+${exposure.toFixed(2)} EV`}
            from={-5}
            to={5}
            precisionResponse
            scale={2.8}
            width={313}
            press={p(t, 1.05, 1.2) * (1 - p(t, 2.8, 2.95))}
            showCursor={t > 1.2 && t < 3}
          />
          <AppInspectorSlider
            label="Contrast"
            value={8 * develop}
            valueText={`+${Math.round(8 * develop)}%`}
            scale={2.8}
            width={313}
          />
          <AppInspectorSlider
            label="Highlights"
            value={-28 * develop}
            valueText={`${Math.round(-28 * develop)}%`}
            scale={2.8}
            width={313}
          />
          <AppInspectorSlider
            label="Shadows"
            value={16 * develop}
            valueText={`+${Math.round(16 * develop)}%`}
            scale={2.8}
            width={313}
          />
          <AppInspectorSlider
            label="Temperature"
            value={wb}
            valueText={`${Math.round(5500 + wb * 65)} K`}
            scale={2.8}
            width={313}
            trackColors={[C.brandBlue, "#e6e6e6", "#e6c382"]}
          />
        </div>
        <div
          style={{
            ...A,
            left: 80,
            top: 1830,
            right: 80,
            display: "flex",
            justifyContent: "space-between",
            fontSize: 36,
            opacity: land(t, 5.8, 6.1),
          }}
        >
          <span>Tone curve</span>
          <span style={{ fontFamily: mono }}>RGB</span>
        </div>
        <div
          style={{
            ...A,
            left: 80,
            top: 1980,
            width: 920,
            height: 1040,
            opacity: land(t, 5.8, 6.1),
          }}
        >
          <svg
            width={920}
            height={1040}
            viewBox="0 0 920 1040"
            preserveAspectRatio="none"
          >
            <g stroke="#303030" strokeWidth={1.3}>
              {Array.from({ length: 5 }, (_, i) => (
                <g key={i}>
                  <path d={`M${i * 230} 0V1040`} />
                  <path d={`M0 ${i * 260}H920`} />
                </g>
              ))}
            </g>
            <path
              d="M0 1040L920 0"
              stroke="#585858"
              strokeWidth={2}
              strokeDasharray="6 10"
            />
            <path
              d={smoothPath(
                samples.map((v) => ({ x: v.x * 920, y: (1 - v.y) * 1040 })),
              )}
              fill="none"
              stroke="white"
              strokeWidth={5}
            />
            {points.map((v, i) => (
              <g key={i}>
                <circle
                  cx={v.x * 920}
                  cy={(1 - v.y) * 1040}
                  r={i === 1 ? 17 : 11}
                  fill={i === 1 ? C.brandInk : C.brandPaper}
                  stroke="white"
                  strokeWidth={4}
                />
                {i === 1 && curve < 0.99 && (
                  <circle
                    cx={v.x * 920}
                    cy={(1 - v.y) * 1040}
                    r={36}
                    stroke="white"
                    opacity={0.25}
                    fill="none"
                  />
                )}
              </g>
            ))}
          </svg>
        </div>
      </div>
      <div style={{ ...A, left: 80, top: 1700, opacity: 1 - p(t, 5.5, 5.85) }}>
        <div style={{ fontSize: 43, fontWeight: 500, letterSpacing: -1 }}>
          Your RAW. Fully developed.
        </div>
        <Meta style={{ marginTop: 20, fontSize: 28 }}>
          4000 × 6000 / NATIVE RESOLUTION
        </Meta>
      </div>
      <div style={{ ...A, left: 80, top: 1470, opacity: land(t, 6.95, 7.25) }}>
        <Copy t={t} start={7.05} size={116}>
          <div>A custom</div>
          <div>color engine.</div>
        </Copy>
      </div>
      <div style={{ ...A, left: 80, top: 1770, opacity: land(t, 7.3, 7.8) }}>
        <Meta style={{ fontSize: 28 }}>LIGHT / TONE / COLOR / DETAIL</Meta>
      </div>
      <Grain />
    </Plate>
  );
};
