import { useCurrentFrame, useVideoConfig } from "remotion";
import {
  AppActivity,
  AppIcon,
  AppInspectorSlider,
} from "../app-kit/AppElements";
import { A, C, Copy, Grain, land, lerp, Meta, p, Photo, Plate } from "./design";

export const SearchShot = ({ t }: { t: number }) => {
  const query = "yellow jacket in the mountains",
    typed = query.slice(0, Math.floor(land(t, 0.4, 2.3) * query.length)),
    results = land(t, 2.7, 3.5),
    choose = p(t, 7.4, 8.55),
    handoff = p(t, 8.65, 9.55);
  return (
    <Plate>
      <div style={{ ...A, left: 80, top: 140 }}>
        <Copy t={t} start={0.05} size={96}>
          <div>Find it by</div>
          <div>what you remember.</div>
        </Copy>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 440,
          width: 920,
          padding: "28px 32px",
          boxSizing: "border-box",
          background: C.ink850,
          display: "flex",
          alignItems: "center",
          gap: 24,
          fontSize: 41,
          minHeight: 120,
        }}
      >
        <AppIcon name="search" size={37} />
        <span>
          {typed}
          <span style={{ opacity: t < 2.5 && Math.sin(t * 12) > 0 ? 1 : 0 }}>
            |
          </span>
        </span>
      </div>
      <Meta style={{ ...A, left: 80, top: 595, fontSize: 28 }}>
        NEPAL COLLECTION / LOCAL VISUAL SEARCH
      </Meta>
      <div
        style={{
          ...A,
          left: 80,
          top: lerp(700, 735, handoff),
          width: lerp(lerp(570, 920, choose), 285, handoff),
          height: lerp(lerp(855, 1080, choose), 427.5, handoff),
          overflow: "hidden",
          opacity: results,
          translate: `0 ${land(t, 2.7, 3.6, 70, 0)}px`,
        }}
      >
        <Photo />
        <div
          style={{
            ...A,
            inset: 0,
            border: `2px solid ${t > 7.4 ? C.brandPaper : "transparent"}`,
          }}
        />
      </div>
      {["IMG_5103", "IMG_5111"].map((name, i) => (
        <div
          key={name}
          style={{
            ...A,
            left: lerp(700 + choose * 400, 80 + (i + 1) * 318, handoff),
            top: lerp(700 + i * 445, 735, handoff),
            width: lerp(300, 285, handoff),
            height: lerp(410, 427.5, handoff),
            overflow: "hidden",
            opacity: results * Math.max(1 - choose, handoff),
            translate: `0 ${land(t, 2.9 + i * 0.12, 3.8 + i * 0.12, 90, 0)}px`,
          }}
        >
          <Photo name={name} />
          <div
            style={{
              ...A,
              inset: 0,
              border: `2px solid ${C.brandPaper}`,
              opacity: handoff,
            }}
          />
        </div>
      ))}
      <div
        style={{
          ...A,
          left: 80,
          top: 1260,
          width: 920,
          padding: "30px 32px",
          boxSizing: "border-box",
          background: C.ink900,
          fontSize: 36,
          opacity: handoff,
        }}
      >
        3 originals. Selected.
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1620,
          width: 920,
          background: C.ink900,
          padding: "26px 30px",
          boxSizing: "border-box",
          opacity: 1 - choose,
        }}
      >
        <div
          style={{
            display: "flex",
            gap: 18,
            alignItems: "center",
            fontSize: 32,
          }}
        >
          <AppActivity
            size={27}
            active={t < 3.5}
            progress={t >= 3.5 ? 1 : -1}
          />
          {t < 3.5
            ? "Searching this collection"
            : "3 matches. Review before you edit."}
        </div>
        <div
          style={{
            fontSize: 28,
            color: C.textMuted,
            marginTop: 20,
            opacity: results,
          }}
        >
          Yellow jacket / person / mountain background
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1815,
          fontSize: 31,
          color: C.textMuted,
          opacity: (1 - choose) * results,
        }}
      >
        Reuse descriptions. Check missing details.
      </div>
      <Grain />
    </Plate>
  );
};

export const AgentShot = ({ t }: { t: number }) => {
  const handoffUI = 1 - p(t, 13.45, 14),
    manual = p(t, 7.2, 8.25),
    apply = p(t, 5.1, 6.65),
    adjust = p(t, 9.0, 10.4),
    undo = p(t, 11.0, 11.35) * (1 - p(t, 12.0, 12.35)),
    exposure = 0.65 - 0.4 * adjust * (1 - undo);
  const tiles = ["IMG_5106", "IMG_5103", "IMG_5111"];
  return (
    <Plate>
      <div style={{ ...A, left: 80, top: 140, opacity: 1 - manual }}>
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
          opacity: 1 - manual,
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
          opacity: 1 - manual,
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
      <div style={{ ...A, left: 80, top: 250, opacity: manual * handoffUI }}>
        <Copy t={t} start={7.4} size={144}>
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
          opacity: manual * handoffUI,
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
          opacity: manual * handoffUI,
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

export const SearchScene = () => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <SearchShot t={f / fps} />;
};
export const AgentScene = () => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <AgentShot t={f / fps} />;
};
