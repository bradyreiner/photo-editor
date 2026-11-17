import { getLength, getPointAtLength } from "@remotion/paths";
import { staticFile } from "remotion";
import { RetouchInsert } from "./Retouch";
import { AppActivity, AppWordmark } from "../app-kit/AppElements";
import {
  A,
  Bar,
  Caption,
  ColorResponse,
  Control,
  Curve,
  fast,
  Icon,
  mix,
  muted,
  Panel,
  Photo,
  PixelPhoto,
  s,
  Tag,
  Title,
  useTime,
  window,
} from "./Core";
import { Artwork } from "./Artwork";

const shoot = [
  "studio/raw-colour/0-0.png",
  "IMG_5071.jpg",
  "IMG_4969.jpg",
  "IMG_4856.jpg",
  "IMG_4870.jpg",
  "IMG_5103.jpg",
  "IMG_5111.jpg",
  "IMG_4744.jpg",
  "IMG_1268-2-7.jpg",
];
export const Intro = () => {
  const t = useTime(),
    reveal = s(t, 0, 3.4),
    library = s(t, 8.8, 10.8),
    open = s(t, 12, 14);
  return (
    <>
      <div
        style={{
          ...A,
          left: mix(0, 180, reveal),
          top: mix(0, 800, reveal),
          width: mix(1080, 720, reveal),
          height: mix(1920, 900, reveal),
          overflow: "hidden",
          opacity: 1 - library,
          boxShadow: "0 40px 100px #0008",
        }}
      >
        <Artwork />
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 160,
          opacity: fast(t, 1.7, 2.4) * (1 - s(t, 8.2, 8.8)),
          translate: `0 ${35 * (1 - fast(t, 1.7, 2.8))}px`,
        }}
      >
        <Tag>INTRODUCING</Tag>
        <div style={{ marginTop: 32 }}>
          <AppWordmark width={730} />
        </div>
        <div
          style={{
            fontSize: 86,
            fontWeight: 500,
            letterSpacing: -4,
            lineHeight: 1.07,
            marginTop: 68,
            opacity: fast(t, 3.2, 3.9),
          }}
        >
          A photo and
          <br />
          video studio.
        </div>
        <div
          style={{
            fontSize: 44,
            color: muted,
            marginTop: 34,
            opacity: fast(t, 4, 4.6),
          }}
        >
          For professionals and hobbyists.
        </div>
      </div>
      <div style={{ opacity: library }}>
        <Bar mode="Library" />
        <Title
          t={t}
          a={9.1}
          b={14.2}
          lines={["Bring the", "whole shoot."]}
          detail="RAW photographs. 4K footage. Your originals."
        />
        {shoot.map((src, i) => (
          <div
            key={src}
            style={{
              ...A,
              left: i === 0 ? mix(80, 0, open) : 80 + (i % 3) * 318,
              top:
                i === 0 ? mix(620, 540, open) : 620 + Math.floor(i / 3) * 318,
              width: i === 0 ? mix(284, 1080, open) : 284,
              height: i === 0 ? mix(284, 820, open) : 284,
              overflow: "hidden",
              borderRadius: 4,
              opacity: i === 0 ? 1 : 1 - open,
              translate: `0 ${100 * (1 - fast(t, 9.2 + i * 0.035, 10.7 + i * 0.035))}px`,
              zIndex: i === 0 ? 2 : 1,
            }}
          >
            <Photo src={src} />
            {i !== 0 && (
              <div
                style={{
                  ...A,
                  left: 14,
                  bottom: 14,
                  fontSize: 20,
                  color: "white",
                  textShadow: "0 2px 10px black",
                }}
              >
                ★ ★ ★ ★ ★
              </div>
            )}
          </div>
        ))}
        <Panel
          style={{
            ...A,
            left: 80,
            top: 1670,
            width: 920,
            padding: 30,
            display: "flex",
            alignItems: "center",
            gap: 25,
            opacity: 1 - open,
          }}
        >
          <AppActivity size={44} progress={s(t, 9.5, 11.2)} active={t < 11.2} />
          <div>
            <div style={{ fontSize: 36 }}>Import. Rate. Cull. Organize.</div>
            <Tag style={{ marginTop: 14 }}>ORIGINAL FILES / LOCAL LIBRARY</Tag>
          </div>
        </Panel>
      </div>
    </>
  );
};

const response = (t: number) =>
  t < 5
    ? { stage: 0, u: s(t, 0.8, 5) }
    : t < 8
      ? { stage: 1, u: s(t, 5, 8) }
      : t < 12
        ? { stage: 2, u: s(t, 8, 12) }
        : t < 17
          ? { stage: 3, u: s(t, 12, 17) }
          : { stage: 4, u: s(t, 17, 20.5) };
