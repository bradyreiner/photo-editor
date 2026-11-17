import { Delivery } from "./FilmStory";
import { useStoryTime } from "./Timing";

import {
  AppActivity,
  AppIcon,
  AppInspectorSlider,
  AppWordmark,
} from "../app-kit/AppElements";
import {
  A,
  C,
  Chrome,
  Head,
  Label,
  Light,
  mix,
  Note,
  Picture,
  q,
  Surface,
  softness,
} from "./Motion";

export const Local = ({
  at,
  hideHero = false,
  withChrome = true,
}: {
  at?: number;
  hideHero?: boolean;
  withChrome?: boolean;
}) => {
  const clock = useStoryTime(),
    t = at ?? clock,
    selected = q(t, 5.4, 7.6),
    agent = q(t, 7.5, 8.7),
    apply = q(t, 10, 11.8),
    manual = q(t, 12.8, 14.2);
  const query = "yellow jacket in the mountains",
    typed = query.slice(0, Math.floor(query.length * q(t, 0.8, 2.5)));
  return (
    <>
      <div
        style={{
          ...A,
          inset: 0,
          translate: `${1080 * (1 - q(t, 0, 0.85))}px ${-35 * Math.sin(Math.PI * q(t, 0, 0.85))}px`,
          filter: `blur(${softness(t, 0, 0.85)}px)`,
        }}
      >
        {withChrome && <Chrome mode="Optional local assistance" />}
        <Head
          t={t}
          b={7.4}
          lines={["Find the shot.", "Keep it local."]}
          detail="Search by what is in the image."
        />
        <Head
          t={t}
          a={7.6}
          b={16.3}
          lines={["An optional", "helping hand."]}
          detail="Batch edits. Your layers. Your decisions."
        />
        <Surface
          style={{
            ...A,
            left: 80,
            top: 505,
            width: 920,
            padding: 28,
            opacity: 1 - agent,
            translate: `0 ${-20 * agent}px`,
          }}
        >
          <div
            style={{
              display: "flex",
              gap: 20,
              alignItems: "center",
              fontSize: 32,
              minHeight: 45,
            }}
          >
            <AppIcon name="search" size={32} />
            {typed}
          </div>
          <Label style={{ marginTop: 24 }}>
            LOCAL VISUAL SEARCH / ORIGINAL FILES
          </Label>
        </Surface>
        {["IMG_5106", "IMG_5103", "IMG_5111"].map((name, i) => (
          <div
            key={name}
            style={{
              ...A,
              left: mix(80 + i * 318, i === 0 ? 80 : 1130 + i * 150, selected),
              top: mix(760, 695, selected),
              width: mix(284, i === 0 ? 420 : 284, selected),
              height: mix(426, i === 0 ? 630 : 426, selected),
              overflow: "hidden",
              borderRadius: 10,
              opacity:
                (hideHero ? 0 : 1) * q(t, 2.7 + i * 0.12, 3.7 + i * 0.12),
              translate: `0 ${mix(60, 0, q(t, 2.7 + i * 0.12, 3.9 + i * 0.12))}px`,
              filter: `brightness(${0.92 + 0.08 * apply - 0.025 * manual})`,
            }}
          >
            <Picture src={`${name}.jpg`} />
          </div>
        ))}
        <Surface
          style={{
            ...A,
            left: 540,
            top: 695,
            width: 460,
            padding: 26,
            opacity: agent,
            translate: `${60 * (1 - agent)}px 0`,
          }}
        >
          <Label>LOCAL MODEL / CONNECTED</Label>
          <div style={{ fontSize: 30, lineHeight: 1.35, margin: "23px 0" }}>
            Match the exposure
            <br />
            across these portraits.
          </div>
          <div
            style={{
              display: "flex",
              alignItems: "center",
              gap: 18,
              fontSize: 26,
              padding: "16px 0",
              borderTop: `1px solid ${C.lineSoft}`,
            }}
          >
            <AppActivity
              size={32}
              active={t > 10 && t < 11.8}
              progress={apply}
            />
            {apply > 0.98 ? "Edits applied" : "Review → Apply to selected"}
          </div>
          <div
            style={{
              fontSize: 25,
              lineHeight: 1.7,
              color: C.textMuted,
              opacity: q(t, 11.4, 12.3),
            }}
          >
            ✓ Original image retained
            <br />✓ Editable adjustment layers
            <br />✓ Full edit history
          </div>
        </Surface>
        <Surface
          style={{
            ...A,
            left: 80,
            top: 1400,
            width: 920,
            padding: 26,
            opacity: agent,
          }}
        >
          <Label>
            {manual > 0.4
              ? "YOUR MANUAL REFINEMENT"
              : "EXPOSURE ADJUSTMENT / EDITABLE"}
          </Label>
          <AppInspectorSlider
            label="Exposure"
            value={0.3 * apply - 0.1 * manual}
            valueText={`+${(0.3 * apply - 0.1 * manual).toFixed(2)} EV`}
            from={-5}
            to={5}
            width={435}
            scale={2}
            showCursor={t > 12.8 && t < 14.2}
          />
          <div
            style={{
              display: "flex",
              gap: 26,
              alignItems: "center",
              fontSize: 27,
              color: C.textMuted,
              marginTop: 12,
            }}
          >
            <AppIcon name="undo" size={27} />
            Undo<span>Redo</span>
            <span style={{ marginLeft: "auto" }}>
              Layers & history retained
            </span>
          </div>
        </Surface>
        <Note style={{ top: 1705, opacity: q(t, 0.6, 1.5) }}>
          {t < 7.5 ? (
            <>
              Connect a local model.
              <br />
              Keep your media on your machine.
            </>
          ) : (
            <>
              Work by hand. Or work alongside an agent.
              <br />
              You keep creative control.
            </>
          )}
        </Note>
      </div>
      {t < 0.85 && (
        <div
          style={{
            ...A,
            inset: 0,
            translate: `${-1080 * q(t, 0, 0.85)}px ${-35 * Math.sin(Math.PI * q(t, 0, 0.85))}px`,
            filter: `blur(${softness(t, 0, 0.85)}px)`,
          }}
        >
          <Delivery at={7.999} />
        </div>
      )}
    </>
  );
};

