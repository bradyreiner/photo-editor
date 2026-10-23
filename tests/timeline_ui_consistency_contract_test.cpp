#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("unable to read " + path.string());
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

bool contains(const std::string& source, const std::string& expected) {
  return source.find(expected) != std::string::npos;
}

int fail(const std::string& message) {
  std::cerr << "timeline UI consistency contract failed: " << message << '\n';
  return 1;
}

double srgb_channel(double value) {
  const auto normalized = value / 255.0;
  return normalized <= 0.04045
             ? normalized / 12.92
             : std::pow((normalized + 0.055) / 1.055, 2.4);
}

double relative_luminance(const std::string& hex) {
  if (hex.size() != 7 || hex.front() != '#') {
    throw std::runtime_error("invalid hex color " + hex);
  }
  const auto component = [&hex](std::size_t offset) {
    return static_cast<double>(std::stoi(hex.substr(offset, 2), nullptr, 16));
  };
  return 0.2126 * srgb_channel(component(1)) +
         0.7152 * srgb_channel(component(3)) +
         0.0722 * srgb_channel(component(5));
}

double contrast_ratio(const std::string& first, const std::string& second) {
  const auto first_luminance = relative_luminance(first);
  const auto second_luminance = relative_luminance(second);
  const auto lighter = std::max(first_luminance, second_luminance);
  const auto darker = std::min(first_luminance, second_luminance);
  return (lighter + 0.05) / (darker + 0.05);
}

}  // namespace

