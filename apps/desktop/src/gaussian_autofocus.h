#pragma once
#include "gaussian_focus_pick.h"
#include "gaussian_physical_camera.h"

namespace photo_editor::desktop {
struct GaussianAutofocusResult {
  GaussianCamera camera;
  QVariantMap report;
  bool canceled = false;
};
// Stateless per-pose/per-time resolution. An empty/invalid target uses the
// authored fallback, never an unrelated previous frame's focus. The unfiltered
// chief ray prevents output-resolution-dependent focal jumps from pixel blur.
inline GaussianAutofocusResult resolve_gaussian_autofocus(const GaussianSceneData& scene,
    const GaussianCamera& authored, std::optional<double> seconds,
    const std::shared_ptr<std::atomic_bool>& canceled = {}) {
  GaussianAutofocusResult result; result.camera = authored;
  if (!authored.photo.lens.auto_focus) return result;
  QElapsedTimer elapsed; elapsed.start();
  result.camera.photo.lens.auto_focus = false;
  const auto focus = pick_gaussian_focus(scene,authored,QPointF(authored.image_size.width()*0.5,authored.image_size.height()*0.5),seconds,canceled,0);
  if (focus.value("canceled").toBool() || (canceled && canceled->load())) { result.canceled = true; return result; }
  bool locked = false; QString reason = focus.value("error").toString();
  if (focus.value("ok").toBool()) {
    auto camera = result.camera;
    camera.photo.lens.focus_distance = focus.value("focusDistance").toFloat();
    if (!camera.photo.valid()) reason = "The centre surface is outside the focus range of the current aperture or barrel distance.";
    else if (authored.physical) {
      auto physical = authored.physical->settings;
      physical.focus_distance_m = focus.value("focusDistance").toDouble()*physical.metres_per_scene_unit;
      const auto configured = make_gaussian_physical_camera(camera,camera.image_size,physical,true);
      if (configured.camera) { result.camera = *configured.camera; locked = true; }
      else reason = configured.error;
    } else { result.camera = camera; locked = true; }
  }
  result.report = {{"mode","auto-centre"},{"model","unfiltered-chief-ray-opacity-median-v1"},{"locked",locked},
      {"status",locked ? "locked" : "fallback"},{"fallbackFocusDistance",authored.photo.lens.focus_distance},
      {"resolvedFocusDistance",result.camera.photo.lens.focus_distance},{"elapsedMs",static_cast<double>(elapsed.nsecsElapsed())/1e6}};
  if (locked) result.report.insert("surfaceDepth",focus.value("surfaceDepth"));
  else result.report.insert("reason",reason.left(256));
  if (result.camera.physical) result.report.insert("resolvedFocusDistanceM",result.camera.physical->settings.focus_distance_m);
  return result;
}
}  // namespace photo_editor::desktop
