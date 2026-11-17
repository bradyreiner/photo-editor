# Local Gemma 4 26B-A4B editing experiment

This is a local language-side workflow QLoRA experiment on the MLX 4-bit
instruction checkpoint of Gemma 4 26B-A4B. It does not train an image generator
or establish professional photographic/design quality. Base, vision, embeddings,
expert and router weights remain frozen. Only rank-8 attention adapters on the
last six language layers are trained.

## Pilot results — 2026-11-14

| Checkpoint | Expected next actions matched |
| --- | ---: |
| Original base | 14 / 24 |
| Rejected pilot with the upstream padding-mask forward | 12 / 24 |
| Corrected causal-training pilot | 18 / 24 |

The corrected pilot trained 729,088 adapter parameters for 100 steps in about
250 seconds on an M4 Pro with 48 GiB memory. Peak MLX allocation was 19.81 GB.
Validation loss fell from 2.495 to 0.0281 on the four validation batches used for
checkpoint selection. Both the base and corrected adapter describe the separate
mountain photo sensibly; this is one manual semantic smoke check.

The improvement is in exact workflow responses, especially layer inspection and
argument selection. Six expected-action checks still fail: discovery filtering
and the protocol used to report missing permission or a changed target. In the
observed negative cases the adapter declines mutation or asks for inspection,
but does not use the required `cannot_complete_safely` decision consistently.
The current default remains unchanged. Raw receipts are under [`results/`](results/).

The selected adapter is 2,919,317 bytes and contains 24 finite LoRA tensors,
including 364,544 nonzero B parameters. The original checkpoint files were
checksum-verified again after training. The compiled app's real planner and
critic completed an image-grounded smoke through the MLX bridge without
fallback; streamed JSON-schema chat and model discovery also passed. That smoke
reviews the supplied landscape fixture rather than rendering the planned edit,
so it establishes transport/response compatibility rather than finished output.

The selected local adapter is
`.photo-editor/agent-training/adapters/selected-editing-pilot`. Rejected and
intermediate checkpoints are retained separately. The temporary verification
server is stopped after testing; use the launch command below when trying it.

The base is stored once at
`.photo-editor/agent-training/base/gemma4-26b-a4b-4bit`. The pinned revision,
15,373,588,575-byte inventory and SHA-256 hashes are in `model.lock.json`.
The MLX checkpoint is from
[`mlx-community/gemma-4-26b-a4b-it-4bit`](https://huggingface.co/mlx-community/gemma-4-26b-a4b-it-4bit),
derived from Google's Apache-2.0
[`gemma-4-26B-A4B-it`](https://huggingface.co/google/gemma-4-26B-A4B-it).

## Corpus and evaluation boundary

`data/` contains 144 synthetic training, 24 validation and 24 test examples across
12 next-action families: inspection, text, typography, transforms, text creation,
creation-receipt IDs, stale-ID repair, duplicate-receipt handling, fresh proof,
native discovery, missing permission and changed targets. Every positive action
is checked against the exported native tool schema in `tool-catalog.json`.
Corpus hashes are recorded in `data/manifest.json` and each adapter experiment.

The splits have distinct prompts, copy/values and asset/layer IDs; the same tool
families appear in each split. This is a small synthetic mechanics corpus,
not human-approved creative demonstrations or an unseen-domain benchmark. No
customer photos, private project histories or credentials are used for training.
The repository landscape is used only for a separate inference description smoke.

Evaluation generates raw JSON without a grammar and checks one expected next
action, exact required arguments, native schema validity and expected safe-stop
decisions. It allows optional discovery pagination defaults. It is deliberately
strict and may reject an alternative useful action; its score is not a general
measure of editing ability or taste. Training uses assistant-completion loss only.

## Training correctness

The initial MLX-VLM 0.7.12 pilot exposed a training-mask problem. Its SFT forward
passes a two-dimensional padding mask into `gemma4`, overriding the model's own
causal/window masks. In a regression probe, changing only future tokens changed
earlier logits by 27.5. The normal model-derived causal forward changed them by
0.0. The misleadingly low training/validation loss did not translate to better
held-out next actions; this pilot is retained as rejected evidence.

`gemma_editing_experiment.py` applies a process-local training guard for this
specific Gemma model: batch-one right-padded text uses the model's own causal
attention, while the padding and completion masks remain active in the loss.
Installed packages and the inference implementation are unchanged. Each training
run executes the future-token invariance probe before optimization. Do not reuse
this guard for multi-image, left-padded or larger-batch training without new tests.

Both pilots use 100 optimizer updates, learning rate 5e-5, rank 8/alpha 16, batch
one, gradient checkpointing, maximum 1,536 tokens, seed 42 and a 34 GiB MLX limit.
Actual corpus sequence lengths are 483–938 tokens. Validation checks use four
batches every 25 updates; checkpoint choice uses that validation loss rather than
test responses. Exact configuration, metrics and selection receipts remain beside
the adapter weights.

## Reproduce and run

From the repository root, create an isolated Python 3.12 environment and install
the exact resolved packages:

```sh
uv venv .photo-editor/agent-training/venv --python python3.12
uv pip install --python .photo-editor/agent-training/venv/bin/python -r training/gemma4-editing/requirements.lock.txt
.photo-editor/agent-training/venv/bin/python scripts/agent-training/download_gemma.py
```

For an existing download, add `--verify-only` to check every file without network
access. To regenerate the schema snapshot from the built native test driver:

```sh
build/dev-macos-debug/tests/photo_automation_bridge_test --write-mcp-tool-catalog training/gemma4-editing/tool-catalog.json
.photo-editor/agent-training/venv/bin/python scripts/agent-training/prepare_editing_data.py --catalog training/gemma4-editing/tool-catalog.json --output training/gemma4-editing/data
```

The experiment runner has `evaluate` and `train` modes. Pass `--model`, a new
`--output` destination and, for evaluation, optional `--adapter`, `--limit`,
`--split` and `--image`. Training output paths must be new. The tracked
`training/gemma4-editing/tool-catalog.json` is the default catalog.
`train --iters 100` creates only adapter checkpoints; it
never overwrites or fuses the 15 GB base.

After a selected adapter is present at
`.photo-editor/agent-training/adapters/selected-editing-pilot`, start the local
experimental inference bridge:

```sh
bash scripts/agent-training/run-gemma-editing.sh
```

Wait for the `ready` receipt. The bridge binds only `127.0.0.1:11435`, rejects
foreign browser origins and exposes only model discovery and chat. It implements
the subset of Ollama's protocol used by Looksy, including bounded image inputs,
JSON-schema output and streamed chat. It uses MLX, not an Ollama-managed model.
The optional app entry is **Gemma 4 26B-A4B editing pilot (MLX)**. The existing
app default is unchanged. Stop this dedicated server with Ctrl-C when finished.
The model entry requires a build containing the updated model manifest; do not
close an editor with unsaved work merely to refresh it.

Native app editing, rendered quality, complete deliverables, save/export/reopen/
revert, broad instruction following and human preference still need qualification
before promoting a pilot to the default. Further tuning should use actual
reviewed editing trajectories and a larger independently held-out task set.
