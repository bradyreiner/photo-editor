import React, { useId } from "react";
import { AbsoluteFill, CanvasImage, staticFile } from "remotion";
import { loadFont } from "@remotion/fonts";

loadFont({
  family: "Manrope",
  url: staticFile("fonts/manrope-latin-500-normal.woff2"),
  weight: "500",
});
loadFont({
  family: "Manrope",
  url: staticFile("fonts/manrope-latin-800-normal.woff2"),
  weight: "800",
});

export const palette = {
  ink: "#101112",
  paper: "#F2F1ED",
  red: "#FF4B36",
  green: "#9CEC63",
  blue: "#4465FF",
};
const asset = (name: string) => staticFile(`media/${name}`);
const abs: React.CSSProperties = { position: "absolute" };

export const Image = ({
  name,
  style,
}: {
  name: string;
  style?: React.CSSProperties;
}) => (
  <CanvasImage
    src={asset(name)}
    style={{ width: "100%", height: "100%", objectFit: "cover", ...style }}
  />
);

export const Brand = ({ light = false }: { light?: boolean }) => (
  <div style={{ ...abs, left: 84, top: 96, width: 160, height: 53 }}>
    <Image
      name={`looksy-wordmark-${light ? "dark" : "light"}.png`}
      style={{ objectFit: "contain" }}
    />
  </div>
);

export const Index = ({
  number,
  light = false,
}: {
  number: string;
  light?: boolean;
}) => (
  <div
    style={{
      ...abs,
      right: 88,
      top: 104,
      display: "flex",
      alignItems: "center",
      gap: 17,
      fontSize: 24,
      color: light ? "#B3B4B2" : "#73736E",
    }}
  >
    <span
      style={{
        width: 7,
        height: 7,
        borderRadius: "50%",
        background: palette.green,
      }}
    />
    PHOTO / VIDEO / DESIGN{" "}
    <span style={{ marginLeft: 12, opacity: 0.5 }}>{number}</span>
  </div>
);

const Scene = ({
  children,
  dark = false,
}: {
  children: React.ReactNode;
  dark?: boolean;
}) => (
  <AbsoluteFill
    style={{
      background: dark ? palette.ink : palette.paper,
      color: dark ? palette.paper : palette.ink,
      fontFamily: "Manrope",
      fontWeight: 500,
      overflow: "hidden",
    }}
  >
    {children}
  </AbsoluteFill>
);

export const LightContours = ({
  style,
  color = palette.blue,
}: {
  style?: React.CSSProperties;
  color?: string;
}) => (
  <svg
    viewBox="0 0 1080 1500"
    fill="none"
    style={{ ...abs, inset: 0, width: 1080, height: 1500, ...style }}
  >
    {[0, 1, 2, 3, 4, 5, 6, 7].map((n) => (
      <path
        key={n}
        d={`M ${-230 + n * 20} ${110 + n * 29} C ${660 - n * 60} ${-200 + n * 42}, ${1190 - n * 16} ${500 + n * 28}, ${830 - n * 28} ${740 + n * 23} S ${-210 + n * 36} ${1010 - n * 13}, ${320 + n * 70} ${1560}`}
        stroke={color}
        strokeWidth={n % 3 === 0 ? 3 : 1.4}
        opacity={0.2 + n * 0.035}
      />
    ))}
  </svg>
);

