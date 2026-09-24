#pragma once
#include <QString>
#include <atomic>

namespace photo_editor::desktop {
struct SearchRuntimeVerification {
  bool ok = false;
  QString executable;
  QString error;
  int files = 0;
  qint64 bytes = 0;
};
// Blocking worker-only verification against an independent trusted build digest.
SearchRuntimeVerification verify_search_runtime_bundle(
    const QString& root, const QString& expected_receipt_sha256,
    const std::atomic_bool* cancelled = nullptr);
}  // namespace photo_editor::desktop
