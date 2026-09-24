#include "brush_rasterizer.h"

#include "edit_recipe_mask_schema.h"
#include "photo_editor/pipeline/color_space.h"

#include <algorithm>
#include <cmath>

#include <QColor>

namespace photo_editor::desktop {
namespace {

double clamp_unit(double value) {
  return std::clamp(value, 0.0, 1.0);
}

double smoothstep(double edge0, double edge1, double value) {
  if (edge0 == edge1) {
    return value < edge0 ? 0.0 : 1.0;
  }
  const auto t = clamp_unit((value - edge0) / (edge1 - edge0));
  return t * t * (3.0 - 2.0 * t);
}

struct AutoMaskPerceptualColor final {
  double lightness = 0.0;
  double a = 0.0;
  double b = 0.0;
  double luminance = 0.0;
};

AutoMaskPerceptualColor auto_mask_perceptual_color(const QColor& color) {
  const auto red = photo_editor::pipeline::srgb_to_linear(color.redF());
  const auto green = photo_editor::pipeline::srgb_to_linear(color.greenF());
  const auto blue = photo_editor::pipeline::srgb_to_linear(color.blueF());
  const auto l = 0.4122214708 * red + 0.5363325363 * green + 0.0514459929 * blue;
  const auto m = 0.2119034982 * red + 0.6806995451 * green + 0.1073969566 * blue;
  const auto s = 0.0883024619 * red + 0.2817188376 * green + 0.6299787005 * blue;
  const auto l_root = std::cbrt(std::max(0.0, l));
  const auto m_root = std::cbrt(std::max(0.0, m));
  const auto s_root = std::cbrt(std::max(0.0, s));
  return AutoMaskPerceptualColor{
      .lightness =
          0.2104542553 * l_root + 0.7936177850 * m_root - 0.0040720468 * s_root,
      .a = 1.9779984951 * l_root - 2.4285922050 * m_root + 0.4505937099 * s_root,
      .b = 0.0259040371 * l_root + 0.7827717662 * m_root - 0.8086757660 * s_root,
      .luminance = red * 0.2126 + green * 0.7152 + blue * 0.0722,
  };
}

double auto_mask_perceptual_distance(
    const AutoMaskPerceptualColor& left,
    const AutoMaskPerceptualColor& right) {
  const auto lightness_delta = left.lightness - right.lightness;
  const auto a_delta = left.a - right.a;
  const auto b_delta = left.b - right.b;
  const auto luminance_delta = left.luminance - right.luminance;
  return std::sqrt(
      lightness_delta * lightness_delta * 1.30 + a_delta * a_delta * 3.20 +
      b_delta * b_delta * 3.20 + luminance_delta * luminance_delta * 0.75);
}

double auto_mask_local_edge_strength(const QImage& image, int source_x, int source_y) {
  if (image.isNull() || image.width() <= 1 || image.height() <= 1) {
    return 0.0;
  }
  const auto center =
      auto_mask_perceptual_color(QColor::fromRgb(image.pixel(source_x, source_y)));
  auto edge_strength = 0.0;
  const auto sample_neighbor = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
      return;
    }
    edge_strength = std::max(
        edge_strength,
        auto_mask_perceptual_distance(
            center,
            auto_mask_perceptual_color(QColor::fromRgb(image.pixel(x, y)))));
  };
  sample_neighbor(source_x - 1, source_y);
  sample_neighbor(source_x + 1, source_y);
  sample_neighbor(source_x, source_y - 1);
  sample_neighbor(source_x, source_y + 1);
  return edge_strength;
}

double auto_mask_edge_barrier_strength(
    const QImage& image,
    int anchor_x,
    int anchor_y,
    int source_x,
    int source_y) {
  if (image.isNull() || image.width() <= 1 || image.height() <= 1) {
    return 0.0;
  }
  const auto delta_x = source_x - anchor_x;
  const auto delta_y = source_y - anchor_y;
  const auto steps = std::min(64, std::max(std::abs(delta_x), std::abs(delta_y)));
  if (steps <= 0) {
    return auto_mask_local_edge_strength(image, source_x, source_y);
  }

  auto edge_strength = 0.0;
  auto previous_x = std::clamp(anchor_x, 0, image.width() - 1);
  auto previous_y = std::clamp(anchor_y, 0, image.height() - 1);
  auto previous_color =
      auto_mask_perceptual_color(QColor::fromRgb(image.pixel(previous_x, previous_y)));
  for (int step = 1; step <= steps; ++step) {
    const auto t = static_cast<double>(step) / static_cast<double>(steps);
    const auto x = std::clamp(
        static_cast<int>(std::lround(
            static_cast<double>(anchor_x) + static_cast<double>(delta_x) * t)),
        0,
        image.width() - 1);
    const auto y = std::clamp(
        static_cast<int>(std::lround(
            static_cast<double>(anchor_y) + static_cast<double>(delta_y) * t)),
        0,
        image.height() - 1);
    const auto current_color =
        auto_mask_perceptual_color(QColor::fromRgb(image.pixel(x, y)));
    edge_strength = std::max(
        edge_strength,
        auto_mask_perceptual_distance(previous_color, current_color));
    edge_strength =
        std::max(edge_strength, auto_mask_local_edge_strength(image, x, y));
    previous_color = current_color;
    previous_x = x;
    previous_y = y;
  }
  return std::max(
      edge_strength, auto_mask_local_edge_strength(image, previous_x, previous_y));
}

}  // namespace

