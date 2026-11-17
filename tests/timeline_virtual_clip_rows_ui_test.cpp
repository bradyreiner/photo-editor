#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
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
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QStringList>

#include <cstdlib>
#include <functional>
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

QVariantMap invoke_map(QObject* object, const char* method) {
  QVariant result;
  if (object == nullptr ||
      !QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result))) {
    return {{QStringLiteral("fixtureInvokeError"),
             QStringLiteral("QML fixture method could not be invoked: %1")
                 .arg(QString::fromLatin1(method))}};
  }
  if (result.canConvert<QJSValue>()) result = result.value<QJSValue>().toVariant();
  return result.toMap();
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    settle_qml();
    if (predicate()) return true;
  }
  return predicate();
}

QVariantMap make_clip(const QString& clip_id, int revision) {
  QVariantList keyframes;
  if (clip_id == QStringLiteral("clip-C")) {
    keyframes.reserve(1204);
    for (int index = 0; index < 1204; ++index) {
      keyframes.push_back(QVariantMap{
          {QStringLiteral("parameter"), QStringLiteral("opacity")},
          {QStringLiteral("timeSeconds"), static_cast<double>(index) / 30.0},
          {QStringLiteral("value"), index % 2 == 0 ? 0.0 : 1.0},
      });
    }
  }
  return {
      {QStringLiteral("clipId"), clip_id},
      {QStringLiteral("revision"), revision},
      {QStringLiteral("keyframes"), keyframes},
  };
}

QVariantList make_source_clips(const QStringList& clip_ids, int revision) {
  QVariantList source;
  source.reserve(clip_ids.size());
  for (const auto& clip_id : clip_ids)
    source.push_back(make_clip(clip_id, revision));
  return source;
}

QVariantList make_visible_clips(
    const QVariantList& source, const QStringList& visible_ids) {
  QHash<QString, QVariantMap> clips_by_id;
  for (const auto& value : source) {
    const auto clip = value.toMap();
    clips_by_id.insert(clip.value(QStringLiteral("clipId")).toString(), clip);
  }
  QVariantList visible;
  visible.reserve(visible_ids.size());
  for (const auto& clip_id : visible_ids) {
    const auto found = clips_by_id.constFind(clip_id);
    if (found != clips_by_id.cend()) visible.push_back(found.value());
  }
  return visible;
}

void stage_snapshot(
    QObject* root,
    QObject* model,
    const QStringList& source_ids,
    const QStringList& visible_ids,
    int revision,
    bool suspended) {
  const auto source = make_source_clips(source_ids, revision);
  const auto visible = make_visible_clips(source, visible_ids);
  root->setProperty("sourceClips", QVariant::fromValue(source));
  root->setProperty("visibleClips", QVariant::fromValue(visible));
  if (suspended) model->setProperty("suspendUpdates", true);
  model->setProperty("sourceClips", QVariant::fromValue(source));
  model->setProperty("visibleClips", QVariant::fromValue(visible));
}

QVariantMap synchronize(QObject* model) {
  return invoke_map(model, "synchronize");
}

QVariantMap set_snapshot(
    QObject* root,
    QObject* model,
    const QStringList& source_ids,
    const QStringList& visible_ids,
    int revision,
    bool suspended = false) {
  stage_snapshot(root, model, source_ids, visible_ids, revision, suspended);
  if (!suspended) model->setProperty("suspendUpdates", false);
  return synchronize(model);
}

QQuickItem* clip_item(QQuickItem* root, const QString& clip_id) {
  return find_item(root, QStringLiteral("virtualClip-") + clip_id);
}

bool clip_item_has_revision(QQuickItem* item, const QString& clip_id, int revision) {
  if (item == nullptr) return false;
  const auto clip = item->property("clipData").toMap();
  return clip.value(QStringLiteral("clipId")).toString() == clip_id &&
      clip.value(QStringLiteral("revision")).toInt() == revision;
}

}  // namespace

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  QStringList qml_warnings;
  QQmlEngine engine;
  QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                   [&qml_warnings](const QList<QQmlError>& warnings) {
                     for (const auto& warning : warnings) {
                       qml_warnings.push_back(warning.toString());
                       qWarning().noquote() << warning.toString();
                     }
                   });

  const auto helper_path = QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/TimelineVirtualClipRows.qml");
  QQmlComponent model_component(&engine, QUrl::fromLocalFile(helper_path));
  std::unique_ptr<QObject> model(model_component.create());
  if (!model) return fail(model_component.errorString());
  if (!model->property("componentsReady").toBool())
    return fail(QStringLiteral("The clip row model should report ready after component creation."));

  engine.rootContext()->setContextProperty(QStringLiteral("virtualClipRows"), model.get());
  QQuickWindow window;
  window.resize(480, 180);
  const QByteArray harness = R"QML(
