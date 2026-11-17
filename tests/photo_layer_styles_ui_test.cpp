#include "editor_preview_controller.h"
#include "edit_adjustment_patch.h"
#include "photo_editor/pipeline/layer_styles.h"
#include "photo_layer_style_codec.h"

#include <QAccessible>
#include <QColor>
#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlError>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#ifndef PHOTO_EDITOR_SOURCE_DIR
#define PHOTO_EDITOR_SOURCE_DIR "."
#endif

using namespace photo_editor::desktop;

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void settle(int milliseconds = 100) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < milliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
}

void restore_history(EditorPreviewController& editor, bool redo) {
  const auto asset_id = editor.assetId();
  bool finished = false;
  bool succeeded = false;
  const auto connection = QObject::connect(
      &editor, &EditorPreviewController::historyRestoreFinished, &editor,
      [&asset_id, &finished, &succeeded](const QString& restored_asset, bool success) {
        if (restored_asset != asset_id) return;
        finished = true;
        succeeded = success;
      });
  if (redo) editor.redo();
  else editor.undo();
  QElapsedTimer timer;
  timer.start();
  while (!finished && timer.elapsed() < 90000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  QObject::disconnect(connection);
  require(finished && succeeded, "The UI workflow history restore must succeed.");
  settle(180);
}

QQuickItem* visual_by_name(QQuickItem* parent, const QString& name) {
  if (parent == nullptr) return nullptr;
  if (parent->objectName() == name) return parent;
  for (auto* child : parent->childItems()) {
    if (auto* found = visual_by_name(child, name)) return found;
  }
  return nullptr;
}

void collect_visuals(QQuickItem* parent, std::vector<QQuickItem*>* items) {
  if (parent == nullptr || items == nullptr) return;
  items->push_back(parent);
  for (auto* child : parent->childItems()) collect_visuals(child, items);
}

QQuickItem* visual_with_property(QQuickItem* root, const QString& key,
                                 const QVariant& value) {
  std::vector<QQuickItem*> items;
  collect_visuals(root, &items);
  for (auto* item : items) {
    const auto property = item->property(key.toUtf8().constData());
    if (property.isValid() && property == value) return item;
  }
  return nullptr;
}

std::vector<QQuickItem*> visuals_with_text(QQuickItem* root,
                                           const QString& text) {
  std::vector<QQuickItem*> result;
  std::vector<QQuickItem*> items;
  collect_visuals(root, &items);
  for (auto* item : items) {
    const auto property = item->property("text");
    if (property.isValid() && property.toString() == text) result.push_back(item);
  }
  return result;
}

QQuickItem* first_visible_enabled(const std::vector<QQuickItem*>& items) {
  const auto found = std::find_if(items.cbegin(), items.cend(), [](const QQuickItem* item) {
    return item != nullptr && item->isVisible() && item->isEnabled();
  });
  return found == items.cend() ? nullptr : *found;
}

QString style_id(EditorPreviewController& editor, int layer_index,
                 const QString& type_id) {
  for (const auto& value : editor.photoLayerStyles(layer_index)) {
    const auto style = value.toMap();
    if (style.value(QStringLiteral("typeId")).toString() == type_id)
      return style.value(QStringLiteral("id")).toString();
  }
  return {};
}

QVariantMap style_by_id(EditorPreviewController& editor, int layer_index,
                        const QString& id) {
  for (const auto& value : editor.photoLayerStyles(layer_index))
    if (value.toMap().value(QStringLiteral("id")).toString() == id)
      return value.toMap();
  return {};
}

QVariantMap sample_gradient(const QVariantMap& gradient, double position) {
  const auto stops = gradient.value(QStringLiteral("stops")).toList();
  const auto value = gradient.value(QStringLiteral("reverse")).toBool()
      ? 1.0 - position : position;
  int upper = 1;
  while (upper < stops.size() - 1 &&
         stops.at(upper).toMap().value(QStringLiteral("position")).toDouble() < value)
    ++upper;
  const auto left = stops.at(upper - 1).toMap();
  const auto right = stops.at(upper).toMap();
  const auto left_position = left.value(QStringLiteral("position")).toDouble();
  const auto right_position = right.value(QStringLiteral("position")).toDouble();
  const auto ratio = std::clamp((value - left_position) /
      std::max(1e-12, right_position - left_position), 0.0, 1.0);
  const auto left_color = left.value(QStringLiteral("color")).toMap();
  const auto right_color = right.value(QStringLiteral("color")).toMap();
  QVariantMap sampled;
  for (const auto* key : {"r", "g", "b", "a"}) {
    const auto channel = QLatin1String(key);
    sampled.insert(channel, left_color.value(channel).toDouble() * (1.0 - ratio) +
        right_color.value(channel).toDouble() * ratio);
  }
  return sampled;
}

bool click(QQuickWindow* window, QQuickItem* item);
QQuickItem* style_panel_for(QQuickItem* item);
void fit_window_to_style_panel(QQuickWindow* window, QQuickItem* panel);

void expand_style_if_needed(QQuickItem* panel, QQuickWindow* window,
                            const QString& id) {
  if (panel->property("expandedId").toString() == id) return;
  auto* header = visual_by_name(panel, QStringLiteral("layerStyleHeader_") + id);
  require(header != nullptr && click(window, header),
          "The named style header must open its own editable controls.");
}

bool click(QQuickWindow* window, QQuickItem* item) {
  if (window == nullptr || item == nullptr || !item->isVisible() ||
      !item->isEnabled() || item->width() <= 0.0 || item->height() <= 0.0) return false;
  auto* panel = style_panel_for(item);
  fit_window_to_style_panel(window, panel);
  const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
  if (point.x() < 0 || point.y() < 0 || point.x() >= window->width() ||
      point.y() >= window->height()) return false;
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
  settle(120);
  fit_window_to_style_panel(window, panel);
  return true;
}

QVariantMap target_layer(EditorPreviewController& editor, int index) {
  const auto layers = editor.photoLayers();
  require(index >= 0 && index < layers.size(), "The QML style target must exist.");
  return layers.at(index).toMap();
}

bool contains_mask_set(EditorPreviewController& editor, const QString& set_id) {
  for (const auto& value : editor.maskStack())
    if (value.toMap().value(QStringLiteral("maskSetId")).toString() == set_id)
      return true;
  return false;
}

QObject* find_object_with_title(QObject* root, const QString& title) {
  if (root == nullptr) return nullptr;
  const auto candidate = root->property("title");
  if (candidate.isValid() && candidate.toString() == title) return root;
  for (auto* child : root->children()) {
    if (auto* found = find_object_with_title(child, title)) return found;
  }
  return nullptr;
}

bool layout_container(const QQuickItem* item) {
  if (item == nullptr) return false;
  const auto name = QString::fromLatin1(item->metaObject()->className());
  return name.contains(QStringLiteral("Layout")) || name.endsWith(QStringLiteral("Flow"));
}

void verify_no_layout_overlaps(QQuickItem* parent) {
  if (parent == nullptr) return;
  if (layout_container(parent)) {
    const auto children = parent->childItems();
    for (int left_index = 0; left_index < children.size(); ++left_index) {
      auto* left = children.at(left_index);
      if (!left->isVisible() || left->width() <= 0.0 || left->height() <= 0.0) continue;
      const auto left_rect = left->mapRectToItem(parent, left->boundingRect());
      require(left_rect.left() >= -1.0 && left_rect.top() >= -1.0 &&
                  left_rect.right() <= parent->width() + 1.0 &&
                  left_rect.bottom() <= parent->height() + 1.0,
              "A visible control must remain inside its responsive layout bounds.");
      for (int right_index = left_index + 1; right_index < children.size(); ++right_index) {
        auto* right = children.at(right_index);
        if (!right->isVisible() || right->width() <= 0.0 || right->height() <= 0.0) continue;
        const auto right_rect = right->mapRectToItem(parent, right->boundingRect());
        const auto overlap = left_rect.intersected(right_rect);
        require(overlap.width() <= 1.0 || overlap.height() <= 1.0,
                "Sibling controls must not overlap at a narrow panel width.");
      }
    }
  }
  for (auto* child : parent->childItems()) verify_no_layout_overlaps(child);
}

QByteArray future_style_envelope() {
  using namespace photo_editor::pipeline;
  LayerStyleOperation future;
  future.id = "future-style-operation";
  future.type_id = "future_layer_style";
  future.algorithm_version = 9;
  future.enabled = true;
  future.opacity = 1.0;
  future.plane = LayerStylePlane::AboveContent;
  future.blend_mode = LayerStyleBlendMode::Normal;
  future.parameters = std::monostate{};
  future.opaque_parameters = "future-renderer-payload";
  LayerStyleStack stack;
  stack.operations.push_back(future);
  std::string error;
  const auto serialized = serialize_layer_style_stack(stack, &error);
  require(serialized.has_value(), "The fixture must serialize an unsupported future style.");
  QString codec_error;
  const auto bytes = layer_style_codec::encode_envelope(
      QString::fromStdString(*serialized), -45.0, &codec_error);
  require(!bytes.isEmpty(), "The unsupported style must use the standard bounded envelope.");
  return bytes;
}

QString patch_with_future_style(EditorPreviewController& editor, int layer_index,
                                const QByteArray& envelope) {
  const auto parsed = parse_adjustment_patch_json(
      editor.currentAssetBoundAdjustmentPatchJson());
  if (!parsed.ok() || !parsed.state.has_value() || layer_index < 0 ||
      layer_index >= parsed.state->layers.size()) return {};
  QString stack;
  double light_angle = -45.0;
  QString codec_error;
  if (!layer_style_codec::decode_envelope(envelope, &stack, &light_angle,
                                           &codec_error)) return {};
  auto state = *parsed.state;
  auto layer = state.layers.at(layer_index).toMap();
  layer.insert(QStringLiteral("layerStyleStack"), stack);
  layer.insert(QStringLiteral("layerStyleGlobalLightAngle"), light_angle);
  state.layers[layer_index] = layer;
  return serialize_adjustment_patch_json(state, parsed.groups, parsed.scope);
}

void refresh_panel(QQuickItem* panel, EditorPreviewController& editor,
                   int index) {
  panel->setProperty("layerIndex", index);
  panel->setProperty("layerData", target_layer(editor, index));
  panel->setProperty("revision", panel->property("revision").toInt() + 1);
  settle(120);
}

QQuickItem* style_panel_for(QQuickItem* item) {
  for (auto* current = item; current != nullptr; current = current->parentItem())
    if (current->objectName() == QStringLiteral("photoLayerStyles")) return current;
  return nullptr;
}

void fit_window_to_style_panel(QQuickWindow* window, QQuickItem* panel) {
  if (window == nullptr || panel == nullptr) return;
  std::vector<QQuickItem*> items;
  collect_visuals(panel, &items);
  qreal content_bottom = std::max(0.0, panel->implicitHeight());
  for (auto* item : items) {
    if (item == panel || !item->isVisible() || item->width() <= 0.0 ||
        item->height() <= 0.0) continue;
    const auto bounds = item->mapRectToItem(panel, item->boundingRect());
    if (std::isfinite(bounds.bottom())) content_bottom = std::max(content_bottom, bounds.bottom());
  }
  const auto height = std::max(1, static_cast<int>(std::ceil(content_bottom + 8.0)));
  const auto width = std::max(1, static_cast<int>(std::ceil(panel->width())));
  panel->setHeight(height);
  if (window->width() != width || window->height() != height)
    window->resize(width, height);
  settle(60);
}

void set_width(QQuickWindow* window, QQuickItem* panel, int width,
               const QString& proof_directory,
               const QString& state_suffix = {}) {
  window->resize(width, window->height());
  panel->setWidth(width);
  settle(180);
  require(panel->implicitHeight() > 0.0,
          "The style panel must report a packed implicit height.");
  fit_window_to_style_panel(window, panel);
  settle(180);
  const auto frame = window->grabWindow();
  require(!frame.isNull() && frame.width() == width,
          "The style panel must render at every requested responsive width.");
  verify_no_layout_overlaps(panel);
  if (!proof_directory.isEmpty()) {
    QDir directory(proof_directory);
    require((directory.exists() || QDir().mkpath(proof_directory)) &&
                frame.save(directory.filePath(QStringLiteral(
                    "photo-layer-styles-offscreen-%1%2.png").arg(width).arg(
                        state_suffix.isEmpty() ? QString() : QStringLiteral("-") + state_suffix)),
                    "PNG"),
            "The optional offscreen responsive proof image must be writable.");
    std::cout << "Offscreen proof: " << directory.filePath(QStringLiteral(
        "photo-layer-styles-offscreen-%1%2.png").arg(width).arg(
            state_suffix.isEmpty() ? QString() : QStringLiteral("-") + state_suffix))
        .toStdString() << '\n';
  }
}

int run_workflow() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "The offscreen style workflow must create isolated storage.");
  const auto proof_directory = qEnvironmentVariable(
      "PHOTO_EDITOR_LAYER_STYLES_PROOF_DIR");
  QImage source(44, 32, QImage::Format_ARGB32);
  source.fill(QColor(QStringLiteral("#394d61")));
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto source_path = temporary.filePath(QStringLiteral("style-ui-source.png"));
  require(source.save(source_path, "PNG"), "The offscreen source image must be writable.");

  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("photo-layer-styles-ui"), source_path,
                   {}, {}, {}, {}, {}, true);
  // Seed a real canvas source through the ordinary fill path so this UI test
  // remains independent of external photograph fixtures.
  require(editor.addFillLayer(QStringLiteral("#6b8196"), QStringLiteral("First target")),
          "Create the first layer style target.");
  settle();
  const auto first_target_id = target_layer(editor, 0).value(QStringLiteral("id")).toString();
  require(!first_target_id.isEmpty(), "The first style target must have an identity.");

  QQmlEngine engine;
  QStringList qml_warnings;
  QObject::connect(&engine, &QQmlEngine::warnings, &engine,
      [&qml_warnings](const QList<QQmlError>& warnings) {
        for (const auto& warning : warnings) qml_warnings.append(warning.toString());
      });
  QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/PhotoLayerStyles.qml")));
  if (component.isError()) {
    for (const auto& error : component.errors())
      std::cerr << "QML component error: " << error.toString().toStdString() << '\n';
    for (const auto& warning : qml_warnings)
      std::cerr << "QML warning: " << warning.toStdString() << '\n';
  }
  require(!component.isError(), "The production PhotoLayerStyles component must load.");
  QQuickWindow window;
  window.setColor(QColor(QStringLiteral("#111820")));
  window.resize(480, 1000);
  const auto initial_layer = target_layer(editor, 0);
  std::unique_ptr<QObject> object(component.createWithInitialProperties({
      {QStringLiteral("controller"), QVariant::fromValue(static_cast<QObject*>(&editor))},
      {QStringLiteral("layerIndex"), 0},
      {QStringLiteral("layerData"), initial_layer}}));
  require(object != nullptr, "The production style panel must instantiate.");
  auto* panel = qobject_cast<QQuickItem*>(object.get());
  require(panel != nullptr && panel->objectName() == QStringLiteral("photoLayerStyles"),
          "The production style panel must expose its QQuickItem root.");
  panel->setParentItem(window.contentItem());
  panel->setWidth(480);
  window.show();
  window.requestActivate();
  settle(250);
  const auto initial_packed_height =
      static_cast<int>(std::ceil(panel->implicitHeight()));
  require(initial_packed_height > 0,
          "The initial style panel must report a packed implicit height.");
  panel->setHeight(initial_packed_height);
  window.resize(480, initial_packed_height);
  settle(150);
  require(window.isExposed(), "The offscreen style panel window must be exposed for input/rendering.");

  auto* picker = visual_by_name(panel, QStringLiteral("layerStylePicker"));
  auto* add_button = visual_by_name(panel, QStringLiteral("addLayerStyleButton"));
  require(picker != nullptr && add_button != nullptr,
          "The production style type picker and Add button must be discoverable.");
  picker->setProperty("currentIndex", 5);  // Color Overlay.
  require(click(&window, add_button), "The visible Add button must add a color overlay.");
  auto styles = editor.photoLayerStyles(0);
  require(styles.size() == 1 && styles.first().toMap()
              .value(QStringLiteral("typeId")).toString() == QStringLiteral("color_overlay"),
          "The QML Add action must author the selected engine style type.");

  const auto overlay_id = styles.first().toMap().value(QStringLiteral("id")).toString();
  expand_style_if_needed(panel, &window, overlay_id);
  settle();
  auto* opacity = visual_with_property(panel, QStringLiteral("accessibleLabel"),
                                       QStringLiteral("Opacity"));
  require(opacity != nullptr, "The expanded style row must expose numeric opacity entry.");
  auto* text_entry = qvariant_cast<QQuickItem*>(opacity->property("contentItem"));
  require(text_entry != nullptr && click(&window, text_entry),
          "The opacity field must receive text entry.");
  QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
  for (const auto key : {Qt::Key_6, Qt::Key_5, Qt::Key_Period, Qt::Key_0})
    QTest::keyClick(&window, key);
  QTest::keyClick(&window, Qt::Key_Return);
  settle(180);
  require(qAbs(editor.photoLayerStyles(0).first().toMap()
                  .value(QStringLiteral("opacity")).toDouble() - 0.65) < 0.001 &&
              opacity->property("value").toInt() == 650,
          "Committing a numeric field must update the operation through the controller.");
  restore_history(editor, false);
  refresh_panel(panel, editor, 0);
  require(editor.photoLayerStyles(0).first().toMap()
              .value(QStringLiteral("opacity")).toDouble() == 1.0 &&
              opacity->property("value").toInt() == 1000,
          "Undo must update both the style model and the same live numeric control.");
  restore_history(editor, true);
  refresh_panel(panel, editor, 0);
  require(qAbs(editor.photoLayerStyles(0).first().toMap()
                  .value(QStringLiteral("opacity")).toDouble() - 0.65) < 0.001 &&
              opacity->property("value").toInt() == 650,
          "Redo must restore the committed numeric value in the same live control.");

  picker = visual_by_name(panel, QStringLiteral("layerStylePicker"));
  add_button = visual_by_name(panel, QStringLiteral("addLayerStyleButton"));
  require(picker != nullptr && add_button != nullptr,
          "Style controls must remain discoverable after a numeric commit.");
  picker->setProperty("currentIndex", 0);  // Stroke.
  require(click(&window, add_button), "The visible Add button must add a stroke.");
  settle();
  styles = editor.photoLayerStyles(0);
  require(styles.size() == 2 && styles.last().toMap()
              .value(QStringLiteral("typeId")).toString() == QStringLiteral("stroke"),
          "The second UI-added style must append in stack order.");
  const auto stroke_id = styles.last().toMap().value(QStringLiteral("id")).toString();
  auto* move_up = visual_by_name(panel, QStringLiteral("layerStyleUp_") + stroke_id);
  require(move_up != nullptr && move_up->isEnabled() && click(&window, move_up),
          "The second style's Move up button must reorder the stack.");
  styles = editor.photoLayerStyles(0);
  require(styles.first().toMap().value(QStringLiteral("typeId")).toString() ==
              QStringLiteral("stroke"),
          "The Move up control must change the serialized style order.");

  auto* stroke_enabled = visual_by_name(
      panel, QStringLiteral("layerStyleEnabled_") + stroke_id);
  require(stroke_enabled != nullptr && click(&window, stroke_enabled),
          "The visible style bypass checkbox must disable one operation.");
  require(!editor.photoLayerStyles(0).first().toMap()
              .value(QStringLiteral("enabled")).toBool(),
          "Disabling a style from QML must update the authored stack.");

  auto* fill_kind = visual_by_name(panel, QStringLiteral("layerStyleFillKind_") + stroke_id);
  const auto history_before_fill_kind = editor.editHistoryCurrentIndex();
  require(fill_kind != nullptr && fill_kind->isVisible() &&
              fill_kind->setProperty("currentIndex", 1) &&
              QMetaObject::invokeMethod(fill_kind, "activated", Q_ARG(int, 1)) &&
              editor.editHistoryCurrentIndex() == history_before_fill_kind + 1,
          "The stroke fill selector must expose and commit the gradient choice.");
  settle(180);
  const auto gradient_stroke = editor.photoLayerStyles(0).first().toMap();
  const auto gradient_stroke_id = gradient_stroke.value(QStringLiteral("id")).toString();
  auto gradient_stops = gradient_stroke.value(QStringLiteral("parameters")).toMap()
      .value(QStringLiteral("fill")).toMap().value(QStringLiteral("gradient")).toMap()
      .value(QStringLiteral("stops")).toList();
  require(gradient_stroke.value(QStringLiteral("typeId")).toString() == QStringLiteral("stroke") &&
              gradient_stops.size() == 2 &&
              qAbs(gradient_stops.first().toMap().value(QStringLiteral("position")).toDouble()) < 1e-12 &&
              qAbs(gradient_stops.last().toMap().value(QStringLiteral("position")).toDouble() - 1.0) < 1e-12,
          "Switching a stroke to gradient fill must create ordered endpoint stops.");
  const auto history_before_add_stop = editor.editHistoryCurrentIndex();
  auto* add_stop = visual_by_name(panel,
      QStringLiteral("layerStyleGradientAddStop_") + gradient_stroke_id);
  require(add_stop != nullptr && add_stop->isEnabled() && click(&window, add_stop),
          "The visible Add stop control must add an interior gradient stop.");
  gradient_stops = editor.photoLayerStyles(0).first().toMap()
      .value(QStringLiteral("parameters")).toMap().value(QStringLiteral("fill")).toMap()
      .value(QStringLiteral("gradient")).toMap().value(QStringLiteral("stops")).toList();
  require(editor.editHistoryCurrentIndex() == history_before_add_stop + 1 &&
              gradient_stops.size() == 3 &&
              qAbs(gradient_stops.at(0).toMap().value(QStringLiteral("position")).toDouble()) < 1e-12 &&
              qAbs(gradient_stops.at(1).toMap().value(QStringLiteral("position")).toDouble() - 0.5) < 1e-12 &&
              qAbs(gradient_stops.at(2).toMap().value(QStringLiteral("position")).toDouble() - 1.0) < 1e-12,
          "Adding a stop must preserve endpoints, ordering, and one-commit history.");

  auto* stop_color_row = visual_by_name(panel, QStringLiteral("layerStyleGradientStopColor_") +
      gradient_stroke_id + QStringLiteral("_1"));
  auto* stop_position = visual_with_property(panel, QStringLiteral("accessibleLabel"),
      QStringLiteral("Stop 2 position"));
  const auto stop_choose_buttons = stop_color_row == nullptr
      ? std::vector<QQuickItem*>{}
      : visuals_with_text(stop_color_row, QStringLiteral("Choose"));
  require(stop_color_row != nullptr && stop_position != nullptr &&
              click(&window, first_visible_enabled(stop_choose_buttons)),
          "The interior gradient stop must expose a color chooser and a position editor.");
  auto* gradient_color_dialog = find_object_with_title(panel, QStringLiteral("Style color"));
  require(gradient_color_dialog != nullptr && gradient_color_dialog->property("visible").toBool(),
          "Choosing an interior stop color must open the style color dialog.");
  gradient_color_dialog->setProperty("selectedColor", QColor(QStringLiteral("#336699")));
  const auto history_before_stop_color = editor.editHistoryCurrentIndex();
  require(QMetaObject::invokeMethod(gradient_color_dialog, "accept", Qt::DirectConnection),
          "The gradient stop color dialog must accept the selected color.");
  settle(180);
  gradient_stops = editor.photoLayerStyles(0).first().toMap()
      .value(QStringLiteral("parameters")).toMap().value(QStringLiteral("fill")).toMap()
      .value(QStringLiteral("gradient")).toMap().value(QStringLiteral("stops")).toList();
  const auto selected_stop_color = gradient_stops.at(1).toMap()
      .value(QStringLiteral("color")).toMap();
  require(editor.editHistoryCurrentIndex() == history_before_stop_color + 1 &&
              qAbs(selected_stop_color.value(QStringLiteral("r")).toDouble() - 0.0331047666) < 0.002 &&
              qAbs(selected_stop_color.value(QStringLiteral("g")).toDouble() - 0.1328683216) < 0.002 &&
              qAbs(selected_stop_color.value(QStringLiteral("b")).toDouble() - 0.3185467781) < 0.002,
          "The stop color chooser must commit the selected sRGB color as linear-sRGB in one history step.");

  // Editing a stop replaces the nested Repeater's stop array and destroys its
  // delegate items, so reacquire the position editor by its stable style/stop ID.
  stop_position = visual_by_name(panel, QStringLiteral("layerStyleGradientStopPosition_") +
      gradient_stroke_id + QStringLiteral("_1"));
  auto* stop_position_field = stop_position == nullptr ? nullptr
      : visual_with_property(stop_position, QStringLiteral("accessibleLabel"),
                             QStringLiteral("Stop 2 position"));
  auto* stop_position_input = stop_position_field == nullptr ? nullptr
      : qvariant_cast<QQuickItem*>(stop_position_field->property("contentItem"));
  window.requestActivate();
  settle(80);
  require(stop_position_input != nullptr && click(&window, stop_position_input),
          "The interior stop position input must accept numeric keyboard entry.");
  QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
  for (const auto key : {Qt::Key_4, Qt::Key_0, Qt::Key_Period, Qt::Key_0})
    QTest::keyClick(&window, key);
  const auto input_text_before_return = stop_position_input->property("text").toString();
  const auto input_editing_before_return = stop_position_input->property("editingText").toBool();
  const auto input_focused_before_return = stop_position_input->hasActiveFocus();
  const auto field_focused_before_return = stop_position_field->hasActiveFocus();
  const auto dialog_visible_before_return = gradient_color_dialog->property("visible").toBool();
  const auto window_active_before_return = window.isActive();
  const auto history_before_stop_position = editor.editHistoryCurrentIndex();
  QTest::keyClick(&window, Qt::Key_Return);
  const auto input_status_after_return = editor.status();
  settle(180);
  gradient_stops = editor.photoLayerStyles(0).first().toMap()
      .value(QStringLiteral("parameters")).toMap().value(QStringLiteral("fill")).toMap()
      .value(QStringLiteral("gradient")).toMap().value(QStringLiteral("stops")).toList();
  const auto actual_stop_position = gradient_stops.at(1).toMap()
      .value(QStringLiteral("position")).toDouble();
  const auto stop_position_history_delta =
      editor.editHistoryCurrentIndex() - history_before_stop_position;
  if (stop_position_history_delta != 1 || qAbs(actual_stop_position - 0.4) >= 1e-6) {
    std::cerr << "Gradient stop position diagnostics: actual_position="
              << actual_stop_position
              << " history_delta=" << stop_position_history_delta
              << " input_text_before_return='" << input_text_before_return.toStdString()
              << "' editing_before_return=" << input_editing_before_return
              << " input_focus_before_return=" << input_focused_before_return
              << " field_focus_before_return=" << field_focused_before_return
              << " window_active_before_return=" << window_active_before_return
              << " dialog_visible_before_return=" << dialog_visible_before_return
              << " status_after_return='" << input_status_after_return.toStdString()
              << "'\n";
  }
  require(stop_position_history_delta == 1 &&
              qAbs(actual_stop_position - 0.4) < 1e-6 &&
              gradient_stops.at(0).toMap().value(QStringLiteral("position")).toDouble() <
                  actual_stop_position &&
              actual_stop_position <
                  gradient_stops.at(2).toMap().value(QStringLiteral("position")).toDouble(),
          "Repositioning an interior stop must keep it between the fixed endpoints.");
  const auto history_before_remove_stop = editor.editHistoryCurrentIndex();
  auto* remove_stop = visual_by_name(panel, QStringLiteral("layerStyleGradientRemoveStop_") +
      gradient_stroke_id + QStringLiteral("_1"));
  require(remove_stop != nullptr && click(&window, remove_stop) &&
              editor.editHistoryCurrentIndex() == history_before_remove_stop + 1,
          "Removing an interior stop must be one committed edit.");
  gradient_stops = editor.photoLayerStyles(0).first().toMap()
      .value(QStringLiteral("parameters")).toMap().value(QStringLiteral("fill")).toMap()
      .value(QStringLiteral("gradient")).toMap().value(QStringLiteral("stops")).toList();
  require(gradient_stops.size() == 2 &&
              qAbs(gradient_stops.first().toMap().value(QStringLiteral("position")).toDouble()) < 1e-12 &&
              qAbs(gradient_stops.last().toMap().value(QStringLiteral("position")).toDouble() - 1.0) < 1e-12,
          "Removing the interior stop must retain the two gradient endpoints.");
  restore_history(editor, false);
  require(editor.photoLayerStyles(0).first().toMap().value(QStringLiteral("parameters")).toMap()
              .value(QStringLiteral("fill")).toMap().value(QStringLiteral("gradient")).toMap()
              .value(QStringLiteral("stops")).toList().size() == 3,
          "Undo must restore the removed gradient stop in the live controller.");
  restore_history(editor, true);
  require(editor.photoLayerStyles(0).first().toMap().value(QStringLiteral("parameters")).toMap()
              .value(QStringLiteral("fill")).toMap().value(QStringLiteral("gradient")).toMap()
              .value(QStringLiteral("stops")).toList().size() == 2,
          "Redo must remove the gradient stop again.");

  auto asymmetric_parameters = editor.photoLayerStyles(0).first().toMap()
      .value(QStringLiteral("parameters")).toMap();
  const QVariantList asymmetric_stops{
      QVariantMap{{QStringLiteral("position"), 0.0},
          {QStringLiteral("color"), QVariantMap{{QStringLiteral("r"), 1.0},
              {QStringLiteral("g"), 0.0}, {QStringLiteral("b"), 0.0},
              {QStringLiteral("a"), 1.0}}}},
      QVariantMap{{QStringLiteral("position"), 0.2},
          {QStringLiteral("color"), QVariantMap{{QStringLiteral("r"), 0.0},
              {QStringLiteral("g"), 0.0}, {QStringLiteral("b"), 1.0},
              {QStringLiteral("a"), 0.4}}}},
      QVariantMap{{QStringLiteral("position"), 1.0},
          {QStringLiteral("color"), QVariantMap{{QStringLiteral("r"), 0.0},
              {QStringLiteral("g"), 1.0}, {QStringLiteral("b"), 0.0},
              {QStringLiteral("a"), 1.0}}}}};
  auto asymmetric_fill = asymmetric_parameters.value(QStringLiteral("fill")).toMap();
  auto asymmetric_gradient = asymmetric_fill.value(QStringLiteral("gradient")).toMap();
  asymmetric_gradient.insert(QStringLiteral("stops"), asymmetric_stops);
  asymmetric_gradient.insert(QStringLiteral("reverse"), true);
  asymmetric_fill.insert(QStringLiteral("gradient"), asymmetric_gradient);
  asymmetric_parameters.insert(QStringLiteral("fill"), asymmetric_fill);
  const auto history_before_asymmetric_gradient = editor.editHistoryCurrentIndex();
  require(editor.setPhotoLayerStyleParameters(0, gradient_stroke_id,
              {{QStringLiteral("parameters"), asymmetric_parameters}}) &&
              editor.editHistoryCurrentIndex() == history_before_asymmetric_gradient + 1,
          "The actual controller must accept a reversed multicolor gradient as the add-stop fixture.");
  refresh_panel(panel, editor, 0);
  const auto before_stop_insert_gradient = editor.photoLayerStyles(0).first().toMap()
      .value(QStringLiteral("parameters")).toMap().value(QStringLiteral("fill")).toMap()
      .value(QStringLiteral("gradient")).toMap();
  const auto sample_positions = {0.0, 0.1, 0.2, 0.35, 0.6, 0.8, 1.0};
  std::vector<QVariantMap> before_samples;
  for (const auto position : sample_positions)
    before_samples.push_back(sample_gradient(before_stop_insert_gradient, position));
  add_stop = visual_by_name(panel,
      QStringLiteral("layerStyleGradientAddStop_") + gradient_stroke_id);
  require(add_stop != nullptr && add_stop->isEnabled() && click(&window, add_stop),
          "The QML Add stop action must also handle an asymmetric reversed gradient.");
  const auto after_stop_insert_gradient = editor.photoLayerStyles(0).first().toMap()
      .value(QStringLiteral("parameters")).toMap().value(QStringLiteral("fill")).toMap()
      .value(QStringLiteral("gradient")).toMap();
  const auto after_samples = [&]() {
    std::vector<QVariantMap> result;
    for (const auto position : sample_positions)
      result.push_back(sample_gradient(after_stop_insert_gradient, position));
    return result;
  }();
  bool same_gradient_function = before_samples.size() == after_samples.size();
  for (std::size_t i = 0; same_gradient_function && i < before_samples.size(); ++i)
    for (const auto* key : {"r", "g", "b", "a"})
      same_gradient_function = same_gradient_function && qAbs(
          before_samples[i].value(QLatin1String(key)).toDouble() -
          after_samples[i].value(QLatin1String(key)).toDouble()) < 1e-12;
  require(same_gradient_function &&
              after_stop_insert_gradient.value(QStringLiteral("reverse")).toBool() &&
              after_stop_insert_gradient.value(QStringLiteral("stops")).toList().size() == 4 &&
              qAbs(after_stop_insert_gradient.value(QStringLiteral("stops")).toList().at(2)
                       .toMap().value(QStringLiteral("position")).toDouble() - 0.6) < 1e-12,
          "Adding a stop must interpolate in authored stop order, preserving reversed asymmetric gradients.");

  for (const auto width : {280, 320, 480})
    set_width(&window, panel, width, proof_directory, QStringLiteral("gradient"));

  const auto copied_envelope = editor.photoLayerStyleEnvelope(0);
  require(!copied_envelope.isEmpty(), "The current styles must be copyable as an envelope.");
  const auto copy_buttons = visuals_with_text(panel, QStringLiteral("Copy styles"));
  require(!copy_buttons.empty() && click(&window, copy_buttons.front()),
          "The visible Copy styles control must capture the explicit envelope.");
  require(panel->property("copiedEnvelope").toString() == copied_envelope,
          "The Copy styles UI must keep the exact explicit envelope.");

  require(editor.addFillLayer(QStringLiteral("#89694b"), QStringLiteral("Second target")),
          "Create a second target for UI paste.");
  settle();
  refresh_panel(panel, editor, 0);
  const auto paste_buttons = visuals_with_text(panel, QStringLiteral("Paste styles"));
  require(!paste_buttons.empty() && click(&window, paste_buttons.front()),
          "The visible Paste styles control must apply the copied envelope.");
  require(editor.photoLayerStyles(0) == editor.photoLayerStyles(1) &&
              editor.photoLayerStyleLightAngle(0) == editor.photoLayerStyleLightAngle(1),
          "UI paste must reproduce the typed stack and shared light on the target layer.");
  const auto pasted_stroke_id = style_id(editor, 0, QStringLiteral("stroke"));
  auto* remove_button = visual_by_name(
      panel, QStringLiteral("layerStyleRemove_") + pasted_stroke_id);
  require(remove_button != nullptr && click(&window, remove_button) &&
              editor.photoLayerStyles(0).size() + 1 == editor.photoLayerStyles(1).size(),
          "The visible Remove style control must delete one pasted operation.");

  refresh_panel(panel, editor, 1);
  const auto pasted_overlay_id = style_id(editor, 1, QStringLiteral("color_overlay"));
  expand_style_if_needed(panel, &window, pasted_overlay_id);
  auto* no_selection_mask_button = visual_by_name(panel,
      QStringLiteral("layerStyleMaskFromSelection_") + pasted_overlay_id);
  require(no_selection_mask_button != nullptr && !no_selection_mask_button->isEnabled() &&
              !visuals_with_text(panel,
                  QStringLiteral("Create a selection with a selection tool, then use Mask from selection to give this style independent coverage.")).empty(),
          "Without a working selection, the mask action must be unavailable and explain how to proceed.");
  auto choose_buttons = visuals_with_text(panel, QStringLiteral("Choose"));
  auto* visible_choose = first_visible_enabled(choose_buttons);
  if (visible_choose == nullptr) {
    std::cerr << "Color chooser diagnostic: expandedId='"
              << panel->property("expandedId").toString().toStdString()
              << "' choose_count=" << choose_buttons.size() << " styles=";
    for (const auto& style_value : editor.photoLayerStyles(1)) {
      const auto style = style_value.toMap();
      std::cerr << "[" << style.value(QStringLiteral("typeId")).toString().toStdString()
                << ",supported=" << style.value(QStringLiteral("supported")).toBool()
                << ",id=" << style.value(QStringLiteral("id")).toString().toStdString()
                << "]";
    }
    for (const auto* button : choose_buttons)
      std::cerr << " choose(visible=" << button->isVisible()
                << ",enabled=" << button->isEnabled() << ")";
    std::cerr << '\n';
  }
  require(visible_choose != nullptr && click(&window, visible_choose),
          "A visible color row must open the style color chooser.");
  auto* color_dialog = find_object_with_title(panel, QStringLiteral("Style color"));
  require(color_dialog != nullptr && color_dialog->property("visible").toBool(),
          "The style color chooser must expose its open state to this UI test.");
  const auto before_layer_switch = editor.currentAssetBoundAdjustmentPatchJson();
  refresh_panel(panel, editor, 0);
  settle();
  require(!color_dialog->property("visible").toBool() &&
              editor.currentAssetBoundAdjustmentPatchJson() == before_layer_switch,
          "Changing layers must close the pending color chooser without committing a color.");

  refresh_panel(panel, editor, 1);
  expand_style_if_needed(panel, &window, pasted_overlay_id);
  choose_buttons = visuals_with_text(panel, QStringLiteral("Choose"));
  visible_choose = first_visible_enabled(choose_buttons);
  require(visible_choose != nullptr && click(&window, visible_choose) &&
              color_dialog->property("visible").toBool(),
          "The color chooser must reopen for lock-cancellation coverage.");
  const auto before_lock = editor.currentAssetBoundAdjustmentPatchJson();
  require(editor.setPhotoLayerLocked(1, true), "Lock the active style target while the dialog is open.");
  refresh_panel(panel, editor, 1);
  panel->setProperty("locked", true);
  settle();
  require(!color_dialog->property("visible").toBool() &&
              editor.currentAssetBoundAdjustmentPatchJson() != before_lock,
          "Locking a layer must close its color chooser while recording only the lock change.");
  require(editor.setPhotoLayerLocked(1, false), "Unlock the style target after dialog cancellation.");
  refresh_panel(panel, editor, 1);
  panel->setProperty("locked", false);
  expand_style_if_needed(panel, &window, pasted_overlay_id);

  editor.addMask(QStringLiteral("rectangle"));
  editor.setActiveMaskFeather(0.0);
  editor.setActiveMaskCenterX(0.52);
  editor.setActiveMaskCenterY(0.48);
  editor.setActiveMaskRadiusX(0.30);
  editor.setActiveMaskRadiusY(0.28);
  settle(180);
  refresh_panel(panel, editor, 1);
  const auto working_selection_id = editor.workingSelectionSetId();
  auto* mask_from_selection = visual_by_name(panel,
      QStringLiteral("layerStyleMaskFromSelection_") + pasted_overlay_id);
  const auto history_before_style_mask = editor.editHistoryCurrentIndex();
  require(!working_selection_id.isEmpty() && mask_from_selection != nullptr &&
              mask_from_selection->isEnabled() && click(&window, mask_from_selection),
          "The visible style-mask action must create an independent mask from the working selection.");
  auto masked_overlay = style_by_id(editor, 1, pasted_overlay_id);
  const auto style_mask_id = masked_overlay.value(QStringLiteral("maskRef")).toString();
  require(!style_mask_id.isEmpty() && style_mask_id != working_selection_id &&
              editor.workingSelectionSetId() == working_selection_id &&
              editor.editHistoryCurrentIndex() == history_before_style_mask + 1 &&
              contains_mask_set(editor, style_mask_id),
          "The UI must keep the working selection intact and record one separate style-mask set.");
  for (const auto width : {280, 320, 480})
    set_width(&window, panel, width, proof_directory, QStringLiteral("mask"));
  const auto shared_mask_envelope = editor.photoLayerStyleEnvelope(1);
  const auto history_before_shared_style = editor.editHistoryCurrentIndex();
  require(!shared_mask_envelope.isEmpty() &&
              editor.applyPhotoLayerStyleEnvelope(0, shared_mask_envelope) &&
              editor.editHistoryCurrentIndex() == history_before_shared_style + 1 &&
              style_by_id(editor, 0, pasted_overlay_id)
                  .value(QStringLiteral("maskRef")).toString() == style_mask_id,
          "A same-document style copy may share the explicit style-mask source.");
  auto* edit_style_mask = visual_by_name(panel,
      QStringLiteral("layerStyleEditMask_") + pasted_overlay_id);
  require(edit_style_mask != nullptr && click(&window, edit_style_mask) &&
              editor.editingLayerMask() &&
              editor.maskEditLayerId() == target_layer(editor, 1).value(QStringLiteral("id")).toString() &&
              editor.workingSelectionSetId() == working_selection_id,
          "The visible Edit mask action must select the style mask through the shared mask target.");
  const auto history_before_clear_style_mask = editor.editHistoryCurrentIndex();
  auto* clear_style_mask = visual_by_name(panel,
      QStringLiteral("layerStyleClearMask_") + pasted_overlay_id);
  require(clear_style_mask != nullptr && click(&window, clear_style_mask) &&
              editor.editHistoryCurrentIndex() == history_before_clear_style_mask + 1 &&
              style_by_id(editor, 1, pasted_overlay_id)
                  .value(QStringLiteral("maskRef")).toString().isEmpty() &&
              contains_mask_set(editor, style_mask_id),
          "Clearing a shared style mask must add one history step and keep the remaining consumer's source.");
  restore_history(editor, false);
  require(style_by_id(editor, 1, pasted_overlay_id)
              .value(QStringLiteral("maskRef")).toString() == style_mask_id &&
              editor.editingLayerMask() && editor.maskEditLayerId() ==
                  target_layer(editor, 1).value(QStringLiteral("id")).toString(),
          "Undo must restore a style mask's reference and inspector editing owner.");
  restore_history(editor, true);
  require(style_by_id(editor, 1, pasted_overlay_id)
              .value(QStringLiteral("maskRef")).toString().isEmpty() &&
              !editor.editingLayerMask() && contains_mask_set(editor, style_mask_id),
          "Redo must clear only this style reference while preserving its other consumer.");
  editor.finishLayerMaskEditing();

  for (const auto width : {280, 320, 480})
    set_width(&window, panel, width, proof_directory, QStringLiteral("known"));

  const auto future_envelope = QString::fromUtf8(future_style_envelope());
  const auto before_unknown_import = editor.currentAssetBoundAdjustmentPatchJson();
  const auto before_unknown_import_history = editor.editHistoryCurrentIndex();
  const auto before_unknown_import_styles = editor.photoLayerStyles(1);
  require(!editor.applyPhotoLayerStyleEnvelope(1, future_envelope) &&
              editor.currentAssetBoundAdjustmentPatchJson() == before_unknown_import &&
              editor.editHistoryCurrentIndex() == before_unknown_import_history &&
              editor.photoLayerStyles(1) == before_unknown_import_styles,
          "Importing an enabled unsupported style must leave the current recipe and history unchanged.");
  const auto existing_future_recipe = patch_with_future_style(
      editor, 1, future_envelope.toUtf8());
  require(!existing_future_recipe.isEmpty(),
          "The test must encode an enabled future style as an existing-document recipe.");
  editor.loadAsset(QStringLiteral("photo-layer-styles-ui"), source_path,
                   {}, {}, {}, {}, existing_future_recipe, true);
  settle();
  refresh_panel(panel, editor, 1);
  styles = editor.photoLayerStyles(1);
  require(styles.size() == 1 &&
              !styles.first().toMap().value(QStringLiteral("supported")).toBool() &&
              styles.first().toMap().value(QStringLiteral("enabled")).toBool(),
          "Unknown style operations must remain visible and marked unsupported.");
  const auto unknown_id = styles.first().toMap().value(QStringLiteral("id")).toString();
  expand_style_if_needed(panel, &window, unknown_id);
  auto* unknown_enabled = visual_by_name(
      panel, QStringLiteral("layerStyleEnabled_") + unknown_id);
  require(unknown_enabled != nullptr && unknown_enabled->isEnabled() &&
          click(&window, unknown_enabled),
          "An enabled unknown style must expose only its safe disable action.");
  styles = editor.photoLayerStyles(1);
  require(!styles.first().toMap().value(QStringLiteral("enabled")).toBool() &&
              !unknown_enabled->isEnabled(),
          "A disabled unknown style must be inert while preserving its payload.");
  const auto unknown_envelope = editor.photoLayerStyleEnvelope(1);
  require(!unknown_envelope.isEmpty(),
          "Disabling an unknown style must preserve a copyable opaque envelope.");

  for (const auto width : {280, 320, 480})
    set_width(&window, panel, width, proof_directory, QStringLiteral("unsupported"));
  window.close();
  panel->setParentItem(nullptr);
  object.reset();
  settle(80);
  if (!qml_warnings.isEmpty()) {
    for (const auto& warning : qml_warnings)
      std::cerr << "QML warning: " << warning.toStdString() << '\n';
    require(false, "The production layer style panel must run without QML warnings.");
  }
  std::cout << "PhotoLayerStyles offscreen UI passed: add/edit/disable/reorder/remove, copy/paste, numeric commit, dialog cancellation, unknown-style safety, and responsive rendering.\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR",
          runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR",
          runtime.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  qputenv("QT_QUICK_CONTROLS_STYLE", QByteArrayLiteral("Basic"));
  qputenv("QT_QUICK_BACKEND", QByteArrayLiteral("software"));
  QGuiApplication application(argc, argv);
  try {
    return run_workflow();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
