#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

#include <QObject>
#include <QRectF>
#include <QTimer>

namespace photo_editor::desktop {

struct PreviewViewportPresentation final {
  double normalized_x = 0.0;
  double normalized_y = 0.0;
  double normalized_width = 0.0;
  double normalized_height = 0.0;
  double visible_source_pixel_width = 0.0;
  double visible_source_pixel_height = 0.0;
  double visible_display_width = 0.0;
  double visible_display_height = 0.0;
  double device_pixel_ratio = 1.0;
  bool mask_work_active = false;
  bool pixel_inspection_active = false;
  bool active = false;
};

struct PublishedPreviewViewport final {
  QRectF normalized_rect;
  int patch_max_edge = 0;
  bool active = false;

  [[nodiscard]] bool operator==(
      const PublishedPreviewViewport&) const = default;
};

class PreviewPublisher final {
 public:
  using PublishCallback =
      std::function<void(const PublishedPreviewViewport&)>;

  PreviewPublisher(QObject* context, PublishCallback publish)
      : context_(context), publish_(std::move(publish)) {
    settle_timer_.setSingleShot(true);
    QObject::connect(
        &settle_timer_,
        &QTimer::timeout,
        context_,
        [this]() { publishPending(); });
  }

  void schedule(
      const PreviewViewportPresentation& presentation,
      bool immediate,
      bool force) {
    pending_ = preparedViewport(presentation);
    if (force) {
      last_published_.reset();
    }
    if (immediate) {
      settle_timer_.stop();
      if (immediate_publish_queued_) {
        return;
      }
      immediate_publish_queued_ = true;
      QTimer::singleShot(0, context_, [this]() {
        immediate_publish_queued_ = false;
        publishPending();
      });
      return;
    }
    settle_timer_.start(settleDelayMs(presentation.pixel_inspection_active));
  }

  [[nodiscard]] static int settleDelayMs(bool pixel_inspection_active) {
    return pixel_inspection_active ? 0 : 160;
  }

  [[nodiscard]] static int patchEdgeForPresentation(
      const PreviewViewportPresentation& presentation) {
    const auto finite = [](double value, double fallback) {
      return std::isfinite(value) ? value : fallback;
    };
    const auto source_edge = std::max(
        1.0,
        std::max(
            finite(presentation.visible_source_pixel_width, 1.0),
            finite(presentation.visible_source_pixel_height, 1.0)));
    const auto display_edge = std::max(
        1.0,
        std::max(
            finite(presentation.visible_display_width, 1.0),
            finite(presentation.visible_display_height, 1.0)));
    const auto device_scale =
        std::max(1.0, finite(presentation.device_pixel_ratio, 1.0));
    const auto settled_mask =
        presentation.mask_work_active &&
        !presentation.pixel_inspection_active;
    const auto display_request =
        display_edge * device_scale * (settled_mask ? 1.5 : 1.0);
    const auto floor_request = settled_mask ? 1536.0 : 1.0;
    const auto inspection_request =
        presentation.pixel_inspection_active ? source_edge : 1.0;
    const auto requested = std::clamp(
        std::max(
            floor_request,
            std::max(display_request, inspection_request)),
        0.0,
        static_cast<double>(std::numeric_limits<int>::max()));
    return static_cast<int>(std::lround(requested));
  }

 private:
  [[nodiscard]] static double roundedUnit(double value) {
    const auto finite_value = std::isfinite(value) ? value : 0.0;
    return std::lround(std::clamp(finite_value, 0.0, 1.0) * 1'000'000.0) /
           1'000'000.0;
  }

  [[nodiscard]] static PublishedPreviewViewport preparedViewport(
      const PreviewViewportPresentation& presentation) {
    const auto width = roundedUnit(presentation.normalized_width);
    const auto height = roundedUnit(presentation.normalized_height);
    const auto active = presentation.active && width > 0.0 && height > 0.0;
    if (!active) {
      return {};
    }
    const auto x = std::min(
        roundedUnit(presentation.normalized_x),
        1.0 - width);
    const auto y = std::min(
        roundedUnit(presentation.normalized_y),
        1.0 - height);
    return PublishedPreviewViewport{
        .normalized_rect = QRectF(x, y, width, height),
        .patch_max_edge = patchEdgeForPresentation(presentation),
        .active = true};
  }

  void publishPending() {
    if (!pending_.has_value()) {
      return;
    }
    const auto request = *pending_;
    pending_.reset();
    if (last_published_.has_value() && *last_published_ == request) {
      return;
    }
    last_published_ = request;
    publish_(request);
  }

  QObject* context_ = nullptr;
  PublishCallback publish_;
  QTimer settle_timer_;
  std::optional<PublishedPreviewViewport> pending_;
  std::optional<PublishedPreviewViewport> last_published_;
  bool immediate_publish_queued_ = false;
};

}  // namespace photo_editor::desktop
