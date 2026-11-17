#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <iostream>
#include <memory>

class ContentController : public QObject {
  Q_OBJECT
 public:
  QObject* field = nullptr;
  QVariantMap layer;
  QVariantList storedLayers;
  int writes = 0, begins = 0, ends = 0, cancels = 0;
  bool reject = false;
  Q_INVOKABLE void beginCoalescedEdit() { ++begins; }
  Q_INVOKABLE void endCoalescedEdit() { ++ends; }
  Q_INVOKABLE bool cancelCoalescedEdit() { ++cancels; return true; }
  Q_INVOKABLE bool setPhotoLayerTextById(const QString& id, const QString& text) {
    if (reject) return false;
    for (qsizetype index = 0; index < storedLayers.size(); ++index) {
      auto candidate = storedLayers.at(index).toMap();
      if (candidate.value("id").toString() != id || candidate.value("locked").toBool()) continue;
      candidate.insert("text", text);
      storedLayers[index] = candidate;
      if (layer.value("id").toString() == id) {
        layer = candidate;
        field->setProperty("layerData", layer);
      }
      ++writes;
      return true;
    }
    return false;
  }
  Q_INVOKABLE bool setPhotoLayerText(int index, const QString& text) {
    if (reject || index != 0 || layer.value("locked").toBool()) return false;
    layer.insert("text", text);
    ++writes;
    field->setProperty("layerData", layer);
    return true;
  }
};
#define CHECK(c, m) do { if (!(c)) { std::cerr << m << '\n'; return 1; } } while (false)
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(
      PHOTO_TEXT_TEST_ROOT "/apps/desktop/qml/components/PhotoTextContentEditor.qml")));
  ContentController controller;
  controller.layer = {{"id", "text-1"}, {"kind", "text"}, {"text", "Original"}};
  controller.storedLayers = {controller.layer};
  std::unique_ptr<QObject> object(component.createWithInitialProperties({
      {"controller", QVariant::fromValue(static_cast<QObject*>(&controller))},
      {"layerIndex", 0}, {"layerData", controller.layer}}));
  CHECK(object, component.errorString().toStdString());
  controller.field = object.get();
  auto* field = qobject_cast<QQuickItem*>(object.get());
  CHECK(field, "Multiline content editor must be a Quick item");
  QQuickWindow window;
  window.resize(360, 160);
  field->setSize(QSizeF(340, 100));
  field->setParentItem(window.contentItem());
  window.show();
  QCoreApplication::processEvents();
  CHECK(object->property("text").toString() == "Original", "Initial copy should load");
  field->forceActiveFocus();
  CHECK(object->property("userEditing").toBool(), "Input must receive editing focus");
  object->setProperty("text", "Temporary");
  object->setProperty("text", "Original");
  QThread::msleep(70);
  QCoreApplication::processEvents();
  CHECK(controller.writes == 0 && !object->property("pendingEdit").toBool(),
        "Undoing text to the saved value cancels the pending debounce write");
  object->setProperty("text", "Title\nSecond line");
  CHECK(controller.layer.value("text") == "Original" && object->property("pendingEdit").toBool(),
        "Typing waits briefly so rapid edits can coalesce");
  QMetaObject::invokeMethod(object.get(), "flushPendingText");
  CHECK(controller.layer.value("text") == "Title\nSecond line", "Flushed newlines persist as plain text");
  object->setProperty("text", "");
  QMetaObject::invokeMethod(object.get(), "flushPendingText");
  CHECK(controller.layer.value("text").toString().isEmpty(), "Empty text must persist");
  CHECK(controller.begins == 2 && controller.ends == 1 && controller.writes == 2,
        "Typing is coalesced within transactions and an undo-to-saved edit closes its empty transaction");
  QMetaObject::invokeMethod(object.get(), "clearInputFocus");
  CHECK(controller.ends == 2, "Focus loss must close the remaining text transaction once");
  controller.layer.insert("text", "Undo restored");
  controller.storedLayers[0] = controller.layer;
  object->setProperty("layerData", controller.layer);
  CHECK(object->property("text") == "Undo restored" && controller.writes == 2,
        "External undo updates must not create a new write");
  field->forceActiveFocus();
  object->setProperty("text", "Edited first");
  // The newly selected layer can already contain the pending copy. Comparing
  // against that layer must not suppress the write to the old target.
  controller.storedLayers.push_back(QVariantMap{{"id", "text-2"}, {"kind", "text"}, {"text", "Edited first"}});
  controller.layer = {{"id", "text-2"}, {"kind", "text"}, {"text", "Edited first"}};
  object->setProperty("layerData", controller.layer);
  CHECK(!object->property("userEditing").toBool() && object->property("text") == "Edited first",
        "Switching layers must reset focus and load the new copy");
  CHECK(controller.begins == 3 && controller.ends == 3 &&
      controller.storedLayers.at(0).toMap().value("text").toString() == "Edited first",
      "Pending text commits to its original layer ID before a target switch closes the transaction");
  field->forceActiveFocus();
  controller.reject = true;
  object->setProperty("text", "Rejected change");
  QMetaObject::invokeMethod(object.get(), "flushPendingText");
  CHECK(object->property("text") == "Rejected change" &&
      object->property("pendingText") == "Rejected change" &&
      object->property("pendingEdit").toBool() &&
      !object->property("editError").toString().isEmpty() && controller.writes == 3,
      "Rejected edits must retain the draft, explain the failure, and avoid a false write");
  QMetaObject::invokeMethod(object.get(), "cancelEditing");
  CHECK(object->property("text") == "Edited first" && controller.cancels == 1,
      "Cancel restores the saved text and closes the rejected transaction");
  field->forceActiveFocus();
  object->setProperty("text", "Unsaved before target change");
  controller.reject = true;
  QMetaObject::invokeMethod(object.get(), "flushPendingText");
  controller.storedLayers.push_back(QVariantMap{{"id", "text-3"}, {"kind", "text"}, {"text", "Next layer"}});
  controller.layer = {{"id", "text-3"}, {"kind", "text"}, {"text", "Next layer"}};
  object->setProperty("layerData", controller.layer);
  CHECK(object->property("pendingEdit").toBool() &&
      object->property("pendingText").toString() == "Unsaved before target change" &&
      !object->property("editError").toString().isEmpty() &&
      object->property("text").toString() == "Unsaved before target change",
      "A rejected flush during target change retains both the draft and its visible error");
  QMetaObject::invokeMethod(object.get(), "cancelEditing");
  CHECK(object->property("text").toString() == "Next layer" &&
      !object->property("pendingEdit").toBool(),
      "Cancel after a rejected target change discards the stale draft and loads the selected layer");
  controller.reject = false;
  object->setProperty("locked", true);
  field->forceActiveFocus();
  object->setProperty("text", "Blocked");
  QMetaObject::invokeMethod(object.get(), "flushPendingText");
  CHECK(controller.writes == 3, "Locked content must never write");
  std::cout << "Multiline, empty copy, undo sync, layer switching, and lock passed\n";
  return 0;
}
#include "photo_text_content_editor_test.moc"
