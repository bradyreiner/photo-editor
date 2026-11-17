#include <QGuiApplication>
#include <QColor>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSizeF>
#include <QThread>
#include <QTemporaryDir>
#include <QUrl>
#include <QVector4D>
#include <QVariantMap>

#include <iostream>
#include <cmath>
#include <memory>

namespace {
const char kProjectionHostQml[] = R"QML(
import QtQuick
import QtQuick.Window
import "../apps/desktop/qml/components" as Components

Window {
    id: root
    width: 320
    height: 240
    visible: true
    color: "#102030"
    property bool usePerspectiveEffect: false
    property bool showAlphaReference: false

    Item {
        id: sourceFrame
        objectName: "sceneProjectionSourceFrame"
        width: 320
        height: 240
        property bool partialAlphaMode: false
        Rectangle {
            anchors.fill: parent
            color: "#f020d0"
            visible: !sourceFrame.partialAlphaMode
        }
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            width: parent.width / 2
            height: parent.height / 2
            color: "#20e060"
            visible: !sourceFrame.partialAlphaMode
        }
        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: parent.width / 2
            height: parent.height / 2
            color: "#2070f0"
            visible: !sourceFrame.partialAlphaMode
        }
        Rectangle {
            x: 40
            y: 150
            width: 80
            height: 60
            color: "#f020d0"
            opacity: 0.5
            visible: sourceFrame.partialAlphaMode
        }
    }

    Rectangle {
        objectName: "timelineMaskAlphaReference"
        x: 40
        y: 150
        width: 80
        height: 60
        color: "#f020d0"
        opacity: 0.5
        visible: root.showAlphaReference
    }

    Components.TimelineSceneProjectionEffect {
        objectName: "timelineSceneProjectionTestEffect"
        anchors.fill: parent
        visible: !root.showAlphaReference && !root.usePerspectiveEffect
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

    Components.TimelinePerspectiveEffect {
        objectName: "timelinePerspectiveTestEffect"
        anchors.fill: parent
        visible: !root.showAlphaReference && root.usePerspectiveEffect
        source: ShaderEffectSource {
            sourceItem: sourceFrame
            hideSource: true
            live: true
        }
        sceneQuad: [
            {"x": 0.0, "y": 0.0},
            {"x": 1.0, "y": 0.0},
            {"x": 0.0, "y": 1.0},
            {"x": 1.0, "y": 1.0}
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

const char* graphics_api_name(QSGRendererInterface::GraphicsApi api) {
  switch (api) {
    case QSGRendererInterface::Unknown: return "Unknown";
    case QSGRendererInterface::Software: return "Software";
    case QSGRendererInterface::OpenVG: return "OpenVG";
    case QSGRendererInterface::OpenGL: return "OpenGL";
    case QSGRendererInterface::Direct3D11: return "Direct3D11";
    case QSGRendererInterface::Vulkan: return "Vulkan";
    case QSGRendererInterface::Metal: return "Metal";
    case QSGRendererInterface::Null: return "Null";
    case QSGRendererInterface::Direct3D12: return "Direct3D12";
  }
  return "Unrecognized";
}

bool close_color(const QColor& actual, const QColor& expected, int tolerance = 3) {
  return std::abs(actual.red() - expected.red()) <= tolerance &&
      std::abs(actual.green() - expected.green()) <= tolerance &&
      std::abs(actual.blue() - expected.blue()) <= tolerance &&
      std::abs(actual.alpha() - expected.alpha()) <= tolerance;
}

QVariantList full_frame_quad() {
  return {
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 1.0}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  };
}

bool verify_masked_premultiplied_coverage(
    QQuickWindow* window, QObject* root, QObject* effect, QObject* source_frame,
    const QColor& background, double pixel_scale_x, double pixel_scale_y,
    const QString& effect_name) {
  const auto set = [&](const char* property, const QVariant& value) {
    return effect->setProperty(property, value);
  };
  const auto capture = [&]() { settle(); return window->grabWindow(); };
  const auto pixel = [&](const QImage& image, int x, int y) {
    return image.pixelColor(static_cast<int>(std::lround(x * pixel_scale_x)),
                            static_cast<int>(std::lround(y * pixel_scale_y)));
  };
  const auto set_mask = [&](bool enabled, const QVector4D& rect, double radius) {
    return set("clipMaskEnabled", enabled ? 1.0 : 0.0) &&
        set("clipMaskShape", 0.0) &&
        set("clipMaskRect", QVariant::fromValue(rect)) &&
        set("clipMaskCornerRadius", radius) &&
        set("clipMaskCanvasSize", QVariant::fromValue(QSizeF(320.0, 240.0))) &&
        set("clipMaskFrameCenter", QVariant::fromValue(QPointF(160.0, 120.0))) &&
        set("clipMaskFrameScale", 1.0) &&
        set("clipMaskFrameRotationDegrees", 0.0);
  };
  if (!effect || !source_frame ||
      !root->setProperty("showAlphaReference", false) ||
      !set("sceneQuad", full_frame_quad()) ||
      !set("fadeAmount", 0.0) ||
      !set("frameCornerRadius", 0.0) ||
      !set_mask(false, QVector4D(0, 0, 1, 1), 0.0) ||
      !source_frame->setProperty("partialAlphaMode", false) ||
      !source_frame->setProperty("visible", true) ||
      !source_frame->setProperty("opacity", 1.0)) {
    std::cerr << effect_name.toStdString() << ": failed setting baseline properties\n";
    return false;
  }

  const QImage baseline = capture();
  const QColor opaque_interior = pixel(baseline, 50, 180);
  const QColor opaque_center = pixel(baseline, 160, 120);
  if (baseline.isNull() || close_color(opaque_interior, background)) {
    std::cerr << effect_name.toStdString() << ": opaque fixture interior is not visible\n";
    return false;
  }

  // Rounded frame corners must remove the entire premultiplied source sample,
  // while leaving fully covered interior pixels identical to the unmasked pass.
  if (!set("frameCornerRadius", 0.30)) return false;
  const QImage rounded_frame = capture();
  if (!close_color(pixel(rounded_frame, 2, 2), background) ||
      !close_color(pixel(rounded_frame, 50, 180), opaque_interior)) {
    std::cerr << effect_name.toStdString()
              << ": rounded frame coverage leaked at a transparent corner or changed the interior\n";
    return false;
  }

  // A rectangular sequence-space mask clips an opaque source region to the
  // nonblack backdrop. A zero-area mask is fully transparent by definition.
  if (!set("frameCornerRadius", 0.0) ||
      !set_mask(true, QVector4D(0.30F, 0.20F, 0.70F, 0.80F), 0.0))
    return false;
  const QImage rectangular_mask = capture();
  if (!close_color(pixel(rectangular_mask, 50, 120), background) ||
      close_color(pixel(rectangular_mask, 160, 120), background)) {
    std::cerr << effect_name.toStdString() << ": rectangle mask did not clip at its sequence-space bounds\n";
    return false;
  }

  if (!set_mask(true, QVector4D(0.50F, 0.20F, 0.50F, 0.80F), 0.0))
    return false;
  const QImage collapsed_mask = capture();
  if (!close_color(pixel(collapsed_mask, 50, 120), background) ||
      !close_color(pixel(collapsed_mask, 160, 120), background)) {
    std::cerr << effect_name.toStdString() << ": collapsed mask must clear the full source without RGB leakage\n";
    return false;
  }

  // Rounded sequence masks include both a fully outside corner and an interior
  // sample, which catches alpha-only edge coverage on premultiplied sources.
  if (!set_mask(true, QVector4D(0.25F, 0.25F, 0.75F, 0.75F), 0.25))
    return false;
  const QImage rounded_mask = capture();
  const QColor rounded_corner = pixel(rounded_mask, 81, 61);
  const QColor rounded_inside = pixel(rounded_mask, 100, 80);
  const QColor rounded_center = pixel(rounded_mask, 160, 120);
  if (!close_color(rounded_corner, background) ||
      close_color(rounded_inside, background) ||
      !close_color(rounded_center, opaque_center)) {
    std::cerr << effect_name.toStdString()
              << ": rounded sequence mask should clip the corner and preserve its interior; corner="
              << rounded_corner.name().toStdString() << " inside="
              << rounded_inside.name().toStdString() << " center="
              << rounded_center.name().toStdString() << " expected-center="
              << opaque_center.name().toStdString() << " background="
              << background.name().toStdString() << '\n';
    return false;
  }

  // Compare the shader output against an ordinary Qt Quick half-alpha rectangle
  // over the same backdrop, avoiding assumptions about GPU sRGB blend space.
  if (!root->setProperty("showAlphaReference", true) ||
      !source_frame->setProperty("visible", false))
    return false;
  const QImage alpha_reference = capture();
  const QColor expected_partial = pixel(alpha_reference, 50, 180);
  if (!root->setProperty("showAlphaReference", false) ||
      !source_frame->setProperty("visible", true) ||
      !source_frame->setProperty("partialAlphaMode", true) ||
      !set_mask(false, QVector4D(0, 0, 1, 1), 0.0))
    return false;
  const QImage partial_alpha = capture();
  const QColor partial_pixel = pixel(partial_alpha, 50, 180);
  if (!close_color(partial_pixel, expected_partial, 10) ||
      close_color(partial_pixel, opaque_interior) || close_color(partial_pixel, background) ||
      close_color(expected_partial, background)) {
    std::cerr << effect_name.toStdString()
              << ": partially transparent source should blend over the nonblack backdrop; partial="
              << partial_pixel.name().toStdString() << " expected="
              << expected_partial.name().toStdString() << " opaque="
              << opaque_interior.name().toStdString() << " backdrop="
              << background.name().toStdString() << '\n';
    return false;
  }
  if (!set_mask(true, QVector4D(0.50F, 0.20F, 0.50F, 0.80F), 0.0))
    return false;
  const QImage partial_alpha_collapsed = capture();
  if (!close_color(pixel(partial_alpha_collapsed, 50, 120), background)) {
    std::cerr << effect_name.toStdString()
              << ": partial-alpha source leaked RGB through a collapsed mask\n";
    return false;
  }
  if (!source_frame->setProperty("partialAlphaMode", false) ||
      !set_mask(false, QVector4D(0, 0, 1, 1), 0.0) ||
      !set("frameCornerRadius", 0.0))
    return false;
  return true;
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

  // Exercise the exact production components used by Program preview for both
  // scene projection and ordinary perspective/photo clips.
  auto* root = object.get();
  auto* source_frame = window->findChild<QObject*>(QStringLiteral("sceneProjectionSourceFrame"));
  auto* scene_effect = window->findChild<QObject*>(QStringLiteral("timelineSceneProjectionTestEffect"));
  auto* perspective_effect = window->findChild<QObject*>(QStringLiteral("timelinePerspectiveTestEffect"));
  if (!root || !source_frame || !scene_effect || !perspective_effect)
    return fail("Both production timeline shader effects should be present for mask-pixel coverage");
  if (!scene_effect->setProperty("occlusionFrame", QVariantMap{
          {QStringLiteral("enabled"), false}, {QStringLiteral("ready"), false}}))
    return fail("Could not clear the earlier occlusion fixture before mask-pixel assertions");
  if (!root->setProperty("usePerspectiveEffect", false) ||
      !verify_masked_premultiplied_coverage(window, root, scene_effect, source_frame,
          QColor(QStringLiteral("#102030")), pixel_scale_x, pixel_scale_y,
          QStringLiteral("scene projection")))
    return fail("Scene projection must attenuate premultiplied RGBA for corner and sequence masks");
  if (!root->setProperty("usePerspectiveEffect", true))
    return fail("Could not activate the production perspective effect");
  settle();
  if (!verify_masked_premultiplied_coverage(window, root, perspective_effect, source_frame,
          QColor(QStringLiteral("#102030")), pixel_scale_x, pixel_scale_y,
          QStringLiteral("perspective")))
    return fail("Perspective rendering must attenuate premultiplied RGBA for corner and sequence masks");

  std::cout << "backend=" << graphics_api_name(window->rendererInterface()->graphicsApi())
            << "; scene projection and perspective rendered premultiplied RGBA mask coverage"
            << " (opaque, half-alpha, collapsed, rounded, interior)\n";
  return 0;
}
