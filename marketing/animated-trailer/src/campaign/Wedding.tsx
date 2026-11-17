import {
  CanvasImage,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import {
  AppActivity,
  AppIcon,
  AppInspectorSlider,
} from "../app-kit/AppElements";
import {
  A,
  C,
  Copy,
  Grain,
  land,
  lerp,
  mono,
  p,
  Plate,
} from "../launch/design";
import { Bar, Caption, Panel, PhaseCopy, WeddingImage } from "./Shared";

const names = [
  "wedding-bride-02",
  "wedding-bride-01",
  "wedding-bride-03",
  "wedding-couple",
  "wedding-dawn",
  "wedding-bride-01",
];
export const WeddingShot = ({ t }: { t: number }) => {
  const open = p(t, 3.1, 4.25),
    detail = p(t, 10.5, 11.4) * (1 - p(t, 13.3, 14.2)),
    batch = p(t, 14.5, 15.6),
    finish = p(t, 19.3, 20.3);
  const exposure = p(t, 4.7, 5.9),
    highlight = p(t, 6, 7.2),
    warm = p(t, 7.3, 8.4),
    curve = p(t, 8.5, 10);
  const frameX = lerp(80, 80, open),
    frameY = lerp(735, 430, open),
    frameW = lerp(285, 920, open),
    frameH = lerp(428, 960, open);
  return (
    <Plate>
      <PhaseCopy t={t} start={-0.5} end={3.8}>
        <Copy t={t} start={-0.5} size={102}>
          <div>For the whole</div>
          <div>wedding gallery.</div>
        </Copy>
      </PhaseCopy>
      <PhaseCopy t={t} start={3.6} end={10.8}>
        <Copy t={t} start={3.6} size={99}>
          <div>RAW. Refined.</div>
          <div>On your terms.</div>
        </Copy>
      </PhaseCopy>
      <PhaseCopy t={t} start={10.8} end={14.5}>
        <Copy t={t} start={10.8} size={100}>
          <div>Keep the</div>
          <div>smallest detail.</div>
        </Copy>
      </PhaseCopy>
      <PhaseCopy t={t} start={14.6} end={22.2}>
        <Copy t={t} start={14.6} size={99}>
          <div>One look.</div>
          <div>A complete set.</div>
        </Copy>
      </PhaseCopy>
      <div
        style={{
          ...A,
          left: 80,
          top: 595,
          fontSize: 29,
          letterSpacing: 1,
          opacity: 1 - open,
        }}
      >
        IMPORT → CULL → DEVELOP → DELIVER
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 655,
          opacity: 1 - open,
          fontSize: 27,
          color: C.textMuted,
          display: "flex",
          gap: 40,
        }}
      >
        <span>Batch import</span>
        <span>★ Rate</span>
        <span>⚑ Pick</span>
        <span>× Reject</span>
      </div>
      {names.map((n, i) => {
        if (i === 0) return null;
        return (
          <div
            key={i}
            style={{
              ...A,
              left: 80 + (i % 3) * 318,
              top: 735 + Math.floor(i / 3) * 485,
              width: 285,
              height: i < 3 ? 428 : 360,
              overflow: "hidden",
              opacity: 1 - open,
              translate: `0 ${land(t, 0.12 * i, 0.7 + 0.12 * i, 36, 0)}px`,
            }}
          >
            <WeddingImage name={n} />
            <div
              style={{
                ...A,
                bottom: 0,
                left: 0,
                right: 0,
                background: "#050505b0",
                padding: 13,
                fontSize: 23,
                color: i === 2 ? C.textMuted : C.brandPaper,
              }}
            >
              {t > 1.1 + i * 0.2
                ? i === 2
                  ? "× Rejected"
                  : "★ ★ ★ ★ ★"
                : "Select"}
            </div>
          </div>
        );
      })}
      <div
        style={{
          ...A,
          left: frameX,
          top: frameY,
          width: frameW,
          height: frameH,
          overflow: "hidden",
          opacity: 1 - batch,
        }}
      >
        <WeddingImage name="wedding-bride-02" />
        <WeddingImage
          name="wedding-exposure"
          style={{ ...A, inset: 0, opacity: exposure }}
        />
        <WeddingImage
          name="wedding-highlights"
          style={{ ...A, inset: 0, opacity: highlight }}
        />
        <WeddingImage style={{ ...A, inset: 0, opacity: warm }} />
        <div style={{ ...A, inset: 0, opacity: detail, overflow: "hidden" }}>
          <CanvasImage
            src={staticFile("media/campaign/wedding-lace-detail.jpg")}
            style={{
              width: "100%",
              height: "100%",
              objectFit: "cover",
            }}
          />
        </div>
        <div
          style={{
            ...A,
            left: 22,
            top: 22,
            fontFamily: mono,
            fontSize: 22,
            padding: "9px 15px",
            background: "#050505a0",
            opacity: open,
          }}
        >
          {detail > 0.5 ? "DETAIL / 1:1" : "DEVELOP / sRGB"}
        </div>
      </div>
      <Panel
        style={{
          ...A,
          left: 80,
          top: 1410,
          width: 920,
          padding: 22,
          opacity: open * (1 - batch),
        }}
      >
        <div
          style={{
            fontSize: 25,
            color: C.textMuted,
            marginBottom: 10,
            letterSpacing: 2,
          }}
        >
          LOOKSY COLOR ENGINE
        </div>
        {detail < 0.5 ? (
          <>
            <AppInspectorSlider
              label="Exposure"
              value={0.32 * exposure}
              valueText={`+${(0.32 * exposure).toFixed(2)} EV`}
              from={-5}
              to={5}
              width={438}
              scale={2}
              showCursor={t > 4.7 && t < 5.9}
              press={t > 4.7 && t < 5.9 ? 1 : 0}
            />
            <AppInspectorSlider
              label="Highlights"
              value={-24 * highlight}
              valueText={Math.round(-24 * highlight).toString()}
              width={438}
              scale={2}
              showCursor={t > 6 && t < 7.2}
            />
            <AppInspectorSlider
              label="Temperature"
              value={10 * warm}
              valueText={`+${Math.round(10 * warm)}`}
              width={438}
              scale={2}
              trackColors={["#3282ff", "#ddd", "#f7bc6e"]}
              showCursor={t > 7.3 && t < 8.4}
            />
          </>
        ) : (
          <>
            <AppInspectorSlider
              label="Sharpening"
              value={32}
              valueText="32"
              from={0}
              to={100}
              width={438}
              scale={2}
            />
            <AppInspectorSlider
              label="Noise"
              value={15}
              valueText="15"
              from={0}
              to={100}
              width={438}
              scale={2}
            />
            <Caption style={{ fontSize: 30, marginTop: 18 }}>
              Lace. Texture. Natural skin.
            </Caption>
          </>
        )}
      </Panel>
      <div
        style={{
          ...A,
          left: 565,
          top: 920,
          width: 395,
          height: 380,
          background: "#101010ee",
          padding: 24,
          opacity: p(t, 8.3, 8.7) * (1 - p(t, 10.2, 10.7)),
          border: `1px solid ${C.lineSoft}`,
        }}
      >
        <div style={{ fontSize: 26, marginBottom: 18 }}>Tone curve · RGB</div>
        <svg width="345" height="285" viewBox="0 0 345 285">
          <path
            d="M0 0V285H345 M86 0V285 M172 0V285 M258 0V285 M0 71H345 M0 142H345 M0 213H345"
            stroke="#393939"
            fill="none"
          />
          <path
            d={`M0 285 C70 ${230 + 18 * curve} 70 ${200 + 20 * curve} 130 175 S270 ${60 - 20 * curve} 345 0`}
            fill="none"
            stroke="white"
            strokeWidth="3"
          />
          <circle cx={130} cy={175} r={7} fill={C.brandPaper} />
        </svg>
      </div>
      <div style={{ ...A, inset: 0, opacity: batch }}>
        {names.slice(0, 5).map((n, i) => {
          const developed = p(t, 16 + i * 0.3, 16.6 + i * 0.3);
          return (
            <div
              key={n}
              style={{
                ...A,
                left: i < 3 ? 80 + i * 318 : 80 + (i - 3) * 477,
                top: i < 3 ? 500 : 1010,
                width: i < 3 ? 285 : 444,
                height: i < 3 ? 428 : 380,
                overflow: "hidden",
                translate: `0 ${land(t, 14.6 + i * 0.06, 15.6 + i * 0.06, 120, 0)}px`,
              }}
            >
              <WeddingImage name={n} />
              <WeddingImage
                name={`${n}-developed`}
                style={{ ...A, inset: 0, opacity: developed }}
              />
              <div
                style={{
                  ...A,
                  right: 15,
                  bottom: 15,
                  opacity: developed,
                  background: "#050505b0",
                  padding: 9,
                }}
              >
                <AppIcon name="check" size={29} />
              </div>
            </div>
          );
        })}
        <Panel style={{ ...A, left: 80, top: 1450, width: 920 }}>
          <Bar
            left={finish > 0.5 ? "Export gallery" : "Sync adjustments"}
            right={finish > 0.5 ? "sRGB" : "5 selected"}
          />
          <div
            style={{
              padding: 26,
              fontSize: 34,
              display: "flex",
              alignItems: "center",
              gap: 24,
            }}
          >
            <AppActivity
              progress={p(t, 16, 18.3)}
              active={t < 18.3}
              size={46}
            />
            {finish > 0.5
              ? "Full-resolution photographs"
              : "Exposure · Color · Detail · Curve"}
          </div>
        </Panel>
        <Caption
          style={{ ...A, left: 80, top: 1700, width: 900, fontSize: 40 }}
        >
          Edit one frame. Keep the gallery consistent.
        </Caption>
      </div>
      <Grain />
    </Plate>
  );
};
export const WeddingScene = () => {
  const { fps } = useVideoConfig();
  return <WeddingShot t={useCurrentFrame() / fps} />;
};
