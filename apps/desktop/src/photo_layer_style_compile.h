#pragma once

#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/color_transform.h"
#include "photo_editor/pipeline/layer_styles.h"
#include "photo_layer_style_light.h"

#include <QVariantMap>

#include <cmath>
#include <type_traits>

namespace photo_editor::desktop {

inline void compile_photo_layer_styles(photo_editor::pipeline::PhotoLayer* target,
    const QVariantMap& layer, const photo_editor::pipeline::ColorPipelineConfig& pipeline,
    double scale_x, double scale_y) {
  using namespace photo_editor::pipeline;
  if (!layer.contains(QStringLiteral("layerStyleStack"))) return;
  const auto value = layer.value(QStringLiteral("layerStyleStack"));
  if (value.metaType().id() != QMetaType::QString) {
    target->style_error = "Layer styles must use a versioned string envelope.";
    return;
  }
  auto stack = deserialize_layer_style_stack(value.toString().toStdString(), &target->style_error);
  if (!stack) return;
  bool light_ok = true;
  const auto linked = photo_layer_style_light::is_linked(layer);
  const auto light = linked
      ? layer.value(QString::fromLatin1(
            photo_layer_style_light::kExpandedDocumentAngleField), -45.0)
            .toDouble(&light_ok)
      : layer.value(QStringLiteral("layerStyleGlobalLightAngle"), -45.0)
            .toDouble(&light_ok);
  if (!light_ok || !std::isfinite(light) || light < -3600 || light > 3600) {
    target->style_error = "Layer style light angle must be finite and between -3600 and 3600 degrees.";
    return;
  }
  const auto matrix = compile_builtin_color_transform(make_linear_srgb_color_space(),
      pipeline.working_profile.color_space).matrix;
  const auto convert = [&](LayerStyleColor& color) {
    const auto rgb = multiply_matrix3_vector(matrix, {color.red, color.green, color.blue});
    color.red = rgb[0]; color.green = rgb[1]; color.blue = rgb[2];
  };
  const auto gradient = [&](LayerStyleGradient& value) {
    for (auto& stop : value.stops) convert(stop.color);
  };
  for (auto& operation : stack->operations) {
    std::visit([&](auto& parameter) {
      using T = std::decay_t<decltype(parameter)>;
      if constexpr (std::is_same_v<T, LayerStyleStroke>) {
        std::visit([&](auto& fill) {
          using Fill = std::decay_t<decltype(fill)>;
          if constexpr (std::is_same_v<Fill, LayerStyleColor>) convert(fill);
          else gradient(fill);
        }, parameter.fill);
      } else if constexpr (std::is_same_v<T, LayerStyleGradientOverlay>) {
        gradient(parameter.gradient);
      } else if constexpr (std::is_same_v<T, LayerStyleBevelEmboss>) {
        convert(parameter.highlight); convert(parameter.shadow);
      } else if constexpr (!std::is_same_v<T, std::monostate>) {
        convert(parameter.color);
      }
    }, operation.parameters);
  }
  target->style_stack = std::move(*stack);
  target->style_global_light_angle = light;
  target->style_scale_x = scale_x;
  target->style_scale_y = scale_y;
}

}  // namespace photo_editor::desktop