QRect rasterize_brush_point_into_mask(
    QVariantMap* mask,
    const QImage* source_image,
    double normalized_x,
    double normalized_y,
    double brush_size,
    double brush_flow,
    double brush_opacity,
    double feather,
    bool erase,
    bool auto_mask,
    double auto_mask_tolerance,
    double auto_mask_anchor_x,
    double auto_mask_anchor_y,
    bool protect_edges,
    photo_editor::pipeline::CancellationToken cancellation,
    bool* cancelled,
    qint64* auto_mask_candidate_pixels,
    qint64* auto_mask_edge_evaluation_pixels) {
  if (mask == nullptr) return {};
  auto data = mask->value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask->value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask->value(QStringLiteral("segmentationHeight")).toInt();
  bool was_cancelled = false;
  const auto changed = rasterize_brush_point_into_alpha(&data, width, height,
      source_image, normalized_x, normalized_y, brush_size, brush_flow,
      brush_opacity, feather, erase, auto_mask, auto_mask_tolerance,
      auto_mask_anchor_x, auto_mask_anchor_y, protect_edges, cancellation,
      &was_cancelled, auto_mask_candidate_pixels, auto_mask_edge_evaluation_pixels);
  if (cancelled) *cancelled = was_cancelled;
  if (!was_cancelled && valid_segmentation_payload(data, width, height))
    mask->insert(QStringLiteral("segmentationData"), data);
  return changed;
}

