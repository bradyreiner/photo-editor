import { getLength, getPointAtLength } from "@remotion/paths";
import { Sequence, staticFile, useVideoConfig } from "remotion";
import { Video } from "@remotion/media";
import { Artwork } from "./Artwork";
import {
  A,
  Bar,
  Caption,
  Control,
  Dot,
  Pointer,
  Icon,
  mix,
  muted,
  Panel,
  Photo,
  s,
  Tag,
  Title,
  useTime,
  window,
} from "./Core";

const stroke = "M80 1160 C250 1070 440 1110 595 1140";
const length = getLength(stroke);
export const CanvasStudio = () => {
  const t = useTime(),
    compose = s(t, 0.6, 2),
    build = s(t, 2.4, 4),
    track = s(t, 5.5, 7.5),
    size = s(t, 8, 10),
    hide = window(t, 11.6, 13.8),
    layers = s(t, 10.35, 11.15) * (1 - s(t, 16.45, 17.2)),
    paint = s(t, 14.3, 16.1),
    wide = s(t, 17.4, 20.3);
  const width = mix(mix(1080, 840, compose), 590, layers),
    height = mix(mix(1200, 1050, compose), 737.5, layers),
    x = mix(mix(0, 120, compose), 40, layers),
    y = mix(470, 500, compose),
    point = getPointAtLength(stroke, length * paint);
  return (
    <>
      <Bar mode="Canvas / layers and typography" />
      <Title
        t={t}
        b={5.1}
        lines={["Build beyond", "the photograph."]}
        detail="Photo editing meets a real design canvas."
      />
      <Title
        t={t}
        a={5.2}
        b={10.5}
        lines={["Type worth", "looking at."]}
        detail="Size. Tracking. Layout. Editable text."
      />
      <Title
        t={t}
        a={10.6}
        b={16.8}
        lines={["Real layers.", "Real control."]}
        detail="Masks, adjustments, type and brushwork."
      />
      <Title
        t={t}
        a={17}
        b={24.5}
        lines={["Posters. Thumbnails.", "Your next idea."]}
        size={89}
        detail="Recompose it. Keep everything editable."
      />
      <div style={{ ...A, inset: 0, translate: `${-1140 * wide}px 0` }}>
        <div
          style={{
            ...A,
            left: x,
            top: y,
            width,
            height,
            overflow: "hidden",
            boxShadow: "0 30px 90px #0008",
          }}
        >
          <Artwork
            build={build}
            tracking={mix(-5, 2, track)}
            font={mix(166, 150, size)}
            foreground={1 - hide}
            brush={paint}
          />
          {paint > 0 && paint < 1 && (
            <svg
              viewBox="0 0 1080 1350"
              width="100%"
              height="100%"
              style={{ ...A, inset: 0 }}
            >
              <circle
                cx={point?.x ?? 80}
                cy={point?.y ?? 1160}
                r={20}
                fill="none"
                stroke="white"
                strokeWidth={2}
              />
              <circle
                cx={point?.x ?? 80}
                cy={point?.y ?? 1160}
                r={4}
                fill="white"
              />
            </svg>
          )}
        </div>
        <div
          style={{
            ...A,
            left: 1220,
            top: 630,
            width: 920,
            height: 518,
            overflow: "hidden",
            boxShadow: "0 30px 90px #0008",
          }}
        >
          <Artwork wide={1} font={170} tracking={-3} />
          <div
            style={{
              ...A,
              inset: 18,
              border: "1px dashed #dce4e56a",
              opacity: window(t, 19.8, 22),
            }}
          />
        </div>
      </div>
      <Panel
        style={{
          ...A,
          left: 660,
          top: 770,
          width: 350,
          padding: 24,
          opacity: layers,
          translate: `${30 * (1 - layers)}px 0`,
        }}
      >
        <Tag style={{ fontSize: 25, marginBottom: 20 }}>LAYERS</Tag>
        {[
          ["brush", "Brush accent"],
          ["selection", "Subject mask"],
          ["text", "TAKE THE LONG WAY."],
          ["adjust", "Color adjustments"],
          ["image", "Original RAW"],
        ].map(([icon, label], i) => {
          const active = i === 1 ? hide < 0.5 : i === 0 ? paint > 0.1 : true;
          return (
            <div
              key={label}
              style={{
                display: "flex",
                gap: 15,
                alignItems: "center",
                padding: "18px 0",
                borderTop: "1px solid #354043",
                fontSize: 24,
                color: active ? "#f4f0e8" : "#677478",
              }}
            >
              <span
                style={{
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                  width: 24,
                  height: 24,
                  border: "1px solid #6e8187",
                  borderRadius: 3,
                }}
              >
                {active && <Icon name="check" size={18} />}
              </span>
              <Icon name={icon} size={24} />
              <span style={{ maxWidth: 205, fontSize: i === 2 ? 20 : 24 }}>
                {label}
              </span>
            </div>
          );
        })}
        <Tag style={{ fontSize: 20, marginTop: 20 }}>SOURCE FILE RETAINED</Tag>
        <Pointer x={36} y={168} press={1} opacity={window(t, 11.4, 12)} />
      </Panel>
      <Caption opacity={layers} top={1530}>
        Toggle a layer. Change the result.
        <br />
        Keep the structure of your work.
      </Caption>
      <Panel
        style={{
          ...A,
          left: 80,
          top: 1590,
          width: 920,
          padding: 28,
          opacity: (1 - wide) * (1 - layers),
        }}
      >
        <Tag style={{ marginBottom: 12 }}>
          {t < 10.5
            ? "TEXT / TAKE THE LONG WAY."
            : "LAYERS / SOURCE → ADJUSTMENT → TYPE → SUBJECT"}
        </Tag>
        {t < 10.5 && (
          <div
            style={{
              fontSize: 29,
              color: muted,
              margin: "8px 0 10px",
              display: "flex",
              gap: 20,
              alignItems: "center",
            }}
          >
            <Icon name="text" size={27} />
            Helvetica Neue · Bold
          </div>
        )}
        {t < 8 ? (
          <Control
            label="Tracking"
            value={mix(-5, 2, track)}
            text={`${mix(-5, 2, track).toFixed(1)} px`}
            min={-20}
            max={30}
            cursor={t > 5.5 && t < 7.5}
          />
        ) : t < 10.5 ? (
          <Control
            label="Font size"
            value={mix(166, 150, size)}
            text={`${Math.round(mix(166, 150, size))} px`}
            min={80}
            max={300}
            cursor
          />
        ) : (
          <div
            style={{
              display: "flex",
              alignItems: "center",
              gap: 20,
              fontSize: 34,
              padding: "20px 0",
            }}
          >
            <Dot active={hide < 0.5} />
            <Icon name="selection" size={32} />
            Subject mask
            <span style={{ marginLeft: "auto", color: muted }}>
              {hide > 0.5 ? "Hidden" : "Visible"}
            </span>
          </div>
        )}
      </Panel>
      <Panel
        style={{
          ...A,
          left: 80,
          top: 1260,
          width: 920,
          padding: 30,
          opacity: wide,
          translate: `0 ${50 * (1 - wide)}px`,
        }}
      >
        <Tag style={{ marginBottom: 20 }}>CANVAS / 16:9 THUMBNAIL</Tag>
        <div
          style={{
            fontSize: 36,
            display: "flex",
            gap: 25,
            alignItems: "center",
          }}
        >
          <Icon name="crop" size={34} />
          Resize the canvas. Recompose the layers.
        </div>
        <div style={{ fontSize: 30, color: muted, marginTop: 25 }}>
          Original photo · Editable type · Subject mask · Paint
        </div>
      </Panel>
      <Caption opacity={wide}>
        A client poster. A video thumbnail. One creative workspace.
      </Caption>
    </>
  );
};

