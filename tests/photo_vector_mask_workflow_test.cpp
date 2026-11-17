#include "edit_recipe_state.h"
#include "edit_recipe_v13_adapter.h"
#include "edit_recipe_v13_persistence.h"
#include "editor_preview_controller.h"
#include "photo_render_utils.h"
#include "photo_vector_mask.h"
#include "photo_vector_path.h"
#include "recipe_asset_store.h"

#include <QColorSpace>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static EditState state(const EditorPreviewController& controller) {
    return controller.currentState();
  }
};
}  // namespace photo_editor::desktop

using namespace photo_editor::desktop;
namespace paths = photo_editor::desktop::vector_paths;
namespace masks = photo_editor::desktop::vector_masks;

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void settle(EditorPreviewController& controller) {
  QElapsedTimer timer;
  timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  } while ((controller.processing() || controller.fullRenderRunning() ||
            controller.professionalToolsBusy() || controller.photoExportBusy()) &&
           timer.elapsed() < 30000);
  require(!controller.processing() && !controller.fullRenderRunning() &&
              !controller.professionalToolsBusy() && !controller.photoExportBusy(),
          "Controller work did not settle.");
}

EditState state(const EditorPreviewController& controller) {
  return EditorPreviewControllerTestAccess::state(controller);
}

int layer_index(const EditState& edit, const QString& id) {
  for (int index = 0; index < edit.layers.size(); ++index)
    if (edit.layers.at(index).toMap().value(QStringLiteral("id")).toString() == id)
      return index;
  return -1;
}

int layer_index(const EditorPreviewController& controller, const QString& id) {
  return layer_index(state(controller), id);
}

QVariantMap layer_by_id(const EditState& edit, const QString& id) {
  const auto index = layer_index(edit, id);
  return index < 0 ? QVariantMap{} : edit.layers.at(index).toMap();
}

bool vector_mask_layer_recipes_equal(const EditState& left, const EditState& right) {
  if (left.layers.size() != right.layers.size()) return false;
  for (qsizetype index = 0; index < left.layers.size(); ++index) {
    const auto left_layer = left.layers.at(index).toMap();
    const auto right_layer = right.layers.at(index).toMap();
    if (left_layer.value(QStringLiteral("id")) !=
            right_layer.value(QStringLiteral("id")) ||
        left_layer.value(QStringLiteral("vectorMask")) !=
            right_layer.value(QStringLiteral("vectorMask"))) {
      return false;
    }
    for (const auto* key : {"selectionScoped", "selectionMaskSetId",
                            "selectionMaskSetIds", "selectionMaskSetName",
                            "selectionMaskSetNames", "maskEnabled"}) {
      if (left_layer.value(QLatin1String(key)) !=
          right_layer.value(QLatin1String(key))) {
        return false;
      }
    }
  }
  return true;
}

QImage render(const QImage& source, const EditState& edit) {
  const auto crop = crop_rect_for_image(source.size(), edit);
  const auto cropped_source = source.copy(crop);
  const RenderMaskOptions mask_options{
      .source_geometry = DesktopSourceGeometry{source.size(), crop},
      .full_source = source};
  const auto result = render_output_image_with_backend(
      cropped_source, edit, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly, nullptr, nullptr, mask_options);
  require(!result.isNull(), "CPU renderer returned a null vector-mask image.");
  return result;
}

QImage patterned_image(const QSize& size, const QColor& base) {
  QImage image(size, QImage::Format_RGB32);
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < size.height(); ++y) {
    for (int x = 0; x < size.width(); ++x) {
      image.setPixelColor(x, y, QColor(
          std::clamp(base.red() + (x * 3 + y) % 19, 0, 255),
          std::clamp(base.green() + (x + y * 2) % 17, 0, 255),
          std::clamp(base.blue() + (x * 2 + y * 3) % 23, 0, 255)));
    }
  }
  return image;
}

QVariantMap rectangle_path(const QString& id, double left, double top,
                           double right, double bottom) {
  paths::VectorPathDocument document;
  QString error;
  require(paths::add_subpath(&document, id, false, &error),
          "Could not create a vector-mask contour.");
  const QVector<QPointF> points{{left, top}, {right, top},
                                {right, bottom}, {left, bottom}};
  for (qsizetype index = 0; index < points.size(); ++index) {
    paths::PathAnchor anchor;
    anchor.id = QStringLiteral("%1-anchor-%2").arg(id).arg(index);
    anchor.position = points.at(index);
    require(paths::append_anchor(&document, id, anchor, &error),
            "Could not append a vector-mask anchor.");
  }
  require(paths::set_closed(&document, id, true, &error),
          "Could not close a vector-mask contour.");
  return paths::to_variant(document, &error);
}

QVariantMap open_path(const QString& id) {
  paths::VectorPathDocument document;
  QString error;
  require(paths::add_subpath(&document, id, false, &error),
          "Could not create an open vector contour.");
  for (int index = 0; index < 3; ++index) {
    paths::PathAnchor anchor;
    anchor.id = QStringLiteral("%1-anchor-%2").arg(id).arg(index);
    anchor.position = QPointF(0.2 + static_cast<double>(index) * 0.3, 0.4);
    require(paths::append_anchor(&document, id, anchor, &error),
            "Could not append an open contour anchor.");
  }
  return paths::to_variant(document, &error);
}

