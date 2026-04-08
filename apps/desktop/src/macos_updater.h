#pragma once

#include <QObject>

namespace photo_editor::desktop {

class MacosUpdater final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool available READ available CONSTANT)

 public:
  explicit MacosUpdater(QObject* parent = nullptr);
  ~MacosUpdater();

  MacosUpdater(const MacosUpdater&) = delete;
  MacosUpdater& operator=(const MacosUpdater&) = delete;

  [[nodiscard]] bool available() const;
  Q_INVOKABLE void checkForUpdates();

 private:
  void* controller_ = nullptr;
};

}  // namespace photo_editor::desktop
