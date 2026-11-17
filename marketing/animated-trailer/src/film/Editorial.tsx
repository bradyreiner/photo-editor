import { AppWordmark } from "../app-kit/AppElements";
import { abs, clamp, enter, mix, T } from "./system";
import { interpolate } from "remotion";

export const filmChapters = [
  {
    start: 0,
    end: 6,
    title: ["Photo. Design. Video."],
    sub: "Professional tools. One creative workspace.",
    brand: true,
  },
  {
    start: 6,
    end: 13,
    title: ["Start with", "the whole shoot."],
    sub: "Import, cull and sync a working collection.",
  },
  {
    start: 13,
    end: 26,
    title: ["A custom engine.", "For your RAW files."],
    sub: "Exposure, tone, color and detail. At native resolution.",
  },
  {
    start: 26,
    end: 35,
    title: ["Every pixel.", "Every edge."],
    sub: "Select. Refine. Paint. Erase.",
  },
  {
    start: 35,
    end: 49,
    title: ["Beyond", "the photograph."],
    sub: "Image layers, editable type, shapes, masks and paint.",
  },
  {
    start: 49,
    end: 56,
    title: ["Shape", "the finish."],
    sub: "Optical effects. Editable stacks. Local control.",
  },
  {
    start: 56,
    end: 73,
    title: ["A complete", "production timeline."],
    sub: "4K. Layered picture. Keyframed motion. Sound.",
  },
  {
    start: 73,
    end: 87,
    title: ["A story across", "every frame."],
    sub: "Photo and video carousels, designed as one.",
  },
  {
    start: 87,
    end: 94,
    title: ["Full resolution.", "Ready to deliver."],
    sub: "Your dimensions. Your formats. Your output color.",
  },
  {
    start: 94,
    end: 105,
    title: ["Find the shot.", "Describe the scene."],
    sub: "Local visual search, with results you can review.",
  },
  {
    start: 105,
    end: 113,
    title: ["Delegate the batch.", "Keep the decisions."],
    sub: "Optional local agent. Native tools. Editable results.",
  },
  {
    start: 113,
    end: 117,
    title: ["Every edit.", "Still yours."],
    sub: "Adjust it. Revisit the mask. Undo. Keep going.",
  },
  {
    start: 117,
    end: 120,
    title: ["Your entire creative workflow."],
    sub: "Photo. Design. Video.",
    brand: true,
  },
] as const;

// Exit finishes BEFORE the next entry. No two statements occupy the same
// reading area. Each line has its own aperture and a short, staggered landing.
export const FilmType = ({ t }: { t: number }) => (
  <>
    {filmChapters.map((chapter, index) => {
      if (t < chapter.start || t >= chapter.end) return null;
      const isBrand = "brand" in chapter && chapter.brand;
      const local = t - chapter.start;
      const exit =
        index === filmChapters.length - 1
          ? 0
          : mix(t, chapter.end - 0.34, chapter.end);
      const appear =
        index === 0 ? enter(local, 0, 0.55) : enter(local, 0.07, 0.55);
      return (
        <div
          key={chapter.start}
          style={{
            ...abs,
            left: 72,
            top: 221,
            width: 936,
            zIndex: 30,
            opacity: 1 - exit,
            translate: `0 ${-exit * 24}px`,
          }}
        >
          {isBrand && (
            <div
              style={{
                height: 137,
                opacity: appear,
                clipPath: `inset(0 ${(1 - appear) * 100}% 0 0)`,
                translate: `0 ${(1 - appear) * 22}px`,
              }}
            >
              <AppWordmark width={index === 0 ? 475 : 465} />
            </div>
          )}
          <div
            style={{
              fontSize: isBrand ? 57 : index === 10 ? 72 : 79,
              fontWeight: 650,
              letterSpacing: isBrand ? -2.4 : -3.6,
              lineHeight: 1.04,
            }}
          >
            {chapter.title.map((line, i) => {
              const landing =
                index === 0
                  ? enter(local, 0.14 + i * 0.08, 0.68 + i * 0.08)
                  : enter(local, 0.08 + i * 0.075, 0.55 + i * 0.075);
              return (
                <div
                  key={line}
                  style={{
                    overflow: "hidden",
                    paddingBottom: 5,
                    marginBottom: -5,
                  }}
                >
                  <div
                    style={{
                      translate: `0 ${interpolate(landing, [0, 1], [104, 0], clamp)}%`,
                      opacity: Math.min(1, landing * 2),
                    }}
                  >
                    {line}
                  </div>
                </div>
              );
            })}
          </div>
          <div
            style={{
              fontSize: 29,
              color: T.textMuted,
              marginTop: isBrand ? 20 : 23,
              lineHeight: 1.35,
              opacity: enter(local, isBrand ? 0.4 : 0.3, 0.8),
              translate: `0 ${enter(local, 0.3, 0.8, 12, 0)}px`,
            }}
          >
            {chapter.sub}
          </div>
        </div>
      );
    })}
  </>
);
