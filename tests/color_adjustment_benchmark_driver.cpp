#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QStringList>

#include "observability.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

namespace {

using photo_editor::pipeline::ColorAdjustmentState;
using photo_editor::pipeline::ColorPixel;

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

QString option_value(const QStringList& arguments, const QString& name) {
  const auto index = arguments.indexOf(name);
  if (index < 0 || index + 1 >= arguments.size()) {
    return {};
  }
  return arguments.at(index + 1).trimmed();
}

bool has_option(const QStringList& arguments, const QString& name) {
  return arguments.contains(name);
}

std::vector<ColorPixel> make_pixels(int width, int height) {
  std::vector<ColorPixel> pixels;
  pixels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    const auto yn = static_cast<float>(y) / static_cast<float>(std::max(1, height - 1));
    for (int x = 0; x < width; ++x) {
      const auto xn = static_cast<float>(x) / static_cast<float>(std::max(1, width - 1));
      const auto wave = static_cast<float>(std::sin((xn * 5.0F + yn * 3.0F) * 3.14159265F));
      pixels.push_back(ColorPixel{
          .red = std::clamp(0.12F + xn * 0.76F + wave * 0.04F, 0.0F, 1.0F),
          .green = std::clamp(0.10F + yn * 0.78F - wave * 0.03F, 0.0F, 1.0F),
          .blue = std::clamp(0.16F + (1.0F - xn) * 0.42F + yn * 0.22F, 0.0F, 1.0F),
          .alpha = 1.0F,
      });
    }
  }
  return pixels;
}

void configure_metal_compatible_color_pipeline(ColorAdjustmentState* state) {
  if (state == nullptr) {
    return;
  }
  state->color_pipeline.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear sRGB");
  state->color_pipeline.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  state->color_pipeline.export_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  state->color_pipeline.render_intent = photo_editor::pipeline::RenderIntent::Perceptual;
  state->color_pipeline.output_headroom = 1.0;
  state->input_transform.raw_input_transform_enabled = false;
  state->input_transform.highlight_recovery_enabled = false;
  state->input_transform.camera_matching_look = "Standard";
}

ColorAdjustmentState benchmark_state() {
  ColorAdjustmentState state;
  configure_metal_compatible_color_pipeline(&state);
  state.exposure = 0.35;
  state.contrast = 18.0;
  state.highlights = -22.0;
  state.shadows = 16.0;
  state.whites = 6.0;
  state.blacks = -8.0;
  state.temperature = 12.0;
  state.tint = -6.0;
  state.vibrance = 18.0;
  state.saturation = 5.0;
  state.texture = 12.0;
  state.clarity = 8.0;
  state.dehaze = 4.0;
  state.sharpening = 34.0;
  state.luminance_nr = 12.0;
  state.color_nr = 10.0;
  state.master_curve_points = {
      {.x = 0.0, .y = 0.0},
      {.x = 0.22, .y = 0.18},
      {.x = 0.55, .y = 0.60},
      {.x = 1.0, .y = 1.0},
  };
  return state;
}

ColorAdjustmentState simple_global_benchmark_state() {
  ColorAdjustmentState state;
  configure_metal_compatible_color_pipeline(&state);
  state.exposure = 0.35;
  state.contrast = 18.0;
  state.highlights = -22.0;
  state.shadows = 16.0;
  state.whites = 6.0;
  state.blacks = -8.0;
  state.master_curve_points = {
      {.x = 0.0, .y = 0.0},
      {.x = 0.22, .y = 0.18},
      {.x = 0.55, .y = 0.60},
      {.x = 1.0, .y = 1.0},
  };
  return state;
}

ColorAdjustmentState spatial_texture_only_benchmark_state() {
  auto state = simple_global_benchmark_state();
  state.texture = 32.0;
  return state;
}

struct Scenario final {
  QString name;
  int width = 0;
  int height = 0;
};