export const Curve = () => (
  <div
    style={{
      width: 440,
      height: 448,
      background: palette.ink,
      color: palette.paper,
      padding: "31px 34px",
      boxSizing: "border-box",
      boxShadow: "0 25px 55px #0C123026",
    }}
  >
    <div
      style={{ display: "flex", justifyContent: "space-between", fontSize: 26 }}
    >
      <span>Tone curve</span>
      <span style={{ color: palette.green }}>RGB ↗</span>
    </div>
    <svg
      width="372"
      height="298"
      viewBox="0 0 372 298"
      style={{ marginTop: 20 }}
    >
      <g stroke="#FFFFFF1C" strokeWidth="1">
        {[0, 1, 2, 3, 4].map((i) => (
          <React.Fragment key={i}>
            <path d={`M${i * 92} 0V282`} />
            <path d={`M0 ${i * 70}H368`} />
          </React.Fragment>
        ))}
      </g>
      <path d="M0 282L368 0" stroke="#FFFFFF3D" strokeDasharray="5 7" />
      <path
        d="M0 280C90 275 113 219 161 166S281 73 368 3"
        stroke={palette.red}
        strokeWidth="3"
        fill="none"
      />
      <path
        d="M0 280C100 282 119 208 165 156S280 57 368 3"
        stroke={palette.green}
        strokeWidth="3"
        fill="none"
      />
      <path
        d="M0 280C75 276 105 207 165 156S270 48 368 3"
        stroke="#E9EDFF"
        strokeWidth="5"
        fill="none"
      />
      <circle
        cx="165"
        cy="156"
        r="11"
        fill={palette.ink}
        stroke="white"
        strokeWidth="4"
      />
      <circle cx="165" cy="156" r="29" fill="white" opacity=".08" />
    </svg>
    <div
      style={{
        fontSize: 19,
        color: "#8D918B",
        display: "flex",
        justifyContent: "space-between",
      }}
    >
      <span>SHADOWS</span>
      <span>HIGHLIGHTS</span>
    </div>
  </div>
);

export const ColorFrame = () => (
  <Scene>
    <LightContours
      style={{ top: 430, left: 250, transform: "rotate(-22deg)" }}
    />
    <Brand />
    <Index number="01" />
    <div
      style={{
        ...abs,
        left: 80,
        top: 232,
        fontSize: 113,
        lineHeight: 1.02,
        letterSpacing: -7,
        fontWeight: 800,
      }}
    >
      Color with
      <br />
      precision<span style={{ color: palette.blue }}>.</span>
    </div>
    <div
      style={{ ...abs, left: 88, top: 506, fontSize: 44, letterSpacing: -0.6 }}
    >
      RAW editing. At native resolution.
    </div>
    <div
      style={{
        ...abs,
        left: -40,
        top: 690,
        width: 1160,
        height: 770,
        transform: "perspective(2000px) rotateY(-8deg) rotateZ(-7deg)",
        boxShadow: "24px 38px 0 #4465FF",
      }}
    >
      <Image name="IMG_5071.jpg" />
      <div
        style={{
          ...abs,
          left: 34,
          top: 30,
          background: palette.paper,
          padding: "13px 20px",
          fontSize: 24,
        }}
      >
        IMG_5071.CR2 <span style={{ opacity: 0.5 }}> / 6000 × 4000</span>
      </div>
      <div
        style={{
          ...abs,
          left: 467,
          top: 302,
          width: 140,
          height: 140,
          border: "2px solid #F2F1ED",
          outline: "9px solid #F2F1ED1A",
        }}
      />
    </div>
    <svg
      viewBox="0 0 1080 1920"
      style={{ ...abs, inset: 0, width: 1080, height: 1920 }}
      fill="none"
    >
      <path
        d="M-110 1190C100 1090 91 1530 290 1517C560 1502 490 1183 768 1214C990 1236 1068 1152 1170 1045"
        stroke={palette.red}
        strokeWidth="49"
      />
      <path
        d="M-104 1225C126 1143 108 1571 307 1545C570 1510 514 1223 775 1251C990 1276 1091 1190 1178 1088"
        stroke={palette.green}
        strokeWidth="21"
      />
      <path
        d="M-110 1261C116 1170 130 1607 330 1578C570 1542 536 1260 785 1292C996 1320 1100 1227 1190 1126"
        stroke={palette.blue}
        strokeWidth="13"
      />
    </svg>
    <div style={{ ...abs, right: 85, top: 1260, transform: "rotate(7deg)" }}>
      <Curve />
    </div>
    <div
      style={{
        ...abs,
        left: 88,
        bottom: 125,
        fontSize: 44,
        lineHeight: 1.25,
        letterSpacing: -1,
      }}
    >
      Tone curves.
      <br />
      Local adjustments.
    </div>
  </Scene>
);

