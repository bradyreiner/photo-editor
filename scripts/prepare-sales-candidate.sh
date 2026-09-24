#!/usr/bin/env bash
# Build a local engineering candidate while production credentials are pending.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"
candidate_root="${1:-${repo_root}/build/sales-candidate-$(date -u +%Y%m%dT%H%M%SZ)}"
postgres_source="${PHOTO_EDITOR_POSTGRES_RUNTIME_SOURCE:-}"
if [ "${1:-}" = "--help" ]; then
  printf '%s\n' 'Usage: bash scripts/prepare-sales-candidate.sh [NEW_OUTPUT_DIRECTORY]' \
    'Builds an unsigned Apple silicon engineering candidate, then verifies its bundled catalog.' \
    'Set PHOTO_EDITOR_POSTGRES_RUNTIME_SOURCE to PostgreSQL 14 (defaults to Homebrew postgresql@14).' \
    'Uses the release-macos build directory. Does not sign, publish, or modify your catalog.'
  exit 0
fi
[ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ] || {
  printf '%s\n' 'This candidate path requires an Apple silicon Mac.' >&2; exit 1;
}
[ ! -e "${candidate_root}" ] || {
  printf '%s\n' 'Choose a new output directory so prior evidence is preserved.' >&2; exit 1;
}
if [ -z "${postgres_source}" ]; then
  postgres_source="$(brew --prefix postgresql@14)"
fi
mkdir -p "${candidate_root}"
candidate_root="$(cd "${candidate_root}" && pwd)"
printf '%s\n' 'Local engineering candidate: Developer ID, notarization, updater and external QA remain required.' \
  > "${candidate_root}/CANDIDATE-NOT-FOR-DISTRIBUTION.txt"

python3 scripts/release-preflight.py --mode candidate --allow-dirty
python3 -B scripts/test-release-commerce.py
node --test scripts/test-download-page.cjs
cargo build --manifest-path rust/Cargo.toml -p photo-catalog --bin catalog_helper --release --locked
bash scripts/prepare-macos-postgres-runtime.sh "${postgres_source}" "${candidate_root}/postgres-runtime" \
  > "${candidate_root}/postgres-relocation.log" 2>&1
cmake --preset release-macos \
  "-DPHOTO_EDITOR_RUST_BINARY_DIR=${repo_root}/rust/target/release" \
  "-DPHOTO_EDITOR_POSTGRES_RUNTIME_DIR=${candidate_root}/postgres-runtime" \
  -DFRAMEHOUSE_LEGAL_PUBLISHER='Local verification only' \
  -DPHOTO_EDITOR_STRICT_PACKAGING=OFF \
  -DPHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER=OFF \
  > "${candidate_root}/configure.log" 2>&1
cmake --build --preset build-release-macos --target photo_editor_app --parallel 6 \
  > "${candidate_root}/build.log" 2>&1
cpack --config build/release-macos/CPackConfig.cmake -B "${candidate_root}/package" \
  > "${candidate_root}/package.log" 2>&1
version="$(tr -d '[:space:]' < VERSION)"
dmg="${candidate_root}/package/Looksy-${version}-Darwin-arm64.dmg"
python3 scripts/generate-third-party-inventory.py --output "${candidate_root}/package/third-party-inventory.json"
python3 scripts/write-release-manifest.py --artifacts "${candidate_root}/package" \
  --output "${candidate_root}/package/release-manifest.json" --version "${version}" --channel local-candidate
(
  cd "${candidate_root}/package"
  shasum -a 256 "Looksy-${version}-Darwin-arm64.dmg" > SHA256SUMS.txt
)
python3 scripts/audit-launch-readiness.py --output "${candidate_root}/readiness.json" --package "${dmg}"
bash scripts/verify-macos-release.sh --dmg "${dmg}" --allow-no-sparkle \
  > "${candidate_root}/package-verification.log" 2>&1
printf 'Local package and verification: %s\n' "${candidate_root}"
printf '%s\n' 'This is not a signed release or clean-machine acceptance proof.'
