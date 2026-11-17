#include "canonical_composite_mask_alpha.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "photo_render_utils.h"

#include <QColorSpace>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static QString operation(const EditorPreviewController& editor) {
    return editor.last_edit_intent_.operation;
  }
  static QString intentReason(const EditorPreviewController& editor) {
    return editor.last_edit_intent_.render_reason;
  }
  static QString renderReason(const EditorPreviewController& editor) {
    return editor.render_reason_;
  }
  static QString dirtyDomain(const EditorPreviewController& editor) {
    return editor.last_render_graph_resolution_.dirty_domain;
  }
  static bool schedulesFullResolution(const EditorPreviewController& editor) {
    return editor.last_edit_intent_.schedule_full_resolution;
  }
  static bool idle(const EditorPreviewController& editor) {
    return !editor.processing_ && !editor.render_scheduler_.interactiveActive() &&
        !editor.render_scheduler_.fullActive() && !editor.interactive_render_watcher_.isRunning() &&
        !editor.full_render_watcher_.isRunning() && !editor.interactive_request_pending_ &&
        !editor.full_request_pending_ && !editor.interactive_render_result_pending_ &&
        !editor.edit_transaction_interactive_preview_timer_.isActive() &&
        !editor.photo_layer_adjustment_render_timer_.isActive();
  }
};

}  // namespace photo_editor::desktop

namespace {

void require(bool condition, const QString& message) {
  if (!condition) throw std::runtime_error(message.toStdString());
}

class ScopedEnvironment final {
 public:
  ScopedEnvironment(const char* name, const QByteArray& value)
      : name_(name), existed_(qEnvironmentVariableIsSet(name)), previous_(qgetenv(name)) {
    qputenv(name_, value);
  }
  ~ScopedEnvironment() {
    if (existed_) qputenv(name_, previous_);
    else qunsetenv(name_);
  }

 private:
  QByteArray name_;
  bool existed_ = false;
  QByteArray previous_;
};

class TestImageSink final : public EditorPreviewImageSink {
 public:
  void setImage(const QString& key, const QImage& image) override {
    if (image.isNull()) images_.remove(key);
    else images_.insert(key, image.copy());
  }
  void clearImage(const QString& key) override { images_.remove(key); }
  QImage image(const QString& key) const override { return images_.value(key); }

