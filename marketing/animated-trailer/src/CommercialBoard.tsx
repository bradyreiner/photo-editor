import { AbsoluteFill, staticFile } from "remotion";
import { MaskArt, palette } from "./StyleFrames";

const media = (name: string) => staticFile(`media/${name}`);
const Picture = ({
  name,
  x,
  y,
  w,
  h,
}: {
  name: string;
  x: number;
  y: number;
  w: number;
  h: number;
}) => (
  <image
    href={media(`${name}.jpg`)}
    x={x}
    y={y}
    width={w}
    height={h}
    preserveAspectRatio="xMidYMid slice"
  />
);
const Arrow = ({ d }: { d: string }) => (
  <path
    d={d}
    stroke={palette.blue}
    strokeWidth="4"
    strokeDasharray="8 7"
    fill="none"
    markerEnd="url(#commercial-arrow)"
  />
);

const MiniDesk = ({ finished = false }: { finished?: boolean }) => (
  <>
    <path d="M-10 239L558 194L726 315L125 374Z" fill="#D6D8CD" />
    <path d="M315 188H365L381 258H295Z" fill={palette.ink} />
    <rect x="183" y="19" width="330" height="203" rx="10" fill={palette.ink} />
    <rect x="196" y="32" width="304" height="174" fill={palette.paper} />
    {finished ? (
      <>
        <Picture name="IMG_5106" x={211} y={43} w={92} h={150} />
        <Picture name="IMG_1207-2-7" x={316} y={45} w={171} h={96} />
        <text x="318" y="175" fontSize="19" fill={palette.blue}>
          DELIVERY COMPLETE
        </text>
      </>
    ) : (
      <>
        <image
          href={media("looksy-wordmark-light.png")}
          x="220"
          y="62"
          width="154"
          height="54"
        />
        <text x="220" y="150" fontSize="21" fill={palette.ink}>
          NEPAL / SHOOT LIBRARY
        </text>
      </>
    )}
    <path
      d="M295 272L434 259L488 294L344 310Z"
      fill="white"
      stroke={palette.ink}
      strokeWidth="2"
    />
    <g transform="translate(70 250) rotate(-9)">
      <rect width="121" height="74" rx="10" fill={palette.ink} />
      <circle cx="61" cy="39" r="35" fill="#4B5543" />
      <circle cx="61" cy="39" r="23" fill={palette.blue} />
      <path d="M27 0L42 -13H78L92 0" fill={palette.ink} />
    </g>
    <g transform="translate(545 227) rotate(9)">
      <rect width="69" height="95" rx="9" fill={palette.blue} />
      <text x="10" y="35" fill="white" fontSize="14">
        NEPAL
      </text>
      <circle cx="53" cy="78" r="3" fill={palette.green} />
    </g>
    <path
      d="M579 232C655 148 522 156 507 160"
      fill="none"
      stroke={palette.ink}
      strokeWidth="5"
    />
    {finished ? (
      <path
        d="M548 117L573 142 620 89"
        fill="none"
        stroke={palette.blue}
        strokeWidth="7"
      />
    ) : (
      <Arrow d="M583 209C610 100 531 88 485 109" />
    )}
  </>
);

