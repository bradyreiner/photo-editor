#pragma once
#include "gaussian_scene_data.h"

namespace photo_editor::desktop {
// A bounded, data-only SafeTensors profile. This does not deserialize Python
// checkpoints, execute trainer code, or certify reconstruction quality.
GaussianSceneLoad load_gaussian_spacetime_artifact(const QString& path, const QString& expected_sha256,
    const std::shared_ptr<std::atomic_bool>& canceled = {});
QVariantMap write_gaussian_spacetime_artifact(const GaussianSceneData& scene, const QString& new_path,
    const std::shared_ptr<std::atomic_bool>& canceled = {});
QVariantMap validate_gaussian_spacetime_artifact(const QString& path, const QString& expected_sha256,
    const std::shared_ptr<std::atomic_bool>& canceled = {});
}
