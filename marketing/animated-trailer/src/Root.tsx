import { PanoramaArtwork } from "./desktop-launch/Panorama";
import { CreativeFilm } from "./creative-launch/CreativeFilm";
import { DesktopActionFilm } from "./desktop-launch/ActionFilm";
import { NativeOpening } from "./desktop-launch/NativeOpening";
import { FullStoryboard } from "./rebuild/FullStoryboard";
import { MotionRebuild } from "./rebuild/MotionProof";
import { DesignBoard } from "./rebuild/Artwork";
import { StudioFilm } from "./studio-film/StudioFilm";
import { Composition, Folder, Still } from "remotion";
import { ColorFrame, MaskFrame, VideoFrame, StyleBoard } from "./StyleFrames";
import { Storyboard } from "./Storyboard";
import { MotionStudy } from "./MotionStudy";
import { OpeningStudy } from "./OpeningStudy";
import { DeskOpening } from "./DeskOpening";
import { CommercialBoard } from "./CommercialBoard";
import { FullTrailer } from "./FullTrailer";
import { AppElementsStudy } from "./AppElementsStudy";
import { LightFlowStudy } from "./LightFlowStudy";
import { CohesiveTrailer } from "./CohesiveTrailer";
import { LaunchOpening } from "./launch/LaunchOpening";
import { CampaignArtwork } from "./launch/Artwork";
import { ImageMaking } from "./launch/ImageMaking";
import { LaunchFilm } from "./launch/LaunchFilm";
import { LaunchScore } from "./launch/LaunchScore";

import { CampaignFilm } from "./campaign/Film";
import { FlowFilm } from "./flow/FlowFilm";
import { PulseFilm } from "./pulse/PulseFilm";
import { RefinedFilm } from "./refine/Film";
import { CampaignFilm as PolishedFilm } from "./polish/Film";

export const RemotionRoot: React.FC = () => {
  return (
    <>
      <Composition
        id="Looksy-Creative-Launch-Landscape"
        component={CreativeFilm}
        width={1920}
        height={1080}
        fps={30}
        durationInFrames={8940}
        defaultProps={{ withMusic: true }}
      />
      <Composition
        id="Looksy-Creative-Launch-Portrait"
        component={CreativeFilm}
        width={1080}
        height={1920}
        fps={30}
        durationInFrames={8940}
        defaultProps={{ withMusic: true }}
      />
      <Composition
        id="Looksy-Desktop-Action-Landscape"
        component={DesktopActionFilm}
        width={1920}
        height={1080}
        fps={30}
        durationInFrames={3120}
      />
      <Composition
        id="Looksy-Desktop-Action-Portrait"
        component={DesktopActionFilm}
        width={1080}
        height={1920}
        fps={30}
        durationInFrames={3120}
      />
      <Still
        id="Looksy-Three-Canvas-Panorama"
        component={PanoramaArtwork}
        width={3240}
        height={1350}
      />
      <Composition
        id="Looksy-Desktop-Opening-Landscape"
        component={NativeOpening}
        width={1920}
        height={1080}
        fps={60}
        durationInFrames={1920}
      />
      <Composition
        id="Looksy-Desktop-Opening-Portrait"
        component={NativeOpening}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={1920}
      />
      <Composition
        id="Looksy-Full-Storyboard"
        component={FullStoryboard}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={12075}
      />
      <Composition
        id="Looksy-Motion-Rebuild"
        component={MotionRebuild}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={2520}
      />
      <Still
        id="Looksy-New-Design-Board"
        component={DesignBoard}
        width={2800}
        height={1720}
      />
      <Composition
        id="Looksy-Studio-Film"
        component={StudioFilm}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={11520}
      />

      <Composition
        id="Looksy-Launch-Flow-v7"
        component={FlowFilm}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={8280}
        defaultProps={{ withMusic: true }}
      />
      <Composition
        id="Looksy-Launch-Motion-v6"
        component={PulseFilm}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={7560}
        defaultProps={{ withMusic: true }}
      />
      <Composition
        id="Looksy-Launch-Refine"
        component={RefinedFilm}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={8520}
        defaultProps={{ withMusic: true }}
      />
      <Composition
        id="Looksy-Launch-Polish"
        component={PolishedFilm}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={9480}
      />
      <Composition
        id="Looksy-Launch-Workflows"
        component={CampaignFilm}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={9240}
      />
      <Composition
        id="Launch-Soundtrack"
        component={LaunchScore}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={7440}
      />
      <Composition
        id="Looksy-Launch-Rebuild"
        component={LaunchFilm}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={7440}
      />
      <Composition
        id="Launch-Rebuild-Photo-Design"
        component={ImageMaking}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={1740}
      />
      <Still
        id="Launch-Campaign-Artwork"
        component={CampaignArtwork}
        width={1200}
        height={1500}
      />
      <Composition
        id="Launch-Rebuild-Opening"
        component={LaunchOpening}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={1560}
      />
      <Composition
        id="Looksy-Trailer"
        component={CohesiveTrailer}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={7200}
      />
      <Composition
        id="Light-to-Library"
        component={LightFlowStudy}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={840}
      />
      <Composition
        id="App-Elements-Study"
        component={AppElementsStudy}
        width={1600}
        height={1000}
        fps={60}
        durationInFrames={720}
      />
      <Still
        id="Delivery-Storyboard"
        component={CommercialBoard}
        width={3200}
        height={2100}
      />
      <Composition
        id="After-the-Shoot"
        component={FullTrailer}
        width={1080}
        height={1920}
        fps={60}
        durationInFrames={4320}
      />
      <Folder name="Motion-Experiments">
        <Composition
          id="Desk-Opening-Test"
          component={DeskOpening}
          width={1080}
          height={1920}
          fps={60}
          durationInFrames={1170}
          defaultProps={{ mode: "manual" }}
        />
        <Composition
          id="Desk-and-Agent-Experiment"
          component={DeskOpening}
          width={1080}
          height={1920}
          fps={60}
          durationInFrames={1620}
          defaultProps={{ mode: "agent" }}
        />
        <Composition
          id="Introducing-Looksy"
          component={OpeningStudy}
          width={1080}
          height={1920}
          fps={60}
          durationInFrames={720}
        />
        <Composition
          id="Curve-to-Mask"
          component={MotionStudy}
          width={1080}
          height={1920}
          fps={60}
          durationInFrames={720}
        />
        <Still
          id="Storyboard"
          component={Storyboard}
          width={2400}
          height={2220}
        />
      </Folder>
      <Still
        id="Style-Board"
        component={StyleBoard}
        width={2160}
        height={1640}
      />
      <Folder name="Style-Frames">
        <Still
          id="Color-Precision"
          component={ColorFrame}
          width={1080}
          height={1920}
        />
        <Still
          id="Editable-Layers"
          component={MaskFrame}
          width={1080}
          height={1920}
        />
        <Still
          id="Timeline-Carousel"
          component={VideoFrame}
          width={1080}
          height={1920}
        />
      </Folder>
    </>
  );
};
