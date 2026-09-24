#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ai_model_runtime.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool write_bytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(bytes) == bytes.size();
}

QJsonObject manifest_for(
    const QByteArray& model_bytes,
    const QString& relative_path = QStringLiteral("inpaint/lama.onnx"),
    const QString& contract = QStringLiteral("opencv-lama-image-mask-nchw-float32-512-v1"),
    bool include_provenance = true) {
  QJsonObject artifact{
      {QStringLiteral("id"), QStringLiteral("lama-inpaint")},
      {QStringLiteral("role"), QStringLiteral("inpaint")},
      {QStringLiteral("contract"), contract},
      {QStringLiteral("path"), relative_path},
      {QStringLiteral("bytes"), static_cast<double>(model_bytes.size())},
      {QStringLiteral("sha256"),
       QString::fromLatin1(
           QCryptographicHash::hash(model_bytes, QCryptographicHash::Sha256)
               .toHex())},
  };
  if (include_provenance) {
    artifact.insert(
        QStringLiteral("sourceUrl"),
        QStringLiteral("https://example.invalid/lama.onnx"));
    artifact.insert(
        QStringLiteral("license"),
        QJsonObject{
            {QStringLiteral("name"), QStringLiteral("fixture-license")},
            {QStringLiteral("url"),
             QStringLiteral("https://example.invalid/license")},
        });
  }
  return QJsonObject{
      {QStringLiteral("schema"),
       QStringLiteral("framehouse-ai-model-bundle-v1")},
      {QStringLiteral("version"), 1},
      {QStringLiteral("artifacts"), QJsonArray{artifact}},
  };
}

