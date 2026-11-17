import { useId } from "react";
import {
  AbsoluteFill,
  CanvasImage,
  Sequence,
  staticFile,
  useVideoConfig,
} from "remotion";
import { Audio, Video } from "@remotion/media";
import { getLength, getPointAtLength } from "@remotion/paths";
import {
  AppIcon,
  AppInspectorSlider,
  AppWordmark,
} from "../app-kit/AppElements";
import { font } from "./Type";
import {
  A,
  Cursor,
  Display,
  Image,
  Tag,
  ink,
  lime,
  paper,
  mix,
  out,
  pop,
  smooth,
  useTime,
} from "./Kit";
import {
  ExpeditionPoster,
  NightThumbnail,
  MountainThumbnail,
  WeddingCover,
} from "./Artwork";
import { Spectral } from "./Spectral";

const Wrap = ({ children }: { children: React.ReactNode }) => (
  <AbsoluteFill
    style={{
      background: ink,
      color: paper,
      fontFamily: font.sans,
      overflow: "hidden",
    }}
  >
    {children}
  </AbsoluteFill>
);

export const NewOpening = ({
  destination = "photo",
}: {
  destination?: "photo" | "wedding";
}) => {
  const t = useTime(),
    { fps } = useVideoConfig(),
    depart = smooth(t, 2.8, 3.6),
    gather =
      destination === "wedding" ? smooth(t, 5.4, 6.5) : smooth(t, 6.3, 7.6),
    enter = smooth(t, destination === "wedding" ? 10.8 : 10.1, 12),
    back = smooth(t, 3.4, 4.3);
  return (
    <>
      <Spectral
        t={t}
        gather={smooth(t, 1.5, 3.3) * (1 - smooth(t, 6, 7))}
        style={{ opacity: 1 - enter }}
      />
      <div
        style={{
          ...A,
          inset: 0,
          background:
            "radial-gradient(ellipse at 50% 48%,transparent 15%,#0b0d1260 72%)",
        }}
      />
      <div
        style={{
          ...A,
          left: 80,
          top: 170,
          opacity: 1 - depart,
          translate: `${-900 * depart}px 0`,
          rotate: `${-7 * depart}deg`,
        }}
      >
        <Tag style={{ color: lime, opacity: out(t, 0.1, 0.5) }}>
          A NEW WAY TO MAKE.
        </Tag>
        <Display
          style={{
            fontSize: 190,
            marginTop: 55,
            translate: `0 ${160 * (1 - pop(t, 0.2))}px`,
          }}
        >
          YOUR WORK
        </Display>
        <Display
          style={{
            fontSize: 200,
            translate: `${-1150 * (1 - pop(t, 0.37))}px 0`,
          }}
        >
          BELONGS
        </Display>
        <div
          style={{
            fontFamily: font.serif,
            fontStyle: "italic",
            fontSize: 250,
            lineHeight: 0.85,
            translate: `0 ${180 * (1 - pop(t, 0.54))}px`,
            color: lime,
          }}
        >
          together.
        </div>
      </div>
      <div
        style={{
          ...A,
          inset: 0,
          perspective: 1500,
          opacity: back * (1 - enter),
        }}
      >
        <div
          style={{
            ...A,
            inset: 0,
            transform: `rotateX(${mix(13, 0, gather)}deg) rotateY(${mix(-13, 0, gather)}deg)`,
            transformStyle: "preserve-3d",
          }}
        >
          <div
            style={{
              ...A,
              left: mix(65, 165, gather),
              top: mix(430, 790, gather),
              width: mix(425, 750, gather),
              height: mix(585, 938, gather),
              rotate: `${mix(-8, 0, gather)}deg`,
              translate: `${-650 * (1 - pop(t, 3.45))}px 0`,
              boxShadow: "0 35px 65px #0009",
              zIndex: 2,
              opacity: destination === "wedding" ? 1 - gather : 1,
            }}
          >
            <ExpeditionPoster />
            <Tag style={{ ...A, left: 0, top: -55, color: lime }}>DESIGN</Tag>
          </div>
          <div
            style={{
              ...A,
              left: mix(590, 165, gather),
              top: mix(360, 790, gather),
              width: mix(400, 750, gather),
              height: mix(500, 938, gather),
              rotate: `${mix(8, 0, gather)}deg`,
              translate: `${650 * (1 - pop(t, 3.65))}px 0`,
              opacity: destination === "wedding" ? 1 : 1 - smooth(t, 6.1, 7.1),
              boxShadow: "0 35px 65px #0009",
            }}
          >
            <WeddingCover
              imageSrc={
                destination === "wedding"
                  ? "campaign/wedding-bride-01.jpg"
                  : undefined
              }
            />
            <Tag style={{ ...A, left: 0, top: -55, color: lime }}>
              PHOTOGRAPHY
            </Tag>
          </div>
          <div
            style={{
              ...A,
              left: mix(350, 165, gather),
              top: mix(1130, 790, gather),
              width: mix(680, 750, gather),
              height: mix(382.5, 938, gather),
              rotate: `${mix(7, 0, gather)}deg`,
              translate: `0 ${800 * (1 - pop(t, 3.85))}px`,
              opacity: 1 - smooth(t, 6.3, 7.3),
              boxShadow: "0 35px 65px #0009",
              overflow: "hidden",
            }}
          >
            <Image src="studio/night-thumb.jpg" />
            <Sequence from={4 * fps} durationInFrames={5 * fps} layout="none">
              <Video
                src={staticFile("media/refine/night.mp4")}
                muted
                objectFit="cover"
                style={{ width: "100%", height: "100%" }}
              />
            </Sequence>
            <div
              style={{
                ...A,
                inset: 0,
                background: "linear-gradient(0deg,#10101288,transparent)",
              }}
            />
            <Display style={{ ...A, left: 35, bottom: 35, fontSize: 100 }}>
              AFTER DARK.
            </Display>
            <Tag style={{ ...A, left: 20, top: 20, color: paper }}>
              FILM / 4K
            </Tag>
          </div>
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 145,
          opacity:
            out(
              t,
              destination === "wedding" ? 5.9 : 6.7,
              destination === "wedding" ? 6.4 : 7.2,
            ) *
            (1 - enter),
          translate: `0 ${65 * (1 - pop(t, destination === "wedding" ? 5.9 : 6.7))}px`,
        }}
      >
        <Tag style={{ color: lime }}>INTRODUCING</Tag>
        <div style={{ marginTop: 25 }}>
          <AppWordmark width={850} />
        </div>
        <div
          style={{
            fontFamily: font.serif,
            fontSize: 108,
            lineHeight: 1.03,
            marginTop: 35,
          }}
        >
          A photo + video studio.
        </div>
        <div
          style={{
            fontSize: 44,
            color: "#bcc3ca",
            marginTop: 26,
            lineHeight: 1.3,
          }}
        >
          Built from the ground up.
          {destination !== "wedding" && (
            <>
              <br />
              Professional tools. Room to play.
            </>
          )}
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 0,
          top: 0,
          width: 1080,
          height: 1920,
          opacity: enter,
          clipPath: `circle(${mix(0, 140, enter)}% at 50% 58%)`,
        }}
      >
        {destination === "wedding" ? (
          <WeddingCover imageSrc="campaign/wedding-bride-01.jpg" />
        ) : (
          <Image src="studio/color/4-8.png" />
        )}
        <div
          style={{
            ...A,
            inset: 0,
            background:
              destination === "wedding"
                ? "transparent"
                : "linear-gradient(#0b0d1299,transparent 65%)",
          }}
        />
      </div>
    </>
  );
};

