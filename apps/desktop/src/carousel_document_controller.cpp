#include "carousel_document_controller.h"

#include "photo_editor/pipeline/color_adjustments.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPen>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <vector>

namespace photo_editor::desktop {

namespace {

constexpr int kMaxSlides = 20;
constexpr int kPreviewLongEdge = 540;

double clamp_unit_interval(double value) {
  return qBound(-1.0, value, 1.0);
}

double clamp_zoom_scale(double value) {
  return qBound(0.6, value, 3.5);
}

QString normalized_path(const QString& path) {
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QStringList image_suffixes() {
  return {
      QStringLiteral("avif"),
      QStringLiteral("bmp"),
      QStringLiteral("gif"),
      QStringLiteral("heic"),
      QStringLiteral("heif"),
      QStringLiteral("jpeg"),
      QStringLiteral("jpg"),
      QStringLiteral("png"),
      QStringLiteral("tif"),
      QStringLiteral("tiff"),
      QStringLiteral("webp"),
  };
}

QStringList video_suffixes() {
  return {
      QStringLiteral("mov"),
      QStringLiteral("mp4"),
      QStringLiteral("m4v"),
      QStringLiteral("mpeg"),
      QStringLiteral("mpg"),
      QStringLiteral("webm"),
  };
}

bool suffix_in_list(const QString& file_path, const QStringList& suffixes) {
  return suffixes.contains(QFileInfo(file_path).suffix().toLower());
}

double crop_aspect_ratio(const QString& preset) {
  if (preset == QStringLiteral("Square")) {
    return 1.0;
  }
  if (preset == QStringLiteral("4x5")) {
    return 4.0 / 5.0;
  }
  if (preset == QStringLiteral("16x9")) {
    return 16.0 / 9.0;
  }
  return 0.0;
}

QRect crop_rect_for_image(const QSize& size, const QString& preset, double crop_zoom) {
  int crop_width = size.width();
  int crop_height = size.height();

  const auto aspect_ratio = crop_aspect_ratio(preset);
  if (aspect_ratio > 0.0) {
    const auto current_ratio = static_cast<double>(size.width()) / static_cast<double>(size.height());
    if (current_ratio > aspect_ratio) {
      crop_width = static_cast<int>(qRound(static_cast<double>(size.height()) * aspect_ratio));
    } else {
      crop_height = static_cast<int>(qRound(static_cast<double>(size.width()) / aspect_ratio));
    }
  }

  crop_width = qMax(1, static_cast<int>(qRound(static_cast<double>(crop_width) / qMax(0.01, crop_zoom))));
  crop_height = qMax(1, static_cast<int>(qRound(static_cast<double>(crop_height) / qMax(0.01, crop_zoom))));

  const auto x = qMax(0, (size.width() - crop_width) / 2);
  const auto y = qMax(0, (size.height() - crop_height) / 2);
  return {x, y, crop_width, crop_height};
}

photo_editor::pipeline::ColorAdjustmentState to_pipeline_state(
    const CarouselDocumentController::RecipeState& state) {
  return photo_editor::pipeline::ColorAdjustmentState{
      .exposure = state.exposure,
      .contrast = state.contrast,
      .highlights = state.highlights,
      .shadows = state.shadows,
      .whites = state.whites,
      .blacks = state.blacks,
      .temperature = state.temperature,
      .tint = state.tint,
      .texture = state.texture,
      .clarity = state.clarity,
      .dehaze = state.dehaze,
      .vibrance = state.vibrance,
      .saturation = state.saturation,
      .curve_shadows = state.curve_shadows,
      .curve_darks = state.curve_darks,
      .curve_lights = state.curve_lights,
      .curve_highlights = state.curve_highlights,
      .color_mixer_channel = state.hsl_channel.toStdString(),
      .hue_adjustment = state.hue_adjustment,
      .saturation_adjustment = state.saturation_adjustment,
      .luminance_adjustment = state.luminance_adjustment,
      .linear_mask_enabled = state.linear_mask_enabled,
      .linear_mask_exposure = state.linear_mask_exposure,
      .linear_mask_temperature = state.linear_mask_temperature,
      .linear_mask_feather = state.linear_mask_feather,
      .linear_mask_position = state.linear_mask_position,
      .linear_mask_rotation = state.linear_mask_rotation,
  };
}

int clamp_channel(double value) {
  return qBound(0, static_cast<int>(qRound(value)), 255);
}

double clamp_alpha(double value) {
  return qBound(0.0, value, 1.0);
}

void apply_color_adjustments(QImage* image, const CarouselDocumentController::RecipeState& state) {
  const auto width = image->width();
  const auto height = image->height();
  std::vector<photo_editor::pipeline::ColorPixel> pixels(static_cast<std::size_t>(width * height));

  for (int y = 0; y < height; ++y) {
    const auto* scanline = reinterpret_cast<const QRgb*>(image->constScanLine(y));
    for (int x = 0; x < width; ++x) {
      const QColor color = QColor::fromRgb(scanline[x]);
      pixels[static_cast<std::size_t>(y * width + x)] = photo_editor::pipeline::ColorPixel{
          .red = photo_editor::pipeline::srgb_to_linear(color.redF()),
          .green = photo_editor::pipeline::srgb_to_linear(color.greenF()),
          .blue = photo_editor::pipeline::srgb_to_linear(color.blueF()),
          .alpha = color.alphaF(),
      };
    }
  }

  photo_editor::pipeline::apply_color_adjustments(&pixels, width, height, to_pipeline_state(state));

  for (int y = 0; y < height; ++y) {
    auto* scanline = reinterpret_cast<QRgb*>(image->scanLine(y));
    for (int x = 0; x < width; ++x) {
      auto& pixel = pixels[static_cast<std::size_t>(y * width + x)];
      scanline[x] = qRgba(
          clamp_channel(photo_editor::pipeline::linear_to_srgb(pixel.red) * 255.0),
          clamp_channel(photo_editor::pipeline::linear_to_srgb(pixel.green) * 255.0),
          clamp_channel(photo_editor::pipeline::linear_to_srgb(pixel.blue) * 255.0),
          clamp_channel(clamp_alpha(pixel.alpha) * 255.0));
    }
  }
}

QString file_filter_from_suffixes(const QString& label, const QStringList& suffixes) {
  QStringList patterns;
  patterns.reserve(suffixes.size());
  for (const auto& suffix : suffixes) {
    patterns.push_back(QStringLiteral("*.%1").arg(suffix));
  }
  return QStringLiteral("%1 (%2)").arg(label, patterns.join(' '));
}

}  // namespace

CarouselDocumentController::CarouselDocumentController(QObject* parent)
    : QObject(parent), session_id_(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
  slides_.push_back(Slide{});
  refreshPreviews();
}

QVariantList CarouselDocumentController::slides() const {
  QVariantList values;
  values.reserve(slides_.size());
  for (int index = 0; index < slides_.size(); ++index) {
    values.push_back(slideMapForIndex(index));
  }
  return values;
}

QVariantMap CarouselDocumentController::selectionDetails() const {
  return selectionMap();
}

int CarouselDocumentController::slideCount() const { return static_cast<int>(slides_.size()); }

int CarouselDocumentController::currentIndex() const { return current_index_; }

QString CarouselDocumentController::documentFormat() const { return document_format_; }

bool CarouselDocumentController::canAddSlide() const { return slides_.size() < kMaxSlides; }

QString CarouselDocumentController::status() const { return status_; }

bool CarouselDocumentController::exportBusy() const { return export_busy_; }

QStringList CarouselDocumentController::templateOptions() const {
  return {
      QStringLiteral("single"),
      QStringLiteral("split-pair"),
      QStringLiteral("linked-horizontal-pair"),
      QStringLiteral("linked-vertical-triptych"),
  };
}

QStringList CarouselDocumentController::fitModeOptions() const {
  return {QStringLiteral("cover"), QStringLiteral("contain")};
}

QStringList CarouselDocumentController::slotOptions() const {
  return {QStringLiteral("primary"), QStringLiteral("secondary")};
}

void CarouselDocumentController::setDocumentFormat(const QString& document_format) {
  const auto normalized = document_format == QStringLiteral("5x4") ? QStringLiteral("5x4") : QStringLiteral("4x5");
  if (document_format_ == normalized) {
    return;
  }
  document_format_ = normalized;
  refreshPreviews();
  emit documentFormatChanged();
  emitSelectionAndSlidesChanged();
  setStatus(
      QStringLiteral("Carousel format set to %1.")
          .arg(normalized == QStringLiteral("4x5") ? QStringLiteral("1080x1350") : QStringLiteral("1350x1080")));
}

void CarouselDocumentController::selectSlide(int index) {
  if (index < 0 || index >= slides_.size() || current_index_ == index) {
    return;
  }
  current_index_ = index;
  if (active_slot_ == QStringLiteral("secondary") &&
      slideAt(current_index_)->template_kind != SlideTemplate::SplitPair) {
    active_slot_ = QStringLiteral("primary");
  }
  emit selectionChanged();
}

void CarouselDocumentController::addSlide() {
  if (!canAddSlide()) {
    setStatus(QStringLiteral("Instagram carousels are capped at 20 slides."));
    return;
  }
  slides_.push_back(Slide{});
  current_index_ = static_cast<int>(slides_.size()) - 1;
  refreshPreviews();
  emitSelectionAndSlidesChanged();
  setStatus(QStringLiteral("Added slide %1.").arg(current_index_ + 1));
}

void CarouselDocumentController::removeCurrentSlide() {
  if (slides_.isEmpty()) {
    return;
  }

  if (const auto* group = groupForIndex(current_index_)) {
    const auto group_id = group->id;
    slides_.remove(group->start_index, group->span);
    for (qsizetype index = groups_.size(); index > 0; --index) {
      if (groups_[index - 1].id == group_id) {
        groups_.removeAt(index - 1);
      }
    }
  } else {
    slides_.removeAt(current_index_);
  }

  if (slides_.isEmpty()) {
    slides_.push_back(Slide{});
  }

  rebuildGroupIndices();
  ensureCurrentIndexValid();
  refreshPreviews();
  emitSelectionAndSlidesChanged();
  setStatus(QStringLiteral("Removed slide."));
}

void CarouselDocumentController::moveCurrentBackward() {
  if (slides_.size() < 2) {
    return;
  }

  if (const auto* group = groupForIndex(current_index_)) {
    if (group->start_index == 0) {
      return;
    }
    const auto moved_group = *group;
    QVector<Slide> block;
    block.reserve(moved_group.span);
    for (int index = 0; index < moved_group.span; ++index) {
      block.push_back(slides_.at(moved_group.start_index + index));
    }
    slides_.remove(moved_group.start_index, moved_group.span);
    const auto destination = moved_group.start_index - 1;
    for (int index = 0; index < block.size(); ++index) {
      slides_.insert(destination + index, block.at(index));
    }
    current_index_ = destination;
  } else {
    if (current_index_ == 0) {
      return;
    }
    slides_.move(current_index_, current_index_ - 1);
    --current_index_;
  }

  rebuildGroupIndices();
  refreshPreviews();
  emitSelectionAndSlidesChanged();
  setStatus(QStringLiteral("Moved selection earlier in the carousel."));
}

void CarouselDocumentController::moveCurrentForward() {
  if (slides_.size() < 2) {
    return;
  }

  if (const auto* group = groupForIndex(current_index_)) {
    if (group->start_index + group->span >= slides_.size()) {
      return;
    }
    const auto moved_group = *group;
    QVector<Slide> block;
    block.reserve(moved_group.span);
    for (int index = 0; index < moved_group.span; ++index) {
      block.push_back(slides_.at(moved_group.start_index + index));
    }
    slides_.remove(moved_group.start_index, moved_group.span);
    const auto destination = moved_group.start_index + 1;
    for (int index = 0; index < block.size(); ++index) {
      slides_.insert(destination + index, block.at(index));
    }
    current_index_ = destination;
  } else {
    if (current_index_ >= slides_.size() - 1) {
      return;
    }
    slides_.move(current_index_, current_index_ + 1);
    ++current_index_;
  }

  rebuildGroupIndices();
  refreshPreviews();
  emitSelectionAndSlidesChanged();
  setStatus(QStringLiteral("Moved selection later in the carousel."));
}

void CarouselDocumentController::applyTemplateToCurrent(const QString& template_kind) {
  if (current_index_ < 0 || current_index_ >= slides_.size()) {
    return;
  }

  MediaItem source_media;
  if (const auto* media = currentEditableMedia()) {
    source_media = *media;
  } else if (const auto* slide = slideAt(current_index_)) {
    source_media = slide->primary;
  }

  bool forced_portrait = false;
  if (template_kind == QStringLiteral("linked-horizontal-pair") &&
      document_format_ != QStringLiteral("4x5")) {
    document_format_ = QStringLiteral("4x5");
    forced_portrait = true;
    emit documentFormatChanged();
  }

  bool forced_landscape = false;
  if ((template_kind == QStringLiteral("split-pair") ||
       template_kind == QStringLiteral("linked-vertical-triptych")) &&
      document_format_ != QStringLiteral("5x4")) {
    document_format_ = QStringLiteral("5x4");
    forced_landscape = true;
    emit documentFormatChanged();
  }

  if (template_kind == QStringLiteral("single")) {
    removeGroupAtIndex(current_index_, true);
    auto* slide = slideAt(current_index_);
    if (source_media.type != MediaType::Empty) {
      slide->primary = source_media;
    }
    slide->template_kind = SlideTemplate::Single;
    slide->secondary = MediaItem{};
    active_slot_ = QStringLiteral("primary");
    refreshPreviews();
    emitSelectionAndSlidesChanged();
    setStatus(QStringLiteral("Selection set to a single-slide canvas."));
    return;
  }

  if (template_kind == QStringLiteral("split-pair")) {
    removeGroupAtIndex(current_index_, true);
    auto* slide = slideAt(current_index_);
    if (source_media.type != MediaType::Empty) {
      slide->primary = source_media;
    }
    slide->template_kind = SlideTemplate::SplitPair;
    slide->secondary = MediaItem{};
    active_slot_ = QStringLiteral("primary");
    refreshPreviews();
    emitSelectionAndSlidesChanged();
    setStatus(forced_landscape
                  ? QStringLiteral("Selection set to a half-and-half landscape split.")
                  : QStringLiteral("Selection set to a half-and-half split slide."));
    return;
  }

  const auto group_kind = groupKindFromTemplate(template_kind);
  if (group_kind == GroupKind::None) {
    return;
  }

  removeGroupAtIndex(current_index_, true);

  const auto span = spanForGroupKind(group_kind);
  if (!ensureCapacityForGroup(current_index_, span)) {
    setStatus(QStringLiteral("Not enough room to create that linked split within the 20-slide limit."));
    return;
  }

  LinkedGroup group;
  group.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  group.kind = group_kind;
  group.start_index = current_index_;
  group.span = span;
  group.media = source_media;
  groups_.push_back(group);

  for (int index = current_index_; index < current_index_ + span; ++index) {
    auto* slide = slideAt(index);
    slide->template_kind = SlideTemplate::Single;
    slide->primary = MediaItem{};
    slide->secondary = MediaItem{};
    slide->linked_group_id = group.id;
  }

  active_slot_ = QStringLiteral("primary");
  refreshPreviews();
  emitSelectionAndSlidesChanged();
  setStatus(
      group_kind == GroupKind::HorizontalPair
          ? (forced_portrait ? QStringLiteral("Created a linked 2-slide portrait spread.")
                             : QStringLiteral("Created a linked 2-slide split."))
          : (forced_landscape ? QStringLiteral("Created a linked 3-slide landscape stack.")
                              : QStringLiteral("Created a linked 3-slide stacked split.")));
}

void CarouselDocumentController::setActiveSlot(const QString& slot_name) {
  const auto normalized = slot_name == QStringLiteral("secondary") ? QStringLiteral("secondary") : QStringLiteral("primary");
  if (active_slot_ == normalized) {
    return;
  }
  active_slot_ = normalized;
  normalizeForCurrentTemplate();
  emit selectionChanged();
}

void CarouselDocumentController::setCurrentFitMode(const QString& fit_mode) {
  if (auto* media = currentEditableMedia()) {
    media->fit_mode = fitModeFromName(fit_mode);
    refreshPreviews();
    emitSelectionAndSlidesChanged();
  }
}

void CarouselDocumentController::setCurrentScale(double scale) {
  if (auto* media = currentEditableMedia()) {
    media->scale = clamp_zoom_scale(scale);
    refreshPreviews();
    emitSelectionAndSlidesChanged();
  }
}

void CarouselDocumentController::setCurrentOffsetX(double offset_x) {
  if (auto* media = currentEditableMedia()) {
    media->offset_x = clamp_unit_interval(offset_x);
    refreshPreviews();
    emitSelectionAndSlidesChanged();
  }
}

void CarouselDocumentController::setCurrentOffsetY(double offset_y) {
  if (auto* media = currentEditableMedia()) {
    media->offset_y = clamp_unit_interval(offset_y);
    refreshPreviews();
    emitSelectionAndSlidesChanged();
  }
}

void CarouselDocumentController::clearCurrentSlot() {
  if (currentSelectionIsLinked()) {
    clearCurrentGroup();
    return;
  }

  auto* slide = slideAt(current_index_);
  if (slide == nullptr) {
    return;
  }

  if (active_slot_ == QStringLiteral("secondary") && slide->template_kind == SlideTemplate::SplitPair) {
    slide->secondary = MediaItem{};
    setStatus(QStringLiteral("Cleared the secondary media slot."));
  } else {
    slide->primary = MediaItem{};
    if (slide->template_kind == SlideTemplate::SplitPair) {
      slide->secondary = MediaItem{};
    }
    setStatus(QStringLiteral("Cleared the current slide media."));
  }

  refreshPreviews();
  emitSelectionAndSlidesChanged();
}

void CarouselDocumentController::clearCurrentGroup() {
  removeGroupAtIndex(current_index_, true);
  refreshPreviews();
  emitSelectionAndSlidesChanged();
  setStatus(QStringLiteral("Collapsed the linked split back to a single canvas."));
}

void CarouselDocumentController::assignPrimaryImage(
    const QString& file_path,
    const QString& display_label,
    const QString& recipe_json) {
  assignImageToCurrentSlot(file_path, display_label, recipe_json, false);
}

void CarouselDocumentController::assignSecondaryImage(
    const QString& file_path,
    const QString& display_label,
    const QString& recipe_json) {
  assignImageToCurrentSlot(file_path, display_label, recipe_json, true);
}

void CarouselDocumentController::assignPrimaryVideo(
    const QString& file_path,
    const QString& display_label) {
  const auto normalized = normalized_path(file_path);
  if (!isVideoFile(normalized)) {
    setStatus(QStringLiteral("That file is not a supported video format."));
    return;
  }

  removeGroupAtIndex(current_index_, true);
  auto* slide = slideAt(current_index_);
  slide->template_kind = SlideTemplate::Single;
  slide->secondary = MediaItem{};
  slide->primary = MediaItem{
      .type = MediaType::Video,
      .file_path = normalized,
      .display_label = displayNameForPath(normalized, display_label),
      .fit_mode = FitMode::Cover,
      .scale = 1.0,
      .offset_x = 0.0,
      .offset_y = 0.0,
  };
  active_slot_ = QStringLiteral("primary");
  refreshPreviews();
  emitSelectionAndSlidesChanged();
  setStatus(QStringLiteral("Video added to the current slide."));
}

void CarouselDocumentController::pickPrimaryImage() {
  const auto file_path = QFileDialog::getOpenFileName(
      nullptr,
      QStringLiteral("Choose Image"),
      QDir::homePath(),
      imageFileFilter());
  if (!file_path.isEmpty()) {
    assignPrimaryImage(file_path, QFileInfo(file_path).completeBaseName(), QString{});
  }
}

void CarouselDocumentController::pickSecondaryImage() {
  const auto file_path = QFileDialog::getOpenFileName(
      nullptr,
      QStringLiteral("Choose Secondary Image"),
      QDir::homePath(),
      imageFileFilter());
  if (!file_path.isEmpty()) {
    assignSecondaryImage(file_path, QFileInfo(file_path).completeBaseName(), QString{});
  }
}

void CarouselDocumentController::pickPrimaryVideo() {
  const auto file_path = QFileDialog::getOpenFileName(
      nullptr,
      QStringLiteral("Choose Video"),
      QDir::homePath(),
      videoFileFilter());
  if (!file_path.isEmpty()) {
    assignPrimaryVideo(file_path, QFileInfo(file_path).completeBaseName());
  }
}

void CarouselDocumentController::exportCarousel() {
  const auto directory_path =
      QFileDialog::getExistingDirectory(nullptr, QStringLiteral("Export Carousel"), QDir::homePath());
  if (directory_path.isEmpty()) {
    return;
  }

  exportCarouselToDirectory(directory_path);
}

bool CarouselDocumentController::exportCarouselToDirectory(const QString& directory_path) {
  const QDir output_dir(directory_path);
  if (!output_dir.exists()) {
    setStatus(QStringLiteral("Export folder does not exist."));
    return false;
  }

  setExportBusy(true);
  const auto size = exportPixelSize();
  bool success = true;
  QString failure_reason;
  int written_count = 0;

  for (int index = 0; index < slides_.size(); ++index) {
    const auto slide = slides_.at(index);
    const auto output_stem = QStringLiteral("carousel-%1").arg(index + 1, 2, 10, QLatin1Char('0'));

    if (const auto* group = groupForIndex(index)) {
      if (group->media.type == MediaType::Empty) {
        success = false;
        failure_reason = QStringLiteral("A linked split is missing its source image.");
        break;
      }
    } else if (slide.template_kind == SlideTemplate::SplitPair) {
      if (slide.primary.type == MediaType::Empty || slide.secondary.type == MediaType::Empty) {
        success = false;
        failure_reason = QStringLiteral("A half-and-half slide is missing one of its images.");
        break;
      }
    } else if (slide.primary.type == MediaType::Empty) {
      success = false;
      failure_reason = QStringLiteral("Slide %1 has no media assigned.").arg(index + 1);
      break;
    }

    if (groupForIndex(index) != nullptr || slide.primary.type == MediaType::Image ||
        slide.template_kind == SlideTemplate::SplitPair) {
      const auto image = renderSlideImage(index, size, false);
      const auto output_path = output_dir.filePath(output_stem + QStringLiteral(".png"));
      QImageWriter writer(output_path, "png");
      if (!writer.write(image)) {
        success = false;
        failure_reason = writer.errorString();
        break;
      }
      ++written_count;
      continue;
    }

    if (slide.primary.type == MediaType::Video) {
      const auto output_path = output_dir.filePath(output_stem + QStringLiteral(".mp4"));
      if (!exportVideoSlide(slide.primary, output_path, size)) {
        success = false;
        failure_reason = QStringLiteral("Unable to export video slide %1. Install ffmpeg to enable video export.")
                             .arg(index + 1);
        break;
      }
      ++written_count;
    }
  }

  setExportBusy(false);
  if (!success) {
    setStatus(failure_reason);
    return false;
  }

  setStatus(QStringLiteral("Exported %1 slide%2 to %3.")
                .arg(written_count)
                .arg(written_count == 1 ? QString{} : QStringLiteral("s"))
                .arg(output_dir.absolutePath()));
  return true;
}

CarouselDocumentController::Slide* CarouselDocumentController::slideAt(int index) {
  return index >= 0 && index < slides_.size() ? &slides_[index] : nullptr;
}

const CarouselDocumentController::Slide* CarouselDocumentController::slideAt(int index) const {
  return index >= 0 && index < slides_.size() ? &slides_[index] : nullptr;
}

CarouselDocumentController::LinkedGroup* CarouselDocumentController::groupForIndex(int index) {
  const auto* self = this;
  return const_cast<LinkedGroup*>(self->groupForIndex(index));
}

const CarouselDocumentController::LinkedGroup* CarouselDocumentController::groupForIndex(int index) const {
  const auto* slide = slideAt(index);
  if (slide == nullptr || slide->linked_group_id.isEmpty()) {
    return nullptr;
  }
  return groupById(slide->linked_group_id);
}

CarouselDocumentController::LinkedGroup* CarouselDocumentController::groupById(const QString& group_id) {
  const auto* self = this;
  return const_cast<LinkedGroup*>(self->groupById(group_id));
}

const CarouselDocumentController::LinkedGroup* CarouselDocumentController::groupById(const QString& group_id) const {
  for (const auto& group : groups_) {
    if (group.id == group_id) {
      return &group;
    }
  }
  return nullptr;
}

CarouselDocumentController::MediaItem* CarouselDocumentController::currentEditableMedia() {
  if (const auto* self = this; self->currentSelectionIsLinked()) {
    if (auto* group = groupForIndex(current_index_)) {
      return &group->media;
    }
  }

  auto* slide = slideAt(current_index_);
  if (slide == nullptr) {
    return nullptr;
  }
  if (active_slot_ == QStringLiteral("secondary") && slide->template_kind == SlideTemplate::SplitPair) {
    return &slide->secondary;
  }
  return &slide->primary;
}

const CarouselDocumentController::MediaItem* CarouselDocumentController::currentEditableMedia() const {
  if (currentSelectionIsLinked()) {
    if (const auto* group = groupForIndex(current_index_)) {
      return &group->media;
    }
  }

  const auto* slide = slideAt(current_index_);
  if (slide == nullptr) {
    return nullptr;
  }
  if (active_slot_ == QStringLiteral("secondary") && slide->template_kind == SlideTemplate::SplitPair) {
    return &slide->secondary;
  }
  return &slide->primary;
}

bool CarouselDocumentController::currentSelectionIsLinked() const {
  return groupForIndex(current_index_) != nullptr;
}

QString CarouselDocumentController::currentActiveSlot() const {
  if (const auto* slide = slideAt(current_index_)) {
    if (slide->template_kind != SlideTemplate::SplitPair) {
      return QStringLiteral("primary");
    }
  }
  return active_slot_;
}

QSize CarouselDocumentController::exportPixelSize() const {
  return document_format_ == QStringLiteral("5x4") ? QSize(1350, 1080) : QSize(1080, 1350);
}

QString CarouselDocumentController::previewsRoot() const {
  const auto cache_root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  const auto base = cache_root.isEmpty() ? QDir::current().filePath(QStringLiteral(".photo-editor-cache")) : cache_root;
  return QDir(base).filePath(QStringLiteral("carousel-previews/%1").arg(session_id_));
}

QString CarouselDocumentController::previewPathForSlide(int index) const {
  return QDir(previewsRoot()).filePath(QStringLiteral("slide-%1.png").arg(index + 1, 2, 10, QLatin1Char('0')));
}

QString CarouselDocumentController::versionedLocalFile(const QString& file_path, int revision) const {
  if (file_path.isEmpty()) {
    return {};
  }
  auto url = QUrl::fromLocalFile(file_path);
  if (revision > 0) {
    url.setQuery(QStringLiteral("rev=%1").arg(revision));
  }
  return url.toString();
}

CarouselDocumentController::RecipeState CarouselDocumentController::recipeStateFromJson(
    const QString& recipe_json) const {
  if (recipe_json.trimmed().isEmpty()) {
    return {};
  }

  const auto document = QJsonDocument::fromJson(recipe_json.toUtf8());
  if (!document.isObject()) {
    return {};
  }

  const auto object = document.object();
  RecipeState state;
  state.exposure = object.value(QStringLiteral("exposure")).toDouble();
  state.contrast = object.value(QStringLiteral("contrast")).toDouble();
  state.highlights = object.value(QStringLiteral("highlights")).toDouble();
  state.shadows = object.value(QStringLiteral("shadows")).toDouble();
  state.whites = object.value(QStringLiteral("whites")).toDouble();
  state.blacks = object.value(QStringLiteral("blacks")).toDouble();
  state.temperature = object.value(QStringLiteral("temperature")).toDouble();
  state.tint = object.value(QStringLiteral("tint")).toDouble();
  state.texture = object.value(QStringLiteral("texture")).toDouble();
  state.clarity = object.value(QStringLiteral("clarity")).toDouble();
  state.dehaze = object.value(QStringLiteral("dehaze")).toDouble();
  state.vibrance = object.value(QStringLiteral("vibrance")).toDouble();
  state.saturation = object.value(QStringLiteral("saturation")).toDouble();
  state.curve_shadows = object.value(QStringLiteral("curveShadows")).toDouble();
  state.curve_darks = object.value(QStringLiteral("curveDarks")).toDouble();
  state.curve_lights = object.value(QStringLiteral("curveLights")).toDouble();
  state.curve_highlights = object.value(QStringLiteral("curveHighlights")).toDouble();
  state.hsl_channel = object.value(QStringLiteral("hslChannel")).toString(QStringLiteral("Reds"));
  state.hue_adjustment = object.value(QStringLiteral("hueAdjustment")).toDouble();
  state.saturation_adjustment = object.value(QStringLiteral("saturationAdjustment")).toDouble();
  state.luminance_adjustment = object.value(QStringLiteral("luminanceAdjustment")).toDouble();
  state.crop_zoom = object.value(QStringLiteral("cropZoom")).toDouble(1.0);
  state.crop_preset = object.value(QStringLiteral("cropPreset")).toString(QStringLiteral("Original"));
  state.linear_mask_enabled = object.value(QStringLiteral("linearMaskEnabled")).toBool(false);
  state.linear_mask_exposure = object.value(QStringLiteral("linearMaskExposure")).toDouble();
  state.linear_mask_temperature = object.value(QStringLiteral("linearMaskTemperature")).toDouble();
  state.linear_mask_feather = object.value(QStringLiteral("linearMaskFeather")).toDouble(0.22);
  state.linear_mask_position = object.value(QStringLiteral("linearMaskPosition")).toDouble(0.5);
  state.linear_mask_rotation = object.value(QStringLiteral("linearMaskRotation")).toDouble();
  return state;
}

bool CarouselDocumentController::isImageFile(const QString& file_path) const {
  return suffix_in_list(file_path, image_suffixes());
}

bool CarouselDocumentController::isVideoFile(const QString& file_path) const {
  return suffix_in_list(file_path, video_suffixes());
}

QString CarouselDocumentController::imageFileFilter() const {
  return file_filter_from_suffixes(QStringLiteral("Images"), image_suffixes());
}

QString CarouselDocumentController::videoFileFilter() const {
  return file_filter_from_suffixes(QStringLiteral("Videos"), video_suffixes());
}

QString CarouselDocumentController::displayNameForPath(const QString& file_path, const QString& fallback) const {
  if (!fallback.trimmed().isEmpty()) {
    return fallback.trimmed();
  }
  const auto info = QFileInfo(file_path);
  return info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
}

QVariantMap CarouselDocumentController::slideMapForIndex(int index) const {
  QVariantMap map;
  const auto* slide = slideAt(index);
  const auto* group = groupForIndex(index);
  const bool selected = index == current_index_;

  QString primary_label;
  QString type_label = QStringLiteral("empty");
  if (group != nullptr) {
    type_label = group->media.type == MediaType::Image ? QStringLiteral("linked-image")
                                                       : group->media.type == MediaType::Video ? QStringLiteral("linked-video")
                                                                                               : QStringLiteral("linked-empty");
    primary_label = group->media.display_label;
  } else if (slide != nullptr) {
    if (slide->template_kind == SlideTemplate::SplitPair) {
      type_label = QStringLiteral("split-pair");
      primary_label = slide->primary.display_label;
    } else if (slide->primary.type == MediaType::Image) {
      type_label = QStringLiteral("image");
      primary_label = slide->primary.display_label;
    } else if (slide->primary.type == MediaType::Video) {
      type_label = QStringLiteral("video");
      primary_label = slide->primary.display_label;
    }
  }

  map.insert(QStringLiteral("index"), index);
  map.insert(QStringLiteral("slideNumber"), index + 1);
  map.insert(QStringLiteral("selected"), selected);
  map.insert(QStringLiteral("templateKind"), templateNameForIndex(index));
  map.insert(QStringLiteral("groupKind"), group != nullptr ? groupKindName(group->kind) : QStringLiteral("none"));
  map.insert(QStringLiteral("groupSpan"), group != nullptr ? group->span : 0);
  map.insert(QStringLiteral("groupPosition"), group != nullptr ? (index - group->start_index + 1) : 0);
  map.insert(QStringLiteral("previewSource"), slide != nullptr ? slide->preview_source : QString{});
  map.insert(QStringLiteral("title"), QStringLiteral("%1").arg(index + 1, 2, 10, QLatin1Char('0')));
  map.insert(QStringLiteral("mediaType"), type_label);
  map.insert(QStringLiteral("label"), primary_label);
  return map;
}

QVariantMap CarouselDocumentController::selectionMap() const {
  QVariantMap map;
  const auto* slide = slideAt(current_index_);
  const auto* group = groupForIndex(current_index_);
  const auto* media = currentEditableMedia();

  map.insert(QStringLiteral("currentIndex"), current_index_);
  map.insert(QStringLiteral("slideNumber"), current_index_ + 1);
  map.insert(QStringLiteral("isLinkedGroup"), group != nullptr);
  map.insert(QStringLiteral("groupKind"), group != nullptr ? groupKindName(group->kind) : QStringLiteral("none"));
  map.insert(QStringLiteral("groupSpan"), group != nullptr ? group->span : 0);
  map.insert(QStringLiteral("templateKind"), templateNameForIndex(current_index_));
  map.insert(QStringLiteral("activeSlot"), currentActiveSlot());
  map.insert(QStringLiteral("canUseSecondary"), slide != nullptr && slide->template_kind == SlideTemplate::SplitPair);
  map.insert(QStringLiteral("hasMedia"), media != nullptr && media->type != MediaType::Empty);
  map.insert(QStringLiteral("mediaType"),
             media == nullptr ? QStringLiteral("empty")
                              : media->type == MediaType::Image ? QStringLiteral("image")
                                                                : media->type == MediaType::Video ? QStringLiteral("video")
                                                                                                  : QStringLiteral("empty"));
  map.insert(QStringLiteral("displayLabel"), media != nullptr ? media->display_label : QString{});
  map.insert(QStringLiteral("fitMode"), media != nullptr ? fitModeName(media->fit_mode) : QStringLiteral("cover"));
  map.insert(QStringLiteral("scale"), media != nullptr ? media->scale : 1.0);
  map.insert(QStringLiteral("offsetX"), media != nullptr ? media->offset_x : 0.0);
  map.insert(QStringLiteral("offsetY"), media != nullptr ? media->offset_y : 0.0);
  map.insert(QStringLiteral("canExportVideo"), !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty());
  return map;
}

QString CarouselDocumentController::templateNameForIndex(int index) const {
  const auto* group = groupForIndex(index);
  if (group != nullptr) {
    return group->kind == GroupKind::HorizontalPair ? QStringLiteral("linked-horizontal-pair")
                                                    : QStringLiteral("linked-vertical-triptych");
  }

  const auto* slide = slideAt(index);
  if (slide == nullptr) {
    return QStringLiteral("single");
  }
  return slide->template_kind == SlideTemplate::SplitPair ? QStringLiteral("split-pair") : QStringLiteral("single");
}

QString CarouselDocumentController::fitModeName(FitMode fit_mode) const {
  return fit_mode == FitMode::Contain ? QStringLiteral("contain") : QStringLiteral("cover");
}

CarouselDocumentController::FitMode CarouselDocumentController::fitModeFromName(const QString& fit_mode) const {
  return fit_mode == QStringLiteral("contain") ? FitMode::Contain : FitMode::Cover;
}

QString CarouselDocumentController::groupKindName(GroupKind kind) const {
  switch (kind) {
    case GroupKind::HorizontalPair:
      return QStringLiteral("linked-horizontal-pair");
    case GroupKind::VerticalTriptych:
      return QStringLiteral("linked-vertical-triptych");
    case GroupKind::None:
      break;
  }
  return QStringLiteral("none");
}

int CarouselDocumentController::spanForGroupKind(GroupKind kind) const {
  switch (kind) {
    case GroupKind::HorizontalPair:
      return 2;
    case GroupKind::VerticalTriptych:
      return 3;
    case GroupKind::None:
      break;
  }
  return 0;
}

CarouselDocumentController::GroupKind CarouselDocumentController::groupKindFromTemplate(
    const QString& template_kind) const {
  if (template_kind == QStringLiteral("linked-horizontal-pair")) {
    return GroupKind::HorizontalPair;
  }
  if (template_kind == QStringLiteral("linked-vertical-triptych")) {
    return GroupKind::VerticalTriptych;
  }
  return GroupKind::None;
}

QSize CarouselDocumentController::probeVideoSize(const QString& file_path) const {
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffprobe.isEmpty()) {
    return {};
  }

