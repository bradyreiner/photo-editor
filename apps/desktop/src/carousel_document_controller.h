#pragma once

#include "customer_edit_access.h"

#include <QByteArray>
#include <QImage>
#include <QFutureWatcher>
#include <QObject>
#include <QPainter>
#include <QPair>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QHash>
#include <QVariantList>
#include <QTimer>
#include <QVariantMap>
#include <QVector>
#include <memory>

#include "edit_recipe_state.h"

class QProcess;
class QThreadPool;

namespace photo_editor::desktop {

class CarouselDocumentController final : public QObject {
  friend struct CarouselPreviewTestAccess;
  Q_OBJECT
  Q_PROPERTY(QVariantList slides READ slides NOTIFY slidesChanged)
  Q_PROPERTY(QVariantList boardSections READ boardSections NOTIFY slidesChanged)
  Q_PROPERTY(QVariantList storyboardEntries READ storyboardEntries NOTIFY slidesChanged)
  Q_PROPERTY(QVariantList repeatedElementLinks READ repeatedElementLinks NOTIFY itemsChanged)
  Q_PROPERTY(QVariantMap selectedRepeatedElementLinkState READ selectedRepeatedElementLinkState NOTIFY selectionChanged)
  Q_PROPERTY(QVariantList linkedPhotoTextGroups READ linkedPhotoTextGroups NOTIFY documentStateChanged)
  Q_PROPERTY(QVariantList canvasItems READ canvasItems NOTIFY itemsChanged)
  Q_PROPERTY(QVariantMap selectionDetails READ selectionDetails NOTIFY selectionChanged)
  Q_PROPERTY(QVariantMap readingOrderState READ readingOrderState NOTIFY readingOrderChanged)
  Q_PROPERTY(int slideCount READ slideCount NOTIFY slidesChanged)
  Q_PROPERTY(int currentIndex READ currentIndex NOTIFY selectionChanged)
  Q_PROPERTY(int selectedSlideCount READ selectedSlideCount NOTIFY selectionChanged)
  Q_PROPERTY(int documentPixelWidth READ documentPixelWidth NOTIFY slidesChanged)
  Q_PROPERTY(int documentPixelHeight READ documentPixelHeight NOTIFY documentFormatChanged)
  Q_PROPERTY(QString documentFormat READ documentFormat WRITE setDocumentFormat NOTIFY documentFormatChanged)
  Q_PROPERTY(bool canAddSlide READ canAddSlide NOTIFY slidesChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QVariantList statusNotices READ statusNotices NOTIFY statusNoticesChanged)
  Q_PROPERTY(bool exportBusy READ exportBusy NOTIFY exportBusyChanged)
  Q_PROPERTY(QString selectedItemId READ selectedItemId NOTIFY selectionChanged)
  Q_PROPERTY(QString documentName READ documentName WRITE setDocumentName NOTIFY documentStateChanged)
  Q_PROPERTY(QString savedDocumentId READ savedDocumentId NOTIFY documentStateChanged)
  Q_PROPERTY(bool hasActiveCarousel READ hasActiveCarousel NOTIFY documentStateChanged)
  Q_PROPERTY(bool documentDirty READ documentDirty NOTIFY documentStateChanged)
  Q_PROPERTY(QString saveState READ saveState NOTIFY documentStateChanged)
  Q_PROPERTY(QString lastSavedAt READ lastSavedAt NOTIFY documentStateChanged)
  Q_PROPERTY(QString saveError READ saveError NOTIFY documentStateChanged)
  Q_PROPERTY(QVariantMap recoveryState READ recoveryState NOTIFY recoveryChanged)
  Q_PROPERTY(QVariantList preflightIssues READ preflightIssues NOTIFY preflightChanged)
  Q_PROPERTY(QVariantList textContrastChecks READ textContrastChecks NOTIFY itemsChanged)
  Q_PROPERTY(QVariantMap preflightIssueState READ preflightIssueState NOTIFY preflightChanged)
  Q_PROPERTY(QVariantMap continuityPreview READ continuityPreview NOTIFY selectionChanged)
  Q_PROPERTY(QVariantMap neighborComparison READ neighborComparison NOTIFY selectionChanged)
  Q_PROPERTY(int preflightBlockingCount READ preflightBlockingCount NOTIFY preflightChanged)
  Q_PROPERTY(int preflightWarningCount READ preflightWarningCount NOTIFY preflightChanged)
  Q_PROPERTY(QVariantList missingMediaEntries READ missingMediaEntries NOTIFY preflightChanged)
  Q_PROPERTY(int missingMediaCount READ missingMediaCount NOTIFY preflightChanged)
  Q_PROPERTY(QVariantList relinkPlan READ relinkPlan NOTIFY relinkChanged)
  Q_PROPERTY(QVariantList missingFontEntries READ missingFontEntries NOTIFY preflightChanged)
  Q_PROPERTY(QStringList installedFontFamilies READ installedFontFamilies CONSTANT)
  Q_PROPERTY(QStringList favoriteFontFamilies READ favoriteFontFamilies NOTIFY fontLibraryChanged)
  Q_PROPERTY(QStringList recentFontFamilies READ recentFontFamilies NOTIFY fontLibraryChanged)
  Q_PROPERTY(QVariantList copyProofIssues READ copyProofIssues NOTIFY copyProofChanged)
  Q_PROPERTY(QString copyProofLanguage READ copyProofLanguage NOTIFY copyProofChanged)
  Q_PROPERTY(QStringList copyProofLanguageOptions READ copyProofLanguageOptions CONSTANT)
  Q_PROPERTY(QStringList customDictionaryWords READ customDictionaryWords NOTIFY copyProofChanged)
  Q_PROPERTY(QVariantList fontSubstitutionPreview READ fontSubstitutionPreview NOTIFY fontRecoveryChanged)
  Q_PROPERTY(QVariantMap exportSummary READ exportSummary NOTIFY exportSettingsChanged)
  Q_PROPERTY(QVariantList exportPresets READ exportPresets NOTIFY exportSettingsChanged)
  Q_PROPERTY(QVariantMap exportPresetState READ exportPresetState NOTIFY exportSettingsChanged)
  Q_PROPERTY(QString exportDirectory READ exportDirectory NOTIFY exportSettingsChanged)
  Q_PROPERTY(QString exportColorSpace READ exportColorSpace NOTIFY exportSettingsChanged)
  Q_PROPERTY(QStringList exportColorSpaceOptions READ exportColorSpaceOptions CONSTANT)
  Q_PROPERTY(QString exportFilenamePattern READ exportFilenamePattern NOTIFY exportSettingsChanged)
  Q_PROPERTY(int exportVersion READ exportVersion NOTIFY exportSettingsChanged)
  Q_PROPERTY(QString exportCollisionPolicy READ exportCollisionPolicy NOTIFY exportSettingsChanged)
  Q_PROPERTY(QStringList exportCollisionPolicyOptions READ exportCollisionPolicyOptions CONSTANT)
  Q_PROPERTY(QString exportBoardScope READ exportBoardScope NOTIFY exportSettingsChanged)
  Q_PROPERTY(QStringList exportBoardScopeOptions READ exportBoardScopeOptions CONSTANT)
  Q_PROPERTY(QVariantMap exportSizeSettings READ exportSizeSettings NOTIFY exportSettingsChanged)
  Q_PROPERTY(QStringList exportSizeModeOptions READ exportSizeModeOptions CONSTANT)
  Q_PROPERTY(QVariantList exportOutputPreview READ exportOutputPreview NOTIFY exportSettingsChanged)
  Q_PROPERTY(QVariantList exportPreflightIssues READ exportPreflightIssues NOTIFY exportSettingsChanged)
  Q_PROPERTY(bool exportManifestEnabled READ exportManifestEnabled NOTIFY exportSettingsChanged)
  Q_PROPERTY(bool exportProofSheetEnabled READ exportProofSheetEnabled NOTIFY exportSettingsChanged)
  Q_PROPERTY(bool exportProofIncludeNotes READ exportProofIncludeNotes NOTIFY exportSettingsChanged)
  Q_PROPERTY(QString exportImageFormat READ exportImageFormat NOTIFY exportSettingsChanged)
  Q_PROPERTY(QStringList exportImageFormatOptions READ exportImageFormatOptions CONSTANT)
  Q_PROPERTY(int exportImageQuality READ exportImageQuality NOTIFY exportSettingsChanged)
  Q_PROPERTY(bool exportPreserveTransparency READ exportPreserveTransparency NOTIFY exportSettingsChanged)
  Q_PROPERTY(QVariantMap exportProgress READ exportProgress NOTIFY exportProgressChanged)
  Q_PROPERTY(QString exportPartialCleanupPolicy READ exportPartialCleanupPolicy NOTIFY exportSettingsChanged)
  Q_PROPERTY(QString cutoutPreviewMode READ cutoutPreviewMode NOTIFY cutoutPreviewChanged)
  Q_PROPERTY(QStringList exportPartialCleanupPolicyOptions READ exportPartialCleanupPolicyOptions CONSTANT)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
  Q_PROPERTY(bool editBoardImageBusy READ editBoardImageBusy NOTIFY editBoardImageBusyChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
  Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
  Q_PROPERTY(QVariantMap clipboardPastePreview READ clipboardPastePreview NOTIFY clipboardChanged)
  Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
  Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
  Q_PROPERTY(QStringList templateOptions READ templateOptions CONSTANT)
  Q_PROPERTY(QVariantList sequenceTemplates READ sequenceTemplates CONSTANT)
  Q_PROPERTY(QVariantList templateLibraryEntries READ templateLibraryEntries NOTIFY designLibraryChanged)
  Q_PROPERTY(QVariantList componentLibraryEntries READ componentLibraryEntries NOTIFY designLibraryChanged)
  Q_PROPERTY(QVariantMap componentInstanceState READ componentInstanceState NOTIFY selectionChanged)
  Q_PROPERTY(QVariantMap designPackageImportPreview READ designPackageImportPreview NOTIFY designPackageImportPreviewChanged)
  Q_PROPERTY(QStringList favoriteTemplateIds READ favoriteTemplateIds NOTIFY designLibraryChanged)
  Q_PROPERTY(QStringList recentTemplateIds READ recentTemplateIds NOTIFY designLibraryChanged)
  Q_PROPERTY(QVariantMap templatePackUpdateState READ templatePackUpdateState NOTIFY documentStateChanged)
  Q_PROPERTY(QVariantMap templatePackUpdatePreview READ templatePackUpdatePreview NOTIFY templatePackUpdatePreviewChanged)
  Q_PROPERTY(QStringList boardRoleOptions READ boardRoleOptions CONSTANT)
  Q_PROPERTY(QVariantList userTemplates READ userTemplates NOTIFY designLibraryChanged)
  Q_PROPERTY(QVariantList brandKits READ brandKits NOTIFY designLibraryChanged)
  Q_PROPERTY(QVariantList brandColorTokens READ brandColorTokens NOTIFY designLibraryChanged)
  Q_PROPERTY(QVariantList brandTypographyTokens READ brandTypographyTokens NOTIFY designLibraryChanged)
  Q_PROPERTY(QVariantList brandThemeVariants READ brandThemeVariants NOTIFY designLibraryChanged)
  Q_PROPERTY(QVariantList textStyles READ textStyles NOTIFY textStylesChanged)
  Q_PROPERTY(QVariantMap textStylePreview READ textStylePreview NOTIFY textStylesChanged)
  Q_PROPERTY(QVariantMap selectionBatchPreview READ selectionBatchPreview NOTIFY selectionBatchPreviewChanged)
  Q_PROPERTY(QVariantMap repeatGridPreview READ repeatGridPreview NOTIFY repeatGridPreviewChanged)
  Q_PROPERTY(QVariantMap documentVariables READ documentVariables NOTIFY documentVariablesChanged)
  Q_PROPERTY(QStringList documentVariableKeys READ documentVariableKeys CONSTANT)
  Q_PROPERTY(QVariantList copySnippets READ copySnippets NOTIFY documentVariablesChanged)
  Q_PROPERTY(bool brandKitEditorActive READ brandKitEditorActive NOTIFY brandKitEditorChanged)
  Q_PROPERTY(QVariantMap brandKitDraft READ brandKitDraft NOTIFY brandKitEditorChanged)
  Q_PROPERTY(QVariantMap brandKitValidation READ brandKitValidation NOTIFY brandKitEditorChanged)
  Q_PROPERTY(QVariantMap brandApplicationPreview READ brandApplicationPreview NOTIFY brandApplicationPreviewChanged)
  Q_PROPERTY(QVariantMap brandLintState READ brandLintState NOTIFY brandLintChanged)
  Q_PROPERTY(QVariantMap layoutSettings READ layoutSettings NOTIFY layoutSettingsChanged)
  Q_PROPERTY(QVariantList layoutRecipes READ layoutRecipes NOTIFY designLibraryChanged)
  Q_PROPERTY(QVariantMap layoutRecipePreview READ layoutRecipePreview NOTIFY layoutRecipePreviewChanged)
  Q_PROPERTY(QVariantMap variantComparisonState READ variantComparisonState NOTIFY variantComparisonChanged)
  Q_PROPERTY(QVariantList savedDesignVersions READ savedDesignVersions NOTIFY variantComparisonChanged)
  Q_PROPERTY(QStringList safeAreaPresetOptions READ safeAreaPresetOptions CONSTANT)
  Q_PROPERTY(QVariantList platformPreviewProfiles READ platformPreviewProfiles CONSTANT)
  Q_PROPERTY(QVariantMap previewDiagnostics READ previewDiagnostics NOTIFY previewDiagnosticsChanged)
  Q_PROPERTY(QString documentContentHash READ documentContentHash NOTIFY documentStateChanged)
  Q_PROPERTY(QVariantList mediaAutofillPlan READ mediaAutofillPlan NOTIFY mediaAutofillPlanChanged)
  Q_PROPERTY(QVariantList photoFramePresets READ photoFramePresets CONSTANT)
  Q_PROPERTY(QStringList fitModeOptions READ fitModeOptions CONSTANT)
  Q_PROPERTY(QStringList slotOptions READ slotOptions CONSTANT)

 public:
  void setEditAccessPolicy(std::function<bool()> policy) { customer_edit_access_.setPolicy(std::move(policy)); }

  explicit CarouselDocumentController(QObject* parent = nullptr);
  ~CarouselDocumentController() override;

  enum class MediaType { Empty, Image, Video };
  enum class FitMode { Cover, Contain };
  enum class SlideTemplate { Single, SplitPair, StackedTriptych };
  enum class GroupKind { None, HorizontalPair };
  enum class ItemKind { Image, Text, Video, Shape, Brush };

  using RecipeState = EditRecipeState;

  struct MediaItem final {
    MediaType type = MediaType::Empty;
    QString file_path;
    QString display_label;
    QString adjustment_patch_json;
    RecipeState recipe_state;
    FitMode fit_mode = FitMode::Cover;
    double scale = 1.0;
    double offset_x = 0.0;
    double offset_y = 0.0;
    double focal_x = 0.5;
    double focal_y = 0.5;
    bool manual_crop = false;
    QString crop_source = QStringLiteral("edit");
    double video_duration_seconds = 0.0;
    double video_trim_in_seconds = 0.0;
    double video_trim_out_seconds = 0.0;
    double video_poster_seconds = 0.0;
    QString video_audio_policy = QStringLiteral("mute");
    bool video_loop_intent = true;
    qint64 source_file_size = 0;
    qint64 source_modified_ms = 0;
    QSize source_pixel_size;
    QString source_fingerprint;
    bool cutout_enabled = false;
    QByteArray cutout_mask_data;
    int cutout_mask_width = 0;
    int cutout_mask_height = 0;
    double cutout_feather = 0.0;
    double cutout_expand = 0.0;
    double cutout_edge_contrast = 0.0;
  };

  struct CanvasItem final {
    QString id;
    QString object_group_id;
    QString component_link_id;
    QString component_definition_id;
    QString component_instance_id;
    QString component_part_id;
    int component_definition_version = 0;
    QJsonObject component_baseline;
    QString constraint_stack_id;
    int constraint_stack_order = -1;
    QString constraint_stack_axis = QStringLiteral("vertical");
    double constraint_stack_padding = 24.0;
    double constraint_stack_gap = 16.0;
    double constraint_stack_anchor_x = 0.0;
    double constraint_stack_anchor_y = 0.0;
    ItemKind kind = ItemKind::Image;
    QString file_path;
    QString external_asset_id;
    QString external_asset_file_name;
    QString external_asset_sha256;
    QString display_label;
    bool hidden = false;
    bool locked = false;
    QString adjustment_patch_json;
    RecipeState recipe_state;
    QString preview_source;
    QSizeF preview_source_size;
    QString text = QStringLiteral("New Text");
    QJsonArray text_spans;
    QString brand_color_kit_id;
    QString brand_color_token_id;
    QString brand_typography_kit_id;
    QString brand_typography_token_id;
    QString text_role = QStringLiteral("body");
    QString text_style_id = QStringLiteral("body");
    QString text_color = QStringLiteral("#1e1a17");
    double text_point_size = 72.0;
    QString text_fit_mode = QStringLiteral("content");
    double text_fit_min_point_size = 18.0;
    double text_fit_max_point_size = 72.0;
    QString text_font_family = QStringLiteral("display");
    QString original_font_family;
    QStringList ignored_preflight_codes;
    QString text_horizontal_alignment = QStringLiteral("center");
    QString text_vertical_alignment = QStringLiteral("middle");
    QString text_weight = QStringLiteral("demibold");
    bool text_italic = false;
    bool text_underline = false;
    bool text_strikethrough = false;
    QString text_case = QStringLiteral("none");
    double text_letter_spacing = 0.0;
    double text_line_height = 1.2;
    double text_paragraph_spacing = 0.0;
    double text_inset = 0.0;
    int text_maximum_line_count = 0;
    QString text_list_style = QStringLiteral("none");
    double text_hanging_indent = 0.0;
    QString text_auto_number_source = QStringLiteral("none");
    QString text_stroke_color = QStringLiteral("#000000");
    double text_stroke_width = 0.0;
    QString text_background_color = QStringLiteral("#ffffff");
    double text_background_opacity = 0.0;
    double text_padding = 0.0;
    double text_background_radius = 0.0;
    QString shape_kind = QStringLiteral("rectangle");
    QVector<QPointF> brush_points;
    QSizeF brush_source_size = QSizeF(1.0, 1.0);
    QString brush_color = QStringLiteral("#1e1a17");
    double brush_width = 24.0;
    QString brush_style = QStringLiteral("marker");
    bool brush_eraser = false;
    QString fill_color = QStringLiteral("#36f2a6");
    QString fill_secondary_color = QStringLiteral("#69cad8");
    QString fill_kind = QStringLiteral("solid");
    QString stroke_color = QStringLiteral("#0d1419");
    double stroke_width = 0.0;
    double corner_radius = 0.0;
    double opacity = 1.0;
    QString blend_mode = QStringLiteral("normal");
    bool flipped_horizontal = false;
    bool flipped_vertical = false;
    QString frame_color = QStringLiteral("#ffffff");
    double frame_width = 0.0;
    QString shadow_color = QStringLiteral("#000000");
    double shadow_opacity = 0.0;
    double shadow_offset_x = 32.0;
    double shadow_offset_y = 32.0;
    double media_scale = 1.0;
    double media_offset_x = 0.0;
    double media_offset_y = 0.0;
    double media_focal_x = 0.5;
    double media_focal_y = 0.5;
    bool media_manual_crop = false;
    QString media_crop_source = QStringLiteral("edit");
    double video_duration_seconds = 0.0;
    double video_trim_in_seconds = 0.0;
    double video_trim_out_seconds = 0.0;
    double video_poster_seconds = 0.0;
    QString video_audio_policy = QStringLiteral("mute");
    bool video_loop_intent = true;
    QString media_mask_shape = QStringLiteral("rectangle");
    QRectF bounds;
    double rotation_degrees = 0.0;
    FitMode fit_mode = FitMode::Cover;
    QSizeF source_size = QSizeF(0.0, 0.0);
    qint64 source_file_size = 0;
    qint64 source_modified_ms = 0;
    QString source_fingerprint;
    bool cutout_enabled = false;
    QByteArray cutout_mask_data;
    int cutout_mask_width = 0;
    int cutout_mask_height = 0;
    double cutout_feather = 0.0;
    double cutout_expand = 0.0;
    double cutout_edge_contrast = 0.0;
  };

  [[nodiscard]] QVariantList slides() const;
  [[nodiscard]] QVariantList boardSections() const;
  [[nodiscard]] QVariantList storyboardEntries() const;
  [[nodiscard]] QVariantList repeatedElementLinks() const;
  [[nodiscard]] QVariantList linkedPhotoTextGroups() const;
  [[nodiscard]] QVariantMap selectedRepeatedElementLinkState() const;
  [[nodiscard]] QVariantList canvasItems() const;
  [[nodiscard]] QVariantMap selectionDetails() const;
  [[nodiscard]] QVariantMap readingOrderState() const;
  [[nodiscard]] int slideCount() const;
  [[nodiscard]] int currentIndex() const;
  [[nodiscard]] int selectedSlideCount() const;
  [[nodiscard]] int documentPixelWidth() const;
  [[nodiscard]] int documentPixelHeight() const;
  [[nodiscard]] QString documentFormat() const;
  [[nodiscard]] bool canAddSlide() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] QVariantList statusNotices() const;
  [[nodiscard]] bool exportBusy() const;
  [[nodiscard]] QString selectedItemId() const;
  [[nodiscard]] QString documentName() const;
  [[nodiscard]] QString savedDocumentId() const;
  [[nodiscard]] bool hasActiveCarousel() const;
  [[nodiscard]] bool documentDirty() const;
  [[nodiscard]] QString saveState() const;
  [[nodiscard]] QString lastSavedAt() const;
  [[nodiscard]] QString saveError() const;
  [[nodiscard]] QVariantMap recoveryState() const;
  [[nodiscard]] QVariantList preflightIssues() const;
  [[nodiscard]] QVariantList textContrastChecks() const;
  [[nodiscard]] QVariantMap preflightIssueState() const;
  [[nodiscard]] QVariantMap continuityPreview() const;
  [[nodiscard]] QVariantMap neighborComparison() const;
  [[nodiscard]] int preflightBlockingCount() const;
  [[nodiscard]] int preflightWarningCount() const;
  [[nodiscard]] QVariantList missingMediaEntries() const;
  [[nodiscard]] int missingMediaCount() const;
  [[nodiscard]] QVariantList relinkPlan() const;
  [[nodiscard]] QVariantList missingFontEntries() const;
  [[nodiscard]] QStringList installedFontFamilies() const;
  [[nodiscard]] QStringList favoriteFontFamilies() const;
  [[nodiscard]] QStringList recentFontFamilies() const;
  [[nodiscard]] QVariantList copyProofIssues() const;
  [[nodiscard]] QString copyProofLanguage() const;
  [[nodiscard]] QStringList copyProofLanguageOptions() const;
  [[nodiscard]] QStringList customDictionaryWords() const;
  [[nodiscard]] QVariantList fontSubstitutionPreview() const;
  [[nodiscard]] QVariantMap exportSummary() const;
  [[nodiscard]] QVariantList exportPresets() const;
  [[nodiscard]] QVariantMap exportPresetState() const;
  [[nodiscard]] QString exportDirectory() const;
  [[nodiscard]] QString exportColorSpace() const;
  [[nodiscard]] QStringList exportColorSpaceOptions() const;
  [[nodiscard]] QString exportFilenamePattern() const;
  [[nodiscard]] int exportVersion() const;
  [[nodiscard]] QString exportCollisionPolicy() const;
  [[nodiscard]] QStringList exportCollisionPolicyOptions() const;
  [[nodiscard]] QString exportBoardScope() const;
  [[nodiscard]] QStringList exportBoardScopeOptions() const;
  [[nodiscard]] QVariantMap exportSizeSettings() const;
  [[nodiscard]] QStringList exportSizeModeOptions() const;
  [[nodiscard]] QVariantList exportOutputPreview() const;
  [[nodiscard]] QVariantList exportPreflightIssues() const;
  [[nodiscard]] bool exportManifestEnabled() const;
  [[nodiscard]] bool exportProofSheetEnabled() const;
  [[nodiscard]] bool exportProofIncludeNotes() const;
  [[nodiscard]] QString exportImageFormat() const;
  [[nodiscard]] QStringList exportImageFormatOptions() const;
  [[nodiscard]] int exportImageQuality() const;
  [[nodiscard]] bool exportPreserveTransparency() const;
  [[nodiscard]] QVariantMap exportProgress() const;
  [[nodiscard]] QString exportPartialCleanupPolicy() const;
  [[nodiscard]] QStringList exportPartialCleanupPolicyOptions() const;
  [[nodiscard]] QString cutoutPreviewMode() const;
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;
  [[nodiscard]] bool canPaste() const;
  [[nodiscard]] QVariantMap clipboardPastePreview() const;
  [[nodiscard]] QString undoText() const;
  [[nodiscard]] QString redoText() const;
  [[nodiscard]] QStringList templateOptions() const;
  [[nodiscard]] QVariantList sequenceTemplates() const;
  [[nodiscard]] QVariantList templateLibraryEntries() const;
  [[nodiscard]] QVariantList componentLibraryEntries() const;
  [[nodiscard]] QVariantMap componentInstanceState() const;
  [[nodiscard]] QVariantMap designPackageImportPreview() const;
  [[nodiscard]] QStringList favoriteTemplateIds() const;
  [[nodiscard]] QStringList recentTemplateIds() const;
  [[nodiscard]] QVariantMap templatePackUpdateState() const;
  [[nodiscard]] QVariantMap templatePackUpdatePreview() const;
  [[nodiscard]] QStringList boardRoleOptions() const;
  [[nodiscard]] QVariantList userTemplates() const;
  [[nodiscard]] QVariantList brandKits() const;
  [[nodiscard]] QVariantList brandColorTokens() const;
  [[nodiscard]] QVariantList brandTypographyTokens() const;
  [[nodiscard]] QVariantList brandThemeVariants() const;
  [[nodiscard]] QVariantList textStyles() const;
  [[nodiscard]] QVariantMap textStylePreview() const;
  [[nodiscard]] QVariantMap selectionBatchPreview() const;
  [[nodiscard]] QVariantMap repeatGridPreview() const;
  [[nodiscard]] QVariantMap documentVariables() const;
  [[nodiscard]] QStringList documentVariableKeys() const;
  [[nodiscard]] QVariantList copySnippets() const;
  [[nodiscard]] bool brandKitEditorActive() const;
  [[nodiscard]] QVariantMap brandKitDraft() const;
  [[nodiscard]] QVariantMap brandKitValidation() const;
  [[nodiscard]] QVariantMap brandApplicationPreview() const;
  [[nodiscard]] QVariantMap brandLintState() const;
  [[nodiscard]] QVariantMap layoutSettings() const;
  [[nodiscard]] QVariantList layoutRecipes() const;
  [[nodiscard]] QVariantMap layoutRecipePreview() const;
  [[nodiscard]] QVariantMap variantComparisonState() const;
  [[nodiscard]] QVariantList savedDesignVersions() const;
  [[nodiscard]] QStringList safeAreaPresetOptions() const;
  [[nodiscard]] QVariantList platformPreviewProfiles() const;
  [[nodiscard]] QVariantMap previewDiagnostics() const;
  [[nodiscard]] QString documentContentHash() const;
  [[nodiscard]] QVariantList mediaAutofillPlan() const;
  Q_INVOKABLE QVariantMap mediaUsageReport(
      const QVariantList& candidate_paths = QVariantList()) const;
  Q_INVOKABLE void focusMediaUsagePlacement(
      int board_index,
      const QString& item_id = QString(),
      const QString& slot = QStringLiteral("primary"));
  [[nodiscard]] QVariantList photoFramePresets() const;
  [[nodiscard]] QStringList fitModeOptions() const;
  [[nodiscard]] QStringList slotOptions() const;

