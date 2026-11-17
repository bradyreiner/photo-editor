import {
  AbsoluteFill,
  CanvasImage,
  Sequence,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { Audio, Video } from "@remotion/media";
import { AppWordmark } from "../app-kit/AppElements";
import { font } from "../rebuild/Type";
import {
  A,
  Cursor,
  ink,
  lime,
  mix,
  out,
  paper,
  pop,
  smooth,
} from "../rebuild/Kit";
import { ExpeditionPoster, NightThumbnail } from "../rebuild/Artwork";
import { PanoramaArtwork } from "./Panorama";
import { DesktopLight } from "./Light";

const media = (p: string) => staticFile(`media/${p}`);
const useShot = () => {
  const { fps, width, height } = useVideoConfig();
  return {
    t: useCurrentFrame() / fps,
    fps,
    width,
    height,
    portrait: height > width,
  };
};

const Caption = ({
  chapter,
  title,
  detail,
  serif = false,
  enterAt = 0,
}: {
  chapter: string;
  title: string;
  detail: string;
  serif?: boolean;
  enterAt?: number;
}) => {
  const { t: clock, portrait } = useShot();
  const t = clock - enterAt;
  const lines = title.replace(/\\n/g, "\n");
  return (
    <div
      style={{
        ...A,
        left: portrait ? 68 : 88,
        right: portrait ? 68 : 88,
        top: portrait ? 160 : 54,
        color: paper,
      }}
    >
      <div
        style={{
          fontFamily: font.mono,
          fontSize: portrait ? 24 : 21,
          letterSpacing: 2,
          color: lime,
          opacity: out(t, 0, 0.3),
        }}
      >
        {chapter}
      </div>
      <div style={{ overflow: "hidden", marginTop: portrait ? 28 : 15 }}>
        <div
          style={{
            fontFamily: serif ? font.serif : font.sans,
            fontStyle: serif ? "italic" : "normal",
            fontWeight: serif ? 400 : 500,
            letterSpacing: serif ? -1 : -3,
            fontSize: portrait ? 91 : 76,
            lineHeight: 1.02,
            translate: `0 ${110 * (1 - out(t, 0.08, 0.7))}px`,
            whiteSpace: portrait ? "pre-line" : "normal",
          }}
        >
          {portrait ? lines : lines.split("\n").join(" ")}
        </div>
      </div>
      <div
        style={{
          fontFamily: font.sans,
          fontSize: portrait ? 32 : 27,
          marginTop: portrait ? 26 : 15,
          color: "#c0c8d3",
          opacity: out(t, 0.35, 0.85),
          translate: `0 ${14 * (1 - out(t, 0.35, 0.85))}px`,
        }}
      >
        {detail}
      </div>
    </div>
  );
};

type PointerKey = { at: number; x: number; y: number; down?: boolean };
const RecordedPointer = ({ keys }: { keys: PointerKey[] }) => {
  const { t } = useShot();
  let a = keys[0],
    b = a;
  for (let i = 1; i < keys.length; i++) {
    b = keys[i];
    if (t <= b.at) break;
    a = b;
  }
  const p = smooth(t, a.at, b.at === a.at ? a.at + 0.001 : b.at);
  return (
    <Cursor x={mix(a.x, b.x, p)} y={mix(a.y, b.y, p)} down={Boolean(a.down)} />
  );
};

// All controls and image pixels below are native recordings. Only the camera,
// editorial pointer emphasis, titles and the surrounding light are authored.
// Portrait uses two synchronized crops of the SAME recording so its control
// and photographic result remain readable together. No fake slider values.
const NativeSurface = ({
  file,
  trim = 0,
  still = false,
  focus = 0.6,
  pointer,
  panelY = 470,
  panelH = 850,
  imageCrop,
  clockOffset = 0,
}: {
  file: string;
  trim?: number;
  still?: boolean;
  focus?: number;
  pointer?: PointerKey[];
  panelY?: number;
  panelH?: number;
  imageCrop?: [number, number, number, number];
  clockOffset?: number;
}) => {
  const { t, fps, portrait } = useShot();
  const source = () =>
    still ? (
      <CanvasImage src={media(file)} style={{ width: 2570, height: 1504 }} />
    ) : (
      <Video
        muted
        src={media(file)}
        trimBefore={trim * fps}
        style={{ width: 2570, height: 1504 }}
      />
    );
  const content = () => (
    <>
      {source()}
      {pointer ? <RecordedPointer keys={pointer} /> : null}
    </>
  );
  const clock = t + clockOffset;
  const arrive = out(clock, 0, 0.8);
  if (portrait) {
    const c = imageCrop ?? [116, 280, 1465, 1015];
    const scale = Math.min(944 / c[2], 670 / c[3]);
    const ps = Math.min(944 / 960, 660 / panelH);
    return (
      <>
        <div
          style={{
            ...A,
            left: 68,
            top: 540,
            width: 944,
            height: 670,
            overflow: "hidden",
            borderRadius: 18,
            background: "#050609",
            translate: `${100 * (1 - arrive)}px 0`,
            opacity: arrive,
            border: "1px solid #ffffff20",
          }}
        >
          <div
            style={{
              ...A,
              left: (944 - c[2] * scale) / 2 - c[0] * scale,
              top: (670 - c[3] * scale) / 2 - c[1] * scale,
              scale,
              transformOrigin: "0 0",
              width: 2570,
              height: 1504,
            }}
          >
            {source()}
          </div>
        </div>
        <div
          style={{
            ...A,
            left: 68,
            top: 1240,
            width: 944,
            height: 540,
            overflow: "hidden",
            borderRadius: 18,
            background: "#171717",
            translate: `0 ${90 * (1 - arrive)}px`,
            opacity: arrive,
            border: "1px solid #ffffff20",
          }}
        >
          <div
            style={{
              ...A,
              left: -1598 * ps,
              top: -panelY * ps,
              scale: ps,
              transformOrigin: "0 0",
              width: 2570,
              height: 1504,
            }}
          >
            {content()}
          </div>
        </div>
        <div
          style={{
            ...A,
            left: 74,
            bottom: 78,
            fontFamily: font.mono,
            fontSize: 18,
            letterSpacing: 1.2,
            color: "#9caec0",
          }}
        >
          LOOKSY DESKTOP / NATIVE APP CAPTURE
        </div>
      </>
    );
  }
  const zoom =
    1 + 0.12 * smooth(clock, 1.4, 3.2) - 0.08 * smooth(clock, 7.5, 9.3);
  const w = 1720,
    h = 775,
    fit = w / 2570;
  return (
    <div
      style={{
        ...A,
        left: 100,
        top: 268,
        width: w,
        height: h,
        overflow: "hidden",
        borderRadius: 22,
        background: "#080a0d",
        border: "1px solid #ffffff22",
        opacity: arrive,
        translate: `0 ${100 * (1 - arrive)}px`,
        boxShadow: "0 35px 100px #0009",
      }}
    >
      <div
        style={{
          ...A,
          left: -(zoom - 1) * 2570 * fit * focus,
          top: -30 - 145 * smooth(clock, 1.4, 3.2),
          width: 2570,
          height: 1504,
          scale: fit * zoom,
          transformOrigin: "0 0",
        }}
      >
        {content()}
      </div>
    </div>
  );
};

const Introduction = () => {
  const { t, portrait, width, height } = useShot();
  const leave = smooth(t, 4.6, 6);
  return (
    <>
      <DesktopLight t={t} gather={smooth(t, 0.9, 3.4)} />
      <div
        style={{
          ...A,
          inset: 0,
          background: "radial-gradient(ellipse,transparent,#0b0d1250)",
          opacity: 1 - leave,
        }}
      />
      <div
        style={{
          ...A,
          left: portrait ? 75 : 110,
          top: portrait ? 390 : 172,
          opacity: 1 - leave,
          translate: `${-width * leave}px 0`,
        }}
      >
        <div
          style={{
            fontFamily: font.mono,
            fontSize: portrait ? 25 : 22,
            letterSpacing: 3,
            color: paper,
            opacity: out(t, 0.05, 0.6),
          }}
        >
          INTRODUCING
        </div>
        <div
          style={{
            width: portrait ? 825 : 970,
            marginTop: 35,
            translate: `0 ${120 * (1 - pop(t, 0.15))}px`,
          }}
        >
          <AppWordmark width={portrait ? 825 : 970} />
        </div>
        <div
          style={{
            fontFamily: font.sans,
            fontSize: portrait ? 75 : 70,
            fontWeight: 500,
            letterSpacing: -3,
            lineHeight: 1.08,
            marginTop: portrait ? 85 : 55,
            color: paper,
            translate: `0 ${70 * (1 - out(t, 0.5, 1.3))}px`,
            opacity: out(t, 0.5, 1.3),
          }}
        >
          Photo. Video.
          <br />A whole desktop studio.
        </div>
        <div
          style={{
            fontFamily: font.serif,
            fontStyle: "italic",
            fontSize: portrait ? 55 : 47,
            color: lime,
            marginTop: 26,
            opacity: out(t, 1.3, 2.2),
          }}
        >
          For the work. And the joy of making it.
        </div>
      </div>
      <div
        style={{
          ...A,
          width,
          height,
          top: height * (1 - leave),
          background: ink,
          borderRadius: `${160 * (1 - leave)}px ${160 * (1 - leave)}px 0 0`,
        }}
      />
    </>
  );
};

const RawScene = () => {
  const { t, fps } = useShot();
  return (
    <>
      <Caption
        enterAt={t < 6 ? 0 : 6}
        chapter="01 / THE PHOTO EDITOR"
        title={t < 6 ? "Start with\nthe RAW." : "A custom\ncolor engine."}
        detail={
          t < 6
            ? "Camera originals. Native resolution."
            : "Exposure, color and detail. Built from the ground up."
        }
      />
      <Sequence durationInFrames={7 * fps} name="Native RAW exposure drag">
        <NativeSurface
          file="desktop-launch/raw-native.mp4"
          trim={18}
          focus={0.77}
          panelY={500}
          pointer={[
            { at: 0, x: 1750, y: 740 },
            { at: 1, x: 2030, y: 725 },
            { at: 1.5, x: 2030, y: 725, down: true },
            { at: 2.5, x: 2107, y: 725 },
            { at: 6, x: 2200, y: 790 },
          ]}
        />
      </Sequence>
      <Sequence
        from={7 * fps}
        durationInFrames={5 * fps}
        name="Native RAW developed response / wait trimmed"
      >
        <NativeSurface
          file="desktop-launch/raw-native.mp4"
          trim={29}
          clockOffset={7}
          focus={0.35}
          panelY={500}
        />
      </Sequence>
    </>
  );
};

const CurveScene = () => (
  <>
    <Caption
      chapter="01 / THE PHOTO EDITOR"
      title="Shape the light.\nKeep the nuance."
      detail="RGB and luma curves. Individual color channels."
    />
    <NativeSurface
      file="desktop-action/tone-curve.mp4"
      trim={12}
      focus={0.84}
      panelY={570}
      panelH={710}
      pointer={[
        { at: 0, x: 1750, y: 945 },
        { at: 2.5, x: 1872, y: 1042 },
        { at: 3, x: 1872, y: 1042, down: true },
        { at: 3.6, x: 1872, y: 1061 },
        { at: 4.1, x: 2284, y: 878, down: true },
        { at: 5.1, x: 2284, y: 858 },
        { at: 9, x: 2380, y: 1110 },
      ]}
    />
  </>
);

const ColorScene = () => (
  <>
    <Caption
      chapter="01 / THE PHOTO EDITOR"
      title="Refine every\ncolor range."
      detail="Hue, saturation and luminance. A perceptual color response."
    />
    <NativeSurface
      file="desktop-action/color-edit.mp4"
      focus={0.8}
      panelY={715}
      panelH={640}
      pointer={[
        { at: 0, x: 2220, y: 830 },
        { at: 2, x: 2405, y: 1068 },
        { at: 3, x: 2405, y: 1068, down: true },
        { at: 5, x: 2285, y: 1090 },
        { at: 7, x: 2190, y: 1190 },
      ]}
    />
  </>
);

const DetailScene = () => {
  const { t, portrait } = useShot();
  const open = smooth(t, 0.8, 2.2),
    close = smooth(t, 3.6, 4.8);
  return (
    <>
      <Caption
        chapter="01 / THE PHOTO EDITOR"
        title="Down to\nthe pixel."
        detail="Full-resolution originals. Precise local adjustments."
        serif
      />
      <div
        style={{
          ...A,
          left: portrait ? 74 : 100,
          top: portrait ? 600 : 290,
          width: portrait ? 932 : 1720,
          height: portrait ? 1100 : 720,
          overflow: "hidden",
          borderRadius: 22,
          background: "#111722",
        }}
      >
        <CanvasImage
          src={media("studio/color/4-8.png")}
          style={{
            width: "100%",
            height: "100%",
            objectFit: "cover",
            scale: 1 + 0.04 * t,
          }}
        />
        <div
          style={{
            ...A,
            left: `${mix(36, 7, open) + 29 * close}%`,
            top: `${mix(43, 4, open) + 39 * close}%`,
            width: `${mix(24, 86, open) - 62 * close}%`,
            height: `${mix(28, 92, open) - 64 * close}%`,
            border: "2px solid #e8f2da",
            overflow: "hidden",
            boxShadow: "0 0 0 2000px #08101d50",
            borderRadius: 7,
          }}
        >
          <CanvasImage
            src={media("studio/color/native-detail-lift.png")}
            style={{
              width: "100%",
              height: "100%",
              objectFit: "contain",
              background: "#09111d",
              imageRendering: "pixelated",
            }}
          />
        </div>
      </div>
      <div
        style={{
          ...A,
          right: portrait ? 90 : 130,
          bottom: portrait ? 115 : 100,
          fontFamily: font.mono,
          fontSize: 24,
          color: paper,
          background: "#0b0d12e8",
          padding: "12px 20px",
        }}
      >
        NATIVE SOURCE DETAIL / 960 × 960 CROP
      </div>
    </>
  );
};

const WeddingScene = () => {
  const { t, portrait } = useShot();
  const grade = smooth(t, 1.8, 3.5);
  return (
    <>
      <Caption
        chapter="PHOTOGRAPHY / CLIENT DELIVERY"
        title="For the moments\nyou deliver."
        detail="Wedding galleries. Travel stories. A consistent photographic finish."
        serif
      />
      <div
        style={{
          ...A,
          left: portrait ? 72 : 180,
          top: portrait ? 585 : 290,
          width: portrait ? 650 : 700,
          height: portrait ? 990 : 710,
          rotate: `${mix(-4, 0, out(t, 0, 1))}deg`,
          translate: `${-300 * (1 - out(t, 0, 0.9))}px 0`,
          overflow: "hidden",
          borderRadius: 8,
          boxShadow: "0 35px 65px #0008",
        }}
      >
        <CanvasImage
          src={media("rebuild/wedding/01/batch-before.png")}
          style={{ width: "100%", height: "100%", objectFit: "cover" }}
        />
        <CanvasImage
          src={media("rebuild/wedding/01/batch-after.png")}
          style={{
            ...A,
            inset: 0,
            width: "100%",
            height: "100%",
            objectFit: "cover",
            clipPath: `inset(0 ${100 * (1 - grade)}% 0 0)`,
          }}
        />
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 510 : 1010,
          top: portrait ? 1050 : 310,
          width: portrait ? 500 : 690,
          height: portrait ? 650 : 660,
          rotate: `${mix(6, 2, out(t, 0.3, 1.3))}deg`,
          translate: `${400 * (1 - out(t, 0.3, 1.3))}px 0`,
          overflow: "hidden",
          borderRadius: 8,
          boxShadow: "0 35px 65px #0008",
        }}
      >
        <CanvasImage
          src={media("rebuild/wedding/02/batch-before.png")}
          style={{ width: "100%", height: "100%", objectFit: "cover" }}
        />
        <CanvasImage
          src={media("rebuild/wedding/02/batch-after.png")}
          style={{
            ...A,
            inset: 0,
            width: "100%",
            height: "100%",
            objectFit: "cover",
            opacity: grade,
          }}
        />
      </div>
    </>
  );
};