  QProcess process;
  process.start(
      ffprobe,
      {
          QStringLiteral("-v"),
          QStringLiteral("error"),
          QStringLiteral("-select_streams"),
          QStringLiteral("v:0"),
          QStringLiteral("-show_entries"),
          QStringLiteral("stream=width,height"),
          QStringLiteral("-of"),
          QStringLiteral("csv=p=0:s=x"),
          file_path,
      });
  if (!process.waitForFinished(5000) || process.exitCode() != 0) {
    return {};
  }

  const auto parts = QString::fromUtf8(process.readAllStandardOutput()).trimmed().split('x');
  if (parts.size() != 2) {
    return {};
  }
  return {parts[0].toInt(), parts[1].toInt()};
}

bool CarouselDocumentController::exportVideoSlide(
    const MediaItem& media,
    const QString& output_path,
    const QSize& target_size) const {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    return false;
  }

  const auto source_size = probeVideoSize(media.file_path);
  if (!source_size.isValid()) {
    return false;
  }

  const double base_scale = media.fit_mode == FitMode::Contain
                                ? qMin(
                                      static_cast<double>(target_size.width()) / static_cast<double>(source_size.width()),
                                      static_cast<double>(target_size.height()) / static_cast<double>(source_size.height()))
                                : qMax(
                                      static_cast<double>(target_size.width()) / static_cast<double>(source_size.width()),
                                      static_cast<double>(target_size.height()) / static_cast<double>(source_size.height()));
  const int scaled_width = qMax(1, static_cast<int>(qRound(source_size.width() * base_scale * media.scale)));
  const int scaled_height = qMax(1, static_cast<int>(qRound(source_size.height() * base_scale * media.scale)));
  const int free_x = target_size.width() - scaled_width;
  const int free_y = target_size.height() - scaled_height;
  const int translate_x =
      static_cast<int>(qRound(static_cast<double>(free_x) * ((media.offset_x + 1.0) / 2.0)));
  const int translate_y =
      static_cast<int>(qRound(static_cast<double>(free_y) * ((media.offset_y + 1.0) / 2.0)));