  void setDocumentFormat(const QString& document_format);
  void setDocumentName(const QString& document_name);

  Q_INVOKABLE void selectSlide(int index);
  Q_INVOKABLE bool selectAdjacentSlide(int direction);
  Q_INVOKABLE void toggleSlideSelection(int index);
  Q_INVOKABLE void selectSlideRange(int index);
  Q_INVOKABLE void addSlide();
  Q_INVOKABLE void duplicateCurrentSlide();
  Q_INVOKABLE bool createBoardVariant();
  Q_INVOKABLE void removeCurrentSlide();
  Q_INVOKABLE void moveCurrentBackward();
  Q_INVOKABLE void moveCurrentForward();
  Q_INVOKABLE void selectAdjacentLayer(int direction);
  Q_INVOKABLE void moveSlide(int from_index, int to_index);
  Q_INVOKABLE void duplicateSelectedSlides();
  Q_INVOKABLE void removeSelectedSlides();
  Q_INVOKABLE bool createBoardSection(const QString& name);
  Q_INVOKABLE bool renameBoardSection(const QString& section_id, const QString& name);
  Q_INVOKABLE bool setBoardSectionCollapsed(const QString& section_id, bool collapsed);
  Q_INVOKABLE bool selectBoardSection(const QString& section_id);
  Q_INVOKABLE bool moveBoardSection(const QString& section_id, int direction);
  Q_INVOKABLE bool removeBoardSection(const QString& section_id);
  Q_INVOKABLE bool createRepeatedElementLink(const QString& name, const QString& scope);
  Q_INVOKABLE bool detachSelectedRepeatedElement();
  Q_INVOKABLE bool removeRepeatedElementLink(const QString& link_id);
  Q_INVOKABLE QVariantList photoTextLinkCandidates(const QString& layer_id) const;
  Q_INVOKABLE QVariantMap photoTextLinkState(const QString& layer_id) const;
  Q_INVOKABLE bool linkEditBoardPhotoText(
      const QString& layer_id, const QString& name, const QStringList& board_ids);
  Q_INVOKABLE bool detachEditBoardPhotoText(const QString& layer_id);
  Q_INVOKABLE void setCurrentSlideBackgroundColor(const QString& color_value);
  Q_INVOKABLE void setCurrentSlideBackgroundGradient(
      const QString& gradient_kind,
      const QString& start_color,
      const QString& end_color);
  Q_INVOKABLE void setCurrentSlideName(const QString& board_name);
  Q_INVOKABLE void setCurrentSlideRole(const QString& board_role);
  Q_INVOKABLE void setSelectedSlidesRole(const QString& board_role);
  Q_INVOKABLE void setCurrentSlideNotes(
      const QString& message_note,
      const QString& media_intent_note,
      const QString& source_reference_note);
  Q_INVOKABLE void setCurrentSlideAccessibilityText(
      const QString& alt_text,
      const QString& plain_language_description);
  Q_INVOKABLE bool moveReadingOrderEntry(const QString& entry_key, int direction);
  Q_INVOKABLE bool setReadingOrderEntryMeaningful(
      const QString& entry_key,
      bool meaningful);
  Q_INVOKABLE bool focusReadingOrderEntry(const QString& entry_key);
  Q_INVOKABLE QVariantMap previewBoardRangeCommand(
      const QString& operation,
      const QString& value,
      const QString& scope) const;
  Q_INVOKABLE bool applyBoardRangeCommand(
      const QString& operation,
      const QString& value,
      const QString& scope);
  Q_INVOKABLE QVariantList documentStructureIssues() const;
  Q_INVOKABLE bool focusDocumentStructureIssue(const QString& issue_id);

