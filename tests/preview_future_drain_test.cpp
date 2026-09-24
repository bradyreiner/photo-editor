#include "preview_future_drain.h"

#include <QCoreApplication>
#include <QSemaphore>
#include <QThreadPool>
#include <QtConcurrent>

#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

// Model the lifetime of the stack-owned destination without deliberately
// dereferencing freed storage in the failing-before control.
struct BufferLifetime {
  std::atomic_bool& alive;
  QSemaphore& returned;
  ~BufferLifetime() { alive = false; returned.release(); }
};

bool check_scope(bool guarded, int mode) {
  QThreadPool pool;
  pool.setMaxThreadCount(3);
  QVector<QFuture<bool>> futures;
  futures.reserve(2);
  QSemaphore worker_entered, release_worker, exit_attempt, returned;
  std::atomic_bool alive = true;
  std::atomic_bool worker_saw_live_buffer = false;
  std::atomic_bool escaped = false;
  std::atomic_bool supervisor_timed_out = false;
  bool caught = false;
  bool result = false;
  std::thread supervisor([&] {
    if (!exit_attempt.tryAcquire(1, 5000)) {
      supervisor_timed_out = true;
    } else {
      escaped = returned.tryAcquire(1, 100);
    }
    release_worker.release();
  });
  try {
    result = [&] {
      const BufferLifetime buffer_lifetime{alive, returned};
      const auto drain = guarded
          ? std::make_unique<photo_editor::desktop::PreviewFutureDrain<bool>>(futures)
          : nullptr;
      futures.push_back(QtConcurrent::run(&pool, [mode] {
        if (mode == 1) {
          throw std::runtime_error("failed tile");
        }
        return mode != 0;
      }));
      futures.push_back(QtConcurrent::run(&pool, [&] {
        worker_entered.release();
        release_worker.acquire();
        worker_saw_live_buffer = alive.load();
        return true;
      }));
      worker_entered.acquire();
      exit_attempt.release();
      if (mode == 2) {
        throw std::runtime_error("caller failed after submitting work");
      }
      for (auto& future : futures) {
        future.waitForFinished();
        if (!future.result()) {
          return false;
        }
      }
      return true;
    }();
  } catch (...) {
    caught = true;
  }
  supervisor.join();
  // Clean up the deliberately unguarded control, too.
  pool.waitForDone();
  const bool expected_result = mode == 3;
  const bool expected_exception = mode == 1 || mode == 2;
  const bool ok = !supervisor_timed_out && !escaped && worker_saw_live_buffer &&
      !alive && result == expected_result && caught == expected_exception;
  std::cout << "mode=" << mode << " guarded=" << guarded
            << " escaped=" << escaped << " workerSawLiveBuffer="
            << worker_saw_live_buffer << " exception=" << caught << '\n';
  return ok;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const bool guarded = !app.arguments().contains(QStringLiteral("--unguarded"));
  bool ok = true;
  for (int mode = 0; mode < 4; ++mode) {
    ok = check_scope(guarded, mode) && ok;
  }
  QVector<QFuture<bool>> empty;
  const photo_editor::desktop::PreviewFutureDrain<bool> empty_drain(empty);
  return ok ? 0 : 1;
}