  const auto filter = QStringLiteral(
                          "scale=%1:%2:flags=lanczos[fg];color=c=black:s=%3x%4[bg];"
                          "[bg][fg]overlay=%5:%6:format=auto,setsar=1")
                          .arg(scaled_width)
                          .arg(scaled_height)
                          .arg(target_size.width())
                          .arg(target_size.height())
                          .arg(translate_x)
                          .arg(translate_y);

  QProcess process;
  process.start(
      ffmpeg,
      {
          QStringLiteral("-y"),
          QStringLiteral("-i"),
          media.file_path,
          QStringLiteral("-vf"),
          filter,
          QStringLiteral("-an"),
          QStringLiteral("-c:v"),
          QStringLiteral("libx264"),
          QStringLiteral("-pix_fmt"),
          QStringLiteral("yuv420p"),
          output_path,
      });
  return process.waitForFinished(-1) && process.exitCode() == 0;
}

QImage CarouselDocumentController::renderSlideImage(int index, const QSize& size, bool preview) const {
  QImage canvas(size, QImage::Format_ARGB32_Premultiplied);
  canvas.fill(QColor(QStringLiteral("#0b1014")));
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  if (const auto* group = groupForIndex(index)) {
    const QSize virtual_size =
        group->kind == GroupKind::HorizontalPair ? QSize(size.width() * group->span, size.height())
                                                 : QSize(size.width(), size.height() * group->span);
    const QPoint origin =
        group->kind == GroupKind::HorizontalPair
            ? QPoint((index - group->start_index) * size.width(), 0)
            : QPoint(0, (index - group->start_index) * size.height());
    renderMediaIntoRect(&painter, QRect(QPoint(0, 0), size), group->media, virtual_size, origin, preview);

    if (preview) {
      QPen guide_pen(QColor(QStringLiteral("#f0b35d")));
      guide_pen.setStyle(Qt::DashLine);
      guide_pen.setWidth(2);
      painter.setPen(guide_pen);
      painter.drawRect(canvas.rect().adjusted(2, 2, -2, -2));
      painter.setPen(QColor(QStringLiteral("#d2dce2")));
      painter.setFont(QFont(QStringLiteral("Menlo"), 18, QFont::DemiBold));
      painter.drawText(canvas.rect().adjusted(16, 16, -16, -16), Qt::AlignTop | Qt::AlignLeft,
                       QStringLiteral("%1 / %2").arg(index - group->start_index + 1).arg(group->span));
    }
    return canvas;
  }

  const auto* slide = slideAt(index);
  if (slide == nullptr) {
    return canvas;
  }

  if (slide->template_kind == SlideTemplate::SplitPair) {
    const QRect left_rect(0, 0, size.width() / 2, size.height());
    const QRect right_rect(left_rect.width(), 0, size.width() - left_rect.width(), size.height());
    renderMediaIntoRect(&painter, left_rect, slide->primary, left_rect.size(), QPoint(0, 0), preview);
    renderMediaIntoRect(&painter, right_rect, slide->secondary, right_rect.size(), QPoint(0, 0), preview);
    if (preview) {
      painter.setPen(QPen(QColor(QStringLiteral("#f0b35d")), 2));
      painter.drawLine(left_rect.right(), 0, left_rect.right(), size.height());
    }
    return canvas;
  }

  renderMediaIntoRect(&painter, QRect(QPoint(0, 0), size), slide->primary, size, QPoint(0, 0), preview);
  return canvas;
}

