#include "svg_fe_displacement_map_prototype.h"

#include <QCoreApplication>
#include <QColor>
#include <QColorSpace>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

using native_recreation::svg_filters::Channel;
using native_recreation::svg_filters::DisplacementParameters;
using native_recreation::svg_filters::apply_displacement_map;
using native_recreation::svg_filters::canvas_brush_noise_parameters;
using native_recreation::svg_filters::render_canvas_brush_noise_map;
using photo_editor::desktop::timeline_motion::render_fractal_noise_frame;

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

QRgb rgba_at(const QImage& image, int x, int y) {
  return image.pixel(x, y);
}

bool equal_pixels(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) return false;
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width(); ++x) {
      if (left.pixelColor(x, y) != right.pixelColor(x, y)) return false;
    }
  }
  return true;
}

bool equal_premultiplied_bytes(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) return false;
  const auto left_premult = left.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  const auto right_premult = right.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  for (int y = 0; y < left.height(); ++y) {
    const auto row_bytes = left.width() * static_cast<int>(sizeof(QRgb));
    if (std::memcmp(left_premult.constScanLine(y), right_premult.constScanLine(y),
                    static_cast<std::size_t>(row_bytes)) != 0) {
      return false;
    }
  }
  return true;
}

QImage scalar_reference(const QImage& input,
                        const QImage& map,
                        const DisplacementParameters& parameters) {
  const auto source = input.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  const auto displacement_map = map.convertToFormat(QImage::Format_RGBA8888);
  QImage reference(input.size(), QImage::Format_ARGB32_Premultiplied);
  auto read = [&source](int x, int y, int channel) {
    if (x < 0 || y < 0 || x >= source.width() || y >= source.height()) return 0.0;
    const auto* row = reinterpret_cast<const QRgb*>(source.constScanLine(y));
    const auto pixel = row[x];
    switch (channel) {
      case 0: return static_cast<double>(qRed(pixel));
      case 1: return static_cast<double>(qGreen(pixel));
      case 2: return static_cast<double>(qBlue(pixel));
      default: return static_cast<double>(qAlpha(pixel));
    }
  };
  const auto offsets = [](Channel channel) { return static_cast<int>(channel); };
  for (int y = 0; y < input.height(); ++y) {
    auto* out = reinterpret_cast<QRgb*>(reference.scanLine(y));
    const auto* map_row = displacement_map.constScanLine(y);
    for (int x = 0; x < input.width(); ++x) {
      const auto dx = parameters.scale *
          (static_cast<double>(map_row[x * 4 + offsets(parameters.x_channel)]) /
               255.0 -
           0.5);
      const auto dy = parameters.scale *
          (static_cast<double>(map_row[x * 4 + offsets(parameters.y_channel)]) /
               255.0 -
           0.5);
      const auto sx = static_cast<double>(x) + dx;
      const auto sy = static_cast<double>(y) + dy;
      if (sx <= -1.0 || sy <= -1.0 || sx >= source.width() ||
          sy >= source.height()) {
        out[x] = qRgba(0, 0, 0, 0);
        continue;
      }
      const auto x0 = static_cast<int>(std::floor(sx));
      const auto y0 = static_cast<int>(std::floor(sy));
      const auto tx = sx - static_cast<double>(x0);
      const auto ty = sy - static_cast<double>(y0);
      const auto interpolate = [&](int channel) {
        const auto top = read(x0, y0, channel) * (1.0 - tx) +
                         read(x0 + 1, y0, channel) * tx;
        const auto bottom = read(x0, y0 + 1, channel) * (1.0 - tx) +
                            read(x0 + 1, y0 + 1, channel) * tx;
        return top * (1.0 - ty) + bottom * ty;
      };
      const auto alpha = std::clamp(qRound(interpolate(3)), 0, 255);
      out[x] = qRgba(std::clamp(qRound(interpolate(0)), 0, alpha),
                     std::clamp(qRound(interpolate(1)), 0, alpha),
                     std::clamp(qRound(interpolate(2)), 0, alpha), alpha);
    }
  }
  reference.setColorSpace(input.colorSpace());
  return reference;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  QImage source(11, 9, QImage::Format_RGBA8888);
  QImage map(11, 9, QImage::Format_RGBA8888);
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      source.setPixelColor(x, y, QColor((x * 21) % 256, (y * 27) % 256,
                                        (x * 13 + y * 7) % 256,
                                        (x * 17 + y * 11) % 256));
      map.setPixelColor(x, y,
                        QColor((x * 47) % 256, (y * 61) % 256,
                               (x * 19 + y * 31) % 256,
                               (x * 37 + y * 53) % 256));
    }
  }
  const DisplacementParameters parameters{8.0, Channel::Alpha, Channel::Alpha};
  QString error;
  const auto actual = apply_displacement_map(source, map, parameters, &error);
  const auto expected = scalar_reference(source, map, parameters);
  if (actual.isNull() || expected.isNull() || !equal_pixels(actual, expected)) {
    std::cerr << error.toStdString() << '\n';
    return fail("The prototype must match the independent W3C equation oracle for every pixel.");
  }

  QImage constant_map(source.size(), QImage::Format_RGBA8888);
  for (int y = 0; y < constant_map.height(); ++y) {
    for (int x = 0; x < constant_map.width(); ++x) {
      constant_map.setPixelColor(x, y, QColor((x * 23) % 256,
                                               (y * 43) % 256,
                                               (x * y * 17) % 256, 255));
    }
  }
  const auto positive_shift = apply_displacement_map(
      source, constant_map, parameters, &error);
  if (positive_shift.isNull()) {
    std::cerr << error.toStdString() << '\n';
    return fail("The constant displacement result should be available.");
  }
  if (positive_shift.pixelColor(2, 1) != source.pixelColor(6, 5)) {
    const auto actual_pixel = positive_shift.pixelColor(2, 1);
    const auto expected_pixel = source.pixelColor(6, 5);
    if (std::abs(actual_pixel.red() - expected_pixel.red()) > 1 ||
        std::abs(actual_pixel.green() - expected_pixel.green()) > 1 ||
        std::abs(actual_pixel.blue() - expected_pixel.blue()) > 1 ||
        actual_pixel.alpha() != expected_pixel.alpha()) {
      return fail("Default A/A selectors must shift by +4 px in both axes, within 8-bit premultiply rounding.");
    }
  }
  if (positive_shift.pixelColor(10, 8).alpha() != 0) {
    std::cerr << "outside alpha=" << positive_shift.pixelColor(10, 8).alpha() << '\n';
    return fail("Default A/A selectors must shift by +4 px and return transparent out of bounds.");
  }

  QImage same_alpha_different_rgb = constant_map;
  for (int y = 0; y < same_alpha_different_rgb.height(); ++y) {
    for (int x = 0; x < same_alpha_different_rgb.width(); ++x) {
      same_alpha_different_rgb.setPixelColor(x, y,
          QColor(255 - ((x * 23) % 256), (y * 9) % 256, (x * 7) % 256, 255));
    }
  }
  if (!equal_pixels(positive_shift,
                    apply_displacement_map(source, same_alpha_different_rgb,
                                           parameters, &error))) {
    return fail("The SVG default alpha selector must ignore map RGB values.");
  }

  QImage alpha_edge_source(2, 2, QImage::Format_RGBA8888);
  for (int y = 0; y < 2; ++y) {
    alpha_edge_source.setPixelColor(0, y, QColor(255, 0, 0, 0));
    alpha_edge_source.setPixelColor(1, y, QColor(0, 0, 255, 255));
  }
  QImage fractional_map(2, 2, QImage::Format_RGBA8888);
  fractional_map.fill(QColor(0, 0, 0, 191));
  const auto premultiplied = apply_displacement_map(
      alpha_edge_source, fractional_map,
      DisplacementParameters{2.0, Channel::Alpha, Channel::Alpha}, &error);
  if (premultiplied.isNull() ||
      premultiplied.pixelColor(0, 0).alpha() < 120 ||
      premultiplied.pixelColor(0, 0).alpha() > 135 ||
      premultiplied.pixelColor(0, 0).red() != 0 ||
      premultiplied.pixelColor(0, 0).blue() != 255) {
    if (!premultiplied.isNull()) {
      const auto pixel = premultiplied.pixelColor(0, 0);
      std::cerr << "premult pixel=" << pixel.red() << ',' << pixel.green() << ','
                << pixel.blue() << ',' << pixel.alpha() << '\n';
    }
    return fail("Source interpolation must preserve premultiplied alpha at translucent edges.");
  }

  const auto noise_parameters = canvas_brush_noise_parameters();
  const QSize small_user_space(96, 120);
  const auto map_from_prototype =
      render_canvas_brush_noise_map(small_user_space, &error);
  const auto map_from_existing = render_fractal_noise_frame(
      small_user_space, noise_parameters, &error);
  if (map_from_prototype.isNull() || map_from_existing.isNull() ||
      !equal_pixels(map_from_prototype, map_from_existing) ||
      noise_parameters.base_frequency_x != 0.07 ||
      noise_parameters.base_frequency_y != 0.2 ||
      noise_parameters.num_octaves != 2 || noise_parameters.seed != 8 ||
      noise_parameters.stitch_tiles) {
    return fail("Canvas turbulence must reuse the existing W3C fractalNoise map without parameter drift.");
  }
  const auto displaced_canvas = apply_displacement_map(
      source, map, DisplacementParameters{8.0, Channel::Alpha, Channel::Alpha},
      &error);
  if (displaced_canvas.isNull() ||
      !equal_pixels(displaced_canvas, scalar_reference(
          source, map, DisplacementParameters{8.0, Channel::Alpha,
                                               Channel::Alpha}))) {
    return fail("The Canvas map/source combination must pass the per-pixel scalar oracle.");
  }

  const auto noise_alpha = map_from_prototype.convertToFormat(QImage::Format_RGBA8888);
  int minimum_alpha = 255;
  int maximum_alpha = 0;
  for (int y = 0; y < noise_alpha.height(); ++y) {
    const auto* row = noise_alpha.constScanLine(y);
    for (int x = 0; x < noise_alpha.width(); ++x) {
      minimum_alpha = std::min(minimum_alpha, static_cast<int>(row[x * 4 + 3]));
      maximum_alpha = std::max(maximum_alpha, static_cast<int>(row[x * 4 + 3]));
    }
  }
  if (minimum_alpha == maximum_alpha) {
    return fail("The Canvas feTurbulence alpha channel must provide spatial displacement.");
  }

  QImage canvas_stroke(QSize(1000, 1250), QImage::Format_RGBA8888);
  canvas_stroke.fill(Qt::transparent);
  {
    QPainter painter(&canvas_stroke);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen brush_pen(QColor(0xee, 0x6d, 0x33), 180.0,
                   Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(brush_pen);
    QPainterPath brush_path;
    brush_path.moveTo(-120.0, 955.0);
    brush_path.lineTo(1100.0, 400.0);
    painter.drawPath(brush_path);
  }
  const auto canvas_noise = render_canvas_brush_noise_map(canvas_stroke.size(), &error);
  const auto canvas_result = apply_displacement_map(
      canvas_stroke, canvas_noise,
      DisplacementParameters{8.0, Channel::Alpha, Channel::Alpha}, &error);
  const auto canvas_oracle = scalar_reference(
      canvas_stroke, canvas_noise,
      DisplacementParameters{8.0, Channel::Alpha, Channel::Alpha});
  if (canvas_noise.isNull() || canvas_result.isNull() || canvas_oracle.isNull() ||
      !equal_premultiplied_bytes(canvas_result, canvas_oracle)) {
    std::cerr << error.toStdString() << '\n';
    return fail("The 1000x1250 CampaignPoster brush must match the scalar pixel oracle.");
  }
  const auto full_size_noise = canvas_noise.convertToFormat(QImage::Format_RGBA8888);
  int canvas_minimum_alpha = 255;
  int canvas_maximum_alpha = 0;
  for (int y = 0; y < full_size_noise.height(); ++y) {
    const auto* row = full_size_noise.constScanLine(y);
    for (int x = 0; x < full_size_noise.width(); ++x) {
      canvas_minimum_alpha = std::min(
          canvas_minimum_alpha, static_cast<int>(row[x * 4 + 3]));
      canvas_maximum_alpha = std::max(
          canvas_maximum_alpha, static_cast<int>(row[x * 4 + 3]));
    }
  }
  const auto original_canvas = canvas_stroke.convertToFormat(
      QImage::Format_ARGB32_Premultiplied);
  const auto premultiplied_canvas_result = canvas_result.convertToFormat(
      QImage::Format_ARGB32_Premultiplied);
  qint64 changed_canvas_pixels = 0;
  for (int y = 0; y < canvas_result.height(); ++y) {
    const auto* original_row = reinterpret_cast<const QRgb*>(original_canvas.constScanLine(y));
    const auto* displaced_row = reinterpret_cast<const QRgb*>(premultiplied_canvas_result.constScanLine(y));
    for (int x = 0; x < canvas_result.width(); ++x) {
      if (original_row[x] != displaced_row[x]) ++changed_canvas_pixels;
    }
  }
  if (changed_canvas_pixels < 1000) {
    return fail("The authored 8 px turbulence map should alter a visible number of brush edge pixels.");
  }
  std::cout << "W3C displacement scalar oracle: 99 source pixels, default A/A, scale=8;"
               " Canvas turbulence alpha range="
            << minimum_alpha << ".." << maximum_alpha
            << "; 1000x1250 map alpha=" << canvas_minimum_alpha << ".."
            << canvas_maximum_alpha
            << "; CampaignPoster changed pixels=" << changed_canvas_pixels << '\n';
  return 0;
}