QVariantMap local_mask_for_crop(QVariantMap serialized_mask,
                                const QSize& source_size,
                                const QRect& crop) {
  masks::PhotoVectorMask mask;
  QString error;
  require(masks::from_variant(serialized_mask, &mask, &error),
          "Could not decode the source-document component mask.");
  require(mask.basis == masks::CoordinateBasis::SourceDocument &&
              source_size.isValid() && crop.isValid() &&
              crop.width() > 0 && crop.height() > 0,
          "Component mask reference needs valid source and crop geometry.");
  const auto source_width = static_cast<double>(source_size.width());
  const auto source_height = static_cast<double>(source_size.height());
  const auto crop_x = static_cast<double>(crop.x());
  const auto crop_y = static_cast<double>(crop.y());
  const auto crop_width = static_cast<double>(crop.width());
  const auto crop_height = static_cast<double>(crop.height());
  const auto scale_x = source_width / crop_width;
  const auto scale_y = source_height / crop_height;
  for (auto& subpath : mask.path.subpaths) {
    for (auto& anchor : subpath.anchors) {
      anchor.position.setX((anchor.position.x() * source_width - crop_x) /
                           crop_width);
      anchor.position.setY((anchor.position.y() * source_height - crop_y) /
                           crop_height);
      anchor.incoming.setX(anchor.incoming.x() * scale_x);
      anchor.incoming.setY(anchor.incoming.y() * scale_y);
      anchor.outgoing.setX(anchor.outgoing.x() * scale_x);
      anchor.outgoing.setY(anchor.outgoing.y() * scale_y);
    }
  }
  mask.basis = masks::CoordinateBasis::LocalLayer;
  const auto result = masks::to_variant(mask, &error);
  require(error.isEmpty(), "Could not convert the component mask to crop-local reference coordinates.");
  return result;
}

bool images_within_channel_delta(const QImage& left, const QImage& right,
                                 int maximum_delta) {
  if (left.size() != right.size() || left.isNull() || right.isNull()) return false;
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width(); ++x) {
      const auto a = left.pixelColor(x, y);
      const auto b = right.pixelColor(x, y);
      if (std::abs(a.red() - b.red()) > maximum_delta ||
          std::abs(a.green() - b.green()) > maximum_delta ||
          std::abs(a.blue() - b.blue()) > maximum_delta ||
          std::abs(a.alpha() - b.alpha()) > maximum_delta) return false;
    }
  }
  return true;
}

QPoint pixel_for_view_point(const QVariantMap& mapped, const QImage& image) {
  require(mapped.value(QStringLiteral("valid")).toBool(),
          "A visible local mask coordinate must map to the cropped canvas.");
  return {qBound(0, qRound(mapped.value(QStringLiteral("x")).toDouble() *
                            (image.width() - 1)), image.width() - 1),
          qBound(0, qRound(mapped.value(QStringLiteral("y")).toDouble() *
                            (image.height() - 1)), image.height() - 1)};
}

std::string layer_cache_identity(const DesktopRenderPlanResult& result) {
  if (!result.ok() || !result.plan.has_value()) return {};
  const auto stage = std::find_if(result.plan->stages.cbegin(),
      result.plan->stages.cend(), [](const auto& value) {
        return value.kind == photo_editor::pipeline::RenderStageKind::Layers;
      });
  return stage == result.plan->stages.cend() ? std::string{} : stage->cache_identity;
}

DesktopRenderPlanRequest plan_request(const QSize& source_size) {
  DesktopRenderPlanRequest request;
  request.recipe_id = QStringLiteral("vector-mask-workflow");
  request.recipe_payload_digest = QStringLiteral("vector-mask-workflow-v1");
  request.source_id = QStringLiteral("vector-mask-source");
  request.decoder_provenance = QStringLiteral("raster:vector-mask-workflow-v1");
  request.source_width = static_cast<std::uint32_t>(source_size.width());
  request.source_height = static_cast<std::uint32_t>(source_size.height());
  return request;
}

void test_raster_intersection_history_and_persistence(
    const QString& base_path, const QImage& base, const QString& overlay_path);
void test_fill_pixel_adjustment_group_and_cache(
    const QString& base_path, const QImage& base);
void test_crop_rotation_and_layer_transfer(
    const QString& base_path, const QImage& base,
    const QString& red_path, const QString& blue_path);

}  // namespace

namespace {

void test_crop_rotation_and_layer_transfer(
    const QString& base_path, const QImage& base,
    const QString& red_path, const QString& blue_path) {
  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("vector-mask-transform-transfer"), base_path,
                   {}, {}, {}, {}, {});
  settle(editor);
  require(editor.addSourceLayer(red_path, QStringLiteral("Mask source")),
          "Could not add transformed mask source.");
  const auto source_id = state(editor).layers.first().toMap()
                             .value(QStringLiteral("id")).toString();
  require(editor.setPhotoLayerVectorMask(layer_index(editor, source_id),
              rectangle_path(QStringLiteral("transfer-mask"), 0.65, 0.35, 0.85, 0.65)),
          "Could not attach a local-layer transfer mask.");
  require(editor.setPhotoLayerTransform(layer_index(editor, source_id), 0, 0, 0.9, 22.0),
          "Could not rotate and scale the local mask layer.");
  editor.updateCropRect(0.05, 0.05, 0.9, 0.9);
  settle(editor);
  const auto rotated_state = state(editor);
  const auto rotated = render(base, rotated_state);
  auto base_only_state = rotated_state;
  base_only_state.layers.clear();
  const auto cropped_base = render(base, base_only_state);
  const auto inside_map = editor.mapPhotoVectorMaskCoordinate(
      layer_index(editor, source_id), 0.75, 0.5, true);
  const auto outside_map = editor.mapPhotoVectorMaskCoordinate(
      layer_index(editor, source_id), 0.35, 0.5, true);
  const auto inside_pixel = pixel_for_view_point(inside_map, rotated);
  const auto outside_pixel = pixel_for_view_point(outside_map, rotated);
  require(rotated.pixelColor(inside_pixel) != cropped_base.pixelColor(inside_pixel) &&
              rotated.pixelColor(outside_pixel) == cropped_base.pixelColor(outside_pixel),
          "A local vector mask must track its rotated layer through crop-aware rendering.");

  const auto basis_before = state(editor);
  const auto basis_pixels = render(base, basis_before);
  const auto basis_mask_id = layer_by_id(basis_before, source_id).value("vectorMask").toMap().value("id");
  require(editor.setPhotoLayerVectorMaskBasis(layer_index(editor, source_id), "source-document"),
          "Could not fix the mask to the source document.");
  settle(editor);
  require(render(base, state(editor)) == basis_pixels &&
              layer_by_id(state(editor), source_id).value("vectorMask").toMap().value("id") == basis_mask_id,
          "Changing mask coordinate basis must preserve its exact pixels and identity.");
  require(editor.setPhotoLayerVectorMaskBasis(layer_index(editor, source_id), "local-layer"),
          "Could not restore the follow-layer mask basis.");
  settle(editor);
  require(render(base, state(editor)) == basis_pixels,
          "Mask basis roundtrip must preserve rendered coverage.");

