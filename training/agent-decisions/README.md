# Advisory decisions for Looksy agents

This experiment separates a creative plan from a narrow next-step judgment.
The planner retains its complete approved tool set. The decision interface
returns a candidate ID and a binding to the input snapshot; it has no mutation
interface and grants no permission. No second model is enabled in normal editing.

## What runs in the app

[`agent_decision_frame.h`](../../apps/desktop/src/agent_decision_frame.h) defines
the shared `looksy-agent-decision-v1` contract. Candidates group only supplied
tools into discovery, inspection, document work, rendered review and delivery.
Asking, stopping and insufficient evidence remain explicit options. Required
tool-plan turns omit the finish-plan option. Groups describe purpose; native
schemas, command availability, ownership and admission still govern each call.

A response must name a current candidate and match the fresh request digest.
The source binding includes the full asset metadata, recipe, style, brief,
memory, tool schemas, receipts, phase and tool-plan flags. A change invalidates
the earlier answer even when the compact model-facing state looks identical.
The digest is a consistency check, not authentication or execution approval.

[`agent_decision_shadow.h`](../../apps/desktop/src/agent_decision_shadow.h)
captures compact text-only frames and the local planner's observed tool families.
Capture is disabled unless the app is launched with:

```sh
export PHOTO_EDITOR_AGENT_DECISION_SHADOW_DIR="$PWD/.photo-editor/agent-training/decision-shadow"
./scripts/run-native.sh
```

Use this on a future normal launch; do not restart an editor with unsaved work.
Capture adds no inference and changes neither the prompt, model response nor
available tools. Files are written atomically with owner-only permissions;
capture failure leaves planning intact. No uploads occur. Files contain private
brief/document text: named path, URL, endpoint and token fields are omitted,
but this is not a general personal-data sanitizer. Keep captures local and review
them before sharing or training. Every observed choice is marked **unreviewed,
not ground truth**.

State projection explicitly discloses omitted fields, caps text/list/depth sizes,
and prefers recent receipts when over budget. It excludes rendered images and
full style/memory/recipe content. A missing or omitted field cannot establish
completion, visual quality or permission. This first version observes Ollama-
transport planner attempts, including the experimental Gemma MLX bridge;
it does not instrument every deterministic planner or critic path.

External Claude/Codex/custom MCP clients receive the same capability-filtered
tool families and option purposes through `app.agent_guide`, workflow resources
and the creative prompt. These are advisory categories, not live document
decisions. No external model is assumed to consume them automatically.

Replay captured frames with both models, serially, without controlling the app:

```sh
bash scripts/agent-training/run-decision-comparison.sh \
  "$PWD/.photo-editor/agent-training/decision-shadow"
```

This creates a dated comparison under the ignored private training directory.
Repeated identical frames are deduplicated. Original planner answers are excluded
from replay input. Without independent review labels, judgments are explicitly
**unscored**: agreement between models is not evidence that either is correct.
An independent reviewer can add `"review":{"acceptableChoices":["inspect_state"]}`
outside a capture's frame. Do not edit the frame itself; its digest would fail.
Keep replay outputs local because model responses can echo document text.

## Local model comparison