const StoryImage = ({ shot }: { shot: number }) => (
  <svg
    width="700"
    height="330"
    viewBox="0 0 700 330"
    style={{ display: "block", background: "#E4E7DC", fontFamily: "Manrope" }}
  >
    <defs>
      <marker
        id="commercial-arrow"
        markerWidth="9"
        markerHeight="9"
        refX="5"
        refY="3"
        orient="auto"
      >
        <path
          d="M0 0L6 3 0 6"
          fill="none"
          stroke={palette.blue}
          strokeWidth="1.5"
        />
      </marker>
    </defs>
    {shot === 1 && <MiniDesk />}
    {shot === 2 && (
      <>
        {[
          "IMG_5106",
          "IMG_5071",
          "IMG_4870",
          "IMG_4969",
          "IMG_5103",
          "IMG_5111",
        ].map((name, i) => (
          <Picture
            key={name}
            name={name}
            x={82 + (i % 3) * 182}
            y={25 + Math.floor(i / 3) * 145}
            w={165}
            h={125}
          />
        ))}
        <Arrow d="M19 303C44 201 58 107 82 76" />
        <text x="95" y="322" fill={palette.ink} fontSize="19">
          FILES BECOME THE CONTACT SHEET
        </text>
      </>
    )}
    {shot === 3 && (
      <>
        <rect x="62" y="25" width="575" height="49" rx="6" fill="white" />
        <text x="80" y="57" fontSize="21" fill={palette.ink}>
          ⌕ yellow jacket portraits
        </text>
        {["IMG_5106", "IMG_5103", "IMG_5111"].map((name, i) => (
          <g key={name}>
            <Picture name={name} x={80 + i * 192} y={99} w={170} h={215} />
            <path
              d={`M${215 + i * 192} 118l8 8 16-18`}
              stroke={palette.blue}
              strokeWidth="4"
              fill="none"
            />
          </g>
        ))}
      </>
    )}
    {shot === 4 && (
      <>
        <path
          d="M201 67C580 -30 655 227 477 264S70 240 201 67Z"
          fill="none"
          stroke={palette.blue}
          strokeWidth="4"
          strokeDasharray="10 8"
        />
        <rect
          x="235"
          y="69"
          width="238"
          height="190"
          rx="14"
          fill={palette.ink}
        />
        <rect
          x="264"
          y="100"
          width="70"
          height="70"
          rx="5"
          fill={palette.blue}
        />
        <text x="355" y="128" fill="white" fontSize="22">
          LOCAL
        </text>
        <text x="355" y="157" fill="white" fontSize="22">
          MODEL
        </text>
        <text x="263" y="216" fill={palette.green} fontSize="20">
          ON THIS COMPUTER
        </text>
        <Picture name="IMG_5106" x={46} y={77} w={132} h={181} />
        <text x="47" y="304" fill={palette.ink} fontSize="20">
          LOCAL INFERENCE LOOP
        </text>
      </>
    )}
    {shot === 5 && (
      <>
        <Picture name="IMG_5106" x={45} y={44} w={166} h={247} />
        <rect
          x="243"
          y="44"
          width="415"
          height="247"
          fill={palette.ink}
          rx="10"
        />
        <text x="272" y="87" fill="white" fontWeight="800" fontSize="26">
          Review the sample
        </text>
        <text x="272" y="132" fill="#C0C8B5" fontSize="22">
          Consistent look.
        </text>
        <text x="272" y="166" fill="#C0C8B5" fontSize="22">
          Keep adjustments editable.
        </text>
        <rect
          x="271"
          y="209"
          width="349"
          height="49"
          fill={palette.blue}
          rx="6"
        />
        <text x="290" y="241" fill="white" fontSize="22">
          Apply to selected batch →
        </text>
      </>
    )}
    {shot === 6 && (
      <>
        <Picture name="IMG_5106" x={42} y={25} w={182} h={279} />
        <rect
          x="255"
          y="25"
          width="398"
          height="279"
          fill={palette.ink}
          rx="8"
        />
        <text x="284" y="68" fill="white" fontSize="25" fontWeight="800">
          Layers + history
        </text>
        {["Original", "Agent draft", "Exposure adjustment"].map((x, i) => (
          <text
            key={x}
            x="285"
            y={119 + i * 45}
            fill={i === 2 ? palette.green : "#D1D9C7"}
            fontSize="23"
          >
            {i === 2 ? "● " : "○ "}
            {x}
          </text>
        ))}
        <path d="M285 272H616" stroke="#747D68" strokeWidth="3" />
        <circle cx="508" cy="272" r="9" fill={palette.green} />
      </>
    )}
    {shot === 7 && (
      <>
        <Picture name="IMG_5071" x={20} y={44} w={385} h={250} />
        <rect x="425" y="25" width="251" height="283" fill={palette.ink} />
        <text x="447" y="65" fill="white" fontSize="24">
          Photo controls
        </text>
        {["Exposure / WB", "Tones / color", "Detail / masks"].map((x, i) => (
          <g key={x}>
            <text x="447" y={113 + i * 67} fill="#C0C8B5" fontSize="20">
              {x}
            </text>
            <path
              d={`M447 ${132 + i * 67}H651`}
              stroke="#5E6953"
              strokeWidth="3"
            />
            <circle
              cx={515 + i * 24}
              cy={132 + i * 67}
              r="7"
              fill={palette.green}
            />
          </g>
        ))}
      </>
    )}
    {shot === 8 && (
      <>
        <rect
          x="205"
          y="40"
          width="207"
          height="274"
          fill={palette.blue}
          transform="rotate(-9 310 180)"
        />
        <svg x="239" y="20" width="207" height="290" viewBox="0 0 1200 1800">
          <MaskArt />
        </svg>
        <path
          d="M101 247C326 161 501 165 584 228S506 340 516 265"
          stroke={palette.red}
          strokeWidth="17"
          fill="none"
          strokeLinecap="round"
        />
        <Arrow d="M90 94C128 9 564 9 593 150" />
      </>
    )}
    {shot === 9 && (
      <>
        <Picture name="IMG_1207-2-7" x={203} y={12} w={292} h={165} />
        <rect x="28" y="195" width="644" height="113" fill={palette.ink} />
        {[0, 1, 2].map((i) => (
          <Picture
            key={i}
            name={i === 1 ? "IMG_1268-2-7" : "IMG_1207-2-7"}
            x={44 + i * 205}
            y={206}
            w={196}
            h={43}
          />
        ))}
        <rect x="171" y="255" width="209" height="17" fill={palette.blue} />
        <text x="189" y="269" fill="white" fontSize="12">
          TITLE / ANIMATION
        </text>
        <rect x="44" y="280" width="608" height="17" fill={palette.green} />
        <path d="M402 183V318" stroke={palette.red} strokeWidth="3" />
        <text x="31" y="179" fill={palette.ink} fontSize="18">
          PICTURE / TITLES / EFFECTS / COLOR / AUDIO
        </text>
      </>
    )}
    {shot === 10 && (
      <>
        <rect x="43" y="35" width="185" height="260" fill={palette.blue} />
        <text x="65" y="96" fontSize="45" fill="white" fontWeight="800">
          NEPAL
        </text>
        <Picture name="IMG_4870" x={246} y={35} w={185} h={260} />
        <Picture name="IMG_4969" x={449} y={35} w={185} h={260} />
        <Arrow d="M30 316H649" />
      </>
    )}
    {shot === 11 && (
      <>
        <Picture name="IMG_5106" x={34} y={40} w={150} h={226} />
        <Picture name="IMG_1207-2-7" x={210} y={40} w={259} h={146} />
        <rect x="493" y="40" width="173" height="226" fill={palette.ink} />
        <text x="513" y="83" fontSize="23" fill="white">
          DELIVERY
        </text>
        <text x="513" y="130" fontSize="20" fill={palette.green}>
          Dimensions
        </text>
        <text x="513" y="167" fontSize="20" fill={palette.green}>
          Formats
        </text>
        <text x="513" y="204" fontSize="20" fill={palette.green}>
          sRGB
        </text>
        <Arrow d="M218 237H458" />
        <text x="33" y="310" fontSize="19" fill={palette.ink}>
          PHOTO / FILM / CAROUSEL
        </text>
      </>
    )}
    {shot === 12 && <MiniDesk finished />}
  </svg>
);

