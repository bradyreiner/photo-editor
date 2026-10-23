# Looksy assistant: animated CRT robot

## Current direction

The latest user clarification is that the character itself should have a CRT
look and move like a little robot. A static concept or generic CRT test card does
not meet that request. The Loki clock was a rendering reference; both the earlier
mechanical scout and actual clock proposals were rejected.

`LooksyAgentCharacter.qml` now renders a compact robot with a large curved glass
screen, phosphor eyes and mouth, a solid rounded housing, a small torso, jointed
arms, grippers and feet. `agent_robot.frag` evaluates actual three-dimensional
rounded solids with material shading, specular glass reflections and ambient
occlusion. This replaces the prior Canvas optical-surveyor illustration.
It is a procedural GPU character, not a generated PNG, SVG or imported rig.
This is an implemented design for visual review; final art approval is pending.

## Animation and CRT behavior

- Idle head turns and small tilts, automatic blinks and a manual blink hook.
- A raised, moving arm for greeting; articulated limbs for walking.
- A talking mouth, happy eyes, warning color and a working progress strip.
- Progress uses the supplied value; indeterminate work uses a scanning strip.
- Scanlines, phosphor triads, glow and transient displacement are mapped onto
  the curved face and follow the head. `CrtSignalSurface.qml` provides a small
  final bloom and event reaction without scanning the physical housing.
- Animation uses render-thread `UniformAnimator`, without Canvas repainting or
  QML property updates every frame. Reduced motion and inactive/hidden states
  stop the animation; reduced motion retains the static rendered character.

The existing character interface, accessibility name and nominal contact geometry
remain available to callers. Advanced tool deployment/contact alignment is not
qualified by this character preview. The current consumer remains the Agent
walkthrough; no roaming character was added over photographs.

## Preview and verification

Build target: `crt_signal_surface_test`. On macOS this also creates
`build/release-macos/tests/crt_signal_surface_test.app`. Running without arguments
opens **Looksy · Animated CRT robot**, with automatic animation cycling and Idle,
Wave, Talk, Work, Walk, Happy, Glitch, Pause and Reduced motion controls.
The actual native preview was opened, raised and visually inspected.
The displayed working percentage is explicitly fixture data (`0.45`).

With `--verify`, the fixture retains the synthetic CRT comparison and additionally
captures the production robot. Native macOS Metal checks verify compiled solid
geometry, changing head/face pixels, a visibly articulated greeting, moving walk
poses, transparency and stable reduced-motion output. The CRT tests additionally
verify real pixel treatment, beam movement, signal displacement and hidden state.
Evidence lives under `docs/evidence/crt-robot-2026-10-22/` (local ignored evidence).
Software rendering skips GPU checks instead of qualifying them.

These are bounded rendering and animation checks, not Hollywood art approval,
editor-wide performance qualification, or a substitute for the user's live visual
review. The fixture records presented frames over a short preview interval;
that count is diagnostic, not a sustained performance benchmark. The final native
run presented 40 frames in 660 ms at 320 by 370 logical pixels (about 61 fps).

The broader `agent_walkthrough_contract_test` previously failed before its
character clauses on separate conversational plan/Start, Stop/Revert and terminal
contracts. Its expected character version was updated for the new renderer;
those unrelated conversation/session clauses were not changed in this pass.

## Archived concepts

Earlier built-in imagegen proposals and their prompts remain in
`apps/desktop/assets/agent-cgi/concepts/`. They are archived exploration and are
not runtime assets. The manifest identifies the new animated CRT robot as the
current implemented direction, without asserting final user art approval.
