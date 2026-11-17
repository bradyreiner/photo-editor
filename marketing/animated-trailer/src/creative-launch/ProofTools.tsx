import { detailZoom } from "./State";
import {
  A,
  Pic,
  Label,
  font,
  lime,
  paper,
  smooth,
  out,
  mix,
  useShot,
} from "./Kit";
import { pixelSample } from "./pixel-data";

export const Pointer = ({
  x,
  y,
  press = 0,
  opacity = 1,
}: {
  x: number;
  y: number;
  press?: number;
  opacity?: number;
}) => (
  <svg
    width="40"
    height="48"
    viewBox="0 0 40 48"
    style={{
      ...A,
      left: x,
      top: y,
      overflow: "visible",
      opacity,
      transform: `scale(${1 - 0.1 * press})`,
      transformOrigin: "0 0",
      pointerEvents: "none",
    }}
  >
    <path
      d="M1 1V31L9 24L18 41L25 37L16 21H31Z"
      fill="white"
      stroke="#07111d"
      strokeWidth="2"
    />
  </svg>
);

export const BeforeAfter = ({
  before,
  s,
  finish = 5.2,
  height = 667,
  width = 1000,
}: {
  before: string;
  s: number;
  finish?: number;
  height?: number;
  width?: number;
}) => {
  const show = smooth(s, 0.15, 0.65) * (1 - smooth(s, finish, finish + 0.6));
  const split = 0.38 * show;
  return (
    <>
      <Pic
        src={before}
        style={{ clipPath: `inset(0 ${100 * (1 - split)}% 0 0)` }}
      />
      <div
        style={{
          ...A,
          left: width * split,
          top: 0,
          height,
          width: 2,
          background: paper,
          opacity: show,
        }}
      />
      <div
        style={{
          ...A,
          left: 20,
          top: 20,
          opacity: show,
          background: "#0b1324dc",
          borderRadius: 5,
          padding: "10px 14px",
        }}
      >
        <Label style={{ fontSize: 15, color: paper }}>BEFORE</Label>
      </div>
      <div
        style={{
          ...A,
          right: 20,
          top: 20,
          background: "#0b1324dc",
          borderRadius: 5,
          padding: "10px 14px",
          opacity: out(s, 0.4, 0.8),
        }}
      >
        <Label style={{ fontSize: 15 }}>AFTER / THIS EDIT</Label>
      </div>
    </>
  );
};