const brushPath = "M436 730C471 748 503 755 506 803C508 843 491 870 465 892";
const brushLength = getLength(brushPath);
export const MeaningfulBrush = ({
  bridgeToDesign = true,
}: {
  bridgeToDesign?: boolean;
}) => {
  const t = useTime(),
    id = useId().replace(/:/g, ""),
    zoom = smooth(t, 0.8, 1.6) * (1 - smooth(t, 7.6, 8.6)),
    paint = smooth(t, 2.05, 4.6),
    lift = 0.55 * smooth(t, 5.1, 6.9),
    overlay = smooth(t, 1.8, 2.05) * (1 - smooth(t, 4.7, 5.05)),
    point = getPointAtLength(brushPath, brushLength * paint),
    design = bridgeToDesign ? smooth(t, 10.8, 13) : 0;
  const vx = mix(0, 300, zoom),
    vy = mix(0, 610, zoom),
    vw = mix(960, 360, zoom),
    vh = mix(1440, 540, zoom);
  return (
    <>
      <svg
        width={1080}
        height={1920}
        viewBox={`${vx} ${vy} ${vw} ${vh}`}
        preserveAspectRatio="xMidYMid slice"
        style={{ ...A, inset: 0, opacity: 1 - design }}
      >
        <defs>
          <mask id={`${id}subject`} style={{ maskType: "alpha" }}>
            <image
              href={staticFile("media/studio/subject.png")}
              width={960}
              height={1440}
            />
          </mask>
          <filter
            id={`${id}feather`}
            filterUnits="userSpaceOnUse"
            x={0}
            y={0}
            width={960}
            height={1440}
          >
            <feGaussianBlur stdDeviation={4} />
          </filter>
          <mask id={`${id}paint`}>
            <path
              d={brushPath}
              fill="none"
              stroke="white"
              strokeWidth={110}
              strokeLinecap="round"
              pathLength={1}
              strokeDasharray={1}
              strokeDashoffset={1 - paint}
              filter={`url(#${id}feather)`}
            />
          </mask>
        </defs>
        <image
          href={staticFile("media/studio/color/4-8.png")}
          width={960}
          height={1440}
        />
        <g mask={`url(#${id}subject)`}>
          <g mask={`url(#${id}paint)`}>
            <image
              href={staticFile("media/studio/color/subject-global.png")}
              width={960}
              height={1440}
              opacity={lift / 0.55}
            />
            <rect
              width={960}
              height={1440}
              fill="#fa5bbd"
              opacity={overlay * 0.7}
            />
          </g>
        </g>
        <g opacity={t > 1.9 && t < 4.7 ? 1 : 0}>
          <circle
            cx={point?.x ?? 436}
            cy={point?.y ?? 730}
            r={55}
            stroke="white"
            strokeWidth={1.1}
            fill="none"
          />
          <circle
            cx={point?.x ?? 436}
            cy={point?.y ?? 730}
            r={1.5}
            fill="white"
          />
        </g>
      </svg>
      <CanvasImage
        src={staticFile("media/studio/subject.png")}
        style={{ ...A, width: 1, height: 1, opacity: 0 }}
      />
      <div
        style={{
          ...A,
          inset: 0,
          background:
            "linear-gradient(#0b0d12d9,transparent 30%,transparent 65%,#0b0d12d9)",
          opacity: 1 - design,
        }}
      />
      <div
        style={{
          ...A,
          left: 80,
          top: 115,
          opacity: (1 - design) * (1 - smooth(t, 9.6, 10.2)),
        }}
      >
        <Tag style={{ color: lime }}>MASKS / BRUSH CONTROL</Tag>
        <Display
          style={{
            fontSize: 130,
            marginTop: 28,
            translate: `0 ${50 * (1 - pop(t, 0.1))}px`,
          }}
        >
          CHOOSE WHAT
        </Display>
        <div
          style={{
            fontFamily: font.serif,
            fontStyle: "italic",
            fontSize: 160,
            lineHeight: 0.9,
            translate: `${-800 * (1 - pop(t, 0.2))}px 0`,
          }}
        >
          changes.
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1420,
          width: 920,
          opacity: 1 - design,
          translate: `0 ${260 * (1 - pop(t, 0.5))}px`,
          background: "#121820e8",
          border: "1px solid #60727c66",
          borderRadius: 20,
          padding: 34,
          boxSizing: "border-box",
          boxShadow: "0 25px 65px #0006",
        }}
      >
        <div style={{ display: "flex", gap: 26, alignItems: "center" }}>
          <div
            style={{
              width: 105,
              height: 135,
              background: "#030404",
              border: "1px solid #89929a",
              overflow: "hidden",
            }}
          >
            <svg viewBox="300 610 360 540" width="100%" height="100%">
              <defs>
                <mask id={`${id}thumb`} style={{ maskType: "alpha" }}>
                  <image
                    href={staticFile("media/studio/subject.png")}
                    width={960}
                    height={1440}
                  />
                </mask>
              </defs>
              <path
                d={brushPath}
                mask={`url(#${id}thumb)`}
                stroke="white"
                strokeWidth={110}
                strokeLinecap="round"
                fill="none"
                pathLength={1}
                strokeDasharray={1}
                strokeDashoffset={1 - paint}
                filter={`url(#${id}feather)`}
              />
            </svg>
          </div>
          <div>
            <Tag style={{ color: "#bac7d2" }}>BRUSH MASK / JACKET</Tag>
            <div style={{ fontSize: 36, marginTop: 12 }}>
              {t < 5
                ? "Paint the area you want to adjust."
                : "Lift the jacket. Protect the scene."}
            </div>
            <div
              style={{
                fontFamily: font.mono,
                fontSize: 24,
                marginTop: 12,
                color: lime,
              }}
            >
              SOFT BRUSH · 460 PX · EDITABLE MASK
            </div>
          </div>
        </div>
        <div
          style={{ height: 1, background: "#596775", margin: "27px 0 20px" }}
        />
        <div style={{ width: 852, height: 96, position: "relative" }}>
          <div style={{ transform: "scale(2.5)", transformOrigin: "0 0" }}>
            <AppInspectorSlider
              label="Exposure"
              valueText={`+${lift.toFixed(2)} EV`}
              value={lift}
              from={-5}
              to={5}
              width={340}
            />
          </div>
          <Cursor x={395 + lift * 21} y={48} down={t > 5.1 && t < 6.9} />
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1760,
          fontSize: 40,
          opacity: (1 - design) * out(t, 5, 5.4),
        }}
      >
        Same pixels. One selected adjustment.
      </div>
      <div
        style={{
          ...A,
          left: mix(80, 100, design),
          top: mix(120, 320, design),
          width: 880,
          height: 1100,
          opacity: design,
          scale: mix(1.2, 1, design),
        }}
      >
        <ExpeditionPoster build={design} />
      </div>
    </>
  );
};

