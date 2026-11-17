#include "app_bootstrap.h"
#include "catalog_recipe_projection.h"
#include "edit_recipe_state.h"
#include "editor_preview_controller.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSettings>
#include <QStringList>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace photo_editor::desktop {

struct EditorPreviewControllerTestAccess {
  static EditRecipeState state(const EditorPreviewController& controller) {
    return controller.currentState();
  }

  static QString journalPath(const EditorPreviewController& controller) {
    return controller.recipe_save_journal_.path();
  }

  static bool idle(const EditorPreviewController& controller) {
    return !controller.recipe_save_queue_.pending() &&
        !controller.recipe_save_queue_.active() &&
        !controller.persist_timer_.isActive() &&
        controller.persist_process_.state() == QProcess::NotRunning &&
        !controller.processing() &&
        !controller.interactive_render_watcher_.isRunning() &&
        !controller.full_render_watcher_.isRunning() &&
        !controller.selectionWorkBusy() &&
        !controller.photoPaintBusy();
  }
};

}  // namespace photo_editor::desktop

namespace {

using photo_editor::desktop::AppBootstrap;
using photo_editor::desktop::EditRecipeState;
using photo_editor::desktop::EditorPreviewController;
using photo_editor::desktop::EditorPreviewControllerTestAccess;
using photo_editor::desktop::project_catalog_adjustments_for_desktop;
using photo_editor::desktop::serialize_edit_recipe_json;

constexpr int kSkip = 77;

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

void require(bool condition, const QString& message) {
  if (!condition) throw std::runtime_error(message.toStdString());
}

QString sha256_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto chunk = file.read(1024 * 1024);
    if (chunk.isEmpty() && file.error() != QFileDevice::NoError) return {};
    hash.addData(chunk);
  }
  return QString::fromLatin1(hash.result().toHex());
}

bool write_file(const QString& path, const QByteArray& bytes) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

bool write_json(const QString& path, const QJsonObject& object) {
  return write_file(path, QJsonDocument(object).toJson(QJsonDocument::Indented));
}

bool isolate_settings_path() {
  const auto path = qEnvironmentVariable("PHOTO_EDITOR_PREFERENCES_DIR").trimmed();
  if (path.isEmpty() || !QDir().mkpath(path)) return false;
  const auto canonical_path = QFileInfo(path).canonicalFilePath();
  if (canonical_path.isEmpty()) return false;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, canonical_path);
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, canonical_path);
  return true;
}

QJsonObject read_json(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(file.readAll()).object();
}

QString run_command(const QString& program, const QStringList& arguments, bool* ok,
                    int timeout_ms = 60000) {
  QProcess process;
  process.start(program, arguments);
  *ok = process.waitForFinished(timeout_ms) &&
      process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
  const auto standard_output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  if (!*ok) {
    std::cerr << program.toStdString() << " failed: "
              << QString::fromUtf8(process.readAllStandardError()).toStdString() << '\n';
  }
  return standard_output;
}

class PrivatePostgres final {
 public:
  PrivatePostgres(QString bin, QString data, QString log, QString socket_directory)
      : bin_(std::move(bin)), data_(std::move(data)), log_(std::move(log)),
        socket_directory_(std::move(socket_directory)) {}

  bool start(QString* database_url) {
    bool ok = false;
    const auto initialize_output = run_command(
        QDir(bin_).filePath(QStringLiteral("initdb")),
        {QStringLiteral("-D"), data_, QStringLiteral("-U"), QStringLiteral("looksy_test"),
         QStringLiteral("-A"), QStringLiteral("trust"), QStringLiteral("--no-locale"),
         QStringLiteral("-E"), QStringLiteral("UTF8")},
        &ok);
    if (!ok) {
      std::cerr << initialize_output.toStdString() << '\n';
      return false;
    }

    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0)) return false;
    const auto port = reservation.serverPort();
    reservation.close();
    const auto start_output = run_command(
        QDir(bin_).filePath(QStringLiteral("pg_ctl")),
        {QStringLiteral("-D"), data_, QStringLiteral("-l"), log_, QStringLiteral("-o"),
         QStringLiteral("-h 127.0.0.1 -p %1 -k %2").arg(port).arg(socket_directory_),
         QStringLiteral("-w"), QStringLiteral("start")},
        &ok);
    if (!ok) {
      std::cerr << start_output.toStdString() << '\n';
      return false;
    }
    running_ = true;
    *database_url = QStringLiteral("postgresql://looksy_test@127.0.0.1:%1/postgres").arg(port);
    return true;
  }

  ~PrivatePostgres() {
    if (!running_) return;
    bool stopped = false;
    (void)run_command(QDir(bin_).filePath(QStringLiteral("pg_ctl")),
                      {QStringLiteral("-D"), data_, QStringLiteral("-m"),
                       QStringLiteral("fast"), QStringLiteral("-w"),
                       QStringLiteral("stop")}, &stopped, 30000);
    if (!stopped) {
      std::cerr << "Could not normally stop private PostgreSQL at "
                << data_.toStdString() << '\n';
    }
  }

 private:
  QString bin_;
  QString data_;
  QString log_;
  QString socket_directory_;
  bool running_ = false;
};

bool wait_controller_idle(EditorPreviewController& controller, int timeout_ms = 30000) {
  QElapsedTimer timer;
  timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (EditorPreviewControllerTestAccess::idle(controller) &&
        !controller.recipeSavePending()) {
      return true;
    }
    QThread::msleep(5);
  } while (timer.elapsed() < timeout_ms);
  std::cerr << "Controller did not settle: " << controller.status().toStdString()
            << " save_error=" << controller.recipeSaveError().toStdString() << '\n';
  return false;
}

int layer_index(const EditRecipeState& state, const QString& id) {
  for (int index = 0; index < state.layers.size(); ++index) {
    if (state.layers.at(index).toMap().value(QStringLiteral("id")).toString() == id) {
      return index;
    }
  }
  return -1;
}

QVariantMap layer_by_id(const EditRecipeState& state, const QString& id) {
  const auto index = layer_index(state, id);
  return index < 0 ? QVariantMap{} : state.layers.at(index).toMap();
}

