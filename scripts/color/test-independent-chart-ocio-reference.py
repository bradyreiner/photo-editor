#!/usr/bin/env python3
"""Regression proof that the independent OCIO oracle stays float until write."""

from __future__ import annotations

import importlib.util
import math
import sys
from pathlib import Path


GENERATOR_PATH = Path(__file__).with_name(
    "render-independent-chart-ocio-reference.py"
)


def load_generator():
  spec = importlib.util.spec_from_file_location(
      "independent_chart_ocio_reference", GENERATOR_PATH
  )
  if spec is None or spec.loader is None:
    raise RuntimeError("unable to load independent OCIO reference generator")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def main() -> int:
  try:
    generator = load_generator()
  except ModuleNotFoundError as exc:
    print(f"SKIP: independent OCIO Python runtime unavailable: {exc}")
    return 0

  oiio = generator.oiio
  source = oiio.ImageBuf(oiio.ImageSpec(1, 1, 4, oiio.UINT8))
  source.setpixel(0, 0, (0.2, 0.20392156862745098, 0.2, 1.0))
  source_pixel = list(source.getpixel(0, 0)[:3])
  rendered = generator.apply_ocio_display_float(source)
  actual = list(rendered.getpixel(0, 0)[:3])

  configuration = generator.ocio.Config.CreateFromBuiltinConfig(
      generator.CONFIG_NAME
  )
  processor = configuration.getProcessor(
      generator.SOURCE_COLOR_SPACE,
      generator.DISPLAY,
      generator.VIEW,
      generator.ocio.TRANSFORM_DIR_FORWARD,
  ).getDefaultCPUProcessor()
  expected = list(processor.applyRGB(source_pixel))
  quantized_8_bit = [round(value * 255.0) / 255.0 for value in expected]

  if str(rendered.spec().format) != "float":
    raise AssertionError("independent transform result did not retain float storage")
  if max(abs(left - right) for left, right in zip(actual, expected)) > 2.0e-6:
    raise AssertionError(
        f"float reference differs from direct OCIO: {actual} versus {expected}"
    )
  if max(abs(left - right) for left, right in zip(actual, quantized_8_bit)) < 1.0e-4:
    raise AssertionError("reference was quantized to an 8-bit transform result")
  if not all(math.isfinite(value) for value in actual):
    raise AssertionError("reference transform produced a non-finite value")

  print(
      "independent OCIO float-intermediate proof passed: "
      f"actual={actual} eight_bit={quantized_8_bit}"
  )
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
