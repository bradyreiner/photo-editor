#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <iostream>
#include <memory>
#include <QSet>
#include <functional>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components"
Item {
    id: harness
    width: 640; height: 700
    signal assetChanged()
    property bool draft: false
    property int applied: 0
    property int cancelled: 0
    property int sizeEdits: 0
    property bool reject: false
    function availablePhotoEffects() {
        return [{type: "gaussian_blur", name: "Gaussian blur", category: "Blur", description: "Soften"}]
    }
    function photoLayerEffects(index) { return [{id: "draft-effect"}] }
    function previewPhotoLayerEffect(index, type) { draft = !reject; return draft }
    function finishPhotoLayerEffectPreview(apply) {
        if (!draft) return false
        draft = false
        if (apply) applied++; else cancelled++
        return true
    }
    function setPhotoLayerGaussianBlur(index, id, size, amount, enabled) { sizeEdits++; return true }
    PhotoEffectStack {
        objectName: "stack"
        width: 300
        controller: harness
        layerIndex: 0
        layerData: null
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EFFECT_TEST_ROOT "/tests/effect-browser-harness.qml")));
  std::unique_ptr<QObject> root(component.create());
  if (!root) { std::cerr << component.errorString().toStdString(); return 1; }
  QQuickWindow window;
  window.resize(640, 700);
  qobject_cast<QQuickItem*>(root.get())->setParentItem(window.contentItem());
  window.show();
  window.requestActivate();
  const auto settle = []() {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 250) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  };
  settle();
  const auto find = [&](const char* name) {
    QSet<QObject*> visited;
    std::function<QObject*(QObject*)> search = [&](QObject* object) -> QObject* {
      if (!object || visited.contains(object)) return nullptr;
      visited.insert(object);
      if (object->objectName() == QString::fromLatin1(name)) return object;
      for (auto* child : object->children()) if (auto* result = search(child)) return result;
      if (auto* item = qobject_cast<QQuickItem*>(object))
        for (auto* child : item->childItems()) if (auto* result = search(child)) return result;
      return nullptr;
    };
    if (auto* result = search(root.get())) return result;
    return search(window.contentItem());
  };
  auto* add = find("photoAddEffectButton");
  auto* picker = find("photoEffectPicker");
  auto* choice = find("photoEffectChoice_gaussian_blur");
  auto* apply = find("photoEffectPreviewApply");
  auto* size = find("photoEffectPreviewSize");
  if (!add || !picker || !choice || !apply || !size) {
    std::cerr << "Browser controls did not instantiate\n"; return 1;
  }
  QMetaObject::invokeMethod(add, "clicked");
  settle();
  if (!picker->property("visible").toBool() || apply->property("enabled").toBool()) return 1;
  QMetaObject::invokeMethod(choice, "clicked");
  if (!root->property("draft").toBool() || !picker->property("visible").toBool() ||
      !apply->property("enabled").toBool()) return 1;
  size->setProperty("value", 23);
  QMetaObject::invokeMethod(size, "valueModified");
  if (root->property("sizeEdits").toInt() != 1) return 1;
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &escape);
  settle();
  if (root->property("draft").toBool() || root->property("cancelled").toInt() != 1) return 1;
  QMetaObject::invokeMethod(add, "clicked");
  settle();
  QMetaObject::invokeMethod(choice, "clicked");
  QMetaObject::invokeMethod(apply, "clicked");
  settle();
  if (root->property("applied").toInt() != 1 || root->property("cancelled").toInt() != 1 ||
      picker->property("visible").toBool()) return 1;
  QMetaObject::invokeMethod(add, "clicked");
  settle();
  QMetaObject::invokeMethod(choice, "clicked");
  QMetaObject::invokeMethod(root.get(), "assetChanged");
  settle();
  if (root->property("draft").toBool() || picker->property("visible").toBool()) return 1;
  root->setProperty("reject", true);
  QMetaObject::invokeMethod(add, "clicked");
  settle();
  QMetaObject::invokeMethod(choice, "clicked");
  if (apply->property("enabled").toBool() || picker->property("errorMessage").toString().isEmpty()) return 1;
  return 0;
}
