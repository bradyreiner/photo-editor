#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

#ifndef PHOTO_EDITOR_SOURCE_DIR
#define PHOTO_EDITOR_SOURCE_DIR "."
#endif

class VectorMaskTestController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap layerData READ layerData NOTIFY stateChanged)

 public:
  explicit VectorMaskTestController(QObject* parent = nullptr)
      : QObject(parent),
        layer_data_{{QStringLiteral("id"), QStringLiteral("layer-1")},
                    {QStringLiteral("kind"), QStringLiteral("pixel")}} {}

  [[nodiscard]] QVariantMap layerData() const { return layer_data_; }
  [[nodiscard]] int enabledCalls() const { return enabled_calls_; }
  [[nodiscard]] int invertCalls() const { return invert_calls_; }
  [[nodiscard]] int densityCalls() const { return density_calls_; }
  [[nodiscard]] int basisCalls() const { return basis_calls_; }
  [[nodiscard]] int removeCalls() const { return remove_calls_; }
  [[nodiscard]] int beginCalls() const { return begin_calls_; }
  [[nodiscard]] int endCalls() const { return end_calls_; }
  [[nodiscard]] int activeEdits() const { return active_edits_; }
  [[nodiscard]] int maxActiveEdits() const { return max_active_edits_; }
  [[nodiscard]] double lastDensity() const { return last_density_; }
  [[nodiscard]] bool lastEnabled() const { return last_enabled_; }
  [[nodiscard]] bool lastInverted() const { return last_inverted_; }
  [[nodiscard]] QString lastBasis() const { return last_basis_; }

  void setMask(const QVariantMap& mask) {
    layer_data_.insert(QStringLiteral("vectorMask"), mask);
    emit stateChanged();
  }

  void setLayerLocked(bool locked) {
    layer_data_.insert(QStringLiteral("locked"), locked);
    emit stateChanged();
  }

  Q_INVOKABLE bool setPhotoLayerVectorMaskEnabled(int index, bool enabled) {
    if (index != 0 || !hasMask()) return false;
    ++enabled_calls_;
    last_enabled_ = enabled;
    auto mask = currentMask();
    mask.insert(QStringLiteral("enabled"), enabled);
    setMask(mask);
    return true;
  }

  Q_INVOKABLE bool setPhotoLayerVectorMaskInverted(int index, bool inverted) {
    if (index != 0 || !hasMask()) return false;
    ++invert_calls_;
    last_inverted_ = inverted;
    auto mask = currentMask();
    mask.insert(QStringLiteral("invert"), inverted);
    setMask(mask);
    return true;
  }

  Q_INVOKABLE bool setPhotoLayerVectorMaskDensity(int index, double density) {
    if (index != 0 || !hasMask() || !std::isfinite(density)) return false;
    ++density_calls_;
    last_density_ = std::clamp(density, 0.0, 1.0);
    auto mask = currentMask();
    mask.insert(QStringLiteral("density"), last_density_);
    setMask(mask);
    return true;
  }

  Q_INVOKABLE bool setPhotoLayerVectorMaskBasis(int index, const QString& basis) {
    if (index != 0 || !hasMask() ||
        (basis != QStringLiteral("local-layer") &&
         basis != QStringLiteral("source-document")) ||
        currentMask().value(QStringLiteral("basis")).toString() == basis) {
      return false;
    }
    ++basis_calls_;
    last_basis_ = basis;
    auto mask = currentMask();
    mask.insert(QStringLiteral("basis"), basis);
    setMask(mask);
    return true;
  }

  Q_INVOKABLE bool removePhotoLayerVectorMask(int index) {
    if (index != 0 || !hasMask()) return false;
    ++remove_calls_;
    layer_data_.remove(QStringLiteral("vectorMask"));
    emit stateChanged();
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
  void stateChanged();

 private:
  [[nodiscard]] bool hasMask() const {
    return layer_data_.value(QStringLiteral("vectorMask")).canConvert<QVariantMap>();
  }

  [[nodiscard]] QVariantMap currentMask() const {
    return layer_data_.value(QStringLiteral("vectorMask")).toMap();
  }

  QVariantMap layer_data_;
  int enabled_calls_ = 0;
  int invert_calls_ = 0;
  int density_calls_ = 0;
  int basis_calls_ = 0;
  int remove_calls_ = 0;
  int begin_calls_ = 0;
  int end_calls_ = 0;
  int active_edits_ = 0;
  int max_active_edits_ = 0;
  double last_density_ = 1.0;
  bool last_enabled_ = true;
  bool last_inverted_ = false;
  QString last_basis_;
};

