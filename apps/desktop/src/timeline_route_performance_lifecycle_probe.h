#pragma once

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "catalog_asset_model.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"
#include "workspace_presentation_observer.h"
#include "native_window_readiness.h"

#if defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <mach/task_info.h>
#endif

namespace photo_editor::desktop {

class TimelineRoutePerformanceLifecycleProbe final : public QObject {
 public:
  TimelineRoutePerformanceLifecycleProbe(
      QApplication* application,
      QObject* root,
      QQuickWindow* window,
      CatalogAssetModel* catalog,
      TimelineDocumentController* timeline,
      TimelinePlaybackController* playback,
      bool* catalog_bootstrap_finished,
      QString output_directory,
      QString source_video_path,
      QString source_image_path,
      QObject* parent = nullptr)
      : QObject(parent),
        application_(application),
        root_(root),
        window_(window),
        catalog_(catalog),
        timeline_(timeline),
        playback_(playback),
        catalog_bootstrap_finished_(catalog_bootstrap_finished),
        output_directory_(std::move(output_directory)),
        source_video_path_(std::move(source_video_path)),
        source_image_path_(std::move(source_image_path)) {
    presentation_observer_ = std::make_unique<WorkspacePresentationObserver>(
        root_, window_, this, [this](quint64 generation) {
          return generation == active_route_generation_ &&
                 routeSurfaceReady(active_route_index_);
        });
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(5);
    connect(&timer_, &QTimer::timeout, this, [this]() { tick(); });
    if (window_ != nullptr) {
      connect(window_, &QWindow::activeChanged, this, [this]() {
        noteForegroundLoss();
      });
      connect(window_, &QWindow::visibleChanged, this, [this]() {
        noteForegroundLoss();
      });
      connect(window_, &QWindow::visibilityChanged, this, [this]() {
        noteForegroundLoss();
      });
      connect(window_, &QWindow::screenChanged, this, [this]() {
        noteForegroundLoss();
      });
    }
    if (application_ != nullptr) {
      connect(application_, &QGuiApplication::applicationStateChanged, this,
              [this](Qt::ApplicationState) { noteForegroundLoss(); });
    }
  }

  static bool writeCommandFailureReceipt(
      const QString& output_directory,
      const QStringList& errors,
      const QVariantMap& inputs = {}) {
    const QFileInfo output_info(output_directory);
    if (!output_info.isAbsolute() || output_info.isSymLink() ||
        !QDir().mkpath(output_directory)) {
      return false;
    }
    const auto receipt_path =
        QDir(output_directory).filePath(QStringLiteral("lifecycle-receipt.json"));
    if (QFileInfo::exists(receipt_path)) {
      return false;
    }
    QSaveFile receipt(receipt_path);
    if (!receipt.open(QIODevice::WriteOnly)) {
      return false;
    }
    const QVariantMap document{
        {QStringLiteral("schema"),
         QStringLiteral("timeline-route-performance-lifecycle:v1")},
        {QStringLiteral("status"), QStringLiteral("failed")},
        {QStringLiteral("ok"), false},
        {QStringLiteral("startedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("completedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("inputs"), inputs},
        {QStringLiteral("errors"), errors},
        {QStringLiteral("routeSamples"), QVariantList{}},
        {QStringLiteral("memorySamples"), QVariantMap{}},
        {QStringLiteral("gates"), QVariantMap{}},
    };
    const auto payload =
        QJsonDocument::fromVariant(document).toJson(QJsonDocument::Indented);
    return receipt.write(payload) == payload.size() && receipt.commit();
  }

  void start() {
    started_at_ = QDateTime::currentDateTimeUtc();
    run_timer_.start();
    if (window_ == nullptr || root_ == nullptr || application_ == nullptr) {
      errors_.push_back(QStringLiteral("The packaged QML application window was unavailable."));
      finish();
      return;
    }
    if (presentation_observer_ == nullptr ||
        !presentation_observer_->installed()) {
      errors_.push_back(
          presentation_observer_ == nullptr
              ? QStringLiteral("The native workspace presentation observer was unavailable.")
              : presentation_observer_->errorMessage());
      finish();
      return;
    }
    if (QFileInfo::exists(receiptPath())) {
      phase_ = Phase::Finished;
      application_->exit(EXIT_FAILURE);
      return;
    }
    activation_timer_.start();
    window_->resize(1440, 900);
    window_->raise();
    window_->requestActivate();
    window_->setOpacity(1.0);
    root_->setProperty("recentUserInput", true);
    timer_.start();
  }

 private:
  enum class Phase {
    WaitingForForeground,
    WaitingForCatalog,
    WaitingForRoute,
    OpeningFirstUsePanels,
    HoldingTimelineInactive,
    WaitingForResumeFrame,
    Finished,
  };

  enum class RoutePurpose { Warmup, FirstTimeline, MeasuredCycle, Inactive, Resume };

  static constexpr int kWarmupCycles = 3;
  static constexpr int kMeasuredCycles = 20;
  static constexpr int kRouteReadyTimeoutMs = 15000;
  static constexpr int kStartupTimeoutMs = 120000;
  static constexpr int kInactiveHoldMs = 12000;
  static constexpr qint64 kFirstTimelineReadyLimitMs = 1500;
  static constexpr qint64 kMemoryPlateauLimitBytes = 64LL * 1024LL * 1024LL;
  static constexpr qint64 kWarmRouteP95LimitMs = 250;

  static QString sha256File(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      if (error != nullptr) {
        *error = file.errorString();
      }
      return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
      if (error != nullptr) {
        *error = file.errorString();
      }
      return {};
    }
    return QString::fromLatin1(hash.result().toHex());
  }

  QString receiptPath() const {
    return QDir(output_directory_).filePath(QStringLiteral("lifecycle-receipt.json"));
  }

  QString routeName(int workspace_index) const {
    if (workspace_index == root_->property("editWorkspaceIndex").toInt()) {
      return QStringLiteral("Edit");
    }
    if (workspace_index == root_->property("libraryWorkspaceIndex").toInt()) {
      return QStringLiteral("Library");
    }
    if (workspace_index == root_->property("timelineWorkspaceIndex").toInt()) {
      return QStringLiteral("Timeline");
    }
    return QStringLiteral("Workspace %1").arg(workspace_index);
  }

  bool foregroundEligible() const {
    return application_ != nullptr && window_ != nullptr &&
           window_->isVisible() && window_->isExposed() && window_->isActive() &&
           QGuiApplication::applicationState() == Qt::ApplicationActive;
  }

  void noteForegroundLoss() {
    if (foreground_measurement_started_ && !foregroundEligible()) {
      if (!foreground_lost_) {
        QVariantList windows;
        for (auto* candidate : QGuiApplication::topLevelWindows()) {
          windows.push_back(QVariantMap{{"objectName", candidate->objectName()},
              {"title", candidate->title()}, {"active", candidate->isActive()},
              {"visible", candidate->isVisible()}, {"exposed", candidate->isExposed()}});
        }
        foreground_loss_details_ = {{"elapsedMs", run_timer_.elapsed()},
            {"applicationState", static_cast<int>(QGuiApplication::applicationState())},
            {"windows", windows}};
#ifdef Q_OS_MACOS
        foreground_loss_details_.insert(QStringLiteral("appKit"),
                                        sampleNativeWindowReadiness(window_));
#endif
      }
      foreground_lost_ = true;
    }
  }

