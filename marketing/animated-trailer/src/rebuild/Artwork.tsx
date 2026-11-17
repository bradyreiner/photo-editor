import { useId } from "react";
import { staticFile } from "remotion";
import { font } from "./Type";
import { A, Image, PaperGrain, lime, paper, blue, out } from "./Kit";
export const ExpeditionPoster = ({
  build = 1,
  foreground = 1,
  tracking = 0,
  plane = "all",
  mapTypeface = font.display,
}: {
  build?: number;
  foreground?: number;
  tracking?: number;
  plane?: "all" | "base" | "type" | "subject" | "marks";
  mapTypeface?: string;
}) => {
  const id = useId().replace(/:/g, "");
  const show = (s: string) => plane === "all" || plane === s;
  return (
    <div
      style={{
        position: "relative",
        width: "100%",
        height: "100%",
        overflow: "hidden",
        background:
          plane === "all" || plane === "base" ? "#1b2440" : "transparent",
      }}
    >
      <svg
        viewBox="0 0 960 1200"
        width="100%"
        height="100%"
        preserveAspectRatio="xMidYMid slice"
        style={{ ...A, inset: 0 }}
      >
        <defs>
          <clipPath id={id}>
            <rect
              x={120}
              y={-80 + 160 * (1 - build)}
              width={720}
              height={1180}
              rx={360}
            />
          </clipPath>
        </defs>
        {show("base") && (
          <>
            <rect width={960} height={1200} fill="#1b2440" />
            <g opacity={0.6} fill="none" stroke={blue} strokeWidth={1.5}>
              {Array.from({ length: 12 }, (_, i) => (
                <path
                  key={i}
                  d={`M${-270 + i * 25} 1200C${80 + i * 45} ${760 - i * 25} ${500 + i * 15} ${1010 - i * 35} ${1100 + i * 10} ${700 - i * 32}`}
                />
              ))}
            </g>
            <g clipPath={`url(#${id})`}>
              <image
                href={staticFile("media/studio/color/4-8.png")}
                x={0}
                y={-120}
                width={960}
                height={1440}
              />
              <rect
                x={120}
                y={-80}
                width={720}
                height={1180}
                fill="#0b152b"
                opacity={0.2}
              />
            </g>
          </>
        )}
        {show("type") && (
          <g fill={paper} fontFamily={font.display} letterSpacing={-4}>
            <text x={52 - 900 * (1 - build)} y={442} fontSize={380}>
              OFF
            </text>
            <text
              x={55 + 900 * (1 - build)}
              y={666}
              fontSize={191}
              fontFamily={mapTypeface}
              letterSpacing={-4 + tracking}
              textLength={850 + tracking * 6}
              lengthAdjust="spacing"
            >
              THE MAP
            </text>
          </g>
        )}
        {show("subject") && (
          <image
            href={staticFile("media/studio/subject.png")}
            x={0}
            y={-120}
            width={960}
            height={1440}
            opacity={foreground * build}
          />
        )}
        {show("marks") && (
          <g opacity={build}>
            <g
              fill={lime}
              fontFamily={font.mono}
              fontSize={24}
              letterSpacing={2}
            >
              <text x={55} y={62}>
                NEPAL / A FIELD JOURNAL
              </text>
              <text x={55} y={1150}>
                PHOTOGRAPHS + MOVING PICTURES
              </text>
            </g>
            <circle cx={806} cy={916} r={82} fill={lime} />
            <g
              fill="#1b2440"
              fontFamily={font.mono}
              fontSize={22}
              textAnchor="middle"
            >
              <text x={806} y={911}>
                FIELD
              </text>
              <text x={806} y={944}>
                001
              </text>
            </g>
            <text
              x={55}
              y={1030}
              fontFamily={font.serif}
              fontStyle="italic"
              fontSize={105}
              fill={paper}
            >
              A little further.
            </text>
            <path
              d="M64 1062C272 1041 420 1048 614 1062"
              stroke={lime}
              fill="none"
              strokeWidth={6}
              strokeLinecap="round"
              pathLength={1}
              strokeDasharray={1}
              strokeDashoffset={1 - build}
            />
          </g>
        )}
      </svg>
      {(plane === "all" || plane === "base") && <PaperGrain opacity={0.2} />}
    </div>
  );
};
export const NightThumbnail = ({
  build = 1,
  children,
}: {
  build?: number;
  children?: React.ReactNode;
}) => (
  <div
    style={{
      position: "relative",
      width: "100%",
      height: "100%",
      overflow: "hidden",
      background: "#101012",
    }}
  >
    <Image src="studio/night-thumb.jpg" />
    {children}
    <div
      style={{
        ...A,
        inset: 0,
        background:
          "linear-gradient(90deg,#0b0d12df 15%,#0b0d1250 65%,transparent)",
      }}
    />
    <svg
      viewBox="0 0 1280 720"
      width="100%"
      height="100%"
      style={{ ...A, inset: 0 }}
    >
      <g fill={paper} fontFamily={font.display}>
        <text x={55 - 700 * (1 - build)} y={290} fontSize={250}>
          AFTER
        </text>
        <text x={55 + 700 * (1 - build)} y={535} fontSize={270}>
          DARK
        </text>
      </g>
      <text
        x={65}
        y={640}
        fontFamily={font.serif}
        fontStyle="italic"
        fontSize={83}
        fill="#ffbd6b"
      >
        in Kathmandu.
      </text>
      <path
        d="M900 100H1200V620H1080"
        stroke="#ffbd6b"
        strokeWidth={3}
        fill="none"
      />
      <g fill={paper} fontFamily={font.mono} fontSize={22}>
        <text x={65} y={55}>
          A NEPAL FILM
        </text>
        <text x={1180} y={585} textAnchor="end">
          04 / NIGHT
        </text>
      </g>
    </svg>
    <PaperGrain opacity={0.2} />
  </div>
);
export const MountainThumbnail = () => (
  <div
    style={{
      position: "relative",
      width: "100%",
      height: "100%",
      overflow: "hidden",
    }}
  >
    <Image src="IMG_5071.jpg" />
    <div
      style={{
        ...A,
        inset: 0,
        background: "linear-gradient(90deg,#152638a6,transparent 85%)",
      }}
    />
    <svg
      viewBox="0 0 1280 720"
      width="100%"
      height="100%"
      style={{ ...A, inset: 0 }}
    >
      <text
        x={50}
        y={300}
        fontFamily={font.serif}
        fontStyle="italic"
        fontSize={260}
        fill={paper}
      >
        Above
      </text>
      <text x={65} y={493} fontFamily={font.display} fontSize={175} fill={lime}>
        THE NOISE
      </text>
      <g fontFamily={font.mono} fontSize={24} fill={paper}>
        <text x={65} y={65}>
          THE HIMALAYAS / FIRST LIGHT
        </text>
        <text x={65} y={650}>
          A FIELD FILM BY BRADY REINER
        </text>
      </g>
      <circle
        cx={1125}
        cy={595}
        r={55}
        fill="none"
        stroke={paper}
        strokeWidth={2}
      />
      <path
        d="M1105 615L1145 575M1105 575H1145V615"
        fill="none"
        stroke={paper}
        strokeWidth={3}
      />
    </svg>
    <PaperGrain opacity={0.2} />
  </div>
);
export const WeddingCover = ({
  imageSrc = "campaign/wedding-dawn.jpg",
}: {
  imageSrc?: string;
}) => (
  <div
    style={{
      position: "relative",
      width: "100%",
      height: "100%",
      background: "#eee5d3",
      color: "#343125",
      overflow: "hidden",
    }}
  >
    <svg
      viewBox="0 0 960 1200"
      width="100%"
      height="100%"
      style={{ ...A, inset: 0 }}
    >
      <text
        x={60}
        y={72}
        fontFamily={font.mono}
        fontSize={24}
        letterSpacing={3}
      >
        THE WEDDING COLLECTION
      </text>
      <g fontFamily={font.serif} fontSize={156}>
        <text x={55} y={250}>
          Every little
        </text>
        <text x={55} y={405} fontStyle="italic">
          forever.
        </text>
      </g>
      <image
        href={staticFile("media/" + imageSrc)}
        x={60}
        y={475}
        width={840}
        height={570}
        preserveAspectRatio="xMidYMid slice"
      />
      <g fontFamily={font.mono} fontSize={23}>
        <text x={60} y={1120}>
          ONE DAY. A WHOLE STORY.
        </text>
        <text x={900} y={1120} textAnchor="end">
          01—24
        </text>
      </g>
    </svg>
    <PaperGrain opacity={0.32} />
  </div>
);
export const DesignBoard = () => (
  <div
    style={{
      width: "100%",
      height: "100%",
      background: "#0b0d12",
      color: paper,
      fontFamily: font.sans,
      padding: 90,
      boxSizing: "border-box",
    }}
  >
    <div
      style={{
        fontSize: 28,
        fontFamily: font.mono,
        color: lime,
        letterSpacing: 3,
      }}
    >
      LOOKSY / NEW VISUAL DIRECTION
    </div>
    <div style={{ fontSize: 100, letterSpacing: -5, margin: "26px 0 70px" }}>
      Make something worth making.
    </div>
    <div style={{ display: "flex", gap: 50 }}>
      <div style={{ width: 670, height: 837.5 }}>
        <ExpeditionPoster />
      </div>
      <div style={{ width: 670, height: 837.5 }}>
        <WeddingCover />
      </div>
      <div style={{ width: 1000 }}>
        <div style={{ height: 562.5 }}>
          <NightThumbnail />
        </div>
        <div style={{ height: 562.5, marginTop: 50 }}>
          <MountainThumbnail />
        </div>
      </div>
    </div>
    <div
      style={{
        fontFamily: font.mono,
        fontSize: 27,
        color: "#a1a5ae",
        marginTop: 45,
      }}
    >
      DISPLAY / ANTON EDITORIAL / INSTRUMENT SERIF UI / SPACE GROTESK DETAILS /
      SPACE MONO
    </div>
  </div>
);

export const BuildPoster = ({ t }: { t: number }) => (
  <ExpeditionPoster build={out(t, 0, 0.8)} />
);