// Authored vector silhouette in the unchanged 1200 × 1800 camera-preview coordinates.
// It is an art-direction mask; replace with a saved Looksy mask before final production.
export const subjectPath =
  "M554 774 Q550 761 574 760 L595 760 Q612 761 619 786 L624 807 630 812 623 840 610 853 616 885 638 945 Q656 990 668 1030 L680 1081 674 1139 665 1149 668 1200 661 1266 659 1324 650 1350 648 1413 638 1478 641 1493 Q662 1512 690 1516 L695 1528 Q672 1549 626 1545 L599 1540 Q573 1546 555 1533 L555 1511 570 1490 553 1489 549 1454 550 1396 546 1333 535 1301 539 1268 533 1225 530 1187 530 1150 525 1135 526 1094 Q480 1067 480 1035 L484 989 495 949 508 922 504 892 Q500 875 508 858 L535 844 544 825 540 811 547 800 Z";

export const MaskArt = () => {
  const id = useId().replace(/:/g, "");
  return (
    <svg
      viewBox="0 0 1200 1800"
      width="100%"
      height="100%"
      style={{ display: "block" }}
    >
      <defs>
        <clipPath id={id}>
          <path d={subjectPath} />
        </clipPath>
      </defs>
      <image href={asset("IMG_5106.jpg")} width="1200" height="1800" />
      <text
        x="600"
        y="1045"
        textAnchor="middle"
        fill={palette.paper}
        style={{
          fontFamily: "Manrope",
          fontWeight: 800,
          fontSize: 310,
          letterSpacing: -23,
        }}
      >
        NEPAL
      </text>
      <image
        href={asset("IMG_5106.jpg")}
        width="1200"
        height="1800"
        clipPath={`url(#${id})`}
      />
      <text
        x="61"
        y="1728"
        fill={palette.paper}
        style={{
          fontFamily: "Manrope",
          fontWeight: 500,
          fontSize: 26,
          letterSpacing: 6,
        }}
      >
        A LITTLE FURTHER.
      </text>
      <path
        d="M925 1590C1080 1530 1100 1470 1070 1437C1043 1405 927 1469 919 1543C910 1618 1044 1652 1180 1553"
        stroke={palette.red}
        strokeWidth="32"
        fill="none"
        strokeLinecap="round"
      />
    </svg>
  );
};

export const Layers = () => (
  <div
    style={{
      width: 340,
      background: palette.paper,
      color: palette.ink,
      padding: "22px 26px",
      boxShadow: "0 28px 60px #0004",
    }}
  >
    <div style={{ fontSize: 23, marginBottom: 18 }}>
      Layers <span style={{ float: "right" }}>＋</span>
    </div>
    {["Brush mark", "Subject mask", "NEPAL", "IMG_5106.CR2"].map((label, i) => (
      <div
        key={label}
        style={{
          height: 48,
          borderTop: "1px solid #16161620",
          display: "flex",
          alignItems: "center",
          gap: 16,
          fontSize: 20,
          color: i === 1 ? "#3054F0" : "#55564F",
        }}
      >
        <span
          style={{
            width: 25,
            height: 25,
            background: [palette.red, palette.blue, "#D7D7CF", "#AEC5D3"][i],
            color: "white",
            display: "grid",
            placeItems: "center",
            fontSize: 14,
          }}
        >
          {i === 1 ? "◒" : i === 2 ? "T" : ""}
        </span>
        {label}
        <span style={{ marginLeft: "auto", opacity: 0.5 }}>◉</span>
      </div>
    ))}
  </div>
);

