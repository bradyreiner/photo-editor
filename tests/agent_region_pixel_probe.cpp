#include <QCoreApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <iostream>

// Read-only comparison of a manually chosen normalized rectangle, independent
// of the generated mask. Inputs must be registered proofs in the same encoding.
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() != 7 && args.size() != 8) {
    std::cerr << "usage: agent_region_pixel_probe BEFORE AFTER X Y WIDTH HEIGHT [--require-unchanged|--require-warmer]\n";
    return 1;
  }
  const auto requirement = args.size() == 8 ? args[7] : QString();
  if (!requirement.isEmpty() && requirement != "--require-unchanged" && requirement != "--require-warmer") return 1;
  const QImage before(args[1]), after(args[2]);
  if (before.isNull() || after.isNull() || before.size() != after.size()) return 1;
  double rectangle[4];
  for (int i = 0; i < 4; ++i) {
    bool valid = false;
    rectangle[i] = args[i + 3].toDouble(&valid);
    if (!valid || !std::isfinite(rectangle[i]) || rectangle[i] < 0 || rectangle[i] > 1) return 1;
  }
  if (rectangle[2] <= 0 || rectangle[3] <= 0 || rectangle[0] + rectangle[2] > 1 || rectangle[1] + rectangle[3] > 1) return 1;
  const int x0 = static_cast<int>(rectangle[0] * before.width());
  const int y0 = static_cast<int>(rectangle[1] * before.height());
  const int x1 = static_cast<int>((rectangle[0] + rectangle[2]) * before.width());
  const int y1 = static_cast<int>((rectangle[1] + rectangle[3]) * before.height());
  double sum = 0, maximum = 0, warmth = 0;
  qint64 count = 0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const auto a = before.pixelColor(x, y), b = after.pixelColor(x, y);
      const double delta[] = {std::abs(a.redF() - b.redF()), std::abs(a.greenF() - b.greenF()), std::abs(a.blueF() - b.blueF())};
      for (double value : delta) { sum += value; maximum = std::max(maximum, value); }
      warmth += (b.redF() - b.blueF()) - (a.redF() - a.blueF());
      ++count;
    }
  }
  if (count == 0) return 1;
  std::cout << QJsonDocument(QJsonObject{{"pixels", count}, {"meanAbsRgbDelta", sum / (3.0 * static_cast<double>(count))},
      {"maxAbsChannelDelta", maximum}, {"meanRedMinusBlueChange", warmth / static_cast<double>(count)},
      {"width", before.width()}, {"height", before.height()}}).toJson().constData();
  if (requirement == "--require-unchanged") return maximum == 0.0 ? 0 : 1;
  if (requirement == "--require-warmer") return warmth > 0.0 ? 0 : 1;
  return 0;
}
