#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include <QColorSpace>
#include <QString>
#include <QtGlobal>

namespace photo_editor::desktop {

struct SystemMonitorProfileSnapshot final {
  QColorSpace color_space;
  QString display_name;
  QString identity;
  QString display_identity;
  QString discovery_backend;
  QString last_discovery_backend;
  QString failure_reason;
  QString display_target_identity =
      QStringLiteral("reference-sdr-100nits");
  QString display_target_provenance =
      QStringLiteral(
          "explicit-reference-default");
  double peak_luminance_nits = 100.0;
  double black_luminance_nits = 0.005;
  double reference_white_nits = 100.0;
  double surround_luminance_nits = 5.0;
  bool display_target_verified = false;
  int consecutive_discovery_failures = 0;
  bool retained_after_failure = false;
  quint64 generation = 0U;

  [[nodiscard]] bool valid() const {
    return color_space.isValid() && !identity.isEmpty() &&
           !display_identity.isEmpty();
  }

  [[nodiscard]] bool display_target_valid() const {
    return !display_target_identity.isEmpty() &&
           std::isfinite(peak_luminance_nits) &&
           std::isfinite(black_luminance_nits) &&
           std::isfinite(reference_white_nits) &&
           std::isfinite(surround_luminance_nits) &&
           peak_luminance_nits > 0.0 &&
           black_luminance_nits >= 0.0 &&
           black_luminance_nits <
               peak_luminance_nits &&
           reference_white_nits > 0.0 &&
           reference_white_nits <=
               peak_luminance_nits &&
           surround_luminance_nits >= 0.0;
  }
};

struct SystemMonitorProfileRefreshResult final {
  SystemMonitorProfileSnapshot snapshot;
  bool profile_changed = false;
  bool metadata_changed = false;
};

struct DisplayTargetCalibration final {
  QString display_identity;
  QString calibration_identity;
  double peak_luminance_nits = 0.0;
  double black_luminance_nits = 0.0;
  double reference_white_nits = 0.0;
  double surround_luminance_nits = 0.0;

