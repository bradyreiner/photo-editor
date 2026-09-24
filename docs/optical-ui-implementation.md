# Framehouse optical UI implementation

Status: in progress. Requested 2026-09-19.

Current requirement-by-requirement status: [current audit](optical-ui-current-audit.md). Historical checkpoints below retain their original context.

Current direction: a precision creative tool with restrained layout and deliberate, bold RGB/CMYK moments. Preserve the photographic stage and compact filmstrip. The user rejected cool graphite, little gradients, outlined soft rectangles, selected bottom bars, decorative dashes, swimming-pool caustics, and the earlier typography/information hierarchy. Earlier mockups and checkpoints below are historical evidence, not the current visual acceptance standard.

## Required result

- [ ] Cohesive achromatic surfaces, refined typography, and shared button/focus/selected/disabled states; deliberate RGB additive and CMYK-inspired subtractive moments, without pervasive gradients, borders, or colored panel casts.
- [ ] Preserve the photograph's size and neutral surround. Remove redundant Edit inspector headings and verbose repeated target information; improve adjustment density without shrinking legibility.
- [ ] Tight aspect-aware filmstrip, with correct selection scrolling and thumbnail prefetch.
- [ ] A coherent selection workflow with a compact contextual bar, organized refinement tools, clear target and pending state, and preserved cancellation/history behavior.
- [ ] Clear side-by-side/wipe comparison with usable canvas space and accurate titles.
- [ ] Histogram/scopes with prominent charts, concise controls, explicit analysis domain, and accessible advanced options.
- [ ] Library hierarchy/filter/metadata cleanup using the shared visual system.
- [ ] Delivery-focused Export layout with clear resolved settings, visible primary action, and secondary jobs tray; preserve preflight/file protection.
- [ ] Subtle finite optical completion effects with intentional RGB/CMYK overlap, tied to real completion events and clipped to controls/panels; no watery wave texture, with reduced-motion behavior and no idle work or image mutation.
- [ ] Shared styling adopted across remaining workspaces/dialogs; audit special-purpose color semantics rather than recoloring content.
- [ ] Native builds and focused behavior tests; actual Qt screenshots at representative dimensions, interaction/keyboard review, motion lifecycle and photographic-stage verification. Component or source checks alone do not establish completion.

## Evidence

Pre-change QML and existing worktree status were copied to `/private/tmp/framehouse-optical-baseline` before implementation. Durable runtime evidence will be recorded under `docs/evidence/optical-ui-2026-09-19/`. Preserve unrelated edits in this already modified checkout.


## Implementation checkpoint — 2026-09-19

Implemented in native QML (completion remains unproven until the remaining gates below pass):

- Shared graphite command surfaces, short hover/press transitions, cyan/violet selected edges, neutral numerical fields, round slider handles, and compact white-balance pad.
- One Edit tab heading and target breadcrumb, optional adjustment search, Light before White Balance, advanced Brightness reset corrected.
- Aspect-aware filmstrip cells and viewport-based prefetch.
- Comparison inspector hidden by default with an explicit Adjust toggle. Source controls remain available in linked views; a full-canvas MouseArea no longer blocks those viewports.
- Bottom scopes use four horizontal charts; settings move into a popover. Large scopes use the shared dock content.
- Selection combine/refine controls reduced to two rows, with preview/copy/reset operations retained in a popover. Repeated diagnostic strings are no longer displayed at rest.
- Export presents a current-edit preview and settings above secondary delivery activity. Fixed a preflight delegate's undefined modelData. This preview is not an output-color/crop simulation.
- Library has one interactive rating row, copyable Capture/File details, and a compact action grid. Compare routes to the actual editor. Removed the unwired keyword input; durable tagging is not implemented by this change.
- A finite caustic follows changed slider gestures after a published preview. A completed export-row transition has a matching effect (real-job completion proof still needed).
- Imported alpha masks now map to the canonical immutable raster kind. The adapter test checks payload identity, inversion, and rejection of missing payloads. This exposed a separate native refinement timeout; adapter coverage is not rendering/performance proof.

### Verified evidence

- `photo_editor_app` and `actual_app_workflow_benchmark_driver` built after the main changes. Final rebuild log: `/private/tmp/framehouse-optical-final-build.log`.
- Native `adjustment_pad_interaction_test` passed fine movement, axis locks, modifier transitions, Escape, and pointer cancellation.
- Native `section_comparison_interaction_test` passed toggle, hold/release, cancellation, and collapsed Color Mix.
- `studio_bounded_input_test` exited successfully, but reported an existing null-parent width warning in `StudioOfflineRecoveryBanner.qml`; this is not a warning-free claim.
- `edit_recipe_v13_adapter_test` passed the imported-alpha regression cases.
- `interaction-fixture-2/receipt.json` verifies a pointer-driven exposure change, a finite completion animation, and reduced-motion suppression on the offscreen backend.
- `native-controls/` contains actual Cocoa screenshots and working popover/linked-view checks. That run used the software scene graph, so its black native-texture image stage is not valid image-rendering proof. Its receipt records the pre-fix alpha adapter rejection.
- `native-metal/` contains valid Metal Edit, completion, comparison, linked-comparison, scopes, and scope-popover captures. It stopped at `Selection did not settle` after importing a matte with default refinement. The test process was terminated after the timeout; no completion receipt was produced.
- Earlier `real-photo/` and `layout-pass-2/` captures are software-rendered real-photo layout evidence from an earlier implementation state, not current full-scope approval.

### Remaining work / explicit proof gaps

- Resolve or characterize the imported-alpha native refinement timeout; verify cancel/history and refined masks with a current rendered result.
- Verify the final Library metadata/rating changes, selection popover actions, export completion effect, and real export preflight/write behavior at representative window sizes.
- Review compact layouts, keyboard access, and filmstrip scrolling/prefetch with larger catalogs; right-inspector scope hierarchy now uses the shared dock.
- Review global styling in Carousel, Timeline, Scene, menus, dialogs, and remaining special controls.
- Expand the restrained additive/subtractive optical treatment beyond the current spectral edges and finite caustic, without coloring the photo surround.
- Measure idle/motion costs and review a recording; a screenshot and completion counter do not establish animation quality or performance.
- Final native real-photo review remains open. Automatic approval review rejected an escalated native run with the private managed RAW source; subsequent native checks use generated synthetic fixtures only. Do not retry that private-source action without authorization.

The full goal remains active. No broad requirement is marked complete from a narrow test or an intermediate screenshot.

### Follow-up diagnostic evidence

The zero-feather/zero-smooth attempt in `native-unrefined/` also timed out. A one-second sample of that isolated process (`/private/tmp/framehouse-optical-mask-sample.txt`) located the active render worker in `compile_pipeline_photo_layers` → `rasterize_layer_selection_alpha` → `apply_raster_selection_morphology`, including median and local-average work. This is CPU mask preprocessing during native rendering, not evidence of a caustic or GPU shader hang. The next harness revision uses the existing Reset refinement action and explicitly labels that narrower fixture state; the default-refinement timeout remains an open gate regardless.

A subsequent rebuild encountered an unrelated missing `QRgbaFloat32` definition in the concurrently edited `photo_pixel_stroke.h`. The concurrent paint change then supplied the defining `QtGui/qrgbafloat.h` include. Removed my redundant forwarding-header include; paint behavior was not changed. The latest rebuild is `/private/tmp/framehouse-optical-reset-build.log`.

### Latest successful native checkpoint

