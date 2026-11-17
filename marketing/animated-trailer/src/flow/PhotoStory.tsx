import { getLength, getPointAtLength } from "@remotion/paths";
import { useStoryTime } from "./Timing";
import { staticFile } from "remotion";
import {
  AppActivity,
  AppIcon,
  AppInspectorSlider,
  AppWordmark,
} from "../app-kit/AppElements";
import {
  A,
  C,
  Chrome,
  Head,
  Label,
  Layers,
  Light,
  mix,
  Note,
  Picture,
  q,
  show,
  Surface,
  softness,
  out,
} from "./Motion";

const library = [
  "IMG_5071.jpg",
  "IMG_5106-developed.jpg",
  "IMG_4969.jpg",
  "campaign/wedding-bride-02.jpg",
  "IMG_4856.jpg",
  "IMG_4870.jpg",
];
const Library = ({
  t = 0,
  omitHero = false,
}: {
  t?: number;
  omitHero?: boolean;
}) => (
  <>
    {library.map((src, i) => (
      <div
        key={src}
        style={{
          visibility: omitHero && i === 0 ? "hidden" : "visible",
          ...A,
          left: 80 + (i % 3) * 318,
          top: 650 + Math.floor(i / 3) * 470,
          width: 284,
          height: 426,
          borderRadius: 5,
          overflow: "hidden",
          translate: `0 ${mix(45, 0, q(t, 0.1 * i, 0.8 + 0.1 * i))}px`,
        }}
      >
        <Picture src={src} />
        <div
          style={{
            ...A,
            bottom: 14,
            left: 14,
            fontSize: 22,
            color: "white",
            textShadow: "0 1px 8px black",
          }}
        >
          ★★★★★
        </div>
      </div>
    ))}
  </>
);

export const Opening = () => {
  const t = useStoryTime(),
    gather = q(t, 10.4, 14),
    markGather = q(t, 10.2, 11.6),
    leave = q(t, 9.7, 10.7);
  return (
    <>
      <Light t={t} draw={q(t, 0, 2.7)} amount={0.95 * (1 - gather)} y={1000} />
      <div
        style={{
          ...A,
          left: mix(80, 80, gather),
          top: mix(240, 80, markGather),
          scale: mix(1, 130 / 720, markGather),
          transformOrigin: "0 0",
          opacity: q(t, 0, 0.9),
        }}
      >
        <AppWordmark width={720} />
      </div>
      <Label
        style={{
          ...A,
          left: 85,
          top: 172,
          opacity: q(t, 0.1, 0.8) * (1 - leave),
        }}
      >
        INTRODUCING
      </Label>
      <div style={{ ...A, left: 80, top: 545, opacity: 1 - leave }}>
        <div
          style={{
            fontSize: 100,
            fontWeight: 600,
            letterSpacing: -5.5,
            lineHeight: 1.01,
            translate: `0 ${mix(60, 0, q(t, 0.8, 2))}px`,
            opacity: q(t, 0.8, 1.6),
          }}
        >
          Photo and video
          <br />
          editing. Made easier.
        </div>
        <div
          style={{
            marginTop: 42,
            fontSize: 43,
            color: C.textMuted,
            lineHeight: 1.3,
            opacity: q(t, 2.4, 3.4),
          }}
        >
          Built for professionals
          <br />
          and hobbyists alike.
        </div>
      </div>
      {library.slice(0, 3).map((src, i) => {
        const x = [50, 390, 745][i],
          y = [1120, 1030, 1160][i],
          width = [350, 350, 350][i];
        return (
          <div
            key={src}
            style={{
              ...A,
              left: mix(x, 80 + i * 318, gather),
              top: mix(y, 650, gather),
              width: mix(width, 284, gather),
              height: mix(i === 1 ? 525 : 480, 426, gather),
              borderRadius: mix(28, 5, gather),
              overflow: "hidden",
              rotate: `${mix([-5, 3, -2][i], 0, gather)}deg`,
              transform: `perspective(1800px) rotateY(${[5, -4, 4][i] * (1 - gather) * Math.sin(Math.PI * q(t, 3.6 + i * 0.2, 6.2 + i * 0.2))}deg)`,
              filter: `blur(${softness(t, 3.6 + i * 0.2, 6.2 + i * 0.2, 260)}px)`,
              translate: `0 ${mix(260, 0, out(t, 3.6 + i * 0.2, 5 + i * 0.2))}px`,
              opacity: q(t, 3.4 + i * 0.2, 4.8 + i * 0.2),
              boxShadow: "0 30px 80px #0009",
            }}
          >
            <Picture src={src} />
          </div>
        );
      })}
      <div style={{ opacity: gather }}>
        {library.slice(3).map((src, i) => (
          <div
            key={src}
            style={{
              ...A,
              left: 80 + i * 318,
              top: 1120 + 500 * (1 - q(t, 12.5, 14)),
              width: 284,
              height: 426,
              borderRadius: 5,
              overflow: "hidden",
              opacity: q(t, 12.5, 13.3),
              translate: `0 ${80 * (1 - gather)}px`,
            }}
          >
            <Picture src={src} />
          </div>
        ))}
      </div>
      <div style={{ ...A, inset: 0, opacity: gather }}>
        <Head
          t={t}
          a={11.2}
          b={20}
          lines={["Bring the whole", "shoot together."]}
          size={102}
        />
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1740,
          fontSize: 36,
          color: C.textMuted,
          opacity: q(t, 13.3, 13.95),
        }}
      >
        Batch import · Rate · Cull · Organize
      </div>
    </>
  );
};

