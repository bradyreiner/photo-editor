import {
  AbsoluteFill,
  Sequence,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { DeskShot, IdentityShot, LibraryShot, RawShot } from "./Opening";
import { A, C, land } from "./design";
const DeskScene = ({ rate = 1 }: { rate?: number }) => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <DeskShot t={(f / fps) * rate} />;
};
const IdentityScene = ({ rate = 1 }: { rate?: number }) => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return (
    <div style={{ ...A, inset: 0, opacity: land((f / fps) * rate, 0, 0.45) }}>
      <IdentityShot t={(f / fps) * rate} />
    </div>
  );
};
const LibraryScene = ({ rate = 1 }: { rate?: number }) => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <LibraryShot t={(f / fps) * rate} />;
};
const RawScene = ({ rate = 1 }: { rate?: number }) => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <RawShot t={(f / fps) * rate} />;
};
export const LaunchOpening = ({ paced = false }: { paced?: boolean }) => {
  const { fps } = useVideoConfig();
  return (
    <AbsoluteFill style={{ background: C.brandInk }}>
      <Sequence
        name="The working desk / camera into the shoot"
        durationInFrames={Math.round((paced ? 4 : 4.9) * fps)}
      >
        <DeskScene rate={paced ? 4.9 / 4 : 1} />
      </Sequence>
      <Sequence
        name="Additive light / original Looksy identity"
        from={Math.round((paced ? 3.6 : 4.4) * fps)}
        durationInFrames={Math.round((paced ? 2.4 : 3.8) * fps)}
      >
        <IdentityScene rate={paced ? 3.8 / 2.4 : 1} />
      </Sequence>
      <Sequence
        name="Import / select / sync / follow selected RAW"
        from={Math.round((paced ? 6 : 8.2) * fps)}
        durationInFrames={Math.round((paced ? 4 : 5) * fps)}
      >
        <LibraryScene rate={paced ? 5 / 4 : 1} />
      </Sequence>
      <Sequence
        name="RAW development / native controls / custom curve engine"
        from={Math.round((paced ? 10 : 13.2) * fps)}
        durationInFrames={Math.round((paced ? 8 : 12.8) * fps)}
      >
        <RawScene rate={paced ? 12.8 / 8 : 1} />
      </Sequence>
    </AbsoluteFill>
  );
};
