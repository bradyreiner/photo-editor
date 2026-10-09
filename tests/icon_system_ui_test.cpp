#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QThread>
#include <iostream>
#include <memory>

namespace {
void settle() {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 180) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
QQuickItem* find_item(QQuickItem* root, const QString& name) {
  if (root->objectName() == name) return root;
  for (auto* child : root->childItems())
    if (auto* found = find_item(child, name)) return found;
  return nullptr;
}
QImage crop_image(const QImage& shot, QQuickItem* item) {
  const auto ratio = shot.devicePixelRatio();
  const auto origin = item->mapToScene(QPointF(0, 0));
  return shot.copy(QRect(qRound(origin.x() * ratio), qRound(origin.y() * ratio),
                        qRound(item->width() * ratio), qRound(item->height() * ratio)));
}
QImage crop_item(QQuickWindow* window, QQuickItem* item) {
  return crop_image(window->grabWindow(), item);
}
int light_pixels(const QImage& shot) {
  int count = 0;
  for (int y = 0; y < shot.height(); ++y)
    for (int x = 0; x < shot.width(); ++x)
      if (shot.pixelColor(x, y).lightness() > 100) ++count;
  return count;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QFile source(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/AppIcon.qml"));
  if (!source.open(QIODevice::ReadOnly)) return fail("Icon source must be readable");
  QStringList names;
  auto matches = QRegularExpression(QStringLiteral("case \"([^\"]+)\":"))
                     .globalMatch(QString::fromUtf8(source.readAll()));
  while (matches.hasNext()) names.append(matches.next().captured(1));
  names.removeDuplicates();
  names.sort();
  // Audit the app-wide static vocabulary as well as the rendered catalog.
  QDirIterator qml_files(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml"),
                        QStringList{QStringLiteral("*.qml")}, QDir::Files, QDirIterator::Subdirectories);
  const QRegularExpression icon_reference(QStringLiteral("\\b(?:iconName|icon)\\s*:\\s*\"([^\"]+)\""));
  while (qml_files.hasNext()) {
    QFile qml(qml_files.next());
    if (!qml.open(QIODevice::ReadOnly)) return fail("QML inventory must be readable");
    auto references = icon_reference.globalMatch(QString::fromUtf8(qml.readAll()));
    while (references.hasNext()) {
      const auto name = references.next().captured(1);
      if (!name.isEmpty() && !names.contains(name)) {
        std::cerr << qml.fileName().toStdString() << ": " << name.toStdString() << '\n';
        return fail("Static workspace and popup icons must belong to the shared vocabulary");
      }
    }
  }
  QQmlEngine engine;
  QStringList warnings;
  QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError>& errors) {
    for (const auto& error : errors) warnings.append(error.toString());
  });
  QQmlPropertyMap settings;
  settings.insert(QStringLiteral("interfaceDensity"), QStringLiteral("balanced"));
  settings.insert(QStringLiteral("reducedMotion"), true);
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), &settings);
  engine.rootContext()->setContextProperty(QStringLiteral("allIconNames"), names);
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
ApplicationWindow {
    id: fixture
    width: 1160; height: 900; visible: true; color: "#111719"
    title: "Looksy icon review"
    property int page: 0
    property int retries: 0
    ColumnLayout {
        x: 28; y: 24; width: 1104; spacing: 20
        Label { text: "Looksy / icons in context"; color: "#e8ece9"; font.pixelSize: 24 }
        RowLayout {
            spacing: 12
            Repeater {
                model: ["library", "edit", "layers", "masks", "carousel", "timeline", "export", "settings"]
                SidebarButton { required property string modelData; title: modelData; kind: modelData; highlighted: modelData === "timeline"; Layout.preferredWidth: 124 }
            }
        }
        RowLayout {
            spacing: 12
            Repeater {
                model: ["photo", "video", "audio", "carousel", "timeline"]
                Rectangle {
                    required property string modelData
                    width: 210; height: 76; color: "#1b292d"; radius: 3
                    TimelineClipContent { anchors.fill: parent; title: parent.modelData + " clip"; mediaType: parent.modelData; metadata: "00:00:12:08"; selected: parent.modelData === "video" }
                }
            }
        }
        RowLayout {
            spacing: 16
            InlineActivityIndicator { title: "Rendering"; detail: "Preparing preview"; progressValue: 0.35; Layout.preferredWidth: 350 }
            InlineActivityIndicator { objectName: "retryIndicator"; title: "Export paused"; detail: "Destination unavailable"; retryable: true; Layout.preferredWidth: 440; onRetryRequested: fixture.retries++ }
            InlineActivityIndicator { title: "Saved"; progressValue: 1; Layout.preferredWidth: 270 }
        }
        RowLayout {
            spacing: 12
            StatusChip { objectName: "errorChip"; text: "Preview failed"; tone: "error"; actionText: "Retry"; onActionTriggered: fixture.retries++ }
            StatusChip { text: "Check destination"; tone: "warning" }
            StatusChip { text: "Saved locally"; tone: "success" }
            ToolButton { iconName: "audio"; label: "Mute track" }
            ToolButton { iconName: "audio-muted"; label: "Unmute track"; active: true }
            ToolButton { iconName: "solo"; label: "Solo track"; active: true }
            ToolButton { iconName: "snap"; label: "Snapping"; active: true }
            ToolButton { iconName: "keyframe"; label: "Add keyframe" }
            ToolButton { iconName: "keyframe-filled"; label: "Remove keyframe"; active: true }
            ToolButton { iconName: "external-link"; label: "Open curve editor" }
        }
        RowLayout {
            spacing: 16
            PopupMenuRow { index: 0; modelData: ({ label: "Duplicate", icon: "copy" }); Layout.preferredWidth: 220 }
            PopupMenuRow { index: 1; modelData: ({ label: "Snapping", icon: "snap", checked: true }); Layout.preferredWidth: 220 }
            PopupMenuRow { objectName: "emptyMenuRow"; index: 2; modelData: ({ label: "Custom action" }); Layout.preferredWidth: 220 }
            PopupMenuRow { index: 3; modelData: ({ label: "Remove clip", icon: "trash", danger: true }); Layout.preferredWidth: 220 }
        }
        Label { text: "14 / 18 / 24 px  ·  " + appShell.interfaceDensity + "  ·  sheet " + (fixture.page + 1); color: "#84979c"; font.pixelSize: 13 }
        Grid {
            columns: 8; columnSpacing: 8; rowSpacing: 14
            Repeater {
                model: allIconNames.slice(fixture.page * 48, (fixture.page + 1) * 48)
                delegate: Column {
                    required property string modelData
                    width: 131; spacing: 6
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter; spacing: 12; height: 24
                        Repeater {
                            model: [14, 18, 24]
                            AppIcon {
                                required property int modelData
                                objectName: "glyph-" + parent.parent.modelData + "-" + modelData
                                name: parent.parent.modelData
                                width: modelData; height: width; anchors.verticalCenter: parent.verticalCenter; color: "#e8ece9"
                            }
                        }
                    }
                    Text { width: parent.width; text: parent.modelData; color: "#84979c"; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter }
                }
            }
        }
    }
    AppIcon { objectName: "probe"; x: 1090; y: 850; width: 16; height: 16; name: "timeline"; color: "#e8ece9" }
}
)qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/icon-system-fixture.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  if (!window) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("Production icon and feedback components must load");
  }
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_ICON_PROOF_DIR");
  if (!output.isEmpty() && !QDir().mkpath(output)) return fail("Proof directory must be writable");
  for (const auto& density : {QStringLiteral("compact"), QStringLiteral("balanced"), QStringLiteral("comfortable")}) {
    settings.insert(QStringLiteral("interfaceDensity"), density);
    for (int page = 0; page * 48 < names.size(); ++page) {
      fixture->setProperty("page", page);
      settle();
      const auto shot = window->grabWindow();
      if (shot.isNull()) return fail("Native window must render");
      for (const auto& name : names.mid(page * 48, 48)) {
        for (const int size : {14, 18, 24}) {
          auto* icon = find_item(window->contentItem(), QStringLiteral("glyph-%1-%2").arg(name).arg(size));
          if (!icon || light_pixels(crop_image(shot, icon)) < 3) {
            std::cerr << name.toStdString() << " at " << size << '\n';
            return fail("Every named icon must produce visible pixels at control sizes");
          }
        }
      }
      if (!output.isEmpty() && !shot.save(output + QStringLiteral("/%1-sheet-%2.png").arg(density).arg(page + 1)))
        return fail("Native sheet must save");
    }
  }
  auto* probe = find_item(window->contentItem(), QStringLiteral("probe"));
  probe->setProperty("name", QStringLiteral("timeline"));
  settle();
  const auto square = crop_item(window, probe);
  probe->setWidth(32);
  settle();
  const auto wide = crop_item(window, probe);
  if (square != wide.copy(square.width() / 2, 0, square.width(), square.height()))
    return fail("Icons must stay square and centered inside rectangular allocations");
  probe->setWidth(16);
  probe->setProperty("name", QStringLiteral("minus"));
  settle();
  const auto minus = crop_item(window, probe);
  probe->setProperty("name", QStringLiteral("missing-icon"));
  settle();
  if (minus == crop_item(window, probe)) return fail("Unknown icons must not impersonate minus actions");
  probe->setProperty("name", QString());
  settle();
  if (light_pixels(crop_item(window, probe)) != 0) return fail("Empty icons must remain blank");
  auto* chip = find_item(window->contentItem(), QStringLiteral("errorChip"));
  if (!chip || chip->property("resolvedIconName").toString() != QStringLiteral("error"))
    return fail("Error notifications need a distinct error symbol");
  auto* indicator = find_item(window->contentItem(), QStringLiteral("retryIndicator"));
  auto* retry = indicator ? find_item(indicator, QStringLiteral("activityCancelButton")) : nullptr;
  if (!retry || retry->property("iconName").toString() != QStringLiteral("retry"))
    return fail("Retry notifications need a labeled retry action");
  retry->forceActiveFocus(Qt::TabFocusReason);
  QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &press);
  QCoreApplication::sendEvent(window, &release);
  settle();
  if (fixture->property("retries").toInt() != 1) return fail("Keyboard retry must still emit its real action");
  // Exercise the actual saved-Boards collection with explicitly fictional data.
  QQmlComponent board_source(&engine);
  board_source.setData(R"qml(import QtQml
QtObject {
    function recentDocuments() {
        return [{ documentId: "sample-boards", name: "Sample / Five-board story", slideCount: 5, documentFormat: "4x5" },
                { documentId: "sample-wide", name: "Sample / Wide sequence", slideCount: 3, documentFormat: "16x9" }]
    }
})qml", QUrl());
  std::unique_ptr<QObject> board_controller(board_source.create());
  if (!board_controller) return fail("Sample board source must load");
  engine.rootContext()->setContextProperty(QStringLiteral("carouselDocument"), board_controller.get());
  QQmlComponent board_component(&engine);
  board_component.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
