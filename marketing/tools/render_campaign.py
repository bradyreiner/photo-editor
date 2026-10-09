#!/usr/bin/env python3
"""Render locally reviewable Looksy campaign videos from a JSON manifest.

Asset and evidence paths are relative to the marketing/ directory. Draft renders
are watermarked. Final renders require explicit rights and proof records for all
used assets, product captures, and referenced claims. No audio is added unless an
audio_track asset is explicitly named in the manifest.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw, ImageFont, ImageFilter


FPS = 30
ASPECTS: dict[str, tuple[int, int]] = {
    "9:16": (1080, 1920),
    "4:5": (1080, 1350),
    "16:9": (1920, 1080),
}
PROFILE_SCALE = {"preview": 0.5, "final": 1.0}
VALID_MODE = {"draft", "final"}


class CampaignError(Exception):
    """A user-correctable campaign, evidence, or rendering error."""


@dataclass(frozen=True)
class Validation:
    manifest: dict[str, Any]
    root: Path
    assets: dict[str, dict[str, Any]]
    asset_paths: dict[str, Path]
    logo_path: Path | None
    total_duration: float
    aspect_ratios: tuple[str, ...]


def _is_within(child: Path, parent: Path) -> bool:
    try:
        child.relative_to(parent)
        return True
    except ValueError:
        return False


def _local_file(root: Path, value: str, label: str, required: bool) -> Path | None:
    if not isinstance(value, str) or not value.strip():
        if required:
            raise CampaignError(f"{label} must be a non-empty marketing-relative path")
        return None
    path = (root / value).resolve()
    if not _is_within(path, root.resolve()):
        raise CampaignError(f"{label} escapes the marketing directory: {value}")
    if required and not path.is_file():
        raise CampaignError(f"{label} does not resolve to an existing file: {value}")
    return path


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _validate_evidence_refs(root: Path, refs: Any, label: str, required: bool) -> None:
    if not isinstance(refs, list):
        if required:
            raise CampaignError(f"{label} must be an array of marketing-relative paths")
        return
    if required and not refs:
        raise CampaignError(f"{label} must contain at least one evidence path")
    for ref in refs:
        _local_file(root, ref, label, True)


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise CampaignError(f"cannot read manifest {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise CampaignError(f"invalid JSON in {path}:{exc.lineno}:{exc.colno}: {exc.msg}") from exc
    if not isinstance(value, dict):
        raise CampaignError("manifest root must be a JSON object")
    return value


def validate_campaign(manifest: dict[str, Any], root: Path, mode: str = "draft") -> Validation:
    issues: list[str] = []

    def issue(text: str) -> None:
        issues.append(text)

    if mode not in VALID_MODE:
        raise CampaignError(f"mode must be one of: {', '.join(sorted(VALID_MODE))}")
    if manifest.get("schema_version") != 1:
        issue("schema_version must be 1")
    campaign_id = manifest.get("campaign_id")
    if not isinstance(campaign_id, str) or not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", campaign_id):
        issue("campaign_id must contain lowercase letters, digits, underscores, or hyphens")
    if not isinstance(manifest.get("title"), str) or not manifest.get("title", "").strip():
        issue("title must be a non-empty string")

    brand = manifest.get("brand")
    if not isinstance(brand, dict) or not isinstance(brand.get("name"), str):
        issue("brand.name is required")
        brand = {}
    colors = brand.get("colors", {})
    if not isinstance(colors, dict):
        colors = {}
        issue("brand.colors must be an object")
    for name in ("background", "foreground", "accent", "optical"):
        if not isinstance(colors.get(name), str) or not re.fullmatch(r"#[0-9A-Fa-f]{6}", colors.get(name, "")):
            issue(f"brand.colors.{name} must be a six-digit hex color")
    logo_path: Path | None = None
    if brand.get("logo_asset"):
        try:
            logo_path = _local_file(root, brand["logo_asset"], "brand.logo_asset", True)
        except CampaignError as exc:
            issue(str(exc))

    cta = manifest.get("cta")
    if not isinstance(cta, dict) or not isinstance(cta.get("label"), str) or not cta.get("label", "").strip():
        issue("cta.label is required")

    aspects = manifest.get("aspect_ratios", list(ASPECTS))
    if not isinstance(aspects, list) or not aspects:
        issue("aspect_ratios must be a non-empty array")
        aspects = list(ASPECTS)
    unknown_aspects = [aspect for aspect in aspects if aspect not in ASPECTS]
    if unknown_aspects:
        issue(f"unsupported aspect ratio(s): {', '.join(map(str, unknown_aspects))}")
    aspects = [aspect for aspect in aspects if aspect in ASPECTS]

    assets_value = manifest.get("assets")
    if not isinstance(assets_value, list):
        issue("assets must be an array")
        assets_value = []
    assets: dict[str, dict[str, Any]] = {}
    asset_paths: dict[str, Path] = {}
    for index, asset in enumerate(assets_value):
        where = f"assets[{index}]"
        if not isinstance(asset, dict):
            issue(f"{where} must be an object")
            continue
        asset_id = asset.get("id")
        if not isinstance(asset_id, str) or not asset_id:
            issue(f"{where}.id must be a non-empty string")
            continue
        if asset_id in assets:
            issue(f"duplicate asset id: {asset_id}")
            continue
        if asset.get("kind") not in {"image", "video", "audio", "capture"}:
            issue(f"{where}.kind must be image, video, audio, or capture")
        if asset.get("media_type") is not None and asset.get("media_type") not in {"image", "video"}:
            issue(f"{where}.media_type must be image or video")
        if asset.get("media_type") == "video" and asset.get("kind") != "capture":
            issue(f"{where}.media_type=video is supported only for capture assets")
        if asset.get("rights_status") not in {"cleared", "internal-review"}:
            issue(f"{where}.rights_status must be cleared or internal-review")
        assets[asset_id] = asset
        try:
            path = _local_file(root, asset.get("path"), f"{where}.path", True)
        except CampaignError as exc:
            issue(str(exc))
            path = None
        if path is not None:
            asset_paths[asset_id] = path
            expected_hash = asset.get("sha256")
            if expected_hash is not None:
                if not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", expected_hash):
                    issue(f"{where}.sha256 must be a 64-character SHA-256 hex string")
                elif path.is_file() and _sha256(path).lower() != expected_hash.lower():
                    issue(f"{where}.sha256 does not match file contents for {asset_id}")

    claims_value = manifest.get("claims")
    if not isinstance(claims_value, list):
        issue("claims must be an array")
        claims_value = []
    claims: dict[str, dict[str, Any]] = {}
    for index, claim in enumerate(claims_value):
        where = f"claims[{index}]"
        if not isinstance(claim, dict):
            issue(f"{where} must be an object")
            continue
        claim_id = claim.get("id")
        if not isinstance(claim_id, str) or not claim_id:
            issue(f"{where}.id must be a non-empty string")
            continue
        if claim_id in claims:
            issue(f"duplicate claim id: {claim_id}")
        claims[claim_id] = claim
        if not isinstance(claim.get("text"), str) or not claim.get("text", "").strip():
            issue(f"{where}.text must be a non-empty string")
        if claim.get("status") not in {"verified", "pending"}:
            issue(f"{where}.status must be verified or pending")
        if not isinstance(claim.get("evidence_refs"), list):
            issue(f"{where}.evidence_refs must be an array")

    scenes_value = manifest.get("scenes")
    if not isinstance(scenes_value, list) or not scenes_value:
        issue("scenes must be a non-empty array")
        scenes_value = []
    total_duration = 0.0
    used_asset_ids: set[str] = set()
    used_claim_ids: set[str] = set()
    scene_ids: set[str] = set()
    for index, scene in enumerate(scenes_value):
        where = f"scenes[{index}]"
        if not isinstance(scene, dict):
            issue(f"{where} must be an object")
            continue
        scene_id = scene.get("id")
        if not isinstance(scene_id, str) or not scene_id:
            issue(f"{where}.id must be a non-empty string")
        elif scene_id in scene_ids:
            issue(f"duplicate scene id: {scene_id}")
        else:
            scene_ids.add(scene_id)
        scene_type = scene.get("type")
        if scene_type not in {"title", "image", "video", "app", "compare"}:
            issue(f"{where}.type must be title, image, video, app, or compare")
        duration = scene.get("duration_s")
        if not isinstance(duration, (int, float)) or isinstance(duration, bool) or not 0 < duration <= 60:
            issue(f"{where}.duration_s must be between 0 and 60 seconds")
        else:
            total_duration += duration
        for text_key in ("headline", "body"):
            if not isinstance(scene.get(text_key), str):
                issue(f"{where}.{text_key} must be a string")
        media_refs = scene.get("media_refs", [])
        if not isinstance(media_refs, list) or any(not isinstance(ref, str) for ref in media_refs):
            issue(f"{where}.media_refs must be an array of asset IDs")
            media_refs = []
        required_media = scene_type in {"image", "video", "app", "compare"}
        if required_media and not media_refs:
            issue(f"{where}.media_refs is required for {scene_type} scenes")
        if scene_type == "compare" and len(media_refs) != 2:
            issue(f"{where}.compare scenes require exactly two media_refs")
        if scene_type in {"image", "video", "app"} and len(media_refs) != 1:
            issue(f"{where}.{scene_type} scenes require exactly one media_ref")
        for ref in media_refs:
            used_asset_ids.add(ref)
            if ref not in assets:
                issue(f"{where} references unknown asset: {ref}")
            elif scene_type == "video" and assets[ref].get("kind") != "video":
                issue(f"{where} requires a video asset: {ref}")
            elif scene_type == "app" and assets[ref].get("kind") != "capture":
                issue(f"{where} requires a capture asset: {ref}")
            elif scene_type in {"image", "compare"} and assets[ref].get("kind") not in {"image", "capture"}:
                issue(f"{where} requires image or capture assets: {ref}")
        motion = scene.get("motion", "slow_zoom")
        if motion not in {"slow_zoom", "pan_left", "pan_right", "none"}:
            issue(f"{where}.motion has an unsupported value")
        claim_refs = scene.get("claim_refs", [])
        if not isinstance(claim_refs, list) or any(not isinstance(ref, str) for ref in claim_refs):
            issue(f"{where}.claim_refs must be an array of claim IDs")
            claim_refs = []
        for ref in claim_refs:
            used_claim_ids.add(ref)
            if ref not in claims:
                issue(f"{where} references unknown claim: {ref}")

    audio_id = manifest.get("audio_track")
    if audio_id is not None:
        if not isinstance(audio_id, str) or audio_id not in assets:
            issue("audio_track must name an asset ID")
        elif assets[audio_id].get("kind") != "audio":
            issue("audio_track must reference an audio asset")
        else:
            used_asset_ids.add(audio_id)

    root = root.resolve()
    if mode == "final" and logo_path is not None:
        if brand.get("logo_rights_status") != "cleared":
            issue("final render blocked: brand logo rights are not marked cleared")
        if brand.get("logo_review_status") != "approved":
            issue("final render blocked: brand logo has not passed human review")
        try:
            _validate_evidence_refs(root, brand.get("logo_rights_refs"), "brand.logo_rights_refs", True)
        except CampaignError as exc:
            issue(f"final render blocked: {exc}")
    if logo_path is not None and brand.get("logo_sha256") is not None:
        expected_logo_hash = brand.get("logo_sha256")
        if not isinstance(expected_logo_hash, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", expected_logo_hash):
            issue("brand.logo_sha256 must be a 64-character SHA-256 hex string")
        elif _sha256(logo_path).lower() != expected_logo_hash.lower():
            issue("brand.logo_sha256 does not match file contents")
    for asset_id in sorted(used_asset_ids):
        asset = assets.get(asset_id)
        if not asset:
            continue
        if mode == "final" and asset.get("rights_status") != "cleared":
            issue(f"final render blocked: asset {asset_id} is not rights-cleared")
        if mode == "final":
            try:
                _validate_evidence_refs(root, asset.get("rights_refs"), f"asset {asset_id} rights_refs", True)
            except CampaignError as exc:
                issue(f"final render blocked: {exc}")
        if mode == "final" and any(scene.get("type") == "app" and asset_id in scene.get("media_refs", []) for scene in scenes_value if isinstance(scene, dict)):
            if asset.get("proof_status") != "verified":
                issue(f"final render blocked: app capture {asset_id} has no verified capture proof")
            proof_refs = asset.get("proof_refs", [])
            if not isinstance(proof_refs, list) or not proof_refs:
                issue(f"final render blocked: app capture {asset_id} must include proof_refs")
            else:
                for ref in proof_refs:
                    try:
                        _local_file(root, ref, f"asset {asset_id} proof reference", True)
                    except CampaignError as exc:
                        issue(f"final render blocked: {exc}")
    if mode == "final":
        for claim_id in sorted(used_claim_ids):
            claim = claims.get(claim_id)
            if not claim:
                continue
            if claim.get("status") != "verified":
                issue(f"final render blocked: claim {claim_id} is not verified")
            refs = claim.get("evidence_refs")
            if not isinstance(refs, list) or not refs:
                issue(f"final render blocked: claim {claim_id} has no evidence_refs")
                continue
            for ref in refs:
                try:
                    _local_file(root, ref, f"claim {claim_id} evidence reference", True)
                except CampaignError as exc:
                    issue(f"final render blocked: {exc}")

    if issues:
        raise CampaignError("\n".join(f"- {item}" for item in issues))
    if not total_duration:
        raise CampaignError("campaign has no usable scene duration")
    return Validation(manifest, root, assets, asset_paths, logo_path, total_duration, tuple(aspects))


def _hex(value: str, fallback: str) -> tuple[int, int, int]:
    if not isinstance(value, str) or not re.fullmatch(r"#[0-9A-Fa-f]{6}", value):
        value = fallback
    return tuple(int(value[index:index + 2], 16) for index in (1, 3, 5))  # type: ignore[return-value]


def _rgb(value: str, fallback: str) -> tuple[int, int, int]:
    return _hex(value, fallback)


def _font_path(bold: bool = False) -> str | None:
    suffix = "Arial Bold.ttf" if bold else "Arial.ttf"
    choices = [
        Path("/System/Library/Fonts/Supplemental") / suffix,
        Path("/Library/Fonts") / suffix,
        Path("/usr/share/fonts/truetype/dejavu") / ("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"),
        Path("/usr/share/fonts/truetype/liberation2") / ("LiberationSans-Bold.ttf" if bold else "LiberationSans-Regular.ttf"),
    ]
    for path in choices:
        if path.is_file():
            return str(path)
    return None


def _font(size: int, bold: bool = False) -> ImageFont.ImageFont:
    path = _font_path(bold)
    return ImageFont.truetype(path, max(size, 1)) if path else ImageFont.load_default()


def _wrap(draw: ImageDraw.ImageDraw, text: str, font: ImageFont.ImageFont, max_width: int) -> list[str]:
    lines: list[str] = []
    for paragraph in text.splitlines() or [""]:
        words = paragraph.split()
        if not words:
            lines.append("")
            continue
        line = words[0]
        for word in words[1:]:
            proposed = f"{line} {word}"
            if draw.textlength(proposed, font=font) <= max_width:
                line = proposed
            else:
                lines.append(line)
                line = word
        lines.append(line)
    return lines


def _fit_font(draw: ImageDraw.ImageDraw, text: str, max_width: int, max_size: int, min_size: int, bold: bool) -> tuple[ImageFont.ImageFont, list[str]]:
    for size in range(max_size, max(min_size, 1) - 1, -1):
        font = _font(size, bold)
        lines = _wrap(draw, text, font, max_width)
        if all(draw.textlength(line, font=font) <= max_width for line in lines):
            return font, lines
    font = _font(min_size, bold)
    return font, _wrap(draw, text, font, max_width)


def _make_background(size: tuple[int, int], colors: dict[str, Any]) -> Image.Image:
    width, height = size
    background = _rgb(colors.get("background", ""), "#101116")
    optical = _rgb(colors.get("optical", ""), "#65D9E8")
    accent = _rgb(colors.get("accent", ""), "#FF6254")
    base = Image.new("RGB", size, background)
    # Build the broad optical falloff at low resolution, then scale once so the
    # per-frame path remains bounded and predictable at 1080p/4K canvas sizes.
    glow_size = (max(width // 3, 160), max(height // 3, 160))
    glow = Image.new("RGBA", glow_size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(glow)
    cx, cy = glow_size[0] * 0.50, glow_size[1] * 0.42
    radius = min(glow_size) * 0.50
    for step in range(18, 0, -1):
        r = int(radius * step / 18)
        alpha = int(5 + 17 * (1 - step / 18))
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(*optical, alpha))
    glow = glow.filter(ImageFilter.GaussianBlur(max(4, min(width, height) // 24)))
    glow = glow.resize(size, Image.Resampling.BICUBIC)
    base = Image.alpha_composite(base.convert("RGBA"), glow).convert("RGB")
    overlay = Image.new("RGBA", size, (0, 0, 0, 0))
    od = ImageDraw.Draw(overlay)
    stroke = max(1, min(width, height) // 500)
    # A single off-axis optical arc gives the dark field a photographic, lens-like
    # cue without pretending to be editor chrome or interface capture.
    arc_box = (-width * 0.36, -height * 0.18, width * 0.72, height * 0.56)
    od.arc(arc_box, start=195, end=330, fill=(*optical, 44), width=stroke)
    od.arc((width * 0.38, height * 0.50, width * 1.30, height * 1.24), start=12, end=142, fill=(*accent, 24), width=stroke)
    overlay = overlay.filter(ImageFilter.GaussianBlur(max(2, stroke * 2)))
    return Image.alpha_composite(base.convert("RGBA"), overlay).convert("RGB")


def _interpolate_alpha(keys: list[tuple[float, int]], value: float) -> int:
    if value <= keys[0][0]:
        return keys[0][1]
    for (x0, y0), (x1, y1) in zip(keys, keys[1:]):
        if value <= x1:
            blend = (value - x0) / max(x1 - x0, 1e-9)
            return max(0, min(255, int(round(y0 + (y1 - y0) * blend))))
    return keys[-1][1]


def _make_photo_scrim(size: tuple[int, int], orientation: str) -> Image.Image:
    width, height = size
    if orientation == "vertical-social":
        keys = [(0.0, 40), (0.12, 18), (0.22, 8), (0.34, 52), (0.44, 172), (0.60, 194), (0.66, 148), (0.78, 72), (1.0, 28)]
        gradient = Image.new("RGBA", (1, height))
        gradient.putdata([(0, 0, 0, _interpolate_alpha(keys, y / max(height - 1, 1))) for y in range(height)])
    elif orientation == "portrait-editorial":
        keys = [(0.0, 28), (0.24, 12), (0.50, 8), (0.62, 34), (0.72, 134), (0.86, 205), (0.94, 164), (1.0, 50)]
        gradient = Image.new("RGBA", (1, height))
        gradient.putdata([(0, 0, 0, _interpolate_alpha(keys, y / max(height - 1, 1))) for y in range(height)])
    else:
        keys = [(0.0, 4), (0.44, 4), (0.58, 62), (0.72, 166), (0.88, 192), (1.0, 208)]
        gradient = Image.new("RGBA", (width, 1))
        gradient.putdata([(0, 0, 0, _interpolate_alpha(keys, x / max(width - 1, 1))) for x in range(width)])
    return gradient.resize(size, Image.Resampling.BICUBIC)


def _make_title_treatment(size: tuple[int, int], colors: dict[str, Any]) -> Image.Image:
    width, height = size
    optical = _rgb(colors.get("optical", ""), "#65D9E8")
    accent = _rgb(colors.get("accent", ""), "#FF6254")
    treatment = Image.new("RGBA", size, (0, 0, 0, 0))
    glow = Image.new("RGBA", size, (0, 0, 0, 0))
    glow_draw = ImageDraw.Draw(glow)
    radius = int(min(width, height) * 0.28)
    center = (int(width * 0.54), int(height * (0.46 if height > width else 0.50)))
    glow_draw.ellipse((center[0] - radius, center[1] - radius, center[0] + radius, center[1] + radius), fill=(*optical, 26))
    glow = glow.filter(ImageFilter.GaussianBlur(max(20, min(width, height) // 14)))
    treatment = Image.alpha_composite(treatment, glow)
    rings = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(rings)
    stroke = max(1, int(min(width, height) / 540))
    for index, (radius_scale, alpha) in enumerate(((0.62, 30), (0.84, 21), (1.0, 13))):
        ring_radius = int(radius * radius_scale)
        box = (center[0] - ring_radius, center[1] - ring_radius, center[0] + ring_radius, center[1] + ring_radius)
        draw.arc(box, start=204 + index * 9, end=334 + index * 7, fill=(*optical, alpha), width=stroke)
        draw.arc(box, start=24, end=88, fill=(*accent, max(8, alpha - 10)), width=stroke)
    rings = rings.filter(ImageFilter.GaussianBlur(max(2, stroke * 2)))
    return Image.alpha_composite(treatment, rings)


def _cover(source: Image.Image, size: tuple[int, int], progress: float, motion: str) -> Image.Image:
    width, height = size
    source = source.convert("RGB")
    sw, sh = source.size
    scale = max(width / max(sw, 1), height / max(sh, 1))
    zoom = 1.0
    if motion == "slow_zoom":
        zoom += 0.08 * min(max(progress, 0), 1)
    elif motion in {"pan_left", "pan_right"}:
        zoom += 0.10
    nw, nh = max(width, int(sw * scale * zoom)), max(height, int(sh * scale * zoom))
    image = source.resize((nw, nh), Image.Resampling.LANCZOS)
    max_x, max_y = nw - width, nh - height
    if motion == "pan_left":
        left = int(max_x * (1 - min(max(progress, 0), 1)))
    elif motion == "pan_right":
        left = int(max_x * min(max(progress, 0), 1))
    else:
        left = max_x // 2
    top = max_y // 2
    return image.crop((left, top, left + width, top + height))


def _contain(source: Image.Image, size: tuple[int, int]) -> Image.Image:
    width, height = size
    source = source.convert("RGB")
    sw, sh = source.size
    scale = min(width / max(sw, 1), height / max(sh, 1))
    return source.resize((max(1, int(sw * scale)), max(1, int(sh * scale))), Image.Resampling.LANCZOS)


def _rounded_mask(size: tuple[int, int], radius: int) -> Image.Image:
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius=radius, fill=255)
    return mask


class SceneSource:
    def __init__(self, path: Path, is_video: bool, width: int, height: int, fit: str = "cover") -> None:
        self.path = path
        self.process: subprocess.Popen[bytes] | None = None
        self.width = width
        self.height = height
        self.frame_bytes = width * height * 3
        self.image: Image.Image | None = None
        if is_video:
            if fit == "contain":
                video_filter = f"fps={FPS},scale={width}:{height}:force_original_aspect_ratio=decrease,pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color=black"
            else:
                video_filter = f"fps={FPS},scale={width}:{height}:force_original_aspect_ratio=increase,crop={width}:{height}"
            command = [
                "ffmpeg", "-hide_banner", "-loglevel", "error", "-stream_loop", "-1", "-i", str(path),
                "-vf", video_filter,
                "-an", "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1",
            ]
            self.process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
            if self.process.stdout is None:
                raise CampaignError(f"could not decode video asset: {path}")
        else:
            try:
                with Image.open(path) as source:
                    self.image = source.convert("RGB")
            except OSError as exc:
                raise CampaignError(f"cannot read image asset {path}: {exc}") from exc

    def frame(self) -> Image.Image:
        if self.process is None:
            assert self.image is not None
            return self.image
        assert self.process.stdout is not None
        raw = bytearray()
        while len(raw) < self.frame_bytes:
            chunk = self.process.stdout.read(self.frame_bytes - len(raw))
            if not chunk:
                break
            raw.extend(chunk)
        if len(raw) != self.frame_bytes:
            stderr = b""
            if self.process.stderr is not None:
                stderr = self.process.stderr.read(4096)
            raise CampaignError(f"video decode ended before requested duration: {self.path}: {stderr.decode(errors='replace')}")
        return Image.frombytes("RGB", (self.width, self.height), bytes(raw))

    def close(self) -> None:
        if self.process is not None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
            if self.process.stderr is not None:
                self.process.stderr.close()
            if self.process.stdout is not None:
                self.process.stdout.close()
            self.process = None


def _safe_name(value: str) -> str:
    value = re.sub(r"[^a-zA-Z0-9_-]+", "-", value.strip()).strip("-")
    return value or "scene"


def _draw_brand(frame: Image.Image, brand_name: str, colors: dict[str, Any], scale: float, logo: Image.Image | None = None, top_y: int | None = None) -> None:
    draw = ImageDraw.Draw(frame)
    optical = _rgb(colors.get("optical", ""), "#65D9E8")
    accent = _rgb(colors.get("accent", ""), "#FF6254")
    margin = int(68 * scale)
    x, y = margin, top_y if top_y is not None else margin
    if logo is not None:
        rgba = logo.convert("RGBA")
        frame.paste(rgba, (x, y), rgba.getchannel("A"))
    else:
        dot_size = max(4, int(9 * scale))
        font = _font(max(16, int(22 * scale)), True)
        draw.ellipse((x, y + int(6 * scale), x + dot_size, y + int(6 * scale) + dot_size), fill=accent)
        draw.text((x + dot_size + int(10 * scale), y), brand_name.upper(), fill=(235, 238, 241), font=font, stroke_width=0)
    draw.line((margin, int(112 * scale), int(margin + 74 * scale), int(112 * scale)), fill=(*optical, 180), width=max(1, int(2 * scale)))


def _draw_copy(frame: Image.Image, scene: dict[str, Any], cta: dict[str, Any], colors: dict[str, Any], scale: float, portrait: bool, vertical_social: bool, show_cta: bool) -> None:
    draw = ImageDraw.Draw(frame)
    width, height = frame.size
    fg = _rgb(colors.get("foreground", ""), "#F3F1EE")
    accent = _rgb(colors.get("accent", ""), "#FF6254")
    if vertical_social:
        margin = int(70 * scale)
        block_x, block_y = margin, int(height * 0.425)
        block_w, block_h = width - margin * 2, int(height * 0.16)
        title_size, body_size = int(69 * scale), int(31 * scale)
        cta_y = int(height * 0.61)
    elif portrait:
        margin = int(70 * scale)
        block_x, block_y = margin, int(height * 0.70)
        block_w, block_h = width - margin * 2, int(height * 0.20)
        title_size, body_size = int(76 * scale), int(35 * scale)
        cta_y = int(height * 0.91)
    else:
        margin = int(78 * scale)
        block_x = int(width * 0.61)
        block_y = int(height * (0.52 if scene["type"] == "app" else 0.28))
        block_w = int(width * 0.34)
        block_h = int(height * (0.25 if scene["type"] == "app" else 0.55))
        title_size, body_size = int(80 * scale), int(35 * scale)
        cta_y = int(height * 0.82)
    if scene["type"] == "title":
        if vertical_social:
            block_y, block_h = int(height * 0.34), int(height * 0.27)
        elif portrait:
            block_y, block_h = int(height * 0.37), int(height * 0.33)
        else:
            block_x, block_y, block_w, block_h = int(width * 0.13), int(height * 0.36), int(width * 0.72), int(height * 0.42)
        title_size = int(96 * scale)
    headline = scene.get("headline", "")
    if headline:
        head_font, head_lines = _fit_font(draw, headline, block_w, title_size, int(35 * scale), True)
    else:
        head_font, head_lines = _font(title_size, True), []
    body = scene.get("body", "")
    body_font = _font(max(int(body_size), 12), False)
    body_lines = _wrap(draw, body, body_font, block_w) if body else []
    line_gap = max(4, int(7 * scale))
    head_line_h = max(1, int(head_font.size * 1.05)) if hasattr(head_font, "size") else int(52 * scale)
    body_line_h = max(1, int(body_font.size * 1.42)) if hasattr(body_font, "size") else int(32 * scale)
    # Reduce type until both pieces fit in the reserved safe area. Text stays
    # complete; no copy is clipped or silently replaced with an ellipsis.
    while head_lines and (len(head_lines) * head_line_h + (int(25 * scale) if body_lines else 0) + len(body_lines) * body_line_h > block_h) and head_font.size > int(28 * scale):
        head_font, head_lines = _fit_font(draw, headline, block_w, head_font.size - 2, int(28 * scale), True)
        head_line_h = max(1, int(head_font.size * 1.05))
    while body_lines and (len(head_lines) * head_line_h + len(body_lines) * body_line_h > block_h) and body_font.size > int(18 * scale):
        body_font = _font(body_font.size - 1, False)
        body_lines = _wrap(draw, body, body_font, block_w)
        body_line_h = max(1, int(body_font.size * 1.42))
    draw.rectangle((block_x, block_y - int(17 * scale), block_x + int(64 * scale), block_y - int(13 * scale)), fill=accent)
    y = block_y
    for line in head_lines:
        draw.text((block_x, y), line, fill=fg, font=head_font)
        y += head_line_h + line_gap
    if body_lines:
        y += int(14 * scale)
    for line in body_lines:
        draw.text((block_x, y), line, fill=(188, 194, 202), font=body_font)
        y += body_line_h
    if show_cta:
        label = str(cta.get("label", ""))
        cta_font = _font(max(int(22 * scale), 12), True)
        label_width = int(draw.textlength(label, font=cta_font))
        pad_x, pad_y = int(21 * scale), int(12 * scale)
        rect = (margin, cta_y, margin + label_width + 2 * pad_x, cta_y + int(2 * pad_y + cta_font.size * 1.15))
        draw.rounded_rectangle(rect, radius=int(7 * scale), fill=accent)
        draw.text((rect[0] + pad_x, rect[1] + pad_y), label, fill=(22, 22, 25), font=cta_font)
        if cta.get("destination"):
            dest_font = _font(max(int(17 * scale), 10), False)
            draw.text((rect[2] + int(19 * scale), cta_y + pad_y), str(cta["destination"]), fill=(193, 199, 204), font=dest_font)


def _draw_scene(
    frame: Image.Image,
    source_frames: list[Image.Image],
    scene: dict[str, Any],
    brand: dict[str, Any],
    cta: dict[str, Any],
    progress: float,
    is_final_scene: bool,
    draft: bool,
    portrait: bool,
    vertical_social: bool,
    logo: Image.Image | None = None,
    photo_scrim: Image.Image | None = None,
    title_treatment: Image.Image | None = None,
) -> Image.Image:
    width, height = frame.size
    scale = min(width, height) / 1080.0
    colors = brand.get("colors", {})
    scene_type = scene["type"]
    motion = scene.get("motion", "slow_zoom")
    if scene_type == "title" and title_treatment is not None:
        frame = Image.alpha_composite(frame.convert("RGBA"), title_treatment).convert("RGB")
    elif scene_type in {"image", "video"} and source_frames:
        frame = _cover(source_frames[0], (width, height), progress, motion)
        if photo_scrim is not None:
            frame = Image.alpha_composite(frame.convert("RGBA"), photo_scrim).convert("RGB")
    elif scene_type == "compare" and source_frames:
        if vertical_social:
            box = (int(width * .08), int(height * .20), int(width * .92), int(height * .405))
            gap = int(9 * scale)
            half_h = (box[3] - box[1] - gap) // 2
            boxes = [(box[0], box[1], box[2], box[1] + half_h), (box[0], box[1] + half_h + gap, box[2], box[3])]
        elif portrait:
            box = (int(width * .08), int(height * .14), int(width * .92), int(height * .64))
            gap = int(9 * scale)
            half_h = (box[3] - box[1] - gap) // 2
            boxes = [(box[0], box[1], box[2], box[1] + half_h), (box[0], box[1] + half_h + gap, box[2], box[3])]
        else:
            box = (int(width * .06), int(height * .16), int(width * .57), int(height * .84))
            gap = int(10 * scale)
            half_w = (box[2] - box[0] - gap) // 2
            boxes = [(box[0], box[1], box[0] + half_w, box[3]), (box[0] + half_w + gap, box[1], box[2], box[3])]
        for media, rect in zip(source_frames, boxes):
            x1, y1, x2, y2 = rect
            image = _cover(media, (x2 - x1, y2 - y1), progress, motion)
            frame.paste(image, (x1, y1), _rounded_mask(image.size, int(8 * scale)))
            ImageDraw.Draw(frame).rounded_rectangle(rect, radius=int(8 * scale), outline=(255, 255, 255, 52), width=max(1, int(scale)))
    elif scene_type == "app" and source_frames:
        if vertical_social:
            rect = (int(width * .065), int(height * .20), int(width * .935), int(height * .405))
        elif portrait:
            rect = (int(width * .065), int(height * .14), int(width * .935), int(height * .655))
        else:
            rect = (int(width * .06), int(height * .20), int(width * .56), int(height * .48))
        x1, y1, x2, y2 = rect
        # A screenshot stays a real, readable capture inside a quiet frame;
        # the only surrounding glow comes from the photographic background.
        pad = int(12 * scale)
        content_size = (x2 - x1 - 2 * pad, y2 - y1 - 2 * pad)
        contained = _contain(source_frames[0], content_size)
        card = Image.new("RGB", (x2 - x1, y2 - y1), (16, 18, 22))
        ox = (card.width - contained.width) // 2
        oy = (card.height - contained.height) // 2
        card.paste(contained, (ox, oy))
        frame.paste(card, (x1, y1), _rounded_mask(card.size, int(8 * scale)))
        ImageDraw.Draw(frame).rounded_rectangle(rect, radius=int(8 * scale), outline=(255, 255, 255, 64), width=max(1, int(scale)))
    logo_y = int(height * 0.14) if vertical_social else None
    _draw_brand(frame, str(brand.get("name", "Looksy")), colors, scale, logo, logo_y)
    _draw_copy(frame, scene, cta, colors, scale, portrait, vertical_social, is_final_scene)
    if draft:
        stamp = Image.new("RGBA", frame.size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(stamp)
        stamp_font = _font(max(18, int(27 * scale)), True)
        text = "INTERNAL PREVIEW"
        bbox = draw.textbbox((0, 0), text, font=stamp_font)
        tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
        draw.rounded_rectangle((width - tw - int(56 * scale), height - th - int(55 * scale), width - int(29 * scale), height - int(28 * scale)), radius=int(5 * scale), fill=(14, 17, 22, 185), outline=(216, 223, 231, 65), width=max(1, int(scale)))
        draw.text((width - tw - int(42 * scale), height - th - int(42 * scale)), text, font=stamp_font, fill=(232, 236, 240, 205))
        frame = Image.alpha_composite(frame.convert("RGBA"), stamp).convert("RGB")
    return frame


def _open_scene_sources(validation: Validation, scene: dict[str, Any], width: int, height: int) -> list[SceneSource]:
    sources: list[SceneSource] = []
    for asset_id in scene.get("media_refs", []):
        asset = validation.assets[asset_id]
        path = validation.asset_paths[asset_id]
        is_video = asset.get("kind") == "video" or (scene.get("type") == "app" and asset.get("media_type") == "video")
        fit = "cover"
        source_width, source_height = width, height
        if scene.get("type") == "app" and asset.get("media_type") == "video":
            scale = min(width, height) / 1080.0
            if height / max(width, 1) >= 1.5:
                panel_width, panel_height = int(width * .87), int(height * .205)
            elif width < height:
                panel_width, panel_height = int(width * .87), int(height * .515)
            else:
                panel_width, panel_height = int(width * .50), int(height * .28)
            pad = int(12 * scale)
            source_width = max(2, panel_width - 2 * pad)
            source_height = max(2, panel_height - 2 * pad)
            fit = "contain"
        sources.append(SceneSource(path, is_video, source_width, source_height, fit))
    return sources


def _raw_encoder(path: Path, width: int, height: int, crf: int) -> subprocess.Popen[bytes]:
    command = [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
        "-s", f"{width}x{height}", "-r", str(FPS), "-i", "pipe:0", "-an", "-c:v", "libx264",
        "-preset", "medium", "-crf", str(crf), "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(path),
    ]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, bufsize=0)
    if process.stdin is None:
        raise CampaignError("could not start ffmpeg video encoder")
    return process


def _check_process(process: subprocess.Popen[bytes], label: str) -> None:
    stderr = process.stderr.read() if process.stderr else b""
    if process.stderr:
        process.stderr.close()
    code = process.wait()
    if code:
        raise CampaignError(f"{label} failed with exit code {code}: {stderr.decode(errors='replace')[-1800:]}")


def _format_time(seconds: float) -> str:
    millis = int(round(seconds * 1000))
    hours, millis = divmod(millis, 3_600_000)
    minutes, millis = divmod(millis, 60_000)
    seconds_int, millis = divmod(millis, 1000)
    return f"{hours:02d}:{minutes:02d}:{seconds_int:02d},{millis:03d}"


def write_srt(validation: Validation, output: Path) -> Path:
    output.parent.mkdir(parents=True, exist_ok=True)
    rows: list[str] = []
    cursor = 0.0
    scenes = validation.manifest["scenes"]
    for index, scene in enumerate(scenes, 1):
        start = cursor
        end = cursor + float(scene["duration_s"])
        cursor = end
        text = [str(scene.get("headline", "")).strip(), str(scene.get("body", "")).strip()]
        if index == len(scenes):
            text.append(str(validation.manifest["cta"].get("label", "")).strip())
        caption = "\n".join(line for line in text if line)
        if caption:
            rows.append(f"{index}\n{_format_time(start)} --> {_format_time(end)}\n{caption}")
    output.write_text("\n\n".join(rows) + "\n", encoding="utf-8")
    return output


def _render_one(validation: Validation, aspect: str, profile: str, mode: str, output_dir: Path) -> Path:
    if shutil.which("ffmpeg") is None:
        raise CampaignError("ffmpeg must be installed and available on PATH")
    base_w, base_h = ASPECTS[aspect]
    factor = PROFILE_SCALE[profile]
    width, height = int(base_w * factor), int(base_h * factor)
    # H.264 4:2:0 requires even dimensions.
    width, height = width - width % 2, height - height % 2
    colors = validation.manifest["brand"]["colors"]
    background = _make_background((width, height), colors)
    if height / max(width, 1) >= 1.5:
        scrim_orientation = "vertical-social"
    elif width < height:
        scrim_orientation = "portrait-editorial"
    else:
        scrim_orientation = "landscape"
    photo_scrim = _make_photo_scrim((width, height), scrim_orientation)
    title_treatment = _make_title_treatment((width, height), colors)
    logo: Image.Image | None = None
    if validation.logo_path is not None:
        try:
            with Image.open(validation.logo_path) as source_logo:
                logo = source_logo.convert("RGBA")
        except OSError as exc:
            raise CampaignError(f"cannot read brand logo {validation.logo_path}: {exc}") from exc
        scale = min(width, height) / 1080.0
        target_width, target_height = int(190 * scale), int(52 * scale)
        logo.thumbnail((max(2, target_width), max(2, target_height)), Image.Resampling.LANCZOS)
    output_dir.mkdir(parents=True, exist_ok=True)
    campaign_id = _safe_name(str(validation.manifest["campaign_id"]))
    video_path = output_dir / f"looksy-{campaign_id}-{aspect.replace(':', 'x')}-{profile}.mp4"
    thumb_dir = output_dir / "thumbnails"
    thumb_dir.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="looksy-render-") as temporary_dir:
        silent_path = Path(temporary_dir) / "silent.mp4"
        encoder = _raw_encoder(silent_path, width, height, 19 if profile == "preview" else 17)
        assert encoder.stdin is not None
        global_frame = 0
        try:
            scenes = validation.manifest["scenes"]
            for scene_index, scene in enumerate(scenes):
                scene_frames = max(1, int(round(float(scene["duration_s"]) * FPS)))
                sources = _open_scene_sources(validation, scene, width, height)
                try:
                    thumb_target = scene_frames // 2
                    for local_frame in range(scene_frames):
                        frame = background.copy()
                        if scene["type"] == "title":
                            media_frames: list[Image.Image] = []
                        else:
                            media_frames = [source.frame() for source in sources]
                        progress = local_frame / max(1, scene_frames - 1)
                        frame = _draw_scene(
                            frame, media_frames, scene, validation.manifest["brand"], validation.manifest["cta"], progress,
                            scene_index == len(scenes) - 1, mode == "draft", aspect != "16:9",
                            height / max(width, 1) >= 1.5,
                            logo, photo_scrim, title_treatment,
                        )
                        if local_frame == thumb_target:
                            thumbnail_path = thumb_dir / f"{_safe_name(scene['id'])}.png"
                            frame.save(thumbnail_path, format="PNG", optimize=True)
                        if global_frame == 0:
                            frame.save(output_dir / f"looksy-{campaign_id}-{aspect.replace(':', 'x')}-thumbnail.png", format="PNG", optimize=True)
                        encoder.stdin.write(frame.tobytes())
                        global_frame += 1
                finally:
                    for source in sources:
                        source.close()
            encoder.stdin.close()
        except BrokenPipeError as exc:
            if encoder.stderr:
                details = encoder.stderr.read().decode(errors="replace")
            else:
                details = ""
            encoder.wait()
            raise CampaignError(f"ffmpeg encoder stopped early: {details}") from exc
        except Exception:
            if encoder.stdin and not encoder.stdin.closed:
                encoder.stdin.close()
            encoder.terminate()
            encoder.wait()
            raise
        _check_process(encoder, "video encoding")
        audio_id = validation.manifest.get("audio_track")
        if audio_id:
            final_path = Path(temporary_dir) / "with-audio.mp4"
            audio_path = validation.asset_paths[audio_id]
            command = [
                "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(silent_path), "-i", str(audio_path),
                "-map", "0:v:0", "-map", "1:a:0", "-c:v", "copy", "-c:a", "aac", "-b:a", "192k",
                "-af", "apad", "-t", f"{validation.total_duration:.3f}", "-movflags", "+faststart", str(final_path),
            ]
            proc = subprocess.run(command, capture_output=True, check=False)
            if proc.returncode:
                raise CampaignError(f"audio mux failed: {proc.stderr.decode(errors='replace')[-1800:]}")
            shutil.copy2(final_path, video_path)
        else:
            shutil.copy2(silent_path, video_path)
    write_srt(validation, output_dir / f"looksy-{campaign_id}-{aspect.replace(':', 'x')}.srt")
    _write_render_record(validation, output_dir, aspect, profile, mode, video_path, width, height)
    return video_path


def _write_render_record(validation: Validation, output_dir: Path, aspect: str, profile: str, mode: str, video_path: Path, width: int, height: int) -> None:
    record = {
        "campaign_id": validation.manifest.get("campaign_id"),
        "title": validation.manifest.get("title"),
        "mode": mode,
        "profile": profile,
        "aspect_ratio": aspect,
        "dimensions": {"width": width, "height": height},
        "fps": FPS,
        "duration_s": round(validation.total_duration, 3),
        "video": video_path.name,
        "audio": validation.manifest.get("audio_track"),
        "claim_gate": "passed" if mode == "final" else "draft-review-required",
        "asset_rights_gate": "passed" if mode == "final" else "draft-review-required",
        "rendered_utc": __import__("datetime").datetime.now(__import__("datetime").timezone.utc).isoformat(timespec="seconds"),
    }
    (output_dir / f"looksy-{_safe_name(str(validation.manifest.get('campaign_id')))}-{aspect.replace(':', 'x')}-{profile}.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")


def _ffprobe(video: Path) -> dict[str, Any]:
    command = ["ffprobe", "-v", "error", "-show_entries", "format=duration:stream=codec_name,width,height,r_frame_rate,pix_fmt", "-of", "json", str(video)]
    proc = subprocess.run(command, capture_output=True, check=False)
    if proc.returncode:
        raise CampaignError(f"ffprobe failed: {proc.stderr.decode(errors='replace')}")
    return json.loads(proc.stdout.decode("utf-8"))


def _aspect_label(image: Image.Image) -> str:
    ratio = image.width / max(image.height, 1)
    for label, (width, height) in ASPECTS.items():
        if abs(ratio - width / height) < 0.025:
            return label
    divisor = math.gcd(image.width, image.height)
    return f"{image.width // max(divisor, 1)}:{image.height // max(divisor, 1)}"


def _group_contact_images(entries: list[tuple[Path, Image.Image]]) -> list[tuple[str, list[tuple[Path, Image.Image]]]]:
    groups: dict[str, list[tuple[Path, Image.Image]]] = {}
    for entry in entries:
        groups.setdefault(_aspect_label(entry[1]), []).append(entry)
    return list(groups.items())


def _ordered_scene_thumbnails(directory: Path, scenes: list[dict[str, Any]]) -> list[Path]:
    ordered: list[Path] = []
    for scene in scenes:
        scene_id = scene.get("id") if isinstance(scene, dict) else None
        if not isinstance(scene_id, str) or not scene_id:
            continue
        thumbnail = directory / f"{_safe_name(scene_id)}.png"
        if thumbnail.is_file():
            ordered.append(thumbnail)
    return ordered


def _contact_sheet(inputs: list[Path], output: Path, title: str | None = None) -> Path:
    if not inputs:
        raise CampaignError("no thumbnail images found to make a contact sheet")
    entries: list[tuple[Path, Image.Image]] = []
    for path in inputs:
        try:
            with Image.open(path) as source:
                entries.append((path, source.convert("RGB")))
        except OSError as exc:
            raise CampaignError(f"cannot read thumbnail {path}: {exc}") from exc
    thumb_w = 360
    gap = 26
    label_h = 44
    header_h = 32
    groups = _group_contact_images(entries)
    cols = min(3, max(len(group) for _, group in groups))
    group_layout: list[tuple[str, list[tuple[Path, Image.Image]], int, int]] = []
    canvas_height = gap + (42 + gap if title else 0) + gap
    for ratio, group in groups:
        cell_h = max(int(thumb_w * image.height / image.width) for _, image in group) + label_h
        rows = math.ceil(len(group) / cols)
        group_layout.append((ratio, group, cell_h, rows))
        canvas_height += header_h + rows * (cell_h + gap)
    canvas_width = cols * thumb_w + (cols + 1) * gap
    canvas = Image.new("RGB", (canvas_width, canvas_height), (16, 18, 23))
    draw = ImageDraw.Draw(canvas)
    y = gap
    if title:
        title_font = _font(24, True)
        draw.text((gap, y), title, font=title_font, fill=(240, 242, 244))
        y += 42 + gap
    font = _font(17, False)
    header_font = _font(18, True)
    for ratio, group, cell_h, rows in group_layout:
        draw.text((gap, y), ratio, font=header_font, fill=(103, 221, 235))
        y += header_h
        for index, (path, image) in enumerate(group):
            column, row = index % cols, index // cols
            x = gap + column * (thumb_w + gap)
            image_y = y + row * (cell_h + gap)
            fitted = _contain(image, (thumb_w, cell_h - label_h))
            canvas.paste(fitted, (x + (thumb_w - fitted.width) // 2, image_y))
            draw.text((x, image_y + cell_h - label_h), path.stem, font=font, fill=(188, 194, 202))
        y += rows * (cell_h + gap)
    output.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(output, format="JPEG", quality=92, optimize=True)
    return output


def _manifest_root(manifest_path: Path) -> Path:
    # Manifests in campaigns/<campaign>/campaign.json still resolve media paths
    # from the stable marketing/ root, making moves of campaign JSON harmless.
    return Path(__file__).resolve().parents[1]


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Validate, render, caption, and review Looksy marketing campaigns.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate = subparsers.add_parser("validate", help="validate a campaign manifest and its local asset references")
    validate.add_argument("manifest", type=Path)
    validate.add_argument("--mode", choices=sorted(VALID_MODE), default="draft")
    render = subparsers.add_parser("render", help="render one aspect-ratio video")
    render.add_argument("manifest", type=Path)
    render.add_argument("--aspect", choices=sorted(ASPECTS), required=True)
    render.add_argument("--size", choices=sorted(PROFILE_SCALE), default="preview")
    render.add_argument("--mode", choices=sorted(VALID_MODE), default="draft")
    render.add_argument("--out", type=Path, help="output directory; defaults to marketing/renders/<campaign>/<size>/<aspect>")
    batch = subparsers.add_parser("batch", help="render all declared aspect ratios")
    batch.add_argument("manifest", type=Path)
    batch.add_argument("--size", choices=sorted(PROFILE_SCALE), default="preview")
    batch.add_argument("--mode", choices=sorted(VALID_MODE), default="draft")
    batch.add_argument("--out", type=Path, help="base output directory; defaults to marketing/renders/<campaign>/<size>")
    srt = subparsers.add_parser("srt", help="export scene headlines, body, and final CTA as SRT captions")
    srt.add_argument("manifest", type=Path)
    srt.add_argument("--out", type=Path)
    sheet = subparsers.add_parser("contact-sheet", help="make a contact sheet from scene thumbnail PNGs")
    sheet.add_argument("thumbnail_dir", type=Path)
    sheet.add_argument("--out", type=Path, required=True)
    sheet.add_argument("--title")
    sheet.add_argument("--manifest", type=Path, help="use scene order, and aggregate aspect subdirectories when present")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.command == "contact-sheet":
            if args.manifest:
                manifest = load_manifest(args.manifest)
                scenes = manifest.get("scenes", [])
                if not isinstance(scenes, list):
                    scenes = []
                aspects = manifest.get("aspect_ratios", list(ASPECTS))
                has_aspect_dirs = any((args.thumbnail_dir / str(aspect).replace(":", "x") / "thumbnails").is_dir() for aspect in aspects)
                if has_aspect_dirs:
                    images = []
                    for aspect in aspects:
                        directory = args.thumbnail_dir / str(aspect).replace(":", "x") / "thumbnails"
                        images.extend(_ordered_scene_thumbnails(directory, scenes))
                else:
                    images = _ordered_scene_thumbnails(args.thumbnail_dir, scenes)
            else:
                images = list(args.thumbnail_dir.glob("*.png"))
            output = _contact_sheet(images, args.out, args.title)
            print(output)
            return 0
        manifest_path = args.manifest.resolve()
        manifest = load_manifest(manifest_path)
        root = _manifest_root(manifest_path)
        mode = getattr(args, "mode", "draft")
        validation = validate_campaign(manifest, root, mode)
        if args.command == "validate":
            print(f"valid: {manifest['campaign_id']} ({len(manifest['scenes'])} scenes, {validation.total_duration:.2f}s, mode={mode})")
            return 0
        if args.command == "srt":
            output = args.out or root / "renders" / str(manifest["campaign_id"]) / f"looksy-{_safe_name(str(manifest['campaign_id']))}.srt"
            write_srt(validation, output)
            print(output)
            return 0
        if args.command == "render":
            out_dir = args.out or root / "renders" / str(manifest["campaign_id"]) / args.size / args.aspect.replace(":", "x")
            result = _render_one(validation, args.aspect, args.size, mode, out_dir)
            probe = _ffprobe(result)
            print(f"rendered: {result}")
            print(json.dumps(probe, indent=2))
            return 0
        if args.command == "batch":
            base_dir = args.out or root / "renders" / str(manifest["campaign_id"]) / args.size
            results: list[Path] = []
            for aspect in validation.aspect_ratios:
                aspect_dir = base_dir / aspect.replace(":", "x")
                result = _render_one(validation, aspect, args.size, mode, aspect_dir)
                results.append(result)
                probe = _ffprobe(result)
                stream = next((stream for stream in probe.get("streams", []) if stream.get("codec_name") == "h264"), {})
                print(f"rendered: {result} ({stream.get('width')}x{stream.get('height')}, {probe.get('format', {}).get('duration')}s)")
            images = []
            for aspect in validation.aspect_ratios:
                thumb_dir = base_dir / aspect.replace(":", "x") / "thumbnails"
                images.extend(_ordered_scene_thumbnails(thumb_dir, manifest["scenes"]))
            _contact_sheet(images, base_dir / "contact-sheet.jpg", f"{manifest['title']} · {args.size} · {mode}")
            write_srt(validation, base_dir / f"looksy-{_safe_name(str(manifest['campaign_id']))}.srt")
            print(f"contact sheet: {base_dir / 'contact-sheet.jpg'}")
            return 0
        raise CampaignError(f"unsupported command: {args.command}")
    except CampaignError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    except FileNotFoundError as exc:
        print(f"required executable not found: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
