import {
  AppIcon,
  AppInspectorSlider,
  AppOpticalField,
} from "../app-kit/AppElements";
import {
  abs,
  enter,
  mix,
  photos,
  Photo,
  Panel,
  T,
  ToneCurve,
  NativeRows,
  windowAt,
  pressAt,
  Pose,
  contour,
} from "./system";

export const Desk = ({
  t,
  closing = false,
}: {
  t: number;
  closing?: boolean;
}) => {
  const fade = closing ? mix(t, 116.5, 118) : 1 - mix(t, 4.8, 6.8);
  return (
    <svg
      width={1080}
      height={1920}
      style={{ ...abs, inset: 0, opacity: fade }}
      viewBox="0 0 1080 1920"
    >
      <path d="M60 1300L900 1180L1080 1560L210 1750Z" fill="#242424" />
      <path
        d="M210 1750V1900M1020 1570V1810"
        stroke="#393939"
        strokeWidth={20}
      />
      <path
        d="M150 650Q150 625 175 625H900Q925 625 925 650V1250Q925 1275 900 1275H175Q150 1275 150 1250Z"
        fill="#050505"
        stroke="#484848"
        strokeWidth={9}
      />
      <path d="M450 1280L430 1330H670L640 1280" fill="#393939" />
      <g transform="translate(65 1380) rotate(-12)">
        <path
          d="M0 30H45L60 8H138L152 30H195V160H0Z"
          fill="#101010"
          stroke="#585858"
          strokeWidth={3}
        />
        <circle
          cx={95}
          cy={93}
          r={58}
          fill="#242424"
          stroke="#585858"
          strokeWidth={4}
        />
        <circle cx={95} cy={93} r={42} fill="#050505" />
        <path
          d="M70 82Q75 58 100 60"
          stroke={T.brandBlue}
          strokeWidth={6}
          fill="none"
        />
      </g>
      <g transform="translate(800 1320) rotate(12)">
        <rect width={120} height={185} rx={5} fill="#393939" />
        <path d="M20 25H100M20 31H100" stroke="#585858" />
        <text
          x={20}
          y={150}
          fill="#f3f3f3"
          fontFamily="Menlo,monospace"
          fontSize={15}
        >
          NEPAL
        </text>
        <text
          x={20}
          y={173}
          fill="#b5b5b5"
          fontFamily="Menlo,monospace"
          fontSize={11}
        >
          SOURCE / 01
        </text>
      </g>
      <g transform="translate(285 1450) rotate(-8)">
        <path d="M0 0H75V100H0Z" fill="#101010" stroke="#585858" />
        <rect x={12} y={48} width={51} height={35} fill={T.brandRed} />
        <text x={18} y={72} fill="white" fontFamily="Menlo" fontSize={20}>
          RAW
        </text>
      </g>
      <g transform="translate(480 1370) rotate(-9)">
        <path d="M0 0H235L280 90H20Z" fill="#181818" stroke="#585858" />
        {Array.from({ length: 28 }, (_, i) => (
          <rect
            key={i}
            x={20 + (i % 7) * 29 + Math.floor(i / 7) * 4}
            y={12 + Math.floor(i / 7) * 15}
            width={22}
            height={9}
            fill="#484848"
          />
        ))}
      </g>
      <g transform="translate(380 1570) rotate(-7)">
        <rect width={350} height={170} fill="#181818" />
        <text x={24} y={36} fontSize={20} fill="white" fontFamily="Menlo">
          NEPAL / DELIVERY
        </text>
        {["PHOTOGRAPHS", "FILM", "CAROUSEL"].map((v, i) => (
          <g key={v}>
            <rect
              x={24}
              y={59 + i * 31}
              width={15}
              height={15}
              stroke="#999"
              fill="none"
            />
            {closing && (
              <path
                d={`M25 ${66 + i * 31}l5 5 10-12`}
                stroke={T.brandGreen}
                strokeWidth={3}
                fill="none"
              />
            )}
            <text
              x={51}
              y={73 + i * 31}
              fontSize={16}
              fill="#b5b5b5"
              fontFamily="Menlo"
            >
              {v}
            </text>
          </g>
        ))}
      </g>
    </svg>
  );
};