  require(editor.addSourceLayer(blue_path, QStringLiteral("Mask destination")),
          "Could not add vector-mask transfer destination.");
  const auto target_id = state(editor).layers.first().toMap()
                             .value(QStringLiteral("id")).toString();
  require(editor.setPhotoLayerTransform(layer_index(editor, target_id),
                                       14, 8, 1.15, -11.0),
          "Could not independently transform the transfer destination.");
  settle(editor);
  const auto source_center = editor.mapPhotoVectorMaskCoordinate(
      layer_index(editor, source_id), 0.75, 0.5, true);
  const auto source_definition = layer_by_id(state(editor), source_id)
      .value(QStringLiteral("vectorMask")).toMap();
  require(editor.transferPhotoLayerVectorMask(source_id, target_id, true),
          "Could not copy a local mask between transformed layers.");
  settle(editor);
  const auto copied = layer_by_id(state(editor), target_id)
      .value(QStringLiteral("vectorMask")).toMap();
  masks::PhotoVectorMask copied_mask;
  QString error;
  require(masks::from_variant(copied, &copied_mask, &error) &&
              copied_mask.basis == masks::CoordinateBasis::SourceDocument &&
              copied_mask.id != source_definition.value(QStringLiteral("id")).toString() &&
              layer_by_id(state(editor), source_id).value(QStringLiteral("vectorMask")) ==
                  source_definition,
          "Copy must preserve its source and project target geometry in document coordinates.");
  const auto copied_bounds = paths::to_painter_path(
      copied_mask.path, QSize(1, 1), &error).boundingRect();
  const auto target_center = editor.mapPhotoVectorMaskCoordinate(
      layer_index(editor, target_id), copied_bounds.center().x(),
      copied_bounds.center().y(), true);
  require(source_center.value(QStringLiteral("valid")).toBool() &&
              target_center.value(QStringLiteral("valid")).toBool() &&
              std::abs(source_center.value(QStringLiteral("x")).toDouble() -
                       target_center.value(QStringLiteral("x")).toDouble()) < 1e-5 &&
              std::abs(source_center.value(QStringLiteral("y")).toDouble() -
                       target_center.value(QStringLiteral("y")).toDouble()) < 1e-5,
          "Copied mask coverage must stay at the same source-document location.");

  editor.undo();
  settle(editor);
  require(!layer_by_id(state(editor), target_id).contains(QStringLiteral("vectorMask")) &&
              layer_by_id(state(editor), source_id).value(QStringLiteral("vectorMask")) ==
                  source_definition,
          "One undo must remove a copied mask and retain its source.");
  editor.redo();
  settle(editor);
  require(layer_by_id(state(editor), target_id).value(QStringLiteral("vectorMask")) == copied,
          "Redo must restore the exact transferred mask definition.");

  require(editor.removePhotoLayerVectorMask(layer_index(editor, target_id)),
          "Could not remove the copied mask before testing move semantics.");
  settle(editor);
  require(editor.transferPhotoLayerVectorMask(source_id, target_id, false),
          "Could not move a vector mask to the transformed destination.");
  settle(editor);
  const auto moved = layer_by_id(state(editor), target_id)
      .value(QStringLiteral("vectorMask")).toMap();
  masks::PhotoVectorMask moved_mask;
  require(!layer_by_id(state(editor), source_id).contains(QStringLiteral("vectorMask")) &&
              masks::from_variant(moved, &moved_mask, &error) &&
              moved_mask.basis == masks::CoordinateBasis::SourceDocument,
          "Move must preserve source-document geometry and detach the source layer.");
  const auto moved_bounds = paths::to_painter_path(
      moved_mask.path, QSize(1, 1), &error).boundingRect();
  const auto moved_center = editor.mapPhotoVectorMaskCoordinate(
      layer_index(editor, target_id), moved_bounds.center().x(),
      moved_bounds.center().y(), true);
  require(std::abs(source_center.value(QStringLiteral("x")).toDouble() -
                   moved_center.value(QStringLiteral("x")).toDouble()) < 1e-5 &&
              std::abs(source_center.value(QStringLiteral("y")).toDouble() -
                       moved_center.value(QStringLiteral("y")).toDouble()) < 1e-5,
          "Moved mask coverage must stay in the original source-document location.");

  editor.undo();
  settle(editor);
  require(layer_by_id(state(editor), source_id).value(QStringLiteral("vectorMask")) ==
              source_definition &&
              !layer_by_id(state(editor), target_id).contains(QStringLiteral("vectorMask")),
          "One undo must restore the source mask and clear the moved destination.");

  require(editor.setPhotoLayerTransform(layer_index(editor, source_id),
                                        800, 600, 1.0, 0.0),
          "Could not move the source mask outside its source document.");
  settle(editor);
  const auto before_rejected_transfer = state(editor);
  require(!editor.transferPhotoLayerVectorMask(source_id, target_id, false) &&
              state(editor) == before_rejected_transfer &&
              !layer_by_id(state(editor), target_id).contains(QStringLiteral("vectorMask")) &&
              layer_by_id(state(editor), source_id).contains(QStringLiteral("vectorMask")),
          "An out-of-document transfer must fail without changing either layer.");
}

