#include <cstdlib>
#include <iostream>

#include <QColor>
#include <QColorSpace>
#include <QCoreApplication>
#include <QImage>
#include <QVariantMap>

#include "photo_render_utils.h"

namespace {

constexpr int kPreviewExportParityTolerance = 2;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

int max_channel_delta(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) {
    return 255;
  }

  auto max_delta = 0;
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width(); ++x) {
      const auto lhs = left.pixelColor(x, y);
      const auto rhs = right.pixelColor(x, y);
      max_delta = std::max(max_delta, std::abs(lhs.red() - rhs.red()));
      max_delta = std::max(max_delta, std::abs(lhs.green() - rhs.green()));
      max_delta = std::max(max_delta, std::abs(lhs.blue() - rhs.blue()));
      max_delta = std::max(max_delta, std::abs(lhs.alpha() - rhs.alpha()));
    }
  }
  return max_delta;
}

QVariantMap solid_red_lut() {
  QVariantList table;
  for (int index = 0; index < 8; ++index) {
    table.append(QVariantMap{
        {QStringLiteral("red"), 1.0},
        {QStringLiteral("green"), 0.0},
        {QStringLiteral("blue"), 0.0},
        {QStringLiteral("alpha"), 1.0},
    });
  }

  QVariantMap lut;
  lut.insert(QStringLiteral("enabled"), true);
  lut.insert(QStringLiteral("strength"), 100.0);
  lut.insert(QStringLiteral("name"), QStringLiteral("Layer Red LUT"));
  lut.insert(QStringLiteral("size"), 2);
  lut.insert(QStringLiteral("table"), table);
  lut.insert(QStringLiteral("domainMin"), QVariantList{0.0, 0.0, 0.0});
  lut.insert(QStringLiteral("domainMax"), QVariantList{1.0, 1.0, 1.0});
  lut.insert(QStringLiteral("inputColorSpace"), QStringLiteral("sRGB"));
  lut.insert(QStringLiteral("outputColorSpace"), QStringLiteral("sRGB"));
  return lut;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  const auto preview_space = QColorSpace(QColorSpace::SRgb);
  QImage source(2, 1, QImage::Format_ARGB32);
  source.setColorSpace(preview_space);
  source.fill(QColor::fromRgb(72, 80, 88));

  QByteArray mask(2, char(0));
  mask[1] = char(0xff);

  QVariantMap lut_adjustments;
  lut_adjustments.insert(QStringLiteral("lutStack"), QVariantList{solid_red_lut()});

  QVariantMap layer;
  layer.insert(QStringLiteral("id"), QStringLiteral("masked-layer-lut"));
  layer.insert(QStringLiteral("name"), QStringLiteral("Masked Layer LUT"));
  layer.insert(QStringLiteral("kind"), QStringLiteral("adjustment"));
  layer.insert(QStringLiteral("enabled"), true);
  layer.insert(QStringLiteral("opacity"), 1.0);
  layer.insert(QStringLiteral("adjustments"), lut_adjustments);
  layer.insert(QStringLiteral("maskData"), mask);
  layer.insert(QStringLiteral("maskWidth"), 2);
  layer.insert(QStringLiteral("maskHeight"), 1);

  photo_editor::desktop::RenderEditState state;
  state.working_color_profile = QStringLiteral("Linear sRGB");
  state.preview_color_space = QStringLiteral("sRGB");
  state.output_color_space = QStringLiteral("sRGB");
  state.layers.append(layer);

  const auto preview = photo_editor::desktop::render_output_image_with_backend(
      source,
      state,
      preview_space,
      QImage::Format_ARGB32,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  const auto export_render = photo_editor::desktop::render_output_image_with_backend(
      source,
      state,
      preview_space,
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::CpuOnly);
  if (preview.isNull() || export_render.isNull()) {
    return fail("masked layer-local LUT preview/export proof should produce rendered images");
  }
  if (max_channel_delta(preview, export_render.convertToFormat(QImage::Format_ARGB32)) >
      kPreviewExportParityTolerance) {
    return fail("masked layer-local LUT preview should match the full-resolution export render");
  }

  const auto unmasked = preview.pixelColor(0, 0);
  const auto masked = preview.pixelColor(1, 0);
  // The display transform is allowed to map scene-linear LUT primaries away
  // from encoded 255/0/0. Prove the layer contract in the rendered output:
  // the unmasked pixel remains neutral and the masked pixel receives a strong,
  // isolated red displacement.
  if (unmasked.red() > unmasked.green() + 20 ||
      masked.red() < unmasked.red() + 100 ||
      masked.red() < masked.green() * 4 ||
      masked.red() < masked.blue() * 4) {
    std::cerr << "unmasked=" << unmasked.red() << ',' << unmasked.green() << ','
              << unmasked.blue() << " masked=" << masked.red() << ',' << masked.green() << ','
              << masked.blue() << '\n';
    return fail("layer-local LUT stacks should parse from layer adjustments and respect the layer mask");
  }

  return EXIT_SUCCESS;
}
