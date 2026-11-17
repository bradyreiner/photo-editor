#include "editor_preview_texture_upload.h"

#include <QPainter>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif
#include <qfloat16.h>
#include <cmath>
#include <iostream>

int upload_workload(const QString& path) {
  QJsonObject report;
  for (bool floating : {false, true}) {
    QImage image(3072, 2048, floating ? QImage::Format_RGBA16FPx4 : QImage::Format_ARGB32);
    image.fill(QColor(175, 120, 80, 113));
    for (const auto& mode : {QStringLiteral("small"), QStringLiteral("full"), QStringLiteral("direct")}) {
      const QRect region = mode == "full" ? image.rect() : QRect(517, 413, 128, 96);
      auto input = mode == "direct"
          ? photo_editor::desktop::premultiplied_preview_upload_image(image, floating) : image;
      QJsonArray samples;
      for (int i = 0; i < 31; ++i) {
        QElapsedTimer timer; timer.start();
        const auto upload = photo_editor::desktop::premultiplied_preview_upload_region(input, region, floating);
        samples.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
        if (upload.image.isNull()) return 1;
        const auto actual = upload.image.copy(QRect(upload.source_offset, region.size()));
        const auto expected = photo_editor::desktop::premultiplied_preview_upload_image(input, floating).copy(region);
        if (actual != expected || actual.colorSpace() != expected.colorSpace() ||
            actual.devicePixelRatio() != expected.devicePixelRatio()) return 2;
      }
      report.insert((floating ? "hdr_" : "sdr_") + mode, samples);
    }
  }
#if defined(__unix__) || defined(__APPLE__)
  rusage usage{}; getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
  constexpr double scale = 1.0;
#else
  constexpr double scale = 1024.0;
#endif
  report.insert(QStringLiteral("peak_rss_bytes"), static_cast<double>(usage.ru_maxrss) * scale);
#endif
  report.insert(QStringLiteral("exact_pixels"), true);
  QFile output(path); if (!output.open(QIODevice::WriteOnly)) return 3;
  output.write(QJsonDocument(report).toJson()); return 0;
}

int main(int argc, char** argv) {
  if (argc == 3 && QByteArray(argv[1]) == "--workload")
    return upload_workload(QString::fromLocal8Bit(argv[2]));

  using photo_editor::desktop::premultiplied_preview_upload_image;
  for (const auto format : {QImage::Format_ARGB32, QImage::Format_ARGB32_Premultiplied,
                           QImage::Format_RGBA64, QImage::Format_RGBA16FPx4}) {
    QImage source(5, 1, format);
    for (int x = 0; x < 5; ++x)
      source.setPixelColor(x, 0, QColor(240, 160, 80, x * 255 / 4));
    const auto original = source.copy();
    for (const bool floating : {false, true}) {
      const auto uploaded = premultiplied_preview_upload_image(source, floating);
      if (source != original || uploaded.isNull()) return 1;
      if (floating) {
        const auto* samples = reinterpret_cast<const qfloat16*>(uploaded.constBits());
        for (int x = 0; x < 5; ++x) {
          const auto color = source.pixelColor(x, 0);
          if (std::abs(float(samples[x * 4 + 3]) - color.alphaF()) > 0.001) {
            std::cerr << "Float alpha format=" << format << " x=" << x
                      << " expected=" << color.alphaF() << " actual=" << float(samples[x * 4 + 3]) << '\n';
            return 1;
          }
          for (int channel = 0; channel < 3; ++channel) {
            const auto rgb = channel == 0 ? color.redF() : channel == 1 ? color.greenF() : color.blueF();
            if (std::abs(float(samples[x * 4 + channel]) - rgb * color.alphaF()) > 0.003)
              return 1;
          }
        }
      } else {
        const auto* pixels = reinterpret_cast<const QRgb*>(uploaded.constBits());
        for (int x = 0; x < 5; ++x) {
          const auto actual = pixels[x];
          const auto expected = qPremultiply(source.pixelColor(x, 0).rgba());
          if (std::abs(qRed(actual) - qRed(expected)) > 1 ||
              std::abs(qGreen(actual) - qGreen(expected)) > 1 ||
              std::abs(qBlue(actual) - qBlue(expected)) > 1 ||
              qAlpha(actual) != qAlpha(expected)) return 1;
        }
      }
      for (const auto background : {QColor(Qt::black), QColor(Qt::white), QColor(48, 48, 48)}) {
        QImage reference(5, 1, QImage::Format_ARGB32_Premultiplied);
        QImage displayed = reference;
        reference.fill(background);
        displayed.fill(background);
        { QPainter painter(&reference); painter.drawImage(0, 0, source); }
        { QPainter painter(&displayed); painter.drawImage(0, 0,
              uploaded.convertToFormat(QImage::Format_ARGB32_Premultiplied)); }
        for (int x = 0; x < 5; ++x) {
          const auto a = reference.pixelColor(x, 0), b = displayed.pixelColor(x, 0);
          if (std::abs(a.red() - b.red()) > 1 || std::abs(a.green() - b.green()) > 1 ||
              std::abs(a.blue() - b.blue()) > 1) {
            std::cerr << "Display premultiplication changed a soft edge: format=" << format
                      << " float=" << floating << " x=" << x << " background=" << background.name().toStdString()
                      << " expected=" << a.name().toStdString() << " actual=" << b.name().toStdString() << '\n';
            return 1;
          }
        }
      }
    }
  }
  // Odd origins/row widths catch crop/stride errors. Compare every byte against
  // the existing full-image conversion, including negative and HDR float RGB.
  for (const auto format : {QImage::Format_ARGB32, QImage::Format_ARGB32_Premultiplied,
      QImage::Format_RGB32, QImage::Format_RGBA8888_Premultiplied, QImage::Format_RGBA64,
      QImage::Format_RGBA64_Premultiplied, QImage::Format_RGBA16FPx4,
      QImage::Format_RGBA32FPx4_Premultiplied}) {
    QImage source(19, 13, format);
    source.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
    source.setDevicePixelRatio(2.0);
    for (int y = 0; y < source.height(); ++y)
      for (int x = 0; x < source.width(); ++x)
        source.setPixelColor(x, y, QColor::fromRgbF(
            float(x) / 7.0F - 0.2F, float(y) / 15.0F, 0.75F, float((x + y) % 11) / 10.0F));
    const auto original = source.copy();
    for (const bool floating : {false, true}) {
      const auto full = premultiplied_preview_upload_image(source, floating);
      for (const QRect region : {QRect(3, 2, 7, 5), QRect(-2, -3, 8, 7), source.rect(),
          QRect(18, 12, 3, 2), QRect(30, 20, 2, 2), QRect{}}) {
        const auto upload = photo_editor::desktop::premultiplied_preview_upload_region(source, region, floating);
        const auto clipped = region.intersected(source.rect());
        if (clipped.isEmpty()) { if (!upload.image.isNull()) return 2; continue; }
        const auto actual = upload.image.copy(QRect(upload.source_offset, clipped.size()));
        const auto expected = full.copy(clipped);
        if (actual != expected || actual.colorSpace() != expected.colorSpace() ||
            actual.devicePixelRatio() != expected.devicePixelRatio()) {
          std::cerr << "Upload rectangle changed pixels format=" << format << " float=" << floating << '\n';
          return 2;
        }
      }
    }
    if (source != original) return 2;
  }
  std::cout << "Display alpha: straight/premultiplied 8/16-bit inputs, float uploads, and light/dark composites passed\n";
  return 0;
}
