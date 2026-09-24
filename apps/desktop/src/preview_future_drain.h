#pragma once

#include <QFuture>
#include <QVector>

namespace photo_editor::desktop {

// Worker-side scope guard: futures do not join when destroyed. Declare this
// after the future collection and after every buffer captured by the tasks, so
// early returns and exception unwinding cannot outlive those buffers. Never use
// this to drain preview work on the GUI thread.
template <typename T>
class PreviewFutureDrain final {
 public:
  explicit PreviewFutureDrain(QVector<QFuture<T>>& futures) : futures_(futures) {}
  PreviewFutureDrain(const PreviewFutureDrain&) = delete;
  PreviewFutureDrain& operator=(const PreviewFutureDrain&) = delete;

  ~PreviewFutureDrain() noexcept {
    for (auto& future : futures_) {
      try {
        future.waitForFinished();
      } catch (...) {
        // Qt may rethrow a completed task's exception while waiting. Continue
        // joining the remaining tasks; result() is the caller's error channel.
      }
    }
  }

 private:
  QVector<QFuture<T>>& futures_;
};

}  // namespace photo_editor::desktop
