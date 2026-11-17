# Stable-ID Timeline audio-state actions

The stable-ID audio actions are implemented in source and pass their focused Debug test. No MCP command, app UI, or native project was mutated during the test. The current running review app does not contain this source patch yet; stage and link the matching app binary before any audio task uses the actions.

## Registered source actions

Both actions are registered through `register_agent_timeline_commands` and use the existing task mutation owner. They resolve one audio track from `snapshotTimelineTracks()` by exact stable ID, reject missing, duplicate, non-audio, locked, or stale targets, and return an idempotent success when the requested state already holds.

`timeline.action.setTimelineTrackMuted` accepts:

```json
{
  "trackId": "stable audio track id",
  "muted": true,
  "expectedMuted": false
}
```

It calls `setTrackAudioSetting(row, "muted", desired)`. The controller setter now validates a Boolean and updates `track->muted` through its existing undo snapshot path. It never relies on `toggleTrackMuted(row)`, so retrying a desired state cannot flip the lane back.

`timeline.action.setTimelineTrackAudioRole` accepts a stable `trackId`, an `audioRole`, and `expectedAudioRole`, constrained to the current roles `dialogue`, `voiceover`, `music`, `ambience`, `effects`, and `commentary`. It retains the controller's role-to-bus mapping (`music` → `music`; `ambience` and `effects` → `sfx`).

The soundtrack plan targets only the old Music 1 track `music-754faa1f-2e2e-43b4-af95-a77bbef7c3c1` for muting. A role-wide music mute would also silence the replacement score.

## Focused test evidence

`timeline_track_audio_state_command_test` uses actual `TimelineDocumentController` snapshots and a task-owned command registry. It covers stable-ID isolation for two music tracks, stale and locked rejection, non-audio/missing IDs, role validation and bus selection, idempotent state, durable native task receipts, undo/redo, task-owned save-copy/readback, reopen, and exact task revert. The fixture admits an unsaved, dirty Timeline, matching the current P9 worker lifecycle. The registered save-copy writes and verifies a task-owned file while preserving the in-memory task-state hash and dirty flag; task revert restores the task baseline and removes the owned copy.

Separate observed behavior: calling the controller's UI `saveTimeline(false)` first-save path inside an active task changed the task-state hash (`d5d281e6451000e264214156595f6297a403767ddf1f8534dcae38c920252f47` to `65b322624157db80f61ec757eb856f2a7bc073bddfac66a968e11cfe0759a463`) and then conflicted with task revert. This is distinct from the registered task save-copy action covered by the passing test and remains a lifecycle integration issue to address separately.

Command:

```sh
ctest --test-dir build/dev-macos-debug -R '^timeline_track_audio_state_command_test$' --output-on-failure
```

Result: **1/1 passed**. The binary and source hashes are recorded in [track-audio-state-adapter-test-result.json](track-audio-state-adapter-test-result.json). The P9 audio plan uses the new action in its guarded authoring steps, but native execution remains pending the matching staged app binary and a freshly admitted audio task.
