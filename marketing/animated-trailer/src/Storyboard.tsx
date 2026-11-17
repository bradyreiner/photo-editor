import { AbsoluteFill, staticFile } from "remotion";
import { MaskArt, palette } from "./StyleFrames";

const src = (name: string) => staticFile(`media/${name}`);
const Photo = ({
  name,
  x = 140,
  y = 52,
  width = 400,
  height = 265,
  transform,
}: {
  name: string;
  x?: number;
  y?: number;
  width?: number;
  height?: number;
  transform?: string;
}) => (
  <image
    href={src(name)}
    x={x}
    y={y}
    width={width}
    height={height}
    preserveAspectRatio="xMidYMid slice"
    transform={transform}
  />
);
const Arrow = ({ d }: { d: string }) => (
  <path
    d={d}
    fill="none"
    stroke={palette.green}
    strokeWidth="4"
    strokeDasharray="7 7"
    markerEnd="url(#arrow)"
  />
);
const Panels = () => (
  <g transform="translate(130 60) rotate(-8 200 140)">
    <rect
      x="-30"
      y="30"
      width="360"
      height="260"
      stroke={palette.blue}
      fill="none"
    />
    <rect x="-15" y="15" width="360" height="260" fill={palette.blue} />
    <Photo name="IMG_5106.jpg" x={0} y={0} width={360} height={260} />
  </g>
);

