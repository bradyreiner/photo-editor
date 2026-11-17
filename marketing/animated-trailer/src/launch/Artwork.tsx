import { useId } from "react";
import { CanvasImage, staticFile } from "remotion";
import { A, asset, C, land, lerp, p } from "./design";
import { GrainPlate } from "../film/GrainPlate";

type Plane = "all" | "base" | "type" | "subject" | "graphics";
export const CampaignArtwork = ({
  t = 8,
  accent = C.brandBlue,
  selected = false,
  tracking = 0,
  plane = "all",
  paintOverride,
  sourceToDesign = 1,
}: {
  t?: number;
  accent?: string;
  selected?: boolean;
  tracking?: number;
  plane?: Plane;
  paintOverride?: number;
  sourceToDesign?: number;
}) => {
  const id = useId().replace(/:/g, "");
  const type = land(t, 0.5, 1.8),
    collage = land(t, 2.4, 3.4),
    paint = paintOverride ?? p(t, 4.4, 5.7),
    subject = land(t, 1.2, 2.25);
  const show = (name: Plane) => plane === "all" || plane === name;
  return (
    <>
      <CanvasImage
        src={staticFile("media/IMG_5106-developed-subject.png")}
        style={{ position: "absolute", width: 1, height: 1, opacity: 0 }}
      />
      <CanvasImage
        src={asset("IMG_5071")}
        style={{ position: "absolute", width: 1, height: 1, opacity: 0 }}
      />
      <svg
        width="100%"
        height="100%"
        viewBox={`0 0 1200 ${lerp(1800, 1500, sourceToDesign)}`}
        preserveAspectRatio="xMidYMid meet"
        style={{ position: "absolute", inset: 0 }}
      >
        <defs>
          <clipPath id={`${id}type`}>
            <rect x={0} y={530} width={1200 * type} height={420} />
          </clipPath>
          <clipPath id={`${id}photo`}>
            <rect width={1200} height={lerp(1800, 1410, sourceToDesign)} />
          </clipPath>
        </defs>
        {show("base") && (
          <rect width={1200} height={1800} fill={C.brandPaper} />
        )}
        <g clipPath={`url(#${id}photo)`}>
          {show("base") && (
            <>
              <image
                href={asset("IMG_5106-developed")}
                y={-150 * sourceToDesign}
                width={1200}
                height={1800}
              />
              <path
                d="M0 0H1200V160H0Z"
                fill="#050505"
                opacity={0.15 * sourceToDesign}
              />
              <g
                fill={C.brandPaper}
                fontFamily="Menlo,monospace"
                fontSize={28}
                letterSpacing={2}
                opacity={sourceToDesign}
              >
                <text x={65} y={85}>
                  NEPAL / A FIELD STUDY
                </text>
                <text x={1135} y={85} textAnchor="end">
                  2026
                </text>
              </g>
            </>
          )}
          {show("type") && (
            <g clipPath={`url(#${id}type)`}>
              <text
                x={48}
                y={908}
                fill={C.brandPaper}
                fontFamily="Manrope"
                fontWeight={800}
                fontSize={310}
                letterSpacing={-21 + tracking}
              >
                NEPAL.
              </text>
            </g>
          )}
          {show("graphics") && (
            <path
              d="M65 1138C230 1010 400 1265 725 1100"
              fill="none"
              stroke={accent}
              strokeWidth={25}
              strokeLinecap="round"
              pathLength={1}
              strokeDasharray={1}
              strokeDashoffset={1 - paint}
            />
          )}
          {show("subject") && (
            <image
              href={staticFile("media/IMG_5106-developed-subject.png")}
              y={-150 * sourceToDesign}
              width={1200}
              height={1800}
              opacity={subject}
            />
          )}
          {show("graphics") && (
            <g
              transform={`translate(${(1 - collage) * 390} 0)`}
              opacity={collage}
            >
              <rect
                x={850}
                y={1050}
                width={285}
                height={255}
                fill={C.brandPaper}
              />
              <image
                href={asset("IMG_5071")}
                x={863}
                y={1063}
                width={259}
                height={185}
                preserveAspectRatio="xMidYMid slice"
              />
              <text
                x={870}
                y={1285}
                fill={C.brandInk}
                fontFamily="Menlo,monospace"
                fontSize={20}
              >
                FIRST LIGHT / 06:12
              </text>
            </g>
          )}
          {show("type") && (
            <g
              fill={C.brandPaper}
              fontFamily="Manrope"
              fontWeight={800}
              fontSize={59}
              letterSpacing={-2}
              opacity={type}
            >
              <text x={65} y={1250}>
                A DIFFERENT
              </text>
              <text x={65} y={1315}>
                PERSPECTIVE.
              </text>
            </g>
          )}
        </g>
        {show("base") && (
          <g
            fontFamily="Menlo,monospace"
            fill={C.brandInk}
            fontSize={25}
            letterSpacing={1}
            opacity={land(sourceToDesign, 0.75, 1)}
            transform={`translate(0 ${390 * (1 - sourceToDesign)})`}
          >
            <text x={65} y={1466}>
              PHOTOGRAPHS / MOVING IMAGES
            </text>
            <text x={1135} y={1466} textAnchor="end">
              01 / 05
            </text>
          </g>
        )}
        {selected && (
          <g fill={C.brandPaper} stroke={C.brandPaper} strokeWidth={2}>
            <rect x={45} y={622} width={1110} height={295} fill="none" />
            {[
              [45, 622],
              [600, 622],
              [1155, 622],
              [45, 917],
              [600, 917],
              [1155, 917],
            ].map(([x, y], i) => (
              <rect key={i} x={x - 7} y={y - 7} width={14} height={14} />
            ))}
          </g>
        )}
      </svg>
    </>
  );
};

