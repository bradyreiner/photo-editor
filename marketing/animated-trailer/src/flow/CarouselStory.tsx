import { staticFile, useVideoConfig } from "remotion";
import { AppInspectorSlider, AppIcon } from "../app-kit/AppElements";
import { Sequence, Video, useStoryTime } from "./Timing";
import {
  A,
  Chrome,
  Head,
  Label,
  Light,
  mix,
  Note,
  Picture,
  q,
  Surface,
} from "./Motion";
import { Poster } from "./PhotoStory";

export const CarouselStory = () => {
  const t = useStoryTime(),
    { fps } = useVideoConfig();
  const split = q(t, 0.7, 2.2),
    preview = q(t, 5.4, 6.4) * (1 - q(t, 8.1, 9.1));
  const swipe = q(t, 6.7, 7.7),
    overview = q(t, 8.1, 9.5),
    enter = q(t, 11.3, 13);
  const bw = mix(mix(460, 560, preview), 210, overview),
    gap = mix(30 * split * (1 - preview), 25, overview);
  const x = mix(mix(80, 260, preview), 80, overview),
    y = mix(510 + 40 * preview, 610, overview);
  const crop = 38 * q(t, 2.8, 4) * (1 - q(t, 4.5, 5.2));
  return (
    <>
      <Chrome mode="Carousel / linked canvases" />
      <div style={{ opacity: 1 - enter }}>
        <Head
          t={t}
          b={8.5}
          lines={["One photo.", "Across canvases."]}
          detail="A continuous image. A seamless swipe."
        />
        <Head
          t={t}
          a={8.6}
          b={12.5}
          lines={["More than a grid.", "A complete story."]}
          size={96}
          detail="Photographs, designs and video. Together."
        />
      </div>
      <div
        style={{
          ...A,
          left: x,
          top: y,
          width: mix(mix(920, 560, preview), 920, overview),
          height:
            bw * 1.25 + 38.333 * (1 - split) * (1 - preview) * (1 - overview),
          overflow: "hidden",
          opacity: 1 - enter,
        }}
      >
        {[0, 1].map((i) => (
          <div
            key={i}
            style={{
              ...A,
              left: i * (bw + gap) - swipe * bw * preview,
              top: 0,
              width: bw,
              height: "100%",
              overflow: "hidden",
              borderRadius: 5 * split,
            }}
          >
            <div
              style={{
                ...A,
                left: -i * bw - 40 * split + crop,
                top: 0,
                width: bw * 2 + 80 * split,
                height: "100%",
              }}
            >
              <Picture src="IMG_5071.jpg" />
            </div>
            <Label
              style={{
                ...A,
                left: 15,
                top: 15,
                color: "white",
                background: "#0006",
                padding: 8,
                opacity: split * (1 - preview),
              }}
            >
              0{i + 1}
            </Label>
          </div>
        ))}
        <div
          style={{
            ...A,
            left: 2 * (bw + gap),
            width: bw,
            height: "100%",
            opacity: overview,
          }}
        >
          <Poster t={8} />
        </div>
      </div>
      <div
        style={{
          ...A,
          left: mix(785, 0, enter),
          top: mix(610, 480, enter),
          width: mix(210, 1080, enter),
          height: mix(262.5, 980, enter),
          overflow: "hidden",
          borderRadius: mix(5, 0, enter),
          opacity: q(t, 8.7, 9.4),
          boxShadow: "0 20px 80px #0008",
        }}
      >
        <Sequence from={9 * fps} layout="none">
          <Video
            src={staticFile("media/refine/arrival.mp4")}
            muted
            objectFit="cover"
            style={{ width: "100%", height: "100%" }}
          />
        </Sequence>
        <Label
          style={{
            ...A,
            left: 15,
            top: 15,
            color: "white",
            background: "#0006",
            padding: 8,
            opacity: 1 - enter,
          }}
        >
          04 / VIDEO
        </Label>
      </div>
      <Light t={t + 55} y={1470} amount={0.7 * enter} />
      <Light
        t={t + 55}
        y={1010}
        amount={0.7 * (1 - enter) * (1 - overview)}
        seam={q(t, 0.8, 1.8) * (1 - q(t, 2.1, 2.8))}
      />
      <Surface
        style={{
          ...A,
          left: 80,
          top: 1230,
          width: 920,
          padding: 28,
          opacity: 1 - enter,
          translate: `0 ${50 * overview}px`,
        }}
      >
        <Label>
          {overview > 0.5
            ? "MIXED MEDIA / 4 CANVASES"
            : preview > 0.5
              ? "SWIPE PREVIEW / 01 → 02"
              : "LINKED SPREAD / ONE SOURCE IMAGE"}
        </Label>
        {preview > 0.5 ? (
          <div
            style={{
              display: "flex",
              gap: 14,
              justifyContent: "center",
              padding: 30,
            }}
          >
            {[0, 1].map((i) => (
              <span
                key={i}
                style={{
                  height: 12,
                  width: (swipe > 0.5 ? i === 1 : i === 0) ? 54 : 12,
                  borderRadius: 8,
                  background: (swipe > 0.5 ? i === 1 : i === 0)
                    ? "white"
                    : "#555",
                }}
              />
            ))}
          </div>
        ) : overview > 0.5 ? (
          <div
            style={{ fontSize: 35, display: "flex", gap: 22, marginTop: 30 }}
          >
            <AppIcon name="check" size={34} />
            01.png · 02.png · 03.png · 04.mp4
          </div>
        ) : (
          <AppInspectorSlider
            label="Position X"
            value={crop}
            valueText={`${Math.round(crop)} px`}
            width={438}
            scale={2}
            showCursor={t > 2.8 && t < 4}
          />
        )}
      </Surface>
      <Note style={{ top: 1590, opacity: 1 - enter }}>
        {overview > 0.5 ? (
          <>
            Export each canvas in order.
            <br />
            Keep the story connected.
          </>
        ) : (
          <>
            Split the image. Adjust the shared crop.
            <br />
            No seams to line up by hand.
          </>
        )}
      </Note>
    </>
  );
};
