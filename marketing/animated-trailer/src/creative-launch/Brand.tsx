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
  Type,
  Footer,
  font,
  lime,
  paper,
  AppWordmark,
  PaperGrain,
} from "./Kit";
import { CampaignPoster } from "./Canvas";

export const Opening = () => {
  const { t, width, height, portrait } = useShot();
  const reveal = smooth(t, 8.5, 10.2);
  return (
    <>
      <Light t={t} gather={1 - smooth(t, 2, 5)} opacity={0.8} />
      <div
        style={{
          ...A,
          inset: 0,
          background:
            "radial-gradient(ellipse at 50% 43%,transparent,#0b0d1290)",
        }}
      />
      <div
        style={{
          ...A,
          left: portrait ? 95 : 130,
          top: portrait ? 190 : 110,
          opacity: 1 - reveal,
          transform: `translateY(${35 * (1 - out(t, 0, 0.8))}px)`,
        }}
      >
        <Label>MEET YOUR NEW DESKTOP STUDIO.</Label>
        <div style={{ marginTop: 26 }}>
          <AppWordmark width={portrait ? 820 : 610} />
        </div>
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 70 : 130,
          top: portrait ? 620 : 400,
          opacity: (1 - reveal) * out(t, 2.2, 2.6),
          width: portrait ? 950 : 850,
        }}
      >
        {["PHOTO.", "DESIGN.", "FILM."].map((word, i) => (
          <div key={word} style={{ overflow: "hidden" }}>
            <Type
              size={portrait ? 146 : 125}
              style={{
                transform: `translateY(${150 * (1 - out(t, 2.2 + i * 0.18, 3 + i * 0.18))}px)`,
                color: i === 1 ? lime : paper,
              }}
            >
              {word}
            </Type>
          </div>
        ))}
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 520 : 1090,
          top: portrait ? 1060 : 200,
          width: 600,
          height: 720,
          perspective: 1400,
          opacity: (1 - reveal) * out(t, 2.4, 3.1),
          transform: `translateY(${60 * (1 - out(t, 2.4, 3.6))}px)`,
        }}
      >
        <div
          style={{
            ...A,
            left: -60,
            top: 80,
            width: 440,
            height: 330,
            overflow: "hidden",
            rotate: "-12deg",
            transform: `translateY(${-10 * Math.sin(t)}px) rotateY(-13deg)`,
            boxShadow: "0 25px 60px #0009",
          }}
        >
          <Pic src="studio/raw-colour/4-8.png" />
        </div>
        <div
          style={{
            ...A,
            left: 170,
            top: -30,
            width: 250,
            height: 313,
            transform: "rotate(8deg) scale(.25)",
            transformOrigin: "0 0",
            boxShadow: "0 30px 80px #0009",
          }}
        >
          <CampaignPoster />
        </div>
        <div
          style={{
            ...A,
            left: 70,
            top: 400,
            width: 500,
            height: 281,
            overflow: "hidden",
            rotate: "7deg",
            boxShadow: "0 25px 70px #0009",
            transform: `translateX(${14 * Math.sin(t * 0.8)}px)`,
          }}
        >
          <Pic src="refine/finished-film.jpg" />
          <Type serif size={63} style={{ ...A, left: 25, bottom: 25 }}>
            A little further.
          </Type>
        </div>
      </div>
      {t > 8.5 && (
        <div
          style={{
            ...A,
            inset: 0,
            clipPath: `circle(${reveal * Math.max(width, height)}px at 62% 53%)`,
          }}
        >
          <Light t={t} opacity={0.2} />
          <div
            style={{
              ...A,
              left: portrait ? 40 : 285,
              top: portrait ? 580 : 215,
              width: portrait ? 1000 : 1350,
              height: portrait ? 585 : 790,
              transform: `perspective(1600px) rotateY(${mix(-12, 0, reveal)}deg) scale(${mix(0.85, 1, reveal)})`,
            }}
          >
            <Native
              src="desktop-launch/raw-editor.png"
              style={{ top: 0, width: "100%", height: "100%" }}
            />
          </div>
          <div
            style={{ ...A, left: portrait ? 62 : 82, top: portrait ? 180 : 70 }}
          >
            <Label>BUILT FROM THE GROUND UP.</Label>
            <Type size={portrait ? 98 : 78} style={{ marginTop: 25 }}>
              Photo. Design. Film. One app.
            </Type>
          </div>
        </div>
      )}
      <Footer
        left="BUILT FOR THE WORK YOU WANT TO MAKE."
        right="LOOKSY / DESKTOP"
      />
    </>
  );
};

