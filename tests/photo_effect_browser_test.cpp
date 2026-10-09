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
    property int lookApplications: 0
    property bool reject: false
    property bool hasSavedEffect: false
    property bool effectEnabled: true
    property real effectAmount: 0.4
    property int bypassEdits: 0
    function availablePhotoEffects() {
        return [{type: "film_grain", name: "Film grain", category: "Film", description: "Texture"},
            {type: "gaussian_blur", name: "Gaussian blur", category: "Blur", description: "Soften"},
            {type: "box_blur", name: "Box blur", category: "Blur", description: "Average nearby pixels"}]
    }
    function availablePhotoEffectLooks() {
        return [{id: "dream_haze", name: "Dream Haze", category: "Optical", description: "Diffuse bright details"}]
    }
    function applyPhotoLayerEffectLook(index, id) {
        if (reject || id !== "dream_haze") return false
        lookApplications++
        return true
    }
    function photoLayerEffects(index) {
        return hasSavedEffect ? [{id: "saved-effect",type:"gaussian_blur",version:1,
            enabled:effectEnabled,amount:effectAmount,sigma:8,edgeMode:0}]
            : [{id: "draft-effect"}]
    }
    function setPhotoLayerEffectEnabled(index, id, enabled) {
        if (index !== 0 || id !== "saved-effect") return false
        effectEnabled = enabled
        bypassEdits++
        return true
    }
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
        layerData: harness.hasSavedEffect ? {displayName:"Violet paint",enabled:harness.effectEnabled} : null
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
  if (picker->property("modal").toBool() || picker->property("dim").toBool()) {
    std::cerr << "Filter browsing and preview must leave the canvas undimmed\n"; return 1;
  }
  auto* blur_group = qobject_cast<QQuickItem*>(find("photoEffectGroup_Blur"));
  auto* film_group = qobject_cast<QQuickItem*>(find("photoEffectGroup_Film"));
  auto* box_choice = qobject_cast<QQuickItem*>(find("photoEffectChoice_box_blur"));
  auto* gaussian_choice = qobject_cast<QQuickItem*>(choice);
  auto* category_picker = find("photoEffectCategoryPicker");
  if (!blur_group || !film_group || !box_choice || !category_picker ||
      blur_group->mapToScene(QPointF()).y() >= film_group->mapToScene(QPointF()).y() ||
      box_choice->mapToScene(QPointF()).y() >= gaussian_choice->mapToScene(QPointF()).y()) {
    std::cerr << "Filters must be grouped by category and alphabetized within each group\n"; return 1;
  }
  QMetaObject::invokeMethod(category_picker, "activated", Q_ARG(int, 1));
  settle();
  if (find("photoEffectGroup_Film") || find("photoEffectLookChoice_dream_haze")) return 1;
  QMetaObject::invokeMethod(category_picker, "activated", Q_ARG(int, 0));
  settle();
  choice = find("photoEffectChoice_gaussian_blur");
  QMetaObject::invokeMethod(choice, "clicked");
  if (!root->property("draft").toBool() || !picker->property("visible").toBool() ||
      !apply->property("enabled").toBool()) return 1;
  if (picker->property("browsing").toBool() || picker->property("dim").toBool()) return 1;
  QVariant committedSize;
  if (!QMetaObject::invokeMethod(size, "testCommitInputText",
                                 Q_RETURN_ARG(QVariant, committedSize),
                                 Q_ARG(QVariant, QVariant(QStringLiteral("23"))))) return 1;
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
  auto* look = find("photoEffectLookChoice_dream_haze");
  auto* search = find("photoEffectSearchField");
  if (!look || !search) return 1;
  QMetaObject::invokeMethod(look, "clicked");
  if (root->property("lookApplications").toInt() != 0 ||
      !picker->property("visible").toBool()) return 1;
  root->setProperty("reject", false);
  search->setProperty("text", QStringLiteral("Optical"));
  settle();
  look = find("photoEffectLookChoice_dream_haze");
  if (!look) return 1;
  QMetaObject::invokeMethod(look, "clicked");
  settle();
  if (root->property("lookApplications").toInt() != 1 ||
      picker->property("visible").toBool()) return 1;
  root->setProperty("hasSavedEffect", true);
  settle();
  auto* bypass = qobject_cast<QQuickItem*>(find("photoEffectEnabled_saved-effect"));
  auto* expand = find("photoEffectExpand_saved-effect");
  auto* collapsedAmount = find("photoGaussianAmountField");
  if (!bypass || !bypass->isVisible() || !expand ||
      !collapsedAmount || collapsedAmount->property("visible").toBool()) {
    std::cerr << "Collapsed effect must expose bypass without its parameter controls\n";
    return 1;
  }
  QMetaObject::invokeMethod(bypass, "toggle");
  settle();
  bypass = qobject_cast<QQuickItem*>(find("photoEffectEnabled_saved-effect"));
  if (root->property("effectEnabled").toBool() || bypass->property("checked").toBool() ||
      root->property("effectAmount").toDouble() != 0.4 || root->property("bypassEdits").toInt() != 1) {
    std::cerr << "Bypass must retain effect settings and update its visible state\n";
    return 1;
  }
  expand = find("photoEffectExpand_saved-effect");
  if (!expand) return 1;
  QMetaObject::invokeMethod(expand, "clicked");
  settle();
  auto* amount = qobject_cast<QQuickItem*>(find("photoGaussianAmountField"));
  auto* radius = qobject_cast<QQuickItem*>(find("photoGaussianSizeField"));
  if (!amount || !radius || !amount->isVisible() ||
      amount->mapToScene(QPointF()).y() >= radius->mapToScene(QPointF()).y()) {
    std::cerr << "Effect amount must be available before advanced parameters\n";
    return 1;
  }
  return 0;
}