  Q_INVOKABLE void addPhotoItem(
      const QString& file_path,
      const QString& display_label = QString(),
      const QString& adjustment_patch_json = QString());
  Q_INVOKABLE void addVideoItem(
      const QString& file_path,
      const QString& display_label = QString());
  Q_INVOKABLE void addTextItem(const QString& text = QStringLiteral("New Text"));
  Q_INVOKABLE void addTextItemWithRole(const QString& text_role, const QString& text = QString());
  Q_INVOKABLE void addShapeItem(const QString& shape_kind = QStringLiteral("rectangle"));
  Q_INVOKABLE void addBrushStroke(
      const QVariantList& points,
      const QString& color,
      double width,
      double opacity,
      const QString& style,
      bool eraser);
  Q_INVOKABLE void pickPhotoItem();
  Q_INVOKABLE void pickVideoItem();
  Q_INVOKABLE void selectItem(const QString& item_id);
  Q_INVOKABLE void addItemToSelection(const QString& item_id);
  Q_INVOKABLE void toggleItemSelection(const QString& item_id);
  Q_INVOKABLE void clearSelection();
  Q_INVOKABLE void removeSelectedItem();
  Q_INVOKABLE void duplicateSelectedItem();
  Q_INVOKABLE void bringSelectedForward();
  Q_INVOKABLE void sendSelectedBackward();
  Q_INVOKABLE void bringSelectedToFront();
  Q_INVOKABLE void sendSelectedToBack();
  Q_INVOKABLE void moveSelectedItemByPixels(double delta_x, double delta_y);
  Q_INVOKABLE void setSelectedItemCenter(double center_x, double center_y);
  Q_INVOKABLE void setSelectedItemSize(double width, double height);
  Q_INVOKABLE void setSelectedTextFrameSize(double width, double height);
  Q_INVOKABLE void setSelectedItemRotation(double rotation_degrees);
  Q_INVOKABLE bool applyTransformExpression(
      const QString& field,
      const QString& expression,
      const QString& anchor = QStringLiteral("center"),
      bool aspect_locked = true);
  Q_INVOKABLE void moveSelectedItemByPixelsSilent(double delta_x, double delta_y);
  Q_INVOKABLE void setSelectedItemCenterSilent(double center_x, double center_y);
  Q_INVOKABLE void setSelectedItemSizeSilent(double width, double height);
  Q_INVOKABLE void setSelectedItemRotationSilent(double rotation_degrees);
  Q_INVOKABLE void commitTransformState();
  Q_INVOKABLE void setSelectedText(const QString& text);
  Q_INVOKABLE bool applySelectedTextSpan(
      int start,
      int length,
      const QVariantMap& attributes);
  Q_INVOKABLE bool clearSelectedTextSpan(int start, int length);
  Q_INVOKABLE void setSelectedTextListStyle(const QString& style);
  Q_INVOKABLE void setSelectedTextHangingIndent(double indent);
  Q_INVOKABLE void setSelectedTextAutoNumberSource(const QString& source);
  Q_INVOKABLE void setSelectedTextRole(const QString& text_role);
  Q_INVOKABLE bool createTextStyleFromSelection(const QString& name);
  Q_INVOKABLE bool previewTextStyleApplication(const QString& style_id, const QString& scope);
  Q_INVOKABLE bool previewTextStyleUpdate(const QString& style_id, const QString& scope);
  Q_INVOKABLE bool applyTextStylePreview();
  Q_INVOKABLE void clearTextStylePreview();
  Q_INVOKABLE void setDocumentVariable(const QString& key, const QString& value);
  Q_INVOKABLE bool insertDocumentVariable(const QString& key, int position = -1);
  Q_INVOKABLE bool detachSelectedTextVariables();
  Q_INVOKABLE bool saveSelectedTextAsSnippet(const QString& name);
  Q_INVOKABLE bool insertCopySnippet(const QString& snippet_id, int position = -1);
  Q_INVOKABLE bool deleteCopySnippet(const QString& snippet_id);
  Q_INVOKABLE void setSelectedTextColor(const QString& color_value);
  Q_INVOKABLE void setSelectedTextPointSize(double point_size);
  Q_INVOKABLE void setSelectedTextFitSettings(
      const QString& mode,
      double minimum_point_size,
      double maximum_point_size);
  Q_INVOKABLE void setSelectedTextFontFamily(const QString& font_family);
  Q_INVOKABLE bool applyFontFamilyFromBrowser(const QString& font_family);
  Q_INVOKABLE void toggleFavoriteFontFamily(const QString& font_family);
  Q_INVOKABLE void setCopyProofLanguage(const QString& language);
  Q_INVOKABLE void nextCopyProofIssue();
  Q_INVOKABLE void ignoreCopyProofIssue(const QString& issue_id);
  Q_INVOKABLE void addCopyProofWord(const QString& word);
  Q_INVOKABLE void setSelectedTextHorizontalAlignment(const QString& alignment);
  Q_INVOKABLE void setSelectedTextVerticalAlignment(const QString& alignment);
  Q_INVOKABLE void setSelectedTextWeight(const QString& weight);
  Q_INVOKABLE void setSelectedTextItalic(bool italic);
  Q_INVOKABLE void setSelectedTextUnderline(bool underline);
  Q_INVOKABLE void setSelectedTextStrikethrough(bool strikethrough);
  Q_INVOKABLE void setSelectedTextCase(const QString& text_case);
  Q_INVOKABLE void setSelectedTextLetterSpacing(double letter_spacing);
  Q_INVOKABLE void setSelectedTextLineHeight(double line_height);
  Q_INVOKABLE void setSelectedTextParagraphSpacing(double paragraph_spacing);
  Q_INVOKABLE void setSelectedTextInset(double inset);
  Q_INVOKABLE void setSelectedTextMaximumLineCount(int maximum_line_count);
  Q_INVOKABLE void setSelectedTextStrokeColor(const QString& color_value);
  Q_INVOKABLE void setSelectedTextStrokeWidth(double stroke_width);
  Q_INVOKABLE void setSelectedTextBackgroundColor(const QString& color_value);
  Q_INVOKABLE void setSelectedTextBackgroundOpacity(double opacity);
  Q_INVOKABLE void setSelectedTextPadding(double padding);
  Q_INVOKABLE void setSelectedTextBackgroundRadius(double radius);
  Q_INVOKABLE void setSelectedItemOpacity(double opacity);
  Q_INVOKABLE void setSelectedItemBlendMode(const QString& blend_mode);
  Q_INVOKABLE void setSelectedItemFlippedHorizontal(bool flipped);
  Q_INVOKABLE void setSelectedItemFlippedVertical(bool flipped);
  Q_INVOKABLE void setSelectedItemFrameColor(const QString& color_value);
  Q_INVOKABLE void setSelectedItemFrameWidth(double frame_width);
  Q_INVOKABLE void setSelectedItemShadowColor(const QString& color_value);
  Q_INVOKABLE void setSelectedItemShadowOpacity(double opacity);
  Q_INVOKABLE void setSelectedItemShadowOffset(double offset_x, double offset_y);
  Q_INVOKABLE void setSelectedItemCornerRadius(double corner_radius);
  Q_INVOKABLE void setSelectedItemMediaScale(double scale);
  Q_INVOKABLE void setSelectedItemMediaOffset(double offset_x, double offset_y);
  Q_INVOKABLE void nudgeSelectedItemMediaByPixels(double delta_x, double delta_y);
  Q_INVOKABLE bool selectAdjacentMediaItem(int direction);
  Q_INVOKABLE void setSelectedItemMediaMaskShape(const QString& mask_shape);
  Q_INVOKABLE void resetSelectedItemMediaCrop();
  Q_INVOKABLE bool applyPhotoFramePreset(const QString& preset_id);
  Q_INVOKABLE void setSelectedShapeKind(const QString& shape_kind);
  Q_INVOKABLE void setSelectedShapeFillColor(const QString& color_value);
  Q_INVOKABLE void setSelectedShapeFillKind(const QString& fill_kind);
  Q_INVOKABLE void setSelectedShapeSecondaryFillColor(const QString& color_value);
  Q_INVOKABLE void setSelectedShapeStrokeColor(const QString& color_value);
  Q_INVOKABLE void setSelectedShapeStrokeWidth(double stroke_width);
  Q_INVOKABLE void setSelectedShapeCornerRadius(double corner_radius);
  Q_INVOKABLE void setSelectedBrushColor(const QString& color_value);
  Q_INVOKABLE void setSelectedBrushWidth(double width);
  Q_INVOKABLE void setSelectedBrushStyle(const QString& style);
  Q_INVOKABLE void setSelectedItemHidden(bool hidden);
  Q_INVOKABLE void setSelectedItemLocked(bool locked);
  Q_INVOKABLE void replaceSelectedPhotoItem(
      const QString& file_path,
      const QString& display_label = QString(),
      const QString& adjustment_patch_json = QString());
  Q_INVOKABLE void replaceSelectedVideoItem(
      const QString& file_path,
      const QString& display_label = QString());
  Q_INVOKABLE QVariantMap selectedPhotoEditPayload() const;
  Q_INVOKABLE bool applyAdjustmentPatchToPhotoEditTarget(
      const QString& target_key,
      const QString& adjustment_patch_json);
  Q_INVOKABLE void alignSelectionLeft();
  Q_INVOKABLE void alignSelectionHorizontalCenter();
  Q_INVOKABLE void alignSelectionRight();
  Q_INVOKABLE void alignSelectionTop();
  Q_INVOKABLE void alignSelectionVerticalCenter();
  Q_INVOKABLE void alignSelectionBottom();
  Q_INVOKABLE void distributeSelectionHorizontally();
  Q_INVOKABLE void distributeSelectionVertically();
  Q_INVOKABLE bool previewSelectionBatchOperation(const QString& operation);
  Q_INVOKABLE bool applySelectionBatchOperation();
  Q_INVOKABLE void clearSelectionBatchOperationPreview();
  Q_INVOKABLE bool previewRepeatGrid(
      int rows,
      int columns,
      double horizontal_gap,
      double vertical_gap,
      const QString& ordering = QStringLiteral("row-major"));
  Q_INVOKABLE bool applyRepeatGrid();
  Q_INVOKABLE void clearRepeatGridPreview();
  Q_INVOKABLE void groupSelectedItems();
  Q_INVOKABLE void ungroupSelectedItems();
  Q_INVOKABLE bool createConstraintStack(
      const QString& axis,
      double padding = 24.0,
      double gap = 16.0);
  Q_INVOKABLE bool updateSelectedConstraintStack(
      const QString& axis,
      double padding,
      double gap);
  Q_INVOKABLE bool reflowSelectedConstraintStack();
  Q_INVOKABLE bool detachSelectedConstraintStack();

