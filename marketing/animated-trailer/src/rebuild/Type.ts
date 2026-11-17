import { loadFont } from "@remotion/fonts";
import { staticFile } from "remotion";

loadFont({
  family: "Looksy Display",
  url: staticFile("fonts/rebuild/Anton.ttf"),
  weight: "400",
});
loadFont({
  family: "Looksy Serif",
  url: staticFile("fonts/rebuild/InstrumentSerif.ttf"),
  weight: "400",
});
loadFont({
  family: "Looksy Serif",
  url: staticFile("fonts/rebuild/InstrumentSerif-Italic.ttf"),
  weight: "400",
  style: "italic",
});
loadFont({
  family: "Looksy Sans",
  url: staticFile("fonts/rebuild/SpaceGrotesk.ttf"),
  weight: "100 900",
});
loadFont({
  family: "Looksy Mono",
  url: staticFile("fonts/rebuild/SpaceMono.ttf"),
  weight: "400",
});

export const font = {
  display: '"Looksy Display"',
  serif: '"Looksy Serif"',
  sans: '"Looksy Sans"',
  mono: '"Looksy Mono"',
};
