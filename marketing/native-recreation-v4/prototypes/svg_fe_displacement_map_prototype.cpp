#include "svg_fe_displacement_map_prototype.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <QColorSpace>

namespace native_recreation::svg_filters {
namespace {

int channel_offset(Channel channel) {
  return static_cast<int>(channel);
}

struct PremultipliedSample final {
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
  double alpha = 0.0;
};

PremultipliedSample mix(const PremultipliedSample& first,
                        const PremultipliedSample& second,
                        double amount) {
  return {
      first.red + (second.red - first.red) * amount,
      first.green + (second.green - first.green) * amount,
      first.blue + (second.blue - first.blue) * amount,
      first.alpha + (second.alpha - first.alpha) * amount,
  };
}

PremultipliedSample read_sample(const QImage& source, int x, int y) {
  if (x < 0 || y < 0 || x >= source.width() || y >= source.height()) {
    return {};
  }
  const auto* row = reinterpret_cast<const QRgb*>(source.constScanLine(y));
  const auto pixel = row[x];
  return {
      static_cast<double>(qRed(pixel)),
      static_cast<double>(qGreen(pixel)),
      static_cast<double>(qBlue(pixel)),
      static_cast<double>(qAlpha(pixel)),
  };
}

PremultipliedSample sample_bilinear(const QImage& source,
                                    double x,
                                    double y) {
  // These limits keep the integer conversion safe and include the one-pixel
  // support where an out-of-bounds bilinear tap can still mix with the image.
  if (!std::isfinite(x) || !std::isfinite(y) || x <= -1.0 || y <= -1.0 ||
      x >= static_cast<double>(source.width()) ||
      y >= static_cast<double>(source.height())) {
    return {};
  }
  const auto left = static_cast<int>(std::floor(x));
  const auto top = static_cast<int>(std::floor(y));
  const auto fraction_x = x - static_cast<double>(left);
  const auto fraction_y = y - static_cast<double>(top);
  const auto top_sample = mix(read_sample(source, left, top),
                              read_sample(source, left + 1, top), fraction_x);
  const auto bottom_sample = mix(read_sample(source, left, top + 1),
                                 read_sample(source, left + 1, top + 1),
                                 fraction_x);
  return mix(top_sample, bottom_sample, fraction_y);
}

int channel_byte(const QImage& map, int x, int y, Channel channel) {
  const auto* row = map.constScanLine(y);
  return static_cast<int>(row[x * 4 + channel_offset(channel)]);
}

}  // namespace

QString validate_displacement_parameters(
    const DisplacementParameters& parameters) {
  if (!std::isfinite(parameters.scale)) {
    return QStringLiteral("The displacement scale must be finite.");
  }
  if (std::abs(parameters.scale) > 32768.0) {
    return QStringLiteral("The displacement scale exceeds the prototype bound.");
  }
  const auto valid_channel = [](Channel channel) {
    return channel == Channel::Red || channel == Channel::Green ||
           channel == Channel::Blue || channel == Channel::Alpha;
  };
  if (!valid_channel(parameters.x_channel) ||
      !valid_channel(parameters.y_channel)) {
    return QStringLiteral("The displacement channel selector is invalid.");
  }
  return {};
}

QImage apply_displacement_map(const QImage& source,
                              const QImage& map,
                              const DisplacementParameters& parameters,
                              QString* error_message) {
  if (error_message != nullptr) error_message->clear();
  const auto parameter_error = validate_displacement_parameters(parameters);
  if (!parameter_error.isEmpty()) {
    if (error_message != nullptr) *error_message = parameter_error;
    return {};
  }
  if (source.isNull() || map.isNull() || source.size() != map.size()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral(
          "The source and displacement map must be nonempty aligned rasters of equal size.");
    }
    return {};
  }

  const auto source_premultiplied =
      source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  const auto map_straight = map.convertToFormat(QImage::Format_RGBA8888);
  if (source_premultiplied.isNull() || map_straight.isNull()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Could not normalize the displacement inputs.");
    }
    return {};
  }

  QImage output(source.size(), QImage::Format_ARGB32_Premultiplied);
  if (output.isNull()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Could not allocate the displaced raster.");
    }
    return {};
  }
  output.setColorSpace(source.colorSpace());

  for (int y = 0; y < output.height(); ++y) {
    auto* output_row = reinterpret_cast<QRgb*>(output.scanLine(y));
    for (int x = 0; x < output.width(); ++x) {
      const auto displacement_x = parameters.scale *
          (static_cast<double>(channel_byte(map_straight, x, y,
                                            parameters.x_channel)) /
               255.0 -
           0.5);
      const auto displacement_y = parameters.scale *
          (static_cast<double>(channel_byte(map_straight, x, y,
                                            parameters.y_channel)) /
               255.0 -
           0.5);
      const auto sample = sample_bilinear(
          source_premultiplied,
          static_cast<double>(x) + displacement_x,
          static_cast<double>(y) + displacement_y);
      const auto alpha = std::clamp(qRound(sample.alpha), 0, 255);
      const auto red = std::clamp(qRound(sample.red), 0, alpha);
      const auto green = std::clamp(qRound(sample.green), 0, alpha);
      const auto blue = std::clamp(qRound(sample.blue), 0, alpha);
      output_row[x] = qRgba(red, green, blue, alpha);
    }
  }
  return output;
}

photo_editor::desktop::timeline_motion::FractalNoiseParameters
canvas_brush_noise_parameters() {
  photo_editor::desktop::timeline_motion::FractalNoiseParameters parameters;
  parameters.base_frequency_x = 0.07;
  parameters.base_frequency_y = 0.2;
  parameters.num_octaves = 2;
  parameters.seed = 8;
  parameters.stitch_tiles = false;
  return parameters;
}

QImage render_canvas_brush_noise_map(const QSize& user_space_size,
                                     QString* error_message) {
  const DisplacementParameters displacement{8.0, Channel::Alpha, Channel::Alpha};
  const auto parameter_error = validate_displacement_parameters(displacement);
  if (!parameter_error.isEmpty()) {
    if (error_message != nullptr) *error_message = parameter_error;
    return {};
  }
  return photo_editor::desktop::timeline_motion::render_fractal_noise_frame(
      user_space_size, canvas_brush_noise_parameters(), error_message);
}

}  // namespace native_recreation::svg_filters
