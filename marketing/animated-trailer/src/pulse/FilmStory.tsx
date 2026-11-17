import { Video, Sequence, useStoryTime } from "./Timing";
import { staticFile, useVideoConfig } from "remotion";
import { AppIcon, AppInspectorSlider } from "../app-kit/AppElements";
import { sourceWaveforms } from "../launch/waveforms";
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

const WAVE = Object.values(sourceWaveforms)[0];
export const Program = ({ title = true }: { title?: boolean }) => {
  const t = useStoryTime(),
    { fps } = useVideoConfig();
  return (
    <>
      <Sequence durationInFrames={6 * fps} layout="none">
        <Video
          objectFit="cover"
          src={staticFile("media/refine/arrival.mp4")}
          trimBefore={Math.round(1.8 * fps)}
          muted
          style={{ width: "100%", height: "100%" }}
        />
      </Sequence>
      <Sequence from={6 * fps} durationInFrames={7 * fps} layout="none">
        <Video
          objectFit="cover"
          src={staticFile("media/refine/hike.mp4")}
          muted
          style={{
            width: "100%",
            height: "100%",

            objectPosition: "42% center",
          }}
        />
      </Sequence>
      <Sequence from={13 * fps} durationInFrames={6 * fps} layout="none">
        <Video
          objectFit="cover"
          src={staticFile("media/launch-1268.mp4")}
          muted
          style={{ width: "100%", height: "100%" }}
        />
      </Sequence>
      <Sequence from={19 * fps} durationInFrames={5 * fps} layout="none">
        <Video
          objectFit="cover"
          src={staticFile("media/refine/night.mp4")}
          muted
          style={{ width: "100%", height: "100%" }}
        />
      </Sequence>
      {title && (
        <div
          style={{
            ...A,
            left: "7%",
            top: "40%",
            fontSize: 59,
            letterSpacing: 9,
            lineHeight: 1.25,
            fontWeight: 600,
            color: "white",
            opacity: show(t, 2, 5.7) + show(t, 6.8, 11.5),
            translate: `${t < 6 ? 0 : 90 * q(t, 8.3, 10.5)}px 0`,
            textShadow: "0 2px 12px #0005",
          }}
        >
          NEPAL
          <br />
          <span style={{ fontSize: 20, letterSpacing: 5 }}>
            FIELD NOTES / A SHORT FILM
          </span>
        </div>
      )}
    </>
  );
};

const Waveform = ({
  width = 650,
  height = 50,
}: {
  width?: number;
  height?: number;
}) => (
  <svg width={width} height={height}>
    <path
      d={WAVE.map(
        (v, i) =>
          `M${(i / (WAVE.length - 1)) * width} ${height / 2 - v * height * 0.43}V${height / 2 + v * height * 0.43}`,
      ).join(" ")}
      stroke="#9bafa3"
      strokeWidth={1.4}
      fill="none"
    />
  </svg>
);

