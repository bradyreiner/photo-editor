#pragma once

#include "agent_app_command_registry.h"
#include "agent_design_schemas.h"
#include "editor_preview_controller.h"

#include <QSet>

namespace photo_editor::desktop {

inline void register_agent_photo_commands(AgentAppCommandRegistry& registry,
    EditorPreviewController* editor, std::function<QString()> task_id,
    std::function<QVariantMap(const QString&, bool)> authorize = {}) {
  const QPointer<EditorPreviewController> owner(editor);
  const auto resolve_layer = [owner](const QVariant& id) -> QVariant {
    if (!owner) return {};
    const auto layers = owner->photoLayerSnapshot();
    for (int index = 0; index < layers.size(); ++index)
      if (layers.at(index).toMap().value("id") == id) return index;
    return {};
  };
  const auto resolve_layers = [resolve_layer](const QVariant& ids) -> QVariant {
    QVariantList indices;
    for (const auto& id : ids.toList()) {
      const auto index = resolve_layer(id);
      if (!index.isValid()) return {};
      indices.append(index);
    }
    return indices;
  };
  registry.registerObject("edit", editor,
      [owner, resolve_layer, resolve_layers, authorize](const QString& name, bool property, bool is_const) {
    // Only native document mutations whose complete state is in the recipe are
    // enabled here. Settings, files, gestures and unowned jobs need other owners.
    static const QSet<QString> reads{
        "photoLayerEffects", "photoLayerTypography", "photoLayerTextLayoutInfo",
        "photoLayerBounds", "availablePhotoEffects", "availablePhotoEffectLooks", "availablePhotoTextFonts",
        "availablePhotoTextFaces", "availablePhotoTextAxes", "photoTextFontWarning", "cropAspectPresets",
        "customCropAspectPresets", "curvePresetPreviewPoints", "photoLayerPresetPreviewRecipe"};
    static const QSet<QString> methods{
        "addAdjustmentLayer", "createAdjustmentLayerFromCurrentGlobalEdits", "bakePhotoLayerAdjustmentsToGlobal",
        "addFillLayer", "addPhotoShape", "setPhotoLayerShapeStyle", "setPhotoLayerShapeFill", "setPhotoLayerShapeTexture",
        "addTextLayer", "addParagraphTextLayer", "addPointTextLayer", "addPaintLayer", "addSourceLayer", "relinkSourceLayer",
        "addLayerGroup", "groupPhotoLayers", "movePhotoLayerIntoGroup", "movePhotoLayerOutOfGroup", "ungroupPhotoLayer",
        "setPhotoLayerGroupExpanded", "setPhotoLayerEnabled", "setPhotoLayerLocked", "setPhotoLayerOpacity",
        "setPhotoLayerGaussianBlur", "removePhotoLayerEffect", "movePhotoLayerEffect", "duplicatePhotoLayerEffect",
        "setPhotoLayerEffectEdgeMode", "setPhotoLayerEffectEnabled", "maskPhotoLayerEffectFromSelection", "clearPhotoLayerEffectMask",
        "addPhotoLayerInvert", "setPhotoLayerPosterize", "setPhotoLayerBoxBlur", "setPhotoLayerMotionBlur",
        "setPhotoLayerDraggedShutter", "setPhotoLayerUnsharpMask", "setPhotoLayerOpticalDiffusion", "setPhotoLayerImperfectPrint",
        "applyPhotoLayerEffectLook", "setPhotoLayerEffectAmount", "resetPhotoLayerEffect", "setPhotoLayerBlendMode",
        "setPhotoLayerAdjustmentValue", "setPhotoLayerAdjustmentString", "setPhotoLayerAdjustmentBool",
        "setPhotoLayerCurvePoints", "setPhotoLayerLutStack", "resetPhotoLayerAdjustmentGroup", "resetPhotoLayerAdjustments",
        "pasteGlobalAdjustmentsToPhotoLayer", "syncPhotoLayerAdjustments", "setPhotoLayerClippingMask", "setPhotoLayerMaskEnabled",
        "linkActiveSelectionToPhotoLayer", "setPhotoLayerMaskOpacity", "setPhotoLayerMaskDensity", "setPhotoLayerText",
        "setPhotoLayerTypography", "setPhotoLayerTextPathFromActiveSelection", "clearPhotoLayerTextPath", "setPhotoLayerTextStyle",
        "setPhotoLayerTextBox", "renamePhotoLayer", "setPhotoLayerTransform", "setPhotoLayerPerspective", "snapPhotoLayerToCanvas",
        "alignPhotoLayers", "distributePhotoLayers", "duplicatePhotoLayer", "duplicatePhotoLayerWithMasks",
        "duplicatePhotoLayerWithoutMasks", "movePhotoLayer", "deletePhotoLayer", "applyCurvePreset"};
    static const QSet<QString> properties{
        "exposure", "contrast", "highlights", "shadows", "whites", "blacks", "brightness", "hdrLocalToneMapping",
        "filmicShoulder", "filmicToe", "adaptiveContrast", "levelsBlackPoint", "levelsGamma", "levelsWhitePoint",
        "zoneDeepShadows", "zoneShadows", "zoneMidtones", "zoneLights", "zoneHighlights", "temperature", "tint",
        "texture", "clarity", "dehaze", "sharpening", "sharpenRadius", "sharpenDetail", "sharpenMasking",
        "luminanceNr", "colorNr", "lensDistortion", "lensVignette", "chromaticAberration", "perspectiveVertical",
        "perspectiveHorizontal", "transformRotation", "transformScale", "transformSkewX", "transformSkewY",
        "vibrance", "saturation", "grainAmount", "grainSize", "grainRoughness", "vignetteAmount", "vignetteMidpoint",
        "vignetteRoundness", "vignetteFeather", "vignetteHighlights"};
    AgentAppCommandRegistry::Policy policy;
    policy.available = property ? properties.contains(name) : is_const ? reads.contains(name) : methods.contains(name);
    policy.resource = "photo-recipe";
    policy.reversal = "exact-task-baseline";
    if (!is_const && authorize) policy.authorize = [authorize, name, property]() { return authorize(name, property); };
    if (name.startsWith("export") || name.startsWith("start") || name.startsWith("save") || name.startsWith("import"))
      policy.reason = "This command also changes files or starts work whose task owner is not registered yet.";
    else if (!policy.available)
      policy.reason = "This control needs a separate settings, selection, gesture or asynchronous task adapter.";
    for (const auto& pair : {qMakePair(QStringLiteral("index"), QStringLiteral("layerId")),
        qMakePair(QStringLiteral("source_index"), QStringLiteral("sourceLayerId")),
        qMakePair(QStringLiteral("group_index"), QStringLiteral("groupId"))})
      policy.arguments.insert(pair.first, {pair.second, {{"type", "string"}}, resolve_layer});
    for (const auto& pair : {qMakePair(QStringLiteral("indices"), QStringLiteral("layerIds")),
        qMakePair(QStringLiteral("target_indices"), QStringLiteral("targetLayerIds"))})
      policy.arguments.insert(pair.first, {pair.second, {{"type", "array"}, {"items", QVariantMap{{"type", "string"}}}}, resolve_layers});
    if (name == "setPhotoLayerShapeFill")
      policy.arguments.insert("options", {"options", agent_shape_fill_schema(), {}});
    if (name == "setPhotoLayerTypography")
      policy.arguments.insert("changes", {"changes", agent_typography_schema(), {}});
    if (name == "addPhotoShape")
      policy.arguments.insert("type", {"type", agent_string_enum({"rectangle", "rounded-rectangle", "ellipse", "line", "arrow"}), {}});
    if (name == "setPhotoLayerTransform") {
      policy.arguments.insert("scale", {"scale", agent_number_schema(0.05, 32, "Uniform scale multiplier."), {}});
      policy.arguments.insert("rotation", {"rotation", agent_number_schema(-3600, 3600, "Clockwise angle in degrees."), {}});
    }
    if (name == "setPhotoLayerTransform" || name == "addPhotoShape" ||
        name == "addParagraphTextLayer" || name == "addPointTextLayer" || name == "setPhotoLayerTextBox") {
      for (const auto& key : {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("start_x"), QStringLiteral("start_y"),
          QStringLiteral("end_x"), QStringLiteral("end_y"), QStringLiteral("width"), QStringLiteral("height"),
          QStringLiteral("canvas_width"), QStringLiteral("canvas_height"), QStringLiteral("pixel_width"), QStringLiteral("pixel_height")})
        policy.arguments.insert(key, {key, {{"type", "integer"}, {"description", "Canvas pixel coordinate/dimension; origin is the top-left. Supply actual canvas dimensions from inspection."}}, {}});
    }
    static const QHash<QString, QString> verified_setters{
        {"setPhotoLayerOpacity", "opacity"}, {"setPhotoLayerEnabled", "enabled"},
        {"setPhotoLayerLocked", "locked"}, {"setPhotoLayerBlendMode", "blendMode"},
        {"setPhotoLayerText", "text"}};
    if (verified_setters.contains(name)) {
      const auto field = verified_setters.value(name);
      const auto value_name = name == "setPhotoLayerBlendMode" ? QStringLiteral("blend_mode") : field;
      policy.confirm_satisfied = [owner, field, value_name](const QVariantMap& values) {
        if (!owner) return false;
        for (const auto& item : owner->photoLayerSnapshot()) {
          const auto layer = item.toMap();
          if (layer.value("id") != values.value("layerId")) continue;
          return layer.value(field) == values.value(value_name);
        }
        return false;
      };
    }
    if (name == "setPhotoLayerTransform" || name == "setPhotoLayerPerspective") {
      policy.confirm_satisfied = [owner, name](const QVariantMap& values) {
        if (!owner) return false;
        for (const auto& item : owner->photoLayerSnapshot()) {
          const auto layer = item.toMap();
          if (layer.value("id") != values.value("layerId") || layer.value("locked").toBool()) continue;
          if (name == "setPhotoLayerTransform")
            return layer.value("x").toInt() == values.value("x").toInt() &&
                layer.value("y").toInt() == values.value("y").toInt() &&
                qFuzzyCompare(layer.value("scale", 1.0).toDouble(), values.value("scale").toDouble()) &&
                layer.value("rotation", 0.0).toDouble() == values.value("rotation").toDouble();
          return layer.value("kind") == "text" &&
              layer.value("textPerspectiveHorizontal", 0.0).toDouble() == values.value("horizontal").toDouble() &&
              layer.value("textPerspectiveVertical", 0.0).toDouble() == values.value("vertical").toDouble();
        }
        return false;
      };
    }
    return policy;
  }, [owner, task_id = std::move(task_id)](const QString& command_id, const QVariantMap& values,
      const AgentAppCommandRegistry::Invocation& invoke) -> QVariantMap {
    const auto id = task_id ? task_id() : QString{};
    if (!owner || id.isEmpty()) return {{"ok", false}, {"error", "Start an owned agent draft before invoking editing commands."}};
    return owner->runAgentTaskNativeCommand(id, command_id, values, invoke);
  });
}

}  // namespace photo_editor::desktop