void test_rotated_parent_crop_alignment(
    const QString& base_path, const QImage& base, const QString& red_path) {
  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("vector-mask-parent-crop"), base_path,
                   {}, {}, {}, {}, {});
  settle(editor);
  require(editor.addSourceLayer(red_path, QStringLiteral("Parent-masked source")),
          "Could not add parent-transform mask source.");
  const auto child_id = state(editor).layers.first().toMap()
                            .value(QStringLiteral("id")).toString();
  require(editor.addLayerGroup(QStringLiteral("Rotated mask parent")),
          "Could not add rotated mask parent group.");
  const auto parent_id = state(editor).layers.first().toMap()
                             .value(QStringLiteral("id")).toString();
  require(editor.movePhotoLayerIntoGroup(layer_index(editor, child_id),
                                         layer_index(editor, parent_id)) &&
              editor.setPhotoLayerTransform(layer_index(editor, parent_id),
                                            0, 0, 1.0, 22.0) &&
              editor.setPhotoLayerVectorMask(layer_index(editor, child_id),
                  rectangle_path(QStringLiteral("parent-local-mask"),
                                 0.65, 0.35, 0.85, 0.65)),
          "Could not attach a local mask below a rotated parent transform.");
  editor.updateCropRect(0.05, 0.05, 0.9, 0.9);
  settle(editor);

  const auto edit = state(editor);
  const auto rendered = render(base, edit);
  auto base_only = edit;
  base_only.layers.clear();
  const auto background = render(base, base_only);
  require(rendered.size() == background.size(),
          "Crop-aware parent fixture must render at the same output dimensions.");
  const auto child_index = layer_index(editor, child_id);
  const auto inside_map = editor.mapPhotoVectorMaskCoordinate(
      child_index, 0.75, 0.5, true);
  const auto outside_map = editor.mapPhotoVectorMaskCoordinate(
      child_index, 0.35, 0.5, true);
  const auto inside = pixel_for_view_point(inside_map, rendered);
  const auto outside = pixel_for_view_point(outside_map, rendered);
  require(rendered.pixelColor(inside) != background.pixelColor(inside) &&
              rendered.pixelColor(outside) == background.pixelColor(outside),
          "Local vector-mask coverage and coordinate mapping must include rotated ancestor groups and off-center crop.");
}

void test_component_source_document_mask_moves_with_instance(
    const QString& base_path, const QImage& base) {
  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("vector-mask-component-source-basis"),
                   base_path, {}, {}, {}, {}, {});
  settle(editor);
  require(editor.addFillLayer(QStringLiteral("#df5635"),
                              QStringLiteral("Masked component fill")),
          "Could not add the component mask fixture.");
  const auto source_layer_id = state(editor).layers.first().toMap()
                                   .value(QStringLiteral("id")).toString();
  require(editor.setPhotoLayerVectorMask(
              layer_index(editor, source_layer_id),
              rectangle_path(QStringLiteral("component-source-mask"),
                             0.24, 0.20, 0.72, 0.78)) &&
              editor.setPhotoLayerVectorMaskBasis(
                  layer_index(editor, source_layer_id),
                  QStringLiteral("source-document")),
          "Could not author a source-document mask on the component child.");
  settle(editor);
  require(editor.convertPhotoLayerToComponent(
              layer_index(editor, source_layer_id)),
          "Could not convert the masked fill into an editable component.");
  settle(editor);

  require(editor.duplicatePhotoComponent(
              layer_index(editor, source_layer_id)),
          "Could not create a second instance for the transform regression.");
  settle(editor);
  QString moved_instance_id;
  for (const auto& value : state(editor).layers) {
    const auto layer = value.toMap();
    if (layer.contains(QStringLiteral("smartObjectDocument")) &&
        layer.value(QStringLiteral("id")).toString() != source_layer_id) {
      moved_instance_id = layer.value(QStringLiteral("id")).toString();
      break;
    }
  }
  require(!moved_instance_id.isEmpty(),
          "The duplicate component instance was not present in the document.");
  editor.updateCropRect(0.05, 0.06, 0.90, 0.86);
  settle(editor);
  require(editor.setPhotoLayerTransform(
              layer_index(editor, moved_instance_id), 14, 8, 0.82, 23.0),
          "Could not move, scale, and rotate the shared component instance.");
  settle(editor);
  const auto component_state = state(editor);
  const auto component_pixels = render(base, component_state);
  auto unmoved_state = component_state;
  for (auto& value : unmoved_state.layers) {
    auto layer = value.toMap();
    if (layer.value(QStringLiteral("id")).toString() != moved_instance_id) continue;
    layer.insert(QStringLiteral("x"), 0);
    layer.insert(QStringLiteral("y"), 0);
    layer.insert(QStringLiteral("scale"), 1.0);
    layer.insert(QStringLiteral("rotation"), 0.0);
    value = layer;
  }
  require(render(base, component_state) != render(base, unmoved_state),
          "Moving the component instance must move its masked content on the cropped canvas.");

  auto ordinary_reference = component_state;
  const auto crop = crop_rect_for_image(base.size(), component_state);
  QVariantList reference_layers;
  for (const auto& value : component_state.layers) {
    auto layer = value.toMap();
    if (!layer.contains(QStringLiteral("smartObjectDocument"))) {
      reference_layers.append(layer);
      continue;
    }
    const auto instance_id = layer.value(QStringLiteral("id")).toString();
    const auto document = layer.value(QStringLiteral("smartObjectDocument")).toMap();
    const auto children = document.value(QStringLiteral("layers")).toList();
    require(children.size() == 1,
            "The mask transform fixture should contain one editable fill child.");
    layer.remove(QStringLiteral("smartObjectDocument"));
    layer.remove(QStringLiteral("smartObjectInstanceId"));
    layer.insert(QStringLiteral("kind"), QStringLiteral("group"));
    reference_layers.append(layer);

    auto child = children.first().toMap();
    child.insert(QStringLiteral("id"), instance_id + QStringLiteral("::reference-fill"));
    child.insert(QStringLiteral("parentId"), instance_id);
    auto vector_mask = child.value(QStringLiteral("vectorMask")).toMap();
    child.insert(QStringLiteral("vectorMask"),
        local_mask_for_crop(vector_mask, base.size(), crop));
    reference_layers.append(child);
  }
  ordinary_reference.layers = reference_layers;
  const auto reference_pixels = render(base, ordinary_reference);
  require(images_within_channel_delta(component_pixels, reference_pixels, 2),
          "A moved and rotated component's source-document mask must match the equivalent ordinary grouped render after crop mapping.");
}

}  // namespace