export const Photo = () => {
  const t = useStoryTime(),
    open = q(t, 1.3, 3.4),
    batch = q(t, 16, 17.5),
    exit = q(t, 22, 24);
  const exposure = q(t, 4.2, 5.7),
    highlights = q(t, 6, 7.5),
    warm = q(t, 8, 9.5),
    curve = q(t, 10, 11.5);
  const detail = q(t, 14, 14.7) * (1 - q(t, 15.6, 16.1));
  const hsl = show(t, 11.9, 14);
  return (
    <>
      <Chrome mode="Photos" />
      <Head
        t={t}
        a={-1.2}
        b={2}
        lines={["Bring the whole", "shoot together."]}
        size={102}
      />
      <Surface
        style={{
          ...A,
          left: 80,
          top: 1650,
          width: 920,
          padding: 25,
          opacity: 1 - q(t, 1.4, 2.1),
          display: "flex",
          alignItems: "center",
          gap: 22,
          fontSize: 32,
        }}
      >
        <AppActivity size={38} progress={q(t, -1, 0.9)} active={t < 0.9} />
        Batch import / Original files kept local
      </Surface>
      <Head
        t={t}
        a={2.1}
        b={11.8}
        lines={["RAW photography.", "Your color, precisely."]}
        size={94}
        detail="A custom color engine. Full creative control."
      />
      <Head
        t={t}
        a={11.9}
        b={14}
        lines={["Shape every color."]}
        detail="HSL / Hue · Saturation · Luminance"
        size={100}
      />
      <Head
        t={t}
        a={14}
        b={16.2}
        lines={["Right down", "to the pixels."]}
        detail="Native-resolution detail. No shortcuts."
      />
      <Head
        t={t}
        a={16.4}
        b={22.2}
        lines={["One edit.", "A consistent gallery."]}
        size={98}
      />
      <div
        style={{
          ...A,
          inset: 0,
          translate: `${-1080 * exit}px ${-45 * Math.sin(Math.PI * exit)}px`,
          filter: `blur(${softness(t, 22, 24)}px)`,
        }}
      >
        <div style={{ opacity: (1 - open) * (1 - batch) }}>
          <Library t={2} omitHero />
        </div>
        <div
          style={{
            ...A,
            left: 80,
            top: mix(650, 495, open),
            width: mix(284, 920, open),
            height: mix(426, 920, open),
            borderRadius: mix(5, 18, open),
            overflow: "hidden",
            opacity: 1,
            translate: `${-1080 * batch}px 0`,
          }}
        >
          <div
            style={{
              width: "100%",
              height: "100%",
              scale: mix(1, 2.7, detail),
              transformOrigin: "52% 41%",
              filter: `brightness(${1 + 0.28 * exposure}) contrast(${1 + 0.1 * curve}) saturate(${1 + 0.14 * warm})`,
            }}
          >
            <Picture src="IMG_5071.jpg" />
          </div>
          <Label
            style={{
              ...A,
              left: 25,
              top: 24,
              background: "#080808aa",
              color: "white",
              padding: "9px 14px",
              opacity: open,
            }}
          >
            {detail > 0.5 ? "DETAIL / 1:1" : "DEVELOP / sRGB"}
          </Label>
        </div>
        <Surface
          style={{
            ...A,
            left: 80,
            top: 1445,
            width: 920,
            padding: "18px 22px",
            opacity: open,
            translate: `${-1080 * batch}px ${60 * (1 - open)}px`,
          }}
        >
          <Label style={{ marginBottom: 8 }}>LOOKSY COLOR ENGINE</Label>
          <AppInspectorSlider
            label={detail > 0.5 ? "Sharpening" : "Exposure"}
            value={detail > 0.5 ? 28 : 0.32 * exposure}
            valueText={
              detail > 0.5 ? "28" : `+${(0.32 * exposure).toFixed(2)} EV`
            }
            from={detail > 0.5 ? 0 : -5}
            to={detail > 0.5 ? 100 : 5}
            scale={2}
            width={438}
            press={t > 4.2 && t < 5.7 ? 1 : 0}
            showCursor={t > 4.2 && t < 5.7}
          />
          <AppInspectorSlider
            label={detail > 0.5 ? "Noise" : "Highlights"}
            value={detail > 0.5 ? 12 : -24 * highlights}
            valueText={detail > 0.5 ? "12" : `${Math.round(-24 * highlights)}`}
            scale={2}
            width={438}
            showCursor={t > 6 && t < 7.5}
          />
          <AppInspectorSlider
            label="Temperature"
            value={8 * warm}
            valueText={`+${Math.round(8 * warm)}`}
            scale={2}
            width={438}
            trackColors={["#659fee", "#ddd", "#efb47f"]}
            showCursor={t > 8 && t < 9.5}
          />
        </Surface>
        <Surface
          style={{
            ...A,
            left: 640,
            top: 1040,
            width: 325,
            height: 350,
            padding: 20,
            opacity: show(t, 9.7, 12.2),
            scale: mix(0.86, 1, q(t, 9.7, 10.5)),
            transformOrigin: "100% 100%",
          }}
        >
          <Label>TONE CURVE / RGB</Label>
          <svg width={280} height={260} style={{ marginTop: 20 }}>
            <path
              d="M0 0V260H280 M70 0V260 M140 0V260 M210 0V260 M0 65H280 M0 130H280 M0 195H280"
              stroke="#333"
              fill="none"
            />
            <path
              d={`M0 260 C70 ${205 + curve * 25} 105 ${190 + curve * 12} 140 140 S230 ${60 - 20 * curve} 280 0`}
              stroke="white"
              strokeWidth={2.5}
              fill="none"
            />
            <circle cx={140} cy={140} r={6} fill="white" />
          </svg>
        </Surface>
        <Surface
          style={{
            ...A,
            left: 490,
            top: 1020,
            width: 475,
            padding: 25,
            opacity: hsl,
            translate: `0 ${25 * (1 - hsl)}px`,
          }}
        >
          <Label>COLOR MIX / ORANGE</Label>
          <div style={{ display: "flex", gap: 18, margin: "20px 0" }}>
            {[
              "#ed6465",
              "#eda65d",
              "#e9cf61",
              "#71b48d",
              "#6cbbd2",
              "#738bcb",
              "#a586ce",
            ].map((c, i) => (
              <div
                key={c}
                style={{
                  background: c,
                  width: 34,
                  height: 34,
                  borderRadius: 30,
                  outline: i === 1 ? "2px solid white" : "none",
                  outlineOffset: 5,
                }}
              />
            ))}
          </div>
          <AppInspectorSlider
            label="Saturation"
            value={14 * q(t, 12.2, 13.1)}
            valueText={`+${Math.round(14 * q(t, 12.2, 13.1))}`}
            width={210}
            scale={2}
            showCursor={t > 12.2 && t < 13.1}
          />
          <AppInspectorSlider
            label="Luminance"
            value={8 * q(t, 12.8, 13.5)}
            valueText={`+${Math.round(8 * q(t, 12.8, 13.5))}`}
            width={210}
            scale={2}
          />
        </Surface>
        <Light
          t={t + 14}
          curve={q(t, 9.5, 10.4)}
          amount={show(t, 9, 10.7) * 0.6}
          tone={curve}
        />
        <div style={{ ...A, inset: 0, translate: `${1080 * (1 - batch)}px 0` }}>
          {["02", "01", "03"].map((n, i) => (
            <div
              key={n}
              style={{
                ...A,
                left: 80 + i * 318,
                top: 560 + 45 * (1 - batch),
                width: 284,
                height: 700,
                overflow: "hidden",
                borderRadius: 12,
                filter: `brightness(${1 + 0.09 * q(t, 18 + i * 0.25, 19.3 + i * 0.25)})`,
              }}
            >
              <Picture src={`campaign/wedding-bride-${n}.jpg`} />
              <div
                style={{
                  ...A,
                  right: 18,
                  bottom: 18,
                  opacity: q(t, 19 + i * 0.25, 19.6 + i * 0.25),
                }}
              >
                <AppIcon name="check" size={32} />
              </div>
            </div>
          ))}
          <Surface
            style={{ ...A, left: 80, top: 1320, width: 920, padding: 30 }}
          >
            <Label>SYNC ADJUSTMENTS / 3 SELECTED</Label>
            <div
              style={{
                display: "flex",
                gap: 24,
                alignItems: "center",
                marginTop: 25,
                fontSize: 34,
              }}
            >
              <AppActivity
                size={42}
                progress={q(t, 18, 20.3)}
                active={t < 20.3}
              />
              Exposure · Color · Curves · Detail
            </div>
          </Surface>
          <Note style={{ top: 1640 }}>
            Portrait sessions. Wedding galleries.
            <br />
            Your own everyday photography.
          </Note>
        </div>
      </div>
      <div
        style={{
          ...A,
          left: mix(1160, 80, exit),
          top: 480,
          width: 920,
          height: 1120,
          borderRadius: 18,
          overflow: "hidden",
          opacity: exit,
        }}
      >
        <Picture src="refine/surf.jpg" />
      </div>
    </>
  );
};

