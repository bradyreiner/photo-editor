#include "gaussian_physical_camera.h"
#include "gaussian_lens_sampling.h"
#include "gaussian_scene_renderer.h"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <iostream>

using namespace photo_editor::desktop;
QImage read_gaussian_photo_texture(const GaussianFrame& frame);
namespace {
struct Moments { double mean = 0, variance = 0, energy = 0; };
Moments moments(const QImage& image, int center_x, int radius) {
  Moments m; double second = 0;
  for (int y = image.height()/2-radius; y < image.height()/2+radius; ++y)
    for (int x = center_x-radius; x < center_x+radius; ++x) {
      const double code = image.pixelColor(x,y).redF();
      const double linear = code <= 0.04045 ? code/12.92 : std::pow((code+0.055)/1.055,2.4);
      m.energy += linear; m.mean += (x+0.5)*linear; second += (x+0.5)*(x+0.5)*linear;
    }
  m.mean /= m.energy; m.variance = second/m.energy-m.mean*m.mean;
  return m;
}
int maximum_error(const QImage& a, const QImage& b, QRect region) {
  if (a.isNull() || b.isNull() || a.size() != b.size()) return 256;
  int result = 0;
  for (int y = region.top(); y <= region.bottom(); ++y) for (int x = region.left(); x <= region.right(); ++x)
    result = std::max(result,std::abs(a.pixelColor(x,y).red()-b.pixelColor(x,y).red()));
  return result;
}
}
int main(int argc, char** argv) {
  QGuiApplication app(argc,argv);
  const auto fail = [](const char* message) { std::cerr << message << '\n'; return 1; };
  const auto renderer = make_gaussian_scene_renderer();
  if (!renderer) return fail("Metal renderer unavailable");
  GaussianCamera pose; pose.photo.lens.samples = 128;
  GaussianPhysicalCameraSettings physical; physical.metres_per_scene_unit = 1;
  physical.f_number = 1.4; physical.focus_distance_m = 1;
  const auto built = make_gaussian_physical_camera(pose,{600,400},physical);
  if (!built.camera) return fail("Physical camera conversion failed");
  const auto camera = *built.camera;
  auto scene = std::make_shared<GaussianSceneData>();
  for (const auto depth : {0.5F,1.0F,2.0F}) {
    const float center_x = depth == 0.5F ? 150 : depth == 1 ? 300 : 450;
    GaussianPoint p;
    p.center_opacity = {(center_x-camera.cx)*depth/camera.fx,0,depth,0.999F};
    const float sigma = depth/camera.fx;
    p.covariance_a = {sigma*sigma,0,0,sigma*sigma}; p.covariance_b = {0,1e-12F,0,0};
    for (size_t channel = 0; channel < 3; ++channel) p.sh[channel] = 0.5F/0.2820947918F;
    scene->points.push_back(p);
  }
  auto pinhole = camera; pinhole.physical.reset(); pinhole.photo.lens.aperture_radius = 0;
  const auto sharp = renderer->render(scene,pinhole,true);
  const auto optical = renderer->render(scene,camera,true);
  const auto texture = renderer->render(scene,camera,false);
  if (!sharp.error.isEmpty() || !optical.error.isEmpty() || !texture.error.isEmpty() ||
      read_gaussian_photo_texture(texture) != optical.image) return fail("Physical texture/export parity");
  if (maximum_error(sharp.image,optical.image,{268,168,64,64}) > 1) return fail("Focused plane lost sharpness");
  const auto offsets = gaussian_aperture_samples(camera.photo.lens);
  double offset_mean = 0, offset_second = 0;
  for (const auto offset : offsets) { offset_mean += offset.x()/128; offset_second += offset.x()*offset.x()/128; }
  const double offset_variance = offset_second-offset_mean*offset_mean;
  QVariantList measurements;
  for (int i = 0; i < 3; ++i) {
    const float depth = scene->points[static_cast<size_t>(i)].center_opacity[2];
    const auto a = moments(sharp.image,150+150*i,40), b = moments(optical.image,150+150*i,40);
    const double factor = camera.fx*(1.0/camera.photo.lens.focus_distance-1.0/depth);
    const double expected = offset_variance*factor*factor;
    const double measured = b.variance-a.variance;
    measurements.append(QVariantMap{{"depthM",depth},{"predictedDefocusVariancePixels",expected},
        {"measuredDefocusVariancePixels",measured},{"pinholeEnergy",a.energy},{"opticalEnergy",b.energy}});
    if (!std::isfinite(measured) || std::abs(measured-expected) > std::max(0.02,expected*0.01)) {
      std::cerr << QJsonDocument::fromVariant(measurements).toJson().constData(); return fail("Physical bokeh spread differs from pupil prediction");
    }
  }
  physical.f_number = 2.8;
  const auto stopped_camera = make_gaussian_physical_camera(pose,{600,400},physical);
  const auto stopped = renderer->render(scene,*stopped_camera.camera,true);
  if (!stopped.error.isEmpty()) return fail("Stopped-down render failed");
  const double wide_variance = moments(optical.image,150,40).variance-moments(sharp.image,150,40).variance;
  const double narrow_variance = moments(stopped.image,150,40).variance-moments(sharp.image,150,40).variance;
  if (std::abs(narrow_variance/wide_variance-0.25) > 0.003) return fail("Doubling f-number must halve blur diameter");
  physical.f_number = 1.4;
  const auto large_camera = make_gaussian_physical_camera(pose,{1200,800},physical);
  const auto large = renderer->render(scene,*large_camera.camera,true);
  auto large_pinhole = *large_camera.camera; large_pinhole.physical.reset(); large_pinhole.photo.lens.aperture_radius = 0;
  const auto large_sharp = renderer->render(scene,large_pinhole,true);
  if (!large.error.isEmpty() || !large_sharp.error.isEmpty()) return fail("Double-resolution render failed");
  const double large_variance = moments(large.image,300,80).variance-moments(large_sharp.image,300,80).variance;
  if (std::abs(large_variance/wide_variance-4) > 0.04) return fail("Physical blur scale changes across resolutions");
  auto scaled_scene = std::make_shared<GaussianSceneData>(*scene);
  for (auto& p : scaled_scene->points) {
    for (size_t i = 0; i < 3; ++i) p.center_opacity[i] *= 100;
    for (auto& c : p.covariance_a) c *= 10000;
    for (auto& c : p.covariance_b) c *= 10000;
  }
  physical.metres_per_scene_unit = 0.01;
  const auto scaled_camera = make_gaussian_physical_camera(pose,{600,400},physical);
  const auto scaled = renderer->render(scaled_scene,*scaled_camera.camera,true);
  const int scale_error = maximum_error(optical.image,scaled.image,optical.image.rect());
  if (!scaled.error.isEmpty() || scale_error > 1) return fail("Equivalent scene units changed physical photo");
  const QVariantMap receipt{{"schema","framehouse-physical-camera-proof-v1"},{"passed",true},
      {"scope","synthetic-ideal-thin-lens-not-measured-lens-or-scene-calibration"},
      {"physicalInput",built.calibration},{"resolvedCamera",camera.description()},
      {"measurements",measurements},{"doubleFNumberVarianceRatio",narrow_variance/wide_variance},
      {"doubleResolutionVarianceRatio",large_variance/wide_variance},{"sceneScaleMaxCodeError",scale_error},
      {"focusedPlaneMaxCodeError",maximum_error(sharp.image,optical.image,{268,168,64,64})}};
  if (app.arguments().size() == 2) {
    const QDir output(app.arguments()[1]);
    if (!output.isAbsolute() || output.exists() || !QDir().mkpath(output.path())) return fail("Choose a new absolute proof directory");
    if (!sharp.image.save(output.filePath("pinhole.png")) || !optical.image.save(output.filePath("50mm-f1.4-focus-1m.png")) ||
        !stopped.image.save(output.filePath("50mm-f2.8-focus-1m.png")) || !large.image.save(output.filePath("double-resolution.png"))) return 1;
    QFile file(output.filePath("receipt.json"));
    if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly) || file.write(QJsonDocument::fromVariant(receipt).toJson()) < 0) return 1;
  }
  std::cout << QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Compact).constData() << '\n';
  return 0;
}
