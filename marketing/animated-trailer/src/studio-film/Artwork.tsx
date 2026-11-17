import { A, Photo, paper, mix } from "./Core";
export const Artwork = ({
  build = 1,
  foreground = 1,
  tracking = 2,
  font = 150,
  brush = 1,
  wide = 0,
}: {
  build?: number;
  foreground?: number;
  tracking?: number;
  font?: number;
  brush?: number;
  wide?: number;
}) => (
  <div
    style={{
      width: "100%",
      height: "100%",
      position: "relative",
      overflow: "hidden",
      containerType: "inline-size",
      background: paper,
    }}
  >
    <Photo
      src="studio/color/4-8.png"
      style={{ objectPosition: `${mix(50, 67, wide)}% 50%` }}
    />
    <div
      style={{
        ...A,
        inset: 0,
        background: "linear-gradient(0deg,#0d151c88,transparent 52%)",
        opacity: build,
      }}
    />
    <div
      style={{
        ...A,
        left: "7%",
        top: mix(10, 9, wide) + "%",
        fontSize: "2.8cqw",
        fontWeight: 500,
        color: paper,
        letterSpacing: ".36cqw",
        opacity: build,
      }}
    >
      FIELD NOTES / NEPAL
    </div>
    <div
      style={{
        ...A,
        left: "5.5%",
        right: "5.5%",
        top: mix(32, 29, wide) + "%",
        fontSize: `${font / 10.8}cqw`,
        fontWeight: 700,
        color: paper,
        lineHeight: 0.9,
        whiteSpace: "nowrap",
        letterSpacing: `${tracking / 10.8}cqw`,
        opacity: build,
        translate: `0 ${55 * (1 - build)}px`,
      }}
    >
      TAKE THE
      <br />
      LONG WAY.
    </div>
    <Photo
      src="studio/subject.png"
      style={{
        ...A,
        inset: 0,
        opacity: foreground,
        objectPosition: `${mix(50, 67, wide)}% 50%`,
      }}
    />
    <svg
      viewBox="0 0 1080 1350"
      preserveAspectRatio="none"
      width="100%"
      height="100%"
      style={{ ...A, inset: 0 }}
    >
      <path
        d="M80 1160 C250 1070 440 1110 595 1140"
        fill="none"
        stroke="#f5c65b"
        strokeWidth={8}
        strokeLinecap="round"
        pathLength={1}
        strokeDasharray={1}
        strokeDashoffset={1 - brush}
      />
    </svg>
    <div
      style={{
        ...A,
        left: "7%",
        bottom: "7%",
        fontSize: "3cqw",
        fontWeight: 500,
        color: paper,
        letterSpacing: ".12cqw",
        opacity: build,
      }}
    >
      Stories from the other side of familiar.
    </div>
  </div>
);
export const FinishedPoster = () => <Artwork />;
export const FilmTitle = ({
  motion = 0,
  small = false,
}: {
  motion?: number;
  small?: boolean;
}) => (
  <div
    style={{
      ...A,
      left: `${7 + motion * 8}%`,
      top: "25%",
      fontSize: small ? 72 : 130,
      fontWeight: 700,
      color: paper,
      letterSpacing: -5,
      lineHeight: 0.87,
    }}
  >
    THE LONG
    <br />
    WAY HOME.
  </div>
);
