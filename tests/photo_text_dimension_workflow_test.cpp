#include "edit_recipe_state.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "flattened_output_renderer.h"
#include "photo_layer_effect_codec.h"
#include "photo_component_workspace_controller.h"
#include "photo_paint_surface.h"
#include "photo_render_utils.h"
#include "preview_output_color_space.h"
#include "photo_text_dimension.h"
#include "photo_text_dimension_outline.h"
#include "photo_text_dimension_render.h"
#include "photo_text_rasterizer.h"
#include "photo_vector_path.h"
#include "photo_editor/pipeline/photo_effects.h"

#include <QColorSpace>
#include <QElapsedTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

#ifndef PHOTO_EDITOR_SOURCE_DIR
#define PHOTO_EDITOR_SOURCE_DIR "."
#endif

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static EditState state(const EditorPreviewController& controller) {
    return controller.currentState();
  }
  static bool apply_layer_stack_change(EditorPreviewController& controller,
                                       const QVariantList& layers) {
    return controller.applyPhotoLayerStackChange(
        layers, QStringLiteral("dimension_recursive_admission_test"), false,
        QStringLiteral("Test nested dimensional text admission"), true);
  }
};
}  // namespace photo_editor::desktop

using namespace photo_editor::desktop;

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms = 90000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (predicate()) return true;
    QThread::msleep(2);
  }
  return predicate();
}

void settle(EditorPreviewController& editor) {
  require(wait_until([&] {
    return !editor.processing() && !editor.fullRenderRunning() &&
        !editor.professionalToolsBusy() && !editor.photoPaintBusy() &&
        !editor.photoExportBusy();
  }), "The dimensional text controller must settle before assertions.");
}

bool same_pixels(const QImage& lhs, const QImage& rhs) {
  if (lhs.isNull() || rhs.isNull() || lhs.size() != rhs.size()) return false;
  const auto a = lhs.convertToFormat(QImage::Format_RGBA8888);
  const auto b = rhs.convertToFormat(QImage::Format_RGBA8888);
  const auto row_bytes = static_cast<std::size_t>(a.width()) * 4U;
  for (int y = 0; y < a.height(); ++y)
    if (std::memcmp(a.constScanLine(y), b.constScanLine(y), row_bytes) != 0) return false;
  return true;
}

std::uint64_t differing_pixel_count(const QImage& lhs, const QImage& rhs) {
  if (lhs.isNull() || rhs.isNull() || lhs.size() != rhs.size()) return 0;
  const auto a = lhs.convertToFormat(QImage::Format_RGBA8888);
  const auto b = rhs.convertToFormat(QImage::Format_RGBA8888);
  std::uint64_t count = 0;
  for (int y = 0; y < a.height(); ++y) {
    const auto* actual = a.constScanLine(y);
    const auto* expected = b.constScanLine(y);
    for (int x = 0; x < a.width(); ++x)
      if (std::memcmp(actual + static_cast<std::size_t>(x) * 4U,
                      expected + static_cast<std::size_t>(x) * 4U, 4U) != 0)
        ++count;
  }
  return count;
}

QString image_color_space_description(const QImage& image) {
  const auto color_space = image.colorSpace();
  if (!color_space.isValid()) return QStringLiteral("invalid");
  return QStringLiteral("%1 (primaries=%2 transfer=%3)")
      .arg(color_space.description())
      .arg(static_cast<int>(color_space.primaries()))
      .arg(static_cast<int>(color_space.transferFunction()));
}

bool same_export_pixels(const QImage& lhs, const QImage& rhs) {
  if (lhs.isNull() || rhs.isNull() || lhs.size() != rhs.size() ||
      lhs.colorSpace() != rhs.colorSpace()) {
    return false;
  }
  const auto a = lhs.convertToFormat(QImage::Format_RGBA64);
  const auto b = rhs.convertToFormat(QImage::Format_RGBA64);
  const auto row_bytes = static_cast<std::size_t>(a.width()) * sizeof(QRgba64);
  for (int y = 0; y < a.height(); ++y)
    if (std::memcmp(a.constScanLine(y), b.constScanLine(y), row_bytes) != 0) return false;
  return true;
}

QString export_pixel_difference_report(const QImage& actual, const QImage& expected) {
  QString report = QStringLiteral(
      "actual size=%1x%2 format=%3 profile=%4; expected size=%5x%6 format=%7 profile=%8")
      .arg(actual.width()).arg(actual.height()).arg(static_cast<int>(actual.format()))
      .arg(image_color_space_description(actual))
      .arg(expected.width()).arg(expected.height()).arg(static_cast<int>(expected.format()))
      .arg(image_color_space_description(expected));
  if (actual.isNull() || expected.isNull() || actual.size() != expected.size()) {
    return report + QStringLiteral("; pixel comparison unavailable (null or size mismatch)");
  }
  const auto a = actual.convertToFormat(QImage::Format_RGBA64);
  const auto b = expected.convertToFormat(QImage::Format_RGBA64);
  std::uint32_t max_channel_delta = 0;
  std::uint64_t differing_pixels = 0;
  QPoint first_difference(-1, -1);
  std::array<std::uint16_t, 4> first_actual{};
  std::array<std::uint16_t, 4> first_expected{};
  for (int y = 0; y < a.height(); ++y) {
    const auto* actual_row = reinterpret_cast<const QRgba64*>(a.constScanLine(y));
    const auto* expected_row = reinterpret_cast<const QRgba64*>(b.constScanLine(y));
    for (int x = 0; x < a.width(); ++x) {
      const std::array<std::uint16_t, 4> actual_channels{
          actual_row[x].red(), actual_row[x].green(), actual_row[x].blue(), actual_row[x].alpha()};
      const std::array<std::uint16_t, 4> expected_channels{
          expected_row[x].red(), expected_row[x].green(), expected_row[x].blue(), expected_row[x].alpha()};
      std::uint32_t pixel_max_delta = 0;
      for (std::size_t channel = 0; channel < actual_channels.size(); ++channel) {
        const auto delta = actual_channels[channel] > expected_channels[channel]
            ? actual_channels[channel] - expected_channels[channel]
            : expected_channels[channel] - actual_channels[channel];
        pixel_max_delta = std::max(pixel_max_delta, static_cast<std::uint32_t>(delta));
      }
      if (pixel_max_delta == 0) continue;
      ++differing_pixels;
      max_channel_delta = std::max(max_channel_delta, pixel_max_delta);
      if (first_difference.x() < 0) {
        first_difference = QPoint(x, y);
        first_actual = actual_channels;
        first_expected = expected_channels;
      }
    }
  }
  report += QStringLiteral(
      "; differing pixels=%1 max channel delta=%2/65535 first at (%3,%4) actual RGBA64=(%5,%6,%7,%8) expected=(%9,%10,%11,%12)")
      .arg(differing_pixels).arg(max_channel_delta)
      .arg(first_difference.x()).arg(first_difference.y())
      .arg(first_actual[0]).arg(first_actual[1]).arg(first_actual[2]).arg(first_actual[3])
      .arg(first_expected[0]).arg(first_expected[1]).arg(first_expected[2]).arg(first_expected[3]);
  return report;
}

bool same_float_pixels(const QImage& lhs, const QImage& rhs) {
  if (lhs.isNull() || rhs.isNull() || lhs.size() != rhs.size()) return false;
  const auto a = lhs.convertToFormat(QImage::Format_RGBA32FPx4);
  const auto b = rhs.convertToFormat(QImage::Format_RGBA32FPx4);
  if (a.colorSpace() != b.colorSpace()) return false;
  const auto row_bytes = static_cast<std::size_t>(a.width()) * sizeof(QRgbaFloat32);
  for (int y = 0; y < a.height(); ++y)
    if (std::memcmp(a.constScanLine(y), b.constScanLine(y), row_bytes) != 0) return false;
  return true;
}

QImage preview(EditorPreviewImageProvider& provider) {
  QImage image;
  require(wait_until([&] {
    image = provider.image(QStringLiteral("edited"));
    return !image.isNull();
  }), "The production text render must reach the preview image provider.");
  return image;
}

QImage changed_preview(EditorPreviewImageProvider& provider, const QImage& previous) {
  QImage image;
  require(wait_until([&] {
    image = provider.image(QStringLiteral("edited"));
    return !image.isNull() && !same_pixels(image, previous);
  }), "The dimensional text draft must publish changed pixels to the preview provider.");
  return image;
}

void restored_preview(EditorPreviewImageProvider& provider, const QImage& expected) {
  require(wait_until([&] { return same_pixels(provider.image(QStringLiteral("edited")), expected); }),
          "History restore must republish the exact expected preview pixels.");
}

QVariantMap text_layer(const EditorPreviewController& editor, int index);
int text_layer_index(const EditorPreviewController& editor, const QString& id);
QVariantMap rectangle_mask(const QString& id);
void restore_history(EditorPreviewController& editor, bool redo);

