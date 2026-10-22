#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/render_tile_planner.h"
#include "photo_editor/pipeline/photo_effect_registry.h"
#include "photo_editor/pipeline/film_grain_profiles.h"
#include "color_adjustment_scheduler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <new>
#include <numbers>
#include <unordered_set>
#include <utility>

namespace photo_editor::pipeline {
namespace {

constexpr double kMaxSigma = 4096.0;
constexpr double kMinDocumentScale = 1.0 / 4096.0;

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

std::uint64_t print_hash(std::uint32_t seed, std::int64_t x, std::int64_t y) {
  auto value = static_cast<std::uint64_t>(seed) ^
      (static_cast<std::uint64_t>(x) * 0x9e3779b185ebca87ULL) ^
      (static_cast<std::uint64_t>(y) * 0xc2b2ae3d27d4eb4fULL);
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  value ^= value >> 31U;
  return value;
}

double print_noise(double x, double y, double scale, std::uint32_t seed) {
  const auto fx = x / scale;
  const auto fy = y / scale;
  constexpr double max_lattice = 9.0e18;
  if (!std::isfinite(fx) || !std::isfinite(fy) || std::abs(fx) > max_lattice || std::abs(fy) > max_lattice)
    return 0.0;
  const auto ix = static_cast<std::int64_t>(std::floor(fx));
  const auto iy = static_cast<std::int64_t>(std::floor(fy));
  const auto tx = fx - static_cast<double>(ix);
  const auto ty = fy - static_cast<double>(iy);
  const auto smooth = [](double value) { return value * value * (3.0 - 2.0 * value); };
  const auto lattice = [seed](std::int64_t lx, std::int64_t ly) {
    constexpr double inverse = 1.0 / 9007199254740992.0;
    return static_cast<double>(print_hash(seed, lx, ly) >> 11U) * inverse;
  };
  const auto top = std::lerp(lattice(ix, iy), lattice(ix + 1, iy), smooth(tx));
  const auto bottom = std::lerp(lattice(ix, iy + 1), lattice(ix + 1, iy + 1), smooth(tx));
  return std::lerp(top, bottom, smooth(ty)) * 2.0 - 1.0;
}

double grain_noise(double x, double y, double size, std::uint32_t seed) {
  const auto fine = print_noise(x + size * 0.37, y + size * 0.19, size, seed);
  const auto broad = print_noise(x - size * 0.23, y + size * 0.41, size * 2.1,
      seed ^ 0xa511e9b3U);
  return 0.72 * fine + 0.28 * broad;
}

// Jittered compact particles avoid the rectangular cells of interpolated noise.
// Signed density fluctuations have zero ensemble mean; independent fine and
// clustered layers model silver clumps / overlapping dye clouds.
double grain_particles(double x, double y, double size, std::uint32_t seed) {
  x /= size;
  y /= size;
  if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 9.0e18 || std::abs(y) > 9.0e18)
    return 0.0;
  const auto cell_x = static_cast<std::int64_t>(std::floor(x));
  const auto cell_y = static_cast<std::int64_t>(std::floor(y));
  double sum = 0.0;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      const auto hash = print_hash(seed, cell_x + dx, cell_y + dy);
      const auto unit = [hash](unsigned int shift) {
        return static_cast<double>((hash >> shift) & 0xffffU) / 65535.0;
      };
      const auto px = static_cast<double>(cell_x + dx) + unit(0);
      const auto py = static_cast<double>(cell_y + dy) + unit(16);
      const auto radius = 0.38 + 0.43 * unit(32);
      const auto distance = ((x - px) * (x - px) + (y - py) * (y - py)) / (radius * radius);
      const auto coverage = std::max(0.0, 1.0 - distance);
      sum += coverage * coverage * (unit(48) * 2.0 - 1.0);
    }
  }
  return sum * 2.4;
}

double stock_grain_noise(double x, double y, double size, std::uint32_t seed,
    double roughness, const FilmGrainProfile& profile) {
  const auto fine_weight = std::clamp(profile.fine_weight - (roughness - profile.roughness) * 0.4, 0.15, 0.98);
  const auto primary = grain_particles(x, y, size * (0.55 + 0.4 * (1.0 - fine_weight)), seed);
  const auto micro = grain_particles(x + 5.37, y + 19.13, size * 0.35, seed ^ 0x68bc21ebU);
  const auto cluster = grain_particles(x + 13.17, y - 7.83,
      size * profile.cluster_scale, seed ^ 0xa511e9b3U);
  // Broad clumps modulate fine particles; they must not become smooth blobs.
  const auto signal = 0.78 * primary + 0.22 * micro + (1.0 - fine_weight) * 0.15 * cluster;
  return signal * (1.0 + roughness * 0.45 * std::abs(cluster));
}

double smoothstep(double edge0, double edge1, double value) {
  if (edge1 <= edge0) return value < edge0 ? 0.0 : 1.0;
  const auto t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

std::array<double, 3> hue_tint(double hue_degrees,
    const std::array<double, 3>& weights) {
  const auto hue = std::fmod(hue_degrees, 360.0) / 60.0;
  const auto chroma = 1.0;
  const auto x = chroma * (1.0 - std::abs(std::fmod(hue, 2.0) - 1.0));
  std::array<double, 3> vivid{};
  if (hue < 1.0) vivid = {chroma, x, 0.0};
  else if (hue < 2.0) vivid = {x, chroma, 0.0};
  else if (hue < 3.0) vivid = {0.0, chroma, x};
  else if (hue < 4.0) vivid = {0.0, x, chroma};
  else if (hue < 5.0) vivid = {x, 0.0, chroma};
  else vivid = {chroma, 0.0, x};
  // Keep tint hue while leaving enough neutral light for a restrained grade.
  constexpr double saturation = 0.72;
  std::array<double, 3> tint{};
  for (std::size_t channel = 0; channel < 3; ++channel)
    tint[channel] = std::lerp(1.0, vivid[channel], saturation);
  const auto luminance = weights[0] * tint[0] + weights[1] * tint[1] + weights[2] * tint[2];
  for (auto& value : tint) value /= luminance;
  return tint;
}

double sample_print_channel(const std::vector<ColorPixel>& pixels, int width, int height,
    double x, double y, int channel) {
  const auto x0 = static_cast<int>(std::floor(x));
  const auto y0 = static_cast<int>(std::floor(y));
  const auto fx = x - static_cast<double>(x0);
  const auto fy = y - static_cast<double>(y0);
  const auto read = [&](int sx, int sy) {
    const auto px = std::clamp(sx, 0, width - 1);
    const auto py = std::clamp(sy, 0, height - 1);
    const auto& pixel = pixels[static_cast<std::size_t>(py) * static_cast<std::size_t>(width) +
        static_cast<std::size_t>(px)];
    const auto value = channel == 0 ? pixel.red : channel == 1 ? pixel.green : pixel.blue;
    return std::pair{static_cast<double>(value) * pixel.alpha, static_cast<double>(pixel.alpha)};
  };
  const auto a = read(x0, y0);
  const auto b = read(x0 + 1, y0);
  const auto c = read(x0, y0 + 1);
  const auto d = read(x0 + 1, y0 + 1);
  const auto interpolate = [fx, fy](double av, double bv, double cv, double dv) {
    return std::lerp(std::lerp(av, bv, fx), std::lerp(cv, dv, fx), fy);
  };
  const auto alpha = interpolate(a.second, b.second, c.second, d.second);
  if (alpha <= 1e-12) return 0.0;
  return interpolate(a.first, b.first, c.first, d.first) / alpha;
}

ColorPixel premultiply(ColorPixel pixel) {
  return {pixel.red * pixel.alpha, pixel.green * pixel.alpha,
      pixel.blue * pixel.alpha, pixel.alpha};
}

template <typename Work>
bool run_effect_lines(int lines, std::uint64_t work_per_line,
    CancellationToken cancellation, const Work& work) {
  if (cancellation.cancelled()) return false;
  // Avoid pool admission for tiny images. Estimate work without multiplying
  // image dimensions by a potentially large kernel radius (overflow).
  constexpr std::uint64_t kMinimumParallelWork = 65536U;
  if (lines < 16 || work_per_line <
      (kMinimumParallelWork + static_cast<std::uint64_t>(lines) - 1U) /
          static_cast<std::uint64_t>(lines)) {
    for (int line = 0; line < lines; ++line) {
      if (cancellation.cancelled()) return false;
      work(line);
    }
  } else {
    // The shared scheduler joins the batch before returning and executes nested
    // worker calls serially. Each line writes disjoint preallocated output;
    // sample accumulation order and scratch-image count stay unchanged.
    run_color_adjustment_parallel_for(lines, [&](int line) {
      if (!cancellation.cancelled()) work(line);
    });
  }
  return !cancellation.cancelled();
}

template <typename Work>
PhotoEffectStatus run_effect_pixels(int width, int height, std::uint64_t work_per_pixel,
    CancellationToken cancellation, const Work& work) {
  std::atomic_bool valid{true};
  const auto completed = run_effect_lines(height,
      static_cast<std::uint64_t>(width) * work_per_pixel, cancellation, [&](int y) {
    if (!valid.load(std::memory_order_relaxed)) return;
    const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    for (int x = 0; x < width; ++x) {
      if ((x & 63) == 0 && cancellation.cancelled()) return;
      if (!work(row + static_cast<std::size_t>(x))) {
        valid.store(false, std::memory_order_relaxed);
        return;
      }
    }
  });
  if (!completed) return PhotoEffectStatus::Cancelled;
  return valid.load(std::memory_order_relaxed) ? PhotoEffectStatus::Applied : PhotoEffectStatus::Invalid;
}

template <typename Pixel>
void resize_effect_tile_buffer(std::vector<Pixel>* buffer, std::size_t count) {
  if (count > buffer->capacity()) {
    // Free the old allocation before growing, and avoid geometric over-allocation.
    // Equal/smaller tiles reuse their allocation; callers budget actual capacity.
    std::vector<Pixel>{}.swap(*buffer);
  }
  buffer->resize(count);
}

bool convolve(const std::vector<ColorPixel>& input, std::vector<ColorPixel>* output,
    int width, int height, std::span<const double> weights, int radius,
    PhotoEffectEdgeMode mode, bool horizontal, CancellationToken cancellation,
    const RenderPixelRect& region) {
  return run_effect_lines(region.height,
      static_cast<std::uint64_t>(region.width) * static_cast<std::uint64_t>(2 * radius + 1),
      cancellation, [&](int row_index) {
    const auto y = region.y + row_index;
    const auto end_x = region.x + region.width;
    // Interleave independent pixels so each retains the original tap order,
    // while the CPU can overlap their double-precision accumulation chains.
    constexpr int kPixelsPerBlock = 4;
    for (int x = region.x; x < end_x;) {
      if ((x & 63) == 0 && cancellation.cancelled()) return;
      const int lanes = std::min(kPixelsPerBlock, end_x - x);
      std::array<std::array<double, 4>, kPixelsPerBlock> sums{};
      for (int offset = -radius; offset <= radius; ++offset) {
        if (((offset + radius) & 255) == 0 && cancellation.cancelled()) return;
        const auto sy = horizontal ? y : sample_coordinate(y + offset, height, mode);
        if (sy < 0) continue;
        const auto row = static_cast<std::size_t>(sy) * static_cast<std::size_t>(width);
        const auto weight = weights[static_cast<std::size_t>(offset + radius)];
        for (int lane = 0; lane < lanes; ++lane) {
          const auto sx = horizontal ? sample_coordinate(x + lane + offset, width, mode) : x + lane;
          if (sx < 0) continue;
          const auto pixel = input[row + static_cast<std::size_t>(sx)];
          auto& sum = sums[static_cast<std::size_t>(lane)];
          sum[0] += weight * pixel.red;
          sum[1] += weight * pixel.green;
          sum[2] += weight * pixel.blue;
          sum[3] += weight * pixel.alpha;
        }
      }
      for (int lane = 0; lane < lanes; ++lane) {
        const auto& sum = sums[static_cast<std::size_t>(lane)];
        (*output)[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x + lane)] = {static_cast<float>(sum[0]),
            static_cast<float>(sum[1]), static_cast<float>(sum[2]), static_cast<float>(sum[3])};
      }
      x += lanes;
    }
  });
}

