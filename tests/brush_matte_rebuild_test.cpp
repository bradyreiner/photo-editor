#include "brush_matte_rebuild.h"
#include "brush_input_schema.h"

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

  // A legacy ellipse must keep its horizontal reach on a wide image. A new
  // retouch circle must have equal pixel radii instead of inheriting that tip.
  auto legacy=brush;
  legacy.insert("brushPoints",QVariantList{point(0.5,0.5)});
  legacy.insert("toolRole","retouch");
  legacy.insert("feather",0.05);
  const auto old_tip=mask_with_rebuilt_brush_matte(legacy,QSize(401,201),"old-tip");
  if (alpha_at(old_tip,275,100)!=255U || alpha_at(old_tip,200,175)!=0U)
    return fail("Legacy brush replay must retain its normalized ellipse on non-square outputs");
  auto modern=legacy;
  modern.insert("brushHardness",1.0);
  modern.insert("feather",0.0);
  const BrushPreviewPoint dab{.x=0.5,.y=0.5,.size=0.25,.flow=1.0};
  modern.insert("brushStrokes",QVariantList{brush_stroke_variant(modern,{dab},1)});
  const auto new_tip=mask_with_rebuilt_brush_matte(modern,QSize(401,201),"new-tip");
  if (alpha_at(new_tip,275,100)!=0U || alpha_at(new_tip,225,100)!=255U ||
      alpha_at(new_tip,200,125)!=255U)
    return fail("New retouch replay must preserve a circular pixel footprint and recorded hardness");
  auto companion=modern;
  companion.insert("toolRole","selection"); companion.insert("operation","subtract");
  companion.insert("brushCompositeSource",true);
  auto erase_dab=dab; erase_dab.erase=true;
  auto companion_stroke=brush_stroke_variant(companion,{erase_dab},1);
  if (companion_stroke.value("operation").toString()!="add" ||
      companion_stroke.value("samples").toList().front().toMap().value("erase").toBool())
    return fail("Subtractive companions must persist positive coverage rather than erase their empty source");
  // Also recognize previously generated companion records with erase samples.
  companion_stroke.insert("operation","erase");
  companion_stroke.insert("samples",QVariantList{brush_point_variant(erase_dab)});
  companion.insert("brushStrokes",QVariantList{companion_stroke});
  if (alpha_at(mask_with_rebuilt_brush_matte(companion,QSize(401,201),"companion"),200,100)!=255U)
    return fail("Subtractive companion replay must retain coverage before mask-set subtraction");
  // Independent crop rendering at the same source pixel pitch must equal an
  // actual crop of the full matte, including an off-crop stroke center.
  const QRectF area(0.625,0.25,0.375,0.5);
  for (auto history:{legacy,modern}) {
    const auto full=mask_with_rebuilt_brush_matte(history,QSize(401,201),"full");
    const auto cropped=mask_with_rebuilt_brush_matte(history,QSize(151,101),"crop",area);
    for (int y=0;y<101;++y) for (int x=0;x<151;++x)
      if (alpha_at(cropped,x,y)!=alpha_at(full,x+250,y+50))
        return fail("Cropped legacy/new replay must match exact full-source pixels without resizing the tip");
    for (const auto crop:{QRect(200,0,1,201),QRect(0,100,401,1),
             QRect(200,100,1,1),QRect(0,0,1,1)}) {
      const QRectF line_area(static_cast<double>(crop.x())/400,
          static_cast<double>(crop.y())/200,static_cast<double>(crop.width()-1)/400,
          static_cast<double>(crop.height()-1)/200);
      const auto line=mask_with_rebuilt_brush_matte(history,crop.size(),"line",line_area,QSizeF(400,200));
      for (int y=0;y<crop.height();++y) for (int x=0;x<crop.width();++x)
        if (alpha_at(line,x,y)!=alpha_at(full,x+crop.x(),y+crop.y()))
          return fail("One-pixel/line crops must sample exact source brush coverage without widening the span");
    }
  }
  // Mixed histories retain the original legacy reach and the newer circle;
  // changing the next brush settings cannot repaint either saved stroke.
  auto mixed=modern;
  auto old_dab=point(0.25,0.5);
  auto new_dab=dab; new_dab.x=0.75;
  mixed.insert("brushStrokes",QVariantList{
      QVariantMap{{"samples",QVariantList{old_dab}}},
      brush_stroke_variant(modern,{new_dab},2)});
  mixed.insert("brushHardness",0.0);
  const auto mix=mask_with_rebuilt_brush_matte(mixed,QSize(801,401),"mixed");
  if (alpha_at(mix,340,200)==0U || alpha_at(mix,740,200)!=0U)
    return fail("Mixed legacy and circular history must preserve each stroke's geometry at larger output sizes");

  // Source-aware evidence belongs to the committed matte even when only an
  // earlier stroke recorded the constraint and the current tool toggles it off.
  for (const auto flag:{QStringLiteral("autoMask"),QStringLiteral("protectEdges")}) {
    auto constrained=modern;
    auto strokes=constrained.value("brushStrokes").toList();
    auto saved=strokes.front().toMap(); auto settings=saved.value("settings").toMap();
    settings.insert(flag,true); saved.insert("settings",settings); strokes[0]=saved;
    constrained.insert("brushStrokes",strokes);
    constrained.insert("segmentationData",QByteArray(12,char(73)));
    const auto before=constrained;
    if (!brush_history_requires_source(constrained) ||
        !mask_with_rebuilt_brush_matte(constrained,QSize(801,401),"constrained").isEmpty())
      return fail("Historical source-aware settings must prohibit reconstruction without source colors");
    (void)rasterize_brush_history_into_mask(&constrained);
    if (constrained!=before)
      return fail("Source-free replay must never overwrite authoritative constrained alpha");
  }

  return EXIT_SUCCESS;
}