const LibraryScene = () => {
  const { fps, t } = useShot();
  return (
    <>
      <Caption
        enterAt={t < 4 ? 0 : 4}
        chapter="02 / THE LIBRARY"
        title={t < 4 ? "Bring the\nwhole shoot." : "Find your selects."}
        detail={
          t < 4
            ? "Batch import. Originals organized."
            : "Rate. Pick. Compare. Cull."
        }
      />
      <Sequence
        durationInFrames={4 * fps}
        name="Native imported camera originals"
      >
        <NativeSurface
          file="desktop-launch/library.png"
          still
          panelY={260}
          imageCrop={[580, 400, 1380, 720]}
        />
      </Sequence>
      <Sequence
        from={4 * fps}
        durationInFrames={6 * fps}
        name="Native culling and ratings"
      >
        <NativeSurface
          file="desktop-launch/cull-native.mp4"
          trim={8}
          clockOffset={4}
          panelY={270}
          imageCrop={[600, 400, 1360, 850]}
        />
      </Sequence>
    </>
  );
};

const MaskScene = () => {
  const { t, fps } = useShot();
  return (
    <>
      <Caption
        enterAt={t < 6 ? 0 : 6}
        chapter="03 / THE CANVAS"
        title={t < 6 ? "Select the subject." : "Adjust just\nwhat matters."}
        detail={
          t < 6
            ? "Subject selection. Masks. Editable layers."
            : "Lift the subject. Preserve the background."
        }
      />
      <Sequence
        durationInFrames={6 * fps}
        name="Native subject selection and layer mask"
      >
        <NativeSurface
          file="desktop-action/subject-mask.mp4"
          trim={6}
          focus={0.55}
          panelY={780}
          panelH={710}
          imageCrop={[395, 280, 796, 1190]}
          pointer={[
            { at: 0, x: 2100, y: 1060 },
            { at: 1, x: 2300, y: 969 },
            { at: 2, x: 2300, y: 969, down: true },
            { at: 4, x: 880, y: 970 },
          ]}
        />
      </Sequence>
      <Sequence
        from={6 * fps}
        durationInFrames={10 * fps}
        name="Native linked selection exposure / processing wait edited out"
      >
        <NativeSurface
          file="desktop-action/mask-exposure.mp4"
          trim={19}
          clockOffset={6}
          focus={0.66}
          panelY={720}
          panelH={650}
          imageCrop={[458, 280, 669, 1004]}
          pointer={[
            { at: 0, x: 1900, y: 1190 },
            { at: 3.5, x: 2058, y: 1234 },
            { at: 4.5, x: 2058, y: 1234, down: true },
            { at: 5.4, x: 2130, y: 1234 },
            { at: 8, x: 2200, y: 1160 },
          ]}
        />
      </Sequence>
    </>
  );
};