export const AgentChapter = () => {
  const { t } = useShot();
  const beat = Math.min(3, Math.floor(t / 6)),
    s = t - beat * 6;
  const copy = [
    [
      "An extra pair\nof hands.\nIf you want it.",
      "Connect a local language model for optional workflow assistance.",
    ],
    [
      "Keep creating.\nShare the busywork.",
      "Ask for help organizing a shoot or preparing batch edits. The model runs on your machine.",
    ],
    [
      "The work stays\neditable.",
      "Layers, masks and adjustments remain available for your own refinement.",
    ],
    [
      "Your call.\nAt every step.",
      "Review the result. Change an adjustment. Return to the edit history.",
    ],
  ];
  return (
    <>
      <Light t={174 + t} opacity={0.43} gather={0.65} />
      <Caption
        chapter="06 / OPTIONAL LOCAL AGENT"
        title={copy[beat][0]}
        detail={copy[beat][1]}
        time={s}
      />
      <Stage>
        {beat < 2 && (
          <>
            <svg
              style={{ ...A, inset: 0, width: 1000, height: 800 }}
              viewBox="0 0 1000 800"
            >
              <rect
                x="80"
                y="100"
                width="840"
                height="530"
                rx="30"
                fill="#121b29"
                stroke="#cbd3db66"
                strokeWidth="2"
              />
              <path
                d="M380 630L360 700H640L620 630"
                fill="#202d40"
                stroke="#cbd3db44"
              />
              <rect
                x="120"
                y="140"
                width="760"
                height="450"
                rx="15"
                fill="#0b111c"
              />
              {[0, 1, 2].map((i) => (
                <path
                  key={i}
                  d={`M500 365Q${200 + i * 300} 365 ${[240, 500, 760][i]} 240`}
                  stroke={["#e18a69", "#a8d8a5", "#8588ff"][i]}
                  strokeWidth="3"
                  fill="none"
                  pathLength="1"
                  strokeDasharray="1"
                  strokeDashoffset={1 - out(s, 0.5 + i * 0.3, 1.7 + i * 0.3)}
                />
              ))}
              <rect
                x="410"
                y="315"
                width="180"
                height="120"
                rx="20"
                fill="#253951"
                stroke={lime}
              />
            </svg>
            <div
              style={{
                ...A,
                left: 415,
                top: 347,
                width: 170,
                textAlign: "center",
                fontFamily: font.mono,
                fontSize: 24,
                color: lime,
              }}
            >
              LOCAL
              <br />
              MODEL
            </div>
            {["ORGANIZE", "BATCH ASSIST", "REFINE"].map((v, i) => (
              <div
                key={v}
                style={{
                  ...A,
                  left: 145 + i * 260,
                  top: 195,
                  width: 190,
                  height: 95,
                  borderRadius: 14,
                  background: ["#b97557", "#3c7458", "#535aa3"][i],
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                  fontFamily: font.mono,
                  fontSize: 18,
                  color: paper,
                  opacity: out(s, 0.4 + i * 0.2, 1.1 + i * 0.2),
                  transform: `translateY(${45 * (1 - out(s, 0.4 + i * 0.2, 1.1 + i * 0.2))}px)`,
                }}
              >
                {v}
              </div>
            ))}
            <Pills
              items={["OPTIONAL", "ON YOUR MACHINE", "YOU REVIEW"]}
              style={{ ...A, left: 150, top: 750 }}
            />
          </>
        )}
        {beat === 2 && (
          <>
            <Native
              src="desktop-launch/canvas.png"
              style={{
                top: 75,
                transform: `perspective(1600px) rotateY(${mix(-5, 0, smooth(s, 0, 6))}deg)`,
              }}
            />
            <div
              style={{
                ...A,
                left: 590,
                top: 470,
                width: 365,
                background: "#131c2a",
                padding: 24,
                borderRadius: 12,
                border: "1px solid #bfcad744",
                opacity: out(s, 2, 2.8),
                transform: `translateY(${50 * (1 - out(s, 2, 2.8))}px)`,
              }}
            >
              <Label style={{ fontSize: 17 }}>NATIVE PROJECT / 24 LAYERS</Label>
              {["Type", "Image", "Adjustment", "Mask"].map((v, i) => (
                <div
                  key={v}
                  style={{
                    fontFamily: font.sans,
                    fontSize: 23,
                    color: paper,
                    padding: "12px 0",
                    borderBottom: "1px solid #ffffff22",
                    display: "flex",
                    justifyContent: "space-between",
                  }}
                >
                  <span>{v}</span>
                  <span style={{ color: lime }}>●</span>
                  <span
                    style={{
                      fontFamily: font.mono,
                      fontSize: 12,
                      color: "#7c8799",
                    }}
                  >
                    EDITABLE / 0{i + 1}
                  </span>
                </div>
              ))}
            </div>
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
                height: 667,
                overflow: "hidden",
                borderRadius: 15,
              }}
            >
              <Pic src="studio/raw-colour/4-8.png" />
              <Pic
                src="studio/raw-colour/0-0.png"
                style={{ clipPath: `inset(0 ${100 * smooth(s, 1, 4)}% 0 0)` }}
              />
            </div>
            <div
              style={{
                ...A,
                left: 130,
                top: 620,
                width: 740,
                height: 140,
                background: "#111a28ed",
                border: "1px solid #aebbc54d",
                borderRadius: 14,
                padding: 25,
                boxSizing: "border-box",
              }}
            >
              <Label style={{ fontSize: 17 }}>
                REVIEW → ADJUST → KEEP CREATING
              </Label>
              <div
                style={{
                  position: "relative",
                  height: 3,
                  background: "#4f5f70",
                  marginTop: 35,
                }}
              >
                {[0, 1, 2, 3, 4].map((i) => (
                  <div
                    key={i}
                    style={{
                      ...A,
                      left: i * 170,
                      top: -7,
                      width: 16,
                      height: 16,
                      borderRadius: 15,
                      background:
                        i <= Math.floor(smooth(s, 1, 4) * 4) ? lime : "#c1cbd5",
                    }}
                  />
                ))}
              </div>
            </div>
          </>
        )}
      </Stage>
      <Footer
        left={
          beat < 2
            ? "OPTIONAL. LOCAL. IN YOUR CONTROL."
            : "EDITABLE WORK. RETAINED CONTROL."
        }
        right="LOOKSY / LOCAL AGENT"
      />
    </>
  );
};

