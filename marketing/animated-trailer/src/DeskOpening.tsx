import React, { useId } from "react";
import {
  AbsoluteFill,
  CanvasImage,
  Easing,
  interpolate,
  Sequence,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { palette } from "./StyleFrames";

const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
const travel = Easing.bezier(0.72, 0, 0.18, 1);
const settle = Easing.bezier(0.12, 0.9, 0.25, 1);
const p = (t: number, start: number, end: number, from = 0, to = 1) =>
  interpolate(t, [start, end], [from, to], { ...clamp, easing: travel });
const media = (name: string) => staticFile(`media/${name}`);
type Mode = "manual" | "agent";
const files = [
  "IMG_5106",
  "IMG_5071",
  "IMG_4870",
  "IMG_4969",
  "IMG_5103",
  "IMG_5111",
  "IMG_5125",
  "IMG_4856",
  "IMG_4744",
  "IMG_5053",
  "IMG_5124",
  "IMG_5077",
];

const Camera = () => (
  <g transform="translate(99 1310) rotate(-13 120 80)">
    <path
      d="M12 35H62L81 10H164L183 35H239Q259 35 259 57V155Q259 175 237 175H16Q0 175 0 155V54Q0 35 12 35Z"
      fill={palette.ink}
    />
    <path
      d="M22 41H66L87 17H155L176 41"
      fill="none"
      stroke="#777B73"
      strokeWidth="3"
    />
    <rect x="195" y="46" width="44" height="15" rx="4" fill={palette.paper} />
    <circle
      cx="128"
      cy="106"
      r="72"
      fill="#40443E"
      stroke={palette.ink}
      strokeWidth="13"
    />
    <circle
      cx="128"
      cy="106"
      r="52"
      fill={palette.ink}
      stroke="#777B73"
      strokeWidth="2"
    />
    <circle cx="128" cy="106" r="31" fill={palette.blue} />
    <path
      d="M104 105Q106 82 126 80"
      fill="none"
      stroke="#AEBBFF"
      strokeWidth="5"
      strokeLinecap="round"
    />
    <path
      d="M10 157Q-49 208 3 250Q60 289 76 213"
      fill="none"
      stroke={palette.blue}
      strokeWidth="13"
    />
    <rect x="6" y="62" width="21" height="80" rx="7" fill="#32372F" />
  </g>
);

export const DeskIllustration = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = frame / fps;
  const push = p(t, 3.6, 7);
  const card = interpolate(t, [0.7, 1.5], [0, 1], { ...clamp, easing: settle });
  return (
    <svg
      width="1080"
      height="1920"
      viewBox="0 0 1080 1920"
      style={{
        position: "absolute",
        inset: 0,
        transform: `translateY(${push * 880}px) scale(${1 + push * 0.6})`,
        transformOrigin: "540px 1060px",
        opacity: 1 - p(t, 5.2, 7),
      }}
    >
      <path d="M-180 1170L1007 1010L1330 1503L138 1710Z" fill="#D6D8CD" />
      <path d="M138 1710L1330 1503V1543L138 1750Z" fill="#92978B" />
      <path d="M-180 1170L138 1710V1750L-180 1210Z" fill="#A8AD9E" />
      <path
        d="M229 1737V2030M999 1603V1958"
        stroke={palette.ink}
        strokeWidth="31"
      />
      <path d="M467 1350L768 1310L940 1434L628 1482Z" fill={palette.ink} />
      <path
        d="M479 1340L780 1300L952 1424L640 1472Z"
        fill={palette.paper}
        stroke={palette.ink}
        strokeWidth="3"
      />
      {[0, 1, 2, 3].map((row) => (
        <g
          key={row}
          transform={`translate(${505 + row * 32} ${1346 + row * 22}) skewX(50) rotate(-7)`}
        >
          {Array.from({ length: 10 }, (_, n) => (
            <rect
              key={n}
              x={n * 25}
              width="19"
              height="13"
              rx="2"
              fill="#ADB2A5"
            />
          ))}
        </g>
      ))}
      <path
        d="M856 1463Q850 1422 882 1417Q913 1415 933 1442L965 1483Q965 1515 930 1515Q898 1513 885 1494Z"
        fill={palette.paper}
        stroke={palette.ink}
        strokeWidth="3"
      />
      <path d="M877 1425L902 1460" stroke={palette.ink} strokeWidth="2" />
      <g transform="translate(840 1180) rotate(11)">
        <rect width="152" height="216" rx="19" fill={palette.blue} />
        <rect x="9" y="8" width="134" height="191" rx="13" fill="#5371FF" />
        <text
          x="24"
          y="53"
          fill="white"
          fontFamily="Manrope"
          fontSize="26"
          fontWeight="800"
        >
          NEPAL
        </text>
        <path d="M24 74H114" stroke="#9FB1FF" />
        <circle cx="116" cy="176" r="5" fill={palette.green} />
        <text x="24" y="180" fill="white" fontSize="14">
          SSD / 01
        </text>
      </g>
      <path
        d="M899 1190C975 1100 1092 1050 971 1011L862 994"
        fill="none"
        stroke={palette.ink}
        strokeWidth="12"
      />
      <path
        d="M899 1190C975 1100 1092 1050 971 1011L862 994"
        fill="none"
        stroke={palette.green}
        strokeWidth="4"
        pathLength="1"
        strokeDasharray=".09 .91"
        strokeDashoffset={-((t * 0.65) % 1)}
      />
      <Camera />
      <g
        transform={`translate(${363 + card * 12} ${1433 - card * 146}) rotate(${14 - card * 22})`}
      >
        <path d="M0 0H68L87 20V111H0Z" fill={palette.red} />
        <rect x="9" y="35" width="67" height="52" fill={palette.paper} />
        <text x="20" y="68" fontSize="21" fill={palette.ink} fontWeight="800">
          RAW
        </text>
        {[0, 1, 2, 3, 4].map((i) => (
          <rect
            key={i}
            x={8 + i * 13}
            y="5"
            width="8"
            height="19"
            fill="#F5CF6C"
          />
        ))}
      </g>
      <g transform="translate(595 1510) rotate(-9)">
        <rect width="240" height="185" fill={palette.paper} stroke="#C1C5B8" />
        <text x="25" y="38" fill={palette.ink} fontSize="24" fontWeight="800">
          NEPAL / DELIVERY
        </text>
        {["Photographs", "Short film", "Carousel"].map((x, i) => (
          <g key={x} transform={`translate(25 ${70 + i * 34})`}>
            <rect
              y="-15"
              width="15"
              height="15"
              fill="none"
              stroke={palette.ink}
            />
            <text x="27" fill={palette.ink} fontSize="19">
              {x}
            </text>
          </g>
        ))}
      </g>
      <g transform="translate(30 870)">
        <path
          d="M0 146Q100 142 122 53M60 150Q62 58 12 5M48 151Q5 73 -41 75"
          fill="none"
          stroke="#566847"
          strokeWidth="8"
        />
        <path
          d="M91 98Q142 53 127 9Q78 23 91 98M41 87Q-2 27 11 -3Q64 14 41 87M16 125Q-63 129 -79 72Q-15 56 16 125"
          fill="#8DAA68"
        />
        <path
          d="M-1 143H93L76 249H17Z"
          fill={palette.paper}
          stroke={palette.ink}
          strokeWidth="3"
        />
      </g>
      <path
        d="M836 505L941 620L1011 716"
        fill="none"
        stroke={palette.ink}
        strokeWidth="11"
        strokeLinecap="round"
      />
      <circle cx="940" cy="619" r="14" fill={palette.blue} />
      <path d="M791 483L849 483L889 550L755 550Z" fill={palette.blue} />
      <path d="M784 550H861L930 822L673 773Z" fill="#E4E7D870" />
      <path d="M481 1090H622L655 1270H448Z" fill={palette.ink} />
      <path d="M415 1268H687L719 1304H385Z" fill="#444A40" />
    </svg>
  );
};

