#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "photo_render_utils.h"
#include "recipe_mask_sampling.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlError>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVector>

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace photo_editor::desktop;

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void events(int milliseconds = 25) {
  QElapsedTimer timer;
  timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  } while (timer.elapsed() < milliseconds);
}

void settle(EditorPreviewController& editor) {
  QElapsedTimer timer;
  timer.start();
  while ((editor.processing() || editor.fullRenderRunning() || editor.photoPaintBusy() ||
          editor.selectionWorkBusy()) && timer.elapsed() < 30000) {
    events(5);
  }
  require(!editor.processing() && !editor.fullRenderRunning() && !editor.photoPaintBusy() &&
              !editor.selectionWorkBusy(),
          "Color Range authoring and preview work must settle.");
}

EditRecipeState recipe(const EditorPreviewController& editor) {
  const auto parsed = parse_adjustment_patch_json(editor.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok(), "The UI fixture recipe must parse.");
  return *parsed.state;
}

void pointer(QQuickWindow& window, QEvent::Type type, QPointF point) {
  const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
  const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
  QMouseEvent event(type, point, window.mapToGlobal(point.toPoint()), button, buttons, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &event);
  events(12);
}

QQuickItem* item(QObject& object, const char* name) {
  auto* result = object.findChild<QQuickItem*>(QString::fromLatin1(name));
  const auto find_visual_child = [&](const auto& self, QQuickItem* parent) -> QQuickItem* {
    if (!parent) return nullptr;
    if (parent->objectName() == QString::fromLatin1(name)) return parent;
    for (auto* child : parent->childItems())
      if (auto* found = self(self, child)) return found;
    return nullptr;
  };
  if (!result) result = find_visual_child(find_visual_child, qobject_cast<QQuickItem*>(&object));
  if (!result) {
    if (auto* root_item = qobject_cast<QQuickItem*>(&object)) {
      const auto dump_named_visual_children = [&](const auto& self, QQuickItem* parent) -> void {
        if (!parent) return;
        if (parent->objectName().startsWith(QStringLiteral("selectionRange"))) {
          std::cerr << "  visual child name=" << parent->objectName().toStdString()
                    << " visible=" << parent->isVisible() << " enabled=" << parent->isEnabled()
                    << " size=" << parent->width() << 'x' << parent->height()
                    << " parent=" << (parent->parentItem() ? parent->parentItem()->objectName().toStdString() : "")
                    << '\n';
        }
        for (auto* child : parent->childItems()) self(self, child);
      };
      dump_named_visual_children(dump_named_visual_children, root_item);
    }
    throw std::runtime_error(std::string("Find Selection Range UI item by object name: ") + name);
  }
  return result;
}

QPointF center(QQuickItem* target) {
  return target->mapToScene(QPointF(target->width() / 2.0, target->height() / 2.0));
}

void click(QQuickWindow& window, QQuickItem* target) {
  require(target->isVisible() && target->isEnabled() && target->width() > 0 && target->height() > 0,
          "The clicked UI control must be visibly sized and enabled.");
  const auto point = center(target);
  pointer(window, QEvent::MouseButtonPress, point);
  pointer(window, QEvent::MouseButtonRelease, point);
}

void click_canvas(QQuickWindow& window, QQuickItem* canvas, QPointF local_point) {
  require(canvas->isVisible(), "The source image canvas must be visible for sampling.");
  const auto point = canvas->mapToScene(local_point);
  pointer(window, QEvent::MouseButtonPress, point);
  pointer(window, QEvent::MouseButtonRelease, point);
}

struct Coverage final {
  int value = 0;
  QSize size;
};

Coverage coverage_at(const QString& path, const EditRecipeState& state,
                     const QVariantMap& mask, int source_x, int source_y) {
  QImage decoded;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  require(decode_source_image(path, &decoded, 0, state, &metadata),
          "Decode the exact source used by the range controls.");
  const auto sampling = prepare_recipe_mask_sampling(
      decoded, state, metadata, mask, crop_rect_for_image(decoded.size(), state));
  require(sampling.valid, "Prepare renderer-equivalent Color Range sampling.");
  const auto result = sampling.evaluate({sampling.compile_mask(mask)});
  require(result.valid, "Evaluate the committed Color Range samples.");
  const auto x = qBound(0, source_x, result.size.width() - 1);
  const auto y = qBound(0, source_y, result.size.height() - 1);
  const auto index = static_cast<qsizetype>(y) * result.size.width() + x;
  require(index >= 0 && index < result.alpha_mask.size(), "Resolve the expected source sample pixel.");
  return Coverage{static_cast<int>(static_cast<unsigned char>(result.alpha_mask[index])), result.size};
}