  void tick() {
    if (phase_ == Phase::Finished) {
      return;
    }
    if (presentation_observer_ == nullptr ||
        !presentation_observer_->installed()) {
      errors_.push_back(
          presentation_observer_ == nullptr
              ? QStringLiteral("The native workspace presentation observer was unavailable.")
              : presentation_observer_->errorMessage());
      finish();
      return;
    }

    if (phase_ == Phase::WaitingForForeground) {
      if (!foregroundEligible()) {
        if (activation_timer_.elapsed() > 15000) {
          errors_.push_back(QStringLiteral(
              "The app did not remain foreground, exposed, and active during lifecycle startup."));
          finish();
        }
        return;
      }
      foreground_sample_count_ = 1;
#ifdef Q_OS_MACOS
      foreground_start_details_ = sampleNativeWindowReadiness(window_);
#endif
      foreground_eligible_count_ = 1;
      foreground_measurement_started_ = true;
      phase_timer_.start();
      phase_ = Phase::WaitingForCatalog;
      return;
    }

    ++foreground_sample_count_;
    if (foreground_lost_ || !foregroundEligible()) {
      errors_.push_back(QStringLiteral(
          "Foreground eligibility was lost: the window must remain active and exposed while the app is active."));
      finish();
      return;
    }
    ++foreground_eligible_count_;
    QStringList visible_blockers;
    if (root_ != nullptr &&
        root_->property("recoverySurfaceVisible").toBool()) {
      visible_blockers.push_back(QStringLiteral("session recovery"));
    }
    if (root_ != nullptr) {
      auto* first_run_guide = root_->findChild<QObject*>(
          QStringLiteral("firstRunGuideDialog"), Qt::FindChildrenRecursively);
      if (first_run_guide != nullptr &&
          first_run_guide->property("visible").toBool()) {
        visible_blockers.push_back(QStringLiteral("first-run guide"));
      }
    }
    if (!visible_blockers.isEmpty()) {
      errors_.push_back(QStringLiteral(
          "A startup guide or recovery surface covered the measured workspace: %1.")
                            .arg(visible_blockers.join(QStringLiteral(", "))));
      finish();
      return;
    }

    switch (phase_) {
      case Phase::WaitingForForeground:
        return;
      case Phase::WaitingForCatalog:
        tickWaitingForCatalog();
        return;
      case Phase::WaitingForRoute:
        tickWaitingForRoute();
        return;
      case Phase::OpeningFirstUsePanels:
        tickOpeningFirstUsePanels();
        return;
      case Phase::HoldingTimelineInactive:
        tickHoldingTimelineInactive();
        return;
      case Phase::WaitingForResumeFrame:
        tickWaitingForResumeFrame();
        return;
      case Phase::Finished:
        return;
    }
  }

  void tickWaitingForCatalog() {
    if (phase_timer_.elapsed() > kStartupTimeoutMs) {
      errors_.push_back(QStringLiteral("Timed out waiting for packaged catalog startup."));
      finish();
      return;
    }
    if (catalog_bootstrap_finished_ == nullptr || !*catalog_bootstrap_finished_ ||
        catalog_ == nullptr || catalog_->loading()) {
      return;
    }
    if (!catalog_->errorMessage().trimmed().isEmpty()) {
      errors_.push_back(QStringLiteral("Catalog startup failed: %1").arg(catalog_->errorMessage()));
      finish();
      return;
    }
    if (!seedRealFixtures()) {
      finish();
      return;
    }

    const auto edit_index = root_->property("editWorkspaceIndex").toInt();
    const auto library_index = root_->property("libraryWorkspaceIndex").toInt();
    if (edit_index < 0 || library_index < 0 ||
        root_->property("timelineWorkspaceIndex").toInt() < 0) {
      errors_.push_back(QStringLiteral("Main.qml did not expose the expected workspace route indices."));
      finish();
      return;
    }
    warmup_routes_.clear();
    for (int cycle = 0; cycle < kWarmupCycles; ++cycle) {
      warmup_routes_.push_back(edit_index);
      warmup_routes_.push_back(library_index);
    }
    warmup_route_index_ = 0;
    beginRoute(warmup_routes_.at(warmup_route_index_).toInt(), RoutePurpose::Warmup);
  }

