import {
  AppActivity,
  AppIcon,
  AppInspectorSlider,
} from "../app-kit/AppElements";
import {
  abs,
  enter,
  mix,
  NativeRows,
  Panel,
  Photo,
  T,
  windowAt,
  pressAt,
} from "./system";

export const SearchScene = ({ t }: { t: number }) => {
  const show = windowAt(t, 93.5, 106);
  if (!show) return null;
  const query = "person in a yellow jacket with mountains";
  const typed = query.slice(0, Math.floor(mix(t, 94.4, 97, 0, query.length)));
  const results = mix(t, 98, 100);
  return (
    <>
      <Panel
        x={72}
        y={460}
        w={936}
        style={{
          opacity: show,
          padding: "26px 28px",
          height: 113,
          background: T.ink850,
        }}
      >
        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: 20,
            fontSize: 28,
          }}
        >
          <AppIcon name="search" size={28} />
          <span>{typed}</span>
          <span style={{ color: T.textMuted }}>{t < 97 ? "|" : ""}</span>
        </div>
        <div style={{ fontSize: 16, color: T.textMuted, marginTop: 14 }}>
          Nepal collection · Photos · Local vision model
        </div>
      </Panel>
      {["IMG_5103", "IMG_5111"].map((name, i) => (
        <div
          key={name}
          style={{
            ...abs,
            left: i === 0 ? 90 : 720,
            top: 665 + enter(t, 98 + i * 0.15, 99.8, 90, 0),
            width: 270,
            height: 405,
            opacity: results * show,
            overflow: "hidden",
          }}
        >
          <Photo name={name} />
          <div
            style={{
              ...abs,
              bottom: 0,
              left: 0,
              right: 0,
              padding: 12,
              background: T.ink900,
              fontFamily: "Menlo",
              fontSize: 12,
            }}
          >
            {name}.CR2
          </div>
        </div>
      ))}
      <Panel x={72} y={1180} w={936} style={{ opacity: show, padding: 26 }}>
        <div
          style={{
            display: "flex",
            gap: 18,
            alignItems: "center",
            fontSize: 25,
          }}
        >
          <AppActivity size={25} active={t < 100} progress={t < 100 ? -1 : 1} />
          {t < 98
            ? "Search by visible content"
            : t < 100
              ? "Reviewing matches"
              : "3 matches. You choose what happens next."}
        </div>
        <div
          style={{
            opacity: results,
            fontSize: 20,
            color: T.textMuted,
            marginTop: 22,
          }}
        >
          Yellow jacket. A person. Mountain background.
        </div>
        <div
          style={{
            display: "flex",
            gap: 15,
            fontSize: 19,
            marginTop: 24,
            color: T.textMuted,
          }}
        >
          <AppIcon name="check" size={20} />
          <span>Reuse saved descriptions · Check missing details</span>
        </div>
        <div
          style={{
            opacity: mix(t, 101, 102),
            marginTop: 26,
            display: "flex",
            gap: 15,
          }}
        >
          <span
            style={{ padding: "12px 20px", background: T.ink800, fontSize: 20 }}
          >
            Edit selected
          </span>
          <span style={{ padding: "12px 0", color: T.textMuted, fontSize: 18 }}>
            Explicit selection. Existing media.
          </span>
        </div>
      </Panel>
      <div
        style={{
          ...abs,
          left: 72,
          top: 1660,
          opacity: show,
          fontSize: 24,
          color: T.textMuted,
        }}
      >
        Describe what you remember. Review why it matched.
      </div>
    </>
  );
};

