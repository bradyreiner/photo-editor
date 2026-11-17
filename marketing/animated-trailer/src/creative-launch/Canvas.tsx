import { staticFile } from "remotion";
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
  Surface,
  Native,
  At,
  Label,
  Pills,
  Type,
  Footer,
  PaperGrain,
  font,
  lime,
  paper,
} from "./Kit";

export const CampaignPoster = ({
  build = 1,
  brush = 1,
  typeface = "display",
  tracking = -7,
  productOpacity = 1,
  brushOpacity = 1,
}: {
  build?: number;
  brush?: number;
  typeface?: "display" | "serif" | "sans";
  tracking?: number;
  productOpacity?: number;
  brushOpacity?: number;
}) => (
  <div
    style={{
      ...A,
      width: 1000,
      height: 1250,
      overflow: "hidden",
      background: "#dfff43",
      color: "#142041",
    }}
  >
    <div
      style={{
        ...A,
        left: 350,
        top: 180,
        width: 950,
        height: 950,
        borderRadius: 500,
        background: "#2545d5",
        transform: `translateX(${500 * (1 - build)}px)`,
      }}
    />
    <svg
      viewBox="0 0 1000 1250"
      style={{
        ...A,
        inset: 0,
        width: 1000,
        height: 1250,
        opacity: brushOpacity,
      }}
    >
      <defs>
        <filter id="campaign-ink">
          <feTurbulence
            type="fractalNoise"
            baseFrequency=".07 .2"
            numOctaves="2"
            seed="8"
          />
          <feDisplacementMap in="SourceGraphic" scale="8" />
        </filter>
      </defs>
      <path
        d="M-120 955L1100 400"
        stroke="#ee6d33"
        strokeWidth="180"
        fill="none"
        strokeLinecap="round"
        pathLength="1"
        strokeDasharray="1"
        strokeDashoffset={1 - brush}
        filter="url(#campaign-ink)"
      />
      <path
        d="M-80 1000L1100 445"
        stroke="#ff8e42"
        strokeWidth="4"
        fill="none"
        pathLength="1"
        strokeDasharray="1"
        strokeDashoffset={1 - brush}
      />
    </svg>
    <div
      style={{
        ...A,
        left: 55,
        right: 55,
        top: 40,
        display: "flex",
        justifyContent: "space-between",
        fontFamily: font.mono,
        fontSize: 18,
        letterSpacing: 2,
        borderBottom: "2px solid #142041",
        paddingBottom: 20,
      }}
    >
      <span>OFF THE CLOCK / OUTSIDE</span>
      <span>FIELD EQUIPMENT · 01</span>
    </div>
    <div
      style={{
        ...A,
        left: 32,
        top: 88,
        fontFamily: font[typeface],
        fontSize: 300,
        lineHeight: 1,
        letterSpacing: tracking,
        transform: `translateY(${120 * (1 - build)}px)`,
      }}
    >
      STAY
    </div>
    <div
      style={{
        ...A,
        left: 77,
        top: 335,
        fontFamily: font.serif,
        fontStyle: "italic",
        fontSize: 165,
        color: paper,
        rotate: "-10deg",
        transform: `scale(${build})`,
      }}
    >
      in
    </div>
    <Pic
      src="creative-launch/trail-shoe-cutout.png"
      style={{
        left: -30,
        top: 365,
        width: 1120,
        height: 840,
        objectFit: "contain",
        opacity: productOpacity,
        transform: `translate(${150 * (1 - build)}px,${-180 * (1 - build)}px) rotate(${mix(-22, -8, build)}deg)`,
        filter: "drop-shadow(0 25px 20px #14204144)",
      }}
    />
    <div
      style={{
        ...A,
        left: 35,
        top: 982,
        fontFamily: font.display,
        fontSize: 218,
        lineHeight: 1,
        letterSpacing: -4,
        color: paper,
        transform: `translateY(${170 * (1 - build)}px)`,
      }}
    >
      MOTION.
    </div>
    <div
      style={{
        ...A,
        left: 55,
        right: 55,
        bottom: 30,
        fontFamily: font.mono,
        fontSize: 17,
        display: "flex",
        justifyContent: "space-between",
        borderTop: "2px solid #142041",
        paddingTop: 16,
      }}
    >
      <span>GO FIND YOUR OWN WAY.</span>
      <span>↗ KEEP MOVING</span>
    </div>
    <PaperGrain opacity={0.28} />
  </div>
);

