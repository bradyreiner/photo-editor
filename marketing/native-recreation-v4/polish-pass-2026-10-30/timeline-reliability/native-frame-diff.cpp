#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const QImage before(QString::fromLocal8Bit(argv[1]));
  const QImage after(QString::fromLocal8Bit(argv[2]));
  const QRect crop(1104, 220, 626, 352);
  if (before.isNull() || after.isNull() || before.size() != after.size() ||
      !before.rect().contains(crop)) return 3;
  qint64 changed = 0;
  qint64 difference = 0;
  for (int y = crop.top(); y <= crop.bottom(); ++y) {
    for (int x = crop.left(); x <= crop.right(); ++x) {
      const auto a = before.pixel(x, y);
      const auto b = after.pixel(x, y);
      if ((a & 0x00ffffffU) != (b & 0x00ffffffU)) ++changed;
      difference += qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b)) + qAbs(qBlue(a) - qBlue(b));
    }
  }
  const QJsonObject result{{"changedProgramPixels", changed},
      {"programPixels", crop.width() * crop.height()},
      {"meanAbsoluteChannelDifference", static_cast<double>(difference) / (crop.width() * crop.height() * 3)},
      {"crop", QJsonArray{crop.x(), crop.y(), crop.width(), crop.height()}}};
  std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
  return changed > 1000 ? 0 : 4;
}
