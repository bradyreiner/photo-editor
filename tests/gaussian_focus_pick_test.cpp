#include "gaussian_focus_pick.h"
#include <QCoreApplication>
#include <iostream>
using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto fail = [](int stage) { std::cerr << "Focus pick stage " << stage << '\n'; return 1; };
  GaussianCamera camera; camera.image_size = {100,100}; camera.fx = camera.fy = 100; camera.cx = camera.cy = 50.5F;
  const auto point = [](float depth, float opacity) {
    GaussianPoint p; p.center_opacity = {0,0,depth,opacity};
    p.covariance_a = {0.01F,0,0,0.01F}; p.covariance_b = {0,0.01F,0,0}; return p;
  };
  GaussianSceneData scene; scene.points = {point(6,0.9F),point(2,0.1F)};
  const auto pick = [&](QPointF pixel = {50.5,50.5}) { return pick_gaussian_focus(scene,camera,pixel,{},{}); };
  if (pick().value("focusDistance").toDouble() != 6) return fail(1);
  scene.points[1].center_opacity[3] = 0.9F;
  if (pick().value("focusDistance").toDouble() != 2) return fail(2);
  scene.points = {point(3,0.4F)};
  if (pick().value("ok").toBool()) return fail(3);
  scene.points = {point(3,0.9F)};
  scene.points[0].center_opacity[0] = 0.3F;
  camera.photo.focal_scale = 2;
  if (pick({70.5,50.5}).value("focusDistance").toDouble() != 3 || pick({0.5,0.5}).value("ok").toBool()) return fail(4);
  const auto pixel = gaussian_focus_pixel({100,100},{200,100},{100,50});
  if (!pixel || *pixel != QPointF(50.5,50.5) || gaussian_focus_pixel({100,100},{200,100},{49,50}) ||
      gaussian_focus_pixel({100,100},{200,100},{150,50})) return fail(5);
  const auto canceled = std::make_shared<std::atomic_bool>(true);
  if (!pick_gaussian_focus(scene,camera,{70.5,50.5},{},canceled).value("canceled").toBool()) return fail(6);
  camera.photo.focal_scale = 1; scene.points = {point(3,0.9F)};
  GaussianTemporalPoint motion; motion.velocity_time = {0.3F,0,0,0}; motion.normalization[0] = 1;
  motion.covariance_factor[0] = motion.covariance_factor[5] = motion.covariance_factor[10] = 0.1F;
  scene.temporal = {motion}; scene.local_time_range = {0,2};
  if (pick().value("ok").toBool() || !pick_gaussian_focus(scene,camera,{60.5,50.5},1,{}).value("ok").toBool() ||
      pick_gaussian_focus(scene,camera,{70.5,50.5},2,{}).value("ok").toBool()) return fail(7);
  std::cout << "Focus: opacity ordering, faint foreground rejection, focal scale, letterbox mapping, temporal motion and cancellation pass.\n";
  return 0;
}
