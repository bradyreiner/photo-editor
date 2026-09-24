"""Independent SafeTensors 0.6.2 roundtrip of native fixture artifacts.

Run the native gaussian_spacetime_artifact_test OUTPUT first, then this script
with NATIVE_TEST OUTPUT NEW_INTEROP_OUTPUT. No Torch, NumPy or model weights.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

import safetensors


def main():
    executable, source, destination = map(Path, sys.argv[1:])
    destination.mkdir(exist_ok=False)
    results = []
    for path in sorted(source.glob("scene-*.safetensors")):
        content = path.read_bytes()
        header_size = struct.unpack("<Q", content[:8])[0]
        header = json.loads(content[8:8 + header_size])
        tensors = dict(safetensors.deserialize(content))
        assert set(tensors) == {"gaussians"}
        tensor = tensors["gaussians"]
        assert tensor["dtype"] == "F32"
        assert tensor["shape"] == header["gaussians"]["shape"]
        assert bytes(tensor["data"]) == content[8 + header_size:]
        rewritten = safetensors.serialize(
            {"gaussians": {"dtype": "float32", "shape": tensor["shape"],
                           "data": bytes(tensor["data"]) }},
            metadata=header["__metadata__"],
        )
        output = destination / path.name
        with output.open("xb") as stream:
            stream.write(rewritten)
        verified = subprocess.run(
            [str(executable.resolve()), "--verify", str(output.resolve()),
             hashlib.sha256(rewritten).hexdigest()],
            check=True, capture_output=True, text=True,
        )
        native = json.loads(verified.stdout)
        assert native["ok"]
        assert native["artifactSha256"] == hashlib.sha256(content).hexdigest()
        results.append(path.name)
    assert len(results) == 12
    receipt = {"ok": True, "safetensorsVersion": safetensors.__version__,
               "nativeToOfficialToNativeExactRoundTrips": len(results),
               "scope": "synthetic-container-interoperability-not-checkpoint-conversion"}
    with (destination / "receipt.json").open("x") as stream:
        json.dump(receipt, stream, indent=2)
    print(json.dumps(receipt))


if __name__ == "__main__":
    main()
