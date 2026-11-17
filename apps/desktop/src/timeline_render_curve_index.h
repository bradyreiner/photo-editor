#pragma once

#include <QString>
#include <QVector>

#include <optional>

namespace photo_editor::desktop::timeline_motion::render_curve {

// This data shape is intentionally independent of TimelineDocumentController::ClipKeyframe.
// An export can copy the channels it needs into an immutable index without exposing
// the editor's private model or changing project serialization.
enum class Interpolation : unsigned char {
  Linear,
  Hold,
  EaseIn,
  EaseOut,
  EaseInOut,
  Bezier,
};

struct Keyframe final {
  double local_seconds = 0.0;
  double value = 0.0;
  Interpolation interpolation = Interpolation::Linear;
  bool bezier_out_custom = false;
  double bezier_out_time_seconds = 0.0;
  double bezier_out_value = 0.0;
  bool bezier_in_custom = false;
  double bezier_in_time_seconds = 0.0;
  double bezier_in_value = 0.0;
};

struct Window final {
  qsizetype first = 0;
  qsizetype end = 0;  // Exclusive.
};

// A sorted, immutable render-local copy of one animated scalar channel.
class ChannelIndex final {
 public:
  [[nodiscard]] static std::optional<ChannelIndex> create(
      QVector<Keyframe> keyframes,
      double base_value,
      QString* error_message = nullptr);

  [[nodiscard]] qsizetype keyframeCount() const { return keyframes_.size(); }
  [[nodiscard]] const QVector<Keyframe>& keyframes() const { return keyframes_; }

  // The returned window includes a predecessor at/before start and a successor
  // at/after end, so interpolation and constant endpoint behavior remain exact
  // throughout [start_seconds, end_seconds).
  [[nodiscard]] std::optional<Window> windowForRange(
      double start_seconds,
      double end_seconds,
      QString* error_message = nullptr) const;

  [[nodiscard]] QString ffmpegExpression(
      const QString& time_expression,
      QString* error_message = nullptr) const;
  [[nodiscard]] QString ffmpegExpressionForRange(
      double start_seconds,
      double end_seconds,
      const QString& time_expression,
      QString* error_message = nullptr) const;

  // C++ evaluator used by differential tests and callers that need per-frame
  // values. It follows the same interpolation and endpoint rules as the legacy
  // controller evaluator.
  [[nodiscard]] double evaluate(double local_seconds) const;
  [[nodiscard]] double evaluate(
      const Window& window,
      double local_seconds) const;

 private:
  QVector<Keyframe> keyframes_;
  double base_value_ = 0.0;
};

[[nodiscard]] std::optional<Interpolation> interpolationFromString(
    const QString& value);

}  // namespace photo_editor::desktop::timeline_motion::render_curve
