#include "catalog_helper_payload.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <iostream>

using namespace photo_editor::desktop;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains(QStringLiteral("--receive"))) {
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) return 1;
    const auto bytes = input.readAll();
    const auto object = QJsonDocument::fromJson(bytes).object();
    if (object.value("schema").toString() != "photo-editor-catalog-arguments:v1") return 1;
    std::cout << QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
    return 0;
  }
  const QStringList values{QStringLiteral("asset"), QString(2 * 1024 * 1024, QChar('x')),
      QString::fromUtf8("quotes \" and slash \\ and α\n"), QString(QChar(0))};
  const auto bytes = catalog_helper_argument_payload(values);
  const auto object = QJsonDocument::fromJson(bytes).object();
  if (object.value("arguments").toArray() != QJsonArray::fromStringList(values)) return 1;
  QProcess process;
  QString error;
  if (!start_catalog_helper_stdin(&process, app.applicationFilePath(), {"--receive"}, values, &error) ||
      !process.waitForFinished(10000) || process.exitCode() != 0 ||
      process.exitStatus() != QProcess::NormalExit || process.arguments() != QStringList{"--receive"} ||
      process.readAllStandardOutput() != QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ||
      process.property("recipePayloadBytes").toLongLong() != bytes.size()) return 1;
  QStringList too_many;
  for (int i = 0; i < 33; ++i) too_many.append("argument");
  if (start_catalog_helper_stdin(&process, app.applicationFilePath(), {"--receive"}, too_many, &error) ||
      error.isEmpty() || process.state() != QProcess::NotRunning) return 1;
  process.start(app.applicationFilePath(), {"--receive"});
  if (!process.waitForStarted(2000) ||
      start_catalog_helper_stdin(&process, app.applicationFilePath(), {"wrong"}, values, &error) ||
      process.arguments() != QStringList{"--receive"}) return 1;
  if (process.write(bytes) != bytes.size()) return 1;
  process.closeWriteChannel();
  if (!process.waitForFinished(10000) || process.exitCode() != 0 ||
      process.readAllStandardOutput() != QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) return 1;
  std::cout << "2 MiB stdin transfer, Unicode/quotes/NUL, EOF, argument privacy and count limit passed.\n";
  return 0;
}
