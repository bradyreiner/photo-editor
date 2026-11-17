#pragma once

#include <QHash>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <atomic>
#include <functional>

namespace photo_editor::desktop::timeline_motion {

struct NativeGeneratorFrameSample final {
  double sequence_seconds = 0.0;
  double local_seconds = 0.0;
  double gather = 0.0;
  double time_scale = 1.0;
};

struct NativeGeneratorFrameSequenceRequest final {
  QString generator_kind;
  QString cache_root;
  QSize frame_size;
  double frames_per_second = 30.0;
  double clip_start_seconds = 0.0;
  double clip_duration_seconds = 0.0;
  double time_offset_seconds = 0.0;
  double veil_gather = 0.18;
  double veil_field_opacity = 0.42;
  QVariantMap vector_path_parameters;
  QVector<NativeGeneratorFrameSample> samples;
};

struct NativeGeneratorFrameSequenceResult final {
  bool ok = false;
  bool cache_hit = false;
  QString cache_key;
  QString frame_directory;
  QString frame_pattern;
  QString backend = QStringLiteral("none");
  QString fallback_reason;
  QString error;
  qint64 encoded_bytes = 0;
  qint64 elapsed_milliseconds = 0;
  int frame_count = 0;
  int metal_frame_count = 0;
  int cpu_frame_count = 0;
  bool lease_acquired = false;
};

class NativeGeneratorFrameSequenceLease final {
 public:
  NativeGeneratorFrameSequenceLease() = default;
  ~NativeGeneratorFrameSequenceLease();
  NativeGeneratorFrameSequenceLease(const NativeGeneratorFrameSequenceLease&) = delete;
  NativeGeneratorFrameSequenceLease& operator=(const NativeGeneratorFrameSequenceLease&) = delete;
  NativeGeneratorFrameSequenceLease(NativeGeneratorFrameSequenceLease&& other) noexcept;
  NativeGeneratorFrameSequenceLease& operator=(NativeGeneratorFrameSequenceLease&& other) noexcept;

  void adoptPinnedDirectory(const QString& directory);
  // Transfers an existing pin and verified descriptor when valid; false leaves
  // pin ownership with the caller.
  [[nodiscard]] bool adoptPinnedResult(
      const NativeGeneratorFrameSequenceResult& result);
  // Returns only results whose directory is still owned by this lease.
  [[nodiscard]] bool lookup(
      const QString& cache_key,
      NativeGeneratorFrameSequenceResult* result) const;
  void append(NativeGeneratorFrameSequenceLease&& other);
  void clear();

 private:
  QStringList directories_;
  QHash<QString, NativeGeneratorFrameSequenceResult> results_by_cache_key_;
};

inline constexpr qint64 kNativeGeneratorFrameCacheBudgetBytes =
    2LL * 1024LL * 1024LL * 1024LL;

[[nodiscard]] QString native_generator_frame_sequence_cache_key(
    const NativeGeneratorFrameSequenceRequest& request);

[[nodiscard]] bool native_generator_frame_sequence_is_cached(
    const NativeGeneratorFrameSequenceRequest& request,
    NativeGeneratorFrameSequenceResult* result = nullptr);

void pin_native_generator_frame_sequence_directory(const QString& directory);
void unpin_native_generator_frame_sequence_directory(const QString& directory);
[[nodiscard]] bool native_generator_frame_sequence_cache_busy();
#ifdef PHOTO_EDITOR_TIMELINE_NATIVE_GENERATOR_CACHE_TEST_HOOKS
[[nodiscard]] int native_generator_frame_cache_inventory_full_scans_for_testing() noexcept;
[[nodiscard]] int native_generator_frame_cache_inventory_manifest_refreshes_for_testing() noexcept;
[[nodiscard]] int native_generator_frame_cache_inventory_manifest_unchanged_skips_for_testing() noexcept;
#endif
// Update a pinned, incomplete cache-stage manifest and enforce the shared LRU
// budget under the same lock used by generator frame publication. Callers
// reserve bytes before writing each encoded frame, then remove/unpin the stage
// on failure.
[[nodiscard]] bool reserve_native_generator_frame_cache_bytes(
    const QString& cache_root,
    const QString& staged_directory,
    qint64 encoded_bytes,
    bool* cache_capacity_failure = nullptr,
    QString* error_message = nullptr);
// Refresh the LRU timestamp of a pinned completed cache entry. Reports a
// shared-budget miss separately so the caller can select a correct fallback.
[[nodiscard]] bool touch_native_generator_frame_cache_entry(
    const QString& cache_root,
    const QString& directory,
    bool* cache_capacity_failure = nullptr,
    QString* error_message = nullptr);

[[nodiscard]] NativeGeneratorFrameSequenceResult ensure_native_generator_frame_sequence(
    const NativeGeneratorFrameSequenceRequest& request,
    const std::atomic_bool* cancelled = nullptr,
    const std::function<bool()>& cancellation_check = {},
    bool pin_result = false);

}  // namespace photo_editor::desktop::timeline_motion
