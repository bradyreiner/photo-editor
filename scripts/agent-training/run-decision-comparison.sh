#!/usr/bin/env bash
set -euo pipefail
decision_repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
decision_runtime="${decision_repo_root}/.photo-editor/agent-training"
decision_frames="${1:-${decision_repo_root}/training/agent-decisions/frames.json}"
decision_output="${2:-${decision_runtime}/decision-comparisons/$(date -u +%Y%m%dT%H%M%SZ)}"
export HF_HUB_OFFLINE=1
export HF_HUB_DISABLE_TELEMETRY=1
export HF_HOME="${decision_runtime}/hf-cache"
cd "${decision_repo_root}"
mkdir -p "${decision_output}"
"${decision_runtime}/venv/bin/python" -u scripts/agent-training/evaluate_decisions.py \
  --engine gemma --frames "${decision_frames}" \
  --model "${decision_runtime}/base/gemma4-26b-a4b-4bit" \
  --adapter "${decision_runtime}/adapters/selected-editing-pilot" \
  --output "${decision_output}/gemma.json"
"${decision_runtime}/venv/bin/python" -u scripts/agent-training/evaluate_decisions.py \
  --engine clef --frames "${decision_frames}" \
  --model "${decision_runtime}/base/clef-flash-4bit" \
  --output "${decision_output}/clef.json"
"${decision_runtime}/venv/bin/python" scripts/agent-training/evaluate_decisions.py \
  --engine compare --reports "${decision_output}/gemma.json" "${decision_output}/clef.json" \
  --output "${decision_output}/comparison.json"
