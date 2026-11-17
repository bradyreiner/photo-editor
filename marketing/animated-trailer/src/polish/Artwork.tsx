import { useId } from "react";
import { staticFile } from "remotion";
import { A, C, land, lerp, p } from "../launch/design";
import { GrainPlate } from "../film/GrainPlate";
import type { CampaignArtwork as OriginalArtwork } from "../launch/Artwork";
type Props = Parameters<typeof OriginalArtwork>[0];
export const CampaignArtwork = ({
  t = 8,
  accent = C.brandBlue,
  selected = false,
  tracking = 0,
  plane = "all",
  paintOverride,
  sourceToDesign = 1,
}: Props) => {
  const id = useId().replace(/:/g, ""),
    type = land(t, 0.5, 1.8),
    paint = paintOverride ?? p(t, 4.4, 5.7),
    show = (n: string) => plane === "all" || plane === n;
  return (
    <svg
      width="100%"
      height="100%"
      viewBox={`0 0 1200 ${lerp(1800, 1500, sourceToDesign)}`}
      preserveAspectRatio="xMidYMid meet"
      style={{ ...A, inset: 0 }}
    >
      <defs>
        <clipPath id={id}>
          <rect width="1200" height={lerp(1800, 1410, sourceToDesign)} />
        </clipPath>
        <clipPath id={`${id}type`}>
          <rect x="0" y="480" width={1200 * type} height="460" />
        </clipPath>
      </defs>
      {show("base") ? (
        <rect width="1200" height="1800" fill={C.brandPaper} />
      ) : null}
      <g clipPath={`url(#${id})`}>
        {show("base") ? (
          <>
            <image
              href={staticFile("media/range/portrait-1200.jpg")}
              width="1200"
              height="1800"
              y={-150 * sourceToDesign}
            />
            <g
              fill="white"
              fontFamily="Menlo,monospace"
              fontSize="27"
              letterSpacing="2"
              opacity={sourceToDesign}
            >
              <text x="65" y="85">
                FORM / EDITORIAL STUDY
              </text>
              <text x="1135" y="85" textAnchor="end">
                01
              </text>
            </g>
          </>
        ) : null}
        {show("type") ? (
          <g clipPath={`url(#${id}type)`}>
            <text
              x="45"
              y="850"
              fill="white"
              fontFamily="Manrope"
              fontWeight="800"
              fontSize="300"
              letterSpacing={-22 + tracking}
            >
              FORM.
            </text>
          </g>
        ) : null}
        {show("subject") ? (
          <image
            href={staticFile("media/range/portrait-subject.png")}
            width="1200"
            height="1800"
            y={-150 * sourceToDesign}
            opacity={land(t, 1.2, 2.25)}
          />
        ) : null}
        {show("graphics") ? (
          <>
            <path
              d="M65 1160C260 1020 450 1280 790 1110"
              fill="none"
              stroke={accent}
              strokeWidth="25"
              strokeLinecap="round"
              pathLength="1"
              strokeDasharray="1"
              strokeDashoffset={1 - paint}
            />
            <g
              fill="white"
              fontFamily="Manrope"
              fontWeight="700"
              fontSize="48"
              opacity={land(t, 2.4, 3.4)}
            >
              <text x="65" y="1280">
                A STUDY IN
              </text>
              <text x="65" y="1338">
                SELF EXPRESSION.
              </text>
            </g>
          </>
        ) : null}
      </g>
      {show("base") ? (
        <g
          fill={C.brandInk}
          fontFamily="Menlo,monospace"
          fontSize="25"
          opacity={sourceToDesign}
        >
          <text x="65" y="1464">
            PHOTOGRAPHY / ART DIRECTION
          </text>
          <text x="1135" y="1464" textAnchor="end">
            01 / 04
          </text>
        </g>
      ) : null}
      {selected ? (
        <g fill="white" stroke="white" strokeWidth="2">
          <rect x="40" y="600" width="1115" height="275" fill="none" />
          {[
            [40, 600],
            [600, 600],
            [1155, 600],
            [40, 875],
            [600, 875],
            [1155, 875],
          ].map(([x, y], i) => (
            <rect key={i} x={x - 7} y={y - 7} width="14" height="14" />
          ))}
        </g>
      ) : null}
    </svg>
  );
};
export const CampaignPreview = ({
  diffusion = 24,
  grain = 0.075,
}: {
  diffusion?: number;
  grain?: number;
}) => (
  <>
    <CampaignArtwork />
    <div
      style={{
        ...A,
        inset: 0,
        filter: "blur(13px)",
        opacity: diffusion / 100,
        mixBlendMode: "screen",
      }}
    >
      <CampaignArtwork />
    </div>
    <GrainPlate amount={grain} />
  </>
);
export const TextMaskArtwork = ({ t }: { t: number }) => {
  const id = useId().replace(/:/g, ""),
    spacing = -26 + p(t, 2, 2.18, 0, 16);
  return (
    <svg
      width="100%"
      height="100%"
      viewBox="0 0 1080 1920"
      preserveAspectRatio="xMidYMid slice"
    >
      <defs>
        <mask id={id}>
          <rect width="1080" height="1920" fill="black" />
          <g
            fill="white"
            fontFamily="Manrope"
            fontWeight="800"
            fontSize="280"
            letterSpacing={spacing}
          >
            <text x="70" y="800">
              GOOD
            </text>
            <text x="70" y="1050">
              TASTE.
            </text>
          </g>
        </mask>
      </defs>
      <rect width="1080" height="1920" fill={C.brandInk} />
      <image
        href={staticFile("media/range/food.jpg")}
        x={-p(t, 0, 4, 0, 150)}
        y="180"
        width="1450"
        height="1500"
        preserveAspectRatio="xMidYMid slice"
        mask={`url(#${id})`}
      />
      <g
        fill="none"
        stroke="white"
        strokeWidth="1"
        opacity=".2"
        fontFamily="Manrope"
        fontWeight="800"
        fontSize="280"
        letterSpacing={spacing}
      >
        <text x="70" y="800">
          GOOD
        </text>
        <text x="70" y="1050">
          TASTE.
        </text>
      </g>
      <text
        x="80"
        y="180"
        fill="white"
        fontFamily="Menlo,monospace"
        fontSize="28"
        letterSpacing="2"
      >
        IMAGE × TYPE / LIVE MASK
      </text>
    </svg>
  );
};
