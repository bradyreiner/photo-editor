#include "gaussian_scene_viewport.h"
#include "gaussian_physical_camera.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QThread>
#include <QtConcurrent>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

using namespace photo_editor::desktop;

struct WorkGate {
  std::mutex mutex;
  std::condition_variable changed;
  bool released = false;
  std::atomic_bool entered{false};
  std::atomic_int active_calls{0}, calls{0};
  std::atomic_bool overlapping_calls{false};
  void block() {
    std::unique_lock lock(mutex);
    entered.store(true);
    changed.wait(lock, [&] { return released; });
  }
  void release() {
    { std::lock_guard lock(mutex); released = true; }
    changed.notify_all();
  }
};
class FixtureRenderer final : public GaussianSceneRenderer {
 public:
  std::atomic_int calls{0};
  std::atomic_bool cancel_next_refinement{false}, cancellation_entered{false}, cancellation_observed{false};
  std::shared_ptr<GaussianSceneRenderer> native;
  std::shared_ptr<WorkGate> gate;
  GaussianFrame renderWithOptions(const std::shared_ptr<const GaussianSceneData>& scene,
      const GaussianCamera& camera, bool readback, std::optional<double> seconds, const GaussianRenderOptions& options) override {
    if (!readback && !options.interactive && cancel_next_refinement.exchange(false)) {
      cancellation_entered.store(true);
      QElapsedTimer timer; timer.start();
      while (options.canceled && !options.canceled->load() && timer.elapsed() < 2000) QThread::msleep(1);
      GaussianFrame frame;
      frame.canceled = options.canceled && options.canceled->load();
      cancellation_observed.store(frame.canceled);
      if (!frame.canceled) frame.error = "Refinement was not canceled.";
      return frame;
    }
    return render(scene, camera, readback, seconds);
  }
  GaussianFrame render(const std::shared_ptr<const GaussianSceneData>& scene,
      const GaussianCamera& camera, bool readback, std::optional<double> scene_seconds = {}) override {
    ++calls;
    struct ActiveCall {
      std::shared_ptr<WorkGate> gate;
      ~ActiveCall() { if (gate) --gate->active_calls; }
    } active{gate};
    if (gate) {
      ++gate->calls;
      if (gate->active_calls.fetch_add(1) != 0) gate->overlapping_calls.store(true);
      gate->block();
    }
    if (native) return native->render(scene, camera, readback, scene_seconds);
    GaussianFrame frame;
    frame.size = camera.image_size;
    frame.metrics.insert("fixtureCall", calls.load());
    if (readback) {
      frame.image = QImage(camera.image_size, QImage::Format_RGB32);
      frame.image.fill(QColor(60, 90, 120));
    }
    return frame;
  }
};

