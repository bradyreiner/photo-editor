#include "edit_adjustment_patch.h"
#include "flattened_output_renderer.h"
#include "photo_pixel_stroke.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>

using namespace photo_editor::desktop;

namespace {
bool save_json(const QString& path, const QJsonObject& object) {
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) &&
      file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) >= 0 && file.commit();
}
bool save_png(const QString& path, const QImage& image) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return false;
  QImageWriter writer(&file, "png");
  return writer.write(image) && file.commit();
}
}

// Brush-only authoring utility. The input contains sampled colors and stroke
// geometry, never source pixels or a photo-fragment brush. Every mark is drawn
// by the same PhotoPixelStroke engine used by the interactive paint tool.
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  if (app.arguments().size() != 4 && app.arguments().size() != 6) {
    std::cerr << "Usage: native_brush_score_driver score.json template-document.json NEW-output-directory [pose-fraction camera-fraction]\n";
    return 2;
  }
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QFile input(app.arguments()[1]), template_file(app.arguments()[2]);
  if (!input.open(QIODevice::ReadOnly) || input.size() > 256LL*1024*1024 ||
      !template_file.open(QIODevice::ReadOnly)) return 2;
  QJsonParseError error;
  const auto score = QJsonDocument::fromJson(input.readAll(), &error).object();
  if (error.error != QJsonParseError::NoError || score.value("schema") != "looksy-sampled-brush-score-v1") return 2;
  auto envelope = QJsonDocument::fromJson(template_file.readAll(), &error).object();
  if (error.error != QJsonParseError::NoError || envelope.isEmpty()) return 2;
  const QSize size(score.value("width").toInt(), score.value("height").toInt());
  if (size.width() < 16 || size.height() < 16 || size.width() > 4096 || size.height() > 4096) return 2;
  const auto output_path = QDir::cleanPath(QFileInfo(app.arguments()[3]).absoluteFilePath());
  if (QFileInfo::exists(output_path) || !QDir().mkpath(output_path)) return 2;
  const QDir output(output_path);
  const QColorSpace working(QColorSpace::SRgbLinear);
  const bool motion = app.arguments().size() == 6;
  const double pose = motion ? std::clamp(app.arguments()[4].toDouble(),0.0,1.0) : 0;
  const double camera = motion ? std::clamp(app.arguments()[5].toDouble(),0.0,1.0) : 0;
  QImage motion_canvas(size,QImage::Format_RGBA32FPx4_Premultiplied);
  motion_canvas.setColorSpace(working); motion_canvas.fill(Qt::transparent);
  QImage source(size, QImage::Format_RGBA64);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb)); source.fill(Qt::transparent);
  if (!save_png(output.filePath("blank-source.png"), source)) return 1;
  EditRecipeState state;
  // This is display-referred painted artwork; select Looksy's existing direct
  // rendering model instead of adding a photographic scene-to-display tone map.
  state.photographic_rendering_model = QStringLiteral("legacy-oklch-v1");
  state.working_color_profile = QStringLiteral("Linear sRGB");
  state.preview_output_mode = QStringLiteral("sRGB");
  const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  QElapsedTimer timer; timer.start();
  qint64 count = 0;
  QJsonArray layer_receipts;
  for (const auto& layer_value : score.value("layers").toArray()) {
    const auto plan = layer_value.toObject();
    QImage canvas = motion ? motion_canvas : QImage(size, QImage::Format_RGBA32FPx4_Premultiplied);
    if (!motion) { canvas.setColorSpace(working); canvas.fill(Qt::transparent); }
    const auto strokes = plan.value("strokes").toArray();
    for (const auto& value : strokes) {
      const auto s = value.toArray();
      if (s.size() < 9) return 2;
      double x=s[0].toDouble(), y=s[1].toDouble(), dx=s[2].toDouble(), dy=s[3].toDouble(), diameter=s[4].toDouble();
      if (motion && s.size() >= 16) {
        x+=pose*s[9].toDouble(); y+=pose*s[10].toDouble();
        dx+=pose*s[11].toDouble(); dy+=pose*s[12].toDouble();
      }
      if (motion) {
        const double zoom=1.005+.035*camera;
        x=(x-size.width()*.50)*zoom+size.width()*.50;
        y=(y-size.height()*.46)*zoom+size.height()*.46;
        dx*=zoom; dy*=zoom; diameter*=zoom;
      }
      if (!std::isfinite(x+y+dx+dy+diameter) || diameter < .5 || diameter > 256 || std::hypot(dx,dy)>512) return 2;
      const auto bounds = QRectF(QPointF(std::min(x-dx,x+dx),std::min(y-dy,y+dy)),
          QPointF(std::max(x-dx,x+dx),std::max(y-dy,y+dy)))
          .adjusted(-diameter,-diameter,diameter,diameter).toAlignedRect().intersected(QRect(QPoint(),size));
      if (bounds.isEmpty()) continue;
      PhotoPixelStroke brush;
      const auto channel=[&](int index) {
        const double a=s[index].toDouble();
        const double b=motion && s.size()>=16 ? s[index+8].toDouble() : a;
        return std::clamp(static_cast<int>(std::lround(a+(b-a)*pose)),0,255);
      };
      brush.color = QColor(channel(5),channel(6),channel(7));
      brush.diameter=diameter; brush.hardness=.85; brush.opacity=s[8].toDouble();
      brush.pressure_size=false; brush.pressure_flow=false; brush.spacing=.18;
      brush.texture_material=QStringLiteral("dry_ink"); brush.texture_strength=.035;
      brush.texture_seed=static_cast<quint32>(count+71);
      QVariantMap local{{"pixelWidth",bounds.width()},{"pixelHeight",bounds.height()},
          {"paintTiles",empty_photo_paint_tiles(working)}};
      if (!brush.begin(local)) return 1;
      // A bounded local patch carries the exact prior linear-float pixels.
      // This avoids recompressing a whole native paint layer for every mark.
      brush.surface.legacy=canvas.copy(bounds);
      brush.point(QPointF(x-dx,y-dy)-bounds.topLeft(),1);
      brush.point(QPointF(x+dx,y+dy)-bounds.topLeft(),1);
      brush.finish();
      if (!brush.error.isEmpty()) { std::cerr<<brush.error.toStdString()<<'\n'; return 1; }
      const auto patch=brush.surface.raster({},nullptr,brush.draft_tiles());
      if (patch.isNull()) return 1;
      QPainter painter(&canvas); painter.setCompositionMode(QPainter::CompositionMode_Source);
      painter.drawImage(bounds.topLeft(),patch); painter.end();
      ++count;
    }
    if (motion) {
      motion_canvas=canvas;
      continue;
    }
    PhotoPaintSurface surface; surface.size=size; surface.color_space=working;
    surface.payload=empty_photo_paint_tiles(working);
    for (int y=0;y<size.height();y+=kPhotoPaintTileEdge)
      for (int x=0;x<size.width();x+=kPhotoPaintTileEdge)
        surface.store(x/kPhotoPaintTileEdge,y/kPhotoPaintTileEdge,canvas.copy(surface.tile_rect(x/kPhotoPaintTileEdge,y/kPhotoPaintTileEdge)));
    const auto layer_id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    state.layers.prepend(QVariantMap{{"id",layer_id},{"kind","pixel"},{"name",plan.value("name").toString()},
        {"visible",true},{"opacity",1.0},{"blendMode","normal"},{"pixelWidth",size.width()},
        {"pixelHeight",size.height()},{"paintTiles",surface.snapshot()}});
    layer_receipts.append(QJsonObject{{"id",layer_id},{"name",plan.value("name")},{"strokes",strokes.size()}});
    std::cout<<plan.value("name").toString().toStdString()<<": "<<strokes.size()<<" strokes; total "<<count<<" in "<<timer.elapsed()<<"ms\n"<<std::flush;
  }
  if (motion) {
    const auto image=convert_tagged_qimage_color_space(motion_canvas,QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA64,QImageColorConversionPurpose::OutputPublication);
    if (image.isNull() || !save_png(output.filePath("painted-frame.png"),image)) return 1;
    std::cout<<"Native temporal brush frame: "<<count<<" marks in "<<timer.elapsed()<<"ms\n";
    return 0;
  }
  auto document=envelope.value("document").toObject();
  auto slide=document.value("slides").toArray().first().toObject();
  auto primary=slide.value("primary").toObject();
  primary.insert("filePath",output.filePath("blank-source.png"));
  primary.insert("sourcePixelWidth",size.width()); primary.insert("sourcePixelHeight",size.height());
  primary.remove("sourceFingerprint"); primary.remove("sourceFileSize"); primary.remove("sourceModifiedMs");
  primary.insert("adjustmentPatch",serialize_adjustment_patch(state,all_adjustment_groups(),AdjustmentPatchScope::AssetBound));
  primary.insert("displayLabel",score.value("name"));
  slide.insert("primary",primary);
  slide.insert("editBoardId",QUuid::createUuid().toString(QUuid::WithoutBraces));
  slide.insert("boardName",score.value("name"));
  document.insert("slides",QJsonArray{slide}); document.insert("items",QJsonArray{});
  document.insert("documentName",score.value("name"));
  document.insert("selectedItemId",QString{}); document.insert("selectedItemIds",QJsonArray{});
  document.insert("customPixelWidth",size.width()); document.insert("customPixelHeight",size.height());
  const auto now=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  document.insert("documentSavedAtUtc",now);
  envelope.insert("documentId",id); envelope.insert("name",score.value("name"));
  envelope.insert("createdAt",now); envelope.insert("updatedAt",now); envelope.insert("document",document);
  if (!save_json(output.filePath("native-document.json"),envelope)) return 1;
  const auto image=render_flattened_output_file(id,output.filePath("blank-source.png"),state,
      QColorSpace(QColorSpace::SRgb),QImage::Format_RGBA64);
  if (image.isNull() || !save_png(output.filePath("painted-frame.png"),image)) return 1;
  if (!save_json(output.filePath("receipt.json"),QJsonObject{{"renderer","Looksy PhotoPixelStroke + native flattened output"},
      {"strokes",count},{"layers",layer_receipts},{"width",size.width()},{"height",size.height()},
      {"elapsedMs",timer.elapsed()},{"referenceProvenance",score.value("referenceProvenance")},
      {"sourcePixelImport",false}})) return 1;
  std::cout<<"Native brush reconstruction complete: "<<count<<" strokes\n";
  return 0;
}