QString primary_mask_id(const QVariantMap& layer) {
  auto id = layer.value(QStringLiteral("selectionMaskSetId")).toString();
  if (id.isEmpty()) {
    const auto ids = layer.value(QStringLiteral("selectionMaskSetIds")).toList();
    if (!ids.isEmpty()) id = ids.first().toString();
  }
  return id;
}

QStringList primary_mask_ids(const QVariantMap& layer) {
  auto ids = QStringList{};
  for (const auto& value : layer.value(QStringLiteral("selectionMaskSetIds")).toList()) {
    const auto id = value.toString();
    if (!id.isEmpty()) ids.push_back(id);
  }
  if (ids.isEmpty()) {
    const auto id = primary_mask_id(layer);
    if (!id.isEmpty()) ids.push_back(id);
  }
  return ids;
}

QStringList intersection_mask_ids(const QVariantMap& layer) {
  auto ids = QStringList{};
  for (const auto& value : layer.value(QStringLiteral("selectionIntersectionMaskSetIds")).toList()) {
    const auto id = value.toString();
    if (!id.isEmpty()) ids.push_back(id);
  }
  return ids;
}

QStringList layer_order(const EditRecipeState& state) {
  QStringList result;
  for (const auto& value : state.layers) {
    const auto id = value.toMap().value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) result.push_back(id);
  }
  return result;
}

bool export_image(EditorPreviewController& controller, const QString& path,
                  QImage* image) {
  if (!controller.exportFlattenedPhoto(path)) return false;
  *image = QImage(path);
  return !image->isNull();
}

bool export_changes_pixel(const QString& path_a, const QString& path_b,
                          const QPoint& point) {
  const QImage first(path_a);
  const QImage second(path_b);
  return !first.isNull() && first.size() == second.size() &&
      first.pixelColor(point) != second.pixelColor(point);
}

bool export_preserves_pixel(const QString& path_a, const QString& path_b,
                            const QPoint& point) {
  const QImage first(path_a);
  const QImage second(path_b);
  return !first.isNull() && first.size() == second.size() &&
      first.pixelColor(point) == second.pixelColor(point);
}

