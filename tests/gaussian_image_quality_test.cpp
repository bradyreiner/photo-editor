#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QTemporaryDir>
#include <QVariantMap>
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
using Moments = std::array<double, 5>;
constexpr int maximum_quality_edge = 8192;
bool supported_quality_size(QSize size) {
  return size.width() > 0 && size.height() > 0 &&
      size.width() <= maximum_quality_edge && size.height() <= maximum_quality_edge;
}
QImage load_quality_image(const QString& path) {
  QImageReader reader(path);
  // Inspect dimensions before decoding. Do not resize, auto-orient or transform
  // colors: evaluation inputs must already share the same pixel/camera domain.
  if (!supported_quality_size(reader.size())) return {};
  return reader.read();
}
// Independent CPU metric on stored display-referred RGB. This is not a
// colorimetric or perceptual-quality certification. Match the trainer's 11x11,
// sigma=1.5, zero-padded SSIM convention, including its edge pixels.
double ssim(const std::vector<Moments>& moments, int width, int height) {
  std::array<double, 11> weights{};
  double sum = 0;
  for (int i = -5; i <= 5; ++i) {
    const auto weight = std::exp(-static_cast<double>(i * i) / 4.5);
    weights[static_cast<size_t>(i + 5)] = weight;
    sum += weight;
  }
  for (auto& weight : weights) weight /= sum;
  std::vector<Moments> vertical(moments.size());
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
    auto& out = vertical[static_cast<size_t>(y * width + x)];
    for (int k = -5; k <= 5; ++k) {
      if (y + k < 0 || y + k >= height) continue;
      const auto& in = moments[static_cast<size_t>((y + k) * width + x)];
      for (size_t c = 0; c < out.size(); ++c) out[c] += in[c] * weights[static_cast<size_t>(k + 5)];
    }
  }
  double result = 0;
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
    Moments m{};
    for (int k = -5; k <= 5; ++k) {
      if (x + k < 0 || x + k >= width) continue;
      const auto& in = vertical[static_cast<size_t>(y * width + x + k)];
      for (size_t c = 0; c < m.size(); ++c) m[c] += in[c] * weights[static_cast<size_t>(k + 5)];
    }
    const auto xx = m[0] * m[0], yy = m[1] * m[1], xy = m[0] * m[1];
    result += ((2 * xy + 0.0001) * (2 * (m[4] - xy) + 0.0009)) /
        ((xx + yy + 0.0001) * (m[2] - xx + m[3] - yy + 0.0009));
  }
  return result / (width * height);
}
// Preserve the dense oracle's vertical-then-horizontal arithmetic and zero
// padding exactly, but retain only eleven source rows plus one vertical row.
// No tile boundaries, downsampling or image-sized double buffers are involved.
template <typename ReadRow>
double streamed_ssim(int width, int height, ReadRow read_row) {
  std::array<double, 11> weights{};
  double sum = 0;
  for (int k = -5; k <= 5; ++k) {
    const auto weight = std::exp(-static_cast<double>(k * k) / 4.5);
    weights[static_cast<size_t>(k + 5)] = weight;
    sum += weight;
  }
  for (auto& weight : weights) weight /= sum;
  const auto row_size = static_cast<size_t>(width);
  std::vector<Moments> ring(row_size * 11), vertical(row_size);
  double result = 0;
  int next_row = 0;
  for (int y = 0; y < height; ++y) {
    while (next_row < height && next_row <= y + 5) {
      read_row(next_row, ring.data() + static_cast<size_t>(next_row % 11) * row_size);
      ++next_row;
    }
    std::fill(vertical.begin(), vertical.end(), Moments{});
    for (int x = 0; x < width; ++x) {
      auto& out = vertical[static_cast<size_t>(x)];
      for (int k = -5; k <= 5; ++k) {
        if (y + k < 0 || y + k >= height) continue;
        const auto& in = ring[static_cast<size_t>((y + k) % 11) * row_size + static_cast<size_t>(x)];
        for (size_t c = 0; c < out.size(); ++c) out[c] += in[c] * weights[static_cast<size_t>(k + 5)];
      }
    }
    for (int x = 0; x < width; ++x) {
      Moments m{};
      for (int k = -5; k <= 5; ++k) {
        if (x + k < 0 || x + k >= width) continue;
        const auto& in = vertical[static_cast<size_t>(x + k)];
        for (size_t c = 0; c < m.size(); ++c) m[c] += in[c] * weights[static_cast<size_t>(k + 5)];
      }
      const auto xx = m[0] * m[0], yy = m[1] * m[1], xy = m[0] * m[1];
      result += ((2 * xy + 0.0001) * (2 * (m[4] - xy) + 0.0009)) /
          ((xx + yy + 0.0001) * (m[2] - xx + m[3] - yy + 0.0009));
    }
  }
  return result / (width * height);
}
QVariantMap compare(const QImage& reference, const QImage& candidate) {
  if (reference.isNull() || reference.size() != candidate.size() ||
      !supported_quality_size(reference.size()))
    return {{"ok", false}, {"error", "Images must be nonempty, equal-sized and at most 8192 pixels per edge."}};
  const auto a = reference.convertToFormat(QImage::Format_RGB32);
  const auto b = candidate.convertToFormat(QImage::Format_RGB32);
  const auto count = static_cast<size_t>(a.width()) * static_cast<size_t>(a.height());
  double squared = 0, score = 0;
  int maximum_code_error = 0;
  for (int channel = 0; channel < 3; ++channel) {
    score += streamed_ssim(a.width(), a.height(), [&](int y, Moments* row) {
      const auto* row_a = reinterpret_cast<const QRgb*>(a.constScanLine(y));
      const auto* row_b = reinterpret_cast<const QRgb*>(b.constScanLine(y));
      for (int x = 0; x < a.width(); ++x) {
        const auto av = static_cast<double>((row_a[x] >> (8 * channel)) & 255) / 255;
        const auto bv = static_cast<double>((row_b[x] >> (8 * channel)) & 255) / 255;
        maximum_code_error = std::max(maximum_code_error, std::abs(static_cast<int>((row_a[x] >> (8 * channel)) & 255) -
            static_cast<int>((row_b[x] >> (8 * channel)) & 255)));
        squared += (av - bv) * (av - bv);
        row[x] = {av, bv, av * av, bv * bv, av * bv};
      }
    });
  }
  const auto mse = squared / (static_cast<double>(count) * 3);
  return {{"ok", true}, {"width", a.width()}, {"height", a.height()},
      {"metric", "stored-rgb-ssim11-sigma1.5-zero-pad-v1"}, {"identical", mse == 0},
      {"maximumChannelCodeError", maximum_code_error},
      {"mse", mse}, {"psnrDb", mse == 0 ? QVariant() : QVariant(-10 * std::log10(mse))}, {"ssim", score / 3}};
}
bool streamed_ssim_test() {
  // Tiny dimensions exercise truncated halos; 11/22 boundaries exercise ring
  // reuse. This dense implementation deliberately remains an independent oracle.
  for (const int width : {1, 2, 5, 6, 10, 11, 12, 21, 22, 23, 37, 257}) {
    for (const int height : {1, 2, 5, 6, 10, 11, 12, 21, 22, 23, 37}) {
      std::vector<Moments> input(static_cast<size_t>(width * height));
      for (int i = 0; i < width * height; ++i) {
        const double a = static_cast<double>((i * 73 + 19) % 256) / 255;
        const double b = static_cast<double>((i * 113 + 47) % 256) / 255;
        input[static_cast<size_t>(i)] = {a, b, a * a, b * b, a * b};
      }
      int reads = 0;
      bool ordered = true;
      const double streamed = streamed_ssim(width, height, [&](int y, Moments* row) {
        ordered = ordered && y == reads++;
        std::copy_n(input.data() + y * width, width, row);
      });
      if (!ordered || reads != height || streamed != ssim(input, width, height)) return false;
    }
  }
  return true;
}
QVariantMap large_image_test(QSize size) {
  if (!supported_quality_size(size) || size.width() < 32 || size.height() < 32)
    return {{"ok", false}, {"error", "Fixture dimensions must be 32 to 8192 pixels per edge."}};
  QImage reference(size, QImage::Format_RGB32); reference.fill(Qt::black);
  auto candidate = reference.copy();
  // Three nonoverlapping impulse responses: top/left padding, interior ring
  // reuse, and bottom/right padding. Dense 32px patches independently predict
  // the full-image SSIM sum without allocating a full-image moments oracle.
  const std::array<QPoint, 3> points{QPoint(0, 0), QPoint(size.width()/2, size.height()/2),
      QPoint(size.width()-1, size.height()-1)};
  for (const auto point : points) candidate.setPixelColor(point, Qt::white);
  double deficit = 0;
  for (const auto point : {QPoint(0, 0), QPoint(16, 16), QPoint(31, 31)}) {
    std::vector<Moments> patch(32 * 32);
    patch[static_cast<size_t>(point.y() * 32 + point.x())] = {0, 1, 0, 1, 0};
    deficit += (1 - ssim(patch, 32, 32)) * (32 * 32);
  }
  const double count = static_cast<double>(size.width()) * size.height();
  const auto actual = compare(reference, candidate);
  const double expected_ssim = 1 - deficit / count, expected_mse = 3 / count;
  const bool passed = actual.value("ok").toBool() && !actual.value("identical").toBool() &&
      actual.value("maximumChannelCodeError").toInt() == 255 &&
      std::abs(actual.value("mse").toDouble() - expected_mse) < 1e-15 &&
      std::abs(actual.value("ssim").toDouble() - expected_ssim) < 1e-12;
  return {{"ok", passed}, {"schema", "framehouse-large-image-metric-proof-v1"},
      {"qualityCertified", false}, {"scope", "synthetic full-resolution metric and boundary correctness, not photographic sharpness"},
      {"expectedMse", expected_mse}, {"expectedSsim", expected_ssim}, {"actual", actual},
      {"momentScratchBytes", static_cast<qint64>(size.width()) * 12 * static_cast<qint64>(sizeof(Moments))}};
}
}
#include "gaussian_reconstruction_report.h"
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() == 4 && args[1] == "--large-image-test") {
    const auto result = large_image_test(QSize(args[2].toInt(), args[3].toInt()));
    std::cout << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).constData() << '\n';
    return result.value("ok").toBool() ? 0 : 1;
  }
  if (args.size() == 5 && args[1] == "--evaluate") {
    const auto result = reconstruction_report(args[2], args[3], args[4]);
    std::cout << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).constData() << '\n';
    return result.value("ok").toBool() ? 0 : 1;
  }
  if (args.size() == 4 && args[1] == "--compare") {
    auto result = compare(load_quality_image(args[2]), load_quality_image(args[3]));
    result.insert("reference", args[2]); result.insert("candidate", args[3]);
    std::cout << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).constData() << '\n';
    return result.value("ok").toBool() ? 0 : 1;
  }
  if (args.size() != 1) return 1;
  if (!streamed_ssim_test() || !supported_quality_size(QSize(8192, 8192)) ||
      supported_quality_size(QSize(8193, 1)) || supported_quality_size(QSize(1, 8193)) ||
      supported_quality_size(QSize(0, 1))) return 1;
  QImage black(24, 20, QImage::Format_RGB32); black.fill(Qt::black);
  QImage white(black.size(), black.format()); white.fill(Qt::white);
  QImage red(black.size(), black.format()); red.fill(Qt::red);
  const auto same = compare(black, black), opposite = compare(black, white), swapped = compare(white, black);
  if (!same.value("identical").toBool() || same.value("ssim").toDouble() != 1 ||
      opposite.value("mse").toDouble() != 1 || opposite.value("psnrDb").toDouble() != 0 ||
      opposite.value("maximumChannelCodeError").toInt() != 255 || same.value("maximumChannelCodeError").toInt() != 0 ||
      opposite.value("ssim").toDouble() != swapped.value("ssim").toDouble() ||
      compare(black, QImage()).value("ok").toBool() ||
      compare(black, QImage(1, 1, QImage::Format_RGB32)).value("ok").toBool() ||
      std::abs(compare(black, red).value("mse").toDouble() - 1.0 / 3) > 1e-12) return 1;
  // Published Brush float-input example, independently evaluated in double.
  // https://github.com/ArthurBrussee/brush/blob/v0.3.0/crates/brush-train/src/ssim.rs
  double reference_ssim = 0;
  std::vector<Moments> moments(30 * 50);
  for (int c = 0; c < 3; ++c) {
    for (int i = 0; i < 30 * 50; ++i) {
      const auto n = static_cast<float>(i * 3 + c);
      const auto a = static_cast<double>((std::sin(n * 0.12F + 0.5F) + 1.0F) / 2.0F);
      const auto b = static_cast<double>((std::sin(n * 0.53F + 2.0F) + 1.0F) / 2.0F);
      moments[static_cast<size_t>(i)] = {a, b, a * a, b * b, a * b};
    }
    reference_ssim += ssim(moments, 50, 30);
  }
  if (std::abs(reference_ssim / 3 - 0.078679755) > 1e-6) return 1;
  if (!reconstruction_report_test()) return 1;
  std::cout << "Independent stored-RGB PSNR/SSIM: dense/streamed equality, row reuse, identity, known error, symmetry, invalid images and published float reference pass.\n";
  return 0;
}
