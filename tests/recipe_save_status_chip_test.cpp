#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <iostream>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine, QUrl::fromLocalFile(
      QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/RecipeSaveStatusChip.qml")));
  std::unique_ptr<QObject> object(component.createWithInitialProperties({{"savePending", false}, {"saveError", ""}}));
  auto* chip = qobject_cast<QQuickItem*>(object.get());
  if (!chip) { std::cerr << component.errorString().toStdString(); return 1; }
  QQuickWindow window;
  window.resize(330, 100);
  window.setColor(QColor("#161b20"));
  chip->setParentItem(window.contentItem());
  chip->setPosition(QPointF(16, 30));
  chip->setWidth(298);
  window.show();
  const auto settle = [] {
    for (int i = 0; i < 10; ++i) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(5);
    }
  };
  settle();
  if (chip->isVisible()) return 1;
  chip->setProperty("savePending", true);
  settle();
  if (!chip->isVisible() || chip->property("text") != "Saving edits" || !chip->property("busy").toBool()) return 1;
  const QString error = "Recovery checkpoint is unavailable. Existing recovery data was retained.";
  chip->setProperty("saveError", error);
  settle();
  if (chip->property("text") != "Edits not safely saved" || chip->property("tone") != "error" ||
      chip->property("busy").toBool() || chip->property("detailText") != error ||
      chip->property("tooltipText") != error) return 1;
  chip->setProperty("savePending", false);
  settle();
  if (!chip->isVisible()) return 1; // Error survives even without queued work.
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_TEST_STATUS_IMAGE");
  if (!output.isEmpty() && !window.grabWindow().save(output)) return 1;
  chip->setProperty("saveError", "");
  settle();
  if (chip->isVisible()) return 1;
  chip->setParentItem(nullptr);
  std::cout << "Pending, persistent recovery-error, accessible detail and committed/hidden QML states passed.\n";
  return 0;
}