export const CampaignThumbnail = ({ build = 1 }: { build?: number }) => (
  <div
    style={{
      ...A,
      width: 1000,
      height: 562,
      overflow: "hidden",
      background: "#f0ead8",
      color: "#132044",
    }}
  >
    <div
      style={{
        ...A,
        left: 470,
        top: -200,
        width: 800,
        height: 1000,
        background: "#263ed2",
        rotate: "18deg",
        transform: `translateX(${600 * (1 - build)}px)`,
      }}
    />
    <svg
      style={{ ...A, inset: 0, width: 1000, height: 562 }}
      viewBox="0 0 1000 562"
    >
      <path
        d="M470 620Q550 80 1020 80"
        fill="none"
        stroke="#dbff43"
        strokeWidth="70"
        pathLength="1"
        strokeDasharray="1"
        strokeDashoffset={1 - build}
      />
      <circle
        cx="820"
        cy="265"
        r="210"
        fill="none"
        stroke="#f0ead84a"
        strokeWidth="2"
      />
    </svg>
    <div
      style={{
        ...A,
        left: 40,
        top: 22,
        fontFamily: font.mono,
        fontSize: 15,
        letterSpacing: 1,
      }}
    >
      OUTSIDE / A FIELD TEST
    </div>
    <div
      style={{
        ...A,
        left: 30,
        top: 77,
        fontFamily: font.display,
        fontSize: 176,
        lineHeight: 0.95,
        letterSpacing: -3,
        transform: `translateX(${-350 * (1 - build)}px)`,
      }}
    >
      OUT
      <br />
      THERE.
    </div>
    <Pic
      src="creative-launch/trail-shoe-cutout.png"
      style={{
        left: 360,
        top: 38,
        width: 720,
        height: 540,
        objectFit: "contain",
        transform: `rotate(-23deg) scale(${mix(0.55, 1, build)})`,
        filter: "drop-shadow(0 25px 15px #0004)",
      }}
    />
    <div
      style={{
        ...A,
        left: 38,
        bottom: 31,
        fontFamily: font.serif,
        fontStyle: "italic",
        fontSize: 39,
      }}
    >
      Take the long way.
    </div>
    <PaperGrain opacity={0.22} />
  </div>
);

