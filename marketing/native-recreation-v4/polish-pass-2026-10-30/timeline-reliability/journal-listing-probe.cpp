#include "timeline_edit_journal_codec.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <iostream>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (argc != 2) return 2;
  QFile file(QString::fromLocal8Bit(argv[1]));
  if (!file.open(QIODevice::ReadOnly)) return 3;
  const auto size = file.size();
  QElapsedTimer timer;
  timer.start();
  const auto tail = photo_editor::desktop::timeline_edit_journal_codec::readListingTail(&file);
  if (tail.entry.isEmpty()) { std::cerr << tail.error.toStdString(); return 4; }
  const auto hash = tail.entry.value(QStringLiteral("documentHash")).toString();
  const QJsonObject result{
      {"journalBytes", size}, {"listingBytesRead", tail.bytesRead},
      {"elapsedMs", timer.nsecsElapsed() / 1000000.0},
      {"documentHash", hash}, {"requiresFullVerification", tail.requiresFullVerification},
      {"damagedTail", tail.damagedTail}};
  std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
  return hash == QStringLiteral("fa0befe66d02dde6e75bb67453ad954fd313b2e12fe2d7b74536a6e06282837a") ? 0 : 5;
}
