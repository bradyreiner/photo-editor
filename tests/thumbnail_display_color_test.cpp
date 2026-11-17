#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QTemporaryDir temporary;
  QImage source(64, 64, QImage::Format_ARGB32);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  source.fill(QColor(60, 105, 146));
  const auto path = temporary.filePath("sky.png");
  if (!source.save(path)) return fail("cannot write tagged sky fixture");
  const auto cached_source = QImage(path);

  QQmlPropertyMap display;
  QQmlPropertyMap settings;
  settings.insert("interfaceScale", 1.0);
  settings.insert("interfaceDensity", "balanced");
  settings.insert("highContrast", false);
  settings.insert("reducedMotion", true);
  QQmlEngine engine;
  engine.rootContext()->setContextProperty("displayProfile", &display);
  engine.rootContext()->setContextProperty("appShell", &settings);
  engine.rootContext()->setContextProperty("sourceUrl", QUrl::fromLocalFile(path));
  QStringList warnings;
  QObject::connect(&engine, &QQmlEngine::warnings, [&warnings](const QList<QQmlError>& errors) {
    for (const auto& error : errors) warnings.append(error.toString());
  });
  display.insert("thumbnailDisplayColorSpace", QColorSpace(QColorSpace::SRgb));
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Window
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
Window {
    width: 320; height: 180; visible: true; color: "black"
    Item {
        id: fixtureWorkspace
        property var editorPreviewController: displayProfile
        property var catalogAssets: ({selectedIndex: 0})
        property color mint: "white"
        property color mintSoft: "white"
        property color hairlineStrong: "black"
        function assetThumb(index) { return sourceUrl }
        function assetRating(index) { return 0 }
        function assetPicked(index) { return false }
        function assetRejected(index) { return false }
        function aspectRatioText(w, h) { return "1:1" }
    }
    StudioThumbnailCard {
        objectName: "filmstripCard"; workspace: fixtureWorkspace
        x: 0; y: 0; width: 140; height: 140; filmstrip: true
    }
    StudioThumbnailCard {
        objectName: "galleryCard"; workspace: fixtureWorkspace
        x: 160; y: 0; width: 140; height: 140; selectionBadgeVisible: false
    }
})qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/thumbnail-color-fixture.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (!window) return fail(component.errorString());
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR");
  if (!output.isEmpty() && !QDir().mkpath(output)) return fail("cannot create proof directory");
  auto case_index = 0;
  for (const auto target : {QColorSpace::SRgb, QColorSpace::DisplayP3, QColorSpace::SRgb}) {
    if (case_index == 2) {
      source.fill(QColor(180, 120, 60));
      const auto next_path = temporary.filePath("next-photo.png");
      if (!source.save(next_path)) return fail("cannot write next photo fixture");
      engine.rootContext()->setContextProperty("sourceUrl", QUrl::fromLocalFile(next_path));
    }
    const auto space = QColorSpace(target);
    display.insert("thumbnailDisplayColorSpace", space);
    const auto expected = source.convertedToColorSpace(space).pixelColor(32, 32);
    QElapsedTimer timer;
    timer.start();
    auto capture = QImage{};
    auto matched = false;
    while (timer.elapsed() < 5000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      window->update();
      capture = window->grabWindow();
      if (!capture.isNull()) {
        const auto scale = capture.width() / 320.0;
        matched = true;
        for (const int x : {70, 230}) {
          const auto actual = capture.pixelColor(qRound(x * scale), qRound(70 * scale));
          matched = matched && std::abs(actual.red() - expected.red()) <= 1 &&
                    std::abs(actual.green() - expected.green()) <= 1 &&
                    std::abs(actual.blue() - expected.blue()) <= 1;
        }
        if (matched) break;
      }
      QThread::msleep(10);
    }
    if (!matched) {
      std::cerr << "target=" << space.description().toStdString()
                << " expected=" << expected.name().toStdString()
                << " capture=" << capture.width() << 'x' << capture.height();
      if (!capture.isNull()) {
        const auto scale = capture.width() / 320.0;
        for (const int x : {70, 230})
          std::cerr << " actual=" << capture.pixelColor(qRound(x * scale), qRound(70 * scale)).name().toStdString();
        if (!output.isEmpty()) capture.save(QDir(output).filePath("failed.png"));
      }
      std::cerr << '\n';
      return fail("filmstrip and gallery must convert cached sRGB into the active display profile, including profile changes");
    }
    if (!output.isEmpty() && !capture.save(QDir(output).filePath(
            QStringLiteral("thumbnail-%1-%2.png").arg(case_index).arg(
                target == QColorSpace::SRgb ? "srgb" : "p3"))))
      return fail("cannot save display color proof");
    ++case_index;
  }
  if (QImage(path) != cached_source || QImage(path).colorSpace() != cached_source.colorSpace())
    return fail("display conversion must preserve portable cached thumbnail pixels");
  for (const auto& warning : warnings) {
    if (!warning.contains("font")) return fail(warning);
  }
  std::cout << "filmstrip/gallery display pixels match sRGB and P3; cached source preserved\n";
  return EXIT_SUCCESS;
}