import QtQuick

Item {
    id: root
    objectName: "timelineVirtualClipRowsHarness"
    width: 480
    height: 180
    property var sourceClips: []
    property var visibleClips: []
    property int delegateCreated: 0
    property int delegateDestroyed: 0

    function modelHasOnlyIdentityRoles() {
        if (virtualClipRows.count < 1)
            return false
        const row = virtualClipRows.get(0)
        const keys = Object.keys(row || {}).sort()
        return keys.length === 2
                && keys[0] === "clipKey"
                && keys[1] === "sourceClipIndex"
                && row.clipData === undefined
                && row.keyframes === undefined
    }

    Flickable {
        objectName: "clipRowsViewport"
        anchors.fill: parent
        contentWidth: width
        contentHeight: 240
        clip: true
        interactive: false

        Repeater {
            objectName: "clipRowsRepeater"
            model: virtualClipRows
            delegate: Rectangle {
                required property string clipKey
                required property int sourceClipIndex
                required property int index
                readonly property var clipData: {
                    const clips = root.sourceClips || []
                    const indexed = clips[sourceClipIndex]
                    if (indexed && String(indexed.clipId || "") === clipKey)
                        return indexed
                    for (let index = 0; index < clips.length; ++index) {
                        if (String(clips[index] && clips[index].clipId || "") === clipKey)
                            return clips[index]
                    }
                    return ({})
                }
                objectName: "virtualClip-" + clipKey
                x: 0
                y: index * 42
                width: 480
                height: 40
                color: "transparent"
                Text {
                    objectName: "virtualClipLabel-" + parent.clipKey
                    text: String(parent.clipData.clipId || "")
                          + " revision " + Number(parent.clipData.revision || 0)
                }
                Component.onCompleted: root.delegateCreated += 1
                Component.onDestruction: root.delegateDestroyed += 1
            }
        }
    }
}
)QML";
  QQmlComponent harness_component(&engine);
  harness_component.setData(harness, QUrl::fromLocalFile(
      QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/TimelineVirtualClipRowsHarness.qml")));
  std::unique_ptr<QObject> harness_object(harness_component.create());
  if (!harness_object) return fail(harness_component.errorString());
  auto* root = qobject_cast<QQuickItem*>(harness_object.get());
  if (root == nullptr)
    return fail(QStringLiteral("The virtual clip row harness must be a QQuickItem."));
  root->setParentItem(window.contentItem());
  window.show();
  settle_qml();

  const auto initial = set_snapshot(
      root, model.get(),
      {QStringLiteral("clip-A"), QStringLiteral("clip-B"),
       QStringLiteral("clip-C"), QStringLiteral("clip-D")},
      {QStringLiteral("clip-B"), QStringLiteral("clip-C")}, 1);
  settle_qml();
  if (!initial.value(QStringLiteral("ok")).toBool() ||
      model->property("count").toInt() != 2 ||
      root->property("delegateCreated").toInt() != 2 ||
      root->property("delegateDestroyed").toInt() != 0) {
    return fail(QStringLiteral("The initial clip window should create one compact row per visible ID."));
  }
  QVariant roles_result;
  if (!QMetaObject::invokeMethod(
          root, "modelHasOnlyIdentityRoles", Q_RETURN_ARG(QVariant, roles_result)) ||
      (roles_result.canConvert<QJSValue>()
           ? !roles_result.value<QJSValue>().toBool()
           : !roles_result.toBool())) {
    return fail(QStringLiteral("The clip ListModel must store only clip ID and source index."));
  }

  auto* original_b = clip_item(root, QStringLiteral("clip-B"));
  auto* original_c = clip_item(root, QStringLiteral("clip-C"));
  if (original_b == nullptr || original_c == nullptr)
    return fail(QStringLiteral("Visible clip IDs should materialize exactly one real Repeater delegate."));
  const auto original_keyframes = original_c->property("clipData").toMap()
                                      .value(QStringLiteral("keyframes")).toList();
  if (original_keyframes.size() != 1204)
    return fail(QStringLiteral("The full keyframe array must remain accessible from the external source snapshot."));

  const auto same_snapshot = set_snapshot(
      root, model.get(),
      {QStringLiteral("clip-A"), QStringLiteral("clip-B"),
       QStringLiteral("clip-C"), QStringLiteral("clip-D")},
      {QStringLiteral("clip-B"), QStringLiteral("clip-C")}, 2);
  settle_qml();
  if (!same_snapshot.value(QStringLiteral("ok")).toBool() ||
      root->property("delegateCreated").toInt() != 2 ||
      root->property("delegateDestroyed").toInt() != 0 ||
      clip_item(root, QStringLiteral("clip-B")) != original_b ||
      clip_item(root, QStringLiteral("clip-C")) != original_c ||
      !clip_item_has_revision(original_b, QStringLiteral("clip-B"), 2) ||
      !clip_item_has_revision(original_c, QStringLiteral("clip-C"), 2)) {
    return fail(QStringLiteral("Refreshing native clip snapshots with unchanged IDs must retain delegates and expose current data."));
  }

  const auto window_shift = set_snapshot(
      root, model.get(),
      {QStringLiteral("clip-A"), QStringLiteral("clip-B"),
       QStringLiteral("clip-C"), QStringLiteral("clip-D")},
      {QStringLiteral("clip-C"), QStringLiteral("clip-D")}, 3);
  settle_qml();
  auto* original_d = clip_item(root, QStringLiteral("clip-D"));
  if (!window_shift.value(QStringLiteral("ok")).toBool() ||
      clip_item(root, QStringLiteral("clip-B")) != nullptr ||
      clip_item(root, QStringLiteral("clip-C")) != original_c || original_d == nullptr ||
      root->property("delegateCreated").toInt() != 3 ||
      root->property("delegateDestroyed").toInt() != 1) {
    return fail(QStringLiteral("A horizontal window shift must preserve overlapping delegates and replace only entering IDs."));
  }

  stage_snapshot(
      root, model.get(),
      {QStringLiteral("clip-X"), QStringLiteral("clip-A"),
       QStringLiteral("clip-B"), QStringLiteral("clip-C"), QStringLiteral("clip-D")},
      {QStringLiteral("clip-C"), QStringLiteral("clip-D")}, 4, false);
  const auto source_map_before_index_repair = original_c->property("clipData").toMap();
  if (source_map_before_index_repair.value(QStringLiteral("clipId")).toString() !=
          QStringLiteral("clip-C") ||
      source_map_before_index_repair.value(QStringLiteral("keyframes")).toList().size() != 1204) {
    return fail(QStringLiteral("Clip-ID lookup must retain the full external keyframe map while its source index is stale."));
  }
  const auto inserted_source = synchronize(model.get());
  settle_qml();
  if (!inserted_source.value(QStringLiteral("ok")).toBool() ||
      inserted_source.value(QStringLiteral("updatedRows")).toInt() != 2 ||
      inserted_source.value(QStringLiteral("structuralChanges")).toInt() != 0 ||
      clip_item(root, QStringLiteral("clip-C")) != original_c ||
      clip_item(root, QStringLiteral("clip-D")) != original_d ||
      original_c->property("sourceClipIndex").toInt() != 3 ||
      original_d->property("sourceClipIndex").toInt() != 4 ||
      !clip_item_has_revision(original_c, QStringLiteral("clip-C"), 4) ||
      !clip_item_has_revision(original_d, QStringLiteral("clip-D"), 4) ||
      original_c->property("clipData").toMap()
              .value(QStringLiteral("keyframes")).toList().size() != 1204) {
    return fail(QStringLiteral("Source insertion must repair indices while retaining delegates and external keyframes."));
  }

  const auto reordered_source = set_snapshot(
      root, model.get(),
      {QStringLiteral("clip-X"), QStringLiteral("clip-D"),
       QStringLiteral("clip-C"), QStringLiteral("clip-B"), QStringLiteral("clip-A")},
      {QStringLiteral("clip-D"), QStringLiteral("clip-C")}, 5);
  settle_qml();
  if (!reordered_source.value(QStringLiteral("ok")).toBool() ||
      reordered_source.value(QStringLiteral("structuralChanges")).toInt() != 1 ||
      clip_item(root, QStringLiteral("clip-D")) != original_d ||
      clip_item(root, QStringLiteral("clip-C")) != original_c ||
      original_d->property("sourceClipIndex").toInt() != 1 ||
      original_c->property("sourceClipIndex").toInt() != 2 ||
      root->property("delegateCreated").toInt() != 3 ||
      root->property("delegateDestroyed").toInt() != 1) {
    return fail(QStringLiteral("Source reorder must move retained IDs and repair indices in place."));
  }

  const auto synchronization_count_before_coalesced_update =
      model->property("synchronizationCount").toInt();
  stage_snapshot(
      root, model.get(),
      {QStringLiteral("clip-X"), QStringLiteral("clip-A"),
       QStringLiteral("clip-D"), QStringLiteral("clip-C"), QStringLiteral("clip-B")},
      {QStringLiteral("clip-D")}, 6, false);
  stage_snapshot(
      root, model.get(),
      {QStringLiteral("clip-X"), QStringLiteral("clip-B"),
       QStringLiteral("clip-D"), QStringLiteral("clip-C"), QStringLiteral("clip-A")},
      {QStringLiteral("clip-C"), QStringLiteral("clip-D")}, 7, false);
  if (!wait_until([&] { return model->property("synchronizationCount").toInt() >
                                synchronization_count_before_coalesced_update; }, 3000)) {
    return fail(QStringLiteral("Property changes should schedule a deferred clip-row synchronization."));
  }
  settle_qml();
  if (model->property("synchronizationCount").toInt() !=
          synchronization_count_before_coalesced_update + 1 ||
      model->property("count").toInt() != 2 ||
      clip_item(root, QStringLiteral("clip-C")) != original_c ||
      clip_item(root, QStringLiteral("clip-D")) != original_d ||
      original_c->property("sourceClipIndex").toInt() != 3 ||
      original_d->property("sourceClipIndex").toInt() != 2 ||
      !clip_item_has_revision(original_c, QStringLiteral("clip-C"), 7) ||
      !clip_item_has_revision(original_d, QStringLiteral("clip-D"), 7)) {
    return fail(QStringLiteral("Multiple snapshot changes in one event turn must coalesce to the latest indexed window."));
  }

  const auto suspended_result = set_snapshot(
      root, model.get(),
      {QStringLiteral("clip-X"), QStringLiteral("clip-D"),
       QStringLiteral("clip-C"), QStringLiteral("clip-B"),
       QStringLiteral("clip-A"), QStringLiteral("clip-E")},
      {QStringLiteral("clip-E")}, 8, true);
  settle_qml();
  if (!suspended_result.value(QStringLiteral("suspended")).toBool() ||
      model->property("count").toInt() != 2 ||
      clip_item(root, QStringLiteral("clip-D")) != original_d ||
      clip_item(root, QStringLiteral("clip-C")) != original_c) {
    return fail(QStringLiteral("Suspended virtualization updates must leave current delegates untouched."));
  }

  const auto synchronization_count_before_resume = model->property("synchronizationCount").toInt();
  model->setProperty("suspendUpdates", false);
  if (!wait_until([&] {
        return model->property("count").toInt() == 1 &&
            clip_item(root, QStringLiteral("clip-E")) != nullptr;
      }, 3000)) {
    return fail(QStringLiteral("Resuming clip virtualization must eventually apply the latest window."));
  }
  if (model->property("synchronizationCount").toInt() !=
          synchronization_count_before_resume + 1 ||
      clip_item(root, QStringLiteral("clip-D")) != nullptr ||
      clip_item(root, QStringLiteral("clip-C")) != nullptr ||
      clip_item(root, QStringLiteral("clip-E"))->property("sourceClipIndex").toInt() != 5) {
    return fail(QStringLiteral("The resumed window must expose the latest source index and ID set."));
  }

  window.close();
  if (!qml_warnings.isEmpty())
    return fail(QStringLiteral("Clip rows must update without QML errors: ") + qml_warnings.join(QLatin1Char('\n')));
  return EXIT_SUCCESS;
}
