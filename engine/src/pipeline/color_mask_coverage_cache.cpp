#include "color_mask_coverage_cache.h"
#include "color_local_fast_path_policy.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <new>

namespace photo_editor::pipeline {
namespace {
constexpr std::size_t kBudget = 384U * 1024U * 1024U;
constexpr std::size_t kMaximumKeyBytes = 32U * 1024U * 1024U;
struct Budget { std::atomic_size_t bytes = 0, hits = 0, misses = 0; };
struct Reservation {
  std::shared_ptr<Budget> budget;
  std::size_t bytes;
  Reservation(std::shared_ptr<Budget> owner, std::size_t size) : budget(std::move(owner)), bytes(size) {}
  ~Reservation() { budget->bytes.fetch_sub(bytes); }
};
std::shared_ptr<Reservation> reserve(const std::shared_ptr<Budget>& budget, std::size_t bytes) {
  auto used = budget->bytes.load();
  do {
    if (bytes > kBudget || used > kBudget - bytes) return {};
  } while (!budget->bytes.compare_exchange_weak(used, used + bytes));
  try { return std::make_shared<Reservation>(budget, bytes); }
  catch (...) { budget->bytes.fetch_sub(bytes); throw; }
}
bool same_pixels(const std::vector<ColorPixel>& a, const std::vector<ColorPixel>& b,
    CancellationToken cancellation) {
  if (a.size() != b.size()) return false;
  constexpr std::size_t chunk = 4096;
  for (std::size_t i = 0; i < a.size(); i += chunk) {
    if (cancellation.cancelled() || std::memcmp(a.data() + i, b.data() + i,
        std::min(chunk, a.size() - i) * sizeof(ColorPixel)) != 0) return false;
  }
  return true;
}
std::size_t key_bytes(std::span<const LocalMaskSet> prefix) {
  std::size_t bytes = 0;
  for (const auto& set : prefix) {
    bytes += sizeof(set) + set.id.size() + set.name.size();
    for (const auto& source : set.sources) {
      bytes += sizeof(source) + source.id.size() + source.parent_id.size() + source.output_id.size() +
          source.name.size() + source.kind.size() + source.operation.size() + source.blend_mode.size() +
          source.retouch_mode.size() + source.inpaint_model_name.size() + source.segmentation_mask.size() +
          source.depth_map.size() + source.brush_points.size() * sizeof(BrushPoint) +
          source.polygon_points.size() * sizeof(CurvePoint) + source.inpaint_patch_pixels.size() * sizeof(ColorPixel);
      if (bytes > kMaximumKeyBytes) return bytes;
    }
    if (bytes > kMaximumKeyBytes) return bytes;
  }
  return bytes;
}
std::vector<LocalMaskSet> coverage_key(std::span<const LocalMaskSet> prefix) {
  std::vector<LocalMaskSet> key(prefix.begin(), prefix.end());
  // Exclude only known current color edits; retain every other field, including
  // future refinement fields. Earlier sets remain complete because their
  // adjustments/decontamination can change downstream range-mask inputs.
  auto& last = key.back();
  for (const auto field : {&LocalMaskSet::exposure, &LocalMaskSet::contrast,
      &LocalMaskSet::highlights, &LocalMaskSet::shadows, &LocalMaskSet::whites, &LocalMaskSet::blacks,
      &LocalMaskSet::temperature, &LocalMaskSet::tint, &LocalMaskSet::vibrance, &LocalMaskSet::saturation,
      &LocalMaskSet::hue_shift, &LocalMaskSet::saturation_shift, &LocalMaskSet::colorize_hue,
      &LocalMaskSet::colorize_strength}) last.*field = 0.0;
  return key;
}
}  // namespace

struct MaskCoverageEntry {
  std::shared_ptr<Reservation> reservation;  // Released after payload destruction.
  std::vector<LocalMaskSet> key;
  std::unique_ptr<double[]> weights;
};
class MaskCoverageFrame {
 public:
  std::shared_ptr<Reservation> reservation;
  std::shared_ptr<Budget> budget;
  std::vector<ColorPixel> pixels, edge;
  int width = 0, height = 0;
  PipelineColorContext context;
  PhotographicRenderingModel model;
  LocalAdjustmentCoordinateMapping mapping;
  std::mutex mutex;
  std::deque<std::shared_ptr<MaskCoverageEntry>> entries;
};
namespace {
struct Cache {
  std::mutex mutex;
  std::shared_ptr<Budget> budget = std::make_shared<Budget>();
  std::shared_ptr<MaskCoverageFrame> frame;
  std::atomic_bool enabled = true;
};
Cache& cache() { static Cache instance; return instance; }
}

const std::vector<ColorPixel>& mask_coverage_edge_pixels(const MaskCoverageFrame& frame) {
  return frame.edge.empty() ? frame.pixels : frame.edge;
}

std::shared_ptr<MaskCoverageFrame> acquire_mask_coverage_frame(
    const std::vector<ColorPixel>& pixels, const std::vector<ColorPixel>* edge,
    int width, int height, const PipelineColorContext& context,
    PhotographicRenderingModel model, const LocalAdjustmentCoordinateMapping& mapping,
    CancellationToken cancellation) {
  auto& state = cache();
  if (!state.enabled || cancellation.cancelled() || pixels.empty() ||
      !local_adjustment_coordinate_mapping_is_valid(mapping, width, height) ||
      pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) ||
      pixels.size() > kBudget / sizeof(ColorPixel) || (edge && edge->size() != pixels.size())) return {};
  std::shared_ptr<MaskCoverageFrame> previous;
  { std::lock_guard lock(state.mutex); previous = state.frame; }
  const auto& edge_pixels = edge ? *edge : pixels;
  if (previous && previous->width == width && previous->height == height &&
      previous->context == context && previous->model == model && previous->mapping == mapping &&
      same_pixels(previous->pixels, pixels, cancellation) &&
      // The normal same-input edge reference needs only one byte comparison.
      ((edge == nullptr && previous->edge.empty()) ||
       same_pixels(mask_coverage_edge_pixels(*previous), edge_pixels, cancellation))) return previous;
  if (cancellation.cancelled()) return {};
  { std::lock_guard lock(state.mutex); if (state.frame == previous) state.frame.reset(); }
  previous.reset();
  const bool separate_edge = edge && !same_pixels(pixels, *edge, cancellation);
  if (cancellation.cancelled()) return {};
  const auto bytes = pixels.size() * sizeof(ColorPixel) +
      (separate_edge ? edge_pixels.size() * sizeof(ColorPixel) : 0) + sizeof(MaskCoverageFrame);
  try {
    auto reservation = reserve(state.budget, bytes);
    if (!reservation) return {};
    auto frame = std::make_shared<MaskCoverageFrame>();
    frame->reservation = std::move(reservation);
    frame->budget = state.budget;
    frame->pixels = pixels;
    if (separate_edge) frame->edge = edge_pixels;
    frame->width = width; frame->height = height;
    frame->context = context; frame->model = model; frame->mapping = mapping;
    if (cancellation.cancelled()) return {};
    { std::lock_guard lock(state.mutex); state.frame = frame; }
    return frame;
  } catch (const std::bad_alloc&) { return {}; }
}

