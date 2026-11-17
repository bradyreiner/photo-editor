#include "photo_auto_horizon.h"

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QTransform>
#include <QtMath>

#include <cmath>
#include <iostream>

#ifndef PHOTO_AUTO_HORIZON_TEST_ROOT
#define PHOTO_AUTO_HORIZON_TEST_ROOT "."
#endif

namespace {

QImage horizon(QSize size, double angle, double center = 0.5) {
  QImage image(size, QImage::Format_RGB32);
  const auto slope = std::tan(qDegreesToRadians(angle));
  for (int y = 0; y < size.height(); ++y) {
    auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < size.width(); ++x) {
      const auto line = center * size.height() + slope * (x - size.width() * 0.5);
      row[x] = y < line ? qRgb(75, 155, 220) : qRgb(135, 85, 35);
    }
  }
  return image;
}

QImage load_fixture(const QString& relative_path) {
  const auto path = QString::fromUtf8(PHOTO_AUTO_HORIZON_TEST_ROOT) + QLatin1Char('/') +
      relative_path;
  QImageReader reader(path);
  reader.setAutoTransform(true);
  return reader.read();
}

QImage rotated_center_crop(const QImage& source, double degrees) {
  QTransform transform;
  transform.rotate(degrees);
  const auto expanded = source.transformed(transform, Qt::SmoothTransformation);
  if (expanded.isNull() || expanded.width() < source.width() ||
      expanded.height() < source.height()) {
    return {};
  }
  const auto left = (expanded.width() - source.width()) / 2;
  const auto top = (expanded.height() - source.height()) / 2;
  return expanded.copy(left, top, source.width(), source.height());
}

