#include "carousel_document_controller.h"
#include "edit_adjustment_patch.h"
#include "app_bootstrap.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <QThreadPool>
#include <QSemaphore>
#include <QEventLoop>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <functional>
#include <limits>
#if !defined(_WIN32)
#include <sys/resource.h>
#endif

namespace photo_editor::desktop {
struct CarouselHistoryTestAccess {
  static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
  }
  static void seed(CarouselDocumentController& controller, int boards, const QString& image_path) {
    controller.slides_.clear();
    controller.items_.clear();
    controller.has_active_carousel_ = true;
    CarouselDocumentController::CanvasItem prototype;
    prototype.kind = CarouselDocumentController::ItemKind::Image;
    prototype.file_path = image_path;
    prototype.source_size = QSizeF(1024, 768);
    prototype.bounds = QRectF(20, 20, 300, 200);
    const auto serialized = controller.itemObject(prototype);
    prototype.source_file_size = serialized.value("sourceFileSize").toInteger();
    prototype.source_modified_ms = serialized.value("sourceModifiedMs").toInteger();
    prototype.source_fingerprint = serialized.value("sourceFingerprint").toString();
    for (int board = 0; board < boards; ++board) {
      controller.slides_.push_back({});
      for (int index = 0; index < 6; ++index) {
        auto item = prototype;
        item.id = QStringLiteral("item-%1-%2").arg(board).arg(index);
        item.bounds.translate(board * 1080.0, index * 20.0);
        if (index % 3 == 1) {
          item.kind = CarouselDocumentController::ItemKind::Text;
          item.file_path.clear();
          item.text = QStringLiteral("Board %1 editable text").arg(board);
        } else if (index % 3 == 2) {
          item.kind = CarouselDocumentController::ItemKind::Brush;
          item.file_path.clear();
          for (int point = 0; point < 48; ++point) item.brush_points.push_back(QPointF(point, point % 13));
        }
        if (item.file_path.isEmpty()) {
          item.source_file_size = 0;
          item.source_modified_ms = 0;
          item.source_fingerprint.clear();
        }
        controller.items_.push_back(std::move(item));
      }
    }
    controller.current_index_ = 0;
    controller.selected_item_id_ = QStringLiteral("item-0-0");
    controller.selected_item_ids_ = {controller.selected_item_id_};
    controller.document_dirty_ = true;
    // Use the same parser normalization as saved documents before measurements.
    require(controller.loadDocumentObject(controller.documentObject()), "Fixture normalization failed");
    controller.clearHistory();
  }
  static QJsonObject read(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Recovery fixture read failed");
    return QJsonDocument::fromJson(file.readAll()).object();
  }
  static void waitAutosave(CarouselDocumentController& controller) {
    QElapsedTimer timeout; timeout.start();
    while (controller.save_state_ == QStringLiteral("Unsaved")) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
      require(timeout.elapsed() < 30000, "Autosave did not finish within 30 seconds");
      if (controller.save_state_ == QStringLiteral("Unsaved")) QThread::msleep(1);
    }
  }
  static void writeAndWait(CarouselDocumentController& controller) {
    controller.save_state_ = QStringLiteral("Unsaved");
    controller.writeAutosave();
    waitAutosave(controller);
  }
  static void verify(CarouselDocumentController& controller, const QString& image_path) {
    const auto complete = controller.items_.at(0);
    const auto item_reference = controller.itemObject(complete);
    for (int field = 0; field < 4; ++field) {
      auto partial = complete;
      if (field == 0 || field == 3) partial.source_file_size = 0;
      if (field == 1 || field == 3) partial.source_modified_ms = 0;
      if (field == 2 || field == 3) partial.source_fingerprint.clear();
      require(controller.itemObject(partial) == item_reference, "Item identity fallback changed output");
    }
    CarouselDocumentController::MediaItem media;
    media.type = CarouselDocumentController::MediaType::Image;
    media.file_path = image_path;
    media.source_file_size = complete.source_file_size;
    media.source_modified_ms = complete.source_modified_ms;
    media.source_fingerprint = complete.source_fingerprint;
    media.source_pixel_size = QSize(1024,768);
    const auto media_reference = controller.mediaObject(media);
    for (int field = 0; field < 5; ++field) {
      auto partial = media;
      if (field == 0 || field == 4) partial.source_file_size = -1;
      if (field == 1 || field == 4) partial.source_modified_ms = -1;
      if (field == 2 || field == 4) partial.source_fingerprint.clear();
      if (field == 3 || field == 4) partial.source_pixel_size = QSize();
      require(controller.mediaObject(partial) == media_reference, "Media identity fallback changed output");
    }
    auto zero_size = media;
    zero_size.source_pixel_size = QSize(0,0);
    require(controller.mediaObject(zero_size).value("sourcePixelWidth").toInt() == 0,
            "Serializer changed the existing valid-size policy");
    // Stored provenance already takes precedence if the source changes or disappears.
    QImage changed(64,32,QImage::Format_RGB32); changed.fill(Qt::blue);
    require(changed.save(image_path), "Source replacement failed");
    require(controller.itemObject(complete) == item_reference && controller.mediaObject(media) == media_reference,
            "Complete captured provenance was replaced by current source facts");
    auto missing = complete;
    missing.source_fingerprint.clear();
    const auto replaced = controller.itemObject(missing);
    require(replaced.value("sourceFingerprint").toString() != complete.source_fingerprint,
            "Missing fingerprint did not inspect the replacement source");
    require(QFile::remove(image_path), "Source removal failed");
    require(controller.itemObject(complete) == item_reference && controller.mediaObject(media) == media_reference,
            "Missing source changed complete captured provenance");
    require(controller.itemObject(missing).value("sourceFingerprint").toString().isEmpty(),
            "Missing source invented a fingerprint");
    media.source_pixel_size = QSize();
    require(controller.mediaObject(media).value("sourcePixelWidth").toInt() == -1,
            "Missing source invented dimensions");

    controller.document_passthrough_.insert(QStringLiteral("futureField"), QJsonObject{{"keep",true}});
    controller.document_dirty_ = true;
    controller.autosave_timer_->stop();
    const auto first = controller.documentObject();
    writeAndWait(controller);
    const auto path = controller.autosavePath();
    require(read(path).value("document").toObject() == first, "First recovery serialization changed content");
    auto* item = &controller.items_[0];
    controller.pushUndoState(QStringLiteral("Fixture edit"));
    item->rotation_degrees = 15.0;
    const auto second = controller.documentObject();
    writeAndWait(controller);
    const auto last_good = QString(path).replace(QStringLiteral(".autosave.json"),QStringLiteral(".last-good.json"));
    require(read(path).value("document").toObject() == second &&
            read(last_good).value("document").toObject() == first, "Current/last-good recovery order changed");
    controller.undo();
    require(controller.documentObject() == first, "Undo changed serialized provenance or unknown fields");
    controller.redo();
    require(controller.documentObject() == second, "Redo changed serialized provenance or unknown fields");
    controller.preview_debounce_timer_->stop();
    const auto backup = read(last_good);
    const auto held = path + QStringLiteral(".held");
    require(QFile::rename(path,held) && QDir().mkpath(path), "Could not prepare write-failure fixture");
    writeAndWait(controller);
    require(!controller.save_error_.isEmpty() && controller.documentObject() == second && read(last_good) == backup,
            "Autosave failure lost the document or last-good recovery");
    require(QDir().rmdir(path) && QFile::rename(held,path), "Could not restore write-failure fixture");
    writeAndWait(controller);
    require(controller.save_error_.isEmpty() && read(path).value("document").toObject() == second,
            "Autosave retry changed document content");
    QFile corrupt(path);
    require(corrupt.open(QIODevice::WriteOnly) && corrupt.write("{truncated") == 10,
            "Could not prepare interrupted-save recovery fixture");
    corrupt.close();
    CarouselDocumentController reopened;
    reopened.refreshRecoveryState();
    require(reopened.recovery_state_.value(QStringLiteral("available")).toBool() &&
            reopened.recovery_state_.value(QStringLiteral("usingLastGood")).toBool() &&
            reopened.restoreRecovery(), "Reopened controller did not recover the last-good document");
    require(reopened.documentObject() == second, "Reopened recovery lost serialized document content");
    reopened.preview_debounce_timer_->stop();
    require(corrupt.open(QIODevice::ReadOnly) && corrupt.readAll() == QByteArrayLiteral("{truncated"),
            "Recovery modified the interrupted-save bytes");
  }
  static void verifySnapshots(const QString& image_path) {
    CarouselDocumentController subject;
    seed(subject, 2, image_path);
    const auto probe_path = image_path + QStringLiteral(".snapshot.png");
    require(QFile::copy(image_path, probe_path), "Snapshot source fixture copy failed");
    auto& media = subject.slides_[0].primary;
    media.type = CarouselDocumentController::MediaType::Image;
    media.file_path = probe_path;
    CarouselDocumentController::RecipeState recipe;
    recipe.exposure = 0.65;
    media.adjustment_patch_json = serialize_adjustment_patch_json(
        recipe, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
    media.cutout_mask_data = QByteArray(4 * 1024 * 1024, '\x37');
    subject.groups_.push_back({});
    subject.groups_[0].id = QStringLiteral("snapshot-group");
    subject.groups_[0].media = media;
    subject.items_[0].file_path = probe_path;
    subject.items_[0].source_file_size = 0;
    subject.items_[0].source_modified_ms = 0;
    subject.items_[0].source_fingerprint.clear();
    subject.items_[0].cutout_mask_data = media.cutout_mask_data;
    subject.items_[0].adjustment_patch_json = media.adjustment_patch_json;
    subject.items_[0].component_baseline = {{"future", "original"}};
    subject.document_variables_ = {{"campaign", "original"}};
    subject.saved_design_versions_ = {QJsonObject{{"id", "version-original"}, {"future", true}}};
    subject.document_passthrough_.insert(QStringLiteral("futureRoot"), QJsonObject{{"preserve", true}});
    subject.items_[2].brush_points = {QPointF(1,2), QPointF(3,4)};
    // Pointers acquired before capture must continue to edit only the live objects.
    auto* item = &subject.items_[0];
    auto* brush = &subject.items_[2];
    auto* slide = &subject.slides_[0];
    auto* group = &subject.groups_[0];
    const auto expected = subject.documentObject();
    const auto snapshot = subject.captureDocumentSnapshot();
    require(CarouselDocumentController::snapshotDocumentObject(*snapshot) == expected,
            "Immutable snapshot differs from immediate serialization");
    require(snapshot->items.at(0).cutout_mask_data.constData() == item->cutout_mask_data.constData() &&
            snapshot->slides.at(0).primary.cutout_mask_data.constData() == slide->primary.cutout_mask_data.constData(),
            "Immutable snapshot duplicated unchanged raster payloads");
    auto worker = QtConcurrent::run([snapshot] {
      return CarouselDocumentController::snapshotDocumentObject(*snapshot);
    });
    item->rotation_degrees = 27;
    item->adjustment_patch_json.clear();
    item->cutout_mask_data[0] = '\x55';
    item->component_baseline.insert(QStringLiteral("future"), false);
    brush->brush_points[0] = QPointF(100,200);
    slide->board_name = QStringLiteral("Changed board");
    slide->primary.cutout_mask_data[0] = '\x66';
    group->media.offset_x = 0.3;
    subject.document_variables_.insert(QStringLiteral("campaign"), QStringLiteral("changed"));
    subject.saved_design_versions_[0].insert(QStringLiteral("future"), false);
    subject.document_passthrough_.insert(QStringLiteral("futureRoot"), false);
    require(item == &subject.items_[0] && slide == &subject.slides_[0] && group == &subject.groups_[0],
            "Snapshot capture invalidated an edit handler's existing pointers");
    require(QFile::remove(probe_path), "Snapshot source removal failed");
    worker.waitForFinished();
    require(worker.result() == expected && CarouselDocumentController::snapshotDocumentObject(*snapshot) == expected,
            "Live mutation or deleted source changed an immutable snapshot");
    const auto missing_expected = subject.documentObject();
    const auto missing_snapshot = subject.captureDocumentSnapshot();
    QImage replacement(32,16,QImage::Format_RGB32); replacement.fill(Qt::blue);
    require(replacement.save(probe_path, "PNG"), "Snapshot replacement fixture failed");
    require(CarouselDocumentController::snapshotDocumentObject(*missing_snapshot) == missing_expected,
            "Delayed serialization probed a source absent at capture");
    require(CarouselDocumentController::snapshotDocumentObject(*snapshot) == expected,
            "Delayed serialization probed a source replaced after capture");
    require(QFile::remove(probe_path), "Snapshot source cleanup failed");

    CarouselDocumentController history;
    seed(history, 2, image_path);
    const auto initial = history.documentObject();
    history.beginHistoryTransaction(QStringLiteral("Fixture transaction"));
    history.commitHistoryTransaction();
    require(history.undo_stack_.isEmpty(), "No-op transaction created history");
    history.beginHistoryTransaction(QStringLiteral("Fixture transient preview"));
    history.items_[0].preview_source = QStringLiteral("file:///transient-preview.png");
    history.items_[0].recipe_state.exposure = 0.125;
    history.commitHistoryTransaction();
    require(history.undo_stack_.isEmpty(), "Transient render state created a document history entry");
    auto* live = &history.items_[0];
    history.beginHistoryTransaction(QStringLiteral("Fixture transaction"));
    live->rotation_degrees = 45;
    history.commitHistoryTransaction();
    const auto changed = history.documentObject();
    require(history.undo_stack_.size() == 1, "Changed transaction did not create one entry");
    history.undo();
    require(history.documentObject() == initial, "Transaction undo lost document state");
    history.redo();
    require(history.documentObject() == changed, "Transaction redo lost document state");
    history.clearHistory();
    live = &history.items_[0];
    history.pushUndoState(QStringLiteral("Fixture coalesced"), true);
    live->rotation_degrees = 60;
    history.pushUndoState(QStringLiteral("Fixture coalesced"), true);
    live->rotation_degrees = 75;
    require(history.undo_stack_.size() == 1, "Coalescing created duplicate history entries");
    history.undo();
    require(history.documentObject() == changed, "Coalescing failed to preserve its starting state");
    history.clearHistory();
    for (int index = 0; index < 60; ++index) {
      live = &history.items_[0];
      history.pushUndoState(QStringLiteral("Fixture bounded"));
      live->rotation_degrees = index;
    }
    require(history.undo_stack_.size() == 48, "History entry bound changed");
    history.preview_debounce_timer_->stop();
    history.autosave_timer_->stop();
  }
  struct WorkerGate {
    QSemaphore entered;
    QSemaphore release;
    QFuture<void> future;
    bool released = false;
    explicit WorkerGate(QThreadPool& pool) {
      future = QtConcurrent::run(&pool, [this] { entered.release(); release.acquire(); });
      if (!entered.tryAcquire(1, 10000)) {
        unblock();
        future.waitForFinished();
        throw std::runtime_error("Autosave worker gate did not start");
      }
    }
    void unblock() { if (!released) { released = true; release.release(); } }
    ~WorkerGate() { unblock(); future.waitForFinished(); }
  };
  static void drainEvents(CarouselDocumentController& subject) {
    QElapsedTimer timer; timer.start();
    while (subject.active_autosave_) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
      require(timer.elapsed() < 30000, "Autosave queue did not drain");
      if (subject.active_autosave_) QThread::msleep(1);
    }
  }
  static void verifyAsyncAutosave(const QString& image_path) {
    // Force catalog helper failure into the real legacy-file save/load path,
    // without consulting a user database or running Cargo.
    QTemporaryDir helper_root;
    require(helper_root.isValid(), "Helper fixture directory failed");
    require(QDir().mkpath(helper_root.filePath("rust/target/debug")), "Helper fixture path failed");
    QFile manifest(helper_root.filePath("rust/Cargo.toml"));
    require(manifest.open(QIODevice::WriteOnly) && manifest.write("# isolated fixture\n") > 0,
            "Helper fixture manifest failed");
    manifest.close();
    auto helper_path = helper_root.filePath("rust/target/debug/catalog_carousel_documents");
#if defined(Q_OS_WIN)
    helper_path += QStringLiteral(".exe");
#endif
    require(QFile::copy(QCoreApplication::applicationFilePath(), helper_path), "Helper fixture copy failed");
    require(QFile::setPermissions(helper_path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
            "Helper fixture permissions failed");
    qputenv("PHOTO_EDITOR_CAROUSEL_FIXTURE_HELPER", "1");
    qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", helper_root.path().toUtf8());
    qputenv("PHOTO_EDITOR_DATABASE_URL", "disabled-for-carousel-fixture");
    require(AppBootstrap::helperProgram(QStringLiteral("catalog_carousel_documents")) == helper_path,
            "Fixture helper isolation failed");
    {
      CarouselDocumentController subject;
      seed(subject, 2, image_path);
      subject.autosave_timer_->stop();
      const auto first = subject.documentObject();
      const auto path = subject.autosavePath();
      const auto last_good = QString(path).replace(".autosave.json", ".last-good.json");
      WorkerGate gate(CarouselDocumentController::autosaveThreadPool());
      subject.writeAutosave();
      for (int revision = 1; revision <= 20; ++revision) {
        subject.items_[0].rotation_degrees = revision;
        subject.markDocumentDirty();
        subject.writeAutosave();
        require(subject.active_autosave_ && subject.pending_autosave_, "Bounded autosave queue lost its requests");
      }
      const auto latest = subject.documentObject();
      require(CarouselDocumentController::snapshotDocumentObject(*subject.active_autosave_->snapshot) == first &&
              CarouselDocumentController::snapshotDocumentObject(*subject.pending_autosave_->snapshot) == latest,
              "Autosave did not retain just the active and latest requested document");
      gate.unblock();
      drainEvents(subject);
      subject.autosave_timer_->stop();
      require(read(path).value("document").toObject() == latest &&
              read(last_good).value("document").toObject() == first &&
              subject.save_state_ == QStringLiteral("Autosaved"), "Coalesced recovery order or status changed");
      WorkerGate stale_gate(CarouselDocumentController::autosaveThreadPool());
      subject.writeAutosave();
      subject.items_[0].rotation_degrees = 45;
      subject.markDocumentDirty();
      subject.autosave_timer_->stop();
      stale_gate.unblock();
      drainEvents(subject);
      require(subject.save_state_ == QStringLiteral("Unsaved") && subject.autosave_timer_->isActive(),
              "A stale completion marked newer edits autosaved or failed to schedule recovery");
      subject.autosave_timer_->stop();
      const auto current_before_failure = read(path);
      const auto held = last_good + QStringLiteral(".held");
      require(QFile::rename(last_good, held) && QDir().mkpath(last_good), "Last-good failure fixture failed");
      writeAndWait(subject);
      require(!subject.save_error_.isEmpty() && read(path) == current_before_failure,
              "Last-good write failure replaced the current recovery file");
      require(QDir().rmdir(last_good) && QFile::rename(held, last_good), "Last-good fixture restore failed");
      writeAndWait(subject);
      const auto valid_backup = read(last_good);
      QFile corrupt(path);
      require(corrupt.open(QIODevice::WriteOnly) &&
              corrupt.write("{\"recoveryVersion\":1,\"document\":{\"version\":99999}}") > 0,
              "Unsupported schema fixture failed");
      corrupt.close();
      writeAndWait(subject);
      require(read(last_good) == valid_backup, "Unsupported current schema replaced last-good recovery");
      // A successful explicit save must remove both the old session-keyed
      // recovery and the newly assigned document-keyed recovery paths.
      WorkerGate save_gate(CarouselDocumentController::autosaveThreadPool());
      subject.writeAutosave();
      subject.items_[0].rotation_degrees = 60;
      subject.markDocumentDirty();
      subject.writeAutosave();
      save_gate.unblock();
      require(subject.saveDocument(QStringLiteral("Worker save fixture")), "Explicit save fixture failed");
      QCoreApplication::processEvents();
      require(subject.save_state_ == QStringLiteral("Saved") && !QFile::exists(path) &&
              !QFile::exists(last_good) && !QFile::exists(subject.autosavePath()) &&
              !subject.active_autosave_ && !subject.pending_autosave_,
              "Delayed autosave resurrected recovery or replaced explicit save status");
      const auto saved = subject.documentObject();
      const auto saved_id = subject.saved_document_id_;
      WorkerGate load_gate(CarouselDocumentController::autosaveThreadPool());
      subject.items_[0].rotation_degrees = 75;
      subject.markDocumentDirty();
      subject.writeAutosave();
      load_gate.unblock();
      require(subject.loadDocument(saved_id), "Saved document load fixture failed");
      QCoreApplication::processEvents();
      require(subject.documentObject() == saved && subject.save_state_ == QStringLiteral("Saved"),
              "Old completion modified the loaded document or status");
      subject.markDocumentDirty();
      WorkerGate discard_gate(CarouselDocumentController::autosaveThreadPool());
      subject.writeAutosave();
      subject.recovery_path_ = subject.autosavePath();
      const auto discard_path = subject.recovery_path_;
      discard_gate.unblock();
      subject.discardRecovery();
      QCoreApplication::processEvents();
      require(!QFile::exists(discard_path) && !QFile::exists(QString(discard_path).replace(".autosave.json", ".last-good.json")) &&
              !subject.autosave_timer_->isActive(), "Delayed write recreated discarded recovery");
    }
    {
      const auto normal_root = qgetenv("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH");
      const auto failed_root = helper_root.filePath(QStringLiteral("failed-save"));
      require(QDir().mkpath(failed_root), "Failed-save fixture root failed");
      QFile blocked(QDir(failed_root).filePath(QStringLiteral("documents")));
      require(blocked.open(QIODevice::WriteOnly), "Failed-save fixture blocker failed");
      blocked.close();
      qputenv("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH", failed_root.toUtf8());
      CarouselDocumentController subject;
      seed(subject, 1, image_path);
      const auto path = subject.autosavePath();
      const auto current = subject.documentObject();
      subject.writeAutosave();
      require(!subject.saveDocument() && !subject.save_error_.isEmpty() &&
              subject.saved_document_id_.isEmpty() && subject.autosave_timer_->isActive() &&
              subject.documentObject() == current && read(path).value("document").toObject() == current,
              "Failed explicit save lost recovery, changed identity, or stopped future autosaves");
      subject.autosave_timer_->stop();
      qputenv("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH", normal_root);
    }
    qunsetenv("PHOTO_EDITOR_CAROUSEL_FIXTURE_HELPER");
    {
      CarouselDocumentController subject;
      seed(subject, 1, image_path);
      const auto old_path = subject.autosavePath();
      const auto old = subject.documentObject();
      WorkerGate gate(CarouselDocumentController::autosaveThreadPool());
      subject.writeAutosave();
      gate.unblock();
      subject.createCarouselWithSettings({{"carouselName", "Replacement document"}});
      QCoreApplication::processEvents();
      require(read(old_path).value("document").toObject() == old &&
              subject.document_name_ == QStringLiteral("Replacement document") &&
              subject.autosavePath() != old_path && subject.save_state_ != QStringLiteral("Autosaved"),
              "Document replacement mixed recovery ownership or status");
      subject.autosave_timer_->stop();
      const auto close_path = subject.autosavePath();
      const auto closing = subject.documentObject();
      subject.writeAutosave();
      subject.closeCarousel();
      QCoreApplication::processEvents();
      require(!subject.has_active_carousel_ && subject.save_state_ == QStringLiteral("Ready") &&
              read(close_path).value("document").toObject() == closing,
              "Close lost an admitted checkpoint or applied stale status");
    }
    {
      auto subject = std::make_unique<CarouselDocumentController>();
      seed(*subject, 1, image_path);
      const auto path = subject->autosavePath();
      subject->writeAutosave();
      subject->items_[0].rotation_degrees = 80;
      subject->markDocumentDirty();
      subject->writeAutosave();
      const auto latest = subject->documentObject();
      subject.reset();
      QCoreApplication::processEvents();
      require(read(path).value("document").toObject() == latest, "Destruction lost the latest admitted checkpoint");
    }
  }
  static void compareToSerialization(const CarouselDocumentController::DocumentSnapshot& left,
                                     const CarouselDocumentController::DocumentSnapshot& right,
                                     const char* label) {
    auto a = CarouselDocumentController::snapshotDocumentObject(left);
    auto b = CarouselDocumentController::snapshotDocumentObject(right);
    if (CarouselDocumentController::documentSnapshotsEqual(left,right) != (a == b))
      throw std::runtime_error(std::string("Document equality differs from serialization: ") + label);
    for (const auto& key : {"currentIndex", "activeSlot", "selectedItemId", "selectedItemIds"}) {
      a.remove(QLatin1String(key)); b.remove(QLatin1String(key));
    }
    if (CarouselDocumentController::documentSnapshotsEqual(left,right,true) != (a == b) ||
        CarouselDocumentController::documentSnapshotsEqual(left,right,true,&a) != (a == b))
      throw std::runtime_error(std::string("Persistent equality differs from serialization: ") + label);
  }
  static void verifyEquality(const QString& image_path) {
    CarouselDocumentController subject;
    seed(subject, 2, image_path);
    subject.groups_.push_back({});
    subject.groups_[0].id = QStringLiteral("comparison-group");
    auto baseline = *subject.captureDocumentSnapshot();
    const std::vector<std::pair<const char*, std::function<void(CarouselDocumentController::MediaItem&)>>> mediaitem_changes = {
      {"MediaItem.type", [](auto& value) { value.type = static_cast<CarouselDocumentController::MediaType>(static_cast<int>(value.type) + 1); }},
      {"MediaItem.file_path", [](auto& value) { value.file_path += QStringLiteral(" changed"); }},
      {"MediaItem.display_label", [](auto& value) { value.display_label += QStringLiteral(" changed"); }},
      {"MediaItem.adjustment_patch_json", [](auto& value) { value.adjustment_patch_json += QStringLiteral(" changed"); }},
      {"MediaItem.recipe_state", [](auto& value) { value.recipe_state.exposure += 0.25; }},
      {"MediaItem.fit_mode", [](auto& value) { value.fit_mode = static_cast<CarouselDocumentController::FitMode>(static_cast<int>(value.fit_mode) + 1); }},
      {"MediaItem.scale", [](auto& value) { value.scale += 0.125; }},
      {"MediaItem.offset_x", [](auto& value) { value.offset_x += 0.125; }},
      {"MediaItem.offset_y", [](auto& value) { value.offset_y += 0.125; }},
      {"MediaItem.focal_x", [](auto& value) { value.focal_x += 0.125; }},
      {"MediaItem.focal_y", [](auto& value) { value.focal_y += 0.125; }},
      {"MediaItem.manual_crop", [](auto& value) { value.manual_crop = !value.manual_crop; }},
      {"MediaItem.crop_source", [](auto& value) { value.crop_source += QStringLiteral(" changed"); }},
      {"MediaItem.video_duration_seconds", [](auto& value) { value.video_duration_seconds += 0.125; }},
      {"MediaItem.video_trim_in_seconds", [](auto& value) { value.video_trim_in_seconds += 0.125; }},
      {"MediaItem.video_trim_out_seconds", [](auto& value) { value.video_trim_out_seconds += 0.125; }},
      {"MediaItem.video_poster_seconds", [](auto& value) { value.video_poster_seconds += 0.125; }},
      {"MediaItem.video_audio_policy", [](auto& value) { value.video_audio_policy += QStringLiteral(" changed"); }},
      {"MediaItem.video_loop_intent", [](auto& value) { value.video_loop_intent = !value.video_loop_intent; }},
      {"MediaItem.source_file_size", [](auto& value) { value.source_file_size += 1; }},
      {"MediaItem.source_modified_ms", [](auto& value) { value.source_modified_ms += 1; }},
      {"MediaItem.source_pixel_size", [](auto& value) { value.source_pixel_size.setWidth(value.source_pixel_size.width() + 1); }},
      {"MediaItem.source_fingerprint", [](auto& value) { value.source_fingerprint += QStringLiteral(" changed"); }},
      {"MediaItem.cutout_enabled", [](auto& value) { value.cutout_enabled = !value.cutout_enabled; }},
      {"MediaItem.cutout_mask_data", [](auto& value) { value.cutout_mask_data.append("changed"); }},
      {"MediaItem.cutout_mask_width", [](auto& value) { value.cutout_mask_width += 1; }},
      {"MediaItem.cutout_mask_height", [](auto& value) { value.cutout_mask_height += 1; }},
      {"MediaItem.cutout_feather", [](auto& value) { value.cutout_feather += 0.125; }},
      {"MediaItem.cutout_expand", [](auto& value) { value.cutout_expand += 0.125; }},
      {"MediaItem.cutout_edge_contrast", [](auto& value) { value.cutout_edge_contrast += 0.125; }},
    };
    for (const auto& [label, mutate] : mediaitem_changes) {
      auto changed = baseline;
      mutate(changed.slides[0].primary);
      compareToSerialization(baseline, changed, label);
    }
    const std::vector<std::pair<const char*, std::function<void(CarouselDocumentController::Slide&)>>> slide_changes = {
      {"Slide.template_kind", [](auto& value) { value.template_kind = static_cast<CarouselDocumentController::SlideTemplate>(static_cast<int>(value.template_kind) + 1); }},
      {"Slide.primary", [](auto& value) { value.primary.offset_x += 0.25; }},
      {"Slide.secondary", [](auto& value) { value.secondary.offset_x += 0.25; }},
      {"Slide.tertiary", [](auto& value) { value.tertiary.offset_x += 0.25; }},
      {"Slide.linked_group_id", [](auto& value) { value.linked_group_id += QStringLiteral(" changed"); }},
      {"Slide.section_id", [](auto& value) { value.section_id += QStringLiteral(" changed"); }},
      {"Slide.background_color", [](auto& value) { value.background_color += QStringLiteral(" changed"); }},
      {"Slide.background_secondary_color", [](auto& value) { value.background_secondary_color += QStringLiteral(" changed"); }},
      {"Slide.background_kind", [](auto& value) { value.background_kind += QStringLiteral(" changed"); }},
      {"Slide.brand_theme_kit_id", [](auto& value) { value.brand_theme_kit_id += QStringLiteral(" changed"); }},
      {"Slide.brand_theme_variant_id", [](auto& value) { value.brand_theme_variant_id += QStringLiteral(" changed"); }},
      {"Slide.board_name", [](auto& value) { value.board_name += QStringLiteral(" changed"); }},
      {"Slide.board_role", [](auto& value) { value.board_role += QStringLiteral(" changed"); }},
      {"Slide.message_note", [](auto& value) { value.message_note += QStringLiteral(" changed"); }},
      {"Slide.media_intent_note", [](auto& value) { value.media_intent_note += QStringLiteral(" changed"); }},
      {"Slide.source_reference_note", [](auto& value) { value.source_reference_note += QStringLiteral(" changed"); }},
      {"Slide.alt_text", [](auto& value) { value.alt_text += QStringLiteral(" changed"); }},
      {"Slide.plain_language_description", [](auto& value) { value.plain_language_description += QStringLiteral(" changed"); }},
      {"Slide.accessibility_reading_order", [](auto& value) { value.accessibility_reading_order.push_back(QStringLiteral("changed")); }},
      {"Slide.decorative_media_keys", [](auto& value) { value.decorative_media_keys.push_back(QStringLiteral("changed")); }},
      {"Slide.preview_source", [](auto& value) { value.preview_source += QStringLiteral(" changed"); }},
      {"Slide.layout_columns", [](auto& value) { value.layout_columns += 1; }},
      {"Slide.layout_rows", [](auto& value) { value.layout_rows += 1; }},
      {"Slide.layout_margin_x", [](auto& value) { value.layout_margin_x += 0.125; }},
      {"Slide.layout_margin_y", [](auto& value) { value.layout_margin_y += 0.125; }},
      {"Slide.layout_gutter_x", [](auto& value) { value.layout_gutter_x += 0.125; }},
      {"Slide.layout_gutter_y", [](auto& value) { value.layout_gutter_y += 0.125; }},
    };
    for (const auto& [label, mutate] : slide_changes) {
      auto changed = baseline;
      mutate(changed.slides[0]);
      compareToSerialization(baseline, changed, label);
    }
    const std::vector<std::pair<const char*, std::function<void(CarouselDocumentController::LinkedGroup&)>>> linkedgroup_changes = {
      {"LinkedGroup.id", [](auto& value) { value.id += QStringLiteral(" changed"); }},
      {"LinkedGroup.kind", [](auto& value) { value.kind = static_cast<CarouselDocumentController::GroupKind>(static_cast<int>(value.kind) + 1); }},
      {"LinkedGroup.start_index", [](auto& value) { value.start_index += 1; }},
      {"LinkedGroup.span", [](auto& value) { value.span += 1; }},
      {"LinkedGroup.media", [](auto& value) { value.media.offset_x += 0.25; }},
    };
    for (const auto& [label, mutate] : linkedgroup_changes) {
      auto changed = baseline;
      mutate(changed.groups[0]);
      compareToSerialization(baseline, changed, label);
    }
    const std::vector<std::pair<const char*, std::function<void(CarouselDocumentController::CanvasItem&)>>> canvasitem_changes = {
      {"CanvasItem.id", [](auto& value) { value.id += QStringLiteral(" changed"); }},
      {"CanvasItem.object_group_id", [](auto& value) { value.object_group_id += QStringLiteral(" changed"); }},
      {"CanvasItem.component_link_id", [](auto& value) { value.component_link_id += QStringLiteral(" changed"); }},
      {"CanvasItem.component_definition_id", [](auto& value) { value.component_definition_id += QStringLiteral(" changed"); }},
      {"CanvasItem.component_instance_id", [](auto& value) { value.component_instance_id += QStringLiteral(" changed"); }},
      {"CanvasItem.component_part_id", [](auto& value) { value.component_part_id += QStringLiteral(" changed"); }},
      {"CanvasItem.component_definition_version", [](auto& value) { value.component_definition_version += 1; }},
      {"CanvasItem.component_baseline", [](auto& value) { value.component_baseline.insert(QStringLiteral("changed"), true); }},
      {"CanvasItem.constraint_stack_id", [](auto& value) { value.constraint_stack_id += QStringLiteral(" changed"); }},
      {"CanvasItem.constraint_stack_order", [](auto& value) { value.constraint_stack_order += 1; }},
      {"CanvasItem.constraint_stack_axis", [](auto& value) { value.constraint_stack_axis += QStringLiteral(" changed"); }},
      {"CanvasItem.constraint_stack_padding", [](auto& value) { value.constraint_stack_padding += 0.125; }},
      {"CanvasItem.constraint_stack_gap", [](auto& value) { value.constraint_stack_gap += 0.125; }},
      {"CanvasItem.constraint_stack_anchor_x", [](auto& value) { value.constraint_stack_anchor_x += 0.125; }},
      {"CanvasItem.constraint_stack_anchor_y", [](auto& value) { value.constraint_stack_anchor_y += 0.125; }},
      {"CanvasItem.kind", [](auto& value) { value.kind = static_cast<CarouselDocumentController::ItemKind>(static_cast<int>(value.kind) + 1); }},
      {"CanvasItem.file_path", [](auto& value) { value.file_path += QStringLiteral(" changed"); }},
      {"CanvasItem.external_asset_id", [](auto& value) { value.external_asset_id += QStringLiteral(" changed"); }},
      {"CanvasItem.external_asset_file_name", [](auto& value) { value.external_asset_file_name += QStringLiteral(" changed"); }},
      {"CanvasItem.external_asset_sha256", [](auto& value) { value.external_asset_sha256 += QStringLiteral(" changed"); }},
      {"CanvasItem.display_label", [](auto& value) { value.display_label += QStringLiteral(" changed"); }},
      {"CanvasItem.hidden", [](auto& value) { value.hidden = !value.hidden; }},
      {"CanvasItem.locked", [](auto& value) { value.locked = !value.locked; }},
      {"CanvasItem.adjustment_patch_json", [](auto& value) { value.adjustment_patch_json += QStringLiteral(" changed"); }},
      {"CanvasItem.recipe_state", [](auto& value) { value.recipe_state.exposure += 0.25; }},
      {"CanvasItem.preview_source", [](auto& value) { value.preview_source += QStringLiteral(" changed"); }},
      {"CanvasItem.preview_source_size", [](auto& value) { value.preview_source_size.setWidth(value.preview_source_size.width() + 1); }},
      {"CanvasItem.text", [](auto& value) { value.text += QStringLiteral(" changed"); }},
      {"CanvasItem.text_spans", [](auto& value) { value.text_spans.push_back(QJsonObject{{"changed",true}}); }},
      {"CanvasItem.brand_color_kit_id", [](auto& value) { value.brand_color_kit_id += QStringLiteral(" changed"); }},
      {"CanvasItem.brand_color_token_id", [](auto& value) { value.brand_color_token_id += QStringLiteral(" changed"); }},
      {"CanvasItem.brand_typography_kit_id", [](auto& value) { value.brand_typography_kit_id += QStringLiteral(" changed"); }},
      {"CanvasItem.brand_typography_token_id", [](auto& value) { value.brand_typography_token_id += QStringLiteral(" changed"); }},
      {"CanvasItem.text_role", [](auto& value) { value.text_role += QStringLiteral(" changed"); }},
      {"CanvasItem.text_style_id", [](auto& value) { value.text_style_id += QStringLiteral(" changed"); }},
      {"CanvasItem.text_color", [](auto& value) { value.text_color += QStringLiteral(" changed"); }},
      {"CanvasItem.text_point_size", [](auto& value) { value.text_point_size += 0.125; }},
      {"CanvasItem.text_fit_mode", [](auto& value) { value.text_fit_mode += QStringLiteral(" changed"); }},
      {"CanvasItem.text_fit_min_point_size", [](auto& value) { value.text_fit_min_point_size += 0.125; }},
      {"CanvasItem.text_fit_max_point_size", [](auto& value) { value.text_fit_max_point_size += 0.125; }},
      {"CanvasItem.text_font_family", [](auto& value) { value.text_font_family += QStringLiteral(" changed"); }},
      {"CanvasItem.original_font_family", [](auto& value) { value.original_font_family += QStringLiteral(" changed"); }},
      {"CanvasItem.ignored_preflight_codes", [](auto& value) { value.ignored_preflight_codes.push_back(QStringLiteral("changed")); }},
      {"CanvasItem.text_horizontal_alignment", [](auto& value) { value.text_horizontal_alignment += QStringLiteral(" changed"); }},
      {"CanvasItem.text_vertical_alignment", [](auto& value) { value.text_vertical_alignment += QStringLiteral(" changed"); }},
      {"CanvasItem.text_weight", [](auto& value) { value.text_weight += QStringLiteral(" changed"); }},
      {"CanvasItem.text_italic", [](auto& value) { value.text_italic = !value.text_italic; }},
      {"CanvasItem.text_underline", [](auto& value) { value.text_underline = !value.text_underline; }},
      {"CanvasItem.text_strikethrough", [](auto& value) { value.text_strikethrough = !value.text_strikethrough; }},
      {"CanvasItem.text_case", [](auto& value) { value.text_case += QStringLiteral(" changed"); }},
      {"CanvasItem.text_letter_spacing", [](auto& value) { value.text_letter_spacing += 0.125; }},
      {"CanvasItem.text_line_height", [](auto& value) { value.text_line_height += 0.125; }},
      {"CanvasItem.text_paragraph_spacing", [](auto& value) { value.text_paragraph_spacing += 0.125; }},
      {"CanvasItem.text_inset", [](auto& value) { value.text_inset += 0.125; }},
      {"CanvasItem.text_maximum_line_count", [](auto& value) { value.text_maximum_line_count += 1; }},
      {"CanvasItem.text_list_style", [](auto& value) { value.text_list_style += QStringLiteral(" changed"); }},
      {"CanvasItem.text_hanging_indent", [](auto& value) { value.text_hanging_indent += 0.125; }},
      {"CanvasItem.text_auto_number_source", [](auto& value) { value.text_auto_number_source += QStringLiteral(" changed"); }},
      {"CanvasItem.text_stroke_color", [](auto& value) { value.text_stroke_color += QStringLiteral(" changed"); }},
      {"CanvasItem.text_stroke_width", [](auto& value) { value.text_stroke_width += 0.125; }},
      {"CanvasItem.text_background_color", [](auto& value) { value.text_background_color += QStringLiteral(" changed"); }},
      {"CanvasItem.text_background_opacity", [](auto& value) { value.text_background_opacity += 0.125; }},
      {"CanvasItem.text_padding", [](auto& value) { value.text_padding += 0.125; }},
      {"CanvasItem.text_background_radius", [](auto& value) { value.text_background_radius += 0.125; }},
      {"CanvasItem.shape_kind", [](auto& value) { value.shape_kind += QStringLiteral(" changed"); }},
      {"CanvasItem.brush_points", [](auto& value) { value.brush_points.push_back(QPointF(1.25,2.5)); }},
      {"CanvasItem.brush_source_size", [](auto& value) { value.brush_source_size.setWidth(value.brush_source_size.width() + 1); }},
      {"CanvasItem.brush_color", [](auto& value) { value.brush_color += QStringLiteral(" changed"); }},
      {"CanvasItem.brush_width", [](auto& value) { value.brush_width += 0.125; }},
      {"CanvasItem.brush_style", [](auto& value) { value.brush_style += QStringLiteral(" changed"); }},
      {"CanvasItem.brush_eraser", [](auto& value) { value.brush_eraser = !value.brush_eraser; }},
      {"CanvasItem.fill_color", [](auto& value) { value.fill_color += QStringLiteral(" changed"); }},
      {"CanvasItem.fill_secondary_color", [](auto& value) { value.fill_secondary_color += QStringLiteral(" changed"); }},
      {"CanvasItem.fill_kind", [](auto& value) { value.fill_kind += QStringLiteral(" changed"); }},
      {"CanvasItem.stroke_color", [](auto& value) { value.stroke_color += QStringLiteral(" changed"); }},
      {"CanvasItem.stroke_width", [](auto& value) { value.stroke_width += 0.125; }},
      {"CanvasItem.corner_radius", [](auto& value) { value.corner_radius += 0.125; }},
      {"CanvasItem.opacity", [](auto& value) { value.opacity += 0.125; }},
      {"CanvasItem.blend_mode", [](auto& value) { value.blend_mode += QStringLiteral(" changed"); }},
      {"CanvasItem.flipped_horizontal", [](auto& value) { value.flipped_horizontal = !value.flipped_horizontal; }},
      {"CanvasItem.flipped_vertical", [](auto& value) { value.flipped_vertical = !value.flipped_vertical; }},
      {"CanvasItem.frame_color", [](auto& value) { value.frame_color += QStringLiteral(" changed"); }},
      {"CanvasItem.frame_width", [](auto& value) { value.frame_width += 0.125; }},
      {"CanvasItem.shadow_color", [](auto& value) { value.shadow_color += QStringLiteral(" changed"); }},
      {"CanvasItem.shadow_opacity", [](auto& value) { value.shadow_opacity += 0.125; }},
      {"CanvasItem.shadow_offset_x", [](auto& value) { value.shadow_offset_x += 0.125; }},
      {"CanvasItem.shadow_offset_y", [](auto& value) { value.shadow_offset_y += 0.125; }},
      {"CanvasItem.media_scale", [](auto& value) { value.media_scale += 0.125; }},
      {"CanvasItem.media_offset_x", [](auto& value) { value.media_offset_x += 0.125; }},
      {"CanvasItem.media_offset_y", [](auto& value) { value.media_offset_y += 0.125; }},
      {"CanvasItem.media_focal_x", [](auto& value) { value.media_focal_x += 0.125; }},
      {"CanvasItem.media_focal_y", [](auto& value) { value.media_focal_y += 0.125; }},
      {"CanvasItem.media_manual_crop", [](auto& value) { value.media_manual_crop = !value.media_manual_crop; }},
      {"CanvasItem.media_crop_source", [](auto& value) { value.media_crop_source += QStringLiteral(" changed"); }},
      {"CanvasItem.video_duration_seconds", [](auto& value) { value.video_duration_seconds += 0.125; }},
      {"CanvasItem.video_trim_in_seconds", [](auto& value) { value.video_trim_in_seconds += 0.125; }},
      {"CanvasItem.video_trim_out_seconds", [](auto& value) { value.video_trim_out_seconds += 0.125; }},
      {"CanvasItem.video_poster_seconds", [](auto& value) { value.video_poster_seconds += 0.125; }},
      {"CanvasItem.video_audio_policy", [](auto& value) { value.video_audio_policy += QStringLiteral(" changed"); }},
      {"CanvasItem.video_loop_intent", [](auto& value) { value.video_loop_intent = !value.video_loop_intent; }},
      {"CanvasItem.media_mask_shape", [](auto& value) { value.media_mask_shape += QStringLiteral(" changed"); }},
      {"CanvasItem.bounds", [](auto& value) { value.bounds.translate(1,2); }},
      {"CanvasItem.rotation_degrees", [](auto& value) { value.rotation_degrees += 0.125; }},
      {"CanvasItem.fit_mode", [](auto& value) { value.fit_mode = static_cast<CarouselDocumentController::FitMode>(static_cast<int>(value.fit_mode) + 1); }},
      {"CanvasItem.source_size", [](auto& value) { value.source_size.setWidth(value.source_size.width() + 1); }},
      {"CanvasItem.source_file_size", [](auto& value) { value.source_file_size += 1; }},
      {"CanvasItem.source_modified_ms", [](auto& value) { value.source_modified_ms += 1; }},
      {"CanvasItem.source_fingerprint", [](auto& value) { value.source_fingerprint += QStringLiteral(" changed"); }},
      {"CanvasItem.cutout_enabled", [](auto& value) { value.cutout_enabled = !value.cutout_enabled; }},
      {"CanvasItem.cutout_mask_data", [](auto& value) { value.cutout_mask_data.append("changed"); }},
      {"CanvasItem.cutout_mask_width", [](auto& value) { value.cutout_mask_width += 1; }},
      {"CanvasItem.cutout_mask_height", [](auto& value) { value.cutout_mask_height += 1; }},
      {"CanvasItem.cutout_feather", [](auto& value) { value.cutout_feather += 0.125; }},
      {"CanvasItem.cutout_expand", [](auto& value) { value.cutout_expand += 0.125; }},
      {"CanvasItem.cutout_edge_contrast", [](auto& value) { value.cutout_edge_contrast += 0.125; }},
    };
    for (const auto& [label, mutate] : canvasitem_changes) {
      auto changed = baseline;
      mutate(changed.items[0]);
      compareToSerialization(baseline, changed, label);
    }
    for (auto it = baseline.metadata.constBegin(); it != baseline.metadata.constEnd(); ++it) {
      auto changed = baseline;
      const auto value = it.value();
      QJsonValue replacement;
      if (value.isString()) replacement = value.toString() + QStringLiteral(" changed");
      else if (value.isDouble()) replacement = value.toDouble() + 1;
      else if (value.isBool()) replacement = !value.toBool();
      else if (value.isObject()) { auto object=value.toObject(); object.insert("changed",true); replacement=object; }
      else if (value.isArray()) { auto array=value.toArray(); array.push_back("changed"); replacement=array; }
      else replacement = QStringLiteral("changed");
      changed.metadata.insert(it.key(), replacement);
      compareToSerialization(baseline,changed,it.key().toUtf8().constData());
    }
    for (int mode = 0; mode < 5; ++mode) {
      auto changed = baseline;
      if (mode == 0) changed.items[0].bounds.setX(std::nextafter(changed.items[0].bounds.x(), 1000.0));
      if (mode == 1) changed.items[0].source_size.setWidth(std::nextafter(changed.items[0].source_size.width(), 10000.0));
      if (mode == 2) changed.items[2].brush_points[1].setX(std::nextafter(changed.items[2].brush_points[1].x(), 1000.0));
      if (mode == 3) changed.items[0].opacity = std::numeric_limits<double>::quiet_NaN();
      if (mode == 4) changed.items[0].bounds.setWidth(std::numeric_limits<double>::infinity());
      compareToSerialization(baseline,changed,"exact floating-point geometry");
    }
    CarouselDocumentController::RecipeState recipe; recipe.exposure = 0.65;
    const auto patch = serialize_adjustment_patch_json(recipe, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
    baseline.items[0].adjustment_patch_json = patch;
    auto changed = baseline;
    changed.items[0].adjustment_patch_json = QString::fromUtf8(QJsonDocument::fromJson(patch.toUtf8()).toJson(QJsonDocument::Indented));
    compareToSerialization(baseline,changed,"equivalent adjustment patch formatting");
    require(CarouselDocumentController::documentSnapshotsEqual(baseline,changed), "Equivalent patch formatting created a change");
    auto passthrough_items = baseline.passthrough.value("items").toArray();
    auto stored = passthrough_items[0].toObject();
    stored.insert("futureField",QJsonObject{{"preserve",true}});
    stored.insert("adjustmentPatch",QJsonDocument::fromJson(patch.toUtf8()).object());
    passthrough_items[0] = stored;
    baseline.passthrough.insert("items",passthrough_items);
    changed = baseline;
    changed.items[0].adjustment_patch_json.clear();
    compareToSerialization(baseline,changed,"existing optional-field passthrough semantics");
    require(CarouselDocumentController::documentSnapshotsEqual(baseline,changed), "Optional-field passthrough semantics changed");
    changed = baseline;
    changed.items.swapItemsAt(0,1);
    compareToSerialization(baseline,changed,"reordered IDs with unknown fields");
    changed = baseline;
    changed.items.removeLast();
    compareToSerialization(baseline,changed,"removed item");
    changed = baseline;
    changed.passthrough.insert("futureRoot",true);
    compareToSerialization(baseline,changed,"changed passthrough root");
    changed = baseline;
    changed.passthrough.insert("documentName",QStringLiteral("overridden by known metadata"));
    compareToSerialization(baseline,changed,"passthrough overridden by known metadata");
    subject.rememberSavedContent();
    const auto saved = subject.persistentDocumentObject();
    subject.selected_item_id_ = subject.items_.last().id;
    subject.selected_item_ids_ = {subject.selected_item_id_};
    subject.current_index_ = 1;
    subject.active_slot_ = QStringLiteral("secondary");
    subject.updateDocumentDirty();
    require(!subject.document_dirty_ && subject.persistentDocumentObject() == saved,
            "Selection-only changes marked the saved document dirty");
    subject.items_[0].rotation_degrees = 15;
    subject.updateDocumentDirty();
    require(subject.document_dirty_, "Persisted edit did not mark saved content dirty");
    subject.items_[0].rotation_degrees = 0;
    subject.updateDocumentDirty();
    require(!subject.document_dirty_, "Returning to saved content did not clear dirty state");
    subject.closeCarousel();
    require(!subject.saved_content_values_ && subject.saved_content_snapshot_.isEmpty(),
            "Closed document retained a saved comparison baseline");
  }
  static double p95(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return samples[static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(samples.size()))) - 1U];
  }
  static QJsonObject measureComparisons(CarouselDocumentController& controller) {
    QJsonObject result;
    const auto original = controller.documentObject();
    controller.rememberSavedContent();
    controller.updateDocumentDirty();
    require(!controller.document_dirty_, "Captured saved baseline was dirty");
    for (const auto changed_index : {-1, 0, static_cast<int>(controller.items_.size()) - 1}) {
      std::vector<double> dirty_samples, transaction_samples;
      for (int iteration = 0; iteration < 12; ++iteration) {
        const auto previous_rotation = changed_index >= 0 ? controller.items_.at(changed_index).rotation_degrees : 0.0;
        if (changed_index >= 0) controller.items_[changed_index].rotation_degrees += 0.25;
        QElapsedTimer timer; timer.start();
        controller.updateDocumentDirty();
        dirty_samples.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
        require(controller.document_dirty_ == (changed_index >= 0), "Saved/dirty comparison changed its decision");
        if (changed_index >= 0) controller.items_[changed_index].rotation_degrees = previous_rotation;
        controller.clearHistory();
        timer.restart();
        controller.beginHistoryTransaction(QStringLiteral("Fixture comparison"));
        if (changed_index >= 0) controller.items_[changed_index].rotation_degrees += 0.25;
        controller.commitHistoryTransaction();
        transaction_samples.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
        require(controller.undo_stack_.size() == (changed_index >= 0 ? 1 : 0), "Transaction comparison changed its decision");
        if (changed_index >= 0) controller.items_[changed_index].rotation_degrees = previous_rotation;
      }
      const auto suffix = changed_index < 0 ? QStringLiteral("unchanged") :
          changed_index == 0 ? QStringLiteral("first_item") : QStringLiteral("last_item");
      result.insert(QStringLiteral("dirty_check_") + suffix + QStringLiteral("_p95_ms"), p95(dirty_samples));
      result.insert(QStringLiteral("transaction_") + suffix + QStringLiteral("_p95_ms"), p95(transaction_samples));
    }
    controller.updateDocumentDirty();
    require(!controller.document_dirty_ && controller.documentObject() == original,
            "Comparison fixture did not restore the exact saved content");
    controller.clearHistory();
    controller.autosave_timer_->stop();
    return result;
  }
  static QJsonObject measure(CarouselDocumentController& controller, const QString& root) {
    std::vector<double> captures, autosaves, completions, heartbeats, restores;
    for (int iteration = 0; iteration < 20; ++iteration) {
      auto* item = &controller.items_[0];
      QElapsedTimer timer; timer.start();
      controller.pushUndoState(QStringLiteral("Fixture edit"));
      captures.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      item->rotation_degrees = static_cast<double>(iteration + 1);
    }
    const auto expected = controller.documentObject();
    controller.document_dirty_ = true;
    controller.autosave_timer_->stop();
    for (int iteration = 0; iteration < 6; ++iteration) {
      controller.save_state_ = QStringLiteral("Unsaved");
      QElapsedTimer timer; timer.start();
      double heartbeat = -1;
      QTimer::singleShot(0, &controller, [&] { heartbeat = static_cast<double>(timer.nsecsElapsed()) / 1e6; });
      controller.writeAutosave();
      autosaves.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      waitAutosave(controller);
      completions.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      QCoreApplication::processEvents();
      require(heartbeat >= 0, "Event-loop heartbeat was not delivered");
      heartbeats.push_back(heartbeat);
      require(controller.save_error_.isEmpty(), "Autosave failed");
    }
    QFile file(controller.autosavePath());
    require(file.open(QIODevice::ReadOnly), "Recovery file not readable");
    const auto recovery = QJsonDocument::fromJson(file.readAll()).object().value("document").toObject();
    require(recovery == expected, "Recovery content changed");
    for (int iteration = 0; iteration < 6; ++iteration) {
      QElapsedTimer timer; timer.start();
      controller.undo(); controller.redo();
      restores.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
    }
    controller.preview_debounce_timer_->stop();
    require(controller.documentObject() == expected, "Undo/redo did not restore the exact document");
    const auto normalized = QJsonDocument(expected).toJson(QJsonDocument::Compact).replace(root.toUtf8(), "<fixture>");
    return {{"capture_p95_ms", p95(captures)}, {"autosave_callback_p95_ms", p95(autosaves)},
      {"autosave_completion_p95_ms", p95(completions)}, {"event_loop_heartbeat_p95_ms", p95(heartbeats)}, {"undo_redo_pair_p95_ms", p95(restores)}, {"document_bytes", normalized.size()}, {"exact_restoration", true},
      {"document_sha256", QString::fromLatin1(QCryptographicHash::hash(normalized,QCryptographicHash::Sha256).toHex())}};
  }
};
}
int main(int argc, char** argv) {
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_CAROUSEL_FIXTURE_HELPER")) return 1;
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QTemporaryDir workspace;
  if (!workspace.isValid()) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", workspace.path().toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", workspace.path().toUtf8());
  qputenv("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH", workspace.filePath(QStringLiteral("carousels")).toUtf8());
  QApplication app(argc, argv);
  const bool verify = argc == 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--verify");
  if (!verify && argc != 3) return 2;
  bool valid = verify;
  const auto boards = verify ? 1 : QString::fromLocal8Bit(argv[1]).toInt(&valid);
  if (!valid || boards < 1 || boards > 200) return 2;
  const auto image_path = workspace.filePath(QStringLiteral("source.png"));
  QImage image(1024,768,QImage::Format_RGB32);
  quint32 random = 12345;
  for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
    random = random * 1664525U + 1013904223U;
    image.setPixel(x,y,0xff000000U | (random & 0x00ffffffU));
  }
  if (!image.save(image_path)) return 2;
  QFile source(image_path);
  if (!source.open(QIODevice::ReadOnly) || !source.setFileTime(QDateTime::fromMSecsSinceEpoch(1700000000000LL),QFileDevice::FileModificationTime)) return 2;
  source.close();
  try {
    photo_editor::desktop::CarouselDocumentController controller;
    photo_editor::desktop::CarouselHistoryTestAccess::seed(controller,boards,image_path);
    if (verify) {
      photo_editor::desktop::CarouselHistoryTestAccess::verifyAsyncAutosave(image_path);
      photo_editor::desktop::CarouselHistoryTestAccess::verifyEquality(image_path);
      photo_editor::desktop::CarouselHistoryTestAccess::verifySnapshots(image_path);
      photo_editor::desktop::CarouselHistoryTestAccess::verify(controller,image_path);
      std::cout << "carousel_serialization_contract_passed\n";
      return 0;
    }
    auto comparisons = photo_editor::desktop::CarouselHistoryTestAccess::measureComparisons(controller);
    auto report = photo_editor::desktop::CarouselHistoryTestAccess::measure(controller,workspace.path());
    for (auto it = comparisons.constBegin(); it != comparisons.constEnd(); ++it) report.insert(it.key(),it.value());
    report.insert("boards",boards);
#if !defined(_WIN32)
    rusage usage{};
    if (getrusage(RUSAGE_SELF,&usage) != 0) return 3;
    double peak = static_cast<double>(usage.ru_maxrss);
#if !defined(__APPLE__)
    peak *= 1024.0;
#endif
    report.insert("peak_rss_bytes",peak);
#endif
    QFile output(QString::fromLocal8Bit(argv[2]));
    if (!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(report).toJson()) < 0) return 3;
    std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
  return 0;
}
