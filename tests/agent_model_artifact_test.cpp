#include "agent_model_artifact.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>

using namespace photo_editor::desktop;

static bool write_file(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().size() == 3 && app.arguments()[1] == "--verify-installed") {
    const auto result = verify_agent_model_artifact(app.arguments()[2], pinned_agent_model_artifact("qwen3-vl:32b"));
    std::cout << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).constData() << '\n';
    return result.value("ok").toBool() ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  QTemporaryDir temp;
  if (!temp.isValid()) return EXIT_FAILURE;
  AgentModelArtifact fixture{"fixture:vision", "Apache-2.0", {}, {}};
  for (const auto& name : {"manifest", "weights", "config", "license", "params"}) {
    const QByteArray bytes(name);
    if (!write_file(QDir(temp.path()).filePath(name), bytes)) return EXIT_FAILURE;
    fixture.files.append({QString::fromLatin1(name), QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(), bytes.size()});
  }
  const auto valid = verify_agent_model_artifact(temp.path(), fixture);
  if (!valid.value("ok").toBool() || valid.value("verifiedFiles").toInt() != 5 ||
      valid.value("verifiedBytes").toLongLong() != fixture.downloadBytes()) return EXIT_FAILURE;
  for (const auto& file : fixture.files) {
    const auto path = QDir(temp.path()).filePath(file.relative_path);
    if (!write_file(path, QByteArray(file.bytes, 'X')) ||
        verify_agent_model_artifact(temp.path(), fixture).value("ok").toBool() ||
        !write_file(path, file.relative_path.toUtf8())) return EXIT_FAILURE;
  }
  auto canceled = std::make_shared<std::atomic_bool>(true);
  if (verify_agent_model_artifact(temp.path(), fixture, canceled).value("ok").toBool()) return EXIT_FAILURE;
  const auto license_path = QDir(temp.path()).filePath("license");
  if (!QFile::remove(license_path) || verify_agent_model_artifact(temp.path(), fixture).value("ok").toBool()) return EXIT_FAILURE;
  if (!QFile::link(QDir(temp.path()).filePath("weights"), license_path) ||
      verify_agent_model_artifact(temp.path(), fixture).value("ok").toBool()) return EXIT_FAILURE;
  auto escaping = fixture;
  escaping.files[0].relative_path = QStringLiteral("../outside");
  if (!agent_model_artifact_fingerprint(temp.path(), escaping).isEmpty()) return EXIT_FAILURE;
  const auto pin = pinned_agent_model_artifact("qwen3-vl:32b");
  if (pin.files.size() != 5 || pin.downloadBytes() != 20910287340LL || pin.license != "Apache-2.0" ||
      !pinned_agent_model_artifact("unapproved:latest").files.isEmpty()) return EXIT_FAILURE;
  std::cout << "artifact verification: all files, same-size corruption, missing license, symlink, cancellation and traversal pass\n";
  return EXIT_SUCCESS;
}
