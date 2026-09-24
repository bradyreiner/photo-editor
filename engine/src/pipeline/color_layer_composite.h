#pragma once
#include "photo_editor/pipeline/color_adjustments.h"
#include <algorithm>
#include <cmath>

namespace photo_editor::pipeline {
// ColorPixel stores straight (unassociated) RGB. Blend only the overlap, then
// source-over the premultiplied contributions and return straight RGB again.
// https://www.w3.org/TR/compositing-1/#generalformula
template <typename Scalar>
ColorPixel composite_layer_source_over(const ColorPixel& base, const ColorPixel& source,
    const ColorPixel& blended, Scalar alpha) {
  if (alpha <= Scalar{0}) return base;
  const auto ba = std::clamp(static_cast<Scalar>(base.alpha), Scalar{0}, Scalar{1});
  if (ba <= Scalar{0}) return {source.red, source.green, source.blue, static_cast<float>(alpha)};
  const auto output_alpha = alpha + ba * (Scalar{1} - alpha);
  const auto channel = [&](float background, float foreground, float overlap) {
    // Preserve the established opaque-backdrop arithmetic exactly.
    if (ba >= Scalar{1}) return static_cast<float>(std::lerp(
        static_cast<Scalar>(background), static_cast<Scalar>(overlap), alpha));
    const auto blended_source = std::lerp(static_cast<Scalar>(foreground), static_cast<Scalar>(overlap), ba);
    return static_cast<float>((alpha * blended_source +
        (Scalar{1} - alpha) * ba * static_cast<Scalar>(background)) / output_alpha);
  };
  return {channel(base.red, source.red, blended.red), channel(base.green, source.green, blended.green),
      channel(base.blue, source.blue, blended.blue), static_cast<float>(output_alpha)};
}
}
