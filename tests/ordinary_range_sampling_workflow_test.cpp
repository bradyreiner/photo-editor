#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "photo_render_utils.h"
#include "photo_source_precision.h"
#include "render_plan_store.h"
#include "recipe_mask_sampling.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace photo_editor::desktop;

namespace photo_editor::desktop {
RenderResult render_preview_request_for_test(const RenderRequest& request);
}

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

class ScopedEnvironment final {
 public:
  ScopedEnvironment(const char* name, QByteArray value)
      : name_(name), existed_(qEnvironmentVariableIsSet(name)), previous_(qgetenv(name)) {
    qputenv(name_, value);
  }
  ~ScopedEnvironment() {
    if (existed_) qputenv(name_, previous_);
    else qunsetenv(name_);
  }
 private:
  QByteArray name_;
  bool existed_ = false;
  QByteArray previous_;
};

void settle(EditorPreviewController& editor, const char* message) {
  QElapsedTimer timer;
  timer.start();
  while ((editor.processing() || editor.fullRenderRunning() ||
          editor.professionalToolsBusy() || editor.photoPaintBusy() ||
          editor.selectionWorkBusy() || editor.photoExportBusy()) &&
         timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  require(!editor.processing() && !editor.fullRenderRunning() &&
              !editor.professionalToolsBusy() && !editor.photoPaintBusy() &&
              !editor.selectionWorkBusy() && !editor.photoExportBusy(),
          message);
}

int rgb_distance(const QColor& left, const QColor& right) {
  return std::abs(left.red() - right.red()) +
      std::abs(left.green() - right.green()) +
      std::abs(left.blue() - right.blue());
}

EditRecipeState recipe(const EditorPreviewController& editor) {
  const auto parsed = parse_adjustment_patch_json(
      editor.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok(), "The ordinary range recipe must parse for inspection.");
  return *parsed.state;
}

struct CoverageAt final {
  int x = 0;
  int y = 0;
  int value = 0;
  QSize size;
};

CoverageAt evaluate_at_sample(
    const QString& path,
    const EditRecipeState& state,
    const QVariantMap& mask,
    double normalized_x,
    double normalized_y) {
  QImage decoded;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  require(decode_source_image(path, &decoded, 0, state, &metadata),
          "Decode the ordinary source for renderer-equivalent range coverage.");
  const auto sampling = prepare_recipe_mask_sampling(
      decoded,
      state,
      metadata,
      mask,
      crop_rect_for_image(decoded.size(), state));
  require(sampling.valid, "Prepare the renderer's range-sampling input.");
  const auto alpha = sampling.evaluate({sampling.compile_mask(mask)});
  require(alpha.valid, "Evaluate the committed mask at its renderer input.");
  const auto x = qBound(0, qRound(normalized_x * (alpha.size.width() - 1)),
                        alpha.size.width() - 1);
  const auto y = qBound(0, qRound(normalized_y * (alpha.size.height() - 1)),
                        alpha.size.height() - 1);
  const auto index = static_cast<qsizetype>(y) * alpha.size.width() + x;
  return CoverageAt{x, y,
      index < alpha.alpha_mask.size()
          ? static_cast<int>(static_cast<unsigned char>(alpha.alpha_mask[index]))
          : 0,
      alpha.size};
}

RenderResult render_state(const QString& path, const QString& asset_id,
                          const EditRecipeState& state, int generation) {
  RenderRequest request;
  request.source_asset_id = asset_id;
  request.file_path = path;
  request.state = state;
  request.full_resolution = true;
  request.max_edge = 0;
  request.full_invalidation = true;
  request.use_in_memory_preview = true;
  request.require_cpu_preview_image = true;
  request.require_cpu_display_image = true;
  request.require_canonical_render_plan = true;
  request.render_mode = QStringLiteral("max_preview");
  request.generation = generation;
  request.latest_generation = std::make_shared<std::atomic<int>>(generation);
  return render_preview_request_for_test(request);
}

EditRecipeState remove_selection_link(
    EditRecipeState state,
    const QString& mask_set_id) {
  auto found = false;
  for (auto index = 0; index < state.layers.size(); ++index) {
    auto layer = state.layers.at(index).toMap();
    const auto linked = layer.value(QStringLiteral("selectionMaskSetId")).toString() == mask_set_id ||
        layer.value(QStringLiteral("selectionMaskSetIds")).toStringList().contains(mask_set_id);
    if (!linked) continue;
    found = true;
    layer.insert(QStringLiteral("selectionScoped"), false);
    for (const auto* key : {"selectionMaskSetId", "selectionMaskSetName",
                             "selectionMaskSetIds", "selectionMaskSetNames",
                             "selectionIntersectionMaskSetIds",
                             "selectionIntersectionMaskSetNames"}) {
      layer.remove(QString::fromLatin1(key));
    }
    state.layers[index] = layer;
  }
  require(found, "Find the adjustment row for the unmasked positive control.");
  return state;
}

QImage render_export_state(
    const QString& path,
    const QString& asset_id,
    const QImage& source,
    const EditRecipeState& state) {
  const auto crop = crop_rect_for_image(source.size(), state);
  const auto admission = RenderPlanStore::shared().admit(
      asset_id, path, state, source.size(),
      photo_editor::pipeline::RenderPurpose::Export,
      photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation);
  require(admission.admitted && admission.plan.has_value(),
          "Admit the ordinary range export fixture.");
  const RenderMaskOptions mask_options{
      .rebuild_editable_masks_at_output_resolution = true,
      .source_geometry = DesktopSourceGeometry{source.size(), crop},
      .full_source = source,
  };
  RenderBackendTelemetry telemetry;
  return render_output_image_with_backend(
      source.copy(crop), state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly, nullptr, &telemetry, mask_options,
      photo_editor::pipeline::RemovalQuality::Final,
      RenderOutputPolicy::DisplayReferred, &*admission.plan);
}

void assert_history_keeps_range_source(
    EditorPreviewController& editor,
    const QString& path,
    const QVariantList& expected_masks,
    double normalized_x,
    double normalized_y) {
  const auto layer_index = editor.ensureActiveSelectionAdjustmentLayer();
  require(layer_index >= 0, "Attach the sampled range to a real adjustment layer.");
  require(editor.setPhotoLayerAdjustmentValue(
              layer_index, QStringLiteral("exposure"), 0.8),
          "Set a visible local adjustment on the sampled range.");
  const auto after = recipe(editor);
  require(after.mask_stack == expected_masks,
          "Attaching a range must not mutate unrelated masks or its captured samples.");
  const auto mask = after.mask_stack.last().toMap();
  const auto coverage = evaluate_at_sample(path, after, mask, normalized_x, normalized_y);
  require(coverage.value >= 250,
          "The attached ordinary range must keep full coverage at its sampled point.");

  editor.undo();
  const auto undone = recipe(editor);
  require(undone.mask_stack == expected_masks,
          "Undoing a local adjustment must preserve the ordinary range definition.");
  editor.redo();
  const auto redone = recipe(editor);
  require(redone.mask_stack == expected_masks && redone.layers == after.layers &&
              std::abs(redone.layers.at(layer_index).toMap()
                           .value(QStringLiteral("exposure")).toDouble() - 0.8) < 1e-6,
          "Redo must restore the local adjustment without changing the range sample.");
}

void test_same_name_unlinked_range_does_not_require_full_source(QTemporaryDir& runtime) {
  EditRecipeState state;
  QVariantMap layer{{QStringLiteral("id"), QStringLiteral("linked-adjustment")},
                    {QStringLiteral("kind"), QStringLiteral("adjustment")},
                    {QStringLiteral("selectionScoped"), true},
                    {QStringLiteral("selectionMaskSetId"), QStringLiteral("linked-set")},
                    {QStringLiteral("selectionMaskSetIds"), QVariantList{QStringLiteral("linked-set")}},
                    {QStringLiteral("selectionMaskSetName"), QStringLiteral("Shared selection")},
                    {QStringLiteral("selectionMaskSetNames"), QVariantList{QStringLiteral("Shared selection")}},
                    {QStringLiteral("exposure"), 0.8}};
  QVariantMap linked_source{{QStringLiteral("id"), QStringLiteral("linked-source")},
                            {QStringLiteral("maskSetId"), QStringLiteral("linked-set")},
                            {QStringLiteral("maskSetName"), QStringLiteral("Shared selection")},
                            {QStringLiteral("kind"), QStringLiteral("rectangle")},
                            {QStringLiteral("operation"), QStringLiteral("add")},
                            {QStringLiteral("enabled"), true},
                            {QStringLiteral("centerX"), 0.5},
                            {QStringLiteral("centerY"), 0.5},
                            {QStringLiteral("radiusX"), 0.3},
                            {QStringLiteral("radiusY"), 0.2}};
  QVariantMap same_name_unlinked_range{{QStringLiteral("id"), QStringLiteral("unlinked-color-source")},
                                       {QStringLiteral("maskSetId"), QStringLiteral("unlinked-set")},
                                       {QStringLiteral("maskSetName"), QStringLiteral("Shared selection")},
                                       {QStringLiteral("kind"), QStringLiteral("color")},
                                       {QStringLiteral("operation"), QStringLiteral("add")},
                                       {QStringLiteral("enabled"), true}};
  state.layers = {layer};
  state.mask_stack = {linked_source, same_name_unlinked_range};

  auto legacy_intersection_state = state;
  QVariantMap intersection_owner{{QStringLiteral("id"), QStringLiteral("intersection-owner")},
                                {QStringLiteral("kind"), QStringLiteral("adjustment")},
                                {QStringLiteral("selectionScoped"), true},
                                {QStringLiteral("selectionMaskSetId"), QStringLiteral("linked-set")},
                                {QStringLiteral("selectionMaskSetIds"), QVariantList{QStringLiteral("linked-set")}},
                                {QStringLiteral("selectionMaskSetName"), QStringLiteral("Shared selection")},
                                {QStringLiteral("selectionMaskSetNames"), QVariantList{QStringLiteral("Shared selection")}},
                                {QStringLiteral("selectionIntersectionMaskSetNames"), QVariantList{QStringLiteral("Legacy intersection")}}};
  legacy_intersection_state.layers.append(intersection_owner);
  QVariantMap legacy_intersection_range{{QStringLiteral("id"), QStringLiteral("legacy-intersection-source")},
                                       {QStringLiteral("maskSetId"), QStringLiteral("legacy-intersection-set")},
                                       {QStringLiteral("maskSetName"), QStringLiteral("Legacy intersection")},
                                       {QStringLiteral("kind"), QStringLiteral("luminance")},
                                       {QStringLiteral("operation"), QStringLiteral("add")},
                                       {QStringLiteral("enabled"), true}};
  legacy_intersection_state.mask_stack.append(legacy_intersection_range);

  auto legacy_primary_state = state;
  QVariantMap legacy_primary_owner{{QStringLiteral("id"), QStringLiteral("legacy-primary-owner")},
                                  {QStringLiteral("kind"), QStringLiteral("adjustment")},
                                  {QStringLiteral("selectionScoped"), true},
                                  {QStringLiteral("selectionMaskSetNames"), QVariantList{QStringLiteral("Legacy primary")}},
                                  {QStringLiteral("selectionIntersectionMaskSetIds"), QVariantList{QStringLiteral("linked-set")}},
                                  {QStringLiteral("selectionIntersectionMaskSetNames"), QVariantList{QStringLiteral("Shared selection")}}};
  legacy_primary_state.layers.append(legacy_primary_owner);
  QVariantMap legacy_primary_range{{QStringLiteral("id"), QStringLiteral("legacy-primary-source")},
                                  {QStringLiteral("maskSetId"), QStringLiteral("legacy-primary-set")},
                                  {QStringLiteral("maskSetName"), QStringLiteral("Legacy primary")},
                                  {QStringLiteral("kind"), QStringLiteral("luminance")},
                                  {QStringLiteral("operation"), QStringLiteral("add")},
                                  {QStringLiteral("enabled"), true}};
  legacy_primary_state.mask_stack.append(legacy_primary_range);

  const QSize source_size(160, 96);
  const auto path = QDir(runtime.path()).filePath(QStringLiteral("same-name-unavailable-source.png"));
  QImage source(source_size, QImage::Format_ARGB32);
  source.fill(QColor(QStringLiteral("#667788")));
  require(source.save(path, "PNG"), "Write a source for canonical plan admission.");
  const auto admit = [&](const QString& asset, const EditRecipeState& candidate) {
    return RenderPlanStore::shared().admit(
        asset, path, candidate, source_size,
        photo_editor::pipeline::RenderPurpose::Export,
        photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation);
  };
  const auto exact_admission = admit(QStringLiteral("same-name-live-range-workflow"), state);
  const auto intersection_admission = admit(
      QStringLiteral("legacy-intersection-range-workflow"), legacy_intersection_state);
  const auto primary_admission = admit(
      QStringLiteral("legacy-primary-range-workflow"), legacy_primary_state);
  if (!exact_admission.admitted) {
    std::cerr << "exact-ID fixture admission failed: "
              << exact_admission.error.toStdString() << '\n';
  }
  if (!intersection_admission.admitted) {
    std::cerr << "legacy-intersection fixture admission failed: "
              << intersection_admission.error.toStdString() << '\n';
  }
  if (!primary_admission.admitted) {
    std::cerr << "legacy-primary fixture admission failed: "
              << primary_admission.error.toStdString() << '\n';
  }
  require(exact_admission.admitted && exact_admission.plan.has_value() &&
              intersection_admission.admitted && intersection_admission.plan.has_value() &&
              primary_admission.admitted && primary_admission.plan.has_value(),
          "Admit the exact-ID and legacy-alias fixtures before exercising deferred decode.");
  require(QFile::remove(path), "Remove the fixture source to expose an accidental range decode.");

  const auto crop = QRect(16, 8, 128, 80);
  const auto render_without_source = [&](const EditRecipeState& candidate,
                                         const CanonicalRenderPlanAdmission& admission) {
    RenderBackendTelemetry telemetry;
    static_cast<void>(render_output_file(
        path, source_size, crop, candidate, QColorSpace(QColorSpace::SRgb),
        QImage::Format_ARGB32, nullptr, &telemetry,
        RenderMaskOptions{.rebuild_editable_masks_at_output_resolution = true,
                          .source_geometry = DesktopSourceGeometry{source_size, crop}},
        photo_editor::pipeline::RemovalQuality::Final,
        RenderOutputPolicy::DisplayReferred, nullptr, &*admission.plan));
    return telemetry.fallback_reason;
  };
  const auto exact_reason = render_without_source(state, exact_admission);
  require(exact_reason !=
              QStringLiteral("full_source_required_for_linked_range_mask"),
          "A same-name unlinked range must not require full-source pixels for another exact-ID-linked mask.");
  const auto intersection_reason = render_without_source(
      legacy_intersection_state, intersection_admission);
  require(intersection_reason ==
              QStringLiteral("full_source_required_for_linked_range_mask"),
          "An intersection name-only alias must still resolve despite explicit primary IDs.");
  const auto primary_reason = render_without_source(legacy_primary_state, primary_admission);
  require(primary_reason ==
              QStringLiteral("full_source_required_for_linked_range_mask"),
          "A primary name-only alias must still resolve despite explicit intersection IDs.");
}

void test_luminance_range_sampling_uses_render_input(QTemporaryDir& runtime) {
  constexpr int width = 160;
  constexpr int height = 96;
  QImage source(width, height, QImage::Format_ARGB32);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto value = qRound(18.0 + 202.0 * static_cast<double>(x) / (width - 1));
      source.setPixelColor(x, y, QColor(value, value, value, 255));
    }
  }
  const auto path = QDir(runtime.path()).filePath(QStringLiteral("ordinary-luma-range.png"));
  require(source.save(path, "PNG"), "Write the ordinary grayscale range fixture.");

  EditorPreviewController editor;
  const auto asset = QStringLiteral("ordinary-luma-range-workflow");
  editor.loadAsset(asset, path, {}, {}, {}, {}, {});
  settle(editor, "The ordinary Luma controller must load its source before sampling.");
  editor.updateCropRect(0.12, 0.10, 0.76, 0.80);
  editor.setExposure(0.7);
  editor.setTemperature(18.0);
  editor.setTint(-8.0);
  settle(editor, "The ordinary Luma crop and global adjustments must settle before sampling.");
  editor.addMask(QStringLiteral("luminance"));

  constexpr double sample_x = 0.44;
  constexpr double sample_y = 0.51;
  require(editor.beginActiveLuminanceRangeSample(sample_x, sample_y) &&
              editor.updateActiveLuminanceRangeSample(sample_x, sample_y) &&
              editor.commitActiveLuminanceRangeSample(sample_x, sample_y),
          "Sample the visible cropped, globally adjusted grayscale canvas.");
  settle(editor, "The ordinary Luma sample must publish before it is rendered.");
  const auto sampled_state = recipe(editor);
  const auto sampled_mask = sampled_state.mask_stack.last().toMap();
  const auto coverage = evaluate_at_sample(path, sampled_state, sampled_mask,
                                           sample_x, sample_y);
  require(coverage.value >= 250,
          "Luminance Range must fully select its sampled point after crop and basic adjustments.");
  const auto expected_masks = sampled_state.mask_stack;
  const auto baseline = render_state(path, asset, sampled_state, 1901);
  require(baseline.success, "Render the sampled ordinary Luminance Range.");

  assert_history_keeps_range_source(editor, path, expected_masks,
                                    sample_x, sample_y);
  const auto adjusted_state = recipe(editor);
  const auto adjusted = render_state(path, asset, adjusted_state, 1902);
  require(adjusted.success && adjusted.preview_image.size() == baseline.preview_image.size(),
          "Render the attached ordinary Luminance Range adjustment.");
  const auto after_pixel = adjusted.preview_image.pixelColor(coverage.x, coverage.y);
  const auto before_pixel = baseline.preview_image.pixelColor(coverage.x, coverage.y);
  const auto rgb_delta = std::abs(after_pixel.red() - before_pixel.red()) +
      std::abs(after_pixel.green() - before_pixel.green()) +
      std::abs(after_pixel.blue() - before_pixel.blue());
  if (rgb_delta <= 20) {
    QImage decoded;
    photo_editor::pipeline::DecodedImageMetadata metadata;
    static_cast<void>(decode_source_image(path, &decoded, 0, adjusted_state, &metadata));
    const auto crop = crop_rect_for_image(decoded.size(), adjusted_state);
    int maximum_delta = -1;
    QPoint maximum_delta_point;
    QColor maximum_before;
    QColor maximum_after;
    for (int y = 0; y < baseline.preview_image.height(); ++y) {
      for (int x = 0; x < baseline.preview_image.width(); ++x) {
        const auto old_color = baseline.preview_image.pixelColor(x, y);
        const auto new_color = adjusted.preview_image.pixelColor(x, y);
        const auto delta = rgb_distance(old_color, new_color);
        if (delta > maximum_delta) {
          maximum_delta = delta;
          maximum_delta_point = QPoint(x, y);
          maximum_before = old_color;
          maximum_after = new_color;
        }
      }
    }
    const auto expected_set_id = sampled_mask.value(QStringLiteral("maskSetId")).toString();
    QVariantMap layer;
    for (const auto& value : adjusted_state.layers) {
      const auto candidate = value.toMap();
      if (candidate.value(QStringLiteral("selectionMaskSetId")).toString() == expected_set_id) {
        layer = candidate;
        break;
      }
    }
    std::cerr << "ordinary luma pixel diagnostic: sample=" << coverage.x << ',' << coverage.y
              << " coverage=" << coverage.value << " output="
              << baseline.preview_image.width() << 'x' << baseline.preview_image.height()
              << " crop=" << crop.x() << ',' << crop.y() << ',' << crop.width() << ',' << crop.height()
              << " before=" << before_pixel.red() << ',' << before_pixel.green() << ','
              << before_pixel.blue() << ',' << before_pixel.alpha()
              << " after=" << after_pixel.red() << ',' << after_pixel.green() << ','
              << after_pixel.blue() << ',' << after_pixel.alpha()
              << " maxDelta=" << maximum_delta << " at=" << maximum_delta_point.x() << ','
              << maximum_delta_point.y() << " maxBefore=" << maximum_before.red() << ','
              << maximum_before.green() << ',' << maximum_before.blue()
              << " maxAfter=" << maximum_after.red() << ',' << maximum_after.green() << ','
              << maximum_after.blue() << " layerId="
              << layer.value(QStringLiteral("id")).toString().toStdString()
              << " scoped=" << layer.value(QStringLiteral("selectionScoped")).toBool()
              << " maskSetId="
              << layer.value(QStringLiteral("selectionMaskSetId")).toString().toStdString()
              << " expectedSetId="
              << expected_set_id.toStdString()
              << " exposure=" << layer.value(QStringLiteral("exposure")).toDouble() << '\n';
  }
  require(rgb_delta > 20,
          "The actual output pixel used for Luminance Range sampling must change.");
  const auto untouched_before = baseline.preview_image.pixelColor(2, coverage.y);
  const auto untouched_after = adjusted.preview_image.pixelColor(2, coverage.y);
  const auto untouched_delta = std::abs(untouched_after.red() - untouched_before.red()) +
      std::abs(untouched_after.green() - untouched_before.green()) +
      std::abs(untouched_after.blue() - untouched_before.blue());
  require(untouched_delta <= 3,
          "A far out-of-range ordinary pixel must remain unchanged by the local adjustment.");
  const auto positive_control = render_state(
      path, asset,
      remove_selection_link(adjusted_state, sampled_mask.value(QStringLiteral("maskSetId")).toString()),
      1903);
  require(positive_control.success &&
              rgb_distance(positive_control.preview_image.pixelColor(coverage.x, coverage.y),
                          before_pixel) > 20 &&
              rgb_distance(positive_control.preview_image.pixelColor(2, coverage.y),
                          untouched_before) > 20,
          "The unmasked positive control must apply the same local adjustment inside and outside the range.");
  const auto baseline_export = render_export_state(path, asset, source, sampled_state);
  const auto adjusted_export = render_export_state(path, asset, source, adjusted_state);
  require(!baseline_export.isNull() && !adjusted_export.isNull() &&
              rgb_distance(adjusted_export.pixelColor(coverage.x, coverage.y),
                           baseline_export.pixelColor(coverage.x, coverage.y)) > 20 &&
              rgb_distance(adjusted_export.pixelColor(2, coverage.y),
                           baseline_export.pixelColor(2, coverage.y)) <= 3,
          "Luminance Range export must apply the same sampled coverage and preserve outside pixels.");
  const auto reopened_export_state = parse_edit_recipe_json(
      serialize_edit_recipe_json(adjusted_state));
  require(reopened_export_state.mask_stack == adjusted_state.mask_stack &&
              reopened_export_state.layers == adjusted_state.layers,
          "The durable ordinary Luminance recipe must retain its mask and linked adjustment row.");
  const auto reopened_export = render_export_state(
      path, asset + QStringLiteral("-reopened"), source, reopened_export_state);
  require(!reopened_export.isNull() && reopened_export == adjusted_export,
          "Reopening the ordinary Luminance recipe must preserve exported pixels.");
  const auto legacy_roundtrip = parse_adjustment_patch_json(
      serialize_adjustment_patch_json(adjusted_state, all_adjustment_groups(),
                                      AdjustmentPatchScope::AssetBound));
  require(legacy_roundtrip.ok() &&
              legacy_roundtrip.state->mask_stack == adjusted_state.mask_stack &&
              legacy_roundtrip.state->layers == adjusted_state.layers,
          "Legacy adjustment-patch reopen must retain Luminance samples and their layer link.");
  editor.setExposure(0.95);
  settle(editor, "A later global exposure edit must settle before resampling Luminance Range.");
  require(editor.beginActiveLuminanceRangeSample(sample_x, sample_y) &&
              editor.updateActiveLuminanceRangeSample(sample_x, sample_y) &&
              editor.commitActiveLuminanceRangeSample(sample_x, sample_y),
          "Resample the Luminance Range after changing the global appearance.");
  const auto resampled_state = recipe(editor);
  const auto resampled_mask = resampled_state.mask_stack.last().toMap();
  require(resampled_mask.value(QStringLiteral("luminanceSamples")).toList().size() == 2 &&
              evaluate_at_sample(path, resampled_state, resampled_mask, sample_x, sample_y).value >= 250,
          "A deliberate Luminance resample must retain full coverage against the changed global prefix.");
}

