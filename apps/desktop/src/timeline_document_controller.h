#pragma once

#include "customer_edit_access.h"

#include <QJsonObject>
#include <QByteArray>
#include <QHash>
#include <QFutureWatcher>
#include <QObject>
#include <QSize>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <atomic>
#include <memory>

class QProcess;

namespace photo_editor::desktop {

struct TimelineSpeechState;

class TimelineDocumentController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool hasActiveTimeline READ hasActiveTimeline NOTIFY timelineStateChanged)
  Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
  Q_PROPERTY(QVariantList sequenceTracks READ sequenceTracks NOTIFY sequenceChanged)
  Q_PROPERTY(QVariantMap currentClip READ currentClip NOTIFY selectionChanged)
  Q_PROPERTY(QVariantMap programClip READ programClip NOTIFY playbackStateChanged)
  Q_PROPERTY(int clipCount READ clipCount NOTIFY clipsChanged)
  Q_PROPERTY(int captionCount READ captionCount NOTIFY clipsChanged)
  Q_PROPERTY(QVariantList transcriptSegments READ transcriptSegments NOTIFY clipsChanged)
  Q_PROPERTY(QVariantList transcriptCleanupSuggestions READ transcriptCleanupSuggestions NOTIFY clipsChanged)
  Q_PROPERTY(QVariantList pendingTextBasedEdits READ pendingTextBasedEdits NOTIFY textBasedEditsChanged)
  Q_PROPERTY(QVariantMap textBasedEditPreview READ textBasedEditPreview NOTIFY textBasedEditsChanged)
  Q_PROPERTY(bool silenceAnalysisBusy READ silenceAnalysisBusy NOTIFY silenceAnalysisChanged)
  Q_PROPERTY(int silenceAnalysisPendingCount READ silenceAnalysisPendingCount NOTIFY silenceAnalysisChanged)
  Q_PROPERTY(QString silenceAnalysisStatus READ silenceAnalysisStatus NOTIFY silenceAnalysisChanged)
  Q_PROPERTY(bool transcriptionBusy READ transcriptionBusy NOTIFY transcriptionChanged)
  Q_PROPERTY(int transcriptionPendingCount READ transcriptionPendingCount NOTIFY transcriptionChanged)
  Q_PROPERTY(QString transcriptionStatus READ transcriptionStatus NOTIFY transcriptionChanged)
  Q_PROPERTY(QString transcriptionLanguage READ transcriptionLanguage NOTIFY transcriptionChanged)
  Q_PROPERTY(bool transcriptionSpeakerDiarization READ transcriptionSpeakerDiarization NOTIFY transcriptionChanged)
  Q_PROPERTY(QString transcriptionProcessingDisclosure READ transcriptionProcessingDisclosure NOTIFY transcriptionChanged)
  Q_PROPERTY(bool beatAnalysisBusy READ beatAnalysisBusy NOTIFY beatAnalysisChanged)
  Q_PROPERTY(int beatAnalysisPendingCount READ beatAnalysisPendingCount NOTIFY beatAnalysisChanged)
  Q_PROPERTY(QString beatAnalysisStatus READ beatAnalysisStatus NOTIFY beatAnalysisChanged)
  Q_PROPERTY(QVariantList beatSuggestions READ beatSuggestions NOTIFY beatAnalysisChanged)
  Q_PROPERTY(bool loudnessAnalysisBusy READ loudnessAnalysisBusy NOTIFY loudnessAnalysisChanged)
  Q_PROPERTY(QString loudnessAnalysisStatus READ loudnessAnalysisStatus NOTIFY loudnessAnalysisChanged)
  Q_PROPERTY(QVariantMap loudnessAnalysisResult READ loudnessAnalysisResult NOTIFY loudnessAnalysisChanged)
  Q_PROPERTY(bool autoReframeBusy READ autoReframeBusy NOTIFY autoReframeChanged)
  Q_PROPERTY(QString autoReframeStatus READ autoReframeStatus NOTIFY autoReframeChanged)
  Q_PROPERTY(QVariantList autoReframeSuggestions READ autoReframeSuggestions NOTIFY autoReframeChanged)
  Q_PROPERTY(bool stabilizationAnalysisBusy READ stabilizationAnalysisBusy NOTIFY stabilizationAnalysisChanged)
  Q_PROPERTY(double stabilizationAnalysisProgress READ stabilizationAnalysisProgress NOTIFY stabilizationAnalysisChanged)
  Q_PROPERTY(QString stabilizationAnalysisStatus READ stabilizationAnalysisStatus NOTIFY stabilizationAnalysisChanged)
  Q_PROPERTY(bool stabilizationCompareBefore READ stabilizationCompareBefore NOTIFY stabilizationAnalysisChanged)
  Q_PROPERTY(int currentIndex READ currentIndex NOTIFY selectionChanged)
  Q_PROPERTY(int trackCount READ trackCount NOTIFY sequenceChanged)
  Q_PROPERTY(int videoTrackCount READ videoTrackCount NOTIFY sequenceChanged)
  Q_PROPERTY(int audioTrackCount READ audioTrackCount NOTIFY sequenceChanged)
  Q_PROPERTY(double framesPerSecond READ framesPerSecond NOTIFY sequenceChanged)
  Q_PROPERTY(int sequenceWidth READ sequenceWidth NOTIFY sequenceChanged)
  Q_PROPERTY(int sequenceHeight READ sequenceHeight NOTIFY sequenceChanged)
  Q_PROPERTY(int audioSampleRate READ audioSampleRate NOTIFY sequenceChanged)
  Q_PROPERTY(QString projectBinding READ projectBinding NOTIFY sequenceChanged)
  Q_PROPERTY(QString colorSpace READ colorSpace NOTIFY sequenceChanged)
  Q_PROPERTY(QString dataLevels READ dataLevels NOTIFY sequenceChanged)
  Q_PROPERTY(QString exportContainer READ exportContainer NOTIFY sequenceChanged)
  Q_PROPERTY(QString exportCodec READ exportCodec NOTIFY sequenceChanged)
  Q_PROPERTY(QString exportPreset READ exportPreset NOTIFY sequenceChanged)
  Q_PROPERTY(QVariantMap deliverySettings READ deliverySettings NOTIFY sequenceChanged)
  Q_PROPERTY(QVariantMap deliveryCapabilities READ deliveryCapabilities NOTIFY sequenceChanged)
  Q_PROPERTY(QString activeOutputVariant READ activeOutputVariant NOTIFY sequenceChanged)
  Q_PROPERTY(QVariantList outputVariants READ outputVariants NOTIFY sequenceChanged)
  Q_PROPERTY(QVariantList titleTemplates READ titleTemplates NOTIFY sequenceChanged)
  Q_PROPERTY(QVariantList captionStyleTemplates READ captionStyleTemplates NOTIFY captionStyleTemplatesChanged)
  Q_PROPERTY(QVariantList effectStackPresets READ effectStackPresets NOTIFY effectStackPresetsChanged)
  Q_PROPERTY(QString effectStackPresetMigrationReport READ effectStackPresetMigrationReport NOTIFY effectStackPresetsChanged)
  Q_PROPERTY(QVariantMap sequenceSettings READ sequenceSettings NOTIFY sequenceChanged)
  Q_PROPERTY(QString sequenceFormatLabel READ sequenceFormatLabel NOTIFY sequenceChanged)
  Q_PROPERTY(QString timelineName READ timelineName WRITE setTimelineName NOTIFY timelineNameChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
  Q_PROPERTY(QString totalDurationText READ totalDurationText NOTIFY clipsChanged)
  Q_PROPERTY(double totalDurationSeconds READ totalDurationSecondsValue NOTIFY clipsChanged)
  Q_PROPERTY(double sequencePlayheadSeconds READ sequencePlayheadSeconds NOTIFY playbackStateChanged)
  Q_PROPERTY(double currentClipPlayheadSeconds READ currentClipPlayheadSeconds NOTIFY playbackStateChanged)
  Q_PROPERTY(bool sequencePlaying READ sequencePlaying NOTIFY playbackStateChanged)
  Q_PROPERTY(QString playbackSourceUrl READ playbackSourceUrl NOTIFY playbackStateChanged)
  Q_PROPERTY(QString previewSource READ previewSource NOTIFY previewSourceChanged)
  Q_PROPERTY(QString savedDocumentPath READ savedDocumentPath NOTIFY savedPathsChanged)
  Q_PROPERTY(QString savedRenderPath READ savedRenderPath NOTIFY savedPathsChanged)
  Q_PROPERTY(QString timelineAssetId READ timelineAssetId NOTIFY savedPathsChanged)
  Q_PROPERTY(QString activeTool READ activeTool WRITE setActiveTool NOTIFY editorStateChanged)
  Q_PROPERTY(QString editMode READ editMode WRITE setEditMode NOTIFY editorStateChanged)
  Q_PROPERTY(bool linkedSelectionEnabled READ linkedSelectionEnabled WRITE setLinkedSelectionEnabled NOTIFY editorStateChanged)
  Q_PROPERTY(QVariantList sequenceMarkers READ sequenceMarkers NOTIFY markersChanged)
  Q_PROPERTY(QVariantList reviewNotes READ reviewNotes NOTIFY markersChanged)
  Q_PROPERTY(QVariantList multicamGroups READ multicamGroups NOTIFY multicamChanged)
  Q_PROPERTY(QVariantMap activeMulticamGroup READ activeMulticamGroup NOTIFY multicamChanged)
  Q_PROPERTY(bool currentClipIsMulticam READ currentClipIsMulticam NOTIFY multicamChanged)
  Q_PROPERTY(QVariantMap sourceClip READ sourceClip NOTIFY sourceMonitorChanged)
  Q_PROPERTY(bool hasSourceClip READ hasSourceClip NOTIFY sourceMonitorChanged)
  Q_PROPERTY(bool sourceTapeEnabled READ sourceTapeEnabled NOTIFY sourceTapeChanged)
  Q_PROPERTY(int sourceTapeIndex READ sourceTapeIndex NOTIFY sourceTapeChanged)
  Q_PROPERTY(QVariantList sourceTapeItems READ sourceTapeItems NOTIFY sourceTapeChanged)
  Q_PROPERTY(double sourceTapeDurationSeconds READ sourceTapeDurationSeconds NOTIFY sourceTapeChanged)
  Q_PROPERTY(double sourceInSeconds READ sourceInSeconds NOTIFY sourceMonitorChanged)
  Q_PROPERTY(double sourceOutSeconds READ sourceOutSeconds NOTIFY sourceMonitorChanged)
  Q_PROPERTY(bool sourcePatchReady READ sourcePatchReady NOTIFY sourceMonitorChanged)
  Q_PROPERTY(QString sourcePatchSummary READ sourcePatchSummary NOTIFY sourceMonitorChanged)
  Q_PROPERTY(QVariantMap programSourceGangState READ programSourceGangState NOTIFY programSourceGangStateChanged)
  Q_PROPERTY(QStringList selectedClipIds READ selectedClipIds NOTIFY selectionChanged)
  Q_PROPERTY(int selectedClipCount READ selectedClipCount NOTIFY selectionChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
  Q_PROPERTY(int keyframeClipboardCount READ keyframeClipboardCount NOTIFY keyframeClipboardChanged)
  Q_PROPERTY(int clipClipboardCount READ clipClipboardCount NOTIFY clipClipboardChanged)
  Q_PROPERTY(double clipClipboardDurationSeconds READ clipClipboardDurationSeconds NOTIFY clipClipboardChanged)
  Q_PROPERTY(QString clipClipboardSummary READ clipClipboardSummary NOTIFY clipClipboardChanged)
  Q_PROPERTY(QVariantList renderCacheRanges READ renderCacheRanges NOTIFY renderCacheChanged)
  Q_PROPERTY(QString renderCacheStatus READ renderCacheStatus NOTIFY renderCacheChanged)
  Q_PROPERTY(bool renderCacheBusy READ renderCacheBusy NOTIFY renderCacheChanged)
  Q_PROPERTY(int renderCacheRangeCount READ renderCacheRangeCount NOTIFY renderCacheChanged)
  Q_PROPERTY(int renderCacheReadyRangeCount READ renderCacheReadyRangeCount NOTIFY renderCacheChanged)
  Q_PROPERTY(bool mediaAnalysisBusy READ mediaAnalysisBusy NOTIFY mediaAnalysisChanged)
  Q_PROPERTY(int mediaAnalysisPendingCount READ mediaAnalysisPendingCount NOTIFY mediaAnalysisChanged)
  Q_PROPERTY(bool incrementalLoadBusy READ incrementalLoadBusy NOTIFY incrementalLoadChanged)
  Q_PROPERTY(int incrementalLoadPendingCount READ incrementalLoadPendingCount NOTIFY incrementalLoadChanged)
  Q_PROPERTY(int incrementalLoadCompletedCount READ incrementalLoadCompletedCount NOTIFY incrementalLoadChanged)
  Q_PROPERTY(QString incrementalLoadStatus READ incrementalLoadStatus NOTIFY incrementalLoadChanged)
  Q_PROPERTY(QVariantMap backgroundWorkPriorityState READ backgroundWorkPriorityState NOTIFY backgroundWorkPriorityChanged)
  Q_PROPERTY(QVariantMap staleResultState READ staleResultState NOTIFY staleResultStateChanged)
  Q_PROPERTY(bool insideCompound READ insideCompound NOTIFY compoundNavigationChanged)
  Q_PROPERTY(int compoundNavigationDepth READ compoundNavigationDepth NOTIFY compoundNavigationChanged)
  Q_PROPERTY(QString compoundBreadcrumb READ compoundBreadcrumb NOTIFY compoundNavigationChanged)
  Q_PROPERTY(QVariantList sequenceVersions READ sequenceVersions NOTIFY sequenceVersionsChanged)
  Q_PROPERTY(QVariantList recoveryCandidates READ recoveryCandidates NOTIFY recoveryCandidatesChanged)
  Q_PROPERTY(QVariantList offlineMediaItems READ offlineMediaItems NOTIFY clipsChanged)
  Q_PROPERTY(QVariantList mediaBins READ mediaBins NOTIFY mediaBinsChanged)
  Q_PROPERTY(QString activeMediaBinId READ activeMediaBinId WRITE setActiveMediaBinId NOTIFY mediaBinsChanged)
  Q_PROPERTY(QVariantList smartCollections READ smartCollections NOTIFY smartCollectionsChanged)
  Q_PROPERTY(QString activeSmartCollectionId READ activeSmartCollectionId WRITE setActiveSmartCollectionId NOTIFY smartCollectionsChanged)
  Q_PROPERTY(QVariantList reviewRanges READ reviewRanges NOTIFY reviewRangesChanged)
  Q_PROPERTY(QVariantMap programScopeData READ programScopeData NOTIFY programScopeAnalysisChanged)
  Q_PROPERTY(bool programScopeAnalysisBusy READ programScopeAnalysisBusy NOTIFY programScopeAnalysisChanged)
  Q_PROPERTY(int programScopePublishedRevision READ programScopePublishedRevision NOTIFY programScopeAnalysisChanged)

 public:
  void setEditAccessPolicy(std::function<bool()> policy) { customer_edit_access_.setPolicy(std::move(policy)); }

  explicit TimelineDocumentController(QObject* parent = nullptr);
  ~TimelineDocumentController() override;

  [[nodiscard]] bool hasActiveTimeline() const;
  [[nodiscard]] QVariantList clips() const;
  [[nodiscard]] QVariantList sequenceTracks() const;
  [[nodiscard]] QVariantMap currentClip() const;
  [[nodiscard]] QVariantMap programClip() const;
  [[nodiscard]] int clipCount() const;
  [[nodiscard]] int captionCount() const;
  [[nodiscard]] QVariantList transcriptSegments() const;
  [[nodiscard]] QVariantList transcriptCleanupSuggestions() const;
  [[nodiscard]] QVariantList pendingTextBasedEdits() const;
  [[nodiscard]] QVariantMap textBasedEditPreview() const;
  [[nodiscard]] bool silenceAnalysisBusy() const;
  [[nodiscard]] int silenceAnalysisPendingCount() const;
  [[nodiscard]] QString silenceAnalysisStatus() const;
  [[nodiscard]] bool transcriptionBusy() const;
  [[nodiscard]] int transcriptionPendingCount() const;
  [[nodiscard]] QString transcriptionStatus() const;
  [[nodiscard]] QString transcriptionLanguage() const;
  [[nodiscard]] bool transcriptionSpeakerDiarization() const;
  [[nodiscard]] QString transcriptionProcessingDisclosure() const;
  [[nodiscard]] bool beatAnalysisBusy() const;
  [[nodiscard]] int beatAnalysisPendingCount() const;
  [[nodiscard]] QString beatAnalysisStatus() const;
  [[nodiscard]] QVariantList beatSuggestions() const;
  [[nodiscard]] bool loudnessAnalysisBusy() const;
  [[nodiscard]] QString loudnessAnalysisStatus() const;
  [[nodiscard]] QVariantMap loudnessAnalysisResult() const;
  [[nodiscard]] bool autoReframeBusy() const;
  [[nodiscard]] QString autoReframeStatus() const;
  [[nodiscard]] QVariantList autoReframeSuggestions() const;
  [[nodiscard]] bool stabilizationAnalysisBusy() const;
  [[nodiscard]] double stabilizationAnalysisProgress() const;
  [[nodiscard]] QString stabilizationAnalysisStatus() const;
  [[nodiscard]] bool stabilizationCompareBefore() const;
  [[nodiscard]] QVariantMap programScopeData() const;
  [[nodiscard]] bool programScopeAnalysisBusy() const;
  [[nodiscard]] int programScopePublishedRevision() const;
  Q_INVOKABLE QVariantList timelineSearchResults(
      const QString& query = QString(),
      const QString& kind = QStringLiteral("all"),
      const QString& track_id = QString(),
      const QString& marker_status = QStringLiteral("all")) const;
  Q_INVOKABLE QVariantList playbackPrefetchCandidates(
      double sequence_seconds,
      double direction,
      double loop_start_seconds = -1.0,
      double loop_end_seconds = -1.0,
      double likely_next_edit_seconds = -1.0,
      int maximum_candidates = 6) const;
  Q_INVOKABLE bool activateTimelineSearchResult(
      const QString& result_type,
      const QString& result_id);
  [[nodiscard]] int currentIndex() const;
  [[nodiscard]] int trackCount() const;
  [[nodiscard]] int videoTrackCount() const;
  [[nodiscard]] int audioTrackCount() const;
  [[nodiscard]] double framesPerSecond() const;
  [[nodiscard]] int sequenceWidth() const;
  [[nodiscard]] int sequenceHeight() const;
  [[nodiscard]] int audioSampleRate() const;
  [[nodiscard]] QString projectBinding() const;
  [[nodiscard]] QString colorSpace() const;
  [[nodiscard]] QString dataLevels() const;
  [[nodiscard]] QString exportContainer() const;
  [[nodiscard]] QString exportCodec() const;
  [[nodiscard]] QString exportPreset() const;
  [[nodiscard]] QVariantMap deliverySettings() const;
  [[nodiscard]] QVariantMap deliveryCapabilities() const;
  [[nodiscard]] QString activeOutputVariant() const;
  [[nodiscard]] QVariantList outputVariants() const;
  [[nodiscard]] QVariantList titleTemplates() const;
  [[nodiscard]] QVariantList captionStyleTemplates() const;
  [[nodiscard]] QVariantList effectStackPresets() const;
  [[nodiscard]] QString effectStackPresetMigrationReport() const;
  [[nodiscard]] QVariantMap sequenceSettings() const;
  Q_INVOKABLE QVariantMap timelineDeliveryPreflight(
      const QString& preset = QString()) const;
  Q_INVOKABLE QVariantMap timelinePublishSummary(
      const QString& preset = QString()) const;
  Q_INVOKABLE QVariantMap timelineProjectHealth() const;
  Q_INVOKABLE QString deterministicDocumentHash() const;
  Q_INVOKABLE QVariantMap timelineDeterminismFingerprint(
      const QString& render_path = QString()) const;
  [[nodiscard]] QString sequenceFormatLabel() const;
  [[nodiscard]] QString timelineName() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] bool dirty() const;
  [[nodiscard]] QString totalDurationText() const;
  [[nodiscard]] double totalDurationSecondsValue() const;
  [[nodiscard]] double sequencePlayheadSeconds() const;
  [[nodiscard]] double currentClipPlayheadSeconds() const;
  [[nodiscard]] bool sequencePlaying() const;
  [[nodiscard]] QString playbackSourceUrl() const;
  [[nodiscard]] QString originalPlaybackSourceUrl() const;
  [[nodiscard]] QString proxyPlaybackSourceUrl() const;
  [[nodiscard]] QString previewSource() const;
  [[nodiscard]] QString savedDocumentPath() const;
  [[nodiscard]] QString savedRenderPath() const;
  [[nodiscard]] QString timelineAssetId() const;
  [[nodiscard]] QString activeTool() const;
  [[nodiscard]] QString editMode() const;
  [[nodiscard]] bool linkedSelectionEnabled() const;
  [[nodiscard]] QVariantList sequenceMarkers() const;
  [[nodiscard]] QVariantList reviewNotes() const;
  [[nodiscard]] QVariantList multicamGroups() const;
  [[nodiscard]] QVariantMap activeMulticamGroup() const;
  [[nodiscard]] bool currentClipIsMulticam() const;
  [[nodiscard]] QVariantMap sourceClip() const;
  [[nodiscard]] bool hasSourceClip() const;
  [[nodiscard]] bool sourceTapeEnabled() const;
  [[nodiscard]] int sourceTapeIndex() const;
  [[nodiscard]] QVariantList sourceTapeItems() const;
  [[nodiscard]] double sourceTapeDurationSeconds() const;
  [[nodiscard]] double sourceInSeconds() const;
  [[nodiscard]] double sourceOutSeconds() const;
  [[nodiscard]] bool sourcePatchReady() const;
  [[nodiscard]] QString sourcePatchSummary() const;
  [[nodiscard]] QVariantMap programSourceGangState() const;
  [[nodiscard]] QStringList selectedClipIds() const;
  [[nodiscard]] int selectedClipCount() const;
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;
  [[nodiscard]] int keyframeClipboardCount() const;
  [[nodiscard]] int clipClipboardCount() const;
  [[nodiscard]] double clipClipboardDurationSeconds() const;
  [[nodiscard]] QString clipClipboardSummary() const;
  [[nodiscard]] QVariantList renderCacheRanges() const;
  [[nodiscard]] QString renderCacheStatus() const;
  [[nodiscard]] bool renderCacheBusy() const;
  [[nodiscard]] int renderCacheRangeCount() const;
  [[nodiscard]] int renderCacheReadyRangeCount() const;
  [[nodiscard]] bool mediaAnalysisBusy() const;
  [[nodiscard]] int mediaAnalysisPendingCount() const;
  [[nodiscard]] bool incrementalLoadBusy() const;
  [[nodiscard]] int incrementalLoadPendingCount() const;
  [[nodiscard]] int incrementalLoadCompletedCount() const;
  [[nodiscard]] QString incrementalLoadStatus() const;
  [[nodiscard]] QVariantMap backgroundWorkPriorityState() const;
  [[nodiscard]] QVariantMap staleResultState() const;
  [[nodiscard]] QString renderCachePlaybackSourceUrl() const;
  [[nodiscard]] double renderCachePlaybackStartSeconds() const;
  [[nodiscard]] bool insideCompound() const;
  [[nodiscard]] int compoundNavigationDepth() const;
  [[nodiscard]] QString compoundBreadcrumb() const;
  [[nodiscard]] QVariantList sequenceVersions() const;
  [[nodiscard]] QVariantList recoveryCandidates() const;
  [[nodiscard]] QVariantList offlineMediaItems() const;
  [[nodiscard]] QVariantList mediaBins() const;
  [[nodiscard]] QString activeMediaBinId() const;
  [[nodiscard]] QVariantList smartCollections() const;
  [[nodiscard]] QString activeSmartCollectionId() const;
  [[nodiscard]] QVariantList reviewRanges() const;

  void setTimelineName(const QString& timeline_name);
  void setActiveTool(const QString& active_tool);
  void setEditMode(const QString& edit_mode);
  void setLinkedSelectionEnabled(bool enabled);
  void setActiveMediaBinId(const QString& bin_id);
  void setActiveSmartCollectionId(const QString& collection_id);

  Q_INVOKABLE QVariantList recentTimelines() const;
  Q_INVOKABLE QVariantList pancakeSequences() const;
  Q_INVOKABLE QVariantMap pancakeSequencePreview(
      const QString& document_path) const;
  Q_INVOKABLE QVariantMap pancakeTransferPreview(
      const QString& source_document_path,
      const QStringList& clip_ids,
      double source_range_start_seconds,
      double source_range_end_seconds,
      double destination_seconds,
      const QString& edit_mode = QStringLiteral("overwrite"),
      const QString& transfer_mode = QStringLiteral("copy")) const;
  Q_INVOKABLE int transferFromPancakeSequence(
      const QString& source_document_path,
      const QStringList& clip_ids,
      double source_range_start_seconds,
      double source_range_end_seconds,
      double destination_seconds,
      const QString& edit_mode = QStringLiteral("overwrite"),
      const QString& transfer_mode = QStringLiteral("copy"));
  Q_INVOKABLE void createTimeline(const QString& timeline_name = QString());
  Q_INVOKABLE void createTimelineWithSettings(const QVariantMap& settings);
  Q_INVOKABLE void applyOutputPreset(const QString& preset);
  Q_INVOKABLE bool setDeliverySetting(const QString& setting, const QVariant& value);
  Q_INVOKABLE bool resetSelectedClipOutputVariantOverride();
  Q_INVOKABLE void closeTimeline();
  Q_INVOKABLE bool loadTimeline(const QString& document_path);
  Q_INVOKABLE bool saveTimeline();
  Q_INVOKABLE bool createSequenceVersion(const QString& label = QString());
  Q_INVOKABLE bool createSequenceVersionWithMetadata(
      const QString& label,
      const QString& notes,
      const QString& author,
      bool protected_milestone);
  Q_INVOKABLE QVariantMap sequenceVersionComparison(const QString& version_id) const;
  Q_INVOKABLE QString programComparisonCapturePath(
      const QString& role,
      int revision) const;
  Q_INVOKABLE void requestProgramScopeAnalysis(
      const QString& source,
      int revision);
  Q_INVOKABLE void clearProgramScopeAnalysis();
  Q_INVOKABLE QVariantMap renderProgramComparisonDifference(
      const QString& current_source,
      const QString& reference_source,
      double gain = 2.0) const;
  Q_INVOKABLE bool updateSequenceVersionMetadata(
      const QString& version_id,
      const QString& label,
      const QString& notes,
      const QString& author,
      bool protected_milestone);
  Q_INVOKABLE bool duplicateSequenceVersion(
      const QString& version_id,
      const QString& label = QString());
  Q_INVOKABLE bool restoreSequenceVersion(const QString& version_id);
  Q_INVOKABLE bool deleteSequenceVersion(const QString& version_id);
  Q_INVOKABLE void refreshRecoveryCandidates();
  Q_INVOKABLE bool openRecoveryCandidate(const QString& candidate_id);
  Q_INVOKABLE bool restoreRecoveryCandidateAsCopy(const QString& candidate_id);
  Q_INVOKABLE bool discardRecoveryCandidate(const QString& candidate_id);
  Q_INVOKABLE QVariantMap previewBatchRelink(const QStringList& search_roots) const;
  Q_INVOKABLE int applyBatchRelink(const QVariantList& approved_substitutions);
  Q_INVOKABLE QString createMediaBin(
      const QString& name,
      const QString& parent_bin_id = QString());
  Q_INVOKABLE bool renameMediaBin(const QString& bin_id, const QString& name);
  Q_INVOKABLE bool moveMediaBin(const QString& bin_id, const QString& parent_bin_id);
  Q_INVOKABLE bool deleteMediaBin(const QString& bin_id);
  Q_INVOKABLE bool assignMediaAssetToBin(
      const QString& asset_id,
      const QString& bin_id);
  Q_INVOKABLE bool mediaAssetVisibleInBin(
      const QString& asset_id,
      const QString& bin_id) const;
  Q_INVOKABLE QString createSmartCollection(
      const QString& name,
      const QVariantMap& rules);
  Q_INVOKABLE bool updateSmartCollection(
      const QString& collection_id,
      const QString& name,
      const QVariantMap& rules);
  Q_INVOKABLE bool deleteSmartCollection(const QString& collection_id);
  Q_INVOKABLE bool mediaAssetMatchesSmartCollection(
      const QVariantMap& catalog_asset,
      const QString& collection_id) const;
  Q_INVOKABLE QString createReviewRange(
      const QString& name,
      double start_seconds,
      double end_seconds,
      double preroll_seconds = 0.0,
      double postroll_seconds = 0.0);
  Q_INVOKABLE bool updateReviewRange(
      const QString& range_id,
      const QString& name,
      double start_seconds,
      double end_seconds,
      double preroll_seconds,
      double postroll_seconds);
  Q_INVOKABLE bool deleteReviewRange(const QString& range_id);
  Q_INVOKABLE QVariantMap mediaAssetMetadata(const QString& asset_id) const;
  Q_INVOKABLE QVariantMap inspectMediaAssetTechnicalMetadata(
      const QVariantMap& catalog_asset) const;
  Q_INVOKABLE bool setMediaAssetNotesAndCustomFields(
      const QString& asset_id,
      const QString& notes,
      const QVariantMap& custom_fields);
  Q_INVOKABLE QVariantMap searchMediaAsset(
      const QString& asset_id,
      const QString& query,
      const QStringList& scopes,
      const QVariantMap& catalog_metadata = QVariantMap()) const;
  Q_INVOKABLE bool setMediaAssetDetectedOnScreenText(
      const QString& asset_id,
      const QStringList& detected_text);
  Q_INVOKABLE QVariantMap analyzeMediaUsage(
      const QVariantList& catalog_assets) const;
  Q_INVOKABLE QVariantMap revealMediaUsage(const QString& asset_id);
  Q_INVOKABLE int removeUnusedMediaFromProject(const QStringList& asset_ids);
  Q_INVOKABLE int consolidateDuplicateMedia(
      const QVariantMap& canonical_asset,
      const QVariantList& duplicate_assets);
  Q_INVOKABLE QString requestMediaHoverFrame(
      const QString& asset_id,
      const QString& file_path,
      double source_seconds);
  Q_INVOKABLE int configureSourceTape(const QVariantList& assets);
  Q_INVOKABLE bool activateSourceTapeIndex(int index);
  Q_INVOKABLE bool stepSourceTape(int direction);
  Q_INVOKABLE void clearSourceTape();
  Q_INVOKABLE bool setMediaAssetRating(
      const QStringList& asset_ids,
      int rating);
  Q_INVOKABLE bool setMediaAssetDisposition(
      const QStringList& asset_ids,
      const QString& disposition);
  Q_INVOKABLE bool setMediaAssetColorLabel(
      const QStringList& asset_ids,
      const QString& color_label);
  Q_INVOKABLE bool setMediaAssetTags(
      const QStringList& asset_ids,
      const QStringList& tags);
  Q_INVOKABLE bool flushEditJournal();
  Q_INVOKABLE QString editJournalPath() const;
  void setBackgroundRenderResourcePolicy(const QString& policy);
  [[nodiscard]] bool renderCurrentTimeline(const QString& output_path) const;
  [[nodiscard]] QString generateVideoPoster(const QString& file_path, const QString& cache_key = QString()) const;
  Q_INVOKABLE void selectClip(int index);
  Q_INVOKABLE void addClipFromGallery(
      const QString& asset_id,
      const QString& file_path,
      const QString& display_label,
      double duration_seconds,
      const QString& poster_source = QString());
  Q_INVOKABLE void addTitleClip();
  Q_INVOKABLE bool addTitleClipFromTemplate(const QString& template_key);
  Q_INVOKABLE bool addAdjustmentClip(double duration_seconds = 5.0);
  Q_INVOKABLE bool applySelectedTitleTemplate(const QString& template_key);
  Q_INVOKABLE void updateSelectedTitle(const QVariantMap& values);
  Q_INVOKABLE QVariantMap captionStyleTemplatePreview(const QString& template_id) const;
  Q_INVOKABLE bool applyCaptionStyleTemplate(
      const QString& template_id,
      bool apply_to_all_captions = false);
  Q_INVOKABLE bool saveSelectedCaptionStyleTemplate(const QString& template_name);
  Q_INVOKABLE bool deleteCaptionStyleTemplate(const QString& template_id);
  Q_INVOKABLE void generateCaptionsFromTranscript(const QString& transcript);
  Q_INVOKABLE int transcribeTimelineAudio();
  Q_INVOKABLE void setTranscriptionOptions(
      const QString& language,
      bool speaker_diarization);
  Q_INVOKABLE int retryTranscription();
  Q_INVOKABLE void cancelTranscription();
  Q_INVOKABLE int analyzeTimelineBeats();
  Q_INVOKABLE void cancelBeatAnalysis();
  Q_INVOKABLE int createBeatMarkersFromAnalysis();
  Q_INVOKABLE bool analyzeTimelineLoudness();
  Q_INVOKABLE void cancelTimelineLoudnessAnalysis();
  Q_INVOKABLE bool normalizeTimelineLoudness();
  Q_INVOKABLE bool analyzeSelectedClipForAutoReframe(
      const QString& composition = QStringLiteral("headroom"));
  Q_INVOKABLE void cancelAutoReframeAnalysis();
  Q_INVOKABLE bool applyAutoReframeSuggestions();
  Q_INVOKABLE bool setSelectedClipStabilization(
      bool enabled,
      double smoothing,
      double crop_protection,
      bool rolling_shutter_enabled,
      double rolling_shutter_amount);
  Q_INVOKABLE bool analyzeSelectedClipStabilization();
  Q_INVOKABLE void cancelStabilizationAnalysis();
  Q_INVOKABLE bool resetSelectedClipStabilization();
  Q_INVOKABLE void setStabilizationCompareBefore(bool before);
  Q_INVOKABLE bool mergeSelectedCaptionWithNext();
  Q_INVOKABLE bool updateSelectedCaptionWord(
      int word_index,
      const QString& text,
      double start_seconds,
      double end_seconds);
  Q_INVOKABLE bool setSelectedCaptionEmphasis(
      const QString& mode,
      bool reduced_motion,
      bool apply_to_all_captions = false);
  Q_INVOKABLE bool updateTranscriptWord(
      const QString& segment_id,
      int word_index,
      const QString& text,
      const QString& speaker);
  Q_INVOKABLE bool seekTranscriptWord(
      const QString& segment_id,
      int word_index,
      const QString& monitor);
  Q_INVOKABLE QVariantMap previewCaptionSegmentation(
      const QVariantMap& options = QVariantMap()) const;
  Q_INVOKABLE bool applyCaptionSegmentation(
      const QVariantMap& options = QVariantMap());
  Q_INVOKABLE QVariantMap validateCaptions(
      const QString& export_format = QStringLiteral("srt")) const;
  Q_INVOKABLE bool exportCaptionSidecar(const QString& format);
  Q_INVOKABLE bool selectClipsInSequenceRange(double start_seconds, double end_seconds);
  Q_INVOKABLE bool extractSequenceRange(double start_seconds, double end_seconds);
  Q_INVOKABLE QString stageTextBasedEditDeletion(
      double start_seconds,
      double end_seconds);
  Q_INVOKABLE QString stageTextBasedEditMove(
      double start_seconds,
      double end_seconds,
      int direction);
  Q_INVOKABLE bool restorePendingTextBasedEdit(const QString& operation_id);
  Q_INVOKABLE void clearPendingTextBasedEdits();
  Q_INVOKABLE bool commitPendingTextBasedEdits();
  Q_INVOKABLE bool applyTranscriptCleanupSuggestions(const QStringList& suggestion_ids);
  Q_INVOKABLE int analyzeTimelineSilence();
  Q_INVOKABLE void cancelSilenceAnalysis();
  Q_INVOKABLE void insertClipFromGallery(
      const QString& asset_id,
      const QString& file_path,
      const QString& display_label,
      double duration_seconds,
      double trim_start_seconds,
      double trim_end_seconds,
      const QString& poster_source = QString());
  Q_INVOKABLE void insertClipFromGalleryAtTrack(
      const QString& asset_id,
      const QString& file_path,
      const QString& display_label,
      double duration_seconds,
      double trim_start_seconds,
      double trim_end_seconds,
      const QString& poster_source,
      int track_row_index,
      double sequence_seconds);
  Q_INVOKABLE void overwriteClipFromGallery(
      const QString& asset_id,
      const QString& file_path,
      const QString& display_label,
      double duration_seconds,
      double trim_start_seconds,
      double trim_end_seconds,
      const QString& poster_source = QString());
  Q_INVOKABLE void addVideoTrack();
  Q_INVOKABLE bool moveTrackRow(int track_row_index, int direction);
  Q_INVOKABLE bool removeTrackRow(int track_row_index, bool confirm_non_empty = false);
  Q_INVOKABLE void moveSelectedClipToTrackRow(int track_row_index, double start_seconds);
  Q_INVOKABLE bool moveSelectedClipToTrackRowWithMode(
      int track_row_index,
      double start_seconds,
      const QString& mode,
      bool ripple);
  Q_INVOKABLE void liftSelectedClip();
  Q_INVOKABLE void extractSelectedClip();
  Q_INVOKABLE void liftSelectedClips();
  Q_INVOKABLE void extractSelectedClips();
  Q_INVOKABLE void removeSelectedClip();
  Q_INVOKABLE int copySelectedClips();
  Q_INVOKABLE int cutSelectedClips();
  Q_INVOKABLE int pasteClipsAtPlayhead(const QString& mode = QStringLiteral("overwrite"));
  Q_INVOKABLE QVariantMap pasteAttributesComparison() const;
  Q_INVOKABLE int pasteAttributesToSelectedClips(const QStringList& categories);
  Q_INVOKABLE void clearClipClipboard();
  Q_INVOKABLE bool duplicateSelectedClip();
  Q_INVOKABLE bool groupSelectedClips();
  Q_INVOKABLE bool ungroupSelectedClips();
  Q_INVOKABLE bool replaceSelectedClipFromSource();
  Q_INVOKABLE bool fitToFillSelectedClipFromSource();
  Q_INVOKABLE bool unlinkSelectedClipAudio();
  Q_INVOKABLE bool relinkSelectedClipAudio();
  Q_INVOKABLE void setLinkedEditOverride(bool enabled);
  Q_INVOKABLE void moveSelectedBackward();
  Q_INVOKABLE void moveSelectedForward();
  Q_INVOKABLE void toggleTrackLocked(int track_row_index);
  Q_INVOKABLE void toggleTrackMuted(int track_row_index);
  Q_INVOKABLE void toggleTrackSolo(int track_row_index);
  Q_INVOKABLE void toggleTrackVisibility(int track_row_index);
  Q_INVOKABLE void toggleTrackTargeted(int track_row_index);
  Q_INVOKABLE bool generateSelectedClipProxy();
  Q_INVOKABLE int generateAllClipProxies();
  Q_INVOKABLE bool cancelSelectedClipProxyGeneration();
  Q_INVOKABLE bool toggleSelectedClipProxy();
  Q_INVOKABLE bool removeSelectedClipProxy();
  Q_INVOKABLE bool generateRenderCache();
  Q_INVOKABLE bool cancelRenderCacheGeneration();
  Q_INVOKABLE void clearRenderCache();
  Q_INVOKABLE QVariantMap projectCacheSummary() const;
  Q_INVOKABLE QVariantMap cleanupProjectCaches(const QStringList& categories);
  Q_INVOKABLE QVariantMap cacheBudgetState() const;
  Q_INVOKABLE bool setProjectCacheBudgets(const QVariantMap& settings);
  Q_INVOKABLE void refreshCachePressure();
  Q_INVOKABLE void setCachePressureLevelForTesting(const QString& level);
  Q_INVOKABLE QVariantList timelineBackgroundJobs() const;
  Q_INVOKABLE void setBackgroundWorkIntent(
      bool workspace_visible,
      bool interaction_active,
      const QStringList& visible_clip_ids = {});
  Q_INVOKABLE bool controlTimelineBackgroundJob(
      const QString& job_id,
      const QString& action);
  Q_INVOKABLE bool activateTimelineBackgroundJob(const QString& job_id);
  Q_INVOKABLE bool toggleTrackAutomationKeyframe(int track_row_index, const QString& parameter);
  Q_INVOKABLE bool setTrackAutomationValue(
      int track_row_index,
      const QString& parameter,
      double value);
  Q_INVOKABLE bool updateTrackAutomationKeyframe(
      int track_row_index,
      const QString& parameter,
      double from_sequence_seconds,
      double to_sequence_seconds,
      double value);
  Q_INVOKABLE bool removeTrackAutomationKeyframe(
      int track_row_index,
      const QString& parameter,
      double sequence_seconds);
  Q_INVOKABLE bool setTrackAutomationKeyframeInterpolation(
      int track_row_index,
      const QString& parameter,
      double sequence_seconds,
      const QString& interpolation);
  Q_INVOKABLE bool setTrackAudioSetting(
      int track_row_index,
      const QString& setting,
      const QVariant& value);
  Q_INVOKABLE bool previewTrackDialogueCleanup(
      int track_row_index,
      const QVariantMap& settings,
      bool audition_original = false);
  Q_INVOKABLE bool applyTrackDialogueCleanup(
      int track_row_index,
      const QVariantMap& settings);
  Q_INVOKABLE void clearTrackDialogueCleanupPreview(int track_row_index);
  Q_INVOKABLE QVariantMap audioRoleSummary(const QString& role) const;
  Q_INVOKABLE bool setAudioRoleSetting(
      const QString& role,
      const QString& setting,
      const QVariant& value);
  Q_INVOKABLE QVariantMap generateDialogueDuckingAutomation(
      double amount_db = -12.0,
      double attack_seconds = 0.12,
      double release_seconds = 0.35);
  Q_INVOKABLE void clearTrackAutomation(int track_row_index, const QString& parameter = QString());
  Q_INVOKABLE void setSelectedTrimStartSeconds(double seconds);
  Q_INVOKABLE void setSelectedTrimEndSeconds(double seconds);
  Q_INVOKABLE void trimSelectedClipStartToSequenceSeconds(double seconds, bool ripple = false);
  Q_INVOKABLE void trimSelectedClipEndToSequenceSeconds(double seconds, bool ripple = false);
  Q_INVOKABLE bool nudgeSelectedTrimEdge(
      const QString& edge,
      int frame_delta,
      bool asymmetric_linked_av = false,
      bool ripple = false);
  Q_INVOKABLE QVariantMap selectedEditPointInfo(const QString& edge) const;
  Q_INVOKABLE QVariantMap selectAdjacentEditPoint(
      double current_boundary_seconds,
      int direction);
  Q_INVOKABLE QVariantMap selectOtherEditPointSide(const QString& edge);
  Q_INVOKABLE bool rollSelectedClipStartToSequenceSeconds(double seconds);
  Q_INVOKABLE bool rollSelectedClipEndToSequenceSeconds(double seconds);
  Q_INVOKABLE bool slipSelectedClipBySeconds(double delta_seconds);
  Q_INVOKABLE bool slideSelectedClipBySeconds(double delta_seconds);
  Q_INVOKABLE void setSelectedFrameMode(const QString& frame_mode);
  Q_INVOKABLE void setSelectedFrameScale(double frame_scale);
  Q_INVOKABLE void setSelectedFrameOffsetX(double frame_offset_x);
  Q_INVOKABLE void setSelectedFrameOffsetY(double frame_offset_y);
  Q_INVOKABLE void setSelectedFrameRotationDegrees(double frame_rotation_degrees);
  Q_INVOKABLE void setSelectedClipOpacity(double opacity);
  Q_INVOKABLE void setSelectedClipAudioGain(double gain_db);
  Q_INVOKABLE bool setSelectedClipAudioChannelMapping(
      const QString& mode,
      bool swap_channels,
      bool invert_left_phase,
      bool invert_right_phase);
  Q_INVOKABLE void setSelectedClipFadeInSeconds(double seconds);
  Q_INVOKABLE void setSelectedClipFadeOutSeconds(double seconds);
  Q_INVOKABLE void setSelectedClipPlaybackRate(double playback_rate);
  Q_INVOKABLE bool applySelectedClipRetimePreset(const QString& preset_name);
  Q_INVOKABLE bool addSelectedClipRetimePoint(double local_seconds, double source_seconds);
  Q_INVOKABLE bool addSelectedClipRetimeHold(
      double local_seconds,
      double duration_seconds = 1.0);
  Q_INVOKABLE bool setSelectedClipRetimeOptions(
      const QString& pitch_strategy,
      const QString& interpolation_mode);
  Q_INVOKABLE bool toggleSelectedClipKeyframe(const QString& parameter);
  Q_INVOKABLE bool removeSelectedClipKeyframe(const QString& parameter, double local_seconds);
  Q_INVOKABLE bool moveSelectedClipKeyframe(
      const QString& parameter,
      double from_local_seconds,
      double to_local_seconds);
  Q_INVOKABLE bool setSelectedClipKeyframeValue(
      const QString& parameter,
      double local_seconds,
      double value);
  Q_INVOKABLE bool setSelectedClipKeyframeInterpolation(
      const QString& parameter,
      double local_seconds,
      const QString& interpolation);
  Q_INVOKABLE bool setSelectedClipKeyframeBezierHandle(
      const QString& parameter,
      double local_seconds,
      const QString& side,
      double time_offset_seconds,
      double value_offset);
  Q_INVOKABLE bool setSelectedClipKeyframeBezierHandlesLinked(
      const QString& parameter,
      double local_seconds,
      bool linked);
  Q_INVOKABLE void clearSelectedClipKeyframes(const QString& parameter = QString());
  Q_INVOKABLE bool seekSelectedClipKeyframe(const QString& parameter, int direction);
  Q_INVOKABLE int copySelectedClipKeyframes(const QString& parameter = QString());
  Q_INVOKABLE int copySelectedClipKeyframeSelection(
      const QString& parameter,
      const QVariantList& local_seconds);
  Q_INVOKABLE int pasteSelectedClipKeyframes(double local_seconds, const QString& parameter = QString());
  Q_INVOKABLE bool scaleSelectedClipKeyframeTimes(
      const QString& parameter,
      double scale_factor);
  Q_INVOKABLE int setSelectedClipKeyframesRoving(
      const QString& parameter,
      const QVariantList& local_seconds,
      bool roving);
  Q_INVOKABLE int alignSelectedClipKeyframes(
      const QString& parameter,
      const QVariantList& local_seconds,
      const QString& alignment);
  Q_INVOKABLE int distributeSelectedClipKeyframes(
      const QString& parameter,
      const QVariantList& local_seconds,
      const QString& axis);
  Q_INVOKABLE int scaleSelectedClipKeyframeSelection(
      const QString& parameter,
      const QVariantList& local_seconds,
      double time_scale,
      double value_scale);
  Q_INVOKABLE bool applySelectedClipMotionPreset(const QString& preset_name);
  Q_INVOKABLE void addSelectedClipEffect(const QString& effect_name);
  Q_INVOKABLE bool moveSelectedClipEffect(const QString& effect_name, int direction);
  Q_INVOKABLE void removeSelectedClipEffect(const QString& effect_name);
  Q_INVOKABLE void clearSelectedClipEffects();
  Q_INVOKABLE void setSelectedClipEffectsEnabled(bool enabled);
  Q_INVOKABLE void setSelectedClipEffectEnabled(const QString& effect_name, bool enabled);
  Q_INVOKABLE void setSelectedClipEffectAmount(const QString& effect_name, double amount);
  Q_INVOKABLE void resetSelectedClipEffect(const QString& effect_name);
  Q_INVOKABLE QVariantMap effectStackPresetPreview(const QString& preset_id) const;
  Q_INVOKABLE bool saveSelectedClipEffectStackPreset(const QString& preset_name);
  Q_INVOKABLE bool deleteEffectStackPreset(const QString& preset_id);
  Q_INVOKABLE bool applyEffectStackPreset(
      const QString& preset_id,
      const QStringList& selected_effect_names = QStringList(),
      bool replace_existing = false);
  Q_INVOKABLE bool applySelectedClipColorLut(const QString& file_path);
  Q_INVOKABLE bool clearSelectedClipColorLut();
  Q_INVOKABLE bool matchSelectedClipColorToReference(const QString& reference_clip_id);
  Q_INVOKABLE QVariantMap analyzeSelectedClipColorScopes() const;
  Q_INVOKABLE void assignSelectedClipTransition(
      const QString& edge,
      const QString& transition_name,
      double duration_seconds = 0.35);
  Q_INVOKABLE void clearSelectedClipTransition(const QString& edge);
  Q_INVOKABLE QVariantList transitionPreviewStatesAt(double sequence_seconds) const;
  Q_INVOKABLE double sourceDurationSecondsForFile(const QString& file_path);
  Q_INVOKABLE bool splitSelectedClip(double split_seconds);
  Q_INVOKABLE void loadSourceClipFromGallery(
      const QString& asset_id,
      const QString& file_path,
      const QString& display_label,
      double duration_seconds,
      const QString& poster_source = QString());
  Q_INVOKABLE void clearSourceClip();
  Q_INVOKABLE void setSourceInSeconds(double seconds);
  Q_INVOKABLE void setSourceOutSeconds(double seconds);
  Q_INVOKABLE void clearSourceInPoint();
  Q_INVOKABLE void clearSourceOutPoint();
  Q_INVOKABLE void clearSourceRange();
  Q_INVOKABLE bool seekSourceInPoint();
  Q_INVOKABLE bool seekSourceOutPoint();
  Q_INVOKABLE bool matchFrameToSource();
  Q_INVOKABLE QVariantMap synchronizeSourceToProgram(double sequence_seconds);
  Q_INVOKABLE QVariantMap gangedProgramPositionForSource(double source_seconds) const;
  Q_INVOKABLE void clearProgramSourceGangState();
  Q_INVOKABLE void appendSourceClipToTimeline();
  Q_INVOKABLE void insertSourceClipAtPlayhead();
  Q_INVOKABLE void overwriteSourceClipAtPlayhead();
  Q_INVOKABLE QString addSequenceMarker(const QString& label = QString());
  Q_INVOKABLE QString addReviewNote(
      const QString& label,
      const QString& note,
      const QString& assignee = QString(),
      const QString& approval_state = QStringLiteral("open"));
  Q_INVOKABLE void moveSequenceMarker(const QString& marker_id, double seconds);
  Q_INVOKABLE QVariantMap resolveTimelineSnap(
      double proposed_seconds,
      double threshold_seconds,
      const QString& excluded_clip_id = QString(),
      const QString& excluded_marker_id = QString(),
      bool include_audio_peaks = false) const;
  Q_INVOKABLE void renameSequenceMarker(const QString& marker_id, const QString& label);
  Q_INVOKABLE void setSequenceMarkerNote(const QString& marker_id, const QString& note);
  Q_INVOKABLE void setSequenceMarkerStatus(const QString& marker_id, const QString& status);
  Q_INVOKABLE void setSequenceMarkerCompleted(const QString& marker_id, bool completed);
  Q_INVOKABLE bool updateSequenceMarkerReview(
      const QString& marker_id,
      const QString& label,
      const QString& note,
      const QString& status,
      const QString& assignee,
      const QString& approval_state,
      bool completed);
  Q_INVOKABLE QString exportReviewNotes(const QString& file_path = QString());
  Q_INVOKABLE int importReviewNotes(const QString& file_path);
  Q_INVOKABLE void seekSequenceMarker(const QString& marker_id);
  Q_INVOKABLE bool seekNextSequenceMarker();
  Q_INVOKABLE bool seekPreviousSequenceMarker();
  Q_INVOKABLE bool seekNextEditPoint();
  Q_INVOKABLE bool seekPreviousEditPoint();
  Q_INVOKABLE void removeSequenceMarker(const QString& marker_id);
  Q_INVOKABLE void playSequence();
  Q_INVOKABLE void pauseSequence();
  Q_INVOKABLE void seekSequenceSeconds(double seconds);
  Q_INVOKABLE void seekCurrentClipSeconds(double seconds);
  Q_INVOKABLE bool applyTimecodeEntry(const QString& operation, const QString& value);
  Q_INVOKABLE void stepSequenceFrames(int direction);
  Q_INVOKABLE void syncPlaybackPosition(double clip_seconds);
  void syncGapPlaybackPosition(double sequence_seconds);
  Q_INVOKABLE bool advancePlaybackAfterClipEnd();

  Q_INVOKABLE void toggleClipSelection(int index);
  Q_INVOKABLE void selectClipRange(int from_index, int to_index);
  Q_INVOKABLE void selectAllClips();
  Q_INVOKABLE void deselectAllClips();
  Q_INVOKABLE int selectClipsForward(bool selected_track_only = false);
  Q_INVOKABLE int selectClipsBackward(bool selected_track_only = false);
  Q_INVOKABLE QVariantList selectedClipSimilarityOptions() const;
  Q_INVOKABLE int selectSimilarClips(const QStringList& constraints);
  Q_INVOKABLE QVariantList timelineGaps(
      double minimum_duration_seconds = 0.0,
      double maximum_duration_seconds = 0.0) const;
  Q_INVOKABLE QVariantMap previewTimelineGap(const QString& gap_id);
  Q_INVOKABLE QVariantMap navigateTimelineGap(
      double current_seconds,
      int direction,
      double minimum_duration_seconds = 0.0,
      double maximum_duration_seconds = 0.0);
  Q_INVOKABLE int closeTimelineGaps(const QStringList& gap_ids);
  Q_INVOKABLE int closeAllTimelineGaps(
      double minimum_duration_seconds = 0.0,
      double maximum_duration_seconds = 0.0);
  Q_INVOKABLE void removeSelectedClips();
  Q_INVOKABLE bool createCompoundClipFromSelection(const QString& label = QString());
  Q_INVOKABLE bool setSelectedClipMulticamSyncMarkerAtPlayhead();
  Q_INVOKABLE QVariantMap previewSelectedExternalAudioSync(
      const QString& sync_method,
      double manual_offset_seconds = 0.0);
  Q_INVOKABLE bool applySelectedExternalAudioSync(
      const QString& sync_method,
      double manual_offset_seconds = 0.0);
  Q_INVOKABLE QString createMulticamGroupFromSelection(
      const QString& sync_method,
      const QString& label = QString());
  Q_INVOKABLE bool switchMulticamAngle(
      const QString& group_id,
      int angle_index,
      double sequence_seconds = -1.0);
  Q_INVOKABLE QVariantMap multicamAnglePreviewState(
      const QString& group_id,
      int angle_index,
      double sequence_seconds = -1.0) const;
  Q_INVOKABLE bool setMulticamAudioMode(
      const QString& group_id,
      const QString& mode,
      int angle_index = -1);
  Q_INVOKABLE bool nudgeMulticamCut(
      const QString& group_id,
      double sequence_seconds,
      int frame_delta);
  Q_INVOKABLE bool removeMulticamCut(const QString& group_id, double sequence_seconds);
  Q_INVOKABLE bool removeMulticamGroup(const QString& group_id);
  Q_INVOKABLE bool openSelectedCompoundClip();
  Q_INVOKABLE bool closeCompoundClip();
  Q_INVOKABLE bool regenerateSelectedCompoundClip();

  Q_INVOKABLE void beginContinuousEdit();
  Q_INVOKABLE void endContinuousEdit();

  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();

signals:
  void timelineStateChanged();
  void clipsChanged();
  void sequenceChanged();
  void selectionChanged();
  void timelineNameChanged();
  void statusChanged();
  void dirtyChanged();
  void previewSourceChanged();
  void savedPathsChanged();
  void playbackStateChanged();
  void editorStateChanged();
  void sourceMonitorChanged();
  void programSourceGangStateChanged();
  void sourceTapeChanged();
  void sourceSeekRequested(double seconds);
  void markersChanged();
  void undoStateChanged();
  void keyframeClipboardChanged();
  void clipClipboardChanged();
  void renderCacheChanged();
  void mediaAnalysisChanged();
  void incrementalLoadChanged();
  void backgroundWorkPriorityChanged();
  void staleResultStateChanged();
  void cacheBudgetStateChanged();
  void pancakeSequencesChanged();
  void compoundNavigationChanged();
  void sequenceVersionsChanged();
  void recoveryCandidatesChanged();
  void mediaBinsChanged();
  void smartCollectionsChanged();
  void reviewRangesChanged();
  void mediaMetadataChanged();
  void mediaHoverFrameReady(
      const QString& asset_id,
      double source_seconds,
      const QString& frame_source);
  void silenceAnalysisChanged();
  void transcriptionChanged();
  void textBasedEditsChanged();
  void beatAnalysisChanged();
  void loudnessAnalysisChanged();
  void autoReframeChanged();
  void stabilizationAnalysisChanged();
  void programScopeAnalysisChanged();
  void multicamChanged();
  void effectStackPresetsChanged();
  void captionStyleTemplatesChanged();

 private:
  CustomerEditAccess customer_edit_access_;

  enum class TrackKind {
    Video,
    Audio,
  };

  struct ClipEffectState final {
    bool enabled = true;
    double amount = 1.0;
  };

  struct ClipKeyframe final {
    ClipKeyframe() = default;
    ClipKeyframe(
        const QString& parameter_value,
        double local_seconds_value,
        double value_value,
        const QString& interpolation_value)
        : parameter(parameter_value),
          local_seconds(local_seconds_value),
          value(value_value),
          interpolation(interpolation_value) {}

    QString parameter;
    double local_seconds = 0.0;
    double value = 0.0;
    QString interpolation = QStringLiteral("linear");
    double bezier_in_time_seconds = 0.0;
    double bezier_in_value = 0.0;
    double bezier_out_time_seconds = 0.0;
    double bezier_out_value = 0.0;
    bool bezier_in_custom = false;
    bool bezier_out_custom = false;
    bool bezier_handles_linked = true;
    bool roving = false;
    QString source;
  };

  struct CaptionWord final {
    QString text;
    double start_seconds = 0.0;
    double end_seconds = 0.0;
    QString speaker;
    double confidence = 1.0;
  };

  struct SilenceRange final {
    double source_start_seconds = 0.0;
    double source_end_seconds = 0.0;
  };

  struct OutputVariantOverride final {
    QString frame_mode = QStringLiteral("fit");
    double frame_scale = 1.0;
    double frame_offset_x = 0.0;
    double frame_offset_y = 0.0;
    double frame_rotation_degrees = 0.0;
    double title_font_size = 64.0;
    double title_position_x = 0.5;
    double title_position_y = 0.82;
    QString title_font_family = QStringLiteral("Sans Serif");
    QString title_color = QStringLiteral("#ffffffff");
    QString title_alignment = QStringLiteral("center");
    QString title_background_color = QStringLiteral("#99000000");
    QString caption_casing = QStringLiteral("none");
    QString caption_stroke_color = QStringLiteral("#ff000000");
    double caption_stroke_width = 0.0;
    bool caption_shadow_enabled = false;
    QString caption_shadow_color = QStringLiteral("#99000000");
    double caption_shadow_offset_x = 2.0;
    double caption_shadow_offset_y = 3.0;
    double caption_background_padding = 14.0;
    double caption_background_radius = 8.0;
    double caption_safe_margin_x = 0.08;
    double caption_safe_margin_y = 0.08;
    QVector<ClipKeyframe> framing_keyframes;
  };

  struct Clip final {
    QString clip_id;
    QString asset_id;
    QString media_type = QStringLiteral("video");
    QString file_path;
    qint64 source_file_size = 0;
    QString source_identity_signature;
    QString source_content_sha256;
    QString source_integrity_status = QStringLiteral("pending");
    QString display_label;
    QString poster_source;
    QString linked_group_id;
    double duration_seconds = 0.0;
    double trim_start_seconds = 0.0;
    double trim_end_seconds = 0.0;
    double start_seconds = 0.0;
    bool has_audio = false;
    bool audio_detached = false;
    int audio_lane_index = -1;
    int source_audio_channels = 0;
    QString source_audio_channel_layout;
    QString audio_channel_mode = QStringLiteral("source");
    bool audio_swap_channels = false;
    bool audio_invert_left_phase = false;
    bool audio_invert_right_phase = false;
    QString waveform_source;
    QString media_analysis_status = QStringLiteral("ready");
    bool duration_provisional = false;
    bool audio_presence_provisional = false;
    QSize source_size;
    double multicam_sync_marker_seconds = -1.0;
    QString proxy_path;
    QString proxy_status = QStringLiteral("none");
    QString proxy_error;
    bool proxy_enabled = true;
    QString compound_document_path;
    QString compound_render_path;
    QString compound_status = QStringLiteral("none");
    QString compound_error;
    QString frame_mode = QStringLiteral("fit");
    double frame_scale = 1.0;
    double frame_offset_x = 0.0;
    double frame_offset_y = 0.0;
    double frame_rotation_degrees = 0.0;
    double opacity = 1.0;
    double audio_gain_db = 0.0;
    double fade_in_seconds = 0.0;
    double fade_out_seconds = 0.0;
    double playback_rate = 1.0;
    QString retime_pitch_strategy = QStringLiteral("preserve");
    QString retime_interpolation_mode = QStringLiteral("frame-sampling");
    bool effects_enabled = true;
    QStringList effect_stack;
    QHash<QString, ClipEffectState> effect_states;
    bool stabilization_enabled = false;
    double stabilization_smoothing = 0.55;
    double stabilization_crop_protection = 0.50;
    bool rolling_shutter_enabled = false;
    double rolling_shutter_amount = 0.0;
    QString stabilization_analysis_state = QStringLiteral("not-analyzed");
    QString stabilization_transform_path;
    QString stabilization_render_path;
    QString stabilization_preview_path;
    QString stabilization_analysis_source_signature;
    double stabilization_analysis_trim_start_seconds = 0.0;
    double stabilization_analysis_trim_end_seconds = 0.0;
    QString color_lut_path;
    bool color_lut_enabled = true;
    double shot_match_red_gain = 1.0;
    double shot_match_green_gain = 1.0;
    double shot_match_blue_gain = 1.0;
    QVector<ClipKeyframe> keyframes;
    QString transition_in_name;
    QString transition_out_name;
    double transition_in_seconds = 0.0;
    double transition_out_seconds = 0.0;
    QString title_text;
    QString title_template_key = QStringLiteral("basic");
    QString title_font_family = QStringLiteral("Sans Serif");
    double title_font_size = 64.0;
    QString title_color = QStringLiteral("#ffffffff");
    QString title_alignment = QStringLiteral("center");
    double title_position_x = 0.5;
    double title_position_y = 0.82;
    QString title_background_color = QStringLiteral("#99000000");
    QString caption_style_template_id = QStringLiteral("clean-subtitle");
    QString caption_casing = QStringLiteral("none");
    QString caption_stroke_color = QStringLiteral("#ff000000");
    double caption_stroke_width = 0.0;
    bool caption_shadow_enabled = false;
    QString caption_shadow_color = QStringLiteral("#99000000");
    double caption_shadow_offset_x = 2.0;
    double caption_shadow_offset_y = 3.0;
    double caption_background_padding = 14.0;
    double caption_background_radius = 8.0;
    double caption_safe_margin_x = 0.08;
    double caption_safe_margin_y = 0.08;
    QString caption_emphasis_mode = QStringLiteral("highlight");
    bool caption_emphasis_reduced_motion = false;
    QVector<CaptionWord> caption_words;
    QString silence_analysis_status = QStringLiteral("not-analyzed");
    QVector<SilenceRange> audio_silence_ranges;
    QHash<QString, OutputVariantOverride> output_variant_overrides;
  };

  struct DialogueCleanupState final {
    bool enabled = false;
    double voice_isolation_amount = 0.65;
    double noise_reduction_amount = 0.35;
    double dereverb_amount = 0.25;
    double hum_removal_amount = 0.50;
    int hum_frequency_hz = 60;
    double artifact_smoothing = 0.65;
    double ambience_preservation = 0.35;
  };

  struct DialogueCleanupPreview final {
    DialogueCleanupState state;
    bool audition_original = false;
  };

  struct Track final {
    QString track_id;
    QString display_label;
    TrackKind kind = TrackKind::Video;
    QString role = QStringLiteral("media");
    int lane_index = 0;
    bool locked = false;
    bool muted = false;
    bool solo = false;
    bool visible = true;
    bool targeted = false;
    double opacity = 1.0;
    double audio_gain_db = 0.0;
    double audio_pan = 0.0;
    QString audio_role = QStringLiteral("dialogue");
    QString audio_bus = QStringLiteral("main");
    double audio_eq_low_db = 0.0;
    double audio_eq_mid_db = 0.0;
    double audio_eq_high_db = 0.0;
    bool audio_eq_enabled = true;
    bool audio_compressor_enabled = false;
    double audio_compressor_threshold_db = -18.0;
    double audio_compressor_ratio = 4.0;
    double audio_compressor_makeup_db = 0.0;
    bool audio_limiter_enabled = false;
    double audio_limiter_ceiling_db = -1.0;
    bool audio_gate_enabled = false;
    double audio_gate_threshold_db = -48.0;
    bool audio_deesser_enabled = false;
    double audio_deesser_amount = 0.35;
    bool audio_high_pass_enabled = false;
    double audio_high_pass_hz = 80.0;
    bool audio_low_pass_enabled = false;
    double audio_low_pass_hz = 16000.0;
    bool audio_processing_bypassed = false;
    bool audio_loudness_matched_bypass = true;
    DialogueCleanupState dialogue_cleanup;
    bool audio_ducking_enabled = false;
    double audio_ducking_amount_db = -12.0;
    QVector<ClipKeyframe> keyframes;
    QVector<Clip> clips;
  };

  struct Marker final {
    QString marker_id;
    QString label;
    QString note;
    QString status = QStringLiteral("todo");
    QString assignee;
    QString approval_state = QStringLiteral("open");
    bool review_note = false;
    bool completed = false;
    QString color = QStringLiteral("#dfb8a8");
    double seconds = 0.0;
  };

  struct MulticamCut final {
    double sequence_seconds = 0.0;
    QString clip_id;
  };

  struct MulticamGroup final {
    QString group_id;
    QString label;
    QString sync_method = QStringLiteral("timecode");
    QString audio_mode = QStringLiteral("follow-video");
    int audio_angle_index = 0;
    QVector<QString> angle_clip_ids;
    QVector<MulticamCut> cuts;
  };

  struct RenderCacheTask final {
    double start_seconds = 0.0;
    double end_seconds = 0.0;
    QString signature;
    QString output_path;
  };

  struct MediaBin final {
    QString bin_id;
    QString parent_bin_id;
    QString name;
  };

  struct SmartCollection final {
    QString collection_id;
    QString name;
    QVariantMap rules;
  };

  struct ReviewRange final {
    QString range_id;
    QString name;
    double start_seconds = 0.0;
    double end_seconds = 0.0;
    double preroll_seconds = 0.0;
    double postroll_seconds = 0.0;
  };

  struct TextBasedEditOperation final {
    QString operation_id;
    QString kind;
    double start_seconds = 0.0;
    double end_seconds = 0.0;
    double destination_seconds = -1.0;
  };

  struct TextBasedEditBlock final {
    double original_start_seconds = 0.0;
    double original_end_seconds = 0.0;
    double new_start_seconds = -1.0;
  };

  struct TextBasedEditPlan final {
    bool valid = false;
    QString reason;
    QVector<double> boundaries;
    QVector<TextBasedEditBlock> blocks;
    QVector<int> ordered_block_indices;
    double original_duration_seconds = 0.0;
    double preview_duration_seconds = 0.0;
    double deleted_duration_seconds = 0.0;
  };

  struct ClipboardClip final {
    Clip clip;
    int lane_index = 0;
    double relative_start_seconds = 0.0;
  };

  struct MediaAssetMetadata final {
    int rating = 0;
    QString disposition = QStringLiteral("none");
    QString color_label = QStringLiteral("none");
    QStringList tags;
    QStringList detected_on_screen_text;
    QString notes;
    QVariantMap custom_fields;
  };

  struct WaveformRequest final {
    QString clip_id;
    QString file_path;
    QString cache_key;
    QString request_key;
  };

  struct TranscriptionRequest final {
    QString file_path;
    QString media_revision_key;
    QString document_revision_key;
  };

  struct MediaAnalysisRequest final {
    QString file_path;
    QString revision_key;
    QString cache_path;
  };

  struct SourceIntegrityRequest final {
    QString file_path;
    QString revision_key;
  };

  struct DeferredMediaHydrationRequest final {
    QString clip_id;
    QString file_path;
    bool integrity = false;
    bool analysis = false;
    bool waveform = false;
  };

  struct SilenceAnalysisRequest final {
    QString clip_id;
    QString file_path;
    double trim_start_seconds = 0.0;
    double trim_end_seconds = 0.0;
  };

  struct PosterRequest final {
    QString file_path;
    QString revision_key;
    QString output_path;
  };

  struct MediaHoverFrameRequest final {
    QString asset_id;
    QString file_path;
    QString revision_key;
    QString request_key;
    QString output_path;
    double source_seconds = 0.0;
  };

  struct SourceTapeItem final {
    QString asset_id;
    QString file_path;
    QString display_label;
    QString poster_source;
    QString media_type;
    double duration_seconds = 0.0;
    double source_in_seconds = 0.0;
    double source_out_seconds = 0.0;
  };

  struct CacheBudgetSettings final {
    bool automatic = true;
    qint64 disk_bytes = 0;
    qint64 cpu_memory_bytes = 0;
    qint64 gpu_memory_bytes = 0;
    qint64 working_memory_bytes = 0;
  };

  struct Snapshot final {
    QVector<Track> tracks;
    QVector<Marker> markers;
    QVector<MulticamGroup> multicam_groups;
    QVector<MediaBin> media_bins;
    QVector<SmartCollection> smart_collections;
    QVector<ReviewRange> review_ranges;
    QHash<QString, QString> media_asset_bins;
    QHash<QString, MediaAssetMetadata> media_asset_metadata;
    QSet<QString> removed_media_asset_ids;
    QString active_media_bin_id;
    QString active_smart_collection_id;
    QString selected_clip_id;
    QStringList selected_clip_ids;
    double sequence_playhead_seconds = 0.0;
    QSize sequence_size;
    double sequence_frame_rate = 30.0;
    int audio_sample_rate = 48000;
    QString project_binding;
    QString color_space;
    QString data_levels;
    QString export_container;
    QString export_codec;
    QString export_preset;
    QString export_encoder_mode;
    bool export_two_pass = false;
    bool export_alpha = false;
    bool export_audio_stems = false;
    bool export_image_sequence = false;
    QString active_output_variant;
    CacheBudgetSettings cache_budgets;
    QHash<QString, QByteArray> external_document_bytes;
  };

  struct CompoundRenderRequest final {
    QString clip_id;
    QString document_path;
    QString render_path;
  };

  struct CompoundNavigationFrame final {
    QString parent_snapshot_path;
    QString parent_saved_document_path;
    QString parent_saved_render_path;
    QString parent_saved_poster_path;
    QString compound_clip_id;
    QString compound_document_path;
    QString compound_render_path;
    QString compound_label;
    QVector<Snapshot> parent_undo_stack;
    QVector<Snapshot> parent_redo_stack;
  };

  struct ClipRef final {
    int track_index = -1;
    int clip_index = -1;
    [[nodiscard]] bool isValid() const { return track_index >= 0 && clip_index >= 0; }
  };

  struct ClipFileProjectionState final {
    QString playback_path;
    bool source_online = false;
    bool proxy_ready = false;
    bool using_proxy = false;
  };

  struct ClipMapContext final {
    QHash<QString, int> flattened_index_by_clip_id;
    QHash<QString, int> track_row_by_id;
    QHash<int, const Track*> audio_track_by_lane;
    QHash<QString, DialogueCleanupState> dialogue_cleanup_by_track_id;
    QHash<QString, double> evaluated_track_opacity_by_id;
    QHash<QString, double> evaluated_track_audio_gain_by_id;
    QHash<QString, double> evaluated_track_audio_pan_by_id;
    QHash<QString, double> preview_duck_gain_by_track_id;
    QHash<int, QString> duration_text_by_rounded_seconds;
    QHash<QString, int> linked_group_counts;
    QHash<QString, ClipFileProjectionState> file_state_by_clip_id;
    bool any_audio_solo = false;
  };

  [[nodiscard]] QVariantMap clipMap(int index) const;
  [[nodiscard]] QVariantMap clipMap(const Clip& clip, int flattened_index, const Track& track) const;
  [[nodiscard]] QVariantMap clipMap(
      const Clip& clip,
      int flattened_index,
      const Track& track,
      const ClipMapContext* context) const;
  [[nodiscard]] QVariantMap trackMap(const Track& track, int track_index) const;
  [[nodiscard]] QVariantMap trackMap(
      const Track& track,
      int track_index,
      const ClipMapContext& context) const;
  [[nodiscard]] ClipMapContext clipMapContext(
      const QVector<ClipRef>& ordered_refs) const;
  [[nodiscard]] bool isVideoFile(const QString& file_path) const;
  [[nodiscard]] bool isAudioFile(const QString& file_path) const;
  [[nodiscard]] bool isImageFile(const QString& file_path) const;
  [[nodiscard]] QString mediaTypeForFile(const QString& file_path) const;
  [[nodiscard]] QVector<ClipRef> orderedVideoClipRefs() const;
  [[nodiscard]] QVector<ClipRef> orderedSelectableClipRefs() const;
  [[nodiscard]] ClipRef clipRefAt(int index) const;
  [[nodiscard]] ClipRef selectedClipRef() const;
  [[nodiscard]] const Clip* clipAt(int index) const;
  [[nodiscard]] Clip* clipAt(int index);
  [[nodiscard]] const Clip* clipAt(const ClipRef& ref) const;
  [[nodiscard]] Clip* clipAt(const ClipRef& ref);
  [[nodiscard]] const Track* trackAt(int track_index) const;
  [[nodiscard]] Track* trackAt(int track_index);
  [[nodiscard]] const Track* primaryVideoTrack() const;
  [[nodiscard]] Track* primaryVideoTrack();
  [[nodiscard]] const Track* videoTrackForLane(int lane_index) const;
  [[nodiscard]] Track* videoTrackForLane(int lane_index);
  [[nodiscard]] const Track* audioTrackForLane(int lane_index) const;
  [[nodiscard]] Track* audioTrackForLane(int lane_index);
  [[nodiscard]] const Track* videoTrackForRowIndex(int track_row_index) const;
  [[nodiscard]] Track* videoTrackForRowIndex(int track_row_index);
  [[nodiscard]] Track* defaultTargetVideoTrack();
  [[nodiscard]] Track* defaultTargetAudioTrack();
  [[nodiscard]] const Track* pairedAudioTrack(const Track& video_track) const;
  [[nodiscard]] Track* pairedAudioTrack(const Track& video_track);
  [[nodiscard]] const Track* targetedTrack(TrackKind kind) const;
  [[nodiscard]] Track* targetedTrack(TrackKind kind);
  [[nodiscard]] int resolvedAudioLane(const Clip& clip, int video_lane_index) const;
  [[nodiscard]] ClipRef activeVideoClipRefForTime(double seconds) const;
  [[nodiscard]] ClipRef nextActiveVideoClipRefAfter(double seconds) const;
  [[nodiscard]] int flattenedIndexForClipId(const QString& clip_id) const;
  [[nodiscard]] int nextLaneIndex() const;
  [[nodiscard]] double clipTrimmedDuration(const Clip& clip) const;
  [[nodiscard]] double clipTrimmedDuration(int index) const;
  [[nodiscard]] double clipEndSeconds(const Clip& clip) const;
  [[nodiscard]] double clipTimelineOffsetSeconds(int index) const;
  [[nodiscard]] double trackEndSeconds(const Track& track) const;
  [[nodiscard]] double totalDurationSeconds() const;
  [[nodiscard]] double clipLocalSecondsForSequenceTime(const Clip& clip, double seconds) const;
  [[nodiscard]] double clipTimelineSecondsForSourceTime(
      const Clip& clip,
      double source_seconds,
      double preferred_local_seconds = -1.0) const;
  [[nodiscard]] double clipRetimeRateForSequenceTime(const Clip& clip, double seconds) const;
  [[nodiscard]] QVector<ClipKeyframe> clipRetimeKeyframes(const Clip& clip) const;
  [[nodiscard]] QStringList clipRetimeSourceLimitWarnings(const Clip& clip) const;
  [[nodiscard]] bool clipHasRetimeCurve(const Clip& clip) const;
  void rippleClipDurationChange(Clip& clip, double previous_end_seconds);
  void trimRetimeCurveStart(Clip& clip, double timeline_delta_seconds);
  void trimRetimeCurveEnd(Clip& clip, double timeline_duration_seconds);
  [[nodiscard]] QString normalizedKeyframeParameter(const QString& parameter) const;
  [[nodiscard]] QString normalizedKeyframeInterpolation(const QString& interpolation) const;
  [[nodiscard]] double clipParameterBaseValue(const Clip& clip, const QString& parameter) const;
  [[nodiscard]] double boundedClipParameterValue(const QString& parameter, double value) const;
  [[nodiscard]] double evaluatedClipParameter(
      const Clip& clip,
      const QString& parameter,
      double local_seconds) const;
  [[nodiscard]] double selectedClipKeyframeLocalSeconds(const Clip& clip) const;
  void reflowRovingClipKeyframes(Clip& clip, const QString& parameter) const;
  [[nodiscard]] bool clipHasKeyframesFor(const Clip& clip, const QString& parameter) const;
  [[nodiscard]] double trackParameterBaseValue(const Track& track, const QString& parameter) const;
  [[nodiscard]] double evaluatedTrackParameter(
      const Track& track,
      const QString& parameter,
      double sequence_seconds) const;
  [[nodiscard]] bool trackHasKeyframesFor(const Track& track, const QString& parameter) const;
  [[nodiscard]] DialogueCleanupState normalizedDialogueCleanupState(
      const QVariantMap& settings,
      const DialogueCleanupState& fallback) const;
  [[nodiscard]] DialogueCleanupState effectiveDialogueCleanupState(
      const Track& track) const;
  bool setSelectedClipAnimatedValue(const QString& parameter, double value);
  bool replaceSelectedClipFromSourceImpl(bool fit_to_fill);
  [[nodiscard]] QString formatDurationText(double seconds) const;
  [[nodiscard]] QVariantMap markerMap(const Marker& marker) const;
  [[nodiscard]] QVariantMap multicamGroupMap(const MulticamGroup& group) const;
  [[nodiscard]] const MulticamGroup* multicamGroupForClip(const QString& clip_id) const;
  [[nodiscard]] MulticamGroup* multicamGroupById(const QString& group_id);
  [[nodiscard]] const MulticamGroup* multicamGroupById(const QString& group_id) const;
  [[nodiscard]] QString multicamActiveClipIdAt(
      const MulticamGroup& group,
      double sequence_seconds) const;
  [[nodiscard]] bool multicamAngleActiveAt(
      const QString& clip_id,
      double sequence_seconds) const;
  [[nodiscard]] bool multicamAngleAudioActiveAt(
      const QString& clip_id,
      double sequence_seconds) const;
  [[nodiscard]] ClipRef programClipRef() const;
  void pruneMulticamGroups();
  void requestMediaAnalysis(const QString& file_path);
  void requestSourceIntegrity(const QString& file_path);
  void startNextSourceIntegrity();
  void finishActiveSourceIntegrity();
  void scheduleIncrementalMediaHydration();
  void processIncrementalMediaHydrationBatch();
  void cancelIncrementalMediaHydration();
  void startNextMediaAnalysis();
  void finishActiveMediaAnalysis(bool succeeded);
  void applyMediaAnalysis(
      const QString& file_path,
      double duration_seconds,
      bool has_audio,
      int audio_channels,
      const QString& audio_channel_layout,
      const QSize& source_size,
      const QString& status);
  void startNextSilenceAnalysis();
  void finishActiveSilenceAnalysis(bool succeeded);
  void startNextTranscription();
  [[nodiscard]] QString transcriptionRevisionIdentity() const;
  void finishActiveTranscription(
      bool succeeded,
      const QVariantList& words = QVariantList{},
      const QString& error_message = QString());
  void completeActiveTranscription(
      bool succeeded,
      const QVariantList& words = QVariantList{},
      const QString& error_message = QString());
  bool commitTranscriptionWords();
  void startNextBeatAnalysis();
  void finishActiveBeatAnalysis(bool succeeded);
  void publishBeatSuggestions();
  [[nodiscard]] OutputVariantOverride outputVariantOverrideForClip(const Clip& clip) const;
  bool applyTitleTemplateToClip(Clip* clip, const QString& template_key, bool replace_text);
  void captureOutputVariantOverrides(const QString& variant_key);
  void applyOutputVariantOverrides(const QString& variant_key);
  void requestVideoPoster(const QString& file_path);
  void startNextVideoPoster();
  void finishActiveVideoPoster(bool succeeded);
  void startNextMediaHoverFrame();
  void finishActiveMediaHoverFrame(bool succeeded);
  void saveCurrentSourceTapeMarks();
  void applyVideoPoster(const QString& file_path, const QString& poster_url);
  [[nodiscard]] QString ensureVideoPoster(const QString& file_path, const QString& cache_key = QString()) const;
  [[nodiscard]] QString cachedAudioWaveform(const QString& file_path, const QString& cache_key = QString()) const;
  [[nodiscard]] QVariantMap cachedAudioWaveformSources(
      const QString& file_path,
      const QString& cache_key = QString()) const;
  [[nodiscard]] QVector<double> waveformPeakFractions(const QString& waveform_url) const;
  void requestAudioWaveform(const QString& clip_id, const QString& file_path, const QString& cache_key = QString());
  void startNextAudioWaveform();
  void finishActiveAudioWaveform(bool succeeded);
  void applyAudioWaveform(const QString& clip_id, const QString& file_path, const QString& waveform_url);
  [[nodiscard]] QString proxyCachePath(const Clip& clip) const;
  [[nodiscard]] QString playbackFilePath(const Clip& clip) const;
  bool queueProxyGeneration(const QString& clip_id);
  void startNextProxyGeneration();
  void finishActiveProxyGeneration(bool succeeded, const QString& error_message = QString());
  [[nodiscard]] QVector<RenderCacheTask> expectedRenderCacheTasks() const;
  [[nodiscard]] QString renderCacheSignature(double start_seconds, double end_seconds) const;
  [[nodiscard]] QString renderCacheRevisionIdentity() const;
  [[nodiscard]] QHash<QString, QSet<QString>> projectCacheArtifactPaths() const;
  [[nodiscard]] QSet<QString> otherProjectCacheReferences() const;
  [[nodiscard]] bool cacheCategoryBusy(const QString& category) const;
  [[nodiscard]] CacheBudgetSettings automaticCacheBudgets() const;
  [[nodiscard]] CacheBudgetSettings effectiveCacheBudgets() const;
  [[nodiscard]] QString detectedCachePressureLevel() const;
  [[nodiscard]] qint64 timelineMemoryCacheUsageBytes() const;
  [[nodiscard]] qint64 estimatedGpuCacheUsageBytes() const;
  void applyCachePressureResponse();
  void startNextRenderCacheExtraction();
  [[nodiscard]] bool shouldDeferBackgroundWork(
      const QString& workflow,
      const QString& affected_clip_id = QString()) const;
  [[nodiscard]] int backgroundWorkRankForClip(const QString& clip_id) const;
  [[nodiscard]] QString clipIdForMediaPath(const QString& file_path) const;
  void reprioritizeBackgroundWorkQueues();
  void resumeDeferredBackgroundWork();
  void recordStaleResultDiscard(
      const QString& workflow,
      const QString& requested_revision,
      const QString& current_revision,
      const QString& affected_item_id = QString());
  void finishRenderCacheGeneration(bool succeeded, const QString& error_message = QString());
  void pruneRenderCacheFiles() const;
  bool startCompoundRender(
      const QString& clip_id,
      const QString& document_path,
      const QString& render_path);
  void finishCompoundRender(bool succeeded, const QString& error_message = QString());
  void clearCompoundNavigation();
  [[nodiscard]] QString ensureTitleFrame(
      const Clip& clip,
      const QSize& target_size,
      double caption_local_seconds = -1.0) const;
  [[nodiscard]] QSize probeVideoSize(const QString& file_path) const;
  [[nodiscard]] bool renderTimelineVideo(const QString& output_path) const;
  [[nodiscard]] QStringList timelineRenderArguments(
      const QString& output_path,
      QString* error_message = nullptr,
      bool audio_analysis_mode = false,
      const QString& output_kind = QStringLiteral("master"),
      const QString& audio_bus_filter = QString(),
      const QString& encoder_override = QString(),
      int render_pass = 0) const;
  [[nodiscard]] bool runTimelineFfmpeg(
      const QStringList& arguments,
      QString* error_message = nullptr) const;
  [[nodiscard]] bool ffmpegEncoderAvailable(const QString& encoder) const;
  [[nodiscard]] QString resolvedDeliveryEncoder() const;
  [[nodiscard]] QStringList deliveryAudioBuses() const;
  void startLoudnessMeasurement();
  void finishLoudnessAnalysis(bool succeeded, const QString& output = QString());
  void invalidateLoudnessAnalysis();
  void resetAutoReframeAnalysis();
  void resetStabilizationAnalysis();
  void startStabilizationAnalysisStage(const QString& stage);
  void finishStabilizationAnalysisStage(bool succeeded, const QString& error_message);
  void updateStabilizationAnalysisProgress(const QByteArray& progress_output);
  [[nodiscard]] bool clipStabilizationCacheReady(const Clip& clip) const;
  [[nodiscard]] QString stabilizationRollingShutterFilter(const Clip& clip) const;
  [[nodiscard]] QString stabilizationTransformFilter(const Clip& clip) const;
  [[nodiscard]] QJsonObject documentObject() const;
  [[nodiscard]] bool persistDocument(const QString& document_path) const;
  [[nodiscard]] QString editJournalRootPath() const;
  bool appendEditJournalEntry(const QString& reason);
  [[nodiscard]] QJsonObject recoveryDocumentForCandidate(
      const QString& candidate_id,
      QVariantMap* metadata = nullptr) const;
  [[nodiscard]] QString sequenceVersionsRootPath() const;
  [[nodiscard]] QString sequenceVersionDocumentPath(const QString& version_id) const;
  bool createSequenceVersionInternal(
      const QString& label,
      const QString& kind,
      const QString& notes = QString(),
      const QString& author = QString(),
      bool protected_milestone = false,
      const QJsonObject& source_document = QJsonObject(),
      const QString& duplicated_from_version_id = QString());
  [[nodiscard]] QString clipVideoFilter(
      const Clip& clip,
      const Track& track,
      int input_index,
      int output_index,
      const QSize& target_size,
      double sequence_start_seconds,
      double segment_playback_rate = 0.0,
      bool reverse_segment = false,
      bool freeze_segment = false,
      double segment_duration_seconds = 0.0) const;
  [[nodiscard]] QString clipParameterFfmpegExpression(
      const Clip& clip,
      const QString& parameter,
      double sequence_start_seconds,
      const QString& time_variable = QStringLiteral("t")) const;
  [[nodiscard]] QString trackParameterFfmpegExpression(
      const Track& track,
      const QString& parameter,
      double sequence_start_seconds,
      const QString& time_variable = QStringLiteral("t")) const;
  [[nodiscard]] QString clipEffectsFilter(
      const Clip& clip,
      double sequence_start_seconds) const;
  [[nodiscard]] QString clipColorFinishingFilter(const Clip& clip) const;
  [[nodiscard]] Clip buildGalleryClip(
      const QString& asset_id,
      const QString& file_path,
      const QString& display_label,
      double duration_seconds,
      double trim_start_seconds,
      double trim_end_seconds,
      const QString& poster_source) const;
  void insertClipFromGalleryResolved(
      const QString& asset_id,
      const QString& file_path,
      const QString& display_label,
      double duration_seconds,
      double trim_start_seconds,
      double trim_end_seconds,
      const QString& poster_source,
      int has_audio_hint,
      int target_track_row = -1,
      double insert_start_seconds = -1.0);
  void overwriteClipFromGalleryResolved(
      const QString& asset_id,
      const QString& file_path,
      const QString& display_label,
      double duration_seconds,
      double trim_start_seconds,
      double trim_end_seconds,
      const QString& poster_source,
      int has_audio_hint);
  void shiftTrackClipsFrom(Track* track, double sequence_seconds, double delta_seconds, const QString& excluded_clip_id = QString());
  void shiftAllTrackClipsFrom(double sequence_seconds, double delta_seconds, const QString& excluded_clip_id = QString());
  void shiftMarkersFrom(double sequence_seconds, double delta_seconds);
  void splitTrackClipAtSequenceSeconds(Track* track, int clip_index, double sequence_seconds);
  bool extractSequenceRangeInternal(double start_seconds, double end_seconds, bool push_undo_snapshot);
  [[nodiscard]] QVariantMap buildTextBasedEditPreview(
      const QVector<TextBasedEditOperation>& operations) const;
  [[nodiscard]] TextBasedEditPlan buildTextBasedEditPlan(
      const QVector<TextBasedEditOperation>& operations) const;
  void overwriteTrackRange(Track* track, double range_start_seconds, double range_end_seconds);
  void initializeDefaultTracks();
  void appendTrackPair();
  Track* appendCaptionTrack();
  [[nodiscard]] Track* captionTrack();
  [[nodiscard]] const Track* captionTrack() const;
  void syncAudioTracks();
  [[nodiscard]] bool shouldEditSelectedClipIndependently() const;
  bool detachSelectedClipAudio(bool push_undo_snapshot);
  void refreshSequenceSizeFromContent();
  void updateCurrentIndexFromSelection();
  void ensureCurrentIndexValid();
  void setStatus(const QString& status);
  void setDirty(bool dirty);
  void clampPlaybackState();
  void emitAllState();
  void emitEditorState();
  void emitSourceMonitorState();
  void emitMarkersState();
  [[nodiscard]] bool isClipSelected(const QString& clip_id) const;
  [[nodiscard]] QVector<Track> copyTracksForSnapshot() const;
  [[nodiscard]] Snapshot captureSnapshot(
      const QHash<QString, QByteArray>& external_documents = {}) const;
  [[nodiscard]] bool restoreSnapshot(const Snapshot& snapshot);
  [[nodiscard]] int maxUndoDepthForCurrentProject() const;
  void pushUndoSnapshot();
  [[nodiscard]] bool restoreExternalDocuments(
      const QHash<QString, QByteArray>& documents);
  void loadEffectStackPresets();
  [[nodiscard]] bool persistEffectStackPresets() const;
  void loadCaptionStyleTemplates();
  [[nodiscard]] bool persistCaptionStyleTemplates() const;
  [[nodiscard]] QVariantMap captionStyleMapForClip(const Clip& clip) const;
  void applyCaptionStyleMapToClip(const QVariantMap& style, Clip* clip) const;

  static constexpr int kMaxUndoDepth = 50;
  static constexpr int kMinLargeProjectUndoDepth = 8;
  static constexpr int kMaxUndoSnapshotClipEntries = 80000;

  bool has_active_timeline_ = false;
  bool continuous_edit_active_ = false;
  QVector<Track> tracks_;
  QVector<Marker> markers_;
  QVector<MulticamGroup> multicam_groups_;
  QVector<MediaBin> media_bins_;
  QVector<SmartCollection> smart_collections_;
  QVector<ReviewRange> review_ranges_;
  QVector<TextBasedEditOperation> pending_text_based_edits_;
  QVariantList effect_stack_presets_;
  QString effect_stack_preset_migration_report_;
  QVariantList caption_style_templates_;
  QString pending_text_based_edit_document_hash_;
  QHash<QString, QString> media_asset_bins_;
  QHash<QString, MediaAssetMetadata> media_asset_metadata_;
  QHash<QString, DialogueCleanupPreview> dialogue_cleanup_previews_;
  QSet<QString> removed_media_asset_ids_;
  QString active_media_bin_id_;
  QString active_smart_collection_id_;
  QString timeline_id_;
  QString timeline_name_ = QStringLiteral("Untitled Timeline");
  QString status_ = QStringLiteral("Create a timeline to begin editing.");
  QString preview_source_;
  QString saved_document_path_;
  QString saved_render_path_;
  QString saved_poster_path_;
  QString created_at_;
  QString selected_clip_id_;
  QStringList selected_clip_ids_;
  QVector<Snapshot> undo_stack_;
  QVector<Snapshot> redo_stack_;
  QVector<ClipKeyframe> keyframe_clipboard_;
  QVector<ClipboardClip> clip_clipboard_;
  double clip_clipboard_duration_seconds_ = 0.0;
  int clip_clipboard_origin_video_lane_ = 0;
  int clip_clipboard_origin_audio_lane_ = 0;
  QSize sequence_size_ = QSize(1080, 1350);
  double sequence_frame_rate_ = 30.0;
  int audio_sample_rate_ = 48000;
  QString project_binding_;
  QString color_space_ = QStringLiteral("Rec.709");
  QString data_levels_ = QStringLiteral("Legal");
  QString export_container_ = QStringLiteral("mp4");
  QString export_codec_ = QStringLiteral("h264");
  QString export_preset_ = QStringLiteral("instagram");
  QString export_encoder_mode_ = QStringLiteral("software");
  bool export_two_pass_ = false;
  bool export_alpha_ = false;
  bool export_audio_stems_ = false;
  bool export_image_sequence_ = false;
  QString background_render_resource_policy_;
  CacheBudgetSettings cache_budgets_;
  QString cache_pressure_level_ = QStringLiteral("normal");
  QString cache_pressure_override_for_testing_;
  QTimer cache_pressure_timer_;
  int cache_pressure_response_count_ = 0;
  qint64 cache_pressure_last_reclaimed_bytes_ = 0;
  QString active_output_variant_ = QStringLiteral("portrait");
  int current_index_ = -1;
  bool dirty_ = false;
  QTimer autosave_timer_;
  QTimer edit_journal_timer_;
  QTimer incremental_load_timer_;
  QVector<DeferredMediaHydrationRequest> deferred_media_hydration_queue_;
  int incremental_load_total_count_ = 0;
  int incremental_load_completed_count_ = 0;
  bool bulk_media_hydration_scheduling_ = false;
  bool bulk_media_hydration_changed_ = false;
  bool background_work_workspace_visible_ = false;
  bool background_work_playback_active_ = false;
  bool background_work_interaction_active_ = false;
  QSet<QString> background_work_visible_clip_ids_;
  qint64 background_work_intent_generation_ = 0;
  QVector<MediaAnalysisRequest> media_analysis_queue_;
  QSet<QString> media_analysis_requests_in_flight_;
  QProcess* active_media_analysis_process_ = nullptr;
  MediaAnalysisRequest active_media_analysis_request_;
  QVector<SourceIntegrityRequest> source_integrity_queue_;
  QSet<QString> source_integrity_requests_in_flight_;
  QSet<QString> source_integrity_completed_revisions_;
  QFutureWatcher<QVariantMap>* source_integrity_watcher_ = nullptr;
  SourceIntegrityRequest active_source_integrity_request_;
  QVector<SilenceAnalysisRequest> silence_analysis_queue_;
  QProcess* active_silence_analysis_process_ = nullptr;
  SilenceAnalysisRequest active_silence_analysis_request_;
  QString silence_analysis_status_ = QStringLiteral("Audio silence has not been analyzed.");
  bool silence_analysis_cancel_requested_ = false;
  std::unique_ptr<TimelineSpeechState> speech_state_;
  QFutureWatcher<QVariantMap>* transcription_diarization_watcher_ = nullptr;
  std::shared_ptr<std::atomic_bool> transcription_diarization_cancel_token_;
  QVector<TranscriptionRequest> transcription_queue_;
  TranscriptionRequest active_transcription_request_;
  QString active_transcription_path_;
  QVariantList accumulated_transcription_words_;
  QString transcription_status_ = QStringLiteral("Audio has not been transcribed.");
  QString transcription_language_ = QStringLiteral("en-US");
  bool transcription_speaker_diarization_ = true;
  bool transcription_cancel_requested_ = false;
  QStringList beat_analysis_queue_;
  QString active_beat_analysis_path_;
  QProcess* active_beat_analysis_process_ = nullptr;
  QHash<QString, QVector<double>> pending_beat_source_times_;
  QVariantList beat_suggestions_;
  QString beat_analysis_status_ = QStringLiteral("Audio beats have not been analyzed.");
  bool beat_analysis_cancel_requested_ = false;
  QProcess* loudness_analysis_process_ = nullptr;
  QString loudness_analysis_stage_;
  QString loudness_analysis_temporary_path_;
  QString loudness_analysis_status_ = QStringLiteral("Timeline loudness has not been measured.");
  bool loudness_analysis_cancel_requested_ = false;
  bool loudness_measured_ = false;
  double loudness_integrated_lufs_ = 0.0;
  double loudness_true_peak_dbtp_ = 0.0;
  double loudness_range_lu_ = 0.0;
  double loudness_threshold_lufs_ = 0.0;
  double loudness_target_offset_db_ = 0.0;
  QFutureWatcher<QVariantMap>* auto_reframe_watcher_ = nullptr;
  std::shared_ptr<std::atomic_bool> auto_reframe_cancel_token_;
  QString auto_reframe_clip_id_;
  QString auto_reframe_status_ = QStringLiteral("Select a video clip to analyze its subject motion.");
  QVariantList auto_reframe_suggestions_;
  QFutureWatcher<QVariantMap>* program_scope_watcher_ = nullptr;
  QVariantMap program_scope_data_;
  QString pending_program_scope_source_;
  int pending_program_scope_revision_ = -1;
  int program_scope_published_revision_ = -1;
  QProcess* stabilization_analysis_process_ = nullptr;
  QString stabilization_analysis_stage_;
  QString stabilization_analysis_clip_id_;
  QString stabilization_analysis_source_path_;
  QString stabilization_analysis_source_signature_;
  QString stabilization_analysis_transform_path_;
  QString stabilization_analysis_render_path_;
  QString stabilization_analysis_preview_path_;
  QString stabilization_analysis_temporary_path_;
  double stabilization_analysis_trim_start_seconds_ = 0.0;
  double stabilization_analysis_trim_end_seconds_ = 0.0;
  double stabilization_analysis_duration_seconds_ = 0.0;
  bool stabilization_analysis_stabilization_enabled_ = false;
  double stabilization_analysis_smoothing_ = 0.55;
  double stabilization_analysis_crop_protection_ = 0.50;
  bool stabilization_analysis_rolling_shutter_enabled_ = false;
  double stabilization_analysis_rolling_shutter_amount_ = 0.0;
  double stabilization_analysis_progress_ = 0.0;
  QString stabilization_analysis_status_ =
      QStringLiteral("Select a video clip to analyze camera motion.");
  QByteArray stabilization_analysis_progress_buffer_;
  bool stabilization_analysis_cancel_requested_ = false;
  bool stabilization_compare_before_ = false;
  QVector<PosterRequest> poster_request_queue_;
  QSet<QString> poster_requests_in_flight_;
  QProcess* active_poster_process_ = nullptr;
  PosterRequest active_poster_request_;
  QVector<MediaHoverFrameRequest> media_hover_frame_queue_;
  QSet<QString> media_hover_frame_requests_in_flight_;
  QProcess* active_media_hover_frame_process_ = nullptr;
  MediaHoverFrameRequest active_media_hover_frame_request_;
  QVector<SourceTapeItem> source_tape_items_;
  int source_tape_index_ = -1;
  QSet<QString> waveform_requests_in_flight_;
  mutable QHash<QString, QVector<double>> waveform_peak_fraction_cache_;
  QVector<WaveformRequest> waveform_request_queue_;
  QProcess* active_waveform_process_ = nullptr;
  WaveformRequest active_waveform_request_;
  QString active_waveform_temporary_path_;
  QStringList proxy_generation_queue_;
  QProcess* active_proxy_process_ = nullptr;
  QString active_proxy_clip_id_;
  QString active_proxy_source_path_;
  QString active_proxy_revision_key_;
  QString active_proxy_output_path_;
  bool active_proxy_cancel_requested_ = false;
  QProcess* render_cache_process_ = nullptr;
  QVector<RenderCacheTask> render_cache_pending_tasks_;
  int render_cache_task_index_ = -1;
  QString render_cache_full_render_path_;
  QString render_cache_partial_path_;
  QString render_cache_revision_identity_;
  QString render_cache_status_ = QStringLiteral("idle");
  QString render_cache_error_;
  bool render_cache_cancel_requested_ = false;
  mutable bool render_cache_tasks_dirty_ = true;
  mutable QVector<RenderCacheTask> render_cache_tasks_cache_;
  int stale_result_discard_count_ = 0;
  QString stale_result_last_workflow_;
  QString stale_result_last_requested_revision_;
  QString stale_result_last_current_revision_;
  QString stale_result_last_affected_item_id_;
  QVector<CompoundNavigationFrame> compound_navigation_stack_;
  QProcess* compound_render_process_ = nullptr;
  QVector<CompoundRenderRequest> compound_render_queue_;
  QString active_compound_clip_id_;
  QString active_compound_document_path_;
  QString active_compound_render_path_;
  bool compound_navigation_loading_ = false;
  double sequence_playhead_seconds_ = 0.0;
  double current_clip_playhead_seconds_ = 0.0;
  bool sequence_playing_ = false;
  QString active_tool_ = QStringLiteral("select");
  QString edit_mode_ = QStringLiteral("insert");
  bool linked_selection_enabled_ = true;
  bool linked_edit_override_ = false;
  Clip source_clip_;
  bool has_source_clip_ = false;
  double source_in_seconds_ = 0.0;
  double source_out_seconds_ = 0.0;
  QString program_source_gang_clip_id_;
  QString program_source_gang_track_id_;
  QString program_source_gang_track_label_;
  QString program_source_gang_status_ = QStringLiteral("Not ganged");
  double program_source_gang_sequence_seconds_ = -1.0;
  double program_source_gang_source_seconds_ = -1.0;
  double program_source_gang_rate_ = 1.0;
  bool program_source_gang_matched_ = false;
};

}  // namespace photo_editor::desktop
