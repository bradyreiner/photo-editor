import { useId } from "react";
import {
  AbsoluteFill,
  CanvasImage,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { AppWordmark, AppIcon, appUiFont } from "./app-kit/AppElements";
import { subjectPath } from "./StyleFrames";
import {
  abs,
  asset,
  contour,
  brandAccentAt,
  mix,
  photos,
  Photo,
  poseAt,
  rippleAt,
  T,
  windowAt,
} from "./film/system";
import { PosterArtwork } from "./film/Artwork";
import { FilmType } from "./film/Editorial";
import { GrainPlate } from "./film/GrainPlate";
import { FilmScore } from "./film/Score";
import {
  CompositionTools,
  Desk,
  Library,
  MaskAndPixels,
  PhotoInspector,
} from "./film/PhotoScenes";
import {
  carouselPose,
  CarouselScene,
  DeliveryScene,
  MovingPicture,
  TimelineScene,
} from "./film/ProductionScenes";
import { AgentScene, SearchScene } from "./film/AssistanceScenes";

const MainMedia = ({ t, exposure }: { t: number; exposure: number }) => {
  const base = poseAt(t),
    p = carouselPose(t, base);
  const ripple = rippleAt(t);
  const initial = mix(t, 0.0, 2.2, 0.33, 1);
  const mask = windowAt(t, 26.1, 34.6);
  const design =
    mix(t, 35, 36.8) * (1 - mix(t, 55.8, 57)) +
    mix(t, 73, 74) * (1 - mix(t, 86.5, 88));
  const closing = mix(t, 117, 118.5);
  const poster = Math.min(1, design + closing);
  const video = windowAt(t, 55.9, 74.2);
  const diffusion = windowAt(t, 50, 54.5) * 0.26,
    grain = windowAt(t, 52, 55.5) * 0.1;
  // Native completion's RGB plate convergence acts on the photograph itself.
  // It resolves media; it is never a freestanding loading logo above the shot.
  const registration = Math.sin(Math.PI * mix(t, 13.8, 16.2));
  const heroOpacity = 1 - mix(t, 111.9, 112.7, 0, 0.04);
  const id = useId().replace(/:/g, "");
  const shape = contour(p.w, p.h, ripple, t / 11, initial);
  // When the photo develops into a composition, the SAME frame splits into
  // source/mask/type planes. Their camera orbit settles back into that frame.
  const separation =
    windowAt(t, 36.8, 40.5) * Math.sin(Math.PI * mix(t, 36.8, 40.5));
  return (
    <>
      {separation > 0.01 &&
        [2, 1].map((i) => (
          <div
            key={i}
            style={{
              ...abs,
              left: p.x + i * 25 * separation,
              top: p.y - i * 35 * separation,
              width: p.w,
              height: p.h,
              background: T.ink900,
              opacity: 0.6 * separation,
              transform: `perspective(1800px) rotateY(${p.ry - i * 8 * separation}deg) rotateZ(${p.r + i * 2 * separation}deg)`,
              clipPath: `path('${shape}')`,
            }}
          >
            {i === 2 ? (
              <Photo name="IMG_5071" />
            ) : (
              <svg
                width="100%"
                height="100%"
                viewBox="0 0 1200 1800"
                preserveAspectRatio="none"
              >
                <rect width={1200} height={1800} fill="black" />
                <path d={subjectPath} fill="white" />
              </svg>
            )}
          </div>
        ))}
      <div
        style={{
          ...abs,
          left: p.x,
          top: p.y,
          width: p.w,
          height: p.h,
          opacity: heroOpacity,
          transformOrigin: "50% 50%",
          transform: `perspective(2000px) rotateY(${p.ry}deg) rotateZ(${p.r}deg)`,
          filter: "drop-shadow(0 24px 40px #0005)",
        }}
      >
        <div
          style={{
            ...abs,
            inset: 0,
            clipPath: `path('${shape}')`,
            background: T.brandInk,
            overflow: "hidden",
          }}
        >
          <Photo
            name="IMG_5106"
            style={{
              filter: `brightness(${2 ** (exposure * 0.22)}) saturate(${1 + mix(t, 19, 23, 0, 0.06)})`,
              opacity: 1 - registration,
            }}
          />
          {registration > 0.001 &&
            [0, 1, 2].map((channel) => (
              <svg
                key={channel}
                width="100%"
                height="100%"
                viewBox="0 0 1200 1800"
                preserveAspectRatio="none"
                style={{
                  ...abs,
                  inset: 0,
                  opacity: registration,
                  mixBlendMode: "screen",
                  translate: `${(channel - 1) * p.w * 0.16 * registration}px ${(channel - 1) * p.h * 0.025 * registration}px`,
                }}
              >
                <defs>
                  <filter
                    id={`${id}channel${channel}`}
                    colorInterpolationFilters="sRGB"
                  >
                    <feColorMatrix
                      type="matrix"
                      values={
                        channel === 0
                          ? "1 0 0 0 0  0 0 0 0 0  0 0 0 0 0  0 0 0 1 0"
                          : channel === 1
                            ? "0 0 0 0 0  0 1 0 0 0  0 0 0 0 0  0 0 0 1 0"
                            : "0 0 0 0 0  0 0 0 0 0  0 0 1 0 0  0 0 0 1 0"
                      }
                    />
                  </filter>
                </defs>
                <image
                  href={asset("IMG_5106.jpg")}
                  width={1200}
                  height={1800}
                  filter={`url(#${id}channel${channel})`}
                />
              </svg>
            ))}
          {diffusion > 0.01 && (
            <Photo
              name="IMG_5106"
              style={{
                ...abs,
                inset: 0,
                filter: "blur(12px)",
                opacity: diffusion,
                mixBlendMode: "screen",
                maskImage: "linear-gradient(white,transparent 65%)",
              }}
            />
          )}
          {grain > 0.001 && <GrainPlate amount={grain} />}
          {poster > 0.001 && (
            <div style={{ ...abs, inset: 0, opacity: poster }}>
              <PosterArtwork t={t > 73 ? 48 : t} accent={brandAccentAt(t)} />
            </div>
          )}
          {mask > 0.001 && (
            <svg
              width="100%"
              height="100%"
              viewBox="0 0 1200 1800"
              preserveAspectRatio="none"
              style={{ ...abs, inset: 0 }}
            >
              <defs>
                <clipPath id={`${id}subject`}>
                  <path d={subjectPath} />
                </clipPath>
              </defs>
              <path d={subjectPath} fill={T.brandRed} opacity={0.2 * mask} />
              <path
                d={subjectPath}
                fill="none"
                stroke="white"
                strokeWidth={4}
                pathLength={1}
                strokeDasharray=".01 .01"
                strokeDashoffset={-t * 0.04}
                opacity={mask}
              />
              <circle
                cx={mix(t, 27.4, 29, 550, 690)}
                cy={mix(t, 27.4, 29, 920, 1200)}
                r={42}
                fill="none"
                stroke="white"
                strokeWidth={3}
                opacity={windowAt(t, 27, 29.5)}
              />
            </svg>
          )}
          {video > 0.001 && (
            <div style={{ ...abs, inset: 0, opacity: video }}>
              <MovingPicture t={t} />
            </div>
          )}
          {t < 35 && (
            <div
              style={{
                ...abs,
                bottom: 0,
                left: 0,
                right: 0,
                height: Math.max(26, p.w * 0.055),
                background: "rgba(16,16,16,.86)",
                fontFamily: "Menlo,monospace",
                fontSize: Math.max(9, p.w * 0.022),
                padding: "7px 10px",
              }}
            >
              IMG_5106.CR2
            </div>
          )}
        </div>
        {/* RGB paths ARE the refracting media aperture. They share its geometry,
       cropping and transformations, then align into the settled image edge. */}
        {ripple > 0.015 && (
          <svg
            width={p.w}
            height={p.h}
            style={{
              ...abs,
              inset: 0,
              overflow: "visible",
              mixBlendMode: "screen",
              opacity: Math.min(1, ripple * 1.6),
            }}
          >
            <path
              d={shape}
              fill="none"
              stroke="#ffffff"
              strokeWidth={1.2}
              opacity={0.55}
            />
            {[T.brandRed, T.brandGreen, T.brandBlue].map((c, i) => (
              <path
                key={c}
                d={contour(p.w, p.h, ripple, t / 11, initial, i - 1)}
                fill="none"
                stroke={c}
                strokeWidth={3.5}
                style={{
                  filter: "drop-shadow(0 0 3px currentColor)",
                  color: c,
                }}
              />
            ))}
          </svg>
        )}
      </div>
    </>
  );
};

const FilmPicture = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = frame / fps;
  const exposure = mix(t, 15.3, 17.1, 0, 0.65) - mix(t, 113.4, 114.7, 0, 0.4);
  const p = carouselPose(t, poseAt(t));
  // One camera rig follows native controls and the material, rather than
  // resetting a slideshow layout between chapter headings.
  const focus = windowAt(t, 17.7, 23.6);
  const typeFocus = windowAt(t, 43.5, 46.8) * 0.36;
  const motionFocus = windowAt(t, 64.8, 68.6) * 0.2;
  const agentFocus = windowAt(t, 113.1, 116.1) * 0.156;
  const totalFocus = focus + typeFocus + motionFocus + agentFocus;
  const s = 1 + 1.6 * totalFocus;
  const cameraX =
    -1652 * focus - 1956 * typeFocus - 2910 * motionFocus - 112 * agentFocus;
  const cameraY =
    (-806 - 584 * mix(t, 20, 21.1)) * focus -
    2756 * typeFocus -
    540 * motionFocus -
    860 * agentFocus;
  const backgroundLift = windowAt(t, 35.4, 40.5) * 0.12;
  return (
    <AbsoluteFill
      style={{
        background: T.ink950,
        color: T.brandPaper,
        fontFamily: appUiFont,
        overflow: "hidden",
      }}
    >
      <div
        style={{
          ...abs,
          inset: 0,
          background: `radial-gradient(ellipse at 50% 60%,rgba(50,130,255,${backgroundLift}),transparent 62%)`,
        }}
      />
      {/* All original SVG image resources are preloaded by Remotion's image loader. */}
      <div style={{ ...abs, width: 1, height: 1, opacity: 0 }}>
        {photos.map((name) => (
          <CanvasImage
            key={name}
            src={asset(`${name}.jpg`)}
            style={{ width: 1, height: 1 }}
          />
        ))}
      </div>
      <div
        style={{
          ...abs,
          left: 72,
          top: 82,
          zIndex: 40,
          opacity: mix(t, 4.8, 6) * (1 - mix(t, 116.6, 117.2)),
        }}
      >
        <AppWordmark width={230} />
      </div>
      <div
        style={{
          ...abs,
          right: 72,
          top: 104,
          zIndex: 40,
          opacity: mix(t, 4.8, 6) * (1 - mix(t, 116.6, 117.2)),
          fontFamily: "Menlo,monospace",
          fontSize: 15,
          color: T.textMuted,
        }}
      >
        PHOTO / DESIGN / VIDEO
      </div>
      <div
        style={{
          ...abs,
          left: 0,
          top: 0,
          right: 0,
          height: 500,
          zIndex: 20,
          pointerEvents: "none",
          background: `linear-gradient(${T.ink950} 0%,${T.ink950} 90%,transparent 100%)`,
          opacity: Math.min(1, totalFocus / 0.16),
        }}
      />
      <FilmType t={t} />
      <div
        style={{
          ...abs,
          inset: 0,
          transformOrigin: "0 0",
          scale: s,
          translate: `${cameraX}px ${cameraY}px`,
        }}
      >
        <Desk t={t} />
        <Desk t={t} closing />
        <Library t={t} />
        <MainMedia t={t} exposure={exposure} />
        <PhotoInspector t={t} exposure={exposure} />
        <MaskAndPixels t={t} pose={p} />
        <CompositionTools t={t} />
        <TimelineScene t={t} />
        <CarouselScene t={t} pose={p} />
        <DeliveryScene t={t} />
        <SearchScene t={t} />
        <AgentScene t={t} />
        {t > 117 && (
          <div
            style={{
              ...abs,
              left: 465,
              top: 865,
              width: 420,
              height: 237,
              rotate: "3deg",
              opacity: mix(t, 117, 118.3),
              overflow: "hidden",
            }}
          >
            <Photo name="IMG_1207-2-7" />
            <div
              style={{
                ...abs,
                left: 20,
                bottom: 24,
                fontWeight: 800,
                fontSize: 30,
              }}
            >
              THE HIGH ROUTE
            </div>
          </div>
        )}
      </div>
      {t > 13 && t < 26 && (
        <div
          style={{
            ...abs,
            left: 72,
            top: 1510,
            fontSize: 23,
            color: T.textMuted,
            opacity: 1 - Math.min(1, focus / 0.3),
          }}
        >
          Exposure · White balance · Curves · Color · Detail
          <br />
          <span style={{ fontFamily: "Menlo", fontSize: 18, lineHeight: 2.4 }}>
            SOURCE / 4000 × 6000 · sRGB output
          </span>
        </div>
      )}
      {t > 117 && (
        <div
          style={{
            ...abs,
            left: 72,
            top: 1640,
            fontSize: 28,
            opacity: mix(t, 117.4, 118.6),
          }}
        >
          <span
            style={{ display: "inline-flex", alignItems: "center", gap: 16 }}
          >
            <AppIcon name="check" size={24} color={T.opticalLight} />
            The shoot. The design. The film. Delivered.
          </span>
        </div>
      )}
    </AbsoluteFill>
  );
};

export const CohesiveTrailer = () => (
  <>
    <FilmPicture />
    <FilmScore />
  </>
);
