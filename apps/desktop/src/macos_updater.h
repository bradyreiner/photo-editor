#pragma once

namespace photo_editor::desktop {

class MacosUpdater final {
 public:
  MacosUpdater();
  ~MacosUpdater();

  MacosUpdater(const MacosUpdater&) = delete;
  MacosUpdater& operator=(const MacosUpdater&) = delete;

  [[nodiscard]] bool available() const;
  void checkForUpdates();

 private:
  void* controller_ = nullptr;
};

}  // namespace photo_editor::desktop
