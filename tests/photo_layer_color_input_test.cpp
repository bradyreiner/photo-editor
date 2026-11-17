#include "desktop_pipeline_state_compiler.h"
#include "photo_layer_compiler.h"
#include "photo_layer_source.h"
#include "photo_render_utils.h"
#include "working_image.h"
#include "photo_layer_effect_codec.h"
#include "photo_text_style.h"
#include "photo_editor/pipeline/color_profile_asset.h"
#include "photo_editor/pipeline/color_transform.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <memory>
#include <QTemporaryDir>
#include <iostream>
#include <cstring>

int image_delta(const QImage& a, const QImage& b) {
  if (a.isNull() || a.size() != b.size()) return 255;
  const auto left = a.convertToFormat(QImage::Format_RGBA8888);
  const auto right = b.convertToFormat(QImage::Format_RGBA8888);
  int worst = 0;
  for (int y = 0; y < left.height(); ++y)
    for (int x = 0; x < left.width() * 4; ++x)
      worst = std::max(worst, std::abs(int(left.constScanLine(y)[x]) - int(right.constScanLine(y)[x])));
  return worst;
}

using namespace photo_editor::desktop;
using namespace photo_editor::pipeline;

int main(int argc, char** argv) {
  const bool gui = argc == 2 && QString::fromLocal8Bit(argv[1]) == "--with-gui";
  std::unique_ptr<QCoreApplication> app;
  if (gui) app = std::make_unique<QGuiApplication>(argc, argv);
  else app = std::make_unique<QCoreApplication>(argc, argv);
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  QImage source(4, 2, QImage::Format_ARGB32);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  source.fill(QColor("#b06030"));
  QByteArray png;
  QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
  if (!source.save(&buffer, "PNG") || !source.save(root.filePath("placed.png"))) return 1;
  {
    QImage prepared_source(32, 24, QImage::Format_RGB32);
    prepared_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
    for (int y = 0; y < prepared_source.height(); ++y) {
      for (int x = 0; x < prepared_source.width(); ++x) {
        prepared_source.setPixelColor(x, y, QColor(20 + x * 5, 18 + y * 7,
            35 + (x + y) * 3));
      }
    }
    const auto prepared_source_path = root.filePath("prepared-placed-source.bmp");
    if (!prepared_source.save(prepared_source_path, "BMP")) return 1;

    EditRecipeState prepared_source_recipe;
    prepared_source_recipe.photographic_rendering_model = QStringLiteral("legacy-oklch-v1");
    const auto prepared_source_recipe_json = serialize_edit_recipe_json(prepared_source_recipe);
    QVariantMap prepared_layer{
        {"id", "prepared-source"}, {"kind", "source"},
        {"sourcePath", prepared_source_path}, {"sourceAssetId", "prepared-source-test"},
        {"sourceRecipeJson", prepared_source_recipe_json},
        {"sourcePixelWidth", 32}, {"sourcePixelHeight", 24},
        {"pixelWidth", 32}, {"pixelHeight", 24},
        {"canvasWidth", 128}, {"canvasHeight", 96},
        {"x", 16}, {"y", 24}, {"scale", 2.0}, {"rotation", 15.0}, {"opacity", 0.75},
        {"boardPhotoCrop", QVariantMap{{"x", 0.25}, {"y", 0.25},
            {"width", 0.5}, {"height", 0.5}}},
    };
    const auto compile_prepared_source = [&](const QVariantMap& layer,
                                                const QString& profile,
                                                const QSize& output_size) {
      ColorPipelineConfig config;
      config.working_profile = working_color_profile_from_name(profile.toStdString());
      return compile_pipeline_photo_layers({layer}, {}, LocalMaskRasterPolicy::UseStoredRaster,
          output_size, {}, config, DesktopRenderPurpose::Preview);
    };
    const auto same_source_pixels = [](const auto& left, const auto& right) {
      return left.size() == 1 && right.size() == 1 &&
          left[0].pixel_width == right[0].pixel_width &&
          left[0].pixel_height == right[0].pixel_height &&
          left[0].pixels.size() == right[0].pixels.size() && !left[0].pixels.empty() &&
          std::memcmp(left[0].pixels.data(), right[0].pixels.data(),
              left[0].pixels.size() * sizeof(ColorPixel)) == 0;
    };

    const auto first_prepared_source = compile_prepared_source(prepared_layer,
        QStringLiteral("Linear sRGB"), QSize(128, 96));
    if (first_prepared_source.size() != 1 || first_prepared_source[0].content_error.size() != 0 ||
        first_prepared_source[0].pixel_width != 32 || first_prepared_source[0].pixel_height != 24 ||
        first_prepared_source[0].x != 32 || first_prepared_source[0].y != 36 ||
        first_prepared_source[0].transform_scale != 1.0 ||
        first_prepared_source[0].transform_rotation != 15.0) {
      std::cerr << "Prepared source cache must retain framed pixels and crop placement metadata\n";
      return 1;
    }

    auto moved_prepared_layer = prepared_layer;
    moved_prepared_layer.insert("x", 42);
    moved_prepared_layer.insert("y", -6);
    moved_prepared_layer.insert("rotation", 31.0);
    moved_prepared_layer.insert("opacity", 0.3);
    const auto moved_prepared_source = compile_prepared_source(moved_prepared_layer,
        QStringLiteral("Linear sRGB"), QSize(128, 96));
    if (!same_source_pixels(first_prepared_source, moved_prepared_source) ||
        moved_prepared_source[0].x != 58 || moved_prepared_source[0].y != 6 ||
        moved_prepared_source[0].transform_rotation != 31.0 ||
        moved_prepared_source[0].opacity != 0.3) {
      std::cerr << "Moving or styling a framed source must preserve its exact prepared pixels\n";
      return 1;
    }

    auto rescaled_prepared_layer = prepared_layer;
    rescaled_prepared_layer.insert("scale", 1.25);
    const auto rescaled_prepared_source = compile_prepared_source(rescaled_prepared_layer,
        QStringLiteral("Linear sRGB"), QSize(128, 96));
    if (rescaled_prepared_source.size() != 1 ||
        rescaled_prepared_source[0].pixel_width != 20 ||
        rescaled_prepared_source[0].pixel_height != 15 ||
        rescaled_prepared_source[0].x != 26 || rescaled_prepared_source[0].y != 32 ||
        rescaled_prepared_source[0].transform_scale != 1.0) {
      std::cerr << "Framed source raster reuse must key the placement scale\n";
      return 1;
    }

    auto recropped_prepared_layer = prepared_layer;
    recropped_prepared_layer.insert("boardPhotoCrop", QVariantMap{
        {"x", 0.5}, {"y", 0.25}, {"width", 0.25}, {"height", 0.5}});
    const auto recropped_prepared_source = compile_prepared_source(recropped_prepared_layer,
        QStringLiteral("Linear sRGB"), QSize(128, 96));
    if (recropped_prepared_source.size() != 1 ||
        recropped_prepared_source[0].pixel_width != 16 ||
        recropped_prepared_source[0].pixel_height != 24 ||
        recropped_prepared_source[0].x != 48 || recropped_prepared_source[0].y != 36 ||
        same_source_pixels(first_prepared_source, recropped_prepared_source)) {
      std::cerr << "Changing a board photo crop must rebuild the prepared frame\n";
      return 1;
    }

    auto resized_authored_layer = prepared_layer;
    resized_authored_layer.insert("pixelWidth", 40);
    const auto resized_authored_source = compile_prepared_source(resized_authored_layer,
        QStringLiteral("Linear sRGB"), QSize(128, 96));
    if (resized_authored_source.size() != 1 || resized_authored_source[0].pixel_width != 40 ||
        resized_authored_source[0].pixel_height != 24) {
      std::cerr << "Changing authored source dimensions must rebuild the prepared frame\n";
      return 1;
    }

    const auto larger_raster_source = compile_prepared_source(prepared_layer,
        QStringLiteral("Linear sRGB"), QSize(256, 192));
    if (larger_raster_source.size() != 1 || larger_raster_source[0].pixel_width != 64 ||
        larger_raster_source[0].pixel_height != 48) {
      std::cerr << "Changing requested raster dimensions must rebuild the prepared frame\n";
      return 1;
    }

    const auto p3_prepared_source = compile_prepared_source(prepared_layer,
        QStringLiteral("Linear Display P3"), QSize(128, 96));
    const auto p3_fresh_source_path = root.filePath("prepared-source-profile-fresh.bmp");
    if (!QFile::copy(prepared_source_path, p3_fresh_source_path)) return 1;
    auto p3_fresh_layer = prepared_layer;
    p3_fresh_layer.insert("sourcePath", p3_fresh_source_path);
    const auto p3_fresh_source = compile_prepared_source(p3_fresh_layer,
        QStringLiteral("Linear Display P3"), QSize(128, 96));
    if (p3_prepared_source.size() != 1 || p3_fresh_source.size() != 1 ||
        p3_prepared_source[0].content_error.size() != 0 ||
        !same_source_pixels(p3_prepared_source, p3_fresh_source)) {
      std::cerr << "Prepared source pixels must remain isolated by working profile\n";
      return 1;
    }

    auto exposure_recipe = prepared_source_recipe;
    exposure_recipe.exposure = 1.0;
    auto graded_prepared_layer = prepared_layer;
    graded_prepared_layer.insert("sourceRecipeJson", serialize_edit_recipe_json(exposure_recipe));
    const auto graded_prepared_source = compile_prepared_source(graded_prepared_layer,
        QStringLiteral("Linear sRGB"), QSize(128, 96));
    if (graded_prepared_source.size() != 1 ||
        same_source_pixels(first_prepared_source, graded_prepared_source)) {
      std::cerr << "Changing a source recipe must not reuse stale prepared pixels\n";
      return 1;
    }

    const auto source_file_info = QFileInfo(prepared_source_path);
    const auto source_size_before_rewrite = source_file_info.size();
    const auto source_mtime_before_rewrite = source_file_info.lastModified();
    QImage replacement_source(32, 24, QImage::Format_RGB32);
    replacement_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
    replacement_source.fill(QColor("#e91b31"));
    if (!replacement_source.save(prepared_source_path, "BMP")) return 1;
    QFile changed_source_file(prepared_source_path);
    if (!changed_source_file.open(QIODevice::ReadWrite) ||
        !changed_source_file.setFileTime(QDateTime::currentDateTime().addSecs(5),
            QFileDevice::FileModificationTime)) return 1;
    changed_source_file.close();
    QFileInfo changed_source_info(prepared_source_path);
    changed_source_info.refresh();
    if (changed_source_info.size() != source_size_before_rewrite ||
        changed_source_info.lastModified() == source_mtime_before_rewrite) {
      std::cerr << "Prepared-source invalidation fixture must change mtime while preserving file size\n";
      return 1;
    }
    const auto changed_file_source = compile_prepared_source(prepared_layer,
        QStringLiteral("Linear sRGB"), QSize(128, 96));
    if (changed_file_source.size() != 1 ||
        same_source_pixels(first_prepared_source, changed_file_source)) {
      std::cerr << "Changing a source file's content and mtime must invalidate prepared pixels\n";
      return 1;
    }

    auto& default_profile_registry =
        photo_editor::pipeline::default_icc_profile_asset_registry();
    default_profile_registry.clear();
    const auto external_profile_payload =
        photo_editor::pipeline::builtin_rgb_icc_profile_payload(
            photo_editor::pipeline::make_display_p3_color_space());
    const auto external_profile_path = root.filePath("source-input-profile.icc");
    QFile external_profile_file(external_profile_path);
    if (external_profile_payload.empty() ||
        !external_profile_file.open(QIODevice::WriteOnly) ||
        external_profile_file.write(
            reinterpret_cast<const char*>(external_profile_payload.data()),
            static_cast<qint64>(external_profile_payload.size())) !=
            static_cast<qint64>(external_profile_payload.size())) {
      return 1;
    }
    external_profile_file.close();
    auto external_profile_recipe = prepared_source_recipe;
    external_profile_recipe.output_color_space = QStringLiteral("Display P3");
    external_profile_recipe.export_icc_profile_asset = IccProfileAssetState{
        .asset_id = QStringLiteral("icc:prepared-source-cache-test"),
        .payload_sha256 = QString::fromStdString(
            photo_editor::pipeline::color_payload_sha256(external_profile_payload)),
        .display_name = QStringLiteral("Prepared Source Cache Test"),
        .source_uri = external_profile_path,
        .source_kind = QStringLiteral("test-generated"),
        .byte_size = static_cast<qint64>(external_profile_payload.size()),
    };
    auto external_profile_layer = prepared_layer;
    external_profile_layer.insert("sourceRecipeJson",
        serialize_edit_recipe_json(external_profile_recipe));
    const auto resolved_external_profile_source = compile_prepared_source(
        external_profile_layer, QStringLiteral("Linear sRGB"), QSize(128, 96));
    if (resolved_external_profile_source.size() != 1 ||
        !resolved_external_profile_source[0].content_error.empty()) {
      std::cerr << "A valid external export profile should render before invalidation: "
                << (resolved_external_profile_source.empty() ? "no compiled layer" :
                    resolved_external_profile_source[0].content_error) << '\n';
      default_profile_registry.clear();
      return 1;
    }
    external_profile_file.remove();
    default_profile_registry.clear();
    const auto missing_external_profile_source = compile_prepared_source(
        external_profile_layer, QStringLiteral("Linear sRGB"), QSize(128, 96));
    default_profile_registry.clear();
    if (missing_external_profile_source.size() != 1 ||
        missing_external_profile_source[0].content_error.empty()) {
      std::cerr << "A warm source must not hide a missing external profile dependency\n";
      return 1;
    }

    auto invalid_recipe_layer = prepared_layer;
    invalid_recipe_layer.insert("sourceRecipeJson", QStringLiteral("not a valid recipe"));
    const auto invalid_recipe_source = compile_prepared_source(invalid_recipe_layer,
        QStringLiteral("Linear sRGB"), QSize(128, 96));
    if (invalid_recipe_source.size() != 1 || invalid_recipe_source[0].content_error.empty()) {
      std::cerr << "Prepared-pixel hits must not bypass invalid source-recipe errors\n";
      return 1;
    }
  }
  if (gui) {
    // Exercise the same source-pixel compiler used by interactive text moves.
    // Placement must change while glyph values remain bit-identical.
    auto moving_text = QVariantMap{{"id", "moving-title"}, {"kind", "text"},
        {"text", "A quiet portrait"}, {"pixelWidth", 720}, {"pixelHeight", 160},
        {"canvasWidth", 1600}, {"canvasHeight", 1000}, {"fontSize", 64},
        {"fontFamily", "Helvetica"}, {"textColor", "#80eaa251"}};
    const auto compile_moving_text = [&] {
      return compile_pipeline_photo_layers({moving_text}, {}, LocalMaskRasterPolicy::UseStoredRaster,
          QSize(1600, 1000), {}, {}, DesktopRenderPurpose::Preview);
    };
    const auto reference_text = compile_moving_text();
    if (reference_text.size() != 1 || reference_text[0].pixels.empty()) return 1;
    constexpr int move_samples = 40;
    QElapsedTimer move_timer;
    move_timer.start();
    for (int sample = 0; sample < move_samples; ++sample) {
      moving_text.insert("x", sample * 3);
      moving_text.insert("y", sample * 2);
      const auto next = compile_moving_text();
      if (next.size() != 1 || next[0].x != sample * 3 || next[0].y != sample * 2 ||
          next[0].pixels.size() != reference_text[0].pixels.size()) return 1;
      if (std::memcmp(next[0].pixels.data(), reference_text[0].pixels.data(),
              next[0].pixels.size() * sizeof(ColorPixel)) != 0) {
        std::cerr << "Moving text must preserve exact source glyph pixels\n";
        return 1;
      }
    }
    std::cout << "text_move_compile samples=" << move_samples
              << " elapsed_ns=" << move_timer.nsecsElapsed() << '\n';
    const auto moving_baseline = moving_text;
    const auto same_pixels = [](const auto& left, const auto& right) {
      return left.size() == 1 && right.size() == 1 &&
          left[0].pixel_width == right[0].pixel_width && left[0].pixel_height == right[0].pixel_height &&
          left[0].pixels.size() == right[0].pixels.size() &&
          std::memcmp(left[0].pixels.data(), right[0].pixels.data(),
              left[0].pixels.size() * sizeof(ColorPixel)) == 0;
    };
    auto changed_style = PhotoTextStyle{};
    changed_style.size = 44.25;
    changed_style.tracking = 2.5;
    changed_style.color = QColor("#70ff5080");
    const QVariantList source_changes{
        QVariantMap{{"text", "A different portrait"}}, QVariantMap{{"fontFamily", "Georgia"}},
        QVariantMap{{"fontSize", 51.5}}, QVariantMap{{"fontWeight", "bold"}},
        QVariantMap{{"textColor", "#4022aacc"}}, QVariantMap{{"textAlignment", "center"}},
        QVariantMap{{"textPerspectiveHorizontal", 0.35}}, QVariantMap{{"pixelWidth", 600}},
        QVariantMap{{"pixelHeight", 220}},
        QVariantMap{{"textTypography", encode_photo_text_style(changed_style).toVariantMap()}}};
    for (const auto& patch : source_changes) {
      moving_text = moving_baseline;
      static_cast<void>(compile_moving_text());
      const auto values = patch.toMap();
      for (auto it = values.cbegin(); it != values.cend(); ++it) moving_text.insert(it.key(), it.value());
      const auto warm_change = compile_moving_text();
      if (!QMetaObject::invokeMethod(app.get(), "fontDatabaseChanged", Qt::DirectConnection)) return 1;
      const auto fresh_change = compile_moving_text();
      if (!same_pixels(warm_change, fresh_change)) {
        std::cerr << "Text source changes must match a fresh compile after font invalidation\n";
        return 1;
      }
    }
    for (const auto& field : {QStringLiteral("text"), QStringLiteral("pixelWidth"), QStringLiteral("fontSize")}) {
      moving_text = moving_baseline;
      moving_text.remove(field);
      static_cast<void>(compile_moving_text());
      moving_text.insert(field, QVariant{});
      const auto explicit_null = compile_moving_text();
      QMetaObject::invokeMethod(app.get(), "fontDatabaseChanged", Qt::DirectConnection);
      if (!same_pixels(explicit_null, compile_moving_text())) {
        std::cerr << "Missing text fields and explicit nulls must keep their distinct raster defaults\n";
        return 1;
      }
    }
    moving_text = moving_baseline;
    for (const auto& profile : {"Linear sRGB", "Linear Display P3", "Linear Rec.2020"}) {
      ColorPipelineConfig profile_config;
      profile_config.working_profile = working_color_profile_from_name(profile);
      const auto warm = compile_pipeline_photo_layers({moving_text}, {}, LocalMaskRasterPolicy::UseStoredRaster,
          QSize(1600, 1000), {}, profile_config, DesktopRenderPurpose::Preview);
      QMetaObject::invokeMethod(app.get(), "fontDatabaseChanged", Qt::DirectConnection);
      const auto fresh = compile_pipeline_photo_layers({moving_text}, {}, LocalMaskRasterPolicy::UseStoredRaster,
          QSize(1600, 1000), {}, profile_config, DesktopRenderPurpose::Preview);
      if (!same_pixels(warm, fresh)) {
        std::cerr << "Text pixel reuse must preserve each working color profile exactly\n";
        return 1;
      }
    }
    const QVariantMap text_layer{{"id", "scaled-text"}, {"kind", "text"}, {"text", "Caption"},
        {"pixelWidth", 200}, {"pixelHeight", 100}, {"canvasWidth", 400}, {"canvasHeight", 400}, {"x", 40}, {"y", 60}};
    const auto compiled = compile_pipeline_photo_layers({text_layer}, {}, LocalMaskRasterPolicy::UseStoredRaster,
        QSize(200, 200), {}, {}, DesktopRenderPurpose::Preview);
    if (compiled.size() != 1 || compiled[0].pixel_width != 100 || compiled[0].pixel_height != 50 ||
        compiled[0].x != 20 || compiled[0].y != 30 || compiled[0].pixels.empty()) return 1;
    const auto enlarged = compile_pipeline_photo_layers({text_layer}, {}, LocalMaskRasterPolicy::UseStoredRaster,
        QSize(800, 1200), {}, {}, DesktopRenderPurpose::Output);
    if (enlarged.size() != 1 || enlarged[0].pixel_width != 400 || enlarged[0].pixel_height != 300 ||
        enlarged[0].x != 80 || enlarged[0].y != 180 || enlarged[0].pixels.empty()) {
      std::cerr << "Text compilation must scale each document axis independently\n";
      return 1;
    }
    auto styled_layer = text_layer;
    PhotoTextStyle typography;
    typography.size = 27.375;
    typography.italic = true;
    typography.underline = true;
    typography.tracking = 1.25;
    typography.color = QColor("#40ff8040");
    styled_layer.insert("textTypography", encode_photo_text_style(typography).toVariantMap());
    EditRecipeState styled_state;
    styled_state.layers = {styled_layer};
    const auto restored_state = parse_edit_recipe_json(serialize_edit_recipe_json(styled_state));
    const auto styled_compiled = compile_pipeline_photo_layers(restored_state.layers, {},
        LocalMaskRasterPolicy::UseStoredRaster, QSize(800, 800), {}, {}, DesktopRenderPurpose::Output);
    if (styled_compiled.size() != 1 || !compiled_photo_layer_source_error(styled_compiled).isEmpty() ||
        styled_compiled[0].pixel_width != 400 || styled_compiled[0].pixel_height != 200 ||
        styled_compiled[0].x != 80 || styled_compiled[0].y != 120)
      return 1;
    float maximum_alpha = 0;
    for (const auto& pixel : styled_compiled[0].pixels)
      maximum_alpha = std::max(maximum_alpha, pixel.alpha);
    if (maximum_alpha <= 0 || maximum_alpha > 64.0F / 255.0F + 1e-6F) {
      std::cerr << "Compiled styled text must retain translucent glyph coverage\n";
      return 1;
    }
    QImage canvas(400, 400, QImage::Format_ARGB32);
    canvas.fill(QColor("#202020"));
    canvas.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const auto original_output = render_output_image_with_backend(canvas, styled_state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    const auto restored_output = render_output_image_with_backend(canvas, restored_state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    EditRecipeState plain_state;
    const auto plain_output = render_output_image_with_backend(canvas, plain_state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    if (original_output.isNull() || image_delta(original_output, restored_output) != 0 ||
        image_delta(original_output, plain_output) == 0) {
      std::cerr << "Styled text must change output pixels and survive recipe round-trip exactly\n";
      return 1;
    }
    styled_layer.insert("textTypography", QVariantMap{{"version", 99}});
    styled_state.layers = {styled_layer};
    if (!render_output_image_with_backend(canvas, styled_state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly).isNull()) {
      std::cerr << "Unsupported active typography must refuse output\n";
      return 1;
    }
    const auto oversized = compile_pipeline_photo_layers({text_layer}, {}, LocalMaskRasterPolicy::UseStoredRaster,
        QSize(40000, 40000), {}, {}, DesktopRenderPurpose::Output);
    if (oversized.size() != 1 || !oversized[0].pixels.empty() ||
        !compiled_photo_layer_source_error(oversized).contains("budget")) {
      std::cerr << "Oversized text must report its budget failure without allocating a raster\n";
      return 1;
    }
  }
  const QStringList profiles{"Linear sRGB", "Linear Display P3", "Linear Rec.2020", "Linear ProPhoto RGB"};
  bool ok = true;
  {
    EditRecipeState text_state;
    text_state.layers = {QVariantMap{{"id", "text-runtime-proof"}, {"kind", "text"}, {"text", "Caption"}}};
    const auto text_output = render_output_image_with_backend(source, text_state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    if (!gui && !text_output.isNull()) {
      std::cerr << "Desktop output must reject text when no GUI font runtime exists\n";
      return 1;
    }
  }
  {
    PhotoEffectInstance blur;
    blur.id = "output-blur";
    blur.input_scope = PhotoEffectInputScope::CompositeBelow;
    blur.gaussian.sigma = 2;
    const std::vector effects{blur};
    const auto payload = encode_photo_layer_effects(effects);
    if (!payload) return 1;
    QVariantMap layer{{"id", "effect-output"}, {"kind", "adjustment"},
        {"canvasWidth", 32}, {"canvasHeight", 32}, {"effectStack", *payload}};
    const auto preview = compile_pipeline_photo_layers({layer}, {}, LocalMaskRasterPolicy::UseStoredRaster,
        QSize(16, 16), {}, {}, DesktopRenderPurpose::Preview);
    if (preview.size() != 1 || preview[0].effects != effects ||
        preview[0].effect_scale_x != 0.5 || preview[0].effect_scale_y != 0.5) return 1;
    auto masked_effects = effects;
    masked_effects[0].mask_ref = "selection-proof";
    const auto masked_payload = encode_photo_layer_effects(masked_effects);
    if (!masked_payload) return 1;
    auto masked_layer = layer;
    masked_layer.insert("effectStack", *masked_payload);
    const QVariantList selection_masks{QVariantMap{{"id", "selection-component"},
        {"kind", "linear"}, {"toolRole", "selection"}, {"maskSetId", "selection-proof"},
        {"maskSetName", "Selection proof"}, {"enabled", true}}};
    const auto masked_compiled = compile_pipeline_photo_layers({masked_layer}, selection_masks,
        LocalMaskRasterPolicy::UseStoredRaster, QSize(32, 32), {}, {}, DesktopRenderPurpose::Output);
    if (masked_compiled.size() != 1 || !masked_compiled[0].effect_error.empty() ||
        masked_compiled[0].effect_masks.size() != 1 ||
        masked_compiled[0].effect_masks[0].coverage.size() != 1024) {
      std::cerr << "Saved selection did not compile into effect coverage\n";
      return 1;
    }
    const auto& coverage = masked_compiled[0].effect_masks[0].coverage;
    const auto bounds = std::minmax_element(coverage.begin(), coverage.end());
    if (*bounds.second - *bounds.first < 0.1F) {
      std::cerr << "Linear selection lost its varying effect coverage\n";
      return 1;
    }
    QImage impulse(32, 32, QImage::Format_ARGB32);
    impulse.fill(Qt::black);
    impulse.setPixelColor(16, 16, Qt::white);
    impulse.setColorSpace(QColorSpace(QColorSpace::SRgb));
    for (const auto& model : {QString("legacy-oklch-v1"), QString("aces2-ocio-v1")}) {
      EditRecipeState state;
      state.photographic_rendering_model = model;
      layer.insert("effectStack", *payload);
      const auto plain = render_output_image_with_backend(impulse, state,
          QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      state.layers = {layer};
      state = parse_edit_recipe_json(serialize_edit_recipe_json(state));
      const auto blurred = render_output_image_with_backend(impulse, state,
          QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      if (blurred.isNull() || plain.isNull() || image_delta(plain, blurred) < 10 ||
          blurred.pixelColor(15, 16).red() <= plain.pixelColor(15, 16).red()) {
        std::cerr << "Persisted Gaussian did not spread the impulse in desktop output\n";
        return 1;
      }

      PhotoEffectInstance diffusion;
      diffusion.id = "f1-optical-diffusion";
      diffusion.type_id = "optical_diffusion";
      diffusion.input_scope = PhotoEffectInputScope::CompositeBelow;
      diffusion.diffusion = PhotoEffectInstance::Diffusion{
          .radius = 4.0,
          .threshold = 0.55,
          .knee = 0.2,
          .strength = 0.9,
          .warmth = 0.25,
          .highlight_protection = 0.0,
      };
      const auto diffusion_payload = encode_photo_layer_effects(std::vector{diffusion});
      QImage f1_source(32, 32, QImage::Format_ARGB32);
      f1_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
      f1_source.fill(Qt::black);
      for (int y = 11; y <= 20; ++y)
        for (int x = 11; x <= 20; ++x)
          f1_source.setPixelColor(x, y, Qt::white);
      if (!diffusion_payload) return 1;
      QVariantMap diffusion_layer{{"id", "f1-optical-diffusion-layer"},
          {"kind", "adjustment"}, {"canvasWidth", 32}, {"canvasHeight", 32},
          {"effectStack", *diffusion_payload}};
      EditRecipeState diffusion_state;
      diffusion_state.photographic_rendering_model = model;
      diffusion_state.layers = {diffusion_layer};
      EditRecipeState diffusion_baseline_state;
      diffusion_baseline_state.photographic_rendering_model = model;
      const auto diffusion_baseline = render_output_image_with_backend(f1_source,
          diffusion_baseline_state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
          RenderBackendPreference::CpuOnly);
      RenderBackendTelemetry diffusion_cpu_telemetry;
      const auto diffusion_cpu = render_output_image_with_backend(f1_source,
          diffusion_state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
          RenderBackendPreference::CpuOnly, nullptr, &diffusion_cpu_telemetry);
      RenderBackendTelemetry diffusion_auto_telemetry;
      const auto diffusion_auto = render_output_image_with_backend(f1_source,
          diffusion_state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
          RenderBackendPreference::Auto, nullptr, &diffusion_auto_telemetry);
      const auto diffusion_reopened_state = parse_edit_recipe_json(
          serialize_edit_recipe_json(diffusion_state));
      const auto diffusion_reopened = render_output_image_with_backend(f1_source,
          diffusion_reopened_state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
          RenderBackendPreference::CpuOnly);
      if (diffusion_cpu.isNull() || diffusion_auto.isNull() || diffusion_reopened.isNull() || diffusion_baseline.isNull() ||
          image_delta(diffusion_cpu, diffusion_baseline) < 10 ||
          image_delta(diffusion_cpu, diffusion_auto) != 0 ||
          image_delta(diffusion_cpu, diffusion_reopened) != 0 ||
          diffusion_auto_telemetry.backend != QStringLiteral("cpu")) {
        std::cerr << "F1 output must render, route to its supported backend, and reopen pixel-identically"
                  << " model=" << model.toStdString()
                  << " cpu_null=" << diffusion_cpu.isNull()
                  << " auto_null=" << diffusion_auto.isNull()
                  << " reopened_null=" << diffusion_reopened.isNull()
                  << " effect_delta=" << image_delta(diffusion_cpu, diffusion_baseline)
                  << " auto_delta=" << image_delta(diffusion_cpu, diffusion_auto)
                  << " reopen_delta=" << image_delta(diffusion_cpu, diffusion_reopened)
                  << " backend=" << diffusion_auto_telemetry.backend.toStdString()
                  << " fallback=" << diffusion_auto_telemetry.fallback_reason.toStdString() << '\n';
        return 1;
      }
      const auto diffusion_gpu_support = evaluate_gpu_pipeline_support(
          impulse, diffusion_state, nullptr, {}, true);
      const auto gpu_rejection_is_explicit =
          diffusion_gpu_support.fallback_reason.contains(QStringLiteral("photo_layers")) ||
          diffusion_gpu_support.fallback_reason == QStringLiteral("metal_not_compiled");
      if (diffusion_gpu_support.supported ||
          !gpu_rejection_is_explicit) {
        std::cerr << "F1 must report photo-layer GPU support honestly until that path is accelerated\n";
        return 1;
      }
      std::cout << "f1_backend_route model=" << model.toStdString()
                << " auto=" << diffusion_auto_telemetry.backend.toStdString()
                << " auto_fallback=" << diffusion_auto_telemetry.fallback_reason.toStdString()
                << " assumed_gpu_support=" << diffusion_gpu_support.supported
                << " assumed_gpu_reason=" << diffusion_gpu_support.fallback_reason.toStdString()
                << " auto_cpu_pixel_delta=" << image_delta(diffusion_cpu, diffusion_auto)
                << " reopened_pixel_delta=" << image_delta(diffusion_cpu, diffusion_reopened)
                << '\n';

      auto unsupported = effects;
      auto box_effects = effects;
      box_effects[0].type_id = "box_blur";
      box_effects[0].box.radius = 2;
      const auto box_payload = encode_photo_layer_effects(box_effects);
      if (!box_payload) return 1;
      auto box_layer = layer;
      box_layer.insert("effectStack", *box_payload);
      state.layers = {box_layer};
      state = parse_edit_recipe_json(serialize_edit_recipe_json(state));
      const auto box_output = render_output_image_with_backend(impulse, state,
          QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      if (box_output.isNull() || box_output.pixelColor(15, 16).red() <= plain.pixelColor(15, 16).red() ||
          image_delta(box_output, plain) < 10) {
        std::cerr << "Persisted box blur did not spread the impulse\n";
        return 1;
      }
      auto invert_effects = effects;
      invert_effects[0].type_id = "invert_linear_rgb";
      const auto invert_payload = encode_photo_layer_effects(invert_effects);
      if (!invert_payload) return 1;
      auto invert_layer = layer;
      invert_layer.insert("effectStack", *invert_payload);
      state.layers = {invert_layer};
      state = parse_edit_recipe_json(serialize_edit_recipe_json(state));
      const auto inverted = render_output_image_with_backend(impulse, state,
          QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      if (inverted.isNull() || inverted.pixelColor(0, 0).red() <= inverted.pixelColor(16, 16).red() ||
          image_delta(inverted, plain) < 100) {
        std::cerr << "Persisted invert did not reverse the impulse contrast\n";
        return 1;
      }
      QImage gradient(32, 32, QImage::Format_ARGB32);
      gradient.setColorSpace(QColorSpace(QColorSpace::SRgb));
      for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
          gradient.setPixelColor(x, y, QColor(x * 255 / 31, y * 255 / 31, 100));
      auto posterize_effects = effects;
      posterize_effects[0].type_id = "posterize_linear_rgb";
      posterize_effects[0].posterize_levels = 4;
      auto posterize_layer = layer;
      const auto posterize_payload = encode_photo_layer_effects(posterize_effects);
      if (!posterize_payload) return 1;
      posterize_layer.insert("effectStack", *posterize_payload);
      state.layers.clear();
      const auto gradient_plain = render_output_image_with_backend(gradient, state,
          QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      state.layers = {posterize_layer};
      const auto posterized = render_output_image_with_backend(gradient, state,
          QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      const auto saved_posterize = parse_edit_recipe_json(serialize_edit_recipe_json(state));
      const auto reopened_posterized = render_output_image_with_backend(gradient, saved_posterize,
          QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      if (posterized.isNull() || gradient_plain.isNull() || image_delta(posterized, gradient_plain) < 10 ||
          image_delta(posterized, reopened_posterized) != 0) {
        std::cerr << "Posterize must affect gradient output and survive recipe reopening exactly\n";
        return 1;
      }
      posterize_effects[0].enabled = false;
      const auto posterize_bypass_payload = encode_photo_layer_effects(posterize_effects);
      if (!posterize_bypass_payload) return 1;
      posterize_layer.insert("effectStack", *posterize_bypass_payload);
      state.layers = {posterize_layer};
      const auto posterize_bypassed = render_output_image_with_backend(gradient, state,
          QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      if (image_delta(posterize_bypassed, gradient_plain) != 0) return 1;
      unsupported[0].type_id = "future-output-filter";
      unsupported[0].opaque_parameters = "future settings";
      const auto unsupported_payload = encode_photo_layer_effects(unsupported);
      if (!unsupported_payload) return 1;
      layer.insert("effectStack", *unsupported_payload);
      state.layers = {layer};
      const auto rejected = render_output_image_with_backend(impulse, state,
          QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      if (!rejected.isNull()) {
        std::cerr << "Desktop output silently omitted an unavailable active effect\n";
        return 1;
      }
      unsupported[0].enabled = false;
      const auto bypass_payload = encode_photo_layer_effects(unsupported);
      if (!bypass_payload) return 1;
      layer.insert("effectStack", *bypass_payload);
      state.layers = {layer};
      const auto bypassed = render_output_image_with_backend(impulse, state,
          QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      if (bypassed.isNull() || image_delta(bypassed, plain) != 0) {
        std::cerr << "Disabled unavailable effect must preserve normal output\n";
        return 1;
      }
    }
  }
  for (const auto& profile : profiles) {
    ColorPipelineConfig config;
    config.working_profile = working_color_profile_from_name(profile.toStdString());
    const auto expected = working_image_from_qimage(source, profile);
    for (const auto& kind : {QString("fill"), QString("pixel"), QString("source")}) {
      QVariantMap layer{{"id", "color-proof"}, {"kind", kind}, {"fillColor", "#b06030"},
          {"pixelWidth", 4}, {"pixelHeight", 2}, {"sourcePath", root.filePath("placed.png")}};
      if (kind == "pixel") layer.insert("pixelData", png);
      const auto compiled = compile_pipeline_photo_layers({layer}, {},
          LocalMaskRasterPolicy::UseStoredRaster, source.size(), {}, config, DesktopRenderPurpose::Output);
      if (compiled.size() != 1 || (kind != "fill" && compiled[0].pixels.empty())) return 1;
      const auto actual = kind == "fill" ? compiled[0].fill_color : compiled[0].pixels[0];
      const auto reference = expected.pixels[0];
      const auto delta = std::max({std::abs(actual.red - reference.red),
          std::abs(actual.green - reference.green), std::abs(actual.blue - reference.blue)});
      std::cout << profile.toStdString() << ' ' << kind.toStdString() << " max RGB delta=" << delta << '\n';
      ok = ok && delta < 0.0006F;
      for (const auto& model : {QString("legacy-oklch-v1"), QString("aces2-ocio-v1")}) {
        EditRecipeState state;
        state.working_color_profile = profile;
        state.photographic_rendering_model = model;
        const auto reference_image = render_output_image_with_backend(source, state,
            QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
        state.layers = {layer};
        // Exercise durable recipe encoding, not just transient QVariant maps.
        state = parse_edit_recipe_json(serialize_edit_recipe_json(state));
        const auto admission = RenderPlanStore::shared().admit("layer-proof-base", root.filePath("placed.png"),
            state, source.size(), RenderPurpose::SettledPreview, DisplayRenderingTarget::ActiveDisplay);
        if (model == "aces2-ocio-v1" && !admission.admitted) {
          std::cerr << "Canonical layer admission failed: " << admission.error.toStdString() << '\n';
          ok = false;
        }
        QImage base(source.size(), source.format()); base.fill(Qt::black); base.setColorSpace(source.colorSpace());
        const auto preview = render_output_image_with_backend(base, state,
            QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
        const auto output = render_output_image_with_backend(base, state,
            QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64, RenderBackendPreference::CpuOnly);
        const auto placed_delta = image_delta(preview, reference_image);
        const auto parity_delta = image_delta(preview, output);
        if (placed_delta > 2 || parity_delta > 2) {
          std::cerr << profile.toStdString() << '/' << kind.toStdString() << '/' << model.toStdString()
                    << " placement=" << placed_delta << " preview/export=" << parity_delta << '\n';
          ok = false;
        }
      }
    }
  }
  {
    const QVariantMap layer{{"id", "geometry"}, {"kind", "source"}, {"sourcePath", root.filePath("placed.png")},
        {"canvasWidth", 8}, {"canvasHeight", 8}, {"pixelWidth", 4}, {"pixelHeight", 2},
        {"x", 2}, {"y", 4}, {"scale", 1.0}};
    ColorPipelineConfig config;
    const auto full = compile_pipeline_photo_layers({layer}, {}, LocalMaskRasterPolicy::UseStoredRaster,
        QSize(8, 8), {}, config, DesktopRenderPurpose::Output);
    const auto preview = compile_pipeline_photo_layers({layer}, {}, LocalMaskRasterPolicy::UseStoredRaster,
        QSize(4, 4), {}, config, DesktopRenderPurpose::Preview);
    if (full[0].x != 2 || full[0].y != 4 || full[0].transform_scale != 1.0 ||
        preview[0].x != 1 || preview[0].y != 2 || preview[0].transform_scale != 1.0 ||
        preview[0].pixel_width != 2 || preview[0].pixel_height != 1) return 1;
  }
  // A placed tagged wide-gamut JPEG must keep its own color interpretation.
  source.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  if (!source.save(root.filePath("placed.jpg"))) return 1;
  const auto jpeg = decode_photo_layer_source_image(root.filePath("placed.jpg"));
  for (const auto& profile : profiles) {
    ColorPipelineConfig config; config.working_profile = working_color_profile_from_name(profile.toStdString());
    const QVariantMap layer{{"id", "jpeg"}, {"kind", "source"}, {"sourcePath", root.filePath("placed.jpg")}};
    const auto compiled = compile_pipeline_photo_layers({layer}, {}, LocalMaskRasterPolicy::UseStoredRaster,
        jpeg.size(), {}, config, DesktopRenderPurpose::Output);
    const auto expected = working_image_from_qimage(jpeg, profile);
    if (expected.isNull() || compiled[0].pixels.size() != expected.pixels.size()) return 1;
    const auto a = compiled[0].pixels[0], b = expected.pixels[0];
    if (std::max({std::abs(a.red-b.red), std::abs(a.green-b.green), std::abs(a.blue-b.blue)}) > 0.0006F) return 1;
  }
  if (argc > 1 && !gui) {
    const auto path = QString::fromLocal8Bit(argv[1]);
    EditRecipeState state;
    state.photographic_rendering_model = "legacy-oklch-v1";
    QImage raw; DecodedImageMetadata metadata;
    if (!decode_source_image(path, &raw, 0, state, &metadata) ||
        !decoded_image_requires_engine_input_transform(metadata)) return 1;
    const auto reference = render_output_image_with_backend(raw, state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_ARGB32, RenderBackendPreference::CpuOnly, &metadata);
    QImage base(raw.size(), QImage::Format_ARGB32); base.fill(Qt::black); base.setColorSpace(QColorSpace(QColorSpace::SRgb));
    state.layers = {QVariantMap{{"id", "raw"}, {"kind", "source"}, {"sourcePath", path}}};
    state = parse_edit_recipe_json(serialize_edit_recipe_json(state));
    const auto placed = render_output_image_with_backend(base, state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    const auto delta = image_delta(reference, placed);
    std::cout << "Real RAW " << raw.width() << 'x' << raw.height() << " ordinary-vs-placed max RGB8 delta=" << delta << '\n';
    ok = ok && delta <= 2;
  }
  if (ok) std::cout << "Layer color, persisted recipe, placed/base equivalence and preview/export gates passed\n";
  return ok ? 0 : 1;
}
