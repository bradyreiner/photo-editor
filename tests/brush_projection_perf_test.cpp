#include "photo_canvas_coordinates.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>
using namespace photo_editor::desktop;
using namespace photo_editor::pipeline;
namespace pipeline = photo_editor::pipeline;
namespace {
// Use the same harness with the preceding signature when comparing headers.
template <typename... Arguments>
QImage transient_projection(Arguments&&... arguments) {
  if constexpr (requires { project_mask_canvas_preview(std::forward<Arguments>(arguments)..., true); })
    return project_mask_canvas_preview(std::forward<Arguments>(arguments)..., true);
  else
    return project_mask_canvas_preview(std::forward<Arguments>(arguments)...);
}
Q_NEVER_INLINE QImage reference_projection(
    const QImage& input, const QSize& output_size,
    const pipeline::CompiledGeometryStageProgram& program,
    const QSize& source_size, const QRect& crop, const QRect& basis,
    const QRect& output_rect = {}, const QSize& full_output_size = {}) {
  if (input.isNull() || output_size.isEmpty() || basis.isEmpty()) return {};
  const auto full_size = full_output_size.isEmpty() ? output_size : full_output_size;
  const auto region = output_rect.isEmpty() ? QRect(QPoint(), output_size) : output_rect;
  if (region.size() != output_size || full_size.isEmpty()) return {};
  if (!program.active && basis == crop && input.size() == output_size &&
      region == QRect(QPoint(), full_size)) return input;
  const auto source = input.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  auto output = QImage(output_size, QImage::Format_ARGB32_Premultiplied);
  output.fill(Qt::transparent);
  for (int y = 0; y < output.height(); ++y) {
    auto* row = reinterpret_cast<QRgb*>(output.scanLine(y));
    for (int x = 0; x < output.width(); ++x) {
      const auto mapped = map_canvas_coordinate(program, source_size, crop,
          {(region.x() + x + 0.5) / full_size.width(),
           (region.y() + y + 0.5) / full_size.height()}, false,
          CanvasCoordinateSpace::SourceMask);
      const auto sample_x = (mapped.point.x() * std::max(1, source_size.width() - 1) - basis.x()) /
          std::max(1, basis.width() - 1) * std::max(0, source.width() - 1);
      const auto sample_y = (mapped.point.y() * std::max(1, source_size.height() - 1) - basis.y()) /
          std::max(1, basis.height() - 1) * std::max(0, source.height() - 1);
      const auto left = static_cast<int>(std::floor(sample_x));
      const auto top = static_cast<int>(std::floor(sample_y));
      const auto fx = sample_x - left;
      const auto fy = sample_y - top;
      double red = 0, green = 0, blue = 0, alpha = 0;
      for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
        const auto sx = left + dx;
        const auto sy = top + dy;
        if (sx < 0 || sy < 0 || sx >= source.width() || sy >= source.height()) continue;
        const auto weight = (dx == 0 ? 1 - fx : fx) * (dy == 0 ? 1 - fy : fy);
        const auto pixel = reinterpret_cast<const QRgb*>(source.constScanLine(sy))[sx];
        red += qRed(pixel) * weight; green += qGreen(pixel) * weight;
        blue += qBlue(pixel) * weight; alpha += qAlpha(pixel) * weight;
      }
      row[x] = qRgba(qRound(red), qRound(green), qRound(blue), qRound(alpha));
    }
  }
  return output;
}

bool verify_exact_projection_case(
    const QString& name, const QImage& input, const QSize& output_size,
    const pipeline::CompiledGeometryStageProgram& program,
    const QSize& source_size, const QRect& crop, const QRect& basis,
    const QRect& output_rect, const QSize& full_output_size,
    QJsonArray* results) {
  const auto expected = reference_projection(input, output_size, program,
      source_size, crop, basis, output_rect, full_output_size);
  const auto actual = transient_projection(input, output_size, program,
      source_size, crop, basis, output_rect, full_output_size);
  if (actual.isNull() || actual != expected) {
    std::cerr << "projection exact guard mismatch case=" << name.toStdString() << '\n';
    return false;
  }
  if (results != nullptr) {
    results->append(QJsonObject{{"name", name}, {"exact_pixels", true},
        {"input_size", QStringLiteral("%1x%2").arg(input.width()).arg(input.height())},
        {"output_size", QStringLiteral("%1x%2").arg(output_size.width()).arg(output_size.height())}});
  }
  return true;
}

