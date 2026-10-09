#include "photo_cache_budget.h"
#include "decoded_image_cache.h"
#include "editor_preview_controller.h"
#include "timeline_document_controller.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <atomic>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static bool insertMask(const QString& key, const QByteArray& bytes, const QSize& size) {
    return EditorPreviewController::insertMaskTileForTesting(key, bytes, size);
  }
  static bool findMask(const QString& key, QByteArray* bytes, QSize* size) {
    return EditorPreviewController::findMaskTileForTesting(key, bytes, size);
  }
};
}
using namespace photo_editor::desktop;
namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
bool accounted() {
  const auto snapshot = photo_cache_budget_snapshot();
  const auto owners = snapshot.value("owners").toMap();
  qint64 sum = 0;
  for (const auto& owner : owners) {
    const auto stats = owner.toMap();
    const auto bytes = stats.value("bytes").toLongLong();
    if (bytes < 0 || stats.value("entries").toLongLong() < 0) return false;
    sum += bytes;
  }
  return sum == snapshot.value("bytes").toLongLong() &&
      sum <= snapshot.value("effectiveLimitBytes").toLongLong();
}
qint64 bytes() { return photo_cache_budget_snapshot().value("bytes").toLongLong(); }
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("Temporary storage unavailable");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temporary.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", temporary.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/cache_fixture");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
  QCoreApplication::setOrganizationName("LooksyCacheBudgetTest");
  QCoreApplication::setApplicationName("IsolatedCacheBudget");
  constexpr quint64 mib = 1024 * 1024;
  if (automatic_photo_cache_budget_bytes(0) != 256 * mib ||
      automatic_photo_cache_budget_bytes(4ULL * 1024 * mib) != 512 * mib ||
      automatic_photo_cache_budget_bytes(8ULL * 1024 * mib) != 1024 * mib ||
      automatic_photo_cache_budget_bytes(64ULL * 1024 * mib) != 2048 * mib)
    return fail("Hardware budget tiers incorrect");
  clear_photo_caches();
  set_photo_cache_pressure("normal");
  set_photo_cache_budget_bytes(100);
  {
    BudgetedPhotoCache<QByteArray> a(PhotoCacheOwner::Rendered, 70);
    BudgetedPhotoCache<QByteArray> b(PhotoCacheOwner::MaskTiles, 100);
    {
      QMutexLocker lock(&photo_cache_mutex());
      a.insertValue("a", QByteArray(40, 'a'), 40);
      b.insertValue("b", QByteArray(40, 'b'), 40);
      a.touch("a");
      b.insertValue("c", QByteArray(40, 'c'), 40);
      if (!a.entries.contains("a") || b.entries.contains("b") || !b.entries.contains("c"))
        return fail("Shared eviction ignored cross-cache access order");
      a.insertValue("d", QByteArray(40, 'd'), 40);
      if (a.entries.contains("a") || a.total_bytes != 40 || b.total_bytes != 40)
        return fail("Local cap failed or evicted another owner unnecessarily");
      a.insertValue("d", QByteArray(20, 'e'), 20);
      a.insertValue("oversized", QByteArray(71, 'x'), 71);
      if (a.entries.size() != 1 || a.total_bytes != 20) return fail("Replacement or local rejection accounting failed");
    }
    if (bytes() != 60 || !accounted()) return fail("Owner accounting mismatch");
    set_photo_cache_budget_bytes(21);
    if (bytes() != 20 || !accounted()) return fail("Shared limit shrink failed");
    {
      QMutexLocker lock(&photo_cache_mutex());
      b.insertValue("shared-oversized", QByteArray(22, 'x'), 22);
      if (!b.entries.isEmpty() || a.entries.value("d") != QByteArray(20, 'e'))
        return fail("Oversized shared insertion displaced valid entries");
    }
  }
  if (bytes() != 0 || !accounted()) return fail("Cache destruction leaked registrations");

  set_photo_cache_budget_bytes(65536);
  set_photo_cache_pressure("warning");
  if (photo_cache_budget_snapshot().value("effectiveLimitBytes").toLongLong() != 42598)
    return fail("Warning ceiling rounding incorrect");
  set_photo_cache_pressure("critical");
  if (photo_cache_budget_snapshot().value("effectiveLimitBytes").toLongLong() != 22937)
    return fail("Critical ceiling rounding incorrect");
  set_photo_cache_pressure("normal");
  if (photo_cache_budget_snapshot().value("effectiveLimitBytes").toLongLong() != 65536)
    return fail("Normal pressure did not restore exact ceiling");

  // Real decoded and editor mask caches must participate in the same LRU.
  QImage image(64, 64, QImage::Format_RGBA8888);
  image.fill(QColor(12, 34, 56, 78));
  const QByteArray matte(16384, 'm');
  set_photo_cache_budget_bytes(32768);
  insert_decoded_image_cache_entry("decoded", image);
  if (!EditorPreviewControllerTestAccess::insertMask("mask", matte, QSize(128, 128)))
    return fail("Production mask cache insertion failed");
  QImage held_image;
  QByteArray held_matte;
  QSize held_size;
  if (!EditorPreviewControllerTestAccess::findMask("mask", &held_matte, &held_size) ||
      !find_decoded_image_cache_entry("decoded", &held_image)) return fail("Production cache lookup failed");
  if (!EditorPreviewControllerTestAccess::insertMask("mask-new", matte, QSize(128, 128)) ||
      EditorPreviewControllerTestAccess::findMask("mask", &held_matte, &held_size) ||
      !find_decoded_image_cache_entry("decoded", &held_image) || !accounted())
    return fail("Production caches do not share LRU ownership");
  {
    TimelineDocumentController controller;
    controller.setCachePressureLevelForTesting("critical");
    if (bytes() != 0 || !accounted() ||
        controller.cacheBudgetState().value("sharedPhotoCacheBudget").toMap().value("pressure").toString() != "critical")
      return fail("Timeline pressure did not release photo cache ownership");
    if (held_image != image || held_matte != matte || held_size != QSize(128, 128))
      return fail("Eviction invalidated references retained by consumers");
    controller.setCachePressureLevelForTesting("normal");
  }
  // Working images use shared ownership; pressure releases only the cache copy.
  auto retained = std::make_shared<QByteArray>(512, 'w');
  {
    BudgetedPhotoCache<std::shared_ptr<QByteArray>> working(PhotoCacheOwner::Linear, 1024);
    {
      QMutexLocker lock(&photo_cache_mutex());
      working.insertValue("working", retained, 512);
    }
    set_photo_cache_pressure("critical");
    if (retained.use_count() != 1 || *retained != QByteArray(512, 'w') || bytes() != 0)
      return fail("Shared working ownership did not survive pressure eviction");
  }
  set_photo_cache_pressure("normal");
  set_photo_cache_budget_bytes(4096);
  std::atomic<bool> valid{true};
  {
    BudgetedPhotoCache<QByteArray> a(PhotoCacheOwner::Adjusted, 3072);
    BudgetedPhotoCache<QByteArray> b(PhotoCacheOwner::ForegroundMaps, 3072);
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
      workers.emplace_back([&, worker] {
        auto& cache = worker % 2 == 0 ? a : b;
        for (int iteration = 0; iteration < 1500; ++iteration) {
          const auto key = QString::number(worker) + ':' + QString::number(iteration % 12);
          {
            QMutexLocker lock(&photo_cache_mutex());
            cache.insertValue(key, QByteArray(256, 't'), 256);
            if (cache.entries.contains(key)) cache.touch(key);
            else cache.noteMiss();
            if (cache.total_bytes > cache.budget_bytes) valid = false;
          }
          if (iteration % 50 == 0) set_photo_cache_pressure(iteration % 100 == 0 ? "warning" : "normal");
          if (!accounted()) valid = false;
        }
      });
    }
    for (auto& worker : workers) worker.join();
  }
  if (!valid || bytes() != 0 || !accounted()) return fail("Concurrent cache accounting or destruction failed");
  set_photo_cache_budget_bytes(0);
  insert_decoded_image_cache_entry("disabled", image);
  if (bytes() != 0) return fail("Zero budget retained an entry");
  std::cout << "Shared cache tiers, LRU, caps, accounting, pressure, retained references, and concurrency passed\n";
  return 0;
}