QRect rasterize_brush_point_into_alpha(
    QByteArray* alpha, int width, int height,
    const QImage* source_image,
    double normalized_x,
    double normalized_y,
    double brush_size,
    double brush_flow,
    double brush_opacity,
    double feather,
    bool erase,
    bool auto_mask,
    double auto_mask_tolerance,
    double auto_mask_anchor_x,
    double auto_mask_anchor_y,
    bool protect_edges,
    photo_editor::pipeline::CancellationToken cancellation,
    bool* cancelled,
    qint64* auto_mask_candidate_pixels,
    qint64* auto_mask_edge_evaluation_pixels) {
  if (cancelled) *cancelled = false;
  if (alpha == nullptr || !valid_segmentation_payload(*alpha, width, height)) return {};
  auto& data = *alpha;
  const auto softness = std::clamp(feather, 0.05, 1.0);
  const auto inner_scale = std::max(0.0, 1.0 - softness);
  const auto clamped_size = std::max(0.01, brush_size);
  const auto clamped_flow = clamp_unit(brush_flow) * clamp_unit(brush_opacity);
  const auto center_x = clamp_unit(normalized_x);
  const auto center_y = clamp_unit(normalized_y);
  const auto anchor_x_normalized =
      auto_mask_anchor_x >= 0.0 && auto_mask_anchor_x <= 1.0
          ? auto_mask_anchor_x
          : center_x;
  const auto anchor_y_normalized =
      auto_mask_anchor_y >= 0.0 && auto_mask_anchor_y <= 1.0
          ? auto_mask_anchor_y
          : center_y;
  const auto auto_mask_enabled =
      auto_mask && source_image != nullptr && !source_image->isNull() &&
      source_image->width() > 1 && source_image->height() > 1;
  const auto source_width = auto_mask_enabled ? source_image->width() : 0;
  const auto source_height = auto_mask_enabled ? source_image->height() : 0;
  const auto anchor_source_x =
      auto_mask_enabled
          ? std::clamp(
                static_cast<int>(std::lround(
                    anchor_x_normalized * static_cast<double>(source_width - 1))),
                0,
                source_width - 1)
          : 0;
  const auto anchor_source_y =
      auto_mask_enabled
          ? std::clamp(
                static_cast<int>(std::lround(
                    anchor_y_normalized * static_cast<double>(source_height - 1))),
                0,
                source_height - 1)
          : 0;
  const auto anchor_color =
      auto_mask_enabled
          ? QColor::fromRgb(source_image->pixel(anchor_source_x, anchor_source_y))
          : QColor{};
  const auto anchor_perceptual =
      auto_mask_enabled ? auto_mask_perceptual_color(anchor_color)
                        : AutoMaskPerceptualColor{};
  const auto clamped_auto_tolerance = std::clamp(auto_mask_tolerance, 0.02, 0.75);
  const auto max_x_radius = std::max(
      2,
      static_cast<int>(
          std::ceil(clamped_size * static_cast<double>(std::max(width - 1, 1)))));
  const auto max_y_radius = std::max(
      2,
      static_cast<int>(
          std::ceil(clamped_size * static_cast<double>(std::max(height - 1, 1)))));
  const auto anchor_x = center_x * static_cast<double>(std::max(width - 1, 1));
  const auto anchor_y = center_y * static_cast<double>(std::max(height - 1, 1));
  const auto min_x = std::max(0, static_cast<int>(std::floor(anchor_x)) - max_x_radius);
  const auto max_x = std::min(width - 1, static_cast<int>(std::ceil(anchor_x)) + max_x_radius);
  const auto min_y = std::max(0, static_cast<int>(std::floor(anchor_y)) - max_y_radius);
  const auto max_y = std::min(height - 1, static_cast<int>(std::ceil(anchor_y)) + max_y_radius);
  QRect changed_rect;

  for (int y = min_y; y <= max_y; ++y) {
    if (cancellation.cancelled()) {
      if (cancelled != nullptr) {
        *cancelled = true;
      }
      return {};
    }
    for (int x = min_x; x <= max_x; ++x) {
      const auto pixel_x =
          width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
      const auto pixel_y =
          height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      const auto delta_x = pixel_x - center_x;
      const auto delta_y = pixel_y - center_y;
      const auto distance = std::sqrt(delta_x * delta_x + delta_y * delta_y) / clamped_size;
      const auto weight = clamped_flow * (1.0 - smoothstep(inner_scale, 1.0, distance));
      auto adjusted_weight = weight;
      if (auto_mask_enabled) {
        if (auto_mask_candidate_pixels != nullptr) {
          ++(*auto_mask_candidate_pixels);
        }
        const auto source_x = std::clamp(
            static_cast<int>(std::lround(pixel_x * static_cast<double>(source_width - 1))),
            0,
            source_width - 1);
        const auto source_y = std::clamp(
            static_cast<int>(std::lround(pixel_y * static_cast<double>(source_height - 1))),
            0,
            source_height - 1);
        const auto sample_perceptual = auto_mask_perceptual_color(
            QColor::fromRgb(source_image->pixel(source_x, source_y)));
        const auto color_distance =
            auto_mask_perceptual_distance(sample_perceptual, anchor_perceptual);
        const auto color_gate = 1.0 - smoothstep(
                                          clamped_auto_tolerance,
                                          clamped_auto_tolerance * 2.5,
                                          color_distance);
        if (color_gate <= 1e-6) {
          continue;
        }
        if (auto_mask_edge_evaluation_pixels != nullptr) {
          ++(*auto_mask_edge_evaluation_pixels);
        }
        const auto edge_strength = std::max(
            auto_mask_local_edge_strength(*source_image, source_x, source_y),
            auto_mask_edge_barrier_strength(
                *source_image,
                anchor_source_x,
                anchor_source_y,
                source_x,
                source_y));
        const auto edge_gate_start = protect_edges ? 0.018 : 0.035;
        const auto edge_gate_end = protect_edges ? 0.09 : 0.16;
        const auto edge_color_start =
            clamped_auto_tolerance * (protect_edges ? 0.05 : 0.35);
        const auto edge_color_end =
            clamped_auto_tolerance * (protect_edges ? 0.35 : 1.0);
        const auto edge_gate =
            1.0 - smoothstep(edge_gate_start, edge_gate_end, edge_strength) *
                      smoothstep(edge_color_start, edge_color_end, color_distance);
        adjusted_weight *= color_gate * edge_gate;
      }
      if (adjusted_weight <= 1e-6) {
        continue;
      }
      const auto index = y * width + x;
      const auto existing = static_cast<unsigned char>(data.at(index));
      char next_value = data.at(index);
      if (erase) {
        next_value = static_cast<char>(std::lround(
            static_cast<double>(existing) * (1.0 - clamp_unit(adjusted_weight))));
      } else {
        next_value = static_cast<char>(std::max(
            static_cast<int>(existing),
            static_cast<int>(std::lround(clamp_unit(adjusted_weight) * 255.0))));
      }
      if (next_value == data.at(index)) {
        continue;
      }
      data[index] = next_value;
      const QRect pixel_rect(x, y, 1, 1);
      changed_rect = changed_rect.isNull() ? pixel_rect : changed_rect.united(pixel_rect);
    }
  }

  return changed_rect;
}

}  // namespace photo_editor::desktop
