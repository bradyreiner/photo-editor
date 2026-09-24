#include "editor_preview_controller.h"
#include "editor_close_coordinator.h"
#include "app_bootstrap.h"
#include "catalog_recipe_projection.h"
#include "catalog_helper_payload.h"
#include "photo_render_utils.h"
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QTcpServer>
#include <QThread>
#include <iostream>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static EditRecipeState state(const EditorPreviewController& c) { return c.currentState(); }
  static void pauseSaveDebounce(EditorPreviewController& c) { c.persist_timer_.stop(); }
  static int retryInterval(const EditorPreviewController& c) { return c.persist_timer_.interval(); }
  static QString journalDirectory(const EditorPreviewController& c) { return QFileInfo(c.recipe_save_journal_.path()).absolutePath(); }
  static bool measureCheckpoint(EditorPreviewController& c) {
    for (int index = 0; index < 3; ++index) {
      QElapsedTimer timer; timer.start();
      if (!c.checkpointRecipeSaves()) return false;
      std::cout << "Local checkpoint bytes=" << QFileInfo(c.recipe_save_journal_.path()).size()
                << " elapsed_ms=" << timer.elapsed() << '\n';
    }
    return true;
  }
  static bool usesStdin(const EditorPreviewController& c) {
    return c.persist_process_.arguments().contains(QStringLiteral("--stdin-json-v1")) &&
        c.persist_process_.arguments().join(' ').size() < 1024;
  }
  static void addLargeRasterMask(EditorPreviewController& c) {
    c.addMask(QStringLiteral("brush"));
    auto state = c.currentState();
    auto mask = state.mask_stack.last().toMap();
    mask.insert(QStringLiteral("segmentationWidth"), 1024);
    mask.insert(QStringLiteral("segmentationHeight"), 1024);
    QByteArray alpha(1024 * 1024, '\0');
    quint32 noise = 0x12345678U;
    for (qsizetype i = 0; i < alpha.size(); ++i) {
      noise ^= noise << 13;
      noise ^= noise >> 17;
      noise ^= noise << 5;
      alpha[i] = static_cast<char>(noise & 255U);
    }
    mask.insert(QStringLiteral("segmentationData"), alpha);
    state.mask_stack.last() = mask;
    c.applyState(state);
    c.persistCurrentState();
  }
  static bool idle(const EditorPreviewController& c) {
    return !c.recipe_save_queue_.pending() && !c.recipe_save_queue_.active() &&
        !c.persist_timer_.isActive() && c.persist_process_.state() == QProcess::NotRunning &&
        !c.processing() && !c.interactive_render_watcher_.isRunning() && !c.full_render_watcher_.isRunning();
  }
};
}
using namespace photo_editor::desktop;

namespace {
QByteArray command(const QString& program, const QStringList& args, bool* ok) {
  QProcess process;
  process.start(program, args);
  *ok = process.waitForFinished(30000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
  if (!*ok) std::cerr << program.toStdString() << ": " << process.readAllStandardError().toStdString() << '\n';
  return process.readAllStandardOutput();
}
bool wait_idle(EditorPreviewController& controller) {
  QElapsedTimer timer; timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (EditorPreviewControllerTestAccess::idle(controller)) return true;
    QThread::msleep(5);
  } while (timer.elapsed() < 30000);
  std::cerr << "Save/render timeout: " << controller.status().toStdString() << '\n';
  return false;
}
struct PrivatePostgres {
  QString bin, data;
  QTemporaryDir* root = nullptr;
  bool running = false;
  ~PrivatePostgres() {
    if (!running) return;
    bool ok = false;
    command(bin + "/pg_ctl", {"-D", data, "-m", "fast", "-w", "stop"}, &ok);
    std::cout << "Private PostgreSQL stopped=" << ok << '\n';
    if (!ok && root != nullptr) {
      root->setAutoRemove(false);
      std::cerr << "Retained private cluster for cleanup: " << data.toStdString() << '\n';
    }
  }
};
}