const TypeScene = () => (
  <>
    <Caption
      chapter="03 / THE CANVAS"
      title="Make it\nyour own."
      detail="Editable type. A real layer stack. Your photograph, reimagined."
      serif
    />
    <NativeSurface
      file="desktop-action/poster-type.mp4"
      trim={34}
      panelY={1000}
      panelH={500}
      focus={0.52}
      imageCrop={[458, 280, 669, 1004]}
      pointer={[
        { at: 0, x: 2240, y: 1260 },
        { at: 1, x: 2400, y: 1260 },
        { at: 2, x: 2400, y: 1260, down: true },
        { at: 4, x: 1658, y: 1334, down: true },
        { at: 5, x: 1800, y: 1380 },
        { at: 9, x: 880, y: 520 },
      ]}
    />
  </>
);

const DesignScene = () => {
  const { t, portrait, width } = useShot();
  const reveal = smooth(t, 3, 5.2),
    shift = smooth(t, 6.1, 7.5);
  return (
    <>
      <Caption
        enterAt={t < 6.1 ? 0 : 6.1}
        chapter="03 / THE CANVAS"
        title={
          t < 6.1
            ? "Posters with\npresence."
            : "Thumbnails\nwith a point of view."
        }
        detail="Photography, type and graphics in one creative workspace."
      />
      <div style={{ ...A, inset: 0, perspective: 1600 }}>
        <div
          style={{
            ...A,
            left: portrait ? 200 - 1200 * shift : 215 - 2000 * shift,
            top: portrait ? 630 : 290,
            width: portrait ? 680 : 560,
            height: portrait ? 850 : 700,
            transformStyle: "preserve-3d",
            transform: `rotateY(${mix(-14, 0, out(t, 0, 1.2))}deg) rotateZ(${mix(-6, 0, out(t, 0, 1.2))}deg)`,
            opacity: 1 - shift,
          }}
        >
          <div
            style={{
              ...A,
              inset: 0,
              transform: `translateZ(${-110 * (1 - reveal)}px)`,
              boxShadow: "0 30px 70px #0008",
            }}
          >
            <ExpeditionPoster plane="base" />
          </div>
          <div
            style={{
              ...A,
              inset: 0,
              transform: `translateZ(${35 * (1 - reveal)}px) translateX(${0 * (1 - reveal)}px)`,
            }}
          >
            <ExpeditionPoster plane="type" build={out(t, 0.3, 1.4)} />
          </div>
          <div
            style={{
              ...A,
              inset: 0,
              transform: `translateZ(${45 * (1 - reveal)}px) translateX(${0 * (1 - reveal)}px)`,
            }}
          >
            <ExpeditionPoster plane="subject" />
          </div>
          <div
            style={{
              ...A,
              inset: 0,
              transform: `translateZ(${60 * (1 - reveal)}px)`,
            }}
          >
            <ExpeditionPoster plane="marks" build={out(t, 1, 2.2)} />
          </div>
        </div>
        {!portrait ? (
          <div
            style={{
              ...A,
              left: 1120,
              top: 420,
              width: 560,
              color: paper,
              fontFamily: font.mono,
              fontSize: 29,
              lineHeight: 2.1,
              opacity: 1 - shift,
            }}
          >
            <div
              style={{
                translate: `${90 * (1 - out(t, 0.4, 1.2))}px 0`,
                opacity: out(t, 0.4, 1.2),
              }}
            >
              01 / PHOTOGRAPH
            </div>
            <div
              style={{
                translate: `${90 * (1 - out(t, 0.7, 1.5))}px 0`,
                opacity: out(t, 0.7, 1.5),
              }}
            >
              02 / EDITABLE TYPE
            </div>
            <div
              style={{
                translate: `${90 * (1 - out(t, 1, 1.8))}px 0`,
                opacity: out(t, 1, 1.8),
              }}
            >
              03 / SUBJECT CUTOUT
            </div>
            <div
              style={{
                translate: `${90 * (1 - out(t, 1.3, 2.1))}px 0`,
                opacity: out(t, 1.3, 2.1),
              }}
            >
              04 / GRAPHIC DETAILS
            </div>
          </div>
        ) : null}
        <div
          style={{
            ...A,
            left: (width - (portrait ? 960 : 1340)) / 2 + width * (1 - shift),
            top: portrait ? 780 : 300,
            width: portrait ? 960 : 1340,
            height: portrait ? 540 : 754,
            opacity: shift,
            rotate: `${3 * (1 - shift)}deg`,
            boxShadow: "0 40px 80px #0009",
          }}
        >
          <NightThumbnail build={out(t, 6.2, 7.8)} />
        </div>
      </div>
      <div
        style={{
          ...A,
          bottom: portrait ? 180 : 60,
          left: portrait ? 75 : 100,
          fontFamily: font.serif,
          fontStyle: "italic",
          fontSize: portrait ? 50 : 37,
          color: lime,
          opacity: out(t, 2, 3),
        }}
      >
        From an edit to an idea.
      </div>
    </>
  );
};

