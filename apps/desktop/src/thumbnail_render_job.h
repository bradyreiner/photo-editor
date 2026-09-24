#pragma once

#include "edit_adjustment_patch.h"
#include "thumbnail_refresh_queue_service.h"

#include <QCryptographicHash>
#include <QUuid>

namespace photo_editor::desktop {

inline AsyncMaskJob make_thumbnail_adjustment_render_job(
    const QString& asset_id,
    const QString& file_path,
    const EditRecipeState& state,
    int state_revision) {
  const auto job_kind = QStringLiteral("photo_layer_thumbnail");
  return AsyncMaskJob{
      .job_id = QStringLiteral("%1-1-%2")
                    .arg(job_kind, QUuid::createUuid().toString(QUuid::WithoutBraces)),
      .job_kind = job_kind,
      .stage = QStringLiteral("shared_preview_thumbnail"),
      .asset_id = asset_id,
      .file_path = file_path,
      .state_signature = QStringLiteral("sha1:%1").arg(QString::fromLatin1(
          QCryptographicHash::hash(
              adjustment_patch_identity_json(state).toUtf8(),
              QCryptographicHash::Sha1)
              .toHex())),
      .generation = 1,
      .state_revision = state_revision,
  };
}

}  // namespace photo_editor::desktop
