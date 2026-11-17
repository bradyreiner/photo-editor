#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <iostream>
#include <memory>

namespace {
void settle(int ms = 20) {
  QElapsedTimer clock;
  clock.start();
  while (clock.elapsed() < ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(2);
  }
}
int fail(const QString& message) { std::cerr << message.toStdString() << '\n'; return 1; }
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlPropertyMap settings;
  settings.insert(QStringLiteral("reducedMotion"), false);
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), &settings);
  QStringList warnings;
  QObject::connect(&engine, &QQmlEngine::warnings, [&warnings](const QList<QQmlError>& errors) {
    for (const auto& error : errors) warnings.append(error.toString());
  });
  QQuickWindow window;
  window.resize(480, 260);
  window.show();
  settle(100);
  const QStringList types{QStringLiteral("OpticalLightField"), QStringLiteral("LooksyAgentCharacter"), QStringLiteral("CrtSignalSurface")};
  int transitions = 0;
  for (const auto& type : types) {
    QQmlComponent component(&engine);
    component.setData((QStringLiteral("import QtQuick\nimport \"file:") + QStringLiteral(PHOTO_EDITOR_SOURCE_DIR) +
        QStringLiteral("/apps/desktop/qml/components\"\n") + type + QStringLiteral(" { width: 180; height: 150 }")).toUtf8(), QUrl());
    for (int cycle = 0; cycle < 40; ++cycle) {
      std::unique_ptr<QObject> object(component.create());
      auto* item = qobject_cast<QQuickItem*>(object.get());
      if (!item) return fail(component.errorString());
      // Loader replacement detaches an item before deferred deletion. Exercise
      // that interval with the old item still alive while another frame runs.
      if (item->property("motionEnabled").toBool()) return fail(type + " animates before it has a window");
      item->setParentItem(window.contentItem());
      settle();
      if (!item->property("motionEnabled").toBool()) return fail(type + " does not resume after attachment");
      item->setVisible(false);
      settle();
      if (item->property("motionEnabled").toBool()) return fail(type + " animates while hidden");
      item->setVisible(true);
      item->setWidth(0);
      settle();
      if (item->property("motionEnabled").toBool()) return fail(type + " animates at zero size");
      item->setWidth(180);
      settle();
      item->setParentItem(nullptr);
      // Inspect the gate before rendering another frame: the old shader node
      // may still be registered with the render-thread animation controller.
      if (item->property("motionEnabled").toBool()) return fail(type + " remains active during detachment");
      settle();
      if (item->property("motionEnabled").toBool()) return fail(type + " keeps animating after window detachment");
      for (auto* child : item->findChildren<QObject*>())
        if (child->property("uniform").isValid() && child->property("running").toBool())
          return fail(type + " leaves a uniform animator running after detachment");
      item->setParentItem(window.contentItem());
      settle();
      if (!item->property("motionEnabled").toBool()) return fail(type + " does not resume after reattachment");
      if (cycle == 0) {
        window.hide();
        settle();
        if (item->property("motionEnabled").toBool()) return fail(type + " animates in a hidden window");
        window.show();
        settle();
        if (!item->property("motionEnabled").toBool()) return fail(type + " does not resume with its window");
      }
      settings.insert(QStringLiteral("reducedMotion"), true);
      settle();
      if (item->property("motionEnabled").toBool()) return fail(type + " ignores reduced motion");
      settings.insert(QStringLiteral("reducedMotion"), false);
      settle();
      // Also destroy a running effect without hiding it first.
      object.reset();
      settle();
      transitions += 8;
    }
  }
  if (!warnings.isEmpty()) return fail(warnings.join('\n'));
  std::cout << transitions << " shader lifecycle transitions passed on "
            << QGuiApplication::platformName().toStdString() << '\n';
  return 0;
}