namespace photo_editor::desktop {
struct GaussianSceneViewportTestAccess {
  static void primeFocus(GaussianSceneViewport& viewport, const std::shared_ptr<FixtureRenderer>& renderer, bool too_close = false) {
    prime(viewport, renderer);
    auto scene = std::make_shared<GaussianSceneData>(*viewport.scene_);
    GaussianPoint p; p.center_opacity = {0,0,3,0.9F}; p.covariance_a = {0.01F,0,0,0.01F};
    p.covariance_b = {0,0.0001F,0,0};
    if (too_close) { p.center_opacity[2] = 0.2F; scene->cameras[0].photo.lens.aperture_radius = 0.1F; }
    scene->points = {p}; viewport.scene_ = scene;
    const auto camera = viewport.currentCamera();
    viewport.frame_.native_texture = 1; viewport.frame_.native_lease = std::make_shared<int>(1);
    viewport.frame_.size = camera.image_size; viewport.frame_.metrics.insert("camera", camera.description());
  }
  static void finishFocusWithoutDelivery(GaussianSceneViewport& viewport) { viewport.focus_pick_.waitForFinished(); }
  static void physicalFocus(GaussianSceneViewport& viewport) {
    GaussianPhysicalCameraSettings p; p.metres_per_scene_unit = 0.5; p.focus_distance_m = 1;
    const auto base = viewport.fullCamera();
    viewport.saved_camera_ = *make_gaussian_physical_camera(base,base.image_size,p).camera;
    const auto camera = viewport.currentCamera();
    viewport.frame_.size = camera.image_size; viewport.frame_.metrics["camera"] = camera.description();
  }
  static bool progressivePublicationGuards() {
    for (int mode = 0; mode < 9; ++mode) {
      GaussianSceneViewport viewport;
      viewport.render_active_ = true;
      viewport.render_epoch_ = viewport.epoch_ = 3;
      viewport.submitted_ = viewport.requested_ = 7;
      viewport.render_canceled_ = std::make_shared<std::atomic_bool>(false);
      viewport.progressive_frames_ = std::make_shared<GaussianProgressiveFrames>();
      GaussianFrame partial; partial.native_texture = 1; partial.native_lease = std::make_shared<int>(1);
      partial.size = {64, 64}; partial.metrics.insert("apertureSamples", 4);
      if (mode == 1) ++viewport.epoch_;
      if (mode == 2) ++viewport.requested_;
      if (mode == 3) viewport.render_canceled_->store(true);
      if (mode == 4) viewport.navigating_ = true;
      if (mode == 5) viewport.export_in_flight_ = true;
      if (mode == 6) viewport.render_interactive_ = true;
      if (mode == 7) viewport.setVisible(false);
      if (mode == 8) partial.error = "invalid prefix";
      viewport.progressive_frames_->publish(partial);
      QMetaObject::invokeMethod(&viewport.refinement_progress_timer_, "timeout", Qt::DirectConnection);
      const bool published = viewport.status().value("publishedPhase") == "refining";
      viewport.render_active_ = false; viewport.export_in_flight_ = false;
      if (published != (mode == 0) || viewport.progressive_frames_->takeLatest()) return false;
    }
    return true;
  }
  static void prime(GaussianSceneViewport& viewport, const std::shared_ptr<FixtureRenderer>& renderer,
      const std::shared_ptr<const GaussianSceneData>& loaded = {}) {
    auto scene = std::make_shared<GaussianSceneData>();
    GaussianCamera camera;
    camera.name = "fixture";
    camera.image_size = QSize(800, 600);
    camera.fx = camera.fy = 600;
    camera.cx = 400; camera.cy = 300;
    scene->cameras.push_back(camera);
    scene->artifact_sha256 = "fixture-hash";
    viewport.scene_ = loaded ? loaded : scene;
    viewport.renderer_ = renderer;
  }
  static void finishWorkerWithoutUiDelivery(GaussianSceneViewport& viewport) {
    viewport.render_.waitForFinished();
  }
  static void queueOldLoad(GaussianSceneViewport& viewport) {
    viewport.source_key_ = "old-source";
    viewport.epoch_ = viewport.load_epoch_ = 1;
    viewport.load_active_ = true;
    viewport.canceled_ = std::make_shared<std::atomic_bool>(false);
    viewport.load_.setFuture(QtConcurrent::run([] {
      auto scene = std::make_shared<GaussianSceneData>();
      GaussianCamera camera; camera.name = "old-camera";
      scene->cameras.push_back(camera);
      return GaussianSceneLoad{scene, {}};
    }));
    viewport.load_.waitForFinished();
  }
  static quint64 loadEpoch(const GaussianSceneViewport& viewport) { return viewport.load_epoch_; }
  static QVariant forward(const GaussianSceneViewport& viewport) { return viewport.fullCamera().description().value("forward"); }
  static GaussianCamera camera(const GaussianSceneViewport& viewport) { return viewport.fullCamera(); }
  static std::shared_ptr<std::atomic_bool> blockLoad(GaussianSceneViewport& viewport, const std::shared_ptr<WorkGate>& gate) {
    auto canceled = std::make_shared<std::atomic_bool>(false);
    viewport.canceled_ = canceled;
    viewport.load_active_ = true;
    viewport.load_.setFuture(QtConcurrent::run([gate, canceled] {
      gate->block();
      return GaussianSceneLoad{{}, canceled->load() ? "canceled" : "not canceled"};
    }));
    return canceled;
  }
  static QFuture<GaussianSceneLoad> loading(const GaussianSceneViewport& viewport) { return viewport.load_.future(); }
  static QFuture<GaussianFrame> rendering(const GaussianSceneViewport& viewport) { return viewport.render_.future(); }
  static std::shared_ptr<std::atomic_bool> cancellation(const GaussianSceneViewport& viewport) { return viewport.canceled_; }
  static void clearQueuedScene(GaussianSceneViewport& viewport) {
    viewport.source_key_ = "fixture-source";
    viewport.setSource({});
  }
};
}

