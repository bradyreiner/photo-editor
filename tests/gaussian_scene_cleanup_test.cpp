#include "gaussian_scene_cleanup.h"
#include "gaussian_scene_import.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace photo_editor::desktop;
namespace {
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool write(const QString& path, const QByteArray& bytes) {
  QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool expect(bool condition, const char* reason) { if (!condition) std::cerr << reason << '\n'; return condition; }
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temporary;
  const QDir root(temporary.path());
  QStringList properties{"x", "y", "z", "opacity", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3", "f_dc_0", "f_dc_1", "f_dc_2", "nx", "ny", "nz"};
  for (int i = 0; i < 45; ++i) properties.append(QStringLiteral("f_rest_%1").arg(i));
  properties.sort();
  QByteArray header("ply\nformat binary_little_endian 1.0\ncomment preserve-all-properties\nelement vertex 4\n");
  for (const auto& property : properties) header += "property float " + property.toLatin1() + '\n';
  header += "end_header\n";
  QByteArray payload;
  QDataStream stream(&payload, QIODevice::WriteOnly); stream.setByteOrder(QDataStream::LittleEndian);
  stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
  const std::array<float, 4> x{-1, 0, 1, 0}, z{3, 3, 3, -3};
  for (size_t row = 0; row < 4; ++row) for (const auto& property : properties) {
    const auto value = property == "x" ? x[row] : property == "z" ? z[row] :
        property == "rot_0" ? 1.0F : property.startsWith("scale_") ? std::log(row == 2 ? 1.0F : 0.1F) :
        property == "opacity" ? (row == 0 ? -5.0F : 2.0F) :
        property.startsWith("f_rest_") ? static_cast<float>(property.mid(7).toInt()) * 0.01F + static_cast<float>(row) : 0.0F;
    stream << value;
  }
  const auto artifact = root.filePath("source.ply"), project = root.filePath("imported");
  const auto bytes = header + payload;
  auto canceled = std::make_shared<std::atomic_bool>(false);
  if (!write(artifact, bytes)) return 2;
  const auto imported = import_gaussian_ply(artifact, project, canceled);
  if (!expect(imported.value("ok").toBool(), "Import failed") ||
      !expect(!import_gaussian_ply(artifact, project, canceled).value("ok").toBool(), "Import clobbered a project")) return 1;
  const auto hash = imported.value("artifactValidation").toMap().value("artifactSha256").toString();
  auto loaded = load_gaussian_scene(QDir(project).filePath("trained/scene.ply"), {}, hash);
  if (!expect(loaded.scene && loaded.scene->points.size() == 4 && loaded.scene->cameras.size() == 1, "Imported scene did not load with an overview")) return 1;
  GaussianCamera camera; camera.image_size = {600, 400}; camera.fx = camera.fy = 300; camera.cx = 300; camera.cy = 200;
  auto mask = gaussian_cleanup_mask(*loaded.scene);
  const auto selected = select_gaussian_cleanup(*loaded.scene, mask, camera, {0.45, 0.4, 0.1, 0.2}, false);
  if (!expect(selected == QByteArray("\1\0\1\1", 4), "Projected center selection or behind-camera handling failed")) return 1;
  const auto kept = select_gaussian_cleanup(*loaded.scene, mask, camera, {0.45, 0.4, 0.1, 0.2}, true);
  if (!expect(kept == QByteArray("\0\1\0\0", 4), "Keep rectangle retained off-screen splats")) return 1;
  const QPolygonF triangle{{0.30,0.40},{0.58,0.40},{0.30,0.70}};
  const auto lasso = select_gaussian_cleanup_polygon(*loaded.scene,mask,camera,triangle,false);
  if (!expect(lasso == QByteArray("\0\1\1\1",4),"Lasso selected the bounding box rather than the traced triangle") ||
      !expect(select_gaussian_cleanup_polygon(*loaded.scene,mask,camera,triangle,true) == QByteArray("\1\0\0\0",4),"Lasso keep or behind-camera handling failed")) return 1;
  const QPolygonF concave{{.30,.40},{.72,.40},{.72,.60},{.56,.60},{.56,.48},{.44,.48},{.44,.60},{.30,.60}};
  auto reversed = concave; std::reverse(reversed.begin(),reversed.end()); reversed.append(reversed.front());
  if (!expect(select_gaussian_cleanup_polygon(*loaded.scene,mask,camera,concave,false) == QByteArray("\0\1\0\1",4) &&
      select_gaussian_cleanup_polygon(*loaded.scene,mask,camera,reversed,false) == QByteArray("\0\1\0\1",4),"Concave outline, winding or duplicate closure failed") ||
      !expect(gaussian_cleanup_polygon_contains(concave,{.5,.48}) && !gaussian_cleanup_polygon_contains(concave,{.5,.5}),"Lasso boundary or concave notch failed") ||
      !expect(valid_gaussian_cleanup_polygon(QPolygonF{{.3,.4},{.7,.6},{.3,.6},{.7,.4}}),"Self-crossing outline was rejected by signed-area cancellation") ||
      !expect(!valid_gaussian_cleanup_polygon(QPolygonF{{.1,.1},{.2,.2},{.3,.3}}) &&
          !valid_gaussian_cleanup_polygon(QPolygonF{{.1,.1},{.9,.1},{.5,std::numeric_limits<double>::quiet_NaN()}}),"Degenerate or nonfinite lasso accepted")) return 1;
  if (!expect(select_gaussian_cleanup_polygon(*loaded.scene,selected,camera,triangle,false) == QByteArray("\0\0\1\1",4),"Lasso resurrected an already hidden splat")) return 1;
  GaussianSceneData layers;
  layers.points.resize(3);
  for (size_t i = 0; i < layers.points.size(); ++i) layers.points[i].center_opacity = {0,0,2.0F+2.0F*static_cast<float>(i),1};
  const QByteArray layer_mask(3,'\1');
  const QPolygonF layer_outline{{.4,.4},{.6,.4},{.6,.6},{.4,.6}};
  if (!expect(select_gaussian_cleanup(layers,layer_mask,camera,{.4,.4,.2,.2},false,{}, {3,5}) == QByteArray("\1\0\1",3) &&
      select_gaussian_cleanup_polygon(layers,layer_mask,camera,layer_outline,true,{}, {3,5}) == QByteArray("\0\1\0",3),
      "Depth range did not separate coincident near, middle and far splats")) return 1;
  auto depth_camera = camera; depth_camera.position = {0,0,1};
  if (!expect(select_gaussian_cleanup_polygon(layers,layer_mask,depth_camera,layer_outline,false,{}, {3,5}) == QByteArray("\1\0\0",3),
      "Cleanup depth is not relative to the current camera or excluded a boundary")) return 1;
  depth_camera.position = {}; depth_camera.forward = {1,0,0}; depth_camera.right = {0,0,-1};
  for (auto& point : layers.points) { point.center_opacity[0] = point.center_opacity[2]; point.center_opacity[2] = 0; }
  if (!expect(select_gaussian_cleanup_polygon(layers,layer_mask,depth_camera,layer_outline,true,{}, {3,5}) == QByteArray("\0\1\0",3),
      "Cleanup depth follows world Z rather than camera forward")) return 1;
  for (const auto& depth : {GaussianCleanupDepthRange{-1,5},GaussianCleanupDepthRange{5,3},GaussianCleanupDepthRange{3,3},
      GaussianCleanupDepthRange{0,std::numeric_limits<double>::quiet_NaN()}})
    if (!expect(select_gaussian_cleanup_polygon(layers,layer_mask,depth_camera,layer_outline,false,{},depth).isEmpty() &&
        select_gaussian_cleanup(layers,layer_mask,depth_camera,{.4,.4,.2,.2},false,{},depth).isEmpty(),"Invalid cleanup depth accepted")) return 1;
  camera.photo.focal_scale = 2;
  const auto zoom = select_gaussian_cleanup(*loaded.scene, mask, camera, {0.78, 0.4, 0.1, 0.2}, true);
  if (!expect(zoom == QByteArray("\0\0\1\0", 4), "Selection ignored focal framing")) return 1;
  if (!expect(select_gaussian_cleanup_polygon(*loaded.scene,mask,camera,QPolygonF{{.78,.4},{.88,.4},{.88,.6},{.78,.6}},true) == zoom,
      "Lasso ignored photographic focal framing")) return 1;
  const auto filtered = filter_gaussian_cleanup(*loaded.scene, mask, 0.02, 0.5);
  if (!expect(filtered == QByteArray("\0\1\0\1", 4), "Opacity or rotation-invariant size filter failed")) return 1;
  const auto applied = apply_gaussian_cleanup(loaded.scene, selected, project, canceled);
  if (!expect(applied.scene && applied.scene->points.size() == 3 && applied.scene->cleanup_source == loaded.scene, "Cleanup did not preserve original immutable data")) return 1;
  const auto persisted = read(QDir(project).filePath("cleanup.json"));
  const auto reopened = restore_gaussian_cleanup(loaded, project, canceled);
  if (!expect(reopened.scene && reopened.scene->cleanup_mask == selected && reopened.scene->points.size() == 3, "Cleanup did not survive reopening")) return 1;
  if (!expect(!apply_gaussian_cleanup(loaded.scene, QByteArray(4, '\0'), project, canceled).scene &&
      read(QDir(project).filePath("cleanup.json")) == persisted, "Empty cleanup corrupted the saved edit")) return 1;
  const auto destination = root.filePath("cleaned.ply");
  const auto exported = export_gaussian_cleanup(artifact, destination, *loaded.scene, selected, canceled);
  if (!expect(exported.value("ok").toBool(), "Cleaned PLY export failed")) return 1;
  auto expected_header = header; expected_header.replace("element vertex 4", "element vertex 3");
  const auto row_bytes = properties.size() * 4;
  const auto expected = expected_header + payload.left(row_bytes) + payload.mid(row_bytes * 2);
  if (!expect(read(destination) == expected && read(artifact) == bytes, "PLY export changed a retained coefficient or original artifact") ||
      !expect(!export_gaussian_cleanup(artifact, destination, *loaded.scene, selected, canceled).value("ok").toBool(), "PLY export clobbered an output")) return 1;
  const auto validated = validate_gaussian_ply(destination, canceled);
  const auto export_receipt = QJsonDocument::fromJson(read(destination + ".splat.json")).toVariant().toMap();
  if (!expect(export_receipt.value("exportedArtifactSha256") == validated.value("artifactSha256"), "Export receipt did not pin the exported bytes")) return 1;
  const auto roundtrip = load_gaussian_scene(destination, {}, validated.value("artifactSha256").toString());
  if (!expect(roundtrip.scene && roundtrip.scene->points.size() == 3 && roundtrip.scene->points[1].sh == loaded.scene->points[2].sh, "Export/import SH roundtrip failed")) return 1;
  const auto original_output = root.filePath("original-cap.ply");
  if (!expect(export_gaussian_cleanup(artifact,original_output,*loaded.scene,selected,canceled,{},GaussianAlphaModel::Inria99).value("ok").toBool() &&
      read(original_output) == expected,"Profiled export changed original rows")) return 1;
  const auto original_project = root.filePath("original-cap-project");
  const auto profile_import = import_gaussian_ply(original_output,original_project,canceled);
  const auto profile = load_gaussian_composition(original_project,validated.value("artifactSha256").toString());
  if (!expect(profile_import.value("ok").toBool() && profile.composition && profile.composition->alpha_model == GaussianAlphaModel::Inria99,
      "Splat export/import lost the opacity convention")) return 1;
  const auto profile_receipt = QJsonDocument::fromJson(read(original_output+".splat.json")).toVariant().toMap();
  for (int fault = 0; fault < 3; ++fault) {
    auto invalid = profile_receipt;
    if (fault == 0) invalid["alphaModel"] = "unknown";
    if (fault == 1) invalid["alphaModel"] = 1;
    if (fault == 2) invalid["exportedArtifactSha256"] = QString(64,'0');
    const auto target = root.filePath(QStringLiteral("invalid-profile-%1").arg(fault));
    if (!write(original_output+".splat.json",QJsonDocument::fromVariant(invalid).toJson()) ||
        !expect(!import_gaussian_ply(original_output,target,canceled).value("ok").toBool() && !QFileInfo::exists(target),"Invalid profile import published a project")) return 1;
  }
  const auto invalid_output = root.filePath("invalid-enum.ply");
  if (!expect(!export_gaussian_cleanup(artifact,invalid_output,*loaded.scene,selected,canceled,{},static_cast<GaussianAlphaModel>(2)).value("ok").toBool() &&
      !QFileInfo::exists(invalid_output),"Invalid C++ profile published a splat")) return 1;
  canceled->store(true);
  if (!expect(select_gaussian_cleanup_polygon(*loaded.scene,mask,camera,triangle,false,canceled).isEmpty(),"Canceled lasso produced a mask")) return 1;
  const auto canceled_output = root.filePath("canceled.ply");
  if (!expect(!apply_gaussian_cleanup(loaded.scene, selected, project, canceled).scene &&
      !export_gaussian_cleanup(artifact, canceled_output, *loaded.scene, selected, canceled).value("ok").toBool() &&
      !QFileInfo::exists(canceled_output), "Canceled operation published output")) return 1;
  canceled->store(false);
  auto tampered = persisted; tampered.replace(hash.toLatin1(), QByteArray(64, '0'));
  if (!write(QDir(project).filePath("cleanup.json"), tampered) ||
      !expect(!restore_gaussian_cleanup(loaded, project, canceled).scene, "Cleanup accepted a different artifact hash")) return 1;
  if (!write(artifact, bytes + 'x')) return 2;
  const auto stale_output = root.filePath("stale.ply");
  if (!expect(!export_gaussian_cleanup(artifact, stale_output, *loaded.scene, selected, canceled).value("ok").toBool() &&
      !QFileInfo::exists(stale_output), "Export accepted changed original bytes")) return 1;
  std::cout << "Gaussian cleanup: projection, crop, filters, persistence, cancel, integrity, exact SH/PLY roundtrip and no-clobber pass.\n";
  return 0;
}
