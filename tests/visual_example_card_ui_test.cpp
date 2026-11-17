#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <functional>
#include <iostream>
#include <memory>

namespace {
bool wait_for(const std::function<bool()>& predicate) {
  QElapsedTimer timer;
  timer.start();
  do {
    QTest::qWait(10);
    if (predicate()) return true;
  } while (timer.elapsed() < 3000);
  return false;
}

QString image_source(QColor color) {
  QImage image(160, 108, QImage::Format_RGB32);
  image.fill(color);
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

QQuickItem* find_item(QQuickItem* parent, const QString& name) {
  if (!parent) return nullptr;
  if (parent->objectName() == name) return parent;
  for (auto* child : parent->childItems())
    if (auto* match = find_item(child, name)) return match;
  return nullptr;
}

bool click(QQuickWindow* window, QQuickItem* item) {
  if (!item || !item->isVisible() || !item->isEnabled()) return false;
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    item->mapToScene(item->boundingRect().center()).toPoint());
  QTest::qWait(40);
  return true;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  engine.rootContext()->setContextProperty("sampleSource", image_source(QColor("#e08a46")));
  engine.rootContext()->setContextProperty("sampleOriginalSource", image_source(QColor("#376dc7")));
  engine.rootContext()->setContextProperty("appShell", QVariantMap{
      {"interfaceScale", 1.0}, {"systemUiFontFamily", app.font().family()}});
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Window
import "../apps/desktop/qml/components"
Window {
    id: fixture
    visible: true
    width: 360; height: 700
    color: "#181818"
    property int selections: 0
    property int favorites: 0
    property bool starred: false
    VisualExampleCard {
        objectName: "exampleCard"
        x: 12; y: 12; width: parent.width - 24
        title: "Warm afternoon"
        description: "Warm highlights with soft, lifted shadows."
        thumbnailSource: sampleSource
        originalSource: sampleOriginalSource
        favoriteAvailable: true
        favorite: fixture.starred
        badgeText: "Sample"
        onClicked: fixture.selections++
        onFavoriteToggled: { fixture.favorites++; fixture.starred = !fixture.starred }
    }
}
)qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/visual-example-card.qml")));
  std::unique_ptr<QObject> object(component.create());
  if (!object) return fail(qPrintable(component.errorString()));
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (!window) return fail("Create native card window");
  window->requestActivate();
  auto* card = find_item(window->contentItem(), QStringLiteral("exampleCard"));
  if (!card || !wait_for([&] { return card->property("previewReady").toBool(); }))
    return fail("Display the actual processed example image");
  auto* compare = find_item(card, QStringLiteral("exampleCardOriginal"));
  auto* star = find_item(card, QStringLiteral("exampleCardFavorite"));
  auto* title = find_item(card, QStringLiteral("exampleCardTitle"));
  if (!compare || !star || !title) return fail("Expose comparison, favorite, and selection controls");
  if (!click(window, compare) || !card->property("comparingOriginal").toBool() ||
      object->property("selections").toInt() != 0 ||
      !wait_for([&] { return card->property("previewReady").toBool(); }))
    return fail("Original comparison changes the image without selecting or applying");
  if (card->property("displayedSource").toString() != engine.rootContext()->contextProperty("sampleOriginalSource").toString())
    return fail("Original comparison uses original sample pixels");
  if (!click(window, compare) || card->property("comparingOriginal").toBool())
    return fail("Return to the processed sample");
  if (!click(window, star) || object->property("favorites").toInt() != 1 ||
      !card->property("favorite").toBool() || object->property("selections").toInt() != 0)
    return fail("Favoriting is independent of selection and application");
  if (!click(window, title) || object->property("selections").toInt() != 1)
    return fail("Pointer selection uses the card's standard click action");
  card->forceActiveFocus();
  QTest::keyClick(window, Qt::Key_Space);
  QTest::qWait(30);
  if (object->property("selections").toInt() != 2)
    return fail("Keyboard activation selects the card");

  const auto proof_dir = qEnvironmentVariable("PHOTO_VISUAL_CARD_PROOF_DIR");
  if (!proof_dir.isEmpty() && !QDir().mkpath(proof_dir)) return fail("Create card proof directory");
  for (const double scale : {1.0, 2.0}) {
    engine.rootContext()->setContextProperty("appShell", QVariantMap{
        {"interfaceScale", scale}, {"systemUiFontFamily", app.font().family()}});
    window->setWidth(260);
    QTest::qWait(70);
    for (auto* item : {compare, star, title}) {
      const auto bounds = item->mapRectToItem(card, item->boundingRect());
      if (bounds.left() < -1 || bounds.right() > card->width() + 1 ||
          bounds.top() < -1 || bounds.bottom() > card->height() + 1)
        return fail("Card controls fit at narrow widths and larger interface scale");
    }
    if (card->height() > window->height() - card->y()) return fail("Card fits its fixture viewport");
    if (!proof_dir.isEmpty()) {
      if (QGuiApplication::platformName() == "cocoa" && !window->isExposed())
        return fail("Native card must be exposed before capture");
      if (!window->grabWindow().save(proof_dir + "/card-scale-" + QString::number(scale) + ".png"))
        return fail("Save card proof");
    }
  }
  card->setProperty("previewActive", false);
  QTest::qWait(30);
  if (!card->property("displayedSource").toString().isEmpty())
    return fail("Offscreen cards stop requesting image previews");
  card->setEnabled(false);
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    title->mapToScene(title->boundingRect().center()).toPoint());
  if (object->property("selections").toInt() != 2)
    return fail("Disabled targets cannot select or apply a sample");
  std::cout << "Actual images, original comparison, independent favorites, pointer/keyboard selection, and narrow-scale card layout passed\n";
  return 0;
}
