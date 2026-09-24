# Timeline Industry-Leading Checklist

This is a post-MVP capability parking lot. It does not expand the active 15-capability [Timeline MVP Checklist](timeline-mvp-checklist.md).

After the MVP release gate passes, this checklist expands the professional-parity work into 200 additional product capabilities. It is proof-gated: a control appearing in QML is not completion.

## Completion rules

Check an item only when every applicable gate passes:

- **Behavior:** the real user workflow works end to end, including empty, loading, unavailable, error, cancel, and retry states.
- **Truth:** preview, saved project state, reopened project state, and exported output agree.
- **Undo:** every destructive or state-changing edit supports undo/redo without corrupting unrelated state.
- **Performance:** interaction stays responsive on the representative stress project; expensive work is cancellable and does not block the UI thread.
- **Persistence:** state survives save/reload, relink, duplicate, sequence nesting, and project archive where applicable.
- **Interchange:** imported/exported state round-trips without silent loss where a standard is involved.
- **Accessibility:** the workflow is keyboard reachable, screen-reader named, focus visible, and usable at 200% scaling.
- **Diagnostics:** failures identify the affected asset/action and offer a recovery path; no placeholder success states.
- **Automated proof:** focused controller/model tests plus QML contracts pass; visual or encoded-media fixtures are added where output is visible or audible.
- **Representative proof:** the feature completes its category workflow on a short-form project and a long/complex project without manual state repair.

Item status vocabulary:

- `[ ]` not proven.
- `[x]` completed and all applicable gates passed.
- Do not use partial completion as checked; record partial work in the item notes or linked issue.

## Gate A: Editing speed and timeline intelligence

- [ ] 1. Source Tape mode presents a bin as one continuous browsable reel.
- [ ] 2. Pancake timelines support dragging material between stacked sequences.
- [ ] 3. Timeline minimap shows clip density, markers, gaps, and problem ranges.
- [ ] 4. Sequence Index provides sortable clips, tracks, effects, and metadata.
- [ ] 5. Searchable command palette exposes every editor operation.
- [ ] 6. Context-sensitive keyboard shortcuts are completely remappable.
- [ ] 7. Users can record and execute multi-command editing macros.
- [ ] 8. Repeat Last Action supports editable operation parameters.
- [ ] 9. Hover scrubbing works in bins, thumbnails, and comparison results.
- [ ] 10. Filmstrip thumbnails expose configurable frame density.
- [ ] 11. Track heights adapt to content and zoom level.
- [ ] 12. Focus mode expands selected tracks and collapses unrelated tracks.
- [ ] 13. Select Similar works by metadata, effects, labels, and source relationships.
- [ ] 14. Select Forward/Backward can be constrained by track, group, role, or type.
- [ ] 15. Paste Attributes provides property-level selection and comparison.
- [ ] 16. Replace Edit preserves timing, effects, transforms, and automation.
- [ ] 17. Fit-to-Fill offers selectable speed-change strategies.
- [ ] 18. Four-up monitor supports slip and slide decisions.
- [ ] 19. Edit-point inspector exposes handles, source limits, and transition availability.
- [ ] 20. Timeline heatmap identifies cut density, pauses, effects, and render cost.

### Gate A completion

- [ ] A1. A keyboard-only editor completes ingest-to-rough-cut without pointer-only blockers.
- [ ] A2. Macro, replace, fit-to-fill, paste, and selection operations are undoable and deterministic.
- [ ] A3. A 10,000-clip/100-track stress project keeps navigation and selection responsive.
- [ ] A4. Source Tape, stacked sequences, index, minimap, and timeline show identical source/time state.
- [ ] A5. Focused editorial workflow tests and shortcut-conflict tests pass.

## Gate B: Media organization and discovery

- [ ] 21. Natural-language visual search finds objects, actions, locations, and composition.
- [ ] 22. OCR search finds signs, slides, screens, products, and embedded text.
- [ ] 23. Face clustering supports user-controlled identities and correction.
- [ ] 24. Speaker identification supports reusable, correctable profiles.
- [ ] 25. Shot-boundary and scene-change detection creates editable results.
- [ ] 26. Shot-size classification distinguishes wide, medium, close-up, and macro.
- [ ] 27. Camera-motion classification identifies static, handheld, pan, tilt, dolly, and drone.
- [ ] 28. Best-take ranking considers focus, stability, speech, and expression.
- [ ] 29. Duplicate and near-duplicate shot detection is explainable.
- [ ] 30. Detection flags black frames, lens caps, flashes, and corrupt footage.
- [ ] 31. Quality analysis flags focus, noise, clipping, and excessive shake.
- [ ] 32. Continuity assistant flags wardrobe, prop, direction, and lighting inconsistencies.
- [ ] 33. Smart collections update automatically from user-defined rules.
- [ ] 34. Productions can define custom metadata schemas.
- [ ] 35. Media relationship graph connects originals, proxies, derivatives, and exports.
- [ ] 36. Camera and recorder identity is derived from metadata with overrides.
- [ ] 37. Media can be browsed by geolocation on a map.
- [ ] 38. Calendar view groups footage by shooting date and time.
- [ ] 39. Contact-sheet comparison provides synchronized playback.
- [ ] 40. Rights metadata records license, territory, owner, and expiration.

