#include <QDir>
#include <QGuiApplication>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUrl>
#include <QVariantMap>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <utility>

#ifndef PHOTO_EDITOR_SOURCE_DIR
#define PHOTO_EDITOR_SOURCE_DIR "."
#endif

class BlendIfTestController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)

 public:
  explicit BlendIfTestController(QObject* parent = nullptr) : QObject(parent) {
    settings_ = QVariantMap{
        {QStringLiteral("enabled"), false},
        {QStringLiteral("channel"), QStringLiteral("gray")},
        {QStringLiteral("sourceBlackLow"), 0.0},
        {QStringLiteral("sourceBlackHigh"), 0.0},
        {QStringLiteral("sourceWhiteLow"), 1.0},
        {QStringLiteral("sourceWhiteHigh"), 1.0},
        {QStringLiteral("underlyingBlackLow"), 0.0},
        {QStringLiteral("underlyingBlackHigh"), 0.0},
        {QStringLiteral("underlyingWhiteLow"), 1.0},
        {QStringLiteral("underlyingWhiteHigh"), 1.0},
    };
  }

  [[nodiscard]] QVariantMap settings() const { return settings_; }
  [[nodiscard]] QVariantMap lastSettings() const { return last_settings_; }
  [[nodiscard]] int setterCalls() const { return setter_calls_; }
  [[nodiscard]] int beginCalls() const { return begin_calls_; }
  [[nodiscard]] int endCalls() const { return end_calls_; }
  [[nodiscard]] int activeEdits() const { return active_edits_; }
  [[nodiscard]] int maxActiveEdits() const { return max_active_edits_; }
  [[nodiscard]] int invalidMapCalls() const { return invalid_map_calls_; }

  void setInitialSettings(const QVariantMap& settings) {
    settings_ = settings;
    emit settingsChanged();
  }

  Q_INVOKABLE QVariantMap photoLayerBlendIf(int index) const {
    return index == 0 ? settings_ : QVariantMap{};
  }

  Q_INVOKABLE bool setPhotoLayerBlendIf(int index, const QVariantMap& settings) {
    ++setter_calls_;
    last_settings_ = settings;
    if (index != 0 || !valid_map(settings)) {
      ++invalid_map_calls_;
      return false;
    }
    settings_ = settings;
    emit settingsChanged();
    return true;
  }

  Q_INVOKABLE void beginCoalescedEdit() {
    ++begin_calls_;
    ++active_edits_;
    max_active_edits_ = std::max(max_active_edits_, active_edits_);
  }

  Q_INVOKABLE void endCoalescedEdit() {
    ++end_calls_;
    active_edits_ = std::max(0, active_edits_ - 1);
  }

 signals:
  void settingsChanged();

 private:
  [[nodiscard]] static bool valid_map(const QVariantMap& map) {
    static const QStringList required{
        QStringLiteral("enabled"), QStringLiteral("channel"),
        QStringLiteral("sourceBlackLow"), QStringLiteral("sourceBlackHigh"),
        QStringLiteral("sourceWhiteLow"), QStringLiteral("sourceWhiteHigh"),
        QStringLiteral("underlyingBlackLow"), QStringLiteral("underlyingBlackHigh"),
        QStringLiteral("underlyingWhiteLow"), QStringLiteral("underlyingWhiteHigh")};
    if (map.size() != required.size()) return false;
    for (const auto& key : required)
      if (!map.contains(key)) return false;
    if (map.value(QStringLiteral("channel")).toString() != QStringLiteral("gray") &&
        map.value(QStringLiteral("channel")).toString() != QStringLiteral("red") &&
        map.value(QStringLiteral("channel")).toString() != QStringLiteral("green") &&
        map.value(QStringLiteral("channel")).toString() != QStringLiteral("blue")) {
      return false;
    }
    const auto pairs = std::array{
        std::pair{QStringLiteral("sourceBlackLow"), QStringLiteral("sourceBlackHigh")},
        std::pair{QStringLiteral("sourceWhiteLow"), QStringLiteral("sourceWhiteHigh")},
        std::pair{QStringLiteral("underlyingBlackLow"), QStringLiteral("underlyingBlackHigh")},
        std::pair{QStringLiteral("underlyingWhiteLow"), QStringLiteral("underlyingWhiteHigh")}};
    for (const auto& [low, high] : pairs) {
      const auto low_value = map.value(low).toDouble();
      const auto high_value = map.value(high).toDouble();
      if (!std::isfinite(low_value) || !std::isfinite(high_value) ||
          low_value < 0.0 || high_value > 1.0 || low_value > high_value) {
        return false;
      }
    }
    return true;
  }

  QVariantMap settings_;
  QVariantMap last_settings_;
  int setter_calls_ = 0;
  int begin_calls_ = 0;
  int end_calls_ = 0;
  int active_edits_ = 0;
  int max_active_edits_ = 0;
  int invalid_map_calls_ = 0;
};

