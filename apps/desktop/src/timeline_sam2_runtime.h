#pragma once

#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <atomic>
#include <functional>

namespace photo_editor::desktop {

enum class TimelineSam2Status {
  Success,
  Unavailable,
  InvalidInput,
  Cancelled,
  TimedOut,
  Failed,
  OutputInvalid,
};

struct TimelineSam2Request final {
  // Input files use numeric %06d.jpg numbering, beginning at 000000.jpg.
  QString frames_directory;
  QString output_directory;
  // SAM2 retains sizeable temporal state per frame; this backend is capped
  // separately from the lighter-weight native Vision runtime.
  int frame_count = 0;  // Supported range: 1..180.
  // Normalized top-left coordinates in the staged analysis frames.
  QRectF seed_box_top_left;
  // Optional correction frames: {frameIndex, points:[{x,y,label}]}; coordinates
  // are normalized top-left, label 1 is positive and label 0 is negative.
  QVariantList correction_prompts = {};
};

struct TimelineSam2Options final {
  int timeout_ms = 30 * 60 * 1000;
  qsizetype maximum_output_bytes = 256 * 1024 * 1024;
  const std::atomic_bool* cancelled = nullptr;
  std::function<void(int completed_frames, int total_frames)> progress;
  QString python_executable;
  QString checkpoint_path;
  QString config_path;
  QString device;
  // Test-only injection point. Production loads the worker from its Qt resource.
  QString worker_executable_override;
};

struct TimelineSam2Result final {
  TimelineSam2Status status = TimelineSam2Status::Failed;
  QString output_directory;
  QStringList mask_paths;
  QString message;
};

// Checks configuration and local files only. It does not import Python packages
// or install/download anything; the worker reports missing runtime dependencies.
[[nodiscard]] bool timelineSam2Available(
    QString* unavailable_reason = nullptr,
    const TimelineSam2Options& options = {});

// Runs synchronously and must be called from a worker thread. The worker accepts
// numbered JPEGs and returns source-frame-indexed white-RGB PNGs with mattes in alpha.
[[nodiscard]] TimelineSam2Result runTimelineSam2Tracking(
    const TimelineSam2Request& request,
    const TimelineSam2Options& options = {});

}  // namespace photo_editor::desktop