bool convolve_box(const std::vector<ColorPixel>& input, std::vector<ColorPixel>* output,
    int width, int height, int radius, PhotoEffectEdgeMode mode, bool horizontal,
    CancellationToken cancellation) {
  const auto lines = horizontal ? height : width;
  const auto length = horizontal ? width : height;
  const auto divisor = static_cast<double>(2 * radius + 1);
  return run_effect_lines(lines,
      static_cast<std::uint64_t>(length) * 2U + static_cast<std::uint64_t>(2 * radius + 1),
      cancellation, [&](int line) {
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
    for (int offset = -radius; offset <= radius; ++offset) {
      if (((offset + radius) & 255) == 0 && cancellation.cancelled()) return;
      accumulate(offset, 1);
    }
    for (int coordinate = 0; coordinate < length; ++coordinate) {
      if ((coordinate & 63) == 0 && cancellation.cancelled()) return;
      const auto x = horizontal ? coordinate : line;
      const auto y = horizontal ? line : coordinate;
      (*output)[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = {
          static_cast<float>(sum[0] / divisor), static_cast<float>(sum[1] / divisor),
          static_cast<float>(sum[2] / divisor), static_cast<float>(sum[3] / divisor)};
      accumulate(coordinate - radius, -1);
      accumulate(coordinate + radius + 1, 1);
    }
  });
}

bool convolve_motion(const std::vector<ColorPixel>& input, std::vector<ColorPixel>* output,
    int width, int height, const PhotoEffectInstance::Motion& motion,
    const PhotoEffectRenderOptions& options) {
  const auto radians = motion.angle * std::numbers::pi / 180.0;
  const auto dx = motion.distance * std::cos(radians) * options.scale_x;
  const auto dy = motion.distance * std::sin(radians) * options.scale_y;
  const int samples = std::max(2, static_cast<int>(std::ceil(std::hypot(dx, dy))));
  return run_effect_lines(height,
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(samples) * 4U,
      options.cancellation, [&](int y) {
    for (int x = 0; x < width; ++x) {
      if (options.cancellation.cancelled()) return;
      std::array<double, 4> sum{};
      for (int tap = 0; tap < samples; ++tap) {
        if ((tap & 255) == 0 && options.cancellation.cancelled()) return;
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
  });
}

}  // namespace

PhotoEffectResult photo_effect_support(const PhotoEffectInstance& effect,
    const PhotoEffectRenderOptions& options, PhotoEffectSupport* support) {
  if (support == nullptr) return {PhotoEffectStatus::Invalid, "Missing effect support output."};
  *support = {};
  if (!std::isfinite(effect.amount) || effect.amount < 0.0 || effect.amount > 1.0)
    return {PhotoEffectStatus::Invalid, "Effect amount must be between zero and one."};
  if (!effect.enabled || effect.amount == 0.0) return {PhotoEffectStatus::Identity, {}};
  double luminance_sum = 0.0;
  for (const auto weight : options.luminance_weights) {
    if (!std::isfinite(weight) || std::abs(weight) > 4.0)
      return {PhotoEffectStatus::Invalid, "Invalid working-gamut luminance coefficients."};
    luminance_sum += weight;
  }
  if (std::abs(luminance_sum - 1.0) > 1e-4)
    return {PhotoEffectStatus::Invalid, "Working-gamut luminance coefficients must sum to one."};
  if (effect.algorithm_version == 1 && is_optical_photo_effect(effect.type_id))
    return optical_photo_effect_support(effect.type_id, effect.optical, options, support);
  if (effect.algorithm_version == 1 && is_artistic_photo_effect(effect.type_id))
    return artistic_photo_effect_support(effect.type_id, effect.artistic, options, support);
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
  if (effect.type_id == "film_grain" && effect.algorithm_version == 1) {
    const auto& grain = effect.grain;
    if (!effect.opaque_parameters.empty() || !std::isfinite(grain.size) || grain.size < 0.25 || grain.size > 64.0 ||
        !std::isfinite(grain.strength) || grain.strength < 0.0 || grain.strength > 0.5 ||
        !std::isfinite(grain.color) || grain.color < -1.0 || grain.color > 1.0 ||
        (!grain.stock.empty() && find_film_grain_profile(grain.stock) == nullptr) ||
        !std::isfinite(grain.roughness) || grain.roughness < 0.0 || grain.roughness > 1.0 ||
        !std::isfinite(grain.shadows) || grain.shadows < 0.0 || grain.shadows > 2.0 ||
        !std::isfinite(grain.highlights) || grain.highlights < 0.0 || grain.highlights > 2.0 ||
        !std::isfinite(options.scale_x) || options.scale_x < kMinDocumentScale || options.scale_x > 4096.0 ||
        !std::isfinite(options.scale_y) || options.scale_y < kMinDocumentScale || options.scale_y > 4096.0 ||
        grain.size * std::max(options.scale_x, options.scale_y) > kMaxSigma ||
        options.origin_x < 0 || options.origin_y < 0)
      return {PhotoEffectStatus::Invalid, "Film grain parameters or document scale are outside supported ranges."};
    return {PhotoEffectStatus::Applied, {}};
  }
  if (effect.type_id == "light_leak" && effect.algorithm_version == 1) {
    const auto& leak = effect.leak;
    if (!effect.opaque_parameters.empty() || !std::isfinite(leak.center_x) || leak.center_x < -1.0 || leak.center_x > 2.0 ||
        !std::isfinite(leak.center_y) || leak.center_y < -1.0 || leak.center_y > 2.0 ||
        !std::isfinite(leak.angle) || leak.angle < -180.0 || leak.angle > 180.0 ||
        !std::isfinite(leak.width) || leak.width < 0.02 || leak.width > 2.0 ||
        !std::isfinite(leak.strength) || leak.strength < 0.0 || leak.strength > 1.0 ||
        !std::isfinite(leak.warmth) || leak.warmth < -1.0 || leak.warmth > 1.0 ||
        !std::isfinite(options.scale_x) || options.scale_x <= 0.0 || options.scale_x > 4096.0 ||
        !std::isfinite(options.scale_y) || options.scale_y <= 0.0 || options.scale_y > 4096.0 ||
        options.origin_x < 0 || options.origin_y < 0 ||
        options.frame_width < 0 || options.frame_height < 0)
      return {PhotoEffectStatus::Invalid, "Light leak parameters or render coordinates are outside supported ranges."};
    return {PhotoEffectStatus::Applied, {}};
  }
  if (effect.type_id == "split_tone" && effect.algorithm_version == 1) {
    const auto& split = effect.split_tone;
    if (!effect.opaque_parameters.empty() || !std::isfinite(split.shadow_hue) || split.shadow_hue < 0.0 || split.shadow_hue > 360.0 ||
        !std::isfinite(split.shadow_strength) || split.shadow_strength < 0.0 || split.shadow_strength > 1.0 ||
        !std::isfinite(split.highlight_hue) || split.highlight_hue < 0.0 || split.highlight_hue > 360.0 ||
        !std::isfinite(split.highlight_strength) || split.highlight_strength < 0.0 || split.highlight_strength > 1.0 ||
        !std::isfinite(split.balance) || split.balance < -1.0 || split.balance > 1.0)
      return {PhotoEffectStatus::Invalid, "Split tone parameters are outside supported ranges."};
    return {PhotoEffectStatus::Applied, {}};
  }
  if (effect.type_id == "vignette" && effect.algorithm_version == 1) {
    const auto& vignette = effect.vignette;
    if (!effect.opaque_parameters.empty() || !std::isfinite(vignette.radius) || vignette.radius < 0.0 || vignette.radius > 2.0 ||
        !std::isfinite(vignette.roundness) || vignette.roundness < -1.0 || vignette.roundness > 1.0 ||
        !std::isfinite(vignette.feather) || vignette.feather < 0.0 || vignette.feather > 2.0 ||
        !std::isfinite(vignette.strength) || vignette.strength < 0.0 || vignette.strength > 2.0 ||
        options.origin_x < 0 || options.origin_y < 0 ||
        options.frame_width < 0 || options.frame_height < 0)
      return {PhotoEffectStatus::Invalid, "Vignette parameters or render coordinates are outside supported ranges."};
    return {PhotoEffectStatus::Applied, {}};
  }
  if (effect.type_id == "duotone" && effect.algorithm_version == 1) {
    const auto& duotone = effect.duotone;
    if (!effect.opaque_parameters.empty() || !std::isfinite(duotone.shadow_hue) || duotone.shadow_hue < 0.0 || duotone.shadow_hue > 360.0 ||
        !std::isfinite(duotone.highlight_hue) || duotone.highlight_hue < 0.0 || duotone.highlight_hue > 360.0 ||
        !std::isfinite(duotone.strength) || duotone.strength < 0.0 || duotone.strength > 1.0 ||
        !std::isfinite(duotone.balance) || duotone.balance < -1.0 || duotone.balance > 1.0)
      return {PhotoEffectStatus::Invalid, "Duotone parameters are outside supported ranges."};
    return {PhotoEffectStatus::Applied, {}};
  }
  if (effect.type_id == "soft_fade" && effect.algorithm_version == 1) {
    const auto& fade = effect.soft_fade;
    if (!effect.opaque_parameters.empty() || !std::isfinite(fade.lift) || fade.lift < 0.0 || fade.lift > 0.3 ||
        !std::isfinite(fade.contrast) || fade.contrast < 0.0 || fade.contrast > 2.0 ||
        !std::isfinite(fade.saturation) || fade.saturation < 0.0 || fade.saturation > 2.0)
      return {PhotoEffectStatus::Invalid, "Soft fade parameters are outside supported ranges."};
    return {PhotoEffectStatus::Applied, {}};
  }
  if (effect.type_id == "imperfect_print" && effect.algorithm_version == 1) {
    const auto& print = effect.imperfect_print;
    if (!effect.opaque_parameters.empty() || !std::isfinite(print.paper_scale) ||
        print.paper_scale < 2.0 || print.paper_scale > 512.0 ||
        !std::isfinite(print.paper_texture) || print.paper_texture < 0.0 || print.paper_texture > 1.0 ||
        !std::isfinite(print.ink_spread) || print.ink_spread < 0.0 || print.ink_spread > 1.0 ||
        !std::isfinite(print.ink_density) || print.ink_density < 0.0 || print.ink_density > 2.0 ||
        !std::isfinite(print.red_offset_x) || std::abs(print.red_offset_x) > 8.0 ||
        !std::isfinite(print.red_offset_y) || std::abs(print.red_offset_y) > 8.0 ||
        !std::isfinite(print.blue_offset_x) || std::abs(print.blue_offset_x) > 8.0 ||
        !std::isfinite(print.blue_offset_y) || std::abs(print.blue_offset_y) > 8.0 ||
        !std::isfinite(options.scale_x) || options.scale_x < kMinDocumentScale ||
        !std::isfinite(options.scale_y) || options.scale_y < kMinDocumentScale ||
        options.scale_x > 4096.0 || options.scale_y > 4096.0 ||
        options.origin_x < 0 || options.origin_y < 0)
      return {PhotoEffectStatus::Invalid, "Imperfect print parameters or render coordinates are outside supported ranges."};
    support->x = static_cast<int>(std::ceil((std::max(std::abs(print.red_offset_x),
        std::abs(print.blue_offset_x)) + 1.0) * options.scale_x));
    support->y = static_cast<int>(std::ceil((std::max(std::abs(print.red_offset_y),
        std::abs(print.blue_offset_y)) + 1.0) * options.scale_y));
    return {PhotoEffectStatus::Applied, {}};
  }
  const bool box = effect.type_id == "box_blur";
  const bool motion = effect.type_id == "motion_blur";
  const bool dragged = effect.type_id == "dragged_shutter";
  const bool unsharp = effect.type_id == "unsharp_mask";
  const bool diffusion = effect.type_id == "optical_diffusion";
  if ((!box && !motion && !dragged && !unsharp && !diffusion && effect.type_id != "gaussian_blur") || effect.algorithm_version != 1)
    return {PhotoEffectStatus::Unsupported, "Unsupported photo effect type or algorithm version."};
  if (!effect.opaque_parameters.empty())
    return {PhotoEffectStatus::Invalid, "Known spatial effect has unexpected opaque parameters."};
  const auto sigma = box ? static_cast<double>(effect.box.radius) : motion ? effect.motion.distance
      : dragged ? effect.dragged_shutter.distance
      : unsharp ? effect.unsharp.sigma : diffusion ? effect.diffusion.radius : effect.gaussian.sigma;
  if (!std::isfinite(sigma) || sigma < 0.0 || sigma > kMaxSigma ||
      !std::isfinite(options.scale_x) || options.scale_x <= 0.0 ||
      !std::isfinite(options.scale_y) || options.scale_y <= 0.0 ||
      sigma * options.scale_x > kMaxSigma || sigma * options.scale_y > kMaxSigma)
    return {PhotoEffectStatus::Invalid, "Effect radius/distance or render scale is outside the supported finite range."};
  if (motion && (!std::isfinite(effect.motion.angle) || effect.motion.angle < -180 || effect.motion.angle > 180))
    return {PhotoEffectStatus::Invalid, "Motion angle must be between -180 and 180 degrees."};
  if (dragged && (!std::isfinite(effect.dragged_shutter.angle) || effect.dragged_shutter.angle < -180 ||
      effect.dragged_shutter.angle > 180 || !std::isfinite(effect.dragged_shutter.falloff) ||
      effect.dragged_shutter.falloff < 0 || effect.dragged_shutter.falloff > 1 ||
      !std::isfinite(effect.dragged_shutter.highlight_weighting) ||
      effect.dragged_shutter.highlight_weighting < 0 || effect.dragged_shutter.highlight_weighting > 1))
    return {PhotoEffectStatus::Invalid, "Dragged shutter parameters are outside their supported ranges."};
  if (unsharp && (!std::isfinite(effect.unsharp.strength) || effect.unsharp.strength < 0 || effect.unsharp.strength > 5 ||
      !std::isfinite(effect.unsharp.threshold) || effect.unsharp.threshold < 0 || effect.unsharp.threshold > 1))
    return {PhotoEffectStatus::Invalid, "Unsharp strength must be 0 to 5 and threshold 0 to 1."};
  if (diffusion && (!std::isfinite(effect.diffusion.threshold) || effect.diffusion.threshold < 0 || effect.diffusion.threshold > 16 ||
      !std::isfinite(effect.diffusion.knee) || effect.diffusion.knee < 0 || effect.diffusion.knee > 8 ||
      !std::isfinite(effect.diffusion.strength) || effect.diffusion.strength < 0 || effect.diffusion.strength > 5 ||
      !std::isfinite(effect.diffusion.warmth) || effect.diffusion.warmth < -1 || effect.diffusion.warmth > 1 ||
      !std::isfinite(effect.diffusion.highlight_protection) || effect.diffusion.highlight_protection < 0 ||
      effect.diffusion.highlight_protection > 1))
    return {PhotoEffectStatus::Invalid, "Optical diffusion parameters are outside their supported ranges."};
  const auto edge_mode = box ? effect.box.edge_mode : motion ? effect.motion.edge_mode
      : dragged ? effect.dragged_shutter.edge_mode
      : unsharp ? effect.unsharp.edge_mode : diffusion ? PhotoEffectEdgeMode::Extend : effect.gaussian.edge_mode;
  if (edge_mode != PhotoEffectEdgeMode::Transparent &&
      edge_mode != PhotoEffectEdgeMode::Extend && edge_mode != PhotoEffectEdgeMode::Reflect)
    return {PhotoEffectStatus::Invalid, "Unknown spatial effect edge mode."};
  support->x = static_cast<int>(std::ceil((box ? 1.0 : 3.0) * sigma * options.scale_x));
  support->y = static_cast<int>(std::ceil((box ? 1.0 : 3.0) * sigma * options.scale_y));
  if (motion || dragged) {
    const auto angle = motion ? effect.motion.angle : effect.dragged_shutter.angle;
    const auto radians = angle * std::numbers::pi / 180.0;
    support->x = static_cast<int>(std::ceil(0.5 * sigma * std::abs(std::cos(radians)) * options.scale_x));
    support->y = static_cast<int>(std::ceil(0.5 * sigma * std::abs(std::sin(radians)) * options.scale_y));
  }
  return {sigma == 0.0 || ((unsharp && effect.unsharp.strength == 0.0) ||
      (diffusion && effect.diffusion.strength == 0.0))
      ? PhotoEffectStatus::Identity : PhotoEffectStatus::Applied, {}};
}

// A retained rectangle is private to the tile renderer: pixels outside it may
// be scratch values. Following stages must keep contributing samples inside it.
static PhotoEffectResult apply_photo_effect_impl(std::vector<ColorPixel>* pixels, int width, int height,
    const PhotoEffectInstance& effect, const PhotoEffectRenderOptions& options,
    std::span<const float> mask, const RenderPixelRect* retained_rect) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      width > std::numeric_limits<int>::max() - 12288 ||
      height > std::numeric_limits<int>::max() - 12288 ||
      ((effect.type_id == "imperfect_print" || effect.type_id == "film_grain" ||
        effect.type_id == "light_leak" || effect.type_id == "vignette") &&
       (options.origin_x < 0 || options.origin_y < 0 ||
        options.origin_x > std::numeric_limits<int>::max() - width ||
        options.origin_y > std::numeric_limits<int>::max() - height)) ||
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
  const bool diffusion = effect.type_id == "optical_diffusion";
  const bool per_pixel = effect.type_id == "film_grain" || effect.type_id == "light_leak" ||
      effect.type_id == "split_tone" || effect.type_id == "vignette" ||
      effect.type_id == "duotone" || effect.type_id == "soft_fade";
  const auto kernel_bytes = effect.type_id == "gaussian_blur" || effect.type_id == "unsharp_mask" || diffusion
      ? static_cast<std::size_t>(2 * support.x + 2 * support.y + 2) * sizeof(double) : 0U;
  const std::size_t scratch_images = per_pixel || effect.type_id == "imperfect_print" ||
      effect.type_id == "invert_linear_rgb" || effect.type_id == "posterize_linear_rgb" ? 1U :
      (effect.type_id == "motion_blur" || effect.type_id == "dragged_shutter") ? 1U : diffusion ? 3U : 2U;
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
  if (effect.algorithm_version == 1 &&
      (is_optical_photo_effect(effect.type_id) || is_artistic_photo_effect(effect.type_id))) {
    const auto bytes = count * sizeof(ColorPixel);
    if (bytes > options.max_scratch_bytes)
      return {PhotoEffectStatus::ResourceLimit, "Extended effect mixing exceeds the scratch budget."};
    auto filtered = *pixels;
    auto extended_options = options;
    extended_options.max_scratch_bytes -= bytes;
    const auto applied = is_optical_photo_effect(effect.type_id)
        ? apply_optical_photo_effect(&filtered, width, height, effect.type_id, effect.optical, extended_options)
        : apply_artistic_photo_effect(&filtered, width, height, effect.type_id, effect.artistic, extended_options);
    if (!applied.ok() || applied.status == PhotoEffectStatus::Identity) return applied;
    const auto mixed = run_effect_pixels(width, height, 2U, options.cancellation, [&](std::size_t i) {
      const auto original = (*pixels)[i];
      const auto transformed = filtered[i];
      const auto amount = effect.amount * (mask.empty() ? 1.0 : static_cast<double>(mask[i]));
      if (amount == 0.0) { filtered[i] = original; return true; }
      if (!std::isfinite(transformed.red) || !std::isfinite(transformed.green) ||
          !std::isfinite(transformed.blue) || !std::isfinite(transformed.alpha) ||
          transformed.alpha < 0.0F || transformed.alpha > 1.0F) return false;
      const auto alpha = std::lerp(static_cast<double>(original.alpha), transformed.alpha, amount);
      const auto channel = [&](float a, float b) {
        return alpha > 0.0 ? static_cast<float>((a * original.alpha * (1.0 - amount) +
            b * transformed.alpha * amount) / alpha) : 0.0F;
      };
      filtered[i] = {channel(original.red, transformed.red), channel(original.green, transformed.green),
          channel(original.blue, transformed.blue), static_cast<float>(alpha)};
      return std::isfinite(filtered[i].red) && std::isfinite(filtered[i].green) && std::isfinite(filtered[i].blue);
    });
    if (mixed == PhotoEffectStatus::Cancelled || options.cancellation.cancelled())
      return {PhotoEffectStatus::Cancelled, "Photo effect cancelled."};
    if (mixed == PhotoEffectStatus::Invalid)
      return {PhotoEffectStatus::Invalid, "Extended effect produced non-finite working RGB."};
    pixels->swap(filtered);
    return {PhotoEffectStatus::Applied, {}};
  }
  if (per_pixel) {
    if (pixels->size() > options.max_scratch_bytes / sizeof(ColorPixel))
      return {PhotoEffectStatus::ResourceLimit, "Per-pixel photo effect exceeds the scratch memory budget."};
    const int frame_width = options.frame_width > 0 ? options.frame_width : width;
    const int frame_height = options.frame_height > 0 ? options.frame_height : height;
    const auto max_float = static_cast<double>(std::numeric_limits<float>::max());
    std::vector<ColorPixel> output = *pixels;
    const auto transformed = run_effect_pixels(width, height, 1U, options.cancellation, [&](std::size_t i) {
      const auto source = (*pixels)[i];
      const auto local_amount = effect.amount * (mask.empty() ? 1.0 : static_cast<double>(mask[i]));
      if (local_amount == 0.0 || source.alpha == 0.0F) return true;
      const auto x = static_cast<int>(i % static_cast<std::size_t>(width));
      const auto y = static_cast<int>(i / static_cast<std::size_t>(width));
      const auto luminance = options.luminance_weights[0] * source.red + options.luminance_weights[1] * source.green + options.luminance_weights[2] * source.blue;
      const std::array<double, 3> original{source.red, source.green, source.blue};
      auto filtered = original;
      if (effect.type_id == "film_grain") {
        const auto document_x = (static_cast<double>(options.origin_x) + x) / options.scale_x;
        const auto document_y = (static_cast<double>(options.origin_y) + y) / options.scale_y;
        const auto& grain = effect.grain;
        const auto* profile = find_film_grain_profile(grain.stock);
        const auto chroma = (grain.color + 1.0) * 0.5;
        const auto level = std::sqrt(std::clamp(std::abs(luminance) /
            (1.0 + std::abs(luminance)), 0.0, 1.0));
        const auto amplitude = grain.strength * (0.25 + 0.75 * level);
        if (profile != nullptr) {
          // Approximate the raster footprint: reduce subpixel variance rather
          // than turning unresolved grains into full-strength digital noise.
          const auto footprint = 1.0 / std::sqrt(options.scale_x * options.scale_y);
          const auto size = std::sqrt(grain.size * grain.size + footprint * footprint * 0.12);
          const auto resolved = std::min(1.0, grain.size / footprint);
          const auto tone = std::lerp(profile->shadow_response * grain.shadows, 1.0,
              smoothstep(0.04, 0.38, level)) *
              std::lerp(1.0, profile->highlight_response * grain.highlights,
                  smoothstep(0.60, 0.94, level));
          const auto stock_amplitude = grain.strength * std::sqrt(std::max(0.0, luminance)) * tone * resolved;
          const auto mono = stock_grain_noise(document_x, document_y, size, grain.seed, grain.roughness, *profile);
          for (std::size_t channel = 0; channel < filtered.size(); ++channel) {
            const auto dye = chroma > 0.0 ? stock_grain_noise(document_x, document_y,
                size * profile->dye_scale[channel],
                grain.seed ^ static_cast<std::uint32_t>(0x9e3779b9U * (channel + 1U)), grain.roughness, *profile) : mono;
            filtered[channel] += stock_amplitude * std::lerp(mono, dye, chroma);
          }
        } else if (chroma <= 0.0) {
          const auto noise = grain_noise(document_x, document_y, grain.size, grain.seed);
          for (auto& channel : filtered) channel += amplitude * noise;
        } else {
          const auto monochrome = chroma < 1.0
              ? grain_noise(document_x, document_y, grain.size, grain.seed) : 0.0;
          for (std::size_t channel = 0; channel < filtered.size(); ++channel) {
            const auto channel_noise = grain_noise(document_x, document_y, grain.size,
                grain.seed ^ static_cast<std::uint32_t>(0x9e3779b9U * (channel + 1U)));
            filtered[channel] += amplitude * std::lerp(monochrome, channel_noise, chroma);
          }
        }
      } else if (effect.type_id == "light_leak") {
        const auto& leak = effect.leak;
        const auto pixel_x = static_cast<double>(options.origin_x) + x + 0.5;
        const auto pixel_y = static_cast<double>(options.origin_y) + y + 0.5;
        const auto short_side = static_cast<double>(std::max(1, std::min(frame_width, frame_height)));
        const auto dx = (pixel_x - leak.center_x * frame_width) / short_side;
        const auto dy = (pixel_y - leak.center_y * frame_height) / short_side;
        const auto radians = leak.angle * std::numbers::pi / 180.0;
        const auto along = dx * std::cos(radians) + dy * std::sin(radians);
        const auto across = -dx * std::sin(radians) + dy * std::cos(radians);
        const auto long_sigma = std::max(0.015, leak.width * 0.72);
        const auto short_sigma = std::max(0.008, leak.width * 0.15);
        auto coverage = std::exp(-0.5 * (along * along / (long_sigma * long_sigma) +
            across * across / (short_sigma * short_sigma)));
        const auto normalized_x = pixel_x / frame_width;
        const auto normalized_y = pixel_y / frame_height;
        coverage *= 0.88 + 0.12 * print_noise(normalized_x, normalized_y, 0.18, leak.seed);
        constexpr std::array<double, 3> warm{1.0, 0.48, 0.19};
        constexpr std::array<double, 3> cool{0.20, 0.43, 1.0};
        const auto cool_mix = (1.0 - leak.warmth) * 0.5;
        const auto level = std::sqrt(std::clamp(std::abs(luminance) /
            (1.0 + std::abs(luminance)), 0.0, 1.0));
        const auto energy = leak.strength * coverage * (0.13 + 0.08 * level);
        for (std::size_t channel = 0; channel < filtered.size(); ++channel)
          filtered[channel] += energy * std::lerp(warm[channel], cool[channel], cool_mix);
      } else if (effect.type_id == "split_tone") {
        const auto& split = effect.split_tone;
        const auto positive_luminance = std::max(0.0, luminance);
        const auto tone = positive_luminance / (0.18 + positive_luminance);
        const auto pivot = 0.5 + split.balance * 0.32;
        const auto shadow_weight = 1.0 - smoothstep(pivot - 0.42, pivot + 0.42, tone);
        const auto highlight_weight = 1.0 - shadow_weight;
        const auto shadow = hue_tint(split.shadow_hue, options.luminance_weights);
        const auto highlight = hue_tint(split.highlight_hue, options.luminance_weights);
        for (std::size_t channel = 0; channel < filtered.size(); ++channel) {
          const auto shadowed = std::lerp(original[channel], positive_luminance * shadow[channel],
              split.shadow_strength);
          const auto highlighted = std::lerp(original[channel], positive_luminance * highlight[channel],
              split.highlight_strength);
          filtered[channel] = std::lerp(shadowed, highlighted, highlight_weight);
        }
      } else if (effect.type_id == "vignette") {
        const auto& vignette = effect.vignette;
        const auto nx = 2.0 * (static_cast<double>(options.origin_x) + x + 0.5) /
            frame_width - 1.0;
        const auto ny = 2.0 * (static_cast<double>(options.origin_y) + y + 0.5) /
            frame_height - 1.0;
        const auto exponent = 2.0 + vignette.roundness * 1.2;
        const auto distance = std::pow(std::pow(std::abs(nx), exponent) +
            std::pow(std::abs(ny), exponent), 1.0 / exponent);
        const auto edge = smoothstep(vignette.radius, vignette.radius + vignette.feather, distance);
        const auto exposure = std::exp2(-vignette.strength * edge);
        for (auto& channel : filtered) channel *= exposure;
      } else if (effect.type_id == "duotone") {
        const auto& duotone = effect.duotone;
        const auto positive_luminance = std::max(0.0, luminance);
        const auto tone = positive_luminance / (0.18 + positive_luminance);
        const auto transition = smoothstep(0.08, 0.86, tone + duotone.balance * 0.3);
        const auto shadow = hue_tint(duotone.shadow_hue, options.luminance_weights);
        const auto highlight = hue_tint(duotone.highlight_hue, options.luminance_weights);
        for (std::size_t channel = 0; channel < filtered.size(); ++channel) {
          const auto tint = std::lerp(shadow[channel], highlight[channel], transition);
          filtered[channel] = std::lerp(original[channel], positive_luminance * tint, duotone.strength);
        }
      } else {
        const auto& fade = effect.soft_fade;
        const auto lift = fade.lift * std::exp(-2.5 * std::max(0.0, luminance));
        const auto faded_luminance = 0.18 + (luminance - 0.18) * fade.contrast + lift;
        for (std::size_t channel = 0; channel < filtered.size(); ++channel) {
          const auto contrasted = original[channel] * fade.contrast + 0.18 * (1.0 - fade.contrast) + lift;
          filtered[channel] = faded_luminance + (contrasted - faded_luminance) * fade.saturation;
        }
      }
      for (std::size_t channel = 0; channel < filtered.size(); ++channel) {
        const auto mixed = std::lerp(original[channel], filtered[channel], local_amount);
        if (!std::isfinite(mixed) || std::abs(mixed) > max_float) return false;
        const auto value = static_cast<float>(mixed);
        if (channel == 0) output[i].red = value;
        else if (channel == 1) output[i].green = value;
        else output[i].blue = value;
      }
      return true;
    });
    if (transformed == PhotoEffectStatus::Cancelled || options.cancellation.cancelled())
      return {PhotoEffectStatus::Cancelled, "Photo effect cancelled."};
    if (transformed == PhotoEffectStatus::Invalid)
      return {PhotoEffectStatus::Invalid, "Photo effect output exceeds the finite working-color range."};
    pixels->swap(output);
    return {PhotoEffectStatus::Applied, {}};
  }
  if (effect.type_id == "invert_linear_rgb" || effect.type_id == "posterize_linear_rgb") {
      auto output = *pixels;
      const auto transformed = run_effect_pixels(width, height, 1U, options.cancellation, [&](std::size_t i) {
        const auto amount = effect.amount * (mask.empty() ? 1.0 : static_cast<double>(mask[i]));
        if (amount == 0 || output[i].alpha == 0) return true;
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
        return true;
      });
      if (transformed == PhotoEffectStatus::Cancelled || options.cancellation.cancelled())
        return {PhotoEffectStatus::Cancelled, "Photo effect cancelled."};
      pixels->swap(output);
      return {PhotoEffectStatus::Applied, {}};
    }
    if (effect.type_id == "imperfect_print") {
      if (pixels->size() > options.max_scratch_bytes / sizeof(ColorPixel))
        return {PhotoEffectStatus::ResourceLimit, "Imperfect print exceeds the scratch memory budget; render padded tiles."};
      const auto& print = effect.imperfect_print;
      std::vector<ColorPixel> output = *pixels;
      const auto scale_noise = print.paper_scale;
      const auto printed = run_effect_lines(height, static_cast<std::uint64_t>(width) * 16U,
          options.cancellation, [&](int y) {
        for (int x = 0; x < width; ++x) {
          if ((x & 63) == 0 && options.cancellation.cancelled())
            return;
          const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
              static_cast<std::size_t>(x);
          const auto amount = effect.amount * (mask.empty() ? 1.0 : static_cast<double>(mask[index]));
          if (amount == 0.0) continue;
          const auto origin_x = static_cast<double>(options.origin_x) + x;
          const auto origin_y = static_cast<double>(options.origin_y) + y;
          const auto paper_x = origin_x / options.scale_x;
          const auto paper_y = origin_y / options.scale_y;
          const auto paper_noise = 0.7 * print_noise(paper_x, paper_y, scale_noise, print.seed) +
              0.3 * print_noise(paper_x, paper_y, scale_noise * 0.25, print.seed ^ 0xa511e9b3U);
          const auto paper = 0.025 * print.paper_texture * paper_noise;
          const auto source = (*pixels)[index];
          const std::array<double, 3> offsets_x{print.red_offset_x, 0.0, print.blue_offset_x};
          const std::array<double, 3> offsets_y{print.red_offset_y, 0.0, print.blue_offset_y};
          std::array<float, 3> result_channels{};
          for (int channel = 0; channel < 3; ++channel) {
            const auto dx = offsets_x[static_cast<std::size_t>(channel)] * options.scale_x;
            const auto dy = offsets_y[static_cast<std::size_t>(channel)] * options.scale_y;
            const auto sample_x = static_cast<double>(x) - dx;
            const auto sample_y = static_cast<double>(y) - dy;
            const auto sampled = sample_print_channel(*pixels, width, height, sample_x, sample_y, channel);
            auto ink = std::clamp(1.0 - sampled, 0.0, 1.0);
            if (print.ink_spread > 0.0) {
              const auto neighbor = std::max({
                  1.0 - std::clamp(sample_print_channel(*pixels, width, height,
                      sample_x - options.scale_x, sample_y, channel), 0.0, 1.0),
                  1.0 - std::clamp(sample_print_channel(*pixels, width, height,
                      sample_x + options.scale_x, sample_y, channel), 0.0, 1.0),
                  1.0 - std::clamp(sample_print_channel(*pixels, width, height,
                      sample_x, sample_y - options.scale_y, channel), 0.0, 1.0),
                  1.0 - std::clamp(sample_print_channel(*pixels, width, height,
                      sample_x, sample_y + options.scale_y, channel), 0.0, 1.0)});
              ink = std::lerp(ink, std::max(ink, neighbor), print.ink_spread);
            }
            ink = std::clamp(ink * print.ink_density, 0.0, 1.0);
            const auto printed = sampled >= 0.0 && sampled <= 1.0
                ? (1.0 - ink) + paper * (1.0 - ink) : sampled;
            const auto original = channel == 0 ? source.red : channel == 1 ? source.green : source.blue;
            result_channels[static_cast<std::size_t>(channel)] = static_cast<float>(
                static_cast<double>(original) * (1.0 - amount) + printed * amount);
          }
          output[index] = {result_channels[0], result_channels[1], result_channels[2], source.alpha};
        }
      });
      if (!printed || options.cancellation.cancelled())
        return {PhotoEffectStatus::Cancelled, "Imperfect print cancelled."};
      pixels->swap(output);
      return {PhotoEffectStatus::Applied, {}};
    }
    const bool box = effect.type_id == "box_blur";
    const bool motion = effect.type_id == "motion_blur";
    const bool dragged = effect.type_id == "dragged_shutter";
    const bool unsharp = effect.type_id == "unsharp_mask";
    const auto sigma = unsharp ? effect.unsharp.sigma : diffusion ? effect.diffusion.radius : effect.gaussian.sigma;
    const auto horizontal_kernel = box || motion || dragged ? std::vector<double>{}
        : kernel(sigma * options.scale_x, support.x);
    const auto vertical_kernel = box || motion || dragged ? std::vector<double>{}
        : kernel(sigma * options.scale_y, support.y);
    const auto edge_mode = box ? effect.box.edge_mode : unsharp ? effect.unsharp.edge_mode
        : diffusion ? PhotoEffectEdgeMode::Extend : effect.gaussian.edge_mode;
    std::vector<ColorPixel> glow_source;
    const std::vector<ColorPixel>* convolution_source = pixels;
    if (diffusion) {
      glow_source.resize(count);
      const auto prepared = run_effect_pixels(width, height, 1U, options.cancellation, [&](std::size_t i) {
        const auto& source = (*pixels)[i];
        const auto luminance = std::max(0.0, options.luminance_weights[0] * source.red + options.luminance_weights[1] * source.green + options.luminance_weights[2] * source.blue);
        const auto excess = std::max(0.0, luminance - effect.diffusion.threshold);
        const auto knee = effect.diffusion.knee;
        const auto x = knee == 0.0 ? (excess > 0.0 ? 1.0 : 0.0) : std::clamp(excess / knee, 0.0, 1.0);
        const auto soft = x * x * (3.0 - 2.0 * x);
        const auto scale = luminance > 0.0 ? soft * excess / luminance : 0.0;
        // For cutout layers, only the extracted highlight contributes glow
        // coverage. Carrying the original paint alpha here spreads the shape
        // even where the highlight extractor produced no light, creating a
        // pale alpha fringe around transparent strokes. Keep the legacy RGB
        // arithmetic for opaque images; changing only the scattered coverage
        // is invisible there because output alpha already clamps to one. Set
        // extracted alpha after premultiplication so RGB energy is not scaled
        // by the highlight mask twice.
        glow_source[i] = {static_cast<float>(source.red * scale * (1.0 + 0.25 * effect.diffusion.warmth)),
            static_cast<float>(source.green * scale),
            static_cast<float>(source.blue * scale * (1.0 - 0.25 * effect.diffusion.warmth)), source.alpha};
        const bool finite = std::isfinite(glow_source[i].red) && std::isfinite(glow_source[i].green) &&
                            std::isfinite(glow_source[i].blue);
        if (finite) {
          glow_source[i] = premultiply(glow_source[i]);
          glow_source[i].alpha = static_cast<float>(source.alpha * scale);
        }
        return finite;
      });
      if (prepared == PhotoEffectStatus::Cancelled) return {prepared, "Photo effect cancelled."};
      if (prepared == PhotoEffectStatus::Invalid)
        return {prepared, "Optical diffusion exceeds the finite working-color range."};
      convolution_source = &glow_source;
    }
    std::vector<ColorPixel> horizontal((motion || dragged) ? 0U : count);
    std::vector<ColorPixel> output(count);
    if (!box && !motion && !dragged && !diffusion) {
      // Every horizontal Gaussian tap previously repeated this float multiply.
      // Prepare it once into the already budgeted output buffer; the vertical
      // pass may overwrite that buffer after horizontal convolution finishes.
      const auto prepared = run_effect_pixels(width, height, 1U, options.cancellation, [&](std::size_t i) {
        output[i] = premultiply((*pixels)[i]);
        return true;
      });
      if (prepared == PhotoEffectStatus::Cancelled) return {prepared, "Photo effect cancelled."};
      convolution_source = &output;
    }
    PhotoEffectInstance::Motion shutter_motion;
    if (dragged) {
      shutter_motion.distance = effect.dragged_shutter.distance;
      shutter_motion.angle = effect.dragged_shutter.angle;
      shutter_motion.edge_mode = effect.dragged_shutter.edge_mode;
    }
    // For a single separable tile effect, only the retained output columns are
    // needed horizontally. Keep every row for vertical taps and image-edge mapping.
    const auto region = retained_rect != nullptr && !box && !motion && !dragged
        ? *retained_rect : RenderPixelRect{0, 0, width, height};
    const RenderPixelRect horizontal_region{region.x, 0, region.width, height};
    const auto convolved = (motion || dragged) ? convolve_motion(*pixels, &output, width, height,
            dragged ? shutter_motion : effect.motion, options) : box
        ? convolve_box(*pixels, &horizontal, width, height, support.x, edge_mode, true, options.cancellation) &&
          convolve_box(horizontal, &output, width, height, support.y, edge_mode, false, options.cancellation)
        : convolve(*convolution_source, &horizontal, width, height, horizontal_kernel, support.x,
            edge_mode, true, options.cancellation, horizontal_region);
    if (!convolved || (!box && !motion && !dragged && !convolve(horizontal, &output, width, height, vertical_kernel, support.y,
            edge_mode, false, options.cancellation, region)))
      return {PhotoEffectStatus::Cancelled, "Photo effect cancelled."};
    const auto mixed = run_effect_pixels(region.width, region.height, 1U, options.cancellation, [&](std::size_t local) {
      const auto i = (static_cast<std::size_t>(region.y) + local / static_cast<std::size_t>(region.width)) *
          static_cast<std::size_t>(width) + static_cast<std::size_t>(region.x) +
          local % static_cast<std::size_t>(region.width);
      auto local_amount = mask.empty() ? 1.0 : static_cast<double>(mask[i]);
      if (dragged && effect.dragged_shutter.protect_subject && !mask.empty()) local_amount = 1.0 - local_amount;
      if (dragged) {
        const auto x = static_cast<double>(options.origin_x) +
            static_cast<double>(i % static_cast<std::size_t>(width));
        const auto y = static_cast<double>(options.origin_y) +
            static_cast<double>(i / static_cast<std::size_t>(width));
        const auto frame_width = options.frame_width > 0 ? options.frame_width : width;
        const auto frame_height = options.frame_height > 0 ? options.frame_height : height;
        const auto nx = frame_width > 1 ? (2.0 * x / static_cast<double>(frame_width - 1) - 1.0) : 0.0;
        const auto ny = frame_height > 1 ? (2.0 * y / static_cast<double>(frame_height - 1) - 1.0) : 0.0;
        const auto radial = std::clamp(std::hypot(nx, ny) / std::sqrt(2.0), 0.0, 1.0);
        const auto falloff_weight = 1.0 - effect.dragged_shutter.falloff * radial;
        const auto& source = (*pixels)[i];
        const auto luminance = std::clamp(options.luminance_weights[0] * source.red + options.luminance_weights[1] * source.green + options.luminance_weights[2] * source.blue, 0.0, 1.0);
        const auto highlight_weight = std::lerp(1.0, 0.25 + 0.75 * luminance,
            effect.dragged_shutter.highlight_weighting);
        local_amount *= falloff_weight * highlight_weight;
      }
      const auto amount = effect.amount * local_amount;
      if (amount == 0.0) { output[i] = (*pixels)[i]; return true; }
      const auto base = premultiply((*pixels)[i]);
      auto& filtered = output[i];
      if (diffusion) {
        const auto luminance = std::max(0.0, options.luminance_weights[0] * (*pixels)[i].red + options.luminance_weights[1] * (*pixels)[i].green +
            options.luminance_weights[2] * (*pixels)[i].blue);
        const auto excess = std::max(0.0, luminance - effect.diffusion.threshold);
        const auto knee = effect.diffusion.knee;
        const auto x = knee == 0.0 ? (excess > 0.0 ? 1.0 : 0.0) : std::clamp(excess / knee, 0.0, 1.0);
        const auto protection_curve = x * x * (3.0 - 2.0 * x);
        const auto glow = effect.diffusion.strength *
            (1.0 - effect.diffusion.highlight_protection * protection_curve);
        const auto alpha = std::clamp(static_cast<double>(base.alpha) + filtered.alpha * glow, 0.0, 1.0);
        const auto channel = [glow](float original_premultiplied, float scattered) {
          // Keep this intermediate premultiplied: the shared mask/amount mix
          // below combines both filtered and original pixels in premultiplied
          // space, then performs the single required unpremultiply.
          return static_cast<float>(static_cast<double>(original_premultiplied) + scattered * glow);
        };
        filtered = {channel(base.red, filtered.red), channel(base.green, filtered.green),
            channel(base.blue, filtered.blue), static_cast<float>(alpha)};
        if (!std::isfinite(filtered.red) || !std::isfinite(filtered.green) || !std::isfinite(filtered.blue))
          return false;
      }
      if (unsharp) {
        const auto original = (*pixels)[i];
        if (original.alpha == 0 || filtered.alpha <= 0) { filtered = original; return true; }
        const std::array<double, 3> detail{original.red - filtered.red / filtered.alpha,
            original.green - filtered.green / filtered.alpha, original.blue - filtered.blue / filtered.alpha};
        const auto magnitude = std::max({std::abs(detail[0]), std::abs(detail[1]), std::abs(detail[2])});
        const auto gain = magnitude >= effect.unsharp.threshold ? amount * effect.unsharp.strength : 0.0;
        filtered = {static_cast<float>(original.red + gain * detail[0]),
            static_cast<float>(original.green + gain * detail[1]),
            static_cast<float>(original.blue + gain * detail[2]), original.alpha};
        return true;
      }
      const auto alpha = std::clamp(base.alpha * (1.0 - amount) + filtered.alpha * amount, 0.0, 1.0);
      const auto mix = [&](float a, float b) {
        return alpha > 0.0 ? static_cast<float>((a * (1.0 - amount) + b * amount) / alpha) : 0.0F;
      };
      filtered = {mix(base.red, filtered.red), mix(base.green, filtered.green),
          mix(base.blue, filtered.blue), static_cast<float>(alpha)};
      return true;
    });
    if (mixed == PhotoEffectStatus::Cancelled) return {mixed, "Photo effect cancelled."};
    if (mixed == PhotoEffectStatus::Invalid)
      return {mixed, "Optical diffusion output exceeds the finite working-color range."};
    if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effect cancelled."};
    pixels->swap(output);
  } catch (const std::bad_alloc&) {
    return {PhotoEffectStatus::ResourceLimit, "Could not allocate photo effect scratch buffers."};
  }
  return {PhotoEffectStatus::Applied, {}};
}