`native-reset-matte-2/receipt.json` is a successful Cocoa/Metal run with ten screenshots. It verifies the pointer-driven exposure change, finite caustic, reduced-motion suppression, linked comparison controls, scope settings, selection options, Library, and Export layouts. The imported matte renders without a reported failure after the normal Reset refinement action. Its general feather setter clamps to 5 percent, as shown in the screenshot; zero was requested, not achieved. This does not close the default-refinement performance gap above.

The selection palette now fits its content instead of obscuring the full photograph height. Export uses the current native texture when available. Library filter dropdowns now use one integrated chevron, preserving labels and opening choices on click. The last QML build is `/private/tmp/framehouse-optical-menu-build.log`; the final checkpoint run is recorded separately in `native-final-checkpoint/`.

`native-final-checkpoint/` completed successfully with ten Cocoa/Metal screenshots and no QML runtime errors or presented render failures found in its log. This includes the corrected Library dropdowns. The receipt was produced before its descriptive feather text was corrected: the screenshot's 5 percent is authoritative. The harness now records actual feather/smooth values in future receipts. The receipt-only correction rebuilt successfully in `/private/tmp/framehouse-optical-receipt-build.log`; production QML built successfully in `/private/tmp/framehouse-optical-menu-build.log`.

No commits, catalog resets, source-photo edits, or exports to the user's destination were performed by these UI checks. Recipe persistence was disabled for the optical proof mode.

### Scope inspector consolidation

The right inspector now uses `StudioScopeDockContent`, matching the bottom/left/large docks. Four charts fit in the inspector while domain, proof status, and update state remain visible. The settings popover includes the previously missing linear/log histogram toggle in every placement. Its position is calculated when opened from the actual Settings button, avoiding a stale upper-left origin. Global edit Reset/Save Preset actions are hidden in Scopes and Info. Missing camera/lens/date metadata now reads Not available instead of invented example data.

Production build: `/private/tmp/framehouse-scope-popover-build.log`. Native Cocoa/Metal evidence: `native-scope-final/`, twelve screenshots and receipt. The driver exercises opening inspector settings and toggling histogram scale in addition to the previous pointer-edit, finite completion, reduced-motion, comparison, selection, Library, and Export checks. Visual inspection confirms the anchored popover and removed footer. The earlier `native-scope-unification/` run failed its obsolete ten-capture expectation after adding two captures; it is not a passing run. The final harness expects twelve. Generated fixtures only, with recipe persistence disabled. This does not close compact-window, real-export, default-mask-refinement, or remaining-workspace gates.

### Responsive-window pass

The native proof driver accepts `--optical-width` and `--optical-height`. The 1100x760 run (`native-compact/`) completed with twelve captures. It showed that the full inspector used unnecessary photo width at small sizes. Below 1120 pixels, Edit now uses its existing 356-pixel compact inspector. The initial minimum-size capture then exposed an overflowing tab row; compact inspectors retain full text labels while omitting their redundant icons, keeping Info reachable. Wide inspectors retain icons.

The minimum-size proof runs at the application's declared 1024x720 minimum. Evidence directories `native-minimum/` and `native-minimum-final/` distinguish the first capture from the corrected tabs. This pass covers the existing twelve fixture views and pointer/caustic/settings interactions; it does not verify every scroll, keyboard path, export write, or larger catalog.

Final minimum-size run exited 0 with twelve captures, no selected QML/runtime failure patterns in its log, and no selection render failure in its receipt. Visually checked `native-minimum-final/01-edit.png`: all inspector tabs, including Info, fit. Production rebuild `/private/tmp/framehouse-minimum-final-build.log` exited 0. Native log: `/private/tmp/framehouse-minimum-final-proof.log`.

### Completion lifecycle proof

`CompletionCaustic` now exposes whether its temporary Canvas is allocated and a paint counter for runtime verification. The native driver confirms that an actual committed edit paints, finishes, unloads its Canvas, and produces no additional paints during a 300ms idle observation. Reduced motion remains covered. A real `ExportQueueRow` component instantiated with simulated job states verifies: preexisting Completed history does not animate; Running to Failed does not animate; Saving to Completed animates and releases its Canvas. No export is queued or written by this fixture. The thirteenth capture is explicitly labeled as simulated feedback.

Evidence: `native-motion/receipt.json` and `04b-export-completion-fixture.png`; build log `/private/tmp/framehouse-motion-build.log`, native log `/private/tmp/framehouse-motion-proof.log`. This establishes component lifecycle and job-state gating, not real export pipeline integration, frame-time performance, or motion aesthetic quality. Those remain separate open gates.

### Cross-workspace entry surfaces

Expanded the native proof to sixteen captures, adding Carousel setup, Timeline setup, and Scene entry. These are entry-state captures, not proof of populated editing workflows. The review found remaining yellow Timeline navigation/setup accents and crowded Scene action labels. Workspace navigation now shares `interaction400`; Timeline setup uses the same accent. Warning colors remain semantic warnings. Shared `ActionButton` no longer subtracts padding twice from its content row, fixing the crowded labels without changing actions.

Before/after evidence: `native-workspaces/` and `native-workspaces-final/`. The final receipt has sixteen captures, with the preceding edit/completion/scope/selection checks retained and no selection failure reported. Visually inspected the corrected Scene buttons. Production build `/private/tmp/framehouse-workspaces-final-build.log` passed. Native log `/private/tmp/framehouse-workspaces-final-proof.log` contains none of the checked ReferenceError, TypeError, binding-loop, or presented-failure patterns. Populated Carousel, Timeline, and Scene workflows still require visual review; the broad redesign remains active.

### Populated Carousel review

The fixture now creates three boards and places its generated image on the first board; capture `07b-carousel-composer` supplements the entry screen. The first populated capture (`native-carousel/`) exposed an expanding control column in the board filmstrip that left virtually no width for thumbnails, plus clipped background-mode choices. The strip's control columns now have explicit minimum/maximum widths (120 standard, 36 compact), reserving the remaining width for thumbnails. Background modes use a two-column grid while preserving their original labels and actions. Document colors and content are unchanged.

Production and fixture-driver build: `/private/tmp/framehouse-carousel-final-build.log`. Updated evidence: `native-carousel-final/`. This remains a generated-image layout fixture, not a full populated-Carousel interaction or export audit.

Final Carousel capture visually confirms all three thumbnails plus the add-board tile, and fully readable Solid / Horizontal gradient / Vertical gradient / Transparent choices. The native receipt contains seventeen captures with no selection render failure. The wider Carousel hierarchy remains a follow-up; this pass fixes the verified layout defects.

### Carousel hierarchy and shared menu surfaces

Carousel's 48-pixel rail now uses centered icons and existing full tooltips/shortcuts rather than overlapping 6–7px labels. Its active material uses the same `CommandButtonSurface` as the other tools. Board range and document structure commands moved to the Board Properties More menu; routine add/duplicate/copy/paste and board settings remain visible. Inspector section More buttons now open their menus from the main button, not only a split arrow. Shared popup/menu backgrounds are opaque graphite so underlying controls do not compete with menu labels.

The native fixture now opens the board menu, waits for its opening transition, captures it, invokes its range action, and verifies the board-range dialog opens. This proves the relocated action path, not a board-range mutation. Evidence: `native-hierarchy-final/` (eighteen captures expected), compared with `native-carousel-hierarchy/`. Build `/private/tmp/framehouse-hierarchy-final-build.log` passed. The broad design goal is still active.

