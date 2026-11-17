import { looksyWebsite } from "./site-data";
import {
  A,
  Pic,
  useShot,
  smooth,
  out,
  mix,
  Light,
  Native,
  Label,
  Type,
  Footer,
  font,
  paper,
  lime,
  AppWordmark,
  PaperGrain,
  Pills,
} from "./Kit";
import { PanoramaArtwork } from "../desktop-launch/Panorama";
import { Pointer } from "./ProofTools";

// Buyer-facing chapters retain the original film clock and source assets.
// All interaction sequences here are authored illustrations, not capture claims.
export const BuyerOpening = () => {
  const { t, width, height, portrait } = useShot(),
    hero = out(t, 2, 3.1),
    desktop = smooth(t, 8, 9.6);
  return (
    <>
      <Light t={t} opacity={0.7} gather={1 - smooth(t, 1, 4)} />
      <div style={{ ...A, inset: 0, opacity: hero * (1 - desktop) }}>
        <Pic
          src="creative-polish-v2/curve/curve-16.png"
          style={{
            left: portrait ? 0 : width * 0.25,
            width: portrait ? width : width * 0.75,
            transform: `scale(${mix(1.08, 1, smooth(t, 2, 8))})`,
          }}
        />
        <div
          style={{
            ...A,
            inset: 0,
            background: portrait
              ? "linear-gradient(180deg,#080c13 4%,#080c13d9 32%,transparent 80%,#080c13)"
              : "linear-gradient(90deg,#080c13 8%,#080c13e8 30%,transparent 80%)",
          }}
        />
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 65 : 90,
          top: portrait ? 150 : 80,
          opacity: 1 - desktop,
          transform: `translateY(${30 * (1 - out(t, 0, 0.7))}px)`,
        }}
      >
        <Label>PHOTO + VIDEO / DESKTOP</Label>
        <div style={{ marginTop: 25 }}>
          <AppWordmark width={portrait ? 650 : 480} />
        </div>
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 65 : 90,
          top: portrait ? 500 : 310,
          width: portrait ? 950 : 1150,
          opacity: hero * (1 - desktop),
          transform: `translateY(${45 * (1 - hero)}px)`,
        }}
      >
        <Type size={portrait ? 150 : 145}>
          One shoot.
          <br />
          Every deliverable.
        </Type>
        <div
          style={{
            fontFamily: font.sans,
            fontSize: portrait ? 39 : 38,
            lineHeight: 1.4,
            color: paper,
            marginTop: 35,
            maxWidth: 800,
          }}
        >
          From camera RAW to finished photos,
          <br />
          layered designs and films. In one app.
        </div>
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 65 : 95,
          top: portrait ? 1390 : 880,
          opacity: out(t, 5, 6) * (1 - desktop),
        }}
      >
        <Pills items={["EDIT", "DESIGN", "CUT", "DELIVER"]} />
      </div>
      <div
        style={{
          ...A,
          inset: 0,
          opacity: desktop,
          clipPath: `circle(${desktop * Math.max(width, height)}px at 58% 55%)`,
        }}
      >
        <Light t={t} opacity={0.24} />
        <div
          style={{
            ...A,
            left: portrait ? 40 : 285,
            top: portrait ? 650 : 245,
            width: portrait ? 1000 : 1350,
            height: portrait ? 585 : 790,
            transform: `scale(${mix(0.92, 1, desktop)})`,
            transformOrigin: "50% 40%",
          }}
        >
          <Native
            src="desktop-launch/raw-editor.png"
            style={{ top: 0, width: "100%", height: "100%" }}
          />
        </div>
        <div
          style={{
            ...A,
            left: portrait ? 65 : 85,
            top: portrait ? 170 : 75,
            width: portrait ? 950 : 1740,
          }}
        >
          <Label>MEET LOOKSY</Label>
          <Type size={portrait ? 115 : 104} style={{ marginTop: 22 }}>
            Your work. From start to finish.
          </Type>
        </div>
      </div>
      <Footer
        left="FOR PROFESSIONALS. FOR THE WORK YOU MAKE FOR YOURSELF."
        right="LOOKSY / DESKTOP"
      />
    </>
  );
};

