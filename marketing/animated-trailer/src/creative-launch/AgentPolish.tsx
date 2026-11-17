import {
  A,
  Pic,
  useShot,
  out,
  Light,
  Caption,
  Stage,
  Label,
  Pills,
  Footer,
  font,
  lime,
  paper,
} from "./Kit";
import { phaseAt } from "./State";
import { EditableLayers, HikerPoster } from "./CanvasPolish";

export const AgentChapter = () => {
  const { t } = useShot(),
    { phase, s } = phaseAt(t, [0, 7, 14, 21, 28]);
  const copy = [
    [
      "An extra pair\nof hands.\nIf you want it.",
      "Connect a model running on your machine. Ask for help with the work around your ideas.",
    ],
    [
      "Keep creating.\nShare the busywork.",
      "Ask for a subject lift and a layered poster. Review the proposed steps before running them.",
    ],
    [
      "The work stays\neditable.",
      "The same photographs, text, masks and adjustments remain available for your refinement.",
    ],
    [
      "Your call.\nAt every step.",
      "Review an adjustment. Return to an earlier state. Keep control of the result.",
    ],
  ];
  const step = s < 2 ? 0 : s < 4.5 ? 2 : 1,
    exposure = [0, 0.35, 0.55][step];
  return (
    <>
      <Light t={252 + t} opacity={0.38} gather={0.6} />
      <Caption
        chapter="06 / OPTIONAL LOCAL AGENT"
        title={copy[phase][0]}
        detail={copy[phase][1]}
        time={s}
      />
      <Stage>
        {phase === 0 && (
          <>
            <svg
              viewBox="0 0 1000 800"
              style={{ ...A, inset: 0, width: 1000, height: 800 }}
            >
              <rect
                x="65"
                y="85"
                width="870"
                height="565"
                rx="25"
                fill="#111b2a"
                stroke="#a4b3c35c"
                strokeWidth="2"
              />
              <rect
                x="100"
                y="125"
                width="800"
                height="480"
                rx="12"
                fill="#0a111c"
              />
              <path d="M375 650L350 730H650L625 650" fill="#26374a" />
              <path
                d="M290 350H710"
                stroke={lime}
                strokeWidth="3"
                pathLength="1"
                strokeDasharray="1"
                strokeDashoffset={1 - out(s, 0.5, 2)}
              />
              <rect
                x="390"
                y="285"
                width="220"
                height="140"
                rx="18"
                fill="#28485d"
                stroke={lime}
              />
            </svg>
            <div
              style={{
                ...A,
                left: 410,
                top: 320,
                width: 180,
                textAlign: "center",
                fontFamily: font.mono,
                color: lime,
                fontSize: 23,
              }}
            >
              LOCAL
              <br />
              MODEL
            </div>
            <div
              style={{
                ...A,
                left: 125,
                top: 250,
                width: 220,
                height: 220,
                overflow: "hidden",
                borderRadius: 12,
                opacity: out(s, 0.4, 1),
              }}
            >
              <Pic src="studio/color/4-8.png" />
            </div>
            <div
              style={{
                ...A,
                left: 660,
                top: 250,
                width: 220,
                height: 220,
                overflow: "hidden",
                borderRadius: 12,
                opacity: out(s, 1.3, 2),
              }}
            >
              <Pic src="studio/subject-lift.png" />
            </div>
            <Pills
              items={["OPTIONAL", "RUNS ON YOUR MACHINE", "YOUR ORIGINALS"]}
              style={{ ...A, left: 110, top: 750 }}
            />
          </>
        )}
        {phase === 1 && (
          <div
            style={{
              ...A,
              left: 60,
              top: 65,
              width: 880,
              padding: 35,
              borderRadius: 18,
              background: "#101b29ed",
              border: "1px solid #a2b4c45c",
            }}
          >
            <Label style={{ fontSize: 17 }}>
              EXAMPLE PROPOSAL / REVIEW BEFORE RUNNING
            </Label>
            {[
              "Lift the subject by +0.55 EV",
              "Place the title behind the subject",
              "Keep the photo, mask and type separate",
            ].map((text, i) => (
              <div
                key={text}
                style={{
                  position: "relative",
                  display: "flex",
                  alignItems: "center",
                  gap: 25,
                  padding: "26px 0",
                  borderBottom: "1px solid #c0cbd52b",
                  opacity: out(s, 0.4 + i * 0.35, 1.1 + i * 0.35),
                  transform: `translateY(${25 * (1 - out(s, 0.4 + i * 0.35, 1.1 + i * 0.35))}px)`,
                }}
              >
                <div
                  style={{
                    position: "relative",
                    width: 150,
                    height: 100,
                    overflow: "hidden",
                    borderRadius: 8,
                  }}
                >
                  {i === 1 ? (
                    <div
                      style={{
                        ...A,
                        left: 0,
                        top: -50,
                        transform: "scale(.15)",
                        transformOrigin: "0 0",
                      }}
                    >
                      <HikerPoster />
                    </div>
                  ) : (
                    <Pic
                      src={
                        i === 0
                          ? "studio/subject-lift.png"
                          : "studio/subject.png"
                      }
                    />
                  )}
                </div>
                <div
                  style={{
                    fontFamily: font.sans,
                    fontSize: 29,
                    color: paper,
                    maxWidth: 530,
                  }}
                >
                  {text}
                  <div
                    style={{
                      fontFamily: font.mono,
                      fontSize: 13,
                      color: "#9bafc1",
                      marginTop: 12,
                    }}
                  >
                    REVIEW THE PROPOSAL
                  </div>
                </div>
              </div>
            ))}
            <Pills
              items={["CHOOSE THE SCOPE", "KEEP THE CONTROL"]}
              style={{ marginTop: 30 }}
            />
          </div>
        )}
        {phase === 2 && (
          <>
            <div
              style={{
                ...A,
                left: 190,
                top: -10,
                transform: "scale(.6) rotate(-3deg)",
                transformOrigin: "0 0",
              }}
            >
              <HikerPoster />
            </div>
            <div
              style={{
                ...A,
                left: 610,
                top: 285,
                opacity: out(s, 0.7, 1.5),
                transform: `translateX(${60 * (1 - out(s, 0.7, 1.5))}px)`,
              }}
            >
              <EditableLayers selected={Math.min(3, Math.floor(s / 1.7))} />
            </div>
          </>
        )}
        {phase === 3 && (
          <>
            <div
              style={{
                ...A,
                left: 230,
                top: -10,
                width: 530,
                height: 795,
                overflow: "hidden",
                borderRadius: 13,
              }}
            >
              <Pic src="studio/color/4-8.png" />
              <Pic
                src="studio/subject-lift.png"
                style={{ opacity: exposure / 0.55 }}
              />
            </div>
            <div
              style={{
                ...A,
                left: 45,
                top: 580,
                width: 910,
                padding: 25,
                background: "#111e2bf5",
                border: "1px solid #acbcc755",
                borderRadius: 12,
              }}
            >
              <Label style={{ fontSize: 17 }}>
                EDIT HISTORY / SUBJECT EXPOSURE +{exposure.toFixed(2)} EV
              </Label>
              <div style={{ marginTop: 30, display: "flex", gap: 20 }}>
                {["Original mask", "Your refinement", "Subject lift"].map(
                  (name, i) => (
                    <div
                      key={name}
                      style={{
                        flex: 1,
                        padding: "17px 14px",
                        fontFamily: font.sans,
                        fontSize: 20,
                        color: i === step ? "#152335" : paper,
                        background: i === step ? lime : "#283c50",
                        borderRadius: 7,
                      }}
                    >
                      {name}
                      <div
                        style={{
                          fontFamily: font.mono,
                          fontSize: 14,
                          marginTop: 10,
                        }}
                      >
                        {["0.00 EV", "+0.35 EV", "+0.55 EV"][i]}
                      </div>
                    </div>
                  ),
                )}
              </div>
            </div>
          </>
        )}
      </Stage>
      <Footer
        left="OPTIONAL ASSISTANCE. EDITABLE WORK. RETAINED CONTROL."
        right="LOOKSY / LOCAL AGENT"
      />
    </>
  );
};
