#!/usr/bin/env python3
"""Build the macOS ICNS container from the committed Virel PNG icon set.

The script intentionally performs no resampling. Source PNG sizes are generated
and visually reviewed separately, then this packs those exact bytes into the
standard modern ICNS PNG chunks used by macOS.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


CHUNKS = (
    ("icp4", "icon_16x16.png"),
    ("icp5", "icon_32x32.png"),
    ("icp6", "icon_32x32@2x.png"),
    ("ic07", "icon_128x128.png"),
    ("ic08", "icon_256x256.png"),
    ("ic09", "icon_512x512.png"),
    ("ic10", "icon_512x512@2x.png"),
)


def chunk(kind: str, payload: bytes) -> bytes:
    encoded_kind = kind.encode("ascii")
    return encoded_kind + struct.pack(">I", len(payload) + 8) + payload


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iconset", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    payload = b"".join(
        chunk(kind, (args.iconset / filename).read_bytes())
        for kind, filename in CHUNKS
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(b"icns" + struct.pack(">I", len(payload) + 8) + payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
