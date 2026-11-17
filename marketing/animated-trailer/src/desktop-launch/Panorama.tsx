import { AbsoluteFill, CanvasImage, staticFile } from "remotion";
import { font } from "../rebuild/Type";

// Art direction for the native spanning-image capture. One image coordinate
// system is shared by all three 1080 × 1350 delivery canvases.
export const PanoramaArtwork = ({
  imagePan = 0,
  imageZoom = 1,
}: {
  imagePan?: number;
  imageZoom?: number;
}) => (
  <AbsoluteFill
    style={{ background: "#182536", color: "#f2eee1", overflow: "hidden" }}
  >
    <CanvasImage
      src={staticFile("media/IMG_5071.jpg")}
      style={{
        position: "absolute",
        inset: 0,
        width: "100%",
        height: "100%",
        objectFit: "cover",
        objectPosition: "50% 49%",
        transform: `translateX(${imagePan}px) scale(${imageZoom})`,
      }}
    />
    <AbsoluteFill
      style={{
        background:
          "linear-gradient(180deg,#0e233637 0%,transparent 32%,#15283b22 47%,#081522dc 100%)",
      }}
    />
    {[0, 1, 2].map((n) => (
      <div
        key={n}
        style={{
          position: "absolute",
          left: n * 1080 + 76,
          top: 69,
          width: 928,
          display: "flex",
          justifyContent: "space-between",
          fontFamily: font.mono,
          fontSize: 25,
          letterSpacing: 1.5,
        }}
      >
        <span>FIELD JOURNAL / NEPAL</span>
        <span>0{n + 1} — 03</span>
      </div>
    ))}
    <div
      style={{
        position: "absolute",
        top: 124,
        left: 76,
        width: 3088,
        height: 2,
        background: "#f2eee17a",
      }}
    />
    <div
      style={{
        position: "absolute",
        left: 72,
        top: 529,
        fontFamily: font.serif,
        fontStyle: "italic",
        fontSize: 190,
        lineHeight: 1,
        letterSpacing: -4,
      }}
    >
      Take the
    </div>
    <div
      style={{
        position: "absolute",
        left: 69,
        top: 719,
        fontFamily: font.display,
        fontSize: 365,
        lineHeight: 0.98,
        letterSpacing: -3,
      }}
    >
      LONG WAY.
    </div>
    <div
      style={{
        position: "absolute",
        left: 2247,
        top: 727,
        fontFamily: font.serif,
        fontSize: 115,
        fontStyle: "italic",
        lineHeight: 1.01,
        letterSpacing: -2,
      }}
    >
      Some things
      <br />
      need a little
      <br />
      more room.
    </div>
    <div
      style={{
        position: "absolute",
        left: 1770,
        top: 172,
        width: 275,
        height: 94,
        background: "#f0834f",
        color: "#182331",
        fontFamily: font.mono,
        fontSize: 27,
        letterSpacing: 1,
        display: "flex",
        alignItems: "center",
        justifyContent: "center",
        rotate: "-5deg",
      }}
    >
      FIRST LIGHT ↗
    </div>
    <svg
      style={{ position: "absolute", left: 1438, top: 241 }}
      width="320"
      height="300"
      viewBox="0 0 320 300"
    >
      <path
        d="M28 266 L126 77 L159 154 L220 22 L294 266"
        stroke="#f2eee1"
        strokeWidth="2"
        fill="none"
        opacity=".7"
      />
      <circle cx="220" cy="22" r="9" fill="#f0834f" />
      <path d="M28 286H294" stroke="#f2eee1" strokeWidth="1" opacity=".5" />
    </svg>
    {[0, 1, 2].map((n) => (
      <div
        key={n}
        style={{
          position: "absolute",
          left: n * 1080 + 76,
          bottom: 72,
          width: 928,
          paddingTop: 24,
          borderTop: "2px solid #f2eee17a",
          fontFamily: font.mono,
          fontSize: 25,
          display: "flex",
          justifyContent: "space-between",
        }}
      >
        <span>
          {
            [
              "A LITTLE FURTHER.",
              "A DIFFERENT PERSPECTIVE.",
              "KEEP THE CURIOSITY.",
            ][n]
          }
        </span>
        <span>{n < 2 ? "SWIPE →" : "END / BEGIN AGAIN"}</span>
      </div>
    ))}
  </AbsoluteFill>
);
