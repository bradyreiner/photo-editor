# Immersive Gaussian viewer: live to settled photo

User direction: Scene should be a full-screen 3D viewer, allow live perspective
changes, and resolve camera-emulated photography when movement stops.

## Implemented and verified

- Scene fills the application window and hides the global navigation bar while
  immersive. A Library return action remains visible. The camera's aspect ratio
  is preserved with letterboxing; this is not forced operating-system fullscreen.
- Capture/setup, saved cameras, paths and export options move into a scrollable
  Scene options overlay. A foreground Take photo action is available when settled.
  A top gradient keeps controls readable over bright images.
- Orbit, pan, dolly and time/path seeking request a bounded 720-edge live frame.
  A held pointer stays in live mode; release or 180 ms without wheel/seeking input
  requests a display-resolution settled render, capped at 4096 pixels.
- Scene data stays resident; work remains on the serialized native renderer.
  An obsolete settled frame cannot overwrite a newer camera request. A live frame
  is never labeled as a settled photo, including when navigation is hidden.
- Hidden viewports stop settlement/playback work and refine on return. Explicit
  PNG/JPG exports retain frozen camera/time and their existing integrity receipts.

## Evidence

Host root: `/private/tmp/framehouse-immersive-scene-mjCQmu`.

`final-proof/receipt.json` and
`final-ui/20260909T165730.669244Z-perf-workspace-switch-f9583e22` record the actual
Main.qml native Metal proof using an existing 753,038-splat, 32-capture scene.
Foreground eligibility, full-window geometry, live/settled states, camera
preservation, rapid navigation and the options popup pass. Live, settled and
options screenshots were inspected. Existing Export preflight binding warnings
remain separate; Scene introduces no observed QML binding errors in this run.

Before/after 800-edge photo exports retain the same SHA-256 as the pre-redesign
proof: `21e3bcb5f06e6093e6a18fb3e3198ddf0d96921fe595643a2fe73ae625df8abb`.
No training or source/camera-file modification was performed.

The focused viewport regression covers held drags, automatic scroll settlement,
720-to-1440 resolution refinement at unchanged perspective, hidden/resume behavior,
stale settled-frame rejection, queued exports and serialized worker lifetime.
Five focused CTest checks pass, including the viewport test, scene controller,
saved views, foreground rules and the comparison tests. The actual-app driver
builds. A rootless QQuickItem fixture initially failed visibility-resume testing;
giving the fixture a parent correctly exercises Qt's effective visibility.

## Not complete yet

Follow-up: `gaussian-photo-controls-2026-09-09.md` connects focal projection,
exposure compensation and generic tone looks with preview/export parity. It does
not close the physical-emulation gaps below.

The settled view currently uses the native pinhole Gaussian renderer. It is NOT
yet a physical lens/aperture/focus/shutter/ISO simulation or a film-stock response.
The camera/film pass must use the same frozen settings in the settled viewport,
saved shots and exported photographs. Exposure/color-space treatment, depth of
field and temporal shutter integration need real-image correctness evidence.
Do not rename higher-resolution refinement as completed camera emulation.

This is functional live-to-settled behavior, not a measured 60 fps claim. Real
camera-motion latency, reconstruction ghosting, loading delays, physical scale
calibration and genuine 4D training remain open. The larger full-window render
also changes the old page-switch benchmark workload; establish a fresh baseline
before making performance comparisons against the earlier layout.
