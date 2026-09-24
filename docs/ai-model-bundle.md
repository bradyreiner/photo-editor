# Local Image Model Bundle

Framehouse does not treat a file named `lama.onnx` as a production model by
itself. A distributable local image-model bundle must pin the exact bytes,
source, license, and tensor contract used by the desktop runtime.

The bundle layout is:

```text
model-bundle/
├── manifest.json
└── inpaint/
    └── lama.onnx
```

`manifest.json` uses this contract. The checked-in production lock currently
pins OpenCV Zoo's Apache-2.0 LaMa export at immutable model revision
`aee6d22f0a13e5e35af1c9a1c3afd62841fc6f3f`:

```json
{
  "schema": "framehouse-ai-model-bundle-v1",
  "version": 1,
  "artifacts": [
    {
      "id": "opencv-inpainting-lama-2025jan",
      "role": "inpaint",
      "contract": "opencv-lama-image-mask-nchw-float32-512-v1",
      "path": "inpaint/lama.onnx",
      "bytes": 92591623,
      "sha256": "7df918ac3921d3daf0aae1d219776cf0dc4e4935f035af81841b40adcf74fdf2",
      "sourceUrl": "https://huggingface.co/opencv/inpainting_lama/blob/aee6d22f0a13e5e35af1c9a1c3afd62841fc6f3f/inpainting_lama_2025jan.onnx",
      "license": {
        "name": "Apache-2.0",
        "url": "https://huggingface.co/opencv/inpainting_lama/blob/aee6d22f0a13e5e35af1c9a1c3afd62841fc6f3f/LICENSE"
      }
    }
  ]
}
```

Install the exact model and Microsoft ONNX Runtime 1.29.0 macOS arm64 SDK into
the ignored local runtime directory, or verify an existing installation:

```sh
python3 -B scripts/install-ai-runtime.py --download
python3 -B scripts/install-ai-runtime.py --verify-only
```

The installer downloads only URLs in
`apps/desktop/models/production-runtime.lock.json`, verifies byte counts and
SHA-256 digests before installation, safely extracts the SDK, records hashes
for its required installed files, and re-verifies both the SDK and model bundle.
It prints the exact CMake configuration command for the installed paths.

To verify any candidate bundle independently:

```sh
python3 -B scripts/verify-ai-model-bundle.py \
  /path/to/model-bundle \
  --require-inpaint \
  --json
```

Configure the production runtime only after the bundle and ONNX Runtime SDK
have been reviewed:

```sh
cmake --preset dev-macos-debug \
  -DPHOTO_EDITOR_ENABLE_AI_INFERENCE=ON \
  -DPHOTO_EDITOR_ONNXRUNTIME_ROOT=/path/to/onnxruntime-sdk \
  -DPHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML=OFF \
  -DPHOTO_EDITOR_BUNDLED_AI_MODELS_DIR=/path/to/model-bundle
```

CMake runs the same verifier and stops configuration if an artifact is missing,
has changed bytes, escapes the bundle directory, lacks source/license evidence,
or does not match the inpaint tensor contract. Packaging copies the verified
directory into the app bundle's `Resources/models` directory.

The desktop runtime independently revalidates the installed manifest, every
declared artifact, byte count, SHA-256 checksum, bundle-contained canonical
path, source/license evidence, and the LaMa tensor contract before it reports
professional retouch as available or opens an inference session. This applies
equally to packaged models and `PHOTO_EDITOR_AI_MODEL_DIR` overrides. A missing,
damaged, substituted, or incomplete bundle remains on the proof-gated Local CPU
Fill tier and exposes a concrete bundle verification error in runtime readiness.

The runtime contract is intentionally model-specific: `image` is RGB float32
NCHW in `[0, 1]`, `mask` is binary float32 NCHW where one means replace, both
inputs are fixed at `512 x 512`, and the output is RGB float32 NCHW in
`[0, 255]`. A generic file renamed to `lama.onnx` is rejected even if it is a
valid ONNX graph.

The production baseline uses ONNX Runtime CPU execution. CoreML can be enabled
as an optional accelerator; it uses the current provider-options API, a
checksum-scoped compiled-model cache, and fails over to the same verified ONNX
graph on CPU rather than falling back to Local CPU Fill. CoreML is not the
shipping default until its compile and warm-latency gates pass on the supported
macOS hardware/OS matrix.
