#include "brush_input_schema.h"
#include "brush_live_preview_raster.h"
#include "brush_matte_rebuild.h"
#include "brush_stroke_sampling.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}
QVariantMap blank(QSize size) {
  return {{"kind", "brush"}, {"segmentationWidth", size.width()},
      {"segmentationHeight", size.height()},
      {"segmentationData", QByteArray(size.width() * size.height(), '\0')}};
}
int value(const QByteArray& alpha, QSize size, int x, int y) {
  return static_cast<unsigned char>(alpha.at(y * size.width() + x));
}
double tip(double distance, double hardness, double feather, double radius = 24.0) {
  const auto inner = hardness * (1.0 - feather);
  if ((1.0 - inner) * radius < 1.0) {
    const auto t = std::clamp((distance * radius - inner * radius + 0.5) /
        ((1.0 - inner) * radius + 1.0), 0.0, 1.0);
    return 1.0 - t * t * (3.0 - 2.0 * t);
  }
  if (distance >= 1.0) return 0.0;
  if (distance <= inner) return 1.0;
  const auto t = (distance - inner) / (1.0 - inner);
  return 1.0 - t * t * (3.0 - 2.0 * t);
}
}  // namespace

int main() {
  using namespace photo_editor::desktop;
  // Independent analytical oracle: a circular tip, including completely hard
  // and soft extremes, must have the same alpha in either pixel direction.
  for (const auto size : {QSize(801, 401), QSize(401, 801), QSize(401, 401)}) {
    for (const double hardness : {0.0, 0.45, 1.0}) {
      for (const double feather : {0.0, 0.55, 1.0}) {
        for (const double flow : {0.35, 1.0}) {
          auto mask = blank(size);
          const BrushPreviewPoint point{.x = 0.5, .y = 0.5, .size = 0.2, .flow = flow};
          (void)rasterize_brush_point_into_mask(&mask, nullptr, point.x, point.y,
              point.size, flow, 1.0, feather, false, false, 0.18, -1, -1, false,
              {}, nullptr, nullptr, nullptr, {.circular = true, .hardness = hardness});
          const auto alpha = mask.value("segmentationData").toByteArray();
          const auto radius = 0.2 * std::min(size.width() - 1, size.height() - 1);
          const auto cx = (size.width() - 1) / 2;
          const auto cy = (size.height() - 1) / 2;
          for (int y = cy - 90; y <= cy + 90; y += 3)
            for (int x = cx - 90; x <= cx + 90; x += 3) {
              const auto expected = std::lround(255 * flow * tip(std::hypot(x - cx, y - cy) / radius, hardness, feather, radius));
              if (value(alpha, size, x, y) != expected)
                return fail("Circular brush alpha disagrees with the independent feather/hardness oracle.");
            }
          BrushLivePreviewRaster live;
          const BrushPreviewStyle style{.feather = feather, .hardness = hardness,
              .opacity = 0.38, .circular = true};
          live.append(size, {point}, style);
          if (live.alpha != alpha) return fail("Live coverage must equal saved coverage for every tip profile.");
          live.append(size, {point}, style);
          if (live.alpha != alpha) return fail("Overlapping mask dabs must not inflate soft-edge alpha.");
          (void)rasterize_brush_point_into_mask(&mask, nullptr, point.x, point.y,
              point.size, flow, 1.0, feather, true, false, 0.18, -1, -1, false,
              {}, nullptr, nullptr, nullptr, {.circular = true, .hardness = hardness});
          const auto erased = mask.value("segmentationData").toByteArray();
          for (int y = cy - 90; y <= cy + 90; y += 5)
            for (int x = cx - 90; x <= cx + 90; x += 5) {
              const auto weight = flow * tip(std::hypot(x - cx, y - cy) / radius, hardness, feather, radius);
              if (value(erased, size, x, y) != std::lround(value(alpha, size, x, y) * (1 - weight)))
                return fail("Erase must subtract the same feathered footprint.");
            }
        }
      }
    }
  }

  // A fast move with smoothing/pressure must retain the cursor endpoint and
  // cover the tail continuously, rather than skipping from the lagged sample.
  const BrushPreviewPoint pressure_start{.x = 0.2, .y = 0.2, .size = 0.01, .pressure = 0.2};
  const auto fast = sampled_brush_points_for_segment(&pressure_start, 0.8, 0.7,
      0.06, 0.7, 1, 1, 1, 0.75, 1.0, false, 1, 0, 0, 2.0);
  auto previous = pressure_start;
  for (const auto& point : fast) {
    if (std::hypot((point.x - previous.x) * 2.0, point.y - previous.y) > 0.0011)
      return fail("Smoothing and pressure must not leave gaps in the cursor tail.");
    previous = point;
  }
  if (fast.isEmpty() || std::abs(fast.back().x - 0.8) > 1e-12 || std::abs(fast.back().y - 0.7) > 1e-12)
    return fail("Smoothed strokes must reach the exact cursor endpoint.");

  const QSize size(801, 401);
  QVector<BrushPreviewPoint> samples;
  for (int i = 0; i <= 12; ++i) {
    const double x = 0.2 + i * 0.05;
    const double y = 0.3 + i * 0.025;
    const auto next = sampled_brush_points_for_segment(samples.isEmpty() ? nullptr : &samples.back(),
        x, y, 0.06, 0.7, 0, 0, 0, 0.75, 0, false, 1, 0, 0, 2.0);
    samples += next;
  }
  BrushLivePreviewRaster live;
  const BrushPreviewStyle style{.feather = 0.55, .hardness = 0.45, .circular = true};
  for (const auto& sample : samples) live.append(size, {sample}, style);
  // Oracle for the continuously swept circular brush along this straight path.
  const QPointF a(160, 120), b(640, 240);
  const auto delta = b - a;
  const auto length_squared = delta.x() * delta.x() + delta.y() * delta.y();
  int maximum_error = 0;
  for (int y = 80; y <= 280; ++y)
    for (int x = 130; x <= 670; ++x) {
      const auto t = std::clamp(((x - a.x()) * delta.x() + (y - a.y()) * delta.y()) / length_squared, 0.0, 1.0);
      const auto nearest = a + delta * t;
      const auto expected = std::lround(255 * 0.7 * tip(std::hypot(x - nearest.x(), y - nearest.y()) / 24.0, 0.45, 0.55));
      maximum_error = std::max(maximum_error, std::abs(value(live.alpha, size, x, y) - static_cast<int>(expected)));
    }
  if (maximum_error > 2) return fail("Stroke spacing must stay within two alpha levels of continuous coverage.");
  auto mask = blank(size);
  mask.insert("feather", 0.55);
  mask.insert("brushHardness", 0.45);
  mask.insert("brushStrokes", QVariantList{brush_stroke_variant(mask, samples, 1)});
  const auto rebuilt = mask_with_rebuilt_brush_matte(mask, size, "preview");
  if (rebuilt.value("segmentationData").toByteArray() != live.alpha)
    return fail("Replaying the saved stroke must preserve exact live alpha.");
  mask.insert("feather", 1.0);
  mask.insert("brushHardness", 0.0);
  if (mask_with_rebuilt_brush_matte(mask, size, "full-resolution-render").value("segmentationData").toByteArray() != live.alpha)
    return fail("Changing the tool for the next stroke must preserve earlier feather and hardness.");
  auto source_aware = mask;
  source_aware.insert("brushAutoMask", true);
  if (!mask_with_rebuilt_brush_matte(source_aware, size, "preview").isEmpty())
    return fail("Auto Mask must retain authoritative alpha instead of replaying without source colors.");
  std::cout << "continuous_sweep_max_alpha_error=" << maximum_error << " samples=" << samples.size() << '\n';
  return EXIT_SUCCESS;
}