export const Retouch = () => {
  const t = useStoryTime(),
    zoom = q(t, 0.7, 1.9) * (1 - q(t, 5.7, 6.7)),
    fill = q(t, 3.5, 4.3),
    next = q(t, 6.7, 8);
  return (
    <>
      <Chrome mode="Retouch" />
      <Head
        t={t}
        b={6.6}
        lines={["Remove distractions.", "Keep the moment."]}
        size={94}
      />
      <div
        style={{
          ...A,
          left: 80 - next * 1080,
          top: 480,
          width: 920,
          height: 1120,
          borderRadius: 18,
          overflow: "hidden",
        }}
      >
        <div
          style={{
            width: "100%",
            height: "100%",
            scale: mix(1, 2.1, zoom),
            transformOrigin: "54% 56%",
          }}
        >
          <Picture src="refine/surf.jpg" />
          <Picture
            src="refine/surf-final.png"
            style={{ ...A, inset: 0, opacity: fill }}
          />
          <svg
            viewBox="0 0 1800 2700"
            preserveAspectRatio="xMidYMid slice"
            width="100%"
            height="100%"
            style={{ ...A, inset: 0, opacity: show(t, 1.9, 3.6) }}
          >
            <path
              d="M976 1418L982 1510L974 1645"
              fill="none"
              stroke="#8bcef2"
              strokeWidth={115}
              strokeLinecap="round"
              strokeLinejoin="round"
              pathLength={1}
              strokeDasharray={1}
              strokeDashoffset={1 - q(t, 2, 3.1)}
              opacity={0.65}
            />
          </svg>
        </div>
      </div>
      <Surface
        style={{
          ...A,
          left: 80,
          top: 1640,
          width: 920,
          padding: 28,
          opacity: q(t, 0.1, 0.6) * (1 - next),
          translate: `0 ${18 * (1 - q(t, 0.1, 0.6))}px`,
        }}
      >
        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: 22,
            fontSize: 33,
          }}
        >
          <AppIcon name="brush" size={34} />
          Retouch · Remove object
          <span style={{ marginLeft: "auto", color: C.textMuted }}>Undo</span>
        </div>
      </Surface>
      <div
        style={{
          ...A,
          left: mix(1160, 80, next),
          top: 480,
          width: 920,
          height: 1120,
          overflow: "hidden",
          borderRadius: 18,
          opacity: next,
        }}
      >
        <Picture src="IMG_5106-developed.jpg" />
      </div>
    </>
  );
};