### Gate B completion

- [ ] B1. Search precision/recall meets a versioned benchmark dataset and reports confidence.
- [ ] B2. Every AI classification can be corrected, excluded, rebuilt, and audited.
- [ ] B3. Analysis is backgrounded, cancellable, cached by media revision, and bounded in storage.
- [ ] B4. Rights-expired or restricted assets produce visible edit/export warnings.
- [ ] B5. Metadata and search indexes survive relink, move, archive, and cross-machine reopen.

## Gate C: AI-assisted storytelling

- [ ] 41. Prompt-to-rough-cut uses selected media and target duration.
- [ ] 42. Script-to-timeline assembly supports editable shot placeholders.
- [ ] 43. Paper edits are reviewable before any timeline mutation.
- [ ] 44. Highlight extraction supports interviews, podcasts, sports, and events.
- [ ] 45. Hook generator creates multiple editable opening variants.
- [ ] 46. Story assistant identifies setup, conflict, payoff, and CTA.
- [ ] 47. B-roll recommendations cite transcript and visual evidence.
- [ ] 48. Insert-shot suggestions hide cuts without changing approved meaning.
- [ ] 49. Edit-style learning uses only user-approved reference projects.
- [ ] 50. Reference analysis extracts pacing without copying protected assets.
- [ ] 51. Pacing assistant maps cuts per minute and energy changes.
- [ ] 52. Delivery analysis finds expressive, authoritative, or emotional takes.
- [ ] 53. Reaction-shot discovery works across synchronized cameras.
- [ ] 54. Jump-cut smoothing offers reframing or generated transition frames.
- [ ] 55. Generative clip extension creates reviewable, labeled handles.
- [ ] 56. Clean-plate generation removes selected background objects.
- [ ] 57. Gaze correction has adjustable strength and comparison preview.
- [ ] 58. Eye-line and headroom correction preserves intentional composition.
- [ ] 59. Smart reframing understands subjects, graphics, and negative space.
- [ ] 60. AI action history exposes source, confidence, model, and mutation.

### Gate C completion

- [ ] C1. AI never changes the timeline until the user previews and accepts a diff.
- [ ] C2. Every accepted AI edit is one-step undoable and reproducible from a recorded recipe.
- [ ] C3. Generated media is visibly labeled and includes provenance where supported.
- [ ] C4. Intent-aware fixtures reject technically successful but visibly wrong results.
- [ ] C5. Local/cloud processing, data retention, cost, and model limitations are disclosed before execution.

## Gate D: Compositing, graphics, and effects

- [ ] 61. Adjustment clips affect every eligible layer beneath them.
- [ ] 62. Node-based compositing supports complex reusable graphs.
- [ ] 63. Track mattes accept video, images, text, and generated masks.
- [ ] 64. Object masks propagate across complete shots with corrections.
- [ ] 65. Planar tracking supports screens, signs, tattoos, and surface replacement.
- [ ] 66. Point tracking can drive text, graphics, blur, and effects.
- [ ] 67. 3D camera tracking reconstructs editable scene motion.
- [ ] 68. Depth maps provide editable foreground/background separation.
- [ ] 69. Depth-aware blur, fog, lighting, and text placement are supported.
- [ ] 70. Chroma keying includes spill suppression and edge repair.
- [ ] 71. Motion blur follows actual transform velocity.
- [ ] 72. Vector paint supports clone, cleanup, and wire removal.
- [ ] 73. Particle systems support rain, snow, sparks, dust, and custom emitters.
- [ ] 74. 3D text supports extrusion, materials, lighting, and shadows.
- [ ] 75. Shape layers support Boolean operations and animated paths.
- [ ] 76. Graphics can bind to CSV, JSON, or live API data.
- [ ] 77. Responsive templates adapt to text length and aspect ratio.
- [ ] 78. Template authors can expose a safe subset of parameters.
- [ ] 79. Effect-stack presets have previews, versions, and migration.
- [ ] 80. Third-party effects run through a sandboxed, crash-isolated SDK.