struct ScenarioResult final {
  qint64 elapsed_ms = 0;
  double avg_ms = 0.0;
  double p50_ms = 0.0;
  double p90_ms = 0.0;
  double p95_ms = 0.0;
  double p99_ms = 0.0;
  double max_ms = 0.0;
  double stddev_ms = 0.0;
  double mad_ms = 0.0;
  double adjustment_spatial_avg_ms = 0.0;
  double adjustment_spatial_luminance_avg_ms = 0.0;
  double adjustment_spatial_texture_blur_avg_ms = 0.0;
  double adjustment_spatial_clarity_blur_avg_ms = 0.0;
  double adjustment_spatial_haze_blur_avg_ms = 0.0;
  double adjustment_spatial_apply_avg_ms = 0.0;
  double adjustment_post_color_avg_ms = 0.0;
  double adjustment_detail_avg_ms = 0.0;
  double adjustment_detail_oklab_avg_ms = 0.0;
  double adjustment_detail_sharpen_avg_ms = 0.0;
  double adjustment_detail_luminance_nr_avg_ms = 0.0;
  double adjustment_detail_color_nr_avg_ms = 0.0;
  double adjustment_detail_output_avg_ms = 0.0;
  double adjustment_final_output_avg_ms = 0.0;
  double adjustment_final_output_black_point_avg_ms = 0.0;
  double adjustment_final_output_headroom_avg_ms = 0.0;
  double adjustment_gpu_command_buffer_count_avg = 0.0;
  double adjustment_gpu_kernel_dispatch_count_avg = 0.0;
  double adjustment_gpu_blit_count_avg = 0.0;
  double adjustment_gpu_cpu_to_gpu_bytes_avg = 0.0;
  double adjustment_gpu_gpu_to_cpu_bytes_avg = 0.0;
  double adjustment_gpu_spatial_texture_blur_dispatch_count_avg = 0.0;
  double adjustment_gpu_spatial_clarity_blur_dispatch_count_avg = 0.0;
  double adjustment_gpu_spatial_haze_blur_dispatch_count_avg = 0.0;
};

double percentile(std::vector<double> sorted_samples, double percentile_value) {
  if (sorted_samples.empty()) {
    return 0.0;
  }
  std::sort(sorted_samples.begin(), sorted_samples.end());
  const auto normalized = std::clamp(percentile_value, 0.0, 100.0) / 100.0;
  const auto index = static_cast<std::size_t>(
      std::ceil(normalized * static_cast<double>(sorted_samples.size())) - 1.0);
  return sorted_samples[std::min(index, sorted_samples.size() - 1)];
}

ScenarioResult summarize_samples(std::vector<double> samples) {
  ScenarioResult result;
  if (samples.empty()) {
    return result;
  }
  result.elapsed_ms = static_cast<qint64>(
      std::accumulate(samples.cbegin(), samples.cend(), 0.0));
  result.avg_ms = static_cast<double>(result.elapsed_ms) / static_cast<double>(samples.size());
  result.p50_ms = percentile(samples, 50.0);
  result.p90_ms = percentile(samples, 90.0);
  result.p95_ms = percentile(samples, 95.0);
  result.p99_ms = percentile(samples, 99.0);
  result.max_ms = *std::max_element(samples.cbegin(), samples.cend());

  auto variance = 0.0;
  for (const auto sample : samples) {
    const auto delta = sample - result.avg_ms;
    variance += delta * delta;
  }
  result.stddev_ms = std::sqrt(variance / static_cast<double>(samples.size()));

  std::vector<double> absolute_deviations;
  absolute_deviations.reserve(samples.size());
  for (const auto sample : samples) {
    absolute_deviations.push_back(std::abs(sample - result.p50_ms));
  }
  result.mad_ms = percentile(std::move(absolute_deviations), 50.0);
  return result;
}