const Beat = ({
  shot,
  art,
  time,
  title,
  why,
  x,
  y,
}: {
  shot: number;
  art?: number;
  time: string;
  title: string;
  why: string;
  x: number;
  y: number;
}) => (
  <div style={{ position: "absolute", left: x, top: y, width: 700 }}>
    <div
      style={{
        fontSize: 22,
        color: "#717969",
        marginBottom: 14,
        display: "flex",
        justifyContent: "space-between",
      }}
    >
      <span>{String(shot).padStart(2, "0")}</span>
      <span>{time}</span>
    </div>
    <StoryImage shot={art ?? shot} />
    <div
      style={{
        fontSize: 30,
        fontWeight: 800,
        letterSpacing: -0.8,
        marginTop: 21,
      }}
    >
      {title}
    </div>
    <div
      style={{
        fontSize: 24,
        lineHeight: 1.38,
        color: "#65705D",
        marginTop: 12,
      }}
    >
      {why}
    </div>
  </div>
);

export const CommercialBoard = () => (
  <AbsoluteFill
    style={{
      background: palette.paper,
      color: palette.ink,
      fontFamily: "Manrope",
    }}
  >
    <div
      style={{
        position: "absolute",
        left: 80,
        top: 60,
        fontSize: 60,
        fontWeight: 800,
        letterSpacing: -2,
      }}
    >
      Looksy / after the shoot.
    </div>
    <div
      style={{
        position: "absolute",
        left: 80,
        top: 147,
        fontSize: 28,
        color: "#65705D",
      }}
    >
      A complete editor. Your manual workflow. Optional local assistance. /
      72-second commercial treatment
    </div>
    <Beat
      shot={1}
      art={1}
      time="00–07"
      title="Back from the shoot. Meet Looksy."
      why="The desk becomes the library. Establish a purpose-built photo, video and design editor."
      x={80}
      y={240}
    />
    <Beat
      shot={2}
      art={2}
      time="07–12"
      title="Bring in the whole shoot."
      why="Batch import, picks, ratings and filters. Select the starting material yourself."
      x={860}
      y={240}
    />
    <Beat
      shot={3}
      art={7}
      time="12–21"
      title="Every adjustment. Your call."
      why="Exposure, white balance, tone, color, detail and masks. Show the full photographic tool family."
      x={1640}
      y={240}
    />
    <Beat
      shot={4}
      art={8}
      time="21–28"
      title="Build the composition."
      why="Work manually with masks, text, brushes and layers. Make and revise the finished design."
      x={2420}
      y={240}
    />
    <Beat
      shot={5}
      art={9}
      time="28–40"
      title="Build the whole production."
      why="Multitrack picture, trims, titles, effects/keyframes, grade and audio become a finished film."
      x={80}
      y={840}
    />
    <Beat
      shot={6}
      art={10}
      time="40–45"
      title="One story. Across the whole set."
      why="Build a coordinated carousel from the same imagery and shared layout canvas."
      x={860}
      y={840}
    />
    <Beat
      shot={7}
      art={11}
      time="45–49"
      title="Deliver at the size you need."
      why="Complete the manual job: photo, film and carousel, with deliberate output settings."
      x={1640}
      y={840}
    />
    <Beat
      shot={8}
      art={4}
      time="49–54"
      title="A hand, when you want it."
      why="Choose to open an optional local agent in the same project. Local processing is a choice."
      x={2420}
      y={840}
    />
    <Beat
      shot={9}
      art={3}
      time="54–58"
      title="Find the frame you remember."
      why="Optional AI search helps find existing media. Review and confirm the matching scope."
      x={80}
      y={1440}
    />
    <Beat
      shot={10}
      art={5}
      time="58–64"
      title="Your direction. Help with repeat work."
      why="Review a sample and apply your direction through native tools on your media. No new content is generated."
      x={860}
      y={1440}
    />
    <Beat
      shot={11}
      art={6}
      time="64–69"
      title="Still yours to change."
      why="Assisted results open in the same editor: adjust the masks, layers, controls and history yourself."
      x={1640}
      y={1440}
    />
    <Beat
      shot={12}
      art={12}
      time="69–72"
      title="Looksy. Your whole creative workflow."
      why="Return through the monitor to the finished delivery. A complete editor, with help when wanted."
      x={2420}
      y={1440}
    />
    <div
      style={{
        position: "absolute",
        left: 80,
        bottom: 45,
        fontSize: 22,
        color: "#7C8373",
      }}
    >
      STAGING BOARD / NOT FINAL APP FOOTAGE • LOCAL MODE AND BATCH/HISTORY
      CLAIMS REQUIRE THE MATCHING REAL WORKFLOW • NO VOICEOVER
    </div>
  </AbsoluteFill>
);
