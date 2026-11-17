#version 440

layout(location = 0) in vec2 textureCoordinate;
layout(location = 0) out vec4 fragmentColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 resolution;
    float timeSeconds;
    float gather;
} ubuf;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

float fbm(vec2 p)
{
    return 0.57 * noise(p) + 0.28 * noise(p * 2.03) + 0.15 * noise(p * 4.07);
}

float field(vec2 p, float channel)
{
    float phase = ubuf.timeSeconds * 0.57 + channel * 0.56;
    float n = fbm(p * 4.0 + vec2(ubuf.timeSeconds * 0.12, -ubuf.timeSeconds * 0.08));
    float centre = 0.51 + 0.19 * sin(p.x * 4.3 - phase) + 0.055 * cos(p.x * 9.1 + phase);
    centre += (channel - 1.0) * 0.075;
    float band = p.y - centre + (n - 0.5) * 0.07;
    float thick = 0.085 + 0.055 * (0.5 + 0.5 * sin(p.x * 4.8 + phase));
    float sheet = exp(-pow(band / thick, 4.0));
    float core = exp(-pow((band + 0.013 * sin(p.x * 15.0 + phase)) / (thick * 0.2), 2.0));
    float rings = length((p - vec2(0.5, 0.52) + vec2((channel - 1.0) * 0.04, 0.0))
                         * vec2(1.0, ubuf.resolution.y / max(1.0, ubuf.resolution.x)));
    float disk = exp(-pow((rings - 0.29) / 0.075, 2.0));
    float caustic = pow(0.5 + 0.5 * sin(n * 15.0 + band * 25.0 - ubuf.timeSeconds * 0.2), 3.0);
    return mix(sheet * 0.53 + core * 0.17 + caustic * sheet * 0.45,
               disk * (0.4 + 0.6 * caustic), clamp(ubuf.gather, 0.0, 1.0));
}

void main()
{
    vec2 p = vec2(textureCoordinate.x, 1.0 - textureCoordinate.y);
    vec3 rgb = vec3(field(p, 0.0), field(p, 1.0), field(p, 2.0));
    vec2 pixel = vec2(textureCoordinate.x * ubuf.resolution.x,
                      (1.0 - textureCoordinate.y) * ubuf.resolution.y);
    float grain = hash(floor(pixel * 0.7));
    rgb *= 0.67 + 0.48 * grain;
    rgb = pow(max(rgb, vec3(0.0)), vec3(0.8));
    fragmentColor = vec4(vec3(0.043, 0.051, 0.071) + rgb * 0.94, 1.0) * ubuf.qt_Opacity;
}
