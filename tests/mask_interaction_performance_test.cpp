#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>

#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QElapsedTimer>
#include <QMetaProperty>
#include <QMetaType>
#include <QTemporaryDir>
#include <QThread>
#include <QSet>
#include <QRgb>
#include <QStringList>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>

#include "edit_recipe_mask_schema.h"
#include "editor_preview_controller.h"
#include "thumbnail_presentation_service.h"

namespace photo_editor::desktop {

struct MaskInteractionPerformanceTestAccess final {
  static int active_brush_live_point_count(const EditorPreviewController& controller) {
    return static_cast<int>(controller.active_brush_live_points_.size());
  }
  static int pending_brush_commit_preview_point_count(const EditorPreviewController& controller) {
    return static_cast<int>(controller.pending_brush_commit_preview_points_.size());
  }
  static QString asset_id(const EditorPreviewController& controller) {
    return controller.asset_id_;
  }

  static bool color_preview_configured(const EditorPreviewController& controller) {
    return controller.color_tool_mask_preview_enabled_ &&
        controller.color_replacement_enabled_ &&
        controller.color_replacement_strength_ > 0.0;
  }

  static int outline_hits(const EditorPreviewController& controller) {
    return controller.mask_session_.outlineCacheHitCount();
  }
  static int outline_misses(const EditorPreviewController& controller) {
    return controller.mask_session_.outlineCacheMissCount();
  }
  static QString color_preview_key(const EditorPreviewController& controller) {
    return controller.colorToolMaskPreviewCacheKey(controller.currentState());
  }
  static QVariantMap color_preview_diagnostics(const EditorPreviewController& controller) {
    const auto state = controller.currentState();
    return {
        {QStringLiteral("assetId"), controller.asset_id_},
        {QStringLiteral("sourcePath"), controller.file_path_},
        {QStringLiteral("previewEnabled"), controller.color_tool_mask_preview_enabled_},
        {QStringLiteral("replacementEnabled"), controller.color_replacement_enabled_},
        {QStringLiteral("sourceHue"), controller.color_replacement_source_hue_},
        {QStringLiteral("strength"), controller.color_replacement_strength_},
        {QStringLiteral("range"), controller.color_replacement_range_},
        {QStringLiteral("cropX"), state.crop_rect_x},
        {QStringLiteral("cropY"), state.crop_rect_y},
        {QStringLiteral("cropWidth"), state.crop_rect_width},
        {QStringLiteral("cropHeight"), state.crop_rect_height},
        {QStringLiteral("cropZoom"), state.crop_zoom},
        {QStringLiteral("exposure"), state.exposure},
        {QStringLiteral("temperature"), state.temperature},
        {QStringLiteral("tint"), state.tint},
        {QStringLiteral("busy"), controller.color_tool_mask_preview_active_ ||
                                       controller.pending_color_tool_mask_preview_request_.has_value()},
        {QStringLiteral("requestedKey"), controller.color_tool_mask_preview_requested_key_},
        {QStringLiteral("currentKey"), controller.colorToolMaskPreviewCacheKey(state)},
    };
  }
  static bool has_color_preview(const EditorPreviewController& controller, const QString& key) {
    QString source;
    return controller.mask_session_.colorToolPreviewLookup(key, &source);
  }
};

}  // namespace photo_editor::desktop

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool outline_misses_after(
    photo_editor::desktop::EditorPreviewController& controller,
    int previous_misses) {
  (void)controller.activeMaskOutlinePaths();
  return photo_editor::desktop::MaskInteractionPerformanceTestAccess::outline_misses(
             controller) == previous_misses + 1;
}

QString await_color_mask_preview(photo_editor::desktop::EditorPreviewController& controller) {
  QElapsedTimer timeout;
  timeout.start();
  while (timeout.elapsed() < 5000) {
    const auto source = controller.colorToolMaskPreviewSource();
    if (!source.isEmpty()) return source;
    QApplication::processEvents();
    QThread::msleep(1);
  }
  return {};
}

bool has_mask_coverage(const QImage& image) {
  if (image.isNull()) return false;
  for (int y = 0; y < image.height(); y += 17) {
    for (int x = 0; x < image.width(); x += 17) {
      if (image.pixelColor(x, y).alpha() > 0) return true;
    }
  }
  return false;
}