 private:
  QHash<QString, QImage> images_;
};

EditRecipeState recipe(const EditorPreviewController& editor) {
  const auto parsed = parse_adjustment_patch_json(editor.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok(), QStringLiteral("The current desktop recipe must parse."));
  return *parsed.state;
}

int layer_index(const EditRecipeState& state, const QString& id) {
  for (int index = 0; index < state.layers.size(); ++index) {
    if (state.layers.at(index).toMap().value(QStringLiteral("id")).toString() == id) return index;
  }
  return -1;
}

bool wait_for_idle(EditorPreviewController& editor, TestImageSink& sink, const QString& key) {
  QElapsedTimer timer;
  timer.start();
  qint64 quiet_since = -1;
  while (timer.elapsed() < 30000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    const bool idle = EditorPreviewControllerTestAccess::idle(editor) &&
        !editor.photoPaintBusy() && !editor.selectionWorkBusy() &&
        !editor.professionalToolsBusy();
    if (idle && !sink.image(key).isNull()) {
      if (quiet_since < 0) quiet_since = timer.elapsed();
      if (timer.elapsed() - quiet_since >= 40) return true;
    } else {
      quiet_since = -1;
    }
    QThread::msleep(2);
  }
  return false;
}

QImage render_expected(const QImage& source, const EditRecipeState& state) {
  return render_output_image_with_backend(
      source, state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
}

QImage normalized_pixels(const QImage& image) {
  if (image.isNull()) return {};
  auto result = image;
  if (result.colorSpace().isValid() && result.colorSpace() != QColorSpace(QColorSpace::SRgb)) {
    result = result.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
  }
  return result.convertToFormat(QImage::Format_ARGB32);
}

bool within_channel_tolerance(const QImage& actual_image, const QImage& expected_image,
                              int tolerance, QString* detail = nullptr) {
  const auto actual = normalized_pixels(actual_image);
  const auto expected = normalized_pixels(expected_image);
  if (actual.isNull() || expected.isNull() || actual.size() != expected.size()) {
    if (detail) *detail = QStringLiteral("image size differs: actual=%1x%2 expected=%3x%4")
        .arg(actual.width()).arg(actual.height()).arg(expected.width()).arg(expected.height());
    return false;
  }
  for (int y = 0; y < actual.height(); ++y) {
    for (int x = 0; x < actual.width(); ++x) {
      const auto a = actual.pixelColor(x, y);
      const auto e = expected.pixelColor(x, y);
      const std::array<int, 4> differences{
          std::abs(a.red() - e.red()), std::abs(a.green() - e.green()),
          std::abs(a.blue() - e.blue()), std::abs(a.alpha() - e.alpha())};
      const auto maximum = *std::max_element(differences.cbegin(), differences.cend());
      if (maximum > tolerance) {
        if (detail) *detail = QStringLiteral("pixel (%1,%2) differs by %3 channels: actual=%4,%5,%6,%7 expected=%8,%9,%10,%11")
            .arg(x).arg(y).arg(maximum).arg(a.red()).arg(a.green()).arg(a.blue()).arg(a.alpha())
            .arg(e.red()).arg(e.green()).arg(e.blue()).arg(e.alpha());
        return false;
      }
    }
  }
  return true;
}

bool exact_pixels(const QImage& left, const QImage& right) {
  const auto a = normalized_pixels(left);
  const auto b = normalized_pixels(right);
  if (a.isNull() || b.isNull() || a.size() != b.size()) return false;
  for (int y = 0; y < a.height(); ++y) {
    if (std::memcmp(a.constScanLine(y), b.constScanLine(y),
                    static_cast<std::size_t>(a.width()) * sizeof(QRgb)) != 0) return false;
  }
  return true;
}

bool same_region(const QImage& left_image, const QImage& right_image,
                 const QRect& region, int tolerance = 0) {
  const auto left = normalized_pixels(left_image);
  const auto right = normalized_pixels(right_image);
  if (left.isNull() || right.isNull() || left.size() != right.size() ||
      !QRect(QPoint(), left.size()).contains(region)) return false;
  for (int y = region.top(); y <= region.bottom(); ++y) {
    for (int x = region.left(); x <= region.right(); ++x) {
      const auto a = left.pixelColor(x, y);
      const auto b = right.pixelColor(x, y);
      if (std::abs(a.red() - b.red()) > tolerance ||
          std::abs(a.green() - b.green()) > tolerance ||
          std::abs(a.blue() - b.blue()) > tolerance ||
          std::abs(a.alpha() - b.alpha()) > tolerance) return false;
    }
  }
  return true;
}

void require_blue_sibling_unchanged(const QImage& published, const QImage& baseline) {
  require(same_region(published, baseline, QRect(345, 140, 80, 90), 1),
          QStringLiteral("The unrelated blue sibling layer must remain visible and unchanged."));
}

struct PublicationMetadata final {
  QString operation;
  QString intent_reason;
  QString render_reason;
  QString dirty_domain;
  bool schedules_full = false;
};

PublicationMetadata publication_metadata(const EditorPreviewController& editor) {
  return {EditorPreviewControllerTestAccess::operation(editor),
          EditorPreviewControllerTestAccess::intentReason(editor),
          EditorPreviewControllerTestAccess::renderReason(editor),
          EditorPreviewControllerTestAccess::dirtyDomain(editor),
          EditorPreviewControllerTestAccess::schedulesFullResolution(editor)};
}

QString describe(const PublicationMetadata& metadata) {
  return QStringLiteral("operation=%1 intentReason=%2 renderReason=%3 dirtyDomain=%4 scheduleFull=%5")
      .arg(metadata.operation, metadata.intent_reason, metadata.render_reason,
           metadata.dirty_domain)
      .arg(metadata.schedules_full ? QStringLiteral("true") : QStringLiteral("false"));
}

void require_published_recipe(EditorPreviewController& editor, TestImageSink& sink,
                              const QImage& source, const QImage& baseline,
                              const QString& label) {
  require(wait_for_idle(editor, sink, QStringLiteral("edited")),
          QStringLiteral("%1 preview work must settle.").arg(label));
  const auto published = sink.image(QStringLiteral("edited"));
  const auto expected = render_expected(source, recipe(editor));
  QString detail;
  require(within_channel_tolerance(published, expected, 2, &detail),
          QStringLiteral("%1 published frame must match its canonical SDR render within two channel levels: %2")
              .arg(label, detail));
  require_blue_sibling_unchanged(published, baseline);
}

void verify_commit_undo_redo(EditorPreviewController& editor, TestImageSink& sink,
                             const QImage& source, const QImage& blue_sibling_baseline,
                             const QString& operation, const std::function<bool()>& edit,
                             const QString& label) {
  const auto before_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto before_pixels = sink.image(QStringLiteral("edited"));
  require(!before_pixels.isNull(), QStringLiteral("%1 needs a settled before frame.").arg(label));
  require(edit(), QStringLiteral("%1 must be accepted.").arg(label));
  require_published_recipe(editor, sink, source, blue_sibling_baseline, label);
  const auto after_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto after_pixels = sink.image(QStringLiteral("edited"));
  require(after_recipe != before_recipe, QStringLiteral("%1 must change the document recipe.").arg(label));
  require(!exact_pixels(before_pixels, after_pixels) &&
              !same_region(before_pixels, after_pixels, QRect(60, 50, 185, 275), 0),
          QStringLiteral("%1 must change pixels on the masked red sibling.").arg(label));

  const auto metadata = publication_metadata(editor);
  if (metadata.operation != operation || !metadata.schedules_full ||
      metadata.intent_reason == QStringLiteral("mask_preview_change") ||
      metadata.render_reason == QStringLiteral("mask_preview_change")) {
    throw std::runtime_error(QStringLiteral("%1 must publish through a committed coverage render (%2)")
                                 .arg(label, describe(metadata)).toStdString());
  }

  editor.undo();
  require(wait_for_idle(editor, sink, QStringLiteral("edited")),
          QStringLiteral("Undo for %1 must settle.").arg(label));
  require(editor.currentAssetBoundAdjustmentPatchJson() == before_recipe &&
              exact_pixels(sink.image(QStringLiteral("edited")), before_pixels),
          QStringLiteral("Undo for %1 must restore exact recipe and published pixels.").arg(label));
  require_blue_sibling_unchanged(sink.image(QStringLiteral("edited")), blue_sibling_baseline);

  editor.redo();
  require(wait_for_idle(editor, sink, QStringLiteral("edited")),
          QStringLiteral("Redo for %1 must settle.").arg(label));
  require(editor.currentAssetBoundAdjustmentPatchJson() == after_recipe &&
              exact_pixels(sink.image(QStringLiteral("edited")), after_pixels),
          QStringLiteral("Redo for %1 must restore exact recipe and published pixels.").arg(label));
  require_published_recipe(editor, sink, source, blue_sibling_baseline,
                           QStringLiteral("Redo: %1").arg(label));
  std::cout << "published mask operation: " << describe(metadata).toStdString() << '\n';
}

void test_layer_mask_preview_publication() {
  QTemporaryDir runtime;
  require(runtime.isValid(), QStringLiteral("Create an isolated runtime directory."));
  ScopedEnvironment app_data("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  ScopedEnvironment persistence("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));

  constexpr int width = 512;
  constexpr int height = 384;
  QImage source(width, height, QImage::Format_ARGB32);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  source.fill(QColor(QStringLiteral("#747f88")));
  const auto source_path = runtime.filePath(QStringLiteral("mask-publication-source.png"));
  require(source.save(source_path, "PNG"), QStringLiteral("Write the synthetic source photo."));

  TestImageSink sink;
  EditorPreviewController editor;
  editor.setImageProvider(&sink);
  editor.setPreviewTargetEdge(512);
  editor.loadAsset(QStringLiteral("layer-mask-preview-publication"), source_path,
                   {}, {}, {}, {}, {});
  require(wait_for_idle(editor, sink, QStringLiteral("edited")),
          QStringLiteral("The initial source preview must settle."));

  require(editor.addPhotoShape(QStringLiteral("rectangle"), 60, 50, 245, 325,
                               width, height, QStringLiteral("#e33f35"), QString(), 0.0),
          QStringLiteral("Create the red mask-owner sibling."));
  require(editor.addPhotoShape(QStringLiteral("ellipse"), 300, 80, 490, 310,
                               width, height, QStringLiteral("#285fe0"), QString(), 0.0),
          QStringLiteral("Create the unrelated blue sibling."));
  require(wait_for_idle(editor, sink, QStringLiteral("edited")),
          QStringLiteral("The two sibling layers must finish rendering."));

  auto current = recipe(editor);
  QString red_layer_id;
  QString blue_layer_id;
  for (const auto& value : current.layers) {
    const auto layer = value.toMap();
    const auto type = layer.value(QStringLiteral("shapeDefinition")).toMap()
                          .value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("rectangle")) red_layer_id = layer.value(QStringLiteral("id")).toString();
    if (type == QStringLiteral("ellipse")) blue_layer_id = layer.value(QStringLiteral("id")).toString();
  }
  require(!red_layer_id.isEmpty() && !blue_layer_id.isEmpty() && red_layer_id != blue_layer_id,
          QStringLiteral("The fixture must contain two separately identified photo siblings."));

