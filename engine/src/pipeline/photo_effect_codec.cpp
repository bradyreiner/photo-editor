#include "photo_editor/pipeline/photo_effect_codec.h"
#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/photo_effect_registry.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_set>

namespace photo_editor::pipeline {
namespace {
constexpr std::string_view kMagic = "photo-editor-effects:v1";
constexpr std::size_t kMaxEffects = 4096;
constexpr std::size_t kMaxBytes = 4U * 1024U * 1024U;
constexpr std::size_t kMaxFieldBytes = 1024U * 1024U;

void fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
}

bool known(const PhotoEffectInstance& effect) {
  return (effect.type_id == "gaussian_blur" || effect.type_id == "box_blur") && effect.algorithm_version == 1;
}

bool valid(const PhotoEffectInstance& effect, std::string* error) {
  if (effect.id.empty() || effect.id.size() > kMaxFieldBytes || effect.type_id.empty() ||
      effect.type_id.size() > kMaxFieldBytes || effect.mask_ref.size() > kMaxFieldBytes ||
      effect.opaque_parameters.size() > kMaxFieldBytes || effect.algorithm_version == 0 ||
      !std::isfinite(effect.amount) || effect.amount < 0 || effect.amount > 1 ||
      (effect.input_scope != PhotoEffectInputScope::LayerContent &&
       effect.input_scope != PhotoEffectInputScope::CompositeBelow)) {
    fail(error, "Invalid effect identity, amount, scope or payload size.");
    return false;
  }
  if (find_photo_effect(effect.type_id, effect.algorithm_version)) {
    auto enabled = effect;
    enabled.enabled = true;
    enabled.amount = 1;
    PhotoEffectSupport support;
    const auto result = photo_effect_support(enabled, {}, &support);
    if (!result.ok()) { fail(error, result.error); return false; }
  }
  return true;
}

bool exhausted(std::istream& stream) {
  stream >> std::ws;
  return stream.eof();
}
}  // namespace

std::optional<std::string> serialize_photo_effect_stack(
    std::span<const PhotoEffectInstance> effects, std::string* error) {
  if (error != nullptr) error->clear();
  if (effects.size() > kMaxEffects) { fail(error, "Too many photo effects."); return std::nullopt; }
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<double>::max_digits10);
  stream << kMagic << ' ' << effects.size() << '\n';
  std::unordered_set<std::string_view> ids;
  for (const auto& effect : effects) {
    if (!valid(effect, error)) return std::nullopt;
    if (!ids.insert(effect.id).second) { fail(error, "Duplicate photo effect ID."); return std::nullopt; }
    auto parameters = effect.opaque_parameters;
    if (known(effect)) {
      std::ostringstream values;
      values.imbue(std::locale::classic());
      const auto radius = effect.type_id == "box_blur"
          ? static_cast<double>(effect.box.radius) : effect.gaussian.sigma;
      values << std::setprecision(std::numeric_limits<double>::max_digits10)
             << (radius == 0 ? 0.0 : radius) << ' '
             << static_cast<unsigned>(effect.type_id == "box_blur" ? effect.box.edge_mode : effect.gaussian.edge_mode);
      parameters = values.str();
    } else if ((effect.type_id == "motion_blur" || effect.type_id == "unsharp_mask") && effect.algorithm_version == 1) {
      std::ostringstream values;
      values.imbue(std::locale::classic());
      values << std::setprecision(std::numeric_limits<double>::max_digits10);
      if (effect.type_id == "motion_blur")
        values << effect.motion.distance << ' ' << effect.motion.angle << ' ' << static_cast<unsigned>(effect.motion.edge_mode);
      else
        values << effect.unsharp.sigma << ' ' << effect.unsharp.strength << ' ' << effect.unsharp.threshold
               << ' ' << static_cast<unsigned>(effect.unsharp.edge_mode);
      parameters = values.str();
    } else if (effect.type_id == "posterize_linear_rgb" && effect.algorithm_version == 1) {
      parameters = std::to_string(effect.posterize_levels);
    }
    stream << std::quoted(effect.id) << ' ' << std::quoted(effect.type_id) << ' '
           << effect.algorithm_version << ' ' << (effect.enabled ? 1 : 0) << ' '
           << (effect.amount == 0 ? 0.0 : effect.amount) << ' '
           << static_cast<unsigned>(effect.input_scope) << ' '
           << std::quoted(effect.mask_ref) << ' ' << std::quoted(parameters) << '\n';
    if (stream.tellp() > static_cast<std::streamoff>(kMaxBytes)) {
      fail(error, "Photo effect payload exceeds size limit."); return std::nullopt;
    }
  }
  return stream.str();
}

