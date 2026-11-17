import { useLayoutEffect, useRef } from "react";
import {
  CanvasImage,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { opticalFragmentShader } from "./optical-shader";
import { paintAppIcon } from "./icon-painter";
import { appTokens as T } from "./tokens";

export { appTokens } from "./tokens";
export const appUiFont =
  '-apple-system, BlinkMacSystemFont, "Helvetica Neue", sans-serif';
const mono = "Menlo, monospace";

export const AppWordmark = ({ width = 240 }: { width?: number }) => (
  <CanvasImage
    src={staticFile("media/looksy-logo-dark.png")}
    style={{ width, height: (width * 454) / 1795, objectFit: "contain" }}
  />
);

export const AppIcon = ({
  name,
  size = 16,
  color = T.textMain,
}: {
  name: string;
  size?: number;
  color?: string;
}) => {
  const ref = useRef<HTMLCanvasElement>(null);
  useLayoutEffect(() => {
    const ctx = ref.current?.getContext("2d");
    if (!ctx) return;
    ctx.setTransform(2, 0, 0, 2, 0, 0);
    paintAppIcon(ctx, name, size, color);
  }, [name, size, color]);
  return (
    <canvas
      ref={ref}
      width={size * 2}
      height={size * 2}
      style={{ width: size, height: size, display: "block" }}
    />
  );
};

// Qt's shader interface is adapted to WebGL2. The actual field equations,
// channel colors, envelope, interference ring and exponential response are reused.
export const AppOpticalField = ({
  kind = 0,
  variant = 0,
  progress = -1,
  width,
  height,
  active = true,
  failed = false,
  phase,
}: {
  kind?: number;
  variant?: number;
  progress?: number;
  width: number;
  height: number;
  active?: boolean;
  failed?: boolean;
  phase?: number;
}) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const ref = useRef<HTMLCanvasElement>(null);
  const renderer = useRef<{
    gl: WebGL2RenderingContext;
    program: WebGLProgram;
    buffer: WebGLBuffer;
    vertex: WebGLShader;
    fragment: WebGLShader;
  } | null>(null);
  useLayoutEffect(() => {
    const gl = ref.current?.getContext("webgl2", {
      alpha: true,
      premultipliedAlpha: true,
      preserveDrawingBuffer: true,
      antialias: false,
    });
    if (!gl)
      throw new Error(
        "App optical shader requires WebGL2; no decorative fallback is substituted.",
      );
    const compile = (type: number, source: string) => {
      const shader = gl.createShader(type);
      if (!shader) throw new Error("Could not allocate optical shader");
      gl.shaderSource(shader, source);
      gl.compileShader(shader);
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
        throw new Error(gl.getShaderInfoLog(shader) || "Optical shader failed");
      return shader;
    };
    const vertex = compile(
      gl.VERTEX_SHADER,
      `#version 300 es\nin vec2 position;out vec2 uv;void main(){uv=vec2(position.x*.5+.5,.5-position.y*.5);gl_Position=vec4(position,0.,1.);}`,
    );
    const fragment = compile(gl.FRAGMENT_SHADER, opticalFragmentShader);
    const program = gl.createProgram();
    const buffer = gl.createBuffer();
    if (!program || !buffer)
      throw new Error("Could not allocate optical renderer");
    gl.attachShader(program, vertex);
    gl.attachShader(program, fragment);
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS))
      throw new Error(
        gl.getProgramInfoLog(program) || "Optical shader link failed",
      );
    gl.useProgram(program);
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(
      gl.ARRAY_BUFFER,
      new Float32Array([-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1]),
      gl.STATIC_DRAW,
    );
    const location = gl.getAttribLocation(program, "position");
    gl.enableVertexAttribArray(location);
    gl.vertexAttribPointer(location, 2, gl.FLOAT, false, 0, 0);
    renderer.current = { gl, program, buffer, vertex, fragment };
    return () => {
      renderer.current = null;
      gl.deleteBuffer(buffer);
      gl.deleteProgram(program);
      gl.deleteShader(vertex);
      gl.deleteShader(fragment);
    };
  }, []);
  useLayoutEffect(() => {
    const r = renderer.current;
    if (!r) return;
    const { gl, program } = r;
    const duration = kind === 1 ? 2.1 : kind === 2 ? 1.9 : 11;
    gl.viewport(0, 0, width, height);
    gl.useProgram(program);
    gl.uniform1f(gl.getUniformLocation(program, "qt_Opacity"), 1);
    gl.uniform2f(gl.getUniformLocation(program, "resolution"), width, height);
    gl.uniform1f(
      gl.getUniformLocation(program, "lightPhase"),
      phase ?? (active ? ((frame / fps) % duration) / duration : 0.16),
    );
    gl.uniform4f(
      gl.getUniformLocation(program, "parameters"),
      kind,
      variant,
      progress,
      failed ? 1 : 0,
    );
    gl.drawArrays(gl.TRIANGLES, 0, 6);
    gl.finish();
  }, [
    frame,
    fps,
    kind,
    variant,
    progress,
    width,
    height,
    active,
    failed,
    phase,
  ]);
  return (
    <canvas
      ref={ref}
      width={width}
      height={height}
      style={{ width, height, display: "block" }}
    />
  );
};