const Sketch = ({ shot }: { shot: number }) => (
  <svg
    viewBox="0 0 680 370"
    width="680"
    height="370"
    style={{ display: "block", background: "#171A1A" }}
  >
    <defs>
      <marker
        id="arrow"
        markerWidth="9"
        markerHeight="9"
        refX="5"
        refY="3"
        orient="auto"
      >
        <path
          d="M0 0L6 3 0 6"
          fill="none"
          stroke={palette.green}
          strokeWidth="1.5"
        />
      </marker>
    </defs>
    {shot === 1 && (
      <>
        <image
          href={src("looksy-wordmark-dark.png")}
          x="32"
          y="22"
          width="150"
          height="50"
        />
        <text x="32" y="106" fill="white" fontSize="24">
          Photo, video and design.
        </text>
        <svg x="53" y="130" width="160" height="225" viewBox="0 0 1200 1800">
          <MaskArt />
        </svg>
        <Photo
          name="IMG_1207-2-7.jpg"
          x={235}
          y={135}
          width={285}
          height={160}
        />
        <Photo name="IMG_4870.jpg" x={532} y={135} width={108} height={160} />
        <Arrow d="M250 330H635" />
        <text x="320" y="73" fill={palette.green} fontSize="19">
          SOURCE → FINISHED WORK
        </text>
      </>
    )}
    {shot === 2 && (
      <>
        <Photo
          name="IMG_5071.jpg"
          x={105}
          y={46}
          width={470}
          height={287}
          transform="rotate(-7 340 185)"
        />
        <rect
          x="284"
          y="129"
          width="91"
          height="91"
          fill="none"
          stroke="white"
          strokeWidth="3"
        />
        <Arrow d="M580 306C486 321 437 242 377 203" />
        <text x="30" y="65" fill="white" fontSize="24">
          .CR2
        </text>
        <text x="433" y="348" fill="white" fontSize="19">
          PUSH INTO DETAIL
        </text>
      </>
    )}
    {shot === 3 && (
      <>
        <Photo name="IMG_5071.jpg" x={40} y={50} width={362} height={245} />
        <rect x="393" y="85" width="234" height="245" fill="#F2F1ED" />
        <path
          d="M414 301L606 110M414 250H606M414 201H606M462 110V301M510 110V301M558 110V301"
          stroke="#CCD0C5"
        />
        <path
          d="M414 300C475 300 468 209 514 183S567 153 606 109"
          fill="none"
          stroke={palette.blue}
          strokeWidth="5"
        />
        <circle cx="514" cy="183" r="9" fill={palette.blue} />
        <Arrow d="M49 341C247 385 358 334 418 283" />
        <text x="40" y="28" fill="white" fontSize="19">
          RIBBON BECOMES CURVE
        </text>
      </>
    )}
    {shot === 4 && (
      <>
        <Panels />
        <path
          d="M322 107Q349 97 359 119L369 150 400 222 389 262 390 300 342 302 322 242 306 211 311 167Z"
          fill="none"
          stroke={palette.green}
          strokeWidth="3"
          strokeDasharray="6 5"
        />
        <Arrow d="M74 278C26 85 535 12 594 175C621 254 527 321 488 302" />
        <text x="440" y="346" fill="white" fontSize="19">
          ORBIT FLAT LAYERS
        </text>
      </>
    )}
    {shot === 5 && (
      <>
        <svg x="220" y="21" width="240" height="327" viewBox="0 0 1200 1800">
          <MaskArt />
        </svg>
        <path
          d="M283 285C439 227 480 232 516 256S452 339 450 288"
          stroke={palette.red}
          strokeWidth="16"
          strokeLinecap="round"
          fill="none"
        />
        <Arrow d="M75 177H208" />
        <Arrow d="M558 180H471" />
        <text x="35" y="330" fill="white" fontSize="19">
          FACE-ON / HOLD THE RESULT
        </text>
      </>
    )}
    {shot === 6 && (
      <>
        <Photo
          name="IMG_1207-2-7.jpg"
          x={149}
          y={20}
          width={377}
          height={213}
          transform="rotate(-5 340 125)"
        />
        <g transform="translate(40 232) rotate(5 300 50)">
          <rect width="600" height="91" fill="#F2F1ED" />
          <Photo
            name="IMG_1207-2-7.jpg"
            x={10}
            y={10}
            width={240}
            height={42}
          />
          <Photo
            name="IMG_1268-2-7.jpg"
            x={254}
            y={10}
            width={330}
            height={42}
          />
          <rect x="10" y="59" width="574" height="19" fill={palette.green} />
          <path d="M310 -15V100" stroke={palette.red} strokeWidth="4" />
        </g>
        <Arrow d="M591 329C527 346 459 306 392 171" />
        <text x="45" y="52" fill="white" fontSize="19">
          DIVE THROUGH PLAYBACK
        </text>
      </>
    )}
    {shot === 7 && (
      <>
        <g transform="translate(55 60) rotate(-4 270 120)">
          <rect width="170" height="245" fill={palette.blue} />
          <text x="20" y="64" fontSize="50" fontWeight="800" fill="white">
            NEPAL
          </text>
          <Photo name="IMG_4870.jpg" x={185} y={0} width={170} height={245} />
          <Photo name="IMG_4969.jpg" x={370} y={0} width={170} height={245} />
        </g>
        <Arrow d="M45 338C240 375 441 347 617 308" />
        <text x="419" y="35" fill="white" fontSize="19">
          TRACK → PULL BACK
        </text>
      </>
    )}
    {shot === 8 && (
      <>
        <Photo name="IMG_5106.jpg" x={70} y={61} width={169} height={250} />
        <Photo
          name="IMG_1207-2-7.jpg"
          x={280}
          y={114}
          width={240}
          height={135}
        />
        <g fill="white" fontSize="20">
          <text x="78" y="340">
            PHOTO / sRGB
          </text>
          <text x="290" y="278">
            VIDEO / DIMENSIONS
          </text>
        </g>
        <Arrow d="M245 83C355 28 485 42 573 147" />
        <path
          d="M564 242L585 263 626 213"
          stroke={palette.green}
          strokeWidth="6"
          fill="none"
        />
      </>
    )}
    {shot === 9 && (
      <>
        <Photo
          name="IMG_5071.jpg"
          x={35}
          y={85}
          width={185}
          height={125}
          transform="rotate(-12 130 150)"
        />
        <Photo
          name="IMG_1207-2-7.jpg"
          x={454}
          y={200}
          width={185}
          height={104}
          transform="rotate(10 540 245)"
        />
        <image
          href={src("looksy-wordmark-dark.png")}
          x="240"
          y="126"
          width="204"
          height="92"
          preserveAspectRatio="xMidYMid meet"
        />
        <path
          d="M77 296C140 363 220 345 247 278M438 72C508 6 610 24 635 97"
          stroke={palette.blue}
          strokeWidth="4"
          fill="none"
        />
        <text x="340" y="248" textAnchor="middle" fontSize="21" fill="white">
          Photo. Video. Design.
        </text>
      </>
    )}
  </svg>
);

