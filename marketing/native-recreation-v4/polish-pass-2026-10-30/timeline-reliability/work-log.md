# Exact trailer reliability pass

Protected project: 196 tracks, 1,465 clips, 298 seconds, native hash
`fa0befe66d02dde6e75bb67453ad954fd313b2e12fe2d7b74536a6e06282837a`.

The window is now exposed and foregrounded. Earlier unexposed playback probes
are not a valid performance baseline. Current playback advances from 218.5 to
247.3 seconds, but visible Qt frame logs contain substantial jank.

## Bounded performance loop

- Maximum three attempts per measured hotspot.
- Same nine guarded seeks plus six seconds of playback, with native thread
  sampling and existing `qml.frame.summary` telemetry before and after.
- Target: reduce measured Qt jank proportion by at least 20%, with seek RPC
  median and RSS protected against more than 10% regression. Compare repeated
  runs if host variability changes the conclusion.
- RPC timing does not establish displayed-frame latency. Actual pointer
  interaction, rendered content, playhead tracking, edits/undo and notes need
  separate native visual checks.
- Retain authored hash, complete recovery backup before each worker restart,
  one isolated worker, no original-media changes.

## Correctness work

1. Video-only background MediaPlayer lifecycle is implemented and focused
   tests pass; original crash elimination still needs sustained native proof.
2. Focused track view currently retains 14 empty lanes. Keep targeted insertion
   lanes and explicitly reveal newly created lanes; do not let old empty lanes
   obscure the working section. All-tracks view must remain available.
3. Verify clip selection, applied effects/keyframe editing and notes at the
   actual frame, then undo test edits and verify native persistence.

## Keyframe cache result

Attempt 1 excluded empty channel indexes. This fixed smaller scenes but still
thrashed at the 110-snapshot carousel scene. Attempt 2 also raised the bounded
working set from 64 to 128. Three same-project QVariant/QML evaluations at 180s
retained identical checksums: median 2985.75 to 167.01 ms (94.4% lower) across
120 simulated frames; peak process RSS 2,537,603,072 to 866,025,472 bytes.
This accepts the focused index optimization, not a whole-app FPS claim.

`timeline_dense_surface_ui_test` passes using the current production helpers,
including retention of selected/targeted lanes, one empty destination per kind,
group headers, static-lane cache reuse and the 128-snapshot retention bound.

Native Notes test: created a frame note at frame 6574 through the UI; MCP readback
confirmed the exact body and anchor. UI Undo restored all three original notes
and the exact protected project hash.

## Whole-app comparison boundary

The first native RPC pair is not accepted as a whole-app performance win:
viewport widths differed (716 vs 943 logical pixels), the new worker was cold,
and native paused sampling showed a full recovery-journal scan consuming a
worker thread throughout the sample. The candidate also exceeded the RSS guard.
Retain both artifacts; do not advertise their Qt frame counts as matched FPS.
The focused keyframe-index result has its own matched three-run memory proof.

## Recovery-list hotspot

Paused native sample: `refreshRecentTimelines -> discover_timeline_recoveries ->
read_valid_timeline_journal -> readJsonLines`, processing the exact 8,713,796,248
byte journal. A new presentation-only tail reader validates the final envelope
but explicitly requires full predecessor verification on open/restore. Its
real-journal probe reads 54,525,952 bytes in 2055 ms and returns the protected
project hash. Existing full-chain recovery APIs remain unchanged. Codec tests
cover a valid tail over a broken prefix, interrupted suffix, IO bounds and
cancellation; strict reading still rejects the prefix.

The consistent private Release build completed. Two unrelated source inputs
changed during that build (`LayerMaskThumbnail.qml` and
`editor_selection_controller.cpp`); preserve them and rebuild their dependencies
before staging. The first recovery CTest invocation ran before its executable
finished linking and was Not Run; the subsequent invocation is the valid check.

## Cold checkpoint scan and bounded active history