Visually checked the final menu capture: opaque labels, no overlapping rail text, and the relocated range action opens successfully. The native log also contains an unrelated Timeline warning at `TimelineView.qml:10584` (`captionSegmentationPreview.schema` assigned undefined to bool); this run must not be described as warning-free. Timeline has concurrent changes and that warning is recorded for the next focused audit.

### Populated Timeline review

The fixture creates a 1920x1080 timeline with a generated still and title clip, adding `08b-timeline-editor` to the native evidence. The optional caption-preview schema visibility check now explicitly converts to bool, removing the undefined-to-bool warning recorded in the previous pass. The populated capture exposed mid-word effect-name wrapping in the narrow inspector. `TimelineEffectLibraryRow` now gives the thumbnail a smaller 52px width, stacks favorite/Add in a bounded action column, and shows the redundant category label inline only at wide widths (category remains available through filtering and accessibility text).

Before/after evidence: `native-timeline-layout/` and `native-timeline-effects/`. This is a still/title layout fixture, not playback, audio, or timeline-export verification. The user-facing labels, add/favorite signals, drag behavior, effect thumbnails, and availability gates are preserved. Production build `/private/tmp/framehouse-timeline-effects-build.log` passed.

Final populated-Timeline capture visually confirms full effect names and readable descriptions. The receipt has nineteen captures. The caption undefined-to-bool warning is absent, but the log contains repeated InspectorSlider.qml:311 parent-width TypeErrors during Timeline construction/teardown; this is a newly exposed verification defect, not a warning-free pass. This does not establish smooth playback or full Timeline correctness.

Follow-up: guarded the slider tick delegate's parent-width binding while detached. `studio_bounded_input_test` exited 0 against the source QML; it still reports the separately known StudioOfflineRecoveryBanner parent-width warning. Full native re-verification after the tick guard remains open. The production rebuild is `/private/tmp/framehouse-slider-guard-build.log` and had expanded to rebuild concurrent tree changes; do not infer its result from the preceding successful build.

The expanded production rebuild subsequently exited 0 (`framehouse-slider-guard-build.log`). The post-guard native warning recheck remains open.

### Native caustic recording and slider recheck

The post-guard native run (`native-slider-recheck/`) no longer reports the InspectorSlider tick parent-width failure. Other teardown warnings appeared in TimelineAudioMeter, so it is not a warning-free overall run.

The proof driver now accepts `--optical-record-caustic`. After verifying a real pointer edit's completion lifecycle, it explicitly replays the same effect and captures 25 native window frames across 992ms, preserving timestamps and animation phase in `native-caustic-recording/caustic-frames/timing.json`. `caustic-native.mp4` uses those observed frame durations. This is a native replay demonstration, not a realtime frame-budget benchmark: screenshot capture adds overhead. The sampled phase progresses from 0 to 1 and stays idle for the remainder; the middle frame shows the restrained refracted lines in the target bar. Production behavior was not changed by the optional recording path. Driver build and video encoding passed. User visual-direction feedback was requested and remains pending.

### Optical material blending

Theme now defines `opticalLight` (screen blend) and `opticalInk` (multiply blend) for display-space UI materials. Shared command surfaces use the light blend at the cyan/violet accent intersection and a restrained multiply tint at the lower filled edge. Unfilled/idle controls retain their neutral surface. These functions are not photographic color transforms or spectral simulation. Completion caustics add a faint source-over dark shoulder beneath the additive light paths, preserving the finite duration and reduced-motion behavior.

Production build `/private/tmp/framehouse-optical-material-build.log` passed. Updated native recording: `native-optical-material/caustic-native.mp4`, with 25 timestamped frames over 980ms; visually inspected the middle frame. Runtime receipt retains the actual edit completion, no-idle-paint, export-state gating, and reduced-motion checks. This does not close aesthetic acceptance, real-export integration, or frame-budget profiling. Timeline teardown warnings remain a separate open issue.

### Accepted direction: more futuristic and vibrant

User explicitly chose "More visibly futuristic and vibrant" on the visual-direction question. Updated interaction colors to brighter cyan, violet, and magenta; active control surfaces receive a restrained cyan top tint and a three-color lower edge with the screen-blended intersection. Caustic paths now use all three spectral colors rather than a white third path, with intensity raised from 0.22 to 0.38 (export row 0.18 to 0.30). The effect remains finite and reduced-motion aware. Large neutral surfaces and photo pixels retain their established treatment. This supersedes the earlier pending-feedback note; restrained neutrality is no longer the default direction for interaction accents.

Verification/artifact directory: `native-vibrant/`. Build log `/private/tmp/framehouse-vibrant-build.log`, native log `/private/tmp/framehouse-vibrant-proof.log`.

The vibrant-direction production build passed; the native receipt contains nineteen captures and passes finite completion, idle-paint stability, export-state gating, and reduced-motion checks. No errors were logged from the changed optical components. The revised recording is `native-vibrant/caustic-native.mp4`. Other previously identified proof gaps remain open; this is an implemented direction change, not full goal completion.

### Canvas reset behavior

The canvas toolbar Reset button now resets only preview zoom/framing. Previously it also reset all image adjustments, despite being grouped with Fit, Fill, and zoom controls. Full adjustment reset remains in the inspector. The native proof driver now performs an exposure edit and clicks the visible canvas Reset button, asserting exposure remains unchanged (`viewResetPreservedExposure`). Production and driver build passed (`/private/tmp/framehouse-view-reset-build.log`). Evidence: `native-view-reset/` at 1100x760; its receipt confirms the preservation check and existing completion checks. The native process exited 0 after teardown.

### Export density and real delivery integration

Removed the redundant asset-count card from Export's selection section, retaining one selection summary and the applicable video/timeline explanation. The pinned delivery review now shows the output path once rather than repeating the filename before it. At 1100x760 the settings remain scrollable and Start Export stays pinned; destination controls still require scrolling at that height.

Native evidence: `native-export-density/`, nineteen captures, process exit 0. Visually inspected `04-export.png`; completion, idle-paint stability, export-state gating, and reduced-motion checks pass. TimelineAudioMeter teardown warnings remain present. Build log: `/private/tmp/framehouse-export-density-build.log`.

Rebuilt and ran `export_job_controller_test --keep-artifacts`: exit 0, including delivery verification and all assertions. It uses generated inputs and an isolated temporary catalog/output directory, and covers durable recovery, preflight, delivery, collision preservation, and failed-destination retry. Log: `/private/tmp/framehouse-export-integration-test.log`; artifacts: `/var/folders/qx/ck73lj3j7fl5tk_glls1rrq40000gn/T/export-controller-proof-DIMARd`. This establishes backend integration separately from the simulated ExportQueueRow completion-state checks; a real queued export triggering the native caustic remains unverified.

### Compact canvas and selection space

Canvas toolbar wrapping now uses the combined measured widths of the zoom and action groups plus their margins, instead of an unconditional 760px breakpoint. At 1100x760 both groups fit in one row, reclaiming the second toolbar row. Reopening an existing mask at small window sizes starts with the selection palette collapsed; its expansion and family controls remain available. New selections and wide windows retain the expanded palette.

Production build and native run exited 0. Evidence: `native-selection-space/` (nineteen captures); visually compared `05-selection-fixture.png` with `native-view-reset/05-selection-fixture.png`. Completion, preserved-exposure reset, and selection render checks pass. Logs: `/private/tmp/framehouse-selection-space-build.log` and `/private/tmp/framehouse-selection-space-proof.log`. Existing TimelineAudioMeter teardown warnings remain; this pass does not establish mask refinement performance or all keyboard interactions.

