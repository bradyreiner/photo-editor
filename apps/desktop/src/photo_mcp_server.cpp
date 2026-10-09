#include "photo_mcp_server.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <algorithm>

#include "automation_capability_policy.h"
#include "photo_automation_bridge.h"
#include "photo_composition_tool_contract.h"
#include "agent_design_schemas.h"

namespace photo_editor::desktop {
namespace {

struct PhotoMcpTool final {
  const char* name;
  const char* title;
  const char* description;
  const char* automation_command;
  QJsonObject input_schema;
  bool read_only = false;
  bool destructive = false;
  bool requires_confirmation = false;
};

QJsonObject json_rpc_error(const QJsonValue& id, int code, const QString& message) {
  QJsonObject error;
  error.insert(QStringLiteral("code"), code);
  error.insert(QStringLiteral("message"), message);
  QJsonObject response;
  response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
  if (!id.isUndefined()) {
    response.insert(QStringLiteral("id"), id);
  }
  response.insert(QStringLiteral("error"), error);
  return response;
}

QJsonObject json_rpc_result(const QJsonValue& id, const QJsonObject& result) {
  QJsonObject response;
  response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
  if (!id.isUndefined()) {
    response.insert(QStringLiteral("id"), id);
  }
  response.insert(QStringLiteral("result"), result);
  return response;
}

QJsonObject empty_object_schema() {
  return QJsonObject{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"), QJsonObject{}},
      {QStringLiteral("additionalProperties"), false},
  };
}

QJsonObject string_property(const QString& description = {}) {
  QJsonObject property{{QStringLiteral("type"), QStringLiteral("string")}};
  if (!description.isEmpty()) {
    property.insert(QStringLiteral("description"), description);
  }
  return property;
}

QJsonObject boolean_property(const QString& description = {}) {
  QJsonObject property{{QStringLiteral("type"), QStringLiteral("boolean")}};
  if (!description.isEmpty()) {
    property.insert(QStringLiteral("description"), description);
  }
  return property;
}

QJsonObject number_property(const QString& description = {}) {
  QJsonObject property{{QStringLiteral("type"), QStringLiteral("number")}};
  if (!description.isEmpty()) {
    property.insert(QStringLiteral("description"), description);
  }
  return property;
}

QJsonObject bounded_number_property(double minimum, double maximum) {
  return QJsonObject{
      {QStringLiteral("type"), QStringLiteral("number")},
      {QStringLiteral("minimum"), minimum},
      {QStringLiteral("maximum"), maximum},
  };
}

QJsonObject generic_object_property(const QString& description = {}) {
  QJsonObject property{{QStringLiteral("type"), QStringLiteral("object")}};
  if (!description.isEmpty()) {
    property.insert(QStringLiteral("description"), description);
  }
  return property;
}

QJsonObject object_schema(
    const QJsonObject& properties,
    const QJsonArray& required = {},
    bool additional_properties = false) {
  QJsonObject schema{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"), properties},
      {QStringLiteral("additionalProperties"), additional_properties},
  };
  if (!required.isEmpty()) {
    schema.insert(QStringLiteral("required"), required);
  }
  return schema;
}

QJsonObject composition_point_schema(bool pressure_allowed) {
  QJsonObject properties{
      {QStringLiteral("x"), number_property(QStringLiteral("Canvas-pixel or normalized horizontal coordinate."))},
      {QStringLiteral("y"), number_property(QStringLiteral("Canvas-pixel or normalized vertical coordinate."))},
  };
  QJsonArray required{QStringLiteral("x"), QStringLiteral("y")};
  if (pressure_allowed) {
    properties.insert(QStringLiteral("pressure"), bounded_number_property(0.0, 1.0));
  }
  return object_schema(properties, required);
}

QJsonObject composition_values_schema() {
  const auto string_fields = {
      QStringLiteral("text"), QStringLiteral("family"), QStringLiteral("face"),
      QStringLiteral("color"), QStringLiteral("strokeColor"), QStringLiteral("shadowColor"),
      QStringLiteral("mode"), QStringLiteral("maskId"), QStringLiteral("materialTint"),
      QStringLiteral("pathSide"), QStringLiteral("textureMaterial"), QStringLiteral("tipLayerId"),
      QStringLiteral("tipPresetName"), QStringLiteral("name"), QStringLiteral("sourcePresetName"),
      QStringLiteral("path"), QStringLiteral("effectType"), QStringLiteral("effectId"),
  };
  const auto number_fields = {
      QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("horizontal"), QStringLiteral("vertical"),
      QStringLiteral("sigma"), QStringLiteral("opacity"), QStringLiteral("position"), QStringLiteral("scale"),
      QStringLiteral("feather"),
      QStringLiteral("rotation"), QStringLiteral("size"), QStringLiteral("weight"), QStringLiteral("tracking"),
      QStringLiteral("lineHeight"), QStringLiteral("strokeWidth"), QStringLiteral("shadowX"),
      QStringLiteral("shadowY"), QStringLiteral("paragraphSpacing"), QStringLiteral("pathStart"),
      QStringLiteral("pathEnd"), QStringLiteral("pathOffset"), QStringLiteral("pathSpacing"),
      QStringLiteral("materialBlur"), QStringLiteral("materialTransmission"),
      QStringLiteral("materialEdgeSoftness"), QStringLiteral("materialDepth"),
      QStringLiteral("materialLightAngle"), QStringLiteral("hardness"), QStringLiteral("flow"),
      QStringLiteral("spacing"), QStringLiteral("textureStrength"), QStringLiteral("textureScale"),
      QStringLiteral("textureAngle"), QStringLiteral("textureSeed"), QStringLiteral("lightFalloff"),
      QStringLiteral("lightExposure"), QStringLiteral("lightTaper"),
      QStringLiteral("tipRotationVariation"), QStringLiteral("tipScaleVariation"),
      QStringLiteral("rotationVariation"), QStringLiteral("scaleVariation"),
      QStringLiteral("radius"), QStringLiteral("levels"), QStringLiteral("strength"),
      QStringLiteral("threshold"), QStringLiteral("highlightThreshold"),
      QStringLiteral("highlightKnee"), QStringLiteral("warmth"),
      QStringLiteral("highlightProtection"), QStringLiteral("paperScale"),
      QStringLiteral("paperTexture"), QStringLiteral("inkSpread"),
      QStringLiteral("inkDensity"), QStringLiteral("redRegistrationX"),
      QStringLiteral("redRegistrationY"), QStringLiteral("blueRegistrationX"),
      QStringLiteral("blueRegistrationY"), QStringLiteral("seed"),
      QStringLiteral("distance"), QStringLiteral("angle"), QStringLiteral("falloff"),
      QStringLiteral("highlightWeighting"), QStringLiteral("amount"),
  };
  const auto boolean_fields = {
      QStringLiteral("italic"), QStringLiteral("underline"), QStringLiteral("subtract"), QStringLiteral("pathClosed"),
      QStringLiteral("pathExclusion"), QStringLiteral("independent"), QStringLiteral("pressureSize"),
      QStringLiteral("pressureFlow"), QStringLiteral("clipSelection"),
      QStringLiteral("lightAccumulation"), QStringLiteral("overwrite"),
      QStringLiteral("protectSubject"), QStringLiteral("enabled"),
  };
  QJsonObject properties;
  for (const auto& field : string_fields) properties.insert(field, string_property());
  for (const auto& field : number_fields) properties.insert(field, number_property());
  for (const auto& field : boolean_fields) properties.insert(field, boolean_property());
  for (const auto& field : {QStringLiteral("x"), QStringLiteral("y")}) {
    properties.insert(field, QJsonObject{
        {QStringLiteral("type"), QStringLiteral("integer")},
        {QStringLiteral("minimum"), -1000000},
        {QStringLiteral("maximum"), 1000000},
        {QStringLiteral("description"), QStringLiteral(
            "Integer canvas-pixel coordinate, not normalized. add_text places the text origin; transform sets the layer raster's top-left before rotation. Inspect x/y, textOriginX/Y and pixel dimensions after styling.")},
    });
  }
  auto size_property = bounded_number_property(1.0, 8192.0);
  size_property.insert(QStringLiteral("description"), QStringLiteral(
      "Typography font size in document pixels, not points or screen pixels. Paint size is in canvas pixels. Layer scale multiplies visible size."));
  properties.insert(QStringLiteral("size"), size_property);
  auto scale_property = number_property();
  scale_property.insert(QStringLiteral("description"), QStringLiteral(
      "Uniform layer-size multiplier; 1 is unscaled. The editor clamps transform scale to 0.05 through 32."));
  properties.insert(QStringLiteral("scale"), scale_property);
  auto rotation_property = number_property();
  rotation_property.insert(QStringLiteral("description"), QStringLiteral(
      "Layer rotation in degrees about its center; 0 is unrotated. The editor clamps transform rotation to -3600 through 3600."));
  properties.insert(QStringLiteral("rotation"), rotation_property);
  properties.insert(QStringLiteral("hardness"), bounded_number_property(0.0, 1.0));
  properties.insert(QStringLiteral("opacity"), bounded_number_property(0.0, 1.0));
  properties.insert(QStringLiteral("amount"), bounded_number_property(0.0, 1.0));
  properties.insert(QStringLiteral("position"), QJsonObject{
      {QStringLiteral("type"), QStringLiteral("integer")},
      {QStringLiteral("minimum"), 0},
      {QStringLiteral("maximum"), 1000000},
      {QStringLiteral("description"), QStringLiteral("Zero-based integer layer or effect position; zero is top.")},
  });
  properties.insert(QStringLiteral("flow"), bounded_number_property(0.0, 1.0));
  properties.insert(QStringLiteral("spacing"), bounded_number_property(0.02, 1.0));
  properties.insert(QStringLiteral("feather"), bounded_number_property(0.0, 0.05));
  properties.insert(QStringLiteral("textureStrength"), bounded_number_property(0.0, 1.0));
  properties.insert(QStringLiteral("textureScale"), bounded_number_property(0.5, 256.0));
  properties.insert(QStringLiteral("textureAngle"), bounded_number_property(-180.0, 180.0));
  properties.insert(QStringLiteral("lightFalloff"), bounded_number_property(0.0, 1.0));
  properties.insert(QStringLiteral("lightExposure"), bounded_number_property(-8.0, 8.0));
  properties.insert(QStringLiteral("lightTaper"), bounded_number_property(0.0, 1.0));
  properties.insert(QStringLiteral("tipRotationVariation"), bounded_number_property(0.0, 180.0));
  properties.insert(QStringLiteral("tipScaleVariation"), bounded_number_property(0.0, 0.9));
  properties.insert(QStringLiteral("rotationVariation"), bounded_number_property(0.0, 180.0));
  properties.insert(QStringLiteral("scaleVariation"), bounded_number_property(0.0, 0.9));
  auto material = string_property();
  material.insert(QStringLiteral("enum"), QJsonArray{
      QStringLiteral("none"), QStringLiteral("frosted_glass"),
      QStringLiteral("smoked_plastic"), QStringLiteral("soft_emboss"),
      QStringLiteral("polished_chrome"), QStringLiteral("brushed_silver"),
      QStringLiteral("warm_gold"), QStringLiteral("copper"),
      QStringLiteral("dark_steel"), QStringLiteral("champagne_gold"),
      QStringLiteral("rose_gold"), QStringLiteral("antique_brass"),
      QStringLiteral("titanium"), QStringLiteral("black_chrome"),
  });
  properties.insert(QStringLiteral("material"), material);
  auto effect_type = string_property(QStringLiteral("Registered editable photo-layer effect type."));
  effect_type.insert(QStringLiteral("enum"), QJsonArray{
      QStringLiteral("gaussian_blur"), QStringLiteral("box_blur"),
      QStringLiteral("motion_blur"), QStringLiteral("posterize_linear_rgb"),
      QStringLiteral("invert_linear_rgb"), QStringLiteral("unsharp_mask"),
      QStringLiteral("optical_diffusion"), QStringLiteral("imperfect_print"),
      QStringLiteral("dragged_shutter"),
  });
  properties.insert(QStringLiteral("effectType"), effect_type);
  auto texture_material = string_property();
  texture_material.insert(QStringLiteral("enum"), QJsonArray{
      QStringLiteral("solid"), QStringLiteral("dry_ink"), QStringLiteral("graphite"),
      QStringLiteral("light_paint"), QStringLiteral("photo_fragment"),
  });
  properties.insert(QStringLiteral("textureMaterial"), texture_material);
  auto axes = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("additionalProperties"), number_property(QStringLiteral("Numeric OpenType axis value."))}};
  properties.insert(QStringLiteral("axes"), axes);
  properties.insert(QStringLiteral("points"), QJsonObject{
      {QStringLiteral("type"), QStringLiteral("array")},
      {QStringLiteral("items"), composition_point_schema(true)},
      {QStringLiteral("maxItems"), 20000},
  });
  properties.insert(QStringLiteral("pathPoints"), QJsonObject{
      {QStringLiteral("type"), QStringLiteral("array")},
      {QStringLiteral("items"), composition_point_schema(false)},
  });
  properties.insert(QStringLiteral("pathContours"), QJsonObject{
      {QStringLiteral("type"), QStringLiteral("array")},
      {QStringLiteral("items"), QJsonObject{
          {QStringLiteral("type"), QStringLiteral("array")},
          {QStringLiteral("items"), composition_point_schema(false)},
      }},
  });
  properties.insert(QStringLiteral("textureSeed"), QJsonObject{
      {QStringLiteral("type"), QStringLiteral("integer")},
      {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 4294967295.0},
  });
  properties.insert(QStringLiteral("levels"), QJsonObject{
      {QStringLiteral("type"), QStringLiteral("integer")},
      {QStringLiteral("minimum"), 2}, {QStringLiteral("maximum"), 256},
  });
  properties.insert(QStringLiteral("seed"), QJsonObject{
      {QStringLiteral("type"), QStringLiteral("integer")},
      {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 4294967295.0},
  });
  properties.insert(QStringLiteral("contourIndex"), QJsonObject{
      {QStringLiteral("type"), QStringLiteral("integer")},
      {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 8191},
  });
  const auto typography_properties = QJsonObject::fromVariantMap(agent_typography_schema())
      .value(QStringLiteral("properties")).toObject();
  for (auto it = typography_properties.begin(); it != typography_properties.end(); ++it)
    if (!properties.contains(it.key())) properties.insert(it.key(), it.value());
  return object_schema(properties, {}, false);
}

