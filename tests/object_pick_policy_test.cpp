#include "object_pick_policy.h"

#include <cstdlib>
#include <iostream>

#include <QImage>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::desktop::SegmentationResult result_with_pixel(
    int x,
    int y,
    int width = 4,
    int height = 4) {
  photo_editor::desktop::SegmentationResult result;
  result.width = width;
  result.height = height;
  result.mask_data = QByteArray(width * height, char(0));
  result.mask_data[y * width + x] = char(0xff);
  result.success = true;
  return result;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  const auto instance = result_with_pixel(0, 0);
  const auto prompt = result_with_pixel(2, 2);
  if (!should_prefer_prompt_object_pick_result(
          instance, prompt, 0.5, 0.5) ||
      should_prefer_prompt_object_pick_result(
          prompt, instance, 0.5, 0.5) ||
      !should_prefer_prompt_object_pick_result({}, prompt, 0.5, 0.5) ||
      should_prefer_prompt_object_pick_result(instance, {}, 0.5, 0.5)) {
    return fail("object-pick arbitration must prefer valid target-centered prompt results");
  }

  if (localized_object_pick_bounds(QSize{}, 0.5, 0.5).isValid() ||
      localized_object_pick_bounds(QSize(1000, 500), 0.5, 0.5) !=
          QRect(435, 185, 130, 130) ||
      localized_object_pick_bounds(QSize(1000, 500), -1.0, -1.0) !=
          QRect(0, 0, 130, 130)) {
    return fail("localized object-pick bounds must be exact, clamped, and aspect independent");
  }

  SegmentationResult square_failure;
  square_failure.width = 100;
  square_failure.height = 100;
  square_failure.mask_data = QByteArray(10000, char(0));
  for (int y = 28; y < 72; ++y) {
    for (int x = 28; x < 72; ++x) {
      square_failure.mask_data[y * 100 + x] = char(0xff);
    }
  }
  QImage source(100, 100, QImage::Format_ARGB32);
  source.fill(Qt::black);
  if (!segmentation_result_is_localized_square_failure(
          source, square_failure, 0.5, 0.5) ||
      segmentation_result_is_localized_square_failure(
          source, result_with_pixel(2, 2, 100, 100), 0.5, 0.5) ||
      segmentation_result_is_localized_square_failure(
          QImage{}, square_failure, 0.5, 0.5)) {
    return fail("localized crop-shaped masks must be rejected without rejecting compact objects");
  }

  SegmentationResult local;
  local.width = 2;
  local.height = 2;
  local.mask_data = QByteArray("\x01\x02\x03\x04", 4);
  local.success = true;
  const auto remapped = remap_localized_segmentation_result(
      local, QRect(1, 1, 2, 2), QSize(4, 4));
  if (!remapped.success || remapped.width != 4 || remapped.height != 4 ||
      static_cast<unsigned char>(remapped.mask_data.at(5)) != 1U ||
      static_cast<unsigned char>(remapped.mask_data.at(6)) != 2U ||
      static_cast<unsigned char>(remapped.mask_data.at(9)) != 3U ||
      static_cast<unsigned char>(remapped.mask_data.at(10)) != 4U ||
      static_cast<unsigned char>(remapped.mask_data.at(0)) != 0U ||
      remap_localized_segmentation_result(
          local, QRect(3, 3, 2, 2), QSize(4, 4))
          .success) {
    return fail("localized remapping must place exact alpha and reject out-of-bounds writes");
  }

  SegmentationResult plausible;
  plausible.width = 4;
  plausible.height = 4;
  plausible.mask_data = QByteArray(16, char(0));
  for (const auto index : {5, 6, 9, 10}) {
    plausible.mask_data[index] = char(0xff);
  }
  auto full = plausible;
  full.mask_data.fill(char(0xff));
  if (!single_point_prompt_result_is_plausible(plausible, 0.5, 0.5) ||
      single_point_prompt_result_is_plausible(
          result_with_pixel(2, 2), 0.5, 0.5) ||
      single_point_prompt_result_is_plausible(full, 0.5, 0.5) ||
      single_point_prompt_result_is_plausible(plausible, 0.0, 0.0)) {
    return fail("single-point results must be centered, bounded, nontrivial, and non-global");
  }

  SegmentationResult prompt_mask;
  prompt_mask.width = 4;
  prompt_mask.height = 4;
  prompt_mask.mask_data = QByteArray(16, char(0));
  prompt_mask.mask_data[5] = char(0xff);
  prompt_mask.mask_data[6] = char(0xff);
  const QVector<SmartSelectPrompt> prompts{
      {.x = 1.0 / 3.0, .y = 1.0 / 3.0, .negative = false},
      {.x = 2.0 / 3.0, .y = 1.0 / 3.0, .negative = false},
      {.x = 2.0 / 3.0, .y = 2.0 / 3.0, .negative = true},
  };
  if (!smart_select_result_honors_prompts(prompt_mask, prompts)) {
    return fail("valid Smart Select output must contain positive and omit negative prompt points");
  }
  prompt_mask.mask_data[10] = char(0xff);
  if (smart_select_result_honors_prompts(prompt_mask, prompts)) {
    return fail("Smart Select output that includes a negative prompt must be rejected");
  }
  prompt_mask.mask_data[10] = char(0);
  prompt_mask.mask_data[5] = char(0);
  if (smart_select_result_honors_prompts(prompt_mask, prompts)) {
    return fail("Smart Select output that omits a positive prompt must be rejected");
  }
  prompt_mask = {};
  prompt_mask.width = 5;
  prompt_mask.height = 5;
  prompt_mask.mask_data = QByteArray(25, char(0));
  prompt_mask.mask_data[6] = char(0xff);
  prompt_mask.mask_data[18] = char(0xff);
  const QVector<SmartSelectPrompt> fragmented_prompts{
      {.x = 0.25, .y = 0.25, .negative = false},
      {.x = 0.75, .y = 0.75, .negative = false},
  };
  if (smart_select_result_honors_prompts(prompt_mask, fragmented_prompts)) {
    return fail("disconnected prompt-colored specks must not pass as one Smart Select region");
  }
  prompt_mask = {};
  prompt_mask.width = 6;
  prompt_mask.height = 2;
  prompt_mask.mask_data = QByteArray(12, char(0));
  prompt_mask.mask_data[0] = char(0xff);
  prompt_mask.mask_data[1] = char(0xff);
  prompt_mask.mask_data[4] = char(0xff);
  prompt_mask.mask_data[5] = char(0xff);
  const QVector<SmartSelectPrompt> separated_subject_prompts{
      {.x = 0.0, .y = 0.0, .negative = false},
      {.x = 1.0, .y = 0.0, .negative = false},
  };
  if (smart_select_result_honors_prompts(prompt_mask, separated_subject_prompts) ||
      !smart_select_result_honors_prompts(
          prompt_mask, separated_subject_prompts, true)) {
    return fail("multi-subject drags may accept separate prompted components while one-object checks stay connected");
  }
  SegmentationResult hollow;
  hollow.width = 10;
  hollow.height = 10;
  hollow.mask_data = QByteArray(100, char(0));
  for (int y = 3; y <= 6; ++y) {
    for (int x = 3; x <= 6; ++x) {
      if (x == 3 || x == 6 || y == 3 || y == 6)
        hollow.mask_data[y * 10 + x] = char(0xff);
    }
  }
  const QVector<SmartSelectPrompt> hollow_box{
      {.x = 0.5, .y = 0.5, .negative = false,
       .box = QRectF(QPointF(0.2, 0.2), QPointF(0.8, 0.8))}};
  if (!smart_select_result_honors_prompts(hollow, hollow_box) ||
      smart_select_result_honors_prompts(
          hollow, {{.x = 0.1, .y = 0.1, .negative = false,
                    .box = QRectF(QPointF(0.0, 0.0), QPointF(0.15, 0.15))}})) {
    return fail("box constraints must allow hollow objects and background corners but reject empty target bounds");
  }
  return EXIT_SUCCESS;
}
