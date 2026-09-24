#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

namespace photo_editor::desktop::detail {

using pipeline::LocalMask;
using pipeline::LocalMaskSet;
using pipeline::BrushPoint;
using pipeline::CurvePoint;
using pipeline::ColorPixel;

// Deliberately conservative: only geometry/paint with no image-dependent range
// or edge refinement can survive a source or color-pipeline change.
inline bool mask_raster_source_independent(const LocalMaskSet& set) {
  return std::all_of(set.sources.begin(), set.sources.end(), [](const LocalMask& mask) {
    const auto& kind = mask.kind;
    return (kind == "linear" || kind == "radial" || kind == "ellipse" ||
            kind == "rectangle" || kind == "brush" || kind == "polygon" || kind == "lasso") &&
        mask.selection_fine_edge == 0.0 && mask.selection_feather == 0.0 &&
        mask.selection_blur == 0.0 && mask.refine_luminance_min == 0.0 &&
        mask.refine_luminance_max == 1.0 && mask.refine_hue_range == 0.5 &&
        mask.refine_saturation_min == 0.0 && mask.refine_saturation_max == 1.0 &&
        mask.refine_depth_min == 0.0 && mask.refine_depth_max == 1.0;
  });
}

// Exact structural keys avoid stale raster reuse after brush edits, undo,
// nesting/order changes, or resizing. Both key payloads and alpha count toward
// the limit; large masks still render normally without entering the cache.
class MaskRasterCache final {
 public:
  static constexpr std::size_t kByteLimit = 64U * 1024U * 1024U;
  static constexpr std::size_t kEntryLimit = 4;

  std::optional<std::vector<std::uint8_t>> find(const LocalMaskSet& set, int width, int height) {
    std::shared_ptr<const Entry> found;
    {
      std::lock_guard lock(mutex_);
      const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) {
        return entry->width == width && entry->height == height && entry->set == set;
      });
      if (it == entries_.end()) {
        trace("miss", width, height, bytes_);
        return std::nullopt;
      }
      trace("hit", width, height, bytes_);
      found = *it;
      entries_.erase(it);
      entries_.push_front(found);
    }
    return found->alpha;
  }

  void insert(const LocalMaskSet& set, int width, int height, const std::vector<std::uint8_t>& alpha) {
    if (size_bytes(set, alpha) > kByteLimit) {
      trace("oversized", width, height, size_bytes(set, alpha));
      return;
    }
    auto entry = std::make_shared<Entry>(Entry{set, width, height, alpha});
    const auto bytes = size_bytes(entry->set, entry->alpha);
    if (bytes > kByteLimit) return;
    std::lock_guard lock(mutex_);
    // Two workers may finish the same raster concurrently.
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if ((*it)->width == width && (*it)->height == height && (*it)->set == set) {
        bytes_ -= size_bytes((*it)->set, (*it)->alpha);
        entries_.erase(it);
        break;
      }
    }
    while (!entries_.empty() && (entries_.size() >= kEntryLimit || bytes_ + bytes > kByteLimit)) {
      trace("evict", entries_.back()->width, entries_.back()->height,
          size_bytes(entries_.back()->set, entries_.back()->alpha));
      bytes_ -= size_bytes(entries_.back()->set, entries_.back()->alpha);
      entries_.pop_back();
    }
    entries_.push_front(std::move(entry));
    bytes_ += bytes;
  }

  std::size_t retained_bytes() const {
    std::lock_guard lock(mutex_);
    return bytes_;
  }

 private:
  struct Entry final {
    LocalMaskSet set;
    int width;
    int height;
    std::vector<std::uint8_t> alpha;
  };

  static void trace(const char* event, int width, int height, std::size_t bytes) {
    static const bool enabled = std::getenv("PHOTO_EDITOR_PROFILE_LAYER_MASK_CACHE") != nullptr;
    if (enabled) std::fprintf(stderr, "layer.mask.cache %s width=%d height=%d bytes=%zu\n",
        event, width, height, bytes);
  }

  static std::size_t size_bytes(const LocalMaskSet& set, const std::vector<std::uint8_t>& alpha) {
    auto bytes = sizeof(Entry) + alpha.capacity() + set.sources.capacity() * sizeof(LocalMask);
    bytes += set.id.capacity() + set.name.capacity() + 2;
    for (const auto& mask : set.sources) {
      for (const auto* text : {&mask.id, &mask.parent_id, &mask.output_id, &mask.name, &mask.kind,
               &mask.operation, &mask.blend_mode, &mask.retouch_mode, &mask.inpaint_model_name})
        bytes += text->capacity() + 1;
      bytes += mask.brush_points.capacity() * sizeof(BrushPoint);
      bytes += mask.polygon_points.capacity() * sizeof(CurvePoint);
      bytes += mask.segmentation_mask.capacity() + mask.depth_map.capacity();
      bytes += mask.inpaint_patch_pixels.capacity() * sizeof(ColorPixel);
    }
    return bytes;
  }

  mutable std::mutex mutex_;
  std::deque<std::shared_ptr<const Entry>> entries_;
  std::size_t bytes_ = 0;
};

// Layer compilation is the sole production caller. General mask/brush sampling
// retains its original evaluation and scheduling behavior.
inline std::vector<std::uint8_t> rasterize_layer_selection_alpha(
    const LocalMaskSet& set, int width, int height,
    std::span<const ColorPixel> source_pixels,
    const pipeline::ColorPipelineConfig& color_pipeline) {
  if (width <= 0 || height <= 0) return {};
  static MaskRasterCache cache;
  const auto reusable = mask_raster_source_independent(set);
  if (reusable) {
    if (auto cached = cache.find(set, width, height)) return std::move(*cached);
  }
  // These masks do not consult either range pixels or the edge image. Passing
  // an empty surface also avoids the scalar rasterizer's full edge-image copy
  // on a cache miss; source-dependent masks keep the complete original input.
  auto alpha = pipeline::rasterize_local_mask_set_alpha(
      set, width, height, reusable ? std::span<const ColorPixel>{} : source_pixels, color_pipeline);
  if (reusable) cache.insert(set, width, height, alpha);
  return alpha;
}

}  // namespace photo_editor::desktop::detail