  bool seedRealFixtures() {
    if (catalog_ == nullptr || timeline_ == nullptr) {
      errors_.push_back(QStringLiteral("Catalog or Timeline controllers are unavailable."));
      return false;
    }

    QString photo_path = source_image_path_;
    if (photo_path.isEmpty()) {
      photo_path = timeline_->generateVideoPoster(
          source_video_path_, QStringLiteral("route-performance-lifecycle-photo"));
      if (photo_path.isEmpty() || !QFileInfo::exists(photo_path)) {
        errors_.push_back(QStringLiteral(
            "A photo fixture was not supplied and the packaged Timeline could not produce a still from the video source."));
        return false;
      }
      photo_fixture_source_ = QStringLiteral("video-poster");
    } else {
      photo_fixture_source_ = QStringLiteral("supplied-image");
    }

    QVector<CatalogAssetItem> fixtures;
    fixtures.reserve(2);
    CatalogAssetItem photo;
    photo.asset_id = QStringLiteral("timeline-route-lifecycle-photo");
    photo.file_path = QFileInfo(photo_path).canonicalFilePath();
    photo.location_path = QFileInfo(photo.file_path).absolutePath();
    photo.thumbnail_source = QUrl::fromLocalFile(photo.file_path).toString();
    photo.camera_label = QStringLiteral("Native lifecycle fixture");
    photo.media_type = QStringLiteral("photo");
    photo.online = !photo.file_path.isEmpty() && QFileInfo(photo.file_path).isReadable();
    if (!photo.online) {
      errors_.push_back(QStringLiteral("The photo fixture is not readable."));
      return false;
    }
    QImageReader photo_reader(photo.file_path);
    if (!photo_reader.canRead()) {
      errors_.push_back(QStringLiteral("The photo fixture is not decodable."));
      return false;
    }
    fixtures.push_back(photo);

    CatalogAssetItem video;
    video.asset_id = QStringLiteral("timeline-route-lifecycle-video");
    video.file_path = source_video_path_;
    video.location_path = QFileInfo(source_video_path_).absolutePath();
    video.thumbnail_source = QUrl::fromLocalFile(
        timeline_->generateVideoPoster(source_video_path_,
                                       QStringLiteral("route-performance-lifecycle-video")))
                                 .toString();
    video.camera_label = QStringLiteral("Native lifecycle fixture");
    video.media_type = QStringLiteral("video");
    video.duration_seconds = 10.0;
    video.duration_text = QStringLiteral("00:10");
    video.online = QFileInfo(source_video_path_).isReadable();
    if (!video.online) {
      errors_.push_back(QStringLiteral("The 10 second video fixture is not readable."));
      return false;
    }
    fixtures.push_back(video);

    catalog_->replaceAssetsForTesting(fixtures);
    if (!catalog_->selectAssetById(photo.asset_id, true)) {
      errors_.push_back(QStringLiteral("The photo fixture could not be selected as the first Library asset."));
      return false;
    }
    if (timeline_->clipCount() == 0) {
      timeline_->addClipFromGallery(
          video.asset_id,
          source_video_path_,
          QStringLiteral("Lifecycle 10 second source"),
          10.0,
          video.thumbnail_source);
    }
    if (timeline_->clipCount() != 1 ||
        timeline_->currentClip().value(QStringLiteral("filePath")).toString() !=
            source_video_path_) {
      errors_.push_back(QStringLiteral("The real video fixture was not attached to the Timeline document."));
      return false;
    }
    timeline_->selectClip(0);
    timeline_->seekSequenceSeconds(2.0);
    timeline_->setSelectedClipOpacity(0.85);
    QString hash_error;
    const auto photo_sha256 = sha256File(photo.file_path, &hash_error);
    if (photo_sha256.isEmpty()) {
      errors_.push_back(QStringLiteral("Could not hash the photo fixture: %1").arg(hash_error));
      return false;
    }
    const auto video_sha256 = sha256File(source_video_path_, &hash_error);
    if (video_sha256.isEmpty()) {
      errors_.push_back(QStringLiteral("Could not hash the video fixture: %1").arg(hash_error));
      return false;
    }
    fixture_artifacts_ = {
        {QStringLiteral("photo"),
         QVariantMap{{QStringLiteral("path"), photo.file_path},
                     {QStringLiteral("sizeBytes"), QFileInfo(photo.file_path).size()},
                     {QStringLiteral("sha256"), photo_sha256},
                     {QStringLiteral("decodable"), true}}},
        {QStringLiteral("video"),
         QVariantMap{{QStringLiteral("path"), source_video_path_},
                     {QStringLiteral("sizeBytes"), QFileInfo(source_video_path_).size()},
                     {QStringLiteral("sha256"), video_sha256},
                     {QStringLiteral("timelineClipDurationSeconds"),
                      timeline_->currentClip()
                          .value(QStringLiteral("durationSeconds"), QVariant(10.0))
                          .toDouble()}}},
    };
    fixture_seeded_ = true;
    return true;
  }

  bool invokeQml(
      QObject* target,
      const char* method,
      QVariant* returned_value = nullptr) const {
    if (target == nullptr) {
      return false;
    }
    if (returned_value == nullptr) {
      return QMetaObject::invokeMethod(target, method, Qt::DirectConnection);
    }
    QVariant result;
    const auto invoked = QMetaObject::invokeMethod(
        target,
        method,
        Qt::DirectConnection,
        Q_RETURN_ARG(QVariant, result));
    if (invoked) {
      *returned_value = result;
    }
    return invoked;
  }

  bool invokeQml(
      QObject* target,
      const char* method,
      const QVariant& argument,
      QVariant* returned_value) const {
    if (target == nullptr) {
      return false;
    }
    QVariant result;
    const auto invoked = QMetaObject::invokeMethod(
        target,
        method,
        Qt::DirectConnection,
        Q_RETURN_ARG(QVariant, result),
        Q_ARG(QVariant, argument));
    if (invoked && returned_value != nullptr) {
      *returned_value = result;
    }
    return invoked;
  }

  bool routeSurfaceReady(int workspace_index) const {
    if (root_ == nullptr ||
        root_->property("currentWorkspace").toInt() != workspace_index ||
        root_->property("workspaceTransitionActive").toBool()) {
      return false;
    }
    const auto edit_index = root_->property("editWorkspaceIndex").toInt();
    const auto library_index = root_->property("libraryWorkspaceIndex").toInt();
    const auto timeline_index = root_->property("timelineWorkspaceIndex").toInt();
    if (workspace_index == edit_index) {
      auto* edit_workspace = root_->findChild<QObject*>(
          QStringLiteral("editStudioWorkspace"), Qt::FindChildrenRecursively);
      return root_->property("editWorkspaceLoaded").toBool() &&
             edit_workspace != nullptr &&
             edit_workspace->property("pageContentReady").toBool();
    }
    if (workspace_index == library_index) {
      auto* library_workspace = root_->findChild<QObject*>(
          QStringLiteral("libraryStudioWorkspace"), Qt::FindChildrenRecursively);
      return root_->property("libraryWorkspaceLoaded").toBool() &&
             !root_->property("libraryLoadingSceneVisible").toBool() &&
             library_workspace != nullptr;
    }
    if (workspace_index == timeline_index) {
      auto* timeline_view = root_->findChild<QObject*>(
          QStringLiteral("timelineWorkspace"), Qt::FindChildrenRecursively);
      auto* timeline_page = root_->findChild<QObject*>(
          QStringLiteral("timelineEditorPageContent"), Qt::FindChildrenRecursively);
      auto* loading_surface = root_->findChild<QObject*>(
          QStringLiteral("timelineWorkspaceLoadingSurface"), Qt::FindChildrenRecursively);
      return root_->property("timelineWorkspaceLoaded").toBool() &&
             timeline_view != nullptr && timeline_page != nullptr &&
             timeline_view->property("timelineEditorPageLoaded").toBool() &&
             (loading_surface == nullptr ||
              !loading_surface->property("visible").toBool());
    }
    return false;
  }

  void beginRoute(int workspace_index, RoutePurpose purpose) {
    if (root_ == nullptr) {
      errors_.push_back(QStringLiteral("The Main.qml route controller was destroyed."));
      finish();
      return;
    }
    active_route_index_ = workspace_index;
    active_route_purpose_ = purpose;
    route_frame_swaps_before_ = presentation_observer_->frameSwapCount();
    ++active_route_generation_;
    presentation_observer_->beginGeneration(active_route_generation_);
    route_timer_.restart();
    phase_ = Phase::WaitingForRoute;
    QVariant result;
    const auto invoked = invokeQml(
        root_,
        "switchToWorkspaceIndex",
        QVariant(workspace_index),
        &result);
    if (!invoked || !result.toBool()) {
      errors_.push_back(QStringLiteral("Main.qml rejected the %1 route request.")
                            .arg(routeName(workspace_index)));
      finish();
      return;
    }
    presentation_observer_->noteReadinessChanged();
    if (purpose == RoutePurpose::Resume) {
      resume_media_ready_ = false;
      resume_frame_swaps_before_ = 0;
    }
  }

