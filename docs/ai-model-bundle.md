# Local Image Model Bundle

Framehouse does not treat a file named `lama.onnx` as a production model by
itself. A distributable local image-model bundle must pin the exact bytes,
source, license, and tensor contract used by the desktop runtime.

The bundle layout is:

```text
model-bundle/
├── manifest.json
├── inpaint/
    └── lama.onnx
└── selection/                 # optional; must contain both SAM2 artifacts
    ├── sam2_hiera_tiny_image_encoder.onnx
    └── sam2_hiera_tiny_image_decoder.onnx
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

The verifier and installer also accept a paired SAM2 Hiera Tiny image encoder
and image decoder under `selection/`, using contracts
`sam2-hiera-tiny-image-encoder-fp32-1024-v1` and
`sam2-hiera-tiny-image-decoder-fp32-1024-v1`. A partial pair is rejected. The
production lock pins the Apache-2.0 ONNX conversion at immutable Hugging Face
revision `2b467a10bfd7d49ba5fe6a00fbee4351f0b1a739`. Its component names,
types, fixed encoder dimensions, dynamic point dimensions, and decoder mask
shape were inspected using ONNX Runtime 1.29.0. The native runtime normalizes
RGB inputs, letterboxes to 1024 on the longest side, caches image features by
source pixels and model hashes, sends foreground label `1` and background
label `0`, sends real box corners with labels `2` and `3`, filters decoder
candidates to those honoring explicit point prompts, selects
the highest-scoring candidate that remains, and maps its logits back to
source-resolution alpha. Runtime tests exercise real ONNX inference;
real-photograph mask quality and native app workflow qualification remain
separate gates.

Boxes remain boxes through the asynchronous preview request and stored prompt
metadata; they are not expanded into foreground corner clicks. The Selection
panel exposes Object and Subject directly, with Click / stroke and Enclose item
modes for Object. Enclose item requires verified SAM2 availability. Box validation
allows background corners and hollow object centers, and requires selected pixels
inside the requested bounds. The existing point-based fallback only receives the
box center; it does not implement SAM's trained box embeddings.

Selection preview, strength, and edge controls belong to the Selection panel.
Layers retain linked selection rows and an Edit Selection entry. Done exits the
tool without moving its settings back into Layers. Object previews are explicitly
accepted with Use selection, and a failed accept keeps the preview available.

### Named objects and generative fill

The bundled SAM2 model is spatially prompted; it has no text encoder. The app
cannot interpret arbitrary item descriptions such as "the red bicycle" with this
model. [Meta SAM3](https://github.com/facebookresearch/sam3) supports text concepts
and visual prompts, making it a candidate for a future Describe item mode. Its
official installation currently specifies Python 3.12+, PyTorch 2.7+, and a CUDA
12.6+ setup. It is not a replacement for the pinned macOS ONNX tensor contract.
A local integration needs a verified compatible export/runtime, reviewed model
artifacts and license, instance result selection, and measured native latency and
photographic quality before that mode is exposed as available.

LaMa reconstructs masked image regions from image and mask inputs; it does not
consume a text prompt. Looksy's current `inpaintPrompt` only steers existing texture
heuristics (smooth/soft/clean versus texture/grain/detail/fabric/weave/bark). The
UI calls this a texture hint and offers automatic, smooth, and detailed texture
choices. It does not advertise text-driven generative fill.

A real generative fill backend needs a text-conditioned inpainting pipeline,
including its text encoder, denoiser, VAE and scheduler, separately verified
artifacts, and cancellable worker execution. The existing recipe patch system can
retain the output, seed, source selection and history. The acceptance flow should
be Select area → Describe replacement → Generate → review variants → Apply,
with exact unmasked-pixel preservation, undo/reopen, and failure/cancel tests.
Context-padded region inference should preserve detail instead of resizing the
entire photograph. [Diffusers' inpainting documentation](https://huggingface.co/docs/diffusers/en/using-diffusers/inpaint)
describes prompt conditioning, seed control, and `padding_mask_crop`; those
capabilities are a separate model integration, not features of the installed
LaMa graph. No new generative model is installed by this UI change.

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
does not match the inpaint tensor contract, or forms an incomplete SAM2 pair.
Packaging copies the verified directory into the app bundle's `Resources/models`
directory.

The desktop runtime independently revalidates the installed manifest, every
declared artifact, byte count, SHA-256 checksum, bundle-contained canonical
path, source/license evidence, and each declared model contract before it
reports a learned capability as available or opens an inference session. This
applies equally to packaged models and `PHOTO_EDITOR_AI_MODEL_DIR` overrides. A missing,
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
