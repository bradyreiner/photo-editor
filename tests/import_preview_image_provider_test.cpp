#include "import_preview_image_provider.h"
#include <QGuiApplication>
#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <iostream>
#include <memory>

using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QTemporaryDir root;
  const auto path = root.filePath("photo #1 100%.png");
  QImage fixture(320, 240, QImage::Format_RGBA8888);
  fixture.fill(Qt::blue);
  for (int y = 0; y < 120; ++y) for (int x = 0; x < 160; ++x) fixture.setPixelColor(x, y, Qt::red);
  if (!fixture.save(path)) return 1;
  ImportPreviewImageProvider provider;
  QSize size;
  const auto key = QString::fromLatin1(QUrl::toPercentEncoding(path));
  auto image = provider.requestImage(key, &size, QSize(128, 128));
  if (image.isNull() || size != QSize(128, 96) || image.pixelColor(10, 10).red() < 240
      || image.pixelColor(110, 80).blue() < 240 || !image.colorSpace().isValid()) {
    std::cerr << "Bounded source preview must preserve orientation, colors and encoded paths\n";
    return 1;
  }
  if (!provider.requestImage("relative.cr2", &size, {}).isNull()) return 1;
  const auto raw = qEnvironmentVariable("PHOTO_EDITOR_IMPORT_PREVIEW_RAW");
  if (!raw.isEmpty()) {
    QElapsedTimer timer; timer.start();
    image = provider.requestImage(QString::fromLatin1(QUrl::toPercentEncoding(raw)), &size, QSize(384, 384));
    if (image.isNull() || std::max(size.width(), size.height()) > 384) {
      std::cerr << "The real Canon RAW must decode through the bounded source preview\n";
      return 1;
    }
    std::cout << "RAW thumbnail: " << size.width() << 'x' << size.height() << "; elapsed " << timer.elapsed() << "ms\n";
  }
  QQmlEngine engine;
  engine.addImageProvider("import-preview", new ImportPreviewImageProvider);
  engine.rootContext()->setContextProperty("testSourcePath", raw.isEmpty() ? path : raw);
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import QtQuick.Controls.Basic
import "file:)QML" PHOTO_EDITOR_SOURCE_DIR R"QML(/apps/desktop/qml/components"
ApplicationWindow {
    width: 420; height: 400; visible: true; color: "#151515"
    ImportImageTile { objectName: "tile"; anchors.centerIn: parent; width: 320; height: 320
        filePath: testSourcePath; fileName: "Source preview"; extension: "CR2"
        fileUrl: "file:///must-not-probe-this-raw-with-qt.cr2"
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/import-preview-fixture.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (!window) {
    for (const auto& error : component.errors()) std::cerr << error.toString().toStdString() << '\n';
    return 1;
  }
  window->requestActivate();
  auto* tile = object->findChild<QQuickItem*>("tile");
  QElapsedTimer timer; timer.start();
  while (tile && !tile->property("previewReady").toBool() && timer.elapsed() < 15000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  if (!tile || !tile->property("previewReady").toBool()) {
    std::cerr << "Production ImportImageTile must load through the registered preview provider\n";
    return 1;
  }
  const auto evidence = qEnvironmentVariable("PHOTO_EDITOR_IMPORT_PREVIEW_CAPTURE");
  if (!evidence.isEmpty()) {
    QElapsedTimer settle; settle.start();
    while (settle.elapsed() < 300) { QCoreApplication::processEvents(); QThread::msleep(5); }
    window->grabWindow().save(evidence);
  }
  std::cout << "Source preview bounds, orientation, encoded paths and production RAW tile passed.\n";
  return 0;
}