namespace {

void test_fill_pixel_adjustment_group_and_cache(
    const QString& base_path, const QImage& base) {
  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("vector-mask-layer-kinds"), base_path,
                   {}, {}, {}, {}, {});
  settle(editor);

  require(editor.addFillLayer(QStringLiteral("#d02d48"), QStringLiteral("Masked fill")),
          "Could not add a fill layer.");
  const auto fill_id = state(editor).layers.first().toMap()
                           .value(QStringLiteral("id")).toString();
  const auto fill_baseline = render(base, state(editor));
  require(editor.setPhotoLayerVectorMask(layer_index(editor, fill_id),
      rectangle_path(QStringLiteral("fill-mask"), 0.5, 0.1, 0.9, 0.9)),
      "Could not attach a vector mask to a fill layer.");
  settle(editor);
  const auto fill_masked = render(base, state(editor));
  require(fill_masked != fill_baseline,
          "Vector coverage must change actual fill-layer output.");
  require(editor.setPhotoLayerVectorMaskEnabled(layer_index(editor, fill_id), false),
          "Could not bypass fill-layer vector coverage.");
  settle(editor);
  require(render(base, state(editor)) == fill_baseline,
          "Fill-layer vector-mask bypass must restore its prior output.");
  require(editor.setPhotoLayerVectorMaskEnabled(layer_index(editor, fill_id), true),
          "Could not re-enable fill-layer vector coverage.");
  settle(editor);

  require(editor.addPhotoVectorPath(
              rectangle_path(QStringLiteral("pixel-shape"), 0.05, 0.05, 0.95, 0.95),
              QStringLiteral("#22d080"), QStringLiteral("#22d080"), 0.0),
          "Could not add an editable pixel vector-shape layer.");
  const auto pixel_id = state(editor).layers.first().toMap()
                            .value(QStringLiteral("id")).toString();
  const auto pixel_baseline = render(base, state(editor));
  require(editor.setPhotoLayerVectorMask(layer_index(editor, pixel_id),
      rectangle_path(QStringLiteral("pixel-mask"), 0.05, 0.05, 0.45, 0.95)),
      "Could not attach a vector mask to the pixel layer.");
  settle(editor);
  const auto pixel_masked = render(base, state(editor));
  require(pixel_masked != pixel_baseline,
          "Vector coverage must change actual editable pixel-layer output.");

  const auto fill_index = layer_index(editor, fill_id);
  const auto pixel_index = layer_index(editor, pixel_id);
  require(fill_index >= 0 && pixel_index >= 0 &&
              editor.groupPhotoLayers(QVariantList{fill_index, pixel_index},
                                      QStringLiteral("Masked group")),
          "Could not group masked fill and pixel layers.");
  settle(editor);
  const auto group_id = state(editor).layers.first().toMap()
                            .value(QStringLiteral("id")).toString();
  const auto group_baseline_state = state(editor);
  const auto group_baseline = render(base, group_baseline_state);
  require(editor.setPhotoLayerVectorMask(layer_index(editor, group_id),
      rectangle_path(QStringLiteral("group-mask"), 0.1, 0.1, 0.45, 0.9)),
      "Could not attach a vector mask to the group.");
  settle(editor);
  const auto group_masked_state = state(editor);
  require(render(base, group_masked_state) != group_baseline,
          "Vector coverage must change actual grouped-layer output.");

  require(editor.addAdjustmentLayer(QStringLiteral("Masked exposure")),
          "Could not add an adjustment layer.");
  const auto adjustment_id = state(editor).layers.first().toMap()
                                 .value(QStringLiteral("id")).toString();
  const auto adjustment_index = layer_index(editor, adjustment_id);
  require(editor.setPhotoLayerAdjustmentValue(adjustment_index,
              QStringLiteral("exposure"), 1.0),
          "Could not create visible adjustment-layer output.");
  settle(editor);
  const auto adjustment_baseline_state = state(editor);
  const auto adjustment_baseline = render(base, adjustment_baseline_state);
  require(editor.setPhotoLayerVectorMask(layer_index(editor, adjustment_id),
      rectangle_path(QStringLiteral("adjustment-mask"), 0.5, 0.05, 0.95, 0.95)),
      "Could not attach a vector mask to an adjustment layer.");
  settle(editor);
  const auto adjustment_masked_state = state(editor);
  const auto adjustment_masked = render(base, adjustment_masked_state);
  require(adjustment_masked != adjustment_baseline,
          "Vector coverage must change actual adjustment-layer output.");

  QTemporaryDir canonical_assets_directory;
  require(canonical_assets_directory.isValid(),
          "Could not create canonical vector-mask asset storage.");
  RecipeAssetStore canonical_assets(
      canonical_assets_directory.filePath(QStringLiteral("recipe-assets")));
  const auto canonical = serialize_desktop_recipe_as_canonical_v13_with_assets(
      adjustment_masked_state,
      {.recipe_id = "vector-mask-layer-kinds",
       .source_id = "vector-mask-layer-kinds-source",
       .decoder_provenance = "raster:vector-mask-layer-kinds-v1",
       .raw_source = false},
      canonical_assets);
  if (!canonical.ok()) {
    for (const auto& error : canonical.errors)
      std::cerr << error.path << ": " << error.message << '\n';
  }
  require(canonical.ok(),
          "Canonical v13 serialization must store grouped and adjustment vector masks.");
  const auto canonical_projection =
      project_canonical_v13_document_to_desktop_state_with_assets(
          canonical.document, canonical_assets);
  if (!canonical_projection.ok()) {
    for (const auto& error : canonical_projection.errors)
      std::cerr << error.path << ": " << error.message << '\n';
  }
  const bool canonical_layer_masks_equal = canonical_projection.ok() &&
      vector_mask_layer_recipes_equal(adjustment_masked_state,
                                      *canonical_projection.state);
  const bool canonical_mask_stack_equal = canonical_projection.ok() &&
      canonical_projection.state->mask_stack == adjustment_masked_state.mask_stack;
  const bool canonical_render_equal = canonical_projection.ok() &&
      render(base, *canonical_projection.state) == adjustment_masked;
  if (canonical_projection.ok() &&
      (!canonical_layer_masks_equal || !canonical_mask_stack_equal ||
       !canonical_render_equal)) {
    std::cerr << "canonical layer-kind mask diagnostic: layerMasks="
              << canonical_layer_masks_equal << " maskStack="
              << canonical_mask_stack_equal << " render="
              << canonical_render_equal << '\n';
  }
  require(canonical_projection.ok() && canonical_layer_masks_equal &&
              canonical_mask_stack_equal && canonical_render_equal,
          "Canonical v13 projection must preserve exact grouped and adjustment vector-mask recipes and pixels.");