int author_process(int argc, char** argv) {
  if (argc != 7) return fail(QStringLiteral("author mode arguments are incomplete"));
  const auto asset_id = QString::fromLocal8Bit(argv[2]);
  const auto source_path = QString::fromLocal8Bit(argv[3]);
  const auto app_data_root = qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR");
  const auto expected_recipe_path = QString::fromLocal8Bit(argv[4]);
  const auto export_path = QString::fromLocal8Bit(argv[5]);
  const auto receipt_path = QString::fromLocal8Bit(argv[6]);
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE") ||
      app_data_root.isEmpty() || asset_id.isEmpty() || source_path.isEmpty()) {
    return fail(QStringLiteral("author mode requires production recipe persistence and private runtime paths"));
  }

  try {
    EditorPreviewController controller;
    controller.loadAsset(asset_id, source_path, {}, {}, {}, {}, {});
    require(wait_controller_idle(controller), QStringLiteral("Initial real-photo decode should settle"));
    const auto width = controller.sourceImageWidth();
    const auto height = controller.sourceImageHeight();
    require(width >= 64 && height >= 64, QStringLiteral("The real photo must decode at usable dimensions"));
    const QPoint inside(width / 2, height / 2);
    const QPoint outside(std::max(1, width / 20), height / 2);
    const QPoint sibling_sample(static_cast<int>(static_cast<double>(width) * 0.9),
                                static_cast<int>(static_cast<double>(height) * 0.9));

    require(controller.addPaintLayer(), QStringLiteral("Create an untouched sibling paint layer"));
    auto state = EditorPreviewControllerTestAccess::state(controller);
    const auto sibling_layer_id = state.layers.first().toMap().value(QStringLiteral("id")).toString();
    QVariantMap sibling_paint_settings{
        {QStringLiteral("color"), QStringLiteral("#16c777")},
        {QStringLiteral("size"), std::max(18.0, static_cast<double>(std::min(width, height)) * 0.05)},
        {QStringLiteral("hardness"), 1.0},
        {QStringLiteral("opacity"), 1.0},
        {QStringLiteral("flow"), 1.0},
        {QStringLiteral("pressureSize"), false},
    };
    require(!sibling_layer_id.isEmpty() &&
                controller.beginPhotoPaintStroke(0, sibling_paint_settings),
            QStringLiteral("Paint a visible control mark on the untouched sibling layer"));
    controller.addPhotoPaintPoint(sibling_sample.x(), sibling_sample.y());
    require(controller.finishPhotoPaintStroke() && wait_controller_idle(controller),
            QStringLiteral("Commit sibling control mark"));
    const auto sibling_tiles = layer_by_id(EditorPreviewControllerTestAccess::state(controller),
                                           sibling_layer_id).value(QStringLiteral("paintTiles"));
    require(!sibling_tiles.isNull(), QStringLiteral("Sibling layer should contain its control paint"));

    controller.addMask(QStringLiteral("rectangle"));
    controller.setActiveMaskFeather(0.0);
    controller.setActiveMaskCenterX(0.5);
    controller.setActiveMaskCenterY(0.5);
    controller.setActiveMaskRadiusX(0.23);
    controller.setActiveMaskRadiusY(0.24);
    require(wait_controller_idle(controller), QStringLiteral("Working selection should settle"));
    const auto working_set_id = controller.workingSelectionSetId();
    require(!working_set_id.isEmpty() && controller.saveWorkingSelection(QStringLiteral("Relaunch selection")),
            QStringLiteral("Save the independent working selection"));

    require(controller.addPaintLayer(), QStringLiteral("Create the selection-clipped paint layer"));
    state = EditorPreviewControllerTestAccess::state(controller);
    const auto paint_layer_id = state.layers.first().toMap().value(QStringLiteral("id")).toString();
    require(!paint_layer_id.isEmpty() && controller.applyWorkingSelectionToLayer(0),
            QStringLiteral("Attach a cloned selection mask to the paint layer"));
    require(wait_controller_idle(controller), QStringLiteral("Independent paint mask should settle"));
    state = EditorPreviewControllerTestAccess::state(controller);
    const auto paint_mask_id = primary_mask_id(layer_by_id(state, paint_layer_id));
    require(!paint_mask_id.isEmpty() && paint_mask_id != working_set_id,
            QStringLiteral("Paint layer mask must have an independent canonical ID"));

    const auto before_paint_path = QFileInfo(export_path).dir().filePath(QStringLiteral("before-paint.png"));
    const auto painted_path = QFileInfo(export_path).dir().filePath(QStringLiteral("painted.png"));
    const auto erased_path = QFileInfo(export_path).dir().filePath(QStringLiteral("erased.png"));
    const auto after_adjustment_path = QFileInfo(export_path).dir().filePath(QStringLiteral("masked-adjustment.png"));
    QImage before_paint;
    require(export_image(controller, before_paint_path, &before_paint),
            QStringLiteral("Export the selection-clipping baseline"));
    const auto sibling_pixel = before_paint.pixelColor(sibling_sample);
    require(sibling_pixel.green() > sibling_pixel.red() + 80,
            QStringLiteral("Sibling control mark must be visible at its own sample"));
    QVariantMap paint_settings{
        {QStringLiteral("color"), QStringLiteral("#e82658")},
        {QStringLiteral("size"), std::max(24.0, static_cast<double>(std::min(width, height)) * 0.075)},
        {QStringLiteral("hardness"), 1.0},
        {QStringLiteral("opacity"), 1.0},
        {QStringLiteral("flow"), 1.0},
        {QStringLiteral("pressureSize"), false},
    };
    require(controller.beginPhotoPaintStroke(0, paint_settings), QStringLiteral("Begin selection-clipped paint"));
    for (int step = 0; step <= 8; ++step) {
      controller.addPhotoPaintPoint(static_cast<double>(width) * (0.12 + 0.095 * step),
                                    static_cast<double>(height) * 0.5);
    }
    require(controller.finishPhotoPaintStroke() && wait_controller_idle(controller),
            QStringLiteral("Commit selection-clipped paint"));
    QImage painted;
    require(export_image(controller, painted_path, &painted) &&
                export_changes_pixel(before_paint_path, painted_path, inside) &&
                export_preserves_pixel(before_paint_path, painted_path, outside) &&
                export_preserves_pixel(before_paint_path, painted_path, sibling_sample),
            QStringLiteral("Paint must change selected pixels while preserving outside and sibling pixels"));

    paint_settings.insert(QStringLiteral("erase"), true);
    require(controller.beginPhotoPaintStroke(0, paint_settings), QStringLiteral("Begin selection-clipped erase"));
    for (int step = 0; step <= 8; ++step) {
      controller.addPhotoPaintPoint(static_cast<double>(width) * (0.12 + 0.095 * step),
                                    static_cast<double>(height) * 0.5);
    }
    require(controller.finishPhotoPaintStroke() && wait_controller_idle(controller),
            QStringLiteral("Commit selection-clipped erase"));
    QImage erased;
    require(export_image(controller, erased_path, &erased) &&
                export_changes_pixel(painted_path, erased_path, inside) &&
                export_preserves_pixel(before_paint_path, erased_path, outside) &&
                export_preserves_pixel(before_paint_path, erased_path, sibling_sample),
            QStringLiteral("Erase must affect selected paint and leave outside and sibling pixels exact"));

    require(controller.addAdjustmentLayer(QStringLiteral("Masked exposure")),
            QStringLiteral("Create the masked adjustment layer"));
    state = EditorPreviewControllerTestAccess::state(controller);
    const auto adjustment_layer_id = state.layers.first().toMap().value(QStringLiteral("id")).toString();
    require(!adjustment_layer_id.isEmpty() && controller.applyWorkingSelectionToLayer(0),
            QStringLiteral("Attach an independent mask to the adjustment layer"));
    require(controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), 0.8) &&
                wait_controller_idle(controller),
            QStringLiteral("Settle the masked exposure adjustment"));
    state = EditorPreviewControllerTestAccess::state(controller);
    const auto adjustment_mask_id = primary_mask_id(layer_by_id(state, adjustment_layer_id));
    require(!adjustment_mask_id.isEmpty() && adjustment_mask_id != working_set_id &&
                adjustment_mask_id != paint_mask_id,
            QStringLiteral("Each layer attachment must retain a distinct canonical mask identity"));
    QImage after_adjustment;
    require(export_image(controller, after_adjustment_path, &after_adjustment) &&
                export_changes_pixel(erased_path, after_adjustment_path, inside) &&
                export_preserves_pixel(erased_path, after_adjustment_path, outside) &&
                export_preserves_pixel(erased_path, after_adjustment_path, sibling_sample),
            QStringLiteral("Masked exposure must change the selected region and preserve outside/sibling pixels"));

    require(controller.addAdjustmentLayer(QStringLiteral("Mask transfer destination")),
            QStringLiteral("Create a neutral destination for mask movement"));
    state = EditorPreviewControllerTestAccess::state(controller);
    const auto destination_layer_id = state.layers.first().toMap().value(QStringLiteral("id")).toString();
    require(!destination_layer_id.isEmpty(), QStringLiteral("Mask move destination needs a stable layer ID"));
    const auto before_move_state = EditorPreviewControllerTestAccess::state(controller);
    QImage before_move_image;
    const auto before_move_path = QFileInfo(export_path).dir().filePath(QStringLiteral("before-mask-move.png"));
    require(export_image(controller, before_move_path, &before_move_image),
            QStringLiteral("Export before moving the mask"));
    require(controller.transferLayerMask(adjustment_layer_id, adjustment_mask_id,
                                         destination_layer_id, false) &&
                wait_controller_idle(controller),
            QStringLiteral("Move the adjustment mask through the production controller"));
    const auto moved_state = EditorPreviewControllerTestAccess::state(controller);
    require(primary_mask_id(layer_by_id(moved_state, adjustment_layer_id)).isEmpty() &&
                primary_mask_id(layer_by_id(moved_state, destination_layer_id)) == adjustment_mask_id,
            QStringLiteral("Moving a mask must preserve its ID and change only its owner"));
    const auto moved_path = QFileInfo(export_path).dir().filePath(QStringLiteral("mask-moved.png"));
    QImage moved_image;
    require(export_image(controller, moved_path, &moved_image) &&
                export_changes_pixel(before_move_path, moved_path, outside),
            QStringLiteral("Removing the mask from the non-neutral adjustment must change an outside pixel"));
    controller.undo();
    require(wait_controller_idle(controller) &&
                serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(controller)) ==
                    serialize_edit_recipe_json(before_move_state),
            QStringLiteral("One undo must restore the exact mask move state"));
    QImage undone_move_image;
    const auto undone_move_path = QFileInfo(export_path).dir().filePath(QStringLiteral("mask-move-undo.png"));
    require(export_image(controller, undone_move_path, &undone_move_image) &&
                undone_move_image == before_move_image,
            QStringLiteral("Undoing mask movement must restore exact export pixels"));
    controller.redo();
    require(wait_controller_idle(controller) &&
                serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(controller)) ==
                    serialize_edit_recipe_json(moved_state),
            QStringLiteral("One redo must restore the exact mask move state"));
    QImage redone_move_image;
    const auto redone_move_path = QFileInfo(export_path).dir().filePath(QStringLiteral("mask-move-redo.png"));
    require(export_image(controller, redone_move_path, &redone_move_image) &&
                redone_move_image == moved_image,
            QStringLiteral("Redoing mask movement must restore exact export pixels"));

    require(controller.transferLayerMask(destination_layer_id, adjustment_mask_id,
                                         adjustment_layer_id, true) &&
                wait_controller_idle(controller),
            QStringLiteral("Copy the moved mask back as an independent adjustment attachment"));
    state = EditorPreviewControllerTestAccess::state(controller);
    const auto copied_adjustment_mask_id = primary_mask_id(layer_by_id(state, adjustment_layer_id));
    require(!copied_adjustment_mask_id.isEmpty() && copied_adjustment_mask_id != adjustment_mask_id &&
                primary_mask_id(layer_by_id(state, destination_layer_id)) == adjustment_mask_id,
            QStringLiteral("Copied mask must have a new ID while the original remains on its destination"));
    const auto copied_mask_path = QFileInfo(export_path).dir().filePath(QStringLiteral("mask-copied.png"));
    QImage copied_mask_image;
    require(export_image(controller, copied_mask_path, &copied_mask_image) &&
                copied_mask_image == after_adjustment &&
                export_preserves_pixel(after_adjustment_path, copied_mask_path, sibling_sample),
            QStringLiteral("Copied mask coverage must reproduce the original masked adjustment pixels"));

    const auto adjustment_index = layer_index(state, adjustment_layer_id);
    const auto adjustment_before_duplicate = layer_by_id(state, adjustment_layer_id);
    const auto primary_ids_before_duplicate = primary_mask_ids(adjustment_before_duplicate);
    const auto intersection_ids_before_duplicate = intersection_mask_ids(adjustment_before_duplicate);
    const auto duplicate_accepted = adjustment_index >= 0 &&
        controller.duplicatePhotoLayerWithMasks(adjustment_index);
    require(duplicate_accepted && wait_controller_idle(controller),
            QStringLiteral("Duplicate the masked adjustment while preserving its linked selections"));
    state = EditorPreviewControllerTestAccess::state(controller);
    QString duplicate_layer_id;
    auto duplicate_index = -1;
    for (auto index = 0; index < state.layers.size(); ++index) {
      const auto value = state.layers.at(index);
      const auto row = value.toMap();
      const auto id = row.value(QStringLiteral("id")).toString();
      if (id.startsWith(adjustment_layer_id + QStringLiteral("-copy-")) &&
          row.value(QStringLiteral("duplicateMaskPolicy")).toString() == QStringLiteral("with-masks")) {
        duplicate_layer_id = id;
        duplicate_index = index;
        break;
      }
    }
    const auto duplicate_layer = layer_by_id(state, duplicate_layer_id);
    QStringList layer_diagnostics;
    for (auto index = 0; index < state.layers.size(); ++index) {
      const auto row = state.layers.at(index).toMap();
      layer_diagnostics.push_back(
          QStringLiteral("%1:%2[%3] kind=%4 primary=%5 intersections=%6 policy=%7")
              .arg(index)
              .arg(row.value(QStringLiteral("id")).toString())
              .arg(row.value(QStringLiteral("name")).toString())
              .arg(row.value(QStringLiteral("kind")).toString())
              .arg(primary_mask_ids(row).join(QStringLiteral(",")))
              .arg(intersection_mask_ids(row).join(QStringLiteral(",")))
              .arg(row.value(QStringLiteral("duplicateMaskPolicy")).toString()));
    }
    const auto duplicate_mask_links_match = !duplicate_layer_id.isEmpty() &&
        primary_mask_ids(duplicate_layer) == primary_ids_before_duplicate &&
        intersection_mask_ids(duplicate_layer) == intersection_ids_before_duplicate;
    require(duplicate_mask_links_match,
            QStringLiteral("With-masks duplication must create a new row that shares the source's exact mask IDs; source index=%1 id=%2 primary=%3 intersections=%4 duplicate index=%5 id=%6 primary=%7 intersections=%8 rows=[%9]")
                .arg(adjustment_index)
                .arg(adjustment_layer_id)
                .arg(primary_ids_before_duplicate.join(QStringLiteral(",")))
                .arg(intersection_ids_before_duplicate.join(QStringLiteral(",")))
                .arg(duplicate_index)
                .arg(duplicate_layer_id)
                .arg(primary_mask_ids(duplicate_layer).join(QStringLiteral(",")))
                .arg(intersection_mask_ids(duplicate_layer).join(QStringLiteral(",")))
                .arg(layer_diagnostics.join(QStringLiteral("; "))));
    const auto before_layer_move_state = state;
    const auto before_layer_move_order = layer_order(before_layer_move_state);
    const auto before_layer_move_path = QFileInfo(export_path).dir().filePath(QStringLiteral("before-layer-move.png"));
    QImage before_layer_move_image;
    require(export_image(controller, before_layer_move_path, &before_layer_move_image),
            QStringLiteral("Export before reordering the duplicate"));
    duplicate_index = layer_index(state, duplicate_layer_id);
    const auto layer_move_offset = duplicate_index == 0 ? 1 : -1;
    require(duplicate_index >= 0 && controller.movePhotoLayer(duplicate_index, layer_move_offset) &&
                wait_controller_idle(controller),
            QStringLiteral("Move the duplicated masked adjustment layer"));
    const auto moved_layer_state = EditorPreviewControllerTestAccess::state(controller);
    require(layer_order(moved_layer_state) != before_layer_move_order,
            QStringLiteral("Layer movement must change the authored stack order"));
    const auto moved_layer_path = QFileInfo(export_path).dir().filePath(QStringLiteral("layer-moved.png"));
    QImage moved_layer_image;
    require(export_image(controller, moved_layer_path, &moved_layer_image) &&
                moved_layer_image == before_layer_move_image &&
                export_preserves_pixel(after_adjustment_path, moved_layer_path, sibling_sample),
            QStringLiteral("Reordering identical masked adjustments must preserve their composite pixels"));
    controller.undo();
    require(wait_controller_idle(controller) &&
                serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(controller)) ==
                    serialize_edit_recipe_json(before_layer_move_state),
            QStringLiteral("Undo must restore exact layer order and authored state"));
    QImage undone_layer_image;
    const auto undone_layer_path = QFileInfo(export_path).dir().filePath(QStringLiteral("layer-move-undo.png"));
    require(export_image(controller, undone_layer_path, &undone_layer_image) &&
                undone_layer_image == before_layer_move_image,
            QStringLiteral("Undo of layer movement must preserve exact exported pixels"));
    controller.redo();
    require(wait_controller_idle(controller) &&
                serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(controller)) ==
                    serialize_edit_recipe_json(moved_layer_state),
            QStringLiteral("Redo must restore exact duplicate layer order and IDs"));
    QImage redone_layer_image;
    const auto redone_layer_path = QFileInfo(export_path).dir().filePath(QStringLiteral("layer-move-redo.png"));
    require(export_image(controller, redone_layer_path, &redone_layer_image) &&
                redone_layer_image == moved_layer_image,
            QStringLiteral("Redo of layer movement must restore exact exported pixels"));

    const auto final_state = EditorPreviewControllerTestAccess::state(controller);
    require(layer_by_id(final_state, sibling_layer_id).value(QStringLiteral("paintTiles")) == sibling_tiles,
            QStringLiteral("Sibling paint content must remain exactly unchanged by target authoring"));
    require(wait_controller_idle(controller) && !controller.recipeSavePending() &&
                controller.recipeSaveError().isEmpty(),
            QStringLiteral("Production recipe journal and PostgreSQL save must be idle without errors"));
    const auto journal_path = EditorPreviewControllerTestAccess::journalPath(controller);
    require(!journal_path.isEmpty() &&
                QFileInfo(journal_path).absoluteFilePath().startsWith(QDir(app_data_root).absolutePath()),
            QStringLiteral("Recipe journal must stay inside the isolated app-data directory"));
    QString close_error;
    require(controller.prepareRecipeClose(&close_error),
            QStringLiteral("Normal editor shutdown should durably checkpoint this recipe: %1").arg(close_error));
    require(wait_controller_idle(controller), QStringLiteral("Final recipe checkpoint should settle"));
    require(write_file(expected_recipe_path, serialize_edit_recipe_json(final_state).toUtf8()),
            QStringLiteral("Persist expected authored recipe outside the private app-data root"));
    QImage final_export;
    require(export_image(controller, export_path, &final_export),
            QStringLiteral("Export the fully authored real-photo document"));
    const auto recipe_hash = QString::fromLatin1(QCryptographicHash::hash(
        serialize_edit_recipe_json(final_state).toUtf8(), QCryptographicHash::Sha256).toHex());
    const auto receipt = QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("looksy-mask-layer-author-process:v1")},
        {QStringLiteral("status"), QStringLiteral("passed")},
        {QStringLiteral("processId"), QCoreApplication::applicationPid()},
        {QStringLiteral("proofDir"), QFileInfo(receipt_path).absolutePath()},
        {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("appDataRoot"), app_data_root},
        {QStringLiteral("cacheRoot"), qEnvironmentVariable("PHOTO_EDITOR_CACHE_DIR")},
        {QStringLiteral("sourceSha256"), sha256_file(source_path)},
        {QStringLiteral("databaseUrlSha256"), QString::fromLatin1(QCryptographicHash::hash(
            qEnvironmentVariable("PHOTO_EDITOR_DATABASE_URL").toUtf8(), QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("recipeSha256"), recipe_hash},
        {QStringLiteral("exportSha256"), sha256_file(export_path)},
        {QStringLiteral("exportBytes"), QFileInfo(export_path).size()},
        {QStringLiteral("workingSelectionSetId"), working_set_id},
        {QStringLiteral("paintLayerId"), paint_layer_id},
        {QStringLiteral("paintMaskSetId"), paint_mask_id},
        {QStringLiteral("adjustmentLayerId"), adjustment_layer_id},
        {QStringLiteral("adjustmentMaskSetId"), copied_adjustment_mask_id},
        {QStringLiteral("maskTransferDestinationId"), destination_layer_id},
        {QStringLiteral("movedMaskSetId"), adjustment_mask_id},
        {QStringLiteral("duplicateLayerId"), duplicate_layer_id},
        {QStringLiteral("layerDuplicateMaskPolicy"), QStringLiteral("with-masks-shared-links")},
        {QStringLiteral("journalPath"), journal_path},
        {QStringLiteral("paintClippedInside"), true},
        {QStringLiteral("eraseClippedInside"), true},
        {QStringLiteral("siblingPixelsUnchanged"), true},
        {QStringLiteral("maskedAdjustmentClippedOutside"), true},
        {QStringLiteral("maskMoveUndoRedoExact"), true},
        {QStringLiteral("maskCopyIndependent"), true},
        {QStringLiteral("layerMoveUndoRedoPixelExact"), true},
    };
    require(write_json(receipt_path, receipt), QStringLiteral("Write author process receipt"));
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    return fail(QString::fromUtf8(error.what()));
  }
}

