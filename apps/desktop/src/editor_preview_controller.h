#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QProcess>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QVariantMap>
#include <QVariantList>

#include "brush_preview_compositor.h"
#include "edit_recipe_state.h"
#include "editor_preview_texture.h"
#include "vision_segmentation.h"

namespace photo_editor::desktop {

class EditorPreviewImageSink;
using EditState = EditRecipeState;
struct EditorPreviewControllerTestAccess;

struct RenderRequest final {
  QString file_path;
  QString reference_file_path;
  QString fallback_thumbnail_source;
  QString render_reason;
  QString interaction_mode;
  EditState state;
  QStringList compare_adjustment_groups;
  bool compare_mode = false;
  bool bypass_crop = false;
  bool full_resolution = false;
  int max_edge = 0;
  int jpeg_quality = 90;
  int generation = 0;
  qint64 requested_at_msecs = 0;
  qint64 queue_wait_ms = 0;
  bool use_in_memory_preview = false;
  bool overlay_only = false;
  QRectF preview_overlay_rect;
  int preview_overlay_max_edge = 0;
  bool draft_interactive = false;
  std::shared_ptr<std::atomic<int>> latest_generation;
};

struct HistogramComputationResult final {
  QVariantList red;
  QVariantList green;
  QVariantList blue;
  QVariantList luminance;
  QVariantList waveform_red;
  QVariantList waveform_green;
  QVariantList waveform_blue;
  QVariantList waveform_luminance;
  QVariantList density_red;
  QVariantList density_green;
  QVariantList density_blue;
  QVariantList density_luminance;
  QVariantList vectorscope_points;
  int shadow_clip_count = 0;
  int highlight_clip_count = 0;
  int sample_count = 0;
  int raw_clip_sample_count = 0;
  int raw_shadow_clip_count = 0;
  int raw_highlight_clip_count = 0;
  int generation = 0;
  qint64 elapsed_ms = 0;
  bool full_resolution = false;
  QString render_reason;
  QString interaction_mode;
};

struct RenderResult final {
  QString preview_source;
  QString reference_preview_source;
  QString display_preview_source;
  QString display_reference_preview_source;
  QString display_preview_overlay_source;
  QString display_reference_preview_overlay_source;
  QString status;
  QString render_reason;
  QString interaction_mode;
  QImage preview_image;
  QImage reference_preview_image;
  QImage preview_overlay_image;
  QImage reference_overlay_image;
  QImage histogram_image;
  bool success = false;
  bool cancelled = false;
  bool full_resolution = false;
  bool overlay_only = false;
  bool draft_interactive = false;
  int generation = 0;
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
  bool decoded_cache_lookup = false;
  bool decoded_cache_hit = false;
  bool linear_cache_lookup = false;
  bool linear_cache_hit = false;
  QString decode_backend;
  QString backend;
  QString fallback_reason;
  qint64 total_elapsed_ms = 0;
  qint64 queue_wait_ms = 0;
  qint64 decode_elapsed_ms = 0;
  qint64 edit_elapsed_ms = 0;
  qint64 encode_elapsed_ms = 0;
  bool had_adjustments = false;
  QRectF preview_overlay_rect;
  bool updated_persistent_sources = false;
};

struct SmartSelectPreviewRequest final {
  QString file_path;
  EditState state;
  QVariantList points;
  int generation = 0;
};

struct SmartSelectPreviewResult final {
  QVariantMap preview_mask;
  QVector<int> instance_ids;
  int generation = 0;
  bool success = false;
};

class EditorPreviewController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString previewSource READ previewSource NOTIFY previewChanged)
  Q_PROPERTY(QString displayPreviewSource READ displayPreviewSource NOTIFY previewChanged)
  Q_PROPERTY(QString displayPreviewOverlaySource READ displayPreviewOverlaySource NOTIFY previewChanged)
  Q_PROPERTY(bool displayPreviewNativeAvailable READ displayPreviewNativeAvailable NOTIFY previewChanged)
  Q_PROPERTY(quint64 displayPreviewNativeHandle READ displayPreviewNativeHandle NOTIFY previewChanged)
  Q_PROPERTY(QSize displayPreviewNativeSize READ displayPreviewNativeSize NOTIFY previewChanged)
  Q_PROPERTY(int displayPreviewNativeRevision READ displayPreviewNativeRevision NOTIFY previewChanged)
  Q_PROPERTY(QVariantList histogramRed READ histogramRed NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList histogramGreen READ histogramGreen NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList histogramBlue READ histogramBlue NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList histogramLuminance READ histogramLuminance NOTIFY histogramChanged)
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
  Q_PROPERTY(int previewTargetEdge READ previewTargetEdge WRITE setPreviewTargetEdge NOTIFY previewTargetEdgeChanged)
  Q_PROPERTY(QString referencePreviewSource READ referencePreviewSource NOTIFY previewChanged)
  Q_PROPERTY(QString displayReferencePreviewSource READ displayReferencePreviewSource NOTIFY previewChanged)
  Q_PROPERTY(QString displayReferencePreviewOverlaySource READ displayReferencePreviewOverlaySource NOTIFY previewChanged)
  Q_PROPERTY(QString referenceImagePath READ referenceImagePath NOTIFY referenceImageChanged)
  Q_PROPERTY(QString referenceImageName READ referenceImageName NOTIFY referenceImageChanged)
  Q_PROPERTY(bool hasReferenceImage READ hasReferenceImage NOTIFY referenceImageChanged)
  Q_PROPERTY(QRectF displayPreviewOverlayRect READ displayPreviewOverlayRect NOTIFY previewChanged)
  Q_PROPERTY(QString debugInfo READ debugInfo NOTIFY debugInfoChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString assetId READ assetId NOTIFY assetChanged)
  Q_PROPERTY(QString cameraLabel READ cameraLabel NOTIFY assetChanged)
  Q_PROPERTY(QString captureDate READ captureDate NOTIFY assetChanged)
  Q_PROPERTY(QString lensModel READ lensModel NOTIFY assetChanged)
  Q_PROPERTY(bool hasAsset READ hasAsset NOTIFY assetChanged)
  Q_PROPERTY(bool subjectMaskAvailable READ subjectMaskAvailable CONSTANT)
  Q_PROPERTY(QString aiModelStatus READ aiModelStatus CONSTANT)
  Q_PROPERTY(bool aiSmartSelectionAvailable READ aiSmartSelectionAvailable CONSTANT)
  Q_PROPERTY(bool aiInpaintAvailable READ aiInpaintAvailable CONSTANT)
  Q_PROPERTY(QString inpaintPrompt READ inpaintPrompt WRITE setInpaintPrompt NOTIFY controlsChanged)
  Q_PROPERTY(int inpaintSeed READ inpaintSeed WRITE setInpaintSeed NOTIFY controlsChanged)
  Q_PROPERTY(double inpaintBlendAmount READ inpaintBlendAmount WRITE setInpaintBlendAmount NOTIFY controlsChanged)
  Q_PROPERTY(QString inpaintQualityMode READ inpaintQualityMode WRITE setInpaintQualityMode NOTIFY controlsChanged)
  Q_PROPERTY(bool retouchBeforeAfterEnabled READ retouchBeforeAfterEnabled WRITE setRetouchBeforeAfterEnabled NOTIFY controlsChanged)
  Q_PROPERTY(bool inpaintPatchDiagnosticsEnabled READ inpaintPatchDiagnosticsEnabled WRITE setInpaintPatchDiagnosticsEnabled NOTIFY controlsChanged)
  Q_PROPERTY(QRectF activeInpaintPatchBounds READ activeInpaintPatchBounds NOTIFY controlsChanged)
  Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
  Q_PROPERTY(bool hasCopiedSettings READ hasCopiedSettings NOTIFY clipboardChanged)
  Q_PROPERTY(QVariantList photoLayers READ photoLayers NOTIFY controlsChanged)
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
  Q_PROPERTY(QStringList compareAdjustmentGroups READ compareAdjustmentGroups WRITE setCompareAdjustmentGroups NOTIFY compareAdjustmentGroupsChanged)
  Q_PROPERTY(bool cropEditing READ cropEditing WRITE setCropEditing NOTIFY controlsChanged)
  Q_PROPERTY(QString inputColorProfile READ inputColorProfile WRITE setInputColorProfile NOTIFY controlsChanged)
  Q_PROPERTY(QString outputColorSpace READ outputColorSpace WRITE setOutputColorSpace NOTIFY controlsChanged)
  Q_PROPERTY(QString monitorColorProfile READ monitorColorProfile WRITE setMonitorColorProfile NOTIFY controlsChanged)
  Q_PROPERTY(bool softProofEnabled READ softProofEnabled WRITE setSoftProofEnabled NOTIFY controlsChanged)
  Q_PROPERTY(QString softProofProfile READ softProofProfile WRITE setSoftProofProfile NOTIFY controlsChanged)
  Q_PROPERTY(bool rawControlsAvailable READ rawControlsAvailable NOTIFY assetChanged)
  Q_PROPERTY(double rawExposure READ rawExposure WRITE setRawExposure NOTIFY controlsChanged)
  Q_PROPERTY(double rawBoost READ rawBoost WRITE setRawBoost NOTIFY controlsChanged)
  Q_PROPERTY(double rawShadowBoost READ rawShadowBoost WRITE setRawShadowBoost NOTIFY controlsChanged)
  Q_PROPERTY(double rawDetail READ rawDetail WRITE setRawDetail NOTIFY controlsChanged)
  Q_PROPERTY(double rawChromaNoiseReduction READ rawChromaNoiseReduction WRITE setRawChromaNoiseReduction NOTIFY controlsChanged)
  Q_PROPERTY(double rawMoireReduction READ rawMoireReduction WRITE setRawMoireReduction NOTIFY controlsChanged)
  Q_PROPERTY(double rawHotDeadPixelCleanup READ rawHotDeadPixelCleanup WRITE setRawHotDeadPixelCleanup NOTIFY controlsChanged)
  Q_PROPERTY(double rawFalseColorSuppression READ rawFalseColorSuppression WRITE setRawFalseColorSuppression NOTIFY controlsChanged)
  Q_PROPERTY(bool rawHighlightRecovery READ rawHighlightRecovery WRITE setRawHighlightRecovery NOTIFY controlsChanged)
  Q_PROPERTY(QString highlightReconstructionMode READ highlightReconstructionMode WRITE setHighlightReconstructionMode NOTIFY controlsChanged)
  Q_PROPERTY(QString rawDemosaicQuality READ rawDemosaicQuality WRITE setRawDemosaicQuality NOTIFY controlsChanged)
  Q_PROPERTY(bool rawGamutMapping READ rawGamutMapping WRITE setRawGamutMapping NOTIFY controlsChanged)
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
  Q_PROPERTY(double colorReplacementStrength READ colorReplacementStrength WRITE setColorReplacementStrength NOTIFY controlsChanged)
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
  Q_PROPERTY(int maskCount READ maskCount NOTIFY controlsChanged)
  Q_PROPERTY(int activeMaskIndex READ activeMaskIndex WRITE setActiveMaskIndex NOTIFY controlsChanged)
  Q_PROPERTY(bool hasActiveMask READ hasActiveMask NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskKind READ activeMaskKind NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskToolRole READ activeMaskToolRole NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskRetouchMode READ activeMaskRetouchMode NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskCommitted READ activeMaskCommitted NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskName READ activeMaskName WRITE setActiveMaskName NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskOperation READ activeMaskOperation WRITE setActiveMaskOperation NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskEnabled READ activeMaskEnabled WRITE setActiveMaskEnabled NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskSolo READ activeMaskSolo WRITE setActiveMaskSolo NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskLocked READ activeMaskLocked WRITE setActiveMaskLocked NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskOpacity READ activeMaskOpacity WRITE setActiveMaskOpacity NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskDensity READ activeMaskDensity WRITE setActiveMaskDensity NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskInverted READ activeMaskInverted WRITE setActiveMaskInverted NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskBlendMode READ activeMaskBlendMode WRITE setActiveMaskBlendMode NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskExposure READ activeMaskExposure WRITE setActiveMaskExposure NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskContrast READ activeMaskContrast WRITE setActiveMaskContrast NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskHighlights READ activeMaskHighlights WRITE setActiveMaskHighlights NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskShadows READ activeMaskShadows WRITE setActiveMaskShadows NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskWhites READ activeMaskWhites WRITE setActiveMaskWhites NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBlacks READ activeMaskBlacks WRITE setActiveMaskBlacks NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskTemperature READ activeMaskTemperature WRITE setActiveMaskTemperature NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskTint READ activeMaskTint WRITE setActiveMaskTint NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskVibrance READ activeMaskVibrance WRITE setActiveMaskVibrance NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSaturation READ activeMaskSaturation WRITE setActiveMaskSaturation NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskHueShift READ activeMaskHueShift WRITE setActiveMaskHueShift NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSaturationShift READ activeMaskSaturationShift WRITE setActiveMaskSaturationShift NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskColorizeHue READ activeMaskColorizeHue WRITE setActiveMaskColorizeHue NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskColorizeStrength READ activeMaskColorizeStrength WRITE setActiveMaskColorizeStrength NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRemoveStrength READ activeMaskRemoveStrength WRITE setActiveMaskRemoveStrength NOTIFY controlsChanged)
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
  Q_PROPERTY(double activeMaskBrushSize READ activeMaskBrushSize WRITE setActiveMaskBrushSize NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBrushFlow READ activeMaskBrushFlow WRITE setActiveMaskBrushFlow NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBrushHardness READ activeMaskBrushHardness WRITE setActiveMaskBrushHardness NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBrushSpacing READ activeMaskBrushSpacing WRITE setActiveMaskBrushSpacing NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBrushSmoothing READ activeMaskBrushSmoothing WRITE setActiveMaskBrushSmoothing NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskBrushBuildup READ activeMaskBrushBuildup WRITE setActiveMaskBrushBuildup NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskBrushEraseMode READ activeMaskBrushEraseMode WRITE setActiveMaskBrushEraseMode NOTIFY controlsChanged)
  Q_PROPERTY(bool activeMaskBrushAutoMask READ activeMaskBrushAutoMask WRITE setActiveMaskBrushAutoMask NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBrushAutoMaskTolerance READ activeMaskBrushAutoMaskTolerance WRITE setActiveMaskBrushAutoMaskTolerance NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskBrushPreset READ activeMaskBrushPreset NOTIFY controlsChanged)
  Q_PROPERTY(QStringList brushPresetNames READ brushPresetNames CONSTANT)
  Q_PROPERTY(int activeMaskBrushPointCount READ activeMaskBrushPointCount NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(QVariantList activeMaskLiveBrushPoints READ activeMaskLiveBrushPoints NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(QString activeMaskLiveBrushPreviewSource READ activeMaskLiveBrushPreviewSource NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(bool activeMaskLiveBrushPreviewNativeAvailable READ activeMaskLiveBrushPreviewNativeAvailable NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(bool brushStrokeActive READ brushStrokeActive NOTIFY activeBrushStrokeChanged)
  Q_PROPERTY(bool activeMaskCloneSourceSet READ activeMaskCloneSourceSet NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskCloneSourceX READ activeMaskCloneSourceX NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskCloneSourceY READ activeMaskCloneSourceY NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskCloneScale READ activeMaskCloneScale WRITE setActiveMaskCloneScale NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskCloneRotation READ activeMaskCloneRotation WRITE setActiveMaskCloneRotation NOTIFY controlsChanged)
  Q_PROPERTY(bool hasSensorDustMap READ hasSensorDustMap NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList activeMaskPolygonPoints READ activeMaskPolygonPoints NOTIFY controlsChanged)
  Q_PROPERTY(int activeMaskPolygonPointCount READ activeMaskPolygonPointCount NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList activeMaskOutlinePoints READ activeMaskOutlinePoints NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList activeMaskOutlinePaths READ activeMaskOutlinePaths NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskPreviewSource READ activeMaskPreviewSource NOTIFY controlsChanged)
  Q_PROPERTY(QString maskPreviewMode READ maskPreviewMode WRITE setMaskPreviewMode NOTIFY controlsChanged)
  Q_PROPERTY(bool smartSelectPreviewActive READ smartSelectPreviewActive NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(QString smartSelectPreviewSource READ smartSelectPreviewSource NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(QVariantList smartSelectPreviewOutlinePoints READ smartSelectPreviewOutlinePoints NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(int smartSelectCandidateCount READ smartSelectCandidateCount NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(int smartSelectCandidateIndex READ smartSelectCandidateIndex NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(bool smartSelectCanCycleCandidates READ smartSelectCanCycleCandidates NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(double activeMaskLuminanceMin READ activeMaskLuminanceMin WRITE setActiveMaskLuminanceMin NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskLuminanceMax READ activeMaskLuminanceMax WRITE setActiveMaskLuminanceMax NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskHueCenter READ activeMaskHueCenter WRITE setActiveMaskHueCenter NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskHueRange READ activeMaskHueRange WRITE setActiveMaskHueRange NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSaturationMin READ activeMaskSaturationMin WRITE setActiveMaskSaturationMin NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSaturationMax READ activeMaskSaturationMax WRITE setActiveMaskSaturationMax NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskDepthMin READ activeMaskDepthMin WRITE setActiveMaskDepthMin NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskDepthMax READ activeMaskDepthMax WRITE setActiveMaskDepthMax NOTIFY controlsChanged)
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

 public:
  explicit EditorPreviewController(QObject* parent = nullptr);

  [[nodiscard]] QString previewSource() const;
  [[nodiscard]] QString displayPreviewSource() const;
  [[nodiscard]] QString displayPreviewOverlaySource() const;
  [[nodiscard]] bool displayPreviewNativeAvailable() const;
  [[nodiscard]] quint64 displayPreviewNativeHandle() const;
  [[nodiscard]] QSize displayPreviewNativeSize() const;
  [[nodiscard]] int displayPreviewNativeRevision() const;
  [[nodiscard]] QString referencePreviewSource() const;
  [[nodiscard]] QString displayReferencePreviewSource() const;
  [[nodiscard]] QString displayReferencePreviewOverlaySource() const;
  [[nodiscard]] QString referenceImagePath() const;
  [[nodiscard]] QString referenceImageName() const;
  [[nodiscard]] bool hasReferenceImage() const;
  [[nodiscard]] QRectF displayPreviewOverlayRect() const;
  [[nodiscard]] QVariantList histogramRed() const;
  [[nodiscard]] QVariantList histogramGreen() const;
  [[nodiscard]] QVariantList histogramBlue() const;
  [[nodiscard]] QVariantList histogramLuminance() const;
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
  [[nodiscard]] int previewTargetEdge() const;
  [[nodiscard]] QString debugInfo() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] QString assetId() const;
  [[nodiscard]] QString cameraLabel() const;
  [[nodiscard]] QString captureDate() const;
  [[nodiscard]] QString lensModel() const;
  [[nodiscard]] bool hasAsset() const;
  [[nodiscard]] bool subjectMaskAvailable() const;
  [[nodiscard]] QString aiModelStatus() const;
  [[nodiscard]] bool aiSmartSelectionAvailable() const;
  [[nodiscard]] bool aiInpaintAvailable() const;
  [[nodiscard]] QString inpaintPrompt() const;
  [[nodiscard]] int inpaintSeed() const;
  [[nodiscard]] double inpaintBlendAmount() const;
  [[nodiscard]] QString inpaintQualityMode() const;
  [[nodiscard]] bool retouchBeforeAfterEnabled() const;
  [[nodiscard]] bool inpaintPatchDiagnosticsEnabled() const;
  [[nodiscard]] QRectF activeInpaintPatchBounds() const;
  [[nodiscard]] bool processing() const;
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;
  [[nodiscard]] bool hasCopiedSettings() const;
  [[nodiscard]] QStringList presetNames() const;
  [[nodiscard]] QStringList presetPackNames() const;
  [[nodiscard]] QStringList presetCategoryNames() const;
  [[nodiscard]] QStringList favoritePresetNames() const;
  [[nodiscard]] double presetIntensity() const;
  [[nodiscard]] bool hasCameraDefault() const;
  [[nodiscard]] bool compareMode() const;
  [[nodiscard]] QStringList compareAdjustmentGroups() const;
  [[nodiscard]] bool cropEditing() const;
  [[nodiscard]] QString inputColorProfile() const;
  [[nodiscard]] QString outputColorSpace() const;
  [[nodiscard]] QString monitorColorProfile() const;
  [[nodiscard]] bool softProofEnabled() const;
  [[nodiscard]] QString softProofProfile() const;
  [[nodiscard]] bool rawControlsAvailable() const;
  [[nodiscard]] double rawExposure() const;
  [[nodiscard]] double rawBoost() const;
  [[nodiscard]] double rawShadowBoost() const;
  [[nodiscard]] double rawDetail() const;
  [[nodiscard]] double rawChromaNoiseReduction() const;
  [[nodiscard]] double rawMoireReduction() const;
  [[nodiscard]] double rawHotDeadPixelCleanup() const;
  [[nodiscard]] double rawFalseColorSuppression() const;
  [[nodiscard]] bool rawHighlightRecovery() const;
  [[nodiscard]] QString highlightReconstructionMode() const;
  [[nodiscard]] QString rawDemosaicQuality() const;
  [[nodiscard]] bool rawGamutMapping() const;
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
  [[nodiscard]] double colorReplacementStrength() const;
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
  [[nodiscard]] int maskSetCount() const;
  [[nodiscard]] QVariantList retouchStack() const;
  [[nodiscard]] int retouchCount() const;
  [[nodiscard]] QVariantList photoLayers() const;
  [[nodiscard]] int photoLayerCount() const;
  [[nodiscard]] bool photoLayerSoloActive() const;
  [[nodiscard]] QVariantList photoLayerVisibilityStates() const;
  [[nodiscard]] int photoLayerVisibilityStateCount() const;
  [[nodiscard]] int sourceImageWidth() const;
  [[nodiscard]] int sourceImageHeight() const;
  [[nodiscard]] QVariantList maskStack() const;
  [[nodiscard]] int maskCount() const;
  [[nodiscard]] int activeMaskIndex() const;
  [[nodiscard]] bool hasActiveMask() const;
  [[nodiscard]] QString activeMaskKind() const;
  [[nodiscard]] QString activeMaskToolRole() const;
  [[nodiscard]] QString activeMaskRetouchMode() const;
  [[nodiscard]] bool activeMaskCommitted() const;
  [[nodiscard]] QString activeMaskName() const;
  [[nodiscard]] QString activeMaskOperation() const;
  [[nodiscard]] bool activeMaskEnabled() const;
  [[nodiscard]] bool activeMaskSolo() const;
  [[nodiscard]] bool activeMaskLocked() const;
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
  [[nodiscard]] double activeMaskBrushFlow() const;
  [[nodiscard]] double activeMaskBrushHardness() const;
  [[nodiscard]] double activeMaskBrushSpacing() const;
  [[nodiscard]] double activeMaskBrushSmoothing() const;
  [[nodiscard]] bool activeMaskBrushBuildup() const;
  [[nodiscard]] bool activeMaskBrushEraseMode() const;
  [[nodiscard]] bool activeMaskBrushAutoMask() const;
  [[nodiscard]] double activeMaskBrushAutoMaskTolerance() const;
  [[nodiscard]] QString activeMaskBrushPreset() const;
  [[nodiscard]] QStringList brushPresetNames() const;
  [[nodiscard]] int activeMaskBrushPointCount() const;
  [[nodiscard]] QVariantList activeMaskLiveBrushPoints() const;
  [[nodiscard]] QString activeMaskLiveBrushPreviewSource() const;
  [[nodiscard]] bool activeMaskLiveBrushPreviewNativeAvailable() const;
  [[nodiscard]] quint64 activeMaskLiveBrushPreviewNativeHandle() const;
  [[nodiscard]] QSize activeMaskLiveBrushPreviewNativeSize() const;
  [[nodiscard]] int activeMaskLiveBrushPreviewRevision() const;
  [[nodiscard]] bool brushStrokeActive() const;
  [[nodiscard]] bool activeMaskCloneSourceSet() const;
  [[nodiscard]] double activeMaskCloneSourceX() const;
  [[nodiscard]] double activeMaskCloneSourceY() const;
  [[nodiscard]] double activeMaskCloneScale() const;
  [[nodiscard]] double activeMaskCloneRotation() const;
  [[nodiscard]] bool hasSensorDustMap() const;
  [[nodiscard]] QVariantList activeMaskPolygonPoints() const;
  [[nodiscard]] int activeMaskPolygonPointCount() const;
  [[nodiscard]] QVariantList activeMaskOutlinePoints() const;
  [[nodiscard]] QVariantList activeMaskOutlinePaths() const;
  [[nodiscard]] QString activeMaskPreviewSource() const;
  [[nodiscard]] QString maskPreviewMode() const;
  [[nodiscard]] bool smartSelectPreviewActive() const;
  [[nodiscard]] QString smartSelectPreviewSource() const;
  [[nodiscard]] QVariantList smartSelectPreviewOutlinePoints() const;
  [[nodiscard]] int smartSelectCandidateCount() const;
  [[nodiscard]] int smartSelectCandidateIndex() const;
  [[nodiscard]] bool smartSelectCanCycleCandidates() const;
  [[nodiscard]] double activeMaskLuminanceMin() const;
  [[nodiscard]] double activeMaskLuminanceMax() const;
  [[nodiscard]] double activeMaskHueCenter() const;
  [[nodiscard]] double activeMaskHueRange() const;
  [[nodiscard]] double activeMaskSaturationMin() const;
  [[nodiscard]] double activeMaskSaturationMax() const;
  [[nodiscard]] double activeMaskDepthMin() const;
  [[nodiscard]] double activeMaskDepthMax() const;
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

  void setPreviewTargetEdge(int edge);
  void setPresetIntensity(double intensity);
  void setCompareMode(bool compare_mode);
  void setCompareAdjustmentGroups(const QStringList& groups);
  void setCropEditing(bool crop_editing);
  void setInputColorProfile(const QString& input_color_profile);
  void setOutputColorSpace(const QString& output_color_space);
  void setMonitorColorProfile(const QString& monitor_color_profile);
  void setSoftProofEnabled(bool enabled);
  void setSoftProofProfile(const QString& soft_proof_profile);
  void setRawExposure(double raw_exposure);
  void setRawBoost(double raw_boost);
  void setRawShadowBoost(double raw_shadow_boost);
  void setRawDetail(double raw_detail);
  void setRawChromaNoiseReduction(double raw_chroma_noise_reduction);
  void setRawMoireReduction(double raw_moire_reduction);
  void setRawHotDeadPixelCleanup(double raw_hot_dead_pixel_cleanup);
  void setRawFalseColorSuppression(double raw_false_color_suppression);
  void setRawHighlightRecovery(bool enabled);
  void setHighlightReconstructionMode(const QString& mode);
  void setRawDemosaicQuality(const QString& quality);
  void setRawGamutMapping(bool enabled);
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
  void setColorReplacementStrength(double value);
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
  void setCropZoom(double crop_zoom);
  void setCropPreset(const QString& crop_preset);
  void setUprightMode(const QString& upright_mode);
  void setUprightVerticalGuideAngle(double guide_angle);
  void setUprightHorizontalGuideAngle(double guide_angle);
  void setActiveMaskIndex(int index);
  void setMaskPreviewMode(const QString& mode);
  void setActiveMaskName(const QString& name);
  void setInpaintPrompt(const QString& prompt);
  void setInpaintSeed(int seed);
  void setInpaintBlendAmount(double amount);
  void setInpaintQualityMode(const QString& mode);
  void setRetouchBeforeAfterEnabled(bool enabled);
  void setInpaintPatchDiagnosticsEnabled(bool enabled);
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
  void setActiveMaskSelectionInverted(bool selection_inverted);
  void setActiveMaskSelectionExpand(double selection_expand);
  void setActiveMaskSelectionSmooth(double selection_smooth);
  void setActiveMaskSelectionFineEdge(double fine_edge);
  void setActiveMaskSelectionEdgeContrast(double edge_contrast);
  void setActiveMaskSelectionDecontaminate(double decontaminate);
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
  void setActiveMaskBrushBuildup(bool brush_buildup);
  void setActiveMaskBrushEraseMode(bool erase_mode);
  void setActiveMaskBrushAutoMask(bool auto_mask);
  void setActiveMaskBrushAutoMaskTolerance(double tolerance);
  Q_INVOKABLE bool applyActiveMaskBrushPreset(const QString& name);
  void setActiveMaskCloneScale(double clone_scale);
  void setActiveMaskCloneRotation(double clone_rotation);
  void setActiveMaskLuminanceMin(double min_value);
  void setActiveMaskLuminanceMax(double max_value);
  void setActiveMaskHueCenter(double center);
  void setActiveMaskHueRange(double range);
  void setActiveMaskSaturationMin(double min_value);
  void setActiveMaskSaturationMax(double max_value);
  void setActiveMaskDepthMin(double min_value);
  void setActiveMaskDepthMax(double max_value);
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
      const QString& recipe_json);
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void resetAdjustments();
  Q_INVOKABLE bool resetAdjustmentGroup(const QString& group);
  Q_INVOKABLE QString currentRecipeJson() const;
  Q_INVOKABLE QString copiedSettingsRecipeJson() const;
  Q_INVOKABLE QString presetRecipeJson(const QString& name) const;
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
  Q_INVOKABLE bool applyPreset(const QString& name, const QStringList& adjustment_groups = {});
  Q_INVOKABLE bool applyPresetWithIntensity(
      const QString& name,
      double intensity,
      const QStringList& adjustment_groups = {});
  Q_INVOKABLE QString presetPreviewRecipeJson(
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
  Q_INVOKABLE bool addFillLayer(const QString& color = {}, const QString& name = {});
  Q_INVOKABLE bool addTextLayer(const QString& text = {}, const QString& name = {});
  Q_INVOKABLE bool addSourceLayer(const QString& file_path, const QString& name = {});
  Q_INVOKABLE bool addLayerGroup(const QString& name = {});
  Q_INVOKABLE bool setPhotoLayerEnabled(int index, bool enabled);
  Q_INVOKABLE bool setPhotoLayerLocked(int index, bool locked);
  Q_INVOKABLE bool setPhotoLayerSolo(int index, bool solo);
  Q_INVOKABLE bool setPhotoLayerOpacity(int index, double opacity);
  Q_INVOKABLE bool setPhotoLayerBlendMode(int index, const QString& blend_mode);
  Q_INVOKABLE bool setPhotoLayerClippingMask(int index, bool clipping_mask);
  Q_INVOKABLE bool setPhotoLayerMaskEnabled(int index, bool enabled);
  Q_INVOKABLE bool isolatePhotoLayer(int index);
  Q_INVOKABLE bool clearPhotoLayerSolo();
  Q_INVOKABLE bool savePhotoLayerVisibilityState(const QString& name = {});
  Q_INVOKABLE bool applyPhotoLayerVisibilityState(int index);
  Q_INVOKABLE bool deletePhotoLayerVisibilityState(int index);
  Q_INVOKABLE bool renamePhotoLayer(int index, const QString& name);
  Q_INVOKABLE bool setPhotoLayerTransform(
      int index,
      int x,
      int y,
      double scale,
      double rotation);
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
  Q_INVOKABLE QStringList cropAspectPresets() const;
  Q_INVOKABLE QStringList customCropAspectPresets() const;
  Q_INVOKABLE bool addCustomCropAspectPreset(const QString& preset);
  Q_INVOKABLE bool removeCustomCropAspectPreset(const QString& preset);
  Q_INVOKABLE bool exportCustomCropAspectPresets(const QString& file_path) const;
  Q_INVOKABLE int importCustomCropAspectPresets(const QString& file_path, bool replace = false);
  Q_INVOKABLE bool setReferenceImage(const QString& file_path);
  Q_INVOKABLE void clearReferenceImage();
  Q_INVOKABLE bool duplicatePhotoLayer(int index);
  Q_INVOKABLE bool movePhotoLayer(int index, int offset);
  Q_INVOKABLE bool deletePhotoLayer(int index);
  Q_INVOKABLE void addMask(const QString& kind);
  Q_INVOKABLE void addAIMask(const QString& semantic_kind);
  Q_INVOKABLE bool addAlphaImageMask(const QString& file_path);
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
  Q_INVOKABLE void updateSmartSelectPreview(double normalized_x, double normalized_y);
  Q_INVOKABLE void updateSmartSelectPreviewPath(const QVariantList& points);
  Q_INVOKABLE void scheduleSmartSelectPreview(double normalized_x, double normalized_y);
  Q_INVOKABLE void scheduleSmartSelectPreviewPath(const QVariantList& points);
  Q_INVOKABLE void clearSmartSelectPreview();
  Q_INVOKABLE void cycleSmartSelectCandidate(int delta = 1);
  Q_INVOKABLE void deselectActiveMask();
  Q_INVOKABLE void smartSelectAddAt(double normalized_x, double normalized_y, bool create_new = false);
  Q_INVOKABLE void smartSelectSubtractAt(double normalized_x, double normalized_y, bool create_new = false);
  Q_INVOKABLE void smartSelectObjectAt(double normalized_x, double normalized_y, bool create_new = false);
  Q_INVOKABLE void objectRemoveAt(double normalized_x, double normalized_y, bool create_new = false);
  Q_INVOKABLE void startPolygonSelection(bool subtract = false);
  Q_INVOKABLE void startLassoSelection(bool subtract = false);
  Q_INVOKABLE void appendActivePolygonPoint(double normalized_x, double normalized_y);
  Q_INVOKABLE void removeLastActivePolygonPoint();
  Q_INVOKABLE void clearActivePolygonPoints();
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
  Q_INVOKABLE void applyActiveRetouch();
  Q_INVOKABLE void cancelActiveRetouch();
  Q_INVOKABLE void regenerateInpaintSeed();
  Q_INVOKABLE QVariantList reusableMaskOutputs() const;
  Q_INVOKABLE void duplicateActiveMask();
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
  Q_INVOKABLE void updatePixelInspector(double normalized_x, double normalized_y);
  Q_INVOKABLE void clearPixelInspector();
  Q_INVOKABLE void useAsShotRawWhiteBalance();

signals:
  void previewChanged();
  void histogramChanged();
  void previewTargetEdgeChanged();
  void debugInfoChanged();
  void statusChanged();
  void assetChanged();
  void processingChanged();
  void controlsChanged();
  void activeBrushStrokeChanged();
  void maskOverlayChanged();
  void compareModeChanged();
  void compareAdjustmentGroupsChanged();
  void referenceImageChanged();
  void whiteBalanceSamplingChanged();
  void pixelInspectorChanged();
  void histogramActivityChanged();
  void historyChanged();
  void clipboardChanged();
  void presetsChanged();
  void presetIntensityChanged();
  void cameraDefaultsChanged();
  void smartSelectPreviewChanged();

 private:
  friend struct EditorPreviewControllerTestAccess;

  void renderPreview(bool schedule_full_resolution = true, const QString& reason = {});
  void performRenderPreview(
      bool schedule_full_resolution,
      int coalesced_request_count,
      const QString& reason);
  void beginEditTransaction();
  void endEditTransaction();
  void renderPreviewStage(bool full_resolution);
  void handleRenderFinished(bool full_resolution);
  void startRenderStage(bool full_resolution, const RenderRequest& request);
  void updateProcessingState();
  [[nodiscard]] RenderRequest currentRenderRequest(bool full_resolution) const;
  void refreshDebugInfo();
  void setStatus(const QString& status);
  void setProcessing(bool processing);
  [[nodiscard]] QString cachePathForSelection(bool edited, bool full_resolution) const;
  void pushUndoState(const EditState& state);
  [[nodiscard]] QVector<EditState>& undoStackForAsset(const QString& asset_id);
  [[nodiscard]] QVector<EditState>& redoStackForAsset(const QString& asset_id);
  void rememberStateForUndo();
  void clearHistoryForAsset(const QString& asset_id);
  void applyHistoryState(const EditState& state);
  void persistCurrentState();
  void restoreStateForAsset(const QString& asset_id, const QString& recipe_json);
  void refreshRawAsShotWhiteBalanceMetadata();
  void setRawAsShotWhiteBalance(double temperature, double tint);
  void applyState(const EditState& state);
  [[nodiscard]] EditState currentState() const;
  [[nodiscard]] EditState stateFromRecipeJson(const QString& recipe_json) const;
  [[nodiscard]] QString recipeJsonForState(const EditState& state) const;
  [[nodiscard]] QString normalizedPresetName(const QString& name) const;
  [[nodiscard]] QString normalizedCameraDefaultKey(const QString& camera_label) const;
  [[nodiscard]] QString presetsFilePath() const;
  [[nodiscard]] QString cameraDefaultsFilePath() const;
  [[nodiscard]] QString customCropPresetsFilePath() const;
  [[nodiscard]] int firstMaskIndexForKind(const QString& kind) const;
  [[nodiscard]] QVariantMap activeMaskDefinition() const;
  [[nodiscard]] QVariantMap maskDefinitionAt(int index) const;
  void invalidateActiveMaskCache();
  void invalidateMaskDerivedCaches();
  void synchronizeMaskState();
  bool applyMaskStackChange(const QVariantList& stack, int active_index, bool rerender = true);
  bool applyPhotoLayerStackChange(const QVariantList& layers);
  bool updateMaskAt(int index, const QVariantMap& mask, bool rerender = true, bool allow_locked = false);
  bool updateActiveMaskDefinition(const QVariantMap& mask, bool rerender = true, bool allow_locked = false);
  bool updateLegacyKindMask(const QString& kind, const QVariantMap& updates);
  void setActiveMaskRefineField(const QString& key, double value, double minimum, double maximum);
  void setActiveMaskRefineMinMaxField(
      const QString& key,
      const QString& paired_key,
      double value,
      bool is_minimum);
  void loadPresets();
  void savePresets() const;
  void loadCameraDefaults();
  void saveCameraDefaults() const;
  void loadCustomCropPresets();
  void saveCustomCropPresets() const;
  void flushPersistedState();
  [[nodiscard]] bool hasVisibleAdjustments() const;
  [[nodiscard]] bool decodeSourceImage(QImage* image, int max_edge) const;
  [[nodiscard]] QSize sourceImageSize() const;
  [[nodiscard]] QImage applyCrop(const QImage& image) const;
  void applyColorAdjustments(QImage* image) const;
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
  void handleHistogramFinished();
  void applyHistogramResult(const HistogramComputationResult& result);
  void setHistogramUpdateActive(bool active);
  void resetSmartSelectPreview();
  void startSmartSelectPreviewRequest();
  void handleSmartSelectPreviewFinished();
  void updateSmartSelectPreviewState(
      const QVariantMap& preview_mask,
      const QVector<int>& instance_ids = {},
      const QVector<int>& candidate_instance_ids = {});
  void updateSmartSelectPreviewResult(
      const SegmentationResult& result,
      const QVector<int>& instance_ids = {});
  void clearSmartSelectPreviewState();
  void updateActiveBrushLivePreview(const QVector<BrushPreviewPoint>& sampled_points);
  void clearActiveBrushLivePreview();
  [[nodiscard]] QSize activeBrushLivePreviewSize() const;
  void resetSmartSelectInstanceMap();
  [[nodiscard]] bool ensureSmartSelectInstanceMap();
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
      bool create_new);
  QString pickObjectRemoveMask(const SegmentationResult& mask_result, bool create_new);
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
  QString saveRecipeProgram() const;
  QStringList saveRecipeArguments(const QString& asset_id, const QString& recipe_json) const;
  [[nodiscard]] QString withDisplayRevision(const QString& source) const;

  QVariantList histogram_red_;
  QVariantList histogram_green_;
  QVariantList histogram_blue_;
  QVariantList histogram_luminance_;
  QVariantList scope_waveform_red_;
  QVariantList scope_waveform_green_;
  QVariantList scope_waveform_blue_;
  QVariantList scope_waveform_luminance_;
  QVariantList scope_density_red_;
  QVariantList scope_density_green_;
  QVariantList scope_density_blue_;
  QVariantList scope_density_luminance_;
  QVariantList scope_vectorscope_points_;
  int histogram_shadow_clip_count_ = 0;
  int histogram_highlight_clip_count_ = 0;
  int histogram_sample_count_ = 0;
  int raw_clip_sample_count_ = 0;
  int raw_shadow_clip_count_ = 0;
  int raw_highlight_clip_count_ = 0;
  QVariantMap pixel_inspector_;
  QImage pixel_inspector_preview_image_;
  QImage pixel_inspector_reference_image_;
  QFutureWatcher<HistogramComputationResult> histogram_watcher_;
  QImage pending_histogram_image_;
  int pending_histogram_generation_ = 0;
  bool pending_histogram_full_resolution_ = false;
  int pending_histogram_raw_clip_sample_count_ = 0;
  int pending_histogram_raw_shadow_clip_count_ = 0;
  int pending_histogram_raw_highlight_clip_count_ = 0;
  QString pending_histogram_render_reason_;
  QString pending_histogram_interaction_mode_;
  bool histogram_update_pending_ = false;
  bool histogram_update_active_ = false;

  QString asset_id_;
  QString file_path_;
  QString fallback_thumbnail_source_;
  QString preview_source_;
  QString display_preview_source_;
  QString display_preview_overlay_source_;
  QString reference_preview_source_;
  QString display_reference_preview_source_;
  QString display_reference_preview_overlay_source_;
  QString reference_image_path_;
  QString reference_image_name_;
  QRectF display_preview_overlay_rect_;
  QString debug_info_;
  QString status_ = QStringLiteral("No asset selected.");
  qint64 last_interactive_render_ms_ = 0;
  qint64 last_full_render_ms_ = 0;
  qint64 last_decode_render_ms_ = 0;
  qint64 last_edit_render_ms_ = 0;
  qint64 last_encode_render_ms_ = 0;
  QSize last_render_source_size_;
  QSize last_render_output_size_;
  bool last_render_had_adjustments_ = false;
  QString camera_label_;
  QString capture_date_;
  QString lens_model_;
  int preview_target_edge_ = 0;
  qint64 last_render_request_msecs_ = 0;
  bool next_interactive_render_draft_ = false;
  QRectF preview_viewport_rect_;
  int preview_viewport_patch_max_edge_ = 0;
  bool preview_viewport_active_ = false;
  QString inpaint_prompt_;
  int inpaint_seed_ = 1;
  double inpaint_blend_amount_ = 1.0;
  QString inpaint_quality_mode_ = QStringLiteral("preview");
  bool retouch_before_after_enabled_ = false;
  bool inpaint_patch_diagnostics_enabled_ = false;
  bool processing_ = false;
  bool compare_mode_ = false;
  QStringList compare_adjustment_groups_;
  bool crop_editing_ = false;
  QString input_color_profile_ = QStringLiteral("Embedded");
  QString working_color_profile_ = QStringLiteral("Linear ProPhoto RGB");
  QString preview_color_space_ = QStringLiteral("sRGB");
  QString monitor_color_profile_;
  bool soft_proof_enabled_ = false;
  QString soft_proof_profile_ = QStringLiteral("sRGB");
  QString render_intent_ = QStringLiteral("Perceptual");
  bool raw_input_transform_enabled_ = true;
  QString highlight_reconstruction_mode_ = QStringLiteral("Enabled");
  QString chromatic_adaptation_method_ = QStringLiteral("Bradford");
  QString output_color_space_ = QStringLiteral("sRGB");
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
  bool raw_gamut_mapping_ = true;
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
  QString color_mixer_model_ = QStringLiteral("OKLab");
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
  double color_replacement_strength_ = 0.0;
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
  QVariantList mask_stack_;
  QVariantList photo_layers_;
  QVariantList photo_layer_visibility_states_;
  mutable bool mask_group_cache_dirty_ = true;
  mutable QVariantList cached_mask_sets_;
  mutable QVariantList cached_retouch_stack_;
  mutable bool active_mask_cache_dirty_ = true;
  mutable QVariantMap cached_active_mask_definition_;
  int active_mask_index_ = -1;
  bool brush_stroke_active_ = false;
  bool brush_stroke_suspends_side_effects_ = false;
  bool brush_stroke_needs_final_render_ = false;
  bool brush_stroke_undo_state_valid_ = false;
  EditState brush_stroke_undo_state_;
  QVector<BrushPreviewPoint> active_brush_live_points_;
  QImage active_brush_live_preview_image_;
  int active_brush_live_preview_revision_ = 0;
  QString mask_preview_mode_ = QStringLiteral("overlay");
  QString smart_select_instance_map_key_;
  QByteArray smart_select_instance_map_data_;
  int smart_select_instance_map_width_ = 0;
  int smart_select_instance_map_height_ = 0;
  QVector<int> smart_select_preview_instance_ids_;
  QVector<int> smart_select_preview_candidate_instance_ids_;
  int smart_select_preview_candidate_index_ = 0;
  QVariantMap smart_select_preview_mask_;
  QTimer smart_select_preview_timer_;
  QFutureWatcher<SmartSelectPreviewResult> smart_select_preview_watcher_;
  SmartSelectPreviewRequest pending_smart_select_preview_request_;
  bool smart_select_preview_request_pending_ = false;
  int smart_select_preview_generation_ = 0;
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
  mutable QMutex asset_state_mutex_;
  QHash<QString, EditState> asset_states_;
  QHash<QString, QVector<EditState>> asset_undo_states_;
  QHash<QString, QVector<EditState>> asset_redo_states_;
  QList<QString> asset_access_order_;
  EditState copied_settings_state_;
  bool has_copied_settings_ = false;
  QMap<QString, EditState> built_in_presets_;
  QMap<QString, QString> built_in_preset_packs_;
  QMap<QString, QString> built_in_preset_categories_;
  QMap<QString, QStringList> built_in_preset_groups_;
  QMap<QString, QJsonArray> built_in_preset_adaptive_rules_;
  QMap<QString, EditState> presets_;
  QMap<QString, QString> user_preset_packs_;
  QMap<QString, QString> user_preset_categories_;
  QMap<QString, QStringList> user_preset_groups_;
  QMap<QString, QJsonArray> user_preset_adaptive_rules_;
  QSet<QString> favorite_presets_;
  double preset_intensity_ = 1.0;
  QMap<QString, EditState> camera_default_states_;
  QStringList custom_crop_presets_;
  bool applying_history_state_ = false;
  QTimer interactive_render_timer_;
  QTimer full_render_timer_;
  QTimer persist_timer_;
  int render_coalesce_request_count_ = 0;
  int edit_transaction_depth_ = 0;
  bool edit_transaction_render_requested_ = false;
  bool edit_transaction_schedule_full_resolution_ = false;
  QString edit_transaction_render_reason_;
  QFutureWatcher<RenderResult> interactive_render_watcher_;
  QFutureWatcher<RenderResult> full_render_watcher_;
  RenderRequest pending_interactive_request_;
  RenderRequest pending_full_request_;
  bool interactive_request_pending_ = false;
  bool full_request_pending_ = false;
  QProcess persist_process_;
  int preview_revision_ = 0;
  int render_generation_ = 0;
  qint64 render_requested_at_msecs_ = 0;
  QString render_reason_ = QStringLiteral("edit_change");
  std::shared_ptr<std::atomic<int>> latest_render_generation_ =
      std::make_shared<std::atomic<int>>(0);
  int last_interactive_preview_generation_ = 0;
  int overlay_generation_ = 0;
  std::shared_ptr<std::atomic<int>> latest_overlay_generation_ =
      std::make_shared<std::atomic<int>>(0);
  int last_overlay_preview_generation_ = 0;
  mutable QString active_mask_preview_cache_key_;
  mutable QString active_mask_preview_cache_source_;
  mutable QString smart_select_preview_cache_key_;
  mutable QString smart_select_preview_cache_source_;
  int last_full_preview_generation_ = 0;
  EditorPreviewImageSink* image_provider_ = nullptr;
  std::unique_ptr<BrushPreviewCompositor> brush_preview_compositor_;
  std::unique_ptr<EditorPreviewTexture> display_preview_texture_;
  int display_preview_native_revision_ = 0;

 public:
  void setImageProvider(EditorPreviewImageSink* image_provider);
};

}  // namespace photo_editor::desktop