MaskCoverageLease acquire_mask_coverage(const std::shared_ptr<MaskCoverageFrame>& frame,
    std::span<const LocalMaskSet> prefix, CancellationToken cancellation) {
  if (!frame || prefix.empty() || cancellation.cancelled()) return {};
  const auto bytes = key_bytes(prefix);
  if (bytes > kMaximumKeyBytes) return {};
  try {
    auto key = coverage_key(prefix);
    std::lock_guard lock(frame->mutex);
    bool seen_before = false;
    for (auto it = frame->entries.begin(); it != frame->entries.end(); ++it) {
      if ((*it)->key == key) {
        if (!(*it)->weights) { seen_before = true; break; }
        auto entry = *it;
        frame->entries.erase(it); frame->entries.push_front(entry);
        frame->budget->hits.fetch_add(1);
        const auto* weights = entry->weights.get();
        return {std::move(entry), true, nullptr, weights};
      }
    }
    frame->budget->misses.fetch_add(1);
    if (!seen_before) {
      // Probation keeps one-off/cold edits on the original pixel path. Only a
      // repeated coverage request pays for a full-resolution weight buffer.
      auto reservation = reserve(frame->budget, bytes + sizeof(MaskCoverageEntry));
      if (!reservation) return {};
      auto entry = std::make_shared<MaskCoverageEntry>();
      entry->reservation = std::move(reservation);
      entry->key = std::move(key);
      frame->entries.push_front(std::move(entry));
      while (frame->entries.size() > 8) frame->entries.pop_back();
      return {};
    }
    auto reservation = reserve(frame->budget, bytes + frame->pixels.size() * sizeof(double) + sizeof(MaskCoverageEntry));
    while (!reservation && !frame->entries.empty()) {
      frame->entries.pop_back();
      reservation = reserve(frame->budget, bytes + frame->pixels.size() * sizeof(double) + sizeof(MaskCoverageEntry));
    }
    if (!reservation) return {};
    auto entry = std::make_shared<MaskCoverageEntry>();
    entry->reservation = std::move(reservation);
    entry->key = std::move(key);
    // Every pixel is written before publication. Cancellation discards the
    // uncompleted lease, so a full zero-fill is unnecessary on the cold path.
    entry->weights = std::make_unique_for_overwrite<double[]>(frame->pixels.size());
    auto* weights = entry->weights.get();
    return {std::move(entry), false, weights, weights};
  } catch (const std::bad_alloc&) { return {}; }
}

void publish_mask_coverage(const std::shared_ptr<MaskCoverageFrame>& frame,
    const MaskCoverageLease& lease, CancellationToken cancellation) {
  if (!frame || !lease.entry || lease.hit || cancellation.cancelled()) return;
  std::lock_guard lock(frame->mutex);
  for (auto it = frame->entries.begin(); it != frame->entries.end(); ++it) {
    if ((*it)->key != lease.entry->key) continue;
    if ((*it)->weights) return;
    frame->entries.erase(it);
    break;
  }
  frame->entries.push_front(lease.entry);
  while (frame->entries.size() > 8) frame->entries.pop_back();
}
MaskCoverageCacheStats mask_coverage_cache_stats() {
  const auto& budget = cache().budget;
  return {budget->hits.load(), budget->misses.load(), budget->bytes.load()};
}
void reset_mask_coverage_cache_for_testing(bool enabled) {
  auto& state = cache();
  std::lock_guard lock(state.mutex);
  state.enabled = enabled;
  state.frame.reset();
  state.budget->hits = 0; state.budget->misses = 0;
}

}  // namespace photo_editor::pipeline