### Compare hierarchy and bypass semantics

Compare now has one workspace heading and a labeled Bypass menu replacing four unexplained adjustment-group buttons. The menu retains checked group states, displays a selected count, and explains that bypass affects Before while After retains the current edit. It is disabled for snapshot/external sources, where those group choices do not drive the reference render. The Before title identifies bypassed groups instead of incorrectly saying Original. Removed the redundant arrow summary above the already labeled images; shortened the source label.

Production and proof-driver builds passed. Native evidence: `native-compare-hierarchy/`, twenty captures including the open bypass menu. The fixture opens the menu, toggles Tone, asserts the controller state and Before title change, then restores the group and exercises linked views. The native receipt confirms the full fixture completed, and the process exited 0 after cleanup. This is UI routing/title verification, not numerical reference-render parity. Existing TimelineAudioMeter teardown warnings remain. Logs: `/private/tmp/framehouse-compare-hierarchy-build.log`, `/private/tmp/framehouse-compare-driver-build.log`, `/private/tmp/framehouse-compare-hierarchy-proof.log`.

### Native keyboard menu behavior

Replaced the bypass fixture's direct action calls with native key events: Space opens the focused menu, Down reaches Color, Return activates it, and Escape dismisses a reopened menu while preserving its checked state. The first run (`native-keyboard/`) failed activation. Added explicit Return/Enter handling to `PopupMenuRow`, suppressing autorepeat. The next run (`native-menu-enter/`) changed state but failed dismissal. `MenuPopover` now closes before dispatching its action, and the row consumes the key before emitting activation, so a checked-state update can safely rebuild the originating delegate.

Production and driver builds passed. The final run is `native-menu-dismiss/`, with `bypassKeyboardNavigation` recorded only after the key-event checks pass. This covers the shared menu through Compare, not every application keyboard path. Runtime logs continue to expose separate Timeline warnings, including an undefined QColor at TimelineView.qml:9170 in this run. Build log: `/private/tmp/framehouse-menu-dismiss-build.log`; native log: `/private/tmp/framehouse-menu-dismiss-proof.log`.

The final keyboard run exited 0 after cleanup and produced twenty captures. Its receipt confirms keyboard navigation alongside the existing completion and preserved-exposure reset checks.

### Theme reference and lifecycle cleanup

Replaced undefined theme tokens in Timeline, Carousel, and shared layer/brush surfaces with established semantic tokens: graphite surfaces, cyan selection/action, muted text, error red, and existing radius/scale helpers. A static scan of direct `theme.*` references against Theme.qml properties/functions now reports zero unresolved names; this does not analyze dynamically passed theme objects or prove every hidden dialog rendered.

Guarded TimelineAudioMeter delegate geometry and normalization while its root/parent is being destroyed, plus the recovery banner parent-width binding. The focused bounded-input test exits 0 without the earlier recovery-banner TypeError. The initial native cleanup run exited 0 and no longer logged the identified audio-meter/color binding failures.

Final production build passed (`/private/tmp/framehouse-theme-tokens-build.log`). Native evidence: `native-theme-tokens/`, twenty captures including the populated Timeline; the receipt retains completion, keyboard menu, and exposure-preservation checks. No TypeError, ReferenceError, undefined-assignment, or binding-loop messages were found in the final run at receipt creation. Catalog-helper warnings in the isolated fixture are separate; this is not an overall warning-free claim. Individual hidden paste-attribute and Carousel detail dialogs still need visual inspection. Native log: `/private/tmp/framehouse-theme-tokens-proof.log`.

Final theme-token native run exited 0 after cleanup; populated Timeline capture was visually inspected. The specified QML binding-error patterns remain absent in the completed log.

### Consolidated selection inspector

When selection controls are active, the shared target bar now shows the parent layer and mask name with a selection icon. The redundant selection summary card is hidden in that inspector, removing repeated feather/density/inversion values; the Layers presentation retains its separate summary. Lock notices now follow semantic selection state rather than the old card visibility, and fallback notices remain separately visible.

Production and driver builds passed. The native fixture verifies the breadcrumb contains the active mask name, the duplicate card is hidden, and a locked mask exposes both its notice and a working Unlock action. `native-selection-target/` exited 0 with twenty-one captures; its first selection capture includes a hover tooltip. `native-selection-target-final/` moves the test pointer off controls and waits for the locked preview before capturing. Build logs: `/private/tmp/framehouse-selection-target-build.log`, `/private/tmp/framehouse-selection-target-driver.log`; native log: `/private/tmp/framehouse-selection-target-final.log`. The latter build reports an unrelated existing nodiscard warning in editor_preview_controller.cpp.

The final selection-target run exited 0 with twenty-one captures. Visually inspected the tooltip-free selection inspector; lock notice/unlock assertions passed. A separate Carousel dialog warning (`Keys` attached to a Dialog rather than an Item) appears in this run and is not covered by this selection change.

### Queue identity and completion feedback

Found a real integration defect while tracing export completion: ordinary and synchronous history refreshes reset JobQueueModel, recreating QML rows and discarding their Running-to-Completed observation. Both paths now publish records by stable job ID with row insert/remove/move notifications and data changes. This also preserves focused row controls during progress/history updates.

Expanded `job_queue_loading_test` to verify a persistent index survives insertion, reordering, removal of another job, and completion without a model reset. The isolated native test passed, together with its existing coalescing, stale-result rejection, history preservation, and nonblocking-close checks. Its sandbox run failed the preexisting outage-heartbeat expectation because the helper was denied immediately; the escalated test exercised the intended outage and passed. Log: `/private/tmp/framehouse-job-identity-test-native.log`. Production/driver builds passed.

The native fixture now drives the production JobQueueModel and real Export workspace delegate with explicitly simulated Running/Completed records. `native-job-row/` exited 0 and verifies the same delegate survives and receives one completion. The first attempt (`native-job-identity/`) used an insufficient QObject-only lookup; using the existing visual-tree lookup resolved that fixture issue. Final drawing/surface-release verification runs in `native-job-row-final/`. This is stronger model-to-UI integration evidence, but still not a file-export-to-animation proof; actual backend exports were tested separately.

The final model-to-row fixture produced twenty-two captures and passed its paint-count, animation-progress, and surface-release checks. Visually inspected `native-job-row-final/04c-queue-completion-fixture.png`; the capture is labeled as simulated and no file was exported by it.

### Real native export completion

Added opt-in `--optical-real-export` to the native driver. It forces temporary app-data/cache/catalog roots, clears any inherited database URL, starts durable runner recovery, clicks the visible Start Export control into a temporary destination, and checks the actual completed row and output file. Its scope guard stops only its owned temporary catalog after controller teardown. No private library input or user export destination is used.

The first real run (`native-real-export/`) exported successfully but did not animate: coalesced history updates can omit Running. ExportQueueRow now recognizes observed Queued/Paused/Canceling as unfinished states as well as Running/Saving, so their transition to Completed triggers feedback. Initial completed history and Failed/Canceled outcomes still do not trigger it.