void set_pixel(QImage* image, int x, int y, QRgb pixel) {
  auto* row = reinterpret_cast<QRgb*>(image->scanLine(y));
  row[x] = pixel;
}

QImage sparse_brush_preview(const QSize& size) {
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  const auto minimum_edge = std::min(size.width(), size.height());
  const auto radius_x = std::max(4, minimum_edge / 64);
  const auto radius_y = std::max(3, minimum_edge / 96);
  constexpr int kDabCount = 19;
  for (int dab = 0; dab < kDabCount; ++dab) {
    const auto t = static_cast<double>(dab) / (kDabCount - 1);
    const auto center_x = qRound((0.36 + 0.28 * t) * (size.width() - 1));
    const auto center_y = qRound((0.5 + 0.04 * std::sin(t * 2.0 * std::acos(-1.0))) *
        (size.height() - 1));
    const auto min_x = std::max(0, center_x - radius_x);
    const auto max_x = std::min(size.width() - 1, center_x + radius_x);
    const auto min_y = std::max(0, center_y - radius_y);
    const auto max_y = std::min(size.height() - 1, center_y + radius_y);
    for (int y = min_y; y <= max_y; ++y) {
      auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
      for (int x = min_x; x <= max_x; ++x) {
        const auto dx = static_cast<double>(x - center_x) / radius_x;
        const auto dy = static_cast<double>(y - center_y) / radius_y;
        const auto distance = std::hypot(dx, dy);
        if (distance > 1.0) continue;
        const auto alpha = qRound(220.0 * (1.0 - distance) * (1.0 - distance));
        if (alpha <= 0) continue;
        const auto color = qRgba(55 + dab * 5, 110 + dab * 3, 210 - dab * 4, alpha);
        row[x] = qPremultiply(color);
      }
    }
  }
  return image;
}

