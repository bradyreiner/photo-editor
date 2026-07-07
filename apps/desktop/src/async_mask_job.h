#pragma once

#include <QString>

namespace photo_editor::desktop {

struct AsyncMaskJob final {
  QString job_id;
  QString job_kind;
  QString stage;
  QString asset_id;
  QString file_path;
  QString state_signature;
  int generation = 0;
  int state_revision = 0;
};

}  // namespace photo_editor::desktop