  const auto request = plan_request(base.size());
  const auto unmasked_group_plan = compile_desktop_render_plan_with_derived_assets(
      group_baseline_state, request);
  const auto masked_group_plan = compile_desktop_render_plan_with_derived_assets(
      group_masked_state, request);
  const auto unmasked_adjustment_plan = compile_desktop_render_plan_with_derived_assets(
      adjustment_baseline_state, request);
  const auto masked_adjustment_plan = compile_desktop_render_plan_with_derived_assets(
      adjustment_masked_state, request);
  require(unmasked_group_plan.ok() && masked_group_plan.ok() &&
              unmasked_adjustment_plan.ok() && masked_adjustment_plan.ok() &&
              !layer_cache_identity(unmasked_group_plan).empty() &&
              layer_cache_identity(masked_group_plan) != layer_cache_identity(unmasked_group_plan) &&
              layer_cache_identity(masked_adjustment_plan) !=
                  layer_cache_identity(unmasked_adjustment_plan),
          "Group and adjustment masks must participate in Layers-stage cache identity.");

  const auto recipe = serialize_edit_recipe_json(adjustment_masked_state);
  EditorPreviewController reopened;
  reopened.loadAsset(QStringLiteral("vector-mask-kinds-reopened"), base_path,
                     {}, {}, {}, {}, recipe);
  settle(reopened);
  require(render(base, state(reopened)) == adjustment_masked,
          "Fill, pixel, group, and adjustment masks must reopen pixel-identically.");
}

}  // namespace

namespace {

void test_raster_intersection_history_and_persistence(
    const QString& base_path, const QImage& base, const QString& overlay_path) {
  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("vector-mask-raster-intersection"),
                   base_path, {}, {}, {}, {}, {});
  settle(editor);
  require(editor.addSourceLayer(overlay_path, QStringLiteral("Raster masked source")),
          "Could not add a raster-mask target source layer.");
  const auto layer_id = state(editor).layers.first().toMap()
                            .value(QStringLiteral("id")).toString();
  const auto index = layer_index(editor, layer_id);
  require(index >= 0, "Raster-mask target needs a stable layer ID.");

  editor.addMask(QStringLiteral("rectangle"));
  editor.setActiveMaskFeather(0.0);
  editor.setActiveMaskCenterX(0.5);
  editor.setActiveMaskCenterY(0.5);
  editor.setActiveMaskRadiusX(0.25);
  editor.setActiveMaskRadiusY(0.45);
  settle(editor);
  require(editor.applyWorkingSelectionToLayer(index),
          "Could not attach the editable raster selection mask.");
  settle(editor);
  const auto raster_layer = layer_by_id(state(editor), layer_id);
  const auto raster_refs = raster_layer.value(QStringLiteral("selectionMaskSetIds")).toList();
  require(!raster_refs.isEmpty() &&
              raster_layer.value(QStringLiteral("selectionScoped")).toBool(),
          "The attached selection mask must be retained as a layer-to-mask-set reference.");
  const auto raster_set_id = raster_refs.first().toString();
  QVariantList raster_sources;
  for (const auto& value : state(editor).mask_stack) {
    const auto source = value.toMap();
    if (source.value(QStringLiteral("maskSetId")).toString() == raster_set_id)
      raster_sources.append(source);
  }
  require(!raster_sources.isEmpty(),
          "The layer mask-set reference must resolve to editable raster-mask sources.");
  const auto raster_state = state(editor);
  const auto raster_only = render(base, raster_state);
  auto base_only_state = raster_state;
  base_only_state.layers.clear();
  const auto base_render = render(base, base_only_state);
  auto unmasked_state = raster_state;
  auto unmasked_layer = unmasked_state.layers.at(index).toMap();
  for (const auto* key : {"selectionMaskSetId", "selectionMaskSetIds",
                          "selectionMaskSetName", "selectionMaskSetNames"})
    unmasked_layer.remove(QLatin1String(key));
  unmasked_layer.insert(QStringLiteral("selectionScoped"), false);
  unmasked_state.layers[index] = unmasked_layer;
  const auto unmasked_render = render(base, unmasked_state);
  bool found_raster_covered_pixel = false;
  bool found_raster_excluded_pixel = false;
  int fully_covered_pixels = 0;
  int excluded_pixels = 0;
  int partially_covered_pixels = 0;
  for (int y = 0; y < raster_only.height(); ++y) {
    for (int x = 0; x < raster_only.width(); ++x) {
      const auto raster_pixel = raster_only.pixelColor(x, y);
      const auto unmasked_pixel = unmasked_render.pixelColor(x, y);
      const auto base_pixel = base_render.pixelColor(x, y);
      if (raster_pixel != base_pixel && raster_pixel == unmasked_pixel) {
        found_raster_covered_pixel = true;
        ++fully_covered_pixels;
      } else if (raster_pixel == base_pixel && unmasked_pixel != base_pixel) {
        found_raster_excluded_pixel = true;
        ++excluded_pixels;
      } else if (raster_pixel != unmasked_pixel) {
        ++partially_covered_pixels;
      }
    }
  }
  if (!found_raster_covered_pixel || !found_raster_excluded_pixel) {
    const auto color_at = [](const QImage& image, int x, int y) {
      return image.pixelColor(x, y).name(QColor::HexArgb).toStdString();
    };
    const auto first_source = raster_sources.first().toMap();
    std::cerr << "raster coverage diagnostic: refs=" << raster_refs.size()
              << " sources=" << raster_sources.size()
              << " kind=" << first_source.value(QStringLiteral("kind")).toString().toStdString()
              << " sourceId=" << first_source.value(QStringLiteral("id")).toString().toStdString()
              << " setId=" << first_source.value(QStringLiteral("maskSetId")).toString().toStdString()
              << " covered=" << fully_covered_pixels
              << " excluded=" << excluded_pixels
              << " partial=" << partially_covered_pixels
              << " output=" << raster_only.width() << 'x' << raster_only.height() << '\n';
    for (const auto& point : {QPoint(0, 0), QPoint(24, 32), QPoint(32, 32),
                              QPoint(48, 32), QPoint(72, 32), QPoint(95, 63)}) {
      std::cerr << "  pixel(" << point.x() << ',' << point.y() << ") baseRender="
                << color_at(base_render, point.x(), point.y()) << " raw="
                << color_at(base, point.x(), point.y()) << " masked="
                << color_at(raster_only, point.x(), point.y()) << " unmasked="
                << color_at(unmasked_render, point.x(), point.y()) << '\n';
    }
  }
  require(found_raster_covered_pixel && found_raster_excluded_pixel,
          "Referenced raster-mask sources must both preserve covered pixels and exclude uncovered pixels in rendering.");