void send_pointer(QQuickWindow& window, QEvent::Type type, QPointF point) {
  const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
  const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
  QMouseEvent event(type, point, window.mapToGlobal(point.toPoint()), button, buttons,
                    Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &event);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

QString dimension_overlay_fixture_qml() {
  const auto components = QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components")).toString();
  return QStringLiteral(R"QML(
import QtQuick
import "%1" as Components

Item {
    id: harness
    objectName: "photoTextDimensionControllerFixture"
    width: 1000
    height: 500
    property var controller: dimensionController
    property int layerIndex: 0
    property int geometryRevision: 0

    function mapPoint(point) {
        return {x: Number(point.x) * width, y: Number(point.y) * height}
    }
    function geometryFor(index, layerId, documentToken, revision) {
        const value = controller.photoTextDimensionCanvasGeometry(index)
        if (value.valid !== true)
            return value
        const handles = value.handles || ({})
        const corners = handles.corners || ({})
        const projectedHandles = ({})
        if (handles.rotate) projectedHandles.rotate = mapPoint(handles.rotate)
        if (handles.depth) projectedHandles.depth = mapPoint(handles.depth)
        projectedHandles.corners = ({})
        for (const name of ["topLeft", "topRight", "bottomRight", "bottomLeft"])
            if (corners[name]) projectedHandles.corners[name] = mapPoint(corners[name])
        value.outline = (value.outline || []).map(mapPoint)
        value.handles = projectedHandles
        value.overlayImageX = 0
        value.overlayImageY = 0
        value.overlayImageWidth = width
        value.overlayImageHeight = height
        value.layerId = layerId
        value.documentToken = documentToken
        value.revision = revision
        return value
    }

    Connections {
        target: harness.controller
        ignoreUnknownSignals: true
        function onControlsChanged() { harness.geometryRevision += 1 }
        function onPreviewChanged() { harness.geometryRevision += 1 }
    }

    Components.PhotoTextDimensionOverlay {
        id: dimensionOverlay
        objectName: "photoTextDimensionOverlay"
        anchors.fill: parent
        active: true
        editable: true
        locked: false
        panActive: false
        controller: harness.controller
        documentToken: String(controller.assetId)
        layerIndex: harness.layerIndex
        targetLayerId: String(controller.photoLayers[harness.layerIndex].id || "")
        revision: String(controller.photoTextDimensionCanvasGeometry(harness.layerIndex).revision || "")
        geometryRevision: harness.geometryRevision
        mode: "rotate"
        dimension: controller.photoLayerTextDimension(harness.layerIndex)
        geometryProvider: function(index, layerId, documentToken, revision) {
            return harness.geometryFor(index, layerId, documentToken, revision)
        }
        imageX: 0
        imageY: 0
        imageWidth: harness.width
        imageHeight: harness.height
        mapViewToDimensionPatch: function(mode, x, y, startDimension, geometry, modifiers) {
            return harness.controller.textDimensionPatchAt(
                harness.layerIndex, mode, x, y, startDimension, geometry, modifiers)
        }
        beginGesture: function(index, layerId, token, revision) {
            return harness.controller.beginTextDimensionGesture(index, layerId, token, revision)
        }
        previewGesture: function(index, layerId, patch, token, revision) {
            return harness.controller.previewTextDimensionGesture(index, layerId, patch, token, revision)
        }
        finishGesture: function(index, layerId, commit, token, revision) {
            return harness.controller.finishTextDimensionGesture(index, layerId, commit, token, revision)
        }
    }
}
)QML").arg(components);
}

void verify_real_overlay_gesture(EditorPreviewController& editor,
                                 EditorPreviewImageProvider& provider,
                                 int layer_index,
                                 const QString& layer_id) {
  const auto geometry = editor.photoTextDimensionCanvasGeometry(layer_index);
  require(geometry.value(QStringLiteral("valid")).toBool() &&
              geometry.value(QStringLiteral("editable")).toBool(),
          "The production dimension overlay must receive a valid editable controller target.");
  const auto start_history = editor.editHistoryCurrentIndex();
  const auto start_layer = text_layer(editor, layer_index);
  const auto start_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto start_pixels = preview(provider);

  QQmlEngine engine;
  QQmlComponent component(&engine);
  const auto source_root = QString::fromUtf8(PHOTO_EDITOR_SOURCE_DIR);
  component.setData(dimension_overlay_fixture_qml().toUtf8(), QUrl::fromLocalFile(
      QDir(source_root).filePath(QStringLiteral("tests/photo_text_dimension_overlay_fixture.qml"))));
  if (component.isError()) {
    for (const auto& error : component.errors())
      std::cerr << error.toString().toStdString() << '\n';
  }
  require(!component.isError(), "The production dimensional text overlay fixture must compile.");
  QQmlContext context(engine.rootContext());
  context.setContextProperty(QStringLiteral("dimensionController"), &editor);
  std::unique_ptr<QObject> object(component.create(&context));
  require(object != nullptr, "The production dimensional text overlay must instantiate.");
  auto* fixture = qobject_cast<QQuickItem*>(object.get());
  auto* overlay = object->findChild<QObject*>(QStringLiteral("photoTextDimensionOverlay"));
  require(fixture != nullptr && overlay != nullptr,
          "The controller fixture must expose the production dimensional text overlay.");

  QQuickWindow window;
  window.resize(1000, 500);
  fixture->setParentItem(window.contentItem());
  fixture->setSize(QSizeF(1000, 500));
  window.show();
  window.requestActivate();
  require(wait_until([&] { return window.isExposed(); }),
          "The dimensional text overlay window must expose its pointer surface.");
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  const auto overlay_geometry = overlay->property("displayGeometry").toMap();
  const auto rotate_handle = overlay_geometry.value(QStringLiteral("handles")).toMap()
                                 .value(QStringLiteral("rotate")).toMap();
  const QPointF start(rotate_handle.value(QStringLiteral("x")).toDouble(),
                      rotate_handle.value(QStringLiteral("y")).toDouble());
  require(std::isfinite(start.x()) && std::isfinite(start.y()) &&
              start.x() >= 0.0 && start.x() < 1000.0 && start.y() >= 0.0 && start.y() < 500.0,
          "The real rotation handle must map into the 1000x500 canvas view.");
  const QPointF finish = start + QPointF(42.0, 23.0);
  send_pointer(window, QEvent::MouseButtonPress, start);
  send_pointer(window, QEvent::MouseMove, finish);
  require(wait_until([&] { return overlay->property("interactionActive").toBool(); }),
          "A pointer drag on the projected rotation handle must begin a production gesture.");
  const auto live_pixels = changed_preview(provider, start_pixels);
  const auto draft_layer = text_layer(editor, layer_index);
  const bool draft_history_unchanged = editor.editHistoryCurrentIndex() == start_history;
  const bool draft_recipe_unchanged =
      editor.currentAssetBoundAdjustmentPatchJson() == start_recipe;
  const bool draft_model_unchanged =
      photo_paint_target_identity(draft_layer) == photo_paint_target_identity(start_layer);
  if (!draft_history_unchanged || !draft_recipe_unchanged || !draft_model_unchanged) {
    std::cerr << "Real overlay draft immutable-state details: history=" << draft_history_unchanged
              << " recipe=" << draft_recipe_unchanged << " authored_model=" << draft_model_unchanged
              << " history_before=" << start_history
              << " history_draft=" << editor.editHistoryCurrentIndex() << '\n';
  }
  require(draft_history_unchanged && draft_recipe_unchanged && draft_model_unchanged,
          "A real overlay draft must update provider pixels without mutating authored state.");
  send_pointer(window, QEvent::MouseButtonRelease, finish);
  require(wait_until([&] { return !overlay->property("interactionActive").toBool(); }),
          "Pointer release must finish the real dimensional text gesture.");
  settle(editor);
  const auto committed = text_layer(editor, layer_index);
  require(editor.editHistoryCurrentIndex() == start_history + 1 &&
              photo_paint_target_identity(committed) != photo_paint_target_identity(start_layer) &&
              editor.currentAssetBoundAdjustmentPatchJson() != start_recipe &&
              same_pixels(preview(provider), live_pixels),
          "The real overlay pointer gesture must commit one exact recipe and preview step.");
  require(committed.value(QStringLiteral("id")).toString() == layer_id,
          "The real overlay gesture must keep the captured layer identity.");
  restore_history(editor, false);
  require(editor.editHistoryCurrentIndex() == start_history &&
              photo_paint_target_identity(text_layer(editor, layer_index)) ==
                  photo_paint_target_identity(start_layer) &&
              editor.currentAssetBoundAdjustmentPatchJson() == start_recipe,
          "Undo after a real overlay gesture must restore its exact starting model.");
  restored_preview(provider, start_pixels);
  window.close();
}

QRectF layer_bounds_rect(const QVariantMap& bounds) {
  return {bounds.value(QStringLiteral("x")).toDouble(),
          bounds.value(QStringLiteral("y")).toDouble(),
          bounds.value(QStringLiteral("width")).toDouble(),
          bounds.value(QStringLiteral("height")).toDouble()};
}

QVariantList nested_dimension_candidate(const QVariantList& parent_layers,
                                        const QString& component_id,
                                        const QString& text_id,
                                        const PhotoTextDimensionSettings& settings) {
  auto layers = parent_layers;
  for (int layer_index = 0; layer_index < layers.size(); ++layer_index) {
    auto layer = layers.at(layer_index).toMap();
    if (layer.value(QStringLiteral("id")).toString() != component_id) continue;
    auto document = layer.value(QStringLiteral("smartObjectDocument")).toMap();
    auto children = document.value(QStringLiteral("layers")).toList();
    for (int child_index = 0; child_index < children.size(); ++child_index) {
      auto child = children.at(child_index).toMap();
      if (child.value(QStringLiteral("id")).toString() != text_id) continue;
      child.insert(QStringLiteral("textDimension"),
                   encode_photo_text_dimension_variant_map(settings));
      children[child_index] = child;
      document.insert(QStringLiteral("layers"), children);
      layer.insert(QStringLiteral("smartObjectDocument"), document);
      layers[layer_index] = layer;
      return layers;
    }
  }
  return {};
}

void verify_expanded_picking_and_style_mapping(EditorPreviewController& editor,
                                               int layer_index,
                                               const PhotoTextDimensionSettings& base_settings) {
  const auto before_bounds = layer_bounds_rect(editor.photoLayerBounds(layer_index));
  PhotoTextDimensionSettings extended = base_settings;
  extended.enabled = true;
  extended.depth = 620.0;
  extended.bevel_width = 3.0;
  extended.rotation_x = 0.0;
  extended.rotation_y = 38.0;
  extended.rotation_z = 0.0;
  extended.projection = PhotoTextDimensionSettings::Projection::Perspective;
  extended.focal_length = 900.0;
  extended.corner_pin_enabled = false;
  require(editor.setPhotoLayerTextDimension(layer_index,
              encode_photo_text_dimension_variant_map(extended)),
          "Set a deep angled extrusion that projects beyond the authored layer frame.");
  settle(editor);

  const auto after_bounds = layer_bounds_rect(editor.photoLayerBounds(layer_index));
  require(after_bounds.isValid() &&
              (after_bounds.width() > before_bounds.width() + 2.0 ||
               after_bounds.height() > before_bounds.height() + 2.0),
          "Layer bounds must include the expanded 3D projection outside the authored text rectangle.");
  double pick_x = 0.0;
  if (after_bounds.left() < before_bounds.left())
    pick_x = (after_bounds.left() + before_bounds.left()) * 0.5;
  else
    pick_x = (before_bounds.right() + after_bounds.right()) * 0.5;
  const double overlap_top = std::max(after_bounds.top(), before_bounds.top());
  const double overlap_bottom = std::min(after_bounds.bottom(), before_bounds.bottom());
  require(overlap_bottom > overlap_top,
          "The projected 3D bounds must overlap the original layer vertically for stable outside picking.");
  const QPointF pick_point(pick_x, (overlap_top + overlap_bottom) * 0.5);
  require(!before_bounds.contains(pick_point) &&
              editor.photoLayerContainsPoint(layer_index, pick_point.x(), pick_point.y()),
          "The expanded 3D extrusion must be pickable beyond the original text rectangle.");

  require(editor.addPhotoLayerStyle(layer_index, QStringLiteral("drop_shadow")),
          "Add a normal layer style to verify cross-tool coordinates over padded dimensional text.");
  settle(editor);
  const auto styles = editor.photoLayerStyles(layer_index);
  const auto style_id = styles.isEmpty() ? QString() :
      styles.constLast().toMap().value(QStringLiteral("id")).toString();
  const auto style_geometry = editor.photoLayerStyleCanvasGeometry(layer_index, style_id);
  const double style_width = style_geometry.value(QStringLiteral("styleWidth")).toDouble();
  const double style_height = style_geometry.value(QStringLiteral("styleHeight")).toDouble();
  const auto authored = text_layer(editor, layer_index);
  require(!style_id.isEmpty() && style_geometry.value(QStringLiteral("valid")).toBool() &&
              style_width > authored.value(QStringLiteral("pixelWidth")).toDouble() &&
              style_height >= authored.value(QStringLiteral("pixelHeight")).toDouble(),
          "The standard layer-style surface must use the expanded padded dimensional-text raster.");
  const std::array<QPointF, 3> points{
      QPointF(style_width * 0.5, style_height * 0.5),
      QPointF(style_width * 0.15, style_height * 0.25),
      QPointF(style_width * 0.85, style_height * 0.75)};
  for (const auto& point : points) {
    const auto view = editor.mapPhotoLayerStyleCoordinate(layer_index, point.x(), point.y(), true);
    require(view.value(QStringLiteral("valid")).toBool(),
            "A padded style-raster point must map into the document canvas.");
    const auto round_trip = editor.mapPhotoLayerStyleCoordinate(layer_index,
        view.value(QStringLiteral("x")).toDouble(), view.value(QStringLiteral("y")).toDouble(), false);
    require(round_trip.value(QStringLiteral("valid")).toBool() &&
                std::abs(round_trip.value(QStringLiteral("x")).toDouble() - point.x()) < 1.5 &&
                std::abs(round_trip.value(QStringLiteral("y")).toDouble() - point.y()) < 1.5,
            "Cross-tool style coordinates must invert the padded raster placement without shifting authored pixels.");
  }

  const auto layer_raster_size = QSize(
      qMax(1, qRound(authored.value(QStringLiteral("pixelWidth")).toDouble() *
                     style_geometry.value(QStringLiteral("styleScaleX")).toDouble())),
      qMax(1, qRound(authored.value(QStringLiteral("pixelHeight")).toDouble() *
                     style_geometry.value(QStringLiteral("styleScaleY")).toDouble())));
  const auto rendered_text_frame = render_photo_text_dimension(authored, layer_raster_size);
  require(rendered_text_frame.error.isEmpty() && !rendered_text_frame.image.isNull() &&
              style_width == rendered_text_frame.image.width() &&
              style_height == rendered_text_frame.image.height(),
          "The style canvas base frame must match the real expanded 3D text raster.");
  const auto round_trip_style_frame = [&](double width, double height) {
    const std::array<QPointF, 4> samples{
        QPointF(width * 0.08, height * 0.12), QPointF(width * 0.5, height * 0.5),
        QPointF(width * 0.92, height * 0.28), QPointF(width * 0.78, height * 0.9)};
    for (const auto& point : samples) {
      const auto view = editor.mapPhotoLayerStyleCoordinate(
          layer_index, point.x(), point.y(), true);
      if (!view.value(QStringLiteral("valid")).toBool()) return false;
      const auto back = editor.mapPhotoLayerStyleCoordinate(layer_index,
          view.value(QStringLiteral("x")).toDouble(),
          view.value(QStringLiteral("y")).toDouble(), false);
      if (!back.value(QStringLiteral("valid")).toBool() ||
          std::abs(back.value(QStringLiteral("x")).toDouble() - point.x()) >= 1.5 ||
          std::abs(back.value(QStringLiteral("y")).toDouble() - point.y()) >= 1.5)
        return false;
    }
    return true;
  };

  require(editor.previewPhotoLayerEffect(layer_index, QStringLiteral("gaussian_blur")) &&
              editor.finishPhotoLayerEffectPreview(true),
          "Add and accept a transparent-edge Gaussian filter on dimensional text.");
  settle(editor);
  const auto effect_rows = editor.photoLayerEffects(layer_index);
  QString gaussian_id;
  for (const auto& entry : effect_rows) {
    const auto row = entry.toMap();
    if (row.value(QStringLiteral("type")).toString() == QStringLiteral("gaussian_blur"))
      gaussian_id = row.value(QStringLiteral("id")).toString();
  }
  require(!gaussian_id.isEmpty(), "Read back the accepted Gaussian effect identifier.");
  const auto filter_layer = text_layer(editor, layer_index);
  const auto decoded_effects = decode_photo_layer_effects(filter_layer);
  require(decoded_effects.has_value() && decoded_effects->size() == 1,
          "Read the canonical Gaussian effect for independent support calculation.");
  photo_editor::pipeline::PhotoEffectSupport filter_support;
  const auto filter_support_result = photo_editor::pipeline::photo_effect_stack_support(
      *decoded_effects,
      {.scale_x = style_geometry.value(QStringLiteral("styleScaleX")).toDouble(),
       .scale_y = style_geometry.value(QStringLiteral("styleScaleY")).toDouble()},
      &filter_support);
  require(filter_support_result.ok() && filter_support.x > 0 && filter_support.y > 0,
          "The transparent Gaussian must report positive support on both axes.");
  const auto transparent_geometry =
      editor.photoLayerStyleCanvasGeometry(layer_index, style_id);
  const double transparent_width =
      transparent_geometry.value(QStringLiteral("styleWidth")).toDouble();
  const double transparent_height =
      transparent_geometry.value(QStringLiteral("styleHeight")).toDouble();
  require(transparent_geometry.value(QStringLiteral("valid")).toBool() &&
              transparent_width == rendered_text_frame.image.width() + 2 * filter_support.x &&
              transparent_height == rendered_text_frame.image.height() + 2 * filter_support.y &&
              round_trip_style_frame(transparent_width, transparent_height),
          "Transparent Gaussian style coordinates must match the exact post-filter 3D frame and invert across its padded extent.");

  require(editor.setPhotoLayerEffectEdgeMode(layer_index, gaussian_id, 2),
          "Switch the Gaussian filter to reflect edges.");
  settle(editor);
  const auto reflected_geometry =
      editor.photoLayerStyleCanvasGeometry(layer_index, style_id);
  require(reflected_geometry.value(QStringLiteral("valid")).toBool() &&
              reflected_geometry.value(QStringLiteral("styleWidth")).toDouble() ==
                  rendered_text_frame.image.width() &&
              reflected_geometry.value(QStringLiteral("styleHeight")).toDouble() ==
                  rendered_text_frame.image.height() &&
              round_trip_style_frame(
                  reflected_geometry.value(QStringLiteral("styleWidth")).toDouble(),
                  reflected_geometry.value(QStringLiteral("styleHeight")).toDouble()),
          "Reflect-edge filtering must preserve the rendered 3D frame without transparent-boundary expansion.");

  require(editor.setPhotoLayerEffectEdgeMode(layer_index, gaussian_id, 0) &&
              editor.setPhotoLayerEffectEnabled(layer_index, gaussian_id, false),
          "Restore transparent edges and disable the Gaussian filter.");
  settle(editor);
  const auto disabled_geometry = editor.photoLayerStyleCanvasGeometry(layer_index, style_id);
  require(disabled_geometry.value(QStringLiteral("valid")).toBool() &&
              disabled_geometry.value(QStringLiteral("styleWidth")).toDouble() ==
                  rendered_text_frame.image.width() &&
              disabled_geometry.value(QStringLiteral("styleHeight")).toDouble() ==
                  rendered_text_frame.image.height(),
          "A disabled transparent filter must not expand the standard style canvas frame.");
  require(editor.removePhotoLayerEffect(layer_index, gaussian_id),
          "Remove the temporary filter after frame and inverse-map coverage.");
  settle(editor);
  require(editor.removePhotoLayerStyle(layer_index, style_id),
          "Remove the temporary canvas-mapping style after its round-trip check.");
  settle(editor);
  require(editor.setPhotoLayerTextDimension(layer_index,
              encode_photo_text_dimension_variant_map(base_settings)),
          "Restore the original 3D text settings after expanded-frame picking coverage.");
  settle(editor);
}

void verify_component_dimension_apply_cancel_reopen(const QString& source_path) {
  EditorPreviewImageProvider provider;
  EditorPreviewController root;
  root.setImageProvider(&provider);
  root.loadAsset(QStringLiteral("photo-text-dimension-component-workflow"), source_path,
                {}, {}, {}, {}, {}, true);
  settle(root);
  require(root.addTextLayer(QStringLiteral("O"), QStringLiteral("Component dimensional text")),
          "Create editable dimensional text for a nested component document.");
  const QString text_id = text_layer(root, 0).value(QStringLiteral("id")).toString();
  const auto font_family = QGuiApplication::font().family();
  require(root.setPhotoLayerTextBox(0, 280, 120) &&
              root.setPhotoLayerTextStyle(0, font_family, 48.0, QStringLiteral("#fff5e4d0"),
                                         QStringLiteral("regular"), QStringLiteral("left")) &&
              root.setPhotoLayerVectorMask(0, rectangle_mask(QStringLiteral("component-dimension-mask"))),
          "Author a masked editable text layer before component conversion.");
  PhotoTextDimensionSettings initial_dimension;
  initial_dimension.enabled = true;
  initial_dimension.depth = 62.0;
  initial_dimension.bevel_width = 4.0;
  initial_dimension.bevel_segments = 4;
  initial_dimension.rotation_x = -12.0;
  initial_dimension.rotation_y = 19.0;
  initial_dimension.focal_length = 900.0;
  initial_dimension.use_text_color = false;
  initial_dimension.front_color = QColor(QStringLiteral("#ffefad68"));
  initial_dimension.side_color = QColor(QStringLiteral("#ff314869"));
  initial_dimension.bevel_color = QColor(QStringLiteral("#ffffe6bb"));
  initial_dimension.light_intensity = 3.5;
  require(root.setPhotoLayerTextDimension(0,
              encode_photo_text_dimension_variant_map(initial_dimension)),
          "Enable a real extrusion, bevel, camera and material before component conversion.");
  require(root.addFillLayer(QStringLiteral("#43576c"), QStringLiteral("Component backing")),
          "Add a backing layer to make a multi-child component.");
  settle(root);
  const QString fill_id = text_layer(root, 0).value(QStringLiteral("id")).toString();
  const int added_fill_index = text_layer_index(root, fill_id);
  require(added_fill_index >= 0 && root.movePhotoLayer(added_fill_index, 1),
          "Move the component backing below the dimensional text so accepted camera edits remain visible.");
  settle(root);
  int text_index = text_layer_index(root, text_id);
  const int fill_index = text_layer_index(root, fill_id);
  require(!text_id.isEmpty() && !fill_id.isEmpty() && text_index >= 0 && fill_index >= 0 &&
              root.groupPhotoLayers(QVariantList{text_index, fill_index}, QStringLiteral("Dimensional title component")),
          "Group the dimensioned text and backing into a nested component placement.");
  settle(root);
  const auto grouped_layers = EditorPreviewControllerTestAccess::state(root).layers;
  QString group_id;
  for (const auto& value : grouped_layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("group")) {
      group_id = layer.value(QStringLiteral("id")).toString();
      break;
    }
  }
  const int group_index = text_layer_index(root, group_id);
  require(!group_id.isEmpty() && group_index >= 0 && root.convertPhotoLayerToComponent(group_index),
          "Convert the nested dimensional text group into an editable component document.");
  settle(root);

  const auto admitted_state = EditorPreviewControllerTestAccess::state(root);
  const auto root_before_rejection = root.currentAssetBoundAdjustmentPatchJson();
  const auto pixels_before_rejection = preview(provider);
  const int history_before_rejection = root.editHistoryCurrentIndex();
  auto invalid_dimension = initial_dimension;
  invalid_dimension.depth = 0.0;
  invalid_dimension.bevel_width = 0.0;
  invalid_dimension.pivot_x = 0.1;
  invalid_dimension.focal_length = 15.0;
  invalid_dimension.rotation_x = 0.0;
  invalid_dimension.rotation_y = 60.0;
  invalid_dimension.rotation_z = 0.0;
  require(decode_photo_text_dimension_variant_map(
              encode_photo_text_dimension_variant_map(invalid_dimension)).has_value(),
          "The recursive-admission rejection fixture must use a schema-valid settings envelope.");
  const auto nested_document = text_layer(root, group_index)
                                   .value(QStringLiteral("smartObjectDocument")).toMap();
  auto original_children = nested_document.value(QStringLiteral("layers")).toList();
  QVariantMap original_nested_text;
  for (const auto& value : original_children)
    if (value.toMap().value(QStringLiteral("id")).toString() == text_id)
      original_nested_text = value.toMap();
  require(!original_nested_text.isEmpty(),
          "The component fixture must expose its nested dimensional text child.");
  const auto nested_outlines = photo_text_dimension_outlines(original_nested_text);
  require(nested_outlines.error.isEmpty() && !nested_outlines.faces.isEmpty(),
          "The recursive-admission text fixture must have a real glyph outline.");
  double outline_left = std::numeric_limits<double>::infinity();
  double outline_right = -std::numeric_limits<double>::infinity();
  for (const auto& face : nested_outlines.faces) {
    outline_left = std::min(outline_left, face.path.boundingRect().left());
    outline_right = std::max(outline_right, face.path.boundingRect().right());
  }
  invalid_dimension.pivot_x = std::clamp((outline_left + outline_right) * 0.5 /
      original_nested_text.value(QStringLiteral("pixelWidth")).toDouble(), 0.0, 1.0);
  invalid_dimension.focal_length = 10.0;
  auto invalid_nested_text = original_nested_text;
  invalid_nested_text.insert(QStringLiteral("textDimension"),
      encode_photo_text_dimension_variant_map(invalid_dimension));
  QString invalid_geometry_error;
  const auto nested_canvas = QSize(nested_document.value(QStringLiteral("canvasWidth")).toInt(),
                                   nested_document.value(QStringLiteral("canvasHeight")).toInt());
  require(!preflight_photo_text_dimension(invalid_nested_text, nested_canvas,
                                           &invalid_geometry_error).isValid() &&
              invalid_geometry_error.contains(QStringLiteral("near plane"), Qt::CaseInsensitive),
          "The nested rejection settings must produce a partial near-plane crossing in real 3D geometry.");
  const auto rejected_layers = nested_dimension_candidate(admitted_state.layers,
      group_id, text_id, invalid_dimension);
  require(!rejected_layers.isEmpty() &&
              !EditorPreviewControllerTestAccess::apply_layer_stack_change(root, rejected_layers) &&
              root.editHistoryCurrentIndex() == history_before_rejection &&
              EditorPreviewControllerTestAccess::state(root).layers == admitted_state.layers &&
              root.currentAssetBoundAdjustmentPatchJson() == root_before_rejection &&
              same_pixels(preview(provider), pixels_before_rejection),
          "Recursive admission must atomically reject an invalid nested 3D layer without recipe, pixels or history changes.");
  auto accepted_dimension = initial_dimension;
  accepted_dimension.rotation_z = 6.0;
  const auto accepted_layers = nested_dimension_candidate(admitted_state.layers,
      group_id, text_id, accepted_dimension);
  require(!accepted_layers.isEmpty() &&
              EditorPreviewControllerTestAccess::apply_layer_stack_change(root, accepted_layers),
          "Recursive admission must accept a valid nested dimensional text update.");
  settle(root);
  const int history_after_nested_apply = root.editHistoryCurrentIndex();
  const auto nested_state_after_apply = EditorPreviewControllerTestAccess::state(root);
  const auto recipe_after_nested_apply = root.currentAssetBoundAdjustmentPatchJson();
  const auto pixels_after_nested_apply = preview(provider);
  const int applied_group_index = text_layer_index(root, group_id);
  const auto applied_group = applied_group_index >= 0
      ? text_layer(root, applied_group_index) : QVariantMap{};
  const auto applied_document = applied_group.value(QStringLiteral("smartObjectDocument")).toMap();
  QVariantMap applied_nested_text;
  for (const auto& value : applied_document.value(QStringLiteral("layers")).toList()) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("id")).toString() == text_id) {
      applied_nested_text = layer;
      break;
    }
  }
  const auto observed_nested_dimension = decode_photo_text_dimension_variant_map(
      applied_nested_text.value(QStringLiteral("textDimension")).toMap());
  const bool accepted_history_is_one =
      history_after_nested_apply == history_before_rejection + 1;
  const bool accepted_recipe_changed = recipe_after_nested_apply != root_before_rejection;
  const bool accepted_state_changed = nested_state_after_apply.layers != admitted_state.layers;
  const bool accepted_nested_dimension_changed =
      observed_nested_dimension == std::optional<PhotoTextDimensionSettings>(accepted_dimension);
  const bool accepted_component_mask_preserved =
      !applied_nested_text.isEmpty() &&
      applied_nested_text.value(QStringLiteral("vectorMask")) ==
          original_nested_text.value(QStringLiteral("vectorMask"));
  const bool accepted_nested_pixels_changed =
      !same_pixels(pixels_after_nested_apply, pixels_before_rejection);
  if (!(accepted_history_is_one && accepted_nested_pixels_changed)) {
    std::cerr << QStringLiteral(
        "Nested Apply diagnostic: history before=%1 after=%2 expectedAfter=%3 historyOne=%4 "
        "recipeChanged=%5 rootLayersChanged=%6 nestedDimensionMatchesAccepted=%7 "
        "componentMaskPreserved=%8 componentChildren=%9 groupIndex=%10 textIndex=%11 "
        "pixelsChanged=%12 differingPixels=%13 beforeSize=%14x%15 afterSize=%16x%17 "
        "acceptedRotationZ=%18 appliedRotationZ=%19")
        .arg(history_before_rejection).arg(history_after_nested_apply)
        .arg(history_before_rejection + 1).arg(accepted_history_is_one)
        .arg(accepted_recipe_changed).arg(accepted_state_changed)
        .arg(accepted_nested_dimension_changed).arg(accepted_component_mask_preserved)
        .arg(applied_document.value(QStringLiteral("layers")).toList().size())
        .arg(applied_group_index).arg(text_layer_index(root, text_id))
        .arg(accepted_nested_pixels_changed)
        .arg(differing_pixel_count(pixels_after_nested_apply, pixels_before_rejection))
        .arg(pixels_before_rejection.width()).arg(pixels_before_rejection.height())
        .arg(pixels_after_nested_apply.width()).arg(pixels_after_nested_apply.height())
        .arg(accepted_dimension.rotation_z)
        .arg(observed_nested_dimension ? observed_nested_dimension->rotation_z : -999.0)
        .toStdString() << '\n';
  }
  initial_dimension = accepted_dimension;
  require(accepted_history_is_one && accepted_recipe_changed &&
              accepted_state_changed && accepted_nested_dimension_changed &&
              accepted_component_mask_preserved && accepted_nested_pixels_changed,
          "An accepted nested 3D change must create one parent history entry and update pixels.");

  const auto root_before_cancel = EditorPreviewControllerTestAccess::state(root);
  const auto recipe_before_cancel = root.currentAssetBoundAdjustmentPatchJson();
  const auto pixels_before_cancel = preview(provider);
  const int history_before_cancel = root.editHistoryCurrentIndex();
  PhotoComponentWorkspaceController workspace(&root);
  require(workspace.open(text_layer_index(root, group_id)),
          "Open the component containing the masked 3D text.");
  require(wait_until([&] { return !workspace.busy() && workspace.active(); }),
          "The dimensional text component must finish opening.");
  auto* child = workspace.controller();
  require(child != nullptr && child != &root,
          "Nested dimensional text must be edited in its isolated component controller.");
  settle(*child);
  text_index = text_layer_index(*child, text_id);
  require(text_index >= 0 && photo_text_dimension_is_active(text_layer(*child, text_index)),
          "The opened component must retain its active 3D text child.");
  const auto original_text = text_layer(*child, text_index);
  const auto original_mask = original_text.value(QStringLiteral("vectorMask"));
  const auto original_child_dimension = child->photoLayerTextDimension(text_index);
  require(original_child_dimension == encode_photo_text_dimension_variant_map(initial_dimension),
          "Opening the component must preserve exact editable dimensional settings.");

  auto cancelled_dimension = initial_dimension;
  cancelled_dimension.rotation_y += 17.0;
  require(child->setPhotoLayerTextDimension(text_index,
              encode_photo_text_dimension_variant_map(cancelled_dimension)),
          "Author a child 3D camera draft that will be cancelled.");
  settle(*child);
  require(root.currentAssetBoundAdjustmentPatchJson() == recipe_before_cancel &&
              root.editHistoryCurrentIndex() == history_before_cancel &&
              same_pixels(preview(provider), pixels_before_cancel),
          "A nested component 3D edit must remain private before Apply.");
  require(workspace.cancel(), "Cancel the nested dimensional text component edit.");
  require(wait_until([&] { return !workspace.busy() && !workspace.active(); }),
          "Cancelling the dimensional text component must close its workspace.");
  settle(root);
  require(EditorPreviewControllerTestAccess::state(root).layers == root_before_cancel.layers &&
              root.currentAssetBoundAdjustmentPatchJson() == recipe_before_cancel &&
              root.editHistoryCurrentIndex() == history_before_cancel &&
              same_pixels(preview(provider), pixels_before_cancel),
          "Component Cancel must preserve the exact parent 3D recipe, pixels and history.");

  require(workspace.open(text_layer_index(root, group_id)),
          "Reopen the cancelled dimensional text component.");
  require(wait_until([&] { return !workspace.busy() && workspace.active(); }),
          "The dimensional text component must reopen after Cancel.");
  child = workspace.controller();
  require(child != nullptr && child != &root,
          "Reopened dimensional text must keep its isolated child editor.");
  settle(*child);
  text_index = text_layer_index(*child, text_id);
  require(text_index >= 0 && child->photoLayerTextDimension(text_index) == original_child_dimension &&
              text_layer(*child, text_index).value(QStringLiteral("vectorMask")) == original_mask,
          "Reopen after Cancel must restore the original 3D settings and vector mask.");

  auto applied_dimension = initial_dimension;
  applied_dimension.rotation_x = 7.0;
  applied_dimension.rotation_y = -21.0;
  applied_dimension.metallic = 0.42;
  require(child->setPhotoLayerTextDimension(text_index,
              encode_photo_text_dimension_variant_map(applied_dimension)),
          "Author an accepted camera and material edit in the nested component.");
  settle(*child);
  const auto child_render = render_photo_text_dimension(text_layer(*child, text_index), QSize(280, 120));
  require(child_render.error.isEmpty() && child_render.image.size().width() > 280,
          "The accepted component text edit must still render expanded 3D pixels.");
  require(workspace.canApply() && workspace.apply(),
          "Apply the accepted dimensional text settings to the root component document.");
  require(wait_until([&] { return !workspace.busy() && !workspace.active(); }),
          "Applying dimensional text must close the isolated component workspace.");
  settle(root);
  const auto root_after_apply = EditorPreviewControllerTestAccess::state(root);
  const auto applied_component = text_layer(root, text_layer_index(root, group_id))
                                     .value(QStringLiteral("smartObjectDocument")).toMap();
  const auto component_layers = applied_component.value(QStringLiteral("layers")).toList();
  QVariantMap applied_text;
  for (const auto& value : component_layers)
    if (value.toMap().value(QStringLiteral("id")).toString() == text_id) applied_text = value.toMap();
  require(root.editHistoryCurrentIndex() == history_before_cancel + 1 &&
              photo_text_dimension_is_active(applied_text) &&
              decode_photo_text_dimension_variant_map(applied_text.value(QStringLiteral("textDimension")).toMap()) ==
                  std::optional<PhotoTextDimensionSettings>(applied_dimension) &&
              applied_text.value(QStringLiteral("vectorMask")) == original_mask,
          "Component Apply must publish editable 3D settings and mask in exactly one parent undo step.");
  const auto pixels_after_apply = preview(provider);
  require(!same_pixels(pixels_after_apply, pixels_before_cancel),
          "Applying component dimensional text must change the rendered parent pixels.");

  restore_history(root, false);
  require(EditorPreviewControllerTestAccess::state(root).layers == root_before_cancel.layers &&
              root.currentAssetBoundAdjustmentPatchJson() == recipe_before_cancel &&
              same_pixels(preview(provider), pixels_before_cancel),
          "One parent Undo must restore the original nested 3D component pixels and recipe.");
  restore_history(root, true);
  require(EditorPreviewControllerTestAccess::state(root).layers == root_after_apply.layers &&
              root.currentAssetBoundAdjustmentPatchJson() != recipe_before_cancel &&
              same_pixels(preview(provider), pixels_after_apply),
          "Redo must restore the exact applied nested 3D text pixels and recipe.");
  require(workspace.open(text_layer_index(root, group_id)),
          "Reopen the applied dimensional text component to verify persisted child state.");
  require(wait_until([&] { return !workspace.busy() && workspace.active(); }),
          "The applied dimensional text component must finish reopening.");
  child = workspace.controller();
  settle(*child);
  text_index = text_layer_index(*child, text_id);
  require(text_index >= 0 && child->photoLayerTextDimension(text_index) ==
              encode_photo_text_dimension_variant_map(applied_dimension) &&
              text_layer(*child, text_index).value(QStringLiteral("vectorMask")) == original_mask,
          "Reopen after Apply must retain the exact 3D recipe and editable mask.");
  require(workspace.cancel(), "Close the reopened applied component without a new edit.");
  require(wait_until([&] { return !workspace.busy() && !workspace.active(); }),
          "The final component close must finish cleanly.");
  settle(root);
  require(EditorPreviewControllerTestAccess::state(root).layers == root_after_apply.layers &&
              same_pixels(preview(provider), pixels_after_apply),
          "Cancelling an unchanged reopened component must not alter the accepted Apply.");
}