ScenarioResult run_scenario(
    const Scenario& scenario,
    int iterations,
    const ColorAdjustmentState& state,
    const QString& state_name) {
  const auto source_pixels = make_pixels(scenario.width, scenario.height);
  std::vector<double> samples;
  std::vector<double> spatial_samples;
  std::vector<double> spatial_luminance_samples;
  std::vector<double> spatial_texture_blur_samples;
  std::vector<double> spatial_clarity_blur_samples;
  std::vector<double> spatial_haze_blur_samples;
  std::vector<double> spatial_apply_samples;
  std::vector<double> post_color_samples;
  std::vector<double> detail_samples;
  std::vector<double> detail_oklab_samples;
  std::vector<double> detail_sharpen_samples;
  std::vector<double> detail_luminance_nr_samples;
  std::vector<double> detail_color_nr_samples;
  std::vector<double> detail_output_samples;
  std::vector<double> final_output_samples;
  std::vector<double> final_output_black_point_samples;
  std::vector<double> final_output_headroom_samples;
  std::vector<double> gpu_command_buffer_count_samples;
  std::vector<double> gpu_kernel_dispatch_count_samples;
  std::vector<double> gpu_blit_count_samples;
  std::vector<double> gpu_cpu_to_gpu_bytes_samples;
  std::vector<double> gpu_gpu_to_cpu_bytes_samples;
  std::vector<double> gpu_spatial_texture_blur_dispatch_count_samples;
  std::vector<double> gpu_spatial_clarity_blur_dispatch_count_samples;
  std::vector<double> gpu_spatial_haze_blur_dispatch_count_samples;
  samples.reserve(static_cast<std::size_t>(iterations));
  spatial_samples.reserve(static_cast<std::size_t>(iterations));
  spatial_luminance_samples.reserve(static_cast<std::size_t>(iterations));
  spatial_texture_blur_samples.reserve(static_cast<std::size_t>(iterations));
  spatial_clarity_blur_samples.reserve(static_cast<std::size_t>(iterations));
  spatial_haze_blur_samples.reserve(static_cast<std::size_t>(iterations));
  spatial_apply_samples.reserve(static_cast<std::size_t>(iterations));
  post_color_samples.reserve(static_cast<std::size_t>(iterations));
  detail_samples.reserve(static_cast<std::size_t>(iterations));
  detail_oklab_samples.reserve(static_cast<std::size_t>(iterations));
  detail_sharpen_samples.reserve(static_cast<std::size_t>(iterations));
  detail_luminance_nr_samples.reserve(static_cast<std::size_t>(iterations));
  detail_color_nr_samples.reserve(static_cast<std::size_t>(iterations));
  detail_output_samples.reserve(static_cast<std::size_t>(iterations));
  final_output_samples.reserve(static_cast<std::size_t>(iterations));
  final_output_black_point_samples.reserve(static_cast<std::size_t>(iterations));
  final_output_headroom_samples.reserve(static_cast<std::size_t>(iterations));
  gpu_command_buffer_count_samples.reserve(static_cast<std::size_t>(iterations));
  gpu_kernel_dispatch_count_samples.reserve(static_cast<std::size_t>(iterations));
  gpu_blit_count_samples.reserve(static_cast<std::size_t>(iterations));
  gpu_cpu_to_gpu_bytes_samples.reserve(static_cast<std::size_t>(iterations));
  gpu_gpu_to_cpu_bytes_samples.reserve(static_cast<std::size_t>(iterations));
  gpu_spatial_texture_blur_dispatch_count_samples.reserve(static_cast<std::size_t>(iterations));
  gpu_spatial_clarity_blur_dispatch_count_samples.reserve(static_cast<std::size_t>(iterations));
  gpu_spatial_haze_blur_dispatch_count_samples.reserve(static_cast<std::size_t>(iterations));
  for (int iteration = 0; iteration < iterations; ++iteration) {
    QElapsedTimer timer;
    timer.start();
    auto pixels = source_pixels;
    photo_editor::pipeline::ColorAdjustmentTelemetry telemetry;
    photo_editor::pipeline::apply_color_adjustments(
        &pixels,
        scenario.width,
        scenario.height,
        state,
        photo_editor::pipeline::RemovalQuality::Interactive,
        photo_editor::pipeline::CancellationToken{},
        &telemetry);
    const auto elapsed_ms = timer.elapsed();
    samples.push_back(static_cast<double>(elapsed_ms));
    spatial_samples.push_back(static_cast<double>(telemetry.spatial_ms));
    spatial_luminance_samples.push_back(static_cast<double>(telemetry.spatial_luminance_ms));
    spatial_texture_blur_samples.push_back(static_cast<double>(telemetry.spatial_texture_blur_ms));
    spatial_clarity_blur_samples.push_back(static_cast<double>(telemetry.spatial_clarity_blur_ms));
    spatial_haze_blur_samples.push_back(static_cast<double>(telemetry.spatial_haze_blur_ms));
    spatial_apply_samples.push_back(static_cast<double>(telemetry.spatial_apply_ms));
    post_color_samples.push_back(static_cast<double>(telemetry.post_color_ms));
    detail_samples.push_back(static_cast<double>(telemetry.detail_ms));
    detail_oklab_samples.push_back(static_cast<double>(telemetry.detail_oklab_ms));
    detail_sharpen_samples.push_back(static_cast<double>(telemetry.detail_sharpen_ms));
    detail_luminance_nr_samples.push_back(static_cast<double>(telemetry.detail_luminance_nr_ms));
    detail_color_nr_samples.push_back(static_cast<double>(telemetry.detail_color_nr_ms));
    detail_output_samples.push_back(static_cast<double>(telemetry.detail_output_ms));
    final_output_samples.push_back(static_cast<double>(telemetry.final_output_ms));
    final_output_black_point_samples.push_back(
        static_cast<double>(telemetry.final_output_black_point_ms));
    final_output_headroom_samples.push_back(
        static_cast<double>(telemetry.final_output_headroom_ms));
    gpu_command_buffer_count_samples.push_back(
        static_cast<double>(telemetry.gpu_command_buffer_count));
    gpu_kernel_dispatch_count_samples.push_back(
        static_cast<double>(telemetry.gpu_kernel_dispatch_count));
    gpu_blit_count_samples.push_back(static_cast<double>(telemetry.gpu_blit_count));
    gpu_cpu_to_gpu_bytes_samples.push_back(
        static_cast<double>(telemetry.gpu_cpu_to_gpu_bytes));
    gpu_gpu_to_cpu_bytes_samples.push_back(
        static_cast<double>(telemetry.gpu_gpu_to_cpu_bytes));
    gpu_spatial_texture_blur_dispatch_count_samples.push_back(
        static_cast<double>(telemetry.gpu_spatial_texture_blur_dispatch_count));
    gpu_spatial_clarity_blur_dispatch_count_samples.push_back(
        static_cast<double>(telemetry.gpu_spatial_clarity_blur_dispatch_count));
    gpu_spatial_haze_blur_dispatch_count_samples.push_back(
        static_cast<double>(telemetry.gpu_spatial_haze_blur_dispatch_count));
    photo_editor::desktop::observability::logInfo(
        photoEditorPerfLog(),
        QStringLiteral("benchmark.color_adjustment_sample.completed"),
        {
            {QStringLiteral("stage"), scenario.name},
            {QStringLiteral("state"), state_name},
            {QStringLiteral("width"), scenario.width},
            {QStringLiteral("height"), scenario.height},
            {QStringLiteral("pixels"), scenario.width * scenario.height},
            {QStringLiteral("iteration"), iteration + 1},
            {QStringLiteral("iterations"), iterations},
            {QStringLiteral("elapsed_ms"), elapsed_ms},
            {QStringLiteral("adjustment_spatial_ms"), telemetry.spatial_ms},
            {QStringLiteral("adjustment_spatial_luminance_ms"), telemetry.spatial_luminance_ms},
            {QStringLiteral("adjustment_spatial_texture_blur_ms"), telemetry.spatial_texture_blur_ms},
            {QStringLiteral("adjustment_spatial_clarity_blur_ms"), telemetry.spatial_clarity_blur_ms},
            {QStringLiteral("adjustment_spatial_haze_blur_ms"), telemetry.spatial_haze_blur_ms},
            {QStringLiteral("adjustment_spatial_apply_ms"), telemetry.spatial_apply_ms},
            {QStringLiteral("adjustment_post_color_ms"), telemetry.post_color_ms},
            {QStringLiteral("adjustment_detail_ms"), telemetry.detail_ms},
            {QStringLiteral("adjustment_detail_oklab_ms"), telemetry.detail_oklab_ms},
            {QStringLiteral("adjustment_detail_sharpen_ms"), telemetry.detail_sharpen_ms},
            {QStringLiteral("adjustment_detail_luminance_nr_ms"), telemetry.detail_luminance_nr_ms},
            {QStringLiteral("adjustment_detail_color_nr_ms"), telemetry.detail_color_nr_ms},
            {QStringLiteral("adjustment_detail_output_ms"), telemetry.detail_output_ms},
            {QStringLiteral("adjustment_final_output_ms"), telemetry.final_output_ms},
            {QStringLiteral("adjustment_final_output_black_point_ms"), telemetry.final_output_black_point_ms},
            {QStringLiteral("adjustment_final_output_headroom_ms"), telemetry.final_output_headroom_ms},
            {QStringLiteral("adjustment_gpu_command_buffer_count"), telemetry.gpu_command_buffer_count},
            {QStringLiteral("adjustment_gpu_kernel_dispatch_count"), telemetry.gpu_kernel_dispatch_count},
            {QStringLiteral("adjustment_gpu_blit_count"), telemetry.gpu_blit_count},
            {QStringLiteral("adjustment_gpu_cpu_to_gpu_bytes"), telemetry.gpu_cpu_to_gpu_bytes},
            {QStringLiteral("adjustment_gpu_gpu_to_cpu_bytes"), telemetry.gpu_gpu_to_cpu_bytes},
            {QStringLiteral("adjustment_gpu_spatial_texture_blur_dispatch_count"),
             telemetry.gpu_spatial_texture_blur_dispatch_count},
            {QStringLiteral("adjustment_gpu_spatial_clarity_blur_dispatch_count"),
             telemetry.gpu_spatial_clarity_blur_dispatch_count},
            {QStringLiteral("adjustment_gpu_spatial_haze_blur_dispatch_count"),
             telemetry.gpu_spatial_haze_blur_dispatch_count},
        });
  }
  auto result = summarize_samples(std::move(samples));
  result.adjustment_spatial_avg_ms = summarize_samples(std::move(spatial_samples)).avg_ms;
  result.adjustment_spatial_luminance_avg_ms =
      summarize_samples(std::move(spatial_luminance_samples)).avg_ms;
  result.adjustment_spatial_texture_blur_avg_ms =
      summarize_samples(std::move(spatial_texture_blur_samples)).avg_ms;
  result.adjustment_spatial_clarity_blur_avg_ms =
      summarize_samples(std::move(spatial_clarity_blur_samples)).avg_ms;
  result.adjustment_spatial_haze_blur_avg_ms =
      summarize_samples(std::move(spatial_haze_blur_samples)).avg_ms;
  result.adjustment_spatial_apply_avg_ms =
      summarize_samples(std::move(spatial_apply_samples)).avg_ms;
  result.adjustment_post_color_avg_ms =
      summarize_samples(std::move(post_color_samples)).avg_ms;
  result.adjustment_detail_avg_ms =
      summarize_samples(std::move(detail_samples)).avg_ms;
  result.adjustment_detail_oklab_avg_ms =
      summarize_samples(std::move(detail_oklab_samples)).avg_ms;
  result.adjustment_detail_sharpen_avg_ms =
      summarize_samples(std::move(detail_sharpen_samples)).avg_ms;
  result.adjustment_detail_luminance_nr_avg_ms =
      summarize_samples(std::move(detail_luminance_nr_samples)).avg_ms;
  result.adjustment_detail_color_nr_avg_ms =
      summarize_samples(std::move(detail_color_nr_samples)).avg_ms;
  result.adjustment_detail_output_avg_ms =
      summarize_samples(std::move(detail_output_samples)).avg_ms;
  result.adjustment_final_output_avg_ms =
      summarize_samples(std::move(final_output_samples)).avg_ms;
  result.adjustment_final_output_black_point_avg_ms =
      summarize_samples(std::move(final_output_black_point_samples)).avg_ms;
  result.adjustment_final_output_headroom_avg_ms =
      summarize_samples(std::move(final_output_headroom_samples)).avg_ms;
  result.adjustment_gpu_command_buffer_count_avg =
      summarize_samples(std::move(gpu_command_buffer_count_samples)).avg_ms;
  result.adjustment_gpu_kernel_dispatch_count_avg =
      summarize_samples(std::move(gpu_kernel_dispatch_count_samples)).avg_ms;
  result.adjustment_gpu_blit_count_avg =
      summarize_samples(std::move(gpu_blit_count_samples)).avg_ms;
  result.adjustment_gpu_cpu_to_gpu_bytes_avg =
      summarize_samples(std::move(gpu_cpu_to_gpu_bytes_samples)).avg_ms;
  result.adjustment_gpu_gpu_to_cpu_bytes_avg =
      summarize_samples(std::move(gpu_gpu_to_cpu_bytes_samples)).avg_ms;
  result.adjustment_gpu_spatial_texture_blur_dispatch_count_avg =
      summarize_samples(std::move(gpu_spatial_texture_blur_dispatch_count_samples)).avg_ms;
  result.adjustment_gpu_spatial_clarity_blur_dispatch_count_avg =
      summarize_samples(std::move(gpu_spatial_clarity_blur_dispatch_count_samples)).avg_ms;
  result.adjustment_gpu_spatial_haze_blur_dispatch_count_avg =
      summarize_samples(std::move(gpu_spatial_haze_blur_dispatch_count_samples)).avg_ms;
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorBenchmarks"));
  QCoreApplication::setApplicationName(QStringLiteral("ColorAdjustmentBenchmarkDriver"));

  const auto arguments = QCoreApplication::arguments();
  const auto iteration_value = option_value(arguments, QStringLiteral("--iterations"));
  const auto iterations = iteration_value.isEmpty() ? 3 : iteration_value.toInt();
  if (iterations <= 0) {
    return fail(QStringLiteral("--iterations must be positive"));
  }

  photo_editor::desktop::observability::initialize();
  photo_editor::desktop::observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("desktop.startup.ready"),
      {
          {QStringLiteral("elapsed_ms"), 0},
          {QStringLiteral("benchmark_driver"), QStringLiteral("color_adjustment")},
      });

  const auto simple_global = has_option(arguments, QStringLiteral("--simple-global"));
  const auto spatial_texture_only =
      has_option(arguments, QStringLiteral("--spatial-texture-only"));
  const auto state =
      spatial_texture_only
          ? spatial_texture_only_benchmark_state()
          : (simple_global ? simple_global_benchmark_state() : benchmark_state());
  const auto state_name =
      spatial_texture_only
          ? QStringLiteral("spatial-texture-only")
          : (simple_global ? QStringLiteral("simple-global") : QStringLiteral("spatial-detail"));
  const std::vector<Scenario> scenarios = {
      {.name = QStringLiteral("tiny-grid"), .width = 64, .height = 64},
      {.name = QStringLiteral("preview-960"), .width = 960, .height = 640},
      {.name = QStringLiteral("full-1920"), .width = 1920, .height = 1280},
  };

  QElapsedTimer suite_timer;
  suite_timer.start();
  for (const auto& scenario : scenarios) {
    const auto result = run_scenario(scenario, iterations, state, state_name);
    const auto pixel_count = scenario.width * scenario.height;
    photo_editor::desktop::observability::logInfo(
        photoEditorPerfLog(),
        QStringLiteral("benchmark.color_adjustment_pass.completed"),
        {
            {QStringLiteral("stage"), scenario.name},
            {QStringLiteral("state"), state_name},
            {QStringLiteral("width"), scenario.width},
            {QStringLiteral("height"), scenario.height},
            {QStringLiteral("pixels"), pixel_count},
            {QStringLiteral("iterations"), iterations},
            {QStringLiteral("elapsed_ms"), result.elapsed_ms},
            {QStringLiteral("avg_ms"), result.avg_ms},
            {QStringLiteral("p50_ms"), result.p50_ms},
            {QStringLiteral("p90_ms"), result.p90_ms},
            {QStringLiteral("p95_ms"), result.p95_ms},
            {QStringLiteral("p99_ms"), result.p99_ms},
            {QStringLiteral("max_ms"), result.max_ms},
            {QStringLiteral("stddev_ms"), result.stddev_ms},
            {QStringLiteral("mad_ms"), result.mad_ms},
            {QStringLiteral("adjustment_spatial_avg_ms"), result.adjustment_spatial_avg_ms},
            {QStringLiteral("adjustment_spatial_luminance_avg_ms"),
             result.adjustment_spatial_luminance_avg_ms},
            {QStringLiteral("adjustment_spatial_texture_blur_avg_ms"),
             result.adjustment_spatial_texture_blur_avg_ms},
            {QStringLiteral("adjustment_spatial_clarity_blur_avg_ms"),
             result.adjustment_spatial_clarity_blur_avg_ms},
            {QStringLiteral("adjustment_spatial_haze_blur_avg_ms"),
             result.adjustment_spatial_haze_blur_avg_ms},
            {QStringLiteral("adjustment_spatial_apply_avg_ms"), result.adjustment_spatial_apply_avg_ms},
            {QStringLiteral("adjustment_post_color_avg_ms"),
             result.adjustment_post_color_avg_ms},
            {QStringLiteral("adjustment_detail_avg_ms"), result.adjustment_detail_avg_ms},
            {QStringLiteral("adjustment_detail_oklab_avg_ms"),
             result.adjustment_detail_oklab_avg_ms},
            {QStringLiteral("adjustment_detail_sharpen_avg_ms"),
             result.adjustment_detail_sharpen_avg_ms},
            {QStringLiteral("adjustment_detail_luminance_nr_avg_ms"),
             result.adjustment_detail_luminance_nr_avg_ms},
            {QStringLiteral("adjustment_detail_color_nr_avg_ms"),
             result.adjustment_detail_color_nr_avg_ms},
            {QStringLiteral("adjustment_detail_output_avg_ms"),
             result.adjustment_detail_output_avg_ms},
            {QStringLiteral("adjustment_final_output_avg_ms"),
             result.adjustment_final_output_avg_ms},
            {QStringLiteral("adjustment_final_output_black_point_avg_ms"),
             result.adjustment_final_output_black_point_avg_ms},
            {QStringLiteral("adjustment_final_output_headroom_avg_ms"),
             result.adjustment_final_output_headroom_avg_ms},
            {QStringLiteral("adjustment_gpu_command_buffer_count_avg"),
             result.adjustment_gpu_command_buffer_count_avg},
            {QStringLiteral("adjustment_gpu_kernel_dispatch_count_avg"),
             result.adjustment_gpu_kernel_dispatch_count_avg},
            {QStringLiteral("adjustment_gpu_blit_count_avg"),
             result.adjustment_gpu_blit_count_avg},
            {QStringLiteral("adjustment_gpu_cpu_to_gpu_bytes_avg"),
             result.adjustment_gpu_cpu_to_gpu_bytes_avg},
            {QStringLiteral("adjustment_gpu_gpu_to_cpu_bytes_avg"),
             result.adjustment_gpu_gpu_to_cpu_bytes_avg},
            {QStringLiteral("adjustment_gpu_spatial_texture_blur_dispatch_count_avg"),
             result.adjustment_gpu_spatial_texture_blur_dispatch_count_avg},
            {QStringLiteral("adjustment_gpu_spatial_clarity_blur_dispatch_count_avg"),
             result.adjustment_gpu_spatial_clarity_blur_dispatch_count_avg},
            {QStringLiteral("adjustment_gpu_spatial_haze_blur_dispatch_count_avg"),
             result.adjustment_gpu_spatial_haze_blur_dispatch_count_avg},
        });
    std::cout << scenario.name.toStdString() << "_elapsed_ms=" << result.elapsed_ms << '\n';
  }

  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.color_adjustment_suite.completed"),
      {
          {QStringLiteral("scenario_count"), static_cast<int>(scenarios.size())},
          {QStringLiteral("state"), state_name},
          {QStringLiteral("iterations"), iterations},
          {QStringLiteral("elapsed_ms"), suite_timer.elapsed()},
      });
  return EXIT_SUCCESS;
}
