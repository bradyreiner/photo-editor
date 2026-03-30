#pragma once

#include <string_view>

namespace photo_editor::pipeline {

enum class PreviewBackendKind {
  Cpu,
  Metal,
  Cuda,
};

enum class PreviewBackendPreference {
  Auto,
  CpuOnly,
  GpuPreferred,
};

struct PreviewBackendCapabilities final {
  PreviewBackendKind kind = PreviewBackendKind::Cpu;
  bool available = false;
  bool gpu = false;
  bool supports_interactive_transform = false;
  bool supports_tone_color = false;
  bool supports_local_adjustments = false;
};

[[nodiscard]] constexpr bool preview_backend_is_gpu(PreviewBackendKind kind) noexcept {
  return kind == PreviewBackendKind::Metal || kind == PreviewBackendKind::Cuda;
}

[[nodiscard]] constexpr std::string_view preview_backend_kind_name(PreviewBackendKind kind) noexcept {
  switch (kind) {
    case PreviewBackendKind::Cpu:
      return "CPU";
    case PreviewBackendKind::Metal:
      return "Metal";
    case PreviewBackendKind::Cuda:
      return "CUDA";
  }
  return "CPU";
}

[[nodiscard]] constexpr std::string_view preview_backend_preference_name(
    PreviewBackendPreference preference) noexcept {
  switch (preference) {
    case PreviewBackendPreference::Auto:
      return "Auto";
    case PreviewBackendPreference::CpuOnly:
      return "CPU";
    case PreviewBackendPreference::GpuPreferred:
      return "GPU Preferred";
  }
  return "Auto";
}

}  // namespace photo_editor::pipeline