std::optional<std::vector<PhotoEffectInstance>> deserialize_photo_effect_stack(
    std::string_view payload, std::string* error) {
  if (error != nullptr) error->clear();
  if (payload.size() > kMaxBytes) { fail(error, "Photo effect payload exceeds size limit."); return std::nullopt; }
  std::istringstream stream{std::string(payload)};
  stream.imbue(std::locale::classic());
  std::string magic;
  std::size_t count = 0;
  if (!(stream >> magic >> count) || magic != kMagic || count > kMaxEffects) {
    fail(error, "Unsupported or invalid photo effect envelope."); return std::nullopt;
  }
  std::vector<PhotoEffectInstance> effects;
  effects.reserve(count);
  std::unordered_set<std::string> ids;
  for (std::size_t index = 0; index < count; ++index) {
    PhotoEffectInstance effect;
    unsigned enabled = 0, scope = 0;
    std::int64_t algorithm_version = 0;
    std::string parameters;
    if (!(stream >> std::quoted(effect.id) >> std::quoted(effect.type_id) >> algorithm_version
          >> enabled >> effect.amount >> scope >> std::quoted(effect.mask_ref) >> std::quoted(parameters)) ||
        algorithm_version <= 0 || algorithm_version > std::numeric_limits<std::uint32_t>::max() ||
        enabled > 1 || scope > 1 || parameters.size() > kMaxFieldBytes) {
      fail(error, "Malformed photo effect entry."); return std::nullopt;
    }
    effect.algorithm_version = static_cast<std::uint32_t>(algorithm_version);
    effect.enabled = enabled == 1;
    effect.input_scope = static_cast<PhotoEffectInputScope>(scope);
    if (known(effect)) {
      std::istringstream values{parameters};
      values.imbue(std::locale::classic());
      unsigned edge = 0;
      if (!(values >> effect.gaussian.sigma >> edge) || edge > 2 || !exhausted(values)) {
        fail(error, "Malformed Gaussian effect parameters."); return std::nullopt;
      }
      effect.gaussian.edge_mode = static_cast<PhotoEffectEdgeMode>(edge);
      if (effect.type_id == "box_blur") {
        if (!std::isfinite(effect.gaussian.sigma) || effect.gaussian.sigma < 0 ||
            effect.gaussian.sigma > 4096 || std::floor(effect.gaussian.sigma) != effect.gaussian.sigma) {
          fail(error, "Box radius must be an integer from 0 to 4096."); return std::nullopt;
        }
        effect.box.radius = static_cast<int>(effect.gaussian.sigma);
        effect.box.edge_mode = static_cast<PhotoEffectEdgeMode>(edge);
        effect.gaussian = {};
      }
    } else if ((effect.type_id == "motion_blur" || effect.type_id == "unsharp_mask") && effect.algorithm_version == 1) {
      std::istringstream values{parameters};
      values.imbue(std::locale::classic());
      unsigned edge = 0;
      const bool parsed = effect.type_id == "motion_blur"
          ? static_cast<bool>(values >> effect.motion.distance >> effect.motion.angle >> edge)
          : static_cast<bool>(values >> effect.unsharp.sigma >> effect.unsharp.strength >> effect.unsharp.threshold >> edge);
      if (!parsed || edge > 2 || !exhausted(values)) {
        fail(error, "Malformed motion blur or unsharp mask parameters."); return std::nullopt;
      }
      if (effect.type_id == "motion_blur") effect.motion.edge_mode = static_cast<PhotoEffectEdgeMode>(edge);
      else effect.unsharp.edge_mode = static_cast<PhotoEffectEdgeMode>(edge);
    } else if (effect.type_id == "posterize_linear_rgb" && effect.algorithm_version == 1) {
      std::istringstream values{parameters};
      values.imbue(std::locale::classic());
      if (!(values >> effect.posterize_levels) || !exhausted(values)) {
        fail(error, "Malformed posterize levels."); return std::nullopt;
      }
    } else {
      effect.opaque_parameters = std::move(parameters);
    }
    if (!valid(effect, error)) return std::nullopt;
    if (!ids.insert(effect.id).second) { fail(error, "Duplicate photo effect ID."); return std::nullopt; }
    effects.push_back(std::move(effect));
  }
  if (!exhausted(stream)) { fail(error, "Trailing data after photo effect stack."); return std::nullopt; }
  return effects;
}

}  // namespace photo_editor::pipeline
