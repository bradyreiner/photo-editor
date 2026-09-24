#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <vector>

#include <QColorSpace>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>

#include "photo_editor/pipeline/color_metrics.h"

namespace {

constexpr auto kReportSchema = "photo-editor-color-corpus-comparison:v1";
constexpr auto kToolVersion = "1";
constexpr std::size_t kHistogramBins = 65536;
constexpr double kDeltaE00HistogramLimit = 200.0;
constexpr double kDeltaEItpHistogramLimit = 1000.0;
constexpr int kMaximumHeatmapEdge = 256;
constexpr int kMaximumRegions = 64;

struct Pixel final {
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
  double alpha = 0.0;
};

struct Region final {
  QString label;
  QString expected_content;
  QRect pixel_rect;
  std::array<double, 4> normalized_rect{};
};

struct Summary final {
  std::uint64_t count = 0;
  double mean = 0.0;
  double p50_upper_bound = 0.0;
  double p95_upper_bound = 0.0;
  double p99_upper_bound = 0.0;
  double maximum = 0.0;
};

class MetricAccumulator final {
 public:
  explicit MetricAccumulator(double histogram_limit, double gate_threshold)
      : histogram_limit_(histogram_limit),
        gate_threshold_(gate_threshold),
        bins_(kHistogramBins, 0) {}

  void add(double value) {
    ++count_;
    sum_ += static_cast<long double>(value);
    maximum_ = std::max(maximum_, value);
    if (value > gate_threshold_) {
      ++above_gate_count_;
    }
    const auto normalized = std::clamp(value / histogram_limit_, 0.0, 1.0);
    const auto scaled = normalized * static_cast<double>(bins_.size() - 1);
    const auto index = static_cast<std::size_t>(std::floor(scaled));
    ++bins_[index];
  }

  [[nodiscard]] Summary summary() const {
    return Summary{
        .count = count_,
        .mean = count_ == 0
                    ? 0.0
                    : static_cast<double>(sum_ / static_cast<long double>(count_)),
        .p50_upper_bound = percentile_upper_bound(0.50),
        .p95_upper_bound = percentile_upper_bound(0.95),
        .p99_upper_bound = percentile_upper_bound(0.99),
        .maximum = maximum_,
    };
  }

  [[nodiscard]] bool percentile_gate_passes(double percentile) const {
    if (count_ == 0) {
      return false;
    }
    const auto required_at_or_below = static_cast<std::uint64_t>(
        std::ceil(percentile * static_cast<double>(count_)));
    return count_ - above_gate_count_ >= required_at_or_below;
  }

  [[nodiscard]] std::uint64_t above_gate_count() const { return above_gate_count_; }

 private:
  [[nodiscard]] double percentile_upper_bound(double percentile) const {
    if (count_ == 0) {
      return 0.0;
    }
    const auto rank = static_cast<std::uint64_t>(
        std::ceil(percentile * static_cast<double>(count_)));
    std::uint64_t cumulative = 0;
    for (std::size_t index = 0; index < bins_.size(); ++index) {
      cumulative += bins_[index];
      if (cumulative >= rank) {
        if (index + 1 == bins_.size() && maximum_ > histogram_limit_) {
          return maximum_;
        }
        return histogram_limit_ * static_cast<double>(index + 1) /
               static_cast<double>(bins_.size());
      }
    }
    return maximum_;
  }

  double histogram_limit_ = 0.0;
  double gate_threshold_ = 0.0;
  std::vector<std::uint64_t> bins_;
  std::uint64_t count_ = 0;
  std::uint64_t above_gate_count_ = 0;
  long double sum_ = 0.0;
  double maximum_ = 0.0;
};

struct RegionMetrics final {
  explicit RegionMetrics(const Region& source)
      : region(source),
        delta_e_00(kDeltaE00HistogramLimit, std::numeric_limits<double>::max()),
        delta_e_itp(kDeltaEItpHistogramLimit, std::numeric_limits<double>::max()) {}

  Region region;
  MetricAccumulator delta_e_00;
  MetricAccumulator delta_e_itp;
};

struct WorstPixel final {
  double value = -1.0;
  int x = 0;
  int y = 0;
  Pixel candidate;
  Pixel reference;
};

struct Thresholds final {
  double p95_delta_e_00 = 0.0;
  double max_delta_e_00 = 0.0;
  double p95_delta_e_itp = 0.0;
  double max_delta_e_itp = 0.0;
  double max_alpha_error = 0.0;
};

struct Provenance final {
  QString execution_path;
  QString capture_id;
  QString candidate_rendering_model;
  QString reference_rendering_model;
  QString recipe_sha256;
  QString output_profile_sha256;
  QString corpus_manifest_sha256;
};

struct CaptureContract final {
  std::vector<Region> regions;
  QString recipe_sha256;
  QString recipe_rendering_model;
  std::vector<QString> profile_sha256_values;
  Thresholds thresholds;
  double linear_one_nits = 0.0;
};

struct ComparisonResult final {
  ComparisonResult(const Thresholds& thresholds, const std::vector<Region>& regions)
      : delta_e_00(kDeltaE00HistogramLimit, thresholds.p95_delta_e_00),
        delta_e_itp(kDeltaEItpHistogramLimit, thresholds.p95_delta_e_itp),
        alpha_error(1.0, thresholds.max_alpha_error) {
    region_metrics.reserve(regions.size());
    for (const auto& region : regions) {
      region_metrics.emplace_back(region);
    }
  }