  [[nodiscard]] bool valid() const {
    return !display_identity.isEmpty() &&
           !calibration_identity.isEmpty() &&
           std::isfinite(peak_luminance_nits) &&
           std::isfinite(black_luminance_nits) &&
           std::isfinite(reference_white_nits) &&
           std::isfinite(surround_luminance_nits) &&
           peak_luminance_nits > 0.0 &&
           black_luminance_nits >= 0.0 &&
           black_luminance_nits <
               peak_luminance_nits &&
           reference_white_nits > 0.0 &&
           reference_white_nits <=
               peak_luminance_nits &&
           surround_luminance_nits >= 0.0;
  }
};

[[nodiscard]] inline SystemMonitorProfileSnapshot
apply_display_target_calibration(
    SystemMonitorProfileSnapshot snapshot,
    const DisplayTargetCalibration& calibration) {
  if (!snapshot.valid() || !calibration.valid() ||
      snapshot.display_identity !=
          calibration.display_identity) {
    return snapshot;
  }
  snapshot.peak_luminance_nits =
      calibration.peak_luminance_nits;
  snapshot.black_luminance_nits =
      calibration.black_luminance_nits;
  snapshot.reference_white_nits =
      calibration.reference_white_nits;
  snapshot.surround_luminance_nits =
      calibration.surround_luminance_nits;
  snapshot.display_target_verified = true;
  snapshot.display_target_provenance =
      QStringLiteral(
          "user-calibration");
  snapshot.display_target_identity =
      QStringLiteral(
          "%1|calibration:%2|peak:%3|black:%4|white:%5|surround:%6")
          .arg(
              snapshot.display_identity,
              calibration.calibration_identity)
          .arg(
              calibration.peak_luminance_nits,
              0,
              'g',
              12)
          .arg(
              calibration.black_luminance_nits,
              0,
              'g',
              12)
          .arg(
              calibration.reference_white_nits,
              0,
              'g',
              12)
          .arg(
              calibration.surround_luminance_nits,
              0,
              'g',
              12);
  return snapshot;
}

// Keeps the last verified profile across transient discovery failures on the
// same physical display. A display change never inherits the previous
// display's profile: it fails to the platform fallback until discovery works.
[[nodiscard]] inline SystemMonitorProfileRefreshResult
reconcile_system_monitor_profile(
    const SystemMonitorProfileSnapshot& previous,
    const SystemMonitorProfileSnapshot& observation) {
  const auto previous_valid = previous.valid();
  const auto observation_valid = observation.valid();
  const auto advanced_generation =
      previous.generation == std::numeric_limits<quint64>::max()
          ? previous.generation
          : previous.generation + 1U;
  auto result = SystemMonitorProfileRefreshResult{};
  if (observation_valid) {
    result.profile_changed =
        !previous_valid || previous.identity != observation.identity ||
        previous.display_identity != observation.display_identity ||
        previous.display_target_identity !=
            observation.display_target_identity;
    result.snapshot = observation;
    result.snapshot.last_discovery_backend = observation.discovery_backend;
    result.snapshot.failure_reason.clear();
    result.snapshot.consecutive_discovery_failures = 0;
    result.snapshot.retained_after_failure = false;
    result.snapshot.generation =
        result.profile_changed ? advanced_generation : previous.generation;
    result.metadata_changed =
        result.profile_changed || previous.consecutive_discovery_failures != 0 ||
        previous.retained_after_failure ||
        previous.discovery_backend != observation.discovery_backend ||
        previous.display_name != observation.display_name ||
        previous.display_target_provenance !=
            observation.display_target_provenance ||
        previous.display_target_verified !=
            observation.display_target_verified;
    return result;
  }

  result.snapshot = observation;
  result.snapshot.generation = previous.generation;
  const auto observation_is_same_display =
      !observation.display_identity.isEmpty() &&
      previous.display_identity == observation.display_identity;
  result.snapshot.consecutive_discovery_failures =
      !observation_is_same_display
          ? 1
          : (previous.consecutive_discovery_failures ==
                     std::numeric_limits<int>::max()
                 ? previous.consecutive_discovery_failures
                 : previous.consecutive_discovery_failures + 1);
  result.snapshot.last_discovery_backend = observation.discovery_backend;
  if (result.snapshot.failure_reason.isEmpty()) {
    result.snapshot.failure_reason = QStringLiteral("monitor-profile-unavailable");
  }
  const auto same_display =
      previous_valid && observation_is_same_display;
  if (same_display) {
    result.snapshot.color_space = previous.color_space;
    result.snapshot.display_name = previous.display_name;
    result.snapshot.identity = previous.identity;
    result.snapshot.display_identity = previous.display_identity;
    result.snapshot.discovery_backend = previous.discovery_backend;
    result.snapshot.display_target_identity =
        previous.display_target_identity;
    result.snapshot.display_target_provenance =
        previous.display_target_provenance;
    result.snapshot.peak_luminance_nits =
        previous.peak_luminance_nits;
    result.snapshot.black_luminance_nits =
        previous.black_luminance_nits;
    result.snapshot.reference_white_nits =
        previous.reference_white_nits;
    result.snapshot.surround_luminance_nits =
        previous.surround_luminance_nits;
    result.snapshot.display_target_verified =
        previous.display_target_verified;
    result.snapshot.retained_after_failure = true;
  } else {
    result.profile_changed = previous_valid;
    result.snapshot.retained_after_failure = false;
    result.snapshot.generation =
        result.profile_changed ? advanced_generation : previous.generation;
  }
  result.metadata_changed =
      result.profile_changed ||
      previous.consecutive_discovery_failures !=
          result.snapshot.consecutive_discovery_failures ||
      previous.failure_reason != result.snapshot.failure_reason ||
      previous.retained_after_failure != result.snapshot.retained_after_failure ||
      previous.last_discovery_backend !=
          result.snapshot.last_discovery_backend ||
      previous.display_identity != result.snapshot.display_identity;
  return result;
}

}  // namespace photo_editor::desktop
