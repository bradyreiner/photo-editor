#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>
#include <QUrl>

#include <iostream>
#include <memory>

namespace {
int fail(const char* message) {
  std::cerr << "Timeline media lifecycle: " << message << '\n';
  return 1;
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QFile source(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/TimelineView.qml"));
  if (!source.open(QIODevice::ReadOnly)) return fail("production QML unavailable");
  const auto text = QString::fromUtf8(source.readAll());
  const auto marker = text.indexOf(QStringLiteral("id: backgroundPlayerLoader"));
  const auto start = text.lastIndexOf(QStringLiteral("Loader {"), marker);
  const auto end = text.indexOf(QStringLiteral("\n\n                                                    MultiEffect {"), marker);
  if (marker < 0 || start < 0 || end < 0) return fail("production video Loader unavailable");
  auto player = text.mid(start, end - start);
  const QString player_name = QStringLiteral("objectName: \"timelineProgramBackgroundMediaPlayer\"");
  if (player.count(player_name) != 1) return fail("expected one production player factory");
  player.replace(player_name, player_name + QStringLiteral(R"QML(
    Component.onCompleted: { root.livePlayers += 1; root.createdPlayers += 1 }
    Component.onDestruction: root.livePlayers -= 1
)QML"));
  const auto function_start = text.indexOf(QStringLiteral("function syncLayerPlayer()"), start - 2500);
  const auto function_end = text.indexOf(QStringLiteral("\n\n                                                    Loader {"), function_start);
  if (function_start < 0 || function_end <= function_start || function_end > start)
    return fail("production player synchronization function unavailable");
  const auto sync = text.mid(function_start, function_end - function_start);
  const auto fixture = QStringLiteral(R"QML(
import QtQuick
import QtQuick.Window
import QtMultimedia
Window {
    id: root
    width: 64; height: 64; visible: false
    property int modelCount: 36
    property int livePlayers: 0
    property int createdPlayers: 0
    property bool sequencePlaying: false
    property string firstKind: "video"
    property real effectAmount: 0
    function programLayerVisible(row) { return true }
    function programPlaybackSourceUrl(row, secondary) { return "" }
    function programLayerSourceSeconds(row, secondary) { return 0 }
    Repeater {
        model: root.modelCount
        delegate: Item {
            id: programBackgroundLayer
            required property int index
            property var modelData: ({mediaType: index === 0 ? root.firstKind : index % 3 === 0 ? "photo" : index % 3 === 1 ? "title" : "motion-generator", effect: root.effectAmount})
            width: 64; height: 64
            %1
            %2
            VideoOutput { id: backgroundVideo; anchors.fill: parent }
        }
    }
}
)QML").arg(sync, player);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(fixture.toUtf8(), QUrl(QStringLiteral("file:///timeline-media-lifecycle.qml")));
  if (component.isError()) {
    std::cerr << component.errorString().toStdString();
    return fail("production player fixture failed to compile");
  }
  std::unique_ptr<QObject> root(component.create());
  if (!root) return fail("fixture failed to instantiate");
  QTest::qWait(20);
  if (root->property("livePlayers").toInt() != 1)
    return fail("35 non-video layers must not allocate native media players");
  const auto created = root->property("createdPlayers").toInt();
  for (int edit = 1; edit <= 100; ++edit) {
    root->setProperty("effectAmount", static_cast<double>(edit) / 100);
    QCoreApplication::processEvents();
  }
  if (root->property("createdPlayers").toInt() != created
      || root->property("livePlayers").toInt() != 1)
    return fail("ordinary effect changes must retain the existing video player");
  root->setProperty("firstKind", "title");
  QTest::qWait(10);
  if (root->property("livePlayers").toInt() != 0)
    return fail("a composition containing no video must own no media players");
  root->setProperty("firstKind", "video");
  QTest::qWait(10);
  if (root->property("livePlayers").toInt() != 1)
    return fail("restoring video must recreate exactly its own player");
  for (int cycle = 0; cycle < 50; ++cycle) {
    root->setProperty("modelCount", 0);
    QTest::qWait(2);
    if (root->property("livePlayers").toInt() != 0)
      return fail("removed delegates must release their media players");
    root->setProperty("modelCount", 36);
    QTest::qWait(2);
    if (root->property("livePlayers").toInt() != 1)
      return fail("recreated layers must allocate players only for video");
  }
  root->setProperty("modelCount", 0);
  QTest::qWait(20);
  std::cout << "timeline_media_player_lifecycle=passed; nonvideo_players=0; video_players=1; cycles=50\n";
  return 0;
}
