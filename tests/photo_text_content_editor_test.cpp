#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <iostream>
#include <memory>

class ContentController : public QObject {
  Q_OBJECT
 public:
  QObject* field = nullptr;
  QVariantMap layer;
  int writes = 0, begins = 0, ends = 0;
  bool reject = false;
  Q_INVOKABLE void beginCoalescedEdit() { ++begins; }
  Q_INVOKABLE void endCoalescedEdit() { ++ends; }
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
  object->setProperty("text", "Title\nSecond line");
  CHECK(controller.layer.value("text") == "Title\nSecond line", "Newlines must persist as plain text");
  object->setProperty("text", "");
  CHECK(controller.layer.value("text").toString().isEmpty(), "Empty text must persist");
  CHECK(controller.begins == 1 && controller.writes == 2, "Typing should share one undo transaction");
  QMetaObject::invokeMethod(object.get(), "clearInputFocus");
  CHECK(controller.ends == 1, "Focus loss must close the transaction once");
  controller.layer.insert("text", "Undo restored");
  object->setProperty("layerData", controller.layer);
  CHECK(object->property("text") == "Undo restored" && controller.writes == 2,
        "External undo updates must not create a new write");
  field->forceActiveFocus();
  object->setProperty("text", "Edited first");
  controller.layer = {{"id", "text-2"}, {"kind", "text"}, {"text", "Second layer"}};
  object->setProperty("layerData", controller.layer);
  CHECK(!object->property("userEditing").toBool() && object->property("text") == "Second layer",
        "Switching layers must reset focus and load the new copy");
  CHECK(controller.begins == 2 && controller.ends == 2, "Layer switch must close the previous transaction");
  field->forceActiveFocus();
  controller.reject = true;
  object->setProperty("text", "Rejected change");
  CHECK(object->property("text") == "Second layer" &&
      !object->property("editError").toString().isEmpty() && controller.writes == 3,
      "Rejected edits must restore saved copy and explain the failure");
  QMetaObject::invokeMethod(object.get(), "clearInputFocus");
  controller.reject = false;
  object->setProperty("locked", true);
  field->forceActiveFocus();
  object->setProperty("text", "Blocked");
  CHECK(controller.writes == 3, "Locked content must never write");
  std::cout << "Multiline, empty copy, undo sync, layer switching, and lock passed\n";
  return 0;
}
#include "photo_text_content_editor_test.moc"
