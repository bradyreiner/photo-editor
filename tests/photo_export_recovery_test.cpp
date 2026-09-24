#include "editor_preview_controller.h"
#include <QBuffer>
#include <QColorSpace>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QSemaphore>
#include <QElapsedTimer>
#include <QThread>
#include <QFile>
#include <QTimer>
#include <QThreadPool>
#include <QSettings>
#include <stdexcept>
#include <iostream>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static void set_layer(EditorPreviewController& controller, const QImage& image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    static_cast<void>(controller.layer_session_.replaceLayers({QVariantMap{
        {"id", "export-fixture"}, {"kind", "pixel"}, {"pixelData", bytes},
        {"pixelWidth", image.width()}, {"pixelHeight", image.height()}, {"opacity", 1.0}}}));
  }
  static bool start(EditorPreviewController& controller, const QString& path, std::function<QImage()> producer) {
    return controller.startPhotoExport(path, std::move(producer));
  }
  static void source(EditorPreviewController& controller, const QString& path) { controller.file_path_ = path; }
  static void clear_layers(EditorPreviewController& controller) {
    static_cast<void>(controller.layer_session_.replaceLayers({}));
  }
};
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/framehouse_export_proof");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, runtime.filePath("preferences"));
  QImage image(64, 48, QImage::Format_ARGB32);
  image.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  image.fill(QColor(80, 120, 150, 128));
  photo_editor::desktop::EditorPreviewController controller;
  using namespace photo_editor::desktop;
  using Access = EditorPreviewControllerTestAccess;
  const auto wait = [](const auto& predicate, qint64 timeout_ms = 10000) {
    QElapsedTimer timeout; timeout.start();
    while (!predicate() && timeout.elapsed() < timeout_ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
      QThread::msleep(1);
    }
    return predicate();
  };
  photo_editor::desktop::EditorPreviewControllerTestAccess::set_layer(controller, image);
  for (const auto& format : {QStringLiteral("png"), QStringLiteral("jpg")}) {
    const auto path = runtime.filePath("layer." + format);
    if (!controller.exportPhotoLayer(0, path)) return 2;
    const QImage decoded(path);
    const bool profile_ok = decoded.colorSpace() == image.colorSpace();
    std::cout << "format=" << format.toStdString() << " profile_preserved=" << profile_ok << '\n';
    if (!profile_ok || decoded.size() != image.size()) return 1;
    if (format == QStringLiteral("png") && decoded != image) return 1;
    if (format == QStringLiteral("jpg") && decoded.hasAlphaChannel()) return 1;
    if (format == QStringLiteral("jpg")) {
      const auto pixel = decoded.pixelColor(16, 16);
      if (std::abs(pixel.red() - 167) > 3 || std::abs(pixel.green() - 187) > 3 ||
          std::abs(pixel.blue() - 202) > 3) return 1;
    }
  }

  const auto snapshot_path = runtime.filePath("snapshot.png");
  if (!controller.startPhotoLayerExport(0, snapshot_path)) return 1;
  QImage replacement = image;
  replacement.fill(Qt::red);
  Access::set_layer(controller, replacement);
  if (!wait([&] { return !controller.photoExportBusy(); }) || !controller.photoExportError().isEmpty() ||
      QImage(snapshot_path) != image) return 1;
  std::cout << "snapshot_pixels_preserved=1\n";

  const auto sentinel_path = runtime.filePath("sentinel.png");
  QFile sentinel(sentinel_path);
  if (!sentinel.open(QIODevice::WriteOnly) || sentinel.write("existing-export") != 15) return 2;
  sentinel.close();
  const auto unchanged = [&] {
    QFile file(sentinel_path);
    return file.open(QIODevice::ReadOnly) && file.readAll() == "existing-export";
  };
  auto cancellation = std::make_shared<PhotoExportControl>();
  const auto cancelled = run_photo_export([image] { return image; }, sentinel_path, "png", cancellation,
      [cancellation] { static_cast<void>(cancellation->cancel()); });
  if (!cancelled.cancelled || cancelled.success || !unchanged()) return 1;
  const auto failed = run_photo_export([image] { return image; }, sentinel_path, "png",
      std::make_shared<PhotoExportControl>(), [] { throw std::runtime_error("injected publication failure"); });
  if (failed.success || failed.error.isEmpty() || !unchanged()) return 1;
  const auto invalid = run_photo_export([image] { return image; }, sentinel_path, "invalid",
      std::make_shared<PhotoExportControl>());
  if (invalid.success || !unchanged()) return 1;
  std::cout << "cancel_failure_preserve_destination=1\n";

  auto entered = std::make_shared<std::atomic<bool>>(false);
  auto release = std::make_shared<QSemaphore>();
  int ticks = 0;
  QTimer timer;
  timer.setInterval(1);
  QObject::connect(&timer, &QTimer::timeout, &app, [&] { ++ticks; });
  timer.start();
  if (!Access::start(controller, sentinel_path, [entered, release, image] {
        entered->store(true);
        if (!release->tryAcquire(1, 10000)) return QImage{};
        return image;
      })) return 1;
  if (!wait([&] { return entered->load() && ticks >= 10; }) ||
      controller.startPhotoLayerExport(0, runtime.filePath("second.png")) || !controller.cancelPhotoExport()) {
    release->release(); return 1;
  }
  release->release();
  if (!wait([&] { return !controller.photoExportBusy(); }) || !unchanged() ||
      controller.photoExportError() != QStringLiteral("Export cancelled.")) return 1;
  std::cout << "worker_keeps_event_loop_alive=1 single_flight=1 cancellation=1\n";

  const auto source_path = runtime.filePath("source.png");
  if (!image.save(source_path)) return 2;
  Access::source(controller, source_path);
  if (controller.startPhotoLayerExport(0, source_path) || controller.exportPhotoLayer(0, source_path) ||
      controller.exportFlattenedPhoto(source_path) || QImage(source_path) != image) return 1;
