#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QStringList>

#include "observability.h"
#include "photo_editor/pipeline/color_adjustments.h"

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

ColorAdjustmentState benchmark_state() {
  ColorAdjustmentState state;
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

struct Scenario final {
  QString name;
  int width = 0;
  int height = 0;
};

qint64 run_scenario(
    const Scenario& scenario,
    int iterations,
    const ColorAdjustmentState& state) {
  const auto source_pixels = make_pixels(scenario.width, scenario.height);
  QElapsedTimer timer;
  timer.start();
  for (int iteration = 0; iteration < iterations; ++iteration) {
    auto pixels = source_pixels;
    photo_editor::pipeline::apply_color_adjustments(
        &pixels,
        scenario.width,
        scenario.height,
        state);
  }
  return timer.elapsed();
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

  const auto state = benchmark_state();
  const std::vector<Scenario> scenarios = {
      {.name = QStringLiteral("tiny-grid"), .width = 64, .height = 64},
      {.name = QStringLiteral("preview-960"), .width = 960, .height = 640},
      {.name = QStringLiteral("full-1920"), .width = 1920, .height = 1280},
  };

  QElapsedTimer suite_timer;
  suite_timer.start();
  for (const auto& scenario : scenarios) {
    const auto elapsed_ms = run_scenario(scenario, iterations, state);
    const auto pixel_count = scenario.width * scenario.height;
    photo_editor::desktop::observability::logInfo(
        photoEditorPerfLog(),
        QStringLiteral("benchmark.color_adjustment_pass.completed"),
        {
            {QStringLiteral("stage"), scenario.name},
            {QStringLiteral("width"), scenario.width},
            {QStringLiteral("height"), scenario.height},
            {QStringLiteral("pixels"), pixel_count},
            {QStringLiteral("iterations"), iterations},
            {QStringLiteral("elapsed_ms"), elapsed_ms},
            {QStringLiteral("avg_ms"), static_cast<double>(elapsed_ms) / iterations},
        });
    std::cout << scenario.name.toStdString() << "_elapsed_ms=" << elapsed_ms << '\n';
  }

  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.color_adjustment_suite.completed"),
      {
          {QStringLiteral("scenario_count"), static_cast<int>(scenarios.size())},
          {QStringLiteral("iterations"), iterations},
          {QStringLiteral("elapsed_ms"), suite_timer.elapsed()},
      });
  return EXIT_SUCCESS;
}
