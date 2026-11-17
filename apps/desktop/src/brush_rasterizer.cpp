#include "brush_rasterizer.h"

#include "edit_recipe_mask_schema.h"
#include "photo_editor/pipeline/color_space.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QColor>
#include <QColorSpace>
#include <QtGui/qrgbafloat.h>

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

AutoMaskPerceptualColor auto_mask_perceptual_color(double r,double g,double b,bool linear) {
  const auto red = linear ? r : photo_editor::pipeline::srgb_to_linear(r);
  const auto green = linear ? g : photo_editor::pipeline::srgb_to_linear(g);
  const auto blue = linear ? b : photo_editor::pipeline::srgb_to_linear(b);
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

AutoMaskPerceptualColor auto_mask_color_at(const QImage& image,int x,int y) {
  const auto source_space=image.colorSpace();
  const auto linear=source_space.isValid() &&
      source_space.transferFunction()==QColorSpace::TransferFunction::Linear;
  const auto from_float=[&](const auto& pixel,bool premultiplied) {
    const auto alpha=static_cast<double>(pixel.alpha());
    const auto inverse=premultiplied ? (alpha>0.0 ? 1.0/alpha : 0.0) : 1.0;
    return auto_mask_perceptual_color(static_cast<double>(pixel.red())*inverse,
        static_cast<double>(pixel.green())*inverse,static_cast<double>(pixel.blue())*inverse,linear);
  };
  switch (image.format()) {
    case QImage::Format_RGBX32FPx4:
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied:
      return from_float(reinterpret_cast<const QRgbaFloat32*>(image.constScanLine(y))[x],
          image.format()==QImage::Format_RGBA32FPx4_Premultiplied);
    case QImage::Format_RGBX16FPx4:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied:
      return from_float(reinterpret_cast<const QRgbaFloat16*>(image.constScanLine(y))[x],
          image.format()==QImage::Format_RGBA16FPx4_Premultiplied);
    case QImage::Format_RGBX64:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied: {
      auto pixel=reinterpret_cast<const QRgba64*>(image.constScanLine(y))[x];
      if (image.format()==QImage::Format_RGBA64_Premultiplied) pixel=pixel.unpremultiplied();
      return auto_mask_perceptual_color(pixel.red()/65535.0,pixel.green()/65535.0,pixel.blue()/65535.0,linear);
    }
    default: {
      auto pixel=image.pixel(x,y);
      if (image.format()==QImage::Format_ARGB32_Premultiplied) pixel=qUnpremultiply(pixel);
      const auto color=QColor::fromRgb(pixel);
      return auto_mask_perceptual_color(color.redF(),color.greenF(),color.blueF(),linear);
    }
  }
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
  if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
    return 0.0;
  }
  const auto center = auto_mask_color_at(image,source_x,source_y);
  auto edge_strength = 0.0;
  const auto sample_neighbor = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
      return;
    }
    edge_strength = std::max(
        edge_strength,
        auto_mask_perceptual_distance(
            center,
            auto_mask_color_at(image,x,y)));
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
  if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
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
  auto previous_color = auto_mask_color_at(image,previous_x,previous_y);
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
    const auto current_color = auto_mask_color_at(image,x,y);
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
    qint64* auto_mask_edge_evaluation_pixels,
    BrushRasterGeometry geometry) {
  if (mask == nullptr) return {};
  auto data = mask->value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask->value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask->value(QStringLiteral("segmentationHeight")).toInt();
  bool was_cancelled = false;
  const auto changed = rasterize_brush_point_into_alpha(&data, width, height,
      source_image, normalized_x, normalized_y, brush_size, brush_flow,
      brush_opacity, feather, erase, auto_mask, auto_mask_tolerance,
      auto_mask_anchor_x, auto_mask_anchor_y, protect_edges, cancellation,
      &was_cancelled, auto_mask_candidate_pixels, auto_mask_edge_evaluation_pixels, geometry);
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
    qint64* auto_mask_edge_evaluation_pixels,
    BrushRasterGeometry geometry) {
  if (cancelled) *cancelled = false;
  if (alpha == nullptr || !valid_segmentation_payload(*alpha, width, height)) return {};
  auto& data = *alpha;
  const auto softness = std::clamp(feather, geometry.circular ? 0.0 : 0.05, 1.0);
  const auto inner_scale = (1.0 - softness) *
      (geometry.circular ? clamp_unit(geometry.hardness) : 1.0);
  const auto clamped_size = std::max(0.01, brush_size);
  const auto clamped_flow = clamp_unit(brush_flow) * clamp_unit(brush_opacity);
  const auto area = geometry.coordinate_area;
  if (!std::isfinite(area.x()) || !std::isfinite(area.y()) ||
      !std::isfinite(area.width()) || !std::isfinite(area.height()) ||
      area.width() < 0.0 || area.height() < 0.0) return {};
  const auto resolved_width=width>1 && area.width()>0.0;
  const auto resolved_height=height>1 && area.height()>0.0;
  const auto canonical_pixel_grid=[](double value) {
    const auto integer=std::round(value);
    const auto uncertainty=8.0*std::numeric_limits<double>::epsilon()*std::max(1.0,std::abs(value));
    return std::abs(value-integer)<=uncertainty ? integer : value;
  };
  const auto derived_width=resolved_width ? canonical_pixel_grid(static_cast<double>(width-1)/area.width()) : 1.0;
  const auto derived_height=resolved_height ? canonical_pixel_grid(static_cast<double>(height-1)/area.height()) : 1.0;
  const auto pitch_scale=resolved_height && geometry.basis_pixel_extent.height()>0.0
      ? derived_height/geometry.basis_pixel_extent.height()
      : (resolved_width && geometry.basis_pixel_extent.width()>0.0
          ? derived_width/geometry.basis_pixel_extent.width() : 1.0);
  const auto reference_width=resolved_width ? derived_width
      : std::max(1.0,geometry.basis_pixel_extent.width()*pitch_scale);
  const auto reference_height=resolved_height ? derived_height
      : std::max(1.0,geometry.basis_pixel_extent.height()*pitch_scale);
  const auto physical_width=geometry.basis_pixel_extent.width()==0.0 ? 0.0 :
      (geometry.basis_pixel_extent.isValid() || area!=QRectF(0.0,0.0,1.0,1.0)
          ? reference_width : static_cast<double>(width-1));
  const auto physical_height=geometry.basis_pixel_extent.height()==0.0 ? 0.0 :
      (geometry.basis_pixel_extent.isValid() || area!=QRectF(0.0,0.0,1.0,1.0)
          ? reference_height : static_cast<double>(height-1));
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
      (auto_mask || protect_edges) && source_image != nullptr && !source_image->isNull() &&
      source_image->width() > 0 && source_image->height() > 0;
  if ((auto_mask || protect_edges) && !auto_mask_enabled) return {};
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
  const auto anchor_perceptual =
      auto_mask_enabled ? auto_mask_color_at(*source_image,anchor_source_x,anchor_source_y)
                        : AutoMaskPerceptualColor{};
  const auto clamped_auto_tolerance = std::clamp(auto_mask_tolerance, 0.02, 0.75);
  const auto pixel_radius = clamped_size * std::min(reference_width, reference_height);
  const auto max_x_radius = std::max(
      2,
      static_cast<int>(
          std::ceil(geometry.circular ? pixel_radius : clamped_size * reference_width)));
  const auto max_y_radius = std::max(
      2,
      static_cast<int>(
          std::ceil(geometry.circular ? pixel_radius : clamped_size * reference_height)));
  const auto crop_pixel_x=canonical_pixel_grid(area.x()*reference_width);
  const auto crop_pixel_y=canonical_pixel_grid(area.y()*reference_height);
  const auto basis_center_pixel_x=center_x*reference_width;
  const auto basis_center_pixel_y=center_y*reference_height;
  const auto anchor_x = basis_center_pixel_x - crop_pixel_x;
  const auto anchor_y = basis_center_pixel_y - crop_pixel_y;
  const auto min_x = std::max(0, static_cast<int>(std::floor(anchor_x)) - max_x_radius);
  const auto max_x = std::min(width - 1, static_cast<int>(std::ceil(anchor_x)) + max_x_radius);
  const auto min_y = std::max(0, static_cast<int>(std::floor(anchor_y)) - max_y_radius);
  const auto max_y = std::min(height - 1, static_cast<int>(std::ceil(anchor_y)) + max_y_radius);
  const auto fast_circular_without_source =
      geometry.circular && !auto_mask_enabled && width > 1 && height > 1;
  const auto pixel_space_circle=geometry.circular &&
      ((width>1 && height>1) || geometry.basis_pixel_extent.isValid() || area!=QRectF(0.0,0.0,1.0,1.0));
  const auto inverse_pixel_radius = pixel_space_circle
      ? 1.0 / pixel_radius
      : 0.0;
  QRect changed_rect;

  for (int y = min_y; y <= max_y; ++y) {
    if (cancellation.cancelled()) {
      if (cancelled != nullptr) {
        *cancelled = true;
      }
      return {};
    }
    for (int x = min_x; x <= max_x; ++x) {
      // A source-free circular brush needs only pixel-space distances. Avoid
      // normalizing each coordinate and multiplying it back inside this loop.
      const auto pixel_x = fast_circular_without_source
          ? 0.5
          : (width > 1 ? (crop_pixel_x+static_cast<double>(x))/reference_width
              : (area.width()==0.0 ? crop_pixel_x/reference_width : area.x()+0.5*area.width()));
      const auto pixel_y = fast_circular_without_source
          ? 0.5
          : (height > 1 ? (crop_pixel_y+static_cast<double>(y))/reference_height
              : (area.height()==0.0 ? crop_pixel_y/reference_height : area.y()+0.5*area.height()));
      const auto delta_x = pixel_x - center_x;
      const auto delta_y = pixel_y - center_y;
      // Preserve the full-source arithmetic order. Forming a cropped center
      // first can fuse multiply/subtract and move a half-alpha edge by one ULP.
      const auto basis_pixel_x=crop_pixel_x+static_cast<double>(x);
      const auto basis_pixel_y=crop_pixel_y+static_cast<double>(y);
      const auto distance = pixel_space_circle
          ? std::hypot(physical_width==0.0 ? 0.0 : basis_pixel_x-basis_center_pixel_x,
                       physical_height==0.0 ? 0.0 : basis_pixel_y-basis_center_pixel_y) * inverse_pixel_radius
          : (geometry.circular
                 ? std::hypot(delta_x * physical_width,
                              delta_y * physical_height) / pixel_radius
                 : std::hypot(delta_x, delta_y) / clamped_size);
      // Even a fully hard tip keeps a one-pixel antialiased edge. Feather
      // changes the falloff inside the same footprint, without a hard fringe.
      const auto tip_weight = geometry.circular && (1.0 - inner_scale) * pixel_radius < 1.0
          ? 1.0 - smoothstep(inner_scale * pixel_radius - 0.5, pixel_radius + 0.5,
                             distance * pixel_radius)
          : 1.0 - smoothstep(inner_scale, 1.0, distance);
      const auto weight = clamped_flow * tip_weight;
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
        const auto sample_perceptual = auto_mask_color_at(*source_image,source_x,source_y);
        const auto color_distance =
            auto_mask_perceptual_distance(sample_perceptual, anchor_perceptual);
        const auto color_gate = auto_mask ? 1.0 - smoothstep(
                                          clamped_auto_tolerance,
                                          clamped_auto_tolerance * 2.5,
                                          color_distance) : 1.0;
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