#if defined(Q_OS_UNIX)
  const auto alias_path = runtime.filePath("source-alias.png");
  if (!QFile::link(source_path, alias_path) || controller.startPhotoLayerExport(0, alias_path) ||
      controller.exportPhotoLayer(0, alias_path) || QImage(source_path) != image) return 1;
#endif
  std::cout << "original_overwrite_rejected=1\n";

  const auto photographic_source = app.arguments().size() > 1 ? app.arguments()[1] : source_path;
  if (!QFileInfo::exists(photographic_source)) return 2;
  Access::source(controller, photographic_source);
  Access::clear_layers(controller);
  const auto reference_path = runtime.filePath("flattened-reference.png");
  const auto flattened_path = runtime.filePath("flattened-worker.png");
  QElapsedTimer flattened_timer;
  flattened_timer.start();
  if (!controller.exportFlattenedPhoto(reference_path)) {
    std::cerr << "flattened_reference_failed source=" << photographic_source.toStdString() << '\n';
    return 1;
  }
  std::cout << "flattened_reference_ms=" << flattened_timer.elapsed() << std::endl;
  flattened_timer.restart();
  if (!controller.startFlattenedPhotoExport(flattened_path)) {
    std::cerr << "flattened_admission_failed error=" << controller.photoExportError().toStdString() << '\n';
    return 1;
  }
  // Full-resolution photographic rendering can exceed the small-fixture wait.
  // This is an output-equality check, not a realtime performance assertion.
  if (!wait([&] { return !controller.photoExportBusy(); }, 45000) || !controller.photoExportError().isEmpty()) {
    std::cerr << "flattened_worker_failed busy=" << controller.photoExportBusy()
              << " elapsed_ms=" << flattened_timer.elapsed()
              << " error=" << controller.photoExportError().toStdString() << '\n';
    return 1;
  }
  std::cout << "flattened_worker_ms=" << flattened_timer.elapsed() << std::endl;
  const QImage reference(reference_path), flattened(flattened_path);
  if (reference.isNull() || reference != flattened || reference.colorSpace() != flattened.colorSpace()) return 1;
  std::cout << "flattened_snapshot_exact=1 width=" << flattened.width() << " height=" << flattened.height() << '\n';

  {
    EditorPreviewController typography_controller;
    QImage text_source(400, 200, QImage::Format_ARGB32);
    text_source.fill(QColor("#202020"));
    text_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const auto text_source_path = runtime.filePath("text-source.png");
    if (!text_source.save(text_source_path)) return 1;
    Access::source(typography_controller, text_source_path);
    if (!typography_controller.addTextLayer(QStringLiteral("Styled caption"), QStringLiteral("Caption")) ||
        !typography_controller.setPhotoLayerTypography(0, {{"size", 27.375}, {"italic", true},
            {"underline", true}, {"tracking", 1.25}, {"color", "#80ff8040"}})) return 1;
    const auto faces = typography_controller.availablePhotoTextFaces(
        typography_controller.photoLayerTypography(0).value("family").toString());
    if (faces.isEmpty() || !typography_controller.setPhotoLayerTypography(0, {{"face", faces.first()}}) ||
        typography_controller.photoLayerTypography(0).value("face").toString() != faces.first()) return 1;
    const auto selected_face_style = typography_controller.photoLayerTypography(0);
    if (typography_controller.setPhotoLayerTypography(0, {{"face", "Unavailable-Face-9475"}}) ||
        typography_controller.photoLayerTypography(0) != selected_face_style) return 1;
    if (!typography_controller.setPhotoLayerTypography(0, {{"italic", !selected_face_style.value("italic").toBool()}}) ||
        !typography_controller.photoLayerTypography(0).value("face").toString().isEmpty()) return 1;
    const auto text_reference = runtime.filePath("text-reference.png");
    const auto text_worker = runtime.filePath("text-worker.png");
    if (!typography_controller.exportFlattenedPhoto(text_reference) ||
        !typography_controller.startFlattenedPhotoExport(text_worker)) return 1;
    // Admission captures the recipe before subsequent editing on the GUI thread.
    if (!typography_controller.setPhotoLayerText(0, QStringLiteral("Changed after export began"))) return 1;
    if (!wait([&] { return !typography_controller.photoExportBusy(); }, 45000) ||
        !typography_controller.photoExportError().isEmpty()) return 1;
    const QImage reference_text(text_reference), worker_text(text_worker);
    if (reference_text.isNull() || worker_text != reference_text ||
        worker_text.colorSpace() != reference_text.colorSpace()) {
      std::cerr << "Styled text worker export must match its captured reference snapshot\n";
      return 1;
    }
    QFile encoded_text(text_worker);
    if (!encoded_text.open(QIODevice::ReadOnly)) return 1;
    const auto png_header = encoded_text.read(29);
    if (png_header.size() != 29 || png_header.mid(12, 4) != "IHDR" ||
        static_cast<unsigned char>(png_header[24]) != 16 || worker_text.depth() != 64) {
      std::cerr << "Styled text PNG export must encode and reopen sixteen-bit channels\n";
      return 1;
    }
    const auto changed_path = runtime.filePath("text-changed.png");
    if (!typography_controller.exportFlattenedPhoto(changed_path) || QImage(changed_path) == worker_text)
      return 1;
    std::cout << "styled_text_worker_snapshot_exact=1\n";
  }

  entered->store(false);
  auto destroyed_done = std::make_shared<std::atomic<bool>>(false);
  auto dying = std::make_unique<EditorPreviewController>();
  if (!Access::start(*dying, sentinel_path, [entered, release, destroyed_done, image] {
        entered->store(true);
        const bool released = release->tryAcquire(1, 10000);
        destroyed_done->store(true);
        return released ? image : QImage{};
      }) || !wait([&] { return entered->load(); })) {
    release->release(); return 1;
  }
  QElapsedTimer destruction;
  destruction.start();
  dying.reset();
  const auto destruction_ms = destruction.elapsed();
  release->release();
  if (!wait([&] { return destroyed_done->load(); }) || destruction_ms > 100 || !unchanged()) return 1;
  if (!QThreadPool::globalInstance()->waitForDone(10000)) return 1;
  std::cout << "destruction_nonblocking=1 destruction_ms=" << destruction_ms << '\n';
  return 0;
}