  editor.addMask(QStringLiteral("ellipse"));
  editor.setActiveMaskFeather(0.0);
  editor.setActiveMaskCenterX(0.28);
  editor.setActiveMaskCenterY(0.50);
  editor.setActiveMaskRadiusX(0.08);
  editor.setActiveMaskRadiusY(0.40);
  require(wait_for_idle(editor, sink, QStringLiteral("edited")),
          QStringLiteral("The initial selection mask must settle."));
  const auto mask_set_id = editor.workingSelectionSetId();
  auto red_index = layer_index(recipe(editor), red_layer_id);
  require(!mask_set_id.isEmpty() && red_index >= 0 &&
              editor.linkActiveSelectionToPhotoLayer(red_index),
          QStringLiteral("Link the working selection explicitly to its red layer owner."));
  require(wait_for_idle(editor, sink, QStringLiteral("edited")),
          QStringLiteral("The explicit layer-mask link must settle."));
  current = recipe(editor);
  red_index = layer_index(current, red_layer_id);
  const auto blue_index = layer_index(current, blue_layer_id);
  require(red_index >= 0 && blue_index >= 0 && current.layers.at(red_index).toMap()
              .value(QStringLiteral("selectionMaskSetId")).toString() == mask_set_id &&
              editor.workingSelectionSetId() == mask_set_id,
          QStringLiteral("The red owner and editable working selection must share one mask-set identity."));