int reopen_process(int argc, char** argv) {
  if (argc != 7) return fail(QStringLiteral("reopen mode arguments are incomplete"));
  const auto asset_id = QString::fromLocal8Bit(argv[2]);
  const auto source_path = QString::fromLocal8Bit(argv[3]);
  const auto expected_recipe_path = QString::fromLocal8Bit(argv[4]);
  const auto expected_export_path = QString::fromLocal8Bit(argv[5]);
  const auto export_path = QString::fromLocal8Bit(argv[6]);
  const auto author_receipt = read_json(QFileInfo(export_path).dir().filePath(QStringLiteral("author.json")));
  if (author_receipt.value(QStringLiteral("status")).toString() != QStringLiteral("passed")) {
    return fail(QStringLiteral("reopen mode needs the successful author-process receipt"));
  }

  try {
    bool ok = false;
    const auto postgres_bin = qEnvironmentVariable("PHOTO_EDITOR_POSTGRES_BIN_DIR");
    require(!postgres_bin.isEmpty(), QStringLiteral("Reopen process needs the isolated PostgreSQL tools"));
    const auto database_url = qEnvironmentVariable("PHOTO_EDITOR_DATABASE_URL");
    require(database_url.startsWith(QStringLiteral("postgres")),
            QStringLiteral("Reopen process must use the isolated PostgreSQL catalog"));
    const auto payload = run_command(
        QDir(postgres_bin).filePath(QStringLiteral("psql")),
        {database_url, QStringLiteral("-X"), QStringLiteral("-A"), QStringLiteral("-t"),
         QStringLiteral("-v"), QStringLiteral("ON_ERROR_STOP=1"), QStringLiteral("-c"),
         QStringLiteral("SELECT json_build_object('legacy',e.recipe_json,'canonical',json_build_object(") +
             QStringLiteral("'recipe_payload',c.recipe_payload,'cache_key',c.cache_key,'provenance_payload',c.provenance_payload::text)) ") +
             QStringLiteral("FROM edit_recipes e JOIN canonical_edit_recipes_v13 c USING(asset_id) WHERE e.asset_id='%1'").arg(asset_id)},
        &ok);
    require(ok && !payload.isEmpty(), QStringLiteral("Catalog must contain the saved legacy and canonical recipe"));
    const auto object = QJsonDocument::fromJson(payload.toUtf8()).object();
    const auto projection = project_catalog_adjustments_for_desktop(
        object.value(QStringLiteral("legacy")), object.value(QStringLiteral("canonical")),
        QDir(qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR")).filePath(QStringLiteral("recipe-assets")));
    require(projection.ok(), QStringLiteral("Canonical recipe projection should reopen: %1").arg(projection.error));

    QFile expected_file(expected_recipe_path);
    require(expected_file.open(QIODevice::ReadOnly), QStringLiteral("Expected recipe receipt should be readable"));
    const auto expected_json = QString::fromUtf8(expected_file.readAll());
    const auto expected_state = photo_editor::desktop::parse_edit_recipe_json(expected_json);
    require(serialize_edit_recipe_json(expected_state) == expected_json,
            QStringLiteral("Saved expected recipe should remain canonical JSON"));

    EditorPreviewController controller;
    controller.loadAsset(asset_id, source_path, {}, {}, {}, {}, projection.adjustment_patch_json);
    require(wait_controller_idle(controller), QStringLiteral("Fresh controller should settle after catalog reopen"));
    const auto reopened_state = EditorPreviewControllerTestAccess::state(controller);
    const auto reopened_json = serialize_edit_recipe_json(reopened_state);
    require(reopened_json == expected_json,
            QStringLiteral("Fresh process must restore every layer/mask ID and authored recipe field"));
    const auto author_recipe_hash = author_receipt.value(QStringLiteral("recipeSha256")).toString();
    const auto reopened_recipe_hash = QString::fromLatin1(QCryptographicHash::hash(
        reopened_json.toUtf8(), QCryptographicHash::Sha256).toHex());
    require(!author_recipe_hash.isEmpty() && reopened_recipe_hash == author_recipe_hash,
            QStringLiteral("Fresh process must restore the exact authored recipe hash"));

    QImage reopened_export;
    require(export_image(controller, export_path, &reopened_export),
            QStringLiteral("Fresh process should export the restored real-photo recipe"));
    const QImage author_export(expected_export_path);
    require(!author_export.isNull() && author_export == reopened_export,
            QStringLiteral("Fresh-process export pixels must match the authored export exactly"));
    const auto reopened_export_hash = sha256_file(export_path);
    const auto author_export_hash = author_receipt.value(QStringLiteral("exportSha256")).toString();
    require(!author_export_hash.isEmpty() && reopened_export_hash == author_export_hash,
            QStringLiteral("Fresh-process exported artifact bytes must match exactly"));
    require(controller.recipeSaveError().isEmpty(), QStringLiteral("Reopen must not report a recipe save error"));

    const auto receipt = QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("looksy-mask-layer-reopen-process:v1")},
        {QStringLiteral("status"), QStringLiteral("passed")},
        {QStringLiteral("processId"), QCoreApplication::applicationPid()},
        {QStringLiteral("proofDir"), QFileInfo(export_path).absolutePath()},
        {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("appDataRoot"), qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR")},
        {QStringLiteral("cacheRoot"), qEnvironmentVariable("PHOTO_EDITOR_CACHE_DIR")},
        {QStringLiteral("databaseUrlSha256"), QString::fromLatin1(QCryptographicHash::hash(
            database_url.toUtf8(), QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("recipeSha256"), reopened_recipe_hash},
        {QStringLiteral("exportSha256"), reopened_export_hash},
        {QStringLiteral("exportBytes"), QFileInfo(export_path).size()},
        {QStringLiteral("layerDuplicateMaskPolicy"),
         author_receipt.value(QStringLiteral("layerDuplicateMaskPolicy"))},
        {QStringLiteral("canonicalProjectionLoaded"), true},
        {QStringLiteral("recipeIdsRestoredExact"), true},
        {QStringLiteral("exportPixelsExact"), true},
        {QStringLiteral("exportBytesExact"), true},
    };
    const auto receipt_path = QFileInfo(export_path).dir().filePath(QStringLiteral("reopen.json"));
    require(write_json(receipt_path, receipt), QStringLiteral("Write reopen process receipt"));
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    return fail(QString::fromUtf8(error.what()));
  }
}

int parent_process() {
#ifndef PHOTO_EDITOR_SOURCE_DIR
  return fail(QStringLiteral("PHOTO_EDITOR_SOURCE_DIR must identify the tracked photo fixture"));
#endif
  auto source_path = qEnvironmentVariable("PHOTO_EDITOR_MASK_LAYER_REAL_PHOTO_SOURCE").trimmed();
  if (source_path.isEmpty()) {
    source_path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(
        QStringLiteral("tests/fixtures/creative-tools/landscape-mountain-source.jpg"));
  }
  const QFileInfo source_info(source_path);
  if (!source_info.isFile() || source_info.isSymLink()) {
    return fail(QStringLiteral("Real-photo source must be a readable regular file: %1").arg(source_path));
  }
  const auto original_source_hash = sha256_file(source_path);
  if (original_source_hash.isEmpty()) return fail(QStringLiteral("Could not hash the real-photo source"));

  auto postgres_bin = qEnvironmentVariable("PHOTO_EDITOR_TEST_POSTGRES_BIN_DIR").trimmed();
  if (postgres_bin.isEmpty()) {
    postgres_bin = qEnvironmentVariable("PHOTO_EDITOR_POSTGRES_BIN_DIR").trimmed();
  }
  if (postgres_bin.isEmpty() || !QFileInfo(QDir(postgres_bin).filePath(QStringLiteral("initdb"))).isExecutable()) {
    std::cout << "SKIP: PHOTO_EDITOR_TEST_POSTGRES_BIN_DIR must point to isolated PostgreSQL test tools\n";
    return kSkip;
  }

  QTemporaryDir runtime(QDir(QDir::tempPath()).filePath(
      QStringLiteral("looksy-mask-layer-relaunch-XXXXXX")));
  if (!runtime.isValid()) return fail(QStringLiteral("Could not create private workflow runtime"));
  const auto app_data_root = runtime.filePath(QStringLiteral("app-data"));
  const auto cache_root = runtime.filePath(QStringLiteral("cache"));
  const auto media_root = runtime.filePath(QStringLiteral("media"));
  const auto retained_proof_root =
      qEnvironmentVariable("PHOTO_EDITOR_MASK_LAYER_RELAUNCH_PROOF_DIR").trimmed();
  const auto proof_root = retained_proof_root.isEmpty()
      ? runtime.filePath(QStringLiteral("proof"))
      : QFileInfo(retained_proof_root).absoluteFilePath();
  if (!QDir().mkpath(app_data_root) || !QDir().mkpath(cache_root) ||
      !QDir().mkpath(media_root) || !QDir().mkpath(proof_root)) {
    return fail(QStringLiteral("Could not create isolated workflow directories"));
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", app_data_root.toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.toUtf8());
  qputenv("PHOTO_EDITOR_PREFERENCES_DIR", runtime.filePath(QStringLiteral("preferences")).toUtf8());
  qputenv("PHOTO_EDITOR_LOG_DIR", QDir(app_data_root).filePath(QStringLiteral("logs")).toUtf8());
  qputenv("PHOTO_EDITOR_BACKUP_DIR", QDir(app_data_root).filePath(QStringLiteral("backups")).toUtf8());
  qputenv("PHOTO_EDITOR_MANAGED_MEDIA_DIR", QDir(app_data_root).filePath(QStringLiteral("managed")).toUtf8());
  qputenv("PHOTO_EDITOR_ORIGINALS_BACKUP_DIR", QDir(app_data_root).filePath(QStringLiteral("originals")).toUtf8());
  qputenv("PHOTO_EDITOR_POSTGRES_BIN_DIR", postgres_bin.toUtf8());
  qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  qunsetenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE");
  if (!isolate_settings_path()) return fail(QStringLiteral("Could not isolate QSettings from the user profile"));

  PrivatePostgres postgres(postgres_bin, runtime.filePath(QStringLiteral("postgres")),
                           runtime.filePath(QStringLiteral("postgres.log")), QStringLiteral("/tmp"));
  QString database_url;
  if (!postgres.start(&database_url)) return fail(QStringLiteral("Could not start private PostgreSQL"));
  qputenv("PHOTO_EDITOR_DATABASE_URL", database_url.toUtf8());

  const auto private_photo_path = QDir(media_root).filePath(source_info.fileName());
  if (!QFile::copy(source_path, private_photo_path) || sha256_file(private_photo_path) != original_source_hash) {
    return fail(QStringLiteral("Private photo copy must match the read-only original bytes"));
  }
  bool ok = false;
  const auto import_output = run_command(
      AppBootstrap::helperProgram(QStringLiteral("import_catalog")),
      AppBootstrap::helperArguments(QStringLiteral("import_catalog"),
          {media_root, QStringLiteral("photo-mask-relaunch-drive"),
           QStringLiteral("Mask and layer relaunch proof"), QStringLiteral("referenced")}),
      &ok, 120000);
  if (!ok) return fail(QStringLiteral("Private catalog import failed: %1").arg(import_output));

  const auto psql = QDir(postgres_bin).filePath(QStringLiteral("psql"));
  const auto asset_id = run_command(
      psql, {database_url, QStringLiteral("-X"), QStringLiteral("-A"), QStringLiteral("-t"),
             QStringLiteral("-v"), QStringLiteral("ON_ERROR_STOP=1"), QStringLiteral("-c"),
             QStringLiteral("SELECT asset_id FROM assets WHERE relative_path='%1'").arg(source_info.fileName())},
      &ok);
  if (!ok || asset_id.isEmpty()) return fail(QStringLiteral("Imported photo needs one catalog asset ID"));

  const auto executable = QCoreApplication::applicationFilePath();
  const auto expected_recipe_path = QDir(proof_root).filePath(QStringLiteral("expected-recipe.json"));
  const auto author_export_path = QDir(proof_root).filePath(QStringLiteral("author-export.png"));
  const auto author_receipt_path = QDir(proof_root).filePath(QStringLiteral("author.json"));
  const auto author_arguments = QStringList{
      QStringLiteral("--author"), asset_id, private_photo_path,
      expected_recipe_path, author_export_path, author_receipt_path};
  QProcess author;
  author.setProgram(executable);
  author.setArguments(author_arguments);
  author.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
  author.setWorkingDirectory(QDir::currentPath());
  author.start();
  if (!author.waitForStarted(10000) || !author.waitForFinished(300000) ||
      author.exitStatus() != QProcess::NormalExit || author.exitCode() != 0) {
    if (author.state() != QProcess::NotRunning) {
      author.kill();
      (void)author.waitForFinished(5000);
    }
    std::cerr << "Author process output: "
              << QString::fromUtf8(author.readAllStandardError()).toStdString() << '\n';
    return fail(QStringLiteral("Author process must exit normally after persisting the recipe"));
  }
  const auto author_receipt = read_json(author_receipt_path);
  if (author_receipt.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("looksy-mask-layer-author-process:v1") ||
      author_receipt.value(QStringLiteral("status")).toString() != QStringLiteral("passed") ||
      author_receipt.value(QStringLiteral("layerDuplicateMaskPolicy")).toString() !=
          QStringLiteral("with-masks-shared-links") ||
      author_receipt.value(QStringLiteral("proofDir")).toString() != proof_root ||
      author_receipt.value(QStringLiteral("processId")).toVariant().toLongLong() <= 1 ||
      author_receipt.value(QStringLiteral("appDataRoot")).toString() != app_data_root ||
      author_receipt.value(QStringLiteral("cacheRoot")).toString() != cache_root ||
      author_receipt.value(QStringLiteral("sourceSha256")).toString() != original_source_hash ||
      author_receipt.value(QStringLiteral("databaseUrlSha256")).toString() !=
          QString::fromLatin1(QCryptographicHash::hash(database_url.toUtf8(), QCryptographicHash::Sha256).toHex())) {
    return fail(QStringLiteral("Author process receipt must prove private catalog, source, and process state"));
  }

  const auto reopen_export_path = QDir(proof_root).filePath(QStringLiteral("reopen-export.png"));
  const auto reopen_arguments = QStringList{
      QStringLiteral("--reopen"), asset_id, private_photo_path,
      expected_recipe_path, author_export_path, reopen_export_path};
  QProcess reopen;
  reopen.setProgram(executable);
  reopen.setArguments(reopen_arguments);
  reopen.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
  reopen.setWorkingDirectory(QDir::currentPath());
  reopen.start();
  if (!reopen.waitForStarted(10000) || !reopen.waitForFinished(300000) ||
      reopen.exitStatus() != QProcess::NormalExit || reopen.exitCode() != 0) {
    if (reopen.state() != QProcess::NotRunning) {
      reopen.kill();
      (void)reopen.waitForFinished(5000);
    }
    std::cerr << "Reopen process output: "
              << QString::fromUtf8(reopen.readAllStandardError()).toStdString() << '\n';
    return fail(QStringLiteral("Fresh reopen process must restore and export the saved recipe"));
  }
  const auto reopen_receipt = read_json(QDir(proof_root).filePath(QStringLiteral("reopen.json")));
  const auto source_hash_after = sha256_file(source_path);
  const auto author_pid = author_receipt.value(QStringLiteral("processId")).toVariant().toLongLong();
  const auto reopen_pid = reopen_receipt.value(QStringLiteral("processId")).toVariant().toLongLong();
  if (reopen_receipt.value(QStringLiteral("status")).toString() != QStringLiteral("passed") ||
      reopen_receipt.value(QStringLiteral("canonicalProjectionLoaded")).toBool() != true ||
      reopen_receipt.value(QStringLiteral("recipeIdsRestoredExact")).toBool() != true ||
      reopen_receipt.value(QStringLiteral("exportPixelsExact")).toBool() != true ||
      reopen_receipt.value(QStringLiteral("exportBytesExact")).toBool() != true ||
      reopen_receipt.value(QStringLiteral("layerDuplicateMaskPolicy")).toString() !=
          QStringLiteral("with-masks-shared-links") ||
      reopen_receipt.value(QStringLiteral("proofDir")).toString() != proof_root ||
      author_pid == reopen_pid || author_pid <= 1 || reopen_pid <= 1 ||
      reopen_receipt.value(QStringLiteral("assetId")).toString() != asset_id ||
      reopen_receipt.value(QStringLiteral("databaseUrlSha256")).toString() !=
          author_receipt.value(QStringLiteral("databaseUrlSha256")).toString() ||
      reopen_receipt.value(QStringLiteral("appDataRoot")).toString() != app_data_root ||
      reopen_receipt.value(QStringLiteral("cacheRoot")).toString() != cache_root ||
      source_hash_after != original_source_hash) {
    return fail(QStringLiteral("Fresh process must use the same private catalog, restore IDs/pixels/export, and leave source unchanged"));
  }

  std::cout << "photo_mask_layer_relaunch_workflow=passed"
            << " author_pid=" << author_pid << " reopen_pid=" << reopen_pid
            << " source_sha256=" << original_source_hash.toStdString()
            << " recipe_sha256=" << reopen_receipt.value(QStringLiteral("recipeSha256")).toString().toStdString()
            << " export_sha256=" << reopen_receipt.value(QStringLiteral("exportSha256")).toString().toStdString()
            << " proof_dir=" << proof_root.toStdString()
            << " private_catalog=1 journal_enabled=1 selection_clip=1 paint=1 erase=1"
            << " masked_adjustment=1 mask_move_copy=1 layer_copy_reorder=1 undo_redo=1"
            << " layer_duplicate_mask_policy=with-masks-shared-links"
            << " fresh_process=1 original_source_unchanged=1\n";
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  if (!qEnvironmentVariable("PHOTO_EDITOR_PREFERENCES_DIR").trimmed().isEmpty() &&
      !isolate_settings_path()) {
    return fail(QStringLiteral("Could not isolate QSettings from the user profile"));
  }
  if (argc > 1) {
    const auto mode = QString::fromLocal8Bit(argv[1]);
    if (mode == QStringLiteral("--author")) return author_process(argc, argv);
    if (mode == QStringLiteral("--reopen")) return reopen_process(argc, argv);
  }
  return parent_process();
}
