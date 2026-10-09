#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#include "photo_editor/pipeline/color_fill_layer_program.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <atomic>
#include <array>
#include <barrier>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <sys/resource.h>

using namespace photo_editor::pipeline;
using Clock = std::chrono::steady_clock;

namespace {
double milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
double percentile(std::vector<double> values, double fraction) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  return values[static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size()))) - 1U];
}
struct Fixture {
  int width;
  int height;
  std::vector<ColorPixel> input;
  ColorAdjustmentState state;
  CompiledToneStageLut tone;
  CompiledCurveStageLuts curves;
  RenderSurfaceDescriptor descriptor;
  QByteArray reference;
  std::vector<ColorPixel> reference_pixels;
};
Fixture fixture(int width, int height, int variant, bool layers = false) {
  Fixture value;
  value.width = width;
  value.height = height;
  value.input.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (std::size_t i = 0; i < value.input.size(); ++i) {
    const auto channel = static_cast<float>(i % 251U) / 251.0F;
    value.input[i] = {channel, channel * 0.7F, 0.1F + channel * 0.3F, 0.8F};
  }
  value.state.color_pipeline.working_profile = working_color_profile_from_name("Linear sRGB");
  value.state.contrast = 12.0 + variant;
  value.state.shadows = 10.0;
  value.state.lens_distortion = 8.0;
  value.state.texture = 15.0;
  value.state.sharpening = 20.0;
  value.state.master_curve_points = {{.x = 0.0, .y = 0.0},
      {.x = 0.5, .y = 0.45 + 0.005 * variant}, {.x = 1.0, .y = 1.0}};
  if (layers) {
    PhotoLayer layer;
    layer.kind = "source";
    layer.opacity = 0.7;
    layer.pixel_width = 4;
    layer.pixel_height = 4;
    layer.pixels.assign(16U, ColorPixel{0.3F + static_cast<float>(variant) * 0.002F, 0.5F, 0.1F, 0.75F});
    layer.mask_width = 2;
    layer.mask_height = 2;
    layer.mask_data = {0U, static_cast<std::uint8_t>(variant + 50), 128U, 255U};
    layer.contrast = 5.0 + variant;
    layer.master_curve_points = value.state.master_curve_points;
    layer.saturation = 2.0 + variant;
    layer.grading_shadows_saturation = 12.0 + variant;
    layer.grading_shadows_hue = 0.6;
    layer.texture = 8.0;
    layer.sharpening = 10.0;
    layer.global_transform_rotation = 3.0;
    layer.transform_scale = 10.0;
    layer.lut_enabled = true;
    layer.lut_size = 2;
    for (int r = 0; r < 2; ++r) for (int g = 0; g < 2; ++g) for (int b = 0; b < 2; ++b) {
      layer.lut_table.push_back({static_cast<float>(r) * (0.95F + static_cast<float>(variant) * 0.0005F),
          static_cast<float>(g), static_cast<float>(b), 1.0F});
    }
    value.state.photo_layers.push_back(layer);
    layer.kind = "adjustment";
    layer.pixels.clear();
    value.state.photo_layers.push_back(std::move(layer));
  }
  value.tone = compile_tone_stage_lut(value.state);
  value.curves = compile_curve_stage_luts(value.state);
  value.descriptor = {.pixel_encoding = RenderPixelEncoding::SceneLinearFloat,
      .color_domain = RenderColorDomain::WorkingScene, .range = RenderRangeModel::SceneExtended,
      .alpha = RenderAlphaModel::Straight,
      .color_space_identity = value.state.color_pipeline.working_profile.color_space.name,
      .transfer_identity = "linear", .width = static_cast<std::uint32_t>(width),
      .height = static_cast<std::uint32_t>(height)};
  return value;
}
QByteArray digest(const std::vector<ColorPixel>& pixels) {
  return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(pixels.data()),
      static_cast<qsizetype>(pixels.size() * sizeof(ColorPixel))), QCryptographicHash::Sha256).toHex();
}
struct Sample { double submit_ms; double completed_ms; bool valid; };
Sample render(metal::MetalPipelineBackend& backend, const Fixture& value,
              std::vector<ColorPixel>* reference = nullptr) {
  ColorAdjustmentTelemetry telemetry;
  const auto start = Clock::now();
  auto surface = backend.publish_masks_and_color_lut_surface(value.input,
      value.width, value.height, value.state, {}, value.descriptor, &telemetry,
      {}, 1.1, false, &value.tone, &value.curves);
  const auto submitted = milliseconds(start);
  std::vector<ColorPixel> output;
  bool valid = surface && surface->copy_pixels(&output) &&
      output.size() == value.input.size() && telemetry.gpu_command_buffer_count == 1 &&
      telemetry.gpu_kernel_dispatch_count > 0 && telemetry.gpu_fallback_reason.empty();
  const auto completed = milliseconds(start);
  if (valid) {
    if (reference) *reference = std::move(output);
    else valid = output.size() == value.reference_pixels.size() &&
        std::memcmp(output.data(), value.reference_pixels.data(), output.size() * sizeof(ColorPixel)) == 0;
  }
  return {submitted, completed, valid};
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() != 3 || (args[1] != "--serial" && args[1] != "--concurrent" && args[1] != "--contract")) return 2;
  metal::MetalPipelineBackend backend;
  if (!backend.available()) {
    std::cerr << backend.unavailable_reason() << '\n';
    return 3;
  }
  const bool contract = args[1] == "--contract";
  const int variants = contract ? 17 : 3;
  // The contract churns all layer caches as well as the global LUT caches.
  // Timed workloads use nine immutable programs with identical fixture inputs.
  std::vector<Fixture> fixtures;
  for (int lane = 0; lane < 3; ++lane) {
    for (int variant = 0; variant < variants; ++variant) {
      fixtures.push_back(fixture(contract ? 96 : lane == 0 ? 1600 : lane == 1 ? 640 : 2560,
          contract ? 64 : lane == 0 ? 1200 : lane == 1 ? 480 : 1707, lane * variants + variant, contract));
    }
  }
  QJsonArray references;
  for (auto& value : fixtures) {
    if (contract) {
      const auto program = compile_fill_layer_program(value.state, value.width, value.height);
      if (!program.ready() || program.layer_count != 2U || program.mask_identity == 0U ||
          program.pixel_identity == 0U || program.tone_identity == 0U ||
          program.curve_identity == 0U || program.downstream_look_identity == 0U ||
          !program.layers[1].has_user_lut || !program.layers[1].has_geometry ||
          !program.layers[1].has_spatial_detail || !program.layers[1].has_fine_detail) {
        std::cerr << "Layer fixture: ready=" << program.ready() << " count=" << program.layer_count
                  << " mask=" << program.mask_identity << " pixels=" << program.pixel_identity
                  << " tone=" << program.tone_identity << " curves=" << program.curve_identity
                  << " look=" << program.downstream_look_identity
                  << " lut=" << program.layers[1].has_user_lut
                  << " geometry=" << program.layers[1].has_geometry
                  << " spatial=" << program.layers[1].has_spatial_detail
                  << " fine=" << program.layers[1].has_fine_detail << '\n';
        return 4;
      }
    }
    if (!render(backend, value, &value.reference_pixels).valid) return 4;
    value.reference = digest(value.reference_pixels);
    references.append(QString::fromLatin1(value.reference));
  }
  const int iterations = contract ? 102 : 36;
  std::array<std::vector<double>, 3> submission, completion;
  std::atomic<bool> valid = true, done = false;
  std::vector<double> cache_wait;
  std::thread observer([&] {
    while (!done.load()) {
      const auto start = Clock::now();
      const auto stats = backend.tone_lut_cache_stats();
      cache_wait.push_back(milliseconds(start));
      if (stats.entries > stats.maximum_entries || stats.bytes > stats.maximum_bytes) valid = false;
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
  });
  const auto run = [&](int lane) {
    for (int i = 0; i < iterations; ++i) {
      const auto& value = fixtures[static_cast<std::size_t>(lane * variants + i % variants)];
      const auto sample = render(backend, value);
      if (!sample.valid) valid = false;
      submission[static_cast<std::size_t>(lane)].push_back(sample.submit_ms);
      completion[static_cast<std::size_t>(lane)].push_back(sample.completed_ms);
    }
  };
  const auto start = Clock::now();
  if (args[1] != "--serial") {
    std::barrier start_line(3);
    std::vector<std::thread> workers;
    for (int lane = 0; lane < 3; ++lane) workers.emplace_back([&, lane] {
      start_line.arrive_and_wait(); run(lane);
    });
    for (auto& worker : workers) worker.join();
  } else {
    for (int lane = 0; lane < 3; ++lane) run(lane);
  }
  const auto elapsed = milliseconds(start);
  done = true;
  observer.join();
  if (contract) {
    const auto& value = fixtures.front();
    const std::atomic_bool cancelled = true;
    ColorAdjustmentTelemetry telemetry;
    if (backend.publish_masks_and_color_lut_surface(value.input, value.width, value.height,
        value.state, {}, value.descriptor, &telemetry, {.cancellation_requested = &cancelled},
        1.1, false, &value.tone, &value.curves) ||
        telemetry.gpu_fallback_reason != "fused_masks_color_lut_cancelled") valid = false;
    // A rejected publication must not leave the cache lock held.
    if (!render(backend, value).valid) valid = false;
  }
  QJsonArray lanes;
  for (std::size_t lane = 0; lane < 3; ++lane) {
    QJsonArray samples;
    for (const auto value : submission[lane]) samples.append(value);
    lanes.append(QJsonObject{{"submit_p95_ms", percentile(submission[lane], 0.95)},
        {"submit_p50_ms", percentile(submission[lane], 0.5)},
        {"complete_p95_ms", percentile(completion[lane], 0.95)}, {"submit_samples_ms", samples}});
  }
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  const QJsonObject report{{"fixture_version", 2}, {"mode", args[1]}, {"device", QString::fromStdString(backend.device_name())},
      {"iterations_per_lane", iterations}, {"lanes", lanes}, {"elapsed_ms", elapsed},
      {"cache_wait_p95_ms", percentile(cache_wait, 0.95)},
      {"cache_wait_max_ms", percentile(cache_wait, 1.0)},
      {"peak_rss_bytes", static_cast<double>(usage.ru_maxrss)},
      {"exact_restoration", valid.load()}, {"reference_sha256", references}};
  QFile file(args[2]);
  if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0) return 5;
  std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
  return valid ? 0 : 6;
}