PhotoEffectResult apply_photo_effect(std::vector<ColorPixel>* pixels, int width, int height,
    const PhotoEffectInstance& effect, const PhotoEffectRenderOptions& options,
    std::span<const float> mask) {
  return apply_photo_effect_impl(pixels, width, height, effect, options, mask, nullptr);
}

bool photo_effect_stack_requires_full_frame(std::span<const PhotoEffectInstance> effects) {
  return std::any_of(effects.begin(), effects.end(), [](const PhotoEffectInstance& effect) {
    return effect.enabled && effect.amount > 0.0 && effect.algorithm_version == 1 &&
        ((is_optical_photo_effect(effect.type_id) && optical_photo_effect_requires_full_frame(effect.type_id, effect.optical)) ||
         (is_artistic_photo_effect(effect.type_id) && artistic_photo_effect_requires_full_frame(effect.type_id, effect.artistic)));
  });
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

static PhotoEffectResult apply_photo_effect_stack_impl(std::vector<ColorPixel>* pixels, int width, int height,
    std::span<const PhotoEffectInstance> effects, const PhotoEffectRenderOptions& options,
    std::span<const PhotoEffectMaskBinding> masks, const RenderPixelRect* retained_rect) {
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
    // Later Gaussian-family stages read only their declared neighborhoods.
    // Other operators retain the full path: e.g. box-filter running sums also
    // depend numerically on the preceding samples, even beyond their support.
    const bool propagate_retained = retained_rect != nullptr && std::all_of(
        effects.begin(), effects.end(), [](const PhotoEffectInstance& effect) {
          return !effect.enabled || effect.amount == 0.0 ||
              effect.type_id == "gaussian_blur" || effect.type_id == "unsharp_mask" ||
              effect.type_id == "optical_diffusion";
        });
    auto remaining_support = support;
    for (const auto& effect : effects) {
      std::span<const float> mask;
      if (!effect.mask_ref.empty()) {
        const auto binding = std::find_if(masks.begin(), masks.end(), [&](const auto& value) {
          return value.id == effect.mask_ref;
        });
        if (binding != masks.end()) mask = binding->coverage;
      }
      RenderPixelRect stage_region;
      const auto* stage_retained = effects.size() == 1U ? retained_rect : nullptr;
      if (propagate_retained) {
        PhotoEffectSupport current_support;
        const auto current = photo_effect_support(effect, stage_options, &current_support);
        if (!current.ok()) return current;
        remaining_support.x -= current_support.x;
        remaining_support.y -= current_support.y;
        // Expand by all following support, not this stage's own support. Its
        // input still includes that halo from the preceding stage (or source).
        stage_region = expand_pixel_rect(*retained_rect,
            std::max(remaining_support.x, remaining_support.y), width, height);
        stage_retained = &stage_region;
      }
      const auto applied = apply_photo_effect_impl(&working, width, height, effect, stage_options, mask,
          stage_retained);
      if (!applied.ok()) return applied;
      if (propagate_retained && (remaining_support.x > 0 || remaining_support.y > 0)) {
        // No later retained output depends on these samples. Clear scratch halo
        // values before the next stage's full input/preparation checks: e.g. an
        // unfiltered HDR pixel could overflow diffusion outside its valid region.
        const bool cleared = run_effect_lines(height, static_cast<std::uint64_t>(width),
            options.cancellation, [&](int y) {
          const auto row = working.begin() + static_cast<std::ptrdiff_t>(
              static_cast<std::size_t>(y) * static_cast<std::size_t>(width));
          constexpr ColorPixel transparent{0.0F, 0.0F, 0.0F, 0.0F};
          if (y < stage_region.y || y >= stage_region.y + stage_region.height) {
            std::fill_n(row, width, transparent);
          } else {
            std::fill_n(row, stage_region.x, transparent);
            std::fill(row + stage_region.x + stage_region.width, row + width, transparent);
          }
        });
        if (!cleared) return {PhotoEffectStatus::Cancelled, "Photo effect stack cancelled."};
      }
    }
    if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effect stack cancelled."};
    pixels->swap(working);
  } catch (const std::bad_alloc&) {
    return {PhotoEffectStatus::ResourceLimit, "Could not allocate photo effect stack buffers."};
  }
  return {PhotoEffectStatus::Applied, {}};
}