export const Poster = ({ t = 10 }: { t?: number }) => (
  <div
    style={{
      width: "100%",
      height: "100%",
      position: "relative",
      containerType: "inline-size",
      overflow: "hidden",
    }}
  >
    <Picture src="IMG_5106-developed.jpg" />
    <div
      style={{
        ...A,
        left: "5%",
        right: "5%",
        top: "29%",
        fontSize: "15.55cqw",
        fontWeight: 700,
        lineHeight: 0.87,
        letterSpacing: "-0.85cqw",
        color: "#fff9e9",
        translate: `0 ${mix(130, 0, q(t, 2.4, 4))}px`,
        opacity: q(t, 2.4, 3.3),
      }}
    >
      GO
      <br />
      FURTHER.
    </div>
    <Picture
      src="IMG_5106-developed-subject.png"
      style={{ ...A, inset: 0, opacity: q(t, 4.2, 5.4) }}
    />
    <div
      style={{
        ...A,
        left: "6%",
        bottom: "6%",
        fontSize: "2.83cqw",
        letterSpacing: ".33cqw",
        color: "#fff9e9",
        opacity: q(t, 5.4, 6.5),
      }}
    >
      NEPAL / FIELD NOTES
    </div>
  </div>
);

const paintPath = "M90 745Q310 655 520 750T830 730";
const paintLength = getLength(paintPath);
const paintPoint = (u: number) =>
  getPointAtLength(paintPath, Math.min(1, Math.max(0, u)) * paintLength) ?? {
    x: 830,
    y: 730,
  };
