#pragma once

#include <QObject>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "edit_recipe_state.h"

namespace photo_editor::desktop {

class CarouselDocumentController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList slides READ slides NOTIFY slidesChanged)
  Q_PROPERTY(QVariantMap selectionDetails READ selectionDetails NOTIFY selectionChanged)
  Q_PROPERTY(int slideCount READ slideCount NOTIFY slidesChanged)
  Q_PROPERTY(int currentIndex READ currentIndex NOTIFY selectionChanged)
  Q_PROPERTY(QString documentFormat READ documentFormat WRITE setDocumentFormat NOTIFY documentFormatChanged)
  Q_PROPERTY(bool canAddSlide READ canAddSlide NOTIFY slidesChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool exportBusy READ exportBusy NOTIFY exportBusyChanged)
  Q_PROPERTY(QStringList templateOptions READ templateOptions CONSTANT)
  Q_PROPERTY(QStringList fitModeOptions READ fitModeOptions CONSTANT)
  Q_PROPERTY(QStringList slotOptions READ slotOptions CONSTANT)

 public:
  explicit CarouselDocumentController(QObject* parent = nullptr);

  [[nodiscard]] QVariantList slides() const;
  [[nodiscard]] QVariantMap selectionDetails() const;
  [[nodiscard]] int slideCount() const;
  [[nodiscard]] int currentIndex() const;
  [[nodiscard]] QString documentFormat() const;
  [[nodiscard]] bool canAddSlide() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] bool exportBusy() const;
  [[nodiscard]] QStringList templateOptions() const;
  [[nodiscard]] QStringList fitModeOptions() const;
  [[nodiscard]] QStringList slotOptions() const;

  void setDocumentFormat(const QString& document_format);

  Q_INVOKABLE void selectSlide(int index);
  Q_INVOKABLE void addSlide();
  Q_INVOKABLE void removeCurrentSlide();
  Q_INVOKABLE void moveCurrentBackward();
  Q_INVOKABLE void moveCurrentForward();
  Q_INVOKABLE void applyTemplateToCurrent(const QString& template_kind);
  Q_INVOKABLE void setActiveSlot(const QString& slot_name);
  Q_INVOKABLE void setCurrentFitMode(const QString& fit_mode);
  Q_INVOKABLE void setCurrentScale(double scale);
  Q_INVOKABLE void setCurrentOffsetX(double offset_x);
  Q_INVOKABLE void setCurrentOffsetY(double offset_y);
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
  Q_INVOKABLE void assignPrimaryVideo(
      const QString& file_path,
      const QString& display_label = QString());
  Q_INVOKABLE void pickPrimaryImage();
  Q_INVOKABLE void pickSecondaryImage();
  Q_INVOKABLE void pickPrimaryVideo();
  Q_INVOKABLE void exportCarousel();
  Q_INVOKABLE bool exportCarouselToDirectory(const QString& directory_path);

 signals:
  void slidesChanged();
  void selectionChanged();
  void documentFormatChanged();
  void statusChanged();
  void exportBusyChanged();

 public:
  enum class MediaType { Empty, Image, Video };
  enum class FitMode { Cover, Contain };
  enum class SlideTemplate { Single, SplitPair };
  enum class GroupKind { None, HorizontalPair, VerticalTriptych };

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

  struct Slide final {
    SlideTemplate template_kind = SlideTemplate::Single;
    MediaItem primary;
    MediaItem secondary;
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

 private:
  [[nodiscard]] Slide* slideAt(int index);
  [[nodiscard]] const Slide* slideAt(int index) const;
  [[nodiscard]] LinkedGroup* groupForIndex(int index);
  [[nodiscard]] const LinkedGroup* groupForIndex(int index) const;
  [[nodiscard]] LinkedGroup* groupById(const QString& group_id);
  [[nodiscard]] const LinkedGroup* groupById(const QString& group_id) const;
  [[nodiscard]] MediaItem* currentEditableMedia();
  [[nodiscard]] const MediaItem* currentEditableMedia() const;
  [[nodiscard]] bool currentSelectionIsLinked() const;
  [[nodiscard]] QString currentActiveSlot() const;
  [[nodiscard]] QSize exportPixelSize() const;
  [[nodiscard]] QString previewsRoot() const;
  [[nodiscard]] QString previewPathForSlide(int index) const;
  [[nodiscard]] QString versionedLocalFile(const QString& file_path, int revision) const;
  [[nodiscard]] RecipeState recipeStateFromJson(const QString& recipe_json) const;
  [[nodiscard]] bool isImageFile(const QString& file_path) const;
  [[nodiscard]] bool isVideoFile(const QString& file_path) const;
  [[nodiscard]] QString imageFileFilter() const;
  [[nodiscard]] QString videoFileFilter() const;
  [[nodiscard]] QString displayNameForPath(const QString& file_path, const QString& fallback) const;
  [[nodiscard]] QVariantMap slideMapForIndex(int index) const;
  [[nodiscard]] QVariantMap selectionMap() const;
  [[nodiscard]] QString templateNameForIndex(int index) const;
  [[nodiscard]] QString fitModeName(FitMode fit_mode) const;
  [[nodiscard]] FitMode fitModeFromName(const QString& fit_mode) const;
  [[nodiscard]] QString groupKindName(GroupKind kind) const;
  [[nodiscard]] int spanForGroupKind(GroupKind kind) const;
  [[nodiscard]] GroupKind groupKindFromTemplate(const QString& template_kind) const;
  [[nodiscard]] QSize probeVideoSize(const QString& file_path) const;
  [[nodiscard]] bool exportVideoSlide(
      const MediaItem& media,
      const QString& output_path,
      const QSize& target_size) const;
  [[nodiscard]] QImage renderSlideImage(int index, const QSize& size, bool preview) const;
  [[nodiscard]] QImage renderVideoPlaceholder(const MediaItem& media, const QSize& size) const;
  [[nodiscard]] bool decodeEditedImage(
      const MediaItem& media,
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
  void ensureCurrentIndexValid();
  void refreshPreviews();
  void emitSelectionAndSlidesChanged();
  void setStatus(const QString& status);
  void setExportBusy(bool export_busy);
  void assignImageToCurrentSlot(
      const QString& file_path,
      const QString& display_label,
      const QString& recipe_json,
      bool secondary);
  void removeGroupAtIndex(int index, bool keep_current_media);
  void clearGroupIndices(const LinkedGroup& group);
  void rebuildGroupIndices();
  bool ensureCapacityForGroup(int start_index, int span);
  void resetSlideForSingle(int index);
  void normalizeForCurrentTemplate();
  void writePreviewForSlide(int index, const QImage& image);

  QVector<Slide> slides_;
  QVector<LinkedGroup> groups_;
  int current_index_ = 0;
  QString document_format_ = QStringLiteral("4x5");
  QString active_slot_ = QStringLiteral("primary");
  QString status_ = QStringLiteral("Carousel ready.");
  bool export_busy_ = false;
  int preview_revision_ = 0;
  QString session_id_;
};

}  // namespace photo_editor::desktop