  void tickWaitingForRoute() {
    if (route_timer_.elapsed() > kRouteReadyTimeoutMs) {
      errors_.push_back(QStringLiteral("Timed out waiting for %1 to become ready and present a frame.")
                            .arg(routeName(active_route_index_)));
      finish();
      return;
    }
    // The observer correlates property notifications with beforeSynchronizing
    // and accepts the first actual frameSwapped for this route generation.
    if (!presentation_observer_->hasPresentedGeneration(
            active_route_generation_)) {
      return;
    }
    const auto current_frame_swaps = presentation_observer_->frameSwapCount();
    if (active_route_purpose_ == RoutePurpose::Resume && !resume_media_ready_) {
      resume_frame_swaps_before_ = current_frame_swaps;
      phase_ = Phase::WaitingForResumeFrame;
      return;
    }

    const auto elapsed_ms = route_timer_.elapsed();
    QVariantMap sample{
        {QStringLiteral("surface"), routeName(active_route_index_)},
        {QStringLiteral("workspaceIndex"), active_route_index_},
        {QStringLiteral("routeReadyPresentedMs"), elapsed_ms},
        {QStringLiteral("presentedFrame"), true},
        {QStringLiteral("frameSwapsAfterRequest"),
         static_cast<qulonglong>(current_frame_swaps - route_frame_swaps_before_)},
        {QStringLiteral("frameSwapAfterReady"), true},
    };
    switch (active_route_purpose_) {
      case RoutePurpose::Warmup:
        sample.insert(QStringLiteral("phase"), QStringLiteral("warmup"));
        warmup_route_samples_.push_back(sample);
        ++warmup_route_index_;
        if (warmup_route_index_ < warmup_routes_.size()) {
          beginRoute(warmup_routes_.at(warmup_route_index_).toInt(), RoutePurpose::Warmup);
          return;
        }
        memory_samples_.insert(QStringLiteral("beforeTimeline"), sampleMemory());
        beginRoute(
            root_->property("timelineWorkspaceIndex").toInt(),
            RoutePurpose::FirstTimeline);
        return;
      case RoutePurpose::FirstTimeline:
        sample.insert(QStringLiteral("phase"), QStringLiteral("firstTimeline"));
        warmup_route_samples_.push_back(sample);
        first_timeline_presented_ms_ = elapsed_ms;
        first_timeline_presented_within_limit_ =
            elapsed_ms <= kFirstTimelineReadyLimitMs;
        gates_.insert(QStringLiteral("firstTimelinePresentedWithin1500ms"),
                      first_timeline_presented_within_limit_);
        if (!first_timeline_presented_within_limit_) {
          errors_.push_back(QStringLiteral(
              "The first Timeline page did not present a ready frame within 1500 ms."));
        }
        beginFirstUsePanels();
        return;
      case RoutePurpose::MeasuredCycle:
        sample.insert(QStringLiteral("phase"), QStringLiteral("measured"));
        sample.insert(QStringLiteral("cycle"), measured_route_index_ / 3 + 1);
        measured_route_samples_.push_back(sample);
        ++measured_route_index_;
        if (measured_route_index_ < kMeasuredCycles * 3) {
          beginRoute(nextMeasuredWorkspaceIndex(), RoutePurpose::MeasuredCycle);
          return;
        }
        beginRoute(
            root_->property("libraryWorkspaceIndex").toInt(),
            RoutePurpose::Inactive);
        return;
      case RoutePurpose::Inactive:
        sample.insert(QStringLiteral("phase"), QStringLiteral("inactive"));
        warmup_route_samples_.push_back(sample);
        inactive_timer_.restart();
        phase_ = Phase::HoldingTimelineInactive;
        return;
      case RoutePurpose::Resume:
        sample.insert(QStringLiteral("phase"), QStringLiteral("resume"));
        warmup_route_samples_.push_back(sample);
        memory_samples_.insert(QStringLiteral("afterResume"), sampleMemory());
        playback_diagnostics_.insert(
            QStringLiteral("afterResume"), playbackResourceDiagnostics());
        captureAfterResumeState();
        finish();
        return;
    }
  }

  int nextMeasuredWorkspaceIndex() const {
    const auto slot = measured_route_index_ % 3;
    if (slot == 0) {
      return root_->property("editWorkspaceIndex").toInt();
    }
    if (slot == 1) {
      return root_->property("libraryWorkspaceIndex").toInt();
    }
    return root_->property("timelineWorkspaceIndex").toInt();
  }

  void beginFirstUsePanels() {
    auto* timeline_view = timelineView();
    if (timeline_view == nullptr ||
        !timeline_view->property("timelineEditorPageLoaded").toBool()) {
      errors_.push_back(QStringLiteral("The first Timeline page did not remain loaded after its presented route."));
      finish();
      return;
    }

    panel_open_actions_ = QVariantMap{};
    panel_open_actions_.insert(
        QStringLiteral("sourceMonitorInvoked"),
        invokeQml(timeline_view, "openSourceMonitor"));
    QVariant inspector_opened;
    panel_open_actions_.insert(
        QStringLiteral("effectsInspectorInvoked"),
        invokeQml(timeline_view, "showTimelinePanel", QStringLiteral("inspector"),
                  &inspector_opened) && inspector_opened.toBool());
    timeline_view->setProperty("inspectorTab", QStringLiteral("effects"));
    panel_open_actions_.insert(QStringLiteral("effectsInspectorSelected"),
                               timeline_view->property("inspectorTab").toString() ==
                                   QStringLiteral("effects"));
    panel_open_stage_ = 0;
    panel_frame_swaps_before_ = presentation_observer_->frameSwapCount();
    if (window_ != nullptr) {
      window_->update();
    }
    phase_timer_.restart();
    phase_ = Phase::OpeningFirstUsePanels;
  }

  QObject* timelineView() const {
    if (root_ == nullptr) {
      return nullptr;
    }
    return root_->findChild<QObject*>(
        QStringLiteral("timelineWorkspace"), Qt::FindChildrenRecursively);
  }

  bool panelExposed(QObject* timeline_view, const QString& panel_id) const {
    QVariant result;
    return invokeQml(
               timeline_view,
               "timelinePanelExposed",
               panel_id,
               &result) &&
           result.toBool();
  }

  bool mixerExposed(QObject* timeline_view) const {
    return panelExposed(timeline_view, QStringLiteral("mixer")) ||
           (timeline_view != nullptr &&
            timeline_view->property("audioMixerPopupVisible").toBool());
  }

