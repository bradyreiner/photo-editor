#pragma once

#include "system_monitor_profile_state.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSettings>

#include <optional>
#include <utility>

namespace photo_editor::desktop {

class DisplayProfileService final {
 public:
  [[nodiscard]] SystemMonitorProfileSnapshot snapshot() const {
    QMutexLocker locker(&mutex_);
    return snapshot_;
  }

  [[nodiscard]] QString cacheIdentity() const {
    return snapshot().identity;
  }

  SystemMonitorProfileRefreshResult refresh(
      SystemMonitorProfileSnapshot observation,
      const std::optional<DisplayTargetCalibration>& calibration =
          std::nullopt) {
    if (calibration.has_value()) {
      observation = apply_display_target_calibration(
          std::move(observation), calibration.value());
    }
    QMutexLocker locker(&mutex_);
    auto refresh =
        reconcile_system_monitor_profile(snapshot_, observation);
    snapshot_ = refresh.snapshot;
    return refresh;
  }

  SystemMonitorProfileRefreshResult refreshFromSettings(
      SystemMonitorProfileSnapshot observation) {
    return refresh(
        std::move(observation),
        configuredDisplayTargetCalibration());
  }

 private:
  static std::optional<DisplayTargetCalibration>
  configuredDisplayTargetCalibration() {
    const auto settings = QSettings{};
    if (!settings
             .value(
                 QStringLiteral("color/displayCalibration/enabled"),
                 false)
             .toBool()) {
      return std::nullopt;
    }
    return DisplayTargetCalibration{
        .display_identity =
            settings
                .value(
                    QStringLiteral(
                        "color/displayCalibration/displayIdentity"))
                .toString(),
        .calibration_identity =
            settings
                .value(
                    QStringLiteral(
                        "color/displayCalibration/calibrationIdentity"))
                .toString(),
        .peak_luminance_nits =
            settings
                .value(
                    QStringLiteral(
                        "color/displayCalibration/peakLuminanceNits"))
                .toDouble(),
        .black_luminance_nits =
            settings
                .value(
                    QStringLiteral(
                        "color/displayCalibration/blackLuminanceNits"))
                .toDouble(),
        .reference_white_nits =
            settings
                .value(
                    QStringLiteral(
                        "color/displayCalibration/referenceWhiteNits"))
                .toDouble(),
        .surround_luminance_nits =
            settings
                .value(
                    QStringLiteral(
                        "color/displayCalibration/surroundLuminanceNits"))
                .toDouble(),
    };
  }

  mutable QMutex mutex_;
  SystemMonitorProfileSnapshot snapshot_;
};

}  // namespace photo_editor::desktop
