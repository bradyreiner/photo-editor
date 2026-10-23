#!/usr/bin/env python3
"""Static contracts for optional CUDA and Little CMS configuration."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")


engine_cmake = (ROOT / "engine/CMakeLists.txt").read_text(encoding="utf-8")
cuda_kernels = (
    ROOT / "engine/src/pipeline/cuda/cuda_pipeline_kernels.cu"
).read_text(encoding="utf-8")
cuda_test = (
    ROOT / "tests/cuda_aces2_output_lut_test.cpp"
).read_text(encoding="utf-8")
test_cmake = (ROOT / "tests/CMakeLists.txt").read_text(encoding="utf-8")

require(
    'if(WIN32 AND PHOTO_EDITOR_ENABLE_CUDA_PREVIEW_BACKEND)' in engine_cmake,
    "CUDA language discovery must remain Windows- and option-gated",
)
require(
    'set(PHOTO_EDITOR_CUDA_ARCHITECTURES "75;86")' in engine_cmake
    and 'set(PHOTO_EDITOR_CUDA_MINIMUM_TOOLKIT_VERSION "11.1")' in engine_cmake,
    "the configured CUDA architectures must have an explicit supported toolkit floor",
)
require(
    "CUDAToolkit_VERSION VERSION_LESS PHOTO_EDITOR_CUDA_MINIMUM_TOOLKIT_VERSION"
    in engine_cmake
    and "CMAKE_CUDA_COMPILER_VERSION VERSION_LESS PHOTO_EDITOR_CUDA_MINIMUM_TOOLKIT_VERSION"
    in engine_cmake,
    "both toolkit and CUDA compiler versions must be checked before adding kernels",
)
require(
    "Windows previews will use CPU fallback" in engine_cmake,
    "unsupported or unavailable CUDA toolchains must fall back cleanly",
)
require(
    'CUDA_ARCHITECTURES "${PHOTO_EDITOR_CUDA_ARCHITECTURES}"' in engine_cmake,
    "the enabled CUDA target must use the architecture list that was validated",
)
require(
    "system-config" not in engine_cmake,
    "Little CMS config targets must never publish a placeholder version",
)
require(
    all(
        name in engine_cmake
        for name in (
            "lcms2_VERSION",
            "LCMS2_VERSION",
            "INTERFACE_INCLUDE_DIRECTORIES",
            "LCMS_VERSION",
            "PHOTO_EDITOR_LCMS_MINIMUM_VERSION",
            "PHOTO_EDITOR_LCMS_VALIDATED_VERSION",
        )
    ),
    "Little CMS config version must come from exported metadata or its public header and be checked",
)
require(
    '"${_encoded_version} / 1000"' in engine_cmake
    and '"(${_encoded_version} / 10) % 100"' in engine_cmake
    and '"${_encoded_version} % 10"' in engine_cmake,
    "the encoded Little CMS header version must be decoded as major.minor.patch",
)
require(
    re.search(
        r"properties\.major\s*>\s*7\s*\|\|\s*\(properties\.major\s*==\s*7\s*&&\s*properties\.minor\s*>=\s*5\)",
        cuda_kernels,
    )
    is not None,
    "runtime availability must match the minimum compiled CUDA architecture",
)
require(
    re.search(
        r"if\s*\(!backend\.available\(\)\)\s*\{.*?return\s+77;",
        cuda_test,
        re.DOTALL,
    )
    is not None,
    "a machine without a suitable CUDA device must skip the hardware test",
)
require(
    "SKIP_RETURN_CODE 77" in test_cmake
    and 'std::cerr << "CUDA output LUT failed:' in cuda_test
    and 'std::cerr << "CUDA scene exposure failed:' in cuda_test,
    "CTest must recognize the no-device skip while kernel failures remain failures",
)

print("PASS: optional CUDA and Little CMS configuration contracts")
