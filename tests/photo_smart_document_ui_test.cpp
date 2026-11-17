#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <iostream>
#include <memory>

#ifndef PHOTO_EDITOR_SOURCE_DIR
#define PHOTO_EDITOR_SOURCE_DIR "."
#endif

class SmartDocumentTestController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool professionalToolsBusy READ professionalToolsBusy NOTIFY controlsChanged)
  Q_PROPERTY(QVariantMap layerData READ layerData NOTIFY controlsChanged)
  Q_PROPERTY(QVariantMap smartDocumentEditState READ smartDocumentEditState NOTIFY controlsChanged)

 public:
  explicit SmartDocumentTestController(QObject* parent = nullptr)
      : QObject(parent),
        layer_data_{{QStringLiteral("id"), QStringLiteral("component-layer")},
                    {QStringLiteral("kind"), QStringLiteral("group")},
                    {QStringLiteral("name"), QStringLiteral("Reusable product")},
                    {QStringLiteral("smartObjectDocument"), QVariantMap{
                         {QStringLiteral("contentId"), QStringLiteral("content-a")}}}},
        parent_layers_(initial_layers()) {}

  [[nodiscard]] bool professionalToolsBusy() const { return busy_; }
  [[nodiscard]] QVariantMap layerData() const { return layer_data_; }
  [[nodiscard]] int beginCalls() const { return begin_calls_; }
  [[nodiscard]] int duplicateCalls() const { return duplicate_calls_; }
  [[nodiscard]] int independentCalls() const { return independent_calls_; }
  [[nodiscard]] int changeCalls() const { return change_calls_; }
  [[nodiscard]] int applyCalls() const { return apply_calls_; }
  [[nodiscard]] int cancelCalls() const { return cancel_calls_; }
  [[nodiscard]] int replaceCalls() const { return replace_calls_; }
  [[nodiscard]] int rejectedChangeCalls() const { return rejected_change_calls_; }
  [[nodiscard]] QString selectedId() const { return selected_id_; }
  [[nodiscard]] QVariantMap lastPatch() const { return last_patch_; }
  [[nodiscard]] QString lastReplaceSession() const { return last_replace_session_; }
  [[nodiscard]] QString lastReplaceChild() const { return last_replace_child_; }
  [[nodiscard]] QString lastReplacePath() const { return last_replace_path_; }
  [[nodiscard]] bool lastReplaceAccepted() const { return last_replace_accepted_; }
  [[nodiscard]] QString sessionId() const { return session_id_; }
  [[nodiscard]] QString parentText() const {
    return layer_by_id(parent_layers_, QStringLiteral("text-child"))
        .value(QStringLiteral("text")).toString();
  }
  [[nodiscard]] QString draftText() const {
    return layer_by_id(edit_layers_, QStringLiteral("text-child"))
        .value(QStringLiteral("text")).toString();
  }

  [[nodiscard]] QVariantMap smartDocumentEditState() const {
    if (!active_) return {{QStringLiteral("active"), false}};
    return {{QStringLiteral("active"), true},
            {QStringLiteral("sessionId"), session_id_},
            {QStringLiteral("layers"), edit_layers_},
            {QStringLiteral("previewSource"), QString()},
            {QStringLiteral("previewBusy"), false}};
  }

  void setBusy(bool busy) {
    if (busy_ == busy) return;
    busy_ = busy;
    emit controlsChanged();
  }

  void changeSessionId(const QString& id) {
    session_id_ = id;
    emit controlsChanged();
  }

  void setRejectChanges(bool reject) { reject_changes_ = reject; }

  Q_INVOKABLE bool convertPhotoLayerToComponent(int index) {
    return index == 0;
  }

  Q_INVOKABLE bool duplicatePhotoComponent(int index) {
    ++duplicate_calls_;
    return index == 0;
  }

  Q_INVOKABLE bool makePhotoComponentIndependent(int index) {
    ++independent_calls_;
    return index == 0;
  }

  Q_INVOKABLE bool beginPhotoComponentEditing(int index) {
    ++begin_calls_;
    if (index != 0 || busy_ || active_) return false;
    busy_ = true;
    emit controlsChanged();
    QTimer::singleShot(25, this, [this] {
      busy_ = false;
      active_ = true;
      session_id_ = QStringLiteral("session-%1").arg(begin_calls_);
      edit_layers_ = parent_layers_;
      emit controlsChanged();
    });
    return true;
  }

  Q_INVOKABLE bool changePhotoComponentChild(const QString& id,
                                             const QVariantMap& patch) {
    if (!active_ || busy_) return false;
    if (reject_changes_) {
      ++rejected_change_calls_;
      return false;
    }
    const auto index = child_index(id, edit_layers_);
    if (index < 0) return false;
    auto child = edit_layers_.at(index).toMap();
    for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
      child.insert(it.key(), it.value());
    }
    if (child == edit_layers_.at(index).toMap()) return false;
    edit_layers_[index] = child;
    ++change_calls_;
    selected_id_ = id;
    last_patch_ = patch;
    emit controlsChanged();
    return true;
  }

  Q_INVOKABLE bool addPhotoComponentChild(const QString& kind) {
    if (!active_ || busy_ ||
        (kind != QStringLiteral("text") && kind != QStringLiteral("fill"))) {
      return false;
    }
    auto child = QVariantMap{
        {QStringLiteral("id"), kind == QStringLiteral("text")
             ? QStringLiteral("added-text") : QStringLiteral("added-fill")},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("name"), kind == QStringLiteral("text")
             ? QStringLiteral("Text") : QStringLiteral("Fill")},
        {QStringLiteral("opacity"), 1.0},
        {QStringLiteral("enabled"), true}};
    if (kind == QStringLiteral("text")) {
      child.insert(QStringLiteral("text"), QStringLiteral("Your text"));
      child.insert(QStringLiteral("textColor"), QStringLiteral("#ffffffff"));
    } else {
      child.insert(QStringLiteral("fillColor"), QStringLiteral("#4a78ff"));
    }
    edit_layers_.prepend(child);
    emit controlsChanged();
    return true;
  }

  Q_INVOKABLE bool replacePhotoComponentImage(const QString& session_id,
                                              const QString& child_id,
                                              const QString& path) {
    ++replace_calls_;
    last_replace_session_ = session_id;
    last_replace_child_ = child_id;
    last_replace_path_ = path;
    last_replace_accepted_ = active_ && !busy_ &&
        session_id == session_id_ && child_id == QStringLiteral("source-child") &&
        !path.isEmpty();
    return last_replace_accepted_;
  }

  Q_INVOKABLE bool applyPhotoComponentEditing() {
    if (!active_ || busy_) return false;
    ++apply_calls_;
    parent_layers_ = edit_layers_;
    finish_session();
    return true;
  }

  Q_INVOKABLE void cancelPhotoComponentEditing() {
    if (!active_ && !busy_) return;
    ++cancel_calls_;
    finish_session();
  }

 signals:
  void controlsChanged();
  void assetChanged();

 private:
  static QVariantList initial_layers() {
    return {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("fill-child")},
                    {QStringLiteral("kind"), QStringLiteral("fill")},
                    {QStringLiteral("name"), QStringLiteral("Brand fill")},
                    {QStringLiteral("fillColor"), QStringLiteral("#28445eff")},
                    {QStringLiteral("opacity"), 1.0},
                    {QStringLiteral("enabled"), true}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("text-child")},
                    {QStringLiteral("kind"), QStringLiteral("text")},
                    {QStringLiteral("name"), QStringLiteral("Headline")},
                    {QStringLiteral("text"), QStringLiteral("Original headline")},
                    {QStringLiteral("textColor"), QStringLiteral("#ffffffff")},
                    {QStringLiteral("opacity"), 1.0},
                    {QStringLiteral("enabled"), true}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("source-child")},
                    {QStringLiteral("kind"), QStringLiteral("source")},
                    {QStringLiteral("name"), QStringLiteral("Product image")},
                    {QStringLiteral("opacity"), 1.0},
                    {QStringLiteral("enabled"), true}}};
  }

  static int child_index(const QString& id, const QVariantList& layers) {
    for (int index = 0; index < layers.size(); ++index) {
      if (layers.at(index).toMap().value(QStringLiteral("id")).toString() == id) {
        return index;
      }
    }
    return -1;
  }

  static QVariantMap layer_by_id(const QVariantList& layers,
                                 const QString& id) {
    const int index = child_index(id, layers);
    return index < 0 ? QVariantMap{} : layers.at(index).toMap();
  }

  void finish_session() {
    active_ = false;
    busy_ = false;
    session_id_.clear();
    edit_layers_.clear();
    emit controlsChanged();
  }

  QVariantMap layer_data_;
  QVariantList parent_layers_;
  QVariantList edit_layers_;
  QString session_id_;
  QString selected_id_;
  QVariantMap last_patch_;
  QString last_replace_session_;
  QString last_replace_child_;
  QString last_replace_path_;
  bool last_replace_accepted_ = false;
  bool busy_ = false;
  bool active_ = false;
  int begin_calls_ = 0;
  int duplicate_calls_ = 0;
  int independent_calls_ = 0;
  int change_calls_ = 0;
  int apply_calls_ = 0;
  int cancel_calls_ = 0;
  int replace_calls_ = 0;
  int rejected_change_calls_ = 0;
  bool reject_changes_ = false;
};