const StorageFlow = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = frame / fps;
  const camera = p(t, 3.6, 7);
  return (
    <svg
      width="1080"
      height="1920"
      viewBox="0 0 1080 1920"
      style={{
        position: "absolute",
        inset: 0,
        pointerEvents: "none",
        opacity: 1 - p(t, 5, 6),
      }}
    >
      {Array.from({ length: 13 }, (_, n) => {
        const u = Math.max(0, Math.min(1, (t - 1.6 - n * 0.14) / 1.45));
        const b = 1 - u;
        const x =
          b * b * b * 930 +
          3 * b * b * u * 1110 +
          3 * b * u * u * 600 +
          u * u * u * 530;
        const y =
          b * b * b * (1240 + camera * 500) +
          3 * b * b * u * 930 +
          3 * b * u * u * 1160 +
          u * u * u * (760 - camera * 150);
        return (
          <g
            key={n}
            opacity={u > 0 && u < 1 ? Math.sin(u * Math.PI) : 0}
            transform={`translate(${x} ${y}) rotate(${-18 + u * 32}) scale(${0.7 - u * 0.35})`}
          >
            <path
              d="M0 0H57L76 19V94H0Z"
              fill={n % 4 === 0 ? palette.blue : palette.paper}
              stroke={palette.ink}
              strokeWidth="3"
            />
            <path
              d="M57 0V20H76"
              fill="none"
              stroke={palette.ink}
              strokeWidth="2"
            />
            <rect
              x="11"
              y="29"
              width="53"
              height="34"
              fill={n % 4 === 0 ? "#9FAFFF" : "#C2D2B2"}
            />
            <path d="M12 62L29 42L43 54L52 45L64 62" fill={palette.blue} />
            <text
              x="11"
              y="84"
              fontSize="16"
              fill={palette.ink}
              fontWeight="800"
            >
              {n % 4 === 0 ? "VID" : "RAW"}
            </text>
          </g>
        );
      })}
    </svg>
  );
};

