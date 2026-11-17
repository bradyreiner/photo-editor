import { Video, useStoryTime } from "./Timing";
import { staticFile, useVideoConfig } from "remotion";
import { Poster } from "./PhotoStory";
import { AppIcon } from "../app-kit/AppElements";
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
  show,
  Surface,
} from "./Motion";

export const Formats = () => {
  const t = useStoryTime(),
    portrait = q(t, 1.2, 3.8),
    deliver = q(t, 7.8, 9.4);
  const w = mix(920, 555, portrait),
    h = mix(518, 987, portrait);
  return (
    <>
      <Chrome mode="Sequence formats" />
      <Light t={t + 103} y={1470} amount={0.4 * (1 - q(t, 0, 1.2))} />
      <Head
        t={t}
        b={10.3}
        lines={["Your film.", "Every format."]}
        detail="Cinema. YouTube. Reels. TikTok."
      />
      <div
        style={{
          ...A,
          left: (1080 - w) / 2,
          top: 490,
          width: w,
          height: h,
          borderRadius: 12,
          overflow: "hidden",
          boxShadow: "0 30px 80px #0009",
        }}
      >
        <Video
          objectFit="cover"
          src={staticFile("media/refine/hike.mp4")}
          trimBefore={Math.round(1.3 * useVideoConfig().fps)}
          muted
          style={{
            width: "100%",
            height: "100%",

            objectPosition: `${mix(50, 43, portrait)}% center`,
          }}
        />
        <div
          style={{
            ...A,
            left: 35,
            top: 40,
            right: 35,
            bottom: 90,
            border: "1px dashed #ffffff70",
            borderRadius: 5,
            opacity: show(t, 4, 7.5),
          }}
        />
        <div
          style={{
            ...A,
            left: "8%",
            right: "8%",
            bottom: "17%",
            fontSize: 52,
            color: "white",
            fontWeight: 600,
            letterSpacing: -2,
            opacity: q(t, 4.8, 5.7),
            translate: `0 ${mix(60, 0, q(t, 4.8, 6))}px`,
            textShadow: "0 2px 10px #0008",
          }}
        >
          A little further.
        </div>
      </div>
      <Surface style={{ ...A, left: 80, top: 1540, width: 920, padding: 27 }}>
        <Label>
          {deliver > 0.5
            ? "EXPORT / MULTIPLE FORMATS"
            : "REFRAME / 16:9 → 9:16"}
        </Label>
        <div
          style={{
            fontSize: 31,
            marginTop: 24,
            display: "flex",
            gap: 24,
            alignItems: "center",
          }}
        >
          <AppIcon name={deliver > 0.5 ? "check" : "crop"} size={32} />
          {deliver > 0.5
            ? "4K master + vertical social version"
            : "Reframe. Animate. Keep the full-resolution master."}
        </div>
      </Surface>
      <Note style={{ top: 1780, fontSize: 35 }}>
        Full productions and social content, in one editor.
      </Note>
    </>
  );
};

export const Delivery = ({ at }: { at?: number } = {}) => {
  const clock = useStoryTime(),
    t = at ?? clock,
    done = q(t, 2, 5.5),
    fold = q(t, 0, 1.4);
  return (
    <>
      <Chrome mode="Export" />
      <Head
        t={t}
        b={8.3}
        lines={["Full resolution.", "Ready to deliver."]}
        detail="Finished photographs, designs and films."
      />
      {[
        ["image", "Photography", "JPEG · PNG · TIFF", "IMG_5071.jpg"],
        [
          "layers",
          "Design & carousel",
          "Full-resolution canvases",
          "IMG_4870.jpg",
        ],
        [
          "play",
          "Film & video",
          "4K masters · Social formats",
          "refine/finished-film.jpg",
        ],
      ].map(([icon, title, line, src], i) => (
        <Surface
          key={title}
          style={{
            ...A,
            left: 80,
            top: 535 + i * 335,
            width: 920,
            height: 285,
            overflow: "hidden",
            translate: `0 ${mix(100, 0, q(t, 0.3 + i * 0.12, 1.4 + i * 0.12))}px`,
            opacity: q(t, 0.2 + i * 0.12, 1 + i * 0.12),
          }}
        >
          <div style={{ ...A, inset: "0 auto 0 0", width: 225 }}>
            {i === 1 ? <Poster t={10} /> : <Picture src={src} />}
          </div>
          <div style={{ ...A, left: 260, right: 30, top: 45 }}>
            <div
              style={{
                display: "flex",
                gap: 18,
                alignItems: "center",
                fontSize: 38,
              }}
            >
              <AppIcon name={icon} size={35} />
              {title}
            </div>
            <div style={{ color: C.textMuted, fontSize: 29, marginTop: 24 }}>
              {line}
            </div>
            <div
              style={{
                height: 3,
                marginTop: 38,
                width: `${Math.min(1, done * 1.4 - i * 0.15) * 100}%`,
                background: "#c7d9e1",
                borderRadius: 2,
              }}
            />
            <div
              style={{
                fontSize: 24,
                marginTop: 15,
                opacity: q(t, 5 + i * 0.25, 5.6 + i * 0.25),
                color: "#c6ded3",
              }}
            >
              ✓ Export complete
            </div>
          </div>
        </Surface>
      ))}
      <div
        style={{
          ...A,
          left: mix(262.5, 80, fold),
          top: mix(490, 1205, fold),
          width: mix(555, 225, fold),
          height: mix(987, 285, fold),
          overflow: "hidden",
          borderRadius: 12 * (1 - fold),
          opacity: 1 - q(t, 1.5, 1.9),
          boxShadow: `0 ${30 * (1 - fold)}px ${80 * (1 - fold)}px #0007`,
        }}
      >
        <Picture src="refine/finished-film.jpg" />
      </div>
      <Note style={{ top: 1650 }}>
        From a client gallery to your next personal project.
      </Note>
    </>
  );
};