export const Closing = () => {
  const t = useStoryTime(),
    travel = q(t, 0, 2.2),
    brand = q(t, 0.8, 1.9),
    clear = q(t, 0, 1.1);
  return (
    <>
      <div
        style={{
          ...A,
          inset: 0,
          translate: `${-1100 * clear}px ${-60 * Math.sin(Math.PI * clear)}px`,
          opacity: 1 - q(t, 0.55, 1.1),
        }}
      >
        <Local at={15.999} hideHero withChrome={false} />
      </div>
      <Light
        t={t + 134}
        y={1240}
        amount={0.5 * q(t, 0.8, 1.8) * (1 - q(t, 4.5, 7))}
      />
      <div
        style={{
          ...A,
          left: 80,
          top: mix(80, 310, brand),
          scale: mix(130 / 720, 1, brand),
          transformOrigin: "0 0",
        }}
      >
        <AppWordmark width={720} />
      </div>
      <Head
        top={670}
        t={t}
        a={1.3}
        b={20}
        lines={["Photo and video", "editing. Made easier."]}
        size={100}
      />
      <div
        style={{
          ...A,
          left: 80,
          top: 1080,
          fontSize: 42,
          lineHeight: 1.3,
          color: C.textMuted,
          opacity: q(t, 2, 2.8),
        }}
      >
        For professionals and hobbyists alike.
      </div>
      <div
        style={{
          ...A,
          left: mix(80, 716, travel) + 85 * Math.sin(Math.PI * travel),
          top: mix(695, 1250, travel) - 100 * Math.sin(Math.PI * travel),
          width: mix(420, 284, travel),
          height: mix(630, 390, travel),
          borderRadius: mix(10, 14, travel),
          overflow: "hidden",
          filter: `brightness(${0.975 + 0.025 * travel})`,
        }}
      >
        <Picture src="IMG_5106.jpg" />
      </div>
      {["refine/portrait-window.jpg", "refine/coast-blue.jpg"].map((src, i) => (
        <div
          key={src}
          style={{
            ...A,
            left: 80 + i * 318,
            top: 1250,
            width: 284,
            height: 390,
            overflow: "hidden",
            borderRadius: 14,
            rotate: `${[-3, 2][i] * (1 - q(t, 2.4, 4))}deg`,
            opacity: q(t, 2.2 + i * 0.1, 3.2 + i * 0.1),
            translate: `${mix(-90, 0, q(t, 2.2 + i * 0.1, 3.7 + i * 0.1))}px ${mix(100, 0, q(t, 2.2 + i * 0.1, 3.7 + i * 0.1))}px`,
          }}
        >
          <Picture src={src} />
        </div>
      ))}
      <Label style={{ ...A, left: 80, top: 1745, opacity: q(t, 3.4, 4.5) }}>
        PHOTOGRAPHY / DESIGN / FILM
      </Label>
    </>
  );
};
