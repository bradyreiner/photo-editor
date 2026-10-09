#include "carousel_document_controller.h"
#include "decoded_image_cache.h"
#include "edit_adjustment_patch.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <functional>
#include <iostream>
#include <utility>

namespace photo_editor::desktop {
struct CarouselHistoryTestAccess {
  static void seed(CarouselDocumentController& controller, const QString& image_path,
                   int board_count = 1) {
    controller.slides_.clear();
    controller.groups_.clear();
    controller.items_.clear();
    controller.has_active_carousel_ = true;
    controller.document_format_ = QStringLiteral("4x5");
    for (int index = 0; index < board_count; ++index) {
      CarouselDocumentController::Slide slide;
      slide.primary.type = CarouselDocumentController::MediaType::Image;
      slide.primary.file_path = image_path;
      slide.primary.source_pixel_size = QSize(1200, 800);
      controller.slides_.push_back(std::move(slide));
      CarouselDocumentController::CanvasItem text;
      text.id = QStringLiteral("text-%1").arg(index);
      text.kind = CarouselDocumentController::ItemKind::Text;
      text.bounds = QRectF(index * 1080.0 + 170.0, 300.0, 620.0, 260.0);
      text.text_color = QStringLiteral("#282020");
      text.text_point_size = 34.0;
      text.text_weight = QStringLiteral("bold");
      text.text = QStringLiteral("Contrast sample %1").arg(index);
      controller.items_.push_back(std::move(text));
    }
    controller.current_index_ = 0;
    controller.selected_item_id_ = QStringLiteral("text-0");
    controller.selected_item_ids_ = {controller.selected_item_id_};
  }
  static qsizetype cacheEntries(const CarouselDocumentController& controller) {
    return controller.text_contrast_sample_cache_.size();
  }
  static qint64 cacheBytes(const CarouselDocumentController& controller) {
    return controller.text_contrast_sample_cache_bytes_;
  }
  static void setBackground(CarouselDocumentController& controller, const QString& color) {
    controller.slides_[0].background_color = color;
  }
  static void changeCrop(CarouselDocumentController& controller) {
    auto& media = controller.slides_[0].primary;
    media.manual_crop = true;
    media.scale = 1.55;
    media.offset_x = -0.48;
    media.offset_y = 0.27;
  }
  static void changeRecipe(CarouselDocumentController& controller) {
    auto& media = controller.slides_[0].primary;
    media.recipe_state.exposure = 1.2;
    media.adjustment_patch_json = serialize_adjustment_patch_json(
        media.recipe_state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
  }
  static void addLinkedGroup(CarouselDocumentController& controller) {
    CarouselDocumentController::Slide next = controller.slides_.first();
    controller.slides_.append(next);
    CarouselDocumentController::LinkedGroup group;
    group.id = QStringLiteral("linked-sample");
    group.kind = CarouselDocumentController::GroupKind::HorizontalPair;
    group.start_index = 0;
    group.span = 2;
    group.edit_spread = true;
    group.media = controller.slides_[0].primary;
    // Keep the slot media populated because textContrastChecks currently gates
    // photo sampling from slide slots even though the renderer prefers a group.
    controller.slides_[0].linked_group_id = group.id;
    controller.slides_[1].linked_group_id = group.id;
    controller.groups_.append(std::move(group));
  }
  static void changeLinkedGroup(CarouselDocumentController& controller) {
    controller.groups_[0].edit_spread = false;
    controller.groups_[0].media.scale = 1.35;
    controller.groups_[0].media.offset_x = 0.42;
  }
  static void clonePresentation(const CarouselDocumentController& source,
                                CarouselDocumentController& destination) {
    destination.slides_ = source.slides_;
    destination.groups_ = source.groups_;
    destination.items_ = source.items_;
    destination.current_index_ = source.current_index_;
    destination.selected_item_id_ = source.selected_item_id_;
    destination.selected_item_ids_ = source.selected_item_ids_;
    destination.selected_slide_indices_ = source.selected_slide_indices_;
    destination.export_color_space_ = source.export_color_space_;
    destination.cutout_preview_mode_ = source.cutout_preview_mode_;
    destination.active_slot_ = source.active_slot_;
    destination.has_active_carousel_ = source.has_active_carousel_;
  }
};
}

namespace {
using namespace photo_editor::desktop;

bool require(bool value, const char* message) {
  if (!value) std::cerr << message << '\n';
  return value;
}

bool write_bmp(const QString& path, bool alternate) {
  QImage image(1200, 800, QImage::Format_RGB32);
  for (int y = 0; y < image.height(); ++y) {
    auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      const auto red = alternate ? (255 - x * 255 / 1199) : (x * 255 / 1199);
      const auto green = alternate ? (255 - y * 255 / 799) : (y * 255 / 799);
      const auto blue = (x * 7 + y * 3 + (alternate ? 91 : 17)) & 255;
      row[x] = qRgb(red, green, blue);
    }
  }
  return image.save(path, "BMP");
}

bool matches_fresh(const CarouselDocumentController& controller,
                   const QVariantList& cached_result) {
  CarouselDocumentController fresh;
  CarouselHistoryTestAccess::clonePresentation(controller, fresh);
  return fresh.textContrastChecks() == cached_result;
}

bool has_missing_media_issue(const QVariantList& issues) {
  for (const auto& value : issues) {
    if (value.toMap().value(QStringLiteral("code")).toString() == QStringLiteral("missing-media"))
      return true;
  }
  return false;
}

bool verify_mutation(CarouselDocumentController& controller,
                     const std::function<void()>& mutate,
                     const char* label) {
  const auto previous_entries = CarouselHistoryTestAccess::cacheEntries(controller);
  mutate();
  const auto current = controller.textContrastChecks();
  if (!require(CarouselHistoryTestAccess::cacheEntries(controller) > previous_entries,
               label) || !require(matches_fresh(controller, current),
                                 "warm contrast cache disagrees with a fresh controller"))
    return false;
  return true;
}
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  const auto source_path = root.filePath(QStringLiteral("contrast-source.bmp"));
  if (!write_bmp(source_path, false)) return 1;

