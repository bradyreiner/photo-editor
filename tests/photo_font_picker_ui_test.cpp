#include <QGuiApplication>
#include <QKeyEvent>
#include <QSignalSpy>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QThread>
#include <QtTest/QTest>
#include <iostream>
#include <memory>

namespace {
void settle() {
  for (auto index = 0; index < 30; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

QObject* find_object_recursive(QObject* parent, const QString& name, QSet<QObject*>& visited) {
  if (parent == nullptr || visited.contains(parent))
    return nullptr;
  visited.insert(parent);
  if (parent->objectName() == name)
    return parent;
  for (auto* child : parent->children()) {
    if (auto* found = find_object_recursive(child, name, visited); found != nullptr)
      return found;
  }
  if (auto* item = qobject_cast<QQuickItem*>(parent)) {
    for (auto* child : item->childItems()) {
      if (auto* found = find_object_recursive(child, name, visited); found != nullptr)
        return found;
    }
  }
  return nullptr;
}

QObject* find_object(QObject* parent, const QString& name) {
  QSet<QObject*> visited;
  return find_object_recursive(parent, name, visited);
}

void click_item(QQuickWindow& window, QQuickItem* item) {
  const auto point = item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
  settle();
}

void type_text(QQuickWindow& window, const QString& text) {
  for (const auto character : text) {
    QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(character));
    QCoreApplication::sendEvent(&window, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, QString(character));
    QCoreApplication::sendEvent(&window, &release);
  }
  settle();
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QGuiApplication app(argc, argv);

  QQmlEngine engine;
  QQmlComponent component(
      &engine,
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/PhotoFontPicker.qml")));
  std::unique_ptr<QObject> object(component.create());
  if (object == nullptr) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("PhotoFontPicker should instantiate");
  }
  auto* picker = qobject_cast<QQuickItem*>(object.get());
  if (picker == nullptr)
    return fail("PhotoFontPicker should be a visual QML item");
  picker->setObjectName(QStringLiteral("pickerProof"));
  picker->setProperty("triggerObjectName", QStringLiteral("pickerProofTrigger"));
  picker->setProperty("currentFamily", QStringLiteral("Fraunces"));
  picker->setProperty("availableFonts", QStringList{
      QStringLiteral("DM Sans"), QStringLiteral("Fraunces"), QStringLiteral("Space Mono")});

  QSignalSpy selected(picker, SIGNAL(familySelected(QString)));
  if (!selected.isValid())
    return fail("PhotoFontPicker should expose a familySelected signal");

  QQuickWindow window;
  window.resize(640, 420);
  picker->setSize(QSizeF(360, 42));
  picker->setParentItem(window.contentItem());
  window.show();
  settle();

  auto* trigger = qobject_cast<QQuickItem*>(find_object(picker, QStringLiteral("pickerProofTrigger")));
  if (trigger == nullptr)
    return fail("PhotoFontPicker should expose its font trigger");
  click_item(window, trigger);
  auto* popup = find_object(picker, QStringLiteral("pickerProofPopup"));
  auto* search = find_object(picker, QStringLiteral("pickerProofSearch"));
  auto* list = find_object(picker, QStringLiteral("pickerProofList"));
  auto* match_status = find_object(picker, QStringLiteral("pickerProofMatchStatus"));
  if (popup == nullptr || search == nullptr || list == nullptr || match_status == nullptr)
    return fail("Font popup should expose search, list, and match status controls");
  if (!popup->property("visible").toBool())
    return fail("Font trigger should open the picker popup");
  if (!search->property("text").toString().isEmpty())
    return fail("Font picker should open with an empty search");

  type_text(window, QStringLiteral("Mono"));
  if (list->property("count").toInt() != 1)
    return fail("Font search should filter the virtualized list to matching families");
  QTest::keyClick(&window, Qt::Key_Return);
  settle();
  if (selected.count() != 1 || selected.takeFirst().at(0).toString() != QStringLiteral("Space Mono"))
    return fail("Return in the search field should choose the first matching font");
  if (popup->property("visible").toBool())
    return fail("Choosing a font should close the popup");

  picker->setProperty("availableFonts", QStringList{
      QStringLiteral("New Sans"), QStringLiteral("New Serif")});
  click_item(window, trigger);
  settle();
  if (!search->property("text").toString().isEmpty())
    return fail("Reopening the font picker should clear the previous search");
  if (list->property("count").toInt() != 2)
    return fail("Reopening should refresh the available font list");
  type_text(window, QStringLiteral("missing-family"));
  if (list->property("count").toInt() != 0 ||
      !match_status->property("text").toString().contains(QStringLiteral("No fonts")))
    return fail("A search with no results should show an empty state");
  QTest::keyClick(&window, Qt::Key_Escape);
  settle();
  if (popup->property("visible").toBool())
    return fail("Escape should close the font picker");

  picker->setProperty("availableFonts", QStringList{
      QStringLiteral(".SF Georgian"), QStringLiteral("Georgia Pro"), QStringLiteral("Georgia")});
  click_item(window, trigger);
  type_text(window, QStringLiteral("Georgia"));
  QTest::keyClick(&window, Qt::Key_Return);
  settle();
  if (selected.count() != 1 || selected.takeFirst().at(0).toString() != QStringLiteral("Georgia"))
    return fail("An exact family name should rank ahead of partial matches for keyboard selection");

  picker->setEnabled(false);
  if (trigger->isEnabled())
    return fail("A disabled picker should disable its font trigger");
  click_item(window, trigger);
  if (popup->property("visible").toBool())
    return fail("A disabled picker should not open");

  std::cout << "PhotoFontPicker search, exact-match ranking, keyboard selection, refresh, empty state, and disabled state passed\n";
  return 0;
}