export const CanvasChapter = () => {
  const { t } = useShot();
  const beat =
      t < 5 ? 0 : t < 10 ? 1 : t < 15 ? 2 : t < 22 ? 3 : t < 30 ? 4 : 5,
    s = t - [0, 5, 10, 15, 22, 30][beat];
  const copy = [
    [
      "Go beyond\nthe whole frame.",
      "Select a subject. Build a mask. Refine exactly what needs attention.",
    ],
    [
      "Your subject.\nIts own layer.",
      "Local exposure adjustments, with the mask and layer retained.",
    ],
    [
      "Leave out\nthe distraction.",
      "Brush a selection. Inpaint locally. Keep the rest of the photograph.",
    ],
    [
      "A photo can\nbe a campaign.",
      "Cutouts, layers and a canvas for posters, graphics and new ideas.",
    ],
    [
      "Make your\nmark.",
      "Brush texture. Editable typography. Every element on its own layer.",
    ],
    [
      "Same idea.\nA new format.",
      "Build a thumbnail that has something to say.",
    ],
  ];
  const nativeAlpha = (1 - smooth(s, 1.2, 2.1)) * out(s, 0, 0.15);
  const focus =
    beat === 3
      ? smooth(s, 1.3, 2.8)
      : beat === 4
        ? smooth(s, 5.2, 6.5)
        : beat === 5
          ? smooth(s, 1.3, 2.8)
          : 0;
  return (
    <>
      <Light t={t + 66} opacity={beat >= 3 ? 0.3 : 0.4} />
      <Caption
        chapter="03 / CANVAS & COMPOSITING"
        title={copy[beat][0]}
        detail={copy[beat][1]}
        serif={beat === 3}
        time={s}
        focus={focus}
      />
      <Stage focus={focus}>
        {beat < 2 && (
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
                boxShadow: "0 30px 100px #0009",
                transform: `rotate(${mix(-4, 0, out(t, 0, 1.3))}deg)`,
              }}
            >
              <Pic src="studio/color/0-0.png" />
              <Pic
                src="studio/subject-lift.png"
                style={{
                  opacity: beat === 1 ? smooth(s, 1.4, 3.5) : 0,
                  maskImage: `url(${staticFile("media/studio/subject.png")})`,
                  maskSize: "100% 100%",
                  maskMode: "alpha",
                }}
              />
              <div
                style={{
                  ...A,
                  inset: 0,
                  background: "#a0ffba",
                  opacity:
                    beat === 0
                      ? 0.48 * smooth(s, 0.8, 1.8)
                      : 0.18 * (1 - smooth(s, 1.1, 2)),
                  maskImage: `url(${staticFile("media/studio/subject.png")})`,
                  maskSize: "100% 100%",
                  maskMode: "alpha",
                  mixBlendMode: "screen",
                }}
              />
              <Pic
                src="studio/subject.png"
                style={{
                  opacity: beat === 0 ? out(s, 1.2, 2.4) : 0,
                  filter:
                    "drop-shadow(0 0 2px #d7ff48) drop-shadow(0 0 7px #d7ff48)",
                  objectFit: "fill",
                }}
              />
            </div>
            <div
              style={{
                ...A,
                left: 630,
                top: 220,
                width: 350,
                background: "#151c24ee",
                border: "1px solid #c1cdd333",
                padding: 24,
                borderRadius: 16,
                opacity: out(s, 1.5, 2.3),
                transform: `translateX(${70 * (1 - out(s, 1.5, 2.5))}px)`,
              }}
            >
              <Label style={{ fontSize: 17 }}>SUBJECT / MASK</Label>
              <div
                style={{
                  fontFamily: font.sans,
                  color: paper,
                  fontSize: 25,
                  marginTop: 20,
                }}
              >
                Exposure{" "}
                <span style={{ float: "right", color: lime }}>
                  {beat === 1
                    ? "+" + (0.84 * smooth(s, 1.4, 3.5)).toFixed(2)
                    : "0.00"}
                </span>
              </div>
              <div style={{ height: 3, background: "#57606b", marginTop: 30 }}>
                <div
                  style={{
                    width: `${50 + (beat === 1 ? 25 * smooth(s, 1.4, 3.5) : 0)}%`,
                    height: 3,
                    background: lime,
                  }}
                />
              </div>
              <div
                style={{
                  marginTop: 30,
                  fontFamily: font.mono,
                  fontSize: 13,
                  color: "#9faab6",
                }}
              >
                MASK + ADJUSTMENT RETAINED
              </div>
            </div>
            {nativeAlpha > 0.001 && (
              <At
                name="Actual desktop masking interaction"
                from={beat * 5}
                seconds={2.2}
              >
                <Native
                  src={
                    beat === 0
                      ? "desktop-action/subject-mask.mp4"
                      : "desktop-action/mask-exposure.mp4"
                  }
                  video
                  trim={beat === 0 ? 7.3 : 23}
                  style={{ opacity: nativeAlpha }}
                />
              </At>
            )}
          </>
        )}
        {beat === 2 && (
          <Surface style={{ left: 20, top: 10, width: 950, height: 750 }}>
            <div
              style={{
                ...A,
                left: 475 - 978 * 0.93,
                top: 375 - 1532 * 0.93,
                width: 1800 * 0.93,
                height: 2700 * 0.93,
                transform: `scale(${mix(1, 0.65, smooth(s, 3, 4.7))})`,
                transformOrigin: `${978 * 0.93}px ${1532 * 0.93}px`,
              }}
            >
              <Pic src="refine/surf.jpg" />
              <Pic
                src="refine/surf-final.png"
                style={{ opacity: smooth(s, 1.7, 2.7) }}
              />
              <div
                style={{
                  ...A,
                  inset: 0,
                  background: "#bca0ff",
                  maskImage: `url(${staticFile("media/refine/surf-mask.png")})`,
                  maskSize: "100% 100%",
                  opacity:
                    0.7 * smooth(s, 0.2, 1.1) * (1 - smooth(s, 1.6, 2.2)),
                }}
              />
            </div>
            <Label
              style={{
                ...A,
                left: 25,
                bottom: 25,
                background: "#11171bd9",
                padding: 15,
                fontSize: 18,
              }}
            >
              {s < 1.7
                ? "BRUSH THE SELECTION"
                : s < 3
                  ? "LOCAL INPAINT"
                  : "DISTRACTION REMOVED"}
            </Label>
          </Surface>
        )}
        {beat === 3 && (
          <>
            <Surface
              style={{
                left: 25,
                top: 65,
                width: 950,
                height: 712,
                opacity: 1 - smooth(s, 1.4, 2.4),
                transform: `scale(${mix(1, 1.2, smooth(s, 0, 2.4))})`,
              }}
            >
              <Pic src="creative-launch/trail-shoe-source.png" />
            </Surface>
            <div
              style={{
                ...A,
                left: 210,
                top: -10,
                width: 600,
                height: 750,
                transform: `perspective(1800px) rotateY(${mix(20, -5, smooth(s, 1.4, 3))}deg) rotateZ(${mix(7, -3, smooth(s, 1.4, 3))}deg) scale(.6)`,
                transformOrigin: "0 0",
                opacity: smooth(s, 1.3, 2.1),
                boxShadow: "0 45px 90px #0008",
              }}
            >
              <CampaignPoster build={out(s, 1.4, 3.2)} brush={0} />
            </div>
          </>
        )}
        {beat === 4 && (
          <>
            <div
              style={{
                ...A,
                left: 210,
                top: -10,
                width: 600,
                height: 750,
                transform: `perspective(1800px) rotateY(${4 * Math.sin(s * 0.8)}deg) rotateZ(-3deg) scale(.6)`,
                transformOrigin: "0 0",
              }}
            >
              <CampaignPoster build={1} brush={smooth(s, 0.2, 2.2)} />
            </div>
            {s > 2 && (
              <div
                style={{
                  ...A,
                  left: 60,
                  top: 260,
                  width: 900,
                  display: "flex",
                  gap: 40,
                  transform: `translateY(${80 * (1 - out(s, 2, 2.7))}px) rotate(-6deg)`,
                  opacity: out(s, 2, 2.7) * (1 - smooth(s, 4.8, 5.5)),
                }}
              >
                {[font.display, font.serif, font.sans].map((family, i) => (
                  <div
                    key={family}
                    style={{
                      width: 250,
                      height: 170,
                      background: i === 1 ? "#2644cc" : paper,
                      color: i === 1 ? paper : "#142041",
                      display: "flex",
                      alignItems: "center",
                      justifyContent: "center",
                      fontFamily: family,
                      fontSize: 95,
                      fontStyle: i === 1 ? "italic" : "normal",
                      boxShadow: "0 20px 60px #0008",
                    }}
                  >
                    Aa
                  </div>
                ))}
              </div>
            )}
            {s < 1.8 && (
              <At name="Real editable text in Looksy" from={22} seconds={1.8}>
                <Native
                  src="desktop-action/poster-type.mp4"
                  video
                  trim={35}
                  style={{
                    opacity: 1,
                    clipPath: `circle(${1080 * (1 - smooth(s, 1.1, 1.8))}px at 85% 50%)`,
                  }}
                />
              </At>
            )}
            <Pills
              items={["BRUSHES", "TEXT", "LAYERS"]}
              style={{ ...A, left: 130, top: 770 }}
            />
          </>
        )}
        {beat === 5 && (
          <>
            <div
              style={{
                ...A,
                left: 0,
                top: 80,
                width: 1000,
                height: 562,
                transform: `perspective(1400px) rotateY(${mix(-12, 0, out(s, 0, 1.5))}deg) rotateZ(${mix(-4, 0, out(s, 0, 1.5))}deg)`,
                boxShadow: "0 35px 90px #0008",
              }}
            >
              <CampaignThumbnail build={out(s, 0.1, 1.9)} />
            </div>
            <Pills
              items={["THUMBNAIL", "WEBSITE GRAPHIC", "YOUR COMPOSITION"]}
              style={{ ...A, left: 25, top: 695, opacity: out(s, 1.3, 2.1) }}
            />
          </>
        )}
      </Stage>
      <Footer
        left={
          beat >= 3
            ? "POSTERS. THUMBNAILS. YOUR NEXT IDEA."
            : "PRECISION WHERE IT MATTERS."
        }
        right="LOOKSY / CANVAS"
      />
    </>
  );
};

