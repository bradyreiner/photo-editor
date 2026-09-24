#include "photo_editor/pipeline/color_adjustments.h"

#include <algorithm>
#include <array>

namespace photo_editor::pipeline {

const char* color_operation_domain_name(ColorOperationDomain domain) {
  switch (domain) {
    case ColorOperationDomain::SceneReferred:
      return "scene-referred";
    case ColorOperationDomain::DisplayReferred:
      return "display-referred";
    case ColorOperationDomain::LookSpace:
      return "look-space";
  }
  return "scene-referred";
}

const char* color_pipeline_stage_name(ColorPipelineStage stage) {
  switch (stage) {
    case ColorPipelineStage::InputSceneLinear:
      return "input-scene-linear";
    case ColorPipelineStage::SceneTone:
      return "scene-tone";
    case ColorPipelineStage::DisplayCurve:
      return "display-curve";
    case ColorPipelineStage::ChannelLook:
      return "channel-look";
    case ColorPipelineStage::PerceptualLook:
      return "perceptual-look";
    case ColorPipelineStage::CreativeLut:
      return "creative-lut";
    case ColorPipelineStage::OutputRendering:
      return "output-rendering";
    case ColorPipelineStage::ProofSimulation:
      return "proof-simulation";
  }
  return "input-scene-linear";
}

std::span<const ColorControlSemantics> core_color_control_semantics() {
  static constexpr auto kSemantics = std::to_array<ColorControlSemantics>({
      {"exposure", "Exposure", ColorOperationDomain::SceneReferred,
       ColorPipelineStage::InputSceneLinear, 100, true, false,
       "Linear gain applied before tone and creative color."},
      {"temperature", "Temperature", ColorOperationDomain::SceneReferred,
       ColorPipelineStage::InputSceneLinear, 110, true, false,
       "Chromatic adaptation in the linear input stage."},
      {"tint", "Tint", ColorOperationDomain::SceneReferred,
       ColorPipelineStage::InputSceneLinear, 120, true, false,
       "Chromatic adaptation in the linear input stage."},
      {"contrast", "Contrast", ColorOperationDomain::SceneReferred,
       ColorPipelineStage::SceneTone, 200, false, false,
       "Monotone log-luminance pivot shaping before curves and color styling."},
      {"highlights", "Highlights", ColorOperationDomain::SceneReferred,
       ColorPipelineStage::SceneTone, 210, false, false,
       "Monotone upper scene-luminance shaping before display curves."},
      {"shadows", "Shadows", ColorOperationDomain::SceneReferred,
       ColorPipelineStage::SceneTone, 220, false, false,
       "Monotone lower scene-luminance shaping before display curves."},
      {"whites", "Whites", ColorOperationDomain::SceneReferred,
       ColorPipelineStage::SceneTone, 230, false, false,
       "Monotone high-stop scene-luminance shaping before display curves."},
      {"blacks", "Blacks", ColorOperationDomain::SceneReferred,
       ColorPipelineStage::SceneTone, 240, false, false,
       "Monotone deep-shadow scene-luminance shaping before display curves."},
      {"brightness", "Brightness", ColorOperationDomain::SceneReferred,
       ColorPipelineStage::SceneTone, 250, false, false,
       "Midtone luminance shaping in the scene-tone stage."},
      {"hdrLocalToneMapping", "Highlight Detail",
       ColorOperationDomain::SceneReferred, ColorPipelineStage::SceneTone,
       260, false, true,
       "Image-dependent local highlight compression."},
      {"filmicShoulder", "Highlight Roll-off",
       ColorOperationDomain::SceneReferred, ColorPipelineStage::SceneTone,
       270, false, true,
       "Image-aware scene-highlight shoulder."},
      {"filmicToe", "Shadow Fade", ColorOperationDomain::SceneReferred,
       ColorPipelineStage::SceneTone, 280, false, false,
       "Scene-luminance toe shaping."},
      {"adaptiveContrast", "Local Contrast",
       ColorOperationDomain::SceneReferred, ColorPipelineStage::SceneTone,
       290, false, true,
       "Image-dependent scene-luminance contrast."},
      {"levelsBlackPoint", "Black Input",
       ColorOperationDomain::SceneReferred, ColorPipelineStage::SceneTone,
       300, false, false,
       "Input-level remap in scene luminance."},
      {"levelsGamma", "Gamma", ColorOperationDomain::SceneReferred,
       ColorPipelineStage::SceneTone, 310, false, false,
       "Input-level gamma remap in scene luminance."},
      {"levelsWhitePoint", "White Input",
       ColorOperationDomain::SceneReferred, ColorPipelineStage::SceneTone,
       320, false, false,
       "Input-level remap in scene luminance."},
      {"zoneDeepShadows", "Deep Shadows",
       ColorOperationDomain::SceneReferred, ColorPipelineStage::SceneTone,
       330, false, false,
       "Deep-shadow zone exposure in scene luminance."},
      {"zoneShadows", "Zone Shadows", ColorOperationDomain::SceneReferred,
       ColorPipelineStage::SceneTone, 340, false, false,
       "Shadow-zone exposure in scene luminance."},
      {"zoneMidtones", "Zone Midtones", ColorOperationDomain::SceneReferred,
       ColorPipelineStage::SceneTone, 350, false, false,
       "Midtone-zone exposure in scene luminance."},
      {"zoneLights", "Zone Lights", ColorOperationDomain::SceneReferred,
       ColorPipelineStage::SceneTone, 360, false, false,
       "Light-zone exposure in scene luminance."},
      {"zoneHighlights", "Zone Highlights",
       ColorOperationDomain::SceneReferred, ColorPipelineStage::SceneTone,
       370, false, false,
       "Highlight-zone exposure in scene luminance."},
      {"curveShadows", "Curve Shadows",
       ColorOperationDomain::DisplayReferred, ColorPipelineStage::DisplayCurve,
       400, false, false,
       "Perceptual parametric curve after scene-tone shaping."},
      {"curveDarks", "Curve Darks", ColorOperationDomain::DisplayReferred,
       ColorPipelineStage::DisplayCurve, 410, false, false,
       "Perceptual parametric curve after scene-tone shaping."},
      {"curveLights", "Curve Lights", ColorOperationDomain::DisplayReferred,
       ColorPipelineStage::DisplayCurve, 420, false, false,
       "Perceptual parametric curve after scene-tone shaping."},
      {"curveHighlights", "Curve Highlights",
       ColorOperationDomain::DisplayReferred, ColorPipelineStage::DisplayCurve,
       430, false, false,
       "Perceptual parametric curve after scene-tone shaping."},
      {"masterCurvePoints", "Master RGB Curve",
       ColorOperationDomain::DisplayReferred, ColorPipelineStage::DisplayCurve,
       440, false, false,
       "Display-style composite RGB curve."},
      {"lumaCurvePoints", "Luminance Curve",
       ColorOperationDomain::DisplayReferred, ColorPipelineStage::DisplayCurve,
       450, false, false,
       "Display-style luminance curve preserving chromatic ratios."},
      {"redCurvePoints", "Red Curve", ColorOperationDomain::DisplayReferred,
       ColorPipelineStage::DisplayCurve, 460, false, false,
       "Display-style red-channel curve."},
      {"greenCurvePoints", "Green Curve",
       ColorOperationDomain::DisplayReferred, ColorPipelineStage::DisplayCurve,
       470, false, false,
       "Display-style green-channel curve."},
      {"blueCurvePoints", "Blue Curve", ColorOperationDomain::DisplayReferred,
       ColorPipelineStage::DisplayCurve, 480, false, false,
       "Display-style blue-channel curve."},
      {"rgbChannelMixer", "RGB Channel Mixer", ColorOperationDomain::LookSpace,
       ColorPipelineStage::ChannelLook, 500, false, false,
       "Creative linear-channel mixing after curves."},
      {"blackAndWhiteMixer", "Black and White Mixer",
       ColorOperationDomain::LookSpace, ColorPipelineStage::ChannelLook,
       510, false, false,
       "Creative monochrome channel mix after curves."},
      {"vibrance", "Vibrance", ColorOperationDomain::LookSpace,
       ColorPipelineStage::PerceptualLook, 600, false, false,
       "Protected OKLab chroma shaping before hue-domain tools."},
      {"colorMixer", "Color Mixer", ColorOperationDomain::LookSpace,
       ColorPipelineStage::PerceptualLook, 610, false, false,
       "Selective OKLab hue, chroma, and lightness styling."},
      {"colorReplacement", "Color Replacement",
       ColorOperationDomain::LookSpace, ColorPipelineStage::PerceptualLook,
       620, false, false,
       "Selective creative hue replacement in perceptual space."},
      {"colorHarmony", "Color Harmony", ColorOperationDomain::LookSpace,
       ColorPipelineStage::PerceptualLook, 630, false, false,
       "Creative hue harmonization in perceptual space."},
      {"colorGrading", "Color Grading", ColorOperationDomain::LookSpace,
       ColorPipelineStage::PerceptualLook, 640, false, false,
       "Three-way perceptual grading after selective color."},
      {"saturation", "Saturation", ColorOperationDomain::LookSpace,
       ColorPipelineStage::PerceptualLook, 650, false, false,
       "Final OKLab chroma scale, including chroma introduced by grading."},
      {"lutStack", "Creative LUT", ColorOperationDomain::LookSpace,
       ColorPipelineStage::CreativeLut, 700, false, false,
       "Creative LUT stack after global and local color styling."},
      {"photographicRendering", "Photographic Rendering",
       ColorOperationDomain::DisplayReferred,
       ColorPipelineStage::OutputRendering, 800, false, true,
       "Target-specific tone and gamut rendering at display handoff."},
      {"outputGamutMapping", "Output Gamut Mapping",
       ColorOperationDomain::DisplayReferred,
       ColorPipelineStage::OutputRendering, 810, false, false,
       "Destination-space containment at output handoff."},
      {"displayEncoding", "Display Encoding",
       ColorOperationDomain::DisplayReferred,
       ColorPipelineStage::OutputRendering, 820, true, false,
       "Target EOTF encoding after linear display rendering."},
      {"softProof", "Soft Proof", ColorOperationDomain::DisplayReferred,
       ColorPipelineStage::ProofSimulation, 900, false, false,
       "Printer/paper simulation after creative rendering."},
  });
  return kSemantics;
}

const ColorControlSemantics* color_control_semantics_for_key(
    std::string_view key) {
  const auto semantics = core_color_control_semantics();
  const auto match = std::find_if(
      semantics.begin(),
      semantics.end(),
      [key](const ColorControlSemantics& value) { return value.key == key; });
  return match == semantics.end() ? nullptr : &*match;
}

}  // namespace photo_editor::pipeline
