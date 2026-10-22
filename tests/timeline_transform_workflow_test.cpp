#include "timeline_document_controller.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <iostream>
#include <limits>

namespace photo_editor::desktop {
struct TimelineHistoryTestAccess {
  static QStringList render(TimelineDocumentController& document, const QString& output,
                            QString* error, double start = -1.0, double end = -1.0) {
    return document.timelineRenderArguments(output, error, false, QStringLiteral("master"),
                                            {}, {}, 0, start, end);
  }
};
}
using namespace photo_editor::desktop;
namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
bool near(double a, double b, double tolerance = 0.005) { return std::abs(a - b) < tolerance; }
QQuickItem* find_item(QQuickItem* root, const QString& name) {
  if (root->objectName() == name) return root;
  for (auto* child : root->childItems())
    if (auto* found = find_item(child, name)) return found;
  return nullptr;
}
bool run(const QStringList& args) {
  QProcess process;
  process.start(QStandardPaths::findExecutable("ffmpeg"), args);
  if (!process.waitForFinished(30000) || process.exitCode() != 0) {
    std::cerr << process.readAllStandardError().toStdString();
    return false;
  }
  return true;
}
QColor sample(const QString& path, double seconds) {
  QProcess process;
  process.start(QStandardPaths::findExecutable("ffmpeg"),
      {"-v", "error", "-ss", QString::number(seconds, 'f', 6), "-i", path,
       "-frames:v", "1", "-f", "image2pipe", "-vcodec", "png", "pipe:1"});
  if (!process.waitForFinished(10000) || process.exitCode() != 0) return {};
  const auto image = QImage::fromData(process.readAllStandardOutput(), "PNG");
  return image.isNull() ? QColor{} : image.pixelColor(image.width() / 2, image.height() / 2);
}
}
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir temp;
  if (!temp.isValid()) return fail("The transform fixture needs an isolated directory");
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", temp.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", temp.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/transform_fixture");
  const auto source = temp.filePath("source.png");
  QImage image(640, 360, QImage::Format_RGB32);
  image.fill(QColor(220, 30, 20));
  if (!image.save(source)) return fail("The transform fixture needs real pixels");
  TimelineDocumentController document;
  document.createTimelineWithSettings({{"timelineName", "Transform"}, {"width", 160},
                                      {"height", 90}, {"frameRate", 30.0}});
  document.addClipFromGallery("photo:transform", source, "Picture", 2.0, {});
  document.selectClip(0);

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("document", &document);
  const auto qml = QByteArray(R"QML(
import QtQuick
import QtQuick.Window
import "../apps/desktop/qml/components" as Components
Window {
    width: 800; height: 520; visible: true; color: "#202328"
    Components.Theme { id: theme }
    Components.TimelineTransformOverlay {
        id: overlay
        objectName: "transformOverlay"
        anchors.fill: parent
        theme: theme
        active: true
        sourceAspect: 16 / 9
        frameScale: document.currentClip.frameScale
        frameRotation: document.currentClip.frameRotationDegrees
        offsetX: document.currentClip.frameOffsetX
        offsetY: document.currentClip.frameOffsetY
        onInteractionStarted: document.beginContinuousEdit()
        onTransformEdited: (x, y, scale, rotation) => document.setSelectedClipTransform(x, y, scale, rotation)
        onInteractionFinished: document.endContinuousEdit()
        onInteractionCancelled: { document.endContinuousEdit(); document.undo() }
    }
}
)QML");
  engine.loadData(qml, QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/transform_host.qml")));
  if (engine.rootObjects().isEmpty()) return fail("The real transform overlay must load");
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  if (!window) return fail("The transform fixture needs a window");
  QTest::qWait(100);
  const auto drag = [&](const QString& name, QPoint delta, bool cancel = false) {
    QTest::qWait(30);
    auto* item = find_item(window->contentItem(), name);
    if (!item) return false;
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
    QTest::mouseMove(window, point + delta, 20);
    QTest::qWait(25);
    if (cancel) QTest::keyClick(window, Qt::Key_Escape);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point + delta);
    QTest::qWait(25);
    return true;
  };
  if (!drag("timelineProgramTransformMoveArea", {40, 20}) ||
      !near(document.currentClip().value("frameOffsetX").toDouble(), 0.2) ||
      !near(document.currentClip().value("frameOffsetY").toDouble(), 80.0 / 520.0))
    return fail("Dragging the picture must move it in quarter-frame coordinates");
  document.undo();
  if (!near(document.currentClip().value("frameOffsetX").toDouble(), 0))
    return fail("A complete pointer move must undo in one step");
  if (!drag("timelineProgramResizeHandle3", {-80, -45}) ||
      document.currentClip().value("frameScale").toDouble() >= 0.95 ||
      document.currentClip().value("frameOffsetX").toDouble() >= -0.1) {
    std::cerr << "resize scale=" << document.currentClip().value("frameScale").toDouble()
              << " x=" << document.currentClip().value("frameOffsetX").toDouble() << '\n';
    return fail("Corner resizing must preserve aspect and move the center around the opposite anchor");
  }
  document.undo();
  if (!drag("timelineProgramRotationHandle", {70, 20}) ||
      std::abs(document.currentClip().value("frameRotationDegrees").toDouble()) < 5)
    return fail("The rotation handle must rotate the selected picture");
  document.undo();
  const auto before_cancel = document.deterministicDocumentHash();
  if (!drag("timelineProgramTransformMoveArea", {60, 25}, true) ||
      document.deterministicDocumentHash() != before_cancel)
    return fail("Escape must restore the exact authored transform");

  int publications = 0;
  QObject::connect(&document, &TimelineDocumentController::selectedClipStateChanged,
                   [&] { ++publications; });
  if (!document.setSelectedClipTransform(0.4, -0.2, 1.3, 12) || publications != 1)
    return fail("One four-property transform must publish once");
  document.undo();
  for (const auto& parameter : {"frameOffsetX", "frameOffsetY", "frameScale", "frameRotationDegrees"})
    document.toggleSelectedClipKeyframe(parameter);
  document.seekSequenceSeconds(1.0);
  if (!document.setSelectedClipTransform(0.4, -0.2, 1.3, 12))
    return fail("An animated transform must update all four properties");
  auto keys = document.currentClip().value("keyframes").toList();
  if (keys.size() != 8) return fail("Dragging animated properties must preserve initial keys and add playhead keys");
  document.setSelectedClipKeyframeInterpolation("frameScale", 1.0, "bezier");
  document.setSelectedClipKeyframeBezierHandle("frameScale", 1.0, "in", -0.2, -0.1);
  document.setSelectedClipTransform(0.5, -0.1, 1.6, 15);
  keys = document.currentClip().value("keyframes").toList();
  bool preserved = false;
  for (const auto& value : keys) {
    const auto key = value.toMap();
    if (key.value("parameter") == "frameScale" && near(key.value("seconds").toDouble(), 1.0))
      preserved = key.value("interpolation") == "bezier" && key.value("bezierInCustom").toBool();
  }
  if (!preserved) return fail("Direct manipulation must preserve existing Bézier tangents");
  const auto locked_hash = document.deterministicDocumentHash();
  if (document.setSelectedClipTransform(0, 0, std::numeric_limits<double>::quiet_NaN(), 0) ||
      document.deterministicDocumentHash() != locked_hash)
    return fail("Non-finite transforms must leave the edit untouched");
  document.toggleTrackLocked(0);
  if (document.setSelectedClipTransform(0, 0, 1, 0)) return fail("Locked tracks must reject direct transforms");
  document.toggleTrackLocked(0);
  if (!document.saveTimeline()) return fail("Animated transforms must save");
  TimelineDocumentController reopened;
  if (!reopened.loadTimeline(document.savedDocumentPath())) return fail("Animated transforms must reopen");
  reopened.selectClip(0);
  if (reopened.currentClip().value("keyframes").toList() != keys)
    return fail("Transform keys and tangents must survive save/reopen");

  if (QStandardPaths::findExecutable("ffmpeg").isEmpty()) return fail("FFmpeg is required for fade verification");
  TimelineDocumentController fades;
  fades.createTimelineWithSettings({{"timelineName", "Fades"}, {"width", 160}, {"height", 90}, {"frameRate", 30.0}});
  fades.addClipFromGallery("photo:fades", source, "Fade picture", 2.0, {});
  fades.selectClip(0);
  fades.assignSelectedClipTransition("in", "Fade to Black", 0.5);
  fades.assignSelectedClipTransition("out", "Fade to White", 0.5);
  QString error;
  const auto output = temp.filePath("fades.mp4");
  auto args = TimelineHistoryTestAccess::render(fades, output, &error);
  if (args.isEmpty() || !run(args)) return fail("Black and white fades must render through the normal compositor");
  const auto black = sample(output, 0.0);
  const auto middle = sample(output, 1.0);
  const auto white = sample(output, 1.966);
  if (!black.isValid() || black.red() > 8 || black.green() > 8 ||
      middle.red() < 190 || middle.green() > 55 || white.green() < 230 || white.blue() < 230)
    return fail("Fade endpoints must be black/white with the original picture between them");
  const auto range = temp.filePath("fade-range.mp4");
  args = TimelineHistoryTestAccess::render(fades, range, &error, 1.5, 2.0);
  if (args.isEmpty() || !run(args) || std::abs(sample(range, 0.25).green() - sample(output, 1.75).green()) > 8)
    return fail("A cached range must use the same clip-local fade timing as full export");
  std::cout << "Transform gestures, animation, persistence, and black/white fade pixels passed\n";
}
