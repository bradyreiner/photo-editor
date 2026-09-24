#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/render_tile_planner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <numbers>
#include <unordered_set>

namespace photo_editor::pipeline {
namespace {

constexpr double kMaxSigma = 4096.0;

std::vector<double> kernel(double sigma, int radius) {
  std::vector<double> weights(static_cast<std::size_t>(2 * radius + 1), 1.0);
  if (radius == 0) return weights;
  double sum = 0.0;
  for (int i = -radius; i <= radius; ++i) {
    const auto distance = static_cast<double>(i) / sigma;
    const auto weight = std::exp(-0.5 * distance * distance);
    weights[static_cast<std::size_t>(i + radius)] = weight;
    sum += weight;
  }
  for (auto& weight : weights) weight /= sum;
  return weights;
}

int sample_coordinate(int value, int size, PhotoEffectEdgeMode mode) {
  if (value >= 0 && value < size) return value;
  if (mode == PhotoEffectEdgeMode::Transparent) return -1;
  if (mode == PhotoEffectEdgeMode::Extend || size == 1) return std::clamp(value, 0, size - 1);
  // Reflect about the boundary between pixels: ... b a | a b c | c b ...
  const auto period = static_cast<std::int64_t>(size) * 2;
  auto wrapped = static_cast<std::int64_t>(value) % period;
  if (wrapped < 0) wrapped += period;
  return static_cast<int>(wrapped < size ? wrapped : period - wrapped - 1);
}

ColorPixel premultiply(ColorPixel pixel) {
  return {pixel.red * pixel.alpha, pixel.green * pixel.alpha,
      pixel.blue * pixel.alpha, pixel.alpha};
}

bool convolve(const std::vector<ColorPixel>& input, std::vector<ColorPixel>* output,
    int width, int height, std::span<const double> weights, int radius,
    PhotoEffectEdgeMode mode, bool horizontal, CancellationToken cancellation) {
  for (int y = 0; y < height; ++y) {
    if (cancellation.cancelled()) return false;
    for (int x = 0; x < width; ++x) {
      if ((x & 63) == 0 && cancellation.cancelled()) return false;
      std::array<double, 4> sum{};
      for (int offset = -radius; offset <= radius; ++offset) {
        const auto sx = horizontal ? sample_coordinate(x + offset, width, mode) : x;
        const auto sy = horizontal ? y : sample_coordinate(y + offset, height, mode);
        if (sx < 0 || sy < 0) continue;
        auto pixel = input[static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(sx)];
        if (horizontal) pixel = premultiply(pixel);
        const auto weight = weights[static_cast<std::size_t>(offset + radius)];
        sum[0] += weight * pixel.red;
        sum[1] += weight * pixel.green;
        sum[2] += weight * pixel.blue;
        sum[3] += weight * pixel.alpha;
      }
      (*output)[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x)] = {static_cast<float>(sum[0]),
          static_cast<float>(sum[1]), static_cast<float>(sum[2]), static_cast<float>(sum[3])};
    }
  }
  return true;
}

bool convolve_box(const std::vector<ColorPixel>& input, std::vector<ColorPixel>* output,
    int width, int height, int radius, PhotoEffectEdgeMode mode, bool horizontal,
    CancellationToken cancellation) {
  const auto lines = horizontal ? height : width;
  const auto length = horizontal ? width : height;
  const auto divisor = static_cast<double>(2 * radius + 1);
  for (int line = 0; line < lines; ++line) {
    if (cancellation.cancelled()) return false;
    std::array<double, 4> sum{};
    const auto accumulate = [&](int coordinate, double sign) {
      const auto mapped = sample_coordinate(coordinate, length, mode);
      if (mapped < 0) return;
      const auto x = horizontal ? mapped : line;
      const auto y = horizontal ? line : mapped;
      auto pixel = input[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
      if (horizontal) pixel = premultiply(pixel);
      sum[0] += sign * pixel.red;
      sum[1] += sign * pixel.green;
      sum[2] += sign * pixel.blue;
      sum[3] += sign * pixel.alpha;
    };
    for (int offset = -radius; offset <= radius; ++offset) accumulate(offset, 1);
    for (int coordinate = 0; coordinate < length; ++coordinate) {
      if ((coordinate & 63) == 0 && cancellation.cancelled()) return false;
      const auto x = horizontal ? coordinate : line;
      const auto y = horizontal ? line : coordinate;
      (*output)[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = {
          static_cast<float>(sum[0] / divisor), static_cast<float>(sum[1] / divisor),
          static_cast<float>(sum[2] / divisor), static_cast<float>(sum[3] / divisor)};
      accumulate(coordinate - radius, -1);
      accumulate(coordinate + radius + 1, 1);
    }
  }
  return true;
}

bool convolve_motion(const std::vector<ColorPixel>& input, std::vector<ColorPixel>* output,
    int width, int height, const PhotoEffectInstance::Motion& motion,
    const PhotoEffectRenderOptions& options) {
  const auto radians = motion.angle * std::numbers::pi / 180.0;
  const auto dx = motion.distance * std::cos(radians) * options.scale_x;
  const auto dy = motion.distance * std::sin(radians) * options.scale_y;
  const int samples = std::max(2, static_cast<int>(std::ceil(std::hypot(dx, dy))));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (options.cancellation.cancelled()) return false;
      std::array<double, 4> sum{};
      for (int tap = 0; tap < samples; ++tap) {
        if ((tap & 255) == 0 && options.cancellation.cancelled()) return false;
        const auto position = (static_cast<double>(tap) + 0.5) / samples - 0.5;
        const auto sx = x + position * dx;
        const auto sy = y + position * dy;
        const int ix = static_cast<int>(std::floor(sx));
        const int iy = static_cast<int>(std::floor(sy));
        const auto fx = sx - ix;
        const auto fy = sy - iy;
        for (int row = 0; row < 2; ++row) {
          const auto py = sample_coordinate(iy + row, height, motion.edge_mode);
          if (py < 0) continue;
          for (int column = 0; column < 2; ++column) {
            const auto px = sample_coordinate(ix + column, width, motion.edge_mode);
            if (px < 0) continue;
            const auto weight = (row == 0 ? 1.0 - fy : fy) * (column == 0 ? 1.0 - fx : fx) / samples;
            const auto pixel = premultiply(input[static_cast<std::size_t>(py) * static_cast<std::size_t>(width) + static_cast<std::size_t>(px)]);
            sum[0] += pixel.red * weight;
            sum[1] += pixel.green * weight;
            sum[2] += pixel.blue * weight;
            sum[3] += pixel.alpha * weight;
          }
        }
      }
      (*output)[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = {
          static_cast<float>(sum[0]), static_cast<float>(sum[1]), static_cast<float>(sum[2]), static_cast<float>(sum[3])};
    }
  }
  return true;
}

}  // namespace

PhotoEffectResult photo_effect_support(const PhotoEffectInstance& effect,
    const PhotoEffectRenderOptions& options, PhotoEffectSupport* support) {
  if (support == nullptr) return {PhotoEffectStatus::Invalid, "Missing effect support output."};
  *support = {};
  if (!std::isfinite(effect.amount) || effect.amount < 0.0 || effect.amount > 1.0)
    return {PhotoEffectStatus::Invalid, "Effect amount must be between zero and one."};
  if (!effect.enabled || effect.amount == 0.0) return {PhotoEffectStatus::Identity, {}};
  if (effect.type_id == "invert_linear_rgb" && effect.algorithm_version == 1) {
    if (!effect.opaque_parameters.empty())
      return {PhotoEffectStatus::Invalid, "Linear invert has no parameters."};
    return {PhotoEffectStatus::Applied, {}};
  }
  if (effect.type_id == "posterize_linear_rgb" && effect.algorithm_version == 1) {
    if (!effect.opaque_parameters.empty() || effect.posterize_levels < 2 || effect.posterize_levels > 256)
      return {PhotoEffectStatus::Invalid, "Posterize requires 2 to 256 levels and no opaque parameters."};
    return {PhotoEffectStatus::Applied, {}};
  }
  const bool box = effect.type_id == "box_blur";
  const bool motion = effect.type_id == "motion_blur";
  const bool unsharp = effect.type_id == "unsharp_mask";
  if ((!box && !motion && !unsharp && effect.type_id != "gaussian_blur") || effect.algorithm_version != 1)
    return {PhotoEffectStatus::Unsupported, "Unsupported photo effect type or algorithm version."};
  if (!effect.opaque_parameters.empty())
    return {PhotoEffectStatus::Invalid, "Known spatial effect has unexpected opaque parameters."};
  const auto sigma = box ? static_cast<double>(effect.box.radius) : motion ? effect.motion.distance
      : unsharp ? effect.unsharp.sigma : effect.gaussian.sigma;
  if (!std::isfinite(sigma) || sigma < 0.0 || sigma > kMaxSigma ||
      !std::isfinite(options.scale_x) || options.scale_x <= 0.0 ||
      !std::isfinite(options.scale_y) || options.scale_y <= 0.0 ||
      sigma * options.scale_x > kMaxSigma || sigma * options.scale_y > kMaxSigma)
    return {PhotoEffectStatus::Invalid, "Effect radius/distance or render scale is outside the supported finite range."};
  if (motion && (!std::isfinite(effect.motion.angle) || effect.motion.angle < -180 || effect.motion.angle > 180))
    return {PhotoEffectStatus::Invalid, "Motion angle must be between -180 and 180 degrees."};
  if (unsharp && (!std::isfinite(effect.unsharp.strength) || effect.unsharp.strength < 0 || effect.unsharp.strength > 5 ||
      !std::isfinite(effect.unsharp.threshold) || effect.unsharp.threshold < 0 || effect.unsharp.threshold > 1))
    return {PhotoEffectStatus::Invalid, "Unsharp strength must be 0 to 5 and threshold 0 to 1."};
  const auto edge_mode = box ? effect.box.edge_mode : motion ? effect.motion.edge_mode
      : unsharp ? effect.unsharp.edge_mode : effect.gaussian.edge_mode;
  if (edge_mode != PhotoEffectEdgeMode::Transparent &&
      edge_mode != PhotoEffectEdgeMode::Extend && edge_mode != PhotoEffectEdgeMode::Reflect)
    return {PhotoEffectStatus::Invalid, "Unknown spatial effect edge mode."};
  support->x = static_cast<int>(std::ceil((box ? 1.0 : 3.0) * sigma * options.scale_x));
  support->y = static_cast<int>(std::ceil((box ? 1.0 : 3.0) * sigma * options.scale_y));
  if (motion) {
    const auto radians = effect.motion.angle * std::numbers::pi / 180.0;
    support->x = static_cast<int>(std::ceil(0.5 * sigma * std::abs(std::cos(radians)) * options.scale_x));
    support->y = static_cast<int>(std::ceil(0.5 * sigma * std::abs(std::sin(radians)) * options.scale_y));
  }
  return {sigma == 0.0 || (unsharp && effect.unsharp.strength == 0.0)
      ? PhotoEffectStatus::Identity : PhotoEffectStatus::Applied, {}};
}

PhotoEffectResult apply_photo_effect(std::vector<ColorPixel>* pixels, int width, int height,
    const PhotoEffectInstance& effect, const PhotoEffectRenderOptions& options,
    std::span<const float> mask) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      width > std::numeric_limits<int>::max() - 12288 ||
      height > std::numeric_limits<int>::max() - 12288 ||
      static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() /
          static_cast<std::size_t>(height) ||
      pixels->size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    return {PhotoEffectStatus::Invalid, "Invalid photo effect image dimensions."};
  PhotoEffectSupport support;
  auto result = photo_effect_support(effect, options, &support);
  if (!result.ok()) return result;
  if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effect cancelled."};
  if (result.status == PhotoEffectStatus::Identity) return result;
  if ((!mask.empty() && mask.size() != pixels->size()) || (!effect.mask_ref.empty() && mask.empty()))
    return {PhotoEffectStatus::Invalid, "Effect mask is missing or has incorrect dimensions."};
  const auto count = pixels->size();
  const auto kernel_bytes = effect.type_id == "gaussian_blur" || effect.type_id == "unsharp_mask"
      ? static_cast<std::size_t>(2 * support.x + 2 * support.y + 2) * sizeof(double) : 0U;
  const std::size_t scratch_images = effect.type_id == "motion_blur" ? 1U : 2U;
  if (options.max_scratch_bytes < kernel_bytes ||
      count > (options.max_scratch_bytes - kernel_bytes) / (scratch_images * sizeof(ColorPixel)))
    return {PhotoEffectStatus::ResourceLimit, "Photo effect exceeds the scratch memory budget; render padded tiles."};
  for (std::size_t i = 0; i < count; ++i) {
    if ((i & 4095U) == 0 && options.cancellation.cancelled())
      return {PhotoEffectStatus::Cancelled, "Photo effect cancelled."};
    const auto& pixel = (*pixels)[i];
    if (!std::isfinite(pixel.red) || !std::isfinite(pixel.green) || !std::isfinite(pixel.blue) ||
        !std::isfinite(pixel.alpha) || pixel.alpha < 0.0F || pixel.alpha > 1.0F ||
        (!mask.empty() && (!std::isfinite(mask[i]) || mask[i] < 0.0F || mask[i] > 1.0F)))
      return {PhotoEffectStatus::Invalid, "Photo effect requires finite RGB and unit alpha/mask coverage."};
  }
  try {
    if (effect.type_id == "invert_linear_rgb" || effect.type_id == "posterize_linear_rgb") {
      auto output = *pixels;
      for (std::size_t i = 0; i < count; ++i) {
        if ((i & 4095U) == 0 && options.cancellation.cancelled())
          return {PhotoEffectStatus::Cancelled, "Photo effect cancelled."};
        const auto amount = effect.amount * (mask.empty() ? 1.0 : static_cast<double>(mask[i]));
        if (amount == 0 || output[i].alpha == 0) continue;
        const auto transform_channel = [amount, &effect](float value) {
          const auto steps = static_cast<double>(effect.posterize_levels - 1);
          // Extend the quantization grid outside SDR rather than clipping HDR/negative RGB.
          const auto filtered = effect.type_id == "posterize_linear_rgb"
              ? std::round(static_cast<double>(value) * steps) / steps : 1.0 - value;
          return static_cast<float>(value * (1.0 - amount) + filtered * amount);
        };
        output[i].red = transform_channel(output[i].red);
        output[i].green = transform_channel(output[i].green);
        output[i].blue = transform_channel(output[i].blue);
      }
      if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effect cancelled."};
      pixels->swap(output);
      return {PhotoEffectStatus::Applied, {}};
    }
    const bool box = effect.type_id == "box_blur";
    const bool motion = effect.type_id == "motion_blur";
    const bool unsharp = effect.type_id == "unsharp_mask";
    const auto sigma = unsharp ? effect.unsharp.sigma : effect.gaussian.sigma;
    const auto horizontal_kernel = box || motion ? std::vector<double>{}
        : kernel(sigma * options.scale_x, support.x);
    const auto vertical_kernel = box || motion ? std::vector<double>{}
        : kernel(sigma * options.scale_y, support.y);
    const auto edge_mode = box ? effect.box.edge_mode : unsharp ? effect.unsharp.edge_mode : effect.gaussian.edge_mode;
    std::vector<ColorPixel> horizontal(motion ? 0U : count);
    std::vector<ColorPixel> output(count);
    const auto convolved = motion ? convolve_motion(*pixels, &output, width, height, effect.motion, options) : box
        ? convolve_box(*pixels, &horizontal, width, height, support.x, edge_mode, true, options.cancellation) &&
          convolve_box(horizontal, &output, width, height, support.y, edge_mode, false, options.cancellation)
        : convolve(*pixels, &horizontal, width, height, horizontal_kernel, support.x,
            edge_mode, true, options.cancellation);
    if (!convolved || (!box && !motion && !convolve(horizontal, &output, width, height, vertical_kernel, support.y,
            edge_mode, false, options.cancellation)))
      return {PhotoEffectStatus::Cancelled, "Photo effect cancelled."};
    for (std::size_t i = 0; i < count; ++i) {
      if ((i & 4095U) == 0 && options.cancellation.cancelled())
        return {PhotoEffectStatus::Cancelled, "Photo effect cancelled."};
      const auto amount = effect.amount * (mask.empty() ? 1.0 : static_cast<double>(mask[i]));
      if (amount == 0.0) { output[i] = (*pixels)[i]; continue; }
      const auto base = premultiply((*pixels)[i]);
      auto& filtered = output[i];
      if (unsharp) {
        const auto original = (*pixels)[i];
        if (original.alpha == 0 || filtered.alpha <= 0) { filtered = original; continue; }
        const std::array<double, 3> detail{original.red - filtered.red / filtered.alpha,
            original.green - filtered.green / filtered.alpha, original.blue - filtered.blue / filtered.alpha};
        const auto magnitude = std::max({std::abs(detail[0]), std::abs(detail[1]), std::abs(detail[2])});
        const auto gain = magnitude >= effect.unsharp.threshold ? amount * effect.unsharp.strength : 0.0;
        filtered = {static_cast<float>(original.red + gain * detail[0]),
            static_cast<float>(original.green + gain * detail[1]),
            static_cast<float>(original.blue + gain * detail[2]), original.alpha};
        continue;
      }
      const auto alpha = std::clamp(base.alpha * (1.0 - amount) + filtered.alpha * amount, 0.0, 1.0);
      const auto mix = [&](float a, float b) {
        return alpha > 0.0 ? static_cast<float>((a * (1.0 - amount) + b * amount) / alpha) : 0.0F;
      };
      filtered = {mix(base.red, filtered.red), mix(base.green, filtered.green),
          mix(base.blue, filtered.blue), static_cast<float>(alpha)};
    }
    if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effect cancelled."};
    pixels->swap(output);
  } catch (const std::bad_alloc&) {
    return {PhotoEffectStatus::ResourceLimit, "Could not allocate photo effect scratch buffers."};
  }
  return {PhotoEffectStatus::Applied, {}};
}

