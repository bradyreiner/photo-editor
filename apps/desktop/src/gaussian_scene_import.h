#pragma once
#include "gaussian_ply_validation.h"
#include "gaussian_alpha_model.h"
#include "gaussian_scene_limits.h"
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>

namespace photo_editor::desktop {
inline QVariantMap import_gaussian_ply(const QString& source, const QString& project,
    const std::shared_ptr<std::atomic_bool>& canceled) {
  const auto fail = [](const QString& error) { return QVariantMap{{"ok", false}, {"error", error}}; };
  if (!QFileInfo(project).isAbsolute() || QFileInfo::exists(project) || !QFileInfo(source).isFile())
    return fail("Choose a Gaussian PLY and a new project folder that does not exist.");
  const auto parent = QFileInfo(project).absolutePath();
  if (!QDir().mkpath(parent)) return fail("Cannot create the import destination.");
  QTemporaryDir staging(QDir(parent).filePath(".looksy-import-XXXXXX"));
  if (!staging.isValid() || !QDir(staging.path()).mkdir("trained")) return fail("Cannot stage the imported scene.");
  const auto path = staging.filePath("trained/scene.ply");
  QFile input(source), output(path);
  if (!input.open(QIODevice::ReadOnly) || input.size() > 2LL * 1024 * 1024 * 1024 ||
      !output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail("Cannot copy the Gaussian PLY.");
  while (!input.atEnd()) {
    if (canceled && canceled->load()) return fail("Scene import canceled.");
    const auto bytes = input.read(1024 * 1024);
    if (bytes.isEmpty() || output.write(bytes) != bytes.size()) return fail("Cannot finish copying the Gaussian PLY.");
  }
  output.close();
  auto validation = validate_gaussian_ply(path, canceled);
  if (!validation.value("ok").toBool()) return validation;
  if (validation.value("vertexCount").toULongLong() > gaussian_static_point_limit) return fail("Import supports at most four million static Gaussians.");
  // A Looksy export carries its opacity convention beside the unchanged PLY.
  // Bind this setting to the copied bytes; never apply the old visibility mask.
  QFile metadata(source+".splat.json");
  if (metadata.exists()) {
    if (!metadata.open(QIODevice::ReadOnly) || metadata.size() > 4*1024*1024) return fail("Cannot read the splat export receipt.");
    const auto receipt = QJsonDocument::fromJson(metadata.readAll()).toVariant().toMap();
    if (receipt.value("schema") != "looksy-gaussian-cleanup-v1" || receipt.value("exportedArtifactSha256") != validation.value("artifactSha256"))
      return fail("The splat export receipt does not match this artifact.");
    if (receipt.contains("alphaModel")) {
      if (receipt.value("alphaModel").metaType().id() != QMetaType::QString) return fail("Invalid splat opacity convention.");
      const auto profile = parse_gaussian_alpha_model(receipt.value("alphaModel").toString());
      if (!profile) return fail("Unknown splat opacity convention.");
      if (*profile != GaussianAlphaModel::Brush999) {
        // Keep the Core-only import/controller independent of GUI geometry.
        // The composition parser roundtrip is covered by the import tests.
        const QVariantMap composition{{"schema","looksy-scene-composition-v8"},{"objects",QVariantList{}},{"lights",QVariantList{}},
            {"ambient",0.15F},{"reflections",false},{"lightSamples",64},{"alphaModel",gaussian_alpha_model_name(*profile)},
            {"captureLighting",QVariantMap{{"mode","captured"},{"power",1},{"shadowStrength",1}}},
            {"haze",QVariantMap{{"density",0},{"distance",1000},{"anisotropy",0},{"color",QVariantList{0.7F,0.8F,1.0F}}}},
            {"environment",QVariantMap{{"diffuse",false},{"strength",1}}}};
        const auto bytes = QJsonDocument::fromVariant(QVariantMap{{"schema","looksy-scene-objects-v1"},
            {"artifactSha256",validation.value("artifactSha256")},{"composition",composition}}).toJson();
        QFile settings(staging.filePath("scene-objects.json"));
        if (!settings.open(QIODevice::WriteOnly|QIODevice::NewOnly) || settings.write(bytes) != bytes.size() || !settings.flush())
          return fail("Cannot stage the splat opacity convention.");
      }
    }
  }
  QFile header_file(path); if (!header_file.open(QIODevice::ReadOnly)) return fail("Cannot inspect the imported PLY.");
  QStringList properties;
  while (!header_file.atEnd()) {
    const auto line = header_file.readLine(16384).trimmed();
    if (line == "end_header") break;
    if (line.startsWith("property ")) properties.append(QString::fromLatin1(line.simplified().split(' ').last()));
  }
  int rest = 0; for (const auto& property : properties) if (property.startsWith("f_rest_")) ++rest;
  if (rest != 0 && rest != 9 && rest != 24 && rest != 45) return fail("Import requires complete spherical harmonics of degree zero through three.");
  for (int i = 0; i < rest; ++i) if (!properties.contains(QStringLiteral("f_rest_%1").arg(i)))
    return fail("The imported spherical harmonics are incomplete.");
  validation.insert("artifactPath", QDir(project).filePath("trained/scene.ply"));
  QVariantMap saved{{"schema", "framehouse-gaussian-scene-v1"}, {"mode", "static-3d"}, {"state", "trained"},
      {"importedArtifact", true}, {"artifactValidation", validation}, {"splatCount", validation.value("vertexCount")}};
  const auto bytes = QJsonDocument::fromVariant(saved).toJson();
  QFile receipt(staging.filePath("scene.json"));
  if (!receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly) || receipt.write(bytes) != bytes.size() || !receipt.flush())
    return fail("Cannot save the imported scene receipt.");
  receipt.close();
  if ((canceled && canceled->load()) || !QDir().rename(staging.path(), project)) return fail("Cannot publish the new scene project; its destination may already exist.");
  staging.setAutoRemove(false);
  saved.insert("ok", true); saved.insert("projectPath", project); saved.insert("artifactPath", validation.value("artifactPath"));
  return saved;
}
}  // namespace photo_editor::desktop