  Q_INVOKABLE void applyTemplateToCurrent(const QString& template_kind);
  Q_INVOKABLE void setActiveSlot(const QString& slot_name);
  Q_INVOKABLE void setCurrentFitMode(const QString& fit_mode);
  Q_INVOKABLE void setCurrentScale(double scale);
  Q_INVOKABLE void setCurrentOffsetX(double offset_x);
  Q_INVOKABLE void setCurrentOffsetY(double offset_y);
  Q_INVOKABLE void setCurrentMediaFocalPoint(double focal_x, double focal_y);
  Q_INVOKABLE void setCurrentMediaCropMode(const QString& crop_mode);
  Q_INVOKABLE void setCurrentMediaCropSource(const QString& crop_source);
  Q_INVOKABLE bool startSelectedMediaBackgroundRemoval();
  Q_INVOKABLE void useSelectedMediaOriginal();
  Q_INVOKABLE void restoreSelectedMediaCutout();
  Q_INVOKABLE void setSelectedMediaCutoutRefinement(
      double feather,
      double expand,
      double edge_contrast);
  Q_INVOKABLE void setCutoutPreviewMode(const QString& mode);
  Q_INVOKABLE void setCurrentSlideBackgroundTransparent();
  Q_INVOKABLE void setCurrentVideoTrim(double in_seconds, double out_seconds);
  Q_INVOKABLE void setCurrentVideoPosterFrame(double seconds);
  Q_INVOKABLE void setCurrentVideoAudioPolicy(const QString& policy);
  Q_INVOKABLE void setCurrentVideoLoopIntent(bool loop);
  Q_INVOKABLE void moveCurrentMediaByPixels(double delta_x, double delta_y);
  Q_INVOKABLE void clearCurrentSlot();
  Q_INVOKABLE void clearCurrentGroup();
  Q_INVOKABLE void assignPrimaryImage(
      const QString& file_path,
      const QString& display_label = QString(),
      const QString& adjustment_patch_json = QString());
  Q_INVOKABLE void assignSecondaryImage(
      const QString& file_path,
      const QString& display_label = QString(),
      const QString& adjustment_patch_json = QString());
  Q_INVOKABLE void assignTertiaryImage(
      const QString& file_path,
      const QString& display_label = QString(),
      const QString& adjustment_patch_json = QString());
  Q_INVOKABLE void assignPrimaryVideo(
      const QString& file_path,
      const QString& display_label = QString());
  Q_INVOKABLE bool dropMediaOnTarget(
      const QString& file_path,
      const QString& media_type,
      int board_index,
      const QString& target_kind,
      const QString& slot_name = QStringLiteral("primary"),
      double normalized_x = 0.5,
      double normalized_y = 0.5,
      const QString& display_label = QString(),
      const QString& adjustment_patch_json = QString());
  Q_INVOKABLE void pickPrimaryImage();
  Q_INVOKABLE void pickSecondaryImage();
  Q_INVOKABLE void pickPrimaryVideo();
  Q_INVOKABLE void exportCarousel();
  Q_INVOKABLE bool exportCarouselToDirectory(const QString& directory_path);
  Q_INVOKABLE void focusPreflightIssue(int board_index, const QString& item_id = QString());
  Q_INVOKABLE void focusPreflightIssueById(const QString& issue_id);
  Q_INVOKABLE void setPreflightSeverityFilter(const QString& severity);
  Q_INVOKABLE void navigatePreflightIssue(int direction);
  Q_INVOKABLE bool ignorePreflightIssue(const QString& issue_id, const QString& reason);
  Q_INVOKABLE bool resolveTextPreflightIssue(
      const QString& item_id,
      const QString& issue_code,
      const QString& action);
  Q_INVOKABLE QString chooseRelinkDirectory();
  Q_INVOKABLE bool scanRelinkDirectory(const QString& directory_path);
  Q_INVOKABLE bool applyRelinkPlan();
  Q_INVOKABLE void clearRelinkPlan();
  Q_INVOKABLE bool previewFontSubstitution(
      const QString& missing_family,
      const QString& replacement_family);
  Q_INVOKABLE bool applyFontSubstitution();
  Q_INVOKABLE void clearFontSubstitutionPreview();
  Q_INVOKABLE void openFontManagement();
  Q_INVOKABLE void dismissStatusNotice(int notice_id);
  Q_INVOKABLE void clearStatusNotices();
  Q_INVOKABLE QString chooseExportDirectory();
  Q_INVOKABLE void setExportDirectory(const QString& directory_path);
  Q_INVOKABLE void setExportColorSpace(const QString& color_space);
  Q_INVOKABLE void setExportFilenamePattern(const QString& pattern);
  Q_INVOKABLE void setExportVersion(int version);
  Q_INVOKABLE void setExportCollisionPolicy(const QString& policy);
  Q_INVOKABLE void setExportBoardScope(const QString& scope);
  Q_INVOKABLE void setExportSizeMode(const QString& mode);
  Q_INVOKABLE void setExportCustomPixelSize(int width, int height);
  Q_INVOKABLE void setExportAspectLocked(bool locked);
  Q_INVOKABLE void setExportManifestEnabled(bool enabled);
  Q_INVOKABLE void setExportProofSheetEnabled(bool enabled);
  Q_INVOKABLE void setExportProofIncludeNotes(bool include_notes);
  Q_INVOKABLE void setExportImageFormat(const QString& format);
  Q_INVOKABLE void setExportImageQuality(int quality);
  Q_INVOKABLE void setExportPreserveTransparency(bool preserve);
  Q_INVOKABLE void setExportPartialCleanupPolicy(const QString& policy);
  Q_INVOKABLE bool applyExportPreset(
      const QString& preset_id,
      bool preserve_user_overrides = true);
  Q_INVOKABLE bool resetExportPresetOverrides();
  Q_INVOKABLE bool exportConfiguredCarousel();
  Q_INVOKABLE bool startConfiguredExport();
  Q_INVOKABLE void cancelExport();
  Q_INVOKABLE bool retryFailedExport();
  Q_INVOKABLE void revealExportOutput();
  Q_INVOKABLE void cleanupPartialExport();
  Q_INVOKABLE QVariantList recentDocuments() const;
  Q_INVOKABLE bool saveDocument(const QString& document_name = QString());
  Q_INVOKABLE bool loadDocument(const QString& document_id);
  Q_INVOKABLE bool restoreRecovery();
  Q_INVOKABLE bool openRecoveryCopy();
  Q_INVOKABLE void discardRecovery();
  Q_INVOKABLE bool openCurrentSavedVersion();
  Q_INVOKABLE void closeCarousel();
  Q_INVOKABLE void createCarouselWithSettings(const QVariantMap& settings);
  Q_INVOKABLE bool createBoardsFromEdit(const QString& file_path, const QString& display_name,
      const QString& adjustment_patch_json, int pixel_width, int pixel_height, int initial_board_count = 2);
  Q_INVOKABLE bool createEditCanvas(const QVariantMap& settings);
  Q_INVOKABLE bool removeEditBoard();
  Q_INVOKABLE bool addEditPhotoBoard(const QString& file_path = QString());
  Q_INVOKABLE bool replaceEditBoardImage(const QString& file_path);
  Q_INVOKABLE bool canUndoEditBoardImageReplacement() const;
  Q_INVOKABLE bool undoEditBoardImageReplacement(const QString& baseline_patch);
  bool editBoardImageBusy() const { return edit_board_image_busy_; }
  Q_INVOKABLE bool prepareEditBoards();
  Q_INVOKABLE QVariantMap editBoardPayload(int index);
  Q_INVOKABLE bool addEditBoard(bool extend_current = false);
  Q_INVOKABLE bool moveEditBoard(int from_index, int insertion_index);
  Q_INVOKABLE bool canUndoEditBoardMove() const;
  Q_INVOKABLE bool undoEditBoardMove();
  Q_INVOKABLE void commitMediaTransformState();
  Q_INVOKABLE void beginHistoryTransaction(const QString& label);
  Q_INVOKABLE void commitHistoryTransaction();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void copySelectedItems();
  Q_INVOKABLE bool copySelectedBoards();
  Q_INVOKABLE void cutSelectedItems();
  Q_INVOKABLE void pasteItems();
  Q_INVOKABLE bool previewClipboardPaste();
  Q_INVOKABLE bool applyClipboardPaste(
      const QString& style_policy = QStringLiteral("preserve"),
      const QString& brand_id = QString());
  Q_INVOKABLE void clearClipboardPastePreview();
  Q_INVOKABLE bool previewMediaAutofill(const QVariantList& file_paths);
  Q_INVOKABLE void moveMediaAutofillEntry(int from_index, int to_index);
  Q_INVOKABLE void setMediaAutofillEntrySkipped(int index, bool skipped);
  Q_INVOKABLE bool applyMediaAutofillPlan();
  Q_INVOKABLE void clearMediaAutofillPlan();
  Q_INVOKABLE void applyDesignPreset(const QString& preset);
  Q_INVOKABLE bool applySequenceTemplate(const QString& template_id);
  Q_INVOKABLE bool applyTemplateLibraryEntry(const QString& template_id);
  Q_INVOKABLE void toggleFavoriteTemplate(const QString& template_id);
  Q_INVOKABLE void clearRecentTemplates();
  Q_INVOKABLE bool previewTemplatePackUpdate();
  Q_INVOKABLE bool applyPreviewedTemplatePackUpdate();
  Q_INVOKABLE void clearTemplatePackUpdatePreview();
  Q_INVOKABLE bool saveCurrentBoardAsTemplate(const QString& name, const QString& category = QStringLiteral("My Templates"));
  Q_INVOKABLE bool saveSelectionAsTemplate(const QString& name, const QString& category = QStringLiteral("Components"));
  Q_INVOKABLE bool applyBuiltInComponent(const QString& component_id);
  Q_INVOKABLE void applyUserTemplate(const QString& template_id, bool replace_existing = false);
  Q_INVOKABLE bool updateSelectedComponentDefinition();
  Q_INVOKABLE bool applySelectedComponentUpdate();
  Q_INVOKABLE bool detachSelectedComponentInstance();
  Q_INVOKABLE bool exportDesignPackage(
      const QString& artifact_type,
      const QString& artifact_id,
      const QString& file_path);
  Q_INVOKABLE bool exportDesignPackageInteractive(
      const QString& artifact_type,
      const QString& artifact_id);
  Q_INVOKABLE bool previewDesignPackageImport(const QString& file_path);
  Q_INVOKABLE bool chooseDesignPackageImport();
  Q_INVOKABLE bool applyDesignPackageImport(const QString& conflict_resolution);
  Q_INVOKABLE void clearDesignPackageImportPreview();
  Q_INVOKABLE void renameUserTemplate(const QString& template_id, const QString& name);
  Q_INVOKABLE void deleteUserTemplate(const QString& template_id);
  Q_INVOKABLE bool saveCurrentBoardAsBrandKit(const QString& name);
  Q_INVOKABLE void beginBrandKitEditor(const QString& brand_id = QString());
  Q_INVOKABLE void setBrandKitDraftField(const QString& field, const QVariant& value);
  Q_INVOKABLE bool saveBrandKitDraft();
  Q_INVOKABLE void cancelBrandKitEditor();
  Q_INVOKABLE bool previewBrandKitApplication(
      const QString& brand_id,
      const QString& scope = QStringLiteral("current"),
      const QString& override_policy = QStringLiteral("retain"));
  Q_INVOKABLE bool applyPreviewedBrandKit();
  Q_INVOKABLE void clearBrandApplicationPreview();
  Q_INVOKABLE void setActiveBrandKitForLint(const QString& brand_id);
  Q_INVOKABLE void focusBrandLintIssue(const QString& issue_id);
  Q_INVOKABLE void ignoreBrandLintIssue(const QString& issue_id);
  Q_INVOKABLE void applyBrandKit(const QString& brand_id, const QString& scope = QStringLiteral("current"));
  Q_INVOKABLE void deleteBrandKit(const QString& brand_id);
  Q_INVOKABLE bool bindSelectedLayerToBrandColorToken(const QString& binding_id);
  Q_INVOKABLE bool bindSelectedTextToBrandTypographyToken(const QString& binding_id);
  Q_INVOKABLE bool detachSelectedBrandColorToken();
  Q_INVOKABLE bool detachSelectedBrandTypographyToken();
  Q_INVOKABLE bool applyBrandThemeVariant(
      const QString& brand_id,
      const QString& variant_id,
      const QString& scope = QStringLiteral("current"));
  Q_INVOKABLE void setLayoutSetting(const QString& key, const QVariant& value);
  Q_INVOKABLE void setCurrentSlideLayoutTokens(
      int columns,
      int rows,
      double margin_x,
      double margin_y,
      double gutter_x,
      double gutter_y);
  Q_INVOKABLE void applyLayoutPreset(const QString& preset);
  Q_INVOKABLE void applySafeAreaPreset(const QString& preset);
  Q_INVOKABLE void resetLayoutSettings();
  Q_INVOKABLE bool saveCurrentLayoutRecipe(const QString& name);
  Q_INVOKABLE bool previewLayoutRecipe(const QString& recipe_id);
  Q_INVOKABLE bool applyPreviewedLayoutRecipe();
  Q_INVOKABLE void clearLayoutRecipePreview();
  Q_INVOKABLE bool renameLayoutRecipe(const QString& recipe_id, const QString& name);
  Q_INVOKABLE bool deleteLayoutRecipe(const QString& recipe_id);
  Q_INVOKABLE bool captureVariantComparisonBaseline(const QString& name = QString());
  Q_INVOKABLE bool refreshVariantComparison();
  Q_INVOKABLE void setVariantComparisonBoardIndex(int board_index);
  Q_INVOKABLE bool resolveVariantComparison(const QString& action);
  Q_INVOKABLE bool restoreSavedDesignVersion(const QString& version_id);
  Q_INVOKABLE bool deleteSavedDesignVersion(const QString& version_id);
  Q_INVOKABLE void addLayoutGuide(const QString& orientation, double position);
  Q_INVOKABLE void moveLayoutGuide(int guide_id, double position);
  Q_INVOKABLE void removeLayoutGuide(int guide_id);
  Q_INVOKABLE void clearLayoutGuides();

