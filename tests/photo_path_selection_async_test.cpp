#include "edit_recipe_state.h"
#include "edit_recipe_v13_adapter.h"
#include "editor_preview_controller.h"
#include "photo_vector_path.h"

#include <QColorSpace>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>

#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void settle(EditorPreviewController& editor) {
  QElapsedTimer timer;
  timer.start();
  while ((editor.processing() || editor.selectionWorkBusy() ||
          editor.professionalToolsBusy()) && timer.elapsed() < 30000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  require(!editor.processing() && !editor.selectionWorkBusy() &&
              !editor.professionalToolsBusy(),
          "Controller work did not settle.");
}

QVariantMap rectangle_path(const QString& subpath_id,
                           double left, double top,
                           double right, double bottom) {
  vector_paths::VectorPathDocument document;
  QString error;
  require(vector_paths::add_subpath(&document, subpath_id, false, &error),
          "Could not add path subpath.");
  const QVector<QPointF> points{{left, top}, {right, top},
                                {right, bottom}, {left, bottom}};
  for (qsizetype index = 0; index < points.size(); ++index) {
    vector_paths::PathAnchor anchor;
    anchor.id = QStringLiteral("%1-anchor-%2").arg(subpath_id).arg(index);
    anchor.position = points.at(index);
    require(vector_paths::append_anchor(&document, subpath_id, anchor, &error),
            "Could not add path anchor.");
  }
  require(vector_paths::set_closed(&document, subpath_id, true, &error),
          "Could not close path subpath.");
  return vector_paths::to_variant(document, &error);
}

QByteArray working_selection_alpha(const EditorPreviewController& editor) {
  const auto set_id = editor.workingSelectionSetId();
  for (const auto& value : editor.maskStack()) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("maskSetId")).toString() == set_id)
      return mask.value(QStringLiteral("segmentationData")).toByteArray();
  }
  return {};
}

