#include "edit_adjustment_patch.h"
#include "edit_recipe_v13_persistence.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "photo_component_workspace_controller.h"
#include "photo_render_utils.h"

#include <QColorSpace>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <QUuid>
#include <QHash>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <iostream>
#include <stdexcept>
#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

using namespace photo_editor::desktop;

namespace photo_editor::desktop {
void begin_photo_render_stage_profile_for_tests();
QVariantMap end_photo_render_stage_profile_for_tests();
QImage smart_select_source_image_for_test(
    const QString& file_path,
    const QString& asset_id,
    const EditState& state);
}

namespace {

class TestImageSink final : public EditorPreviewImageSink {
 public:
  void setImage(const QString& key, const QImage& image) override {
    if (image.isNull()) images_.remove(key);
    else images_.insert(key, image);
  }
  void clearImage(const QString& key) override { images_.remove(key); }
  QImage image(const QString& key) const override { return images_.value(key); }
 private:
  QHash<QString, QImage> images_;
};

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

EditRecipeState recipe(const EditorPreviewController& editor) {
  const auto parsed = parse_adjustment_patch_json(
      editor.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok(), "The editor recipe must parse before it is inspected.");
  return *parsed.state;
}

bool same_pixels(const QImage& left, const QImage& right) {
  if (left.isNull() || right.isNull() || left.size() != right.size()) return false;
  const auto a = left.convertToFormat(QImage::Format_RGBA8888);
  const auto b = right.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < a.height(); ++y) {
    if (std::memcmp(a.constScanLine(y), b.constScanLine(y),
                    static_cast<std::size_t>(a.width()) * 4) != 0) {
      return false;
    }
  }
  return true;
}

QImage render(const QImage& base, const EditRecipeState& state) {
  const auto image = render_output_image_with_backend(
      base, state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  require(!image.isNull(), "The component workflow CPU render must be available.");
  return image;
}

void settle(EditorPreviewController& editor, const char* context) {
  QElapsedTimer timer;
  timer.start();
  const auto idle = [&editor]() {
    return !editor.processing() && !editor.fullRenderRunning() &&
        !editor.professionalToolsBusy() && !editor.photoPaintBusy() &&
        !editor.selectionWorkBusy() && !editor.photoExportBusy();
  };
  while (!idle() && timer.elapsed() < 90000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  require(idle(), context);
}

template <typename Predicate>
void wait_until(Predicate done, const char* message, int timeout_ms = 90000) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  require(done(), message);
}

void wait_workspace(PhotoComponentWorkspaceController& workspace,
                    EditorPreviewController* expected_controller,
                    const char* message) {
  wait_until([&workspace, expected_controller]() {
    return !workspace.busy() && workspace.active() &&
        workspace.controller() == expected_controller;
  }, message);
}

void wait_closed(PhotoComponentWorkspaceController& workspace,
                 const char* message) {
  wait_until([&workspace]() { return !workspace.busy() && !workspace.active(); },
             message);
}

void wait_apply_rejection(PhotoComponentWorkspaceController& workspace,
                          const char* message) {
  wait_until([&workspace]() {
    return !workspace.busy() && workspace.active() &&
        !workspace.error().trimmed().isEmpty();
  }, message);
}

QVariantMap layer_by_id(const QVariantList& layers, const QString& id) {
  // IDs are scoped to their current layer document. Search all rows here
  // before descending into ordinary nested group containers so a repeated
  // document-local ID cannot shadow a later root-level placement.
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("id")).toString() == id) return layer;
  }
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    for (const auto& key : {QStringLiteral("children"),
                            QStringLiteral("layers")}) {
      const auto nested = layer_by_id(layer.value(key).toList(), id);
      if (!nested.isEmpty()) return nested;
    }
  }
  return {};
}

QVariantMap layer_by_id(const EditRecipeState& state, const QString& id) {
  return layer_by_id(state.layers, id);
}

QVariantMap mask_by_set_id(const QVariantList& masks, const QString& set_id,
                          const QString& kind) {
  for (const auto& value : masks) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("maskSetId")).toString() == set_id &&
        mask.value(QStringLiteral("kind")).toString() == kind) return mask;
  }
  return {};
}

QByteArray canonical_mask_json(const QVariantList& masks) {
  return QJsonDocument(QJsonArray::fromVariantList(masks))
      .toJson(QJsonDocument::Compact);
}

QByteArray canonical_mask_definition_json(const QVariantMap& mask) {
  return canonical_mask_json(mask.isEmpty() ? QVariantList{} : QVariantList{mask});
}

QString changed_mask_fields(const QVariantMap& expected,
                            const QVariantMap& actual) {
  QStringList changed;
  for (auto iterator = expected.constBegin(); iterator != expected.constEnd(); ++iterator) {
    const auto key = iterator.key();
    if (!actual.contains(key) ||
        canonical_mask_definition_json({{key, iterator.value()}}) !=
            canonical_mask_definition_json({{key, actual.value(key)}})) {
      changed.append(key);
    }
  }
  for (auto iterator = actual.constBegin(); iterator != actual.constEnd(); ++iterator) {
    if (!expected.contains(iterator.key())) changed.append(QStringLiteral("+") + iterator.key());
  }
  if (changed.size() > 16) {
    changed = changed.mid(0, 16);
    changed.append(QStringLiteral("..."));
  }
  return changed.join(QLatin1Char(','));
}

QVariantMap layer_by_id(const EditorPreviewController& editor,
                        const QString& id) {
  return layer_by_id(recipe(editor), id);
}

int layer_index(const EditorPreviewController& editor, const QString& id) {
  const auto layers = recipe(editor).layers;
  for (int index = 0; index < layers.size(); ++index) {
    if (layers.at(index).toMap().value(QStringLiteral("id")).toString() == id)
      return index;
  }
  return -1;
}

QString top_layer_id(const EditorPreviewController& editor,
                     const QString& kind,
                     const QString& name = {}) {
  for (const auto& value : recipe(editor).layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("kind")).toString() == kind &&
        (name.isEmpty() || layer.value(QStringLiteral("name")).toString() == name)) {
      return layer.value(QStringLiteral("id")).toString();
    }
  }
  return {};
}

QString component_content_id(const EditorPreviewController& editor,
                             const QString& instance_id) {
  return layer_by_id(editor, instance_id)
      .value(QStringLiteral("smartObjectDocument")).toMap()
      .value(QStringLiteral("contentId")).toString();
}

QByteArray file_bytes(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "Read the embedded source fixture bytes.");
  return file.readAll();
}

QString preview_key(const QString& source) {
  const QUrl url(source);
  if (url.scheme() != QStringLiteral("image") ||
      url.host() != QStringLiteral("editor-preview")) return {};
  auto path = url.path();
  if (path.startsWith(QLatin1Char('/'))) path.remove(0, 1);
  return path;
}

void restore_history(EditorPreviewController& editor, bool redo) {
  const auto asset_id = editor.assetId();
  bool finished = false;
  bool succeeded = false;
  const auto connection = QObject::connect(
      &editor, &EditorPreviewController::historyRestoreFinished, &editor,
      [&asset_id, &finished, &succeeded](const QString& restored_asset,
                                         bool success) {
        if (restored_asset != asset_id) return;
        finished = true;
        succeeded = success;
      });
  if (redo) editor.redo();
  else editor.undo();
  wait_until([&finished]() { return finished; },
             "Child editor history restore must emit completion.");
  QObject::disconnect(connection);
  require(succeeded, "Child editor history restore must succeed.");
  settle(editor, "Child editor must settle after history restore.");
}

void drain_cancelled_workspace(PhotoComponentWorkspaceController& workspace,
                               EditorPreviewController& parent) {
  require(QThreadPool::globalInstance()->waitForDone(90000),
          "Cancelled workspace workers must drain without blocking.");
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  settle(parent, "Parent rendering and professional work must settle after cancellation.");
  require(!workspace.active() && !workspace.busy() &&
              workspace.error().isEmpty() &&
              !parent.professionalToolsBusy() &&
              !parent.smartDocumentEditState().value(QStringLiteral("active")).toBool(),
          "Cancelled workspace callbacks must leave no active frame or parent component draft.");
}

