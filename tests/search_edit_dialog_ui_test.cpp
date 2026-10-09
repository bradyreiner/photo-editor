#include <QAccessible>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QThread>
#include <functional>
#include <iostream>
#include <memory>

namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
bool wait_for(const std::function<bool()>& done) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  return done();
}
QQuickItem* find_visual(QQuickItem* parent, const QString& name) {
  if (parent->objectName() == name) return parent;
  for (auto* child : parent->childItems()) {
    if (auto* found = find_visual(child, name)) return found;
  }
  return nullptr;
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);
  QTemporaryDir temporary;
  QImage thumbnail(120, 80, QImage::Format_RGB32);
  thumbnail.fill(QColor("#526475"));
  const auto thumbnail_path = QDir(temporary.path()).filePath("portrait.png");
  if (!thumbnail.save(thumbnail_path)) return fail("Search fixture thumbnail should save");
  QQmlEngine engine;
  engine.rootContext()->setContextProperty("fixtureThumbnail", QUrl::fromLocalFile(thumbnail_path));
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import "file:)" PHOTO_EDITOR_SOURCE_DIR R"(/apps/desktop/qml/components"
ApplicationWindow {
  width: 900; height: 700; visible: true; color: "#161b20"
  QtObject {
    id: fixtureWorkflow
    objectName: "fixtureWorkflow"
    property string phase: "search"
    property string status: "Ready to search the current library view."
    property string confirmationToken: "review-generation-1"
    property string confirmedToken: ""
    property var confirmedIds: []
    property string confirmedInstructions: ""
    property var search: QtObject {
      property bool running: false
      property bool hasMore: false
      property string modelName: "Local vision model"
      property var searchPlan: ({query: "outdoor portraits", editInstructions: "Warm the background."})
      property var resultIds: ["portrait-01", "portrait-02"]
      property var results: [
        {assetId: "portrait-01", fileName: "Summer portrait - approved original with a deliberately long filename.jpg",
         thumbnailSource: fixtureThumbnail, reason: "An outdoor portrait with warm afternoon light."},
        {assetId: "portrait-02", filePath: "/photos/Evening portrait.jpg", thumbnailSource: fixtureThumbnail,
         reason: "A portrait in the current library view."}
      ]
    }
    signal changed()
    function start(instructions, model) { phase = "confirm"; status = "2 matching photos"; changed() }
    function cancel() { phase = "search"; changed() }
    function confirmTargets(token, ids, instructions) {
      confirmedToken = token; confirmedIds = ids; confirmedInstructions = instructions
      return {accepted: true}
    }
  }
  SearchEditDialog { parent: Overlay.overlay; anchors.centerIn: parent; workflow: fixtureWorkflow }
})", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/search-edit-fixture.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (!window) { std::cerr << component.errorString().toStdString(); return 1; }
  auto* dialog = window->findChild<QObject*>("searchEditDialog");
  auto* workflow = window->findChild<QObject*>("fixtureWorkflow");
  auto* input = window->findChild<QQuickItem*>("searchEditInstruction");
  auto* find = window->findChild<QObject*>("searchEditFind");
  auto* confirm = window->findChild<QQuickItem*>("searchEditConfirm");
  if (!dialog || !workflow || !input || !find || !confirm || !QMetaObject::invokeMethod(dialog, "open"))
    return fail("Search & Edit should instantiate and open");
  if (!wait_for([&] { return dialog->property("opened").toBool() && window->activeFocusItem() == input; }))
    return fail("Search & Edit should focus its instruction field");
  auto* background = qvariant_cast<QQuickItem*>(dialog->property("background"));
  auto* accessible = QAccessible::queryAccessibleInterface(background);
  if (!accessible || accessible->role() != QAccessible::Dialog || accessible->text(QAccessible::Name) != "Search & Edit")
    return fail("Search & Edit should expose a named dialog");
  input->setProperty("text", "Find outdoor portraits and warm the background.");
  if (!QMetaObject::invokeMethod(find, "clicked")) return fail("Find photos should enter matching-photo review");
  QQuickItem* first = nullptr;
  if (!wait_for([&] {
        first = find_visual(window->contentItem(), "searchEditIncludePhoto_portrait-01");
        return first && first->isVisible() && window->activeFocusItem() == first;
      })) {
    auto* focus = window->activeFocusItem();
    std::cerr << "Review focus: first=" << (first != nullptr)
              << " visible=" << (first && first->isVisible())
              << " focus=" << (focus ? focus->objectName().toStdString() : "none")
              << " opened=" << dialog->property("opened").toBool() << '\n';
    return fail("Matching-photo review should focus its first selection checkbox");
  }
  auto* selection = QAccessible::queryAccessibleInterface(first);
  if (!selection || selection->role() != QAccessible::CheckBox || !selection->state().checked ||
      !selection->text(QAccessible::Name).contains("Summer portrait"))
    return fail("Photo selection should announce the full filename and checked state");
  if (!wait_for([&] {
        QVariant ready;
        return QMetaObject::invokeMethod(dialog, "firstPreviewReady", Q_RETURN_ARG(QVariant, ready)) && ready.toBool();
      })) return fail("Matching-photo thumbnails should load");
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_TEST_SEARCH_EDIT_DIR");
  if (!output.isEmpty() && !QDir().mkpath(output)) return fail("Search review capture directory should be writable");
  for (const auto& size : {QSize(900, 700), QSize(640, 660)}) {
    window->resize(size);
    QElapsedTimer settling;
    settling.start();
    wait_for([&] { return settling.elapsed() >= 180; });
    auto* name = find_visual(window->contentItem(), "searchEditMatchName_portrait-01");
    const QRectF bounds(0, 0, window->width(), window->height());
    if (!name || name->property("text").toString() != "Summer portrait - approved original with a deliberately long filename.jpg" ||
        !bounds.contains(name->mapRectToScene(name->boundingRect())) ||
        !bounds.contains(first->mapRectToScene(first->boundingRect())) ||
        !bounds.contains(confirm->mapRectToScene(confirm->boundingRect())) ||
        name->mapRectToScene(name->boundingRect()).intersects(first->mapRectToScene(first->boundingRect())))
      return fail("Matching filenames, selection, and confirmation should remain contained without overlap");
    if (!output.isEmpty() && !window->grabWindow().save(QDir(output).filePath(QString("matches-%1.png").arg(size.width()))))
      return fail("Search review capture should save");
  }
  first->forceActiveFocus(Qt::TabFocusReason);
  QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &press);
  QCoreApplication::sendEvent(window, &release);
  if (first->property("checked").toBool() || dialog->property("chosenIds").toList().size() != 1 ||
      !confirm->property("text").toString().contains("1 photo"))
    return fail("Keyboard selection should update the displayed target count");
  if (!QMetaObject::invokeMethod(confirm, "clicked") ||
      workflow->property("confirmedToken") != "review-generation-1" ||
      workflow->property("confirmedIds").toList() != QVariantList{QString("portrait-02")} ||
      workflow->property("confirmedInstructions") != "Warm the background.")
    return fail("Confirmation should use the reviewed generation, selected photos, and visible instructions");
  if (!wait_for([&] { return !dialog->property("visible").toBool(); }))
    return fail("Accepted draft creation should dismiss the review");
  std::cout << "Search & Edit: dialog semantics, prompt-to-review focus, thumbnail readiness, compact filename layout, keyboard target selection, and confirmation handoff passed.\n";
  return 0;
}