QVariantMap active_mask(const EditRecipeState& state) {
  require(!state.mask_stack.isEmpty(), "The UI workflow must retain a mask definition.");
  return state.mask_stack.last().toMap();
}

void drag_tolerance_to_minimum(QQuickWindow& window, QQuickItem* slider) {
  const auto contact = slider->property("agentContactPoint").toPointF();
  const auto track = slider->property("agentSurfaceRect").toRectF();
  require(track.width() > 20 && track.left() >= 0 && track.right() <= slider->width() + 1 &&
              track.top() >= 0 && track.top() <= slider->height() &&
              std::isfinite(track.center().y()),
          "The tolerance slider must expose its actual centerline track within the row.");
  const auto start = slider->mapToScene(contact);
  const auto end = slider->mapToScene(QPointF(track.left() + 3, track.center().y()));
  pointer(window, QEvent::MouseButtonPress, start);
  constexpr int moves = 10;
  for (int index = 1; index <= moves; ++index) {
    const auto t = static_cast<double>(index) / moves;
    pointer(window, QEvent::MouseMove, start + (end - start) * t);
  }
  pointer(window, QEvent::MouseButtonRelease, end);
  events(150);
}

QRectF item_rect_in(QQuickItem* item, QQuickItem* target) {
  return item->mapRectToItem(target, QRectF(0, 0, item->width(), item->height()));
}