const PhotoTile = ({
  file,
  x,
  y,
  width,
  height,
  selected,
  label = true,
  opacity = 1,
  exposure = 0,
}: {
  file: string;
  x: number;
  y: number;
  width: number;
  height: number;
  selected?: boolean;
  label?: boolean;
  opacity?: number;
  exposure?: number;
}) => {
  const id = useId().replace(/:/g, "");
  return (
    <g transform={`translate(${x} ${y})`} opacity={opacity}>
      <defs>
        <clipPath id={id}>
          <rect width={width} height={height - (label ? 30 : 0)} rx="2" />
        </clipPath>
      </defs>
      <rect
        x="-3"
        y="-3"
        width={width + 6}
        height={height + 6}
        rx="4"
        fill={selected ? palette.blue : "#D8DCD0"}
      />
      <image
        href={media(`${file}.jpg`)}
        width={width}
        height={height - (label ? 30 : 0)}
        preserveAspectRatio="xMidYMid slice"
        clipPath={`url(#${id})`}
        style={{ filter: `brightness(${Math.pow(2, exposure * 0.32)})` }}
      />
      {label && (
        <>
          <rect
            y={height - 30}
            width={width}
            height="30"
            fill={palette.paper}
          />
          <text
            x="10"
            y={height - 10}
            fill="#666C5E"
            fontSize={Math.max(9, Math.min(17, width * 0.065))}
          >
            {file}.CR2
          </text>
        </>
      )}
      {selected && (
        <g transform={`translate(${width - 28} 8)`}>
          <circle r="13" cx="10" cy="10" fill={palette.blue} />
          <path
            d="M4 10L8 14L16 6"
            stroke="white"
            strokeWidth="2"
            fill="none"
          />
        </g>
      )}
    </g>
  );
};

