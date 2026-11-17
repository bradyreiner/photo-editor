import { useCurrentFrame, useVideoConfig } from "remotion";
import {
  AppActivity,
  AppIcon,
  AppInspectorSlider,
} from "../app-kit/AppElements";
import { A, C, Copy, Grain, land, lerp, Meta, p, Photo, Plate } from "../launch/design";

export const AgentShot = ({ t }: { t: number }) => {
  const handoffUI = 1 - p(t, 13.45, 14),
    manual = p(t, 7.2, 8.25),
    outgoing = 1 - p(t, 7.2, 7.62),
    incoming = p(t, 7.68, 8.25),
    apply = p(t, 5.1, 6.65),
    adjust = p(t, 9.0, 10.4),
    undo = p(t, 11.0, 11.35) * (1 - p(t, 12.0, 12.35)),
    exposure = 0.65 - 0.4 * adjust * (1 - undo);
  const tiles = ["IMG_5106", "IMG_5103", "IMG_5111"];
  return (
    <Plate>
      <div style={{ ...A, left: 80, top: 140 - 24 * (1 - outgoing), opacity: outgoing }}>
        <Copy t={t} start={0.1} size={108}>
          <div>Your model.</div>
          <div>Your machine.</div>
        </Copy>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 430,
          width: 920,
          padding: "28px 32px",
          background: C.ink850,
          boxSizing: "border-box",
          opacity: outgoing,
        }}
      >
        <Meta style={{ fontSize: 27, marginBottom: 17 }}>
          OPTIONAL LOCAL AGENT
        </Meta>
        <div style={{ fontSize: 42, lineHeight: 1.2, letterSpacing: -1 }}>
          Match the exposure across
          <br />
          these selected portraits.
        </div>
      </div>
      {tiles.map((name, i) => {
        const first = i === 0,
          smallX = 80 + i * 318;
        return (
          <div
            key={name}
            style={{
              ...A,
              left: lerp(smallX, first ? 80 : 1450 + i * 120, manual),
              top: lerp(735, 590, manual),
              width: lerp(285, first ? 420 : 285, manual),
              height: lerp(427.5, first ? 630 : 427.5, manual),
              overflow: "hidden",
              opacity: first ? 1 : 1 - manual,
              filter: `brightness(${first ? (1 - 0.12 * (1 - apply)) * 2 ** (manual * (exposure - 0.65)) : 0.86 + apply * 0.14})`,
            }}
          >
            <Photo name={name} />
          </div>
        );
      })}
      <div
        style={{
          ...A,
          left: 80,
          top: 1260,
          width: 920,
          padding: "30px 32px",
          background: C.ink900,
          boxSizing: "border-box",
          opacity: outgoing,
        }}
      >
        <div style={{ fontSize: 36 }}>Review a sample. Set the direction.</div>
        <div style={{ display: "flex", gap: 20, marginTop: 28, fontSize: 31 }}>
          <div
            style={{
              padding: "17px 23px",
              border: "1px solid #585858",
              background: t > 5.05 && t < 5.2 ? C.ink800 : "#383838",
            }}
          >
            Apply to selected
          </div>
          <div
            style={{
              padding: "17px 23px",
              border: "1px solid #585858",
              color: C.textMuted,
            }}
          >
            Adjust the direction
          </div>
        </div>
        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: 16,
            marginTop: 32,
            fontSize: 29,
            color: C.textMuted,
          }}
        >
          <AppActivity
            size={27}
            active={t > 5.1 && t < 6.65}
            progress={t >= 6.65 ? 1 : -1}
          />
          {t < 5.1
            ? "Awaiting your review"
            : t < 6.65
              ? "Applying native adjustments"
              : "Selected edits ready to revise"}
        </div>
      </div>
      <div style={{ ...A, left: 80, top: 250 + 24 * (1 - incoming), opacity: incoming * handoffUI }}>
        <Copy t={t} start={7.68} size={144}>
          <div>Still yours.</div>
        </Copy>
      </div>
      <div
        style={{
          ...A,
          left: 560,
          top: 590,
          width: 440,
          padding: "27px 24px",
          background: C.ink900,
          boxSizing: "border-box",
          opacity: incoming * handoffUI,
        }}
      >
        <div style={{ fontSize: 31, marginBottom: 26 }}>
          Native edits. Real layers.
        </div>
        {[
          ["image", "Original image"],
          ["layers", "Subject mask"],
          ["adjust", "Color adjustments"],
        ].map(([icon, label]) => (
          <div
            key={label}
            style={{
              display: "flex",
              gap: 15,
              alignItems: "center",
              height: 60,
              fontSize: 27,
              borderBottom: "1px solid #303030",
            }}
          >
            <AppIcon name={icon} size={27} />
            {label}
          </div>
        ))}
        <div style={{ fontSize: 28, margin: "29px 0 15px" }}>History</div>
        {[
          "Import original",
          "Approved color adjustments",
          "Manual exposure revision",
        ].map((label, i) => (
          <div
            key={label}
            style={{
              fontSize: 23,
              padding: "14px 10px",
              background: i === 2 && t > 9 ? C.ink800 : "transparent",
              color: i === 2 && t > 9 ? C.brandPaper : C.textMuted,
            }}
          >
            ○ {label}
          </div>
        ))}
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1370,
          width: 920,
          padding: "28px 22px",
          background: C.ink900,
          boxSizing: "border-box",
          opacity: incoming * handoffUI,
        }}
      >
        <AppInspectorSlider
          label="Exposure"
          value={exposure}
          valueText={`+${exposure.toFixed(2)} EV`}
          from={-5}
          to={5}
          precisionResponse
          scale={2.8}
          width={313}
          press={p(t, 8.9, 9) * (1 - p(t, 10.4, 10.5))}
          showCursor={t > 9 && t < 10.5}
        />
        <div
          style={{
            display: "flex",
            gap: 30,
            margin: "24px 16px 0",
            fontSize: 31,
            color: C.textMuted,
          }}
        >
          <span style={{ color: undo > 0.5 ? C.brandPaper : C.textMuted }}>
            Undo
          </span>
          <span style={{ color: t > 12 ? C.brandPaper : C.textMuted }}>
            Redo
          </span>
          <span style={{ marginLeft: "auto" }}>Your original media</span>
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1735,
          fontSize: 40,
          letterSpacing: -1,
          opacity: land(t, 1, 2) * handoffUI,
        }}
      >
        {manual < 0.5
          ? "Delegate the batch. Keep the decisions."
          : "Adjust it. Revisit the mask. Keep going."}
      </div>
      <Grain />
    </Plate>
  );
};

export const AgentScene = () => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <AgentShot t={f / fps} />;
};