namespace {

void settle(int milliseconds = 70) {
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

bool click_layer_row(QQuickWindow* window, int row) {
  auto* list = find_item(window, QStringLiteral("photoComponentChildList"));
  if (window == nullptr || list == nullptr || !list->isVisible() ||
      !list->isEnabled() || row < 0 || row > 2) {
    return false;
  }
  const auto point = list->mapToScene(QPointF(22.0, row * 32.0 + 16.0)).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
  settle();
  return true;
}

void type_text(QQuickWindow* window, const QString& text) {
  for (const auto character : text) {
    QTest::keyClick(window, character.toLatin1());
  }
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
  if (!temporary.isValid()) return fail("Create the component image picker fixture");
  const auto replacement_path = temporary.filePath(QStringLiteral("replacement.png"));
  QFile replacement_file(replacement_path);
  if (!replacement_file.open(QIODevice::WriteOnly) ||
      replacement_file.write("png") != 3) {
    return fail("Create an image path for the component picker");
  }
  replacement_file.close();
  SmartDocumentTestController controller;

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
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"),
                                           settings.get());
  engine.rootContext()->setContextProperty(QStringLiteral("smartDocumentController"),
                                           &controller);

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
    objectName: "photoSmartDocumentUiHost"
    visible: true
    width: 960
    height: 760
    color: "#171717"
    Components.PhotoSmartDocumentControls {
        id: controls
        objectName: "photoSmartDocumentControls"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        controller: smartDocumentController
        layerIndex: 0
        layerData: smartDocumentController.layerData
    }
    Components.PhotoSmartDocumentDialog {
        id: editDialog
        objectName: "photoSmartDocumentDialog"
        controller: smartDocumentController
    }
}
)qml").arg(components_import);
  QQmlComponent fixture_component(&engine);
  fixture_component.setData(fixture_source.toUtf8(),
      QUrl::fromLocalFile(QDir::temp().filePath(
          QStringLiteral("photo-smart-document-ui-fixture.qml"))));
  std::unique_ptr<QObject> fixture(fixture_component.create());
  if (!fixture) {
    std::cerr << fixture_component.errorString().toStdString() << '\n';
    return fail("Instantiate production component controls and dialog in QQuickWindow");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* controls = find_item(window,
      QStringLiteral("photoSmartDocumentControls"));
  auto* dialog = fixture->findChild<QObject*>(
      QStringLiteral("photoSmartDocumentDialog"));
  if (window == nullptr || controls == nullptr || dialog == nullptr) {
    return fail("Find the production component controls, dialog, and host window");
  }
  settle(120);
  const auto capture = [window](const QString& name) {
    const auto directory = qEnvironmentVariable("LOOKSY_PROFESSIONAL_UI_CAPTURE_DIR");
    if (directory.isEmpty()) return true;
    return QDir().mkpath(directory) &&
        window->grabWindow().save(QDir(directory).filePath(name));
  };

  auto* edit_button = find_item(window,
      QStringLiteral("photoComponentEditButton"));
  auto* duplicate_button = find_item(window,
      QStringLiteral("photoComponentDuplicateButton"));
  auto* independent_button = find_item(window,
      QStringLiteral("photoComponentIndependentButton"));
  if (edit_button == nullptr || !edit_button->isVisible() ||
      !edit_button->isEnabled() || duplicate_button == nullptr ||
      !duplicate_button->isVisible() || independent_button == nullptr ||
      !independent_button->isVisible()) {
    return fail("Component layer controls should expose edit, shared duplicate, and independent actions");
  }

  controller.setBusy(true);
  settle();
  const int begins_before_busy = controller.beginCalls();
  click_even_if_disabled(window, edit_button);
  if (edit_button->isEnabled() || controller.beginCalls() != begins_before_busy) {
    return fail("Busy state must disable layer edit dispatch");
  }
  controller.setBusy(false);
  if (!click_item(window, QStringLiteral("photoComponentDuplicateButton")) ||
      controller.duplicateCalls() != 1 ||
      !click_item(window, QStringLiteral("photoComponentIndependentButton")) ||
      controller.independentCalls() != 1) {
    return fail("Shared duplicate and independent component controls should dispatch their layer actions");
  }
  if (!click_item(window, QStringLiteral("photoComponentEditButton")) ||
      controller.beginCalls() != 1) {
    return fail("Edit contents pointer action should dispatch the selected layer index");
  }
  settle(140);
  if (!dialog->property("opened").toBool() ||
      !controller.smartDocumentEditState().value(QStringLiteral("active")).toBool()) {
    return fail("The dialog should open after asynchronous edit-session admission");
  }

  auto* child_list = find_item(window, QStringLiteral("photoComponentChildList"));
  auto* text_area = find_item(window, QStringLiteral("photoComponentChildText"));
  auto* apply_button = find_item(window, QStringLiteral("photoComponentApplyButton"));
  auto* cancel_button = find_item(window, QStringLiteral("photoComponentCancelButton"));
  if (child_list == nullptr || text_area == nullptr || apply_button == nullptr ||
      cancel_button == nullptr) {
    return fail("The nested editor should expose its child list and editable text controls");
  }

  if (!click_layer_row(window, 1) ||
      dialog->property("selectedId").toString() !=
          QStringLiteral("text-child") ||
      !text_area->isVisible()) {
    return fail("Pointer selection should switch the inspector to the chosen text layer");
  }
  if (!capture(QStringLiteral("component-dialog-standard.png")))
    return fail("Save the standard production component dialog capture");
  const auto original_parent_text = controller.parentText();
  const auto text_point = text_area->mapToScene(
      text_area->boundingRect().center()).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, text_point);
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  type_text(window, QStringLiteral("Cancelled title"));
  settle();
  if (text_area->property("text").toString() !=
      QStringLiteral("Cancelled title")) {
    return fail("Keyboard input should edit the selected nested text field");
  }
  if (!click_layer_row(window, 0) || !click_layer_row(window, 1) ||
      controller.draftText() != QStringLiteral("Cancelled title")) {
    return fail("Changing child selection should flush text into the isolated draft");
  }
  if (!click_item(window, QStringLiteral("photoComponentAddTextButton")) ||
      dialog->property("selectedId").toString() != QStringLiteral("added-text") ||
      find_item(window, QStringLiteral("photoComponentChildText")) == nullptr ||
      !find_item(window, QStringLiteral("photoComponentChildText"))->isVisible() ||
      find_item(window, QStringLiteral("photoComponentChildText"))
              ->property("text").toString() != QStringLiteral("Your text")) {
    return fail("Adding text should select the new row and show its text in the inspector");
  }
  if (!click_item(window, QStringLiteral("photoComponentAddFillButton")) ||
      dialog->property("selectedId").toString() != QStringLiteral("added-fill") ||
      find_item(window, QStringLiteral("photoComponentChildText")) == nullptr ||
      find_item(window, QStringLiteral("photoComponentChildText"))->isVisible() ||
      find_item(window, QStringLiteral("photoComponentChildColor")) == nullptr ||
      !find_item(window, QStringLiteral("photoComponentChildColor"))->isVisible() ||
      find_item(window, QStringLiteral("photoComponentChildColor"))
              ->property("text").toString() != QStringLiteral("#4a78ff")) {
    return fail("Adding fill should select the new row and show its color in the inspector");
  }
  if (!click_item(window, QStringLiteral("photoComponentCancelButton")) ||
      controller.cancelCalls() != 1 || controller.parentText() != original_parent_text ||
      dialog->property("opened").toBool()) {
    return fail("Cancel should discard draft text without changing the component parent");
  }

  // A narrower host should keep the list, text field, and footer actions
  // visible inside the production dialog surface.
  window->setWidth(360);
  settle();
  if (!click_item(window, QStringLiteral("photoComponentEditButton"))) {
    return fail("Reopen the nested editor for the compact layout check");
  }
  settle(140);
  child_list = find_item(window, QStringLiteral("photoComponentChildList"));
  text_area = find_item(window, QStringLiteral("photoComponentChildText"));
  apply_button = find_item(window, QStringLiteral("photoComponentApplyButton"));
  cancel_button = find_item(window, QStringLiteral("photoComponentCancelButton"));
  if (!click_layer_row(window, 1)) {
    return fail("Select the text child before checking the compact inspector layout");
  }
  text_area = find_item(window, QStringLiteral("photoComponentChildText"));
  if (!dialog->property("opened").toBool() || child_list == nullptr ||
      text_area == nullptr || apply_button == nullptr || cancel_button == nullptr ||
      !inside_item(window->contentItem(), child_list) ||
      !inside_item(window->contentItem(), text_area) ||
      !inside_item(window->contentItem(), apply_button) ||
      !inside_item(window->contentItem(), cancel_button) ||
      dialog->property("width").toDouble() > window->width() - 16.0) {
    return fail("Compact component editing must keep its child list, editor, and actions within the window");
  }
  if (!capture(QStringLiteral("component-dialog-compact.png")))
    return fail("Save the compact production component dialog capture");

  if (!click_layer_row(window, 1)) {
    return fail("Select the nested text layer in the compact dialog");
  }
  text_area = find_item(window, QStringLiteral("photoComponentChildText"));
  const auto compact_text_point = text_area->mapToScene(
      text_area->boundingRect().center()).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, compact_text_point);
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  type_text(window, QStringLiteral("Applied title"));
  settle();
  if (!click_layer_row(window, 0) || !click_layer_row(window, 1) ||
      controller.draftText() != QStringLiteral("Applied title")) {
    return fail("Text input should flush into the isolated draft before busy Apply validation");
  }

  controller.setRejectChanges(true);
  text_area = find_item(window, QStringLiteral("photoComponentChildText"));
  const auto rejected_text_point = text_area->mapToScene(
      text_area->boundingRect().center()).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, rejected_text_point);
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  type_text(window, QStringLiteral("Rejected valid-looking title"));
  settle();
  const auto rejected_text = text_area->property("text").toString();
  if (rejected_text != QStringLiteral("Rejected valid-looking title") ||
      !click_item(window, QStringLiteral("photoComponentApplyButton")) ||
      controller.applyCalls() != 0 ||
      controller.parentText() != original_parent_text ||
      controller.draftText() != QStringLiteral("Applied title") ||
      !dialog->property("opened").toBool() ||
      dialog->property("formError").toString().isEmpty() ||
      controller.rejectedChangeCalls() == 0 ||
      text_area->property("text").toString() != rejected_text) {
    return fail("A rejected backend text patch must keep the dialog and typed input without applying an older draft");
  }

  // Once the rejected input is restored to the selected layer's current text,
  // Apply should accept it as an exact no-op and publish the earlier valid
  // draft change.
  controller.setRejectChanges(false);
  text_area = find_item(window, QStringLiteral("photoComponentChildText"));
  const auto restored_text_point = text_area->mapToScene(
      text_area->boundingRect().center()).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, restored_text_point);
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  type_text(window, QStringLiteral("Applied title"));
  settle();
  const int changes_before_noop_apply = controller.changeCalls();

  controller.setBusy(true);
  settle();
  apply_button = find_item(window, QStringLiteral("photoComponentApplyButton"));
  cancel_button = find_item(window, QStringLiteral("photoComponentCancelButton"));
  if (apply_button == nullptr || apply_button->isEnabled() ||
      cancel_button == nullptr || !cancel_button->isEnabled()) {
    return fail("Busy work should guard Apply while keeping Cancel available");
  }
  const int applies_before_busy_click = controller.applyCalls();
  click_even_if_disabled(window, apply_button);
  if (controller.applyCalls() != applies_before_busy_click ||
      controller.parentText() != original_parent_text) {
    return fail("A busy Apply click must not publish nested changes");
  }
  controller.setBusy(false);
  settle();
  if (!click_item(window, QStringLiteral("photoComponentApplyButton")) ||
      controller.applyCalls() != 1 ||
      controller.changeCalls() != changes_before_noop_apply ||
      controller.parentText() != QStringLiteral("Applied title") ||
      dialog->property("opened").toBool()) {
    return fail("Apply should publish the edited text and close the dialog once work is available");
  }

  // Reopen and select the image layer. Acceptance after the edit session ID
  // changes must be rejected by the dialog before it dispatches to the
  // controller, leaving the current draft and parent untouched.
  if (!click_item(window, QStringLiteral("photoComponentEditButton"))) {
    return fail("Open a new session for image replacement picker validation");
  }
  settle(140);
  if (!click_layer_row(window, 2) ||
      dialog->property("selectedId").toString() !=
          QStringLiteral("source-child") ||
      !click_item(window, QStringLiteral("photoComponentReplaceImageButton"))) {
    return fail("Image replacement should be reachable for the selected source child");
  }
  auto* picker = fixture->findChild<QObject*>(
      QStringLiteral("photoComponentReplaceImageDialog"));
  if (picker == nullptr) return fail("Find the production component image picker");
  const auto captured_session = controller.sessionId();
  const auto parent_before_stale_picker = controller.parentText();
  const auto draft_before_stale_picker = controller.draftText();
  picker->setProperty("selectedFile",
      QUrl::fromLocalFile(replacement_path));
  controller.changeSessionId(QStringLiteral("newer-session"));
  if (!QMetaObject::invokeMethod(picker, "accepted", Qt::DirectConnection) ||
      controller.replaceCalls() != 0 ||
      controller.sessionId() == captured_session ||
      controller.parentText() != parent_before_stale_picker ||
      controller.draftText() != draft_before_stale_picker ||
      !controller.smartDocumentEditState().value(QStringLiteral("active")).toBool() ||
      !dialog->property("opened").toBool()) {
    return fail("A stale image picker result should be rejected without dispatch or parent/draft mutation");
  }
  picker->setProperty("visible", false);
  settle();
  if (!click_item(window, QStringLiteral("photoComponentCancelButton")) ||
      controller.cancelCalls() != 2) {
    return fail("Close the image picker edit session through Cancel");
  }

  if (!warnings.isEmpty()) {
    for (const auto& warning : warnings) std::cerr << warning.toStdString() << '\n';
    return fail("Production component dialog should instantiate without QML warnings");
  }

  std::cout << "photo_smart_document_ui_test passed\n";
  return 0;
}

#include "photo_smart_document_ui_test.moc"