### Gate D completion

- [ ] D1. Every enabled effect matches preview, save/reload, cache, and export.
- [ ] D2. Golden-frame fixtures cover alpha, color, transform, masks, depth, tracking, and nesting.
- [ ] D3. Effect invalidation rerenders only affected ranges and never shows stale frames as final.
- [ ] D4. Plugin crashes do not terminate the editor or corrupt the project.
- [ ] D5. Complex reference composition plays or degrades predictably under the performance budget.

## Gate E: Color and image finishing

- [ ] 81. Scene-referred color management supports OpenColorIO and ACES.
- [ ] 82. Input color spaces are detected automatically with visible overrides.
- [ ] 83. Supported RAW formats expose debayer and camera controls.
- [ ] 84. Color-space transforms cover SDR, HDR, log, and camera gamuts.
- [ ] 85. HDR10, HLG, and Dolby Vision workflows include metadata handling.
- [ ] 86. Reference gallery supports split, wipe, overlay, and difference views.
- [ ] 87. Gallery stills and reusable grades persist with the project.
- [ ] 88. Node-based grading supports serial, parallel, and layer structures.
- [ ] 89. Tracked power windows support feathering and correction.
- [ ] 90. Selective color keys hue, saturation, luminance, and depth.
- [ ] 91. Skin-tone tools protect texture and complexion.
- [ ] 92. False color, exposure zones, focus peaking, and gamut warnings are available.
- [ ] 93. Camera charts can drive automatic technical balancing.
- [ ] 94. Film emulation supports halation, grain, weave, and print response.
- [ ] 95. Shot-matching groups remain editable after automatic matching.
- [ ] 96. Remote grading sessions synchronize high-quality reference frames.
- [ ] 97. Display calibration profiles and output validation are supported.
- [ ] 98. Per-output trim passes support SDR, HDR, web, and theatrical delivery.
- [ ] 99. Color decision lists import and export without silent loss.
- [ ] 100. Render comparison detects preview/export color divergence.

### Gate E completion

- [ ] E1. Color-managed golden fixtures pass across CPU, GPU, preview, scopes, and export.
- [ ] E2. No enabled operation silently clips legal values or changes color space.
- [ ] E3. HDR metadata and measured output pass external validation tools.
- [ ] E4. RAW and interchange settings round-trip with explicit unsupported-field reporting.
- [ ] E5. Calibrated-display and reference-export comparisons stay within documented tolerances.

## Gate F: Audio production

- [ ] 101. Spectral editing removes isolated clicks, hum, coughs, and noises.
- [ ] 102. Source separation produces dialogue, music, ambience, and effects stems.
- [ ] 103. Voice isolation and speech enhancement expose artifact controls.
- [ ] 104. De-reverb reduces room reflections with comparison preview.
- [ ] 105. Room tone is discovered and can fill gaps seamlessly.
- [ ] 106. Plosives, sibilance, mouth clicks, and breaths are detected for review.
- [ ] 107. ADR workflow supports cues, countdowns, takes, and selection.
- [ ] 108. Voiceover recording writes directly to armed timeline tracks.
- [ ] 109. Multiple microphones can be phase-aligned automatically.
- [ ] 110. External audio synchronizes to camera media at sample accuracy.
- [ ] 111. Pitch correction preserves configurable formants.
- [ ] 112. Tempo maps follow performed music rather than fixed BPM.
- [ ] 113. Audio conforms mixed sample rates and variable-rate media.
- [ ] 114. Surround, Ambisonics, binaural, and immersive audio are supported.
- [ ] 115. Audio roles route dialogue, music, ambience, effects, and commentary.
- [ ] 116. Tracks and buses support sidechain routing.
- [ ] 117. Automation supports write, touch, latch, trim, and read modes.
- [ ] 118. Plugin delay is compensated and reported.
- [ ] 119. Loudness compliance covers broadcast and streaming standards.
- [ ] 120. Effect comparisons are loudness matched automatically.

### Gate F completion

- [ ] F1. Sample-accurate fixtures prove sync, routing, automation, latency compensation, and export stems.
- [ ] F2. Loudness, peak, phase, and channel-layout results pass external analyzers.
- [ ] F3. Destructive-looking cleanup remains reversible and preserves the original media.
- [ ] F4. Recording survives device changes, underruns, cancellation, and crash recovery.
- [ ] F5. AI audio tools expose artifacts through matched A/B preview and never replace originals silently.

## Gate G: Creator and social-video intelligence

