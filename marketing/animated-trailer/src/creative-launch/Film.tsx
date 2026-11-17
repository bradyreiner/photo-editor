import {
  A,
  Pic,
  Film,
  useShot,
  smooth,
  out,
  mix,
  Light,
  Caption,
  Stage,
  Native,
  At,
  Label,
  Pills,
  Footer,
  font,
  lime,
  paper,
  PaperGrain,
} from "./Kit";

const curvePoint = (u: number) => ({
  x:
    30 * (1 - u) ** 3 +
    3 * 280 * (1 - u) ** 2 * u +
    3 * 470 * (1 - u) * u ** 2 +
    970 * u ** 3,
  y:
    175 * (1 - u) ** 3 +
    3 * 175 * (1 - u) ** 2 * u +
    3 * 15 * (1 - u) * u ** 2 +
    15 * u ** 3,
});

const FilmMontage = () => {
  const { t, portrait } = useShot();
  return (
    <>
      <At from={0} seconds={3.3} name="Nepal arrival">
        <Film src="refine/arrival.mp4" trim={1} />
      </At>
      <At from={3.3} seconds={3.2} name="Mountain light">
        <Film src="MVI_5051-2.3-5.9.mp4" />
      </At>
      <At from={6.5} seconds={2.7} name="Kathmandu street">
        <Film src="launch-1268.mp4" trim={2} />
      </At>
      <At from={9.2} seconds={2.8} name="Kathmandu after dark">
        <Film src="refine/night.mp4" trim={1} />
      </At>
      <div
        style={{
          ...A,
          inset: 0,
          background: "linear-gradient(0deg,#08121a99,transparent 60%)",
        }}
      />
      <div
        style={{
          ...A,
          left: portrait ? 80 : 100,
          right: portrait ? 80 : 100,
          bottom: portrait ? 190 : 120,
          color: paper,
        }}
      >
        <Label style={{ fontSize: 17, color: paper }}>
          27° N / A FIELD FILM
        </Label>
        <div
          style={{
            fontFamily: font.serif,
            fontStyle: "italic",
            fontSize: portrait ? 100 : 130,
            letterSpacing: -3,
            marginTop: 10,
            transform: `translateY(${50 * (1 - out(t, 0.5, 1.5))}px)`,
            opacity: out(t, 0.5, 1.5),
          }}
        >
          After the ridgeline.
        </div>
      </div>
    </>
  );
};

export const TimelineArt = ({ t }: { t: number }) => (
  <div
    style={{
      ...A,
      left: 0,
      top: 495,
      width: 1000,
      height: 265,
      background: "#111823",
      borderRadius: 13,
      padding: 24,
      boxSizing: "border-box",
      border: "1px solid #8999ab44",
    }}
  >
    <div
      style={{
        display: "flex",
        justifyContent: "space-between",
        fontFamily: font.mono,
        color: "#b6bfce",
        fontSize: 14,
        marginBottom: 18,
      }}
    >
      <span>THE FIELD FILM / LAYERED EDIT</span>
      <span>
        00:00:{String(Math.floor(t * 2)).padStart(2, "0")}:
        {String(Math.floor(((t * 2) % 1) * 24)).padStart(2, "0")}
      </span>
    </div>
    <div
      style={{
        height: 28,
        background: "#6753a6",
        borderRadius: 3,
        width: "78%",
        marginLeft: 80,
        fontFamily: font.mono,
        fontSize: 13,
        color: paper,
        paddingLeft: 18,
        paddingTop: 6,
        boxSizing: "border-box",
        transform: `scaleX(${out(t, 0, 0.7)})`,
        transformOrigin: "0 0",
      }}
    >
      TITLE / AFTER THE RIDGELINE
    </div>
    <div style={{ display: "flex", gap: 5, height: 77, marginTop: 8 }}>
      {[
        "arrival-first.jpg",
        "ridge-thumb.jpg",
        "city-delivery.jpg",
        "night-thumb.jpg",
      ].map((src, i) => (
        <div
          key={src}
          style={{
            position: "relative",
            width: [240, 190, 280, 225][i],
            height: 77,
            overflow: "hidden",
            borderRadius: 4,
            border: "1px solid #81afc2",
            transform: `translateY(${60 * (1 - out(t, 0.15 + i * 0.1, 0.7 + i * 0.1))}px)`,
          }}
        >
          <Pic src={`studio/${src}`} />
          <div
            style={{
              ...A,
              bottom: 0,
              left: 0,
              right: 0,
              background: "#183a4bd1",
              padding: 4,
              color: paper,
              fontFamily: font.mono,
              fontSize: 11,
            }}
          >
            0{i + 1} / {src.split("-")[0].toUpperCase()}
          </div>
        </div>
      ))}
    </div>
    <div
      style={{
        marginTop: 8,
        height: 48,
        background: "#246251",
        borderRadius: 3,
        overflow: "hidden",
      }}
    >
      <svg viewBox="0 0 1000 48" width="100%" height="48">
        {Array.from({ length: 130 }, (_, i) => (
          <rect
            key={i}
            x={i * 7.7}
            y={24 - (4 + Math.abs(Math.sin(i * 7.4)) * 16)}
            width="3"
            height={8 + Math.abs(Math.sin(i * 7.4)) * 32}
            fill="#a2e6ca"
          />
        ))}
      </svg>
    </div>
    <div
      style={{
        ...A,
        left: 35 + ((t * 90) % 920),
        top: 54,
        bottom: 15,
        width: 2,
        background: lime,
      }}
    />
  </div>
);