bool write_manifest(const QString& root, const QJsonObject& manifest) {
  return write_bytes(
      QDir(root).filePath(QStringLiteral("manifest.json")),
      QJsonDocument(manifest).toJson(QJsonDocument::Compact));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary(QStringLiteral("framehouse-runtime-bundle-XXXXXX"));
  if (!temporary.isValid()) {
    return fail("AI runtime bundle test should create a temporary directory");
  }
  const auto root = temporary.path();
  const auto inpaint_dir =
      QDir(root).filePath(QStringLiteral("inpaint"));
  const auto inpaint_path =
      QDir(root).filePath(QStringLiteral("inpaint/lama.onnx"));
  if (!QDir().mkpath(inpaint_dir)) {
    return fail("AI runtime bundle test should create its inpaint directory");
  }

  const auto missing_manifest =
      photo_editor::desktop::verify_ai_model_bundle(root);
  if (missing_manifest.ok || missing_manifest.manifest_available ||
      missing_manifest.error_code != QStringLiteral("manifest_missing")) {
    return fail("runtime bundle verification should fail closed when the manifest is missing");
  }

  const QByteArray model_bytes("deterministic-lama-runtime-fixture");
  if (!write_bytes(inpaint_path, model_bytes) ||
      !write_manifest(root, manifest_for(model_bytes))) {
    return fail("AI runtime bundle test should write a valid fixture");
  }
  const auto verified = photo_editor::desktop::verify_ai_model_bundle(root);
  if (!verified.ok || !verified.manifest_available ||
      !verified.inpaint_artifact_verified || verified.artifact_count != 1 ||
      verified.inpaint_bytes != model_bytes.size() ||
      verified.inpaint_sha256 !=
          QString::fromLatin1(
              QCryptographicHash::hash(model_bytes, QCryptographicHash::Sha256)
                  .toHex()) ||
      verified.inpaint_model_path != QFileInfo(inpaint_path).canonicalFilePath()) {
    return fail("runtime bundle verification should accept only the complete pinned inpaint fixture");
  }

  qputenv("PHOTO_EDITOR_AI_MODEL_DIR", root.toUtf8());
  const auto runtime_status =
      photo_editor::desktop::ai_model_runtime_status();
  qunsetenv("PHOTO_EDITOR_AI_MODEL_DIR");
  if (!runtime_status.bundle_verified ||
      !runtime_status.inpaint_artifact_verified ||
      runtime_status.inpaint_available ||
      !runtime_status.diagnostics.contains(QStringLiteral("bundle:verified")) ||
      !runtime_status.diagnostics.contains(QStringLiteral("lama:runtime-missing")) ||
      !runtime_status.summary.contains(QStringLiteral("ONNX Runtime is not linked"))) {
    return fail("runtime readiness should distinguish a verified model artifact from an unlinked inference SDK");
  }

  QByteArray corrupted_bytes = model_bytes;
  corrupted_bytes[0] = corrupted_bytes.at(0) == 'x' ? 'y' : 'x';
  if (!write_bytes(inpaint_path, corrupted_bytes)) {
    return fail("AI runtime bundle test should write its corrupted artifact");
  }
  const auto corrupted =
      photo_editor::desktop::verify_ai_model_bundle(root);
  if (corrupted.ok ||
      corrupted.error_code != QStringLiteral("artifact_sha256_mismatch")) {
    return fail("runtime bundle verification should reject same-size model corruption");
  }
  const auto corrupted_runtime_root =
      QDir(root).filePath(QStringLiteral("corrupted-runtime"));
  const auto corrupted_runtime_model =
      QDir(corrupted_runtime_root).filePath(QStringLiteral("inpaint/lama.onnx"));
  if (!QDir().mkpath(QFileInfo(corrupted_runtime_model).absolutePath()) ||
      !write_bytes(corrupted_runtime_model, corrupted_bytes) ||
      !write_manifest(corrupted_runtime_root, manifest_for(model_bytes))) {
    return fail("AI runtime bundle test should write its corrupted readiness fixture");
  }
  qputenv("PHOTO_EDITOR_AI_MODEL_DIR", corrupted_runtime_root.toUtf8());
  const auto corrupted_runtime_status =
      photo_editor::desktop::ai_model_runtime_status();
  qunsetenv("PHOTO_EDITOR_AI_MODEL_DIR");
  if (corrupted_runtime_status.bundle_verified ||
      corrupted_runtime_status.inpaint_artifact_verified ||
      corrupted_runtime_status.inpaint_available ||
      corrupted_runtime_status.bundle_verification_error_code !=
          QStringLiteral("artifact_sha256_mismatch") ||
      !corrupted_runtime_status.summary.contains(
          QStringLiteral("failed integrity verification"))) {
    return fail("runtime readiness should fail closed and disclose same-size bundle corruption");
  }

  if (!write_bytes(inpaint_path, model_bytes) ||
      !write_manifest(root, manifest_for(model_bytes, QStringLiteral("../lama.onnx")))) {
    return fail("AI runtime bundle test should write its escaped-path fixture");
  }
  const auto escaped = photo_editor::desktop::verify_ai_model_bundle(root);
  if (escaped.ok ||
      escaped.error_code != QStringLiteral("artifact_path_invalid")) {
    return fail("runtime bundle verification should reject path traversal");
  }

  if (!write_manifest(
          root,
          manifest_for(
              model_bytes,
              QStringLiteral("inpaint/lama.onnx"),
              QStringLiteral("unknown-inpaint-contract")))) {
    return fail("AI runtime bundle test should write its tensor-contract fixture");
  }
  const auto wrong_contract =
      photo_editor::desktop::verify_ai_model_bundle(root);
  if (wrong_contract.ok ||
      wrong_contract.error_code !=
          QStringLiteral("artifact_contract_invalid")) {
    return fail("runtime bundle verification should reject an unsupported tensor contract");
  }

  if (!write_manifest(
          root,
          manifest_for(
              model_bytes,
              QStringLiteral("inpaint/lama.onnx"),
              QStringLiteral("opencv-lama-image-mask-nchw-float32-512-v1"),
              false))) {
    return fail("AI runtime bundle test should write its missing-provenance fixture");
  }
  const auto missing_provenance =
      photo_editor::desktop::verify_ai_model_bundle(root);
  if (missing_provenance.ok ||
      missing_provenance.error_code !=
          QStringLiteral("artifact_provenance_missing")) {
    return fail("runtime bundle verification should reject missing source or license evidence");
  }

  return EXIT_SUCCESS;
}