  MetricAccumulator delta_e_00;
  MetricAccumulator delta_e_itp;
  MetricAccumulator alpha_error;
  std::vector<RegionMetrics> region_metrics;
  WorstPixel worst_delta_e_00;
  WorstPixel worst_delta_e_itp;
  WorstPixel worst_alpha;
  QImage heatmap;
  bool finite = true;
  QString failure;
};

[[nodiscard]] QString file_sha256(const QString& path) {
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&input)) {
    return {};
  }
  return QString::fromLatin1(hash.result().toHex());
}

[[nodiscard]] bool is_sha256(const QString& value) {
  if (value.size() != 64) {
    return false;
  }
  return std::all_of(value.cbegin(), value.cend(), [](QChar character) {
    return (character >= QLatin1Char('0') && character <= QLatin1Char('9')) ||
           (character >= QLatin1Char('a') && character <= QLatin1Char('f'));
  });
}

[[nodiscard]] Pixel pixel_at(const QImage& image, int x, int y) {
  const auto* row = reinterpret_cast<const float*>(image.constScanLine(y));
  const auto offset = static_cast<std::size_t>(x) * 4;
  return Pixel{
      .red = static_cast<double>(row[offset]),
      .green = static_cast<double>(row[offset + 1]),
      .blue = static_cast<double>(row[offset + 2]),
      .alpha = static_cast<double>(row[offset + 3]),
  };
}

[[nodiscard]] bool finite_pixel(const Pixel& pixel) {
  return std::isfinite(pixel.red) && std::isfinite(pixel.green) &&
         std::isfinite(pixel.blue) && std::isfinite(pixel.alpha);
}

[[nodiscard]] photo_editor::pipeline::CieLab rec2020_to_lab(const Pixel& pixel) {
  return photo_editor::pipeline::cie_xyz_to_lab(
      {
          .x = 0.6369580483 * pixel.red + 0.1446169036 * pixel.green +
               0.1688809752 * pixel.blue,
          .y = 0.2627002120 * pixel.red + 0.6779980715 * pixel.green +
               0.0593017165 * pixel.blue,
          .z = 0.0280726930 * pixel.green + 1.0609850577 * pixel.blue,
      },
      photo_editor::pipeline::cie_d65_white());
}

[[nodiscard]] photo_editor::pipeline::IctcpColor rec2020_to_ictcp(
    const Pixel& pixel,
    double linear_one_nits) {
  return photo_editor::pipeline::rec2020_nits_to_ictcp({
      .red = pixel.red * linear_one_nits,
      .green = pixel.green * linear_one_nits,
      .blue = pixel.blue * linear_one_nits,
  });
}

[[nodiscard]] QRgb false_color(double normalized) {
  const auto value = std::clamp(normalized, 0.0, 1.0);
  struct Stop final {
    double position;
    std::array<int, 3> rgb;
  };
  constexpr std::array<Stop, 5> stops = {{
      {0.00, {12, 18, 48}},
      {0.25, {0, 166, 214}},
      {0.50, {59, 201, 92}},
      {0.75, {255, 214, 10}},
      {1.00, {220, 30, 38}},
  }};
  for (std::size_t index = 1; index < stops.size(); ++index) {
    if (value <= stops[index].position) {
      const auto& left = stops[index - 1];
      const auto& right = stops[index];
      const auto amount =
          (value - left.position) / (right.position - left.position);
      const auto interpolate = [amount](int first, int second) {
        return static_cast<int>(std::lround(
            static_cast<double>(first) +
            (static_cast<double>(second) - static_cast<double>(first)) * amount));
      };
      return qRgb(
          interpolate(left.rgb[0], right.rgb[0]),
          interpolate(left.rgb[1], right.rgb[1]),
          interpolate(left.rgb[2], right.rgb[2]));
    }
  }
  return qRgb(220, 30, 38);
}

void update_worst(
    WorstPixel* worst,
    double value,
    int x,
    int y,
    const Pixel& candidate,
    const Pixel& reference) {
  if (worst != nullptr && value > worst->value) {
    *worst = WorstPixel{
        .value = value,
        .x = x,
        .y = y,
        .candidate = candidate,
        .reference = reference,
    };
  }
}