export const FilmChapter = () => {
  const { t, portrait } = useShot();
  const beat =
      t < 12
        ? 0
        : t < 18
          ? 1
          : t < 24
            ? 2
            : t < 31
              ? 3
              : t < 36
                ? 4
                : t < 42
                  ? 5
                  : 6,
    s = t - [0, 12, 18, 24, 31, 36, 42][beat];
  const copy = [
    [
      "Make the\nwhole film.",
      "From the first assembly to the final delivery. A video timeline built into Looksy.",
    ],
    [
      "Build the edit.\nLayer the story.",
      "Video, titles and audio. Arrange the production on a real desktop timeline.",
    ],
    [
      "Give every\nframe intent.",
      "Keyframes, easing and animation. Make motion part of the edit.",
    ],
    [
      "The look is\npart of the story.",
      "Grade the image. Stack effects. Work with Log and Rec.709 color support.",
    ],
    [
      "Let it\nsound right.",
      "Shape the soundtrack alongside your pictures, titles and effects.",
    ],
    [
      "Change your\nperspective.",
      "Gaussian scenes and camera movement. Another dimension in the creative toolbox.",
    ],
    [
      "A film.\nA social cut.\nBoth yours.",
      "Full productions and vertical videos. 4K workflows and ProRes delivery.",
    ],
  ];
  if (beat === 0)
    return (
      <>
        <div style={{ ...A, inset: 0, overflow: "hidden" }}>
          <At
            from={0}
            seconds={12}
            name="Full-screen finished Nepal field film"
          >
            <FilmMontage />
          </At>
        </div>
        <div
          style={{ ...A, left: portrait ? 80 : 100, top: portrait ? 160 : 75 }}
        >
          <Label style={{ color: paper }}>05 / MAKE THE WHOLE FILM.</Label>
          <div
            style={{
              fontFamily: font.sans,
              fontSize: portrait ? 37 : 32,
              color: paper,
              marginTop: 20,
              opacity: 1 - smooth(s, 3.4, 4.5),
            }}
          >
            A desktop timeline. From first cut to final frame.
          </div>
        </div>
      </>
    );
  return (
    <>
      <Light t={124 + t} opacity={0.4} />
      <Caption
        chapter="05 / VIDEO TIMELINE"
        title={copy[beat][0]}
        detail={copy[beat][1]}
        serif={beat === 5}
        time={s}
      />
      <Stage>
        {beat === 1 && (
          <>
            <div
              style={{
                ...A,
                left: 150,
                top: 10,
                width: 700,
                height: 394,
                overflow: "hidden",
                borderRadius: 8,
                transform: `translateY(${mix(80, 0, out(s, 0, 1))}px)`,
              }}
            >
              <At from={12} seconds={6} name="Timeline preview footage">
                <Film src="refine/arrival.mp4" trim={0} />
              </At>
            </div>
            <TimelineArt t={s} />
            {s < 2.5 && (
              <At from={12} seconds={2.5} name="Actual Looksy desktop timeline">
                <Native
                  src="desktop-action/timeline-build.mp4"
                  video
                  trim={35}
                  style={{ opacity: 1 - smooth(s, 1.6, 2.5) }}
                />
              </At>
            )}
          </>
        )}
        {beat === 2 && (
          <>
            <div
              style={{
                ...A,
                left: 0,
                top: 15,
                width: 1000,
                height: 562,
                overflow: "hidden",
                borderRadius: 10,
              }}
            >
              <At from={18} seconds={6} name="Keyframe motion preview">
                <Film src="refine/hike.mp4" trim={2} />
              </At>
              <div style={{ ...A, inset: 0, background: "#10243933" }} />
              <div
                style={{
                  ...A,
                  left: mix(-650, 58, smooth(s, 0.5, 2.3)),
                  top: mix(330, 230, smooth(s, 0.5, 2.3)),
                  fontFamily: font.display,
                  color: paper,
                  fontSize: 135,
                  lineHeight: 0.95,
                  letterSpacing: -2,
                  transform: `scale(${mix(0.7, 1, smooth(s, 0.5, 2.3))})`,
                  transformOrigin: "0 0",
                }}
              >
                KEEP
                <br />
                GOING.
              </div>
              <Label
                style={{
                  ...A,
                  left: 60,
                  bottom: 25,
                  color: paper,
                  fontSize: 15,
                }}
              >
                TITLE TRANSFORM / POSITION + SCALE
              </Label>
            </div>
            <svg
              viewBox="0 0 1000 200"
              style={{ ...A, left: 0, top: 590, width: 1000, height: 200 }}
            >
              <path d="M30 175H970M30 175V15" stroke="#6e7b89" fill="none" />
              <defs>
                <clipPath id="motion-curve-progress">
                  <rect
                    width={curvePoint(smooth(s, 0.5, 2.3)).x}
                    height={200}
                  />
                </clipPath>
              </defs>
              <path
                d="M30 175C280 175 470 15 970 15"
                stroke="#455468"
                strokeWidth="2"
                fill="none"
              />
              <path
                d="M30 175C280 175 470 15 970 15"
                stroke={lime}
                strokeWidth="4"
                fill="none"
                clipPath="url(#motion-curve-progress)"
              />
              {[0, 1, 2, 3].map((i) => (
                <path
                  key={i}
                  d={`M${curvePoint(i / 3).x} ${curvePoint(i / 3).y - 9}l9 9-9 9-9-9z`}
                  fill={paper}
                />
              ))}
              <circle
                cx={curvePoint(smooth(s, 0.5, 2.3)).x}
                cy={curvePoint(smooth(s, 0.5, 2.3)).y}
                r="8"
                fill={lime}
              />
            </svg>
          </>
        )}
        {beat === 3 && (
          <>
            <div
              style={{
                ...A,
                left: 0,
                top: 35,
                width: 1000,
                height: 562,
                overflow: "hidden",
                borderRadius: 10,
              }}
            >
              <At
                from={24}
                seconds={7}
                name="Native Looksy grade and monochrome effect"
              >
                <Film src="studio/city-grade.mp4" trim={0} />
                <div
                  style={{
                    ...A,
                    inset: 0,
                    clipPath: `inset(0 ${100 * (1 - smooth(s, 1.8, 4.2))}% 0 0)`,
                  }}
                >
                  <Film src="studio/city-mono.mp4" />
                </div>
              </At>
              <At
                from={30}
                seconds={1}
                name="Hold the final native effect frame"
              >
                <Pic src="creative-launch/city-mono-last.jpg" />
              </At>
              <Label
                style={{
                  ...A,
                  left: 26,
                  bottom: 26,
                  color: paper,
                  fontSize: 18,
                  background: "#101620b3",
                  padding: 15,
                }}
              >
                {s < 3 ? "LOOKSY / COLOR GRADE" : "LOOKSY / MONOCHROME EFFECT"}
              </Label>
            </div>
            <Pills
              items={["LOG INPUT", "Rec.709", "EFFECT LAYERS"]}
              style={{ ...A, left: 25, top: 640 }}
            />
            <div
              style={{
                ...A,
                left: 25,
                top: 730,
                fontFamily: font.mono,
                fontSize: 16,
                color: "#8b98a7",
              }}
            >
              INPUT COLOR → LOOKSY GRADE → DELIVERY
            </div>
          </>
        )}
        {beat === 4 && (
          <>
            <div
              style={{
                ...A,
                left: 70,
                top: 0,
                width: 860,
                height: 485,
                overflow: "hidden",
                borderRadius: 9,
              }}
            >
              <At from={31} seconds={5} name="Film and audio preview">
                <Film src="refine/night.mp4" />
              </At>
            </div>
            {["MUSIC", "ATMOSPHERE", "ACCENTS"].map((label, i) => (
              <div
                key={label}
                style={{
                  ...A,
                  left: i * 25,
                  top: 510 + i * 83,
                  width: 950,
                  height: 65,
                  background: ["#355b88", "#2a6554", "#645083"][i],
                  borderRadius: 5,
                  overflow: "hidden",
                  transform: `translateX(${200 * (1 - out(s, i * 0.15, 0.8 + i * 0.15))}px)`,
                }}
              >
                <Label
                  style={{
                    ...A,
                    left: 15,
                    top: 11,
                    fontSize: 12,
                    color: paper,
                  }}
                >
                  {label}
                </Label>
                <svg
                  style={{ ...A, left: 130, top: 10, width: 800, height: 45 }}
                  viewBox="0 0 800 45"
                >
                  {Array.from({ length: 100 }, (_, n) => (
                    <rect
                      key={n}
                      x={n * 8}
                      y={22 - (3 + Math.abs(Math.sin(n * (i + 1.7))) * 15)}
                      width="3"
                      height={6 + Math.abs(Math.sin(n * (i + 1.7))) * 30}
                      fill="#d3e8ed"
                    />
                  ))}
                </svg>
              </div>
            ))}
            <div
              style={{
                ...A,
                left: 100 + s * 130,
                top: 510,
                height: 230,
                width: 2,
                background: lime,
              }}
            />
          </>
        )}
        {beat === 5 && (
          <div
            style={{
              ...A,
              left: 0,
              top: 25,
              width: 1000,
              height: 665,
              overflow: "hidden",
              borderRadius: 14,
            }}
          >
            <At
              from={36}
              seconds={6}
              name="Genuine Gaussian splat camera render"
            >
              <Film src="studio/gaussian-camera.mp4" trim={3} />
            </At>
            <Label style={{ ...A, left: 25, bottom: 25, fontSize: 17 }}>
              113,648 SPLATS / LOOKSY METAL RENDERER
            </Label>
          </div>
        )}
        {beat === 6 && (
          <>
            <div
              style={{
                ...A,
                left: 0,
                top: 75,
                width: 830,
                height: 467,
                overflow: "hidden",
                borderRadius: 10,
                transform: `perspective(1400px) rotateY(-7deg) translateX(${-40 * (1 - out(s, 0, 1.4))}px)`,
                boxShadow: "0 30px 80px #0009",
              }}
            >
              <At from={42} seconds={8} name="Landscape field film">
                <Film src="refine/arrival.mp4" />
                <div
                  style={{
                    ...A,
                    left: 30,
                    bottom: 40,
                    fontFamily: font.serif,
                    fontStyle: "italic",
                    fontSize: 70,
                    color: paper,
                  }}
                >
                  A little further.
                </div>
              </At>
            </div>
            <div
              style={{
                ...A,
                left: 690,
                top: 240,
                width: 260,
                height: 462,
                overflow: "hidden",
                borderRadius: 18,
                border: "5px solid #c5cbd0",
                boxShadow: "0 30px 70px #0009",
                transform: `translateY(${100 * (1 - out(s, 0.4, 1.5))}px) rotate(5deg)`,
              }}
            >
              <At
                from={42}
                seconds={8}
                name="Independently framed vertical cut"
              >
                <Film
                  src="refine/arrival.mp4"
                  style={{ objectPosition: "56% 50%" }}
                />
                <div
                  style={{
                    ...A,
                    left: 15,
                    right: 15,
                    top: 60,
                    fontFamily: font.display,
                    fontSize: 58,
                    lineHeight: 1,
                    color: paper,
                  }}
                >
                  A LITTLE
                  <br />
                  FURTHER.
                </div>
              </At>
            </div>
            <Pills
              items={["4K", "PRORES", "LANDSCAPE + VERTICAL"]}
              style={{ ...A, left: 10, top: 745 }}
            />
          </>
        )}
      </Stage>
      <PaperGrain opacity={0.055} />
      <Footer
        left={
          beat === 5
            ? "HOUSEPLANT SCENE / MARCEL PADILLA / CC BY 4.0"
            : "FROM FIRST CUT TO FINAL FRAME."
        }
        right={portrait ? "LOOKSY / FILM" : "COLOR · MOTION · LAYERS · AUDIO"}
      />
    </>
  );
};
