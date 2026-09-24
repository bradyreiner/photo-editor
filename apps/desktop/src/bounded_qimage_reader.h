#pragma once

#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QSize>

#include <algorithm>
#include <limits>
#include <optional>

namespace photo_editor::desktop {

[[nodiscard]] inline std::optional<int>
bounded_qimage_allocation_limit_megabytes(
    const QSize& size,
    int bytes_per_pixel,
    int maximum_megabytes = 1024) {
  if (!size.isValid() || bytes_per_pixel <= 0 ||
      maximum_megabytes <= 0) {
    return std::nullopt;
  }
  constexpr quint64 kMegabyte = 1024U * 1024U;
  const auto width = static_cast<quint64>(size.width());
  const auto height = static_cast<quint64>(size.height());
  const auto pixel_bytes = static_cast<quint64>(bytes_per_pixel);
  if (height > std::numeric_limits<quint64>::max() / width ||
      width * height >
          std::numeric_limits<quint64>::max() / pixel_bytes) {
    return std::nullopt;
  }
  const auto required_bytes = width * height * pixel_bytes;
  const auto required_megabytes =
      (required_bytes + kMegabyte - 1U) / kMegabyte + 1U;
  if (required_megabytes >
      static_cast<quint64>(maximum_megabytes)) {
    return std::nullopt;
  }
  return static_cast<int>(required_megabytes);
}

class ScopedQImageReaderAllocationLimit final {
 public:
  ScopedQImageReaderAllocationLimit(
      const QSize& expected_size,
      int bytes_per_pixel,
      int maximum_megabytes = 1024)
      : locker_(&allocation_mutex()),
        previous_limit_(QImageReader::allocationLimit()),
        required_limit_(bounded_qimage_allocation_limit_megabytes(
            expected_size,
            bytes_per_pixel,
            maximum_megabytes)) {
    if (required_limit_.has_value() &&
        previous_limit_ > 0 &&
        *required_limit_ > previous_limit_) {
      QImageReader::setAllocationLimit(*required_limit_);
      changed_ = true;
    }
  }

  ~ScopedQImageReaderAllocationLimit() {
    if (changed_) {
      QImageReader::setAllocationLimit(previous_limit_);
    }
  }

  ScopedQImageReaderAllocationLimit(
      const ScopedQImageReaderAllocationLimit&) = delete;
  ScopedQImageReaderAllocationLimit& operator=(
      const ScopedQImageReaderAllocationLimit&) = delete;

  [[nodiscard]] bool valid() const {
    return required_limit_.has_value();
  }

  [[nodiscard]] int required_limit_megabytes() const {
    return required_limit_.value_or(0);
  }

 private:
  [[nodiscard]] static QMutex& allocation_mutex() {
    static QMutex mutex;
    return mutex;
  }

  QMutexLocker<QMutex> locker_;
  int previous_limit_ = 0;
  std::optional<int> required_limit_;
  bool changed_ = false;
};

}  // namespace photo_editor::desktop