int alpha_at(const QByteArray& alpha, const QSize& size, int x, int y) {
  if (x < 0 || y < 0 || x >= size.width() || y >= size.height() ||
      alpha.size() != size.width() * size.height())
    return -1;
  return static_cast<unsigned char>(alpha.at(y * size.width() + x));
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication application(argc, argv);

  struct DrainWorkers {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(60000); }
  } drain_workers;

  try {
    const QSize source_size(320, 180);
    QImage source(source_size, QImage::Format_RGB32);
    source.setColorSpace(QColorSpace(QColorSpace::SRgb));
    source.fill(QColor(QStringLiteral("#526779")));
    const auto source_a = runtime.filePath(QStringLiteral("source-a.png"));
    const auto source_b = runtime.filePath(QStringLiteral("source-b.png"));
    require(source.save(source_a) && source.save(source_b),
            "Could not save source image fixtures.");

    const auto left_path = rectangle_path(QStringLiteral("left-selection"),
                                           0.10, 0.10, 0.32, 0.38);
    const auto right_path = rectangle_path(QStringLiteral("right-selection"),
                                            0.66, 0.60, 0.88, 0.88);

    // Cancelling while rasterization is pending invalidates the worker revision
    // and leaves selection and edit history untouched.
    EditorPreviewController cancelled;
    cancelled.loadAsset(QStringLiteral("path-cancel"), source_a, {}, {}, {}, {}, {});
    settle(cancelled);
    const auto stack_before_cancel = cancelled.maskStack();
    const auto recipe_before_cancel = cancelled.currentAssetBoundAdjustmentPatchJson();
    const auto history_index_before_cancel = cancelled.editHistoryCurrentIndex();
    const auto undo_before_cancel = cancelled.canUndo();
    QSignalSpy cancelled_finished(
        &cancelled, &EditorPreviewController::professionalPathSelectionFinished);
    require(cancelled_finished.isValid(),
            "Could not observe asynchronous path-selection completion.");
    require(cancelled.selectPhotoVectorPath(left_path, QStringLiteral("add")),
            "Could not start a vector-path selection.");
    require(cancelled.selectionWorkBusy(),
            "Path rasterization must be pending before immediate cancellation.");
    cancelled.cancelPhotoVectorPathSelection();
    require(cancelled.maskStack() == stack_before_cancel &&
                cancelled.currentAssetBoundAdjustmentPatchJson() == recipe_before_cancel &&
                cancelled.editHistoryCurrentIndex() == history_index_before_cancel &&
                cancelled.canUndo() == undo_before_cancel && !cancelled.hasWorkingSelection(),
            "Immediate cancellation must not alter selection, recipe or history.");
    settle(cancelled);
    require(cancelled_finished.size() == 1 &&
                !cancelled_finished.first().first().toBool() &&
                cancelled.maskStack() == stack_before_cancel &&
                cancelled.editHistoryCurrentIndex() == history_index_before_cancel,
            "Cancelled path work must finish as rejected without applying stale selection state.");

    // Replacing the source asset while the worker runs rejects its result.
    EditorPreviewController stale_source;
    stale_source.loadAsset(QStringLiteral("path-stale-a"), source_a, {}, {}, {}, {}, {});
    settle(stale_source);
    QSignalSpy stale_finished(
        &stale_source, &EditorPreviewController::professionalPathSelectionFinished);
    require(stale_finished.isValid() &&
                stale_source.selectPhotoVectorPath(left_path, QStringLiteral("add")),
            "Could not start the stale-source selection fixture.");
    stale_source.loadAsset(QStringLiteral("path-stale-b"), source_b, {}, {}, {}, {}, {});
    settle(stale_source);
    require(stale_source.assetId() == QStringLiteral("path-stale-b") &&
                stale_finished.size() == 1 &&
                !stale_finished.first().first().toBool() &&
                !stale_source.hasWorkingSelection(),
            "Changing source assets must report failure and reject the old path result.");

    // An additive path over an existing selection runs a nested worker. The
    // public completion must arrive only after the final worker clears busy.
    EditorPreviewController additive;
    additive.loadAsset(QStringLiteral("path-additive"), source_a, {}, {}, {}, {}, {});
    settle(additive);
    QSignalSpy additive_finished(
        &additive, &EditorPreviewController::professionalPathSelectionFinished);
    require(additive_finished.isValid() &&
                additive.selectPhotoVectorPath(left_path, QStringLiteral("add")),
            "Could not create the initial working path selection.");
    settle(additive);
    require(additive_finished.size() == 1 &&
                additive_finished.first().first().toBool() &&
                additive.hasWorkingSelection(),
            "Initial path selection must establish a working selection.");
    const auto working_id = additive.workingSelectionSetId();
    const auto old_alpha = working_selection_alpha(additive);
    require(!working_id.isEmpty() &&
                alpha_at(old_alpha, source_size, 64, 43) > 200 &&
                alpha_at(old_alpha, source_size, 246, 133) == 0,
            "Initial path selection must cover the first contour only.");

    // Cancel specifically after the outer rasterizer completes and the nested
    // combine worker has entered busy. This catches stale nested results.
    const auto stack_before_nested_cancel = additive.maskStack();
    const auto recipe_before_nested_cancel = additive.currentAssetBoundAdjustmentPatchJson();
    const auto history_index_before_nested_cancel = additive.editHistoryCurrentIndex();
    const auto undo_before_nested_cancel = additive.canUndo();
    additive_finished.clear();
    int busy_true_transitions = 0;
    bool cancelled_on_nested_stage = false;
    const auto nested_cancel_connection = QObject::connect(
        &additive, &EditorPreviewController::maskStateChanged, &additive, [&] {
          if (!additive.selectionWorkBusy()) return;
          ++busy_true_transitions;
          if (busy_true_transitions == 2) {
            cancelled_on_nested_stage = true;
            additive.cancelPhotoVectorPathSelection();
          }
        });
    require(additive.selectPhotoVectorPath(right_path, QStringLiteral("add")) &&
                additive.selectionWorkBusy() && busy_true_transitions == 1,
            "Second path must begin in its outer rasterization busy stage.");
    settle(additive);
    QObject::disconnect(nested_cancel_connection);
    require(cancelled_on_nested_stage && busy_true_transitions == 2 &&
                additive_finished.size() == 1 &&
                !additive_finished.first().first().toBool() &&
                additive.maskStack() == stack_before_nested_cancel &&
                additive.currentAssetBoundAdjustmentPatchJson() == recipe_before_nested_cancel &&
                additive.editHistoryCurrentIndex() == history_index_before_nested_cancel &&
                additive.canUndo() == undo_before_nested_cancel &&
                additive.workingSelectionSetId() == working_id,
            "Nested-worker cancellation must signal failure without changing selection, recipe or history.");

    // A subsequent add remains admissible and reports only after its nested
    // composition worker has cleared busy.
    additive_finished.clear();
    bool busy_at_final_signal = true;
    QObject::connect(&additive,
                     &EditorPreviewController::professionalPathSelectionFinished,
                     &additive, [&] {
      busy_at_final_signal = additive.selectionWorkBusy() ||
                             additive.professionalToolsBusy();
    });
    require(additive.selectPhotoVectorPath(right_path, QStringLiteral("add")),
            "Could not start additive selection over an existing working selection.");
    require(additive.selectionWorkBusy(),
            "The additive outer rasterization must enter the busy state.");
    settle(additive);
    const auto combined_alpha = working_selection_alpha(additive);
    require(additive_finished.size() == 1 &&
                additive_finished.first().first().toBool() &&
                !busy_at_final_signal && !additive.selectionWorkBusy() &&
                !additive.professionalToolsBusy() && additive.hasWorkingSelection() &&
                additive.workingSelectionSetId() == working_id &&
                alpha_at(combined_alpha, source_size, 64, 43) > 200 &&
                alpha_at(combined_alpha, source_size, 246, 133) > 200,
            "Additive completion must preserve both contours and signal after nested work clears busy.");

    std::cout << "Async path selection: raster and nested-worker cancellation, stale-source rejection, additive completion passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Async path selection test failed: " << error.what() << '\n';
    return 1;
  }
}
