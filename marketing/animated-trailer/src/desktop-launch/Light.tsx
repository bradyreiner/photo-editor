import { useLayoutEffect, useRef } from "react";
import { useVideoConfig } from "remotion";
import { A, ink } from "../rebuild/Kit";

// Broad RGB light fields, not stroked polylines. The three emitters add before
// display encoding. Grain is stable in screen space; internal caustics advect.
const fragment = `#version 300 es
precision highp float;
out vec4 frag;
uniform vec2 size;
uniform float time;
uniform float gather;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);}
float fbm(vec2 p){return .57*noise(p)+.28*noise(p*2.03)+.15*noise(p*4.07);}
float field(vec2 p,float channel){
 float phase=time*.57+channel*.56;
 float n=fbm(p*4.+vec2(time*.12,-time*.08));
 float centre=.51+.19*sin(p.x*4.3-phase)+.055*cos(p.x*9.1+phase);
 centre+=(channel-1.)*.075;
 float band=p.y-centre+(n-.5)*.07;
 float thick=.085+.055*(.5+.5*sin(p.x*4.8+phase));
 float sheet=exp(-pow(band/thick,4.));
 float core=exp(-pow((band+.013*sin(p.x*15.+phase))/(thick*.2),2.));
 float rings=length((p-vec2(.5,.52)+vec2((channel-1.)*.04,0))*vec2(1.,size.y/size.x));
 float disk=exp(-pow((rings-.29)/.075,2.));
 float caustic=pow(.5+.5*sin(n*15.+band*25.-time*.2),3.);
 return mix(sheet*.53+core*.17+caustic*sheet*.45,disk*(.4+.6*caustic),gather);
}
void main(){vec2 p=vec2(gl_FragCoord.x/size.x,1.-gl_FragCoord.y/size.y);
 vec3 rgb=vec3(field(p,0.),field(p,1.),field(p,2.));
 float grain=hash(floor(gl_FragCoord.xy*.7));
 rgb*=.67+.48*grain;
 rgb=pow(rgb,vec3(.8));
 frag=vec4(vec3(.043,.051,.071)+rgb*.94,1.);
}`;
export const DesktopLight = ({
  t,
  gather = 0,
  style = {},
}: {
  t: number;
  gather?: number;
  style?: React.CSSProperties;
}) => {
  const { width, height } = useVideoConfig();
  const ref = useRef<HTMLCanvasElement>(null),
    renderer = useRef<{
      gl: WebGL2RenderingContext;
      program: WebGLProgram;
      buffer: WebGLBuffer;
      v: WebGLShader;
      f: WebGLShader;
    } | null>(null);
  useLayoutEffect(() => {
    const gl = ref.current?.getContext("webgl2", {
      alpha: false,
      preserveDrawingBuffer: true,
      antialias: false,
    });
    if (!gl) throw new Error("RGB field requires WebGL2");
    const compile = (type: number, s: string) => {
      const shader = gl.createShader(type)!;
      gl.shaderSource(shader, s);
      gl.compileShader(shader);
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
        throw new Error(
          gl.getShaderInfoLog(shader) || "Shader compilation failed",
        );
      return shader;
    };
    const v = compile(
        gl.VERTEX_SHADER,
        "#version 300 es\nin vec2 position;void main(){gl_Position=vec4(position,0.,1.);}",
      ),
      f = compile(gl.FRAGMENT_SHADER, fragment),
      program = gl.createProgram()!,
      buffer = gl.createBuffer()!;
    gl.attachShader(program, v);
    gl.attachShader(program, f);
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS))
      throw new Error(
        gl.getProgramInfoLog(program) || "Light field link failed",
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
    renderer.current = { gl, program, buffer, v, f };
    return () => {
      renderer.current = null;
      gl.deleteProgram(program);
      gl.deleteBuffer(buffer);
      gl.deleteShader(v);
      gl.deleteShader(f);
    };
  }, []);
  useLayoutEffect(() => {
    const r = renderer.current;
    if (!r) return;
    const { gl, program } = r;
    gl.viewport(0, 0, width, height);
    gl.useProgram(program);
    gl.uniform2f(gl.getUniformLocation(program, "size"), width, height);
    gl.uniform1f(gl.getUniformLocation(program, "time"), t);
    gl.uniform1f(gl.getUniformLocation(program, "gather"), gather);
    gl.drawArrays(gl.TRIANGLES, 0, 6);
    gl.finish();
  }, [t, gather, width, height]);
  return (
    <canvas
      ref={ref}
      width={width}
      height={height}
      style={{
        ...A,
        inset: 0,
        width: "100%",
        height: "100%",
        background: ink,
        ...style,
      }}
    />
  );
};