const LibrarySurface = ({
  width,
  height,
  push,
  mode,
}: {
  width: number;
  height: number;
  push: number;
  mode: Mode;
}) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = frame / fps;
  const search = p(t, 11.2, 13.5);
  const agent = mode === "agent" ? p(t, 15.2, 17.3) : 0;
  const edit = mode === "agent" ? p(t, 22.3, 24.2) : p(t, 15.2, 17.3);
  const editClock = mode === "agent" ? t : t + 6.6;
  const exposure =
    p(editClock, 24, 24.7, 0, 0.35) -
    p(editClock, 25.2, 25.5, 0, 0.35) +
    p(editClock, 25.8, 26.2, 0, 0.35);
  const gap = 12 + 8 * push;
  const gutter = 25 + 40 * push;
  const smallW = (width - gutter * 2 - gap * 5) / 6;
  const largeW = (width - gutter * 2 - gap * 3) / 4;
  const gridY = 95 + push * 465;
  const titleColor = "#777E6C";
  const q = "yellow jacket portraits".slice(
    0,
    Math.floor(p(t, 10.4, 11.6) * 22),
  );
  return (
    <svg
      width={width}
      height={height}
      viewBox={`0 0 ${width} ${height}`}
      style={{
        display: "block",
        background: palette.paper,
        fontFamily: "Manrope",
      }}
    >
      <rect width={width} height={38 + 38 * push} fill={palette.ink} />
      <image
        href={media("looksy-wordmark-dark.png")}
        x={20 + push * 20}
        y={8 + push * 8}
        width={90 + push * 50}
        height={24 + push * 24}
      />
      <text
        x={width - 410 + 120 * (1 - push)}
        y={27 + push * 20}
        fill="#D2D6C9"
        fontSize={12 + push * 7}
      >
        Library · Photo · Design · Timeline
      </text>
      <text
        x={gutter}
        y={66 + 41 * push}
        fill={titleColor}
        fontSize={12 + push * 7}
      >
        NEPAL / SHOOT LIBRARY
      </text>
      <rect
        x={gutter}
        y={70 + push * 360}
        width={width - gutter * 2}
        height={24 + push * 46}
        rx={7 + push * 3}
        fill="#E1E4D9"
        opacity={(1 - agent) * (1 - edit)}
      />
      <text
        x={gutter + 20}
        y={87 + push * 388}
        fontSize={13 + push * 13}
        fill={titleColor}
        opacity={(1 - agent) * (1 - edit)}
      >
        {t < 10.4
          ? "Import folder / Photos + footage"
          : mode === "manual"
            ? "Picks / selected photographs"
            : `⌕  ${q}${t < 11.7 ? "│" : ""}`}
      </text>
      {Array.from({ length: 32 }, (_, i) => {
        const fileIndex = i % files.length;
        const matchRank = [0, 4, 5].indexOf(fileIndex);
        const match = matchRank !== -1 && i < files.length;
        const imported =
          i < 6
            ? 1
            : interpolate(t, [1.8 + i * 0.09, 2.6 + i * 0.09], [0, 1], {
                ...clamp,
                easing: settle,
              });
        const startX = gutter + (i % 6) * (smallW + gap);
        const startY = gridY + Math.floor(i / 6) * (smallW * 0.9 + 26 + gap);
        const endX = gutter + (i % 4) * (largeW + gap);
        const endY = gridY + Math.floor(i / 4) * (largeW * 1.1 + 30 + gap);
        let x = startX + (endX - startX) * push;
        let y = startY + (endY - startY) * push;
        let w = smallW + (largeW - smallW) * push;
        let h = w * (0.9 + 0.2 * push) + 30;
        if (match) {
          const sx = 72 + matchRank * 316;
          x += (sx - x) * search;
          y += (600 - y) * search;
          w += (280 - w) * search;
          h += (415 - h) * search;
          if (matchRank === 0) {
            x += (70 - x) * edit;
            y += (590 - y) * edit;
            w += (470 - w) * edit;
            h += (705 - h) * edit;
          } else {
            y += edit * 950;
            x += edit * (matchRank * 110 - 240);
          }
        } else {
          x += search * (i % 2 === 0 ? -250 : 250);
          y += search * 160;
        }
        return (
          <PhotoTile
            key={i}
            file={files[fileIndex]}
            x={x + (1 - imported) * (width - x)}
            y={y + (1 - imported) * (height - y)}
            width={w}
            height={h}
            selected={match && search > 0.5}
            exposure={matchRank === 0 && match ? exposure : 0}
            opacity={imported * (match ? 1 : 1 - search)}
          />
        );
      })}
      <g opacity={p(t, 8, 8.6) * (1 - search)}>
        <rect
          x={gutter}
          y={height - 130}
          width={width - gutter * 2}
          height="70"
          rx="9"
          fill={palette.ink}
        />
        <text x={gutter + 25} y={height - 87} fill="white" fontSize="24">
          Review import · RAW + footage
        </text>
        <path
          d={`M${width - 140} ${height - 108}l13 13 24-26`}
          fill="none"
          stroke={palette.green}
          strokeWidth="4"
        />
      </g>
      <text
        x="72"
        y="1080"
        fill={titleColor}
        fontSize="25"
        opacity={search * (1 - edit)}
      >
        {mode === "manual"
          ? "Your selection / 3 picked"
          : "Review matches / 3 selected"}
      </text>
      <g
        style={{ display: mode === "manual" ? "none" : undefined }}
        opacity={agent * (1 - edit)}
        transform={`translate(0 ${(1 - agent) * 200})`}
      >
        <path
          d="M195 1130C195 1240 816 1090 816 1215S275 1450 275 1320"
          fill="none"
          stroke="#D0D6C4"
          strokeWidth="3"
        />
        <path
          d="M195 1130C195 1240 816 1090 816 1215S275 1450 275 1320"
          fill="none"
          stroke={palette.blue}
          strokeWidth="6"
          pathLength="1"
          strokeDasharray=".12 .88"
          strokeDashoffset={-((t * 0.5) % 1)}
        />
        <rect
          x="720"
          y="1170"
          width="250"
          height="170"
          rx="12"
          fill={palette.ink}
        />
        <rect
          x="747"
          y="1200"
          width="64"
          height="64"
          rx="6"
          fill={palette.blue}
        />
        {[0, 1, 2, 3].map((n) => (
          <g key={n} stroke={palette.green} strokeWidth="3">
            <path d={`M${757 + n * 13} 1191v9M${757 + n * 13} 1264v9`} />
            <path d={`M738 ${1210 + n * 13}h9M811 ${1210 + n * 13}h9`} />
          </g>
        ))}
        <text x="833" y="1225" fill="white" fontSize="21">
          Local
        </text>
        <text x="833" y="1253" fill="white" fontSize="21">
          model
        </text>
        <text x="746" y="1312" fill="#BBC7AC" fontSize="17">
          ON THIS COMPUTER
        </text>
        <rect x="72" y="1150" width="530" height="270" rx="12" fill="#E2E7D9" />
        <text
          x="100"
          y="1193"
          fill={palette.ink}
          fontSize="25"
          fontWeight="800"
        >
          AI Edit
        </text>
        <text x="100" y="1240" fill="#636E59" fontSize="22">
          {t < 18 ? "Connect your local model." : "Use a consistent look."}
        </text>
        <text x="100" y="1272" fill="#636E59" fontSize="22">
          {t < 18
            ? "Choose a model on this computer."
            : "Keep the exposure editable."}
        </text>
        <rect
          x="100"
          y="1310"
          width="300"
          height="59"
          rx="6"
          fill={palette.ink}
        />
        <text x="125" y="1348" fill="white" fontSize="21">
          {t < 18 ? "Local model connected" : "Create sample draft"}
        </text>
        <g opacity={p(t, 19.6, 20.3)}>
          <rect
            x="72"
            y="1500"
            width="898"
            height="150"
            rx="10"
            fill={palette.ink}
          />
          <circle cx="111" cy="1550" r="8" fill={palette.green} />
          <text x="138" y="1559" fill="white" fontSize="27">
            Review the sample. Set the direction.
          </text>
          <text x="101" y="1606" fill="#ADB7A3" fontSize="23">
            Apply to the selected batch after review.
          </text>
        </g>
      </g>
      <g opacity={edit} transform={`translate(${(1 - edit) * 500} 0)`}>
        <rect
          x="590"
          y="590"
          width="420"
          height="710"
          fill={palette.ink}
          rx="9"
        />
        <text x="625" y="645" fill="white" fontSize="28" fontWeight="800">
          {mode === "manual" ? "Refine the image" : "Adjust the draft"}
        </text>
        <text x="625" y="702" fill="#B9C3AE" fontSize="24">
          Exposure
        </text>
        <text x="908" y="702" fill={palette.green} fontSize="24">
          +{exposure.toFixed(2)}
        </text>
        <path d="M625 737H970" stroke="#59624F" strokeWidth="4" />
        <path
          d={`M625 737H${760 + (exposure / 0.35) * 68}`}
          stroke={palette.green}
          strokeWidth="4"
        />
        <circle
          cx={760 + (exposure / 0.35) * 68}
          cy="737"
          r="10"
          fill={palette.paper}
        />
        <text x="625" y="809" fill="#B9C3AE" fontSize="23">
          {mode === "manual" ? "Photo tools" : "Layers"}
        </text>
        {(mode === "manual"
          ? ["White balance", "Tone curve", "Detail + masks"]
          : ["Subject mask", "Color adjustment", "Original"]
        ).map((label, i) => (
          <g key={label}>
            <rect
              x="625"
              y={835 + i * 56}
              width="30"
              height="30"
              fill={[palette.blue, palette.green, "#5C6653"][i]}
            />
            <text x="673" y={858 + i * 56} fill="white" fontSize="22">
              {label}
            </text>
          </g>
        ))}
        <text x="625" y="1085" fill="#B9C3AE" fontSize="23">
          History
        </text>
        {[
          "Import original",
          mode === "manual" ? "White balance" : "Agent draft",
          "Exposure adjustment",
        ].map((label, i) => (
          <text
            key={label}
            x="625"
            y={1130 + i * 48}
            fill={i === (exposure > 0.01 ? 2 : 1) ? palette.green : "#D3DACB"}
            fontSize="21"
          >
            {i === (exposure > 0.01 ? 2 : 1) ? "● " : "○ "}
            {label}
          </text>
        ))}
        <text x="74" y="1400" fill={titleColor} fontSize="28">
          Review. Adjust. Go back.
        </text>
      </g>
    </svg>
  );
};

