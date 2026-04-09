#pragma once

#include <QImage>
#include <QObject>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "edit_recipe_state.h"

namespace photo_editor::desktop {

class CarouselDocumentController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList slides READ slides NOTIFY slidesChanged)
  Q_PROPERTY(QVariantList canvasItems READ canvasItems NOTIFY itemsChanged)
  Q_PROPERTY(QVariantMap selectionDetails READ selectionDetails NOTIFY selectionChanged)
  Q_PROPERTY(int slideCount READ slideCount NOTIFY slidesChanged)
  Q_PROPERTY(int currentIndex READ currentIndex NOTIFY selectionChanged)
  Q_PROPERTY(int documentPixelWidth READ documentPixelWidth NOTIFY slidesChanged)
  Q_PROPERTY(int documentPixelHeight READ documentPixelHeight NOTIFY documentFormatChanged)
  Q_PROPERTY(QString documentFormat READ documentFormat WRITE setDocumentFormat NOTIFY documentFormatChanged)
  Q_PROPERTY(bool canAddSlide READ canAddSlide NOTIFY slidesChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool exportBusy READ exportBusy NOTIFY exportBusyChanged)
  Q_PROPERTY(QString selectedItemId READ selectedItemId NOTIFY selectionChanged)
  Q_PROPERTY(QString documentName READ documentName WRITE setDocumentName NOTIFY documentStateChanged)
  Q_PROPERTY(QString savedDocumentId READ savedDocumentId NOTIFY documentStateChanged)
  Q_PROPERTY(QStringList templateOptions READ templateOptions CONSTANT)
  Q_PROPERTY(QStringList fitModeOptions READ fitModeOptions CONSTANT)
  Q_PROPERTY(QStringList slotOptions READ slotOptions CONSTANT)

 public:
  explicit CarouselDocumentController(QObject* parent = nullptr);

  enum class MediaType { Empty, Image, Video };
  enum class FitMode { Cover, Contain };
  enum class SlideTemplate { Single, SplitPair, StackedTriptych };
  enum class GroupKind { None, HorizontalPair };
  enum class ItemKind { Image, Text, Video };

  using RecipeState = EditRecipeState;

  struct MediaItem final {
    MediaType type = MediaType::Empty;
    QString file_path;
    QString display_label;
    QString recipe_json;
    RecipeState recipe_state;
    FitMode fit_mode = FitMode::Cover;
    double scale = 1.0;
    double offset_x = 0.0;
    double offset_y = 0.0;
  };

  struct CanvasItem final {
    QString id;
    ItemKind kind = ItemKind::Image;
    QString file_path;
    QString display_label;
    bool hidden = false;
    bool locked = false;
    QString recipe_json;
    RecipeState recipe_state;
    QString text = QStringLiteral("New Text");
    QString text_color = QStringLiteral("#1e1a17");
    double text_point_size = 72.0;
    QRectF bounds;
    double rotation_degrees = 0.0;
    FitMode fit_mode = FitMode::Cover;
    QSizeF source_size;
  };

  [[nodiscard]] QVariantList slides() const;
  [[nodiscard]] QVariantList canvasItems() const;
  [[nodiscard]] QVariantMap selectionDetails() const;
  [[nodiscard]] int slideCount() const;
  [[nodiscard]] int currentIndex() const;
  [[nodiscard]] int documentPixelWidth() const;
  [[nodiscard]] int documentPixelHeight() const;
  [[nodiscard]] QString documentFormat() const;
  [[nodiscard]] bool canAddSlide() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] bool exportBusy() const;
  [[nodiscard]] QString selectedItemId() const;
  [[nodiscard]] QString documentName() const;
  [[nodiscard]] QString savedDocumentId() const;
  [[nodiscard]] QStringList templateOptions() const;
  [[nodiscard]] QStringList fitModeOptions() const;
  [[nodiscard]] QStringList slotOptions() const;

  void setDocumentFormat(const QString& document_format);
  void setDocumentName(const QString& document_name);

  Q_INVOKABLE void selectSlide(int index);
  Q_INVOKABLE void addSlide();
  Q_INVOKABLE void removeCurrentSlide();
  Q_INVOKABLE void moveCurrentBackward();
  Q_INVOKABLE void moveCurrentForward();

  Q_INVOKABLE void addPhotoItem(
      const QString& file_path,
      const QString& display_label = QString(),
      const QString& recipe_json = QString());
  Q_INVOKABLE void addVideoItem(
      const QString& file_path,
      const QString& display_label = QString());
  Q_INVOKABLE void addTextItem(const QString& text = QStringLiteral("New Text"));
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
  Q_INVOKABLE void moveSelectedItemByPixels(double delta_x, double delta_y);
  Q_INVOKABLE void setSelectedItemCenter(double center_x, double center_y);
  Q_INVOKABLE void setSelectedItemSize(double width, double height);
  Q_INVOKABLE void setSelectedItemRotation(double rotation_degrees);
  Q_INVOKABLE void setSelectedText(const QString& text);
  Q_INVOKABLE void setSelectedTextColor(const QString& color_value);
  Q_INVOKABLE void setSelectedTextPointSize(double point_size);
  Q_INVOKABLE void setSelectedItemHidden(bool hidden);
  Q_INVOKABLE void setSelectedItemLocked(bool locked);
  Q_INVOKABLE void replaceSelectedPhotoItem(
      const QString& file_path,
      const QString& display_label = QString(),
      const QString& recipe_json = QString());
  Q_INVOKABLE void replaceSelectedVideoItem(
      const QString& file_path,
      const QString& display_label = QString());
  Q_INVOKABLE void alignSelectionLeft();
  Q_INVOKABLE void alignSelectionHorizontalCenter();
  Q_INVOKABLE void alignSelectionRight();
  Q_INVOKABLE void alignSelectionTop();
  Q_INVOKABLE void alignSelectionVerticalCenter();
  Q_INVOKABLE void alignSelectionBottom();
  Q_INVOKABLE void distributeSelectionHorizontally();
  Q_INVOKABLE void distributeSelectionVertically();

  Q_INVOKABLE void applyTemplateToCurrent(const QString& template_kind);
  Q_INVOKABLE void setActiveSlot(const QString& slot_name);
  Q_INVOKABLE void setCurrentFitMode(const QString& fit_mode);
  Q_INVOKABLE void setCurrentScale(double scale);
  Q_INVOKABLE void setCurrentOffsetX(double offset_x);
  Q_INVOKABLE void setCurrentOffsetY(double offset_y);
  Q_INVOKABLE void moveCurrentMediaByPixels(double delta_x, double delta_y);
  Q_INVOKABLE void clearCurrentSlot();
  Q_INVOKABLE void clearCurrentGroup();
  Q_INVOKABLE void assignPrimaryImage(
      const QString& file_path,
      const QString& display_label = QString(),
      const QString& recipe_json = QString());
  Q_INVOKABLE void assignSecondaryImage(
      const QString& file_path,
      const QString& display_label = QString(),
      const QString& recipe_json = QString());
  Q_INVOKABLE void assignTertiaryImage(
      const QString& file_path,
      const QString& display_label = QString(),
      const QString& recipe_json = QString());
  Q_INVOKABLE void assignPrimaryVideo(
      const QString& file_path,
      const QString& display_label = QString());
  Q_INVOKABLE void pickPrimaryImage();
  Q_INVOKABLE void pickSecondaryImage();
  Q_INVOKABLE void pickPrimaryVideo();
  Q_INVOKABLE void exportCarousel();
  Q_INVOKABLE bool exportCarouselToDirectory(const QString& directory_path);
  Q_INVOKABLE QVariantList recentDocuments() const;
  Q_INVOKABLE bool saveDocument(const QString& document_name = QString());
  Q_INVOKABLE bool loadDocument(const QString& document_id);

 signals:
  void slidesChanged();
  void itemsChanged();
  void selectionChanged();
  void documentFormatChanged();
  void statusChanged();
  void exportBusyChanged();
  void documentStateChanged();

 private:
  struct Slide final {
    SlideTemplate template_kind = SlideTemplate::Single;
    MediaItem primary;
    MediaItem secondary;
    MediaItem tertiary;
    QString linked_group_id;
    QString preview_source;
  };

  struct LinkedGroup final {
    QString id;
    GroupKind kind = GroupKind::None;
    int start_index = 0;
    int span = 0;
    MediaItem media;
  };

  [[nodiscard]] Slide* slideAt(int index);
  [[nodiscard]] const Slide* slideAt(int index) const;
  [[nodiscard]] LinkedGroup* groupForIndex(int index);
  [[nodiscard]] const LinkedGroup* groupForIndex(int index) const;
  [[nodiscard]] LinkedGroup* groupById(const QString& group_id);
  [[nodiscard]] const LinkedGroup* groupById(const QString& group_id) const;
  [[nodiscard]] CanvasItem* currentItem();
  [[nodiscard]] const CanvasItem* currentItem() const;
  [[nodiscard]] MediaItem* currentEditableMedia();
  [[nodiscard]] const MediaItem* currentEditableMedia() const;
  [[nodiscard]] int indexOfItem(const QString& item_id) const;
  [[nodiscard]] int currentSelectionSlideIndex() const;
  [[nodiscard]] QRectF slideRectInDocument(int index) const;
  [[nodiscard]] int slideIndexForX(double x) const;
  [[nodiscard]] int slideIndexForItem(const CanvasItem& item) const;
  [[nodiscard]] QSize exportPixelSize() const;
  [[nodiscard]] QString previewsRoot() const;
  [[nodiscard]] QString previewPathForSlide(int index) const;
  [[nodiscard]] QString versionedLocalFile(const QString& file_path, int revision) const;
  [[nodiscard]] QString fileUrlForPath(const QString& file_path) const;
  [[nodiscard]] RecipeState recipeStateFromJson(const QString& recipe_json) const;
  [[nodiscard]] bool isImageFile(const QString& file_path) const;
  [[nodiscard]] bool isVideoFile(const QString& file_path) const;
  [[nodiscard]] QString imageFileFilter() const;
  [[nodiscard]] QString videoFileFilter() const;
  [[nodiscard]] QString displayNameForPath(const QString& file_path, const QString& fallback) const;
  [[nodiscard]] QVariantMap slideMapForIndex(int index) const;
  [[nodiscard]] QVariantMap itemMap(const CanvasItem& item) const;
  [[nodiscard]] QVariantMap selectionMap() const;
  [[nodiscard]] QString templateNameForIndex(int index) const;
  [[nodiscard]] QString fitModeName(FitMode fit_mode) const;
  [[nodiscard]] FitMode fitModeFromName(const QString& fit_mode) const;
  [[nodiscard]] QString groupKindName(GroupKind kind) const;
  [[nodiscard]] int spanForGroupKind(GroupKind kind) const;
  [[nodiscard]] GroupKind groupKindFromTemplate(const QString& template_kind) const;
  [[nodiscard]] bool currentSelectionIsLinked() const;
  [[nodiscard]] QString currentActiveSlot() const;
  [[nodiscard]] QSize currentMediaViewportSize() const;
  [[nodiscard]] QSize effectiveMediaSourceSize(const MediaItem& media) const;
  [[nodiscard]] qreal normalizedAspectRatio(const CanvasItem& item) const;
  [[nodiscard]] qreal measuredTextHeight(const CanvasItem& item) const;
  [[nodiscard]] QSize probeVideoSize(const QString& file_path) const;
  [[nodiscard]] bool hasFreeformItemsOnSlide(int index) const;
  [[nodiscard]] bool exportVideoSlide(
      const MediaItem& media,
      const QString& output_path,
      const QSize& target_size) const;
  [[nodiscard]] QString helperProgram() const;
  [[nodiscard]] QStringList helperArguments(const QStringList& command_arguments) const;
  [[nodiscard]] bool useBuiltHelperBinary() const;
  [[nodiscard]] QString builtBinaryPath(const QString& binary_name) const;
  [[nodiscard]] bool runHelperCommand(
      const QStringList& command_arguments,
      QString* output = nullptr,
      QString* error_message = nullptr) const;
  [[nodiscard]] QJsonObject mediaObject(const MediaItem& media) const;
  [[nodiscard]] MediaItem mediaFromObject(const QJsonObject& object) const;
  [[nodiscard]] QJsonObject slideObject(const Slide& slide) const;
  [[nodiscard]] Slide slideFromObject(const QJsonObject& object) const;
  [[nodiscard]] QJsonObject groupObject(const LinkedGroup& group) const;
  [[nodiscard]] LinkedGroup groupFromObject(const QJsonObject& object) const;
  [[nodiscard]] QJsonObject itemObject(const CanvasItem& item) const;
  [[nodiscard]] CanvasItem itemFromObject(const QJsonObject& object) const;
  [[nodiscard]] QJsonObject documentObject() const;
  bool loadDocumentObject(const QJsonObject& object);
  [[nodiscard]] QImage renderSlideBaseImage(int index, const QSize& size, bool preview) const;
  [[nodiscard]] QImage renderSlideImage(int index, const QSize& size, bool preview) const;
  [[nodiscard]] QString outputColorSpaceForSlide(int index) const;
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
  void renderTextItem(QPainter* painter, const CanvasItem& item, const QRectF& frame_rect) const;
  void adjustItemsForFormatChange(const QSize& old_slide_size, const QSize& new_slide_size);
  void adjustItemsForSlidesShifted(int start_index, int delta_slides);
  void adjustItemsForSlideSwap(int first_index, int second_index);
  void adjustItemsForBlockMove(int start_index, int span, int destination_index);
  [[nodiscard]] QStringList normalizedSelectedItemIds(const QStringList& item_ids) const;
  [[nodiscard]] QVector<int> selectedItemIndexes(
      bool primary_slide_only = false,
      bool unlocked_only = false) const;
  void normalizeSelection();
  void emitItemGeometryChanged(bool slides_changed);
  void ensureCurrentIndexValid();
  void refreshPreviews();
  void refreshPreviewAt(int index);
  void refreshCurrentSelectionPreviews();
  void emitCanvasStateChanged(bool slides_changed = true);
  void setSelectedItemIds(const QStringList& item_ids, const QString& primary_item_id = QString());
  void setSelectedItemId(const QString& item_id);
  void setStatus(const QString& status);
  void setExportBusy(bool export_busy);
  void assignImageToCurrentSlot(
      const QString& file_path,
      const QString& display_label,
      const QString& recipe_json,
      const QString& slot_name);
  void removeGroupAtIndex(int index, bool keep_current_media);
  void clearGroupIndices(const LinkedGroup& group);
  void rebuildGroupIndices();
  bool ensureCapacityForGroup(int start_index, int span);
  void normalizeForCurrentTemplate();
  void writePreviewForSlide(int index, const QImage& image);

  QVector<Slide> slides_;
  QVector<LinkedGroup> groups_;
  QVector<CanvasItem> items_;
  int current_index_ = 0;
  QString document_format_ = QStringLiteral("4x5");
  QString active_slot_ = QStringLiteral("primary");
  QString status_ = QStringLiteral("Carousel ready.");
  bool export_busy_ = false;
  int preview_revision_ = 0;
  QString session_id_;
  QString selected_item_id_;
  QStringList selected_item_ids_;
  QString document_name_ = QStringLiteral("Untitled Carousel");
  QString saved_document_id_;
};

}  // namespace photo_editor::desktop