PhotoEffectResult photo_effect_stack_support(std::span<const PhotoEffectInstance> effects,
    const PhotoEffectRenderOptions& options, PhotoEffectSupport* support) {
  if (support == nullptr) return {PhotoEffectStatus::Invalid, "Missing effect support output."};
  *support = {};
  std::unordered_set<std::string_view> ids;
  bool active = false;
  for (const auto& effect : effects) {
    if (effect.id.empty() || !ids.insert(effect.id).second)
      return {PhotoEffectStatus::Invalid, "Photo effects require unique stable IDs."};
    PhotoEffectSupport next;
    const auto result = photo_effect_support(effect, options, &next);
    if (!result.ok()) return result;
    active = active || result.status == PhotoEffectStatus::Applied;
    if (next.x > std::numeric_limits<int>::max() - support->x ||
        next.y > std::numeric_limits<int>::max() - support->y)
      return {PhotoEffectStatus::ResourceLimit, "Photo effect stack support exceeds coordinate limits."};
    support->x += next.x;
    support->y += next.y;
  }
  return {active ? PhotoEffectStatus::Applied : PhotoEffectStatus::Identity, {}};
}

PhotoEffectResult apply_photo_effect_stack(std::vector<ColorPixel>* pixels, int width, int height,
    std::span<const PhotoEffectInstance> effects, const PhotoEffectRenderOptions& options,
    std::span<const PhotoEffectMaskBinding> masks) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height) ||
      pixels->size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    return {PhotoEffectStatus::Invalid, "Invalid photo effect stack dimensions."};
  try {
    PhotoEffectSupport support;
    const auto result = photo_effect_stack_support(effects, options, &support);
    if (!result.ok()) return result;
    if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effect stack cancelled."};
    if (result.status == PhotoEffectStatus::Identity) return result;
    std::unordered_set<std::string_view> mask_ids;
    for (const auto& mask : masks) {
      if (mask.id.empty() || !mask_ids.insert(mask.id).second)
        return {PhotoEffectStatus::Invalid, "Effect mask bindings require unique IDs."};
    }
    if (pixels->size() > options.max_scratch_bytes / sizeof(ColorPixel))
      return {PhotoEffectStatus::ResourceLimit, "Photo effect stack exceeds scratch budget."};
    auto stage_options = options;
    stage_options.max_scratch_bytes -= pixels->size() * sizeof(ColorPixel);
    auto working = *pixels;
    for (const auto& effect : effects) {
      std::span<const float> mask;
      if (!effect.mask_ref.empty()) {
        const auto binding = std::find_if(masks.begin(), masks.end(), [&](const auto& value) {
          return value.id == effect.mask_ref;
        });
        if (binding != masks.end()) mask = binding->coverage;
      }
      const auto applied = apply_photo_effect(&working, width, height, effect, stage_options, mask);
      if (!applied.ok()) return applied;
    }
    if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effect stack cancelled."};
    pixels->swap(working);
  } catch (const std::bad_alloc&) {
    return {PhotoEffectStatus::ResourceLimit, "Could not allocate photo effect stack buffers."};
  }
  return {PhotoEffectStatus::Applied, {}};
}

