#pragma once

#include "editor_recipe_save_queue.h"
#include <QLockFile>
#include <memory>

namespace photo_editor::desktop {

// An atomic local outbox, not a replacement catalog. Acknowledged entries are
// removed only by a subsequent atomic checkpoint. Never opens another authority.
class EditorRecipeSaveJournal final {
 public:
  bool open(const QString& app_data_root, const QString& authority,
            EditorRecipeSaveCheckpoint* recovered, QString* error);
  bool write(const EditorRecipeSaveCheckpoint& checkpoint, QString* error);
  bool isOpen() const { return lock_ && lock_->isLocked() && valid_; }
  QString path() const { return path_; }
  static QString currentAuthority();

 private:
  QString path_;
  QString authority_;
  std::unique_ptr<QLockFile> lock_;
  bool valid_ = false;
};

}  // namespace photo_editor::desktop
