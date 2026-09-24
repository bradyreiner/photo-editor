#version 440

layout(location = 0) in vec2 textureCoordinate;
layout(location = 0) out vec4 fragmentColor;

layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    int simulationMode;
} ubuf;

vec3 srgbToLinear(vec3 value)
{
    bvec3 cutoff = lessThanEqual(value, vec3(0.04045));
    vec3 lower = value / 12.92;
    vec3 upper = pow((value + vec3(0.055)) / 1.055, vec3(2.4));
    return mix(upper, lower, cutoff);
}

vec3 linearToSrgb(vec3 value)
{
    value = max(value, vec3(0.0));
    bvec3 cutoff = lessThanEqual(value, vec3(0.0031308));
    vec3 lower = value * 12.92;
    vec3 upper = 1.055 * pow(value, vec3(1.0 / 2.4)) - vec3(0.055);
    return mix(upper, lower, cutoff);
}

mat3 simulationMatrix(int mode)
{
    if (mode == 1) {
        return mat3(
            0.56667, 0.55833, 0.00000,
            0.43333, 0.44167, 0.24167,
            0.00000, 0.00000, 0.75833);
    }
    if (mode == 2) {
        return mat3(
            0.62500, 0.70000, 0.00000,
            0.37500, 0.30000, 0.30000,
            0.00000, 0.00000, 0.70000);
    }
    if (mode == 3) {
        return mat3(
            0.95000, 0.00000, 0.00000,
            0.05000, 0.43333, 0.47500,
            0.00000, 0.56667, 0.52500);
    }
    return mat3(
        0.21260, 0.21260, 0.21260,
        0.71520, 0.71520, 0.71520,
        0.07220, 0.07220, 0.07220);
}

void main()
{
    vec4 sampled = texture(source, textureCoordinate);
    if (sampled.a <= 0.0) {
        fragmentColor = vec4(0.0);
        return;
    }

    vec3 unpremultiplied = clamp(sampled.rgb / sampled.a, 0.0, 1.0);
    vec3 simulated = simulationMatrix(ubuf.simulationMode) * srgbToLinear(unpremultiplied);
    vec3 encoded = clamp(linearToSrgb(simulated), 0.0, 1.0);
    fragmentColor = vec4(encoded * sampled.a, sampled.a) * ubuf.qt_Opacity;
}
