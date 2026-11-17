#!/usr/bin/env python3
"""Local monocular-depth -> colored point cloud -> small-camera-motion film.

JSON lines on stdout are the desktop protocol; diagnostics belong on stderr.
No content is uploaded. Model downloads occur only through --setup.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile

MODEL = "depth-anything/Depth-Anything-V2-Small-hf"
CHILDREN = []


def emit(**value):
    print(json.dumps(value), flush=True)


def stop(_signum=None, _frame=None):
    for process in CHILDREN:
        if process.poll() is None:
            process.terminate()
    raise SystemExit(130)


def setup(home):
    if sys.version_info < (3, 10):
        raise RuntimeError("Depth Motion requires Python 3.10 or later. Install a current Python and retry.")
    home = Path(home)
    python = home / "venv" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    home.mkdir(parents=True, exist_ok=True)
    commands = []
    if not python.exists():
        commands.append([sys.executable, "-m", "venv", str(home / "venv")])
    commands.extend([
        [str(python), "-m", "pip", "install", "torch>=2.4,<3", "transformers>=4.45,<5",
         "numpy>=1.26,<3", "Pillow>=10,<13", "opencv-python-headless>=4.10,<5"],
        [str(python), str(Path(__file__).resolve()), "--download", str(home / "model")],
    ])
    for index, command in enumerate(commands):
        emit(status="Installing local depth tools and model…", progress=index / len(commands))
        process = subprocess.Popen(command, stdout=sys.stderr, stderr=sys.stderr)
        CHILDREN.append(process)
        if process.wait() != 0:
            raise RuntimeError("Depth setup failed. Check your connection and available disk space, then retry.")
    emit(done=True, status="Local depth model ready", progress=1)


def load_libraries():
    global np, cv2, Image, ImageOps
    import numpy as np
    import cv2
    from PIL import Image, ImageOps


def file_hash(path):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_depth(raw, limits=None):
    if raw.ndim != 2 or not np.isfinite(raw).all():
        raise ValueError("Depth must be a finite, single-channel image.")
    low, high = np.percentile(raw, [2, 98]) if limits is None else limits
    if high - low < 1e-6:
        return np.full(raw.shape, 0.5, np.float32)
    return np.clip((raw - low) / (high - low), 0, 1).astype(np.float32)


def stabilize_depth(depth, rgb, previous):
    """Warp previous depth with backward flow; reject occlusions and scene cuts."""
    gray = cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY)
    if previous is None:
        return depth, (gray, depth)
    old_gray, old_depth = previous
    if float(np.mean(np.abs(gray.astype(np.float32) - old_gray))) > 35:
        return depth, (gray, depth)
    flow = cv2.calcOpticalFlowFarneback(gray, old_gray, None, 0.5, 3, 15, 3, 5, 1.2, 0)
    yy, xx = np.indices(depth.shape, dtype=np.float32)
    map_x, map_y = xx + flow[..., 0], yy + flow[..., 1]
    warped = cv2.remap(old_depth, map_x, map_y, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    old_luma = cv2.remap(old_gray, map_x, map_y, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    valid = ((np.abs(gray.astype(np.float32) - old_luma) < 12)
             & (np.abs(depth - warped) < 0.12)
             & (map_x >= 0) & (map_x < depth.shape[1] - 1)
             & (map_y >= 0) & (map_y < depth.shape[0] - 1))
    result = np.where(valid, depth * 0.55 + warped * 0.45, depth).astype(np.float32)
    return result, (gray, result)


def point_cloud(depth, strength=0.65, convergence=0.5):
    """Pinhole back-projection. White depth is near; units are relative, not metres."""
    height, width = depth.shape
    focal = 0.5 * width / math.tan(math.radians(50) / 2)
    # Reciprocal mapping makes foreground parallax grow smoothly, without huge depths.
    z = (1.0 + strength * convergence) / (1.0 + strength * depth)
    yy, xx = np.indices(depth.shape, dtype=np.float32)
    return np.stack(((xx - (width - 1) / 2) * z / focal,
                     (yy - (height - 1) / 2) * z / focal, z), axis=-1), focal


def camera_coordinates(points, phase, amount, path):
    wave = math.sin(2 * math.pi * phase)
    if path == "push":
        center = np.array([0, 0, amount * (1 - math.cos(2 * math.pi * phase)) / 2], np.float32)
    else:
        center = np.array([amount * wave, amount * 0.25 * (1 - math.cos(2 * math.pi * phase))
                           if path == "arc" else 0, 0], np.float32)
    # Look at a convergence plane one relative unit away, keeping the subject framed.
    forward = np.array([0, 0, 1], np.float32) - center
    forward /= np.linalg.norm(forward)
    right = np.cross(np.array([0, 1, 0], np.float32), forward)
    right /= np.linalg.norm(right)
    down = np.cross(forward, right)
    return (points - center) @ np.stack((right, down, forward), axis=1)


def render(rgb, depth, phase, settings):
    height, width = depth.shape
    strength = settings["depth"]
    amount = settings["motion"]
    style = settings["style"]
    if style == "photo" and (amount == 0 or abs(phase - round(phase)) < 1e-10):
        return rgb.copy(), 0.0
    points, focal = point_cloud(depth, strength, settings["convergence"])
    if style == "points":
        appearance = settings.get("pointAppearance", {})
        colors = rgb.astype(np.float32)
        mode = appearance.get("colorMode", 0)
        if mode == 1:
            colors = np.repeat((colors @ np.array([0.2126, 0.7152, 0.0722], np.float32))[..., None], 3, axis=-1)
        elif mode == 2:
            # Match DepthPointCloudViewport's depth-to-gradient coordinates and
            # interpolate the chosen stops, rather than a preset-name lookup.
            position = np.clip((points[..., 2] - 0.4) / 1.2, 0, 1)
            stops = [np.array([int(appearance[key][i:i + 2], 16) for i in (1, 3, 5)], np.float32)
                     for key in ("gradientNear", "gradientMid", "gradientFar")]
            near = position < 0.5
            blend = np.where(near, position * 2, (position - 0.5) * 2)[..., None]
            low = np.where(near[..., None], stops[0], stops[1])
            high = np.where(near[..., None], stops[1], stops[2])
            colors = low * (1 - blend) + high * blend
        rgb = np.clip(np.floor(colors * appearance.get("brightness", 1) + 0.5), 0, 255).astype(np.uint8)
    camera = camera_coordinates(points, phase, amount, settings["path"])
    z = camera[..., 2].reshape(-1)
    u = (focal * camera[..., 0].reshape(-1) / z + (width - 1) / 2)
    v = (focal * camera[..., 1].reshape(-1) / z + (height - 1) / 2)
    colors = rgb.reshape(-1, 3)
    if style == "points":
        # Stable source-space sampling avoids random sparkle from frame to frame.
        keep = np.zeros((height, width), bool)
        keep[::3, ::3] = True
        keep = keep.reshape(-1)
        u, v, z, colors = u[keep], v[keep], z[keep], colors[keep]
    x, y = np.floor(u).astype(np.int32), np.floor(v).astype(np.int32)
    output = np.zeros((height * width, 3), np.uint8)
    if style == "points":
        background = settings.get("pointAppearance", {}).get("background", "#000000")
        output[:] = [int(background[i:i + 2], 16) for i in (1, 3, 5)]
    zbuffer = np.full(height * width, np.inf, np.float32)
    samples = []
    # Bilinear splats preserve subpixel motion; a z-buffer rejects occluded surfaces.
    for dy, dx in ((0, 0), (0, 1), (1, 0), (1, 1)):
        px, py = x + dx, y + dy
        weights = (1 - np.abs(u - px)) * (1 - np.abs(v - py))
        valid = (px >= 0) & (px < width) & (py >= 0) & (py < height) & (z > 0.05) & (weights > 1e-6)
        indices = py[valid] * width + px[valid]
        zs, cs = z[valid], colors[valid]
        np.minimum.at(zbuffer, indices, zs)
        samples.append((indices, zs, cs, weights[valid]))
    total = np.zeros(height * width, np.float64)
    weighted = np.zeros((height * width, 3), np.float64)
    for indices, zs, cs, weights in samples:
        # Smooth the same surface while excluding foreground/background depth jumps.
        visible = zs <= zbuffer[indices] * 1.01 + 1e-6
        indices, weights, cs = indices[visible], weights[visible], cs[visible]
        total += np.bincount(indices, weights=weights, minlength=height * width)
        for channel in range(3):
            weighted[:, channel] += np.bincount(indices, weights=weights * cs[:, channel], minlength=height * width)
    covered = total > 1e-6
    output[covered] = np.clip(np.rint(weighted[covered] / total[covered, None]), 0, 255).astype(np.uint8)
    output = output.reshape(height, width, 3)
    holes = (~np.isfinite(zbuffer)).reshape(height, width).astype(np.uint8)
    hole_fraction = float(holes.mean())
    if style == "photo" and holes.any():
        # This fills unseen pixels; it does not claim to recover hidden scene geometry.
        output = cv2.inpaint(output, holes * 255, 3, cv2.INPAINT_TELEA)
    return output, hole_fraction


def write_ply(path, rgb, depth, settings):
    points, _ = point_cloud(depth, settings["depth"], settings["convergence"])
    records = np.empty(depth.size, dtype=[("x", "<f4"), ("y", "<f4"), ("z", "<f4"),
                                        ("red", "u1"), ("green", "u1"), ("blue", "u1")])
    for index, name in enumerate(("x", "y", "z")):
        records[name] = points[..., index].reshape(-1)
    for index, name in enumerate(("red", "green", "blue")):
        records[name] = rgb[..., index].reshape(-1)
    with open(path, "wb") as output:
        output.write(("ply\nformat binary_little_endian 1.0\n"
                      "comment Looksy relative depth; x right, y down, z forward\n"
                      f"element vertex {depth.size}\nproperty float x\nproperty float y\n"
                      "property float z\nproperty uchar red\nproperty uchar green\n"
                      "property uchar blue\nend_header\n").encode())
        output.write(records.tobytes())


def write_cloud_sequence(project, cache, names, video, settings):
    """Portable per-frame XYZRGB for the live viewer; camera motion is separate."""
    project, cache = Path(project), Path(cache)
    with tempfile.TemporaryDirectory(prefix=".pointcloud-", dir=project) as staging:
        staging = Path(staging)
        counts = []
        for index, name in enumerate(names if video else names[:1]):
            with np.load(cache / name, allow_pickle=False) as frame:
                rgb, depth = frame["rgb"], frame["depth"].astype(np.float32)
            if rgb.shape != (*depth.shape, 3) or not np.isfinite(depth).all():
                raise ValueError("Invalid cached color/depth frame.")
            if settings["invert"]:
                depth = 1 - depth
            points, _ = point_cloud(depth, settings["depth"], settings["convergence"])
            # Stable pixel-grid sampling, with bounded native playback memory.
            step = max(2, math.ceil(math.sqrt(depth.size / 180000)))
            points, colors = points[::step, ::step], rgb[::step, ::step]
            records = np.empty(points.shape[0] * points.shape[1], dtype=[
                ("position", "<f4", (3,)), ("color", "u1", (3,))])
            records["position"] = points.reshape(-1, 3)
            records["color"] = colors.reshape(-1, 3)
            (staging / f"{index:06d}.xyzrgb").write_bytes(records.tobytes())
            counts.append(len(records))
            emit(status=f"Building playable point cloud · frame {index + 1}",
                 progress=0.90 + 0.09 * (index + 1) / len(names))
        manifest = {"version": 1, "format": "xyz-f32le-rgb-u8", "stride": 15,
                    "frames": len(counts), "counts": counts, "fps": settings["fps"],
                    "width": int(rgb.shape[1]), "height": int(rgb.shape[0]),
                    "duration": len(counts) / settings["fps"] if video else settings["duration"],
                    "sampleStep": step, "coordinates": "x-right-y-down-z-forward"}
        (staging / "manifest.json").write_text(json.dumps(manifest, indent=2))
        destination = project / "pointcloud"
        if destination.exists():
            raise ValueError("This project already has a point-cloud sequence. Create a new render to rebuild it.")
        staging.rename(destination)
    write_depth_video(project, cache, names if video else names[:1], settings)
    return manifest


def write_depth_video(project, cache, names, settings):
    """Source-aligned grayscale preview frames; no camera projection or inference."""
    project, cache = Path(project), Path(cache)
    destination = project / "depth-video"
    source_destination = project / "source-video"
    if not source_destination.exists():
        with tempfile.TemporaryDirectory(prefix=".source-video-", dir=project) as staging:
            staging = Path(staging)
            for index, name in enumerate(names):
                with np.load(cache / name, allow_pickle=False) as frame:
                    if "rgb" not in frame:
                        break  # Legacy depth-only fixtures can still prepare their depth pass.
                    Image.fromarray(frame["rgb"]).save(staging / f"{index:06d}.png")
            else:
                (staging / "manifest.json").write_text(json.dumps({"version": 1, "frames": len(names), "fps": settings["fps"]}))
                staging.rename(source_destination)
    if destination.exists():
        if (destination / "manifest.json").is_file():
            return
        raise ValueError("An incomplete depth-video folder already exists.")
    with tempfile.TemporaryDirectory(prefix=".depth-video-", dir=project) as staging:
        staging = Path(staging)
        for index, name in enumerate(names):
            with np.load(cache / name, allow_pickle=False) as frame:
                depth = frame["depth"].astype(np.float32)
            if depth.ndim != 2 or not np.isfinite(depth).all():
                raise ValueError("Invalid cached depth video frame.")
            if settings["invert"]:
                depth = 1 - depth
            Image.fromarray(np.round(np.clip(depth, 0, 1) * 255).astype(np.uint8)).save(staging / f"{index:06d}.png")
            emit(status=f"Preparing depth video · frame {index + 1}", progress=(index + 1) / len(names))
        (staging / "manifest.json").write_text(json.dumps({"version": 1, "frames": len(names),
            "fps": settings["fps"], "width": int(depth.shape[1]), "height": int(depth.shape[0]), "whiteIsNear": True}))
        staging.rename(destination)


def prepare_project_depth_video(path):
    load_libraries()
    path = Path(path).resolve(strict=True)
    recipe = json.loads(path.read_text())
    settings = validate(recipe)
    count = int(recipe["frames"]) if recipe["video"] else 1
    if not 1 <= count <= 360:
        raise ValueError("Invalid depth video frame count.")
    names = [f"{index:06d}.npz" for index in range(count)]
    write_depth_video(path.parent, Path(recipe["cachePath"]), names, settings)
    emit(done=True, status="Depth video ready", progress=1)


def prepare_project_cloud(path):
    load_libraries()
    path = Path(path).resolve(strict=True)
    recipe = json.loads(path.read_text())
    settings = validate(recipe)
    cache = Path(recipe["cachePath"])
    if (cache / "manifest.json").is_file():
        saved = json.loads((cache / "manifest.json").read_text())
    else:
        # Early saved projects retain numbered frames but may lack a manifest.
        count = int(recipe["frames"]) if recipe["video"] else 1
        if not 1 <= count <= 360:
            raise ValueError("Invalid cached point-cloud frame count.")
        names = [f"{index:06d}.npz" for index in range(count)]
        if not all((cache / name).is_file() for name in names):
            raise ValueError("Some saved depth frames are missing. Rebuild the project from its source.")
        saved = {"frames": names, "video": recipe["video"]}
    recipe["pointCloud"] = write_cloud_sequence(path.parent, cache, saved["frames"], saved["video"], settings)
    # Only publish after every frame is complete; the existing movie stays usable.
    temporary = path.with_name(path.name + ".cloud-tmp")
    temporary.write_text(json.dumps(recipe, indent=2))
    temporary.replace(path)
    emit(done=True, status="Playable point cloud ready", progress=1)


def validate(request):
    settings = {"depth": 0.65, "motion": 0.025, "convergence": 0.5, "path": "slide", "style": "photo",
                "duration": 6.0, "start": 0.0, "fps": 24, "edge": 960, "invert": False}
    settings.update(request.get("settings", {}))
    for key, low, high in (("depth", 0, 1.5), ("motion", 0, 0.08), ("convergence", 0, 1),
                           ("duration", 1 / 24, 12), ("start", 0, 86400), ("fps", 12, 30), ("edge", 256, 1920)):
        value = settings[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
            raise ValueError(f"Invalid {key}: expected {low}–{high}.")
    for key in ("edge", "fps"):
        if int(settings[key]) != settings[key]:
            raise ValueError(f"{key} must be an integer.")
    if settings["path"] not in ("slide", "arc", "push") or settings["style"] not in ("photo", "points"):
        raise ValueError("Unknown camera path or rendering style.")
    if not isinstance(settings["invert"], bool):
        raise ValueError("Invert depth must be a boolean.")
    appearance = {"colorMode": 0, "brightness": 1, "gradientNear": "#0000ff",
                  "gradientMid": "#00ff00", "gradientFar": "#ff0000", "background": "#000000"}
    saved_view = request.get("interactiveView", {})
    supplied = settings.get("pointAppearance", saved_view.get("appearance", {}) if isinstance(saved_view, dict) else {})
    if not isinstance(supplied, dict):
        raise ValueError("Point appearance must be an object.")
    appearance.update(supplied)
    if type(appearance["colorMode"]) is not int or appearance["colorMode"] not in (0, 1, 2):
        raise ValueError("Unknown point-cloud color mode.")
    brightness = appearance["brightness"]
    if isinstance(brightness, bool) or not isinstance(brightness, (int, float)) or not math.isfinite(brightness) or not 0.1 <= brightness <= 3:
        raise ValueError("Point-cloud brightness must be between 0.1 and 3.")
    for key in ("gradientNear", "gradientMid", "gradientFar", "background"):
        value = appearance[key]
        if not isinstance(value, str) or len(value) != 7 or value[0] != "#" or any(c not in "0123456789abcdefABCDEF" for c in value[1:]):
            raise ValueError(f"Invalid point-cloud {key} color.")
    settings["pointAppearance"] = appearance
    return settings


class Estimator:
    def __init__(self, model_path):
        import torch
        from transformers import AutoImageProcessor, AutoModelForDepthEstimation
        if not Path(model_path, "config.json").is_file():
            raise RuntimeError("Install the local depth model first, or supply your own depth map.")
        self.torch = torch
        self.device = "cuda" if torch.cuda.is_available() else "mps" if torch.backends.mps.is_available() else "cpu"
        self.processor = AutoImageProcessor.from_pretrained(model_path, local_files_only=True, use_fast=False)
        self.model = AutoModelForDepthEstimation.from_pretrained(model_path, local_files_only=True).to(self.device).eval()

    def __call__(self, rgb):
        torch = self.torch
        inputs = self.processor(images=Image.fromarray(rgb), return_tensors="pt").to(self.device)
        with torch.inference_mode():
            prediction = self.model(**inputs).predicted_depth[:, None]
            prediction = torch.nn.functional.interpolate(prediction, size=rgb.shape[:2], mode="bicubic", align_corners=False)
        return prediction[0, 0].float().cpu().numpy()


def probe_video(path, ffprobe):
    result = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                             "stream=width,height:format=duration", "-of", "json", str(path)],
                            check=True, capture_output=True, text=True)
    metadata = json.loads(result.stdout)
    return float(metadata.get("format", {}).get("duration", 0))


def frames(source, settings, ffmpeg, ffprobe):
    try:
        image = ImageOps.exif_transpose(Image.open(source)).convert("RGB")
    except (OSError, ValueError):
        image = None
    if image is not None:
        image.thumbnail((settings["edge"], settings["edge"]))
        width, height = max(2, image.width // 2 * 2), max(2, image.height // 2 * 2)
        rgb = np.array(image.resize((width, height), Image.Resampling.LANCZOS))
        yield rgb, False
        return
    duration = probe_video(source, ffprobe)
    if duration <= settings["start"]:
        raise ValueError("The source start is beyond the end of the video.")
    filters = f"fps={settings['fps']},scale={settings['edge']}:{settings['edge']}:force_original_aspect_ratio=decrease:force_divisible_by=2"
    # Image2pipe carries each frame's dimensions and honors FFmpeg's autorotation.
    with tempfile.TemporaryFile() as diagnostics:
        process = subprocess.Popen([ffmpeg, "-v", "error", "-ss", str(settings["start"]), "-i", str(source),
                                    "-t", str(settings["duration"]), "-vf", filters,
                                    "-f", "image2pipe", "-vcodec", "ppm", "-"],
                                   stdout=subprocess.PIPE, stderr=diagnostics)
        CHILDREN.append(process)
        try:
            while True:
                magic = process.stdout.readline()
                if not magic:
                    break
                if magic.strip() != b"P6":
                    raise RuntimeError("Unexpected video frame encoding.")
                width, height = map(int, process.stdout.readline().split())
                if process.stdout.readline().strip() != b"255":
                    raise RuntimeError("Unexpected video frame precision.")
                data = process.stdout.read(width * height * 3)
                if len(data) != width * height * 3:
                    raise RuntimeError("The video ended during a frame.")
                yield np.frombuffer(data, np.uint8).reshape(height, width, 3).copy(), True
            if process.wait() != 0:
                diagnostics.seek(0)
                raise RuntimeError(diagnostics.read().decode(errors="replace")[-3000:])
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait()


def generate(request, output):
    load_libraries()
    settings = validate(request)
    source = Path(request["source"]).resolve(strict=True)
    depth_path = Path(request["depthMap"]).resolve(strict=True) if request.get("depthMap") else None
    ffmpeg = request.get("ffmpeg") or shutil.which("ffmpeg")
    ffprobe = request.get("ffprobe") or shutil.which("ffprobe")
    if not ffmpeg or not ffprobe:
        raise RuntimeError("Depth Motion needs FFmpeg and FFprobe to read and render clips.")
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    emit(status="Reading source…", progress=0.01)
    identity = {"source": file_hash(source), "depthMap": file_hash(depth_path) if depth_path else None,
                "sampling": {key: settings[key] for key in ("duration", "start", "fps", "edge")},
                "model": MODEL, "cacheVersion": 1}
    cache = Path(request.get("cachePath") or output / "cache")
    manifest = cache / "manifest.json"
    reused = False
    if manifest.is_file():
        saved = json.loads(manifest.read_text())
        owner_identity = request.get("cacheIdentity")
        reused = (saved.get("identity") == identity and
                  (not owner_identity or owner_identity == saved.get("identity")) and
                  all((cache / name).is_file() for name in saved["frames"]))
    if not reused:
        cache = output / "cache"
        manifest = cache / "manifest.json"
        cache.mkdir()
        estimator = None
        supplied_depth = None
        if depth_path:
            supplied_depth = np.array(Image.open(depth_path), dtype=np.float32)
            if supplied_depth.ndim == 3:
                supplied_depth = supplied_depth[..., 0]
        else:
            emit(status="Loading local depth model…", progress=0.02)
            estimator = Estimator(request["modelPath"])
        names, previous, limits = [], None, None
        video = False
        for index, (rgb, video) in enumerate(frames(source, settings, ffmpeg, ffprobe)):
            if supplied_depth is not None and video:
                raise ValueError("A single supplied depth map is for still images. Use automatic depth for video.")
            if supplied_depth is not None and supplied_depth.shape != rgb.shape[:2]:
                # Aspect must match even when a full-resolution map accompanies a downscaled source.
                if abs(supplied_depth.shape[1] / supplied_depth.shape[0] - rgb.shape[1] / rgb.shape[0]) > 0.02:
                    raise ValueError("The depth map and source image must have the same aspect ratio.")
                raw = cv2.resize(supplied_depth, (rgb.shape[1], rgb.shape[0]), interpolation=cv2.INTER_LINEAR)
            else:
                raw = supplied_depth if supplied_depth is not None else estimator(rgb)
            current_limits = np.percentile(raw, [2, 98])
            gray = cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY)
            cut = previous is None or float(np.mean(np.abs(gray.astype(np.float32) - previous[0]))) > 35
            limits = current_limits if cut or limits is None else limits * 0.9 + current_limits * 0.1
            depth = normalize_depth(raw, limits)
            if video:
                depth, previous = stabilize_depth(depth, rgb, previous)
            name = f"{index:06d}.npz"
            np.savez_compressed(cache / name, rgb=rgb, depth=depth.astype(np.float32))
            names.append(name)
            expected = max(1, int(settings["duration"] * settings["fps"])) if video else 1
            emit(status=f"Estimating depth · frame {index + 1}", progress=0.05 + 0.55 * min(1, (index + 1) / expected))
        if not names:
            raise ValueError("No readable frames were found in this source.")
        saved = {"identity": identity, "frames": names, "video": video}
        manifest.write_text(json.dumps(saved))
    else:
        emit(status="Reusing saved depth…", progress=0.6)
    names, video = saved["frames"], saved["video"]
    count = len(names) if video else max(1, int(round(settings["duration"] * settings["fps"])))
    with np.load(cache / names[0], allow_pickle=False) as first:
        rgb, depth = first["rgb"], first["depth"].astype(np.float32)
    if settings["invert"]:
        depth = 1 - depth
    Image.fromarray(rgb).save(output / "source.png")
    Image.fromarray((depth * 65535).astype(np.uint16)).save(output / "depth.png")
    write_ply(output / "cloud.ply", rgb, depth, settings)
    height, width = rgb.shape[:2]
    command = [ffmpeg, "-v", "error", "-n", "-f", "rawvideo", "-pixel_format", "rgb24",
               "-video_size", f"{width}x{height}", "-framerate", str(settings["fps"]), "-i", "-"]
    if video:
        command += ["-ss", str(settings["start"]), "-i", str(source), "-map", "0:v:0", "-map", "1:a:0?",
                    "-c:a", "aac", "-b:a", "192k"]
    command += ["-t", str(count / settings["fps"]), "-c:v", "libx264", "-crf", "18", "-preset", "medium",
                "-vf", "scale=in_range=full:out_range=tv:out_color_matrix=bt709", "-pix_fmt", "yuv420p",
                "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709",
                "-movflags", "+faststart", str(output / "motion.mp4")]
    max_holes = 0
    with tempfile.TemporaryFile() as diagnostics:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stderr=diagnostics)
        CHILDREN.append(process)
        try:
            for index in range(count):
                if video and index > 0:
                    with np.load(cache / names[index], allow_pickle=False) as frame:
                        rgb, depth = frame["rgb"], frame["depth"].astype(np.float32)
                    if settings["invert"]:
                        depth = 1 - depth
                result, holes = render(rgb, depth, index / max(1, count), settings)
                max_holes = max(max_holes, holes)
                process.stdin.write(result.tobytes())
                if index == count // 4:
                    Image.fromarray(result).save(output / "preview.png")
                if index % 6 == 0:
                    emit(status=f"Rendering camera motion · {index + 1}/{count}", progress=0.6 + 0.39 * (index + 1) / count)
            process.stdin.close()
            if process.wait() != 0:
                diagnostics.seek(0)
                raise RuntimeError(diagnostics.read().decode(errors="replace")[-3000:])
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait()
    recipe = {"version": 1, "source": str(source), "depthMap": str(depth_path) if depth_path else "",
              "settings": settings, "cachePath": str(cache.resolve()), "identity": identity,
              "video": video, "frames": count, "width": width, "height": height,
              "maxFilledFraction": max_holes, "model": MODEL if not depth_path else "supplied-depth"}
    recipe["interactiveView"] = dict(request.get("interactiveView", {}))
    if settings["style"] == "points":
        recipe["interactiveView"]["appearance"] = settings["pointAppearance"]
    recipe["pointCloud"] = write_cloud_sequence(output, cache, names, video, settings)
    (output / "project.depthmotion.json").write_text(json.dumps(recipe, indent=2))
    emit(done=True, status="Depth motion ready", progress=1, project=str(output.resolve()),
         cachePath=str(cache.resolve()), filledFraction=max_holes, frames=count, reusedDepth=reused)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--setup")
    parser.add_argument("--download")
    parser.add_argument("--request")
    parser.add_argument("--output")
    parser.add_argument("--prepare-project")
    parser.add_argument("--depth-video")
    args = parser.parse_args()
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    if args.setup:
        setup(args.setup)
    elif args.download:
        from transformers import AutoImageProcessor, AutoModelForDepthEstimation
        AutoImageProcessor.from_pretrained(MODEL).save_pretrained(args.download)
        AutoModelForDepthEstimation.from_pretrained(MODEL).save_pretrained(args.download)
    elif args.depth_video:
        prepare_project_depth_video(args.depth_video)
    elif args.prepare_project:
        prepare_project_cloud(args.prepare_project)
    elif args.request and args.output:
        generate(json.loads(Path(args.request).read_text()), args.output)
    else:
        parser.error("Provide --setup HOME or --request JSON --output NEW_FOLDER.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        emit(error=str(error), status="Depth Motion needs attention")
        print(str(error), file=sys.stderr)
        stop()