const Shot = ({
  shot,
  time,
  title,
  motion,
  x,
  y,
}: {
  shot: number;
  time: string;
  title: string;
  motion: string;
  x: number;
  y: number;
}) => (
  <div style={{ position: "absolute", left: x, top: y, width: 680 }}>
    <div
      style={{
        fontSize: 22,
        letterSpacing: 1,
        marginBottom: 16,
        display: "flex",
        justifyContent: "space-between",
        color: "#797C70",
      }}
    >
      <span>0{shot}</span>
      <span>{time}</span>
    </div>
    <Sketch shot={shot} />
    <div
      style={{
        fontSize: 31,
        fontWeight: 800,
        marginTop: 22,
        letterSpacing: -0.8,
      }}
    >
      {title}
    </div>
    <div
      style={{
        fontSize: 25,
        lineHeight: 1.38,
        marginTop: 12,
        color: "#63665E",
      }}
    >
      {motion}
    </div>
  </div>
);

export const Storyboard = () => (
  <AbsoluteFill
    style={{
      fontFamily: "Manrope",
      background: palette.paper,
      color: palette.ink,
    }}
  >
    <div
      style={{
        position: "absolute",
        left: 88,
        top: 65,
        fontSize: 56,
        fontWeight: 800,
        letterSpacing: -2,
      }}
    >
      Looksy / the camera follows the craft.
    </div>
    <div
      style={{
        position: "absolute",
        left: 88,
        top: 145,
        fontSize: 28,
        color: "#74766D",
      }}
    >
      49-second text-only trailer / nine beats / green arrows indicate camera
      travel
    </div>
    <Shot
      shot={1}
      time="00:00—00:06.7"
      title="Introducing Looksy. One workspace."
      motion="Introduce photo, video and design with finished work. Align the outputs to establish the source-to-finish workflow."
      x={88}
      y={240}
    />
    <Shot
      shot={2}
      time="00:06.7—00:12"
      title="Keep the detail."
      motion="The file lands with weight. Push into a detail region, then return to the full photograph."
      x={860}
      y={240}
    />
    <Shot
      shot={3}
      time="00:12—00:17"
      title="Color with precision."
      motion="The light contour tightens into a tone curve. Hold the camera while image and control respond."
      x={1632}
      y={240}
    />
    <Shot
      shot={4}
      time="00:17—00:21"
      title="Built to be revised."
      motion="Separate the relevant layers with a short shallow orbit. Change a mask and show its effect within the composition."
      x={88}
      y={890}
    />
    <Shot
      shot={5}
      time="00:21—00:24"
      title="Bring the composition together."
      motion="Type slips behind the subject. A brush adds an accent; settle face-on for a clean finished reveal."
      x={860}
      y={890}
    />
    <Shot
      shot={6}
      time="00:24—00:32"
      title="From first cut to final frame."
      motion="The brush straightens into a playhead. Clips trim to the beat; dive through the playback frame."
      x={1632}
      y={890}
    />
    <Shot
      shot={7}
      time="00:32—00:38"
      title="One design. Every panel."
      motion="Playback opens into a layout space. Panels unfold; track alongside them and pull back to reveal the set."
      x={88}
      y={1540}
    />
    <Shot
      shot={8}
      time="00:38—00:45"
      title="Your dimensions. Your output."
      motion="Photo, video, and carousel resolve into their deliverables. Show the matching export controls clearly."
      x={860}
      y={1540}
    />
    <Shot
      shot={9}
      time="00:45—00:49"
      title="Looksy. Photo. Video. Design."
      motion="Outputs settle around the wordmark. Colored paths return to the mark, leaving a calm final hold."
      x={1632}
      y={1540}
    />
    <div
      style={{
        position: "absolute",
        left: 88,
        bottom: 50,
        fontSize: 22,
        color: "#797C70",
      }}
    >
      ROUGH STAGING / NOT FINAL ART • WIDE PANELS SHOW OVERSCAN AND CAMERA
      ROUTES • TARGET DELIVERY: 9:16 • NO VOICEOVER
    </div>
  </AbsoluteFill>
);