 signals:
  void slidesChanged();
  void itemsChanged();
  void selectionChanged();
  void documentFormatChanged();
  void statusChanged();
  void statusNoticesChanged();
  void exportBusyChanged();
  void exportProgressChanged();
  void recoveryChanged();
  void relinkChanged();
  void fontRecoveryChanged();
  void fontLibraryChanged();
  void copyProofChanged();
  void documentStateChanged();
  void preflightChanged();
  void exportSettingsChanged();
  void cutoutPreviewChanged();
  void historyChanged();
  void editBoardImageBusyChanged();
  void editBoardImageReplaced(bool success, const QString& error);
  void clipboardChanged();
  void designLibraryChanged();
  void templatePackUpdatePreviewChanged();
  void designPackageImportPreviewChanged();
  void textStylesChanged();
  void selectionBatchPreviewChanged();
  void repeatGridPreviewChanged();
  void documentVariablesChanged();
  void brandKitEditorChanged();
  void brandApplicationPreviewChanged();
  void brandLintChanged();
  void layoutSettingsChanged();
  void layoutRecipePreviewChanged();
  void variantComparisonChanged();
  void previewDiagnosticsChanged();
  void mediaAutofillPlanChanged();
  void readingOrderChanged();

 private:
  friend struct CarouselHistoryTestAccess;
  struct PreviewFiles;
  struct PreviewResult;
  static QThreadPool& previewThreadPool();
  void queuePreviewRefresh(const QVector<int>& boards, const QSet<QString>& items);
  void startPendingPreviewRefresh();
  void finishPreviewRefresh();
  void invalidatePreviewRefresh(bool reset_document);
  struct ExportSnapshot;
  struct ExportTaskState;
  explicit CarouselDocumentController(const ExportSnapshot& snapshot);
  [[nodiscard]] std::shared_ptr<const ExportSnapshot> captureExportSnapshot() const;
  [[nodiscard]] QVariantMap runExportJob();
  [[nodiscard]] bool exportCancellationRequested() const;
  [[nodiscard]] bool waitForExportProcess(QProcess* process, int timeout_ms) const;
  void invalidateExportForDocumentChange();
  CustomerEditAccess customer_edit_access_;