namespace {

void settle(int milliseconds = 40) {
  QTest::qWait(milliseconds);
}

QQuickItem* find_visual(QQuickItem* parent, const QString& name) {
  if (parent == nullptr) return nullptr;
  if (parent->objectName() == name) return parent;
  for (auto* child : parent->childItems())
    if (auto* match = find_visual(child, name)) return match;
  return nullptr;
}

QQuickItem* find_item(QQuickWindow* window, const QString& name) {
  if (window == nullptr) return nullptr;
  if (auto* item = find_visual(window->contentItem(), name)) return item;
  return window->findChild<QQuickItem*>(name);
}

bool click_item(QQuickWindow* window, const QString& name) {
  auto* item = find_item(window, name);
  if (window == nullptr || item == nullptr || !item->isVisible() || !item->isEnabled()) {
    std::cerr << "Unavailable control: " << name.toStdString() << '\n';
    return false;
  }
  const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
  settle();
  return true;
}

[[nodiscard]] bool near(double left, double right, double tolerance = 0.025) {
  return std::abs(left - right) <= tolerance;
}

[[nodiscard]] bool is_default_map(const QVariantMap& settings) {
  return settings.value(QStringLiteral("enabled")).toBool() == false &&
      settings.value(QStringLiteral("channel")).toString() == QStringLiteral("gray") &&
      near(settings.value(QStringLiteral("sourceBlackLow")).toDouble(), 0.0) &&
      near(settings.value(QStringLiteral("sourceBlackHigh")).toDouble(), 0.0) &&
      near(settings.value(QStringLiteral("sourceWhiteLow")).toDouble(), 1.0) &&
      near(settings.value(QStringLiteral("sourceWhiteHigh")).toDouble(), 1.0) &&
      near(settings.value(QStringLiteral("underlyingBlackLow")).toDouble(), 0.0) &&
      near(settings.value(QStringLiteral("underlyingBlackHigh")).toDouble(), 0.0) &&
      near(settings.value(QStringLiteral("underlyingWhiteLow")).toDouble(), 1.0) &&
      near(settings.value(QStringLiteral("underlyingWhiteHigh")).toDouble(), 1.0);
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

bool drag_slider(
    QQuickWindow* window,
    const QString& object_name,
    double destination,
    int movement_steps = 5) {
  auto* slider = find_item(window, object_name);
  if (window == nullptr || slider == nullptr || !slider->isVisible() || !slider->isEnabled()) {
    std::cerr << "Unavailable slider: " << object_name.toStdString() << '\n';
    return false;
  }
  const auto handle_variant = slider->property("handle");
  auto* handle = qvariant_cast<QQuickItem*>(handle_variant);
  const auto left_padding = slider->property("leftPadding").toDouble();
  const auto available_width = slider->property("availableWidth").toDouble();
  const auto handle_width = handle != nullptr ? handle->width() : 0.0;
  if (available_width <= handle_width || slider->height() <= 0.0) return false;

  const auto start = handle != nullptr
      ? handle->mapToScene(QPointF(handle->width() / 2.0, handle->height() / 2.0)).toPoint()
      : slider->mapToScene(QPointF(left_padding + handle_width / 2.0,
            slider->height() / 2.0)).toPoint();
  const auto bounded_destination = std::clamp(destination, 0.0, 1.0);
  const auto target_x = left_padding + handle_width / 2.0 +
      bounded_destination * (available_width - handle_width);
  const auto finish = slider->mapToScene(QPointF(target_x, slider->height() / 2.0)).toPoint();
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
  for (int step = 1; step <= std::max(1, movement_steps); ++step) {
    const auto t = static_cast<double>(step) / std::max(1, movement_steps);
    const auto point = (QPointF(start) + (QPointF(finish) - QPointF(start)) * t).toPoint();
    QTest::mouseMove(window, point, 20);
  }
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, finish);
  settle(80);
  return true;
}

[[nodiscard]] bool inside_item(QQuickItem* bounds, QQuickItem* item, double epsilon = 1.0) {
  if (bounds == nullptr || item == nullptr || !item->isVisible()) return false;
  const auto rect = item->mapRectToItem(bounds, item->boundingRect());
  return rect.left() >= -epsilon && rect.top() >= -epsilon &&
      rect.right() <= bounds->width() + epsilon &&
      rect.bottom() <= bounds->height() + epsilon;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  BlendIfTestController controller;
  // Legacy/partial layer maps should acquire the feature defaults without
  // dropping existing channel or range values when the first control changes.
  controller.setInitialSettings(QVariantMap{
      {QStringLiteral("enabled"), false},
      {QStringLiteral("channel"), QStringLiteral("red")},
      {QStringLiteral("sourceBlackHigh"), 0.64},
      {QStringLiteral("underlyingWhiteLow"), 0.34},
  });

  QQmlEngine engine;
  QQmlComponent settings_component(&engine);
  settings_component.setData(R"qml(import QtQml
QtObject {
    property real interfaceScale: 1
    property string interfaceDensity: "balanced"
    property string systemUiFontFamily: ""
    property bool reducedMotion: true
    property bool highContrast: false
    property string colorVisionMode: "standard"
})qml", QUrl());
  std::unique_ptr<QObject> settings(settings_component.create());
  if (!settings) {
    std::cerr << settings_component.errorString().toStdString() << '\n';
    return fail("Create application theme settings");
  }
  settings->setProperty("systemUiFontFamily", app.font().family());
  engine.rootContext()->setContextProperty("appShell", settings.get());
  engine.rootContext()->setContextProperty("blendController", &controller);

  QStringList warnings;
  QObject::connect(&engine, &QQmlEngine::warnings,
      [&warnings](const QList<QQmlError>& errors) {
        for (const auto& error : errors) warnings.append(error.toString());
      });

  const auto components_path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
      .filePath(QStringLiteral("apps/desktop/qml/components"));
  const auto components_import = QUrl::fromLocalFile(components_path).toString();
  auto fixture_source = QStringLiteral(R"qml(import QtQuick
import QtQuick.Layouts
import "%1" as Components
Window {
    id: host
    objectName: "photoBlendIfUiHost"
    visible: true
    width: 440
    height: 1240
    color: "#171717"
    property bool layerLocked: false

    Components.PhotoBlendIfControls {
        id: panel
        objectName: "photoBlendIfControls"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: implicitHeight
        controller: blendController
        layerIndex: 0
        locked: host.layerLocked
        layerData: ({ locked: host.layerLocked, blendIf: blendController.settings })
    }
}
)qml").arg(components_import);
  QQmlComponent fixture_component(&engine);
  fixture_component.setData(fixture_source.toUtf8(),
      QUrl::fromLocalFile(QDir::temp().filePath(QStringLiteral("photo-blend-if-ui-fixture.qml"))));
  std::unique_ptr<QObject> fixture(fixture_component.create());
  if (!fixture) {
    std::cerr << fixture_component.errorString().toStdString() << '\n';
    return fail("Instantiate the production Blend If control in QQuickWindow");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* panel = find_item(window, QStringLiteral("photoBlendIfControls"));
  if (window == nullptr || panel == nullptr) return fail("Find the production QQuickWindow and panel");
  settle(120);

  if (!find_item(window, QStringLiteral("photoBlendIfEnable")) ||
      !find_item(window, QStringLiteral("photoBlendIfChannel")) ||
      !find_item(window, QStringLiteral("photoBlendIf_sourceBlackLow")) ||
      !find_item(window, QStringLiteral("photoBlendIf_underlyingWhiteHigh"))) {
    return fail("Production Blend If controls expose stable toggle, channel, and range identities");
  }
  const auto initial = controller.settings();
  if (initial.value(QStringLiteral("enabled")).toBool() ||
      initial.value(QStringLiteral("channel")).toString() != QStringLiteral("red") ||
      !near(initial.value(QStringLiteral("sourceBlackHigh")).toDouble(), 0.64) ||
      !near(initial.value(QStringLiteral("underlyingWhiteLow")).toDouble(), 0.34)) {
    return fail("The production component reads existing partial layer settings without mutating them");
  }
  auto* default_black_low = find_item(window,
      QStringLiteral("photoBlendIf_sourceBlackLowControl"));
  auto* default_white_high = find_item(window,
      QStringLiteral("photoBlendIf_sourceWhiteHighControl"));
  if (default_black_low == nullptr || default_white_high == nullptr ||
      !near(default_black_low->property("from").toDouble(), 0.0, 1.0e-3) ||
      !near(default_black_low->property("to").toDouble(), 1.0, 1.0e-3) ||
      !near(default_black_low->property("visualPosition").toDouble(), 0.0, 1.0e-3) ||
      !near(default_white_high->property("from").toDouble(), 0.0, 1.0e-3) ||
      !near(default_white_high->property("to").toDouble(), 1.0, 1.0e-3) ||
      !near(default_white_high->property("visualPosition").toDouble(), 1.0, 1.0e-3)) {
    return fail("Default Blend If sliders use a full 0..1 track with black low at left and white high at right");
  }

  // Enable and bypass through the actual switch. Each discrete edit should
  // preserve the existing values and close its own coalesced edit.
  if (!click_item(window, QStringLiteral("photoBlendIfEnable")) ||
      !controller.settings().value(QStringLiteral("enabled")).toBool() ||
      controller.settings().value(QStringLiteral("channel")).toString() != QStringLiteral("red") ||
      !near(controller.settings().value(QStringLiteral("sourceBlackHigh")).toDouble(), 0.64) ||
      !near(controller.settings().value(QStringLiteral("underlyingWhiteLow")).toDouble(), 0.34)) {
    return fail("Enabling Blend If writes a complete map and preserves existing ranges");
  }
  if (!click_item(window, QStringLiteral("photoBlendIfEnable")) ||
      controller.settings().value(QStringLiteral("enabled")).toBool() ||
      controller.settings().value(QStringLiteral("channel")).toString() != QStringLiteral("red")) {
    return fail("The switch bypasses both gates without clearing their channel or thresholds");
  }
  if (auto* bypassed_slider = find_item(window,
          QStringLiteral("photoBlendIf_sourceBlackHighControl"));
      bypassed_slider == nullptr || bypassed_slider->isEnabled()) {
    return fail("Bypassing Blend If disables its threshold sliders");
  }
  if (!click_item(window, QStringLiteral("photoBlendIfEnable")))
    return fail("Re-enable Blend If before changing channel and thresholds");

  // Use the ToneComboBox popup and its real option delegate to choose green.
  if (!click_item(window, QStringLiteral("photoBlendIfChannel")))
    return fail("Open the production Blend If channel picker");
  settle(80);
  auto* green_option = find_item(window, QStringLiteral("photoBlendIfChannelOption2"));
  if (green_option == nullptr || !green_option->isVisible() || !green_option->isEnabled())
    return fail("The open channel picker exposes its Green option");
  const auto green_position = green_option->mapToScene(green_option->boundingRect().center()).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, green_position);
  settle(80);
  if (controller.settings().value(QStringLiteral("channel")).toString() != QStringLiteral("green") ||
      !near(controller.settings().value(QStringLiteral("sourceBlackHigh")).toDouble(), 0.64) ||
      !near(controller.settings().value(QStringLiteral("underlyingWhiteLow")).toDouble(), 0.34)) {
    return fail("Changing channel preserves all existing partial range values");
  }

  // Drag the upper split out, then try to move the lower split beyond it.
  // Each pointer gesture must be one coalesced edit and the setter must never
  // receive an inverted pair.
  auto* source_high = find_item(window, QStringLiteral("photoBlendIf_sourceBlackHighControl"));
  if (source_high == nullptr || !source_high->isEnabled())
    return fail("Enabling Blend If makes its range sliders interactive");
  const auto begin_before_high = controller.beginCalls();
  const auto end_before_high = controller.endCalls();
  const auto source_high_to = source_high->property("to").toDouble();
  if (!near(source_high_to, 1.0) ||
      !drag_slider(window, QStringLiteral("photoBlendIf_sourceBlackHighControl"), 0.72)) {
    return fail("Drag the source black high split with the real InspectorSlider");
  }
  if (controller.beginCalls() != begin_before_high + 1 ||
      controller.endCalls() != end_before_high + 1 || controller.activeEdits() != 0 ||
      controller.maxActiveEdits() != 1 || controller.invalidMapCalls() != 0) {
    return fail("A slider drag is one coalesced edit and only sends valid ordered maps");
  }
  const auto high_value = controller.settings().value(QStringLiteral("sourceBlackHigh")).toDouble();
  if (high_value < 0.55 || high_value > 0.9 ||
      !near(controller.settings().value(QStringLiteral("sourceBlackLow")).toDouble(), 0.0)) {
    std::cerr << "Dragged source black high: " << high_value << '\n';
    return fail("Dragging source black high updates its threshold without changing its low endpoint");
  }

  const auto begin_before_low = controller.beginCalls();
  const auto end_before_low = controller.endCalls();
  if (!drag_slider(window, QStringLiteral("photoBlendIf_sourceBlackLowControl"), 1.0))
    return fail("Drag the lower black split toward the high split");
  const auto low_value = controller.settings().value(QStringLiteral("sourceBlackLow")).toDouble();
  const auto paired_high_value = controller.settings().value(QStringLiteral("sourceBlackHigh")).toDouble();
  if (controller.beginCalls() != begin_before_low + 1 ||
      controller.endCalls() != end_before_low + 1 || controller.activeEdits() != 0 ||
      controller.invalidMapCalls() != 0 || low_value > paired_high_value + 1.0e-9 ||
      !near(low_value, paired_high_value, 0.06)) {
    std::cerr << "Source black split after drag: low=" << low_value
              << " high=" << paired_high_value << '\n';
    return fail("Dragging a split endpoint keeps the low/high pair ordered at its bound");
  }
  if (!near(controller.settings().value(QStringLiteral("underlyingWhiteLow")).toDouble(), 0.34) ||
      controller.settings().value(QStringLiteral("channel")).toString() != QStringLiteral("green")) {
    return fail("Dragging one threshold preserves the channel and unrelated underlying ranges");
  }

  // Reset through the shared InspectorSection action and verify all persisted
  // keys return to the disabled identity defaults.
  const auto begin_before_reset = controller.beginCalls();
  const auto end_before_reset = controller.endCalls();
  if (!click_item(window, QStringLiteral("photoBlendIfReset")) ||
      !is_default_map(controller.settings()) || controller.invalidMapCalls() != 0 ||
      controller.beginCalls() != begin_before_reset + 1 ||
      controller.endCalls() != end_before_reset + 1 || controller.activeEdits() != 0) {
    return fail("Blend If reset restores all eight 0/0/1/1 thresholds and closes one edit");
  }

  // Lock the actual host layer and verify controls become disabled before any
  // setter or coalesced-edit call can occur.
  const auto setter_before_lock = controller.setterCalls();
  const auto begin_before_lock = controller.beginCalls();
  const auto end_before_lock = controller.endCalls();
  fixture->setProperty("layerLocked", true);
  settle();
  auto* toggle = find_item(window, QStringLiteral("photoBlendIfEnable"));
  auto* locked_slider = find_item(window, QStringLiteral("photoBlendIf_sourceBlackHighControl"));
  if (toggle == nullptr || locked_slider == nullptr || toggle->isEnabled() || locked_slider->isEnabled())
    return fail("Locked layers disable the feature switch and all range controls");
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
      toggle->mapToScene(toggle->boundingRect().center()).toPoint());
  settle();
  if (controller.setterCalls() != setter_before_lock ||
      controller.beginCalls() != begin_before_lock || controller.endCalls() != end_before_lock ||
      !is_default_map(controller.settings())) {
    return fail("Locked layer clicks cannot mutate Blend If or create history edits");
  }