  CarouselDocumentController controller;
  CarouselHistoryTestAccess::seed(controller, source_path);
  const auto cold = controller.textContrastChecks();
  if (!require(cold.size() == 1, "text contrast fixture did not produce one result")) return 1;
  const auto warmed = controller.textContrastChecks();
  if (!require(warmed == cold, "warm board sample changed contrast output") ||
      !require(CarouselHistoryTestAccess::cacheEntries(controller) == 1,
               "warm read should reuse one cached board raster") ||
      !require(matches_fresh(controller, warmed),
               "cached contrast result differs from an uncached controller")) return 1;

  if (!verify_mutation(controller,
          [&] { CarouselHistoryTestAccess::setBackground(controller, QStringLiteral("#d82c6a")); },
          "background edits must produce a new board sample key") ||
      !verify_mutation(controller,
          [&] { CarouselHistoryTestAccess::changeCrop(controller); },
          "crop edits must produce a new board sample key") ||
      !verify_mutation(controller,
          [&] { CarouselHistoryTestAccess::changeRecipe(controller); },
          "recipe edits must produce a new board sample key") ||
      !verify_mutation(controller,
          [&] { CarouselHistoryTestAccess::addLinkedGroup(controller); },
          "linked-group creation must produce a new board sample key") ||
      !verify_mutation(controller,
          [&] { CarouselHistoryTestAccess::changeLinkedGroup(controller); },
          "linked-group media/span presentation changes must produce a new key")) return 1;

  const auto previous_size = QFileInfo(source_path).size();
  const auto previous_modified = QFileInfo(source_path).lastModified();
  if (!write_bmp(source_path, true)) return 1;
  QFile restored_time(source_path);
  if (!restored_time.open(QIODevice::ReadOnly) ||
      !restored_time.setFileTime(previous_modified, QFileDevice::FileModificationTime) ||
      !require(QFileInfo(source_path).size() == previous_size,
               "same-path image replacement should preserve fixture size") ||
      !require(QFileInfo(source_path).lastModified() == previous_modified,
               "same-path image replacement should preserve the coarse modification time")) return 1;
  if (!verify_mutation(controller, [] {},
                       "same-path source replacement must miss via file identity revision")) return 1;

  const auto before_invalid = CarouselHistoryTestAccess::cacheEntries(controller);
  if (!require(!has_missing_media_issue(controller.preflightIssues()),
               "valid external source should not be reported missing")) return 1;
  if (!QFile::remove(source_path)) return 1;
  if (!require(has_missing_media_issue(controller.preflightIssues()),
               "preflight must observe an external source removal")) return 1;
  const auto invalid_source = controller.textContrastChecks();
  if (!require(!invalid_source.isEmpty(), "missing source should retain a contrast result") ||
      !require(CarouselHistoryTestAccess::cacheEntries(controller) > before_invalid,
               "missing-source preview should be isolated under its own revision key") ||
      !write_bmp(source_path, false)) return 1;
  const auto restored_source = controller.textContrastChecks();
  if (!require(matches_fresh(controller, restored_source),
               "restored source reused a stale invalid-source board sample") ||
      !require(!has_missing_media_issue(controller.preflightIssues()),
               "preflight must observe an externally restored source")) return 1;

  CarouselDocumentController many_boards;
  CarouselHistoryTestAccess::seed(many_boards, source_path, 40);
  const auto many = many_boards.textContrastChecks();
  if (!require(many.size() == 40, "multi-board contrast checks lost text items") ||
      !require(CarouselHistoryTestAccess::cacheEntries(many_boards) <= 32,
               "board raster cache exceeded its entry bound") ||
      !require(CarouselHistoryTestAccess::cacheBytes(many_boards) <= 16LL * 1024 * 1024,
               "board raster cache exceeded its byte bound")) return 1;

  std::cout << "Carousel contrast board samples preserve warm/fresh parity and invalidate on background, crop, recipe, linked group, and same-path source revisions; missing sources do not poison restored-image reads.\n";
  return 0;
}