  struct Slide final {
    QString edit_board_id;
    bool edit_board_joined_previous = false;
    QString edit_board_previous_id;
    SlideTemplate template_kind = SlideTemplate::Single;
    MediaItem primary;
    MediaItem secondary;
    MediaItem tertiary;
    QString linked_group_id;
    QString section_id;
    QString background_color = QStringLiteral("#fbf7f1");
    QString background_secondary_color = QStringLiteral("#ffffff");
    QString background_kind = QStringLiteral("solid");
    QString brand_theme_kit_id;
    QString brand_theme_variant_id;
    QString board_name;
    QString board_role = QStringLiteral("content");
    QString message_note;
    QString media_intent_note;
    QString source_reference_note;
    QString alt_text;
    QString plain_language_description;
    QStringList accessibility_reading_order;
    QStringList decorative_media_keys;
    QString preview_source;
    int layout_columns = 4;
    int layout_rows = 4;
    double layout_margin_x = 72.0;
    double layout_margin_y = 72.0;
    double layout_gutter_x = 24.0;
    double layout_gutter_y = 24.0;
  };

  struct LinkedGroup final {
    QString id;
    GroupKind kind = GroupKind::None;
    bool edit_spread = false;
    int start_index = 0;
    int span = 0;
    MediaItem media;
  };

  struct BoardSection final {
    QString id;
    QString name;
    bool collapsed = false;
  };

  struct RepeatedElementLink final {
    QString id;
    QString name;
  };

  struct PhotoTextLink final {
    QString id;
    QString name;
    QString layer_id;
    QStringList board_ids;
  };

  struct DocumentSnapshot final {
    QJsonObject metadata;
    QJsonObject passthrough;
    QVector<Slide> slides;
    QVector<LinkedGroup> groups;
    QVector<CanvasItem> items;
    QVector<int> selected_slide_indices;
  };
  [[nodiscard]] std::shared_ptr<const DocumentSnapshot> captureDocumentSnapshot() const;
  [[nodiscard]] static QJsonObject snapshotDocumentObject(const DocumentSnapshot& snapshot);
  [[nodiscard]] static bool documentSnapshotsEqual(
      const DocumentSnapshot& left, const DocumentSnapshot& right,
      bool persistent = false, const QJsonObject* serialized_left = nullptr);
  [[nodiscard]] static bool sameSerializedInputs(const MediaItem& left, const MediaItem& right);
  [[nodiscard]] static bool sameSerializedInputs(const Slide& left, const Slide& right);
  [[nodiscard]] static bool sameSerializedInputs(const LinkedGroup& left, const LinkedGroup& right);
  [[nodiscard]] static bool sameSerializedInputs(const CanvasItem& left, const CanvasItem& right);
  [[nodiscard]] QJsonObject documentMetadataObject() const;
  [[nodiscard]] static QJsonObject serializeDocument(
      QJsonObject metadata, const QJsonObject& passthrough, const QVector<Slide>& slides,
      const QVector<LinkedGroup>& groups, const QVector<CanvasItem>& items,
      bool resolve_identity);

  struct AutosaveRequest final {
    std::shared_ptr<const DocumentSnapshot> snapshot;
    QString path;
    QString document_id;
    QString document_name;
    QString captured_at;
    quint64 generation = 0;
    quint64 revision = 0;
  };
  static QThreadPool& autosaveThreadPool();
  [[nodiscard]] static bool persistAutosave(const AutosaveRequest& request);
  void startAutosave(AutosaveRequest request);
  void finishAutosave();
  void settleAutosaves();

  struct HistoryEntry final {
    std::shared_ptr<const DocumentSnapshot> document;
    QString label;
  };

