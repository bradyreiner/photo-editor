#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

#include <QDir>
#include <QElapsedTimer>
#include <QColor>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

std::unique_ptr<QObject> create_component(
    QQmlEngine* engine,
    const QString& file_name) {
  const auto path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                        .filePath(QStringLiteral("apps/desktop/qml/components/") + file_name);
  QQmlComponent component(engine, QUrl::fromLocalFile(path));
  if (component.isError()) {
    for (const auto& error : component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return {};
  }
  return std::unique_ptr<QObject>(component.create());
}

bool queue_value(QObject* control, const QString& key, const double value) {
  return QMetaObject::invokeMethod(
      control,
      "queueValueEdited",
      Q_ARG(QVariant, QVariant(key)),
      Q_ARG(QVariant, QVariant(value)));
}

bool flush_values(QObject* control) {
  return QMetaObject::invokeMethod(control, "flushValueEdits");
}

bool near(const double actual, const double expected) {
  return std::abs(actual - expected) <= 1e-9;
}

bool wait_for_component(QQmlComponent* component) {
  QElapsedTimer timer;
  timer.start();
  while (component->status() == QQmlComponent::Loading && timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  if (component->status() == QQmlComponent::Ready) {
    return true;
  }
  for (const auto& error : component->errors()) {
    std::cerr << error.toString().toStdString() << '\n';
  }
  return false;
}

struct RecordedEdit {
  QString key;
  double value{0.0};
};

class EditRecorder final : public QObject {
  Q_OBJECT

 public:
  QVector<RecordedEdit> edits;

 public slots:
  void record(const QString& key, const double value) {
    edits.push_back({key, value});
  }
};

class RecoverySignalRecorder final : public QObject {
  Q_OBJECT

 public:
  int relink_selected{0};
  int relink_all{0};
  int recheck{0};
  int mark_missing{0};

 public slots:
  void recordRelinkSelected() { ++relink_selected; }
  void recordRelinkAll() { ++relink_all; }
  void recordRecheck() { ++recheck; }
  void recordMarkMissing() { ++mark_missing; }
};

bool expect_edit(
    const QVector<RecordedEdit>& edits,
    const qsizetype index,
    const QString& key,
    const double value) {
  if (index >= edits.size()) {
    return false;
  }
  return edits.at(index).key == key && near(edits.at(index).value, value);
}

bool verify_keyed_queue(QObject* control, const char* owner) {
  EditRecorder recorder;
  if (!QObject::connect(
          control,
          SIGNAL(valueEdited(QString,double)),
          &recorder,
          SLOT(record(QString,double)))) {
    std::cerr << owner << " should expose valueEdited(string, real)\n";
    return false;
  }

  if (!queue_value(control, QStringLiteral("first"), 1.0) || recorder.edits.size() != 1 ||
      !expect_edit(recorder.edits, 0, QStringLiteral("first"), 1.0)) {
    std::cerr << owner << " should publish the leading edit immediately\n";
    return false;
  }
  if (!queue_value(control, QStringLiteral("second"), 2.0) ||
      !queue_value(control, QStringLiteral("first"), 3.0) ||
      !queue_value(control, QStringLiteral("second"), 4.0) || recorder.edits.size() != 1) {
    std::cerr << owner << " should retain pending edits until the trailing flush\n";
    return false;
  }
  if (!flush_values(control) || recorder.edits.size() != 3 ||
      !expect_edit(recorder.edits, 1, QStringLiteral("second"), 4.0) ||
      !expect_edit(recorder.edits, 2, QStringLiteral("first"), 3.0)) {
    std::cerr << owner << " should flush each key once with its latest value\n";
    return false;
  }
  if (!control->property("pendingKeys").toList().isEmpty() ||
      !control->property("pendingValues").toMap().isEmpty()) {
    std::cerr << owner << " should clear pending state after flushing\n";
    return false;
  }
  return true;
}

bool verify_scope_tile(QQmlEngine* engine) {
  QQmlComponent fixture_component(engine);
  fixture_component.setData(
      R"QML(
import QtQuick
Item {
    property string scopeLayoutPreset: "Quad"
    property bool scopeShowRgb: true
    property bool scopeShowLuma: true
    property bool scopeLogHistogram: false
    property bool scopeShowGrid: true
    property bool scopeShowTrace: true
    property bool scopeShowDensity: true
    property color hairline: "#445566"
    property alias previewController: preview
    QtObject {
        id: preview
        property var histogramRed: []
        property var histogramGreen: []
        property var histogramBlue: []
        property var histogramLuminance: []
        property int histogramSampleCount: 0
        property int histogramShadowClipCount: 0
        property int histogramHighlightClipCount: 0
        property var scopeWaveformRed: []
        property var scopeWaveformGreen: []
        property var scopeWaveformBlue: []
        property var scopeWaveformLuminance: []
        property var scopeDensityRed: []
        property var scopeDensityGreen: []
        property var scopeDensityBlue: []
        property var scopeDensityLuminance: []
        property var scopeVectorscopePoints: []
        signal histogramChanged()
    }
}
)QML",
      QUrl(QStringLiteral("inmemory:/StudioScopeFixture.qml")));
  if (!wait_for_component(&fixture_component)) {
    return false;
  }
  std::unique_ptr<QObject> fixture(fixture_component.create());
  if (!fixture) {
    for (const auto& error : fixture_component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return false;
  }

  const auto path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                        .filePath(QStringLiteral(
                            "apps/desktop/qml/components/StudioScopeTile.qml"));
  QQmlComponent scope_component(engine, QUrl::fromLocalFile(path));
  if (!wait_for_component(&scope_component)) {
    return false;
  }
  const QVariantMap initial_properties{
      {QStringLiteral("workspace"), QVariant::fromValue(fixture.get())},
      {QStringLiteral("previewController"), fixture->property("previewController")},
      {QStringLiteral("kind"), QStringLiteral("Waveform")},
      {QStringLiteral("large"), true},
  };
  std::unique_ptr<QObject> scope(scope_component.createWithInitialProperties(initial_properties));
  if (!scope) {
    for (const auto& error : scope_component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return false;
  }
  QCoreApplication::processEvents();

  auto* canvas = scope->findChild<QObject*>(QStringLiteral("studioScopeCanvas"));
  if (canvas == nullptr || !canvas->property("visible").toBool() ||
      !near(scope->property("preferredTileHeight").toDouble(), 320.0)) {
    return false;
  }
  for (const auto& kind : {
           QStringLiteral("Parade"),
           QStringLiteral("Vectorscope"),
           QStringLiteral("Waveform")}) {
    scope->setProperty("kind", kind);
    QCoreApplication::processEvents();
    if (!canvas->property("visible").toBool()) {
      return false;
    }
  }
  scope->setProperty("kind", QStringLiteral("Histogram"));
  QCoreApplication::processEvents();
  if (canvas->property("visible").toBool()) {
    return false;
  }
  fixture->setProperty("scopeShowGrid", false);
  fixture->setProperty("scopeShowTrace", false);
  fixture->setProperty("scopeShowDensity", false);
  fixture->setProperty("scopeShowRgb", false);
  fixture->setProperty("scopeShowLuma", false);
  QMetaObject::invokeMethod(
      fixture->property("previewController").value<QObject*>(),
      "histogramChanged");
  QCoreApplication::processEvents();
  return true;
}

bool verify_thumbnail_card(QQmlEngine* engine) {
  QQmlComponent fixture_component(engine);
  fixture_component.setData(
      R"QML(
import QtQuick
Item {
    property color mint: "#69d4b1"
    property color mintSoft: "#a4ead3"
    property color hairlineStrong: "#55616a"
    property bool selectionMode: true
    property alias catalogAssets: assets
    QtObject {
        id: assets
        property int selectedIndex: 2
    }
    function assetThumb(index) { return "" }
    function assetRating(index) { return index === 2 ? 3 : 0 }
    function assetPicked(index) { return index === 2 }
    function assetRejected(index) { return false }
    function aspectRatioText(width, height) { return width + ":" + height }
}

)QML",
      QUrl(QStringLiteral("inmemory:/StudioThumbnailFixture.qml")));
  if (!wait_for_component(&fixture_component)) {
    return false;
  }
  std::unique_ptr<QObject> fixture(fixture_component.create());
  if (!fixture) {
    return false;
  }

  const auto path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                        .filePath(QStringLiteral(
                            "apps/desktop/qml/components/StudioThumbnailCard.qml"));
  QQmlComponent thumbnail_component(engine, QUrl::fromLocalFile(path));
  if (!wait_for_component(&thumbnail_component)) {
    return false;
  }
  const QVariantMap initial_properties{
      {QStringLiteral("workspace"), QVariant::fromValue(fixture.get())},
      {QStringLiteral("assetIndex"), 2},
  };
  std::unique_ptr<QObject> card(
      thumbnail_component.createWithInitialProperties(initial_properties));
  if (!card) {
    for (const auto& error : thumbnail_component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return false;
  }
  QCoreApplication::processEvents();

  if (!card->property("selected").toBool() ||
      card->property("assetRating").toInt() != 3 ||
      !card->property("assetPicked").toBool() ||
      card->property("assetRejected").toBool() ||
      card->metaObject()->indexOfSignal("activated()") < 0 ||
      card->metaObject()->indexOfSignal("doubleActivated()") < 0 ||
      card->findChild<QObject*>(QStringLiteral("luminanceRangeSamplePatch")) != nullptr) {
    return false;
  }
  card->setProperty("filmstrip", true);
  QCoreApplication::processEvents();
  const auto* image = card->findChild<QObject*>(QStringLiteral("filmstripImage"));
  const auto* placeholder = card->findChild<QObject*>(QStringLiteral("filmstripPlaceholder"));
  const auto* selection =
      card->findChild<QObject*>(QStringLiteral("filmstripSelectionIndicator"));
  return image != nullptr && placeholder != nullptr && selection != nullptr &&
         placeholder->property("visible").toBool() &&
         selection->property("visible").toBool();
}

bool verify_offline_recovery_banner(QQmlEngine* engine) {
  auto theme = create_component(engine, QStringLiteral("Theme.qml"));
  if (!theme) {
    return false;
  }
  const auto path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                        .filePath(QStringLiteral(
                            "apps/desktop/qml/components/StudioOfflineRecoveryBanner.qml"));
  QQmlComponent component(engine, QUrl::fromLocalFile(path));
  if (!wait_for_component(&component)) {
    return false;
  }
  const QVariantMap initial_properties{
      {QStringLiteral("studioTheme"), QVariant::fromValue(theme.get())},
      {QStringLiteral("pageName"), QStringLiteral("edit")},
      {QStringLiteral("selectedAssetId"), QStringLiteral("asset-offline")},
      {QStringLiteral("availabilityState"), QStringLiteral("moved-file")},
      {QStringLiteral("availabilityTitle"), QStringLiteral("Original moved")},
      {QStringLiteral("availabilityDetail"), QStringLiteral("Relink the original")},
      {QStringLiteral("importModeOpen"), false},
      {QStringLiteral("missingConfirmationArmed"), false},
      {QStringLiteral("accentColor"), QColor(QStringLiteral("#69d4b1"))},
      {QStringLiteral("dimTextColor"), QColor(QStringLiteral("#84919a"))},
  };
  std::unique_ptr<QObject> banner(component.createWithInitialProperties(initial_properties));
  if (!banner) {
    for (const auto& error : component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return false;
  }
  QCoreApplication::processEvents();

  auto* relink_selected =
      banner->findChild<QObject*>(QStringLiteral("offlineRelinkSelectedButton"));
  auto* relink_all = banner->findChild<QObject*>(QStringLiteral("offlineRelinkAllButton"));
  auto* recheck = banner->findChild<QObject*>(QStringLiteral("offlineRecheckButton"));
  auto* mark_missing =
      banner->findChild<QObject*>(QStringLiteral("offlineMarkMissingButton"));
  if (!banner->property("visible").toBool() ||
      banner->property("recoverySurface").toString() != QStringLiteral("edit") ||
      banner->property("recoveryState").toString() != QStringLiteral("moved-file") ||
      relink_selected == nullptr || relink_all == nullptr || recheck == nullptr ||
      mark_missing == nullptr) {
    return false;
  }

  RecoverySignalRecorder recorder;
  if (!QObject::connect(
          banner.get(), SIGNAL(relinkSelectedRequested()), &recorder,
          SLOT(recordRelinkSelected())) ||
      !QObject::connect(
          banner.get(), SIGNAL(relinkAllRequested()), &recorder,
          SLOT(recordRelinkAll())) ||
      !QObject::connect(
          banner.get(), SIGNAL(recheckRequested()), &recorder,
          SLOT(recordRecheck())) ||
      !QObject::connect(
          banner.get(), SIGNAL(markMissingRequested()), &recorder,
          SLOT(recordMarkMissing()))) {
    return false;
  }
  for (auto* button : {relink_selected, relink_all, recheck, mark_missing}) {
    if (!QMetaObject::invokeMethod(button, "clicked")) {
      return false;
    }
  }
  QCoreApplication::processEvents();
  if (recorder.relink_selected != 1 || recorder.relink_all != 1 || recorder.recheck != 1 ||
      recorder.mark_missing != 1) {
    return false;
  }

  banner->setProperty("missingConfirmationArmed", true);
  QCoreApplication::processEvents();
  if (mark_missing->property("text").toString() != QStringLiteral("Confirm Missing")) {
    return false;
  }
  banner->setProperty("pageName", QStringLiteral("export"));
  QCoreApplication::processEvents();
  if (banner->objectName() != QStringLiteral("exportOfflineRecoveryBanner") ||
      relink_selected->objectName() != QStringLiteral("exportOfflineRelinkSelectedButton") ||
      relink_all->objectName() != QStringLiteral("exportOfflineRelinkAllButton") ||
      recheck->objectName() != QStringLiteral("exportOfflineRecheckButton") ||
      mark_missing->objectName() != QStringLiteral("exportOfflineMarkMissingButton") ||
      !banner->property("preservationContract").toString().contains(
          QStringLiteral("surface:export"))) {
    return false;
  }
  banner->setProperty("availabilityState", QStringLiteral("online"));
  QCoreApplication::processEvents();
  return !banner->property("visible").toBool();
}

bool verify_tool_transition_overlay(QQmlEngine* engine) {
  auto theme = create_component(engine, QStringLiteral("Theme.qml"));
  if (!theme) {
    return false;
  }
  const auto path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                        .filePath(QStringLiteral(
                            "apps/desktop/qml/components/StudioToolTransitionOverlay.qml"));
  QQmlComponent component(engine, QUrl::fromLocalFile(path));
  if (!wait_for_component(&component)) {
    return false;
  }
  const QVariantMap initial_properties{
      {QStringLiteral("studioTheme"), QVariant::fromValue(theme.get())},
      {QStringLiteral("transitionActive"), true},
      {QStringLiteral("motionDuration"), 140},
      {QStringLiteral("transitionTitle"), QStringLiteral("Switching to Crop")},
      {QStringLiteral("transitionDetail"), QStringLiteral("Preparing crop controls")},
      {QStringLiteral("accentColor"), QColor(QStringLiteral("#69d4b1"))},
      {QStringLiteral("width"), 900},
      {QStringLiteral("height"), 600},
  };
  std::unique_ptr<QObject> overlay(component.createWithInitialProperties(initial_properties));
  if (!overlay) {
    for (const auto& error : component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return false;
  }
  QCoreApplication::processEvents();

  const auto* title =
      overlay->findChild<QObject*>(QStringLiteral("editToolTransitionTitle"));
  const auto* detail =
      overlay->findChild<QObject*>(QStringLiteral("editToolTransitionDetail"));
  const auto* progress =
      overlay->findChild<QObject*>(QStringLiteral("editToolTransitionProgress"));
  if (!overlay->property("visible").toBool() || title == nullptr || detail == nullptr ||
      progress == nullptr ||
      title->property("text").toString() != QStringLiteral("Switching to Crop") ||
      detail->property("text").toString() != QStringLiteral("Preparing crop controls") ||
      progress->property("progress").toDouble() != -1.0 ||
      !overlay->property("motionContract").toString().contains(
          QStringLiteral("duration:140|overlay-fade:true|interruptible:true"))) {
    return false;
  }
  overlay->setProperty("transitionDetail", QString{});
  QCoreApplication::processEvents();
  if (detail->property("visible").toBool()) {
    return false;
  }
  overlay->setProperty("transitionActive", false);
  QCoreApplication::processEvents();
  return !overlay->property("visible").toBool();
}

#include "studio_bounded_input_test.moc"

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;

  auto detail = create_component(&engine, QStringLiteral("StudioDetailTextureControl.qml"));
  if (!detail || !verify_keyed_queue(detail.get(), "StudioDetailTextureControl")) {
    return fail("detail input bounded-delivery contract failed");
  }

  auto lens = create_component(&engine, QStringLiteral("StudioLensCorrectionControl.qml"));
  if (!lens || !verify_keyed_queue(lens.get(), "StudioLensCorrectionControl")) {
    return fail("lens input bounded-delivery contract failed");
  }
  if (!verify_scope_tile(&engine)) {
    return fail("StudioScopeTile runtime contract failed");
  }
  if (!verify_thumbnail_card(&engine)) {
    return fail("StudioThumbnailCard runtime contract failed");
  }
  if (!verify_offline_recovery_banner(&engine)) {
    return fail("StudioOfflineRecoveryBanner runtime contract failed");
  }
  if (!verify_tool_transition_overlay(&engine)) {
    return fail("StudioToolTransitionOverlay runtime contract failed");
  }

  return EXIT_SUCCESS;
}
