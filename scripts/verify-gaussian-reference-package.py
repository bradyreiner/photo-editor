#!/usr/bin/env python3
"""Render every named reference view from a standard 3DGS camera package.

Converts the documented camera-to-world JSON pose to COLMAP calibration solely
for the native test driver. Images are never transformed, aligned, or resampled.
Requires built gaussian_scene_render_test and gaussian_image_quality_test tools.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError("Camera values must be finite numbers")
    return float(value)


def pose(camera):
    rotation = camera["rotation"]  # Camera-to-world, rows of the 3x3 matrix.
    position = camera["position"]
    if len(rotation) != 3 or any(len(row) != 3 for row in rotation) or len(position) != 3:
        raise ValueError("Expected a 3x3 rotation and a 3-vector position")
    rotation = [[number(value) for value in row] for row in rotation]
    position = [number(value) for value in position]
    for a in range(3):
        for b in range(3):
            if abs(sum(rotation[i][a] * rotation[i][b] for i in range(3)) - (a == b)) > 1e-5:
                raise ValueError("Camera rotation is not orthonormal")
    determinant = sum(rotation[0][i] * (
        rotation[1][(i + 1) % 3] * rotation[2][(i + 2) % 3]
        - rotation[1][(i + 2) % 3] * rotation[2][(i + 1) % 3]) for i in range(3))
    if abs(determinant - 1) > 1e-5:
        raise ValueError("Camera rotation must be right handed")
    world_to_camera = list(map(list, zip(*rotation)))
    translation = [-sum(row[i] * position[i] for i in range(3)) for row in world_to_camera]
    return world_to_camera, translation


def quaternion(rotation):
    trace = sum(rotation[i][i] for i in range(3))
    if trace > 0:
        scale = math.sqrt(trace + 1) * 2
        q = [scale / 4, (rotation[2][1] - rotation[1][2]) / scale,
             (rotation[0][2] - rotation[2][0]) / scale,
             (rotation[1][0] - rotation[0][1]) / scale]
    else:
        i = max(range(3), key=lambda axis: rotation[axis][axis])
        j, k = (i + 1) % 3, (i + 2) % 3
        scale = math.sqrt(1 + rotation[i][i] - rotation[j][j] - rotation[k][k]) * 2
        q = [(rotation[k][j] - rotation[j][k]) / scale, 0, 0, 0]
        q[i + 1] = scale / 4
        q[j + 1] = (rotation[i][j] + rotation[j][i]) / scale
        q[k + 1] = (rotation[i][k] + rotation[k][i]) / scale
    length = math.sqrt(sum(value * value for value in q))
    return [value / length for value in q]


def dimensions(path):
    with path.open("rb") as source:
        header = source.read(24)
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise ValueError(f"Invalid PNG header: {path.name}")
    width, height = struct.unpack(">II", header[16:24])
    if not (0 < width <= 8192 and 0 < height <= 8192):
        raise ValueError("Reference dimensions exceed the native quality-tool admission limit")
    return width, height


def run(arguments):
    process = subprocess.run([str(value) for value in arguments], capture_output=True, text=True)
    if process.returncode:
        raise RuntimeError(f"{Path(arguments[0]).name} failed: {process.stderr[-2000:]}")
    return json.loads(process.stdout)


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--cameras", required=True, type=Path)
    parser.add_argument("--references", required=True, type=Path)
    parser.add_argument("--comparison-renders", "--reference-renders", dest="reference_renders", type=Path,
                        help="Optional image comparator; model identity is not inferred from filenames")
    parser.add_argument("--comparison-note", default="Comparison-image model identity is unverified; this is not a same-weight renderer parity gate.")
    parser.add_argument("--make-project", action="store_true", help="Copy the verified model and calibration into an openable private Scene project")
    parser.add_argument("--alpha-model", choices=("brush", "original-3dgs"), default="brush",
                        help="Splat opacity cap convention; other rasterizer differences remain separately qualified")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--renderer", type=Path, default=root / "build/dev-macos-debug/tests/gaussian_scene_render_test")
    parser.add_argument("--quality-tool", type=Path, default=root / "build/dev-macos-debug/tests/gaussian_image_quality_test")
    args = parser.parse_args()
    alpha_model = "inria-alpha-099-v1" if args.alpha_model == "original-3dgs" else "brush-alpha-0999-v1"
    for name in ("model", "cameras", "references", "renderer", "quality_tool"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    if args.reference_renders:
        args.reference_renders = args.reference_renders.resolve(strict=True)
    if not args.output.is_absolute() or args.output.exists():
        raise ValueError("Output must be a new absolute directory")
    references = sorted(args.references.glob("*.png"))
    if not 1 <= len(references) <= 256:
        raise ValueError("Expected 1 to 256 named reference PNGs")
    if args.reference_renders and {p.name for p in args.reference_renders.glob("*.png")} != {p.name for p in references}:
        raise ValueError("Comparison renders must have the exact same named view set")
    if args.cameras.stat().st_size > 16 * 1024 * 1024:
        raise ValueError("Camera metadata exceeds 16 MiB")
    cameras = json.loads(args.cameras.read_text())
    if not isinstance(cameras, list) or len(cameras) > 100000:
        raise ValueError("Expected a camera list")
    by_name = {}
    for camera in cameras:
        name = str(camera["img_name"])
        if name in by_name:
            raise ValueError("Duplicate camera image name")
        by_name[name] = camera
    cameras_bin = bytearray(struct.pack("<Q", len(references)))
    images_bin = bytearray(struct.pack("<Q", len(references)))
    mapping = []
    for index, reference in enumerate(references, start=1):
        camera = by_name.get(reference.name, by_name.get(reference.stem))
        if camera is None:
            raise ValueError(f"No exact named camera for {reference.name}")
        width, height = dimensions(reference)
        original_width, original_height = number(camera["width"]), number(camera["height"])
        if original_width <= 0 or original_height <= 0 or not original_width.is_integer() or not original_height.is_integer():
            raise ValueError("Camera dimensions must be positive integers")
        sx, sy = width / original_width, height / original_height
        if abs(sx / sy - 1) > 0.002:
            raise ValueError("Reference aspect ratio differs from the named camera")
        fx, fy = number(camera["fx"]) * sx, number(camera["fy"]) * sy
        if not 0 < fx <= 1e8 or not 0 < fy <= 1e8:
            raise ValueError("Invalid focal length")
        rotation, translation = pose(camera)
        q = quaternion(rotation)
        cameras_bin += struct.pack("<IiQQ4d", index, 1, width, height, fx, fy, width / 2, height / 2)
        images_bin += struct.pack("<I7dI", index, *q, *translation, index)
        images_bin += reference.name.encode("utf8") + b"\0" + struct.pack("<Q", 0)
        if args.reference_renders and dimensions(args.reference_renders / reference.name) != (width, height):
            raise ValueError("Reference renderer has a different image size")
        mapping.append({"name": reference.name, "sourceCameraId": camera["id"], "width": width,
                        "height": height, "intrinsicScale": [sx, sy], "fx": fx, "fy": fy})
    args.output.mkdir()
    calibration = args.output / "calibration"
    native = args.output / "native"
    calibration.mkdir()
    native.mkdir()
    (calibration / "cameras.bin").write_bytes(cameras_bin)
    (calibration / "images.bin").write_bytes(images_bin)
    manifest = {"scope": "All named reference views; standard 3DGS camera-to-world JSON to COLMAP pinhole conversion",
                "cameraMapping": mapping, "imageProcessing": "none; only intrinsics scaled to the existing reference pixel grid",
                "driverSha256": digest(Path(__file__).resolve()), "alphaModel": alpha_model,
                "comparisonIdentityVerified": False, "comparisonNote": args.comparison_note if args.reference_renders else None,
                "sha256": {str(path): digest(path) for path in (args.model, args.cameras, args.renderer, args.quality_tool)}}
    (args.output / "inputs.json").write_text(json.dumps(manifest, indent=2) + "\n")
    metrics = []
    for reference in references:
        compared = (args.reference_renders / reference.name) if args.reference_renders else reference
        command = [args.renderer, args.model, calibration, native / reference.name, reference.name, compared, "--no-motion"]
        if args.alpha_model == "original-3dgs":
            command.append("--inria-alpha")
        result = run(command)
        if result.get("alphaModel") != alpha_model:
            raise RuntimeError("Renderer did not use the requested opacity convention")
        if result.get("artifactSha256") != manifest["sha256"][str(args.model)]:
            raise RuntimeError("Model changed during the render batch")
        (args.output / (reference.stem + ".json")).write_text(json.dumps(result, indent=2) + "\n")
        metrics.append(result)
        print(f"Rendered {reference.name}", flush=True)
    quality = run([args.quality_tool, "--evaluate", args.references, native, args.output / "native-vs-capture"])
    reference_quality = None
    if args.reference_renders:
        reference_quality = run([args.quality_tool, "--evaluate", args.references, args.reference_renders,
                                 args.output / "reference-renderer-vs-capture"])
    summary = {"ok": True, "qualityCertified": False, "frameCount": len(metrics), "alphaModel": alpha_model,
               "scope": "Neutral imported-model rendering and named-view reconstruction diagnostics; capture/training/general-view quality requires separate evidence",
               "nativeMeanPsnrDb": quality["meanFramePsnrDb"], "nativeMeanSsim": quality["meanFrameSsim"],
               "nativeWorstPsnrDb": min(frame["psnrDb"] for frame in quality["frames"]),
               "nativeWorstSsim": min(frame["ssim"] for frame in quality["frames"])}
    if reference_quality:
        summary.update({"comparisonMeanPsnrDb": reference_quality["meanFramePsnrDb"],
                        "comparisonMeanSsim": reference_quality["meanFrameSsim"],
                        "comparisonImagesMinimumPsnrDb": min(frame["referencePsnr"] for frame in metrics),
                        "comparisonImagesMaximumCodeError": max(frame["referenceMaxChannelError"] for frame in metrics),
                        "comparisonIdentityVerified": False, "comparisonNote": args.comparison_note})
    if digest(args.model) != manifest["sha256"][str(args.model)] or digest(args.cameras) != manifest["sha256"][str(args.cameras)]:
        raise RuntimeError("Input model or camera metadata changed during evaluation")
    if args.make_project:
        project = args.output / "project"
        (project / "trained").mkdir(parents=True)
        (project / "dataset/sparse").mkdir(parents=True)
        shutil.copy2(args.model, project / "trained/scene.ply")
        for name in ("cameras.bin", "images.bin"):
            shutil.copy2(calibration / name, project / "dataset/sparse" / name)
        scene = {"schema": "framehouse-gaussian-scene-v1", "mode": "static-3d", "state": "trained", "busy": False,
                 "projectPath": str(project), "artifactValidation": {"artifactSha256": manifest["sha256"][str(args.model)]},
                 "sourceCount": 0, "registeredViews": len(mapping), "originalsModified": False,
                 "origin": {"kind": "calibrated-reference-package", "trainingPerformedByApp": False,
                            "cameraDomain": manifest["imageProcessing"], "inputReceipt": str(args.output / "inputs.json")}}
        (project / "scene.json").write_text(json.dumps(scene, indent=2) + "\n")
        if args.alpha_model == "original-3dgs":
            composition = {"schema": "looksy-scene-composition-v8", "objects": [], "lights": [], "ambient": 0.15,
                           "reflections": False, "lightSamples": 64, "alphaModel": alpha_model,
                           "captureLighting": {"mode": "captured", "power": 1, "shadowStrength": 1},
                           "haze": {"density": 0, "distance": 1000, "anisotropy": 0, "color": [0.7, 0.8, 1]},
                           "environment": {"diffuse": False, "strength": 1}}
            (project / "scene-objects.json").write_text(json.dumps({"schema": "looksy-scene-objects-v1",
                "artifactSha256": manifest["sha256"][str(args.model)], "composition": composition}, indent=2) + "\n")
        summary["project"] = str(project)
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary), flush=True)


if __name__ == "__main__":
    main()
