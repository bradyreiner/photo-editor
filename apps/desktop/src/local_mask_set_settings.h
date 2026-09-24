#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
#include <QVariantMap>

namespace photo_editor::desktop {

// Compile settings independently of visibility/solo filtering so presentation
// and rendering use the same post-composition refinement contract.
inline photo_editor::pipeline::LocalMaskSet compile_local_mask_set_settings(
    const QVariantMap& mask) {
  photo_editor::pipeline::LocalMaskSet set;
  const auto number = [&mask](const char* key, double fallback = 0.0) {
    return mask.value(QString::fromLatin1(key), fallback).toDouble();
  };
  set.id = mask.value("maskSetId").toString().toStdString();
  set.name = mask.value("maskSetName").toString().toStdString();
  set.enabled = mask.value("enabled", true).toBool();
  set.exposure = number("exposure");
  set.contrast = number("contrast");
  set.highlights = number("highlights");
  set.shadows = number("shadows");
  set.whites = number("whites");
  set.blacks = number("blacks");
  set.temperature = number("temperature");
  set.tint = number("tint");
  set.vibrance = number("vibrance");
  set.saturation = number("saturation");
  set.hue_shift = number("hueShift");
  set.saturation_shift = number("saturationShift");
  set.colorize_hue = number("colorizeHue");
  set.colorize_strength = number("colorizeStrength");
  set.selection_invert = mask.value("maskSetSelectionInvert", false).toBool();
  set.selection_expand = number("maskSetSelectionExpand");
  set.selection_smooth = number("maskSetSelectionSmooth");
  set.selection_fine_edge = number("maskSetSelectionFineEdge");
  set.selection_edge_contrast = number("maskSetSelectionEdgeContrast");
  set.opacity = number("maskSetOpacity", 1.0);
  set.density = number("maskSetDensity", 1.0);
  return set;
}

}  // namespace photo_editor::desktop
