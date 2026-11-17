# Looksy agent interface

Implementation and initial local-model evidence:
[2026-11-14 verification](../evidence/agent-mcp-foundation-2026-11-14/README.md).

The running editor and the repository have different MCP servers. The Rust
`photo-editor-mcp` server in `.mcp.json` helps develop, test and diagnose this
repository. The desktop MCP server controls the user's running editor. External
agents should use the desktop server for creative work, without needing the
source tree, Cargo, or the resident agent's model.

## Connect an external agent

Launch a build containing the desktop MCP changes with local automation enabled.
For a development launch, set these variables before `./scripts/run-native.sh`:

```sh
export PHOTO_EDITOR_ENABLE_AUTOMATION=1
export PHOTO_EDITOR_AUTOMATION_CAPABILITIES=read,draft,export
./scripts/run-native.sh
```

The default capability is `read`. Add `commit` only when that client should be
able to request commits; the app's confirmation and customer access policies
continue to apply. The adapter does not enable automation or grant capabilities.
Do not close an editor with unsaved work to change its launch configuration.

Register a **stdio** MCP server in the client with:

- Command: `python3`
- Arguments: `/absolute/path/to/looksy-mcp.py`

The distributable adapter is [`scripts/looksy-mcp.py`](../scripts/looksy-mcp.py).
It needs Python 3.9 or newer and the running editor, with no third-party Python
dependencies. For clients using `mcpServers` JSON configuration:

```json
{
  "mcpServers": {
    "looksy-desktop": {
      "command": "python3",
      "args": ["/absolute/path/to/looksy-mcp.py"]
    }
  }
}
```

Use this command/argument pair in Claude, Codex or another MCP-capable client;
the registration location depends on that client. The adapter can be distributed
as a single file. Desktop builds also copy the adapter and this guide into
`Contents/Resources/agent` on macOS, or `agent` beside the executable elsewhere.
A connection setup surface and signed-release client qualification remain
product delivery tasks.

The adapter honors `PHOTO_EDITOR_APP_DATA_DIR`, then checks the native launcher's
`PhotoEditor/automation/session.json` before Qt's default
`PhotoEditor/PhotoEditor/automation/session.json` under the platform's application
data directory (on macOS, `~/Library/Application Support`). An existing native
record is authoritative, including when external MCP is disabled; the adapter
does not fall through to another editor. Override it with
`--session-file /absolute/path/to/automation/session.json` or
`PHOTO_EDITOR_AUTOMATION_SESSION_FILE` when using a custom app data directory.
No port or bearer token belongs in client configuration. The adapter pins the
session after connecting, reloads it on a new `initialize`, and never retries an
uncertain mutation. Reconnect the client after restarting Looksy.

Custom clients may also read that private session locally and POST JSON-RPC to
`mcpEndpoint` with `Authorization: Bearer <token>`, accepting JSON responses.
Initialize first and send `notifications/initialized`; accepted notifications
return HTTP 202 with an empty body. Use the negotiated `MCP-Protocol-Version`
header on subsequent requests. Never publish or copy the session token into a
prompt, repository or log.

## Give the model context that helps it act

The server's `initialize.instructions` points clients to `app.agent_guide`.
Call it with `{"workflow":"design"}` (or `overview`, `photo`, `timeline`,
`delivery`). It returns tool purposes and a workflow, filtered to the connection's
capabilities. Native availability and resource ownership must still be discovered
from live commands; a guide never grants editing authority.

The same context is available through:

- `resources/list` and `resources/read`, using
  `looksy://agent/workflows/design` and the other workflow names.
- `prompts/list` and `prompts/get`, using `create-with-looksy` with required
  `brief` and optional `workflow` arguments.

