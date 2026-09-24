#pragma once

#include <utility>

#include "histogram_scope_service.h"

namespace photo_editor::desktop {

inline constexpr auto kHistogramScopeSessionContract =
    "histogram-scope-session:v2|typed-result|atomic-publication|raw-clipping|domain-policy";

inline QString normalized_histogram_scope_domain(const QString& domain) {
  const auto normalized = domain.trimmed().toLower();
  if (normalized == QStringLiteral("scene-linear") ||
      normalized == QStringLiteral("working") ||
      normalized == QStringLiteral("pre-output") ||
      normalized == QStringLiteral("proof")) {
    return normalized;
  }
  return QStringLiteral("final-display");
}

inline QString histogram_scope_domain_label(const QString& domain) {
  const auto normalized = normalized_histogram_scope_domain(domain);
  if (normalized == QStringLiteral("scene-linear")) {
    return QStringLiteral("Scene Linear");
  }
  if (normalized == QStringLiteral("working")) {
    return QStringLiteral("Working");
  }
  if (normalized == QStringLiteral("pre-output")) {
    return QStringLiteral("Pre-Output");
  }
  if (normalized == QStringLiteral("proof")) {
    return QStringLiteral("Proof");
  }
  return QStringLiteral("Final Display");
}

class HistogramScopeSession final {
 public:
  void publish(const HistogramComputationResult& result) {
    published_ = result;
  }

  void updateRawClipping(
      int sample_count,
      int shadow_clip_count,
      int highlight_clip_count) {
    published_.raw_clip_sample_count = sample_count;
    published_.raw_shadow_clip_count = shadow_clip_count;
    published_.raw_highlight_clip_count = highlight_clip_count;
  }

  [[nodiscard]] const HistogramComputationResult& published() const {
    return published_;
  }

  [[nodiscard]] bool setVisible(bool visible) {
    if (visible_ == visible) {
      return false;
    }
    visible_ = visible;
    return true;
  }

  [[nodiscard]] bool setDomain(
      const QString& requested_domain,
      bool soft_proof_enabled) {
    auto normalized = normalized_histogram_scope_domain(requested_domain);
    if (normalized == QStringLiteral("proof") && !soft_proof_enabled) {
      normalized = QStringLiteral("final-display");
    }
    if (domain_ == normalized) {
      return false;
    }
    domain_ = std::move(normalized);
    return true;
  }

  [[nodiscard]] bool reconcileProofAvailability(bool soft_proof_enabled) {
    if (soft_proof_enabled || domain_ != QStringLiteral("proof")) {
      return false;
    }
    domain_ = QStringLiteral("final-display");
    return true;
  }

  [[nodiscard]] bool visible() const { return visible_; }
  [[nodiscard]] const QString& domain() const { return domain_; }
  [[nodiscard]] QString domainLabel() const {
    return histogram_scope_domain_label(domain_);
  }

  [[nodiscard]] QString contract() const {
    return QString::fromLatin1(kHistogramScopeSessionContract);
  }

 private:
  HistogramComputationResult published_;
  bool visible_ = true;
  QString domain_ = QStringLiteral("final-display");
};

}  // namespace photo_editor::desktop