export const GalleryDelivery = ({
  s,
  batch = false,
}: {
  s: number;
  batch?: boolean;
}) => {
  const { portrait, width } = useShot(),
    names = ["wedding-walk", "wedding-embrace", "wedding-play"];
  const margin = portrait ? 45 : 85,
    gap = portrait ? 12 : 22,
    cardW = (width - margin * 2 - gap * 2) / 3,
    cardH = portrait ? 720 : 540;
  const u = batch ? smooth(s, 0.7, 3.7) : 0;
  return (
    <>
      <Light t={batch ? 86 + s : 79 + s} opacity={0.22} />
      <div
        style={{
          ...A,
          left: portrait ? 65 : 85,
          top: portrait ? 160 : 75,
          width: width - 130,
        }}
      >
        <Label>02 / LIBRARY & WORKFLOW</Label>
        <Type size={portrait ? 112 : 104} style={{ marginTop: 25 }}>
          {batch
            ? "A consistent finish.\nAcross the gallery."
            : "For the moments\nyou only get once."}
        </Type>
        <div
          style={{
            fontFamily: font.sans,
            color: paper,
            fontSize: portrait ? 36 : 32,
            marginTop: 22,
            maxWidth: 1400,
          }}
        >
          {batch
            ? "Apply a shared adjustment. Keep individual control."
            : "Cull the shoot. Refine the keepers. Deliver a considered set."}
        </div>
      </div>
      {names.map((name, i) => (
        <div
          key={name}
          style={{
            ...A,
            left: margin + i * (cardW + gap),
            top: portrait ? 730 : 430,
            width: cardW,
            height: cardH,
            overflow: "hidden",
            borderRadius: 5,
            opacity: batch ? 1 : out(s, i * 0.12, 0.55 + i * 0.12),
            transform: `translateY(${batch ? 0 : 35 * (1 - out(s, i * 0.12, 0.9 + i * 0.12))}px)`,
          }}
        >
          <Pic src={`creative-polish-v2/${name}/batch-before.png`} />
          <Pic
            src={`creative-polish-v2/${name}/batch-after.png`}
            style={{ clipPath: `inset(0 ${100 * (1 - u)}% 0 0)` }}
          />
          <div
            style={{
              ...A,
              left: 14,
              bottom: 18,
              padding: "9px 12px",
              background: "#09101bd9",
              fontFamily: font.mono,
              fontSize: portrait ? 17 : 18,
              color: paper,
            }}
          >
            {batch
              ? u < 0.99
                ? "SHARED RECIPE / APPLYING"
                : "SHARED RECIPE / EDITABLE"
              : "WEDDING / SELECTED FRAME"}
          </div>
        </div>
      ))}
      <Footer
        left="ONE RECIPE. ROOM FOR EVERY PHOTOGRAPH."
        right="LOOKSY / LIBRARY"
      />
    </>
  );
};