Final native run `native-real-export-transition/` exited 0 with twenty-three captures. It verifies a pointer-driven real export, exactly one completion event, actual caustic painting/progress, and drawing-surface release. Preserved output `exported-fixture.png` is a readable 1800x1200, 16-bit PNG; `real-export-verification.json` records its hash and proof scope. Visually inspected `04d-real-export-completion.png`. This closes the previously open real-file-export-to-animation integration gate for this generated photo fixture; it does not prove real-photo color fidelity, all batch/video cases, or animation frame budgets. Build: `/private/tmp/framehouse-real-export-transition-build.log`; native log: `/private/tmp/framehouse-real-export-transition-proof.log`.

### Delivery summary hierarchy

Real export evidence exposed duplicate preset and completion-count copy. Delivery activity now has one header summary with Clear Done beside it; removed the additional saved-job count. Rows show their preset once, with job kind/output profile in the subtitle. Aggregate progress appears only for unfinished multi-job deliveries; completed rows retain their own result. A single completed job uses a 210px tray instead of 280px, returning space to the preview.

Production build passed. `native-delivery-summary/` exited 0 with twenty-three captures and a real generated-image export, preserving the completion-paint and surface-release assertions. Visually inspected `04d-real-export-completion.png`: the duplicate copy is gone and the completed row fits at 1100x760. No TypeError, ReferenceError, or undefined-assignment patterns appear in this run; other previously recorded warnings are not broadly cleared. Logs: `/private/tmp/framehouse-delivery-summary-build.log` and `/private/tmp/framehouse-delivery-summary-proof.log`.

### Authorized real-photo visual review

The user explicitly authorized IMG_4440.CR2 for native review. `native-real-photo-current/` contains twenty-two captures and a completed receipt with recipe persistence disabled. No real export was requested in this run. The native process has terminated; its exit code was not recovered after context compaction. The receipt verifies pointer exposure editing, view reset preserving exposure, bypass keyboard navigation, finite completion, stable idle painting, reduced-motion suppression, and selection rendering without a reported failure. Selection uses the documented synthetic ellipse, not AI subject segmentation.

Visually reviewed Edit, Compare, Delivery, and Selection captures with the actual photograph. The neutral image stage and consolidated inspector remain legible; Compare gives the photographs substantially more width. Delivery still has a gold existing-file notice and a crowded review area, which remain visual cleanup candidates. This review does not establish photo color accuracy or animation performance; runtime logs contain frame-budget overruns and fixture catalog warnings.

The separate 1024x720 generated-image run (`native-minimum-current/`) stopped at “Carousel board menu did not finish opening.” It has no completed receipt and must not be counted as a passing minimum-size workflow. Logs: `/private/tmp/framehouse-real-photo-current-proof.log` and `/private/tmp/framehouse-minimum-current-proof.log`.

### Direction correction after explicit visual rejection

The user rejected the beveled soft rectangles, outlines, multicolor bottom bars, cool graphite cast, wave-like caustics, typography, decorative dashes, and information hierarchy. Earlier optical/vibrant checkpoints are functional evidence only, not visual acceptance. The complete design objective remains open.

Removed gradients, highlights, and bottom accents from CommandButtonSurface; changed base surface/text neutrals to grayscale; ordinary ActionButtons now rest without filled/outlined boxes. InspectorSection defaults use sentence-case UI headings without decorative markers. Delivery details are expandable (open on blocking issues), while the pinned summary uses an adaptive height and filename, with a cyan numbered-copy notice instead of gold.

Production and driver build passed (`/private/tmp/framehouse-neutral-build.log`). Native generated-image run `native-neutral-reset/` exited 0 with 22 captures. Visually inspected Edit: shared button bevels and stripes are gone, but local blue selections and adjustment-header dash controls remain. The existing wave animation is unchanged and remains rejected. This is a foundation reset, not a completed new design or RGB/CMYK implementation.

### Neutral Edit follow-through and motion experiment

MiniButton selected fills now use neutral ink750; StudioInspectorGroup and SectionHeader no longer add decorative dash markers. CompletionCaustic replaces all sinusoidal paths with three straight CMY registration strips using multiply overlap over a narrow paper field. This is an unfinished motion experiment, not physical ink simulation or the full requested additive/subtractive language. Native middle-frame inspection shows insufficient color visibility, so aesthetic acceptance is explicitly open.

Build passed (`/private/tmp/framehouse-registration-build.log`). Native `native-registration/` produced a receipt and timestamped frames; finite completion, idle painting, and reduced-motion assertions pass. Visually inspected frame-010: neutral selected tabs and unadorned adjustment headers are visible. Typography and information architecture still require substantive design work.

### Platform typography and quieter numeric controls

Theme now obtains its UI/display family through AppShellController.systemUiFontFamily, backed by QFontDatabase::systemFont(GeneralFont), with a standalone QML fallback. The initial SF internal-name attempt produced a missing-family warning and was replaced. InspectorSlider values use medium UI text rather than bold monospace; idle fields are transparent, hover reveals a neutral surface, and keyboard focus retains the explicit focus border. Typed input semantics and fixed columns are preserved.

Production and native driver build passed (`/private/tmp/framehouse-system-type-build.log`). Native `native-system-type/` exited 0 with 22 captures; inspected Edit for label/value readability and removal of idle boxes. No missing .SF NS warning remains; a separate System font warning appears when Carousel opens, so this is not a warning-free claim. The standalone exposure interaction test passed numeric entry, fine drag, endpoints, mapping and cancel (`/private/tmp/framehouse-type-input-test.log`); it has an unrelated Sans Serif alias warning. Full typography, information architecture, and motion aesthetic acceptance remain open.

### Inspector target hierarchy and reset noise

The active target is now a 36px transparent header instead of a 44px outlined card, retaining its target name, actions and completion owner. Unmodified slider reset controls have zero opacity while retaining their fixed layout column; they were already disabled at default values. Modified values retain visible, keyboard-accessible reset actions.

Production/driver build and exposure numeric/gesture test passed. Native `native-inspector-hierarchy/` exited 0 with 22 captures; inspected Edit for alignment and the compact target header. Existing native selection/locked-target and completion assertions still pass. Logs: `/private/tmp/framehouse-inspector-hierarchy-build.log`, `/private/tmp/framehouse-inspector-hierarchy-proof.log`, `/private/tmp/framehouse-hierarchy-input-test.log`. This closes only this local hierarchy change; the broader design and motion requirements remain open.

### Selection language and contextual actions

Selection inspector labels now read Selection, Outline, Density and Opacity. The former Show/Hide Selection action toggles activeMaskEnabled (mask contribution), not overlay visibility; its label now explicitly reads Enable/Disable mask. Overlay visibility retains its separate Hidden mode. The global Reset/Save Preset footer is hidden while mask controls own the inspector.

Build passed. Native `native-selection-language/` produced 22 captures and passed existing selection render/lock recovery and completion assertions. Visually inspected 05-selection-fixture: shortened labels fit and the unrelated footer is absent. The remaining multiple toolbar areas and action-state hierarchy still need design work. Logs: `/private/tmp/framehouse-selection-language-build.log`, `/private/tmp/framehouse-selection-language-proof.log`.

### Responsive Selection context toolbar

Grouped creation and refinement in one GridLayout. At available widths of at least 1060px they share one row; narrower contexts stack with no intervening panel spacing. Removed separate toolbar backgrounds/borders, aligned group captions vertically, removed redundant combine icons, and shortened the secondary popup label to Options. Existing actions, recovery controls, and popup contents remain wired. Wrapper visibility follows active tool state directly to avoid a parent/child effective-visibility dependency.