export const CarouselChapter = () => {
  const { t, portrait, width } = useShot();
  const split = smooth(t, 5, 8.5),
    swipe = smooth(t, 13.5, 17.5);
  const sc = portrait ? 0.27 : 0.36;
  const panelW = 1080 * sc,
    panelH = 1350 * sc,
    gap = mix(0, 36, split);
  return (
    <>
      <Light t={104 + t} opacity={0.35} />
      <div
        style={{
          ...A,
          left: portrait ? 62 : 82,
          top: portrait ? 160 : 75,
          width: portrait ? 950 : 1700,
        }}
      >
        <Label>04 / CAROUSEL</Label>
        <Type size={portrait ? 105 : 100} style={{ marginTop: 25 }}>
          {t < 6
            ? "Some ideas need more room."
            : t < 14
              ? "One photograph. Three canvases."
              : "Made to keep swiping."}
        </Type>
        <div
          style={{
            fontFamily: font.sans,
            fontSize: portrait ? 32 : 28,
            marginTop: 23,
            color: "#c2c9d1",
          }}
        >
          Split a photograph across social canvases. Keep the design connected.
        </div>
      </div>
      <div
        style={{
          ...A,
          left: (width - (3 * panelW + 2 * gap)) / 2,
          top: portrait ? 730 : 360,
          transform: `translateX(${-swipe * (portrait ? panelW * 0.25 : 0)}px)`,
          perspective: 1800,
        }}
      >
        {[0, 1, 2].map((i) => (
          <div
            key={i}
            style={{
              ...A,
              left: i * (panelW + gap),
              top: i === 1 ? -10 * split : 12 * split,
              width: panelW,
              height: panelH,
              overflow: "hidden",
              borderRadius: 8 * split,
              boxShadow: split ? "0 30px 70px #0008" : "none",
              transform: `rotateY(${(i - 1) * -7 * split}deg) rotateZ(${(i - 1) * 2 * split}deg) translateY(${50 * (1 - out(t, i * 0.12, 1.2 + i * 0.12))}px)`,
            }}
          >
            <div
              style={{
                ...A,
                left: -i * panelW,
                top: 0,
                width: 3240,
                height: 1350,
                transform: `scale(${sc})`,
                transformOrigin: "0 0",
              }}
            >
              <PanoramaArtwork />
            </div>
          </div>
        ))}
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 90 : 480,
          top: portrait ? 1240 : 915,
          display: "flex",
          gap: 12,
          color: paper,
          fontFamily: font.mono,
          fontSize: 20,
          alignItems: "center",
        }}
      >
        {[0, 1, 2].map((i) => (
          <div
            key={i}
            style={{
              width: 10,
              height: 10,
              borderRadius: 10,
              background:
                Math.min(2, Math.floor(swipe * 3)) === i ? lime : "#5e6774",
            }}
          />
        ))}
        <span style={{ marginLeft: 20 }}>01 — 03 / SEPARATE 4:5 EXPORTS</span>
        <span
          style={{
            fontSize: 36,
            marginLeft: 20,
            transform: `translateX(${15 * Math.sin(t * 2)}px)`,
          }}
        >
          →
        </span>
      </div>
      {t > 14 && (
        <div
          style={{
            ...A,
            left: portrait ? 95 : 82,
            top: portrait ? 1400 : 850,
            transform: `translateY(${30 * (1 - out(t, 14, 15))}px)`,
            opacity: out(t, 14, 15),
          }}
        >
          <Pills
            items={["SHARED IMAGE", "SHARED TYPE", "INDIVIDUAL CANVASES"]}
          />
        </div>
      )}
      <Footer
        left="A CONTINUOUS STORY, ACROSS THE SEAMS."
        right="LOOKSY / CAROUSEL"
      />
    </>
  );
};