export const AgentScene = ({ t }: { t: number }) => {
  const show = windowAt(t, 104.5, 118);
  if (!show) return null;
  const approved = mix(t, 109.2, 110.5),
    revision = mix(t, 113.4, 114.7, 0.65, 0.25);
  return (
    <>
      <Panel
        x={72}
        y={450}
        w={936}
        style={{ opacity: show * (1 - mix(t, 112.7, 113.6)), padding: 24 }}
      >
        <div
          style={{
            fontFamily: "Menlo",
            fontSize: 16,
            color: T.textMuted,
            display: "flex",
            alignItems: "center",
            gap: 12,
          }}
        >
          <AppActivity
            size={18}
            active={t < 107.1}
            progress={t < 107.1 ? -1 : 1}
          />
          LOCAL MODEL / THIS COMPUTER
        </div>
        <div style={{ fontSize: 25, lineHeight: 1.3, marginTop: 17 }}>
          Match the approved color adjustments
          <br />
          across these selected portraits.
        </div>
      </Panel>
      {["IMG_5103", "IMG_5111"].map((name, i) => (
        <div
          key={name}
          style={{
            ...abs,
            left: 388 + i * 316,
            top: 650,
            width: 290,
            height: 435,
            opacity: show * (1 - mix(t, 112.7, 113.6)),
            overflow: "hidden",
            filter: `brightness(${1 + approved * 0.09})`,
          }}
        >
          <Photo name={name} />
          <div
            style={{
              ...abs,
              left: 0,
              right: 0,
              bottom: 0,
              padding: 12,
              background: T.ink900,
              fontSize: 14,
              fontFamily: "Menlo",
            }}
          >
            {name}.CR2
          </div>
        </div>
      ))}
      <Panel
        x={72}
        y={1160}
        w={936}
        style={{ opacity: show * (1 - mix(t, 112.7, 113.6)), padding: 28 }}
      >
        <div style={{ fontSize: 24, fontWeight: 600 }}>
          Review a sample. Set the direction.
        </div>
        <div style={{ display: "flex", gap: 14, marginTop: 24, fontSize: 21 }}>
          <span
            style={{
              padding: "12px 18px",
              background: t > 109.2 ? T.ink800 : T.ink850,
            }}
          >
            Approve selected batch
          </span>
          <span style={{ padding: "12px 18px", background: T.ink850 }}>
            Adjust the direction
          </span>
        </div>
        <div
          style={{
            marginTop: 26,
            fontSize: 20,
            color: T.textMuted,
            display: "flex",
            gap: 15,
          }}
        >
          <AppActivity
            size={22}
            active={t > 110.5 && t < 112.6}
            progress={t >= 112.6 ? 1 : -1}
          />
          {t < 110.5
            ? "Awaiting your review"
            : t < 112.6
              ? "Applying native adjustments"
              : "Selected edits ready to revise"}
        </div>
      </Panel>
      <Panel
        x={450}
        y={650}
        w={370}
        style={{ opacity: show * mix(t, 112.7, 113.6), padding: 15 }}
      >
        <div style={{ fontSize: 19, fontWeight: 600, marginBottom: 20 }}>
          Still yours to change.
        </div>
        <AppInspectorSlider
          label="Exposure"
          value={revision}
          valueText={`+${revision.toFixed(2)} EV`}
          from={-5}
          to={5}
          precisionResponse
          press={pressAt(t, 113.3, 114.7)}
          showCursor={t > 113.3 && t < 114.8}
        />
        <NativeRows
          rows={["Original image", "Subject mask", "Color adjustments"]}
          active={2}
        />
        <div style={{ fontSize: 17, margin: "22px 12px 12px" }}>History</div>
        {[
          "Import original",
          "Approved color adjustments",
          "Manual exposure revision",
        ].map((v, i) => (
          <div
            key={v}
            style={{
              padding: "12px",
              fontSize: 14,
              color: i === 2 ? T.brandPaper : T.textMuted,
              background: i === 2 ? T.ink800 : "transparent",
            }}
          >
            {i === 2 ? "●" : "○"} {v}
          </div>
        ))}
      </Panel>
      <div
        style={{
          ...abs,
          left: 72,
          top: 1620,
          opacity: show * (1 - mix(t, 112.7, 113.4)),
          fontSize: 26,
          lineHeight: 1.4,
        }}
      >
        Your files. Your model. Your decisions.
        <br />
        <span style={{ color: T.textMuted, fontSize: 22 }}>
          Native edits, masks, layers and history stay editable.
        </span>
      </div>
      <div
        style={{
          ...abs,
          left: 72,
          top: 1740,
          opacity: show * (1 - mix(t, 112.7, 113.4)),
          fontSize: 19,
          color: T.textMuted,
        }}
      >
        Optional assistance · Local inference · Existing media
      </div>
    </>
  );
};
