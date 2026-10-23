#pragma once

#include <QVariantMap>
#include <QStringList>

namespace photo_editor::desktop {

inline QVariantMap agent_number_schema(double minimum, double maximum, const QString& description = {}) {
  return {{"type", "number"}, {"minimum", minimum}, {"maximum", maximum}, {"description", description}};
}

inline QVariantMap agent_string_enum(const QStringList& choices, const QString& description = {}) {
  return {{"type", "string"}, {"enum", choices}, {"description", description}};
}

// These partial-update schemas mirror the native setters, not a replacement
// document format. Native cross-field, font and image checks remain authoritative.
inline QVariantMap agent_shape_fill_schema() {
  const QVariantMap color{{"type", "string"}, {"description", "Qt color name or hex. Prefer opaque six-digit #RRGGBB. Eight-digit hex is Qt #AARRGGBB for intentional alpha, never CSS #RRGGBBAA."}};
  return {{"type", "object"}, {"additionalProperties", false}, {"minProperties", 1},
      {"description", "Partial shape fill update. Gradient centers are normalized within the shape; angles are degrees."},
      {"properties", QVariantMap{
          {"fillMode", agent_string_enum({"none", "solid", "linear", "radial", "texture"})},
          {"fillColor", color}, {"gradientStartColor", color}, {"gradientEndColor", color},
          {"gradientAngle", agent_number_schema(-360, 360, "Degrees.")},
          {"gradientCenterX", agent_number_schema(0, 1, "Normalized horizontal coordinate; zero is left.")},
          {"gradientCenterY", agent_number_schema(0, 1, "Normalized vertical coordinate; zero is top.")},
          {"gradientRadius", agent_number_schema(0.05, 2)},
          {"textureType", agent_string_enum({"paper", "grain", "hatch", "image"})},
          {"textureStrength", agent_number_schema(0, 1)}, {"textureScale", agent_number_schema(0.1, 8)},
          {"textureAngle", agent_number_schema(-360, 360)},
          {"textureSeed", agent_number_schema(0, 4294967295.0)},
          {"textureData", QVariantMap{{"type", "string"}, {"description", "Existing encoded texture only; use setPhotoLayerShapeTexture to load a file."}}},
          {"textureName", QVariantMap{{"type", "string"}, {"maxLength", 256}}}}}};
}

inline QVariantMap agent_typography_schema() {
  const QVariantMap text{{"type", "string"}, {"maxLength", 1024}};
  const QVariantMap color{{"type", "string"}, {"description", "Qt color name or hex. Prefer opaque six-digit #RRGGBB. Eight-digit hex is Qt #AARRGGBB for intentional alpha, never CSS #RRGGBBAA."}};
  const QVariantMap boolean{{"type", "boolean"}};
  const QVariantMap point{{"type", "object"}, {"additionalProperties", false}, {"required", QStringList{"x", "y"}},
      {"properties", QVariantMap{{"x", agent_number_schema(0, 1)}, {"y", agent_number_schema(0, 1)}}}};
  const QVariantMap contour{{"type", "array"}, {"minItems", 3}, {"maxItems", 8192}, {"items", point}};
  return {{"type", "object"}, {"additionalProperties", false}, {"minProperties", 1},
      {"description", "Partial text typography update. Distances are canvas pixels; path points are normalized. Inspect available fonts/faces/axes before selecting them."},
      {"properties", QVariantMap{
          {"version", QVariantMap{{"type", "integer"}, {"minimum", 1}, {"maximum", 9}}},
          {"family", text}, {"face", text}, {"color", color},
          {"size", agent_number_schema(0.25, 8192, "Font size in canvas pixels.")},
          {"weight", QVariantMap{{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}},
          {"italic", boolean}, {"underline", boolean},
          {"tracking", agent_number_schema(-8192, 8192,
              "Qt AbsoluteSpacing: additional canvas pixels between glyphs. Zero is normal; 40 adds 40 canvas pixels between glyphs.")},
          {"lineHeight", agent_number_schema(0.1, 20,
              "Line-height multiplier; 1.0 is the normal line advance.")},
          {"baselineOffset", agent_number_schema(-8192, 8192)},
          {"mode", agent_string_enum({"point", "paragraph"})},
          {"paragraphSpacing", agent_number_schema(0, 8192)},
          {"strokeWidth", agent_number_schema(0, 256)}, {"strokeColor", color},
          {"shadowX", agent_number_schema(-8192, 8192)}, {"shadowY", agent_number_schema(-8192, 8192)},
          {"shadowColor", color},
          {"axes", QVariantMap{{"type", "object"}, {"maxProperties", 32},
              {"additionalProperties", QVariantMap{{"type", "number"}}},
              {"description", "Supported font axis tags mapped to values within the inspected native axis ranges."}}},
          {"pathPoints", QVariantMap{{"type", "array"}, {"minItems", 2}, {"maxItems", 8192}, {"items", point}}},
          {"pathStart", agent_number_schema(0, 1)}, {"pathEnd", agent_number_schema(0, 1)},
          {"pathOffset", agent_number_schema(-8192, 8192)}, {"pathSpacing", agent_number_schema(0, 8192)},
          {"pathSide", agent_string_enum({"left", "right"})}, {"pathClosed", boolean}, {"pathExclusion", boolean},
          {"pathContours", QVariantMap{{"type", "array"}, {"minItems", 1}, {"maxItems", 256}, {"items", contour}}},
          {"material", agent_string_enum({"none", "frosted_glass", "smoked_plastic", "soft_emboss",
              "polished_chrome", "brushed_silver", "warm_gold", "copper", "dark_steel",
              "champagne_gold", "rose_gold", "antique_brass", "titanium", "black_chrome"})},
          {"materialTint", color}, {"materialBlur", agent_number_schema(0, 64)},
          {"materialTransmission", agent_number_schema(0, 1)}, {"materialEdgeSoftness", agent_number_schema(0, 16)},
          {"materialDepth", agent_number_schema(0, 32)}, {"materialLightAngle", agent_number_schema(-180, 180)}}}};
}

}  // namespace photo_editor::desktop