- [ ] 121. Long-form sequences generate linked short-form derivatives.
- [ ] 122. One finished edit can produce multiple hook variants.
- [ ] 123. Platform overlays show captions, buttons, profile UI, and safe zones.
- [ ] 124. Spoken-word captions emphasize timing and vocal energy.
- [ ] 125. Caption animation supports karaoke, bounce, pop, highlight, and single-word modes.
- [ ] 126. Caption line breaks follow meaning and reading comfort.
- [ ] 127. Thumbnail finder scores expression, sharpness, contrast, and readability.
- [ ] 128. Thumbnail workspace supports titles, faces, and safe-zone testing.
- [ ] 129. Opening-three-seconds analysis evaluates motion, speech, branding, and clarity.
- [ ] 130. Retention predictions show uncertainty and evidence.
- [ ] 131. Pacing map identifies likely audience drop-off regions.
- [ ] 132. CTA detection flags missing or poorly timed calls to action.
- [ ] 133. Chapters and timestamps derive from subject changes.
- [ ] 134. Descriptions, chapters, and hashtag drafts derive from final content.
- [ ] 135. Platform packaging preserves one editable master.
- [ ] 136. Reusable end-screen and related-video layouts are supported.
- [ ] 137. Localization covers captions, graphics, audio, and cultural variants.
- [ ] 138. AI dubbing preserves speakers and supports pronunciation correction.
- [ ] 139. Lip synchronization supports translated dialogue.
- [ ] 140. Export records required AI-disclosure and authenticity metadata.

### Gate G completion

- [ ] G1. Platform profiles are versioned and validated against current official requirements.
- [ ] G2. One master project produces 16:9, 9:16, 1:1, and 4:5 outputs without destructive duplication.
- [ ] G3. Caption readability, safe zones, loudness, metadata, and duration validators pass every target.
- [ ] G4. Predictions never present engagement or monetization as guaranteed outcomes.
- [ ] G5. A representative long-form project produces reviewed, publishable short-form packages in every supported language.

## Gate H: Collaboration and review

- [ ] 141. Multiple users edit concurrently with conflict-safe operations.
- [ ] 142. Tracks, sequences, bins, and clips support edit locking.
- [ ] 143. Collaborator cursors and selections appear live.
- [ ] 144. Presence indicates who is viewing or editing each sequence.
- [ ] 145. Browser review links require no editor installation.
- [ ] 146. Reviewers can draw frame-accurate annotations.
- [ ] 147. Versions compare side by side with comments carried forward.
- [ ] 148. Approval stages cover rough cut, fine cut, picture lock, and delivery.
- [ ] 149. Review comments synchronize with timeline markers.
- [ ] 150. Assignments and approvals can trigger notifications.
- [ ] 151. Change digests summarize edits, comments, and exports.
- [ ] 152. Audit history identifies who changed each timeline operation.
- [ ] 153. Permissions distinguish editors, reviewers, clients, and producers.
- [ ] 154. Review links can expire and apply visible or forensic watermarks.
- [ ] 155. Shared libraries support local caching and conflict handling.
- [ ] 156. Media upload supports resume, deduplication, and checksum proof.
- [ ] 157. Cloud relinking resolves alternate mounts and user machines.
- [ ] 158. Offline collaboration reconciles changes after reconnecting.
- [ ] 159. Production dashboard shows status, owner, deadline, and blockers.
- [ ] 160. Client-safe review mode hides implementation complexity.

### Gate H completion

- [ ] H1. Concurrent randomized-edit tests converge to identical project state for every client.
- [ ] H2. Offline, reconnect, duplicate-event, and permission-revocation scenarios preserve data.
- [ ] H3. Audit history is immutable, attributable, filterable, and exportable.
- [ ] H4. Review links enforce permissions, expiration, watermarking, and revocation.
- [ ] H5. A distributed team completes review-to-approval without manual comment or version migration.

## Gate I: Interchange, automation, and extensibility

- [ ] 161. OpenTimelineIO imports and exports supported timeline state.
- [ ] 162. AAF interchange supports professional sound post.
- [ ] 163. CMX EDL imports and exports supported editorial state.
- [ ] 164. FCPXML imports and exports supported project state.
- [ ] 165. Premiere-compatible XML interchange is supported.
- [ ] 166. Resolve-compatible timelines and markers round-trip.
- [ ] 167. IMF packages can be created and validated.
- [ ] 168. Broadcast MXF and AS-11 delivery are supported.
- [ ] 169. Image sequences detect and report missing frames.
- [ ] 170. Watch folders trigger ingest, transcode, analysis, or export.
- [ ] 171. Headless command-line rendering is supported.
- [ ] 172. Public scripting API exposes safe editor operations.
- [ ] 173. Event hooks cover import, edit, save, render, and delivery.
- [ ] 174. No-code automation graphs orchestrate media workflows.
- [ ] 175. Structured data can batch-create projects from templates.
- [ ] 176. Render queues distribute work across workers.
- [ ] 177. Background jobs support priorities and resource limits.
- [ ] 178. Hardware-control API supports jog wheels, panels, mixers, and keyboards.
- [ ] 179. Plugin marketplace validates compatibility and security.
- [ ] 180. Portable archives consolidate media with checksum verification.

