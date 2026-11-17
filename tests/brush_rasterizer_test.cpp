#include "brush_rasterizer.h"
#include "brush_live_preview_raster.h"
#include <QElapsedTimer>

#include <atomic>
#include <cstdlib>
#include <iostream>

#include <QColor>
#include <QColorSpace>
#include <QtGui/qrgbafloat.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QVariantMap zero_mask(int width, int height) {
  QVariantMap mask;
  mask.insert(QStringLiteral("segmentationData"), QByteArray(width * height, char(0)));
  mask.insert(QStringLiteral("segmentationWidth"), width);
  mask.insert(QStringLiteral("segmentationHeight"), height);
  return mask;
}

unsigned char alpha_at(const QVariantMap& mask, int x, int y) {
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  return static_cast<unsigned char>(data.at(y * width + x));
}

double clamp_unit(double value) {
  return std::clamp(value, 0.0, 1.0);
}

double legacy_smoothstep(double edge0, double edge1, double value) {
  if (edge0 == edge1) return value < edge0 ? 0.0 : 1.0;
  const auto t = clamp_unit((value - edge0) / (edge1 - edge0));
  return t * t * (3.0 - 2.0 * t);
}

QRect legacy_circular_brush_raster(
    QByteArray* alpha,
    const QSize& size,
    double normalized_x,
    double normalized_y,
    double brush_size,
    double brush_flow,
    double brush_opacity,
    double feather,
    double hardness,
    bool erase) {
  const auto width = size.width();
  const auto height = size.height();
  const auto softness = std::clamp(feather, 0.0, 1.0);
  const auto inner_scale = (1.0 - softness) * clamp_unit(hardness);
  const auto clamped_size = std::max(0.01, brush_size);
  const auto clamped_flow = clamp_unit(brush_flow) * clamp_unit(brush_opacity);
  const auto center_x = clamp_unit(normalized_x);
  const auto center_y = clamp_unit(normalized_y);
  const auto pixel_radius = clamped_size *
      static_cast<double>(std::max(1, std::min(width - 1, height - 1)));
  const auto radius = std::max(2, static_cast<int>(std::ceil(pixel_radius)));
  const auto anchor_x = center_x * static_cast<double>(std::max(width - 1, 1));
  const auto anchor_y = center_y * static_cast<double>(std::max(height - 1, 1));
  const auto min_x = std::max(0, static_cast<int>(std::floor(anchor_x)) - radius);
  const auto max_x = std::min(width - 1, static_cast<int>(std::ceil(anchor_x)) + radius);
  const auto min_y = std::max(0, static_cast<int>(std::floor(anchor_y)) - radius);
  const auto max_y = std::min(height - 1, static_cast<int>(std::ceil(anchor_y)) + radius);
  auto changed = QRect{};

  for (int y = min_y; y <= max_y; ++y) {
    for (int x = min_x; x <= max_x; ++x) {
      const auto pixel_x = width > 1
          ? static_cast<double>(x) / static_cast<double>(width - 1)
          : 0.5;
      const auto pixel_y = height > 1
          ? static_cast<double>(y) / static_cast<double>(height - 1)
          : 0.5;
      const auto delta_x = pixel_x - center_x;
      const auto delta_y = pixel_y - center_y;
      const auto distance = std::hypot(
          delta_x * static_cast<double>(width - 1),
          delta_y * static_cast<double>(height - 1)) / pixel_radius;
      const auto tip_weight = (1.0 - inner_scale) * pixel_radius < 1.0
          ? 1.0 - legacy_smoothstep(
                      inner_scale * pixel_radius - 0.5,
                      pixel_radius + 0.5,
                      distance * pixel_radius)
          : 1.0 - legacy_smoothstep(inner_scale, 1.0, distance);
      const auto weight = clamped_flow * tip_weight;
      if (weight <= 1e-6) continue;

      const auto index = y * width + x;
      const auto existing = static_cast<unsigned char>(alpha->at(index));
      const auto next_value = erase
          ? static_cast<char>(std::lround(
                static_cast<double>(existing) * (1.0 - clamp_unit(weight))))
          : static_cast<char>(std::max(
                static_cast<int>(existing),
                static_cast<int>(std::lround(clamp_unit(weight) * 255.0))));
      if (next_value == alpha->at(index)) continue;
      (*alpha)[index] = next_value;
      const auto pixel_rect = QRect(x, y, 1, 1);
      changed = changed.isNull() ? pixel_rect : changed.united(pixel_rect);
    }
  }
  return changed;
}

