#pragma once

#include "editor_recipe_save_queue.h"
#include "edit_recipe_mask_encoding_cache.h"
#include <QLockFile>
#include <memory>

namespace photo_editor::desktop {

// A durable local outbox with checksummed edit records and atomic compaction.
// Never opens another authority. Each successful write is durable before return.
class EditorRecipeSaveJournal final {
 public:
  bool open(const QString& app_data_root, const QString& authority,
            EditorRecipeSaveCheckpoint* recovered, QString* error);
  bool write(const EditorRecipeSaveCheckpoint& checkpoint, QString* error);
  bool archiveRecoverySnapshot(const EditorRecipeSaveSnapshot& snapshot, QString* archive_path, QString* error) const;
  bool isOpen() const { return lock_ && lock_->isLocked() && valid_; }
  bool canRetryOpen() const { return retryable_lock_conflict_; }
  QString path() const { return path_; }
  static QString currentAuthority();

 private:
  EditRecipeMaskEncodingCache mask_encoding_cache_;
  QJsonObject durable_payload_;
  QByteArray durable_digest_;
  qint64 durable_bytes_ = 0;
  int delta_records_ = 0;
  QString path_;
  QString authority_;
  std::unique_ptr<QLockFile> lock_;
  bool valid_ = false;
  bool retryable_lock_conflict_ = false;
};

}  // namespace photo_editor::desktop