int text_layer_index(const EditorPreviewController& editor, const QString& id) {
  const auto layers = EditorPreviewControllerTestAccess::state(editor).layers;
  for (int index = 0; index < layers.size(); ++index)
    if (layers.at(index).toMap().value(QStringLiteral("id")).toString() == id) return index;
  return -1;
}

QVariantMap text_layer(const EditorPreviewController& editor, int index) {
  const auto layers = EditorPreviewControllerTestAccess::state(editor).layers;
  require(index >= 0 && index < layers.size(), "The dimensional text layer must remain addressable.");
  return layers.at(index).toMap();
}

void restore_history(EditorPreviewController& editor, bool redo) {
  const QString asset_id = editor.assetId();
  bool finished = false;
  bool succeeded = false;
  const auto connection = QObject::connect(
      &editor, &EditorPreviewController::historyRestoreFinished, &editor,
      [&asset_id, &finished, &succeeded](const QString& restored_id, bool success) {
        if (restored_id != asset_id) return;
        finished = true;
        succeeded = success;
      });
  if (redo) editor.redo();
  else editor.undo();
  require(wait_until([&] { return finished; }), "The text history restore must finish.");
  QObject::disconnect(connection);
  require(succeeded, "The text history restore must succeed.");
  settle(editor);
}

