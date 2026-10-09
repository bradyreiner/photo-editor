# Looksy Edit and Timeline UI polish

This pass implements shared optical control styling, task-focused Edit navigation,
three global density levels, and four Timeline studio layouts. It preserves existing
editing tools and the user's unrelated worktree changes.

## User-visible behavior

- Shared controls use the configured corner radius, a restrained active edge,
  consistent hover/press/focus feedback, and cleaner slider thumbs. Motion follows
  the existing reduced-motion setting. High contrast strengthens control states.
- Comfortable, Balanced, and Compact change control dimensions and spacing across
  the app. Preferences, the Edit Layout menu, and the Timeline View menu expose them.
  Text scale and panel layouts remain independent. Existing Clean/Pro/Inspect
  preferences resolve to Compact/Balanced/Comfortable.
- Edit provides All/Light/Color/Detail adjustment focus. Search composes with that
  focus; All retains every adjustment group, including Lens. Color reveals White
  Balance, Color, Color Grading, Tone Curve, and Advanced Color in their existing
  processing order. A persistent label identifies Entire image, Layer, or Mask.
- Timeline provides Edit/Color/Audio/Finish layouts. Color opens supported clip
  grading and docked scopes; Audio docks the existing mixer; Finish gives direct
  access to captions and delivery. Profile persistence does not reopen modal tools.
- Selected clips and existing linked/grouped badges are clearer. Track headers
  identify locks, show reorder press feedback, and provide larger resize targets.
- The docked mixer uses a compact Tools menu and scrollable strips with dedicated
  meter space. The Audio dock completes loading before opening its mixer; other
  inspectors retain deferred loading. Inspector incubation updates run outside
  Loader active bindings.

## Verification

The dedicated `desktop_shell_studio_polish_test` uses isolated settings, generated
photo/video/audio fixtures, and the actual application QML. It checks density
selection, Color focus/search, retained Lens controls, panel-layout independence,
four Timeline presets, visible mixer/inspector/scopes, bounded editing surfaces,
reduced motion/high contrast, and density/profile persistence across window reload.

The matrix covers 1180×768, 1440×900, and 1920×1080 at all three densities. Receipts
and screenshots are under [studio-polish evidence](evidence/studio-polish-2026-10-03/):

- [Native matrix](evidence/studio-polish-2026-10-03/native/manifest.json)
- [Offscreen matrix](evidence/studio-polish-2026-10-03/offscreen/manifest.json)
- [Panel layout regression](evidence/studio-polish-2026-10-03/panel-layouts/manifest.json)

Both studio matrices contain 49 captures; the panel regression contains 23.
The [verification receipt](evidence/studio-polish-2026-10-03/verification.json)
records the checks and known limits, with test output in the adjacent `logs/` folder.

The QML control group and `app_shell_controller_test` cover existing control
semantics and canonical/legacy density persistence. The desktop app is built with
the macOS Debug preset.

These are UI layout and interaction checks. They do not establish photographic
fidelity, video decoding/playback quality, realtime grading, export parity, or
physical trackpad/VoiceOver behavior. The generated native video fixture's Program
monitor remained black in this harness; media-ready decoded frame delivery is not
claimed by this pass.

A separate `timeline_ui_consistency_contract_test` fails on its existing delivery
queue source expectation: `job_runner.cpp` no longer contains the exact literal
`QStringLiteral("queueOrder")`. This pass does not alter delivery queue behavior.

## Reproduce

```sh
cmake --build --preset build-dev-macos-debug --target photo_editor_app desktop_shell_studio_polish_test desktop_shell_layout_visual_proof_test --parallel 4
ctest --test-dir build/dev-macos-debug -L '^qml$' --output-on-failure
ctest --test-dir build/dev-macos-debug -R '^app_shell_controller_test$' --output-on-failure
ctest --test-dir build/dev-macos-debug -R '^desktop_shell_studio_polish_test$' --output-on-failure
PHOTO_EDITOR_SMOKE_NATIVE_POINTER=1 PHOTO_EDITOR_VISUAL_PROOF_DIR="$PWD/docs/evidence/studio-polish-2026-10-03/native" build/dev-macos-debug/tests/desktop_shell_studio_polish_test
```

Native reproduction needs access to the macOS display. The test needs FFmpeg to
generate its small video/audio fixture.
