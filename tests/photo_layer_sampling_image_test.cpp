#include "photo_layer_compiler.h"
#include "desktop_pipeline_state_compiler.h"
#include "photo_layer_source.h"
#include "photo_render_utils.h"
#include "working_image.h"
#include "photo_editor/pipeline/color_fill_layer_program.h"
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPainter>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>

using namespace photo_editor::desktop;
using namespace photo_editor::pipeline;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir fixture;
  QString path, output;
  bool native = false, full = false;
  double scale = 0.93;
  int benchmark_frames = 0;
  bool probe_admission = false;
  for (int i = 3; i < argc; ++i) {
    const auto option = QString::fromLocal8Bit(argv[i]);
    if (option == "--metal") native = true;
    else if (option == "--probe-admission") probe_admission = true;
    else if (option == "--full-resolution") full = true;
    else if (option == "--benchmark-frames" && i+1 < argc) {
      bool parsed = false; benchmark_frames = QString::fromLocal8Bit(argv[++i]).toInt(&parsed);
      if (!parsed || benchmark_frames < 4 || benchmark_frames > 16) return 1;
    }
    else if (option == "--scale" && i+1 < argc) {
      bool parsed = false; scale = QString::fromLocal8Bit(argv[++i]).toDouble(&parsed);
      if (!parsed || !std::isfinite(scale) || scale < 0.05 || scale > 32) return 1;
    } else return 1;
  }
  if (argc >= 3) {
    path = QString::fromLocal8Bit(argv[1]); output = QString::fromLocal8Bit(argv[2]);
    if (!QFileInfo(path).isFile() || !QFileInfo(output).isAbsolute() || QFileInfo::exists(output) || !QDir().mkpath(output)) return 1;
  } else if (argc == 1 || argc == 2) {
    path = fixture.filePath("cutout.png"); output = fixture.path();
    if (argc == 2) {
      output = QString::fromLocal8Bit(argv[1]);
      if (!QFileInfo(output).isAbsolute() || QFileInfo::exists(output) || !QDir().mkpath(output)) return 1;
    }
    QImage image(128, 96, QImage::Format_RGBA8888); image.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
      const double d = std::hypot((x - 64) / 50.0, (y - 48) / 36.0);
      image.setPixelColor(x, y, d < 1 ? QColor(60, 210, 120, qRound(std::clamp((1-d)*12, 0.0, 1.0)*255)) : QColor(255, 0, 255, 0));
    }
    if (!image.save(path)) return 1;
  } else return 1;
  if (benchmark_frames && !native) return 1;
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  std::unique_ptr<metal::MetalPipelineBackend> backend;
  if (native) { backend = std::make_unique<metal::MetalPipelineBackend>(); if (!backend->available()) return 1; }
#else
  if (native) return 1;
