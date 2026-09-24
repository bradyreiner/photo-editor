#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <utility>

#include <QVariantList>
#include <QVariantMap>

#include "agent_edit_recipe_patch.h"
#include "agent_edit_style.h"
#include "photo_layer_effect_codec.h"
#include "photo_text_style.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool almost_equal(double left, double right) {
  return std::abs(left - right) <= 1e-6;
}

QVariantMap point(double x, double y) {
  return QVariantMap{
      {QStringLiteral("x"), x},
      {QStringLiteral("y"), y},
  };
}

QVariantList curve_points(std::initializer_list<std::pair<double, double>> points) {
  QVariantList result;
  for (const auto& [x, y] : points) {
    result.append(point(x, y));
  }
  return result;
}

double curve_point_coordinate(const QVariantList& points, int index, const QString& key) {
  return points.at(index).toMap().value(key).toDouble();
}

QVariantMap lut_row(double red, double green, double blue, double alpha = 1.0) {
  return QVariantMap{
      {QStringLiteral("red"), red},
      {QStringLiteral("green"), green},
      {QStringLiteral("blue"), blue},
      {QStringLiteral("alpha"), alpha},
  };
}

QVariantMap first_built_in_style() {
  const auto styles = photo_editor::desktop::built_in_agent_edit_styles();
  return styles.first().toMap();
}

}  // namespace

