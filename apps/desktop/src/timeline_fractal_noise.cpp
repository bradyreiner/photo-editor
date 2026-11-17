#include "timeline_fractal_noise.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <QMetaType>
#include <QSet>
#include <utility>

namespace photo_editor::desktop::timeline_motion {
namespace {

constexpr std::int64_t kRandomModulus = 2147483647;
constexpr std::int64_t kRandomMultiplier = 16807;
constexpr std::int64_t kRandomQuotient = 127773;
constexpr std::int64_t kRandomRemainder = 2836;
constexpr int kPermutationSize = 256;
constexpr int kPermutationMask = 0xff;
constexpr double kPerlinOffset = 4096.0;
constexpr qint64 kMaximumPixels = 64LL * 1024LL * 1024LL;
constexpr int kMaximumDimension = 16384;
constexpr int kMaximumOctaves = 16;
constexpr double kMaximumBaseFrequency = 64.0;
constexpr std::int64_t kMaximumExactJsonInteger = 9007199254740991LL;

struct StitchInfo {
  std::int64_t width = 0;
  std::int64_t height = 0;
  std::int64_t wrap_x = 0;
  std::int64_t wrap_y = 0;
};

class TurbulenceNoise {
 public:
  explicit TurbulenceNoise(std::int64_t seed) {
    initialize(seed);
  }

  double fractal_sum(int channel,
                     double x,
                     double y,
                     double frequency_x,
                     double frequency_y,
                     int octaves,
                     bool stitch,
                     double tile_x,
                     double tile_y,
                     double tile_width,
                     double tile_height) const {
    StitchInfo stitch_info;
    const StitchInfo* stitch_pointer = nullptr;
    if (stitch) {
      stitch_info.width = static_cast<std::int64_t>(
          tile_width * frequency_x + 0.5);
      stitch_info.wrap_x = static_cast<std::int64_t>(
          tile_x * frequency_x + kPerlinOffset + stitch_info.width);
      stitch_info.height = static_cast<std::int64_t>(
          tile_height * frequency_y + 0.5);
      stitch_info.wrap_y = static_cast<std::int64_t>(
          tile_y * frequency_y + kPerlinOffset + stitch_info.height);
      stitch_pointer = &stitch_info;
    }

    double sum = 0.0;
    double point_x = x * frequency_x;
    double point_y = y * frequency_y;
    double ratio = 1.0;
    for (int octave = 0; octave < octaves; ++octave) {
      sum += noise2(channel, point_x, point_y, stitch_pointer) / ratio;
      point_x *= 2.0;
      point_y *= 2.0;
      ratio *= 2.0;
      if (stitch_pointer != nullptr) {
        stitch_info.width *= 2;
        stitch_info.wrap_x = 2 * stitch_info.wrap_x -
                             static_cast<std::int64_t>(kPerlinOffset);
        stitch_info.height *= 2;
        stitch_info.wrap_y = 2 * stitch_info.wrap_y -
                             static_cast<std::int64_t>(kPerlinOffset);
      }
    }
    return sum;
  }

 private:
  using Gradient = std::array<double, 2>;
  static constexpr int kTableSize = kPermutationSize * 2 + 2;

  void initialize(std::int64_t seed) {
    std::int64_t state = setup_seed(seed);
    for (int channel = 0; channel < 4; ++channel) {
      for (int index = 0; index < kPermutationSize; ++index) {
        permutation_[static_cast<std::size_t>(index)] = index;
        while (true) {
          Gradient candidate{};
          for (auto& component : candidate) {
            state = fractal_noise_detail::park_miller_step(state);
            component = static_cast<double>(state % (kPermutationSize * 2) -
                                            kPermutationSize) /
                        static_cast<double>(kPermutationSize);
          }
          if (candidate[0] == 0.0 && candidate[1] == 0.0) {
            continue;
          }
          const auto magnitude = std::sqrt(candidate[0] * candidate[0] +
                                           candidate[1] * candidate[1]);
          if (magnitude > 1.0) {
            continue;
          }
          gradients_[static_cast<std::size_t>(channel)]
                    [static_cast<std::size_t>(index)] =
              {candidate[0] / magnitude, candidate[1] / magnitude};
          break;
        }
      }
    }

    for (int index = kPermutationSize - 1; index > 0; --index) {
      const auto random_index = static_cast<int>(
          (state = fractal_noise_detail::park_miller_step(state)) %
          kPermutationSize);
      std::swap(permutation_[static_cast<std::size_t>(index)],
                permutation_[static_cast<std::size_t>(random_index)]);
    }

    for (int index = 0; index < kPermutationSize + 2; ++index) {
      permutation_[static_cast<std::size_t>(kPermutationSize + index)] =
          permutation_[static_cast<std::size_t>(index)];
      for (int channel = 0; channel < 4; ++channel) {
        gradients_[static_cast<std::size_t>(channel)]
                  [static_cast<std::size_t>(kPermutationSize + index)] =
            gradients_[static_cast<std::size_t>(channel)]
                      [static_cast<std::size_t>(index)];
      }
    }
  }