export const LayeredDesign = () => {
  const t = useTime(),
    { fps } = useVideoConfig(),
    explode = smooth(t, 0.4, 1.25) * (1 - smooth(t, 3.45, 4.2)),
    type = out(t, 4.3, 5),
    thumb = smooth(t, 7.8, 9.2),
    more = smooth(t, 11.5, 12.6),
    last = smooth(t, 15, 17);
  return (
    <>
      <Spectral t={t + 18} style={{ opacity: 0.24 * (1 - last) }} />
      <div style={{ ...A, left: 80, top: 120, opacity: 1 - last }}>
        <Tag style={{ color: lime }}>REAL LAYERS. YOUR COMPOSITION.</Tag>
        <div
          style={{
            fontFamily: font.serif,
            fontSize: 130,
            fontStyle: "italic",
            lineHeight: 0.93,
            marginTop: 25,
          }}
        >
          Make it
        </div>
        <Display style={{ fontSize: 164, marginTop: 8 }}>YOUR OWN.</Display>
      </div>
      <div style={{ ...A, inset: 0, perspective: 1700, opacity: 1 - last }}>
        <div
          style={{
            ...A,
            left: mix(100, 80, thumb),
            top: mix(510, 650, thumb) + 110 * explode,
            width: mix(880, 920, thumb),
            height: mix(1100, 517.5, thumb),
            transformStyle: "preserve-3d",
            transform: `rotateY(${-13 * explode + 180 * thumb}deg) rotateX(${8 * explode}deg)`,
            scale: mix(1, 0.68, more) * (1 - 0.2 * explode),
            translate: `0 ${-165 * more}px`,
            transformOrigin: "50% 20%",
          }}
        >
          {thumb < 1 &&
            (["base", "type", "subject", "marks"] as const).map((plane, i) => (
              <div
                key={plane}
                style={{
                  ...A,
                  inset: 0,
                  opacity: 1,
                  backfaceVisibility: "hidden",
                  translate: `${(i - 1.5) * 100 * explode}px ${(i - 1.5) * -130 * explode}px`,
                  transform: `translateZ(${i * 80 * explode}px)`,
                  boxShadow: explode > 0.1 ? "0 25px 50px #0006" : undefined,
                }}
              >
                <ExpeditionPoster
                  plane={plane}
                  tracking={
                    plane === "type" ? mix(-5, 3, smooth(t, 5.2, 6.7)) : 0
                  }
                />
                {explode > 0.1 && (
                  <Tag
                    style={{
                      ...A,
                      left: 25,
                      bottom: -42,
                      translate: "0 0",
                      color: lime,
                      background: ink,
                      padding: 8,
                      fontSize: 22,
                    }}
                  >
                    {["PHOTO", "TYPE", "SUBJECT MASK", "GRAPHICS"][i]}
                  </Tag>
                )}
              </div>
            ))}
          {thumb > 0 && (
            <div
              style={{
                ...A,
                inset: 0,
                opacity: 1,
                backfaceVisibility: "hidden",
                transform: "rotateY(180deg)",
                overflow: "hidden",
                boxShadow: "0 30px 70px #0009",
              }}
            >
              <NightThumbnail build={smooth(t, 8.2, 9.6)} />
            </div>
          )}
          {type > 0 && thumb === 0 && (
            <div
              style={{
                ...A,
                left: 25,
                right: 25,
                top: 435,
                height: 185,
                border: "2px solid #e1e8dd",
                opacity: 1 - smooth(t, 7.2, 7.7),
              }}
            >
              {[
                [0, 0],
                [50, 0],
                [100, 0],
                [0, 100],
                [50, 100],
                [100, 100],
              ].map(([x, y], i) => (
                <div
                  key={i}
                  style={{
                    ...A,
                    left: `${x}%`,
                    top: `${y}%`,
                    width: 12,
                    height: 12,
                    background: paper,
                    translate: "-50% -50%",
                  }}
                />
              ))}
            </div>
          )}
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1640,
          width: 920,
          opacity: smooth(t, 4.3, 4.8) * (1 - smooth(t, 7.55, 8.05)),
          translate: `0 ${130 * (1 - pop(t, 4.3))}px`,
          padding: 30,
          boxSizing: "border-box",
          background: "#141a23",
          border: "1px solid #566270",
          borderRadius: 15,
        }}
      >
        <div style={{ display: "flex", alignItems: "center", gap: 22 }}>
          <AppIcon name="text" size={40} />
          <span style={{ fontSize: 32 }}>Anton</span>
          <span style={{ fontSize: 30, color: lime, marginLeft: "auto" }}>
            Tracking {mix(-5, 3, smooth(t, 5.2, 6.7)).toFixed(1)}
          </span>
        </div>
        <div style={{ fontSize: 27, color: "#a7b1be", marginTop: 18 }}>
          Editable type · Foreground mask · Original photograph
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1360,
          width: 920,
          opacity: smooth(t, 9.05, 9.5) * (1 - more),
        }}
      >
        <div
          style={{ fontFamily: font.serif, fontSize: 104, fontStyle: "italic" }}
        >
          A different format.
        </div>
        <Display style={{ fontSize: 105, marginTop: 15 }}>
          A DIFFERENT IDEA.
        </Display>
        <Tag style={{ marginTop: 28, color: lime }}>
          16:9 THUMBNAIL / EDITABLE TYPE + PHOTO
        </Tag>
      </div>
      <div
        style={{
          ...A,
          left: 130,
          top: 1100,
          width: 820,
          height: 461.25,
          opacity: more * (1 - last),
          translate: `0 ${250 * (1 - pop(t, 11.6))}px`,
          rotate: `${-3 * more}deg`,
          boxShadow: "0 25px 80px #000a",
        }}
      >
        <MountainThumbnail />
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1650,
          opacity: more * (1 - last),
          fontFamily: font.serif,
          fontSize: 100,
          fontStyle: "italic",
        }}
      >
        For whatever comes next.
      </div>
      <div style={{ ...A, inset: 0, background: ink, opacity: last }}>
        <Spectral
          t={t + 30}
          gather={smooth(t, 15, 16.3)}
          style={{ opacity: 0.7 }}
        />
        <div
          style={{
            ...A,
            left: 80,
            top: 430,
            translate: `0 ${100 * (1 - pop(t, 15.25))}px`,
          }}
        >
          <AppWordmark width={850} />
          <div
            style={{
              fontFamily: font.serif,
              fontSize: 132,
              marginTop: 80,
              lineHeight: 0.98,
            }}
          >
            Photo. Design.
            <br />
            <span style={{ fontStyle: "italic", color: lime }}>
              Film. Yours.
            </span>
          </div>
          <Tag style={{ marginTop: 100, color: paper }}>
            PROFESSIONAL TOOLS. ROOM TO PLAY.
          </Tag>
        </div>
      </div>
      <Sequence from={9.2 * fps} durationInFrames={0.8 * fps} layout="none">
        <Audio
          src={staticFile("audio/foley/interface-click.mp3")}
          volume={0.08}
        />
      </Sequence>
    </>
  );
};

