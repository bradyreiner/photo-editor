#include "preview_backend_controller.h"

#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>

namespace photo_editor::desktop {

namespace {

using photo_editor::pipeline::PreviewBackendKind;
using photo_editor::pipeline::PreviewBackendPreference;

const auto kPreviewBackendPreferenceKey = "preview/backendPreference";

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
      fallback_reason_ = QStringLiteral("GPU preview is unavailable on this machine, so PhotoEditor fell back to CPU.");
    }
  }
}

PreviewBackendController::ProbeResult PreviewBackendController::probeMetal() const {
  ProbeResult result;
#if defined(Q_OS_MACOS)
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  const auto architecture = QSysInfo::currentCpuArchitecture().toLower();
  if (architecture.contains(QStringLiteral("arm")) || architecture.contains(QStringLiteral("aarch64"))) {
    result.available = true;
    return result;
  }

  // Intel Macs also have Metal support
  result.available = true;
  return result;
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
#if defined(PHOTO_EDITOR_ENABLE_CUDA_PREVIEW_BACKEND) && PHOTO_EDITOR_ENABLE_CUDA_PREVIEW_BACKEND
  const auto nvidia_smi = QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
  if (nvidia_smi.isEmpty()) {
    result.reason = QStringLiteral("CUDA preview requires an NVIDIA GPU and nvidia-smi on PATH.");
    return result;
  }

  QProcess process;
  process.start(
      nvidia_smi,
      {
          QStringLiteral("--query-gpu=name"),
          QStringLiteral("--format=csv,noheader"),
      });
  if (!process.waitForFinished(2000) || process.exitCode() != 0) {
    result.reason = QStringLiteral("CUDA preview could not confirm a compatible NVIDIA device; falling back to CPU.");
    return result;
  }

  const auto output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  if (output.isEmpty()) {
    result.reason = QStringLiteral("CUDA preview found no compatible NVIDIA device; falling back to CPU.");
    return result;
  }

  result.reason = QStringLiteral("CUDA preview is planned for supported Windows NVIDIA systems, but this build still uses CPU rendering.");
  return result;
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