QVariantMap rectangle_mask(const QString& id) {
  vector_paths::VectorPathDocument document;
  QString error;
  require(vector_paths::add_subpath(&document, id, false, &error),
          "Create the text vector-mask contour.");
  const QVector<QPointF> points{{0.03, 0.04}, {0.97, 0.04},
                                {0.97, 0.96}, {0.03, 0.96}};
  for (qsizetype index = 0; index < points.size(); ++index) {
    vector_paths::PathAnchor anchor;
    anchor.id = QStringLiteral("%1-anchor-%2").arg(id).arg(index);
    anchor.position = points.at(index);
    require(vector_paths::append_anchor(&document, id, anchor, &error),
            "Append the text vector-mask contour.");
  }
  require(vector_paths::set_closed(&document, id, true, &error),
          "Close the text vector-mask contour.");
  return vector_paths::to_variant(document, &error);
}

QImage cpu_composite(const QImage& source, const EditorPreviewController& editor) {
  const auto result = render_output_image_with_backend(
      source, EditorPreviewControllerTestAccess::state(editor),
      QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  require(!result.isNull(), "The dimensional text CPU composite must render.");
  return result;
}

double maximum_linear_channel(const QImage& image) {
  const auto linear = image.convertToFormat(QImage::Format_RGBA32FPx4);
  double maximum = 0.0;
  for (int y = 0; y < linear.height(); ++y) {
    const auto* row = reinterpret_cast<const QRgbaFloat32*>(linear.constScanLine(y));
    for (int x = 0; x < linear.width(); ++x)
      maximum = std::max({maximum, static_cast<double>(row[x].red()),
                          static_cast<double>(row[x].green()),
                          static_cast<double>(row[x].blue())});
  }
  return maximum;
}

void same_text_model(const QVariantMap& before, const QVariantMap& after) {
  for (const auto* key : {"text", "fontFamily", "fontSize", "fontWeight", "textColor",
                          "textAlignment", "textTypography", "vectorMask"}) {
    require(before.value(QLatin1String(key)) == after.value(QLatin1String(key)),
            "Dimensional text edits must retain editable content, character runs, and masks.");
  }
}

void run_workflow() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "Create isolated storage for the dimensional text workflow.");
  QImage source(QSize(1000, 500), QImage::Format_ARGB32);
  source.fill(QColor(QStringLiteral("#33475d")));
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto source_path = temporary.filePath(QStringLiteral("dimension-source.png"));
  require(source.save(source_path, "PNG"), "Write the dimensional text source image.");

  EditorPreviewImageProvider provider;
  EditorPreviewController editor;
  editor.setImageProvider(&provider);
  editor.loadAsset(QStringLiteral("photo-text-dimension-controller-workflow"), source_path,
                   {}, {}, {}, {}, {}, true);
  settle(editor);
  require(editor.addTextLayer(QStringLiteral("O"), QStringLiteral("Editable dimensional title")),
          "Create an editable text layer with a counter outline.");
  settle(editor);
  const QString layer_id = text_layer(editor, 0).value(QStringLiteral("id")).toString();
  require(!layer_id.isEmpty() && editor.setPhotoLayerTextBox(0, 280, 120),
          "Give the editable text a stable authored box.");
  const auto family = QGuiApplication::font().family();
  require(editor.setPhotoLayerTextStyle(0, family, 48.0, QStringLiteral("#ffb5d5f0"),
                                        QStringLiteral("regular"), QStringLiteral("left")),
          "Set the legacy editable text face before enabling geometry.");
  require(editor.setPhotoLayerTextRangeStyle(0, 0, 1,
              {{QStringLiteral("family"), family}, {QStringLiteral("size"), 46.0},
               {QStringLiteral("color"), QStringLiteral("#ffd1e8ff")},
               {QStringLiteral("underline"), false}}, QStringLiteral("O")),
          "Attach an editable character run to the title.");
  require(editor.setPhotoLayerVectorMask(0, rectangle_mask(QStringLiteral("dimension-text-mask"))),
          "Attach a vector mask that dimensional authoring must preserve.");
  settle(editor);

  const auto initial_layer = text_layer(editor, 0);
  require(!initial_layer.contains(QStringLiteral("textDimension")) &&
              !photo_text_dimension_is_active(initial_layer) &&
              !editor.photoLayerTypography(0).value(QStringLiteral("runs")).toList().isEmpty(),
          "Legacy text remains unchanged by default and retains its editable run model.");
  const auto authored_size = QSize(initial_layer.value(QStringLiteral("pixelWidth")).toInt(),
                                   initial_layer.value(QStringLiteral("pixelHeight")).toInt());
  const auto legacy_image = rasterize_legacy_photo_text(initial_layer, authored_size);
  const auto legacy_frame = render_photo_text_dimension(initial_layer, authored_size);
  QImage expected_legacy = legacy_image;
  if (!expected_legacy.colorSpace().isValid()) expected_legacy.setColorSpace(QColorSpace(QColorSpace::SRgb));
  expected_legacy = expected_legacy.convertedToColorSpace(
      QColorSpace(QColorSpace::SRgbLinear), QImage::Format_RGBA32FPx4);
  require(legacy_frame.error.isEmpty() && same_float_pixels(legacy_frame.image, expected_legacy),
          "The adapter must preserve absent-field legacy glyph pixels exactly.");
  const auto initial_preview = preview(provider);

  PhotoTextDimensionSettings disabled;
  // Make the persisted disabled envelope differ from the implicit default,
  // while still proving that the renderer ignores all dormant 3D values.
  disabled.depth = 0.0;
  auto disabled_map = encode_photo_text_dimension_variant_map(disabled);
  require(!disabled_map.isEmpty() && editor.setPhotoLayerTextDimension(0, disabled_map),
          "Persist a disabled dimensional envelope without changing its legacy rendering.");
  settle(editor);
  const auto disabled_layer = text_layer(editor, 0);
  const auto disabled_frame = render_photo_text_dimension(disabled_layer, authored_size);
  require(disabled_frame.error.isEmpty() &&
              same_float_pixels(disabled_frame.image, expected_legacy) &&
              same_pixels(provider.image(QStringLiteral("edited")), initial_preview),
          "A disabled dimensional envelope must preserve legacy provider and raster pixels.");
  same_text_model(initial_layer, disabled_layer);

  PhotoTextDimensionSettings settings = disabled;
  settings.enabled = true;
  settings.depth = 58.0;
  settings.bevel_width = 5.0;
  settings.bevel_segments = 4;
  settings.rotation_x = -16.0;
  settings.rotation_y = 23.0;
  settings.rotation_z = 2.0;
  settings.projection = PhotoTextDimensionSettings::Projection::Perspective;
  settings.focal_length = 900.0;
  settings.use_text_color = false;
  settings.front_color = QColor(QStringLiteral("#fff1a35b"));
  settings.side_color = QColor(QStringLiteral("#ff334fe5"));
  settings.bevel_color = QColor(QStringLiteral("#ffffe5a0"));
  settings.roughness = 0.36;
  settings.metallic = 0.24;
  settings.light_azimuth = 25.0;
  settings.light_elevation = 52.0;
  settings.light_intensity = 8.0;
  settings.ambient = 0.28;
  const auto enabled_map = encode_photo_text_dimension_variant_map(settings);
  require(!enabled_map.isEmpty() && editor.setPhotoLayerTextDimension(0, enabled_map),
          "Enable real extruded, beveled, perspective text on the production controller.");
  settle(editor);
  const auto enabled_layer = text_layer(editor, 0);
  const auto enabled_preview = preview(provider);
  const auto geometry = editor.photoTextDimensionCanvasGeometry(0);
  const auto dimension_frame = render_photo_text_dimension(enabled_layer, authored_size);
  require(photo_text_dimension_is_active(enabled_layer) && geometry.value(QStringLiteral("valid")).toBool() &&
              geometry.value(QStringLiteral("editable")).toBool() && dimension_frame.error.isEmpty() &&
              dimension_frame.image.format() == QImage::Format_RGBA32FPx4 &&
              dimension_frame.image.size().width() > authored_size.width() &&
              dimension_frame.image.size().height() > authored_size.height() &&
              !same_pixels(enabled_preview, initial_preview),
          "Extrusion, bevel and camera rotation must change real provider pixels and expand output bounds.");
  require(maximum_linear_channel(dimension_frame.image) > 1.0,
          "Directional light intensity must retain linear HDR headroom in the direct text frame.");
  same_text_model(initial_layer, enabled_layer);

  verify_real_overlay_gesture(editor, provider, 0, layer_id);
  settle(editor);
  verify_expanded_picking_and_style_mapping(editor, 0, settings);

  PhotoTextDimensionSettings flat_camera;
  flat_camera.enabled = true;
  flat_camera.depth = 0.0;
  flat_camera.bevel_width = 0.0;
  flat_camera.rotation_x = 0.0;
  flat_camera.rotation_y = 0.0;
  flat_camera.rotation_z = 0.0;
  flat_camera.focal_length = 1000.0;
  flat_camera.use_text_color = true;
  const auto flat_map = encode_photo_text_dimension_variant_map(flat_camera);
  require(editor.setPhotoLayerTextDimension(0, flat_map),
          "Set a zero-depth, front-facing camera to verify flat counter handling.");
  settle(editor);
  const auto flat_layer = text_layer(editor, 0);
  const auto flat_frame = render_photo_text_dimension(flat_layer, authored_size);
  const auto outlines = photo_text_dimension_outlines(flat_layer);
  require(flat_frame.error.isEmpty() && outlines.error.isEmpty() && !outlines.faces.isEmpty(),
          "Zero depth with a front-facing camera must still rasterize editable glyph outlines.");
  const auto hole = outlines.faces.front().path.boundingRect().center();
  require(!outlines.faces.front().path.contains(hole), "The O glyph fixture must have a real counter.");
  const int hole_x = qRound(hole.x()) + flat_frame.padding.x();
  const int hole_y = qRound(hole.y()) + flat_frame.padding.y();
  const auto* flat_pixels = reinterpret_cast<const QRgbaFloat32*>(flat_frame.image.constScanLine(hole_y));
  require(flat_pixels[hole_x].alpha() == 0.0F,
          "A zero-depth, front-facing render must keep the glyph counter transparent.");

  // Restore extrusion, then exercise a canvas preview transaction. Drafts
  // reach only the preview provider until release commits one history entry.
  require(editor.setPhotoLayerTextDimension(0, enabled_map),
          "Restore the editable beveled 3D camera for gesture coverage.");
  settle(editor);
  const int gesture_start_history = editor.editHistoryCurrentIndex();
  const auto gesture_start_layer = text_layer(editor, 0);
  const auto gesture_start_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto gesture_start_preview = preview(provider);
  const auto gesture_geometry = editor.photoTextDimensionCanvasGeometry(0);
  const QString document_token = gesture_geometry.value(QStringLiteral("documentToken")).toString();
  const QString revision = gesture_geometry.value(QStringLiteral("revision")).toString();
  require(editor.beginTextDimensionGesture(0, layer_id, document_token, revision),
          "Begin the production dimensional text gesture using its document target identity.");
  const QVariantMap cancel_patch{{QStringLiteral("rotationX"), 34.0},
                                 {QStringLiteral("rotationY"), -31.0}};
  require(editor.previewTextDimensionGesture(0, layer_id, cancel_patch, document_token, revision),
          "Preview the first dimensional text rotation draft.");
  const auto cancelled_draft_preview = changed_preview(provider, gesture_start_preview);
  const auto cancel_draft_layer = text_layer(editor, 0);
  const bool cancel_history_unchanged = editor.editHistoryCurrentIndex() == gesture_start_history;
  const bool cancel_recipe_unchanged =
      editor.currentAssetBoundAdjustmentPatchJson() == gesture_start_recipe;
  const bool cancel_model_unchanged = photo_paint_target_identity(gesture_start_layer) ==
      photo_paint_target_identity(cancel_draft_layer);
  if (!cancel_history_unchanged || !cancel_recipe_unchanged || !cancel_model_unchanged) {
    std::cerr << "Dimension draft immutable-state details: history=" << cancel_history_unchanged
              << " recipe=" << cancel_recipe_unchanged << " authored_model=" << cancel_model_unchanged
              << " history_before=" << gesture_start_history
              << " history_draft=" << editor.editHistoryCurrentIndex() << '\n';
    for (auto it = gesture_start_layer.cbegin(); it != gesture_start_layer.cend(); ++it) {
      if (it.value() != cancel_draft_layer.value(it.key()))
        std::cerr << "  photoLayers changed presentation/model key: "
                  << it.key().toStdString() << '\n';
    }
  }
  require(cancel_history_unchanged && cancel_recipe_unchanged && cancel_model_unchanged,
          "A live canvas draft must change provider pixels only, leaving recipe, model and history untouched.");
  require(editor.finishTextDimensionGesture(0, layer_id, false, document_token, revision),
          "Cancel the first dimensional text draft.");
  settle(editor);
  restored_preview(provider, gesture_start_preview);
  require(editor.editHistoryCurrentIndex() == gesture_start_history &&
              editor.currentAssetBoundAdjustmentPatchJson() == gesture_start_recipe &&
              !same_pixels(cancelled_draft_preview, gesture_start_preview),
          "Cancel must restore exact pixels without adding a recipe or history change.");

  require(editor.beginTextDimensionGesture(0, layer_id, document_token, revision),
          "Begin the committed dimensional text rotation gesture.");
  const QVariantMap commit_patch{{QStringLiteral("rotationX"), 28.0},
                                 {QStringLiteral("rotationY"), -26.0}};
  require(editor.previewTextDimensionGesture(0, layer_id, commit_patch, document_token, revision),
          "Preview the committed dimensional text rotation.");
  const auto committed_draft_preview = changed_preview(provider, gesture_start_preview);
  const bool commit_history_unchanged = editor.editHistoryCurrentIndex() == gesture_start_history;
  const bool commit_recipe_unchanged =
      editor.currentAssetBoundAdjustmentPatchJson() == gesture_start_recipe;
  require(commit_history_unchanged && commit_recipe_unchanged,
          "The committed gesture must remain a preview until the pointer release.");
  require(editor.finishTextDimensionGesture(0, layer_id, true, document_token, revision),
          "Release the dimensional text gesture as one authored edit.");
  settle(editor);
  const int committed_history = gesture_start_history + 1;
  const auto committed_layer = text_layer(editor, 0);
  const auto committed_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto committed_preview = preview(provider);
  require(editor.editHistoryCurrentIndex() == committed_history &&
              committed_layer.value(QStringLiteral("textDimension")).toMap()
                  .value(QStringLiteral("rotationX")).toDouble() == 28.0 &&
              committed_recipe != gesture_start_recipe &&
              same_pixels(committed_preview, committed_draft_preview),
          "Pointer release must commit the preview pixels and one canonical dimensional recipe step.");
  same_text_model(initial_layer, committed_layer);

  restore_history(editor, false);
  require(editor.editHistoryCurrentIndex() == gesture_start_history &&
              photo_paint_target_identity(text_layer(editor, 0)) ==
                  photo_paint_target_identity(gesture_start_layer) &&
              editor.currentAssetBoundAdjustmentPatchJson() == gesture_start_recipe,
          "Undo must restore the exact editable text model and dimensional recipe.");
  restored_preview(provider, gesture_start_preview);
  const auto stale_geometry = editor.photoTextDimensionCanvasGeometry(0);
  const auto stale_revision = stale_geometry.value(QStringLiteral("revision")).toString() + QStringLiteral("-stale");
  require(!editor.beginTextDimensionGesture(0, layer_id, document_token, stale_revision) &&
              editor.editHistoryCurrentIndex() == gesture_start_history &&
              editor.currentAssetBoundAdjustmentPatchJson() == gesture_start_recipe,
          "A stale canvas revision must reject without consuming the accepted redo state.");
  restore_history(editor, true);
  require(editor.editHistoryCurrentIndex() == committed_history &&
              photo_paint_target_identity(text_layer(editor, 0)) ==
                  photo_paint_target_identity(committed_layer) &&
              editor.currentAssetBoundAdjustmentPatchJson() == committed_recipe,
          "Redo after stale-target rejection must restore the accepted 3D text edit.");
  restored_preview(provider, committed_preview);

  // Directly exercise the normalized corner-pin helper at the authored
  // 280x120 layer frame, then persist the same quad through the controller.
  PhotoTextDimensionSettings pinned = settings;
  pinned.enabled = false;
  pinned.depth = 0.0;
  pinned.corner_pin_enabled = true;
  pinned.corner_pins = {QPointF(0.10, 0.15), QPointF(0.92, 0.04),
                        QPointF(0.98, 0.89), QPointF(0.08, 0.96)};
  bool quad_valid = false;
  const QSizeF box(280.0, 120.0);
  const auto quad = photo_text_dimension_corner_transform(pinned, box, &quad_valid);
  require(quad_valid, "The normalized perspective pins must form a valid convex quad.");
  const std::array<QPointF, 4> corners{QPointF(0, 0), QPointF(box.width(), 0),
                                      QPointF(box.width(), box.height()), QPointF(0, box.height())};
  for (std::size_t i = 0; i < corners.size(); ++i) {
    const auto mapped = quad.map(corners[i]);
    require(std::abs(mapped.x() - pinned.corner_pins[i].x() * box.width()) < 1.0e-4 &&
                std::abs(mapped.y() - pinned.corner_pins[i].y() * box.height()) < 1.0e-4,
            "Corner-pin coordinates must map normalized pins to the authored text rectangle.");
  }
  const auto pinned_map = encode_photo_text_dimension_variant_map(pinned);
  require(editor.setPhotoLayerTextDimension(0, pinned_map),
          "Persist direct corner pins through the production controller.");
  settle(editor);
  const auto pinned_layer = text_layer(editor, 0);
  const auto pinned_geometry = editor.photoTextDimensionCanvasGeometry(0);
  require(pinned_geometry.value(QStringLiteral("valid")).toBool() &&
              pinned_geometry.value(QStringLiteral("handles")).toMap()
                  .value(QStringLiteral("corners")).toMap().size() == 4 &&
              !same_pixels(preview(provider), committed_preview),
          "Corner pins must update actual canvas geometry and rendered output.");
  const auto displayed_corner = pinned_geometry.value(QStringLiteral("handles")).toMap()
                                    .value(QStringLiteral("corners")).toMap()
                                    .value(QStringLiteral("topLeft")).toMap();
  const double view_x = displayed_corner.value(QStringLiteral("x")).toDouble() * 1000.0;
  const double view_y = displayed_corner.value(QStringLiteral("y")).toDouble() * 500.0;
  const QVariantMap mapping_context{{QStringLiteral("overlayImageX"), 0.0},
                                   {QStringLiteral("overlayImageY"), 0.0},
                                   {QStringLiteral("overlayImageWidth"), 1000.0},
                                   {QStringLiteral("overlayImageHeight"), 500.0},
                                   {QStringLiteral("pressViewX"), view_x},
                                   {QStringLiteral("pressViewY"), view_y}};
  const auto mapped_patch = editor.textDimensionPatchAt(text_layer_index(editor, layer_id),
      QStringLiteral("corner-topLeft"), view_x, view_y, editor.photoLayerTextDimension(
          text_layer_index(editor, layer_id)), mapping_context);
  const auto mapped_pins = mapped_patch.value(QStringLiteral("cornerPins")).toList();
  require(mapped_pins.size() == 4 &&
              std::abs(mapped_pins.front().toMap().value(QStringLiteral("x")).toDouble() - 0.10) < 0.01 &&
              std::abs(mapped_pins.front().toMap().value(QStringLiteral("y")).toDouble() - 0.15) < 0.01,
          "A pointer at a normalized 0.10, 0.15 corner in the 1000x500 view must remain at 0.10, 0.15 in layer coordinates.");
  same_text_model(initial_layer, pinned_layer);

  // Reopen the canonical recipe and compare the ordinary flattened PNG with
  // the production CPU composite for the same editable model.
  const QString canonical_patch = editor.currentAssetBoundAdjustmentPatchJson();
  const auto canonical_state = EditorPreviewControllerTestAccess::state(editor);
  EditorPreviewImageProvider reopened_provider;
  EditorPreviewController reopened;
  reopened.setImageProvider(&reopened_provider);
  reopened.loadAsset(editor.assetId(), source_path, {}, {}, {}, {}, canonical_patch, true);
  settle(reopened);
  require(EditorPreviewControllerTestAccess::state(reopened).layers == canonical_state.layers &&
              same_pixels(preview(reopened_provider), preview(provider)),
          "Canonical recipe round-trip must preserve editable dimensional text and preview pixels.");
  // Match the export producer's 16-bit format and selected output profile while
  // keeping the reference on the independent CPU renderer path.
  const auto expected_export_result = render_output_image_with_backend(
      source, canonical_state,
      preview_output_color_space_for_name(canonical_state.output_color_space),
      QImage::Format_RGBA64, RenderBackendPreference::CpuOnly);
  require(!expected_export_result.isNull(),
          "The independent 16-bit CPU export reference must render.");
  const auto expected_export = expected_export_result;
  const auto export_path = temporary.filePath(QStringLiteral("dimension-output.png"));
  require(editor.startFlattenedPhotoExport(export_path), "Start an ordinary flattened PNG export.");
  settle(editor);
  const QImage exported(export_path);
  const bool export_matches = editor.photoExportError().isEmpty() &&
      !exported.isNull() && same_export_pixels(exported, expected_export);
  if (!export_matches) {
    std::cerr << "Ordinary PNG export comparison failed: "
              << export_pixel_difference_report(exported, expected_export).toStdString()
              << "; export error=" << editor.photoExportError().toStdString() << '\n';
  }
  require(export_matches,
          "Ordinary PNG export must match the canonical 16-bit CPU composite pixels.");

  const auto pinned_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto pinned_preview = preview(provider);
  PhotoTextDimensionSettings cropped_3d = settings;
  cropped_3d.enabled = true;
  cropped_3d.corner_pin_enabled = false;
  require(editor.setPhotoLayerTextDimension(0,
              encode_photo_text_dimension_variant_map(cropped_3d)),
          "Restore active 3D geometry before testing crop output.");
  settle(editor);
  editor.setCropPreset(QStringLiteral("1:1"));
  settle(editor);
  const auto cropped_layer = text_layer(editor, 0);
  const auto cropped_state = EditorPreviewControllerTestAccess::state(editor);
  const auto source_crop_rect = crop_rect_for_image(source.size(), cropped_state);
  const auto cropped_output = render_flattened_output_file(
      editor.assetId(), source_path, cropped_state,
      preview_output_color_space_for_name(cropped_state.output_color_space),
      QImage::Format_RGBA64);
  require(!cropped_output.isNull(), "The cropped 3D file render must produce pixels.");
  const int crop_width = source_crop_rect.width();
  const int crop_height = source_crop_rect.height();
  const QSize cropped_raster(
      qMax(1, qRound(cropped_layer.value(QStringLiteral("pixelWidth")).toDouble() * crop_width /
                     cropped_layer.value(QStringLiteral("canvasWidth"), crop_width).toDouble())),
      qMax(1, qRound(cropped_layer.value(QStringLiteral("pixelHeight")).toDouble() * crop_height /
                     cropped_layer.value(QStringLiteral("canvasHeight"), crop_height).toDouble())));
  const auto cropped_frame = render_photo_text_dimension(cropped_layer, cropped_raster);
  const auto cropped_geometry = editor.photoTextDimensionCanvasGeometry(0);
  const auto source_crop = source.copy(source_crop_rect);
  const bool output_is_square = cropped_output.width() == cropped_output.height();
  const bool output_has_crop_size = cropped_output.size() == QSize(crop_width, crop_height);
  const bool crop_dimension_is_active = photo_text_dimension_is_active(cropped_layer);
  const bool crop_mask_is_preserved =
      cropped_layer.value(QStringLiteral("vectorMask")) ==
      pinned_layer.value(QStringLiteral("vectorMask"));
  const bool crop_geometry_is_valid = cropped_geometry.value(QStringLiteral("valid")).toBool();
  const bool crop_frame_rendered = cropped_frame.error.isEmpty() &&
      !cropped_frame.image.isNull();
  const bool crop_geometry_is_padded =
      cropped_frame.image.width() > cropped_raster.width() ||
      cropped_frame.image.height() > cropped_raster.height();
  const bool crop_contains_visible_edits = !same_pixels(cropped_output, source_crop);
  if (!(output_is_square && output_has_crop_size && crop_dimension_is_active &&
        crop_mask_is_preserved && crop_geometry_is_valid && crop_frame_rendered &&
        crop_geometry_is_padded && crop_contains_visible_edits)) {
    const auto reason = cropped_geometry.value(QStringLiteral("reason")).toString();
    const auto geometry_error = cropped_geometry.value(QStringLiteral("error")).toString();
    const auto cropped_dimension =
        cropped_layer.value(QStringLiteral("textDimension")).toMap();
    std::cerr << QStringLiteral(
        "Cropped 3D coverage diagnostic: output=%1x%2 expected=%3x%4 cropRect=(%5,%6,%7,%8) "
        "square=%9 sizeMatch=%10 active=%11 maskPreserved=%12 geometryValid=%13 reason=%14 error=%15 "
        "frameError=%16 frame=%17x%18 logicalBounds=(%19,%20,%21,%22) raster=%23x%24 padded=%25 "
        "pixelsDifferFromSourceCrop=%26 differingPixelCount=%27 layerPos=(%28,%29) layerSize=(%30,%31) "
        "pixelSize=%32x%33 canvasSize=%34x%35 depth=%36 rotation=(%37,%38,%39)")
        .arg(cropped_output.width()).arg(cropped_output.height())
        .arg(crop_width).arg(crop_height)
        .arg(cropped_state.crop_rect_x).arg(cropped_state.crop_rect_y)
        .arg(cropped_state.crop_rect_width).arg(cropped_state.crop_rect_height)
        .arg(output_is_square).arg(output_has_crop_size).arg(crop_dimension_is_active)
        .arg(crop_mask_is_preserved).arg(crop_geometry_is_valid)
        .arg(reason).arg(geometry_error).arg(cropped_frame.error)
        .arg(cropped_frame.image.width()).arg(cropped_frame.image.height())
        .arg(cropped_frame.logical_bounds.x()).arg(cropped_frame.logical_bounds.y())
        .arg(cropped_frame.logical_bounds.width()).arg(cropped_frame.logical_bounds.height())
        .arg(cropped_raster.width()).arg(cropped_raster.height())
        .arg(crop_geometry_is_padded).arg(crop_contains_visible_edits)
        .arg(differing_pixel_count(cropped_output, source_crop))
        .arg(cropped_layer.value(QStringLiteral("x")).toDouble())
        .arg(cropped_layer.value(QStringLiteral("y")).toDouble())
        .arg(cropped_layer.value(QStringLiteral("width")).toDouble())
        .arg(cropped_layer.value(QStringLiteral("height")).toDouble())
        .arg(cropped_layer.value(QStringLiteral("pixelWidth")).toDouble())
        .arg(cropped_layer.value(QStringLiteral("pixelHeight")).toDouble())
        .arg(cropped_layer.value(QStringLiteral("canvasWidth"), crop_width).toDouble())
        .arg(cropped_layer.value(QStringLiteral("canvasHeight"), crop_height).toDouble())
        .arg(cropped_dimension.value(QStringLiteral("depth")).toDouble())
        .arg(cropped_dimension.value(QStringLiteral("rotationX")).toDouble())
        .arg(cropped_dimension.value(QStringLiteral("rotationY")).toDouble())
        .arg(cropped_dimension.value(QStringLiteral("rotationZ")).toDouble())
        .toStdString() << '\n';
  }
  require(output_is_square && output_has_crop_size && crop_dimension_is_active &&
              crop_mask_is_preserved && crop_geometry_is_valid && crop_frame_rendered &&
              crop_geometry_is_padded && crop_contains_visible_edits,
          "A real extruded text layer must retain padded geometry and visible pixels in the cropped output.");
  editor.resetCropRect();
  settle(editor);
  require(editor.setPhotoLayerTextDimension(0,
              encode_photo_text_dimension_variant_map(pinned)),
          "Restore the original corner-pinned state after crop coverage.");
  settle(editor);
  require(editor.currentAssetBoundAdjustmentPatchJson() == pinned_recipe,
          "Resetting the temporary crop and 3D state must restore the exact prior recipe.");
  restored_preview(provider, pinned_preview);

  verify_component_dimension_apply_cancel_reopen(source_path);

  // A locked ancestor rejects the edit while retaining the accepted text and
  // vector-mask model. A stale target cannot bypass this inherited lock.
  require(editor.addFillLayer(QStringLiteral("#64758a"), QStringLiteral("Group lock peer")),
          "Add a peer layer for inherited-lock coverage.");
  settle(editor);
  const auto after_fill = editor.photoLayers();
  const QString fill_id = after_fill.first().toMap().value(QStringLiteral("id")).toString();
  int text_index = text_layer_index(editor, layer_id);
  int fill_index = text_layer_index(editor, fill_id);
  require(text_index >= 0 && fill_index >= 0 &&
              editor.groupPhotoLayers(QVariantList{text_index, fill_index}, QStringLiteral("Locked text parent")),
          "Group dimensional text and its peer for inherited-lock coverage.");
  settle(editor);
  text_index = text_layer_index(editor, layer_id);
  const auto grouped_text = text_layer(editor, text_index);
  const QString parent_id = grouped_text.value(QStringLiteral("parentId")).toString();
  const int parent_index = text_layer_index(editor, parent_id);
  require(!parent_id.isEmpty() && parent_index >= 0 && editor.setPhotoLayerLocked(parent_index, true),
          "Lock the text layer's parent group.");
  settle(editor);
  const int locked_history = editor.editHistoryCurrentIndex();
  const auto locked_dimension = editor.photoLayerTextDimension(text_index);
  const auto locked_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto locked_text = text_layer(editor, text_index);
  require(!editor.setPhotoLayerTextDimension(text_index,
              {{QStringLiteral("rotationZ"), 91.0}}) &&
              editor.editHistoryCurrentIndex() == locked_history &&
              editor.photoLayerTextDimension(text_index) == locked_dimension &&
              editor.currentAssetBoundAdjustmentPatchJson() == locked_recipe &&
              text_layer(editor, text_index).value(QStringLiteral("vectorMask")) ==
                  locked_text.value(QStringLiteral("vectorMask")) &&
              !editor.photoTextDimensionCanvasGeometry(text_index)
                   .value(QStringLiteral("editable")).toBool(),
          "An inherited lock must reject dimension edits and preserve the accepted recipe, text and mask.");
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));
  QGuiApplication application(argc, argv);
  struct DrainWorkers final {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(90000); }
  } drain_workers;
  try {
    run_workflow();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
