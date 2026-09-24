#pragma once
#include "gaussian_scene_data.h"
#include <optional>

namespace photo_editor::desktop {
std::optional<GaussianCamera> gaussian_saved_camera(const QVariantMap& description);
QVariantMap load_gaussian_saved_views(const QString& project, const GaussianSceneData& scene);
QVariantMap save_gaussian_view(const QString& project, const GaussianSceneData& scene,
    const QString& name, int capture_camera_index, const GaussianCamera& camera);
QVariantMap save_gaussian_camera_path(const QString& project, const GaussianSceneData& scene, const QVariantMap& settings);
}  // namespace photo_editor::desktop