export const Library = ({ t }: { t: number }) => {
  const initial = mix(t, 4.7, 6.5),
    leaving = mix(t, 12, 13.8),
    visible = 1 - leaving;
  if (t > 14) return null;
  return (
    <div style={{ opacity: visible }}>
      <div
        style={{
          ...abs,
          inset: 0,
          height: 1920,
          clipPath: "inset(0 0 470px 0)",
        }}
      >
        {photos.slice(0, 12).map((name, i) => {
          if (i === 1) return null;
          const x0 = 260 + (i % 3) * 200,
            y0 = 740 + Math.floor(i / 3) * 240;
          const x1 = 90 + (i % 3) * 315,
            y1 = 570 + Math.floor(i / 3) * 390;
          const amount = enter(t, 0.4 + i * 0.13, 1.5 + i * 0.13);
          const w = 130 + 140 * initial,
            h = 195 + 160 * initial;
          const x =
            x0 + (x1 - x0) * initial + (x1 < 400 ? -500 : 700) * leaving;
          const y = y0 + (y1 - y0) * initial;
          return (
            <div
              key={name}
              style={{
                ...abs,
                left: x,
                top: y,
                width: w,
                height: h,
                clipPath: `path('${contour(w, h, (1 - amount) * 1.8, t / 9, Math.max(0.05, amount))}')`,
                opacity: i > 5 ? initial : 1,
                rotate: `${(1 - amount) * (i % 2 ? 7 : -7)}deg`,
                background: T.brandInk,
              }}
            >
              <Photo
                name={name}
                style={{
                  filter: `brightness(${1 + mix(t, 9.2, 10.6, 0, i === 4 || i === 5 ? 0.1 : 0)})`,
                }}
              />
              {(i === 4 || i === 5) && (
                <div
                  style={{
                    ...abs,
                    right: 10,
                    top: 10,
                    opacity: mix(t, 8, 9.2),
                    background: T.ink900,
                    padding: 6,
                  }}
                >
                  <AppIcon name="check" size={18} />
                </div>
              )}
              <div
                style={{
                  ...abs,
                  bottom: 0,
                  left: 0,
                  right: 0,
                  height: 30,
                  padding: "7px 9px",
                  background: T.ink900,
                  fontFamily: "Menlo",
                  fontSize: 12,
                }}
              >
                {name}.CR2
              </div>
            </div>
          );
        })}
      </div>
      <div
        style={{
          ...abs,
          left: 90,
          top: 488,
          opacity: initial,
          fontSize: 20,
          color: T.textMuted,
        }}
      >
        NEPAL COLLECTION · RAW + VIDEO
      </div>
      <div
        style={{
          ...abs,
          left: 90,
          top: 1490,
          opacity: initial,
          width: 900,
          fontSize: 22,
          display: "flex",
          gap: 30,
          color: T.textMuted,
        }}
      >
        <span>Batch import</span>
        <span>Picks · Ratings · Filters</span>
        <span>Sync selected edits</span>
      </div>
      <div
        style={{
          ...abs,
          left: 180,
          top: 1210,
          width: 720,
          opacity: 1 - initial,
        }}
      >
        <AppOpticalField
          kind={2}
          width={720}
          height={5}
          progress={mix(t, 0, 3)}
        />
      </div>
      <div
        style={{ ...abs, left: 90, top: 1545, opacity: windowAt(t, 8, 12.5) }}
      >
        <div style={{ display: "flex", gap: 15 }}>
          {["★★★★★", "PICKED", "MATCH EXPOSURE"].map((v, i) => (
            <span
              key={v}
              style={{
                padding: "12px 18px",
                fontSize: 17,
                background: i === 1 ? T.ink800 : T.ink900,
              }}
            >
              {v}
            </span>
          ))}
        </div>
        <div style={{ fontSize: 18, color: T.textMuted, marginTop: 20 }}>
          Apply the selected adjustments. Refine each image.
        </div>
      </div>
    </div>
  );
};