The pinned [Clef Flash MLX conversion](https://huggingface.co/mlx-community/clef-flash-4bit)
is stored under `.photo-editor/agent-training/base/clef-flash-4bit`.
[`clef.lock.json`](clef.lock.json) pins revision, file sizes and SHA-256 hashes,
including its custom Python loader. The loader was inspected before use. The
experiment uses its joint decision head through `systemone()`, with truncation
disabled; ordinary chat generation does not run that head. No dependency
upgrades were needed in the existing private Gemma environment.

[`cases.json`](cases.json) contains 24 fictional, developer-authored next-step
fixtures. The native test exports [`frames.json`](frames.json) through the actual
contract builder. The labels allow alternatives on selected ambiguous cases and
stay outside all model requests. The labels are contract expectations, not
human-reviewed creative preferences. The Gemma pilot was trained on related
workflow mechanics; this is not a holdout of unseen task families. One initial
fixture named a nonexistent photo export-status tool. It was corrected to the
real Timeline export/inspection tools, and a native-catalog check now rejects
unknown names. Labels and model parameters were unchanged. The original frames
and diagnostic results are retained under `results`; the final comparison uses
the corrected native-catalog-validated inputs.

Both engines receive the same state, candidate descriptions and task
instructions. Gemma uses constrained JSON choice generation at temperature zero;
Clef uses its decision head. The caller binds the same digest for both engines,
avoiding generated checksum-copy overhead. Each engine runs in its own process,
with one warm-up excluded and three repeats in identical seeded order. Repeated
answers are **24 distinct fixtures, not 72 independent editing tasks**.
Latency includes preparation and choice validation, excludes initial model load,
and is descriptive for this host and corpus. Peak memory is MLX allocation,
not total system RAM. Clef probabilities are uncalibrated for Looksy.

The initial comparison is retained in `results/*-initial.json` and
`results/comparison-initial.json`. It produced 16/24 expected choices for each
model, consistently across three repeats. Initial medians were approximately
1.00 seconds for Gemma and 1.63 seconds for Clef. A build overlapped part of that
Gemma run, so its latency is diagnostic only. `results/fixture-v1-serial` retains
the serial rerun on those original fixtures. The final comparison in `results`
uses the corrected fixture set, runs Gemma then Clef, and starts after builds
finish. No fixture labels or model parameters were changed in response to scores.

Final measured results on the Apple M4 Pro / 48 GB host:

| Model | Expected choices, distinct fixtures | Warm median per choice | Peak MLX allocation |
| --- | --- | --- | --- |
| Gemma 4 26B-A4B with editing pilot adapter | 15/24 | 883 ms | 16.05 GB |
| Clef Flash MLX 4-bit | 16/24 | 1,622 ms | 6.94 GB |

All three repeats produced the same answers: 45/72 and 48/72 respectively.
See [paired summary](results/comparison.json), [Gemma raw responses](results/gemma.json)
and [Clef raw distributions](results/clef.json). Clef gains one expected choice
while taking about 1.84 times as long for a short routing judgment. This does
not justify adding another model to the normal editing loop. Its lower memory
use could matter as a standalone router, which this experiment does not qualify.
The app's selected/default model and the Gemma adapter weights were unchanged.

The two models make different mistakes. For example, Clef sometimes selects
document work before obtaining required IDs or command schemas. Gemma often
abstains where the fixture expects discovery or proof generation, and misses
the corrected pending-Timeline-export status case. Some scored misses are
conservative abstentions rather than dangerous actions. These labels measure
adherence to the fixture's expected next step; they are not a safety rate.
Neither output can bypass native admission. The added contract and capture
infrastructure are ready for actual app trajectories; decision-model control
remains disabled.

Reproduce from the repository root with the existing private Python environment:

```sh
python3 scripts/agent-training/prepare_decision_cases.py
build/dev-macos-debug/tests/agent_decision_frame_test --export-cases \
  training/agent-decisions/cases.json training/agent-decisions/frames.json
python3 scripts/agent-training/test_decisions.py

export HF_HUB_OFFLINE=1
export HF_HUB_DISABLE_TELEMETRY=1
export HF_HOME="$PWD/.photo-editor/agent-training/hf-cache"
.photo-editor/agent-training/venv/bin/python scripts/agent-training/evaluate_decisions.py \
  --engine clef --model .photo-editor/agent-training/base/clef-flash-4bit \
  --output training/agent-decisions/results/clef.json
.photo-editor/agent-training/venv/bin/python scripts/agent-training/evaluate_decisions.py \
  --engine gemma --model .photo-editor/agent-training/base/gemma4-26b-a4b-4bit \
  --adapter .photo-editor/agent-training/adapters/selected-editing-pilot \
  --output training/agent-decisions/results/gemma.json
python3 scripts/agent-training/evaluate_decisions.py --engine compare \
  --reports training/agent-decisions/results/gemma.json training/agent-decisions/results/clef.json \
  --output training/agent-decisions/results/comparison.json
```

If the pinned Clef files are absent, unset `HF_HUB_OFFLINE` and download them with
`scripts/agent-training/download_gemma.py --lock training/agent-decisions/clef.lock.json --output .photo-editor/agent-training/base/clef-flash-4bit`
using the private environment. Despite the script name, it verifies any manifest
with this file-lock format. It retains an 8 GiB disk reserve. Do not run model
benchmarks while another inference job is using the GPU.

## Adoption gate

This is a routing experiment. It does not execute the proposed edits, inspect
finished pixels, measure total autonomous completion time, or establish taste.
Before allowing a decision model to influence execution, collect normal app
trajectories, independently review the useful next steps and final output, and
compare completed tasks, loops, elapsed time, memory, visible quality and user
preference against the same planner without the extra model. Hold out entire
projects and brief families. Keep exact facts and authority checks in app code.
No model is automatically promoted by this experiment.
