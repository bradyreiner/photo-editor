#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QThread>
#include <QtTest/QTest>

#include <iostream>
#include <memory>

class FakeTimelineTrackingDocument final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool trackingBusy READ trackingBusy NOTIFY trackingChanged)
  Q_PROPERTY(bool sceneTrackingAvailable READ sceneTrackingAvailable NOTIFY trackingChanged)
  Q_PROPERTY(bool surfaceTrackingAvailable READ surfaceTrackingAvailable NOTIFY trackingChanged)
  Q_PROPERTY(double trackingProgress READ trackingProgress NOTIFY trackingChanged)
  Q_PROPERTY(QString trackingStatus READ trackingStatus NOTIFY trackingChanged)
  Q_PROPERTY(bool rotoscopingAvailable READ rotoscopingAvailable NOTIFY trackingChanged)
  Q_PROPERTY(QVariantMap currentClip READ currentClip NOTIFY selectionChanged)
  Q_PROPERTY(QVariantList motionTrackSources READ motionTrackSources NOTIFY clipsChanged)
  Q_PROPERTY(QVariantList sceneTrackSources READ sceneTrackSources NOTIFY clipsChanged)
  Q_PROPERTY(QVariantList surfaceTrackSources READ surfaceTrackSources NOTIFY clipsChanged)
  Q_PROPERTY(QVariantMap seedRect READ seedRect WRITE setSeedRect NOTIFY seedRectChanged)

 public:
  explicit FakeTimelineTrackingDocument(QObject* parent = nullptr) : QObject(parent) {
    current_clip_ = {
        {QStringLiteral("clipId"), QStringLiteral("target-title")},
        {QStringLiteral("displayLabel"), QStringLiteral("Title overlay")},
        {QStringLiteral("trackKind"), QStringLiteral("video")},
        {QStringLiteral("mediaType"), QStringLiteral("title")},
        {QStringLiteral("foregroundCutoutEnabled"), false},
        {QStringLiteral("foregroundCutoutReady"), false},
    };
    motion_track_sources_ = QVariantList{QVariantMap{
        {QStringLiteral("clipId"), QStringLiteral("tracked-source")},
        {QStringLiteral("displayLabel"), QStringLiteral("Subject source")},
        {QStringLiteral("motionTracking"), QVariantMap{{QStringLiteral("samples"), 8}}},
    }};
    scene_track_sources_ = QVariantList{QVariantMap{
        {QStringLiteral("clipId"), QStringLiteral("scene-source")},
        {QStringLiteral("displayLabel"), QStringLiteral("Scene source")},
        {QStringLiteral("occlusionAvailable"), true},
        {QStringLiteral("pointCount"), 5},
        {QStringLiteral("points"), QVariantList{
             QVariantMap{{QStringLiteral("id"), QStringLiteral("p1")},
                         {QStringLiteral("x"), 0.12}, {QStringLiteral("y"), 0.18},
                         {QStringLiteral("reprojectionError"), 0.35}},
             QVariantMap{{QStringLiteral("id"), QStringLiteral("p2")},
                         {QStringLiteral("x"), 0.82}, {QStringLiteral("y"), 0.20},
                         {QStringLiteral("reprojectionError"), 0.42}},
             QVariantMap{{QStringLiteral("id"), QStringLiteral("p3")},
                         {QStringLiteral("x"), 0.22}, {QStringLiteral("y"), 0.78},
                         {QStringLiteral("reprojectionError"), 0.31}},
             QVariantMap{{QStringLiteral("id"), QStringLiteral("p4")},
                         {QStringLiteral("x"), 0.68}, {QStringLiteral("y"), 0.69},
                         {QStringLiteral("reprojectionError"), 0.39}},
             QVariantMap{{QStringLiteral("id"), QStringLiteral("p5")},
                         {QStringLiteral("x"), 0.50}, {QStringLiteral("y"), 0.48},
                         {QStringLiteral("reprojectionError"), 0.28}},
        }},
    }};
    seed_rect_ = {
        {QStringLiteral("x"), 0.18},
        {QStringLiteral("y"), 0.20},
        {QStringLiteral("width"), 0.24},
        {QStringLiteral("height"), 0.36},
    };
  }

  bool trackingBusy() const { return tracking_busy_; }
  bool sceneTrackingAvailable() const { return true; }
  bool surfaceTrackingAvailable() const { return true; }
  double trackingProgress() const { return tracking_progress_; }
  QString trackingStatus() const { return tracking_status_; }
  bool rotoscopingAvailable() const { return rotoscoping_available_; }
  QVariantMap currentClip() const { return current_clip_; }
  QVariantList motionTrackSources() const { return motion_track_sources_; }
  QVariantList sceneTrackSources() const { return scene_track_sources_; }
  QVariantList surfaceTrackSources() const { return surface_track_sources_; }
  QVariantMap seedRect() const { return seed_rect_; }

  void setSeedRect(const QVariantMap& seed) {
    if (seed_rect_ == seed)
      return;
    seed_rect_ = seed;
    emit seedRectChanged();
  }

  void setCurrentClip(const QVariantMap& clip) {
    current_clip_ = clip;
    emit selectionChanged();
  }

  void setSurfaceTrackDraftForTest() {
    auto updated = current_clip_;
    updated.insert(QStringLiteral("surfaceTrackReady"), false);
    updated.insert(QStringLiteral("surfaceTrackRepairable"), true);
    const QVariantList quad{
        QVariantMap{{QStringLiteral("x"), 0.12}, {QStringLiteral("y"), 0.14}},
        QVariantMap{{QStringLiteral("x"), 0.44}, {QStringLiteral("y"), 0.14}},
        QVariantMap{{QStringLiteral("x"), 0.12}, {QStringLiteral("y"), 0.52}},
        QVariantMap{{QStringLiteral("x"), 0.44}, {QStringLiteral("y"), 0.52}},
    };
    const QVariantMap seed{{QStringLiteral("x"), 0.12}, {QStringLiteral("y"), 0.14},
                           {QStringLiteral("width"), 0.32}, {QStringLiteral("height"), 0.38},
                           {QStringLiteral("correctionFrames"), QVariantList{
                                QVariantMap{{QStringLiteral("frameIndex"), 0},
                                            {QStringLiteral("quad"), quad}}}}};
    auto tracking = updated.value(QStringLiteral("motionTracking")).toMap();
    tracking.insert(QStringLiteral("surfaceTrackDraft"),
                    QVariantMap{{QStringLiteral("seed"), seed}, {QStringLiteral("fps"), 24.0},
                                {QStringLiteral("trimStart"), 0.0}, {QStringLiteral("frameCount"), 24}});
    updated.insert(QStringLiteral("motionTracking"), tracking);
    current_clip_ = updated;
    emit selectionChanged();
  }

  Q_INVOKABLE bool analyzeSelectedClipMotion(const QVariantMap& seed) {
    last_motion_seed_ = seed;
    ++motion_analysis_count_;
    tracking_status_ = QStringLiteral("Motion analysis started");
    emit trackingChanged();
    return true;
  }

  Q_INVOKABLE bool analyzeSelectedClipSceneCamera() {
    ++scene_analysis_count_;
    tracking_status_ = QStringLiteral("Scene camera analysis started");
    emit trackingChanged();
    return true;
  }

  Q_INVOKABLE bool analyzeSelectedClipSurface(const QVariantMap& seed) {
    last_surface_seed_ = seed;
    ++surface_analysis_count_;
    auto updated = current_clip_;
    updated.insert(QStringLiteral("surfaceTrackReady"), true);
    updated.insert(QStringLiteral("surfaceTrackRepairable"), true);
    updated.insert(QStringLiteral("surfaceTrackQuad"), QVariantList{
        QVariantMap{{QStringLiteral("x"), 0.18}, {QStringLiteral("y"), 0.20}},
        QVariantMap{{QStringLiteral("x"), 0.42}, {QStringLiteral("y"), 0.18}},
        QVariantMap{{QStringLiteral("x"), 0.19}, {QStringLiteral("y"), 0.56}},
        QVariantMap{{QStringLiteral("x"), 0.45}, {QStringLiteral("y"), 0.58}},
    });
    auto persisted_seed = seed;
    if (!persisted_seed.contains(QStringLiteral("correctionFrames"))) {
      persisted_seed.insert(QStringLiteral("correctionFrames"), QVariantList{
          QVariantMap{{QStringLiteral("frameIndex"), 3},
                      {QStringLiteral("quad"), QVariantList{
                           QVariantMap{{QStringLiteral("x"), 0.1}, {QStringLiteral("y"), 0.1}},
                           QVariantMap{{QStringLiteral("x"), 0.4}, {QStringLiteral("y"), 0.1}},
                           QVariantMap{{QStringLiteral("x"), 0.1}, {QStringLiteral("y"), 0.4}},
                           QVariantMap{{QStringLiteral("x"), 0.4}, {QStringLiteral("y"), 0.4}},
                       }}}
      });
    }
    const QVariantMap surface_track{{QStringLiteral("seed"), persisted_seed},
                                    {QStringLiteral("fps"), 24.0},
                                    {QStringLiteral("trimStart"), 0.0},
                                    {QStringLiteral("frameCount"), 24}};
    updated.insert(QStringLiteral("motionTracking"),
                   QVariantMap{{QStringLiteral("surfaceTrack"), surface_track}});
    current_clip_ = updated;
    surface_track_sources_ = QVariantList{
        QVariantMap{{QStringLiteral("clipId"), QStringLiteral("surface-source")},
                    {QStringLiteral("displayLabel"), QStringLiteral("Wall pan")},
                    {QStringLiteral("occlusionAvailable"), true},
                    {QStringLiteral("minConfidence"), 0.82},
                    {QStringLiteral("meanErrorPx"), 1.7},
                    {QStringLiteral("sampleCount"), 24}},
        QVariantMap{{QStringLiteral("clipId"), QStringLiteral("surface-source-2")},
                    {QStringLiteral("displayLabel"), QStringLiteral("Desk shot")},
                    {QStringLiteral("occlusionAvailable"), true},
                    {QStringLiteral("minConfidence"), 0.71},
                    {QStringLiteral("meanErrorPx"), 2.1},
                    {QStringLiteral("sampleCount"), 18}},
    };
    emit selectionChanged();
    emit clipsChanged();
    return true;
  }

  Q_INVOKABLE bool correctSelectedSurfaceTrack(const QVariantList& quad) {
    last_surface_correction_quad_ = quad;
    ++surface_correction_count_;
    return true;
  }

  Q_INVOKABLE bool attachSurfaceTrackToSelectedClip(const QString& source_clip_id) {
    surface_attached_source_id_ = source_clip_id;
    auto updated = current_clip_;
    updated.insert(QStringLiteral("sceneAttachmentEnabled"), true);
    updated.insert(QStringLiteral("sceneAttachmentReady"), true);
    updated.insert(QStringLiteral("sceneAttachment"),
                   QVariantMap{{QStringLiteral("mode"), QStringLiteral("surface2d")},
                               {QStringLiteral("sourceClipId"), source_clip_id},
                               {QStringLiteral("widthScale"), 1.0},
                               {QStringLiteral("offsetX"), 0.0},
                               {QStringLiteral("offsetY"), 0.0},
                               {QStringLiteral("occlusionEnabled"), false},
                               {QStringLiteral("rotationDegrees"), 0.0}});
    current_clip_ = updated;
    ++surface_attach_count_;
    emit selectionChanged();
    return true;
  }

  Q_INVOKABLE bool attachSceneTrackToSelectedClip(const QString& source_clip_id,
                                                 const QStringList& point_ids) {
    scene_attached_source_id_ = source_clip_id;
    scene_attached_point_ids_ = point_ids;
    auto updated = current_clip_;
    updated.insert(QStringLiteral("sceneAttachmentEnabled"), true);
    updated.insert(QStringLiteral("sceneAttachmentReady"), true);
    updated.insert(QStringLiteral("sceneAttachment"),
                    QVariantMap{{QStringLiteral("widthScale"), 1.0},
                                {QStringLiteral("sourceClipId"), source_clip_id},
                                {QStringLiteral("offsetX"), 0.0},
                                {QStringLiteral("offsetY"), 0.0},
                                {QStringLiteral("offsetZ"), 0.0},
                                {QStringLiteral("occlusionEnabled"), false},
                                {QStringLiteral("rotationDegrees"), 0.0}});
    current_clip_ = updated;
    ++scene_attach_count_;
    emit selectionChanged();
    return true;
  }

  Q_INVOKABLE bool setSelectedSceneAttachmentPlacement(const QVariantMap&) { return true; }

  Q_INVOKABLE bool setSelectedSceneAttachmentOcclusion(bool enabled) {
    auto updated = current_clip_;
    auto attachment = updated.value(QStringLiteral("sceneAttachment")).toMap();
    attachment.insert(QStringLiteral("occlusionEnabled"), enabled);
    updated.insert(QStringLiteral("sceneAttachment"), attachment);
    current_clip_ = updated;
    ++occlusion_toggle_count_;
    emit selectionChanged();
    return true;
  }

  Q_INVOKABLE bool detachSelectedSceneAttachment() {
    auto updated = current_clip_;
    updated.insert(QStringLiteral("sceneAttachmentEnabled"), false);
    updated.insert(QStringLiteral("sceneAttachmentReady"), false);
    current_clip_ = updated;
    ++scene_detach_count_;
    emit selectionChanged();
    return true;
  }

  Q_INVOKABLE bool analyzeSelectedClipCutout() {
    ++cutout_analysis_count_;
    tracking_status_ = QStringLiteral("Cutout analysis started");
    emit trackingChanged();
    return true;
  }

  Q_INVOKABLE bool analyzeSelectedClipOcclusion(const QVariantMap& seed, bool temporal) {
    last_occlusion_seed_ = seed;
    last_occlusion_temporal_ = temporal;
    ++occlusion_analysis_count_;
    tracking_status_ = QStringLiteral("Occlusion mask analysis started");
    emit trackingChanged();
    return true;
  }

  Q_INVOKABLE bool analyzeSelectedClipRotoscope(const QVariantMap& seed) {
    last_rotoscope_seed_ = seed;
    ++rotoscope_analysis_count_;
    tracking_status_ = QStringLiteral("SAM2 rotoscope started");
    emit trackingChanged();
    return true;
  }

  Q_INVOKABLE void cancelTrackingAnalysis() {
    ++cancel_count_;
    tracking_busy_ = false;
    tracking_status_ = QStringLiteral("Analysis canceled");
    emit trackingChanged();
  }

  Q_INVOKABLE bool attachMotionTrackToSelectedClip(const QString& source_clip_id) {
    attached_source_id_ = source_clip_id;
    attached_target_id_ = current_clip_.value(QStringLiteral("clipId")).toString();
    ++attach_count_;
    return true;
  }

  Q_INVOKABLE bool setSelectedClipForegroundCutout(bool enabled) {
    auto updated = current_clip_;
    updated.insert(QStringLiteral("foregroundCutoutEnabled"), enabled);
    current_clip_ = updated;
    ++cutout_toggle_count_;
    emit selectionChanged();
    return true;
  }

  void setTrackingBusy(bool busy, double progress = 0.0) {
    tracking_busy_ = busy;
    tracking_progress_ = progress;
    tracking_status_ = busy ? QStringLiteral("Analyzing source frames")
                            : QStringLiteral("Analysis ready");
    emit trackingChanged();
  }

  void setRotoscopingAvailable(bool available) {
    rotoscoping_available_ = available;
    emit trackingChanged();
  }

  const QVariantMap& lastMotionSeed() const { return last_motion_seed_; }
  const QVariantMap& lastSurfaceSeed() const { return last_surface_seed_; }
  const QVariantMap& lastRotoscopeSeed() const { return last_rotoscope_seed_; }
  const QVariantMap& lastOcclusionSeed() const { return last_occlusion_seed_; }
  bool lastOcclusionTemporal() const { return last_occlusion_temporal_; }
  int motionAnalysisCount() const { return motion_analysis_count_; }
  int cutoutAnalysisCount() const { return cutout_analysis_count_; }
  int rotoscopeAnalysisCount() const { return rotoscope_analysis_count_; }
  int cancelCount() const { return cancel_count_; }
  int attachCount() const { return attach_count_; }
  int cutoutToggleCount() const { return cutout_toggle_count_; }
  int occlusionAnalysisCount() const { return occlusion_analysis_count_; }
  int occlusionToggleCount() const { return occlusion_toggle_count_; }
  int sceneAnalysisCount() const { return scene_analysis_count_; }
  int sceneAttachCount() const { return scene_attach_count_; }
  int sceneDetachCount() const { return scene_detach_count_; }
  int surfaceAnalysisCount() const { return surface_analysis_count_; }
  int surfaceAttachCount() const { return surface_attach_count_; }
  int surfaceCorrectionCount() const { return surface_correction_count_; }
  const QVariantList& lastSurfaceCorrectionQuad() const { return last_surface_correction_quad_; }
  QString surfaceAttachedSourceId() const { return surface_attached_source_id_; }
  QString sceneAttachedSourceId() const { return scene_attached_source_id_; }
  QStringList sceneAttachedPointIds() const { return scene_attached_point_ids_; }
  QString attachedSourceId() const { return attached_source_id_; }
  QString attachedTargetId() const { return attached_target_id_; }

 signals:
  void trackingChanged();
  void selectionChanged();
  void clipsChanged();
  void seedRectChanged();

 private:
  bool tracking_busy_ = false;
  bool rotoscoping_available_ = false;
  double tracking_progress_ = 0.0;
  QString tracking_status_;
  QVariantMap current_clip_;
  QVariantList motion_track_sources_;
  QVariantList scene_track_sources_;
  QVariantList surface_track_sources_;
  QVariantMap seed_rect_;
  QVariantMap last_motion_seed_;
  QVariantMap last_surface_seed_;
  QVariantList last_surface_correction_quad_;
  QVariantMap last_rotoscope_seed_;
  QVariantMap last_occlusion_seed_;
  bool last_occlusion_temporal_ = false;
  int motion_analysis_count_ = 0;
  int cutout_analysis_count_ = 0;
  int rotoscope_analysis_count_ = 0;
  int cancel_count_ = 0;
  int attach_count_ = 0;
  int cutout_toggle_count_ = 0;
  int occlusion_analysis_count_ = 0;
  int occlusion_toggle_count_ = 0;
  int scene_analysis_count_ = 0;
  int scene_attach_count_ = 0;
  int scene_detach_count_ = 0;
  int surface_analysis_count_ = 0;
  int surface_attach_count_ = 0;
  int surface_correction_count_ = 0;
  QString attached_source_id_;
  QString attached_target_id_;
  QString scene_attached_source_id_;
  QString surface_attached_source_id_;
  QStringList scene_attached_point_ids_;
};