Final production/driver build passed (`/private/tmp/framehouse-selection-toolbar-final-build.log`). Native wide and 1100x760 compact runs produced 22 captures each; inspected both Selection captures. Wide view uses one contextual row and returns vertical space to the canvas; compact controls stack without clipping. Options popup and existing lock/unlock checks pass. Evidence: `native-selection-toolbar/`, `native-selection-toolbar-compact/`. This does not close the previously failed 1024x720 Carousel menu gate or broader design acceptance.

### Deterministic RGB/CMY completion registration

Replaced per-draw transparency with a whole-Canvas fade and introduced explicit RGB light and CMY process primitives. The initial native multiply probe did not render the expected ink overlaps; the final implementation computes intersection colors directly (clamped RGB sums and idealized CMY transmittance products), avoiding dependence on Canvas multiply composition. It draws two narrow registering bands, with no sinusoidal waves, panel gradients, or persistent animation. This is display-space illustrative mixing, not a print/color-management transform.

Isolated full-intensity native proof `registration-overlap-probe.png` shows white additive and black subtractive centers with colored edges. Final build passed (`/private/tmp/framehouse-explicit-blend-build.log`); `native-explicit-blend/` exited 0 with 22 captures and timestamped motion frames. Finite completion, idle-paint stability, and reduced-motion checks pass; inspected frame-006 at normal UI size. Prior real-photo run `native-registration-blend/` exited 0 but predates the explicit-overlap correction and is not final blend proof. The normal-size effect remains restrained; visual acceptance and broader interaction-color design remain open.

### Minimum-window drawer workflow and chip consistency

The previous 1024x720 Carousel menu failure was a proof-driver sequencing error: the compact inspector lives in a closed drawer. The driver now clicks the visible Inspector control, waits for the drawer, captures it, then exercises board actions and closes the drawer. Receipt records compactCarouselInspectorVerified and includes the conditional extra capture in its expected count. The initial corrected run reached every interaction but failed its old fixed capture count; final verification includes that correction.

The new drawer evidence exposed FilterChip's separate colored-underlining style. Removed that underline and use a neutral selected fill for neutral chips, retaining semantic non-neutral tone fills and keyboard focus. Final build passed (`/private/tmp/framehouse-minimum-chip-build.log`). `native-minimum-final/` contains 23 captures, a true compactCarouselInspectorVerified receipt, and passes the native workflow assertions. Visually inspected the drawer: controls fit and the old underline treatment is absent. This closes the previously recorded minimum-window driver failure, not exhaustive responsive, performance, or aesthetic acceptance.

### Filmstrip off-screen loading and selection proof

Expanded the generated native fixture from 8 to 24 mixed-orientation photos and named the filmstrip ListView for targeted verification. The driver positions the strip at its end, waits for the last Image to reach Ready, clicks its visible card, verifies catalog selection, settles the preview, and returns through the normal selection function to index zero. Receipt records filmstripScrollVerified and captures the scrolled state.

The initial QObject descendant lookup missed virtualized visual delegates and timed out; replaced it with traversal through the ListView content item's visual children. Also corrected the return call's required modifier argument and added a selected-index assertion. Final driver build passed; `native-filmstrip-visual/` exited 0 with 23 captures and filmstripScrollVerified=true. Visually inspected 01c-filmstrip-scrolled: last portrait selected, compact mixed-aspect spacing retained. This verifies off-screen loading and pointer selection after programmatic positioning; it does not measure wheel/touchpad smoothness or prove large-catalog performance. Logs: `/private/tmp/framehouse-filmstrip-visual-build.log`, `/private/tmp/framehouse-filmstrip-visual-proof.log`.

### Scope framing and axis readability

StudioScopeTile now uses an unboxed transparent surface and clearer section labels. Waveform/parade plots reserve 7px above and below their plotting range, keeping middle-baseline 100/0 labels inside the Canvas. Axis type increased from 8px faint monospace to 10px UI text in textMuted. Density/average drawing uses the same inset coordinates as the grid; measurement arrays and value mapping are unchanged.

Build passed (`/private/tmp/framehouse-scope-layout-build.log`). Native `native-scope-layout/` exited 0 with 23 captures and passing workflow assertions. Visually inspected 03-scopes and 03c-inspector-scopes at 1100x760: endpoints are fully visible and tile frames are absent. This is UI geometry/readability verification, not numerical validation of the analysis engine. Broader design acceptance remains open.


### Precision-tool direction and Delivery hierarchy

The user selected “Precision creative tool: restrained layout, bold RGB/CMYK moments.” This is direction, not acceptance of the current design. Delivery selection count moves into its panel subtitle; the extra Selection section remains only for mixed video/timeline warnings. Preset metrics use UI text and lose their outer frame. Color-profile guidance is shorter.

Production and driver builds passed. Native `native-delivery-layout/` exited 0 with 24 captures, including a real generated-image export in isolated temporary storage. Screenshot inspection exposed half-width stacked color/crop controls. The responsive column decision now reads the parent width; `native-delivery-grid/04-export.png` shows full-width stacked controls with destination and Start Export visible. The intended two-column density is not verified and remains open. Latest build log: `/private/tmp/framehouse-delivery-grid-build.log`. This is a local hierarchy improvement, not completion or visual acceptance of the broader redesign.

Final corrected native run `native-delivery-grid/` exited 0 with 23 captures and passing workflow assertions. Its compact screenshot was inspected directly.


### Shared control consistency and stable Delivery row

MiniButton ordinary actions now rest on a transparent surface without borders; primary/selected states retain neutral fill, destructive states retain their semantic border, and keyboard focus remains explicit. ToolButton likewise reserves its border for keyboard focus. Native Edit inspection confirms zoom/reset/search/footer actions no longer carry permanent boxes.

A fixed two-column GridLayout exposed overlapping Delivery sections at 1024x720 (`native-control-consistency/04-export.png`); the workflow assertions alone did not detect it. Replaced the nested layout with an explicitly sized Item row, equal section widths, and a height derived from the larger section. Added a native geometry assertion for minimum size, aligned tops, and non-overlapping side-by-side sections.

Production/driver build passed (`/private/tmp/framehouse-delivery-row-build.log`); the updated driver built in `/private/tmp/framehouse-delivery-geometry-build.log`. Inspected `native-delivery-row/04-export.png`: two readable columns, destination, Output details and Start Export all visible at 1024x720. Broader design and motion acceptance remain open. The tracker opening now reflects the latest precision-tool direction and explicitly rejects the superseded graphite/spectral-edge baseline.

Final `native-delivery-row/` run exited 0 with 24 captures. Geometry, keyboard navigation, compact Carousel drawer, off-screen filmstrip selection, finite completion, stable idle painting, and reduced-motion assertions passed.


### Compare source truth and caption hierarchy

Compare now resolves its displayed source with the renderer's priority: a valid selected snapshot in auto/snapshot mode, then an external image in auto/external mode, otherwise Original. Titles, active source buttons and Bypass availability share that resolution; the snapshot picker is only visible for an active snapshot. Before/After captions retain legible backplates but lose borders and the cyan After treatment.

The native driver creates a temporary named snapshot, selects auto mode, asserts the snapshot title and active source control, then restores Original and deletes the fixture. Build passed (`/private/tmp/framehouse-compare-source-build.log`). `native-compare-source/` produced its completed receipt with 23 captures; inspected 02-compare directly for caption presentation. This covers the automatic-snapshot labeling regression, not exhaustive external-reference rendering parity or final visual acceptance.


### Deliberate color registration and authorized RAW review

