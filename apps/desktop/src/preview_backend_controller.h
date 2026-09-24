#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "photo_editor/pipeline/preview_backend.h"

namespace photo_editor::desktop {

class PreviewBackendController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString preferredBackend READ preferredBackend WRITE setPreferredBackend NOTIFY backendChanged)
  Q_PROPERTY(QString activeBackend READ activeBackend NOTIFY backendChanged)
  Q_PROPERTY(QString fallbackReason READ fallbackReason NOTIFY backendChanged)
  Q_PROPERTY(QString backendSummary READ backendSummary NOTIFY backendChanged)
  Q_PROPERTY(QStringList backendPreferenceOptions READ backendPreferenceOptions CONSTANT)
  Q_PROPERTY(bool metalAvailable READ metalAvailable NOTIFY backendChanged)
  Q_PROPERTY(bool cudaAvailable READ cudaAvailable NOTIFY backendChanged)
  Q_PROPERTY(bool gpuAvailable READ gpuAvailable NOTIFY backendChanged)
  Q_PROPERTY(QString metalUnavailableReason READ metalUnavailableReason NOTIFY backendChanged)
  Q_PROPERTY(QString cudaUnavailableReason READ cudaUnavailableReason NOTIFY backendChanged)

 public:
  explicit PreviewBackendController(QObject* parent = nullptr);

  [[nodiscard]] QString preferredBackend() const;
  [[nodiscard]] QString activeBackend() const;
  [[nodiscard]] QString fallbackReason() const;
  [[nodiscard]] QString backendSummary() const;
  [[nodiscard]] QStringList backendPreferenceOptions() const;
  [[nodiscard]] bool metalAvailable() const;
  [[nodiscard]] bool cudaAvailable() const;
  [[nodiscard]] bool gpuAvailable() const;
  [[nodiscard]] QString metalUnavailableReason() const;
  [[nodiscard]] QString cudaUnavailableReason() const;

  void setPreferredBackend(const QString& preferred_backend);

  Q_INVOKABLE void refresh();

 signals:
  void backendChanged();

 private:
  struct ProbeResult final {
    bool available = false;
    QString reason;
  };

  [[nodiscard]] photo_editor::pipeline::PreviewBackendPreference storedPreference() const;
  void writePreference(photo_editor::pipeline::PreviewBackendPreference preference);
  void resolveBackend();
  [[nodiscard]] ProbeResult probeMetal() const;
  [[nodiscard]] ProbeResult probeCuda() const;
  [[nodiscard]] QString preferenceName(photo_editor::pipeline::PreviewBackendPreference preference) const;
  [[nodiscard]] QString kindName(photo_editor::pipeline::PreviewBackendKind kind) const;

  photo_editor::pipeline::PreviewBackendPreference preferred_backend_ =
      photo_editor::pipeline::PreviewBackendPreference::Auto;
  photo_editor::pipeline::PreviewBackendKind active_backend_ =
      photo_editor::pipeline::PreviewBackendKind::Cpu;
  QString fallback_reason_;
  ProbeResult metal_probe_;
  ProbeResult cuda_probe_;
};

}  // namespace photo_editor::desktop