int main() {
  try {
    const std::filesystem::path root(PHOTO_EDITOR_SOURCE_DIR);
    const auto qml = root / "apps/desktop/qml";
    const auto components = qml / "components";
    const auto main_qml = read_text(qml / "Main.qml");
    const auto timeline_view = read_text(qml / "workspaces/TimelineView.qml");
    const auto export_view = read_text(qml / "workspaces/ExportView.qml");
    const auto source_panel = read_text(components / "TimelineSourcePanel.qml");
    const auto effects_panel = read_text(components / "TimelineEffectsPanel.qml");
    const auto platform_overlay = read_text(components / "TimelinePlatformOverlay.qml");
    const auto viewer_guide_overlay =
        read_text(components / "TimelineViewerGuideOverlay.qml");
    const auto multicam_angle_monitor =
        read_text(components / "TimelineMulticamAngleMonitor.qml");
    const auto effect_library_row = read_text(components / "TimelineEffectLibraryRow.qml");
    const auto track_header = read_text(components / "TimelineTrackHeader.qml");
    const auto track_toggle = read_text(components / "TimelineTrackToggleButton.qml");
    const auto marker_pin = read_text(components / "TimelineMarkerPin.qml");
    const auto media_asset_row = read_text(components / "TimelineMediaAssetRow.qml");
    const auto audio_meter = read_text(components / "TimelineAudioMeter.qml");
    const auto audio_mixer = read_text(components / "TimelineAudioMixer.qml");
    const auto pancake_editor = read_text(components / "TimelinePancakeEditor.qml");
    const auto difference_effect = read_text(components / "TimelineDifferenceEffect.qml");
    const auto timeline_scopes = read_text(components / "TimelineScopesPanel.qml");
    const auto gallery_picker = read_text(components / "GalleryPickerDialog.qml");
    const auto waveform = read_text(components / "TimelineWaveform.qml");
    const auto clip_content = read_text(components / "TimelineClipContent.qml");
    const auto theme = read_text(components / "Theme.qml");
    const auto action_button = read_text(components / "ActionButton.qml");
    const auto app_icon = read_text(components / "AppIcon.qml");
    const auto tool_button = read_text(components / "ToolButton.qml");
    const auto filter_chip = read_text(components / "FilterChip.qml");
    const auto menu_button = read_text(components / "MenuButton.qml");
    const auto app_menu = read_text(components / "AppMenu.qml");
    const auto preferences = read_text(components / "AppPreferencesDialog.qml");
    const auto shortcuts = read_text(components / "KeyboardShortcutsDialog.qml");
    const auto cmake = read_text(root / "apps/desktop/CMakeLists.txt");
    const auto catalog_model_cpp = read_text(root / "apps/desktop/src/catalog_asset_model.cpp");
    const auto checklist = read_text(root / "docs/timeline-ui-consistency-checklist.md");
    const auto runtime_proof = read_text(root / "tests/actual_app_workflow_benchmark_driver.cpp");
    const auto desktop_main = read_text(root / "apps/desktop/src/main.cpp");
    const auto playback_controller =
        read_text(root / "apps/desktop/src/timeline_playback_controller.cpp");
    const auto audio_output = read_text(root / "apps/desktop/src/timeline_audio_output.cpp");
    const auto timeline_document_controller =
        read_text(root / "apps/desktop/src/timeline_document_controller.cpp");
    const auto timeline_document_controller_header =
        read_text(root / "apps/desktop/src/timeline_document_controller.h");
    const auto decoded_image_cache =
        read_text(root / "apps/desktop/src/decoded_image_cache.cpp");
    const auto editor_preview_controller =
        read_text(root / "apps/desktop/src/editor_preview_controller.cpp");
    const auto timeline_document_test =
        read_text(root / "tests/timeline_document_controller_test.cpp");
    const auto timeline_playback_test =
        read_text(root / "tests/timeline_playback_controller_test.cpp");
    const auto job_queue_model =
        read_text(root / "apps/desktop/src/job_queue_model.cpp");
    const auto job_runner = read_text(root / "apps/desktop/src/job_runner.cpp");
    const auto job_admission_policy =
        read_text(root / "apps/desktop/src/job_admission_policy.h");
    const auto job_store = read_text(root / "apps/desktop/src/job_store.cpp");
    const auto worker_queue_registry =
        read_text(root / "apps/desktop/src/worker_queue_registry.cpp");
    const auto job_queue_model_test = read_text(root / "tests/job_queue_model_test.cpp");
    const auto timeline_recovery_test =
        read_text(root / "tests/timeline_recovery_test.cpp");

    if (!contains(timeline_view,
                  "timeline-ui-consistency:v1|layout:source-program-inspector-multitrack|"
                  "theme:shared|type:shared|spacing:shared|controls:shared|"
                  "popups:shared-lifecycle|states:accessible|video-editor-identity:preserved")) {
      return fail("the protected shared-style/video-editor layout contract is missing");
    }
    if (!contains(
            timeline_view,
            "persistent-render-queue:v1|storage:job-store|ordering:priority-then-manual-order|"
            "actions:move-priority-pause-resume-cancel-retry-duplicate-reveal|"
            "surface:background-jobs-progressive-more-menu|relaunch:explicit-confirmation") ||
        !contains(
            export_view,
            "persistent-render-queue:v1|jobs:asset-export-and-timeline-render|storage:job-store|"
            "ordering:priority-then-manual-order|actions:move-priority-pause-resume-cancel-retry-duplicate-reveal|"
            "recovery:explicit-confirmation|ui:progressive-row-menu") ||
        !contains(timeline_view, "function persistentRenderJobMenuItems(job)") ||
        !contains(timeline_view, "jobQueue.moveRenderJob(id, -1)") ||
        !contains(timeline_view, "jobQueue.duplicateRenderJob(id)") ||
        !contains(timeline_view, "jobQueue.revealJobOutput(id)") ||
        !contains(export_view, "function jobRowMenuItems(status, jobKind, outputPath, priority,") ||
        !contains(export_view, "jobQueue.setJobPriority(jobId, key.slice(\"priority-\".length))") ||
        !contains(export_view, "jobQueue.revealJobOutput(jobId)") ||
        !contains(job_queue_model, "bool JobQueueModel::moveRenderJob(") ||
        !contains(job_queue_model, "QString JobQueueModel::duplicateRenderJob(") ||
        !contains(job_queue_model, "QString JobQueueModel::revealOutputPath(") ||
        !contains(job_store, "record.job_kind != QStringLiteral(\"timeline_render\")") ||
        !contains(job_runner,
                  "a.priority == b.priority ? job_order_less_than(*a.job, *b.job) : a.priority > b.priority") ||
        !contains(job_admission_policy,
                  "job_order_less_than(const PersistentJobRecord& lhs, const PersistentJobRecord& rhs)") ||
        !contains(job_admission_policy, "metadata_json.value(\"queueOrder\")") ||
        !contains(job_runner, "Paused safely. Resume to render this timeline again.") ||
        !contains(job_queue_model_test, "priority then manual render order should survive a fresh model instance") ||
        !contains(job_queue_model_test, "duplicate should preserve delivery intent while clearing runtime results") ||
        !contains(job_queue_model_test, "canceled timeline renders should retry")) {
      return fail("persistent timeline deliveries need a durable ordered queue and progressively disclosed complete job controls");
    }
    if (!contains(
            timeline_view,
            "background-export:v1|editing:concurrent|policies:playback-first-balanced-export-first|"
            "resources:thread-limit-and-os-priority-only|quality:no-preview-or-output-reduction|"
            "surface:background-job-more-menu") ||
        !contains(
            export_view,
            "background-export:v1|editing:concurrent|policies:playback-first-balanced-export-first|"
            "resources:thread-limit-and-os-priority-only|preview-quality:unchanged|"
            "output-settings:unchanged|default:playback-first|ui:progressive-row-menu") ||
        !contains(export_view, "key: \"resources-playback-first\"") ||
        !contains(export_view, "key: \"resources-balanced\"") ||
        !contains(export_view, "key: \"resources-export-first\"") ||
        !contains(export_view, "jobQueue.setRenderResourcePolicy(") ||
        !contains(job_queue_model, "bool JobQueueModel::setRenderResourcePolicy(") ||
        !contains(job_runner, "resourcePolicyAffectsQuality") ||
        !contains(job_runner, "QThread::LowestPriority") ||
        !contains(timeline_document_controller, "setBackgroundRenderResourcePolicy(") ||
        !contains(timeline_document_controller, "background_render_resource_policy_ == QStringLiteral(\"balanced\")") ||
        !contains(worker_queue_registry, "backgroundNiceness") ||
        !contains(job_queue_model_test, "three explicit resource policies")) {
      return fail("background delivery needs an explicit persisted resource policy that changes scheduling only, never preview or output quality");
    }
    if (!contains(
            timeline_view,
            "timeline-media-integrity:v1|source:background-full-sha256|fast-id:size-head-tail|"
            "generated:atomic-sidecar-full-sha256|stages:ingest-relink-consolidation-cache-rebuild-delivery|"
            "mismatch:block-source-rebuild-derived|ui:project-health-details-only|"
            "proof:midfile-source-generated-and-delivery-tamper") ||
        !contains(timeline_document_controller_header, "QString source_content_sha256;") ||
        !contains(timeline_document_controller_header, "QFutureWatcher<QVariantMap>* source_integrity_watcher_") ||
        !contains(timeline_document_controller, "QStringLiteral(\"generated-media-integrity:v1\")") ||
        !contains(timeline_document_controller, "QStringLiteral(\"source-integrity-mismatch\")") ||
        !contains(timeline_document_controller, "void TimelineDocumentController::startNextSourceIntegrity()") ||
        !contains(timeline_document_controller, "remove_artifact_and_integrity(output_path)") ||
        !contains(job_runner, "QStringLiteral(\"delivery-integrity:v1\")") ||
        !contains(job_runner, "QStringLiteral(\"outputIntegrity\")") ||
        !contains(timeline_document_test, "middle-byte tamper must fail full SHA-256") ||
        !contains(timeline_document_test, "regenerating a tampered proxy should atomically rebuild media and its checksum")) {
      return fail("media integrity needs non-blocking full-source hashes, atomic generated sidecars, persisted delivery manifests, repair behavior, and tamper proof");
    }
    if (!contains(timeline_view,
                  "timeline-intrinsic-geometry:v1|allowed:time-to-pixel-lane-height-ruler-cadence-"
                  "playhead-trim-handles-waveform-safe-area-media-black|chrome-values:theme-only|"
                  "documented-exceptions:true")) {
      return fail("intrinsic timeline geometry exceptions are not documented");
    }
    if (!contains(
            timeline_view,
            "timeline-ui-ux-remediation:v1|hierarchy:program-and-timeline-dominant|"
            "splitters:persistent-safe|side-panels:bounded-collapsible|"
            "empty-state:contained-single-primary|viewer:aspect-fit-responsive|"
            "toolbar:create-search-tools-together|safe-areas:viewer-menu|"
            "tracks:quiet-targeting|inspector:no-selection-contextual|"
            "proof:focused-contract") ||
        !contains(timeline_view, "property real monitorPanelHeight: 540") ||
        !contains(timeline_view, "timelineWorkspaceSettings.shellLayoutVersion < 4") ||
        !contains(timeline_view, "timelineWorkspaceSettings.shellLayoutVersion = 4") ||
        !contains(timeline_view, "objectName: \"timelineProgramEmptyState\"") ||
        !contains(timeline_view, "readonly property bool compactGuidance:") ||
        !contains(timeline_view, "anchors.horizontalCenter: parent.horizontalCenter") ||
        !contains(timeline_view,
                  "parent.width - Math.min(theme.space16 * 2,") ||
        !contains(timeline_view, "? \"Add\" : \"Start your first cut\"") ||
        !contains(timeline_view, "? \"media\"") ||
        !contains(timeline_view, "? theme.space6 : theme.space16") ||
        !contains(timeline_view, "objectName: \"timelineProgramOverlayTimecode\"") ||
        !contains(timeline_view, "visible: parent.width >= theme.scaled(180)") ||
        !contains(timeline_view,
                  "height: Math.max(1, Math.min(theme.scaled(210), parent.height - theme.space24))") ||
        !contains(timeline_view, "key: \"safe-areas\"") ||
        contains(timeline_view, "text: \"Safe\"") ||
        contains(source_panel, "Load a clip to set timing and edit points.")) {
      return fail("the first hierarchy, contained empty-state, and viewer-menu remediation slice is missing");
    }
    if (!contains(timeline_view, "property bool timelineRenameActive: false") ||
        !contains(timeline_view, "objectName: \"timelineCompactTitleControl\"") ||
        !contains(timeline_view, "objectName: \"timelineInlineRenameField\"") ||
        !contains(timeline_view, "objectName: \"timelineProjectSummaryButton\"") ||
        !contains(timeline_view, "function timelineProjectSummaryText()") ||
        !contains(timeline_view, "objectName: \"timelineHistoryControlGroup\"") ||
        !contains(timeline_view, "objectName: \"timelineUndoButton\"") ||
        !contains(timeline_view, "objectName: \"timelineRedoButton\"") ||
        !contains(timeline_view, "objectName: \"timelinePrimaryAddMediaButton\"") ||
        !contains(timeline_view, "objectName: \"timelineSaveButton\"") ||
        !contains(timeline_view, "objectName: \"timelineSaveStateChip\"") ||
        !contains(timeline_view, "function timelineSaveStateText()") ||
        !contains(timeline_view, "objectName: \"timelineHeaderMoreMenuButton\"") ||
        !contains(timeline_view, "function timelineHeaderMenuItems()") ||
        contains(timeline_view, "label: \"Tracks\"") ||
        contains(timeline_view, "key: \"add-media\"")) {
      return fail("the compact project, history, primary-media, save-state, and overflow header is incomplete");
    }
    if (!contains(source_panel, "objectName: \"timelineSourceTab\"") ||
        !contains(source_panel, "objectName: \"timelineMediaTab\"") ||
        !contains(source_panel, "objectName: \"timelineImportMediaButton\"") ||
        !contains(source_panel, "visible: activeTab === \"media\"") ||
        !contains(source_panel, "objectName: \"timelineSourceEmptyBrowseButton\"") ||
        !contains(source_panel, "Choose media to preview and mark") ||
        !contains(source_panel, "objectName: \"timelineSourceMonitor\"") ||
        !contains(source_panel, "objectName: \"timelineSourceVideoOutput\"") ||
        !contains(source_panel, "fillMode: VideoOutput.PreserveAspectFit") ||
        !contains(source_panel, "objectName: \"timelineSourcePosterImage\"") ||
        !contains(source_panel, "fillMode: Image.PreserveAspectFit") ||
        !contains(source_panel, "function scrollEditorToTop()") ||
        !contains(source_panel, "function scrollEditorToBottom()") ||
        !contains(source_panel, "objectName: \"timelineSourceFrameTransport\"") ||
        !contains(source_panel, "objectName: \"timelineSourceStepBackButton\"") ||
        !contains(source_panel, "objectName: \"timelineSourcePlayButton\"") ||
        !contains(source_panel, "objectName: \"timelineSourceStepForwardButton\"") ||
        !contains(source_panel, "objectName: \"timelineSourceMarkControls\"") ||
        !contains(source_panel, "objectName: \"timelineSourceMarkInButton\"") ||
        !contains(source_panel, "objectName: \"timelineSourceMarkOutButton\"") ||
        !contains(source_panel, "objectName: \"timelineSourceScrubber\"") ||
        !contains(source_panel, "Layout.preferredHeight: theme.controlHeightTall") ||
        !contains(source_panel, "objectName: \"timelineSourceMarkedRange\"") ||
        !contains(source_panel, "objectName: \"timelineClearSourceInButton\"") ||
        !contains(source_panel, "root.documentController.clearSourceInPoint()") ||
        !contains(source_panel, "objectName: \"timelineClearSourceOutButton\"") ||
        !contains(source_panel, "root.documentController.clearSourceOutPoint()") ||
        !contains(source_panel, "objectName: \"timelineSourceInsertButton\"") ||
        !contains(source_panel, "objectName: \"timelineSourceEditMenu\"") ||
        !contains(media_asset_row, "objectName: \"timelineMediaAssetThumbnail\"") ||
        !contains(media_asset_row, "objectName: \"timelineMediaAssetDragAffordance\"") ||
        !contains(media_asset_row, "objectName: \"timelineMediaAssetStatus\"") ||
        !contains(media_asset_row, "text: root.online ? \"Ready\" : \"Offline\"") ||
        !contains(gallery_picker, "readonly property real selectedDurationSeconds:") ||
        !contains(catalog_model_cpp, "map.insert(QStringLiteral(\"durationSeconds\"), item.duration_seconds)") ||
        !contains(timeline_view, "function addPickedMedia()") ||
        !contains(timeline_view, "timelineDocument.insertClipFromGallery(") ||
        !contains(timeline_view, "function overwritePickedMedia()") ||
        !contains(timeline_view, "timelineDocument.overwriteClipFromGallery(") ||
        !contains(timeline_view, "root.sourceRailTab = \"source\"") ||
        !contains(timeline_view, "root.sourcePanelCollapsed = false")) {
      return fail("the Source and Media workflow must be calm, aspect-fit, markable, and insertion-ready");
    }
    const auto toolbar_position = timeline_view.find("objectName: \"timelineToolBar\"");
    const auto create_position = timeline_view.find("objectName: \"timelineCreateMenuButton\"");
    const auto search_position = timeline_view.find("objectName: \"timelineSearchButton\"");
    if (toolbar_position == std::string::npos || create_position <= toolbar_position ||
        search_position <= create_position) {
      return fail("Create and Search must stay beside the primary timeline tool control");
    }
    if (!contains(timeline_view,
                  "timeline-coordinate-alignment:v1|origin:header-boundary-ruler-playhead-lanes-clips|") ||
        !contains(timeline_view, "function timelineX(seconds)") ||
        !contains(timeline_view, "property real committedX: root.timelineX(clipBlock.displayedStartSeconds)") ||
        !contains(timeline_view, "function rulerLabelInterval()") ||
        !contains(timeline_view, "function isMajorRulerTick(seconds)") ||
        !contains(timeline_view, "objectName: \"timelinePlayheadHitTarget\"") ||
        !contains(timeline_view, "Accessible.role: Accessible.Slider") ||
        !contains(timeline_view, "objectName: \"timelineBladeToolButton\"") ||
        !contains(timeline_view, "objectName: \"timelineTrimToolButton\"") ||
        !contains(timeline_view, "function zoomToSelection()") ||
        !contains(timeline_view, "key: \"zoom-selection\"") ||
        !contains(timeline_view, "visible: root.width >= 1060")) {
      return fail("ruler, playhead, clips, immediate tools, and anchored zoom must share one navigation geometry contract");
    }
    if (!contains(timeline_view, "property real laneHeaderWidth: 144") ||
        !contains(timeline_view, "function setLaneHeaderWidth(width)") ||
        !contains(timeline_view, "onLaneHeaderWidthChanged: timelineWorkspaceSettings.laneHeaderWidth = laneHeaderWidth") ||
        !contains(timeline_view, "objectName: \"timelineTrackHeaderWidthHandle\"") ||
        !contains(timeline_view, "Accessible.name: \"Resize track headers\"") ||
        !contains(timeline_view, "Keys.onLeftPressed: root.setLaneHeaderWidth") ||
        !contains(timeline_view, "property string trackHeightsJson: \"{}\"") ||
        !contains(timeline_view, "function restoreTimelineViewportState()") ||
        !contains(timeline_view, "onTrackHeightsChanged: {") ||
        !contains(timeline_view, "timelineWorkspaceSettings.timelineZoom = timelineZoom") ||
        !contains(timeline_view, "timelineWorkspaceSettings.timelineScrollX = contentX") ||
        !contains(timeline_view, "timelineWorkspaceSettings.timelineScrollY = contentY") ||
        !contains(track_header, "signal heightPresetRequested(real height)") ||
        !contains(track_header, "text: \"Minimize Track\"") ||
        !contains(track_header, "text: \"Standard Track Height\"") ||
        !contains(track_header, "text: \"Large Track Height\"") ||
        !contains(track_header, "objectName: \"timelineMoveTrackEarlierAction\"") ||
        !contains(track_header, "documentController.moveTrackRow(trackData.trackIndex, -1)") ||
        !contains(track_header, "objectName: \"timelineTrackReorderHandle\"") ||
        !contains(track_header, "objectName: \"timelineTrackReorderPreview\"") ||
        !contains(track_header, "root.reorderPreviewDirection = -1") ||
        !contains(track_header, ": (trackData.muted ? \"audio-muted\" : \"audio\")") ||
        !contains(track_header, "iconName: \"source\"") ||
        !contains(track_header, "objectName: \"timelineDeleteTrackConfirmation\"") ||
        !contains(track_header, "You can undo the deletion.") ||
        !contains(track_header, "documentController.removeTrackRow(trackData.trackIndex, true)")) {
      return fail("track headers need persisted horizontal resize plus keyboard and preset lane sizing");
    }
    if (!contains(effects_panel, "timeline-effects-panel-style:v9|surface:borderless") ||
        !contains(effects_panel, "objectName: \"timelineInspectorNoSelectionState\"") ||
        !contains(effects_panel, "readonly property bool hasClip: hasSingleSelection") ||
        !contains(effects_panel, "text: \"Select a clip to inspect\"") ||
        !contains(effects_panel, "objectName: \"timelineInspectorMultiSelectionState\"") ||
        !contains(effects_panel, "Batch selection · only shared actions are shown") ||
        !contains(effects_panel, "root.documentController.createCompoundClipFromSelection(\"\")") ||
        !contains(effects_panel, "root.documentController.copySelectedClips()") ||
        !contains(effects_panel, "root.documentController.extractSelectedClips()") ||
        !contains(timeline_view, "function currentInspectorSelectionType()") ||
        !contains(timeline_view, "function inspectorTabValidForType(tab, selectionType)") ||
        !contains(timeline_view, "function restoreInspectorTabForSelection()") ||
        !contains(timeline_view, "property string inspectorTabsByTypeJson: \"{}\"") ||
        !contains(timeline_view, "root.restoreInspectorTabForSelection()") ||
        !contains(timeline_view, "timeline-detached-window-geometry:v1|source-inspector:bounded-persisted|") ||
        !contains(timeline_view, "function restoreDetachedWindowGeometry(windowItem, storedX, storedY,") ||
        !contains(timeline_view, "targetScreen.availableGeometry") ||
        !contains(timeline_view, "onXChanged: if (visible) timelineWorkspaceSettings.detachedSourceX = x") ||
        !contains(timeline_view, "onWidthChanged: if (visible) timelineWorkspaceSettings.detachedInspectorWidth = width") ||
        !contains(effects_panel, "text: \"APPLIED EFFECTS\"") ||
        !contains(effects_panel, "text: \"AVAILABLE EFFECTS\"") ||
        !contains(effects_panel, "objectName: \"timelineAppliedEffectsStack\"") ||
        !contains(effects_panel, "moveSelectedClipEffect(") ||
        !contains(effects_panel, "objectName: \"timelineEffectLibraryList\"") ||
        !contains(effects_panel, "objectName: \"timelineAudioPanControl\"") ||
        !contains(effects_panel, "objectName: \"timelineAudioNormalizationControls\"") ||
        !contains(effects_panel, "objectName: \"timelineCropModeControl\"") ||
        contains(effects_panel, "iconName: \"more\"") ||
        !contains(effects_panel, "signal sequenceSettingsRequested()") ||
        !contains(effects_panel, "signal projectSettingsRequested()") ||
        !contains(timeline_view, "property string inspectorScrollByContextJson: \"{}\"") ||
        !contains(timeline_view, "property string inspectorDisclosureByTypeJson: \"{}\"") ||
        !contains(timeline_view, "function setInspectorContentScrollPosition(position)") ||
        !contains(timeline_view, "function setInspectorEffectDisclosure(effectName, expanded)") ||
        !contains(effect_library_row, "readonly property string effectIcon:") ||
        !contains(effect_library_row, "readonly property bool effectAvailable: rendererAvailable && compatible") ||
        !contains(effect_library_row, "This effect is not compatible with the selected media type.") ||
        contains(effects_panel, "text: \"Preview + export effects\"") ||
        !contains(effects_panel, "signal focusTimelineRequested()") ||
        !contains(timeline_view, "onFocusTimelineRequested:") ||
        contains(timeline_view, "border.width: timelineViewport.activeFocus")) {
      return fail("the Inspector must replace unavailable controls with a contextual no-selection action");
    }
    if (!contains(media_asset_row, "readonly property string libraryType: \"timeline-media\"") ||
        !contains(media_asset_row, "Drag.keys: [\"timeline-media\"]") ||
        !contains(media_asset_row, "signal mediaDragDropped(var payload, real sceneX, real sceneY)") ||
        !contains(timeline_view, "objectName: \"timelineMediaLaneDropArea\"") ||
        !contains(timeline_view, "objectName: \"timelineMediaDropPreview\"") ||
        !contains(timeline_view, "function commitExternalMediaDrop(payload, sceneX, sceneY)") ||
        !contains(timeline_view, "timelineDocument.insertClipFromGalleryAtTrack(") ||
        !contains(timeline_view, "\"Incompatible track\"")) {
      return fail("media must drag from the Media panel to an exact compatible lane and insertion time");
    }
    if (!contains(timeline_view, "objectName: \"timelineClipSelectionOverlay\"") ||
        !contains(timeline_view, "border.width: theme.scaled(2)") ||
        !contains(timeline_view, "readonly property bool editHandlesVisible:") ||
        !contains(timeline_view, "objectName: \"timelineClipDragDestinationLane\"") ||
        !contains(timeline_view, "objectName: \"timelineClipDragAffectedRange\"") ||
        !contains(timeline_view, "objectName: \"timelineClipDragPreview\"") ||
        !contains(timeline_view, "objectName: \"timelineClipDragInsertionPoint\"") ||
        !contains(timeline_view, "timelineDocument.moveSelectedClipToTrackRowWithMode(") ||
        !contains(timeline_view, "root.clipMoveResultLabel(") ||
        !contains(timeline_view, "\"trackLabel\": String(") ||
        !contains(timeline_view, "root.timecodeText(") ||
        !contains(timeline_view, "objectName: \"timelineSnapGuide\"") ||
        !contains(timeline_view, "objectName: \"timelineSnapGuideLabel\"") ||
        !contains(timeline_view, "root.snapGuideState.label") ||
        !contains(timeline_view, "function clipStatusBadges(clip, trackSuppressed)") ||
        !contains(timeline_view, "\"key\": \"offline\"") ||
        !contains(timeline_view, "\"key\": \"effects\"") ||
        !contains(timeline_view, "\"key\": \"speed\"") ||
        !contains(timeline_view, "\"key\": \"proxy\"") ||
        !contains(timeline_view, "\"key\": \"caption\"") ||
        !contains(timeline_view, "\"key\": \"disabled\"") ||
        !contains(timeline_view, "\"key\": \"cache\"") ||
        !contains(clip_content, "objectName: \"timelineClipStatusStrip\"") ||
        !contains(clip_content, "objectName: \"timelineClipStatusBadge-\" + badgeKey") ||
        contains(clip_content, "border.width:") ||
        !contains(timeline_view, "objectName: \"timelineGroupSelectedClipsAction\"") ||
        !contains(timeline_view, "timelineDocument.groupSelectedClips()") ||
        !contains(timeline_view, "text: rippleModeEnabled ? \"Ripple Delete\" : \"Lift Clip\"") ||
        !contains(timeline_view, "objectName: \"timelineRevealClipInSourceAction\"") ||
        !contains(timeline_view, "timelineDocument.matchFrameToSource()") ||
        !contains(timeline_view, "objectName: \"timelineOpenClipEffectsAction\"") ||
        !contains(timeline_view, "objectName: \"timelineOpenClipSpeedAction\"") ||
        !contains(timeline_view, "objectName: \"timelineOpenClipPropertiesAction\"")) {
      return fail("clip selection, contextual handles, drag destination, and typed snap feedback must remain precise over media");
    }
    if (!contains(timeline_view,
                  "timeline-progressive-disclosure:v1|default:calm|"
                  "always:media-undo-save-playback-tool|"
                  "contextual:inspector-split-review-status|"
                  "menus:create-view-tools-zoom|panels:source-inspector-collapsible-detachable|"
                  "advanced:opt-in|commands:preserved") ||
        !contains(timeline_view, "objectName: \"timelineCreateMenuButton\"") ||
        !contains(timeline_view, "objectName: \"timelineViewMenuButton\"") ||
        !contains(timeline_view, "objectName: \"timelineZoomMenuButton\"") ||
        !contains(timeline_view, "objectName: \"timelineDetachedSourceWindow\"") ||
        !contains(timeline_view, "objectName: \"timelineDetachedInspectorWindow\"") ||
        !contains(timeline_view, "property bool effectsPanelCollapsed: true") ||
        !contains(timeline_view, "property bool timelineAdvancedControlsVisible: false") ||
        !contains(timeline_view, "function timelineCreateMenuItems()") ||
        !contains(timeline_view, "key: \"add-audio-track\"") ||
        !contains(timeline_view, "label: \"Independent Audio Lane…\"") ||
        !contains(timeline_view, "objectName: \"timelineAudioTrackRolePopup\"") ||
        !contains(timeline_view, "timelineDocument.addIndependentAudioTrack(") ||
        !contains(timeline_view, "{\"role\": \"dialogue\", \"label\": \"Dialogue\"}") ||
        !contains(timeline_view, "{\"role\": \"music\", \"label\": \"Music\"}") ||
        !contains(timeline_view, "{\"role\": \"voiceover\", \"label\": \"Voiceover\"}") ||
        !contains(timeline_view, "{\"role\": \"ambience\", \"label\": \"Ambience\"}") ||
        !contains(timeline_view, "{\"role\": \"effects\", \"label\": \"Sound Effects\"}") ||
        !contains(timeline_view, "{\"role\": \"commentary\", \"label\": \"Commentary\"}") ||
        !contains(timeline_view, "function timelineViewMenuItems()") ||
        !contains(timeline_view, "function timelineZoomMenuItems()")) {
      return fail("the calm default, contextual controls, and progressive command menus are missing");
    }
    if (!contains(timeline_view,
                  "timeline-search:v2|query:clip-name-marker-note-timecode-media-effect-transition-online-state|"
                  "filters:clips-markers-video-audio-photo-title-caption-offline-effects-marker-status|"
                  "scope:all-or-selected-track|results:controller-sorted|"
                  "navigation:select-clip-seek-marker-prev-next-wrap|"
                  "ui:contextual-single-row-shared-menu|performance:id-map") ||
        !contains(timeline_view, "objectName: \"timelineSearchButton\"") ||
        !contains(timeline_view, "objectName: \"timelineSearchField\"") ||
        !contains(timeline_view, "objectName: \"timelineSearchFilterMenuButton\"") ||
        !contains(timeline_view, "timelineDocument.timelineSearchResults(") ||
        !contains(timeline_view, "timelineDocument.activateTimelineSearchResult(") ||
        !contains(timeline_view, "function timelineSearchFilterMenuItems()") ||
        !contains(timeline_view, "key: \"kind-offline\"") ||
        !contains(timeline_view, "key: \"kind-effects\"") ||
        !contains(timeline_view, "key: \"scope-selected\"")) {
      return fail("large timelines need one contextual shared-style search surface with typed filters, selected-track scope, and exact result navigation");
    }
    if (!contains(timeline_view,
                  "timeline-beat-markers:v1|analysis:ffmpeg-pcm-onset-serial-cancellable|"
                  "sources:unique-online-audio|mapping:source-trim-rate-sequence|"
                  "review:suggestions-inert|commit:confirmed-one-undo|"
                  "markers:editable-persistent-snap-ready|dedupe:frame-aware|"
                  "ui:create-menu-modular-popup") ||
        !contains(timeline_view, "key: \"beat-markers\"") ||
        !contains(timeline_view, "objectName: \"timelineBeatAnalysisPopup\"") ||
        !contains(timeline_view, "objectName: \"timelineAnalyzeBeatsButton\"") ||
        !contains(timeline_view, "objectName: \"timelineBeatSuggestionList\"") ||
        !contains(timeline_view, "objectName: \"timelineCreateBeatMarkersButton\"") ||
        !contains(timeline_view, "timelineDocument.analyzeTimelineBeats()") ||
        !contains(timeline_view, "timelineDocument.cancelBeatAnalysis()") ||
        !contains(timeline_view, "timelineDocument.createBeatMarkersFromAnalysis()")) {
      return fail("beat detection needs a cancellable inert review flow that creates editable snap-ready markers only after confirmation");
    }
    if (!contains(timeline_view,
                  "timeline-title-templates:v1|builtins:basic-headline-lower-third-callout-quote-brand-card|"
                  "create:single-modular-picker|apply:inspector-preserve-text-one-undo|"
                  "customize:shared-title-controls|render:preview-export-shared|"
                  "variants:position-size-compatible|persistence:save-reopen|density:add-menu-contextual") ||
        !contains(timeline_view, "objectName: \"timelineTitleTemplatePopup\"") ||
        !contains(timeline_view, "objectName: \"timelineTitleTemplateGrid\"") ||
        !contains(timeline_view, "timelineDocument.titleTemplates") ||
        !contains(timeline_view, "timelineDocument.addTitleClipFromTemplate(") ||
        !contains(effects_panel, "objectName: \"timelineTitleTemplateMenuButton\"") ||
        !contains(effects_panel, "root.documentController.applySelectedTitleTemplate(key)")) {
      return fail("title production needs contextual reusable presets for creation and non-destructive inspector restyling");
    }
    if (!contains(effects_panel, "objectName: \"timelineTitleMotionMenuButton\"") ||
        !contains(effects_panel, "\"key\": \"title-fade\"") ||
        !contains(effects_panel, "\"key\": \"title-pop\"") ||
        !contains(effects_panel, "\"key\": \"title-rise\"") ||
        !contains(effects_panel, "\"key\": \"title-slide-from-left\"") ||
        !contains(effects_panel, "\"key\": \"title-slide-from-right\"") ||
        !contains(effects_panel, "applySelectedClipMotionPreset(key)")) {
      return fail("the title Inspector should expose editable fade, pop, rise, and slide motion presets");
    }
    if (!contains(timeline_view,
                  "timeline-delivery-preflight:v3|platforms:youtube-shorts-tiktok-instagram|"
                  "checks:offline-media-fonts-effects-caption-validation-loudness-clipping-gamut-flash-risk-frame-rate-aspect-ratio-free-disk|"
                  "flash-risk:edit-graph-screen-with-frame-analysis-disclosure|"
                  "loudness:postmix-lufs-truepeak-lra-cancellable|"
                  "normalize:static-gain-peak-limited-one-undo|severity:blocking-warning|"
                  "fix:recommended-output-variant|ui:publish-flow-modular-popup") ||
        !contains(timeline_view,
                  "timeline-publish-flow:v1|primary:one-clear-publish-action|"
                  "summary:format-size-duration-range-destination-always-visible|"
                  "advanced:progressive|preflight:blocking-before-render|"
                  "save:available-in-split-menu|render:existing-background-queue|ui:single-modular-popup") ||
        !contains(timeline_view, "key: \"delivery-check\"") ||
        !contains(timeline_view, "objectName: \"timelinePublishPopup\"") ||
        !contains(timeline_view, "objectName: \"timelinePublishAdvancedToggle\"") ||
        !contains(timeline_view, "objectName: \"timelinePublishConfirmButton\"") ||
        !contains(timeline_view, "timelineDocument.timelinePublishSummary()") ||
        !contains(timeline_view, "objectName: \"timelineDeliveryPreflightPopup\"") ||
        !contains(timeline_view, "objectName: \"timelineDeliveryPreflightIssueList\"") ||
        !contains(timeline_view, "timelineDocument.timelineDeliveryPreflight()") ||
        !contains(timeline_view, "objectName: \"timelineMeasureLoudnessButton\"") ||
        !contains(timeline_view, "timelineDocument.analyzeTimelineLoudness()") ||
        !contains(timeline_view, "timelineDocument.cancelTimelineLoudnessAnalysis()") ||
        !contains(timeline_view, "objectName: \"timelineNormalizeLoudnessButton\"") ||
        !contains(timeline_view, "timelineDocument.normalizeTimelineLoudness()") ||
        !contains(timeline_view, "objectName: \"timelineApplyDeliveryFormatButton\"")) {
      return fail("publishing needs one clear output summary plus a truthful complete preflight and exact format repair path");
    }
    if (!contains(timeline_view,
                  "timeline-compound:v3|child:editable-document|"
                  "nesting:arbitrary-depth-cycle-safe|navigation:breadcrumb-return|"
                  "guard:self-ancestor-and-32-level|undo:isolated-restored|"
                  "duration:source-updated-parent-edit-preserved|"
                  "render:dependency-queued-bounded|failure:retryable|"
                  "persistence:save-reopen|ui:contextual") ||
        !contains(timeline_view, "key: \"make-compound\"") ||
        !contains(timeline_view, "timelineDocument.createCompoundClipFromSelection()") ||
        !contains(timeline_view, "timelineDocument.compoundBreadcrumb") ||
        !contains(timeline_view, "timelineDocument.closeCompoundClip()") ||
        !contains(timeline_view, "timelineDocument.openSelectedCompoundClip()") ||
        !contains(timeline_view, "text: modelData.compoundStatus === \"failed\" ? \"CMP !\"") ||
        !contains(timeline_document_controller,
                  "This compound points back to the timeline currently being edited") ||
        !contains(timeline_document_controller,
                  "This compound points to an ancestor timeline") ||
        !contains(timeline_document_controller,
                  "safe 32-level navigation limit")) {
      return fail("compound creation, nested navigation, and low-density clip status are missing");
    }
    if (!contains(timeline_view,
                  "timeline-multicam:v2|groups:source-relationship-preserved|"
                  "sync:embedded-timecode-waveform-correlation-source-marker|"
                  "monitor:all-angles-controller-exact-muted-decode|"
                  "cuts:frame-snapped-switch-nudge-remove|program:active-angle-shared|"
                  "audio:follow-video-fixed-angle-mute|"
                  "export:shared-video-and-audio-segments|keyboard:angles-1-through-9|"
                  "transport:play-pause-step-timecode|persistence:undo-save-reopen|"
                  "performance:sources-released-on-close|"
                  "ui:single-modular-popup-default-closed") ||
        !contains(multicam_angle_monitor,
                  "timeline-multicam-angle-monitor:v1|"
                  "source:controller-exact-trim-retime-map|"
                  "sync:program-sequence-clock|decode:muted-preview-only|"
                  "state:active-audio-unavailable|interaction:cut-at-playhead|"
                  "release:source-cleared-when-closed") ||
        !contains(cmake, "qml/components/TimelineMulticamAngleMonitor.qml") ||
        !contains(timeline_view, "objectName: \"timelineMulticamPopup\"") ||
        !contains(timeline_view, "objectName: \"timelineMulticamSyncMethod\"") ||
        !contains(timeline_view, "objectName: \"timelineSetMulticamSyncMarkerButton\"") ||
        !contains(timeline_view, "objectName: \"timelineCreateMulticamButton\"") ||
        !contains(timeline_view, "objectName: \"timelineMulticamMonitorGrid\"") ||
        !contains(timeline_view, "objectName: \"timelineMulticamAngleMonitor_\"") ||
        !contains(timeline_view, "objectName: \"timelineMulticamAudioModePicker\"") ||
        !contains(timeline_view, "objectName: \"timelineMulticamPlayPauseButton\"") ||
        !contains(timeline_view, "objectName: \"timelineMulticamStepBackButton\"") ||
        !contains(timeline_view, "objectName: \"timelineMulticamStepForwardButton\"") ||
        !contains(timeline_view, "objectName: \"timelineMulticamProgramTimecode\"") ||
        !contains(timeline_view, "objectName: \"timelineMulticamCutList\"") ||
        !contains(timeline_view, "timelineDocument.createMulticamGroupFromSelection(") ||
        !contains(timeline_view, "timelineDocument.switchMulticamAngle(") ||
        !contains(timeline_view, "timelineDocument.setMulticamAudioMode(") ||
        !contains(timeline_view, "timelineDocument.nudgeMulticamCut(") ||
        !contains(timeline_view, "timelineDocument.removeMulticamCut(") ||
        !contains(timeline_view, "timelineDocument.removeMulticamGroup(") ||
        !contains(timeline_view, "modelData.multicamAudioActive !== false") ||
        !contains(timeline_document_controller, "multicamAnglePreviewState(") ||
        !contains(timeline_document_controller, "setMulticamAudioMode(") ||
        !contains(timeline_document_controller, "nudgeMulticamCut(") ||
        !contains(timeline_document_controller, "multicamAngleAudioActiveAt(") ||
        !contains(timeline_document_controller, "QStringLiteral(\"audioMode\")") ||
        !contains(timeline_document_controller, "QStringLiteral(\"audioAngleIndex\")")) {
      return fail("multicam needs synchronized live angles, explicit audio policies, transport, and frame-editable decisions in one modular monitor");
    }
    if (!contains(timeline_view,
                  "timeline-sequence-versions:v2|named:immutable-content-mutable-metadata|"
                  "metadata:notes-author-created-updated|"
                  "milestones:protected-delete-guard-explicit-unprotect|"
                  "duplicate:exact-snapshot-new-identity|"
                  "compare:added-removed-changed-clips-markers-format-finishing|"
                  "restore:atomic-undoable|recovery:pre-restore-and-autosave|"
                  "storage:timeline-scoped-atomic|ui:single-contextual-popup-progressive-details") ||
        !contains(timeline_view, "key: \"versions\"") ||
        !contains(timeline_view, "objectName: \"timelineSequenceVersionsPopup\"") ||
        !contains(timeline_view, "objectName: \"timelineSequenceVersionsList\"") ||
        !contains(timeline_view, "objectName: \"timelineSequenceVersionDetailsButton\"") ||
        !contains(timeline_view, "objectName: \"timelineSequenceVersionNotesField\"") ||
        !contains(timeline_view, "objectName: \"timelineSequenceVersionAuthorField\"") ||
        !contains(timeline_view, "objectName: \"timelineSequenceVersionMilestoneToggle\"") ||
        !contains(timeline_view, "objectName: \"timelineSequenceVersionComparisonPanel\"") ||
        !contains(timeline_view, "objectName: \"timelineCompareSequenceVersionButton\"") ||
        !contains(timeline_view, "timelineDocument.createSequenceVersionWithMetadata(") ||
        !contains(timeline_view, "timelineDocument.updateSequenceVersionMetadata(") ||
        !contains(timeline_view, "timelineDocument.duplicateSequenceVersion(") ||
        !contains(timeline_view, "timelineDocument.restoreSequenceVersion(String(modelData.versionId))") ||
        !contains(timeline_view, "timelineDocument.deleteSequenceVersion(String(modelData.versionId))") ||
        !contains(timeline_document_controller, "sequence_version_detailed_comparison(") ||
        !contains(timeline_document_controller, "Unprotect this milestone before removing it.") ||
        !contains(timeline_document_controller, "duplicatedFromVersionId")) {
      return fail("named sequence versions need progressive metadata, protected milestones, exact duplication, detailed comparison, and safe restore in one contextual surface");
    }
    if (!contains(timeline_view,
                  "timeline-edit-recovery:v1|journal:append-only-hash-chain-fsync|"
                  "scope:unsaved-and-saved|tail:latest-valid-entry|autosave:discoverable|"
                  "actions:open-restore-copy-discard|original:unchanged-until-explicit-save|"
                  "ui:startup-explained") ||
        !contains(timeline_view, "objectName: \"timelineRecoveryPopup\"") ||
        !contains(timeline_view, "objectName: \"timelineRecoveryCandidateList\"") ||
        !contains(timeline_view, "Open works from the recovery and changes nothing until Save.") ||
        !contains(timeline_view, "Restore Copy creates a separate project immediately.") ||
        !contains(timeline_view, "timelineDocument.openRecoveryCandidate(candidateId)") ||
        !contains(timeline_view, "function onRecoveryOpenCompleted(candidateId, succeeded)") ||
        !contains(timeline_view, "timelineDocument.recoveryOpenInProgress") ||
        !contains(timeline_view, "timelineDocument.cancelRecoveryOpen()") ||
        !contains(timeline_view, "timelineDocument.restoreRecoveryCandidateAsCopy(") ||
        !contains(timeline_view, "timelineDocument.discardRecoveryCandidate(") ||
        !contains(timeline_view, "Qt.callLater(presentTimelineRecoveryIfNeeded)")) {
      return fail("crash recovery needs a calm startup chooser with explicit non-destructive action semantics");
    }
    if (!contains(timeline_view,
                  "timeline-project-health:v1|checks:media-fonts-effects-proxies-caches-"
                  "nesting-disk-delivery|severity:blocking-warning|scope:item-and-path|"
                  "scan:deterministic-read-only|ui:single-contextual-popup") ||
        !contains(timeline_view, "key: \"project-health\"") ||
        !contains(timeline_view, "objectName: \"timelineProjectHealthPopup\"") ||
        !contains(timeline_view, "objectName: \"timelineProjectHealthIssueList\"") ||
        !contains(timeline_view, "root.projectHealthData = timelineDocument.timelineProjectHealth()") ||
        !contains(timeline_view, "source media, fonts, effects, proxies, generated caches, compound nesting")) {
      return fail("project trust needs a readable contextual health scan across every release-blocking dependency class");
    }
    if (!contains(timeline_view,
                  "timeline-project-cache-manager:v1|categories:thumbnail-waveform-proxy-analysis-preview-render-cache|"
                  "inventory:current-project-current-and-invalidated-files-and-bytes|"
                  "cleanup:managed-roots-only-source-and-delivery-protected|"
                  "sharing:other-project-references-retained|busy:active-generators-skipped|") ||
        !contains(timeline_view, "key: \"project-cache\"") ||
        !contains(timeline_view, "objectName: \"timelineProjectCachePopup\"") ||
        !contains(timeline_view, "objectName: \"timelineProjectCacheCategoryList\"") ||
        !contains(timeline_view, "timelineDocument.projectCacheSummary()") ||
        !contains(timeline_view, "timelineDocument.cleanupProjectCaches(") ||
        !contains(timeline_view, "Confirm Clean") ||
        !contains(timeline_view, "Source media, saved timelines, exports, shared cache files, and active job outputs are protected.") ||
        !contains(timeline_document_controller,
                  "QVariantMap TimelineDocumentController::projectCacheSummary() const") ||
        !contains(timeline_document_controller,
                  "QVariantMap TimelineDocumentController::cleanupProjectCaches(") ||
        !contains(timeline_document_controller, "otherProjectCacheReferences() const") ||
        !contains(timeline_document_controller, "cache_path_matches_category(path, category)")) {
      return fail("project cache management needs exact six-category usage, shared and active-work protection, managed-root cleanup, and a confirmed modular UI");
    }
    if (!contains(timeline_view,
                  "timeline-cache-budgets:v1|resources:disk-cpu-decoded-gpu-program-estimated-working-memory|"
                  "defaults:installed-ram-and-volume|overrides:project-persisted-undoable|"
                  "pressure:five-second-normal-warning-critical|"
                  "response:effective-budgets-lru-trim-background-defer-safe-disk-clean|"
                  "ui:existing-project-cache-progressive") ||
        !contains(timeline_view, "objectName: \"timelineProjectCacheBudgetsDisclosure\"") ||
        !contains(timeline_view, "objectName: \"timelineProjectCacheBudgetPanel\"") ||
        !contains(timeline_view, "objectName: \"timelineCachePressureStatus\"") ||
        !contains(timeline_view, "objectName: \"timelineDiskCacheBudgetSlider\"") ||
        !contains(timeline_view, "objectName: \"timelineCpuCacheBudgetSlider\"") ||
        !contains(timeline_view, "objectName: \"timelineGpuCacheBudgetSlider\"") ||
        !contains(timeline_view, "objectName: \"timelineMemoryCacheBudgetSlider\"") ||
        !contains(timeline_view, "objectName: \"timelineApplyCacheBudgetsButton\"") ||
        !contains(timeline_view, "timelineDocument.cacheBudgetState()") ||
        !contains(timeline_view, "timelineDocument.setProjectCacheBudgets({") ||
        !contains(timeline_view, "*GPU usage is an estimate of active Program surfaces.") ||
        !contains(timeline_document_controller,
                  "QVariantMap TimelineDocumentController::cacheBudgetState() const") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::setProjectCacheBudgets(") ||
        !contains(timeline_document_controller, "cacheBudgets") ||
        !contains(timeline_document_controller, "cache_pressure_timer_.setInterval(5000)") ||
        !contains(timeline_document_controller, "set_decoded_image_cache_budget_bytes(") ||
        !contains(timeline_document_controller, "clear_decoded_image_cache()") ||
        !contains(timeline_document_controller, "waveform_peak_fraction_cache_.clear()") ||
        !contains(timeline_document_controller_header, "setCachePressureLevelForTesting") ||
        !contains(decoded_image_cache, "cache.budget_bytes = qBound(") ||
        !contains(decoded_image_cache, "cache.trim();") ||
        !contains(timeline_document_test,
                  "critical pressure should reduce effective limits, clear decoded frames, and defer background work") ||
        !contains(timeline_document_test,
                  "all four project cache budget overrides should survive save and reopen exactly")) {
      return fail("cache budgets need automatic host defaults, honest four-resource controls, persisted project overrides, dynamic LRU limits, and deterministic pressure response");
    }
    if (!contains(timeline_view,
                  "timeline-background-job-center:v1|sources:persistent-queue-plus-timeline-local|"
                  "fields:stage-progress-priority-status-failure|controls:pause-resume-cancel-retry-priority|") ||
        !contains(timeline_view, "key: \"background-jobs\"") ||
        !contains(timeline_view, "objectName: \"timelineBackgroundJobsPopup\"") ||
        !contains(timeline_view, "objectName: \"timelineBackgroundJobList\"") ||
        !contains(timeline_view, "objectName: \"timelineBackgroundJobsHistoryToggle\"") ||
        !contains(timeline_view, "jobQueue.jobsForCenter()") ||
        !contains(timeline_view, "timelineDocument.timelineBackgroundJobs()") ||
        !contains(timeline_view, "timelineDocument.controlTimelineBackgroundJob(id, action)") ||
        !contains(timeline_view, "jobQueue.setJobPriority(String(job.jobId || \"\"), next)") ||
        !contains(timeline_view, "timelineDocument.activateTimelineBackgroundJob(String(job.jobId || \"\"))") ||
        !contains(timeline_view, "appShell.revealPath(String(job.affectedPath || affectedId))") ||
        !contains(timeline_view, "visible: String(modelData.errorMessage || \"\").length > 0") ||
        !contains(timeline_view, "modelData.progressKnown === true") ||
        !contains(timeline_document_controller,
                  "QVariantList TimelineDocumentController::timelineBackgroundJobs() const") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::controlTimelineBackgroundJob(") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::activateTimelineBackgroundJob(") ||
        !contains(job_queue_model, "QVariantList JobQueueModel::jobsForCenter(") ||
        !contains(job_queue_model, "bool JobQueueModel::setJobPriority(") ||
        !contains(job_queue_model, "QStringLiteral(\"canChangePriority\")") ||
        !contains(job_queue_model, "QStringLiteral(\"affectedType\")") ||
        !contains(job_runner, "bool queued_job_less_than(") ||
        !contains(job_runner, "job.metadata_json.value(QStringLiteral(\"priority\"))") ||
        !contains(job_runner, "std::sort(queued_jobs.begin(), queued_jobs.end(), queued_job_less_than)")) {
      return fail("background work needs one persistent and Timeline-local job center with truthful stage, progress, failures, controls, priority ordering, recovery history, and affected-item routing");
    }
    if (!contains(timeline_view,
                  "timeline-background-priority:v1|intent:workspace-visibility-playback-direct-manipulation-focus-visible-clips|") ||
        !contains(timeline_view, "function visibleBackgroundWorkClipIds()") ||
        !contains(timeline_view, "function publishBackgroundWorkIntent()") ||
        !contains(timeline_view, "timelineDocument.setBackgroundWorkIntent(") ||
        !contains(timeline_view, "id: backgroundWorkIntentTimer") ||
        !contains(timeline_view, "root.scheduleBackgroundWorkIntentPublish()") ||
        !contains(timeline_view, "visible: String(modelData.priorityReason || \"\").length > 0") ||
        !contains(timeline_document_controller,
                  "void TimelineDocumentController::setBackgroundWorkIntent(") ||
        !contains(timeline_document_controller,
                  "void TimelineDocumentController::reprioritizeBackgroundWorkQueues()") ||
        !contains(timeline_document_controller,
                  "void TimelineDocumentController::resumeDeferredBackgroundWork()") ||
        !contains(timeline_document_controller,
                  "QStringLiteral(\"media-analysis\"),") ||
        !contains(timeline_document_controller,
                  "shouldDeferBackgroundWork(QStringLiteral(\"render-cache\"))") ||
        !contains(timeline_document_controller,
                  "QStringLiteral(\"transcription\"),") ||
        !contains(timeline_document_controller,
                  "QStringLiteral(\"waveform\"),") ||
        !contains(timeline_document_controller,
                  "QStringLiteral(\"proxy\"), proxy_generation_queue_.first()") ||
        !contains(timeline_document_controller,
                  "QStringLiteral(\"compound-render\"), clip_id") ||
        !contains(timeline_document_controller,
                  "QStringLiteral(\"-threads\"), QStringLiteral(\"1\")") ||
        !contains(worker_queue_registry, "void WorkerQueueRegistry::publishUserIntent(") ||
        !contains(worker_queue_registry, "QVariantMap WorkerQueueRegistry::priorityDecision(") ||
        !contains(worker_queue_registry, "void WorkerQueueRegistry::configureBackgroundProcess(") ||
        !contains(worker_queue_registry, "backgroundPriorityApplied") ||
        !contains(worker_queue_registry, "is_deferrable_background_workflow") ||
        !contains(worker_queue_registry, "Direct playback or visible interaction") ||
        !contains(worker_queue_registry, "Waiting for playback") ||
        !contains(job_runner, "WorkerQueueRegistry::priorityDecision(") ||
        !contains(job_runner, "wake_timer_.start(120)")) {
      return fail("background scheduling must derive one shared policy from visible editing intent, defer all low-value Timeline and export-preview starts, order focused and visible work first, bound current workers, resume automatically, and explain the decision only in the job center");
    }
    if (!contains(timeline_view,
                  "timeline-stale-results:v1|identity:source-revision-and-document-revision|") ||
        !contains(timeline_document_controller_header,
                  "Q_PROPERTY(QVariantMap staleResultState READ staleResultState NOTIFY staleResultStateChanged)") ||
        !contains(timeline_document_controller_header, "struct TranscriptionRequest final") ||
        !contains(timeline_document_controller,
                  "QString TimelineDocumentController::transcriptionRevisionIdentity() const") ||
        !contains(timeline_document_controller_header, "active_proxy_revision_key_") ||
        !contains(timeline_document_controller_header, "render_cache_revision_identity_") ||
        !contains(timeline_document_controller,
                  "void TimelineDocumentController::recordStaleResultDiscard(") ||
        !contains(timeline_document_controller,
                  "QStringLiteral(\"thumbnail\"), request.revision_key") ||
        !contains(timeline_document_controller,
                  "QStringLiteral(\"waveform\"), request.request_key") ||
        !contains(timeline_document_controller,
                  "QStringLiteral(\"proxy\"), requested_revision") ||
        !contains(timeline_document_controller,
                  "QStringLiteral(\"transcription\"),") ||
        !contains(timeline_document_controller,
                  "QStringLiteral(\"render-cache\"), render_cache_revision_identity_") ||
        !contains(editor_preview_controller,
                  "!preview_session_.isCurrent(result.async_job.generation)") ||
        !contains(editor_preview_controller,
                  "if (preview_session_.isCurrent(result.generation))") ||
        !contains(timeline_document_test,
                  "proxy stale-result proof should advance the source revision while encoding") ||
        !contains(timeline_document_test,
                  "stale transcript results must not replace captions") ||
        !contains(timeline_document_test,
                  "a preview-cache render from an older sequence revision must be discarded")) {
      return fail("every asynchronous thumbnail, waveform, proxy, effect, scope, transcript, and render-cache result needs an explicit revision identity and deterministic stale-publication proof");
    }
    if (!contains(timeline_view,
                  "timeline-incremental-load:v1|publish:editorial-graph-first|"
                  "priority:first-playable-source|hydrate:deduplicated-32-per-12ms|"
                  "work:analysis-waveforms-posters|cancel:timeline-generation|"
                  "ui:contextual-progress|proof:10000-clips-100-tracks") ||
        !contains(timeline_view, "objectName: \"timelineIncrementalLoadChip\"") ||
        !contains(timeline_view, "visible: timelineDocument.incrementalLoadBusy") ||
        !contains(timeline_view, "timelineDocument.incrementalLoadPendingCount") ||
        !contains(timeline_view, "Editing and playback are already available.")) {
      return fail("large-project loading needs truthful contextual progress after the edit graph becomes usable");
    }
    if (!contains(timeline_view,
                  "timeline-playback-prefetch:v1|protected:visible-player-separate|"
                  "lookaround:playhead-cuts|direction:forward-reverse|loop:both-boundaries|"
                  "intent:likely-next-edit|cache:ready-ranges|debounce:18ms|plan:bounded-6") ||
        !contains(timeline_view, "timelinePlayback.setLikelyNextEditSeconds(clamped)") ||
        !contains(timeline_view, "timelinePlayback.setLikelyNextEditSeconds(requested)")) {
      return fail("timeline edit intent should feed the bounded direction-aware playback prefetch scheduler");
    }
    if (!contains(timeline_view,
                  "timeline-determinism:v1|state:canonical-json-sha256|"
                  "volatile:updated-at-excluded|save-reopen:equal|"
                  "render:file-sha256-plus-decoded-video-audio-framemd5|"
                  "combined:sha256|proof:two-independent-renders") ||
        !contains(timeline_view,
                  "text: \"STATE  \" + String(root.projectHealthData.documentHash || \"\").slice(0, 12)")) {
      return fail("release proof needs a visible canonical project hash and deterministic decoded render fingerprint");
    }
    if (!contains(timeline_view,
                  "timeline-runtime-recovery:v1|decoder:reload-source-seek|"
                  "encoder:hardware-to-software|gpu:rebuild-program-media|"
                  "audio-device:recreate-default-sink|external-display:rehome-detached-windows|"
                  "state:canonical-hash-preserved|busy:bounded-cleared|ui:contextual-status") ||
        !contains(timeline_view, "objectName: \"timelineRuntimeRecoveryChip\"") ||
        !contains(timeline_view, "function onRuntimeRecoveryRequested(kind)") ||
        !contains(timeline_view, "onScreenChanged: if (visible) Qt.callLater(root.restoreDetached") ||
        !contains(desktop_main, "&QQuickWindow::sceneGraphError") ||
        !contains(desktop_main, "&QGuiApplication::screenRemoved") ||
        !contains(playback_controller, "recoverRuntimeFailure(QStringLiteral(\"decoder\"), detail)") ||
        !contains(audio_output, "&QMediaDevices::audioOutputsChanged") ||
        !contains(audio_output, "emit outputDeviceRecoveryRequested(detail)")) {
      return fail("runtime device loss needs integrated decoder, GPU, audio, display, and contextual recovery paths");
    }
    if (!contains(timeline_view,
                  "timeline-batch-relink:v1|scan:recursive-bounded-50000|"
                  "identity:sha256-head-tail-size|compare:name-extension-size-duration-"
                  "dimensions-signature|preview:no-mutation|approval:explicit-per-source|"
                  "ambiguous:never-recommended|mismatch:rejected|apply:all-uses-one-undo|"
                  "persistence:save-reopen|ui:single-modular-popup") ||
        !contains(source_panel, "objectName: \"timelineBatchRelinkButton\"") ||
        !contains(timeline_view, "objectName: \"timelineBatchRelinkPopup\"") ||
        !contains(timeline_view, "objectName: \"timelineApplyBatchRelinkButton\"") ||
        !contains(timeline_view, "Nothing changes until you approve and apply.") ||
        !contains(timeline_document_controller, "media_identity_signature") ||
        !contains(timeline_document_controller, "int TimelineDocumentController::applyBatchRelink(")) {
      return fail("offline media needs a preview-first, identity-verified, explicit batch relink workflow");
    }
    if (!contains(timeline_view,
                  "timeline-media-bins:v1|hierarchy:nested-cycle-safe|"
                  "names:sibling-unique|filter:descendant-aware|"
                  "organize:drag-media-and-bins|delete:contents-to-parent|"
                  "state:undo-redo-save-reopen|ui:calm-filter-plus-modular-manager") ||
        !contains(source_panel, "objectName: \"timelineActiveMediaBinFilter\"") ||
        !contains(source_panel, "objectName: \"timelineMediaPoolBrowseMenuButton\"") ||
        !contains(source_panel, "objectName: \"timelineMediaBinsPopup\"") ||
        !contains(source_panel, "keys: [\"timeline-media\", \"timeline-bin\"]") ||
        !contains(source_panel, "root.documentController.assignMediaAssetToBin(") ||
        !contains(source_panel, "root.documentController.moveMediaBin(") ||
        !contains(media_asset_row, "property bool binVisible: true") ||
        !contains(media_asset_row, "matchesSearch && binVisible") ||
        !contains(timeline_document_controller,
                  "QString TimelineDocumentController::createMediaBin(") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::mediaAssetVisibleInBin(")) {
      return fail("media organization needs calm descendant filtering plus a nested drag-capable bin manager");
    }
    if (!contains(timeline_view,
                  "timeline-media-review-metadata:v1|ratings:0-through-5|"
                  "marks:favorite-rejected-clear|labels:six-colors|tags:user-normalized|"
                  "selection:contextual-batch|keyboard:0-5-f-x-t|"
                  "state:undo-redo-save-reopen|search:metadata-ready|ui:calm-until-selected") ||
        !contains(source_panel, "objectName: \"timelineMediaMetadataBatchBar\"") ||
        !contains(source_panel, "objectName: \"timelineMediaBatchRatingButton\"") ||
        !contains(source_panel, "objectName: \"timelineMediaBatchMarkButton\"") ||
        !contains(source_panel, "objectName: \"timelineMediaBatchColorButton\"") ||
        !contains(source_panel, "objectName: \"timelineMediaTagsPopup\"") ||
        !contains(source_panel, "objectName: \"timelineMediaApplyTagsButton\"") ||
        !contains(media_asset_row, "timeline-media-asset-row:v5|") ||
        !contains(media_asset_row, "event.key >= Qt.Key_0 && event.key <= Qt.Key_5") ||
        !contains(media_asset_row, "event.key === Qt.Key_F") ||
        !contains(media_asset_row, "event.key === Qt.Key_X") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::setMediaAssetRating(") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::setMediaAssetTags(") ||
        !contains(timeline_document_controller, "mediaAssetMetadata")) {
      return fail("media review metadata needs contextual batch commands, fast keys, undo, and persisted controller state");
    }
    if (!contains(source_panel,
                  "timeline-unified-media-search:v2|one-field:filename-metadata-tags-"
                  "transcript-captions-markers-on-screen-text|fields:search-menu-composable|"
                  "filters:bin-plus-smart-collection|"
                  "quoted-phrases:true|results:matched-scope|default-density:query-contextual") ||
        !contains(source_panel, "objectName: \"timelineUnifiedMediaSearchField\"") ||
        !contains(source_panel, "objectName: \"timelineMediaSearchScopeMenuButton\"") ||
        !contains(source_panel, "{ \"key\": \"transcript\", \"label\": \"Transcript\", \"icon\": \"audio\"") ||
        !contains(source_panel, "{ \"key\": \"text\", \"label\": \"On-screen text\", \"icon\": \"text\"") ||
        !contains(media_asset_row, "documentController.searchMediaAsset(") ||
        !contains(media_asset_row, "objectName: \"timelineMediaSearchMatchScope\"") ||
        !contains(timeline_document_controller,
                  "QVariantMap TimelineDocumentController::searchMediaAsset(") ||
        !contains(timeline_document_controller, "detected_on_screen_text") ||
        !contains(timeline_document_controller, "marker.note") ||
        !contains(timeline_document_controller, "clip.caption_words")) {
      return fail("media search needs one contextual field with composable scope chips and controller-indexed filename, metadata, tags, transcript, caption, marker, and detected-text matches");
    }
    if (!contains(source_panel,
                  "timeline-media-hover-scrub:v1|pointer:debounced-80ms|"
                  "density:compact-5-standard-9-detailed-15|decode:ffmpeg-384x216-single-frame|"
                  "queue:latest-per-asset-bounded-24|cache:bounded-240-files-96mb|"
                  "full-resolution-per-move:false|ui:hover-contextual") ||
        !contains(source_panel, "objectName: \"timelineMediaPoolBrowseMenuButton\"") ||
        !contains(source_panel, "mediaFilmstripDensity === \"compact\" ? 5") ||
        !contains(media_asset_row, "interval: 80") ||
        !contains(media_asset_row, "documentController.requestMediaHoverFrame(") ||
        !contains(media_asset_row, "root.durationSeconds") ||
        !contains(timeline_document_controller,
                  "QString TimelineDocumentController::requestMediaHoverFrame(") ||
        !contains(timeline_document_controller, "media_hover_frame_queue_.size() >= 24") ||
        !contains(timeline_document_controller,
                  "scale=384:216:force_original_aspect_ratio=decrease") ||
        !contains(catalog_model_cpp, "{DurationSecondsRole, \"durationSeconds\"}")) {
      return fail("media rows need configurable, debounced, low-resolution cached hover scrubbing with a bounded latest-request queue");
    }
    if (!contains(source_panel,
                  "timeline-take-comparison:v1|selection:2-to-8|view:contact-sheet|"
                  "playback:synchronized-normalized-drift-correction|audio:leader-only|"
                  "rating:controller-owned|choice:load-source|ui:modular-contextual") ||
        !contains(source_panel, "objectName: \"timelineCompareTakesButton\"") ||
        !contains(source_panel, "objectName: \"timelineTakeComparisonPopup\"") ||
        !contains(source_panel, "objectName: \"timelineTakeComparisonPlayButton\"") ||
        !contains(source_panel, "id: takeComparisonSyncTimer") ||
        !contains(source_panel, "Math.abs(follower.normalizedPosition - position) > 0.025") ||
        !contains(source_panel, "muted: index !== 0") ||
        !contains(source_panel, "root.documentController.setMediaAssetRating(") ||
        !contains(source_panel, "objectName: \"timelineChooseTakeButton\"") ||
        !contains(source_panel, "root.loadIntoSource(")) {
      return fail("selected media needs a contextual contact-sheet take comparison with synchronized playback, one audio leader, rating, and an explicit Load Source route");
    }
    if (!contains(source_panel,
                  "timeline-media-usage-ui:v1|categories:used-unused-duplicate-offline-"
                  "derivative|reveal:timeline-or-catalog|remove:unused-confirmed|"
                  "consolidate:explicit-canonical-verified-group-confirmed|"
                  "state:controller-undo-redo-save-reopen|ui:view-menu-modular-filtered") ||
        !contains(source_panel, "{ \"key\": \"usage\", \"label\": \"Analyze Media Usage\", \"icon\": \"info\"") ||
        !contains(source_panel, "objectName: \"timelineMediaUsagePopup\"") ||
        !contains(source_panel, "objectName: \"timelineMediaUsageFilters\"") ||
        !contains(source_panel, "objectName: \"timelineRevealMediaUsageButton\"") ||
        !contains(source_panel, "objectName: \"timelineRemoveUnusedMediaButton\"") ||
        !contains(source_panel, "objectName: \"timelineChooseCanonicalMediaButton\"") ||
        !contains(source_panel, "objectName: \"timelineConsolidateMediaButton\"") ||
        !contains(timeline_document_controller,
                  "QVariantMap TimelineDocumentController::analyzeMediaUsage(") ||
        !contains(timeline_document_controller,
                  "int TimelineDocumentController::removeUnusedMediaFromProject(") ||
        !contains(timeline_document_controller,
                  "int TimelineDocumentController::consolidateDuplicateMedia(") ||
        !contains(timeline_document_controller, "removedMediaAssetIds")) {
      return fail("media usage needs read-only classification plus explicit reveal, confirmed unused removal, verified canonical consolidation, and persistent undoable project state");
    }
    if (!contains(source_panel,
                  "timeline-source-tape:v1|scope:active-bin-and-search|"
                  "reel:continuous-up-to-1000-online-media|identity:per-asset|"
                  "marks:per-asset-preserved|navigation:previous-next-auto-advance|"
                  "source:existing-monitor-and-edit-actions|exit:current-source-retained|"
                  "sequence-mutation:none") ||
        !contains(source_panel,
                  "{ \"key\": \"source-tape\", \"label\": \"Review as Source Tape\", \"icon\": \"play\"") ||
        !contains(source_panel, "objectName: \"timelineSourceTapeBar\"") ||
        !contains(source_panel, "objectName: \"timelineSourceTapePreviousButton\"") ||
        !contains(source_panel, "objectName: \"timelineSourceTapeNextButton\"") ||
        !contains(source_panel, "objectName: \"timelineSourceTapeExitButton\"") ||
        !contains(source_panel, "mediaStatus === MediaPlayer.EndOfMedia") ||
        !contains(source_panel, "documentController.searchMediaAsset(") ||
        !contains(timeline_document_controller,
                  "int TimelineDocumentController::configureSourceTape(") ||
        !contains(timeline_document_controller,
                  "void TimelineDocumentController::saveCurrentSourceTapeMarks()") ||
        !contains(timeline_document_controller, "items.size() >= 1000")) {
      return fail("Source Tape needs a filtered identity-preserving reel with per-asset marks, continuous navigation, existing Source actions, and no sequence mutation");
    }
    if (!contains(source_panel,
                  "timeline-media-metadata-inspector:v1|"
                  "technical:codec-resolution-frame-rate-color-space-audio-channels-camera-date|"
                  "editable:notes-custom-fields|probe:explicit-contextual|"
                  "state:undo-redo-save-reopen|ui:single-selection-modular") ||
        !contains(source_panel, "objectName: \"timelineMediaMetadataInfoButton\"") ||
        !contains(source_panel, "visible: root.selectedMediaAssetIds.length === 1") ||
        !contains(source_panel, "objectName: \"timelineMediaMetadataInspectorPopup\"") ||
        !contains(source_panel, "objectName: \"timelineMediaTechnicalMetadataCard\"") ||
        !contains(source_panel, "objectName: \"timelineMediaNotesField\"") ||
        !contains(source_panel, "objectName: \"timelineMediaCustomFieldsField\"") ||
        !contains(source_panel, "objectName: \"timelineMediaMetadataSaveButton\"") ||
        !contains(source_panel,
                  "documentController.inspectMediaAssetTechnicalMetadata(asset)") ||
        !contains(source_panel,
                  "root.documentController.setMediaAssetNotesAndCustomFields(") ||
        !contains(timeline_document_controller,
                  "QVariantMap TimelineDocumentController::inspectMediaAssetTechnicalMetadata(") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::setMediaAssetNotesAndCustomFields(")) {
      return fail("single-selected media needs a contextual modular inspector for real technical metadata plus searchable, undoable, persistent notes and custom fields");
    }
    if (!contains(source_panel,
                  "timeline-smart-collections:v1|rules:media-type-rating-label-tag-date-"
                  "duration-orientation-offline-usage-custom-field|logic:all-rules-live|"
                  "state:create-edit-delete-undo-redo-save-reopen|"
                  "filter:bin-search-composable|ui:direct-filter-plus-browse-manager") ||
        !contains(source_panel,
                  "{ \"key\": \"smart-collections\", \"label\": \"Manage Smart Collections…\", \"icon\": \"filter\"") ||
        !contains(source_panel, "objectName: \"timelineSmartCollectionsPopup\"") ||
        !contains(source_panel, "objectName: \"timelineActiveMediaBinFilter\"") ||
        !contains(source_panel, "objectName: \"timelineSmartCollectionMediaTypeRule\"") ||
        !contains(source_panel, "objectName: \"timelineSmartCollectionRatingRule\"") ||
        !contains(source_panel, "objectName: \"timelineSmartCollectionLabelRule\"") ||
        !contains(source_panel, "objectName: \"timelineSmartCollectionTagRule\"") ||
        !contains(source_panel, "objectName: \"timelineSmartCollectionDateFromRule\"") ||
        !contains(source_panel, "objectName: \"timelineSmartCollectionMinimumDurationRule\"") ||
        !contains(source_panel, "objectName: \"timelineSmartCollectionOrientationRule\"") ||
        !contains(source_panel, "objectName: \"timelineSmartCollectionOnlineRule\"") ||
        !contains(source_panel, "objectName: \"timelineSmartCollectionUsageRule\"") ||
        !contains(source_panel, "objectName: \"timelineSmartCollectionCustomFieldRule\"") ||
        !contains(source_panel, "objectName: \"timelineSaveSmartCollectionButton\"") ||
        !contains(source_panel, "documentController.mediaAssetMatchesSmartCollection(") ||
        !contains(media_asset_row, "&& smartCollectionVisible") ||
        !contains(timeline_document_controller,
                  "QString TimelineDocumentController::createSmartCollection(") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::updateSmartCollection(") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::deleteSmartCollection(") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::mediaAssetMatchesSmartCollection(")) {
      return fail("smart collections need a modular saved-rule builder, composable live filtering, full required rule coverage, and controller-owned undoable persistence");
    }
    if (!contains(timeline_view,
                  "timeline-playback-diagnostics:v1|detect:sequence-clock-versus-decoded-position|"
                  "scope:clip-track-source-timecode|causes:decoder-effect-cache-storage|"
                  "evidence:quality-buffer-backend-detail|actions:relink-bypass-cache-proxy-transcode|"
                  "reset:explicit|ui:contextual-details-popup") ||
        !contains(timeline_view, "objectName: \"timelinePlaybackDiagnosticsPopup\"") ||
        !contains(timeline_view, "objectName: \"timelineResetPlaybackDiagnosticsButton\"") ||
        !contains(timeline_view, "actionText: timelinePlayback.droppedFrameCount > 0 ? \"Details\" : \"\"") ||
        !contains(playback_controller, "void TimelinePlaybackController::recordPlaybackTimingSample(") ||
        !contains(playback_controller, "cause = QStringLiteral(\"decoder\")") ||
        !contains(playback_controller, "cause = QStringLiteral(\"effect\")") ||
        !contains(playback_controller, "cause = QStringLiteral(\"cache\")") ||
        !contains(playback_controller, "cause = QStringLiteral(\"storage\")")) {
      return fail("dropped frames need exact clip/source evidence, cause classification, and an actionable contextual drill-down");
    }
    if (!contains(timeline_view,
                  "timeline-captions:v2|track:dedicated-cc-no-empty-audio|"
                  "authoring:clip-and-word-text-timing|preview:title-safe-active-word|"
                  "delivery:burn-in-srt-vtt|persistence:undo-save-reopen|ui:contextual") ||
        !contains(timeline_view, "function programCaptionRichText(layer)") ||
        !contains(timeline_view, "timelineDocument.exportCaptionSidecar(\"srt\")") ||
        !contains(timeline_view, "timelineDocument.exportCaptionSidecar(\"vtt\")") ||
        !contains(timeline_view, "root.programClip.mediaType === \"caption\"") ||
        !contains(effects_panel, "title: \"Word Timing\"") ||
        !contains(effects_panel, "root.documentController.updateSelectedCaptionWord(")) {
      return fail("caption delivery needs a dedicated low-density CC lane, contextual word timing, title-safe preview, and SRT/VTT actions");
    }
    if (!contains(timeline_view,
                  "timeline-text-edit:v3|plan:controller-owned-document-revision|"
                  "stage:delete-rearrange-earlier-later|"
                  "review:nonmutating-final-transcript-duration-operations|"
                  "restore:per-operation-or-clear-all|stale:hash-invalidated|"
                  "commit:all-or-nothing-one-undo|"
                  "sync:video-audio-captions-markers-automation-review-ranges-multicam|"
                  "persistence:save-reopen|export:ordinary-delivery-order|"
                  "ui:existing-transcript-window-calm-until-staged") ||
        !contains(timeline_view, "key: \"text-based-edit\"") ||
        !contains(timeline_view, "objectName: \"timelineTranscribeAudioButton\"") ||
        !contains(timeline_view, "timelineDocument.transcribeTimelineAudio()") ||
        !contains(timeline_view, "timelineDocument.cancelTranscription()") ||
        !contains(timeline_view, "timelineDocument.transcriptionStatus") ||
        !contains(timeline_view, "id: captionTranscriptionReplaceButton") ||
        !contains(timeline_view, "objectName: \"timelineTranscriptEditorPopup\"") ||
        !contains(timeline_view, "objectName: \"timelineTranscriptSearchField\"") ||
        !contains(timeline_view, "objectName: \"timelineTranscriptSegmentList\"") ||
        !contains(timeline_view, "function chooseTranscriptWord(word, segment)") ||
        !contains(timeline_view, "timelineDocument.selectClipsInSequenceRange(") ||
        !contains(timeline_view, "objectName: \"timelineStageTextDeletionButton\"") ||
        !contains(timeline_view, "objectName: \"timelineStageTextMoveEarlierButton\"") ||
        !contains(timeline_view, "objectName: \"timelineStageTextMoveLaterButton\"") ||
        !contains(timeline_view, "objectName: \"timelineTextEditReviewPlan\"") ||
        !contains(timeline_view, "objectName: \"timelineTextEditReviewOperationList\"") ||
        !contains(timeline_view, "objectName: \"timelineRestoreStagedTextEditButton\"") ||
        !contains(timeline_view, "objectName: \"timelineTextEditFinalTranscriptPreview\"") ||
        !contains(timeline_view, "objectName: \"timelineCommitTextEditPlanButton\"") ||
        !contains(timeline_view, "timelineDocument.stageTextBasedEditDeletion(") ||
        !contains(timeline_view, "timelineDocument.stageTextBasedEditMove(") ||
        !contains(timeline_view, "timelineDocument.restorePendingTextBasedEdit(") ||
        !contains(timeline_view, "timelineDocument.commitPendingTextBasedEdits()") ||
        !contains(timeline_document_controller, "timeline-text-edit-plan:v1") ||
        !contains(timeline_document_controller,
                  "QString TimelineDocumentController::stageTextBasedEditDeletion(") ||
        !contains(timeline_document_controller,
                  "QString TimelineDocumentController::stageTextBasedEditMove(") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::commitPendingTextBasedEdits()") ||
        !contains(timeline_document_controller,
                  "deterministicDocumentHash() != pending_text_based_edit_document_hash_") ||
        !contains(timeline_document_controller, "pushUndoSnapshot();") ||
        !contains(timeline_document_controller, "syncAudioTracks();")) {
      return fail("text-based editing needs staged delete and rearrange operations, non-mutating review, restore, revision invalidation, and one atomic synchronized commit in the existing transcript window");
    }
    if (!contains(timeline_view,
                  "timeline-transcript-panel:v1|sync:program-and-source|navigation:word-level|") ||
        !contains(timeline_view,
                  "timeline-transcription-workflow:v2|language:eleven-locales|"
                  "speaker-diarization:local-bounded-acoustic-clustering|"
                  "speaker-tags:provider-preferred-stable-editable|") ||
        !contains(timeline_view,
                  "timeline-caption-segmentation:v1|inputs:reading-speed-line-length-lines-semantic-breaks-min-duration-frame-gap|") ||
        !contains(timeline_view,
                  "timeline-caption-validation:v1|checks:spelling-overflow-contrast-safe-area-reading-speed-frame-gaps-export-format|") ||
        !contains(timeline_view, "objectName: \"timelineTranscriptionLanguageCombo\"") ||
        !contains(timeline_view, "objectName: \"timelineTranscriptionDiarizationToggle\"") ||
        !contains(timeline_view, "objectName: \"timelineTranscriptionProcessingDisclosure\"") ||
        !contains(timeline_view, "objectName: \"timelineRetryTranscriptionButton\"") ||
        !contains(timeline_view, "objectName: \"timelineTranscriptWordCorrectionCard\"") ||
        !contains(timeline_view, "timelineDocument.seekTranscriptWord(") ||
        !contains(timeline_view, "timelineDocument.updateTranscriptWord(") ||
        !contains(timeline_view, "objectName: \"timelineCaptionToolsPanel\"") ||
        !contains(timeline_view, "timelineDocument.previewCaptionSegmentation(") ||
        !contains(timeline_view, "timelineDocument.applyCaptionSegmentation(") ||
        !contains(timeline_view, "timelineDocument.validateCaptions(") ||
        !contains(timeline_view, "objectName: \"timelineCaptionValidationIssueList\"") ||
        !contains(timeline_document_controller, "timeline-caption-segmentation:v1") ||
        !contains(timeline_document_controller, "timeline-caption-validation:v1") ||
        !contains(timeline_document_controller, "time_reference") ||
        !contains(timeline_document_controller, "transcription_language_") ||
        !contains(timeline_document_controller, "diarize_transcription_words_locally(") ||
        !contains(timeline_document_controller, "kChunkSeconds = 300.0") ||
        !contains(timeline_document_controller, "Identifying speakers locally in bounded audio chunks")) {
      return fail("professional transcript, transcription, caption segmentation, and validation must remain real, preview-first, editable, and progressively disclosed");
    }
    if (!contains(
            timeline_view,
            "timeline-caption-style-templates:v1|builtins:clean-subtitle-bold-social-boxed-explainer-minimal-lower|"
            "style:font-casing-stroke-shadow-background-position-safe-margins|"
            "variants:16x9-9x16-1x1-4x5|preview:aspect-specific|"
            "apply:selected-or-all-one-undo|custom:user-global-atomic-bounded-64|"
            "render:program-export-shared|persistence:clip-overrides-save-reopen|"
            "ui:existing-transcript-window-no-default-density") ||
        !contains(timeline_view, "objectName: \"timelineCaptionStylesTab\"") ||
        !contains(timeline_view, "objectName: \"timelineCaptionStylesPanel\"") ||
        !contains(timeline_view, "objectName: \"timelineCaptionStyleTemplatePicker\"") ||
        !contains(timeline_view, "objectName: \"timelineCaptionStyleAspectPicker\"") ||
        !contains(timeline_view, "objectName: \"timelineCaptionStylePreview\"") ||
        !contains(timeline_view, "objectName: \"timelineApplyCaptionStyleSelectedButton\"") ||
        !contains(timeline_view, "objectName: \"timelineApplyCaptionStyleAllButton\"") ||
        !contains(timeline_view, "objectName: \"timelineSaveCaptionStyleButton\"") ||
        !contains(timeline_view, "objectName: \"timelineDeleteCaptionStyleButton\"") ||
        !contains(timeline_view, "timelineDocument.applyCaptionStyleTemplate(") ||
        !contains(timeline_view, "timelineDocument.saveSelectedCaptionStyleTemplate(") ||
        !contains(timeline_view, "timelineDocument.deleteCaptionStyleTemplate(") ||
        !contains(timeline_view, "programBackgroundLayer.modelData.captionStrokeWidth") ||
        !contains(timeline_view, "programBackgroundLayer.modelData.captionShadowEnabled") ||
        !contains(timeline_document_controller,
                  "PHOTO_EDITOR_TIMELINE_CAPTION_TEMPLATE_PATH") ||
        !contains(timeline_document_controller,
                  "QVariantList built_in_caption_style_templates()") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::persistCaptionStyleTemplates() const") ||
        !contains(timeline_document_controller, "QStringLiteral(\"timeline-title-v2\")") ||
        !contains(timeline_document_controller,
                  "clip.caption_safe_margin_x") ||
        !contains(timeline_document_controller,
                  "clip.caption_background_radius")) {
      return fail("caption style templates need a calm reusable workflow, complete format-specific treatment, atomic custom storage, and shared Program/export rendering");
    }
    if (!contains(
            timeline_view,
            "timeline-caption-word-emphasis:v1|modes:none-highlight-karaoke-pop-bounce-single-word|"
            "timing:editable-word-in-out|program:active-word-sequence-clock|"
            "export:word-boundary-render-segments|motion:pop-scale-bounce-rise|"
            "reduced-motion:pop-bounce-to-static-highlight-preview-and-delivery|"
            "apply:selected-or-all-one-undo|persistence:save-reopen|"
            "ui:contextual-caption-inspector") ||
        !contains(timeline_view, "function captionEffectiveEmphasisMode(layer)") ||
        !contains(timeline_view, "function captionActiveWordIndex(layer)") ||
        !contains(timeline_view, "function captionEmphasisPhase(layer)") ||
        !contains(timeline_view, "theme.reducedMotion") ||
        !contains(timeline_view, "captionEmphasisReducedMotion") ||
        !contains(effects_panel, "objectName: \"timelineCaptionEmphasisSection\"") ||
        !contains(effects_panel, "objectName: \"timelineCaptionEmphasisModePicker\"") ||
        !contains(effects_panel,
                  "model: [\"None\", \"Highlight\", \"Karaoke\", \"Pop\", \"Bounce\", \"Single Word\"]") ||
        !contains(effects_panel, "objectName: \"timelineCaptionReducedMotionToggle\"") ||
        !contains(effects_panel, "objectName: \"timelineApplyCaptionEmphasisAllButton\"") ||
        !contains(effects_panel, "updateSelectedCaptionWord(") ||
        !contains(effects_panel, "setSelectedCaptionEmphasis(") ||
        !contains(timeline_document_controller,
                  "QString normalized_caption_emphasis_mode(const QString& mode)") ||
        !contains(timeline_document_controller,
                  "clip.start_seconds + word.start_seconds") ||
        !contains(timeline_document_controller, "caption_local_midpoint") ||
        !contains(timeline_document_controller,
                  "sin(PI*min(1,max(0,t/%1)))") ||
        !contains(timeline_document_controller, "QTextDocument main_document") ||
        !contains(timeline_document_controller, "captionEmphasisMode") ||
        !contains(timeline_document_controller, "captionEmphasisReducedMotion")) {
      return fail("word-level caption emphasis needs all six modes, editable timing, matching Program/export evaluation, motion rendering, and a persistent reduced-motion fallback");
    }
    if (!contains(
            timeline_view,
            "timeline-platform-preview-overlays:v1|presets:tiktok-reels-shorts-feed|"
            "layers:platform-controls-captions-profile-device-crops-safe-regions|"
            "geometry:normalized-aspect-specific|format:mismatch-warning-one-click-switch|"
            "state:session-only|export:preview-only-qml-state-never-project-render|"
            "ui:viewer-menu-modular-popup|density:visible-only-when-enabled") ||
        !contains(timeline_view, "property string platformOverlayPreset: \"off\"") ||
        !contains(timeline_view, "function platformOverlayDefinition(key)") ||
        !contains(timeline_view, "\"key\": \"tiktok\"") ||
        !contains(timeline_view, "\"key\": \"reels\"") ||
        !contains(timeline_view, "\"key\": \"shorts\"") ||
        !contains(timeline_view, "\"key\": \"feed\"") ||
        !contains(timeline_view, "objectName: \"timelinePlatformOverlayPopup\"") ||
        !contains(timeline_view, "objectName: \"timelinePlatformOverlayPresetPicker\"") ||
        !contains(timeline_view,
                  "model: [\"Off\", \"TikTok\", \"Instagram Reels\", \"YouTube Shorts\", \"Feed 4:5\"]") ||
        !contains(timeline_view, "objectName: \"timelinePlatformDeviceCropPicker\"") ||
        !contains(timeline_view,
                  "model: [\"None\", \"iPhone Dynamic Island\", \"Android Camera\"]") ||
        !contains(timeline_view, "label: \"Safe regions\"") ||
        !contains(timeline_view, "label: \"Platform controls\"") ||
        !contains(timeline_view, "label: \"Profile actions\"") ||
        !contains(timeline_view, "label: \"Caption region\"") ||
        !contains(timeline_view, "function platformOverlayAspectMismatch()") ||
        !contains(timeline_view, "objectName: \"timelinePlatformUseRecommendedFormatButton\"") ||
        !contains(timeline_view,
                  "timelineDocument.applyOutputPreset(") ||
        !contains(timeline_view, "objectName: \"timelineProgramPlatformPreviewOverlay\"") ||
        !contains(timeline_view, "visible: root.platformOverlayPreset !== \"off\"") ||
        !contains(platform_overlay,
                  "timeline-platform-overlay-surface:v1|geometry:normalized-platform-safe-regions|"
                  "layers:unsafe-wash-safe-corners-controls-profile-caption-device-cutout|"
                  "interaction:none|render-path:program-only") ||
        !contains(platform_overlay, "objectName: \"timelinePlatformUnsafeRight\"") ||
        !contains(platform_overlay, "objectName: \"timelinePlatformActionRail\"") ||
        !contains(platform_overlay, "objectName: \"timelinePlatformCaptionRegion\"") ||
        !contains(platform_overlay, "objectName: \"timelinePlatformDynamicIsland\"") ||
        !contains(platform_overlay, "objectName: \"timelinePlatformAndroidCamera\"") ||
        !contains(platform_overlay, "enabled: false") ||
        !contains(cmake, "qml/components/TimelinePlatformOverlay.qml") ||
        contains(timeline_document_controller, "platformOverlayPreset") ||
        contains(timeline_document_controller, "platformPreviewOverlay")) {
      return fail("platform previews need accurate aspect-specific obstruction guides, modular controls, device crops, and an architecturally separate Program-only render path");
    }
    if (!contains(
            timeline_view,
            "timeline-viewer-guide-presets:v1|presets:thirds-crosshair-custom-motion-path-"
            "crop-bounds-title-action-safe-platform-ui|surface:actual-program-only|"
            "custom:four-normalized-session-controls|motion:real-selected-clip-keyframes|"
            "crop:inverse-live-transform|platform:existing-aspect-specific-simulator|"
            "state:session-only-document-clean|export:never-rendered|"
            "ui:one-view-menu-modular-popup|density:off-by-default") ||
        !contains(timeline_view, "property string viewerGuidePreset: \"off\"") ||
        !contains(timeline_view, "function viewerGuidePresetAt(index)") ||
        !contains(timeline_view, "objectName: \"timelineViewerGuidePresetPicker\"") ||
        !contains(timeline_view,
                  "model: [\"Off\", \"Rule of Thirds\", \"Center Crosshair\", \"Custom Guides\",") ||
        !contains(timeline_view, "\"Motion Path\", \"Crop Bounds\", \"Title + Action Safe\"]") ||
        !contains(timeline_view, "objectName: \"timelineCustomGuideControls\"") ||
        !contains(timeline_view, "objectName: \"timelineCustomGuideVerticalASlider\"") ||
        !contains(timeline_view, "objectName: \"timelineCustomGuideHorizontalBSlider\"") ||
        !contains(timeline_view, "objectName: \"timelineResetCustomGuidesButton\"") ||
        !contains(timeline_view, "objectName: \"timelineViewerGuideOverlay\"") ||
        !contains(timeline_view, "property var programGuideClip: programGuidePictureClip()") ||
        !contains(timeline_view, "function programGuidePictureClip()") ||
        !contains(timeline_view, "root.viewerGuidePreset === \"motion-path\"") ||
        !contains(timeline_view, "readonly property var pathPoints: root.currentClipMotionPathPoints()") ||
        !contains(viewer_guide_overlay,
                  "timeline-viewer-guides:v1|presets:thirds-crosshair-custom-motion-path-"
                  "crop-bounds-title-safe-platform-ui|geometry:normalized-and-transform-aware|") ||
        !contains(viewer_guide_overlay, "readonly property bool showThirds:") ||
        !contains(viewer_guide_overlay, "readonly property bool showCrosshair:") ||
        !contains(viewer_guide_overlay, "readonly property bool showCustom:") ||
        !contains(viewer_guide_overlay, "readonly property bool showCropBounds:") ||
        !contains(viewer_guide_overlay, "const cropWidth = width / scale") ||
        !contains(viewer_guide_overlay, "ctx.rotate(-Number(root.frameRotationDegrees || 0)") ||
        !contains(viewer_guide_overlay, "enabled: false") ||
        !contains(cmake, "qml/components/TimelineViewerGuideOverlay.qml") ||
        contains(timeline_document_controller, "viewerGuidePreset") ||
        contains(timeline_document_controller, "customGuideVerticalA")) {
      return fail("viewer guides need one restrained session-only Program surface with real composition, motion, crop, safe-area, custom, and platform presets");
    }
    if (!contains(
            timeline_view,
            "timeline-stabilization:v1|analysis:vidstab-two-pass-cancellable-progress|"
            "settings:smoothing-crop-protection-rolling-shutter-signed-skew|"
            "program:responsive-stabilized-preview-before-after|"
            "export:lossless-ffv1-cache-original-resolution|"
            "cache:source-signature-trim-invalidated-reusable-transform|"
            "persistence:settings-artifacts-save-reopen|"
            "undo:settings-one-continuous-edit|ui:collapsed-motion-inspector") ||
        !contains(effects_panel, "objectName: \"timelineStabilizationSection\"") ||
        !contains(effects_panel, "expanded: false") ||
        !contains(effects_panel, "objectName: \"timelineStabilizationEnableButton\"") ||
        !contains(effects_panel, "objectName: \"timelineRollingShutterEnableButton\"") ||
        !contains(effects_panel, "objectName: \"timelineStabilizationSmoothingControl\"") ||
        !contains(effects_panel, "objectName: \"timelineStabilizationCropProtectionControl\"") ||
        !contains(effects_panel, "objectName: \"timelineRollingShutterAmountControl\"") ||
        !contains(effects_panel, "objectName: \"timelineStabilizationProgress\"") ||
        !contains(effects_panel, "objectName: \"timelineAnalyzeStabilizationButton\"") ||
        !contains(effects_panel, "objectName: \"timelineCancelStabilizationButton\"") ||
        !contains(effects_panel, "beforeAfterObjectName: \"timelineStabilizationBeforeAfterButton\"") ||
        !contains(effects_panel, "setSelectedClipStabilization(") ||
        !contains(effects_panel, "analyzeSelectedClipStabilization()") ||
        !contains(effects_panel, "cancelStabilizationAnalysis()") ||
        !contains(effects_panel, "resetSelectedClipStabilization()") ||
        !contains(effects_panel, "setStabilizationCompareBefore(") ||
        !contains(timeline_view, "function programUsesStabilizedPreview(clip, stabilizedVisual)") ||
        !contains(timeline_view, "root.programLayerSourceSeconds(modelData, true)") ||
        !contains(timeline_view,
                  "root.programPlaybackSourceUrl(programBackgroundLayer.modelData, true)") ||
        !contains(timeline_view,
                  "root.programPlaybackSourceUrl(programAudioLayer.modelData) : \"\"") ||
        !contains(timeline_document_controller, "vidstabdetect=result=") ||
        !contains(timeline_document_controller, "fileformat=ascii") ||
        !contains(timeline_document_controller, "vidstabtransform=input=") ||
        !contains(timeline_document_controller, "perspective=x0=") ||
        !contains(timeline_document_controller, "QStringLiteral(\"-progress\")") ||
        !contains(timeline_document_controller, "QStringLiteral(\"ffv1\")") ||
        !contains(timeline_document_controller, "QStringLiteral(\"yuv422p10le\")") ||
        !contains(timeline_document_controller, "stabilization_render_path") ||
        !contains(timeline_document_controller, "stabilization_analysis_source_signature") ||
        !contains(timeline_document_controller, "stabilization_analysis_trim_start_seconds") ||
        !contains(timeline_document_controller, "Stabilization cancelled") ||
        contains(timeline_document_controller, "stabilizationCompareBefore\"")) {
      return fail("stabilization needs a collapsed professional inspector, real cancellable vid.stab analysis, visual-only before/after preview, lossless full-resolution delivery media, and persistent cache-safe settings");
    }
    if (!contains(timeline_view,
                  "timeline-cleanup-review:v2|detect:conservative-fillers-timed-speech-gaps-waveform-silence|"
                  "audio:ffmpeg-serial-bounded-cancellable|mapping:source-trim-rate-sequence|"
                  "review:non-destructive-toggle-preview|apply:confirmed-multi-range-one-undo|"
                  "handles:frame-editable-natural-pause|sync:video-audio-captions-markers-automation|"
                  "persistence:save-reopen|ui:transcript-popup-mode") ||
        !contains(timeline_view, "text: \"Review Cleanup · \"") ||
        !contains(timeline_view, "objectName: \"timelineTranscriptCleanupList\"") ||
        !contains(timeline_view, "objectName: \"timelineAnalyzeSilenceButton\"") ||
        !contains(timeline_view, "timelineDocument.analyzeTimelineSilence()") ||
        !contains(timeline_view, "timelineDocument.cancelSilenceAnalysis()") ||
        !contains(timeline_view, "timelineDocument.silenceAnalysisStatus") ||
        !contains(timeline_view, "function previewCleanupSuggestion(suggestion)") ||
        !contains(timeline_view, "id: transcriptCleanupApplyButton") ||
        !contains(timeline_view, "timelineDocument.applyTranscriptCleanupSuggestions(")) {
      return fail("cleanup review needs real cancellable audio analysis, conservative inert suggestions, explicit preview and selection, persistence, and one confirmed atomic batch apply inside the transcript popup");
    }
    if (!contains(timeline_view,
                  "timeline-output-variants:v1|formats:16x9-9x16-1x1-4x5|"
                  "switch:single-context-menu|overrides:clip-framing-transform-keyframes-title-layout|"
                  "inherit:first-visit|reset:selected-active-format|viewer-export:active-format|"
                  "undo-save-reopen:true|density:advanced-only") ||
        !contains(timeline_view, "objectName: \"timelineOutputVariantMenuButton\"") ||
        !contains(timeline_view, "timelineDocument.outputVariants") ||
        !contains(timeline_view, "timelineDocument.activeOutputVariant") ||
        !contains(timeline_view, "timelineDocument.applyOutputPreset(key.substring(8))") ||
        !contains(timeline_view, "timelineDocument.resetSelectedClipOutputVariantOverride()")) {
      return fail("output variants need one advanced-only shared menu with four formats and independently persistent clip framing controls");
    }
    if (!contains(
            timeline_view,
            "timeline-playback-truth:v1|rate:source-sequence-duration-program-audio-export|") ||
        !contains(timeline_view, "function clipSourceTime(modelData, localX)") ||
        !contains(timeline_view, "Math.max(0.25, Number(modelData.playbackRate || 1))")) {
      return fail("the speed-aware Program, razor, and export timing contract is missing");
    }
    if (!contains(
            timeline_view,
            "timeline-advanced-retime:v2|model:timeline-local-to-source-time-curve|"
            "presets:constant-ramp-up-ramp-down-reverse-freeze|"
            "holds:duration-authored-at-playhead|curve:linear-ease-hold-bezier-linked-broken|"
            "preview:bezier-source-seek-effective-rate|frames:sampling-blend-optical-flow|"
            "audio:preserve-follow-speed-mute-reverse-hold-silent|"
            "limits:source-in-out-handle-overshoot-peak-rate|"
            "cache:existing-final-preview-render-range|"
            "export:bounded-sampled-curve-shared-options|duration:ripple-downstream|"
            "persistence:undo-save-reopen") ||
        !contains(timeline_view, "root.animatedClipValueAtLocal(") ||
        !contains(timeline_view, "setPlayerPitchCompensation(") ||
        !contains(timeline_view, "timelineDocument.addSelectedClipRetimePoint(") ||
        !contains(timeline_view, "timelineDocument.addSelectedClipRetimeHold(local, 1.0)") ||
        !contains(timeline_view, "timelineDocument.applySelectedClipRetimePreset(\"reset\")") ||
        !contains(effects_panel, "\"key\": \"ramp-up\"") ||
        !contains(effects_panel, "\"key\": \"ramp-down\"") ||
        !contains(effects_panel, "\"key\": \"reverse\"") ||
        !contains(effects_panel, "\"key\": \"freeze-frame\"") ||
        !contains(effects_panel, "objectName: \"timelineSpeedRampOptionsSection\"") ||
        !contains(effects_panel, "objectName: \"timelineRetimePitchStrategyControl\"") ||
        !contains(effects_panel, "objectName: \"timelineRetimeInterpolationModeControl\"") ||
        !contains(effects_panel, "objectName: \"timelineRetimeSourceLimitStatus\"") ||
        !contains(effects_panel, "objectName: \"timelineRetimeAddHoldButton\"") ||
        !contains(effects_panel, "objectName: \"timelineRetimeBuildPreviewButton\"") ||
        !contains(effects_panel, "root.documentController.generateRenderCache()") ||
        !contains(effects_panel, "root.openCurveEditorRequested(\"sourceTime\")") ||
        !contains(timeline_document_controller, "TimelineDocumentController::addSelectedClipRetimeHold(") ||
        !contains(timeline_document_controller, "TimelineDocumentController::setSelectedClipRetimeOptions(") ||
        !contains(timeline_document_controller, "minterpolate=fps=%1:mi_mode=blend") ||
        !contains(timeline_document_controller, "mi_mode=mci:mc_mode=aobmc:me_mode=bidir:vsbmc=1") ||
        !contains(timeline_document_controller, "QStringLiteral(\"asetrate=%1\")") ||
        !contains(timeline_document_controller, "clipRetimeSourceLimitWarnings")) {
      return fail("advanced retiming needs compact presets and options, authored holds and Bezier curves, source-limit feedback, selectable frame/audio policy, and shared cached-preview/export truth");
    }
    if (!contains(timeline_view,
                  "timeline-editorial-chrome:v3|appearance:dark-only|surfaces:borderless|"
                  "buttons:borderless-rest|primary:restrained-fill|tabs:underline|"
                  "selection:soft-fill-hairline|tracks:accent-rail|viewer:sequence-aspect-fit-effects|"
                  "effects:seven-real-stack-controls|focus:keyboard-only|"
                  "signal-colors:functional-only") ||
        !contains(action_button, "action-button:v11|shared-component:true|hierarchy:primary-graphite-secondary-neutral-tertiary-quiet") ||
        !contains(action_button, "flat-surface:true|accent-restrained:true") ||
        !contains(action_button, "borderless-rest:true|hover-fill-not-border:true") ||
        !contains(tool_button, "cornerRadius: theme.radiusControl") ||
        !contains(filter_chip, "theme.commandFill(\"secondary\", root.active") ||
        !contains(track_header, "target:accent-rail|edges:structural-only") ||
        !contains(theme, "readonly property bool darkMode: true") ||
        !contains(timeline_view, "objectName: \"timelineProgramMonitorStage\"") ||
        !contains(timeline_view, "readonly property real sequenceAspectRatio") ||
        !contains(timeline_view, "parent.height * sequenceAspectRatio") ||
        !contains(timeline_view, "parent.width / sequenceAspectRatio")) {
      return fail("the dark-only editorial chrome and aspect-fitted Program viewer contract is missing");
    }
    if (!contains(source_panel, "timeline-source-panel-style:v2|surface:shared") ||
        !contains(effects_panel, "timeline-effects-panel-style:v9|surface:borderless") ||
        !contains(effects_panel, "seven-real-search-favorites-recents-categories-compatibility-live-thumbnails-versioned-presets-stack-amount-enable-bypass-reset-remove-clear") ||
        !contains(effects_panel, "timeline-keyframe-toolbar:v1|hierarchy:heading-count-menu|controls:dedicated-row|responsive:no-overlap|accessibility:combined-count") ||
        !contains(effects_panel, "objectName: \"timelineKeyframeToolbarLabel\"") ||
        !contains(effects_panel, "objectName: \"timelineKeyframeToolbarActions\"") ||
        !contains(timeline_view, "import QtQuick.Effects") ||
        !contains(timeline_view, "MultiEffect {") ||
        !contains(timeline_view, "function clipEffectBrightness(clip)") ||
        !contains(timeline_view, "function clipEffectBlur(clip)") ||
        !contains(track_header, "timeline-track-header-style:v3|surface:quiet-editorial")) {
      return fail("Source, Inspector, and track header style contracts must be present");
    }
    if (!contains(
            timeline_view,
            "timeline-effect-discovery:v1|favorites:persistent|"
            "recents:persistent-deduplicated-bounded-eight|"
            "categories:all-favorites-recent-color-style-utility|"
            "compatibility:selected-media-filter|"
            "thumbnails:bounded-live-current-poster-no-animation|"
            "search:keyboard-command-name-category-description-keywords|"
            "ui:existing-effects-panel-progressive-no-default-density") ||
        !contains(timeline_view, "property string effectFavoritesJson: \"[]\"") ||
        !contains(timeline_view, "property string effectRecentsJson: \"[]\"") ||
        !contains(timeline_view, "function normalizedEffectNames(names, maximumCount)") ||
        !contains(timeline_view, "effectRecents = updated.slice(0, 8)") ||
        !contains(timeline_view, "function applyEffectToClip(clipIndex, effectName)") ||
        !contains(timeline_view, "recordRecentEffect(effectName)") ||
        !contains(timeline_view, "onEffectFavoritesChanged: timelineWorkspaceSettings.effectFavoritesJson") ||
        !contains(timeline_view, "onEffectRecentsChanged: timelineWorkspaceSettings.effectRecentsJson") ||
        !contains(timeline_view, "\"compatibleTypes\": [\"video\", \"photo\", \"image\", \"title\", \"caption\", \"adjustment\"]") ||
        !contains(timeline_view, "favoriteEffectNames: root.effectFavorites") ||
        !contains(timeline_view, "effectPreviewSource: root.safePosterSource(root.currentClip)") ||
        !contains(effects_panel, "function buildEffectDiscoveryItems()") ||
        !contains(effects_panel, "function effectMatchesSearch(effect)") ||
        !contains(effects_panel, "function isEffectCompatible(effect)") ||
        !contains(effects_panel, "objectName: \"timelineEffectCategoryMenu\"") ||
        !contains(effects_panel, "objectName: \"timelineEffectCompatibilityFilter\"") ||
        !contains(effects_panel, "objectName: \"timelineEffectKeyboardSearch\"") ||
        !contains(effects_panel, "sequences: [StandardKey.Find]") ||
        !contains(effects_panel, "model: root.filteredEffectLibraryItems") ||
        !contains(effect_library_row, "timeline-effect-library-row:v2|") ||
        !contains(effect_library_row, "objectName: \"timelineEffectLiveThumbnail\"") ||
        !contains(effect_library_row, "sourceSize.width: 144") ||
        !contains(effect_library_row, "sourceSize.height: 88") ||
        !contains(effect_library_row, "asynchronous: true") ||
        !contains(effect_library_row, "MultiEffect {") ||
        !contains(effect_library_row, "objectName: \"timelineEffectFavoriteButton\"") ||
        contains(effect_library_row, "NumberAnimation")) {
      return fail("effect discovery must stay progressive while providing persistent favorites, bounded recents, compatibility-aware keyboard search, and bounded live thumbnails");
    }
    if (!contains(
            timeline_view,
            "timeline-effect-stack-presets:v2|builtins:12-stable-ids-supported-stacks-not-persisted|"
            "media:video-clips-only-current-preview-path|"
            "storage:user-global-atomic-json-bounded-128|"
            "versioning:family-retained-revisions-latest-promotion|"
            "schema:migrate-v1-to-v2|preview:ordered-enabled-amount-availability|"
            "application:selective-merge-or-replace-one-undo|"
            "missing:retained-reported-never-substituted|"
            "persistence:applied-state-save-reopen|ui:single-modular-effects-popup") ||
        !contains(timeline_document_controller, "timeline-effect-stack-presets:v2") ||
        !contains(timeline_document_controller, "PHOTO_EDITOR_TIMELINE_EFFECT_PRESET_PATH") ||
        !contains(timeline_document_controller, "void TimelineDocumentController::loadEffectStackPresets()") ||
        !contains(timeline_document_controller, "bool TimelineDocumentController::persistEffectStackPresets() const") ||
        !contains(timeline_document_controller, "saveSelectedClipEffectStackPreset") ||
        !contains(timeline_document_controller, "effectStackPresetPreview") ||
        !contains(timeline_document_controller, "applyEffectStackPreset") ||
        !contains(timeline_document_controller, "auto custom_preset_count = 0;") ||
        !contains(timeline_document_controller, "skipped %1 unavailable") ||
        !contains(effects_panel, "objectName: \"timelineEffectStackPresetsButton\"") ||
        !contains(effects_panel, "objectName: \"timelineEffectStackPresetPopup\"") ||
        !contains(effects_panel, "objectName: \"timelineEffectPresetVersionSelector\"") ||
        !contains(effects_panel, "objectName: \"timelineEffectPresetMigrationReport\"") ||
        !contains(effects_panel, "objectName: \"timelineEffectPresetMissingEffects\"") ||
        !contains(effects_panel, "objectName: \"timelinePresetEffectSelection-\"") ||
        !contains(effects_panel, "objectName: \"timelineEffectPresetReplaceMode\"") ||
        !contains(effects_panel, "objectName: \"timelineApplyEffectStackPreset\"") ||
        !contains(effects_panel, "root.selectedPresetEffectNames()") ||
        !contains(effects_panel, "root.effectPresetReplaceExisting")) {
      return fail("effect-stack presets need atomic reusable storage, retained revisions, migration and missing-effect reporting, exact previews, and selective one-undo application");
    }
    if (!contains(
            timeline_view,
            "timeline-source-patching:v1|streams:video-audio-independent|"
            "destinations:exclusive-per-kind|disable:either-or-both|"
            "insert-overwrite-append:deterministic|linked-audio:cross-lane|"
            "fallback:none|undo-save-reopen:true") ||
        !contains(timeline_view, "objectName: \"timelineSourcePatchSummary\"") ||
        !contains(timeline_view, "timelineDocument.sourcePatchReady") ||
        !contains(track_header, "readonly property string groupHeadingText:") ||
        !contains(track_header, "Patch source \" + trackData.trackKind + \" to \" + trackData.displayLabel")) {
      return fail("independent source video/audio patch destinations and compact track affordances are missing");
    }
    if (!contains(
            timeline_view,
            "timeline-shuttle-transport:v2|keys:j-k-l-shift-i-shift-o-f|"
            "directions:reverse-stop-forward|ladder:1-2-4-8|"
            "clock:sequence-authoritative|boundaries:auto-stop|frames:paused-step|"
            "navigation:edits-markers-source-in-out-match-frame|feedback:contextual-rate") ||
        !contains(timeline_view, "onActivated: timelinePlayback.shuttleReverse()") ||
        !contains(timeline_view, "onActivated: timelinePlayback.stopShuttle()") ||
        !contains(timeline_view, "onActivated: timelinePlayback.shuttleForward()") ||
        !contains(timeline_view, "timelineDocument.seekSourceInPoint()") ||
        !contains(timeline_view, "timelineDocument.seekSourceOutPoint()") ||
        !contains(timeline_view, "timelineDocument.matchFrameToSource()") ||
        !contains(timeline_view, "objectName: \"timelineShuttleRateChip\"")) {
      return fail("professional keyboard shuttle, source navigation, Match Frame, and contextual rate feedback are missing");
    }
    if (!contains(
            timeline_view,
            "timeline-magnetic-snap:v1|resolver:controller-single-source|"
            "targets:playhead-video-audio-transitions-markers-frames-optional-waveform-peaks|"
            "gestures:move-trim-roll-marker|threshold:pixels-zoom-stable|"
            "feedback:typed-guide-timecode|marker-drag:preview-commit-undo|"
            "performance:waveform-peaks-cached") ||
        !contains(timeline_view, "timelineDocument.resolveTimelineSnap(") ||
        !contains(timeline_view, "key: \"toggle-audio-peak-snapping\"") ||
        !contains(timeline_view, "snapResult.label") ||
        !contains(marker_pin, "timeline-marker-pin:v2|") ||
        !contains(marker_pin, "signal movePreviewRequested(string markerId, real proposedSeconds)") ||
        !contains(marker_pin, "signal moveCommittedRequested(string markerId, real proposedSeconds)")) {
      return fail("typed controller-backed magnetic snapping and draggable marker feedback are missing");
    }
    if (!contains(
            timeline_view,
            "timeline-clip-clipboard:v2|copy-cut-paste:overwrite-insert|"
            "selection:multi-track-relative-spacing|routing:targeted-video-audio|"
            "linked-av:preserved|duplicate:multi-selection|"
            "replace:source-timing-finishing-preserved|"
            "fit-to-fill:marked-source-retimed-target-duration|match-frame:exact|"
            "undo:atomic|feedback:edit-menu-status") ||
        !contains(timeline_view, "objectName: \"timelineEditMenuButton\"") ||
        !contains(timeline_view, "function timelineEditMenuItems()") ||
        !contains(timeline_view, "timelineDocument.copySelectedClips()") ||
        !contains(timeline_view, "timelineDocument.cutSelectedClips()") ||
        !contains(timeline_view, "timelineDocument.pasteClipsAtPlayhead(\"overwrite\")") ||
        !contains(timeline_view, "timelineDocument.pasteClipsAtPlayhead(\"insert\")") ||
        !contains(timeline_view, "timelineDocument.replaceSelectedClipFromSource()") ||
        !contains(timeline_view, "objectName: \"timelineFitToFillSourceAction\"") ||
        !contains(timeline_view, "timelineDocument.fitToFillSelectedClipFromSource()") ||
        !contains(timeline_view, "sequences: [StandardKey.Copy]") ||
        !contains(timeline_view, "sequences: [StandardKey.Cut]") ||
        !contains(timeline_view, "sequences: [StandardKey.Paste]") ||
        !contains(shortcuts, "Copy, cut, or paste selected timeline clips.") ||
        !contains(shortcuts, "Replace selected clip media from Source without changing sequence timing.")) {
      return fail("the unified multi-track clipboard, replace, Match Frame, Edit menu, and standard shortcuts are missing");
    }
    if (!contains(
            timeline_view,
            "timeline-fullscreen-program:v1|surface:actual-program-reparented|"
            "window:true-fullscreen-restored|transport:play-pause-frame-step-seek|"
            "timecode:always|overlays:temporary-safe-platform-off-on-exit|"
            "controls:auto-hide-pointer-keyboard|return:visible-and-escape|"
            "continuity:source-playhead-playback|density:normal-workspace-unchanged") ||
        !contains(timeline_view, "key: \"full-screen-program\"") ||
        !contains(timeline_view, "function enterFullScreenProgram()") ||
        !contains(timeline_view, "function exitFullScreenProgram()") ||
        !contains(timeline_view,
                  "property bool fullScreenProgramNativeWindowEnabled: true") ||
        !contains(timeline_view, "hostWindow.visibility = Window.FullScreen") ||
        !contains(timeline_view,
                  "hostWindow.visibility = fullScreenProgramPreviousWindowVisibility") ||
        !contains(timeline_view, "parent: root.fullScreenProgramActive") ||
        !contains(timeline_view, "? fullScreenProgramStage") ||
        !contains(timeline_view, "programComparisonWindow.comparisonStage : programMonitorStage") ||
        !contains(timeline_view, "objectName: \"timelineFullScreenProgramSurface\"") ||
        !contains(timeline_view, "objectName: \"timelineFullScreenProgramStage\"") ||
        !contains(timeline_view, "objectName: \"timelineFullScreenExitButton\"") ||
        !contains(timeline_view, "objectName: \"timelineFullScreenTimecode\"") ||
        !contains(timeline_view, "objectName: \"timelineFullScreenPlayPauseButton\"") ||
        !contains(timeline_view, "objectName: \"timelineFullScreenSeekSlider\"") ||
        !contains(timeline_view, "objectName: \"timelineFullScreenSafeAreasButton\"") ||
        !contains(timeline_view,
                  "objectName: \"timelineFullScreenPlatformOverlayMenu\"") ||
        !contains(timeline_view, "fullScreenProgramSafeAreas = false") ||
        !contains(timeline_view, "fullScreenProgramPlatformPreset = \"off\"") ||
        !contains(timeline_view, "sequence: \"Esc\"") ||
        !contains(main_qml, "objectName: \"appNavigationBar\"") ||
        !contains(main_qml, "visible: !window.timelineProgramFullScreenActive") ||
        !contains(main_qml,
                  "anchors.margins: window.timelineProgramFullScreenActive ? 0 : 6") ||
        !contains(runtime_proof,
                  "Full-screen Program did not reparent the truthful monitor") ||
        !contains(runtime_proof,
                  "Full-screen Program did not restore the normal Timeline layout")) {
      return fail("full-screen Program needs the actual compositor, reversible native full screen, complete transport, temporary overlays, and an obvious exit without increasing default density");
    }
    if (!contains(
            timeline_view,
            "timeline-program-comparison:v1|surface:actual-program-reparented|"
            "modes:split-wipe-overlay-linear-light-difference|"
            "references:captured-program-immutable-version-still-user-still|"
            "effects:selected-program-clip-session-only-bypass-no-history|"
            "frame:shared-playhead-paused-refreshable|"
            "controls:mode-source-position-opacity-gain-step|"
            "return:close-escape-state-restored|"
            "performance:one-bounded-grab-no-second-live-compositor|"
            "density:modular-window-default-unchanged") ||
        !contains(timeline_view, "key: \"compare-program\"") ||
        !contains(timeline_view, "function openProgramComparison(invoker)") ||
        !contains(timeline_view, "function closeProgramComparison()") ||
        !contains(timeline_view, "function captureProgramComparisonReference()") ||
        !contains(timeline_view, "function useProgramComparisonVersion(index)") ||
        !contains(timeline_view, "function useProgramComparisonReferenceStill(sourceUrl)") ||
        !contains(timeline_view, "function beginProgramEffectsComparison()") ||
        !contains(timeline_view, "timelineDocument.pauseSequence()") ||
        !contains(timeline_view, "programMonitor.grabToImage(function(result)") ||
        !contains(timeline_view, "objectName: \"timelineProgramComparisonWindow\"") ||
        !contains(timeline_view, "objectName: \"timelineProgramComparisonStage\"") ||
        !contains(timeline_view, "objectName: \"timelineProgramComparisonCloseButton\"") ||
        !contains(timeline_view, "objectName: \"timelineProgramComparisonSourceMenu\"") ||
        !contains(timeline_view, "objectName: \"timelineProgramComparisonBlendSlider\"") ||
        !contains(timeline_view, "objectName: \"timelineProgramComparisonMode_\"") ||
        !contains(timeline_view, "objectName: \"timelineProgramComparisonSplitView\"") ||
        !contains(timeline_view, "objectName: \"timelineProgramComparisonWipeView\"") ||
        !contains(timeline_view, "objectName: \"timelineProgramComparisonOverlayView\"") ||
        !contains(timeline_view, "objectName: \"timelineProgramComparisonDifferenceView\"") ||
        !contains(timeline_view,
                  "String(clip.clipId || \"\") === String(root.programClip.clipId || \"\")") ||
        !contains(timeline_view, "programComparisonBypassEffects = false") ||
        !contains(timeline_document_controller, "comparisonMediaPath") ||
        !contains(timeline_document_controller, "QFile::copy(comparison_media_source") ||
        !contains(timeline_document_controller, "QFile::remove(comparison_media_path)") ||
        !contains(difference_effect,
                  "timeline-difference-effect:v2|space:controller-linear-light-srgb") ||
        !contains(timeline_document_controller,
                  "renderProgramComparisonDifference(") ||
        !contains(timeline_document_controller,
                  "programComparisonCapturePath(") ||
        !contains(timeline_document_controller,
                  "program-comparison-captures") ||
        !contains(timeline_document_controller,
                  "std::abs(current_linear - reference_linear) * bounded_gain") ||
        !contains(timeline_document_controller,
                  "for (auto index = 16; index < cached_frames.size(); ++index)") ||
        !contains(cmake, "qml/components/TimelineDifferenceEffect.qml") ||
        !contains(runtime_proof,
                  "Program comparison did not reparent the truthful monitor") ||
        !contains(runtime_proof,
                  "Program comparison did not restore the ordinary Program monitor")) {
      return fail("Program comparison needs one truthful reparented compositor, exact paused-frame references, four professional visual modes, immutable version stills, non-mutating effect bypass, and complete close restoration in a modular window");
    }
    if (!contains(
            timeline_view,
            "timeline-program-scopes:v1|video:waveform-vectorscope-rgb-parade-histogram|"
            "audio:post-processing-stereo-phase-correlation|source:actual-program-composite|"
            "analysis:bounded-480x270-qtconcurrent-latest-wins-stale-rejected|"
            "rate:four-fps-live-freeze-manual|ui:one-scope-bottom-dock-all-five-popout|"
            "transport:never-paused-or-blocked|density:closed-by-default") ||
        !contains(timeline_view, "key: \"program-scopes\"") ||
        !contains(timeline_view, "function captureProgramScopeFrame(force)") ||
        !contains(timeline_view, "programMonitor.grabToImage(function(result)") ||
        !contains(timeline_view, "objectName: \"timelineDockedProgramScopes\"") ||
        !contains(timeline_view, "objectName: \"timelineProgramScopesWindow\"") ||
        !contains(timeline_view, "objectName: \"timelineDetachedProgramScopes\"") ||
        !contains(timeline_view, "interval: 250") ||
        !contains(timeline_scopes,
                  "timeline-scopes-panel:v1|video:waveform-vectorscope-rgb-parade-histogram") ||
        !contains(timeline_scopes, "objectName: \"timelineScopesAllGrid\"") ||
        !contains(timeline_scopes, "objectName: \"timelineScope\" + readableLabel") ||
        !contains(timeline_scopes, "root.scopeData.vectorscopePoints") ||
        !contains(timeline_scopes, "root.audioPhaseData.points") ||
        !contains(timeline_document_controller, "analyze_program_scope_frame(") ||
        !contains(timeline_document_controller, "QtConcurrent::run(") ||
        !contains(timeline_document_controller, "pending_program_scope_revision_") ||
        !contains(audio_output, "phase_correlation_ = qBound(-1.0") ||
        !contains(audio_output, "phase_points->push_back(QVariantMap") ||
        !contains(cmake, "qml/components/TimelineScopesPanel.qml") ||
        !contains(runtime_proof, "Timeline scopes did not publish all five truthful scope surfaces")) {
      return fail("Timeline scopes need truthful bounded Program-frame video analysis, decoded stereo phase data, a calm dock, an all-scope pop-out, and live updates that never pause transport");
    }
    if (!contains(
            timeline_view,
            "timeline-source-program-gang:v1|monitors:existing-source-and-actual-program|"
            "directions:program-drives-source-source-scrub-drives-program|"
            "mapping:trim-rate-reverse-and-nonlinear-retime|"
            "playback:program-master-source-muted|"
            "match-frame:active-highest-visible-picture|"
            "gap:explicit-no-match-last-source-retained|"
            "ui:source-link-toggle-view-menu-time-map|state:session-only-document-clean") ||
        !contains(timeline_view, "key: \"gang-monitors\"") ||
        !contains(timeline_view, "function setProgramSourceGangEnabled(enabled)") ||
        !contains(timeline_view, "function synchronizeGangedSourceToProgram()") ||
        !contains(timeline_view, "function seekProgramFromGangedSource(sourceSeconds)") ||
        !contains(timeline_view, "objectName: \"timelineProgramSourceGangSyncTimer\"") ||
        !contains(timeline_view, "interval: 33") ||
        !contains(timeline_view, "timelinePlayback.togglePlayback()") ||
        !contains(
            source_panel,
            "timeline-source-program-gang:v1|direction:program-to-source-and-source-to-program|") ||
        !contains(source_panel, "objectName: \"timelineSourceProgramGangButton\"") ||
        !contains(source_panel, "objectName: \"timelineSourceProgramGangOverlay\"") ||
        !contains(source_panel, "objectName: \"timelineSourceProgramGangTimeMap\"") ||
        !contains(source_panel, "muted: root.programGangEnabled") ||
        !contains(source_panel, "root.gangedSourceSeekRequested(bounded)") ||
        !contains(timeline_document_controller_header, "programSourceGangState") ||
        !contains(timeline_document_controller, "synchronizeSourceToProgram(") ||
        !contains(timeline_document_controller, "gangedProgramPositionForSource(") ||
        !contains(timeline_document_controller, "clipTimelineSecondsForSourceTime(") ||
        !contains(runtime_proof,
                  "Ganged Source and Program did not maintain exact bidirectional frame mapping")) {
      return fail("ganged Source and Program monitors need exact trim/retime mapping in both directions, Program-master playback, honest gap state, one calm direct toggle, and non-mutating runtime proof");
    }
    if (!contains(
            timeline_view,
            "timeline-pancake:v1|layout:two-stacked-sequences|source:saved-read-only|"
            "selection:clip-drag-or-exact-in-out-range|destination:frame-snapped-playhead|"
            "modes:copy-move-overwrite-insert|timing:relative-track-spacing-and-source-trims|"
            "linked-av:preserved|move:atomic-two-document-undo-redo|"
            "persistence:source-and-destination-save-reopen|"
            "ui:focused-modular-window-no-default-density") ||
        !contains(timeline_view, "key: \"pancake-timelines\"") ||
        !contains(timeline_view, "objectName: \"timelinePancakePopup\"") ||
        !contains(timeline_view, "objectName: \"timelinePancakeEditor\"") ||
        !contains(timeline_view, "timelineDocument.pancakeSequences().length > 0") ||
        !contains(pancake_editor, "objectName: \"timelinePancakeSourceTimeline\"") ||
        !contains(pancake_editor, "objectName: \"timelinePancakeDestinationTimeline\"") ||
        !contains(pancake_editor, "objectName: \"timelinePancakeDestinationDropArea\"") ||
        !contains(pancake_editor, "Drag.keys: [\"timeline-pancake-clip\"]") ||
        !contains(pancake_editor, "keys: [\"timeline-pancake-clip\"]") ||
        !contains(pancake_editor, "objectName: \"timelinePancakeSourceInField\"") ||
        !contains(pancake_editor, "objectName: \"timelinePancakeSourceOutField\"") ||
        !contains(pancake_editor, "objectName: \"timelinePancakeDestinationField\"") ||
        !contains(pancake_editor, "objectName: \"timelinePancakeTransferButton\"") ||
        !contains(pancake_editor, "timelineDocument.pancakeTransferPreview(") ||
        !contains(pancake_editor, "timelineDocument.transferFromPancakeSequence(") ||
        !contains(pancake_editor, "One Undo restores both sequences.") ||
        !contains(timeline_document_controller,
                  "QVariantList TimelineDocumentController::pancakeSequences() const") ||
        !contains(timeline_document_controller,
                  "QVariantMap TimelineDocumentController::pancakeSequencePreview(") ||
        !contains(timeline_document_controller,
                  "int TimelineDocumentController::transferFromPancakeSequence(") ||
        !contains(timeline_document_controller,
                  "undo_stack_.last().external_document_bytes.insert(") ||
        !contains(timeline_document_controller,
                  "restoreExternalDocuments(snapshot.external_document_bytes)") ||
        !contains(timeline_document_test,
                  "one ordinary Undo should restore both saved source bytes and the destination sequence") ||
        !contains(timeline_document_test,
                  "both pancake sequences should reopen with their transferred and retained material intact") ||
        !contains(cmake, "qml/components/TimelinePancakeEditor.qml")) {
      return fail("pancake editing needs two stacked saved sequences, real clip drag and range transfer, exact track timing, atomic cross-document move undo, and a focused shared-style surface");
    }
    if (!contains(
            waveform,
            "timeline-waveform:v2|cache:media-revision-multiresolution-bounded|tiers:600-2400-9600|"
            "zoom:adaptive|trim:source-cropped|generation:async-serial|viewport:lazy|"
            "background:transparent|fallback:progressive") ||
        !contains(waveform, "requiredFullSourcePixels: Math.max(1, width) / sourceSpanFraction") ||
        !contains(waveform, "sourceClipRect: root.sourceStartFraction <= 0.0001") ||
        !contains(waveform, "Qt.rect(root.sourceStartFraction * root.resolvedSourcePixelWidth") ||
        !contains(waveform, "source: root.viewportActive ? root.resolvedSource : \"\"") ||
        !contains(waveform, "visible: String(source).length > 0") ||
        !contains(waveform, "root.generationState === \"ready\"") ||
        !contains(waveform, "waveformImage.status !== Image.Ready") ||
        !contains(waveform, "cache: true") ||
        !contains(timeline_view, "objectName: \"timelineAdaptiveWaveform\"") ||
        !contains(timeline_view, "compactSource: String(modelData.waveformSourceCompact || \"\")") ||
        !contains(timeline_view, "detailSource: String(modelData.waveformSourceDetail || \"\")") ||
        !contains(timeline_view, "trimStartSeconds: Number(modelData.trimStartSeconds || 0)") ||
        !contains(timeline_view, "clipBlock.x + clipBlock.width >= timelineViewport.contentX - 80") ||
        !contains(cmake, "qml/components/TimelineWaveform.qml")) {
      return fail("revision-cached adaptive waveform tiers, trim-accurate cropping, and lazy viewport loading are missing");
    }
    if (!contains(timeline_view, "timelineDocument.setLinkedEditOverride((mouse.modifiers & Qt.AltModifier) !== 0)") ||
        !contains(timeline_view, "text: clipMenu.clipData.audioLinked ? \"Unlink Audio\" : \"Relink Audio\"") ||
        !contains(timeline_view, "? \"Duplicate Selected Clips\"") ||
        !contains(timeline_view, "sequences: [\"Ctrl+D\", \"Meta+D\"]") ||
        !contains(timeline_view, "clipMenu.clipData.audioLinked")) {
      return fail("linked A/V override, link-state, duplicate, and quiet badge controls are missing");
    }
    if (!contains(timeline_view, "timeline-trim-preview:v1|viewer:two-up-outgoing-incoming|frames:source-timecode-exact|throttle:24ms|restore:on-release-cancel") ||
        !contains(timeline_view, "function beginTrimFeedback(mode, edge, clipId, baselineSeconds)") ||
        !contains(timeline_view, "now - lastTrimPreviewUpdateTime < 24") ||
        !contains(timeline_view, "objectName: \"timelineProgramTrimPreview\"") ||
        !contains(timeline_view, "objectName: \"timelineTrimOutgoingFrame\"") ||
        !contains(timeline_view, "objectName: \"timelineTrimIncomingFrame\"") ||
        !contains(timeline_view, "Number(baseClip.trimEndSeconds || 0) + delta - frameDuration") ||
        !contains(timeline_view, "Number(baseClip.trimStartSeconds || 0) + delta") ||
        !contains(timeline_view, "onCanceled: root.cancelTrimFeedback()") ||
        !contains(timeline_view, "sequence: \"Escape\"")) {
      return fail("live two-up trim preview, exact frame mapping, throttling, and cancel restoration are missing");
    }
    if (!contains(timeline_view,
                  "timeline-keyframe-motion:v2|parameters:transform-opacity-gain-effects-shared-grade-controls|"
                  "interpolation:hold-linear-ease-bezier|lanes:inline-progressive|"
                  "curve-editor:modular|preview:playhead-evaluated|persistence:undo-save-reopen") ||
        !contains(timeline_view, "objectName: \"timelineKeyframeCurveWindow\"") ||
        !contains(timeline_view, "objectName: \"timelineInlineKeyframeLane\"") ||
        !contains(timeline_view, "objectName: \"timelineViewerMotionPath\"") ||
        !contains(timeline_view, "function animatedClipValue(clip, parameter, fallbackValue)") ||
        !contains(timeline_view, "timelineDocument.moveSelectedClipKeyframe(") ||
        !contains(timeline_view, "timelineDocument.setSelectedClipKeyframeValue(") ||
        !contains(timeline_view,
                  "timeline-keyframe-bezier:v2|handles:weighted-time-value-in-out|"
                  "modes:linked-broken|evaluation:cubic-time-solved|"
                  "preview:qml-controller-shared|export:bounded-sampled-expression|"
                  "persistence:undo-save-reopen|ui:modular-curve-editor") ||
        !contains(timeline_view, "objectName: \"timelineBezierInHandle\"") ||
        !contains(timeline_view, "objectName: \"timelineBezierOutHandle\"") ||
        !contains(timeline_view, "objectName: \"timelineBezierHandleLinkButton\"") ||
        !contains(timeline_view, "objectName: \"timelineKeyframeBatchMenuButton\"") ||
        !contains(timeline_view, "function interpolatedKeyframeValue(left, right, progress)") ||
        !contains(timeline_view, "timelineDocument.setSelectedClipKeyframeBezierHandle(") ||
        !contains(timeline_view, "timelineDocument.setSelectedClipKeyframeBezierHandlesLinked(") ||
        !contains(timeline_view,
                  "timeline-advanced-keyframes:v4|graphs:value-speed|selection:modifier-multi|"
                  "handles:weighted-bezier-linked-broken|tracks:automation-shared-curve-editor|"
                  "batch:clip-and-track|clipboard:clip-and-track-selection|timing:fixed-roving-clip-only|"
                  "align:value-first-average-last|distribute:time-value|"
                  "scale:selected-time-value|clipboard:selected-paste-at-playhead|"
                  "preview-export:shared-evaluation|persistence:undo-save-reopen|"
                  "performance:bounded-canvas-sampling|density:modular-window-only") ||
        !contains(timeline_view, "objectName: \"timelineKeyframeValueGraphButton\"") ||
        !contains(timeline_view, "objectName: \"timelineKeyframeSpeedGraphButton\"") ||
        !contains(timeline_view, "objectName: \"timelineKeyframeIncomingSpeedValueField\"") ||
        !contains(timeline_view, "objectName: \"timelineKeyframeOutgoingSpeedValueField\"") ||
        !contains(timeline_view,
                  "timelineDocument.setSelectedClipKeyframeSpeedForSide(") ||
        !contains(timeline_view, "objectName: \"timelineKeyframeSpeedDetailRow\"") ||
        !contains(timeline_view, "function setCurveKeyframeSpeed(seconds, side, value)") ||
        !contains(timeline_view,
                  "objectName: \"timelineTrackAutomationSpeedEditorPopup\"") ||
        !contains(timeline_view,
                  "objectName: \"timelineTrackAutomationIncomingSpeedValueField\"") ||
        !contains(timeline_view,
                  "objectName: \"timelineTrackAutomationOutgoingSpeedValueField\"") ||
        !contains(timeline_view,
                  "objectName: \"timelineTrackAutomationSetKeyframeSpeedAction\"") ||
        !contains(timeline_view,
                  "objectName: \"timelineTrackAutomationToggleBezierHandlesLinkedAction\"") ||
        !contains(timeline_view, "function trackAutomationSpeedUnit(parameter)") ||
        !contains(timeline_view,
                  "timelineDocument.setTrackAutomationKeyframeSpeedForSide(") ||
        !contains(timeline_view,
                  "timelineDocument.setTrackAutomationKeyframeSpeed(") ||
        !contains(timeline_view,
                  "objectName: \"timelineTrackAutomationOpenCurveEditorButton\"") ||
        !contains(timeline_view,
                  "function openTrackAutomationCurveEditor(trackData, parameter)") ||
        !contains(timeline_view,
                  "timelineDocument.setTrackAutomationKeyframeBezierHandle(") ||
        !contains(timeline_view,
                  "objectName: \"timelineTrackAutomationCurveCopyButton\"") ||
        !contains(timeline_view,
                  "objectName: \"timelineTrackAutomationCurvePasteButton\"") ||
        !contains(timeline_view,
                  "timelineDocument.copyTrackAutomationKeyframes(") ||
        !contains(timeline_view,
                  "timelineDocument.pasteTrackAutomationKeyframes(") ||
        !contains(timeline_document_controller,
                  "TimelineDocumentController::setTrackAutomationKeyframeBezierHandle(") ||
        !contains(timeline_document_controller,
                  "TimelineDocumentController::copyTrackAutomationKeyframes(") ||
        !contains(timeline_document_controller,
                  "TimelineDocumentController::pasteTrackAutomationKeyframes(") ||
        !contains(timeline_view,
                  "readonly property bool trackAutomationMode: root.curveEditorTarget === \"track\"") ||
        !contains(timeline_view, "objectName: \"timelineKeyframeRovingButton\"") ||
        !contains(timeline_view, "function curveSpeedAt(localSeconds)") ||
        !contains(timeline_view, "property var selectedSeconds: []") ||
        !contains(timeline_view, "mouse.modifiers & Qt.MetaModifier") ||
        !contains(timeline_view, "timelineDocument.copySelectedClipKeyframeSelection(") ||
        !contains(timeline_view, "timelineDocument.pasteSelectedClipKeyframes(local,") ||
        !contains(timeline_view, "timelineDocument.setSelectedClipKeyframesRoving(") ||
        !contains(timeline_view, "timelineDocument.alignSelectedClipKeyframes(") ||
        !contains(timeline_view, "timelineDocument.distributeSelectedClipKeyframes(") ||
        !contains(timeline_view, "timelineDocument.scaleSelectedClipKeyframeSelection(") ||
        !contains(timeline_view, "for (let sample = 0; sample <= 120; ++sample)") ||
        !contains(timeline_document_controller,
                  "TimelineDocumentController::copySelectedClipKeyframeSelection(") ||
        !contains(timeline_document_controller,
                  "TimelineDocumentController::setSelectedClipKeyframesRoving(") ||
        !contains(timeline_document_controller,
                  "TimelineDocumentController::alignSelectedClipKeyframes(") ||
        !contains(timeline_document_controller,
                  "TimelineDocumentController::distributeSelectedClipKeyframes(") ||
        !contains(timeline_document_controller,
                  "TimelineDocumentController::scaleSelectedClipKeyframeSelection(") ||
        !contains(timeline_document_controller, "reflowRovingClipKeyframes") ||
        !contains(timeline_document_controller,
                  "keyframe_object.insert(QStringLiteral(\"roving\"), keyframe.roving)") ||
        !contains(timeline_view,
                  "timeline-keyframe-lanes:v3|clip:inline-transform-opacity-gain-effects-shared-grade-controls|"
                  "track:inline-opacity-gain-pan|density:closed-by-default-expand-per-track|"
                  "edit:direct-time-value-interpolation-delete|curve:vector-bounded|"
                  "preview-export:shared|undo-save-reopen:true") ||
        !contains(timeline_view, "objectName: \"timelineTrackAutomationLane\"") ||
        !contains(timeline_view, "timelineDocument.updateTrackAutomationKeyframe(") ||
        !contains(timeline_view, "timelineDocument.removeTrackAutomationKeyframe(") ||
        !contains(timeline_view, "timelineDocument.setTrackAutomationKeyframeInterpolation(") ||
        !contains(effects_panel, "component ParameterKeyframeButton: ActionButton") ||
        !contains(effects_panel,
                  "timeline-title-motion:v4|target:title-clips|presets:fade-pop-rise-slide-left-right|"
                  "lifecycle:entrance-and-exit|animation:editable-clip-keyframes|"
                  "text-position:anchor-and-transform-keyframes|"
                  "clear:static-removes-motion-and-transform-keys|"
                  "preview-export:shared-anchor-transform-path|state:undo-save-reopen") ||
        !contains(effects_panel, "function titlePositionValue(axis)") ||
        !contains(effects_panel, "function setTitlePosition(axis, value)") ||
        !contains(effects_panel, "function animatedClipValue(clip, parameter, fallbackValue)") ||
        !contains(effects_panel, "const valueProvider = root.animatedClipValueProvider") ||
        !contains(effects_panel, "if (typeof valueProvider === \"function\")") ||
        !contains(effects_panel,
                  "valueProvider.call(root, clip, parameter, fallbackValue)") ||
        !contains(effects_panel, "const offset = root.animatedClipValue(") ||
        !contains(timeline_view,
                  "animatedClipValueProvider: function(clip, parameter, fallbackValue)") ||
        !contains(effects_panel,
                  "root.documentController.setSelectedTitlePositionX(anchor)") ||
        !contains(effects_panel,
                  "root.documentController.setSelectedTitlePositionY(anchor)") ||
        !contains(effects_panel, "parameterName: \"titlePositionX\"") ||
        !contains(effects_panel, "parameterName: \"titlePositionY\"") ||
        !contains(timeline_view, "objectName: \"timelineProgramTitleDragArea\"") ||
        !contains(timeline_view, "timelineDocument.beginContinuousEdit()") ||
        !contains(timeline_view, "timelineDocument.setSelectedTitlePositionX(") ||
        !contains(timeline_view, "timelineDocument.setSelectedTitlePositionY(") ||
        !contains(effects_panel, "objectName: \"timelineTitlePositionXSlider\"") ||
        !contains(effects_panel, "objectName: \"timelineTitlePositionYSlider\"") ||
        !contains(effects_panel, "objectName: \"timelineTitlePositionXKeyframeButton\"") ||
        !contains(effects_panel, "objectName: \"timelineTitlePositionYKeyframeButton\"") ||
        !contains(effects_panel, "from: 0.05") ||
        !contains(effects_panel, "to: 4") ||
        !contains(timeline_view,
                  "if (parameter === \"frameScale\") return ({\"minimum\": 0.05, \"maximum\": 4})") ||
        !contains(effects_panel, "parameterName: \"frameScale\"") ||
        !contains(effects_panel, "parameterName: \"frameOffsetX\"") ||
        !contains(effects_panel, "parameterName: \"frameOffsetY\"") ||
        !contains(effects_panel, "parameterName: \"frameRotationDegrees\"") ||
        !contains(effects_panel, "parameterName: \"opacity\"") ||
        !contains(effects_panel, "parameterName: \"audioGainDb\"") ||
        !contains(effects_panel, "parameterName: \"effect:\" + effectName") ||
        !contains(effects_panel, "[\"Hold\", \"Linear\", \"Ease In\", \"Ease Out\", \"Ease In Out\", \"Bezier\"]")) {
      return fail("persistent keyframe controls, inline lanes, interpolation, and modular curve editing are missing");
    }
    if (!contains(timeline_view,
                  "timeline-adaptive-playback:v4|modes:auto-full-half-quarter|"
                  "load:timing-buffer-stall-measured|"
                  "auto:cache-then-proxy-plus-half-quarter-detail|"
                  "hysteresis:three-slow-twelve-healthy|paused:requested-full-restored|"
                  "disclosure:contextual-exact-scale-and-reason|"
                  "half-quarter:real-program-texture-downsample|layers:video-audio|"
                  "export:original-only|setting:persistent") ||
        !contains(timeline_view, "function programPlaybackSourceUrl(clip, stabilizedVisual)") ||
        !contains(timeline_view, "property string previewQualityMode: \"auto\"") ||
        !contains(timeline_view, "key: \"quality-auto\"") ||
        !contains(timeline_view, "key: \"quality-full\"") ||
        !contains(timeline_view, "key: \"quality-half\"") ||
        !contains(timeline_view, "key: \"quality-quarter\"") ||
        !contains(timeline_view, "* root.previewResolutionScale") ||
        !contains(timeline_view, "timelinePlayback.previewQualityMode = previewQualityMode") ||
        !contains(timeline_view, "timelinePlayback.interactionActive = previewInteractionActive") ||
        !contains(timeline_view,
                  "previewResolutionScale: timelinePlayback.effectivePreviewScale") ||
        !contains(timeline_view, "objectName: \"timelinePlaybackHealthChip\"") ||
        !contains(timeline_view, "timelinePlayback.adaptiveDetailLevel === \"quarter\"") ||
        !contains(timeline_view, "timelinePlayback.adaptiveQualityReason") ||
        !contains(timeline_view, "objectName: \"timelinePlaybackResolutionChip\"") ||
        !contains(timeline_view, "Export always renders at the delivery resolution") ||
        !contains(timeline_view, "timelinePlayback.adaptiveQualityActive") ||
        !contains(timeline_view, "timelinePlayback.effectivePreviewQuality === \"render-cache\"") ||
        !contains(playback_controller,
                  "void TimelinePlaybackController::applyAdaptivePlaybackPressure(") ||
        !contains(playback_controller,
                  "void TimelinePlaybackController::recordAdaptiveHealthySample()") ||
        !contains(playback_controller, "adaptive_healthy_sample_count_ < 12") ||
        !contains(playback_controller, "resetAdaptivePlaybackDetail();")) {
      return fail("adaptive quality policy, contextual health feedback, and low-density View controls are missing");
    }
    if (!contains(timeline_view,
                  "timeline-review-ranges:v1|saved:frame-snapped-name-in-out-preroll-postroll|"
                  "commands:play-in-to-out-loop-review-from-edit|"
                  "transport:exact-boundary-stop-or-wrap|"
                  "state:create-edit-delete-undo-redo-save-reopen|"
                  "ui:view-menu-modular-contextual-active") ||
        !contains(timeline_view, "key: \"review-ranges\"") ||
        !contains(timeline_view, "objectName: \"timelineReviewRangesPopup\"") ||
        !contains(timeline_view, "objectName: \"timelinePlayReviewRangeButton\"") ||
        !contains(timeline_view, "objectName: \"timelineLoopReviewRangeButton\"") ||
        !contains(timeline_view, "objectName: \"timelinePlayInToOutButton\"") ||
        !contains(timeline_view, "objectName: \"timelineReviewFromEditPointButton\"") ||
        !contains(timeline_view, "objectName: \"timelineSaveReviewRangeButton\"") ||
        !contains(timeline_view, "objectName: \"timelineReviewRangePlaybackChip\"") ||
        !contains(timeline_document_controller,
                  "QString TimelineDocumentController::createReviewRange(") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::updateReviewRange(") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::deleteReviewRange(") ||
        !contains(playback_controller,
                  "bool TimelinePlaybackController::playReviewRange(") ||
        !contains(playback_controller,
                  "bool TimelinePlaybackController::playInToOut(") ||
        !contains(playback_controller,
                  "bool TimelinePlaybackController::reviewFromEditPoint(") ||
        !contains(playback_controller, "target_seconds >= loop_end_seconds_")) {
      return fail("saved review ranges need exact persistent In/Out and handle state plus real one-shot, loop, and review-from-edit playback boundaries in one modular window");
    }
    if (!contains(timeline_view, "property string keyboardTrimEdge: \"\"") ||
        !contains(timeline_view, "function toggleKeyboardTrimEdge()") ||
        !contains(timeline_view, "function nudgeKeyboardTrim(frameDelta, asymmetricLinkedAv)") ||
        !contains(timeline_view, "timelineDocument.nudgeSelectedTrimEdge(") ||
        !contains(timeline_view, "sequence: \"T\"") ||
        !contains(timeline_view, "sequence: \"Shift+T\"") ||
        !contains(timeline_view, "sequence: \"Shift+,\"") ||
        !contains(timeline_view, "sequence: \"Shift+.\"") ||
        !contains(timeline_view, "sequence: \"Alt+,\"") ||
        !contains(timeline_view, "sequence: \"Alt+.\"") ||
        !contains(timeline_view, "objectName: \"timelineKeyboardTrimEdgeChip\"")) {
      return fail("keyboard trim-side selection, one/five-frame nudging, and asymmetric linked A/V controls are missing");
    }
    if (!contains(timeline_view,
                  "timeline-edit-point-selection:v1|mode:t-selects-incoming-outgoing|"
                  "navigation:brackets-previous-next|boundary:frame-exact-timecode|"
                  "context:outgoing-incoming-source-in-out|limits:head-tail-handles|"
                  "transition-room:minimum-shared-handle|state:selection-only-no-undo|"
                  "ui:contextual-chip-modular-details") ||
        !contains(timeline_view, "objectName: \"timelineEditPointSelectionChip\"") ||
        !contains(timeline_view, "objectName: \"timelineEditPointPopup\"") ||
        !contains(timeline_view, "objectName: \"timelinePreviousEditPointButton\"") ||
        !contains(timeline_view, "objectName: \"timelineNextEditPointButton\"") ||
        !contains(timeline_view, "sequence: \"[\"") ||
        !contains(timeline_view, "sequence: \"]\"") ||
        !contains(timeline_view, "timelineDocument.selectedEditPointInfo(") ||
        !contains(timeline_view, "timelineDocument.selectAdjacentEditPoint(") ||
        !contains(timeline_view, "timelineDocument.selectOtherEditPointSide(") ||
        !contains(timeline_document_controller, "timeline-edit-point:v1") ||
        !contains(timeline_document_controller,
                  "QVariantMap TimelineDocumentController::selectedEditPointInfo(")) {
      return fail("edit-point mode needs exact boundary, two-sided source handles, transition room, navigation, and contextual disclosure");
    }
    if (!contains(timeline_view,
                  "timeline-advanced-selection:v1|direction:forward-backward-all-or-track|"
                  "similar:track-media-type-color-label-effects-group-source|"
                  "constraints:composable-available-only|selection:controller-owned-no-undo|"
                  "ui:edit-menu-single-modular-popup") ||
        !contains(timeline_view, "key: \"select-forward\"") ||
        !contains(timeline_view, "key: \"select-forward-track\"") ||
        !contains(timeline_view, "key: \"select-backward\"") ||
        !contains(timeline_view, "key: \"select-backward-track\"") ||
        !contains(timeline_view, "key: \"select-similar\"") ||
        !contains(timeline_view, "objectName: \"timelineSelectSimilarPopup\"") ||
        !contains(timeline_view, "objectName: \"timelineSelectSimilarConstraintFlow\"") ||
        !contains(timeline_view, "objectName: \"timelineApplySelectSimilarButton\"") ||
        !contains(timeline_view, "timelineDocument.selectClipsForward(false)") ||
        !contains(timeline_view, "timelineDocument.selectClipsBackward(true)") ||
        !contains(timeline_view, "timelineDocument.selectedClipSimilarityOptions()") ||
        !contains(timeline_view, "timelineDocument.selectSimilarClips(root.similarSelectionConstraints)") ||
        !contains(timeline_document_controller,
                  "int TimelineDocumentController::selectClipsForward(bool selected_track_only)") ||
        !contains(timeline_document_controller,
                  "int TimelineDocumentController::selectSimilarClips(const QStringList& constraints)")) {
      return fail("advanced selection needs directional scope plus a composable six-property Select Similar surface");
    }
    if (!contains(timeline_view,
                  "timeline-paste-attributes:v1|source:first-copied-clip-explicit|"
                  "targets:multi-selection|"
                  "categories:effects-motion-crop-speed-audio-transitions-keyframes-independent|"
                  "comparison:source-destination-change-state|speed:ripple-safe|"
                  "keyframes:duration-scaled|apply:one-undo|persistence:save-reopen|"
                  "ui:edit-menu-modular-sheet") ||
        !contains(timeline_view, "key: \"paste-attributes\"") ||
        !contains(timeline_view, "objectName: \"timelinePasteAttributesPopup\"") ||
        !contains(timeline_view, "objectName: \"timelinePasteAttributesComparisonSheet\"") ||
        !contains(timeline_view, "objectName: \"timelinePasteAttributeCategory-\" + pasteAttributeRow.categoryKey") ||
        !contains(timeline_view, "objectName: \"timelineApplyPasteAttributesButton\"") ||
        !contains(timeline_view, "timelineDocument.pasteAttributesComparison()") ||
        !contains(timeline_view,
                  "timelineDocument.pasteAttributesToSelectedClips(") ||
        !contains(timeline_document_controller,
                  "QVariantMap TimelineDocumentController::pasteAttributesComparison() const") ||
        !contains(timeline_document_controller,
                  "int TimelineDocumentController::pasteAttributesToSelectedClips(")) {
      return fail("Paste Attributes needs an independent seven-category comparison and atomic multi-target apply sheet");
    }
    if (!contains(timeline_view,
                  "timeline-gap-index:v1|detect:internal-frame-tolerant-track-and-sequence|"
                  "filter:min-max-duration|navigation:previous-next-wrap|"
                  "preview:midpoint-nonmutating|close:selected-or-all-one-undo|"
                  "safety:locked-cross-track-group-and-stale-id-blocked|"
                  "scope:track-local-or-sequence-wide|markers:sequence-gaps-only|"
                  "persistence:save-reopen|ui:edit-menu-modular-index") ||
        !contains(timeline_view, "key: \"gap-index\"") ||
        !contains(timeline_view, "objectName: \"timelineGapIndexPopup\"") ||
        !contains(timeline_view, "objectName: \"timelineGapDurationFilters\"") ||
        !contains(timeline_view, "objectName: \"timelineGapIndexList\"") ||
        !contains(timeline_view, "objectName: \"timelinePreviousGapButton\"") ||
        !contains(timeline_view, "objectName: \"timelineNextGapButton\"") ||
        !contains(timeline_view, "objectName: \"timelineCloseSelectedGapsButton\"") ||
        !contains(timeline_view, "objectName: \"timelineCloseAllGapsButton\"") ||
        !contains(timeline_view, "timelineDocument.timelineGaps(") ||
        !contains(timeline_view, "timelineDocument.previewTimelineGap(") ||
        !contains(timeline_view, "timelineDocument.navigateTimelineGap(") ||
        !contains(timeline_view, "timelineDocument.closeTimelineGaps(") ||
        !contains(timeline_view, "timelineDocument.closeAllTimelineGaps(") ||
        !contains(timeline_document_controller,
                  "QVariantList TimelineDocumentController::timelineGaps(") ||
        !contains(timeline_document_controller,
                  "int TimelineDocumentController::closeTimelineGaps(")) {
      return fail("Gap Index needs duration filtering, exact preview/navigation, and guarded selected/all close commands");
    }
    if (!contains(timeline_view, "objectName: \"timelineTimecodeEntryPopup\"") ||
        !contains(timeline_view, "objectName: \"timelineTimecodeOperationCombo\"") ||
        !contains(timeline_view, "objectName: \"timelineTimecodeEntryField\"") ||
        !contains(timeline_view, "[\"seek\", \"place\", \"trim-start\", \"trim-end\", \"duration\", \"marker\"]") ||
        !contains(timeline_view, "timelineDocument.applyTimecodeEntry(timecodeOperation, timecodeDraft)") ||
        !contains(timeline_view, "placeholderText: \"00:00:00:00 or +00:00:01:00\"") ||
        !contains(timeline_view, "sequences: [\"Ctrl+G\", \"Meta+G\"]") ||
        !contains(timeline_view, "onTapped: root.openTimecodeEntry(\"seek\", parent)")) {
      return fail("direct absolute and relative timecode entry is not available from the Program readout and keyboard");
    }
    if (!contains(timeline_view,
                  "timeline-media-analysis:v1|metadata:duration-audio-size|poster:background|"
                  "waveform:background|queues:serial-bounded|cache:source-revision|"
                  "interaction:immediate-provisional|state:quiet-contextual|reopen:resumable") ||
        !contains(timeline_view, "objectName: \"timelineClipAnalyzingState\"") ||
        !contains(timeline_view, "modelData.mediaAnalysisStatus === \"analyzing\"") ||
        !contains(source_panel, "objectName: \"timelineSourceAnalyzingState\"") ||
        !contains(source_panel, "enabled: root.sourceEditReady") ||
        !contains(source_panel, "Source duration and audio are still being analyzed.")) {
      return fail("nonblocking media analysis and quiet provisional source/clip states are missing");
    }
    if (!contains(timeline_view,
                  "timeline-transition-truth:v1|enabled:cross-dissolve|timing:controller-owned|"
                  "preview:dual-source-linear-alpha|export:dual-source-linear-alpha|"
                  "handles:incoming-preroll|pairing:adjacent-same-track|undo-save-reopen:true|"
                  "proof:start-mid-end-rendered-frames") ||
        !contains(timeline_view, "readonly property var programTransitionStates:") ||
        !contains(timeline_view, "timelineDocument.transitionPreviewStatesAt(sequencePlayheadSeconds)") ||
        !contains(timeline_view, "function programTransitionStateForClip(layer)") ||
        !contains(timeline_view, "return baseOpacity * Number(transition.incomingAlpha || 0)") ||
        !contains(timeline_view, "opacity: root.programLayerOpacity(programClip)")) {
      return fail("controller-owned Cross Dissolve timing and shared Program layer compositing are missing");
    }
    if (!contains(timeline_view,
                  "timeline-transform-truth:v1|frame-modes:fit-fill-stretch|"
                  "order:frame-scale-rotate-translate-clip-effects-opacity|"
                  "position:positive-right-down-quarter-frame|rotation:center-transparent|"
                  "preview:program-layer-shared|export:ffmpeg-shared|blend:unsupported-hidden|"
                  "proof:fit-fill-position-rotation-opacity-pixels") ||
        !contains(timeline_view, "function programVideoFillModeFor(frameMode)") ||
        !contains(timeline_view, "function programImageFillModeFor(frameMode)") ||
        !contains(timeline_view, "programBackgroundLayer.modelData.baseFrameScale") ||
        !contains(timeline_view, "programBackgroundLayer.modelData.baseFrameRotationDegrees") ||
        !contains(timeline_view, "programBackgroundLayer.modelData.baseFrameOffsetX || 0) * width * 0.25") ||
        !contains(timeline_view, "programBackgroundLayer.modelData.baseFrameOffsetY || 0) * height * 0.25") ||
        !contains(timeline_view, "opacity: root.programLayerOpacity(programClip)")) {
      return fail("shared Program framing, transform direction, rotation, and opacity mapping are missing");
    }
    if (!contains(timeline_view,
                  "timeline-adjustment-clips:v1|create:top-eligible-lane-at-playhead|"
                  "scope:picture-tracks-below-only|effects:existing-stack-keyframes-lut|"
                  "program:shared-exact-render-cache-auto-background|"
                  "export:ordered-composite-filter|opacity:blendable-keyframeable|"
                  "persistence:undo-save-reopen|ui:effects-panel-contextual|"
                  "density:default-unchanged") ||
        !contains(timeline_view, "key: \"add-adjustment\"") ||
        !contains(timeline_view, "timelineDocument.addAdjustmentClip(5.0)") ||
        !contains(timeline_view, "function programAdjustmentLayers()") ||
        !contains(timeline_view, "function hasAnyAdjustmentClips()") ||
        !contains(timeline_view, "objectName: \"timelineAdjustmentPreviewCacheTimer\"") ||
        !contains(timeline_view, "objectName: \"timelineAdjustmentPreviewStatus\"") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::addAdjustmentClip(") ||
        !contains(timeline_document_controller,
                  "clip.media_type = QStringLiteral(\"adjustment\")") ||
        !contains(timeline_document_controller,
                  "clip->media_type == QStringLiteral(\"adjustment\")") ||
        !contains(timeline_document_controller, "[%1]split=2[%2][%3]") ||
        !contains(timeline_document_controller,
                  "blend=all_expr='A*(1-(%3))+B*(%3)'")) {
      return fail("adjustment clips need creation, contextual effects, lower-track compositing, exact Program cache, and export parity");
    }
    if (!contains(timeline_view,
                  "timeline-scale-proof:v1|fixture:100-authored-tracks-10000-clips-four-hours|"
                  "density:2400-markers-200-captions-2450-effected-9799-offline|"
                  "projection:single-linear-context-under-1500ms|"
                  "interaction:seek-select-under-100ms-p95|edit-undo:under-250ms-p95|"
                  "memory:projection-512mib-growth-repeated-128mib-retention-total-640mib|"
                  "undo:80000-clip-entry-budget-minimum-eight|"
                  "audio:known-silent-not-duplicated|minimap:single-bounded-canvas|"
                  "background:hydration-remains-live") ||
        !contains(timeline_view, "objectName: \"timelineMinimapCanvas\"") ||
        !contains(timeline_view, "renderStrategy: Canvas.Cooperative") ||
        !contains(timeline_document_controller,
                  "TimelineDocumentController::ClipMapContext TimelineDocumentController::clipMapContext(") ||
        !contains(timeline_document_controller,
                  "context.flattened_index_by_clip_id.value(clip.clip_id, -1)") ||
        !contains(timeline_document_controller,
                  "(!clip.has_audio && !clip.audio_presence_provisional)") ||
        !contains(timeline_document_controller, "kMaxUndoSnapshotClipEntries / project_clip_count") ||
        !contains(timeline_recovery_test, "constexpr auto kStressTrackCount = 100") ||
        !contains(timeline_recovery_test, "constexpr auto kStressDurationSeconds = 4.0 * 60.0 * 60.0") ||
        !contains(timeline_recovery_test, "constexpr auto kStressMarkerCount = 2400") ||
        !contains(timeline_recovery_test, "projection_ms > 1500") ||
        !contains(timeline_recovery_test, "seek_p95_ms > 100") ||
        !contains(timeline_recovery_test, "retained_undo_depth != 8") ||
        !contains(timeline_recovery_test, "640LL * kMebibyte")) {
      return fail("the 10,000-clip scale gate needs linear projection, bounded minimap and undo state, explicit latency and RSS ceilings, and background-load interaction proof");
    }
    if (!contains(timeline_view,
                  "timeline-audio-channel-mapping:v1|source:count-layout-analyzed|"
                  "modes:source-mono-left-mono-right-dual-mono-stereo-downmix-stereo|"
                  "operations:swap-invert-left-invert-right|"
                  "preview:decoded-buffer-before-processing|"
                  "export:ordered-pan-standard-multichannel-downmix|"
                  "state:one-step-undo-save-reopen|ui:focused-mixer-window|"
                  "density:default-unchanged") ||
        !contains(audio_mixer,
                  "timeline-audio-channel-window:v1|source-metadata:count-layout|"
                  "routing:six-modes|phase:left-right|swap:stereo|"
                  "preview:live-diagram-and-program|reset:source|surface:focused-modal") ||
        !contains(audio_mixer, "objectName: \"timelineAudioChannelMappingButton\"") ||
        !contains(audio_mixer, "objectName: \"timelineAudioChannelMappingWindow\"") ||
        !contains(audio_mixer, "objectName: \"timelineAudioChannelModeCombo\"") ||
        !contains(audio_mixer, "objectName: \"timelineAudioChannelPreviewCard\"") ||
        !contains(audio_mixer, "objectName: \"timelineAudioSwapChannelsToggle\"") ||
        !contains(audio_mixer, "objectName: \"timelineAudioInvertLeftToggle\"") ||
        !contains(audio_mixer, "objectName: \"timelineAudioInvertRightToggle\"") ||
        !contains(audio_mixer, "timelineDocument.setSelectedClipAudioChannelMapping(") ||
        !contains(timeline_view,
                  "channelMode: String(programAudioLayer.modelData.audioChannelMode || \"source\")") ||
        !contains(timeline_view,
                  "swapChannels: programAudioLayer.modelData.audioSwapChannels === true") ||
        !contains(timeline_view,
                  "invertLeftPhase: programAudioLayer.modelData.audioInvertLeftPhase === true") ||
        !contains(timeline_view,
                  "invertRightPhase: programAudioLayer.modelData.audioInvertRightPhase === true") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::setSelectedClipAudioChannelMapping(") ||
        !contains(timeline_document_controller,
                  "format=duration:format_tags=Gamma for Look,Color for Look:") ||
        !contains(timeline_document_controller,
                  "color_transfer,color_primaries,channels,channel_layout:") ||
        !contains(timeline_document_controller,
                  "clip_object.insert(QStringLiteral(\"audioChannelMode\"), clip.audio_channel_mode)") ||
        !contains(timeline_document_controller,
                  "clip->audio_channel_mode == QStringLiteral(\"mono-left\")") ||
        !contains(timeline_document_controller,
                  "QStringLiteral(\"aformat=sample_rates=%1:channel_layouts=stereo\")") ||
        !contains(audio_output, "channel_mode == QStringLiteral(\"mono-left\")") ||
        !contains(audio_output, "if (swap_channels)") ||
        !contains(audio_output, "if (invert_left_phase)") ||
        !contains(audio_output, "if (invert_right_phase)")) {
      return fail("professional source-channel mapping needs analyzed metadata, six modes, live preview, export parity, persistence, and a focused mixer window");
    }
    if (!contains(timeline_view,
                  "timeline-audio-mixer:v6|window:focused-modular|"
                  "strips:selected-clip-tracks-role-groups-submixes-master|"
                  "roles:dialogue-music-ambience-effects-voiceover-commentary|"
                  "role-routing:dialogue-music-sfx|role-batch:mute-solo-gain-route|"
                  "processors:eq-compressor-limiter-gate-deesser-highpass-lowpass|"
                  "processor-automation:track-keyframes|bypass:loudness-matched|"
                  "ducking:role-generated-editable-regenerable-manual-preserved|"
                  "controls:mute-solo-pan-gain-fades-inserts|"
                  "processing:preview-export-shared|"
                  "state:undo-save-reopen|density:default-unchanged") ||
        !contains(timeline_view,
                  "timeline-audio-meters:v1|track-and-master:peak-truepeak-rms-lufs|"
                  "truepeak:four-times-cubic-oversample|overload:two-second-hold-plus-latch|"
                  "reset:all-stages|scale:0-minus6-minus12-minus24-minus48|"
                  "reduced-motion:static-values|ui:mixer-window") ||
        !contains(timeline_view,
                  "timeline-track-automation:v8|video:opacity|"
                  "audio:gain-pan-eq-compressor-limiter-gate-deesser-highpass-lowpass|"
                  "keyframe-tangents:weighted-bezier-in-out-linked-broken|"
                  "curve:shared-modular-value-speed-editor|"
                  "clipboard:copy-paste-full-or-selected|bulk-align-distribute-scale:multi-key-undoable|"
                  "ui:context-mixer-processor-window-inline-lanes|"
                  "edit:add-drag-time-value-interpolation-delete|"
                  "preview:decoded-buffer-playhead-evaluated|"
                  "export:continuous-gain-pan-keyframe-segment-processors|"
                  "meters:stereo-peak-clip|persistence:undo-save-reopen") ||
        !contains(timeline_view, "function programAudioPan(layer)") ||
        !contains(timeline_view, "TimelineAudioOutput {") ||
        !contains(timeline_view, "objectName: \"timelineAudioMixerPopup\"") ||
        !contains(timeline_view, "key: \"audio-mixer\"") ||
        !contains(audio_mixer, "objectName: \"timelineSelectedClipMixerStrip\"") ||
        !contains(audio_mixer, "objectName: \"timelineMasterMixerStrip\"") ||
        !contains(audio_mixer, "objectName: \"timelineResetAudioMetersButton\"") ||
        !contains(audio_mixer, "objectName: \"timelineAudioMeterReducedMotionToggle\"") ||
        !contains(audio_mixer, "objectName: \"timelineAudioRoleBatchRail\"") ||
        !contains(audio_mixer, "objectName: \"timelineTrackAudioRoleCombo\"") ||
        !contains(audio_mixer, "objectName: \"timelineAudioProcessorWindow\"") ||
        !contains(audio_mixer, "objectName: \"timelineAudioMatchedBypassBar\"") ||
        !contains(audio_mixer, "objectName: \"timelineAudioLoudnessMatchedBypassToggle\"") ||
        !contains(audio_mixer, "objectName: \"timelineAudioProcessingBypassButton\"") ||
        !contains(audio_mixer, "objectName: \"timelineAudioProcessorKeyframeButton\"") ||
        !contains(audio_mixer, "objectName: \"timelineDialogueDuckingMenu\"") ||
        !contains(audio_mixer, "generateDialogueDuckingAutomation(amount, 0.12, 0.35)") ||
        !contains(audio_mixer, "\"audioLimiterCeilingDb\"") ||
        !contains(audio_mixer, "\"audioGateThresholdDb\"") ||
        !contains(audio_mixer, "\"audioDeEsserAmount\"") ||
        !contains(audio_mixer, "\"audioHighPassHz\"") ||
        !contains(audio_mixer, "\"audioLowPassHz\"") ||
        !contains(audio_mixer,
                  "[\"Dialogue\", \"Music\", \"Ambience\", \"Effects\", \"Voiceover\", \"Commentary\"]") ||
        !contains(audio_mixer, "root.timelineDocument.setAudioRoleSetting(") ||
        !contains(audio_mixer, "\"audioRole\", model[index]") ||
        !contains(audio_mixer, "root.timelineDocument.toggleTrackMuted(") ||
        !contains(audio_mixer, "root.timelineDocument.toggleTrackSolo(") ||
        !contains(audio_mixer, "text: busStrip.modelData + \" Submix\"") ||
        !contains(timeline_view,
                  "timeline-external-audio-sync:v1|selection:one-video-one-standalone-audio|") ||
        !contains(audio_mixer, "objectName: \"timelineExternalAudioSyncButton\"") ||
        !contains(timeline_view, "objectName: \"timelineExternalAudioSyncPopup\"") ||
        !contains(timeline_view, "objectName: \"timelineExternalAudioSyncPreviewCard\"") ||
        !contains(timeline_view, "objectName: \"timelineExternalAudioSyncSampleResult\"") ||
        !contains(timeline_view, "timelineDocument.previewSelectedExternalAudioSync(") ||
        !contains(timeline_view, "timelineDocument.applySelectedExternalAudioSync(") ||
        !contains(audio_mixer,
                  "tooltipText: \"Preview timecode, waveform, or manual synchronization before moving external audio.\"") ||
        !contains(timeline_document_controller, "Broadcast Wave stores its source timecode") ||
        !contains(timeline_document_controller, "waveform_alignment(") ||
        !contains(timeline_document_controller, "timeline-external-audio-sync:v1") ||
        !contains(timeline_document_controller,
                  "QVariantMap TimelineDocumentController::audioRoleSummary(") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::setAudioRoleSetting(") ||
        !contains(timeline_document_controller, "default_bus_for_audio_role(") ||
        !contains(timeline_document_controller, "is_audio_processor_automation_parameter(") ||
        !contains(timeline_document_controller,
                  "QVariantMap TimelineDocumentController::generateDialogueDuckingAutomation(") ||
        !contains(timeline_document_controller, "dialogue-ducking:v1") ||
        !contains(timeline_document_controller, "track->audio_processing_bypassed") ||
        !contains(timeline_document_controller, "track->audio_loudness_matched_bypass") ||
        !contains(timeline_document_controller, "highpass=f=%1") ||
        !contains(timeline_document_controller, "lowpass=f=%1") ||
        !contains(timeline_document_controller, "deesser=i=%1") ||
        !contains(timeline_document_controller, "agate=threshold=%1") ||
        !contains(timeline_document_controller, "alimiter=limit=%1") ||
        !contains(timeline_document_controller, "qRound64(") ||
        !contains(audio_meter, "model: [0, -6, -12, -24, -48]") ||
        !contains(audio_meter, "NumberAnimation { duration: 70") ||
        !contains(audio_meter, "enabled: root && !root.reducedMotion") ||
        !contains(audio_output, "double* true_peak_left") ||
        !contains(audio_output, "publish_true_peak_segment") ||
        !contains(audio_output, "void TimelineAudioOutput::resetMeters()") ||
        !contains(timeline_view, "layerAudioOutput.attachPlayer(audioPlayer)") ||
        !contains(timeline_view, "root.publishTrackMeter(") ||
        !contains(timeline_view, "meterVisible: root.sequencePlaying") ||
        !contains(track_header,
                  "timeline-track-mixer:v3|popup:gain-pan-processing|automation:inline-lanes-contextual|"
                  "routing:main-dialogue-music-sfx|"
                  "processing:progressive-three-band-eq-compressor-ducking|"
                  "meters:contextual-stereo-peak-clip|default-density:unchanged|controls:shared") ||
        !contains(track_header, "objectName: \"timelineTrackStereoMeter\"") ||
        !contains(track_header, "label: \"Pan\"") ||
        !contains(track_header, "model: [\"Main\", \"Dialogue\", \"Music\", \"SFX\"]") ||
        !contains(track_header, "label: \"Low · 120 Hz\"") ||
        !contains(track_header, "\"audioCompressorEnabled\"") ||
        !contains(track_header, "\"audioDuckingEnabled\"") ||
        !contains(track_header, "trackData.trackIndex, \"audioPan\", nextValue") ||
        !contains(track_header, "root.hasAutomationKeyAtPlayhead(\"audioPan\")")) {
      return fail("persistent buses, track processing, shared decoded-buffer preview/export, and contextual stereo meters are missing");
    }
    if (!contains(
            timeline_view,
            "timeline-dialogue-cleanup:v1|tools:voice-isolation-noise-hum-dereverb|"
            "preview:transient-original-cleaned-ab|apply:atomic-undo|"
            "artifacts:smoothing-ambience|hum:50-60|"
            "processing:program-export-shared|state:save-reopen|"
            "surface:focused-progressive") ||
        !contains(audio_mixer,
                  "dialogue-cleanup:voice-isolation-noise-hum-dereverb|") ||
        !contains(audio_mixer, "objectName: \"timelineDialogueCleanupWindow\"") ||
        !contains(audio_mixer, "objectName: \"timelineDialogueCleanupOriginalButton\"") ||
        !contains(audio_mixer, "objectName: \"timelineDialogueCleanupCleanedButton\"") ||
        !contains(audio_mixer, "objectName: \"timelineDialogueVoiceIsolationControl\"") ||
        !contains(audio_mixer, "objectName: \"timelineDialogueNoiseReductionControl\"") ||
        !contains(audio_mixer, "objectName: \"timelineDialogueDereverbControl\"") ||
        !contains(audio_mixer, "objectName: \"timelineDialogueHumRemovalControl\"") ||
        !contains(audio_mixer, "objectName: \"timelineDialogueCleanupAdvancedButton\"") ||
        !contains(audio_mixer, "objectName: \"timelineDialogueArtifactSmoothingControl\"") ||
        !contains(audio_mixer, "objectName: \"timelineDialogueAmbiencePreservationControl\"") ||
        !contains(audio_mixer, "objectName: \"timelineDialogueCleanupApplyButton\"") ||
        !contains(audio_mixer, "timelineDocument.previewTrackDialogueCleanup(") ||
        !contains(audio_mixer, "timelineDocument.applyTrackDialogueCleanup(") ||
        !contains(audio_mixer, "timelineDocument.clearTrackDialogueCleanupPreview(") ||
        !contains(timeline_view,
                  "dialogueCleanupEnabled: programAudioLayer.modelData.trackDialogueCleanupEnabled === true") ||
        !contains(timeline_view, "voiceIsolationAmount: Number(") ||
        !contains(audio_output, "notch_coefficients") ||
        !contains(audio_output, "cleanup_fast_envelope_") ||
        !contains(audio_output, "const auto isolated_side = side *") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::previewTrackDialogueCleanup(") ||
        !contains(timeline_document_controller,
                  "bool TimelineDocumentController::applyTrackDialogueCleanup(") ||
        !contains(timeline_document_controller,
                  "void TimelineDocumentController::clearTrackDialogueCleanupPreview(") ||
        !contains(timeline_document_controller,
                  "QStringLiteral(\"dialogueCleanupEnabled\"), track.dialogue_cleanup.enabled") ||
        !contains(timeline_document_controller, "bandreject=f=%1:t=q:w=8:m=%2") ||
        !contains(timeline_document_controller, "afftdn=nr=%1:nf=%2:tn=1:gs=%3") ||
        !contains(timeline_document_controller,
                  "dialoguenhance=original=1:enhance=%1:voice=%2") ||
        !contains(timeline_document_test,
                  "Cleaned preview should publish staged settings without mutating project state") ||
        !contains(timeline_document_test,
                  "dialogue cleanup Apply should undo in one history step") ||
        !contains(timeline_document_test,
                  "committed dialogue cleanup recipe") ||
        !contains(timeline_playback_test,
                  "reject 60 Hz hum without erasing speech") ||
        !contains(timeline_playback_test,
                  "suppress stable low-level noise with smoothed gain changes")) {
      return fail("dialogue cleanup needs staged A/B preview, progressive artifact controls, atomic persistence, and shared measured Program/export processing");
    }
    if (!contains(timeline_view,
                  "timeline-render-cache:v2|segments:four-seconds|"
                  "signature:project-identity-render-state-and-source-revision|invalidation:affected-range|"
                  "generation:background-cancellable-bounded|playback:auto-composited|"
                  "fallback:live|visualization:ruler-hairline|export:independent") ||
        !contains(timeline_view, "key: \"cache-toggle\"") ||
        !contains(timeline_view, "key: \"cache-clear\"") ||
        !contains(timeline_view, "objectName: \"timelineRenderCacheRange\"") ||
        !contains(timeline_view, "readonly property bool renderCacheActive:") ||
        !contains(timeline_view, "timelineDocument.generateRenderCache()") ||
        !contains(timeline_view, "timelineDocument.cancelRenderCacheGeneration()")) {
      return fail("bounded range-cache generation, playback fallback, and quiet ruler status are missing");
    }

    const std::vector<std::string> type_tokens = {
        "typeMicro", "typeCaption", "typeLabel", "typeBody", "typeControl",
        "typeSection", "typeTitle", "typeDialogTitle", "typeHero"};
    for (const auto& token : type_tokens) {
      if (!contains(theme, "readonly property int " + token)) {
        return fail("Theme.qml is missing shared type token " + token);
      }
    }
    if (!contains(theme, "readonly property color mediaCanvas") ||
        !contains(theme, "readonly property color editorControlIdle") ||
        !contains(theme, "readonly property color editorControlHover") ||
        !contains(theme, "readonly property color editorSelectionFill") ||
        !contains(theme, "readonly property color editorSelectionEdge") ||
        !contains(theme, "readonly property int hairlineWidth") ||
        !contains(theme, "readonly property int focusBorderWidth")) {
      return fail("semantic media, editorial chrome, hairline, and focus tokens must exist");
    }
    if (!contains(timeline_view,
                  "timeline-dark-accessibility:v1|appearance:dark-only|"
                  "surfaces:ink950-900-850-800-750|outlines:focus-selection-structure-only|") ||
        !contains(theme, "accessibility-theme:v4|appearance:dark-only|scale:0.9-2.0|") ||
        !contains(theme, "semantic-accents:v4|primary:graphite|selection:muted-blue|focus:paper|") ||
        !contains(theme, "dark-contrast:v2|normal-text-target:4.5|control-target:3|") ||
        !contains(theme, "readonly property int typeMicro: scaled(11)") ||
        !contains(theme, "readonly property int typeCaption: scaled(12)") ||
        !contains(theme, "readonly property color textFaint: highContrast ? \"#CDD1D8\" : \"#9299A5\"") ||
        !contains(theme, "readonly property color editorSelectionEdge: selectionAccent") ||
        !contains(theme, "readonly property int radiusControl: scaled(3)") ||
        !contains(theme, "readonly property int radiusPanel: radius4") ||
        !contains(theme, "readonly property int radiusSurface: radius4") ||
        !contains(app_icon, "app-icon:v2|canvas:16-grid|aspect:square-centered|") ||
        !contains(action_button, "minimum-px:10") ||
        !contains(track_header, "objectName: \"timelineTrackHeightResizeHandle-") ||
        !contains(track_header, "Accessible.role: Accessible.Slider") ||
        !contains(track_header, "Keys.onUpPressed: root.resizeRequested(-8)") ||
        !contains(timeline_view, "Accessible.role: Accessible.List") ||
        !contains(timeline_view, "Keys.onLeftPressed: timelineDocument.stepSequenceFrames(-1)") ||
        !contains(timeline_view, "objectName: \"timelineViewportFocusRing\"") ||
        !contains(timeline_view, "visible: timelineViewport.activeFocus || timelineViewport.focus") ||
        !contains(timeline_view, "event.key === Qt.Key_Home") ||
        !contains(timeline_view, "event.key === Qt.Key_End") ||
        !contains(timeline_view, "Accessible.name: \"Timeline overview\"") ||
        !contains(timeline_view, "Keys.onRightPressed: timelineViewport.contentX = Math.min(")) {
      return fail("the dark semantic, readable, scalable, focus-visible, and keyboard-operable Timeline contract is incomplete");
    }
    if (contrast_ratio("#E0E2E7", "#050505") < 4.5 ||
        contrast_ratio("#9C9CAF", "#050505") < 4.5 ||
        contrast_ratio("#9299A5", "#050505") < 4.5 ||
        contrast_ratio("#F4F6F8", "#050505") < 3.0 ||
        contrast_ratio("#F6C945", "#050505") < 4.5 ||
        contrast_ratio("#FF3B30", "#050505") < 4.5) {
      return fail("dark Timeline text, focus, warning, and error tokens must pass their contrast thresholds");
    }
    if (!contains(
            timeline_view,
            "timeline-release-performance:v1|virtualization:media-effects-markers-tracks-clips-ruler|") ||
        !contains(timeline_view, "function virtualizedRulerTicks(interval)") ||
        !contains(timeline_view, "function virtualizedSequenceMarkers()") ||
        !contains(timeline_view, "function virtualizedTrackModel()") ||
        !contains(timeline_view, "function virtualizedClipsForTrack(track)") ||
        !contains(timeline_view, "model: root.virtualizedRulerTicks(interval)") ||
        !contains(timeline_view, "model: root.virtualizedSequenceMarkers()") ||
        !contains(timeline_view, "model: root.timelineVirtualizedTrackRows") ||
        !contains(timeline_view, "model: root.virtualizedClipsForTrack(trackLane.modelData)") ||
        !contains(source_panel, "objectName: \"timelineVirtualizedMediaList\"") ||
        !contains(source_panel, "reuseItems: true") ||
        !contains(effects_panel, "cacheBuffer: Math.max(height, theme.scaled(320))") ||
        !contains(gallery_picker, "objectName: \"timelineVirtualizedGalleryGrid\"") ||
        !contains(runtime_proof, "timeline-release-acceptance:v1") ||
        !contains(runtime_proof, "QStringLiteral(\"pointer-scrub\")") ||
        !contains(runtime_proof, "QStringLiteral(\"clip-drag\")") ||
        !contains(runtime_proof, "QStringLiteral(\"visibleFeedbackBudgetMs\"), 100.0") ||
        !contains(runtime_proof, "QStringLiteral(\"one-clip-portrait\")") ||
        !contains(runtime_proof, "QStringLiteral(\"multitrack-social\")") ||
        !contains(runtime_proof, "renderCurrentTimeline(acceptance_export_path)")) {
      return fail("the bounded, virtualized, latency-gated Timeline release proof is incomplete");
    }

    if (!contains(source_panel, "FilterChip {") ||
        !contains(source_panel, "ToneTextField {") ||
        !contains(effects_panel, "FilterChip {") ||
        !contains(effects_panel, "InspectorSection {") ||
        !contains(effects_panel, "ToneTextArea {") ||
        !contains(timeline_view, "background: PopupSurface {") ||
        !contains(timeline_view, "ToneTextArea {")) {
      return fail("Timeline chrome must use shared tabs, fields, sections, and popup surfaces");
    }
    if (!contains(track_toggle, "implicitWidth: theme.iconButtonSizeCompact") ||
        !contains(track_toggle, "implicitHeight: theme.iconButtonSizeCompact")) {
      return fail("track toggles must keep the shared 32-pixel interaction target");
    }
    if (!contains(cmake, "qml/components/ToneTextArea.qml") ||
        !std::filesystem::exists(components / "ToneTextArea.qml")) {
      return fail("the shared multiline field must be packaged in the QML module");
    }

    if (!contains(timeline_view, "function closeTransientSurfaces()") ||
        !contains(timeline_view, "if (!visible) {\n            adjustmentPreviewCacheTimer.stop()") ||
        !contains(timeline_view, "function restorePopupInvoker(item)") ||
        !contains(timeline_view, "onClosed: {") ||
        !contains(menu_button, "if (!visible)\n            menuPopup.close()") ||
        !contains(app_menu, "if (root.parent && !root.parent.visible)\n                root.close()")) {
      return fail("dialogs, split menus, and context menus must close with their workspace");
    }
    if (!contains(timeline_view, "root.currentClip = timelineDocument.currentClip") ||
        !contains(timeline_view, "timelineDocument: root.documentController") ||
        !contains(timeline_view, "currentClip: root.currentClip")) {
      return fail("the Inspector clip map must refresh when controller selection changes");
    }
    if (contains(track_header, "typeof documentController.toggleTrack") ||
        !contains(track_header, "readonly property bool canToggleLocked: documentController !== null")) {
      return fail("C++ track invokables must remain enabled without JavaScript typeof checks");
    }
    if (!contains(preferences, "onCheckedChanged:") ||
        !contains(preferences, "row.toggled(toggleControl.checked)")) {
      return fail("theme switches must forward direct accessibility state changes");
    }
    if (!contains(
            preferences,
            "readonly property var pageTitles: [\"General\", \"Color\", \"Timeline\", \"Storage\", \"Privacy & Support\", \"Account\"]") ||
        !contains(preferences, "editorPreview.systemMonitorDisplayIdentity") ||
        !contains(preferences, "editorPreview.systemMonitorDisplayTarget.verified") ||
        !contains(preferences, "appShell.setDisplayCalibration(") ||
        !contains(preferences, "appShell.clearDisplayCalibration()") ||
        !contains(preferences, "editorPreview.refreshDisplayTarget()") ||
        !contains(preferences, "objectName: \"displayCalibrationIdentityField\"") ||
        !contains(preferences, "objectName: \"displayPeakLuminanceField\"") ||
        !contains(preferences, "objectName: \"displayBlackLuminanceField\"") ||
        !contains(preferences, "objectName: \"displayReferenceWhiteField\"") ||
        !contains(preferences, "objectName: \"displaySurroundLuminanceField\"") ||
        !contains(preferences, "Reference defaults — not measured")) {
      return fail(
          "color preferences must expose display-bound measured calibration without labeling defaults as verified");
    }

    const std::vector<std::pair<std::string, std::string>> audited_sources = {
        {"TimelineView.qml", timeline_view},
        {"TimelineSourcePanel.qml", source_panel},
        {"TimelineEffectsPanel.qml", effects_panel},
        {"TimelineTrackHeader.qml", track_header},
        {"TimelineTrackToggleButton.qml", track_toggle},
        {"TimelineClipContent.qml", read_text(components / "TimelineClipContent.qml")},
        {"TimelineMediaAssetRow.qml", read_text(components / "TimelineMediaAssetRow.qml")},
        {"TimelineEffectLibraryRow.qml", read_text(components / "TimelineEffectLibraryRow.qml")},
        {"TimelineTransitionLibraryRow.qml", read_text(components / "TimelineTransitionLibraryRow.qml")},
        {"TimelineMarkerCard.qml", read_text(components / "TimelineMarkerCard.qml")},
        {"TimelineMarkerPin.qml", read_text(components / "TimelineMarkerPin.qml")},
    };
    const std::regex raw_font_size(R"(font\.pixelSize\s*:\s*[0-9])");
    const std::regex raw_text_control(R"(\n\s*(TextField|TextArea)\s*\{)");
    const std::vector<std::string> forbidden_colors = {
        "#0b1014", "#4f1713", "#e58767", "#ffd7ca"};
    for (const auto& [name, source] : audited_sources) {
      if (std::regex_search(source, raw_font_size)) {
        return fail(name + " contains an ad hoc chrome font size");
      }
      if ((name == "TimelineView.qml" || name == "TimelineSourcePanel.qml" ||
           name == "TimelineEffectsPanel.qml") &&
          std::regex_search(source, raw_text_control)) {
        return fail(name + " contains a raw text input instead of a shared tone control");
      }
      std::string lower = source;
      for (char& character : lower) {
        if (character >= 'A' && character <= 'Z') {
          character = static_cast<char>(character - 'A' + 'a');
        }
      }
      for (const auto& color : forbidden_colors) {
        if (contains(lower, color)) {
          return fail(name + " contains literal chrome color " + color);
        }
      }
    }

    const std::regex item_pattern(R"((?:^|\n)- \[([ x])\] ([0-9]+)\.)");
    int expected = 1;
    for (auto it = std::sregex_iterator(checklist.begin(), checklist.end(), item_pattern);
         it != std::sregex_iterator(); ++it) {
      const int number = std::stoi((*it)[2].str());
      if (number != expected) {
        return fail("checklist numbering is not sequential at item " + std::to_string(expected));
      }
      if ((*it)[1].str() != "x") {
        return fail("checklist item " + std::to_string(number) + " is not proven complete");
      }
      ++expected;
    }
    if (expected != 101) {
      return fail("the implementation checklist must contain exactly 100 numbered items");
    }
    for (char gate = 'A'; gate <= 'J'; ++gate) {
      if (!contains(checklist, std::string("**Gate ") + gate)) {
        return fail(std::string("checklist is missing Gate ") + gate);
      }
    }

    std::cout << "timeline_ui_consistency_contract: 100 items, 10 gates, shared chrome, "
                 "protected editor geometry, and popup lifecycle verified\n";
    return 0;
  } catch (const std::exception& error) {
    return fail(error.what());
  }
}
