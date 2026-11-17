#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QHash>
#include <QJSValue>
#include <QList>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QUrl>
#include <QVariant>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

QQuickItem* find_item(QQuickItem* root, const QString& object_name) {
  if (root == nullptr) return nullptr;
  if (root->objectName() == object_name) return root;
  for (auto* child : root->childItems()) {
    if (auto* found = find_item(child, object_name)) return found;
  }
  return nullptr;
}

void settle_qml() {
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

QVariantMap invoke_map(QObject* object, const char* method, const QList<QVariant>& arguments) {
  QVariant result;
  if (object == nullptr) return {};

  bool invoked = false;
  if (arguments.size() == 4) {
    invoked = QMetaObject::invokeMethod(
        object, method, Q_RETURN_ARG(QVariant, result),
        Q_ARG(QVariant, arguments.at(0)), Q_ARG(QVariant, arguments.at(1)),
        Q_ARG(QVariant, arguments.at(2)), Q_ARG(QVariant, arguments.at(3)));
  } else if (arguments.size() == 1) {
    invoked = QMetaObject::invokeMethod(
        object, method, Q_RETURN_ARG(QVariant, result),
        Q_ARG(QVariant, arguments.at(0)));
  } else if (arguments.isEmpty()) {
    invoked = QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result));
  }
  if (!invoked) {
    return {{QStringLiteral("fixtureInvokeError"),
             QStringLiteral("QML fixture method could not be invoked: %1")
                 .arg(QString::fromLatin1(method))}};
  }
  if (result.canConvert<QJSValue>()) result = result.value<QJSValue>().toVariant();
  return result.toMap();
}

QQuickItem* row_item(QQuickItem* root, int track_index) {
  return find_item(root, QStringLiteral("virtualTrackRow-track-%1").arg(track_index));
}

bool row_has_revision(QQuickItem* item, int revision) {
  if (item == nullptr) return false;
  const auto track_data = item->property("trackData").toMap();
  const auto clips = track_data.value(QStringLiteral("clips")).toList();
  return track_data.value(QStringLiteral("clipCount")).toInt() == revision &&
         clips.size() == 1 &&
         clips.constFirst().toMap().value(QStringLiteral("clipId")).toString() ==
             QStringLiteral("clip-%1-r%2")
                 .arg(track_data.value(QStringLiteral("trackIndex")).toInt())
                 .arg(revision);
}

}  // namespace

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  QQmlEngine engine;
  QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                   [](const QList<QQmlError>& warnings) {
                     for (const auto& warning : warnings) {
                       qWarning().noquote() << warning.toString();
                     }
                   });
  const auto helper_path = QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/TimelineVirtualTrackRows.qml");
  QQmlComponent model_component(&engine, QUrl::fromLocalFile(helper_path));
  std::unique_ptr<QObject> model(model_component.create());
  if (!model) return fail(model_component.errorString());

  engine.rootContext()->setContextProperty(QStringLiteral("virtualizedTrackRows"), model.get());
  QQuickWindow window;
  window.resize(420, 220);
  const QByteArray harness = R"QML(
import QtQuick

