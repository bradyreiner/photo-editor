import { Sequence, staticFile, useVideoConfig } from "remotion";
import { Video } from "@remotion/media";
import { AppActivity, AppWordmark } from "../app-kit/AppElements";
import {
  A,
  Bar,
  Caption,
  ColorResponse,
  Control,
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
import { Artwork } from "./Artwork";
import { sceneSamples } from "./scene-data";

export const SceneStudio = () => {
  const t = useTime(),
    { fps } = useVideoConfig(),
    sample = sceneSamples[Math.max(0, Math.min(359, Math.floor((t - 3) * 30)))],
    strip = s(t, 15, 17.1);
  return (
    <>
      <Bar mode="Gaussian scenes / new camera views" />
      <Title
        t={t}
        b={5.6}
        lines={["Gaussian", "splatting."]}
        detail="Move beyond a fixed viewpoint."
      />
      <Title
        t={t}
        a={5.7}
        b={12}
        lines={["Find the camera.", "Choose the focus."]}
        detail="Orbit the capture. Save your next viewpoint."
        size={96}
      />
      <Title
        t={t}
        a={12.1}
        b={18.4}
        lines={["Render a still.", "Make a camera move."]}
        detail="Bring new views into your creative workflow."
        size={94}
      />
      <div
        style={{
          ...A,
          left: 0,
          top: 500,
          width: 1080,
          height: 900,
          mixBlendMode: "screen",
          scale: mix(1, 0.78, strip),
          translate: `0 ${-120 * strip}px`,
          transformOrigin: "50% 45%",
          overflow: "hidden",
        }}
      >
        <Photo
          src={t < 3 ? "studio/scene-first.jpg" : "studio/scene-last.jpg"}
          style={{ objectFit: "contain" }}
        />
        <Sequence from={3 * fps} durationInFrames={12 * fps} layout="none">
          <Video
            src={staticFile("media/studio/gaussian-camera.mp4")}
            muted
            objectFit="contain"
            style={{ width: "100%", height: "100%" }}
          />
        </Sequence>
      </div>
      <Panel
        style={{
          ...A,
          left: 80,
          top: 1450,
          width: 920,
          padding: 30,
          background: "#11181bec",
          opacity: 1 - strip,
        }}
      >
        <Tag style={{ marginBottom: 14 }}>
          PHOTOGRAPHIC SPLAT / 113,648 GAUSSIANS
        </Tag>
        {t < 7 ? (
          <Control
            label="Camera yaw"
            value={sample.yawDegrees}
            text={`${sample.yawDegrees.toFixed(1)}°`}
            min={-90}
            max={90}
            cursor={t > 3 && t < 7}
          />
        ) : t < 12 ? (
          <Control
            label="Focus distance"
            value={sample.focusDistance}
            text={`${sample.focusDistance.toFixed(2)} u`}
            min={0.5}
            max={3}
            cursor
          />
        ) : (
          <Control
            label="Exposure"
            value={sample.exposureEv}
            text={`+${sample.exposureEv.toFixed(2)} EV`}
            min={-5}
            max={5}
            cursor
          />
        )}
      </Panel>
      <div
        style={{
          ...A,
          left: 80,
          top: 1460,
          display: "flex",
          gap: 12,
          opacity: strip,
          translate: `0 ${60 * (1 - strip)}px`,
        }}
      >
        {[0, 60, 120, 200, 270, 359].map((i) => (
          <div
            key={i}
            style={{
              width: 143,
              height: 161,
              overflow: "hidden",
              border: "1px solid #333",
            }}
          >
            <Photo src={`studio/scene/${String(i).padStart(4, "0")}.png`} />
          </div>
        ))}
      </div>
      <Caption opacity={1 - strip}>
        Import a Gaussian PLY. Move through the captured object.
      </Caption>
      <Caption opacity={strip}>
        New stills and camera paths, rendered in Looksy.
      </Caption>
    </>
  );
};

const batch = [
  ["studio/color/0-0.png", "studio/color/0-4.png"],
  ["studio/batch-5103/batch-before.png", "studio/batch-5103/batch-after.png"],
  ["studio/batch-5111/batch-before.png", "studio/batch-5111/batch-after.png"],
];
export const LocalStudio = () => {
  const t = useTime(),
    query = "yellow jacket in the mountains",
    typed = query.slice(0, Math.floor(query.length * s(t, 0.7, 3))),
    agent = s(t, 6.3, 8.3),
    apply = s(t, 11.5, 13.6),
    undo = window(t, 15.4, 17),
    manual = s(t, 18, 20.5),
    adjust = 0.8 * apply * (1 - undo) - 0.08 * manual,
    selected = s(t, 7, 9);
  return (
    <>
      <Bar mode="Optional local agent / your originals" />
      <Title
        t={t}
        b={6.3}
        lines={["Find the shot.", "Keep it local."]}
        detail="Describe what is in your own photographs."
      />
      <Title
        t={t}
        a={6.4}
        b={14.5}
        lines={["Hand off the", "repetitive edits."]}
        detail="An optional local model. A reviewed batch edit."
      />
      <Title
        t={t}
        a={14.6}
        b={22.5}
        lines={["Keep every", "creative decision."]}
        detail="Originals. Layers. Full edit history."
      />
      <Panel
        style={{
          ...A,
          left: 80,
          top: 500,
          width: 920,
          padding: 30,
          opacity: 1 - agent,
          translate: `0 ${-30 * agent}px`,
        }}
      >
        <div
          style={{
            display: "flex",
            gap: 22,
            alignItems: "center",
            fontSize: 36,
          }}
        >
          <Icon name="search" size={35} />
          {typed}
        </div>
        <Tag style={{ marginTop: 23 }}>LOCAL VISUAL SEARCH / YOUR LIBRARY</Tag>
      </Panel>
      {batch.map((pair, i) => (
        <div
          key={i}
          style={{
            ...A,
            left: mix(80 + i * 318, i === 0 ? 80 : 1110 + i * 318, selected),
            top: mix(760, 610, selected),
            width: mix(284, i === 0 ? 480 : 284, selected),
            height: mix(426, i === 0 ? 720 : 426, selected),
            overflow: "hidden",
            borderRadius: 5,
            boxShadow: "0 20px 70px #0007",
          }}
        >
          {i === 0 ? (
            <ColorResponse stage={0} amount={Math.max(0, adjust / 1.4)} />
          ) : (
            <>
              <Photo src={pair[0]} />
              <Photo
                src={pair[1]}
                style={{ ...A, inset: 0, opacity: apply * (1 - undo) }}
              />
            </>
          )}
          {i > 0 && (
            <div
              style={{
                ...A,
                inset: 0,
                background: "#87c2b310",
                opacity: apply,
              }}
            />
          )}
        </div>
      ))}
      <Panel
        style={{
          ...A,
          left: 590,
          top: 610,
          width: 410,
          padding: 25,
          opacity: agent,
          translate: `${40 * (1 - agent)}px 0`,
        }}
      >
        <Tag>LOCAL MODEL / CONNECTED</Tag>
        <div style={{ fontSize: 32, lineHeight: 1.32, margin: "28px 0" }}>
          Raise the exposure
          <br />
          across these three RAWs.
        </div>
        <div
          style={{
            fontSize: 28,
            lineHeight: 1.7,
            color: muted,
            borderTop: "1px solid #354043",
            paddingTop: 20,
          }}
        >
          Exposure +0.80 EV
          <br />3 originals retained
          <br />
          Editable adjustments
        </div>
        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: 16,
            fontSize: 29,
            marginTop: 27,
          }}
        >
          <AppActivity
            size={36}
            progress={apply}
            active={t > 11.5 && t < 13.6}
          />
          {apply > 0.99 ? "Edits applied" : "Review → Apply"}
        </div>
      </Panel>
      <Panel
        style={{
          ...A,
          left: 80,
          top: 1410,
          width: 920,
          padding: 30,
          opacity: agent,
        }}
      >
        <Tag style={{ marginBottom: 18 }}>
          {manual > 0.1
            ? "YOUR MANUAL REFINEMENT"
            : "EXPOSURE / EDITABLE ADJUSTMENT LAYER"}
        </Tag>
        <Control
          label="Exposure"
          value={adjust}
          text={`+${adjust.toFixed(2)} EV`}
          min={-5}
          max={5}
          cursor={t > 18 && t < 20.5}
        />
        <div
          style={{
            display: "flex",
            gap: 30,
            fontSize: 29,
            color: muted,
            alignItems: "center",
            marginTop: 20,
          }}
        >
          <Icon name="undo" size={28} />
          <span style={{ color: undo > 0.5 ? "white" : muted }}>Undo</span>
          <span>Redo</span>
          <span style={{ marginLeft: "auto" }}>
            Layers and history retained
          </span>
        </div>
      </Panel>
      <Caption>
        {t < 6.4 ? (
          <>
            Bring your own local model.
            <br />
            Your media stays on your machine.
          </>
        ) : (
          <>
            Editing operations. No image generation.
            <br />
            Refine every result by hand.
          </>
        )}
      </Caption>
    </>
  );
};

