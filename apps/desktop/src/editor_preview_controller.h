#pragma once

#include "customer_edit_access.h"
#include "agent_task_journal.h"
#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <functional>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include "photo_paint_session.h"
#include "photo_fragment_layer_tip_cache.h"
#include <QJsonArray>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QProcess>
#include <QRect>
#include <QRectF>
#include <QRegion>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QThreadPool>
#include <QVector>
#include <QVariantMap>
#include <QVariantList>
#include "async_mask_job.h"
#include "brush_preview_compositor.h"
#include "brush_live_preview_raster.h"
#include "edit_history_snapshot_store.h"
#include "edit_render_policy.h"
#include "edit_recipe_state.h"
#include "editor_recipe_save_queue.h"
#include "editor_recipe_save_journal.h"
#include "editor_preview_texture.h"
#include "layer_session.h"
#include "history_service.h"
#include "histogram_scope_coordinator.h"
#include "histogram_scope_service.h"
#include "histogram_scope_session.h"
#include "mask_session.h"
#include "mask_worker_coordinator.h"
#include "output_lut_publication_telemetry.h"
#include "photo_editor/pipeline/recipe_compiler.h"
#include "preview_publisher.h"
#include "preview_session.h"
#include "preset_service.h"
#include "photo_export_task.h"
#include "render_scheduler.h"
#include "thumbnail_refresh_coordinator.h"
#include "thumbnail_cache_session.h"
#include "thumbnail_presentation_service.h"
#include "thumbnail_refresh_queue_service.h"
#include "vision_segmentation.h"
namespace photo_editor::desktop {
class EditorPreviewImageSink;
class LinkedImageSourceMonitor;
class AgentTaskJournal;
using EditState = EditRecipeState;
struct PreparedEditorRecipePersistence;
struct EditorPreviewControllerTestAccess;
struct RenderRequest final {
  QImage paint_preview_base;
  QString source_asset_id;
  QString recipe_cache_identity;
  QString adjusted_recipe_cache_identity;
  QString file_path;
  QString reference_file_path;
  QString fallback_thumbnail_source;
  QString render_reason;
  QString interaction_mode;
  QString render_mode;
  QString tool_id;
  QString interaction_id;
  int render_priority = 0;
  QString dirty_domain;
  QStringList dirty_stages;
  QRectF dirty_region;
  bool full_invalidation = true;
  bool tile_visible_region_active = false;
  QRectF tile_visible_region;
  int tile_size = 0;
  int tile_padding_px = 0;
  int planned_tile_count = 0;
  int planned_visible_tile_count = 0;
  int planned_adjacent_tile_count = 0;
  int planned_background_tile_count = 0;
  int executed_visible_tile_count = 0;
  int executed_adjacent_tile_count = 0;
  int executed_background_tile_count = 0;
  QString first_executed_tile_priority;
  EditState state;
  EditState comparison_reference_state;
  bool has_comparison_reference_state = false;
  QString rendering_upgrade_reference_model;
  QStringList compare_adjustment_groups;
  QStringList mask_compare_adjustment_groups;
  bool compare_mode = false;
  bool bypass_crop = false;
  bool full_resolution = false;
  int max_edge = 0;
  int jpeg_quality = 90;
  int generation = 0;
  quint64 edit_transaction_generation = 0;
  quint64 layer_source_content_revision = 0;
  qint64 requested_at_msecs = 0;
  qint64 queue_wait_ms = 0;
  qint64 render_latency_budget_ms = 0;
  AsyncMaskJob async_job;
  QVariantMap mask_alpha_trace_fields;
  QString active_target;
  QString active_layer_id;
  QString active_mask_id;
  QString active_mask_set_id;
  int cancelled_render_count = 0;
  QString stale_preview_fallback;
  bool use_in_memory_preview = false;
  bool overlay_only = false;
  QRectF preview_overlay_rect;
  int preview_overlay_max_edge = 0;
  bool draft_interactive = false, allow_texture_patch_only = false, allow_native_srgb_surface = false, allow_native_device_surface = false, require_cpu_preview_image = false;
  bool reuse_layer_transform_prefix = false;
  // Image-provider consumers may copy final display pixels from native output;
  // they do not require CPU materialization of the float working pipeline.
  bool require_cpu_display_image = false;
  bool visible_tile_only = false;
  bool generate_proof_gamut_warning_mask = false;
  bool require_canonical_render_plan = false;
  bool scopes_visible = false;
  QString scope_domain =
      QStringLiteral("final-display");
  QString texture_patch_base_rendered_cache_key;
  std::shared_ptr<std::atomic<int>> latest_generation;
  std::optional<photo_editor::pipeline::RenderPlan> canonical_render_plan;
  std::optional<photo_editor::pipeline::RenderPlan> canonical_scope_plan;
};

struct RenderResult final {
  QString canonical_plan_cache_key;
  QString canonical_recipe_cache_key;
  QString canonical_plan_error;
  QString canonical_scope_plan_cache_key;
  QString canonical_scope_plan_error;
  QString scope_domain =
      QStringLiteral("final-display");
  QString scope_surface_stage;
  QStringList completed_render_stages;
  QVariantMap render_stage_elapsed_ms;
  QVariantMap render_stage_cache_hits;
  QString failed_render_stage;
  qint64 in_flight_render_stage_elapsed_ms = 0;
  qint64 render_plan_overhead_ms = 0;
  qint64 render_executor_cache_bytes = 0;
  bool render_executor_cache_bypassed = false;
  bool render_executor_cache_writes_skipped = false;
  bool geometry_executor_cache_hit = false;
  bool photographic_output_executor_cache_hit = false;
  QString file_path;
  QString preview_source;
  QString reference_preview_source;
  QString display_preview_source;
  QString display_reference_preview_source;
  QString display_preview_overlay_source;
  QString status;
  QString failure_stage;
  QString failure_detail;
  QStringList failure_layer_ids;
  QStringList failure_mask_set_ids;
  QString render_reason;
  QString interaction_mode;
  QString render_mode;
  QString tool_id;
  QString interaction_id;
  int render_priority = 0;
  QString dirty_domain;
  QStringList dirty_stages;
  QRectF dirty_region;
  bool full_invalidation = true;
  bool tile_visible_region_active = false;
  QRectF tile_visible_region;
  int tile_size = 0;
  int tile_padding_px = 0;
  int planned_tile_count = 0;
  int planned_visible_tile_count = 0;
  int planned_adjacent_tile_count = 0;
  int planned_background_tile_count = 0;
  int executed_tile_count = 0;
  int executed_visible_tile_count = 0;
  int executed_adjacent_tile_count = 0;
  int executed_background_tile_count = 0;
  QString first_executed_tile_priority;
  qint64 executed_tile_pixels = 0;
  QRect texture_upload_rect;
  qint64 texture_upload_pixels = 0;
  qint64 texture_patch_base_copy_pixels = 0;
  bool partial_texture_upload_candidate = false;
  QImage texture_upload_image;
  std::shared_ptr<const photo_editor::pipeline::metal::MetalOutputSurface> texture_upload_surface;
  bool texture_patch_base_state_hit = false;
  int adjusted_stage_tile_count = 0;
  qint64 adjusted_stage_pixels = 0;
  qint64 adjusted_stage_elapsed_ms = 0;
  int mask_tile_cache_hit_count = 0;
  int mask_tile_cache_miss_count = 0;
  qint64 crop_geometry_elapsed_ms = 0;
  qint64 output_stage_elapsed_ms = 0;
  qint64 photographic_output_stage_elapsed_ms = 0;
  qint64 output_linear_pack_elapsed_ms = 0;
  qint64 output_color_convert_elapsed_ms = 0;
  OutputLutPublicationTelemetry output_lut;
  qint64 output_stitch_elapsed_ms = 0;
  qint64 output_tile_cache_elapsed_ms = 0;
  qint64 adjustment_linear_stage_elapsed_ms = 0;
  qint64 adjustment_pipeline_elapsed_ms = 0;
  qint64 adjustment_output_elapsed_ms = 0;
  qint64 adjustment_fast_global_ms = 0;
  qint64 adjustment_tone_mask_ms = 0;
  qint64 adjustment_spatial_ms = 0;
  qint64 adjustment_spatial_luminance_ms = 0;
  qint64 adjustment_spatial_texture_blur_ms = 0;
  qint64 adjustment_spatial_clarity_blur_ms = 0;
  qint64 adjustment_spatial_haze_blur_ms = 0;
  qint64 adjustment_spatial_apply_ms = 0;
  qint64 adjustment_post_color_ms = 0;
  qint64 adjustment_retouch_ms = 0;
  qint64 adjustment_user_lut_ms = 0;
  qint64 adjustment_detail_ms = 0;
  qint64 adjustment_detail_oklab_ms = 0;
  qint64 adjustment_detail_sharpen_ms = 0;
  qint64 adjustment_detail_luminance_nr_ms = 0;
  qint64 adjustment_detail_color_nr_ms = 0;
  qint64 adjustment_detail_output_ms = 0;
  qint64 adjustment_final_output_ms = 0;
  qint64 adjustment_final_output_black_point_ms = 0;
  qint64 adjustment_final_output_headroom_ms = 0;
  qint64 overlay_composite_elapsed_ms = 0;
  qint64 histogram_scope_elapsed_ms = 0;
  qint64 adjustment_gpu_command_buffer_count = 0;
  qint64 adjustment_gpu_kernel_dispatch_count = 0;
  qint64 adjustment_gpu_blit_count = 0;
  qint64 adjustment_gpu_cpu_to_gpu_bytes = 0;
  qint64 adjustment_gpu_gpu_to_cpu_bytes = 0;
  qint64 adjustment_gpu_spatial_texture_blur_dispatch_count = 0;
  qint64 adjustment_gpu_spatial_clarity_blur_dispatch_count = 0;
  qint64 adjustment_gpu_spatial_haze_blur_dispatch_count = 0;
  QString rendered_cache_key;
  QString publish_path;
  QImage preview_image;
  QImage reference_preview_image;
  QImage preview_overlay_image;
  QImage histogram_image;
  QImage proof_gamut_warning_mask;
  bool success = false;
  bool cancelled = false;
  bool full_resolution = false;
  bool overlay_only = false;
  bool draft_interactive = false;
  int generation = 0;
  quint64 edit_transaction_generation = 0;
  quint64 layer_source_content_revision = 0;
  int max_edge = 0;
  int source_width = 0;
  int source_height = 0;
  int output_width = 0;
  int output_height = 0;
  int raw_clip_sample_count = 0;
  int raw_shadow_clip_count = 0;
  int raw_highlight_clip_count = 0;
  int decoded_cache_entries = 0;
  qint64 decoded_cache_bytes = 0;
  int linear_cache_entries = 0;
  qint64 linear_cache_bytes = 0;
  int adjusted_cache_entries = 0;
  qint64 adjusted_cache_bytes = 0;
  int rendered_cache_entries = 0;
  qint64 rendered_cache_bytes = 0;
  bool decoded_cache_lookup = false;
  bool decoded_cache_hit = false;
  bool linear_cache_lookup = false;
  bool linear_cache_hit = false;
  bool adjusted_cache_lookup = false;
  bool adjusted_cache_hit = false;
  bool rendered_cache_lookup = false;
  bool rendered_cache_hit = false;
  bool base_rendered_image_hit = false;
  bool disk_cache_lookup = false;
  bool disk_cache_hit = false;
  QString decode_backend;
  QString backend;
  QString fallback_reason;
  qint64 total_elapsed_ms = 0;
  qint64 queue_wait_ms = 0;
  qint64 render_completed_at_msecs = 0;
  qint64 render_latency_budget_ms = 0;
  bool render_within_latency_budget = true;
  AsyncMaskJob async_job;
  QVariantMap mask_alpha_trace_fields;
  QString active_target;
  QString active_layer_id;
  QString active_mask_id;
  QString active_mask_set_id;
  int cancelled_render_count = 0;
  QString stale_preview_fallback;
  qint64 decode_elapsed_ms = 0;
  qint64 edit_elapsed_ms = 0;
  qint64 encode_elapsed_ms = 0;
  bool jpeg_encode_requested = false;
  bool png_encode_requested = false;
  int gpu_readback_count = 0;
  bool had_adjustments = false;
  QRectF preview_overlay_rect;
  bool updated_persistent_sources = false;
};

struct ColorToolMaskPreviewResult final {
  QString cache_key;
  QString data_uri;
  bool cancelled = false;
};

struct ColorToolMaskPreviewRequest final {
  QString cache_key;
  QString file_path;
  EditState state;
  QVariantMap settings;
  std::shared_ptr<std::atomic_bool> cancellation;
};

struct EditIntent final {
  QString operation;
  QString phase = QStringLiteral("commit");
  QString tool_id;
  QString interaction_id;
  QString dirty_domain;
  QString render_reason = QStringLiteral("edit_change");
  QRectF normalized_region;
  bool schedule_full_resolution = true;
  bool approximate_ok = false;
};

struct RenderGraphResolution final {
  QString dirty_domain;
  QStringList dirty_stages;
  QRectF dirty_region;
  bool full_invalidation = true;
};

class EditorPreviewController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool sourceSamplingBusy READ sourceSamplingBusy NOTIFY sourceSamplingBusyChanged)
  Q_PROPERTY(bool photoPaintBusy READ photoPaintBusy NOTIFY photoPaintStateChanged)
  Q_PROPERTY(QString photoPaintError READ photoPaintError NOTIFY photoPaintStateChanged)
  Q_PROPERTY(QString previewSource READ previewSource NOTIFY previewChanged)
  Q_PROPERTY(bool photoExportBusy READ photoExportBusy NOTIFY photoExportChanged)
  Q_PROPERTY(QString photoExportError READ photoExportError NOTIFY photoExportChanged)
  Q_PROPERTY(QString photoExportPath READ photoExportPath NOTIFY photoExportChanged)
  Q_PROPERTY(QString displayPreviewSource READ displayPreviewSource NOTIFY previewChanged)
  Q_PROPERTY(QString baseImageThumbnailSource READ baseImageThumbnailSource NOTIFY previewChanged)
  Q_PROPERTY(QString displayPreviewOverlaySource READ displayPreviewOverlaySource NOTIFY previewChanged)
  Q_PROPERTY(QString focusCheckOverlaySource READ focusCheckOverlaySource NOTIFY diagnosticOverlaysChanged)
  Q_PROPERTY(QString focusPeakingOverlaySource READ focusPeakingOverlaySource NOTIFY diagnosticOverlaysChanged)
  Q_PROPERTY(QString clippingOverlaySource READ clippingOverlaySource NOTIFY diagnosticOverlaysChanged)
  Q_PROPERTY(QString falseColorOverlaySource READ falseColorOverlaySource NOTIFY diagnosticOverlaysChanged)
  Q_PROPERTY(QString gamutOverlaySource READ gamutOverlaySource NOTIFY diagnosticOverlaysChanged)
  Q_PROPERTY(QString zoneMapOverlaySource READ zoneMapOverlaySource NOTIFY diagnosticOverlaysChanged)
  Q_PROPERTY(bool colorToolMaskPreviewEnabled READ colorToolMaskPreviewEnabled WRITE setColorToolMaskPreviewEnabled NOTIFY controlsChanged)
  Q_PROPERTY(QString colorToolMaskPreviewSource READ colorToolMaskPreviewSource NOTIFY controlsChanged)
  Q_PROPERTY(bool colorToolMaskPreviewBusy READ colorToolMaskPreviewBusy NOTIFY controlsChanged)
  Q_PROPERTY(bool displayPreviewNativeAvailable READ displayPreviewNativeAvailable NOTIFY previewChanged)
  Q_PROPERTY(quint64 displayPreviewNativeHandle READ displayPreviewNativeHandle NOTIFY previewChanged)
  Q_PROPERTY(QSize displayPreviewNativeSize READ displayPreviewNativeSize NOTIFY previewChanged)
  Q_PROPERTY(bool displayPreviewNativeFullResolution READ displayPreviewNativeFullResolution NOTIFY previewChanged)
  Q_PROPERTY(bool displayPreviewNativeViewportFullResolution READ displayPreviewNativeViewportFullResolution NOTIFY displayPreviewNativeViewportFullResolutionChanged)
  Q_PROPERTY(int displayPreviewNativeRevision READ displayPreviewNativeRevision NOTIFY previewChanged)
  Q_PROPERTY(QVariantList histogramRed READ histogramRed NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList histogramGreen READ histogramGreen NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList histogramBlue READ histogramBlue NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList histogramLuminance READ histogramLuminance NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList histogramRedCounts READ histogramRedCounts NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList histogramGreenCounts READ histogramGreenCounts NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList histogramBlueCounts READ histogramBlueCounts NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList histogramLuminanceCounts READ histogramLuminanceCounts NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList scopeWaveformRed READ scopeWaveformRed NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList scopeWaveformGreen READ scopeWaveformGreen NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList scopeWaveformBlue READ scopeWaveformBlue NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList scopeWaveformLuminance READ scopeWaveformLuminance NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList scopeDensityRed READ scopeDensityRed NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList scopeDensityGreen READ scopeDensityGreen NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList scopeDensityBlue READ scopeDensityBlue NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList scopeDensityLuminance READ scopeDensityLuminance NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList scopeVectorscopePoints READ scopeVectorscopePoints NOTIFY histogramChanged)
  Q_PROPERTY(int histogramShadowClipCount READ histogramShadowClipCount NOTIFY histogramChanged)
  Q_PROPERTY(int histogramHighlightClipCount READ histogramHighlightClipCount NOTIFY histogramChanged)
  Q_PROPERTY(int histogramSampleCount READ histogramSampleCount NOTIFY histogramChanged)
  Q_PROPERTY(int rawClipSampleCount READ rawClipSampleCount NOTIFY histogramChanged)
  Q_PROPERTY(int rawShadowClipCount READ rawShadowClipCount NOTIFY histogramChanged)
  Q_PROPERTY(int rawHighlightClipCount READ rawHighlightClipCount NOTIFY histogramChanged)
  Q_PROPERTY(QVariantMap pixelInspector READ pixelInspector NOTIFY pixelInspectorChanged)
  Q_PROPERTY(bool histogramUpdateActive READ histogramUpdateActive NOTIFY histogramActivityChanged)
  Q_PROPERTY(bool scopesVisible READ scopesVisible WRITE setScopesVisible NOTIFY controlsChanged)
  Q_PROPERTY(QString scopeDomain READ scopeDomain WRITE setScopeDomain NOTIFY controlsChanged)
  Q_PROPERTY(QString scopeDomainLabel READ scopeDomainLabel NOTIFY controlsChanged)
  Q_PROPERTY(int previewTargetEdge READ previewTargetEdge WRITE setPreviewTargetEdge NOTIFY previewTargetEdgeChanged)
  Q_PROPERTY(QString referencePreviewSource READ referencePreviewSource NOTIFY previewChanged)
  Q_PROPERTY(QString displayReferencePreviewSource READ displayReferencePreviewSource NOTIFY previewChanged)
  Q_PROPERTY(QString referenceImagePath READ referenceImagePath NOTIFY referenceImageChanged)
  Q_PROPERTY(QString referenceImageName READ referenceImageName NOTIFY referenceImageChanged)
  Q_PROPERTY(bool hasReferenceImage READ hasReferenceImage NOTIFY referenceImageChanged)
  Q_PROPERTY(QRectF displayPreviewOverlayRect READ displayPreviewOverlayRect NOTIFY previewChanged)
  Q_PROPERTY(bool previewViewportActive READ previewViewportActive NOTIFY previewViewportChanged)
  Q_PROPERTY(QRectF previewViewportRect READ previewViewportRect NOTIFY previewViewportChanged)
  Q_PROPERTY(int previewViewportPatchMaxEdge READ previewViewportPatchMaxEdge NOTIFY previewViewportChanged)
  Q_PROPERTY(QVariantMap colorTransformDiagnostics READ colorTransformDiagnostics NOTIFY controlsChanged)
  Q_PROPERTY(QVariantMap iccProfileAssets READ iccProfileAssets NOTIFY controlsChanged)
  Q_PROPERTY(QVariantMap cameraDcpProfileAsset READ cameraDcpProfileAsset NOTIFY controlsChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString recipeSaveError READ recipeSaveError NOTIFY recipeSaveStateChanged)
  Q_PROPERTY(bool catalogRecipePersistenceEnabled READ catalogRecipePersistenceEnabled WRITE setCatalogRecipePersistenceEnabled NOTIFY controlsChanged)
  Q_PROPERTY(bool recipeSavePending READ recipeSavePending NOTIFY recipeSaveStateChanged)
  Q_PROPERTY(QString selectionFailureReason READ selectionFailureReason NOTIFY statusChanged)
  Q_PROPERTY(QString previewFailureStage READ previewFailureStage NOTIFY statusChanged)
  Q_PROPERTY(QString previewFailureDetail READ previewFailureDetail NOTIFY statusChanged)
  Q_PROPERTY(QStringList previewFailureLayerIds READ previewFailureLayerIds NOTIFY statusChanged)
  Q_PROPERTY(QStringList previewFailureMaskSetIds READ previewFailureMaskSetIds NOTIFY statusChanged)
  Q_PROPERTY(QString assetId READ assetId NOTIFY assetChanged)
  Q_PROPERTY(QString cameraLabel READ cameraLabel NOTIFY assetChanged)
  Q_PROPERTY(QString captureDate READ captureDate NOTIFY assetChanged)
  Q_PROPERTY(QString lensModel READ lensModel NOTIFY assetChanged)
  Q_PROPERTY(bool hasAsset READ hasAsset NOTIFY assetChanged)
  Q_PROPERTY(bool subjectMaskAvailable READ subjectMaskAvailable CONSTANT)
  Q_PROPERTY(QString aiModelStatus READ aiModelStatus CONSTANT)
  Q_PROPERTY(QString aiModelDiagnostics READ aiModelDiagnostics CONSTANT)
  Q_PROPERTY(bool aiSmartSelectionAvailable READ aiSmartSelectionAvailable CONSTANT)
  Q_PROPERTY(bool aiInpaintAvailable READ aiInpaintAvailable CONSTANT)
  Q_PROPERTY(QString smartSelectionRuntimeLabel READ smartSelectionRuntimeLabel CONSTANT)
  Q_PROPERTY(QString inpaintRuntimeLabel READ inpaintRuntimeLabel CONSTANT)
  Q_PROPERTY(bool maskActionRetryAvailable READ maskActionRetryAvailable NOTIFY controlsChanged)
  Q_PROPERTY(QString maskActionRetryLabel READ maskActionRetryLabel NOTIFY controlsChanged)
  Q_PROPERTY(QString maskActionRetryDetail READ maskActionRetryDetail NOTIFY controlsChanged)
  Q_PROPERTY(QString inpaintPrompt READ inpaintPrompt WRITE setInpaintPrompt NOTIFY controlsChanged)
  Q_PROPERTY(int inpaintSeed READ inpaintSeed WRITE setInpaintSeed NOTIFY controlsChanged)
  Q_PROPERTY(double inpaintBlendAmount READ inpaintBlendAmount WRITE setInpaintBlendAmount NOTIFY controlsChanged)
  Q_PROPERTY(QString inpaintQualityMode READ inpaintQualityMode WRITE setInpaintQualityMode NOTIFY controlsChanged)
  Q_PROPERTY(bool inpaintProgressActive READ inpaintProgressActive NOTIFY controlsChanged)
  Q_PROPERTY(double inpaintProgressValue READ inpaintProgressValue NOTIFY controlsChanged)
  Q_PROPERTY(QString inpaintProgressLabel READ inpaintProgressLabel NOTIFY controlsChanged)
  Q_PROPERTY(QString inpaintProgressSignature READ inpaintProgressSignature NOTIFY controlsChanged)
  Q_PROPERTY(bool retouchBeforeAfterEnabled READ retouchBeforeAfterEnabled WRITE setRetouchBeforeAfterEnabled NOTIFY controlsChanged)
  Q_PROPERTY(bool inpaintPatchDiagnosticsAvailable READ inpaintPatchDiagnosticsAvailable CONSTANT)
  Q_PROPERTY(bool inpaintPatchDiagnosticsEnabled READ inpaintPatchDiagnosticsEnabled WRITE setInpaintPatchDiagnosticsEnabled NOTIFY controlsChanged)
  Q_PROPERTY(QRectF activeInpaintPatchBounds READ activeInpaintPatchBounds NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList activeInpaintCandidates READ activeInpaintCandidates NOTIFY controlsChanged)
  Q_PROPERTY(int activeInpaintCandidateCount READ activeInpaintCandidateCount NOTIFY controlsChanged)
  Q_PROPERTY(int activeInpaintCandidateIndex READ activeInpaintCandidateIndex WRITE setActiveInpaintCandidateIndex NOTIFY controlsChanged)
  Q_PROPERTY(bool activeInpaintVariationPickerVisible READ activeInpaintVariationPickerVisible NOTIFY controlsChanged)
  Q_PROPERTY(bool activeInpaintRegenerationAvailable READ activeInpaintRegenerationAvailable NOTIFY controlsChanged)
  Q_PROPERTY(bool activeInpaintFinalizationAvailable READ activeInpaintFinalizationAvailable NOTIFY controlsChanged)
  Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
  Q_PROPERTY(bool fullRenderRunning READ fullRenderRunning NOTIFY processingChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
  Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY historyChanged)
  Q_PROPERTY(QString redoLabel READ redoLabel NOTIFY historyChanged)
  Q_PROPERTY(QVariantList editHistoryEntries READ editHistoryEntries NOTIFY historyChanged)
  Q_PROPERTY(int editHistoryCurrentIndex READ editHistoryCurrentIndex NOTIFY historyChanged)
  Q_PROPERTY(QVariantList editHistorySnapshots READ editHistorySnapshots NOTIFY historyChanged)
  Q_PROPERTY(bool hasCopiedPhotoRecipe READ hasCopiedPhotoRecipe NOTIFY clipboardChanged)
  Q_PROPERTY(QString copiedPhotoRecipePatchJson READ copiedPhotoRecipePatchJson NOTIFY clipboardChanged)
  Q_PROPERTY(bool hasCopiedSettings READ hasCopiedSettings NOTIFY clipboardChanged)
  Q_PROPERTY(bool hasCopiedPhotoLayerAdjustments READ hasCopiedPhotoLayerAdjustments NOTIFY clipboardChanged)
  Q_PROPERTY(QVariantList photoLayers READ photoLayers NOTIFY controlsChanged)
  Q_PROPERTY(bool baseImageLocked READ baseImageLocked NOTIFY controlsChanged)
  Q_PROPERTY(bool hasCopiedPhotoLayers READ hasCopiedPhotoLayers NOTIFY clipboardChanged)
  Q_PROPERTY(bool photoLayerClipboardBusy READ photoLayerClipboardBusy NOTIFY clipboardChanged)
  Q_PROPERTY(QVariantMap baseImageLayerData READ baseImageLayerData NOTIFY controlsChanged)
  Q_PROPERTY(QStringList photoFragmentBrushPresetNames READ photoFragmentBrushPresetNames NOTIFY controlsChanged)
  Q_PROPERTY(int photoLayerCount READ photoLayerCount NOTIFY controlsChanged)
  Q_PROPERTY(bool photoLayerSoloActive READ photoLayerSoloActive NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList photoLayerVisibilityStates READ photoLayerVisibilityStates NOTIFY controlsChanged)
  Q_PROPERTY(int photoLayerVisibilityStateCount READ photoLayerVisibilityStateCount NOTIFY controlsChanged)
  Q_PROPERTY(int sourceImageWidth READ sourceImageWidth NOTIFY previewChanged)
  Q_PROPERTY(int sourceImageHeight READ sourceImageHeight NOTIFY previewChanged)
  Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetsChanged)
  Q_PROPERTY(QStringList presetPackNames READ presetPackNames NOTIFY presetsChanged)
  Q_PROPERTY(QStringList presetCategoryNames READ presetCategoryNames NOTIFY presetsChanged)
  Q_PROPERTY(QStringList favoritePresetNames READ favoritePresetNames NOTIFY presetsChanged)
  Q_PROPERTY(double presetIntensity READ presetIntensity WRITE setPresetIntensity NOTIFY presetIntensityChanged)
  Q_PROPERTY(bool hasCameraDefault READ hasCameraDefault NOTIFY cameraDefaultsChanged)
  Q_PROPERTY(bool compareMode READ compareMode WRITE setCompareMode NOTIFY compareModeChanged)
  Q_PROPERTY(QString compareSourceMode READ compareSourceMode WRITE setCompareSourceMode NOTIFY compareSnapshotChanged)
  Q_PROPERTY(QString compareSnapshotId READ compareSnapshotId WRITE setCompareSnapshotId NOTIFY compareSnapshotChanged)
  Q_PROPERTY(QString compareSnapshotName READ compareSnapshotName NOTIFY compareSnapshotChanged)
  Q_PROPERTY(QString currentPhotographicRenderingModel READ currentPhotographicRenderingModel NOTIFY controlsChanged)
  Q_PROPERTY(QString renderingUpgradeTargetModel READ renderingUpgradeTargetModel CONSTANT)
  Q_PROPERTY(bool renderingUpgradeAvailable READ renderingUpgradeAvailable NOTIFY controlsChanged)
  Q_PROPERTY(bool renderingUpgradePreviewActive READ renderingUpgradePreviewActive NOTIFY controlsChanged)
  Q_PROPERTY(QString renderingUpgradeStatus READ renderingUpgradeStatus NOTIFY controlsChanged)
  Q_PROPERTY(QVariantMap renderingUpgradeEvidence READ renderingUpgradeEvidence NOTIFY controlsChanged)
  Q_PROPERTY(QString sectionComparisonScope READ sectionComparisonScope NOTIFY sectionComparisonChanged)
  Q_PROPERTY(QString sectionComparisonLayerId READ sectionComparisonLayerId NOTIFY sectionComparisonChanged)
  Q_PROPERTY(QStringList compareAdjustmentGroups READ compareAdjustmentGroups WRITE setCompareAdjustmentGroups NOTIFY compareAdjustmentGroupsChanged)
  Q_PROPERTY(QStringList maskCompareAdjustmentGroups READ maskCompareAdjustmentGroups WRITE setMaskCompareAdjustmentGroups NOTIFY compareAdjustmentGroupsChanged)
  Q_PROPERTY(bool cropEditing READ cropEditing WRITE setCropEditing NOTIFY controlsChanged)
  Q_PROPERTY(QString inputColorProfile READ inputColorProfile WRITE setInputColorProfile NOTIFY controlsChanged)
  Q_PROPERTY(QString outputColorSpace READ outputColorSpace WRITE setOutputColorSpace NOTIFY controlsChanged)
  Q_PROPERTY(QString previewOutputMode READ previewOutputMode WRITE setPreviewOutputMode NOTIFY controlsChanged)
  Q_PROPERTY(QStringList previewOutputModeOptions READ previewOutputModeOptions CONSTANT)
  Q_PROPERTY(QString referenceDisplayTarget READ referenceDisplayTarget WRITE setReferenceDisplayTarget NOTIFY controlsChanged)
  Q_PROPERTY(QString monitorColorProfile READ monitorColorProfile WRITE setMonitorColorProfile NOTIFY controlsChanged)
  Q_PROPERTY(QString systemMonitorProfileName READ systemMonitorProfileName NOTIFY systemMonitorProfileChanged)
  Q_PROPERTY(QString systemMonitorDisplayIdentity READ systemMonitorDisplayIdentity NOTIFY systemMonitorProfileChanged)
  Q_PROPERTY(QVariantMap systemMonitorDisplayTarget READ systemMonitorDisplayTarget NOTIFY systemMonitorProfileChanged)
  Q_PROPERTY(bool softProofEnabled READ softProofEnabled WRITE setSoftProofEnabled NOTIFY controlsChanged)
  Q_PROPERTY(QString softProofProfile READ softProofProfile WRITE setSoftProofProfile NOTIFY controlsChanged)
  Q_PROPERTY(QString proofRenderIntent READ proofRenderIntent WRITE setProofRenderIntent NOTIFY controlsChanged)
  Q_PROPERTY(bool proofSimulatePaperWhite READ proofSimulatePaperWhite WRITE setProofSimulatePaperWhite NOTIFY controlsChanged)
  Q_PROPERTY(bool proofSimulateInkBlack READ proofSimulateInkBlack WRITE setProofSimulateInkBlack NOTIFY controlsChanged)
  Q_PROPERTY(bool rawControlsAvailable READ rawControlsAvailable NOTIFY assetChanged)
  Q_PROPERTY(double rawExposure READ rawExposure WRITE setRawExposure NOTIFY controlsChanged)
  Q_PROPERTY(double rawHotDeadPixelCleanup READ rawHotDeadPixelCleanup WRITE setRawHotDeadPixelCleanup NOTIFY controlsChanged)
  Q_PROPERTY(double rawFalseColorSuppression READ rawFalseColorSuppression WRITE setRawFalseColorSuppression NOTIFY controlsChanged)
  Q_PROPERTY(bool rawHighlightRecovery READ rawHighlightRecovery WRITE setRawHighlightRecovery NOTIFY controlsChanged)
  Q_PROPERTY(QString highlightReconstructionMode READ highlightReconstructionMode WRITE setHighlightReconstructionMode NOTIFY controlsChanged)
  Q_PROPERTY(QString rawDemosaicQuality READ rawDemosaicQuality WRITE setRawDemosaicQuality NOTIFY controlsChanged)
  Q_PROPERTY(bool rawLensCorrection READ rawLensCorrection WRITE setRawLensCorrection NOTIFY controlsChanged)
  Q_PROPERTY(bool rawWhiteBalanceOverride READ rawWhiteBalanceOverride WRITE setRawWhiteBalanceOverride NOTIFY controlsChanged)
  Q_PROPERTY(QString rawCameraMatchingLook READ rawCameraMatchingLook WRITE setRawCameraMatchingLook NOTIFY controlsChanged)
  Q_PROPERTY(double rawWhiteBalanceTemperature READ rawWhiteBalanceTemperature WRITE setRawWhiteBalanceTemperature NOTIFY controlsChanged)
  Q_PROPERTY(double rawWhiteBalanceTint READ rawWhiteBalanceTint WRITE setRawWhiteBalanceTint NOTIFY controlsChanged)
  Q_PROPERTY(double rawAsShotWhiteBalanceTemperature READ rawAsShotWhiteBalanceTemperature NOTIFY assetChanged)
  Q_PROPERTY(double rawAsShotWhiteBalanceTint READ rawAsShotWhiteBalanceTint NOTIFY assetChanged)
  Q_PROPERTY(double rawBlackLevel READ rawBlackLevel WRITE setRawBlackLevel NOTIFY controlsChanged)
  Q_PROPERTY(double rawWhiteLevel READ rawWhiteLevel WRITE setRawWhiteLevel NOTIFY controlsChanged)
  Q_PROPERTY(double rawCalibrationHue READ rawCalibrationHue WRITE setRawCalibrationHue NOTIFY controlsChanged)
  Q_PROPERTY(double rawCalibrationSaturation READ rawCalibrationSaturation WRITE setRawCalibrationSaturation NOTIFY controlsChanged)
  Q_PROPERTY(double rawRedPrimaryHue READ rawRedPrimaryHue WRITE setRawRedPrimaryHue NOTIFY controlsChanged)
  Q_PROPERTY(double rawRedPrimarySaturation READ rawRedPrimarySaturation WRITE setRawRedPrimarySaturation NOTIFY controlsChanged)
  Q_PROPERTY(double rawGreenPrimaryHue READ rawGreenPrimaryHue WRITE setRawGreenPrimaryHue NOTIFY controlsChanged)
  Q_PROPERTY(double rawGreenPrimarySaturation READ rawGreenPrimarySaturation WRITE setRawGreenPrimarySaturation NOTIFY controlsChanged)
  Q_PROPERTY(double rawBluePrimaryHue READ rawBluePrimaryHue WRITE setRawBluePrimaryHue NOTIFY controlsChanged)
  Q_PROPERTY(double rawBluePrimarySaturation READ rawBluePrimarySaturation WRITE setRawBluePrimarySaturation NOTIFY controlsChanged)
  Q_PROPERTY(double exposure READ exposure WRITE setExposure NOTIFY controlsChanged)
  Q_PROPERTY(double contrast READ contrast WRITE setContrast NOTIFY controlsChanged)
  Q_PROPERTY(double highlights READ highlights WRITE setHighlights NOTIFY controlsChanged)
  Q_PROPERTY(double shadows READ shadows WRITE setShadows NOTIFY controlsChanged)
  Q_PROPERTY(double whites READ whites WRITE setWhites NOTIFY controlsChanged)
  Q_PROPERTY(double blacks READ blacks WRITE setBlacks NOTIFY controlsChanged)
  Q_PROPERTY(double brightness READ brightness WRITE setBrightness NOTIFY controlsChanged)
  Q_PROPERTY(double hdrLocalToneMapping READ hdrLocalToneMapping WRITE setHdrLocalToneMapping NOTIFY controlsChanged)
  Q_PROPERTY(double filmicShoulder READ filmicShoulder WRITE setFilmicShoulder NOTIFY controlsChanged)
  Q_PROPERTY(double filmicToe READ filmicToe WRITE setFilmicToe NOTIFY controlsChanged)
  Q_PROPERTY(double adaptiveContrast READ adaptiveContrast WRITE setAdaptiveContrast NOTIFY controlsChanged)
  Q_PROPERTY(double levelsBlackPoint READ levelsBlackPoint WRITE setLevelsBlackPoint NOTIFY controlsChanged)
  Q_PROPERTY(double levelsGamma READ levelsGamma WRITE setLevelsGamma NOTIFY controlsChanged)
  Q_PROPERTY(double levelsWhitePoint READ levelsWhitePoint WRITE setLevelsWhitePoint NOTIFY controlsChanged)
  Q_PROPERTY(double zoneDeepShadows READ zoneDeepShadows WRITE setZoneDeepShadows NOTIFY controlsChanged)
  Q_PROPERTY(double zoneShadows READ zoneShadows WRITE setZoneShadows NOTIFY controlsChanged)
  Q_PROPERTY(double zoneMidtones READ zoneMidtones WRITE setZoneMidtones NOTIFY controlsChanged)
  Q_PROPERTY(double zoneLights READ zoneLights WRITE setZoneLights NOTIFY controlsChanged)
  Q_PROPERTY(double zoneHighlights READ zoneHighlights WRITE setZoneHighlights NOTIFY controlsChanged)
  Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY controlsChanged)
  Q_PROPERTY(double tint READ tint WRITE setTint NOTIFY controlsChanged)
  Q_PROPERTY(double texture READ texture WRITE setTexture NOTIFY controlsChanged)
  Q_PROPERTY(double clarity READ clarity WRITE setClarity NOTIFY controlsChanged)
  Q_PROPERTY(double dehaze READ dehaze WRITE setDehaze NOTIFY controlsChanged)
  Q_PROPERTY(double sharpening READ sharpening WRITE setSharpening NOTIFY controlsChanged)
  Q_PROPERTY(double sharpenRadius READ sharpenRadius WRITE setSharpenRadius NOTIFY controlsChanged)
  Q_PROPERTY(double sharpenDetail READ sharpenDetail WRITE setSharpenDetail NOTIFY controlsChanged)
  Q_PROPERTY(double sharpenMasking READ sharpenMasking WRITE setSharpenMasking NOTIFY controlsChanged)
  Q_PROPERTY(double luminanceNr READ luminanceNr WRITE setLuminanceNr NOTIFY controlsChanged)
  Q_PROPERTY(double colorNr READ colorNr WRITE setColorNr NOTIFY controlsChanged)
  Q_PROPERTY(double lensDistortion READ lensDistortion WRITE setLensDistortion NOTIFY controlsChanged)
  Q_PROPERTY(double lensVignette READ lensVignette WRITE setLensVignette NOTIFY controlsChanged)
  Q_PROPERTY(double chromaticAberration READ chromaticAberration WRITE setChromaticAberration NOTIFY controlsChanged)
  Q_PROPERTY(double perspectiveVertical READ perspectiveVertical WRITE setPerspectiveVertical NOTIFY controlsChanged)
  Q_PROPERTY(double perspectiveHorizontal READ perspectiveHorizontal WRITE setPerspectiveHorizontal NOTIFY controlsChanged)
  Q_PROPERTY(double transformRotation READ transformRotation WRITE setTransformRotation NOTIFY controlsChanged)
  Q_PROPERTY(double transformScale READ transformScale WRITE setTransformScale NOTIFY controlsChanged)
  Q_PROPERTY(double transformSkewX READ transformSkewX WRITE setTransformSkewX NOTIFY controlsChanged)
  Q_PROPERTY(double transformSkewY READ transformSkewY WRITE setTransformSkewY NOTIFY controlsChanged)
  Q_PROPERTY(double liquifyCenterX READ liquifyCenterX WRITE setLiquifyCenterX NOTIFY controlsChanged)
  Q_PROPERTY(double liquifyCenterY READ liquifyCenterY WRITE setLiquifyCenterY NOTIFY controlsChanged)
  Q_PROPERTY(double liquifyRadius READ liquifyRadius WRITE setLiquifyRadius NOTIFY controlsChanged)
  Q_PROPERTY(double liquifyStrength READ liquifyStrength WRITE setLiquifyStrength NOTIFY controlsChanged)
  Q_PROPERTY(bool contentAwareCropFill READ contentAwareCropFill WRITE setContentAwareCropFill NOTIFY controlsChanged)
  Q_PROPERTY(double vibrance READ vibrance WRITE setVibrance NOTIFY controlsChanged)
  Q_PROPERTY(double saturation READ saturation WRITE setSaturation NOTIFY controlsChanged)
  Q_PROPERTY(double curveShadows READ curveShadows WRITE setCurveShadows NOTIFY controlsChanged)
  Q_PROPERTY(double curveDarks READ curveDarks WRITE setCurveDarks NOTIFY controlsChanged)
  Q_PROPERTY(double curveLights READ curveLights WRITE setCurveLights NOTIFY controlsChanged)
  Q_PROPERTY(double curveHighlights READ curveHighlights WRITE setCurveHighlights NOTIFY controlsChanged)
  Q_PROPERTY(QString curveChannel READ curveChannel WRITE setCurveChannel NOTIFY controlsChanged)
  Q_PROPERTY(QString activeCurveMode READ activeCurveMode NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList activeCurvePoints READ activeCurvePoints WRITE setActiveCurvePoints NOTIFY controlsChanged)
  Q_PROPERTY(QStringList curvePresetNames READ curvePresetNames CONSTANT)
  Q_PROPERTY(QString colorMixerModel READ colorMixerModel WRITE setColorMixerModel NOTIFY controlsChanged)
  Q_PROPERTY(QString hslChannel READ hslChannel WRITE setHslChannel NOTIFY controlsChanged)
  Q_PROPERTY(double hueAdjustment READ hueAdjustment WRITE setHueAdjustment NOTIFY controlsChanged)
  Q_PROPERTY(double saturationAdjustment READ saturationAdjustment WRITE setSaturationAdjustment NOTIFY controlsChanged)
  Q_PROPERTY(double luminanceAdjustment READ luminanceAdjustment WRITE setLuminanceAdjustment NOTIFY controlsChanged)
  Q_PROPERTY(double selectiveCyan READ selectiveCyan WRITE setSelectiveCyan NOTIFY controlsChanged)
  Q_PROPERTY(double selectiveMagenta READ selectiveMagenta WRITE setSelectiveMagenta NOTIFY controlsChanged)
  Q_PROPERTY(double selectiveYellow READ selectiveYellow WRITE setSelectiveYellow NOTIFY controlsChanged)
  Q_PROPERTY(double selectiveBlack READ selectiveBlack WRITE setSelectiveBlack NOTIFY controlsChanged)
  Q_PROPERTY(double channelMixerRedFromRed READ channelMixerRedFromRed WRITE setChannelMixerRedFromRed NOTIFY controlsChanged)
  Q_PROPERTY(double channelMixerRedFromGreen READ channelMixerRedFromGreen WRITE setChannelMixerRedFromGreen NOTIFY controlsChanged)
  Q_PROPERTY(double channelMixerRedFromBlue READ channelMixerRedFromBlue WRITE setChannelMixerRedFromBlue NOTIFY controlsChanged)
  Q_PROPERTY(double channelMixerGreenFromRed READ channelMixerGreenFromRed WRITE setChannelMixerGreenFromRed NOTIFY controlsChanged)
  Q_PROPERTY(double channelMixerGreenFromGreen READ channelMixerGreenFromGreen WRITE setChannelMixerGreenFromGreen NOTIFY controlsChanged)
  Q_PROPERTY(double channelMixerGreenFromBlue READ channelMixerGreenFromBlue WRITE setChannelMixerGreenFromBlue NOTIFY controlsChanged)
  Q_PROPERTY(double channelMixerBlueFromRed READ channelMixerBlueFromRed WRITE setChannelMixerBlueFromRed NOTIFY controlsChanged)
  Q_PROPERTY(double channelMixerBlueFromGreen READ channelMixerBlueFromGreen WRITE setChannelMixerBlueFromGreen NOTIFY controlsChanged)
  Q_PROPERTY(double channelMixerBlueFromBlue READ channelMixerBlueFromBlue WRITE setChannelMixerBlueFromBlue NOTIFY controlsChanged)
  Q_PROPERTY(bool blackAndWhiteEnabled READ blackAndWhiteEnabled WRITE setBlackAndWhiteEnabled NOTIFY controlsChanged)
  Q_PROPERTY(double blackAndWhiteReds READ blackAndWhiteReds WRITE setBlackAndWhiteReds NOTIFY controlsChanged)
  Q_PROPERTY(double blackAndWhiteOranges READ blackAndWhiteOranges WRITE setBlackAndWhiteOranges NOTIFY controlsChanged)
  Q_PROPERTY(double blackAndWhiteYellows READ blackAndWhiteYellows WRITE setBlackAndWhiteYellows NOTIFY controlsChanged)
  Q_PROPERTY(double blackAndWhiteGreens READ blackAndWhiteGreens WRITE setBlackAndWhiteGreens NOTIFY controlsChanged)
  Q_PROPERTY(double blackAndWhiteAquas READ blackAndWhiteAquas WRITE setBlackAndWhiteAquas NOTIFY controlsChanged)
  Q_PROPERTY(double blackAndWhiteBlues READ blackAndWhiteBlues WRITE setBlackAndWhiteBlues NOTIFY controlsChanged)
  Q_PROPERTY(double blackAndWhitePurples READ blackAndWhitePurples WRITE setBlackAndWhitePurples NOTIFY controlsChanged)
  Q_PROPERTY(double blackAndWhiteMagentas READ blackAndWhiteMagentas WRITE setBlackAndWhiteMagentas NOTIFY controlsChanged)
  Q_PROPERTY(bool colorReplacementEnabled READ colorReplacementEnabled WRITE setColorReplacementEnabled NOTIFY controlsChanged)
  Q_PROPERTY(double colorReplacementSourceHue READ colorReplacementSourceHue WRITE setColorReplacementSourceHue NOTIFY controlsChanged)
  Q_PROPERTY(double colorReplacementTargetHue READ colorReplacementTargetHue WRITE setColorReplacementTargetHue NOTIFY controlsChanged)
  Q_PROPERTY(double colorReplacementRange READ colorReplacementRange WRITE setColorReplacementRange NOTIFY controlsChanged)
  Q_PROPERTY(double colorReplacementSoftness READ colorReplacementSoftness WRITE setColorReplacementSoftness NOTIFY controlsChanged)
  Q_PROPERTY(double colorReplacementStrength READ colorReplacementStrength WRITE setColorReplacementStrength NOTIFY controlsChanged)
  Q_PROPERTY(bool colorReplacementPreserveLuminance READ colorReplacementPreserveLuminance WRITE setColorReplacementPreserveLuminance NOTIFY controlsChanged)
  Q_PROPERTY(bool colorReplacementPreserveSaturation READ colorReplacementPreserveSaturation WRITE setColorReplacementPreserveSaturation NOTIFY controlsChanged)
  Q_PROPERTY(bool skinToneProtectionEnabled READ skinToneProtectionEnabled WRITE setSkinToneProtectionEnabled NOTIFY controlsChanged)
  Q_PROPERTY(double skinToneLineHue READ skinToneLineHue WRITE setSkinToneLineHue NOTIFY controlsChanged)
  Q_PROPERTY(double skinToneLineAngle READ skinToneLineAngle NOTIFY controlsChanged)
  Q_PROPERTY(double skinToneProtectionRange READ skinToneProtectionRange WRITE setSkinToneProtectionRange NOTIFY controlsChanged)
  Q_PROPERTY(double skinToneProtectionStrength READ skinToneProtectionStrength WRITE setSkinToneProtectionStrength NOTIFY controlsChanged)
  Q_PROPERTY(bool colorHarmonyEnabled READ colorHarmonyEnabled WRITE setColorHarmonyEnabled NOTIFY controlsChanged)
  Q_PROPERTY(QString colorHarmonyMode READ colorHarmonyMode WRITE setColorHarmonyMode NOTIFY controlsChanged)
  Q_PROPERTY(double colorHarmonyBaseHue READ colorHarmonyBaseHue WRITE setColorHarmonyBaseHue NOTIFY controlsChanged)
  Q_PROPERTY(double colorHarmonyStrength READ colorHarmonyStrength WRITE setColorHarmonyStrength NOTIFY controlsChanged)
  Q_PROPERTY(double gradingShadowsHue READ gradingShadowsHue WRITE setGradingShadowsHue NOTIFY controlsChanged)
  Q_PROPERTY(double gradingShadowsSaturation READ gradingShadowsSaturation WRITE setGradingShadowsSaturation NOTIFY controlsChanged)
  Q_PROPERTY(double gradingShadowsLuminance READ gradingShadowsLuminance WRITE setGradingShadowsLuminance NOTIFY controlsChanged)
  Q_PROPERTY(double gradingMidtonesHue READ gradingMidtonesHue WRITE setGradingMidtonesHue NOTIFY controlsChanged)
  Q_PROPERTY(double gradingMidtonesSaturation READ gradingMidtonesSaturation WRITE setGradingMidtonesSaturation NOTIFY controlsChanged)
  Q_PROPERTY(double gradingMidtonesLuminance READ gradingMidtonesLuminance WRITE setGradingMidtonesLuminance NOTIFY controlsChanged)
  Q_PROPERTY(double gradingHighlightsHue READ gradingHighlightsHue WRITE setGradingHighlightsHue NOTIFY controlsChanged)
  Q_PROPERTY(double gradingHighlightsSaturation READ gradingHighlightsSaturation WRITE setGradingHighlightsSaturation NOTIFY controlsChanged)
  Q_PROPERTY(double gradingHighlightsLuminance READ gradingHighlightsLuminance WRITE setGradingHighlightsLuminance NOTIFY controlsChanged)
  Q_PROPERTY(double gradingAmount READ gradingAmount WRITE setGradingAmount NOTIFY controlsChanged)
  Q_PROPERTY(double gradingBalance READ gradingBalance WRITE setGradingBalance NOTIFY controlsChanged)
  Q_PROPERTY(double cropRectX READ cropRectX NOTIFY controlsChanged)
  Q_PROPERTY(double cropRectY READ cropRectY NOTIFY controlsChanged)
  Q_PROPERTY(double cropRectWidth READ cropRectWidth NOTIFY controlsChanged)
  Q_PROPERTY(double cropRectHeight READ cropRectHeight NOTIFY controlsChanged)
  Q_PROPERTY(double cropAspectRatio READ cropAspectRatio NOTIFY controlsChanged)
  Q_PROPERTY(double cropZoom READ cropZoom WRITE setCropZoom NOTIFY controlsChanged)
  Q_PROPERTY(QString cropPreset READ cropPreset WRITE setCropPreset NOTIFY controlsChanged)
  Q_PROPERTY(QString uprightMode READ uprightMode WRITE setUprightMode NOTIFY controlsChanged)
  Q_PROPERTY(double uprightVerticalGuideAngle READ uprightVerticalGuideAngle WRITE setUprightVerticalGuideAngle NOTIFY controlsChanged)
  Q_PROPERTY(double uprightHorizontalGuideAngle READ uprightHorizontalGuideAngle WRITE setUprightHorizontalGuideAngle NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList maskSets READ maskSets NOTIFY controlsChanged)
  Q_PROPERTY(int maskSetCount READ maskSetCount NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList retouchStack READ retouchStack NOTIFY controlsChanged)
  Q_PROPERTY(int retouchCount READ retouchCount NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList maskStack READ maskStack NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList typedMaskSources READ typedMaskSources NOTIFY controlsChanged)
  Q_PROPERTY(int maskCount READ maskCount NOTIFY controlsChanged)
  Q_PROPERTY(int emptyMaskCount READ emptyMaskCount NOTIFY controlsChanged)
  Q_PROPERTY(int staleGeneratedMaskCount READ staleGeneratedMaskCount NOTIFY controlsChanged)
  Q_PROPERTY(QString staleGeneratedMaskSummary READ staleGeneratedMaskSummary NOTIFY controlsChanged)
  Q_PROPERTY(int activeGeneratedMaskQualityScore READ activeGeneratedMaskQualityScore NOTIFY controlsChanged)
  Q_PROPERTY(QString activeGeneratedMaskQualitySummary READ activeGeneratedMaskQualitySummary NOTIFY controlsChanged)
  Q_PROPERTY(QStringList activeGeneratedMaskQualityFlags READ activeGeneratedMaskQualityFlags NOTIFY controlsChanged)
  Q_PROPERTY(double activeGeneratedMaskHoleFillThreshold READ activeGeneratedMaskHoleFillThreshold WRITE setActiveGeneratedMaskHoleFillThreshold NOTIFY controlsChanged)
  Q_PROPERTY(double activeGeneratedMaskIslandRemovalThreshold READ activeGeneratedMaskIslandRemovalThreshold WRITE setActiveGeneratedMaskIslandRemovalThreshold NOTIFY controlsChanged)
  Q_PROPERTY(QString activeGeneratedMaskCleanupSummary READ activeGeneratedMaskCleanupSummary NOTIFY controlsChanged)
  Q_PROPERTY(QString maskStackAuditSignature READ maskStackAuditSignature NOTIFY controlsChanged)
  Q_PROPERTY(QString maskMemoryPolicySignature READ maskMemoryPolicySignature CONSTANT)
  Q_PROPERTY(int activeMaskIndex READ activeMaskIndex WRITE setActiveMaskIndex NOTIFY maskStateChanged)
  Q_PROPERTY(bool hasActiveMask READ hasActiveMask NOTIFY maskStateChanged)
  Q_PROPERTY(QString activeMaskKind READ activeMaskKind NOTIFY maskStateChanged)
  Q_PROPERTY(QString activeMaskToolRole READ activeMaskToolRole NOTIFY maskStateChanged)
  Q_PROPERTY(QString activeMaskRetouchMode READ activeMaskRetouchMode NOTIFY controlsChanged)
  Q_PROPERTY(QString retouchWorkflowStateSignature READ retouchWorkflowStateSignature NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskCommitted READ activeMaskCommitted NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskName READ activeMaskName WRITE setActiveMaskName NOTIFY maskStateChanged)
  Q_PROPERTY(QString activeMaskSetName READ activeMaskSetName NOTIFY maskStateChanged)
  Q_PROPERTY(QString activeMaskOperation READ activeMaskOperation WRITE setActiveMaskOperation NOTIFY maskStateChanged)
  Q_PROPERTY(bool activeMaskEnabled READ activeMaskEnabled WRITE setActiveMaskEnabled NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskSolo READ activeMaskSolo WRITE setActiveMaskSolo NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskLocked READ activeMaskLocked WRITE setActiveMaskLocked NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskEditDisabledReason READ activeMaskEditDisabledReason NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskOpacity READ activeMaskOpacity WRITE setActiveMaskOpacity NOTIFY maskStateChanged)
  Q_PROPERTY(double activeMaskDensity READ activeMaskDensity WRITE setActiveMaskDensity NOTIFY maskStateChanged)
  Q_PROPERTY(bool activeMaskInverted READ activeMaskInverted WRITE setActiveMaskInverted NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskBlendMode READ activeMaskBlendMode WRITE setActiveMaskBlendMode NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskExposure READ activeMaskExposure WRITE setActiveMaskExposure NOTIFY maskStateChanged)
  Q_PROPERTY(double activeMaskContrast READ activeMaskContrast WRITE setActiveMaskContrast NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskHighlights READ activeMaskHighlights WRITE setActiveMaskHighlights NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskShadows READ activeMaskShadows WRITE setActiveMaskShadows NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskWhites READ activeMaskWhites WRITE setActiveMaskWhites NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBlacks READ activeMaskBlacks WRITE setActiveMaskBlacks NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskTemperature READ activeMaskTemperature WRITE setActiveMaskTemperature NOTIFY maskStateChanged)
  Q_PROPERTY(double activeMaskTint READ activeMaskTint WRITE setActiveMaskTint NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskVibrance READ activeMaskVibrance WRITE setActiveMaskVibrance NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSaturation READ activeMaskSaturation WRITE setActiveMaskSaturation NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskHueShift READ activeMaskHueShift WRITE setActiveMaskHueShift NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSaturationShift READ activeMaskSaturationShift WRITE setActiveMaskSaturationShift NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskColorizeHue READ activeMaskColorizeHue WRITE setActiveMaskColorizeHue NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskColorizeStrength READ activeMaskColorizeStrength WRITE setActiveMaskColorizeStrength NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRemoveStrength READ activeMaskRemoveStrength WRITE setActiveMaskRemoveStrength NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskHealTextureBlend READ activeMaskHealTextureBlend WRITE setActiveMaskHealTextureBlend NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskHealColorBlend READ activeMaskHealColorBlend WRITE setActiveMaskHealColorBlend NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskSelectionInverted READ activeMaskSelectionInverted WRITE setActiveMaskSelectionInverted NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSelectionExpand READ activeMaskSelectionExpand WRITE setActiveMaskSelectionExpand NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSelectionSmooth READ activeMaskSelectionSmooth WRITE setActiveMaskSelectionSmooth NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSelectionFineEdge READ activeMaskSelectionFineEdge WRITE setActiveMaskSelectionFineEdge NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSelectionEdgeContrast READ activeMaskSelectionEdgeContrast WRITE setActiveMaskSelectionEdgeContrast NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSelectionDecontaminate READ activeMaskSelectionDecontaminate WRITE setActiveMaskSelectionDecontaminate NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskFeather READ activeMaskFeather WRITE setActiveMaskFeather NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskPosition READ activeMaskPosition WRITE setActiveMaskPosition NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRotation READ activeMaskRotation WRITE setActiveMaskRotation NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskCenterX READ activeMaskCenterX WRITE setActiveMaskCenterX NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskCenterY READ activeMaskCenterY WRITE setActiveMaskCenterY NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRadiusX READ activeMaskRadiusX WRITE setActiveMaskRadiusX NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRadiusY READ activeMaskRadiusY WRITE setActiveMaskRadiusY NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBrushSize READ activeMaskBrushSize WRITE setActiveMaskBrushSize NOTIFY maskStateChanged)
  Q_PROPERTY(double activeMaskBrushSizeNormalized READ activeMaskBrushSizeNormalized NOTIFY maskStateChanged)
  Q_PROPERTY(int activeMaskBrushPreviewDiameterPx READ activeMaskBrushPreviewDiameterPx NOTIFY maskStateChanged)
  Q_PROPERTY(int activeMaskBrushFinalDiameterXPx READ activeMaskBrushFinalDiameterXPx NOTIFY maskStateChanged)
  Q_PROPERTY(int activeMaskBrushFinalDiameterYPx READ activeMaskBrushFinalDiameterYPx NOTIFY maskStateChanged)
  Q_PROPERTY(QString activeMaskBrushSizeSummary READ activeMaskBrushSizeSummary NOTIFY maskStateChanged)
  Q_PROPERTY(double activeMaskBrushFlow READ activeMaskBrushFlow WRITE setActiveMaskBrushFlow NOTIFY maskStateChanged)
  Q_PROPERTY(double activeMaskBrushHardness READ activeMaskBrushHardness WRITE setActiveMaskBrushHardness NOTIFY maskStateChanged)
  Q_PROPERTY(double activeMaskBrushSpacing READ activeMaskBrushSpacing WRITE setActiveMaskBrushSpacing NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBrushSmoothing READ activeMaskBrushSmoothing WRITE setActiveMaskBrushSmoothing NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBrushPressureSize READ activeMaskBrushPressureSize WRITE setActiveMaskBrushPressureSize NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBrushPressureFlow READ activeMaskBrushPressureFlow WRITE setActiveMaskBrushPressureFlow NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBrushPressureOpacity READ activeMaskBrushPressureOpacity WRITE setActiveMaskBrushPressureOpacity NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskBrushBuildup READ activeMaskBrushBuildup WRITE setActiveMaskBrushBuildup NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskBrushEraseMode READ activeMaskBrushEraseMode WRITE setActiveMaskBrushEraseMode NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskBrushAutoMask READ activeMaskBrushAutoMask WRITE setActiveMaskBrushAutoMask NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBrushAutoMaskTolerance READ activeMaskBrushAutoMaskTolerance WRITE setActiveMaskBrushAutoMaskTolerance NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskBrushAutoMaskContinuousSampling READ activeMaskBrushAutoMaskContinuousSampling WRITE setActiveMaskBrushAutoMaskContinuousSampling NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskBrushProtectEdges READ activeMaskBrushProtectEdges WRITE setActiveMaskBrushProtectEdges NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskBrushPreset READ activeMaskBrushPreset NOTIFY controlsChanged)
  Q_PROPERTY(QStringList brushPresetNames READ brushPresetNames NOTIFY controlsChanged)
  Q_PROPERTY(int activeMaskBrushPointCount READ activeMaskBrushPointCount NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(QVariantList activeMaskLiveBrushPoints READ activeMaskLiveBrushPoints NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(int activeMaskLiveBrushPreviewPointCount READ activeMaskLiveBrushPreviewPointCount NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(QString activeMaskLiveBrushPreviewSource READ activeMaskLiveBrushPreviewSource NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(bool activeMaskLiveBrushPreviewNativeAvailable READ activeMaskLiveBrushPreviewNativeAvailable NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(QString activeMaskLiveBrushPreviewBackend READ activeMaskLiveBrushPreviewBackend NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(qint64 activeMaskLiveBrushPreviewElapsedMs READ activeMaskLiveBrushPreviewElapsedMs NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(bool activeMaskLiveBrushPreviewWithinLatencyBudget READ activeMaskLiveBrushPreviewWithinLatencyBudget NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(QString activeMaskLiveBrushPreviewFallbackReason READ activeMaskLiveBrushPreviewFallbackReason NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(bool brushStrokeActive READ brushStrokeActive NOTIFY brushStrokeActiveChanged)
  Q_PROPERTY(bool activeMaskCloneSourceSet READ activeMaskCloneSourceSet NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskCloneSourceX READ activeMaskCloneSourceX NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskCloneSourceY READ activeMaskCloneSourceY NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskCloneScale READ activeMaskCloneScale WRITE setActiveMaskCloneScale NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskCloneRotation READ activeMaskCloneRotation WRITE setActiveMaskCloneRotation NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskCloneAligned READ activeMaskCloneAligned WRITE setActiveMaskCloneAligned NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList activeMaskCloneSources READ activeMaskCloneSources NOTIFY controlsChanged)
  Q_PROPERTY(int activeMaskCloneSourceCount READ activeMaskCloneSourceCount NOTIFY controlsChanged)
  Q_PROPERTY(int activeMaskCloneSourceIndex READ activeMaskCloneSourceIndex NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskPatchSourceSet READ activeMaskPatchSourceSet NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskPatchSourceX READ activeMaskPatchSourceX NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskPatchSourceY READ activeMaskPatchSourceY NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskPatchSourceWidth READ activeMaskPatchSourceWidth NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskPatchSourceHeight READ activeMaskPatchSourceHeight NOTIFY controlsChanged)
  Q_PROPERTY(bool hasSensorDustMap READ hasSensorDustMap NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList activeMaskPolygonPoints READ activeMaskPolygonPoints NOTIFY polygonPathChanged)
  Q_PROPERTY(int activeMaskPolygonPointCount READ activeMaskPolygonPointCount NOTIFY polygonPathChanged)
  Q_PROPERTY(bool activeMaskPolygonSelfIntersecting READ activeMaskPolygonSelfIntersecting NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList activeMaskOutlinePoints READ activeMaskOutlinePoints NOTIFY maskOverlayChanged)
  Q_PROPERTY(QVariantList activeMaskOutlinePaths READ activeMaskOutlinePaths NOTIFY maskOverlayChanged)
  Q_PROPERTY(QVariantMap activeMaskOutlineTrace READ activeMaskOutlineTrace NOTIFY maskOverlayChanged)
  Q_PROPERTY(double activeMaskOutlineDisplayScale READ activeMaskOutlineDisplayScale WRITE setActiveMaskOutlineDisplayScale NOTIFY maskOverlayChanged)
  Q_PROPERTY(QString activeMaskOutlineQualityTier READ activeMaskOutlineQualityTier NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(QString activeMaskPreviewQualityTier READ activeMaskPreviewQualityTier NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskPreviewSource READ activeMaskPreviewSource NOTIFY controlsChanged)
  Q_PROPERTY(QString maskPreviewMode READ maskPreviewMode WRITE setMaskPreviewMode NOTIFY controlsChanged)
  Q_PROPERTY(QString maskPreviewDisplayMode READ maskPreviewDisplayMode NOTIFY maskStateChanged)
  Q_PROPERTY(QStringList maskDisplayModeNames READ maskDisplayModeNames CONSTANT)
  Q_PROPERTY(double maskPreviewOverlayOpacity READ maskPreviewOverlayOpacity WRITE setMaskPreviewOverlayOpacity NOTIFY controlsChanged)
  Q_PROPERTY(QString maskPreviewOverlayColor READ maskPreviewOverlayColor WRITE setMaskPreviewOverlayColor NOTIFY controlsChanged)
  Q_PROPERTY(bool maskPreviewShowAll READ maskPreviewShowAll WRITE setMaskPreviewShowAll NOTIFY controlsChanged)
  Q_PROPERTY(bool maskPreviewTemporarilyHidden READ maskPreviewTemporarilyHidden WRITE setMaskPreviewTemporarilyHidden NOTIFY controlsChanged)
  Q_PROPERTY(QString maskRefinementPreviewMode READ maskRefinementPreviewMode WRITE setMaskRefinementPreviewMode NOTIFY controlsChanged)
  Q_PROPERTY(bool maskRefinementHighQualityPreview READ maskRefinementHighQualityPreview WRITE setMaskRefinementHighQualityPreview NOTIFY controlsChanged)
  Q_PROPERTY(bool maskFullResolutionPreview READ maskFullResolutionPreview WRITE setMaskFullResolutionPreview NOTIFY controlsChanged)
  Q_PROPERTY(bool smartSelectPreviewActive READ smartSelectPreviewActive NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(QString smartSelectPreviewSource READ smartSelectPreviewSource NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(QString smartSelectPreviewPipelineSignature READ smartSelectPreviewPipelineSignature NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(QVariantList smartSelectPreviewOutlinePoints READ smartSelectPreviewOutlinePoints NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(int smartSelectCandidateCount READ smartSelectCandidateCount NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(int smartSelectCandidateIndex READ smartSelectCandidateIndex NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(QString smartSelectCandidateHudText READ smartSelectCandidateHudText NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(bool smartSelectCanCycleCandidates READ smartSelectCanCycleCandidates NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(bool smartSelectStrokeActive READ smartSelectStrokeActive NOTIFY smartSelectStrokeChanged)
  Q_PROPERTY(QVariantList smartSelectStrokePoints READ smartSelectStrokePoints NOTIFY smartSelectStrokeChanged)
  Q_PROPERTY(double activeMaskLuminanceMin READ activeMaskLuminanceMin WRITE setActiveMaskLuminanceMin NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskLuminanceMax READ activeMaskLuminanceMax WRITE setActiveMaskLuminanceMax NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList activeMaskLuminanceSamples READ activeMaskLuminanceSamples NOTIFY controlsChanged)
  Q_PROPERTY(QVariantMap activeMaskLuminanceSamplePreview READ activeMaskLuminanceSamplePreview NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskHueCenter READ activeMaskHueCenter WRITE setActiveMaskHueCenter NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskHueRange READ activeMaskHueRange WRITE setActiveMaskHueRange NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSaturationMin READ activeMaskSaturationMin WRITE setActiveMaskSaturationMin NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSaturationMax READ activeMaskSaturationMax WRITE setActiveMaskSaturationMax NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList activeMaskColorSamples READ activeMaskColorSamples NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskDepthMin READ activeMaskDepthMin WRITE setActiveMaskDepthMin NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskDepthMax READ activeMaskDepthMax WRITE setActiveMaskDepthMax NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskDepthSamplingAvailable READ activeMaskDepthSamplingAvailable NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskDepthSamplingDisabledReason READ activeMaskDepthSamplingDisabledReason NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList activeMaskDepthSamples READ activeMaskDepthSamples NOTIFY controlsChanged)
  Q_PROPERTY(bool alphaMaskImportPreviewActive READ alphaMaskImportPreviewActive NOTIFY controlsChanged)
  Q_PROPERTY(QVariantMap alphaMaskImportPreview READ alphaMaskImportPreview NOTIFY controlsChanged)
  Q_PROPERTY(bool alphaMaskImportInvert READ alphaMaskImportInvert WRITE setAlphaMaskImportInvert NOTIFY controlsChanged)
  Q_PROPERTY(double alphaMaskImportThreshold READ alphaMaskImportThreshold WRITE setAlphaMaskImportThreshold NOTIFY controlsChanged)
  Q_PROPERTY(QString alphaMaskImportResampleMode READ alphaMaskImportResampleMode WRITE setAlphaMaskImportResampleMode NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRangeFeather READ activeMaskRangeFeather WRITE setActiveMaskRangeFeather NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRefineLuminanceMin READ activeMaskRefineLuminanceMin WRITE setActiveMaskRefineLuminanceMin NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRefineLuminanceMax READ activeMaskRefineLuminanceMax WRITE setActiveMaskRefineLuminanceMax NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRefineHueCenter READ activeMaskRefineHueCenter WRITE setActiveMaskRefineHueCenter NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRefineHueRange READ activeMaskRefineHueRange WRITE setActiveMaskRefineHueRange NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRefineSaturationMin READ activeMaskRefineSaturationMin WRITE setActiveMaskRefineSaturationMin NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRefineSaturationMax READ activeMaskRefineSaturationMax WRITE setActiveMaskRefineSaturationMax NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRefineDepthMin READ activeMaskRefineDepthMin WRITE setActiveMaskRefineDepthMin NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRefineDepthMax READ activeMaskRefineDepthMax WRITE setActiveMaskRefineDepthMax NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRefineEdgeConfidenceMin READ activeMaskRefineEdgeConfidenceMin WRITE setActiveMaskRefineEdgeConfidenceMin NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRefineRangeFeather READ activeMaskRefineRangeFeather WRITE setActiveMaskRefineRangeFeather NOTIFY controlsChanged)
  Q_PROPERTY(bool linearMaskEnabled READ linearMaskEnabled WRITE setLinearMaskEnabled NOTIFY controlsChanged)
  Q_PROPERTY(double linearMaskExposure READ linearMaskExposure WRITE setLinearMaskExposure NOTIFY controlsChanged)
  Q_PROPERTY(double linearMaskTemperature READ linearMaskTemperature WRITE setLinearMaskTemperature NOTIFY controlsChanged)
  Q_PROPERTY(double linearMaskFeather READ linearMaskFeather WRITE setLinearMaskFeather NOTIFY controlsChanged)
  Q_PROPERTY(double linearMaskPosition READ linearMaskPosition WRITE setLinearMaskPosition NOTIFY controlsChanged)
  Q_PROPERTY(double linearMaskRotation READ linearMaskRotation WRITE setLinearMaskRotation NOTIFY controlsChanged)
  Q_PROPERTY(bool radialMaskEnabled READ radialMaskEnabled WRITE setRadialMaskEnabled NOTIFY controlsChanged)
  Q_PROPERTY(double radialMaskExposure READ radialMaskExposure WRITE setRadialMaskExposure NOTIFY controlsChanged)
  Q_PROPERTY(double radialMaskTemperature READ radialMaskTemperature WRITE setRadialMaskTemperature NOTIFY controlsChanged)
  Q_PROPERTY(double radialMaskFeather READ radialMaskFeather WRITE setRadialMaskFeather NOTIFY controlsChanged)
  Q_PROPERTY(double radialMaskCenterX READ radialMaskCenterX WRITE setRadialMaskCenterX NOTIFY controlsChanged)
  Q_PROPERTY(double radialMaskCenterY READ radialMaskCenterY WRITE setRadialMaskCenterY NOTIFY controlsChanged)
  Q_PROPERTY(double radialMaskRadiusX READ radialMaskRadiusX WRITE setRadialMaskRadiusX NOTIFY controlsChanged)
  Q_PROPERTY(double radialMaskRadiusY READ radialMaskRadiusY WRITE setRadialMaskRadiusY NOTIFY controlsChanged)
  Q_PROPERTY(double radialMaskRotation READ radialMaskRotation WRITE setRadialMaskRotation NOTIFY controlsChanged)
  Q_PROPERTY(bool whiteBalanceSampling READ whiteBalanceSampling WRITE setWhiteBalanceSampling NOTIFY whiteBalanceSamplingChanged)
  Q_PROPERTY(QString colorToolSamplingTarget READ colorToolSamplingTarget WRITE setColorToolSamplingTarget NOTIFY colorToolSamplingTargetChanged)

 public:
  void setEditAccessPolicy(std::function<bool()> policy) { customer_edit_access_.setPolicy(std::move(policy)); }

  explicit EditorPreviewController(QObject* parent = nullptr);
  ~EditorPreviewController() override;
  bool sourceSamplingBusy() const { return source_sample_active_; }
  bool prepareRecipeClose(QString* error);
  void pauseRecipePersistence(bool paused);
  QProcess* recipeSaveProcessForClose() { return &persist_process_; }

  [[nodiscard]] QString previewSource() const;
  [[nodiscard]] QString displayPreviewSource() const;
  [[nodiscard]] QString baseImageThumbnailSource() const;
  [[nodiscard]] QString displayPreviewOverlaySource() const;
  [[nodiscard]] QString focusCheckOverlaySource() const;
  [[nodiscard]] QString focusPeakingOverlaySource() const;
  [[nodiscard]] QString clippingOverlaySource() const;
  [[nodiscard]] QString falseColorOverlaySource() const;
  [[nodiscard]] QString gamutOverlaySource() const;
  [[nodiscard]] QString zoneMapOverlaySource() const;
  Q_INVOKABLE void setDiagnosticOverlaysRequested(bool requested);
  [[nodiscard]] bool colorToolMaskPreviewEnabled() const;
  [[nodiscard]] QString colorToolMaskPreviewSource() const;
  [[nodiscard]] bool colorToolMaskPreviewBusy() const;
  [[nodiscard]] bool displayPreviewNativeAvailable() const;
  [[nodiscard]] quint64 displayPreviewNativeHandle() const;
  [[nodiscard]] std::shared_ptr<void> displayPreviewNativeTextureLease() const;
  [[nodiscard]] QSize displayPreviewNativeSize() const;
  [[nodiscard]] bool displayPreviewNativeFullResolution() const;
  [[nodiscard]] bool displayPreviewNativeViewportFullResolution() const;
  [[nodiscard]] int displayPreviewNativeRevision() const;
  [[nodiscard]] QString referencePreviewSource() const;
  [[nodiscard]] QString displayReferencePreviewSource() const;
  [[nodiscard]] QString referenceImagePath() const;
  [[nodiscard]] QString referenceImageName() const;
  [[nodiscard]] bool hasReferenceImage() const;
  [[nodiscard]] QRectF displayPreviewOverlayRect() const;
  [[nodiscard]] bool previewViewportActive() const;
  [[nodiscard]] QRectF previewViewportRect() const;
  [[nodiscard]] int previewViewportPatchMaxEdge() const;
  [[nodiscard]] QVariantList histogramRed() const;
  [[nodiscard]] QVariantList histogramGreen() const;
  [[nodiscard]] QVariantList histogramBlue() const;
  [[nodiscard]] QVariantList histogramLuminance() const;
  [[nodiscard]] QVariantList histogramRedCounts() const;
  [[nodiscard]] QVariantList histogramGreenCounts() const;
  [[nodiscard]] QVariantList histogramBlueCounts() const;
  [[nodiscard]] QVariantList histogramLuminanceCounts() const;
  [[nodiscard]] QVariantList scopeWaveformRed() const;
  [[nodiscard]] QVariantList scopeWaveformGreen() const;
  [[nodiscard]] QVariantList scopeWaveformBlue() const;
  [[nodiscard]] QVariantList scopeWaveformLuminance() const;
  [[nodiscard]] QVariantList scopeDensityRed() const;
  [[nodiscard]] QVariantList scopeDensityGreen() const;
  [[nodiscard]] QVariantList scopeDensityBlue() const;
  [[nodiscard]] QVariantList scopeDensityLuminance() const;
  [[nodiscard]] QVariantList scopeVectorscopePoints() const;
  [[nodiscard]] int histogramShadowClipCount() const;
  [[nodiscard]] int histogramHighlightClipCount() const;
  [[nodiscard]] int histogramSampleCount() const;
  [[nodiscard]] int rawClipSampleCount() const;
  [[nodiscard]] int rawShadowClipCount() const;
  [[nodiscard]] int rawHighlightClipCount() const;
  [[nodiscard]] QVariantMap pixelInspector() const;
  [[nodiscard]] bool histogramUpdateActive() const;
  [[nodiscard]] bool scopesVisible() const;
  [[nodiscard]] QString scopeDomain() const;
  [[nodiscard]] QString scopeDomainLabel() const;
  [[nodiscard]] int previewTargetEdge() const { return clamp_preview_target_edge(preview_target_edge_); }
  [[nodiscard]] QVariantMap colorTransformDiagnostics() const;
  [[nodiscard]] QVariantMap iccProfileAssets() const;
  [[nodiscard]] QVariantMap cameraDcpProfileAsset() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] QString recipeSaveError() const { return recipe_save_error_; }
  [[nodiscard]] bool catalogRecipePersistenceEnabled() const { return catalog_recipe_persistence_enabled_; }
  void setCatalogRecipePersistenceEnabled(bool enabled) {
    if (catalog_recipe_persistence_enabled_ == enabled) return;
    catalog_recipe_persistence_enabled_ = enabled;
    emit controlsChanged();
  }
  [[nodiscard]] bool recipeSavePending() const { return recipe_save_queue_.active() || recipe_save_queue_.pending(); }
  [[nodiscard]] QString selectionFailureReason() const;
  [[nodiscard]] QString previewFailureStage() const;
  [[nodiscard]] QString previewFailureDetail() const;
  [[nodiscard]] QStringList previewFailureLayerIds() const;
  [[nodiscard]] QStringList previewFailureMaskSetIds() const;
  [[nodiscard]] QString assetId() const;
  [[nodiscard]] QString cameraLabel() const;
  [[nodiscard]] QString captureDate() const;
  [[nodiscard]] QString lensModel() const;
  [[nodiscard]] bool hasAsset() const;
  [[nodiscard]] bool subjectMaskAvailable() const;
  [[nodiscard]] QString aiModelStatus() const;
  [[nodiscard]] QString aiModelDiagnostics() const;
  [[nodiscard]] bool aiSmartSelectionAvailable() const;
  [[nodiscard]] bool aiInpaintAvailable() const;
  [[nodiscard]] QString smartSelectionRuntimeLabel() const;
  [[nodiscard]] QString inpaintRuntimeLabel() const;
  [[nodiscard]] bool maskActionRetryAvailable() const;
  [[nodiscard]] QString maskActionRetryLabel() const;
  [[nodiscard]] QString maskActionRetryDetail() const;
  [[nodiscard]] QString inpaintPrompt() const;
  [[nodiscard]] int inpaintSeed() const;
  [[nodiscard]] double inpaintBlendAmount() const;
  [[nodiscard]] QString inpaintQualityMode() const;
  [[nodiscard]] bool inpaintProgressActive() const;
  [[nodiscard]] double inpaintProgressValue() const;
  [[nodiscard]] QString inpaintProgressLabel() const;
  [[nodiscard]] QString inpaintProgressSignature() const;
  [[nodiscard]] bool retouchBeforeAfterEnabled() const;
  [[nodiscard]] bool inpaintPatchDiagnosticsAvailable() const;
  [[nodiscard]] bool inpaintPatchDiagnosticsEnabled() const;
  [[nodiscard]] QRectF activeInpaintPatchBounds() const;
  [[nodiscard]] QVariantList activeInpaintCandidates() const;
  [[nodiscard]] int activeInpaintCandidateCount() const;
  [[nodiscard]] int activeInpaintCandidateIndex() const;
  [[nodiscard]] bool activeInpaintVariationPickerVisible() const;
  [[nodiscard]] bool activeInpaintRegenerationAvailable() const;
  [[nodiscard]] bool activeInpaintFinalizationAvailable() const;
  [[nodiscard]] bool processing() const;
  [[nodiscard]] bool fullRenderRunning() const;
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;
  [[nodiscard]] QString undoLabel() const;
  [[nodiscard]] QString redoLabel() const;
  [[nodiscard]] QVariantList editHistoryEntries() const;
  [[nodiscard]] int editHistoryCurrentIndex() const;
  [[nodiscard]] QVariantList editHistorySnapshots() const;
  Q_INVOKABLE bool setEditHistoryPreviewIndex(int index);
  [[nodiscard]] bool hasCopiedPhotoRecipe() const;
  [[nodiscard]] QString copiedPhotoRecipePatchJson() const;
  Q_INVOKABLE bool copyPhotoRecipe(const QString& payload, const QStringList& groups);
  Q_INVOKABLE void applyCatalogRecipeChange(
      const QString& asset_id, const QString& file_path,
      const QString& previous_patch, const QString& next_patch, bool restoring_history);
  [[nodiscard]] bool hasCopiedSettings() const;
  [[nodiscard]] bool hasCopiedPhotoLayerAdjustments() const;
  [[nodiscard]] QStringList presetNames() const;
  [[nodiscard]] QStringList presetPackNames() const;
  [[nodiscard]] QStringList presetCategoryNames() const;
  [[nodiscard]] QStringList favoritePresetNames() const;
  [[nodiscard]] double presetIntensity() const;
  [[nodiscard]] bool hasCameraDefault() const;
  [[nodiscard]] bool compareMode() const;
  [[nodiscard]] QString compareSourceMode() const;
  [[nodiscard]] QString compareSnapshotId() const;
  [[nodiscard]] QString compareSnapshotName() const;
  [[nodiscard]] QString currentPhotographicRenderingModel() const;
  [[nodiscard]] QString renderingUpgradeTargetModel() const;
  [[nodiscard]] bool renderingUpgradeAvailable() const;
  [[nodiscard]] bool renderingUpgradePreviewActive() const;
  [[nodiscard]] QString renderingUpgradeStatus() const;
  [[nodiscard]] QVariantMap renderingUpgradeEvidence() const;
  [[nodiscard]] QString sectionComparisonScope() const { return section_comparison_scope_; }
  [[nodiscard]] QString sectionComparisonLayerId() const { return section_comparison_layer_id_; }
  Q_INVOKABLE bool setSectionComparison(const QString& scope, int layer_index = -1);
  Q_INVOKABLE void clearSectionComparison();
  Q_INVOKABLE bool beginSectionComparisonHold(const QString& scope, int layer_index = -1);
  Q_INVOKABLE void endSectionComparisonHold();
  [[nodiscard]] QStringList compareAdjustmentGroups() const;
  [[nodiscard]] QStringList maskCompareAdjustmentGroups() const;
  [[nodiscard]] bool cropEditing() const;
  [[nodiscard]] QString inputColorProfile() const;
  [[nodiscard]] QString outputColorSpace() const;
  [[nodiscard]] QString previewOutputMode() const;
  [[nodiscard]] QStringList previewOutputModeOptions() const;
  [[nodiscard]] QString referenceDisplayTarget() const;
  [[nodiscard]] QString monitorColorProfile() const;
  [[nodiscard]] QString systemMonitorProfileName() const;
  [[nodiscard]] QString systemMonitorDisplayIdentity() const;
  [[nodiscard]] QVariantMap systemMonitorDisplayTarget() const;
  Q_INVOKABLE void refreshDisplayTarget();
  [[nodiscard]] bool softProofEnabled() const;
  [[nodiscard]] QString softProofProfile() const;
  [[nodiscard]] QString proofRenderIntent() const;
  [[nodiscard]] bool proofSimulatePaperWhite() const;
  [[nodiscard]] bool proofSimulateInkBlack() const;
  [[nodiscard]] bool rawControlsAvailable() const;
  [[nodiscard]] double rawExposure() const;
  [[nodiscard]] double rawHotDeadPixelCleanup() const;
  [[nodiscard]] double rawFalseColorSuppression() const;
  [[nodiscard]] bool rawHighlightRecovery() const;
  [[nodiscard]] QString highlightReconstructionMode() const;
  [[nodiscard]] QString rawDemosaicQuality() const;
  [[nodiscard]] bool rawLensCorrection() const;
  [[nodiscard]] bool rawWhiteBalanceOverride() const;
  [[nodiscard]] QString rawCameraMatchingLook() const;
  [[nodiscard]] double rawWhiteBalanceTemperature() const;
  [[nodiscard]] double rawWhiteBalanceTint() const;
  [[nodiscard]] double rawAsShotWhiteBalanceTemperature() const;
  [[nodiscard]] double rawAsShotWhiteBalanceTint() const;
  [[nodiscard]] double rawBlackLevel() const;
  [[nodiscard]] double rawWhiteLevel() const;
  [[nodiscard]] double rawCalibrationHue() const;
  [[nodiscard]] double rawCalibrationSaturation() const;
  [[nodiscard]] double rawRedPrimaryHue() const;
  [[nodiscard]] double rawRedPrimarySaturation() const;
  [[nodiscard]] double rawGreenPrimaryHue() const;
  [[nodiscard]] double rawGreenPrimarySaturation() const;
  [[nodiscard]] double rawBluePrimaryHue() const;
  [[nodiscard]] double rawBluePrimarySaturation() const;
  [[nodiscard]] double exposure() const;
  [[nodiscard]] double contrast() const;
  [[nodiscard]] double highlights() const;
  [[nodiscard]] double shadows() const;
  [[nodiscard]] double whites() const;
  [[nodiscard]] double blacks() const;
  [[nodiscard]] double brightness() const;
  [[nodiscard]] double hdrLocalToneMapping() const;
  [[nodiscard]] double filmicShoulder() const;
  [[nodiscard]] double filmicToe() const;
  [[nodiscard]] double adaptiveContrast() const;
  [[nodiscard]] double levelsBlackPoint() const;
  [[nodiscard]] double levelsGamma() const;
  [[nodiscard]] double levelsWhitePoint() const;
  [[nodiscard]] double zoneDeepShadows() const;
  [[nodiscard]] double zoneShadows() const;
  [[nodiscard]] double zoneMidtones() const;
  [[nodiscard]] double zoneLights() const;
  [[nodiscard]] double zoneHighlights() const;
  [[nodiscard]] double temperature() const;
  [[nodiscard]] double tint() const;
  [[nodiscard]] double texture() const;
  [[nodiscard]] double clarity() const;
  [[nodiscard]] double dehaze() const;
  [[nodiscard]] double sharpening() const;
  [[nodiscard]] double sharpenRadius() const;
  [[nodiscard]] double sharpenDetail() const;
  [[nodiscard]] double sharpenMasking() const;
  [[nodiscard]] double luminanceNr() const;
  [[nodiscard]] double colorNr() const;
  [[nodiscard]] double lensDistortion() const;
  [[nodiscard]] double lensVignette() const;
  [[nodiscard]] double chromaticAberration() const;
  [[nodiscard]] double perspectiveVertical() const;
  [[nodiscard]] double perspectiveHorizontal() const;
  [[nodiscard]] double transformRotation() const;
  [[nodiscard]] double transformScale() const;
  [[nodiscard]] double transformSkewX() const;
  [[nodiscard]] double transformSkewY() const;
  [[nodiscard]] double liquifyCenterX() const;
  [[nodiscard]] double liquifyCenterY() const;
  [[nodiscard]] double liquifyRadius() const;
  [[nodiscard]] double liquifyStrength() const;
  [[nodiscard]] bool contentAwareCropFill() const;
  [[nodiscard]] double vibrance() const;
  [[nodiscard]] double saturation() const;
  [[nodiscard]] double curveShadows() const;
  [[nodiscard]] double curveDarks() const;
  [[nodiscard]] double curveLights() const;
  [[nodiscard]] double curveHighlights() const;
  [[nodiscard]] QString curveChannel() const;
  [[nodiscard]] QString activeCurveMode() const;
  [[nodiscard]] QVariantList activeCurvePoints() const;
  [[nodiscard]] QStringList curvePresetNames() const;
  [[nodiscard]] QString colorMixerModel() const;
  [[nodiscard]] QString hslChannel() const;
  [[nodiscard]] double hueAdjustment() const;
  [[nodiscard]] double saturationAdjustment() const;
  [[nodiscard]] double luminanceAdjustment() const;
  [[nodiscard]] double selectiveCyan() const;
  [[nodiscard]] double selectiveMagenta() const;
  [[nodiscard]] double selectiveYellow() const;
  [[nodiscard]] double selectiveBlack() const;
  [[nodiscard]] double channelMixerRedFromRed() const;
  [[nodiscard]] double channelMixerRedFromGreen() const;
  [[nodiscard]] double channelMixerRedFromBlue() const;
  [[nodiscard]] double channelMixerGreenFromRed() const;
  [[nodiscard]] double channelMixerGreenFromGreen() const;
  [[nodiscard]] double channelMixerGreenFromBlue() const;
  [[nodiscard]] double channelMixerBlueFromRed() const;
  [[nodiscard]] double channelMixerBlueFromGreen() const;
  [[nodiscard]] double channelMixerBlueFromBlue() const;
  [[nodiscard]] bool blackAndWhiteEnabled() const;
  [[nodiscard]] double blackAndWhiteReds() const;
  [[nodiscard]] double blackAndWhiteOranges() const;
  [[nodiscard]] double blackAndWhiteYellows() const;
  [[nodiscard]] double blackAndWhiteGreens() const;
  [[nodiscard]] double blackAndWhiteAquas() const;
  [[nodiscard]] double blackAndWhiteBlues() const;
  [[nodiscard]] double blackAndWhitePurples() const;
  [[nodiscard]] double blackAndWhiteMagentas() const;
  [[nodiscard]] bool colorReplacementEnabled() const;
  [[nodiscard]] double colorReplacementSourceHue() const;
  [[nodiscard]] double colorReplacementTargetHue() const;
  [[nodiscard]] double colorReplacementRange() const;
  [[nodiscard]] double colorReplacementSoftness() const;
  [[nodiscard]] double colorReplacementStrength() const;
  [[nodiscard]] bool colorReplacementPreserveLuminance() const;
  [[nodiscard]] bool colorReplacementPreserveSaturation() const;
  [[nodiscard]] bool skinToneProtectionEnabled() const;
  [[nodiscard]] double skinToneLineHue() const;
  [[nodiscard]] double skinToneLineAngle() const;
  [[nodiscard]] double skinToneProtectionRange() const;
  [[nodiscard]] double skinToneProtectionStrength() const;
  [[nodiscard]] bool colorHarmonyEnabled() const;
  [[nodiscard]] QString colorHarmonyMode() const;
  [[nodiscard]] double colorHarmonyBaseHue() const;
  [[nodiscard]] double colorHarmonyStrength() const;
  [[nodiscard]] double gradingShadowsHue() const;
  [[nodiscard]] double gradingShadowsSaturation() const;
  [[nodiscard]] double gradingShadowsLuminance() const;
  [[nodiscard]] double gradingMidtonesHue() const;
  [[nodiscard]] double gradingMidtonesSaturation() const;
  [[nodiscard]] double gradingMidtonesLuminance() const;
  [[nodiscard]] double gradingHighlightsHue() const;
  [[nodiscard]] double gradingHighlightsSaturation() const;
  [[nodiscard]] double gradingHighlightsLuminance() const;
  [[nodiscard]] double gradingAmount() const;
  [[nodiscard]] double gradingBalance() const;
  [[nodiscard]] double cropRectX() const;
  [[nodiscard]] double cropRectY() const;
  [[nodiscard]] double cropRectWidth() const;
  [[nodiscard]] double cropRectHeight() const;
  [[nodiscard]] double cropAspectRatio() const;
  [[nodiscard]] double cropZoom() const;
  [[nodiscard]] QString cropPreset() const;
  [[nodiscard]] QString uprightMode() const;
  [[nodiscard]] double uprightVerticalGuideAngle() const;
  [[nodiscard]] double uprightHorizontalGuideAngle() const;
  [[nodiscard]] QVariantList maskSets() const;
  Q_INVOKABLE QVariantList maskSetSnapshot() const { return maskSets(); }
  [[nodiscard]] int maskSetCount() const;
  [[nodiscard]] QVariantList retouchStack() const;
  [[nodiscard]] int retouchCount() const;
  [[nodiscard]] QVariantList photoLayers() const;
  Q_INVOKABLE QVariantList photoLayerSnapshot() const { return photoLayers(); }
  [[nodiscard]] int photoLayerCount() const;
  bool baseImageLocked() const;
  QVariantMap baseImageLayerData() const;
  Q_INVOKABLE bool setBaseImageLocked(bool locked);
  bool hasCopiedPhotoLayers() const;
  bool photoLayerClipboardBusy() const { return photo_layer_clipboard_busy_; }
  Q_INVOKABLE bool copyPhotoLayers(const QVariantList& indices);
  Q_INVOKABLE QStringList pastePhotoLayers(int above_index = 0, bool in_place = false);
  [[nodiscard]] bool photoLayerSoloActive() const;
  [[nodiscard]] QVariantList photoLayerVisibilityStates() const;
  [[nodiscard]] int photoLayerVisibilityStateCount() const;
  [[nodiscard]] int sourceImageWidth() const;
  [[nodiscard]] int sourceImageHeight() const;
  [[nodiscard]] QVariantList maskStack() const;
  [[nodiscard]] QVariantList typedMaskSources() const;
  [[nodiscard]] int maskCount() const;
  [[nodiscard]] int emptyMaskCount() const;
  [[nodiscard]] int staleGeneratedMaskCount() const;
  [[nodiscard]] QString staleGeneratedMaskSummary() const;
  [[nodiscard]] int activeGeneratedMaskQualityScore() const;
  [[nodiscard]] QString activeGeneratedMaskQualitySummary() const;
  [[nodiscard]] QStringList activeGeneratedMaskQualityFlags() const;
  [[nodiscard]] double activeGeneratedMaskHoleFillThreshold() const;
  [[nodiscard]] double activeGeneratedMaskIslandRemovalThreshold() const;
  [[nodiscard]] QString activeGeneratedMaskCleanupSummary() const;
  [[nodiscard]] QString maskStackAuditSignature() const;
  [[nodiscard]] int activeMaskIndex() const;
  [[nodiscard]] bool hasActiveMask() const;
  [[nodiscard]] QString activeMaskKind() const;
  [[nodiscard]] QString activeMaskToolRole() const;
  [[nodiscard]] QString activeMaskRetouchMode() const;
  [[nodiscard]] QString retouchWorkflowStateSignature() const;
  [[nodiscard]] bool activeMaskCommitted() const;
  [[nodiscard]] QString activeMaskName() const;
  [[nodiscard]] QString activeMaskSetName() const;
  [[nodiscard]] QString activeMaskOperation() const;
  [[nodiscard]] bool activeMaskEnabled() const;
  [[nodiscard]] bool activeMaskSolo() const;
  [[nodiscard]] bool activeMaskLocked() const;
  [[nodiscard]] QString activeMaskEditDisabledReason() const;
  [[nodiscard]] double activeMaskOpacity() const;
  [[nodiscard]] double activeMaskDensity() const;
  [[nodiscard]] bool activeMaskInverted() const;
  [[nodiscard]] QString activeMaskBlendMode() const;
  [[nodiscard]] double activeMaskExposure() const;
  [[nodiscard]] double activeMaskContrast() const;
  [[nodiscard]] double activeMaskHighlights() const;
  [[nodiscard]] double activeMaskShadows() const;
  [[nodiscard]] double activeMaskWhites() const;
  [[nodiscard]] double activeMaskBlacks() const;
  [[nodiscard]] double activeMaskTemperature() const;
  [[nodiscard]] double activeMaskTint() const;
  [[nodiscard]] double activeMaskVibrance() const;
  [[nodiscard]] double activeMaskSaturation() const;
  [[nodiscard]] double activeMaskHueShift() const;
  [[nodiscard]] double activeMaskSaturationShift() const;
  [[nodiscard]] double activeMaskColorizeHue() const;
  [[nodiscard]] double activeMaskColorizeStrength() const;
  [[nodiscard]] double activeMaskRemoveStrength() const;
  [[nodiscard]] double activeMaskHealTextureBlend() const;
  [[nodiscard]] double activeMaskHealColorBlend() const;
  [[nodiscard]] bool activeMaskSelectionInverted() const;
  [[nodiscard]] double activeMaskSelectionExpand() const;
  [[nodiscard]] double activeMaskSelectionSmooth() const;
  [[nodiscard]] double activeMaskSelectionFineEdge() const;
  [[nodiscard]] double activeMaskSelectionEdgeContrast() const;
  [[nodiscard]] double activeMaskSelectionDecontaminate() const;
  [[nodiscard]] double activeMaskFeather() const;
  [[nodiscard]] double activeMaskPosition() const;
  [[nodiscard]] double activeMaskRotation() const;
  [[nodiscard]] double activeMaskCenterX() const;
  [[nodiscard]] double activeMaskCenterY() const;
  [[nodiscard]] double activeMaskRadiusX() const;
  [[nodiscard]] double activeMaskRadiusY() const;
  [[nodiscard]] double activeMaskBrushSize() const;
  [[nodiscard]] double activeMaskBrushSizeNormalized() const;
  [[nodiscard]] int activeMaskBrushPreviewDiameterPx() const;
  [[nodiscard]] int activeMaskBrushFinalDiameterXPx() const;
  [[nodiscard]] int activeMaskBrushFinalDiameterYPx() const;
  [[nodiscard]] QString activeMaskBrushSizeSummary() const;
  [[nodiscard]] QString maskMemoryPolicySignature() const;
  [[nodiscard]] double activeMaskBrushFlow() const;
  [[nodiscard]] double activeMaskBrushHardness() const;
  [[nodiscard]] double activeMaskBrushSpacing() const;
  [[nodiscard]] double activeMaskBrushSmoothing() const;
  [[nodiscard]] double activeMaskBrushPressureSize() const;
  [[nodiscard]] double activeMaskBrushPressureFlow() const;
  [[nodiscard]] double activeMaskBrushPressureOpacity() const;
  [[nodiscard]] bool activeMaskBrushBuildup() const;
  [[nodiscard]] bool activeMaskBrushEraseMode() const;
  [[nodiscard]] bool activeMaskBrushAutoMask() const;
  [[nodiscard]] double activeMaskBrushAutoMaskTolerance() const;
  [[nodiscard]] bool activeMaskBrushAutoMaskContinuousSampling() const;
  [[nodiscard]] bool activeMaskBrushProtectEdges() const;
  [[nodiscard]] QString activeMaskBrushPreset() const;
  [[nodiscard]] QStringList brushPresetNames() const;
  [[nodiscard]] int activeMaskBrushPointCount() const;
  [[nodiscard]] QVariantList activeMaskLiveBrushPoints() const;
  [[nodiscard]] BrushPreviewStyle activeBrushPreviewStyle() const;
  [[nodiscard]] int activeMaskLiveBrushPreviewPointCount() const { return static_cast<int>(active_brush_live_preview_frame_.point_count); }
  [[nodiscard]] QString activeMaskLiveBrushPreviewSource() const;
  [[nodiscard]] bool activeMaskLiveBrushPreviewNativeAvailable() const;
  [[nodiscard]] quint64 activeMaskLiveBrushPreviewNativeHandle() const;
  [[nodiscard]] QSize activeMaskLiveBrushPreviewNativeSize() const;
  [[nodiscard]] int activeMaskLiveBrushPreviewRevision() const;
  [[nodiscard]] QString activeMaskLiveBrushPreviewBackend() const;
  [[nodiscard]] qint64 activeMaskLiveBrushPreviewElapsedMs() const;
  [[nodiscard]] bool activeMaskLiveBrushPreviewWithinLatencyBudget() const;
  [[nodiscard]] QString activeMaskLiveBrushPreviewFallbackReason() const;
  [[nodiscard]] bool brushStrokeActive() const;
  [[nodiscard]] bool activeMaskCloneSourceSet() const;
  [[nodiscard]] double activeMaskCloneSourceX() const;
  [[nodiscard]] double activeMaskCloneSourceY() const;
  [[nodiscard]] double activeMaskCloneScale() const;
  [[nodiscard]] double activeMaskCloneRotation() const;
  [[nodiscard]] bool activeMaskCloneAligned() const;
  [[nodiscard]] QVariantList activeMaskCloneSources() const;
  [[nodiscard]] int activeMaskCloneSourceCount() const;
  [[nodiscard]] int activeMaskCloneSourceIndex() const;
  [[nodiscard]] bool activeMaskPatchSourceSet() const;
  [[nodiscard]] double activeMaskPatchSourceX() const;
  [[nodiscard]] double activeMaskPatchSourceY() const;
  [[nodiscard]] double activeMaskPatchSourceWidth() const;
  [[nodiscard]] double activeMaskPatchSourceHeight() const;
  [[nodiscard]] bool hasSensorDustMap() const;
  [[nodiscard]] QVariantList activeMaskPolygonPoints() const;
  [[nodiscard]] int activeMaskPolygonPointCount() const;
  [[nodiscard]] bool activeMaskPolygonSelfIntersecting() const;
  [[nodiscard]] QVariantList activeMaskOutlinePoints() const;
  [[nodiscard]] QVariantList activeMaskOutlinePaths() const;
  [[nodiscard]] QVariantMap activeMaskOutlineTrace() const;
  [[nodiscard]] double activeMaskOutlineDisplayScale() const;
  void setActiveMaskOutlineDisplayScale(double scale);
  [[nodiscard]] QString activeMaskOutlineQualityTier() const;
  [[nodiscard]] QString activeMaskPreviewQualityTier() const;
  [[nodiscard]] QString activeMaskPreviewSource() const;
  [[nodiscard]] QString maskPreviewMode() const;
  [[nodiscard]] QString maskPreviewDisplayMode() const;
  [[nodiscard]] QStringList maskDisplayModeNames() const;
  [[nodiscard]] double maskPreviewOverlayOpacity() const;
  [[nodiscard]] QString maskPreviewOverlayColor() const;
  [[nodiscard]] bool maskPreviewShowAll() const;
  [[nodiscard]] bool maskPreviewTemporarilyHidden() const;
  [[nodiscard]] QString maskRefinementPreviewMode() const;
  [[nodiscard]] bool maskRefinementHighQualityPreview() const;
  [[nodiscard]] bool maskFullResolutionPreview() const;
  [[nodiscard]] bool smartSelectPreviewActive() const;
  [[nodiscard]] QString smartSelectPreviewSource() const;
  [[nodiscard]] QString smartSelectPreviewPipelineSignature() const;
  [[nodiscard]] QVariantList smartSelectPreviewOutlinePoints() const;
  [[nodiscard]] bool smartSelectStrokeActive() const;
  [[nodiscard]] QVariantList smartSelectStrokePoints() const;
  [[nodiscard]] int smartSelectCandidateCount() const;
  [[nodiscard]] int smartSelectCandidateIndex() const;
  [[nodiscard]] QString smartSelectCandidateHudText() const;
  [[nodiscard]] bool smartSelectCanCycleCandidates() const;
  [[nodiscard]] double activeMaskLuminanceMin() const;
  [[nodiscard]] double activeMaskLuminanceMax() const;
  [[nodiscard]] QVariantList activeMaskLuminanceSamples() const;
  [[nodiscard]] QVariantMap activeMaskLuminanceSamplePreview() const;
  [[nodiscard]] double activeMaskHueCenter() const;
  [[nodiscard]] double activeMaskHueRange() const;
  [[nodiscard]] double activeMaskSaturationMin() const;
  [[nodiscard]] double activeMaskSaturationMax() const;
  [[nodiscard]] QVariantList activeMaskColorSamples() const;
  [[nodiscard]] double activeMaskDepthMin() const;
  [[nodiscard]] double activeMaskDepthMax() const;
  [[nodiscard]] bool activeMaskDepthSamplingAvailable() const;
  [[nodiscard]] QString activeMaskDepthSamplingDisabledReason() const;
  [[nodiscard]] QVariantList activeMaskDepthSamples() const;
  [[nodiscard]] bool alphaMaskImportPreviewActive() const;
  [[nodiscard]] QVariantMap alphaMaskImportPreview() const;
  [[nodiscard]] bool alphaMaskImportInvert() const;
  [[nodiscard]] double alphaMaskImportThreshold() const;
  [[nodiscard]] QString alphaMaskImportResampleMode() const;
  [[nodiscard]] double activeMaskRangeFeather() const;
  [[nodiscard]] double activeMaskRefineLuminanceMin() const;
  [[nodiscard]] double activeMaskRefineLuminanceMax() const;
  [[nodiscard]] double activeMaskRefineHueCenter() const;
  [[nodiscard]] double activeMaskRefineHueRange() const;
  [[nodiscard]] double activeMaskRefineSaturationMin() const;
  [[nodiscard]] double activeMaskRefineSaturationMax() const;
  [[nodiscard]] double activeMaskRefineDepthMin() const;
  [[nodiscard]] double activeMaskRefineDepthMax() const;
  [[nodiscard]] double activeMaskRefineEdgeConfidenceMin() const;
  [[nodiscard]] double activeMaskRefineRangeFeather() const;
  [[nodiscard]] bool linearMaskEnabled() const;
  [[nodiscard]] double linearMaskExposure() const;
  [[nodiscard]] double linearMaskTemperature() const;
  [[nodiscard]] double linearMaskFeather() const;
  [[nodiscard]] double linearMaskPosition() const;
  [[nodiscard]] double linearMaskRotation() const;
  [[nodiscard]] bool radialMaskEnabled() const;
  [[nodiscard]] double radialMaskExposure() const;
  [[nodiscard]] double radialMaskTemperature() const;
  [[nodiscard]] double radialMaskFeather() const;
  [[nodiscard]] double radialMaskCenterX() const;
  [[nodiscard]] double radialMaskCenterY() const;
  [[nodiscard]] double radialMaskRadiusX() const;
  [[nodiscard]] double radialMaskRadiusY() const;
  [[nodiscard]] double radialMaskRotation() const;
  [[nodiscard]] bool whiteBalanceSampling() const;
  [[nodiscard]] QString colorToolSamplingTarget() const;

  void setPreviewTargetEdge(int edge);
  void setPresetIntensity(double intensity);
  void setCompareMode(bool compare_mode);
  void setCompareSourceMode(const QString& source_mode);
  void setCompareSnapshotId(const QString& snapshot_id);
  void setCompareAdjustmentGroups(const QStringList& groups);
  void setMaskCompareAdjustmentGroups(const QStringList& groups);
  void setCropEditing(bool crop_editing);
  void setInputColorProfile(const QString& input_color_profile);
  void setWorkingColorProfile(const QString& working_color_profile);
  void setOutputColorSpace(const QString& output_color_space);
  void setPreviewOutputMode(const QString& preview_output_mode);
  void setReferenceDisplayTarget(
      const QString& reference_display_target);
  void setMonitorColorProfile(const QString& monitor_color_profile);
  void setSoftProofEnabled(bool enabled);
  void setSoftProofProfile(const QString& soft_proof_profile);
  void setProofRenderIntent(const QString& proof_render_intent);
  void setProofSimulatePaperWhite(bool enabled);
  void setProofSimulateInkBlack(bool enabled);
  void setRawExposure(double raw_exposure);
  void setRawHotDeadPixelCleanup(double raw_hot_dead_pixel_cleanup);
  void setRawFalseColorSuppression(double raw_false_color_suppression);
  void setRawHighlightRecovery(bool enabled);
  void setHighlightReconstructionMode(const QString& mode);
  void setRawDemosaicQuality(const QString& quality);
  void setRawLensCorrection(bool enabled);
  void setRawWhiteBalanceOverride(bool enabled);
  void setRawCameraMatchingLook(const QString& look);
  void setRawWhiteBalanceTemperature(double temperature);
  void setRawWhiteBalanceTint(double tint);
  void setRawBlackLevel(double black_level);
  void setRawWhiteLevel(double white_level);
  void setRawCalibrationHue(double value);
  void setRawCalibrationSaturation(double value);
  void setRawRedPrimaryHue(double value);
  void setRawRedPrimarySaturation(double value);
  void setRawGreenPrimaryHue(double value);
  void setRawGreenPrimarySaturation(double value);
  void setRawBluePrimaryHue(double value);
  void setRawBluePrimarySaturation(double value);
  void setExposure(double exposure);
  void setContrast(double contrast);
  void setHighlights(double highlights);
  void setShadows(double shadows);
  void setWhites(double whites);
  void setBlacks(double blacks);
  void setBrightness(double brightness);
  void setHdrLocalToneMapping(double hdr_local_tone_mapping);
  void setFilmicShoulder(double filmic_shoulder);
  void setFilmicToe(double filmic_toe);
  void setAdaptiveContrast(double adaptive_contrast);
  void setLevelsBlackPoint(double black_point);
  void setLevelsGamma(double gamma);
  void setLevelsWhitePoint(double white_point);
  void setZoneDeepShadows(double value);
  void setZoneShadows(double value);
  void setZoneMidtones(double value);
  void setZoneLights(double value);
  void setZoneHighlights(double value);
  void setTemperature(double temperature);
  void setTint(double tint);
  void setTexture(double texture);
  void setClarity(double clarity);
  void setDehaze(double dehaze);
  void setSharpening(double sharpening);
  void setSharpenRadius(double sharpen_radius);
  void setSharpenDetail(double sharpen_detail);
  void setSharpenMasking(double sharpen_masking);
  void setLuminanceNr(double luminance_nr);
  void setColorNr(double color_nr);
  void setLensDistortion(double lens_distortion);
  void setLensVignette(double lens_vignette);
  void setChromaticAberration(double chromatic_aberration);
  void setPerspectiveVertical(double perspective_vertical);
  void setPerspectiveHorizontal(double perspective_horizontal);
  void setTransformRotation(double transform_rotation);
  void setTransformScale(double transform_scale);
  void setTransformSkewX(double transform_skew_x);
  void setTransformSkewY(double transform_skew_y);
  void setLiquifyCenterX(double liquify_center_x);
  void setLiquifyCenterY(double liquify_center_y);
  void setLiquifyRadius(double liquify_radius);
  void setLiquifyStrength(double liquify_strength);
  void setContentAwareCropFill(bool enabled);
  void setVibrance(double vibrance);
  void setSaturation(double saturation);
  void setCurveShadows(double curve_shadows);
  void setCurveDarks(double curve_darks);
  void setCurveLights(double curve_lights);
  void setCurveHighlights(double curve_highlights);
  void setCurveChannel(const QString& curve_channel);
  void setActiveCurvePoints(const QVariantList& active_curve_points);
  void setColorMixerModel(const QString& model);
  void setHslChannel(const QString& hsl_channel);
  void setHueAdjustment(double hue_adjustment);
  void setSaturationAdjustment(double saturation_adjustment);
  void setLuminanceAdjustment(double luminance_adjustment);
  void setSelectiveCyan(double value);
  void setSelectiveMagenta(double value);
  void setSelectiveYellow(double value);
  void setSelectiveBlack(double value);
  void setChannelMixerRedFromRed(double value);
  void setChannelMixerRedFromGreen(double value);
  void setChannelMixerRedFromBlue(double value);
  void setChannelMixerGreenFromRed(double value);
  void setChannelMixerGreenFromGreen(double value);
  void setChannelMixerGreenFromBlue(double value);
  void setChannelMixerBlueFromRed(double value);
  void setChannelMixerBlueFromGreen(double value);
  void setChannelMixerBlueFromBlue(double value);
  void setBlackAndWhiteEnabled(bool enabled);
  void setBlackAndWhiteReds(double value);
  void setBlackAndWhiteOranges(double value);
  void setBlackAndWhiteYellows(double value);
  void setBlackAndWhiteGreens(double value);
  void setBlackAndWhiteAquas(double value);
  void setBlackAndWhiteBlues(double value);
  void setBlackAndWhitePurples(double value);
  void setBlackAndWhiteMagentas(double value);
  void setColorReplacementEnabled(bool enabled);
  void setColorReplacementSourceHue(double value);
  void setColorReplacementTargetHue(double value);
  void setColorReplacementRange(double value);
  void setColorReplacementSoftness(double value);
  void setColorReplacementStrength(double value);
  void setColorReplacementPreserveLuminance(bool enabled);
  void setColorReplacementPreserveSaturation(bool enabled);
  void setColorToolMaskPreviewEnabled(bool enabled);
  void setSkinToneProtectionEnabled(bool enabled);
  void setSkinToneLineHue(double value);
  void setSkinToneProtectionRange(double value);
  void setSkinToneProtectionStrength(double value);
  void setColorHarmonyEnabled(bool enabled);
  void setColorHarmonyMode(const QString& mode);
  void setColorHarmonyBaseHue(double value);
  void setColorHarmonyStrength(double value);
  void setGradingShadowsHue(double value);
  void setGradingShadowsSaturation(double value);
  void setGradingShadowsLuminance(double value);
  void setGradingMidtonesHue(double value);
  void setGradingMidtonesSaturation(double value);
  void setGradingMidtonesLuminance(double value);
  void setGradingHighlightsHue(double value);
  void setGradingHighlightsSaturation(double value);
  void setGradingHighlightsLuminance(double value);
  void setGradingAmount(double value);
  void setGradingBalance(double value);
  Q_INVOKABLE void updateCropRect(double x, double y, double width, double height);
  Q_INVOKABLE void resetCropRect();
  Q_INVOKABLE bool resetTransform();
  Q_INVOKABLE void applyGuidedUpright();
  Q_INVOKABLE bool straightenFromHorizon(
      double start_x,
      double start_y,
      double end_x,
      double end_y);
  Q_INVOKABLE bool straightenFromHorizonAngle(double horizon_angle);
  Q_INVOKABLE void setPreviewViewport(
      double normalized_x,
      double normalized_y,
      double normalized_width,
      double normalized_height,
      int patch_max_edge,
      bool active = true);
  Q_INVOKABLE void setPreviewPresentationMetrics(
      double content_width, double content_height, double device_pixel_ratio, bool settled_mask_quality);
  Q_INVOKABLE void schedulePreviewViewportPublication(
      double normalized_x, double normalized_y, double normalized_width, double normalized_height,
      double source_width, double source_height, double display_width, double display_height,
      double device_pixel_ratio, bool mask_work_active, bool pixel_inspection_active,
      bool active, bool immediate, bool force = false);
  void setCropZoom(double crop_zoom);
  void setCropPreset(const QString& crop_preset);
  void setUprightMode(const QString& upright_mode);
  void setUprightVerticalGuideAngle(double guide_angle);
  void setUprightHorizontalGuideAngle(double guide_angle);
  void setActiveMaskIndex(int index);
  Q_INVOKABLE bool selectMaskSet(const QString& set_id, const QString& component_id = {});
  void setMaskPreviewMode(const QString& mode);
  void setMaskPreviewOverlayOpacity(double opacity);
  void setMaskPreviewOverlayColor(const QString& color);
  void setMaskPreviewShowAll(bool show_all);
  void setMaskPreviewTemporarilyHidden(bool hidden);
  void setMaskRefinementPreviewMode(const QString& mode);
  void setMaskRefinementHighQualityPreview(bool enabled);
  void setMaskFullResolutionPreview(bool enabled);
  void setActiveMaskName(const QString& name);
  void setInpaintPrompt(const QString& prompt);
  void setInpaintSeed(int seed);
  void setInpaintBlendAmount(double amount);
  void setInpaintQualityMode(const QString& mode);
  void setRetouchBeforeAfterEnabled(bool enabled);
  void setInpaintPatchDiagnosticsEnabled(bool enabled);
  void setActiveInpaintCandidateIndex(int index);
  void setActiveMaskOperation(const QString& operation);
  void setActiveMaskEnabled(bool enabled);
  void setActiveMaskSolo(bool solo);
  void setActiveMaskLocked(bool locked);
  void setActiveMaskOpacity(double opacity);
  void setActiveMaskDensity(double density);
  void setActiveMaskInverted(bool inverted);
  void setActiveMaskBlendMode(const QString& blend_mode);
  void setActiveMaskExposure(double local_exposure);
  void setActiveMaskContrast(double contrast);
  void setActiveMaskHighlights(double highlights);
  void setActiveMaskShadows(double shadows);
  void setActiveMaskWhites(double whites);
  void setActiveMaskBlacks(double blacks);
  void setActiveMaskTemperature(double local_temperature);
  void setActiveMaskTint(double tint);
  void setActiveMaskVibrance(double vibrance);
  void setActiveMaskSaturation(double saturation);
  void setActiveMaskHueShift(double hue_shift);
  void setActiveMaskSaturationShift(double saturation_shift);
  void setActiveMaskColorizeHue(double colorize_hue);
  void setActiveMaskColorizeStrength(double colorize_strength);
  void setActiveMaskRemoveStrength(double remove_strength);
  void setActiveMaskHealTextureBlend(double texture_blend);
  void setActiveMaskHealColorBlend(double color_blend);
  void setActiveMaskSelectionInverted(bool selection_inverted);
  void setActiveMaskSelectionExpand(double selection_expand);
  void setActiveMaskSelectionSmooth(double selection_smooth);
  void setActiveMaskSelectionFineEdge(double fine_edge);
  void setActiveMaskSelectionEdgeContrast(double edge_contrast);
  void setActiveMaskSelectionDecontaminate(double decontaminate);
  Q_INVOKABLE bool resetActiveMaskRefinement();
  Q_INVOKABLE bool applyActiveSelectionPixel(
      double normalized_x,
      double normalized_y,
      const QString& operation);
  void setActiveMaskFeather(double local_feather);
  void setActiveMaskPosition(double local_position);
  void setActiveMaskRotation(double local_rotation);
  void setActiveMaskCenterX(double center_x);
  void setActiveMaskCenterY(double center_y);
  void setActiveMaskRadiusX(double radius_x);
  void setActiveMaskRadiusY(double radius_y);
  void setActiveMaskBrushSize(double brush_size);
  void setActiveMaskBrushFlow(double brush_flow);
  void setActiveMaskBrushHardness(double brush_hardness);
  void setActiveMaskBrushSpacing(double brush_spacing);
  void setActiveMaskBrushSmoothing(double brush_smoothing);
  void setActiveMaskBrushPressureSize(double pressure_size);
  void setActiveMaskBrushPressureFlow(double pressure_flow);
  void setActiveMaskBrushPressureOpacity(double pressure_opacity);
  Q_INVOKABLE bool resetActiveMaskBrushDefaults();
  void setActiveMaskBrushBuildup(bool brush_buildup);
  void setActiveMaskBrushEraseMode(bool erase_mode);
  void setActiveMaskBrushAutoMask(bool auto_mask);
  void setActiveMaskBrushAutoMaskTolerance(double tolerance);
  void setActiveMaskBrushAutoMaskContinuousSampling(bool continuous_sampling);
  void setActiveMaskBrushProtectEdges(bool protect_edges);
  Q_INVOKABLE bool applyActiveMaskBrushPreset(const QString& name);
  Q_INVOKABLE bool saveActiveMaskBrushPreset(const QString& name, bool overwrite = true);
  Q_INVOKABLE bool deleteCustomBrushPreset(const QString& name);
  Q_INVOKABLE bool resetCustomBrushPresets();
  Q_INVOKABLE bool isCustomBrushPreset(const QString& name) const;
  void setActiveMaskCloneScale(double clone_scale);
  void setActiveMaskCloneRotation(double clone_rotation);
  void setActiveMaskCloneAligned(bool clone_aligned);
  Q_INVOKABLE bool saveActiveCloneSource();
  Q_INVOKABLE bool selectActiveCloneSource(int index);
  Q_INVOKABLE bool deleteActiveCloneSource(int index);
  Q_INVOKABLE bool setActiveMaskPatchSourceRegion(
      double normalized_x,
      double normalized_y,
      double normalized_width,
      double normalized_height);
  void setActiveMaskLuminanceMin(double min_value);
  void setActiveMaskLuminanceMax(double max_value);
  Q_INVOKABLE bool beginActiveLuminanceRangeSample(double normalized_x, double normalized_y);
  Q_INVOKABLE bool updateActiveLuminanceRangeSample(double normalized_x, double normalized_y);
  Q_INVOKABLE bool commitActiveLuminanceRangeSample(double normalized_x, double normalized_y);
  Q_INVOKABLE bool cancelActiveLuminanceRangeSample();
  Q_INVOKABLE bool removeActiveLuminanceRangeSample(int index);
  void setActiveMaskHueCenter(double center);
  void setActiveMaskHueRange(double range);
  void setActiveMaskSaturationMin(double min_value);
  void setActiveMaskSaturationMax(double max_value);
  Q_INVOKABLE bool sampleActiveColorRangeAt(double normalized_x, double normalized_y);
  Q_INVOKABLE bool removeActiveColorRangeSample(int index);
  void setActiveMaskDepthMin(double min_value);
  void setActiveMaskDepthMax(double max_value);
  Q_INVOKABLE bool sampleActiveDepthRangeAt(double normalized_x, double normalized_y);
  Q_INVOKABLE bool removeActiveDepthRangeSample(int index);
  void setAlphaMaskImportInvert(bool invert);
  void setAlphaMaskImportThreshold(double threshold);
  void setAlphaMaskImportResampleMode(const QString& mode);
  void setActiveMaskRangeFeather(double feather);
  void setActiveMaskRefineLuminanceMin(double min_value);
  void setActiveMaskRefineLuminanceMax(double max_value);
  void setActiveMaskRefineHueCenter(double center);
  void setActiveMaskRefineHueRange(double range);
  void setActiveMaskRefineSaturationMin(double min_value);
  void setActiveMaskRefineSaturationMax(double max_value);
  void setActiveMaskRefineDepthMin(double min_value);
  void setActiveMaskRefineDepthMax(double max_value);
  void setActiveMaskRefineEdgeConfidenceMin(double min_value);
  void setActiveMaskRefineRangeFeather(double feather);
  void setActiveGeneratedMaskHoleFillThreshold(double threshold);
  void setActiveGeneratedMaskIslandRemovalThreshold(double threshold);
  void setLinearMaskEnabled(bool enabled);
  void setLinearMaskExposure(double local_exposure);
  void setLinearMaskTemperature(double local_temperature);
  void setLinearMaskFeather(double local_feather);
  void setLinearMaskPosition(double local_position);
  void setLinearMaskRotation(double local_rotation);
  void setRadialMaskEnabled(bool enabled);
  void setRadialMaskExposure(double local_exposure);
  void setRadialMaskTemperature(double local_temperature);
  void setRadialMaskFeather(double local_feather);
  void setRadialMaskCenterX(double center_x);
  void setRadialMaskCenterY(double center_y);
  void setRadialMaskRadiusX(double radius_x);
  void setRadialMaskRadiusY(double radius_y);
  void setRadialMaskRotation(double local_rotation);

  Q_INVOKABLE void loadAsset(
      const QString& asset_id,
      const QString& file_path,
      const QString& thumbnail_source,
      const QString& camera_label,
      const QString& capture_date,
      const QString& lens_model,
      const QString& adjustment_payload_json);
  Q_INVOKABLE QVariantMap importIccProfileAsset(
      const QString& slot,
      const QString& file_path);
  Q_INVOKABLE QVariantMap relinkIccProfileAsset(
      const QString& slot,
      const QString& file_path);
  Q_INVOKABLE bool clearIccProfileAsset(const QString& slot);
  Q_INVOKABLE QVariantMap importDcpProfileAsset(const QString& file_path);
  Q_INVOKABLE QVariantMap relinkDcpProfileAsset(const QString& file_path);
  Q_INVOKABLE bool clearDcpProfileAsset();
  Q_INVOKABLE bool retryPreviewRender();
  Q_INVOKABLE bool retryThumbnailRender();
  Q_INVOKABLE bool requestLayerFullResolutionPreview();
  Q_INVOKABLE bool applyExternalAdjustmentPatchEdit(
      const QString& asset_id,
      const QString&
          previous_adjustment_patch_json,
      const QString&
          adjustment_patch_json,
      const QString& history_label = QStringLiteral("Looksy assistant edit"));
  Q_INVOKABLE bool startRenderingUpgradePreview();
  Q_INVOKABLE bool applyRenderingUpgrade();
  Q_INVOKABLE void cancelRenderingUpgradePreview();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE bool restoreEditHistoryIndex(int index);
  Q_INVOKABLE QString createEditHistorySnapshot(const QString& name);
  Q_INVOKABLE bool restoreEditHistorySnapshot(const QString& snapshot_id);
  Q_INVOKABLE bool deleteEditHistorySnapshot(const QString& snapshot_id);
  Q_INVOKABLE void beginCoalescedEdit();
  Q_INVOKABLE void endCoalescedEdit();
  Q_INVOKABLE bool cancelCoalescedEdit();
  Q_INVOKABLE bool previewPhotoLayerEffect(int layer_index, const QString& type);
  Q_INVOKABLE bool finishPhotoLayerEffectPreview(bool apply);
  Q_INVOKABLE void resetAdjustments();
  Q_INVOKABLE bool resetAdjustmentGroup(const QString& group);
  Q_INVOKABLE bool resetChannelMixer();
  Q_INVOKABLE bool resetChannelMixerRow(const QString& output_channel);
  Q_INVOKABLE bool applyChannelMixerPreset(const QString& preset);
  Q_INVOKABLE QString currentAdjustmentPatchJson(const QStringList& groups = {}) const;
  Q_INVOKABLE QString currentAssetBoundAdjustmentPatchJson() const;
  Q_INVOKABLE QString copiedSettingsAdjustmentPatchJson(const QStringList& groups = {}) const;
  Q_INVOKABLE QString presetAdjustmentPatchJson(const QString& name) const;
  Q_INVOKABLE QString presetPreviewAdjustmentPatchJson(const QString& name, const QStringList& groups = {}, double intensity = -1.0) const;
  Q_INVOKABLE QVariantMap presetVersionInfo(
      const QString& name) const;
  Q_INVOKABLE QString presetPackName(const QString& name) const;
  Q_INVOKABLE QString presetCategoryName(const QString& name) const;
  Q_INVOKABLE QStringList presetAdjustmentGroups(const QString& name) const;
  Q_INVOKABLE QStringList presetSafetyWarnings(
      const QString& name,
      const QStringList& adjustment_groups = {}) const;
  Q_INVOKABLE QStringList presetChangedControls(
      const QString& name,
      const QStringList& adjustment_groups = {}) const;
  Q_INVOKABLE bool isPresetAdaptive(const QString& name) const;
  Q_INVOKABLE QString presetThumbnailSource(const QString& name) const;
  Q_INVOKABLE QStringList searchPresetNames(const QString& query) const;
  Q_INVOKABLE bool isUserPreset(const QString& name) const;
  Q_INVOKABLE bool isPresetFavorite(const QString& name) const;
  Q_INVOKABLE void setPresetFavorite(const QString& name, bool favorite);
  Q_INVOKABLE bool copyCurrentSettings();
  Q_INVOKABLE bool pasteCopiedSettings(const QStringList& adjustment_groups = {});
  Q_INVOKABLE bool savePreset(const QString& name, bool overwrite = true);
  Q_INVOKABLE bool savePresetToPack(
      const QString& name,
      const QString& pack,
      bool overwrite = true);
  Q_INVOKABLE QStringList presetAuthoringAdjustmentGroups() const;
  Q_INVOKABLE QVariantMap saveCustomPreset(
      const QString& name,
      const QString& pack,
      const QStringList& adjustment_groups = {},
      int layer_index = -1,
      bool overwrite = false);
  Q_INVOKABLE QVariantMap duplicatePreset(
      const QString& source_name,
      const QString& new_name,
      const QString& pack = {});
  Q_INVOKABLE bool applyPreset(const QString& name, const QStringList& adjustment_groups = {});
  Q_INVOKABLE bool applyPresetWithIntensity(
      const QString& name,
      double intensity,
      const QStringList& adjustment_groups = {});
  Q_INVOKABLE bool applyPresetToPhotoLayer(
      int index,
      const QString& name,
      double intensity,
      const QStringList& adjustment_groups = {});
  Q_INVOKABLE QVariantMap photoLayerPresetPreviewRecipe(
      int index,
      const QString& name,
      const QStringList& adjustment_groups = {},
      double intensity = -1.0) const;
  Q_INVOKABLE bool renamePreset(const QString& old_name, const QString& new_name);
  Q_INVOKABLE bool movePresetToPack(const QString& name, const QString& pack);
  Q_INVOKABLE bool deletePreset(const QString& name);
  Q_INVOKABLE bool exportPresetPack(const QString& file_path, const QString& pack = {}) const;
  Q_INVOKABLE bool importPresetPack(const QString& file_path, bool overwrite = true);
  Q_INVOKABLE QString presetSyncManifestJson() const;
  Q_INVOKABLE bool exportPresetSyncBundle(const QString& folder_path) const;
  Q_INVOKABLE bool importPresetSyncBundle(const QString& folder_path, bool overwrite = true);
  Q_INVOKABLE QVariantList curvePresetPreviewPoints(const QString& name) const;
  Q_INVOKABLE bool applyCurvePreset(const QString& name);
  Q_INVOKABLE bool saveCameraDefault();
  Q_INVOKABLE bool clearCameraDefault();
  Q_INVOKABLE bool addAdjustmentLayer(const QString& name = {});
  Q_INVOKABLE bool createAdjustmentLayerFromCurrentGlobalEdits(const QString& name = {});
  Q_INVOKABLE bool bakePhotoLayerAdjustmentsToGlobal(int index);
  Q_INVOKABLE int ensureActiveSelectionAdjustmentLayer();
  Q_INVOKABLE bool addFillLayer(const QString& color = {}, const QString& name = {});
  Q_INVOKABLE bool addPhotoShape(const QString& type, int start_x, int start_y,
      int end_x, int end_y, int canvas_width, int canvas_height,
      const QString& fill_color, const QString& stroke_color, double stroke_width);
  Q_INVOKABLE bool setPhotoLayerShapeStyle(int index, const QString& fill_color,
      const QString& stroke_color, double stroke_width);
  Q_INVOKABLE bool setPhotoLayerShapeFill(int index, const QVariantMap& options);
  Q_INVOKABLE bool setPhotoLayerShapeTexture(int index, const QString& file_path);
  Q_INVOKABLE bool addTextLayer(const QString& text = {}, const QString& name = {});
  Q_INVOKABLE bool addParagraphTextLayer(const QString& text, int x, int y,
      int width, int height, int canvas_width, int canvas_height);
  bool photoPaintBusy() const { return !photo_paint_jobs_.empty(); }
  QString photoPaintError() const { return photo_paint_error_; }
  Q_INVOKABLE bool addPointTextLayer(const QString& text, int x, int y, int canvas_width, int canvas_height);
  Q_INVOKABLE bool copyPixelsFromSelection(int source_layer_index = -1, bool independent = false);
  Q_INVOKABLE bool erasePixelsFromSelection(int layer_index);
  Q_INVOKABLE QVariantMap photoCompositionCommand(const QVariantMap& request);
  // Native task ownership hooks. These are not generic QObject invocation tools.
  QVariantMap beginAgentTask(const QString& task_id);
  void setAgentTaskJournal(std::shared_ptr<AgentTaskJournal> journal) { agent_task_journal_ = std::move(journal); }
  using AgentRecipePersistenceHandler = std::function<QVariantMap(const QString&, const QVariantMap&)>;
  void setAgentRecipePersistenceHandler(AgentRecipePersistenceHandler handler) {
    agent_recipe_persistence_handler_ = std::move(handler);
  }
  AgentTaskJournal::ResourceAdapter agentTaskResourceAdapter(const QString& task_id);
  QVariantMap recoverAgentTask(const QString& task_id);
  QVariantMap runAgentTaskComposition(const QString& task_id, const QVariantMap& request);
  QVariantMap runAgentTaskNativeCommand(const QString& task_id, const QString& command_id,
      const QVariantMap& values, const std::function<QVariantMap()>& invoke);
  QVariantMap finishAgentTask(const QString& task_id, const QString& status, const QString& authored_recipe = {});
  QVariantMap checkpointAgentTaskRecipe(const QString& task_id, const QString& authored_recipe);
  QVariantMap agentTaskState() const;
  QVariantMap revertAgentTask(const QString& task_id, bool check_only = false);
  void cancelAgentTaskWork(const QString& task_id);
  Q_INVOKABLE bool addPaintLayer();
  [[nodiscard]] QStringList photoFragmentBrushPresetNames() const;
  Q_INVOKABLE QVariantMap photoFragmentBrushPreset(const QString& name) const;
  Q_INVOKABLE bool savePhotoFragmentBrushPreset(const QString& name, const QString& tip_layer_id,
      const QString& source_preset_name, double rotation_variation, double scale_variation,
      bool overwrite = true);
  Q_INVOKABLE bool deletePhotoFragmentBrushPreset(const QString& name);
  Q_INVOKABLE bool importPhotoFragmentBrushPreset(const QString& path, bool overwrite = false);
  Q_INVOKABLE bool exportPhotoFragmentBrushPreset(const QString& name, const QString& path);
  Q_INVOKABLE bool beginPhotoPaintStroke(int index, const QVariantMap& settings);
  Q_INVOKABLE void addPhotoPaintPoint(double x, double y, double pressure = 1.0);
  Q_INVOKABLE bool finishPhotoPaintStroke(bool commit = true);
  Q_INVOKABLE void cancelPhotoPaint();
  Q_INVOKABLE bool addSourceLayer(const QString& file_path, const QString& name = {});
  Q_INVOKABLE bool relinkSourceLayer(const QString& layer_id, const QString& file_path);
  Q_INVOKABLE bool addLayerGroup(const QString& name = {});
  Q_INVOKABLE bool groupPhotoLayers(const QVariantList& indices, const QString& name = {});
  Q_INVOKABLE bool movePhotoLayerIntoGroup(int index, int group_index);
  Q_INVOKABLE bool movePhotoLayerOutOfGroup(int index);
  Q_INVOKABLE bool ungroupPhotoLayer(int index);
  Q_INVOKABLE bool setPhotoLayerGroupExpanded(int index, bool expanded);
  Q_INVOKABLE bool setPhotoLayerEnabled(int index, bool enabled);
  Q_INVOKABLE bool setPhotoLayerAdjustmentsPreviewBypassed(int index, bool bypassed);
  Q_INVOKABLE bool setPhotoLayerLocked(int index, bool locked);
  Q_INVOKABLE bool setPhotoLayerSolo(int index, bool solo);
  Q_INVOKABLE bool setPhotoLayerOpacity(int index, double opacity);
  Q_INVOKABLE QVariantList photoLayerEffects(int index) const;
  Q_INVOKABLE bool setPhotoLayerGaussianBlur(int index, const QString& effect_id,
      double sigma, double amount, bool enabled);
  Q_INVOKABLE bool removePhotoLayerEffect(int index, const QString& effect_id);
  Q_INVOKABLE bool movePhotoLayerEffect(int index, const QString& effect_id, int destination);
  Q_INVOKABLE bool duplicatePhotoLayerEffect(int index, const QString& effect_id);
  Q_INVOKABLE bool setPhotoLayerEffectEdgeMode(int index, const QString& effect_id, int mode);
  Q_INVOKABLE bool setPhotoLayerEffectEnabled(int index, const QString& effect_id, bool enabled);
  Q_INVOKABLE bool maskPhotoLayerEffectFromSelection(int index, const QString& effect_id);
  Q_INVOKABLE bool clearPhotoLayerEffectMask(int index, const QString& effect_id);
  Q_INVOKABLE bool selectPhotoLayerEffectMask(int index, const QString& effect_id);
  Q_INVOKABLE bool addPhotoLayerInvert(int index);
  Q_INVOKABLE bool setPhotoLayerPosterize(int index, const QString& effect_id, int levels, double amount, bool enabled);
  Q_INVOKABLE bool setPhotoLayerBoxBlur(int index, const QString& effect_id, int radius, double amount, bool enabled);
  Q_INVOKABLE bool setPhotoLayerMotionBlur(int index, const QString& effect_id, double distance, double angle, double amount, bool enabled);
  Q_INVOKABLE bool setPhotoLayerDraggedShutter(int index, const QString& effect_id,
      double distance, double angle, double falloff, double highlight_weighting,
      bool protect_subject, double amount, bool enabled);
  Q_INVOKABLE bool setPhotoLayerUnsharpMask(int index, const QString& effect_id, double sigma, double strength, double threshold, double amount, bool enabled);
  Q_INVOKABLE bool setPhotoLayerOpticalDiffusion(int index, const QString& effect_id,
      double radius, double threshold, double knee, double strength, double warmth, double highlight_protection,
      double amount, bool enabled);
  Q_INVOKABLE bool setPhotoLayerImperfectPrint(int index, const QString& effect_id,
      double paper_scale, double paper_texture, double ink_spread, double ink_density,
      double red_offset_x, double red_offset_y, double blue_offset_x, double blue_offset_y,
      double seed, double amount, bool enabled);
  Q_INVOKABLE bool setPhotoLayerEffectParameters(int index, const QString& type,
      const QString& effect_id, const QVariantMap& parameters, double amount, bool enabled);
  Q_INVOKABLE QVariantList photoEffectParameterFields(const QString& type) const;
  Q_INVOKABLE bool setPhotoLayerDisplacementMap(int index, const QString& effect_id, const QString& path);
  Q_INVOKABLE QVariantList availablePhotoEffects() const;
  Q_INVOKABLE QVariantList availablePhotoEffectLooks() const;
  Q_INVOKABLE bool applyPhotoLayerEffectLook(int index, const QString& look_id);
  Q_INVOKABLE QStringList availablePhotoTextFonts() const;
  Q_INVOKABLE QStringList availablePhotoTextFaces(const QString& family) const;
  Q_INVOKABLE QVariantList availablePhotoTextAxes(const QString& family, const QString& face) const;
  Q_INVOKABLE QString photoTextFontWarning(const QString& family) const;
  Q_INVOKABLE bool setPhotoLayerEffectAmount(int index, const QString& effect_id, double amount);
  Q_INVOKABLE bool resetPhotoLayerEffect(int index, const QString& effect_id);
  Q_INVOKABLE bool setPhotoLayerBlendMode(int index, const QString& blend_mode);
  Q_INVOKABLE bool setPhotoLayerAdjustmentValue(int index, const QString& adjustment, double value);
  Q_INVOKABLE bool setPhotoLayerAdjustmentString(
      int index,
      const QString& adjustment,
      const QString& value);
  Q_INVOKABLE bool setPhotoLayerAdjustmentBool(int index, const QString& adjustment, bool value);
  Q_INVOKABLE bool setPhotoLayerCurvePoints(
      int index,
      const QString& channel,
      const QVariantList& points);
  Q_INVOKABLE bool setPhotoLayerLutStack(int index, const QVariantList& lut_stack);
  Q_INVOKABLE bool resetPhotoLayerAdjustmentGroup(int index, const QString& group);
  Q_INVOKABLE bool resetPhotoLayerAdjustments(int index);
  Q_INVOKABLE bool copyPhotoLayerAdjustments(int index);
  Q_INVOKABLE bool pastePhotoLayerAdjustments(int index);
  Q_INVOKABLE bool pasteGlobalAdjustmentsToPhotoLayer(int index);
  Q_INVOKABLE bool syncPhotoLayerAdjustments(int source_index, const QVariantList& target_indices);
  Q_INVOKABLE bool setPhotoLayerClippingMask(int index, bool clipping_mask);
  Q_INVOKABLE bool setPhotoLayerMaskEnabled(int index, bool enabled);
  Q_INVOKABLE bool linkActiveSelectionToPhotoLayer(int index, bool inverted = false);
  Q_INVOKABLE bool setPhotoLayerMaskOpacity(int index, double opacity);
  Q_INVOKABLE bool setPhotoLayerMaskDensity(int index, double density);
  Q_INVOKABLE bool setPhotoLayerText(int index, const QString& text);
  Q_INVOKABLE QVariantMap photoLayerTypography(int index) const;
  Q_INVOKABLE QVariantMap photoLayerTextLayoutInfo(int index) const;
  Q_INVOKABLE bool setPhotoLayerTypography(int index, const QVariantMap& changes);
  Q_INVOKABLE bool setPhotoLayersTypography(const QVariantList& indices, const QVariantMap& changes);
  Q_INVOKABLE bool setPhotoLayerTextPathFromActiveSelection(int index, int contour_index = 0);
  Q_INVOKABLE bool clearPhotoLayerTextPath(int index);
  Q_INVOKABLE bool setPhotoLayerTextStyle(
      int index,
      const QString& font_family,
      double font_size,
      const QString& color,
      const QString& font_weight,
      const QString& alignment);
  Q_INVOKABLE bool setPhotoLayerTextBox(int index, int pixel_width, int pixel_height);
  Q_INVOKABLE bool resizePhotoLayerTextBox(int index, int x, int y, int pixel_width, int pixel_height);
  Q_INVOKABLE bool isolatePhotoLayer(int index);
  Q_INVOKABLE bool clearPhotoLayerSolo();
  Q_INVOKABLE bool savePhotoLayerVisibilityState(const QString& name = {});
  Q_INVOKABLE bool applyPhotoLayerVisibilityState(int index);
  Q_INVOKABLE bool deletePhotoLayerVisibilityState(int index);
  Q_INVOKABLE bool renamePhotoLayer(int index, const QString& name);
  Q_INVOKABLE bool movePhotoLayersByPixels(const QVariantList& indices, int delta_x, int delta_y);
  Q_INVOKABLE bool setPhotoLayerTransform(
      int index,
      int x,
      int y,
      double scale,
      double rotation);
  Q_INVOKABLE bool setPhotoLayerPerspective(
      int index,
      double horizontal,
      double vertical);
  Q_INVOKABLE QVariantMap photoLayerBounds(int index) const;
  Q_INVOKABLE bool snapPhotoLayerToCanvas(
      int index,
      const QString& anchor,
      int canvas_width = 0,
      int canvas_height = 0);
  Q_INVOKABLE bool alignPhotoLayers(
      const QVariantList& indices,
      const QString& alignment,
      int canvas_width = 0,
      int canvas_height = 0);
  Q_INVOKABLE bool distributePhotoLayers(const QVariantList& indices, const QString& axis);
  Q_INVOKABLE bool exportPhotoLayer(int index, const QString& file_path) const;
  Q_INVOKABLE bool exportFlattenedPhoto(const QString& file_path) const;
  Q_INVOKABLE bool startPhotoLayerExport(int index, const QString& file_path);
  Q_INVOKABLE bool startFlattenedPhotoExport(const QString& file_path);
  Q_INVOKABLE bool cancelPhotoExport();
  bool photoExportBusy() const { return photo_export_busy_; }
  QString photoExportError() const { return photo_export_error_; }
  QString photoExportPath() const { return photo_export_path_; }
  Q_INVOKABLE QStringList cropAspectPresets() const;
  Q_INVOKABLE QStringList customCropAspectPresets() const;
  Q_INVOKABLE bool addCustomCropAspectPreset(const QString& preset);
  Q_INVOKABLE bool removeCustomCropAspectPreset(const QString& preset);
  Q_INVOKABLE bool exportCustomCropAspectPresets(const QString& file_path) const;
  Q_INVOKABLE int importCustomCropAspectPresets(const QString& file_path, bool replace = false);
  Q_INVOKABLE bool setReferenceImage(const QString& file_path);
  Q_INVOKABLE void clearReferenceImage();
  Q_INVOKABLE bool duplicatePhotoLayer(int index);
  Q_INVOKABLE bool duplicatePhotoLayerWithMasks(int index);
  Q_INVOKABLE bool duplicatePhotoLayerWithoutMasks(int index);
  Q_INVOKABLE bool movePhotoLayer(int index, int offset);
  Q_INVOKABLE bool deletePhotoLayer(int index);
  Q_INVOKABLE void addMask(const QString& kind);
  Q_INVOKABLE int deleteEmptyMasks();
  Q_INVOKABLE int updateStaleGeneratedMasks();
  Q_INVOKABLE void addAIMask(const QString& semantic_kind);
  Q_INVOKABLE bool addAlphaImageMask(const QString& file_path);
  Q_INVOKABLE bool beginAlphaImageMaskImport(const QString& file_path);
  Q_INVOKABLE bool commitAlphaImageMaskImport();
  Q_INVOKABLE bool cancelAlphaImageMaskImport();
  Q_INVOKABLE void addSubjectMask();
  Q_INVOKABLE void addObjectMaskAt(double normalized_x, double normalized_y);
  Q_INVOKABLE QString pickMaskAt(
      double normalized_x,
      double normalized_y,
      const QString& intent = QStringLiteral("selection"),
      bool create_new = false);
  Q_INVOKABLE QString pickMaskPath(
      const QVariantList& points,
      const QString& intent = QStringLiteral("selection"),
      bool create_new = false);
  Q_INVOKABLE bool beginSmartSelectStroke(
      double normalized_x,
      double normalized_y,
      double pixel_width = 1.0,
      double pixel_height = 1.0);
  Q_INVOKABLE bool updateSmartSelectStroke(
      double normalized_x,
      double normalized_y,
      double pixel_width = 1.0,
      double pixel_height = 1.0);
  Q_INVOKABLE QString commitSmartSelectStroke(
      const QString& intent = QStringLiteral("selection"),
      bool create_new = false);
  Q_INVOKABLE void resetSmartSelectStroke();
  Q_INVOKABLE QVariantList smartSelectStrokePromptPayload() const;
  Q_INVOKABLE void updateSmartSelectPreview(double normalized_x, double normalized_y);
  Q_INVOKABLE void updateSmartSelectPreviewPath(const QVariantList& points);
  Q_INVOKABLE void scheduleSmartSelectPreview(double normalized_x, double normalized_y);
  Q_INVOKABLE void scheduleSmartSelectPreviewPath(const QVariantList& points);
  Q_INVOKABLE void clearSmartSelectPreview();
  Q_INVOKABLE void cycleSmartSelectCandidate(int delta = 1);
  Q_INVOKABLE QString acceptSmartSelectPreview(
      const QString& intent = QStringLiteral("selection"),
      bool create_new = false);
  Q_INVOKABLE void rejectSmartSelectPreview();
  Q_INVOKABLE bool refineSmartSelectPreviewWithPrompt(
      double normalized_x,
      double normalized_y,
      bool negative = false);
  Q_INVOKABLE bool refineActiveMaskWithSmartSelect();
  Q_INVOKABLE void deselectActiveMask();
  Q_INVOKABLE void smartSelectAddAt(double normalized_x, double normalized_y, bool create_new = false);
  Q_INVOKABLE void smartSelectSubtractAt(double normalized_x, double normalized_y, bool create_new = false);
  Q_INVOKABLE void smartSelectObjectAt(double normalized_x, double normalized_y, bool create_new = false);
  Q_INVOKABLE void objectRemoveAt(double normalized_x, double normalized_y, bool create_new = false);
  Q_INVOKABLE void startPolygonSelection(bool subtract = false);
  Q_INVOKABLE void startLassoSelection(bool subtract = false);
  Q_INVOKABLE void appendActivePolygonPoint(double normalized_x, double normalized_y);
  Q_INVOKABLE bool moveActivePolygonPoint(int point_index, double normalized_x, double normalized_y);
  Q_INVOKABLE bool insertActivePolygonPointAfter(int point_index, double normalized_x, double normalized_y);
  Q_INVOKABLE void removeLastActivePolygonPoint();
  Q_INVOKABLE void clearActivePolygonPoints();
  Q_INVOKABLE bool simplifyActivePolygon(double tolerance = 0.012);
  Q_INVOKABLE bool smoothActivePolygon();
  Q_INVOKABLE bool closeActiveLassoPath(double tolerance = -1.0);
  Q_INVOKABLE void startSelectionBrush(bool subtract = false);
  Q_INVOKABLE void startAIMaskRefineBrush(bool subtract = false);
  Q_INVOKABLE void startSelectionEdgeBrush(bool subtract = false);
  Q_INVOKABLE void startHealBrush();
  Q_INVOKABLE void startPatchBrush();
  Q_INVOKABLE void removeDustSpots();
  Q_INVOKABLE void saveActiveDustMap();
  Q_INVOKABLE void applySensorDustMap();
  Q_INVOKABLE void removeRedEye();
  Q_INVOKABLE void cleanupSkin();
  Q_INVOKABLE void smoothSkinFrequency();
  Q_INVOKABLE void startCloneBrush();
  Q_INVOKABLE void startRemoveBrush();
  Q_INVOKABLE void startRemoveRefineBrush(bool subtract = false);
  Q_INVOKABLE void applyActiveRetouch();
  Q_INVOKABLE void cancelActiveRetouch();
  Q_INVOKABLE bool resetActiveRetouchSelection();
  Q_INVOKABLE bool deleteActiveRetouchPatch();
  Q_INVOKABLE bool reopenActiveRetouchSelection();
  Q_INVOKABLE void regenerateInpaintSeed();
  Q_INVOKABLE bool regenerateActiveInpaintPatch();
  Q_INVOKABLE bool queueActiveFinalInpaintPatch();
  Q_INVOKABLE bool retryLastMaskAction();
  Q_INVOKABLE QVariantList reusableMaskOutputs() const;
  Q_INVOKABLE void duplicateActiveMask();
  Q_INVOKABLE void duplicateActiveMaskInverted();
  Q_INVOKABLE bool duplicateActiveMaskSet();
  Q_INVOKABLE bool setMaskSetEnabled(const QString& set_id, bool enabled);
  Q_INVOKABLE bool setMaskSetPreviewBypassed(const QString& set_id, bool bypassed);
  Q_INVOKABLE bool setMaskSetSolo(const QString& set_id, bool solo);
  Q_INVOKABLE bool setMaskSourceEnabled(int index, bool enabled);
  Q_INVOKABLE bool setMaskSourceSolo(int index, bool solo);
  Q_INVOKABLE bool moveMaskSource(int index, const QString& target_set_id, int target_source_index);
  Q_INVOKABLE bool moveMaskSetInPhotoLayer(int layer_index, const QString& set_id, int target_mask_index);
  Q_INVOKABLE void removeActiveMask();
  Q_INVOKABLE void moveActiveMask(int offset);
  Q_INVOKABLE void beginActiveBrushStroke();
  Q_INVOKABLE void endActiveBrushStroke();
  Q_INVOKABLE void addBrushPoint(double normalized_x, double normalized_y);
  Q_INVOKABLE void addBrushPointWithPressure(double normalized_x, double normalized_y, double pressure);
  Q_INVOKABLE void addBrushPointWithTablet(
      double normalized_x,
      double normalized_y,
      double pressure,
      double tilt_x,
      double tilt_y);
  Q_INVOKABLE void addBrushPointWithTabletEraser(
      double normalized_x,
      double normalized_y,
      double pressure,
      double tilt_x,
      double tilt_y,
      bool eraser);
  Q_INVOKABLE void clearActiveBrushPoints();
  Q_INVOKABLE void setActiveMaskCloneSource(double normalized_x, double normalized_y);
  Q_INVOKABLE void setWhiteBalanceSampling(bool enabled);
  Q_INVOKABLE void sampleWhiteBalanceAt(double normalized_x, double normalized_y);
  Q_INVOKABLE void setColorToolSamplingTarget(const QString& target);
  Q_INVOKABLE void sampleColorToolAt(double normalized_x, double normalized_y);
  Q_INVOKABLE bool selectColorMixerChannelAt(double normalized_x, double normalized_y);
  Q_INVOKABLE void cancelColorMixerSample();
  Q_INVOKABLE bool autoDetectSkinProtection();
  Q_INVOKABLE void updatePixelInspector(double normalized_x, double normalized_y);
  Q_INVOKABLE void clearPixelInspector();
  Q_INVOKABLE void setScopesVisible(bool visible);
  Q_INVOKABLE void setScopeDomain(const QString& domain);
  Q_INVOKABLE void useAsShotRawWhiteBalance();

signals:
  void sourceSamplingBusyChanged();
  void colorMixerSampleCompleted(bool success);
  void documentEditCommitted();
  void polygonPathChanged();
  void photoExportChanged();
  void photoPaintStateChanged();
  void photoExportFinished(bool success, bool cancelled, const QString& path, const QString& error);
  void previewChanged();
  void previewViewportChanged();
  void displayPreviewNativeViewportFullResolutionChanged();
  void diagnosticOverlaysChanged();
  void histogramChanged();
  void previewTargetEdgeChanged();
  void statusChanged();
  void recipeSaveStateChanged();
  void assetChanged();
  void processingChanged();
  void controlsChanged();
  void maskStateChanged();
  void activeBrushStrokeChanged();
  void brushStrokeActiveChanged();
  void maskOverlayChanged();
  void compareModeChanged();
  void compareSnapshotChanged();
  void sectionComparisonChanged();
  void compareAdjustmentGroupsChanged();
  void referenceImageChanged();
  void whiteBalanceSamplingChanged();
  void colorToolSamplingTargetChanged();
  void pixelInspectorChanged();
  void histogramActivityChanged();
  void historyChanged();
  void clipboardChanged();
  void photoLayerClipboardOperationFinished(const QString& operation, int count, const QString& error);
  void presetsChanged();
  void presetIntensityChanged();
  void cameraDefaultsChanged();
  void smartSelectPreviewChanged();
  void smartSelectStrokeChanged();
  void systemMonitorProfileChanged();

 private:
  CustomerEditAccess customer_edit_access_;

  friend struct EditorPreviewControllerTestAccess;
  friend struct MaskInteractionPerformanceTestAccess;
  bool photo_layer_clipboard_busy_ = false;
  quint64 photo_layer_clipboard_epoch_ = 0;
  friend struct EditorHistoryStorageTestAccess;
  bool startPhotoExport(const QString& path, std::function<QImage()> producer);
  bool photo_export_busy_ = false;
  QString photo_export_error_;
  QString photo_export_path_;
  std::shared_ptr<PhotoExportControl> photo_export_control_;
  LinkedImageSourceMonitor* linked_source_monitor_ = nullptr;
  quint64 layer_source_content_revision_ = 0;

  struct PendingBrushCommit {
    int mask_index = -1;
    QString mask_id;
    QVariantMap mask;
    QVector<BrushPreviewPoint> live_points;
    QVector<QVector<BrushPreviewPoint>> live_strokes;
    EditState undo_state;
    bool undo_state_valid = false;
    QString undo_label;
    double brush_spacing = 0.75;
    double image_aspect = 0.0;
    double feather = 0.0;
    double hardness = 0.25;
    double auto_mask_tolerance = 0.18;
    bool auto_mask = false;
    bool protect_edges = false;
    qint64 live_preview_elapsed_ms = 0;
    bool live_preview_within_latency_budget = true;
  };

  struct NativePreviewCommitContext {
    int generation = 0;
    bool full_resolution = false;
    bool overlay_only = false;
    bool success = false;
    bool cancelled = false;
    bool accepted_transaction_feedback = false;
    quint64 edit_transaction_generation = 0;
    quint64 layer_source_content_revision = 0;
    QString source_asset_id;
    QString source_file_path;
    int mask_work_revision = 0;
  };

  void renderPreview(bool schedule_full_resolution = true, const QString& reason = {});
  void submitEditIntent(const EditIntent& intent);
  void submitFullPipelineEditIntent(
      const QString& operation,
      bool schedule_full_resolution = true,
      const QString& render_reason = {});
  void submitGlobalToneColorEditIntent(const QString& operation);
  void submitRawEditIntent(const QString& operation);
  void submitInputColorTransformEditIntent(const QString& operation);
  void submitDisplayTransformEditIntent(const QString& operation);
  void refreshSystemMonitorProfile();
  void submitLayerCompositeEditIntent(
      const QString& operation,
      bool schedule_full_resolution = true,
      const QString& render_reason = {});
  void schedulePhotoLayerAdjustmentRender();
  void scheduleEditTransactionInteractivePreview(const QString& reason);
  void flushEditTransactionInteractivePreview();
  void submitMaskRasterizationEditIntent(
      const QString& operation,
      bool schedule_full_resolution = true,
      const QString& render_reason = {},
      const QRectF& normalized_region = {});
  static QString maskTileCacheKeyForTesting(
      const QVariantMap& mask,
      const QString& source_revision,
      const QRectF& crop_rect,
      const QSize& raster_size,
      const QRect& tile_rect);
  static void clearPreviewWorkingCachesForTesting();
  static void clearRenderedPreviewImageCacheForTesting();
  static void clearMaskTileCacheForTesting();
  static bool insertMaskTileForTesting(
      const QString& key,
      const QByteArray& matte_data,
      const QSize& tile_size);
  static bool findMaskTileForTesting(
      const QString& key,
      QByteArray* matte_data,
      QSize* tile_size);
  static int maskTileCacheEntryCountForTesting();
  static qint64 maskTileCacheBytesForTesting();
  static QString adjustedWorkingCacheKeyForTesting(const RenderRequest& request);
  static QString renderedPreviewCacheKeyForTesting(const RenderRequest& request);
  static void scheduleColorTransformPrewarmForTesting(
      EditState state,
      const QString& file_path);
  static bool cancelledPreviewEncodeLeavesNoArtifactForTesting(
      const QImage& image,
      const QString& output_path);
  void resolveDirtyGraphForIntent(const EditIntent& intent);
  void resolveDirtyGraphForRenderReason(const QString& reason);
  void performRenderPreview(
      bool schedule_full_resolution,
      int coalesced_request_count,
      const QString& reason);
  void beginEditTransaction();
  void endEditTransaction();
  void renderPreviewStage(bool full_resolution);
  void handleRenderFinished(bool full_resolution);
  void handleOverlayRenderFinished();
  void handleRenderResult(bool full_resolution, const RenderResult& result);
  [[nodiscard]] bool nativePreviewCommitAllowed(
      const NativePreviewCommitContext& context) const;
  void publishOverlayRenderResult(const RenderResult& result);
  void recordVisibleOverlayQueueWait(const RenderRequest& request);
  void startOverlayRenderStage(const RenderRequest& request);
  void startRenderStage(bool full_resolution, const RenderRequest& request);
  void dispatchPendingRenderRequests();
  void updateProcessingState();
  [[nodiscard]] RenderRequest currentRenderRequest(bool full_resolution) const;
  void setStatus(const QString& status);
  void scheduleControlsChanged();
  void setSelectionFailureReason(const QString& reason);
  void recordMaskWorkTelemetry(QVariantMap fields);
  void recordCancelledMaskWork(
      const QString& operation,
      int job_generation,
      int latest_job_generation,
      int state_revision,
      const QString& reason,
      const QString& stage,
      const QVariantMap& mask = {},
      const QString& source_revision = {},
      const QSize& raster_size = {},
      const AsyncMaskJob& job = {});
  void setInpaintProgress(bool active, double value, const QString& label);
  void setProcessing(bool processing);
  void rememberSmartSelectionRetry(
      const QVariantList& points,
      const QString& intent,
      bool create_new,
      const QString& detail);
  void rememberInpaintRetry(const QString& retouch_mode, const QString& detail);
  void clearMaskActionRetry();
  bool retryInpaintForMode(const QString& retouch_mode);
  QVariantMap generatedMaskMetadata(const QString& kind, const QString& runtime_label = {}) const;
  void stampGeneratedMaskMetadata(QVariantMap* mask, const QString& runtime_label = {}) const;
  void applyGeneratedMaskCleanup(QVariantMap* mask) const;
  bool generatedMaskIsStale(const QVariantMap& mask) const;
  [[nodiscard]] QString cachePathForSelection(bool edited, bool full_resolution) const;
  void pushUndoState(const EditState& state, const QString& label = {});
  [[nodiscard]] QVector<HistoryRevision>& undoStackForAsset(const QString& asset_id);
  [[nodiscard]] QVector<HistoryRevision>& redoStackForAsset(const QString& asset_id);
  [[nodiscard]] QVector<QString>& undoLabelStackForAsset(const QString& asset_id);
  [[nodiscard]] QVector<QString>& redoLabelStackForAsset(const QString& asset_id);
  void rememberStateForUndo(const QString& label = {});
  void clearHistoryForAsset(const QString& asset_id);
  void applyHistoryState(const EditState& state);
  void loadEditHistorySnapshots();
  void saveEditHistorySnapshots() const;
  [[nodiscard]] QString editHistorySnapshotsFilePath() const;
  void persistCurrentState();
  bool ensureRecipeSaveJournal();
  bool checkpointRecipeSaves();
  void restoreStateFromAdjustmentPayload(
      const QString& asset_id,
      const QString& adjustment_payload_json);
  void refreshRawAsShotWhiteBalanceMetadata();
  void setRawAsShotWhiteBalance(double temperature, double tint);
  void applyState(const EditState& state);
  [[nodiscard]] EditState currentState() const;
  [[nodiscard]] EditState stateFromAdjustmentPayload(
      const QString& adjustment_payload_json) const;
  [[nodiscard]] EditState presetPreviewState(const QString& name, const QStringList& groups, double intensity) const;
  [[nodiscard]] QString normalizedPresetName(const QString& name) const;
  [[nodiscard]] QString normalizedCameraDefaultKey(const QString& camera_label) const;
  [[nodiscard]] QString presetsFilePath() const;
  [[nodiscard]] QString cameraDefaultsFilePath() const;
  [[nodiscard]] QString customCropPresetsFilePath() const;
  [[nodiscard]] int firstMaskIndexForKind(const QString& kind) const;
  [[nodiscard]] QVariantMap activeMaskDefinition() const;
  [[nodiscard]] QVariantMap maskDefinitionAt(int index) const;
  void invalidateActiveMaskCache();
  void invalidatePhotoLayerThumbnailCache() const;
  void invalidateMaskDerivedCaches();
  [[nodiscard]] bool thumbnailRefreshDeferred() const;
  [[nodiscard]] QStringList thumbnailRefreshDeferBlockers() const;
  [[nodiscard]] QVariantMap thumbnailRefreshQueueFields() const;
  [[nodiscard]] ThumbnailRefreshQueueCompletion thumbnailRefreshQueueCompletion() const;
  [[nodiscard]] ThumbnailPresentationFacts thumbnailPresentationFacts() const;
  [[nodiscard]] ThumbnailPresentationDependencies thumbnailPresentationDependencies() const;
  [[nodiscard]] bool consumeThumbnailStaleWarningLog(const QString& reason) const;
  [[nodiscard]] bool thumbnailEntryCacheLookup(const QString& key, QVariantMap* entry) const;
  void storeThumbnailEntryCache(const QString& key, const QVariantMap& entry) const;
  void clearThumbnailEntryCache() const;
  void clearActiveMaskPreviewCache() const;
  [[nodiscard]] bool activeMaskPreviewCacheLookup(const QString& key, QString* source) const;
  void storeActiveMaskPreviewCacheEntry(const QString& key, const QString& source) const;
  void synchronizeMaskState();
  bool applyMaskStackChange(
      const QVariantList& stack,
      int active_index,
      bool rerender = true,
      const QString& operation = {},
      bool ensure_selection_adjustment_layer = false);
  bool applyPhotoLayerStackChange(
      const QVariantList& layers,
      const QString& operation = {},
      bool debounce_render = false,
      const QString& undo_label = {},
      bool record_undo = true);
  bool applyPhotoLayerMetadataChange(
      const QVariantList& layers,
      const QStringList& changed_keys,
      const QString& undo_label);
  bool duplicate_photo_layer(int index, bool preserve_masks);
  bool updateMaskAt(
      int index,
      const QVariantMap& mask,
      bool rerender = true,
      bool allow_locked = false,
      const QString& operation = {});
  bool updateActiveMaskDefinition(
      const QVariantMap& mask,
      bool rerender = true,
      bool allow_locked = false,
      const QString& operation = {});
  bool updateLegacyKindMask(const QString& kind, const QVariantMap& updates);
  void setActiveMaskRefineField(const QString& key, double value, double minimum, double maximum);
  void setActiveMaskRefineMinMaxField(
      const QString& key,
      const QString& paired_key,
      double value,
      bool is_minimum);
  void loadPresets();
  bool savePresets(QString* error = nullptr) const;
  void loadCameraDefaults();
  void saveCameraDefaults() const;
  void loadCustomCropPresets();
  void saveCustomCropPresets() const;
  void flushPersistedState();
  void reportRecipeSaveFailure(const QString& detail);
  [[nodiscard]] bool hasVisibleAdjustments() const;
  [[nodiscard]] bool decodeSourceImage(QImage* image, int max_edge) const;
  [[nodiscard]] QSize sourceImageSize() const;
  [[nodiscard]] QImage applyCrop(const QImage& image) const;
  void updateRawClippingWarnings(const QImage& image);
  void computeHistogram(const QImage& image);
  void startHistogramUpdate(const RenderResult& result);
  void startHistogramUpdate(
      const QImage& image,
      int generation,
      bool full_resolution,
      int raw_clip_sample_count,
      int raw_shadow_clip_count,
      int raw_highlight_clip_count,
      const QString& render_reason,
      const QString& interaction_mode);
  void publishImmediateHistogramPreview(
      const QImage& image,
      int generation,
      bool full_resolution,
      int raw_clip_sample_count,
      int raw_shadow_clip_count,
      int raw_highlight_clip_count,
      const QString& render_reason,
      const QString& interaction_mode);
  void handleHistogramFinished();
  void applyHistogramResult(const HistogramComputationResult& result);
  void resetSmartSelectPreview();
  bool appendSmartSelectStrokePoint(
      double normalized_x,
      double normalized_y,
      double pixel_width,
      double pixel_height,
      bool force);
  void startSmartSelectPreviewRequest();
  void handleSmartSelectPreviewFinished();
  void startSmartSelectCommitRequest();
  void handleSmartSelectCommitFinished();
  void publishSmartSelectCommitResult(const SmartSelectCommitResult& result);
  void updateSmartSelectPreviewState(
      const QVariantMap& preview_mask,
      const QVector<int>& instance_ids = {},
      const QVector<int>& candidate_instance_ids = {});
  void updateSmartSelectPreviewResult(
      const SegmentationResult& result,
      const QVector<int>& instance_ids = {});
  void clearSmartSelectPreviewState();
  void updateActiveBrushLivePreview(const QVector<BrushPreviewPoint>& sampled_points);
  bool setBrushStrokeActive(bool active);
  void dispatchBrushLivePreview();
  void clearActiveBrushLivePreview();
  void schedulePendingBrushCommit(PendingBrushCommit commit);
  void processNextPendingBrushCommit();
  void handleBrushRasterCommitFinished();
  void publishBrushRasterCommitResult(const BrushRasterCommitResult& result);
  void handleInpaintApplyFinished();
  void publishInpaintApplyResult(const InpaintApplyResult& result);
  void handleInpaintRegenerateFinished();
  void publishInpaintRegenerateResult(const InpaintRegenerateResult& result);
  void updateDiagnosticOverlayImages(
      const QImage& image,
      const QImage& proof_gamut_warning_mask = QImage());
  void scheduleDiagnosticOverlayUpdate(
      const QImage& image,
      int generation,
      const QImage& proof_gamut_warning_mask = QImage());
  void clearDiagnosticOverlayImages();
  struct DiagnosticOverlayResult final {
    std::array<QImage, 6> images;
    quint64 revision = 0;
    bool readback_failed = false;
  };
  void queueDiagnosticOverlayUpdate(
      const QImage& image, const QImage& proof_gamut_warning_mask,
      std::shared_ptr<const photo_editor::pipeline::metal::MetalOutputSurface> surface,
      bool deferred);
  void startDiagnosticOverlayUpdate();
  void publishDiagnosticOverlayImages(const DiagnosticOverlayResult& result);
  void cancelDiagnosticOverlayUpdate();
  [[nodiscard]] QSize activeBrushLivePreviewSize() const;
  bool regenerateActiveInpaintPatchWithQuality(
      const QString& requested_quality_mode,
      const QString& success_status_template);
  void resetSmartSelectInstanceMap();
  [[nodiscard]] bool ensureSmartSelectInstanceMap();
  [[nodiscard]] bool loadCachedSmartSelectInstanceMap();
  void completePendingSmartSelectCommit();
  [[nodiscard]] int smartSelectInstanceIdAt(double normalized_x, double normalized_y) const;
  [[nodiscard]] QVector<int> smartSelectCandidateInstanceIdsAt(
      double normalized_x,
      double normalized_y) const;
  [[nodiscard]] QVector<int> smartSelectCandidateInstanceIdsForPath(
      const QVariantList& points) const;
  [[nodiscard]] QVariantMap smartSelectPreviewMaskForInstances(
      const QVector<int>& instance_ids) const;
  QString pickObjectSelectionMask(
      const SegmentationResult& mask_result,
      const QString& operation,
      bool create_new,
      const QImage& source_image = {},
      const QString& runtime_label = {},
      const QVariantList& prompts = {},
      QVariantMap* published_mask = nullptr);
  QString pickObjectRemoveMask(
      const SegmentationResult& mask_result,
      bool create_new,
      const QImage& source_image = {},
      const QString& runtime_label = {},
      const QVariantList& prompts = {},
      QVariantMap* published_mask = nullptr);
  [[nodiscard]] int findPendingRetouchMaskIndex(
      const QString& retouch_mode,
      const QString& kind = QString()) const;
  [[nodiscard]] int findReusableRetouchBrushIndex(const QString& retouch_mode) const;
  [[nodiscard]] QVariantMap buildBrushMask(
      const QString& name,
      const QString& operation,
      const QString& tool_role,
      const QString& retouch_mode,
      bool committed,
      int ordinal) const;
  bool rebuildActiveBrushSegmentation(int max_edge);
  [[nodiscard]] bool channelMatchesSelection(const QColor& color) const;
  [[nodiscard]] QString withDisplayRevision(const QString& source) const;
  bool refreshAlphaMaskImportPreview();
  void completeRecipePreparation(const EditorRecipeSaveSnapshot& snapshot,
                                 const PreparedEditorRecipePersistence& prepared,
                                 qint64 preparation_elapsed_ms);
  bool recipe_preparation_active_ = false;
  bool requestSourceSample(double normalized_x, double normalized_y,
                           std::function<bool(const QImage&, int, int, int)> apply);
  quint64 source_sample_revision_ = 0;
  bool source_sample_active_ = false;
  std::shared_ptr<std::atomic_bool> source_sample_cancellation_;
  std::function<void()> pending_source_sample_;
  void cancelPendingAssetScopedWork();
  void scheduleThumbnailRefreshQueue(const QString& reason) const;
  void runQueuedThumbnailRefresh();
  void handleThumbnailRefreshQueueFinished();

  HistogramScopeSession histogram_scope_session_;
  void startNativePixelSample();
  void publishPixelInspector(const QVariantMap& after, double normalized_x, double normalized_y, bool pending = false);
  QVariantMap pixel_inspector_;
  bool pixel_inspector_requested_ = false;
  bool native_pixel_sample_active_ = false;
  bool native_pixel_sample_pending_ = false;
  quint64 pixel_inspector_sample_revision_ = 0;
  QPointF pixel_inspector_sample_position_;
  QFutureWatcher<QVariantMap> native_pixel_sample_watcher_;
  std::shared_ptr<const photo_editor::pipeline::metal::MetalOutputSurface> pixel_inspector_native_surface_;
  QVariantMap active_luminance_sample_preview_;
  QString alpha_mask_import_path_;
  QVariantMap alpha_mask_import_preview_;
  bool alpha_mask_import_invert_ = false;
  double alpha_mask_import_threshold_ = 0.0;
  QString alpha_mask_import_resample_mode_ = QStringLiteral("Original");
  QImage pixel_inspector_preview_image_;
  QImage latest_proof_gamut_warning_mask_;
  QImage pixel_inspector_reference_image_;
  HistogramScopeCoordinator histogram_scope_coordinator_;
  int histogram_scope_skipped_count_ = 0;

  QString asset_id_;
  QString file_path_;
  QString fallback_thumbnail_source_;
  QString preview_source_;
  QString display_preview_source_;
  QString display_preview_overlay_source_;
  QString focus_check_overlay_source_;
  QString focus_peaking_overlay_source_;
  QString clipping_overlay_source_;
  QString false_color_overlay_source_;
  QString gamut_overlay_source_;
  QString zone_map_overlay_source_;
  QString diagnostic_overlay_rendered_cache_key_;
  bool diagnostic_overlays_requested_ = false;
  bool diagnostic_overlay_active_ = false;
  bool diagnostic_overlay_pending_ = false;
  bool diagnostic_native_readback_failed_ = false;
  quint64 diagnostic_overlay_revision_ = 0;
  QImage diagnostic_overlay_pending_image_;
  QImage diagnostic_overlay_pending_gamut_mask_;
  std::shared_ptr<const photo_editor::pipeline::metal::MetalOutputSurface> diagnostic_overlay_pending_surface_;
  std::shared_ptr<std::atomic_bool> diagnostic_overlay_cancellation_;
  QFutureWatcher<DiagnosticOverlayResult> diagnostic_overlay_watcher_;
  QTimer diagnostic_overlay_timer_;
  bool color_tool_mask_preview_enabled_ = false;
  QFutureWatcher<ColorToolMaskPreviewResult> color_tool_mask_preview_watcher_;
  QThreadPool color_tool_mask_preview_pool_;
  QString color_tool_mask_preview_requested_key_;
  std::shared_ptr<std::atomic_bool> color_tool_mask_preview_cancellation_;
  std::optional<ColorToolMaskPreviewRequest> pending_color_tool_mask_preview_request_;
  bool color_tool_mask_preview_active_ = false;
  [[nodiscard]] QString colorToolMaskPreviewCacheKey(const EditState& state) const;
  void launchColorToolMaskPreview(ColorToolMaskPreviewRequest request);
  QString reference_preview_source_;
  QString display_reference_preview_source_;
  QString reference_image_path_;
  QString reference_image_name_;
  QRectF display_preview_overlay_rect_;
  QString status_ = QStringLiteral("No asset selected.");
  QString selection_failure_reason_;
  QString post_render_status_override_;
  QString preview_failure_stage_;
  QString preview_failure_detail_;
  QStringList preview_failure_layer_ids_;
  QStringList preview_failure_mask_set_ids_;
  QString last_successful_preview_file_path_;
  qint64 last_interactive_render_ms_ = 0;
  qint64 last_full_render_ms_ = 0;
  qint64 last_decode_render_ms_ = 0;
  qint64 last_edit_render_ms_ = 0;
  qint64 last_encode_render_ms_ = 0;
  QSize last_render_source_size_;
  mutable QString source_size_cache_path_;
  mutable qint64 source_size_cache_bytes_ = -1;
  mutable qint64 source_size_cache_modified_ms_ = -1;
  mutable QSize source_size_cache_;
  mutable qint64 source_size_cache_misses_ = 0;
  QSize last_render_output_size_;
  QSize last_full_render_output_size_;
  QString last_full_render_state_signature_;
  bool last_render_had_adjustments_ = false;
  QString camera_label_;
  QString capture_date_;
  QString lens_model_;
  int preview_target_edge_ = 0;
  qint64 last_render_request_msecs_ = 0;
  bool next_interactive_render_draft_ = false;
  PreviewPublisher preview_publisher_;
  QRectF preview_viewport_rect_;
  int preview_viewport_patch_max_edge_ = 0;
  bool preview_viewport_active_ = false;
  double mask_preview_overlay_opacity_ = 0.58;
  QString mask_preview_overlay_color_;
  bool mask_preview_show_all_ = false;
  bool mask_preview_temporarily_hidden_ = false;
  QString mask_refinement_preview_mode_ = QStringLiteral("refined");
  bool mask_refinement_high_quality_preview_ = false;
  bool mask_full_resolution_preview_ = false;
  QRectF pending_mask_dirty_region_;
  QString inpaint_prompt_;
  int inpaint_seed_ = 1;
  double inpaint_blend_amount_ = 1.0;
  QString inpaint_quality_mode_ = QStringLiteral("preview");
  bool retouch_before_after_enabled_ = false;
  bool inpaint_patch_diagnostics_enabled_ = false;
  bool processing_ = false;
  bool compare_mode_ = false;
  QString compare_source_mode_ = QStringLiteral("auto");
  QString compare_snapshot_id_;
  bool section_comparison_hold_active_ = false;
  QString section_comparison_previous_scope_;
  QString section_comparison_previous_layer_id_;
  QString section_comparison_scope_;
  std::optional<EditRecipeState> history_preview_state_;
  QString section_comparison_layer_id_;
  QStringList compare_adjustment_groups_;
  QStringList mask_compare_adjustment_groups_;
  bool crop_editing_ = false;
  QString input_color_profile_ = QStringLiteral("Embedded");
  QString working_color_profile_ = QStringLiteral("Linear ProPhoto RGB");
  QString preview_color_space_ = QStringLiteral("sRGB");
  QString preview_output_mode_ = QStringLiteral("Monitor");
  DcpProfileAssetState camera_dcp_profile_asset_;
  QString camera_profile_rendering_model_ = QStringLiteral("dng-profile-v2");
  QString photographic_rendering_model_ = QStringLiteral("aces2-ocio-v1"), renderer_transition_look_ = QStringLiteral("none");
  bool rendering_upgrade_preview_active_ = false, rendering_upgrade_previous_compare_mode_ = false;
  QString rendering_upgrade_source_model_;
  QVariantMap rendering_upgrade_evidence_;
  IccProfileAssetState input_icc_profile_asset_;
  IccProfileAssetState preview_icc_profile_asset_;
  IccProfileAssetState proof_icc_profile_asset_;
  IccProfileAssetState export_icc_profile_asset_;
  QString monitor_color_profile_;
  bool soft_proof_enabled_ = false;
  QString soft_proof_profile_ = QStringLiteral("sRGB");
  QString proof_render_intent_ = QStringLiteral("Relative Colorimetric");
  bool proof_simulate_paper_white_ = false;
  bool proof_simulate_ink_black_ = false;
  QString render_intent_ = QStringLiteral("Perceptual");
  bool raw_input_transform_enabled_ = true;
  QString highlight_reconstruction_mode_ = QStringLiteral("Enabled");
  QString highlight_reconstruction_model_ = QStringLiteral("camera-linear-opposed-v2");
  QString chromatic_adaptation_method_ = QStringLiteral("Bradford");
  QString output_color_space_ = QStringLiteral("sRGB");
  double output_headroom_ = 1.0;
  bool output_gamut_mapping_enabled_ = true;
  QString output_gamut_mapping_policy_ = QStringLiteral("Perceptual");
  bool black_point_compensation_enabled_ = true;
  double source_black_point_ = 0.0;
  double output_black_point_ = 0.0;
  double raw_exposure_ = 0.0;
  double raw_boost_ = 0.0;
  double raw_shadow_boost_ = 1.0;
  double raw_detail_ = 0.0;
  double raw_chroma_noise_reduction_ = 0.0;
  double raw_moire_reduction_ = 0.0;
  double raw_hot_dead_pixel_cleanup_ = 0.0;
  double raw_false_color_suppression_ = 0.0;
  bool raw_highlight_recovery_ = true;
  QString raw_demosaic_quality_ = QStringLiteral("Balanced");
  bool raw_gamut_mapping_ = false;
  bool raw_lens_correction_ = true;
  bool raw_white_balance_override_ = false;
  QString raw_camera_matching_look_ = QStringLiteral("Standard");
  double raw_white_balance_temperature_ = 5200.0;
  double raw_white_balance_tint_ = 0.0;
  double raw_as_shot_white_balance_temperature_ = 5200.0;
  double raw_as_shot_white_balance_tint_ = 0.0;
  double raw_black_level_ = 0.0;
  double raw_white_level_ = 1.0;
  double raw_calibration_hue_ = 0.0;
  double raw_calibration_saturation_ = 0.0;
  double raw_red_primary_hue_ = 0.0;
  double raw_red_primary_saturation_ = 0.0;
  double raw_green_primary_hue_ = 0.0;
  double raw_green_primary_saturation_ = 0.0;
  double raw_blue_primary_hue_ = 0.0;
  double raw_blue_primary_saturation_ = 0.0;
  double exposure_ = 0.0;
  double contrast_ = 0.0;
  double highlights_ = 0.0;
  double shadows_ = 0.0;
  double whites_ = 0.0;
  double blacks_ = 0.0;
  double brightness_ = 0.0;
  double hdr_local_tone_mapping_ = 0.0;
  double filmic_shoulder_ = 0.0;
  double filmic_toe_ = 0.0;
  double adaptive_contrast_ = 0.0;
  double levels_black_point_ = 0.0;
  double levels_gamma_ = 0.0;
  double levels_white_point_ = 0.0;
  double zone_deep_shadows_ = 0.0;
  double zone_shadows_ = 0.0;
  double zone_midtones_ = 0.0;
  double zone_lights_ = 0.0;
  double zone_highlights_ = 0.0;
  double temperature_ = 0.0;
  double tint_ = 0.0;
  double texture_ = 0.0;
  double clarity_ = 0.0;
  double dehaze_ = 0.0;
  double sharpening_ = 0.0;
  double sharpen_radius_ = 1.0;
  double sharpen_detail_ = 0.0;
  double sharpen_masking_ = 0.0;
  double luminance_nr_ = 0.0;
  double color_nr_ = 0.0;
  double lens_distortion_ = 0.0;
  double lens_vignette_ = 0.0;
  double chromatic_aberration_ = 0.0;
  double perspective_vertical_ = 0.0;
  double perspective_horizontal_ = 0.0;
  double transform_rotation_ = 0.0;
  double transform_scale_ = 1.0;
  double transform_skew_x_ = 0.0;
  double transform_skew_y_ = 0.0;
  double liquify_center_x_ = 0.5;
  double liquify_center_y_ = 0.5;
  double liquify_radius_ = 0.25;
  double liquify_strength_ = 0.0;
  bool content_aware_crop_fill_ = false;
  double vibrance_ = 0.0;
  double saturation_ = 0.0;
  double curve_shadows_ = 0.0;
  double curve_darks_ = 0.0;
  double curve_lights_ = 0.0;
  double curve_highlights_ = 0.0;
  QString curve_channel_ = QStringLiteral("RGB");
  QVariantList master_curve_points_;
  QVariantList luma_curve_points_;
  QVariantList red_curve_points_;
  QVariantList green_curve_points_;
  QVariantList blue_curve_points_;
  QString color_mixer_model_ = QStringLiteral("HSL");
  QString hsl_channel_ = QStringLiteral("Reds");
  double hue_adjustment_ = 0.0;
  double saturation_adjustment_ = 0.0;
  double luminance_adjustment_ = 0.0;
  struct MixerChannel {
    double hue = 0.0;
    double saturation = 0.0;
    double luminance = 0.0;
    double selective_cyan = 0.0;
    double selective_magenta = 0.0;
    double selective_yellow = 0.0;
    double selective_black = 0.0;
  };
  std::array<MixerChannel, 8> mixer_channels_{};
  EditRecipeState::RgbChannelMixer rgb_channel_mixer_{};
  bool black_and_white_enabled_ = false;
  std::array<double, 8> black_and_white_mixer_{};
  bool color_replacement_enabled_ = false;
  double color_replacement_source_hue_ = 0.0;
  double color_replacement_target_hue_ = 0.33;
  double color_replacement_range_ = 35.0;
  double color_replacement_softness_ = 100.0;
  double color_replacement_strength_ = 0.0;
  bool color_replacement_preserve_luminance_ = true;
  bool color_replacement_preserve_saturation_ = true;
  bool skin_tone_protection_enabled_ = false;
  double skin_tone_line_hue_ = 0.08;
  double skin_tone_protection_range_ = 30.0;
  double skin_tone_protection_strength_ = 0.0;
  bool color_harmony_enabled_ = false;
  QString color_harmony_mode_ = QStringLiteral("Complementary");
  double color_harmony_base_hue_ = 0.0;
  double color_harmony_strength_ = 0.0;
  double grading_shadows_hue_ = 0.0;
  double grading_shadows_saturation_ = 0.0;
  double grading_shadows_luminance_ = 0.0;
  double grading_midtones_hue_ = 0.0;
  double grading_midtones_saturation_ = 0.0;
  double grading_midtones_luminance_ = 0.0;
  double grading_highlights_hue_ = 0.0;
  double grading_highlights_saturation_ = 0.0;
  double grading_highlights_luminance_ = 0.0;
  double grading_amount_ = 100.0;
  double grading_balance_ = 0.0;
  double crop_rect_x_ = 0.0;
  double crop_rect_y_ = 0.0;
  double crop_rect_width_ = 0.0;
  double crop_rect_height_ = 0.0;
  double crop_zoom_ = 1.0;
  QString crop_preset_ = QStringLiteral("Original");
  QString upright_mode_ = QStringLiteral("Off");
  double upright_vertical_guide_angle_ = 0.0;
  double upright_horizontal_guide_angle_ = 0.0;
  // Stored in normalized form; edit paths should mutate this directly and only
  // renormalize when ingesting external stack payloads.
  QVariantList preset_applications_;
  QString recipe_history_branch_ = QStringLiteral("main");
  QVariantList recipe_history_snapshots_;
  mutable bool mask_group_cache_dirty_ = true;
  bool mask_thumbnail_refresh_suppressed_ = false;
  mutable ThumbnailRefreshCoordinator thumbnail_refresh_coordinator_;
  mutable ThumbnailCacheSession thumbnail_cache_session_;
  ThumbnailPresentationService thumbnail_presentation_service_;
  mutable QVariantList cached_mask_sets_;
  mutable QVariantList cached_retouch_stack_;
  QVariantList custom_brush_presets_;
  bool brush_stroke_active_ = false;
  bool brush_stroke_suspends_side_effects_ = false;
  bool brush_stroke_needs_final_render_ = false;
  bool brush_stroke_undo_state_valid_ = false;
  EditState brush_stroke_undo_state_;
  QVector<BrushPreviewPoint> active_brush_live_points_;
  QVector<BrushPreviewPoint> pending_brush_commit_preview_points_;
  QString pending_brush_commit_preview_mask_id_;
  bool active_brush_auto_mask_anchor_valid_ = false;
  double active_brush_auto_mask_anchor_x_ = 0.5;
  double active_brush_auto_mask_anchor_y_ = 0.5;
  QImage active_brush_live_preview_image_;
  BrushLivePreviewFrame active_brush_live_preview_frame_;
  QFutureWatcher<BrushLivePreviewFrame> auto_mask_live_preview_watcher_;
  QVector<BrushPreviewPoint> pending_brush_live_preview_points_;
  std::shared_ptr<std::atomic<int>> brush_live_preview_generation_ =
      std::make_shared<std::atomic<int>>(0);
  bool brush_live_preview_running_ = false;
  int active_brush_live_preview_revision_ = 0;
  QString smart_select_instance_map_key_;
  QByteArray smart_select_instance_map_data_;
  int smart_select_instance_map_width_ = 0;
  int smart_select_instance_map_height_ = 0;
  bool smart_select_instance_map_shared_cache_hit_ = false;
  QVector<int> smart_select_preview_instance_ids_;
  QVector<int> smart_select_preview_candidate_instance_ids_;
  int smart_select_preview_candidate_index_ = 0;
  QVariantMap smart_select_preview_mask_;
  QVariantList smart_select_preview_prompts_;
  bool smart_select_commit_pending_ = false;
  QString smart_select_commit_intent_;
  bool smart_select_commit_create_new_ = false;
  bool smart_select_stroke_active_ = false;
  QVariantList smart_select_stroke_points_;
  double smart_select_stroke_pixel_width_ = 1.0;
  double smart_select_stroke_pixel_height_ = 1.0;
  QTimer smart_select_preview_timer_;
  int smart_select_preview_duplicate_skip_count_ = 0;
  bool linear_mask_enabled_ = false;
  double linear_mask_exposure_ = 0.0;
  double linear_mask_temperature_ = 0.0;
  double linear_mask_feather_ = 0.22;
  double linear_mask_position_ = 0.5;
  double linear_mask_rotation_ = 0.0;
  bool radial_mask_enabled_ = false;
  double radial_mask_exposure_ = 0.0;
  double radial_mask_temperature_ = 0.0;
  double radial_mask_feather_ = 0.25;
  double radial_mask_center_x_ = 0.5;
  double radial_mask_center_y_ = 0.5;
  double radial_mask_radius_x_ = 0.25;
  double radial_mask_radius_y_ = 0.25;
  double radial_mask_rotation_ = 0.0;
  bool white_balance_sampling_ = false;
  QString color_tool_sampling_target_;
  using EditHistorySnapshot = StoredEditHistorySnapshot;
  HistoryService history_service_;
  std::shared_ptr<AgentTaskJournal> agent_task_journal_;
  QString agent_task_mutation_id_;
  AgentRecipePersistenceHandler agent_recipe_persistence_handler_;
  QVariantMap agent_recipe_persistence_context_;
  bool requestHistoryRevision(const HistoryRevision& revision, std::function<void(const EditState&)> apply);
  void invalidateHistoryRead();
  QThreadPool history_restore_pool_;
  QThreadPool history_format_pool_;
  std::shared_ptr<std::atomic<quint64>> history_read_token_ = std::make_shared<std::atomic<quint64>>(0);
  std::shared_ptr<std::atomic<quint64>> history_format_token_ = std::make_shared<std::atomic<quint64>>(0);
  quint64 history_state_generation_ = 0;
  int history_requested_index_ = -1;
  mutable QString history_format_requested_key_;
  mutable QString history_format_ready_key_;
  mutable QVariantList history_format_entries_;
  mutable QHash<QString, QVariantMap> history_format_cache_;
  mutable bool history_format_busy_ = false;

  EditState copied_settings_state_;
  bool has_copied_settings_ = false;
  PresetService preset_service_;
  QStringList custom_crop_presets_;
  RenderScheduler render_scheduler_;
  void dispatchPhotoPaintWork();
  void completePhotoPaintWork();
  void cancelAllPhotoPaint();
  void restoreCancelledPhotoPaintPreview(const QImage& image);
  PhotoFragmentLayerTipCache photo_fragment_layer_tip_cache_;
  std::deque<std::shared_ptr<PhotoPaintSession>> photo_paint_jobs_;
  std::shared_ptr<PhotoPaintSession> photo_paint_input_, photo_paint_running_job_;
  QFutureWatcher<PhotoPaintWorkResult> photo_paint_watcher_;
  QVariantMap last_photo_layer_stack_performance_;
  QString photo_paint_error_;
  QVariantMap photo_paint_pending_asset_;
  quint64 photo_paint_asset_request_generation_ = 0;
  quint64 photo_paint_revision_ = 0;
  bool photo_paint_worker_running_ = false;
  QTimer photo_layer_adjustment_render_timer_;
  QTimer edit_transaction_interactive_preview_timer_;
  QFutureWatcher<RenderResult> overlay_render_watcher_;
  RenderRequest pending_overlay_request_;
  RenderRequest last_overlay_render_request_;
  bool overlay_request_pending_ = false;
  QTimer persist_timer_;
  EditorRecipeSaveQueue recipe_save_queue_;
  bool recipe_close_paused_ = false;
  EditorRecipeSaveJournal recipe_save_journal_;
  QString recipe_save_authority_;
  QString recipe_save_error_;
  int recipe_save_retry_delay_ms_ = 2000;
  bool catalog_recipe_persistence_enabled_ = true;
  QString document_owned_asset_id_;
  bool recipe_save_journal_attempted_ = false;
  QTimer system_monitor_profile_timer_;
  QString system_monitor_profile_name_;
  QString system_monitor_profile_identity_;
  QString system_monitor_profile_discovery_;
  QString system_monitor_profile_display_identity_;
  QString system_monitor_display_target_identity_;
  QString system_monitor_profile_last_discovery_backend_;
  QString system_monitor_profile_failure_reason_;
  int system_monitor_profile_discovery_failures_ = 0;
  bool system_monitor_profile_retained_after_failure_ = false;
  quint64 system_monitor_profile_generation_ = 0U;
  QMetaObject::Connection focus_window_screen_connection_;
  int render_coalesce_request_count_ = 0;
  int edit_intent_count_ = 0;
  EditIntent last_edit_intent_;
  RenderGraphResolution last_render_graph_resolution_;
  bool render_graph_resolution_from_intent_ = false;
  int draft_promoted_count_ = 0;
  int adjusted_preview_cache_hit_count_ = 0;
  int adjusted_preview_cache_fast_schedule_count_ = 0;
  int rendered_preview_cache_hit_count_ = 0;
  int rendered_preview_cache_fast_schedule_count_ = 0;
  int disk_preview_cache_hit_count_ = 0;
  int disk_preview_cache_fast_schedule_count_ = 0;
  int disk_preview_cache_direct_schedule_count_ = 0;
  int visible_tile_prepass_schedule_count_ = 0;
  int viewport_max_preview_schedule_count_ = 0;
  int visible_overlay_queue_wait_exceeded_count_ = 0;
  qint64 last_visible_overlay_queue_wait_ms_ = 0;
  QVariantMap last_mask_work_telemetry_;
  MaskSession mask_session_;
  LayerSession layer_session_;
  mutable int mask_group_cache_rebuild_count_ = 0;
  QVector<PendingBrushCommit> pending_brush_commits_;
  bool pending_brush_commit_scheduled_ = false;
  bool brush_raster_commit_active_ = false;
  BrushRasterCommitRequest active_brush_raster_commit_request_;
  bool active_brush_raster_commit_request_valid_ = false;
  bool active_brush_raster_commit_requeued_ = false;
  MaskWorkerCoordinator mask_worker_coordinator_;
  bool inpaint_progress_active_ = false;
  double inpaint_progress_value_ = 0.0;
  QString inpaint_progress_label_;
  int interactive_render_cancelled_count_ = 0;
  int full_render_cancelled_count_ = 0;
  int photo_layer_adjustment_render_debounce_count_ = 0;
  int photo_layer_adjustment_render_flush_count_ = 0;
  qint64 photo_layer_adjustment_render_burst_started_msecs_ = 0;
  int photo_layer_adjustment_render_pending_write_count_ = 0;
  int photo_layer_adjustment_render_last_coalesced_write_count_ = 0;
  qint64 photo_layer_adjustment_render_last_flush_elapsed_ms_ = 0;
  qint64 photo_layer_adjustment_render_budget_ms_ = 16;
  bool photo_layer_adjustment_render_within_latency_budget_ = true;
  bool photo_layer_adjustment_render_invalidated_active_preview_ = false;
  int photo_layer_adjustment_superseded_preview_count_ = 0;
  bool controls_changed_queued_ = false;
  bool edit_transaction_render_requested_ = false;
  bool photo_effect_preview_active_ = false;
  quint64 edit_transaction_generation_ = 0;
  bool edit_transaction_end_pending_ = false;
  bool edit_transaction_schedule_full_resolution_ = false;
  QString edit_transaction_render_reason_;
  int edit_transaction_interactive_preview_request_count_ = 0;
  QString edit_transaction_interactive_preview_reason_;
  QFutureWatcher<RenderResult> interactive_render_watcher_;
  bool interactive_render_result_pending_ = false;
  QFutureWatcher<RenderResult> full_render_watcher_;
  RenderRequest pending_interactive_request_;
  RenderRequest pending_full_request_;
  bool interactive_request_pending_ = false;
  bool full_request_pending_ = false;
  QProcess persist_process_;
  PreviewSession preview_session_;
  qint64 render_requested_at_msecs_ = 0;
  QString render_reason_ = QStringLiteral("edit_change");
  QString current_tool_id_;
  QString current_interaction_id_;
  int overlay_generation_ = 0;
  std::shared_ptr<std::atomic<int>> latest_overlay_generation_ =
      std::make_shared<std::atomic<int>>(0);
  int last_overlay_preview_generation_ = 0;
  mutable qint64 active_mask_outline_trace_elapsed_ms_ = 0;
  mutable qint64 active_mask_outline_trace_budget_ms_ = 33;
  mutable bool active_mask_outline_trace_within_latency_budget_ = true;
  mutable QString active_mask_outline_trace_source_;
  mutable int active_mask_outline_trace_path_count_ = 0;
  mutable QVariantMap active_mask_outline_trace_fields_;
  double active_mask_outline_display_scale_ = 1.0;
  mutable int active_mask_preview_source_skip_count_ = 0;
  mutable bool smart_select_preview_outline_cache_valid_ = false;
  mutable QVariantList smart_select_preview_outline_cache_points_;
  mutable int smart_select_preview_outline_cache_hit_count_ = 0;
  mutable int smart_select_preview_outline_cache_miss_count_ = 0;
  QTimer photo_paint_preview_timer_;
  bool photo_paint_preview_pending_=false;
  EditorPreviewImageSink* image_provider_ = nullptr;
  std::unique_ptr<BrushPreviewCompositor> brush_preview_compositor_;
  QString active_brush_live_preview_backend_ = QStringLiteral("none");
  qint64 active_brush_live_preview_elapsed_ms_ = 0;
  bool active_brush_live_preview_within_latency_budget_ = true;
  QString active_brush_live_preview_fallback_reason_;
  std::unique_ptr<EditorPreviewTexture> display_preview_texture_;
  int display_preview_native_revision_ = 0;
  QString display_preview_native_rendered_cache_key_;
  QRegion display_preview_native_full_resolution_coverage_;
  QSize display_preview_native_full_resolution_coverage_size_;
  QString display_preview_native_full_resolution_coverage_state_signature_;

 public:
  void setImageProvider(EditorPreviewImageSink* image_provider);
};

}  // namespace photo_editor::desktop
