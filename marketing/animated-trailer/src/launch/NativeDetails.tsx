import { A, C, mono, p } from "./design";
import { appUiFont } from "../app-kit/AppElements";
import { mixerEnvelope } from "./meter-data";

// PhotoTypographyPanel's two-column ToneTextField spacing grid. Values commit
// on editingFinished; this is deliberately not an invented tracking slider.
export const TypographyFields = ({ t }: { t: number }) => {
  const focused = t > 1.1 && t < 2.4,
    selected = t > 1.15 && t < 1.65;
  const text = t < 1.65 ? "-26" : t < 1.8 ? "-" : t < 1.95 ? "-1" : "-10";
  return (
    <div
      style={{
        display: "grid",
        gridTemplateColumns: "1fr 1fr",
        fontFamily: appUiFont,
        gap: 26,
      }}
    >
      {[
        ["Letter spacing · px", text],
        ["Line height · × size", "0.8611"],
      ].map(([label, value], i) => (
        <div key={label}>
          <div style={{ fontSize: 33, color: C.textMuted, marginBottom: 13 }}>
            {label}
          </div>
          <div
            style={{
              height: 84,
              display: "flex",
              alignItems: "center",
              padding: "0 24px",
              fontFamily: mono,
              fontSize: 39,
              background: C.ink850,
              border: `${i === 0 && focused ? 2 : 1}px solid ${i === 0 && focused ? C.brandPaper : C.lineStrong}`,
              borderRadius: 9,
            }}
          >
            <span
              style={{
                background: i === 0 && selected ? "#c4c4c4" : "transparent",
                color: i === 0 && selected ? C.ink950 : C.brandPaper,
              }}
            >
              {value}
            </span>
            {i === 0 && focused && t < 2.05 && (
              <span
                style={{
                  height: 43,
                  width: 2,
                  background: C.brandPaper,
                  marginLeft: 2,
                }}
              />
            )}
          </div>
        </div>
      ))}
    </div>
  );
};

const db = (value: number) =>
  Math.max(-60, 20 * Math.log10(Math.max(0.000001, value)));
const normalized = (value: number) =>
  Math.max(0, Math.min(1, (db(value) + 60) / 60));