export const AppActivity = ({
  progress = -1,
  active = true,
  size = 18,
}: {
  progress?: number;
  active?: boolean;
  size?: number;
}) =>
  !active && progress >= 1 ? (
    <AppIcon name="check" size={size} color={T.opticalLight} />
  ) : (
    <AppOpticalField
      kind={1}
      progress={progress}
      active={active}
      width={size}
      height={size}
    />
  );

// The compact StudioAdjustmentSlider geometry at 100% UI scale. Film scale is
// applied outside the control, keeping all label/rail/value/reset relationships.
export const AppInspectorSlider = ({
  label,
  value,
  valueText,
  from = -100,
  to = 100,
  defaultValue = 0,
  precisionResponse = false,
  press = 0,
  scale = 1,
  width = 340,
  trackColors,
  showCursor = false,
}: {
  label: string;
  value: number;
  valueText: string;
  from?: number;
  to?: number;
  defaultValue?: number;
  precisionResponse?: boolean;
  press?: number;
  scale?: number;
  width?: number;
  trackColors?: string[];
  showCursor?: boolean;
}) => {
  const valueX = width - 6 - 28 - 8 - 80;
  const labelWidth = Math.max(32, Math.min(96, width - 80 - 28 - 12 - 24));
  const trackX = 6 + labelWidth + 8,
    trackWidth = valueX - trackX - 8;
  const normalized = (value - from) / (to - from);
  const signed = value < 0 ? value / -from : value / to;
  const position =
    precisionResponse && from < 0 && to > 0
      ? 0.5 + Math.sign(signed) * Math.sqrt(Math.abs(signed)) * 0.5
      : normalized;
  const defaultPosition = (defaultValue - from) / (to - from);
  const modified = Math.abs(value - defaultValue) > 0.0001;
  const cx = trackX + position * (trackWidth - 16) + 8;
  const thumb = 12 + 4 * Math.max(0, Math.min(1, press));
  return (
    <div
      style={{ width: width * scale, height: 40 * scale, position: "relative" }}
    >
      <div
        style={{
          position: "absolute",
          width,
          height: 40,
          scale,
          transformOrigin: "0 0",
          fontFamily: appUiFont,
          color: T.textMain,
          fontSize: 13,
          fontWeight: 500,
        }}
      >
        <span
          style={{
            position: "absolute",
            left: 6,
            top: 0,
            height: 40,
            width: labelWidth,
            display: "flex",
            alignItems: "center",
          }}
        >
          {label}
        </span>
        <div
          style={{
            position: "absolute",
            left: trackX,
            top: 18.5,
            width: trackWidth,
            height: 3,
            borderRadius: 2,
            background: trackColors
              ? `linear-gradient(90deg,${trackColors.join(",")})`
              : "rgba(88,88,88,.3)",
          }}
        >
          {!trackColors &&
            Array.from({ length: 11 }, (_, i) => (
              <i
                key={i}
                style={{
                  position: "absolute",
                  left: (i * (trackWidth - 1)) / 10,
                  top: -4,
                  width: 1,
                  height: i % 5 === 0 ? 4 : 2,
                  background: `rgba(181,181,181,${i % 5 === 0 ? 0.28 : 0.12})`,
                }}
              />
            ))}
          {from < 0 && to > 0 && (
            <i
              style={{
                position: "absolute",
                left: (-from / (to - from)) * trackWidth - 0.5,
                top: -2,
                width: 1,
                height: 7,
                background: "rgba(243,243,243,.34)",
              }}
            />
          )}
          {modified && (
            <i
              style={{
                position: "absolute",
                left: Math.min(defaultPosition, position) * trackWidth,
                width: Math.max(
                  1,
                  Math.abs(defaultPosition - position) * trackWidth,
                ),
                height: 3,
                borderRadius: 2,
                background: "#c4c4c4",
                opacity: 0.82,
              }}
            />
          )}
        </div>
        <div
          style={{
            position: "absolute",
            left: cx - thumb / 2,
            top: 20 - thumb / 2,
            width: thumb,
            height: thumb,
            borderRadius: "50%",
            background: press > 0 ? T.brandPaper : T.textMain,
            border: `1px solid ${press > 0 ? "rgba(196,196,196,.9)" : "rgba(16,16,16,.62)"}`,
          }}
        />
        <span
          style={{
            position: "absolute",
            left: valueX,
            top: 0,
            width: 80,
            height: 40,
            padding: "0 4px",
            display: "flex",
            alignItems: "center",
            justifyContent: "flex-end",
            fontFamily: mono,
            color: T.brandPaper,
            fontVariantNumeric: "tabular-nums",
          }}
        >
          {valueText}
        </span>
        {modified && (
          <div
            style={{
              position: "absolute",
              left: width - 6 - 28,
              top: 6,
              width: 28,
              height: 28,
              display: "grid",
              placeItems: "center",
            }}
          >
            <AppIcon name="reset" size={14} />
          </div>
        )}
        {showCursor && (
          <svg
            width={20}
            height={26}
            viewBox="0 0 20 26"
            style={{
              position: "absolute",
              left: cx + 3,
              top: 25,
              filter: "drop-shadow(0 1px 2px #0008)",
            }}
          >
            <path
              d="M2 1V20L7 15L11 24L15 22L11 13H18Z"
              fill="white"
              stroke="#111"
              strokeWidth={1.2}
            />
          </svg>
        )}
      </div>
    </div>
  );
};