PhotoEffectResult apply_photo_effect_stack(std::vector<ColorPixel>* pixels, int width, int height,
    std::span<const PhotoEffectInstance> effects, const PhotoEffectRenderOptions& options,
    std::span<const PhotoEffectMaskBinding> masks) {
  return apply_photo_effect_stack_impl(pixels, width, height, effects, options, masks, nullptr);
}

static PhotoEffectResult render_photo_effect_tiles_impl(std::span<const ColorPixel> pixels, int width, int height,
    std::span<const PhotoEffectInstance> effects, const PhotoEffectTileSink& sink,
    const PhotoEffectRenderOptions& options,
    std::span<const PhotoEffectMaskBinding> masks, int tile_size) {
  if (!sink || width <= 0 || height <= 0 || tile_size <= 0 ||
      options.origin_x < 0 || options.origin_y < 0 ||
      options.origin_x > std::numeric_limits<int>::max() - width ||
      options.origin_y > std::numeric_limits<int>::max() - height ||
      static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height) ||
      pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    return {PhotoEffectStatus::Invalid, "Invalid tiled effect dimensions."};
  try {
    PhotoEffectSupport support;
    const auto admission = photo_effect_stack_support(effects, options, &support);
    if (!admission.ok()) return admission;
    if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effects cancelled."};
    if (admission.status == PhotoEffectStatus::Identity) return admission;
    std::unordered_set<std::string_view> ids;
    for (const auto& mask : masks) {
      if (mask.id.empty() || !ids.insert(mask.id).second || mask.coverage.size() != pixels.size())
        return {PhotoEffectStatus::Invalid, "Invalid full-image effect mask binding."};
    }
    if (photo_effect_stack_requires_full_frame(effects)) {
      const auto bytes = pixels.size() * sizeof(ColorPixel);
      if (bytes > options.max_scratch_bytes)
        return {PhotoEffectStatus::ResourceLimit, "Whole-frame effect source exceeds the scratch budget."};
      auto complete_options = options;
      complete_options.max_scratch_bytes -= bytes;
      if (complete_options.frame_width == 0) complete_options.frame_width = width;
      if (complete_options.frame_height == 0) complete_options.frame_height = height;
      std::vector<ColorPixel> complete(pixels.begin(), pixels.end());
      const auto applied = apply_photo_effect_stack(&complete, width, height, effects, complete_options, masks);
      if (!applied.ok() || applied.status == PhotoEffectStatus::Identity) return applied;
      for (int y = 0; y < height; y += std::min(tile_size, height - y)) {
        for (int x = 0; x < width; x += std::min(tile_size, width - x)) {
          if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effects cancelled."};
          const auto offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
          if (!sink({x, y, std::min(tile_size, width - x), std::min(tile_size, height - y), width,
              std::span<const ColorPixel>(complete).subspan(offset)}))
            return {PhotoEffectStatus::Cancelled, "Whole-frame effect tile sink cancelled."};
        }
      }
      return applied;
    }
    const auto budget = options.max_scratch_bytes;
    const auto halo = std::max(support.x, support.y);
    if (halo > std::numeric_limits<int>::max() / 4 ||
        width > std::numeric_limits<int>::max() / 2 || height > std::numeric_limits<int>::max() / 2)
      return {PhotoEffectStatus::ResourceLimit, "Effect tile geometry exceeds coordinate limits."};
    auto tile_options = options;
    tile_options.max_scratch_bytes = budget;
    std::vector<ColorPixel> tile;
    std::vector<std::vector<float>> mask_pixels(masks.size());
    std::vector<PhotoEffectMaskBinding> bindings;
    bindings.reserve(masks.size());
    if (masks.size() > (std::numeric_limits<std::size_t>::max() - sizeof(ColorPixel)) / sizeof(float))
      return {PhotoEffectStatus::ResourceLimit, "Effect mask storage exceeds addressable memory."};
    const auto bytes_per_pixel = sizeof(ColorPixel) + masks.size() * sizeof(float);
    // Iterate rects directly: retaining millions of tile descriptors is not necessary.
    tile_size = std::min(tile_size, std::max(width, height));
    for (int y = 0; y < height; y += std::min(tile_size, height - y)) {
      for (int x = 0; x < width; x += std::min(tile_size, width - x)) {
        if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effects cancelled."};
        const RenderPixelRect rect{x, y, std::min(tile_size, width - x), std::min(tile_size, height - y)};
        const auto padded = expand_pixel_rect(rect, halo, width, height);
        const auto count = static_cast<std::size_t>(padded.width) * static_cast<std::size_t>(padded.height);
        if (count > tile_options.max_scratch_bytes / bytes_per_pixel)
          return {PhotoEffectStatus::ResourceLimit, "Effect tile and masks exceed memory budget."};
        resize_effect_tile_buffer(&tile, count);
        for (auto& coverage : mask_pixels) resize_effect_tile_buffer(&coverage, count);
        auto available_bytes = tile_options.max_scratch_bytes;
        const auto account_capacity = [&](std::size_t capacity, std::size_t pixel_bytes) {
          if (capacity > available_bytes / pixel_bytes) return false;
          available_bytes -= capacity * pixel_bytes;
          return true;
        };
        if (!account_capacity(tile.capacity(), sizeof(ColorPixel)))
          return {PhotoEffectStatus::ResourceLimit, "Retained effect tile exceeds memory budget."};
        for (const auto& coverage : mask_pixels) {
          if (!account_capacity(coverage.capacity(), sizeof(float)))
            return {PhotoEffectStatus::ResourceLimit, "Retained effect masks exceed memory budget."};
        }
        auto stage_options = tile_options;
        stage_options.max_scratch_bytes = available_bytes;
        stage_options.origin_x = options.origin_x + padded.x;
        stage_options.origin_y = options.origin_y + padded.y;
        stage_options.frame_width = options.frame_width > 0 ? options.frame_width : width;
        stage_options.frame_height = options.frame_height > 0 ? options.frame_height : height;
        bindings.clear();
        for (std::size_t i = 0; i < masks.size(); ++i) bindings.push_back({masks[i].id, mask_pixels[i]});
        for (int row = 0; row < padded.height; ++row) {
          if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effects cancelled."};
          const auto source = static_cast<std::size_t>(padded.y + row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(padded.x);
          const auto destination = static_cast<std::size_t>(row) * static_cast<std::size_t>(padded.width);
          std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(source), padded.width,
              tile.begin() + static_cast<std::ptrdiff_t>(destination));
          for (std::size_t i = 0; i < masks.size(); ++i)
            std::copy_n(masks[i].coverage.begin() + static_cast<std::ptrdiff_t>(source), padded.width,
                mask_pixels[i].begin() + static_cast<std::ptrdiff_t>(destination));
        }
        const RenderPixelRect retained{rect.x - padded.x, rect.y - padded.y, rect.width, rect.height};
        const auto applied = apply_photo_effect_stack_impl(&tile, padded.width, padded.height,
            effects, stage_options, bindings, &retained);
        if (!applied.ok()) return applied;
        const auto offset = static_cast<std::size_t>(rect.y - padded.y) *
            static_cast<std::size_t>(padded.width) + static_cast<std::size_t>(rect.x - padded.x);
        const auto view_size = static_cast<std::size_t>(rect.height - 1) *
            static_cast<std::size_t>(padded.width) + static_cast<std::size_t>(rect.width);
        if (!sink({rect.x, rect.y, rect.width, rect.height, padded.width,
                std::span<const ColorPixel>(tile).subspan(offset, view_size)}))
          return {options.cancellation.cancelled() ? PhotoEffectStatus::Cancelled : PhotoEffectStatus::Invalid,
              "Effect tile destination rejected output."};
      }
    }
    if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effects cancelled."};
    return {PhotoEffectStatus::Applied, {}};
  } catch (const std::bad_alloc&) {
    return {PhotoEffectStatus::ResourceLimit, "Could not allocate effect tiles."};
  }
}