  void tickOpeningFirstUsePanels() {
    auto* timeline_view = timelineView();
    if (timeline_view == nullptr) {
      errors_.push_back(QStringLiteral("The Timeline workspace disappeared during first-use panel setup."));
      finish();
      return;
    }
    const auto presented_after_panel_open =
        presentation_observer_->frameSwapCount() > panel_frame_swaps_before_;
    if (panel_open_stage_ == 0 &&
        !timeline_view->property("sourcePanelCollapsed").toBool() &&
        !timeline_view->property("effectsPanelCollapsed").toBool() &&
        timeline_view->property("inspectorTab").toString() == QStringLiteral("effects") &&
        panelExposed(timeline_view, QStringLiteral("source")) &&
        panelExposed(timeline_view, QStringLiteral("inspector")) &&
        presented_after_panel_open) {
      panel_open_actions_.insert(QStringLiteral("sourceExposed"), true);
      panel_open_actions_.insert(QStringLiteral("effectsExposed"), true);
      timeline_view->setProperty("inspectorTab", QStringLiteral("color"));
      panel_open_actions_.insert(QStringLiteral("colorInspectorSelected"),
                                 timeline_view->property("inspectorTab").toString() ==
                                     QStringLiteral("color"));
      panel_open_actions_.insert(
          QStringLiteral("audioMixerInvoked"),
          invokeQml(timeline_view, "openAudioMixerPopup"));
      QVariant keyframes_opened;
      panel_open_actions_.insert(
          QStringLiteral("keyframesPanelInvoked"),
          invokeQml(timeline_view, "showTimelinePanel", QStringLiteral("keyframes"),
                    &keyframes_opened) && keyframes_opened.toBool());
      panel_frame_swaps_before_ = presentation_observer_->frameSwapCount();
      panel_open_stage_ = 1;
      phase_timer_.restart();
      if (window_ != nullptr) {
        window_->update();
      }
      return;
    }
    const auto panels_ready = panel_open_stage_ == 1 &&
        !timeline_view->property("sourcePanelCollapsed").toBool() &&
        !timeline_view->property("effectsPanelCollapsed").toBool() &&
        timeline_view->property("inspectorTab").toString() == QStringLiteral("color") &&
        panelExposed(timeline_view, QStringLiteral("source")) &&
        panelExposed(timeline_view, QStringLiteral("inspector")) &&
        mixerExposed(timeline_view) &&
        panelExposed(timeline_view, QStringLiteral("keyframes"));
    if (panels_ready && presented_after_panel_open) {
      panel_open_actions_.insert(QStringLiteral("colorExposed"), true);
      panel_open_actions_.insert(QStringLiteral("sourceExposed"), true);
      panel_open_actions_.insert(QStringLiteral("effectsExposed"), true);
      panel_open_actions_.insert(QStringLiteral("mixerExposed"), true);
      panel_open_actions_.insert(QStringLiteral("keyframesExposed"), true);
      panel_open_actions_.insert(QStringLiteral("panelSetupPresented"), true);
      memory_samples_.insert(QStringLiteral("afterTimelineLoaded"), sampleMemory());
      playback_diagnostics_.insert(
          QStringLiteral("afterTimelineLoaded"), playbackResourceDiagnostics());
      baseline_state_ = captureTimelineState();
      phase_timer_.restart();
      phase_ = Phase::WaitingForResumeFrame;
      // The initial Timeline view is the first full render; route back to Edit
      // to begin the 20 measured Edit/Library/Timeline cycles.
      beginRoute(
          root_->property("editWorkspaceIndex").toInt(),
          RoutePurpose::MeasuredCycle);
      return;
    }
    if (phase_timer_.elapsed() > 10000) {
      panel_open_actions_.insert(QStringLiteral("sourceExposed"),
                                 panelExposed(timeline_view, QStringLiteral("source")));
      panel_open_actions_.insert(QStringLiteral("effectsExposed"),
                                 panelExposed(timeline_view, QStringLiteral("inspector")));
      panel_open_actions_.insert(QStringLiteral("colorExposed"),
          timeline_view->property("inspectorTab").toString() == QStringLiteral("color"));
      panel_open_actions_.insert(QStringLiteral("mixerExposed"),
                                 mixerExposed(timeline_view));
      panel_open_actions_.insert(QStringLiteral("keyframesExposed"),
                                 panelExposed(timeline_view, QStringLiteral("keyframes")));
      errors_.push_back(QStringLiteral("The source, effects, color, mixer, and keyframes panels did not all reach their visible first-use state."));
      finish();
    }
  }

  void tickHoldingTimelineInactive() {
    if (inactive_timer_.elapsed() < kInactiveHoldMs) {
      return;
    }
    memory_samples_.insert(QStringLiteral("after12SecondsInactive"), sampleMemory());
    playback_diagnostics_.insert(
        QStringLiteral("after12SecondsInactive"), playbackResourceDiagnostics());
    const auto inactive_resources = playbackResourceDiagnostics();
    const auto released =
        inactive_resources.value(QStringLiteral("hibernated")).toBool() &&
        !inactive_resources.value(QStringLiteral("workspaceActive")).toBool() &&
        !inactive_resources.value(QStringLiteral("playerSourceLoaded")).toBool() &&
        !inactive_resources.value(QStringLiteral("prefetchPlayerSourceLoaded")).toBool() &&
        inactive_resources.value(QStringLiteral("retainedVideoFrameCount")).toInt() == 0 &&
        !inactive_resources.value(QStringLiteral("processedFrameImagePresent")).toBool();
    gates_.insert(QStringLiteral("decoderResourcesReleasedAfterInactivity"), released);
    if (!released) {
      errors_.push_back(QStringLiteral(
          "Timeline playback diagnostics still showed retained decoder or frame resources after 12 seconds inactive."));
    }
    beginRoute(
        root_->property("timelineWorkspaceIndex").toInt(),
        RoutePurpose::Resume);
  }

  void tickWaitingForResumeFrame() {
    if (active_route_purpose_ != RoutePurpose::Resume) {
      return;
    }
    if (route_timer_.elapsed() > kRouteReadyTimeoutMs) {
      const auto resources = playbackResourceDiagnostics();
      errors_.push_back(QStringLiteral(
          "Timeline did not reacquire a decoded frame after returning to the workspace."));
      memory_samples_.insert(QStringLiteral("afterResume"), sampleMemory());
      playback_diagnostics_.insert(QStringLiteral("afterResume"), resources);
      captureAfterResumeState();
      finish();
      return;
    }
    const auto resources = playbackResourceDiagnostics();
    const auto frame_image_ready =
        resources.value(QStringLiteral("processedFrameImagePresent")).toBool();
    if (!playback_->mediaReady() || !frame_image_ready) {
      return;
    }
    if (!resume_media_ready_) {
      resume_media_ready_ = true;
      resume_frame_swaps_before_ = presentation_observer_->frameSwapCount();
      if (window_ != nullptr) {
        window_->update();
      }
      return;
    }
    if (presentation_observer_->frameSwapCount() <=
        resume_frame_swaps_before_) {
      return;
    }
    // Finish only after the restored page and its decoded video frame have
    // both appeared in a fresh window swap.
    phase_ = Phase::WaitingForRoute;
    tickWaitingForRoute();
  }

