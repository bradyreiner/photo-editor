#!/usr/bin/env python3
"""Adversarial tests for the proprietary color-engine boundary validator."""

from __future__ import annotations

import importlib.util
import json
import tempfile
from pathlib import Path
from typing import Any, Callable


VALIDATOR_PATH = Path(__file__).with_name(
    "validate-proprietary-color-boundary.py"
)


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location(
      "proprietary_color_boundary_validator", VALIDATOR_PATH
  )
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load validator: {VALIDATOR_PATH}")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def write(path: Path, value: str) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_text(value, encoding="utf-8")


def main() -> int:
  validator = load_validator()
  with tempfile.TemporaryDirectory(prefix="proprietary-color-boundary-") as temp:
    root = Path(temp)
    write(root / "CMakeLists.txt", "project(fixture)\n")
    write(root / "vcpkg.json", '{"dependencies":["libraw","lcms"]}\n')
    write(
        root / "engine/color.cpp",
        'const char* output = "Adobe RGB"; // interoperable color-space name\n',
    )
    write(
        root / "tests/fixtures/color/reference.json",
        json.dumps({"producer": "project-authored DNG/CIE oracle"}) + "\n",
    )
    errors, _ = validator.validate_root(root)
    if errors:
      raise AssertionError(f"valid proprietary fixture rejected: {errors}")

    cases: list[tuple[str, Callable[[], None], str]] = [
        (
            "dependency",
            lambda: write(root / "cmake/Color.cmake", "find_package(DNG_SDK)\n"),
            "SDK dependency",
        ),
        (
            "header",
            lambda: write(root / "engine/sdk.cpp", '#include "dng_host.h"\n'),
            "SDK header",
        ),
        (
            "network-api",
            lambda: write(
                root / "apps/client.cpp",
                'const char* endpoint = "https://api.adobe.com/color";\n',
            ),
            "network API",
        ),
        (
            "fixture-producer",
            lambda: write(
                root / "tests/fixtures/color/reference.json",
                json.dumps({"producer": "Adobe Camera Raw"}) + "\n",
            ),
            "Adobe-produced fixture",
        ),
        (
            "fixture-name",
            lambda: write(
                root / "tests/fixtures/color/adobe-generated-reference.dng", "fixture"
            ),
            "fixture name",
        ),
    ]
    for name, mutate, expected in cases:
      # Reset all adversarial files so every case proves its own rule.
      for path in (
          root / "cmake/Color.cmake",
          root / "engine/sdk.cpp",
          root / "apps/client.cpp",
          root / "tests/fixtures/color/adobe-generated-reference.dng",
      ):
        path.unlink(missing_ok=True)
      write(
          root / "tests/fixtures/color/reference.json",
          json.dumps({"producer": "project-authored DNG/CIE oracle"}) + "\n",
      )
      mutate()
      errors, _ = validator.validate_root(root)
      if not any(expected in error for error in errors):
        raise AssertionError(f"{name} violation was accepted: {errors}")

  print("proprietary color-boundary adversarial tests passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
