#include "carousel_document_controller.h"

#include "app_bootstrap.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "raw_image_decoder.h"
#include "working_image.h"

#include <QColor>
#include <QColorSpace>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QPen>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QtMath>
#include <algorithm>
#include <array>
#include <vector>

namespace photo_editor::desktop {

namespace {

constexpr int kMaxSlides = 20;
constexpr int kPreviewLongEdge = 1600;
constexpr double kMinItemSize = 36.0;
constexpr double kMinTextPointSize = 12.0;
constexpr auto kTextFontFamily = "Avenir Next";

QImage normalize_preview_color_space(QImage image);
QColorSpace preview_target_color_space() {
  return preview_color_space();
}

QColorSpace output_color_space_for_name(const QString& name) {
  const auto normalized = normalized_output_color_space_name(name);
  if (normalized == QStringLiteral("Display P3")) {
    return QColorSpace(QColorSpace::DisplayP3);
  }
  return preview_target_color_space();
}

void apply_output_color_space_transform(QImage* image, const QString& output_color_space_name) {
  if (image == nullptr || image->isNull()) {
    return;
  }

  const auto target_color_space = output_color_space_for_name(output_color_space_name);
  if (!target_color_space.isValid()) {
    return;
  }

  *image = finalize_output_image(std::move(*image), target_color_space);
}

QImage normalize_preview_color_space(QImage image) {
  if (image.isNull()) {
    return image;
  }

  if (!image.colorSpace().isValid()) {
    image.setColorSpace(preview_target_color_space());
  }
  return image;
}

double clamp_unit_interval(double value) {
  return qBound(-1.0, value, 1.0);
}

double clamp_zoom_scale(double value) {
  return qBound(0.6, value, 3.5);
}

QString normalized_path(const QString& path) {
  if (path.trimmed().isEmpty()) {
    return {};
  }
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QStringList image_suffixes() {
  return {
      QStringLiteral("3fr"),
      QStringLiteral("arw"),
      QStringLiteral("avif"),
      QStringLiteral("bmp"),
      QStringLiteral("cr2"),
      QStringLiteral("cr3"),
      QStringLiteral("dng"),
      QStringLiteral("erf"),
      QStringLiteral("gif"),
      QStringLiteral("heic"),
      QStringLiteral("heif"),
      QStringLiteral("iiq"),
      QStringLiteral("jpeg"),
      QStringLiteral("jpg"),
      QStringLiteral("kdc"),
      QStringLiteral("mef"),
      QStringLiteral("mos"),
      QStringLiteral("nef"),
      QStringLiteral("nrw"),
      QStringLiteral("orf"),
      QStringLiteral("pef"),
      QStringLiteral("png"),
      QStringLiteral("raf"),
      QStringLiteral("raw"),
      QStringLiteral("rw2"),
      QStringLiteral("sr2"),
      QStringLiteral("srf"),
      QStringLiteral("tif"),
      QStringLiteral("tiff"),
      QStringLiteral("webp"),
      QStringLiteral("x3f"),
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

QString file_filter_from_suffixes(const QString& label, const QStringList& suffixes) {
  QStringList patterns;
  patterns.reserve(suffixes.size());
  for (const auto& suffix : suffixes) {
    patterns.push_back(QStringLiteral("*.%1").arg(suffix));
  }
  return QStringLiteral("%1 (%2)").arg(label, patterns.join(' '));
}

double crop_aspect_ratio(const QString& preset) {
  if (preset == QStringLiteral("Square")) {
    return 1.0;
  }
  if (preset == QStringLiteral("4x5")) {
    return 4.0 / 5.0;
  }
  if (preset == QStringLiteral("3x2")) {
    return 3.0 / 2.0;
  }
  if (preset == QStringLiteral("16x9")) {
    return 16.0 / 9.0;
  }
  return 0.0;
}

bool has_explicit_crop_rect(const CarouselDocumentController::RecipeState& state) {
  return state.crop_rect_width > 0.0 && state.crop_rect_height > 0.0;
}

QRect legacy_crop_rect_for_image(const QSize& size, const QString& preset, double crop_zoom) {
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

QRect crop_rect_for_image(const QSize& size, const CarouselDocumentController::RecipeState& state) {
  if (has_explicit_crop_rect(state)) {
    const auto width = qBound(0.05, state.crop_rect_width, 1.0);
    const auto height = qBound(0.05, state.crop_rect_height, 1.0);
    const auto x = qBound(0.0, state.crop_rect_x, 1.0 - width);
    const auto y = qBound(0.0, state.crop_rect_y, 1.0 - height);
    const auto pixel_x = qBound(0, static_cast<int>(qRound(x * static_cast<double>(size.width()))), size.width() - 1);
    const auto pixel_y = qBound(0, static_cast<int>(qRound(y * static_cast<double>(size.height()))), size.height() - 1);
    const auto pixel_width =
        qBound(1, static_cast<int>(qRound(width * static_cast<double>(size.width()))), size.width() - pixel_x);
    const auto pixel_height =
        qBound(1, static_cast<int>(qRound(height * static_cast<double>(size.height()))), size.height() - pixel_y);
    return {pixel_x, pixel_y, pixel_width, pixel_height};
  }

  return legacy_crop_rect_for_image(size, state.crop_preset, state.crop_zoom);
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
      .color_mixer_channels = [&]() {
        std::array<photo_editor::pipeline::ColorMixerChannelAdjustment,
                   photo_editor::pipeline::kColorMixerChannelCount>
            channels{};
        for (std::size_t i = 0; i < channels.size(); ++i) {
          channels[i].hue = state.mixer_channels[i].hue;
          channels[i].saturation = state.mixer_channels[i].saturation;
          channels[i].luminance = state.mixer_channels[i].luminance;
        }
        return channels;
      }(),
      .linear_mask_enabled = state.linear_mask_enabled,
      .linear_mask_exposure = state.linear_mask_exposure,
      .linear_mask_temperature = state.linear_mask_temperature,
      .linear_mask_feather = state.linear_mask_feather,
      .linear_mask_position = state.linear_mask_position,
      .linear_mask_rotation = state.linear_mask_rotation,
      .radial_mask_enabled = state.radial_mask_enabled,
      .radial_mask_exposure = state.radial_mask_exposure,
      .radial_mask_temperature = state.radial_mask_temperature,
      .radial_mask_feather = state.radial_mask_feather,
      .radial_mask_center_x = state.radial_mask_center_x,
      .radial_mask_center_y = state.radial_mask_center_y,
      .radial_mask_radius_x = state.radial_mask_radius_x,
      .radial_mask_radius_y = state.radial_mask_radius_y,
      .radial_mask_rotation = state.radial_mask_rotation,
  };
}

void apply_color_adjustments(QImage* image, const CarouselDocumentController::RecipeState& state) {
  if (image == nullptr || image->isNull()) {
    return;
  }

  auto working = working_image_from_qimage(*image);
  if (working.isNull()) {
    return;
  }

  photo_editor::pipeline::apply_color_adjustments(
      &working.pixels, working.width, working.height, to_pipeline_state(state));

  *image = qimage_from_working_image(working, preview_target_color_space());
  apply_output_color_space_transform(image, state.output_color_space);
}

QString summary_label(const CarouselDocumentController::CanvasItem& item) {
  if (item.kind == CarouselDocumentController::ItemKind::Text) {
    const auto trimmed = item.text.simplified();
    return trimmed.isEmpty() ? QStringLiteral("Text") : trimmed.left(28);
  }
  if (item.kind == CarouselDocumentController::ItemKind::Video) {
    if (!item.display_label.trimmed().isEmpty()) {
      return item.display_label.trimmed();
    }
    const QFileInfo info(item.file_path);
    return info.completeBaseName().isEmpty() ? QStringLiteral("Video") : info.completeBaseName();
  }
  if (!item.display_label.trimmed().isEmpty()) {
    return item.display_label.trimmed();
  }
  const QFileInfo info(item.file_path);
  return info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
}

QString summary_label(const CarouselDocumentController::MediaItem& item) {
  if (!item.display_label.trimmed().isEmpty()) {
    return item.display_label.trimmed();
  }
  const QFileInfo info(item.file_path);
  return info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
}

QString normalized_document_name(const QString& value) {
  const auto trimmed = value.simplified();
  return trimmed.isEmpty() ? QStringLiteral("Untitled Carousel") : trimmed;
}

QString carousel_documents_root() {
  return QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("carousels"));
}

QString carousel_document_index_path() {
  return QDir(carousel_documents_root()).filePath(QStringLiteral("index.json"));
}

QString carousel_document_file_path(const QString& document_id) {
  return QDir(carousel_documents_root())
      .filePath(QStringLiteral("documents/%1.json").arg(document_id));
}

QJsonObject legacy_document_entry(
    const QString& document_id,
    const QString& name,
    const QString& document_format,
    int slide_count,
    const QString& created_at,
    const QString& updated_at) {
  QJsonObject entry;
  entry.insert(QStringLiteral("documentId"), document_id);
  entry.insert(QStringLiteral("name"), name);
  entry.insert(QStringLiteral("documentFormat"), document_format);
  entry.insert(QStringLiteral("slideCount"), slide_count);
  entry.insert(QStringLiteral("createdAt"), created_at);
  entry.insert(QStringLiteral("updatedAt"), updated_at);
  return entry;
}

QJsonArray load_legacy_document_index() {
  QFile file(carousel_document_index_path());
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return {};
  }

  const auto parsed = QJsonDocument::fromJson(file.readAll());
  if (!parsed.isObject()) {
    return {};
  }
  return parsed.object().value(QStringLiteral("documents")).toArray();
}

bool save_legacy_document_index(const QJsonArray& documents) {
  QDir().mkpath(QFileInfo(carousel_document_index_path()).absolutePath());
  QJsonObject root;
  root.insert(QStringLiteral("documents"), documents);
  QSaveFile file(carousel_document_index_path());
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  return file.commit();
}

QJsonObject load_legacy_document_payload(const QString& document_id) {
  QFile file(carousel_document_file_path(document_id));
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return {};
  }

  const auto parsed = QJsonDocument::fromJson(file.readAll());
  return parsed.isObject() ? parsed.object() : QJsonObject{};
}

bool save_legacy_document_payload(const QString& document_id, const QJsonObject& payload) {
  QDir().mkpath(QFileInfo(carousel_document_file_path(document_id)).absolutePath());
  QSaveFile file(carousel_document_file_path(document_id));
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(QJsonDocument(payload).toJson(QJsonDocument::Indented));
  return file.commit();
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

QVariantList CarouselDocumentController::canvasItems() const {
  QVariantList values;
  values.reserve(items_.size());
  for (const auto& item : items_) {
    values.push_back(itemMap(item));
  }
  return values;
}

QVariantMap CarouselDocumentController::selectionDetails() const {
  return selectionMap();
}

int CarouselDocumentController::slideCount() const { return static_cast<int>(slides_.size()); }

int CarouselDocumentController::currentIndex() const { return current_index_; }

int CarouselDocumentController::documentPixelWidth() const { return exportPixelSize().width() * slideCount(); }

int CarouselDocumentController::documentPixelHeight() const { return exportPixelSize().height(); }

QString CarouselDocumentController::documentFormat() const { return document_format_; }

bool CarouselDocumentController::canAddSlide() const { return slides_.size() < kMaxSlides; }

QString CarouselDocumentController::status() const { return status_; }

bool CarouselDocumentController::exportBusy() const { return export_busy_; }

QString CarouselDocumentController::selectedItemId() const { return selected_item_id_; }

QString CarouselDocumentController::documentName() const { return document_name_; }

QString CarouselDocumentController::savedDocumentId() const { return saved_document_id_; }

QStringList CarouselDocumentController::templateOptions() const {
  return {
      QStringLiteral("single"),
      QStringLiteral("split-pair"),
      QStringLiteral("linked-horizontal-pair"),
      QStringLiteral("stacked-triptych"),
  };
}

QStringList CarouselDocumentController::fitModeOptions() const {
  return {QStringLiteral("cover"), QStringLiteral("contain")};
}

QStringList CarouselDocumentController::slotOptions() const {
  return {QStringLiteral("primary"), QStringLiteral("secondary"), QStringLiteral("tertiary")};
}

void CarouselDocumentController::setDocumentFormat(const QString& document_format) {
  const auto normalized = document_format == QStringLiteral("5x4") ? QStringLiteral("5x4") : QStringLiteral("4x5");
  if (document_format_ == normalized) {
    return;
  }

  const auto old_slide_size = exportPixelSize();
  document_format_ = normalized;
  const auto new_slide_size = exportPixelSize();
  adjustItemsForFormatChange(old_slide_size, new_slide_size);
  refreshPreviews();
  emit documentFormatChanged();
  emitCanvasStateChanged(true);
  setStatus(
      QStringLiteral("Canvas format set to %1.")
          .arg(normalized == QStringLiteral("4x5") ? QStringLiteral("1080x1350") : QStringLiteral("1350x1080")));
}

void CarouselDocumentController::setDocumentName(const QString& document_name) {
  const auto normalized = normalized_document_name(document_name);
  if (document_name_ == normalized) {
    return;
  }
  document_name_ = normalized;
  emit documentStateChanged();
}

void CarouselDocumentController::selectSlide(int index) {
  if (index < 0 || index >= slides_.size()) {
    return;
  }
  current_index_ = index;
  normalizeSelection();
  if (const auto* item = currentItem(); item == nullptr || slideIndexForItem(*item) != current_index_) {
    setSelectedItemIds({});
  }
  normalizeForCurrentTemplate();
  emitCanvasStateChanged(true);
}

void CarouselDocumentController::addSlide() {
  if (!canAddSlide()) {
    setStatus(QStringLiteral("Instagram carousels are capped at 20 canvases."));
    return;
  }

  slides_.push_back(Slide{});
  current_index_ = static_cast<int>(slides_.size()) - 1;
  refreshPreviews();
  emitCanvasStateChanged(true);
  setStatus(QStringLiteral("Added canvas %1.").arg(current_index_ + 1));
}

void CarouselDocumentController::removeCurrentSlide() {
  if (slides_.size() <= 1) {
    setStatus(QStringLiteral("The carousel needs at least one canvas."));
    return;
  }

  int start_index = current_index_;
  int span = 1;
  QString group_id;
  if (const auto* group = groupForIndex(current_index_)) {
    start_index = group->start_index;
    span = group->span;
    group_id = group->id;
  }

  const auto slide_width = exportPixelSize().width();
  const auto removed_left = static_cast<double>(start_index * slide_width);
  const auto removed_right = removed_left + static_cast<double>(span * slide_width);
  for (qsizetype index = items_.size(); index > 0; --index) {
    auto& item = items_[index - 1];
    const auto center_x = item.bounds.center().x();
    if (center_x >= removed_left && center_x < removed_right) {
      items_.removeAt(index - 1);
      continue;
    }
    if (center_x >= removed_right) {
      item.bounds.translate(-static_cast<double>(slide_width * span), 0.0);
    }
  }

  if (!group_id.isEmpty()) {
    slides_.remove(start_index, span);
    for (qsizetype index = groups_.size(); index > 0; --index) {
      if (groups_[index - 1].id == group_id) {
        groups_.removeAt(index - 1);
      }
    }
  } else {
    slides_.removeAt(start_index);
  }

  if (slides_.isEmpty()) {
    slides_.push_back(Slide{});
  }

  rebuildGroupIndices();
  current_index_ = qBound(0, start_index, static_cast<int>(slides_.size()) - 1);
  normalizeSelection();
  normalizeForCurrentTemplate();
  refreshPreviews();
  emitCanvasStateChanged(true);
  setStatus(QStringLiteral("Removed the current canvas."));
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
    adjustItemsForBlockMove(moved_group.start_index, moved_group.span, destination);
    current_index_ = destination;
  } else {
    if (current_index_ == 0) {
      return;
    }
    const auto from_index = current_index_;
    const auto to_index = current_index_ - 1;
    slides_.move(from_index, to_index);
    adjustItemsForSlideSwap(to_index, from_index);
    current_index_ = to_index;
  }

  rebuildGroupIndices();
  refreshPreviews();
  emitCanvasStateChanged(true);
  setStatus(QStringLiteral("Moved the current canvas earlier in the carousel."));
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
    adjustItemsForBlockMove(moved_group.start_index, moved_group.span, destination);
    current_index_ = destination;
  } else {
    if (current_index_ >= slides_.size() - 1) {
      return;
    }
    const auto from_index = current_index_;
    const auto to_index = current_index_ + 1;
    slides_.move(from_index, to_index);
    adjustItemsForSlideSwap(from_index, to_index);
    current_index_ = to_index;
  }