export const MotionRebuild = () => {
  const { fps } = useVideoConfig();
  return (
    <Wrap>
      <Audio
        src={staticFile("audio/studio/origami.mp3")}
        trimBefore={81 * fps}
        volume={(f) =>
          0.5 * smooth(f / fps, 0, 0.6) * (1 - smooth(f / fps, 40.7, 42))
        }
      />
      <Sequence
        name="01 / Your work belongs together / new brand motion"
        durationInFrames={12 * fps}
      >
        <NewOpening />
      </Sequence>
      <Sequence
        name="02 / Paint a meaningful mask / native exposure result"
        from={12 * fps}
        durationInFrames={13 * fps}
      >
        <MeaningfulBrush />
      </Sequence>
      <Sequence
        name="03 / Editorial design / exploded layers / creator thumbnails"
        from={25 * fps}
        durationInFrames={17 * fps}
      >
        <LayeredDesign />
      </Sequence>
      <Sequence
        name="Sound / identity resolves"
        from={6.8 * fps}
        durationInFrames={fps}
        layout="none"
      >
        <Audio
          src={staticFile("audio/foley/interface-click.mp3")}
          volume={0.06}
        />
      </Sequence>
      <Sequence
        name="Sound / enter the photograph"
        from={10.5 * fps}
        durationInFrames={1.5 * fps}
        layout="none"
      >
        <Audio
          src={staticFile("audio/foley/quick-air-woosh.mp3")}
          volume={0.05}
        />
      </Sequence>
      <Sequence
        name="Sound / brush contact"
        from={14 * fps}
        durationInFrames={fps}
        layout="none"
      >
        <Audio
          src={staticFile("audio/foley/interface-click.mp3")}
          volume={0.04}
        />
      </Sequence>
      <Sequence
        name="Sound / adjustment confirms"
        from={19 * fps}
        durationInFrames={fps}
        layout="none"
      >
        <Audio
          src={staticFile("audio/foley/interface-click.mp3")}
          volume={0.05}
        />
      </Sequence>
      <Sequence
        name="Sound / type settles"
        from={29.4 * fps}
        durationInFrames={fps}
        layout="none"
      >
        <Audio
          src={staticFile("audio/foley/interface-click.mp3")}
          volume={0.06}
        />
      </Sequence>
    </Wrap>
  );
};
