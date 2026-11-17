#pragma once

#include <QAudio>

namespace photo_editor::desktop {

inline bool audioSinkErrorNeedsRecovery(QAudio::Error error) {
  return error != QAudio::NoError && error != QAudio::UnderrunError;
}

}  // namespace photo_editor::desktop