export const MaskFrame = () => (
  <Scene dark>
    <LightContours
      color={palette.green}
      style={{ left: -580, top: 440, transform: "rotate(20deg)" }}
    />
    <Brand light />
    <Index number="02" light />
    <div
      style={{
        ...abs,
        left: 80,
        top: 232,
        fontSize: 113,
        lineHeight: 1.02,
        letterSpacing: -7,
        fontWeight: 800,
      }}
    >
      Keep it
      <br />
      <span style={{ color: palette.green }}>editable.</span>
    </div>
    <div style={{ ...abs, left: 88, top: 504, fontSize: 44, color: "#BCBDB6" }}>
      Layers. Masks. Pixel editing.
    </div>
    <div
      style={{
        ...abs,
        left: 177,
        top: 630,
        width: 750,
        height: 1125,
        border: "2px solid #4465FF",
        transform:
          "perspective(2000px) rotateY(-10deg) rotateZ(-9deg) translate(-44px, 40px)",
      }}
    />
    <div
      style={{
        ...abs,
        left: 177,
        top: 630,
        width: 750,
        height: 1125,
        background: palette.blue,
        transform:
          "perspective(2000px) rotateY(-10deg) rotateZ(-6deg) translate(-18px, 21px)",
      }}
    />
    <div
      style={{
        ...abs,
        left: 177,
        top: 630,
        width: 750,
        height: 1125,
        transform: "perspective(2000px) rotateY(-10deg) rotateZ(-3deg)",
        boxShadow: "15px 35px 65px #0006",
      }}
    >
      <MaskArt />
    </div>
    <svg
      viewBox="0 0 1080 1920"
      width="1080"
      height="1920"
      style={{ ...abs, inset: 0 }}
      fill="none"
    >
      <path
        d="M1130 684C1030 645 979 728 1019 825C1073 956 969 963 955 1052C942 1137 1058 1116 1148 1209"
        stroke={palette.green}
        strokeWidth="40"
      />
      <path
        d="M-155 1653C73 1520 24 1390 92 1343C181 1283 204 1445 104 1488"
        stroke={palette.blue}
        strokeWidth="55"
      />
      <path
        d="M-135 1695C105 1555 55 1400 112 1380"
        stroke={palette.red}
        strokeWidth="10"
      />
      <path
        d="M126 1150V1202H178M131 1577V1525H183"
        stroke={palette.paper}
        strokeWidth="3"
      />
    </svg>
    <div style={{ ...abs, right: 50, top: 1462, transform: "rotate(5deg)" }}>
      <Layers />
    </div>
  </Scene>
);

const FilmStrip = ({ name, width }: { name: string; width: number }) => (
  <div
    style={{
      width,
      height: 91,
      display: "flex",
      overflow: "hidden",
      border: "2px solid #5476FF",
      background: "#B8C9D5",
    }}
  >
    {[0, 1, 2, 3].map((n) => (
      <div
        key={n}
        style={{
          minWidth: 104,
          width: 104,
          height: 91,
          borderRight: "1px solid white",
        }}
      >
        <Image name={name} />
      </div>
    ))}
  </div>
);

export const Timeline = () => (
  <div
    style={{
      width: 1160,
      background: palette.paper,
      color: palette.ink,
      padding: "28px 38px 26px",
      boxSizing: "border-box",
      boxShadow: "0 30px 55px #0005",
    }}
  >
    <div
      style={{
        display: "flex",
        justifyContent: "space-between",
        fontSize: 26,
        marginBottom: 19,
      }}
    >
      <span>Nepal / sequence 01</span>
      <span style={{ fontSize: 21 }}>00:00:08:12</span>
    </div>
    <div
      style={{
        display: "flex",
        justifyContent: "space-between",
        fontSize: 17,
        color: "#8B8B81",
        marginBottom: 13,
      }}
    >
      {["00:00", "00:04", "00:08", "00:12", "00:16", "00:20"].map((t) => (
        <span key={t}>{t}</span>
      ))}
    </div>
    <div style={{ position: "relative" }}>
      <div style={{ display: "flex", gap: 8 }}>
        <FilmStrip name="IMG_1207-2-7.jpg" width={390} />
        <FilmStrip name="IMG_1268-2-7.jpg" width={330} />
        <FilmStrip name="IMG_1207-2-7.jpg" width={340} />
      </div>
      <svg
        width="1084"
        height="62"
        style={{ display: "block", background: "#C5EAB7", marginTop: 9 }}
        viewBox="0 0 1084 62"
      >
        <path d="M0 31H1084" stroke="#489054" strokeWidth="1" />
        {Array.from({ length: 190 }, (_, n) => {
          const h = 4 + Math.abs(Math.sin(n * 1.8) * Math.cos(n * 0.23)) * 46;
          return (
            <rect
              key={n}
              x={n * 5.7}
              y={(62 - h) / 2}
              width="2.8"
              height={h}
              fill="#4A9754"
            />
          );
        })}
      </svg>
      <div
        style={{
          ...abs,
          top: -30,
          left: 447,
          width: 3,
          height: 207,
          background: palette.red,
        }}
      >
        <div
          style={{
            width: 19,
            height: 17,
            background: palette.red,
            transform: "translateX(-8px)",
            clipPath: "polygon(0 0, 100% 0, 100% 50%, 50% 100%, 0 50%)",
          }}
        />
      </div>
    </div>
  </div>
);