export const Film = () => {
  const t = useStoryTime(),
    unfold = q(t, 0.3, 2.4),
    focus = q(t, 7.8, 9) * (1 - q(t, 11.4, 12.5));
  const trim = q(t, 4.4, 6.2),
    color = show(t, 12.3, 16.5),
    effects = show(t, 16.6, 19.7),
    audio = show(t, 19.8, 23.5),
    end = q(t, 22.7, 24);
  return (
    <>
      <Chrome mode="Film timeline" />
      <Head
        t={t}
        b={7.5}
        lines={["From first cut.", "To full film."]}
        detail="4K timelines. Layered picture and sound."
      />
      <Head
        t={t}
        a={7.7}
        b={12}
        lines={["Give every frame", "its own movement."]}
        size={100}
        detail="Keyframe position, scale and animation."
      />
      <Head
        t={t}
        a={12.2}
        b={16.3}
        lines={["Your footage.", "Your color."]}
        detail="sRGB · Rec.709 · Log workflows"
      />
      <Head
        t={t}
        a={16.5}
        b={19.7}
        lines={["Build the look.", "Layer the effects."]}
        size={102}
      />
      <Head
        t={t}
        a={19.9}
        b={24.5}
        lines={["Bring it all", "together."]}
        detail="Picture. Titles. Effects. A complete sound mix."
      />
      <div
        style={{
          ...A,
          left: 80,
          top: 490,
          width: 920,
          height: 518,
          borderRadius: mix(22, 7, unfold),
          overflow: "hidden",
          scale: 1 + 0.055 * focus,
          transformOrigin: "50% 50%",
          boxShadow: "0 20px 70px #0008",
        }}
      >
        <Program />
        <div
          style={{
            ...A,
            inset: 0,
            translate: `${920 * (1 - end)}px 0`,
            overflow: "hidden",
          }}
        >
          <Sequence
            from={Math.round(22.7 * useVideoConfig().fps)}
            durationInFrames={Math.round(1.3 * useVideoConfig().fps)}
            layout="none"
          >
            <Video
              objectFit="cover"
              src={staticFile("media/refine/hike.mp4")}
              muted
              style={{ width: "100%", height: "100%" }}
            />
          </Sequence>
        </div>
        {effects > 0 && (
          <div
            style={{
              ...A,
              inset: 0,
              pointerEvents: "none",
              opacity: effects * 0.28,
              background:
                "radial-gradient(ellipse at 45% 45%,transparent 30%,#080a14 100%)",
            }}
          />
        )}
        {color > 0 && (
          <div
            style={{
              ...A,
              right: 20,
              bottom: 20,
              fontSize: 23,
              padding: "9px 13px",
              background: "#080808b0",
            }}
          >
            Rec.709 / Color transform
          </div>
        )}
      </div>
      <Surface
        style={{
          ...A,
          left: 80,
          top: 1050,
          width: 920,
          height: 485,
          borderRadius: 8,
          opacity: unfold * (1 - end),
          translate: `0 ${150 * (1 - unfold)}px`,
          scale: 1 + 0.04 * focus,
          transformOrigin: "50% 0",
        }}
      >
        <div
          style={{
            display: "flex",
            justifyContent: "space-between",
            padding: "22px 25px",
            borderBottom: `1px solid ${C.lineSoft}`,
            fontSize: 24,
          }}
        >
          <span>NEPAL / SHORT FILM</span>
          <Label>3840 × 2160 / 30 fps</Label>
        </div>
        <div
          style={{
            ...A,
            left: 170,
            right: 20,
            top: 80,
            display: "flex",
            justifyContent: "space-between",
            fontSize: 19,
            color: C.textMuted,
          }}
        >
          <span>00:00</span>
          <span>00:30</span>
          <span>01:00</span>
          <span>01:30</span>
          <span>02:00</span>
        </div>
        {[
          ["V3", "Titles"],
          ["V2", "Picture"],
          ["A1", "Location sound"],
          ["A2", "Score"],
        ].map(([tag, name], i) => (
          <div
            key={tag}
            style={{
              ...A,
              left: 0,
              right: 0,
              top: 116 + i * 83,
              height: 83,
              borderTop: `1px solid ${C.lineSoft}`,
            }}
          >
            <div
              style={{
                ...A,
                left: 20,
                top: 23,
                fontSize: 19,
                color: C.textMuted,
              }}
            >
              {tag}
              <br />
              <span style={{ fontSize: 14 }}>{name}</span>
            </div>
            <div
              style={{
                ...A,
                left: 165 + (i === 0 ? 40 : 0),
                top: 8,
                height: 65,
                width: i === 0 ? 350 - trim * 65 : 725,
                background:
                  i < 2 ? (i === 0 ? "#475468" : "#323e4a") : "#2e3d35",
                border: `1px solid ${i === 0 ? "#90a7c4" : "#61776b"}`,
                borderRadius: 5,
                overflow: "hidden",
                boxSizing: "border-box",
              }}
            >
              {i === 0 ? (
                <>
                  <div style={{ padding: "10px 14px", fontSize: 18 }}>
                    NEPAL / FIELD NOTES
                  </div>
                  <div
                    style={{
                      position: "absolute",
                      left: 14,
                      right: 14,
                      bottom: 12,
                      height: 1,
                      background: "#a7b4c8",
                    }}
                  />
                  {[0, 1].map((v) => (
                    <div
                      key={v}
                      style={{
                        ...A,
                        left: 25 + v * 170,
                        bottom: 7,
                        width: 10,
                        height: 10,
                        rotate: "45deg",
                        background: "#e1e9f3",
                        opacity: q(t, 8, 9),
                      }}
                    />
                  ))}
                </>
              ) : i === 1 ? (
                <div
                  style={{
                    display: "flex",
                    gap: 3,
                    height: "100%",
                    opacity: 0.8,
                  }}
                >
                  {[
                    "IMG_1207-2-7.jpg",
                    "IMG_1268-2-7.jpg",
                    "IMG_5071.jpg",
                    "IMG_1207-2-7.jpg",
                    "IMG_1268-2-7.jpg",
                  ].map((src, n) => (
                    <div
                      key={n}
                      style={{ width: 142, height: "100%", flexShrink: 0 }}
                    >
                      <Picture src={src} />
                    </div>
                  ))}
                </div>
              ) : (
                <div style={{ padding: "8px 9px", opacity: 0.8 }}>
                  <Waveform width={710} />
                </div>
              )}
            </div>
          </div>
        ))}
        <div
          style={{
            ...A,
            left: 165 + 600 * q(t, 2.5, 23.8),
            top: 106,
            height: 350,
            width: 2,
            background: "#f0f3f4",
          }}
        >
          <div
            style={{
              width: 10,
              height: 10,
              background: "#f0f3f4",
              translate: "-4px 0",
            }}
          />
        </div>
        <div
          style={{
            ...A,
            left: 205 + 350 - trim * 65 - 7,
            top: 124,
            width: 6,
            height: 65,
            background: "#d3e2f3",
            opacity: show(t, 4, 6.8),
          }}
        />
      </Surface>
      <Surface
        style={{
          ...A,
          left: 80,
          top: 1570,
          width: 920,
          padding: "20px 25px",
          opacity: unfold * (1 - end),
          height: 240,
        }}
      >
        <div
          style={{
            ...A,
            inset: "20px 25px",
            opacity: show(t, 1.3, 12.65) * (1 - color),
          }}
        >
          <Label>
            {t >= 7.7 ? "MOTION / KEYFRAMES" : "EDIT / TRIM & ARRANGE"}
          </Label>
          <AppInspectorSlider
            label={t >= 7.7 ? "Position X" : "Clip out"}
            value={t >= 7.7 ? 90 * q(t, 8.3, 10.5) : 18 - 2 * trim}
            valueText={
              t >= 7.7
                ? `${Math.round(90 * q(t, 8.3, 10.5))} px`
                : `00:00:${Math.round(18 - 2 * trim)}`
            }
            from={t >= 7.7 ? -500 : 0}
            to={t >= 7.7 ? 500 : 30}
            scale={2}
            width={435}
            showCursor={t >= 7.7 || (t > 4.4 && t < 6.2)}
          />
          <div
            style={{
              display: "flex",
              gap: 22,
              fontSize: 25,
              color: C.textMuted,
              marginTop: 10,
            }}
          >
            <AppIcon name="keyframe" size={25} />
            {t >= 7.7
              ? "Editable animation curves"
              : "Trim · Split · Ripple · Arrange"}
          </div>
        </div>
        <div style={{ ...A, inset: "20px 25px", opacity: color }}>
          <Label>COLOR / INPUT TRANSFORM → Rec.709</Label>
          <AppInspectorSlider
            label="Exposure"
            value={0.24 * q(t, 13, 14.5)}
            valueText={`+${(0.24 * q(t, 13, 14.5)).toFixed(2)} EV`}
            from={-5}
            to={5}
            scale={2}
            width={435}
            showCursor={t > 13 && t < 14.5}
          />
          <div style={{ fontSize: 27, color: C.textMuted, marginTop: 10 }}>
            Tone curves · Color adjustments · Detail
          </div>
        </div>
        <div style={{ ...A, inset: "20px 25px", opacity: effects }}>
          <Label>EFFECT STACK / EDITABLE</Label>
          <div
            style={{
              fontSize: 30,
              display: "flex",
              alignItems: "center",
              gap: 20,
              marginTop: 15,
            }}
          >
            <AppIcon name="layers" size={30} />
            Color transform → Vignette → Grain
          </div>
          <AppInspectorSlider
            label="Amount"
            value={28 * q(t, 17, 18.5)}
            valueText={`${Math.round(28 * q(t, 17, 18.5))}%`}
            from={0}
            to={100}
            scale={2}
            width={435}
            showCursor={t > 17 && t < 18.5}
          />
        </div>
        <div style={{ ...A, inset: "20px 25px", opacity: audio }}>
          <Label>AUDIO MIXER / TRACK LEVELS</Label>
          <AppInspectorSlider
            label="Location"
            value={-3}
            valueText="−3.0 dB"
            from={-60}
            to={12}
            scale={2}
            width={435}
          />
          <AppInspectorSlider
            label="Score"
            value={mix(-6, -12, q(t, 20.5, 21.8))}
            valueText={`${mix(-6, -12, q(t, 20.5, 21.8)).toFixed(1)} dB`}
            from={-60}
            to={12}
            scale={2}
            width={435}
            showCursor={t > 20.5 && t < 21.8}
          />
        </div>
      </Surface>
      <Light t={t + 76} amount={end * 0.3} y={1460} />
    </>
  );
};

export const Formats = () => {
  const t = useStoryTime(),
    portrait = q(t, 1.2, 3.8),
    deliver = q(t, 7.8, 9.4);
  const w = mix(920, 555, portrait),
    h = mix(518, 987, portrait);
  return (
    <>
      <Chrome mode="Sequence formats" />
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
        [
          "image",
          "Photography",
          "JPEG · PNG · TIFF",
          "refine/portrait-window.jpg",
        ],
        [
          "layers",
          "Design & carousel",
          "Full-resolution canvases",
          "refine/coast-blue.jpg",
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
            <Picture src={src} />
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