struct CircularRasterCase final {
  QSize size;
  double x = 0.5;
  double y = 0.5;
  double brush_size = 0.06;
  double flow = 1.0;
  double opacity = 1.0;
  double feather = 0.18;
  double hardness = 0.45;
  bool erase = false;
  bool seed_existing_alpha = false;
};

bool circular_raster_matches_legacy(const CircularRasterCase& test_case) {
  auto actual = QByteArray(test_case.size.width() * test_case.size.height(), char(0));
  auto expected = actual;
  if (test_case.seed_existing_alpha) {
    for (qsizetype index = 0; index < actual.size(); ++index) {
      const auto value = static_cast<unsigned char>(
          (static_cast<std::uint64_t>(index) * 37U + 19U) % 256U);
      actual[index] = static_cast<char>(value);
      expected[index] = static_cast<char>(value);
    }
  }
  const auto expected_rect = legacy_circular_brush_raster(
      &expected,
      test_case.size,
      test_case.x,
      test_case.y,
      test_case.brush_size,
      test_case.flow,
      test_case.opacity,
      test_case.feather,
      test_case.hardness,
      test_case.erase);
  const auto actual_rect = photo_editor::desktop::rasterize_brush_point_into_alpha(
      &actual,
      test_case.size.width(),
      test_case.size.height(),
      nullptr,
      test_case.x,
      test_case.y,
      test_case.brush_size,
      test_case.flow,
      test_case.opacity,
      test_case.feather,
      test_case.erase,
      false,
      0.18,
      -1.0,
      -1.0,
      false,
      {},
      nullptr,
      nullptr,
      nullptr,
      {.circular = true, .hardness = test_case.hardness});
  if (actual_rect != expected_rect) {
    std::cerr << "legacy parity dirty rect mismatch size="
              << test_case.size.width() << 'x' << test_case.size.height()
              << " center=" << test_case.x << ',' << test_case.y
              << " brush_size=" << test_case.brush_size
              << " expected=" << expected_rect.x() << ',' << expected_rect.y()
              << ':' << expected_rect.width() << 'x' << expected_rect.height()
              << " actual=" << actual_rect.x() << ',' << actual_rect.y()
              << ':' << actual_rect.width() << 'x' << actual_rect.height() << '\n';
    return false;
  }
  for (qsizetype index = 0; index < actual.size(); ++index) {
    if (actual.at(index) == expected.at(index)) continue;
    const auto width = test_case.size.width();
    std::cerr << "legacy parity alpha mismatch size=" << width << 'x'
              << test_case.size.height() << " center=" << test_case.x << ','
              << test_case.y << " brush_size=" << test_case.brush_size
              << " flow=" << test_case.flow << " opacity=" << test_case.opacity
              << " feather=" << test_case.feather
              << " hardness=" << test_case.hardness << " erase=" << test_case.erase
              << " pixel=" << index % width << ',' << index / width
              << " expected=" << static_cast<int>(static_cast<unsigned char>(expected.at(index)))
              << " actual=" << static_cast<int>(static_cast<unsigned char>(actual.at(index)))
              << '\n';
    return false;
  }
  return true;
}

qint64 median_ns(std::vector<qint64> values) {
  std::sort(values.begin(), values.end());
  return values.at(values.size() / 2);
}

struct CircularRasterTiming final {
  double legacy_ms = 0.0;
  double candidate_ms = 0.0;
};