export const DeliveryStudio = () => {
  const t = useTime(),
    { fps } = useVideoConfig(),
    done = s(t, 0.8, 4.2),
    brand = s(t, 5.1, 7.4);
  return (
    <>
      <Bar mode="Deliver / finished work" opacity={1 - brand} />
      <div
        style={{
          opacity: 1 - brand,
          translate: `0 ${180 * brand}px`,
          scale: 1 - 0.04 * brand,
          transformOrigin: "50% 30%",
        }}
      >
        <Title
          t={t}
          a={-0.1}
          b={5.6}
          lines={["From source.", "To finished."]}
          detail="Photo. Design. Film. Scene."
        />
        <div
          style={{
            ...A,
            left: 80,
            top: 540,
            width: 440,
            height: 550,
            overflow: "hidden",
          }}
        >
          <Photo src="studio/retouch-final.png" />
          <Tag
            style={{
              ...A,
              left: 18,
              top: 18,
              color: "white",
              background: "#1117",
              padding: 8,
            }}
          >
            PHOTOGRAPH / TIFF
          </Tag>
        </div>
        <div
          style={{
            ...A,
            left: 560,
            top: 540,
            width: 440,
            height: 550,
            overflow: "hidden",
          }}
        >
          <Artwork />
          <Tag
            style={{
              ...A,
              left: 18,
              top: 18,
              color: "white",
              background: "#1117",
              padding: 8,
            }}
          >
            DESIGN / LAYERS
          </Tag>
        </div>
        <div
          style={{
            ...A,
            left: 80,
            top: 1130,
            width: 440,
            height: 440,
            overflow: "hidden",
            background: "black",
          }}
        >
          <Photo
            src="studio/scene-delivery.jpg"
            style={{ objectFit: "contain" }}
          />
          <Sequence durationInFrames={6 * fps} layout="none">
            <Video
              src={staticFile("media/studio/gaussian-camera.mp4")}
              trimBefore={3 * fps}
              muted
              objectFit="contain"
              style={{ width: "100%", height: "100%" }}
            />
          </Sequence>
          <Tag
            style={{
              ...A,
              left: 18,
              top: 18,
              color: "white",
              background: "#1117",
              padding: 8,
            }}
          >
            SCENE / CAMERA PATH
          </Tag>
        </div>
        <div
          style={{
            ...A,
            left: 560,
            top: 1130,
            width: 440,
            height: 440,
            overflow: "hidden",
          }}
        >
          <Photo src="studio/city-delivery.jpg" />
          <Sequence durationInFrames={6 * fps} layout="none">
            <Video
              src={staticFile("media/launch-1268.mp4")}
              muted
              objectFit="cover"
              style={{ width: "100%", height: "100%" }}
            />
          </Sequence>
          <Tag
            style={{
              ...A,
              left: 18,
              top: 18,
              color: "white",
              background: "#1117",
              padding: 8,
            }}
          >
            FILM / 4K MASTER
          </Tag>
        </div>
        <Panel style={{ ...A, left: 80, top: 1630, width: 920, padding: 28 }}>
          <div
            style={{
              display: "flex",
              gap: 20,
              fontSize: 33,
              alignItems: "center",
            }}
          >
            <AppActivity size={40} active={done < 0.99} progress={done} />
            {done > 0.99
              ? "Ready to deliver."
              : "Exporting full-resolution work."}
          </div>
          <div style={{ height: 3, background: "#394346", marginTop: 25 }}>
            <div
              style={{
                height: 3,
                width: `${100 * done}%`,
                background: "#bed4d3",
              }}
            />
          </div>
        </Panel>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 560,
          width: 920,
          opacity: brand,
          translate: `0 ${50 * (1 - brand)}px`,
        }}
      >
        <AppWordmark width={830} />
        <div
          style={{
            fontSize: 80,
            fontWeight: 500,
            letterSpacing: -3.5,
            lineHeight: 1.1,
            marginTop: 100,
          }}
        >
          Made for the work
          <br />
          you want to make.
        </div>
        <div style={{ fontSize: 41, color: muted, marginTop: 58 }}>
          A photo and video studio.
          <br />
          For professionals and hobbyists alike.
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1660,
          width: 920,
          fontSize: 25,
          lineHeight: 1.5,
          color: "#899295",
          opacity: s(t, 7.6, 9),
        }}
      >
        Music: “Origami” — Scott Buckley · CC BY 4.0
        <br />
        Photographic Gaussian scan: Marcel Padilla · CC BY 4.0
      </div>
    </>
  );
};
