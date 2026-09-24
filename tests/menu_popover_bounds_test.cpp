#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QVariantList>
#include <iostream>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine, QUrl::fromLocalFile(
      QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/MenuButton.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* button = qobject_cast<QQuickItem*>(object.get());
  if (!button) { std::cerr << component.errorString().toStdString(); return 1; }
  QQuickWindow window;
  button->setParentItem(window.contentItem());
  button->setObjectName("boundsMenu");
  button->setProperty("text", "Layer actions");
  QVariantList items;
  for (auto index = 0; index < 30; ++index) {
    items.append(QVariantMap{{"key", QString::number(index)},
        {"label", index == 0 ? QString("Relink Image…") : QString("Layer action %1").arg(index)},
        {"enabled", index != 5 && index != 9}, {"disabledReason", "Test disabled action"}});
  }
  button->setProperty("menuItems", items);
  auto settle = [] {
    for (auto i = 0; i < 12; ++i) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(5);
    }
  };
  for (const auto& size : {QSize(990, 620), QSize(360, 260)}) {
    window.resize(size); window.show();
    for (const auto& corner : {QPoint(0, 0), QPoint(size.width() - 40, size.height() - 40)}) {
      button->setPosition(corner);
      if (!QMetaObject::invokeMethod(button, "openMenu")) return 1;
      settle();
      auto* popup = object->findChild<QObject*>("boundsMenuPopup");
      auto* content = popup ? qvariant_cast<QQuickItem*>(popup->property("contentItem")) : nullptr;
      if (!content) return 1;
      const auto bounds = content->mapRectToScene(QRectF(0, 0, content->width(), content->height()));
      if (!QRectF(QPointF(0, 0), QSizeF(size)).contains(bounds)) {
        std::cerr << "Menu escaped " << size.width() << 'x' << size.height() << '\n'; return 1;
      }
      QVariant focused;
      if (!QMetaObject::invokeMethod(popup, "focusMenuItem", Q_RETURN_ARG(QVariant, focused), Q_ARG(QVariant, 29)) ||
          !focused.toBool()) return 1;
      settle();
      if (content->property("contentY").toDouble() <= 0 || popup->property("currentFocusIndex").toInt() != 29) return 1;
      if (!QMetaObject::invokeMethod(popup, "focusMenuItem", Q_RETURN_ARG(QVariant, focused), Q_ARG(QVariant, 4)) ||
          !QMetaObject::invokeMethod(popup, "focusNextMenuItem", Q_ARG(QVariant, 1))) return 1;
      if (popup->property("currentFocusIndex").toInt() != 6) return 1;
      if (!QMetaObject::invokeMethod(popup, "focusMenuItem", Q_RETURN_ARG(QVariant, focused), Q_ARG(QVariant, 0))) return 1;
      settle();
      if (content->property("contentY").toDouble() != 0.0) return 1;
      QMetaObject::invokeMethod(button, "closeMenu");
      settle();
    }
  }
  button->setParentItem(nullptr);
  std::cout << "Menu viewport bounds, overflow scrolling, first/last focus and disabled-item navigation passed at both window sizes and corners\n";
  return 0;
}