export const BuyerCarousel = () => {
  const { t, portrait, width } = useShot(),
    split = smooth(t, 6, 8),
    phone = smooth(t, 15, 17),
    sc = portrait ? 0.27 : 0.43;
  const pw = 1080 * sc,
    ph = 1350 * sc,
    gap = 14 * split,
    pan = mix(-110, 110, smooth(t, 8.5, 12.5));
  const swipe = smooth(t, 18, 19.3) + smooth(t, 20.7, 22),
    phoneW = portrait ? 550 : 420,
    contentW = phoneW - 28,
    phoneH = contentW * 1.25 + 155;
  return (
    <>
      <Light t={160 + t} opacity={0.3} />
      <div
        style={{
          ...A,
          left: portrait ? 65 : 85,
          top: portrait ? 160 : 65,
          width: width - 130,
        }}
      >
        <Label>04 / CAROUSEL</Label>
        <Type size={portrait ? 110 : 102} style={{ marginTop: 24 }}>
          {t < 15
            ? "One photograph.\nAcross every canvas."
            : "Designed together.\nReady to swipe."}
        </Type>
        <div
          style={{
            fontFamily: font.sans,
            fontSize: portrait ? 36 : 32,
            marginTop: 22,
            color: "#c9ced6",
          }}
        >
          {t < 15
            ? "Compose the whole story. Export individual panels."
            : "The same composition. Three separate 4:5 images."}
        </div>
      </div>
      <div
        style={{
          ...A,
          left: mix(
            (width - (pw * 3 + gap * 2)) / 2,
            portrait ? (width - (pw * 3 + gap * 2)) / 2 : 85,
            phone,
          ),
          top: portrait ? 790 : mix(395, 470, phone),
          width: pw * 3 + gap * 2,
          height: ph,
          opacity: portrait ? 1 - phone : 1,
          transform: `translateY(${-40 * phone}px) scale(${1 - (portrait ? 0.08 : 0.34) * phone})`,
          transformOrigin: "0 0",
        }}
      >
        {[0, 1, 2].map((i) => (
          <div
            key={i}
            style={{
              ...A,
              left: i * (pw + gap),
              width: pw,
              height: ph,
              overflow: "hidden",
              border: `${split ? 1 : 0}px solid #d8e4dc66`,
              borderRadius: 3 * split,
            }}
          >
            <div
              style={{
                ...A,
                left: -i * pw,
                width: 3240,
                height: 1350,
                transform: `scale(${sc})`,
                transformOrigin: "0 0",
              }}
            >
              <PanoramaArtwork imagePan={pan} imageZoom={1.12} />
            </div>
          </div>
        ))}
        <Pointer
          x={pw * 1.4 + mix(-25, 70, smooth(t, 8.5, 12.5))}
          y={ph * 0.55}
          opacity={smooth(t, 8, 8.4) * (1 - smooth(t, 12.7, 13.2))}
        />
      </div>
      <div
        style={{
          ...A,
          left: portrait ? (width - phoneW) / 2 : width - 120 - phoneW,
          top: portrait ? 720 : 385,
          width: phoneW,
          height: phoneH,
          opacity: phone,
          transform: `translateY(${65 * (1 - phone)}px)`,
          border: "2px solid #8a98aa80",
          borderRadius: 34,
          background: "#111822",
          padding: 12,
          boxShadow: "0 35px 90px #0008",
        }}
      >
        <div
          style={{
            height: 48,
            display: "flex",
            alignItems: "center",
            gap: 12,
            color: paper,
            fontFamily: font.sans,
            fontSize: 19,
            paddingLeft: 10,
          }}
        >
          <span
            style={{
              width: 22,
              height: 22,
              borderRadius: 20,
              background: lime,
            }}
          />
          Field journal
          <span
            style={{
              marginLeft: "auto",
              marginRight: 15,
              fontFamily: font.mono,
              fontSize: 14,
            }}
          >
            {1 + Math.round(swipe)} / 3
          </span>
        </div>
        <div
          style={{
            position: "relative",
            width: contentW,
            height: contentW * 1.25,
            overflow: "hidden",
          }}
        >
          <div
            style={{
              ...A,
              left: -swipe * contentW,
              top: 0,
              width: 3240,
              height: 1350,
              transform: `scale(${contentW / 1080})`,
              transformOrigin: "0 0",
            }}
          >
            <PanoramaArtwork imagePan={110} imageZoom={1.12} />
          </div>
        </div>
        <div
          style={{
            display: "flex",
            gap: 8,
            justifyContent: "center",
            marginTop: 17,
          }}
        >
          {[0, 1, 2].map((i) => (
            <span
              key={i}
              style={{
                width: 7,
                height: 7,
                borderRadius: 8,
                background: Math.round(swipe) === i ? lime : "#586574",
              }}
            />
          ))}
        </div>
        <div
          style={{
            fontFamily: font.sans,
            fontSize: 17,
            color: paper,
            margin: "15px 9px",
          }}
        >
          Take the long way. ↗
        </div>
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 65 : 100,
          bottom: portrait ? 190 : 60,
          opacity: 1 - phone,
        }}
      >
        <Pills
          items={
            t < 6
              ? ["3240 × 1350 / ONE COMPOSITION"]
              : ["01.PNG", "02.PNG", "03.PNG", "1080 × 1350 EACH"]
          }
        />
      </div>
      <Footer
        left="COMPOSE ONCE. LET THE STORY CONTINUE."
        right="LOOKSY / CAROUSEL"
      />
    </>
  );
};