int main(int argc, char** argv) {
  if (argc == 6 && QString::fromLocal8Bit(argv[1]) == "--crash-edit") {
    // Only the parent-created private catalog/runtime is inherited. No cluster
    // setup or destructor-driven save is performed in this killed process.
    QGuiApplication app(argc, argv);
    QFile recipe(QString::fromLocal8Bit(argv[4]));
    if (!recipe.open(QIODevice::ReadOnly)) return 1;
    EditorPreviewController controller;
    controller.loadAsset(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]),
        {}, {}, {}, {}, QString::fromUtf8(recipe.readAll()));
    controller.setExposure(2.25);
    EditorPreviewControllerTestAccess::pauseSaveDebounce(controller);
    if (!controller.recipeSaveError().isEmpty() || !controller.recipeSavePending()) return 1;
    QSaveFile expected(QString::fromLocal8Bit(argv[5]));
    const auto bytes = serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(controller)).toUtf8();
    if (!expected.open(QIODevice::WriteOnly) || expected.write(bytes) != bytes.size() || !expected.commit()) return 1;
    std::cout << "CRASH_READY\n" << std::flush;
    return app.exec();
  }
  // This test always creates its own cluster; never accepts an existing DB URL.
  const auto bin = qEnvironmentVariable("PHOTO_EDITOR_TEST_POSTGRES_BIN_DIR");
  if (bin.isEmpty()) return 77;
  qputenv("PHOTO_EDITOR_POSTGRES_BIN_DIR", bin.toUtf8());
  QTemporaryDir root(QStringLiteral("/private/tmp/framehouse-recipe-pg-XXXXXX"));
  if (!root.isValid()) return 1;
  qunsetenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("runtime").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_BACKUP_DIR", root.filePath("backups").toUtf8());
  qputenv("PHOTO_EDITOR_MANAGED_MEDIA_DIR", root.filePath("managed").toUtf8());
  qunsetenv("PHOTO_EDITOR_ORIGINALS_BACKUP_DIR");
  QGuiApplication app(argc, argv);
  QTcpServer reservation;
  if (!reservation.listen(QHostAddress::LocalHost, 0)) return 1;
  const auto port = reservation.serverPort();
  reservation.close();
  PrivatePostgres pg{bin, root.filePath("postgres"), &root};
  bool ok = false;
  command(bin + "/initdb", {"-D", pg.data, "-U", "photo_test", "-A", "trust", "--no-locale", "-E", "UTF8"}, &ok);
  if (!ok) return 1;
  pg.running = true;
  command(bin + "/pg_ctl", {"-D", pg.data, "-l", root.filePath("postgres.log"), "-o",
      QString("-h 127.0.0.1 -p %1 -k %2").arg(port).arg(root.path()), "-w", "start"}, &ok);
  if (!ok) return 1;
  std::cout << "Private PostgreSQL port=" << port << '\n';
  const auto url = QString("postgresql://photo_test@127.0.0.1:%1/postgres").arg(port);
  qputenv("PHOTO_EDITOR_DATABASE_URL", url.toUtf8());
  QDir().mkpath(root.filePath("media"));
  const auto a_name = argc > 1 ? QStringLiteral("a.dng") : QStringLiteral("a.jpg");
  const auto a = root.filePath("media/" + a_name), b = root.filePath("media/b.jpg");
  QImage image(48, 32, QImage::Format_RGB32); image.fill(QColor("#a05030"));
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  if (argc > 1) {
    if (!QFile::copy(QString::fromLocal8Bit(argv[1]), a)) return 1;
  } else if (!image.save(a)) {
    return 1;
  }
  image.fill(QColor("#3050a0"));
  if (!image.save(b)) return 1;
  command(AppBootstrap::helperProgram("import_catalog"), AppBootstrap::helperArguments("import_catalog",
      {root.filePath("media"), "proof-drive", "Persistence proof", "referenced"}), &ok);
  if (!ok) return 1;
  const auto sql = [&](const QString& query) {
    return command(bin + "/psql", {url, "-X", "-A", "-t", "-v", "ON_ERROR_STOP=1", "-c", query}, &ok).trimmed();
  };
  const auto id_a = QString::fromUtf8(sql(QString("SELECT asset_id FROM assets WHERE relative_path='%1'").arg(a_name)));
  if (!ok || id_a.isEmpty()) return 1;
  const auto id_b = QString::fromUtf8(sql("SELECT asset_id FROM assets WHERE relative_path='b.jpg'"));
  if (!ok || id_b.isEmpty()) return 1;
  EditRecipeState expected_a, expected_b;
  {
    EditorPreviewController controller;
    controller.loadAsset(id_a, a, {}, {}, {}, {}, {});
    const auto linked = argc > 1 ? QString::fromLocal8Bit(argv[1]) : b;
    if (!controller.addSourceLayer(linked, "Persisted source")) return 1;
    controller.setPhotoLayerTransform(0, 3, 4, 0.75, 12);
    controller.setPhotoLayerMaskEnabled(0, true);
    controller.setPhotoLayerMaskOpacity(0, 0.6);
    controller.addMask(QStringLiteral("linear"));
    controller.setActiveMaskExposure(0.4);
    controller.setActiveMaskTemperature(12.0);
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_LARGE_RECIPE")) {
      EditorPreviewControllerTestAccess::addLargeRasterMask(controller);
    }
    controller.setExposure(1.25);
    expected_a = EditorPreviewControllerTestAccess::state(controller);
    const auto recipe_bytes = serialize_edit_recipe_json(expected_a).toUtf8().size();
    std::cout << "Serialized source recipe bytes=" << recipe_bytes << '\n';
    if (!EditorPreviewControllerTestAccess::measureCheckpoint(controller)) return 1;
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_LARGE_RECIPE") && recipe_bytes < 512 * 1024) return 1;
    if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_LARGE_RECIPE")) {
      QProcess legacy;
      legacy.start(AppBootstrap::helperProgram("save_edit_recipe"), AppBootstrap::helperArguments(
          "save_edit_recipe", {id_a, serialize_edit_recipe_json(expected_a), "", "", ""}));
      const auto started = legacy.waitForStarted(2000);
      std::cout << "Legacy argv helper started=" << started
                << " error=" << legacy.errorString().toStdString() << '\n';
      if (started) {
        legacy.closeWriteChannel();
        if (!legacy.waitForFinished(5000)) { legacy.kill(); legacy.waitForFinished(1000); }
      }
    }
    // No event-loop turn between the edit and document switch: the debounce
    // cannot save A first, which reproduced the old active-document bug.
    controller.loadAsset(id_b, b, {}, {}, {}, {}, {});
    controller.setExposure(-0.75);
    expected_b = EditorPreviewControllerTestAccess::state(controller);
    if (!wait_idle(controller)) return 1;
    if (!EditorPreviewControllerTestAccess::usesStdin(controller)) return 1;
    sql("CREATE FUNCTION proof_reject_save() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'injected save failure'; END $$; "
        "CREATE TRIGGER proof_reject_save BEFORE INSERT OR UPDATE ON edit_recipes FOR EACH ROW EXECUTE FUNCTION proof_reject_save()");
    if (!ok) return 1;
    bool observed_failure = false;
    QObject::connect(&controller, &EditorPreviewController::recipeSaveStateChanged, &controller, [&] {
      observed_failure = observed_failure || !controller.recipeSaveError().isEmpty();
    });
    controller.setExposure(-1.5);
    QElapsedTimer failure_timer; failure_timer.start();
    while (!observed_failure && failure_timer.elapsed() < 15000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(5);
    }
    if (!observed_failure) return 1;
    if (EditorPreviewControllerTestAccess::retryInterval(controller) != 2000) return 1;
    failure_timer.restart();
    while (EditorPreviewControllerTestAccess::retryInterval(controller) != 4000 && failure_timer.elapsed() < 10000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(5);
    }
    if (EditorPreviewControllerTestAccess::retryInterval(controller) != 4000 || controller.recipeSaveError().isEmpty()) return 1;
    const auto last_saved = sql(QString("SELECT recipe_json::text FROM edit_recipes WHERE asset_id='%1'").arg(id_b));
    if (!ok || parse_edit_recipe_json(QString::fromUtf8(last_saved)).exposure != -0.75) return 1;
    sql("DROP TRIGGER proof_reject_save ON edit_recipes; DROP FUNCTION proof_reject_save()");
    if (!ok) return 1;
    controller.setExposure(-2.0);
    expected_b = EditorPreviewControllerTestAccess::state(controller);
    controller.loadAsset(id_a, a, {}, {}, {}, {}, {});
    if (!wait_idle(controller)) return 1;
    std::cout << "Injected PostgreSQL save failure preserved the previous commit and retried the newest asset-bound edit.\n";
  }
  const auto reopen = [&](const QString& id, const QString& path, const EditRecipeState& expected) {
    const auto payload = sql(QString(
        "SELECT json_build_object('legacy',e.recipe_json,'canonical',json_build_object("
        "'recipe_payload',c.recipe_payload,'cache_key',c.cache_key,'provenance_payload',c.provenance_payload::text)) "
        "FROM edit_recipes e JOIN canonical_edit_recipes_v13 c USING(asset_id) WHERE e.asset_id='%1'").arg(id));
    if (!ok || payload.isEmpty()) return false;
    const auto object = QJsonDocument::fromJson(payload).object();
    const auto projection = project_catalog_adjustments_for_desktop(object.value("legacy"), object.value("canonical"),
        root.filePath("runtime/recipe-assets"));
    if (!projection.ok()) { std::cerr << projection.error.toStdString() << '\n'; return false; }
    EditorPreviewController reopened;
    reopened.loadAsset(id, path, {}, {}, {}, {}, projection.adjustment_patch_json);
    if (!wait_idle(reopened)) return false;
    const auto restored = EditorPreviewControllerTestAccess::state(reopened);
    const auto expected_json = serialize_edit_recipe_json(expected);
    const auto restored_json = serialize_edit_recipe_json(restored);
    if (expected_json != restored_json) {
      const auto e = QJsonDocument::fromJson(expected_json.toUtf8()).object();
      const auto r = QJsonDocument::fromJson(restored_json.toUtf8()).object();
      for (auto it = e.begin(); it != e.end(); ++it)
        if (r.value(it.key()) != it.value()) {
          std::cerr << "Changed field: " << it.key().toStdString() << '\n';
          if (it.key() == "cropTransform") std::cerr
              << QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact).toStdString() << " -> "
              << QJsonDocument(r.value(it.key()).toObject()).toJson(QJsonDocument::Compact).toStdString() << '\n';
        }
      return false;
    }
    QImage base; photo_editor::pipeline::DecodedImageMetadata metadata;
    if (!decode_source_image(path, &base, 128, expected, &metadata)) return false;
    const auto before = render_output_image_with_backend(base, expected, QColorSpace(QColorSpace::SRgb),
        QImage::Format_ARGB32, RenderBackendPreference::CpuOnly, &metadata);
    const auto after = render_output_image_with_backend(base, restored, QColorSpace(QColorSpace::SRgb),
        QImage::Format_ARGB32, RenderBackendPreference::CpuOnly, &metadata);
    return !before.isNull() && before == after;
  };
  if (!reopen(id_a, a, expected_a) || !reopen(id_b, b, expected_b)) return 1;
  std::cout << "Real PostgreSQL rapid-switch save and fresh-controller canonical reopen preserved both recipes and render pixels.\n";
  const auto crash_input = root.filePath("crash-input.json");
  const auto crash_expected = root.filePath("crash-expected.json");
  {
    QSaveFile input(crash_input);
    const auto bytes = serialize_edit_recipe_json(expected_a).toUtf8();
    if (!input.open(QIODevice::WriteOnly) || input.write(bytes) != bytes.size() || !input.commit()) return 1;
  }
  QProcess crashed;
  crashed.start(app.applicationFilePath(), {"--crash-edit", id_a, a, crash_input, crash_expected});
  if (!crashed.waitForStarted(5000)) return 1;
  QByteArray crash_output;
  QElapsedTimer crash_timer; crash_timer.start();
  while (!crash_output.contains("CRASH_READY") && crash_timer.elapsed() < 30000) {
    crashed.waitForReadyRead(100);
    crash_output += crashed.readAllStandardOutput();
    if (crashed.state() == QProcess::NotRunning) break;
  }
  if (!crash_output.contains("CRASH_READY")) {
    std::cerr << crashed.readAllStandardError().toStdString() << '\n';
    return 1;
  }
  crashed.kill();
  if (!crashed.waitForFinished(3000) || crashed.exitStatus() != QProcess::CrashExit) return 1;
  const auto before_recovery = sql(QString("SELECT recipe_json::text FROM edit_recipes WHERE asset_id='%1'").arg(id_a));
  if (!ok || parse_edit_recipe_json(QString::fromUtf8(before_recovery)).exposure != expected_a.exposure) return 1;
  QFile expected_file(crash_expected);
  if (!expected_file.open(QIODevice::ReadOnly)) return 1;
  expected_a = parse_edit_recipe_json(QString::fromUtf8(expected_file.readAll()));
  // The first reopen receives the OLD committed catalog recipe. Recovery must
  // override it visibly, then retry the database save. The second reads the new
  // canonical commit with an empty journal.
  if (!reopen(id_a, a, expected_a) || !reopen(id_a, a, expected_a)) return 1;
  std::cout << "Killed editor before debounce: stale catalog stayed unchanged until reopen; local recipe recovered, committed canonically and preserved render pixels on both reopens.\n";
  // Synthetic approval metadata tests transaction replay safety, NOT the
  // photographic quality or human approval of a rendering migration.
  const auto migration_payload = sql(QString(
      "SELECT json_build_object('legacy',e.recipe_json::text,'canonical',c.recipe_payload,"
      "'key',c.cache_key,'provenance',c.provenance_payload::text) "
      "FROM edit_recipes e JOIN canonical_edit_recipes_v13 c USING(asset_id) WHERE e.asset_id='%1'").arg(id_b));
  if (!ok) return 1;
  const auto migration = QJsonDocument::fromJson(migration_payload).object();
  const QByteArray evidence("{\"testOnly\":\"migration-acknowledgement-replay\"}");
  QStringList migration_args{id_b, migration.value("legacy").toString(), migration.value("canonical").toString(),
      migration.value("key").toString(), migration.value("provenance").toString(), "12", "legacy-v1", "aces2-ocio-v1",
      "[]", "test-plan", QString::fromLatin1(QCryptographicHash::hash(evidence, QCryptographicHash::Sha256).toHex()),
      "2026-09-07T12:00:00Z", QString::fromUtf8(evidence)};
  const auto accept_migration = [&](const QStringList& args, bool expected_success) {
    QProcess helper;
    QString error;
    if (!start_catalog_helper_stdin(&helper, AppBootstrap::helperProgram("accept_recipe_migration"),
        AppBootstrap::helperArguments("accept_recipe_migration", {"--stdin-json-v1"}), args, &error)) return false;
    const bool success = helper.waitForFinished(30000) && helper.exitStatus() == QProcess::NormalExit && helper.exitCode() == 0;
    if (success != expected_success) std::cerr << helper.readAllStandardError().toStdString() << '\n';
    return success == expected_success;
  };
  if (!accept_migration(migration_args, true)) return 1;
  const auto original_record = sql(QString("SELECT row_to_json(r)::text FROM recipe_migration_records r WHERE asset_id='%1'").arg(id_b));
  if (!ok || original_record.isEmpty()) return 1;
  const auto rollback = QJsonDocument::fromJson(original_record).object().value("rollback_pointer").toString();
  if (!QFileInfo(rollback).isFile() || QFileInfo(rollback).size() <= 0) return 1;
  {
    EditorPreviewController later;
    later.loadAsset(id_b, b, {}, {}, {}, {}, serialize_edit_recipe_json(expected_b));
    later.setExposure(-3.0);
    expected_b = EditorPreviewControllerTestAccess::state(later);
    if (!wait_idle(later)) return 1;
  }
  if (!accept_migration(migration_args, true) || !reopen(id_b, b, expected_b)) return 1;
  if (sql(QString("SELECT row_to_json(r)::text FROM recipe_migration_records r WHERE asset_id='%1'").arg(id_b)) != original_record || !ok) return 1;
  auto conflicting = migration_args;
  conflicting[9] = "different-plan";
  if (!accept_migration(conflicting, false) || !reopen(id_b, b, expected_b)) return 1;
  sql(QString("UPDATE recipe_migration_records SET status='rolled_back' WHERE asset_id='%1'").arg(id_b));
  if (!ok || !accept_migration(migration_args, false) || !reopen(id_b, b, expected_b)) return 1;
  sql(QString("UPDATE recipe_migration_records SET status='accepted',evidence_hash='newer-decision',approval_time='2026-09-08T12:00:00Z' WHERE asset_id='%1'").arg(id_b));
  if (!ok || !accept_migration(migration_args, false) || !reopen(id_b, b, expected_b)) return 1;
  sql(QString("UPDATE recipe_migration_records SET status='candidate',evidence_hash='%1',approval_time=NULL WHERE asset_id='%2'")
      .arg(migration_args.at(10), id_b));
  if (!ok || !accept_migration(migration_args, true)) return 1;
  expected_b = parse_edit_recipe_json(migration_args.at(1));
  if (!reopen(id_b, b, expected_b)) return 1;
  std::cout << "Migration acknowledgement replay preserved later edits and the original rollback record; conflicting and rolled-back approvals were rejected.\n";
  sql("CREATE FUNCTION proof_delay_save() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN PERFORM pg_sleep(5); RETURN NEW; END $$; "
      "CREATE TRIGGER proof_delay_save BEFORE INSERT OR UPDATE ON edit_recipes FOR EACH ROW EXECUTE FUNCTION proof_delay_save()");
  if (!ok) return 1;
  const auto before_close_exposure = expected_a.exposure;
  {
    EditorPreviewController closing;
    closing.loadAsset(id_a, a, {}, {}, {}, {}, serialize_edit_recipe_json(expected_a));
    closing.setExposure(-1.1);
    expected_a = EditorPreviewControllerTestAccess::state(closing);
    if (expected_a.exposure == before_close_exposure || !closing.recipeSavePending()) return 1;
    QElapsedTimer starting; starting.start();
    while (closing.recipeSaveProcessForClose()->state() != QProcess::Running && starting.elapsed() < 10000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5);
    }
    if (closing.recipeSaveProcessForClose()->state() != QProcess::Running) return 1;
    bool sleeping_transaction = false;
    while (!sleeping_transaction && starting.elapsed() < 10000) {
      sleeping_transaction = sql("SELECT count(*) FROM pg_stat_activity WHERE wait_event='PgSleep'").toInt() > 0;
      if (!ok) return 1;
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5);
    }
    if (!sleeping_transaction) return 1;
    EditorCloseCoordinator close(closing.recipeSaveProcessForClose(),
        [&](QString* error) { return closing.prepareRecipeClose(error); },
        [&](bool paused) { closing.pauseRecipePersistence(paused); });
    close.requestClose();
    QElapsedTimer stopping; stopping.start();
    while (!close.ready() && close.error().isEmpty() && stopping.elapsed() < 4000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5);
    }
    if (!close.ready() || closing.recipeSaveProcessForClose()->state() != QProcess::NotRunning) return 1;
  }
  const auto after_stopped_save = sql(QString("SELECT recipe_json::text FROM edit_recipes WHERE asset_id='%1'").arg(id_a));
  if (!ok || parse_edit_recipe_json(QString::fromUtf8(after_stopped_save)).exposure != before_close_exposure) return 1;
  sql("DROP TRIGGER proof_delay_save ON edit_recipes; DROP FUNCTION proof_delay_save()");
  if (!ok || !reopen(id_a, a, expected_a) || !reopen(id_a, a, expected_a)) return 1;
  {
    EditorPreviewController closing;
    closing.loadAsset(id_a, a, {}, {}, {}, {}, serialize_edit_recipe_json(expected_a));
    closing.beginCoalescedEdit();
    closing.setExposure(-1.3);
    QString error;
    if (closing.prepareRecipeClose(&error) || error.isEmpty()) return 1;
    closing.endCoalescedEdit();
    expected_a = EditorPreviewControllerTestAccess::state(closing);
    if (expected_a.exposure != -1.3 || !closing.recipeSavePending()) return 1;
    const auto directory = EditorPreviewControllerTestAccess::journalDirectory(closing);
    const auto held = directory + "-held";
    if (!QDir().rename(directory, held)) return 1;
    EditorCloseCoordinator close(closing.recipeSaveProcessForClose(),
        [&](QString* message) { return closing.prepareRecipeClose(message); },
        [&](bool paused) { closing.pauseRecipePersistence(paused); });
    const auto preview_status_before_failure = closing.status();
    close.requestClose();
    const bool blocked = !close.ready() && !close.error().isEmpty() && !close.closing() &&
        closing.status() == preview_status_before_failure;
    if (!QDir().rename(held, directory)) return 1;
    if (!blocked) { std::cerr << "Failed checkpoint did not block close\n"; return 1; }
    close.requestClose();
    QElapsedTimer retry; retry.start();
    while (!close.ready() && retry.elapsed() < 4000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5);
    }
    if (!close.ready()) return 1;
  }
  if (!reopen(id_a, a, expected_a) || !reopen(id_a, a, expected_a)) return 1;
  {
    EditorPreviewController closing;
    closing.loadAsset(id_a, a, {}, {}, {}, {}, serialize_edit_recipe_json(expected_a));
    closing.setExposure(-1.5);
    expected_a = EditorPreviewControllerTestAccess::state(closing);
    if (expected_a.exposure != -1.5 || !closing.recipeSavePending()) return 1;
    QElapsedTimer starting; starting.start();
    while (closing.recipeSaveProcessForClose()->state() != QProcess::Running && starting.elapsed() < 10000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2);
    }
    if (closing.recipeSaveProcessForClose()->state() != QProcess::Running) return 1;
    EditorCloseCoordinator close(closing.recipeSaveProcessForClose(),
        [&](QString* message) { return closing.prepareRecipeClose(message); },
        [&](bool paused) { closing.pauseRecipePersistence(paused); });
    close.requestClose();
    QElapsedTimer grace; grace.start();
    while (!close.ready() && grace.elapsed() < 4000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2);
    }
    if (!close.ready() || closing.recipeSavePending() ||
        closing.recipeSaveProcessForClose()->state() != QProcess::NotRunning) return 1;
  }
  if (!reopen(id_a, a, expected_a)) return 1;
  {
    EditorPreviewController closing;
    closing.loadAsset(id_a, a, {}, {}, {}, {}, serialize_edit_recipe_json(expected_a));
    closing.setExposure(-1.7);
    expected_a = EditorPreviewControllerTestAccess::state(closing);
    if (expected_a.exposure != -1.7 || !closing.recipeSavePending()) return 1;
    QElapsedTimer starting; starting.start();
    while (closing.recipeSaveProcessForClose()->state() != QProcess::Running && starting.elapsed() < 10000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2);
    }
    if (closing.recipeSaveProcessForClose()->state() != QProcess::Running) return 1;
    const auto directory = EditorPreviewControllerTestAccess::journalDirectory(closing);
    const auto held = directory + "-held";
    if (!QDir().rename(directory, held)) return 1;
    const bool acknowledged = wait_idle(closing) && !closing.recipeSavePending() && !closing.recipeSaveError().isEmpty();
    QString error;
    const bool blocked = !closing.prepareRecipeClose(&error) && !error.isEmpty();
    if (!QDir().rename(held, directory)) return 1;
    if (!acknowledged || !blocked || !closing.prepareRecipeClose(&error) || !closing.recipeSaveError().isEmpty()) {
      std::cerr << "Acknowledged save with failed outbox cleanup was not safely blocked/retried\n";
      return 1;
    }
  }
  if (!reopen(id_a, a, expected_a)) return 1;
  std::cout << "Coordinated close stopped a delayed real save helper, retained/recovered the recipe, blocked an unfinished edit and failed checkpoint, and retried safely.\n";
  return 0;
}
