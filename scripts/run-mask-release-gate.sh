#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

build_dir=""
real_image_manifest="${PHOTO_EDITOR_REAL_IMAGE_CORPUS_MANIFEST:-}"
schema_only_real_image=0

usage() {
  cat <<'EOF'
Run the release-blocking mask, matte, render, and real-image QA gate.

Usage:
  scripts/run-mask-release-gate.sh --build-dir PATH --real-image-manifest PATH

Options:
  --build-dir PATH              CMake build directory containing tests.
  --real-image-manifest PATH    Materialized real-image QA manifest.
  --schema-only-real-image      Validate real-image manifest contract without requiring image files.
  --help                        Show this help text.

Environment:
  PHOTO_EDITOR_REAL_IMAGE_CORPUS_MANIFEST can provide --real-image-manifest.
EOF
}

log() {
  printf '[mask-release-gate] %s\n' "$*"
}

fail() {
  printf '[mask-release-gate] error: %s\n' "$*" >&2
  exit 1
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --build-dir)
      [ "$#" -ge 2 ] || fail "--build-dir requires a value"
      build_dir="$2"
      shift 2
      ;;
    --real-image-manifest)
      [ "$#" -ge 2 ] || fail "--real-image-manifest requires a value"
      real_image_manifest="$2"
      shift 2
      ;;
    --schema-only-real-image)
      schema_only_real_image=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      fail "unknown option: $1"
      ;;
  esac
done

[ -n "${build_dir}" ] || fail "--build-dir is required"
[ -d "${build_dir}" ] || fail "build directory does not exist: ${build_dir}"
[ -n "${real_image_manifest}" ] || fail "real-image QA manifest is required; set PHOTO_EDITOR_REAL_IMAGE_CORPUS_MANIFEST or pass --real-image-manifest"
[ -f "${real_image_manifest}" ] || fail "real-image QA manifest does not exist: ${real_image_manifest}"

build_dir="$(cd "${build_dir}" && pwd)"
real_image_manifest="$(cd "$(dirname "${real_image_manifest}")" && pwd)/$(basename "${real_image_manifest}")"

cd "${REPO_ROOT}"

required_tests=(
  "render_parity_test"
  "render_golden_test"
  "editor_preview_math_test"
  "desktop_shell_brush_workflow_test"
  "desktop_shell_retouch_hud_test"
  "desktop_shell_selection_geometry_test"
  "desktop_shell_tool_exit_test"
  "desktop_shell_layer_hover_test"
  "synthetic_raw_pipeline_invariant_test"
)

for test_name in "${required_tests[@]}"; do
  if ! ctest --test-dir "${build_dir}" -N -R "^${test_name}$" | grep -q "${test_name}"; then
    fail "required release-gate test is missing from ${build_dir}: ${test_name}"
  fi
done

log "Running mask UX, matte math, render parity, golden, and synthetic RAW pipeline invariant tests"
for test_name in "${required_tests[@]}"; do
  log "Running ${test_name}"
  output="$(ctest --test-dir "${build_dir}" -R "^${test_name}$" --output-on-failure 2>&1)" || {
    printf '%s\n' "${output}"
    fail "${test_name} failed"
  }
  printf '%s\n' "${output}"
  if printf '%s\n' "${output}" | grep -Eq '(Skipped|did not run|The following tests did not run)'; then
    fail "${test_name} was skipped; release-gate tests must run"
  fi
done

real_image_args=("${real_image_manifest}")
if [ "${schema_only_real_image}" -eq 1 ]; then
  real_image_args+=(--schema-only)
fi

log "Validating real-image QA corpus"
python3 scripts/perf/check-real-image-corpus.py "${real_image_args[@]}"

log "mask_release_gate=passed"