[[nodiscard]] ComparisonResult compare_images(
    const QImage& candidate,
    const QImage& reference,
    const std::vector<Region>& regions,
    const Thresholds& thresholds,
    double linear_one_nits) {
  ComparisonResult result(thresholds, regions);
  if (candidate.isNull() || reference.isNull()) {
    result.failure = QStringLiteral("candidate and reference images must decode");
    return result;
  }
  if (candidate.size() != reference.size()) {
    result.failure = QStringLiteral("candidate and reference dimensions differ");
    return result;
  }
  if (candidate.format() != QImage::Format_RGBA32FPx4 ||
      reference.format() != QImage::Format_RGBA32FPx4) {
    result.failure = QStringLiteral("internal comparison images must be RGBA32FPx4");
    return result;
  }

  const auto heatmap_width = std::min(candidate.width(), kMaximumHeatmapEdge);
  const auto heatmap_height = std::min(candidate.height(), kMaximumHeatmapEdge);
  std::vector<double> heatmap_values(
      static_cast<std::size_t>(heatmap_width) *
          static_cast<std::size_t>(heatmap_height),
      0.0);
  std::vector<std::vector<std::size_t>> active_regions(
      static_cast<std::size_t>(candidate.height()));
  for (std::size_t index = 0; index < regions.size(); ++index) {
    const auto top = std::max(0, regions[index].pixel_rect.top());
    const auto bottom = std::min(candidate.height() - 1, regions[index].pixel_rect.bottom());
    for (auto y = top; y <= bottom; ++y) {
      active_regions[static_cast<std::size_t>(y)].push_back(index);
    }
  }

  for (auto y = 0; y < candidate.height(); ++y) {
    for (auto x = 0; x < candidate.width(); ++x) {
      const auto candidate_pixel = pixel_at(candidate, x, y);
      const auto reference_pixel = pixel_at(reference, x, y);
      if (!finite_pixel(candidate_pixel) || !finite_pixel(reference_pixel)) {
        result.finite = false;
        result.failure = QStringLiteral("non-finite pixel encountered at (%1, %2)")
                             .arg(x)
                             .arg(y);
        return result;
      }
      const auto delta_e_00 = photo_editor::pipeline::delta_e_2000(
          rec2020_to_lab(candidate_pixel), rec2020_to_lab(reference_pixel));
      const auto delta_e_itp = photo_editor::pipeline::delta_e_itp(
          rec2020_to_ictcp(candidate_pixel, linear_one_nits),
          rec2020_to_ictcp(reference_pixel, linear_one_nits));
      const auto alpha_error = std::abs(candidate_pixel.alpha - reference_pixel.alpha);
      result.delta_e_00.add(delta_e_00);
      result.delta_e_itp.add(delta_e_itp);
      result.alpha_error.add(alpha_error);
      update_worst(
          &result.worst_delta_e_00,
          delta_e_00,
          x,
          y,
          candidate_pixel,
          reference_pixel);
      update_worst(
          &result.worst_delta_e_itp,
          delta_e_itp,
          x,
          y,
          candidate_pixel,
          reference_pixel);
      update_worst(
          &result.worst_alpha,
          alpha_error,
          x,
          y,
          candidate_pixel,
          reference_pixel);

      for (const auto region_index : active_regions[static_cast<std::size_t>(y)]) {
        auto& region = result.region_metrics[region_index];
        if (region.region.pixel_rect.contains(x, y)) {
          region.delta_e_00.add(delta_e_00);
          region.delta_e_itp.add(delta_e_itp);
        }
      }

      const auto heatmap_x = static_cast<int>(
          static_cast<long long>(x) * static_cast<long long>(heatmap_width) /
          static_cast<long long>(candidate.width()));
      const auto heatmap_y = static_cast<int>(
          static_cast<long long>(y) * static_cast<long long>(heatmap_height) /
          static_cast<long long>(candidate.height()));
      const auto score = std::max(
          delta_e_00 / thresholds.max_delta_e_00,
          delta_e_itp / thresholds.max_delta_e_itp);
      const auto heatmap_index =
          static_cast<std::size_t>(heatmap_y) * static_cast<std::size_t>(heatmap_width) +
          static_cast<std::size_t>(heatmap_x);
      heatmap_values[heatmap_index] = std::max(heatmap_values[heatmap_index], score);
    }
  }

  result.heatmap = QImage(heatmap_width, heatmap_height, QImage::Format_RGB32);
  for (auto y = 0; y < heatmap_height; ++y) {
    auto* row = reinterpret_cast<QRgb*>(result.heatmap.scanLine(y));
    for (auto x = 0; x < heatmap_width; ++x) {
      const auto index = static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(heatmap_width) +
                         static_cast<std::size_t>(x);
      row[x] = false_color(heatmap_values[index]);
    }
  }
  return result;
}

[[nodiscard]] QJsonArray pixel_json(const Pixel& pixel) {
  return {pixel.red, pixel.green, pixel.blue, pixel.alpha};
}

[[nodiscard]] QJsonObject summary_json(
    const MetricAccumulator& accumulator,
    double threshold,
    bool percentile_gate) {
  const auto summary = accumulator.summary();
  QJsonObject object{
      {QStringLiteral("count"), static_cast<qint64>(summary.count)},
      {QStringLiteral("mean"), summary.mean},
      {QStringLiteral("p50_upper_bound"), summary.p50_upper_bound},
      {QStringLiteral("p95_upper_bound"), summary.p95_upper_bound},
      {QStringLiteral("p99_upper_bound"), summary.p99_upper_bound},
      {QStringLiteral("max"), summary.maximum},
      {QStringLiteral("threshold"), threshold},
  };
  if (percentile_gate) {
    object.insert(
        QStringLiteral("above_threshold_count"),
        static_cast<qint64>(accumulator.above_gate_count()));
    object.insert(
        QStringLiteral("gate_passed"),
        accumulator.percentile_gate_passes(0.95));
  } else {
    object.insert(
        QStringLiteral("gate_passed"),
        summary.maximum <= threshold);
  }
  return object;
}

[[nodiscard]] QJsonObject worst_json(
    const WorstPixel& worst,
    int width,
    int height) {
  return {
      {QStringLiteral("value"), worst.value},
      {QStringLiteral("x"), worst.x},
      {QStringLiteral("y"), worst.y},
      {QStringLiteral("normalized_x"),
       (static_cast<double>(worst.x) + 0.5) / static_cast<double>(width)},
      {QStringLiteral("normalized_y"),
       (static_cast<double>(worst.y) + 0.5) / static_cast<double>(height)},
      {QStringLiteral("candidate_linear_bt2020_rgba"), pixel_json(worst.candidate)},
      {QStringLiteral("reference_linear_bt2020_rgba"), pixel_json(worst.reference)},
  };
}

[[nodiscard]] QJsonObject file_json(const QString& path) {
  const QFileInfo info(path);
  return {
      {QStringLiteral("path"), info.absoluteFilePath()},
      {QStringLiteral("byte_size"), info.size()},
      {QStringLiteral("sha256"), file_sha256(path)},
  };
}

