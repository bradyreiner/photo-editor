#include "timeline_compound_producer_identity.h"

#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

using photo_editor::desktop::timeline_compound::producer::CandidateState;
using photo_editor::desktop::timeline_compound::producer::IdentityState;

namespace compound = photo_editor::desktop::timeline_compound::producer;

namespace {

bool write_child_document(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write("{}") == 2;
}

int fail(const QString& message, QProcess* process = nullptr) {
  if (process != nullptr && process->state() != QProcess::NotRunning) {
    process->kill();
    process->waitForFinished(3000);
  }
  std::cerr << message.toStdString() << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  const auto perl_candidate = QStandardPaths::findExecutable(QStringLiteral("perl"));
  const auto perl = QFileInfo(perl_candidate).canonicalFilePath();
  if (perl.isEmpty()) return fail(QStringLiteral("perl is required for the producer identity test."));

  QTemporaryDir temporary;
  const auto directory = QFileInfo(temporary.path()).canonicalFilePath();
  if (directory.isEmpty()) return fail(QStringLiteral("The process fixture directory must be canonical."));
  const auto child_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
  const auto operation_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
  const auto document_path = QDir(directory).filePath(QStringLiteral("compound-%1.json").arg(child_id));
  const auto render_path = QDir(directory).filePath(QStringLiteral("compound-%1.mov").arg(child_id));
  const auto partial_path = compound::partial_render_path(render_path);
  if (!write_child_document(document_path)) return fail(QStringLiteral("The child project fixture must be writable."));

  const QStringList process_arguments{
      QStringLiteral("-e"),
      QStringLiteral("sleep 45"),
      partial_path};
  QString error;
  const auto launch_spec = compound::make_launch_specification(
      perl, process_arguments, operation_id, child_id, document_path, render_path, &error);
  if (launch_spec.isEmpty()) return fail(QStringLiteral("The exact launch spec should seal: ") + error);
  if (!compound::validate_launch_specification(launch_spec, &error)) {
    return fail(QStringLiteral("A fresh prepared launch spec should validate: ") + error);
  }
  const auto serialized_launch_spec = QJsonDocument::fromJson(
      QJsonDocument::fromVariant(launch_spec).toJson(QJsonDocument::Compact)).object().toVariantMap();
  if (!compound::validate_launch_specification(serialized_launch_spec, &error)) {
    return fail(QStringLiteral("The journal JSON round-trip should preserve launch-spec validation: ") + error);
  }

  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(perl, process_arguments);
  if (!process.waitForStarted(3000)) return fail(QStringLiteral("The real producer process should start."), &process);

  QVariantMap candidate_receipt;
  const auto candidate_state = compound::find_unique_candidate(
      serialized_launch_spec, &candidate_receipt, &error);
  if (candidate_state != CandidateState::UniqueMatch || candidate_receipt.isEmpty()) {
    return fail(QStringLiteral("A single live process with the exact UID/executable/argv/start identity should match: ") + error,
                &process);
  }

  auto permuted_arguments = process_arguments;
  permuted_arguments.swapItemsAt(1, 2);
  const auto permuted_spec = compound::make_launch_specification(
      perl, permuted_arguments, operation_id, child_id, document_path, render_path, &error);
  QVariantMap unexpected_receipt;
  if (permuted_spec.isEmpty() ||
      compound::find_unique_candidate(permuted_spec, &unexpected_receipt, &error) !=
          CandidateState::NoMatch) {
    return fail(QStringLiteral("A reordered argv must not identify the active producer: ") + error, &process);
  }

  auto receipt = compound::capture_receipt(
      process.processId(), perl, process_arguments, operation_id, child_id,
      document_path, render_path, &error);
  if (receipt.isEmpty() ||
      compound::inspect_receipt(receipt, &error) != IdentityState::Matching) {
    return fail(QStringLiteral("The real producer receipt should verify against its live process: ") + error,
                &process);
  }

  const auto receipt_path = compound::receipt_path(render_path);
  if (!compound::write_receipt(receipt_path, receipt, &error)) {
    return fail(QStringLiteral("The receipt should be atomically persisted: ") + error, &process);
  }
  const auto loaded = compound::read_receipt(receipt_path, &error);
  if (QJsonDocument::fromVariant(loaded).toJson(QJsonDocument::Compact) !=
      QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Compact)) {
    return fail(QStringLiteral("The persisted receipt should round-trip exactly: ") + error, &process);
  }
  if (compound::inspect_receipt(loaded, &error) != IdentityState::Matching) {
    return fail(QStringLiteral("A JSON-decoded receipt should preserve its argv identity: ") + error,
                &process);
  }
  if (!compound::validate_launch_specification(serialized_launch_spec, &error)) {
    return fail(QStringLiteral("A sealed launch spec should remain valid while its receipt exists: ") + error,
                &process);
  }