QVariant canonical_mask_snapshot_value(QVariant value) {
  static const QSet<QString> transient_thumbnail_fields{
      QStringLiteral("thumbnailQueueContract"),
      QStringLiteral("thumbnailQueueDeferBlockerSignature"),
      QStringLiteral("thumbnailQueueDeferBlockers"),
      QStringLiteral("thumbnailQueueGeneration"),
      QStringLiteral("thumbnailQueueJobId"),
      QStringLiteral("thumbnailQueueStateSignature"),
      QStringLiteral("thumbnailStaleCurrentRecipeHash"),
      QStringLiteral("thumbnailStaleDetectionSignature"),
  };
  if (value.typeId() == QMetaType::QVariantMap) {
    auto map = value.toMap();
    for (const auto& key : transient_thumbnail_fields)
      map.remove(key);
    for (auto it = map.begin(); it != map.end(); ++it)
      it.value() = canonical_mask_snapshot_value(it.value());
    return map;
  }
  if (value.typeId() == QMetaType::QVariantList) {
    auto list = value.toList();
    for (auto& item : list)
      item = canonical_mask_snapshot_value(item);
    return list;
  }
  return value;
}

QVariantList canonical_mask_snapshot(const QVariantList& rows) {
  return canonical_mask_snapshot_value(QVariant::fromValue(rows)).toList();
}

struct MaskCoverageStats final {
  qsizetype nontransparent_pixels = 0;
  int maximum_alpha = 0;
};