export const PhotoInspector = ({
  t,
  exposure,
}: {
  t: number;
  exposure: number;
}) => {
  const opacity = windowAt(t, 12.7, 27.2);
  if (!opacity) return null;
  const curve = mix(t, 20, 23),
    wb = mix(t, 17.5, 19, 0, 24);
  return (
    <Panel x={674} y={560} w={370} style={{ opacity, padding: "22px 15px" }}>
      <div
        style={{
          fontSize: 18,
          fontWeight: 600,
          display: "flex",
          alignItems: "center",
          gap: 10,
          marginBottom: 20,
        }}
      >
        <AppIcon name="adjust" size={20} />
        Light
      </div>
      <AppInspectorSlider
        label="Exposure"
        value={exposure}
        valueText={`${exposure >= 0 ? "+" : ""}${exposure.toFixed(2)} EV`}
        from={-5}
        to={5}
        precisionResponse
        press={pressAt(t, 15.3, 17.1)}
        showCursor={t > 15.3 && t < 17.4}
      />
      <AppInspectorSlider label="Contrast" value={8} valueText="+8%" />
      <AppInspectorSlider
        label="Highlights"
        value={-mix(t, 16, 18.2, 0, 28)}
        valueText={`-${Math.round(mix(t, 16, 18.2, 0, 28))}%`}
      />
      <AppInspectorSlider label="Shadows" value={16} valueText="+16%" />
      <div style={{ fontSize: 15, marginTop: 18, marginBottom: 8 }}>
        White balance
      </div>
      <AppInspectorSlider
        label="Temperature"
        value={wb}
        valueText={`${Math.round(5500 + wb * 65)} K`}
        trackColors={["#3282ff", "#e6e6e6", "#e6c382"]}
      />
      <div
        style={{
          fontSize: 16,
          margin: "20px 0 12px",
          display: "flex",
          justifyContent: "space-between",
        }}
      >
        <span>Tone curve</span>
        <span style={{ color: T.textMuted }}>RGB</span>
      </div>
      <ToneCurve amount={curve} size={330} />
      <div style={{ marginTop: 18, fontSize: 15, color: T.textMuted }}>
        Color mix · Detail · Optics
      </div>
    </Panel>
  );
};

export const MaskAndPixels = ({ t, pose }: { t: number; pose: Pose }) => {
  if (t < 25.5 || t > 35.7) return null;
  const show = windowAt(t, 25.5, 35.7),
    grid = mix(t, 30, 31.2) * (1 - mix(t, 33.7, 35));
  const draw =
    mix(t, 31.5, 32.1) * (1 - mix(t, 32.7, 33.1)) + mix(t, 33.25, 33.65) * 0.65;
  return (
    <>
      <Panel x={720} y={590} w={320} style={{ opacity: show, padding: 18 }}>
        <div style={{ fontSize: 20, fontWeight: 600, marginBottom: 20 }}>
          Subject mask
        </div>
        <div style={{ display: "flex", gap: 8, marginBottom: 18 }}>
          {["Add", "Subtract"].map((v, i) => (
            <span
              key={v}
              style={{
                fontSize: 16,
                padding: "8px 12px",
                background: i ? T.ink850 : T.ink800,
              }}
            >
              {v}
            </span>
          ))}
        </div>
        <AppInspectorSlider
          label="Feather"
          value={22}
          valueText="22 px"
          from={0}
          to={100}
          width={280}
        />
        <AppInspectorSlider
          label="Opacity"
          value={100}
          valueText="100%"
          from={0}
          to={100}
          width={280}
        />
        <NativeRows
          rows={["Original image", "Subject mask", "Local adjustment"]}
          active={1}
        />
      </Panel>
      <div
        style={{
          ...abs,
          left: pose.x + 70,
          top: pose.y + 200,
          width: 400,
          height: 400,
          opacity: grid,
          background: T.ink950,
          border: "1px solid #585858",
          overflow: "hidden",
          scale: 1 + grid * 0.12,
          rotate: "3deg",
        }}
      >
        <Photo
          name="IMG_5106"
          style={{
            width: 1300,
            height: 1950,
            position: "absolute",
            left: -565,
            top: -710,
          }}
        />
        <svg width={400} height={400} style={{ ...abs, inset: 0 }}>
          {Array.from({ length: 21 }, (_, i) => (
            <g key={i} stroke="rgba(243,243,243,.28)" strokeWidth={1}>
              <path d={`M${i * 20} 0V400M0 ${i * 20}H400`} />
            </g>
          ))}
          <path
            d="M160 210L180 190L200 210L220 190L240 210"
            stroke={T.brandRed}
            strokeWidth={18}
            fill="none"
            pathLength={1}
            strokeDasharray={1}
            strokeDashoffset={1 - draw}
          />
        </svg>
        <div
          style={{
            ...abs,
            left: 12,
            bottom: 12,
            padding: "8px 12px",
            background: T.ink900,
            fontSize: 15,
          }}
        >
          Pixel layer · Brush / Erase · Undo
        </div>
      </div>
      <div
        style={{
          ...abs,
          left: 72,
          top: 1490,
          opacity: show,
          fontSize: 28,
          color: T.textMuted,
        }}
      >
        Subject selection. Edge refinement. Native pixel layers.
      </div>
    </>
  );
};