const CarouselScene = () => {
  const { t, portrait, width } = useShot();
  const pan = smooth(t, 1.2, 6.5),
    split = smooth(t, 6.1, 8.4);
  const scale = portrait ? 0.83 - split * 0.55 : 0.49 - split * 0.04;
  const gap = 60 * split;
  return (
    <>
      <Caption
        chapter="04 / THE CAROUSEL"
        title="One photograph.\nRoom to keep going."
        detail="A continuous image across canvases. Made for the swipe."
        serif
      />
      <div
        style={{
          ...A,
          left: portrait ? 60 : 90,
          top: portrait ? 690 : 330,
          width: width - (portrait ? 120 : 180),
          height: portrait ? 1060 : 700,
          overflow: "hidden",
          perspective: 1600,
        }}
      >
        <div
          style={{
            ...A,
            left: portrait ? mix(0, -1600, pan) * (1 - out(t, 6.1, 7.8)) : 0,
            top: portrait ? 0 : 0,
            width: 3240 + 2 * gap,
            height: 1350,
            scale,
            transformOrigin: "0 0",
            transform: `rotateY(${7 * (1 - smooth(t, 0, 2))}deg)`,
          }}
        >
          <div
            style={{
              ...A,
              left: 0,
              top: 0,
              width: 1080,
              height: 1350,
              overflow: "hidden",
              boxShadow: "0 20px 60px #0007",
            }}
          >
            <div style={{ position: "relative", width: 3240, height: 1350 }}>
              <PanoramaArtwork />
            </div>
          </div>
          <div
            style={{
              ...A,
              left: 1080 + gap,
              top: 0,
              width: 1080,
              height: 1350,
              overflow: "hidden",
              boxShadow: "0 20px 60px #0007",
            }}
          >
            <div
              style={{
                position: "relative",
                width: 3240,
                height: 1350,
                translate: "-1080px 0",
              }}
            >
              <PanoramaArtwork />
            </div>
          </div>
          <div
            style={{
              ...A,
              left: 2160 + 2 * gap,
              top: 0,
              width: 1080,
              height: 1350,
              overflow: "hidden",
              boxShadow: "0 20px 60px #0007",
            }}
          >
            <div
              style={{
                position: "relative",
                width: 3240,
                height: 1350,
                translate: "-2160px 0",
              }}
            >
              <PanoramaArtwork />
            </div>
          </div>
        </div>
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 75 : 100,
          bottom: portrait ? 115 : 50,
          fontFamily: font.mono,
          color: "#a7b6c7",
          fontSize: 20,
        }}
      >
        CONTINUOUS 3240 × 1350 ARTWORK → 3 × 1080 × 1350 CANVASES
      </div>
    </>
  );
};

