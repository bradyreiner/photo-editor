#pragma once
#include <QString>
#include <optional>

namespace photo_editor::desktop {
enum class GaussianAlphaModel { Brush999, Inria99 };
inline bool valid_gaussian_alpha_model(GaussianAlphaModel model) {
  return model == GaussianAlphaModel::Brush999 || model == GaussianAlphaModel::Inria99;
}
inline float gaussian_alpha_limit(GaussianAlphaModel model) { return model == GaussianAlphaModel::Inria99 ? 0.99F : 0.999F; }
inline QString gaussian_alpha_model_name(GaussianAlphaModel model) {
  if (model == GaussianAlphaModel::Brush999) return "brush-alpha-0999-v1";
  if (model == GaussianAlphaModel::Inria99) return "inria-alpha-099-v1";
  return "invalid";
}
inline std::optional<GaussianAlphaModel> parse_gaussian_alpha_model(const QString& name) {
  if (name == "brush-alpha-0999-v1") return GaussianAlphaModel::Brush999;
  if (name == "inria-alpha-099-v1") return GaussianAlphaModel::Inria99;
  return {};
}
}  // namespace photo_editor::desktop
