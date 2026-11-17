# Native review excerpt verification

## Build

- `cmake --build build/dev-macos-debug --target photo_automation_bridge_test timeline_mcp_export_range_test timeline_document_controller_test -j 2` completed with exit code 0 in tool session 74293. Ninja warned that `.ninja_log` ended prematurely, recovered it, and rebuilt 304 objects. The output was captured in the agent tool session and was not saved as a separate log file.

## Focused checks

- `timeline_review_excerpt_export_workflow_test`: passed, 1/1, 10.28 s (tool session 82418).
- `timeline_mcp_export_range_test`: passed, 1/1, 0.25 s (tool session 35946). Verifies range bounds/fingerprints, active-source filtering, stat-pinned dependencies, worker hashing, and fail-closed changed-source detection.
- `build/dev-macos-debug/tests/photo_automation_bridge_test --timeline-h264-export-contract`: passed (tool session 89686, run with elevated sandbox permission so the local MCP server could bind its localhost socket). Covers full-timeline default admission and native worker completion.
- `timeline_source_replacement_contract_test`: passed in an earlier focused run.

## Render evidence

The retained MP4 is a 2-second excerpt for global frames `[30, 90)` at 30 fps from a 4K sequence. ffprobe reports H.264, 3840x2160, 60 decoded video frames, stereo AAC at 48 kHz, video/audio start time 0, and both streams at 2.000 seconds. The test measured audio mean volume at -24.1 dB and confirmed generated animated-content samples differ across the excerpt. See the adjacent ffprobe JSON, verification JSON, MP4, and start/end PNGs.

## Separate failures

- The sandboxed CTest invocation of the export contract could not start the local bridge; the same already-built focused binary passed with localhost binding allowed.
- The broad `photo_automation_bridge_test` fails later at `photo_read_asset should return selected asset metadata`, outside the export contract. The isolated export contract above passes.