  static std::int64_t setup_seed(std::int64_t seed) {
    if (seed <= 0) {
      seed = -(seed % (kRandomModulus - 1)) + 1;
    }
    return std::min(seed, kRandomModulus - 1);
  }

  double noise2(int channel,
                double point_x,
                double point_y,
                const StitchInfo* stitch) const {
    const auto x_position = point_x + kPerlinOffset;
    const auto y_position = point_y + kPerlinOffset;
    auto x0 = static_cast<std::int64_t>(x_position);
    auto x1 = x0 + 1;
    const auto x_fraction = x_position - static_cast<double>(x0);
    const auto x_fraction_minus_one = x_fraction - 1.0;
    auto y0 = static_cast<std::int64_t>(y_position);
    auto y1 = y0 + 1;
    const auto y_fraction = y_position - static_cast<double>(y0);
    const auto y_fraction_minus_one = y_fraction - 1.0;

    if (stitch != nullptr) {
      if (x0 >= stitch->wrap_x) x0 -= stitch->width;
      if (x1 >= stitch->wrap_x) x1 -= stitch->width;
      if (y0 >= stitch->wrap_y) y0 -= stitch->height;
      if (y1 >= stitch->wrap_y) y1 -= stitch->height;
    }
    const auto& permutation = permutation_;
    const auto& channel_gradients = gradients_[static_cast<std::size_t>(channel)];
    const auto i = permutation[static_cast<std::size_t>(x0 & kPermutationMask)];
    const auto j = permutation[static_cast<std::size_t>(x1 & kPermutationMask)];
    const auto y0_index = static_cast<std::size_t>(y0 & kPermutationMask);
    const auto y1_index = static_cast<std::size_t>(y1 & kPermutationMask);
    const auto b00 = permutation[static_cast<std::size_t>(i) + y0_index];
    const auto b10 = permutation[static_cast<std::size_t>(j) + y0_index];
    const auto b01 = permutation[static_cast<std::size_t>(i) + y1_index];
    const auto b11 = permutation[static_cast<std::size_t>(j) + y1_index];
    const auto fade_x = smooth_curve(x_fraction);
    const auto fade_y = smooth_curve(y_fraction);
    const auto a = interpolate(
        fade_x,
        dot(channel_gradients[static_cast<std::size_t>(b00)],
            x_fraction, y_fraction),
        dot(channel_gradients[static_cast<std::size_t>(b10)],
            x_fraction_minus_one, y_fraction));
    const auto b = interpolate(
        fade_x,
        dot(channel_gradients[static_cast<std::size_t>(b01)],
            x_fraction, y_fraction_minus_one),
        dot(channel_gradients[static_cast<std::size_t>(b11)],
            x_fraction_minus_one, y_fraction_minus_one));
    return interpolate(fade_y, a, b);
  }

  static double smooth_curve(double value) {
    return value * value * (3.0 - 2.0 * value);
  }

  static double interpolate(double amount, double from, double to) {
    return from + amount * (to - from);
  }

  static double dot(const Gradient& gradient, double x, double y) {
    return x * gradient[0] + y * gradient[1];
  }