// Canvas scanline construction adapted from CompletionCaustic.qml; this is the
// app's finite RGB/CMY registration mark, not an invented sweep over the image.
export const AppCompletion = ({
  phase,
  width = 64,
  height = 20,
}: {
  phase: number;
  width?: number;
  height?: number;
}) => {
  const ref = useRef<HTMLCanvasElement>(null);
  useLayoutEffect(() => {
    const c = ref.current?.getContext("2d");
    if (!c) return;
    c.clearRect(0, 0, width, height);
    if (phase < 0 || phase > 1) return;
    const focus = Math.min(1, phase / 0.72),
      separation = (1 - focus) ** 2 * width * 0.18;
    const halfWidth = Math.min(30, width * 0.3),
      halfHeight = Math.max(2, Math.floor(height * 0.42));
    const lights = [
      [255, 59, 48],
      [48, 209, 88],
      [50, 130, 255],
    ].map((v) => v.map((x) => x / 255));
    const inks = [
      [0, 1, 1],
      [1, 0, 1],
      [1, 1, 0],
    ];
    for (let y = -halfHeight; y < halfHeight; y++) {
      const span = halfWidth * (1 - Math.abs(y + 0.5) / halfHeight);
      if (span <= 0) continue;
      const additive = y < 0,
        colors = additive ? lights : inks;
      const edges = colors
        .flatMap((_, i) => [
          width * 0.5 + (i - 1) * separation - span,
          width * 0.5 + (i - 1) * separation + span,
        ])
        .sort((a, b) => a - b);
      for (let i = 0; i < edges.length - 1; i++) {
        const midpoint = (edges[i] + edges[i + 1]) / 2;
        let r = additive ? 0 : 1,
          g = r,
          b = r,
          coverage = 0;
        for (let plate = 0; plate < 3; plate++) {
          if (
            Math.abs(midpoint - width * 0.5 - (plate - 1) * separation) > span
          )
            continue;
          coverage++;
          if (additive) {
            r += colors[plate][0];
            g += colors[plate][1];
            b += colors[plate][2];
          } else {
            r *= colors[plate][0];
            g *= colors[plate][1];
            b *= colors[plate][2];
          }
        }
        if (!coverage) continue;
        c.fillStyle = `rgb(${Math.min(1, r) * 255},${Math.min(1, g) * 255},${Math.min(1, b) * 255})`;
        c.fillRect(edges[i], height * 0.5 + y, edges[i + 1] - edges[i], 1);
      }
    }
  }, [phase, width, height]);
  return (
    <canvas
      ref={ref}
      width={width}
      height={height}
      style={{
        width,
        height,
        opacity:
          phase >= 0 && phase <= 1
            ? Math.sin(Math.PI * phase) ** 1.4 * 0.82
            : 0,
      }}
    />
  );
};
