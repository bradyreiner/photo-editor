import { useId } from "react";
import { CanvasImage, Sequence, useVideoConfig } from "remotion";
import { Video } from "@remotion/media";
import { subjectPath } from "../StyleFrames";
import { asset, mix, T, wave } from "./system";

export const PosterArtwork = ({
  t = 45,
  accent = T.brandRed,
  mini = false,
}: {
  t?: number;
  accent?: string;
  mini?: boolean;
}) => {
  const id = useId().replace(/:/g, "");
  const type = mix(t, 35.5, 38.5),
    paint = mix(t, 40, 42);
  const d = Array.from({ length: 50 }, (_, i) => {
    const u = i / 49;
    return `${i ? "L" : "M"}${80 + u * 1040} ${1425 + wave(u, 0.27) * 95}`;
  }).join(" ");
  return (
    <svg
      width="100%"
      height="100%"
      viewBox="0 0 1200 1800"
      preserveAspectRatio="xMidYMid slice"
      style={{ position: "absolute", inset: 0 }}
    >
      <defs>
        <clipPath id={id}>
          <path d={subjectPath} />
        </clipPath>
      </defs>
      <rect width={1200} height={1800} fill="#000" opacity={0.13 * type} />
      <g
        opacity={type}
        fill="#f3f3f3"
        fontFamily="Manrope"
        fontWeight={800}
        letterSpacing={-10 + mix(t, 44.2, 45.3, 0, 6)}
      >
        <text x={65} y={460} fontSize={230}>
          THE
        </text>
        <text x={65} y={660} fontSize={230}>
          HIGH
        </text>
        <text x={65} y={860} fontSize={230}>
          ROUTE.
        </text>
      </g>
      <image
        href={asset("IMG_5106.jpg")}
        width={1200}
        height={1800}
        clipPath={`url(#${id})`}
        opacity={type}
      />
      <g
        opacity={type}
        fill="#f3f3f3"
        fontFamily="Menlo,monospace"
        fontSize={24}
        letterSpacing={3}
      >
        <text x={70} y={105}>
          NEPAL / FIELD NOTES
        </text>
        <text x={1090} y={105} textAnchor="end">
          01
        </text>
      </g>
      <g
        opacity={type}
        transform={`translate(${mix(t, 38.2, 39.4, 360, 0)} 0)`}
      >
        <rect x={750} y={1270} width={365} height={255} fill="#f3f3f3" />
        <image
          href={asset("IMG_5071.jpg")}
          x={762}
          y={1282}
          width={341}
          height={194}
          preserveAspectRatio="xMidYMid slice"
        />
        <text
          x={774}
          y={1508}
          fill="#101010"
          fontFamily="Menlo,monospace"
          fontSize={18}
        >
          FIRST LIGHT / NEPAL
        </text>
      </g>
      <path
        d={d}
        fill="none"
        stroke={accent}
        strokeWidth={22}
        strokeLinecap="round"
        pathLength={1}
        strokeDasharray={1}
        strokeDashoffset={1 - paint}
      />
      <g opacity={type} fill="#f3f3f3" fontFamily="Manrope" fontWeight={800}>
        <text x={70} y={1630} fontSize={72} letterSpacing={-2}>
          A LITTLE FURTHER.
        </text>
        {!mini && (
          <text
            x={72}
            y={1686}
            fontSize={25}
            fontWeight={500}
            letterSpacing={1}
          >
            PHOTOGRAPHS / MOVING IMAGES / NEPAL
          </text>
        )}
      </g>
    </svg>
  );
};

export const BoardArtwork = ({
  index,
  accent = T.brandRed,
  moving = false,
}: {
  index: number;
  accent?: string;
  moving?: boolean;
}) => {
  const { fps } = useVideoConfig();
  if (index === 0)
    return (
      <>
        <CanvasImage
          src={asset("IMG_5106.jpg")}
          style={{ width: "100%", height: "100%", objectFit: "cover" }}
        />
        <PosterArtwork t={48} accent={accent} mini />
      </>
    );
  const names = ["IMG_4870", "IMG_4969", "IMG_5071", "IMG_4856"];
  const lines = [
    ["IN", "BETWEEN."],
    ["TAKE", "IT IN."],
    ["FIRST", "LIGHT."],
    ["KEEP", "GOING."],
  ];
  return (
    <div
      style={{
        position: "absolute",
        inset: 0,
        overflow: "hidden",
        background: index % 2 ? "#ecebe6" : T.ink900,
        color: index % 2 ? T.ink950 : T.brandPaper,
      }}
    >
      <CanvasImage
        src={asset(`${names[index - 1]}.jpg`)}
        style={{
          position: "absolute",
          left: "8%",
          top: "26%",
          width: "84%",
          height: "55%",
          objectFit: "cover",
        }}
      />
      {moving && index === 2 && (
        <div
          style={{
            position: "absolute",
            left: "8%",
            top: "26%",
            width: "84%",
            height: "55%",
            overflow: "hidden",
          }}
        >
          <Sequence from={73 * fps} durationInFrames={15 * fps} layout="none">
            <Video
              src={asset("IMG_1268-2-7.mp4")}
              muted
              loop
              objectFit="cover"
              style={{ width: "100%", height: "100%" }}
            />
          </Sequence>
        </div>
      )}
      <svg
        width="100%"
        height="100%"
        viewBox="0 0 800 1000"
        preserveAspectRatio="none"
        style={{ position: "absolute", inset: 0 }}
      >
        <g
          fill="currentColor"
          fontFamily="Manrope"
          fontWeight={800}
          fontSize={100}
          letterSpacing={-5}
        >
          <text x={56} y={150}>
            {lines[index - 1][0]}
          </text>
          <text x={56} y={248}>
            {lines[index - 1][1]}
          </text>
        </g>
        <path
          d="M40 840 C180 800 330 880 470 840 S680 800 790 850"
          fill="none"
          stroke={accent}
          strokeWidth={12}
        />
        <text
          x={60}
          y={935}
          fontFamily="Menlo,monospace"
          fill="currentColor"
          fontSize={18}
        >
          NEPAL / FIELD NOTES
        </text>
        <text
          x={740}
          y={935}
          textAnchor="end"
          fontFamily="Menlo,monospace"
          fill="currentColor"
          fontSize={18}
        >
          0{index + 1}
        </text>
      </svg>
    </div>
  );
};