static bool wait_for(const std::function<bool()>& condition) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!condition() && elapsed.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return condition();
}

static qint64 destroy_with_deadline(std::unique_ptr<GaussianSceneViewport>& viewport, const std::shared_ptr<WorkGate>& gate) {
  // The old blocking destructor is released after 500ms, so a failing-before
  // regression terminates instead of deadlocking the test runner.
  std::thread deadline([gate] {
    std::unique_lock lock(gate->mutex);
    gate->changed.wait_for(lock, std::chrono::milliseconds(500), [&] { return gate->released; });
    gate->released = true;
    lock.unlock(); gate->changed.notify_all();
  });
  QElapsedTimer timer; timer.start();
  viewport.reset();
  const auto elapsed = timer.nsecsElapsed() / 1000;
  gate->release(); deadline.join();
  return elapsed;
}
static bool progressive_preview_regression() {
  {
    QQuickItem host;
    GaussianSceneViewport viewport(&host);
    viewport.setSize({640, 480});
    const auto renderer = std::make_shared<FixtureRenderer>();
    renderer->cancel_next_refinement.store(true);
    GaussianSceneViewportTestAccess::prime(viewport, renderer);
    viewport.resetCamera();
    if (!wait_for([&] { return renderer->cancellation_entered.load(); })) return false;
    viewport.orbit(1, 1);
    if (!wait_for([&] { return renderer->cancellation_observed.load() && viewport.status().value("previewPhase") == "photo"; }) ||
        !viewport.status().value("error").toString().isEmpty()) return false;
  }
  {
    QQuickItem host;
    GaussianSceneViewport edited(&host), control(&host);
    edited.setVisible(false); control.setVisible(false);
    const auto renderer = std::make_shared<FixtureRenderer>();
    GaussianSceneViewportTestAccess::prime(edited, renderer);
    GaussianSceneViewportTestAccess::prime(control, renderer);
    for (auto* viewport : {&edited, &control}) { viewport->dolly(0.5); viewport->orbit(5, 3); viewport->pan(0.1, 0.2); }
    const GaussianPhotoSettings settings{1.5F, 1, 1};
    if (!edited.setPhotoSettings(settings.description()).value("ok").toBool()) return false;
    edited.orbit(2, 1); control.orbit(2, 1);
    auto actual = GaussianSceneViewportTestAccess::camera(edited);
    actual.photo = {};
    if (actual.description() != GaussianSceneViewportTestAccess::camera(control).description()) return false;
  }
  QQuickItem host;
  GaussianSceneViewport viewport(&host);
  viewport.setSize(QSizeF(1920, 1080));
  const auto failed = [&](int stage) {
    std::cerr << "progressive stage " << stage << ": " << QJsonDocument::fromVariant(viewport.status()).toJson().constData();
    return false;
  };
  auto renderer = std::make_shared<FixtureRenderer>();
  GaussianSceneViewportTestAccess::prime(viewport, renderer);
  viewport.beginNavigation();
  viewport.orbit(10, 5);
  if (!wait_for([&] { return viewport.status().value("publishedPhase") == "live"; })) return failed(1);
  const auto live_camera = viewport.status().value("metrics").toMap().value("camera").toMap();
  if (live_camera.value("width").toInt() != 720) return failed(2);
  QElapsedTimer hold; hold.start();
  wait_for([&] { return hold.elapsed() > 250; });
  if (!viewport.status().value("navigating").toBool() || viewport.status().value("previewPhase") != "live") return failed(3);
  viewport.endNavigation();
  if (!wait_for([&] { return viewport.status().value("previewPhase") == "photo"; })) return failed(4);
  const auto photo_camera = viewport.status().value("metrics").toMap().value("camera").toMap();
  if (photo_camera.value("width").toInt() != 1440 || photo_camera.value("forward") != live_camera.value("forward") ||
      viewport.status().value("navigating").toBool()) return failed(5);
  auto film_photo = *gaussian_photo_settings(viewport.status().value("photoSettings").toMap());
  film_photo.film.grain_amount = 0.5F;
  if (!viewport.setPhotoSettings(film_photo.description()).value("ok").toBool() || viewport.status().value("navigating").toBool() ||
      !wait_for([&] { return viewport.status().value("previewPhase") == "photo"; }) ||
      viewport.status().value("metrics").toMap().value("camera").toMap().value("width").toInt() != 1440) return failed(11);
  // Wheel navigation settles without pointer-release events.
  viewport.dolly(1);
  if (!wait_for([&] { return viewport.status().value("previewPhase") == "photo"; })) return failed(6);
  viewport.beginNavigation(); viewport.pan(.01, .01);
  if (!wait_for([&] { return viewport.status().value("publishedPhase") == "live"; })) return failed(7);
  viewport.setVisible(false); viewport.endNavigation();
  const auto hidden_calls = renderer->calls.load();
  hold.restart(); wait_for([&] { return hold.elapsed() > 250; });
  if (renderer->calls.load() != hidden_calls) return failed(8);
  viewport.setVisible(true);
  if (!wait_for([&] { return viewport.status().value("previewPhase") == "photo"; })) return failed(8);
  // A high-quality frame finishing after a newer camera request must not
  // replace the live view or report the older camera as the settled photo.
  auto gate = std::make_shared<WorkGate>();
  renderer->gate = gate;
  const auto before_stale = viewport.status().value("publishedFrames").toInt();
  viewport.resetCamera();
  if (!wait_for([&] { return gate->entered.load(); })) { gate->release(); return failed(9); }
  const auto stale_forward = GaussianSceneViewportTestAccess::forward(viewport);
  bool stale_published = false;
  const auto publication = QObject::connect(&viewport, &GaussianSceneViewport::statusChanged, &viewport, [&] {
    if (viewport.status().value("publishedFrames").toInt() > before_stale &&
        viewport.status().value("metrics").toMap().value("camera").toMap().value("forward") == stale_forward)
      stale_published = true;
  });
  viewport.orbit(25, 10);
  gate->release();
  const bool settled = wait_for([&] { return viewport.status().value("previewPhase") == "photo"; });
  QObject::disconnect(publication);
  if (!settled || stale_published || gate->overlapping_calls.load()) return failed(10);
  return true;
}
static bool lifetime_regression(const std::shared_ptr<const GaussianSceneData>& scene, bool native) {
  QTemporaryDir outputs;
  bool responsive = true;
  {
    auto viewport = std::make_unique<GaussianSceneViewport>();
    auto gate = std::make_shared<WorkGate>();
    const auto canceled = GaussianSceneViewportTestAccess::blockLoad(*viewport, gate);
    auto future = GaussianSceneViewportTestAccess::loading(*viewport);
    if (!wait_for([&] { return gate->entered.load(); })) { gate->release(); return false; }
    const auto elapsed = destroy_with_deadline(viewport, gate);
    if (!wait_for([&] { return future.isFinished(); }) || !canceled->load() || future.result().error != "canceled") return false;
    std::cout << "load teardown_us=" << elapsed << '\n';
    responsive &= elapsed < 100000;
  }
  for (const auto mode : {QStringLiteral("preview"), QStringLiteral("photo"), QStringLiteral("queued-photo")}) {
    auto gate = std::make_shared<WorkGate>();
    auto renderer = std::make_shared<FixtureRenderer>(); renderer->gate = gate;
    if (native) renderer->native = make_gaussian_scene_renderer();
    const std::weak_ptr<FixtureRenderer> lifetime = renderer;
    auto viewport = std::make_unique<GaussianSceneViewport>();
    viewport->setSize(QSizeF(800, 600));
    GaussianSceneViewportTestAccess::prime(*viewport, renderer, scene);
    const auto photo = QDir(outputs.path()).filePath(mode + ".png");
    if (mode == "photo") {
      if (!viewport->exportPhoto(photo, 256).value("ok").toBool()) return false;
    } else viewport->orbit(0.1, 0.05);
    if (!wait_for([&] { return gate->entered.load(); })) { gate->release(); return false; }
    const auto frozen_forward = GaussianSceneViewportTestAccess::forward(*viewport);
    if (mode == "queued-photo") {
      if (!viewport->exportPhoto(photo, 256).value("ok").toBool()) { gate->release(); return false; }
      viewport->orbit(20, 10);
      GaussianSceneViewportTestAccess::clearQueuedScene(*viewport);
    }
    auto future = GaussianSceneViewportTestAccess::rendering(*viewport);
    renderer.reset(); // Only submitted work and the viewport now own it.
    const auto elapsed = destroy_with_deadline(viewport, gate);
    if (!wait_for([&] { return future.isFinished(); })) return false;
    future = {};
    if (!wait_for([&] { return lifetime.expired(); })) return false;
    if (gate->active_calls.load() != 0 || gate->overlapping_calls.load() ||
        gate->calls.load() != (mode == "queued-photo" ? 2 : 1)) return false;
    std::cout << mode.toStdString() << " teardown_us=" << elapsed << '\n';
    if (mode != "preview") {
      QFile receipt(photo + ".camera.json");
      if (QImage(photo).size() != QSize(256, 192) && !native) return false;
      if (QImage(photo).isNull() || !receipt.open(QIODevice::ReadOnly)) return false;
      const auto saved = QJsonDocument::fromJson(receipt.readAll()).toVariant().toMap();
      if (saved.value("outputPath") != photo || saved.value("camera").toMap().value("forward") != frozen_forward ||
          saved.value("artifactSha256") != (scene ? scene->artifact_sha256 : QStringLiteral("fixture-hash"))) return false;
    }
    responsive &= elapsed < 100000;
  }
  return responsive;
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  for (const int mode : {0,1,2,3}) {
    GaussianSceneViewport viewport;
    viewport.setSize({640,480});
    auto fixture = std::make_shared<FixtureRenderer>();
    GaussianSceneViewportTestAccess::primeFocus(viewport,fixture,mode == 2);
    if (mode == 3) GaussianSceneViewportTestAccess::physicalFocus(viewport);
    if (viewport.pickFocusAt(-1,0).value("ok").toBool() || !viewport.pickFocusAt(320,240).value("ok").toBool()) return 1;
    GaussianSceneViewportTestAccess::finishFocusWithoutDelivery(viewport);
    if (viewport.pickFocusAt(320,240).value("ok").toBool()) return 1; // Completed but undelivered is still owned.
    if (mode == 1) viewport.orbit(2,0);
    if (!wait_for([&] { return !viewport.status().value("focusPicking").toBool(); })) return 1;
    const auto focus = viewport.status().value("lensSettings").toMap().value("focusDistance").toDouble();
    if (std::abs(focus-(mode == 0 || mode == 3 ? 3.0 : 1.0)) > 1e-6) return 1;
    if (mode == 3) {
      const auto camera = GaussianSceneViewportTestAccess::camera(viewport);
      if (!camera.physical || !gaussian_physical_camera_consistent(camera) ||
          camera.physical->settings.focus_distance_m != 1.5 || camera.physical->settings.f_number != 2.8) return 1;
    }
    if (mode == 2 && (viewport.status().value("focusPickError").toString().isEmpty() ||
        std::abs(viewport.status().value("lensSettings").toMap().value("apertureRadius").toDouble()-0.1) > 1e-6)) return 1;
  }
  {
    GaussianSceneViewport viewport; auto fixture = std::make_shared<FixtureRenderer>();
    GaussianSceneViewportTestAccess::prime(viewport,fixture); viewport.setVisible(false);
    GaussianPhysicalCameraSettings p; p.metres_per_scene_unit = 0.5;
    if (!viewport.setPhysicalCamera(p.description()).value("ok").toBool()) return 1;
    const auto camera = GaussianSceneViewportTestAccess::camera(viewport);
    auto altered = camera.photo; altered.lens.focus_distance *= 2;
    if (viewport.setPhotoSettings(altered.description()).value("ok").toBool()) return 1;
    auto bad = p.description(); bad["metresPerSceneUnit"] = 0;
    if (viewport.setPhysicalCamera(bad).value("ok").toBool() ||
        GaussianSceneViewportTestAccess::camera(viewport).description() != camera.description()) return 1;
    altered = camera.photo; altered.exposure_ev = 1;
    if (!viewport.setPhotoSettings(altered.description()).value("ok").toBool() ||
        GaussianSceneViewportTestAccess::camera(viewport).physical != camera.physical) return 1;
    if (!viewport.clearPhysicalCamera().value("ok").toBool()) return 1;
    const auto cleared = GaussianSceneViewportTestAccess::camera(viewport);
    if (cleared.physical || cleared.fx != camera.fx || cleared.fy != camera.fy || cleared.photo != altered) return 1;
  }
  if (!GaussianSceneViewportTestAccess::progressivePublicationGuards()) return 1;
  if (!progressive_preview_regression()) {
    std::cerr << "Live-to-photo preview must settle automatically, preserve perspective and pause while hidden\n";
    return EXIT_FAILURE;
  }
  QTemporaryDir root;
  {
    GaussianSceneViewport physical_view; auto fixture = std::make_shared<FixtureRenderer>();
    GaussianSceneViewportTestAccess::prime(physical_view,fixture); physical_view.setVisible(false);
    GaussianPhysicalCameraSettings p; p.metres_per_scene_unit = 0.5;
    if (!physical_view.setPhysicalCamera(p.description()).value("ok").toBool()) return 1;
    const auto destination = QDir(root.path()).filePath("physical-frozen.png");
    if (!physical_view.exportPhoto(destination,256).value("ok").toBool()) return 1;
    p.f_number = 8;
    if (!physical_view.setPhysicalCamera(p.description()).value("ok").toBool() ||
        !wait_for([&] { return physical_view.status().value("lastExport").toString() == destination; })) return 1;
    QFile file(destination+".camera.json");
    if (!file.open(QIODevice::ReadOnly)) return 1;
    const auto receipt = QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
    const auto exported = gaussian_saved_camera(receipt.value("camera").toMap());
    if (receipt.value("schema") != "framehouse-gaussian-photo-v3" || !exported || !exported->physical ||
        exported->physical->settings.f_number != 2.8 ||
        GaussianSceneViewportTestAccess::camera(physical_view).physical->settings.f_number != 8) return 1;
  }
  auto renderer = std::make_shared<FixtureRenderer>();
  std::shared_ptr<const GaussianSceneData> scene;
  if (app.arguments().size() == 3 && app.arguments()[1] == "--scene") {
    const QDir project(app.arguments()[2]);
    QFile receipt(project.filePath("scene.json"));
    if (!receipt.open(QIODevice::ReadOnly)) return EXIT_FAILURE;
    const auto hash = QJsonDocument::fromJson(receipt.readAll()).toVariant().toMap().value("artifactValidation").toMap().value("artifactSha256").toString();
    const auto loaded = load_gaussian_scene(project.filePath("trained/scene.ply"), project.filePath("dataset/sparse"), hash);
    if (!loaded.scene) { std::cerr << loaded.error.toStdString(); return EXIT_FAILURE; }
    scene = loaded.scene;
    renderer->native = make_gaussian_scene_renderer();
    if (!renderer->native) return EXIT_FAILURE;
    std::cout << "Real scene points: " << scene->points.size() << '\n';
    auto loading_view = std::make_unique<GaussianSceneViewport>();
    loading_view->setSource({{"state", "trained"}, {"projectPath", project.path()},
        {"artifactPath", project.filePath("trained/scene.ply")}, {"artifactValidation", QVariantMap{{"artifactSha256", hash}}}});
    auto loading = GaussianSceneViewportTestAccess::loading(*loading_view);
    const auto canceled = GaussianSceneViewportTestAccess::cancellation(*loading_view);
    QThread::msleep(10); // Exercise the actual file worker, without delivering its UI completion.
    const bool already_finished = loading.isFinished();
    QElapsedTimer teardown; teardown.start();
    loading_view.reset();
    const auto microseconds = teardown.nsecsElapsed() / 1000;
    if (!canceled || !canceled->load() || microseconds >= 100000 || !wait_for([&] { return loading.isFinished(); }) ||
        (!already_finished && loading.result().scene)) return EXIT_FAILURE;
    std::cout << "Real source load teardown_us=" << microseconds << " already_finished=" << already_finished << '\n';
  }
  if (!lifetime_regression(scene, static_cast<bool>(renderer->native))) {
    std::cerr << "Viewport teardown must not wait for workers or lose accepted photo exports\n";
    return EXIT_FAILURE;
  }
  if (app.arguments().contains("--lifetime")) return EXIT_SUCCESS;
  GaussianSceneViewport viewport;
  viewport.setSize(QSizeF(800, 600));
  GaussianSceneViewportTestAccess::prime(viewport, renderer, scene);
  const auto initial_forward = GaussianSceneViewportTestAccess::forward(viewport);
  const auto photo = QDir(root.path()).filePath("view.png");
  if (!viewport.exportPhoto(photo, 256).value("ok").toBool()) return EXIT_FAILURE;
  GaussianSceneViewportTestAccess::finishWorkerWithoutUiDelivery(viewport);
  // The PNG is already written, but its GUI completion is still queued.
  const GaussianPhotoSettings adjusted{1.5F, 1.25F, 1};
  if (!viewport.setPhotoSettings(adjusted.description()).value("ok").toBool()) return EXIT_FAILURE;
  const auto invalid = QVariantMap{{"schema", "invalid"}};
  if (viewport.setPhotoSettings(invalid).value("ok").toBool() ||
      viewport.status().value("photoSettings").toMap() != adjusted.description()) return EXIT_FAILURE;
  for (int i = 0; i < 100; ++i) viewport.orbit(0.1, 0.05);
  bool reentrant_camera_change = false;
  const auto connection = QObject::connect(&viewport, &GaussianSceneViewport::statusChanged, &app, [&] {
    if (!reentrant_camera_change && viewport.status().value("lastExport").toString() == photo) {
      reentrant_camera_change = true;
      viewport.orbit(1, 0);
    }
  });
  if (!wait_for([&] { return viewport.status().value("lastExport").toString() == photo; })) {
    std::cerr << "completed photo export must retain its completion during camera changes\n";
    return EXIT_FAILURE;
  }
  if (!wait_for([&] { return viewport.status().value("state") == "ready"; }) ||
      renderer->calls.load() != 2 || viewport.status().value("exporting").toBool() ||
      QImage(photo).isNull() || !QFileInfo::exists(photo + ".camera.json")) {
    std::cerr << "camera changes must coalesce behind the export without losing its files or status\n";
    return EXIT_FAILURE;
  }
  const auto exported = viewport.status().value("lastExportCamera").toMap();
  const auto displayed = viewport.status().value("metrics").toMap().value("camera").toMap();
  if (exported.value("forward") == displayed.value("forward") ||
      exported.value("forward") != initial_forward || exported.contains("photo") ||
      displayed.value("photo").toMap() != adjusted.description()) {
    std::cerr << "export must keep its frozen camera while preview advances to the newest camera\n";
    return EXIT_FAILURE;
  }
  QObject::disconnect(connection);
  {
    bool stale_camera_published = false;
    GaussianSceneViewport switching;
    QObject::connect(&switching, &GaussianSceneViewport::statusChanged, &app, [&] {
      stale_camera_published |= switching.status().value("cameras").toStringList().contains("old-camera");
    });
    GaussianSceneViewportTestAccess::queueOldLoad(switching);
    switching.setSource({{"state", "trained"}, {"projectPath", root.path()},
        {"artifactPath", QDir(root.path()).filePath("trained/scene.ply")},
        {"artifactValidation", QVariantMap{{"artifactSha256", "new-source-hash"}}}});
    if (GaussianSceneViewportTestAccess::loadEpoch(switching) != 1 ||
        !wait_for([&] { return switching.status().value("state") == "failed"; }) || stale_camera_published) {
      std::cerr << "source switch must consume the canceled load before starting and reporting the new one\n";
      return EXIT_FAILURE;
    }
  }
  std::cout << "Gaussian viewport: queued export completion, 100-change coalescing, reentrant camera change, frozen export camera and source switch pass.\n";
  return EXIT_SUCCESS;
}
