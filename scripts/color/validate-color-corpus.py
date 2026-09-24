#!/usr/bin/env python3
"""Validate the licensed, independently referenced color-validation corpus."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


SCHEMA = "photo-editor-color-validation-corpus:v1"
ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9._-]*$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")

REQUIRED_ILLUMINANTS = {
    "daylight",
    "tungsten",
    "fluorescent-led",
    "mixed",
}
REQUIRED_SCENE_TAGS = {
    "skin-deep",
    "high-iso-shadows",
    "clipped-highlights",
    "foliage",
    "sky",
    "neon-led",
    "saturated-textiles",
}
REQUIRED_EXECUTION_PATHS = {
    "camera-input",
    "icc",
    "sdr",
    "hdr",
    "proof",
    "preview",
    "export",
}
REQUIRED_CAPTURE_PATHS = {"camera-input", "preview", "export"}
REQUIRED_REVIEW_CATEGORIES = {
    "skin",
    "highlight-rolloff",
    "shadow-color",
    "foliage",
    "sky",
    "monochrome",
    "saturated-lights",
}

RAW_CONTAINERS = {
    "3fr",
    "arw",
    "cr2",
    "cr3",
    "dng",
    "erf",
    "fff",
    "iiq",
    "kdc",
    "mef",
    "mos",
    "mrw",
    "nef",
    "nrw",
    "orf",
    "pef",
    "raf",
    "raw",
    "rw2",
    "rwl",
    "srw",
    "x3f",
}

ROOT_KEYS = {
    "schema",
    "corpus_id",
    "corpus_version",
    "description",
    "candidate_rendering_model",
    "quality_gates",
    "asset_count",
    "assets",
    "comparison_sets",
    "review_records",
}
ROOT_REQUIRED = ROOT_KEYS - {"description"}
COMMON_ASSET_KEYS = {
    "asset_id",
    "kind",
    "path",
    "sha256",
    "byte_size",
    "license",
}
ASSET_KEYS = {
    "capture": COMMON_ASSET_KEYS
    | {
        "media_type",
        "container",
        "camera",
        "illuminant",
        "iso",
        "scene_tags",
        "profile_ids",
        "reference_ids",
        "recipe_id",
        "execution_paths",
        "review_regions",
    },
    "profile": COMMON_ASSET_KEYS | {"profile_type", "profile_role", "provenance"},
    "reference": COMMON_ASSET_KEYS
    | {
        "reference_type",
        "producer",
        "color_encoding",
        "observer",
        "white_point",
    },
    "recipe": COMMON_ASSET_KEYS | {"rendering_model", "recipe_schema"},
}
LICENSE_KEYS = {
    "name",
    "distribution",
    "source_url",
    "license_url",
    "evidence",
    "review",
}
LICENSE_REQUIRED = {"name", "distribution", "evidence", "review"}
QUALITY_GATE_KEYS = {
    "linear_one_nits",
    "p95_delta_e_00",
    "max_delta_e_00",
    "p95_delta_e_itp",
    "max_delta_e_itp",
    "max_alpha_error",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate the immutable asset, coverage, independent-reference, license, "
            "and editorial-review contract for the color corpus."
        )
    )
    parser.add_argument("manifest", type=Path, help="Path to the corpus manifest JSON.")
    parser.add_argument(
        "--schema-only",
        action="store_true",
        help="Validate the complete contract without reading referenced files.",
    )
    parser.add_argument(
        "--require-promotion-approval",
        action="store_true",
        help="Require two distinct blinded approvals for the candidate rendering model.",
    )
    return parser.parse_args()


def resolve_path(manifest_path: Path, value: str) -> Path:
    candidate = Path(value).expanduser()
    if candidate.is_absolute():
        return candidate
    return (manifest_path.parent / candidate).resolve()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def check_keys(
    value: dict[str, Any],
    allowed: set[str],
    required: set[str],
    label: str,
    errors: list[str],
) -> None:
    for key in sorted(set(value) - allowed):
        errors.append(f"{label}: unknown field {key!r}")
    for key in sorted(required - set(value)):
        errors.append(f"{label}: missing required field {key!r}")


def require_string(value: Any, label: str, errors: list[str]) -> str | None:
    if not isinstance(value, str) or not value.strip():
        errors.append(f"{label} must be a non-empty string")
        return None
    return value


def require_id(value: Any, label: str, errors: list[str]) -> str | None:
    identifier = require_string(value, label, errors)
    if identifier is not None and ID_PATTERN.fullmatch(identifier) is None:
        errors.append(f"{label} must match {ID_PATTERN.pattern}")
        return None
    return identifier


def require_string_array(
    value: Any,
    label: str,
    errors: list[str],
    *,
    allow_empty: bool = False,
) -> list[str]:
    if not isinstance(value, list) or not all(
        isinstance(item, str) and bool(item.strip()) for item in value
    ):
        errors.append(f"{label} must be an array of non-empty strings")
        return []
    if not value and not allow_empty:
        errors.append(f"{label} must not be empty")
    if len(value) != len(set(value)):
        errors.append(f"{label} must not contain duplicates")
    return value


def validate_date(value: Any, label: str, errors: list[str]) -> None:
    text = require_string(value, label, errors)
    if text is None:
        return
    try:
        dt.date.fromisoformat(text)
    except ValueError:
        errors.append(f"{label} must use YYYY-MM-DD")


def validate_sha256(value: Any, label: str, errors: list[str]) -> str | None:
    if not isinstance(value, str) or SHA256_PATTERN.fullmatch(value) is None:
        errors.append(f"{label} must be a lowercase 64-character SHA-256 digest")
        return None
    return value


def validate_file_contract(
    manifest_path: Path,
    value: dict[str, Any],
    label: str,
    schema_only: bool,
    errors: list[str],
    *,
    path_key: str = "path",
    sha_key: str = "sha256",
    size_key: str = "byte_size",
) -> None:
    path_value = require_string(value.get(path_key), f"{label}.{path_key}", errors)
    expected_sha = validate_sha256(value.get(sha_key), f"{label}.{sha_key}", errors)
    expected_size = value.get(size_key)
    if not isinstance(expected_size, int) or isinstance(expected_size, bool) or expected_size <= 0:
        errors.append(f"{label}.{size_key} must be a positive integer")
        expected_size = None

    if schema_only or path_value is None:
        return

    path = resolve_path(manifest_path, path_value)
    if not path.is_file():
        errors.append(f"{label}.{path_key} does not exist: {path_value}")
        return
    actual_size = path.stat().st_size
    if expected_size is not None and actual_size != expected_size:
        errors.append(
            f"{label}.{size_key} mismatch: manifest={expected_size}, actual={actual_size}"
        )
    if expected_sha is not None:
        actual_sha = sha256_file(path)
        if actual_sha != expected_sha:
            errors.append(
                f"{label}.{sha_key} mismatch: manifest={expected_sha}, actual={actual_sha}"
            )


def validate_license(value: Any, label: str, errors: list[str]) -> None:
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return
    check_keys(value, LICENSE_KEYS, LICENSE_REQUIRED, label, errors)
    require_string(value.get("name"), f"{label}.name", errors)
    distribution = value.get("distribution")
    if distribution not in {"repository", "private-ci"}:
        errors.append(f"{label}.distribution must be repository or private-ci")
    require_string(value.get("evidence"), f"{label}.evidence", errors)
    for key in ("source_url", "license_url"):
        if key in value:
            url = require_string(value.get(key), f"{label}.{key}", errors)
            if url is not None and not url.startswith(("https://", "http://", "internal:")):
                errors.append(f"{label}.{key} must be an http(s) or internal: URI")

    review = value.get("review")
    if not isinstance(review, dict):
        errors.append(f"{label}.review must be an object")
        return
    check_keys(
        review,
        {"reviewer_id", "reviewed_on", "status"},
        {"reviewer_id", "reviewed_on", "status"},
        f"{label}.review",
        errors,
    )
    require_id(review.get("reviewer_id"), f"{label}.review.reviewer_id", errors)
    validate_date(review.get("reviewed_on"), f"{label}.review.reviewed_on", errors)
    if review.get("status") != "approved":
        errors.append(f"{label}.review.status must be approved")


def validate_quality_gates(value: Any, errors: list[str]) -> None:
    if not isinstance(value, dict):
        errors.append("manifest.quality_gates must be an object")
        return
    path_keys = set(value)
    for path in sorted(path_keys - REQUIRED_EXECUTION_PATHS):
        errors.append(f"manifest.quality_gates contains unknown execution path: {path}")
    for path in sorted(REQUIRED_EXECUTION_PATHS - path_keys):
        errors.append(f"manifest.quality_gates is missing execution path: {path}")
    for path, gate in value.items():
        label = f"manifest.quality_gates.{path}"
        if not isinstance(gate, dict):
            errors.append(f"{label} must be an object")
            continue
        check_keys(gate, QUALITY_GATE_KEYS, QUALITY_GATE_KEYS, label, errors)
        parsed: dict[str, float] = {}
        for key in QUALITY_GATE_KEYS:
            metric = gate.get(key)
            if (
                not isinstance(metric, (int, float))
                or isinstance(metric, bool)
                or not 0.0 < float(metric)
            ):
                errors.append(f"{label}.{key} must be a number greater than zero")
            else:
                parsed[key] = float(metric)
        if parsed.get("linear_one_nits", 0.0) > 10000.0:
            errors.append(f"{label}.linear_one_nits must not exceed 10000")
        if parsed.get("max_alpha_error", 0.0) > 1.0:
            errors.append(f"{label}.max_alpha_error must not exceed 1")
        if parsed.get("max_delta_e_00", 0.0) < parsed.get("p95_delta_e_00", 0.0):
            errors.append(f"{label}.max_delta_e_00 must be >= p95_delta_e_00")
        if parsed.get("max_delta_e_itp", 0.0) < parsed.get("p95_delta_e_itp", 0.0):
            errors.append(f"{label}.max_delta_e_itp must be >= p95_delta_e_itp")


def validate_camera(value: Any, label: str, errors: list[str]) -> tuple[str, str] | None:
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return None
    check_keys(value, {"make", "model"}, {"make", "model"}, label, errors)
    make = require_string(value.get("make"), f"{label}.make", errors)
    model = require_string(value.get("model"), f"{label}.model", errors)
    if make is None or model is None:
        return None
    return (make.casefold(), model.casefold())


def validate_regions(value: Any, label: str, errors: list[str]) -> None:
    if not isinstance(value, list) or not value:
        errors.append(f"{label} must be a non-empty array")
        return
    region_keys = {"label", "x", "y", "width", "height", "expected_content"}
    for index, region in enumerate(value):
        region_label = f"{label}[{index}]"
        if not isinstance(region, dict):
            errors.append(f"{region_label} must be an object")
            continue
        check_keys(region, region_keys, region_keys, region_label, errors)
        require_string(region.get("label"), f"{region_label}.label", errors)
        require_string(
            region.get("expected_content"), f"{region_label}.expected_content", errors
        )
        coordinates: dict[str, float] = {}
        for key in ("x", "y", "width", "height"):
            coordinate = region.get(key)
            if (
                not isinstance(coordinate, (int, float))
                or isinstance(coordinate, bool)
                or not 0.0 <= float(coordinate) <= 1.0
            ):
                errors.append(f"{region_label}.{key} must be a normalized number in [0, 1]")
            else:
                coordinates[key] = float(coordinate)
        if coordinates.get("width", 0.0) <= 0.0:
            errors.append(f"{region_label}.width must be greater than zero")
        if coordinates.get("height", 0.0) <= 0.0:
            errors.append(f"{region_label}.height must be greater than zero")
        if coordinates.get("x", 0.0) + coordinates.get("width", 0.0) > 1.0:
            errors.append(f"{region_label} extends beyond the normalized image width")
        if coordinates.get("y", 0.0) + coordinates.get("height", 0.0) > 1.0:
            errors.append(f"{region_label} extends beyond the normalized image height")


def validate_reference_asset(asset: dict[str, Any], label: str, errors: list[str]) -> None:
    reference_type = asset.get("reference_type")
    if reference_type not in {"measured-chart", "independent-render"}:
        errors.append(
            f"{label}.reference_type must be measured-chart or independent-render"
        )
    producer = asset.get("producer")
    if not isinstance(producer, dict):
        errors.append(f"{label}.producer must be an object")
    else:
        producer_keys = {"organization", "tool", "version", "independent_of_photo_editor"}
        check_keys(producer, producer_keys, producer_keys, f"{label}.producer", errors)
        for key in ("organization", "tool", "version"):
            require_string(producer.get(key), f"{label}.producer.{key}", errors)
        if producer.get("independent_of_photo_editor") is not True:
            errors.append(f"{label}.producer.independent_of_photo_editor must be true")
    for key in ("color_encoding", "observer", "white_point"):
        require_string(asset.get(key), f"{label}.{key}", errors)


def validate_asset(
    manifest_path: Path,
    asset: Any,
    index: int,
    schema_only: bool,
    errors: list[str],
) -> tuple[str | None, str | None, dict[str, Any] | None]:
    label = f"assets[{index}]"
    if not isinstance(asset, dict):
        errors.append(f"{label} must be an object")
        return (None, None, None)
    kind = asset.get("kind")
    if kind not in ASSET_KEYS:
        errors.append(f"{label}.kind must be capture, profile, reference, or recipe")
        return (None, None, asset)
    check_keys(asset, ASSET_KEYS[kind], ASSET_KEYS[kind], label, errors)
    asset_id = require_id(asset.get("asset_id"), f"{label}.asset_id", errors)
    validate_file_contract(manifest_path, asset, label, schema_only, errors)
    validate_license(asset.get("license"), f"{label}.license", errors)

    if kind == "capture":
        if asset.get("media_type") != "raw-photo":
            errors.append(f"{label}.media_type must be raw-photo")
        container = asset.get("container")
        if container not in RAW_CONTAINERS:
            errors.append(f"{label}.container is not a supported RAW container")
        validate_camera(asset.get("camera"), f"{label}.camera", errors)
        if asset.get("illuminant") not in REQUIRED_ILLUMINANTS:
            errors.append(
                f"{label}.illuminant must be one of {', '.join(sorted(REQUIRED_ILLUMINANTS))}"
            )
        iso = asset.get("iso")
        if not isinstance(iso, int) or isinstance(iso, bool) or iso <= 0:
            errors.append(f"{label}.iso must be a positive integer")
        require_string_array(asset.get("scene_tags"), f"{label}.scene_tags", errors)
        require_string_array(asset.get("profile_ids"), f"{label}.profile_ids", errors)
        require_string_array(asset.get("reference_ids"), f"{label}.reference_ids", errors)
        require_id(asset.get("recipe_id"), f"{label}.recipe_id", errors)
        execution_paths = set(
            require_string_array(
                asset.get("execution_paths"), f"{label}.execution_paths", errors
            )
        )
        unknown_paths = execution_paths - REQUIRED_EXECUTION_PATHS
        if unknown_paths:
            errors.append(
                f"{label}.execution_paths contains unknown paths: {', '.join(sorted(unknown_paths))}"
            )
        missing_paths = REQUIRED_CAPTURE_PATHS - execution_paths
        if missing_paths:
            errors.append(
                f"{label}.execution_paths is missing required capture paths: "
                f"{', '.join(sorted(missing_paths))}"
            )
        validate_regions(asset.get("review_regions"), f"{label}.review_regions", errors)
    elif kind == "profile":
        if asset.get("profile_type") not in {"dcp", "icc"}:
            errors.append(f"{label}.profile_type must be dcp or icc")
        if asset.get("profile_role") not in {
            "camera",
            "input",
            "display",
            "proof",
            "output",
        }:
            errors.append(f"{label}.profile_role is invalid")
        require_string(asset.get("provenance"), f"{label}.provenance", errors)
    elif kind == "reference":
        validate_reference_asset(asset, label, errors)
    elif kind == "recipe":
        require_string(asset.get("rendering_model"), f"{label}.rendering_model", errors)
        require_string(asset.get("recipe_schema"), f"{label}.recipe_schema", errors)

    return (asset_id, kind, asset)


def validate_comparison_set(
    manifest_path: Path,
    value: Any,
    index: int,
    schema_only: bool,
    candidate_model: str | None,
    errors: list[str],
) -> tuple[str | None, dict[str, Any] | None]:
    label = f"comparison_sets[{index}]"
    keys = {
        "comparison_set_id",
        "candidate_rendering_model",
        "baseline_rendering_model",
        "blinded",
        "artifact_manifest_path",
        "artifact_manifest_sha256",
        "artifact_manifest_byte_size",
        "capture_ids",
        "categories",
        "recipe_sha256",
        "output_profile_sha256",
    }
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return (None, None)
    check_keys(value, keys, keys, label, errors)
    set_id = require_id(value.get("comparison_set_id"), f"{label}.comparison_set_id", errors)
    set_model = require_string(
        value.get("candidate_rendering_model"),
        f"{label}.candidate_rendering_model",
        errors,
    )
    if candidate_model is not None and set_model != candidate_model:
        errors.append(f"{label}.candidate_rendering_model must match the corpus candidate")
    require_string(
        value.get("baseline_rendering_model"),
        f"{label}.baseline_rendering_model",
        errors,
    )
    if value.get("blinded") is not True:
        errors.append(f"{label}.blinded must be true")
    validate_file_contract(
        manifest_path,
        value,
        label,
        schema_only,
        errors,
        path_key="artifact_manifest_path",
        sha_key="artifact_manifest_sha256",
        size_key="artifact_manifest_byte_size",
    )
    require_string_array(value.get("capture_ids"), f"{label}.capture_ids", errors)
    categories = set(
        require_string_array(value.get("categories"), f"{label}.categories", errors)
    )
    unknown_categories = categories - REQUIRED_REVIEW_CATEGORIES
    if unknown_categories:
        errors.append(
            f"{label}.categories contains unknown categories: "
            f"{', '.join(sorted(unknown_categories))}"
        )
    for key in ("recipe_sha256", "output_profile_sha256"):
        validate_sha256(value.get(key), f"{label}.{key}", errors)
    return (set_id, value)


def validate_review_record(
    value: Any,
    index: int,
    comparison_sets: dict[str, dict[str, Any]],
    errors: list[str],
) -> tuple[str | None, dict[str, Any] | None]:
    label = f"review_records[{index}]"
    keys = {
        "review_id",
        "reviewer_id",
        "reviewed_on",
        "comparison_set_id",
        "result",
        "blinded_identity_revealed_after_decision",
        "categories",
        "notes",
    }
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return (None, None)
    check_keys(value, keys, keys - {"notes"}, label, errors)
    review_id = require_id(value.get("review_id"), f"{label}.review_id", errors)
    require_id(value.get("reviewer_id"), f"{label}.reviewer_id", errors)
    validate_date(value.get("reviewed_on"), f"{label}.reviewed_on", errors)
    set_id = require_id(
        value.get("comparison_set_id"), f"{label}.comparison_set_id", errors
    )
    if set_id is not None and set_id not in comparison_sets:
        errors.append(f"{label}.comparison_set_id does not reference a comparison set")
    if value.get("result") not in {"approved", "rejected", "needs-changes"}:
        errors.append(f"{label}.result must be approved, rejected, or needs-changes")
    if not isinstance(value.get("blinded_identity_revealed_after_decision"), bool):
        errors.append(f"{label}.blinded_identity_revealed_after_decision must be boolean")
    categories = set(
        require_string_array(value.get("categories"), f"{label}.categories", errors)
    )
    unknown_categories = categories - REQUIRED_REVIEW_CATEGORIES
    if unknown_categories:
        errors.append(
            f"{label}.categories contains unknown categories: "
            f"{', '.join(sorted(unknown_categories))}"
        )
    if "notes" in value:
        require_string(value.get("notes"), f"{label}.notes", errors)
    return (review_id, value)


def validate_manifest(
    manifest_path: Path,
    *,
    schema_only: bool = False,
    require_promotion_approval: bool = False,
) -> list[str]:
    errors: list[str] = []
    try:
        with manifest_path.open("r", encoding="utf-8") as handle:
            manifest = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        return [f"unable to read manifest: {exc}"]
    if not isinstance(manifest, dict):
        return ["manifest root must be an object"]

    check_keys(manifest, ROOT_KEYS, ROOT_REQUIRED, "manifest", errors)
    if manifest.get("schema") != SCHEMA:
        errors.append(f"manifest.schema must be {SCHEMA!r}")
    require_id(manifest.get("corpus_id"), "manifest.corpus_id", errors)
    corpus_version = manifest.get("corpus_version")
    if not isinstance(corpus_version, int) or isinstance(corpus_version, bool) or corpus_version <= 0:
        errors.append("manifest.corpus_version must be a positive integer")
    if "description" in manifest:
        require_string(manifest.get("description"), "manifest.description", errors)
    candidate_model = require_string(
        manifest.get("candidate_rendering_model"),
        "manifest.candidate_rendering_model",
        errors,
    )
    validate_quality_gates(manifest.get("quality_gates"), errors)

    assets = manifest.get("assets")
    if not isinstance(assets, list) or not assets:
        errors.append("manifest.assets must be a non-empty array")
        assets = []
    asset_count = manifest.get("asset_count")
    if not isinstance(asset_count, int) or isinstance(asset_count, bool) or asset_count < 1:
        errors.append("manifest.asset_count must be a positive integer")
    elif asset_count != len(assets):
        errors.append(
            f"manifest.asset_count mismatch: manifest={asset_count}, actual={len(assets)}"
        )

    asset_by_id: dict[str, tuple[str, dict[str, Any]]] = {}
    captures: list[dict[str, Any]] = []
    for index, asset in enumerate(assets):
        asset_id, kind, validated = validate_asset(
            manifest_path, asset, index, schema_only, errors
        )
        if asset_id is not None and kind is not None and validated is not None:
            if asset_id in asset_by_id:
                errors.append(f"duplicate asset_id: {asset_id}")
            else:
                asset_by_id[asset_id] = (kind, validated)
            if kind == "capture":
                captures.append(validated)

    if len(captures) < 4:
        errors.append("corpus must contain at least four RAW captures")

    illuminants = {
        capture.get("illuminant")
        for capture in captures
        if isinstance(capture.get("illuminant"), str)
    }
    for illuminant in sorted(REQUIRED_ILLUMINANTS - illuminants):
        errors.append(f"missing required illuminant coverage: {illuminant}")

    scene_tags: set[str] = set()
    execution_paths: set[str] = set()
    camera_models: set[tuple[str, str]] = set()
    for capture in captures:
        scene_tags.update(
            item for item in capture.get("scene_tags", []) if isinstance(item, str)
        )
        execution_paths.update(
            item for item in capture.get("execution_paths", []) if isinstance(item, str)
        )
        camera = capture.get("camera")
        if isinstance(camera, dict):
            make = camera.get("make")
            model = camera.get("model")
            if isinstance(make, str) and isinstance(model, str):
                camera_models.add((make.casefold(), model.casefold()))
    for tag in sorted(REQUIRED_SCENE_TAGS - scene_tags):
        errors.append(f"missing required scene coverage: {tag}")
    for path in sorted(REQUIRED_EXECUTION_PATHS - execution_paths):
        errors.append(f"missing required execution-path coverage: {path}")
    if len(camera_models) < 3:
        errors.append("corpus must span at least three distinct camera make/model pairs")

    profile_types = {
        asset.get("profile_type")
        for kind, asset in asset_by_id.values()
        if kind == "profile"
    }
    for profile_type in sorted({"dcp", "icc"} - profile_types):
        errors.append(f"missing required profile type: {profile_type}")
    reference_types = {
        asset.get("reference_type")
        for kind, asset in asset_by_id.values()
        if kind == "reference"
    }
    for reference_type in sorted({"measured-chart", "independent-render"} - reference_types):
        errors.append(f"missing required independent-reference type: {reference_type}")

    for capture in captures:
        capture_id = capture.get("asset_id", "unknown")
        for field, expected_kind in (
            ("profile_ids", "profile"),
            ("reference_ids", "reference"),
        ):
            for linked_id in capture.get(field, []):
                linked = asset_by_id.get(linked_id)
                if linked is None:
                    errors.append(f"capture {capture_id}: {field} references unknown asset {linked_id}")
                elif linked[0] != expected_kind:
                    errors.append(
                        f"capture {capture_id}: {field} references {linked_id}, "
                        f"which is not a {expected_kind}"
                    )
        recipe_id = capture.get("recipe_id")
        linked_recipe = asset_by_id.get(recipe_id)
        if linked_recipe is None:
            errors.append(f"capture {capture_id}: recipe_id references unknown asset {recipe_id}")
        elif linked_recipe[0] != "recipe":
            errors.append(f"capture {capture_id}: recipe_id does not reference a recipe")
        elif candidate_model is not None and linked_recipe[1].get("rendering_model") != candidate_model:
            errors.append(
                f"capture {capture_id}: recipe rendering model does not match corpus candidate"
            )

    comparison_values = manifest.get("comparison_sets")
    if not isinstance(comparison_values, list):
        errors.append("manifest.comparison_sets must be an array")
        comparison_values = []
    comparison_sets: dict[str, dict[str, Any]] = {}
    capture_ids = {
        asset_id for asset_id, (kind, _) in asset_by_id.items() if kind == "capture"
    }
    candidate_recipe_hashes = {
        asset.get("sha256")
        for kind, asset in asset_by_id.values()
        if kind == "recipe" and asset.get("rendering_model") == candidate_model
    }
    profile_hashes = {
        asset.get("sha256")
        for kind, asset in asset_by_id.values()
        if kind == "profile"
    }
    capture_profile_hashes: dict[str, set[str]] = {}
    for capture in captures:
        linked_hashes: set[str] = set()
        for profile_id in capture.get("profile_ids", []):
            linked = asset_by_id.get(profile_id)
            if linked is not None and linked[0] == "profile":
                profile_hash = linked[1].get("sha256")
                if isinstance(profile_hash, str):
                    linked_hashes.add(profile_hash)
        capture_id = capture.get("asset_id")
        if isinstance(capture_id, str):
            capture_profile_hashes[capture_id] = linked_hashes
    for index, comparison in enumerate(comparison_values):
        set_id, validated = validate_comparison_set(
            manifest_path,
            comparison,
            index,
            schema_only,
            candidate_model,
            errors,
        )
        if set_id is not None and validated is not None:
            if set_id in comparison_sets:
                errors.append(f"duplicate comparison_set_id: {set_id}")
            else:
                comparison_sets[set_id] = validated
            for capture_id in validated.get("capture_ids", []):
                if capture_id not in capture_ids:
                    errors.append(
                        f"comparison set {set_id}: capture_ids references unknown capture {capture_id}"
                    )
            recipe_sha = validated.get("recipe_sha256")
            if recipe_sha not in candidate_recipe_hashes:
                errors.append(
                    f"comparison set {set_id}: recipe_sha256 does not identify a candidate recipe asset"
                )
            output_profile_sha = validated.get("output_profile_sha256")
            if output_profile_sha not in profile_hashes:
                errors.append(
                    f"comparison set {set_id}: output_profile_sha256 does not identify a profile asset"
                )
            for capture_id in validated.get("capture_ids", []):
                if (
                    capture_id in capture_profile_hashes
                    and output_profile_sha not in capture_profile_hashes[capture_id]
                ):
                    errors.append(
                        f"comparison set {set_id}: output profile is not linked to capture {capture_id}"
                    )

    review_values = manifest.get("review_records")
    if not isinstance(review_values, list):
        errors.append("manifest.review_records must be an array")
        review_values = []
    review_ids: set[str] = set()
    reviews: list[dict[str, Any]] = []
    for index, review in enumerate(review_values):
        review_id, validated = validate_review_record(
            review, index, comparison_sets, errors
        )
        if review_id is not None:
            if review_id in review_ids:
                errors.append(f"duplicate review_id: {review_id}")
            review_ids.add(review_id)
        if validated is not None:
            reviews.append(validated)

    if require_promotion_approval:
        qualifying_sets = {
            set_id
            for set_id, comparison in comparison_sets.items()
            if comparison.get("blinded") is True
            and set(comparison.get("categories", [])) >= REQUIRED_REVIEW_CATEGORIES
            and set(comparison.get("capture_ids", [])) == capture_ids
        }
        if not qualifying_sets:
            errors.append(
                "promotion approval requires a blinded comparison set covering every capture "
                "and required review category"
            )
        approved_reviewers: set[str] = set()
        for review in reviews:
            if (
                review.get("comparison_set_id") in qualifying_sets
                and review.get("result") == "approved"
                and set(review.get("categories", [])) >= REQUIRED_REVIEW_CATEGORIES
            ):
                reviewer_id = review.get("reviewer_id")
                if isinstance(reviewer_id, str):
                    approved_reviewers.add(reviewer_id)
        if len(approved_reviewers) < 2:
            errors.append(
                "promotion approval requires approved records from at least two distinct reviewers"
            )

    return errors


def main() -> int:
    args = parse_args()
    manifest_path = args.manifest.resolve()
    errors = validate_manifest(
        manifest_path,
        schema_only=args.schema_only,
        require_promotion_approval=args.require_promotion_approval,
    )
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    with manifest_path.open("r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    captures = sum(
        1
        for asset in manifest["assets"]
        if isinstance(asset, dict) and asset.get("kind") == "capture"
    )
    approval_status = (
        "two-reviewer promotion gate passed"
        if args.require_promotion_approval
        else "promotion approval not requested"
    )
    print(
        "color-validation corpus manifest ok: "
        f"{len(manifest['assets'])} immutable assets, {captures} RAW captures, "
        f"{approval_status}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
