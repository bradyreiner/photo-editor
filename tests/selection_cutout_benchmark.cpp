#include "editor_preview_controller.h"
#include "photo_paint_surface.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QTemporaryDir temporary;
  QJsonArray rows;
  for (const auto size : {QSize(1600, 1000), QSize(3000, 2000)}) {
    QImage image(size, QImage::Format_RGB32);
    image.setColorSpace(QColorSpace::SRgb);
    for (int y = 0; y < size.height(); ++y)
      for (int x = 0; x < size.width(); ++x)
        image.setPixel(x, y, qRgb((x * 3 + y) % 256, (y * 2 + x / 3) % 256, (x + y / 2) % 256));
    const auto path = temporary.filePath(QString::number(size.width()) + ".png");
    if (!image.save(path)) return 1;
    for (const bool dense : {false, true}) for (int run = 0; run < 3; ++run) {
      EditorPreviewController editor;
      const auto wait = [&] (bool paint_only = false) {
        QElapsedTimer timer; timer.start();
        do { QCoreApplication::processEvents(QEventLoop::AllEvents, 2); QThread::msleep(1); }
        while ((editor.photoPaintBusy() || (!paint_only && (editor.processing() || editor.selectionWorkBusy()))) && timer.elapsed() < 120000);
        return !editor.photoPaintBusy() && (paint_only || (!editor.processing() && !editor.selectionWorkBusy()));
      };
      editor.loadAsset("cutout-benchmark", path, {}, {}, {}, {}, {});
      if (!wait()) return 2;
      editor.addMask("rectangle"); editor.setActiveMaskFeather(0.04);
      editor.setActiveMaskCenterX(0.5); editor.setActiveMaskCenterY(0.5);
      editor.setActiveMaskRadiusX(dense ? 0.46 : 0.08);
      editor.setActiveMaskRadiusY(dense ? 0.46 : 0.08);
      if (!wait()) return 3;
      QElapsedTimer timer; timer.start();
      if (!editor.copyPixelsFromSelection(-1, true) || !wait(true) || !editor.photoPaintError().isEmpty()) return 4;
      const auto elapsed = timer.nsecsElapsed() / 1e6;
      std::cout << "cutout " << size.width() << "x" << size.height() << (dense ? " dense " : " sparse ") << run << " " << elapsed << " ms" << std::endl;
      const auto layer = editor.photoLayers().first().toMap();
      const auto raster = rasterize_photo_paint_layer(layer);
      if (raster.isNull()) return 5;
      QCryptographicHash hash(QCryptographicHash::Sha256);
      hash.addData(QByteArrayView(reinterpret_cast<const char*>(raster.constBits()), raster.sizeInBytes()));
      rows.append(QJsonObject{{"width", size.width()}, {"height", size.height()}, {"dense", dense}, {"run", run},
          {"elapsedMs", elapsed}, {"rasterHash", QString::fromLatin1(hash.result().toHex())},
          {"tileBytes", QJsonDocument::fromVariant(layer.value("paintTiles")).toJson(QJsonDocument::Compact).size()}});
      if (!wait()) return 6;
    }
  }
  const auto json = QJsonDocument(QJsonObject{{"schema", "selection-cutout-benchmark:v1"}, {"rows", rows}}).toJson();
  if (argc > 1) { QFile output(QString::fromLocal8Bit(argv[1])); if (!output.open(QIODevice::WriteOnly)) return 7; output.write(json); }
  else std::cout << json.toStdString();
  return 0;
}