void test_color_range_sampling_uses_render_input(QTemporaryDir& runtime) {
  constexpr int width = 160;
  constexpr int height = 96;
  QImage source(width, height, QImage::Format_ARGB32);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  source.fill(QColor(QStringLiteral("#1e2630")));
  QPainter painter(&source);
  painter.fillRect(QRect(42, 14, 90, 68), QColor(QStringLiteral("#b04080")));
  painter.end();
  const auto path = QDir(runtime.path()).filePath(QStringLiteral("ordinary-color-range.png"));
  require(source.save(path, "PNG"), "Write the ordinary Color Range fixture.");

  EditorPreviewController editor;
  const auto asset = QStringLiteral("ordinary-color-range-workflow");
  editor.loadAsset(asset, path, {}, {}, {}, {}, {});
  settle(editor, "The ordinary Color controller must load its source before sampling.");
  editor.updateCropRect(0.10, 0.08, 0.80, 0.84);
  editor.setExposure(0.45);
  editor.setTemperature(-14.0);
  editor.setTint(12.0);
  settle(editor, "The ordinary Color crop and global adjustments must settle before sampling.");
  editor.addMask(QStringLiteral("color"));

  constexpr double sample_x = 0.50;
  constexpr double sample_y = 0.50;
  require(editor.sampleActiveColorRangeAt(sample_x, sample_y),
          "Sample the visible cropped, globally adjusted color patch.");
  settle(editor, "The ordinary Color sample must publish before it is rendered.");
  const auto sampled_state = recipe(editor);
  const auto sampled_mask = sampled_state.mask_stack.last().toMap();
  const auto coverage = evaluate_at_sample(path, sampled_state, sampled_mask,
                                           sample_x, sample_y);
  require(coverage.value >= 250,
          "Color Range must fully select its sampled point after crop and basic adjustments.");
  const auto samples = sampled_mask.value(QStringLiteral("colorSamples")).toList();
  require(samples.size() == 1, "Color Range must persist exactly one authored sample.");
  QImage decoded;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  require(decode_source_image(path, &decoded, 0, sampled_state, &metadata),
          "Decode the source for authored RGB metadata verification.");
  const auto crop = crop_rect_for_image(decoded.size(), sampled_state);
  const auto raw_sample = decoded.pixelColor(
      crop.x() + qRound(sample_x * (crop.width() - 1)),
      crop.y() + qRound(sample_y * (crop.height() - 1)));
  const auto sample_record = samples.first().toMap();
  require(sample_record.value(QStringLiteral("red")).toInt() == raw_sample.red() &&
              sample_record.value(QStringLiteral("green")).toInt() == raw_sample.green() &&
              sample_record.value(QStringLiteral("blue")).toInt() == raw_sample.blue(),
          "Color Range must retain the source RGB values separately from its adjusted range measurements.");

  const auto expected_masks = sampled_state.mask_stack;
  const auto baseline = render_state(path, asset, sampled_state, 1911);
  require(baseline.success, "Render the sampled ordinary Color Range.");
  assert_history_keeps_range_source(editor, path, expected_masks,
                                    sample_x, sample_y);
  const auto adjusted = render_state(path, asset, recipe(editor), 1912);
  require(adjusted.success && adjusted.preview_image.size() == baseline.preview_image.size(),
          "Render the attached ordinary Color Range adjustment.");
  const auto adjusted_state = recipe(editor);
  const auto before_pixel = baseline.preview_image.pixelColor(coverage.x, coverage.y);
  const auto after_pixel = adjusted.preview_image.pixelColor(coverage.x, coverage.y);
  const auto delta = std::abs(after_pixel.red() - before_pixel.red()) +
      std::abs(after_pixel.green() - before_pixel.green()) +
      std::abs(after_pixel.blue() - before_pixel.blue());
  require(delta > 20,
          "The actual output pixel used for Color Range sampling must change.");
  require(rgb_distance(baseline.preview_image.pixelColor(2, coverage.y),
                       adjusted.preview_image.pixelColor(2, coverage.y)) <= 3,
          "A far out-of-range ordinary pixel must remain unchanged by Color Range.");
  const auto positive_control = render_state(
      path, asset,
      remove_selection_link(adjusted_state, sampled_mask.value(QStringLiteral("maskSetId")).toString()),
      1913);
  require(positive_control.success &&
              rgb_distance(positive_control.preview_image.pixelColor(coverage.x, coverage.y),
                          before_pixel) > 20 &&
              rgb_distance(positive_control.preview_image.pixelColor(2, coverage.y),
                          baseline.preview_image.pixelColor(2, coverage.y)) > 20,
          "The unmasked Color Range positive control must visibly change both sampled and outside pixels.");
  const auto baseline_export = render_export_state(path, asset, source, sampled_state);
  const auto adjusted_export = render_export_state(path, asset, source, adjusted_state);
  require(!baseline_export.isNull() && !adjusted_export.isNull() &&
              rgb_distance(adjusted_export.pixelColor(coverage.x, coverage.y),
                           baseline_export.pixelColor(coverage.x, coverage.y)) > 20 &&
              rgb_distance(adjusted_export.pixelColor(2, coverage.y),
                           baseline_export.pixelColor(2, coverage.y)) <= 3,
          "Color Range export must apply the same sampled coverage and preserve outside pixels.");
  const auto reopened_export_state = parse_edit_recipe_json(
      serialize_edit_recipe_json(adjusted_state));
  require(reopened_export_state.mask_stack == adjusted_state.mask_stack &&
              reopened_export_state.layers == adjusted_state.layers,
          "The durable ordinary Color recipe must retain its mask and linked adjustment row.");
  const auto reopened_export = render_export_state(
      path, asset + QStringLiteral("-reopened"), source, reopened_export_state);
  require(!reopened_export.isNull() && reopened_export == adjusted_export,
          "Reopening the ordinary Color recipe must preserve exported pixels.");
  const auto legacy_roundtrip = parse_adjustment_patch_json(
      serialize_adjustment_patch_json(adjusted_state, all_adjustment_groups(),
                                      AdjustmentPatchScope::AssetBound));
  require(legacy_roundtrip.ok() &&
              legacy_roundtrip.state->mask_stack == adjusted_state.mask_stack &&
              legacy_roundtrip.state->layers == adjusted_state.layers,
          "Legacy adjustment-patch reopen must retain Color samples and their layer link.");
  editor.setTemperature(8.0);
  settle(editor, "A later white-balance edit must settle before resampling Color Range.");
  require(editor.removeActiveColorRangeSample(0),
          "Remove the old Color sample before replacing it in the changed global domain.");
  require(editor.sampleActiveColorRangeAt(sample_x, sample_y),
          "Replace the Color sample after changing the global appearance.");
  const auto resampled_state = recipe(editor);
  const auto resampled_mask = resampled_state.mask_stack.last().toMap();
  const auto resampled_coverage = evaluate_at_sample(
      path, resampled_state, resampled_mask, sample_x, sample_y);
  if (resampled_mask.value(QStringLiteral("colorSamples")).toList().size() != 1 ||
      resampled_coverage.value < 250) {
    const auto sample_values = resampled_mask.value(QStringLiteral("colorSamples")).toList();
    std::cerr << "ordinary color resample diagnostic: samples=" << sample_values.size()
              << " coverage=" << resampled_coverage.value << " at="
              << resampled_coverage.x << ',' << resampled_coverage.y
              << " hueCenter=" << resampled_mask.value(QStringLiteral("hueCenter")).toDouble()
              << " hueRange=" << resampled_mask.value(QStringLiteral("hueRange")).toDouble()
              << " saturation=" << resampled_mask.value(QStringLiteral("saturationMin")).toDouble()
              << ".." << resampled_mask.value(QStringLiteral("saturationMax")).toDouble()
              << " exposure=" << resampled_state.exposure << " temperature="
              << resampled_state.temperature << " sampleMetrics=";
    for (const auto& value : sample_values) {
      const auto sample = value.toMap();
      std::cerr << '[' << sample.value(QStringLiteral("hue")).toDouble() << ','
                << sample.value(QStringLiteral("saturation")).toDouble() << ','
                << sample.value(QStringLiteral("luminance")).toDouble() << ']';
    }
    std::cerr << '\n';
  }
  require(resampled_mask.value(QStringLiteral("colorSamples")).toList().size() == 1 &&
              resampled_coverage.value >= 250,
          "Replacing the Color sample must retain full coverage against the changed global prefix.");
}

