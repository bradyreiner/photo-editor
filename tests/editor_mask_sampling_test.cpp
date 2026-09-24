#include "editor_preview_controller.h"
#include "photo_render_utils.h"
#include "recipe_mask_sampling.h"
#include "mask_outline_presentation.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QSemaphore>
#include <QJsonDocument>
#include <QtConcurrent/QtConcurrentRun>
#include <iostream>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static void setup(EditorPreviewController& c, const QString& path, const EditRecipeState& state) {
    c.asset_id_ = QStringLiteral("overlay-proof");
    c.file_path_ = path;
    c.applyState(state);
  }
  static EditRecipeState state(const EditorPreviewController& c) { return c.currentState(); }
  static QVariantMap mask(const EditorPreviewController& c) { return c.activeMaskDefinition(); }
  static int stale(const EditorPreviewController& c) {
    return c.mask_worker_coordinator_.activeMaskOutlineStaleResultCount();
  }
  static const ActiveMaskPreviewWorker& preview(const EditorPreviewController& c) {
    return c.mask_worker_coordinator_.activeMaskPreviewWorker();
  }
  static QThreadPool* outline_pool(EditorPreviewController& c) {
    return c.mask_worker_coordinator_.outlineWorkerPool();
  }
};
}
using namespace photo_editor::desktop;

bool check_small_outline_worker(const QString& path, const QString& kind) {
  EditRecipeState state;
  state.render_intent = QStringLiteral("Relative Colorimetric");
  state.mask_stack = {QVariantMap{{"id", "small-mask"}, {"maskSetId", "small-set"},
      {"toolRole", "selection"}, {"kind", kind}, {"operation", "add"},
      {"exposure", 0.75}, {"temperature", 12.0}, {"feather", 0.3},
      {"position", 0.4}, {"rotation", 7.0}, {"radiusX", 0.3}}};
  state.active_mask_index = 0;
  EditorPreviewController controller;
  EditorPreviewControllerTestAccess::setup(controller, path, state);
  auto* pool = EditorPreviewControllerTestAccess::outline_pool(controller);
  pool->setMaxThreadCount(1);
  QSemaphore entered, release;
  auto blocker = QtConcurrent::run(pool, [&] { entered.release(); release.acquire(); });
  const bool blocked = entered.tryAcquire(1, 5000);
  QElapsedTimer admission;
  admission.start();
  const auto pending = controller.activeMaskOutlineTrace();
  const auto admission_ms = admission.nsecsElapsed() / 1000000.0;
  release.release();
  blocker.waitForFinished();
  if (!blocked || pending.value("outline_execution_mode") != "async_worker" ||
      !pending.value("outline_worker_pending").toBool()) {
    std::cerr << "Small-image outline must queue behind a held worker, not rasterize inline\n";
    return false;
  }
  state = EditorPreviewControllerTestAccess::state(controller);
  QImage decoded;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  if (!decode_source_image(path, &decoded, 512, state, &metadata)) return false;
  const auto target = EditorPreviewControllerTestAccess::mask(controller);
  const auto sampling = prepare_recipe_mask_sampling(decoded, state, metadata, target,
      crop_rect_for_image(decoded.size(), state));
  if (!sampling.valid) return false;
  const auto mask = compile_pipeline_local_mask(target,
      LocalMaskRasterPolicy::RebuildEditableAtOutputResolution, QSize(sampling.range.width, sampling.range.height));
  const auto expected = sampling.evaluate({mask});
  if (!expected.valid) return false;
  QElapsedTimer wait;
  wait.start();
  while (wait.elapsed() < 20000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    const auto trace = controller.activeMaskOutlineTrace();
    if (trace.value("outline_worker_alpha_hash").toString() == expected.alpha_hash &&
        !controller.activeMaskOutlinePaths().isEmpty() &&
        EditorPreviewControllerTestAccess::stale(controller) == 0) {
      std::cout << "small_" << kind.toStdString() << "_outline queued=1 alpha_exact=1 admission_ms=" << admission_ms << '\n';
      return true;
    }
    QThread::msleep(1);
  }
  std::cerr << "Small outline did not settle: expected=" << expected.alpha_hash.toStdString()
            << " trace=" << QJsonDocument::fromVariant(controller.activeMaskOutlineTrace()).toJson(QJsonDocument::Compact).constData() << '\n';
  return false;
}