Completion color now occupies a dedicated 104x10px area at the target-header/queue-row edge instead of drawing behind text across the entire owning surface. Raised effect opacity to 0.82, made both bands 3px high, and sized the registering plates relative to that bounded area. Existing explicit additive RGB and subtractive CMY intersection math, finite lifecycle, publication triggers, and reduced-motion behavior remain. This is illustrative display-space mixing, not physical ink simulation.

Build passed (`/private/tmp/framehouse-color-registration-build.log`). Authorized RAW run `native-photo-registration/` exited 0 with 22 captures and 25 timestamped replay frames. Recipe persistence was disabled; no real-export flag was used and no RAW export was performed. Finite completion, unloaded idle surface, stable idle painting, reduced-motion suppression, comparison snapshot-source assertions, Delivery geometry, and selection rendering checks passed. Inspected replay frame-006 and Delivery directly: registration is visible at normal size in its dedicated area, and the color/crop row remains aligned with the actual photograph. The test changes exposure for interaction proof; its displayed grade is not an artistic recommendation or color-fidelity validation.

Remaining visible cleanup includes the redundant Current Image section in Delivery and persistent global navigation underlines. Full motion aesthetic acceptance, performance metrics, and broad design completion remain open.


### Global navigation and duplicate Delivery information

Global workspace tabs now use a neutral selected fill and weight instead of a colored bottom underline, with a separate explicit keyboard-focus border. Removed Delivery's Current Image section: the same filename is already above the preview and media/selection count is in the settings header. Updated the directly affected source-contract expectations for that intentional removal.

Production and driver build passed (`/private/tmp/framehouse-navigation-build.log`). `native-navigation/` exited 0 with 23 captures and passing native workflow assertions; inspected Delivery for the selected nav state, complete settings, and visible primary action. This did not add a dedicated keyboard traversal test of global tabs.

Both smoke and focused source-contract targets compiled. The monolithic smoke executable skipped with exit 77 because it is an explicit opt-in manual soak. Focused `desktop_shell_source_contract_test` failed before Export assertions: it still requires the previously removed literal `subtitle: "Scene-referred • shapes linear light before color styling"`. Other rendering-upgrade literals in that guard are present. This stale-copy guard is separate from the native proof; do not claim a green source-contract suite. Logs: `/private/tmp/framehouse-navigation-source-test.log`, `/private/tmp/framehouse-navigation-smoke.log`. Full design completion remains open.


### Source-contract alignment completed

Updated stale source expectations after inspecting each failed guard against current code: Light heading without its removed subtitle; multiline slider coalescing handlers; Outline and shorter selection labels; the retouch in-progress Cancel branch; flat borderless ActionButton/MiniButton metadata and focus behavior; sentence-case Color space; icon-only Carousel rail with selected fill and full tooltips; board range/structure actions in the Board menu; 38px adjustment rows; and Light before White Balance. Preserved checks for routing, setters, keyboard/accessibility, complete labels, and edit coalescing. The C++ ellipsis check now examines quoted text rather than incorrectly rejecting `catch (...)`. ActionButton/MiniButton metadata now describes the actual flat design rather than superseded optical/stripe styling; no appearance or behavior changed in this pass.

`desktop_shell_source_contract_test` built and exited 0 with QT_QPA_PLATFORM=offscreen. Logs: `/private/tmp/framehouse-source-alignment-build.log`, `/private/tmp/framehouse-source-alignment-test.log`. The log contains host audio-device warnings, so this is not a warning-free claim. This closes the source-contract failure noted in the previous checkpoint, but source tests do not establish visual acceptance, runtime parity, or completion of the full design objective.


### Selection state controls

Replaced the selected-looking Disable mask action and Invert action with shared ToolOptionToggle controls labeled Mask enabled and Inverted. They expose checked state, keyboard semantics and explanatory tooltips; Edit Layer remains an action. Both follow existing lock/retouch restrictions. Updated the older integration label/state expectations and the focused source contract.

Production, driver and source-contract targets built (`/private/tmp/framehouse-mask-state-final-build.log`); source-contract test exited 0. Native `native-mask-state/` produced 23 captures and maskEnableStateVerified=true: pointer disable, Space re-enable, checked-state synchronization, preservation of overlay display mode, and locked-state disablement are asserted. Inspected 05-selection-fixture directly. Inversion retains its existing controller setter but was not separately toggled in this new native check. Synthetic matte only; not an AI segmentation or default-refinement performance proof. Broader design acceptance remains open.


### Shared field and checkbox system

Theme now defines fieldFill/fieldHoverFill and a 3px control radius. ToneTextField and ToneComboBox use flat neutral fills without resting outlines or nested focus-tint layers; keyboard focus remains explicit. Dropdown options use neutral highlighting and fill the popup's available width. ToolOptionToggle uses the checkbox itself for checked state, with a transparent resting label surface and a clear neutral checkmark. Native Selection inspection confirms the large selected checkbox-label background is gone.

Added native preset-popup keyboard coverage: Space opens, Escape closes without changing the selected preset, and the popup is captured. A first density reduction exposed clipped option text because default delegate padding remained. Corrected it with explicit zero horizontal delegate padding and 4px vertical padding inside 26px rows; added a rendered label/content-height assertion. The failed visual iteration `native-field-density/` is not acceptable menu evidence despite its earlier workflow receipt.

Final production/driver build passed (`/private/tmp/framehouse-field-padding-build.log`); focused source-contract test exited 0. `native-field-padding/` has 25 captures and a completed receipt with presetKeyboardVerified, maskEnableStateVerified, and compactCarouselInspectorVerified true at 1024x720. Inspected 04a-preset-menu directly: complete readable option labels and compact rows. Earlier `native-field-system/` exited 0 and supplies the inspected field/checkbox captures before the final row-padding correction. Broader visual acceptance and full-objective completion remain open.

### Carousel navigation and Scopes typography review

The compact native Carousel review exposed white navigation text disappearing on a light board and large previous/next actions covering the artwork. Grouped Previous, Next, and Compare neighbor in one opaque neutral toolbar. The canvas reserves vertical space for this toolbar, preserving all three board numbers and keeping navigation clear of the artwork. Existing command handlers, enabled states, accessible text, and shortcuts remain intact. Scope domain and proof-status labels now use the shared UI typeface.

Both production and workflow-driver targets built successfully (`/private/tmp/framehouse-navigation-inset-build.log`). The first native run (`native-navigation-shelf`) completed but exposed a toolbar/board-number overlap; that intermediate screenshot is not acceptance evidence. The corrected native capture in `native-navigation-inset/07b-carousel-composer.png` was visually inspected at 1024 x 720 logical dimensions and shows the toolbar separated from all board labels and content. `native-navigation-shelf/03-scopes.png` confirms readable UI-font status labels. These are generated-fixture layout checks, not real-photo fidelity or comprehensive Carousel interaction/performance proof. The broader visual-system and motion requirements remain open.

### Timeline source-panel layout correction

The current compact Timeline capture showed an empty Source Tape row and a forced minimum-height layout distributing unused space above the source monitor. This pushed the empty-state Browse Media action below the visible panel. `TimelineSourcePanel.qml` now hides the tape row when inactive, sizes the scroll content from its actual implicit height, and anchors the content column at the top without stretching it vertically.

Production and workflow-driver builds passed (`/private/tmp/framehouse-source-layout-build.log`). The native generated-fixture capture `native-source-layout/08b-timeline-editor.png` was visually inspected: the title, monitor, and Browse Media action are fully visible together at 1024 x 720 logical dimensions. This verifies the empty-source layout; populated source transport, source tape, and media-browser workflows are not newly proven by this screenshot.