export const Closing = () => {
  const { t, portrait, width, height } = useShot();
  const end = smooth(t, 11.5, 12.8);
  return (
    <>
      <Light t={280 + t} opacity={0.7} gather={smooth(t, 9, 14)} />
      <div
        style={{
          ...A,
          left: portrait ? 62 : 82,
          top: portrait ? 160 : 90,
          opacity: 1 - smooth(t, 10.2, 11.2),
        }}
      >
        <Label>PHOTO. DESIGN. FILM.</Label>
        <Type size={portrait ? 112 : 110} style={{ marginTop: 24 }}>
          Make the whole thing.
        </Type>
        <Type serif size={portrait ? 53 : 55} style={{ marginTop: 20 }}>
          Professional tools. For everyone who creates.
        </Type>
      </div>
      <div
        style={{
          ...A,
          inset: 0,
          opacity: 1 - smooth(t, 5.7, 7),
          perspective: 1800,
        }}
      >
        <div
          style={{
            ...A,
            left: portrait ? 60 : 150,
            top: portrait ? 650 : 420,
            width: portrait ? 650 : 700,
            height: portrait ? 440 : 467,
            overflow: "hidden",
            rotate: "-8deg",
            transform: `translateY(${80 * (1 - out(t, 0, 1))}px)`,
            boxShadow: "0 35px 100px #000a",
          }}
        >
          <Pic src="studio/raw-colour/4-8.png" />
        </div>
        <div
          style={{
            ...A,
            left: portrait ? 600 : 820,
            top: portrait ? 570 : 300,
            transform: `scale(${portrait ? 0.3 : 0.38}) rotate(8deg) translateY(${100 * (1 - out(t, 0.2, 1.2))}px)`,
            transformOrigin: "0 0",
            boxShadow: "0 35px 90px #000a",
          }}
        >
          <CampaignPoster />
        </div>
        <div
          style={{
            ...A,
            left: portrait ? 180 : 1170,
            top: portrait ? 1120 : 570,
            width: portrait ? 780 : 620,
            height: portrait ? 440 : 350,
            overflow: "hidden",
            rotate: "5deg",
            boxShadow: "0 35px 90px #000a",
            transform: `translateY(${120 * (1 - out(t, 0.4, 1.4))}px)`,
          }}
        >
          <At from={0} seconds={6.1} name="Closing moving field film">
            <Film src="refine/arrival.mp4" />
          </At>
          <Type
            serif
            size={portrait ? 70 : 54}
            style={{ ...A, left: 30, bottom: 30 }}
          >
            A little further.
          </Type>
        </div>
      </div>
      {t >= 5.7 && t < 12.8 && (
        <div
          style={{
            ...A,
            left: portrait ? 40 : 360,
            top: portrait ? 650 : 350,
            width: portrait ? 1000 : 1200,
            height: portrait ? 585 : 702,
            opacity: smooth(t, 5.7, 7) * (1 - smooth(t, 10.2, 11.2)),
            transform: `scale(${mix(0.92, 1, smooth(t, 6, 11))})`,
            transformOrigin: "50% 20%",
          }}
        >
          <Native
            src="desktop-launch/raw-editor.png"
            style={{ top: 0, width: "100%", height: "100%" }}
          />
        </div>
      )}
      <div
        style={{
          ...A,
          left: 0,
          top: height * 0.35,
          width,
          textAlign: "center",
          opacity: end,
          transform: `translateY(${40 * (1 - end)}px)`,
        }}
      >
        <div style={{ display: "flex", justifyContent: "center" }}>
          <AppWordmark width={portrait ? 800 : 1000} />
        </div>
        <div
          style={{
            fontFamily: font.sans,
            fontSize: portrait ? 42 : 45,
            color: paper,
            marginTop: 40,
          }}
        >
          A whole creative studio. On your desktop.
        </div>
        <Pills
          items={["PHOTO", "DESIGN", "FILM"]}
          style={{ justifyContent: "center", marginTop: 50 }}
        />
      </div>
      <PaperGrain opacity={0.12} />
      <Footer left="CREATE SOMETHING WORTH LOOKING AT." right="LOOKSY" />
    </>
  );
};