  const auto initial_expected = render_expected(source, current);
  const auto initial_published = sink.image(QStringLiteral("edited"));
  QString initial_detail;
  require(within_channel_tolerance(initial_published, initial_expected, 2, &initial_detail),
          QStringLiteral("The initial sibling composition must match canonical rendering: %1")
              .arg(initial_detail));
  require(initial_published.pixelColor(390, 190).blue() > 150 &&
              initial_published.pixelColor(390, 190).red() < 100,
          QStringLiteral("The unrelated blue sibling must be visibly present before mask edits."));
  const auto blue_sibling_baseline = initial_published.copy();

  verify_commit_undo_redo(editor, sink, source, blue_sibling_baseline,
      QStringLiteral("invert_layer_mask"), [&] {
        red_index = layer_index(recipe(editor), red_layer_id);
        return editor.invertLayerMask(red_index, mask_set_id);
      }, QStringLiteral("Invert the explicitly linked layer mask"));

  verify_commit_undo_redo(editor, sink, source, blue_sibling_baseline,
      QStringLiteral("paint_layer_mask"), [&] {
        red_index = layer_index(recipe(editor), red_layer_id);
        if (red_index < 0 || !editor.selectLayerMask(red_index, mask_set_id)) return false;
        if (!editor.beginPhotoPaintStroke(red_index,
                {{QStringLiteral("erase"), true}, {QStringLiteral("size"), 32.0},
                 {QStringLiteral("hardness"), 1.0}, {QStringLiteral("opacity"), 1.0},
                 {QStringLiteral("flow"), 1.0}, {QStringLiteral("pressureSize"), false},
                 {QStringLiteral("clipSelection"), true}})) return false;
        editor.addPhotoPaintPoint(87.0, 192.0);
        return editor.finishPhotoPaintStroke();
      }, QStringLiteral("Paint the selected shared layer mask"));

  editor.finishLayerMaskEditing();
  verify_commit_undo_redo(editor, sink, source, blue_sibling_baseline,
      QStringLiteral("invert_selection"), [&] { return editor.invertActiveSelection(); },
      QStringLiteral("Invert the working selection shared with its layer owner"));
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  ScopedEnvironment app_data("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath(QStringLiteral("app")).toUtf8());
  ScopedEnvironment persistence("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));
  QGuiApplication application(argc, argv);
  try {
    test_layer_mask_preview_publication();
    std::cout << "Layer-mask committed previews preserve unrelated sibling layers and Undo/Redo pixels.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