#endif
  const auto decoded = decode_photo_layer_source_image(path, full ? 0 : 640);
  if (decoded.isNull()) return 1;
  const QSize canvas(qCeil(decoded.width()*scale) + 192, qCeil(decoded.height()*scale) + 192);
  QImage clear(canvas, QImage::Format_RGBA32FPx4); clear.fill(Qt::transparent);
  clear.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
  for (const auto* profile : {"Linear sRGB", "Linear Display P3", "Linear Rec.2020", "Linear ProPhoto RGB"}) {
    if (benchmark_frames && QString::fromLatin1(profile) != "Linear ProPhoto RGB") continue;
    EditRecipeState recipe; recipe.working_color_profile = QString::fromLatin1(profile);
    recipe.photographic_rendering_model = "aces2-ocio-v1";
    recipe.layers = {QVariantMap{{"id", "transformed-photo"}, {"kind", "source"}, {"sourcePath", path},
        {"pixelWidth", decoded.width()}, {"pixelHeight", decoded.height()},
        {"canvasWidth", canvas.width()}, {"canvasHeight", canvas.height()},
        {"x", 70}, {"y", 60}, {"scale", scale}, {"rotation", 17.0}, {"opacity", 0.8}}};
    recipe = parse_edit_recipe_json(serialize_edit_recipe_json(recipe));
    ColorAdjustmentState state; state.color_pipeline.working_profile = working_color_profile_from_name(profile);
    state.photo_layers = compile_pipeline_photo_layers(recipe.layers, {}, LocalMaskRasterPolicy::UseStoredRaster,
        canvas, {}, state.color_pipeline, DesktopRenderPurpose::Output);
    if (state.photo_layers.size() != 1 || state.photo_layers[0].pixels.empty()) return 1;
    const auto& layer = state.photo_layers[0];
    const std::vector<ColorPixel> input(static_cast<std::size_t>(canvas.width() * canvas.height()), ColorPixel{0,0,0,0});
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
    if (benchmark_frames) {
      const auto descriptor = RenderSurfaceDescriptor{.pixel_encoding = RenderPixelEncoding::SceneLinearFloat,
          .color_domain = RenderColorDomain::WorkingScene, .range = RenderRangeModel::SceneExtended,
          .alpha = RenderAlphaModel::Straight, .color_space_identity = state.color_pipeline.working_profile.color_space.name,
          .transfer_identity = "linear", .width = static_cast<std::uint32_t>(canvas.width()),
          .height = static_cast<std::uint32_t>(canvas.height())};
      for (int frame = 0; frame < benchmark_frames; ++frame) {
        state.photo_layers[0].opacity = 0.55+0.02*frame;
        QElapsedTimer clock; clock.start();
        double admission_ms = 0;
        if (probe_admission) {
          if (!retained_simple_layers_supported(state)) return 1;
          admission_ms = static_cast<double>(clock.nsecsElapsed())/1e6;
          clock.restart();
        }
        { const auto probe = compile_fill_layer_program(state, canvas.width(), canvas.height()); if (!probe.ready()) return 1; }
        const double compile_ms = static_cast<double>(clock.nsecsElapsed())/1e6;
        clock.restart();
        ColorAdjustmentTelemetry telemetry;
        const auto surface = backend->publish_masks_and_color_lut_surface(input, canvas.width(), canvas.height(), state, {},
            descriptor, &telemetry, {}, 1.0, false);
        std::vector<ColorPixel> pixels;
        if (!surface || !surface->copy_pixels(&pixels) || pixels.size() != input.size()) return 1;
        const double elapsed_ms = static_cast<double>(clock.nsecsElapsed())/1e6;
        const auto bytes = QByteArrayView(reinterpret_cast<const char*>(pixels.data()), static_cast<qsizetype>(pixels.size()*sizeof(ColorPixel)));
        const QJsonObject row{{"event", "benchmark.layer_publish.sample"}, {"frame",frame},
            {"phase",frame == 0 ? "cold" : "warm"}, {"elapsed_ms",elapsed_ms}, {"compile_probe_ms",compile_ms},
            {"admission_probe_ms",admission_ms},
            {"gpu_cpu_to_gpu_bytes",static_cast<double>(telemetry.gpu_cpu_to_gpu_bytes)},
            {"sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}};
        std::cout << QJsonDocument(row).toJson(QJsonDocument::Compact).constData() << '\n';
      }
      continue;
    }
#endif
    auto cpu = input, retained = input;
    const auto program = compile_fill_layer_program(state, canvas.width(), canvas.height());
    QElapsedTimer timer; timer.start();
    if (!apply_color_layers_stage(&cpu, canvas.width(), canvas.height(), state)) return 1;
    const double cpu_ms = static_cast<double>(timer.nsecsElapsed())/1e6;
    timer.restart();
    if (!program.ready() || !apply_compiled_fill_layer_program(&retained, program)) return 1;
    const double retained_ms = static_cast<double>(timer.nsecsElapsed())/1e6;
    double gpu_ms = 0;
    std::vector<ColorPixel> gpu;
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
    if (backend) {
      const auto descriptor = RenderSurfaceDescriptor{.pixel_encoding = RenderPixelEncoding::SceneLinearFloat,
          .color_domain = RenderColorDomain::WorkingScene, .range = RenderRangeModel::SceneExtended,
          .alpha = RenderAlphaModel::Straight, .color_space_identity = state.color_pipeline.working_profile.color_space.name,
          .transfer_identity = "linear", .width = static_cast<std::uint32_t>(canvas.width()),
          .height = static_cast<std::uint32_t>(canvas.height())};
      ColorAdjustmentTelemetry telemetry;
      timer.restart();
      const auto surface = backend->publish_masks_and_color_lut_surface(input, canvas.width(), canvas.height(), state, {},
          descriptor, &telemetry, {}, 1.0, false);
      if (!surface || !surface->copy_pixels(&gpu) || gpu.size() != input.size()) return 1;
      gpu_ms = static_cast<double>(timer.nsecsElapsed())/1e6;
    }
#endif
    double cpu_error = 0, retained_error = 0, gpu_error = 0;
    const auto radians = -17.0 * std::numbers::pi / 180.0;
    for (int y = 0; y < canvas.height(); ++y) for (int x = 0; x < canvas.width(); ++x) {
      // Reference geometry comes from the persisted recipe, not compiled uniforms.
      const double dx = x + 0.5 - 70 - decoded.width() * scale / 2;
      const double dy = y + 0.5 - 60 - decoded.height() * scale / 2;
      const double sx = (dx * std::cos(radians) - dy * std::sin(radians)) / scale + decoded.width() / 2.0 - 0.5;
      const double sy = (dx * std::sin(radians) + dy * std::cos(radians)) / scale + decoded.height() / 2.0 - 0.5;
      const double radius = std::max(1.0,1.0/scale);
      double nx = 0, ny = 0;
      for (int i = static_cast<int>(std::ceil(sx-radius)); i <= static_cast<int>(std::floor(sx+radius)); ++i)
        nx += std::max(0.0,1-std::abs(sx-i)/radius);
      for (int i = static_cast<int>(std::ceil(sy-radius)); i <= static_cast<int>(std::floor(sy+radius)); ++i)
        ny += std::max(0.0,1-std::abs(sy-i)/radius);
      double expected[4]{};
      for (int iy = static_cast<int>(std::ceil(sy-radius)); iy <= static_cast<int>(std::floor(sy+radius)); ++iy)
        for (int ix = static_cast<int>(std::ceil(sx-radius)); ix <= static_cast<int>(std::floor(sx+radius)); ++ix) {
        if (ix < 0 || iy < 0 || ix >= layer.pixel_width || iy >= layer.pixel_height) continue;
        const auto p = layer.pixels[static_cast<std::size_t>(iy * layer.pixel_width + ix)];
        const double w = std::max(0.0,1-std::abs(sx-ix)/radius)*std::max(0.0,1-std::abs(sy-iy)/radius)*p.alpha*0.8/(nx*ny);
        expected[0] += w * p.red; expected[1] += w * p.green; expected[2] += w * p.blue; expected[3] += w;
      }
      const auto error = [&](ColorPixel p) {
        return std::max({std::abs(p.red*p.alpha-expected[0]), std::abs(p.green*p.alpha-expected[1]),
            std::abs(p.blue*p.alpha-expected[2]), std::abs(p.alpha-expected[3])});
      };
      const auto i = static_cast<std::size_t>(y * canvas.width() + x);
      cpu_error = std::max(cpu_error, error(cpu[i])); retained_error = std::max(retained_error, error(retained[i]));
      if (!gpu.empty()) gpu_error = std::max(gpu_error, error(gpu[i]));
    }
    const auto preview = render_output_image_with_backend(clear, recipe, QColorSpace(QColorSpace::SRgb),
        QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    const auto output16 = render_output_image_with_backend(clear, recipe, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA64, RenderBackendPreference::CpuOnly);
    if (preview.size() != canvas || output16.size() != canvas) return 1;
    const auto output8 = output16.convertToFormat(QImage::Format_ARGB32);
    int parity = 0, hidden_rgb_error = 0;
    for (int y = 0; y < canvas.height(); ++y) for (int x = 0; x < canvas.width(); ++x) {
      const auto p = preview.pixelColor(x,y), o = output8.pixelColor(x,y);
      const auto rgb_error = std::max({std::abs(p.red()-o.red()), std::abs(p.green()-o.green()), std::abs(p.blue()-o.blue())});
      // Straight RGB is not observable when both published alphas are zero.
      // Keep checking alpha everywhere and the same one-level RGB gate wherever
      // either publication has coverage; do not penalize hidden color payloads.
      parity = std::max(parity, std::abs(p.alpha()-o.alpha()));
      if (p.alpha() > 0 || o.alpha() > 0) parity = std::max(parity, rgb_error);
      else hidden_rgb_error = std::max(hidden_rgb_error, rgb_error);
    }
    std::cout << profile << " source=" << decoded.width() << 'x' << decoded.height() << " scale=" << scale
              << " CPU=" << cpu_error << " retained=" << retained_error
              << " Metal=" << gpu_error << " native=" << native << " preview8_output16=" << parity
              << " zero_alpha_RGB_difference=" << hidden_rgb_error
              << " CPU_ms=" << cpu_ms << " retained_ms=" << retained_ms << " Metal_publish_readback_ms=" << gpu_ms << '\n';
    if (cpu_error > 2e-6 || retained_error > 0.0002 || gpu_error > 0.0002 || parity > 1) return 1;
    if (!output16.save(QDir(output).filePath(QString::fromLatin1(profile) + ".png"))) return 1;
    QImage checker(canvas, QImage::Format_RGB32); checker.setColorSpace(QColorSpace(QColorSpace::SRgb));
    QPainter painter(&checker);
    for (int y=0; y<canvas.height(); y+=32) for(int x=0; x<canvas.width(); x+=32)
      painter.fillRect(x,y,32,32,(x/32+y/32)%2 ? QColor(190,190,190) : QColor(240,240,240));
    painter.drawImage(0,0,preview); painter.end();
    if (!checker.save(QDir(output).filePath(QString::fromLatin1(profile) + "-checker.png"))) return 1;
  }
  return 0;
}
