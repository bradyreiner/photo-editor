#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QRectF>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <iostream>
#include <memory>

#ifndef PHOTO_EDITOR_SOURCE_DIR
#define PHOTO_EDITOR_SOURCE_DIR "."
#endif

class VectorPathFileTestController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString assetId READ assetId WRITE setAssetId NOTIFY stateChanged)
  Q_PROPERTY(QVariantList photoLayers READ photoLayers WRITE setPhotoLayers NOTIFY stateChanged)
  Q_PROPERTY(bool professionalToolsBusy READ professionalToolsBusy WRITE setProfessionalToolsBusy NOTIFY stateChanged)

 public:
  explicit VectorPathFileTestController(QObject* parent = nullptr)
      : QObject(parent), asset_id_(QStringLiteral("asset-a")),
        layers_({QVariantMap{{QStringLiteral("id"), QStringLiteral("path-layer-a")},
                             {QStringLiteral("vectorPath"), QVariantMap{{QStringLiteral("schema"), QStringLiteral("photo-vector-path")}}}}}) {}

  [[nodiscard]] QString assetId() const { return asset_id_; }
  [[nodiscard]] QVariantList photoLayers() const { return layers_; }
  [[nodiscard]] bool professionalToolsBusy() const { return busy_; }
  [[nodiscard]] int imports() const { return imports_; }
  [[nodiscard]] int exports() const { return exports_; }
  [[nodiscard]] QString lastAssetId() const { return last_asset_id_; }
  [[nodiscard]] QString lastLayerId() const { return last_layer_id_; }
  [[nodiscard]] QString lastPath() const { return last_path_; }
  void setImportAdmission(bool admitted) { import_admission_ = admitted; }
  void setExportAdmission(bool admitted) { export_admission_ = admitted; }

  void setAssetId(const QString& assetId) {
    if (asset_id_ == assetId) return;
    asset_id_ = assetId;
    emit stateChanged();
  }

  void setPhotoLayers(const QVariantList& layers) {
    layers_ = layers;
    emit stateChanged();
  }

  void setProfessionalToolsBusy(bool busy) {
    if (busy_ == busy) return;
    busy_ = busy;
    emit stateChanged();
  }

  Q_INVOKABLE bool importPhotoVectorSvg(const QString& assetId, const QString& path) {
    ++imports_;
    last_asset_id_ = assetId;
    last_layer_id_.clear();
    last_path_ = path;
    return import_admission_;
  }

  Q_INVOKABLE bool exportPhotoVectorSvg(const QString& assetId, const QString& layerId,
                                        const QString& path) {
    ++exports_;
    last_asset_id_ = assetId;
    last_layer_id_ = layerId;
    last_path_ = path;
    return export_admission_;
  }

 signals:
  void stateChanged();

 private:
  QString asset_id_;
  QVariantList layers_;
  bool busy_ = false;
  bool import_admission_ = true;
  bool export_admission_ = true;
  int imports_ = 0;
  int exports_ = 0;
  QString last_asset_id_;
  QString last_layer_id_;
  QString last_path_;
};

