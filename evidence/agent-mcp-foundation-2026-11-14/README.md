# Agent and MCP foundation verification — 2026-11-14

Implemented standard desktop MCP initialization, ping and notification handling;
five workflow resources; a creative-brief prompt; the capability-filtered
`app.agent_guide` tool; and bounded rendered PNG image content for external photo
proof/validation calls. Added a dependency-free stdio adapter and bundled it with
the desktop build. Resident and external agents share creative/workmanship
guidance. Photo-intent system prompts omit construction-specific mechanics.

## Verified scope

- Six native checks pass: `agent_local_model_test`, `photo_automation_bridge_test`,
  `agent_mcp_discovery_contract_test`, `agent_internal_tools_test`,
  `composition_tool_contract`, `search_edit_tool_contract`.
- Six stdio adapter tests pass against a mock loopback editor: initialization,
  notifications, authentication, endpoint validation, redirection/error redaction,
  no mutation replay, invalid input/size handling and session pinning.
- Benchmark runner checks pass, including rejecting mixed source/model receipts.
- `photo_editor_app` builds. The app bundle contains the adapter and interface
  guide. The existing running editor was not restarted.
- Fixed the native bridge test target's missing Carousel dependency and selected
  its fixtures by stable identity instead of assuming catalog row order.

## Context failure and repair

The initial live synthetic-photo smoke never reached Ollama: estimated context
was 9,073 tokens against an 8,192-token budget. Its system prompt included detailed
construction instructions with tool calls disabled. Task-specific photo guidance
reduced aggregate text from 19,907 to 15,253 bytes and the estimate to 7,910 tokens.
The identical smoke then received real image-grounded planner and critic responses
without fallback. The admission guard and context limit remain intact.

`baseline-35b-01-failure.json` retains the initial rejection. The 9B candidate
then correctly declined that abstract fixture as unsuitable for photographic
editing; `candidate-9b-01-failure.json` retains its response. That refusal is a
smoke-fixture limitation, not evidence of worse instruction following.

## Real-photo timing screen

Host: Apple M4 Pro, 48 GiB memory, external Ollama 0.40.13. Five warm runs per
model used the same repository landscape, the same binary, 8,192-token context,
768-pixel planning edge and 1,536-token planning output limit. The source SHA-256
is recorded in every sample and must match within an aggregate.

| Installed artifact | Median plan | Median critique | Median total |
| --- | ---: | ---: | ---: |
| `qwen3.6:35b`, Q4_K_M, mixture of experts | 8,827 ms | 1,661 ms | 10,476 ms |
| `qwen3.5:9b`, NVFP4 | 9,652 ms | 2,731 ms | 13,072 ms |

Medians of individual stages do not necessarily sum to the median paired total.
The smaller candidate's paired median was 24.78% slower. It misses the screening
gate of at least a 20% latency improvement, so no default promotion was made.
Model digests and sizes are in `model-artifacts.json`; exact sample receipts and
aggregates are under `realphoto-35b/` and `realphoto-9b/`.

This is one-input timing and response-contract evidence. The smoke critic reviews
the provided photograph; the test does not apply the planned recipe or inspect
the resulting edited image. It does not qualify model creativity, photographic
quality, general instruction following, multi-step tool completion, or a complete
deliverable. The two installed artifacts also have different architectures and
quantization; the result applies to these artifacts on this host.

## Remaining work

Qualify real Claude/Codex/custom-client work in a packaged editor; finish app
connection setup; extend resource-owned adapters where actual briefs encounter
missing controls; retrieve tool families/examples by task; and evaluate unseen
creative briefs through rendered review, save/export/reopen/revert and human
preference. No model weights were trained. The model adaptation/data plan is in
[`docs/looksy-agent-interface.md`](../../docs/looksy-agent-interface.md).

Machine-readable summary and source/binary hashes: [`verification.json`](verification.json).