export const BuyerClosing = () => {
  const { t, portrait, width, height } = useShot(),
    hero = 1 - smooth(t, 7, 8.5),
    cta = smooth(t, 8.5, 10);
  return (
    <>
      <Light t={280 + t} opacity={0.65} gather={smooth(t, 6, 15)} />
      <div
        style={{
          ...A,
          left: portrait ? 65 : 85,
          top: portrait ? 170 : 90,
          opacity: hero,
          width: width - 130,
        }}
      >
        <Label>PHOTO. DESIGN. FILM.</Label>
        <Type size={portrait ? 135 : 125} style={{ marginTop: 25 }}>
          Make the whole thing.
        </Type>
        <div
          style={{
            fontFamily: font.sans,
            fontSize: portrait ? 38 : 36,
            color: paper,
            marginTop: 26,
          }}
        >
          One desktop studio. Your work stays yours to refine.
        </div>
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 40 : 310,
          top: portrait ? 770 : 390,
          width: portrait ? 1000 : 1300,
          height: portrait ? 585 : 760,
          opacity: hero,
          transform: `scale(${mix(0.96, 1, smooth(t, 0, 7))})`,
        }}
      >
        <Native
          src="desktop-launch/raw-editor.png"
          style={{ top: 0, width: "100%", height: "100%" }}
        />
      </div>
      <div
        style={{
          ...A,
          left: 0,
          top: height * (portrait ? 0.3 : 0.26),
          width,
          textAlign: "center",
          opacity: cta,
          transform: `translateY(${30 * (1 - cta)}px)`,
        }}
      >
        <div style={{ display: "flex", justifyContent: "center" }}>
          <AppWordmark width={portrait ? 800 : 900} />
        </div>
        <div
          style={{
            fontFamily: font.serif,
            fontStyle: "italic",
            fontSize: portrait ? 55 : 57,
            color: paper,
            marginTop: 32,
          }}
        >
          From your first edit to your final delivery.
        </div>
        <div
          style={{
            display: "inline-flex",
            alignItems: "center",
            gap: 30,
            marginTop: 60,
            padding: "23px 38px",
            borderRadius: 8,
            background: lime,
            color: "#101820",
            fontFamily: font.sans,
            fontSize: portrait ? 38 : 35,
            fontWeight: 600,
          }}
        >
          Visit the website <span>↗</span>
        </div>
        {looksyWebsite && (
          <div
            style={{
              fontFamily: font.mono,
              fontSize: portrait ? 30 : 28,
              color: paper,
              marginTop: 25,
            }}
          >
            {looksyWebsite}
          </div>
        )}
      </div>
      <PaperGrain opacity={0.12} />
      <Footer
        left="FOR WORK. FOR YOURSELF. FOR WHAT COMES NEXT."
        right="LOOKSY / DESKTOP"
      />
    </>
  );
};