void test_color_range_multi_pick_union_and_removal(QTemporaryDir& runtime) {
  constexpr int width = 200;
  constexpr int height = 100;
  QImage source(width, height, QImage::Format_ARGB32);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  source.fill(QColor(QStringLiteral("#666666")));
  QPainter painter(&source);
  painter.fillRect(QRect(15, 12, 50, 76), QColor(QStringLiteral("#d05050")));
  painter.fillRect(QRect(75, 12, 50, 76), QColor(QStringLiteral("#50d050")));
  painter.fillRect(QRect(135, 12, 50, 76), QColor(QStringLiteral("#5050d0")));
  painter.end();
  const auto path = QDir(runtime.path()).filePath(QStringLiteral("ordinary-color-multipick.png"));
  require(source.save(path, "PNG"), "Write the disjoint-color Color Range fixture.");

  EditorPreviewController editor;
  const auto asset = QStringLiteral("ordinary-color-multipick-workflow");
  editor.loadAsset(asset, path, {}, {}, {}, {}, {});
  settle(editor, "The multi-pick Color controller must load its source before sampling.");
  editor.updateCropRect(0.05, 0.05, 0.90, 0.90);
  editor.setExposure(0.30);
  editor.setTemperature(8.0);
  editor.setTint(-4.0);
  settle(editor, "The cropped, adjusted multi-color source must settle before sampling.");
  editor.addMask(QStringLiteral("color"));

  constexpr double red_x = 0.17;
  constexpr double green_x = 0.50;
  constexpr double blue_x = 0.83;
  constexpr double sample_y = 0.50;
  require(editor.sampleActiveColorRangeAt(red_x, sample_y),
          "Add the first Color Range sample from the red source patch.");
  settle(editor, "The first Color Range swatch must publish before editing its radius.");
  editor.setActiveMaskHueRange(0.065);
  settle(editor, "The edited per-pick hue radius must settle before adding more samples.");
  require(editor.sampleActiveColorRangeAt(green_x, sample_y) &&
              editor.sampleActiveColorRangeAt(blue_x, sample_y),
          "Add disjoint green and blue Color Range samples.");
  settle(editor, "The three-swatch Color Range selection must settle.");

  auto sampled_state = recipe(editor);
  auto sampled_mask = sampled_state.mask_stack.last().toMap();
  require(sampled_mask.value(QStringLiteral("colorSampleMode")).toString() == QStringLiteral("union") &&
              sampled_mask.value(QStringLiteral("colorSamples")).toList().size() == 3 &&
              std::abs(sampled_mask.value(QStringLiteral("hueRange")).toDouble() - 0.065) < 1e-6,
          "Additive multi-pick must persist the explicit union mode and preserve the editable radius.");
  auto red_coverage = evaluate_at_sample(path, sampled_state, sampled_mask, red_x, sample_y);
  auto green_coverage = evaluate_at_sample(path, sampled_state, sampled_mask, green_x, sample_y);
  auto blue_coverage = evaluate_at_sample(path, sampled_state, sampled_mask, blue_x, sample_y);
  require(red_coverage.value >= 250 && green_coverage.value >= 250 && blue_coverage.value >= 250,
          "Each disjoint authored color sample must remain fully selected in the soft union.");
  const auto baseline_state = sampled_state;
  const auto baseline = render_state(path, asset, baseline_state, 1921);
  require(baseline.success, "Render the ordinary three-swatch Color Range baseline.");
  const auto expected_masks = sampled_state.mask_stack;
  assert_history_keeps_range_source(editor, path, expected_masks, green_x, sample_y);

  require(editor.removeActiveColorRangeSample(1), "Remove only the green Color Range swatch.");
  settle(editor, "The reduced two-swatch Color Range must settle after removal.");
  const auto removed_state = recipe(editor);
  const auto removed_mask = removed_state.mask_stack.last().toMap();
  red_coverage = evaluate_at_sample(path, removed_state, removed_mask, red_x, sample_y);
  green_coverage = evaluate_at_sample(path, removed_state, removed_mask, green_x, sample_y);
  blue_coverage = evaluate_at_sample(path, removed_state, removed_mask, blue_x, sample_y);
  require(removed_mask.value(QStringLiteral("colorSamples")).toList().size() == 2 &&
              removed_mask.value(QStringLiteral("colorSampleMode")).toString() == QStringLiteral("union") &&
              red_coverage.value >= 250 && green_coverage.value <= 2 && blue_coverage.value >= 250,
          "Removing one swatch must recompute the union from the remaining source-appearance samples.");
  const auto removed_render = render_state(path, asset, removed_state, 1923);
  require(removed_render.success &&
              rgb_distance(baseline.preview_image.pixelColor(red_coverage.x, red_coverage.y),
                           removed_render.preview_image.pixelColor(red_coverage.x, red_coverage.y)) > 20 &&
              rgb_distance(baseline.preview_image.pixelColor(green_coverage.x, green_coverage.y),
                           removed_render.preview_image.pixelColor(green_coverage.x, green_coverage.y)) <= 3 &&
              rgb_distance(baseline.preview_image.pixelColor(blue_coverage.x, blue_coverage.y),
                           removed_render.preview_image.pixelColor(blue_coverage.x, blue_coverage.y)) > 20,
          "Removing a swatch must remove only that hue from the attached rendered adjustment.");

  require(editor.sampleActiveColorRangeAt(green_x, sample_y),
          "Re-add the removed green swatch from the current adjusted source.");
  settle(editor, "The restored three-swatch Color Range must settle before rendering.");
  sampled_state = recipe(editor);
  sampled_mask = sampled_state.mask_stack.last().toMap();
  red_coverage = evaluate_at_sample(path, sampled_state, sampled_mask, red_x, sample_y);
  green_coverage = evaluate_at_sample(path, sampled_state, sampled_mask, green_x, sample_y);
  blue_coverage = evaluate_at_sample(path, sampled_state, sampled_mask, blue_x, sample_y);
  require(sampled_mask.value(QStringLiteral("colorSamples")).toList().size() == 3 &&
              std::abs(sampled_mask.value(QStringLiteral("hueRange")).toDouble() - 0.065) < 1e-6 &&
              red_coverage.value >= 250 && green_coverage.value >= 250 && blue_coverage.value >= 250,
          "Re-adding a swatch must restore its coverage without resetting the selected tolerance.");
  const auto adjusted_state = recipe(editor);
  const auto adjusted = render_state(path, asset, adjusted_state, 1924);
  require(adjusted.success && adjusted.preview_image.size() == baseline.preview_image.size(),
          "Render the attached multi-pick Color Range adjustment.");
  for (const auto& coverage : {red_coverage, green_coverage, blue_coverage}) {
    require(rgb_distance(baseline.preview_image.pixelColor(coverage.x, coverage.y),
                         adjusted.preview_image.pixelColor(coverage.x, coverage.y)) > 20,
            "The attached multi-pick adjustment must affect every sampled hue in the rendered preview.");
  }
  require(rgb_distance(baseline.preview_image.pixelColor(2, green_coverage.y),
                       adjusted.preview_image.pixelColor(2, green_coverage.y)) <= 3,
          "The soft union must leave a neutral out-of-range background pixel unchanged.");

  const auto baseline_export = render_export_state(path, asset, source, baseline_state);
  const auto adjusted_export = render_export_state(path, asset, source, adjusted_state);
  require(!baseline_export.isNull() && !adjusted_export.isNull(),
          "Render the multi-pick Color Range through the actual export pipeline.");
  const std::array<CoverageAt, 3> export_samples{red_coverage, green_coverage, blue_coverage};
  auto export_union_passed = true;
  for (int sample_index = 0; sample_index < 3; ++sample_index) {
    const auto& coverage = export_samples[static_cast<std::size_t>(sample_index)];
    const auto before_pixel = baseline_export.pixelColor(coverage.x, coverage.y);
    const auto after_pixel = adjusted_export.pixelColor(coverage.x, coverage.y);
    const auto delta = rgb_distance(before_pixel, after_pixel);
    if (delta <= 20) {
      export_union_passed = false;
      std::cerr << "ordinary union export diagnostic: sample=" << sample_index
                << " output=" << adjusted_export.width() << 'x' << adjusted_export.height()
                << " coverageSize=" << coverage.size.width() << 'x' << coverage.size.height()
                << " pixel=" << coverage.x << ',' << coverage.y << " alpha=" << coverage.value
                << " delta=" << delta << " before=" << before_pixel.red() << ','
                << before_pixel.green() << ',' << before_pixel.blue() << ',' << before_pixel.alpha()
                << " after=" << after_pixel.red() << ',' << after_pixel.green() << ','
                << after_pixel.blue() << ',' << after_pixel.alpha();
      const auto source_x = crop_rect_for_image(source.size(), adjusted_state).x() + coverage.x;
      const auto source_y = crop_rect_for_image(source.size(), adjusted_state).y() + coverage.y;
      if (source_x >= 0 && source_y >= 0 && source_x < source.width() && source_y < source.height()) {
        const auto source_pixel = source.pixelColor(source_x, source_y);
        std::cerr << " source=" << source_pixel.red() << ',' << source_pixel.green() << ','
                  << source_pixel.blue() << ',' << source_pixel.alpha();
      }
      std::cerr << '\n';
    }
  }
  if (!export_union_passed && baseline_export.size() == adjusted_export.size()) {
    auto maximum_delta = 0;
    QPoint maximum_delta_point;
    for (int y = 0; y < adjusted_export.height(); ++y) {
      for (int x = 0; x < adjusted_export.width(); ++x) {
        const auto delta = rgb_distance(baseline_export.pixelColor(x, y),
                                        adjusted_export.pixelColor(x, y));
        if (delta > maximum_delta) {
          maximum_delta = delta;
          maximum_delta_point = QPoint(x, y);
        }
      }
    }
    std::cerr << "ordinary union export diagnostic: maxDelta=" << maximum_delta
              << " at=" << maximum_delta_point.x() << ',' << maximum_delta_point.y()
              << " baselineSize=" << baseline_export.width() << 'x' << baseline_export.height()
              << " adjustedSize=" << adjusted_export.width() << 'x' << adjusted_export.height() << '\n';
  }
  require(export_union_passed,
          "Export must apply the same union coverage to every sampled hue.");
  const auto reopened = parse_edit_recipe_json(serialize_edit_recipe_json(adjusted_state));
  require(reopened.mask_stack == adjusted_state.mask_stack &&
              reopened.layers == adjusted_state.layers,
          "Canonical recipe reopen must retain union mode, sample order, controls, and local adjustment.");
  const auto reopened_export = render_export_state(path, asset + QStringLiteral("-reopened"), source,
                                                   reopened);
  require(!reopened_export.isNull() && reopened_export == adjusted_export,
          "Reopened multi-pick Color Range export must be pixel-identical.");
  const auto legacy = parse_adjustment_patch_json(serialize_adjustment_patch_json(
      adjusted_state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound));
  require(legacy.ok() && legacy.state->mask_stack == adjusted_state.mask_stack,
          "Legacy adjustment-patch reopen must retain explicit multi-pick Color Range semantics.");
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  QTemporaryDir runtime;
  require(runtime.isValid(), "Create a private ordinary range test directory.");
  ScopedEnvironment disable_persistence(
      "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));
  ScopedEnvironment app_data_root(
      "PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  try {
    test_same_name_unlinked_range_does_not_require_full_source(runtime);
    test_luminance_range_sampling_uses_render_input(runtime);
    test_color_range_sampling_uses_render_input(runtime);
    test_color_range_multi_pick_union_and_removal(runtime);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Ordinary range sampling workflow passed.\n";
  return 0;
}
