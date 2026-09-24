#pragma once
#include "gaussian_scene_process.h"

namespace photo_editor::desktop {
QVariantMap prepare_gaussian_video(const QString& source, const QString& project, int maximum_edge,
    int frame_count, const QString& ffmpeg, const QString& ffprobe,
    const std::shared_ptr<std::atomic_bool>& canceled, const std::shared_ptr<GaussianProcessLog>& log);
}