### Current real-photo design review

Reviewed the current native build with the explicitly authorized IMG_4440.CR2 at 1600 x 1000 logical dimensions. The Cocoa/Metal workflow exited 0, produced 23 screen captures and 25 timestamped completion replay frames under `native-current-photo/`, and confirms recipe persistence disabled. No real export was requested. The test changes exposure in memory; the resulting exposure is an interaction fixture, not an intended photographic grade.

Direct visual findings:

- `01-edit.png`: large uninterrupted photograph, neutral surround, a single Adjust tab heading, aligned Light controls, and compact White Balance layout. This is current real-photo composition evidence; it does not prove color-pipeline numerical fidelity.
- `05-selection-fixture.png`: one compact contextual toolbar, organized inspector, separate mask-enabled and overlay states, and a bounded tool palette. The ellipse is a synthetic imported alpha fixture, not an AI subject selection.
- `04-export.png`: preview and delivery settings have clear hierarchy; the main Start Export action remains too similar to ordinary neutral controls. Next correction: distinguish the primary delivery action without restoring gradients or colored outlines.
- `caustic-frames/frame-006.png`: the current RGB/CMY effect reads as two tiny colored bars, not a refined optical event. Functional lifecycle checks pass, but the aesthetic requirement remains unmet. Replace this visual treatment with a cohesive focused optical/registration motion and review a sequence at normal scale. Preserve completion-only triggering, reduced motion, clipping, and zero idle work.

Receipt flags confirm keyboard bypass navigation, preset keyboard interaction, mask enable state, finite completion, stable idle paint count, reduced-motion suppression, and view reset preserving exposure. Filmstrip scrolling and compact Carousel inspector flags are false in this single-image wide run; earlier generated-fixture checks remain separate. Render logs contain frame-budget overruns and a camera-characterization CPU fallback, so this is not a smoothness or all-GPU claim.

The next implementation priority is primary-action hierarchy plus the optical completion design. Neither broad visual acceptance nor the full goal is complete.

### Primary action hierarchy

Added shared achromatic primary-action tokens in Theme: off-white resting surface, brighter hover, darker pressed surface, and dark foreground. ActionButton and MiniButton use the same palette for enabled primary actions; ordinary actions and selected modes retain their quieter neutral treatment. Disabled and embedded ActionButton states retain readable foregrounds on dark backgrounds. No gradient, selected underline, or new border was added.

Production and workflow-driver builds passed after both components were updated (`/private/tmp/framehouse-primary-consistency-build.log`). The focused desktop source-contract test exited 0 (`/private/tmp/framehouse-primary-source-test.log`). Native generated-fixture screenshots at 1024 x 720 under `native-primary-hierarchy/` were inspected for Export and populated Carousel: the primary delivery action is distinct, with dark text/icons, while secondary controls remain subordinate. This addresses the primary-action weakness recorded in the real-photo review; optical completion design remains open.

### Optical facet completion iteration

Replaced the two narrow RGB/CMY bars with one converging faceted mark. Upper overlaps add RGB light; lower overlaps multiply idealized CMY transmittance. The facets align by 72 percent of the animation and resolve to light/dark before fading. The Edit placement is now 24 pixels tall and centered in its existing target header, clear of the photograph. Export rows reuse the same geometry within their existing compact allocation. This is a stylized display-space effect, not a physical caustic or print simulation.

The first recording exposed faint fractional-pixel fills; switched to pixel-aligned scanlines. Production and driver builds passed (`/private/tmp/framehouse-optical-facet-aligned-build.log`). Focused source checks passed before this final raster alignment. The final native recording is under `native-optical-facet-aligned/caustic-frames/`; frames 003, 006, and 009 were inspected at normal capture scale and show separated color, convergence, and a resolved light/dark mark. This establishes the rendered sequence, not user aesthetic approval or a measured animation frame budget.

Completion-only triggers, reduced-motion suppression, clipping, and on-demand Canvas allocation are retained. Broader UI acceptance remains open.

### Current interaction audit

Built and ran `section_comparison_interaction_test`, `adjustment_pad_interaction_test`, and `retouch_cancel_ui_test`: all three passed on the Qt offscreen platform (`/private/tmp/framehouse-interaction-audit-test.log`). These exercise comparison gestures, adjustment-pad modifiers/cancellation, and the retouch primary action returning from Cancel to Apply/Done. The retouch test uses a controller fixture and does not establish real inpaint cancellation or history integrity.

Built and ran `desktop_shell_tool_exit_test`. Its startup assertions initially omitted the current Paint tool. Updated the expected rail group order, primary-tool list, and lifecycle count from eight to nine after inspecting `editToolModel()`. Navigation, target preservation, and cancellation assertions remain. The rebuilt app-level shard passes (`/private/tmp/framehouse-tool-exit-count-test.log`, 7.27 seconds). It invokes the shared cancellation handler and checks shortcut availability; this is not a physical native Escape-key event test.

This strengthens the interaction evidence behind the redesigned controls. It does not close real-render selection history, motion frame-budget, or full visual acceptance gates.

### Shared panel geometry

Aligned panel and surface radii to the shared four-pixel step and chips to the control radius. GlassSurface no longer adds a resting border by default; callers that explicitly request an edge still receive it. Focus indicators and content-specific outlines are unchanged.

Production and workflow-driver builds passed (`/private/tmp/framehouse-panel-geometry-build.log`), as did the focused source-contract check. Native compact captures in `native-panel-geometry/04-export.png` and `08b-timeline-editor.png` were inspected: the shared panel boundaries are quieter, while headings, spacing, and separators retain grouping. The Timeline source monitor and Browse Media action remain fully visible.

A remaining local exception is Export's outlined empty-job container, which does not use the shared surface defaults. The Scene entry screenshot was reviewed separately and already follows the neutral surface/primary-action hierarchy; a populated Scene workspace is not proven by that entry view.

### Export empty state and current actual delivery

Removed the local rounded empty-queue card. Its content now uses shared body typography, left alignment, available-width wrapping, and content-derived height. Loading and history-error messages remain distinct. Production/driver build and source-contract checks passed.

`native-delivery-current/` verifies actual native pointer activation of Start Export with generated media in an isolated temporary destination. The output PNG decodes at the original 1800 x 1200 dimensions; an artifact is retained as `exported-fixture.png`. The real Completed transition triggered the optical effect once and released its surface afterward. Inspected `04-export.png` and `04d-real-export-completion.png`. No RAW export was performed. This test does not establish export color parity or collision preservation.

The completed-job capture reveals a remaining visual exception: a striped purple progress bar remains visible after completion. That should become a quiet completed-state presentation in the next delivery-row pass.

### Completed delivery row

Completed jobs no longer retain the progress strip. Shared ProgressStrip now uses a thin solid fill without diagonal hatching or an outline. ExportQueueRow uses shared panel geometry and UI typography, with a border only for keyboard focus and an error-only attention rail. A fixed bottom inset keeps the completion mark clear of destination text.

Production/driver build and source-contract check passed. The native run `native-delivery-row-final` performed an actual generated-media export through the visible Start Export control. Inspected `04d-real-export-completion.png`: Completed status and file details remain readable, the progress bar is absent, and the optical mark occupies its reserved space. The driver verifies the full-resolution output artifact, exactly one completion effect, and release of its drawing surface. This does not extend the claim to collision or color-parity testing.