export const Design = () => {
  const t = useStoryTime(),
    compose = q(t, 0.4, 1.9),
    textMask = q(t, 8.8, 10),
    exit = q(t, 14, 16);
  return (
    <>
      <Chrome mode="Canvas & layers" />
      <Head
        t={t}
        b={8.4}
        lines={["Beyond the photo.", "Into your next design."]}
        size={94}
        detail="Masks. Type. Paint. Editable layers."
      />
      <Head
        t={t}
        a={8.6}
        b={14.3}
        lines={["Put your images", "into your ideas."]}
        size={100}
        detail="Image-filled type. Brushwork. Effects."
      />
      <div
        style={{
          ...A,
          left: 80,
          top: mix(480, 510, compose),
          width: 920,
          height: mix(1120, 1020, compose) - 407 * exit,
          overflow: "hidden",
          borderRadius: 18 * (1 - exit),
          scale: mix(1, 0.84, q(t, 6.1, 7.3)) + 0.16 * q(t, 13.5, 14.2),
          transformOrigin: "0 0",
        }}
      >
        <div
          style={{
            ...A,
            inset: 0,
            translate: `${-1040 * textMask}px ${-45 * Math.sin(Math.PI * textMask)}px`,
            rotate: `${-2.4 * Math.sin(Math.PI * textMask)}deg`,
            filter: `blur(${softness(t, 8.8, 10)}px)`,
          }}
        >
          <Poster t={t} />
        </div>
        <div
          style={{
            ...A,
            inset: 0,
            background: "#ece8dd",
            translate: `${1040 * (1 - textMask)}px ${45 * Math.sin(Math.PI * textMask)}px`,
            rotate: `${2.4 * Math.sin(Math.PI * textMask)}deg`,
            filter: `blur(${softness(t, 8.8, 10)}px)`,
          }}
        >
          <div
            style={{
              ...A,
              left: 60,
              top: 70,
              fontSize: 25,
              color: "#343434",
              letterSpacing: 3,
            }}
          >
            NOTES FROM THE HIMALAYAS
          </div>
          <svg width={920} height={1020} style={{ ...A, inset: 0 }}>
            <defs>
              <pattern
                id="coast-text-fill"
                patternUnits="userSpaceOnUse"
                width={1200}
                height={1020}
              >
                <image
                  href={staticFile("media/IMG_4870.jpg")}
                  width={1200}
                  height={800}
                  x={-40 * q(t, 10, 11.5)}
                  y={110 - 25 * q(t, 10, 11.5)}
                  preserveAspectRatio="xMidYMid slice"
                />
              </pattern>
            </defs>
            <text
              x={55}
              y={380}
              fill="url(#coast-text-fill)"
              fontFamily="Helvetica Neue,Arial,sans-serif"
              fontSize={185}
              fontWeight={800}
              letterSpacing={-10}
            >
              FIELD
              <tspan x={55} dy={175}>
                NOTES.
              </tspan>
            </text>
          </svg>
          <svg width={920} height={1020} style={{ ...A, inset: 0 }}>
            <path
              d={paintPath}
              fill="none"
              stroke="#c85130"
              strokeWidth={12}
              strokeLinecap="round"
              pathLength={1}
              strokeDasharray={1}
              strokeDashoffset={1 - q(t, 11.8, 13.2)}
            />
            <g opacity={show(t, 11.7, 13.5)}>
              <circle
                cx={paintPoint(q(t, 11.8, 13.2)).x}
                cy={paintPoint(q(t, 11.8, 13.2)).y}
                r={17}
                fill="none"
                stroke="#fff"
                strokeWidth={2}
              />
              <circle
                cx={paintPoint(q(t, 11.8, 13.2)).x}
                cy={paintPoint(q(t, 11.8, 13.2)).y}
                r={5}
                fill="#c85130"
              />
            </g>
          </svg>
        </div>
        <Picture
          src="IMG_5071.jpg"
          style={{
            ...A,
            inset: 0,
            opacity: 1,
            clipPath: `inset(0 ${100 * (1 - exit)}% 0 0 round ${80 * Math.sin(Math.PI * exit)}px)`,
          }}
        />
      </div>
      <div
        style={{
          ...A,
          left: 650,
          top: 1160,
          width: 350,
          opacity: q(t, 6.5, 6.85) * (1 - q(t, 8.1, 8.4)),
          translate: `${50 * (1 - q(t, 6.1, 7))}px 0`,
        }}
      >
        <Layers title="GO FURTHER." />
      </div>
      <Surface
        style={{
          ...A,
          left: 80,
          top: 1560,
          width: 920,
          padding: 26,
          opacity: show(t, 1.2, 14),
        }}
      >
        <div
          style={{
            display: "flex",
            gap: 30,
            fontSize: 30,
            alignItems: "center",
          }}
        >
          <AppIcon name={textMask > 0.5 ? "text" : "selection"} size={32} />
          {textMask > 0.5
            ? "Text mask / Source image"
            : t > 5
              ? "Layers / Original · Type · Subject"
              : "Subject mask / Refine edge"}
        </div>
        <AppInspectorSlider
          label={textMask > 0.5 ? "Position X" : "Feather"}
          value={textMask > 0.5 ? -20 * q(t, 10, 11.5) : 1.4 * q(t, 4.4, 5.4)}
          valueText={
            textMask > 0.5 ? `${Math.round(-20 * q(t, 10, 11.5))} px` : "1.4 px"
          }
          scale={2}
          width={438}
          showCursor={t > 10 && t < 11.5}
        />
      </Surface>
      <Light
        t={t + 39}
        y={1010}
        amount={0.7 * q(t, 14, 16)}
        draw={q(t, 14, 15.4)}
      />
    </>
  );
};