QString create_component(EditorPreviewController& editor,
                         const QString& name,
                         const QString& fill_name,
                         const QString& fill_color,
                         const QString& source_path,
                         const QString& text_name,
                         const QString& text) {
  require(editor.addFillLayer(fill_color, fill_name),
          "Create a full-canvas fill for the editable component fixture.");
  require(editor.addSourceLayer(source_path, QStringLiteral("Embedded product image")),
          "Create the image child for the editable component fixture.");
  require(editor.addTextLayer(text, text_name),
          "Create editable text for the component fixture.");
  settle(editor, "Fixture layers must render before grouping.");

  const auto fill_id = top_layer_id(editor, QStringLiteral("fill"), fill_name);
  const auto source_id = top_layer_id(editor, QStringLiteral("source"));
  const auto text_id = top_layer_id(editor, QStringLiteral("text"), text_name);
  require(!fill_id.isEmpty() && !source_id.isEmpty() && !text_id.isEmpty(),
          "The component fixture must expose fill, source, and text layers.");
  const QVariantList members{layer_index(editor, text_id),
                             layer_index(editor, source_id),
                             layer_index(editor, fill_id)};
  require(members.at(0).toInt() >= 0 && members.at(1).toInt() >= 0 &&
              members.at(2).toInt() >= 0 &&
              editor.groupPhotoLayers(members, name),
          "Group the editable image, fill, and text fixture.");
  settle(editor, "Grouped component fixture must render.");
  const auto group_id = top_layer_id(editor, QStringLiteral("group"), name);
  require(!group_id.isEmpty() &&
              editor.convertPhotoLayerToComponent(layer_index(editor, group_id)),
          "Convert the grouped fixture into an editable component document.");
  settle(editor, "Converted component fixture must render.");
  const auto converted = layer_by_id(editor, group_id);
  const auto document = converted.value(QStringLiteral("smartObjectDocument")).toMap();
  if (document.isEmpty()) {
    const auto detail = QStringLiteral(
        "Component conversion did not publish a document: asset=%1 group=%2 kind=%3 status=%4")
        .arg(editor.assetId(), group_id,
             converted.value(QStringLiteral("kind")).toString(), editor.status());
    require(false, qPrintable(detail));
  }
  return group_id;
}

QVariantMap component_document(const EditorPreviewController& editor,
                               const QString& id) {
  return layer_by_id(editor, id)
      .value(QStringLiteral("smartObjectDocument")).toMap();
}

void assert_export_matches(const QString& path,
                           const QImage& expected,
                           const char* message) {
  const QImage exported(path);
  require(!exported.isNull() && exported.size() == expected.size(), message);
  const auto actual_pixels = exported.convertToFormat(QImage::Format_RGBA8888);
  const auto expected_pixels = expected.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < expected_pixels.height(); ++y) {
    for (int x = 0; x < expected_pixels.width(); ++x) {
      const auto a = actual_pixels.pixel(x, y);
      const auto e = expected_pixels.pixel(x, y);
      if (qAbs(qRed(a) - qRed(e)) > 2 || qAbs(qGreen(a) - qGreen(e)) > 2 ||
          qAbs(qBlue(a) - qBlue(e)) > 2 || qAbs(qAlpha(a) - qAlpha(e)) > 2) {
        require(false, message);
      }
    }
  }
}

struct ReopenReceipt {
  QString asset_id;
  QString source_path;
  QString export_path;
  QString canonical_patch;
  QImage source_image;
  QImage expected_pixels;
};

