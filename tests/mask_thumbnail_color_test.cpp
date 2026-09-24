#include "canonical_composite_mask_alpha.h"
#include "edit_recipe_pipeline_bridge.h"
#include "mask_range_preview_math.h"
#include "mask_thumbnail_payload_service.h"
#include "photo_render_utils.h"
#include "thumbnail_worker_renderer_factory.h"
#include "working_image.h"
#include "desktop_pipeline_state_compiler.h"
#include "photo_editor/pipeline/local_mask_sampling.h"
#include "recipe_mask_sampling.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

using namespace photo_editor::desktop;
using namespace photo_editor::pipeline;

namespace {
QImage thumbnail(const QVariantMap& entry) {
  return QImage::fromData(QByteArray::fromBase64(
      entry.value("thumbnailSource").toString().section(',', 1).toLatin1()), "PNG");
}

QImage alpha_thumbnail(const std::vector<std::uint8_t>& alpha, const QSize& size) {
  const auto evaluation = evaluate_mask_alpha(size, 1, [&](int x, int y, double, double) {
    return double(alpha[static_cast<std::size_t>(y * size.width() + x)]) / 255.0;
  });
  return scaled_square_thumbnail_image(grayscale_mask_alpha_image(evaluation), kMaskThumbnailBackingEdge);
}

bool check_stack(ThumbnailRefreshQueueRequest request, const WorkingImage& look,
                 const ColorAdjustmentState& pipeline, const QImage& decoded,
                 const DecodedImageMetadata& metadata) {
  const auto size = QSize(look.width, look.height);
  const QVariantMap first{{"id", "first"}, {"maskSetId", "first-set"},
      {"kind", "luminance"}, {"toolRole", "selection"},
      {"luminanceMin", 0.0}, {"luminanceMax", 1.0}, {"exposure", 1.2}};
  const QVariantMap second{{"id", "second"}, {"maskSetId", "second-set"},
      {"kind", "color"}, {"toolRole", "selection"},
      {"hueCenter", 0.15}, {"hueRange", 0.3}, {"exposure", -0.7}, {"temperature", -25.0}};
  const QVariantMap target{{"id", "target"}, {"maskSetId", "target-set"},
      {"kind", "luminance"}, {"toolRole", "selection"},
      {"luminanceMin", 0.2}, {"luminanceMax", 0.65}, {"rangeFeather", 0.15},
      {"exposure", 0.4},
      {"maskSetSelectionInvert", true}, {"maskSetOpacity", 0.8}, {"maskSetDensity", 0.65},
      {"maskSetSelectionExpand", 12.0}, {"maskSetSelectionSmooth", 20.0},
      {"maskSetSelectionFineEdge", 15.0}, {"maskSetSelectionEdgeContrast", 10.0}};
  const auto renderers = make_thumbnail_worker_renderers({});
  int prefix_changed = 0;
  for (int variant = 0; variant < 5; ++variant) {
    auto first_variant = first;
    auto target_variant = target;
    if (variant == 1) first_variant.insert("enabled", false);
    if (variant == 2) first_variant.insert("maskSetPreviewBypassed", true);
    if (variant == 3) target_variant.insert("solo", true);
    request.state.mask_stack = variant == 4 ? QVariantList{target_variant, first_variant, second}
        : QVariantList{first_variant, second, target_variant};
    // Only the target row is requested; rendering order comes from the recipe,
    // not from the rows being refreshed or from their presentation order.
    request.mask_sets = {QVariantMap{{"id", "target-set"}, {"sources", QVariantList{target_variant}}}};
    request.job.state_signature += QString::number(variant);
    const auto entries = renderers.mask_thumbnail_entries(request);
    if (entries.size() != 2) {
      std::cerr << "Stack entry count=" << entries.size() << " variant=" << variant << '\n';
      return false;
    }
    const auto compiled = compile_pipeline_local_mask_sets(request.state.mask_stack,
        DesktopRenderPurpose::Preview, LocalMaskRasterPolicy::RebuildEditableAtOutputResolution, size);
    const auto wanted = std::find_if(compiled.begin(), compiled.end(), [](const auto& set) {
      return set.id == "target-set";
    });
    if (wanted == compiled.end()) {
      std::cerr << "Missing compiled target, sets=" << compiled.size() << " variant=" << variant << '\n';
      for (const auto& set : compiled) std::cerr << set.id << '\n';
      return false;
    }
    const auto overlay_sampling = prepare_recipe_mask_sampling(decoded, request.state, metadata,
        target_variant, crop_rect_for_image(decoded.size(), request.state));
    const auto overlay_alpha = overlay_sampling.evaluate(wanted->sources);
    if (!overlay_alpha.valid) return false;
    auto prefix = pipeline;
    prefix.local_mask_sets.assign(compiled.begin(), wanted);
    // Disabled sets remain in the canonical stack with zero effect; preview
    // bypass and solo filtering remove sets from it.
    const auto expected_prefix_size = variant < 2 ? 2U : variant == 2 ? 1U : 0U;
    if (prefix.local_mask_sets.size() != expected_prefix_size) {
      std::cerr << "Unexpected prefix size=" << prefix.local_mask_sets.size() << " variant=" << variant << '\n';
      return false;
    }
    auto adjusted = look.pixels;
    if (!apply_color_layers_stage(&adjusted, look.width, look.height, prefix)) return false;
    for (const auto& value : entries) {
      const auto entry = value.toMap();
      auto mask = *wanted;
      if (entry.value("id").toString() == "target") {
        mask = LocalMaskSet{};
        mask.sources = wanted->sources;
      }
      const auto expected_alpha = rasterize_local_mask_set_alpha_with_context(
          mask, look.width, look.height, adjusted, look.pixels, pipeline.color_pipeline);
      if (entry.value("id").toString() == "target-set") {
        if (overlay_alpha.alpha_mask.size() != static_cast<qsizetype>(expected_alpha.size())) return false;
        for (std::size_t i = 0; i < expected_alpha.size(); ++i)
          if (static_cast<unsigned char>(overlay_alpha.alpha_mask[static_cast<qsizetype>(i)]) != expected_alpha[i]) {
            std::cerr << "Recipe overlay sampler differs from the engine stack\n";
            return false;
          }
      }
      const auto expected = alpha_thumbnail(expected_alpha, size);
      const auto actual = thumbnail(entry);
      if (actual.size() != expected.size()) return false;
      for (int y = 0; y < actual.height(); ++y)
        for (int x = 0; x < actual.width(); ++x)
          if (actual.pixelColor(x, y).alpha() != expected.pixelColor(x, y).alpha()) {
            std::cerr << "Stack mismatch variant=" << variant << " id="
                      << entry.value("id").toString().toStdString() << '\n';
            return false;
          }
      if (variant == 0) {
        const auto unadjusted = alpha_thumbnail(rasterize_local_mask_set_alpha(
            mask, look.width, look.height, look.pixels, pipeline.color_pipeline), size);
        for (int y = 0; y < actual.height(); ++y)
          for (int x = 0; x < actual.width(); ++x)
            prefix_changed += actual.pixelColor(x, y).alpha() != unadjusted.pixelColor(x, y).alpha();
        const auto output = qEnvironmentVariable("PHOTO_EDITOR_TEST_MASK_OUTPUT");
        if (!output.isEmpty() && entry.value("id").toString() == "target-set" &&
            request.state.working_color_profile == "Linear ProPhoto RGB" && request.state.crop_rect_width == 0) {
          if (!actual.save(output + "-stack.png") || !unadjusted.save(output + "-stack-old-input.png")) return false;
        }
      }
    }
    if (variant == 0) {
      auto reversed_rows = request;
      reversed_rows.mask_sets.append(QVariantMap{{"id", "second-set"}, {"sources", QVariantList{second}}});
      reversed_rows.mask_sets.append(QVariantMap{{"id", "first-set"}, {"sources", QVariantList{first_variant}}});
      const auto reversed_entries = renderers.mask_thumbnail_entries(reversed_rows);
      if (reversed_entries.size() != 6) return false;
      for (auto entry = entries.constBegin(); entry != entries.constEnd(); ++entry) {
        if (thumbnail(entry.value().toMap()) != thumbnail(reversed_entries.value(entry.key()).toMap())) {
          std::cerr << "Presentation order changed the target's mask coverage\n";
          return false;
        }
      }
    }
  }
  std::cout << "Stack prefix/group refinement exact; old-input changed=" << prefix_changed << '\n';
  return prefix_changed > 0;
}

bool check_engine_edge_context() {
  constexpr int width = 32;
  constexpr int height = 16;
  std::vector<ColorPixel> look(static_cast<std::size_t>(width * height));
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const auto value = 0.05F + 0.015F * static_cast<float>(x);
      look[static_cast<std::size_t>(y * width + x)] = {value, value * 0.8F, value * 0.6F, 1.0F};
    }
  LocalMask first_source;
  first_source.kind = "linear";
  first_source.position = 0.5;
  first_source.feather = 0.2;
  LocalMaskSet first;
  first.exposure = 2.0;
  first.sources = {first_source};
  LocalMask edge;
  edge.kind = "brush";
  edge.segmentation_width = width;
  edge.segmentation_height = height;
  edge.selection_fine_edge = 100.0;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      edge.segmentation_mask.push_back(static_cast<std::uint8_t>(x < 15 ? 0 : x == 15 ? 64 : x == 16 ? 192 : 255));
  LocalMaskSet target;
  target.exposure = 0.7;
  target.sources = {edge};
  ColorAdjustmentState state;
  state.local_mask_sets = {first};
  auto before = look;
  if (!apply_color_layers_stage(&before, width, height, state)) return false;
  auto after = look;
  state.local_mask_sets.push_back(target);
  if (!apply_color_layers_stage(&after, width, height, state)) return false;
  auto resumed = before;
  auto continuation = state;
  continuation.local_mask_sets = {target};
  if (!apply_local_adjustments_to_look_pixels_with_context(
          &resumed, width, height, continuation, look)) return false;
  for (std::size_t i = 0; i < after.size(); ++i) {
    if (after[i].red != resumed[i].red || after[i].green != resumed[i].green ||
        after[i].blue != resumed[i].blue || after[i].alpha != resumed[i].alpha) return false;
  }
  LocalMask full_source;
  full_source.kind = "luminance";
  full_source.luminance_min = 0;
  full_source.luminance_max = 1;
  auto full_target = target;
  full_target.sources = {full_source};
  state.local_mask_sets = {full_target};
  auto full_effect = before;
  if (!apply_color_layers_stage(&full_effect, width, height, state)) return false;
  const auto actual = rasterize_local_mask_set_alpha_with_context(
      target, width, height, before, look, state.color_pipeline);
  const auto wrong_context = rasterize_local_mask_set_alpha(
      target, width, height, before, state.color_pipeline);
  int changed = 0;
  int maximum_delta = 0;
  for (std::size_t i = 0; i < actual.size(); ++i) {
    // The full color-stage result is blended by mask coverage. Recover that
    // weight independently from rendered pixels, without calling a mask oracle.
    const auto denominator = double(full_effect[i].red) - before[i].red;
    if (std::abs(denominator) < 1e-5) return false;
    const auto inferred = std::lround(std::clamp(
        (double(after[i].red) - before[i].red) / denominator, 0.0, 1.0) * 255.0);
    maximum_delta = std::max(maximum_delta, std::abs(int(inferred) - int(actual[i])));
    changed += actual[i] != wrong_context[i];
  }
  std::cout << "Independent rendered edge alpha max delta=" << maximum_delta
            << " wrong-edge-reference changed=" << changed << '\n';
  return actual.size() == look.size() && maximum_delta <= 1 && changed > 0 &&
      rasterize_local_mask_set_alpha_with_context(target, width, height,
          before, {}, state.color_pipeline).empty();
}