The consistent native profile confirmed a second independent cold scan in
`submitEditJournalEntry -> persist_timeline_journal_snapshot`, not Recents.
The writer correctly verifies a predecessor chain before appending, but this
legacy chain is 8.7 GB. A clean, fully verified chain larger than 64 MiB now
gets a byte-preserving `.sealed-UUID` archive and an atomic QSaveFile replacement
with one independently verified current head. The old active file remains in
place until the replacement commits. Explicit old candidate IDs still search
sealed archives and require full verification. Ordinary listings skip archives.
Large snapshots (at least 1 MiB) now use the existing v2 compressed codec by
default; explicit PHOTO_EDITOR_TIMELINE_COMPRESSED_JOURNAL=0 retains legacy
storage, and =1 forces compression. This does not alter the authored document.

Controller regressions pass 3/3, including the added 72 MiB legacy-chain rollover,
byte-exact archive retention, small verified new head and reopening the exact
old recovery candidate. The codec/virtual rows/inspector lanes/media lifecycle
checks passed 5/5 in the preceding consistent build. Native migration remains
to be exercised before declaring this storage hotspot resolved.

Native UI scale test: selected grade-original-city from Elements, changed
1.00x to 1.05x in the actual numeric field and captured the Program. 79,320
of 220,352 composition pixels changed at the same paused playhead. Undo restored
1.00x and the exact protected document hash.

## Static playhead dependency fanout

A three-second sustained-playback native sample attributes 250/375 main-thread
samples to gap-clock publication and QML binding updates. The clip animation
wrapper, static track opacity and no-transition fade state read the global
playhead even when they have no corresponding animation. New bounded hypothesis:
avoid clock subscriptions for those static values; retain subscriptions when
keys/fades are authored. Maximum three attempts, target at least 90% fewer
static reevaluations over 100 ticks, exact fallback/animated values retained,
no added cache or memory retention. Then repeat the native large-project scene.

## Cold native restart and save guard follow-up
- Static-clock bundle 6b86c6dd was backed up/restarted into PID 21357. Exact native autosave recovery returned terminal opened and fa0 hash unchanged.
- The Mac locked during the restart (CGSSessionScreenIsLocked=1); CUA has no exposed window. Requested unlock; final physical scrub/notes and matched foreground comparison remain pending.
- Cold paused sample `/private/tmp/looksy-static-clock-paused.sample.txt` contains no journal-validation or recovery-listing CPU scan. Render-loop waiting while locked is not a foreground responsiveness result.
- Native asynchronous Library save completed clean/saved with exact fa0. Later close correctly refused because background integrity-status updates made the same authored hash dirty again. Source queue was still settling (63 pending).
- Fix: completing verification of an existing expected checksum publishes runtime status but neither starts a journal write nor dirties the saved document. Learning a missing expected checksum still persists/marks dirty. Mismatch continues to fail closed and retains the original expected checksum. Extended actual recovered-source controller regression to save during paused audit then require clean state after valid and mismatched verification.

## Empty preview-cache UI stall
- Native reopen sample `/private/tmp/looksy-native-reopen.sample.txt`: 316/316 main-thread samples were in media hydration → emitAllState → QML renderCacheReadyRangeCount → expectedRenderCacheTasks → range JSON serialization/hash.
- One-loop hypothesis: missing-cache count/visualization must not build signatures at all. Segment count now uses duration/segment length; ready count first checks for project-owned cache files; QML range model stays empty until a cache exists or is generating. Existing explicit signed range/generation APIs unchanged.
- Protected gates: exact count equality, no signature materialization during 100 zero-cache reads, unrelated cache files do not force hashing, explicit range inspection still signs tasks; actual native cache invalidation/generation tests retained.
- First Library reopen did close/open, but readiness did not settle within 45s under this hotspot; recorded as incomplete, not passing. Source hash remained fa0.