const at = (track: "ambience" | "music", t: number) => {
  const a = mixerEnvelope[track],
    index = Math.min(a.length - 1, Math.max(0, Math.floor(t * 20)));
  return a[index];
};
// TimelineAudioMixer / TimelineAudioMeter source geometry: -60..+24 fader,
// 36×13 handle, 4px rail, paired 14px dB-scaled meters and neutral level color.
export const NativeMixer = ({ t }: { t: number }) => {
  const gain = Math.round(p(t, 15.5, 16.7, -6, 0) * 2) / 2,
    amp = 10 ** (gain / 20),
    amb = at("ambience", t - 14.8).map((v) => v * amp),
    music = at("music", t - 14.8);
  const master = [
    Math.max(amb[0], music[0]),
    Math.max(amb[1], music[1]),
    Math.hypot(amb[2], music[2]),
    Math.hypot(amb[3], music[3]),
  ];
  return (
    <div
      style={{
        ...A,
        left: 80,
        top: 410,
        width: 920,
        height: 1260,
        fontFamily: appUiFont,
        background: C.ink900,
        padding: "32px 20px",
        boxSizing: "border-box",
      }}
    >
      <div
        style={{
          fontSize: 35,
          padding: "0 18px 26px",
          borderBottom: "1px solid #393939",
        }}
      >
        Audio Mixer
      </div>
      {["Ambience", "Music", "Master"].map((name, i) => {
        const value = i === 0 ? gain : 0,
          meter = i === 0 ? amb : i === 1 ? music : master,
          handle = (1 - (value + 60) / 84) * (620 - 36);
        return (
          <div
            key={name}
            style={{
              ...A,
              left: 20 + i * 300,
              top: 120,
              width: 280,
              height: 1050,
              background: i === 0 ? C.ink850 : C.ink900,
              border: i === 0 ? "1px solid #585858" : "1px solid #303030",
              boxSizing: "border-box",
              padding: 20,
            }}
          >
            <div style={{ fontSize: 34, marginBottom: 18 }}>{name}</div>
            <div
              style={{
                display: "flex",
                gap: 12,
                fontSize: 25,
                color: C.textMuted,
              }}
            >
              <span
                style={{ border: "1px solid #585858", padding: "8px 15px" }}
              >
                M
              </span>
              <span
                style={{ border: "1px solid #585858", padding: "8px 15px" }}
              >
                S
              </span>
              <span style={{ marginLeft: "auto", paddingTop: 8 }}>OK</span>
            </div>
            <div style={{ ...A, left: 42, top: 190, width: 104, height: 620 }}>
              <div
                style={{
                  ...A,
                  left: 45,
                  top: 0,
                  width: 11,
                  height: 620,
                  background: "#58585866",
                  borderRadius: 5,
                }}
              />
              <div
                style={{
                  ...A,
                  left: 45,
                  bottom: 0,
                  width: 11,
                  height: (620 * (value + 60)) / 84,
                  background: "#c4c4c4",
                  opacity: 0.74,
                  borderRadius: 5,
                }}
              />
              {[24, 0, -24, -60].map((mark) => (
                <div
                  key={mark}
                  style={{
                    ...A,
                    left: 0,
                    right: 0,
                    top: (1 - (mark + 60) / 84) * 620,
                    height: 1,
                    background: "#b5b5b588",
                  }}
                />
              ))}
              <div
                style={{
                  ...A,
                  left: 0,
                  top: handle,
                  width: 101,
                  height: 36,
                  background: C.brandPaper,
                  borderRadius: 8,
                  border:
                    i === 0 && t > 15.5 && t < 16.7
                      ? "3px solid white"
                      : "2px solid #a5a5a5",
                }}
              />
              {i === 0 && t > 15.5 && t < 16.8 && (
                <svg
                  width={32}
                  height={43}
                  viewBox="0 0 20 26"
                  style={{
                    ...A,
                    left: 61,
                    top: handle + 23,
                    filter: "drop-shadow(0 1px 2px #0008)",
                  }}
                >
                  <path
                    d="M2 1V20L7 15L11 24L15 22L11 13H18Z"
                    fill="white"
                    stroke="#111"
                    strokeWidth={1.2}
                  />
                </svg>
              )}
            </div>
            <div
              style={{
                ...A,
                left: 164,
                top: 190,
                height: 620,
                display: "flex",
                gap: 8,
              }}
            >
              {[0, 1].map((channel) => (
                <div
                  key={channel}
                  style={{
                    height: 620,
                    width: 27,
                    position: "relative",
                    background: C.ink950,
                    borderRadius: 5,
                    overflow: "hidden",
                  }}
                >
                  <div
                    style={{
                      ...A,
                      left: 0,
                      right: 0,
                      bottom: 0,
                      height: 620 * normalized(meter[channel]),
                      background:
                        db(meter[channel]) > -6 ? "#d0b178" : "#c4c4c4",
                    }}
                  />
                  <div
                    style={{
                      ...A,
                      left: 0,
                      right: 0,
                      top: 620 * (1 - normalized(meter[channel])),
                      height: 3,
                      background: C.brandPaper,
                    }}
                  />
                </div>
              ))}
            </div>
            <div
              style={{
                ...A,
                left: 20,
                right: 20,
                top: 855,
                height: 70,
                display: "grid",
                placeItems: "center",
                fontFamily: mono,
                fontSize: 31,
                background: C.ink950,
                border: "1px solid #585858",
              }}
            >
              {value.toFixed(1)} dB
            </div>
            <div
              style={{
                ...A,
                left: 20,
                right: 20,
                top: 955,
                fontFamily: mono,
                fontSize: 25,
                color: C.textMuted,
              }}
            >
              P {db(Math.max(meter[0], meter[1])).toFixed(1)}
              <br />R {db(Math.max(meter[2], meter[3])).toFixed(1)}
            </div>
          </div>
        );
      })}
      <div
        style={{ ...A, left: 38, bottom: 26, fontSize: 27, color: C.textMuted }}
      >
        Track gain / pan / mute / solo / processors
      </div>
    </div>
  );
};