bool check(const QString& path, const QString& profile, bool crop) {
  ThumbnailRefreshQueueRequest request;
  request.job.file_path = path;
  request.job.asset_id = "mask-color-proof";
  request.job.state_signature = profile + (crop ? "crop" : "full");
  request.cancellation = std::make_shared<std::atomic_bool>(false);
  auto& state = request.state;
  state.working_color_profile = profile;
  state.exposure = 0.7;
  state.contrast = 35;
  state.shadows = 25;
  state.saturation = 30;
  state.temperature = 25;
  state.tint = 12;
  if (crop) {
    state.crop_rect_x = 0.2;
    state.crop_rect_y = 0.1;
    state.crop_rect_width = 0.6;
    state.crop_rect_height = 0.7;
  }
  const QVariantList sources{
      QVariantMap{{"id", "luma"}, {"kind", "luminance"},
          {"luminanceMin", 0.2}, {"luminanceMax", 0.65}, {"rangeFeather", 0.15}},
      QVariantMap{{"id", "color"}, {"kind", "color"},
          {"hueCenter", 0.07}, {"hueRange", 0.15}, {"rangeFeather", 0.1}}};
  request.mask_sets = {QVariantMap{{"id", "set"}, {"sources", sources}}};
  const auto renderers = make_thumbnail_worker_renderers({});
  const auto entries = renderers.mask_thumbnail_entries(request);
  if (entries.size() != 3) {
    std::cerr << "Expected three mask entries, got " << entries.size() << '\n';
    return false;
  }

  // Integration oracle: the engine's public pre-layer stages and mask
  // rasterizer, starting with full-frame linearization before cropping.
  QImage decoded;
  DecodedImageMetadata metadata;
  if (!decode_source_image(path, &decoded, kMaskThumbnailBackingEdge, state, &metadata)) return false;
  if (path.endsWith(".dng", Qt::CaseInsensitive) &&
      (!metadata.is_raw || !decoded_image_requires_engine_input_transform(metadata))) {
    std::cerr << "DNG proof must exercise camera-linear RAW decoding\n";
    return false;
  }
  const auto pipeline = compile_base_color_adjustment_state(state, &metadata);
  auto full = working_image_from_qimage(decoded, profile,
      &pipeline.color_pipeline.input_icc_profile_asset,
      decoded_image_requires_engine_input_transform(metadata));
  auto working = crop_working_image(full, crop_rect_for_image(decoded.size(), state));
  // Preserve source-scoped highlight statistics for the crop, as the executor does.
  auto prefix = prepare_color_prefix_stage_context(full.pixels, full.width, full.height, pipeline);
  if (!apply_color_normalize_raw_stage(&working.pixels, working.width, working.height, pipeline, &prefix) ||
      !apply_color_camera_characterization_stage(&working.pixels, working.width, working.height, pipeline, prefix)) return false;
  // Compare final byte masks, not display RGB.
  if (!apply_color_scene_adjustments_stage(&working.pixels, working.width, working.height, pipeline) ||
      !apply_color_tone_stage(&working.pixels, working.width, working.height, pipeline) ||
      !apply_color_curves_stage(&working.pixels, working.width, working.height, pipeline) ||
      !apply_color_look_stage(&working.pixels, working.width, working.height, pipeline)) return false;

  int legacy_changed = 0;
  for (const auto& value : entries) {
    const auto entry = value.toMap();
    const auto id = entry.value("id").toString();
    const QVariantList masks = id == "set" ? sources : QVariantList{
        sources.at(id == "luma" ? 0 : 1)};
    LocalMaskSet set;
    for (const auto& mask : masks) set.sources.push_back(compile_pipeline_local_mask(
        mask.toMap(), LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,
        QSize(working.width, working.height)));
    const auto alpha = rasterize_local_mask_set_alpha(
        set, working.width, working.height, working.pixels, pipeline.color_pipeline);
    const auto evaluation = evaluate_mask_alpha(QSize(working.width, working.height),
        static_cast<int>(masks.size()), [&](int x, int y, double, double) {
          return double(alpha[static_cast<std::size_t>(y * working.width + x)]) / 255.0;
        });
    const auto reference = scaled_square_thumbnail_image(
        grayscale_mask_alpha_image(evaluation), kMaskThumbnailBackingEdge);
    const auto actual = thumbnail(entry);
    if (actual.size() != reference.size() || !entry.value("thumbnailReady").toBool() ||
        !entry.value("maskAlphaProviderCacheKey").toString().startsWith("sha1:") ||
        !entry.value("thumbnailMaskInputContract").toString().contains("through-look")) return false;
    const auto legacy = thumbnail(canonical_mask_thumbnail_payload(
        scaled_square_thumbnail_image(apply_crop(decoded, state), kMaskThumbnailBackingEdge),
        masks, std::pow(2.0, state.exposure), preview_white_balance_multipliers(state.temperature, state.tint)));
    for (int y = 0; y < actual.height(); ++y) {
      for (int x = 0; x < actual.width(); ++x) {
        const auto color = actual.pixelColor(x, y);
        if (color.red() != color.alpha() || color.green() != color.alpha() ||
            color.blue() != color.alpha() || color.alpha() != reference.pixelColor(x, y).alpha()) {
          std::cerr << "Mask alpha differs for " << id.toStdString() << " at " << x << ',' << y << '\n';
          return false;
        }
        legacy_changed += actual.pixelColor(x, y).alpha() != legacy.pixelColor(x, y).alpha();
      }
    }
    const auto output = qEnvironmentVariable("PHOTO_EDITOR_TEST_MASK_OUTPUT");
    if (!output.isEmpty() && id == "luma" && !crop && profile == "Linear ProPhoto RGB") {
      if (!actual.save(output + "-mask.png") || !legacy.save(output + "-legacy-mask.png")) return false;
    }
  }
  std::cout << path.toStdString() << " raw=" << metadata.is_raw << " " << profile.toStdString() << " crop=" << crop
            << " exact alpha; legacy changed=" << legacy_changed << '\n';
  if (legacy_changed == 0) return false;
  if (!check_stack(request, working, pipeline, decoded, metadata)) return false;
  request.cancellation->store(true);
  return renderers.mask_thumbnail_entries(request).isEmpty();
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  MaskAlphaEvaluationResult invalid;
  invalid.valid = true;
  invalid.size = QSize(96, 96);
  if (!mask_thumbnail_payload_from_alpha(invalid).isEmpty()) return 1;
  if (!check_engine_edge_context()) return 1;
  QStringList paths;
  for (const auto space : {QColorSpace::SRgb, QColorSpace::DisplayP3}) {
    QImage image(96, 80, QImage::Format_RGB32);
    image.setColorSpace(QColorSpace(space));
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x)
        image.setPixelColor(x, y, QColor(10 + x * 2, 10 + y * 3, 35 + x));
    const auto path = root.filePath(QString::number(static_cast<int>(space)) + ".jpg");
    if (!image.save(path)) return 1;
    paths.append(path);
  }
  for (int i = 1; i < argc; ++i) paths.append(QString::fromLocal8Bit(argv[i]));
  for (const auto& path : paths)
    for (const auto* profile : {"Linear sRGB", "Linear Display P3", "Linear Rec.2020", "Linear ProPhoto RGB"})
      for (const auto crop : {false, true})
        if (!check(path, QString::fromLatin1(profile), crop)) return 1;
  std::cout << "Mask thumbnail color, crop and cancellation checks passed.\n";
  return 0;
}
