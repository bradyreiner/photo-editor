#include <QGuiApplication>
#include <QDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <iostream>
#include <memory>

namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
void settle() {
  for (int i = 0; i < 8; ++i) { QCoreApplication::processEvents(); QTest::qWait(5); }
}
QQuickItem* find_item(QQuickItem* item, const QString& name) {
  if (item->objectName() == name) return item;
  for (auto* child : item->childItems()) if (auto* found = find_item(child, name)) return found;
  return nullptr;
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QStringList warnings;
  QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError>& errors) {
    for (const auto& error : errors) warnings.append(error.toString());
  });
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import QtQuick.Layouts
import "../apps/desktop/qml/components"
Rectangle {
    width: 360; height: 360; color: "#171b20"
    QtObject {
        id: backend
        property bool exportBusy: false
        property var exportDeliveryFormatIds: ["original"]
        property string exportDeliveryFitMode: "fit"
        readonly property var exportDeliveryFormats: ["original", "square", "portrait", "story", "landscape"].map(
            id => ({id: id, name: id.charAt(0).toUpperCase() + id.slice(1), selected: exportDeliveryFormatIds.indexOf(id) >= 0}))
        function setExportDeliveryFormats(ids) {
            if (ids.length > 0) exportDeliveryFormatIds = ids
        }
        function setExportDeliveryFitMode(mode) { exportDeliveryFitMode = mode }
    }
    ColumnLayout {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top; anchors.margins: 16
        CampaignDeliveryControls {
            id: controls
            Layout.fillWidth: true
            controller: backend
        }
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/campaign-delivery-fixture.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* stage = qobject_cast<QQuickItem*>(object.get());
  if (!stage) return fail(qPrintable(component.errorString()));
  QQuickWindow window;
  window.resize(360, 360);
  stage->setParentItem(window.contentItem());
  window.show(); settle();
  auto click = [&](const QString& name) {
    auto* item = find_item(stage, name);
    if (!item || !item->isVisible() || !item->isEnabled()) return false;
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
        item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
    settle(); return true;
  };
  if (!click(QStringLiteral("campaignDeliveryFormat_square")) ||
      !find_item(stage, QStringLiteral("campaignDeliveryFormat_square"))->property("active").toBool() ||
      !find_item(stage, QStringLiteral("campaignDeliveryFraming_fit"))->isVisible())
    return fail("Pointer selection enables a real delivery format and exposes framing");
  auto* fill = find_item(stage, QStringLiteral("campaignDeliveryFraming_fill"));
  fill->forceActiveFocus();
  QTest::keyClick(&window, Qt::Key_Space); settle();
  if (!fill->property("active").toBool()) return fail("Framing can be selected using the keyboard");
  if (!click(QStringLiteral("campaignDeliveryFormat_original")) ||
      !click(QStringLiteral("campaignDeliveryFormat_square")) ||
      !find_item(stage, QStringLiteral("campaignDeliveryFormat_square"))->property("active").toBool())
    return fail("The last format stays visibly selected when an empty job is rejected");

  const auto proof = qEnvironmentVariable("PHOTO_EDITOR_CAMPAIGN_UI_PROOF_DIR");
  if (!proof.isEmpty() && !QDir().mkpath(proof)) return fail("Create the UI evidence directory");
  for (const int width : {280, 360, 560}) {
    stage->setWidth(width); window.resize(width, 360); settle();
    for (const auto& name : {"original", "square", "portrait", "story", "landscape"}) {
      const auto* item = find_item(stage, QStringLiteral("campaignDeliveryFormat_") + QString::fromLatin1(name));
      if (!item) return fail("All delivery controls remain present");
      const auto bounds = item->mapRectToItem(stage, item->boundingRect());
      if (bounds.left() < 0 || bounds.right() > width + 0.5 || bounds.bottom() > stage->height())
        return fail("Delivery controls wrap within compact and wide inspectors");
    }
    if (!proof.isEmpty() && !window.grabWindow().save(QDir(proof).filePath(QStringLiteral("delivery-%1.png").arg(width))))
      return fail("Capture the rendered production delivery controls");
  }
  auto* controls = find_item(stage, QStringLiteral("campaignDeliveryFormatsSection"));
  auto* backend = controls->property("controller").value<QObject*>();
  backend->setProperty("exportBusy", true); settle();
  if (click(QStringLiteral("campaignDeliveryFormat_story"))) return fail("Busy exports disable delivery mutations");
  controls->setProperty("controller", QVariant::fromValue(static_cast<QObject*>(nullptr))); settle();
  if (controls->isEnabled()) return fail("A detached Board controller leaves its delivery controls inactive");
  if (!warnings.isEmpty()) return fail(qPrintable(warnings.join(QLatin1Char('\n'))));
  std::cout << "Production delivery controls passed pointer, keyboard, last-selection and responsive-layout checks\n";
  return 0;
}