  [[nodiscard]] Slide* slideAt(int index);
  [[nodiscard]] const Slide* slideAt(int index) const;
  [[nodiscard]] LinkedGroup* groupForIndex(int index);
  [[nodiscard]] const LinkedGroup* groupForIndex(int index) const;
  [[nodiscard]] LinkedGroup* groupById(const QString& group_id);
  [[nodiscard]] const LinkedGroup* groupById(const QString& group_id) const;
  [[nodiscard]] BoardSection* boardSectionById(const QString& section_id);
  [[nodiscard]] const BoardSection* boardSectionById(const QString& section_id) const;
  [[nodiscard]] QPair<int, int> boardSectionRange(const QString& section_id) const;
  [[nodiscard]] RepeatedElementLink* repeatedElementLinkById(const QString& link_id);
  [[nodiscard]] const RepeatedElementLink* repeatedElementLinkById(const QString& link_id) const;
  [[nodiscard]] QString repeatedElementLinkIdForComponent(const QString& component_link_id) const;
  [[nodiscard]] CanvasItem* currentItem();
  [[nodiscard]] const CanvasItem* currentItem() const;
  [[nodiscard]] MediaItem* currentEditableMedia();
  [[nodiscard]] const MediaItem* currentEditableMedia() const;
  [[nodiscard]] QString currentCutoutTargetKey() const;
  [[nodiscard]] MediaItem* mediaForCutoutTarget(const QString& target_key);
  void finishBackgroundRemoval();
  [[nodiscard]] int indexOfItem(const QString& item_id) const;
  [[nodiscard]] int currentSelectionSlideIndex() const;
  [[nodiscard]] QRectF slideRectInDocument(int index) const;
  [[nodiscard]] int slideIndexForX(double x) const;
  [[nodiscard]] int slideIndexForItem(const CanvasItem& item) const;
  [[nodiscard]] QSize exportPixelSize() const;
  [[nodiscard]] QSize configuredExportPixelSize() const;
  [[nodiscard]] QString previewsRoot() const;
  [[nodiscard]] QString previewPathForSlide(int index) const;
  [[nodiscard]] QString previewPathForItem(const QString& item_id) const;
  [[nodiscard]] QString versionedLocalFile(const QString& file_path, int revision) const;
  [[nodiscard]] QString fileUrlForPath(const QString& file_path) const;
  [[nodiscard]] RecipeState recipeStateFromAdjustmentPayload(const QString& adjustment_patch_json) const;
  bool applyAdjustmentPatchToPhotoEditTargetInternal(
      const QString& target_key,
      const QString& adjustment_patch_json);
  [[nodiscard]] bool isImageFile(const QString& file_path) const;
  [[nodiscard]] bool isVideoFile(const QString& file_path) const;
  [[nodiscard]] QString imageFileFilter() const;
  [[nodiscard]] QString videoFileFilter() const;
  [[nodiscard]] QString displayNameForPath(const QString& file_path, const QString& fallback) const;
  [[nodiscard]] QVariantMap slideMapForIndex(int index) const;
  [[nodiscard]] QVariantMap itemMap(const CanvasItem& item) const;
  [[nodiscard]] QJsonObject componentBaselineForItem(const CanvasItem& item) const;
  [[nodiscard]] bool componentItemHasLocalOverrides(const CanvasItem& item) const;
  [[nodiscard]] QVariantMap selectionMap() const;
  [[nodiscard]] QString templateNameForIndex(int index) const;
  [[nodiscard]] static QString fitModeName(FitMode fit_mode);
  [[nodiscard]] FitMode fitModeFromName(const QString& fit_mode) const;
  [[nodiscard]] static QString groupKindName(GroupKind kind);
  [[nodiscard]] int spanForGroupKind(GroupKind kind) const;
  [[nodiscard]] GroupKind groupKindFromTemplate(const QString& template_kind) const;
  [[nodiscard]] bool currentSelectionIsLinked() const;
  [[nodiscard]] QString currentActiveSlot() const;
  [[nodiscard]] QSize currentMediaViewportSize() const;
  [[nodiscard]] QSize effectiveMediaSourceSize(const MediaItem& media) const;
  [[nodiscard]] qreal normalizedAspectRatio(const CanvasItem& item) const;
  [[nodiscard]] qreal measuredTextHeight(const CanvasItem& item) const;
  void updateTextFit(CanvasItem* item, bool allow_content_frame_resize = true);
  [[nodiscard]] QString textFitState(const CanvasItem& item) const;
  [[nodiscard]] QVariantList defaultTextStyles() const;
  [[nodiscard]] QVariantMap textStyleById(const QString& style_id) const;
  [[nodiscard]] QVector<int> boardIndexesForScope(const QString& scope) const;
  [[nodiscard]] QVariantMap textStyleFromItem(
      const CanvasItem& item,
      const QString& style_id,
      const QString& name) const;
  [[nodiscard]] QVariantMap buildTextStylePreview(
      const QVariantMap& style,
      const QString& scope,
      const QString& mode) const;
  [[nodiscard]] QVariantMap defaultDocumentVariables() const;
  [[nodiscard]] QString resolvedVariableTextForItem(const CanvasItem& item) const;
  [[nodiscard]] QString resolvedTextForItem(const CanvasItem& item) const;
  [[nodiscard]] int stepNumberForItem(const CanvasItem& item) const;
  [[nodiscard]] QVariantMap textLayoutPresentation(
      const CanvasItem& item,
      const QString& resolved_text) const;
  [[nodiscard]] QVariantList textLayoutLines(const CanvasItem& item) const;
  [[nodiscard]] bool textVerticallyOverflows(const CanvasItem& item) const;
  [[nodiscard]] QSize probeVideoSize(const QString& file_path) const;
  [[nodiscard]] double probeVideoDuration(const QString& file_path) const;
  [[nodiscard]] bool hasFreeformItemsOnSlide(int index) const;
  [[nodiscard]] bool exportVideoSlide(
      const MediaItem& media,
      const QString& output_path,
      const QSize& target_size,
      int slide_index) const;
  [[nodiscard]] bool runHelperCommand(
      const QStringList& command_arguments,
      QString* output = nullptr,
      QString* error_message = nullptr) const;
  [[nodiscard]] static QJsonObject mediaObject(const MediaItem& media, bool resolve_identity = true);
  [[nodiscard]] MediaItem mediaFromObject(const QJsonObject& object) const;
  [[nodiscard]] static QJsonObject slideObject(const Slide& slide, bool resolve_identity = true);
  [[nodiscard]] Slide slideFromObject(const QJsonObject& object) const;
  [[nodiscard]] static QJsonObject groupObject(const LinkedGroup& group, bool resolve_identity = true);
  [[nodiscard]] LinkedGroup groupFromObject(const QJsonObject& object) const;
  [[nodiscard]] static QJsonObject itemObject(const CanvasItem& item, bool resolve_identity = true);
  [[nodiscard]] CanvasItem itemFromObject(const QJsonObject& object) const;
  [[nodiscard]] QVariantList readingOrderEntriesForSlide(
      int slide_index,
      bool meaningful_only = false) const;
  [[nodiscard]] QJsonObject carouselClipboardPayload() const;
  [[nodiscard]] QVariantMap buildClipboardPastePreview(const QJsonObject& payload) const;
  void writeCarouselClipboardPayload(const QJsonObject& payload);
  [[nodiscard]] QJsonObject documentObject() const;
  [[nodiscard]] QJsonObject persistentDocumentObject() const;
  bool loadDocumentObject(QJsonObject object);
  [[nodiscard]] QImage renderSlideBaseImage(int index, const QSize& size, bool preview) const;
  [[nodiscard]] QImage renderSlideOverlayImage(int index, const QSize& size) const;
  [[nodiscard]] QImage renderSlideImage(int index, const QSize& size, bool preview) const;
  [[nodiscard]] QString outputColorSpaceForSlide(int index) const;
  [[nodiscard]] QString exportBaseNameForSlide(int index) const;
  [[nodiscard]] QVector<int> exportBoardIndexes() const;
  [[nodiscard]] QVariantMap exportPresetById(const QString& preset_id) const;
  [[nodiscard]] QVariantMap currentExportPresetControlledSettings() const;
  [[nodiscard]] QVariantMap normalizedExportPresetSettings(const QVariantMap& settings) const;
  void applyExportPresetSettings(const QVariantMap& settings);
  [[nodiscard]] bool exportBoardToPlan(
      int index,
      const QVariantMap& output_plan,
      const QSize& size,
      QString* failure_reason);
  [[nodiscard]] bool writeDeliveryManifest(
      const QVariantList& output_preview,
      const QVariantList& preflight_issues,
      const QSize& size,
      QString* failure_reason);
  [[nodiscard]] bool writeProofContactSheet(
      const QVariantList& output_preview,
      const QSize& size,
      QString* failure_reason);
  [[nodiscard]] QImage renderVideoPlaceholder(const MediaItem& media, const QSize& size) const;
  [[nodiscard]] bool decodeEditedImage(
      const MediaItem& media,
      QImage* image,
      bool preview,
      int max_edge = 0) const;
  [[nodiscard]] bool decodeEditedImage(
      const CanvasItem& item,
      QImage* image,
      bool preview,
      int max_edge = 0) const;
  void renderMediaIntoRect(
      QPainter* painter,
      const QRect& destination_rect,
      const MediaItem& media,
      const QSize& virtual_canvas_size,
      const QPoint& segment_origin,
      bool preview) const;
  void renderItemIntoSlide(
      QPainter* painter,
      const CanvasItem& item,
      int slide_index,
      const QSize& slide_size,
      bool preview) const;
  void renderPhotoItem(
      QPainter* painter,
      const CanvasItem& item,
      const QRectF& frame_rect,
      bool preview) const;
  void renderVideoItem(
      QPainter* painter,
      const CanvasItem& item,
      const QRectF& frame_rect) const;
  void renderShapeItem(QPainter* painter, const CanvasItem& item, const QRectF& frame_rect) const;
  void renderTextItem(QPainter* painter, const CanvasItem& item, const QRectF& frame_rect) const;
  void renderBrushItem(QPainter* painter, const CanvasItem& item, int slide_index, const QSize& slide_size) const;
  void adjustItemsForFormatChange(const QSize& old_slide_size, const QSize& new_slide_size);
  void adjustItemsForSlidesShifted(int start_index, int delta_slides);
  void adjustItemsForSlideSwap(int first_index, int second_index);
  void adjustItemsForBlockMove(int start_index, int span, int destination_index);
  [[nodiscard]] QStringList normalizedSelectedItemIds(const QStringList& item_ids) const;
  [[nodiscard]] QVector<int> selectedItemIndexes(
      bool primary_slide_only = false,
      bool unlocked_only = false) const;
  [[nodiscard]] QStringList itemIdsForObjectGroup(const QString& group_id) const;
  [[nodiscard]] QVector<int> constraintStackItemIndexes(const QString& stack_id) const;
  [[nodiscard]] QRectF constraintStackBounds(const QString& stack_id) const;
  bool relayoutConstraintStack(const QString& stack_id);
  void relayoutConstraintStackForItem(const QString& item_id);
  void normalizeConstraintStacks();
  [[nodiscard]] QRectF selectedItemsBounds(bool unlocked_only = false) const;
  void resizeSelectedItems(const QSizeF& size, bool silent);
  void rotateSelectedItems(double rotation_degrees, bool silent);
  void normalizeSelection();
  void emitItemGeometryChanged(bool slides_changed);
  void ensureCurrentIndexValid();
  void refreshPreviews();
  void refreshPreviewAt(int index);
  void refreshItemPreview(CanvasItem* item);
  void refreshCurrentSelectionPreviews();
  void beginPreviewDiagnostics(const QString& reason, const QVector<int>& board_indices);
  void finishPreviewDiagnostics();
  void emitCanvasStateChanged(bool slides_changed = true);
  void setSelectedItemIds(const QStringList& item_ids, const QString& primary_item_id = QString());
  void setSelectedItemId(const QString& item_id);
  void setStatus(const QString& status);
  void setExportBusy(bool export_busy);
  void processNextExportStep();
  void finishAsyncExport(const QString& state, const QString& failure_reason = QString());
  void markDocumentDirty();
  void updateDocumentDirty();
  void rememberSavedContent();
  void setSaveState(const QString& state, const QString& error = QString());
  void scheduleAutosave();
  void writeAutosave();
  void refreshRecoveryState();
  [[nodiscard]] QString autosavePath() const;
  [[nodiscard]] bool loadRecoveryDocument(bool as_copy);
  void removeCurrentRecoveryFiles(const QString& previous_path = QString());
  void assignImageToCurrentSlot(
      const QString& file_path,
      const QString& display_label,
      const QString& adjustment_patch_json,
      const QString& slot_name);
  void removeGroupAtIndex(int index, bool keep_current_media);
  void clearGroupIndices(const LinkedGroup& group);
  void rebuildGroupIndices();
  void normalizeBoardSections();
  void moveSlideBlock(int start_index, int span, int destination_index);
  [[nodiscard]] QPair<int, int> editBoardMoveRange(int index) const;
  void captureRepeatedElementSyncSources();
  void synchronizePendingRepeatedElements();
  void normalizeRepeatedElementLinks();
  void normalizePhotoTextLinks();
  [[nodiscard]] bool editBoardHasUnlockedPhotoTextLayer(
      const Slide& slide, const QString& layer_id) const;
  [[nodiscard]] int editBoardIndexForId(const QString& board_id) const;
  [[nodiscard]] bool prepareLinkedPhotoTextSync(
      const QString& source_board_id,
      const QString& before_adjustment_patch,
      const QString& after_adjustment_patch,
      QVector<QPair<int, QString>>* peer_patches,
      QString* error) const;
  bool ensureCapacityForGroup(int start_index, int span);
  void normalizeForCurrentTemplate();
  void loadDesignLibrary();
  bool saveDesignLibrary() const;
  [[nodiscard]] bool templateLibraryIdExists(const QString& template_id) const;
  void recordRecentTemplateUse(const QString& template_id);
  [[nodiscard]] QJsonObject* userTemplateById(const QString& template_id);
  [[nodiscard]] const QJsonObject* userTemplateById(const QString& template_id) const;
  [[nodiscard]] const QJsonObject* brandKitById(const QString& brand_id) const;
  [[nodiscard]] bool applyBrandColorTokenBinding(CanvasItem* item);
  [[nodiscard]] bool applyBrandTypographyTokenBinding(CanvasItem* item);
  void refreshBrandTokenBindings(const QString& brand_id);
  void writePreviewForSlide(int index, const QImage& image);
  void pushUndoState(const QString& label, bool coalesce = false);
  void appendUndoEntry(HistoryEntry entry);
  void restoreHistoryEntry(const HistoryEntry& entry, const QString& status_prefix);
  void clearHistory();