QJsonObject composition_operation_schema() {
  auto operation = string_property(QStringLiteral("Composition operation to perform."));
  operation.insert(QStringLiteral("enum"), QJsonArray{
      QStringLiteral("add_text"), QStringLiteral("add_pixel_layer"),
      QStringLiteral("add_source_layer"), QStringLiteral("text"),
      QStringLiteral("typography"), QStringLiteral("text_path"), QStringLiteral("transform"),
      QStringLiteral("perspective"), QStringLiteral("move"), QStringLiteral("copy_selection"),
      QStringLiteral("erase_selection"), QStringLiteral("select_subject"), QStringLiteral("lasso_selection"), QStringLiteral("blur"),
      QStringLiteral("opacity"), QStringLiteral("paint"), QStringLiteral("add_effect"),
      QStringLiteral("update_effect"), QStringLiteral("remove_effect"),
      QStringLiteral("reorder_effect"), QStringLiteral("mask_effect"),
      QStringLiteral("clear_effect_mask"),
      QStringLiteral("save_photo_fragment_preset"), QStringLiteral("delete_photo_fragment_preset"),
      QStringLiteral("import_photo_fragment_preset"), QStringLiteral("export_photo_fragment_preset"),
  });
  return operation;
}

QJsonObject composition_edit_input_schema() {
  auto schema = object_schema(
      QJsonObject{
          {QStringLiteral("assetId"), string_property()},
          {QStringLiteral("operation"), composition_operation_schema()},
          {QStringLiteral("layerId"), string_property(QStringLiteral(
              "Stable existing layer ID at the top level of arguments. Required for operations that edit a layer. Use an inspected ID or a prior creation receipt; never predict a new ID."))},
          {QStringLiteral("values"), composition_values_schema()},
      },
      QJsonArray{QStringLiteral("assetId"), QStringLiteral("operation"), QStringLiteral("values")});
  const QVector<QPair<QString, QStringList>> required_values{
      {QStringLiteral("add_text"), {QStringLiteral("text"), QStringLiteral("x"), QStringLiteral("y")}},
      {QStringLiteral("text"), {QStringLiteral("text")}},
      {QStringLiteral("transform"), {QStringLiteral("x"), QStringLiteral("y"),
          QStringLiteral("scale"), QStringLiteral("rotation")}},
      {QStringLiteral("perspective"), {QStringLiteral("horizontal"), QStringLiteral("vertical")}},
      {QStringLiteral("move"), {QStringLiteral("position")}},
      {QStringLiteral("copy_selection"), {QStringLiteral("maskId")}},
      {QStringLiteral("erase_selection"), {QStringLiteral("maskId")}},
      {QStringLiteral("lasso_selection"), {QStringLiteral("points")}},
      {QStringLiteral("text_path"), {QStringLiteral("maskId")}},
      {QStringLiteral("blur"), {QStringLiteral("sigma")}},
      {QStringLiteral("add_effect"), {QStringLiteral("effectType")}},
      {QStringLiteral("update_effect"), {QStringLiteral("effectId")}},
      {QStringLiteral("remove_effect"), {QStringLiteral("effectId")}},
      {QStringLiteral("reorder_effect"), {QStringLiteral("effectId"), QStringLiteral("position")}},
      {QStringLiteral("mask_effect"), {QStringLiteral("effectId"), QStringLiteral("maskId")}},
      {QStringLiteral("clear_effect_mask"), {QStringLiteral("effectId")}},
      {QStringLiteral("opacity"), {QStringLiteral("opacity")}},
      {QStringLiteral("paint"), {QStringLiteral("points")}},
      {QStringLiteral("delete_photo_fragment_preset"), {QStringLiteral("name")}},
      {QStringLiteral("import_photo_fragment_preset"), {QStringLiteral("path")}},
      {QStringLiteral("export_photo_fragment_preset"), {QStringLiteral("name"), QStringLiteral("path")}},
  };
  QJsonArray conditions;
  conditions.append(QJsonObject{
      {QStringLiteral("if"), object_schema(QJsonObject{
          {QStringLiteral("operation"), QJsonObject{{QStringLiteral("enum"),
              QJsonArray::fromStringList(composition_layer_target_operations())}}},
      }, QJsonArray{QStringLiteral("operation")}, true)},
      {QStringLiteral("then"), QJsonObject{
          {QStringLiteral("required"), QJsonArray{QStringLiteral("layerId")}},
          {QStringLiteral("properties"), QJsonObject{
              {QStringLiteral("layerId"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                  {QStringLiteral("minLength"), 1}, {QStringLiteral("pattern"), QStringLiteral("\\S")}}},
          }},
      }},
  });
  for (const auto& [operation, fields] : required_values) {
    QJsonArray required;
    for (const auto& field : fields) required.append(field);
    // Match the operation within the full call; other argument fields belong
    // to the outer schema and must not make this predicate false.
    const auto condition = object_schema(
        QJsonObject{{QStringLiteral("operation"), QJsonObject{{QStringLiteral("const"), operation}}}},
        QJsonArray{QStringLiteral("operation")}, true);
    QJsonObject values_constraints{{QStringLiteral("required"), required}};
    if (operation == QStringLiteral("paint")) {
      values_constraints.insert(QStringLiteral("properties"), QJsonObject{
          {QStringLiteral("size"), bounded_number_property(1.0, 4096.0)},
      });
    } else if (operation == QStringLiteral("lasso_selection")) {
      auto points_schema = composition_values_schema().value(QStringLiteral("properties"))
          .toObject().value(QStringLiteral("points")).toObject();
      auto normalized_point = composition_point_schema(false);
      auto coordinates = normalized_point.value(QStringLiteral("properties")).toObject();
      for (const auto& field : {QStringLiteral("x"), QStringLiteral("y")}) {
        auto coordinate = bounded_number_property(0.0, 1.0);
        coordinate.insert(QStringLiteral("description"), QStringLiteral("Normalized image coordinate from zero to one."));
        coordinates.insert(field, coordinate);
      }
      normalized_point.insert(QStringLiteral("properties"), coordinates);
      points_schema.insert(QStringLiteral("items"), normalized_point);
      points_schema.insert(QStringLiteral("minItems"), 3);
      points_schema.insert(QStringLiteral("maxItems"), 4096);
      values_constraints.insert(QStringLiteral("properties"), QJsonObject{
          {QStringLiteral("points"), points_schema},
          {QStringLiteral("feather"), bounded_number_property(0.0, 0.05)},
      });
    } else if (operation == QStringLiteral("update_effect")) {
      values_constraints.insert(QStringLiteral("minProperties"), 2);
    }
    const auto then_schema = QJsonObject{
        {QStringLiteral("properties"), QJsonObject{{QStringLiteral("values"), values_constraints}}}};
    conditions.append(QJsonObject{{QStringLiteral("if"), condition},
        {QStringLiteral("then"), then_schema}});
  }
  conditions.append(QJsonObject{
      {QStringLiteral("if"), object_schema(
          QJsonObject{{QStringLiteral("operation"),
              QJsonObject{{QStringLiteral("const"), QStringLiteral("typography")}}}},
          QJsonArray{QStringLiteral("operation")}, true)},
      {QStringLiteral("then"), QJsonObject{{QStringLiteral("properties"),
          QJsonObject{{QStringLiteral("values"), QJsonObject::fromVariantMap(agent_typography_schema())}}}}},
  });
  conditions.append(QJsonObject{
      {QStringLiteral("if"), object_schema(
          QJsonObject{{QStringLiteral("operation"),
              QJsonObject{{QStringLiteral("const"), QStringLiteral("save_photo_fragment_preset")}}}},
          QJsonArray{QStringLiteral("operation")}, true)},
      {QStringLiteral("then"), QJsonObject{{QStringLiteral("properties"),
          QJsonObject{{QStringLiteral("values"), QJsonObject{
              {QStringLiteral("required"), QJsonArray{QStringLiteral("name")}},
              {QStringLiteral("oneOf"), QJsonArray{
              object_schema({}, QJsonArray{QStringLiteral("tipLayerId")}, true),
              object_schema({}, QJsonArray{QStringLiteral("sourcePresetName")}, true),
          }}}}}}}},
  });
  schema.insert(QStringLiteral("allOf"), conditions);
  return schema;
}