Clients control whether resources, prompts and initialization instructions reach
the model. The guide is also a regular tool so tool-only hosts have a discovery
path. Do not assume a resource is automatically included in every model prompt.
These interfaces follow the MCP [lifecycle](https://modelcontextprotocol.io/specification/2025-06-18/basic/lifecycle),
[resources](https://modelcontextprotocol.io/specification/2025-11-25/server/resources)
and [prompts](https://modelcontextprotocol.io/specification/2025-11-25/server/prompts)
contracts.

The resident planner and external guide share the creative workflow and workmanship
instructions. They encourage interpreting an open brief, choosing a coherent
direction, combining primitives, building an editable sample, inspecting delivery
size, and changing approach when a repair stalls. Current copy, protected content,
stable IDs, app policy and actual rendered proof constrain that creative freedom.
This is guidance, not evidence that a model produces professional results.

External calls to `photo.render_current_proof` and `photo.validate_edit` now include
a standard MCP PNG image block as well as structured metadata. The preview is
bounded to 1024 pixels per edge; its receipt binds source and preview SHA-256
hashes. A missing/unsupported proof is explicitly reported as unavailable. A
client's model must receive and inspect the image to perform visual review; a
local file path in text alone cannot provide that evidence to a remote client.

## What still needs work

The [advisory decision experiment](../training/agent-decisions/README.md) adds
shared tool-family choices, snapshot-bound responses and opt-in local shadow
capture. Workflow guides expose those choices to external clients. The planner's
tool access remains intact; the separate decision model has no editing authority.

1. **Complete resource ownership before exposing more mutations.** Use
   `app.list_commands` by domain/query, then `app.command_schema`. Prioritize
   creating/opening documents, importing sources, Carousel/board editing, Scene,
   selection/retouch and delivery gaps revealed by real briefs. Each adapter needs
   stable resources, admission, reversible edits, async completion/cancellation and
   fresh readback. Do not turn every Qt method into an untyped universal tool.
2. **Retrieve context by task and phase.** The guide provides purpose-based entry
   points today; the resident planner still has substantial prompt/schema context.
   Photo-intent turns now omit construction-specific system guidance. Measure
   the remaining audits, then retrieve the relevant command family and a few approved
   examples. Keep schemas and permissions exact; cache stable prefixes and compact
   receipts. Avoid dumping the whole native inventory into each model request.
3. **Evaluate complete creative work.** Keep an unseen suite covering photographic
   correction, localized repair, editable poster, subject cutout composition,
   multi-board campaign and Timeline delivery. Score exact instructions, visible
   composition, text/readability, editability, productive tool completion,
   save/export/reopen/revert, and human preference. Keep failure cases.
4. **Finish connection setup.** The build bundles the adapter and this guide.
   Provide client-neutral setup controls in the app, and qualify real Claude/Codex/custom-client workflows
   against a packaged editor. The focused protocol tests do not establish those
   complete client workflows.

## Faster local models and weight tuning

The inspected development host is an Apple M4 Pro with 48 GiB unified memory.
Its installed `qwen3.6:35b` reports `qwen35moe`, Q4_K_M, vision and tools. Model
labels alone cannot establish throughput or instruction following.

The app manifest now includes external Ollama evaluation candidates
`ollama-qwen35-9b` (`qwen3.5:9b`) and `ollama-qwen35-4b` (`qwen3.5:4b`). They
are unavailable until installed and explicitly probed. Neither replaces the
default or enters the checksum-pinned managed model bundle. The upstream
[Ollama model catalog](https://ollama.com/library/qwen3.5) advertises vision/tools;
the [Qwen 9B model card](https://huggingface.co/Qwen/Qwen3.5-9B) states Apache 2.0.
That establishes candidates, not app qualification.

Use the existing `scripts/perf/run-agent-live-tier-benchmark.py` with matching
`--model-id` and `--ollama-model`, an isolated output directory, and the same
hardware tier. It measures image-grounded planning and critique, not autonomous
multi-step creative execution. Its critic reviews the provided fixture; this
smoke test does not apply the planned recipe or verify the resulting edited image.
Cold samples explicitly stop a model; run only
when the runtime is free. Use `--source-image tests/fixtures/creative-tools/landscape-mountain-source.jpg`
for a real-photo input rather than the synthetic default; receipts bind the
source image hash and aggregates reject mixed sources/models. The runner requires
at least five samples for percentile evidence. A promising candidate needs at
least a 20% median latency
gain with valid image-grounded responses and no fallback, then the unseen workflow
suite above before promotion. More tokens per second alone is insufficient.

For training, keep changing tool schemas and project taste in runtime context.
The first local Gemma 4 26B-A4B QLoRA pilot, its reproducible training tools and
evaluation limits are in [the Gemma editing experiment](../training/gemma4-editing/README.md).
Train stable behaviors such as tool selection, correct arguments, reading proof,
repairing a specific failure, and recognizing completion. Start with LoRA or QLoRA
adapters on a supported small base model, retaining the original checkpoint.
[MLX-LM](https://github.com/ml-explore/mlx-lm/blob/main/mlx_lm/LORA.md) supports
LoRA/QLoRA for text models; [MLX-VLM](https://github.com/Neywa-Labs/mlx-vlm)
provides vision-language fine-tuning. Verify the exact model architecture,
training template and inference/export path before committing to either.

Build a curated dataset from rights-cleared tasks with actual tool schemas,
observations, successful tool arguments, returned IDs, visible review, corrections
and delivery receipts. Remove tokens, personal paths and private photo content
unless explicitly included by the owner. Split by project/source/brief family so
near duplicates cannot leak into evaluation. Rejected outputs are failure evidence,
not successful demonstrations. Use human-approved direction choices for later
preference tuning. Never teach the model that an accepted tool call means a
visually successful result.

Only fuse/export/quantize an adapter after held-out completion and visual quality
improve without regressions. Record base revision/hash, adapter hash, dataset and
split hashes, training config, quantization, license and evaluation results. Keep
a rollback route. No model weights have been trained or automatically promoted
by this interface change.