PhotoEffectResult apply_photo_effect_stack_tiled(std::vector<ColorPixel>* pixels, int width, int height,
    std::span<const PhotoEffectInstance> effects, const PhotoEffectRenderOptions& options,
    std::span<const PhotoEffectMaskBinding> masks, int tile_size) {
  if (pixels == nullptr || width <= 0 || height <= 0 || tile_size <= 0 ||
      static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height) ||
      pixels->size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    return {PhotoEffectStatus::Invalid, "Invalid tiled effect dimensions."};
  try {
    PhotoEffectSupport support;
    const auto admission = photo_effect_stack_support(effects, options, &support);
    if (!admission.ok()) return admission;
    if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effects cancelled."};
    if (admission.status == PhotoEffectStatus::Identity) return admission;
    std::unordered_set<std::string_view> ids;
    for (const auto& mask : masks) {
      if (mask.id.empty() || !ids.insert(mask.id).second || mask.coverage.size() != pixels->size())
        return {PhotoEffectStatus::Invalid, "Invalid full-image effect mask binding."};
    }
    const auto budget = options.max_scratch_bytes;
    if (pixels->size() > budget / sizeof(ColorPixel))
      return {PhotoEffectStatus::ResourceLimit, "Effect output exceeds memory budget."};
    const auto output_bytes = pixels->size() * sizeof(ColorPixel);
    const auto halo = std::max(support.x, support.y);
    if (halo > std::numeric_limits<int>::max() / 4 ||
        width > std::numeric_limits<int>::max() / 2 || height > std::numeric_limits<int>::max() / 2)
      return {PhotoEffectStatus::ResourceLimit, "Effect tile geometry exceeds coordinate limits."};
    auto tile_options = options;
    tile_options.max_scratch_bytes = budget - output_bytes;
    std::vector<ColorPixel> output(pixels->size());
    // Iterate rects directly: retaining millions of tile descriptors is not necessary.
    tile_size = std::min(tile_size, std::max(width, height));
    for (int y = 0; y < height; y += std::min(tile_size, height - y)) {
      for (int x = 0; x < width; x += std::min(tile_size, width - x)) {
        if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effects cancelled."};
        const RenderPixelRect rect{x, y, std::min(tile_size, width - x), std::min(tile_size, height - y)};
        const auto padded = expand_pixel_rect(rect, halo, width, height);
        const auto count = static_cast<std::size_t>(padded.width) * static_cast<std::size_t>(padded.height);
        const auto bytes_per_pixel = sizeof(ColorPixel) + masks.size() * sizeof(float);
        if (count > tile_options.max_scratch_bytes / bytes_per_pixel)
          return {PhotoEffectStatus::ResourceLimit, "Effect tile and masks exceed memory budget."};
        auto stage_options = tile_options;
        stage_options.max_scratch_bytes -= count * bytes_per_pixel;
        std::vector<ColorPixel> tile(count);
        std::vector<std::vector<float>> mask_pixels(masks.size());
        for (auto& coverage : mask_pixels) coverage.resize(count);
        std::vector<PhotoEffectMaskBinding> bindings;
        for (std::size_t i = 0; i < masks.size(); ++i) bindings.push_back({masks[i].id, mask_pixels[i]});
        for (int row = 0; row < padded.height; ++row) {
          const auto source = static_cast<std::size_t>(padded.y + row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(padded.x);
          const auto destination = static_cast<std::size_t>(row) * static_cast<std::size_t>(padded.width);
          std::copy_n(pixels->begin() + static_cast<std::ptrdiff_t>(source), padded.width,
              tile.begin() + static_cast<std::ptrdiff_t>(destination));
          for (std::size_t i = 0; i < masks.size(); ++i)
            std::copy_n(masks[i].coverage.begin() + static_cast<std::ptrdiff_t>(source), padded.width,
                mask_pixels[i].begin() + static_cast<std::ptrdiff_t>(destination));
        }
        const auto applied = apply_photo_effect_stack(&tile, padded.width, padded.height, effects, stage_options, bindings);
        if (!applied.ok()) return applied;
        for (int row = 0; row < rect.height; ++row) {
          const auto source = static_cast<std::size_t>(rect.y - padded.y + row) * static_cast<std::size_t>(padded.width) + static_cast<std::size_t>(rect.x - padded.x);
          const auto destination = static_cast<std::size_t>(rect.y + row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(rect.x);
          std::copy_n(tile.begin() + static_cast<std::ptrdiff_t>(source), rect.width,
              output.begin() + static_cast<std::ptrdiff_t>(destination));
        }
      }
    }
    if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effects cancelled."};
    pixels->swap(output);
    return {PhotoEffectStatus::Applied, {}};
  } catch (const std::bad_alloc&) {
    return {PhotoEffectStatus::ResourceLimit, "Could not allocate effect tiles."};
  }
}

}  // namespace photo_editor::pipeline
