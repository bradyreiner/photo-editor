import { Video } from "@remotion/media";
import {
  interpolate,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { AppIcon, AppInspectorSlider } from "../app-kit/AppElements";
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
import { Bar, Caption, Cursor, Panel } from "./Shared";

export const VerticalPicture = ({
  t,
  preview = false,
  frameWidth = 608,
  frameHeight = 1080,
}: {
  t: number;
  preview?: boolean;
  frameWidth?: number;
  frameHeight?: number;
}) => {
  const min = frameWidth / (2 * frameHeight * (4096 / 2160));
  const follow = Math.min(
    1 - min,
    Math.max(
      min,
      interpolate(
        t,
        [1, 3, 5, 7, 9, 11, 13, 15],
        [0.594, 0.586, 0.548, 0.511, 0.504, 0.699, 0.442, 0.066],
      ),
    ),
  );
  return (
    <>
      <div
        style={{
          position: "absolute",
          left: "50%",
          transform: `translateX(${-follow * 100}%)`,
          aspectRatio: "4096 / 2160",
          top: 0,
          height: "100%",
        }}
      >
        <Video
          objectFit="fill"
          src={staticFile("media/range/skate.mp4")}
          trimBefore={0}
          playbackRate={0.5}
          muted
          style={{
            width: "100%",
            height: "100%",
          }}
        />
      </div>
      <div
        style={{
          ...A,
          left: "8%",
          right: "8%",
          top: lerp(22, 29, p(t, 5, 6.7)) + "%",
          fontFamily: "Manrope",
          fontWeight: 800,
          fontSize: preview ? 44 : 72,
          lineHeight: 0.95,
          letterSpacing: -3,
          color: "white",
          opacity: land(t, 4.7, 5.2),
          translate: `${land(t, 4.7, 5.8, -130, 0)}px 0`,
          textShadow: "0 2px 9px #0004",
        }}
      >
        MAKE YOUR
        <br />
        NEXT MOVE.
      </div>
    </>
  );
};
export const VerticalScene = ({ at }: { at?: number }) => {
  const { fps } = useVideoConfig(),
    frame = useCurrentFrame(),
    t = at ?? frame / fps,
    portrait = p(t, 0.7, 2.2),
    safe = p(t, 7.1, 7.7) * (1 - p(t, 10.5, 11.2)),
    deliver = p(t, 11.4, 12.1);
  const w = lerp(920, 608, portrait),
    h = lerp(518, 1080, portrait),
    x = (1080 - w) / 2;
  const mx =
    (0.5 -
      interpolate(
        t,
        [1, 3, 5, 7, 9, 11, 13, 15],
        [0.594, 0.586, 0.548, 0.511, 0.504, 0.699, 0.442, 0.066],
      )) *
    ((1920 * 4096) / 2160);
  return (
    <Plate>
      <div style={{ ...A, left: 80, top: 150 }}>
        <Copy t={t} size={104}>
          <div>Made for the feed.</div>
          <div>Built like a film.</div>
        </Copy>
      </div>
      <div
        style={{
          ...A,
          left: x,
          top: 470,
          width: w,
          height: h,
          overflow: "hidden",
          boxShadow: "0 35px 100px #0008",
        }}
      >
        <VerticalPicture t={t} frameWidth={w} frameHeight={h} />
        <div style={{ ...A, inset: 0, opacity: safe, color: "white" }}>
          <div
            style={{
              ...A,
              inset: "12% 17% 21% 7%",
              border: "2px dashed #ffffffc0",
            }}
          />
          <div
            style={{
              ...A,
              right: 18,
              top: "50%",
              display: "flex",
              flexDirection: "column",
              gap: 35,
              fontSize: 35,
            }}
          >
            <span>♡</span>
            <span>◯</span>
            <span>↗</span>
          </div>
          <div
            style={{
              ...A,
              left: 30,
              right: 60,
              bottom: 65,
              padding: "24px 0",
              fontSize: 25,
              borderTop: "1px solid #fff8",
            }}
          >
            Caption & platform controls
          </div>
        </div>
        <div
          style={{
            ...A,
            left: 20,
            top: 20,
            fontFamily: mono,
            fontSize: 21,
            padding: 10,
            background: "#050505a0",
          }}
        >
          9:16 / {t > 7.1 ? "SAFE AREA PREVIEW" : "PROGRAM"}
        </div>
      </div>
      <Panel
        style={{ ...A, left: 80, top: 1600, width: 920, opacity: 1 - deliver }}
      >
        <Bar
          left={t < 4.5 ? "Sequence settings" : "Transform · Keyframes"}
          right={t < 4.5 ? "1080 × 1920" : "Position Y"}
        />
        {t < 4.5 ? (
          <div style={{ padding: 18 }}>
            <AppInspectorSlider
              label="Position X"
              value={mx}
              valueText={`${Math.round(mx)} px`}
              from={-2000}
              to={2000}
              width={440}
              scale={2}
              showCursor={t > 2.5 && t < 4}
            />
          </div>
        ) : (
          <div
            style={{
              height: 112,
              position: "relative",
              padding: 20,
              boxSizing: "border-box",
            }}
          >
            <div
              style={{
                ...A,
                left: 160,
                right: 70,
                top: 57,
                height: 2,
                background: C.lineStrong,
              }}
            />
            {[180, 450, 780].map((a, i) => (
              <div
                key={a}
                style={{
                  ...A,
                  left: a,
                  top: 46,
                  width: 21,
                  height: 21,
                  rotate: "45deg",
                  background: i === 1 ? C.brandCoral : C.brandPaper,
                }}
              />
            ))}
            <div
              style={{
                ...A,
                left: lerp(180, 780, p(t, 4.7, 6.8)),
                top: 25,
                width: 2,
                height: 70,
                background: C.brandPaper,
              }}
            />
            <div style={{ fontFamily: mono, fontSize: 24 }}>Y</div>
          </div>
        )}
      </Panel>
      <div
        style={{
          ...A,
          left: 80,
          top: 1770,
          display: "flex",
          gap: 20,
          opacity: land(t, 7.2, 8) * (1 - deliver),
        }}
      >
        {["TikTok", "Instagram Reels", "YouTube Shorts"].map((n, i) => (
          <span
            key={n}
            style={{
              fontSize: 25,
              padding: "12px 18px",
              background: i === 0 ? C.ink850 : C.ink900,
              border: `1px solid ${i === 0 ? C.brandPaper : C.lineSoft}`,
            }}
          >
            {n}
          </span>
        ))}
      </div>
      <Panel
        style={{
          ...A,
          left: 80,
          top: 1580,
          width: 920,
          opacity: deliver,
          translate: `0 ${land(t, 11.4, 12.2, 80, 0)}px`,
        }}
      >
        <Bar left="Export vertical video" right="H.264" />
        <div
          style={{
            padding: 26,
            fontSize: 33,
            display: "flex",
            justifyContent: "space-between",
          }}
        >
          <span>1080 × 1920</span>
          <span>Rec.709</span>
          <AppIcon name="check" size={32} />
        </div>
      </Panel>
      <Caption
        style={{ ...A, left: 80, top: 1780, opacity: deliver, fontSize: 37 }}
      >
        Reframe. Animate. Mix. Deliver.
      </Caption>
      <Cursor
        x={lerp(750, 690, p(t, 2.5, 4))}
        y={1690}
        opacity={t > 2.5 && t < 4 ? 1 : 0}
      />
      <Grain />
    </Plate>
  );
};
