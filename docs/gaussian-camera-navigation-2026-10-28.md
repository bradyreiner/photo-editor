# Direct camera movement

Scene now starts in **Look** mode. Dragging turns the view around the camera's
own position. W/S move forward and backward along the direction currently in
view; A/D move sideways, and Q/E move up/down in the camera frame. Scrolling
moves forward/backward. Turning, moving, stopping, turning again and moving
again always uses the current pose.

**Orbit** is a separate mode. Double-click a visible surface, or use Target/F
and click, to choose its world-space pivot. Dragging then moves around that
point. Free-flight movement retains a selected pivot's world position. If it
has moved behind the camera, starting an orbit uses a new forward focus pivot
instead of silently refusing to move.

**Pan** translates the view with a drag. Right-drag, middle-drag and Shift-drag
also pan directly. **Roll** twists the camera with a horizontal drag; Alt-drag
does the same without switching modes. Alt-click remains subject focus.
Arrow keys turn the view (or follow the selected Orbit/Pan/Roll tool), while
Shift-arrows pan and Alt-left/right roll. R restores the capture camera.

Rotation uses normalized current-camera basis vectors and quaternion increments.
Look and Roll keep the camera position fixed; Orbit preserves the picked
point's off-axis framing and orbit radius. There is no horizon lock, automatic
leveling or former 85-degree pitch limit. Saved shots retain orientation through
their existing right/down/forward camera vectors; no new file schema is needed.

Pointer deltas are combined over a 16 ms interval and flushed on release. This
reduces repeated model/render requests without dropping the last movement or
adding inertia. It does not establish a new GPU frame-rate or input-to-photon
latency result. Movement step sizes follow scene/focus depth, and trackpad pixel
deltas are handled separately from wheel notches.

The existing large monochrome camera cards stay unchanged. The small navigation
HUD uses icon-only mode controls in narrow viewports and hides the extra pan pad
when it cannot fit.

The native pointer/keyboard proof passes the complete Look → forward → Look →
forward sequence, explicit picked-point Orbit, direct Roll, Alt-drag Roll, and
horizontal/vertical Pan in the rolled camera frame. It also checks that movement
preserves lens/photo settings and that reset restores the original pose. The
full isolated Cocoa/Metal camera workflow passes with the new controls.
Eight focused suites pass in 19.53 seconds: navigation math, controller preview
handoff, native focus controls, rendering, ray-camera optics, ray-scene
integration, saved views and camera paths.
The Release desktop app was rebuilt successfully. Camera-specific source hashes,
test/binary hashes and source-to-QML-cache/C++-object timestamps are recorded in
`verification.json`.

Math/controller checks cover 2000 incremental rotations, finite orthonormal
camera axes, movement along the current forward/right/down vectors, rotation
beyond the former pitch limit, selected-pivot continuity and recovery after
looking away from a pivot. Native Scene component proof does not establish
foreground input latency or full production-app startup qualification.

Receipts, screenshots and matching source/binary hashes are in
[evidence/gaussian-camera-navigation-2026-10-28](evidence/gaussian-camera-navigation-2026-10-28).