  const auto definition = rectangle_path(QStringLiteral("vector-mask"),
                                         0.55, 0.1, 0.9, 0.9);
  require(editor.setPhotoLayerVectorMask(index, definition),
          "Could not attach a closed vector mask to a raster-masked layer.");
  settle(editor);
  const auto attached = state(editor);
  const auto attached_layer = layer_by_id(attached, layer_id);
  QVariantList attached_sources;
  for (const auto& value : attached.mask_stack) {
    const auto source = value.toMap();
    if (source.value(QStringLiteral("maskSetId")).toString() == raster_set_id)
      attached_sources.append(source);
  }
  require(attached_layer.value(QStringLiteral("selectionMaskSetIds")).toList() == raster_refs &&
              attached_sources == raster_sources &&
              attached_layer.value(QStringLiteral("vectorMask")).toMap()
                  .value(QStringLiteral("path")).toMap() == definition,
          "Editable vector geometry must coexist with and preserve referenced raster mask sources.");
  const auto vector_and_raster = render(base, attached);
  const auto overlap_map = editor.mapPhotoVectorMaskCoordinate(index, 0.68, 0.5, true);
  const auto raster_only_map = editor.mapPhotoVectorMaskCoordinate(index, 0.34, 0.5, true);
  const auto overlap = pixel_for_view_point(overlap_map, vector_and_raster);
  const auto raster_only_area = pixel_for_view_point(raster_only_map, vector_and_raster);
  require(vector_and_raster.pixelColor(overlap) == raster_only.pixelColor(overlap) &&
              vector_and_raster.pixelColor(raster_only_area) == base_render.pixelColor(raster_only_area) &&
              vector_and_raster.pixelColor(raster_only_area) != raster_only.pixelColor(raster_only_area),
          "Vector coverage must intersect existing raster alpha instead of replacing it.");

  editor.undo();
  settle(editor);
  require(render(base, state(editor)) == raster_only &&
              !layer_by_id(state(editor), layer_id).contains(QStringLiteral("vectorMask")),
          "One undo must remove the entire vector-mask attachment.");
  editor.redo();
  settle(editor);
  require(render(base, state(editor)) == vector_and_raster,
          "Redo must restore the same vector/raster intersection pixels.");

  require(editor.setPhotoLayerVectorMaskEnabled(index, false),
          "Could not bypass the vector mask.");
  settle(editor);
  require(render(base, state(editor)) == raster_only,
          "Bypassing vector coverage must leave the independent raster mask active.");
  editor.undo();
  settle(editor);

  require(editor.setPhotoLayerVectorMaskInverted(index, true),
          "Could not invert vector-mask coverage.");
  settle(editor);
  const auto inverted = render(base, state(editor));
  require(inverted.pixelColor(raster_only_area) == raster_only.pixelColor(raster_only_area) &&
              inverted.pixelColor(overlap) != raster_only.pixelColor(overlap),
          "Inversion must exchange vector-covered and vector-excluded regions under raster alpha.");
  require(editor.setPhotoLayerVectorMaskDensity(index, 0.0),
          "Could not lower vector-mask density to zero.");
  settle(editor);
  require(render(base, state(editor)) == raster_only,
          "Zero density must make vector coverage all-white while preserving raster alpha.");
  editor.undo();
  settle(editor);

  require(editor.setPhotoLayerLocked(index, true), "Could not lock vector-mask target.");
  settle(editor);
  const auto locked = state(editor);
  require(!editor.setPhotoLayerVectorMaskEnabled(index, false) &&
              !editor.removePhotoLayerVectorMask(index) && state(editor) == locked,
          "Locked layers must reject vector-mask setting and removal atomically.");
  require(editor.setPhotoLayerLocked(index, false), "Could not unlock vector-mask target.");
  settle(editor);

  const auto before_open = state(editor);
  require(!editor.setPhotoLayerVectorMask(layer_index(editor, layer_id),
                                          open_path(QStringLiteral("open-mask"))) &&
              state(editor) == before_open,
          "Open paths must be rejected as masks without changing the layer recipe.");