### Gate I completion

- [ ] I1. Golden interchange projects round-trip each standard and report every lossy field before mutation.
- [ ] I2. CLI, scripting, automation graph, and GUI produce equivalent deterministic results.
- [ ] I3. Jobs survive cancellation, restart, worker loss, duplicate delivery, and partial output.
- [ ] I4. Plugins and scripts are permission-scoped, versioned, sandboxed, and revocable.
- [ ] I5. Portable archives reopen on a clean machine with no missing required dependency.

## Gate J: Reliability, accessibility, and trust

- [ ] 181. Append-only edit journal supports crash-safe recovery.
- [ ] 182. Automatic checkpoints precede risky or batch operations.
- [ ] 183. Project health scan finds missing media, fonts, plugins, and corrupt caches.
- [ ] 184. Deterministic rendering produces repeatable outputs.
- [ ] 185. Source checksums verify media from import through delivery.
- [ ] 186. Render manifests list sources, effects, versions, settings, and hashes.
- [ ] 187. Background saving does not interrupt playback.
- [ ] 188. Cache budgets expose disk, GPU, and memory use.
- [ ] 189. GPU failures recover without losing the project.
- [ ] 190. Effects report their playback and render cost.
- [ ] 191. Diagnostic packages redact private data.
- [ ] 192. Every editor control exposes a screen-reader name and role.
- [ ] 193. Every workflow is operable without pointer-only interactions.
- [ ] 194. High-contrast, color-blind-safe, and reduced-motion modes are supported.
- [ ] 195. Interface remains usable at high DPI and 200% scaling.
- [ ] 196. UI and timeline text support right-to-left languages.
- [ ] 197. Caption validator checks reading speed, contrast, and position.
- [ ] 198. Flash analysis warns about photosensitive-seizure risk.
- [ ] 199. C2PA provenance can be inspected and signed on export.
- [ ] 200. Privacy controls show when and why AI data leaves the machine.

### Gate J completion

- [ ] J1. Crash-injection tests recover the last acknowledged edit without corrupting originals.
- [ ] J2. Repeat renders match hashes or document every expected nondeterministic field.
- [ ] J3. Stress tests prove bounded memory, disk, cache, and GPU behavior.
- [ ] J4. Keyboard, screen-reader, contrast, scaling, reduced-motion, and RTL audits pass.
- [ ] J5. Privacy, provenance, diagnostics, and render manifests are understandable and verifiable by the user.

## Program release gates

- [ ] R1. All checked feature items link to their focused automated proof.
- [ ] R2. All ten category completion gates pass on the current release candidate.
- [ ] R3. No enabled control is a placeholder or knowingly diverges between preview and output.
- [ ] R4. A 60-second vertical creator project completes ingest, rough cut, graphics, captions, mix, localization, validation, and delivery.
- [ ] R5. A 30-minute multicamera project completes ingest, organization, edit, color, audio, review, interchange, archive, and delivery.
- [ ] R6. Crash recovery, offline media, missing plugin, low disk, GPU reset, and canceled AI workflows recover without project corruption.
- [ ] R7. Performance budgets are recorded for startup, import, seek, scrub, trim, playback, save, reopen, and export.
- [ ] R8. Accessibility and privacy audits have no release-blocking findings.
- [ ] R9. Exported masters pass codec, color, audio, caption, metadata, and platform validation.
- [ ] R10. Final visible and audible output is reviewed against the approved reference, not only status codes or scores.

## Recommended execution order

1. Gate J reliability foundations: edit journal, deterministic render, health scan, cache budgets.
2. Gate A editorial speed: Source Tape, Sequence Index, command palette, Replace Edit.
3. Gate B discovery: visual search, OCR, scene detection, duplicate detection.
4. Gate C trustworthy AI: paper edit, action history, prompt rough cut, B-roll suggestions.
5. Gate G creator delivery: derived shorts, captions, safe zones, localization.
6. Gate F audio cleanup and recording.
7. Gate D compositing and template system.
8. Gate E color-managed finishing.
9. Gate H collaboration and review.
10. Gate I interchange, automation, and ecosystem.