Item {
    id: root
    objectName: "timelineVirtualTrackRowsHarness"
    width: 420
    height: 220
    property var sequenceTrackModel: []
    property int delegateCreated: 0
    property int delegateDestroyed: 0

    function synchronizeGeneratedRows(firstIndex, rowCount, revision, updateTrackData) {
        const tracks = []
        for (let index = 0; index < 659; ++index) {
            tracks.push({
                trackId: "track-" + index,
                trackIndex: index,
                displayLabel: "Track " + index + " revision 0",
                clipCount: 0,
                clips: []
            })
        }
        const rows = []
        for (let index = firstIndex; index < firstIndex + rowCount; ++index) {
            const keyframes = []
            for (let keyIndex = 0; keyIndex < 1204; ++keyIndex) {
                keyframes.push({
                    parameter: "opacity",
                    timeSeconds: keyIndex / 30,
                    value: (keyIndex % 2) === 0 ? 0 : 1
                })
            }
            const track = {
                trackId: "track-" + index,
                trackIndex: index,
                displayLabel: "Track " + index + " revision " + revision,
                clipCount: revision,
                clips: [{clipId: "clip-" + index + "-r" + revision,
                         keyframes: keyframes}]
            }
            tracks[index] = track
            rows.push(track)
        }
        root.sequenceTrackModel = tracks
        return virtualizedTrackRows.synchronizeRows(rows)
    }

    function modelRowHasOnlyIdentityRoles() {
        if (virtualizedTrackRows.count < 1)
            return {onlyIdentity: false}
        const row = virtualizedTrackRows.get(0)
        const keys = Object.keys(row || {}).sort()
        return {
            onlyIdentity: keys.length === 2
                          && keys[0] === "trackIndex"
                          && keys[1] === "trackKey"
                          && row.trackData === undefined
                          && row.clips === undefined
        }
    }

    function reverseVisibleRows() {
        const rows = []
        for (let index = virtualizedTrackRows.count - 1; index >= 0; --index) {
            const row = virtualizedTrackRows.get(index)
            const track = sequenceTrackModel[Number(row.trackIndex)]
            if (!track || String(track.trackId || "") !== String(row.trackKey || ""))
                return {ok: false, error: "The source track snapshot changed during reorder."}
            rows.push(track)
        }
        return virtualizedTrackRows.synchronizeRows(rows)
    }

    function synchronizeDuplicateRows() {
        return virtualizedTrackRows.synchronizeRows([
            {trackId: "duplicate"},
            {trackId: "duplicate"}
        ], true)
    }

    function probeTrackIndexAtY(tops, heights, hidden, contentY) {
        const index = virtualizedTrackRows.nearestTrackIndexAtY({
            topByIndex: tops,
            rowHeightByIndex: heights,
            hiddenByIndex: hidden
        }, contentY)
        return {index: index}
    }

    Flickable {
        id: viewport
        objectName: "trackRowsViewport"
        anchors.fill: parent
        contentWidth: width
        contentHeight: 659 * 40
        clip: true
        interactive: false

        Repeater {
            id: rowsRepeater
            objectName: "trackRowsRepeater"
            model: virtualizedTrackRows
            delegate: Rectangle {
                required property string trackKey
                required property int trackIndex
                readonly property var trackData: {
                    const tracks = root.sequenceTrackModel || []
                    const track = tracks[trackIndex]
                    return track && String(track.trackId || "") === trackKey ? track : ({})
                }
                objectName: "virtualTrackRow-" + trackKey
                x: 0
                y: trackIndex * 40
                width: 420
                height: 40
                color: "transparent"
                Text {
                    objectName: "virtualTrackLabel-" + parent.trackKey
                    text: String(parent.trackData.displayLabel || "")
                }
                Component.onCompleted: root.delegateCreated += 1
                Component.onDestruction: root.delegateDestroyed += 1
            }
        }
    }
}
)QML";
  QQmlComponent harness_component(&engine);
  harness_component.setData(
      harness,
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR
                                         "/apps/desktop/qml/components/TimelineVirtualTrackRowsTestHarness.qml")));
  std::unique_ptr<QObject> harness_object(harness_component.create());
  if (!harness_object) return fail(harness_component.errorString());
  auto* root = qobject_cast<QQuickItem*>(harness_object.get());
  if (root == nullptr) return fail(QStringLiteral("The virtual track row harness must be a QQuickItem."));

  root->setParentItem(window.contentItem());
  window.show();
  settle_qml();

  const auto initial = invoke_map(
      root, "synchronizeGeneratedRows",
      {QVariant(290), QVariant(16), QVariant(1), QVariant(true)});
  settle_qml();
  if (!initial.value(QStringLiteral("ok")).toBool() ||
      initial.value(QStringLiteral("rowCount")).toInt() != 16 ||
      root->property("delegateCreated").toInt() != 16 ||
      root->property("delegateDestroyed").toInt() != 0) {
    qWarning().noquote() << "Initial keyed-row state:" << initial
                         << "model.count=" << model->property("count")
                         << "delegateCreated=" << root->property("delegateCreated")
                         << "delegateDestroyed=" << root->property("delegateDestroyed");
    return fail(QStringLiteral("Initial visible track IDs should materialize exactly one delegate each."));
  }
  const auto compact_roles = invoke_map(root, "modelRowHasOnlyIdentityRoles", {});
  if (!compact_roles.value(QStringLiteral("onlyIdentity")).toBool()) {
    return fail(QStringLiteral(
        "The virtual row ListModel must store only stable track ID and source index, not clip or keyframe maps."));
  }

  QHash<int, QQuickItem*> original_items;
  for (int index = 290; index < 306; ++index) {
    auto* item = row_item(root, index);
    if (item == nullptr) return fail(QStringLiteral("Every visible stable track ID should have a row delegate."));
    original_items.insert(index, item);
  }

  const auto updated = invoke_map(
      root, "synchronizeGeneratedRows",
      {QVariant(290), QVariant(16), QVariant(2), QVariant(true)});
  settle_qml();
  if (!updated.value(QStringLiteral("ok")).toBool() ||
      root->property("delegateCreated").toInt() != 16 ||
      root->property("delegateDestroyed").toInt() != 0) {
    return fail(QStringLiteral("A batch update for unchanged visible IDs must not recreate their delegates."));
  }
  for (int index = 290; index < 306; ++index) {
    auto* item = row_item(root, index);
    if (item != original_items.value(index) || !row_has_revision(item, 2)) {
      return fail(QStringLiteral("Existing row delegates must retain identity while receiving the latest track and clip data."));
    }
    auto* label = find_item(root, QStringLiteral("virtualTrackLabel-track-%1").arg(index));
    if (label == nullptr ||
        label->property("text").toString() !=
            QStringLiteral("Track %1 revision 2").arg(index)) {
      return fail(QStringLiteral("Existing visible rows must refresh their displayed metadata in place."));
    }
  }

  auto* viewport = qobject_cast<QQuickItem*>(find_item(root, QStringLiteral("trackRowsViewport")));
  if (viewport == nullptr) return fail(QStringLiteral("The virtualized row viewport should exist."));
  viewport->setProperty("contentY", 300.0 * 40.0 - 100.0);
  const auto scrolled = invoke_map(
      root, "synchronizeGeneratedRows",
      {QVariant(293), QVariant(16), QVariant(3), QVariant(true)});
  settle_qml();
  if (!scrolled.value(QStringLiteral("ok")).toBool() ||
      root->property("delegateCreated").toInt() != 19 ||
      root->property("delegateDestroyed").toInt() != 3) {
    return fail(QStringLiteral("Scrolling to overlapping rows should add and remove only IDs leaving the viewport."));
  }
  for (int index = 293; index < 306; ++index) {
    auto* item = row_item(root, index);
    if (item == nullptr || !row_has_revision(item, 3)) {
      return fail(QStringLiteral("Visible rows after scroll must expose the current stable track and clip state."));
    }
    if (index >= 296 && original_items.value(index) != item) {
      return fail(QStringLiteral("Overlapping visible IDs must retain their row delegates during viewport scroll."));
    }
  }
  for (int index = 306; index < 309; ++index) {
    if (row_item(root, index) == nullptr)
      return fail(QStringLiteral("Newly visible rows must be materialized for immediate hit testing."));
  }

  QHash<int, QQuickItem*> scrolled_items;
  for (int index = 293; index < 309; ++index) {
    auto* item = row_item(root, index);
    if (item == nullptr) return fail(QStringLiteral("Visible rows need stable IDs before reorder."));
    scrolled_items.insert(index, item);
  }
  const auto reordered = invoke_map(root, "reverseVisibleRows", {});
  settle_qml();
  if (!reordered.value(QStringLiteral("ok")).toBool())
    return fail(QStringLiteral("Reordering track IDs must keep the owned track snapshot available."));
  for (int index = 293; index < 309; ++index) {
    auto* item = row_item(root, index);
    if (item != scrolled_items.value(index) || !row_has_revision(item, 3)) {
      return fail(QStringLiteral(
          "Reordered rows must retain each stable delegate and resolve its latest full track snapshot."));
    }
  }

  const QVariantList heterogeneous_tops = {24.0, 54.0, 256.0, 286.0};
  const QVariantList heterogeneous_heights = {28.0, 200.0, 28.0, 200.0};
  const QVariantList heterogeneous_hidden = {false, false, false, false};
  const auto dragged_into_tall_row = invoke_map(
      root, "probeTrackIndexAtY",
      {QVariant::fromValue(heterogeneous_tops),
       QVariant::fromValue(heterogeneous_heights),
       QVariant::fromValue(heterogeneous_hidden), QVariant(124.0)});
  if (dragged_into_tall_row.value(QStringLiteral("index")).toInt() != 1) {
    return fail(QStringLiteral(
        "A drag pointer inside a 200px row after a 28px row must resolve to the tall row, not a fixed-pitch estimate."));
  }
  const auto dragged_into_short_row = invoke_map(
      root, "probeTrackIndexAtY",
      {QVariant::fromValue(heterogeneous_tops),
       QVariant::fromValue(heterogeneous_heights),
       QVariant::fromValue(heterogeneous_hidden), QVariant(264.0)});
  if (dragged_into_short_row.value(QStringLiteral("index")).toInt() != 2) {
    return fail(QStringLiteral(
        "A drag pointer inside a 28px row after a 200px row must resolve to the short row, not remain on the source row."));
  }
  const auto hidden_row_is_skipped = invoke_map(
      root, "probeTrackIndexAtY",
      {QVariant::fromValue(QVariantList{24.0, 54.0, 54.0, 84.0}),
       QVariant::fromValue(QVariantList{28.0, 0.0, 28.0, 200.0}),
       QVariant::fromValue(QVariantList{false, true, false, false}), QVariant(70.0)});
  if (hidden_row_is_skipped.value(QStringLiteral("index")).toInt() != 2) {
    return fail(QStringLiteral(
        "Drag targeting must skip collapsed rows and resolve against the visible heterogeneous row geometry."));
  }

  const auto selected_row = row_item(root, 300);
  const auto selected_viewport_y = selected_row == nullptr
      ? -1.0 : selected_row->mapToItem(viewport, QPointF(0.0, 0.0)).y();
  if (selected_viewport_y < 0.0 || selected_viewport_y + selected_row->height() > viewport->height()) {
    return fail(QStringLiteral("A selected row in the scrolled viewport must keep correct visible hit geometry."));
  }

  const auto invalid = invoke_map(root, "synchronizeDuplicateRows", {});
  settle_qml();
  if (invalid.value(QStringLiteral("ok")).toBool() ||
      root->property("delegateCreated").toInt() != 19 ||
      row_item(root, 300) == nullptr || !row_has_revision(row_item(root, 300), 3)) {
    return fail(QStringLiteral("Invalid duplicate track IDs must fail closed without disturbing the live viewport model."));
  }

  window.close();
  return EXIT_SUCCESS;
}