int main() {
  auto style = first_built_in_style();
  const QVariantMap patch{
      {QStringLiteral("globalTone"),
       QVariantMap{
           {QStringLiteral("exposure"), 0.65},
           {QStringLiteral("contrast"), 14.0},
       }},
      {QStringLiteral("color"),
       QVariantMap{
           {QStringLiteral("temperature"), 9.0},
           {QStringLiteral("vibrance"), 12.0},
       }},
      {QStringLiteral("curves"),
       QVariantMap{
           {QStringLiteral("curveHighlights"), -8.0},
       }},
      {QStringLiteral("detail"),
       QVariantMap{
           {QStringLiteral("clarity"), 6.0},
           {QStringLiteral("sharpening"), 24.0},
       }},
  };

  const auto validation = photo_editor::desktop::validate_agent_recipe_patch(patch, style);
  if (!validation.valid) {
    std::cerr << validation.errors.join(QStringLiteral("; ")).toStdString() << '\n';
    return fail("valid recipe patch should pass");
  }

  photo_editor::desktop::EditRecipeState state;
  state.exposure = -0.25;
  state.contrast = 2.0;
  const auto before_state = state;
  photo_editor::desktop::apply_agent_recipe_patch(&state, patch);
  if (!almost_equal(state.exposure, 0.65) ||
      !almost_equal(state.contrast, 14.0) ||
      !almost_equal(state.temperature, 9.0) ||
      !almost_equal(state.vibrance, 12.0) ||
      !almost_equal(state.curve_highlights, -8.0) ||
      !almost_equal(state.clarity, 6.0) ||
      !almost_equal(state.sharpening, 24.0)) {
    return fail("valid recipe patch should update the expected edit fields");
  }
  const auto diff = photo_editor::desktop::agent_recipe_patch_diff(before_state, state, patch);
  const auto tone_diff = diff.value(QStringLiteral("globalTone")).toMap();
  const auto exposure_diff = tone_diff.value(QStringLiteral("exposure")).toMap();
  if (!almost_equal(exposure_diff.value(QStringLiteral("before")).toDouble(), -0.25) ||
      !almost_equal(exposure_diff.value(QStringLiteral("after")).toDouble(), 0.65) ||
      !almost_equal(exposure_diff.value(QStringLiteral("delta")).toDouble(), 0.9)) {
    return fail("recipe patch diff should include before, after, and delta values");
  }
  const auto detail_diff = diff.value(QStringLiteral("detail")).toMap();
  if (detail_diff.value(QStringLiteral("clarity")).toMap().isEmpty()) {
    return fail("recipe patch diff should preserve grouped edit fields");
  }

  const QVariantMap out_of_range_patch{
      {QStringLiteral("globalTone"),
       QVariantMap{
           {QStringLiteral("exposure"), 12.0},
       }},
  };
  const auto out_of_range =
      photo_editor::desktop::validate_agent_recipe_patch(out_of_range_patch, style);
  if (out_of_range.valid ||
      !out_of_range.errors.join(QStringLiteral("; ")).contains(QStringLiteral("between"))) {
    return fail("recipe patch validation should reject out-of-range values");
  }

  const QVariantMap unsupported_field_patch{
      {QStringLiteral("globalTone"),
       QVariantMap{
           {QStringLiteral("notAControl"), 1.0},
       }},
  };
  const auto unsupported_field =
      photo_editor::desktop::validate_agent_recipe_patch(unsupported_field_patch, style);
  if (unsupported_field.valid ||
      !unsupported_field.errors.join(QStringLiteral("; ")).contains(QStringLiteral("Unsupported"))) {
    return fail("recipe patch validation should reject unsupported fields");
  }

  const QVariantMap direct_recipe_json_patch{
      {QStringLiteral("recipeJson"), QStringLiteral("{\"exposure\": 1.0}")},
  };
  const auto direct_recipe_json =
      photo_editor::desktop::validate_agent_recipe_patch(direct_recipe_json_patch, style);
  if (direct_recipe_json.valid ||
      !direct_recipe_json.errors.join(QStringLiteral("; ")).contains(QStringLiteral("direct recipe JSON"))) {
    return fail("recipe patch validation should explicitly reject direct recipe JSON payloads");
  }

  const QVariantMap nested_recipe_json_patch{
      {QStringLiteral("globalTone"),
       QVariantMap{
           {QStringLiteral("exposure"), 0.3},
           {QStringLiteral("fullRecipeJson"), QStringLiteral("{\"temperature\": 18.0}")},
       }},
  };
  const auto nested_recipe_json =
      photo_editor::desktop::validate_agent_recipe_patch(nested_recipe_json_patch, style);
  if (nested_recipe_json.valid ||
      !nested_recipe_json.errors.join(QStringLiteral("; ")).contains(QStringLiteral("patch.globalTone.fullRecipeJson"))) {
    return fail("recipe patch validation should reject nested full recipe JSON payloads before apply");
  }

  auto no_curves_style = style;
  auto allowed_tools = no_curves_style.value(QStringLiteral("allowedTools")).toMap();
  allowed_tools.insert(QStringLiteral("curves"), false);
  no_curves_style.insert(QStringLiteral("allowedTools"), allowed_tools);
  const QVariantMap curve_patch{
      {QStringLiteral("curves"),
       QVariantMap{
           {QStringLiteral("curveShadows"), 8.0},
       }},
  };
  const auto disallowed =
      photo_editor::desktop::validate_agent_recipe_patch(curve_patch, no_curves_style);
  if (disallowed.valid ||
      !disallowed.errors.join(QStringLiteral("; ")).contains(QStringLiteral("does not allow"))) {
    return fail("recipe patch validation should enforce style tool permissions");
  }

  const QVariantMap malformed_curve_type_patch{
      {QStringLiteral("curves"),
       QVariantMap{
           {QStringLiteral("masterCurvePoints"), 1.0},
       }},
  };
  const auto malformed_curve_type =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_curve_type_patch, style);
  if (malformed_curve_type.valid ||
      !malformed_curve_type.errors.join(QStringLiteral("; ")).contains(QStringLiteral("array of curve points"))) {
    return fail("recipe patch validation should enforce point curve array fields");
  }

  const QVariantMap malformed_curve_endpoint_patch{
      {QStringLiteral("curves"),
       QVariantMap{
           {QStringLiteral("masterCurvePoints"), curve_points({{0.10, 0.05}, {1.0, 1.0}})},
       }},
  };
  const auto malformed_curve_endpoint =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_curve_endpoint_patch, style);
  if (malformed_curve_endpoint.valid ||
      !malformed_curve_endpoint.errors.join(QStringLiteral("; ")).contains(QStringLiteral("endpoints"))) {
    return fail("recipe patch validation should require point curve endpoints");
  }

  const QVariantMap malformed_curve_order_patch{
      {QStringLiteral("curves"),
       QVariantMap{
           {QStringLiteral("redCurvePoints"), curve_points({{0.0, 0.0}, {0.60, 0.58}, {0.40, 0.42}, {1.0, 1.0}})},
       }},
  };
  const auto malformed_curve_order =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_curve_order_patch, style);
  if (malformed_curve_order.valid ||
      !malformed_curve_order.errors.join(QStringLiteral("; ")).contains(QStringLiteral("must increase"))) {
    return fail("recipe patch validation should require ordered point curve x values");
  }

  const QVariantMap malformed_curve_range_patch{
      {QStringLiteral("curves"),
       QVariantMap{
           {QStringLiteral("blueCurvePoints"), curve_points({{0.0, 0.0}, {0.50, 1.4}, {1.0, 1.0}})},
       }},
  };
  const auto malformed_curve_range =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_curve_range_patch, style);
  if (malformed_curve_range.valid ||
      !malformed_curve_range.errors.join(QStringLiteral("; ")).contains(QStringLiteral("between 0 and 1"))) {
    return fail("recipe patch validation should enforce point curve coordinate ranges");
  }

  const QVariantMap malformed_raw_bool_patch{
      {QStringLiteral("raw"),
       QVariantMap{
           {QStringLiteral("rawHighlightRecovery"), 1.0},
       }},
  };
  const auto malformed_raw_bool =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_raw_bool_patch, style);
  if (malformed_raw_bool.valid ||
      !malformed_raw_bool.errors.join(QStringLiteral("; ")).contains(QStringLiteral("must be boolean"))) {
    return fail("recipe patch validation should enforce boolean RAW fields");
  }

  const QVariantMap malformed_raw_enum_patch{
      {QStringLiteral("raw"),
       QVariantMap{
           {QStringLiteral("rawDemosaicQuality"), QStringLiteral("Ultra")},
       }},
  };
  const auto malformed_raw_enum =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_raw_enum_patch, style);
  if (malformed_raw_enum.valid ||
      !malformed_raw_enum.errors.join(QStringLiteral("; ")).contains(QStringLiteral("must be one of"))) {
    return fail("recipe patch validation should enforce RAW enum fields");
  }

  const QVariantMap malformed_raw_range_patch{
      {QStringLiteral("raw"),
       QVariantMap{
           {QStringLiteral("rawBlackLevel"), 1.2},
       }},
  };
  const auto malformed_raw_range =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_raw_range_patch, style);
  if (malformed_raw_range.valid ||
      !malformed_raw_range.errors.join(QStringLiteral("; ")).contains(QStringLiteral("between"))) {
    return fail("recipe patch validation should enforce RAW numeric ranges");
  }

  const QVariantMap malformed_crop_preset_patch{
      {QStringLiteral("crop"),
       QVariantMap{
           {QStringLiteral("cropPreset"), 4.0},
       }},
  };
  const auto malformed_crop_preset =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_crop_preset_patch, style);
  if (malformed_crop_preset.valid ||
      !malformed_crop_preset.errors.join(QStringLiteral("; ")).contains(QStringLiteral("must be a string"))) {
    return fail("recipe patch validation should enforce string crop presets");
  }

  const QVariantMap malformed_upright_mode_patch{
      {QStringLiteral("crop"),
       QVariantMap{
           {QStringLiteral("uprightMode"), QStringLiteral("Auto")},
       }},
  };
  const auto malformed_upright_mode =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_upright_mode_patch, style);
  if (malformed_upright_mode.valid ||
      !malformed_upright_mode.errors.join(QStringLiteral("; ")).contains(QStringLiteral("must be one of"))) {
    return fail("recipe patch validation should enforce upright mode values");
  }

  const QVariantMap malformed_crop_rect_partial_patch{
      {QStringLiteral("crop"),
       QVariantMap{
           {QStringLiteral("cropX"), 0.1},
       }},
  };
  const auto malformed_crop_rect_partial =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_crop_rect_partial_patch, style);
  if (malformed_crop_rect_partial.valid ||
      !malformed_crop_rect_partial.errors.join(QStringLiteral("; ")).contains(QStringLiteral("must include"))) {
    return fail("recipe patch validation should require complete crop rectangles");
  }

  const QVariantMap malformed_crop_rect_small_patch{
      {QStringLiteral("crop"),
       QVariantMap{
           {QStringLiteral("cropX"), 0.0},
           {QStringLiteral("cropY"), 0.0},
           {QStringLiteral("cropWidth"), 0.04},
           {QStringLiteral("cropHeight"), 0.4},
       }},
  };
  const auto malformed_crop_rect_small =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_crop_rect_small_patch, style);
  if (malformed_crop_rect_small.valid ||
      !malformed_crop_rect_small.errors.join(QStringLiteral("; ")).contains(QStringLiteral("0.05"))) {
    return fail("recipe patch validation should enforce minimum crop rectangle size");
  }

  const QVariantMap malformed_crop_rect_bounds_patch{
      {QStringLiteral("crop"),
       QVariantMap{
           {QStringLiteral("cropX"), 0.7},
           {QStringLiteral("cropY"), 0.2},
           {QStringLiteral("cropWidth"), 0.4},
           {QStringLiteral("cropHeight"), 0.4},
       }},
  };
  const auto malformed_crop_rect_bounds =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_crop_rect_bounds_patch, style);
  if (malformed_crop_rect_bounds.valid ||
      !malformed_crop_rect_bounds.errors.join(QStringLiteral("; ")).contains(QStringLiteral("within"))) {
    return fail("recipe patch validation should enforce crop rectangle bounds");
  }

  const QVariantMap malformed_output_profile_patch{
      {QStringLiteral("output"),
       QVariantMap{
           {QStringLiteral("outputColorSpace"), QStringLiteral("Mystery RGB")},
       }},
  };
  const auto malformed_output_profile =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_output_profile_patch, style);
  if (malformed_output_profile.valid ||
      !malformed_output_profile.errors.join(QStringLiteral("; ")).contains(QStringLiteral("must be one of"))) {
    return fail("recipe patch validation should enforce output profile values");
  }

  const QVariantMap malformed_soft_proof_bool_patch{
      {QStringLiteral("output"),
       QVariantMap{
           {QStringLiteral("softProofEnabled"), 1.0},
       }},
  };
  const auto malformed_soft_proof_bool =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_soft_proof_bool_patch, style);
  if (malformed_soft_proof_bool.valid ||
      !malformed_soft_proof_bool.errors.join(QStringLiteral("; ")).contains(QStringLiteral("must be boolean"))) {
    return fail("recipe patch validation should enforce boolean soft proof fields");
  }

  auto layers_style = style;
  auto layers_tools = layers_style.value(QStringLiteral("allowedTools")).toMap();
  layers_tools.insert(QStringLiteral("layers_luts"), true);
  layers_tools.insert(QStringLiteral("graphics"), false);
  layers_style.insert(QStringLiteral("allowedTools"), layers_tools);
  const QVariantList valid_lut_stack{
      QVariantMap{
          {QStringLiteral("enabled"), true},
          {QStringLiteral("strength"), 72.0},
          {QStringLiteral("name"), QStringLiteral("Agent Warm LUT")},
          {QStringLiteral("size"), 2.0},
          {QStringLiteral("table"), QVariantList{lut_row(0.0, 0.0, 0.0), lut_row(1.0, 0.92, 0.82)}},
          {QStringLiteral("domainMin"), QVariantList{0.0, 0.0, 0.0}},
          {QStringLiteral("domainMax"), QVariantList{1.0, 1.0, 1.0}},
          {QStringLiteral("inputColorSpace"), QStringLiteral("sRGB")},
          {QStringLiteral("outputColorSpace"), QStringLiteral("Display P3")},
      },
  };
  const QVariantMap layer_stack_patch{
      {QStringLiteral("layers"),
       QVariantMap{
           {QStringLiteral("photoLayers"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-polish-layer")},
                    {QStringLiteral("kind"), QStringLiteral("adjustment")},
                    {QStringLiteral("name"), QStringLiteral("Agent Polish")},
                    {QStringLiteral("enabled"), true},
                    {QStringLiteral("opacity"), 0.68},
                    {QStringLiteral("blendMode"), QStringLiteral("soft light")},
                    {QStringLiteral("selectionScoped"), true},
                    {QStringLiteral("selectionMaskSetId"), QStringLiteral("subject-mask")},
                    {QStringLiteral("adjustments"),
                     QVariantMap{
                         {QStringLiteral("lutStack"), valid_lut_stack},
                     }},
                },
            }},
       }},
  };
  const auto layer_stack_validation =
      photo_editor::desktop::validate_agent_recipe_patch(layer_stack_patch, layers_style);
  if (!layer_stack_validation.valid) {
    std::cerr << layer_stack_validation.errors.join(QStringLiteral("; ")).toStdString() << '\n';
    return fail("recipe patch validation should accept safe adjustment layer LUT stacks");
  }
  if (!layer_stack_validation.warnings.join(QStringLiteral("; ")).contains(QStringLiteral("Layer and LUT edits"))) {
    return fail("recipe patch validation should warn for layer and LUT tool usage");
  }
  const QVariantMap empty_layer_stack_patch{
      {QStringLiteral("layers"),
       QVariantMap{
           {QStringLiteral("photoLayers"), QVariantList{}},
       }},
  };
  const auto empty_layer_stack_validation =
      photo_editor::desktop::validate_agent_recipe_patch(empty_layer_stack_patch, layers_style);
  if (empty_layer_stack_validation.valid ||
      !empty_layer_stack_validation.errors.join(QStringLiteral("; ")).contains(QStringLiteral("delete existing user layers"))) {
    return fail("recipe patch validation should reject empty layer stacks that would delete user layers");
  }
  photo_editor::desktop::EditRecipeState layer_state;
  const auto layer_before = layer_state;
  photo_editor::desktop::apply_agent_recipe_patch(&layer_state, layer_stack_patch);
  const auto patched_layer = layer_state.layers.constFirst().toMap();
  const auto patched_lut_stack =
      patched_layer.value(QStringLiteral("adjustments")).toMap().value(QStringLiteral("lutStack")).toList();
  if (layer_state.layers.size() != 1 ||
      patched_layer.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment") ||
      !almost_equal(patched_layer.value(QStringLiteral("opacity")).toDouble(), 0.68) ||
      patched_layer.value(QStringLiteral("blendMode")).toString() != QStringLiteral("soft light") ||
      patched_lut_stack.size() != 1 ||
      !almost_equal(patched_lut_stack.constFirst().toMap().value(QStringLiteral("strength")).toDouble(), 72.0)) {
    return fail("recipe patch should apply safe adjustment layer LUT stacks");
  }
  const auto layer_diff =
      photo_editor::desktop::agent_recipe_patch_diff(layer_before, layer_state, layer_stack_patch);
  if (layer_diff.value(QStringLiteral("layers"))
          .toMap()
          .value(QStringLiteral("photoLayers"))
          .toMap()
          .isEmpty()) {
    return fail("recipe patch diff should include layer stack changes");
  }

  auto graphics_style = layers_style;
  auto graphics_allowed_tools = graphics_style.value(QStringLiteral("allowedTools")).toMap();
  graphics_allowed_tools.insert(QStringLiteral("graphics"), true);
  graphics_style.insert(QStringLiteral("allowedTools"), graphics_allowed_tools);
  const QVariantMap graphic_text_layer{
      {QStringLiteral("id"), QStringLiteral("agent-graphic-headline")},
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("name"), QStringLiteral("Poster headline")},
      {QStringLiteral("text"), QStringLiteral("NORTH BY NATURE")},
      {QStringLiteral("enabled"), true},
      {QStringLiteral("opacity"), 1.0},
      {QStringLiteral("blendMode"), QStringLiteral("normal")},
      {QStringLiteral("x"), 96},
      {QStringLiteral("y"), 88},
      {QStringLiteral("pixelWidth"), 900},
      {QStringLiteral("pixelHeight"), 180},
      {QStringLiteral("canvasWidth"), 1200},
      {QStringLiteral("canvasHeight"), 1200},
      {QStringLiteral("textTypography"),
       photo_editor::desktop::encode_photo_text_style(
           photo_editor::desktop::PhotoTextStyle{}).toVariantMap()},
  };
  const QVariantMap graphic_patch{
      {QStringLiteral("graphics"),
       QVariantMap{{QStringLiteral("textLayers"), QVariantList{graphic_text_layer}}}},
  };
  const auto graphic_validation =
      photo_editor::desktop::validate_agent_recipe_patch(graphic_patch, graphics_style);
  if (!graphic_validation.valid ||
      !graphic_validation.warnings.join(QStringLiteral("; ")).contains(QStringLiteral("legibility"))) {
    std::cerr << graphic_validation.errors.join(QStringLiteral("; ")).toStdString() << '\n';
    return fail("agent graphic text layers should validate with layout review guidance");
  }
  const auto graphic_permission_validation =
      photo_editor::desktop::validate_agent_recipe_patch(graphic_patch, layers_style);
  if (graphic_permission_validation.valid ||
      !graphic_permission_validation.errors.join(QStringLiteral("; ")).contains(QStringLiteral("does not allow"))) {
    return fail("agent graphics should respect the explicit style tool permission");
  }
  const auto graphic_schema = photo_editor::desktop::agent_recipe_patch_tool_schema()
                                  .value(QStringLiteral("groups")).toMap()
                                  .value(QStringLiteral("graphics")).toMap()
                                  .value(QStringLiteral("fields")).toMap()
                                  .value(QStringLiteral("textLayers")).toMap();
  if (graphic_schema.value(QStringLiteral("type")).toString() != QStringLiteral("graphicTextStack") ||
      !graphic_schema.value(QStringLiteral("appendOnly")).toBool()) {
    return fail("agent graphics schema should advertise append-only editable text layers");
  }
  const auto effect_change_schema = photo_editor::desktop::agent_recipe_patch_tool_schema()
                                       .value(QStringLiteral("groups")).toMap()
                                       .value(QStringLiteral("layers")).toMap()
                                       .value(QStringLiteral("fields")).toMap()
                                       .value(QStringLiteral("effectChanges")).toMap();
  if (effect_change_schema.value(QStringLiteral("type")).toString() !=
          QStringLiteral("layerEffectChanges") ||
      effect_change_schema.value(QStringLiteral("allowedTypes")).toStringList().size() != 6) {
    return fail("agent layer-effect schema should expose every currently supported non-destructive effect");
  }
  photo_editor::desktop::EditRecipeState graphic_state;
  graphic_state.layers = QVariantList{QVariantMap{
      {QStringLiteral("id"), QStringLiteral("user-existing-text")},
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("name"), QStringLiteral("User caption")},
      {QStringLiteral("text"), QStringLiteral("Keep me")},
  }};
  const auto graphic_before = graphic_state;
  photo_editor::desktop::apply_agent_recipe_patch(&graphic_state, graphic_patch);
  if (graphic_state.layers.size() != 2 ||
      graphic_state.layers.at(0).toMap().value(QStringLiteral("agentGraphic")).toBool() != true ||
      graphic_state.layers.at(1).toMap().value(QStringLiteral("id")).toString() !=
          QStringLiteral("user-existing-text")) {
    return fail("agent graphic text should be editable and preserve existing user layers");
  }
  const auto graphic_diff = photo_editor::desktop::agent_recipe_patch_diff(
      graphic_before, graphic_state, graphic_patch);
  if (graphic_diff.value(QStringLiteral("graphics")).toMap()
          .value(QStringLiteral("textLayers")).toMap()
          .value(QStringLiteral("after")).toList().size() != 1) {
    return fail("agent graphic diff should expose the generated editable text layer");
  }
  auto revised_graphic_text_layer = graphic_text_layer;
  revised_graphic_text_layer.insert(QStringLiteral("text"), QStringLiteral("WILD LIGHT"));
  photo_editor::desktop::apply_agent_recipe_patch(
      &graphic_state,
      QVariantMap{{QStringLiteral("graphics"), QVariantMap{{QStringLiteral("textLayers"),
                                                            QVariantList{revised_graphic_text_layer}}}}});
  if (graphic_state.layers.size() != 2 ||
      graphic_state.layers.at(0).toMap().value(QStringLiteral("text")).toString() !=
          QStringLiteral("WILD LIGHT") ||
      graphic_state.layers.at(1).toMap().value(QStringLiteral("id")).toString() !=
          QStringLiteral("user-existing-text")) {
    return fail("graphic revisions should replace only prior agent-owned layers");
  }
  auto malformed_graphic_layer = graphic_text_layer;
  malformed_graphic_layer.insert(QStringLiteral("id"), QStringLiteral("user-text"));
  const QVariantMap malformed_graphic_patch{
      {QStringLiteral("graphics"), QVariantMap{{QStringLiteral("textLayers"),
                                                QVariantList{malformed_graphic_layer}}}},
  };
  if (photo_editor::desktop::validate_agent_recipe_patch(malformed_graphic_patch, graphics_style).valid) {
    return fail("agent graphics should reject layer ids outside the agent-owned namespace");
  }
  const QVariantMap effect_change{
      {QStringLiteral("layerId"), QStringLiteral("user-existing-text")},
      {QStringLiteral("effectId"), QStringLiteral("agent-effect-soft-focus")},
      {QStringLiteral("type"), QStringLiteral("gaussian_blur")},
      {QStringLiteral("enabled"), true},
      {QStringLiteral("amount"), 0.35},
      {QStringLiteral("sigma"), 6.0},
      {QStringLiteral("edgeMode"), QStringLiteral("reflect")},
  };
  const QVariantMap effect_patch{
      {QStringLiteral("layers"), QVariantMap{{QStringLiteral("effectChanges"), QVariantList{effect_change}}}},
  };
  const auto effect_validation =
      photo_editor::desktop::validate_agent_recipe_patch(effect_patch, layers_style);
  if (!effect_validation.valid) {
    std::cerr << effect_validation.errors.join(QStringLiteral("; ")).toStdString() << '\n';
    return fail("bounded agent layer effects should validate under layer permissions");
  }
  photo_editor::desktop::apply_agent_recipe_patch(&graphic_state, effect_patch);
  const auto graphic_effect_layer = graphic_state.layers.at(1).toMap();
  const auto generated_effects = photo_editor::desktop::decode_photo_layer_effects(graphic_effect_layer);
  if (!generated_effects || generated_effects->size() != 1 ||
      generated_effects->front().id != "agent-effect-soft-focus" ||
      generated_effects->front().type_id != "gaussian_blur" ||
      !almost_equal(generated_effects->front().amount, 0.35) ||
      !almost_equal(generated_effects->front().gaussian.sigma, 6.0) ||
      graphic_effect_layer.value(QStringLiteral("text")).toString() != QStringLiteral("Keep me")) {
    return fail("agent layer effects should update only the target layer while retaining its editable text");
  }
  const auto effect_diff = photo_editor::desktop::agent_recipe_patch_diff(
      graphic_before, graphic_state, effect_patch);
  if (effect_diff.value(QStringLiteral("layers")).toMap()
          .value(QStringLiteral("effectChanges")).toList().first().toMap()
          .value(QStringLiteral("after")).toMap().isEmpty()) {
    return fail("agent layer-effect diff should show the applied effect");
  }
  auto invalid_effect = effect_change;
  invalid_effect.insert(QStringLiteral("sigma"), 5000.0);
  const QVariantMap invalid_effect_patch{
      {QStringLiteral("layers"), QVariantMap{{QStringLiteral("effectChanges"), QVariantList{invalid_effect}}}},
  };
  if (photo_editor::desktop::validate_agent_recipe_patch(invalid_effect_patch, layers_style).valid) {
    return fail("agent layer effects should reject values outside renderer limits");
  }

  const auto layer_stack_disallowed =
      photo_editor::desktop::validate_agent_recipe_patch(layer_stack_patch, style);
  if (layer_stack_disallowed.valid ||
      !layer_stack_disallowed.errors.join(QStringLiteral("; ")).contains(QStringLiteral("does not allow"))) {
    return fail("recipe patch validation should enforce layer/LUT tool permissions");
  }

  const QVariantMap malformed_layer_kind_patch{
      {QStringLiteral("layers"),
       QVariantMap{
           {QStringLiteral("photoLayers"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-source-layer")},
                    {QStringLiteral("kind"), QStringLiteral("source")},
                    {QStringLiteral("name"), QStringLiteral("External Source")},
                },
            }},
       }},
  };
  const auto malformed_layer_kind =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_layer_kind_patch, layers_style);
  if (malformed_layer_kind.valid ||
      !malformed_layer_kind.errors.join(QStringLiteral("; ")).contains(QStringLiteral("kind must be adjustment"))) {
    return fail("recipe patch validation should reject unsafe layer kinds");
  }

  const QVariantMap malformed_layer_adjustment_patch{
      {QStringLiteral("layers"),
       QVariantMap{
           {QStringLiteral("photoLayers"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-layer")},
                    {QStringLiteral("kind"), QStringLiteral("adjustment")},
                    {QStringLiteral("name"), QStringLiteral("Agent Layer")},
                    {QStringLiteral("adjustments"),
                     QVariantMap{
                         {QStringLiteral("rawPayload"), QVariantMap{{QStringLiteral("x"), 1.0}}},
                     }},
                },
            }},
       }},
  };
  const auto malformed_layer_adjustment =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_layer_adjustment_patch, layers_style);
  if (malformed_layer_adjustment.valid ||
      !malformed_layer_adjustment.errors.join(QStringLiteral("; ")).contains(QStringLiteral("unsupported"))) {
    return fail("recipe patch validation should reject unsupported layer adjustment payloads");
  }

  const QVariantMap malformed_lut_channel_patch{
      {QStringLiteral("layers"),
       QVariantMap{
           {QStringLiteral("photoLayers"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-layer")},
                    {QStringLiteral("kind"), QStringLiteral("adjustment")},
                    {QStringLiteral("name"), QStringLiteral("Agent Layer")},
                    {QStringLiteral("adjustments"),
                     QVariantMap{
                         {QStringLiteral("lutStack"),
                          QVariantList{
                              QVariantMap{
                                  {QStringLiteral("name"), QStringLiteral("Bad LUT")},
                                  {QStringLiteral("table"),
                                   QVariantList{lut_row(0.0, 0.0, 0.0), lut_row(1.2, 0.5, 0.5)}},
                              },
                          }},
                     }},
                },
            }},
       }},
  };
  const auto malformed_lut_channel =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_lut_channel_patch, layers_style);
  if (malformed_lut_channel.valid ||
      !malformed_lut_channel.errors.join(QStringLiteral("; ")).contains(QStringLiteral("between 0 and 1"))) {
    return fail("recipe patch validation should enforce LUT table channel ranges");
  }

  const QVariantList valid_mask_stack{
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("agent-brush-mask")},
          {QStringLiteral("kind"), QStringLiteral("brush")},
          {QStringLiteral("name"), QStringLiteral("Agent Brush")},
          {QStringLiteral("maskSetId"), QStringLiteral("agent-selection")},
          {QStringLiteral("maskSetName"), QStringLiteral("Agent Selection")},
          {QStringLiteral("operation"), QStringLiteral("add")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("opacity"), 0.74},
          {QStringLiteral("density"), 0.66},
          {QStringLiteral("blendMode"), QStringLiteral("screen")},
          {QStringLiteral("exposure"), 0.42},
          {QStringLiteral("temperature"), -8.0},
          {QStringLiteral("texture"), 18.0},
          {QStringLiteral("clarity"), -12.0},
          {QStringLiteral("dehaze"), 7.0},
          {QStringLiteral("sharpening"), 36.0},
          {QStringLiteral("sharpenRadius"), 1.7},
          {QStringLiteral("sharpenDetail"), 42.0},
          {QStringLiteral("sharpenMasking"), 58.0},
          {QStringLiteral("luminanceNr"), 22.0},
          {QStringLiteral("colorNr"), 14.0},
          {QStringLiteral("selectionSmooth"), 32.0},
          {QStringLiteral("selectionFineEdge"), 44.0},
          {QStringLiteral("brushPreset"), QStringLiteral("Detail")},
          {QStringLiteral("brushSize"), 0.12},
          {QStringLiteral("brushFlow"), 0.82},
          {QStringLiteral("brushHardness"), 0.38},
          {QStringLiteral("brushAutoMask"), true},
          {QStringLiteral("brushProtectEdges"), true},
          {QStringLiteral("brushPoints"),
           QVariantList{
               QVariantMap{
                   {QStringLiteral("x"), 0.42},
                   {QStringLiteral("y"), 0.33},
                   {QStringLiteral("size"), 0.12},
                   {QStringLiteral("flow"), 0.82},
                   {QStringLiteral("opacity"), 0.58},
                   {QStringLiteral("pressure"), 0.54},
                   {QStringLiteral("tiltX"), 0.2},
                   {QStringLiteral("tiltY"), -0.1},
                   {QStringLiteral("erase"), false},
               },
           }},
      },
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("agent-lasso-mask")},
          {QStringLiteral("kind"), QStringLiteral("lasso")},
          {QStringLiteral("name"), QStringLiteral("Agent Lasso")},
          {QStringLiteral("maskSetId"), QStringLiteral("agent-selection")},
          {QStringLiteral("maskSetName"), QStringLiteral("Agent Selection")},
          {QStringLiteral("operation"), QStringLiteral("subtract")},
          {QStringLiteral("polygonPoints"),
           QVariantList{
               point(0.20, 0.20),
               point(0.70, 0.24),
               point(0.62, 0.72),
               point(0.24, 0.68),
           }},
      },
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("agent-subject-mask")},
          {QStringLiteral("kind"), QStringLiteral("subject")},
          {QStringLiteral("name"), QStringLiteral("Agent Subject")},
          {QStringLiteral("maskSetId"), QStringLiteral("agent-subject")},
          {QStringLiteral("maskSetName"), QStringLiteral("Agent Subject")},
          {QStringLiteral("selectionInvert"), false},
          {QStringLiteral("selectionExpand"), 12.0},
          {QStringLiteral("selectionFeather"), 18.0},
          {QStringLiteral("refineLuminanceMin"), 0.08},
          {QStringLiteral("refineLuminanceMax"), 0.96},
          {QStringLiteral("refineHueRange"), 0.22},
          {QStringLiteral("refineRangeFeather"), 0.12},
          {QStringLiteral("generatedMaskHoleFillThreshold"), 0.006},
          {QStringLiteral("generatedMaskIslandRemovalThreshold"), 0.002},
      },
  };
  const QVariantMap mask_stack_patch{
      {QStringLiteral("masks"),
       QVariantMap{
           {QStringLiteral("maskStack"), valid_mask_stack},
       }},
  };
  const auto mask_stack_validation =
      photo_editor::desktop::validate_agent_recipe_patch(mask_stack_patch, style);
  if (!mask_stack_validation.valid) {
    std::cerr << mask_stack_validation.errors.join(QStringLiteral("; ")).toStdString() << '\n';
    return fail("recipe patch validation should accept safe brush, lasso, and generated mask stacks");
  }
  if (!mask_stack_validation.warnings.join(QStringLiteral("; ")).contains(QStringLiteral("Mask edits"))) {
    return fail("recipe patch validation should warn for mask tool usage");
  }
  const QVariantMap empty_mask_stack_patch{
      {QStringLiteral("masks"),
       QVariantMap{
           {QStringLiteral("maskStack"), QVariantList{}},
       }},
  };
  const auto empty_mask_stack_validation =
      photo_editor::desktop::validate_agent_recipe_patch(empty_mask_stack_patch, style);
  if (empty_mask_stack_validation.valid ||
      !empty_mask_stack_validation.errors.join(QStringLiteral("; ")).contains(QStringLiteral("delete existing user masks"))) {
    return fail("recipe patch validation should reject empty mask stacks that would delete user masks");
  }
  photo_editor::desktop::EditRecipeState mask_state;
  const auto mask_before = mask_state;
  photo_editor::desktop::apply_agent_recipe_patch(&mask_state, mask_stack_patch);
  if (mask_state.mask_stack.size() != 3 ||
      mask_state.mask_stack.at(0).toMap().value(QStringLiteral("kind")).toString() != QStringLiteral("brush") ||
      mask_state.mask_stack.at(0).toMap().value(QStringLiteral("brushPoints")).toList().size() != 1 ||
      !almost_equal(mask_state.mask_stack.at(0).toMap().value(QStringLiteral("temperature")).toDouble(), -8.0) ||
      !almost_equal(mask_state.mask_stack.at(0).toMap().value(QStringLiteral("texture")).toDouble(), 18.0) ||
      !almost_equal(mask_state.mask_stack.at(0).toMap().value(QStringLiteral("sharpenRadius")).toDouble(), 1.7) ||
      !almost_equal(mask_state.mask_stack.at(0).toMap().value(QStringLiteral("luminanceNr")).toDouble(), 22.0) ||
      mask_state.mask_stack.at(1).toMap().value(QStringLiteral("operation")).toString() != QStringLiteral("subtract") ||
      mask_state.mask_stack.at(2).toMap().value(QStringLiteral("kind")).toString() != QStringLiteral("subject") ||
      !almost_equal(mask_state.mask_stack.at(2).toMap().value(QStringLiteral("selectionFeather")).toDouble(), 18.0)) {
    return fail("recipe patch should normalize and apply safe mask stacks");
  }
  const auto mask_diff =
      photo_editor::desktop::agent_recipe_patch_diff(mask_before, mask_state, mask_stack_patch);
  if (mask_diff.value(QStringLiteral("masks"))
          .toMap()
          .value(QStringLiteral("maskStack"))
          .toMap()
          .isEmpty()) {
    return fail("recipe patch diff should include mask stack changes");
  }
  const auto existing_first_mask_id =
      mask_state.mask_stack.first().toMap().value(QStringLiteral("id")).toString();
  const QVariantMap append_mask_stack_patch{
      {QStringLiteral("masks"),
       QVariantMap{
           {QStringLiteral("appendMaskStack"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-localized-face")},
                    {QStringLiteral("kind"), QStringLiteral("radial")},
                    {QStringLiteral("name"), QStringLiteral("Agent Face Lift")},
                    {QStringLiteral("centerX"), 0.52},
                    {QStringLiteral("centerY"), 0.38},
                    {QStringLiteral("radiusX"), 0.16},
                    {QStringLiteral("radiusY"), 0.22},
                    {QStringLiteral("feather"), 0.68},
                    {QStringLiteral("exposure"), 0.22},
                },
            }},
       }},
  };
  const auto append_mask_validation =
      photo_editor::desktop::validate_agent_recipe_patch(
          append_mask_stack_patch, style);
  photo_editor::desktop::apply_agent_recipe_patch(
      &mask_state, append_mask_stack_patch);
  if (!append_mask_validation.valid || mask_state.mask_stack.size() != 4 ||
      mask_state.mask_stack.first()
              .toMap()
              .value(QStringLiteral("id"))
              .toString() != existing_first_mask_id ||
      mask_state.mask_stack.last()
              .toMap()
              .value(QStringLiteral("id"))
              .toString() != QStringLiteral("agent-localized-face")) {
    return fail("append-mask recipe patches should preserve existing user masks and add the bounded local adjustment");
  }
  auto semantic_request_patch = append_mask_stack_patch;
  auto semantic_request_group =
      semantic_request_patch.value(QStringLiteral("masks")).toMap();
  auto semantic_request_masks =
      semantic_request_group.value(QStringLiteral("appendMaskStack")).toList();
  auto semantic_request_mask = semantic_request_masks.first().toMap();
  semantic_request_mask.insert(
      QStringLiteral("agentSelectionMode"), QStringLiteral("semantic"));
  semantic_request_mask.insert(
      QStringLiteral("agentSemanticTarget"), QStringLiteral("subject"));
  semantic_request_masks[0] = semantic_request_mask;
  semantic_request_group.insert(
      QStringLiteral("appendMaskStack"), semantic_request_masks);
  semantic_request_patch.insert(
      QStringLiteral("masks"), semantic_request_group);
  const auto semantic_request_validation =
      photo_editor::desktop::validate_agent_recipe_patch(
          semantic_request_patch, style);
  if (!semantic_request_validation.valid) {
    return fail("agent masks should accept a bounded semantic request with a radial fail-closed fallback");
  }
  semantic_request_mask.insert(
      QStringLiteral("agentSemanticTarget"), QStringLiteral("face"));
  semantic_request_masks[0] = semantic_request_mask;
  semantic_request_group.insert(
      QStringLiteral("appendMaskStack"), semantic_request_masks);
  semantic_request_patch.insert(
      QStringLiteral("masks"), semantic_request_group);
  const auto unsupported_semantic_request =
      photo_editor::desktop::validate_agent_recipe_patch(
          semantic_request_patch, style);
  if (unsupported_semantic_request.valid ||
      !unsupported_semantic_request.errors.join(QStringLiteral("; "))
           .contains(QStringLiteral("subject, background, or sky"))) {
    return fail("agent semantic requests should fail closed for unsupported semantic targets");
  }
  auto guided_request_patch = append_mask_stack_patch;
  auto guided_request_group =
      guided_request_patch.value(QStringLiteral("masks")).toMap();
  auto guided_request_masks =
      guided_request_group.value(QStringLiteral("appendMaskStack")).toList();
  auto guided_request_mask = guided_request_masks.first().toMap();
  guided_request_mask.insert(
      QStringLiteral("agentSelectionMode"), QStringLiteral("guided"));
  guided_request_mask.insert(
      QStringLiteral("agentSelectionLabel"), QStringLiteral("red jacket"));
  guided_request_masks[0] = guided_request_mask;
  guided_request_group.insert(
      QStringLiteral("appendMaskStack"), guided_request_masks);
  guided_request_patch.insert(
      QStringLiteral("masks"), guided_request_group);
  const auto guided_request_validation =
      photo_editor::desktop::validate_agent_recipe_patch(
          guided_request_patch, style);
  if (!guided_request_validation.valid) {
    return fail("agent masks should accept a labeled evidence-guided object request with a radial fail-closed fallback");
  }
  guided_request_mask.insert(
      QStringLiteral("agentSelectionCandidates"),
      QVariantList{
          QVariantMap{
              {QStringLiteral("id"), QStringLiteral("candidate-left-window")},
              {QStringLiteral("label"), QStringLiteral("Left window")},
              {QStringLiteral("confidence"), 0.78},
              {QStringLiteral("region"),
               QVariantMap{
                   {QStringLiteral("centerX"), 0.28},
                   {QStringLiteral("centerY"), 0.34},
                   {QStringLiteral("radiusX"), 0.12},
                   {QStringLiteral("radiusY"), 0.18},
                   {QStringLiteral("feather"), 0.68},
               }},
          },
          QVariantMap{
              {QStringLiteral("id"), QStringLiteral("candidate-right-window")},
              {QStringLiteral("label"), QStringLiteral("Right window")},
              {QStringLiteral("confidence"), 0.74},
              {QStringLiteral("region"),
               QVariantMap{
                   {QStringLiteral("centerX"), 0.76},
                   {QStringLiteral("centerY"), 0.32},
                   {QStringLiteral("radiusX"), 0.13},
                   {QStringLiteral("radiusY"), 0.19},
                   {QStringLiteral("feather"), 0.68},
               }},
          },
      });
  guided_request_masks[0] = guided_request_mask;
  guided_request_group.insert(
      QStringLiteral("appendMaskStack"), guided_request_masks);
  guided_request_patch.insert(
      QStringLiteral("masks"), guided_request_group);
  const auto candidate_guided_request =
      photo_editor::desktop::validate_agent_recipe_patch(
          guided_request_patch, style);
  if (!candidate_guided_request.valid) {
    return fail("ambiguous guided requests should accept two bounded, stable object candidates");
  }
  auto visual_referent_mask = guided_request_mask;
  visual_referent_mask.remove(QStringLiteral("agentSelectionCandidates"));
  visual_referent_mask.insert(
      QStringLiteral("agentVisualReferentId"),
      QStringLiteral("referent-right-window"));
  visual_referent_mask.insert(
      QStringLiteral("agentSelectionSource"),
      QStringLiteral("confirmed-visual-referent"));
  auto visual_referent_group = guided_request_group;
  visual_referent_group.insert(
      QStringLiteral("appendMaskStack"),
      QVariantList{visual_referent_mask});
  auto visual_referent_patch = guided_request_patch;
  visual_referent_patch.insert(
      QStringLiteral("masks"), visual_referent_group);
  const auto visual_referent_validation =
      photo_editor::desktop::validate_agent_recipe_patch(
          visual_referent_patch, style);
  if (!visual_referent_validation.valid) {
    return fail("confirmed visual referents should validate only as resolved guided selections");
  }
  visual_referent_mask.insert(
      QStringLiteral("agentSelectionCandidates"),
      guided_request_mask.value(QStringLiteral("agentSelectionCandidates")));
  visual_referent_group.insert(
      QStringLiteral("appendMaskStack"),
      QVariantList{visual_referent_mask});
  visual_referent_patch.insert(
      QStringLiteral("masks"), visual_referent_group);
  const auto ambiguous_visual_referent =
      photo_editor::desktop::validate_agent_recipe_patch(
          visual_referent_patch, style);
  if (ambiguous_visual_referent.valid ||
      !ambiguous_visual_referent.errors.join(QStringLiteral("; "))
           .contains(QStringLiteral("cannot retain ambiguous candidates"))) {
    return fail("a remembered object must not retain fresh ambiguous candidates");
  }
  auto invalid_candidates =
      guided_request_mask.value(QStringLiteral("agentSelectionCandidates")).toList();
  invalid_candidates.removeLast();
  guided_request_mask.insert(
      QStringLiteral("agentSelectionCandidates"), invalid_candidates);
  guided_request_masks[0] = guided_request_mask;
  guided_request_group.insert(
      QStringLiteral("appendMaskStack"), guided_request_masks);
  guided_request_patch.insert(
      QStringLiteral("masks"), guided_request_group);
  const auto underspecified_candidate_request =
      photo_editor::desktop::validate_agent_recipe_patch(
          guided_request_patch, style);
  if (underspecified_candidate_request.valid ||
      !underspecified_candidate_request.errors.join(QStringLiteral("; "))
           .contains(QStringLiteral("two or three"))) {
    return fail("ambiguous guided requests should fail closed unless at least two candidates are explicit");
  }
  guided_request_mask.remove(QStringLiteral("agentSelectionCandidates"));
  guided_request_mask.remove(QStringLiteral("agentSelectionLabel"));
  guided_request_masks[0] = guided_request_mask;
  guided_request_group.insert(
      QStringLiteral("appendMaskStack"), guided_request_masks);
  guided_request_patch.insert(
      QStringLiteral("masks"), guided_request_group);
  const auto unlabeled_guided_request =
      photo_editor::desktop::validate_agent_recipe_patch(
          guided_request_patch, style);
  if (unlabeled_guided_request.valid ||
      !unlabeled_guided_request.errors.join(QStringLiteral("; "))
           .contains(QStringLiteral("target label"))) {
    return fail("evidence-guided object requests should fail closed without a specific target label");
  }
  guided_request_mask.insert(
      QStringLiteral("agentSelectionLabel"), QStringLiteral("red jacket"));
  guided_request_mask.insert(
      QStringLiteral("agentSemanticTarget"), QStringLiteral("subject"));
  guided_request_masks[0] = guided_request_mask;
  guided_request_group.insert(
      QStringLiteral("appendMaskStack"), guided_request_masks);
  guided_request_patch.insert(
      QStringLiteral("masks"), guided_request_group);
  const auto mixed_guided_request =
      photo_editor::desktop::validate_agent_recipe_patch(
          guided_request_patch, style);
  if (mixed_guided_request.valid) {
    return fail("evidence-guided object requests should not silently mix semantic target contracts");
  }
  auto generated_agent_mask = mask_state.mask_stack.last().toMap();
  const QByteArray generated_alpha(16, static_cast<char>(0x7f));
  generated_agent_mask.insert(QStringLiteral("segmentationData"), generated_alpha);
  generated_agent_mask.insert(QStringLiteral("segmentationWidth"), 4);
  generated_agent_mask.insert(QStringLiteral("segmentationHeight"), 4);
  generated_agent_mask.insert(
      QStringLiteral("agentSemanticTarget"), QStringLiteral("subject"));
  mask_state.mask_stack[mask_state.mask_stack.size() - 1] = generated_agent_mask;
  auto updated_append_mask_stack_patch = append_mask_stack_patch;
  auto updated_append_group =
      updated_append_mask_stack_patch.value(QStringLiteral("masks")).toMap();
  auto updated_append_masks =
      updated_append_group.value(QStringLiteral("appendMaskStack")).toList();
  auto updated_appended_mask = updated_append_masks.first().toMap();
  updated_appended_mask.insert(QStringLiteral("kind"), QStringLiteral("subject"));
  updated_appended_mask.insert(QStringLiteral("exposure"), 0.31);
  updated_append_masks[0] = updated_appended_mask;
  updated_append_group.insert(
      QStringLiteral("appendMaskStack"), updated_append_masks);
  updated_append_mask_stack_patch.insert(
      QStringLiteral("masks"), updated_append_group);
  photo_editor::desktop::apply_agent_recipe_patch(
      &mask_state, updated_append_mask_stack_patch);
  if (mask_state.mask_stack.size() != 4 ||
      !almost_equal(
          mask_state.mask_stack.last()
              .toMap()
              .value(QStringLiteral("exposure"))
              .toDouble(),
          0.31) ||
      mask_state.mask_stack.last()
              .toMap()
              .value(QStringLiteral("segmentationData"))
              .toByteArray() != generated_alpha) {
    std::cerr << "updated stable mask="
              << QJsonDocument::fromVariant(mask_state.mask_stack.last())
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << " alphaBytes="
              << mask_state.mask_stack.last()
                     .toMap()
                     .value(QStringLiteral("segmentationData"))
                     .toByteArray()
                     .size()
              << '\n';
    return fail("append-mask recipe patches should update a stable agent mask without duplicating it or discarding its generated matte");
  }

  const QVariantMap malformed_mask_payload_patch{
      {QStringLiteral("masks"),
       QVariantMap{
           {QStringLiteral("maskStack"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-mask")},
                    {QStringLiteral("kind"), QStringLiteral("brush")},
                    {QStringLiteral("name"), QStringLiteral("Agent Mask")},
                    {QStringLiteral("brushPoints"), QVariantList{point(0.4, 0.4)}},
                    {QStringLiteral("segmentationData"), QByteArray("unsafe")},
                },
            }},
       }},
  };
  const auto malformed_mask_payload =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_mask_payload_patch, style);
  if (malformed_mask_payload.valid ||
      !malformed_mask_payload.errors.join(QStringLiteral("; ")).contains(QStringLiteral("cannot be supplied"))) {
    return fail("recipe patch validation should reject binary mask payload injection");
  }

  const QVariantMap malformed_brush_point_patch{
      {QStringLiteral("masks"),
       QVariantMap{
           {QStringLiteral("maskStack"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-mask")},
                    {QStringLiteral("kind"), QStringLiteral("brush")},
                    {QStringLiteral("name"), QStringLiteral("Agent Mask")},
                    {QStringLiteral("brushPoints"),
                     QVariantList{
                         QVariantMap{
                             {QStringLiteral("x"), 1.4},
                             {QStringLiteral("y"), 0.4},
                         },
                     }},
                },
            }},
       }},
  };
  const auto malformed_brush_point =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_brush_point_patch, style);
  if (malformed_brush_point.valid ||
      !malformed_brush_point.errors.join(QStringLiteral("; ")).contains(QStringLiteral("between 0 and 1"))) {
    return fail("recipe patch validation should enforce brush point bounds");
  }

  const QVariantMap malformed_mask_detail_patch{
      {QStringLiteral("masks"),
       QVariantMap{
           {QStringLiteral("maskStack"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-mask")},
                    {QStringLiteral("kind"), QStringLiteral("brush")},
                    {QStringLiteral("name"), QStringLiteral("Agent Mask")},
                    {QStringLiteral("texture"), 130.0},
                    {QStringLiteral("brushPoints"), QVariantList{point(0.4, 0.4)}},
                },
            }},
       }},
  };
  const auto malformed_mask_detail =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_mask_detail_patch, style);
  if (malformed_mask_detail.valid ||
      !malformed_mask_detail.errors.join(QStringLiteral("; ")).contains(QStringLiteral("between -100 and 100"))) {
    return fail("recipe patch validation should enforce local mask detail adjustment bounds");
  }

  const QVariantMap malformed_lasso_patch{
      {QStringLiteral("masks"),
       QVariantMap{
           {QStringLiteral("maskStack"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-lasso")},
                    {QStringLiteral("kind"), QStringLiteral("lasso")},
                    {QStringLiteral("name"), QStringLiteral("Agent Lasso")},
                    {QStringLiteral("polygonPoints"), QVariantList{point(0.2, 0.2), point(0.7, 0.7)}},
                },
            }},
       }},
  };
  const auto malformed_lasso =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_lasso_patch, style);
  if (malformed_lasso.valid ||
      !malformed_lasso.errors.join(QStringLiteral("; ")).contains(QStringLiteral("between 3 and 256"))) {
    return fail("recipe patch validation should require valid lasso polygons");
  }

  const QVariantMap malformed_retouch_mask_patch{
      {QStringLiteral("masks"),
       QVariantMap{
           {QStringLiteral("maskStack"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-retouch")},
                    {QStringLiteral("kind"), QStringLiteral("brush")},
                    {QStringLiteral("name"), QStringLiteral("Agent Retouch")},
                    {QStringLiteral("retouchMode"), QStringLiteral("heal")},
                    {QStringLiteral("brushPoints"), QVariantList{point(0.4, 0.4)}},
                },
            }},
       }},
  };
  const auto malformed_retouch_mask =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_retouch_mask_patch, style);
  if (malformed_retouch_mask.valid ||
      !malformed_retouch_mask.errors.join(QStringLiteral("; ")).contains(QStringLiteral("retouch fields"))) {
    return fail("recipe patch validation should keep retouch fields behind explicit retouch permissions");
  }

  const QVariantList valid_retouch_stack{
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("agent-heal-retouch")},
          {QStringLiteral("kind"), QStringLiteral("brush")},
          {QStringLiteral("name"), QStringLiteral("Agent Heal")},
          {QStringLiteral("retouchMode"), QStringLiteral("heal")},
          {QStringLiteral("removeStrength"), 68.0},
          {QStringLiteral("healTextureBlend"), 0.32},
          {QStringLiteral("healColorBlend"), 0.74},
          {QStringLiteral("brushSize"), 0.06},
          {QStringLiteral("brushFlow"), 0.78},
          {QStringLiteral("committed"), false},
          {QStringLiteral("brushPoints"),
           QVariantList{
               QVariantMap{
                   {QStringLiteral("x"), 0.36},
                   {QStringLiteral("y"), 0.44},
                   {QStringLiteral("size"), 0.06},
                   {QStringLiteral("flow"), 0.78},
               },
           }},
      },
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("agent-remove-retouch")},
          {QStringLiteral("kind"), QStringLiteral("object")},
          {QStringLiteral("name"), QStringLiteral("Agent Remove")},
          {QStringLiteral("retouchMode"), QStringLiteral("remove")},
          {QStringLiteral("removeStrength"), 90.0},
      },
  };
  const QVariantMap retouch_stack_patch{
      {QStringLiteral("retouch"),
       QVariantMap{
           {QStringLiteral("retouchStack"), valid_retouch_stack},
       }},
  };
  const auto retouch_stack_validation =
      photo_editor::desktop::validate_agent_recipe_patch(retouch_stack_patch, style);
  if (!retouch_stack_validation.valid) {
    std::cerr << retouch_stack_validation.errors.join(QStringLiteral("; ")).toStdString() << '\n';
    return fail("recipe patch validation should accept explicit retouch stacks when retouch is allowed");
  }
  if (!retouch_stack_validation.warnings.join(QStringLiteral("; ")).contains(QStringLiteral("Retouch edits"))) {
    return fail("recipe patch validation should warn for retouch tool usage");
  }
  const QVariantMap empty_retouch_stack_patch{
      {QStringLiteral("retouch"),
       QVariantMap{
           {QStringLiteral("retouchStack"), QVariantList{}},
       }},
  };
  const auto empty_retouch_stack_validation =
      photo_editor::desktop::validate_agent_recipe_patch(empty_retouch_stack_patch, style);
  if (empty_retouch_stack_validation.valid ||
      !empty_retouch_stack_validation.errors.join(QStringLiteral("; ")).contains(QStringLiteral("delete existing user retouch edits"))) {
    return fail("recipe patch validation should reject empty retouch stacks that would delete user retouch edits");
  }
  auto no_retouch_style = style;
  auto no_retouch_tools = no_retouch_style.value(QStringLiteral("allowedTools")).toMap();
  no_retouch_tools.insert(QStringLiteral("retouch"), false);
  no_retouch_style.insert(QStringLiteral("allowedTools"), no_retouch_tools);
  const auto retouch_disallowed =
      photo_editor::desktop::validate_agent_recipe_patch(retouch_stack_patch, no_retouch_style);
  if (retouch_disallowed.valid ||
      !retouch_disallowed.errors.join(QStringLiteral("; ")).contains(QStringLiteral("does not allow"))) {
    return fail("recipe patch validation should enforce explicit retouch permissions");
  }
  photo_editor::desktop::EditRecipeState retouch_state;
  retouch_state.mask_stack = valid_mask_stack;
  const auto retouch_before = retouch_state;
  photo_editor::desktop::apply_agent_recipe_patch(&retouch_state, retouch_stack_patch);
  if (retouch_state.mask_stack.size() != 5 ||
      retouch_state.mask_stack.at(0).toMap().value(QStringLiteral("toolRole")).toString() !=
          QStringLiteral("selection") ||
      retouch_state.mask_stack.at(3).toMap().value(QStringLiteral("toolRole")).toString() !=
          QStringLiteral("retouch") ||
      retouch_state.mask_stack.at(3).toMap().value(QStringLiteral("retouchMode")).toString() !=
          QStringLiteral("heal") ||
      !almost_equal(
          retouch_state.mask_stack.at(3).toMap().value(QStringLiteral("healColorBlend")).toDouble(),
          0.74) ||
      retouch_state.mask_stack.at(4).toMap().value(QStringLiteral("retouchMode")).toString() !=
          QStringLiteral("remove") ||
      !almost_equal(
          retouch_state.mask_stack.at(4).toMap().value(QStringLiteral("removeStrength")).toDouble(),
          90.0)) {
    return fail("recipe patch should replace retouch entries while preserving selection masks");
  }
  const auto retouch_diff =
      photo_editor::desktop::agent_recipe_patch_diff(retouch_before, retouch_state, retouch_stack_patch);
  if (retouch_diff.value(QStringLiteral("retouch"))
          .toMap()
          .value(QStringLiteral("retouchStack"))
          .toMap()
          .isEmpty()) {
    return fail("recipe patch diff should include retouch stack changes");
  }

  auto appended_retouch = valid_retouch_stack.first().toMap();
  appended_retouch.insert(
      QStringLiteral("id"), QStringLiteral("agent-heal-appended"));
  appended_retouch.insert(
      QStringLiteral("name"), QStringLiteral("Agent Heal Appended"));
  const QVariantMap append_retouch_patch{
      {QStringLiteral("retouch"),
       QVariantMap{
           {QStringLiteral("appendRetouchStack"),
            QVariantList{appended_retouch}},
       }},
  };
  const auto append_retouch_validation =
      photo_editor::desktop::validate_agent_recipe_patch(
          append_retouch_patch, style);
  if (!append_retouch_validation.valid) {
    return fail("recipe patch validation should accept append-only retouch edits");
  }
  photo_editor::desktop::apply_agent_recipe_patch(
      &retouch_state, append_retouch_patch);
  const auto appended_retouch_stack = retouch_state.mask_stack;
  auto existing_retouch_count = 0;
  auto appended_retouch_count = 0;
  for (const auto& value : appended_retouch_stack) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("id")).toString() ==
        QStringLiteral("agent-heal-retouch")) {
      ++existing_retouch_count;
    }
    if (mask.value(QStringLiteral("id")).toString() ==
        QStringLiteral("agent-heal-appended")) {
      ++appended_retouch_count;
    }
  }
  if (existing_retouch_count != 1 || appended_retouch_count != 1 ||
      appended_retouch_stack.size() != 6) {
    return fail("append-only retouch edits should preserve existing selection and retouch entries");
  }
  appended_retouch.insert(QStringLiteral("removeStrength"), 54.0);
  photo_editor::desktop::apply_agent_recipe_patch(
      &retouch_state,
      QVariantMap{
          {QStringLiteral("retouch"),
           QVariantMap{
               {QStringLiteral("appendRetouchStack"),
                QVariantList{appended_retouch}},
           }},
      });
  auto updated_appended_count = 0;
  auto updated_appended_strength = 0.0;
  for (const auto& value : retouch_state.mask_stack) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("id")).toString() ==
        QStringLiteral("agent-heal-appended")) {
      ++updated_appended_count;
      updated_appended_strength =
          mask.value(QStringLiteral("removeStrength")).toDouble();
    }
  }
  if (updated_appended_count != 1 ||
      !almost_equal(updated_appended_strength, 54.0) ||
      retouch_state.mask_stack.size() != 6) {
    return fail("append-only retouch retries should update their own stable id without duplicating edits");
  }

  const QVariantMap malformed_retouch_payload_patch{
      {QStringLiteral("retouch"),
       QVariantMap{
           {QStringLiteral("retouchStack"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-remove")},
                    {QStringLiteral("kind"), QStringLiteral("object")},
                    {QStringLiteral("name"), QStringLiteral("Agent Remove")},
                    {QStringLiteral("retouchMode"), QStringLiteral("remove")},
                    {QStringLiteral("removeStrength"), 90.0},
                    {QStringLiteral("inpaintPatchData"), QByteArray("unsafe-patch")},
                },
            }},
       }},
  };
  const auto malformed_retouch_payload =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_retouch_payload_patch, style);
  if (malformed_retouch_payload.valid ||
      !malformed_retouch_payload.errors.join(QStringLiteral("; ")).contains(QStringLiteral("cannot be supplied"))) {
    return fail("recipe patch validation should reject inpaint patch payload injection");
  }

  const QVariantMap malformed_retouch_mode_patch{
      {QStringLiteral("retouch"),
       QVariantMap{
           {QStringLiteral("retouchStack"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-retouch")},
                    {QStringLiteral("kind"), QStringLiteral("brush")},
                    {QStringLiteral("name"), QStringLiteral("Agent Retouch")},
                    {QStringLiteral("retouchMode"), QStringLiteral("liquify")},
                    {QStringLiteral("brushPoints"), QVariantList{point(0.4, 0.4)}},
                },
            }},
       }},
  };
  const auto malformed_retouch_mode =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_retouch_mode_patch, style);
  if (malformed_retouch_mode.valid ||
      !malformed_retouch_mode.errors.join(QStringLiteral("; ")).contains(QStringLiteral("retouchMode"))) {
    return fail("recipe patch validation should enforce retouch modes");
  }

  const QVariantMap retired_noop_raw_patch{
      {QStringLiteral("raw"),
       QVariantMap{
           {QStringLiteral("rawBoost"), 1.2},
           {QStringLiteral("rawGamutMapping"), false},
       }},
  };
  const auto retired_noop_raw_validation =
      photo_editor::desktop::validate_agent_recipe_patch(
          retired_noop_raw_patch, style);
  if (retired_noop_raw_validation.valid ||
      !retired_noop_raw_validation.errors.join(QStringLiteral("; "))
           .contains(QStringLiteral("rawBoost"))) {
    return fail(
        "agent recipe patches must reject retired RAW controls that never altered pixels");
  }

  auto coverage_style = layers_style;
  const QVariantMap coverage_patch{
      {QStringLiteral("raw"),
       QVariantMap{
           {QStringLiteral("rawExposure"), 0.4},
           {QStringLiteral("rawHotDeadPixelCleanup"), 0.41},
           {QStringLiteral("rawFalseColorSuppression"), 0.29},
           {QStringLiteral("rawHighlightRecovery"), false},
           {QStringLiteral("rawDemosaicQuality"), QStringLiteral("Full")},
           {QStringLiteral("rawLensCorrection"), false},
           {QStringLiteral("rawWhiteBalanceOverride"), true},
           {QStringLiteral("rawCameraMatchingLook"), QStringLiteral("Portrait")},
           {QStringLiteral("rawWhiteBalanceTemperature"), 5600.0},
           {QStringLiteral("rawWhiteBalanceTint"), 12.0},
           {QStringLiteral("rawBlackLevel"), 0.016},
           {QStringLiteral("rawWhiteLevel"), 1.16},
           {QStringLiteral("rawCalibrationHue"), -11.0},
           {QStringLiteral("rawCalibrationSaturation"), 21.0},
           {QStringLiteral("rawRedPrimaryHue"), 7.0},
           {QStringLiteral("rawRedPrimarySaturation"), 18.0},
           {QStringLiteral("rawGreenPrimaryHue"), -9.0},
           {QStringLiteral("rawGreenPrimarySaturation"), -12.0},
           {QStringLiteral("rawBluePrimaryHue"), 14.0},
           {QStringLiteral("rawBluePrimarySaturation"), 16.0},
       }},
      {QStringLiteral("globalTone"),
       QVariantMap{
           {QStringLiteral("exposure"), 0.3},
           {QStringLiteral("contrast"), 11.0},
           {QStringLiteral("highlights"), -20.0},
           {QStringLiteral("shadows"), 18.0},
           {QStringLiteral("whites"), -5.0},
           {QStringLiteral("blacks"), -9.0},
           {QStringLiteral("brightness"), 7.0},
           {QStringLiteral("hdrLocalToneMapping"), 14.0},
           {QStringLiteral("filmicShoulder"), -6.0},
           {QStringLiteral("filmicToe"), 5.0},
           {QStringLiteral("adaptiveContrast"), 9.0},
           {QStringLiteral("levelsBlackPoint"), -3.0},
           {QStringLiteral("levelsGamma"), 4.0},
           {QStringLiteral("levelsWhitePoint"), 6.0},
           {QStringLiteral("zoneDeepShadows"), -8.0},
           {QStringLiteral("zoneShadows"), 7.0},
           {QStringLiteral("zoneMidtones"), 5.0},
           {QStringLiteral("zoneLights"), 4.0},
           {QStringLiteral("zoneHighlights"), -7.0},
       }},
      {QStringLiteral("color"),
       QVariantMap{
           {QStringLiteral("temperature"), 8.0},
           {QStringLiteral("tint"), 3.0},
           {QStringLiteral("vibrance"), 10.0},
           {QStringLiteral("saturation"), 2.0},
       }},
      {QStringLiteral("curves"),
       QVariantMap{
           {QStringLiteral("curveShadows"), 3.0},
           {QStringLiteral("curveDarks"), 2.0},
           {QStringLiteral("curveLights"), 6.0},
           {QStringLiteral("curveHighlights"), -4.0},
           {QStringLiteral("masterCurvePoints"),
            curve_points({{0.0, 0.02}, {0.32, 0.18}, {0.70, 0.86}, {1.0, 0.98}})},
           {QStringLiteral("lumaCurvePoints"),
            curve_points({{0.0, 0.0}, {0.50, 0.44}, {1.0, 1.0}})},
           {QStringLiteral("redCurvePoints"),
            curve_points({{0.0, 0.0}, {0.40, 0.58}, {1.0, 1.0}})},
           {QStringLiteral("greenCurvePoints"),
            curve_points({{0.0, 0.0}, {0.55, 0.48}, {1.0, 1.0}})},
           {QStringLiteral("blueCurvePoints"),
            curve_points({{0.0, 0.0}, {0.70, 0.60}, {1.0, 1.0}})},
       }},
      {QStringLiteral("hsl"),
       QVariantMap{
           {QStringLiteral("hueAdjustment"), 17.0},
           {QStringLiteral("saturationAdjustment"), -11.0},
           {QStringLiteral("luminanceAdjustment"), 9.0},
       }},
      {QStringLiteral("colorMixer"),
       QVariantMap{
           {QStringLiteral("colorMixerModel"), QStringLiteral("LCH")},
           {QStringLiteral("hslChannel"), QStringLiteral("Blues")},
           {QStringLiteral("redsHue"), -18.0},
           {QStringLiteral("redsSaturation"), 12.0},
           {QStringLiteral("redsLuminance"), 7.0},
           {QStringLiteral("redsSelectiveCyan"), 22.0},
           {QStringLiteral("redsSelectiveMagenta"), -14.0},
           {QStringLiteral("redsSelectiveYellow"), 11.0},
           {QStringLiteral("redsSelectiveBlack"), 9.0},
           {QStringLiteral("orangesSaturation"), 16.0},
           {QStringLiteral("yellowsLuminance"), -10.0},
           {QStringLiteral("greensSelectiveCyan"), -24.0},
           {QStringLiteral("aquasSelectiveMagenta"), 19.0},
           {QStringLiteral("bluesHue"), 17.0},
           {QStringLiteral("bluesSaturation"), -11.0},
           {QStringLiteral("bluesLuminance"), 9.0},
           {QStringLiteral("bluesSelectiveYellow"), -21.0},
           {QStringLiteral("purplesSelectiveBlack"), 13.0},
           {QStringLiteral("magentasHue"), -7.0},
           {QStringLiteral("channelMixerRedFromRed"), 108.0},
           {QStringLiteral("channelMixerRedFromGreen"), 18.0},
           {QStringLiteral("channelMixerRedFromBlue"), -12.0},
           {QStringLiteral("channelMixerGreenFromRed"), -8.0},
           {QStringLiteral("channelMixerGreenFromGreen"), 96.0},
           {QStringLiteral("channelMixerGreenFromBlue"), 14.0},
           {QStringLiteral("channelMixerBlueFromRed"), 6.0},
           {QStringLiteral("channelMixerBlueFromGreen"), -16.0},
           {QStringLiteral("channelMixerBlueFromBlue"), 112.0},
           {QStringLiteral("blackAndWhiteEnabled"), true},
           {QStringLiteral("blackAndWhiteReds"), 18.0},
           {QStringLiteral("blackAndWhiteOranges"), 12.0},
           {QStringLiteral("blackAndWhiteYellows"), 8.0},
           {QStringLiteral("blackAndWhiteGreens"), -6.0},
           {QStringLiteral("blackAndWhiteAquas"), -8.0},
           {QStringLiteral("blackAndWhiteBlues"), -14.0},
           {QStringLiteral("blackAndWhitePurples"), 4.0},
           {QStringLiteral("blackAndWhiteMagentas"), 6.0},
           {QStringLiteral("colorReplacementEnabled"), true},
           {QStringLiteral("colorReplacementSourceHue"), 0.12},
           {QStringLiteral("colorReplacementTargetHue"), 0.58},
           {QStringLiteral("colorReplacementRange"), 44.0},
           {QStringLiteral("colorReplacementSoftness"), 72.0},
           {QStringLiteral("colorReplacementStrength"), 36.0},
           {QStringLiteral("colorReplacementPreserveLuminance"), false},
           {QStringLiteral("colorReplacementPreserveSaturation"), true},
           {QStringLiteral("skinToneProtectionEnabled"), true},
           {QStringLiteral("skinToneLineHue"), 0.08},
           {QStringLiteral("skinToneProtectionRange"), 32.0},
           {QStringLiteral("skinToneProtectionStrength"), 58.0},
       }},
      {QStringLiteral("colorGrading"),
       QVariantMap{
           {QStringLiteral("colorHarmonyEnabled"), true},
           {QStringLiteral("colorHarmonyMode"), QStringLiteral("Tetradic")},
           {QStringLiteral("colorHarmonyBaseHue"), 0.24},
           {QStringLiteral("colorHarmonyStrength"), 42.0},
           {QStringLiteral("gradingShadowsHue"), 0.64},
           {QStringLiteral("gradingShadowsSaturation"), 22.0},
           {QStringLiteral("gradingShadowsLuminance"), -14.0},
           {QStringLiteral("gradingMidtonesHue"), 0.08},
           {QStringLiteral("gradingMidtonesSaturation"), 12.0},
           {QStringLiteral("gradingMidtonesLuminance"), 5.0},
           {QStringLiteral("gradingHighlightsHue"), 0.12},
           {QStringLiteral("gradingHighlightsSaturation"), -8.0},
           {QStringLiteral("gradingHighlightsLuminance"), 18.0},
           {QStringLiteral("gradingAmount"), 74.0},
           {QStringLiteral("gradingBalance"), -24.0},
       }},
      {QStringLiteral("detail"),
       QVariantMap{
           {QStringLiteral("texture"), 3.0},
           {QStringLiteral("clarity"), 4.0},
           {QStringLiteral("dehaze"), 2.0},
           {QStringLiteral("sharpening"), 22.0},
           {QStringLiteral("sharpenRadius"), 1.8},
           {QStringLiteral("sharpenDetail"), 30.0},
           {QStringLiteral("sharpenMasking"), 45.0},
           {QStringLiteral("luminanceNr"), 12.0},
           {QStringLiteral("colorNr"), 16.0},
       }},
      {QStringLiteral("optics"),
       QVariantMap{
           {QStringLiteral("lensDistortion"), -2.0},
           {QStringLiteral("lensVignette"), 6.0},
           {QStringLiteral("chromaticAberration"), 20.0},
       }},
      {QStringLiteral("crop"),
       QVariantMap{
           {QStringLiteral("perspectiveVertical"), 2.0},
           {QStringLiteral("perspectiveHorizontal"), -2.0},
           {QStringLiteral("transformRotation"), 1.0},
           {QStringLiteral("transformScale"), 1.2},
           {QStringLiteral("transformSkewX"), 1.5},
           {QStringLiteral("transformSkewY"), -1.5},
           {QStringLiteral("cropPreset"), QStringLiteral("3:2")},
           {QStringLiteral("cropX"), 0.12},
           {QStringLiteral("cropY"), 0.08},
           {QStringLiteral("cropWidth"), 0.71},
           {QStringLiteral("cropHeight"), 0.48},
           {QStringLiteral("cropZoom"), 1.1},
           {QStringLiteral("uprightMode"), QStringLiteral("Guided")},
           {QStringLiteral("uprightVerticalGuideAngle"), 12.0},
           {QStringLiteral("uprightHorizontalGuideAngle"), -8.0},
           {QStringLiteral("contentAwareCropFill"), true},
       }},
      {QStringLiteral("output"),
       QVariantMap{
           {QStringLiteral("outputColorSpace"), QStringLiteral("Display P3")},
           {QStringLiteral("softProofEnabled"), true},
           {QStringLiteral("softProofProfile"), QStringLiteral("sRGB")},
           {QStringLiteral("renderIntent"), QStringLiteral("Relative Colorimetric")},
           {QStringLiteral("outputHeadroom"), 2.0},
       }},
      {QStringLiteral("masks"),
       QVariantMap{
           {QStringLiteral("linearMaskExposure"), 0.35},
           {QStringLiteral("linearMaskTemperature"), 12.0},
           {QStringLiteral("linearMaskFeather"), 0.3},
           {QStringLiteral("linearMaskPosition"), 0.45},
           {QStringLiteral("linearMaskRotation"), -8.0},
           {QStringLiteral("radialMaskExposure"), -0.25},
           {QStringLiteral("radialMaskTemperature"), -6.0},
           {QStringLiteral("radialMaskFeather"), 0.4},
           {QStringLiteral("radialMaskCenterX"), 0.52},
           {QStringLiteral("radialMaskCenterY"), 0.48},
           {QStringLiteral("radialMaskRadiusX"), 0.32},
           {QStringLiteral("radialMaskRadiusY"), 0.28},
           {QStringLiteral("radialMaskRotation"), 6.0},
           {QStringLiteral("maskStack"), valid_mask_stack},
       }},
      {QStringLiteral("layers"),
       QVariantMap{
           {QStringLiteral("photoLayers"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("coverage-agent-layer")},
                    {QStringLiteral("kind"), QStringLiteral("adjustment")},
                    {QStringLiteral("name"), QStringLiteral("Coverage Agent Layer")},
                    {QStringLiteral("enabled"), true},
                    {QStringLiteral("opacity"), 0.57},
                    {QStringLiteral("blendMode"), QStringLiteral("overlay")},
                    {QStringLiteral("adjustments"),
                     QVariantMap{
                         {QStringLiteral("lutStack"), valid_lut_stack},
                     }},
                },
            }},
       }},
      {QStringLiteral("retouch"),
       QVariantMap{
           {QStringLiteral("retouchStack"), valid_retouch_stack},
       }},
  };
  const auto coverage_validation =
      photo_editor::desktop::validate_agent_recipe_patch(coverage_patch, coverage_style);
  if (!coverage_validation.valid) {
    std::cerr << coverage_validation.errors.join(QStringLiteral("; ")).toStdString() << '\n';
    return fail("typed recipe patch should expose the current supported edit field coverage");
  }
  const auto tool_schema = photo_editor::desktop::agent_recipe_patch_tool_schema();
  const auto validation_coverage =
      photo_editor::desktop::agent_recipe_patch_validation_coverage();
  const auto schema_groups = tool_schema.value(QStringLiteral("groups")).toMap();
  const auto validation_groups =
      validation_coverage.value(QStringLiteral("groups")).toMap();
  const auto security_review_gate =
      validation_coverage.value(QStringLiteral("securityReviewGate")).toMap();
  const QStringList required_color_groups{
      QStringLiteral("raw"),
      QStringLiteral("globalTone"),
      QStringLiteral("color"),
      QStringLiteral("curves"),
      QStringLiteral("hsl"),
      QStringLiteral("colorMixer"),
      QStringLiteral("colorGrading"),
      QStringLiteral("detail"),
      QStringLiteral("output"),
  };
  if (!tool_schema.value(QStringLiteral("typedSchema")).toBool() ||
      !tool_schema.value(QStringLiteral("validationBacked")).toBool() ||
      !tool_schema.value(QStringLiteral("allColorEditGroupsPresent")).toBool() ||
      tool_schema.value(QStringLiteral("colorEditGroups")).toStringList() !=
          required_color_groups ||
      !validation_coverage.value(QStringLiteral("validatesEveryExposedField")).toBool() ||
      !validation_coverage.value(QStringLiteral("validatesUnsupportedFields")).toBool() ||
      !validation_coverage.value(QStringLiteral("validatesToolPermissions")).toBool() ||
      !security_review_gate.value(QStringLiteral("required")).toBool() ||
      !security_review_gate.value(QStringLiteral("passed")).toBool() ||
      !security_review_gate.value(QStringLiteral("blocksDirectRecipePayloads")).toBool() ||
      !security_review_gate.value(QStringLiteral("blocksNestedRecipePayloads")).toBool() ||
      !security_review_gate.value(QStringLiteral("blocksAgentSuppliedMaskBinaryPayloads")).toBool() ||
      !security_review_gate.value(QStringLiteral("blocksAgentSuppliedInpaintPayloads")).toBool() ||
      !security_review_gate.value(QStringLiteral("requiresExplicitRetouchPermission")).toBool() ||
      tool_schema.value(QStringLiteral("fieldCount")).toInt() <= 0 ||
      validation_coverage.value(QStringLiteral("fieldCount")).toInt() !=
          tool_schema.value(QStringLiteral("fieldCount")).toInt()) {
    return fail("agent recipe patch schema should expose typed, validation-backed color tool coverage");
  }
  const auto mask_stack_schema = schema_groups.value(QStringLiteral("masks"))
                                     .toMap()
                                     .value(QStringLiteral("fields"))
                                     .toMap()
                                     .value(QStringLiteral("maskStack"))
                                     .toMap();
  const auto append_mask_stack_schema =
      schema_groups.value(QStringLiteral("masks"))
          .toMap()
          .value(QStringLiteral("fields"))
          .toMap()
          .value(QStringLiteral("appendMaskStack"))
          .toMap();
  if (append_mask_stack_schema.value(QStringLiteral("type")).toString() !=
      QStringLiteral("maskStack")) {
    return fail("agent recipe patch schema should expose non-destructive append-mask edits");
  }
  const auto append_retouch_stack_schema =
      schema_groups.value(QStringLiteral("retouch"))
          .toMap()
          .value(QStringLiteral("fields"))
          .toMap()
          .value(QStringLiteral("appendRetouchStack"))
          .toMap();
  if (append_retouch_stack_schema.value(QStringLiteral("type")).toString() !=
      QStringLiteral("retouchStack")) {
    return fail("agent recipe patch schema should expose non-destructive append-retouch edits");
  }
  const auto mask_creation_kinds =
      mask_stack_schema.value(QStringLiteral("maskCreationKinds")).toStringList();
  const auto local_adjustment_fields =
      mask_stack_schema.value(QStringLiteral("localAdjustmentFields")).toStringList();
  const QStringList required_mask_kinds{
      QStringLiteral("linear"),
      QStringLiteral("radial"),
      QStringLiteral("brush"),
      QStringLiteral("polygon"),
      QStringLiteral("lasso"),
      QStringLiteral("luminance"),
      QStringLiteral("color"),
      QStringLiteral("depth"),
      QStringLiteral("subject"),
      QStringLiteral("sky"),
      QStringLiteral("object"),
  };
  const QStringList required_local_adjustments{
      QStringLiteral("exposure"),
      QStringLiteral("temperature"),
      QStringLiteral("contrast"),
      QStringLiteral("hueShift"),
      QStringLiteral("texture"),
      QStringLiteral("sharpenRadius"),
      QStringLiteral("luminanceNr"),
      QStringLiteral("opacity"),
      QStringLiteral("blendMode"),
      QStringLiteral("colorizeStrength"),
  };
  for (const auto& required_kind : required_mask_kinds) {
    if (!mask_creation_kinds.contains(required_kind)) {
      return fail("agent recipe patch schema should expose every supported mask creation kind");
    }
  }
  for (const auto& required_adjustment : required_local_adjustments) {
    if (!local_adjustment_fields.contains(required_adjustment)) {
      return fail("agent recipe patch schema should expose every supported local adjustment field");
    }
  }
  for (auto group_iterator = coverage_patch.cbegin(); group_iterator != coverage_patch.cend(); ++group_iterator) {
    const auto schema_group = schema_groups.value(group_iterator.key()).toMap();
    const auto validation_group = validation_groups.value(group_iterator.key()).toMap();
    const auto schema_fields = schema_group.value(QStringLiteral("fields")).toMap();
    const auto patch_fields = group_iterator.value().toMap();
    if (schema_group.isEmpty() || validation_group.isEmpty() ||
        !validation_group.value(QStringLiteral("validated")).toBool() ||
        !validation_group.value(QStringLiteral("appliedByRecipePatch")).toBool()) {
      return fail("agent recipe patch schema should include validated coverage for every exposed group");
    }
    for (auto field_iterator = patch_fields.cbegin(); field_iterator != patch_fields.cend(); ++field_iterator) {
      const auto field_schema = schema_fields.value(field_iterator.key()).toMap();
      if (field_schema.isEmpty() ||
          !field_schema.value(QStringLiteral("validated")).toBool() ||
          !field_schema.value(QStringLiteral("appliedByRecipePatch")).toBool() ||
          field_schema.value(QStringLiteral("type")).toString().isEmpty()) {
        return fail("agent recipe patch schema should include typed validation metadata for every covered field");
      }
    }
  }

  photo_editor::desktop::EditRecipeState coverage_state;
  const auto coverage_before = coverage_state;
  photo_editor::desktop::apply_agent_recipe_patch(&coverage_state, coverage_patch);
  if (!almost_equal(coverage_state.raw_white_balance_temperature, 5600.0) ||
      !almost_equal(coverage_state.raw_hot_dead_pixel_cleanup, 0.41) ||
      !almost_equal(coverage_state.raw_false_color_suppression, 0.29) ||
      coverage_state.raw_highlight_recovery ||
      coverage_state.raw_demosaic_quality != QStringLiteral("Full") ||
      coverage_state.raw_gamut_mapping ||
      coverage_state.raw_lens_correction ||
      !coverage_state.raw_white_balance_override ||
      coverage_state.raw_camera_matching_look != QStringLiteral("Portrait") ||
      !almost_equal(coverage_state.raw_black_level, 0.016) ||
      !almost_equal(coverage_state.raw_white_level, 1.16) ||
      !almost_equal(coverage_state.raw_calibration_hue, -11.0) ||
      !almost_equal(coverage_state.raw_red_primary_saturation, 18.0) ||
      !almost_equal(coverage_state.raw_green_primary_hue, -9.0) ||
      !almost_equal(coverage_state.raw_blue_primary_saturation, 16.0) ||
      !almost_equal(coverage_state.zone_highlights, -7.0) ||
      !almost_equal(coverage_state.temperature, 8.0) ||
      !almost_equal(coverage_state.curve_lights, 6.0) ||
      coverage_state.master_curve_points.size() != 4 ||
      coverage_state.luma_curve_points.size() != 3 ||
      coverage_state.red_curve_points.size() != 3 ||
      coverage_state.green_curve_points.size() != 3 ||
      coverage_state.blue_curve_points.size() != 3 ||
      !almost_equal(curve_point_coordinate(coverage_state.master_curve_points, 1, QStringLiteral("x")), 0.32) ||
      !almost_equal(curve_point_coordinate(coverage_state.master_curve_points, 2, QStringLiteral("y")), 0.86) ||
      !almost_equal(curve_point_coordinate(coverage_state.blue_curve_points, 1, QStringLiteral("y")), 0.60) ||
      coverage_state.color_mixer_model != QStringLiteral("LCH") ||
      coverage_state.hsl_channel != QStringLiteral("Blues") ||
      !almost_equal(coverage_state.hue_adjustment, 17.0) ||
      !almost_equal(coverage_state.saturation_adjustment, -11.0) ||
      !almost_equal(coverage_state.luminance_adjustment, 9.0) ||
      !almost_equal(coverage_state.mixer_channels[0].hue, -18.0) ||
      !almost_equal(coverage_state.mixer_channels[0].selective_cyan, 22.0) ||
      !almost_equal(coverage_state.mixer_channels[3].selective_cyan, -24.0) ||
      !almost_equal(coverage_state.mixer_channels[5].selective_yellow, -21.0) ||
      !almost_equal(coverage_state.rgb_channel_mixer.red_from_green, 18.0) ||
      !almost_equal(coverage_state.rgb_channel_mixer.blue_from_blue, 112.0) ||
      !coverage_state.black_and_white_enabled ||
      !almost_equal(coverage_state.black_and_white_mixer[0], 18.0) ||
      !almost_equal(coverage_state.black_and_white_mixer[5], -14.0) ||
      !coverage_state.color_replacement_enabled ||
      !almost_equal(coverage_state.color_replacement_target_hue, 0.58) ||
      !almost_equal(coverage_state.color_replacement_strength, 36.0) ||
      coverage_state.color_replacement_preserve_luminance ||
      !coverage_state.skin_tone_protection_enabled ||
      !almost_equal(coverage_state.skin_tone_protection_strength, 58.0) ||
      !coverage_state.color_harmony_enabled ||
      coverage_state.color_harmony_mode != QStringLiteral("Tetradic") ||
      !almost_equal(coverage_state.color_harmony_base_hue, 0.24) ||
      !almost_equal(coverage_state.color_harmony_strength, 42.0) ||
      !almost_equal(coverage_state.grading_shadows_hue, 0.64) ||
      !almost_equal(coverage_state.grading_midtones_saturation, 12.0) ||
      !almost_equal(coverage_state.grading_highlights_luminance, 18.0) ||
      !almost_equal(coverage_state.grading_amount, 74.0) ||
      !almost_equal(coverage_state.grading_balance, -24.0) ||
      !almost_equal(coverage_state.sharpen_radius, 1.8) ||
      !almost_equal(coverage_state.lens_vignette, 6.0) ||
      !almost_equal(coverage_state.transform_skew_y, -1.5) ||
      coverage_state.crop_preset != QStringLiteral("3:2") ||
      !almost_equal(coverage_state.crop_rect_x, 0.12) ||
      !almost_equal(coverage_state.crop_rect_y, 0.08) ||
      !almost_equal(coverage_state.crop_rect_width, 0.71) ||
      !almost_equal(coverage_state.crop_rect_height, 0.48) ||
      coverage_state.upright_mode != QStringLiteral("Guided") ||
      !almost_equal(coverage_state.upright_vertical_guide_angle, 12.0) ||
      !almost_equal(coverage_state.upright_horizontal_guide_angle, -8.0) ||
      !coverage_state.content_aware_crop_fill ||
      coverage_state.output_color_space != QStringLiteral("Display P3") ||
      !coverage_state.soft_proof_enabled ||
      coverage_state.soft_proof_profile != QStringLiteral("sRGB") ||
      coverage_state.render_intent != QStringLiteral("Relative Colorimetric") ||
      !almost_equal(coverage_state.output_headroom, 2.0) ||
      !almost_equal(coverage_state.linear_mask_exposure, 0.35) ||
      !almost_equal(coverage_state.radial_mask_radius_y, 0.28) ||
      coverage_state.mask_stack.size() != 5 ||
      !almost_equal(coverage_state.mask_stack.at(0).toMap().value(QStringLiteral("texture")).toDouble(), 18.0) ||
      !almost_equal(coverage_state.mask_stack.at(0).toMap().value(QStringLiteral("sharpenRadius")).toDouble(), 1.7) ||
      !almost_equal(coverage_state.mask_stack.at(0).toMap().value(QStringLiteral("luminanceNr")).toDouble(), 22.0) ||
      coverage_state.mask_stack.at(3).toMap().value(QStringLiteral("retouchMode")).toString() !=
          QStringLiteral("heal") ||
      coverage_state.mask_stack.at(4).toMap().value(QStringLiteral("retouchMode")).toString() !=
          QStringLiteral("remove") ||
      coverage_state.layers.size() != 1 ||
      !almost_equal(
          coverage_state.layers.constFirst().toMap().value(QStringLiteral("opacity")).toDouble(),
          0.57) ||
      coverage_state.layers.constFirst().toMap().value(QStringLiteral("blendMode")).toString() !=
          QStringLiteral("overlay")) {
    return fail("typed recipe patch should apply representative fields from each exposed edit group");
  }
  const auto coverage_diff =
      photo_editor::desktop::agent_recipe_patch_diff(coverage_before, coverage_state, coverage_patch);
  if (coverage_diff.value(QStringLiteral("raw"))
          .toMap()
          .value(QStringLiteral("rawWhiteBalanceTemperature"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("raw"))
          .toMap()
          .value(QStringLiteral("rawDemosaicQuality"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("raw"))
          .toMap()
          .value(QStringLiteral("rawHighlightRecovery"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("raw"))
          .toMap()
          .value(QStringLiteral("rawCalibrationHue"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("globalTone"))
          .toMap()
          .value(QStringLiteral("levelsGamma"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("curves"))
          .toMap()
          .value(QStringLiteral("masterCurvePoints"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("curves"))
          .toMap()
          .value(QStringLiteral("blueCurvePoints"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("crop"))
          .toMap()
          .value(QStringLiteral("cropZoom"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("crop"))
          .toMap()
          .value(QStringLiteral("cropPreset"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("crop"))
          .toMap()
          .value(QStringLiteral("cropWidth"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("crop"))
          .toMap()
          .value(QStringLiteral("contentAwareCropFill"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("colorMixer"))
          .toMap()
          .value(QStringLiteral("colorMixerModel"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("colorMixer"))
          .toMap()
          .value(QStringLiteral("hslChannel"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("colorMixer"))
          .toMap()
          .value(QStringLiteral("redsHue"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("colorMixer"))
          .toMap()
          .value(QStringLiteral("channelMixerBlueFromBlue"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("colorMixer"))
          .toMap()
          .value(QStringLiteral("blackAndWhiteBlues"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("colorMixer"))
          .toMap()
          .value(QStringLiteral("colorReplacementEnabled"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("colorMixer"))
          .toMap()
          .value(QStringLiteral("skinToneProtectionStrength"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("colorGrading"))
          .toMap()
          .value(QStringLiteral("colorHarmonyEnabled"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("colorGrading"))
          .toMap()
          .value(QStringLiteral("colorHarmonyMode"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("colorGrading"))
          .toMap()
          .value(QStringLiteral("gradingBalance"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("output"))
          .toMap()
          .value(QStringLiteral("outputHeadroom"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("output"))
          .toMap()
          .value(QStringLiteral("outputColorSpace"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("output"))
          .toMap()
          .value(QStringLiteral("softProofEnabled"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("masks"))
          .toMap()
          .value(QStringLiteral("radialMaskRadiusY"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("masks"))
          .toMap()
          .value(QStringLiteral("maskStack"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("layers"))
          .toMap()
          .value(QStringLiteral("photoLayers"))
          .toMap()
          .isEmpty() ||
      coverage_diff.value(QStringLiteral("retouch"))
          .toMap()
          .value(QStringLiteral("retouchStack"))
          .toMap()
          .isEmpty()) {
    return fail("typed recipe patch diff should expose coverage fields in grouped output");
  }

  auto no_masks_style = style;
  auto no_masks_tools = no_masks_style.value(QStringLiteral("allowedTools")).toMap();
  no_masks_tools.insert(QStringLiteral("masks"), false);
  no_masks_style.insert(QStringLiteral("allowedTools"), no_masks_tools);
  const QVariantMap mask_patch{
      {QStringLiteral("masks"),
       QVariantMap{
           {QStringLiteral("linearMaskExposure"), 0.25},
       }},
  };
  const auto mask_disallowed =
      photo_editor::desktop::validate_agent_recipe_patch(mask_patch, no_masks_style);
  if (mask_disallowed.valid ||
      !mask_disallowed.errors.join(QStringLiteral("; ")).contains(QStringLiteral("does not allow"))) {
    return fail("recipe patch validation should enforce mask tool permissions");
  }

  auto no_hsl_style = style;
  auto no_hsl_tools = no_hsl_style.value(QStringLiteral("allowedTools")).toMap();
  no_hsl_tools.insert(QStringLiteral("hsl"), false);
  no_hsl_style.insert(QStringLiteral("allowedTools"), no_hsl_tools);
  const QVariantMap color_mixer_patch{
      {QStringLiteral("colorMixer"),
       QVariantMap{
           {QStringLiteral("blackAndWhiteEnabled"), true},
       }},
  };
  const auto color_mixer_disallowed =
      photo_editor::desktop::validate_agent_recipe_patch(color_mixer_patch, no_hsl_style);
  if (color_mixer_disallowed.valid ||
      !color_mixer_disallowed.errors.join(QStringLiteral("; ")).contains(QStringLiteral("does not allow"))) {
    return fail("recipe patch validation should enforce color mixer tool permissions");
  }

  auto no_color_grading_style = style;
  auto no_color_grading_tools = no_color_grading_style.value(QStringLiteral("allowedTools")).toMap();
  no_color_grading_tools.insert(QStringLiteral("color_grading"), false);
  no_color_grading_style.insert(QStringLiteral("allowedTools"), no_color_grading_tools);
  const QVariantMap color_grading_patch{
      {QStringLiteral("colorGrading"),
       QVariantMap{
           {QStringLiteral("gradingAmount"), 50.0},
       }},
  };
  const auto color_grading_disallowed =
      photo_editor::desktop::validate_agent_recipe_patch(color_grading_patch, no_color_grading_style);
  if (color_grading_disallowed.valid ||
      !color_grading_disallowed.errors.join(QStringLiteral("; ")).contains(QStringLiteral("does not allow"))) {
    return fail("recipe patch validation should enforce color grading tool permissions");
  }

  const QVariantMap malformed_bool_patch{
      {QStringLiteral("colorMixer"),
       QVariantMap{
           {QStringLiteral("skinToneProtectionEnabled"), 1.0},
       }},
  };
  const auto malformed_bool =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_bool_patch, style);
  if (malformed_bool.valid ||
      !malformed_bool.errors.join(QStringLiteral("; ")).contains(QStringLiteral("must be boolean"))) {
    return fail("recipe patch validation should enforce boolean color mixer fields");
  }

  const QVariantMap malformed_bw_patch{
      {QStringLiteral("colorMixer"),
       QVariantMap{
           {QStringLiteral("blackAndWhiteReds"), 180.0},
       }},
  };
  const auto malformed_bw =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_bw_patch, style);
  if (malformed_bw.valid ||
      !malformed_bw.errors.join(QStringLiteral("; ")).contains(QStringLiteral("between"))) {
    return fail("recipe patch validation should enforce black-and-white channel ranges");
  }

  const QVariantMap malformed_color_mixer_model_patch{
      {QStringLiteral("colorMixer"),
       QVariantMap{
           {QStringLiteral("colorMixerModel"), QStringLiteral("CMYK")},
       }},
  };
  const auto malformed_color_mixer_model =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_color_mixer_model_patch, style);
  if (malformed_color_mixer_model.valid ||
      !malformed_color_mixer_model.errors.join(QStringLiteral("; ")).contains(QStringLiteral("must be one of"))) {
    return fail("recipe patch validation should enforce color mixer model values");
  }

  const QVariantMap malformed_hsl_channel_patch{
      {QStringLiteral("colorMixer"),
       QVariantMap{
           {QStringLiteral("hslChannel"), QStringLiteral("Neons")},
       }},
  };
  const auto malformed_hsl_channel =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_hsl_channel_patch, style);
  if (malformed_hsl_channel.valid ||
      !malformed_hsl_channel.errors.join(QStringLiteral("; ")).contains(QStringLiteral("must be one of"))) {
    return fail("recipe patch validation should enforce HSL channel values");
  }

  const QVariantMap malformed_mixer_channel_patch{
      {QStringLiteral("colorMixer"),
       QVariantMap{
           {QStringLiteral("redsHue"), 140.0},
       }},
  };
  const auto malformed_mixer_channel =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_mixer_channel_patch, style);
  if (malformed_mixer_channel.valid ||
      !malformed_mixer_channel.errors.join(QStringLiteral("; ")).contains(QStringLiteral("between"))) {
    return fail("recipe patch validation should enforce per-channel mixer ranges");
  }

  const QVariantMap malformed_rgb_channel_mixer_patch{
      {QStringLiteral("colorMixer"),
       QVariantMap{
           {QStringLiteral("channelMixerRedFromRed"), 240.0},
       }},
  };
  const auto malformed_rgb_channel_mixer =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_rgb_channel_mixer_patch, style);
  if (malformed_rgb_channel_mixer.valid ||
      !malformed_rgb_channel_mixer.errors.join(QStringLiteral("; ")).contains(QStringLiteral("between"))) {
    return fail("recipe patch validation should enforce RGB channel mixer ranges");
  }

  const QVariantMap malformed_color_grading_bool_patch{
      {QStringLiteral("colorGrading"),
       QVariantMap{
           {QStringLiteral("colorHarmonyEnabled"), 1.0},
       }},
  };
  const auto malformed_color_grading_bool =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_color_grading_bool_patch, style);
  if (malformed_color_grading_bool.valid ||
      !malformed_color_grading_bool.errors.join(QStringLiteral("; ")).contains(QStringLiteral("must be boolean"))) {
    return fail("recipe patch validation should enforce boolean color grading fields");
  }

  const QVariantMap malformed_color_harmony_mode_type_patch{
      {QStringLiteral("colorGrading"),
       QVariantMap{
           {QStringLiteral("colorHarmonyMode"), 2.0},
       }},
  };
  const auto malformed_color_harmony_mode_type =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_color_harmony_mode_type_patch, style);
  if (malformed_color_harmony_mode_type.valid ||
      !malformed_color_harmony_mode_type.errors.join(QStringLiteral("; ")).contains(QStringLiteral("must be a string"))) {
    return fail("recipe patch validation should enforce string color harmony mode fields");
  }

  const QVariantMap malformed_color_harmony_mode_patch{
      {QStringLiteral("colorGrading"),
       QVariantMap{
           {QStringLiteral("colorHarmonyMode"), QStringLiteral("Moody")},
       }},
  };
  const auto malformed_color_harmony_mode =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_color_harmony_mode_patch, style);
  if (malformed_color_harmony_mode.valid ||
      !malformed_color_harmony_mode.errors.join(QStringLiteral("; ")).contains(QStringLiteral("must be one of"))) {
    return fail("recipe patch validation should enforce color harmony mode values");
  }

  const QVariantMap malformed_grading_patch{
      {QStringLiteral("colorGrading"),
       QVariantMap{
           {QStringLiteral("gradingBalance"), 160.0},
       }},
  };
  const auto malformed_grading =
      photo_editor::desktop::validate_agent_recipe_patch(malformed_grading_patch, style);
  if (malformed_grading.valid ||
      !malformed_grading.errors.join(QStringLiteral("; ")).contains(QStringLiteral("between"))) {
    return fail("recipe patch validation should enforce color grading ranges");
  }

  return EXIT_SUCCESS;
}