[[nodiscard]] QJsonObject build_report(
    const QString& candidate_path,
    const QString& reference_path,
    const QString& heatmap_path,
    const QImage& candidate,
    const ComparisonResult& result,
    const Thresholds& thresholds,
    double linear_one_nits,
    const Provenance& provenance) {
  const auto delta_e_00_summary = result.delta_e_00.summary();
  const auto delta_e_itp_summary = result.delta_e_itp.summary();
  const auto alpha_summary = result.alpha_error.summary();
  const auto p95_de00_pass = result.delta_e_00.percentile_gate_passes(0.95);
  const auto p95_deitp_pass = result.delta_e_itp.percentile_gate_passes(0.95);
  const auto passed = result.failure.isEmpty() && result.finite && p95_de00_pass &&
                      p95_deitp_pass &&
                      delta_e_00_summary.maximum <= thresholds.max_delta_e_00 &&
                      delta_e_itp_summary.maximum <= thresholds.max_delta_e_itp &&
                      alpha_summary.maximum <= thresholds.max_alpha_error;

  QJsonArray region_array;
  for (const auto& region : result.region_metrics) {
    region_array.append(QJsonObject{
        {QStringLiteral("label"), region.region.label},
        {QStringLiteral("expected_content"), region.region.expected_content},
        {QStringLiteral("normalized_rect"),
         QJsonArray{
             region.region.normalized_rect[0],
             region.region.normalized_rect[1],
             region.region.normalized_rect[2],
             region.region.normalized_rect[3],
         }},
        {QStringLiteral("delta_e_00"),
         summary_json(region.delta_e_00, thresholds.max_delta_e_00, false)},
        {QStringLiteral("delta_e_itp"),
         summary_json(region.delta_e_itp, thresholds.max_delta_e_itp, false)},
    });
  }

  return {
      {QStringLiteral("schema"), QString::fromLatin1(kReportSchema)},
      {QStringLiteral("tool_version"), QString::fromLatin1(kToolVersion)},
      {QStringLiteral("passed"), passed},
      {QStringLiteral("failure"), result.failure},
      {QStringLiteral("comparison_space"), QStringLiteral("linear Rec.2020 D65")},
      {QStringLiteral("linear_one_nits"), linear_one_nits},
      {QStringLiteral("percentile_method"),
       QStringLiteral("fixed-histogram upper bound; threshold gate uses exact exceedance count")},
      {QStringLiteral("dimensions"),
       QJsonObject{
           {QStringLiteral("width"), candidate.width()},
           {QStringLiteral("height"), candidate.height()},
       }},
      {QStringLiteral("provenance"),
       QJsonObject{
           {QStringLiteral("execution_path"), provenance.execution_path},
           {QStringLiteral("capture_id"), provenance.capture_id},
           {QStringLiteral("candidate_rendering_model"),
            provenance.candidate_rendering_model},
           {QStringLiteral("reference_rendering_model"),
            provenance.reference_rendering_model},
           {QStringLiteral("recipe_sha256"), provenance.recipe_sha256},
           {QStringLiteral("output_profile_sha256"), provenance.output_profile_sha256},
           {QStringLiteral("corpus_manifest_sha256"), provenance.corpus_manifest_sha256},
       }},
      {QStringLiteral("candidate"), file_json(candidate_path)},
      {QStringLiteral("reference"), file_json(reference_path)},
      {QStringLiteral("heatmap"), file_json(heatmap_path)},
      {QStringLiteral("metrics"),
       QJsonObject{
           {QStringLiteral("delta_e_00_p95"),
            summary_json(result.delta_e_00, thresholds.p95_delta_e_00, true)},
           {QStringLiteral("delta_e_00_max"),
            summary_json(result.delta_e_00, thresholds.max_delta_e_00, false)},
           {QStringLiteral("delta_e_itp_p95"),
            summary_json(result.delta_e_itp, thresholds.p95_delta_e_itp, true)},
           {QStringLiteral("delta_e_itp_max"),
            summary_json(result.delta_e_itp, thresholds.max_delta_e_itp, false)},
           {QStringLiteral("alpha_error_max"),
            summary_json(result.alpha_error, thresholds.max_alpha_error, false)},
       }},
      {QStringLiteral("worst_pixel"),
       QJsonObject{
           {QStringLiteral("delta_e_00"),
            worst_json(result.worst_delta_e_00, candidate.width(), candidate.height())},
           {QStringLiteral("delta_e_itp"),
            worst_json(result.worst_delta_e_itp, candidate.width(), candidate.height())},
           {QStringLiteral("alpha_error"),
            worst_json(result.worst_alpha, candidate.width(), candidate.height())},
       }},
      {QStringLiteral("regions"), region_array},
  };
}

[[nodiscard]] bool write_json(const QString& path, const QJsonObject& object) {
  QSaveFile output(path);
  if (!output.open(QIODevice::WriteOnly)) {
    return false;
  }
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
  if (output.write(bytes) != bytes.size()) {
    return false;
  }
  return output.commit();
}