PhotoEffectResult render_photo_effect_tiles(std::span<const ColorPixel> pixels, int width, int height,
    std::span<const PhotoEffectInstance> effects, const PhotoEffectTileSink& sink,
    const PhotoEffectRenderOptions& options,
    std::span<const PhotoEffectMaskBinding> masks, int tile_size) {
  return render_photo_effect_tiles_impl(pixels, width, height, effects, sink, options, masks, tile_size);
}

PhotoEffectResult apply_photo_effect_stack_to_private_raster(std::vector<ColorPixel>* pixels, int width, int height,
    std::span<const PhotoEffectInstance> effects, const PhotoEffectRenderOptions& options,
    std::span<const PhotoEffectMaskBinding> masks, int tile_size) {
  if (pixels == nullptr || width <= 0 || height <= 0 || tile_size <= 0 ||
      static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height) ||
      pixels->size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    return {PhotoEffectStatus::Invalid, "Invalid private effect raster dimensions."};
  if (photo_effect_stack_requires_full_frame(effects)) {
    auto complete_options = options;
    if (complete_options.frame_width == 0) complete_options.frame_width = width;
    if (complete_options.frame_height == 0) complete_options.frame_height = height;
    return apply_photo_effect_stack(pixels, width, height, effects, complete_options, masks);
  }

  PhotoEffectSupport support;
  const auto admission = photo_effect_stack_support(effects, options, &support);
  if (!admission.ok()) return admission;
  if (options.cancellation.cancelled()) return {PhotoEffectStatus::Cancelled, "Photo effects cancelled."};
  if (admission.status == PhotoEffectStatus::Identity) return admission;
  const auto halo = std::max(support.x, support.y);
  tile_size = std::min(tile_size, std::max(width, height));
  const auto pending_height = static_cast<std::size_t>(std::min<std::int64_t>(height,
      static_cast<std::int64_t>(halo) + tile_size));
  const auto row_width = static_cast<std::size_t>(width);
  if (pending_height > options.max_scratch_bytes / sizeof(ColorPixel) / row_width)
    return {PhotoEffectStatus::ResourceLimit, "Pending effect rows exceed memory budget."};
  try {
    std::vector<ColorPixel> pending(row_width * pending_height);
    if (pending.capacity() > options.max_scratch_bytes / sizeof(ColorPixel))
      return {PhotoEffectStatus::ResourceLimit, "Retained effect rows exceed memory budget."};
    auto tile_options = options;
    tile_options.max_scratch_bytes -= pending.capacity() * sizeof(ColorPixel);
    int committed_rows = 0;
    return render_photo_effect_tiles_impl(*pixels, width, height, effects,
        [&](const PhotoEffectTileView& tile) {
          for (int row = 0; row < tile.height; ++row) {
            const auto ring_row = static_cast<std::size_t>(tile.y + row) % pending_height;
            std::copy_n(tile.pixels.begin() + static_cast<std::ptrdiff_t>(row) * tile.stride,
                tile.width, pending.begin() + static_cast<std::ptrdiff_t>(ring_row * row_width +
                    static_cast<std::size_t>(tile.x)));
          }
          // The tile iterator visits complete horizontal bands in order. Only
          // after the band's last tile may we overwrite rows outside the halo
          // of every future band. The final band has no remaining readers.
          if (tile.x + tile.width == width) {
            const auto next_y = tile.y + tile.height;
            const auto safe_end = next_y == height ? height : std::max(0, next_y - halo);
            while (committed_rows < safe_end) {
              if (options.cancellation.cancelled()) return false;
              const auto row = static_cast<std::size_t>(committed_rows);
              std::copy_n(pending.begin() + static_cast<std::ptrdiff_t>((row % pending_height) * row_width),
                  row_width, pixels->begin() + static_cast<std::ptrdiff_t>(row * row_width));
              ++committed_rows;
            }
          }
          return !options.cancellation.cancelled();
        }, tile_options, masks, tile_size);
  } catch (const std::bad_alloc&) {
    return {PhotoEffectStatus::ResourceLimit, "Could not allocate pending effect rows."};
  }
}