bool check(const QString& path, const QString& profile, bool crop) {
  EditRecipeState state;
  state.working_color_profile = profile;
  state.exposure = 0.4;
  state.temperature = 18;
  state.tint = -8;
  state.contrast = 25;
  state.saturation = 20;
  if (crop) {
    state.crop_rect_x = 0.2; state.crop_rect_y = 0.1;
    state.crop_rect_width = 0.6; state.crop_rect_height = 0.7;
  }
  state.mask_stack = {
      QVariantMap{{"id", "first"}, {"maskSetId", "first-set"}, {"kind", "luminance"},
          {"luminanceMin", 0.0}, {"luminanceMax", 1.0}, {"exposure", 0.8}},
      QVariantMap{{"id", "target"}, {"maskSetId", "target-set"}, {"kind", "luminance"},
          {"luminanceMin", 0.15}, {"luminanceMax", 0.6}, {"rangeFeather", 0.12},
          {"maskSetDensity", 0.7}, {"maskSetOpacity", 0.8}}};
  state.active_mask_index = 1;
  EditorPreviewController controller;
  EditorPreviewControllerTestAccess::setup(controller, path, state);
  controller.setMaskPreviewMode(QStringLiteral("color-on-bw"));
  controller.setMaskPreviewOverlayColor(QStringLiteral("#527396"));
  int notifications = 0;
  QObject::connect(&controller, &EditorPreviewController::maskOverlayChanged, &controller, [&] { ++notifications; });
  (void)controller.activeMaskOutlinePaths(); // Old color recipe in flight.
  controller.setContrast(-35);
  if (notifications == 0) return false;
  state = EditorPreviewControllerTestAccess::state(controller);
  QImage decoded;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  if (!decode_source_image(path, &decoded, 512, state, &metadata)) return false;
  if (path.endsWith(".dng", Qt::CaseInsensitive) &&
      !photo_editor::pipeline::decoded_image_requires_engine_input_transform(metadata)) return false;
  const auto target = EditorPreviewControllerTestAccess::mask(controller);
  const auto sampling = prepare_recipe_mask_sampling(decoded, state, metadata, target,
      crop_rect_for_image(decoded.size(), state));
  const auto mask = compile_pipeline_local_mask(target,
      LocalMaskRasterPolicy::RebuildEditableAtOutputResolution, QSize(sampling.range.width, sampling.range.height));
  const auto alpha = sampling.evaluate({mask});
  if (!alpha.valid) return false;
  QElapsedTimer admission;
  admission.start();
  auto source = controller.activeMaskPreviewSource();
  const auto admission_ms = admission.nsecsElapsed() / 1000000.0;
  if (!source.isEmpty() || admission_ms > 33) {
    std::cerr << "Uncached raster admission should return pending within 33 ms: " << admission_ms << '\n';
    return false;
  }
  QElapsedTimer raster_wait;
  raster_wait.start();
  while (source.isEmpty() && raster_wait.elapsed() < 20000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    source = controller.activeMaskPreviewSource();
    if (source.isEmpty()) QThread::msleep(5);
  }
  const auto& preview_worker = EditorPreviewControllerTestAccess::preview(controller);
  if (preview_worker.started != 1 || preview_worker.published != 1 || preview_worker.last_worker_thread ==
      QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)) return false;
  const auto image = QImage::fromData(QByteArray::fromBase64(source.section(',', 1).toLatin1()), "PNG");
  if (image.size() != alpha.size) {
    std::cerr << "Overlay size=" << image.width() << 'x' << image.height() << " expected="
              << alpha.size.width() << 'x' << alpha.size.height()
              << " mode=" << controller.maskPreviewMode().toStdString()
              << " role=" << controller.activeMaskToolRole().toStdString()
              << " hidden=" << controller.maskPreviewTemporarilyHidden()
              << " source=" << source.left(90).toStdString() << '\n';
    return false;
  }
  int covered = 0;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      const auto expected = static_cast<int>(std::lround(
          static_cast<unsigned char>(alpha.alpha_mask[y * image.width() + x]) * 0.42));
      const auto pixel = image.pixelColor(x, y);
      if (pixel.alpha() != expected || (expected > 0 && pixel.name() != "#527396")) {
        std::cerr << "Overlay RGBA mismatch at " << x << ',' << y << " alpha=" << pixel.alpha()
                  << " expected=" << expected << " color=" << pixel.name().toStdString() << '\n';
        return false;
      }
      covered += expected > 0;
    }
  if (covered == 0) return false;
  QElapsedTimer timer;
  timer.start();
  QVariantMap trace;
  while (timer.elapsed() < 20000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    trace = controller.activeMaskOutlineTrace();
    if (trace.value("outline_worker_alpha_hash").toString() == alpha.alpha_hash) break;
    QThread::msleep(5);
  }
  if (trace.value("outline_worker_alpha_hash").toString() != alpha.alpha_hash ||
      !trace.value("outline_mask_input_contract").toString().contains("stack-prefix") ||
      EditorPreviewControllerTestAccess::stale(controller) == 0) {
    std::cerr << "Outline mismatch/stale guard: " << trace.value("outline_trace_source").toString().toStdString()
              << " hash=" << trace.value("outline_worker_alpha_hash").toString().toStdString()
              << " stale=" << EditorPreviewControllerTestAccess::stale(controller) << '\n';
    return false;
  }
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_TEST_OVERLAY_OUTPUT");
  if (!output.isEmpty() && profile == "Linear ProPhoto RGB" && !crop &&
      (!image.save(output + "-overlay.png") || !grayscale_mask_alpha_image(alpha).save(output + "-matte.png"))) return false;
  controller.setActiveMaskDensity(0);
  timer.restart();
  while (timer.elapsed() < 20000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    trace = controller.activeMaskOutlineTrace();
    if (trace.value("outline_worker_alpha_valid").toBool() &&
        trace.value("outline_worker_alpha_max").toInt() == 0 &&
        trace.value("outline_worker_alpha_hash").toString() != alpha.alpha_hash) break;
    QThread::msleep(5);
  }
  if (!trace.value("outline_worker_alpha_valid").toBool() ||
      trace.value("outline_worker_alpha_max").toInt() != 0 ||
      !controller.activeMaskOutlinePaths().isEmpty()) {
    std::cerr << "A valid empty mask must not display a fallback outline\n";
    return false;
  }
  // Restoring a recipe with the same IDs and global color must reject the
  // in-flight mask content, even without another getter between restore and
  // worker completion (the undo/redo publication boundary).
  const auto discarded_before = preview_worker.discarded;
  EditorPreviewControllerTestAccess::setup(controller, path, state);
  (void)controller.activeMaskPreviewSource();
  auto restored = state;
  auto restored_target = restored.mask_stack[1].toMap();
  restored_target.insert("density", 0.0);
  restored.mask_stack[1] = restored_target;
  EditorPreviewControllerTestAccess::setup(controller, path, restored);
  timer.restart();
  while (preview_worker.running() && timer.elapsed() < 20000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(1);
  }
  if (preview_worker.discarded <= discarded_before) return false;
  source = controller.activeMaskPreviewSource();
  timer.restart();
  while (source.isEmpty() && timer.elapsed() < 20000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    source = controller.activeMaskPreviewSource();
    QThread::msleep(1);
  }
  const auto restored_image = QImage::fromData(QByteArray::fromBase64(source.section(',', 1).toLatin1()), "PNG");
  if (restored_image.isNull()) return false;
  for (int y = 0; y < restored_image.height(); ++y)
    for (int x = 0; x < restored_image.width(); ++x)
      if (restored_image.pixelColor(x, y).alpha() != 0) return false;
  std::cout << path.toStdString() << " " << profile.toStdString() << " crop=" << crop << " raw=" << metadata.is_raw
            << " exact fill alpha/color and worker alpha hash; empty outline verified; stale=" << EditorPreviewControllerTestAccess::stale(controller)
            << " admission_ms=" << admission_ms << " raster_worker_ms=" << preview_worker.last_elapsed_ms << '\n';
  return true;
}