namespace {

void settle(int milliseconds = 70) {
  QTest::qWait(milliseconds);
}

QQuickItem* find_visual(QQuickItem* parent, const QString& name) {
  if (parent == nullptr) return nullptr;
  if (parent->objectName() == name) return parent;
  for (auto* child : parent->childItems())
    if (auto* match = find_visual(child, name)) return match;
  return nullptr;
}

QQuickItem* find_item(QQuickWindow* window, const QString& name) {
  if (window == nullptr) return nullptr;
  if (auto* item = find_visual(window->contentItem(), name)) return item;
  return window->findChild<QQuickItem*>(name);
}

bool inside_item(QQuickItem* parent, QQuickItem* child) {
  if (parent == nullptr || child == nullptr) return false;
  const auto point = child->mapToItem(parent, QPointF(0.0, 0.0));
  const QRectF rect(point, child->size());
  return parent->boundingRect().contains(rect);
}

bool click_item(QQuickWindow* window, const QString& name) {
  auto* item = find_item(window, name);
  if (window == nullptr || item == nullptr || !item->isVisible() || !item->isEnabled()) {
    std::cerr << "Unavailable control: " << name.toStdString() << '\n';
    return false;
  }
  const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
  settle();
  return true;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  VectorPathFileTestController controller;
  QTemporaryDir picker_files;
  if (!picker_files.isValid()) return fail("Create temporary SVG picker fixtures");
  const auto import_path = picker_files.filePath(QStringLiteral("editable.svg"));
  const QByteArray svg_fixture("<svg viewBox='0 0 1 1'/>");
  QFile import_fixture(import_path);
  if (!import_fixture.open(QIODevice::WriteOnly) ||
      import_fixture.write(svg_fixture) != svg_fixture.size()) {
    return fail("Write a temporary SVG file for the production import picker");
  }
  import_fixture.close();

  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("vectorPathFileController"),
                                           &controller);
  const auto components_path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
      .filePath(QStringLiteral("apps/desktop/qml/components"));
  const auto components_import = QUrl::fromLocalFile(components_path).toString();
  const auto fixture_source = QStringLiteral(R"qml(import QtQuick
import "%1" as Components
Window {
    id: host
    visible: true
    width: 340
    height: 250
    color: "#171717"
    Components.PhotoVectorPathFileControls {
        id: panel
        objectName: "photoVectorPathFileControls"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: implicitHeight
        controller: vectorPathFileController
        layerIndex: vectorPathFileController.photoLayers.length > 0 ? 0 : -1
        layerData: vectorPathFileController.photoLayers.length > 0
                   ? vectorPathFileController.photoLayers[0] : ({})
    }
}
)qml").arg(components_import);
  QQmlComponent fixture_component(&engine);
  fixture_component.setData(fixture_source.toUtf8(),
      QUrl::fromLocalFile(QDir::temp().filePath(
          QStringLiteral("photo-vector-path-file-ui-fixture.qml"))));
  std::unique_ptr<QObject> fixture(fixture_component.create());
  if (!fixture) {
    std::cerr << fixture_component.errorString().toStdString() << '\n';
    return fail("Instantiate production SVG path file controls in QQuickWindow");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* panel = find_item(window, QStringLiteral("photoVectorPathFileControls"));
  auto* picker_status = find_item(window, QStringLiteral("photoVectorPathFilePickerStatus"));
  if (window == nullptr || panel == nullptr)
    return fail("Find SVG path file controls in the production QML component");
  if (picker_status == nullptr)
    return fail("Find SVG picker admission status in the production QML component");
  settle(120);

  for (const auto& name : {QStringLiteral("photoVectorPathImportButton"),
                           QStringLiteral("photoVectorPathExportButton")}) {
    auto* button = find_item(window, name);
    if (button == nullptr || !button->isVisible() || !button->isEnabled() ||
        !inside_item(panel, button)) {
      std::cerr << "Missing or clipped SVG action: " << name.toStdString() << '\n';
      return fail("Show import and export actions in the path inspector");
    }
  }
  auto* export_button = find_item(window, QStringLiteral("photoVectorPathExportButton"));
  const auto export_tooltip = export_button->property("tooltip").toString();
  if (!export_tooltip.contains(QStringLiteral("Masks")) ||
      !export_tooltip.contains(QStringLiteral("placement"))) {
    return fail("Disclose that SVG export includes only path geometry and solid fill/stroke");
  }

  if (!click_item(window, QStringLiteral("photoVectorPathImportButton")))
    return fail("Open the production SVG import picker from a pointer click");
  auto* import_dialog = fixture->findChild<QObject*>(QStringLiteral("photoVectorPathImportDialog"));
  if (import_dialog == nullptr) return fail("Find the production SVG import picker");
  import_dialog->setProperty("selectedFile", QUrl::fromLocalFile(import_path));
  const bool accepted_import = QMetaObject::invokeMethod(import_dialog, "accepted",
                                                          Qt::DirectConnection);
  if (!accepted_import || controller.imports() != 1 ||
      controller.lastAssetId() != QStringLiteral("asset-a") ||
      controller.lastPath() != import_path) {
    std::cerr << "accepted=" << accepted_import
              << " imports=" << controller.imports()
              << " asset=" << controller.lastAssetId().toStdString()
              << " path=" << controller.lastPath().toStdString()
              << " selected=" << import_dialog->property("selectedFile").toUrl().toString().toStdString()
              << '\n';
    return fail("Import picker dispatches the selected local file to the captured asset");
  }
  import_dialog->setProperty("visible", false);
  settle();

  const auto imports_before_rejected_admission = controller.imports();
  if (!click_item(window, QStringLiteral("photoVectorPathImportButton")))
    return fail("Open SVG import to verify synchronous backend rejection");
  controller.setImportAdmission(false);
  QVariant rejected_import_result;
  const bool invoked_rejected_import = QMetaObject::invokeMethod(
      panel, "acceptImport", Qt::DirectConnection,
      Q_RETURN_ARG(QVariant, rejected_import_result),
      Q_ARG(QVariant, QVariant::fromValue(QUrl::fromLocalFile(import_path))));
  if (!invoked_rejected_import || rejected_import_result.toBool() ||
      controller.imports() != imports_before_rejected_admission + 1 ||
      !picker_status->isVisible() ||
      !panel->property("pickerStatus").toString().contains(QStringLiteral("could not start"))) {
    return fail("Rejected import admission remains unaccepted and shows an inline reason");
  }
  import_dialog->setProperty("visible", false);
  controller.setImportAdmission(true);
  settle();

  const auto imports_before_stale_asset = controller.imports();
  if (!click_item(window, QStringLiteral("photoVectorPathImportButton")))
    return fail("Reopen the production SVG import picker");
  import_dialog->setProperty("selectedFile", QUrl::fromLocalFile(import_path));
  controller.setAssetId(QStringLiteral("asset-b"));
  if (!QMetaObject::invokeMethod(import_dialog, "accepted", Qt::DirectConnection) ||
      controller.imports() != imports_before_stale_asset) {
    return fail("Stale SVG import picker cannot dispatch into a different asset");
  }
  import_dialog->setProperty("visible", false);
  controller.setAssetId(QStringLiteral("asset-a"));
  settle();

  const auto imports_before_removed_layer = controller.imports();
  if (!click_item(window, QStringLiteral("photoVectorPathImportButton")))
    return fail("Reopen SVG import to verify the captured layer anchor");
  import_dialog->setProperty("selectedFile", QUrl::fromLocalFile(import_path));
  controller.setPhotoLayers({});
  if (!QMetaObject::invokeMethod(import_dialog, "accepted", Qt::DirectConnection) ||
      controller.imports() != imports_before_removed_layer) {
    return fail("Stale SVG import picker cannot dispatch after its captured layer is removed");
  }
  import_dialog->setProperty("visible", false);
  settle();

  const auto imports_before_empty_document = controller.imports();
  auto* empty_document_import = find_item(window, QStringLiteral("photoVectorPathImportButton"));
  if (empty_document_import == nullptr || !empty_document_import->isEnabled() ||
      panel->property("layerIndex").toInt() != -1 ||
      !click_item(window, QStringLiteral("photoVectorPathImportButton"))) {
    return fail("Enable SVG import for an empty document without a selected layer");
  }
  import_dialog->setProperty("selectedFile", QUrl::fromLocalFile(import_path));
  if (!QMetaObject::invokeMethod(import_dialog, "accepted", Qt::DirectConnection) ||
      controller.imports() != imports_before_empty_document + 1 ||
      controller.lastAssetId() != QStringLiteral("asset-a") ||
      controller.lastPath() != import_path) {
    return fail("Empty-document SVG picker dispatches using the asset anchor alone");
  }
  import_dialog->setProperty("visible", false);
  controller.setPhotoLayers({QVariantMap{
      {QStringLiteral("id"), QStringLiteral("path-layer-a")},
      {QStringLiteral("vectorPath"), QVariantMap{{QStringLiteral("schema"), QStringLiteral("photo-vector-path")}}}}});
  settle();

  const auto export_path = QDir::temp().filePath(QStringLiteral("editable-export.svg"));
  if (!click_item(window, QStringLiteral("photoVectorPathExportButton")))
    return fail("Open the production SVG export picker from a pointer click");
  auto* export_dialog = fixture->findChild<QObject*>(QStringLiteral("photoVectorPathExportDialog"));
  if (export_dialog == nullptr) return fail("Find the production SVG export picker");
  export_dialog->setProperty("selectedFile", QUrl::fromLocalFile(export_path));
  if (!QMetaObject::invokeMethod(export_dialog, "accepted", Qt::DirectConnection) ||
      controller.exports() != 1 || controller.lastAssetId() != QStringLiteral("asset-a") ||
      controller.lastLayerId() != QStringLiteral("path-layer-a") ||
      controller.lastPath() != export_path) {
    return fail("Export picker resolves the captured layer ID and dispatches the selected local file");
  }
  export_dialog->setProperty("visible", false);
  settle();

  const auto exports_before_rejected_admission = controller.exports();
  if (!click_item(window, QStringLiteral("photoVectorPathExportButton")))
    return fail("Open SVG export to verify synchronous backend rejection");
  controller.setExportAdmission(false);
  QVariant rejected_export_result;
  const bool invoked_rejected_export = QMetaObject::invokeMethod(
      panel, "acceptExport", Qt::DirectConnection,
      Q_RETURN_ARG(QVariant, rejected_export_result),
      Q_ARG(QVariant, QVariant::fromValue(QUrl::fromLocalFile(export_path))));
  if (!invoked_rejected_export || rejected_export_result.toBool() ||
      controller.exports() != exports_before_rejected_admission + 1 ||
      !picker_status->isVisible() ||
      !panel->property("pickerStatus").toString().contains(QStringLiteral("could not start"))) {
    return fail("Rejected export admission remains unaccepted and shows an inline reason");
  }
  export_dialog->setProperty("visible", false);
  controller.setExportAdmission(true);
  settle();

  const auto exports_before_stale_asset = controller.exports();
  if (!click_item(window, QStringLiteral("photoVectorPathExportButton")))
    return fail("Reopen the production SVG export picker");
  export_dialog->setProperty("selectedFile", QUrl::fromLocalFile(export_path));
  controller.setAssetId(QStringLiteral("asset-b"));
  if (!QMetaObject::invokeMethod(export_dialog, "accepted", Qt::DirectConnection) ||
      controller.exports() != exports_before_stale_asset) {
    return fail("Stale SVG export picker cannot dispatch into a different asset");
  }
  export_dialog->setProperty("visible", false);
  controller.setAssetId(QStringLiteral("asset-a"));
  settle();

  const auto exports_before_removed_layer = controller.exports();
  if (!click_item(window, QStringLiteral("photoVectorPathExportButton")))
    return fail("Reopen SVG export to verify the captured layer identity");
  export_dialog->setProperty("selectedFile", QUrl::fromLocalFile(export_path));
  controller.setPhotoLayers({QVariantMap{
      {QStringLiteral("id"), QStringLiteral("replacement-layer")},
      {QStringLiteral("vectorPath"), QVariantMap{{QStringLiteral("schema"), QStringLiteral("photo-vector-path")}}}}});
  if (!QMetaObject::invokeMethod(export_dialog, "accepted", Qt::DirectConnection) ||
      controller.exports() != exports_before_removed_layer) {
    return fail("Stale SVG export picker cannot dispatch against a replacement row at the same index");
  }
  export_dialog->setProperty("visible", false);
  controller.setPhotoLayers({QVariantMap{
      {QStringLiteral("id"), QStringLiteral("path-layer-a")},
      {QStringLiteral("vectorPath"), QVariantMap{{QStringLiteral("schema"), QStringLiteral("photo-vector-path")}}}}});
  settle();

  // A read-only export remains available on locked artwork, and the action
  // flow wraps within the compact inspector width without clipping controls.
  panel->setProperty("locked", true);
  window->setWidth(270);
  settle();
  export_button = find_item(window, QStringLiteral("photoVectorPathExportButton"));
  auto* import_button = find_item(window, QStringLiteral("photoVectorPathImportButton"));
  if (export_button == nullptr || import_button == nullptr ||
      !export_button->isEnabled() || !inside_item(panel, export_button) ||
      !inside_item(panel, import_button)) {
    return fail("Keep path interchange controls available and contained at compact width");
  }

  const auto arguments = app.arguments();
  if (arguments.size() > 1) {
    const QFileInfo capture_info(arguments.at(1));
    if (!QDir().mkpath(capture_info.absolutePath()))
      return fail("Create the optional UI capture directory");
    const auto capture = window->grabWindow();
    if (capture.isNull() || !capture.save(capture_info.absoluteFilePath()))
      return fail("Save the optional compact SVG controls capture");
  }

  std::cout << "photo_vector_path_file_ui_test passed\n";
  return 0;
}

#include "photo_vector_path_file_ui_test.moc"