namespace {
const char kTrackingPanelHostQml[] = R"QML(
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import "../apps/desktop/qml/components"

Window {
    objectName: "timelineTrackingTestWindow"
    width: 760
    height: 720
    visible: true
    color: "#12161b"

    Theme { id: looksyTheme }

    TimelineTrackingPanel {
        objectName: "timelineTrackingTestPanel"
        anchors.fill: parent
        theme: looksyTheme
        timelineDocument: trackingDocument
        currentClip: trackingDocument.currentClip
        motionTrackSources: trackingDocument.motionTrackSources
        sceneTrackSources: trackingDocument.sceneTrackSources
        sceneSourceClipId: "scene-source"
        scenePointIds: ["p1", "p2", "p3"]
        seedSelectionActive: false
        seedSelectionAllowed: true
        seedRectNormalized: trackingDocument.seedRect
        surfaceCorrectionAvailable: true
    }
}
)QML";

const char kScenePointOverlayHostQml[] = R"QML(
import QtQuick
import QtQuick.Window
import "../apps/desktop/qml/components"

Window {
    id: sceneOverlayHost
    width: 200
    height: 200
    visible: true
    color: "#101820"
    property var chosenPointIds: []
    TimelineScenePointOverlay {
        objectName: "timelineScenePointOverlayTest"
        anchors.fill: parent
        points: [
            {"id": "p1", "x": 0.20, "y": 0.20},
            {"id": "p2", "x": 0.80, "y": 0.20},
            {"id": "p3", "x": 0.20, "y": 0.80},
            {"id": "p4", "x": 0.82, "y": 0.82}
        ]
        sourceBounds: Qt.rect(0, 0, 200, 200)
        selectedPointIds: sceneOverlayHost.chosenPointIds
        selectionEnabled: true
        onSelectedPointsChanged: function(pointIds) {
            sceneOverlayHost.chosenPointIds = pointIds
        }
    }
}
)QML";

