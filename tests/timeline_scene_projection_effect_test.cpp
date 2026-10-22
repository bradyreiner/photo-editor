#include <QGuiApplication>
#include <QColor>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QThread>
#include <QTemporaryDir>
#include <QUrl>

#include <iostream>
#include <cmath>
#include <memory>

namespace {
const char kProjectionHostQml[] = R"QML(
import QtQuick
import QtQuick.Window
import "../apps/desktop/qml/components" as Components

Window {
    width: 320
    height: 240
    visible: true
    color: "#102030"

    Item {
        id: sourceFrame
        objectName: "sceneProjectionSourceFrame"
        width: 320
        height: 240
        Rectangle {
            anchors.fill: parent
            color: "#f020d0"
        }
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            width: parent.width / 2
            height: parent.height / 2
            color: "#20e060"
        }
        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: parent.width / 2
            height: parent.height / 2
            color: "#2070f0"
        }
    }

    Components.TimelineSceneProjectionEffect {
        objectName: "timelineSceneProjectionTestEffect"
        anchors.fill: parent
        source: ShaderEffectSource {
            sourceItem: sourceFrame
            hideSource: true
            live: true
        }
        sceneQuad: [
            {"x": 0.25, "y": 0.20},
            {"x": 0.75, "y": 0.10},
            {"x": 0.20, "y": 0.85},
            {"x": 0.82, "y": 0.75}
        ]
    }
}
)QML";

