#pragma once

#include <QByteArray>

#include <cmath>

#include <QtGlobal>

namespace photo_editor::desktop {

struct StereoPanGains final {
  double left = 1.0;
  double right = 1.0;
};

inline void appendBoundedAudioFrames(
    QByteArray* pending,
    const QByteArray& incoming,
    qsizetype maximum_frames,
    qsizetype bytes_per_frame) {
  if (pending == nullptr) {
    return;
  }
  if (maximum_frames <= 0 || bytes_per_frame <= 0) {
    pending->clear();
    return;
  }

  const auto frame_aligned_pending_size =
      pending->size() - pending->size() % bytes_per_frame;
  if (frame_aligned_pending_size != pending->size()) {
    pending->truncate(frame_aligned_pending_size);
  }
  const auto incoming_size = incoming.size() - incoming.size() % bytes_per_frame;
  if (incoming_size <= 0) {
    return;
  }
  const auto maximum_bytes = maximum_frames * bytes_per_frame;
  if (incoming_size >= maximum_bytes) {
    *pending = incoming.mid(incoming_size - maximum_bytes, maximum_bytes);
    return;
  }

  pending->append(incoming.constData(), incoming_size);
  const auto excess_bytes = pending->size() - maximum_bytes;
  if (excess_bytes > 0) {
    pending->remove(0, excess_bytes);
  }
}

inline StereoPanGains stereoPanGains(double pan, double volume = 1.0, bool enabled = true) {
  if (!enabled) {
    return {volume, volume};
  }
  constexpr double kPi = 3.14159265358979323846;
  const auto bounded_pan = qBound(-1.0, pan, 1.0);
  const auto angle = (bounded_pan + 1.0) * kPi / 4.0;
  const auto center_reference = std::sqrt(2.0);
  return {
      volume * std::cos(angle) * center_reference,
      volume * std::sin(angle) * center_reference,
  };
}

}  // namespace photo_editor::desktop
