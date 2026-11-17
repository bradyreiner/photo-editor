#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
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

class SmartObjectTestController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString assetId READ assetId NOTIFY stateChanged)
  Q_PROPERTY(bool professionalToolsBusy READ professionalToolsBusy NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap layerData READ layerData NOTIFY stateChanged)
  Q_PROPERTY(QVariantList photoLayers READ photoLayers NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap smartObjectLinkStatuses READ smartObjectLinkStatuses NOTIFY stateChanged)

 public:
  explicit SmartObjectTestController(QObject* parent = nullptr)
      : QObject(parent),
        asset_id_(QStringLiteral("test-asset")),
        layer_data_{{QStringLiteral("id"), QStringLiteral("source-layer")},
                    {QStringLiteral("kind"), QStringLiteral("source")},
                    {QStringLiteral("name"), QStringLiteral("Source")},
                    {QStringLiteral("sourcePath"), QStringLiteral("/tmp/source.png")}} {}

  [[nodiscard]] bool professionalToolsBusy() const { return busy_; }
  [[nodiscard]] QString assetId() const { return asset_id_; }
  [[nodiscard]] QVariantMap layerData() const { return layer_data_; }
  [[nodiscard]] QVariantList photoLayers() const { return {layer_data_}; }
  [[nodiscard]] QVariantMap smartObjectLinkStatuses() const { return link_statuses_; }
  [[nodiscard]] int convertCalls() const { return convert_calls_; }
  [[nodiscard]] int duplicateCalls() const { return duplicate_calls_; }
  [[nodiscard]] int independentCalls() const { return independent_calls_; }
  [[nodiscard]] int checkLinkCalls() const { return check_link_calls_; }
  [[nodiscard]] int refreshCalls() const { return refresh_calls_; }
  [[nodiscard]] int relinkCalls() const { return relink_calls_; }
  [[nodiscard]] int embedCalls() const { return embed_calls_; }
  [[nodiscard]] int replaceCalls() const { return replace_calls_; }
  [[nodiscard]] int lastIndex() const { return last_index_; }
  [[nodiscard]] bool lastLinked() const { return last_linked_; }
  [[nodiscard]] QString lastRelinkPath() const { return last_relink_path_; }
  [[nodiscard]] QString lastReplacePath() const { return last_replace_path_; }

  void setAssetId(QString asset_id) {
    if (asset_id_ == asset_id) return;
    asset_id_ = std::move(asset_id);
    emit stateChanged();
  }

  void setBusy(bool busy) {
    if (busy_ == busy) return;
    busy_ = busy;
    emit stateChanged();
  }

  void setLayerData(QVariantMap layer) {
    layer_data_ = std::move(layer);
    link_statuses_.clear();
    emit stateChanged();
  }

  Q_INVOKABLE bool checkPhotoSmartObjectLink(int index) {
    ++check_link_calls_;
    last_index_ = index;
    if (index != 0 || layer_data_.value(QStringLiteral("smartObject")).toMap()
                           .value(QStringLiteral("policy")).toString() != QStringLiteral("linked")) {
      return false;
    }
    link_statuses_.insert(layer_data_.value(QStringLiteral("id")).toString(),
                          QVariantMap{{QStringLiteral("status"), QStringLiteral("current")}});
    emit stateChanged();
    return true;
  }

  Q_INVOKABLE bool refreshPhotoSmartObjectContents(int index) {
    ++refresh_calls_;
    last_index_ = index;
    return index == 0 && layer_data_.value(QStringLiteral("smartObject")).toMap()
                            .value(QStringLiteral("policy")).toString() == QStringLiteral("linked");
  }

  Q_INVOKABLE bool relinkPhotoSmartObjectContents(int index, const QString& path) {
    ++relink_calls_;
    last_index_ = index;
    last_relink_path_ = path;
    return index == 0 && !path.isEmpty();
  }

  Q_INVOKABLE bool embedPhotoSmartObjectContents(int index) {
    ++embed_calls_;
    last_index_ = index;
    if (index != 0 || layer_data_.value(QStringLiteral("smartObject")).toMap()
                           .value(QStringLiteral("policy")).toString() != QStringLiteral("linked")) {
      return false;
    }
    auto descriptor = layer_data_.value(QStringLiteral("smartObject")).toMap();
    descriptor.insert(QStringLiteral("policy"), QStringLiteral("embedded"));
    descriptor.remove(QStringLiteral("sourcePath"));
    layer_data_.insert(QStringLiteral("smartObject"), descriptor);
    link_statuses_.clear();
    emit stateChanged();
    return true;
  }

  Q_INVOKABLE bool convertPhotoLayerToSmartObject(int index, bool linked = false) {
    ++convert_calls_;
    last_index_ = index;
    last_linked_ = linked;
    if (index != 0) return false;
    layer_data_.insert(QStringLiteral("smartObject"), QVariantMap{
        {QStringLiteral("schema"), QStringLiteral("photo-smart-object-source")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("policy"), linked ? QStringLiteral("linked")
                                          : QStringLiteral("embedded")},
        {QStringLiteral("contentId"), QStringLiteral("shared-content")}});
    emit stateChanged();
    return true;
  }

  Q_INVOKABLE bool duplicatePhotoSmartObject(int index) {
    ++duplicate_calls_;
    last_index_ = index;
    return index == 0 && layer_data_.contains(QStringLiteral("smartObject"));
  }

  Q_INVOKABLE bool makePhotoSmartObjectIndependent(int index) {
    ++independent_calls_;
    last_index_ = index;
    return index == 0 && layer_data_.contains(QStringLiteral("smartObject"));
  }

  Q_INVOKABLE bool replacePhotoSmartObjectContents(int index, const QString& path) {
    ++replace_calls_;
    last_index_ = index;
    last_replace_path_ = path;
    return index == 0 && layer_data_.contains(QStringLiteral("smartObject")) && !path.isEmpty();
  }

 signals:
  void stateChanged();

 private:
  QString asset_id_;
  QVariantMap layer_data_;
  QVariantMap link_statuses_;
  bool busy_ = false;
  int convert_calls_ = 0;
  int duplicate_calls_ = 0;
  int independent_calls_ = 0;
  int check_link_calls_ = 0;
  int refresh_calls_ = 0;
  int relink_calls_ = 0;
  int embed_calls_ = 0;
  int replace_calls_ = 0;
  int last_index_ = -1;
  bool last_linked_ = false;
  QString last_relink_path_;
  QString last_replace_path_;
};