export const VideoFrame = () => (
  <Scene>
    <LightContours
      color={palette.blue}
      style={{ left: 400, top: 600, transform: "rotate(38deg)" }}
    />
    <Brand />
    <Index number="03" />
    <div
      style={{
        ...abs,
        left: 80,
        top: 232,
        fontSize: 102,
        lineHeight: 1.06,
        letterSpacing: -6,
        fontWeight: 800,
      }}
    >
      From first cut
      <br />
      to final frame<span style={{ color: palette.blue }}>.</span>
    </div>
    <div style={{ ...abs, left: 88, top: 498, fontSize: 44 }}>
      Timeline editing. Color. Audio.
    </div>
    <div
      style={{
        ...abs,
        left: 76,
        top: 656,
        width: 930,
        height: 523,
        transform: "perspective(2000px) rotateY(8deg) rotateZ(-5deg)",
        boxShadow: "18px 25px 0 #4465FF",
      }}
    >
      <Image name="IMG_1207-2-7.jpg" />
      <div
        style={{
          ...abs,
          top: 25,
          left: 26,
          background: palette.ink,
          color: "white",
          padding: "12px 16px",
          fontSize: 23,
        }}
      >
        3840 × 2160 <span style={{ color: "#BBB" }}> / SOURCE</span>
      </div>
      <div style={{ ...abs, inset: 32, border: "1px solid #FFFFFF66" }} />
    </div>
    <svg
      viewBox="0 0 1080 1920"
      width="1080"
      height="1920"
      fill="none"
      style={{ ...abs, inset: 0 }}
    >
      <path
        d="M-90 740C98 691 95 1095 302 1224C546 1375 842 1082 1130 1240"
        stroke={palette.red}
        strokeWidth="36"
      />
      <path
        d="M-90 784C102 728 109 1121 319 1258C563 1415 847 1120 1130 1278"
        stroke={palette.green}
        strokeWidth="17"
      />
    </svg>
    <div
      style={{
        ...abs,
        left: -47,
        top: 1198,
        transform: "perspective(2200px) rotateX(13deg) rotateZ(5deg)",
      }}
    >
      <Timeline />
    </div>
    <div
      style={{
        ...abs,
        left: 90,
        top: 1640,
        display: "flex",
        gap: 18,
        alignItems: "center",
      }}
    >
      <div
        style={{
          width: 156,
          height: 195,
          background: palette.blue,
          transform: "rotate(-8deg)",
          color: "white",
          padding: 16,
          boxSizing: "border-box",
          fontSize: 28,
          fontWeight: 800,
          lineHeight: 1,
        }}
      >
        NE
        <br />
        PAL
        <span
          style={{
            display: "block",
            marginTop: 47,
            fontSize: 12,
            fontWeight: 500,
          }}
        >
          01 / THE JOURNEY
        </span>
      </div>
      <div style={{ width: 156, height: 195, transform: "rotate(-2deg)" }}>
        <Image name="IMG_4870.jpg" />
      </div>
      <div style={{ width: 156, height: 195, transform: "rotate(4deg)" }}>
        <Image name="IMG_4969.jpg" />
      </div>
      <div
        style={{
          marginLeft: 27,
          fontSize: 35,
          letterSpacing: -1,
          lineHeight: 1.25,
        }}
      >
        One design.
        <br />
        Every panel.
        <span
          style={{
            display: "block",
            marginTop: 14,
            fontSize: 23,
            color: "#73746D",
          }}
        >
          Carousel layouts ↗
        </span>
      </div>
    </div>
  </Scene>
);