PhotoEffectResult apply_photo_effect_stack_tiled(std::vector<ColorPixel>* pixels, int width, int height,
    std::span<const PhotoEffectInstance> effects, const PhotoEffectRenderOptions& options,
    std::span<const PhotoEffectMaskBinding> masks, int tile_size) {
  if (pixels == nullptr || width <= 0 || height <= 0 || tile_size <= 0 ||
      options.origin_x < 0 || options.origin_y < 0 ||
      options.origin_x > std::numeric_limits<int>::max() - width ||
      options.origin_y > std::numeric_limits<int>::max() - height ||
      static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height) ||
      pixels->size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    return {PhotoEffectStatus::Invalid, "Invalid tiled effect dimensions."};
  if (photo_effect_stack_requires_full_frame(effects)) {
    auto complete_options = options;
    if (complete_options.frame_width == 0) complete_options.frame_width = width;
    if (complete_options.frame_height == 0) complete_options.frame_height = height;
    return apply_photo_effect_stack(pixels, width, height, effects, complete_options, masks);
  }

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
    if (pixels->size() > options.max_scratch_bytes / sizeof(ColorPixel))
      return {PhotoEffectStatus::ResourceLimit, "Effect output exceeds memory budget."};
    auto tile_options = options;
    tile_options.max_scratch_bytes -= pixels->size() * sizeof(ColorPixel);
    std::vector<ColorPixel> output(pixels->size());
    const auto result = render_photo_effect_tiles(*pixels, width, height, effects,
        [&](const PhotoEffectTileView& tile) {
          for (int row = 0; row < tile.height; ++row) {
            if (options.cancellation.cancelled()) return false;
            const auto destination = static_cast<std::size_t>(tile.y + row) *
                static_cast<std::size_t>(width) + static_cast<std::size_t>(tile.x);
            std::copy_n(tile.pixels.begin() + static_cast<std::ptrdiff_t>(row) * tile.stride,
                tile.width, output.begin() + static_cast<std::ptrdiff_t>(destination));
          }
          return true;
        }, tile_options, masks, tile_size);
    if (result.ok() && result.status == PhotoEffectStatus::Applied) pixels->swap(output);
    return result;
  } catch (const std::bad_alloc&) {
    return {PhotoEffectStatus::ResourceLimit, "Could not allocate effect tiles."};
  }
}


}  // namespace photo_editor::pipeline
