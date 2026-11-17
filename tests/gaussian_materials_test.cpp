#include "gaussian_scene_renderer.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
bool require(bool pass,const char* reason) { if (!pass) std::cerr << reason << '\n'; return pass; }
bool write(const QString& path,const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
bool near(const QImage& a,const QImage& b,int tolerance = 1) {
  if (a.isNull() || a.size() != b.size()) return false;
  for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) {
    const auto p = a.pixelColor(x,y), q = b.pixelColor(x,y);
    if (std::max({std::abs(p.red()-q.red()),std::abs(p.green()-q.green()),std::abs(p.blue()-q.blue())}) > tolerance) return false;
  }
  return true;
}
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv); QTemporaryDir directory;
  const auto obj_path = QDir(directory.path()).filePath("surface.obj"), mtl_path = QDir(directory.path()).filePath("surface materials.mtl");
  const auto image_path = QDir(directory.path()).filePath("surface image.png");
  const QByteArray obj("mtllib surface materials.mtl\nv -1 -1 3\nv 1 -1 3\nv 1 1 3\nv -1 1 3\nvt 0 1\nvt 1 1\nvt 1 0\nvt 0 0\nusemtl Surface\nf -4/-4 -3/-3 -2/-2 -1/-1\n");
  const QByteArray mtl("newmtl Surface\nKd 1 1 1\nNs 200\nPr 0.4\nPm 0.25\nKe 0 0 0\nmap_Kd -s 1 1 1 -o 0 0 0 -clamp off surface image.png\n");
  QImage pattern(2,2,QImage::Format_RGBA8888); pattern.setColorSpace(QColorSpace::SRgb);
  pattern.setPixelColor(0,0,Qt::red); pattern.setPixelColor(1,0,Qt::green); pattern.setPixelColor(0,1,Qt::blue); pattern.setPixelColor(1,1,Qt::yellow);
  if (!write(obj_path,obj) || !write(mtl_path,mtl) || !pattern.save(image_path)) return 1;
  const auto geometry = load_gaussian_obj(obj_path); if (!require(geometry.mesh && geometry.mesh->material_names.size() == 2,"OBJ lost face materials")) return 1;
  const auto imported = load_gaussian_obj_materials(obj_path,*geometry.mesh);
  if (!require(imported.assets && imported.assets->materials.size() == 2 && imported.assets->textures.size() == 1 && imported.error.isEmpty(),"MTL/image import failed")) { std::cerr << imported.error.toStdString(); return 1; }
  const auto& surface = imported.assets->materials[1];
  if (!require(surface.roughness == 0.4F && surface.metallic == 0.25F && surface.uv_scale == QVector2D(1,1) && !surface.clamp,"MTL physical surface coefficients or texture transform changed")) return 1;
  if (!require(gaussian_surface_material(surface.description()).has_value(),"Surface material roundtrip failed")) return 1;
  auto invalid = surface.description(); invalid["roughness"] = -1;
  if (!require(!gaussian_surface_material(invalid),"Negative material roughness accepted")) return 1;
  if (!require(store_gaussian_mesh_asset(directory.path(),geometry).isEmpty() && store_gaussian_material_assets(directory.path(),imported).isEmpty(),"Material/texture asset publication failed")) return 1;
  GaussianSceneObject object; object.id = "mesh"; object.name = "Surface"; object.kind = 2; object.position = {}; object.size = {1,1,1}; object.color = {1,1,1};
  object.mesh_sha256 = geometry.mesh->sha256; object.mesh = geometry.mesh; object.material_set_sha256 = imported.assets->sha256; object.material_assets = imported.assets; object.materials = imported.assets->materials;
  GaussianSceneComposition composition; composition.objects = {object}; composition.ambient = 1;
  const auto saved = save_gaussian_composition(directory.path(),"artifact",composition,{});
  const auto reopened = load_gaussian_composition(directory.path(),"artifact");
  if (!require(saved.error.isEmpty() && reopened.composition && reopened.composition->description() == composition.description(),"Textured composition persistence failed")) { std::cerr << saved.error.toStdString(); return 1; }
  if (!require(reopened.composition->objects[0].material_assets->textures.value(surface.texture_sha256) == imported.assets->textures.value(surface.texture_sha256),"Immutable decoded image was duplicated on reopen")) return 1;
  auto renderer = make_gaussian_scene_renderer(); GaussianCamera camera; camera.image_size = {129,129}; camera.fx = camera.fy = 120; camera.cx = camera.cy = 64.5F;
  auto scene = std::make_shared<GaussianSceneData>(); GaussianPoint point; point.center_opacity = {0,0,-4,0.5F}; point.covariance_a = {0.04F,0,0,0.04F}; point.covariance_b = {0,0.04F,0,0}; scene->points = {point};
  const auto render = [&] { camera.composition = std::make_shared<GaussianSceneComposition>(composition); return renderer->render(scene,camera,true); };
  const auto image = render();
  if (!require(image.error.isEmpty() && image.metrics.value("colorImageCount").toInt() == 1,"Textured native surface did not render")) { std::cerr << image.error.toStdString(); return 1; }
  for (const auto& sample : {std::pair<QPoint,QColor>{{44,44},Qt::red},{{84,44},Qt::green},{{44,84},Qt::blue},{{84,84},Qt::yellow}}) {
    const auto actual = image.image.pixelColor(sample.first); const auto expected = sample.second;
    if (!require(std::abs(actual.red()-expected.red()) <= 2 && std::abs(actual.green()-expected.green()) <= 2 && std::abs(actual.blue()-expected.blue()) <= 2,"OBJ UV orientation, face interpolation or sRGB image sampling is incorrect")) { std::cerr << actual.name().toStdString(); return 1; }
  }
  composition.objects[0].materials[1].color = {0.25F,0.25F,0.25F}; const auto tinted = render();
  // Linear reflectance 0.25 encodes to 137; averaging encoded color would
  // instead give 64. This oracle is independent of the native texture path.
  if (!require(std::abs(tinted.image.pixelColor(44,44).red()-137) <= 2 && !tinted.metrics.value("resolvedSourceReused").toBool() && tinted.metrics.value("colorImagesReused").toBool() && tinted.metrics.value("colorImageUploadBytes").toLongLong() == 0,"MTL color factors were applied in the wrong color space or reused stale pixels")) return 1;
  composition.objects[0].materials[1].color = {1,1,1}; composition.objects[0].materials[1].uv_offset = {1,0}; const auto repeated = render();
  if (!require(near(repeated.image,image.image,1),"Repeated texture UV offset changed its appearance")) return 1;
  composition.objects[0].materials[1].clamp = true; const auto clamped = render();
  if (!require(clamped.image.pixelColor(44,44).green() > 250 && clamped.image.pixelColor(44,44).red() < 2,"Texture clamp mode sampled wrapped pixels")) return 1;
  composition.objects[0].materials[1].clamp = false; composition.objects[0].materials[1].uv_offset = {}; composition.objects[0].materials[1].use_texture = false;
  if (!require(render().image.pixelColor(44,44) == QColor(Qt::white),"Disabled color map did not use its base material")) return 1;
  composition.objects[0].use_imported_materials = false; composition.objects[0].color = {0.2F,0.4F,0.6F};
  if (!require(render().image.pixelColor(44,44) == QColor(51,102,153),"Single-surface override did not replace imported face materials")) return 1;
  composition.objects[0] = object; composition.ambient = 0; composition.objects[0].materials[1].emission = {0.25F,0,1}; const auto emitted = render();
  if (!require(std::abs(emitted.image.pixelColor(44,44).red()-137) <= 1 && emitted.image.pixelColor(44,44).green() == 0 && emitted.image.pixelColor(44,44).blue() == 255,"Colored material emission did not retain linear radiance")) return 1;
  composition.objects[0] = object; composition.ambient = 1;
  GaussianRenderOptions tiles; tiles.export_tile_edge = 64; const auto full = render(); const auto tiled = renderer->renderWithOptions(scene,camera,true,{},tiles);
  if (!require(tiled.error.isEmpty() && near(full.image,tiled.image,0),"Textured surfaces changed at tile boundaries")) return 1;
  camera.photo.lens = {3,0.15F,0,0,0,32}; const auto lens = render(); const auto lens_tiles = renderer->renderWithOptions(scene,camera,true,{},tiles);
  if (!require(lens.error.isEmpty() && lens_tiles.error.isEmpty() && near(lens.image,full.image,1) && near(lens.image,lens_tiles.image,1),"Texture ray footprints changed under focused optics or aperture tiling")) {
    const auto difference = [](const QImage& a,const QImage& b) { int maximum = 0, count = 0, mx = 0, my = 0; for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) { const auto p = a.pixelColor(x,y),q = b.pixelColor(x,y); const int d = std::max({std::abs(p.red()-q.red()),std::abs(p.green()-q.green()),std::abs(p.blue()-q.blue())}); if (d > 1) ++count; if (d > maximum) { maximum = d; mx = x; my = y; } } std::cerr << " max=" << maximum << " count=" << count << " at=" << mx << "," << my; };
    std::cerr << " lens/full"; difference(lens.image,full.image); std::cerr << " lens/tiles"; difference(lens.image,lens_tiles.image); std::cerr << lens.error.toStdString() << lens_tiles.error.toStdString(); return 1;
  }
  camera.photo = {}; composition.haze_density = 0.05F; const auto haze_full = render(); const auto haze_tiles = renderer->renderWithOptions(scene,camera,true,{},tiles);
  if (!require(haze_tiles.error.isEmpty() && near(haze_full.image,haze_tiles.image,0),"Textured haze rays changed across tiles")) return 1;
  composition.haze_density = 0; composition.ambient = 0;
  GaussianSceneLight light; light.id = "light"; light.name = "Key"; light.vector = {-1,0,-1}; light.intensity = 3; composition.lights = {light};
  const auto unshadowed = render(); GaussianSceneObject blocker; blocker.id = "blocker"; blocker.name = "Blocker"; blocker.position = {-0.5F,0,2.5F}; blocker.size = {0.15F,0.15F,0.15F}; composition.objects.push_back(blocker);
  const auto shadow = render();
  if (!require(unshadowed.image.pixelColor(64,64).red() > 50 && shadow.image.pixelColor(64,64) == QColor(Qt::black),"Textured surfaces did not receive object shadows")) return 1;
  // High-frequency minification must average linear light: a black/white
  // checker converges to sRGB 188, independently of UV offset or tile edge.
  QImage checker(256,256,QImage::Format_RGBA8888);
  for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x) checker.setPixelColor(x,y,(x+y)%2 ? Qt::white : Qt::black);
  if (!checker.save(image_path)) return 1;
  const auto minified_assets = load_gaussian_obj_materials(obj_path,*geometry.mesh); if (!require(bool(minified_assets.assets),"Checker import failed")) return 1;
  composition = GaussianSceneComposition{}; composition.ambient = 1; composition.objects = {object};
  composition.objects[0].material_assets = minified_assets.assets; composition.objects[0].material_set_sha256 = minified_assets.assets->sha256; composition.objects[0].materials = minified_assets.assets->materials;
  composition.objects[0].materials[1].uv_scale = {32,32}; const auto minified = render(); const auto minified_tiles = renderer->renderWithOptions(scene,camera,true,{},tiles);
  if (!require(minified.error.isEmpty() && std::abs(minified.image.pixelColor(64,64).red()-188) <= 2 && near(minified.image,minified_tiles.image,0),"Texture mipmaps did not average linear light or minification differed across tiles")) { std::cerr << minified.error.toStdString() << " checker=" << minified.image.pixelColor(64,64).red(); return 1; }
  // Missing dependencies and incomplete UVs must fail rather than silently
  // flattening a requested textured import.
  if (!write(mtl_path,"newmtl Surface\nmap_Kd missing.png\n")) return 1;
  if (!require(!load_gaussian_obj_materials(obj_path,*geometry.mesh).error.isEmpty(),"Missing texture imported silently")) return 1;
  if (!write(mtl_path,"newmtl Surface\nNs 250\nmap_Kd \"surface image.png\"\n")) return 1;
  const auto legacy_ns = load_gaussian_obj_materials(obj_path,*geometry.mesh);
  if (!require(legacy_ns.assets && legacy_ns.assets->materials[1].roughness == 0.5F,"Legacy exponent mapping or quoted color-image path failed")) return 1;
  if (!write(mtl_path,mtl)) return 1;
  const auto no_uv = parse_gaussian_obj("mtllib surface materials.mtl\nv -1 -1 3\nv 1 -1 3\nv 0 1 3\nusemtl Surface\nf 1 2 3\n");
  if (!require(no_uv.mesh && !load_gaussian_obj_materials(obj_path,*no_uv.mesh).error.isEmpty(),"Textured face without UVs imported silently")) return 1;
  auto alpha = pattern; alpha.setPixelColor(0,0,QColor(255,0,0,127)); if (!alpha.save(image_path)) return 1;
  const auto transparent = load_gaussian_obj_materials(obj_path,*geometry.mesh);
  if (!require(transparent.assets && !transparent.assets->warnings.isEmpty(),"Ignored alpha was not reported")) return 1;
  if (!QFile::remove(obj_path) || !QFile::remove(mtl_path) || !QFile::remove(image_path)) return 1;
  const auto standalone = load_gaussian_composition(directory.path(),"artifact");
  if (!require(standalone.composition && standalone.composition->description() == saved.composition->description(),"Project materials depended on removed source OBJ/MTL/images")) return 1;
  const auto frozen = gaussian_scene_composition(saved.composition->description());
  auto edited = *standalone.composition; edited.objects[0].materials[1].color = {0.1F,0.8F,0.2F};
  const auto changed = save_gaussian_composition(directory.path(),"artifact",edited,standalone.receipt);
  if (!require(frozen && changed.composition && changed.composition->description() != saved.composition->description(),"Frozen recipe test did not change the current scene")) return 1;
  const auto frozen_resources = resolve_gaussian_composition_resources(directory.path(),*frozen);
  if (!require(frozen_resources.composition && frozen_resources.composition->description() == saved.composition->description() &&
      frozen_resources.composition->objects[0].mesh && frozen_resources.composition->objects[0].material_assets,
      "Frozen geometry/material resolution used the mutable current recipe")) return 1;
  const auto recipe_path = QDir(directory.path()).filePath("scene-objects.json");
  if (!QFile::rename(recipe_path,recipe_path+".hidden")) return 1;
  const auto without_current_recipe = resolve_gaussian_composition_resources(directory.path(),*frozen);
  if (!QFile::rename(recipe_path+".hidden",recipe_path) || !require(without_current_recipe.composition &&
      without_current_recipe.composition->description() == saved.composition->description(),"Frozen resources depended on the current recipe")) return 1;
  const auto mesh_path = gaussian_mesh_asset_path(directory.path(),object.mesh_sha256);
  if (!QFile::rename(mesh_path,mesh_path+".hidden")) return 1;
  const auto missing_mesh = resolve_gaussian_composition_resources(directory.path(),*frozen);
  if (!QFile::rename(mesh_path+".hidden",mesh_path) || !require(!missing_mesh.composition && !missing_mesh.error.isEmpty(),"Frozen recipe accepted missing geometry")) return 1;
  QFile corrupt(gaussian_texture_asset_path(directory.path(),surface.texture_sha256));
  if (!corrupt.open(QIODevice::WriteOnly|QIODevice::Append) || corrupt.write("changed") != 7) return 1;
  corrupt.close();
  if (!require(!load_gaussian_composition(directory.path(),"artifact").error.isEmpty(),"Changed project texture passed its integrity check")) return 1;
  if (!require(!resolve_gaussian_composition_resources(directory.path(),*frozen).composition,"Frozen recipe accepted a changed texture")) return 1;
  std::cout << "Materials: portable OBJ/MTL/images, UV orientation/interpolation, linear color/emission/mipmaps, repeat/clamp, overrides, shadows, aperture/tile/haze parity, immutable sharing, missing UV/dependency rejection, notices and integrity checks pass.\n";
  return 0;
}
