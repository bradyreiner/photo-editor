#version 440

layout(location = 0) in vec2 textureCoordinate;
layout(location = 1) in vec3 sceneCoordinate;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D occlusionMask;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float itemWidth;
    float itemHeight;
    vec4 topLeft;
    vec4 topRight;
    vec4 bottomLeft;
    vec4 bottomRight;
    vec4 occlusionRect;
    float occlusionEnabled;
    float occlusionReady;
    vec4 fadeColor;
    float fadeAmount;
    float frameCornerRadius;
    float clipMaskEnabled;
    float clipMaskShape;
    vec4 clipMaskRect;
    float clipMaskCornerRadius;
    vec2 clipMaskCanvasSize;
    vec2 clipMaskFrameCenter;
    float clipMaskFrameScale;
    float clipMaskFrameRotationDegrees;
} ubuf;

void main()
{
    fragColor = texture(source, textureCoordinate) * ubuf.qt_Opacity;
    fragColor.rgb = mix(fragColor.rgb, ubuf.fadeColor.rgb * fragColor.a, clamp(ubuf.fadeAmount, 0.0, 1.0));
    if (ubuf.frameCornerRadius > 0.0) {
        vec2 size = vec2(ubuf.itemWidth, ubuf.itemHeight);
        float radius = clamp(ubuf.frameCornerRadius, 0.0, 0.5) * min(size.x, size.y);
        vec2 halfSize = size * 0.5;
        vec2 corner = abs(textureCoordinate * size - halfSize) - (halfSize - vec2(radius));
        float distanceToEdge = length(max(corner, vec2(0.0)))
            + min(max(corner.x, corner.y), 0.0) - radius;
        float antialiasWidth = max(fwidth(distanceToEdge), 0.5);
        float frameCoverage = 1.0 - smoothstep(-antialiasWidth, antialiasWidth, distanceToEdge);
        // ShaderEffect sources use premultiplied alpha. Coverage must attenuate
        // RGB as well as alpha or transparent corners leak source color.
        fragColor *= frameCoverage;
    }
    if (ubuf.clipMaskEnabled > 0.5) {
        vec2 projectedPoint = sceneCoordinate.xy / max(sceneCoordinate.z, 0.00001);
        vec2 localPoint = (projectedPoint - vec2(0.5)) * vec2(ubuf.itemWidth, ubuf.itemHeight);
        float angle = ubuf.clipMaskFrameRotationDegrees * 0.017453292519943295;
        float cosine = cos(angle);
        float sine = sin(angle);
        vec2 worldPoint = ubuf.clipMaskFrameCenter + ubuf.clipMaskFrameScale
            * vec2(cosine * localPoint.x - sine * localPoint.y,
                   sine * localPoint.x + cosine * localPoint.y);
        vec2 maskMinimum = ubuf.clipMaskRect.xy * ubuf.clipMaskCanvasSize;
        vec2 maskMaximum = ubuf.clipMaskRect.zw * ubuf.clipMaskCanvasSize;
        float maskAlpha = 0.0;
        if (maskMaximum.x > maskMinimum.x && maskMaximum.y > maskMinimum.y) {
            vec2 maskSize = maskMaximum - maskMinimum;
            vec2 maskPoint = worldPoint - (maskMinimum + maskMaximum) * 0.5;
            if (ubuf.clipMaskShape > 0.5) {
                float ellipseDistance = length(maskPoint / (maskSize * 0.5));
                float aa = max(fwidth(ellipseDistance), 0.001);
                maskAlpha = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, ellipseDistance);
            } else {
                float radius = clamp(ubuf.clipMaskCornerRadius, 0.0, 0.5)
                    * min(maskSize.x, maskSize.y);
                vec2 corner = abs(maskPoint) - (maskSize * 0.5 - vec2(radius));
                float distanceToEdge = length(max(corner, vec2(0.0)))
                    + min(max(corner.x, corner.y), 0.0) - radius;
                float aa = max(fwidth(distanceToEdge), 0.5);
                maskAlpha = 1.0 - smoothstep(-aa, aa, distanceToEdge);
            }
        }
        // Keep the output premultiplied while applying geometric coverage.
        fragColor *= maskAlpha;
    }
    if (ubuf.occlusionEnabled > 0.5) {
        if (ubuf.occlusionReady < 0.5) {
            fragColor = vec4(0.0);
            return;
        }
        vec2 sceneUV = sceneCoordinate.xy / sceneCoordinate.z;
        vec2 maskUV = (sceneUV - ubuf.occlusionRect.xy) / ubuf.occlusionRect.zw;
        if (all(greaterThanEqual(maskUV, vec2(0.0))) && all(lessThanEqual(maskUV, vec2(1.0))))
            fragColor *= 1.0 - texture(occlusionMask, maskUV).a;
    }
}