  std::array<int, kTableSize> permutation_{};
  std::array<std::array<Gradient, kTableSize>, 4> gradients_{};
};

double clamp_unit(double value) {
  return std::clamp(value, 0.0, 1.0);
}

double linear_to_srgb(double linear) {
  const auto value = clamp_unit(linear);
  return value <= 0.0031308 ? value * 12.92
                            : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

unsigned char byte_from_unit(double value) {
  return static_cast<unsigned char>(
      std::lround(clamp_unit(value) * 255.0));
}

bool numeric_variant(const QVariant& value, double* number) {
  if (number == nullptr) return false;
  switch (value.metaType().id()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Long:
    case QMetaType::Short:
    case QMetaType::Char:
    case QMetaType::ULong:
    case QMetaType::UShort:
    case QMetaType::UChar:
    case QMetaType::Float:
    case QMetaType::SChar:
      break;
    default:
      return false;
  }
  bool converted = false;
  const auto result = value.toDouble(&converted);
  if (!converted || !std::isfinite(result)) return false;
  *number = result;
  return true;
}

bool integer_variant(const QVariant& value,
                     std::int64_t minimum,
                     std::int64_t maximum,
                     std::int64_t* integer) {
  double number = 0.0;
  if (integer == nullptr || !numeric_variant(value, &number) ||
      std::trunc(number) != number ||
      number < static_cast<double>(minimum) ||
      number > static_cast<double>(maximum)) {
    return false;
  }
  *integer = static_cast<std::int64_t>(number);
  return true;
}

}  // namespace

namespace fractal_noise_detail {

std::int64_t park_miller_step(std::int64_t state) {
  auto result = kRandomMultiplier * (state % kRandomQuotient) -
                kRandomRemainder * (state / kRandomQuotient);
  if (result <= 0) result += kRandomModulus;
  return result;
}

double stitched_frequency(double frequency, double tile_extent) {
  if (!std::isfinite(frequency) || !std::isfinite(tile_extent) ||
      frequency <= 0.0 || tile_extent <= 0.0) {
    return frequency;
  }
  const auto low_periods = std::floor(tile_extent * frequency);
  const auto high_periods = std::ceil(tile_extent * frequency);
  const auto low_frequency = low_periods / tile_extent;
  const auto high_frequency = high_periods / tile_extent;
  if (low_frequency > 0.0 && frequency / low_frequency < high_frequency / frequency) {
    return low_frequency;
  }
  return high_frequency;
}

}  // namespace fractal_noise_detail

QString validate_fractal_noise_parameters(
    const FractalNoiseParameters& parameters) {
  if (!std::isfinite(parameters.base_frequency_x) ||
      !std::isfinite(parameters.base_frequency_y) ||
      parameters.base_frequency_x < 0.0 ||
      parameters.base_frequency_y < 0.0 ||
      parameters.base_frequency_x > kMaximumBaseFrequency ||
      parameters.base_frequency_y > kMaximumBaseFrequency) {
    return QStringLiteral(
        "SVG fractal-noise frequencies must be finite and between 0 and 64.");
  }
  if (parameters.num_octaves < 1 || parameters.num_octaves > kMaximumOctaves) {
    return QStringLiteral("SVG fractal noise supports 1 to 16 octaves.");
  }
  const auto has_output_width = parameters.output_width != 0;
  const auto has_output_height = parameters.output_height != 0;
  if (has_output_width != has_output_height) {
    return QStringLiteral(
        "outputWidth and outputHeight must be supplied together.");
  }
  if (has_output_width) {
    const auto output_size = QSize(parameters.output_width,
                                   parameters.output_height);
    const auto output_size_error = validate_fractal_noise_frame_size(output_size);
    if (!output_size_error.isEmpty()) return output_size_error;
  }
  return {};
}

QString validate_fractal_noise_frame_size(const QSize& viewport_size) {
  const auto pixel_count = static_cast<qint64>(viewport_size.width()) *
                           static_cast<qint64>(viewport_size.height());
  if (!viewport_size.isValid() || viewport_size.isEmpty() ||
      viewport_size.width() > kMaximumDimension ||
      viewport_size.height() > kMaximumDimension || pixel_count <= 0 ||
      pixel_count > kMaximumPixels) {
    return QStringLiteral(
        "The SVG fractal-noise output size is invalid or exceeds its pixel limit.");
  }
  return {};
}

QString validate_fractal_noise_output_size(
    const QSize& output_size,
    const FractalNoiseParameters& parameters) {
  const auto size_error = validate_fractal_noise_frame_size(output_size);
  if (!size_error.isEmpty()) return size_error;
  const auto parameter_error = validate_fractal_noise_parameters(parameters);
  if (!parameter_error.isEmpty()) return parameter_error;
  if ((parameters.output_width > 0 || parameters.output_height > 0) &&
      output_size != QSize(parameters.output_width, parameters.output_height)) {
    return QStringLiteral(
        "The requested fractal-noise output size does not match its typed parameters.");
  }
  return {};
}

QSize resolved_fractal_noise_output_size(
    const QSize& fallback_size,
    const FractalNoiseParameters& parameters) {
  if (parameters.output_width > 0 && parameters.output_height > 0) {
    return QSize(parameters.output_width, parameters.output_height);
  }
  return fallback_size;
}

QVariantMap canonical_fractal_noise_parameters(
    const FractalNoiseParameters& parameters) {
  if (!validate_fractal_noise_parameters(parameters).isEmpty() ||
      parameters.seed < -kMaximumExactJsonInteger ||
      parameters.seed > kMaximumExactJsonInteger) {
    return {};
  }
  QVariantMap canonical{
      {QStringLiteral("baseFrequencyX"), parameters.base_frequency_x},
      {QStringLiteral("baseFrequencyY"), parameters.base_frequency_y},
      {QStringLiteral("numOctaves"), parameters.num_octaves},
      {QStringLiteral("seed"), QVariant::fromValue<qlonglong>(parameters.seed)},
      {QStringLiteral("stitchTiles"), parameters.stitch_tiles}};
  if (parameters.output_width > 0 && parameters.output_height > 0) {
    canonical.insert(QStringLiteral("outputWidth"), parameters.output_width);
    canonical.insert(QStringLiteral("outputHeight"), parameters.output_height);
  }
  return canonical;
}

bool fractal_noise_parameters_from_variant_map(
    const QVariantMap& values,
    FractalNoiseParameters* parameters,
    QVariantMap* canonical_values,
    QString* error_message) {
  auto fail = [error_message](const QString& message) {
    if (error_message != nullptr) *error_message = message;
    return false;
  };
  if (parameters == nullptr) {
    return fail(QStringLiteral("A fractal-noise output parameter is required."));
  }
  const QSet<QString> allowed_keys{
      QStringLiteral("baseFrequencyX"), QStringLiteral("baseFrequencyY"),
      QStringLiteral("numOctaves"), QStringLiteral("seed"),
      QStringLiteral("stitchTiles"), QStringLiteral("outputWidth"),
      QStringLiteral("outputHeight")};
  for (auto iterator = values.cbegin(); iterator != values.cend(); ++iterator) {
    if (!allowed_keys.contains(iterator.key())) {
      return fail(QStringLiteral("Unsupported SVG fractal-noise parameter: %1")
                      .arg(iterator.key()));
    }
  }

  auto parsed = FractalNoiseParameters{};
  for (const auto& item : {
           std::pair<QString, double*>{QStringLiteral("baseFrequencyX"),
                                       &parsed.base_frequency_x},
           std::pair<QString, double*>{QStringLiteral("baseFrequencyY"),
                                       &parsed.base_frequency_y}}) {
    if (!values.contains(item.first)) continue;
    double number = 0.0;
    if (!numeric_variant(values.value(item.first), &number)) {
      return fail(QStringLiteral("%1 must be a finite numeric value.").arg(item.first));
    }
    *item.second = number;
  }
  if (values.contains(QStringLiteral("numOctaves"))) {
    std::int64_t octave_count = 0;
    if (!integer_variant(values.value(QStringLiteral("numOctaves")), 1,
                         kMaximumOctaves, &octave_count)) {
      return fail(QStringLiteral("numOctaves must be an integer from 1 to 16."));
    }
    parsed.num_octaves = static_cast<int>(octave_count);
  }
  if (values.contains(QStringLiteral("seed")) &&
      !integer_variant(values.value(QStringLiteral("seed")),
                       -kMaximumExactJsonInteger, kMaximumExactJsonInteger,
                       &parsed.seed)) {
    return fail(QStringLiteral("seed must be an exactly representable integer."));
  }
  if (values.contains(QStringLiteral("stitchTiles"))) {
    const auto stitch_value = values.value(QStringLiteral("stitchTiles"));
    if (stitch_value.metaType().id() != QMetaType::Bool) {
      return fail(QStringLiteral("stitchTiles must be a boolean."));
    }
    parsed.stitch_tiles = stitch_value.toBool();
  }
  const auto has_output_width = values.contains(QStringLiteral("outputWidth"));
  const auto has_output_height = values.contains(QStringLiteral("outputHeight"));
  if (has_output_width != has_output_height) {
    return fail(QStringLiteral(
        "outputWidth and outputHeight must be supplied together."));
  }
  if (has_output_width) {
    std::int64_t output_width = 0;
    std::int64_t output_height = 0;
    if (!integer_variant(values.value(QStringLiteral("outputWidth")), 1,
                         kMaximumDimension, &output_width) ||
        !integer_variant(values.value(QStringLiteral("outputHeight")), 1,
                         kMaximumDimension, &output_height)) {
      return fail(QStringLiteral(
          "outputWidth and outputHeight must be integers from 1 to 16384."));
    }
    parsed.output_width = static_cast<int>(output_width);
    parsed.output_height = static_cast<int>(output_height);
  }

  const auto validation_error = validate_fractal_noise_parameters(parsed);
  if (!validation_error.isEmpty()) return fail(validation_error);
  const auto canonical = canonical_fractal_noise_parameters(parsed);
  if (canonical.isEmpty()) {
    return fail(QStringLiteral("The SVG fractal-noise parameters cannot be canonicalized."));
  }
  *parameters = parsed;
  if (canonical_values != nullptr) *canonical_values = canonical;
  if (error_message != nullptr) error_message->clear();
  return true;
}

QImage render_fractal_noise_frame(const QSize& viewport_size,
                                  const FractalNoiseParameters& parameters,
                                  QString* error_message,
                                  const std::function<bool()>& cancellation_check) {
  const auto validation_error = validate_fractal_noise_parameters(parameters);
  const auto size_error = validate_fractal_noise_output_size(viewport_size,
                                                             parameters);
  if (!validation_error.isEmpty() || !size_error.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = validation_error.isEmpty()
                           ? size_error
                           : validation_error;
    }
    return {};
  }