QImage CarouselDocumentController::renderVideoPlaceholder(const MediaItem& media, const QSize& size) const {
  QImage canvas(size, QImage::Format_ARGB32_Premultiplied);
  canvas.fill(QColor(QStringLiteral("#0d1419")));
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::Antialiasing, true);

  QRect frame = canvas.rect().adjusted(size.width() / 10, size.height() / 5, -size.width() / 10, -size.height() / 5);
  painter.setPen(QPen(QColor(QStringLiteral("#69cad8")), 3));
  painter.drawRoundedRect(frame, 18, 18);
  const QPointF points[] = {
      QPointF(frame.center().x() - 36, frame.center().y() - 48),
      QPointF(frame.center().x() - 36, frame.center().y() + 48),
      QPointF(frame.center().x() + 54, frame.center().y()),
  };
  painter.setBrush(QColor(QStringLiteral("#69cad8")));
  painter.drawPolygon(points, 3);

  painter.setPen(QColor(QStringLiteral("#eef4f6")));
  painter.setFont(QFont(QStringLiteral("Avenir Next"), qMax(14, size.width() / 18), QFont::Medium));
  painter.drawText(
      canvas.rect().adjusted(24, size.height() - 148, -24, -24),
      Qt::AlignHCenter | Qt::TextWordWrap,
      media.display_label.isEmpty() ? QStringLiteral("Video") : media.display_label);
  painter.setPen(QColor(QStringLiteral("#8ea2ae")));
  painter.setFont(QFont(QStringLiteral("Menlo"), qMax(11, size.width() / 28)));
  painter.drawText(
      canvas.rect().adjusted(24, size.height() - 72, -24, -24),
      Qt::AlignHCenter,
      QStringLiteral("Video export uses ffmpeg"));
  return canvas;
}