  QVector<Slide> slides_;
  QVector<LinkedGroup> groups_;
  QVector<BoardSection> board_sections_;
  QVector<RepeatedElementLink> repeated_element_links_;
  QVector<PhotoTextLink> photo_text_links_;
  QVector<CanvasItem> items_;
  int current_index_ = 0;
  QString document_format_ = QStringLiteral("4x5");
  QString source_template_id_;
  int source_template_version_ = 0;
  QVariantList source_template_migration_history_;
  int custom_pixel_width_ = 1080;
  int custom_pixel_height_ = 1350;
  QString active_slot_ = QStringLiteral("primary");
  QString status_ = QStringLiteral("Carousel ready.");
  QVariantList status_notices_;
  int next_status_notice_id_ = 1;
  bool export_busy_ = false;
  bool edit_board_image_busy_ = false;
  int preview_revision_ = 0;
  QString session_id_;
  QString selected_item_id_;
  QStringList selected_item_ids_;
  QVector<int> selected_slide_indices_;
  mutable QHash<QString, QImage> text_contrast_sample_cache_;
  mutable QStringList text_contrast_sample_cache_lru_;
  mutable qint64 text_contrast_sample_cache_bytes_ = 0;
  QStringList pending_repeated_element_sync_source_ids_;
  QString document_name_ = QStringLiteral("Untitled Carousel");
  QString saved_document_id_;
  bool has_active_carousel_ = false;
  bool document_dirty_ = false;
  QString save_state_ = QStringLiteral("Ready");
  QString last_saved_at_;
  QString save_error_;
  QString export_directory_;
  QString export_color_space_ = QStringLiteral("sRGB");
  QString export_filename_pattern_ = QStringLiteral("carousel-{board_number}");
  int export_version_ = 1;
  QString export_collision_policy_ = QStringLiteral("rename");
  QString export_board_scope_ = QStringLiteral("all");
  QString export_size_mode_ = QStringLiteral("1x");
  int export_custom_pixel_width_ = 1080;
  int export_custom_pixel_height_ = 1350;
  bool export_aspect_locked_ = true;
  bool export_manifest_enabled_ = true;
  bool export_proof_sheet_enabled_ = false;
  bool export_proof_include_notes_ = true;
  QString export_image_format_ = QStringLiteral("png");
  int export_image_quality_ = 92;
  bool export_preserve_transparency_ = true;
  QString active_export_preset_id_;
  int active_export_preset_version_ = 0;
  QVariantMap export_preset_baseline_;
  QString export_partial_cleanup_policy_ = QStringLiteral("keep");
  QTimer* export_step_timer_ = nullptr;
  QFutureWatcher<QVariantMap>* export_watcher_ = nullptr;
  std::shared_ptr<ExportTaskState> export_task_state_;
  bool export_worker_context_ = false;
  QString export_job_directory_;
  quint64 export_request_generation_ = 0;
  quint64 export_active_generation_ = 0;
  QString export_state_ = QStringLiteral("idle");
  int export_current_board_ = 0;
  int export_completed_count_ = 0;
  int export_total_count_ = 0;
  QString export_failure_reason_;
  QVariantList export_job_plan_;
  QVariantList export_job_preflight_;
  QString preflight_severity_filter_ = QStringLiteral("all");
  QString preflight_current_issue_id_;
  QMap<QString, QString> ignored_preflight_issue_reasons_;
  QSize export_job_size_;
  QStringList export_job_created_files_;
  bool export_cancel_requested_ = false;
  QJsonObject document_passthrough_;
  QJsonObject design_library_passthrough_;
  QString document_load_error_;
  QTimer* autosave_timer_ = nullptr;
  QFutureWatcher<bool>* autosave_watcher_ = nullptr;
  std::unique_ptr<AutosaveRequest> active_autosave_;
  std::unique_ptr<AutosaveRequest> pending_autosave_;
  quint64 autosave_generation_ = 0;
  quint64 autosave_revision_ = 0;
  QVariantMap recovery_state_;
  QString recovery_path_;
  bool recovery_active_ = false;
  QVariantList relink_plan_;
  QString relink_directory_;
  QVariantList font_substitution_preview_;
  QString font_substitution_source_;
  QString font_substitution_replacement_;
  QStringList favorite_font_families_;
  QStringList recent_font_families_;
  QString copy_proof_language_ = QStringLiteral("en-US");
  QStringList ignored_copy_proof_issue_ids_;
  QStringList custom_dictionary_words_;
  QString copy_proof_current_issue_id_;
  QJsonObject saved_content_snapshot_;
  std::shared_ptr<const DocumentSnapshot> saved_content_values_;
  QTimer* preview_debounce_timer_ = nullptr;
  QFutureWatcher<PreviewResult>* preview_watcher_ = nullptr;
  std::shared_ptr<ExportTaskState> preview_task_state_;
  bool preview_worker_active_ = false;
  QSet<int> pending_preview_boards_;
  QSet<QString> pending_preview_items_;
  QHash<QString, std::shared_ptr<PreviewFiles>> preview_file_owners_;
  QString preview_output_directory_;
  QVariantMap preview_diagnostics_;
  bool preview_diagnostics_active_ = false;
  quint64 preview_request_revision_ = 0;
  int stale_preview_requests_dropped_ = 0;
  bool transform_dirty_ = false;
  bool transform_slides_changed_ = false;
  bool restoring_history_ = false;
  bool history_transaction_active_ = false;
  std::shared_ptr<const DocumentSnapshot> history_transaction_start_;
  QString history_transaction_label_;
  QVector<HistoryEntry> undo_stack_;
  QVector<HistoryEntry> redo_stack_;
  QVector<CanvasItem> clipboard_items_;
  QVariantMap clipboard_paste_preview_;
  QVector<QJsonObject> user_templates_;
  QStringList favorite_template_ids_;
  QStringList recent_template_ids_;
  QVector<QJsonObject> brand_kits_;
  QVector<QJsonObject> layout_recipes_;
  QVariantList text_styles_;
  QVariantMap text_style_preview_;
  QVariantMap selection_batch_preview_;
  QVariantMap repeat_grid_preview_;
  QVariantMap template_pack_update_preview_;
  QVariantMap design_package_import_preview_;
  QJsonObject design_package_import_document_;
  QVariantMap document_variables_;
  QVariantList copy_snippets_;
  bool brand_kit_editor_active_ = false;
  QVariantMap brand_kit_draft_;
  QVariantMap brand_application_preview_;
  QString active_brand_lint_kit_id_;
  QStringList ignored_brand_lint_issue_ids_;
  QVariantMap layout_settings_;
  QVariantMap layout_recipe_preview_;
  QVariantMap variant_comparison_state_;
  QJsonObject variant_comparison_left_document_;
  QVector<QJsonObject> saved_design_versions_;
  int next_layout_guide_id_ = 1;
  int clipboard_source_slide_index_ = 0;
  int clipboard_paste_generation_ = 0;
  QTimer* history_coalesce_timer_ = nullptr;
  QFutureWatcher<QVariantMap>* cutout_watcher_ = nullptr;
  QString cutout_processing_target_;
  QString cutout_processing_document_hash_;
  QString cutout_error_target_;
  QString cutout_error_message_;
  QString cutout_preview_mode_ = QStringLiteral("result");
  QString history_coalesce_label_;
  QVariantList media_autofill_plan_;
};

}  // namespace photo_editor::desktop
