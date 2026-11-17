#include "editor_preview_controller.h"
#include "canonical_composite_mask_alpha.h"

#include <QColor>
#include <QEventLoop>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

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
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  } while ((editor.processing() || editor.photoPaintBusy() || editor.selectionWorkBusy() ||
            editor.brushStrokeActive()) &&
           timer.elapsed() < 20000);
  require(!editor.processing() && !editor.photoPaintBusy() && !editor.selectionWorkBusy(),
          "Selection work must settle before asserting source order");
  if (!editor.photoPaintError().isEmpty()) {
    throw std::runtime_error(editor.photoPaintError().toStdString());
  }
}

QVariantList set_sources(const EditorPreviewController& editor, const QString& set_id) {
  QVariantList result;
  for (const auto& value : editor.maskStack()) {
    if (value.toMap().value(QStringLiteral("maskSetId")).toString() == set_id) {
      result.append(value);
    }
  }
  return result;
}

QStringList source_ids(const QVariantList& masks) {
  QStringList result;
  for (const auto& value : masks) result.append(value.toMap().value(QStringLiteral("id")).toString());
  return result;
}

QByteArray rendered_selection(EditorPreviewController& editor, const QImage& source,
                               const QString& set_id) {
  const auto result = evaluate_canonical_composite_mask_alpha(source, set_sources(editor, set_id),
                                                               1.0, {1.0, 1.0, 1.0});
  require(result.valid, "The canonical mask renderer must evaluate selection source order");
  return result.alpha_mask;
}

int alpha_at(const QByteArray& alpha, const QSize& size, double x, double y) {
  const auto px = qBound(0, qRound(x * static_cast<double>(size.width() - 1)), size.width() - 1);
  const auto py = qBound(0, qRound(y * static_cast<double>(size.height() - 1)), size.height() - 1);
  return static_cast<unsigned char>(alpha.at(py * size.width() + px));
}

void add_rectangle(EditorPreviewController& editor, const QString& operation,
                   double center_x, double center_y, double radius_x, double radius_y) {
  editor.addMask(QStringLiteral("rectangle"));
  require(editor.activeMaskKind() == QStringLiteral("rectangle"),
          "The fixture should create editable rectangle sources");
  editor.setActiveMaskOperation(operation);
  editor.setActiveMaskFeather(0.0);
  editor.setActiveMaskCenterX(center_x);
  editor.setActiveMaskCenterY(center_y);
  editor.setActiveMaskRadiusX(radius_x);
  editor.setActiveMaskRadiusY(radius_y);
  settle(editor);
}

