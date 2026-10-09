#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QList>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRect>
#include <QThread>
#include <QUrl>
#include <QVariant>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool wait_for_component(QQmlComponent* component) {
  QElapsedTimer timeout;
  timeout.start();
  while (component->status() == QQmlComponent::Loading && timeout.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  if (component->status() == QQmlComponent::Ready) return true;
  for (const auto& error : component->errors())
    std::cerr << error.toString().toStdString() << '\n';
  return false;
}

void settle(QQuickWindow& window, int milliseconds = 100) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < milliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    window.update();
    QThread::msleep(2);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

QQuickItem* find_visual_item(QQuickItem* parent, const QString& object_name) {
  if (parent == nullptr) return nullptr;
  if (parent->objectName() == object_name) return parent;
  for (auto* child : parent->childItems()) {
    if (auto* found = find_visual_item(child, object_name)) return found;
  }
  return nullptr;
}

void collect_handle_items(QQuickItem* parent, QList<QQuickItem*>* handles) {
  if (parent == nullptr) return;
  if (parent->objectName().startsWith(QStringLiteral("cropVisualHandle_")))
    handles->push_back(parent);
  for (auto* child : parent->childItems()) collect_handle_items(child, handles);
}

struct ExpectedRect final {
  const char* name;
  qreal x;
  qreal y;
  qreal width;
  qreal height;
};

constexpr std::array<const char*, 8> kHandleNames{
    "topLeft", "topRight", "bottomLeft", "bottomRight",
    "top", "bottom", "left", "right"};

constexpr std::array<ExpectedRect, 8> kInitialGeometry{{
    {"topLeft", 163, 133, 14, 14},
    {"topRight", 513, 133, 14, 14},
    {"bottomLeft", 163, 333, 14, 14},
    {"bottomRight", 513, 333, 14, 14},
    {"top", 326, 138, 38, 4},
    {"bottom", 326, 338, 38, 4},
    {"left", 168, 221, 4, 38},
    {"right", 518, 221, 4, 38},
}};

constexpr std::array<ExpectedRect, 8> kUpdatedGeometry{{
    {"topLeft", 229, 112, 14, 14},
    {"topRight", 517, 112, 14, 14},
    {"bottomLeft", 229, 342, 14, 14},
    {"bottomRight", 517, 342, 14, 14},
    {"top", 361, 117, 38, 4},
    {"bottom", 361, 347, 38, 4},
    {"left", 234, 215, 4, 38},
    {"right", 522, 215, 4, 38},
}};

bool near(qreal actual, qreal expected) {
  return std::abs(actual - expected) <= 1.0e-6;
}

bool check_crop_handles(
    QQuickItem* fixture,
    QQuickItem* overlay,
    const std::array<ExpectedRect, 8>& expected,
    const std::array<QQuickItem*, 8>* expected_identity,
    std::array<QQuickItem*, 8>* captured_identity,
    const char* phase) {
  QList<QQuickItem*> handles;
  collect_handle_items(fixture, &handles);
  if (handles.size() != static_cast<qsizetype>(kHandleNames.size())) {
    std::cerr << phase << ": expected eight visual handles, got " << handles.size() << '\n';
    return false;
  }
  if (overlay == nullptr || !overlay->isVisible()) {
    std::cerr << phase << ": crop overlay should be visible\n";
    return false;
  }

  for (std::size_t i = 0; i < kHandleNames.size(); ++i) {
    const auto object_name = QStringLiteral("cropVisualHandle_") +
                             QString::fromLatin1(kHandleNames[i]);
    auto* item = find_visual_item(fixture, object_name);
    if (item == nullptr || !item->isVisible()) {
      std::cerr << phase << ": missing or hidden " << object_name.toStdString() << '\n';
      return false;
    }
    if (expected_identity != nullptr && (*expected_identity)[i] != item) {
      std::cerr << phase << ": stable handle name lost its delegate identity: "
                << object_name.toStdString() << '\n';
      return false;
    }
    if (captured_identity != nullptr) (*captured_identity)[i] = item;
    const auto& rect = expected[i];
    if (!near(item->x(), rect.x) || !near(item->y(), rect.y) ||
        !near(item->width(), rect.width) || !near(item->height(), rect.height)) {
      std::cerr << phase << ": unexpected rect for " << object_name.toStdString()
                << " actual=(" << item->x() << ',' << item->y() << ','
                << item->width() << ',' << item->height() << ") expected=("
                << rect.x << ',' << rect.y << ',' << rect.width << ','
                << rect.height << ")\n";
      return false;
    }
  }
  return true;
}

