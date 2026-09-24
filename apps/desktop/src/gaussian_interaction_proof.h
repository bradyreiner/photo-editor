#pragma once
#include <QVariantMap>

namespace photo_editor::desktop {
// This gates UI-thread timer responsiveness, not input-to-photon latency.
// Inactive/locked desktop runs can prove exports but cannot qualify interaction.
inline QVariantMap gaussian_interaction_proof(const QVariantList& samples) {
  bool complete = samples.size() == 60;
  bool foreground = complete;
  qint64 previous = 0, maximum = 0;
  for (const auto& value : samples) {
    const auto sample = value.toMap();
    const auto elapsed = sample.value("elapsedMs").toLongLong();
    const auto gap = sample.value("gapMs").toLongLong();
    complete = complete && elapsed > previous && gap == elapsed - previous;
    foreground = foreground && sample.value("windowActive").toBool() &&
        sample.value("windowExposed").toBool() &&
        sample.value("applicationState").toInt() == static_cast<int>(Qt::ApplicationActive);
    maximum = qMax(maximum, gap);
    previous = elapsed;
  }
  const bool eligible = complete && foreground;
  const bool passed = eligible && maximum <= 50 && previous <= 1500;
  return {{"scope", "main-thread-navigation-timer-only"}, {"eligible", eligible},
      {"passed", passed}, {"state", !complete ? "incomplete" : !foreground ? "unverified-window-inactive" :
          passed ? "within-budget" : "over-budget"},
      {"maxGapBudgetMs", 50}, {"totalBudgetMs", 1500},
      {"maximumGapMs", maximum}, {"elapsedMs", previous}, {"sampleCount", samples.size()}};
}
}  // namespace photo_editor::desktop