CircularRasterTiming benchmark_circular_raster(const CircularRasterCase& test_case) {
  constexpr int kTrials = 9;
  auto alpha = QByteArray(test_case.size.width() * test_case.size.height(), char(0));
  std::vector<qint64> legacy_ns;
  std::vector<qint64> candidate_ns;
  legacy_ns.reserve(kTrials);
  candidate_ns.reserve(kTrials);
  const auto run = [&](bool legacy) {
    alpha.fill(char(0));
    QElapsedTimer timer;
    timer.start();
    if (legacy) {
      static_cast<void>(legacy_circular_brush_raster(
          &alpha,
          test_case.size,
          test_case.x,
          test_case.y,
          test_case.brush_size,
          test_case.flow,
          test_case.opacity,
          test_case.feather,
          test_case.hardness,
          test_case.erase));
    } else {
      static_cast<void>(photo_editor::desktop::rasterize_brush_point_into_alpha(
          &alpha,
          test_case.size.width(),
          test_case.size.height(),
          nullptr,
          test_case.x,
          test_case.y,
          test_case.brush_size,
          test_case.flow,
          test_case.opacity,
          test_case.feather,
          test_case.erase,
          false,
          0.18,
          -1.0,
          -1.0,
          false,
          {},
          nullptr,
          nullptr,
          nullptr,
          {.circular = true, .hardness = test_case.hardness}));
    }
    return timer.nsecsElapsed();
  };
  for (int trial = 0; trial < kTrials; ++trial) {
    if (trial % 2 == 0) {
      legacy_ns.push_back(run(true));
      candidate_ns.push_back(run(false));
    } else {
      candidate_ns.push_back(run(false));
      legacy_ns.push_back(run(true));
    }
  }
  return CircularRasterTiming{
      .legacy_ms = static_cast<double>(median_ns(std::move(legacy_ns))) / 1'000'000.0,
      .candidate_ms = static_cast<double>(median_ns(std::move(candidate_ns))) / 1'000'000.0,
  };
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  bool cancelled = true;
  QVariantMap invalid;
  if (rasterize_brush_point_into_mask(
          nullptr, nullptr, 0.5, 0.5, 0.25, 1.0, 1.0, 0.05)
          .isValid() ||
      rasterize_brush_point_into_mask(
          &invalid, nullptr, 0.5, 0.5, 0.25, 1.0, 1.0, 0.05, false,
          false, 0.18, -1.0, -1.0, false, {}, &cancelled)
          .isValid() ||
      cancelled) {
    return fail("brush rasterization must reject absent payloads and normalize cancellation output");
  }

  auto mask = zero_mask(9, 9);
  const auto painted = rasterize_brush_point_into_mask(
      &mask, nullptr, 0.5, 0.5, 0.25, 1.0, 1.0, 0.05);
  if (painted != QRect(3, 3, 3, 3) || alpha_at(mask, 4, 4) != 255U ||
      alpha_at(mask, 3, 3) != 255U || alpha_at(mask, 2, 4) != 0U ||
      alpha_at(mask, 0, 0) != 0U) {
    return fail("ordinary brush rasterization must publish the exact bounded opaque dab");
  }
  const auto erased = rasterize_brush_point_into_mask(
      &mask, nullptr, 0.5, 0.5, 0.25, 0.5, 1.0, 0.05, true);
  if (erased != QRect(3, 3, 3, 3) || alpha_at(mask, 4, 4) != 128U) {
    return fail("erase rasterization must attenuate existing alpha with the exact flow weight");
  }

  QImage split(9, 9, QImage::Format_ARGB32);
  for (int y = 0; y < split.height(); ++y) {
    for (int x = 0; x < split.width(); ++x) {
      split.setPixelColor(x, y, x < 5 ? QColor(220, 35, 25) : QColor(25, 45, 220));
    }
  }
  auto auto_mask = zero_mask(9, 9);
  qint64 candidates = 0;
  qint64 edge_evaluations = 0;
  const auto auto_changed = rasterize_brush_point_into_mask(
      &auto_mask, &split, 0.5, 0.5, 0.5, 1.0, 1.0, 0.05, false, true,
      0.05, 0.25, 0.5, true, {}, nullptr, &candidates, &edge_evaluations);
  if (!auto_changed.isValid() || alpha_at(auto_mask, 3, 4) == 0U ||
      alpha_at(auto_mask, 6, 4) != 0U || candidates <= 0 ||
      edge_evaluations <= 0 || candidates <= edge_evaluations) {
    return fail("auto-mask rasterization must admit matching color and reject the split region");
  }
  auto edge_only=zero_mask(9,9);
  auto unprotected=zero_mask(9,9);
  (void)rasterize_brush_point_into_mask(&edge_only,&split,0.5,0.5,0.5,1.0,1.0,0.05,
      false,false,0.05,0.25,0.5,true);
  (void)rasterize_brush_point_into_mask(&unprotected,nullptr,0.5,0.5,0.5,1.0,1.0,0.05);
  if (alpha_at(edge_only,3,4)==0U || alpha_at(edge_only,6,4)!=0U ||
      alpha_at(unprotected,6,4)!=255U)
    return fail("Protect Edges must block a strong color boundary with Auto Mask disabled");
  const auto before_missing_source=edge_only;
  (void)rasterize_brush_point_into_mask(&edge_only,nullptr,0.5,0.5,0.5,1.0,1.0,0.05,
      false,false,0.05,0.25,0.5,true);
  if (edge_only!=before_missing_source)
    return fail("A source-aware stroke must never fall back to unfiltered coverage when decode is unavailable");
  QImage line_source(1,9,QImage::Format_ARGB32);
  for (int y=0;y<9;++y) line_source.setPixelColor(0,y,y<5 ? QColor(220,35,25) : QColor(25,45,220));
  auto line_mask=zero_mask(1,9);
  (void)rasterize_brush_point_into_mask(&line_mask,&line_source,0.5,0.5,0.5,1.0,1.0,0.05,
      false,true,0.05,0.5,0.25,true);
  if (alpha_at(line_mask,0,3)==0U || alpha_at(line_mask,0,6)!=0U)
    return fail("Source-aware brush filtering must support one-pixel-wide sources");
  QImage hdr_source(9,9,QImage::Format_RGBA32FPx4);
  QImage hdr_premultiplied(9,9,QImage::Format_RGBA32FPx4_Premultiplied);
  hdr_source.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
  hdr_premultiplied.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
  for (int y=0;y<9;++y) for (int x=0;x<9;++x) {
    const auto color=x<5 ? 1.5F : 3.0F;
    reinterpret_cast<QRgbaFloat32*>(hdr_source.scanLine(y))[x]={color,color,color,0.25F};
    reinterpret_cast<QRgbaFloat32*>(hdr_premultiplied.scanLine(y))[x]=
        {color*0.25F,color*0.25F,color*0.25F,0.25F};
  }
  auto hdr_mask=zero_mask(9,9),premultiplied_mask=zero_mask(9,9);
  for (auto pair:{std::pair{&hdr_source,&hdr_mask},std::pair{&hdr_premultiplied,&premultiplied_mask}})
    (void)rasterize_brush_point_into_mask(pair.second,pair.first,0.5,0.5,0.5,1.0,1.0,0.05,
        false,true,0.05,0.25,0.5,true);
  if (alpha_at(hdr_mask,3,4)==0U || alpha_at(hdr_mask,6,4)!=0U || hdr_mask!=premultiplied_mask)
    return fail("Auto Mask must distinguish two above-white HDR colors and unpremultiply float source colors");

  std::atomic<int> latest_generation{2};
  auto cancelled_mask = zero_mask(9, 9);
  cancelled = false;
  const auto cancelled_rect = rasterize_brush_point_into_mask(
      &cancelled_mask, nullptr, 0.5, 0.5, 0.25, 1.0, 1.0, 0.05,
      false, false, 0.18, -1.0, -1.0, false,
      photo_editor::pipeline::CancellationToken{&latest_generation, 1},
      &cancelled);
  if (cancelled_rect.isValid() || !cancelled || alpha_at(cancelled_mask, 4, 4) != 0U) {
    return fail("cancelled brush rasterization must publish no partial mask mutation");
  }

  // The Selection live raster is circular and has no source-color sampling.
  // Keep a literal copy of its previous pixel formula as a byte-level oracle;
  // the fast path must retain both alpha bytes and the exact dirty rectangle.
  const auto just_above_half = std::nextafter(0.5, 1.0);
  const std::vector<CircularRasterCase> selection_cases{
      {QSize(2560, 1675), 0.31, 0.36, 0.06, 1.0, 1.0, 0.18, 0.45},
      {QSize(512, 335), 0.31, 0.36, 0.06, 1.0, 1.0, 0.18, 0.45},
      // Degenerate axes retain the legacy normalized-distance behavior.
      {QSize(1, 37), 0.82, 0.41, 0.1, 0.7, 0.8, 0.2, 0.4},
      {QSize(29, 1), 0.41, 0.82, 0.1, 0.7, 0.8, 0.2, 0.4},
      // A center-aligned half-alpha tie exercises lround's half-away rule.
      {QSize(513, 337), 0.5, 0.5, 0.12, 0.5, 1.0, 0.0, 1.0},
      // Half-pixel and neighboring representable centers stress algebraic
      // rearrangement around byte quantization boundaries.
      {QSize(2560, 1675), 0.5002, 0.5003, 0.06, 0.37, 0.83, 0.32, 0.2},
      {QSize(2560, 1675), just_above_half, std::nextafter(0.5, 0.0),
       0.075, 0.5, 1.0, 0.55, 0.45},
      {QSize(512, 335), 0.999, 0.001, 0.08, 0.35, 0.65, 0.0, 0.0},
      {QSize(801, 401), 0.3333333333333333, 0.6666666666666666,
       0.2, 0.9, 0.71, 1.0, 0.7},
      {QSize(401, 801), 0.123456789, 0.987654321,
       0.01, 0.63, 0.92, 0.25, 0.6},
      {QSize(2560, 1675), 0.5002, 0.5003, 0.06, 0.37, 0.83, 0.32, 0.2,
       true, true},
  };
  for (const auto& test_case : selection_cases) {
    if (!circular_raster_matches_legacy(test_case)) {
      return fail("Optimized Selection raster must preserve legacy alpha bytes and dirty bounds.");
    }
  }

  // Sweep rational pixel anchors and one-ULP neighbors. At an integer radial
  // boundary the old hard-tip AA formula produces 127.5 before lround; this
  // catches reassociation errors that can turn a half-byte into 127 or 128.
  const std::vector<QSize> tie_sizes{
      QSize(18, 14), QSize(19, 15), QSize(32, 21),
      QSize(33, 22), QSize(65, 48), QSize(64, 49)};
  const auto flow_below_half = std::nextafter(0.5, 0.0);
  const auto flow_above_half = std::nextafter(0.5, 1.0);
  const std::vector<double> half_flows{
      flow_below_half, 0.5, flow_above_half};
  for (const auto& size : tie_sizes) {
    const auto width = size.width();
    const auto height = size.height();
    const auto center_pixel_x = (width - 1) / 2;
    const auto center_pixel_y = (height - 1) / 2;
    const auto center_x = static_cast<double>(center_pixel_x) /
        static_cast<double>(width - 1);
    const auto center_y = static_cast<double>(center_pixel_y) /
        static_cast<double>(height - 1);
    const std::vector<double> x_centers{
        std::nextafter(center_x, 0.0), center_x, std::nextafter(center_x, 1.0)};
    const std::vector<double> y_centers{
        std::nextafter(center_y, 0.0), center_y, std::nextafter(center_y, 1.0)};
    const auto max_tie_radius = std::min({4, center_pixel_x, center_pixel_y});
    const auto minimum_extent = std::min(width - 1, height - 1);
    for (int radius = 1; radius <= max_tie_radius; ++radius) {
      const auto brush_size = static_cast<double>(radius) /
          static_cast<double>(minimum_extent);
      for (const auto x : x_centers) {
        for (const auto y : y_centers) {
          for (const auto flow : half_flows) {
            const CircularRasterCase tie_case{
                size, x, y, brush_size, flow, 1.0, 0.0, 1.0};
            if (!circular_raster_matches_legacy(tie_case)) {
              return fail("Optimized Selection raster must preserve exact-radius half-byte parity.");
            }
          }
        }
      }
    }
  }

  const auto large_selection_benchmark = benchmark_circular_raster(selection_cases.front());
  const auto immediate_selection_benchmark = benchmark_circular_raster(selection_cases.at(1));
  const auto improvement_percent = [](const CircularRasterTiming& timing) {
    return timing.legacy_ms > 0.0
        ? 100.0 * (timing.legacy_ms - timing.candidate_ms) / timing.legacy_ms
        : 0.0;
  };
  std::cout << "selection_live_raster_benchmark size=2560x1675 legacy_ms="
            << large_selection_benchmark.legacy_ms
            << " candidate_ms=" << large_selection_benchmark.candidate_ms
            << " improvement_percent=" << improvement_percent(large_selection_benchmark)
            << '\n';
  std::cout << "selection_immediate_raster_benchmark size=512x335 legacy_ms="
            << immediate_selection_benchmark.legacy_ms
            << " candidate_ms=" << immediate_selection_benchmark.candidate_ms
            << " improvement_percent=" << improvement_percent(immediate_selection_benchmark)
            << '\n';


  // Consecutive pointer batches must preserve earlier segments and their exact
  // alpha. The old preview recreated an empty mask for every batch.
  BrushLivePreviewRaster preview;
  const QSize preview_size(2560, 1280);
  const BrushPreviewStyle preview_style{.color = QColor(210, 85, 30), .feather = 0.5,
      .hardness = 0.25, .opacity = 0.4};
  const BrushPreviewPoint first{.x = 0.2, .y = 0.3, .size = 0.012, .flow = 0.5};
  const BrushPreviewPoint second{.x = 0.7, .y = 0.6, .size = 0.012, .flow = 0.8};
  preview.append(preview_size, {first}, preview_style);
  const auto first_pixel = preview.image.pixel(512, 384);
  const auto* allocation = preview.alpha.constData();
  const auto dirty = preview.append(preview_size, {second}, preview_style);
  if (preview.image.pixel(512, 384) != first_pixel || qAlpha(first_pixel) == 0 ||
      preview.alpha.constData() != allocation || dirty.width() >= 100 || dirty.height() >= 100)
    return fail("Live brush batches must retain earlier coverage and update only the new footprint");
  const auto pixel = preview.image.pixel(1791, 767);
  if (qRed(pixel) > qAlpha(pixel) || qGreen(pixel) > qAlpha(pixel) || qBlue(pixel) > qAlpha(pixel))
    return fail("Live brush output must contain premultiplied RGB");
  auto reference = zero_mask(preview_size.width(), preview_size.height());
  for (const auto& point : {first, second})
    (void)rasterize_brush_point_into_mask(&reference, nullptr, point.x, point.y,
        point.size, point.flow, point.opacity, preview_style.feather);
  if (preview.alpha != reference.value("segmentationData").toByteArray())
    return fail("Incremental live coverage must match committed brush rasterization");
  auto hidden_style = preview_style;
  hidden_style.opacity = 0;
  preview.append(preview_size, {}, hidden_style);
  if (qAlpha(preview.image.pixel(512, 384)) != 0 || qAlpha(preview.image.pixel(1791, 767)) != 0)
    return fail("Changing overlay mode must also restyle earlier segments");
  preview.append(preview_size, {}, preview_style);
  if (preview.image.pixel(512, 384) != first_pixel)
    return fail("Restoring overlay visibility must retain the complete stroke");

  // Compare warm incremental shading with the previous full-frame scan. Include
  // an image snapshot as the UI provider would retain, so COW costs are included.
  QElapsedTimer clock;
  clock.start();
  for (int i = 0; i < 80; ++i) {
    const auto snapshot = preview.image;
    auto point = first;
    point.x = 0.1 + static_cast<double>(i) * 0.009;
    point.y = 0.8;
    preview.append(preview_size, {point}, preview_style);
    if (snapshot.isNull()) return fail("Preview snapshot should remain valid");
  }
  const auto incremental_ns = clock.nsecsElapsed();
  clock.restart();
  QImage baseline(preview_size, QImage::Format_ARGB32_Premultiplied);
  for (int i = 0; i < 80; ++i) {
    auto point = first;
    point.x = 0.1 + static_cast<double>(i) * 0.009;
    point.y = 0.8;
    auto batch = zero_mask(preview_size.width(), preview_size.height());
    (void)rasterize_brush_point_into_mask(&batch, nullptr, point.x, point.y,
        point.size, point.flow, point.opacity, preview_style.feather);
    const auto alpha = batch.value("segmentationData").toByteArray();
    baseline.fill(Qt::transparent);
    for (int y = 0; y < baseline.height(); ++y) {
      auto* row = reinterpret_cast<QRgb*>(baseline.scanLine(y));
      for (int x = 0; x < baseline.width(); ++x) {
        const auto a = qRound(static_cast<unsigned char>(alpha.at(y * baseline.width() + x)) * preview_style.opacity);
        if (a) row[x] = qPremultiply(qRgba(210, 85, 30, a));
      }
    }
  }
  std::cout << "live_preview_80_batches_incremental_ms=" << incremental_ns / 1000000.0
            << " baseline_ms=" << clock.nsecsElapsed() / 1000000.0 << '\n';
  for (int fixture = 0; fixture < 3; ++fixture) {
    QImage source(65, 65, QImage::Format_ARGB32); source.setColorSpace(QColorSpace::SRgb);
    const QColor foreground = fixture == 1 ? QColor(125, 125, 125) : QColor(180, 65, 35);
    const QColor background = fixture == 1 ? QColor(110, 110, 110) : QColor(30, 95, 175);
    const auto inside = [&](int x, int y) { return fixture == 2 ? (x % 7 == 4 || std::abs(y - 32) <= 1) : std::abs(x - 32) <= fixture; };
    for (int y = 0; y < 65; ++y) for (int x = 0; x < 65; ++x) source.setPixelColor(x, y, inside(x, y) ? foreground : background);
    auto guided = zero_mask(65, 65), plain = zero_mask(65, 65);
    (void)rasterize_brush_point_into_mask(&guided, &source, .5, .5, .5, 1, 1, .05, false, true, .14, .5, .5, true);
    (void)rasterize_brush_point_into_mask(&plain, nullptr, .5, .5, .5, 1, 1, .05);
    double guided_spill = 0, plain_spill = 0; int samples = 0;
    for (int y = 4; y < 61; ++y) for (int x = 4; x < 61; ++x) if (!inside(x, y)) {
      guided_spill += alpha_at(guided, x, y); plain_spill += alpha_at(plain, x, y); ++samples;
    }
    if (alpha_at(guided, 32, 32) < 250 || guided_spill >= plain_spill)
      return fail("Edge-aware brush must retain thin foreground and reduce high/low-contrast and foliage-like background spill");
    std::cout << "edge-quality fixture=" << fixture << " guided-spill=" << guided_spill / samples / 255.0
        << " plain-spill=" << plain_spill / samples / 255.0 << '\n';
  }
  return EXIT_SUCCESS;
}