void test_reopened_component_export(const ReopenReceipt& receipt) {
  TestImageSink image_sink;
  EditorPreviewController reopened;
  reopened.setImageProvider(&image_sink);
  reopened.loadAsset(receipt.asset_id, receipt.source_path, {}, {}, {}, {},
                     receipt.canonical_patch, true);
  settle(reopened, "The canonical component recipe must reopen in a fresh controller.");
  const auto reopened_recipe = recipe(reopened);
  const auto decoded = parse_adjustment_patch_json(receipt.canonical_patch);
  require(decoded.ok() && decoded.state->layers == reopened_recipe.layers &&
              same_pixels(render(receipt.source_image, reopened_recipe),
                      receipt.expected_pixels),
          "Reopening the canonical recipe must preserve child edits and rendered pixels.");
  require(reopened.startFlattenedPhotoExport(receipt.export_path),
          "Start a real flattened PNG export from the reopened component recipe.");
  settle(reopened, "The flattened component PNG export must finish.");
  require(reopened.photoExportError().isEmpty(),
          "The flattened component PNG export must finish without an error.");
  assert_export_matches(receipt.export_path, receipt.expected_pixels,
                        "Exported PNG must match the reopened child canvas render.");

  const auto proof_directory = qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR").trimmed();
  if (!proof_directory.isEmpty()) {
    QDir output(proof_directory);
    if (!output.exists() && !QDir().mkpath(proof_directory)) {
      std::cerr << "Could not create component workspace evidence directory: "
                << proof_directory.toStdString() << '\n';
      return;
    }
    QString stem = QStringLiteral("component-workspace");
    if (output.exists(stem + QStringLiteral("-export.png")) ||
        output.exists(stem + QStringLiteral("-reference.png"))) {
      stem += QLatin1Char('-');
      stem += QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    const auto exported_path = output.filePath(stem + QStringLiteral("-export.png"));
    const auto reference_path = output.filePath(stem + QStringLiteral("-reference.png"));
    if (!QFile::copy(receipt.export_path, exported_path) ||
        !receipt.expected_pixels.save(reference_path, "PNG")) {
      std::cerr << "Could not save component workspace export evidence under: "
                << proof_directory.toStdString() << '\n';
      return;
    }
    std::cout << "Component workflow pixel evidence: exported="
              << exported_path.toStdString() << " reference="
              << reference_path.toStdString() << '\n';
  }
}

ReopenReceipt test_component_workspace(const QString& runtime_path) {
  constexpr int width = 192;
  constexpr int height = 128;
  QImage base(width, height, QImage::Format_RGB32);
  base.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      base.setPixelColor(x, y, QColor(20 + x, 30 + y, 54 + ((x + y) % 60)));
    }
  }
  const auto base_path = QDir(runtime_path).filePath(QStringLiteral("canvas-source.png"));
  require(base.save(base_path, "PNG"), "Write the real outer canvas fixture.");
  QImage transparent_source(width, height, QImage::Format_ARGB32);
  transparent_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  transparent_source.fill(Qt::transparent);
  const auto transparent_source_path =
      QDir(runtime_path).filePath(QStringLiteral("transparent-component-source.png"));
  require(transparent_source.save(transparent_source_path, "PNG"),
          "Write the transparent component source fixture.");

  QImage embedded(40, 32, QImage::Format_ARGB32);
  embedded.setColorSpace(QColorSpace(QColorSpace::SRgb));
  embedded.fill(Qt::transparent);
  for (int y = 2; y < embedded.height() - 2; ++y) {
    for (int x = 2; x < embedded.width() - 2; ++x) {
      embedded.setPixelColor(x, y, QColor(224, 75 + (x % 40), 42, 210));
    }
  }
  const auto embedded_path = QDir(runtime_path).filePath(QStringLiteral("embedded-product.png"));
  require(embedded.save(embedded_path, "PNG"), "Write the embedded image fixture.");

  QImage added_source(26, 18, QImage::Format_ARGB32);
  added_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  added_source.fill(QColor(QStringLiteral("#3d91d2")));
  const auto added_source_path = QDir(runtime_path).filePath(
      QStringLiteral("child-added-source.png"));
  require(added_source.save(added_source_path, "PNG"),
          "Write the child-added source fixture.");
  const auto added_source_bytes = file_bytes(added_source_path);

  TestImageSink image_provider;
  EditorPreviewController root;
  root.setImageProvider(&image_provider);
  const auto root_asset_id = QStringLiteral("component-workspace-workflow-root");
  require(!qEnvironmentVariableIsSet("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE") &&
              root.catalogRecipePersistenceEnabled(),
          "The root fixture must run with ordinary recipe persistence enabled.");
  root.loadAsset(root_asset_id, base_path, {}, {}, {}, {}, {});
  settle(root, "The root source preview must finish before creating components.");

  const auto inner_id = create_component(
      root, QStringLiteral("Inner component"), QStringLiteral("Inner fill"),
      QStringLiteral("#254c6d"), embedded_path,
      QStringLiteral("Inner title"), QStringLiteral("INNER EDITABLE TITLE"));

  require(root.addFillLayer(QStringLiteral("#142b3c"),
                            QStringLiteral("Outer background")) &&
              root.addTextLayer(QStringLiteral("OUTER EDITABLE TITLE"),
                                QStringLiteral("Outer title")),
          "Create the outer component's editable fill and text children.");
  settle(root, "Outer fixture layers must render before component conversion.");
  const auto outer_fill_id = top_layer_id(root, QStringLiteral("fill"),
                                         QStringLiteral("Outer background"));
  const auto outer_text_id = top_layer_id(root, QStringLiteral("text"),
                                          QStringLiteral("Outer title"));
  const QVariantList outer_members{layer_index(root, outer_text_id),
                                   layer_index(root, inner_id),
                                   layer_index(root, outer_fill_id)};
  require(!outer_fill_id.isEmpty() && !outer_text_id.isEmpty() &&
              outer_members.at(0).toInt() >= 0 && outer_members.at(1).toInt() >= 0 &&
              outer_members.at(2).toInt() >= 0 &&
              root.groupPhotoLayers(outer_members, QStringLiteral("Outer design")),
          "Group the nested component with outer fill and text.");
  settle(root, "The outer design group must render.");
  const auto outer_group_id = top_layer_id(root, QStringLiteral("group"),
                                           QStringLiteral("Outer design"));
  require(!outer_group_id.isEmpty() &&
              root.convertPhotoLayerToComponent(layer_index(root, outer_group_id)),
          "Convert the outer design into the editable canvas component.");
  settle(root, "The outer component preview must settle after conversion.");
  const auto outer_document = component_document(root, outer_group_id);
  if (outer_document.isEmpty()) {
    const auto layer = layer_by_id(root, outer_group_id);
    const auto detail = QStringLiteral(
        "Outer component conversion did not publish a document: id=%1 kind=%2 status=%3")
        .arg(outer_group_id, layer.value(QStringLiteral("kind")).toString(),
             root.status());
    require(false, qPrintable(detail));
  }
  require(root.duplicatePhotoComponent(layer_index(root, outer_group_id)),
          "Create a second shared placement for component publication proof.");
  settle(root, "The shared component placement must render.");

  const auto before_open_cancel = recipe(root);
  const auto before_open_cancel_pixels = render(base, before_open_cancel);
  const auto before_open_cancel_history = root.editHistoryCurrentIndex();
  PhotoComponentWorkspaceController open_cancel_workspace(&root);
  require(open_cancel_workspace.open(layer_index(root, outer_group_id)),
          "Start asynchronous component validation for the open-cancel race.");
  require(open_cancel_workspace.cancel(),
          "Cancel component opening before asynchronous validation publishes a frame.");
  drain_cancelled_workspace(open_cancel_workspace, root);
  require(recipe(root) == before_open_cancel &&
              root.editHistoryCurrentIndex() == before_open_cancel_history &&
              same_pixels(render(base, recipe(root)), before_open_cancel_pixels),
          "Immediate open cancellation must preserve the parent recipe, history, and exact pixels.");

  const auto before_no_op_apply = recipe(root);
  const auto before_no_op_apply_pixels = render(base, before_no_op_apply);
  const auto before_no_op_apply_history = root.editHistoryCurrentIndex();
  PhotoComponentWorkspaceController no_op_workspace(&root);
  require(no_op_workspace.open(layer_index(root, outer_group_id)),
          "Open the component for a no-op Apply check.");
  wait_until([&no_op_workspace]() {
    return !no_op_workspace.busy() && no_op_workspace.active();
  }, "The no-op component workspace must finish opening.");
  auto* no_op_editor = no_op_workspace.controller();
  require(no_op_editor != nullptr && no_op_editor != &root,
          "No-op Apply must use a transient child editor.");
  settle(*no_op_editor, "The untouched child document must finish loading before no-op Apply.");
  require(no_op_workspace.canApply() && no_op_workspace.apply(),
          "Applying an untouched component must be accepted as a no-op.");
  wait_closed(no_op_workspace,
              "No-op Apply must close the transient component workspace.");
  settle(root, "The root must remain settled after no-op Apply.");
  require(recipe(root) == before_no_op_apply &&
              root.editHistoryCurrentIndex() == before_no_op_apply_history &&
              same_pixels(render(base, recipe(root)), before_no_op_apply_pixels),
          "Applying an untouched component must preserve parent recipe, history, and exact pixels.");

  const auto initial_recipe = recipe(root);
  const auto shared_content_id = component_content_id(root, outer_group_id);
  QStringList shared_instance_ids;
  for (const auto& value : initial_recipe.layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("smartObjectDocument")).toMap()
            .value(QStringLiteral("contentId")).toString() == shared_content_id) {
      shared_instance_ids.push_back(layer.value(QStringLiteral("id")).toString());
    }
  }
  if (shared_instance_ids.size() != 2 ||
      !shared_instance_ids.contains(outer_group_id)) {
    QStringList layer_summary;
    for (const auto& value : initial_recipe.layers) {
      const auto layer = value.toMap();
      const auto document = layer.value(QStringLiteral("smartObjectDocument")).toMap();
      layer_summary.push_back(QStringLiteral(
          "%1:%2:%3:parent=%4:hasDoc=%5:content=%6:locked=%7")
          .arg(layer.value(QStringLiteral("name")).toString(),
               layer.value(QStringLiteral("id")).toString(),
               layer.value(QStringLiteral("kind")).toString(),
               layer.value(QStringLiteral("parentId")).toString(),
               document.isEmpty() ? QStringLiteral("false") : QStringLiteral("true"),
               document.value(QStringLiteral("contentId")).toString(),
               layer.value(QStringLiteral("locked")).toBool()
                   ? QStringLiteral("true") : QStringLiteral("false")));
    }
    const auto detail = QStringLiteral(
        "Expected two shared component placements; status=%1 outer=%2 content=%3 matches=%4 layers=[%5]")
        .arg(root.status(), outer_group_id, shared_content_id)
        .arg(shared_instance_ids.size())
        .arg(layer_summary.join(QStringLiteral("; ")));
    require(false, qPrintable(detail));
  }
  const auto initial_document = component_document(root, outer_group_id);
  const auto nested_inner = layer_by_id(
      initial_document.value(QStringLiteral("layers")).toList(), inner_id);
  require(nested_inner.value(QStringLiteral("smartObjectDocument")).toMap().size() > 0 &&
              !layer_by_id(initial_document.value(QStringLiteral("layers")).toList(),
                           outer_text_id).isEmpty(),
          "The outer component must preserve the nested component and editable text definitions.");

  const auto initial_pixels = render(base, initial_recipe);
  const auto initial_history = root.editHistoryCurrentIndex();
  const auto parent_preview_source = root.displayPreviewSource();
  const auto parent_preview_key = preview_key(parent_preview_source);
  require(!parent_preview_key.isEmpty(),
          "The root controller must publish an image-provider preview for isolation checks.");
  const auto parent_preview = image_provider.image(parent_preview_key);
  require(!parent_preview.isNull(),
          "The root image provider must hold the root preview before opening contents.");

  PhotoComponentWorkspaceController workspace(&root);
  require(workspace.open(layer_index(root, outer_group_id)),
          "Open the outer component in an isolated canvas workspace.");
  wait_until([&workspace]() { return !workspace.busy() && workspace.active(); },
             "The outer component workspace must finish opening.");
  auto* outer_editor = workspace.controller();
  require(outer_editor != nullptr && outer_editor != &root && outer_editor->hasAsset(),
          "Component editing must use a separate loaded child editor controller.");
  settle(*outer_editor, "The isolated component editor preview must finish loading.");
  require(!outer_editor->catalogRecipePersistenceEnabled() &&
              !outer_editor->recipeSavePending(),
          "The transient child must not claim catalog recipe persistence or queue a journal save.");
  require(outer_editor->assetId() != root.assetId() &&
              outer_editor->sourceImageWidth() > 0 && outer_editor->sourceImageHeight() > 0,
          "The child editor must own a distinct synthetic transparent canvas asset.");
  const auto opened_document = component_document(root, outer_group_id);
  require(outer_editor->sourceImageWidth() ==
                  opened_document.value(QStringLiteral("canvasWidth")).toInt() &&
              outer_editor->sourceImageHeight() ==
                  opened_document.value(QStringLiteral("canvasHeight")).toInt(),
          "The child transparent source dimensions must match the editable document canvas.");
  const auto child_preview_key = preview_key(outer_editor->displayPreviewSource());
  require(!child_preview_key.isEmpty() && child_preview_key != parent_preview_key,
          "The child canvas preview must use a child-specific provider key.");
  require(same_pixels(image_provider.image(parent_preview_key), parent_preview),
          "Opening child contents must not replace the root preview provider image.");

  const auto root_before_cancel = recipe(root);
  const auto root_pixels_before_cancel = render(base, root_before_cancel);
  require(outer_editor->setPhotoLayerTextById(outer_text_id,
                                               QStringLiteral("CANCELLED CHILD TEXT")),
          "Author an edit that will be discarded by child Cancel.");
  settle(*outer_editor, "The isolated child text edit must render before Cancel.");
  require(recipe(root).layers == root_before_cancel.layers &&
              same_pixels(render(base, recipe(root)), root_pixels_before_cancel) &&
              root.editHistoryCurrentIndex() == initial_history,
          "Child authoring must leave the parent recipe, rendered pixels, and history unchanged.");
  require(workspace.cancel(), "Cancel the child canvas workspace.");
  wait_closed(workspace, "Cancel must close the isolated child workspace.");
  require(recipe(root) == root_before_cancel &&
              same_pixels(render(base, recipe(root)), root_pixels_before_cancel) &&
              root.editHistoryCurrentIndex() == initial_history,
          "Cancel must discard child edits without changing parent recipe, pixels, or history.");

  require(workspace.open(layer_index(root, outer_group_id)),
          "Reopen the component for the complete canvas workflow.");
  wait_until([&workspace]() { return !workspace.busy() && workspace.active(); },
             "The component workspace must reopen after Cancel.");
  outer_editor = workspace.controller();
  require(outer_editor != nullptr && outer_editor != &root,
          "Reopened contents must still use an isolated child editor.");
  settle(*outer_editor, "Reopened child canvas must finish loading.");

  const auto prior_fake_segmentation = qgetenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION");
  const auto had_fake_segmentation = qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION");
  qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", QByteArrayLiteral("1"));
  constexpr int smart_select_left = 22;
  constexpr int smart_select_top = 20;
  constexpr int smart_select_right = 58;
  constexpr int smart_select_bottom = 48;
  require(outer_editor->addPhotoShape(
              QStringLiteral("rectangle"), smart_select_left, smart_select_top,
              smart_select_right, smart_select_bottom,
              outer_editor->sourceImageWidth(), outer_editor->sourceImageHeight(),
              QStringLiteral("#f020dc"), QStringLiteral("#f020dc"), 0.0),
          "Add visible child artwork for merged-source Smart Select coverage.");
  settle(*outer_editor, "The Smart Select artwork must render in the component child.");
  const auto smart_select_shape_id =
      top_layer_id(*outer_editor, QStringLiteral("pixel"), QStringLiteral("Rectangle"));
  const auto smart_select_shape_index = layer_index(*outer_editor, smart_select_shape_id);
  require(!smart_select_shape_id.isEmpty() && smart_select_shape_index >= 0 &&
              outer_editor->setPhotoLayerTransform(
                  smart_select_shape_index, 64, 44, 1.0, 17.0),
          "The child Smart Select artwork must remain an editable layer.");
  settle(*outer_editor, "The transformed Smart Select artwork must render in the component child.");
  const auto smart_select_state = recipe(*outer_editor);
  const auto smart_select_canvas = smart_select_source_image_for_test(
      transparent_source_path, outer_editor->assetId(), smart_select_state);
  auto state_without_smart_select_artwork = smart_select_state;
  for (qsizetype layer_index = state_without_smart_select_artwork.layers.size();
       layer_index > 0; --layer_index) {
    if (state_without_smart_select_artwork.layers.at(layer_index - 1).toMap()
            .value(QStringLiteral("id")).toString() == smart_select_shape_id) {
      state_without_smart_select_artwork.layers.removeAt(layer_index - 1);
      break;
    }
  }
  const auto canvas_without_smart_select_artwork = smart_select_source_image_for_test(
      transparent_source_path, outer_editor->assetId(), state_without_smart_select_artwork);
  auto changed_pixels = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto with_shape = smart_select_canvas.pixelColor(x, y);
      const auto without_shape = canvas_without_smart_select_artwork.pixelColor(x, y);
      const auto distance = std::abs(with_shape.red() - without_shape.red()) +
          std::abs(with_shape.green() - without_shape.green()) +
          std::abs(with_shape.blue() - without_shape.blue());
      if (distance <= 80) continue;
      ++changed_pixels;
    }
  }
  require(changed_pixels > 100,
          "The composite Smart Select source must contain the transformed child artwork pixels.");
  const auto smart_select_shape = layer_by_id(smart_select_state, smart_select_shape_id);
  const auto shape_x = smart_select_shape.value(QStringLiteral("x")).toDouble();
  const auto shape_y = smart_select_shape.value(QStringLiteral("y")).toDouble();
  const auto shape_scale = smart_select_shape.value(QStringLiteral("scale"), 1.0).toDouble();
  const auto shape_width = smart_select_shape.value(QStringLiteral("pixelWidth")).toDouble();
  const auto shape_height = smart_select_shape.value(QStringLiteral("pixelHeight")).toDouble();
  const QPoint smart_select_sample(
      qBound(0, static_cast<int>(std::lround(shape_x + shape_width * shape_scale * 0.5)),
             width - 1),
      qBound(0, static_cast<int>(std::lround(shape_y + shape_height * shape_scale * 0.5)),
             height - 1));
  const auto artwork_pixel = smart_select_canvas.pixelColor(smart_select_sample);
  const auto backdrop_pixel =
      canvas_without_smart_select_artwork.pixelColor(smart_select_sample);
  const auto is_magenta_artwork =
      artwork_pixel.red() > artwork_pixel.green() + 50 &&
      artwork_pixel.blue() > artwork_pixel.green() + 50;
  const auto artwork_pixel_distance =
      std::abs(artwork_pixel.red() - backdrop_pixel.red()) +
      std::abs(artwork_pixel.green() - backdrop_pixel.green()) +
      std::abs(artwork_pixel.blue() - backdrop_pixel.blue());
  const auto smart_select_source_size_ok =
      smart_select_canvas.size() == QSize(width, height) &&
      canvas_without_smart_select_artwork.size() == QSize(width, height);
  if (!smart_select_source_size_ok || artwork_pixel_distance <= 80 ||
      !is_magenta_artwork) {
    QString capture_detail;
    const auto proof_directory =
        qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR").trimmed();
    if (!proof_directory.isEmpty()) {
      QDir output(proof_directory);
      if (!output.exists()) output.mkpath(QStringLiteral("."));
      const auto stem = QStringLiteral("component-smart-select-source-%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
      const auto merged_path = output.filePath(stem + QStringLiteral("-merged.png"));
      const auto backdrop_path = output.filePath(stem + QStringLiteral("-without-shape.png"));
      if (smart_select_canvas.save(merged_path, "PNG") &&
          canvas_without_smart_select_artwork.save(backdrop_path, "PNG")) {
        capture_detail = QStringLiteral(" captures=%1,%2").arg(merged_path, backdrop_path);
      }
    }
    const auto detail = QStringLiteral(
        "Smart Select sampler diagnostic: merged=%1x%2 backdrop=%3x%4 expected=%5x%6 sample=(%7,%8) merged-rgba=(%9,%10,%11,%12) backdrop-rgba=(%13,%14,%15,%16) distance=%17 changedPixels=%18%19")
        .arg(smart_select_canvas.width())
        .arg(smart_select_canvas.height())
        .arg(canvas_without_smart_select_artwork.width())
        .arg(canvas_without_smart_select_artwork.height())
        .arg(width)
        .arg(height)
        .arg(smart_select_sample.x())
        .arg(smart_select_sample.y())
        .arg(artwork_pixel.red())
        .arg(artwork_pixel.green())
        .arg(artwork_pixel.blue())
        .arg(artwork_pixel.alpha())
        .arg(backdrop_pixel.red())
        .arg(backdrop_pixel.green())
        .arg(backdrop_pixel.blue())
        .arg(backdrop_pixel.alpha())
        .arg(artwork_pixel_distance)
        .arg(changed_pixels)
        .arg(capture_detail);
    require(false, qPrintable(detail));
  }
  auto cropped_smart_select_state = smart_select_state;
  cropped_smart_select_state.crop_rect_x = 0.0;
  cropped_smart_select_state.crop_rect_y = 0.0;
  cropped_smart_select_state.crop_rect_width = 0.35;
  cropped_smart_select_state.crop_rect_height = 0.25;
  const auto uncropped_smart_select_canvas = smart_select_source_image_for_test(
      transparent_source_path, outer_editor->assetId(), cropped_smart_select_state);
  const auto artwork_after_crop = uncropped_smart_select_canvas.pixelColor(smart_select_sample);
  require(uncropped_smart_select_canvas.size() == QSize(width, height) &&
              artwork_after_crop.red() == artwork_pixel.red() &&
              artwork_after_crop.green() == artwork_pixel.green() &&
              artwork_after_crop.blue() == artwork_pixel.blue(),
          "Smart Select must retain full-canvas coordinates and bypass crop exactly once.");

  const auto prompt_x = (static_cast<double>(smart_select_sample.x()) + 0.5) / width;
  const auto prompt_y = (static_cast<double>(smart_select_sample.y()) + 0.5) / height;
  outer_editor->scheduleSmartSelectPreview(prompt_x, prompt_y);
  wait_until([outer_editor]() { return outer_editor->smartSelectPreviewActive(); },
             "Component Smart Select preview must classify its rendered child artwork.");
  outer_editor->rejectSmartSelectPreview();
  outer_editor->smartSelectAddAt(prompt_x, prompt_y, true);
  settle(*outer_editor, "Component Smart Select commit must finish from the same merged source.");
  const auto committed_smart_select = outer_editor->maskStack().value(outer_editor->activeMaskIndex()).toMap();
  const auto committed_smart_select_data =
      committed_smart_select.value(QStringLiteral("segmentationData")).toByteArray();
  const auto committed_smart_select_width =
      committed_smart_select.value(QStringLiteral("segmentationWidth")).toInt();
  const auto committed_smart_select_height =
      committed_smart_select.value(QStringLiteral("segmentationHeight")).toInt();
  require(committed_smart_select.value(QStringLiteral("kind")).toString() ==
                  QStringLiteral("object") &&
              committed_smart_select_width == width &&
              committed_smart_select_height == height &&
              committed_smart_select_data.size() == width * height,
          "Component Smart Select commit must preserve a full-canvas object mask.");
  const auto committed_sample_x = qBound(
      0, static_cast<int>(std::lround(prompt_x * (committed_smart_select_width - 1))),
      committed_smart_select_width - 1);
  const auto committed_sample_y = qBound(
      0, static_cast<int>(std::lround(prompt_y * (committed_smart_select_height - 1))),
      committed_smart_select_height - 1);
  const auto committed_sample_index =
      committed_sample_y * committed_smart_select_width + committed_sample_x;
  require(static_cast<uchar>(committed_smart_select_data.at(committed_sample_index)) > 0,
          "Component Smart Select commit must preserve the full-canvas object mask at the prompted artwork pixel.");
  if (had_fake_segmentation) qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", prior_fake_segmentation);
  else qunsetenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION");

  const auto outer_child_layers = recipe(*outer_editor).layers;
  const auto nested_child = layer_by_id(outer_child_layers, inner_id);
  const auto nested_child_index = layer_index(*outer_editor, inner_id);
  require(nested_child_index >= 0 &&
              !nested_child.value(QStringLiteral("smartObjectDocument")).toMap().isEmpty(),
          "The child canvas must contain the original nested editable component.");
  require(workspace.open(nested_child_index),
          "Open nested component contents from the child canvas.");
  wait_until([&workspace, outer_editor]() {
    return !workspace.busy() && workspace.active() &&
        workspace.controller() != outer_editor;
  },
             "Nested component opening must finish asynchronously.");
  auto* nested_editor = workspace.controller();
  require(nested_editor != nullptr && nested_editor != outer_editor && nested_editor != &root,
          "Nested component editing must create a second isolated editor frame.");
  settle(*nested_editor, "Nested component preview must finish loading.");
  const auto inner_text_id = top_layer_id(*nested_editor, QStringLiteral("text"),
                                          QStringLiteral("Inner title"));
  require(!inner_text_id.isEmpty() &&
              nested_editor->setPhotoLayerTextById(
                  inner_text_id, QStringLiteral("NESTED WORKSPACE EDIT")),
          "Edit nested text through the nested child editor.");
  settle(*nested_editor, "Nested text edit must render before one-level Apply.");
  require(recipe(root) == root_before_cancel,
          "Nested child edits must remain private from the outer document until nested Apply.");
  require(workspace.apply(), "Apply the nested document into its immediate parent frame.");
  wait_workspace(workspace, outer_editor,
                 "Nested Apply must return to the existing outer child frame.");
  require(layer_by_id(component_document(*outer_editor, inner_id)
                          .value(QStringLiteral("layers")).toList(), inner_text_id)
                  .value(QStringLiteral("text")).toString() ==
              QStringLiteral("NESTED WORKSPACE EDIT") &&
              recipe(root) == root_before_cancel &&
              root.editHistoryCurrentIndex() == initial_history,
          "Nested Apply must update one child document while leaving root history untouched.");

  require(outer_editor->addFillLayer(QStringLiteral("#3a5e76"),
                                     QStringLiteral("Lock probe fill")) &&
              outer_editor->addTextLayer(QStringLiteral("LOCK PROBE TEXT"),
                                         QStringLiteral("Lock probe text")) &&
              outer_editor->addPhotoShape(
                  QStringLiteral("rectangle"), 12, 10, 44, 34,
                  outer_editor->sourceImageWidth(), outer_editor->sourceImageHeight(),
                  QStringLiteral("#d87539"), QStringLiteral("#f2e4d5"), 1.5),
          "Create representative fill, text, and shape children for inherited-lock checks.");
  settle(*outer_editor, "Lock-probe children must settle before grouping.");
  const auto lock_probe_fill_id = top_layer_id(
      *outer_editor, QStringLiteral("fill"), QStringLiteral("Lock probe fill"));
  const auto lock_probe_text_id = top_layer_id(
      *outer_editor, QStringLiteral("text"), QStringLiteral("Lock probe text"));
  const auto lock_probe_shape_id = top_layer_id(
      *outer_editor, QStringLiteral("pixel"), QStringLiteral("Rectangle"));
  const QVariantList lock_probe_members{
      layer_index(*outer_editor, lock_probe_text_id),
      layer_index(*outer_editor, lock_probe_shape_id),
      layer_index(*outer_editor, lock_probe_fill_id)};
  require(!lock_probe_fill_id.isEmpty() && !lock_probe_text_id.isEmpty() &&
              !lock_probe_shape_id.isEmpty() &&
              lock_probe_members.at(0).toInt() >= 0 &&
              lock_probe_members.at(1).toInt() >= 0 &&
              lock_probe_members.at(2).toInt() >= 0 &&
              outer_editor->groupPhotoLayers(lock_probe_members,
                                             QStringLiteral("Locked child group")),
          "Group representative child layer kinds under one editable parent.");
  settle(*outer_editor, "Lock-probe group must settle before locking.");
  const auto lock_probe_group_id = top_layer_id(
      *outer_editor, QStringLiteral("group"), QStringLiteral("Locked child group"));
  const auto lock_probe_group_index = layer_index(*outer_editor, lock_probe_group_id);
  require(lock_probe_group_index >= 0 &&
              outer_editor->setPhotoLayerLocked(lock_probe_group_index, true),
          "Lock the child group before exercising descendant operations.");
  settle(*outer_editor, "Locked child group must settle before testing its children.");
  const auto locked_child_state = recipe(*outer_editor);
  const auto locked_child_history = outer_editor->editHistoryCurrentIndex();
  const auto parent_before_lock_probe = recipe(root);
  const auto lock_probe_text_index = layer_index(*outer_editor, lock_probe_text_id);
  const auto lock_probe_shape_index = layer_index(*outer_editor, lock_probe_shape_id);
  require(lock_probe_text_index >= 0 && lock_probe_shape_index >= 0 &&
              !outer_editor->setPhotoLayerTextById(
                  lock_probe_text_id, QStringLiteral("MUTATED LOCKED TEXT")) &&
              !outer_editor->setPhotoLayerTransform(
                  lock_probe_text_index, 18, 17, 1.2, 9.0) &&
              !outer_editor->setPhotoLayerOpacity(lock_probe_text_index, 0.37) &&
              !outer_editor->setPhotoLayerShapeStyle(
                  lock_probe_shape_index, QStringLiteral("#ffffff"),
                  QStringLiteral("#000000"), 4.0) &&
              !outer_editor->setPhotoLayerGaussianBlur(
                  lock_probe_text_index, QString(), 2.0, 0.8, true) &&
              !outer_editor->setPhotoLayerMaskEnabled(lock_probe_text_index, true) &&
              !outer_editor->deletePhotoLayer(lock_probe_text_index) &&
              !outer_editor->movePhotoLayerOutOfGroup(lock_probe_text_index),
          "Inherited group locks must reject text, transform, opacity, shape style, effect, mask, delete, and reparent APIs.");
  require(recipe(*outer_editor) == locked_child_state &&
              outer_editor->editHistoryCurrentIndex() == locked_child_history &&
              recipe(root) == parent_before_lock_probe &&
              root.editHistoryCurrentIndex() == initial_history,
          "Rejected descendant operations must preserve both parent and child recipes and history.");

  require(outer_editor->setPhotoLayerTextById(
              outer_text_id, QStringLiteral("OUTER WORKSPACE EDIT")),
          "Edit text in the outer child canvas.");
  require(outer_editor->addSourceLayer(added_source_path,
                                       QStringLiteral("Child-added embedded image")),
          "Add a new external image source to the isolated component canvas.");
  settle(*outer_editor, "The new child source must load before Apply.");
  const auto added_source_id = top_layer_id(
      *outer_editor, QStringLiteral("source"),
      QStringLiteral("Child-added embedded image"));
  require(!added_source_id.isEmpty(),
          "The new child image source must have a stable editable layer ID.");
  const auto before_link_attempt = recipe(*outer_editor);
  const auto history_before_link_attempt = outer_editor->editHistoryCurrentIndex();
  require(!outer_editor->convertPhotoLayerToSmartObject(
              layer_index(*outer_editor, added_source_id), true) &&
              recipe(*outer_editor) == before_link_attempt &&
              outer_editor->editHistoryCurrentIndex() == history_before_link_attempt &&
              outer_editor->status().contains(QStringLiteral("Component images are embedded")),
          "Component content must reject explicit linked policy without silently changing it on Apply.");
  auto current_outer_index = layer_index(*outer_editor, inner_id);
  require(current_outer_index >= 0 &&
              outer_editor->setPhotoLayerTransform(current_outer_index, 9, 7, 1.12, 4.0),
          "Transform the embedded nested component in the outer child canvas.");
  const auto text_index_before_move = layer_index(*outer_editor, outer_text_id);
  const auto sibling_count = recipe(*outer_editor).layers.size();
  const auto text_move_offset = text_index_before_move == sibling_count - 1 ? -1 : 1;
  require(text_index_before_move >= 0 && sibling_count > 1 &&
              outer_editor->movePhotoLayer(text_index_before_move, text_move_offset),
          "Reorder outer child layers through the production layer stack API.");
  const auto added_adjustment = outer_editor->addAdjustmentLayer(QStringLiteral("Child color grade"));
  require(added_adjustment, qPrintable(QStringLiteral("Add an editable child adjustment layer: %1").arg(outer_editor->status())));
  require(outer_editor->setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), 0.42),
          "Edit color on the child adjustment layer rather than child global adjustments.");
  const auto fill_id = top_layer_id(*outer_editor, QStringLiteral("fill"),
                                    QStringLiteral("Outer background"));
  const auto fill_index = layer_index(*outer_editor, fill_id);
  require(fill_index >= 0 && outer_editor->setPhotoLayerGaussianBlur(
              fill_index, QString(), 3.5, 0.65, true),
          "Add a layer-local Gaussian effect to the child fill.");
  const auto adjusted_index = layer_index(*outer_editor,
      top_layer_id(*outer_editor, QStringLiteral("adjustment"),
                   QStringLiteral("Child color grade")));
  const auto adjusted_layer_id = top_layer_id(
      *outer_editor, QStringLiteral("adjustment"),
      QStringLiteral("Child color grade"));
  require(adjusted_index >= 0,
          "The child color adjustment layer must remain addressable after reorder.");
  outer_editor->addMask(QStringLiteral("rectangle"));
  outer_editor->setActiveMaskFeather(0.0);
  outer_editor->setActiveMaskCenterX(0.46);
  outer_editor->setActiveMaskCenterY(0.53);
  outer_editor->setActiveMaskRadiusX(0.31);
  outer_editor->setActiveMaskRadiusY(0.28);
  require(outer_editor->applyWorkingSelectionToLayer(adjusted_index) &&
              outer_editor->setPhotoLayerMaskOpacity(adjusted_index, 0.82) &&
              outer_editor->setPhotoLayerMaskDensity(adjusted_index, 0.91),
          "Create and edit a real selection-backed layer mask in the child canvas.");
  settle(*outer_editor, "Layer, text, effect, and mask edits must render in the child.");
  const auto before_paint = recipe(*outer_editor);
  require(outer_editor->addPaintLayer(),
          "Create an editable paint layer in the child canvas.");
  settle(*outer_editor, "The empty child paint layer must be ready for authoring.");
  const auto paint_layer_id = top_layer_id(*outer_editor, QStringLiteral("pixel"));
  const auto paint_index = layer_index(*outer_editor, paint_layer_id);
  require(paint_index == 0 && outer_editor->beginPhotoPaintStroke(
              paint_index, {{QStringLiteral("color"), QStringLiteral("#f0a940")},
                            {QStringLiteral("size"), 13.0},
                            {QStringLiteral("hardness"), 1.0},
                            {QStringLiteral("opacity"), 1.0},
                            {QStringLiteral("flow"), 1.0},
                            {QStringLiteral("pressureSize"), false}}),
          "Start a real child paint stroke through the production controller API.");
  outer_editor->addPhotoPaintPoint(outer_editor->sourceImageWidth() * 0.48,
                                   outer_editor->sourceImageHeight() * 0.51, 1.0);
  require(outer_editor->finishPhotoPaintStroke(true),
          "Commit the real child paint stroke.");
  settle(*outer_editor, "Child paint tiles must finish committing before Apply.");
  const auto after_paint = recipe(*outer_editor);
  const auto painted_layer = layer_by_id(after_paint, paint_layer_id);
  require(!painted_layer.value(QStringLiteral("paintTiles")).toMap()
               .value(QStringLiteral("tiles")).toMap().isEmpty(),
          "The real child stroke must create editable paint tile data.");
  QImage child_canvas(outer_editor->sourceImageWidth(),
                      outer_editor->sourceImageHeight(), QImage::Format_ARGB32);
  child_canvas.setColorSpace(QColorSpace(QColorSpace::SRgb));
  child_canvas.fill(Qt::transparent);
  const auto child_render_before_apply = render(child_canvas, after_paint);
  require(outer_editor->undoLabel().size() > 0,
          "The child edit history must expose an undoable paint commit.");
  restore_history(*outer_editor, false);
  require(layer_by_id(recipe(*outer_editor), paint_layer_id)
                  .value(QStringLiteral("paintTiles")).toMap()
                  .value(QStringLiteral("tiles")).toMap().isEmpty(),
          "Child Undo must remove the committed paint stroke.");
  restore_history(*outer_editor, true);
  require(recipe(*outer_editor).layers == after_paint.layers &&
              recipe(*outer_editor).mask_stack == after_paint.mask_stack,
          "Child Redo must restore the exact editable paint and layer recipe.");
  require(workspace.canApply(),
          "The edited child document must pass workspace Apply validation.");
  settle(root, "The root must remain settled while child edits are private.");
  require(recipe(root) == root_before_cancel &&
              root.editHistoryCurrentIndex() == initial_history &&
              same_pixels(image_provider.image(parent_preview_key), parent_preview),
          "All child edits, including paint and nested Apply, must remain private until outer Apply.");

  const auto history_before_apply = root.editHistoryCurrentIndex();
  const auto child_status_before_apply = outer_editor->status();
  require(workspace.apply(), "Apply the complete outer child canvas document.");
  wait_closed(workspace, "Outer Apply must close the workspace after publication.");
  settle(root, "Root component preview must settle after child Apply.");
  const auto after_apply = recipe(root);
  const auto applied_document = component_document(root, outer_group_id);
  const auto shared_document = component_document(root, shared_instance_ids.at(1));
  require(root.editHistoryCurrentIndex() == history_before_apply + 1 &&
              applied_document == shared_document,
          "Outer Apply must publish shared component contents as exactly one root history step.");
  const auto applied_outer_text = layer_by_id(
      applied_document.value(QStringLiteral("layers")).toList(), outer_text_id);
  const auto applied_nested_component = layer_by_id(
      applied_document.value(QStringLiteral("layers")).toList(), inner_id);
  const auto applied_nested_doc = applied_nested_component
      .value(QStringLiteral("smartObjectDocument")).toMap();
  const auto applied_nested_text = layer_by_id(
      applied_nested_doc.value(QStringLiteral("layers")).toList(), inner_text_id);
  const auto applied_paint = layer_by_id(
      applied_document.value(QStringLiteral("layers")).toList(), paint_layer_id);
  const auto applied_added_source = layer_by_id(
      applied_document.value(QStringLiteral("layers")).toList(), added_source_id);
  const auto added_source_descriptor =
      applied_added_source.value(QStringLiteral("smartObject")).toMap();
  const auto expected_adjustment = layer_by_id(after_paint, adjusted_layer_id);
  const auto applied_adjustment = layer_by_id(
      applied_document.value(QStringLiteral("layers")).toList(), adjusted_layer_id);
  const auto expected_mask_set_id = expected_adjustment
      .value(QStringLiteral("selectionMaskSetId")).toString();
  const auto expected_mask_definition =
      mask_by_set_id(after_paint.mask_stack, expected_mask_set_id, QStringLiteral("rectangle"));
  const auto applied_mask_definition = mask_by_set_id(
      applied_document.value(QStringLiteral("maskStack")).toList(),
      expected_mask_set_id, QStringLiteral("rectangle"));
  const auto outer_text_ok = applied_outer_text.value(QStringLiteral("text")).toString() ==
      QStringLiteral("OUTER WORKSPACE EDIT");
  const auto nested_text_ok = applied_nested_text.value(QStringLiteral("text")).toString() ==
      QStringLiteral("NESTED WORKSPACE EDIT");
  const auto paint_tiles_ok = !applied_paint.value(QStringLiteral("paintTiles")).toMap()
      .value(QStringLiteral("tiles")).toMap().isEmpty();
  const auto applied_paint_tiles = applied_paint.value(QStringLiteral("paintTiles"))
      .toMap().value(QStringLiteral("tiles")).toMap();
  const auto source_path_cleared =
      applied_added_source.value(QStringLiteral("sourcePath")).toString().isEmpty();
  const auto source_policy = added_source_descriptor.value(QStringLiteral("policy")).toString();
  const auto embedded_source_bytes = QByteArray::fromBase64(
      added_source_descriptor.value(QStringLiteral("dataBase64")).toString().toLatin1());
  const auto source_bytes_exact = embedded_source_bytes == added_source_bytes;
  const auto source_embedded_ok = source_path_cleared &&
      source_policy == QStringLiteral("embedded") &&
      source_bytes_exact;
  const auto mask_stack_before = canonical_mask_json(after_paint.mask_stack);
  const auto mask_stack_after = canonical_mask_json(
      applied_document.value(QStringLiteral("maskStack")).toList());
  const auto mask_stack_equal = mask_stack_before == mask_stack_after;
  const auto mask_geometry_ok =
      qAbs(expected_mask_definition.value(QStringLiteral("centerX")).toDouble() - 0.46) < 1e-6 &&
      qAbs(expected_mask_definition.value(QStringLiteral("centerY")).toDouble() - 0.53) < 1e-6 &&
      qAbs(expected_mask_definition.value(QStringLiteral("radiusX")).toDouble() - 0.31) < 1e-6 &&
      qAbs(expected_mask_definition.value(QStringLiteral("radiusY")).toDouble() - 0.28) < 1e-6;
  const auto mask_definition_equal =
      canonical_mask_definition_json(expected_mask_definition) ==
      canonical_mask_definition_json(applied_mask_definition);
  const auto mask_stack_coverage_ok = !expected_mask_definition.isEmpty() &&
      !applied_mask_definition.isEmpty() && mask_stack_equal &&
      mask_geometry_ok && mask_definition_equal;
  const auto mask_reference_ok = !expected_mask_set_id.isEmpty() &&
      applied_adjustment.value(QStringLiteral("selectionMaskSetId")).toString() ==
          expected_mask_set_id &&
      qAbs(applied_adjustment.value(QStringLiteral("maskOpacity")).toDouble() - 0.82) < 1e-6 &&
      qAbs(applied_adjustment.value(QStringLiteral("maskDensity")).toDouble() - 0.91) < 1e-6;
  if (!outer_text_ok || !nested_text_ok || !paint_tiles_ok ||
      !source_embedded_ok || !mask_stack_coverage_ok || !mask_reference_ok) {
    const auto detail = QStringLiteral("Apply field check failed: ") + QStringList{
        QStringLiteral("outerText=%1").arg(outer_text_ok ? QStringLiteral("ok") :
            applied_outer_text.value(QStringLiteral("text")).toString()),
        QStringLiteral("nestedText=%1").arg(nested_text_ok ? QStringLiteral("ok") :
            applied_nested_text.value(QStringLiteral("text")).toString()),
        QStringLiteral("paintTiles=%1/%2").arg(paint_tiles_ok ? QStringLiteral("nonempty") :
            QStringLiteral("empty")).arg(applied_paint_tiles.size()),
        QStringLiteral("sourcePathCleared=%1").arg(source_path_cleared ? QStringLiteral("yes") : QStringLiteral("no")),
        QStringLiteral("descriptorPolicy=%1").arg(source_policy),
        QStringLiteral("sourceBytesExact=%1 size=%2/%3").arg(source_bytes_exact ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(embedded_source_bytes.size()).arg(added_source_bytes.size()),
        QStringLiteral("maskSet=%1 expectedMask=%2 appliedMask=%3")
            .arg(expected_mask_set_id,
                 expected_mask_definition.isEmpty() ? QStringLiteral("missing") : QStringLiteral("present"),
                 applied_mask_definition.isEmpty() ? QStringLiteral("missing") : QStringLiteral("present")),
        QStringLiteral("maskStackEqual=%1 jsonBytes=%2/%3")
            .arg(mask_stack_equal ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(mask_stack_before.size()).arg(mask_stack_after.size()),
        QStringLiteral("maskGeometry=%1 definitionEqual=%2 changedFields=%3")
            .arg(mask_geometry_ok ? QStringLiteral("ok") : QStringLiteral("changed"),
                 mask_definition_equal ? QStringLiteral("yes") : QStringLiteral("no"),
                 changed_mask_fields(expected_mask_definition, applied_mask_definition)),
        QStringLiteral("maskReference=%1 ids=%2/%3 opacity=%4 density=%5")
            .arg(mask_reference_ok ? QStringLiteral("ok") : QStringLiteral("changed"),
                 applied_adjustment.value(QStringLiteral("selectionMaskSetId")).toString(),
                 expected_mask_set_id,
                 QString::number(applied_adjustment.value(QStringLiteral("maskOpacity")).toDouble(), 'g', 6),
                 QString::number(applied_adjustment.value(QStringLiteral("maskDensity")).toDouble(), 'g', 6)),
        QStringLiteral("childStatusBeforeApply=%1").arg(child_status_before_apply)
    }.join(QStringLiteral("; "));
    require(false, qPrintable(detail));
  }
  require(outer_text_ok && nested_text_ok,
          "Apply must retain outer and nested editable text.");
  require(paint_tiles_ok,
          "Apply must retain committed editable paint tile data.");
  require(source_embedded_ok,
          "Apply must embed new source content and remove its external path.");
  require(mask_stack_coverage_ok && mask_reference_ok,
          "Apply must preserve canonical mask coverage, selection references, opacity, and density.");
  const auto applied_pixels = render(base, after_apply);
  auto applied_component_state = after_paint;
  applied_component_state.layers =
      applied_document.value(QStringLiteral("layers")).toList();
  applied_component_state.mask_stack =
      applied_document.value(QStringLiteral("maskStack")).toList();
  const auto applied_component_pixels = render(child_canvas, applied_component_state);
  require(same_pixels(child_render_before_apply, applied_component_pixels),
          "Applied parent component rendering must exactly match the child canvas render, including masks, effects, and paint.");
  require(!same_pixels(applied_pixels, initial_pixels),
          "Applying real child canvas edits must change the root rendered pixels.");

  const auto before_apply_cancel = recipe(root);
  const auto before_apply_cancel_pixels = render(base, before_apply_cancel);
  const auto before_apply_cancel_history = root.editHistoryCurrentIndex();
  require(workspace.open(layer_index(root, outer_group_id)),
          "Open the applied component to check cancellation during asynchronous Apply.");
  wait_until([&workspace]() { return !workspace.busy() && workspace.active(); },
             "Apply-cancel workspace must open.");
  auto* apply_cancel_editor = workspace.controller();
  require(apply_cancel_editor != nullptr,
          "Apply-cancel workspace must expose its temporary child editor.");
  settle(*apply_cancel_editor, "Apply-cancel child canvas must settle before authoring.");
  require(apply_cancel_editor->setPhotoLayerTextById(
              outer_text_id, QStringLiteral("CANCELLED APPLY RACE")) &&
              apply_cancel_editor->addSourceLayer(
                  added_source_path, QStringLiteral("Cancel race source")),
          "Prepare editable text and a source for asynchronous embed-before-Apply.");
  settle(*apply_cancel_editor, "Apply-cancel child source must be loaded before Apply.");
  require(workspace.apply() && workspace.busy(),
          "Apply must queue asynchronous source embedding before the cancellation race.");
  require(workspace.cancel(),
          "Cancel immediately after the asynchronous Apply worker is queued.");
  drain_cancelled_workspace(workspace, root);
  require(recipe(root) == before_apply_cancel &&
              root.editHistoryCurrentIndex() == before_apply_cancel_history &&
              same_pixels(render(base, recipe(root)), before_apply_cancel_pixels),
          "Immediate Apply cancellation must prevent worker callbacks from changing parent recipe, history, or pixels.");

  const auto canonical_patch = root.currentAssetBoundAdjustmentPatchJson();
  const auto decoded = parse_adjustment_patch_json(canonical_patch);
  require(decoded.ok() && decoded.state->layers == after_apply.layers,
          "The root canonical recipe must retain the complete child component tree.");
  const auto export_path = QDir(runtime_path).filePath(QStringLiteral("component-workspace-export.png"));

  restore_history(root, false);
  require(recipe(root).layers == root_before_cancel.layers &&
              same_pixels(render(base, recipe(root)), initial_pixels),
          "Root Undo must restore the pre-Apply component tree and pixels exactly.");
  restore_history(root, true);
  require(recipe(root).layers == after_apply.layers &&
              same_pixels(render(base, recipe(root)), applied_pixels),
          "Root Redo must restore the complete applied child canvas document.");

  const auto shared_sibling_id = shared_instance_ids.at(1) == outer_group_id
      ? shared_instance_ids.at(0) : shared_instance_ids.at(1);
  const auto shared_sibling_index = layer_index(root, shared_sibling_id);
  require(shared_sibling_index >= 0 &&
              root.setPhotoLayerLocked(shared_sibling_index, true),
          "Lock the second shared component placement for the publication gate.");
  settle(root, "The locked shared placement must finish rendering.");
  const auto locked_parent_state = recipe(root);
  const auto locked_parent_history = root.editHistoryCurrentIndex();
  PhotoComponentWorkspaceController locked_workspace(&root);
  require(locked_workspace.open(layer_index(root, outer_group_id)),
          "Opening one shared placement should remain possible when its sibling is locked.");
  wait_until([&locked_workspace]() {
    return !locked_workspace.busy() && locked_workspace.active();
  }, "Locked-sibling workspace must open.");
  auto* locked_editor = locked_workspace.controller();
  require(locked_editor != nullptr,
          "Locked-sibling workspace must expose the isolated child editor.");
  settle(*locked_editor, "Locked-sibling child preview must settle.");
  require(locked_editor->setPhotoLayerTextById(
              outer_text_id, QStringLiteral("LOCKED SHARED DRAFT")),
          "Author a child draft before checking locked shared publication.");
  settle(*locked_editor, "Locked-shared draft preview must settle.");
  require(locked_workspace.apply(),
          "Start asynchronous Apply for the locked shared placement check.");
  wait_apply_rejection(locked_workspace,
                       "Apply must reject a shared document with a locked placement.");
  require(locked_workspace.error().contains(QStringLiteral("locked"),
                                             Qt::CaseInsensitive) &&
              recipe(root) == locked_parent_state &&
              root.editHistoryCurrentIndex() == locked_parent_history &&
              layer_by_id(recipe(*locked_editor), outer_text_id)
                      .value(QStringLiteral("text")).toString() ==
                  QStringLiteral("LOCKED SHARED DRAFT"),
          "A locked shared sibling must reject publication without changing the parent or dropping the child draft.");
  require(locked_workspace.cancel(),
          "Cancel the retained child draft after locked-sibling rejection.");
  wait_closed(locked_workspace,
              "Cancel after locked-sibling rejection must close the workspace.");
  require(root.setPhotoLayerLocked(layer_index(root, shared_sibling_id), false),
          "Unlock the shared sibling after the publication gate assertion.");
  settle(root, "The shared sibling unlock must settle before the stale-baseline test.");

  PhotoComponentWorkspaceController stale_workspace(&root);
  require(stale_workspace.open(layer_index(root, outer_group_id)),
          "Open contents for the parent stale-baseline gate.");
  wait_until([&stale_workspace]() {
    return !stale_workspace.busy() && stale_workspace.active();
  }, "Stale-baseline workspace must open.");
  auto* stale_editor = stale_workspace.controller();
  require(stale_editor != nullptr,
          "Stale-baseline workspace must expose the isolated child editor.");
  settle(*stale_editor, "Stale-baseline child preview must settle.");
  require(stale_editor->setPhotoLayerTextById(
              outer_text_id, QStringLiteral("STALE PARENT DRAFT")),
          "Author a child draft before a direct parent edit.");
  settle(*stale_editor, "Stale-parent child preview must settle before the external edit.");
  const auto second_instance_index = layer_index(root, shared_sibling_id);
  require(second_instance_index >= 0 &&
              root.setPhotoLayerOpacity(second_instance_index, 0.93),
          "The parent controller should accept an external layer edit during isolated authoring.");
  settle(root, "The external parent layer edit must settle before stale Apply.");
  const auto accepted_parent_edit = recipe(root);
  const auto accepted_parent_history = root.editHistoryCurrentIndex();
  require(stale_workspace.apply(),
          "Start Apply so the captured parent baseline is checked asynchronously.");
  wait_apply_rejection(stale_workspace,
                       "Stale parent baseline must reject Apply and retain the child draft.");
  require(stale_workspace.error().contains(QStringLiteral("parent"),
                                            Qt::CaseInsensitive) &&
              recipe(root) == accepted_parent_edit &&
              root.editHistoryCurrentIndex() == accepted_parent_history &&
              layer_by_id(recipe(*stale_editor), outer_text_id)
                      .value(QStringLiteral("text")).toString() ==
                  QStringLiteral("STALE PARENT DRAFT"),
          "Stale Apply must preserve the accepted parent edit and retain the rejected child draft.");
  require(stale_workspace.cancel(),
          "Cancel the child draft after stale-parent rejection.");
  wait_closed(stale_workspace,
              "Cancel after stale-parent rejection must close the workspace.");
  require(recipe(root) == accepted_parent_edit,
          "Cancel after stale-parent rejection must preserve the parent's accepted edit exactly.");

  PhotoComponentWorkspaceController unsupported_workspace(&root);
  require(unsupported_workspace.open(layer_index(root, outer_group_id)),
          "Open contents to verify unsupported global recipe edits are rejected.");
  wait_until([&unsupported_workspace]() {
    return !unsupported_workspace.busy() && unsupported_workspace.active();
  }, "Unsupported-recipe workspace must open.");
  auto* unsupported_editor = unsupported_workspace.controller();
  require(unsupported_editor != nullptr,
          "Unsupported-recipe workspace must expose its child editor.");
  settle(*unsupported_editor, "Unsupported-recipe child editor must finish loading.");
  const auto root_before_unsupported = recipe(root);
  unsupported_editor->setExposure(0.55);
  unsupported_editor->setCropEditing(true);
  unsupported_editor->updateCropRect(0.08, 0.07, 0.84, 0.86);
  settle(*unsupported_editor, "Child global exposure and crop state must settle before validation.");
  require(!unsupported_workspace.apply() &&
              !unsupported_workspace.error().trimmed().isEmpty() &&
              unsupported_workspace.active() &&
              recipe(root) == root_before_unsupported,
          "Unsupported child-global exposure/crop edits must fail Apply with a visible error and leave root content intact.");
  const auto unsupported_layers = recipe(*unsupported_editor).layers;
  require(!unsupported_layers.isEmpty() &&
              layer_by_id(unsupported_layers, outer_text_id)
                  .value(QStringLiteral("text")).toString() ==
                  QStringLiteral("OUTER WORKSPACE EDIT"),
          "Rejecting unsupported global state must not drop the editable child document.");
  require(unsupported_workspace.cancel(),
          "Cancel the workspace after the unsupported global-state rejection.");
  wait_closed(unsupported_workspace,
              "Cancel after unsupported-state rejection must close cleanly.");
  require(recipe(root) == root_before_unsupported,
          "Cancel after rejected global edits must preserve the applied root document.");
  const auto mask_preferences_directory = QDir(
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
      .filePath(QStringLiteral("mask-selection"));
  require(!QDir(mask_preferences_directory).exists(),
          "Transient child mask editing must not create persistent mask-selection preferences.");
  return {root_asset_id, base_path, export_path, canonical_patch, base,
          applied_pixels};
}

void benchmark_large_component_render(const ReopenReceipt& receipt, const QString& output_path) {
  const auto parsed = parse_adjustment_patch_json(receipt.canonical_patch);
  require(parsed.ok(), "Large component workload must reopen its authored recipe.");
  const auto original = *parsed.state;
  QVariantMap component;
  for (const auto& value : original.layers) {
    const auto layer = value.toMap();
    if (layer.contains(QStringLiteral("smartObjectDocument"))) { component = layer; break; }
  }
  require(!component.isEmpty(), "The workload needs its authored nested component.");
  const auto base = receipt.source_image.scaled(1536, 1024);
  QJsonObject report{{"scenario", "authored-nested-component-render-float-v2"},
      {"width", base.width()}, {"height", base.height()},
      {"outputFormat", "RGBA32FPx4"}, {"originalRootLayerCount", original.layers.size()},
      {"scope", "CPU render; exact float bytes; repeated and varied placements; no displayed-frame claim"}};
  QByteArray base_hash;
  struct Workload { const char* key; int extra; bool varied; };
  for (const Workload workload : {Workload{"1", 0, false}, Workload{"12", 11, false},
                                  Workload{"12-varied", 11, true}}) {
    auto state = original;
    for (int index = 0; index < workload.extra; ++index) {
      auto placement = component;
      placement.insert("id", QStringLiteral("large-placement-%1").arg(index));
      placement.insert("smartObjectInstanceId", QStringLiteral("large-instance-%1").arg(index));
      if (workload.varied) {
        placement.insert("x", 128 + (index % 4) * 320);
        placement.insert("y", 96 + (index / 4) * 256);
        placement.insert("scale", 0.65 + 0.05 * (index % 4));
        placement.insert("rotation", (index % 5 - 2) * 9.0);
        placement.insert("opacity", 0.65 + 0.025 * index);
      }
      // Inspector order is top-to-bottom. Keep varied placements visible.
      state.layers.prepend(placement);
    }
    QImage expected;
    QJsonArray samples, compile_times, adjust_times, component_stage_profiles;
    for (int repeat = 0; repeat < 4; ++repeat) {
      begin_photo_render_stage_profile_for_tests();
      QElapsedTimer timer; timer.start();
      const auto image = render_output_image_with_backend(base, state,
          QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA32FPx4,
          RenderBackendPreference::CpuOnly);
      samples.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      const auto stages = end_photo_render_stage_profile_for_tests();
      compile_times.append(static_cast<double>(stages.value("compileExportPipelineStateNs").toLongLong()));
      adjust_times.append(static_cast<double>(stages.value("applyColorAdjustmentsNs").toLongLong()));
      component_stage_profiles.append(QJsonObject::fromVariantMap(stages));
      require(!image.isNull() && image.size() == base.size() &&
          image.format() == QImage::Format_RGBA32FPx4,
          "Large document rendering must retain the requested float canvas.");
      if (expected.isNull()) expected = image;
      require(image.colorSpace() == expected.colorSpace(), "Float profile must stay unchanged.");
      for (int y = 0; y < image.height(); ++y)
        require(std::memcmp(image.constScanLine(y), expected.constScanLine(y),
            static_cast<std::size_t>(image.width()) * 16U) == 0,
            "Repeated large document renders must match every float channel bit.");
    }
    const auto hash = QCryptographicHash::hash(
        QByteArrayView(reinterpret_cast<const char*>(expected.constBits()), expected.sizeInBytes()),
        QCryptographicHash::Sha256).toHex();
    if (workload.extra == 0) base_hash = hash;
    if (workload.varied) require(hash != base_hash, "Varied placements must change visible output.");
    report.insert(QString::fromLatin1(workload.key), QJsonObject{
        {"extraPlacements", workload.extra}, {"render_ms", samples},
        {"compileExportPipelineStateNs", compile_times}, {"applyColorAdjustmentsNs", adjust_times},
        {"componentCompileStageProfiles", component_stage_profiles},
        {"pixelSha256", QString::fromLatin1(hash)}, {"exactRepeatedFloatPixels", true}});
  }
#if defined(Q_OS_UNIX)
  rusage usage{}; getrusage(RUSAGE_SELF, &usage);
#if defined(Q_OS_MACOS)
  constexpr double rss_scale = 1.0;
#else
  constexpr double rss_scale = 1024.0;
#endif
  report.insert("peakRssBytes", static_cast<double>(usage.ru_maxrss) * rss_scale);
#endif
  QFile output(output_path);
  require(output.open(QIODevice::WriteOnly), "Large document report must open.");
  require(output.write(QJsonDocument(report).toJson()) > 0, "Large document report must be written.");
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR",
          runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR",
          runtime.filePath(QStringLiteral("cache")).toUtf8());
  qunsetenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE");
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyComponentWorkflowTests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("PhotoComponentWorkspace-%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
  QStandardPaths::setTestModeEnabled(true);
  QGuiApplication application(argc, argv);

  struct DrainWorkers {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(90000); }
  } drain_workers;

  try {
    const auto receipt = test_component_workspace(runtime.path());
    test_reopened_component_export(receipt);
    if (argc == 3 && QByteArray(argv[1]) == "--benchmark-large-components")
      benchmark_large_component_render(receipt, QString::fromLocal8Bit(argv[2]));
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "photo_component_workspace_workflow_test passed\n";
  return 0;
}
