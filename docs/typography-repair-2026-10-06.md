# Typography repair — 2026-10-06

The requested work is a working photo typography editor, a redesigned inspector, and measured performance fixes. Three Luna agents at xhigh effort own the inspector, canvas interactions, and text engine; integration and validation are centralized. Existing uncommitted repository work is preserved.

## Research and decisions

- [Figma text properties](https://help.figma.com/hc/en-us/articles/360039956634-Explore-text-properties) groups family, weight, size, line height, and letter spacing as primary typography controls. [Font browsing](https://help.figma.com/hc/en-us/articles/360041308034-Browse-and-apply-fonts) uses a searchable picker. The revised inspector follows that task order and puts effects, paths, and geometry behind optional sections.
- [Figma resizing](https://help.figma.com/hc/en-us/articles/27378154668951-Adjust-text-dimensions-and-resizing) distinguishes font size from text-box dimensions and wrapping. Keep point and paragraph modes explicit and separate text size from box/perspective controls.
- [Photoshop editing](https://helpx.adobe.com/photoshop/desktop/text-typography/get-started-with-text/edit-text.html) documents direct canvas insertion/selection and an explicit commit action. Preserve canvas Done/Cancel, document identity checks, and retained drafts on rejected commits.
- [Qt TextArea](https://doc.qt.io/qt-6/qml-qtquick-controls-textarea.html) does not scroll itself; embedding it in ScrollView supplies scrolling and clipping. The new multiline inspector content editor uses that arrangement.
- [Qt TextEdit](https://doc.qt.io/qt-6/qml-qtquick-textedit.html) supports persistent selection and native text-document access. Preserve selection when applying inspector formatting and keep document metrics aligned with the committed text engine.
- [Qt QTextLayout](https://doc.qt.io/qt-6/qtextlayout.html) explicitly describes glyph-run retrieval as expensive; it belongs in rendering and ink measurement rather than a routine inspector warning query.
- [Qt font database](https://doc.qt.io/qt-6/qfontdatabase.html) and [font information](https://doc.qt.io/qt-6/qfontinfo.html) are the source for installed faces, fallback diagnostics, and supported variable axes.

## Verified defects and repair scope

The previous inspector exposed a single-line content field and duplicate basic/full typography controls. Core controls move into one typography panel with a multiline copy editor. Undo transactions close on focus loss, layer changes, and lock changes. Box and perspective controls remain accessible under a disclosure.

Native review found that the previous sidebar allocation clipped basic controls at smaller window sizes. Text now receives a larger share of the sidebar, and its duplicate inner header is removed in Studio. Bold, italic, underline, fill, alignment, font face, and fractional size are directly accessible. Font creation and editing share a lazy, virtualized picker with font previews. Exact family matches rank first: searching “Georgia” previously offered “.SF Georgian” ahead of the requested family. Disclosure state follows layer identity rather than a stack index.

The layout-warning query invoked complete text rasterization into an invisible one-pixel image. A layout-only option skips painting and ink bounds for inspector consumers; actual rendering and point fitting keep full rasterization by default. Correctness gates compare reported metrics and preserve output pixels.

Alignment previously used the combined basic text-style setter, which could normalize a translucent fill to RGB. A focused alignment patch preserves saved typography, axes, and ARGB color. The canvas editor also incorrectly excluded typography versions 4–8 from its full base-font path. All supported versions now retain their base font metrics during editing; path/material drafts explain their straight-text editing preview.

Changing a baseline point rebuilt a JavaScript array used as a Repeater model, potentially replacing the handle under the active pointer. A stable numeric delegate model preserves handle identity during the drag.

Text previously shared the generic image-layer scaling gesture. Text now uses eight box handles: edges resize one dimension, corners resize width and height independently, Shift preserves the box's aspect ratio, and Alt keeps the box centered. The atomic geometry update preserves typography size, fill, axes, layer scale, and rotation. A manually resized point-text layer becomes a paragraph box so it can wrap. Opposite-edge/corner anchoring and one grouped undo are regression checks. The canvas status shows width and height for text.

## Performance acceptance

Default loop: at most three narrow attempts, at least 20% improvement on the targeted query, and no layout/raster correctness regression. Timing thresholds are evidence gates, not flaky test assertions. Same-fixture before/after results are stored under `docs/evidence/typography-repair-2026-10-06/`. Query results do not establish whole-app frame timing or full-resolution quality across every font.

The accepted layout-only change reduced the median inspector query from 26.1999 ms to 0.486586 ms (98.1%) on the same long paragraph, with checksum 274740 unchanged. An experimental contour-bin index was reverted because repeated measurements did not show a reliable improvement.

## Validation

All 11 focused tests passed: `photo_text_layout_test`, `photo_text_style_test`, `photo_text_rasterizer_test`, `photo_text_overlay_test`, `creative_typography_styles_test`, `photo_font_picker_ui_test`, `photo_text_content_editor_test`, `canvas_coordinate_mapper_test`, `layer_snapping_ui_test`, `photo_typography_workflow_test`, and `desktop_shell_typography_inspector_test`. [The final CTest log](evidence/typography-repair-2026-10-06/tests.log) records the result. The content editor test covers multiline and empty text, grouped undo, external synchronization, target changes, locking, and rejected changes. Font picker tests cover search, exact-match ranking, keyboard selection, refreshing fonts, no matches, Escape, and disabled controls. Handle tests exercise rotated edge and corner resizing at scale 0.8, anchor preservation, and ordinary layer movement. The five changed standalone QML components passed `qmllint`.

The native Cocoa workflow passed with a photograph cropped to its deterministic 640 × 512 test canvas: point and paragraph creation, growing point bounds, draft composition and cancellation, variable fonts, fallback diagnostics, path/contour editing, material controls, locking, undo/redo, persistence, and actual PNG export. A new controller reopened the saved recipe and produced a pixel-identical exported PNG. The focused canvas overlay test separately verifies native selection state and preservation of typed drafts during style changes. [Workflow screenshots and output](evidence/typography-repair-2026-10-06/native-workflow/) include editable drafts, three installed variable-font fixtures, material output, and final export.

The real Studio inspector was exercised with native mouse/key events at compact, balanced, and comfortable densities, at 1180 × 768 and 1440 × 900. Multiline content, primary formatting, keyboard font selection, expanded geometry scrolling, and float/dock behavior passed. Real pointer drags resized paragraph and point-text boxes while preserving font size, scale, rotation, and style; one-step undo/redo restored exact recipes. [Native inspector captures](evidence/typography-repair-2026-10-06/native-inspector/) show the integrated UI. The macOS app and focused targets built successfully. The rebuilt app is `build/dev-macos-debug/apps/desktop/Looksy.app`; the already-running app was left alone.

Reproduction from the repository root:

```sh
cmake --build --preset build-dev-macos-debug --target photo_editor_app desktop_shell_studio_polish_test photo_typography_workflow_test layer_snapping_ui_test photo_font_picker_ui_test photo_text_content_editor_test photo_text_overlay_test photo_text_layout_test photo_text_style_test photo_text_rasterizer_test creative_typography_styles_test canvas_coordinate_mapper_test -j 4
ctest --test-dir build/dev-macos-debug -R '^(photo_font_picker_ui_test|photo_text_content_editor_test|canvas_coordinate_mapper_test|photo_text_layout_test|photo_text_style_test|photo_text_rasterizer_test|photo_text_overlay_test|creative_typography_styles_test|layer_snapping_ui_test|photo_typography_workflow_test|desktop_shell_typography_inspector_test)$' --output-on-failure
QT_QPA_PLATFORM=cocoa PHOTO_TEXT_EVIDENCE_DIR="$PWD/docs/evidence/typography-repair-2026-10-06/native-workflow" PHOTO_TEXT_TEST_SOURCE="$PWD/docs/evidence/creative-tools/optical-diffusion/full-resolution-review-release/contact-sheet-previews/night-chicago-building-source.jpg" build/dev-macos-debug/tests/photo_typography_workflow_test
QT_QPA_PLATFORM=cocoa PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1 PHOTO_EDITOR_SMOKE_TYPOGRAPHY_ONLY=1 PHOTO_EDITOR_VISUAL_PROOF_DIR="$PWD/docs/evidence/typography-repair-2026-10-06/native-inspector" PHOTO_EDITOR_STUDIO_PHOTO_SOURCE="$PWD/docs/evidence/creative-tools/optical-diffusion/full-resolution-review-release/contact-sheet-previews/night-chicago-building-source.jpg" build/dev-macos-debug/tests/desktop_shell_studio_polish_test
```

These results establish the exercised local workflows and inspector layouts. They do not certify every installed font, every full-resolution composition, or whole-app realtime responsiveness. Formatting applies to the text layer; mixed formatting within a selected character range is outside this repair.