- Exact-project cold-count probe (three runs, same controller process for old materialization and candidate query): 75 equal segments, median 2,833.839 ms → 0.409 ms. Scope excludes QML, displayed pixels and FPS. Artifact: empty-cache-counts-measurement.json.
- Canonical native reopen also replayed ready cached media facts and unconditionally called setDirty. Added a cheap tuple comparison of affected metadata before/after; runtime-only analysis status publishes without authored invalidation. Extended ordinary/bulk cache replay regression and a changed-source-dimensions positive persistence case.
- A CTest run started before the final test rebuild finished; retain cached-metadata-tests.log as unqualified for the newest test source. Final post-link run is cached-metadata-tests-final.log.

## Playback cache follow-up (attempt 2 for absent-cache hotspot)
- Final reopen sample `/private/tmp/looksy-final-reopen-delay.sample.txt` showed 301/301 main-thread samples in hydration → setDirty → Playback.syncToDocument → renderCachePlaybackSourceUrl → all range signatures. The count/UI guard alone was insufficient.
- Centralized cheap project-cache-file presence and applied it to playback source/start, prefetch, health inspection and ready count. Explicit signed task construction stays intact. Added zero-cache playback/prefetch invariants plus positive exact-cache and stale-signature rejection tests.
- Native saved workflow attempts 1/2 retained, not passing: first harness incorrectly asserted a hash during asynchronous save withholding; second reopened successfully but hit the 120s background readiness bound while the unguarded playback path was hashing. No hash drift/crash. Final iteration must use the guarded build.
- Source Monitor media facts/checksum/waveforms are not serialized in the Timeline document. They now publish monitor state without setting Timeline dirty/bulk-change flags. Added a separate Source Monitor regression alongside cached facts and genuinely changed dimensions.

## Monitoring overhead and bounded native navigation
- Latest native sample `/private/tmp/looksy-final-cache-guard.sample.txt` no longer showed preview-cache signatures; 334/334 main-thread samples instead came from repeated inspect_document deterministic hashing of the 143 MiB native document. One-second full-hash polling was actively interfering with readiness, so those elapsed runs remain unqualified for human playback.
- Added optional typed `includeDocumentHash:false` to read-only timeline.inspect_document. Default behavior and mutation hash guards remain unchanged; lightweight results explicitly omit the hash. Native verification now polls status cheaply and asks for the full stable hash once ready.
- Existing open_saved_draft receipts were 123 MiB (actual saved MCP evidence). Successful opening now returns compact identity/readiness and the existing saved path/name rather than every clip/keyframe; bounded structure inspection remains separate. Guarded canonical identity/path validation preserved. Added bridge contract checks for omitted graph data, typed boolean, path rejection and empty authorization hash.
- Hydration reconciliation compares canonical saved native content only when no save is in flight. A net-identical cached roundtrip stays clean; genuinely changed facts and noncanonical recoveries remain dirty. Focused canonical-equality / changed-dimensions test passes.

## Final native functional receipt
- Final private build: cb9f10b0671564d6e0291e52635085cedf2cddbae6a2bbdbbbd8ce6cfd30a18b; one isolated worker PID 33715.
- 12/12 focused checks pass. Native workflow 04 passes clean saved close/reopen, all 3 exact notes, 7 guarded chapter seeks, unchanged fa0, clean final state. Earlier failed/unqualified attempts retained.
- Latest desktop read confirms locked. Native pointer scrubbing, scroll/resize, keyframe edit, note jump and physical playback remain unqualified. A 62.5s guarded seek took about59s; sample showed full deterministic hashing dominating the MCP request. Hash-free polling/open receipt fixes do not eliminate full-hash guarded-action latency. Keep this limit explicit; do not claim responsive UI from RPC liveness.

- Final paired native status roundtrips: median lightweight 2.217ms vs full hash 2966.454ms (3 pairs, same paused fa0). No physical UI latency claim.
