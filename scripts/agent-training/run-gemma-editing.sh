#!/usr/bin/env bash
set -euo pipefail
training_repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
training_runtime="${training_repo_root}/.photo-editor/agent-training"
export HF_HUB_OFFLINE=1
export HF_HUB_DISABLE_TELEMETRY=1
export HF_HOME="${training_runtime}/hf-cache"
exec "${training_runtime}/venv/bin/python" -u \
  "${training_repo_root}/scripts/agent-training/serve_gemma_editing.py" \
  --model "${training_runtime}/base/gemma4-26b-a4b-4bit" \
  --adapter "${training_runtime}/adapters/selected-editing-pilot" \
  "$@"