MaskCoverageStats mask_coverage_stats(const QImage& image) {
  MaskCoverageStats stats;
  if (image.isNull()) return stats;
  for (int y = 0; y < image.height(); ++y) {
    const auto* scanline = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      const auto alpha = qAlpha(scanline[x]);
      if (alpha > 0) ++stats.nontransparent_pixels;
      stats.maximum_alpha = std::max(stats.maximum_alpha, alpha);
    }
  }
  return stats;
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir temporary_directory;
  if (!temporary_directory.isValid()) {
    return fail("mask interaction fixture directory should be available");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", temporary_directory.path().toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temporary_directory.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/photo_editor_offline_test");
  QApplication application(argc, argv);
  using namespace photo_editor::desktop;

  const auto source_path = QDir(temporary_directory.path()).filePath(QStringLiteral("landscape.png"));
  QImage landscape(512, 341, QImage::Format_RGB32);
  landscape.fill(QColor(255, 24, 16));
  if (!landscape.save(source_path, "PNG")) {
    return fail("mask interaction landscape source should save");
  }

  QVariantMap selection_candidate{
      {QStringLiteral("id"), QStringLiteral("selection-source")},
      {QStringLiteral("kind"), QStringLiteral("brush")},
      {QStringLiteral("toolRole"), QStringLiteral("selection")},
      {QStringLiteral("maskSetId"), QStringLiteral("selection-set")},
      {QStringLiteral("maskSetName"), QStringLiteral("Selection")},
      {QStringLiteral("brushPoints"), QVariantList{QVariantMap{{QStringLiteral("x"), 0.25}}}},
      {QStringLiteral("brushStrokes"), QVariantList{QVariantMap{{QStringLiteral("points"), QVariantList{0.25, 0.5}}}}},
      {QStringLiteral("segmentationWidth"), 2},
      {QStringLiteral("segmentationHeight"), 2},
      {QStringLiteral("segmentationData"), QByteArray(4, '\x7f')},
  };
  const auto canonical_selection = normalized_mask_definition(selection_candidate, 1);
  auto retouch_candidate = selection_candidate;
  retouch_candidate.insert(QStringLiteral("id"), QStringLiteral("retouch-source"));
  retouch_candidate.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  retouch_candidate.insert(QStringLiteral("retouchMode"), QStringLiteral("heal"));
  const auto canonical_retouch = normalized_mask_definition(retouch_candidate, 2);
  const ThumbnailPresentationService presentation_service;
  const auto grouped_sets = presentation_service.groupMaskSets(
      {canonical_selection, canonical_retouch}, true);
  const auto grouped_retouch = presentation_service.groupRetouchStack(
      {canonical_selection, canonical_retouch}, true);
  if (grouped_sets.size() != 1 || grouped_retouch.size() != 1 ||
      grouped_sets.constFirst().toMap().value(QStringLiteral("sources")).toList().size() != 1 ||
      grouped_sets.constFirst().toMap().value(QStringLiteral("sources")).toList()
              .constFirst().toMap().value(QStringLiteral("segmentationData")).toByteArray() !=
          canonical_selection.value(QStringLiteral("segmentationData")).toByteArray() ||
      grouped_retouch.constFirst().toMap().value(QStringLiteral("id")).toString() !=
          QStringLiteral("retouch-source")) {
    return fail("canonical presentation should preserve selection alpha and filter retouch roles");
  }

  EditorPreviewController controller;
  controller.setEditAccessPolicy([] { return true; });
  controller.loadAsset(
      QStringLiteral("mask-preview-landscape"), source_path, {}, {}, {}, {}, QStringLiteral("{}"));

  EditorPreviewController stroke_state_controller;
  stroke_state_controller.setEditAccessPolicy([] { return true; });
  stroke_state_controller.loadAsset(
      QStringLiteral("stroke-state-first-asset"),
      source_path,
      {}, {}, {}, {}, QStringLiteral("{}"));
  auto* stroke_meta = stroke_state_controller.metaObject();
  const auto stroke_property_index = stroke_meta->indexOfProperty("brushStrokeActive");
  if (stroke_property_index < 0 ||
      stroke_meta->property(stroke_property_index).notifySignal().name() !=
          QByteArrayLiteral("brushStrokeActiveChanged")) {
    return fail("brushStrokeActive should use its dedicated boolean-state notifier");
  }
  int brush_state_notifications = 0;
  int broad_brush_notifications = 0;
  enum class ExpectedStrokeTransition { Begin, AssetReset, Cancel, End };
  auto expected_stroke_transition = ExpectedStrokeTransition::Begin;
  int expected_pending_commit_points = 0;
  bool stroke_transition_observed_coherently = true;
  QObject::connect(
      &stroke_state_controller,
      &EditorPreviewController::brushStrokeActiveChanged,
      &stroke_state_controller,
      [&] {
        ++brush_state_notifications;
        if (stroke_state_controller.brushStrokeActive()) {
          stroke_transition_observed_coherently =
              stroke_transition_observed_coherently &&
              expected_stroke_transition == ExpectedStrokeTransition::Begin &&
              MaskInteractionPerformanceTestAccess::active_brush_live_point_count(
                  stroke_state_controller) == 0 &&
              MaskInteractionPerformanceTestAccess::pending_brush_commit_preview_point_count(
                  stroke_state_controller) == 0;
          return;
        }
        const auto active_points =
            MaskInteractionPerformanceTestAccess::active_brush_live_point_count(
                stroke_state_controller);
        const auto pending_points =
            MaskInteractionPerformanceTestAccess::pending_brush_commit_preview_point_count(
                stroke_state_controller);
        switch (expected_stroke_transition) {
          case ExpectedStrokeTransition::AssetReset:
            stroke_transition_observed_coherently =
                stroke_transition_observed_coherently && active_points == 0 &&
                pending_points == 0 &&
                MaskInteractionPerformanceTestAccess::asset_id(stroke_state_controller) ==
                    QStringLiteral("stroke-state-second-asset");
            break;
          case ExpectedStrokeTransition::Cancel:
            stroke_transition_observed_coherently =
                stroke_transition_observed_coherently && active_points == 0 &&
                pending_points == 0;
            break;
          case ExpectedStrokeTransition::End:
            stroke_transition_observed_coherently =
                stroke_transition_observed_coherently && active_points == 0 &&
                pending_points == expected_pending_commit_points;
            break;
          case ExpectedStrokeTransition::Begin:
            stroke_transition_observed_coherently = false;
            break;
        }
      });
  QObject::connect(
      &stroke_state_controller,
      &EditorPreviewController::activeBrushStrokeChanged,
      &stroke_state_controller,
      [&broad_brush_notifications] { ++broad_brush_notifications; });

  stroke_state_controller.startSelectionBrush(false);
  stroke_state_controller.beginActiveBrushStroke();
  if (!stroke_state_controller.brushStrokeActive() || brush_state_notifications != 1) {
    return fail("beginning a brush stroke should notify the boolean transition once");
  }
  const auto broad_notifications_before_points = broad_brush_notifications;
  stroke_state_controller.addBrushPoint(0.2, 0.3);
  stroke_state_controller.addBrushPoint(0.4, 0.5);
  if (!stroke_state_controller.brushStrokeActive() || brush_state_notifications != 1 ||
      broad_brush_notifications < broad_notifications_before_points + 2) {
    return fail("brush point updates should notify live stroke consumers but not boolean state");
  }

  expected_stroke_transition = ExpectedStrokeTransition::AssetReset;
  stroke_state_controller.loadAsset(
      QStringLiteral("stroke-state-second-asset"), source_path, {}, {}, {}, {}, QStringLiteral("{}"));
  if (stroke_state_controller.brushStrokeActive() || brush_state_notifications != 2) {
    return fail("switching assets should publish the active-to-inactive brush transition once");
  }

  stroke_state_controller.startSelectionBrush(false);
  stroke_state_controller.beginCoalescedEdit();
  expected_stroke_transition = ExpectedStrokeTransition::Begin;
  stroke_state_controller.beginActiveBrushStroke();
  if (!stroke_state_controller.brushStrokeActive() || brush_state_notifications != 3) {
    return fail("a new brush stroke after asset replacement should notify its transition");
  }
  expected_stroke_transition = ExpectedStrokeTransition::Cancel;
  if (!stroke_state_controller.cancelCoalescedEdit() ||
      stroke_state_controller.brushStrokeActive() || brush_state_notifications != 4) {
    return fail("cancelling a coalesced brush edit should notify the active-to-inactive transition");
  }
  if (!stroke_transition_observed_coherently) {
    return fail("brush state observers should see cleared point state after asset reset and cancel");
  }

  expected_stroke_transition = ExpectedStrokeTransition::Begin;
  stroke_state_controller.startSelectionBrush(false);
  stroke_state_controller.beginActiveBrushStroke();
  stroke_state_controller.addBrushPoint(0.3, 0.4);
  expected_pending_commit_points =
      MaskInteractionPerformanceTestAccess::active_brush_live_point_count(
          stroke_state_controller);
  expected_stroke_transition = ExpectedStrokeTransition::End;
  stroke_state_controller.endActiveBrushStroke();
  if (stroke_state_controller.brushStrokeActive() || brush_state_notifications != 6) {
    return fail("ending a brush stroke should publish exactly one state transition");
  }
  if (!stroke_transition_observed_coherently) {
    return fail("brush state observers should see the completed pending preview at stroke end");
  }

  controller.setColorToolMaskPreviewEnabled(true);
  controller.setColorReplacementEnabled(true);
  controller.setColorReplacementStrength(100.0);
  controller.setColorReplacementSourceHue(0.0);
  if (!MaskInteractionPerformanceTestAccess::color_preview_configured(controller)) {
    return fail("preview fixture should enable its color mask directly");
  }
  const auto first_request_source = controller.colorToolMaskPreviewSource();
  if (!first_request_source.isEmpty()) {
    return fail("cold landscape mask preview should be dispatched asynchronously");
  }
  controller.setColorReplacementSourceHue(0.02);
  const auto stale_settings_key = MaskInteractionPerformanceTestAccess::color_preview_key(controller);
  const auto first_settings_request = controller.colorToolMaskPreviewSource();
  controller.setColorReplacementSourceHue(0.04);
  const auto latest_settings_request = controller.colorToolMaskPreviewSource();
  if (!first_settings_request.isEmpty() || !latest_settings_request.isEmpty() ||
      MaskInteractionPerformanceTestAccess::has_color_preview(controller, stale_settings_key)) {
    return fail("cold settings changes should coalesce without publishing stale mask results");
  }
  const auto preview_source = await_color_mask_preview(controller);
  const auto data_prefix = QStringLiteral("data:image/png;base64,");
  if (!preview_source.startsWith(data_prefix)) {
    return fail("landscape color-tool mask preview should return a PNG data URI");
  }
  const auto preview_bytes = QByteArray::fromBase64(preview_source.mid(data_prefix.size()).toLatin1());
  QImage decoded_preview;
  const auto preview_loaded = decoded_preview.loadFromData(preview_bytes, "PNG");
  const auto preview_coverage = mask_coverage_stats(decoded_preview);
  if (!preview_loaded || decoded_preview.size() != QSize(512, 341) ||
      !has_mask_coverage(decoded_preview)) {
    const auto context = MaskInteractionPerformanceTestAccess::color_preview_diagnostics(controller);
    std::cerr << "color-mask PNG diagnostic: uri_bytes=" << preview_source.size()
              << " png_bytes=" << preview_bytes.size()
              << " loaded=" << preview_loaded
              << " dimensions=" << decoded_preview.width() << 'x' << decoded_preview.height()
              << " alpha_pixels=" << preview_coverage.nontransparent_pixels
              << " max_alpha=" << preview_coverage.maximum_alpha
              << " asset=" << context.value(QStringLiteral("assetId")).toString().toStdString()
              << " source=" << context.value(QStringLiteral("sourcePath")).toString().toStdString()
              << " hue=" << context.value(QStringLiteral("sourceHue")).toDouble()
              << " strength=" << context.value(QStringLiteral("strength")).toDouble()
              << " range=" << context.value(QStringLiteral("range")).toDouble()
              << " crop=" << context.value(QStringLiteral("cropX")).toDouble() << ','
              << context.value(QStringLiteral("cropY")).toDouble() << ','
              << context.value(QStringLiteral("cropWidth")).toDouble() << ','
              << context.value(QStringLiteral("cropHeight")).toDouble() << ','
              << context.value(QStringLiteral("cropZoom")).toDouble()
              << " busy=" << context.value(QStringLiteral("busy")).toBool()
              << " requested_key_matches="
              << (context.value(QStringLiteral("requestedKey")).toString() ==
                  context.value(QStringLiteral("currentKey")).toString()) << '\n';
    return fail("landscape color-tool mask preview should be a valid full-size PNG");
  }

  controller.setColorReplacementSourceHue(0.08);
  const auto warm_preview_source = controller.colorToolMaskPreviewSource();
  const auto warm_preview_bytes = QByteArray::fromBase64(
      warm_preview_source.mid(data_prefix.size()).toLatin1());
  QImage warm_decoded_preview;
  if (!warm_preview_source.startsWith(data_prefix) ||
      !warm_decoded_preview.loadFromData(warm_preview_bytes, "PNG") ||
      warm_decoded_preview.size() != QSize(512, 341) ||
      !has_mask_coverage(warm_decoded_preview)) {
    return fail("warm decoded source should return its changed-hue preview synchronously");
  }

  const auto second_source_path =
      QDir(temporary_directory.path()).filePath(QStringLiteral("landscape-two.png"));
  QImage second_landscape(512, 341, QImage::Format_RGB32);
  second_landscape.fill(QColor(16, 32, 255));
  if (!second_landscape.save(second_source_path, "PNG")) {
    return fail("second landscape source should save");
  }
  controller.loadAsset(
      QStringLiteral("mask-preview-landscape-two"),
      second_source_path,
      {}, {}, {}, {}, QStringLiteral("{}"));
  controller.setColorToolMaskPreviewEnabled(true);
  controller.setColorReplacementEnabled(true);
  controller.setColorReplacementStrength(100.0);
  controller.setColorReplacementSourceHue(0.66);
  const auto stale_asset_preview = await_color_mask_preview(controller);
  QImage second_decoded_preview;
  const auto second_preview_bytes = QByteArray::fromBase64(
      stale_asset_preview.mid(data_prefix.size()).toLatin1());
  if (!stale_asset_preview.startsWith(data_prefix) ||
      !second_decoded_preview.loadFromData(second_preview_bytes, "PNG") ||
      second_decoded_preview.size() != QSize(512, 341) ||
      !has_mask_coverage(second_decoded_preview)) {
    const auto coverage = mask_coverage_stats(second_decoded_preview);
    const auto context = MaskInteractionPerformanceTestAccess::color_preview_diagnostics(controller);
    std::cerr << "latest-asset color-mask diagnostic: uri_bytes=" << stale_asset_preview.size()
              << " png_bytes=" << second_preview_bytes.size()
              << " dimensions=" << second_decoded_preview.width() << 'x'
              << second_decoded_preview.height()
              << " alpha_pixels=" << coverage.nontransparent_pixels
              << " max_alpha=" << coverage.maximum_alpha
              << " asset=" << context.value(QStringLiteral("assetId")).toString().toStdString()
              << " source=" << context.value(QStringLiteral("sourcePath")).toString().toStdString()
              << " hue=" << context.value(QStringLiteral("sourceHue")).toDouble()
              << " strength=" << context.value(QStringLiteral("strength")).toDouble()
              << " range=" << context.value(QStringLiteral("range")).toDouble()
              << " crop=" << context.value(QStringLiteral("cropX")).toDouble() << ','
              << context.value(QStringLiteral("cropY")).toDouble() << ','
              << context.value(QStringLiteral("cropWidth")).toDouble() << ','
              << context.value(QStringLiteral("cropHeight")).toDouble() << ','
              << context.value(QStringLiteral("cropZoom")).toDouble()
              << " busy=" << context.value(QStringLiteral("busy")).toBool()
              << " requested_key_matches="
              << (context.value(QStringLiteral("requestedKey")).toString() ==
                  context.value(QStringLiteral("currentKey")).toString()) << '\n';
    return fail("latest landscape asset should publish its own decoded mask preview");
  }

  controller.startSelectionBrush(false);
  if (controller.emptyMaskCount() != 1) {
    return fail("canonical empty brush validation should remain accurate");
  }
  (void)controller.activeMaskOutlinePaths();
  const auto warm_misses = MaskInteractionPerformanceTestAccess::outline_misses(controller);
  (void)controller.activeMaskOutlinePaths();
  if (MaskInteractionPerformanceTestAccess::outline_hits(controller) <= 0 ||
      MaskInteractionPerformanceTestAccess::outline_misses(controller) != warm_misses) {
    return fail("unchanged mask outline should reuse its cached contour");
  }

  controller.setMaskPreviewMode(QStringLiteral("grayscale-alpha"));
  if (!outline_misses_after(controller, warm_misses)) {
    return fail("mask preview mode change should invalidate the outline cache");
  }

  const auto after_mode_misses = MaskInteractionPerformanceTestAccess::outline_misses(controller);
  controller.setMaskRefinementPreviewMode(QStringLiteral("original"));
  if (!outline_misses_after(controller, after_mode_misses)) {
    return fail("refinement preview mode change should invalidate the outline cache");
  }

  const auto after_refinement_mode_misses =
      MaskInteractionPerformanceTestAccess::outline_misses(controller);
  controller.setMaskRefinementHighQualityPreview(true);
  if (!outline_misses_after(controller, after_refinement_mode_misses)) {
    return fail("high-quality refinement preview change should invalidate the outline cache");
  }

  const auto after_high_quality_misses =
      MaskInteractionPerformanceTestAccess::outline_misses(controller);
  controller.setMaskFullResolutionPreview(true);
  if (!outline_misses_after(controller, after_high_quality_misses)) {
    return fail("full-resolution refinement preview change should invalidate the outline cache");
  }

  const auto after_refinement_toggles_misses =
      MaskInteractionPerformanceTestAccess::outline_misses(controller);
  controller.setCropZoom(1.1);
  if (!outline_misses_after(controller, after_refinement_toggles_misses)) {
    return fail("source geometry state change should invalidate the outline cache");
  }

  const auto after_crop_misses = MaskInteractionPerformanceTestAccess::outline_misses(controller);
  controller.setActiveMaskBrushSize(0.23);
  if (!outline_misses_after(controller, after_crop_misses)) {
    return fail("mask setter change should invalidate the outline cache");
  }

  const auto before_undo_misses = MaskInteractionPerformanceTestAccess::outline_misses(controller);
  controller.undo();
  if (!outline_misses_after(controller, before_undo_misses)) {
    return fail("undo-restored mask geometry should invalidate the outline cache");
  }

  const auto before_asset_misses = MaskInteractionPerformanceTestAccess::outline_misses(controller);
  controller.loadAsset(
      QStringLiteral("mask-preview-landscape-two"),
      second_source_path,
      {}, {}, {}, {}, QStringLiteral("{}"));
  controller.startSelectionBrush(false);
  if (!outline_misses_after(controller, before_asset_misses)) {
    return fail("asset and source replacement should invalidate the outline cache");
  }

  {
    QQmlEngine qml_engine;
    qml_engine.rootContext()->setContextProperty(QStringLiteral("editorPreview"), &controller);
    QQmlComponent component(&qml_engine);
    component.setData(
        QByteArrayLiteral(
            "import QtQml\n"
            "QtObject {\n"
            "  property int observedCount: editorPreview.maskSetCount\n"
            "  property var maskRows: editorPreview.maskSetCount > 0 ? "
            "editorPreview.maskSetSnapshot() : []\n"
            "}"),
        QUrl(QStringLiteral("qrc:/MaskSetSnapshotProbe.qml")));
    QObject* snapshot_probe = component.create();
    if (!snapshot_probe) {
      return fail("mask snapshot QML probe should instantiate");
    }
    const auto snapshot_rows = [&]() {
      QApplication::processEvents();
      return snapshot_probe->property("maskRows").toList();
    };
    const auto matches_controller_snapshot = [&]() {
      return canonical_mask_snapshot(snapshot_rows()) ==
          canonical_mask_snapshot(controller.maskSets());
    };
    const auto initial_rows = snapshot_rows();
    if (initial_rows.size() != controller.maskSetCount() ||
        !matches_controller_snapshot() || initial_rows.isEmpty()) {
      delete snapshot_probe;
      return fail("detached QML mask snapshot should preserve fields and order");
    }

    controller.setActiveMaskName(QStringLiteral("Snapshot rename check"));
    const auto renamed_rows = snapshot_rows();
    const auto renamed_controller_rows = controller.maskSets();
    const auto qml_snapshot_matches_controller =
        canonical_mask_snapshot(renamed_rows) ==
        canonical_mask_snapshot(renamed_controller_rows);
    const auto has_renamed_active_set = !renamed_rows.isEmpty() &&
        renamed_rows.constFirst().toMap().value(QStringLiteral("name")).toString() ==
            QStringLiteral("Snapshot rename check");
    if (renamed_rows.size() != initial_rows.size() ||
        controller.maskSetCount() != initial_rows.size() ||
        !has_renamed_active_set || !qml_snapshot_matches_controller) {
      const auto describe_rows = [](const QVariantList& rows) {
        QStringList descriptions;
        for (const auto& row_value : rows) {
          const auto row = row_value.toMap();
          const auto sources = row.value(QStringLiteral("sources")).toList();
          const auto first_source = sources.isEmpty() ? QVariantMap{} : sources.constFirst().toMap();
          descriptions.push_back(
              QStringLiteral("id=%1 name=%2 sourceCount=%3 firstSource=%4")
                  .arg(row.value(QStringLiteral("id")).toString(),
                       row.value(QStringLiteral("name")).toString())
                  .arg(row.value(QStringLiteral("sourceCount")).toInt())
                  .arg(first_source.value(QStringLiteral("name")).toString()));
        }
        return descriptions.join(QStringLiteral("; "));
      };
      QStringList field_differences;
      const auto describe_value = [](const QVariant& value) {
        if (value.typeId() == QMetaType::QVariantMap)
          return QStringLiteral("<map:%1>").arg(value.toMap().size());
        if (value.typeId() == QMetaType::QVariantList)
          return QStringLiteral("<list:%1>").arg(value.toList().size());
        if (value.typeId() == QMetaType::QByteArray)
          return QStringLiteral("<bytes:%1>").arg(value.toByteArray().size());
        auto text = value.toString();
        if (text.size() > 96)
          text = text.left(96) + QStringLiteral("…");
        return text;
      };
      const auto append_differences = [&](auto&& self,
                                          const QString& path,
                                          const QVariant& left,
                                          const QVariant& right) -> void {
        if (left == right)
          return;
        if (left.typeId() == QMetaType::QVariantMap &&
            right.typeId() == QMetaType::QVariantMap) {
          const auto left_map = left.toMap();
          const auto right_map = right.toMap();
          auto keys = left_map.keys();
          for (const auto& key : right_map.keys()) {
            if (!keys.contains(key))
              keys.push_back(key);
          }
          for (const auto& key : keys) {
            if (!left_map.contains(key) || !right_map.contains(key)) {
              field_differences.push_back(
                  QStringLiteral("%1.%2: %3 => %4")
                      .arg(path, key,
                           left_map.contains(key) ? QStringLiteral("present") : QStringLiteral("missing"),
                           right_map.contains(key) ? QStringLiteral("present") : QStringLiteral("missing")));
            } else {
              self(self, path + QLatin1Char('.') + key,
                   left_map.value(key), right_map.value(key));
            }
            if (field_differences.size() >= 30)
              return;
          }
          return;
        }
        if (left.typeId() == QMetaType::QVariantList &&
            right.typeId() == QMetaType::QVariantList) {
          const auto left_list = left.toList();
          const auto right_list = right.toList();
          if (left_list.size() != right_list.size()) {
            field_differences.push_back(
                QStringLiteral("%1.length: %2 => %3")
                    .arg(path)
                    .arg(left_list.size())
                    .arg(right_list.size()));
            return;
          }
          for (qsizetype index = 0; index < left_list.size(); ++index) {
            self(self, path + QStringLiteral("[%1]").arg(index),
                 left_list.at(index), right_list.at(index));
            if (field_differences.size() >= 30)
              return;
          }
          return;
        }
        field_differences.push_back(
            QStringLiteral("%1: %2 => %3")
                .arg(path, describe_value(left), describe_value(right)));
      };
      append_differences(append_differences, QStringLiteral("rows"),
                         QVariant::fromValue(canonical_mask_snapshot(renamed_rows)),
                         QVariant::fromValue(canonical_mask_snapshot(renamed_controller_rows)));
      std::cerr << "mask snapshot rename diagnostic: initial=["
                << describe_rows(initial_rows).toStdString() << "] qml=["
                << describe_rows(renamed_rows).toStdString() << "] controller=["
                << describe_rows(renamed_controller_rows).toStdString()
                << "] observed_count=" << snapshot_probe->property("observedCount").toInt()
                << " controller_count=" << controller.maskSetCount()
                << " changed=" << (renamed_rows != initial_rows)
                << " canonical_parity=" << qml_snapshot_matches_controller
                << " differences=[" << field_differences.join(QStringLiteral("; ")).toStdString()
                << "]\n";
      delete snapshot_probe;
      return fail("same-count mask rename should refresh the detached QML snapshot");
    }

    if (!controller.duplicateActiveMaskSet() ||
        controller.maskSetCount() != initial_rows.size() + 1 ||
        snapshot_rows().size() != initial_rows.size() + 1 ||
        !matches_controller_snapshot()) {
      delete snapshot_probe;
      return fail("mask snapshot should refresh when its row count increases");
    }
    const auto duplicated_rows = snapshot_rows();
    const auto initial_set_id = initial_rows.constFirst().toMap().value(QStringLiteral("id"));
    if (duplicated_rows.size() != initial_rows.size() + 1 ||
        duplicated_rows.constFirst().toMap().value(QStringLiteral("id")) != initial_set_id ||
        duplicated_rows.constLast().toMap().value(QStringLiteral("id")) == initial_set_id) {
      delete snapshot_probe;
      return fail("duplicating a mask set should preserve row order and append a distinct set");
    }
    const auto duplicated_source_count = duplicated_rows.isEmpty()
        ? 0
        : duplicated_rows.constLast().toMap().value(QStringLiteral("sources")).toList().size();
    for (qsizetype source_index = 0; source_index < duplicated_source_count; ++source_index)
      controller.removeActiveMask();
    if (controller.maskSetCount() != initial_rows.size() ||
        snapshot_rows().size() != initial_rows.size() ||
        !matches_controller_snapshot() ||
        snapshot_rows().constFirst().toMap().value(QStringLiteral("id")) != initial_set_id) {
      delete snapshot_probe;
      return fail("mask snapshot should refresh when its row count decreases");
    }
    delete snapshot_probe;
  }

  return EXIT_SUCCESS;
}
