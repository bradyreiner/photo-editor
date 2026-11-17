#include "editor_preview_controller.h"
#include "canonical_composite_mask_alpha.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_v13_persistence.h"
#include "photo_paint_surface.h"
#include "photo_render_utils.h"
#include "desktop_recipe_persistence_service.h"
#include "mask_render_change_policy.h"

#include <QGuiApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJSEngine>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;
namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static int photo_intents(const EditorPreviewController& editor) { return editor.edit_intent_count_; }
  static bool photo_pending(const EditorPreviewController& editor) {
    return editor.render_scheduler_.interactiveActive() || editor.render_scheduler_.fullActive() ||
        editor.interactive_render_watcher_.isRunning() || editor.full_render_watcher_.isRunning() ||
        editor.interactive_request_pending_ || editor.full_request_pending_;
  }
  static bool working_outline_running(const EditorPreviewController& editor) {
    return editor.working_outline_running_;
  }
  static QString working_outline_key(const EditorPreviewController& editor) {
    return editor.working_outline_key_;
  }
  static void publish_failed_pick(EditorPreviewController& editor, const QVariantList& points) {
    SmartSelectCommitResult result;
    auto& request = result.request;
    request.asset_id = editor.asset_id_;
    request.file_path = editor.file_path_;
    request.state = editor.currentState();
    request.points = points;
    request.generation = editor.mask_session_.advanceSmartSelectionGeneration();
    request.state_revision = editor.mask_session_.workRevision();
    request.job = AsyncMaskJob{
        .job_id = QStringLiteral("failed-pick-test"),
        .job_kind = QStringLiteral("smart_selection"),
        .stage = QStringLiteral("async_commit_publish"),
        .asset_id = request.asset_id,
        .file_path = request.file_path,
        .state_signature = QStringLiteral("sha1:%1").arg(QString::fromLatin1(QCryptographicHash::hash(
            adjustment_patch_identity_json(request.state).toUtf8(), QCryptographicHash::Sha1).toHex())),
        .generation = request.generation,
        .state_revision = request.state_revision};
    editor.mask_worker_coordinator_.latestSmartSelectCommitJob() = request.job;
    result.decode_success = true;
    result.segmentation_result.error_message = QStringLiteral("No object found");
    editor.publishSmartSelectCommitResult(result);
  }
};
}
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
EditState state(EditorPreviewController& editor);
QByteArray alpha(EditorPreviewController& editor, const QImage& source, const QString& id);
void settle(EditorPreviewController& editor) {
  QElapsedTimer timer; timer.start();
  do { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
  while ((editor.processing() || editor.photoPaintBusy() || editor.selectionWorkBusy() || editor.brushStrokeActive()) && timer.elapsed() < 20000);
  require(!editor.processing() && !editor.photoPaintBusy() && !editor.selectionWorkBusy(), "Pending editing work must finish");
  if (!editor.photoPaintError().isEmpty()) throw std::runtime_error(editor.photoPaintError().toStdString());
}
void settle_outline(EditorPreviewController& editor) {
  QElapsedTimer timer; timer.start();
  do {
    static_cast<void>(editor.workingSelectionOutlinePaths());
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  } while (EditorPreviewControllerTestAccess::working_outline_running(editor) && timer.elapsed() < 20000);
  require(!EditorPreviewControllerTestAccess::working_outline_running(editor), "Working outline must finish");
  require(!editor.workingSelectionOutlinePaths().isEmpty(), "Working outline must contain selection coverage");
}
void settle_photo(EditorPreviewController& editor) {
  QElapsedTimer timer; timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1);
  } while ((editor.processing() || EditorPreviewControllerTestAccess::photo_pending(editor)) && timer.elapsed() < 20000);
  require(!editor.processing() && !EditorPreviewControllerTestAccess::photo_pending(editor), "Photographic work must settle");
}
void verify_free_selection_rendering(const QString& path, const QImage& source) {
  EditorPreviewController editor;
  editor.loadAsset("selection-overlay-only", path, {}, {}, {}, {}, {}); settle_photo(editor);
  const auto before = EditorPreviewControllerTestAccess::photo_intents(editor);
  editor.addMask("rectangle"); editor.setActiveMaskFeather(0);
  for (int i = 0; i < 12; ++i) editor.setActiveMaskCenterX(0.2 + i * 0.025);
  require(EditorPreviewControllerTestAccess::photo_intents(editor) == before &&
      !EditorPreviewControllerTestAccess::photo_pending(editor),
      "Creating and reshaping a free selection must not queue photographic rendering");
  const auto working = editor.workingSelectionSetId();
  require(editor.addFillLayer("#397ca4", "Artwork"), "Create artwork while retaining selection"); settle_photo(editor);
  const auto layered_before = EditorPreviewControllerTestAccess::photo_intents(editor);
  for (int i = 0; i < 12; ++i) editor.setActiveMaskCenterX(0.25 + i * 0.02);
  require(EditorPreviewControllerTestAccess::photo_intents(editor) == layered_before,
      "Free selection changes must also leave a layered photograph's preview alone");
  require(editor.applyWorkingSelectionToLayer(0), "Attach independent coverage to artwork"); settle_photo(editor);
  const auto attached_id = state(editor).layers[0].toMap().value("selectionMaskSetId").toString();
  require(editor.selectLayerMask(0, attached_id), "Inspect the attached mask");
  const auto attached_before = EditorPreviewControllerTestAccess::photo_intents(editor);
  const auto pixels_before = render_output_image_with_backend(source, state(editor), QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  editor.setActiveMaskCenterX(0.15); settle_photo(editor);
  require(EditorPreviewControllerTestAccess::photo_intents(editor) > attached_before,
      "Changing an attached mask must still render the photograph");
  const auto pixels_after = render_output_image_with_backend(source, state(editor), QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  require(pixels_before != pixels_after && editor.workingSelectionSetId() == working,
      "Attached coverage changes output without altering the working selection identity");
  editor.activateWorkingSelection();
  const auto local_before = EditorPreviewControllerTestAccess::photo_intents(editor);
  editor.setActiveMaskExposure(0.7); settle_photo(editor);
  require(EditorPreviewControllerTestAccess::photo_intents(editor) > local_before,
      "Legacy mask-local color changes must retain photographic rendering");
  std::cout << "selection-overlay-photo-intents: bare=0; layered=0; attached-mask=rendered; local-color=rendered\n";
}
void verify_mask_render_consumers() {
  const QVariantMap mask{{"id", "source"}, {"maskSetId", "set"}, {"maskSetName", "Selected area"},
      {"toolRole", "selection"}, {"centerX", 0.4}};
  auto moved = mask; moved.insert("centerX", 0.6);
  for (const auto& owner : {QVariantMap{{"selectionMaskSetId", "set"}},
           QVariantMap{{"selectionMaskSetName", "Selected area"}},
           QVariantMap{{"selectionIntersectionMaskSetIds", QStringList{"set"}}},
           QVariantMap{{"effectStack", "malformed"}},
           QVariantMap{{"smartObjectDocument", QVariantMap{}}}}) {
    require(mask_stack_change_affects_photo({mask}, {moved}, {owner}),
        "Primary, legacy-name, intersection and uncertain operation/document consumers retain renders");
  }
  auto repair = moved; repair.insert("toolRole", "retouch"); repair.insert("committed", true);
  require(mask_stack_change_affects_photo({mask}, {repair}, {}), "Retouch changes retain renders");
  auto solo = moved; solo.insert("solo", true);
  require(mask_stack_change_affects_photo({mask}, {solo}, {}), "Solo filtering retains renders");
  auto second = moved; second.insert("id", "second-source"); second.insert("operation", "subtract");
  require(mask_stack_change_affects_photo({mask, second}, {second, mask},
      QVariantList{QVariantMap{{"selectionMaskSetId", "set"}}}),
      "Source reordering retains renders for an attached set");
}
void verify_working_outline_cache(const QString& path) {
  EditorPreviewController editor;
  editor.loadAsset("working-outline-cache", path, {}, {}, {}, {}, {}); settle(editor);
  editor.addMask("rectangle"); editor.setActiveMaskFeather(0); settle(editor);
  require(editor.addAdjustmentLayer("Unrelated grade"), "Create unrelated layer for outline cache test");
  settle(editor); settle_outline(editor);
  int rebuilds = 0;
  QElapsedTimer timer; timer.start();
  for (int i = 0; i < 12; ++i) {
    const auto previous_key = EditorPreviewControllerTestAccess::working_outline_key(editor);
    require(editor.setPhotoLayerAdjustmentValue(0, "exposure", 0.1 * (i + 1)), "Change unrelated layer grade");
    settle(editor); settle_outline(editor);
    if (EditorPreviewControllerTestAccess::working_outline_key(editor) != previous_key) ++rebuilds;
  }
  std::cout << "working-outline-unrelated-edits: rebuilds=" << rebuilds
            << "; elapsed_ms=" << timer.elapsed() << '\n';
  if (qEnvironmentVariableIsSet("LOOKSY_OUTLINE_BASELINE_ONLY")) return;
  require(rebuilds == 0, "Unrelated layer edits must reuse working selection geometry");
  const auto old_key = EditorPreviewControllerTestAccess::working_outline_key(editor);
  editor.setActiveMaskCenterX(0.2); settle(editor); settle_outline(editor);
  require(EditorPreviewControllerTestAccess::working_outline_key(editor) != old_key,
      "Selection geometry changes must invalidate the outline");
  const auto geometry_key = EditorPreviewControllerTestAccess::working_outline_key(editor);
  editor.setExposure(0.5); settle(editor); settle_outline(editor);
  require(EditorPreviewControllerTestAccess::working_outline_key(editor) != geometry_key,
      "Changed global sampling inputs must invalidate working outlines");
}
void verify_explicit_selection_adjustment(const QString& path, const QImage& source) {
  EditorPreviewController editor;
  editor.loadAsset("explicit-selection-adjustment", path, {}, {}, {}, {}, {}); settle(editor);
  require(editor.createAdjustmentLayerFromWorkingSelection() < 0 && editor.photoLayerCount() == 0,
      "No selection must not create an empty adjustment layer");
  editor.addMask("rectangle"); editor.setActiveMaskFeather(0); settle(editor);
  const auto selection_id = editor.workingSelectionSetId();
  const auto before = state(editor);
  require(editor.createAdjustmentLayerFromWorkingSelection() == 0, "Explicit selection adjustment action succeeds");
  settle(editor);
  const auto created = state(editor);
  require(created.layers.size() == 1, "Adjust selection creates exactly one layer");
  const auto mask_id = created.layers[0].toMap().value("selectionMaskSetId").toString();
  require(!mask_id.isEmpty() && mask_id != selection_id && editor.workingSelectionSetId() == selection_id,
      "Adjustment owns an independent mask and preserves the working selection");
  require(alpha(editor, source, mask_id) == alpha(editor, source, selection_id),
      "Attached adjustment mask exactly matches the selected coverage");
  editor.undo(); settle(editor);
  require(state(editor).layers == before.layers && state(editor).mask_stack == before.mask_stack,
      "One undo removes the new adjustment and its mask together");
  editor.redo(); settle(editor);
  require(state(editor).layers == created.layers && state(editor).mask_stack == created.mask_stack,
      "Redo restores the single masked adjustment exactly");
  editor.activateWorkingSelection(); editor.setActiveMaskCenterX(0.15); settle(editor);
  require(alpha(editor, source, mask_id) != alpha(editor, source, selection_id),
      "Editing the working selection must not move the adjustment mask");
  const auto recipe_before_miss = editor.currentAssetBoundAdjustmentPatchJson();
  const auto history_before_miss = editor.editHistoryCurrentIndex();
  for (const auto& prompts : {QVariantList{QVariantMap{{"x", 0.02}, {"y", 0.02}}},
           QVariantList{QVariantMap{{"type", "box"}, {"left", 0.0}, {"top", 0.0}, {"right", 0.2}, {"bottom", 0.2}}}}) {
    EditorPreviewControllerTestAccess::publish_failed_pick(editor, prompts);
    require(editor.currentAssetBoundAdjustmentPatchJson() == recipe_before_miss &&
        editor.editHistoryCurrentIndex() == history_before_miss && editor.maskActionRetryAvailable(),
        "Failed point and box picks preserve the prior selection, layers and history and expose retry");
  }
}
EditState state(EditorPreviewController& editor) {
  const auto parsed = parse_adjustment_patch_json(editor.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok(), "Recipe must parse"); return *parsed.state;
}
int layer_index(EditorPreviewController& editor, const QString& id) {
  const auto layers = state(editor).layers;
  for (int i = 0; i < layers.size(); ++i) if (layers[i].toMap().value("id").toString() == id) return i;
  return -1;
}
QVariantList masks(EditorPreviewController& editor, const QString& id) {
  QVariantList result;
  for (const auto& value : editor.maskStack()) if (value.toMap().value("maskSetId").toString() == id) result.append(value);
  return result;
}
QByteArray alpha(EditorPreviewController& editor, const QImage& source, const QString& id) {
  const auto result = evaluate_canonical_composite_mask_alpha(source, masks(editor, id), 1.0, {1, 1, 1});
  require(result.valid, "Mask coverage must evaluate"); return result.alpha_mask;
}
void verify_edge_adjustment_pixels(const QString& path, const QImage& source) {
  const auto count = [](const QByteArray& bytes, bool fractional) {
    return std::count_if(bytes.begin(), bytes.end(), [fractional](char value) {
      const auto alpha = static_cast<unsigned char>(value);
      return fractional ? alpha > 0 && alpha < 255 : alpha >= 128;
    });
  };
  for (const auto& kind : {QString("rectangle"), QString("ellipse"), QString("polygon"), QString("subject"), QString("brush")}) {
    EditorPreviewController editor;
    editor.loadAsset("edge-pixels-" + kind, path, {}, {}, {}, {}, {}); settle(editor);
    editor.addMask("rectangle"); editor.setActiveMaskFeather(0.0);
    auto fixture = state(editor);
    auto mask = fixture.mask_stack[0].toMap();
    mask.insert("kind", kind); mask.insert("feather", 0.0);
    mask.insert("selectionFeather", 0.0);
    mask.insert("polygonPoints", QVariantList{QVariantMap{{"x", .3}, {"y", .3}},
        QVariantMap{{"x", .7}, {"y", .3}}, QVariantMap{{"x", .7}, {"y", .7}}, QVariantMap{{"x", .3}, {"y", .7}}});
    QByteArray raw(source.width() * source.height(), char(0));
    for (int y = 35; y < 85; ++y) for (int x = 50; x < 110; ++x) raw[y * source.width() + x] = char(255);
    if (kind == "subject" || kind == "brush") {
      mask.insert("segmentationData", raw); mask.insert("segmentationWidth", source.width());
      mask.insert("segmentationHeight", source.height());
    }
    fixture.mask_stack = {mask};
    editor.loadAsset("edge-pixels-" + kind, path, {}, {}, {}, {}, serialize_edit_recipe_json(fixture)); settle(editor);
    const auto id = editor.workingSelectionSetId();
    const auto authored_alpha = editor.maskStack()[0].toMap().value("segmentationData").toByteArray();
    const auto hard = alpha(editor, source, id);
    require(count(hard, true) == 0, "Zero feather must produce a hard-edged fixture");
    require(editor.setSelectionEdgeAdjustment("feather", .25), "Feather must accept an edit"); settle(editor);
    const auto feathered = alpha(editor, source, id);
    require(feathered != hard && count(feathered, true) > 0, "Feather must change real matte pixels, including binary AI/painted masks");
    if (kind == "subject" || kind == "brush") {
      const QRect crop(51, 36, 64, 54);
      const QRectF area(double(crop.x()) / (source.width() - 1), double(crop.y()) / (source.height() - 1),
          double(crop.width() - 1) / (source.width() - 1), double(crop.height() - 1) / (source.height() - 1));
      const auto cropped = evaluate_canonical_composite_mask_alpha(source.copy(crop), masks(editor, id), 1.0,
          {1, 1, 1}, area, QSizeF(source.width() - 1, source.height() - 1));
      require(cropped.valid, "Cropped feather preview evaluates");
      for (int y = 0; y < crop.height(); ++y) for (int x = 0; x < crop.width(); ++x)
        require(std::abs(static_cast<unsigned char>(cropped.alpha_mask[y * crop.width() + x]) -
            static_cast<unsigned char>(feathered[(y + crop.y()) * source.width() + x + crop.x()])) <= 1,
            "Cropped feathered coverage must match the full source, including boundary samples");
    }
    editor.undo(); settle(editor); require(alpha(editor, source, id) == hard, "Undo feather restores exact alpha");
    editor.redo(); settle(editor); require(alpha(editor, source, id) == feathered, "Redo feather restores exact alpha");
    require(editor.setSelectionEdgeAdjustment("fineEdge", 80), "Fine edge accepts an edit"); settle(editor);
    require(alpha(editor, source, id) != feathered, "Fine edge changes feathered coverage");
    require(editor.setSelectionEdgeAdjustment("contrast", 80), "Edge contrast accepts an edit"); settle(editor);
    const auto contrast = alpha(editor, source, id);
    require(contrast != feathered, "Edge contrast changes edge coverage");
    require(editor.setSelectionEdgeAdjustment("fineEdge", 0) && editor.setSelectionEdgeAdjustment("contrast", 0) &&
        editor.setSelectionEdgeAdjustment("feather", 0), "Reset edge parameters"); settle(editor);
    require(alpha(editor, source, id) == hard, "Zero feather restores raw coverage without baking changes into the source");
    require(editor.setSelectionEdgeAdjustment("shift", 10), "Grow edges"); settle(editor);
    require(count(alpha(editor, source, id), false) > count(hard, false), "Positive edge shift must grow a hard mask geometrically");
    require(editor.setSelectionEdgeAdjustment("shift", -10), "Shrink edges"); settle(editor);
    require(count(alpha(editor, source, id), false) < count(hard, false), "Negative edge shift must shrink a hard mask geometrically");
    require(editor.setSelectionEdgeAdjustment("shift", 0) && editor.setSelectionEdgeAdjustment("smooth", 60), "Smooth edges"); settle(editor);
    require(alpha(editor, source, id) != hard, "Smooth must alter actual hard-edge pixels");
    require(editor.setSelectionEdgeAdjustment("smooth", 0), "Reset smoothing");
    const auto target = editor.selectionEdgeControls().value("id");
    editor.startSelectionBrush(false);
    const auto brush_index = editor.activeMaskIndex();
    require(editor.setSelectionEdgeAdjustment("density", .4), "Change composed mask density from a new brush");
    auto strengthened = alpha(editor, source, id);
    require(*std::max_element(reinterpret_cast<const unsigned char*>(strengthened.constData()),
        reinterpret_cast<const unsigned char*>(strengthened.constData()) + strengthened.size()) <= 102,
        "Density must cap the entire mask, including existing components");
    require(editor.setSelectionEdgeAdjustment("opacity", .5), "Change composed mask opacity");
    strengthened = alpha(editor, source, id);
    require(*std::max_element(reinterpret_cast<const unsigned char*>(strengthened.constData()),
        reinterpret_cast<const unsigned char*>(strengthened.constData()) + strengthened.size()) <= 51,
        "Opacity must scale the entire mask after density");
    require(editor.setSelectionEdgeAdjustment("density", 1) && editor.setSelectionEdgeAdjustment("opacity", 1) &&
        editor.setSelectionEdgeAdjustment("invert", 1), "Restore strength and invert the composed mask");
    const auto inverted = alpha(editor, source, id);
    for (int i = 0; i < hard.size(); ++i)
      require(static_cast<unsigned char>(inverted[i]) == 255 - static_cast<unsigned char>(hard[i]),
          "Inversion must invert the complete mask rather than a new empty brush");
    require(editor.setSelectionEdgeAdjustment("invert", 0), "Restore composed mask inversion");
    require(editor.selectionEdgeControls().value("id") == target &&
        editor.setSelectionEdgeAdjustment("feather", .2) && editor.activeMaskIndex() == brush_index,
        "Starting a refine brush must keep edge controls on the established target without changing the brush target");
    if (kind == "subject" || kind == "brush")
      require(editor.maskStack()[0].toMap().value("segmentationData").toByteArray() == authored_alpha,
          "Feather and refinements must preserve the authored source alpha exactly");
    if (kind == "subject") {
      require(editor.createAdjustmentLayerFromWorkingSelection("Feather proof") == 0,
          "Create an independently masked adjustment"); settle(editor);
      require(editor.setPhotoLayerAdjustmentValue(0, "exposure", 1.0), "Give the masked adjustment a visible edit"); settle(editor);
      const auto attached = state(editor).layers[0].toMap().value("selectionMaskSetId").toString();
      require(editor.selectLayerMask(0, attached), "Select the layer mask for refinement");
      const auto before = render_output_image_with_backend(source, state(editor), QColorSpace(QColorSpace::SRgb),
          QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      require(editor.setSelectionEdgeAdjustment("feather", .45), "Refine the attached layer mask"); settle(editor);
      const auto after = render_output_image_with_backend(source, state(editor), QColorSpace(QColorSpace::SRgb),
          QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      require(after != before, "Layer-mask feather must change final photograph pixels");
      const auto saved = serialize_edit_recipe_json(state(editor));
      EditorPreviewController reopened;
      reopened.loadAsset("reopened-feather", path, {}, {}, {}, {}, saved); settle(reopened);
      require(render_output_image_with_backend(source, state(reopened), QColorSpace(QColorSpace::SRgb),
          QImage::Format_ARGB32, RenderBackendPreference::CpuOnly) == after, "Feathered layer-mask output survives save/reopen exactly");
      require(editor.setPhotoLayerLocked(0, true), "Lock the refined layer");
      const auto locked = editor.currentAssetBoundAdjustmentPatchJson();
      require(!editor.setSelectionEdgeAdjustment("feather", .1) && editor.currentAssetBoundAdjustmentPatchJson() == locked,
          "Refinement must respect the owning layer lock without changing coverage");
    }
    std::cout << "edge-pixels: " << kind.toStdString() << " feather/shift/smooth/fine/contrast/history passed\n";
  }
}
void stroke(EditorPreviewController& editor, int index, double x, double y, bool erase, bool release = true) {
  require(editor.beginPhotoPaintStroke(index, {{"erase", erase}, {"color", "#1688ff"}, {"size", 18.0},
      {"hardness", 1.0}, {"opacity", 1.0}, {"flow", 1.0}, {"pressureSize", false}}), "Begin targeted brush stroke");
  editor.addPhotoPaintPoint(x, y);
  if (release) { editor.finishPhotoPaintStroke(); settle(editor); }
}
QStringList string_values(const QVariantMap& map, const QString& key) {
  QStringList result;
  for (const auto& value : map.value(key).toList()) {
    const auto text = value.toString();
    if (!text.isEmpty()) result.append(text);
  }
  return result;
}
void verify_intersection_mask_attachments(const QString& path, const QImage& source) {
  EditorPreviewController seed;
  seed.loadAsset("intersection-mask-seed", path, {}, {}, {}, {}, {});
  settle(seed);
  seed.addMask("rectangle");
  seed.setActiveMaskFeather(0.0);
  seed.setActiveMaskCenterX(0.35);
  seed.setActiveMaskCenterY(0.5);
  seed.setActiveMaskRadiusX(0.3);
  seed.setActiveMaskRadiusY(0.45);
  settle(seed);
  require(seed.saveWorkingSelection("Left intersection"), "Save the left-hand mask set");
  const auto intersection_id = seed.savedSelections().last().toMap().value("id").toString();
  require(!intersection_id.isEmpty() && intersection_id != seed.workingSelectionSetId(),
      "Intersection fixture needs an independent second mask-set identity");
  require(seed.transformWorkingSelection({{"x", 0.3}, {"y", 0.0}, {"scaleX", 1.0}, {"scaleY", 1.0}}),
      "Move the working set to create partial overlap with the saved set");
  settle(seed);
  require(seed.addAdjustmentLayer("Conjoined adjustment"), "Create an adjustment layer for the two-domain mask");
  require(seed.applyWorkingSelectionToLayer(0), "Attach the working mask as the primary domain");
  require(seed.setPhotoLayerAdjustmentValue(0, "exposure", 0.8), "Give the combined mask a visible adjustment");

  auto fixture = state(seed);
  auto fixture_layer = fixture.layers.first().toMap();
  const auto primary_id = fixture_layer.value("selectionMaskSetId").toString();
  require(!primary_id.isEmpty() && primary_id != intersection_id,
      "Primary and intersection domains need distinct canonical IDs");
  fixture_layer.insert("selectionIntersectionMaskSetIds", QVariantList{intersection_id});
  fixture_layer.insert("selectionIntersectionMaskSetNames", QVariantList{QStringLiteral("Left intersection")});
  fixture_layer.insert("maskEnabled", true);
  fixture.layers[0] = fixture_layer;

  EditorPreviewController editor;
  editor.loadAsset("intersection-mask-workflow", path, {}, {}, {}, {}, serialize_edit_recipe_json(fixture));
  settle(editor);
  auto loaded = state(editor);
  auto owner = loaded.layers.first().toMap();
  require(string_values(owner, "selectionMaskSetIds") == QStringList{primary_id} &&
              string_values(owner, "selectionIntersectionMaskSetIds") == QStringList{intersection_id} &&
              owner.value("selectionScoped").toBool() && owner.value("maskEnabled", true).toBool(),
      "Loading must keep primary and intersection IDs in separate AND domains");

  const auto primary_alpha = alpha(editor, source, primary_id);
  const auto intersection_alpha = alpha(editor, source, intersection_id);
  const auto owner_id = owner.value("id").toString();
  const auto combined = render_output_image_with_backend(
      source, loaded, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  auto unmasked_state = loaded;
  auto unmasked_layer = unmasked_state.layers.first().toMap();
  unmasked_layer.insert("maskEnabled", false);
  unmasked_state.layers[0] = unmasked_layer;
  const auto unmasked = render_output_image_with_backend(
      source, unmasked_state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  auto baseline_state = loaded;
  auto baseline_layer = baseline_state.layers.first().toMap();
  baseline_layer.insert("enabled", false);
  baseline_state.layers[0] = baseline_layer;
  const auto baseline = render_output_image_with_backend(
      source, baseline_state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  auto primary_only_state = loaded;
  auto primary_only_layer = primary_only_state.layers.first().toMap();
  primary_only_layer.remove("selectionIntersectionMaskSetIds");
  primary_only_layer.remove("selectionIntersectionMaskSetNames");
  primary_only_state.layers[0] = primary_only_layer;
  const auto primary_only = render_output_image_with_backend(
      source, primary_only_state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  auto intersection_only_state = loaded;
  auto intersection_only_layer = intersection_only_state.layers.first().toMap();
  intersection_only_layer.insert("selectionMaskSetId", intersection_id);
  intersection_only_layer.insert("selectionMaskSetIds", QVariantList{intersection_id});
  intersection_only_layer.insert("selectionMaskSetName", QStringLiteral("Left intersection"));
  intersection_only_layer.insert("selectionMaskSetNames", QVariantList{QStringLiteral("Left intersection")});
  intersection_only_layer.remove("selectionIntersectionMaskSetIds");
  intersection_only_layer.remove("selectionIntersectionMaskSetNames");
  intersection_only_state.layers[0] = intersection_only_layer;
  const auto intersection_only = render_output_image_with_backend(
      source, intersection_only_state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  const auto overlap_offset = 60 * source.width() + 80;
  const auto left_only_offset = 60 * source.width() + 20;
  const auto right_only_offset = 60 * source.width() + 130;
  require(static_cast<unsigned char>(primary_alpha[overlap_offset]) > 254 &&
              static_cast<unsigned char>(intersection_alpha[overlap_offset]) > 254 &&
              static_cast<unsigned char>(primary_alpha[left_only_offset]) < 16 &&
              static_cast<unsigned char>(intersection_alpha[left_only_offset]) > 240 &&
              static_cast<unsigned char>(primary_alpha[right_only_offset]) > 240 &&
              static_cast<unsigned char>(intersection_alpha[right_only_offset]) < 16,
      "The fixture must contain visible overlap and non-overlap regions in both source sets");
  const auto close_color = [](const QColor& left, const QColor& right) {
    return std::abs(left.red() - right.red()) <= 1 &&
        std::abs(left.green() - right.green()) <= 1 &&
        std::abs(left.blue() - right.blue()) <= 1 &&
        left.alpha() == right.alpha();
  };
  if (combined.isNull() || unmasked.isNull() ||
      baseline.isNull() || primary_only.isNull() || intersection_only.isNull() ||
      !close_color(combined.pixelColor(80, 60), unmasked.pixelColor(80, 60)) ||
      !close_color(combined.pixelColor(20, 60), baseline.pixelColor(20, 60)) ||
      !close_color(combined.pixelColor(130, 60), baseline.pixelColor(130, 60))) {
    const auto pixel = [](const QImage& image, int x, int y) {
      return image.isNull() ? QStringLiteral("<null>")
                            : image.pixelColor(x, y).name(QColor::HexArgb);
    };
    std::cerr << "Intersection render diagnosis: layer="
              << QJsonDocument::fromVariant(loaded.layers.first().toMap()).toJson(QJsonDocument::Compact).constData()
              << " primary-alpha(overlap/left/right)="
              << static_cast<int>(static_cast<unsigned char>(primary_alpha[overlap_offset])) << "/"
              << static_cast<int>(static_cast<unsigned char>(primary_alpha[left_only_offset])) << "/"
              << static_cast<int>(static_cast<unsigned char>(primary_alpha[right_only_offset]))
              << " intersection-alpha(overlap/left/right)="
              << static_cast<int>(static_cast<unsigned char>(intersection_alpha[overlap_offset])) << "/"
              << static_cast<int>(static_cast<unsigned char>(intersection_alpha[left_only_offset])) << "/"
              << static_cast<int>(static_cast<unsigned char>(intersection_alpha[right_only_offset]))
              << " source(20/80/130)=" << pixel(source, 20, 60).toStdString() << "/"
              << pixel(source, 80, 60).toStdString() << "/" << pixel(source, 130, 60).toStdString()
              << " baseline=" << pixel(baseline, 20, 60).toStdString() << "/"
              << pixel(baseline, 80, 60).toStdString() << "/" << pixel(baseline, 130, 60).toStdString()
              << " combined=" << pixel(combined, 20, 60).toStdString() << "/"
              << pixel(combined, 80, 60).toStdString() << "/" << pixel(combined, 130, 60).toStdString()
              << " unmasked=" << pixel(unmasked, 20, 60).toStdString() << "/"
              << pixel(unmasked, 80, 60).toStdString() << "/" << pixel(unmasked, 130, 60).toStdString()
              << " primary-only=" << pixel(primary_only, 20, 60).toStdString() << "/"
              << pixel(primary_only, 80, 60).toStdString() << "/" << pixel(primary_only, 130, 60).toStdString()
              << " intersection-only=" << pixel(intersection_only, 20, 60).toStdString() << "/"
              << pixel(intersection_only, 80, 60).toStdString() << "/"
              << pixel(intersection_only, 130, 60).toStdString() << '\n';
  }
  require(!combined.isNull() && !unmasked.isNull() && !baseline.isNull() &&
              !primary_only.isNull() && !intersection_only.isNull() &&
              close_color(combined.pixelColor(80, 60), unmasked.pixelColor(80, 60)) &&
              close_color(combined.pixelColor(20, 60), baseline.pixelColor(20, 60)) &&
              close_color(combined.pixelColor(130, 60), baseline.pixelColor(130, 60)) &&
              !close_color(unmasked.pixelColor(20, 60), baseline.pixelColor(20, 60)) &&
              !close_color(unmasked.pixelColor(130, 60), baseline.pixelColor(130, 60)) &&
              close_color(primary_only.pixelColor(20, 60), baseline.pixelColor(20, 60)) &&
              !close_color(primary_only.pixelColor(130, 60), baseline.pixelColor(130, 60)) &&
              !close_color(intersection_only.pixelColor(20, 60), baseline.pixelColor(20, 60)) &&
              close_color(intersection_only.pixelColor(130, 60), baseline.pixelColor(130, 60)),
      "Two attached sets must intersect: adjustment is clipped to overlap, not either set's union");

  require(editor.selectLayerMask(0, intersection_id) && editor.editingLayerMask() &&
              editor.maskEditLayerId() == owner_id,
      "Intersection-domain masks must be selectable as the layer's attached mask");
  const auto before_invert = state(editor);
  require(editor.invertLayerMask(0, intersection_id), "Invert an intersection-domain mask by ID");
  settle(editor);
  const auto inverted = state(editor);
  require(string_values(inverted.layers.first().toMap(), "selectionMaskSetIds") == QStringList{primary_id} &&
              string_values(inverted.layers.first().toMap(), "selectionIntersectionMaskSetIds") ==
                  QStringList{intersection_id} && inverted.mask_stack != before_invert.mask_stack,
      "Inverting a linked intersection mask must preserve both domains and change only its source coverage");
  editor.undo(); settle(editor);
  require(state(editor).layers == before_invert.layers && state(editor).mask_stack == before_invert.mask_stack,
      "Undo restores the exact primary/intersection attachment and source coverage");
  editor.redo(); settle(editor);
  require(state(editor).layers == inverted.layers && state(editor).mask_stack == inverted.mask_stack,
      "Redo restores the exact intersection-mask inversion");

  const auto before_remove = state(editor);
  require(editor.removeLayerMask(0, intersection_id), "Remove one intersection-domain attachment");
  settle(editor);
  const auto removed = state(editor);
  const auto primary_only_owner = removed.layers.first().toMap();
  require(string_values(primary_only_owner, "selectionMaskSetIds") == QStringList{primary_id} &&
              primary_only_owner.value("selectionIntersectionMaskSetIds").toList().isEmpty() &&
              primary_only_owner.value("selectionMaskSetCount").toInt() == 1 &&
              primary_only_owner.value("selectionScoped").toBool() && primary_only_owner.value("maskEnabled").toBool(),
      "Removing an intersection ref must preserve the primary ref, scoped status, count, and bypass setting");
  editor.undo(); settle(editor);
  require(state(editor).layers == before_remove.layers && state(editor).mask_stack == before_remove.mask_stack,
      "Undo restores the removed intersection attachment without flattening its domain");
  editor.redo(); settle(editor);
  require(state(editor).layers == removed.layers && state(editor).mask_stack == removed.mask_stack,
      "Redo restores the exact removal");
  editor.undo(); settle(editor);

  require(editor.addAdjustmentLayer("Intersection move destination"), "Create a move destination");
  const auto move_destination_id = state(editor).layers.first().toMap().value("id").toString();
  const auto source_mask_layer_index = layer_index(editor, owner_id);
  require(editor.selectLayerMask(source_mask_layer_index, intersection_id) &&
              editor.editingLayerMask() && editor.maskEditLayerId() == owner_id,
      "Select the source intersection mask before moving its attachment");
  const auto active_set_id = [&editor]() {
    const auto active_index = editor.activeMaskIndex();
    const auto stack = editor.maskStack();
    return active_index >= 0 && active_index < static_cast<int>(stack.size())
        ? stack.at(active_index).toMap().value("maskSetId").toString()
        : QString();
  };
  require(active_set_id() == intersection_id,
      "The source intersection mask must be the active inspected set before transfer");
  const auto before_move = state(editor);
  require(editor.transferLayerMask(owner_id, intersection_id, move_destination_id, false),
      "Move an intersection-domain mask to another layer");
  settle(editor);
  const auto moved = state(editor);
  const auto moved_source = moved.layers.at(layer_index(editor, owner_id)).toMap();
  const auto moved_destination = moved.layers.at(layer_index(editor, move_destination_id)).toMap();
  require(string_values(moved_source, "selectionMaskSetIds") == QStringList{primary_id} &&
              moved_source.value("selectionIntersectionMaskSetIds").toList().isEmpty() &&
              moved_destination.value("selectionMaskSetIds").toList().isEmpty() &&
              string_values(moved_destination, "selectionIntersectionMaskSetIds") == QStringList{intersection_id} &&
              moved_destination.value("selectionScoped").toBool() &&
              moved_destination.value("selectionMaskSetCount").toInt() == 1 &&
              moved_destination.value("maskEnabled").toBool() && editor.editingLayerMask() &&
              editor.maskEditLayerId() == move_destination_id && active_set_id() == intersection_id,
      "Moving an intersection attachment must preserve its AND domain, including on an intersection-only owner");
  editor.undo(); settle(editor);
  require(state(editor).layers == before_move.layers && state(editor).mask_stack == before_move.mask_stack &&
              editor.editingLayerMask() && editor.maskEditLayerId() == owner_id &&
              active_set_id() == intersection_id,
      "Undo restores the source attachment and its inspected intersection target");
  editor.redo(); settle(editor);
  require(state(editor).layers == moved.layers && state(editor).mask_stack == moved.mask_stack &&
              editor.editingLayerMask() && editor.maskEditLayerId() == move_destination_id &&
              active_set_id() == intersection_id,
      "Redo restores the destination attachment and its inspected intersection target");

  const auto before_intersection_only_invert = state(editor);
  const auto move_destination_index = layer_index(editor, move_destination_id);
  require(editor.invertLayerMask(move_destination_index, intersection_id),
      "Invert a mask attached only through the intersection domain");
  settle(editor);
  const auto intersection_only_inverted = state(editor);
  const auto intersection_only_owner =
      intersection_only_inverted.layers.at(layer_index(editor, move_destination_id)).toMap();
  require(string_values(intersection_only_owner, "selectionMaskSetIds").isEmpty() &&
              intersection_only_owner.value("selectionMaskSetId").toString().isEmpty() &&
              string_values(intersection_only_owner, "selectionIntersectionMaskSetIds") ==
                  QStringList{intersection_id} && intersection_only_owner.value("selectionScoped").toBool() &&
              intersection_only_owner.value("selectionMaskSetCount").toInt() == 1,
      "Inverting an intersection-only attachment must not migrate it into the primary domain");
  editor.undo(); settle(editor);
  require(state(editor).layers == before_intersection_only_invert.layers &&
              state(editor).mask_stack == before_intersection_only_invert.mask_stack,
      "Undo restores the intersection-only source and its domain");
  editor.redo(); settle(editor);
  require(state(editor).layers == intersection_only_inverted.layers &&
              state(editor).mask_stack == intersection_only_inverted.mask_stack,
      "Redo keeps the inverted source in the intersection-only domain");

  require(editor.addAdjustmentLayer("Intersection copy destination"), "Create a copy destination");
  const auto copy_destination_id = state(editor).layers.first().toMap().value("id").toString();
  require(editor.transferLayerMask(move_destination_id, intersection_id, copy_destination_id, true),
      "Copy an intersection-domain mask to another layer");
  settle(editor);
  const auto copied = state(editor);
  const auto copy_owner = copied.layers.at(layer_index(editor, copy_destination_id)).toMap();
  const auto copied_intersection_ids = string_values(copy_owner, "selectionIntersectionMaskSetIds");
  require(copied_intersection_ids.size() == 1 && copied_intersection_ids.first() != intersection_id &&
              string_values(copy_owner, "selectionMaskSetIds").isEmpty() &&
              copy_owner.value("selectionScoped").toBool() && copy_owner.value("maskEnabled").toBool() &&
              alpha(editor, source, copied_intersection_ids.first()) == alpha(editor, source, intersection_id) &&
              string_values(copied.layers.at(layer_index(editor, move_destination_id)).toMap(),
                  "selectionIntersectionMaskSetIds") == QStringList{intersection_id},
      "Copying an intersection ref creates independent source identity and keeps both layers in the AND domain");
  const auto copy_layer_index = layer_index(editor, copy_destination_id);
  require(editor.setPhotoLayerMaskEnabled(copy_layer_index, false),
      "Bypass the copied intersection-only layer mask");
  const auto bypassed_copy_state = state(editor);
  require(!bypassed_copy_state.layers.at(layer_index(editor, copy_destination_id))
               .toMap().value("maskEnabled").toBool() &&
              editor.applyWorkingSelectionToLayer(copy_layer_index),
      "Append a primary mask to an intersection-only layer while the combined mask is bypassed");
  const auto appended_primary = state(editor).layers.at(layer_index(editor, copy_destination_id)).toMap();
  const auto appended_state = state(editor);
  require(string_values(appended_primary, "selectionMaskSetIds").size() == 1 &&
              string_values(appended_primary, "selectionIntersectionMaskSetIds") == copied_intersection_ids &&
              appended_primary.value("selectionMaskSetCount").toInt() == 2 &&
              appended_primary.value("selectionScoped").toBool() &&
              !appended_primary.value("maskEnabled").toBool(),
      "Adding a primary ref must retain the intersection domain, total count, and existing bypass state");
  editor.undo(); settle(editor);
  require(state(editor).layers == bypassed_copy_state.layers &&
              state(editor).mask_stack == bypassed_copy_state.mask_stack,
      "Undo removes the appended primary ref while retaining the bypassed intersection attachment");
  editor.redo(); settle(editor);
  require(state(editor).layers == appended_state.layers && state(editor).mask_stack == appended_state.mask_stack,
      "Redo restores the appended primary ref without flattening the intersection domain");

  require(editor.setPhotoLayerLocked(copy_layer_index, true), "Lock the linked-mask copy owner");
  const auto locked = state(editor);
  require(!editor.removeLayerMask(copy_layer_index, copied_intersection_ids.first()) &&
              !editor.invertLayerMask(copy_layer_index, copied_intersection_ids.first()) &&
              !editor.transferLayerMask(copy_destination_id, copied_intersection_ids.first(), move_destination_id, false) &&
              state(editor).layers == locked.layers && state(editor).mask_stack == locked.mask_stack,
      "A locked owner rejects intersection remove, invert, and move without changing coverage");
  editor.undo(); settle(editor);
  require(!state(editor).layers.at(layer_index(editor, copy_destination_id)).toMap().value("locked").toBool(),
      "Rejected intersection mutations add no history ahead of the explicit lock change");
}
}
int main(int argc, char** argv) {
  QTemporaryDir runtime;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath("app").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  try {
    QImage source(160, 120, QImage::Format_RGB32); source.fill(QColor("#788490"));
    const auto path = runtime.filePath("photo.png"); require(source.save(path), "Save fixture");
    if (qEnvironmentVariableIsSet("LOOKSY_EDGE_REFINEMENT_ONLY")) {
      verify_edge_adjustment_pixels(path, source);
      return 0;
    }
    verify_mask_render_consumers();
    verify_free_selection_rendering(path, source);
    verify_working_outline_cache(path);
    if (qEnvironmentVariableIsSet("LOOKSY_OUTLINE_BASELINE_ONLY")) return 0;
    verify_explicit_selection_adjustment(path, source);
    verify_edge_adjustment_pixels(path, source);
    EditorPreviewController editor; editor.loadAsset("selection-mask-workflow", path, {}, {}, {}, {}, {}); settle(editor);
    editor.addMask("rectangle"); editor.setActiveMaskFeather(0);
    editor.setActiveMaskCenterX(0.25); editor.setActiveMaskCenterY(0.5);
    editor.setActiveMaskRadiusX(0.25); editor.setActiveMaskRadiusY(0.5); settle(editor);
    require(editor.photoLayerCount() == 0 && editor.hasWorkingSelection(), "Creating a selection must not create an adjustment layer");
    const auto selection_id = editor.workingSelectionSetId();
    const auto original_selection = alpha(editor, source, selection_id);
    require(editor.addAdjustmentLayer("Color"), "Explicitly create adjustment layer");
    const auto adjustment_id = state(editor).layers[0].toMap().value("id").toString();
    require(editor.applyWorkingSelectionToLayer(0), "Attach selection to adjustment layer");
    const auto adjustment_mask = state(editor).layers[0].toMap().value("selectionMaskSetId").toString();
    require(!adjustment_mask.isEmpty() && adjustment_mask != selection_id && editor.workingSelectionSetId() == selection_id,
        "Attached mask and working selection must be independent identities");
    require(editor.setPhotoLayerAdjustmentValue(0, "exposure", 0.8), "Edit masked adjustment layer colors");
    const auto before_bypass = state(editor);
    require(editor.setPhotoLayerMaskEnabled(0, false), "Disable the layer mask without removing it");
    const auto bypassed = state(editor);
    require(bypassed.layers[0].toMap().value("selectionMaskSetId").toString() == adjustment_mask &&
        bypassed.mask_stack == before_bypass.mask_stack, "Bypassing preserves attached mask identity and coverage");
    const auto bypass_image = render_output_image_with_backend(source, bypassed, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    const auto masked_image = render_output_image_with_backend(source, before_bypass, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    require(bypass_image.pixelColor(110, 60) != masked_image.pixelColor(110, 60), "Disabled mask bypasses clipping in the rendered adjustment");
    const auto bypass_reopened = parse_edit_recipe_json(serialize_edit_recipe_json(bypassed));
    require(bypass_reopened.layers[0].toMap().value("maskEnabled") == false, "Mask bypass survives recipe reopen");
    require(editor.setPhotoLayerMaskEnabled(0, true), "Enable the same mask again");
    require(state(editor).layers[0].toMap().value("selectionMaskSetId").toString() == adjustment_mask,
        "Changing adjustment colors must retain the attached mask");
    require(editor.selectLayerMask(0, adjustment_mask), "Click mask thumbnail targets the layer mask");
    require(editor.editingLayerMask() && editor.maskEditLayerId() == adjustment_id && editor.workingSelectionSetId() == selection_id,
        "Inspecting a layer mask cannot replace the working selection");
    const auto before_hide = state(editor); const auto before_mask = alpha(editor, source, adjustment_mask);
    stroke(editor, 0, 40.5, 60.5, true);
    const auto after_hide = state(editor); const auto hidden_mask = alpha(editor, source, adjustment_mask);
    const auto before_image = render_output_image_with_backend(source, before_hide, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    const auto after_image = render_output_image_with_backend(source, after_hide, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    require(!before_image.isNull() && !after_image.isNull() && before_image.pixelColor(40, 60) != after_image.pixelColor(40, 60) &&
        before_image.pixelColor(110, 60) == after_image.pixelColor(110, 60), "Mask paint changes the adjustment result inside the selection and preserves outside pixels");
    require(hidden_mask[60 * 160 + 40] == '\0' && static_cast<unsigned char>(before_mask[60 * 160 + 40]) > 240,
        "Hide brush must edit mask coverage on an adjustment layer");
    require(after_hide.layers == before_hide.layers && alpha(editor, source, selection_id) == original_selection,
        "Mask painting must preserve every layer's pixels/adjustments and the working selection");
    for (qsizetype i = 0; i < hidden_mask.size(); ++i)
      if (original_selection[i] == '\0') require(hidden_mask[i] == before_mask[i], "Working selection protects outside mask coverage");
    for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x)
      if (std::hypot(x - 40.0, y - 60.0) > 12.0)
        require(hidden_mask[y * source.width() + x] == before_mask[y * source.width() + x],
            "Painting must preserve all soft mask coverage outside the brush footprint");
    editor.undo(); settle(editor);
    require(alpha(editor, source, adjustment_mask) == before_mask && editor.editingLayerMask(), "Undo restores mask pixels and keeps mask target");
    editor.redo(); settle(editor); require(alpha(editor, source, adjustment_mask) == hidden_mask, "Redo restores mask edit");
    stroke(editor, 0, 40.5, 60.5, false);
    require(static_cast<unsigned char>(alpha(editor, source, adjustment_mask)[60 * 160 + 40]) > 240,
        "Reveal brush restores hidden coverage without painting image pixels");
    require(editor.deselectWorkingSelection() && !editor.hasWorkingSelection() && editor.editingLayerMask(), "Deselect keeps layer-mask editing active");
    require(editor.reselectWorkingSelection() && editor.workingSelectionSetId() == selection_id, "Reselect restores the independent selection");
    require(editor.invertLayerMask(0, adjustment_mask) && alpha(editor, source, selection_id) == original_selection,
        "Invert Mask must leave the working selection unchanged");
    require(editor.invertLayerMask(0, adjustment_mask), "Restore mask orientation");
    const auto attached_before_invert = masks(editor, adjustment_mask);
    require(editor.invertActiveSelection() && masks(editor, adjustment_mask) == attached_before_invert,
        "Invert changes working selection, not the inspected layer mask");
    require(editor.invertActiveSelection(), "Restore selection orientation");
    require(editor.saveWorkingSelection("Portrait") && editor.savedSelections().size() == 1, "Save a named selection");
    editor.finishLayerMaskEditing(); require(!editor.editingLayerMask(), "Image thumbnail returns to pixel targeting");
    require(editor.addPaintLayer(), "Create pixel layer");
    const auto pixel_id = state(editor).layers[0].toMap().value("id").toString();
    require(editor.transferLayerMask(adjustment_id, adjustment_mask, pixel_id), "Drag a mask from adjustment to pixel layer");
    require(!state(editor).layers[layer_index(editor, adjustment_id)].toMap().value("selectionScoped").toBool() &&
        state(editor).layers[layer_index(editor, pixel_id)].toMap().value("selectionMaskSetId").toString() == adjustment_mask,
        "Move detaches from source and attaches to destination without changing mask identity");
    editor.undo(); settle(editor);
    require(state(editor).layers[layer_index(editor, adjustment_id)].toMap().value("selectionMaskSetId").toString() == adjustment_mask,
        "One undo restores the entire mask transfer");
    editor.redo(); settle(editor);
    require(editor.transferLayerMask(pixel_id, adjustment_mask, adjustment_id, true), "Alt-drag copies mask independently");
    const auto copied_mask = state(editor).layers[layer_index(editor, adjustment_id)].toMap().value("selectionMaskSetId").toString();
    require(copied_mask != adjustment_mask && alpha(editor, source, copied_mask) == alpha(editor, source, adjustment_mask),
        "Copied mask has independent identity and identical alpha");
    const auto transferred = state(editor);
    require(editor.setPhotoLayerLocked(layer_index(editor, pixel_id), true), "Lock destination");
    require(!editor.transferLayerMask(adjustment_id, copied_mask, pixel_id), "Locked destination must reject a mask drop");
    editor.undo(); settle(editor); require(state(editor).layers == transferred.layers, "Rejected drop must not mutate document");
    const auto paint_index = layer_index(editor, pixel_id);
    stroke(editor, paint_index, 40.5, 60.5, false);
    const auto painted_pixels = rasterize_photo_paint_layer(state(editor).layers[paint_index].toMap());
    require(editor.setPhotoLayerAlphaLocked(paint_index, true), "Lock layer transparency");
    require(!editor.beginPhotoPaintStroke(paint_index, {{"erase", true}}), "Transparency lock rejects destructive erasing");
    require(editor.beginPhotoPaintStroke(paint_index, {{"color", "#ff7040"}, {"size", 80}, {"hardness", 1.0}, {"pressureSize", false}}), "Paint color through transparency lock");
    editor.addPhotoPaintPoint(40.5, 60.5); editor.finishPhotoPaintStroke(); settle(editor);
    const auto recolored = rasterize_photo_paint_layer(state(editor).layers[paint_index].toMap());
    for (int y = 0; y < recolored.height(); ++y) for (int x = 0; x < recolored.width(); ++x)
      require(recolored.pixelColor(x, y).alpha() == painted_pixels.pixelColor(x, y).alpha(), "Transparency lock preserves every existing alpha value");
    require(recolored.pixelColor(40, 60).red() > painted_pixels.pixelColor(40, 60).red(), "Transparency lock still permits recoloring opaque pixels");
    for (const auto& kind : {QString("fill"), QString("text"), QString("group")}) {
      if (kind == "fill") require(editor.addFillLayer("#22aa88", "Fill"), "Create fill");
      else if (kind == "text") require(editor.addTextLayer("Type", "Text"), "Create text");
      else require(editor.addLayerGroup("Group"), "Create group");
      require(editor.applyWorkingSelectionToLayer(0), "Any unlocked supported layer can receive a layer mask");
    }
    const auto saved_id = editor.savedSelections().first().toMap().value("id").toString();
    const auto saved_alpha = alpha(editor, source, saved_id);
    const auto before_transform_layers = state(editor).layers;
    require(editor.transformWorkingSelection({{"x", 0.1}, {"y", 0.0}, {"scaleX", 1.0}, {"scaleY", 1.0}}), "Transform selection outline"); settle(editor);
    require(state(editor).layers == before_transform_layers && alpha(editor, source, selection_id) != original_selection,
        "Transforming selection must not move or change layer content");
    require(alpha(editor, source, saved_id) == saved_alpha, "Saved selection is an immutable snapshot of its saved coverage");
    editor.undo(); settle(editor); require(alpha(editor, source, selection_id) == original_selection, "Undo selection transform");
    const auto current = state(editor);
    const RecipeAssetStore assets(runtime.filePath("recipe-assets"));
    const auto canonical = serialize_desktop_recipe_as_canonical_v13_with_assets(current,
        {.recipe_id = "mask-workflow", .source_id = "selection-mask-workflow", .decoder_provenance = "raster:test-v1", .raw_source = false}, assets);
    if (!canonical.ok()) for (const auto& error : canonical.errors) std::cerr << error.path << ": " << error.message << '\n';
    require(canonical.ok(), "Save canonical recipe with immutable mask assets and selection metadata");
    const auto projected = project_canonical_v13_document_to_desktop_state_with_assets(canonical.document, assets);
    require(projected.ok() && projected.state->selection_state == current.selection_state,
        "Canonical recipe provenance must preserve independent selection metadata");
    require(projected.state->layers[layer_index(editor, pixel_id)].toMap().value("alphaLocked").toBool(),
        "Canonical save preserves transparency lock");
    EditorPreviewController reopened; reopened.loadAsset("reopened-selection-mask", path, {}, {}, {}, {}, serialize_edit_recipe_json(current)); settle(reopened);
    const auto reopened_image = render_output_image_with_backend(source, state(reopened), QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    const auto saved_image = render_output_image_with_backend(source, current, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    require(!saved_image.isNull() && reopened_image == saved_image, "Save/reopen preserves flattened output pixels exactly");
    if (reopened.workingSelectionSetId() != selection_id || reopened.savedSelections().size() != 1 || state(reopened).layers != current.layers) {
      std::cerr << "reopen selection=" << reopened.workingSelectionSetId().toStdString() << " expected=" << selection_id.toStdString()
          << " saved=" << reopened.savedSelections().size() << " layers_equal=" << (state(reopened).layers == current.layers)
          << " metadata=" << QJsonDocument::fromVariant(current.selection_state).toJson(QJsonDocument::Compact).constData()
          << " reopened_metadata=" << QJsonDocument::fromVariant(state(reopened).selection_state).toJson(QJsonDocument::Compact).constData() << '\n';
    }
    require(reopened.workingSelectionSetId() == selection_id && reopened.savedSelections().size() == 1 && state(reopened).layers == current.layers,
        "Reopen must preserve working selection, saved names, masks and layer adjustments");
    const auto copied_before_load = masks(editor, copied_mask);
    const auto incoming_alpha = alpha(editor, source, copied_mask);
    const auto before_subtract_alpha = alpha(editor, source, selection_id);
    require(editor.loadMaskAsSelection(copied_mask, "subtract"), "Subtract a saved layer mask from working selection"); settle(editor);
    require(editor.workingSelectionSetId() == selection_id && masks(editor, copied_mask) == copied_before_load,
        "Loading a mask refines selection without modifying the original layer mask");
    const auto subtracted_alpha = alpha(editor, source, selection_id);
    for (qsizetype i = 0; i < subtracted_alpha.size(); ++i) {
      const auto expected = std::max(0, static_cast<unsigned char>(before_subtract_alpha[i]) - static_cast<unsigned char>(incoming_alpha[i]));
      if (std::abs(static_cast<int>(static_cast<unsigned char>(subtracted_alpha[i])) - expected) > 1) {
        std::cerr << "Subtract pixel " << i << ": before=" << static_cast<int>(static_cast<unsigned char>(before_subtract_alpha[i]))
            << " incoming=" << static_cast<int>(static_cast<unsigned char>(incoming_alpha[i]))
            << " actual=" << static_cast<int>(static_cast<unsigned char>(subtracted_alpha[i])) << " expected=" << expected << '\n';
        require(false, "Subtract must produce the composed coverage without unioning the original back in");
      }
    }
    for (const auto& operation : {QStringLiteral("add"), QStringLiteral("intersect")}) {
      editor.undo(); settle(editor);
      require(alpha(editor, source, selection_id) == before_subtract_alpha, "Undo restores the exact pre-combination selection");
      require(editor.loadMaskAsSelection(copied_mask, operation), "Combine another mask with the working selection"); settle(editor);
      const auto combined = alpha(editor, source, selection_id);
      for (qsizetype i = 0; i < combined.size(); ++i) {
        const int left = static_cast<unsigned char>(before_subtract_alpha[i]), right = static_cast<unsigned char>(incoming_alpha[i]);
        const int expected = operation == "add" ? qRound(left + right - left * right / 255.0) : qRound(left * right / 255.0);
        require(std::abs(static_cast<int>(static_cast<unsigned char>(combined[i])) - expected) <= 1,
            "Add and Intersect preserve the exact soft coverage algebra");
      }
    }
    {
      EditState base_state; base_state.mask_stack = masks(editor, selection_id);
      base_state.selection_state = {{"currentSetId", selection_id}, {"initialized", true}};
      EditorPreviewController base; base.loadAsset("base-mask", path, {}, {}, {}, {}, serialize_edit_recipe_json(base_state)); settle(base);
      // Use a fresh opaque rectangle for a clear Base Image visibility assertion.
      require(base.deselectWorkingSelection(), "Deselect the algebra fixture");
      base.addMask("rectangle"); base.setActiveMaskFeather(0);
      base.setActiveMaskCenterX(0.25); base.setActiveMaskCenterY(0.5);
      base.setActiveMaskRadiusX(0.25); base.setActiveMaskRadiusY(0.5);
      require(base.setBaseImageLocked(false) && base.applyWorkingSelectionToLayer(0), "Unlocked Base Image accepts a mask"); settle(base);
      const auto base_mask = state(base).layers[0].toMap().value("selectionMaskSetId").toString();
      const auto masked = render_output_image_with_backend(source, state(base), QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      require(masked.pixelColor(40, 60).alpha() > 250 && masked.pixelColor(110, 60).alpha() == 0,
          "Base Image mask controls final visibility");
      stroke(base, -1, 40.5, 60.5, true);
      require(base.selectLayerMask(0, base_mask), "Target the Base Image mask");
      stroke(base, 0, 40.5, 60.5, false);
      const auto after_reveal = render_output_image_with_backend(source, state(base), QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      require(after_reveal.pixelColor(40, 60).alpha() == 0 && after_reveal.pixelColor(60, 60).alpha() > 250,
          "Revealing the Base Image mask cannot resurrect erased source pixels");
    }
    verify_intersection_mask_attachments(path, source);
    std::cout << "Independent selection, zero automatic layers, adjustment/pixel/text/fill/group masks, hide/reveal clipping, target-preserving undo, move/copy/drop locks, named selection, transform and reopen passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