export const CompositionTools = ({ t }: { t: number }) => {
  const show = windowAt(t, 35, 56.8);
  if (!show) return null;
  const effects = t > 48.8;
  const typography = t > 43.7 && t < 46.7;
  return (
    <>
      <Panel
        x={604 + mix(t, 43, 44, 500, 0)}
        y={1210}
        w={398}
        style={{
          opacity: show,
          padding: 16,
          boxShadow: "0 12px 40px #0008",
          transform: `perspective(1600px) rotateY(${mix(t, 42.5, 44.5, 14, 0)}deg)`,
        }}
      >
        <div
          style={{
            fontSize: 19,
            fontWeight: 600,
            marginBottom: 14,
            display: "flex",
            gap: 12,
          }}
        >
          <AppIcon name="layers" size={22} />
          {effects ? "Effects" : typography ? "Typography" : "Layers"}
        </div>
        {typography ? (
          <>
            <div
              style={{
                padding: "13px 14px",
                background: T.ink850,
                fontSize: 20,
                marginBottom: 12,
              }}
            >
              Manrope / ExtraBold
            </div>
            <div style={{ display: "flex", gap: 12, marginBottom: 16 }}>
              {["B", "I", "U"].map((label, i) => (
                <span
                  key={label}
                  style={{
                    padding: "6px 14px",
                    background: i === 0 ? T.ink800 : T.ink900,
                    fontSize: 18,
                    fontWeight: i === 0 ? 700 : 500,
                  }}
                >
                  {label}
                </span>
              ))}
            </div>
            {[
              ["Size", "230 px"],
              [
                "Letter spacing",
                `${(-10 + mix(t, 44.2, 45.3, 0, 6)).toFixed(1)} px`,
              ],
              ["Line height", "200 px"],
            ].map(([label, value]) => (
              <div
                key={label}
                style={{
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "space-between",
                  height: 43,
                  fontSize: 18,
                }}
              >
                <span style={{ color: T.textMuted }}>{label}</span>
                <span
                  style={{
                    fontFamily: "Menlo",
                    padding: "5px 9px",
                    background: T.ink850,
                  }}
                >
                  {value}
                </span>
              </div>
            ))}
          </>
        ) : (
          <>
            {" "}
            <NativeRows
              rows={
                effects
                  ? ["Optical diffusion", "Film grain", "Subject mask"]
                  : [
                      "Editable typography",
                      "Paint layer",
                      "Subject mask",
                      "Mountain source",
                      "Original image",
                    ]
              }
              active={effects ? Math.floor(mix(t, 49, 53, 0, 1)) : 0}
            />
          </>
        )}
        {effects && (
          <>
            <AppInspectorSlider
              label="Amount"
              value={mix(t, 50, 52, 0, 28) - mix(t, 53, 54, 0, 14)}
              valueText={`${Math.round(mix(t, 50, 52, 0, 28) - mix(t, 53, 54, 0, 14))}%`}
              from={0}
              to={100}
              width={360}
              press={pressAt(t, 50, 52.5)}
              showCursor={t > 50 && t < 52.5}
            />
            <div style={{ fontSize: 15, color: T.textMuted, marginTop: 8 }}>
              Mask · Reorder · Bypass · Revise
            </div>
          </>
        )}
      </Panel>
      <div
        style={{
          ...abs,
          left: 72,
          top: 1740,
          fontSize: 23,
          color: T.textMuted,
          opacity: show,
        }}
      >
        {effects
          ? "Optical effects. Adjustable stacks. Your original intact."
          : "Image layers · Text · Shapes · Paint · Masks · Blend"}
      </div>
    </>
  );
};