export const PixelProof = ({ s }: { s: number }) => {
  const { portrait } = useShot(),
    z = detailZoom(s);
  const cropWidth = mix((960 / 6024) * 1000, portrait ? 960 : 960 / 1.1, z),
    cropHeight = mix((960 / 4020) * 667, portrait ? 960 : 960 / 1.1, z);
  const cropLeft = mix(((6024 * 0.57 - 480) / 6024) * 1000, 0, z),
    cropTop = mix(40 + ((4020 * 0.59 - 480) / 4020) * 667, -35, z);
  const sourceX = cropLeft + (pixelSample.cropX / 960) * cropWidth,
    sourceY = cropTop + (pixelSample.cropY / 960) * cropHeight;
  const open = smooth(s, 5, 6) * (1 - smooth(s, 10, 10.8)),
    step = s < 6.9 ? 0 : s < 7.8 ? 1 : s < 8.7 ? 2 : s < 9.5 ? 3 : 4;
  const cells = [
    [2, 3],
    [3, 3],
    [4, 3],
  ];
  const selected = step === 0 ? [] : cells.slice(0, Math.min(3, step));
  const cx =
    s < 7.8
      ? mix(2, 3, smooth(s, 7.2, 7.7))
      : s < 8.7
        ? mix(3, 4, smooth(s, 8.1, 8.6))
        : mix(4, 3, smooth(s, 9, 9.45));
  const px = step < 2 ? 2 : step === 2 ? 3 : step === 3 ? 4 : 3,
    py = step === 4 ? 4 : 3;
  const press = Math.max(
    ...[6.9, 7.8, 8.7, 9.5].map(
      (t) => smooth(s, t - 0.08, t) * (1 - smooth(s, t + 0.03, t + 0.18)),
    ),
  );
  return (
    <div
      style={{
        ...A,
        left: 0,
        top: 0,
        width: 1000,
        height: 800,
        opacity: smooth(s, 4.95, 5.1) * (1 - smooth(s, 10.7, 10.9)),
        pointerEvents: "none",
      }}
    >
      <div
        style={{
          ...A,
          left: sourceX,
          top: sourceY,
          width: (8 / 960) * cropWidth,
          height: (8 / 960) * cropHeight,
          border: `2px solid ${lime}`,
          boxShadow: "0 0 0 2px #061120",
        }}
      />
      <svg style={{ ...A, inset: 0, width: 1000, height: 800 }}>
        <path
          d={`M${sourceX + 8} ${sourceY + 8}L430 160L460 160`}
          fill="none"
          stroke={lime}
          strokeWidth="2"
          pathLength="1"
          strokeDasharray="1"
          strokeDashoffset={1 - open}
        />
      </svg>
      <div
        style={{
          ...A,
          left: mix(sourceX, 440, open),
          top: mix(sourceY, 140, open),
          width: 512,
          height: 605,
          border: "1px solid #b8c9da77",
          borderRadius: 12,
          overflow: "hidden",
          background: "#101b2a",
          boxShadow: "0 25px 70px #000a",
          transform: `scale(${mix(8 / 512, 1, open)})`,
          transformOrigin: "0 0",
        }}
      >
        <Label style={{ fontSize: 16, padding: 18 }}>
          SELECTION EDGE / {step === 4 ? "ERASE" : "ADD"} / 1 PX
        </Label>
        <svg
          width="512"
          height="512"
          style={{ display: "block" }}
          viewBox="0 0 512 512"
        >
          {pixelSample.rgb.map((rgb, i) => {
            const x = i % 8,
              y = Math.floor(i / 8);
            return (
              <g key={i}>
                <rect
                  x={x * 64}
                  y={y * 64}
                  width="64"
                  height="64"
                  fill={`rgb(${rgb.join(",")})`}
                />
                <rect
                  x={x * 64}
                  y={y * 64}
                  width="64"
                  height="64"
                  fill="none"
                  stroke="#c7d5e84a"
                  strokeWidth="1"
                />
                {(y < 3 ||
                  (x === 3 && y === 4 && step < 4) ||
                  selected.some(([a, b]) => a === x && b === y)) && (
                  <rect
                    x={x * 64 + 2}
                    y={y * 64 + 2}
                    width="60"
                    height="60"
                    fill="#d7ff4860"
                    stroke={lime}
                    strokeWidth="3"
                  />
                )}
              </g>
            );
          })}
          <rect
            x={px * 64 + 1}
            y={py * 64 + 1}
            width="62"
            height="62"
            fill="none"
            stroke="white"
            strokeWidth="3"
          />
        </svg>
        <div
          style={{
            padding: "12px 18px",
            fontFamily: font.mono,
            fontSize: 13,
            color: paper,
          }}
        >
          SOURCE {pixelSample.sourceX + px}, {pixelSample.sourceY + py} ·
          SELECTED {selected.length + 24 + (step < 4 ? 1 : 0)} PX
        </div>
        <Pointer
          x={cx * 64 + 35}
          y={3 * 64 + 90 + 64 * smooth(s, 9, 9.45)}
          press={press}
        />
      </div>
      <Label style={{ ...A, left: 20, top: 750, fontSize: 16 }}>
        {step === 4
          ? "STRAY PIXEL REMOVED. EDGE RETAINED."
          : "SELECT THE EDGE. LEAVE THE BACKGROUND."}
      </Label>
    </div>
  );
};
