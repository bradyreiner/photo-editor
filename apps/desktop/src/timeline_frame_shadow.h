#pragma once

#include <QColor>
#include <QSize>
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop::timeline_frame_shadow {

struct Parameters final {
  bool enabled = false;
  QColor color = QColor(QStringLiteral("#00000000"));
  double offset_x = 0.0;
  double offset_y = 0.0;
  double blur_radius = 0.0;
  double spread = 0.0;
};

QVariantMap default_parameters();
bool parse_parameters(const QVariantMap& map,
                      Parameters* parameters,
                      QString* error_message = nullptr);
bool is_default_parameters(const Parameters& parameters);
QVariantMap to_variant_map(const Parameters& parameters);

// Returns a filtergraph branch that consumes the current unlabelled RGBA frame
// and emits a labeled, padded RGBA frame with its CSS-style shadow behind it.
// Shadow dimensions are authored in sequence pixels and are scaled to target.
// Set high_precision_compositing when the caller rewrites RGBA formats to
// rgba64le so the constant shadow color uses the matching 16-bit channel range.
QString ffmpeg_filter(const Parameters& parameters,
                      const QSize& frame_size,
                      const QSize& sequence_size,
                      const QSize& target_size,
                      const QString& unique_prefix,
                      const QString& output_label,
                      QSize* padded_frame_size = nullptr,
                      QString* error_message = nullptr,
                      bool high_precision_compositing = false);

}  // namespace photo_editor::desktop::timeline_frame_shadow
