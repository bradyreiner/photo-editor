#include "editor_preview_controller.h"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static void replace(EditorPreviewController& controller, const QVariantList& layers,
                      const QString& source, const QString& thumbnail = {}) {
    controller.asset_id_ = "snapshot-test";
    controller.file_path_ = source;
    static_cast<void>(controller.layer_session_.replaceLayers(layers));
    auto cached = layers;
    for (auto& entry : cached) {
      auto layer = entry.toMap();
      layer.insert("thumbnailSource", thumbnail);
      layer.insert("thumbnailReady", !thumbnail.isEmpty());
      entry = layer;
    }
    controller.thumbnail_cache_session_.photoLayersWithThumbnails() = cached;
    controller.thumbnail_cache_session_.photoLayerAssetKey() = "snapshot-test|" + source;
    controller.thumbnail_cache_session_.photoLayerDirty() = false;
    emit controller.controlsChanged();
  }
  static int reads(const EditorPreviewController& controller) {
    return controller.thumbnail_cache_session_.photoLayerHitCount();
  }
};
}

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/snapshot-test");
  QGuiApplication app(argc, argv);
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, runtime.filePath("preferences"));
  using namespace photo_editor::desktop;
  using Access = EditorPreviewControllerTestAccess;
  EditorPreviewController controller;
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import "file:)" PHOTO_EDITOR_SOURCE_DIR R"(/apps/desktop/qml/components"
Item {
    id: fixture
    required property var controller
    PhotoLayerSnapshot { id: snapshot; controller: fixture.controller }
    property var saved: []
    function capture() { saved = snapshot.rows }
    function savedName() { return saved.length ? String(saved[0].name) : "" }
    function summary() {
        return snapshot.rows.length ? snapshot.rows[0].name + "|" + snapshot.rows[0].opacity
                                      + "|" + snapshot.rows[0].thumbnailSource : "empty"
    }
    function consume() {
        let value = 0
        for (let i = 0; i < 1000; ++i)
            value += snapshot.rows[0].opacity
        return value
    }
})", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/photo-layer-snapshot-fixture.qml")));
  std::unique_ptr<QObject> object(component.createWithInitialProperties({{"controller", QVariant::fromValue(&controller)}}));
  if (!object) { std::cerr << component.errorString().toStdString(); return 1; }
  const auto call = [&](const char* method) {
    QVariant result;
    if (!QMetaObject::invokeMethod(object.get(), method, Q_RETURN_ARG(QVariant, result)))
      std::cerr << "Invocation failed: " << method << '\n';
    return result;
  };
  if (call("summary") != "empty") return 1;
  const auto source = runtime.filePath("base.png");
  QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::red);
  if (!image.save(source)) return 2;
  QVariantMap layer{{"id", "one"}, {"kind", "source"}, {"name", "Original"},
                    {"sourcePath", source}, {"opacity", 0.5}};
  Access::replace(controller, {layer}, source, "thumb:one");
  if (call("summary") != "Original|0.5|thumb:one") return 1;
  const auto before_reads = Access::reads(controller);
  if (call("consume").toDouble() != 500 || Access::reads(controller) != before_reads) {
    std::cerr << "Inspector field reads refetched the native presentation\n"; return 1;
  }
  call("capture");
  layer.insert("name", "Edited"); layer.insert("opacity", 0.25);
  Access::replace(controller, {layer}, source, "thumb:one");
  if (call("summary") != "Edited|0.25|thumb:one" || call("savedName") != "Original") return 1;
  Access::replace(controller, {layer}, source, "thumb:two");
  if (call("summary") != "Edited|0.25|thumb:two") return 1;
  auto second = layer; second.insert("id", "two"); second.insert("name", "Second");
  Access::replace(controller, {second, layer}, source, "thumb:two");
  if (call("summary") != "Second|0.25|thumb:two") return 1;
  Access::replace(controller, {}, source);
  if (call("summary") != "empty" || call("savedName") != "Original") return 1;
  std::cout << "Snapshot field reads reuse presentation; same-count edits, thumbnail completion, "
               "reordering and removal refresh; captured values remain detached.\n";
}