bool no_crop_handles(QQuickItem* fixture) {
  QList<QQuickItem*> handles;
  collect_handle_items(fixture, &handles);
  return handles.isEmpty();
}

bool set_property(QObject* object, const char* name, const QVariant& value) {
  if (object->setProperty(name, value)) return true;
  std::cerr << "could not set QML property " << name << '\n';
  return false;
}

QString components_directory() {
  return QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
      .filePath(QStringLiteral("apps/desktop/qml/components"));
}

bool instrument_production_wave_source(const QString& path, QByteArray* source) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    std::cerr << "could not read production AdditiveLightWaves.qml\n";
    return false;
  }
  *source = file.readAll();
  const std::array<std::pair<QByteArray, QByteArray>, 4> edits{{
      {QByteArrayLiteral("import QtQuick\n"),
       QByteArrayLiteral("import QtQuick\nimport \".\"\n")},
      {QByteArrayLiteral("    property real phase: 0\n"),
       QByteArrayLiteral("    property real phase: 0\n    property int probePaintedCount: 0\n")},
      {QByteArrayLiteral("        id: lightTexture\n"),
       QByteArrayLiteral("        id: lightTexture\n        objectName: \"probeProductionWaveCanvas\"\n")},
      {QByteArrayLiteral("        onPaint: {\n"),
       QByteArrayLiteral("        onPainted: root.probePaintedCount += 1\n        onPaint: {\n")},
  }};
  for (const auto& [before, after] : edits) {
    if (source->count(before) != 1) {
      std::cerr << "production wave instrumentation anchor should occur once: "
                << before.constData() << '\n';
      return false;
    }
    source->replace(before, after);
  }
  return true;
}

int paint_count(const QObject* wave) {
  return wave == nullptr ? -1 : wave->property("probePaintedCount").toInt();
}