bool verify_sparse_projection_cases(QJsonArray* results) {
  const QSize zero_size(83, 59);
  const QRect zero_crop(QPoint(), zero_size);
  const auto zero_program = compile_geometry_stage_program(
      ColorAdjustmentState{}, zero_crop.width(), zero_crop.height());
  QImage all_zero(zero_size, QImage::Format_ARGB32_Premultiplied);
  all_zero.fill(Qt::transparent);
  if (!verify_exact_projection_case(QStringLiteral("sparse_all_zero_fractional_roi"),
          all_zero, QSize(91, 76), zero_program, zero_size, zero_crop, zero_crop,
          QRect(13, 5, 91, 76), QSize(166, 118), results)) return false;

  const QSize sparse_size(97, 67);
  const QRect sparse_crop(QPoint(), sparse_size);
  const auto sparse_program = compile_geometry_stage_program(
      ColorAdjustmentState{}, sparse_crop.width(), sparse_crop.height());
  QImage disconnected(sparse_size, QImage::Format_ARGB32_Premultiplied);
  disconnected.fill(Qt::transparent);
  set_pixel(&disconnected, 10, 8, qPremultiply(qRgba(230, 24, 18, 240)));
  set_pixel(&disconnected, 16, 10, qPremultiply(qRgba(22, 215, 45, 190)));
  set_pixel(&disconnected, 47, 28, qPremultiply(qRgba(30, 55, 235, 255)));
  set_pixel(&disconnected, 80, 55, qPremultiply(qRgba(220, 180, 20, 160)));
  if (!verify_exact_projection_case(QStringLiteral("sparse_disconnected_fractional_upscale_roi"),
          disconnected, QSize(105, 81), sparse_program, sparse_size, sparse_crop, sparse_crop,
          QRect(20, 12, 105, 81), QSize(194, 134), results)) return false;

  const QSize edge_size(41, 31);
  const QRect edge_crop(QPoint(), edge_size);
  const auto edge_program = compile_geometry_stage_program(
      ColorAdjustmentState{}, edge_crop.width(), edge_crop.height());
  QImage top_left_edge(edge_size, QImage::Format_ARGB32_Premultiplied);
  top_left_edge.fill(Qt::transparent);
  set_pixel(&top_left_edge, 0, 0, qPremultiply(qRgba(250, 40, 30, 255)));
  if (!verify_exact_projection_case(QStringLiteral("sparse_top_left_bilinear_edge_fringe"),
          top_left_edge, QSize(45, 36), edge_program, edge_size, edge_crop, edge_crop,
          QRect(0, 0, 45, 36), QSize(82, 62), results)) return false;
  QImage bottom_right_edge(edge_size, QImage::Format_ARGB32_Premultiplied);
  bottom_right_edge.fill(Qt::transparent);
  set_pixel(&bottom_right_edge, edge_size.width() - 1, edge_size.height() - 1,
      qPremultiply(qRgba(20, 70, 245, 255)));
  if (!verify_exact_projection_case(QStringLiteral("sparse_bottom_right_bilinear_edge_fringe"),
          bottom_right_edge, QSize(45, 36), edge_program, edge_size, edge_crop, edge_crop,
          QRect(37, 26, 45, 36), QSize(82, 62), results)) return false;

  const QSize transparent_rgb_size(67, 43);
  const QRect transparent_rgb_crop(QPoint(), transparent_rgb_size);
  const auto transparent_rgb_program = compile_geometry_stage_program(
      ColorAdjustmentState{}, transparent_rgb_crop.width(), transparent_rgb_crop.height());
  QImage transparent_rgb(transparent_rgb_size, QImage::Format_ARGB32_Premultiplied);
  transparent_rgb.fill(Qt::transparent);
  // Deliberately exercise noncanonical premultiplied data: the production
  // sampler preserves RGB accumulation even when the stored alpha is zero.
  set_pixel(&transparent_rgb, 22, 14, qRgba(17, 33, 65, 0));
  if (!verify_exact_projection_case(QStringLiteral("sparse_nonzero_rgb_zero_alpha"),
          transparent_rgb, QSize(98, 67), transparent_rgb_program,
          transparent_rgb_size, transparent_rgb_crop, transparent_rgb_crop,
          QRect(17, 8, 98, 67), QSize(134, 86), results)) return false;

  const QSize corners_size(53, 41);
  const QRect corners_crop(QPoint(), corners_size);
  const auto corners_program = compile_geometry_stage_program(
      ColorAdjustmentState{}, corners_crop.width(), corners_crop.height());
  QImage four_corners(corners_size, QImage::Format_ARGB32_Premultiplied);
  four_corners.fill(Qt::transparent);
  set_pixel(&four_corners, 0, 0, qPremultiply(qRgba(230, 15, 30, 220)));
  set_pixel(&four_corners, corners_size.width() - 1, 0, qPremultiply(qRgba(20, 220, 45, 220)));
  set_pixel(&four_corners, 0, corners_size.height() - 1, qPremultiply(qRgba(30, 50, 235, 220)));
  set_pixel(&four_corners, corners_size.width() - 1, corners_size.height() - 1,
      qPremultiply(qRgba(225, 185, 20, 220)));
  if (!verify_exact_projection_case(QStringLiteral("sparse_four_nonzero_corners_full_bounds"),
          four_corners, QSize(106, 82), corners_program, corners_size,
          corners_crop, corners_crop, QRect(QPoint(), QSize(106, 82)),
          QSize(106, 82), results)) return false;

  const QSize downsample_size(151, 109);
  const QRect downsample_crop(QPoint(), downsample_size);
  const auto downsample_program = compile_geometry_stage_program(
      ColorAdjustmentState{}, downsample_crop.width(), downsample_crop.height());
  auto downsample_sparse = sparse_brush_preview(downsample_size);
  if (!verify_exact_projection_case(QStringLiteral("sparse_downsample_fallback_roi"),
          downsample_sparse, QSize(86, 62), downsample_program,
          downsample_size, downsample_crop, downsample_crop,
          QRect(8, 9, 86, 62), QSize(108, 79), results)) return false;
  return true;
}

QJsonArray json_ns_samples(const QVector<qint64>& samples) {
  QJsonArray result;
  for (const auto sample : samples) result.append(static_cast<double>(sample));
  return result;
}

