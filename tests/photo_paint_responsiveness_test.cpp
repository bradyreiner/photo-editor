#include "photo_pixel_stroke.h"

#include <QCoreApplication>
#include <iostream>

using namespace photo_editor::desktop;

namespace {
QVariantMap layer() {
  return {{"kind", "pixel"}, {"pixelWidth", 1536}, {"pixelHeight", 1024},
      {"paintTiles", empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear))}};
}

bool verify_chunks(const QString& material, bool erase, int budget) {
  PhotoPixelStroke direct;
  direct.color = QColor(240, 70, 35, 191);
  direct.diameter = 96;
  direct.spacing = 0.08;
  direct.flow = 0.37;
  direct.opacity = 0.64;
  direct.hardness = 0.73;
  direct.erase = erase;
  direct.texture_material = material;
  direct.pressure_flow = true;
  direct.light_taper = 0.6;
  direct.layer_to_document.translate(30, 20);
  direct.layer_to_document.rotate(7);
  direct.layer_to_document.scale(0.9, 0.9);
  direct.selection = QImage(1600, 1200, QImage::Format_Grayscale8);
  direct.selection.fill(153);
  if (material == QStringLiteral("photo_fragment")) {
    direct.tip_image = QImage(43, 27, QImage::Format_ARGB32_Premultiplied);
    direct.tip_image.fill(QColor(20, 120, 240, 173));
    direct.tip_image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    direct.tip_rotation_variation = 35;
    direct.tip_scale_variation = 0.4;
  }
  auto target = layer();
  if (erase) {
    PhotoPaintSurface surface;
    if (!surface.load(target)) return false;
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 12; ++x) {
      auto tile = surface.tile(x, y);
      tile.fill(QColor(120, 80, 20, 210));
      surface.store(x, y, tile);
    }
    target.insert("paintTiles", surface.snapshot());
  }
  if (!direct.begin(target)) return false;
  auto chunked = direct;
  const std::vector<PhotoPaintPoint> points{{{90.25, 240.5}, 0.2},
      {{1280.75, 240.5}, 1}, {{1280.75, 240.5}, 0.6},
      {{700.5, 720.25}, 0.8}, {{100.25, 720.25}, 0}};
  bool yielded = false;
  QVariantMap held_draft;
  QVariantMap held_copy;
  for (const auto& point : points) {
    direct.point(point.position, point.pressure);
    int iterations = 0;
    while (!chunked.point_chunk(point.position, point.pressure, budget)) {
      yielded = true;
      if (++iterations > 10000) return false;
      if (held_draft.isEmpty()) {
        held_draft = chunked.draft_tiles();
        for (auto it = held_draft.cbegin(); it != held_draft.cend(); ++it)
          held_copy.insert(it.key(), it.value().value<QImage>().copy());
      }
    }
  }
  direct.finish();
  chunked.finish();
  return yielded && direct.error.isEmpty() && chunked.error.isEmpty() &&
      direct.draft_tiles() == chunked.draft_tiles() && held_draft == held_copy;
}

bool verify_progress_and_cancel() {
  PhotoPixelStroke stroke;
  stroke.diameter = 128;
  stroke.pressure_size = false;
  if (!stroke.begin(layer())) return false;
  if (!stroke.point_chunk({100, 512}, 1)) return false;
  if (stroke.point_chunk({1400, 512}, 1)) return false;
  // A sparse, long event publishes real paint before consuming its endpoint.
  const auto draft = stroke.draft_tiles();
  if (draft.isEmpty() || !stroke.changed || stroke.changed_tile_count() > 6) return false;
  stroke.cancelled->store(true);
  return stroke.point_chunk({1400, 512}, 1) && stroke.draft_tiles() == draft;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  for (const auto& material : {QStringLiteral("solid"), QStringLiteral("dry_ink"),
           QStringLiteral("graphite"), QStringLiteral("light_paint"), QStringLiteral("photo_fragment")}) {
    for (int budget : {1, 7, 31}) {
      if (!verify_chunks(material, false, budget)) {
        std::cerr << "Chunked paint changed pixels: " << material.toStdString() << " budget=" << budget << '\n';
        return 1;
      }
    }
  }
  if (!verify_chunks(QStringLiteral("solid"), true, 1) || !verify_progress_and_cancel()) return 2;
  std::cout << "Incremental dabs preserve exact pixels, pressure, selection, transforms, materials, "
               "held drafts, erasing and cancellation.\n";
  return 0;
}