const Monitor = ({ mode }: { mode: Mode }) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = frame / fps;
  const push = p(t, 3.6, 7);
  const x = 110 * (1 - push);
  const y = 570 * (1 - push);
  const width = 860 + 220 * push;
  const height = 630 + 1290 * push;
  const bezel = 22 * (1 - push);
  return (
    <div
      style={{
        position: "absolute",
        left: x - bezel,
        top: y - bezel,
        width: width + bezel * 2,
        height: height + bezel * 2,
        background: palette.ink,
        borderRadius: 22 * (1 - push),
        padding: bezel,
        boxSizing: "border-box",
        boxShadow: `0 ${26 * (1 - push)}px 0 #3D4437`,
        transform: `perspective(2200px) rotateY(${(1 - push) * -7}deg) rotateX(${(1 - push) * 3}deg)`,
        overflow: "hidden",
      }}
    >
      <LibrarySurface width={width} height={height} push={push} mode={mode} />
    </div>
  );
};

const DeskCopy = ({ mode }: { mode: Mode }) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = frame / fps;
  const headline: React.CSSProperties = {
    position: "absolute",
    left: 80,
    top: 175,
    fontSize: 100,
    fontWeight: 800,
    letterSpacing: -6,
    lineHeight: 1.03,
  };
  const reveal = (start: number, end: number, exit: number) => ({
    opacity: p(t, start, start + 0.15) * (1 - p(t, exit, exit + 0.3)),
    clipPath: `inset(${p(t, start, end, 100, 0)}% 0 0 0)`,
    translate: `0px ${p(t, start, end, 65, 0)}px`,
  });
  return (
    <>
      <div style={{ ...headline, ...reveal(0, 0.7, 2.8) }}>
        Looksy<span style={{ color: palette.blue }}>.</span>
        <span
          style={{
            display: "block",
            fontSize: 40,
            fontWeight: 500,
            letterSpacing: -1,
            marginTop: 24,
          }}
        >
          Photo. Video. Design.
        </span>
      </div>
      <div style={{ ...headline, ...reveal(2.9, 3.5, 4.5) }}>
        One shoot.
        <br />A whole project<span style={{ color: palette.blue }}>.</span>
        <span
          style={{
            display: "block",
            fontWeight: 500,
            letterSpacing: -1,
            fontSize: 37,
            marginTop: 28,
          }}
        >
          Photo. Video. Design.
        </span>
      </div>
      <div style={{ ...headline, fontSize: 87, ...reveal(7.1, 7.8, 10.1) }}>
        Bring in
        <br />
        the whole shoot<span style={{ color: palette.blue }}>.</span>
        <span
          style={{
            display: "block",
            fontWeight: 500,
            letterSpacing: -1,
            fontSize: 39,
            marginTop: 27,
          }}
        >
          Batch import.
        </span>
      </div>
      <div style={{ ...headline, fontSize: 87, ...reveal(10.5, 11.2, 14.8) }}>
        {mode === "manual" ? "Choose your" : "Find the frame"}
        <br />
        {mode === "manual" ? "starting frames" : "you remember"}
        <span style={{ color: palette.blue }}>.</span>
        <span
          style={{
            display: "block",
            fontWeight: 500,
            letterSpacing: -1,
            fontSize: 39,
            marginTop: 27,
          }}
        >
          {mode === "manual" ? "Picks. Ratings. Filters." : "AI search."}
        </span>
      </div>
      {mode === "manual" && (
        <div style={{ ...headline, fontSize: 95, ...reveal(15.2, 15.9, 22) }}>
          Every adjustment.
          <br />
          Your call<span style={{ color: palette.blue }}>.</span>
          <span
            style={{
              display: "block",
              fontWeight: 500,
              letterSpacing: -1,
              fontSize: 39,
              marginTop: 27,
            }}
          >
            Exposure. Color. Detail.
          </span>
        </div>
      )}
      <div
        style={{
          ...headline,
          display: mode === "manual" ? "none" : undefined,
          fontSize: 91,
          ...reveal(15.2, 15.9, 18.7),
        }}
      >
        Your AI.
        <br />
        On your machine<span style={{ color: palette.blue }}>.</span>
        <span
          style={{
            display: "block",
            fontWeight: 500,
            letterSpacing: -1,
            fontSize: 39,
            marginTop: 27,
          }}
        >
          Connect a local model.
        </span>
      </div>
      <div
        style={{
          ...headline,
          display: mode === "manual" ? "none" : undefined,
          fontSize: 87,
          ...reveal(19.1, 19.8, 22.2),
        }}
      >
        Set the direction<span style={{ color: palette.blue }}>.</span>
        <span
          style={{
            display: "block",
            fontWeight: 500,
            letterSpacing: -1,
            fontSize: 39,
            marginTop: 27,
          }}
        >
          Review a sample. Apply to the batch.
        </span>
      </div>
      <div
        style={{
          ...headline,
          display: mode === "manual" ? "none" : undefined,
          fontSize: 95,
          ...reveal(22.6, 23.3, 28),
        }}
      >
        Still yours
        <br />
        to change<span style={{ color: palette.blue }}>.</span>
        <span
          style={{
            display: "block",
            fontWeight: 500,
            letterSpacing: -1,
            fontSize: 39,
            marginTop: 27,
          }}
        >
          Editable drafts. Layers. History.
        </span>
      </div>
    </>
  );
};

export const DeskOpening = ({ mode }: { mode: Mode }) => {
  const { fps } = useVideoConfig();
  return (
    <AbsoluteFill
      style={{
        background: palette.paper,
        color: palette.ink,
        fontFamily: "Manrope",
        overflow: "hidden",
      }}
    >
      {files.map((file) => (
        <CanvasImage
          key={file}
          src={media(`${file}.jpg`)}
          style={{ position: "absolute", width: 1, height: 1, opacity: 0 }}
        />
      ))}
      <Sequence
        name="Illustrated desk — storage — camera approach"
        durationInFrames={7.1 * fps}
      >
        <DeskIllustration />
      </Sequence>
      <Sequence
        name="Continuous monitor — library — editable tools"
        durationInFrames={(mode === "manual" ? 19.5 : 27) * fps}
      >
        <Monitor mode={mode} />
      </Sequence>
      <Sequence
        name="Problem — product — professional value"
        durationInFrames={(mode === "manual" ? 19.5 : 27) * fps}
      >
        <DeskCopy mode={mode} />
      </Sequence>
      <Sequence
        name="Files travel from storage into the monitor"
        durationInFrames={6.1 * fps}
      >
        <StorageFlow />
      </Sequence>
    </AbsoluteFill>
  );
};
