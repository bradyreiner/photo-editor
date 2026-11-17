import { staticFile } from "remotion";
import { AppActivity } from "../app-kit/AppElements";
import { A, Icon, mix, Panel, s, Tag, Title, window } from "./Core";
export const RetouchInsert = ({ t }: { t: number }) => {
  const zoom = s(t, 20.6, 21.7) * (1 - s(t, 25, 26)),
    fill = s(t, 23, 23.7),
    paint = window(t, 21.8, 22.9),
    vx = mix(87.805, 453, zoom),
    vy = mix(0, 755, zoom),
    vw = mix(1264.39, 270, zoom),
    vh = mix(960, 205, zoom);
  return (
    <>
      <Title
        t={t}
        a={20.6}
        b={26.3}
        lines={["Finish the details."]}
        detail="Retouch. Clone. Inpaint."
      />
      <svg
        width={1080}
        height={820}
        viewBox={`${vx} ${vy} ${vw} ${vh}`}
        style={{ ...A, left: 0, top: 540, overflow: "hidden" }}
      >
        <image
          href={staticFile("media/studio/raw-colour/4-8.png")}
          width={1440}
          height={960}
        />
        <image
          href={staticFile("media/studio/retouch-final.png")}
          width={1440}
          height={960}
          opacity={fill}
        />
        <image
          href={staticFile("media/studio/retouch-overlay.png")}
          width={1440}
          height={960}
          opacity={paint * 0.7}
        />
        <ellipse
          cx={588}
          cy={874}
          rx={25}
          ry={22}
          transform="rotate(-12 588 874)"
          stroke="white"
          strokeWidth={0.6}
          fill="none"
          opacity={paint}
        />
      </svg>
      <Panel
        style={{
          ...A,
          left: 80,
          top: 1430,
          width: 920,
          padding: 30,
          background: "#11181bec",
        }}
      >
        <Tag>RETOUCH / REMOVE A DISTRACTION</Tag>
        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: 24,
            fontSize: 36,
            marginTop: 27,
          }}
        >
          <Icon name="brush" size={34} />
          <AppActivity
            size={42}
            progress={fill}
            active={t >= 22.9 && t < 23.7}
          />
          {fill > 0.99
            ? "Inpaint complete"
            : t < 22.9
              ? "Paint the area."
              : "Inpainting the selection…"}
        </div>
        <div style={{ fontSize: 30, color: "#9fa5a7", marginTop: 24 }}>
          The surrounding photograph stays intact.
        </div>
      </Panel>
    </>
  );
};