const char kSurfaceCorrectionOverlayHostQml[] = R"QML(
import QtQuick
import QtQuick.Window
import "../apps/desktop/qml/components"

Window {
    id: correctionHost
    width: 300
    height: 240
    visible: true
    color: "#101820"
    Theme { id: looksyTheme }
    TimelineSurfaceCorrectionOverlay {
        objectName: "timelineSurfaceCorrectionOverlayTest"
        anchors.fill: parent
        theme: looksyTheme
        surfaceQuad: [
            {"x": 0.15, "y": 0.20}, {"x": 0.75, "y": 0.16},
            {"x": 0.20, "y": 0.75}, {"x": 0.80, "y": 0.78}
        ]
        sourceWidth: 1600
        sourceHeight: 900
        sourceFillMode: Image.PreserveAspectFit
    }
}
)QML";

const char kRotoscopeCorrectionOverlayHostQml[] = R"QML(
import QtQuick
import QtQuick.Window
import "../apps/desktop/qml/components"

Window {
    id: host
    property var draftPoints: []
    width: 200
    height: 200
    visible: true
    TimelineRotoscopeCorrectionOverlay {
        objectName: "timelineRotoscopeCorrectionOverlayTest"
        anchors.fill: parent
        sourceBounds: Qt.rect(20, 20, 160, 160)
        inputEnabled: true
        points: host.draftPoints
        onCorrectionPointsChanged: function(points) { host.draftPoints = points }
    }
}
)QML";

