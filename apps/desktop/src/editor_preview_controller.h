#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QProcess>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QVariantList>

#include "brush_preview_compositor.h"
#include "edit_recipe_state.h"
#include "vision_segmentation.h"

namespace photo_editor::desktop {

class EditorPreviewImageSink;
using EditState = EditRecipeState;
struct EditorPreviewControllerTestAccess;

struct RenderRequest final {
  QString file_path;
  QString fallback_thumbnail_source;
  EditState state;
  bool compare_mode = false;
  bool bypass_crop = false;
  bool full_resolution = false;
  int max_edge = 0;
  int jpeg_quality = 90;
  int generation = 0;
  bool use_in_memory_preview = false;
  bool overlay_only = false;
  QRectF preview_overlay_rect;
  int preview_overlay_max_edge = 0;
  std::shared_ptr<std::atomic<int>> latest_generation;
};

struct RenderResult final {
  QString preview_source;
  QString reference_preview_source;
  QString display_preview_source;
  QString display_reference_preview_source;
  QString display_preview_overlay_source;
  QString display_reference_preview_overlay_source;
  QString status;
  QImage preview_image;
  QImage reference_preview_image;
  QImage preview_overlay_image;
  QImage reference_overlay_image;
  QImage histogram_image;
  bool success = false;
  bool cancelled = false;
  bool full_resolution = false;
  bool overlay_only = false;
  int generation = 0;
  int source_width = 0;
  int source_height = 0;
  int output_width = 0;
  int output_height = 0;
  qint64 total_elapsed_ms = 0;
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
  Q_PROPERTY(QVariantList histogramRed READ histogramRed NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList histogramGreen READ histogramGreen NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList histogramBlue READ histogramBlue NOTIFY histogramChanged)
  Q_PROPERTY(QVariantList histogramLuminance READ histogramLuminance NOTIFY histogramChanged)
  Q_PROPERTY(int previewTargetEdge READ previewTargetEdge WRITE setPreviewTargetEdge NOTIFY previewTargetEdgeChanged)
  Q_PROPERTY(QString referencePreviewSource READ referencePreviewSource NOTIFY previewChanged)
  Q_PROPERTY(QString displayReferencePreviewSource READ displayReferencePreviewSource NOTIFY previewChanged)
  Q_PROPERTY(QString displayReferencePreviewOverlaySource READ displayReferencePreviewOverlaySource NOTIFY previewChanged)
  Q_PROPERTY(QRectF displayPreviewOverlayRect READ displayPreviewOverlayRect NOTIFY previewChanged)
  Q_PROPERTY(QString debugInfo READ debugInfo NOTIFY debugInfoChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString cameraLabel READ cameraLabel NOTIFY assetChanged)
  Q_PROPERTY(QString captureDate READ captureDate NOTIFY assetChanged)
  Q_PROPERTY(QString lensModel READ lensModel NOTIFY assetChanged)
  Q_PROPERTY(bool hasAsset READ hasAsset NOTIFY assetChanged)
  Q_PROPERTY(bool subjectMaskAvailable READ subjectMaskAvailable CONSTANT)
  Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
  Q_PROPERTY(bool hasCopiedSettings READ hasCopiedSettings NOTIFY clipboardChanged)
  Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetsChanged)
  Q_PROPERTY(bool compareMode READ compareMode WRITE setCompareMode NOTIFY compareModeChanged)
  Q_PROPERTY(bool cropEditing READ cropEditing WRITE setCropEditing NOTIFY controlsChanged)
  Q_PROPERTY(bool rawControlsAvailable READ rawControlsAvailable NOTIFY assetChanged)
  Q_PROPERTY(double rawExposure READ rawExposure WRITE setRawExposure NOTIFY controlsChanged)
  Q_PROPERTY(double rawBoost READ rawBoost WRITE setRawBoost NOTIFY controlsChanged)
  Q_PROPERTY(double rawShadowBoost READ rawShadowBoost WRITE setRawShadowBoost NOTIFY controlsChanged)
  Q_PROPERTY(double rawDetail READ rawDetail WRITE setRawDetail NOTIFY controlsChanged)
  Q_PROPERTY(bool rawHighlightRecovery READ rawHighlightRecovery WRITE setRawHighlightRecovery NOTIFY controlsChanged)
  Q_PROPERTY(bool rawGamutMapping READ rawGamutMapping WRITE setRawGamutMapping NOTIFY controlsChanged)
  Q_PROPERTY(bool rawLensCorrection READ rawLensCorrection WRITE setRawLensCorrection NOTIFY controlsChanged)
  Q_PROPERTY(bool rawWhiteBalanceOverride READ rawWhiteBalanceOverride WRITE setRawWhiteBalanceOverride NOTIFY controlsChanged)
  Q_PROPERTY(double rawWhiteBalanceTemperature READ rawWhiteBalanceTemperature WRITE setRawWhiteBalanceTemperature NOTIFY controlsChanged)
  Q_PROPERTY(double rawWhiteBalanceTint READ rawWhiteBalanceTint WRITE setRawWhiteBalanceTint NOTIFY controlsChanged)
  Q_PROPERTY(double exposure READ exposure WRITE setExposure NOTIFY controlsChanged)
  Q_PROPERTY(double contrast READ contrast WRITE setContrast NOTIFY controlsChanged)
  Q_PROPERTY(double highlights READ highlights WRITE setHighlights NOTIFY controlsChanged)
  Q_PROPERTY(double shadows READ shadows WRITE setShadows NOTIFY controlsChanged)
  Q_PROPERTY(double whites READ whites WRITE setWhites NOTIFY controlsChanged)
  Q_PROPERTY(double blacks READ blacks WRITE setBlacks NOTIFY controlsChanged)
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
  Q_PROPERTY(double vibrance READ vibrance WRITE setVibrance NOTIFY controlsChanged)
  Q_PROPERTY(double saturation READ saturation WRITE setSaturation NOTIFY controlsChanged)
  Q_PROPERTY(double curveShadows READ curveShadows WRITE setCurveShadows NOTIFY controlsChanged)
  Q_PROPERTY(double curveDarks READ curveDarks WRITE setCurveDarks NOTIFY controlsChanged)
  Q_PROPERTY(double curveLights READ curveLights WRITE setCurveLights NOTIFY controlsChanged)
  Q_PROPERTY(double curveHighlights READ curveHighlights WRITE setCurveHighlights NOTIFY controlsChanged)
  Q_PROPERTY(QString curveChannel READ curveChannel WRITE setCurveChannel NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList activeCurvePoints READ activeCurvePoints WRITE setActiveCurvePoints NOTIFY controlsChanged)
  Q_PROPERTY(QString hslChannel READ hslChannel WRITE setHslChannel NOTIFY controlsChanged)
  Q_PROPERTY(double hueAdjustment READ hueAdjustment WRITE setHueAdjustment NOTIFY controlsChanged)
  Q_PROPERTY(double saturationAdjustment READ saturationAdjustment WRITE setSaturationAdjustment NOTIFY controlsChanged)
  Q_PROPERTY(double luminanceAdjustment READ luminanceAdjustment WRITE setLuminanceAdjustment NOTIFY controlsChanged)
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
  Q_PROPERTY(double activeMaskFeather READ activeMaskFeather WRITE setActiveMaskFeather NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskPosition READ activeMaskPosition WRITE setActiveMaskPosition NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRotation READ activeMaskRotation WRITE setActiveMaskRotation NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskCenterX READ activeMaskCenterX WRITE setActiveMaskCenterX NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskCenterY READ activeMaskCenterY WRITE setActiveMaskCenterY NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRadiusX READ activeMaskRadiusX WRITE setActiveMaskRadiusX NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRadiusY READ activeMaskRadiusY WRITE setActiveMaskRadiusY NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBrushSize READ activeMaskBrushSize WRITE setActiveMaskBrushSize NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskBrushFlow READ activeMaskBrushFlow WRITE setActiveMaskBrushFlow NOTIFY controlsChanged)
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
  Q_PROPERTY(QVariantList activeMaskPolygonPoints READ activeMaskPolygonPoints NOTIFY controlsChanged)
  Q_PROPERTY(int activeMaskPolygonPointCount READ activeMaskPolygonPointCount NOTIFY controlsChanged)
  Q_PROPERTY(QVariantList activeMaskOutlinePoints READ activeMaskOutlinePoints NOTIFY controlsChanged)
  Q_PROPERTY(QString activeMaskPreviewSource READ activeMaskPreviewSource NOTIFY controlsChanged)
  Q_PROPERTY(QString maskPreviewMode READ maskPreviewMode WRITE setMaskPreviewMode NOTIFY controlsChanged)
  Q_PROPERTY(bool smartSelectPreviewActive READ smartSelectPreviewActive NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(QString smartSelectPreviewSource READ smartSelectPreviewSource NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(QVariantList smartSelectPreviewOutlinePoints READ smartSelectPreviewOutlinePoints NOTIFY smartSelectPreviewChanged)
  Q_PROPERTY(double activeMaskLuminanceMin READ activeMaskLuminanceMin WRITE setActiveMaskLuminanceMin NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskLuminanceMax READ activeMaskLuminanceMax WRITE setActiveMaskLuminanceMax NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskHueCenter READ activeMaskHueCenter WRITE setActiveMaskHueCenter NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskHueRange READ activeMaskHueRange WRITE setActiveMaskHueRange NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSaturationMin READ activeMaskSaturationMin WRITE setActiveMaskSaturationMin NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskSaturationMax READ activeMaskSaturationMax WRITE setActiveMaskSaturationMax NOTIFY controlsChanged)
  Q_PROPERTY(double activeMaskRangeFeather READ activeMaskRangeFeather WRITE setActiveMaskRangeFeather NOTIFY controlsChanged)
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
  [[nodiscard]] QString referencePreviewSource() const;
  [[nodiscard]] QString displayReferencePreviewSource() const;
  [[nodiscard]] QString displayReferencePreviewOverlaySource() const;
  [[nodiscard]] QRectF displayPreviewOverlayRect() const;
  [[nodiscard]] QVariantList histogramRed() const;
  [[nodiscard]] QVariantList histogramGreen() const;
  [[nodiscard]] QVariantList histogramBlue() const;
  [[nodiscard]] QVariantList histogramLuminance() const;
  [[nodiscard]] int previewTargetEdge() const;
  [[nodiscard]] QString debugInfo() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] QString cameraLabel() const;
  [[nodiscard]] QString captureDate() const;
  [[nodiscard]] QString lensModel() const;
  [[nodiscard]] bool hasAsset() const;
  [[nodiscard]] bool subjectMaskAvailable() const;
  [[nodiscard]] bool processing() const;
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;
  [[nodiscard]] bool hasCopiedSettings() const;
  [[nodiscard]] QStringList presetNames() const;
  [[nodiscard]] bool compareMode() const;
  [[nodiscard]] bool cropEditing() const;
  [[nodiscard]] bool rawControlsAvailable() const;
  [[nodiscard]] double rawExposure() const;
  [[nodiscard]] double rawBoost() const;
  [[nodiscard]] double rawShadowBoost() const;
  [[nodiscard]] double rawDetail() const;
  [[nodiscard]] bool rawHighlightRecovery() const;
  [[nodiscard]] bool rawGamutMapping() const;
  [[nodiscard]] bool rawLensCorrection() const;
  [[nodiscard]] bool rawWhiteBalanceOverride() const;
  [[nodiscard]] double rawWhiteBalanceTemperature() const;
  [[nodiscard]] double rawWhiteBalanceTint() const;
  [[nodiscard]] double exposure() const;
  [[nodiscard]] double contrast() const;
  [[nodiscard]] double highlights() const;
  [[nodiscard]] double shadows() const;
  [[nodiscard]] double whites() const;
  [[nodiscard]] double blacks() const;
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
  [[nodiscard]] double vibrance() const;
  [[nodiscard]] double saturation() const;
  [[nodiscard]] double curveShadows() const;
  [[nodiscard]] double curveDarks() const;
  [[nodiscard]] double curveLights() const;
  [[nodiscard]] double curveHighlights() const;
  [[nodiscard]] QString curveChannel() const;
  [[nodiscard]] QVariantList activeCurvePoints() const;
  [[nodiscard]] QString hslChannel() const;
  [[nodiscard]] double hueAdjustment() const;
  [[nodiscard]] double saturationAdjustment() const;
  [[nodiscard]] double luminanceAdjustment() const;
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
  [[nodiscard]] QVariantList maskSets() const;
  [[nodiscard]] int maskSetCount() const;
  [[nodiscard]] QVariantList retouchStack() const;
  [[nodiscard]] int retouchCount() const;
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
  [[nodiscard]] double activeMaskFeather() const;
  [[nodiscard]] double activeMaskPosition() const;
  [[nodiscard]] double activeMaskRotation() const;
  [[nodiscard]] double activeMaskCenterX() const;
  [[nodiscard]] double activeMaskCenterY() const;
  [[nodiscard]] double activeMaskRadiusX() const;
  [[nodiscard]] double activeMaskRadiusY() const;
  [[nodiscard]] double activeMaskBrushSize() const;
  [[nodiscard]] double activeMaskBrushFlow() const;
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
  [[nodiscard]] QVariantList activeMaskPolygonPoints() const;
  [[nodiscard]] int activeMaskPolygonPointCount() const;
  [[nodiscard]] QVariantList activeMaskOutlinePoints() const;
  [[nodiscard]] QString activeMaskPreviewSource() const;
  [[nodiscard]] QString maskPreviewMode() const;
  [[nodiscard]] bool smartSelectPreviewActive() const;
  [[nodiscard]] QString smartSelectPreviewSource() const;
  [[nodiscard]] QVariantList smartSelectPreviewOutlinePoints() const;
  [[nodiscard]] double activeMaskLuminanceMin() const;
  [[nodiscard]] double activeMaskLuminanceMax() const;
  [[nodiscard]] double activeMaskHueCenter() const;
  [[nodiscard]] double activeMaskHueRange() const;
  [[nodiscard]] double activeMaskSaturationMin() const;
  [[nodiscard]] double activeMaskSaturationMax() const;
  [[nodiscard]] double activeMaskRangeFeather() const;
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
  void setCompareMode(bool compare_mode);
  void setCropEditing(bool crop_editing);
  void setRawExposure(double raw_exposure);
  void setRawBoost(double raw_boost);
  void setRawShadowBoost(double raw_shadow_boost);
  void setRawDetail(double raw_detail);
  void setRawHighlightRecovery(bool enabled);
  void setRawGamutMapping(bool enabled);
  void setRawLensCorrection(bool enabled);
  void setRawWhiteBalanceOverride(bool enabled);
  void setRawWhiteBalanceTemperature(double temperature);
  void setRawWhiteBalanceTint(double tint);
  void setExposure(double exposure);
  void setContrast(double contrast);
  void setHighlights(double highlights);
  void setShadows(double shadows);
  void setWhites(double whites);
  void setBlacks(double blacks);
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
  void setVibrance(double vibrance);
  void setSaturation(double saturation);
  void setCurveShadows(double curve_shadows);
  void setCurveDarks(double curve_darks);
  void setCurveLights(double curve_lights);
  void setCurveHighlights(double curve_highlights);
  void setCurveChannel(const QString& curve_channel);
  void setActiveCurvePoints(const QVariantList& active_curve_points);
  void setHslChannel(const QString& hsl_channel);
  void setHueAdjustment(double hue_adjustment);
  void setSaturationAdjustment(double saturation_adjustment);
  void setLuminanceAdjustment(double luminance_adjustment);
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
  Q_INVOKABLE void setPreviewViewport(
      double normalized_x,
      double normalized_y,
      double normalized_width,
      double normalized_height,
      int patch_max_edge,
      bool active = true);
  void setCropZoom(double crop_zoom);
  void setCropPreset(const QString& crop_preset);
  void setActiveMaskIndex(int index);
  void setMaskPreviewMode(const QString& mode);
  void setActiveMaskName(const QString& name);
  void setActiveMaskOperation(const QString& operation);
  void setActiveMaskEnabled(bool enabled);
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
  void setActiveMaskFeather(double local_feather);
  void setActiveMaskPosition(double local_position);
  void setActiveMaskRotation(double local_rotation);
  void setActiveMaskCenterX(double center_x);
  void setActiveMaskCenterY(double center_y);
  void setActiveMaskRadiusX(double radius_x);
  void setActiveMaskRadiusY(double radius_y);
  void setActiveMaskBrushSize(double brush_size);
  void setActiveMaskBrushFlow(double brush_flow);
  void setActiveMaskCloneScale(double clone_scale);
  void setActiveMaskCloneRotation(double clone_rotation);
  void setActiveMaskLuminanceMin(double min_value);
  void setActiveMaskLuminanceMax(double max_value);
  void setActiveMaskHueCenter(double center);
  void setActiveMaskHueRange(double range);
  void setActiveMaskSaturationMin(double min_value);
  void setActiveMaskSaturationMax(double max_value);
  void setActiveMaskRangeFeather(double feather);
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
  Q_INVOKABLE QString currentRecipeJson() const;
  Q_INVOKABLE QString copiedSettingsRecipeJson() const;
  Q_INVOKABLE QString presetRecipeJson(const QString& name) const;
  Q_INVOKABLE bool copyCurrentSettings();
  Q_INVOKABLE bool pasteCopiedSettings(const QStringList& adjustment_groups = {});
  Q_INVOKABLE bool savePreset(const QString& name, bool overwrite = true);
  Q_INVOKABLE bool applyPreset(const QString& name, const QStringList& adjustment_groups = {});
  Q_INVOKABLE bool deletePreset(const QString& name);
  Q_INVOKABLE void addMask(const QString& kind);
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
  Q_INVOKABLE void deselectActiveMask();
  Q_INVOKABLE void smartSelectObjectAt(double normalized_x, double normalized_y, bool create_new = false);
  Q_INVOKABLE void objectRemoveAt(double normalized_x, double normalized_y, bool create_new = false);
  Q_INVOKABLE void startPolygonSelection();
  Q_INVOKABLE void appendActivePolygonPoint(double normalized_x, double normalized_y);
  Q_INVOKABLE void removeLastActivePolygonPoint();
  Q_INVOKABLE void clearActivePolygonPoints();
  Q_INVOKABLE void startSelectionBrush(bool subtract = false);
  Q_INVOKABLE void startSelectionEdgeBrush(bool subtract = false);
  Q_INVOKABLE void startHealBrush();
  Q_INVOKABLE void startCloneBrush();
  Q_INVOKABLE void startRemoveBrush();
  Q_INVOKABLE void applyActiveRetouch();
  Q_INVOKABLE void cancelActiveRetouch();
  Q_INVOKABLE void duplicateActiveMask();
  Q_INVOKABLE void removeActiveMask();
  Q_INVOKABLE void moveActiveMask(int offset);
  Q_INVOKABLE void beginActiveBrushStroke();
  Q_INVOKABLE void endActiveBrushStroke();
  Q_INVOKABLE void addBrushPoint(double normalized_x, double normalized_y);
  Q_INVOKABLE void clearActiveBrushPoints();
  Q_INVOKABLE void setActiveMaskCloneSource(double normalized_x, double normalized_y);
  Q_INVOKABLE void setWhiteBalanceSampling(bool enabled);
  Q_INVOKABLE void sampleWhiteBalanceAt(double normalized_x, double normalized_y);

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
  void whiteBalanceSamplingChanged();
  void historyChanged();
  void clipboardChanged();
  void presetsChanged();
  void smartSelectPreviewChanged();

 private:
  friend struct EditorPreviewControllerTestAccess;

  void renderPreview(bool schedule_full_resolution = true);
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
  void applyState(const EditState& state);
  [[nodiscard]] EditState currentState() const;
  [[nodiscard]] EditState stateFromRecipeJson(const QString& recipe_json) const;
  [[nodiscard]] QString recipeJsonForState(const EditState& state) const;
  [[nodiscard]] QString normalizedPresetName(const QString& name) const;
  [[nodiscard]] QString presetsFilePath() const;
  [[nodiscard]] int firstMaskIndexForKind(const QString& kind) const;
  [[nodiscard]] QVariantMap activeMaskDefinition() const;
  [[nodiscard]] QVariantMap maskDefinitionAt(int index) const;
  void invalidateActiveMaskCache();
  void invalidateMaskDerivedCaches();
  void synchronizeMaskState();
  bool applyMaskStackChange(const QVariantList& stack, int active_index, bool rerender = true);
  bool updateMaskAt(int index, const QVariantMap& mask, bool rerender = true);
  bool updateActiveMaskDefinition(const QVariantMap& mask, bool rerender = true);
  bool updateLegacyKindMask(const QString& kind, const QVariantMap& updates);
  void loadPresets();
  void savePresets() const;
  void flushPersistedState();
  [[nodiscard]] bool hasVisibleAdjustments() const;
  [[nodiscard]] bool decodeSourceImage(QImage* image, int max_edge) const;
  [[nodiscard]] QSize sourceImageSize() const;
  [[nodiscard]] QImage applyCrop(const QImage& image) const;
  void applyColorAdjustments(QImage* image) const;
  void computeHistogram(const QImage& image);
  void resetSmartSelectPreview();
  void startSmartSelectPreviewRequest();
  void handleSmartSelectPreviewFinished();
  void updateSmartSelectPreviewState(
      const QVariantMap& preview_mask,
      const QVector<int>& instance_ids = {});
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
  QString startManualSelectionFallbackAt(double normalized_x, double normalized_y);
  QString startManualRemoveFallbackAt(double normalized_x, double normalized_y);
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
  bool useBuiltSaveRecipeBinary() const;
  QString builtBinaryPath(const QString& binary_name) const;
  [[nodiscard]] QString withDisplayRevision(const QString& source) const;

  QVariantList histogram_red_;
  QVariantList histogram_green_;
  QVariantList histogram_blue_;
  QVariantList histogram_luminance_;

  QString asset_id_;
  QString file_path_;
  QString fallback_thumbnail_source_;
  QString preview_source_;
  QString display_preview_source_;
  QString display_preview_overlay_source_;
  QString reference_preview_source_;
  QString display_reference_preview_source_;
  QString display_reference_preview_overlay_source_;
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
  QRectF preview_viewport_rect_;
  int preview_viewport_patch_max_edge_ = 0;
  bool preview_viewport_active_ = false;
  bool processing_ = false;
  bool compare_mode_ = false;
  bool crop_editing_ = false;
  double raw_exposure_ = 0.0;
  double raw_boost_ = 0.0;
  double raw_shadow_boost_ = 1.0;
  double raw_detail_ = 0.0;
  bool raw_highlight_recovery_ = true;
  bool raw_gamut_mapping_ = true;
  bool raw_lens_correction_ = true;
  bool raw_white_balance_override_ = false;
  double raw_white_balance_temperature_ = 5200.0;
  double raw_white_balance_tint_ = 0.0;
  double exposure_ = 0.0;
  double contrast_ = 0.0;
  double highlights_ = 0.0;
  double shadows_ = 0.0;
  double whites_ = 0.0;
  double blacks_ = 0.0;
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
  double vibrance_ = 0.0;
  double saturation_ = 0.0;
  double curve_shadows_ = 0.0;
  double curve_darks_ = 0.0;
  double curve_lights_ = 0.0;
  double curve_highlights_ = 0.0;
  QString curve_channel_ = QStringLiteral("Master");
  QVariantList master_curve_points_;
  QVariantList red_curve_points_;
  QVariantList green_curve_points_;
  QVariantList blue_curve_points_;
  QString hsl_channel_ = QStringLiteral("Reds");
  double hue_adjustment_ = 0.0;
  double saturation_adjustment_ = 0.0;
  double luminance_adjustment_ = 0.0;
  struct MixerChannel {
    double hue = 0.0;
    double saturation = 0.0;
    double luminance = 0.0;
  };
  std::array<MixerChannel, 8> mixer_channels_{};
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
  // Stored in normalized form; edit paths should mutate this directly and only
  // renormalize when ingesting external stack payloads.
  QVariantList mask_stack_;
  mutable bool mask_group_cache_dirty_ = true;
  mutable QVariantList cached_mask_sets_;
  mutable QVariantList cached_retouch_stack_;
  mutable bool active_mask_cache_dirty_ = true;
  mutable QVariantMap cached_active_mask_definition_;
  int active_mask_index_ = -1;
  bool brush_stroke_active_ = false;
  bool brush_stroke_suspends_side_effects_ = false;
  bool brush_stroke_needs_final_render_ = false;
  QVector<BrushPreviewPoint> active_brush_live_points_;
  QImage active_brush_live_preview_image_;
  int active_brush_live_preview_revision_ = 0;
  QString mask_preview_mode_ = QStringLiteral("overlay");
  QString smart_select_instance_map_key_;
  QByteArray smart_select_instance_map_data_;
  int smart_select_instance_map_width_ = 0;
  int smart_select_instance_map_height_ = 0;
  QVector<int> smart_select_preview_instance_ids_;
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
  QMap<QString, EditState> presets_;
  bool applying_history_state_ = false;
  QTimer interactive_render_timer_;
  QTimer full_render_timer_;
  QTimer persist_timer_;
  QFutureWatcher<RenderResult> interactive_render_watcher_;
  QFutureWatcher<RenderResult> full_render_watcher_;
  RenderRequest pending_interactive_request_;
  RenderRequest pending_full_request_;
  bool interactive_request_pending_ = false;
  bool full_request_pending_ = false;
  QProcess persist_process_;
  int preview_revision_ = 0;
  int render_generation_ = 0;
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

 public:
  void setImageProvider(EditorPreviewImageSink* image_provider);
};

}  // namespace photo_editor::desktop