QJsonObject metrics_properties(bool include_confirmed) {
  QJsonObject properties{
      {QStringLiteral("preset"), string_property(QStringLiteral("Export preset name."))},
      {QStringLiteral("destinationDirectory"), string_property(QStringLiteral("Export destination directory."))},
      {QStringLiteral("histogramSampleCount"), number_property()},
      {QStringLiteral("shadowClipCount"), number_property()},
      {QStringLiteral("highlightClipCount"), number_property()},
      {QStringLiteral("rawClipSampleCount"), number_property()},
      {QStringLiteral("rawShadowClipCount"), number_property()},
      {QStringLiteral("rawHighlightClipCount"), number_property()},
  };
  if (include_confirmed) {
    properties.insert(
        QStringLiteral("confirmed"),
        boolean_property(QStringLiteral("Must be true before queueing export jobs.")));
  }
  return properties;
}

QVector<PhotoMcpTool> photo_mcp_tools() {
  auto input_transform_property = string_property(
      QStringLiteral("Use auto for exact supported file-tag detection, none for a manual bypass, or a supported Sony camera transform."));
  input_transform_property.insert(
      QStringLiteral("enum"),
      QJsonArray{
          QStringLiteral("auto"),
          QStringLiteral("none"),
          QStringLiteral("slog2-sgamut"),
          QStringLiteral("slog3-sgamut3"),
          QStringLiteral("slog3-sgamut3-cine"),
      });
  auto shot_match_gain_property = number_property();
  shot_match_gain_property.insert(QStringLiteral("minimum"), 0.5);
  shot_match_gain_property.insert(QStringLiteral("maximum"), 2.0);
  shot_match_gain_property.insert(
      QStringLiteral("description"),
      QStringLiteral("Finite per-channel multiplier from 0.5 to 2.0; use 1.0 to reset."));
  return {
      {
          "app.list_commands", "Discover Native App Commands",
          "Find native app controls by domain and query. Paginated descriptors identify availability, resource ownership and reversal; unavailable commands explain the missing adapter. Fetch app.command_schema before invoking a discovered command. Use domain edit for fills, shapes, source images, groups, typography, layer effects and image controls.",
          "app_list_commands",
          object_schema(QJsonObject{{"domain", string_property()}, {"query", string_property()},
              {"offset", QJsonObject{{"type", "integer"}, {"minimum", 0}}},
              {"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 64}}}}, {}),
          true, false, false,
      },
      {
          "app.command_schema", "Read Native Command Schema",
          "Read the authoritative typed arguments and task contract for a commandId from app.list_commands. Layer targets use stable IDs returned by photo.inspect_composition; never invent an ID or pass a UI row index.",
          "app_command_schema",
          object_schema(QJsonObject{{"commandId", string_property()}}, QJsonArray{"commandId"}),
          true, false, false,
      },
      {
          "app.inspect_command", "Inspect Native Command",
          "Invoke a readOnly native command using its discovered commandId and typed values. Rejects every mutable control; this tool requires read capability only. Use the invokeTool returned by app.command_schema.",
          "app_inspect_command",
          object_schema(QJsonObject{{"commandId", string_property()},
              {"values", QJsonObject{{"type", "object"}}}}, QJsonArray{"commandId", "values"}),
          true, false, false,
      },
      {
          "app.invoke_command", "Invoke Owned Native Command",
          "Invoke an available native command using its exact discovered commandId and typed values. Mutations require an active owned agent draft and capture their full recipe in its revert journal. Returns actual createdLayerId(s), changed and taskStateHash. Unavailable or invalid commands fail before mutation. This tool does not grant commit or export capability.",
          "app_invoke_command",
          object_schema(QJsonObject{{"commandId", string_property()},
              {"values", QJsonObject{{"type", "object"}, {"description", "Exact arguments from app.command_schema."}}}},
              QJsonArray{"commandId", "values"}),
          false, false, false,
      },
      {
          "timeline.inspect_playback_performance", "Inspect Playback Performance",
          "Read playback health, preview route and quality, dropped-frame count, smoothed per-stage timing, and cumulative color-LUT cache hits, misses, evictions, and accounted payload bytes. Timings are an EWMA updated from completed worker frames and exclude decoder wait and display presentation. The measurement source key identifies the clip, rendered dimensions, and input transform; no media path is returned. Cache byte counts cover accounted vector payloads, not total process or GPU memory.",
          "timeline_playback_performance_inspect",
          empty_object_schema(),
          true, false, false,
      },
      {
          "timeline.capture_program_color_scopes", "Capture Program Color Scopes",
          "Request a bounded 480 by 270 capture of the displayed Program Monitor and open the Timeline scopes panel. Capture and analysis are asynchronous; poll timeline.inspect_program_color_scopes until analysisBusy is false and revision advances. Scope measurements describe 8-bit SDR monitor output, not source-linear or HDR pixels.",
          "timeline_scope_capture_request",
          empty_object_schema(),
          true, false, false,
      },
      {
          "timeline.inspect_program_color_scopes", "Inspect Program Color Scopes",
          "Read compact RGB and luma mean, percentile, and near-black or near-white summaries from the latest bounded Program Monitor scope capture. The response identifies sample size, revision, freshness, and 8-bit SDR display precision; it does not return full histogram or waveform arrays.",
          "timeline_scope_summary_inspect",
          empty_object_schema(),
          true, false, false,
      },
      {
          "timeline.inspect_clip_color", "Inspect Clip Color",
          "Read the color input transform, creative LUT, shot-match gains, and affected sequence range for a stable clip id in the active timeline. When this clip is the current Program frame, also return Qt decoder-reported frame color metadata, the preview conversion path, and any conversion warning; those tags do not identify a camera log profile.",
          "timeline_color_inspect",
          object_schema(
              QJsonObject{{QStringLiteral("clipId"),
                           string_property(QStringLiteral("Stable timeline clip id."))}},
              QJsonArray{QStringLiteral("clipId")}),
          true, false, false,
      },
      {
          "timeline.open_program_comparison", "Open Program Color Comparison",
          "Open the existing Program Comparison window on a video or photo clip by stable id. By default, the comparison opens at the clip midpoint; sequenceSeconds can select another visible moment inside the clip. The captured Program frame becomes the reference for iterative visual review. This changes the playhead and window state only; it does not modify the timeline document or undo history.",
          "timeline_color_compare_open",
          object_schema(
              QJsonObject{
                  {QStringLiteral("clipId"),
                   string_property(QStringLiteral("Stable video or photo timeline clip id."))},
                  {QStringLiteral("sequenceSeconds"),
                   QJsonObject{
                       {QStringLiteral("type"), QStringLiteral("number")},
                       {QStringLiteral("minimum"), 0},
                       {QStringLiteral("description"),
                        QStringLiteral("Optional sequence time inside the clip; defaults to its midpoint. The clip must be the visible Program clip at this time.")},
                   }},
                  {QStringLiteral("mode"),
                   QJsonObject{
                       {QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("enum"),
                        QJsonArray{QStringLiteral("split"), QStringLiteral("wipe"),
                                   QStringLiteral("overlay"), QStringLiteral("difference")}},
                       {QStringLiteral("description"),
                        QStringLiteral("Optional initial comparison layout; defaults to wipe.")},
                   }},
              },
              QJsonArray{QStringLiteral("clipId")}),
          true, false, false,
      },
      {
          "timeline.inspect_clip_source_color_metadata", "Inspect Clip Source Color Metadata",
          "Read generic FFmpeg stream color fields and bounded Sony gamma, gamut, monitoring, or transform tags from a video clip's source file by stable clip id. When explicit Gamma for Look and Color for Look tags exactly match a supported Sony S-Log pair, return a suggested input transform. This reads metadata only and never selects or changes the clip transform; missing, conflicting, or unrecognized pairs remain unresolved.",
          "timeline_source_color_metadata_inspect",
          object_schema(
              QJsonObject{{QStringLiteral("clipId"),
                           string_property(QStringLiteral("Stable video timeline clip id."))}},
              QJsonArray{QStringLiteral("clipId")}),
          true, false, false,
      },
      {
          "timeline.inspect_clip_effects", "Inspect Clip Effects",
          "Read a picture clip's ordered effect stack, enabled state, current values at the sequence playhead, and keyframe information by stable id.",
          "timeline_effects_inspect",
          object_schema(
              QJsonObject{{QStringLiteral("clipId"),
                           string_property(QStringLiteral("Stable timeline clip id."))}},
              QJsonArray{QStringLiteral("clipId")}),
          true, false, false,
      },
      {
          "timeline.inspect_clip_color_grade", "Inspect Clip Color Grade",
          "Read the photo-engine grade recipe on a video or timeline adjustment clip, its preview render mode, and sequence-export capability. For pointwise grades, inspection compiles or loads the shared-engine CUBE and reports its interpolation-fidelity validation, cube size, p99 error, and maximum error. Spatial or image-dependent grades remain preview-only for sequence export.",
          "timeline_color_grade_inspect",
          object_schema(
              QJsonObject{{QStringLiteral("clipId"),
                           string_property(QStringLiteral("Stable video or timeline adjustment clip id."))}},
              QJsonArray{QStringLiteral("clipId")}),
          true, false, false,
      },
      {
          "timeline.set_clip_color_grade", "Set Clip Color Grade",
          "Attach a validated photo-engine recipe to a video clip or timeline adjustment clip. Adjustment clips apply the grade to the picture composite beneath them. Sequence export supports pointwise recipes through a shared-engine CUBE LUT; spatial masks and image-dependent operations are not yet supported by sequence export.",
          "timeline_color_grade_set",
          object_schema(
              QJsonObject{
                  {QStringLiteral("clipId"),
                   string_property(QStringLiteral("Stable video or timeline adjustment clip id."))},
                  {QStringLiteral("recipe"),
                   generic_object_property(QStringLiteral("Complete photo edit recipe object. Inspect the clip first, then provide the updated recipe."))},
              },
              QJsonArray{QStringLiteral("clipId"), QStringLiteral("recipe")}),
          false, false, false,
      },
      {
          "timeline.edit_clip_color_grade_layer", "Edit Clip Color Grade Layer",
          "Edit the ordered photo-engine adjustment layers on a video or timeline adjustment clip by stable clip and layer ids. Operations add, replace, move, enable, set opacity, set a supported blend mode, set one grade parameter, or remove one adjustment layer. Add and replace take a complete adjustment layer object; position 0 is the top of the stack. Each edit is validated and undoable.",
          "timeline_color_grade_layer_edit",
          object_schema(
              QJsonObject{
                  {QStringLiteral("clipId"),
                   string_property(QStringLiteral("Stable video or timeline adjustment clip id."))},
                  {QStringLiteral("operation"),
                   QJsonObject{
                       {QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("enum"),
                        QJsonArray{QStringLiteral("add"), QStringLiteral("replace"),
                                   QStringLiteral("remove"), QStringLiteral("move"),
                                   QStringLiteral("set_enabled"),
                                   QStringLiteral("set_opacity"),
                                   QStringLiteral("set_blend_mode"),
                                   QStringLiteral("set_parameter")}},
                   }},
                  {QStringLiteral("layerId"),
                   string_property(QStringLiteral("Stable id of the adjustment layer to edit; required except for add."))},
                  {QStringLiteral("layer"),
                   generic_object_property(QStringLiteral("Complete adjustment layer object with a stable id and kind 'adjustment'; required for add and replace."))},
                  {QStringLiteral("position"),
                   QJsonObject{
                       {QStringLiteral("type"), QStringLiteral("integer")},
                       {QStringLiteral("minimum"), 0},
                       {QStringLiteral("description"),
                        QStringLiteral("Zero-based stack index. Required for move and optional for add; 0 is the top.")},
                   }},
                  {QStringLiteral("enabled"),
                   boolean_property(QStringLiteral("New enabled state; used only with set_enabled."))},
                  {QStringLiteral("opacity"),
                   number_property(QStringLiteral("Layer opacity from 0 to 1; used only with set_opacity."))},
                  {QStringLiteral("blendMode"),
                   QJsonObject{
                       {QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("enum"),
                        QJsonArray{QStringLiteral("normal"), QStringLiteral("multiply"),
                                   QStringLiteral("screen"), QStringLiteral("overlay"),
                                   QStringLiteral("soft-light"), QStringLiteral("color"),
                                   QStringLiteral("luminosity")}},
                       {QStringLiteral("description"),
                        QStringLiteral("Adjustment-layer compositing mode; used only with set_blend_mode.")},
                   }},
                  {QStringLiteral("parameter"),
                   QJsonObject{
                       {QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("enum"),
                        QJsonArray{QStringLiteral("exposure"), QStringLiteral("contrast"),
                                   QStringLiteral("highlights"), QStringLiteral("shadows"),
                                   QStringLiteral("whites"), QStringLiteral("blacks"),
                                   QStringLiteral("temperature"), QStringLiteral("tint"),
                                   QStringLiteral("vibrance"),
                                   QStringLiteral("saturation"),
                                   QStringLiteral("gradingShadowsHue"),
                                   QStringLiteral("gradingShadowsSaturation"),
                                   QStringLiteral("gradingShadowsLuminance"),
                                   QStringLiteral("gradingMidtonesHue"),
                                   QStringLiteral("gradingMidtonesSaturation"),
                                   QStringLiteral("gradingMidtonesLuminance"),
                                   QStringLiteral("gradingHighlightsHue"),
                                   QStringLiteral("gradingHighlightsSaturation"),
                                   QStringLiteral("gradingHighlightsLuminance")}},
                       {QStringLiteral("description"),
                        QStringLiteral("Supported photo-engine adjustment-layer control, including three-way color grading; used only with set_parameter.")},
                   }},
                  {QStringLiteral("value"),
                   number_property(QStringLiteral("New grade value; used only with set_parameter. Values are clamped to the supported control range."))},
              },
              QJsonArray{QStringLiteral("clipId"), QStringLiteral("operation")}),
          false, false, false,
      },
      {
          "timeline.set_clip_color_grade_curve", "Set Clip Color Grade Curve",
          "Set or reset one RGB, luma, or individual-channel point curve on a video or timeline adjustment clip. Pass an empty points array to reset the curve. Provide layerId to target a stable adjustment layer; edits are validated and undoable.",
          "timeline_color_grade_curve_set",
          object_schema(
              QJsonObject{
                  {QStringLiteral("clipId"),
                   string_property(QStringLiteral("Stable video or timeline adjustment clip id."))},
                  {QStringLiteral("layerId"),
                   string_property(QStringLiteral("Optional stable id of the adjustment layer to edit."))},
                  {QStringLiteral("channel"),
                   QJsonObject{
                       {QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("enum"),
                        QJsonArray{QStringLiteral("RGB"), QStringLiteral("Luma"),
                                   QStringLiteral("Red"), QStringLiteral("Green"),
                                   QStringLiteral("Blue")}},
                   }},
                  {QStringLiteral("points"),
                   QJsonObject{
                       {QStringLiteral("type"), QStringLiteral("array")},
                       {QStringLiteral("maxItems"), 64},
                       {QStringLiteral("items"),
                        QJsonObject{
                            {QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"),
                             QJsonObject{
                                 {QStringLiteral("x"), number_property()},
                                 {QStringLiteral("y"), number_property()},
                             }},
                            {QStringLiteral("required"),
                             QJsonArray{QStringLiteral("x"), QStringLiteral("y")}},
                            {QStringLiteral("additionalProperties"), false},
                        }},
                       {QStringLiteral("description"),
                        QStringLiteral("Ordered curve points in the normalized 0 to 1 range; use an empty array to reset the selected channel.")},
                   }},
              },
              QJsonArray{QStringLiteral("clipId"), QStringLiteral("channel"),
                         QStringLiteral("points")}),
          false, false, false,
      },
      {
          "timeline.set_clip_color_grade_parameter", "Set Clip Color Grade Parameter",
          "Set one supported pointwise photo-engine control on a video or timeline adjustment clip by stable id. Adjustment clips grade the picture composite beneath them. Values update the existing grade recipe and are undoable; supported parameters include exposure, contrast, highlights, shadows, whites, blacks, temperature, tint, vibrance, saturation, and the hue, saturation, and luminance controls for shadow, midtone, and highlight color grading.",
          "timeline_color_grade_parameter_set",
          object_schema(
              QJsonObject{
                  {QStringLiteral("clipId"),
                   string_property(QStringLiteral("Stable video or timeline adjustment clip id."))},
                  {QStringLiteral("parameter"),
                   QJsonObject{
                       {QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("enum"),
                        QJsonArray{QStringLiteral("exposure"), QStringLiteral("contrast"),
                                   QStringLiteral("highlights"), QStringLiteral("shadows"),
                                   QStringLiteral("whites"), QStringLiteral("blacks"),
                                   QStringLiteral("temperature"), QStringLiteral("tint"),
                                   QStringLiteral("vibrance"),
                                   QStringLiteral("saturation"),
                                   QStringLiteral("gradingShadowsHue"),
                                   QStringLiteral("gradingShadowsSaturation"),
                                   QStringLiteral("gradingShadowsLuminance"),
                                   QStringLiteral("gradingMidtonesHue"),
                                   QStringLiteral("gradingMidtonesSaturation"),
                                   QStringLiteral("gradingMidtonesLuminance"),
                                   QStringLiteral("gradingHighlightsHue"),
                                   QStringLiteral("gradingHighlightsSaturation"),
                                   QStringLiteral("gradingHighlightsLuminance")}},
                       {QStringLiteral("description"),
                        QStringLiteral("Supported photo-engine grade parameter name, including three-way grading wheel controls.")},
                   }},
                  {QStringLiteral("value"),
                   number_property(QStringLiteral("New parameter value."))},
              },
              QJsonArray{QStringLiteral("clipId"), QStringLiteral("parameter"),
                         QStringLiteral("value")}),
          false, false, false,
      },
      {
          "timeline.clear_clip_color_grade", "Clear Clip Color Grade",
          "Remove the shared photo-engine grade recipe from a video or timeline adjustment clip by stable id. This is undoable.",
          "timeline_color_grade_clear",
          object_schema(
              QJsonObject{{QStringLiteral("clipId"),
                           string_property(QStringLiteral("Stable video or timeline adjustment clip id."))}},
              QJsonArray{QStringLiteral("clipId")}),
          false, false, false,
      },
      {
          "timeline.edit_clip_effects", "Edit Clip Effects",
          "Edit a picture clip's ordinary timeline effect stack by stable id. Operations add, remove, reorder, enable, clear, or set one effect amount. Amount edits on animated effects write at the current sequence playhead and preserve existing keyframes. Each edit is undoable.",
          "timeline_effects_edit",
          object_schema(
              QJsonObject{
                  {QStringLiteral("clipId"),
                   string_property(QStringLiteral("Stable timeline clip id."))},
                  {QStringLiteral("operation"),
                   QJsonObject{
                       {QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("enum"),
                        QJsonArray{QStringLiteral("add"), QStringLiteral("remove"),
                                   QStringLiteral("move_up"), QStringLiteral("move_down"),
                                   QStringLiteral("set_amount"), QStringLiteral("set_enabled"),
                                   QStringLiteral("set_stack_enabled"), QStringLiteral("clear")}},
                   }},
                  {QStringLiteral("effectName"),
                   QJsonObject{
                       {QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("enum"),
                        QJsonArray{QStringLiteral("Exposure"), QStringLiteral("Contrast"),
                                   QStringLiteral("Saturation"), QStringLiteral("Black & White"),
                                   QStringLiteral("Blur"), QStringLiteral("Fade"),
                                   QStringLiteral("Punch")}},
                   }},
                  {QStringLiteral("amount"),
                   number_property(QStringLiteral("Exposure, Contrast, and Saturation accept -1 to 1; other effects accept 0 to 1. Values are clamped to that range."))},
                  {QStringLiteral("enabled"), boolean_property()},
              },
              QJsonArray{QStringLiteral("clipId"), QStringLiteral("operation")}),
          false, false, false,
      },
      {
          "timeline.add_adjustment_clip", "Add Timeline Adjustment Clip",
          "Create a transparent adjustment clip over the picture tracks at an explicit sequence start time. The new stable clip id can be passed to timeline.edit_clip_effects and timeline.set_clip_opacity. Duration is limited to the remaining sequence range.",
          "timeline_adjustment_clip_add",
          object_schema(
              QJsonObject{
                  {QStringLiteral("startSeconds"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                               {QStringLiteral("minimum"), 0},
                               {QStringLiteral("description"),
                                QStringLiteral("Sequence start time in seconds.")}}},
                  {QStringLiteral("durationSeconds"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                               {QStringLiteral("exclusiveMinimum"), 0},
                               {QStringLiteral("description"),
                                QStringLiteral("Requested duration in seconds; clipped at the sequence end.")}}},
              },
              QJsonArray{QStringLiteral("startSeconds"),
                         QStringLiteral("durationSeconds")}),
          false, false, false,
      },
      {
          "timeline.set_clip_opacity", "Set Timeline Clip Opacity",
          "Set the opacity of a picture clip, including a timeline adjustment clip, by stable id. If the clip already has opacity keyframes, this writes or updates a keyframe at the current sequence playhead; otherwise it changes the clip-wide value.",
          "timeline_clip_opacity_set",
          object_schema(
              QJsonObject{
                  {QStringLiteral("clipId"),
                   string_property(QStringLiteral("Stable picture clip id."))},
                  {QStringLiteral("opacity"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                               {QStringLiteral("minimum"), 0},
                               {QStringLiteral("maximum"), 1},
                               {QStringLiteral("description"),
                                QStringLiteral("Opacity from fully transparent (0) to fully opaque (1).")}}},
              },
              QJsonArray{QStringLiteral("clipId"), QStringLiteral("opacity")}),
          false, false, false,
      },
      {
          "timeline.edit_clip_keyframe", "Edit Timeline Clip Keyframe",
          "Set or remove a transform, opacity, audio-gain, ordinary effect-amount, or shared photo-grade control keyframe by stable clip id. Use grade:<parameter> for the clip's base photo grade, or grade:layer:<stable-layer-id>:<parameter> for a photo adjustment layer on that clip. Both require the shared grade recipe and target layer to exist. Grade controls include exposure, contrast, highlights, shadows, whites, blacks, temperature, tint, vibrance, saturation, and the shadows/midtones/highlights wheel hue, saturation, and luminance channels. localSeconds is measured from the trimmed clip start. Call set at two or more times to create an animated curve; interpolation is linear by default and can use hold, easing, or Bezier.",
          "timeline_clip_keyframe_edit",
          object_schema(
              QJsonObject{
                  {QStringLiteral("clipId"),
                   string_property(QStringLiteral("Stable timeline clip id."))},
                  {QStringLiteral("operation"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                               {QStringLiteral("enum"),
                                QJsonArray{QStringLiteral("set"),
                                           QStringLiteral("remove")}}}},
                  {QStringLiteral("parameter"),
                   string_property(
                       QStringLiteral("Opacity, transform, audio gain, effect:<name>, grade:<parameter>, or grade:layer:<stable-layer-id>:<parameter>. Grade layer ids come from timeline.inspect_clip_color_grade."))},
                  {QStringLiteral("localSeconds"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                               {QStringLiteral("minimum"), 0},
                               {QStringLiteral("description"),
                                QStringLiteral("Time from the trimmed clip start in seconds.")}}},
                  {QStringLiteral("value"),
                   number_property(QStringLiteral("Keyframe value; required for set and clamped to the parameter range."))},
                  {QStringLiteral("interpolation"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                               {QStringLiteral("enum"),
                                QJsonArray{QStringLiteral("hold"),
                                           QStringLiteral("linear"),
                                           QStringLiteral("ease-in"),
                                           QStringLiteral("ease-out"),
                                           QStringLiteral("ease-in-out"),
                                           QStringLiteral("bezier")}},
                               {QStringLiteral("description"),
                                QStringLiteral("Optional interpolation for set; omitted updates preserve the existing interpolation.")}}},
              },
              QJsonArray{QStringLiteral("clipId"), QStringLiteral("operation"),
                         QStringLiteral("parameter"), QStringLiteral("localSeconds")}),
          false, false, false,
      },
      {
          "timeline.set_video_input_transform", "Set Video Input Transform",
          "Set automatic source-tag detection, a manual bypass, or a supported Sony S-Log input transform on a video clip by stable id. Automatic mode applies a transform only when file metadata has one exact supported Gamma for Look and Color for Look pair. This is undoable and marks color-derived render output as needing regeneration.",
          "timeline_color_set_video_input_transform",
          object_schema(
              QJsonObject{
                  {QStringLiteral("clipId"),
                   string_property(QStringLiteral("Stable id of a video clip in the active timeline."))},
                  {QStringLiteral("profile"), input_transform_property},
              },
              QJsonArray{QStringLiteral("clipId"), QStringLiteral("profile")}),
          false, false, false,
      },
      {
          "timeline.set_clip_color_lut", "Set Clip CUBE LUT",
          "Validate and assign a readable .cube file to a picture clip by stable id. Supports the photo engine's 1D, 3D, and shaper-plus-3D formats; the edit is undoable.",
          "timeline_color_set_lut",
          object_schema(
              QJsonObject{
                  {QStringLiteral("clipId"),
                   string_property(QStringLiteral("Stable timeline clip id."))},
                  {QStringLiteral("filePath"),
                   string_property(QStringLiteral("Absolute local path to a .cube LUT."))},
              },
              QJsonArray{QStringLiteral("clipId"), QStringLiteral("filePath")}),
          false, false, false,
      },
      {
          "timeline.clear_clip_color_lut", "Clear Clip CUBE LUT",
          "Remove the creative CUBE LUT from a picture clip by stable id. The edit is undoable.",
          "timeline_color_clear_lut",
          object_schema(
              QJsonObject{{QStringLiteral("clipId"),
                           string_property(QStringLiteral("Stable timeline clip id."))}},
              QJsonArray{QStringLiteral("clipId")}),
          false, false, false,
      },
      {
          "timeline.set_clip_shot_match_gains", "Set Clip Shot-Match Gains",
          "Set the bounded RGB shot-match gains on a picture clip by stable id. Values of 1.0 reset a channel; the edit is undoable and invalidates color-derived render output.",
          "timeline_color_set_shot_match_gains",
          object_schema(
              QJsonObject{
                  {QStringLiteral("clipId"),
                   string_property(QStringLiteral("Stable timeline clip id."))},
                  {QStringLiteral("redGain"), shot_match_gain_property},
                  {QStringLiteral("greenGain"), shot_match_gain_property},
                  {QStringLiteral("blueGain"), shot_match_gain_property},
              },
              QJsonArray{QStringLiteral("clipId"), QStringLiteral("redGain"),
                         QStringLiteral("greenGain"), QStringLiteral("blueGain")}),
          false, false, false,
      },
      {
          "photo.search_edit_start", "Find Photos for an Edit",
          "Start local visual search from one search-and-edit instruction. Never edits or confirms targets. The user must review matches and confirm in the app. Poll photo.search_edit_status.",
          "photo_search_edit_start",
          object_schema(QJsonObject{{"instruction", string_property()}}, QJsonArray{"instruction"}),
          false, false, false,
      },
      {
          "photo.search_edit_status", "Search and Edit Status",
          "Read search progress, interpreted instructions and candidate ids. Approval is only available in the local app.",
          "photo_search_edit_status", empty_object_schema(), true, false, false,
      },
      {
          "photo.search_edit_cancel", "Cancel Search",
          "Stop an unconfirmed photo search. Does not cancel or commit an active draft.",
          "photo_search_edit_cancel", empty_object_schema(), false, false, false,
      },
      {
          "photo.live_status",
          "Photo Live Status",
          "Read the current app, selection, agent, and draft status.",
          "photo_live_status",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.list_models",
          "List Agent Models",
          "Read available local agent models and the selected model id.",
          "photo_list_models",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.selected_assets",
          "Selected Assets",
          "Read selected asset ids and selected asset count.",
          "photo_selected_assets",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.read_asset",
          "Read Asset",
          "Read metadata for the selected asset or a requested asset id.",
          "photo_read_asset",
          object_schema(QJsonObject{{QStringLiteral("assetId"), string_property(QStringLiteral("Optional asset id."))}}),
          true,
          false,
          false,
      },
      {
          "photo.read_adjustments",
          "Read Adjustments",
          "Read the selected asset as a canonical asset-bound adjustment patch.",
          "photo_read_adjustments",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.read_recipe",
          "Read Legacy Recipe",
          "Compatibility-only read of the selected asset's legacy recipe projection.",
          "photo_read_recipe",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.get_recipe_patch_schema",
          "Recipe Patch Schema",
          "Read the validation-backed schema for agent recipe patch tools.",
          "photo_get_recipe_patch_schema",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.get_style_rulebook",
          "Style Rulebook",
          "Read the selected style rulebook, constraints, allowed tools, and batch policy.",
          "photo_get_style_rulebook",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.validate_recipe_patch",
          "Validate Recipe Patch",
          "Validate a recipe patch before applying it.",
          "photo_validate_recipe_patch",
          object_schema(
              QJsonObject{{QStringLiteral("patch"), generic_object_property(QStringLiteral("Recipe patch object."))}},
              QJsonArray{QStringLiteral("patch")}),
          true,
          false,
          false,
      },
      {
          "photo.render_current_proof",
          "Render Current Proof",
          "Render a proof image for the current selected asset.",
          "photo_render_proof",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.validate_edit",
          "Validate Current Edit",
          "Render and validate the current selected edit state.",
          "photo_validate_edit",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.validate_batch",
          "Validate Batch",
          "Validate selected assets without mutating recipes.",
          "photo_validate_batch",
          empty_object_schema(),
          true,
          false,
          false,
      },
      {
          "photo.export_preflight",
          "Export Preflight",
          "Check export readiness before queueing jobs.",
          "photo_export_preflight",
          object_schema(metrics_properties(false)),
          true,
          false,
          false,
      },
      {
          "photo.export_batch",
          "Export Batch",
          "Queue export jobs for selected assets after explicit confirmation.",
          "photo_export_batch",
          object_schema(metrics_properties(true), QJsonArray{QStringLiteral("confirmed")}),
          false,
          true,
          true,
      },
      {
          "photo.apply_recipe_patch_draft",
          "Apply Recipe Patch Draft",
          "Apply a validated recipe patch as a reversible draft.",
          "photo_apply_recipe_patch_draft",
          object_schema(
              QJsonObject{
                  {QStringLiteral("patch"), generic_object_property(QStringLiteral("Recipe patch object."))},
                  {QStringLiteral("confirmed"), boolean_property(QStringLiteral("Required for retouch patches."))},
              },
              QJsonArray{QStringLiteral("patch")}),
          false,
          true,
          false,
      },
      {
          "photo.plan_bulk_edit",
          "Plan Bulk Edit",
          "Plan a selected-assets edit without mutating recipes.",
          "photo_plan_bulk_edit",
          object_schema(QJsonObject{{QStringLiteral("instructions"), string_property()}}),
          true,
          false,
          false,
      },
      {
          "photo.apply_bulk_draft",
          "Apply Bulk Draft",
          "Apply a reversible draft to the selected asset set.",
          "photo_apply_bulk_draft",
          object_schema(QJsonObject{{QStringLiteral("instructions"), string_property()}}),
          false,
          true,
          false,
      },
      {
          "photo.inspect_composition", "Inspect Photo Composition",
          "Read the open photo's layers in top-first order, stable ids, selection ids, canvas dimensions, and installed fonts. Call photo.inspect_composition with {assetId}; omit operation and values to receive the authoritative exact family-to-face inventory in fontFaces entries {family,faces}. Choose one exact family and face string from that inventory; do not probe guessed face names to discover styles. With no family/face requested, font availability and fontAxes fields are omitted, not false. If verifying one selected pair, pass values {family,face}; the requested names are echoed without substitution, fontFamilyAvailable/fontFaceAvailable report that exact pair, requestedFamilyFaces lists the selected family's faces, and fontAxes may be empty for an available fixed font.",
          "photo_inspect_composition",
          object_schema(QJsonObject{
              {"assetId", string_property()},
              {"values", object_schema(QJsonObject{
                  {"family", string_property()}, {"face", string_property()},
              }, QJsonArray{"family"}, false)},
          }, QJsonArray{"assetId"}),
          true, false, false,
      },
      {
          "photo.edit_composition", "Edit Photo Composition",
          "Edit the open photo through the same undoable controls as the user. Read state first using photo.inspect_composition with {assetId}; inspect is not an edit operation. Operations and values: add_text {text,x,y}; add_pixel_layer {} creates an empty editable transparent paint layer and returns createdLayerId; add_source_layer {} creates an editable linked source layer from the open photo and returns createdLayerId, allowing effects to stay editable above the base image; text {text}; typography {family,face,size,weight,italic,underline,color,tracking,lineHeight,strokeWidth,strokeColor,shadowX,shadowY,shadowColor,mode,paragraphSpacing,axes,pathPoints,pathContours,pathStart,pathEnd,pathOffset,pathSpacing,pathSide,pathClosed,pathExclusion,material,materialTint,materialBlur,materialTransmission,materialEdgeSoftness,materialDepth,materialLightAngle}; axes is a map of supported OpenType tags; photo.inspect_composition accepts values {family,face} and returns fontAxes with supported tags and ranges before values are chosen; pathPoints is an ordered list of normalized {x,y} baseline points, with pathStart/pathEnd selecting a path interval, pathOffset moving text to either side, pathSpacing adding tracking in document pixels, and pathSide choosing the contour side, pathContours preserving multiple closed mask outlines including holes, and pathExclusion=true flowing paragraph lines around those contours through the widest free interval. Contour text without exclusion remains single-line. material is none, frosted_glass, smoked_plastic, soft_emboss, polished_chrome, brushed_silver, warm_gold, copper, dark_steel, champagne_gold, rose_gold, antique_brass, titanium, or black_chrome; materialTint sets the material color, materialBlur controls glass backdrop blur, materialTransmission controls glass transparency or metal reflectivity, materialEdgeSoftness and materialDepth shape the bevel, and materialLightAngle moves the studio reflections and edge light. Metal finishes use directional reflection bands and glyph-edge relief so they remain visibly metallic as tint, depth, and light are edited. Glass reads only layers below the text; upper layers remain composited afterward. text_path {maskId,contourIndex?} detaches a traced selection contour into the selected text layer; query the font face's available axes before setting values, and leave axes empty for static fonts; paint {layerId,points,color,size,hardness,opacity,flow,spacing,textureMaterial,textureStrength,textureScale,textureAngle,textureSeed,pressureSize,pressureFlow,clipSelection,lightFalloff,lightExposure,lightTaper,lightAccumulation,tipLayerId,tipPresetName,tipRotationVariation,tipScaleVariation}; points are canvas-pixel {x,y,pressure} samples on an unlocked pixel layer. textureMaterial is solid, dry_ink, graphite, light_paint, or photo_fragment; photo_fragment accepts exactly one of tipLayerId or tipPresetName and uses seeded tipRotationVariation/tipScaleVariation; save_photo_fragment_preset {name,tipLayerId or sourcePresetName,rotationVariation?,scaleVariation?,overwrite?}, import_photo_fragment_preset {path,overwrite?}, export_photo_fragment_preset {name,path}, and delete_photo_fragment_preset {name} manage portable Photo Fragment assets. inspect returns photoFragmentBrushPresets. light_paint adds a soft HDR halo controlled by lightFalloff (0-1), lightExposure (-8 to +8 EV), lightTaper (0-1 at both stroke ends), and lightAccumulation (boolean). For photo_fragment, first create an independent cutout with copy_selection {maskId,independent:true}, inspect to get its pixel-layer ID, then save_photo_fragment_preset with that tipLayerId; paint later with tipPresetName even if the source layer has changed or been removed. The stamp keeps soft alpha and holes. tipRotationVariation is seeded degrees (0-180) and tipScaleVariation is seeded size variance (0-0.9). poll inspect until paintBusy is false and check paintError; transform {x,y,scale,rotation}; perspective {horizontal,vertical}; move {position} (0 is top); copy_selection {maskId,independent?} (omit layerId for the base photo, or provide a content-layer ID to copy that layer through the selection while intersecting its existing mask; independent=true rasterizes the selected content layer and its masks into a self-contained pixel cutout); erase_selection {maskId} (pixel layer required; queues an undoable pixel erase; poll inspect until paintBusy is false and check paintError); select_subject {} (reports an error if segmentation is unavailable); lasso_selection {points,subtract?,feather?} creates a closed editable selection from 3 to 4096 normalized image coordinates, returns createdMaskId, and accepts normalized feather 0-0.05 for soft-edged cutouts; use it to isolate a specific reflection or other detail before copy_selection. blur {sigma}; opacity {opacity}; add_effect {effectType plus type-specific registered effect controls}; update_effect {effectId plus changed controls}; remove_effect {effectId}; reorder_effect {effectId,position} where position is the zero-based effect row within that layer; mask_effect {effectId,maskId} creates an effect mask from an existing selection; clear_effect_mask {effectId}. Effect IDs are returned by add_effect and listed by inspect. These operations use the same undoable layer-effect stack as the editor. Supply only changed typography fields. For text behind a subject: generate or reuse a selection, add and style text, then copy_selection with independent=true and no layerId to place a self-contained cutout above text. Changes apply directly to the open document and support ordinary Undo; they are not agent drafts.",
          "photo_edit_composition",
          composition_edit_input_schema(),
          false, false, false,
      },
      {
          "photo.create_mask",
          "Create Mask",
          "Create a supported mask and apply initial local adjustments as a draft.",
          "photo_create_mask",
          object_schema(
              QJsonObject{
                  {QStringLiteral("type"), string_property(QStringLiteral("linear or radial"))},
                  {QStringLiteral("exposure"), number_property()},
                  {QStringLiteral("temperature"), number_property()},
                  {QStringLiteral("feather"), number_property()},
                  {QStringLiteral("position"), number_property()},
                  {QStringLiteral("rotation"), number_property()},
                  {QStringLiteral("centerX"), number_property()},
                  {QStringLiteral("centerY"), number_property()},
                  {QStringLiteral("radiusX"), number_property()},
                  {QStringLiteral("radiusY"), number_property()},
              },
              QJsonArray{QStringLiteral("type")}),
          false,
          true,
          false,
      },
      {
          "photo.apply_mask_adjustment",
          "Apply Mask Adjustment",
          "Apply a local mask adjustment as a reversible draft.",
          "photo_apply_mask_adjustment",
          object_schema(
              QJsonObject{
                  {QStringLiteral("type"), string_property(QStringLiteral("linear or radial"))},
                  {QStringLiteral("exposure"), number_property()},
                  {QStringLiteral("temperature"), number_property()},
                  {QStringLiteral("feather"), number_property()},
                  {QStringLiteral("position"), number_property()},
                  {QStringLiteral("rotation"), number_property()},
                  {QStringLiteral("centerX"), number_property()},
                  {QStringLiteral("centerY"), number_property()},
                  {QStringLiteral("radiusX"), number_property()},
                  {QStringLiteral("radiusY"), number_property()},
              },
              QJsonArray{QStringLiteral("type")}),
          false,
          true,
          false,
      },
      {
          "photo.run_draft_on_selected",
          "Run Draft On Selected",
          "Plan and apply an agent draft for the selected assets.",
          "photo_run_draft_on_selected",
          object_schema(QJsonObject{{QStringLiteral("instructions"), string_property()}}),
          false,
          true,
          false,
      },
      {
          "photo.commit_agent_draft",
          "Commit Agent Draft",
          "Commit the active reversible draft after explicit confirmation.",
          "photo_commit_agent_draft",
          object_schema(
              QJsonObject{{QStringLiteral("confirmed"), boolean_property(QStringLiteral("Must be true."))}},
              QJsonArray{QStringLiteral("confirmed")}),
          false,
          true,
          true,
      },
      {
          "photo.resolve_agent_review_issue",
          "Resolve Agent Review Issue",
          "Resolve one flagged draft photo by fixing it, accepting it anyway, or rolling back that photo.",
          "photo_resolve_agent_review_issue",
          object_schema(
              QJsonObject{
                  {QStringLiteral("assetId"), string_property()},
                  {QStringLiteral("action"),
                   string_property(QStringLiteral(
                       "fix, accept_anyway, or rollback_photo"))},
                  {QStringLiteral("confirmed"),
                   boolean_property(QStringLiteral("Must be true."))},
              },
              QJsonArray{QStringLiteral("assetId"),
                         QStringLiteral("action"),
                         QStringLiteral("confirmed")}),
          false,
          true,
          true,
      },
      {
          "photo.cancel_agent_run",
          "Cancel Agent Run",
          "Cancel an active agent run.",
          "photo_cancel_agent_run",
          object_schema(QJsonObject{{QStringLiteral("reason"), string_property()}}),
          false,
          false,
          false,
      },
      {
          "photo.rollback_agent_draft",
          "Rollback Agent Draft",
          "Rollback the active reversible agent draft.",
          "photo_rollback_agent_draft",
          empty_object_schema(),
          false,
          true,
          false,
      },
  };
}

const PhotoMcpTool* find_tool(const QString& name) {
  static const auto tools = photo_mcp_tools();
  const auto iterator = std::find_if(
      tools.cbegin(),
      tools.cend(),
      [&](const PhotoMcpTool& tool) {
        return name == QString::fromLatin1(tool.name);
      });
  return iterator == tools.cend() ? nullptr : &(*iterator);
}

QString required_capability(const PhotoMcpTool& tool) {
  return automation_capability_for_command(
      QString::fromLatin1(tool.automation_command));
}

QStringList all_capabilities() {
  return automation_capability_names();
}

bool permits_tool(const PhotoMcpTool& tool, const QStringList& capabilities) {
  return capabilities.contains(required_capability(tool));
}

QJsonObject tool_annotations(const PhotoMcpTool& tool) {
  return QJsonObject{
      {QStringLiteral("readOnlyHint"), tool.read_only},
      {QStringLiteral("destructiveHint"), tool.destructive},
      {QStringLiteral("requiresConfirmation"), tool.requires_confirmation},
      {QStringLiteral("localOnly"), true},
  };
}

QJsonObject mcp_tool_object(const PhotoMcpTool& tool) {
  return QJsonObject{
      {QStringLiteral("name"), QString::fromLatin1(tool.name)},
      {QStringLiteral("title"), QString::fromLatin1(tool.title)},
      {QStringLiteral("description"), QString::fromLatin1(tool.description)},
      {QStringLiteral("inputSchema"), tool.input_schema},
      {QStringLiteral("annotations"), tool_annotations(tool)},
      {QStringLiteral("requiredCapability"), required_capability(tool)},
  };
}

QString compact_json(const QJsonObject& object) {
  return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

}  // namespace

PhotoMcpServer::PhotoMcpServer(PhotoAutomationBridge* automation_bridge, QObject* parent,
                             bool internal_agent)
    : QObject(parent), automation_bridge_(automation_bridge), internal_agent_(internal_agent) {}

QJsonObject PhotoMcpServer::toolsListResult() const {
  return toolsListResult(all_capabilities());
}

QJsonObject PhotoMcpServer::toolsListResult(const QStringList& capabilities) const {
  QJsonArray tools;
  for (const auto& tool : photo_mcp_tools()) {
    if (permits_tool(tool, capabilities)) {
      tools.append(mcp_tool_object(tool));
    }
  }
  return QJsonObject{{QStringLiteral("tools"), tools}};
}

QJsonObject PhotoMcpServer::callTool(const QString& name, const QJsonObject& arguments) const {
  return callTool(name, arguments, all_capabilities());
}

QJsonObject PhotoMcpServer::callTool(
    const QString& name,
    const QJsonObject& arguments,
    const QStringList& capabilities) const {
  const auto* tool = find_tool(name.trimmed());
  if (tool == nullptr) {
    return QJsonObject{
        {QStringLiteral("content"),
         QJsonArray{QJsonObject{
             {QStringLiteral("type"), QStringLiteral("text")},
             {QStringLiteral("text"), QStringLiteral("Unknown photo tool: %1").arg(name)},
         }}},
        {QStringLiteral("isError"), true},
    };
  }
  if (!permits_tool(*tool, capabilities)) {
    const auto capability = required_capability(*tool);
    return QJsonObject{
        {QStringLiteral("content"),
         QJsonArray{QJsonObject{
             {QStringLiteral("type"), QStringLiteral("text")},
             {QStringLiteral("text"),
              QStringLiteral("MCP capability '%1' is required for %2.")
                  .arg(capability, name)},
         }}},
        {QStringLiteral("structuredContent"),
         QJsonObject{
             {QStringLiteral("error"), QStringLiteral("insufficient_capability")},
             {QStringLiteral("requiredCapability"), capability},
         }},
        {QStringLiteral("isError"), true},
    };
  }
  if (automation_bridge_ == nullptr) {
    return QJsonObject{
        {QStringLiteral("content"),
         QJsonArray{QJsonObject{
             {QStringLiteral("type"), QStringLiteral("text")},
             {QStringLiteral("text"), QStringLiteral("Photo automation bridge is not connected.")},
         }}},
        {QStringLiteral("isError"), true},
    };
  }

  const auto automation_request = QJsonObject{
      {QStringLiteral("token"), automation_bridge_->token()},
      {QStringLiteral("command"), QString::fromLatin1(tool->automation_command)},
      {QStringLiteral("args"), arguments},
  };
  const auto automation_response = internal_agent_
      ? automation_bridge_->handleAgentJsonRequest(automation_request)
      : automation_bridge_->handleJsonRequest(automation_request);
  const auto ok = automation_response.value(QStringLiteral("ok")).toBool(false);
  auto structured_content = ok
                                      ? automation_response.value(QStringLiteral("result")).toObject()
                                      : QJsonObject{
                                            {QStringLiteral("error"),
                                             automation_response.value(QStringLiteral("error")).toString()}};
  if (!ok && automation_response.value(QStringLiteral("data")).isObject()) {
    structured_content = automation_response.value(QStringLiteral("data")).toObject();
    structured_content.insert(QStringLiteral("error"), automation_response.value(QStringLiteral("error")));
  }
  const auto text = ok
                        ? compact_json(structured_content)
                        : automation_response.value(QStringLiteral("error")).toString();
  QJsonObject result{
      {QStringLiteral("content"),
       QJsonArray{QJsonObject{
           {QStringLiteral("type"), QStringLiteral("text")},
           {QStringLiteral("text"), text},
       }}},
      {QStringLiteral("structuredContent"), structured_content},
  };
  if (!ok) {
    result.insert(QStringLiteral("isError"), true);
  }
  return result;
}

QJsonObject PhotoMcpServer::handleJsonRpc(const QJsonObject& request) const {
  return handleJsonRpc(request, all_capabilities());
}

QJsonObject PhotoMcpServer::handleJsonRpc(
    const QJsonObject& request,
    const QStringList& capabilities) const {
  const auto id = request.value(QStringLiteral("id"));
  if (request.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0")) {
    return json_rpc_error(id, -32600, QStringLiteral("MCP requests must use JSON-RPC 2.0."));
  }
  const auto method = request.value(QStringLiteral("method")).toString();
  if (method == QStringLiteral("tools/list")) {
    const auto params = request.value(QStringLiteral("params"));
    if (!params.isUndefined() && !params.isObject()) {
      return json_rpc_error(id, -32602, QStringLiteral("tools/list params must be an object when provided."));
    }
    return json_rpc_result(id, toolsListResult(capabilities));
  }
  if (method == QStringLiteral("tools/call")) {
    const auto params = request.value(QStringLiteral("params")).toObject();
    const auto name = params.value(QStringLiteral("name")).toString().trimmed();
    const auto arguments_value = params.value(QStringLiteral("arguments"));
    if (name.isEmpty() || (!arguments_value.isUndefined() && !arguments_value.isObject())) {
      return json_rpc_error(
          id,
          -32602,
          QStringLiteral("tools/call requires params.name and optional object params.arguments."));
    }
    return json_rpc_result(id, callTool(name, arguments_value.toObject(), capabilities));
  }
  return json_rpc_error(id, -32601, QStringLiteral("Unsupported MCP method: %1").arg(method));
}

}  // namespace photo_editor::desktop