  const auto recipe = serialize_edit_recipe_json(state(editor));
  require(validate_edit_recipe_json(recipe).valid,
          "A vector mask with its independent raster mask must validate in a durable recipe.");
  const auto final_state = state(editor);
  QTemporaryDir canonical_assets_directory;
  require(canonical_assets_directory.isValid(),
          "Could not create canonical raster/vector mask asset storage.");
  RecipeAssetStore assets(
      canonical_assets_directory.filePath(QStringLiteral("canonical-recipe-assets")));
  const auto canonical = serialize_desktop_recipe_as_canonical_v13_with_assets(
      final_state,
      {.recipe_id = "vector-mask-raster-intersection",
       .source_id = "vector-mask-raster-source",
       .decoder_provenance = "raster:vector-mask-raster-v1",
       .raw_source = false},
      assets);
  if (!canonical.ok()) {
    for (const auto& error : canonical.errors)
      std::cerr << error.path << ": " << error.message << '\n';
  }
  require(canonical.ok(),
          "Canonical v13 serialization must retain vector masks and referenced raster sources.");
  const auto projected = project_canonical_v13_document_to_desktop_state_with_assets(
      canonical.document, assets);
  if (!projected.ok()) {
    for (const auto& error : projected.errors)
      std::cerr << error.path << ": " << error.message << '\n';
  }
  const bool layer_mask_recipes_equal = projected.ok() &&
      vector_mask_layer_recipes_equal(final_state, *projected.state);
  const bool all_layers_equal = projected.ok() &&
                                projected.state->layers == final_state.layers;
  const bool mask_stack_equal = projected.ok() &&
                                projected.state->mask_stack == final_state.mask_stack;
  const bool projected_render_equal = projected.ok() &&
      render(base, *projected.state) == render(base, final_state);
  if (projected.ok() &&
      (!all_layers_equal || !layer_mask_recipes_equal || !mask_stack_equal ||
       !projected_render_equal)) {
    std::cerr << "canonical raster/vector mask diagnostic: layerMasks="
              << layer_mask_recipes_equal << " allLayers=" << all_layers_equal
              << " maskStack=" << mask_stack_equal
              << " render=" << projected_render_equal
              << " layerCount=" << final_state.layers.size() << '/'
              << projected.state->layers.size()
              << " maskCount=" << final_state.mask_stack.size() << '/'
              << projected.state->mask_stack.size() << '\n';
    for (qsizetype layer = 0; layer < std::min(final_state.layers.size(),
                                               projected.state->layers.size()); ++layer) {
      const auto before = final_state.layers.at(layer).toMap();
      const auto after = projected.state->layers.at(layer).toMap();
      if (before != after) {
        std::cerr << "  layer[" << layer << "] id="
                  << before.value(QStringLiteral("id")).toString().toStdString()
                  << " vectorMask before="
                  << QJsonDocument::fromVariant(before.value(QStringLiteral("vectorMask")))
                         .toJson(QJsonDocument::Compact).constData()
                  << " after="
                  << QJsonDocument::fromVariant(after.value(QStringLiteral("vectorMask")))
                         .toJson(QJsonDocument::Compact).constData() << '\n';
        for (const auto& key : before.keys()) {
          if (before.value(key) != after.value(key)) {
            std::cerr << "    field " << key.toStdString() << " before="
                      << QJsonDocument::fromVariant(before.value(key))
                             .toJson(QJsonDocument::Compact).constData()
                      << " after="
                      << QJsonDocument::fromVariant(after.value(key))
                             .toJson(QJsonDocument::Compact).constData() << '\n';
          }
        }
        for (const auto& key : after.keys()) {
          if (!before.contains(key))
            std::cerr << "    added field " << key.toStdString() << " after="
                      << QJsonDocument::fromVariant(after.value(key))
                             .toJson(QJsonDocument::Compact).constData() << '\n';
        }
      }
    }
    for (qsizetype mask = 0; mask < std::min(final_state.mask_stack.size(),
                                             projected.state->mask_stack.size()); ++mask) {
      if (final_state.mask_stack.at(mask) != projected.state->mask_stack.at(mask)) {
        std::cerr << "  mask[" << mask << "] before="
                  << QJsonDocument::fromVariant(final_state.mask_stack.at(mask))
                         .toJson(QJsonDocument::Compact).constData()
                  << " after="
                  << QJsonDocument::fromVariant(projected.state->mask_stack.at(mask))
                         .toJson(QJsonDocument::Compact).constData() << '\n';
      }
    }
  }
  require(projected.ok() && layer_mask_recipes_equal && mask_stack_equal &&
              projected_render_equal,
          "Canonical v13 asset projection must preserve exact vector-mask, raster-source, and rendered coverage.");
  EditorPreviewController reopened;
  reopened.loadAsset(QStringLiteral("vector-mask-reopened"), base_path,
                     {}, {}, {}, {}, serialize_edit_recipe_json(*projected.state));
  settle(reopened);
  require(render(base, state(reopened)) == render(base, final_state) &&
              layer_by_id(state(reopened), layer_id).value(QStringLiteral("vectorMask")) ==
                  layer_by_id(state(editor), layer_id).value(QStringLiteral("vectorMask")),
          "Saved/reopened vector geometry and rendered alpha must match exactly.");
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR",
         runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR",
         runtime.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication application(argc, argv);
  struct DrainWorkers {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(60000); }
  } drain_workers;

  try {
    const QSize size(96, 64);
    const auto base = patterned_image(size, QColor(QStringLiteral("#3b5978")));
    QImage red(size, QImage::Format_RGB32);
    red.setColorSpace(QColorSpace(QColorSpace::SRgb));
    red.fill(QColor(QStringLiteral("#e02024")));
    QImage blue(size, QImage::Format_RGB32);
    blue.setColorSpace(QColorSpace(QColorSpace::SRgb));
    blue.fill(QColor(QStringLiteral("#174be0")));
    const auto base_path = runtime.filePath(QStringLiteral("base.png"));
    const auto red_path = runtime.filePath(QStringLiteral("red.png"));
    const auto blue_path = runtime.filePath(QStringLiteral("blue.png"));
    require(base.save(base_path) && red.save(red_path) && blue.save(blue_path),
            "Could not save deterministic vector-mask source fixtures.");

    test_raster_intersection_history_and_persistence(base_path, base, red_path);
    test_fill_pixel_adjustment_group_and_cache(base_path, base);
    test_crop_rotation_and_layer_transfer(base_path, base, red_path, blue_path);
    test_rotated_parent_crop_alignment(base_path, base, red_path);
    test_component_source_document_mask_moves_with_instance(base_path, base);
    std::cout << "photo_vector_mask_workflow_test passed\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "photo vector mask workflow failure: " << exception.what() << '\n';
    return 1;
  }
}