  fixture->setProperty("layerLocked", false);
  window->setWidth(240);
  settle(120);
  if (std::abs(panel->width() - 240.0) > 1.0)
    return fail("The production Blend If component follows a narrow inspector width");
  for (const auto& name : {
           QStringLiteral("photoBlendIfChannel"),
           QStringLiteral("photoBlendIf_sourceBlackLow"),
           QStringLiteral("photoBlendIf_sourceBlackHigh"),
           QStringLiteral("photoBlendIf_sourceWhiteLow"),
           QStringLiteral("photoBlendIf_sourceWhiteHigh"),
           QStringLiteral("photoBlendIf_underlyingBlackLow"),
           QStringLiteral("photoBlendIf_underlyingBlackHigh"),
           QStringLiteral("photoBlendIf_underlyingWhiteLow"),
           QStringLiteral("photoBlendIf_underlyingWhiteHigh")}) {
    auto* item = find_item(window, name);
    if (item == nullptr || !item->isVisible() || !inside_item(panel, item)) {
      std::cerr << "Narrow-width item is missing or out of bounds: " << name.toStdString() << '\n';
      return fail("All source and underlying controls remain visible and within a narrow inspector");
    }
  }

  if (argc > 1) {
    const auto evidence_directory = QString::fromLocal8Bit(argv[1]);
    if (!QDir().mkpath(evidence_directory) ||
        !window->grabWindow().save(evidence_directory + QStringLiteral("/photo-blend-if-ui.png"))) {
      return fail("Save the optional Blend If UI screenshot");
    }
  }

  settle();
  if (!warnings.isEmpty()) {
    for (const auto& warning : warnings) std::cerr << warning.toStdString() << '\n';
    return fail("The Blend If production QML must render and interact without QML warnings");
  }
  std::cout << "Production Blend If QQuickWindow interactions, map preservation, ordered split drag, coalescing, reset, lock, and narrow layout passed\n";
  return 0;
}

#include "photo_blend_if_ui_test.moc"
