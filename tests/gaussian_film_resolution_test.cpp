#include "gaussian_scene_renderer.h"
#include <QGuiApplication>
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using namespace photo_editor::desktop;
namespace {
GaussianPoint spot(const GaussianCamera& camera,float x,float y,float z,float sigma,
    std::array<float,3> color,float opacity) {
  GaussianPoint point;
  point.center_opacity={(x-camera.cx)*z/camera.fx,(y-camera.cy)*z/camera.fy,z,opacity};
  const float world_sigma=sigma*z/camera.fx;
  point.covariance_a={world_sigma*world_sigma,0,0,world_sigma*world_sigma};
  point.covariance_b={0,1e-12F,0,0};
  for (size_t channel=0;channel<3;++channel) point.sh[channel]=(color[channel]-0.5F)/0.2820947918F;
  return point;
}
double decode(int code) {
  const double value=code/255.0;
  return value<=0.04045 ? value/12.92 : std::pow((value+0.055)/1.055,2.4);
}
double density_correlation(const QImage& clean,const QImage& film,int lag) {
  constexpr int edge=512;
  std::vector<double> field(edge*edge);
  double mean=0;
  for (int y=0;y<edge;++y) {
    const auto* source=reinterpret_cast<const QRgb*>(clean.constScanLine(128+y));
    const auto* output=reinterpret_cast<const QRgb*>(film.constScanLine(128+y));
    for (int x=0;x<edge;++x) {
      const double value=std::log(decode(qRed(output[128+x]))/decode(qRed(source[128+x])));
      field[static_cast<size_t>(y*edge+x)]=value; mean+=value/(edge*edge);
    }
  }
  double variance=0,covariance=0;
  for (int y=0;y<edge-lag;++y) for (int x=0;x<edge-lag;++x) {
    const double a=field[static_cast<size_t>(y*edge+x)]-mean;
    variance+=a*a;
    covariance+=a*(field[static_cast<size_t>(y*edge+x+lag)]-mean);
  }
  return covariance/variance;
}
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv);
  const auto fail=[](const char* reason) { std::cerr<<"8K film: "<<reason<<'\n'; return 1; };
  const auto output=app.arguments().value(1);
  if (!output.isEmpty() && (!QDir::isAbsolutePath(output) || QFileInfo::exists(output) || !QDir().mkpath(output))) return 2;
  auto renderer=make_gaussian_scene_renderer(); if (!renderer) return fail("Metal unavailable");
  GaussianCamera camera; camera.image_size={8192,8192}; camera.fx=camera.fy=8192; camera.cx=camera.cy=4096;
  auto scene=std::make_shared<GaussianSceneData>();
  auto background=spot(camera,4096,4096,3,819200,{0.7F,0.7F,0.7F},0.9F);
  scene->points.push_back(background);
  for (const auto pixel:{QPointF(2048.5,2048.5),QPointF(4096.5,3072.5),QPointF(6144.5,6144.5)})
    scene->points.push_back(spot(camera,static_cast<float>(pixel.x()),static_cast<float>(pixel.y()),2,3,{4,1.6F,0.4F},0.999F));
  const auto clean=renderer->render(scene,camera,true);
  if (!clean.error.isEmpty()) return fail("maximum-resolution neutral export");
  camera.photo.film.film_plane=true; camera.photo.film.grain_amount=0.6F; camera.photo.film.grain_microns=8;
  camera.photo.film.halation_amount=0.3F; camera.photo.film.halation_microns=60;
  camera.photo.film.return_rgb={0.8F,0.25F,0.05F}; camera.photo.film.seed=8675309;
  const auto large_tiles=renderer->render(scene,camera,true);
  GaussianRenderOptions small; small.export_tile_edge=1024;
  const auto small_tiles=renderer->renderWithOptions(scene,camera,true,{},small);
  if (!large_tiles.error.isEmpty() || !small_tiles.error.isEmpty() ||
      large_tiles.image.size()!=QSize(8192,8192) || small_tiles.image.size()!=QSize(8192,8192) ||
      large_tiles.metrics.value("tileCount").toInt()!=16 || small_tiles.metrics.value("tileCount").toInt()!=64 ||
      !large_tiles.metrics.value("filmEffectsApplied").toBool() || large_tiles.metrics.value("tileHalo").toInt()<60)
    return fail("maximum-resolution film tile/halo contract");
  int maximum_error=0; quint64 different_pixels=0;
  for (int y=0;y<8192;++y) {
    const auto* a=reinterpret_cast<const QRgb*>(large_tiles.image.constScanLine(y));
    const auto* b=reinterpret_cast<const QRgb*>(small_tiles.image.constScanLine(y));
    for (int x=0;x<8192;++x) {
      const int error=std::max({std::abs(qRed(a[x])-qRed(b[x])),std::abs(qGreen(a[x])-qGreen(b[x])),std::abs(qBlue(a[x])-qBlue(b[x]))});
      maximum_error=std::max(maximum_error,error); if (error) ++different_pixels;
    }
  }
  if (maximum_error>1) return fail("grain or halation changed across 16/64 tile partitions");
  auto half=camera.resized({4096,4096}); half.photo.film={};
  const auto half_clean=renderer->render(scene,half,true); half.photo.film=camera.photo.film;
  const auto half_film=renderer->render(scene,half,true);
  if (!half_clean.error.isEmpty() || !half_film.error.isEmpty()) return fail("half-resolution comparison");
  const double full_correlation=density_correlation(clean.image,large_tiles.image,4);
  const double half_correlation=density_correlation(half_clean.image,half_film.image,2);
  // The 24 mm square gate gives the same 11.71875 um separation in both
  // recordings. Quantization and finite spectral sampling limit this estimate.
  if (!std::isfinite(full_correlation) || !std::isfinite(half_correlation) ||
      full_correlation<0.15 || full_correlation>0.6 || half_correlation<0.15 || half_correlation>0.6 ||
      std::abs(full_correlation-half_correlation)>0.12) return fail("grain correlation lost its physical scale");
  const QVariantMap receipt{{"passed",true},{"pixels",8192ULL*8192},{"maximumChannelCodeError",maximum_error},
      {"differentPixels",different_pixels},{"largeTiles",large_tiles.metrics},{"smallTiles",small_tiles.metrics},
      {"grainCorrelation8K",full_correlation},{"grainCorrelation4K",half_correlation},{"correlationSeparationMicrons",11.71875},
      {"scope","Synthetic native maximum-resolution grain/halation, tile-partition invariance and physical-scale check; not a photographic film-stock match"}};
  if (!output.isEmpty()) {
    if (!large_tiles.image.save(QDir(output).filePath("film-8192.png")) || !clean.image.save(QDir(output).filePath("clean-8192.png")) ||
        !large_tiles.image.copy(1536,1536,1024,1024).save(QDir(output).filePath("tile-corner-detail.png")) ||
        !large_tiles.image.copy(128,128,1024,1024).save(QDir(output).filePath("grain-detail.png"))) return 2;
    QFile file(QDir(output).filePath("receipt.json"));
    if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly) || file.write(QJsonDocument::fromVariant(receipt).toJson())<0) return 2;
  }
  std::cout<<QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Compact).constData()<<'\n'; return 0;
}
