#!/usr/bin/env python3
"""Compare named float properties in two bounded, uncompressed Gaussian PLYs.

This checks reference-replay parameter drift; it is not an image-quality gate.
No inputs are modified. Only the Python standard library is used.
"""
import argparse
import array
import hashlib
import json
import math
import sys


def read(path):
    with open(path, "rb") as stream:
        header = bytearray()
        while len(header) < 16384:
            line = stream.readline(16384 - len(header))
            if not line:
                raise ValueError("Incomplete PLY header")
            header.extend(line)
            if line.strip() == b"end_header":
                break
        lines = header.decode("ascii").splitlines()
        if lines[:2] != ["ply", "format binary_little_endian 1.0"] or lines[-1] != "end_header":
            raise ValueError("Unsupported PLY format")
        count = 0
        properties = []
        for line in lines:
            fields = line.split()
            if fields[:2] == ["element", "vertex"] and not count:
                count = int(fields[2])
            elif fields[:1] == ["element"]:
                raise ValueError("Unsupported or duplicate element")
            elif fields[:1] == ["property"]:
                if len(fields) != 3 or fields[1] not in ("float", "float32"):
                    raise ValueError("Only scalar float properties are supported")
                properties.append(fields[2])
        if not 0 < count <= 2_000_000 or not 0 < len(properties) <= 128 or len(set(properties)) != len(properties):
            raise ValueError("Invalid or oversized Gaussian layout")
        values = array.array("f")
        if values.itemsize != 4:
            raise ValueError("This platform does not use 32-bit array floats")
        values.fromfile(stream, count * len(properties))
        if stream.read(1):
            raise ValueError("Unexpected trailing data")
        if sys.byteorder != "little":
            values.byteswap()
    with open(path, "rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return count, properties, values, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first")
    parser.add_argument("second")
    args = parser.parse_args()
    count_a, names_a, a, hash_a = read(args.first)
    count_b, names_b, b, hash_b = read(args.second)
    if count_a != count_b or set(names_a) != set(names_b):
        raise ValueError("Gaussian count/property sets differ; correspondence is unproven")
    result = {}
    non_finite = 0
    for index, name in enumerate(names_a):
        other = names_b.index(name)
        changed, maximum, reference_maximum = 0, 0.0, 0.0
        for first, second in zip(a[index::len(names_a)], b[other::len(names_b)]):
            if not math.isfinite(first) or not math.isfinite(second):
                non_finite += 1
                continue
            changed += first != second
            maximum = max(maximum, abs(first - second))
            reference_maximum = max(reference_maximum, abs(first))
        result[name] = {"changedCount": changed, "maxAbsoluteDifference": maximum,
                        "referenceMaxAbsolute": reference_maximum}
    print(json.dumps({"vertexCount": count_a, "firstSha256": hash_a, "secondSha256": hash_b,
                      "nonFinitePairs": non_finite, "properties": result}, sort_keys=True))
    return 1 if non_finite else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, EOFError) as error:
        print(json.dumps({"error": str(error)}), file=sys.stderr)
        sys.exit(1)
