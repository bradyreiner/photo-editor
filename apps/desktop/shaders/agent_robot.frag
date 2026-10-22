#version 440
layout(location = 0) in vec2 textureCoordinate;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 viewport;
    vec4 pose;
    vec4 behavior;
    vec4 tint;
    float motionPhase;
    float blinkAmount;
    float reactionAmount;
} ubuf;

// A small, actual three-dimensional character. Rounded solids, articulated
// joints, glass, lighting and occlusion are evaluated on the GPU; no raster
// animation strip or per-frame Canvas reconstruction is involved.
const float PI = 3.14159265;
float clockTime;
float headYaw;
float headRoll;
vec2 rot(vec2 p, float a) {
    float c = cos(a), s = sin(a);
    return mat2(c, -s, s, c) * p;
}
float box(vec3 p, vec3 halfSize, float radius) {
    vec3 q = abs(p) - halfSize + radius;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - radius;
}
float capsule(vec3 p, vec3 a, vec3 b, float radius) {
    vec3 pa = p - a, ba = b - a;
    return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0)) - radius;
}
vec2 nearer(vec2 a, vec2 b) { return a.x < b.x ? a : b; }
vec3 headSpace(vec3 p) {
    p -= vec3(0.0, 2.06, 0.0);
    p.xy = rot(p.xy, headRoll);
    p.xz = rot(p.xz, headYaw);
    if (ubuf.pose.x > 2.5 && ubuf.pose.x < 3.5)
        p.yz = rot(p.yz, ubuf.reactionAmount * 0.055);
    return p;
}
vec2 scene(vec3 p) {
    vec3 h = headSpace(p);
    vec2 d = vec2(box(h, vec3(0.91, 0.63, 0.51), 0.19), 1.0);
    // Recessed bezel, then gently convex glass in front of it.
    d = nearer(d, vec2(box(h - vec3(0.0, 0.015, 0.473), vec3(0.805, 0.505, 0.083), 0.16), 2.0));
    vec3 glass = h - vec3(0.0, 0.025, 0.535);
    glass.z += 0.105 * dot(glass.xy, glass.xy);
    d = nearer(d, vec2(box(glass, vec3(0.735, 0.435, 0.084), 0.135), 3.0));
    // Side ears and a neck bearing give the screen a convincing solid chassis.
    for (int side = -1; side <= 1; side += 2) {
        float s = float(side);
        d = nearer(d, vec2(box(h - vec3(s * 0.92, -0.04, -0.08), vec3(0.09, 0.20, 0.24), 0.065), 4.0));
        d = nearer(d, vec2(box(h - vec3(s * 1.002, -0.04, -0.045), vec3(0.017, 0.12, 0.12), 0.016), 5.0));
    }
    d = nearer(d, vec2(capsule(p, vec3(0.0, 1.32, 0.0), vec3(0.0, 1.56, 0.0), 0.16), 5.0));
    d = nearer(d, vec2(box(p - vec3(0.0, 1.02, -0.025), vec3(0.48, 0.34, 0.30), 0.14), 1.0));
    d = nearer(d, vec2(box(p - vec3(0.0, 1.03, 0.283), vec3(0.34, 0.20, 0.027), 0.055), 4.0));
    d = nearer(d, vec2(box(p - vec3(-0.23, 1.04, 0.319), vec3(0.027, 0.105, 0.015), 0.012), 6.0));
    // Two articulated arms and comfortable, broad feet; wave and walk are poses,
    // not scale-pulsing the entire silhouette.
    bool waving = ubuf.pose.x > 4.5 && ubuf.pose.x < 5.5;
    bool walking = ubuf.pose.x > 5.5;
    for (int side = -1; side <= 1; side += 2) {
        float s = float(side);
        float stride = walking ? sin(clockTime * PI * 2.0 + s * PI * 0.5) : 0.0;
        vec3 shoulder = vec3(s * 0.53, 1.22, -0.025);
        vec3 elbow = vec3(s * 0.73, 0.98, 0.015 + stride * 0.12);
        vec3 hand = vec3(s * 0.70, 0.75, 0.14 + stride * 0.16);
        if (waving && side == 1) {
            elbow = vec3(0.96, 1.59, 0.03);
            hand = vec3(1.13 + 0.11 * sin(clockTime * PI * 2.0), 2.03, 0.12);
        } else if (side == 1 && ubuf.behavior.x > 0.0) {
            hand += vec3(0.32, 0.28, 0.10) * ubuf.behavior.x;
            elbow += vec3(0.12, 0.12, 0.0) * ubuf.behavior.x;
        }
        d = nearer(d, vec2(length(p - shoulder) - 0.125, 2.0));
        d = nearer(d, vec2(capsule(p, shoulder, elbow, 0.074), 5.0));
        d = nearer(d, vec2(length(p - elbow) - 0.105, 4.0));
        d = nearer(d, vec2(capsule(p, elbow, hand, 0.080), 1.0));
        d = nearer(d, vec2(box(p - hand, vec3(0.105, 0.095, 0.09), 0.045), 4.0));
        // Simple rounded gripper fingers, never thin human fingers.
        d = nearer(d, vec2(capsule(p, hand + vec3(-0.065, -0.05, 0.015), hand + vec3(-0.07, -0.16, 0.06), 0.035), 1.0));
        d = nearer(d, vec2(capsule(p, hand + vec3(0.065, -0.05, 0.015), hand + vec3(0.07, -0.16, 0.06), 0.035), 1.0));
        float lift = max(0.0, stride) * 0.075;
        vec3 hip = vec3(s * 0.27, 0.70, 0.0);
        vec3 knee = vec3(s * 0.28, 0.45 + lift, stride * 0.12);
        vec3 ankle = vec3(s * 0.29, 0.21 + lift, stride * 0.20);
        d = nearer(d, vec2(capsule(p, hip, knee, 0.088), 2.0));
        d = nearer(d, vec2(length(p - knee) - 0.105, 5.0));
        d = nearer(d, vec2(capsule(p, knee, ankle, 0.085), 4.0));
        d = nearer(d, vec2(box(p - ankle - vec3(s * 0.025, -0.06, 0.08), vec3(0.215, 0.12, 0.28), 0.085), 1.0));
        d = nearer(d, vec2(box(p - ankle - vec3(s * 0.025, -0.14, 0.08), vec3(0.215, 0.038, 0.27), 0.025), 2.0));
    }
    return d;
}
vec3 normalAt(vec3 p) {
    const vec2 e = vec2(0.0015, 0.0);
    return normalize(vec3(scene(p + e.xyy).x - scene(p - e.xyy).x,
                          scene(p + e.yxy).x - scene(p - e.yxy).x,
                          scene(p + e.yyx).x - scene(p - e.yyx).x));
}
float occlusion(vec3 p, vec3 n) {
    float result = 0.0;
    for (int i = 1; i <= 3; ++i) {
        float h = float(i) * 0.065;
        result += max(0.0, h - scene(p + n * h).x) / float(i);
    }
    return clamp(1.0 - result * 2.2, 0.35, 1.0);
}
float oval(vec2 p, vec2 size) { return (length(p / size) - 1.0) * min(size.x, size.y); }
vec3 screenLight(vec3 h) {
    vec2 uv = h.xy - vec2(0.0, 0.025);
    // All displacement happens in face coordinates, so CRT noise belongs to the
    // curved screen and follows the head instead of floating over the window.
    float band = step(0.92, sin(uv.y * 73.0 + clockTime * 21.0));
    uv.x += ubuf.reactionAmount * band * 0.065;
    float gaze = clamp(ubuf.pose.y * 0.0018, -0.07, 0.07);
    uv.x -= gaze + sin(clockTime * PI / 3.0) * 0.018;
    float openEye = max(0.055, ubuf.blinkAmount);
    bool happy = ubuf.pose.x > 2.5 && ubuf.pose.x < 3.5;
    bool warning = ubuf.pose.x > 3.5 && ubuf.pose.x < 4.5;
    float eyes = 10.0;
    for (int side = -1; side <= 1; side += 2) {
        vec2 e = uv - vec2(float(side) * 0.245, 0.065);
        float eye;
        if (happy) {
            eye = abs(length(e - vec2(0.0, -0.09)) - 0.125) - 0.019;
            eye = max(eye, -e.y);
        } else {
            eye = oval(e, vec2(0.072, (warning ? 0.086 : 0.126) * openEye));
        }
        eyes = min(eyes, eye);
    }
    vec2 m = uv - vec2(0.0, -0.12);
    float mouth;
    if (ubuf.pose.x > 1.5 && ubuf.pose.x < 2.5) {
        mouth = oval(m, vec2(0.085, 0.022 + 0.052 * (0.5 + 0.5 * sin(clockTime * PI * 6.0))));
    } else {
        mouth = max(abs(length(m - vec2(0.0, 0.12)) - 0.15) - 0.012, m.y + 0.025);
        mouth = max(mouth, abs(m.x) - 0.115);
    }
    float ink = min(eyes, mouth);
    float sharp = 1.0 - smoothstep(-0.004, 0.009, ink);
    float glow = exp(-max(ink, 0.0) * 27.0) * 0.24;
    vec3 phosphor = warning ? vec3(1.0, 0.46, 0.15) : ubuf.tint.rgb;
    vec3 light = phosphor * (sharp * 1.65 + glow);
    if (ubuf.pose.x > 0.5 && ubuf.pose.x < 1.5) {
        float progress = ubuf.pose.z;
        float bar = (1.0 - smoothstep(0.008, 0.017, abs(uv.y + 0.30))) * step(abs(uv.x), 0.43);
        float cursor = progress >= 0.0 ? step(uv.x, -0.43 + 0.86 * clamp(progress, 0.0, 1.0))
            : exp(-pow((uv.x - 0.28 * sin(clockTime * PI)) / 0.13, 2.0));
        light += phosphor * bar * (0.10 + cursor * 0.7);
    }
    float scan = 0.78 + 0.22 * smoothstep(0.05, 0.8, sin(h.y * 330.0));
    float beam = exp(-pow((fract(h.y * 0.6 - clockTime * 0.2) - 0.5) * 14.0, 2.0));
    light *= scan * (0.95 + beam * 0.10);
    // Restrained RGB phosphor triads, resolved in the screen's own space.
    light *= 0.94 + 0.06 * cos(h.x * 630.0 + vec3(0.0, 2.094, 4.189));
    return light;
}
void main() {
    clockTime = ubuf.motionPhase * 12.0;
    headYaw = radians(clamp(ubuf.pose.y, -22.0, 22.0)) + sin(clockTime * PI / 3.0) * 0.035;
    headRoll = sin(clockTime * PI / 6.0) * 0.025 + ubuf.reactionAmount * 0.025;
    vec2 uv = textureCoordinate;
    if (ubuf.behavior.y > 0.5) uv.x = 1.0 - uv.x;
    vec2 screen = (uv - 0.5) * vec2(ubuf.viewport.x / max(ubuf.viewport.y, 1.0), -1.0);
    vec3 camera = vec3(2.7, 2.65, 8.5);
    vec3 target = vec3(0.0, 1.39, 0.0);
    vec3 forward = normalize(target - camera);
    vec3 right = normalize(cross(forward, vec3(0.0, 1.0, 0.0)));
    vec3 up = cross(right, forward);
    vec3 ray = normalize(forward * 2.65 + screen.x * right + screen.y * up);
    // A bounding sphere rejects transparent pixels before expensive traversal.
    vec3 offset = camera - target;
    float b = dot(offset, ray);
    float discriminant = b * b - dot(offset, offset) + 2.05 * 2.05;
    if (discriminant < 0.0) { fragColor = vec4(0.0); return; }
    float distance = max(0.0, -b - sqrt(discriminant));
    vec2 hit = vec2(1.0, 0.0);
    float limit = -b + sqrt(discriminant);
    for (int stepIndex = 0; stepIndex < 72; ++stepIndex) {
        hit = scene(camera + ray * distance);
        if (hit.x < 0.0018 || distance > limit) break;
        distance += hit.x * 0.86;
    }
    if (distance > limit || hit.x > 0.006) { fragColor = vec4(0.0); return; }
    vec3 p = camera + ray * distance;
    vec3 n = normalAt(p);
    vec3 view = -ray;
    vec3 key = normalize(vec3(-3.0, 5.0, 4.0));
    vec3 fill = normalize(vec3(4.0, 1.8, 2.0));
    float diffuse = max(0.0, dot(n, key));
    float fillLight = max(0.0, dot(n, fill));
    float ao = occlusion(p, n);
    vec3 base = vec3(0.68, 0.72, 0.68);
    float roughness = 0.32;
    if (hit.y > 1.5 && hit.y < 2.5) { base = vec3(0.025, 0.045, 0.048); roughness = 0.4; }
    if (hit.y > 3.5 && hit.y < 4.5) { base = vec3(0.075, 0.18, 0.19); roughness = 0.3; }
    if (hit.y > 4.5 && hit.y < 5.5) { base = vec3(0.26, 0.34, 0.35); roughness = 0.21; }
    if (hit.y > 5.5) { base = vec3(0.94, 0.40, 0.13); roughness = 0.3; }
    vec3 color = base * (vec3(0.18, 0.23, 0.28) + diffuse * vec3(0.85, 0.83, 0.73) + fillLight * vec3(0.12, 0.27, 0.30)) * ao;
    float shine = pow(max(0.0, dot(n, normalize(key + view))), mix(110.0, 28.0, roughness));
    color += vec3(0.95, 0.97, 0.92) * shine * (hit.y > 4.5 && hit.y < 5.5 ? 0.9 : 0.28);
    float rim = pow(1.0 - max(0.0, dot(n, view)), 3.0);
    color += vec3(0.18, 0.37, 0.40) * rim * 0.45;
    if (hit.y > 2.5 && hit.y < 3.5) {
        vec3 h = headSpace(p);
        color = vec3(0.006, 0.021, 0.025) + screenLight(h);
        vec3 reflected = reflect(-view, n);
        float softbox = pow(max(0.0, dot(reflected, normalize(vec3(-2.0, 4.0, 4.0)))), 30.0);
        color += vec3(0.30, 0.48, 0.49) * softbox * 0.45 + vec3(0.05, 0.13, 0.15) * rim;
    }
    // Gentle filmic compression and display transfer preserve glowing highlights.
    color = color / (vec3(1.0) + color * 0.32);
    color = pow(max(color, 0.0), vec3(1.0 / 2.2));
    fragColor = vec4(color, 1.0) * ubuf.qt_Opacity;
}