export const CarouselStudio = () => {
  const t = useTime(),
    { fps } = useVideoConfig(),
    split = s(t, 0.8, 3),
    crop = 52 * s(t, 3.8, 6.1) * (1 - s(t, 6.6, 7.8)),
    preview = s(t, 8.2, 9.5) * (1 - s(t, 12, 13.4)),
    swipe = s(t, 9.8, 11.3),
    all = s(t, 12, 14.1),
    enter = s(t, 17.2, 20);
  const bw = mix(mix(500, 610, preview), 222, all),
    gap = mix(28 * split * (1 - preview), 22, all),
    left = mix(mix(40 - 14 * split, 235, preview), 54, all),
    top = mix(570, 650, all),
    height = bw * 1.25;
  return (
    <>
      <Bar mode="Carousel / linked canvases" />
      <div style={{ opacity: 1 - enter }}>
        <Title
          t={t}
          b={7.9}
          lines={["Let the image", "keep going."]}
          detail="One photograph. Multiple canvases."
        />
        <Title
          t={t}
          a={8}
          b={12.1}
          lines={["Made for", "a seamless swipe."]}
          detail="Reposition once. Keep every edge connected."
        />
        <Title
          t={t}
          a={12.3}
          b={20.4}
          lines={["Photos. Design. Video.", "One complete story."]}
          size={88}
          detail="Mix the media. Export the canvases in order."
        />
      </div>
      <div
        style={{
          ...A,
          left,
          top,
          width: mix(mix(1000 + 28 * split, 610, preview), 972, all),
          height,
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
              height,
              overflow: "hidden",
            }}
          >
            <div
              style={{
                ...A,
                left: -i * bw - 72 + crop,
                top: 0,
                width: bw * 2 + 144,
                height,
              }}
            >
              <Photo src="IMG_5071.jpg" />
            </div>
            <Tag
              style={{
                ...A,
                left: 14,
                top: 14,
                color: "white",
                opacity: split * (1 - preview),
                background: "#1117",
                padding: 7,
              }}
            >
              0{i + 1}
            </Tag>
          </div>
        ))}
        <div
          style={{
            ...A,
            left: 2 * (bw + gap),
            top: 0,
            width: bw,
            height,
            opacity: all,
          }}
        >
          <Artwork />
        </div>
      </div>
      <div
        style={{
          ...A,
          left: mix(786, 0, enter),
          top: mix(650, 540, enter),
          width: mix(222, 1080, enter),
          height: mix(277.5, 820, enter),
          overflow: "hidden",
          opacity: s(t, 13.4, 14.2),
          boxShadow: "0 30px 90px #0007",
        }}
      >
        <Photo src="studio/arrival-first.jpg" style={{ ...A, inset: 0 }} />
        <Sequence from={14 * fps} layout="none">
          <Video
            src={staticFile("media/refine/arrival.mp4")}
            muted
            objectFit="cover"
            style={{ width: "100%", height: "100%" }}
          />
        </Sequence>
        <Tag
          style={{
            ...A,
            left: 14,
            top: 14,
            color: "white",
            background: "#1117",
            padding: 7,
            opacity: 1 - enter,
          }}
        >
          04 / VIDEO
        </Tag>
      </div>
      <Panel
        style={{
          ...A,
          left: 80,
          top: 1370,
          width: 920,
          padding: 30,
          opacity: 1 - enter,
        }}
      >
        <Tag style={{ marginBottom: 18 }}>
          {all > 0.5
            ? "ORDERED EXPORT / MIXED MEDIA"
            : preview > 0.5
              ? "SWIPE PREVIEW"
              : "SHARED PHOTO / LINKED POSITION"}
        </Tag>
        {all > 0.5 ? (
          <div
            style={{
              fontSize: 35,
              display: "flex",
              alignItems: "center",
              gap: 20,
            }}
          >
            <Icon name="check" size={32} />
            01.png · 02.png · 03.png · 04.mp4
          </div>
        ) : preview > 0.5 ? (
          <div
            style={{
              display: "flex",
              gap: 14,
              padding: 25,
              justifyContent: "center",
            }}
          >
            {[0, 1].map((i) => (
              <span
                key={i}
                style={{
                  width: (swipe > 0.5 ? i === 1 : i === 0) ? 56 : 12,
                  height: 12,
                  borderRadius: 12,
                  background: (swipe > 0.5 ? i === 1 : i === 0)
                    ? "#eee"
                    : "#50595b",
                }}
              />
            ))}
          </div>
        ) : (
          <Control
            label="Position X"
            value={crop}
            text={`${Math.round(crop)} px`}
            min={-100}
            max={100}
            cursor={t > 3.8 && t < 6.1}
          />
        )}
      </Panel>
      <Caption opacity={1 - enter}>
        Built for social. With the same creative control.
      </Caption>
    </>
  );
};