QString scenario_name(int scenario) {
  switch (scenario) {
    case 0: return QStringLiteral("dense_bbox_upscale_1800x1200_to_2560x1707");
    case 1: return QStringLiteral("dense_bbox_downsample_guard");
    case 2: return QStringLiteral("dense_bbox_output_roi_guard");
    case 3: return QStringLiteral("dense_bbox_crop_output_roi_guard");
    case 4: return QStringLiteral("dense_bbox_active_geometry_guard");
    case 5: return QStringLiteral("dense_bbox_identity_fast_path_guard");
    case 6: return QStringLiteral("dense_bbox_degenerate_axis_guard");
    case 7: return QStringLiteral("sparse_brush_1800x1200_to_2560x1707");
    case 8: return QStringLiteral("sparse_brush_1800x1200_to_3108x2072");
    default: return QStringLiteral("unknown");
  }
}
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QJsonArray cases;
  for (int scenario = 0; scenario < 9; ++scenario) {
    QSize source_size(1800, 1200), authored_size(1800, 1200), full(2560, 1707);
    QRect crop(QPoint(), source_size), basis = crop, region(QPoint(), full);
    ColorAdjustmentState state;
    if (scenario == 1) { full = QSize(720, 480); region = QRect(QPoint(), full); }
    if (scenario == 2) { full = QSize(2560, 1707); region = QRect(317, 99, 619, 453); }
    if (scenario == 3) { crop = QRect(123, 77, 1500, 1000); authored_size = QSize(512, 341); full = QSize(800, 533); region = QRect(37, 19, 629, 479); }
    if (scenario == 4) { state.transform_rotation = .13; full = QSize(640, 427); region = QRect(QPoint(), full); }
    if (scenario == 5) { full = source_size; region = QRect(QPoint(), full); }
    if (scenario == 6) { source_size = QSize(1, 37); authored_size = source_size; crop = QRect(QPoint(), source_size); basis = crop; full = QSize(7, 149); region = QRect(QPoint(), full); }
    if (scenario == 8) { full = QSize(3108, 2072); region = QRect(QPoint(), full); }
    const auto program = compile_geometry_stage_program(state, crop.width(), crop.height());
    QImage input(authored_size, QImage::Format_ARGB32_Premultiplied);
    if (scenario >= 7) {
      input = sparse_brush_preview(authored_size);
    } else {
      for (int y = 0; y < input.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(input.scanLine(y));
        for (int x = 0; x < input.width(); ++x)
          row[x] = qPremultiply(qRgba((x * 71 + y * 29) % 256, (x * 31 + y * 13) % 256,
                                     (x * 11 + y * 19) % 256, (x * 17 + y * 7) % 256));
      }
    }
    QVector<qint64> before, after;
    QByteArray hash;
    for (int trial = 0; trial < 7; ++trial) {
      QImage reference, candidate;
      const auto run_before = [&] {
        QElapsedTimer timer; timer.start();
        reference = reference_projection(input, region.size(), program, source_size, crop, basis, region, full);
        if (trial > 1) before.append(timer.nsecsElapsed());
      };
      const auto run_after = [&] {
        QElapsedTimer timer; timer.start();
        candidate = transient_projection(input, region.size(), program, source_size, crop, basis, region, full);
        if (trial > 1) after.append(timer.nsecsElapsed());
      };
      if (trial % 2) { run_after(); run_before(); } else { run_before(); run_after(); }
      if (candidate.isNull() || candidate != reference) {
        std::cerr << "projection pixel mismatch scenario " << scenario << '\n'; return 1;
      }
      hash = QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(candidate.constBits()), candidate.sizeInBytes()), QCryptographicHash::Sha256).toHex();
    }
    auto before_sorted = before;
    auto after_sorted = after;
    std::sort(before_sorted.begin(), before_sorted.end());
    std::sort(after_sorted.begin(), after_sorted.end());
    cases.append(QJsonObject{{"scenario", scenario}, {"case_name", scenario_name(scenario)},
        {"before_ns", before_sorted[2]}, {"after_ns", after_sorted[2]},
        {"before_ns_samples", json_ns_samples(before)},
        {"after_ns_samples", json_ns_samples(after)},
        {"rgba_sha256", QString::fromLatin1(hash)}, {"exact_pixels", true}});
  }
  QJsonArray exact_guard_cases;
  if (!verify_sparse_projection_cases(&exact_guard_cases)) return 1;
  std::cout << QJsonDocument(QJsonObject{{"cases", cases},
      {"exact_guard_cases", exact_guard_cases}}).toJson(QJsonDocument::Compact).constData() << '\n';
}