const Ending = () => {
  const { t, portrait } = useShot();
  return (
    <>
      <DesktopLight t={t + 13} gather={smooth(t, 0, 2.8)} />
      <div
        style={{
          ...A,
          left: portrait ? 100 : 530,
          top: portrait ? 680 : 290,
          width: portrait ? 880 : 860,
          translate: `0 ${60 * (1 - out(t, 0, 0.9))}px`,
          opacity: out(t, 0, 0.9),
        }}
      >
        <AppWordmark width={portrait ? 880 : 860} />
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 75 : 360,
          right: portrait ? 75 : 360,
          top: portrait ? 950 : 565,
          textAlign: "center",
          color: paper,
          fontFamily: font.serif,
          fontStyle: "italic",
          fontSize: portrait ? 62 : 55,
          opacity: out(t, 0.4, 1.3),
        }}
      >
        The work is yours.
        <br />
        So are the possibilities.
      </div>
    </>
  );
};

// This action cut is a 104-second review unit of the longer launch
// project. The 3:52 production script, timeline and optional-agent chapters
// remain separate and explicit; this is not labelled as that completed film.
export const DesktopActionFilm = () => {
  const { fps } = useVideoConfig();
  const globalTime = useCurrentFrame() / fps;
  return (
    <AbsoluteFill
      style={{
        background: ink,
        color: paper,
        overflow: "hidden",
        fontFamily: font.sans,
      }}
    >
      <DesktopLight t={globalTime} style={{ opacity: 0.2 }} />
      <Sequence
        name="Introducing Looksy / textured additive light"
        durationInFrames={6 * fps}
      >
        <Introduction />
      </Sequence>
      <Sequence
        name="01 / Native RAW and the custom color engine"
        from={6 * fps}
        durationInFrames={10 * fps}
      >
        <RawScene />
      </Sequence>
      <Sequence
        name="01 / Native RGB curve points and actual response"
        from={16 * fps}
        durationInFrames={8 * fps}
      >
        <CurveScene />
      </Sequence>
      <Sequence
        name="01 / Native Color Mixer and actual image response"
        from={24 * fps}
        durationInFrames={8 * fps}
      >
        <ColorScene />
      </Sequence>
      <Sequence
        name="01 / Full-resolution source pixels"
        from={32 * fps}
        durationInFrames={5 * fps}
      >
        <DetailScene />
      </Sequence>
      <Sequence
        name="01 / Wedding photography delivery example"
        from={37 * fps}
        durationInFrames={8 * fps}
      >
        <WeddingScene />
      </Sequence>
      <Sequence
        name="02 / Native library import and culling"
        from={45 * fps}
        durationInFrames={8 * fps}
      >
        <LibraryScene />
      </Sequence>
      <Sequence
        name="03 / Native mask and isolated adjustment"
        from={53 * fps}
        durationInFrames={16 * fps}
      >
        <MaskScene />
      </Sequence>
      <Sequence
        name="03 / Native editable typography"
        from={69 * fps}
        durationInFrames={10 * fps}
      >
        <TypeScene />
      </Sequence>
      <Sequence
        name="03 / Layered poster and thumbnail art direction"
        from={79 * fps}
        durationInFrames={11 * fps}
      >
        <DesignScene />
      </Sequence>
      <Sequence
        name="04 / A photograph split across social canvases"
        from={90 * fps}
        durationInFrames={10 * fps}
      >
        <CarouselScene />
      </Sequence>
      <Sequence
        name="Review-unit close / full production continues separately"
        from={100 * fps}
        durationInFrames={4 * fps}
      >
        <Ending />
      </Sequence>
      <Audio src={staticFile("audio/desktop-action-master.wav")} />
    </AbsoluteFill>
  );
};