export const StyleBoard = () => (
  <AbsoluteFill
    style={{
      background: "#DCDDD7",
      color: palette.ink,
      fontFamily: "Manrope",
      padding: "70px 84px",
    }}
  >
    <div style={{ fontSize: 56, fontWeight: 800, letterSpacing: -2 }}>
      Looksy / light into craft.
    </div>
    <div
      style={{
        position: "absolute",
        right: 84,
        top: 83,
        fontSize: 24,
        color: "#666A60",
      }}
    >
      VISUAL DEVELOPMENT 01 / THREE ANCHOR FRAMES
    </div>
    <div style={{ fontSize: 27, marginTop: 17, color: "#666A60" }}>
      Flat artwork. Dimensional camera. One continuous creative journey.
    </div>
    <div
      style={{
        position: "absolute",
        left: 84,
        top: 222,
        width: 594,
        height: 1056,
        boxShadow: "0 16px 35px #0001",
      }}
    >
      <div
        style={{
          position: "relative",
          width: 1080,
          height: 1920,
          transform: "scale(.55)",
          transformOrigin: "top left",
        }}
      >
        <ColorFrame />
      </div>
    </div>
    <div
      style={{
        position: "absolute",
        left: 783,
        top: 222,
        width: 594,
        height: 1056,
        boxShadow: "0 16px 35px #0001",
      }}
    >
      <div
        style={{
          position: "relative",
          width: 1080,
          height: 1920,
          transform: "scale(.55)",
          transformOrigin: "top left",
        }}
      >
        <MaskFrame />
      </div>
    </div>
    <div
      style={{
        position: "absolute",
        left: 1482,
        top: 222,
        width: 594,
        height: 1056,
        boxShadow: "0 16px 35px #0001",
      }}
    >
      <div
        style={{
          position: "relative",
          width: 1080,
          height: 1920,
          transform: "scale(.55)",
          transformOrigin: "top left",
        }}
      >
        <VideoFrame />
      </div>
    </div>
    <div style={{ position: "absolute", left: 84, top: 1322, width: 594 }}>
      <div style={{ fontSize: 30, fontWeight: 800 }}>01 / LIGHT → CURVE</div>
      <p style={{ fontSize: 25, lineHeight: 1.45, color: "#61645B" }}>
        The camera follows the RGB ribbon into the photograph. It tightens into
        a curve; a control point lands on the beat.
      </p>
    </div>
    <div style={{ position: "absolute", left: 783, top: 1322, width: 594 }}>
      <div style={{ fontSize: 30, fontWeight: 800 }}>02 / CURVE → MASK</div>
      <p style={{ fontSize: 25, lineHeight: 1.45, color: "#61645B" }}>
        Orbit through flat image layers. The curve traces the subject; type
        slides behind it. Settle face-on to read the result.
      </p>
    </div>
    <div style={{ position: "absolute", left: 1482, top: 1322, width: 594 }}>
      <div style={{ fontSize: 30, fontWeight: 800 }}>
        03 / STROKE → PLAYHEAD
      </div>
      <p style={{ fontSize: 25, lineHeight: 1.45, color: "#61645B" }}>
        A brush stroke straightens into the playhead. Dive through footage, then
        unfold the carousel on the same camera path.
      </p>
    </div>
    <div
      style={{
        position: "absolute",
        bottom: 45,
        left: 84,
        fontSize: 20,
        color: "#777C70",
      }}
    >
      STYLE FRAMES / ANIMATED INTERFACE REPRESENTATIONS / SOURCE PHOTOGRAPHY AND
      FOOTAGE: BRADY REINER
    </div>
  </AbsoluteFill>
);
