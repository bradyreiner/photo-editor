#include "display_profile_discovery.h"
#include "display_profile_service.h"
#include "system_monitor_profile_state.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::desktop::SystemMonitorProfileSnapshot valid_profile(
    const QString& display_identity,
    const QString& profile_identity) {
  return {
      .color_space = QColorSpace(QColorSpace::SRgb),
      .display_name = QStringLiteral("Test display profile"),
      .identity = profile_identity,
      .display_identity = display_identity,
      .discovery_backend = QStringLiteral("test-discovery"),
      .last_discovery_backend = QStringLiteral("test-discovery"),
  };
}

photo_editor::desktop::SystemMonitorProfileSnapshot failed_observation(
    const QString& display_identity,
    const QString& reason) {
  return {
      .display_identity = display_identity,
      .discovery_backend = QStringLiteral("test-discovery"),
      .last_discovery_backend = QStringLiteral("test-discovery"),
      .failure_reason = reason,
  };
}

}  // namespace

int main() {
  using photo_editor::desktop::SystemMonitorProfileSnapshot;
  using photo_editor::desktop::DisplayTargetCalibration;
  using photo_editor::desktop::DisplayProfileService;
  using photo_editor::desktop::apply_display_target_calibration;
  using photo_editor::desktop::reconcile_system_monitor_profile;

  const auto empty_discovery =
      photo_editor::desktop::display_profile_discovery::fromPayload(
          {},
          QStringLiteral("Test display"),
          QStringLiteral("display-payload"),
          QStringLiteral("test-payload"));
  if (empty_discovery.valid() ||
      empty_discovery.failure_reason !=
          QStringLiteral("monitor-profile-payload-empty")) {
    return fail("display profile discovery must reject an empty ICC payload");
  }

  const auto invalid_discovery =
      photo_editor::desktop::display_profile_discovery::fromPayload(
          QByteArrayLiteral("not-an-icc-profile"),
          QStringLiteral("Test display"),
          QStringLiteral("display-payload"),
          QStringLiteral("test-payload"));
  if (invalid_discovery.valid() ||
      invalid_discovery.failure_reason !=
          QStringLiteral("monitor-profile-payload-invalid")) {
    return fail("display profile discovery must reject an invalid ICC payload");
  }

  const auto srgb_payload = QColorSpace(QColorSpace::SRgb).iccProfile();
  const auto valid_discovery =
      photo_editor::desktop::display_profile_discovery::fromPayload(
          srgb_payload,
          QStringLiteral("Test display"),
          QStringLiteral("display-payload"),
          QStringLiteral("test-payload"));
  if (!valid_discovery.valid() ||
      valid_discovery.display_identity != QStringLiteral("display-payload") ||
      valid_discovery.discovery_backend != QStringLiteral("test-payload") ||
      !valid_discovery.identity.contains(QStringLiteral("icc-sha256:"))) {
    return fail("display profile discovery must identity-bind a valid ICC payload");
  }

  DisplayProfileService service;
  const auto service_first = service.refresh(
      valid_profile(QStringLiteral("service-display"), QStringLiteral("service-profile")));
  if (!service_first.profile_changed ||
      service.cacheIdentity() != QStringLiteral("service-profile") ||
      service.snapshot().generation != 1U) {
    return fail("display profile service should own the current verified profile generation");
  }
  const auto service_failure = service.refresh(
      failed_observation(
          QStringLiteral("service-display"),
          QStringLiteral("temporary-service-failure")));
  if (service_failure.profile_changed ||
      !service_failure.snapshot.retained_after_failure ||
      service.snapshot().identity != QStringLiteral("service-profile")) {
    return fail("display profile service should retain same-display state across a transient failure");
  }

  const auto first = reconcile_system_monitor_profile(
      {},
      valid_profile(QStringLiteral("display-a"), QStringLiteral("profile-a")));
  if (!first.profile_changed || !first.snapshot.valid() ||
      !first.snapshot.display_target_valid() ||
      first.snapshot.display_target_verified ||
      first.snapshot.generation != 1U ||
      first.snapshot.consecutive_discovery_failures != 0 ||
      first.snapshot.retained_after_failure) {
    return fail("the first verified monitor profile should become generation one");
  }

  const auto unchanged = reconcile_system_monitor_profile(
      first.snapshot,
      valid_profile(QStringLiteral("display-a"), QStringLiteral("profile-a")));
  if (unchanged.profile_changed || unchanged.snapshot.generation != 1U ||
      unchanged.metadata_changed) {
    return fail("an unchanged monitor profile should not invalidate display caches");
  }

  const auto transient_failure = reconcile_system_monitor_profile(
      unchanged.snapshot,
      failed_observation(
          QStringLiteral("display-a"),
          QStringLiteral("temporary-query-failure")));
  if (transient_failure.profile_changed || !transient_failure.metadata_changed ||
      !transient_failure.snapshot.valid() ||
      transient_failure.snapshot.identity != QStringLiteral("profile-a") ||
      transient_failure.snapshot.generation != 1U ||
      transient_failure.snapshot.consecutive_discovery_failures != 1 ||
      !transient_failure.snapshot.retained_after_failure ||
      transient_failure.snapshot.failure_reason !=
          QStringLiteral("temporary-query-failure")) {
    return fail("a transient same-display failure should retain the last verified profile");
  }

  const auto repeated_failure = reconcile_system_monitor_profile(
      transient_failure.snapshot,
      failed_observation(
          QStringLiteral("display-a"),
          QStringLiteral("temporary-query-failure")));
  if (repeated_failure.profile_changed ||
      repeated_failure.snapshot.consecutive_discovery_failures != 2 ||
      repeated_failure.snapshot.generation != 1U) {
    return fail("repeated failures should remain observable without cache churn");
  }

  const auto recovered = reconcile_system_monitor_profile(
      repeated_failure.snapshot,
      valid_profile(QStringLiteral("display-a"), QStringLiteral("profile-a")));
  if (recovered.profile_changed || !recovered.metadata_changed ||
      recovered.snapshot.consecutive_discovery_failures != 0 ||
      recovered.snapshot.retained_after_failure ||
      !recovered.snapshot.failure_reason.isEmpty() ||
      recovered.snapshot.generation != 1U) {
    return fail("same-profile recovery should clear failure state without invalidating pixels");
  }

  const auto changed_profile = reconcile_system_monitor_profile(
      recovered.snapshot,
      valid_profile(QStringLiteral("display-a"), QStringLiteral("profile-b")));
  if (!changed_profile.profile_changed ||
      changed_profile.snapshot.identity != QStringLiteral("profile-b") ||
      changed_profile.snapshot.generation != 2U) {
    return fail("a changed ICC payload should advance generation and invalidate display pixels");
  }

  const auto different_display_failure = reconcile_system_monitor_profile(
      changed_profile.snapshot,
      failed_observation(
          QStringLiteral("display-b"),
          QStringLiteral("new-display-profile-unavailable")));
  if (!different_display_failure.profile_changed ||
      different_display_failure.snapshot.valid() ||
      different_display_failure.snapshot.retained_after_failure ||
      different_display_failure.snapshot.consecutive_discovery_failures != 1 ||
      different_display_failure.snapshot.display_identity !=
          QStringLiteral("display-b") ||
      different_display_failure.snapshot.generation != 3U) {
    return fail("a new display must never inherit the previous display's profile");
  }

  const auto new_display_profile = reconcile_system_monitor_profile(
      different_display_failure.snapshot,
      valid_profile(QStringLiteral("display-b"), QStringLiteral("profile-c")));
  if (!new_display_profile.profile_changed ||
      new_display_profile.snapshot.generation != 4U ||
      new_display_profile.snapshot.identity != QStringLiteral("profile-c")) {
    return fail("a verified profile on the new display should become authoritative");
  }

  const auto reassigned_profile =
      reconcile_system_monitor_profile(
          new_display_profile.snapshot,
          valid_profile(
              QStringLiteral("display-b"),
              QStringLiteral(
                  "profile-c-reassigned")));
  if (!reassigned_profile.profile_changed ||
      reassigned_profile.snapshot
              .display_identity !=
          QStringLiteral("display-b") ||
      reassigned_profile.snapshot.identity !=
          QStringLiteral(
              "profile-c-reassigned") ||
      reassigned_profile.snapshot.generation !=
          5U) {
    return fail(
        "profile reassignment on one physical display should invalidate exactly that display generation");
  }

  const auto hotplug_return_to_a =
      reconcile_system_monitor_profile(
          reassigned_profile.snapshot,
          valid_profile(
              QStringLiteral("display-a"),
              QStringLiteral(
                  "profile-a-hotplugged")));
  if (!hotplug_return_to_a.profile_changed ||
      hotplug_return_to_a.snapshot
              .display_identity !=
          QStringLiteral("display-a") ||
      hotplug_return_to_a.snapshot.identity !=
          QStringLiteral(
              "profile-a-hotplugged") ||
      hotplug_return_to_a.snapshot.generation !=
          6U ||
      hotplug_return_to_a.snapshot
          .retained_after_failure) {
    return fail(
        "hot-plugging back to a prior display should resolve fresh profile state without publishing the other display");
  }

  const auto unplugged_without_display =
      reconcile_system_monitor_profile(
          hotplug_return_to_a.snapshot,
          failed_observation(
              {},
              QStringLiteral(
                  "display-topology-empty")));
  if (!unplugged_without_display
           .profile_changed ||
      unplugged_without_display.snapshot.valid() ||
      unplugged_without_display.snapshot
          .retained_after_failure ||
      unplugged_without_display.snapshot
              .generation !=
          7U) {
    return fail(
        "removing the active display without a replacement must fail closed instead of retaining a stale frame target");
  }

  const auto replacement_display =
      reconcile_system_monitor_profile(
          unplugged_without_display.snapshot,
          valid_profile(
              QStringLiteral("display-d"),
              QStringLiteral("profile-d")));
  if (!replacement_display.profile_changed ||
      replacement_display.snapshot.generation !=
          8U ||
      replacement_display.snapshot
              .display_identity !=
          QStringLiteral("display-d")) {
    return fail(
        "a replacement display after hot-plug removal should establish a new authoritative generation");
  }

  auto measured_target =
      valid_profile(
          QStringLiteral("display-c"),
          QStringLiteral("profile-measured"));
  measured_target.display_target_identity =
      QStringLiteral(
          "display-c|measured-target:v1");
  measured_target.display_target_provenance =
      QStringLiteral("platform-calibration");
  measured_target.peak_luminance_nits =
      600.0;
  measured_target.black_luminance_nits =
      0.001;
  measured_target.reference_white_nits =
      203.0;
  measured_target.surround_luminance_nits =
      10.0;
  measured_target.display_target_verified =
      true;
  const auto reference_target =
      reconcile_system_monitor_profile(
          {},
          valid_profile(
              QStringLiteral("display-c"),
              QStringLiteral(
                  "profile-measured")));
  const auto target_changed =
      reconcile_system_monitor_profile(
          reference_target.snapshot,
          measured_target);
  if (!target_changed.profile_changed ||
      !target_changed.snapshot
           .display_target_valid() ||
      !target_changed.snapshot
           .display_target_verified ||
      target_changed.snapshot.generation !=
          2U ||
      target_changed.snapshot
              .peak_luminance_nits !=
          600.0) {
    return fail(
        "a verified mastering-target change should invalidate display pixels even when ICC bytes are unchanged");
  }
  const auto target_query_failure =
      reconcile_system_monitor_profile(
          target_changed.snapshot,
          failed_observation(
              QStringLiteral("display-c"),
              QStringLiteral(
                  "temporary-target-query-failure")));
  if (target_query_failure.profile_changed ||
      !target_query_failure.snapshot
           .display_target_verified ||
      target_query_failure.snapshot
              .display_target_identity !=
          measured_target
              .display_target_identity ||
      target_query_failure.snapshot
              .reference_white_nits !=
          203.0) {
    return fail(
        "a transient same-display query failure should retain the last verified mastering target");
  }

  const auto rejected_calibration =
      apply_display_target_calibration(
          valid_profile(
              QStringLiteral("display-a"),
              QStringLiteral("profile-a")),
          DisplayTargetCalibration{
              .display_identity =
                  QStringLiteral("display-b"),
              .calibration_identity =
                  QStringLiteral("meter-run-1"),
              .peak_luminance_nits = 500.0,
              .black_luminance_nits = 0.002,
              .reference_white_nits = 160.0,
              .surround_luminance_nits = 8.0,
          });
  if (rejected_calibration
          .display_target_verified) {
    return fail(
        "calibration for another physical display must not be applied");
  }
  const auto calibrated =
      apply_display_target_calibration(
          valid_profile(
              QStringLiteral("display-a"),
              QStringLiteral("profile-a")),
          DisplayTargetCalibration{
              .display_identity =
                  QStringLiteral("display-a"),
              .calibration_identity =
                  QStringLiteral("meter-run-1"),
              .peak_luminance_nits = 500.0,
              .black_luminance_nits = 0.002,
              .reference_white_nits = 160.0,
              .surround_luminance_nits = 8.0,
          });
  if (!calibrated.display_target_verified ||
      !calibrated.display_target_valid() ||
      calibrated.display_target_provenance !=
          QStringLiteral("user-calibration") ||
      !calibrated.display_target_identity
           .contains(
               QStringLiteral(
                   "meter-run-1")) ||
      calibrated.reference_white_nits !=
          160.0) {
    return fail(
        "a complete calibration for the active display should produce an identity-bearing verified target");
  }

  return EXIT_SUCCESS;
}