bool CarouselDocumentController::decodeEditedImage(
    const MediaItem& media,
    QImage* image,
    bool preview,
    int max_edge) const {
  if (media.type != MediaType::Image) {
    return false;
  }

  QImageReader reader(media.file_path);
  if (!reader.canRead()) {
    return false;
  }

  if (preview && max_edge > 0) {
    const auto source_size = reader.size();
    if (source_size.isValid()) {
      reader.setScaledSize(source_size.scaled(max_edge, max_edge, Qt::KeepAspectRatio));
    }
  }

  *image = reader.read().convertToFormat(QImage::Format_ARGB32);
  if (image->isNull()) {
    return false;
  }

  if (media.recipe_state.crop_preset != QStringLiteral("Original") || !qFuzzyCompare(media.recipe_state.crop_zoom, 1.0)) {
    *image = image->copy(crop_rect_for_image(image->size(), media.recipe_state.crop_preset, media.recipe_state.crop_zoom));
  }
  apply_color_adjustments(image, media.recipe_state);
  return !image->isNull();
}

void CarouselDocumentController::renderMediaIntoRect(
    QPainter* painter,
    const QRect& destination_rect,
    const MediaItem& media,
    const QSize& virtual_canvas_size,
    const QPoint& segment_origin,
    bool preview) const {
  painter->save();
  painter->setClipRect(destination_rect);
  painter->fillRect(destination_rect, QColor(QStringLiteral("#0d1419")));

  if (media.type == MediaType::Empty || media.file_path.isEmpty()) {
    painter->setPen(QPen(QColor(QStringLiteral("#223340")), 2, Qt::DashLine));
    painter->drawRoundedRect(destination_rect.adjusted(8, 8, -8, -8), 16, 16);
    painter->setPen(QColor(QStringLiteral("#627783")));
    painter->setFont(QFont(QStringLiteral("Menlo"), qMax(10, destination_rect.width() / 28)));
    painter->drawText(destination_rect.adjusted(18, 18, -18, -18), Qt::AlignCenter | Qt::TextWordWrap,
                      QStringLiteral("Add media"));
    painter->restore();
    return;
  }

  if (media.type == MediaType::Video) {
    const auto placeholder = renderVideoPlaceholder(media, destination_rect.size());
    painter->drawImage(destination_rect.topLeft(), placeholder);
    painter->restore();
    return;
  }

  QImage source_image;
  if (!decodeEditedImage(media, &source_image, preview, kPreviewLongEdge * 2)) {
    painter->setPen(QColor(QStringLiteral("#a56a5e")));
    painter->drawText(destination_rect.adjusted(18, 18, -18, -18), Qt::AlignCenter | Qt::TextWordWrap,
                      QStringLiteral("Unable to read image"));
    painter->restore();
    return;
  }

  const double base_scale = media.fit_mode == FitMode::Contain
                                ? qMin(
                                      static_cast<double>(virtual_canvas_size.width()) / static_cast<double>(source_image.width()),
                                      static_cast<double>(virtual_canvas_size.height()) / static_cast<double>(source_image.height()))
                                : qMax(
                                      static_cast<double>(virtual_canvas_size.width()) / static_cast<double>(source_image.width()),
                                      static_cast<double>(virtual_canvas_size.height()) / static_cast<double>(source_image.height()));
  const int scaled_width = qMax(1, static_cast<int>(qRound(source_image.width() * base_scale * media.scale)));
  const int scaled_height = qMax(1, static_cast<int>(qRound(source_image.height() * base_scale * media.scale)));
  const int free_x = virtual_canvas_size.width() - scaled_width;
  const int free_y = virtual_canvas_size.height() - scaled_height;
  const int position_x =
      static_cast<int>(qRound(static_cast<double>(free_x) * ((media.offset_x + 1.0) / 2.0)));
  const int position_y =
      static_cast<int>(qRound(static_cast<double>(free_y) * ((media.offset_y + 1.0) / 2.0)));

  const auto scaled_image =
      source_image.scaled(scaled_width, scaled_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  painter->drawImage(
      destination_rect.left() + position_x - segment_origin.x(),
      destination_rect.top() + position_y - segment_origin.y(),
      scaled_image);
  painter->restore();
}

void CarouselDocumentController::ensureCurrentIndexValid() {
  current_index_ = qBound(0, current_index_, qMax(0, static_cast<int>(slides_.size()) - 1));
  normalizeForCurrentTemplate();
}

void CarouselDocumentController::refreshPreviews() {
  QDir().mkpath(previewsRoot());
  ++preview_revision_;
  const auto export_size = exportPixelSize();
  const auto preview_size = export_size.scaled(kPreviewLongEdge, kPreviewLongEdge, Qt::KeepAspectRatio);
  for (int index = 0; index < slides_.size(); ++index) {
    writePreviewForSlide(index, renderSlideImage(index, preview_size, true));
  }
}

void CarouselDocumentController::emitSelectionAndSlidesChanged() {
  emit slidesChanged();
  emit selectionChanged();
}

void CarouselDocumentController::setStatus(const QString& status) {
  if (status_ == status) {
    return;
  }
  status_ = status;
  emit statusChanged();
}

void CarouselDocumentController::setExportBusy(bool export_busy) {
  if (export_busy_ == export_busy) {
    return;
  }
  export_busy_ = export_busy;
  emit exportBusyChanged();
}

void CarouselDocumentController::assignImageToCurrentSlot(
    const QString& file_path,
    const QString& display_label,
    const QString& recipe_json,
    bool secondary) {
  const auto normalized = normalized_path(file_path);
  if (!isImageFile(normalized)) {
    setStatus(QStringLiteral("That file is not a supported image format."));
    return;
  }

  if (currentSelectionIsLinked()) {
    auto* group = groupForIndex(current_index_);
    if (group == nullptr) {
      return;
    }
    group->media = MediaItem{
        .type = MediaType::Image,
        .file_path = normalized,
        .display_label = displayNameForPath(normalized, display_label),
        .recipe_json = recipe_json,
        .recipe_state = recipeStateFromJson(recipe_json),
        .fit_mode = FitMode::Cover,
        .scale = 1.0,
        .offset_x = 0.0,
        .offset_y = 0.0,
    };
    refreshPreviews();
    emitSelectionAndSlidesChanged();
    setStatus(QStringLiteral("Updated the linked split source image."));
    return;
  }

  auto* slide = slideAt(current_index_);
  if (slide == nullptr) {
    return;
  }

  if (secondary && slide->template_kind != SlideTemplate::SplitPair) {
    slide->template_kind = SlideTemplate::SplitPair;
  }

  MediaItem media{
      .type = MediaType::Image,
      .file_path = normalized,
      .display_label = displayNameForPath(normalized, display_label),
      .recipe_json = recipe_json,
      .recipe_state = recipeStateFromJson(recipe_json),
      .fit_mode = FitMode::Cover,
      .scale = 1.0,
      .offset_x = 0.0,
      .offset_y = 0.0,
  };
  if (secondary && slide->template_kind == SlideTemplate::SplitPair) {
    slide->secondary = media;
    active_slot_ = QStringLiteral("secondary");
    setStatus(QStringLiteral("Added an image to the secondary half."));
  } else {
    slide->primary = media;
    active_slot_ = QStringLiteral("primary");
    setStatus(QStringLiteral("Added an image to the current slide."));
  }

  refreshPreviews();
  emitSelectionAndSlidesChanged();
}

void CarouselDocumentController::removeGroupAtIndex(int index, bool keep_current_media) {
  const auto* group = groupForIndex(index);
  if (group == nullptr) {
    return;
  }

  const auto group_copy = *group;
  clearGroupIndices(group_copy);
  for (qsizetype group_index = groups_.size(); group_index > 0; --group_index) {
    if (groups_[group_index - 1].id == group_copy.id) {
      groups_.removeAt(group_index - 1);
    }
  }

  if (keep_current_media) {
    current_index_ = group_copy.start_index;
    if (group_copy.span > 1) {
      slides_.remove(group_copy.start_index + 1, group_copy.span - 1);
    }

    auto* slide = slideAt(current_index_);
    if (slide != nullptr) {
      slide->template_kind = SlideTemplate::Single;
      slide->primary = group_copy.media;
      slide->secondary = MediaItem{};
      slide->linked_group_id.clear();
    }
  } else {
    for (int group_index = group_copy.start_index;
         group_index < group_copy.start_index + group_copy.span && group_index < slides_.size();
         ++group_index) {
      if (auto* slide = slideAt(group_index)) {
        slide->linked_group_id.clear();
        slide->template_kind = SlideTemplate::Single;
        slide->secondary = MediaItem{};
      }
    }
  }

  rebuildGroupIndices();
}

void CarouselDocumentController::clearGroupIndices(const LinkedGroup& group) {
  for (int index = group.start_index; index < group.start_index + group.span && index < slides_.size(); ++index) {
    auto* slide = slideAt(index);
    if (slide != nullptr) {
      slide->linked_group_id.clear();
    }
  }
}

void CarouselDocumentController::rebuildGroupIndices() {
  for (qsizetype group_index = groups_.size(); group_index > 0; --group_index) {
    auto& group = groups_[group_index - 1];
    int first_index = -1;
    int span = 0;
    for (int slide_index = 0; slide_index < slides_.size(); ++slide_index) {
      if (slides_[slide_index].linked_group_id == group.id) {
        if (first_index < 0) {
          first_index = slide_index;
        }
        ++span;
      }
    }

    if (first_index < 0 || span == 0) {
      groups_.removeAt(group_index - 1);
      continue;
    }

    group.start_index = first_index;
    group.span = span;
  }
}

bool CarouselDocumentController::ensureCapacityForGroup(int start_index, int span) {
  const int additional_slides = qMax(0, span - 1);
  if (static_cast<int>(slides_.size()) + additional_slides > kMaxSlides) {
    return false;
  }

  for (int offset = 0; offset < additional_slides; ++offset) {
    slides_.insert(start_index + 1 + offset, Slide{});
  }

  rebuildGroupIndices();
  return true;
}

void CarouselDocumentController::resetSlideForSingle(int index) {
  auto* slide = slideAt(index);
  if (slide == nullptr) {
    return;
  }
  slide->linked_group_id.clear();
  slide->template_kind = SlideTemplate::Single;
  slide->secondary = MediaItem{};
}

void CarouselDocumentController::normalizeForCurrentTemplate() {
  const auto* slide = slideAt(current_index_);
  if (slide == nullptr || slide->template_kind != SlideTemplate::SplitPair) {
    active_slot_ = QStringLiteral("primary");
  }
}

void CarouselDocumentController::writePreviewForSlide(int index, const QImage& image) {
  const auto path = previewPathForSlide(index);
  QImageWriter writer(path, "png");
  writer.write(image);
  if (auto* slide = slideAt(index)) {
    slide->preview_source = versionedLocalFile(path, preview_revision_);
  }
}

}  // namespace photo_editor::desktop