  QVariantMap sampleMemory() const {
    QVariantMap sample{
        {QStringLiteral("sampledAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("available"), false},
    };
#if defined(Q_OS_MACOS)
    task_vm_info_data_t vm_info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    const auto status = task_info(
        mach_task_self(),
        TASK_VM_INFO,
        reinterpret_cast<task_info_t>(&vm_info),
        &count);
    sample.insert(QStringLiteral("machStatus"), static_cast<int>(status));
    if (status == KERN_SUCCESS) {
      sample.insert(QStringLiteral("available"), true);
      sample.insert(QStringLiteral("residentSizeBytes"),
                    static_cast<qulonglong>(vm_info.resident_size));
      sample.insert(QStringLiteral("physicalFootprintBytes"),
                    static_cast<qulonglong>(vm_info.phys_footprint));
    }
#else
    sample.insert(QStringLiteral("error"), QStringLiteral("macOS task_info is required for this lifecycle receipt."));
#endif
    return sample;
  }

  QVariantMap playbackResourceDiagnostics() const {
    return playback_ == nullptr ? QVariantMap{} : playback_->playbackResourceDiagnostics();
  }

  QVariantMap captureTimelineState() const {
    if (timeline_ == nullptr) {
      return {};
    }
    QVariantMap panel_state;
    auto* timeline_view = timelineView();
    panel_state.insert(QStringLiteral("available"), timeline_view != nullptr);
    if (timeline_view != nullptr) {
      for (const auto& property_name : {
               QStringLiteral("sourcePanelCollapsed"),
               QStringLiteral("sourcePanelHidden"),
               QStringLiteral("sourceRailTab"),
               QStringLiteral("effectsPanelCollapsed"),
               QStringLiteral("inspectorTab"),
               QStringLiteral("curveEditorOpen"),
               QStringLiteral("curveEditorDocked"),
               QStringLiteral("audioMixerPopupVisible"),
               QStringLiteral("workspaceResourcesHibernated")}) {
        panel_state.insert(property_name, timeline_view->property(property_name.toUtf8().constData()));
      }
      panel_state.insert(
          QStringLiteral("retainedPanelSessionState"),
          timeline_view->property("retainedPanelSessionState").toMap());
      panel_state.insert(
          QStringLiteral("projectCacheBudgetDraft"),
          timeline_view->property("projectCacheBudgetDraft").toMap());
      for (const auto& panel_id : {QStringLiteral("source"), QStringLiteral("inspector"),
                                   QStringLiteral("mixer"), QStringLiteral("keyframes")}) {
        panel_state.insert(panel_id + QStringLiteral("Exposed"),
                           panel_id == QStringLiteral("mixer")
                               ? mixerExposed(timeline_view)
                               : panelExposed(timeline_view, panel_id));
      }
    }
    return {
        {QStringLiteral("documentHash"), timeline_->deterministicDocumentHash()},
        {QStringLiteral("canUndo"), timeline_->canUndo()},
        {QStringLiteral("canRedo"), timeline_->canRedo()},
        {QStringLiteral("playheadSeconds"), timeline_->sequencePlayheadSeconds()},
        {QStringLiteral("clipCount"), timeline_->clipCount()},
        {QStringLiteral("sequencePlaying"), timeline_->sequencePlaying()},
        {QStringLiteral("hasActiveTimeline"), timeline_->hasActiveTimeline()},
        {QStringLiteral("dirty"), timeline_->dirty()},
        {QStringLiteral("savedDocumentPath"), timeline_->savedDocumentPath()},
        {QStringLiteral("panelState"), panel_state},
    };
  }

  void captureAfterResumeState() {
    after_resume_state_ = captureTimelineState();
    const auto baseline_hash = baseline_state_.value(QStringLiteral("documentHash")).toString();
    const auto after_hash = after_resume_state_.value(QStringLiteral("documentHash")).toString();
    const auto same_undo =
        baseline_state_.value(QStringLiteral("canUndo")).toBool() ==
            after_resume_state_.value(QStringLiteral("canUndo")).toBool() &&
        baseline_state_.value(QStringLiteral("canRedo")).toBool() ==
            after_resume_state_.value(QStringLiteral("canRedo")).toBool();
    const auto same_playhead = qAbs(
        baseline_state_.value(QStringLiteral("playheadSeconds")).toDouble() -
        after_resume_state_.value(QStringLiteral("playheadSeconds")).toDouble()) <= 0.000001;
    const auto same_hash = !baseline_hash.isEmpty() && baseline_hash == after_hash;
    const auto same_session =
        baseline_state_.value(QStringLiteral("clipCount")) ==
            after_resume_state_.value(QStringLiteral("clipCount")) &&
        baseline_state_.value(QStringLiteral("sequencePlaying")) ==
            after_resume_state_.value(QStringLiteral("sequencePlaying")) &&
        baseline_state_.value(QStringLiteral("hasActiveTimeline")) ==
            after_resume_state_.value(QStringLiteral("hasActiveTimeline")) &&
        baseline_state_.value(QStringLiteral("dirty")) ==
            after_resume_state_.value(QStringLiteral("dirty")) &&
        baseline_state_.value(QStringLiteral("savedDocumentPath")) ==
            after_resume_state_.value(QStringLiteral("savedDocumentPath"));
    const auto same_panels =
        baseline_state_.value(QStringLiteral("panelState")).toMap() ==
        after_resume_state_.value(QStringLiteral("panelState")).toMap();
    gates_.insert(QStringLiteral("timelineDocumentHashUnchanged"), same_hash);
    gates_.insert(QStringLiteral("timelineUndoStateUnchanged"), same_undo);
    gates_.insert(QStringLiteral("timelinePlayheadUnchanged"), same_playhead);
    gates_.insert(QStringLiteral("timelineDocumentSessionUnchanged"), same_session);
    gates_.insert(QStringLiteral("timelinePanelDraftAndSessionRestored"), same_panels);
    if (!same_hash || !same_undo || !same_playhead || !same_session || !same_panels) {
      errors_.push_back(QStringLiteral(
          "Timeline document, Undo, playhead, session, or panel draft state changed across workspace cycling and resume."));
    }

    const auto resources = playbackResourceDiagnostics();
    const auto resumed =
        resources.value(QStringLiteral("workspaceActive")).toBool() &&
        resources.value(QStringLiteral("playerSourceLoaded")).toBool() &&
        resources.value(QStringLiteral("playerMediaReady")).toBool() &&
        resources.value(QStringLiteral("processedFrameImagePresent")).toBool();
    gates_.insert(QStringLiteral("decoderResourcesResumed"), resumed);
    if (!resumed) {
      errors_.push_back(QStringLiteral(
          "Timeline playback diagnostics did not show a resumed decoder and processed frame."));
    }
  }

  static qint64 percentile95(QVector<qint64> values) {
    if (values.isEmpty()) {
      return -1;
    }
    std::sort(values.begin(), values.end());
    const auto index = qBound(0, static_cast<int>(std::ceil(0.95 * values.size())) - 1,
                              static_cast<int>(values.size()) - 1);
    return values.at(index);
  }

  QJsonObject routeTimingSummary() {
    QJsonObject summary;
    for (const auto& name : {QStringLiteral("Edit"), QStringLiteral("Library"),
                             QStringLiteral("Timeline")}) {
      QVector<qint64> durations;
      for (const auto& value : measured_route_samples_) {
        const auto sample = value.toMap();
        if (sample.value(QStringLiteral("surface")).toString() == name) {
          durations.push_back(sample.value(QStringLiteral("routeReadyPresentedMs")).toLongLong());
        }
      }
      const auto p95 = percentile95(durations);
      const auto max = durations.isEmpty()
                           ? -1
                           : *std::max_element(durations.cbegin(), durations.cend());
      const auto within_limit = durations.size() == kMeasuredCycles && p95 >= 0 &&
                                p95 <= kWarmRouteP95LimitMs;
      summary.insert(name, QJsonObject{
          {QStringLiteral("sampleCount"), durations.size()},
          {QStringLiteral("p95Ms"), p95},
          {QStringLiteral("maxMs"), max},
          {QStringLiteral("within250ms"), within_limit},
      });
      gates_.insert(name + QStringLiteral("WarmRouteP95Within250ms"), within_limit);
      if (!within_limit) {
        errors_.push_back(QStringLiteral(
            "%1 warm route p95 exceeded 250 ms or did not produce 20 measured samples.")
                              .arg(name));
      }
    }
    return summary;
  }

  void evaluateMemoryGates() {
    const auto before = memory_samples_.value(QStringLiteral("beforeTimeline")).toMap();
    const auto loaded = memory_samples_.value(QStringLiteral("afterTimelineLoaded")).toMap();
    const auto inactive = memory_samples_.value(QStringLiteral("after12SecondsInactive")).toMap();
    const auto resumed = memory_samples_.value(QStringLiteral("afterResume")).toMap();
    const auto all_available =
        before.value(QStringLiteral("available")).toBool() &&
        loaded.value(QStringLiteral("available")).toBool() &&
        inactive.value(QStringLiteral("available")).toBool() &&
        resumed.value(QStringLiteral("available")).toBool();
    gates_.insert(QStringLiteral("nativeMachMemoryAvailable"), all_available);
    if (!all_available) {
      errors_.push_back(QStringLiteral("One or more native mach task_info memory samples were unavailable."));
      return;
    }

    const auto rss_loaded = loaded.value(QStringLiteral("residentSizeBytes")).toLongLong();
    const auto rss_inactive = inactive.value(QStringLiteral("residentSizeBytes")).toLongLong();
    const auto rss_resumed = resumed.value(QStringLiteral("residentSizeBytes")).toLongLong();
    const auto footprint_loaded = loaded.value(QStringLiteral("physicalFootprintBytes")).toLongLong();
    const auto footprint_inactive = inactive.value(QStringLiteral("physicalFootprintBytes")).toLongLong();
    const auto footprint_resumed = resumed.value(QStringLiteral("physicalFootprintBytes")).toLongLong();
    const auto rss_peak_growth = qMax<qint64>(0, qMax(rss_inactive, rss_resumed) - rss_loaded);
    const auto footprint_peak_growth = qMax<qint64>(
        0, qMax(footprint_inactive, footprint_resumed) - footprint_loaded);
    const auto rss_resume_delta = qAbs(rss_resumed - rss_inactive);
    const auto footprint_resume_delta = qAbs(footprint_resumed - footprint_inactive);
    const auto rss_plateau = rss_peak_growth <= kMemoryPlateauLimitBytes &&
                             rss_resume_delta <= kMemoryPlateauLimitBytes;
    const auto footprint_plateau = footprint_peak_growth <= kMemoryPlateauLimitBytes &&
                                   footprint_resume_delta <= kMemoryPlateauLimitBytes;
    gates_.insert(QStringLiteral("residentMemoryPlateauWithin64MiB"), rss_plateau);
    gates_.insert(QStringLiteral("physicalFootprintPlateauWithin64MiB"), footprint_plateau);
    memory_summary_ = {
        {QStringLiteral("residentPeakGrowthFromLoadedBytes"), rss_peak_growth},
        {QStringLiteral("residentResumeDeltaFromInactiveBytes"), rss_resume_delta},
        {QStringLiteral("physicalFootprintPeakGrowthFromLoadedBytes"), footprint_peak_growth},
        {QStringLiteral("physicalFootprintResumeDeltaFromInactiveBytes"), footprint_resume_delta},
        {QStringLiteral("limitBytes"), kMemoryPlateauLimitBytes},
    };
    if (!rss_plateau || !footprint_plateau) {
      errors_.push_back(QStringLiteral(
          "Resident memory or physical footprint exceeded the 64 MiB lifecycle plateau."));
    }
  }

  void captureFinalWindowImage() {
    const auto capture_path = QDir(output_directory_).filePath(
        QStringLiteral("resumed-timeline.png"));
    QVariantMap capture{
        {QStringLiteral("path"), capture_path},
        {QStringLiteral("captured"), false},
        {QStringLiteral("width"), 0},
        {QStringLiteral("height"), 0},
        {QStringLiteral("workspaceIndex"),
         root_ == nullptr ? -1 : root_->property("currentWorkspace").toInt()},
        {QStringLiteral("windowVisible"), window_ != nullptr && window_->isVisible()},
        {QStringLiteral("windowExposed"), window_ != nullptr && window_->isExposed()},
        {QStringLiteral("windowActive"), window_ != nullptr && window_->isActive()},
    };
    if (window_ != nullptr) {
      const auto image = window_->grabWindow();
      if (!image.isNull() && image.save(capture_path, "PNG")) {
        capture.insert(QStringLiteral("captured"), true);
        capture.insert(QStringLiteral("width"), image.width());
        capture.insert(QStringLiteral("height"), image.height());
      }
    }
    const auto captured = capture.value(QStringLiteral("captured")).toBool();
    const auto timeline_index = root_ == nullptr
                                   ? -1
                                   : root_->property("timelineWorkspaceIndex").toInt();
    const auto resumed_timeline_visible =
        captured && window_ != nullptr && window_->isVisible() &&
        window_->isExposed() && window_->isActive() && root_ != nullptr &&
        root_->property("currentWorkspace").toInt() == timeline_index &&
        routeSurfaceReady(timeline_index);
    capture.insert(QStringLiteral("resumedTimelineVisible"), resumed_timeline_visible);
    gates_.insert(QStringLiteral("finalWindowCaptureWritten"), captured);
    gates_.insert(QStringLiteral("finalWindowCaptureShowsResumedTimeline"),
                  resumed_timeline_visible);
    if (!captured || !resumed_timeline_visible) {
      errors_.push_back(QStringLiteral(
          "The final packaged application capture was not a visible, ready resumed Timeline window."));
      capture.insert(QStringLiteral("error"), captured
          ? QStringLiteral("The capture was not an exposed ready Timeline workspace.")
          : QStringLiteral("QQuickWindow::grabWindow or PNG writing failed."));
    }
    final_window_capture_ = capture;
  }

  void finish() {
    if (phase_ == Phase::Finished) {
      return;
    }
    phase_ = Phase::Finished;
    timer_.stop();
    const auto timings = routeTimingSummary();
    evaluateMemoryGates();
    captureFinalWindowImage();
    const auto all_foreground_samples_eligible =
        foreground_sample_count_ > 0 && foreground_sample_count_ == foreground_eligible_count_;
    gates_.insert(QStringLiteral("foregroundWindowStayedActiveAndExposed"),
                  all_foreground_samples_eligible);
    if (!all_foreground_samples_eligible && errors_.isEmpty()) {
      errors_.push_back(QStringLiteral("Foreground eligibility was not continuously established."));
    }
    if (!gates_.contains(QStringLiteral("firstTimelinePresentedWithin1500ms"))) {
      first_timeline_presented_within_limit_ = false;
      gates_.insert(QStringLiteral("firstTimelinePresentedWithin1500ms"), false);
      if (errors_.isEmpty()) {
        errors_.push_back(QStringLiteral("The first Timeline page did not produce a ready presented frame."));
      }
    }
    const auto ok = errors_.isEmpty();
    const auto edit_index = root_ == nullptr
                                ? -1
                                : root_->property("editWorkspaceIndex").toInt();
    const auto library_index = root_ == nullptr
                                   ? -1
                                   : root_->property("libraryWorkspaceIndex").toInt();
    const auto timeline_index = root_ == nullptr
                                    ? -1
                                    : root_->property("timelineWorkspaceIndex").toInt();
    QVariantMap inputs{
        {QStringLiteral("outputDirectory"), output_directory_},
        {QStringLiteral("sourceVideoPath"), source_video_path_},
        {QStringLiteral("sourceImagePath"), source_image_path_},
        {QStringLiteral("photoFixtureSource"), photo_fixture_source_},
        {QStringLiteral("fixtureArtifacts"), fixture_artifacts_},
        {QStringLiteral("cycles"), kMeasuredCycles},
        {QStringLiteral("routesPerCycle"),
         QVariantList{edit_index, library_index, timeline_index}},
    };
    QVariantMap receipt{
        {QStringLiteral("schema"),
         QStringLiteral("timeline-route-performance-lifecycle:v1")},
        {QStringLiteral("status"), ok ? QStringLiteral("passed") : QStringLiteral("failed")},
        {QStringLiteral("ok"), ok},
        {QStringLiteral("startedAt"), started_at_.toString(Qt::ISODateWithMs)},
        {QStringLiteral("completedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("elapsedMs"), run_timer_.elapsed()},
        {QStringLiteral("inputs"), inputs},
        {QStringLiteral("catalogFixtureSeeded"), fixture_seeded_},
        {QStringLiteral("foregroundSampleCount"), foreground_sample_count_},
        {QStringLiteral("foregroundEligibleSampleCount"), foreground_eligible_count_},
        {QStringLiteral("foregroundLossDetails"), foreground_loss_details_},
        {QStringLiteral("foregroundStartDetails"), foreground_start_details_},
        {QStringLiteral("warmupRoutes"), warmup_route_samples_},
        {QStringLiteral("measuredRoutes"), measured_route_samples_},
        {QStringLiteral("firstTimelinePresentedMs"), first_timeline_presented_ms_},
        {QStringLiteral("firstTimelinePresentedWithin1500ms"),
         first_timeline_presented_within_limit_},
        {QStringLiteral("routeTimingSummary"), timings.toVariantMap()},
        {QStringLiteral("memorySamples"), memory_samples_},
        {QStringLiteral("memorySummary"), memory_summary_},
        {QStringLiteral("playbackResourceDiagnostics"), playback_diagnostics_},
        {QStringLiteral("finalWindowCapture"), final_window_capture_},
        {QStringLiteral("timelineStateBaseline"), baseline_state_},
        {QStringLiteral("timelineStateAfterResume"), after_resume_state_},
        {QStringLiteral("firstUsePanelActions"), panel_open_actions_},
        {QStringLiteral("gates"), gates_},
        {QStringLiteral("errors"), errors_},
    };
    if (QFileInfo::exists(receiptPath())) {
      if (application_ != nullptr) {
        application_->exit(EXIT_FAILURE);
      }
      return;
    }
    QSaveFile file(receiptPath());
    const auto payload =
        QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() ||
        !file.commit()) {
      errors_.push_back(QStringLiteral("The native lifecycle receipt could not be written."));
      if (application_ != nullptr) {
        application_->exit(EXIT_FAILURE);
      }
      return;
    }
    if (application_ != nullptr) {
      application_->exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
    }
  }

  QApplication* application_ = nullptr;
  QPointer<QObject> root_;
  QPointer<QQuickWindow> window_;
  CatalogAssetModel* catalog_ = nullptr;
  TimelineDocumentController* timeline_ = nullptr;
  TimelinePlaybackController* playback_ = nullptr;
  bool* catalog_bootstrap_finished_ = nullptr;
  QString output_directory_;
  QString source_video_path_;
  QString source_image_path_;
  QString photo_fixture_source_;
  QTimer timer_;
  QVariantMap foreground_loss_details_;
  QVariantMap foreground_start_details_;
  QElapsedTimer run_timer_;
  QElapsedTimer activation_timer_;
  QElapsedTimer phase_timer_;
  QElapsedTimer route_timer_;
  QElapsedTimer inactive_timer_;
  Phase phase_ = Phase::WaitingForForeground;
  RoutePurpose active_route_purpose_ = RoutePurpose::Warmup;
  int active_route_index_ = -1;
  quint64 route_frame_swaps_before_ = 0;
  quint64 active_route_generation_ = 0;
  quint64 panel_frame_swaps_before_ = 0;
  int warmup_route_index_ = 0;
  int measured_route_index_ = 0;
  int panel_open_stage_ = 0;
  int foreground_sample_count_ = 0;
  int foreground_eligible_count_ = 0;
  bool fixture_seeded_ = false;
  bool foreground_measurement_started_ = false;
  bool foreground_lost_ = false;
  bool first_timeline_presented_within_limit_ = false;
  bool resume_media_ready_ = false;
  quint64 resume_frame_swaps_before_ = 0;
  qint64 first_timeline_presented_ms_ = -1;
  QDateTime started_at_;
  QVariantList warmup_routes_;
  QVariantList warmup_route_samples_;
  QVariantList measured_route_samples_;
  QVariantMap memory_samples_;
  QVariantMap memory_summary_;
  QVariantMap playback_diagnostics_;
  QVariantMap panel_open_actions_;
  QVariantMap fixture_artifacts_;
  QVariantMap baseline_state_;
  QVariantMap after_resume_state_;
  QVariantMap gates_;
  QStringList errors_;
  QVariantMap final_window_capture_;
  // Declare last so its destructor disconnects mapped QML signals before the
  // readiness callback's probe state is destroyed.
  std::unique_ptr<WorkspacePresentationObserver> presentation_observer_;
};

}  // namespace photo_editor::desktop