export const ColorStudio = () => {
  const t = useTime(),
    r = response(t),
    compare = 0,
    split = 0;
  const exposure = 1.4 * (r.stage === 0 ? r.u : 1),
    temp = 16 * (r.stage < 2 ? 0 : r.stage === 2 ? r.u : 1);
  return (
    <>
      <Bar
        mode={
          t < 20.5
            ? "Develop / custom color engine"
            : "Retouch / local inpainting"
        }
      />
      <Title
        t={t}
        b={7.6}
        lines={["A custom", "color engine."]}
        detail="RAW photo editing. At native resolution."
      />
      <Title
        t={t}
        a={7.8}
        b={12}
        lines={["Find your", "natural balance."]}
        detail="Exposure. White balance. Full control."
      />
      <Title
        t={t}
        a={12.1}
        b={17}
        lines={["Shape every tone."]}
        detail="RGB curves. Small moves. Real changes."
      />
      <Title
        t={t}
        a={17.1}
        b={20.5}
        lines={["Own every color."]}
        detail="Color Mix / target the blue, keep the yellow."
      />
      <div
        style={{
          ...A,
          left: 0,
          top: 540,
          width: 1080,
          height: 820,
          overflow: "hidden",
        }}
      >
        <ColorResponse stage={r.stage} amount={r.u} family="raw-colour" />
        <Photo
          src="studio/raw-colour/0-0.png"
          style={{
            ...A,
            inset: 0,
            clipPath: `inset(0 ${100 * (1 - split)}% 0 0)`,
          }}
        />
        {compare > 0 && (
          <div
            style={{
              ...A,
              left: 1080 * split,
              top: 0,
              bottom: 0,
              width: 2,
              background: "#f1eee7",
              opacity: compare,
            }}
          />
        )}
        <Tag
          style={{
            ...A,
            left: 80,
            top: 35,
            color: "#f4f0e8",
            background: "#1118",
            padding: "12px 16px",
          }}
        >
          IMG_4969.CR2 / FULL-RESOLUTION ORIGINAL
        </Tag>
        {compare > 0 && (
          <Tag
            style={{
              ...A,
              left: 80,
              top: 105,
              color: "white",
              opacity: compare,
            }}
          >
            BEFORE / AFTER
          </Tag>
        )}
      </div>
      <Panel
        style={{
          ...A,
          left: 80,
          top: 1430,
          width: 920,
          padding: "26px 30px",
          background: "#0e1418ed",
          opacity: (1 - compare) * (1 - s(t, 20.4, 20.6)),
        }}
      >
        <Tag style={{ marginBottom: 12 }}>
          {r.stage < 2
            ? "DEVELOP / LIGHT"
            : r.stage === 2
              ? "WHITE BALANCE"
              : r.stage === 3
                ? "CURVES / RGB"
                : "COLOR MIX / BLUES"}
        </Tag>
        {r.stage < 2 ? (
          <>
            <Control
              label="Exposure"
              value={exposure}
              text={`+${exposure.toFixed(2)} EV`}
              min={-5}
              max={5}
              cursor={t > 0.8 && t < 5}
            />
            <Control
              label="Shadows"
              value={35 * (r.stage === 1 ? r.u : 0)}
              text={`+${Math.round(35 * (r.stage === 1 ? r.u : 0))}`}
              cursor={t > 5 && t < 8}
            />
          </>
        ) : r.stage === 2 ? (
          <>
            <Control
              label="Temperature"
              value={temp}
              text={`+${Math.round(temp)}`}
              colors={["#679dc2", "#d9d3c7", "#e2a870"]}
              cursor={t > 8 && t < 12}
            />
            <Control
              label="Exposure"
              value={1.4}
              text="+1.40 EV"
              min={-5}
              max={5}
            />
          </>
        ) : r.stage === 3 ? (
          <div style={{ display: "flex", alignItems: "center", gap: 45 }}>
            <Curve amount={r.u} width={400} height={245} />
            <div style={{ fontSize: 34, lineHeight: 1.5, color: muted }}>
              RGB · Luma
              <br />
              Red · Green · Blue
            </div>
          </div>
        ) : (
          <>
            <Control
              label="Hue"
              value={-14 * r.u}
              text={`${Math.round(-14 * r.u)}`}
              cursor={t > 17 && t < 20.5}
            />
            <Control
              label="Saturation"
              value={18 * r.u}
              text={`+${Math.round(18 * r.u)}`}
            />
          </>
        )}
      </Panel>
      {t >= 20.5 && <RetouchInsert t={t} />}
      <Caption opacity={1 - compare}>
        A bespoke pipeline for the photographs you care about.
      </Caption>
    </>
  );
};