// The effect preview remains on the same artwork as it becomes a video clip.
export const CampaignPreview = ({
  diffusion = 24,
  grain = 0.075,
}: {
  diffusion?: number;
  grain?: number;
}) => (
  <>
    <CampaignArtwork t={8} />
    <div
      style={{
        ...A,
        inset: 0,
        filter: "blur(13px)",
        opacity: diffusion / 100,
        mixBlendMode: "screen",
      }}
    >
      <CampaignArtwork t={8} />
    </div>
    <GrainPlate amount={grain} />
  </>
);

export const TextMaskArtwork = ({ t }: { t: number }) => {
  const id = useId().replace(/:/g, "");
  const tracking = -26 + p(t, 2, 2.18, 0, 16);
  return (
    <svg
      width="100%"
      height="100%"
      viewBox="0 0 1080 1920"
      preserveAspectRatio="xMidYMid slice"
    >
      <defs>
        <mask id={id}>
          <rect width={1080} height={1920} fill="black" />
          <g
            fill="white"
            fontFamily="Manrope"
            fontWeight={800}
            fontSize={360}
            letterSpacing={tracking}
          >
            <text x={70} y={800}>
              LOOK
            </text>
            <text x={70} y={1110}>
              UP.
            </text>
          </g>
        </mask>
      </defs>
      <rect width={1080} height={1920} fill={C.brandInk} />
      <image
        href={asset("IMG_4870")}
        x={-p(t, 0, 4, 0, 180)}
        y={200}
        width={1400}
        height={1500}
        preserveAspectRatio="xMidYMid slice"
        mask={`url(#${id})`}
      />
      <g
        fill="none"
        stroke={C.brandPaper}
        strokeWidth={1.5}
        opacity={0.2}
        fontFamily="Manrope"
        fontWeight={800}
        fontSize={360}
        letterSpacing={tracking}
      >
        <text x={70} y={800}>
          LOOK
        </text>
        <text x={70} y={1110}>
          UP.
        </text>
      </g>
      <g
        fill={C.brandPaper}
        fontFamily="Menlo,monospace"
        fontSize={28}
        letterSpacing={2}
      >
        <text x={80} y={180}>
          IMAGE × TYPE / LIVE MASK
        </text>
      </g>
    </svg>
  );
};
