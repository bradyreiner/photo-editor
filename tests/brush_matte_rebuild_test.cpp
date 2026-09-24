#include "brush_matte_rebuild.h"

#include <cstdlib>
#include <iostream>

#include <QByteArray>
#include <QVariantList>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QVariantMap point(double x, double y, bool erase = false) {
  QVariantMap value;
  value.insert(QStringLiteral("x"), x);
  value.insert(QStringLiteral("y"), y);
  value.insert(QStringLiteral("size"), 0.25);
  value.insert(QStringLiteral("flow"), 1.0);
  value.insert(QStringLiteral("opacity"), 1.0);
  if (erase) {
    value.insert(QStringLiteral("erase"), true);
  }
  return value;
}

unsigned char alpha_at(const QVariantMap& mask, int x, int y) {
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  return static_cast<unsigned char>(data.at(y * width + x));
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  QVariantMap non_brush;
  non_brush.insert(QStringLiteral("kind"), QStringLiteral("subject"));
  QVariantMap empty_brush;
  empty_brush.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  if (!mask_with_rebuilt_brush_matte(non_brush, QSize(9, 9), QStringLiteral("preview"))
           .isEmpty() ||
      !mask_with_rebuilt_brush_matte(empty_brush, QSize(9, 9), QStringLiteral("preview"))
           .isEmpty() ||
      !mask_with_rebuilt_brush_matte(empty_brush, QSize(1, 9), QStringLiteral("preview"))
           .isEmpty()) {
    return fail("brush matte rebuild must reject non-brush, empty, and degenerate requests");
  }

  QVariantMap brush;
  brush.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  brush.insert(QStringLiteral("segmentationWidth"), 4);
  brush.insert(QStringLiteral("segmentationHeight"), 3);
  brush.insert(QStringLiteral("feather"), 0.05);
  brush.insert(QStringLiteral("brushPoints"), QVariantList{point(0.5, 0.5)});
  const auto rebuilt = mask_with_rebuilt_brush_matte(
      brush, QSize(9, 9), QStringLiteral("full-resolution-render"));
  if (rebuilt.value(QStringLiteral("segmentationWidth")).toInt() != 9 ||
      rebuilt.value(QStringLiteral("segmentationHeight")).toInt() != 9 ||
      alpha_at(rebuilt, 4, 4) != 255U || alpha_at(rebuilt, 0, 0) != 0U ||
      rebuilt.value(QStringLiteral("scaledMattePurpose")).toString() !=
          QStringLiteral("full-resolution-render") ||
      rebuilt.value(QStringLiteral("scaledMatteSourceWidth")).toInt() != 4 ||
      rebuilt.value(QStringLiteral("scaledMatteSourceHeight")).toInt() != 3 ||
      rebuilt.value(QStringLiteral("scaledMatteWidth")).toInt() != 9 ||
      rebuilt.value(QStringLiteral("scaledMatteHeight")).toInt() != 9 ||
      !rebuilt.value(QStringLiteral("scaledMatteRebuiltFromBrushStrokes")).toBool() ||
      rebuilt.value(QStringLiteral("scaledMatteBrushReplayPointCount")).toInt() != 1 ||
      rebuilt.value(QStringLiteral("scaled_matte_rebuild_pixels")).toLongLong() != 81 ||
      rebuilt.value(QStringLiteral("full_resolution_matte_rebuild_pixels")).toLongLong() != 81 ||
      !rebuilt.contains(QStringLiteral("scaled_matte_rebuild_ms")) ||
      !rebuilt.contains(QStringLiteral("full_resolution_matte_rebuild_ms"))) {
    return fail("brush matte rebuild must replay exact alpha and publish complete size telemetry");
  }

  brush.insert(
      QStringLiteral("brushPoints"),
      QVariantList{point(0.5, 0.5), point(0.5, 0.5, true)});
  const auto erased =
      mask_with_rebuilt_brush_matte(brush, QSize(9, 9), QStringLiteral("preview"));
  if (alpha_at(erased, 4, 4) != 0U ||
      erased.value(QStringLiteral("scaledMatteBrushReplayPointCount")).toInt() != 2 ||
      erased.contains(QStringLiteral("full_resolution_matte_rebuild_pixels"))) {
    return fail("brush matte replay must preserve point order and scope full-resolution telemetry");
  }

  return EXIT_SUCCESS;
}