bool expect_photo_estimate(const QString& label, const QImage& image, bool should_be_confident,
                           double expected_angle = 0.0, double angle_tolerance = 0.0) {
  const auto estimate = photo_editor::desktop::estimate_auto_horizon(image);
  const auto valid = std::isfinite(estimate.angle_degrees) &&
      std::isfinite(estimate.confidence) && estimate.confidence >= 0.0 &&
      estimate.confidence <= 1.0;
  const auto angle_matches = !should_be_confident ||
      std::abs(estimate.angle_degrees - expected_angle) <= angle_tolerance;
  if (!valid || estimate.confident != should_be_confident || !angle_matches) {
    std::cerr << label.toStdString() << ": confident=" << estimate.confident
              << " angle=" << estimate.angle_degrees
              << " confidence=" << estimate.confidence
              << " expected_confident=" << should_be_confident
              << " expected_angle=" << expected_angle
              << " tolerance=" << angle_tolerance << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  Q_UNUSED(application);
  using photo_editor::desktop::estimate_auto_horizon;
  for (const auto size : {QSize(800, 400), QSize(400, 800), QSize(1200, 800)}) {
    for (const double angle : {-8.0, -3.5, 0.0, 4.0, 9.0}) {
      const auto estimate = estimate_auto_horizon(horizon(size, angle));
      if (!estimate.confident || !std::isfinite(estimate.confidence) ||
          estimate.confidence < 0 || estimate.confidence > 1 ||
          std::abs(estimate.angle_degrees - angle) > 0.8) {
        std::cerr << "Horizon " << size.width() << 'x' << size.height() << " at " << angle
                  << " degrees: detected=" << estimate.confident << " angle="
                  << estimate.angle_degrees << " confidence=" << estimate.confidence << '\n';
        return 1;
      }
    }
  }

  QImage uniform(800, 400, QImage::Format_RGB32);
  uniform.fill(QColor(100, 120, 130));
  if (estimate_auto_horizon(uniform).confident || estimate_auto_horizon(QImage()).confident ||
      estimate_auto_horizon(horizon(QSize(40, 30), 2)).confident) {
    std::cerr << "Missing or featureless horizons must not suggest a rotation\n";
    return 1;
  }

  QImage ambiguous(800, 400, QImage::Format_RGB32);
  for (int y = 0; y < ambiguous.height(); ++y) {
    auto* row = reinterpret_cast<QRgb*>(ambiguous.scanLine(y));
    for (int x = 0; x < ambiguous.width(); ++x)
      row[x] = (y / 100) % 2 == 0 ? qRgb(75, 155, 220) : qRgb(135, 85, 35);
  }
  if (estimate_auto_horizon(ambiguous).confident) {
    std::cerr << "Equally strong competing lines must not choose an arbitrary horizon\n";
    return 1;
  }

  auto partial = uniform;
  const auto clear = horizon(partial.size(), 4);
  for (int y = 0; y < partial.height(); ++y) {
    auto* destination = reinterpret_cast<QRgb*>(partial.scanLine(y));
    const auto* source = reinterpret_cast<const QRgb*>(clear.constScanLine(y));
    for (int x = 250; x < 550; ++x) destination[x] = source[x];
  }
  if (estimate_auto_horizon(partial).confident ||
      estimate_auto_horizon(horizon(QSize(800, 400), 25)).confident) {
    std::cerr << "Short or steep lines need a manual horizon guide\n";
    return 1;
  }

  const auto test_root = QString::fromUtf8(PHOTO_AUTO_HORIZON_TEST_ROOT);
  const auto ocean_path = QStringLiteral("tests/fixtures/auto-horizon/noaa-sun-peeks-clouds.jpg");
  const auto subdued_ocean_path = QStringLiteral("tests/fixtures/auto-horizon/noaa-open-ocean.jpg");
  const auto mountain_path = QStringLiteral(
      "tests/fixtures/creative-tools/landscape-mountain-source.jpg");
  const auto ocean = load_fixture(ocean_path);
  const auto subdued_ocean = load_fixture(subdued_ocean_path);
  const auto mountain = load_fixture(mountain_path);
  if (ocean.isNull() || subdued_ocean.isNull() || mountain.isNull()) {
    std::cerr << "Unable to load Auto Level photo fixture(s) below "
              << test_root.toStdString() << '\n';
    return 1;
  }

  // This NOAA open-ocean frame has a visible, nearly straight water/sky edge.
  // A rough visual trace is about -0.9 degrees. The 0.3 degree tolerance
  // accommodates that approximate trace and the detector's 0.25 degree grid.
  const auto ocean_estimate = estimate_auto_horizon(ocean);
  if (!expect_photo_estimate(QStringLiteral("NOAA open-sea horizon"), ocean, true,
                             -0.75, 0.3) || ocean_estimate.confidence < 0.8) {
    std::cerr << "The clear NOAA ocean horizon should produce a stable, high-confidence level estimate\n";
    return 1;
  }

  const auto half_resolution = ocean.scaled(ocean.width() / 2, ocean.height() / 2,
                                             Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  const auto analysis_resolution = ocean.scaled(640, 640, Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation);
  const auto half_estimate = estimate_auto_horizon(half_resolution);
  const auto analysis_estimate = estimate_auto_horizon(analysis_resolution);
  if (!expect_photo_estimate(QStringLiteral("NOAA horizon half resolution"),
                             half_resolution, true, ocean_estimate.angle_degrees, 0.3) ||
      !expect_photo_estimate(QStringLiteral("NOAA horizon 640px long edge"),
                             analysis_resolution, true, ocean_estimate.angle_degrees, 0.3) ||
      std::abs(half_estimate.confidence - ocean_estimate.confidence) > 0.12 ||
      std::abs(analysis_estimate.confidence - ocean_estimate.confidence) > 0.12) {
    std::cerr << "NOAA horizon estimate should remain stable when source resolution changes\n";
    return 1;
  }

  for (const auto degrees : {-4.0, 4.0}) {
    const auto rotated = rotated_center_crop(ocean, degrees);
    const auto estimate = estimate_auto_horizon(rotated);
    if (rotated.isNull() || !estimate.confident ||
        std::abs((estimate.angle_degrees - ocean_estimate.angle_degrees) - degrees) > 0.35 ||
        estimate.confidence < 0.8) {
      std::cerr << "NOAA photo relative tilt " << degrees << " degrees: confident="
                << estimate.confident << " delta="
                << (estimate.angle_degrees - ocean_estimate.angle_degrees)
                << " confidence=" << estimate.confidence << '\n';
      return 1;
    }
  }

  // This NOAA image is hazy and low contrast, so it should not trigger a
  // potentially surprising automatic rotation. The mountain photo provides
  // a real non-horizon scene with long structural edges as a false-positive check.
  if (!expect_photo_estimate(QStringLiteral("NOAA low-contrast open ocean"),
                             subdued_ocean, false) ||
      !expect_photo_estimate(QStringLiteral("tracked mountain ridge"), mountain, false)) {
    std::cerr << "Ambiguous or non-horizon photos must not suggest an automatic rotation\n";
    return 1;
  }

  std::cout << "Auto horizon: landscape/portrait synthetic slopes, flat/ambiguous/partial/steep cases, "
               "NOAA photo scale and relative-tilt checks, and real-photo refusal cases passed\n";
  return 0;
}
