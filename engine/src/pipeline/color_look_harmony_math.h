#pragma once

// Kept free of engine types so the CPU pipeline and Metal kernels can compile
// the same circular harmony operator.
#if defined(__METAL_VERSION__)
#define PHOTO_EDITOR_HARMONY_ANCHOR_POINTER constant float*
#define PHOTO_EDITOR_HARMONY_FLOOR(value) floor(value)
#define PHOTO_EDITOR_HARMONY_SIN(value) sin(value)
#else
#include <cmath>
#define PHOTO_EDITOR_HARMONY_ANCHOR_POINTER const float*
#define PHOTO_EDITOR_HARMONY_FLOOR(value) std::floor(value)
#define PHOTO_EDITOR_HARMONY_SIN(value) std::sin(value)
#endif

#if defined(__METAL_VERSION__)
constant constexpr float kPhotoEditorColorHarmonyTwoPi = 6.28318530717958647692F;
constant constexpr float kPhotoEditorColorHarmonyAttractionScale = 1.4F;
constant constexpr unsigned int kPhotoEditorColorHarmonyMaximumAnchorCount = 4U;
#else
constexpr float kPhotoEditorColorHarmonyTwoPi = 6.28318530717958647692F;
constexpr float kPhotoEditorColorHarmonyAttractionScale = 1.4F;
constexpr unsigned int kPhotoEditorColorHarmonyMaximumAnchorCount = 4U;
#endif

// Returns a continuous hue force in radians. Anchor hues are turns in the
// engine's canonical OKLab-hue contract. In the interval from the previous
// anchor to the next, let g be the interval width in turns and t be the hue's
// normalized position in that interval. The force is
//
//   F(h) = -1.4 * g * sin(2*pi*t).
//
// It is zero at every anchor and every bisector, points toward the nearer
// palette anchor, and is C2 at interval boundaries and across the hue seam.
// Its derivative is -1.4*cos(2*pi*t), so with the retained maximum edit
// coefficient alpha=.65 the output map h+alpha*F has slope in [.09, 1.91].
// At a quarter-gap, |F|=1.4*g while the old nearest-anchor delta is pi*g/2;
// the .65 coefficient cancels in their ratio, so this field retains
// 1.4*2/pi ~= 89% of the previous quarter-gap displacement.
// This gives a finite Lipschitz bound without selecting an anchor by a hard
// nearest-neighbor branch or merging nearby palette anchors.
inline float photo_editor_color_harmony_circular_force(
    float hue_radians,
    PHOTO_EDITOR_HARMONY_ANCHOR_POINTER anchor_hue_turns,
    unsigned int anchor_count) {
  const auto count = anchor_count < kPhotoEditorColorHarmonyMaximumAnchorCount
                         ? anchor_count
                         : kPhotoEditorColorHarmonyMaximumAnchorCount;
  if (count == 0U) return 0.0F;

  const auto hue_turns = hue_radians / kPhotoEditorColorHarmonyTwoPi;
  auto previous_distance = 1.0F;
  auto next_distance = 1.0F;
  for (auto index = 0U; index < count; ++index) {
    const auto anchor = anchor_hue_turns[index];
    const auto from_anchor = hue_turns - anchor;
    const auto to_anchor = anchor - hue_turns;
    const auto distance_after =
        from_anchor - PHOTO_EDITOR_HARMONY_FLOOR(from_anchor);
    const auto distance_before =
        to_anchor - PHOTO_EDITOR_HARMONY_FLOOR(to_anchor);
    if (distance_after == 0.0F || distance_before == 0.0F) return 0.0F;
    if (distance_after < previous_distance) {
      previous_distance = distance_after;
    }
    if (distance_before < next_distance) {
      next_distance = distance_before;
    }
  }

  // Equal distances identify the analytic antipodal or palette-bisector tie.
  // Returning zero avoids relying on libm's approximation of sin(pi).
  if (previous_distance == next_distance) return 0.0F;
  const auto gap = previous_distance + next_distance;
  if (gap <= 0.0F) return 0.0F;
  const auto interval_position = previous_distance / gap;
  return -kPhotoEditorColorHarmonyAttractionScale * gap *
         PHOTO_EDITOR_HARMONY_SIN(
             kPhotoEditorColorHarmonyTwoPi * interval_position);
}

#undef PHOTO_EDITOR_HARMONY_ANCHOR_POINTER
#undef PHOTO_EDITOR_HARMONY_FLOOR
#undef PHOTO_EDITOR_HARMONY_SIN
