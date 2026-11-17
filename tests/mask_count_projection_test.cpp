#include "edit_recipe_mask_schema.h"
#include "editor_preview_controller.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace photo_editor::desktop {
struct MaskInteractionPerformanceTestAccess final {
  static void seed(EditorPreviewController& controller, int sets) {
    QVariantList masks;
    for (int set = 0; set < sets; ++set) {
      for (int component = 0; component < 3; ++component) {
        auto mask = default_mask_definition(QStringLiteral("linear"));
        mask.insert(QStringLiteral("id"), QStringLiteral("mask-%1-%2").arg(set).arg(component));
        mask.insert(QStringLiteral("maskSetId"), QStringLiteral("set-%1").arg(set));
        mask.insert(QStringLiteral("maskSetName"), QStringLiteral("Selection %1").arg(set));
        masks.append(mask);
      }
    }
    auto retouch = default_mask_definition(QStringLiteral("brush"));
    retouch.insert(QStringLiteral("id"), QStringLiteral("retouch"));
    retouch.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
    retouch.insert(QStringLiteral("retouchMode"), QStringLiteral("clone"));
    masks.append(retouch);
    controller.asset_id_ = QStringLiteral("mask-count-fixture");
    controller.file_path_ = QStringLiteral("/isolated-mask-count/source.png");
    controller.mask_session_.replaceStack(masks, 0);
    controller.invalidateMaskDerivedCaches();
  }
  static int misses(const EditorPreviewController& controller) {
    return controller.thumbnail_cache_session_.entryMissCount();
  }
  static int rebuilds(const EditorPreviewController& controller) {
    return controller.mask_group_cache_rebuild_count_;
  }
};
}  // namespace photo_editor::desktop

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QTemporaryDir storage;
  if (!storage.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", storage.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", storage.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/mask_count_fixture");
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storage.filePath("settings"));
  QApplication app(argc, argv);
  const bool verify = app.arguments().contains(QStringLiteral("--verify"));
  using namespace photo_editor::desktop;
  try {
    QJsonArray cases;
    for (const int count : {8, 64, 256}) {
      EditorPreviewController controller;
      MaskInteractionPerformanceTestAccess::seed(controller, count);
      if (controller.maskSetCount() != count || controller.retouchCount() != 1)
        throw std::runtime_error("cold mask/retouch count mismatch");
      const auto rebuilds = MaskInteractionPerformanceTestAccess::rebuilds(controller);
      const auto misses = MaskInteractionPerformanceTestAccess::misses(controller);
      QVector<qint64> samples;
      for (int trial = 0; trial < 15; ++trial) {
        QElapsedTimer timer;
        timer.start();
        for (int consumer = 0; consumer < 6; ++consumer) {
          if (controller.maskSetCount() != count)
            throw std::runtime_error("warm mask count mismatch");
        }
        samples.append(timer.nsecsElapsed());
      }
      std::sort(samples.begin(), samples.end());
      const auto extra_misses = MaskInteractionPerformanceTestAccess::misses(controller) - misses;
      if (MaskInteractionPerformanceTestAccess::rebuilds(controller) != rebuilds)
        throw std::runtime_error("unchanged count rebuilt grouped masks");
      if (verify && extra_misses != 0)
        throw std::runtime_error("count-only reads performed thumbnail lookups");
      const auto count_ns = samples.at(samples.size() / 2);

      samples.clear();
      for (int trial = 0; trial < 5; ++trial) {
        QElapsedTimer timer;
        timer.start();
        const auto presented = controller.maskSets();
        samples.append(timer.nsecsElapsed());
        if (presented.size() != count)
          throw std::runtime_error("presented mask group count mismatch");
      }
      std::sort(samples.begin(), samples.end());
      const auto presentation_ns = samples.at(samples.size() / 2);
      const auto remaining = controller.maskSets();
      if (remaining.constFirst().toMap().value(QStringLiteral("sources")).toList().size() != 3)
        throw std::runtime_error("group source count changed");
      // Replacing the stack invalidates the count, while presentation still
      // performs real thumbnail lookups after the count-only fast path.
      if (MaskInteractionPerformanceTestAccess::misses(controller) <= misses)
        throw std::runtime_error("mask presentation stopped looking up thumbnails");
      MaskInteractionPerformanceTestAccess::seed(controller, count + 1);
      if (controller.retouchCount() != 1 || controller.maskSetCount() != count + 1)
        throw std::runtime_error("count did not refresh after stack mutation");
      cases.append(QJsonObject{{"sets", count}, {"count_consumers", 6},
          {"count_median_ns", count_ns},
          {"extra_thumbnail_misses", extra_misses},
          {"presentation_median_ns", presentation_ns}});
    }
    EditorPreviewController empty;
    MaskInteractionPerformanceTestAccess::seed(empty, 0);
    if (empty.retouchCount() != 1 || empty.maskSetCount() != 0 || !empty.maskSets().isEmpty())
      throw std::runtime_error("retouch-first or empty selection grouping mismatch");
    std::cout << QJsonDocument(QJsonObject{{"cases", cases}, {"verified", verify}})
                     .toJson(QJsonDocument::Compact).constData() << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
