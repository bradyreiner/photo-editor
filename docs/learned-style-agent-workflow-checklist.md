# Learned Style Agent Workflow Checklist

## Goal

Let a user select edited photos or a project, create a reusable style profile from that work, and let the local agent use that profile plus tool knowledge, photo-quality rules, and project workflow policy when planning edits.

## User Workflow

- [x] User can select one or more edited photos in the catalog/project.
- [x] User can create a learned style profile from the current selection.
- [x] User can preview a style-aware project run plan before bulk editing.
- [x] User can save sample-edit feedback back into the active learned style profile.
- [x] The created style becomes the active agent style.
- [x] The style records source asset metadata and recipe summaries instead of embedding image bytes.
- [x] The style records derived edit tendencies such as average tone/color/detail/crop adjustments.
- [x] The style records notes about the user's editing intent.
- [x] The Studio agent panel exposes a `Learn from selection` command.
- [x] The Studio agent panel shows whether the active style is learned from selected photos.

## Style Profile Contract

- [x] Learned profiles are stored inside normal agent style JSON, not a separate system.
- [x] Runtime styles expose `learnedProfile`, `toolKnowledge`, `qualityModel`, and `projectWorkflow`.
- [x] Saved/exported styles persist those sections as `learned_profile`, `tool_knowledge`, `quality_model`, and `project_workflow`.
- [x] Reference examples are metadata-only: asset id, file path, capture metadata, project summary, and recipe summary.
- [x] Before/after pairs are inferred from source/virtual-copy relationships, with a low-confidence selected-pair fallback for two-reference learning sets.
- [x] Reference examples can include rendered visual fingerprints when the selected source can be decoded locally.
- [x] Embedded image payloads, base64 blobs, data URLs, and byte payloads are rejected by style validation.
- [x] Existing text-rulebook styles remain valid.
- [x] Learned style profiles can opt into `reference_policy.mode = learned_style_profile` with `reference_image_matching = metadata_only`.

## Agent Context

- [x] Local model prompts already serialize the full active style JSON.
- [x] Learned profile context is included in Ollama/Qwen prompt payloads.
- [x] Tool knowledge is included in the same style payload so the model understands when each editing tool should be used.
- [x] Quality priorities are included in the style payload so the model relates "good photo" judgment to the user's style.
- [x] Project workflow policy is included in the style payload so the model can plan sample-first, cluster-aware, validation-first runs.
- [x] Project planning preview exposes lighting/metadata clusters, representative sample assets, and before/after pair metadata.
- [x] Existing validation and model repair paths continue to use the selected style.
- [x] Validation reports include per-image style-match scoring for learned profiles using recipe and rendered visual fingerprints.

## Safety And Validation

- [x] The app remains the editor of record; the model receives style context and returns structured plans.
- [x] The model cannot write style files or image files directly.
- [x] Style validation rejects executable/script-like payload keys.
- [x] Style validation rejects embedded image payloads in learned references.
- [x] Recipe patch validation still gates any model edit.
- [x] Run manifests continue to record the selected style and model context.

## Tests

- [x] `agent_edit_style_test` covers learned reference metadata acceptance and embedded payload rejection.
- [x] `agent_edit_controller_test` covers creating, saving, selecting, and persisting a learned style profile from selected assets.
- [x] `agent_edit_controller_test` covers before/after pair inference, project run planning, feedback persistence, and style-match scoring.
- [x] `agent_local_model_test` covers learned profile/tool/quality/workflow context reaching the Ollama request prompt.

## Completed Expansion

- [x] Add before/after pair grouping when the catalog can identify an original and edited derivative, plus selected-pair fallback metadata.
- [x] Add deterministic visual fingerprint similarity for style references.
- [x] Add a project planning preview that clusters lighting groups before bulk edits.
- [x] Add sample-edit approval feedback that updates the active learned profile for the current project.
- [x] Add per-image style-match scoring after validation renders.

Future ML upgrade: the deterministic visual fingerprint can be replaced or augmented by a neural/perceptual embedding runtime when the app ships one.