  rebuildGroupIndices();
  refreshPreviews();
  emitCanvasStateChanged(true);
  setStatus(QStringLiteral("Moved the current canvas later in the carousel."));
}

void CarouselDocumentController::addPhotoItem(
    const QString& file_path,
    const QString& display_label,
    const QString& recipe_json) {
  const auto normalized = normalized_path(file_path);
  if (!isImageFile(normalized)) {
    setStatus(QStringLiteral("That file is not a supported image format."));
    return;
  }

  QSizeF source_size;
  if (native_image_supported(normalized)) {
    const auto native_size = native_image_size(normalized);
    if (native_size.isValid()) {
      source_size = QSizeF(native_size);
    }
  } else {
    QImageReader reader(normalized);
    if (reader.canRead()) {
      source_size = reader.size().isValid() ? QSizeF(reader.size()) : QSizeF(1200.0, 1200.0);
    } else {
      const auto raw_size = raw_image_size(normalized);
      if (raw_size.isValid()) {
        source_size = QSizeF(raw_size);
      }
    }
  }

  if (!source_size.isValid()) {
    setStatus(QStringLiteral("Unable to read that image file."));
    return;
  }
  const auto slide_rect = slideRectInDocument(current_index_);
  const auto max_width = slide_rect.width() * 0.72;
  const auto max_height = slide_rect.height() * 0.72;
  QSizeF fitted = source_size;
  fitted.scale(max_width, max_height, Qt::KeepAspectRatio);

  CanvasItem item;
  item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  item.kind = ItemKind::Image;
  item.file_path = normalized;
  item.display_label = displayNameForPath(normalized, display_label);
  item.recipe_json = recipe_json;
  item.recipe_state = recipeStateFromJson(recipe_json);
  item.fit_mode = FitMode::Cover;
  item.source_size = source_size;
  item.bounds = QRectF(0.0, 0.0, fitted.width(), fitted.height());
  item.bounds.moveCenter(slide_rect.center());

  items_.push_back(item);
  setSelectedItemId(item.id);
  emitCanvasStateChanged(true);
  setStatus(QStringLiteral("Added a freeform photo layer to canvas %1.").arg(current_index_ + 1));
}

void CarouselDocumentController::addVideoItem(const QString& file_path, const QString& display_label) {
  const auto normalized = normalized_path(file_path);
  if (!isVideoFile(normalized)) {
    setStatus(QStringLiteral("That file is not a supported video format."));
    return;
  }

  QSizeF source_size = QSizeF(probeVideoSize(normalized));
  if (!source_size.isValid()) {
    source_size = QSizeF(1080.0, 1920.0);
  }

  const auto slide_rect = slideRectInDocument(current_index_);
  const auto max_width = slide_rect.width() * 0.58;
  const auto max_height = slide_rect.height() * 0.62;
  QSizeF fitted = source_size;
  fitted.scale(max_width, max_height, Qt::KeepAspectRatio);

  CanvasItem item;
  item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  item.kind = ItemKind::Video;
  item.file_path = normalized;
  item.display_label = displayNameForPath(normalized, display_label);
  item.fit_mode = FitMode::Contain;
  item.source_size = source_size;
  item.bounds = QRectF(0.0, 0.0, fitted.width(), fitted.height());
  item.bounds.moveCenter(slide_rect.center());

  items_.push_back(item);
  setSelectedItemId(item.id);
  emitCanvasStateChanged(true);
  setStatus(QStringLiteral("Added a freeform video layer to canvas %1.").arg(current_index_ + 1));
}

void CarouselDocumentController::addTextItem(const QString& text) {
  CanvasItem item;
  item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  item.kind = ItemKind::Text;
  item.text = text.trimmed().isEmpty() ? QStringLiteral("New Text") : text;
  item.display_label = item.text.left(24);
  item.text_point_size = qMax(48.0, documentPixelHeight() * 0.07);
  item.bounds = QRectF(0.0, 0.0, exportPixelSize().width() * 0.62, 10.0);
  item.bounds.setHeight(measuredTextHeight(item));
  item.bounds.moveCenter(slideRectInDocument(current_index_).center());

  items_.push_back(item);
  setSelectedItemId(item.id);
  emitCanvasStateChanged(true);
  setStatus(QStringLiteral("Added a text layer to canvas %1.").arg(current_index_ + 1));
}

void CarouselDocumentController::pickPhotoItem() {
  const auto file_path = QFileDialog::getOpenFileName(
      nullptr,
      QStringLiteral("Choose Photo"),
      QDir::homePath(),
      imageFileFilter());
  if (!file_path.isEmpty()) {
    addPhotoItem(file_path, QFileInfo(file_path).completeBaseName(), QString{});
  }
}

void CarouselDocumentController::pickVideoItem() {
  const auto file_path = QFileDialog::getOpenFileName(
      nullptr,
      QStringLiteral("Choose Video Layer"),
      QDir::homePath(),
      videoFileFilter());
  if (!file_path.isEmpty()) {
    addVideoItem(file_path, QFileInfo(file_path).completeBaseName());
  }
}

void CarouselDocumentController::selectItem(const QString& item_id) {
  const auto index = indexOfItem(item_id);
  if (index < 0) {
    return;
  }

  setSelectedItemId(items_.at(index).id);
  current_index_ = slideIndexForItem(items_.at(index));
  emitCanvasStateChanged(true);
}

void CarouselDocumentController::addItemToSelection(const QString& item_id) {
  const auto index = indexOfItem(item_id);
  if (index < 0) {
    return;
  }

  const auto item_slide = slideIndexForItem(items_.at(index));
  const auto selection_slide = currentSelectionSlideIndex();
  if (selection_slide >= 0 && item_slide != selection_slide) {
    setSelectedItemId(items_.at(index).id);
  } else {
    auto next_ids = selected_item_ids_;
    next_ids.removeAll(items_.at(index).id);
    next_ids.push_back(items_.at(index).id);
    setSelectedItemIds(next_ids, items_.at(index).id);
  }
  current_index_ = item_slide;
  emitCanvasStateChanged(true);
}

void CarouselDocumentController::toggleItemSelection(const QString& item_id) {
  const auto index = indexOfItem(item_id);
  if (index < 0) {
    return;
  }

  const auto item_slide = slideIndexForItem(items_.at(index));
  const auto selection_slide = currentSelectionSlideIndex();
  if (selection_slide >= 0 && item_slide != selection_slide) {
    setSelectedItemId(items_.at(index).id);
    current_index_ = item_slide;
    emitCanvasStateChanged(true);
    return;
  }

  auto next_ids = selected_item_ids_;
  if (next_ids.contains(items_.at(index).id)) {
    next_ids.removeAll(items_.at(index).id);
    const auto next_primary = next_ids.isEmpty() ? QString{} : next_ids.back();
    setSelectedItemIds(next_ids, next_primary);
  } else {
    next_ids.push_back(items_.at(index).id);
    setSelectedItemIds(next_ids, items_.at(index).id);
  }
  current_index_ = item_slide;
  emitCanvasStateChanged(true);
}

void CarouselDocumentController::clearSelection() {
  if (selected_item_ids_.isEmpty()) {
    return;
  }
  setSelectedItemIds({});
  emitCanvasStateChanged(true);
}

void CarouselDocumentController::removeSelectedItem() {
  const auto indexes = selectedItemIndexes();
  if (indexes.isEmpty()) {
    return;
  }

  for (qsizetype i = indexes.size(); i > 0; --i) {
    items_.removeAt(indexes.at(i - 1));
  }
  setSelectedItemIds({});
  emitCanvasStateChanged(true);
  setStatus(
      indexes.size() == 1 ? QStringLiteral("Removed the selected layer.")
                          : QStringLiteral("Removed %1 selected layers.").arg(indexes.size()));
}

void CarouselDocumentController::duplicateSelectedItem() {
  const auto indexes = selectedItemIndexes();
  if (indexes.isEmpty()) {
    return;
  }

  QVector<CanvasItem> duplicates;
  duplicates.reserve(indexes.size());
  for (const auto index : indexes) {
    auto item = items_.at(index);
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.bounds.translate(48.0, 48.0);
    duplicates.push_back(item);
  }

  const auto insert_at = indexes.back() + 1;
  for (int i = 0; i < duplicates.size(); ++i) {
    items_.insert(insert_at + i, duplicates.at(i));
  }

  QStringList duplicate_ids;
  duplicate_ids.reserve(duplicates.size());
  for (const auto& item : duplicates) {
    duplicate_ids.push_back(item.id);
  }
  setSelectedItemIds(duplicate_ids, duplicate_ids.isEmpty() ? QString{} : duplicate_ids.back());
  current_index_ = slideIndexForItem(duplicates.back());
  emitCanvasStateChanged(true);
  setStatus(
      duplicates.size() == 1 ? QStringLiteral("Duplicated the selected layer.")
                             : QStringLiteral("Duplicated %1 selected layers.").arg(duplicates.size()));
}

void CarouselDocumentController::bringSelectedForward() {
  auto indexes = selectedItemIndexes();
  if (indexes.isEmpty()) {
    return;
  }

  bool moved = false;
  for (qsizetype i = indexes.size(); i > 0; --i) {
    const auto index = indexes.at(i - 1);
    if (index >= items_.size() - 1) {
      continue;
    }
    const auto next_id = items_.at(index + 1).id;
    if (selected_item_ids_.contains(next_id)) {
      continue;
    }
    items_.move(index, index + 1);
    moved = true;
  }
  if (moved) {
    emitCanvasStateChanged(true);
  }
}

void CarouselDocumentController::sendSelectedBackward() {
  const auto indexes = selectedItemIndexes();
  if (indexes.isEmpty()) {
    return;
  }

  bool moved = false;
  for (const auto index : indexes) {
    if (index <= 0) {
      continue;
    }
    const auto previous_id = items_.at(index - 1).id;
    if (selected_item_ids_.contains(previous_id)) {
      continue;
    }
    items_.move(index, index - 1);
    moved = true;
  }
  if (moved) {
    emitCanvasStateChanged(true);
  }
}

void CarouselDocumentController::moveSelectedItemByPixels(double delta_x, double delta_y) {
  const auto indexes = selectedItemIndexes(false, false);
  if (indexes.isEmpty()) {
    return;
  }

  bool moved = false;
  bool slides_changed = false;
  for (const auto index : indexes) {
    auto& item = items_[index];
    if (item.locked) {
      continue;
    }
    const auto previous_slide = slideIndexForItem(item);
    item.bounds.translate(delta_x, delta_y);
    slides_changed = slides_changed || previous_slide != slideIndexForItem(item);
    moved = true;
  }
  if (!moved) {
    return;
  }

  if (const auto* item = currentItem()) {
    current_index_ = slideIndexForItem(*item);
  }
  emitItemGeometryChanged(slides_changed);
}

void CarouselDocumentController::setSelectedItemCenter(double center_x, double center_y) {
  auto* item = currentItem();
  if (item == nullptr) {
    return;
  }

  const auto delta_x = center_x - item->bounds.center().x();
  const auto delta_y = center_y - item->bounds.center().y();
  moveSelectedItemByPixels(delta_x, delta_y);
}

void CarouselDocumentController::setSelectedItemSize(double width, double height) {
  auto* item = currentItem();
  if (item == nullptr || item->locked) {
    return;
  }

  const auto center = item->bounds.center();
  auto next_width = qMax(kMinItemSize, width);
  auto next_height = qMax(kMinItemSize, height);

  if (item->kind != ItemKind::Text) {
    const auto aspect_ratio = normalizedAspectRatio(*item);
    if (aspect_ratio > 0.0) {
      if (next_width / next_height > aspect_ratio) {
        next_width = next_height * aspect_ratio;
      } else {
        next_height = next_width / aspect_ratio;
      }
    }
  } else {
    item->bounds.setWidth(next_width);
    item->text_point_size = qMax(kMinTextPointSize, next_height / 1.2);
    next_height = measuredTextHeight(*item);
  }

  item->bounds = QRectF(0.0, 0.0, next_width, next_height);
  item->bounds.moveCenter(center);
  emitItemGeometryChanged(false);
}

void CarouselDocumentController::setSelectedItemRotation(double rotation_degrees) {
  auto* item = currentItem();
  if (item == nullptr || item->locked) {
    return;
  }

  item->rotation_degrees = rotation_degrees;
  emitCanvasStateChanged(false);
}

void CarouselDocumentController::setSelectedText(const QString& text) {
  auto* item = currentItem();
  if (item == nullptr || item->kind != ItemKind::Text || item->locked) {
    return;
  }

  item->text = text;
  item->display_label = text.simplified().left(24);
  const auto center = item->bounds.center();
  item->bounds.setHeight(measuredTextHeight(*item));
  item->bounds.moveCenter(center);
  emitCanvasStateChanged(true);
}

void CarouselDocumentController::setSelectedTextColor(const QString& color_value) {
  auto* item = currentItem();
  if (item == nullptr || item->kind != ItemKind::Text || item->locked) {
    return;
  }

  const QColor color(color_value);
  if (!color.isValid()) {
    return;
  }

  item->text_color = color.name(QColor::HexRgb);
  emitCanvasStateChanged(false);
}

void CarouselDocumentController::setSelectedTextPointSize(double point_size) {
  auto* item = currentItem();
  if (item == nullptr || item->kind != ItemKind::Text || item->locked) {
    return;
  }

  item->text_point_size = qMax(kMinTextPointSize, point_size);
  const auto center = item->bounds.center();
  item->bounds.setHeight(measuredTextHeight(*item));
  item->bounds.moveCenter(center);
  emitCanvasStateChanged(false);
}

void CarouselDocumentController::setSelectedItemHidden(bool hidden) {
  const auto indexes = selectedItemIndexes(false, false);
  if (indexes.isEmpty()) {
    return;
  }

  bool changed = false;
  for (const auto index : indexes) {
    auto& item = items_[index];
    if (item.hidden == hidden) {
      continue;
    }
    item.hidden = hidden;
    changed = true;
  }
  if (!changed) {
    return;
  }
  emitCanvasStateChanged(true);
}

void CarouselDocumentController::setSelectedItemLocked(bool locked) {
  const auto indexes = selectedItemIndexes(false, false);
  if (indexes.isEmpty()) {
    return;
  }

  bool changed = false;
  for (const auto index : indexes) {
    auto& item = items_[index];
    if (item.locked == locked) {
      continue;
    }
    item.locked = locked;
    changed = true;
  }
  if (!changed) {
    return;
  }
  emitCanvasStateChanged(false);
}

void CarouselDocumentController::replaceSelectedPhotoItem(
    const QString& file_path,
    const QString& display_label,
    const QString& recipe_json) {
  auto* item = currentItem();
  if (item == nullptr) {
    return;
  }

  const auto normalized = normalized_path(file_path);
  if (!isImageFile(normalized)) {
    setStatus(QStringLiteral("That file is not a supported image format."));
    return;
  }

  QSizeF source_size;
  if (native_image_supported(normalized)) {
    const auto native_size = native_image_size(normalized);
    if (native_size.isValid()) {
      source_size = QSizeF(native_size);
    }
  } else {
    QImageReader reader(normalized);
    if (reader.canRead()) {
      source_size = reader.size().isValid() ? QSizeF(reader.size()) : QSizeF(1200.0, 1200.0);
    } else {
      const auto raw_size = raw_image_size(normalized);
      if (raw_size.isValid()) {
        source_size = QSizeF(raw_size);
      }
    }
  }

  if (!source_size.isValid()) {
    setStatus(QStringLiteral("Unable to read that image file."));
    return;
  }

  item->kind = ItemKind::Image;
  item->file_path = normalized;
  item->display_label = displayNameForPath(normalized, display_label);
  item->recipe_json = recipe_json;
  item->recipe_state = recipeStateFromJson(recipe_json);
  item->fit_mode = FitMode::Cover;
  item->source_size = source_size;
  emitCanvasStateChanged(true);
  setStatus(QStringLiteral("Replaced the selected layer with a photo."));
}

void CarouselDocumentController::replaceSelectedVideoItem(
    const QString& file_path,
    const QString& display_label) {
  auto* item = currentItem();
  if (item == nullptr) {
    return;
  }

  const auto normalized = normalized_path(file_path);
  if (!isVideoFile(normalized)) {
    setStatus(QStringLiteral("That file is not a supported video format."));
    return;
  }

  QSizeF source_size = QSizeF(probeVideoSize(normalized));
  if (!source_size.isValid()) {
    source_size = QSizeF(1080.0, 1920.0);
  }

  item->kind = ItemKind::Video;
  item->file_path = normalized;
  item->display_label = displayNameForPath(normalized, display_label);
  item->recipe_json.clear();
  item->recipe_state = RecipeState{};
  item->fit_mode = FitMode::Contain;
  item->source_size = source_size;
  emitCanvasStateChanged(true);
  setStatus(QStringLiteral("Replaced the selected layer with a video."));
}

void CarouselDocumentController::alignSelectionLeft() {
  const auto indexes = selectedItemIndexes(true, true);
  if (indexes.isEmpty()) {
    return;
  }

  const auto slide_rect = slideRectInDocument(currentSelectionSlideIndex());
  for (const auto index : indexes) {
    items_[index].bounds.moveLeft(slide_rect.left());
  }
  emitItemGeometryChanged(false);
}

void CarouselDocumentController::alignSelectionHorizontalCenter() {
  const auto indexes = selectedItemIndexes(true, true);
  if (indexes.isEmpty()) {
    return;
  }

  const auto slide_rect = slideRectInDocument(currentSelectionSlideIndex());
  for (const auto index : indexes) {
    items_[index].bounds.moveCenter(QPointF(slide_rect.center().x(), items_[index].bounds.center().y()));
  }
  emitItemGeometryChanged(false);
}

void CarouselDocumentController::alignSelectionRight() {
  const auto indexes = selectedItemIndexes(true, true);
  if (indexes.isEmpty()) {
    return;
  }

  const auto slide_rect = slideRectInDocument(currentSelectionSlideIndex());
  const auto right_edge = slide_rect.left() + slide_rect.width();
  for (const auto index : indexes) {
    items_[index].bounds.moveRight(right_edge);
  }
  emitItemGeometryChanged(false);
}

void CarouselDocumentController::alignSelectionTop() {
  const auto indexes = selectedItemIndexes(true, true);
  if (indexes.isEmpty()) {
    return;
  }

  const auto slide_rect = slideRectInDocument(currentSelectionSlideIndex());
  for (const auto index : indexes) {
    items_[index].bounds.moveTop(slide_rect.top());
  }
  emitItemGeometryChanged(false);
}

void CarouselDocumentController::alignSelectionVerticalCenter() {
  const auto indexes = selectedItemIndexes(true, true);
  if (indexes.isEmpty()) {
    return;
  }

  const auto slide_rect = slideRectInDocument(currentSelectionSlideIndex());
  for (const auto index : indexes) {
    items_[index].bounds.moveCenter(QPointF(items_[index].bounds.center().x(), slide_rect.center().y()));
  }
  emitItemGeometryChanged(false);
}

void CarouselDocumentController::alignSelectionBottom() {
  const auto indexes = selectedItemIndexes(true, true);
  if (indexes.isEmpty()) {
    return;
  }

  const auto slide_rect = slideRectInDocument(currentSelectionSlideIndex());
  const auto bottom_edge = slide_rect.top() + slide_rect.height();
  for (const auto index : indexes) {
    items_[index].bounds.moveBottom(bottom_edge);
  }
  emitItemGeometryChanged(false);
}

void CarouselDocumentController::distributeSelectionHorizontally() {
  auto indexes = selectedItemIndexes(true, true);
  if (indexes.size() < 3) {
    return;
  }

  std::sort(indexes.begin(), indexes.end(), [this](int left, int right) {
    return items_.at(left).bounds.left() < items_.at(right).bounds.left();
  });

  const auto first_left = items_.at(indexes.front()).bounds.left();
  const auto last_right = items_.at(indexes.back()).bounds.right();
  double total_width = 0.0;
  for (const auto index : indexes) {
    total_width += items_.at(index).bounds.width();
  }
  const auto gap = (last_right - first_left - total_width) / static_cast<double>(indexes.size() - 1);

  auto cursor = first_left;
  for (int i = 0; i < indexes.size(); ++i) {
    auto& item = items_[indexes.at(i)];
    item.bounds.moveLeft(cursor);
    cursor += item.bounds.width() + gap;
  }
  emitItemGeometryChanged(false);
}

void CarouselDocumentController::distributeSelectionVertically() {
  auto indexes = selectedItemIndexes(true, true);
  if (indexes.size() < 3) {
    return;
  }

  std::sort(indexes.begin(), indexes.end(), [this](int top, int bottom) {
    return items_.at(top).bounds.top() < items_.at(bottom).bounds.top();
  });

  const auto first_top = items_.at(indexes.front()).bounds.top();
  const auto last_bottom = items_.at(indexes.back()).bounds.bottom();
  double total_height = 0.0;
  for (const auto index : indexes) {
    total_height += items_.at(index).bounds.height();
  }
  const auto gap = (last_bottom - first_top - total_height) / static_cast<double>(indexes.size() - 1);

  auto cursor = first_top;
  for (int i = 0; i < indexes.size(); ++i) {
    auto& item = items_[indexes.at(i)];
    item.bounds.moveTop(cursor);
    cursor += item.bounds.height() + gap;
  }
  emitItemGeometryChanged(false);
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
  if (template_kind == QStringLiteral("split-pair") &&
      document_format_ != QStringLiteral("5x4")) {
    document_format_ = QStringLiteral("5x4");
    forced_landscape = true;
    emit documentFormatChanged();
  }

  if (template_kind == QStringLiteral("single")) {
    removeGroupAtIndex(current_index_, true);
    if (auto* slide = slideAt(current_index_)) {
      if (source_media.type != MediaType::Empty) {
        slide->primary = source_media;
      }
      slide->template_kind = SlideTemplate::Single;
      slide->secondary = MediaItem{};
      slide->tertiary = MediaItem{};
    }
    active_slot_ = QStringLiteral("primary");
    refreshPreviews();
    emitCanvasStateChanged(true);
    setStatus(QStringLiteral("Selection set to a single canvas."));
    return;
  }

  if (template_kind == QStringLiteral("split-pair")) {
    removeGroupAtIndex(current_index_, true);
    if (auto* slide = slideAt(current_index_)) {
      if (source_media.type != MediaType::Empty) {
        slide->primary = source_media;
      }
      slide->template_kind = SlideTemplate::SplitPair;
      slide->secondary = MediaItem{};
      slide->tertiary = MediaItem{};
    }
    active_slot_ = QStringLiteral("primary");
    refreshPreviews();
    emitCanvasStateChanged(true);
    setStatus(forced_landscape
                  ? QStringLiteral("Selection set to a half-and-half landscape split.")
                  : QStringLiteral("Selection set to a half-and-half split slide."));
    return;
  }

  if (template_kind == QStringLiteral("stacked-triptych")) {
    removeGroupAtIndex(current_index_, true);
    if (auto* slide = slideAt(current_index_)) {
      if (source_media.type != MediaType::Empty) {
        slide->primary = source_media;
      }
      slide->template_kind = SlideTemplate::StackedTriptych;
      slide->secondary = MediaItem{};
      slide->tertiary = MediaItem{};
    }
    active_slot_ = QStringLiteral("primary");
    refreshPreviews();
    emitCanvasStateChanged(true);
    setStatus(QStringLiteral("Selection set to a stacked triptych canvas."));
    return;
  }

  const auto group_kind = groupKindFromTemplate(template_kind);
  if (group_kind == GroupKind::None) {
    return;
  }

  removeGroupAtIndex(current_index_, true);
  const auto span = spanForGroupKind(group_kind);
  if (!ensureCapacityForGroup(current_index_, span)) {
    setStatus(QStringLiteral("Not enough room to create that linked spread within the 20-canvas limit."));
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
    if (auto* slide = slideAt(index)) {
      slide->template_kind = SlideTemplate::Single;
      slide->primary = MediaItem{};
      slide->secondary = MediaItem{};
      slide->tertiary = MediaItem{};
      slide->linked_group_id = group.id;
    }
  }

  active_slot_ = QStringLiteral("primary");
  refreshPreviews();
  emitCanvasStateChanged(true);
  setStatus(forced_portrait
                ? QStringLiteral("Created a linked 2-slide portrait spread.")
                : QStringLiteral("Created a linked 2-slide spread."));
}

void CarouselDocumentController::setActiveSlot(const QString& slot_name) {
  QString normalized = QStringLiteral("primary");
  if (slot_name == QStringLiteral("secondary")) {
    normalized = QStringLiteral("secondary");
  } else if (slot_name == QStringLiteral("tertiary")) {
    normalized = QStringLiteral("tertiary");
  }
  if (active_slot_ == normalized) {
    return;
  }
  active_slot_ = normalized;
  normalizeForCurrentTemplate();
  emit selectionChanged();
}

void CarouselDocumentController::setCurrentFitMode(const QString& fit_mode) {
  if (auto* item = currentItem()) {
    if (item->kind == ItemKind::Image || item->kind == ItemKind::Video) {
      item->fit_mode = fitModeFromName(fit_mode);
      refreshCurrentSelectionPreviews();
      emitCanvasStateChanged(true);
    }
    return;
  }

  if (auto* media = currentEditableMedia()) {
    media->fit_mode = fitModeFromName(fit_mode);
    refreshCurrentSelectionPreviews();
    emitCanvasStateChanged(true);
  }
}

void CarouselDocumentController::setCurrentScale(double scale) {
  if (currentItem() != nullptr) {
    return;
  }
  if (auto* media = currentEditableMedia()) {
    media->scale = clamp_zoom_scale(scale);
    refreshCurrentSelectionPreviews();
    emitCanvasStateChanged(true);
  }
}

void CarouselDocumentController::setCurrentOffsetX(double offset_x) {
  if (currentItem() != nullptr) {
    return;
  }
  if (auto* media = currentEditableMedia()) {
    media->offset_x = clamp_unit_interval(offset_x);
    refreshCurrentSelectionPreviews();
    emitCanvasStateChanged(true);
  }
}

void CarouselDocumentController::setCurrentOffsetY(double offset_y) {
  if (currentItem() != nullptr) {
    return;
  }
  if (auto* media = currentEditableMedia()) {
    media->offset_y = clamp_unit_interval(offset_y);
    refreshCurrentSelectionPreviews();
    emitCanvasStateChanged(true);
  }
}

void CarouselDocumentController::moveCurrentMediaByPixels(double delta_x, double delta_y) {
  if (currentItem() != nullptr) {
    return;
  }

  auto* media = currentEditableMedia();
  if (media == nullptr || media->type == MediaType::Empty || media->file_path.trimmed().isEmpty()) {
    return;
  }

  const auto viewport_size = currentMediaViewportSize();
  const auto source_size = effectiveMediaSourceSize(*media);
  if (!viewport_size.isValid() || !source_size.isValid()) {
    return;
  }

  const double viewport_width = static_cast<double>(viewport_size.width());
  const double viewport_height = static_cast<double>(viewport_size.height());
  const double source_width = static_cast<double>(source_size.width());
  const double source_height = static_cast<double>(source_size.height());
  if (viewport_width <= 0.0 || viewport_height <= 0.0 || source_width <= 0.0 || source_height <= 0.0) {
    return;
  }

  const double base_scale = media->fit_mode == FitMode::Contain
                                ? qMin(viewport_width / source_width, viewport_height / source_height)
                                : qMax(viewport_width / source_width, viewport_height / source_height);
  const double scaled_width = qMax(1.0, source_width * base_scale * media->scale);
  const double scaled_height = qMax(1.0, source_height * base_scale * media->scale);
  const double free_x = viewport_width - scaled_width;
  const double free_y = viewport_height - scaled_height;

  auto update_axis = [](double offset, double free_space, double delta) {
    if (qAbs(free_space) <= 0.001) {
      return 0.0;
    }
    const double minimum_position = qMin(0.0, free_space);
    const double maximum_position = qMax(0.0, free_space);
    const double current_position = free_space * ((offset + 1.0) / 2.0);
    const double next_position = qBound(minimum_position, current_position + delta, maximum_position);
    double next_offset = ((next_position / free_space) * 2.0) - 1.0;
    if (qAbs(next_offset) < 0.06) {
      next_offset = 0.0;
    } else if (qAbs(next_offset - 1.0) < 0.04) {
      next_offset = 1.0;
    } else if (qAbs(next_offset + 1.0) < 0.04) {
      next_offset = -1.0;
    }
    return clamp_unit_interval(next_offset);
  };

  const double next_offset_x = update_axis(media->offset_x, free_x, delta_x);
  const double next_offset_y = update_axis(media->offset_y, free_y, delta_y);
  if (qFuzzyCompare(media->offset_x, next_offset_x) && qFuzzyCompare(media->offset_y, next_offset_y)) {
    return;
  }

  media->offset_x = next_offset_x;
  media->offset_y = next_offset_y;
  refreshCurrentSelectionPreviews();
  emitCanvasStateChanged(true);
}

void CarouselDocumentController::clearCurrentSlot() {
  if (currentItem() != nullptr) {
    removeSelectedItem();
    return;
  }
  if (currentSelectionIsLinked()) {
    clearCurrentGroup();
    return;
  }

  auto* slide = slideAt(current_index_);
  if (slide == nullptr) {
    return;
  }

  if (active_slot_ == QStringLiteral("secondary") &&
      (slide->template_kind == SlideTemplate::SplitPair || slide->template_kind == SlideTemplate::StackedTriptych)) {
    slide->secondary = MediaItem{};
    setStatus(slide->template_kind == SlideTemplate::SplitPair
                  ? QStringLiteral("Cleared the secondary media slot.")
                  : QStringLiteral("Cleared the middle media slot."));
  } else if (active_slot_ == QStringLiteral("tertiary") &&
             slide->template_kind == SlideTemplate::StackedTriptych) {
    slide->tertiary = MediaItem{};
    setStatus(QStringLiteral("Cleared the bottom media slot."));
  } else {
    slide->primary = MediaItem{};
    setStatus(QStringLiteral("Cleared the current slide media."));
  }

  refreshCurrentSelectionPreviews();
  emitCanvasStateChanged(true);
}

void CarouselDocumentController::clearCurrentGroup() {
  removeGroupAtIndex(current_index_, true);
  refreshPreviews();
  emitCanvasStateChanged(true);
  setStatus(QStringLiteral("Collapsed the linked spread back to a single canvas."));
}

void CarouselDocumentController::assignPrimaryImage(
    const QString& file_path,
    const QString& display_label,
    const QString& recipe_json) {
  assignImageToCurrentSlot(file_path, display_label, recipe_json, QStringLiteral("primary"));
}

void CarouselDocumentController::assignSecondaryImage(
    const QString& file_path,
    const QString& display_label,
    const QString& recipe_json) {
  assignImageToCurrentSlot(file_path, display_label, recipe_json, QStringLiteral("secondary"));
}

void CarouselDocumentController::assignTertiaryImage(
    const QString& file_path,
    const QString& display_label,
    const QString& recipe_json) {
  assignImageToCurrentSlot(file_path, display_label, recipe_json, QStringLiteral("tertiary"));
}

void CarouselDocumentController::assignPrimaryVideo(
    const QString& file_path,
    const QString& display_label) {
  const auto normalized = normalized_path(file_path);
  if (!isVideoFile(normalized)) {
    setStatus(QStringLiteral("That file is not a supported video format."));
    return;
  }

  if (currentSelectionIsLinked()) {
    setStatus(QStringLiteral("Video is only supported on a single canvas."));
    return;
  }

  auto* slide = slideAt(current_index_);
  if (slide == nullptr) {
    return;
  }
  if (slide->template_kind != SlideTemplate::Single) {
    setStatus(QStringLiteral("Video is only supported on the Single template."));
    return;
  }

  slide->primary = MediaItem{
      .type = MediaType::Video,
      .file_path = normalized,
      .display_label = displayNameForPath(normalized, display_label),
      .fit_mode = FitMode::Cover,
      .scale = 1.0,
      .offset_x = 0.0,
      .offset_y = 0.0,
  };
  slide->secondary = MediaItem{};
  slide->tertiary = MediaItem{};
  active_slot_ = QStringLiteral("primary");
  setSelectedItemIds({});
  refreshCurrentSelectionPreviews();
  emitCanvasStateChanged(true);
  setStatus(QStringLiteral("Video added to the current canvas."));
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
      QStringLiteral("Choose Image"),
      QDir::homePath(),
      imageFileFilter());
  if (!file_path.isEmpty()) {
    if (active_slot_ == QStringLiteral("tertiary")) {
      assignTertiaryImage(file_path, QFileInfo(file_path).completeBaseName(), QString{});
    } else {
      assignSecondaryImage(file_path, QFileInfo(file_path).completeBaseName(), QString{});
    }
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
  bool success = true;
  QString failure_reason;
  int written_count = 0;
  const auto size = exportPixelSize();

  for (int index = 0; index < slides_.size(); ++index) {
    const auto output_stem = QStringLiteral("carousel-%1").arg(index + 1, 2, 10, QLatin1Char('0'));
    const auto* slide = slideAt(index);
    const auto* group = groupForIndex(index);
    const auto has_freeform = hasFreeformItemsOnSlide(index);

    if (group != nullptr) {
      if (group->media.type != MediaType::Image) {
        success = false;
        failure_reason = QStringLiteral("A linked spread is missing its source image.");
        break;
      }
    } else if (slide != nullptr && slide->template_kind == SlideTemplate::SplitPair) {
      if (slide->primary.type != MediaType::Image || slide->secondary.type != MediaType::Image) {
        success = false;
        failure_reason = QStringLiteral("A split canvas is missing one of its images.");
        break;
      }
    } else if (slide != nullptr && slide->template_kind == SlideTemplate::StackedTriptych) {
      if (slide->primary.type != MediaType::Image || slide->secondary.type != MediaType::Image ||
          slide->tertiary.type != MediaType::Image) {
        success = false;
        failure_reason = QStringLiteral("A stacked canvas is missing one of its images.");
        break;
      }
    }

    if (slide != nullptr && slide->template_kind == SlideTemplate::Single &&
        slide->primary.type == MediaType::Video) {
      if (has_freeform) {
        success = false;
        failure_reason = QStringLiteral("Video canvases cannot have freeform photo or text overlays during export.");
        break;
      }
      const auto output_path = output_dir.filePath(output_stem + QStringLiteral(".mp4"));
      if (!exportVideoSlide(slide->primary, output_path, size)) {
        success = false;
        failure_reason = QStringLiteral("Unable to export video canvas %1. Install ffmpeg to enable video export.")
                             .arg(index + 1);
        break;
      }
      ++written_count;
      continue;
    }

    const auto image = renderSlideImage(index, size, false);
    const auto output_path = output_dir.filePath(output_stem + QStringLiteral(".png"));
    QImageWriter writer(output_path, "png");
    if (!writer.write(image)) {
      success = false;
      failure_reason = writer.errorString();
      break;
    }
    ++written_count;
  }

  setExportBusy(false);
  if (!success) {
    setStatus(failure_reason);
    return false;
  }

  setStatus(QStringLiteral("Exported %1 canvas%2 to %3.")
                .arg(written_count)
                .arg(written_count == 1 ? QString{} : QStringLiteral("es"))
                .arg(output_dir.absolutePath()));
  return true;
}

QVariantList CarouselDocumentController::recentDocuments() const {
  QVariantList documents;

  QString output;
  if (runHelperCommand({QStringLiteral("list")}, &output, nullptr)) {
    const auto parsed = QJsonDocument::fromJson(output.toUtf8());
    if (parsed.isArray()) {
      for (const auto& value : parsed.array()) {
        if (!value.isObject()) {
          continue;
        }
        const auto object = value.toObject();
        QVariantMap document;
        document.insert(QStringLiteral("documentId"), object.value(QStringLiteral("document_id")).toString().trimmed());
        document.insert(QStringLiteral("name"), object.value(QStringLiteral("name")).toString().trimmed());
        document.insert(QStringLiteral("documentFormat"), object.value(QStringLiteral("document_format")).toString().trimmed());
        document.insert(QStringLiteral("slideCount"), object.value(QStringLiteral("slide_count")).toInt());
        document.insert(QStringLiteral("createdAt"), object.value(QStringLiteral("created_at")).toString().trimmed());
        document.insert(QStringLiteral("updatedAt"), object.value(QStringLiteral("updated_at")).toString().trimmed());
        documents.push_back(document);
      }
      return documents;
    }
  }

  for (const auto& value : load_legacy_document_index()) {
    if (!value.isObject()) {
      continue;
    }
    const auto object = value.toObject();
    QVariantMap document;
    document.insert(QStringLiteral("documentId"), object.value(QStringLiteral("documentId")).toString().trimmed());
    document.insert(QStringLiteral("name"), object.value(QStringLiteral("name")).toString().trimmed());
    document.insert(QStringLiteral("documentFormat"), object.value(QStringLiteral("documentFormat")).toString().trimmed());
    document.insert(QStringLiteral("slideCount"), object.value(QStringLiteral("slideCount")).toInt());
    document.insert(QStringLiteral("createdAt"), object.value(QStringLiteral("createdAt")).toString().trimmed());
    document.insert(QStringLiteral("updatedAt"), object.value(QStringLiteral("updatedAt")).toString().trimmed());
    documents.push_back(document);
  }

  return documents;
}

bool CarouselDocumentController::saveDocument(const QString& document_name) {
  const auto previous_document_id = saved_document_id_;
  const auto next_name = normalized_document_name(document_name.isEmpty() ? document_name_ : document_name);
  if (saved_document_id_.isEmpty()) {
    saved_document_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  document_name_ = next_name;

  QJsonObject payload;
  payload.insert(QStringLiteral("document_id"), saved_document_id_);
  payload.insert(QStringLiteral("name"), document_name_);
  payload.insert(QStringLiteral("document_format"), document_format_);
  payload.insert(QStringLiteral("slide_count"), slideCount());
  payload.insert(QStringLiteral("document_json"), documentObject());
  payload.insert(QStringLiteral("created_at"), QString{});
  payload.insert(QStringLiteral("updated_at"), QString{});

  const auto json_payload = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
  QString error_message;
  if (!runHelperCommand({QStringLiteral("upsert"), json_payload}, nullptr, &error_message)) {
    const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    auto index_entries = load_legacy_document_index();
    QString created_at = now;
    QJsonArray next_entries;
    bool updated_existing = false;
    for (const auto& value : index_entries) {
      if (!value.isObject()) {
        continue;
      }
      auto entry = value.toObject();
      if (entry.value(QStringLiteral("documentId")).toString().trimmed() == saved_document_id_) {
        const auto existing_created_at = entry.value(QStringLiteral("createdAt")).toString().trimmed();
        if (!existing_created_at.isEmpty()) {
          created_at = existing_created_at;
        }
        entry = legacy_document_entry(
            saved_document_id_,
            document_name_,
            document_format_,
            slideCount(),
            created_at,
            now);
        updated_existing = true;
      }
      next_entries.push_back(entry);
    }
    if (!updated_existing) {
      next_entries.push_back(
          legacy_document_entry(saved_document_id_, document_name_, document_format_, slideCount(), now, now));
    }

    QJsonObject legacy_payload;
    legacy_payload.insert(QStringLiteral("documentId"), saved_document_id_);
    legacy_payload.insert(QStringLiteral("name"), document_name_);
    legacy_payload.insert(QStringLiteral("documentFormat"), document_format_);
    legacy_payload.insert(QStringLiteral("slideCount"), slideCount());
    legacy_payload.insert(QStringLiteral("document"), documentObject());

    if (!save_legacy_document_payload(saved_document_id_, legacy_payload) || !save_legacy_document_index(next_entries)) {
      saved_document_id_ = previous_document_id;
      setStatus(error_message.isEmpty() ? QStringLiteral("Unable to save the carousel document.") : error_message);
      return false;
    }
  }

  emit documentStateChanged();
  setStatus(QStringLiteral("Saved carousel '%1'.").arg(document_name_));
  return true;
}

bool CarouselDocumentController::loadDocument(const QString& document_id) {
  const auto trimmed_id = document_id.trimmed();
  if (trimmed_id.isEmpty()) {
    setStatus(QStringLiteral("Choose a saved carousel to load."));
    return false;
  }

  QString output;
  QString error_message;
  QJsonObject stored_document;
  if (runHelperCommand({QStringLiteral("get"), trimmed_id}, &output, &error_message)) {
    const auto parsed = QJsonDocument::fromJson(output.toUtf8());
    if (parsed.isObject()) {
      stored_document = parsed.object();
    }
  } else {
    stored_document = load_legacy_document_payload(trimmed_id);
  }

  if (stored_document.isEmpty()) {
    setStatus(QStringLiteral("The selected carousel is unavailable."));
    return false;
  }

  auto document_value = stored_document.value(QStringLiteral("document_json"));
  if (!document_value.isObject()) {
    document_value = stored_document.value(QStringLiteral("document"));
  }
  if (!document_value.isObject() || !loadDocumentObject(document_value.toObject())) {
    setStatus(QStringLiteral("The selected carousel document is invalid."));
    return false;
  }

  saved_document_id_ = stored_document.value(QStringLiteral("document_id")).toString().trimmed();
  if (saved_document_id_.isEmpty()) {
    saved_document_id_ = stored_document.value(QStringLiteral("documentId")).toString().trimmed();
  }
  document_name_ = normalized_document_name(stored_document.value(QStringLiteral("name")).toString());
  const auto stored_format = stored_document.value(QStringLiteral("document_format")).toString().trimmed().isEmpty()
      ? stored_document.value(QStringLiteral("documentFormat")).toString().trimmed()
      : stored_document.value(QStringLiteral("document_format")).toString().trimmed();
  if (!stored_format.isEmpty()) {
    document_format_ = stored_format == QStringLiteral("5x4") ? QStringLiteral("5x4") : QStringLiteral("4x5");
  }
  refreshPreviews();
  emit documentFormatChanged();
  emit documentStateChanged();
  emitCanvasStateChanged(true);
  setStatus(QStringLiteral("Loaded carousel '%1'.").arg(document_name_));
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

CarouselDocumentController::CanvasItem* CarouselDocumentController::currentItem() {
  const auto* self = this;
  return const_cast<CanvasItem*>(self->currentItem());
}

const CarouselDocumentController::CanvasItem* CarouselDocumentController::currentItem() const {
  const auto index = indexOfItem(selected_item_id_);
  return index >= 0 ? &items_.at(index) : nullptr;
}

CarouselDocumentController::MediaItem* CarouselDocumentController::currentEditableMedia() {
  const auto* self = this;
  return const_cast<MediaItem*>(self->currentEditableMedia());
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
  if (active_slot_ == QStringLiteral("secondary") &&
      (slide->template_kind == SlideTemplate::SplitPair ||
       slide->template_kind == SlideTemplate::StackedTriptych)) {
    return &slide->secondary;
  }
  if (active_slot_ == QStringLiteral("tertiary") &&
      slide->template_kind == SlideTemplate::StackedTriptych) {
    return &slide->tertiary;
  }
  return &slide->primary;
}

int CarouselDocumentController::indexOfItem(const QString& item_id) const {
  for (int index = 0; index < items_.size(); ++index) {
    if (items_.at(index).id == item_id) {
      return index;
    }
  }
  return -1;
}

int CarouselDocumentController::currentSelectionSlideIndex() const {
  if (const auto* item = currentItem()) {
    return slideIndexForItem(*item);
  }
  if (!selected_item_ids_.isEmpty()) {
    const auto index = indexOfItem(selected_item_ids_.back());
    if (index >= 0) {
      return slideIndexForItem(items_.at(index));
    }
  }
  return -1;
}

QRectF CarouselDocumentController::slideRectInDocument(int index) const {
  const auto size = exportPixelSize();
  return QRectF(index * size.width(), 0.0, size.width(), size.height());
}

int CarouselDocumentController::slideIndexForX(double x) const {
  const auto slide_width = qMax(1, exportPixelSize().width());
  const auto index = static_cast<int>(qFloor(x / slide_width));
  return qBound(0, index, qMax(0, slideCount() - 1));
}

int CarouselDocumentController::slideIndexForItem(const CanvasItem& item) const {
  return slideIndexForX(item.bounds.center().x());
}

QSize CarouselDocumentController::exportPixelSize() const {
  return document_format_ == QStringLiteral("5x4") ? QSize(1350, 1080) : QSize(1080, 1350);
}

QString CarouselDocumentController::previewsRoot() const {
  const auto cache_root = AppBootstrap::cacheLocation();
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

QString CarouselDocumentController::helperProgram() const {
  if (useBuiltHelperBinary()) {
    return builtBinaryPath(QStringLiteral("catalog_carousel_documents"));
  }

  return QStringLiteral("cargo");
}

QStringList CarouselDocumentController::helperArguments(const QStringList& command_arguments) const {
  if (useBuiltHelperBinary()) {
    return command_arguments;
  }

  QStringList arguments = {
      QStringLiteral("run"),
      QStringLiteral("--quiet"),
      QStringLiteral("--manifest-path"),
      QStringLiteral("rust/Cargo.toml"),
      QStringLiteral("-p"),
      QStringLiteral("photo-catalog"),
      QStringLiteral("--bin"),
      QStringLiteral("catalog_carousel_documents"),
      QStringLiteral("--"),
  };
  arguments.append(command_arguments);
  return arguments;
}

bool CarouselDocumentController::useBuiltHelperBinary() const {
  const QFileInfo binary(builtBinaryPath(QStringLiteral("catalog_carousel_documents")));
  return binary.exists() && binary.isExecutable();
}

QString CarouselDocumentController::builtBinaryPath(const QString& binary_name) const {
  return QDir::current().absoluteFilePath(QStringLiteral("rust/target/debug/%1").arg(binary_name));
}

bool CarouselDocumentController::runHelperCommand(
    const QStringList& command_arguments,
    QString* output,
    QString* error_message) const {
  QProcess process;
  process.setWorkingDirectory(QDir::currentPath());
  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    if (error_message != nullptr) {
      *error_message = environment_error;
    }
    return false;
  }

  process.setProcessEnvironment(environment);
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(helperProgram(), helperArguments(command_arguments));
  if (!process.waitForStarted(5000)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to start carousel document helper.");
    }
    return false;
  }

  if (!process.waitForFinished(15000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error_message != nullptr) {
      const auto helper_output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
      *error_message = helper_output.isEmpty() ? QStringLiteral("Carousel document helper failed.") : helper_output;
    }
    return false;
  }

  if (output != nullptr) {
    *output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  }
  return true;
}

QString CarouselDocumentController::fileUrlForPath(const QString& file_path) const {
  if (file_path.isEmpty()) {
    return {};
  }
  return QUrl::fromLocalFile(file_path).toString();
}

CarouselDocumentController::RecipeState CarouselDocumentController::recipeStateFromJson(
    const QString& recipe_json) const {
  return parse_edit_recipe_json(recipe_json);
}

QJsonObject CarouselDocumentController::mediaObject(const MediaItem& media) const {
  QJsonObject object;
  QString type = QStringLiteral("empty");
  if (media.type == MediaType::Image) {
    type = QStringLiteral("image");
  } else if (media.type == MediaType::Video) {
    type = QStringLiteral("video");
  }
  object.insert(QStringLiteral("type"), type);
  object.insert(QStringLiteral("filePath"), media.file_path);
  object.insert(QStringLiteral("displayLabel"), media.display_label);
  object.insert(QStringLiteral("recipeJson"), media.recipe_json);
  object.insert(QStringLiteral("fitMode"), fitModeName(media.fit_mode));
  object.insert(QStringLiteral("scale"), media.scale);
  object.insert(QStringLiteral("offsetX"), media.offset_x);
  object.insert(QStringLiteral("offsetY"), media.offset_y);
  return object;
}

CarouselDocumentController::MediaItem CarouselDocumentController::mediaFromObject(const QJsonObject& object) const {
  MediaItem media;
  const auto type = object.value(QStringLiteral("type")).toString().trimmed();
  if (type == QStringLiteral("image")) {
    media.type = MediaType::Image;
  } else if (type == QStringLiteral("video")) {
    media.type = MediaType::Video;
  }
  media.file_path = normalized_path(object.value(QStringLiteral("filePath")).toString());
  media.display_label = object.value(QStringLiteral("displayLabel")).toString().trimmed();
  media.recipe_json = object.value(QStringLiteral("recipeJson")).toString().trimmed();
  media.recipe_state = recipeStateFromJson(media.recipe_json);
  media.fit_mode = fitModeFromName(object.value(QStringLiteral("fitMode")).toString());
  media.scale = clamp_zoom_scale(object.value(QStringLiteral("scale")).toDouble(1.0));
  media.offset_x = clamp_unit_interval(object.value(QStringLiteral("offsetX")).toDouble());
  media.offset_y = clamp_unit_interval(object.value(QStringLiteral("offsetY")).toDouble());
  return media;
}

QJsonObject CarouselDocumentController::slideObject(const Slide& slide) const {
  QJsonObject object;
  QString template_kind = QStringLiteral("single");
  if (slide.template_kind == SlideTemplate::SplitPair) {
    template_kind = QStringLiteral("split-pair");
  } else if (slide.template_kind == SlideTemplate::StackedTriptych) {
    template_kind = QStringLiteral("stacked-triptych");
  }
  object.insert(QStringLiteral("templateKind"), template_kind);
  object.insert(QStringLiteral("primary"), mediaObject(slide.primary));
  object.insert(QStringLiteral("secondary"), mediaObject(slide.secondary));
  object.insert(QStringLiteral("tertiary"), mediaObject(slide.tertiary));
  object.insert(QStringLiteral("linkedGroupId"), slide.linked_group_id);
  return object;
}

CarouselDocumentController::Slide CarouselDocumentController::slideFromObject(const QJsonObject& object) const {
  Slide slide;
  const auto template_kind = object.value(QStringLiteral("templateKind")).toString().trimmed();
  if (template_kind == QStringLiteral("split-pair")) {
    slide.template_kind = SlideTemplate::SplitPair;
  } else if (template_kind == QStringLiteral("stacked-triptych")) {
    slide.template_kind = SlideTemplate::StackedTriptych;
  }
  if (object.value(QStringLiteral("primary")).isObject()) {
    slide.primary = mediaFromObject(object.value(QStringLiteral("primary")).toObject());
  }
  if (object.value(QStringLiteral("secondary")).isObject()) {
    slide.secondary = mediaFromObject(object.value(QStringLiteral("secondary")).toObject());
  }
  if (object.value(QStringLiteral("tertiary")).isObject()) {
    slide.tertiary = mediaFromObject(object.value(QStringLiteral("tertiary")).toObject());
  }
  slide.linked_group_id = object.value(QStringLiteral("linkedGroupId")).toString().trimmed();
  return slide;
}

QJsonObject CarouselDocumentController::groupObject(const LinkedGroup& group) const {
  QJsonObject object;
  object.insert(QStringLiteral("id"), group.id);
  object.insert(QStringLiteral("kind"), groupKindName(group.kind));
  object.insert(QStringLiteral("startIndex"), group.start_index);
  object.insert(QStringLiteral("span"), group.span);
  object.insert(QStringLiteral("media"), mediaObject(group.media));
  return object;
}

CarouselDocumentController::LinkedGroup CarouselDocumentController::groupFromObject(const QJsonObject& object) const {
  LinkedGroup group;
  group.id = object.value(QStringLiteral("id")).toString().trimmed();
  group.kind = groupKindFromTemplate(object.value(QStringLiteral("kind")).toString().trimmed());
  group.start_index = qMax(0, object.value(QStringLiteral("startIndex")).toInt());
  group.span = qMax(0, object.value(QStringLiteral("span")).toInt());
  if (object.value(QStringLiteral("media")).isObject()) {
    group.media = mediaFromObject(object.value(QStringLiteral("media")).toObject());
  }
  return group;
}

QJsonObject CarouselDocumentController::itemObject(const CanvasItem& item) const {
  QJsonObject object;
  object.insert(QStringLiteral("id"), item.id);
  QString kind = QStringLiteral("image");
  if (item.kind == ItemKind::Text) {
    kind = QStringLiteral("text");
  } else if (item.kind == ItemKind::Video) {
    kind = QStringLiteral("video");
  }
  object.insert(QStringLiteral("kind"), kind);
  object.insert(QStringLiteral("filePath"), item.file_path);
  object.insert(QStringLiteral("displayLabel"), item.display_label);
  object.insert(QStringLiteral("hidden"), item.hidden);
  object.insert(QStringLiteral("locked"), item.locked);
  object.insert(QStringLiteral("recipeJson"), item.recipe_json);
  object.insert(QStringLiteral("text"), item.text);
  object.insert(QStringLiteral("textColor"), item.text_color);
  object.insert(QStringLiteral("textPointSize"), item.text_point_size);
  QJsonObject bounds;
  bounds.insert(QStringLiteral("x"), item.bounds.x());
  bounds.insert(QStringLiteral("y"), item.bounds.y());
  bounds.insert(QStringLiteral("width"), item.bounds.width());
  bounds.insert(QStringLiteral("height"), item.bounds.height());
  object.insert(QStringLiteral("bounds"), bounds);
  object.insert(QStringLiteral("rotationDegrees"), item.rotation_degrees);
  object.insert(QStringLiteral("fitMode"), fitModeName(item.fit_mode));
  QJsonObject source_size;
  source_size.insert(QStringLiteral("width"), item.source_size.width());
  source_size.insert(QStringLiteral("height"), item.source_size.height());
  object.insert(QStringLiteral("sourceSize"), source_size);
  return object;
}

CarouselDocumentController::CanvasItem CarouselDocumentController::itemFromObject(const QJsonObject& object) const {
  CanvasItem item;
  item.id = object.value(QStringLiteral("id")).toString().trimmed();
  const auto kind = object.value(QStringLiteral("kind")).toString().trimmed();
  if (kind == QStringLiteral("text")) {
    item.kind = ItemKind::Text;
  } else if (kind == QStringLiteral("video")) {
    item.kind = ItemKind::Video;
  } else {
    item.kind = ItemKind::Image;
  }
  item.file_path = normalized_path(object.value(QStringLiteral("filePath")).toString());
  item.display_label = object.value(QStringLiteral("displayLabel")).toString().trimmed();
  item.hidden = object.value(QStringLiteral("hidden")).toBool(false);
  item.locked = object.value(QStringLiteral("locked")).toBool(false);
  item.recipe_json = object.value(QStringLiteral("recipeJson")).toString().trimmed();
  item.recipe_state = recipeStateFromJson(item.recipe_json);
  item.text = object.value(QStringLiteral("text")).toString();
  item.text_color = object.value(QStringLiteral("textColor")).toString().trimmed();
  if (item.text_color.isEmpty()) {
    item.text_color = QStringLiteral("#1e1a17");
  }
  item.text_point_size = qMax(kMinTextPointSize, object.value(QStringLiteral("textPointSize")).toDouble(72.0));
  const auto bounds_value = object.value(QStringLiteral("bounds"));
  if (bounds_value.isObject()) {
    const auto bounds = bounds_value.toObject();
    item.bounds = QRectF(
        bounds.value(QStringLiteral("x")).toDouble(),
        bounds.value(QStringLiteral("y")).toDouble(),
        qMax(kMinItemSize, bounds.value(QStringLiteral("width")).toDouble(kMinItemSize)),
        qMax(kMinItemSize, bounds.value(QStringLiteral("height")).toDouble(kMinItemSize)));
  }
  item.rotation_degrees = object.value(QStringLiteral("rotationDegrees")).toDouble();
  item.fit_mode = fitModeFromName(object.value(QStringLiteral("fitMode")).toString());
  const auto source_size_value = object.value(QStringLiteral("sourceSize"));
  if (source_size_value.isObject()) {
    const auto source_size = source_size_value.toObject();
    item.source_size = QSizeF(
        qMax(0.0, source_size.value(QStringLiteral("width")).toDouble()),
        qMax(0.0, source_size.value(QStringLiteral("height")).toDouble()));
  }
  if (item.kind == ItemKind::Text && item.bounds.height() <= 0.0) {
    item.bounds.setHeight(measuredTextHeight(item));
  }
  return item;
}

QJsonObject CarouselDocumentController::documentObject() const {
  QJsonObject object;
  object.insert(QStringLiteral("version"), 2);
  object.insert(QStringLiteral("documentName"), document_name_);
  object.insert(QStringLiteral("documentFormat"), document_format_);
  object.insert(QStringLiteral("currentIndex"), current_index_);
  object.insert(QStringLiteral("activeSlot"), active_slot_);
  object.insert(QStringLiteral("selectedItemId"), selected_item_id_);
  QJsonArray selected_items_array;
  for (const auto& item_id : selected_item_ids_) {
    selected_items_array.push_back(item_id);
  }
  object.insert(QStringLiteral("selectedItemIds"), selected_items_array);

  QJsonArray slides_array;
  for (const auto& slide : slides_) {
    slides_array.push_back(slideObject(slide));
  }
  object.insert(QStringLiteral("slides"), slides_array);

  QJsonArray groups_array;
  for (const auto& group : groups_) {
    groups_array.push_back(groupObject(group));
  }
  object.insert(QStringLiteral("groups"), groups_array);

  QJsonArray items_array;
  for (const auto& item : items_) {
    items_array.push_back(itemObject(item));
  }
  object.insert(QStringLiteral("items"), items_array);
  return object;
}

bool CarouselDocumentController::loadDocumentObject(const QJsonObject& object) {
  QVector<Slide> next_slides;
  QVector<LinkedGroup> next_groups;
  QVector<CanvasItem> next_items;

  const auto slides_value = object.value(QStringLiteral("slides"));
  if (!slides_value.isArray()) {
    return false;
  }

  const auto slides_array = slides_value.toArray();
  next_slides.reserve(slides_array.size());
  for (const auto& value : slides_array) {
    if (!value.isObject()) {
      continue;
    }
    next_slides.push_back(slideFromObject(value.toObject()));
  }
  if (next_slides.isEmpty()) {
    next_slides.push_back(Slide{});
  }

  const auto groups_array = object.value(QStringLiteral("groups")).toArray();
  next_groups.reserve(groups_array.size());
  for (const auto& value : groups_array) {
    if (!value.isObject()) {
      continue;
    }
    const auto group = groupFromObject(value.toObject());
    if (!group.id.isEmpty()) {
      next_groups.push_back(group);
    }
  }

  const auto items_array = object.value(QStringLiteral("items")).toArray();
  next_items.reserve(items_array.size());
  for (const auto& value : items_array) {
    if (!value.isObject()) {
      continue;
    }
    auto item = itemFromObject(value.toObject());
    if (item.id.isEmpty()) {
      item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    next_items.push_back(item);
  }

  slides_ = next_slides;
  groups_ = next_groups;
  items_ = next_items;
  document_name_ = normalized_document_name(object.value(QStringLiteral("documentName")).toString());
  document_format_ =
      object.value(QStringLiteral("documentFormat")).toString().trimmed() == QStringLiteral("5x4")
          ? QStringLiteral("5x4")
          : QStringLiteral("4x5");
  current_index_ = qBound(0, object.value(QStringLiteral("currentIndex")).toInt(), qMax(0, static_cast<int>(slides_.size()) - 1));
  active_slot_ = object.value(QStringLiteral("activeSlot")).toString().trimmed();
  if (active_slot_ != QStringLiteral("secondary") && active_slot_ != QStringLiteral("tertiary")) {
    active_slot_ = QStringLiteral("primary");
  }
  QStringList next_selected_ids;
  const auto selected_items_value = object.value(QStringLiteral("selectedItemIds"));
  if (selected_items_value.isArray()) {
    const auto selected_items_array = selected_items_value.toArray();
    next_selected_ids.reserve(selected_items_array.size());
    for (const auto& value : selected_items_array) {
      next_selected_ids.push_back(value.toString().trimmed());
    }
  }
  const auto primary_selected_id = object.value(QStringLiteral("selectedItemId")).toString().trimmed();
  rebuildGroupIndices();
  ensureCurrentIndexValid();
  normalizeForCurrentTemplate();
  setSelectedItemIds(next_selected_ids, primary_selected_id);
  return true;
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
  const QFileInfo info(file_path);
  return info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
}

QVariantMap CarouselDocumentController::slideMapForIndex(int index) const {
  QVariantMap map;
  const auto* slide = slideAt(index);
  const auto* group = groupForIndex(index);
  const auto slide_rect = slideRectInDocument(index);
  const bool selected = index == current_index_;

  QString label = QStringLiteral("Empty canvas");
  QString media_type = QStringLiteral("empty");
  if (group != nullptr) {
    label = group->media.type == MediaType::Empty ? QStringLiteral("Linked spread") : summary_label(group->media);
    media_type = group->media.type == MediaType::Video ? QStringLiteral("linked-video")
                                                       : group->media.type == MediaType::Image ? QStringLiteral("linked-image")
                                                                                               : QStringLiteral("linked-empty");
  } else if (slide != nullptr) {
    if (slide->template_kind == SlideTemplate::SplitPair) {
      label = QStringLiteral("Split canvas");
      media_type = QStringLiteral("split-pair");
    } else if (slide->template_kind == SlideTemplate::StackedTriptych) {
      label = QStringLiteral("Stacked triptych");
      media_type = QStringLiteral("stacked-triptych");
    } else if (slide->primary.type != MediaType::Empty) {
      label = summary_label(slide->primary);
      media_type = slide->primary.type == MediaType::Video ? QStringLiteral("video") : QStringLiteral("image");
    }
  }

  int freeform_count = 0;
  QString freeform_label;
  for (const auto& item : items_) {
    if (item.hidden) {
      continue;
    }
    if (!slide_rect.intersects(item.bounds)) {
      continue;
    }
    ++freeform_count;
    if (freeform_label.isEmpty()) {
      freeform_label = summary_label(item);
    }
  }
  if (freeform_count > 0) {
    if (media_type == QStringLiteral("empty")) {
      media_type = QStringLiteral("freeform");
      label = freeform_count == 1 ? freeform_label
                                  : QStringLiteral("%1 + %2 more").arg(freeform_label).arg(freeform_count - 1);
    } else {
      label += QStringLiteral(" + %1 layer%2")
                   .arg(freeform_count)
                   .arg(freeform_count == 1 ? QString{} : QStringLiteral("s"));
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
  map.insert(QStringLiteral("primaryHasMedia"), group != nullptr ? group->media.type != MediaType::Empty
                                                                  : slide != nullptr ? slide->primary.type != MediaType::Empty
                                                                                    : false);
  map.insert(QStringLiteral("secondaryHasMedia"), slide != nullptr ? slide->secondary.type != MediaType::Empty : false);
  map.insert(QStringLiteral("tertiaryHasMedia"), slide != nullptr ? slide->tertiary.type != MediaType::Empty : false);
  map.insert(QStringLiteral("title"), QStringLiteral("%1").arg(index + 1, 2, 10, QLatin1Char('0')));
  map.insert(QStringLiteral("mediaType"), media_type);
  map.insert(QStringLiteral("label"), label);
  map.insert(QStringLiteral("itemCount"), freeform_count);
  return map;
}

QVariantMap CarouselDocumentController::itemMap(const CanvasItem& item) const {
  QVariantMap map;
  map.insert(QStringLiteral("itemId"), item.id);
  QString type = QStringLiteral("image");
  if (item.kind == ItemKind::Text) {
    type = QStringLiteral("text");
  } else if (item.kind == ItemKind::Video) {
    type = QStringLiteral("video");
  }
  map.insert(QStringLiteral("type"), type);
  map.insert(QStringLiteral("x"), item.bounds.x());
  map.insert(QStringLiteral("y"), item.bounds.y());
  map.insert(QStringLiteral("width"), item.bounds.width());
  map.insert(QStringLiteral("height"), item.bounds.height());
  map.insert(QStringLiteral("centerX"), item.bounds.center().x());
  map.insert(QStringLiteral("centerY"), item.bounds.center().y());
  map.insert(QStringLiteral("rotation"), item.rotation_degrees);
  map.insert(QStringLiteral("fitMode"), fitModeName(item.fit_mode));
  map.insert(QStringLiteral("selected"), selected_item_ids_.contains(item.id));
  map.insert(QStringLiteral("primarySelected"), item.id == selected_item_id_);
  map.insert(QStringLiteral("hidden"), item.hidden);
  map.insert(QStringLiteral("locked"), item.locked);
  map.insert(QStringLiteral("displayLabel"), summary_label(item));
  map.insert(QStringLiteral("sourceUrl"), fileUrlForPath(item.file_path));
  map.insert(QStringLiteral("text"), item.text);
  map.insert(QStringLiteral("textColor"), item.text_color);
  map.insert(QStringLiteral("textPointSize"), item.text_point_size);
  map.insert(QStringLiteral("slideIndex"), slideIndexForItem(item));
  return map;
}

QVariantMap CarouselDocumentController::selectionMap() const {
  QVariantMap map;
  const auto* item = currentItem();
  const auto* slide = slideAt(current_index_);
  const auto* group = groupForIndex(current_index_);
  const auto* media = currentEditableMedia();
  const auto unlocked_selected_indexes = selectedItemIndexes(false, true);
  QVariantList selected_item_ids;
  selected_item_ids.reserve(selected_item_ids_.size());
  for (const auto& item_id : selected_item_ids_) {
    selected_item_ids.push_back(item_id);
  }

  map.insert(QStringLiteral("currentIndex"), current_index_);
  map.insert(QStringLiteral("slideNumber"), current_index_ + 1);
  map.insert(QStringLiteral("hasSelection"), item != nullptr);
  map.insert(QStringLiteral("selectedCount"), selected_item_ids_.size());
  map.insert(QStringLiteral("hasMultipleSelection"), selected_item_ids_.size() > 1);
  map.insert(QStringLiteral("editableSelectionCount"), unlocked_selected_indexes.size());
  map.insert(QStringLiteral("hasUnlockedSelection"), !unlocked_selected_indexes.isEmpty());
  map.insert(QStringLiteral("selectedItemIds"), selected_item_ids);
  map.insert(QStringLiteral("itemId"), item != nullptr ? item->id : QString{});
  QString item_type = QStringLiteral("none");
  if (item != nullptr) {
    if (item->kind == ItemKind::Text) {
      item_type = QStringLiteral("text");
    } else if (item->kind == ItemKind::Video) {
      item_type = QStringLiteral("video");
    } else {
      item_type = QStringLiteral("image");
    }
  }
  map.insert(QStringLiteral("itemType"), item_type);
  map.insert(QStringLiteral("isText"), item != nullptr && item->kind == ItemKind::Text);
  map.insert(QStringLiteral("isImage"), item != nullptr && item->kind == ItemKind::Image);
  map.insert(QStringLiteral("isVideo"), item != nullptr && item->kind == ItemKind::Video);
  map.insert(QStringLiteral("layerDisplayLabel"), item != nullptr ? summary_label(*item) : QStringLiteral("Nothing selected"));
  map.insert(QStringLiteral("displayLabel"),
             item != nullptr ? summary_label(*item) : media != nullptr ? summary_label(*media) : QStringLiteral("Nothing selected"));
  map.insert(QStringLiteral("itemFitMode"),
             item != nullptr && item->kind != ItemKind::Text ? fitModeName(item->fit_mode) : QStringLiteral("cover"));
  map.insert(QStringLiteral("x"), item != nullptr ? item->bounds.x() : 0.0);
  map.insert(QStringLiteral("y"), item != nullptr ? item->bounds.y() : 0.0);
  map.insert(QStringLiteral("width"), item != nullptr ? item->bounds.width() : 0.0);
  map.insert(QStringLiteral("height"), item != nullptr ? item->bounds.height() : 0.0);
  map.insert(QStringLiteral("centerX"), item != nullptr ? item->bounds.center().x() : 0.0);
  map.insert(QStringLiteral("centerY"), item != nullptr ? item->bounds.center().y() : 0.0);
  map.insert(QStringLiteral("rotation"), item != nullptr ? item->rotation_degrees : 0.0);
  map.insert(QStringLiteral("hidden"), item != nullptr ? item->hidden : false);
  map.insert(QStringLiteral("locked"), item != nullptr ? item->locked : false);
  map.insert(QStringLiteral("text"), item != nullptr ? item->text : QString{});
  map.insert(QStringLiteral("textColor"), item != nullptr ? item->text_color : QStringLiteral("#1e1a17"));
  map.insert(QStringLiteral("textPointSize"), item != nullptr ? item->text_point_size : 72.0);
  map.insert(QStringLiteral("layerCount"), items_.size());

  map.insert(QStringLiteral("isLinkedGroup"), group != nullptr);
  map.insert(QStringLiteral("groupKind"), group != nullptr ? groupKindName(group->kind) : QStringLiteral("none"));
  map.insert(QStringLiteral("groupSpan"), group != nullptr ? group->span : 0);
  map.insert(QStringLiteral("templateKind"), templateNameForIndex(current_index_));
  map.insert(QStringLiteral("activeSlot"), currentActiveSlot());
  map.insert(QStringLiteral("canUseSecondary"),
             slide != nullptr &&
                 (slide->template_kind == SlideTemplate::SplitPair ||
                  slide->template_kind == SlideTemplate::StackedTriptych));
  map.insert(QStringLiteral("canUseTertiary"),
             slide != nullptr && slide->template_kind == SlideTemplate::StackedTriptych);
  map.insert(QStringLiteral("hasMedia"), media != nullptr && media->type != MediaType::Empty);
  map.insert(QStringLiteral("mediaType"),
             media == nullptr ? QStringLiteral("empty")
                              : media->type == MediaType::Image ? QStringLiteral("image")
                                                                : media->type == MediaType::Video ? QStringLiteral("video")
                                                                                                  : QStringLiteral("empty"));
  map.insert(QStringLiteral("slotDisplayLabel"), media != nullptr ? summary_label(*media) : QStringLiteral("Empty slot"));
  map.insert(QStringLiteral("mediaFitMode"), media != nullptr ? fitModeName(media->fit_mode) : QStringLiteral("cover"));
  map.insert(QStringLiteral("fitMode"),
             item != nullptr && item->kind != ItemKind::Text ? fitModeName(item->fit_mode)
                                                             : media != nullptr ? fitModeName(media->fit_mode)
                                                                                : QStringLiteral("cover"));
  map.insert(QStringLiteral("scale"), media != nullptr ? media->scale : 1.0);
  map.insert(QStringLiteral("offsetX"), media != nullptr ? media->offset_x : 0.0);
  map.insert(QStringLiteral("offsetY"), media != nullptr ? media->offset_y : 0.0);
  map.insert(QStringLiteral("canExportVideo"), !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty());
  map.insert(QStringLiteral("canPlaceVideo"),
             group == nullptr && slide != nullptr && slide->template_kind == SlideTemplate::Single);
  return map;
}

QString CarouselDocumentController::templateNameForIndex(int index) const {
  const auto* group = groupForIndex(index);
  if (group != nullptr) {
    return group->kind == GroupKind::HorizontalPair ? QStringLiteral("linked-horizontal-pair")
                                                    : QStringLiteral("single");
  }

  const auto* slide = slideAt(index);
  if (slide == nullptr) {
    return QStringLiteral("single");
  }
  if (slide->template_kind == SlideTemplate::SplitPair) {
    return QStringLiteral("split-pair");
  }
  if (slide->template_kind == SlideTemplate::StackedTriptych) {
    return QStringLiteral("stacked-triptych");
  }
  return QStringLiteral("single");
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
    case GroupKind::None:
      break;
  }
  return QStringLiteral("none");
}

int CarouselDocumentController::spanForGroupKind(GroupKind kind) const {
  switch (kind) {
    case GroupKind::HorizontalPair:
      return 2;
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
  return GroupKind::None;
}

bool CarouselDocumentController::currentSelectionIsLinked() const {
  return groupForIndex(current_index_) != nullptr;
}

QString CarouselDocumentController::currentActiveSlot() const {
  const auto* slide = slideAt(current_index_);
  if (slide == nullptr) {
    return QStringLiteral("primary");
  }
  if (slide->template_kind == SlideTemplate::Single) {
    return QStringLiteral("primary");
  }
  if (slide->template_kind == SlideTemplate::SplitPair && active_slot_ == QStringLiteral("tertiary")) {
    return QStringLiteral("primary");
  }
  return active_slot_;
}

QSize CarouselDocumentController::currentMediaViewportSize() const {
  if (currentSelectionIsLinked()) {
    if (const auto* group = groupForIndex(current_index_)) {
      const auto slide_size = exportPixelSize();
      return QSize(slide_size.width() * qMax(1, group->span), slide_size.height());
    }
    return exportPixelSize();
  }

  const auto* slide = slideAt(current_index_);
  if (slide == nullptr) {
    return exportPixelSize();
  }

  const auto slide_size = exportPixelSize();
  switch (slide->template_kind) {
    case SlideTemplate::SplitPair:
      return QSize(slide_size.width() / 2, slide_size.height());
    case SlideTemplate::StackedTriptych:
      return QSize(slide_size.width(), slide_size.height() / 3);
    case SlideTemplate::Single:
      break;
  }
  return slide_size;
}

QSize CarouselDocumentController::effectiveMediaSourceSize(const MediaItem& media) const {
  if (media.type == MediaType::Video) {
    return probeVideoSize(media.file_path);
  }

  QImageReader reader(media.file_path);
  QSize source_size = reader.size();
  bool using_decoded_size = false;
  if (!source_size.isValid()) {
    QImage decoded;
    if (decodeEditedImage(media, &decoded, true, kPreviewLongEdge * 2)) {
      source_size = decoded.size();
      using_decoded_size = true;
    }
  }
  if (!source_size.isValid()) {
    return {};
  }

  if (!using_decoded_size &&
      (has_explicit_crop_rect(media.recipe_state) ||
      media.recipe_state.crop_preset != QStringLiteral("Original") ||
      !qFuzzyCompare(media.recipe_state.crop_zoom, 1.0))) {
    source_size = crop_rect_for_image(source_size, media.recipe_state).size();
  }
  return source_size;
}

qreal CarouselDocumentController::normalizedAspectRatio(const CanvasItem& item) const {
  if (item.source_size.width() > 0.0 && item.source_size.height() > 0.0) {
    return item.source_size.width() / item.source_size.height();
  }
  if (item.bounds.width() > 0.0 && item.bounds.height() > 0.0) {
    return item.bounds.width() / item.bounds.height();
  }
  return 1.0;
}

qreal CarouselDocumentController::measuredTextHeight(const CanvasItem& item) const {
  QFont font(QString::fromLatin1(kTextFontFamily));
  font.setPointSizeF(qMax(kMinTextPointSize, item.text_point_size));
  const QFontMetricsF metrics(font);
  const auto width = qMax(kMinItemSize, item.bounds.width());
  const auto text = item.text.isEmpty() ? QStringLiteral(" ") : item.text;
  const auto rect =
      metrics.boundingRect(QRectF(0.0, 0.0, width, 10000.0), Qt::TextWordWrap | Qt::AlignHCenter, text);
  return qMax(metrics.height() * 1.25, rect.height() + 10.0);
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

bool CarouselDocumentController::hasFreeformItemsOnSlide(int index) const {
  const auto rect = slideRectInDocument(index);
  for (const auto& item : items_) {
    if (item.hidden) {
      continue;
    }
    if (rect.intersects(item.bounds)) {
      return true;
    }
  }
  return false;
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

QImage CarouselDocumentController::renderSlideBaseImage(int index, const QSize& size, bool preview) const {
  QImage canvas(size, QImage::Format_ARGB32_Premultiplied);
  canvas.setColorSpace(output_color_space_for_name(outputColorSpaceForSlide(index)));
  canvas.fill(QColor(QStringLiteral("#fbf7f1")));
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  if (const auto* group = groupForIndex(index)) {
    const QSize virtual_size = QSize(size.width() * group->span, size.height());
    const QPoint origin((index - group->start_index) * size.width(), 0);
    renderMediaIntoRect(&painter, QRect(QPoint(0, 0), size), group->media, virtual_size, origin, preview);

    if (preview) {
      QPen guide_pen(QColor(QStringLiteral("#f0b35d")));
      guide_pen.setStyle(Qt::DashLine);
      guide_pen.setWidth(2);
      painter.setPen(guide_pen);
      painter.drawRect(canvas.rect().adjusted(2, 2, -2, -2));
      painter.setPen(QColor(QStringLiteral("#6e5a49")));
      painter.setFont(QFont(QStringLiteral("Menlo"), 14, QFont::DemiBold));
      painter.drawText(canvas.rect().adjusted(14, 14, -14, -14), Qt::AlignTop | Qt::AlignLeft,
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
      painter.setPen(QPen(QColor(QStringLiteral("#e8b35d")), 2));
      painter.drawLine(left_rect.right(), 0, left_rect.right(), size.height());
    }
    return canvas;
  }

  if (slide->template_kind == SlideTemplate::StackedTriptych) {
    const int top_height = size.height() / 3;
    const int middle_height = size.height() / 3;
    const QRect top_rect(0, 0, size.width(), top_height);
    const QRect middle_rect(0, top_rect.bottom() + 1, size.width(), middle_height);
    const QRect bottom_rect(0, middle_rect.bottom() + 1, size.width(), size.height() - top_height - middle_height);
    renderMediaIntoRect(&painter, top_rect, slide->primary, top_rect.size(), QPoint(0, 0), preview);
    renderMediaIntoRect(&painter, middle_rect, slide->secondary, middle_rect.size(), QPoint(0, 0), preview);
    renderMediaIntoRect(&painter, bottom_rect, slide->tertiary, bottom_rect.size(), QPoint(0, 0), preview);
    if (preview) {
      painter.setPen(QPen(QColor(QStringLiteral("#e8b35d")), 2));
      painter.drawLine(0, top_rect.bottom(), size.width(), top_rect.bottom());
      painter.drawLine(0, middle_rect.bottom(), size.width(), middle_rect.bottom());
    }
    return canvas;
  }

  renderMediaIntoRect(&painter, QRect(QPoint(0, 0), size), slide->primary, size, QPoint(0, 0), preview);
  return canvas;
}

QImage CarouselDocumentController::renderSlideImage(int index, const QSize& size, bool preview) const {
  auto canvas = renderSlideBaseImage(index, size, preview);
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  for (const auto& item : items_) {
    renderItemIntoSlide(&painter, item, index, size, preview);
  }
  canvas.setColorSpace(output_color_space_for_name(outputColorSpaceForSlide(index)));
  return canvas;
}

QString CarouselDocumentController::outputColorSpaceForSlide(int index) const {
  const auto normalized_from_media = [](const MediaItem& media) {
    return media.type == MediaType::Image
               ? normalized_output_color_space_name(media.recipe_state.output_color_space)
               : QStringLiteral("sRGB");
  };
  const auto normalized_from_item = [](const CanvasItem& item) {
    return item.kind == ItemKind::Image
               ? normalized_output_color_space_name(item.recipe_state.output_color_space)
               : QStringLiteral("sRGB");
  };

  if (const auto* group = groupForIndex(index)) {
    return normalized_from_media(group->media);
  }

  if (const auto* slide = slideAt(index)) {
    if (slide->primary.type == MediaType::Image) {
      return normalized_from_media(slide->primary);
    }
    if (slide->secondary.type == MediaType::Image) {
      return normalized_from_media(slide->secondary);
    }
    if (slide->tertiary.type == MediaType::Image) {
      return normalized_from_media(slide->tertiary);
    }
  }

  for (const auto& item : items_) {
    if (slideIndexForItem(item) == index && item.kind == ItemKind::Image) {
      return normalized_from_item(item);
    }
  }

  return QStringLiteral("sRGB");
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

  if (native_image_supported(media.file_path)) {
    if (!decode_native_linear_image(media.file_path, image, preview ? max_edge : 0)) {
      return false;
    }
  } else {
    QImageReader reader(media.file_path);
    reader.setAutoTransform(true);
    if (reader.canRead()) {
      if (preview && max_edge > 0) {
        const auto source_size = reader.size();
        if (source_size.isValid()) {
          reader.setScaledSize(source_size.scaled(max_edge, max_edge, Qt::KeepAspectRatio));
        }
      }

      *image = normalize_preview_color_space(reader.read());
    } else if (!decode_raw_linear_image(media.file_path, image, preview ? max_edge : 0)) {
      return false;
    }
  }

  if (image->isNull()) {
    return false;
  }

  *image = normalize_preview_color_space(std::move(*image));

  if (has_explicit_crop_rect(media.recipe_state) ||
      media.recipe_state.crop_preset != QStringLiteral("Original") ||
      !qFuzzyCompare(media.recipe_state.crop_zoom, 1.0)) {
    *image = image->copy(crop_rect_for_image(image->size(), media.recipe_state));
  }
  apply_color_adjustments(image, media.recipe_state);
  return !image->isNull();
}

bool CarouselDocumentController::decodeEditedImage(
    const CanvasItem& item,
    QImage* image,
    bool preview,
    int max_edge) const {
  if (item.kind != ItemKind::Image) {
    return false;
  }

  MediaItem media;
  media.type = MediaType::Image;
  media.file_path = item.file_path;
  media.recipe_state = item.recipe_state;
  media.recipe_json = item.recipe_json;
  return decodeEditedImage(media, image, preview, max_edge);
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
  painter->fillRect(destination_rect, QColor(QStringLiteral("#fffdf8")));

  if (media.type == MediaType::Empty || media.file_path.isEmpty()) {
    painter->setPen(QPen(QColor(QStringLiteral("#d4c4b4")), 2, Qt::DashLine));
    painter->drawRoundedRect(destination_rect.adjusted(8, 8, -8, -8), 16, 16);
    painter->setPen(QColor(QStringLiteral("#8f7e6d")));
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

void CarouselDocumentController::renderItemIntoSlide(
    QPainter* painter,
    const CanvasItem& item,
    int slide_index,
    const QSize& slide_size,
    bool preview) const {
  if (item.hidden) {
    return;
  }
  const auto export_size = exportPixelSize();
  const auto scale_x = static_cast<double>(slide_size.width()) / static_cast<double>(export_size.width());
  const auto scale_y = static_cast<double>(slide_size.height()) / static_cast<double>(export_size.height());
  const auto slide_origin_x = static_cast<double>(slide_index * export_size.width());
  const QRectF frame_rect(
      (item.bounds.x() - slide_origin_x) * scale_x,
      item.bounds.y() * scale_y,
      item.bounds.width() * scale_x,
      item.bounds.height() * scale_y);

  if (!frame_rect.adjusted(-12.0, -12.0, 12.0, 12.0).intersects(QRectF(QPointF(0.0, 0.0), QSizeF(slide_size)))) {
    return;
  }

  painter->save();
  painter->translate(frame_rect.center());
  painter->rotate(item.rotation_degrees);
  const QRectF local_rect(-frame_rect.width() / 2.0, -frame_rect.height() / 2.0, frame_rect.width(), frame_rect.height());
  if (item.kind == ItemKind::Image) {
    renderPhotoItem(painter, item, local_rect, preview);
  } else if (item.kind == ItemKind::Video) {
    renderVideoItem(painter, item, local_rect);
  } else {
    renderTextItem(painter, item, local_rect);
  }
  painter->restore();
}

void CarouselDocumentController::renderPhotoItem(
    QPainter* painter,
    const CanvasItem& item,
    const QRectF& frame_rect,
    bool preview) const {
  QImage source_image;
  if (!decodeEditedImage(item, &source_image, preview, kPreviewLongEdge * 3)) {
    painter->setPen(QColor(QStringLiteral("#a56a5e")));
    painter->drawText(frame_rect, Qt::AlignCenter | Qt::TextWordWrap, QStringLiteral("Unable to read image"));
    return;
  }

  painter->save();
  painter->setClipRect(frame_rect);
  const auto base_scale = item.fit_mode == FitMode::Contain
                              ? qMin(frame_rect.width() / source_image.width(), frame_rect.height() / source_image.height())
                              : qMax(frame_rect.width() / source_image.width(), frame_rect.height() / source_image.height());
  const QSizeF draw_size(source_image.width() * base_scale, source_image.height() * base_scale);
  QRectF draw_rect(QPointF(0.0, 0.0), draw_size);
  draw_rect.moveCenter(frame_rect.center());
  painter->drawImage(draw_rect, source_image);
  painter->restore();
}

void CarouselDocumentController::renderVideoItem(
    QPainter* painter,
    const CanvasItem& item,
    const QRectF& frame_rect) const {
  MediaItem media;
  media.type = MediaType::Video;
  media.file_path = item.file_path;
  media.display_label = item.display_label;

  painter->save();
  painter->setClipRect(frame_rect);
  const auto placeholder =
      renderVideoPlaceholder(media, frame_rect.size().toSize().expandedTo(QSize(1, 1)));
  painter->drawImage(frame_rect, placeholder);
  painter->restore();
}

void CarouselDocumentController::renderTextItem(
    QPainter* painter,
    const CanvasItem& item,
    const QRectF& frame_rect) const {
  QFont font(QString::fromLatin1(kTextFontFamily));
  const auto scale = frame_rect.height() / qMax(1.0, item.bounds.height());
  font.setPointSizeF(qMax(6.0, item.text_point_size * scale));
  font.setWeight(QFont::DemiBold);
  painter->setFont(font);
  painter->setPen(QColor(item.text_color));
  painter->drawText(frame_rect, Qt::AlignCenter | Qt::TextWordWrap, item.text);
}

void CarouselDocumentController::adjustItemsForFormatChange(const QSize& old_slide_size, const QSize& new_slide_size) {
  if (!old_slide_size.isValid() || !new_slide_size.isValid() || items_.isEmpty()) {
    return;
  }

  const auto old_width = static_cast<double>(old_slide_size.width() * slideCount());
  const auto new_width = static_cast<double>(new_slide_size.width() * slideCount());
  const auto scale_x = new_width / qMax(1.0, old_width);
  const auto scale_y = static_cast<double>(new_slide_size.height()) / qMax(1.0, static_cast<double>(old_slide_size.height()));

  for (auto& item : items_) {
    item.bounds = QRectF(
        item.bounds.x() * scale_x,
        item.bounds.y() * scale_y,
        item.bounds.width() * scale_x,
        item.bounds.height() * scale_y);
    if (item.kind == ItemKind::Text) {
      item.text_point_size *= scale_y;
      const auto center = item.bounds.center();
      item.bounds.setHeight(measuredTextHeight(item));
      item.bounds.moveCenter(center);
    }
  }
}

void CarouselDocumentController::adjustItemsForSlidesShifted(int start_index, int delta_slides) {
  if (delta_slides == 0) {
    return;
  }
  const auto slide_width = exportPixelSize().width();
  const auto threshold = static_cast<double>(start_index * slide_width);
  for (auto& item : items_) {
    if (item.bounds.center().x() >= threshold) {
      item.bounds.translate(static_cast<double>(delta_slides * slide_width), 0.0);
    }
  }
}

void CarouselDocumentController::adjustItemsForSlideSwap(int first_index, int second_index) {
  if (first_index == second_index) {
    return;
  }

  const auto slide_width = exportPixelSize().width();
  const auto delta = static_cast<double>((second_index - first_index) * slide_width);
  for (auto& item : items_) {
    const auto item_slide = slideIndexForItem(item);
    if (item_slide == first_index) {
      item.bounds.translate(delta, 0.0);
    } else if (item_slide == second_index) {
      item.bounds.translate(-delta, 0.0);
    }
  }
}

void CarouselDocumentController::adjustItemsForBlockMove(int start_index, int span, int destination_index) {
  if (span <= 0 || start_index == destination_index) {
    return;
  }

  const auto slide_width = exportPixelSize().width();
  if (destination_index < start_index) {
    const auto displaced_index = destination_index;
    for (auto& item : items_) {
      const auto item_slide = slideIndexForItem(item);
      if (item_slide >= start_index && item_slide < start_index + span) {
        item.bounds.translate(-static_cast<double>(slide_width), 0.0);
      } else if (item_slide == displaced_index) {
        item.bounds.translate(static_cast<double>(slide_width * span), 0.0);
      }
    }
    return;
  }

  const auto displaced_index = start_index + span;
  for (auto& item : items_) {
    const auto item_slide = slideIndexForItem(item);
    if (item_slide >= start_index && item_slide < start_index + span) {
      item.bounds.translate(static_cast<double>(slide_width), 0.0);
    } else if (item_slide == displaced_index) {
      item.bounds.translate(-static_cast<double>(slide_width * span), 0.0);
    }
  }
}

void CarouselDocumentController::emitItemGeometryChanged(bool slides_changed) {
  emit itemsChanged();
  emit selectionChanged();
  if (slides_changed) {
    emit slidesChanged();
  }
}

void CarouselDocumentController::ensureCurrentIndexValid() {
  current_index_ = qBound(0, current_index_, qMax(0, static_cast<int>(slides_.size()) - 1));
  normalizeForCurrentTemplate();
}

void CarouselDocumentController::refreshPreviews() {
  QDir().mkpath(previewsRoot());
  ++preview_revision_;
  for (int index = 0; index < slides_.size(); ++index) {
    refreshPreviewAt(index);
  }
}

void CarouselDocumentController::refreshPreviewAt(int index) {
  if (index < 0 || index >= slides_.size()) {
    return;
  }

  QDir().mkpath(previewsRoot());
  const auto preview_size = exportPixelSize().scaled(kPreviewLongEdge, kPreviewLongEdge, Qt::KeepAspectRatio);
  writePreviewForSlide(index, renderSlideBaseImage(index, preview_size, true));
}

void CarouselDocumentController::refreshCurrentSelectionPreviews() {
  QDir().mkpath(previewsRoot());
  ++preview_revision_;
  if (const auto* group = groupForIndex(current_index_)) {
    for (int index = group->start_index; index < group->start_index + group->span; ++index) {
      refreshPreviewAt(index);
    }
    return;
  }

  refreshPreviewAt(current_index_);
  const auto item_index = indexOfItem(selected_item_id_);
  if (item_index >= 0) {
    const auto center_slide = slideIndexForItem(items_.at(item_index));
    refreshPreviewAt(center_slide);
    if (center_slide > 0) {
      refreshPreviewAt(center_slide - 1);
    }
    if (center_slide + 1 < slideCount()) {
      refreshPreviewAt(center_slide + 1);
    }
  }
}

QStringList CarouselDocumentController::normalizedSelectedItemIds(const QStringList& item_ids) const {
  QStringList normalized_ids;
  normalized_ids.reserve(item_ids.size());
  for (const auto& item_id : item_ids) {
    const auto normalized_id = item_id.trimmed();
    if (normalized_id.isEmpty() || indexOfItem(normalized_id) < 0 || normalized_ids.contains(normalized_id)) {
      continue;
    }
    normalized_ids.push_back(normalized_id);
  }
  return normalized_ids;
}

QVector<int> CarouselDocumentController::selectedItemIndexes(bool primary_slide_only, bool unlocked_only) const {
  QVector<int> indexes;
  indexes.reserve(selected_item_ids_.size());
  const auto selection_slide = currentSelectionSlideIndex();
  for (int index = 0; index < items_.size(); ++index) {
    const auto& item = items_.at(index);
    if (!selected_item_ids_.contains(item.id)) {
      continue;
    }
    if (primary_slide_only && selection_slide >= 0 && slideIndexForItem(item) != selection_slide) {
      continue;
    }
    if (unlocked_only && item.locked) {
      continue;
    }
    indexes.push_back(index);
  }
  return indexes;
}

void CarouselDocumentController::normalizeSelection() {
  setSelectedItemIds(selected_item_ids_, selected_item_id_);
}

void CarouselDocumentController::emitCanvasStateChanged(bool slides_changed) {
  emit itemsChanged();
  emit selectionChanged();
  if (slides_changed) {
    emit slidesChanged();
  }
}

void CarouselDocumentController::setSelectedItemIds(
    const QStringList& item_ids,
    const QString& primary_item_id) {
  auto normalized_ids = normalizedSelectedItemIds(item_ids);
  auto normalized_primary = primary_item_id.trimmed();
  if (!normalized_primary.isEmpty() && !normalized_ids.contains(normalized_primary) &&
      indexOfItem(normalized_primary) >= 0) {
    normalized_ids.push_back(normalized_primary);
  }

  if (normalized_ids.isEmpty()) {
    selected_item_id_.clear();
    selected_item_ids_.clear();
    return;
  }

  if (normalized_primary.isEmpty() || !normalized_ids.contains(normalized_primary)) {
    normalized_primary = normalized_ids.back();
  } else {
    normalized_ids.removeAll(normalized_primary);
    normalized_ids.push_back(normalized_primary);
  }

  selected_item_id_ = normalized_primary;
  selected_item_ids_ = normalized_ids;
}

void CarouselDocumentController::setSelectedItemId(const QString& item_id) {
  setSelectedItemIds(item_id.trimmed().isEmpty() ? QStringList{} : QStringList{item_id}, item_id);
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
    const QString& slot_name) {
  const auto normalized = normalized_path(file_path);
  if (!isImageFile(normalized)) {
    setStatus(QStringLiteral("That file is not a supported image format."));
    return;
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

  if (currentSelectionIsLinked()) {
    if (auto* group = groupForIndex(current_index_)) {
      group->media = media;
      refreshCurrentSelectionPreviews();
      emitCanvasStateChanged(true);
      setStatus(QStringLiteral("Updated the linked spread source image."));
    }
    return;
  }

  auto* slide = slideAt(current_index_);
  if (slide == nullptr) {
    return;
  }

  if (slot_name == QStringLiteral("secondary") && slide->template_kind == SlideTemplate::Single) {
    slide->template_kind = SlideTemplate::SplitPair;
  }
  if (slot_name == QStringLiteral("tertiary") && slide->template_kind != SlideTemplate::StackedTriptych) {
    slide->template_kind = SlideTemplate::StackedTriptych;
  }

  if (slot_name == QStringLiteral("secondary") &&
      (slide->template_kind == SlideTemplate::SplitPair || slide->template_kind == SlideTemplate::StackedTriptych)) {
    slide->secondary = media;
    active_slot_ = QStringLiteral("secondary");
    setStatus(slide->template_kind == SlideTemplate::SplitPair
                  ? QStringLiteral("Added an image to the secondary half.")
                  : QStringLiteral("Added an image to the middle slot."));
  } else if (slot_name == QStringLiteral("tertiary") &&
             slide->template_kind == SlideTemplate::StackedTriptych) {
    slide->tertiary = media;
    active_slot_ = QStringLiteral("tertiary");
    setStatus(QStringLiteral("Added an image to the bottom slot."));
  } else {
    if (slide->template_kind == SlideTemplate::Single) {
      slide->secondary = MediaItem{};
      slide->tertiary = MediaItem{};
    }
    slide->primary = media;
    active_slot_ = QStringLiteral("primary");
    setStatus(QStringLiteral("Added an image to the current slot."));
  }

  refreshCurrentSelectionPreviews();
  emitCanvasStateChanged(true);
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

  const auto removed_slides = qMax(0, group_copy.span - 1);
  if (removed_slides > 0) {
    const auto slide_width = exportPixelSize().width();
    const auto removed_left = static_cast<double>((group_copy.start_index + 1) * slide_width);
    const auto removed_right = removed_left + static_cast<double>(removed_slides * slide_width);
    for (qsizetype item_index = items_.size(); item_index > 0; --item_index) {
      auto& item = items_[item_index - 1];
      const auto center_x = item.bounds.center().x();
      if (center_x >= removed_left && center_x < removed_right) {
        items_.removeAt(item_index - 1);
        continue;
      }
      if (center_x >= removed_right) {
        item.bounds.translate(-static_cast<double>(slide_width * removed_slides), 0.0);
      }
    }
  }

  current_index_ = group_copy.start_index;
  normalizeSelection();
  if (group_copy.span > 1) {
    slides_.remove(group_copy.start_index + 1, group_copy.span - 1);
  }

  auto* slide = slideAt(current_index_);
  if (slide != nullptr) {
    slide->linked_group_id.clear();
    slide->template_kind = SlideTemplate::Single;
    slide->secondary = MediaItem{};
    slide->tertiary = MediaItem{};
    if (keep_current_media) {
      slide->primary = group_copy.media;
    }
  }

  rebuildGroupIndices();
  ensureCurrentIndexValid();
}

void CarouselDocumentController::clearGroupIndices(const LinkedGroup& group) {
  for (int index = group.start_index; index < group.start_index + group.span && index < slides_.size(); ++index) {
    if (auto* slide = slideAt(index)) {
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
  adjustItemsForSlidesShifted(start_index + 1, additional_slides);
  rebuildGroupIndices();
  return true;
}

void CarouselDocumentController::normalizeForCurrentTemplate() {
  const auto* slide = slideAt(current_index_);
  if (slide == nullptr || slide->template_kind == SlideTemplate::Single) {
    active_slot_ = QStringLiteral("primary");
    return;
  }
  if (slide->template_kind == SlideTemplate::SplitPair && active_slot_ == QStringLiteral("tertiary")) {
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
