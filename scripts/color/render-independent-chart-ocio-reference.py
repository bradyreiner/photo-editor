#!/usr/bin/env python3
"""Render one tagged chart through a direct, non-Photo-Editor ACES 2 path."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import sys
from pathlib import Path, PurePosixPath
from typing import Any

import OpenImageIO as oiio
import PyOpenColorIO as ocio


SCHEMA = "photo-editor-independent-chart-ocio-reference-report:v2"
TOOL_VERSION = "photo-editor-independent-chart-ocio-reference:v2"
CONFIG_NAME = "cg-config-v4.0.0_aces-v2.0_ocio-v2.5"
CONFIG_URI = f"ocio://{CONFIG_NAME}"
SOURCE_COLOR_SPACE = "Linear Rec.709 (sRGB)"
DISPLAY = "sRGB - Display"
VIEW = "ACES 2.0 - SDR 100 nits (Rec.709)"
EXPECTED_OCIO_VERSION = "2.5.1"
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description="Render a tagged linear-sRGB chart with direct OCIO ACES 2."
  )
  parser.add_argument("--input", type=Path, required=True)
  parser.add_argument("--output", type=Path, required=True)
  parser.add_argument("--report", type=Path, required=True)
  parser.add_argument("--source-id", required=True)
  parser.add_argument("--output-id", required=True)
  return parser.parse_args()


def sha256_file(path: Path) -> str:
  digest = hashlib.sha256()
  with path.open("rb") as handle:
    for block in iter(lambda: handle.read(1024 * 1024), b""):
      digest.update(block)
  return digest.hexdigest()


def valid_logical_id(value: str) -> bool:
  if not value or "\\" in value or ":" in value:
    return False
  path = PurePosixPath(value)
  return (
      not path.is_absolute() and str(path) == value and
      all(part not in ("", ".", "..") for part in path.parts)
  )


def png_chunks(path: Path) -> list[bytes]:
  payload = path.read_bytes()
  if payload[:8] != b"\x89PNG\r\n\x1a\n":
    raise ValueError("reference output is not a PNG")
  chunks: list[bytes] = []
  offset = 8
  while offset + 12 <= len(payload):
    size = struct.unpack(">I", payload[offset:offset + 4])[0]
    end = offset + 12 + size
    if end > len(payload):
      raise ValueError("reference PNG chunk exceeds the payload")
    kind = payload[offset + 4:offset + 8]
    chunks.append(kind)
    offset = end
    if kind == b"IEND":
      break
  if offset != len(payload) or not chunks or chunks[-1] != b"IEND":
    raise ValueError("reference PNG chunk structure is incomplete")
  return chunks


def require_source_contract(source: Any) -> None:
  spec = source.spec()
  if (
      source.has_error or spec.width != 606 or spec.height != 404 or
      spec.nchannels != 4 or spec.get_string_attribute("oiio:ColorSpace") !=
      "srgb_rec709_scene" or
      spec.get_string_attribute("ICCProfile:profile_description") != "Linear sRGB"
  ):
    raise ValueError("source must be a tagged 606x404 RGBA linear-sRGB chart")


def apply_ocio_display_float(source: Any) -> Any:
  """Apply the pinned display/view without inheriting integer source storage."""
  # ImageBufAlgo operations otherwise inherit the integer storage of the
  # source buffer. These registered charts are 8-bit source observations, so
  # applying OCIO directly to `source` would quantize the independent truth to
  # 8-bit before its nominal 16-bit publication. Promote first and require the
  # transform result to stay float until the final PNG write.
  float_source = oiio.ImageBufAlgo.copy(source, oiio.FLOAT)
  if (
      float_source.has_error or
      str(float_source.spec().format) != "float" or
      float_source.spec().width != source.spec().width or
      float_source.spec().height != source.spec().height or
      float_source.spec().nchannels != source.spec().nchannels
  ):
    raise ValueError("unable to promote independent source to RGBA float")
  rendered = oiio.ImageBufAlgo.ociodisplay(
      float_source,
      DISPLAY,
      VIEW,
      SOURCE_COLOR_SPACE,
      colorconfig=CONFIG_URI,
  )
  if (
      rendered.has_error or
      rendered.spec().width != source.spec().width or
      rendered.spec().height != source.spec().height or
      rendered.spec().nchannels != source.spec().nchannels or
      str(rendered.spec().format) != "float"
  ):
    raise ValueError(f"direct OCIO reference render failed: {rendered.geterror()}")
  return rendered


def render_reference(
    input_path: Path,
    output_path: Path,
    report_path: Path,
    source_id: str,
    output_id: str,
) -> dict[str, Any]:
  if (
      not input_path.is_file() or input_path == output_path or
      input_path == report_path or output_path == report_path or
      not valid_logical_id(source_id) or not valid_logical_id(output_id) or
      source_id == output_id
  ):
    raise ValueError("invalid path or logical-identity contract")
  if output_path.exists() or report_path.exists():
    raise ValueError("output already exists")
  if ocio.__version__ != EXPECTED_OCIO_VERSION:
    raise ValueError(
        f"strict reference renderer requires PyOpenColorIO {EXPECTED_OCIO_VERSION}; "
        f"found {ocio.__version__}"
    )
  configuration = ocio.Config.CreateFromBuiltinConfig(CONFIG_NAME)
  processor = configuration.getProcessor(
      SOURCE_COLOR_SPACE, DISPLAY, VIEW, ocio.TRANSFORM_DIR_FORWARD
  )
  source = oiio.ImageBuf(str(input_path))
  require_source_contract(source)
  rendered = apply_ocio_display_float(source)

  # The transform result is display-encoded sRGB. Remove the inherited linear
  # source ICC payload so the PNG writer emits standard sRGB transfer metadata.
  rendered.specmod().erase_attribute("ICCProfile")
  rendered.specmod().attribute("oiio:ColorSpace", "sRGB")
  output_path.parent.mkdir(parents=True, exist_ok=True)
  report_path.parent.mkdir(parents=True, exist_ok=True)
  partial_output = output_path.with_name(output_path.name + ".partial.png")
  partial_output.unlink(missing_ok=True)
  try:
    if not rendered.write(str(partial_output), oiio.UINT16):
      raise ValueError(f"unable to write direct OCIO reference: {rendered.geterror()}")
    os.replace(partial_output, output_path)
    readback = oiio.ImageBuf(str(output_path))
    spec = readback.spec()
    chunks = png_chunks(output_path)
    if (
        readback.has_error or spec.width != 606 or spec.height != 404 or
        spec.nchannels != 4 or str(spec.format) != "uint16" or
        b"sRGB" not in chunks or b"gAMA" not in chunks or b"cHRM" not in chunks or
        b"iCCP" in chunks
    ):
      raise ValueError("direct OCIO reference readback contract failed")
    pixel_sha1 = oiio.ImageBufAlgo.computePixelHashSHA1(readback)
    if not isinstance(pixel_sha1, str) or len(pixel_sha1) != 40:
      raise ValueError("unable to hash decoded direct-reference pixels")
    report = {
        "schema": SCHEMA,
        "tool_version": TOOL_VERSION,
        "renderer": "OpenImageIO ImageBufAlgo.ociodisplay",
        "independent_of_photo_editor": True,
        "source_id": source_id,
        "output_id": output_id,
        "source_file_sha256": sha256_file(input_path),
        "output_file_sha256": sha256_file(output_path),
        "output_pixel_sha1": pixel_sha1,
        "output_color_space": "sRGB",
        "processing_storage": "rgba32-float",
        "output_storage": "rgba16-png",
        "width": spec.width,
        "height": spec.height,
        "ocio_version": ocio.__version__,
        "openimageio_version": oiio.VERSION_STRING,
        "built_in_config": CONFIG_NAME,
        "config_cache_id": configuration.getCacheID(),
        "processor_cache_id": processor.getCacheID(),
        "source_color_space": SOURCE_COLOR_SPACE,
        "display": DISPLAY,
        "view": VIEW,
        "generator_sha256": sha256_file(Path(__file__).resolve()),
        "png_color_metadata": [chunk.decode("ascii") for chunk in chunks if chunk in {
            b"sRGB", b"gAMA", b"cHRM", b"iCCP",
        }],
    }
    temporary_report = report_path.with_name(report_path.name + ".partial")
    temporary_report.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    os.replace(temporary_report, report_path)
    return report
  except Exception:
    partial_output.unlink(missing_ok=True)
    output_path.unlink(missing_ok=True)
    report_path.unlink(missing_ok=True)
    raise


def main() -> int:
  args = parse_args()
  try:
    report = render_reference(
        args.input.resolve(), args.output.resolve(), args.report.resolve(),
        args.source_id, args.output_id,
    )
  except Exception as exc:  # noqa: BLE001 - preserve exact reference failure.
    print(f"unable to render independent OCIO reference: {exc}", file=sys.stderr)
    return 1
  print(json.dumps(report, sort_keys=True, separators=(",", ":")))
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
