#!/usr/bin/env python3
"""Guard the single-binary macOS catalog-helper packaging contract."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    helpers = {
        line.strip()
        for line in (ROOT / "cmake/CatalogHelpers.txt").read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }
    multicall = (ROOT / "rust/crates/catalog/src/bin/catalog_helper.rs").read_text(
        encoding="utf-8"
    )
    modules = set(re.findall(r'^mod ([a-z][a-z0-9_]*);$', multicall, re.MULTILINE))
    dispatch = set(
        re.findall(
            r'^\s*"([a-z][a-z0-9_]*)" => \1::main\(\),$',
            multicall,
            re.MULTILINE,
        )
    )
    require(modules == helpers, "multicall modules must exactly match CatalogHelpers.txt")
    require(dispatch == helpers, "multicall dispatch must exactly match CatalogHelpers.txt")

    for helper in sorted(helpers):
        source = (ROOT / f"rust/crates/catalog/src/bin/{helper}.rs").read_text(encoding="utf-8")
        require(
            "pub(crate) fn main() -> std::process::ExitCode" in source,
            f"{helper} must expose its established entry point to catalog_helper",
        )

    packaging = (ROOT / "cmake/Packaging.cmake").read_text(encoding="utf-8")
    require(
        'set(catalog_helper_path "${PHOTO_EDITOR_RUST_BINARY_DIR}/catalog_helper")' in packaging
        and r'file(CREATE_LINK \"catalog_helper\"' in packaging
        and "SYMBOLIC" in packaging,
        "macOS packaging must install one catalog_helper and relative compatibility symlinks",
    )
    release_script = (ROOT / "scripts/build-release.sh").read_text(encoding="utf-8")
    require(
        "-p photo-catalog --bin catalog_helper --release" in release_script,
        "the macOS release script must build only the multicall helper",
    )

    print(f"catalog-helper multicall contract passed aliases={len(helpers)} physical_binaries=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
