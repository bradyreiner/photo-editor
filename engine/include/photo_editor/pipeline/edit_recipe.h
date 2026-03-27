#pragma once

#include <string>
#include <vector>

namespace photo_editor::pipeline {

struct GlobalAdjustments final {
  double exposure = 0.0;
  double contrast = 0.0;
  double highlights = 0.0;
  double shadows = 0.0;
  double whites = 0.0;
  double blacks = 0.0;
  double temperature = 0.0;
  double tint = 0.0;
  double texture = 0.0;
  double clarity = 0.0;
  double dehaze = 0.0;
  double vibrance = 0.0;
  double saturation = 0.0;
};

struct ParametricCurveDefinition final {
  double shadows = 0.0;
  double darks = 0.0;
  double lights = 0.0;
  double highlights = 0.0;
};

struct CurvePoint final {
  double x = 0.0;
  double y = 0.0;
  std::string type = "smooth";
};

struct CurveChannelDefinition final {
  std::string channel = "master";
  std::vector<CurvePoint> points;
};

struct CurveSetDefinition final {
  std::string mode = "parametric";
  ParametricCurveDefinition parametric;
  std::vector<CurveChannelDefinition> channels;
};

struct ColorMixerDefinition final {
  std::string channel = "Reds";
  double hue = 0.0;
  double saturation = 0.0;
  double luminance = 0.0;
};

struct ColorGradingDefinition final {};

struct CropTransformDefinition final {
  double x = 0.0;
  double y = 0.0;
  double width = 0.0;
  double height = 0.0;
  std::string preset = "Original";
  double zoom = 1.0;
};

struct MaskAdjustments final {
  double exposure = 0.0;
  double temperature = 0.0;
};

struct MaskGeometryDefinition final {
  double feather = 0.22;
  double position = 0.5;
  double rotation = 0.0;
};

struct MaskDefinition final {
  std::string id;
  std::string kind;
  std::string operation = "add";
  bool enabled = true;
  bool ai_assisted = false;
  MaskAdjustments adjustments;
  MaskGeometryDefinition geometry;
};

struct OverlayItemDefinition final {
  std::string id;
  std::string kind;
};

struct GuideSettingsDefinition final {
  std::vector<double> custom_guides;
};

struct EditRecipe final {
  int recipe_version = 3;
  std::string asset_id;
  GlobalAdjustments global_adjustments;
  CurveSetDefinition curves;
  ColorMixerDefinition color_mixer;
  ColorGradingDefinition color_grading;
  CropTransformDefinition crop_transform;
  std::vector<MaskDefinition> mask_stack;
  std::vector<OverlayItemDefinition> overlay_items;
  GuideSettingsDefinition guide_settings;
};

}  // namespace photo_editor::pipeline
