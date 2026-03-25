#include <cstdlib>
#include <iostream>

#include <QColor>
#include <QImage>

#include "../apps/desktop/src/editor_preview_controller.cpp"

namespace photo_editor::desktop {
namespace {

bool almost_equal(double left, double right, double tolerance = 1e-6) {
  return std::abs(left - right) <= tolerance;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace
}  // namespace photo_editor::desktop

int main() {
  using namespace photo_editor::desktop;

  if (!(apply_highlight_adjustment(0.82, 0.6) > 0.82)) {
    return fail("positive highlights should brighten highlight regions");
  }

  if (!(apply_highlight_adjustment(0.82, -0.6) < 0.82)) {
    return fail("negative highlights should recover highlight regions");
  }

  if (!(apply_shadow_adjustment(0.14, 0.6) > 0.14)) {
    return fail("positive shadows should lift dark regions");
  }

  if (!(apply_shadow_adjustment(0.14, -0.6) < 0.14)) {
    return fail("negative shadows should deepen dark regions");
  }

  EditState neutral_state;
  EditState curve_state;
  curve_state.curve_highlights = 60.0;
  curve_state.curve_shadows = -40.0;

  const auto neutral_high = evaluate_tone_curve(0.85, neutral_state);
  const auto curved_high = evaluate_tone_curve(0.85, curve_state);
  if (!(curved_high > neutral_high)) {
    return fail("raising the highlight curve point should brighten high values");
  }

  const auto neutral_shadow = evaluate_tone_curve(0.20, neutral_state);
  const auto curved_shadow = evaluate_tone_curve(0.20, curve_state);
  if (!(curved_shadow < neutral_shadow)) {
    return fail("lowering the shadow curve point should darken low values");
  }

  if (!(channel_weight_for_selection(0.0, QStringLiteral("Reds")) >
        channel_weight_for_selection(0.0, QStringLiteral("Blues")))) {
    return fail("red hue should match the red channel more strongly than blue");
  }

  const auto base_source = QStringLiteral("file:///tmp/preview.jpg");
  const auto revisioned_source = with_preview_revision(base_source, 3);
  if (revisioned_source == base_source || !revisioned_source.endsWith(QStringLiteral("rev=3"))) {
    return fail("preview sources should receive a render revision query");
  }

  const auto updated_revision = with_preview_revision(revisioned_source, 4);
  if (updated_revision.contains(QStringLiteral("rev=3")) || !updated_revision.endsWith(QStringLiteral("rev=4"))) {
    return fail("preview source revisions should update in place");
  }

  QImage image(1, 1, QImage::Format_ARGB32);
  image.fill(QColor::fromHslF(0.0F, 0.35F, 0.50F, 1.0F));
  EditState hsl_state;
  hsl_state.hsl_channel = QStringLiteral("Reds");
  hsl_state.hue_adjustment = 35.0;
  apply_color_adjustments(&image, hsl_state);

  const QColor adjusted(image.pixel(0, 0));
  if (almost_equal(adjusted.hslHueF(), 0.0, 0.01)) {
    return fail("hue adjustment should move matching channel hues");
  }

  return EXIT_SUCCESS;
}