void settle() {
  for (int index = 0; index < 40; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QGuiApplication app(argc, argv);

  QQmlApplicationEngine engine;
  QQmlComponent component(&engine);
  component.setData(
      QByteArray(kProjectionHostQml),
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR
                                         "/tests/timeline_scene_projection_effect_test.qml")));
  std::unique_ptr<QObject> object(component.create());
  if (object == nullptr) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("Scene projection effect should instantiate with the qsb shader resources.");
  }
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (window == nullptr)
    return fail("Scene projection fixture should create a native QQuickWindow.");
  window->show();
  settle();
  if (window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
    std::cout << "Scene projection pixels require a GPU-backed Qt Quick renderer; software QPA skips ShaderEffect rendering\n";
    return 77;
  }
  const QImage frame = window->grabWindow();
  const auto screenshot_path = qEnvironmentVariable("PHOTO_EDITOR_SCENE_PROJECTION_SCREENSHOT");
  if (!screenshot_path.isEmpty())
    frame.save(screenshot_path);
  if (frame.isNull() || frame.width() < 320 || frame.height() < 240
          || std::abs(static_cast<double>(frame.width()) / frame.height() - 4.0 / 3.0) > 0.01)
  {
    std::cerr << "unexpected projection frame dimensions: " << frame.width() << 'x'
              << frame.height() << '\n';
    return fail("Scene projection fixture should render a complete 4:3 frame.");
  }

  const double pixel_scale_x = static_cast<double>(frame.width()) / window->width();
  const double pixel_scale_y = static_cast<double>(frame.height()) / window->height();
  const auto pixel_at_logical = [&](int x, int y) {
    return frame.pixelColor(static_cast<int>(std::lround(x * pixel_scale_x)),
                            static_cast<int>(std::lround(y * pixel_scale_y)));
  };

  const QColor background = pixel_at_logical(12, 12);
  const QColor insideQuad = pixel_at_logical(160, 120);
  const QColor outsideQuadBounds = pixel_at_logical(66, 50);
  if (insideQuad == background || insideQuad == QColor(QStringLiteral("#102030")))
  {
    std::cerr << "projection pixel readback failed: frame=" << frame.width() << 'x' << frame.height()
              << " background=" << background.name().toStdString()
              << " inside=" << insideQuad.name().toStdString() << '\n';
    return fail("The projective homography should render source texture inside the reconstructed quad.");
  }
  if (outsideQuadBounds != QColor(QStringLiteral("#102030")))
    return fail("The projective homography should clip texture outside the tilted scene plane quad.");

  auto* fade_effect = window->findChild<QObject*>(QStringLiteral("timelineSceneProjectionTestEffect"));
  if (!fade_effect || !fade_effect->setProperty("fadeColor", QColor(Qt::white)) ||
      !fade_effect->setProperty("fadeAmount", 0.5))
    return fail("The Program geometry shader must accept color-fade state");
  settle();
  const auto fade_frame = window->grabWindow();
  const auto faded = fade_frame.pixelColor(static_cast<int>(160 * pixel_scale_x),
                                          static_cast<int>(120 * pixel_scale_y));
  if (std::abs(faded.red() - (insideQuad.red() + 255) / 2) > 6 ||
      std::abs(faded.green() - (insideQuad.green() + 255) / 2) > 6 ||
      std::abs(faded.blue() - (insideQuad.blue() + 255) / 2) > 6 ||
      fade_frame.pixelColor(static_cast<int>(12 * pixel_scale_x),
                            static_cast<int>(12 * pixel_scale_y)) != background)
    return fail("A white fade must mix the picture while preserving transparent geometry outside it");
  fade_effect->setProperty("fadeAmount", 0.0);
  settle();

  QTemporaryDir mask_directory;
  QImage mask(320, 240, QImage::Format_ARGB32);
  mask.fill(Qt::transparent);
  for (int y = 0; y < mask.height(); ++y) for (int x = 0; x < mask.width() / 2; ++x)
    mask.setPixel(x, y, qRgba(255, 255, 255, 255));
  const auto mask_path = mask_directory.filePath(QStringLiteral("mask.png"));
  if (!mask.save(mask_path)) return fail("Could not create occlusion fixture");
  auto* effect = window->findChild<QObject*>(QStringLiteral("timelineSceneProjectionTestEffect"));
  if (!effect || !effect->setProperty("occlusionFrame", QVariantMap{
      {QStringLiteral("enabled"), true}, {QStringLiteral("ready"), true},
      {QStringLiteral("source"), QUrl::fromLocalFile(mask_path).toString()},
      {QStringLiteral("rect"), QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0},
          {QStringLiteral("width"), 1.0}, {QStringLiteral("height"), 1.0}}}})) return fail("Could not set occlusion frame");
  settle();
  const auto occluded_frame = window->grabWindow();
  const auto read = [&](int x, int y) { return occluded_frame.pixelColor(
      static_cast<int>(std::lround(x * pixel_scale_x)), static_cast<int>(std::lround(y * pixel_scale_y))); };
  if (read(120, 120) != QColor(QStringLiteral("#102030")) || read(200, 120) == QColor(QStringLiteral("#102030")))
    return fail("Foreground occlusion must use scene-image coordinates, preserve the visible half, and hide the covered half");
  if (!screenshot_path.isEmpty()) occluded_frame.save(screenshot_path + QStringLiteral(".occlusion.png"));
  auto letterboxed_mask = effect->property("occlusionFrame").toMap();
  letterboxed_mask.insert(QStringLiteral("rect"), QVariantMap{
      {QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.25},
      {QStringLiteral("width"), 1.0}, {QStringLiteral("height"), 0.5}});
  effect->setProperty("occlusionFrame", letterboxed_mask);
  settle();
  const auto letterboxed = window->grabWindow();
  const auto letterbox_pixel = [&](int x, int y) { return letterboxed.pixelColor(
      static_cast<int>(std::lround(x * pixel_scale_x)), static_cast<int>(std::lround(y * pixel_scale_y))); };
  if (letterbox_pixel(120, 55) == QColor(QStringLiteral("#102030")) ||
      letterbox_pixel(120, 120) != QColor(QStringLiteral("#102030")))
    return fail("Fit framing must restrict foreground occlusion to the source image rectangle");
  effect->setProperty("occlusionFrame", QVariantMap{{QStringLiteral("enabled"), true}, {QStringLiteral("ready"), false}});
  settle();
  const auto unavailable = window->grabWindow();
  if (unavailable.pixelColor(static_cast<int>(std::lround(200 * pixel_scale_x)), static_cast<int>(std::lround(120 * pixel_scale_y))) != QColor(QStringLiteral("#102030")))
    return fail("An unavailable requested occlusion matte must not display an unmasked layer");

  std::cout << "Scene projection shader rendered perspective, scene-space foreground occlusion, and fail-closed missing-mask behavior\n";
  return 0;
}