  auto tampered_spec = serialized_launch_spec;
  auto tampered_argv = tampered_spec.value(QStringLiteral("argv")).toList();
  if (tampered_argv.size() < 2) {
    return fail(QStringLiteral("The serialized launch-spec argv should retain its complete string list."),
                &process);
  }
  tampered_argv[1] = QStringLiteral("/usr/bin/false");
  tampered_spec.insert(QStringLiteral("argv"), tampered_argv);
  if (compound::validate_launch_specification(tampered_spec, &error)) {
    return fail(QStringLiteral("A changed serialized argv must fail its sealed hash."), &process);
  }

  auto tampered = receipt;
  tampered.insert(QStringLiteral("executablePath"), QStringLiteral("/usr/bin/false"));
  if (compound::inspect_receipt(tampered, &error) != IdentityState::Unavailable) {
    return fail(QStringLiteral("A changed receipt field must fail its source hash check."), &process);
  }

  bool process_stopped = false;
  if (!compound::stop_verified_producer(receipt, 1000, &process_stopped, &error) || !process_stopped) {
    return fail(QStringLiteral("Only the exact receipt-verified producer should be stopped: ") + error,
                &process);
  }
  if (!process.waitForFinished(3000)) return fail(QStringLiteral("The stopped producer should be reaped."), &process);
  if (compound::inspect_receipt(receipt, &error) != IdentityState::NotRunning) {
    return fail(QStringLiteral("A verified stopped producer should no longer match its PID/start receipt."));
  }

  QFile completed_render(render_path);
  if (!completed_render.open(QIODevice::WriteOnly) ||
      completed_render.write("verified completed render") != 25) {
    return fail(QStringLiteral("The completed-render fixture should be writable."));
  }
  completed_render.close();
  if (!compound::validate_launch_specification(serialized_launch_spec, &error) ||
      compound::inspect_receipt(receipt, &error) != IdentityState::NotRunning) {
    return fail(QStringLiteral("A completed render and its exact receipt should remain inspectable: ") + error);
  }
  if (!compound::make_launch_specification(
           perl, process_arguments, operation_id, child_id, document_path, render_path, &error).isEmpty()) {
    return fail(QStringLiteral("A new producer launch must not be sealed over existing output siblings."));
  }

  if (!QFile::remove(render_path) || !QFile::link(document_path, render_path)) {
    return fail(QStringLiteral("The symlink rejection fixture should be constructible."));
  }
  if (compound::validate_launch_specification(serialized_launch_spec, &error)) {
    return fail(QStringLiteral("A symlink at the task-owned render path must remain rejected."));
  }
  QFile::remove(render_path);

  QFile receipt_file(receipt_path);
  if (!receipt_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      receipt_file.write("{\"tampered\":true}") != 17) {
    return fail(QStringLiteral("The receipt tamper fixture should be writable."));
  }
  if (!compound::read_receipt(receipt_path, &error).isEmpty()) {
    return fail(QStringLiteral("Tampered receipt bytes must fail source hash/schema verification."));
  }

  std::cout << "compound producer identity, argv permutation, receipt integrity, and exact cancellation passed\n";
  return 0;
}