void verify_responsive_layout(QQuickWindow& window, QQuickItem* root, QQuickItem* controls, int width) {
  root->setWidth(width);
  window.resize(width, 1300);
  events(120);
  require(std::abs(controls->width() - width) <= 1,
          "The controls panel must track the compact host width.");
  require(controls->height() > 0 && controls->height() < root->height(),
          "The range controls must remain within the visible host height.");

  QVector<QRectF> visible_rows;
  for (auto* child : controls->childItems()) {
    if (!child->isVisible() || child->width() <= 0 || child->height() <= 0) continue;
    const auto rect = item_rect_in(child, controls);
    require(rect.left() >= -1 && rect.right() <= width + 1 && rect.top() >= -1 &&
                rect.bottom() <= controls->height() + 1,
            "Every visible range-control row must stay inside the panel without clipping.");
    for (const auto& previous : visible_rows)
      require(!rect.intersects(previous), "Visible range-control rows must not overlap at compact widths.");
    visible_rows.append(rect);
  }
  for (const auto* name : {"selectionRangeSampleButton", "selectionRangeHueWidth",
                           "selectionRangeMinimum", "selectionRangeMaximum",
                           "selectionRangeSoftness", "selectionRangeRemoveSample0"}) {
    auto* control = item(*root, name);
    require(control->isVisible() && control->isEnabled(),
            "Primary sampling, swatch, and tolerance controls must remain actionable at compact widths.");
    const auto rect = item_rect_in(control, controls);
    require(rect.left() >= -1 && rect.right() <= width + 1 && rect.bottom() <= controls->height() + 1,
            "Primary sampling and tolerance controls must remain within the compact panel.");
  }
  auto* slider = item(*root, "selectionRangeHueWidth");
  for (const auto* suffix : {"Label", "ValueField", "Control"}) {
    const auto object_name = QByteArray("selectionRangeHueWidth") + suffix;
    auto* part = slider->findChild<QQuickItem*>(QString::fromLatin1(object_name));
    require(part && part->isVisible(), "The visible tolerance slider must retain its label, value, and track.");
    const auto rect = item_rect_in(part, slider);
    require(rect.left() >= -1 && rect.right() <= slider->width() + 1 && rect.top() >= -1 &&
                rect.bottom() <= slider->height() + 1,
            "Tolerance slider internals must remain inside their row without clipping.");
  }
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  require(runtime.isValid(), "Create isolated range-control test data.");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));
  QGuiApplication app(argc, argv);

  try {
    QImage source(160, 120, QImage::Format_ARGB32);
    for (int y = 0; y < source.height(); ++y) {
      for (int x = 0; x < source.width(); ++x) {
        if (x < 80) {
          const auto hue = 0.15 * static_cast<double>(x) / 79.0;
          source.setPixelColor(x, y, QColor::fromHsvF(static_cast<float>(hue), 0.92F, 0.84F));
        } else {
          const auto hue = 0.60 + 0.10 * static_cast<double>(x - 80) / 79.0;
          source.setPixelColor(x, y, QColor::fromHsvF(static_cast<float>(hue), 0.92F, 0.84F));
        }
      }
    }
    const auto path = runtime.filePath(QStringLiteral("selection-range-controls.png"));
    require(source.save(path, "PNG"), "Write the disjoint-hue and tolerance UI fixture.");

    EditorPreviewController editor;
    QQmlEngine::setObjectOwnership(&editor, QQmlEngine::CppOwnership);
    editor.loadAsset(QStringLiteral("selection-range-controls-ui"), path, {}, {}, {}, {}, {});
    settle(editor);
    require(editor.addPaintLayer(), "Create an existing authored layer to protect during sampling.");
    editor.addMask(QStringLiteral("color"));
    settle(editor);
    const auto pre_sampling = recipe(editor);
    require(editor.photoLayerCount() == 1 && pre_sampling.layers.size() == 1,
            "The UI fixture starts with exactly one existing layer.");

    const auto qml = QStringLiteral(R"QML(import QtQuick
import "." as C
Item {
    id: root
    required property var previewController
    required property url imageUrl
    property bool sampling: false
    width: 420
    height: 1300
    C.SelectionRangeControls {
        id: controls
        objectName: "selectionRangeControls"
        x: 0
        y: 0
        width: root.width
        height: implicitHeight
        previewController: root.previewController
        sampling: root.sampling
        onSamplingRequested: function(enabled) { root.sampling = enabled }
    }
    Rectangle {
        id: canvas
        objectName: "selectionRangeCanvas"
        x: 8
        y: controls.y + controls.height + 12
        width: 160
        height: 120
        color: "#20242a"
        border.width: 1
        border.color: "#9ba3ad"
        Image {
            anchors.fill: parent
            source: root.imageUrl
            fillMode: Image.Stretch
            cache: false
        }
        MouseArea {
            objectName: "selectionRangeCanvasPointer"
            anchors.fill: parent
            onClicked: function(mouse) {
                if (!root.sampling)
                    return
                if (root.previewController.sampleActiveColorRangeAt(mouse.x / width, mouse.y / height))
                    root.sampling = false
            }
        }
    }
})QML");

    QQmlEngine qml_engine;
    QObject::connect(&qml_engine, &QQmlEngine::warnings, &qml_engine,
                     [](const QList<QQmlError>& warnings) {
                       for (const auto& warning : warnings)
                         std::cerr << "selection range QML warning: "
                                   << warning.toString().toStdString() << '\n';
                     });
    QQmlComponent component(&qml_engine);
    component.setData(qml.toUtf8(), QUrl::fromLocalFile(
        QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/SelectionRangeControlsUiTest.qml")));
    while (component.isLoading()) events(5);
    std::unique_ptr<QObject> object(component.createWithInitialProperties({
        {QStringLiteral("previewController"), QVariant::fromValue<QObject*>(&editor)},
        {QStringLiteral("imageUrl"), QUrl::fromLocalFile(path)}}));
    if (!object) std::cerr << component.errorString().toStdString() << '\n';
    require(object != nullptr, "Load the production SelectionRangeControls component in a pointer host.");
    auto* root = qobject_cast<QQuickItem*>(object.get());
    require(root != nullptr, "Create the real Selection Range controls host item.");
    QQuickWindow window;
    window.setColor(QColor(QStringLiteral("#181818")));
    window.resize(420, 1300);
    root->setParentItem(window.contentItem());
    root->setSize(QSizeF(420, 1300));
    window.show();
    events(180);
    auto* controls = item(*root, "selectionRangeControls");
    auto* canvas = item(*root, "selectionRangeCanvasPointer");
    auto* sample_button = item(*root, "selectionRangeSampleButton");

    click(window, sample_button);
    require(root->property("sampling").toBool() && sample_button->property("text").toString() == QStringLiteral("Cancel sampling"),
            "The production Sample button must arm canvas sampling and visibly become Cancel sampling.");
    click_canvas(window, canvas, QPointF(4.0, 60.0));
    settle(editor);
    require(!root->property("sampling").toBool() && editor.activeMaskColorSamples().size() == 1,
            "A real pointer click on the armed image canvas must create the first Color Range swatch.");

    click(window, sample_button);
    require(root->property("sampling").toBool(), "The production Add color action must arm a second sample.");
    click_canvas(window, canvas, QPointF(120.0, 60.0));
    settle(editor);
    auto sampled_state = recipe(editor);
    auto sampled_mask = active_mask(sampled_state);
    require(editor.activeMaskColorSamples().size() == 2 &&
                sampled_mask.value(QStringLiteral("colorSampleMode")).toString() == QStringLiteral("union") &&
                sampled_mask.value(QStringLiteral("colorSamples")).toList().size() == 2,
            "The two production pointer picks must remain distinct additive color swatches.");
    require(sampled_state.layers == pre_sampling.layers && editor.photoLayerCount() == 1,
            "Sampling must preserve every existing layer without creating or replacing layer content.");
    events(100);
    const auto screenshot_path = QDir::temp().filePath(QStringLiteral("looksy-selection-range-controls-ui.png"));
    const auto screenshot = window.grabWindow();
    if (!screenshot.isNull() && screenshot.save(screenshot_path))
      std::cerr << "selection range UI screenshot: " << screenshot_path.toStdString() << '\n';
    auto* first_remove_sample = item(*root, "selectionRangeRemoveSample0");
    auto* second_remove_sample = item(*root, "selectionRangeRemoveSample1");
    for (const auto* swatch_button : {first_remove_sample, second_remove_sample}) {
      const auto parent = swatch_button->parentItem();
      std::cerr << "selection range swatch control: object="
                << swatch_button->objectName().toStdString()
                << " visible=" << swatch_button->isVisible() << " enabled=" << swatch_button->isEnabled()
                << " size=" << swatch_button->width() << 'x' << swatch_button->height()
                << " position=" << swatch_button->x() << ',' << swatch_button->y()
                << " parent=" << (parent ? parent->objectName().toStdString() : "")
                << " parentSize=" << (parent ? parent->width() : 0) << 'x'
                << (parent ? parent->height() : 0) << '\n';
    }
    require(first_remove_sample->isVisible() && second_remove_sample->isVisible() &&
                first_remove_sample->width() > 0 && second_remove_sample->width() > 0 &&
                first_remove_sample->parentItem() && second_remove_sample->parentItem() &&
                item_rect_in(first_remove_sample, controls).right() <
                    item_rect_in(second_remove_sample, controls).left(),
            "Both sampled hues must have separate, visible remove-swatch controls in the Flow.");
    auto red_coverage = coverage_at(path, sampled_state, sampled_mask, 4, 60);
    auto blue_coverage = coverage_at(path, sampled_state, sampled_mask, 120, 60);
    require(red_coverage.value >= 250 && blue_coverage.value >= 250,
            "Both disjoint sampled hues must remain selected by the live two-swatch union.");

    auto* remove_sample = first_remove_sample;
    require(remove_sample->isVisible() && remove_sample->isEnabled() &&
                remove_sample->width() > 0 && remove_sample->height() > 0 &&
                remove_sample->parentItem() && remove_sample->parentItem()->isVisible() &&
                remove_sample->parentItem()->width() > 0 && remove_sample->parentItem()->height() > 0,
            "The remove-swatch delegate must be a real, visible, sized item in its Flow row.");
    click(window, remove_sample);
    settle(editor);
    auto removed_state = recipe(editor);
    auto removed_mask = active_mask(removed_state);
    require(editor.activeMaskColorSamples().size() == 1 &&
                removed_mask.value(QStringLiteral("colorSamples")).toList().size() == 1 &&
                removed_mask.value(QStringLiteral("colorSamples")).toList().first().toMap().value(QStringLiteral("hex")) ==
                    sampled_mask.value(QStringLiteral("colorSamples")).toList().last().toMap().value(QStringLiteral("hex")),
            "Removing one swatch through its actual button must retain exactly the other sample.");
    red_coverage = coverage_at(path, removed_state, removed_mask, 4, 60);
    blue_coverage = coverage_at(path, removed_state, removed_mask, 120, 60);
    require(red_coverage.value <= 2 && blue_coverage.value >= 250,
            "Removing a swatch must recompute coverage only from the remaining sampled hue.");
    require(removed_state.layers == pre_sampling.layers && editor.photoLayerCount() == 1,
            "Removing a color swatch must not alter existing layer identity or count.");

    const auto before_cancel = editor.currentAssetBoundAdjustmentPatchJson();
    const auto cancel_history = editor.editHistoryCurrentIndex();
    click(window, sample_button);
    require(root->property("sampling").toBool() && sample_button->property("text").toString() == QStringLiteral("Cancel sampling"),
            "Sampling mode must expose the actual cancel action.");
    click(window, sample_button);
    settle(editor);
    require(!root->property("sampling").toBool() && editor.activeMaskColorSamples().size() == 1 &&
                editor.currentAssetBoundAdjustmentPatchJson() == before_cancel &&
                editor.editHistoryCurrentIndex() == cancel_history,
            "Clicking Cancel sampling must end the armed workflow without changing recipe or history.");

    const auto before_slider = recipe(editor);
    const auto before_slider_json = editor.currentAssetBoundAdjustmentPatchJson();
    const auto before_slider_history = editor.editHistoryCurrentIndex();
    const auto before_hue_range = editor.activeMaskHueRange();
    const auto narrow_coverage_before = coverage_at(path, before_slider, active_mask(before_slider), 159, 60);
    auto* tolerance_slider = item(*root, "selectionRangeHueWidth");
    const auto slider_track_before = tolerance_slider->property("agentSurfaceRect").toRectF();
    const auto slider_contact_before = tolerance_slider->property("agentContactPoint").toPointF();
    drag_tolerance_to_minimum(window, tolerance_slider);
    settle(editor);
    const auto after_slider = recipe(editor);
    const auto after_slider_json = editor.currentAssetBoundAdjustmentPatchJson();
    const auto after_hue_range = editor.activeMaskHueRange();
    const auto narrow_coverage_after = coverage_at(path, after_slider, active_mask(after_slider), 159, 60);
    if (!(after_hue_range < before_hue_range - 0.04 && after_hue_range <= 0.03 &&
          narrow_coverage_before.value >= 240 &&
          narrow_coverage_after.value + 100 < narrow_coverage_before.value)) {
      std::cerr << "selection range slider diagnostic: visible=" << tolerance_slider->isVisible()
                << " enabled=" << tolerance_slider->isEnabled()
                << " size=" << tolerance_slider->width() << 'x' << tolerance_slider->height()
                << " track=" << slider_track_before.x() << ',' << slider_track_before.y() << ','
                << slider_track_before.width() << ',' << slider_track_before.height()
                << " contact=" << slider_contact_before.x() << ',' << slider_contact_before.y()
                << " before=" << before_hue_range << " after=" << after_hue_range
                << " controlValue=" << tolerance_slider->property("value").toDouble()
                << " coverageBefore=" << narrow_coverage_before.value
                << " coverageAfter=" << narrow_coverage_after.value
                << " historyBefore=" << before_slider_history
                << " historyAfter=" << editor.editHistoryCurrentIndex()
                << " qmlSamples=" << controls->property("samples").toList().size()
                << '\n';
    }
    require(after_hue_range < before_hue_range - 0.04 && after_hue_range <= 0.03 &&
                narrow_coverage_before.value >= 240 &&
                narrow_coverage_after.value + 100 < narrow_coverage_before.value,
            "One actual tolerance-slider drag must narrow the sampled hue and change rendered-equivalent coverage.");
    require(editor.editHistoryCurrentIndex() == before_slider_history + 1 &&
                after_slider.layers == before_slider.layers,
            "A tolerance drag must create exactly one coalesced edit and leave layers untouched.");
    editor.undo();
    settle(editor);
    require(editor.currentAssetBoundAdjustmentPatchJson() == before_slider_json,
            "Undo must restore the exact Color Range recipe before the tolerance gesture.");
    editor.redo();
    settle(editor);
    require(editor.currentAssetBoundAdjustmentPatchJson() == after_slider_json,
            "Redo must restore the exact narrowed Color Range recipe.");

    EditorPreviewController reopened;
    reopened.loadAsset(QStringLiteral("selection-range-controls-reopened"), path, {}, {}, {}, {},
                       serialize_edit_recipe_json(after_slider));
    settle(reopened);
    const auto reopened_masks = reopened.maskStack();
    int reopened_color_index = -1;
    for (int index = 0; index < reopened_masks.size(); ++index) {
      if (reopened_masks.at(index).toMap().value(QStringLiteral("kind")).toString() ==
          QStringLiteral("color")) {
        reopened_color_index = index;
        break;
      }
    }
    if (reopened_color_index >= 0) reopened.setActiveMaskIndex(reopened_color_index);
    settle(reopened);
    require(std::abs(reopened.activeMaskHueRange() - after_hue_range) < 1e-6 &&
                reopened.activeMaskKind() == QStringLiteral("color") &&
                reopened.activeMaskIndex() == reopened_color_index &&
                reopened.activeMaskColorSamples().size() == 1 &&
                reopened.photoLayerCount() == 1,
            "Reopening the serialized recipe must retain the slider value, remaining swatch, and original layer.");

    verify_responsive_layout(window, root, controls, 420);
    verify_responsive_layout(window, root, controls, 320);
    verify_responsive_layout(window, root, controls, 420);

    std::cout << "Selection Range controls pointer, recipe, history, and compact-layout workflow passed.\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