void settle() {
  for (int index = 0; index < 24; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

QQuickItem* find_item(QQuickItem* parent, const QString& name) {
  if (parent == nullptr)
    return nullptr;
  if (parent->objectName() == name)
    return parent;
  for (auto* child : parent->childItems()) {
    if (auto* found = find_item(child, name); found != nullptr)
      return found;
  }
  return nullptr;
}

void click_item(QQuickWindow* window, QQuickItem* item) {
  const auto point = item->mapToScene(item->boundingRect().center());
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
  settle();
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QGuiApplication app(argc, argv);

  FakeTimelineTrackingDocument tracking_document;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("trackingDocument"),
                                           &tracking_document);
  QQmlComponent component(&engine);
  component.setData(
      QByteArray(kTrackingPanelHostQml),
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR
                                         "/tests/timeline_tracking_panel_ui_test.qml")));
  std::unique_ptr<QObject> object(component.create());
  if (object == nullptr) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("TimelineTrackingPanel should instantiate with the timeline controller API.");
  }
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (window == nullptr)
    return fail("Timeline tracking test fixture should create a native QQuickWindow.");
  window->show();
  settle();

  auto* panel = find_item(window->contentItem(), QStringLiteral("timelineTrackingTestPanel"));
  auto* region_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineMotionSeedButton"));
  auto* motion_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineAnalyzeMotionButton"));
  auto* cutout_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineAnalyzeCutoutButton"));
  auto* rotoscope_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineAnalyzeRotoscopeButton"));
  auto* correct_rotoscope_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineCorrectRotoscopeButton"));
  auto* occlusion_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineGenerateOcclusionMaskButton"));
  auto* occlusion_temporal_toggle = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineOcclusionTemporalModeCheckBox"));
  auto* rotoscope_setup = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineRotoscopeSetupDisclosure"));
  auto* attach_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineAttachMotionTrackButton"));
  auto* cancel_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineCancelTrackingButton"));
  auto* cutout_toggle = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineForegroundCutoutEnabledCheckBox"));
  auto* scene_solve_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineSolveSceneCameraButton"));
  auto* scene_points_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineSelectScenePlanePointsButton"));
  auto* scene_attach_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineAttachSceneTrackButton"));
  auto* scene_detach_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineDetachSceneTrackButton"));
  auto* surface_seed_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineSurfaceSeedButton"));
  auto* surface_track_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineAnalyzeSurfaceButton"));
  auto* surface_attach_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineAttachSurfaceTrackButton"));
  auto* surface_detach_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineDetachSurfaceTrackButton"));
  auto* surface_status = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineSurfaceTrackStatus"));
  auto* surface_exclusion_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineAddSurfaceExclusionButton"));
  auto* surface_exclusion_count = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineSurfaceExclusionCount"));
  auto* surface_retrack_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineRetrackSurfaceButton"));
  auto* surface_correction_button = panel == nullptr ? nullptr
      : find_item(panel, QStringLiteral("timelineCorrectSurfaceAtPlayheadButton"));
  if (!panel || !region_button || !motion_button || !cutout_button || !rotoscope_button ||
      !correct_rotoscope_button ||
      !occlusion_button || !occlusion_temporal_toggle ||
      !rotoscope_setup || !attach_button || !cancel_button || !cutout_toggle ||
      !scene_solve_button || !scene_points_button ||
      !scene_attach_button || !scene_detach_button || !surface_seed_button ||
      !surface_track_button || !surface_attach_button || !surface_status ||
      !surface_detach_button ||
      !surface_exclusion_button || !surface_exclusion_count || !surface_retrack_button ||
      !surface_correction_button)
    return fail("Timeline tracking panel should expose surface, motion, 3D scene, and cutout controls.");

  if (surface_track_button->isEnabled() || surface_attach_button->isEnabled())
    return fail("Surface tracking should wait for a video source and surface attachment should wait for a track.");
  tracking_document.setCurrentClip(QVariantMap{
      {QStringLiteral("clipId"), QStringLiteral("surface-video")},
      {QStringLiteral("displayLabel"), QStringLiteral("Surface source")},
      {QStringLiteral("trackKind"), QStringLiteral("video")},
      {QStringLiteral("mediaType"), QStringLiteral("video")},
      {QStringLiteral("foregroundCutoutEnabled"), false},
      {QStringLiteral("foregroundCutoutReady"), false},
  });
  settle();
  if (!surface_seed_button->isEnabled() || !surface_track_button->isEnabled())
    return fail("A selected video with a source-normalized seed should enable surface analysis.");
  QSignalSpy surface_seed_requested(panel, SIGNAL(seedSelectionRequested()));
  if (!surface_seed_requested.isValid())
    return fail("Surface selection should use the shared seed-selection request.");
  click_item(window, surface_seed_button);
  if (surface_seed_requested.count() != 1)
    return fail("Choose surface should open the shared Program monitor seed selection.");
  click_item(window, surface_track_button);
  const auto surface_seed = tracking_document.lastSurfaceSeed();
  if (tracking_document.surfaceAnalysisCount() != 1 ||
      qAbs(surface_seed.value(QStringLiteral("x")).toDouble() - 0.18) > 0.001 ||
      qAbs(surface_seed.value(QStringLiteral("height")).toDouble() - 0.36) > 0.001)
    return fail("Track Surface should pass the shared normalized seed rectangle into source analysis.");

  QSignalSpy exclusion_requested(panel, SIGNAL(surfaceExclusionSelectionRequested()));
  if (!exclusion_requested.isValid() || !surface_exclusion_button->isVisible())
    return fail("A tracked source should offer a first-frame exclusion region.");
  click_item(window, surface_exclusion_button);
  if (exclusion_requested.count() != 1 ||
      !panel->property("surfaceExclusionSelectionPending").toBool())
    return fail("Add exclusion should request the shared region picker and wait for Done.");
  const QVariantMap exclusion{{QStringLiteral("x"), 0.62},
                              {QStringLiteral("y"), 0.12},
                              {QStringLiteral("width"), 0.18},
                              {QStringLiteral("height"), 0.25}};
  if (!QMetaObject::invokeMethod(panel, "acceptSurfaceExclusionSelection",
                                 Qt::DirectConnection, Q_ARG(QVariant, QVariant(exclusion))))
    return fail("The tracking panel should accept the completed exclusion rectangle.");
  settle();
  if (!surface_retrack_button->isEnabled() ||
      !surface_exclusion_count->property("text").toString().contains(QStringLiteral("1 exclusion")))
    return fail("A valid normalized exclusion should enable retracking and appear in the summary.");
  click_item(window, surface_retrack_button);
  const auto retrack_seed = tracking_document.lastSurfaceSeed();
  const auto retrack_exclusions = retrack_seed.value(QStringLiteral("exclusions")).toList();
  if (tracking_document.surfaceAnalysisCount() != 2 || retrack_exclusions.size() != 1 ||
      qAbs(retrack_exclusions.front().toMap().value(QStringLiteral("x")).toDouble() - 0.62) > 0.001 ||
      retrack_seed.value(QStringLiteral("correctionFrames")).toList().size() != 1)
    return fail("Retrack should resubmit the seed with the accepted normalized exclusion.");

  QSignalSpy surface_correction_requested(panel, SIGNAL(surfaceCorrectionRequested()));
  if (!surface_correction_requested.isValid() || !surface_correction_button->isEnabled())
    return fail("A ready paused surface should enable current-frame correction.");
  click_item(window, surface_correction_button);
  if (surface_correction_requested.count() != 1)
    return fail("Correct current frame should open the Program corner editor.");

  tracking_document.setSurfaceTrackDraftForTest();
  settle();
  if (!surface_correction_button->isVisible() || !surface_correction_button->isEnabled() ||
      surface_correction_button->property("text").toString() != QStringLiteral("Correct corners") ||
      !surface_retrack_button->isEnabled())
    return fail("A failed solve with a cached draft should remain repairable and allow correction followed by retracking.");
  click_item(window, surface_retrack_button);
  if (tracking_document.surfaceAnalysisCount() != 3 ||
      tracking_document.lastSurfaceSeed().value(QStringLiteral("correctionFrames")).toList().isEmpty())
    return fail("Retrack should preserve correction frames from a failed cached surface draft.");

  tracking_document.setCurrentClip(QVariantMap{
      {QStringLiteral("clipId"), QStringLiteral("target-title")},
      {QStringLiteral("displayLabel"), QStringLiteral("Title overlay")},
      {QStringLiteral("trackKind"), QStringLiteral("video")},
      {QStringLiteral("mediaType"), QStringLiteral("title")},
  });
  settle();
  panel->setProperty("selectedSurfaceSourceClipId", QStringLiteral("surface-source-2"));
  settle();
  if (!surface_attach_button->isEnabled() ||
      !surface_status->property("text").toString().contains(QStringLiteral("71%")))
    return fail("A tracked source should show its confidence summary and enable attachment to a title.");
  click_item(window, surface_attach_button);
  auto* surface_occlusion = find_item(panel, QStringLiteral("timelineSurfaceAttachmentOcclusionCheckBox"));
  if (!surface_occlusion || !surface_occlusion->isVisible() || !surface_occlusion->isEnabled())
    return fail("A surface attachment with a ready source matte should expose occlusion controls.");
  click_item(window, surface_occlusion);
  if (tracking_document.occlusionToggleCount() != 1 ||
      !tracking_document.currentClip().value(QStringLiteral("sceneAttachment")).toMap()
           .value(QStringLiteral("occlusionEnabled")).toBool())
    return fail("Surface attachment occlusion should persist independently from its plane placement.");
  if (tracking_document.surfaceAttachCount() != 1 ||
      tracking_document.surfaceAttachedSourceId() != QStringLiteral("surface-source-2") ||
      tracking_document.currentClip().value(QStringLiteral("sceneAttachment")).toMap()
          .value(QStringLiteral("mode")).toString() != QStringLiteral("surface2d"))
    return fail("Attach surface should preserve the chosen source and mark the target as a 2D surface attachment.");
  if (!surface_detach_button->isVisible())
    return fail("A 2D surface attachment should expose its own detach action.");
  click_item(window, surface_detach_button);
  if (tracking_document.sceneDetachCount() != 1)
    return fail("Detach should remove a 2D surface attachment.");
  tracking_document.setCurrentClip(QVariantMap{
      {QStringLiteral("clipId"), QStringLiteral("target-caption")},
      {QStringLiteral("displayLabel"), QStringLiteral("Caption overlay")},
      {QStringLiteral("trackKind"), QStringLiteral("video")},
      {QStringLiteral("mediaType"), QStringLiteral("caption")},
  });
  settle();
  if (panel->property("selectedSurfaceSourceClipId").toString() != QStringLiteral("surface-source-2"))
    return fail("The selected surface source should persist when the selected target clip changes.");

  tracking_document.setCurrentClip(QVariantMap{
      {QStringLiteral("clipId"), QStringLiteral("target-title")},
      {QStringLiteral("displayLabel"), QStringLiteral("Title overlay")},
      {QStringLiteral("trackKind"), QStringLiteral("video")},
      {QStringLiteral("mediaType"), QStringLiteral("title")},
  });
  settle();

  click_item(window, attach_button);
  if (tracking_document.attachCount() != 1 ||
      tracking_document.attachedSourceId() != QStringLiteral("tracked-source") ||
      tracking_document.attachedTargetId() != QStringLiteral("target-title"))
    return fail("Attach should apply the selected source track to the selected title target.");

  if (scene_solve_button->isEnabled())
    return fail("Scene camera solve should require a selected video source clip.");
  auto* scene_section = find_item(panel, QStringLiteral("timelineSceneCameraDetails"));
  QSignalSpy scene_points_requested(panel,
                                    SIGNAL(scenePointSelectionRequested(QString,QVariant)));
  if (!scene_points_requested.isValid() || !scene_points_button->isEnabled() ||
      !scene_section || !scene_section->isVisible())
    return fail("Scene Camera should remain a visible peer section with reconstructed point selection available.");
  click_item(window, scene_points_button);
  if (scene_points_requested.count() != 1)
    return fail("Select 3 Points should request the visible reconstructed features at the paused playhead.");
  if (!scene_attach_button->isEnabled())
    return fail("Three selected static points should enable scene attachment to the selected title.");
  click_item(window, scene_attach_button);
  if (tracking_document.sceneAttachCount() != 1 ||
      tracking_document.sceneAttachedSourceId() != QStringLiteral("scene-source") ||
      tracking_document.sceneAttachedPointIds() !=
          QStringList{QStringLiteral("p1"), QStringLiteral("p2"), QStringLiteral("p3")} ||
      !scene_detach_button->isVisible())
    return fail("Attach Scene should persist the source and three plane point IDs on the target title.");
  auto* scene_occlusion = find_item(panel, QStringLiteral("timelineSceneAttachmentOcclusionCheckBox"));
  if (!scene_occlusion || !scene_occlusion->isVisible() || !scene_occlusion->isEnabled())
    return fail("A scene attachment with a ready source matte should expose occlusion controls.");
  click_item(window, scene_occlusion);
  if (tracking_document.occlusionToggleCount() != 2 ||
      !tracking_document.currentClip().value(QStringLiteral("sceneAttachment")).toMap()
           .value(QStringLiteral("occlusionEnabled")).toBool())
    return fail("Scene attachment occlusion should be a separate attachment setting.");
  click_item(window, scene_detach_button);
  if (tracking_document.sceneDetachCount() != 2)
    return fail("Detach should remove the scene attachment from the selected target.");


  const QVariantMap source_clip{
      {QStringLiteral("clipId"), QStringLiteral("source-video")},
      {QStringLiteral("displayLabel"), QStringLiteral("Source video")},
      {QStringLiteral("trackKind"), QStringLiteral("video")},
      {QStringLiteral("mediaType"), QStringLiteral("video")},
      {QStringLiteral("foregroundCutoutEnabled"), false},
      {QStringLiteral("foregroundCutoutReady"), false},
      {QStringLiteral("rotoCorrectionReady"), true},
  };
  tracking_document.setCurrentClip(source_clip);
  settle();
  QSignalSpy roto_correction_requested(panel, SIGNAL(rotoCorrectionRequested()));
  if (!roto_correction_requested.isValid() || !correct_rotoscope_button->isVisible() ||
      !correct_rotoscope_button->isEnabled())
    return fail("A repairable SAM2 matte should offer paused-frame correction on the selected source.");
  click_item(window, correct_rotoscope_button);
  if (roto_correction_requested.count() != 1)
    return fail("Refine Matte should open the paused-source correction overlay.");
  if (!motion_button->isEnabled() || !cutout_button->isEnabled() || !scene_solve_button->isEnabled())
    return fail("Video source clips should enable motion and cutout analysis.");
  if (!occlusion_button->isEnabled())
    return fail("Source video should expose an independent occlusion-mask generation action.");
  click_item(window, occlusion_button);
  if (tracking_document.occlusionAnalysisCount() != 1 ||
      tracking_document.lastOcclusionTemporal() ||
      tracking_document.lastOcclusionSeed().value(QStringLiteral("width")).toDouble() <= 0 ||
      tracking_document.currentClip().value(QStringLiteral("foregroundCutoutEnabled")).toBool())
    return fail("The automatic occlusion-mask action should remain separate from enabling the source cutout.");

  click_item(window, scene_solve_button);
  if (tracking_document.sceneAnalysisCount() != 1)
    return fail("Solve Scene Camera should invoke the temporal camera reconstruction API.");

  QSignalSpy seed_requested(panel, SIGNAL(seedSelectionRequested()));
  if (!seed_requested.isValid())
    return fail("Seed selection should expose a callable preview-region action.");
  click_item(window, region_button);
  if (seed_requested.count() != 1)
    return fail("Set Region should request a Program monitor seed selection.");

  click_item(window, motion_button);
  const auto seed = tracking_document.lastMotionSeed();
  if (tracking_document.motionAnalysisCount() != 1 ||
      qAbs(seed.value(QStringLiteral("x")).toDouble() - 0.18) > 0.001 ||
      qAbs(seed.value(QStringLiteral("y")).toDouble() - 0.20) > 0.001 ||
      qAbs(seed.value(QStringLiteral("width")).toDouble() - 0.24) > 0.001 ||
      qAbs(seed.value(QStringLiteral("height")).toDouble() - 0.36) > 0.001)
    return fail("Track Motion should pass the raw-source normalized seed rectangle.");

  if (rotoscope_button->isEnabled() || !rotoscope_setup->isVisible())
    return fail("SAM2 Rotoscope should disclose local model setup and stay disabled when unavailable.");
  tracking_document.setRotoscopingAvailable(true);
  settle();
  if (!rotoscope_button->isEnabled())
    return fail("A configured local SAM2 runtime should enable Rotoscope for a seeded video source.");
  click_item(window, rotoscope_button);
  const auto rotoscope_seed = tracking_document.lastRotoscopeSeed();
  if (tracking_document.rotoscopeAnalysisCount() != 1 ||
      qAbs(rotoscope_seed.value(QStringLiteral("x")).toDouble() - 0.18) > 0.001 ||
      qAbs(rotoscope_seed.value(QStringLiteral("height")).toDouble() - 0.36) > 0.001)
    return fail("SAM2 Rotoscope should pass the same normalized seed into the local analysis API.");
  occlusion_temporal_toggle->setProperty("checked", true);
  settle();
  click_item(window, occlusion_button);
  if (tracking_document.occlusionAnalysisCount() != 2 ||
      !tracking_document.lastOcclusionTemporal())
    return fail("SAM2 temporal mode should call the dedicated occlusion analysis API without enabling source cutout.");

  tracking_document.setTrackingBusy(true, 0.42);
  settle();
  if (!cancel_button->isVisible() || !cancel_button->isEnabled())
    return fail("An active analysis should display an enabled Cancel action.");
  click_item(window, cancel_button);
  if (tracking_document.cancelCount() != 1 || tracking_document.trackingBusy())
    return fail("Cancel should stop the active tracking or cutout analysis.");

  click_item(window, cutout_button);
  if (tracking_document.cutoutAnalysisCount() != 1)
    return fail("Auto Cutout should invoke foreground matte analysis.");

  if (cutout_toggle->isEnabled())
    return fail("Foreground cutout should remain unavailable until a valid matte is ready.");
  auto ready_clip = source_clip;
  ready_clip.insert(QStringLiteral("foregroundCutoutReady"), true);
  tracking_document.setCurrentClip(ready_clip);
  settle();
  if (!cutout_toggle->isEnabled())
    return fail("A ready matte should enable the per-source cutout toggle.");
  click_item(window, cutout_toggle);
  if (tracking_document.cutoutToggleCount() != 1 ||
      !tracking_document.currentClip().value(QStringLiteral("foregroundCutoutEnabled")).toBool())
    return fail("The cutout toggle should update the selected source clip.");

  QQmlApplicationEngine overlay_engine;
  QQmlComponent overlay_component(&overlay_engine);
  overlay_component.setData(
      QByteArray(kScenePointOverlayHostQml),
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR
                                         "/tests/timeline_tracking_panel_ui_test.qml")));
  std::unique_ptr<QObject> overlay_object(overlay_component.create());
  if (overlay_object == nullptr) {
    std::cerr << overlay_component.errorString().toStdString() << '\n';
    return fail("Scene feature overlay should instantiate with normalized source points.");
  }
  auto* overlay_window = qobject_cast<QQuickWindow*>(overlay_object.get());
  auto* point_overlay = overlay_window == nullptr ? nullptr
      : find_item(overlay_window->contentItem(), QStringLiteral("timelineScenePointOverlayTest"));
  if (overlay_window == nullptr || point_overlay == nullptr)
    return fail("Scene feature overlay should expose a native hit-test surface.");
  settle();
  QSignalSpy point_selection_changed(point_overlay,
                                     SIGNAL(selectedPointsChanged(QVariant)));
  if (!point_selection_changed.isValid())
    return fail("Scene feature overlay should publish stable point ID selections.");
  const auto click_feature = [&](double x, double y) {
    const auto source_bounds = point_overlay->property("sourceBounds").toRectF();
    const auto point = point_overlay->mapToScene(
        QPointF(source_bounds.x() + x * source_bounds.width(),
                source_bounds.y() + y * source_bounds.height()));
    QTest::mouseClick(overlay_window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
    settle();
  };
  click_feature(0.20, 0.20);
  click_feature(0.80, 0.20);
  click_feature(0.20, 0.80);
  if (point_selection_changed.count() != 3)
    return fail("Clicking three separated reconstructed points should select each one.");
  QStringList selected_points;
  for (const auto& point_id : point_selection_changed.last().at(0).toList())
    selected_points.push_back(point_id.toString());
  if (selected_points != QStringList{QStringLiteral("p1"), QStringLiteral("p2"), QStringLiteral("p3")}) {
    std::cerr << "Selected scene point IDs: ";
    for (const auto& point_id : selected_points)
      std::cerr << point_id.toStdString() << ' ';
    std::cerr << '\n';
    return fail("Scene point selection should preserve the clicked stable feature IDs in order.");
  }

  QQmlApplicationEngine roto_overlay_engine;
  QQmlComponent roto_overlay_component(&roto_overlay_engine);
  roto_overlay_component.setData(
      QByteArray(kRotoscopeCorrectionOverlayHostQml),
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR
                                         "/tests/timeline_tracking_panel_ui_test.cpp")));
  std::unique_ptr<QObject> roto_overlay_object(roto_overlay_component.create());
  if (roto_overlay_object == nullptr) {
    std::cerr << roto_overlay_component.errorString().toStdString() << '\n';
    return fail("SAM2 correction overlay should instantiate with normalized source coordinates.");
  }
  auto* roto_overlay_window = qobject_cast<QQuickWindow*>(roto_overlay_object.get());
  auto* roto_overlay = roto_overlay_window == nullptr ? nullptr
      : find_item(roto_overlay_window->contentItem(),
                  QStringLiteral("timelineRotoscopeCorrectionOverlayTest"));
  if (!roto_overlay_window || !roto_overlay)
    return fail("SAM2 correction overlay should expose its source-frame hit-test surface.");
  settle();
  QSignalSpy roto_points_changed(roto_overlay, SIGNAL(correctionPointsChanged(QVariant)));
  if (!roto_points_changed.isValid())
    return fail("SAM2 correction overlay should publish normalized positive/negative point prompts.");
  auto click_roto_source = [&](double x, double y, Qt::KeyboardModifiers modifiers) {
    const auto bounds = roto_overlay->property("sourceBounds").toRectF();
    const auto point = roto_overlay->mapToScene(
        QPointF(bounds.x() + x * bounds.width(), bounds.y() + y * bounds.height()));
    QTest::mouseClick(roto_overlay_window, Qt::LeftButton, modifiers, point.toPoint());
    settle();
  };
  click_roto_source(0.5, 0.5, Qt::NoModifier);
  click_roto_source(0.75, 0.25, Qt::AltModifier);
  if (roto_points_changed.count() != 2)
    return fail("SAM2 correction overlay should add points directly over the paused source image.");
  const auto correction_points = roto_points_changed.last().at(0).toList();
  if (correction_points.size() != 2 ||
      correction_points.at(0).toMap().value(QStringLiteral("label")).toInt() != 1 ||
      correction_points.at(1).toMap().value(QStringLiteral("label")).toInt() != 0 ||
      qAbs(correction_points.at(1).toMap().value(QStringLiteral("x")).toDouble() - 0.75) > 0.01)
    return fail("SAM2 correction points should carry normalized coordinates and Alt-click negative labels.");
  roto_overlay_object->setProperty("draftPoints", QVariantList{correction_points.first()});
  settle();
  click_roto_source(0.75, 0.25, Qt::AltModifier);
  if (roto_points_changed.last().at(0).toList().size() != 2)
    return fail("Adding a point after external Undo must use the parent's current draft, without restoring the undone point.");

  QQmlApplicationEngine correction_engine;
  QQmlComponent correction_component(&correction_engine);
  correction_component.setData(
      QByteArray(kSurfaceCorrectionOverlayHostQml),
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR
                                         "/tests/timeline_tracking_panel_ui_test.qml")));
  std::unique_ptr<QObject> correction_object(correction_component.create());
  if (correction_object == nullptr) {
    std::cerr << correction_component.errorString().toStdString() << '\n';
    return fail("Surface correction overlay should instantiate over a paused source frame.");
  }
  auto* correction_window = qobject_cast<QQuickWindow*>(correction_object.get());
  auto* correction_overlay = correction_window == nullptr ? nullptr
      : find_item(correction_window->contentItem(), QStringLiteral("timelineSurfaceCorrectionOverlayTest"));
  auto* first_corner = correction_overlay == nullptr ? nullptr
      : find_item(correction_overlay, QStringLiteral("timelineSurfaceCorrectionHandle0"));
  auto* apply_correction = correction_overlay == nullptr ? nullptr
      : find_item(correction_overlay, QStringLiteral("timelineSurfaceCorrectionApplyButton"));
  if (correction_window == nullptr || !correction_overlay || !first_corner || !apply_correction)
    return fail("Surface correction should expose draggable source-frame corner handles and an apply action.");
  settle();
  QSignalSpy correction_accepted(correction_overlay, SIGNAL(correctionAccepted(QVariant)));
  if (!correction_accepted.isValid())
    return fail("Applying surface corners should emit the normalized correction quad.");
  const auto first_corner_center = first_corner->mapToScene(first_corner->boundingRect().center());
  QTest::mousePress(correction_window, Qt::LeftButton, Qt::NoModifier,
                    first_corner_center.toPoint());
  QTest::mouseMove(correction_window, first_corner_center.toPoint() + QPoint(24, 18));
  QTest::mouseRelease(correction_window, Qt::LeftButton, Qt::NoModifier,
                      first_corner_center.toPoint() + QPoint(24, 18));
  settle();
  click_item(correction_window, apply_correction);
  if (correction_accepted.count() != 1 ||
      correction_accepted.last().at(0).toList().size() != 4 ||
      qAbs(correction_accepted.last().at(0).toList().front().toMap()
               .value(QStringLiteral("x")).toDouble() - 0.15) < 0.02)
    return fail("Dragging a corner should update and submit one complete normalized four-corner surface quad.");
  tracking_document.correctSelectedSurfaceTrack(correction_accepted.last().at(0).toList());
  if (tracking_document.surfaceCorrectionCount() != 1 ||
      tracking_document.lastSurfaceCorrectionQuad().size() != 4)
    return fail("The corrected normalized quad should reach the source retracking API.");

  const auto screenshot = window->grabWindow();
  if (screenshot.isNull() || screenshot.size() != QSize(760, 720))
    return fail("The native panel should render a complete offscreen UI frame.");
  const auto screenshot_path = qEnvironmentVariable("PHOTO_EDITOR_TRACKING_UI_SCREENSHOT");
  if (!screenshot_path.isEmpty() && !screenshot.save(screenshot_path))
    return fail("Could not save the requested tracking UI evidence.");

  std::cout << "Timeline tracking seed, progress cancellation, tracked-source attachment, and cutout readiness workflow passed\n";
  return 0;
}

#include "timeline_tracking_panel_ui_test.moc"