void run_case(QTemporaryDir& runtime, const QString& tool) {
  constexpr int kExtent = 128;
  QImage source(kExtent, kExtent, QImage::Format_RGB32);
  source.fill(QColor("#8292a3"));
  const auto path = runtime.filePath(tool + QStringLiteral("-source.png"));
  require(source.save(path), "The selection fixture source image must be writable");

  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("source-order-") + tool, path, {}, {}, {}, {}, {});
  settle(editor);

  add_rectangle(editor, QStringLiteral("add"), 0.5, 0.5, 0.44, 0.44); // A
  add_rectangle(editor, QStringLiteral("subtract"), 0.5, 0.5, 0.19, 0.19); // B
  add_rectangle(editor, QStringLiteral("add"), 0.24, 0.24, 0.055, 0.055); // C

  const auto first_three = editor.maskStack();
  require(first_three.size() == 3, "The baseline selection should contain A, B, and C");
  QString set_id;
  QStringList original_ids;
  for (const auto& value : first_three) {
    const auto mask = value.toMap();
    if (set_id.isEmpty()) set_id = mask.value(QStringLiteral("maskSetId")).toString();
    require(!set_id.isEmpty() && mask.value(QStringLiteral("maskSetId")).toString() == set_id,
            "A, B, and C must be ordered sources of one selection set");
    original_ids.append(mask.value(QStringLiteral("id")).toString());
  }
  require(!set_id.isEmpty(), "The baseline selection set must have a stable identity");
  require(first_three.at(0).toMap().value(QStringLiteral("operation")).toString() ==
              QStringLiteral("add") &&
              first_three.at(1).toMap().value(QStringLiteral("operation")).toString() ==
                  QStringLiteral("subtract") &&
              first_three.at(2).toMap().value(QStringLiteral("operation")).toString() ==
                  QStringLiteral("add"),
          "The fixture must use the intended A Add, B Subtract, C Add algebra");

  const auto before = rendered_selection(editor, source, set_id);
  require(alpha_at(before, source.size(), 0.5, 0.5) < 8,
          "A minus B plus C must leave the central B region unselected");
  require(alpha_at(before, source.size(), 0.24, 0.24) > 247,
          "The independent C add region must remain selected");
  require(alpha_at(before, source.size(), 0.80, 0.50) > 247,
          "A region outside B must remain selected");
  require(alpha_at(before, source.size(), 0.04, 0.04) < 8,
          "Pixels outside A must remain unselected");

  // Select A while B and C remain later in the same set. The newly created
  // source covers the center of B, so incorrect insertion after A would make
  // B subtract it again and leave the center clear.
  editor.setActiveMaskIndex(0);
  editor.beginCoalescedEdit();
  if (tool == QStringLiteral("polygon")) {
    editor.startPolygonSelection(false);
    editor.appendActivePolygonPoint(0.42, 0.42);
    editor.appendActivePolygonPoint(0.58, 0.42);
    editor.appendActivePolygonPoint(0.58, 0.58);
    editor.appendActivePolygonPoint(0.42, 0.58);
  } else if (tool == QStringLiteral("lasso")) {
    editor.startLassoSelection(false);
    editor.appendActivePolygonPoint(0.42, 0.42);
    editor.appendActivePolygonPoint(0.58, 0.42);
    editor.appendActivePolygonPoint(0.58, 0.58);
    editor.appendActivePolygonPoint(0.42, 0.58);
    require(editor.closeActiveLassoPath(), "The lasso refinement path must close");
  } else if (tool == QStringLiteral("edge")) {
    editor.startSelectionEdgeBrush(false);
    require(editor.activeMaskKind() == QStringLiteral("brush"),
            "Edge Refine should create an additive brush source");
    editor.setActiveMaskBrushAutoMask(false);
    editor.setActiveMaskBrushProtectEdges(false);
    editor.setActiveMaskBrushSize(0.20);
    editor.beginActiveBrushStroke();
    editor.addBrushPoint(0.5, 0.5);
    editor.endActiveBrushStroke();
  } else {
    throw std::runtime_error("Unknown source-order selection tool");
  }
  settle(editor);
  editor.endCoalescedEdit();
  settle(editor);

  const auto after_sources = set_sources(editor, set_id);
  const auto after_ids = source_ids(after_sources);
  require(after_ids.size() == 4,
          "Each selection tool should append one new source to the existing set");
  for (int index = 0; index < original_ids.size(); ++index) {
    require(after_ids.at(index) == original_ids.at(index),
            "Existing selection source identities and their order must remain unchanged");
  }
  const auto new_id = after_ids.constLast();
  require(!new_id.isEmpty() && new_id != original_ids.constLast(),
          "The appended selection source must have its own stable identity");

  const auto after = rendered_selection(editor, source, set_id);
  require(alpha_at(after, source.size(), 0.5, 0.5) > 247,
          "The new Add source must follow B and restore coverage in B's center");
  require(alpha_at(after, source.size(), 0.24, 0.24) > 247,
          "Appending a new source must preserve C's existing coverage");
  require(alpha_at(after, source.size(), 0.80, 0.50) > 247 &&
              alpha_at(after, source.size(), 0.04, 0.04) < 8,
          "The new source must preserve unaffected pixels outside its coverage");
  require(editor.canUndo(), "Creating the source and its geometry must be undoable");

  editor.undo();
  settle(editor);
  require(source_ids(set_sources(editor, set_id)) == original_ids,
          "One undo must remove the newly authored source and restore the original source order");
  require(rendered_selection(editor, source, set_id) == before,
          "Undo must restore the exact pre-edit selection coverage");
  require(editor.canRedo(), "Undoing source creation must leave a redo step");

  editor.redo();
  settle(editor);
  const auto redone_sources = set_sources(editor, set_id);
  require(source_ids(redone_sources) == after_ids,
          "Redo must restore the appended source with the same identity and order");
  require(rendered_selection(editor, source, set_id) == after,
          "Redo must restore the exact rendered selection coverage");
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  try {
    for (const auto& tool : {QStringLiteral("lasso"), QStringLiteral("polygon"), QStringLiteral("edge")}) {
      try {
        run_case(runtime, tool);
      } catch (const std::exception& error) {
        std::cerr << tool.toStdString() << " case: " << error.what() << '\n';
        return 1;
      }
      std::cout << tool.toStdString() << " source-order algebra, identity, undo, and redo passed\n";
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