namespace {

void settle(int milliseconds = 60) {
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

QQuickItem* find_native_slider(QQuickItem* parent) {
  if (parent == nullptr) return nullptr;
  if (parent->property("handle").isValid() &&
      parent->property("availableWidth").isValid()) return parent;
  for (auto* child : parent->childItems())
    if (auto* match = find_native_slider(child)) return match;
  return nullptr;
}

bool click_item(QQuickWindow* window, const QString& name) {
  auto* item = find_item(window, name);
  if (window == nullptr || item == nullptr || !item->isVisible() ||
      !item->isEnabled()) {
    std::cerr << "Unavailable control: " << name.toStdString() << '\n';
    return false;
  }
  const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
  settle();
  return true;
}

void click_even_if_disabled(QQuickWindow* window, QQuickItem* item) {
  if (window == nullptr || item == nullptr) return;
  const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
  settle();
}

bool inside_item(QQuickItem* bounds, QQuickItem* item) {
  if (bounds == nullptr || item == nullptr || !item->isVisible()) return false;
  const auto rect = item->mapRectToItem(bounds, item->boundingRect());
  return rect.left() >= -1.0 && rect.top() >= -1.0 &&
      rect.right() <= bounds->width() + 1.0 &&
      rect.bottom() <= bounds->height() + 1.0;
}

bool drag_density(QQuickWindow* window, double destination) {
  auto* control = find_item(window, QStringLiteral("photoVectorMaskDensitySlider"));
  auto* slider = find_native_slider(control);
  if (window == nullptr || control == nullptr || slider == nullptr ||
      !control->isVisible() || !control->isEnabled() || !slider->isVisible() ||
      !slider->isEnabled()) return false;
  auto* handle = qvariant_cast<QQuickItem*>(slider->property("handle"));
  const double left_padding = slider->property("leftPadding").toDouble();
  const double available_width = slider->property("availableWidth").toDouble();
  const double handle_width = handle != nullptr ? handle->width() : 0.0;
  if (available_width <= handle_width || slider->height() <= 0.0) return false;
  const auto start = handle != nullptr
      ? handle->mapToScene(QPointF(handle->width() / 2.0, handle->height() / 2.0)).toPoint()
      : slider->mapToScene(QPointF(left_padding + handle_width / 2.0,
                                   slider->height() / 2.0)).toPoint();
  const double bounded = std::clamp(destination, 0.0, 1.0);
  const double target_x = left_padding + handle_width / 2.0 +
      bounded * (available_width - handle_width);
  const auto finish = slider->mapToScene(
      QPointF(target_x, slider->height() / 2.0)).toPoint();
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
  for (int step = 1; step <= 6; ++step) {
    const auto t = static_cast<double>(step) / 6.0;
    const auto point = (QPointF(start) + (QPointF(finish) - QPointF(start)) * t).toPoint();
    QTest::mouseMove(window, point, 15);
  }
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, finish);
  settle(80);
  return true;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

QVariantMap test_mask(const QString& basis = QStringLiteral("local-layer")) {
  return {{QStringLiteral("id"), QStringLiteral("vector-mask-1")},
          {QStringLiteral("basis"), basis},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("invert"), false},
          {QStringLiteral("density"), 1.0},
          {QStringLiteral("path"), QVariantMap{}}};
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  VectorMaskTestController controller;

  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("vectorMaskController"),
                                           &controller);
  const auto components_path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
      .filePath(QStringLiteral("apps/desktop/qml/components"));
  const auto components_import = QUrl::fromLocalFile(components_path).toString();
  const auto fixture_source = QStringLiteral(R"qml(import QtQuick
import "%1" as Components
Window {
    id: host
    visible: true
    width: 360
    height: 560
    color: "#171717"
    property int attachCount: 0
    property int editCount: 0
    Components.PhotoVectorMaskControls {
        id: panel
        objectName: "photoVectorMaskControls"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: implicitHeight
        controller: vectorMaskController
        layerIndex: 0
        layerData: vectorMaskController.layerData
        onAttachRequested: host.attachCount += 1
        onEditRequested: host.editCount += 1
    }
}
)qml").arg(components_import);
  QQmlComponent fixture_component(&engine);
  fixture_component.setData(fixture_source.toUtf8(),
      QUrl::fromLocalFile(QDir::temp().filePath(
          QStringLiteral("photo-vector-mask-ui-fixture.qml"))));
  std::unique_ptr<QObject> fixture(fixture_component.create());
  if (!fixture) {
    std::cerr << fixture_component.errorString().toStdString() << '\n';
    return fail("Instantiate the production vector mask controls in QQuickWindow");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* panel = find_item(window, QStringLiteral("photoVectorMaskControls"));
  if (window == nullptr || panel == nullptr)
    return fail("Find the vector mask panel in the QQuickWindow");
  settle(120);

  QSignalSpy attach_spy(panel, SIGNAL(attachRequested()));
  QSignalSpy edit_spy(panel, SIGNAL(editRequested()));
  auto* create_button = find_item(window, QStringLiteral("photoVectorMaskAttachButton"));
  if (create_button == nullptr || !create_button->isVisible() ||
      !create_button->isEnabled() || !click_item(window,
          QStringLiteral("photoVectorMaskAttachButton")) ||
      attach_spy.count() != 1 || fixture->property("attachCount").toInt() != 1) {
    return fail("Expose a primary create action that enters closed-path authoring");
  }

  controller.setMask(test_mask());
  settle();
  const QStringList attached_controls{
      QStringLiteral("photoVectorMaskEditPathButton"),
      QStringLiteral("photoVectorMaskRemoveButton"),
      QStringLiteral("photoVectorMaskEnabledToggle"),
      QStringLiteral("photoVectorMaskInvertToggle"),
      QStringLiteral("photoVectorMaskFollowLayerButton"),
      QStringLiteral("photoVectorMaskFixedDocumentButton"),
      QStringLiteral("photoVectorMaskDensitySlider")};
  for (const auto& name : attached_controls) {
    auto* item = find_item(window, name);
    if (item == nullptr || !item->isVisible() || !item->isEnabled() ||
        !inside_item(panel, item)) {
      std::cerr << "Missing, disabled, or clipped attached-mask control: "
                << name.toStdString() << '\n';
      return fail("Show responsive and enabled controls for an editable local-layer mask");
    }
  }
  auto* follow_basis = find_item(window,
      QStringLiteral("photoVectorMaskFollowLayerButton"));
  auto* fixed_basis = find_item(window,
      QStringLiteral("photoVectorMaskFixedDocumentButton"));
  if (follow_basis == nullptr || fixed_basis == nullptr ||
      !follow_basis->property("active").toBool() ||
      fixed_basis->property("active").toBool() ||
      !click_item(window, QStringLiteral("photoVectorMaskFixedDocumentButton")) ||
      controller.basisCalls() != 1 ||
      controller.lastBasis() != QStringLiteral("source-document") ||
      !fixed_basis->property("active").toBool() ||
      !click_item(window, QStringLiteral("photoVectorMaskFollowLayerButton")) ||
      controller.basisCalls() != 2 ||
      controller.lastBasis() != QStringLiteral("local-layer") ||
      !follow_basis->property("active").toBool()) {
    return fail("Use real pointer dispatch to switch between follow-layer and fixed-document coordinates");
  }
  if (!click_item(window, QStringLiteral("photoVectorMaskEditPathButton")) ||
      edit_spy.count() != 1 || fixture->property("editCount").toInt() != 1) {
    return fail("Dispatch path editing to the workspace pen-tool flow");
  }
  if (!click_item(window, QStringLiteral("photoVectorMaskEnabledToggle")) ||
      controller.enabledCalls() != 1 || controller.lastEnabled()) {
    return fail("Dispatch mask bypass edits to the layer controller");
  }
  if (!click_item(window, QStringLiteral("photoVectorMaskInvertToggle")) ||
      controller.invertCalls() != 1 || !controller.lastInverted()) {
    return fail("Dispatch mask inversion edits to the layer controller");
  }
  if (!drag_density(window, 0.35) || controller.densityCalls() < 1 ||
      std::abs(controller.lastDensity() - 0.35) > 0.06 ||
      controller.beginCalls() != 1 || controller.endCalls() != 1 ||
      controller.activeEdits() != 0 || controller.maxActiveEdits() != 1) {
    return fail("Coalesce a density drag into one controller history operation");
  }

  controller.setLayerLocked(true);
  settle();
  auto* remove_button = find_item(window, QStringLiteral("photoVectorMaskRemoveButton"));
  auto* enabled_toggle = find_item(window, QStringLiteral("photoVectorMaskEnabledToggle"));
  if (remove_button == nullptr || enabled_toggle == nullptr ||
      remove_button->isEnabled() || enabled_toggle->isEnabled() ||
      follow_basis == nullptr || fixed_basis == nullptr ||
      follow_basis->isEnabled() || fixed_basis->isEnabled()) {
    return fail("Respect layer locks for vector mask edits and removal");
  }
  click_even_if_disabled(window, remove_button);
  if (controller.removeCalls() != 0)
    return fail("Locked removal clicks must not reach the layer controller");

  controller.setLayerLocked(false);
  controller.setMask(test_mask(QStringLiteral("source-document")));
  window->setWidth(300);
  settle();
  auto* basis_guidance = find_item(window,
      QStringLiteral("photoVectorMaskBasisGuidance"));
  auto* edit_button = find_item(window, QStringLiteral("photoVectorMaskEditPathButton"));
  if (basis_guidance == nullptr || !basis_guidance->isVisible() ||
      !basis_guidance->property("text").toString().contains(
          QStringLiteral("Fixed to document")) ||
      edit_button == nullptr || !edit_button->isEnabled() ||
      !click_item(window, QStringLiteral("photoVectorMaskEditPathButton")) ||
      edit_spy.count() != 2 || fixture->property("editCount").toInt() != 2) {
    return fail("Explain source-document basis and keep its editable path available");
  }
  if (!click_item(window, QStringLiteral("photoVectorMaskEnabledToggle")) ||
      controller.enabledCalls() != 2 || controller.lastEnabled() ||
      !click_item(window, QStringLiteral("photoVectorMaskInvertToggle")) ||
      controller.invertCalls() != 2 || !controller.lastInverted()) {
    return fail("Dispatch source-document bypass and inversion edits to the controller");
  }
  if (!drag_density(window, 0.62) || controller.densityCalls() < 2 ||
      std::abs(controller.lastDensity() - 0.62) > 0.06 ||
      controller.beginCalls() != 2 || controller.endCalls() != 2 ||
      controller.activeEdits() != 0 || controller.maxActiveEdits() != 1) {
    return fail("Coalesce source-document density into one controller history operation");
  }
  auto* density_slider_host = find_item(window,
      QStringLiteral("photoVectorMaskDensitySlider"));
  auto* density_slider = find_native_slider(density_slider_host);
  if (density_slider == nullptr) return fail("Find density slider for keyboard history test");
  density_slider->forceActiveFocus();
  QTest::keyClick(window, Qt::Key_Right);
  settle();
  if (controller.beginCalls() != 3 || controller.endCalls() != 3 ||
      controller.activeEdits() != 0 || controller.maxActiveEdits() != 1) {
    return fail("Bound a keyboard density change as its own history operation");
  }

  // Keep the source-document controls visible at a compact inspector width.
  auto* title = find_item(window, QStringLiteral("photoVectorMaskTitle"));
  auto* density_control = find_item(window,
      QStringLiteral("photoVectorMaskDensitySlider"));
  follow_basis = find_item(window,
      QStringLiteral("photoVectorMaskFollowLayerButton"));
  fixed_basis = find_item(window,
      QStringLiteral("photoVectorMaskFixedDocumentButton"));
  if (title == nullptr || density_control == nullptr ||
      follow_basis == nullptr || fixed_basis == nullptr ||
      !inside_item(panel, title) || !inside_item(panel, density_control) ||
      !inside_item(panel, follow_basis) || !inside_item(panel, fixed_basis) ||
      !inside_item(window->contentItem(), panel)) {
    return fail("Keep source-document controls inside a compact inspector width");
  }

  // An optional first argument captures the live narrow-width panel for review.
  const auto arguments = app.arguments();
  if (arguments.size() > 1) {
    const QFileInfo capture_info(arguments.at(1));
    const auto parent_directory = capture_info.absolutePath();
    if (!QDir().mkpath(parent_directory))
      return fail("Create the optional UI capture directory");
    const auto capture = window->grabWindow();
    if (capture.isNull() || !capture.save(capture_info.absoluteFilePath()))
      return fail("Save the optional narrow-width UI capture");
  }

  follow_basis = find_item(window,
      QStringLiteral("photoVectorMaskFollowLayerButton"));
  fixed_basis = find_item(window,
      QStringLiteral("photoVectorMaskFixedDocumentButton"));
  if (follow_basis == nullptr || fixed_basis == nullptr ||
      !fixed_basis->property("active").toBool() ||
      !click_item(window, QStringLiteral("photoVectorMaskFollowLayerButton")) ||
      controller.basisCalls() != 3 ||
      controller.lastBasis() != QStringLiteral("local-layer") ||
      !follow_basis->property("active").toBool()) {
    return fail("Convert fixed-document coordinates back to layer-following coordinates through the basis control");
  }

  if (!click_item(window, QStringLiteral("photoVectorMaskRemoveButton")) ||
      controller.removeCalls() != 1 ||
      find_item(window, QStringLiteral("photoVectorMaskAttachButton")) == nullptr) {
    return fail("Remove a source-document vector mask through the controller");
  }

  std::cout << "photo_vector_mask_ui_test passed\n";
  return 0;
}

#include "photo_vector_mask_ui_test.moc"