[[nodiscard]] std::optional<CaptureContract> load_capture_contract(
    const QString& corpus_manifest_path,
    const QString& capture_id,
    const QString& execution_path,
    const QSize& image_size,
    QString* error) {
  QFile input(corpus_manifest_path);
  if (!input.open(QIODevice::ReadOnly)) {
    *error = QStringLiteral("unable to read corpus manifest");
    return std::nullopt;
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(input.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    *error = QStringLiteral("corpus manifest is not valid JSON");
    return std::nullopt;
  }
  const auto manifest = document.object();
  const auto assets = manifest.value(QStringLiteral("assets")).toArray();
  const auto gate = manifest.value(QStringLiteral("quality_gates"))
                        .toObject()
                        .value(execution_path)
                        .toObject();
  const auto positive_gate_value = [&gate](const QString& key) -> std::optional<double> {
    const auto value = gate.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() <= 0.0) {
      return std::nullopt;
    }
    return value.toDouble();
  };
  const auto linear_one_nits = positive_gate_value(QStringLiteral("linear_one_nits"));
  const auto p95_de00 = positive_gate_value(QStringLiteral("p95_delta_e_00"));
  const auto max_de00 = positive_gate_value(QStringLiteral("max_delta_e_00"));
  const auto p95_deitp = positive_gate_value(QStringLiteral("p95_delta_e_itp"));
  const auto max_deitp = positive_gate_value(QStringLiteral("max_delta_e_itp"));
  const auto max_alpha = positive_gate_value(QStringLiteral("max_alpha_error"));
  if (!linear_one_nits.has_value() || !p95_de00.has_value() ||
      !max_de00.has_value() || !p95_deitp.has_value() ||
      !max_deitp.has_value() || !max_alpha.has_value() ||
      *linear_one_nits > 10000.0 || *max_alpha > 1.0 ||
      *max_de00 < *p95_de00 || *max_deitp < *p95_deitp) {
    *error = QStringLiteral("execution path has no valid precommitted quality gate");
    return std::nullopt;
  }
  QJsonObject capture;
  for (const auto& value : assets) {
    const auto object = value.toObject();
    if (object.value(QStringLiteral("kind")).toString() == QStringLiteral("capture") &&
        object.value(QStringLiteral("asset_id")).toString() == capture_id) {
      capture = object;
      break;
    }
  }
  if (capture.isEmpty()) {
    *error = QStringLiteral("capture_id is not present in corpus manifest");
    return std::nullopt;
  }

  const auto find_asset = [&assets](const QString& asset_id) {
    for (const auto& value : assets) {
      const auto object = value.toObject();
      if (object.value(QStringLiteral("asset_id")).toString() == asset_id) {
        return object;
      }
    }
    return QJsonObject{};
  };
  const auto recipe_id = capture.value(QStringLiteral("recipe_id")).toString();
  const auto recipe = find_asset(recipe_id);
  const auto recipe_sha = recipe.value(QStringLiteral("sha256")).toString();
  const auto recipe_model = recipe.value(QStringLiteral("rendering_model")).toString();
  if (recipe.value(QStringLiteral("kind")).toString() != QStringLiteral("recipe") ||
      !is_sha256(recipe_sha) || recipe_model.isEmpty()) {
    *error = QStringLiteral("capture recipe link is incomplete or invalid");
    return std::nullopt;
  }
  std::vector<QString> profile_hashes;
  const auto profile_ids = capture.value(QStringLiteral("profile_ids")).toArray();
  for (const auto& profile_id_value : profile_ids) {
    const auto profile = find_asset(profile_id_value.toString());
    const auto profile_sha = profile.value(QStringLiteral("sha256")).toString();
    if (profile.value(QStringLiteral("kind")).toString() != QStringLiteral("profile") ||
        !is_sha256(profile_sha)) {
      *error = QStringLiteral("capture profile link is incomplete or invalid");
      return std::nullopt;
    }
    profile_hashes.push_back(profile_sha);
  }
  if (profile_hashes.empty()) {
    *error = QStringLiteral("capture must link at least one immutable profile");
    return std::nullopt;
  }
  const auto values = capture.value(QStringLiteral("review_regions")).toArray();
  if (values.isEmpty() || values.size() > kMaximumRegions) {
    *error = QStringLiteral("capture must define between 1 and %1 review regions")
                 .arg(kMaximumRegions);
    return std::nullopt;
  }
  std::vector<Region> regions;
  regions.reserve(static_cast<std::size_t>(values.size()));
  for (const auto& value : values) {
    const auto object = value.toObject();
    const auto x = object.value(QStringLiteral("x")).toDouble(-1.0);
    const auto y = object.value(QStringLiteral("y")).toDouble(-1.0);
    const auto width = object.value(QStringLiteral("width")).toDouble(-1.0);
    const auto height = object.value(QStringLiteral("height")).toDouble(-1.0);
    if (x < 0.0 || y < 0.0 || width <= 0.0 || height <= 0.0 ||
        x + width > 1.0 || y + height > 1.0) {
      *error = QStringLiteral("corpus review region is outside normalized image bounds");
      return std::nullopt;
    }
    const auto left = static_cast<int>(std::floor(x * image_size.width()));
    const auto top = static_cast<int>(std::floor(y * image_size.height()));
    const auto right = static_cast<int>(std::ceil((x + width) * image_size.width()));
    const auto bottom = static_cast<int>(std::ceil((y + height) * image_size.height()));
    regions.push_back(Region{
        .label = object.value(QStringLiteral("label")).toString(),
        .expected_content =
            object.value(QStringLiteral("expected_content")).toString(),
        .pixel_rect = QRect(
            left,
            top,
            std::max(1, right - left),
            std::max(1, bottom - top)),
        .normalized_rect = {x, y, width, height},
    });
  }
  return CaptureContract{
      .regions = std::move(regions),
      .recipe_sha256 = recipe_sha,
      .recipe_rendering_model = recipe_model,
      .profile_sha256_values = std::move(profile_hashes),
      .thresholds = Thresholds{
          .p95_delta_e_00 = *p95_de00,
          .max_delta_e_00 = *max_de00,
          .p95_delta_e_itp = *p95_deitp,
          .max_delta_e_itp = *max_deitp,
          .max_alpha_error = *max_alpha,
      },
      .linear_one_nits = *linear_one_nits,
  };
}