export const DeliveryProof = ({ s }: { s: number }) => {
  const { portrait, width } = useShot();
  const columns = [
    ["COLOR", "sRGB · Rec.709", "Color-managed workflows"],
    ["LOG INPUT", "Sony S-Log2 · S-Log3", "Supported camera transforms"],
    ["DELIVERY", "4K · ProRes", "Resolution and codec options"],
  ];
  return (
    <>
      <Light t={232 + s} opacity={0.26} />
      <div
        style={{
          ...A,
          left: portrait ? 65 : 85,
          top: portrait ? 160 : 70,
          width: width - 130,
        }}
      >
        <Label>05 / VIDEO TIMELINE</Label>
        <Type size={portrait ? 114 : 106} style={{ marginTop: 24 }}>
          From camera color
          <br />
          to final delivery.
        </Type>
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 45 : 85,
          top: portrait ? 690 : 355,
          width: portrait ? 990 : 1080,
          height: portrait ? 557 : 608,
          overflow: "hidden",
          borderRadius: 10,
        }}
      >
        <Pic src="studio/city-delivery.jpg" />
        <div
          style={{
            ...A,
            left: 25,
            bottom: 25,
            padding: "12px 17px",
            background: "#0b1424de",
            color: paper,
            fontFamily: font.mono,
            fontSize: 20,
          }}
        >
          CURRENT EXAMPLE / Rec.709 SOURCE
        </div>
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 65 : 1240,
          top: portrait ? 1260 : 380,
          width: portrait ? 950 : 590,
        }}
      >
        {columns.map(([label, value, detail], i) => (
          <div
            key={label}
            style={{
              borderTop: "1px solid #b3c2cf55",
              padding: portrait ? "14px 0" : "22px 0",
              opacity: out(s, i * 0.35, 0.55 + i * 0.35),
              transform: `translateY(${20 * (1 - out(s, i * 0.35, 0.55 + i * 0.35))}px)`,
            }}
          >
            <Label style={{ fontSize: portrait ? 20 : 19 }}>{label}</Label>
            <div
              style={{
                fontFamily: font.sans,
                fontSize: portrait ? 35 : 37,
                color: paper,
                marginTop: 10,
              }}
            >
              {value}
            </div>
            <div
              style={{
                fontFamily: font.sans,
                fontSize: portrait ? 24 : 24,
                color: "#b7c4d2",
                marginTop: 8,
              }}
            >
              {detail}
            </div>
          </div>
        ))}
      </div>
      <Footer
        left="COLOR SPACES. CAMERA INPUTS. DELIVERY OPTIONS."
        right="LOOKSY / VIDEO"
      />
    </>
  );
};

export const PhotoDelivery = ({ s }: { s: number }) => {
  const { portrait, width } = useShot();
  return (
    <>
      <Light t={57 + s} opacity={0.25} />
      <div
        style={{
          ...A,
          left: portrait ? 65 : 85,
          top: portrait ? 160 : 75,
          width: width - 130,
        }}
      >
        <Label>01 / PHOTO EDITOR</Label>
        <Type size={portrait ? 115 : 108} style={{ marginTop: 25 }}>
          Deliver the photograph.
          <br />
          Keep every pixel.
        </Type>
        <div
          style={{
            fontFamily: font.sans,
            fontSize: portrait ? 36 : 32,
            color: paper,
            marginTop: 22,
          }}
        >
          Full-resolution output from the same camera RAW.
        </div>
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 45 : 85,
          top: portrait ? 740 : 430,
          width: portrait ? 990 : 930,
          height: portrait ? 660 : 620,
          overflow: "hidden",
          borderRadius: 8,
        }}
      >
        <Pic src="creative-polish-v4/raw-export/preview.jpg" />
      </div>
      <div
        style={{
          ...A,
          left: portrait ? 65 : 1120,
          top: portrait ? 1470 : 485,
          width: portrait ? 950 : 690,
          display: "flex",
          flexDirection: portrait ? "row" : "column",
          gap: portrait ? 18 : 32,
        }}
      >
        {[
          ["JPEG", "6024 × 4020", "CLIENT GALLERY"],
          ["16-BIT TIFF", "6024 × 4020", "FULL-RESOLUTION MASTER"],
        ].map(([format, size, use], i) => (
          <div
            key={format}
            style={{
              flex: 1,
              borderTop: "1px solid #99aeb966",
              paddingTop: 20,
              opacity: out(s, 0.3 + i * 0.25, 0.9 + i * 0.25),
              transform: `translateY(${25 * (1 - out(s, 0.3 + i * 0.25, 0.9 + i * 0.25))}px)`,
            }}
          >
            <Label style={{ fontSize: portrait ? 25 : 26 }}>{format}</Label>
            <div
              style={{
                fontFamily: font.sans,
                fontSize: portrait ? 38 : 42,
                color: paper,
                marginTop: 15,
              }}
            >
              {size}
            </div>
            <div
              style={{
                fontFamily: font.mono,
                fontSize: portrait ? 17 : 17,
                color: "#aebdcb",
                marginTop: 16,
              }}
            >
              {use}
            </div>
          </div>
        ))}
      </div>
      <Footer left="CAMERA ORIGINAL TO FINISHED FILE." right="LOOKSY / PHOTO" />
    </>
  );
};
