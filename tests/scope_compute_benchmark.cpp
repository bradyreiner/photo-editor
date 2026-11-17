#include "histogram_scope_service.h"

#include <algorithm>
#include <iostream>
#include <vector>
#include <QElapsedTimer>

int main(int argc, char**) {
  using photo_editor::desktop::compute_histogram_scope_data;
  for (const auto format : {QImage::Format_ARGB32, QImage::Format_RGBA16FPx4}) {
    const bool budgets = argc > 1;
    QImage image(budgets ? 1024 : 512, budgets ? 768 : 384, format);
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        image.setPixelColor(x, y, QColor::fromRgbF(static_cast<float>(x) / static_cast<float>(image.width()-1), static_cast<float>(y) / static_cast<float>(image.height()-1),
                                                 ((x * 13 + y * 7) % 256) / 255.0f));
      }
    }
    const QStringList modes = budgets ? QStringList{"Prior budget", "Fast", "Balanced", "Precise", "Live"}
                                     : QStringList{"Prior budget"};
    for (const auto& mode : modes) {
      std::vector<double> samples;
      int sample_count = 0;
      for (int i = 0; i < 23; ++i) {
        QElapsedTimer timer;
        timer.start();
        const auto result = mode == QStringLiteral("Prior budget") ? compute_histogram_scope_data(image)
            : photo_editor::desktop::compute_histogram_scope_data_with_options(image,
                photo_editor::desktop::histogram_scope_options(mode, mode == QStringLiteral("Live")));
        const auto ms = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
        sample_count = result.sample_count;
        if (mode == QStringLiteral("Prior budget") && result.sample_count != image.width() * image.height()) return 1;
        if (i >= 3) samples.push_back(ms);
      }
      std::sort(samples.begin(), samples.end());
      std::cout << "format=" << format << " mode=" << mode.toStdString()
                << " samples=" << sample_count << " median_ms=" << samples[10]
                << " p95_ms=" << samples[18] << '\n';
    }
  }
}