[[nodiscard]] std::optional<QImage> load_linear_bt2020(
    const QString& path,
    QString* error) {
  QImage image(path);
  if (image.isNull()) {
    *error = QStringLiteral("unable to decode image: %1").arg(path);
    return std::nullopt;
  }
  if (!image.colorSpace().isValid()) {
    *error = QStringLiteral("image has no valid embedded color space: %1").arg(path);
    return std::nullopt;
  }
  const QColorSpace linear_bt2020(
      QColorSpace::Primaries::Bt2020,
      QColorSpace::TransferFunction::Linear);
  auto converted = image.convertedToColorSpace(
      linear_bt2020,
      QImage::Format_RGBA32FPx4,
      Qt::AutoColor);
  if (converted.isNull()) {
    *error = QStringLiteral("unable to convert image to linear Rec.2020: %1").arg(path);
    return std::nullopt;
  }
  return converted;
}

[[nodiscard]] int self_test() {
  QTemporaryDir temporary;
  if (!temporary.isValid()) {
    return EXIT_FAILURE;
  }
  const QColorSpace linear_bt2020(
      QColorSpace::Primaries::Bt2020,
      QColorSpace::TransferFunction::Linear);
  QImage reference(16, 8, QImage::Format_RGBA32FPx4);
  QImage candidate(16, 8, QImage::Format_RGBA32FPx4);
  reference.setColorSpace(linear_bt2020);
  candidate.setColorSpace(linear_bt2020);
  for (auto y = 0; y < reference.height(); ++y) {
    auto* reference_row = reinterpret_cast<float*>(reference.scanLine(y));
    auto* candidate_row = reinterpret_cast<float*>(candidate.scanLine(y));
    for (auto x = 0; x < reference.width(); ++x) {
      const auto offset = static_cast<std::size_t>(x) * 4;
      for (std::size_t channel = 0; channel < 3; ++channel) {
        reference_row[offset + channel] = 0.18F;
        candidate_row[offset + channel] = 0.18F;
      }
      reference_row[offset + 3] = 1.0F;
      candidate_row[offset + 3] = 1.0F;
    }
  }
  auto* changed_row = reinterpret_cast<float*>(candidate.scanLine(3));
  changed_row[static_cast<std::size_t>(7) * 4] = 0.28F;

  const auto candidate_path = temporary.filePath(QStringLiteral("candidate.png"));
  const auto reference_path = temporary.filePath(QStringLiteral("reference.png"));
  auto encoded_candidate = candidate.convertToFormat(QImage::Format_RGBA64);
  auto encoded_reference = reference.convertToFormat(QImage::Format_RGBA64);
  encoded_candidate.setColorSpace(linear_bt2020);
  encoded_reference.setColorSpace(linear_bt2020);
  if (!encoded_candidate.save(candidate_path, "PNG") ||
      !encoded_reference.save(reference_path, "PNG")) {
    return EXIT_FAILURE;
  }
  QString load_error;
  const auto loaded_candidate = load_linear_bt2020(candidate_path, &load_error);
  const auto loaded_reference = load_linear_bt2020(reference_path, &load_error);
  if (!loaded_candidate.has_value() || !loaded_reference.has_value()) {
    return EXIT_FAILURE;
  }

  const auto corpus_path = temporary.filePath(QStringLiteral("manifest.json"));
  const auto hash = QString(64, QLatin1Char('a'));
  const QJsonObject region_json{
      {QStringLiteral("label"), QStringLiteral("self-test region")},
      {QStringLiteral("expected_content"),
       QStringLiteral("single deterministic changed pixel")},
      {QStringLiteral("x"), 0.0},
      {QStringLiteral("y"), 0.0},
      {QStringLiteral("width"), 1.0},
      {QStringLiteral("height"), 1.0},
  };
  const QJsonObject corpus_json{
      {QStringLiteral("quality_gates"),
       QJsonObject{
           {QStringLiteral("preview"),
            QJsonObject{
                {QStringLiteral("linear_one_nits"), 100.0},
                {QStringLiteral("p95_delta_e_00"), 0.01},
                {QStringLiteral("max_delta_e_00"), 50.0},
                {QStringLiteral("p95_delta_e_itp"), 0.01},
                {QStringLiteral("max_delta_e_itp"), 100.0},
                {QStringLiteral("max_alpha_error"), 0.001},
            }},
       }},
      {QStringLiteral("assets"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("kind"), QStringLiteral("capture")},
               {QStringLiteral("asset_id"), QStringLiteral("self-test-capture")},
               {QStringLiteral("recipe_id"), QStringLiteral("self-test-recipe")},
               {QStringLiteral("profile_ids"),
                QJsonArray{QStringLiteral("self-test-profile")}},
               {QStringLiteral("review_regions"), QJsonArray{region_json}},
           },
           QJsonObject{
               {QStringLiteral("kind"), QStringLiteral("recipe")},
               {QStringLiteral("asset_id"), QStringLiteral("self-test-recipe")},
               {QStringLiteral("sha256"), hash},
               {QStringLiteral("rendering_model"), QStringLiteral("candidate-v1")},
           },
           QJsonObject{
               {QStringLiteral("kind"), QStringLiteral("profile")},
               {QStringLiteral("asset_id"), QStringLiteral("self-test-profile")},
               {QStringLiteral("sha256"), hash},
           },
       }},
  };
  if (!write_json(corpus_path, corpus_json)) {
    return EXIT_FAILURE;
  }
  const auto capture_contract = load_capture_contract(
      corpus_path,
      QStringLiteral("self-test-capture"),
      QStringLiteral("preview"),
      loaded_candidate->size(),
      &load_error);
  if (!capture_contract.has_value() || capture_contract->recipe_sha256 != hash ||
      capture_contract->recipe_rendering_model != QStringLiteral("candidate-v1") ||
      capture_contract->profile_sha256_values != std::vector<QString>{hash} ||
      capture_contract->linear_one_nits != 100.0 ||
      capture_contract->thresholds.max_delta_e_00 != 50.0) {
    return EXIT_FAILURE;
  }

  const Thresholds permissive{
      .p95_delta_e_00 = 0.01,
      .max_delta_e_00 = 50.0,
      .p95_delta_e_itp = 0.01,
      .max_delta_e_itp = 100.0,
      .max_alpha_error = 0.001,
  };
  const auto result = compare_images(
      *loaded_candidate,
      *loaded_reference,
      capture_contract->regions,
      permissive,
      100.0);
  if (!result.failure.isEmpty() || result.worst_delta_e_00.value <= 0.0 ||
      result.worst_delta_e_00.x != 7 || result.worst_delta_e_00.y != 3 ||
      result.region_metrics.size() != 1 || result.heatmap.isNull() ||
      !result.delta_e_00.percentile_gate_passes(0.95)) {
    return EXIT_FAILURE;
  }
  const auto heatmap_path = temporary.filePath(QStringLiteral("heatmap.png"));
  const auto report_path = temporary.filePath(QStringLiteral("report.json"));
  if (!result.heatmap.save(heatmap_path, "PNG")) {
    return EXIT_FAILURE;
  }
  const Provenance provenance{
      .execution_path = QStringLiteral("preview"),
      .capture_id = QStringLiteral("self-test-capture"),
      .candidate_rendering_model = QStringLiteral("candidate-v1"),
      .reference_rendering_model = QStringLiteral("independent-reference-v1"),
      .recipe_sha256 = hash,
      .output_profile_sha256 = hash,
      .corpus_manifest_sha256 = file_sha256(corpus_path),
  };
  const auto report = build_report(
      candidate_path,
      reference_path,
      heatmap_path,
      *loaded_candidate,
      result,
      permissive,
      100.0,
      provenance);
  if (!write_json(report_path, report) ||
      report.value(QStringLiteral("schema")).toString() !=
          QString::fromLatin1(kReportSchema) ||
      !report.value(QStringLiteral("passed")).toBool() ||
      file_sha256(heatmap_path).isEmpty() || file_sha256(report_path).isEmpty()) {
    return EXIT_FAILURE;
  }
  const Thresholds strict = {
      .p95_delta_e_00 = permissive.p95_delta_e_00,
      .max_delta_e_00 = 0.001,
      .p95_delta_e_itp = permissive.p95_delta_e_itp,
      .max_delta_e_itp = 0.001,
      .max_alpha_error = permissive.max_alpha_error,
  };
  const auto strict_result = compare_images(
      *loaded_candidate,
      *loaded_reference,
      capture_contract->regions,
      strict,
      100.0);
  if (strict_result.delta_e_00.summary().maximum <= strict.max_delta_e_00 ||
      strict_result.delta_e_itp.summary().maximum <= strict.max_delta_e_itp) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication application(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("color-corpus-comparison"));
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Compare licensed-corpus renders in a common colorimetric space."));
  parser.addHelpOption();

  const QCommandLineOption self_test_option(
      QStringLiteral("self-test"), QStringLiteral("Run deterministic internal proof."));
  const QCommandLineOption diagnostic_option(
      QStringLiteral("diagnostic"),
      QStringLiteral("Measure a provenance-pinned pair without treating it as promotion evidence."));
  const QCommandLineOption candidate_option(
      QStringLiteral("candidate"), QStringLiteral("Candidate image path."), QStringLiteral("path"));
  const QCommandLineOption reference_option(
      QStringLiteral("reference"), QStringLiteral("Independent reference image path."), QStringLiteral("path"));
  const QCommandLineOption report_option(
      QStringLiteral("report"), QStringLiteral("Machine-readable report path."), QStringLiteral("path"));
  const QCommandLineOption heatmap_option(
      QStringLiteral("heatmap"), QStringLiteral("False-color PNG heatmap path."), QStringLiteral("path"));
  const QCommandLineOption corpus_option(
      QStringLiteral("corpus-manifest"), QStringLiteral("Validated color-corpus manifest."), QStringLiteral("path"));
  const QCommandLineOption capture_option(
      QStringLiteral("capture-id"), QStringLiteral("Capture asset ID in the corpus manifest."), QStringLiteral("id"));
  const QCommandLineOption execution_option(
      QStringLiteral("execution-path"), QStringLiteral("camera-input, icc, sdr, hdr, proof, preview, or export."), QStringLiteral("name"));
  const QCommandLineOption candidate_model_option(
      QStringLiteral("candidate-rendering-model"), QStringLiteral("Candidate rendering-model identity."), QStringLiteral("id"));
  const QCommandLineOption reference_model_option(
      QStringLiteral("reference-rendering-model"), QStringLiteral("Independent reference renderer identity."), QStringLiteral("id"));
  const QCommandLineOption recipe_hash_option(
      QStringLiteral("recipe-sha256"), QStringLiteral("Exact candidate recipe digest."), QStringLiteral("digest"));
  const QCommandLineOption profile_hash_option(
      QStringLiteral("output-profile-sha256"), QStringLiteral("Exact output profile digest."), QStringLiteral("digest"));
  parser.addOptions({
      self_test_option,
      diagnostic_option,
      candidate_option,
      reference_option,
      report_option,
      heatmap_option,
      corpus_option,
      capture_option,
      execution_option,
      candidate_model_option,
      reference_model_option,
      recipe_hash_option,
      profile_hash_option,
  });
  parser.process(application);
  if (parser.isSet(self_test_option)) {
    return self_test();
  }

  const std::array<const QCommandLineOption*, 11> required = {
      &candidate_option,
      &reference_option,
      &report_option,
      &heatmap_option,
      &corpus_option,
      &capture_option,
      &execution_option,
      &candidate_model_option,
      &reference_model_option,
      &recipe_hash_option,
      &profile_hash_option,
  };
  for (const auto* option : required) {
    if (!parser.isSet(*option)) {
      parser.showHelp(EXIT_FAILURE);
    }
  }
  const auto execution_path = parser.value(execution_option);
  const std::array<QString, 7> allowed_paths = {
      QStringLiteral("camera-input"),
      QStringLiteral("icc"),
      QStringLiteral("sdr"),
      QStringLiteral("hdr"),
      QStringLiteral("proof"),
      QStringLiteral("preview"),
      QStringLiteral("export"),
  };
  if (std::find(allowed_paths.cbegin(), allowed_paths.cend(), execution_path) ==
      allowed_paths.cend()) {
    qCritical("invalid --execution-path");
    return EXIT_FAILURE;
  }
  const auto recipe_sha = parser.value(recipe_hash_option);
  const auto profile_sha = parser.value(profile_hash_option);
  if (!is_sha256(recipe_sha) || !is_sha256(profile_sha)) {
    qCritical("recipe and output-profile digests must be lowercase SHA-256 values");
    return EXIT_FAILURE;
  }

  QString error;
  const auto candidate_path = parser.value(candidate_option);
  const auto reference_path = parser.value(reference_option);
  const auto candidate = load_linear_bt2020(candidate_path, &error);
  if (!candidate.has_value()) {
    qCritical("%s", qPrintable(error));
    return EXIT_FAILURE;
  }
  const auto reference = load_linear_bt2020(reference_path, &error);
  if (!reference.has_value()) {
    qCritical("%s", qPrintable(error));
    return EXIT_FAILURE;
  }
  if (candidate->size() != reference->size()) {
    qCritical("candidate and reference dimensions differ");
    return EXIT_FAILURE;
  }

  const auto corpus_path = parser.value(corpus_option);
  const auto capture_id = parser.value(capture_option);
  const auto diagnostic = parser.isSet(diagnostic_option);
  auto thresholds = Thresholds{
      .p95_delta_e_00 = 200.0,
      .max_delta_e_00 = 200.0,
      .p95_delta_e_itp = 1000.0,
      .max_delta_e_itp = 1000.0,
      .max_alpha_error = 0.0001,
  };
  auto regions = std::vector<Region>{};
  auto linear_one_nits = 100.0;
  auto capture_contract = std::optional<CaptureContract>{};
  if (!diagnostic) {
    capture_contract = load_capture_contract(
        corpus_path,
        capture_id,
        execution_path,
        candidate->size(),
        &error);
    if (!capture_contract.has_value()) {
      qCritical("%s", qPrintable(error));
      return EXIT_FAILURE;
    }
    thresholds = capture_contract->thresholds;
    regions = capture_contract->regions;
    linear_one_nits = capture_contract->linear_one_nits;
  }
  const auto candidate_model = parser.value(candidate_model_option);
  if (!diagnostic && recipe_sha != capture_contract->recipe_sha256) {
    qCritical("--recipe-sha256 does not match the capture's linked recipe asset");
    return EXIT_FAILURE;
  }
  if (!diagnostic && candidate_model != capture_contract->recipe_rendering_model) {
    qCritical("--candidate-rendering-model does not match the linked recipe asset");
    return EXIT_FAILURE;
  }
  if (!diagnostic && std::find(
          capture_contract->profile_sha256_values.cbegin(),
          capture_contract->profile_sha256_values.cend(),
          profile_sha) == capture_contract->profile_sha256_values.cend()) {
    qCritical("--output-profile-sha256 is not linked to the selected capture");
    return EXIT_FAILURE;
  }
  auto result = compare_images(
      *candidate,
      *reference,
      regions,
      thresholds,
      linear_one_nits);
  if (!result.failure.isEmpty()) {
    qCritical("%s", qPrintable(result.failure));
    return EXIT_FAILURE;
  }

  const auto heatmap_path = parser.value(heatmap_option);
  if (!result.heatmap.save(heatmap_path, "PNG")) {
    qCritical("unable to write heatmap");
    return EXIT_FAILURE;
  }
  const Provenance provenance{
      .execution_path = execution_path,
      .capture_id = capture_id,
      .candidate_rendering_model = candidate_model,
      .reference_rendering_model = parser.value(reference_model_option),
      .recipe_sha256 = recipe_sha,
      .output_profile_sha256 = profile_sha,
      .corpus_manifest_sha256 = file_sha256(corpus_path),
  };
  auto report = build_report(
      candidate_path,
      reference_path,
      heatmap_path,
      *candidate,
      result,
      thresholds,
      linear_one_nits,
      provenance);
  report.insert(QStringLiteral("diagnostic_only"), diagnostic);
  if (!write_json(parser.value(report_option), report)) {
    qCritical("unable to write comparison report");
    return EXIT_FAILURE;
  }
  return diagnostic || report.value(QStringLiteral("passed")).toBool()
             ? EXIT_SUCCESS
             : 2;
}