bool wait_for_paint_count(QQuickWindow& window, const QObject* wave, int minimum) {
  QElapsedTimer timeout;
  timeout.start();
  while (paint_count(wave) < minimum && timeout.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    window.update();
    QThread::msleep(2);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  return paint_count(wave) >= minimum;
}

bool same_wave_pixels(const QImage& frame, qreal dpr, int width, int height) {
  const QRect baseline(0, qRound(700 * dpr), qRound(width * dpr), qRound(height * dpr));
  const QRect production(qRound(500 * dpr), qRound(700 * dpr),
                         qRound(width * dpr), qRound(height * dpr));
  if (!frame.rect().contains(baseline) || !frame.rect().contains(production)) return false;
  return frame.copy(baseline) == frame.copy(production);
}

constexpr auto kOriginalWaveSource = R"QML(import QtQuick

// Frozen original visible waveform drawing used as the pixel oracle.
Item {
    id: root
    property bool active: true
    property bool reducedMotion: false
    property real phase: 0
    property int probePaintedCount: 0
    readonly property bool motionEnabled: active && visible && !reducedMotion && !theme.reducedMotion
    implicitWidth: 360
    implicitHeight: 80
    clip: true
    Accessible.ignored: true

    QtObject {
        id: theme
        readonly property bool reducedMotion: false
    }

    Canvas {
        id: lightTexture
        width: root.width * 2
        height: root.height
        x: -root.phase * root.width
        onPainted: root.probePaintedCount += 1
        onPaint: {
            const c = getContext("2d")
            c.reset()
            if (root.width <= 0 || height <= 0) return
            const colors = ["#ff344f", "#25ec83", "#398cff"]
            const spread = [-1, 0, 1]
            c.globalCompositeOperation = "lighter"
            for (let channel = 0; channel < 3; ++channel) {
                c.strokeStyle = colors[channel]
                c.shadowColor = colors[channel]
                c.shadowBlur = Math.min(16, height * 0.16)
                for (let halo = 0; halo < 2; ++halo) {
                    c.globalAlpha = halo === 0 ? 0.13 : 0.78
                    c.lineWidth = halo === 0 ? Math.max(3, height * 0.085) : Math.max(1, height * 0.017)
                    c.beginPath()
                    for (let step = 0; step <= 180; ++step) {
                        const t = step / 180 * 2
                        const y = height / 2 + height * (0.09 * Math.sin(t * Math.PI * 4)
                                  + spread[channel] * 0.25 * Math.sin(t * Math.PI * 2))
                        if (step === 0) c.moveTo(t * root.width, y)
                        else c.lineTo(t * root.width, y)
                    }
                    c.stroke()
                }
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: { if (visible) requestPaint() }
    }

    NumberAnimation on phase {
        running: root.motionEnabled
        from: 0
        to: 1
        duration: 5600
        loops: Animation.Infinite
    }
}
)QML";

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQuickWindow window;
  window.resize(1000, 800);

  const auto components_dir = components_directory();
  const auto components_import = QUrl::fromLocalFile(components_dir).toString();
  QByteArray crop_qml = QByteArrayLiteral("import QtQuick\nimport \"") +
      components_import.toUtf8() + QByteArrayLiteral("\"\n") + QByteArrayLiteral(R"QML(
Item {
    id: fixture
    width: 1000
    height: 800
    property bool cropActive: false
    property real testImageX: 30
    property real testImageY: 40
    property real testImageWidth: 700
    property real testImageHeight: 500
    property real testCropX: 0.2
    property real testCropY: 0.2
    property real testCropWidth: 0.5
    property real testCropHeight: 0.4

    CropOverlay {
        id: cropOverlay
        objectName: "cropOverlay"
        anchors.fill: parent
        active: fixture.cropActive
        imageX: fixture.testImageX
        imageY: fixture.testImageY
        imageWidth: fixture.testImageWidth
        imageHeight: fixture.testImageHeight
        cropX: fixture.testCropX
        cropY: fixture.testCropY
        cropWidth: fixture.testCropWidth
        cropHeight: fixture.testCropHeight
    }

    readonly property string topLeftHitMode: cropOverlay.testHitMode(
        cropOverlay.cropScreenRect.x, cropOverlay.cropScreenRect.y)
    readonly property string topEdgeHitMode: cropOverlay.testHitMode(
        cropOverlay.cropScreenRect.x + cropOverlay.cropScreenRect.width / 2,
        cropOverlay.cropScreenRect.y)
}
)QML");
  QQmlComponent crop_component(&engine);
  crop_component.setData(
      crop_qml,
      QUrl::fromLocalFile(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                              .filePath(QStringLiteral("tests/crop-loading-lifecycle-fixture.qml"))));
  if (!wait_for_component(&crop_component)) return fail("production CropOverlay fixture should load");
  std::unique_ptr<QObject> crop_fixture(crop_component.create());
  auto* crop_root = qobject_cast<QQuickItem*>(crop_fixture.get());
  if (crop_root == nullptr) return fail("CropOverlay fixture root should be a visual item");
  auto* crop_overlay = find_visual_item(crop_root, QStringLiteral("cropOverlay"));
  if (crop_overlay == nullptr) return fail("fixture should contain the production CropOverlay");
  crop_root->setParentItem(window.contentItem());

  const auto production_wave_path =
      QDir(components_dir).filePath(QStringLiteral("AdditiveLightWaves.qml"));
  QByteArray production_wave_source;
  if (!instrument_production_wave_source(production_wave_path, &production_wave_source))
    return fail("production AdditiveLightWaves should expose test-only paint observation anchors");
  QQmlComponent production_wave_component(&engine);
  production_wave_component.setData(
      production_wave_source, QUrl::fromLocalFile(production_wave_path));
  if (!wait_for_component(&production_wave_component))
    return fail("instrumented production AdditiveLightWaves should load");
  std::unique_ptr<QObject> production_wave(production_wave_component.create());
  auto* production_wave_item = qobject_cast<QQuickItem*>(production_wave.get());
  if (production_wave_item == nullptr)
    return fail("production AdditiveLightWaves should create a visual item");

  QQmlComponent original_wave_component(&engine);
  original_wave_component.setData(
      QByteArray(kOriginalWaveSource),
      QUrl::fromLocalFile(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                              .filePath(QStringLiteral("tests/fixtures/AdditiveLightWavesOriginal.qml"))));
  if (!wait_for_component(&original_wave_component))
    return fail("frozen original AdditiveLightWaves pixel oracle should load");
  std::unique_ptr<QObject> original_wave(original_wave_component.create());
  auto* original_wave_item = qobject_cast<QQuickItem*>(original_wave.get());
  if (original_wave_item == nullptr)
    return fail("original waveform oracle should create a visual item");

  for (auto* wave : {production_wave.get(), original_wave.get()}) {
    if (!set_property(wave, "active", false) ||
        !set_property(wave, "reducedMotion", true) ||
        !set_property(wave, "phase", 0.37) ||
        !set_property(wave, "width", 360.0) ||
        !set_property(wave, "height", 80.0)) {
      return fail("wave components should accept deterministic phase and geometry");
    }
  }
  production_wave_item->setX(500);
  production_wave_item->setY(700);
  original_wave_item->setX(0);
  original_wave_item->setY(700);
  production_wave_item->setParentItem(crop_root);
  original_wave_item->setParentItem(crop_root);

  window.show();
  settle(window, 100);

  if (!no_crop_handles(crop_root))
    return fail("inactive production CropOverlay should have no visual handle delegates");
  if (!set_property(crop_root, "cropActive", true))
    return fail("CropOverlay should activate");
  settle(window);
  std::array<QQuickItem*, 8> initial_identity{};
  if (!check_crop_handles(crop_root, crop_overlay, kInitialGeometry, nullptr,
                          &initial_identity, "initial active crop")) {
    return EXIT_FAILURE;
  }
  if (crop_root->property("topLeftHitMode").toString() != QStringLiteral("topLeft") ||
      crop_root->property("topEdgeHitMode").toString() != QStringLiteral("top")) {
    return fail("corner and edge hit modes should remain unchanged");
  }

  const std::array<std::pair<const char*, QVariant>, 8> geometry_updates{{
      {"testImageX", 20.0}, {"testImageY", 50.0},
      {"testImageWidth", 720.0}, {"testImageHeight", 460.0},
      {"testCropX", 0.3}, {"testCropY", 0.15},
      {"testCropWidth", 0.4}, {"testCropHeight", 0.5},
  }};
  for (const auto& [name, value] : geometry_updates) {
    if (!set_property(crop_root, name, value)) return EXIT_FAILURE;
  }
  settle(window);
  if (!check_crop_handles(crop_root, crop_overlay, kUpdatedGeometry,
                          &initial_identity, nullptr, "updated active crop")) {
    return EXIT_FAILURE;
  }
  if (crop_root->property("topLeftHitMode").toString() != QStringLiteral("topLeft") ||
      crop_root->property("topEdgeHitMode").toString() != QStringLiteral("top")) {
    return fail("corner and edge hit modes should remain stable after crop geometry changes");
  }
  if (!set_property(crop_root, "cropActive", false)) return EXIT_FAILURE;
  settle(window);
  if (!no_crop_handles(crop_root))
    return fail("hidden production CropOverlay should destroy all visual handle delegates");
  if (!set_property(crop_root, "cropActive", true)) return EXIT_FAILURE;
  settle(window);
  if (!check_crop_handles(crop_root, crop_overlay, kUpdatedGeometry, nullptr,
                          nullptr, "reopened active crop")) {
    return EXIT_FAILURE;
  }

  if (paint_count(production_wave.get()) < 1 || paint_count(original_wave.get()) < 1)
    return fail("visible production and original waves should complete a Canvas paint");
  auto frame = window.grabWindow();
  const qreal dpr = frame.devicePixelRatio();
  if (!same_wave_pixels(frame, dpr, 360, 80))
    return fail("production AdditiveLightWaves should preserve exact original visible pixels");

  production_wave_item->setVisible(false);
  settle(window, 30);
  const int hidden_paint_count = paint_count(production_wave.get());
  original_wave_item->setWidth(300);
  original_wave_item->setHeight(70);
  production_wave_item->setWidth(300);
  production_wave_item->setHeight(70);
  settle(window, 150);
  if (paint_count(production_wave.get()) != hidden_paint_count)
    return fail("hidden production AdditiveLightWaves should skip Canvas paints while resized");

  production_wave_item->setVisible(true);
  if (!wait_for_paint_count(window, production_wave.get(), hidden_paint_count + 1))
    return fail("production waves should redraw after becoming visible");
  frame = window.grabWindow();
  if (!same_wave_pixels(frame, dpr, 300, 70))
    return fail("shown waves should match original pixels after hidden resize");

  std::cout << "CropOverlay and AdditiveLightWaves lifecycle contracts passed: "
               "8 stable handles, exact geometry, hidden delegates, exact wave pixels, "
               "and hidden resize/show repaint behavior\n";
  return EXIT_SUCCESS;
}
