#include "preview_publisher.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool wait_for_count(
    const std::vector<photo_editor::desktop::PublishedPreviewViewport>& requests,
    std::size_t count,
    int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (requests.size() >= count) {
      return true;
    }
    QThread::msleep(5);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return requests.size() >= count;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using photo_editor::desktop::PreviewPublisher;
  using photo_editor::desktop::PreviewViewportPresentation;
  using photo_editor::desktop::PublishedPreviewViewport;

  std::vector<PublishedPreviewViewport> published;
  PreviewPublisher publisher(
      &app,
      [&published](const PublishedPreviewViewport& request) {
        published.push_back(request);
      });

  auto presentation = PreviewViewportPresentation{
      .normalized_x = 0.25,
      .normalized_y = 0.125,
      .normalized_width = 0.5,
      .normalized_height = 0.5,
      .visible_source_pixel_width = 512.0,
      .visible_source_pixel_height = 384.0,
      .visible_display_width = 64.0,
      .visible_display_height = 48.0,
      .device_pixel_ratio = 1.0,
      .active = true};
  publisher.schedule(presentation, true, false);
  if (!wait_for_count(published, 1, 250) ||
      published.back().normalized_rect != QRectF(0.25, 0.125, 0.5, 0.5) ||
      published.back().patch_max_edge != 64 ||
      !published.back().active) {
    return fail("immediate publication should normalize geometry and own patch sizing");
  }

  publisher.schedule(presentation, true, false);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  if (published.size() != 1) {
    return fail("the publisher should suppress an unchanged viewport request");
  }
  publisher.schedule(presentation, true, true);
  if (!wait_for_count(published, 2, 250)) {
    return fail("forced publication should retry an unchanged viewport request");
  }

  presentation.normalized_x = 0.3;
  publisher.schedule(presentation, false, false);
  presentation.normalized_x = 0.4;
  publisher.schedule(presentation, false, false);
  if (!wait_for_count(published, 3, 500) ||
      published.back().normalized_rect.x() != 0.4) {
    return fail("settled publication should coalesce to the newest presentation facts");
  }

  auto mask_presentation = presentation;
  mask_presentation.visible_display_width = 220.0;
  mask_presentation.visible_display_height = 180.0;
  mask_presentation.mask_work_active = true;
  if (PreviewPublisher::patchEdgeForPresentation(mask_presentation) != 1536 ||
      PreviewPublisher::settleDelayMs(false) != 160 ||
      PreviewPublisher::settleDelayMs(true) != 0) {
    return fail("the publisher should own settled-mask quality and settle timing");
  }

  mask_presentation.pixel_inspection_active = true;
  if (PreviewPublisher::patchEdgeForPresentation(mask_presentation) != 512) {
    return fail("pixel inspection should use visible source-pixel demand without the mask floor");
  }

  publisher.schedule(PreviewViewportPresentation{}, true, false);
  if (!wait_for_count(published, 4, 250) || published.back().active ||
      published.back().patch_max_edge != 0 ||
      published.back().normalized_rect.isValid()) {
    return fail("inactive presentation should publish one canonical cleared viewport");
  }

  return EXIT_SUCCESS;
}
