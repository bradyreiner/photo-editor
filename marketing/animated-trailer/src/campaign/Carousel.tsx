import { Video } from "@remotion/media";
import {
  Sequence,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { AppIcon, AppInspectorSlider } from "../app-kit/AppElements";
import { CampaignArtwork } from "../launch/Artwork";
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
  pop,
} from "../launch/design";
import { Bar, Caption, Panel, WeddingImage } from "./Shared";

export const SpreadPicture = ({
  side,
  bw,
  crop = 0,
}: {
  side: number;
  bw: number;
  crop?: number;
}) => (
  <div
    style={{
      ...A,
      top: 0,
      left: -side * bw - (60 * bw) / 444 + (crop * bw) / 444,
      width: 2 * bw + (120 * bw) / 444,
      height: bw * 1.25,
    }}
  >
    <WeddingImage name="wedding-couple-developed" />
  </div>
);
export const CarouselScene = () => {
  const { fps } = useVideoConfig(),
    t = useCurrentFrame() / fps;
  const split = pop(t, 2.4, 3.5),
    preview = p(t, 9, 10),
    swipe = p(t, 11, 12.6),
    collect = p(t, 13.4, 14.7),
    exported = p(t, 17.2, 18.3);
  const bw =
    lerp(lerp(460, 444, split), 560, preview) * (1 - collect) + 205 * collect;
  const gap = 32 * split * (1 - preview) * (1 - collect) + 33 * collect;
  const left = lerp(lerp(80, 260, preview), 80, collect),
    top = lerp(lerp(560, 590, preview), 650, collect);
  const shift = -swipe * bw * (1 - collect),
    crop = 35 * p(t, 5.3, 7.2) * (1 - p(t, 8, 8.5));
  const windowW = lerp(lerp(920, 560, preview), 920, collect),
    windowH = bw * 1.25;
  return (
    <Plate>
      <div style={{ ...A, left: 80, top: 150, opacity: 1 - p(t, 13, 13.6) }}>
        <Copy t={t} size={105}>
          <div>One photo.</div>
          <div>Across the swipe.</div>
        </Copy>
      </div>
      <div style={{ ...A, left: 80, top: 150, opacity: p(t, 13.4, 14) }}>
        <Copy t={t} start={13.4} size={104}>
          <div>Build the story.</div>
          <div>Board by board.</div>
        </Copy>
      </div>
      <div
        style={{
          ...A,
          left,
          top,
          width: windowW,
          height: windowH,
          overflow: "hidden",
        }}
      >
        {[0, 1].map((i) => (
          <div
            key={i}
            style={{
              ...A,
              left: i * (bw + gap) + shift,
              top: 0,
              width: bw,
              height: bw * 1.25,
              overflow: "hidden",
              outline:
                split > 0 && preview < 0.8
                  ? `1px solid ${C.lineStrong}`
                  : "none",
            }}
          >
            <SpreadPicture side={i} bw={bw} crop={crop} />
            <div
              style={{
                ...A,
                left: 16,
                top: 16,
                fontFamily: mono,
                fontSize: lerp(24, 16, collect),
                padding: "6px 10px",
                background: "#050505a0",
                opacity: split * (1 - preview + collect),
              }}
            >{`0${i + 1}`}</div>
          </div>
        ))}
        {collect > 0 ? (
          <>
            <div
              style={{
                ...A,
                left: 476,
                top: 0,
                width: 205,
                height: 256,
                overflow: "hidden",
                opacity: collect,
              }}
            >
              <div
                style={{
                  width: 1200,
                  height: 1500,
                  scale: 205 / 1200,
                  transformOrigin: "0 0",
                }}
              >
                <CampaignArtwork />
              </div>
            </div>
            <div
              style={{
                ...A,
                left: 714,
                top: 0,
                width: 205,
                height: 256,
                overflow: "hidden",
                opacity: collect,
              }}
            >
              <Sequence
                from={Math.round(13.4 * fps)}
                durationInFrames={Math.round(6.6 * fps)}
                layout="none"
              >
                <Video
                  objectFit="cover"
                  src={staticFile("media/launch-1268.mp4")}
                  muted
                  trimBefore={120}
                  playbackRate={0.5}
                  style={{ width: "100%", height: "100%" }}
                />
              </Sequence>
              <div
                style={{
                  ...A,
                  bottom: 14,
                  right: 14,
                  background: "#050505a0",
                  padding: 7,
                }}
              >
                <AppIcon name="play" size={25} />
              </div>
            </div>
          </>
        ) : null}
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: lerp(1180, 1240, preview),
          width: 920,
          opacity: 1 - collect,
        }}
      >
        <Panel>
          <Bar
            left={preview > 0.5 ? "Continuity preview" : "Seamless spread"}
            right={preview > 0.5 ? "Swipe 1 → 2" : "1 image → 2 boards"}
          />
          {preview < 0.5 ? (
            <div style={{ padding: "20px 22px" }}>
              <AppInspectorSlider
                label="Position X"
                value={crop}
                valueText={`${Math.round(crop)} px`}
                from={-100}
                to={100}
                width={438}
                scale={2}
                showCursor={t > 5.3 && t < 7.2}
              />
            </div>
          ) : (
            <div
              style={{
                padding: 30,
                display: "flex",
                alignItems: "center",
                justifyContent: "center",
                gap: 16,
              }}
            >
              {[0, 1].map((i) => (
                <div
                  key={i}
                  style={{
                    width:
                      swipe > 0.5 ? (i === 1 ? 45 : 12) : i === 0 ? 45 : 12,
                    height: 12,
                    borderRadius: 7,
                    background: (swipe > 0.5 ? i === 1 : i === 0)
                      ? C.brandPaper
                      : C.lineStrong,
                  }}
                />
              ))}
            </div>
          )}
        </Panel>
        <Caption style={{ marginTop: 35, fontSize: 40 }}>
          {preview > 0.5
            ? "Check the seam before you export."
            : "Adjust the shared crop. Both boards follow."}
        </Caption>
      </div>
      <Panel
        style={{ ...A, left: 80, top: 1050, width: 920, opacity: collect }}
      >
        <Bar
          left={exported > 0.5 ? "Export carousel" : "Carousel / 4 boards"}
          right={exported > 0.5 ? "Numbered files" : "Photo · Design · Video"}
        />
        <div style={{ padding: 30, fontSize: 34, lineHeight: 1.5 }}>
          {exported > 0.5 ? (
            <>
              {[
                "01.png + 02.png / Seamless spread",
                "03.png / Layered artwork",
                "04.mp4 / Moving image",
              ].map((v, i) => (
                <div
                  key={v}
                  style={{
                    display: "flex",
                    justifyContent: "space-between",
                    alignItems: "center",
                    opacity: land(t, 17.7 + i * 0.25, 18.3 + i * 0.25),
                  }}
                >
                  <span>{v}</span>
                  <AppIcon name="check" size={29} />
                </div>
              ))}
            </>
          ) : (
            <>
              <div style={{ display: "flex", gap: 20, alignItems: "center" }}>
                <AppIcon name="link" size={31} />
                Linked source · Boards 01 + 02
              </div>
              <div style={{ marginTop: 18, color: C.textMuted }}>
                Photos, type, layers and video in one sequence.
              </div>
            </>
          )}
        </div>
      </Panel>
      <Caption
        style={{
          ...A,
          left: 80,
          top: 1570,
          width: 900,
          opacity: collect,
          fontSize: 42,
        }}
      >
        A continuous image.
        <br />A coordinated social story.
      </Caption>
      <div
        style={{
          ...A,
          left: 80,
          top: 1790,
          fontSize: 27,
          color: C.textMuted,
          opacity: p(t, 9.5, 10) * (1 - collect),
        }}
      >
        Platform overlay is preview only.
      </div>
      <Grain />
    </Plate>
  );
};