const brushPath = "M420 810 Q500 660 625 600 T750 470";
const brushLength = getLength(brushPath);
export const MaskStudio = () => {
  const t = useTime(),
    selection = window(t, 0.6, 4.3),
    lift = s(t, 4.4, 7.6) * (1 - s(t, 18.5, 20)),
    off = window(t, 8.2, 9.6),
    zoom = s(t, 10, 12) * (1 - s(t, 16.5, 19.7)),
    paint = s(t, 13.8, 16);
  const detailScale = mix(1, 4, s(t, 12.1, 13.2)),
    pixel = 1.25 * detailScale;
  const point = getPointAtLength(brushPath, brushLength * paint);
  return (
    <>
      <Bar mode="Masks / pixel editing" />
      <Title
        t={t}
        b={4.4}
        lines={["Precision,", "where it matters."]}
        detail="Select the subject. Refine the edge."
      />
      <Title
        t={t}
        a={4.5}
        b={10}
        lines={["Light the subject.", "Keep the sky."]}
        detail="The mask decides exactly what changes."
        size={96}
      />
      <Title
        t={t}
        a={10.1}
        b={20.3}
        lines={["Right down", "to the pixel."]}
        detail="Zoom in. Paint. Refine. Stay in control."
      />
      <div
        style={{
          ...A,
          left: 0,
          top: 470,
          width: 1080,
          height: 1200,
          overflow: "hidden",
        }}
      >
        <div style={{ width: "100%", height: "100%", opacity: 1 - zoom }}>
          <PixelPhoto amount={lift * (1 - off)} />
          <Photo
            src="studio/selection.png"
            style={{ ...A, inset: 0, opacity: selection * 0.75 }}
          />
        </div>
        <div style={{ ...A, inset: 0, opacity: zoom, overflow: "hidden" }}>
          <Photo
            src="studio/color/native-detail.png"
            style={{
              scale: detailScale,
              imageRendering: "pixelated",
            }}
          />
          <svg
            viewBox="0 0 1080 1200"
            width="100%"
            height="100%"
            style={{ ...A, inset: 0, opacity: 1 }}
          >
            <defs>
              <pattern
                id="pixel-guides"
                width={pixel}
                height={pixel}
                x={540 - 600 * detailScale}
                y={600 - 600 * detailScale}
                patternUnits="userSpaceOnUse"
              >
                <path
                  d={`M${pixel} 0H0V${pixel}`}
                  stroke="#dce8e849"
                  fill="none"
                  strokeWidth={1}
                />
              </pattern>
            </defs>
            <rect
              width={1080}
              height={1200}
              fill="url(#pixel-guides)"
              opacity={window(t, 13.2, 16.8)}
            />
            <defs>
              <mask id="paint-reveal">
                <path
                  d={brushPath}
                  fill="none"
                  stroke="white"
                  strokeWidth={3 * pixel}
                  strokeLinecap="round"
                  pathLength={1}
                  strokeDasharray={1}
                  strokeDashoffset={1 - paint}
                />
              </mask>
            </defs>
            <image
              href={staticFile("media/studio/color/native-detail-lift.png")}
              x={-540 * (detailScale - 1)}
              y={-600 * (detailScale - 1)}
              width={1080 * detailScale}
              height={1200 * detailScale}
              preserveAspectRatio="xMidYMid slice"
              mask="url(#paint-reveal)"
              style={{ imageRendering: "pixelated" }}
            />
            <path
              d={brushPath}
              fill="none"
              stroke="#83dcda50"
              opacity={window(t, 13.7, 16.4)}
              strokeWidth={3 * pixel}
              strokeLinecap="round"
              pathLength={1}
              strokeDasharray={1}
              strokeDashoffset={1 - paint}
            />
            <circle
              cx={point?.x ?? 420}
              cy={point?.y ?? 810}
              r={(3 * pixel) / 2 + 5}
              opacity={window(t, 13.7, 16.4)}
              fill="none"
              stroke="white"
              strokeWidth={2}
            />
          </svg>
        </div>
      </div>
      <Panel
        style={{
          ...A,
          left: 80,
          top: 1440,
          width: 920,
          padding: 30,
          background: "#10171aee",
        }}
      >
        <Tag style={{ marginBottom: 16 }}>
          {zoom > 0.5
            ? "PIXEL BRUSH / MASK REFINEMENT"
            : "SUBJECT MASK / EDITABLE ADJUSTMENT"}
        </Tag>
        <Control
          label={zoom > 0.5 ? "Brush size" : "Exposure"}
          value={zoom > 0.5 ? 3 : 0.55 * lift * (1 - off)}
          text={
            zoom > 0.5 ? "3 px" : `+${(0.55 * lift * (1 - off)).toFixed(2)} EV`
          }
          min={zoom > 0.5 ? 1 : -5}
          max={zoom > 0.5 ? 100 : 5}
          cursor={t > 4.4 && t < 7.6}
        />
        <div
          style={{
            display: "flex",
            gap: 22,
            fontSize: 29,
            color: muted,
            marginTop: 12,
            alignItems: "center",
          }}
        >
          <Icon name="selection" size={28} />
          {zoom > 0.5
            ? "Original-size source pixels. Editable brush strokes."
            : off > 0.5
              ? "Adjustment hidden / original retained"
              : "Subject selected / background protected"}
        </div>
      </Panel>
      <Caption>Original pixels. Editable masks. Precise control.</Caption>
    </>
  );
};