namespace {

void settle(int milliseconds = 60) {
  QTest::qWait(milliseconds);
}

QQuickItem* find_visual(QQuickItem* parent, const QString& name) {
  if (parent == nullptr) return nullptr;
  if (parent->objectName() == name) return parent;
  for (auto* child : parent->childItems()) {
    if (auto* match = find_visual(child, name)) return match;
  }
  return nullptr;
}

QQuickItem* find_item(QQuickWindow* window, const QString& name) {
  if (window == nullptr) return nullptr;
  if (auto* item = find_visual(window->contentItem(), name)) return item;
  return window->findChild<QQuickItem*>(name);
}

bool click_item(QQuickWindow* window, const QString& name) {
  auto* item = find_item(window, name);
  if (window == nullptr || item == nullptr || !item->isVisible() ||
      !item->isEnabled()) {
    std::cerr << "Unavailable control: " << name.toStdString() << '\n';
    return false;
  }
  const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
  settle();
  return true;
}

void click_even_if_disabled(QQuickWindow* window, QQuickItem* item) {
  if (window == nullptr || item == nullptr) return;
  const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
  settle();
}

bool inside_item(QQuickItem* bounds, QQuickItem* item, double epsilon = 1.0) {
  if (bounds == nullptr || item == nullptr || !item->isVisible()) return false;
  const auto rect = item->mapRectToItem(bounds, item->boundingRect());
  return rect.left() >= -epsilon && rect.top() >= -epsilon &&
      rect.right() <= bounds->width() + epsilon &&
      rect.bottom() <= bounds->height() + epsilon;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("Create the relink picker fixture directory");
  const auto relink_source_path = temporary.filePath(QStringLiteral("relinked-source.png"));
  QFile relink_source_file(relink_source_path);
  if (!relink_source_file.open(QIODevice::WriteOnly) ||
      relink_source_file.write("source") != 6) {
    return fail("Create a valid file for the relink picker");
  }
  relink_source_file.close();
  SmartObjectTestController controller;

  QQmlEngine engine;
  QQmlComponent settings_component(&engine);
  settings_component.setData(R"qml(import QtQml
QtObject {
    property real interfaceScale: 1
    property string interfaceDensity: "balanced"
    property string systemUiFontFamily: ""
    property bool reducedMotion: true
    property bool highContrast: false
    property string colorVisionMode: "standard"
})qml", QUrl());
  std::unique_ptr<QObject> settings(settings_component.create());
  if (!settings) {
    std::cerr << settings_component.errorString().toStdString() << '\n';
    return fail("Create application theme settings");
  }
  settings->setProperty("systemUiFontFamily", app.font().family());
  engine.rootContext()->setContextProperty("appShell", settings.get());
  engine.rootContext()->setContextProperty("smartObjectController", &controller);

  QStringList warnings;
  QObject::connect(&engine, &QQmlEngine::warnings,
      [&warnings](const QList<QQmlError>& errors) {
        for (const auto& error : errors) warnings.append(error.toString());
      });

  const auto components_path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
      .filePath(QStringLiteral("apps/desktop/qml/components"));
  const auto components_import = QUrl::fromLocalFile(components_path).toString();
  const auto fixture_source = QStringLiteral(R"qml(import QtQuick
import "%1" as Components
Window {
    id: host
    objectName: "photoSmartObjectUiHost"
    visible: true
    width: 440
    height: 480
    color: "#171717"
    property bool layerLocked: false
    Components.PhotoSmartObjectControls {
        id: panel
        objectName: "photoSmartObjectControls"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: implicitHeight
        controller: smartObjectController
        layerIndex: 0
        layerData: smartObjectController.layerData
        locked: host.layerLocked
    }
}
)qml").arg(components_import);
  QQmlComponent fixture_component(&engine);
  fixture_component.setData(fixture_source.toUtf8(),
      QUrl::fromLocalFile(QDir::temp().filePath(
          QStringLiteral("photo-smart-object-ui-fixture.qml"))));
  std::unique_ptr<QObject> fixture(fixture_component.create());
  if (!fixture) {
    std::cerr << fixture_component.errorString().toStdString() << '\n';
    return fail("Instantiate the production Smart Object control in QQuickWindow");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* panel = find_item(window, QStringLiteral("photoSmartObjectControls"));
  if (window == nullptr || panel == nullptr) {
    return fail("Find the production Smart Object QQuickWindow and panel");
  }
  settle(120);

  auto* create_primary = find_item(
      window, QStringLiteral("photoSmartObjectCreateButtonPrimaryAction"));
  if (!panel->property("canConvert").toBool() || create_primary == nullptr ||
      !create_primary->isVisible() || !create_primary->isEnabled()) {
    return fail("Source layers expose the production embed and link create menu");
  }

  // A busy or locked source cannot start conversion through the visible menu.
  controller.setBusy(true);
  settle();
  if (create_primary->isEnabled()) {
    return fail("Professional-tools busy state disables Smart Object conversion");
  }
  const auto conversions_before_busy = controller.convertCalls();
  click_even_if_disabled(window, create_primary);
  if (controller.convertCalls() != conversions_before_busy) {
    return fail("Busy conversion click must not reach the controller");
  }
  controller.setBusy(false);
  fixture->setProperty("layerLocked", true);
  settle();
  if (create_primary->isEnabled()) {
    return fail("Locked source disables Smart Object conversion");
  }
  const auto conversions_before_lock = controller.convertCalls();
  click_even_if_disabled(window, create_primary);
  if (controller.convertCalls() != conversions_before_lock) {
    return fail("Locked conversion click must not reach the controller");
  }
  fixture->setProperty("layerLocked", false);
  settle();

  if (!click_item(window,
          QStringLiteral("photoSmartObjectCreateButtonPrimaryAction"))) {
    return fail("Open the production Smart Object creation menu");
  }
  auto* embed_option = find_item(
      window, QStringLiteral("photoSmartObjectCreateButtonPopupItem0"));
  auto* link_option = find_item(
      window, QStringLiteral("photoSmartObjectCreateButtonPopupItem1"));
  if (embed_option == nullptr || link_option == nullptr ||
      !embed_option->isVisible() || !link_option->isVisible() ||
      !embed_option->isEnabled() || !link_option->isEnabled()) {
    return fail("Source create menu enables both embedded and linked policies");
  }
  if (!click_item(window,
          QStringLiteral("photoSmartObjectCreateButtonPopupItem0")) ||
      controller.convertCalls() != 1 || controller.lastIndex() != 0 ||
      controller.lastLinked() ||
      controller.layerData().value(QStringLiteral("smartObject")).toMap()
              .value(QStringLiteral("policy")).toString() !=
          QStringLiteral("embedded")) {
    return fail("Embed source menu action converts the selected source with linked=false");
  }

  // Shared duplication and Make Independent are separate production actions.
  for (const auto& button_name : {
           QStringLiteral("photoSmartObjectDuplicateButton"),
           QStringLiteral("photoSmartObjectIndependentButton"),
           QStringLiteral("photoSmartObjectReplaceButton")}) {
    auto* button = find_item(window, button_name);
    if (button == nullptr || !button->isVisible() || !button->isEnabled()) {
      std::cerr << "Missing Smart Object action: " << button_name.toStdString() << '\n';
      return fail("Embedded Smart Object exposes shared duplicate, independent and replace actions");
    }
  }
  if (!click_item(window, QStringLiteral("photoSmartObjectDuplicateButton")) ||
      controller.duplicateCalls() != 1 || controller.lastIndex() != 0 ||
      !click_item(window, QStringLiteral("photoSmartObjectIndependentButton")) ||
      controller.independentCalls() != 1 || controller.lastIndex() != 0) {
    return fail("Shared duplicate and Make Independent dispatch the selected layer index");
  }

  // A recipe switch can retain the same layer ID. An open Replace picker must
  // not apply its selection to that unrelated layer in the new asset.
  const auto replacements_before_stale_picker = controller.replaceCalls();
  if (!click_item(window, QStringLiteral("photoSmartObjectReplaceButton"))) {
    return fail("Open the production Smart Object replacement picker");
  }
  auto* replacement_dialog = fixture->findChild<QObject*>(
      QStringLiteral("photoSmartObjectReplaceDialog"));
  if (replacement_dialog == nullptr) return fail("Find the production replacement dialog");
  replacement_dialog->setProperty("selectedFile", QUrl::fromLocalFile(relink_source_path));
  controller.setAssetId(QStringLiteral("other-replace-asset"));
  if (!QMetaObject::invokeMethod(replacement_dialog, "accepted", Qt::DirectConnection) ||
      controller.replaceCalls() != replacements_before_stale_picker ||
      controller.layerData().value(QStringLiteral("id")).toString() != QStringLiteral("source-layer")) {
    return fail("Stale Replace picker acceptance must not dispatch when the same layer ID belongs to another asset");
  }
  replacement_dialog->setProperty("visible", false);
  controller.setAssetId(QStringLiteral("test-asset"));
  settle();

  // Reopen the create menu for a source and choose linked content.
  controller.setLayerData(QVariantMap{
      {QStringLiteral("id"), QStringLiteral("source-layer")},
      {QStringLiteral("kind"), QStringLiteral("source")},
      {QStringLiteral("name"), QStringLiteral("Source")},
      {QStringLiteral("sourcePath"), QStringLiteral("/tmp/source.png")}});
  settle();
  if (!click_item(window,
          QStringLiteral("photoSmartObjectCreateButtonPrimaryAction")) ||
      !click_item(window,
          QStringLiteral("photoSmartObjectCreateButtonPopupItem1")) ||
      controller.convertCalls() != 2 || controller.lastIndex() != 0 ||
      !controller.lastLinked() ||
      controller.layerData().value(QStringLiteral("smartObject")).toMap()
              .value(QStringLiteral("policy")).toString() !=
          QStringLiteral("linked")) {
    return fail("Keep file linked menu action converts the selected source with linked=true");
  }
  settle();
  auto* link_status_label = find_item(window,
      QStringLiteral("photoSmartObjectLinkStatus"));
  if (controller.checkLinkCalls() != 1 || link_status_label == nullptr ||
      link_status_label->property("text").toString() != QStringLiteral("Link is current")) {
    return fail("Linked Smart Objects check their source asynchronously and show current status");
  }
  auto* source_summary = find_item(window,
      QStringLiteral("photoSmartObjectSourceSummary"));
  if (source_summary == nullptr ||
      !source_summary->property("text").toString().contains(QStringLiteral("source.png"))) {
    return fail("Linked Smart Objects show their source filename and policy");
  }
  for (const auto& action : {
           QStringLiteral("photoSmartObjectRelinkButton"),
           QStringLiteral("photoSmartObjectRefreshButton"),
           QStringLiteral("photoSmartObjectEmbedButton"),
           QStringLiteral("photoSmartObjectCheckLinkButton")}) {
    auto* button = find_item(window, action);
    if (button == nullptr || !button->isVisible() || !button->isEnabled()) {
      std::cerr << "Missing linked Smart Object action: " << action.toStdString() << '\n';
      return fail("Linked Smart Objects expose relink, update, embed and check actions");
    }
  }
  fixture->setProperty("layerLocked", true);
  settle();
  auto* locked_check = find_item(window,
      QStringLiteral("photoSmartObjectCheckLinkButton"));
  auto* locked_refresh = find_item(window,
      QStringLiteral("photoSmartObjectRefreshButton"));
  const auto checks_before_locked_read = controller.checkLinkCalls();
  if (locked_check == nullptr || !locked_check->isEnabled() ||
      locked_refresh == nullptr || locked_refresh->isEnabled() ||
      !click_item(window, QStringLiteral("photoSmartObjectCheckLinkButton")) ||
      controller.checkLinkCalls() != checks_before_locked_read + 1) {
    return fail("Read-only link checks stay available when a Smart Object is locked");
  }
  fixture->setProperty("layerLocked", false);
  settle();
  const auto checks_before_retry = controller.checkLinkCalls();
  if (!click_item(window, QStringLiteral("photoSmartObjectCheckLinkButton")) ||
      controller.checkLinkCalls() != checks_before_retry + 1 ||
      !click_item(window, QStringLiteral("photoSmartObjectRefreshButton")) ||
      controller.refreshCalls() != 1 || controller.lastIndex() != 0) {
    return fail("Check link and Update dispatch to their linked-source controller methods");
  }

  // The relink dialog keeps the target by stable layer ID while it is open.
  if (!click_item(window, QStringLiteral("photoSmartObjectRelinkButton"))) {
    return fail("Open the production Smart Object relink picker");
  }
  auto* relink_dialog = fixture->findChild<QObject*>(QStringLiteral("photoSmartObjectRelinkDialog"));
  if (relink_dialog == nullptr) return fail("Find the production Smart Object relink dialog");
  const auto relink_path = QUrl::fromLocalFile(relink_source_path);
  relink_dialog->setProperty("selectedFile", relink_path);
  if (!QMetaObject::invokeMethod(relink_dialog, "accepted", Qt::DirectConnection) ||
      controller.relinkCalls() != 1 || controller.lastIndex() != 0 ||
      !controller.lastRelinkPath().contains(QStringLiteral("relinked-source.png"))) {
    return fail("Relink picker acceptance dispatches the chosen file to the current layer ID");
  }
  relink_dialog->setProperty("visible", false);
  settle();
  const auto relinks_before_stale_picker = controller.relinkCalls();
  if (!click_item(window, QStringLiteral("photoSmartObjectRelinkButton"))) {
    return fail("Reopen the production Smart Object relink picker");
  }
  relink_dialog->setProperty("selectedFile", relink_path);
  controller.setAssetId(QStringLiteral("other-relink-asset"));
  if (!QMetaObject::invokeMethod(relink_dialog, "accepted", Qt::DirectConnection) ||
      controller.relinkCalls() != relinks_before_stale_picker ||
      controller.layerData().value(QStringLiteral("id")).toString() != QStringLiteral("source-layer")) {
    return fail("Stale Relink picker acceptance must not dispatch when the same layer ID belongs to another asset");
  }
  relink_dialog->setProperty("visible", false);
  controller.setAssetId(QStringLiteral("test-asset"));
  settle();
  if (!click_item(window, QStringLiteral("photoSmartObjectEmbedButton")) ||
      controller.embedCalls() != 1 || controller.lastIndex() != 0 ||
      controller.layerData().value(QStringLiteral("smartObject")).toMap()
              .value(QStringLiteral("policy")).toString() != QStringLiteral("embedded")) {
    return fail("Embed dispatches the linked source and changes the visible policy");
  }

  // Component documents embed image content; explicit linked policy must not
  // dispatch and then be silently changed when the component is applied.
  controller.setLayerData(QVariantMap{
      {QStringLiteral("id"), QStringLiteral("component-source")},
      {QStringLiteral("kind"), QStringLiteral("source")},
      {QStringLiteral("sourcePath"), QStringLiteral("/tmp/component-image.png")}});
  panel->setProperty("linkedContentAllowed", false);
  settle();
  if (!click_item(window, QStringLiteral("photoSmartObjectCreateButtonPrimaryAction")))
    return fail("Component image content exposes embedded conversion");
  link_option = find_item(window, QStringLiteral("photoSmartObjectCreateButtonPopupItem1"));
  if (!link_option || !link_option->isVisible() || link_option->isEnabled())
    return fail("Component image menu must explain unavailable linked policy");
  const auto conversions_before_component_link = controller.convertCalls();
  click_even_if_disabled(window, link_option);
  if (controller.convertCalls() != conversions_before_component_link)
    return fail("Unavailable component linked policy cannot dispatch");
  QTest::keyClick(window, Qt::Key_Escape);
  panel->setProperty("linkedContentAllowed", true);
  settle();

  // Pixel-backed layers may be embedded; a linked option is unavailable.
  controller.setLayerData(QVariantMap{
      {QStringLiteral("id"), QStringLiteral("pixel-layer")},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("name"), QStringLiteral("Pixel source")},
      {QStringLiteral("pixelData"), QByteArray("png-payload")}});
  settle();
  if (!panel->property("canConvert").toBool() ||
      !click_item(window,
          QStringLiteral("photoSmartObjectCreateButtonPrimaryAction"))) {
    return fail("PNG-backed pixel layers expose the embedded conversion menu");
  }
  link_option = find_item(window,
      QStringLiteral("photoSmartObjectCreateButtonPopupItem1"));
  if (link_option == nullptr || !link_option->isVisible() || link_option->isEnabled()) {
    return fail("Pixel-backed Smart Object menu disables file linking");
  }
  const auto conversions_before_disabled_link = controller.convertCalls();
  click_even_if_disabled(window, link_option);
  if (controller.convertCalls() != conversions_before_disabled_link) {
    return fail("Disabled linked policy cannot dispatch for a pixel-backed layer");
  }
  QTest::keyClick(window, Qt::Key_Escape);
  settle();

  // Restore a Smart Object and exercise both lock and busy gating on every action.
  controller.setLayerData(QVariantMap{
      {QStringLiteral("id"), QStringLiteral("smart-layer")},
      {QStringLiteral("kind"), QStringLiteral("source")},
      {QStringLiteral("name"), QStringLiteral("Embedded source")},
      {QStringLiteral("smartObject"), QVariantMap{
          {QStringLiteral("policy"), QStringLiteral("embedded")},
          {QStringLiteral("contentId"), QStringLiteral("shared-content")}}}});
  settle();
  const QStringList actions{
      QStringLiteral("photoSmartObjectDuplicateButton"),
      QStringLiteral("photoSmartObjectIndependentButton"),
      QStringLiteral("photoSmartObjectReplaceButton")};
  const auto duplicate_before_disabled = controller.duplicateCalls();
  const auto independent_before_disabled = controller.independentCalls();
  fixture->setProperty("layerLocked", true);
  settle();
  for (const auto& action : actions) {
    auto* button = find_item(window, action);
    if (button == nullptr || !button->isVisible() || button->isEnabled()) {
      std::cerr << "Locked action state: " << action.toStdString() << '\n';
      return fail("Locked Smart Objects disable all content-changing actions");
    }
    click_even_if_disabled(window, button);
  }
  if (controller.duplicateCalls() != duplicate_before_disabled ||
      controller.independentCalls() != independent_before_disabled) {
    return fail("Locked Smart Object actions cannot dispatch to the controller");
  }

  fixture->setProperty("layerLocked", false);
  controller.setBusy(true);
  settle();
  for (const auto& action : actions) {
    auto* button = find_item(window, action);
    if (button == nullptr || !button->isVisible() || button->isEnabled()) {
      std::cerr << "Busy action state: " << action.toStdString() << '\n';
      return fail("Professional-tools busy state disables all Smart Object actions");
    }
    click_even_if_disabled(window, button);
  }
  if (controller.duplicateCalls() != duplicate_before_disabled ||
      controller.independentCalls() != independent_before_disabled) {
    return fail("Busy Smart Object actions cannot dispatch to the controller");
  }
  controller.setBusy(false);
  settle();

  // A narrow inspector still exposes every action without overflow.
  window->setWidth(210);
  window->setHeight(520);
  settle(120);
  if (std::abs(panel->width() - 210.0) > 1.0) {
    return fail("Smart Object controls follow a narrow inspector width");
  }
  for (const auto& action : actions) {
    auto* button = find_item(window, action);
    if (button == nullptr || !button->isVisible() || !button->isEnabled() ||
        !inside_item(panel, button)) {
      std::cerr << "Narrow-layout action is missing or out of bounds: "
                << action.toStdString() << '\n';
      return fail("Smart Object actions remain visible and inside the narrow inspector");
    }
  }

  // Optional compact capture for receipt/evidence review.
  if (argc > 1) {
    controller.setLayerData(QVariantMap{
        {QStringLiteral("id"), QStringLiteral("capture-layer")},
        {QStringLiteral("kind"), QStringLiteral("source")},
        {QStringLiteral("name"), QStringLiteral("Linked source")},
        {QStringLiteral("sourcePath"), QStringLiteral("/Samples/Summit-Peak-Original.CR3")},
        {QStringLiteral("smartObject"), QVariantMap{
            {QStringLiteral("policy"), QStringLiteral("linked")},
            {QStringLiteral("contentId"), QStringLiteral("capture-content")},
            {QStringLiteral("fileName"), QStringLiteral("Summit-Peak-Original.CR3")},
            {QStringLiteral("sourcePath"), QStringLiteral("/Samples/Summit-Peak-Original.CR3")},
            {QStringLiteral("sourceSha256"), QString(64, QLatin1Char('a'))}}}});
    window->setWidth(280);
    window->setHeight(800);
    settle(160);
    auto* capture_status = find_item(window,
        QStringLiteral("photoSmartObjectLinkStatus"));
    if (capture_status == nullptr || !capture_status->isVisible() ||
        capture_status->property("text").toString() != QStringLiteral("Link is current")) {
      return fail("Compact linked-source capture includes a checked status");
    }
    const auto screenshot = window->grabWindow();
    if (screenshot.isNull() || !screenshot.save(QString::fromLocal8Bit(argv[1]), "PNG")) {
      return fail("Save the optional compact Smart Object controls PNG capture");
    }
  }

  settle();
  if (!warnings.isEmpty()) {
    for (const auto& warning : warnings) std::cerr << warning.toStdString() << '\n';
    return fail("The production Smart Object controls render and interact without QML warnings");
  }
  std::cout << "Production Smart Object QML embed/link menu, shared duplicate, Make Independent, lock/busy gating, and narrow inspector interactions passed; nested editable documents are outside this first slice\n";
  return 0;
}

#include "photo_smart_object_ui_test.moc"
