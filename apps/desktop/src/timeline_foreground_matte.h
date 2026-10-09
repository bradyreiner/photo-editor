#pragma once

#include <QImage>
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {

struct TimelineForegroundMatteState final {
  bool enabled = false;
  bool ready = false;
  QString directory;
  QString source_signature;
  QString error_message;
  double fps = 0.0;
  double trim_start_seconds = 0.0;
  double trim_end_seconds = 0.0;
  int frame_count = 0;
};

// A split clip may use any subrange of the matte's analyzed source range.
[[nodiscard]] bool timelineForegroundCutoutReady(
    const QVariantMap& metadata,
    const QString& source_signature,
    double clip_trim_start_seconds,
    double clip_trim_end_seconds);

[[nodiscard]] QString timelineForegroundMaskPath(
    const QVariantMap& metadata,
    const QString& source_signature,
    double source_seconds,
    QString* error_message = nullptr);

// The generated PNG stores the matte in its alpha channel. The operation
// multiplies that channel into the source image's alpha.
[[nodiscard]] bool applyTimelineForegroundMask(
    QImage* source,
    const QString& mask_path,
    QString* error_message = nullptr);

// The clip map uses foregroundCutout metadata and its source identity fields.
[[nodiscard]] TimelineForegroundMatteState evaluate_timeline_foreground_matte(
    const QVariantMap& clip);

// Returns an empty path and a reason when the enabled matte does not cover this
// source timestamp. Callers must treat that as an error, never as an unmasked
// frame.
[[nodiscard]] QString timeline_foreground_matte_frame_path(
    const TimelineForegroundMatteState& state,
    double source_seconds,
    QString* error_message = nullptr);

// Multiplies the source image alpha by a grayscale matte, preserving any
// existing source alpha and using smooth scaling when analysis and playback
// frame sizes differ.
[[nodiscard]] QImage apply_timeline_foreground_matte(
    const QImage& source,
    const QString& mask_path,
    QString* error_message = nullptr);

}  // namespace photo_editor::desktop
