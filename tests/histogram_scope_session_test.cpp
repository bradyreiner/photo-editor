#include "histogram_scope_session.h"

#include <cstdlib>
#include <iostream>

#include <QCoreApplication>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::desktop::HistogramComputationResult result_for(int seed) {
  using photo_editor::desktop::HistogramComputationResult;
  auto value = [seed](int offset) { return QVariantList{seed + offset}; };
  return HistogramComputationResult{
      .red = value(1),
      .green = value(2),
      .blue = value(3),
      .luminance = value(4),
      .red_counts = value(5),
      .green_counts = value(6),
      .blue_counts = value(7),
      .luminance_counts = value(8),
      .waveform_red = value(9),
      .waveform_green = value(10),
      .waveform_blue = value(11),
      .waveform_luminance = value(12),
      .density_red = value(13),
      .density_green = value(14),
      .density_blue = value(15),
      .density_luminance = value(16),
      .vectorscope_points = value(17),
      .shadow_clip_count = seed + 18,
      .highlight_clip_count = seed + 19,
      .sample_count = seed + 20,
      .raw_clip_sample_count = seed + 21,
      .raw_shadow_clip_count = seed + 22,
      .raw_highlight_clip_count = seed + 23,
      .generation = seed + 24,
      .elapsed_ms = seed + 25,
      .full_resolution = seed % 2 == 0,
      .render_reason = QStringLiteral("reason-%1").arg(seed),
      .interaction_mode = QStringLiteral("mode-%1").arg(seed),
  };
}

bool same_result(
    const photo_editor::desktop::HistogramComputationResult& left,
    const photo_editor::desktop::HistogramComputationResult& right) {
  return left.red == right.red && left.green == right.green &&
         left.blue == right.blue && left.luminance == right.luminance &&
         left.red_counts == right.red_counts &&
         left.green_counts == right.green_counts &&
         left.blue_counts == right.blue_counts &&
         left.luminance_counts == right.luminance_counts &&
         left.waveform_red == right.waveform_red &&
         left.waveform_green == right.waveform_green &&
         left.waveform_blue == right.waveform_blue &&
         left.waveform_luminance == right.waveform_luminance &&
         left.density_red == right.density_red &&
         left.density_green == right.density_green &&
         left.density_blue == right.density_blue &&
         left.density_luminance == right.density_luminance &&
         left.vectorscope_points == right.vectorscope_points &&
         left.shadow_clip_count == right.shadow_clip_count &&
         left.highlight_clip_count == right.highlight_clip_count &&
         left.sample_count == right.sample_count &&
         left.raw_clip_sample_count == right.raw_clip_sample_count &&
         left.raw_shadow_clip_count == right.raw_shadow_clip_count &&
         left.raw_highlight_clip_count == right.raw_highlight_clip_count &&
         left.generation == right.generation &&
         left.elapsed_ms == right.elapsed_ms &&
         left.full_resolution == right.full_resolution &&
         left.render_reason == right.render_reason &&
         left.interaction_mode == right.interaction_mode;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using namespace photo_editor::desktop;

  HistogramScopeSession session;
  if (session.contract() != QString::fromLatin1(kHistogramScopeSessionContract)) {
    return fail("histogram scope session must publish its stable ownership contract");
  }
  if (!session.visible() || session.domain() != QStringLiteral("final-display") ||
      session.domainLabel() != QStringLiteral("Final Display") ||
      session.setVisible(true) || !session.setVisible(false) || session.visible() ||
      session.setVisible(false)) {
    return fail("scope visibility must default on and report only real state transitions");
  }
  if (!session.setDomain(QStringLiteral("  WORKING  "), false) ||
      session.domain() != QStringLiteral("working") ||
      session.domainLabel() != QStringLiteral("Working") ||
      session.setDomain(QStringLiteral("working"), false) ||
      !session.setDomain(QStringLiteral("proof"), false) ||
      session.domain() != QStringLiteral("final-display") ||
      !session.setDomain(QStringLiteral("proof"), true) ||
      session.domain() != QStringLiteral("proof") ||
      session.domainLabel() != QStringLiteral("Proof") ||
      session.reconcileProofAvailability(true) ||
      !session.reconcileProofAvailability(false) ||
      session.domain() != QStringLiteral("final-display") ||
      session.reconcileProofAvailability(false) ||
      session.setDomain(QStringLiteral("unknown-domain"), true) ||
      session.domain() != QStringLiteral("final-display")) {
    return fail("scope domain policy must normalize names and forbid Proof without soft proof");
  }

  auto first = result_for(100);
  const auto expected_first = first;
  session.publish(first);
  first.red[0] = -1;
  first.vectorscope_points.clear();
  first.sample_count = -1;
  if (!same_result(session.published(), expected_first)) {
    return fail("published scope state must be an immutable complete snapshot");
  }

  session.updateRawClipping(901, 902, 903);
  if (session.published().raw_clip_sample_count != 901 ||
      session.published().raw_shadow_clip_count != 902 ||
      session.published().raw_highlight_clip_count != 903 ||
      session.published().sample_count != expected_first.sample_count ||
      session.published().red != expected_first.red) {
    return fail("RAW clipping updates must not disturb the published histogram or scope arrays");
  }

  const auto second = result_for(200);
  session.publish(second);
  if (!same_result(session.published(), second)) {
    return fail("a new typed result must atomically replace every prior publication field");
  }

  return EXIT_SUCCESS;
}
