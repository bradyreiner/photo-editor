#include <QGuiApplication>
#include <QFontDatabase>
#include <QColor>
#include <QFont>
#include <QDir>
#include <QImage>
#include <QQmlError>
#include <QSignalSpy>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QThread>
#include <QVariant>
#include <QStringList>
#include <QtTest/QTest>
#include <cmath>
#include <iostream>
#include <memory>

namespace {
bool capture_window(QQuickWindow& window, const QString& directory, const QString& filename) {
  if (directory.isEmpty())
    return true;
  if (!window.isExposed())
    return false;
  if (!QDir().mkpath(directory))
    return false;
  const auto image = window.grabWindow();
  return !image.isNull() && image.save(QDir(directory).filePath(filename), "PNG");
}

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

void type_text(QQuickWindow& window, QQuickItem* target, const QString& text) {
  if (target != nullptr)
    target->forceActiveFocus(Qt::OtherFocusReason);
  for (const auto character : text) {
    if (character.unicode() <= 0xff)
      QTest::keyClick(&window, character.toLatin1(), Qt::NoModifier, 1);
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
  app.setFont(QFont(QStringLiteral("Helvetica Neue")));
  for (const auto& font : {
           QStringLiteral("dmsans/DMSans[opsz,wght].ttf"),
           QStringLiteral("fraunces/Fraunces[SOFT,WONK,opsz,wght].ttf"),
           QStringLiteral("spacemono/SpaceMono-Regular.ttf")}) {
    if (QFontDatabase::addApplicationFont(
            QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/assets/fonts/") + font) < 0)
      return fail("Load the shipped fonts used by the font-picker specimen");
  }

  QQmlEngine engine;
  QStringList qmlWarnings;
  QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                   [&qmlWarnings](const QList<QQmlError>& warnings) {
                     for (const auto& warning : warnings) {
                       qmlWarnings.push_back(warning.toString());
                       std::cerr << "QML warning: " << warning.toString().toStdString() << '\n';
                     }
                   });
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
  picker->setProperty("previewText", QStringLiteral("Looksy display 0123"));
  picker->setProperty("availableFonts", QStringList{
      QStringLiteral("DM Sans"), QStringLiteral("Fraunces"), QStringLiteral("Space Mono")});

  QSignalSpy selected(picker, SIGNAL(familySelected(QString)));
  if (!selected.isValid())
    return fail("PhotoFontPicker should expose a familySelected signal");

  QQuickWindow window;
  window.resize(640, 680);
  window.setColor(QColor(QStringLiteral("#111111")));
  picker->setSize(QSizeF(360, 42));
  picker->setX(120);
  picker->setY(120);
  picker->setParentItem(window.contentItem());
  window.show();
  window.requestActivate();
  settle();
  const auto proof_directory = qEnvironmentVariable("PHOTO_FONT_PICKER_UI_PROOF_DIR");

  auto* trigger = qobject_cast<QQuickItem*>(find_object(picker, QStringLiteral("pickerProofTrigger")));
  if (trigger == nullptr)
    return fail("PhotoFontPicker should expose its font trigger");
  click_item(window, trigger);
  auto* popup = find_object(picker, QStringLiteral("pickerProofPopup"));
  auto* search = find_object(picker, QStringLiteral("pickerProofSearch"));
  auto* list = find_object(picker, QStringLiteral("pickerProofList"));
  auto* list_item = qobject_cast<QQuickItem*>(list);
  auto* match_status = find_object(picker, QStringLiteral("pickerProofMatchStatus"));
  if (popup == nullptr || search == nullptr || list_item == nullptr || match_status == nullptr)
    return fail("Font popup should expose search, list, and match status controls");
  if (!popup->property("visible").toBool())
    return fail("Font trigger should open the picker popup");
  if (popup->property("modal").toBool() || popup->property("dim").toBool())
    return fail("Font picker should remain nonmodal and leave the canvas undimmed");
  if (std::abs(popup->property("x").toReal() - picker->x()) > 2.0 ||
      popup->property("y").toReal() <= picker->y() + picker->height())
    return fail("Font picker should open below and align with its trigger");
  if (!search->property("text").toString().isEmpty())
    return fail("Font picker should open with an empty search");
  auto* specimen = find_object(picker, QStringLiteral("pickerProofSpecimenText"));
  auto* specimen_family = find_object(picker, QStringLiteral("pickerProofSpecimenFamily"));
  auto* current_sample = find_object(picker, QStringLiteral("pickerProofOption1Sample"));
  if (specimen == nullptr || specimen->property("text").toString() != QStringLiteral("Looksy display 0123"))
    return fail("Font picker should show supplied text in its large specimen preview");
  if (list->property("currentIndex").toInt() != 1 || specimen_family == nullptr ||
      specimen_family->property("text").toString() != QStringLiteral("Fraunces") ||
      specimen->property("font").value<QFont>().family().compare(QStringLiteral("Fraunces"), Qt::CaseInsensitive) != 0) {
    std::cerr << "Initial font index=" << list->property("currentIndex").toInt()
              << " specimen family=" << (specimen_family ? specimen_family->property("text").toString().toStdString() : "missing")
              << " font=" << specimen->property("font").value<QFont>().family().toStdString()
              << " active=" << window.isActive() << '\n';
    return fail("Opening should highlight and preview the current font family");
  }
  if (current_sample == nullptr ||
      current_sample->property("font").value<QFont>().family().compare(QStringLiteral("Fraunces"), Qt::CaseInsensitive) != 0)
    return fail("Each font row should render its sample in that family");
  if (!capture_window(window, proof_directory, QStringLiteral("font-picker-open.png")))
    return fail("The open font-picker screenshot should be captured");

  type_text(window, qobject_cast<QQuickItem*>(search), QStringLiteral("Mono"));
  if (list->property("count").toInt() != 1)
    return fail("Font search should filter the virtualized list to matching families");
  QTest::keyClick(&window, Qt::Key_Down);
  settle();
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
  type_text(window, qobject_cast<QQuickItem*>(search), QStringLiteral("missing-family"));
  if (list->property("count").toInt() != 0 ||
      !match_status->property("text").toString().contains(QStringLiteral("No fonts")))
    return fail("A search with no results should show an empty state");
  QTest::keyClick(&window, Qt::Key_Escape);
  settle();
  if (popup->property("visible").toBool())
    return fail("Escape should close the font picker");

  window.resize(640, 300);
  settle();
  picker->setY(window.height() - picker->height() - 18);
  click_item(window, trigger);
  if (!popup->property("visible").toBool() || popup->property("y").toReal() >= picker->y())
    return fail("Font picker should flip above its trigger near the lower viewport edge");
  if (specimen->property("visible").toBool() || !search->property("visible").toBool() ||
      list_item->height() < 20)
    return fail("A compact font picker should hide the specimen while keeping search and list usable");
  if (!capture_window(window, proof_directory, QStringLiteral("font-picker-compact.png")))
    return fail("The compact font-picker screenshot should be captured");
  QTest::keyClick(&window, Qt::Key_Escape);
  settle();
  window.resize(640, 420);
  settle();
  picker->setY(120);

  picker->setProperty("availableFonts", QStringList{
      QStringLiteral(".SF Georgian"), QStringLiteral("Georgia Pro"), QStringLiteral("Georgia")});
  click_item(window, trigger);
  type_text(window, qobject_cast<QQuickItem*>(search), QStringLiteral("Georgia"));
  QTest::keyClick(&window, Qt::Key_Return);
  settle();
  if (selected.count() != 1 || selected.takeFirst().at(0).toString() != QStringLiteral("Georgia"))
    return fail("An exact family name should rank ahead of partial matches for keyboard selection");

  QQmlComponent inventoryComponent(&engine);
  inventoryComponent.setData(QByteArrayLiteral(
      "import QtQml\n"
      "QtObject { property int callCount: 0; signal photoTextFontsChanged(); "
      "function availablePhotoTextFonts() { callCount += 1; return ['Avenir Next', 'Baskerville'] } }"),
      QUrl(QStringLiteral("file:///FontInventory.qml")));
  std::unique_ptr<QObject> inventory(inventoryComponent.create());
  if (inventory == nullptr)
    return fail("A QML font inventory fixture should instantiate");
  picker->setProperty("availableFonts", QStringList{});
  picker->setProperty("controller", QVariant::fromValue(static_cast<QObject*>(inventory.get())));
  click_item(window, trigger);
  if (inventory->property("callCount").toInt() != 1 || list->property("count").toInt() != 2)
    return fail("Font picker should load controller families when no explicit list is supplied");
  QTest::keyClick(&window, Qt::Key_Escape);
  settle();
  click_item(window, trigger);
  if (inventory->property("callCount").toInt() != 1)
    return fail("Reopening should reuse the cached font inventory");
  QTest::keyClick(&window, Qt::Key_Escape);
  settle();
  if (!QMetaObject::invokeMethod(inventory.get(), "photoTextFontsChanged"))
    return fail("The font inventory fixture should expose its change signal");
  click_item(window, trigger);
  if (inventory->property("callCount").toInt() != 2)
    return fail("A font-database change signal should invalidate the cached inventory");
  QTest::keyClick(&window, Qt::Key_Escape);
  settle();
  picker->setProperty("controller", QVariant::fromValue(static_cast<QObject*>(nullptr)));
  picker->setProperty("controller", QVariant::fromValue(static_cast<QObject*>(inventory.get())));
  click_item(window, trigger);
  if (inventory->property("callCount").toInt() != 3 || list->property("count").toInt() != 2)
    return fail("Changing the font controller should invalidate the cached inventory");
  QTest::keyClick(&window, Qt::Key_Escape);
  settle();

  picker->setEnabled(false);
  if (trigger->isEnabled())
    return fail("A disabled picker should disable its font trigger");
  click_item(window, trigger);
  if (popup->property("visible").toBool())
    return fail("A disabled picker should not open");
  if (!qmlWarnings.isEmpty())
    return fail("PhotoFontPicker should instantiate without QML warnings");

  std::cout << "PhotoFontPicker anchored popup, specimen preview, font samples, search, keyboard selection, cache, refresh, empty state, and disabled state passed\n";
  return 0;
}
