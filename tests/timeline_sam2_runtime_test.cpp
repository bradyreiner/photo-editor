#include <cstdlib>
#include <chrono>
#include <iostream>
#include <thread>

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>

#include "timeline_sam2_runtime.h"

using namespace photo_editor::desktop;

namespace {

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

QString prepare_frames(const QString& root) {
  const QString directory = QDir(root).filePath(QStringLiteral("frames"));
  if (!QDir().mkpath(directory)) {
    return {};
  }
  for (int index = 0; index < 2; ++index) {
    QImage frame(16, 12, QImage::Format_RGB32);
    frame.fill(QColor(30 + index * 20, 80, 110));
    if (!frame.save(QDir(directory).filePath(
                        QStringLiteral("%1.jpg").arg(index, 6, 10, QLatin1Char('0'))),
                    "JPEG")) {
      return {};
    }
  }
  return directory;
}

QString output_dir(const QString& root, const QString& leaf) {
  const QString path = QDir(root).filePath(leaf);
  return QDir().mkpath(path) ? path : QString{};
}

TimelineSam2Request request_for(const QString& frames, const QString& output) {
  return {frames, output, 2, QRectF(0.2, 0.15, 0.4, 0.6)};
}

TimelineSam2Options options_for(const QString& worker) {
  TimelineSam2Options options;
  options.worker_executable_override = worker;
  options.timeout_ms = 3000;
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temp;
  if (!temp.isValid()) {
    return fail(QStringLiteral("Could not create SAM2 runtime test directory"));
  }
  const QString frames = prepare_frames(temp.path());
  if (frames.isEmpty()) {
    return fail(QStringLiteral("Could not stage numbered JPEG source frames"));
  }

  const QString success_worker = QStringLiteral(PHOTO_EDITOR_SAM2_FAKE_SUCCESS);
  if (!timelineSam2Available(nullptr, options_for(success_worker))) {
    return fail(QStringLiteral("Injected fake worker executable was not accepted for protocol testing"));
  }
  int progress_updates = 0;
  auto success_options = options_for(success_worker);
  success_options.progress = [&progress_updates](int completed, int total) {
    if (completed > 0 && total == 2) {
      ++progress_updates;
    }
  };
  const QString success_output = output_dir(temp.path(), QStringLiteral("success"));
  auto corrected_request = request_for(frames, success_output);
  corrected_request.correction_prompts = QVariantList{QVariantMap{
      {QStringLiteral("frameIndex"), 1},
      {QStringLiteral("points"), QVariantList{
           QVariantMap{{QStringLiteral("x"), 0.36}, {QStringLiteral("y"), 0.40}, {QStringLiteral("label"), 1}},
           QVariantMap{{QStringLiteral("x"), 0.72}, {QStringLiteral("y"), 0.66}, {QStringLiteral("label"), 0}},
       }},
  }};
  const auto success = runTimelineSam2Tracking(
      corrected_request, success_options);
  if (success.status != TimelineSam2Status::Success || success.mask_paths.size() != 2 ||
      progress_updates != 2) {
    return fail(QStringLiteral("Fake worker success/progress protocol failed: %1").arg(success.message));
  }
  const QImage mask(success.mask_paths.first());
  if (mask.isNull() || qRed(mask.pixel(3, 2)) != 255 || qGreen(mask.pixel(3, 2)) != 255 ||
      qBlue(mask.pixel(3, 2)) != 255 || qAlpha(mask.pixel(3, 2)) == 0) {
    return fail(QStringLiteral("SAM2 output contract must keep the matte in white-image alpha"));
  }
  const QImage occluded_mask(success.mask_paths.at(1));
  if (occluded_mask.isNull() || qAlpha(occluded_mask.pixel(3, 2)) != 0) {
    return fail(QStringLiteral("An individually empty frame should be retained for full-shot occlusion and reappearance"));
  }

  const QString unavailable_output = output_dir(temp.path(), QStringLiteral("unavailable"));
  auto unavailable_options = options_for(QStringLiteral(PHOTO_EDITOR_SAM2_FAKE_UNAVAILABLE));
  const auto unavailable = runTimelineSam2Tracking(
      request_for(frames, unavailable_output), unavailable_options);
  if (unavailable.status != TimelineSam2Status::Unavailable || unavailable.message.isEmpty()) {
    return fail(QStringLiteral("Missing optional SAM2 imports must report Unavailable"));
  }

  const QString timeout_output = output_dir(temp.path(), QStringLiteral("timeout"));
  auto timeout_options = options_for(QStringLiteral(PHOTO_EDITOR_SAM2_FAKE_WAIT));
  timeout_options.timeout_ms = 120;
  const auto timeout = runTimelineSam2Tracking(
      request_for(frames, timeout_output), timeout_options);
  if (timeout.status != TimelineSam2Status::TimedOut) {
    return fail(QStringLiteral("Worker time limit was not enforced"));
  }

  const QString cancelled_output = output_dir(temp.path(), QStringLiteral("cancelled"));
  std::atomic_bool cancelled{false};
  auto cancel_options = options_for(QStringLiteral(PHOTO_EDITOR_SAM2_FAKE_WAIT));
  cancel_options.timeout_ms = 3000;
  cancel_options.cancelled = &cancelled;
  std::thread cancel_after_start([&cancelled] {
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    cancelled.store(true, std::memory_order_relaxed);
  });
  const auto cancelled_result = runTimelineSam2Tracking(
      request_for(frames, cancelled_output), cancel_options);
  cancel_after_start.join();
  if (cancelled_result.status != TimelineSam2Status::Cancelled) {
    return fail(QStringLiteral("Worker cancellation was not propagated"));
  }

  const QString empty_output = output_dir(temp.path(), QStringLiteral("all-empty"));
  qputenv("PHOTO_EDITOR_TEST_SAM2_EMPTY_ALL", "1");
  const auto all_empty = runTimelineSam2Tracking(
      request_for(frames, empty_output), options_for(success_worker));
  qunsetenv("PHOTO_EDITOR_TEST_SAM2_EMPTY_ALL");
  if (all_empty.status != TimelineSam2Status::OutputInvalid ||
      !all_empty.message.contains(QStringLiteral("entire source shot"))) {
    return fail(QStringLiteral("An all-empty sequence must be rejected even though empty individual frames are valid"));
  }

  auto invalid_request = request_for(frames, output_dir(temp.path(), QStringLiteral("invalid")));
  invalid_request.seed_box_top_left = QRectF(0.8, 0.1, 0.3, 0.2);
  if (runTimelineSam2Tracking(invalid_request, success_options).status != TimelineSam2Status::InvalidInput) {
    return fail(QStringLiteral("Out-of-frame seed boxes must be rejected"));
  }

  const auto invalid_corrections_status = [&](const QVariantList& corrections, int frame_count = 2) {
    auto request = request_for(frames, output_dir(temp.path(), QStringLiteral("invalid-corrections")));
    request.frame_count = frame_count;
    request.correction_prompts = corrections;
    return runTimelineSam2Tracking(request, success_options).status;
  };
  if (invalid_corrections_status(QVariantList{QVariantMap{
          {QStringLiteral("frameIndex"), 2},
          {QStringLiteral("points"), QVariantList{QVariantMap{{QStringLiteral("x"), 0.5},
                                                               {QStringLiteral("y"), 0.5},
                                                               {QStringLiteral("label"), 1}}}},
      }}) != TimelineSam2Status::InvalidInput) {
    return fail(QStringLiteral("Correction prompts outside the staged frame range must be rejected"));
  }
  const QVariantMap repeated_frame{
      {QStringLiteral("frameIndex"), 1},
      {QStringLiteral("points"), QVariantList{QVariantMap{{QStringLiteral("x"), 0.5},
                                                           {QStringLiteral("y"), 0.5},
                                                           {QStringLiteral("label"), 1}}}},
  };
  if (invalid_corrections_status(QVariantList{repeated_frame, repeated_frame}) !=
      TimelineSam2Status::InvalidInput) {
    return fail(QStringLiteral("Duplicate correction frames must be rejected"));
  }
  if (invalid_corrections_status(QVariantList{QVariantMap{
          {QStringLiteral("frameIndex"), 1},
          {QStringLiteral("points"), QVariantList{QVariantMap{{QStringLiteral("x"), 0.5},
                                                               {QStringLiteral("y"), 0.5},
                                                               {QStringLiteral("label"), 2}}}},
      }}) != TimelineSam2Status::InvalidInput) {
    return fail(QStringLiteral("Correction labels other than positive/negative must be rejected"));
  }
  QVariantList too_many_points;
  for (int index = 0; index < 257; ++index)
    too_many_points.append(QVariantMap{{QStringLiteral("x"), 0.5},
                                       {QStringLiteral("y"), 0.5},
                                       {QStringLiteral("label"), 1}});
  if (invalid_corrections_status(QVariantList{QVariantMap{
          {QStringLiteral("frameIndex"), 1},
          {QStringLiteral("points"), too_many_points},
      }}) != TimelineSam2Status::InvalidInput) {
    return fail(QStringLiteral("More than 256 correction points on one frame must be rejected"));
  }
  if (invalid_corrections_status(QVariantList{QVariantMap{
          {QStringLiteral("frameIndex"), 1},
          {QStringLiteral("points"), QVariantList{QVariantMap{{QStringLiteral("x"), 1.1},
                                                               {QStringLiteral("y"), 0.5},
                                                               {QStringLiteral("label"), 1}}}},
      }}) != TimelineSam2Status::InvalidInput) {
    return fail(QStringLiteral("Correction coordinates outside the normalized frame must be rejected"));
  }
  QVariantList too_many_frames;
  for (int index = 0; index < 65; ++index) {
    too_many_frames.append(QVariantMap{
        {QStringLiteral("frameIndex"), index},
        {QStringLiteral("points"), QVariantList{QVariantMap{{QStringLiteral("x"), 0.5},
                                                             {QStringLiteral("y"), 0.5},
                                                             {QStringLiteral("label"), 1}}}},
    });
  }
  if (invalid_corrections_status(too_many_frames, 180) != TimelineSam2Status::InvalidInput) {
    return fail(QStringLiteral("More than 64 prompted frames must be rejected before worker launch"));
  }
  return EXIT_SUCCESS;
}