int main(int argc, char** argv) {
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("runtime").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  const QVariantMap geometric{{"kind", "rectangle"}, {"operation", "add"}};
  if (!clean_canvas_outline_matches_final_single_mask(geometric)) return 1;
  for (const auto* key : {"opacity", "density", "maskSetOpacity", "maskSetDensity"}) {
    auto changed = geometric;
    changed.insert(QString::fromLatin1(key), 0.0);
    if (clean_canvas_outline_matches_final_single_mask(changed)) return 1;
  }
  auto inverted = geometric;
  inverted.insert("maskSetSelectionInvert", true);
  if (clean_canvas_outline_matches_final_single_mask(inverted)) return 1;
  QImage image(128, 96, QImage::Format_RGB32);
  image.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x)
      image.setPixelColor(x, y, QColor(10 + x, 5 + y * 2, 35 + x));
  const auto path = root.filePath("gradient.jpg");
  if (!image.save(path)) return 1;
  for (const auto* kind : {"linear", "radial"})
    if (!check_small_outline_worker(path, QString::fromLatin1(kind))) return 1;
  QStringList paths{path};
  for (int i = 1; i < argc; ++i) paths.append(QString::fromLocal8Bit(argv[i]));
  for (const auto& photo : paths)
    for (const auto* profile : {"Linear sRGB", "Linear Display P3", "Linear Rec.2020", "Linear ProPhoto RGB"})
      for (const auto crop : {false, true})
        if (!check(photo, QString::fromLatin1(profile), crop)) return 1;
  {
    EditRecipeState inspection_state;
    inspection_state.mask_stack = {QVariantMap{{"id", "inspection"}, {"maskSetId", "inspection-set"},
        {"kind", "subject"}, {"segmentationWidth", 2}, {"segmentationHeight", 2},
        {"segmentationData", QByteArray::fromHex("00ff00ff")}}};
    inspection_state.active_mask_index = 0;
    EditorPreviewController inspection;
    EditorPreviewControllerTestAccess::setup(inspection, path, inspection_state);
    QString bound_source;
    QObject::connect(&inspection, &EditorPreviewController::controlsChanged, &inspection,
        [&] { bound_source = inspection.activeMaskPreviewSource(); });
    const auto wait_for_binding = [&] {
      QElapsedTimer timer;
      timer.start();
      while (bound_source.isEmpty() && timer.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
      }
      return !QImage::fromData(QByteArray::fromBase64(bound_source.section(',', 1).toLatin1()), "PNG").isNull();
    };
    for (const auto* mode : {"color-on-bw", "image-on-bw", "grayscale", "black-on-white",
                            "white-on-black", "on-black", "on-white"}) {
      inspection.setMaskPreviewMode(QString::fromLatin1(mode));
      if (!bound_source.isEmpty() || !wait_for_binding()) return 1;
    }
    inspection.setActiveMaskSelectionInverted(true);
    if (!wait_for_binding()) return 1;
    const auto refined = bound_source;
    inspection.setMaskRefinementPreviewMode(QStringLiteral("original"));
    if (!wait_for_binding() || bound_source == refined) return 1;
    inspection.setMaskRefinementPreviewMode(QStringLiteral("refined"));
    if (!wait_for_binding() || bound_source != refined) return 1;
    inspection.setMaskPreviewMode(QStringLiteral("overlay"));
    if (!bound_source.isEmpty()) return 1;
    std::cout << "Seven raster modes, notify-driven publication and original/refined cache return passed\n";
  }
  {
    EditRecipeState large_state;
    large_state.mask_stack = {QVariantMap{{"id", "large"}, {"maskSetId", "large-set"},
        {"kind", "subject"}, {"segmentationWidth", 4096}, {"segmentationHeight", 4096},
        {"segmentationData", QByteArray(4096 * 4096, char(0x7f))}}};
    large_state.active_mask_index = 0;
    EditorPreviewController large_controller;
    EditorPreviewControllerTestAccess::setup(large_controller, path, large_state);
    large_controller.setMaskPreviewMode(QStringLiteral("color-on-bw"));
    QElapsedTimer admission;
    admission.start();
    const auto source = large_controller.activeMaskPreviewSource();
    const auto admission_ms = admission.nsecsElapsed() / 1000000.0;
    if (!source.isEmpty() || admission_ms > 33) return 1;
    std::cout << "16 MiB mask payload admission_ms=" << admission_ms << '\n';
  }
  return 0;
}