ApplicationWindow {
    id: boardsFixture
    width: 760; height: 480; visible: true; color: "#111719"
    property int opened: 0
    Label { x: 24; y: 20; text: "Sample documents / Library cards"; color: "#84979c"; font.pixelSize: 13 }
    SavedBoardsLibrary { x: 24; y: 55; width: 712; height: 400; onOpenRequested: boardsFixture.opened++ }
})qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/boards-icon-fixture.qml")));
  std::unique_ptr<QObject> boards(board_component.create());
  auto* board_window = qobject_cast<QQuickWindow*>(boards.get());
  if (!board_window) {
    std::cerr << board_component.errorString().toStdString() << '\n';
    return fail("Saved Boards production collection must load");
  }
  settle();
  auto* board_grid = find_item(board_window->contentItem(), QStringLiteral("savedBoardsGrid"));
  auto* open_board = find_item(board_window->contentItem(), QStringLiteral("libraryOpenBoardsButtonsample-boards"));
  if (!board_grid || board_grid->property("count").toInt() != 2 || !open_board)
    return fail("Sample board cards and their real open controls must be present");
  if (!output.isEmpty() && !board_window->grabWindow().save(output + QStringLiteral("/saved-boards-samples.png")))
    return fail("Saved Boards native review must save");
  open_board->forceActiveFocus(Qt::TabFocusReason);
  QCoreApplication::sendEvent(board_window, &press);
  QCoreApplication::sendEvent(board_window, &release);
  settle();
  if (boards->property("opened").toInt() != 1)
    return fail("Adding collection icons must preserve keyboard document opening");
  if (!warnings.isEmpty()) {
    std::cerr << warnings.join('\n').toStdString() << '\n';
    return fail("Icon review must have no QML warnings");
  }
  std::cout << names.size() << " icons rendered at 14/18/24 px in three densities; geometry and keyboard retry passed\n";
  return 0;
}