  QImage output(viewport_size, QImage::Format_RGBA8888);
  if (output.isNull()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Cannot allocate the SVG fractal-noise output image.");
    }
    return {};
  }

  const TurbulenceNoise noise(parameters.seed);
  // SVG filter defaults expand the object bounding box by 10% on each side.
  const auto tile_x = -static_cast<double>(viewport_size.width()) * 0.1;
  const auto tile_y = -static_cast<double>(viewport_size.height()) * 0.1;
  const auto tile_width = static_cast<double>(viewport_size.width()) * 1.2;
  const auto tile_height = static_cast<double>(viewport_size.height()) * 1.2;
  const auto frequency_x = parameters.stitch_tiles
      ? fractal_noise_detail::stitched_frequency(
            parameters.base_frequency_x, tile_width)
      : parameters.base_frequency_x;
  const auto frequency_y = parameters.stitch_tiles
      ? fractal_noise_detail::stitched_frequency(
            parameters.base_frequency_y, tile_height)
      : parameters.base_frequency_y;
  const auto luma_r = 0.213;
  const auto luma_g = 0.715;
  const auto luma_b = 0.072;

  for (int y = 0; y < viewport_size.height(); ++y) {
    if (cancellation_check && cancellation_check()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("SVG fractal-noise rendering was cancelled.");
      }
      return {};
    }
    auto* row = output.scanLine(y);
    for (int x = 0; x < viewport_size.width(); ++x) {
      const auto point_x = static_cast<double>(x) + 0.5;
      const auto point_y = static_cast<double>(y) + 0.5;
      const auto red_noise = noise.fractal_sum(
          0, point_x, point_y, frequency_x,
          frequency_y, parameters.num_octaves,
          parameters.stitch_tiles, tile_x, tile_y, tile_width, tile_height);
      const auto green_noise = noise.fractal_sum(
          1, point_x, point_y, frequency_x,
          frequency_y, parameters.num_octaves,
          parameters.stitch_tiles, tile_x, tile_y, tile_width, tile_height);
      const auto blue_noise = noise.fractal_sum(
          2, point_x, point_y, frequency_x,
          frequency_y, parameters.num_octaves,
          parameters.stitch_tiles, tile_x, tile_y, tile_width, tile_height);
      const auto alpha_noise = noise.fractal_sum(
          3, point_x, point_y, frequency_x,
          frequency_y, parameters.num_octaves,
          parameters.stitch_tiles, tile_x, tile_y, tile_width, tile_height);
      const auto linear_gray = clamp_unit(
          (red_noise * luma_r + green_noise * luma_g + blue_noise * luma_b +
           1.0) *
          0.5);
      const auto srgb_gray = linear_to_srgb(linear_gray);
      const auto pixel_offset = static_cast<std::size_t>(x) * 4U;
      const auto gray = byte_from_unit(srgb_gray);
      row[pixel_offset] = gray;
      row[pixel_offset + 1U] = gray;
      row[pixel_offset + 2U] = gray;
      row[pixel_offset + 3U] = byte_from_unit((alpha_noise + 1.0) * 0.5);
    }
  }
  if (error_message != nullptr) error_message->clear();
  return output;
}

}  // namespace photo_editor::desktop::timeline_motion
