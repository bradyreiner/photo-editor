#include "photo_pixel_stroke.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QVariantMap>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using namespace photo_editor::desktop;

bool benchmark_commit() {
  QVariantMap layer{{"kind", "pixel"}, {"pixelWidth", 6000}, {"pixelHeight", 4000},
      {"paintTiles", empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear))}};
  PhotoPixelStroke original;
  original.color = Qt::white;
  original.diameter = 1024;
  original.hardness = 0.8;
  original.flow = 1.0;
  original.opacity = 1.0;
  original.pressure_size = false;
  if (!original.begin(layer)) return false;
  original.point({1500, 2000}, 1.0);
  original.point({3900, 2000}, 1.0);
  original.finish();
  const auto draft = original.draft_tiles();
  std::vector<double> candidate_times, reference_times;
  for (int trial = 0; trial < 7; ++trial) {
    auto candidate = original;
    auto reference = original.surface;
    QElapsedTimer timer;
    QVariantMap candidate_payload;
    double candidate_ms = 0.0, reference_ms = 0.0;
    const auto run_candidate = [&] {
      timer.start();
      candidate_payload = candidate.commit();
      candidate_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    };
    const auto run_reference = [&] {
      timer.start();
      for (auto it = draft.cbegin(); it != draft.cend(); ++it) {
        const auto coordinates = it.key().split(',');
        reference.store(coordinates[0].toInt(), coordinates[1].toInt(), it.value().value<QImage>());
      }
      reference_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    };
    if (trial % 2 == 0) { run_candidate(); run_reference(); }
    else { run_reference(); run_candidate(); }
    if (candidate_payload != reference.snapshot() || candidate.draft_tiles() != draft)
      return false;
    if (trial >= 2) {
      candidate_times.push_back(candidate_ms);
      reference_times.push_back(reference_ms);
    }
  }
  std::sort(candidate_times.begin(), candidate_times.end());
  std::sort(reference_times.begin(), reference_times.end());
  std::cout << "brush_commit_benchmark trials=5 reference_median_ms=" << reference_times[2]
            << " candidate_median_ms=" << candidate_times[2]
            << " touched_tiles=" << draft.size() << " bit_exact=1\n";
  return true;
}

bool verify_commit(QSize size, double diameter, QPointF first, QPointF last,
                   bool cancel = false) {
  QVariantMap layer{{"kind", "pixel"}, {"pixelWidth", size.width()},
      {"pixelHeight", size.height()},
      {"paintTiles", empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear))}};
  PhotoPixelStroke stroke;
  stroke.color = QColor(230, 85, 40, 191);
  stroke.diameter = diameter;
  stroke.hardness = 0.8;
  stroke.opacity = 0.72;
  stroke.pressure_size = false;
  if (!stroke.begin(layer)) return false;
  stroke.point(first, 1.0);
  stroke.point(last, 1.0);
  stroke.finish();
  const auto held_draft = stroke.draft_tiles();
  const auto original = stroke.surface.snapshot();
  if (cancel) {
    stroke.cancelled->store(true);
    return stroke.commit() == original && stroke.draft_tiles() == held_draft;
  }

  // The public single-tile store is the serial encoding reference, including
  // its byte order, compression level, base64 data, and recipe metadata.
  auto serial = stroke.surface;
  for (auto it = held_draft.cbegin(); it != held_draft.cend(); ++it) {
    const auto coordinates = it.key().split(',');
    serial.store(coordinates[0].toInt(), coordinates[1].toInt(), it.value().value<QImage>());
  }
  const auto committed = stroke.commit();
  if (committed != serial.snapshot() || stroke.draft_tiles() != held_draft) return false;

  layer.insert("paintTiles", committed);
  PhotoPaintSurface restored;
  if (!restored.load(layer)) return false;
  for (auto it = held_draft.cbegin(); it != held_draft.cend(); ++it) {
    const auto coordinates = it.key().split(',');
    const auto decoded = restored.tile(coordinates[0].toInt(), coordinates[1].toInt());
    const auto expected = it.value().value<QImage>();
    if (decoded.size() != expected.size() || decoded.format() != expected.format() ||
        decoded.sizeInBytes() != expected.sizeInBytes() ||
        std::memcmp(decoded.constBits(), expected.constBits(),
            static_cast<std::size_t>(expected.sizeInBytes())) != 0) return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains(QStringLiteral("--benchmark")))
    return benchmark_commit() ? 0 : 1;
  if (!verify_commit({2048, 1024}, 1024, {512.25, 512.25}, {1536.75, 512.25}) ||
      !verify_commit({192, 128}, 24, {30.25, 64.25}, {160.75, 64.25}) ||
      !verify_commit({8192, 1}, 32, {0.5, 0.5}, {8191.5, 0.5}) ||
      !verify_commit({2048, 1024}, 1024, {512.25, 512.25}, {1536.75, 512.25}, true)) {
    std::cerr << "Paint commit differs from serial encoding, mutates a held draft, or ignores cancellation.\n";
    return 1;
  }
  return 0;
}
