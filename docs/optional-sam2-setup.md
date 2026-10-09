# Optional SAM 2 timeline masks

Timeline SAM 2 uses Meta's video predictor as an optional local runtime. It segments a selected subject through the source shot and stores one mask per analyzed frame. SAM 2 is not installed or downloaded by the app at runtime, and this temporal runtime is separate from the bundled photo-selection models.

## Local runtime setup

The app first recognizes this checkout's optional runtime at `.photo-editor/tracking/sam2-runtime/venv/bin/python` with the checkpoint `.photo-editor/tracking/sam2-runtime/sam2.1_hiera_tiny.pt`. `scripts/run-native.sh` detects that project-local pair and sets the Python and checkpoint paths for launch. The desktop runtime also checks the same repository-relative locations. An arbitrary clone elsewhere is not guessed automatically; configure its Python, checkpoint, and (if needed) config explicitly.

For a local SAM 2 checkout, create the isolated environment in the project runtime directory and install the checkout there. Use Python 3.10 or newer and the PyTorch/TorchVision versions required by the upstream install guide:

```sh
mkdir -p .photo-editor/tracking/sam2-runtime
python3 -m venv .photo-editor/tracking/sam2-runtime/venv
source .photo-editor/tracking/sam2-runtime/venv/bin/activate
python -m pip install --upgrade pip
git clone https://github.com/facebookresearch/sam2.git /path/to/sam2
python -m pip install torch torchvision
python -m pip install -e /path/to/sam2
```

Install PyTorch/TorchVision using the [official selector](https://pytorch.org/get-started/locally/) for the machine. Download an official SAM 2.1 checkpoint from the upstream [checkpoint list](https://github.com/facebookresearch/sam2#download-checkpoints) and copy the tiny checkpoint to `.photo-editor/tracking/sam2-runtime/sam2.1_hiera_tiny.pt`. Its matching config is `configs/sam2.1/sam2.1_hiera_t.yaml`.

The default config is `configs/sam2.1/sam2.1_hiera_t.yaml` and the default device is CPU. To use a checkout or checkpoint stored elsewhere, set these variables before launching Looksy:

```sh
export PHOTO_EDITOR_SAM2_PYTHON="/path/to/python-with-sam2/bin/python"
export PHOTO_EDITOR_SAM2_CHECKPOINT="/path/to/sam2.1_hiera_tiny.pt"
# Optional overrides; defaults shown.
export PHOTO_EDITOR_SAM2_CONFIG="configs/sam2.1/sam2.1_hiera_t.yaml"
export PHOTO_EDITOR_SAM2_DEVICE="cpu"
```

`mps` is also supported when `torch.backends.mps.is_available()` returns true. The app checks local files and configuration without importing packages or installing/downloading anything. Missing Python, SAM 2 package, config, checkpoint, or device support is reported as unavailable analysis.

See the upstream [SAM 2 installation guide](https://github.com/facebookresearch/sam2/blob/main/INSTALL.md) for current requirements and platform-specific details.

## Rotoscope and correction workflow

Select an online video source, open **Motion / Track → Track / Cutout**, choose **SAM2 Rotoscope**, and mark the subject region on the first analyzed frame. The video predictor propagates that initial box through the staged source frames and must return a complete mask sequence before the result is committed.

To refine a result, pause within the source shot and choose **Refine Matte**. The correction overlay shows the source frame at the paused playhead. Add positive points on the subject and negative points on background or unwanted areas; use the mode toggle to switch label and remove the last point if needed. Apply the correction to rerun the full source shot with the initial box and all saved point corrections. Corrections can target up to 64 distinct source frames, with 1–256 points per correction frame. Each point is normalized to the source frame; label `1` is positive and `0` is negative.

The full-shot rerun is bounded to 180 source frames and a 640-pixel maximum analysis edge to limit temporal-model memory. Longer clips must be split into separately analyzed segments. The 180-frame SAM 2 limit is separate from the 900-frame limit for native motion tracking and scene/surface analysis. A partial mask sequence is not accepted as complete. Inspect the subject edges and temporal stability before delivery; these thresholded segmentation masks are not qualified fine-hair or transparent-material mattes.

## Foreground cutout and attachment occlusion

SAM 2 can generate either a subject cutout or a foreground mask for an attached title/image's occlusion. For attachment occlusion, choose **Generate occlusion mask** on the source clip, enable **SAM2 temporal**, and provide a seed region; then enable **Use foreground matte for occlusion** on the scene or surface attachment. The original video remains complete; the matte only composites foreground pixels above the attached layer. This is separate from enabling the video cutout itself.

Native Apple Vision foreground masks are another option on supported macOS versions; those per-frame masks do not use the SAM 2 temporal predictor. For source-space segmentation, disable stabilization and rolling-shutter correction and use supported Fit framing. See [Scene Camera](timeline-scene-tracking.md) and [Surface Tracking](timeline-surface-tracking.md) for their attachment requirements.
