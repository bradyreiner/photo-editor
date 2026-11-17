#include "preview_backend_controller.h"

#include <QSettings>

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif
#if defined(PHOTO_EDITOR_HAS_CUDA_BACKEND) && PHOTO_EDITOR_HAS_CUDA_BACKEND
#include "photo_editor/pipeline/cuda/cuda_pipeline_backend.h"
#endif

namespace photo_editor::desktop {

namespace {

using photo_editor::pipeline::PreviewBackendKind;
using photo_editor::pipeline::PreviewBackendPreference;

const auto kPreviewBackendPreferenceKey = "preview/backendPreference";

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
photo_editor::pipeline::metal::MetalPipelineBackend& metal_probe_backend() {
  return photo_editor::pipeline::metal::
      shared_metal_pipeline_backend();
}
#endif

#if defined(PHOTO_EDITOR_HAS_CUDA_BACKEND) && PHOTO_EDITOR_HAS_CUDA_BACKEND
photo_editor::pipeline::cuda::CudaPipelineBackend& cuda_probe_backend() {
  return photo_editor::pipeline::cuda::shared_cuda_pipeline_backend();
}
#endif

PreviewBackendPreference preference_from_string(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("cpu")) {
    return PreviewBackendPreference::CpuOnly;
  }
  if (normalized == QStringLiteral("gpu preferred")) {
    return PreviewBackendPreference::GpuPreferred;
  }
  return PreviewBackendPreference::Auto;
}

QString preference_to_string(PreviewBackendPreference preference) {
  return QString::fromUtf8(
      photo_editor::pipeline::preview_backend_preference_name(preference).data(),
      static_cast<int>(photo_editor::pipeline::preview_backend_preference_name(preference).size()));
}

QString kind_to_string(PreviewBackendKind kind) {
  return QString::fromUtf8(
      photo_editor::pipeline::preview_backend_kind_name(kind).data(),
      static_cast<int>(photo_editor::pipeline::preview_backend_kind_name(kind).size()));
}

}  // namespace

PreviewBackendController::PreviewBackendController(QObject* parent) : QObject(parent) {
  preferred_backend_ = storedPreference();
  resolveBackend();
}

QString PreviewBackendController::preferredBackend() const { return preferenceName(preferred_backend_); }

QString PreviewBackendController::activeBackend() const { return kindName(active_backend_); }

QString PreviewBackendController::fallbackReason() const { return fallback_reason_; }

QString PreviewBackendController::backendSummary() const {
  if (fallback_reason_.isEmpty()) {
    return QStringLiteral("%1 preview backend is active.").arg(activeBackend());
  }
  return QStringLiteral("%1 preview backend is active. %2").arg(activeBackend(), fallback_reason_);
}

QStringList PreviewBackendController::backendPreferenceOptions() const {
  return {
      QStringLiteral("Auto"),
      QStringLiteral("CPU"),
      QStringLiteral("GPU Preferred"),
  };
}

bool PreviewBackendController::metalAvailable() const { return metal_probe_.available; }

bool PreviewBackendController::cudaAvailable() const { return cuda_probe_.available; }

bool PreviewBackendController::gpuAvailable() const { return metal_probe_.available || cuda_probe_.available; }

QString PreviewBackendController::metalUnavailableReason() const {
  return metal_probe_.available ? QString{} : metal_probe_.reason;
}

QString PreviewBackendController::cudaUnavailableReason() const {
  return cuda_probe_.available ? QString{} : cuda_probe_.reason;
}

void PreviewBackendController::setPreferredBackend(const QString& preferred_backend) {
  const auto next = preference_from_string(preferred_backend);
  if (preferred_backend_ == next) {
    return;
  }

  preferred_backend_ = next;
  writePreference(next);
  resolveBackend();
  emit backendChanged();
}

void PreviewBackendController::refresh() {
  resolveBackend();
  emit backendChanged();
}

PreviewBackendPreference PreviewBackendController::storedPreference() const {
  const QSettings settings;
  return preference_from_string(settings.value(QString::fromUtf8(kPreviewBackendPreferenceKey), QStringLiteral("Auto")).toString());
}

void PreviewBackendController::writePreference(PreviewBackendPreference preference) {
  QSettings settings;
  settings.setValue(QString::fromUtf8(kPreviewBackendPreferenceKey), preference_to_string(preference));
  settings.sync();
}

void PreviewBackendController::resolveBackend() {
  metal_probe_ = probeMetal();
  cuda_probe_ = probeCuda();
  fallback_reason_.clear();

  if (preferred_backend_ == PreviewBackendPreference::CpuOnly) {
    active_backend_ = PreviewBackendKind::Cpu;
    return;
  }

  if (metal_probe_.available) {
    active_backend_ = PreviewBackendKind::Metal;
    return;
  }

  if (cuda_probe_.available) {
    active_backend_ = PreviewBackendKind::Cuda;
    return;
  }

  active_backend_ = PreviewBackendKind::Cpu;
  if (preferred_backend_ == PreviewBackendPreference::GpuPreferred) {
    if (!metal_probe_.reason.isEmpty()) {
      fallback_reason_ = metal_probe_.reason;
    } else if (!cuda_probe_.reason.isEmpty()) {
      fallback_reason_ = cuda_probe_.reason;
    } else {
      fallback_reason_ = QStringLiteral("GPU preview is unavailable on this machine, so Looksy fell back to CPU.");
    }
  }
}

PreviewBackendController::ProbeResult PreviewBackendController::probeMetal() const {
  ProbeResult result;
#if defined(Q_OS_MACOS)
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  auto& backend = metal_probe_backend();
  if (backend.available()) {
    result.available = true;
    return result;
  }
  const auto reason = QString::fromStdString(backend.unavailable_reason()).trimmed();
  result.reason = reason.isEmpty()
                      ? QStringLiteral("Metal preview backend is unavailable.")
                      : QStringLiteral("Metal preview backend is unavailable: %1").arg(reason);
#else
  result.reason = QStringLiteral("Metal preview backend was not compiled into this build.");
#endif
#else
  result.reason = QStringLiteral("Metal preview is only available on macOS.");
#endif
  return result;
}

PreviewBackendController::ProbeResult PreviewBackendController::probeCuda() const {
  ProbeResult result;
#if defined(Q_OS_WIN)
#if defined(PHOTO_EDITOR_HAS_CUDA_BACKEND) && PHOTO_EDITOR_HAS_CUDA_BACKEND
  auto& backend = cuda_probe_backend();
  if (backend.available()) {
    result.available = true;
    return result;
  }
  const auto reason =
      QString::fromStdString(backend.unavailable_reason()).trimmed();
  result.reason = reason.isEmpty()
                      ? QStringLiteral("CUDA preview backend is unavailable.")
                      : QStringLiteral("CUDA preview backend is unavailable: %1")
                            .arg(reason);
#else
  result.reason = QStringLiteral("CUDA preview backend was not compiled into this build.");
#endif
#else
  result.reason = QStringLiteral("CUDA preview is only available on Windows.");
#endif
  return result;
}

QString PreviewBackendController::preferenceName(PreviewBackendPreference preference) const {
  return preference_to_string(preference);
}

QString PreviewBackendController::kindName(PreviewBackendKind kind) const {
  return kind_to_string(kind);
}

}  // namespace photo_editor::desktop
