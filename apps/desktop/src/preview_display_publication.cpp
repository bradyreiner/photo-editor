#include "preview_display_publication.h"

#include "color_profile_asset_bridge.h"
#include "edit_recipe_pipeline_bridge.h"
#include "preview_output_color_space.h"
#include "qimage_color_conversion.h"

#include <QByteArray>
#include <QColorSpace>
#include <QImage>

#include <utility>

namespace photo_editor::desktop {
namespace {

QImage normalized_preview_image(QImage image) {
  if (image.isNull()) {
    return image;
  }
  const auto target = preview_color_space();
  if (image.colorSpace().isValid() && image.colorSpace() != target) {
    const auto converted = convert_tagged_qimage_color_space(
        image, target, QImageColorConversionPurpose::DecodeNormalization);
    if (!converted.isNull()) {
      image = converted;
    }
  }
  if (image.format() != QImage::Format_ARGB32) {
    image = image.convertToFormat(QImage::Format_ARGB32);
  }
  image.setColorSpace(target);
  return image;
}

void transform_to_named_output(QImage* image, const QString& output_name) {
  if (image == nullptr || image->isNull()) {
    return;
  }
  const auto target = preview_output_color_space_for_name(output_name);
  if (!target.isValid()) {
    return;
  }
  auto source = image->colorSpace().isValid()
                    ? *image
                    : normalized_preview_image(*image);
  if (source.colorSpace() == target) {
    source.setColorSpace(target);
    *image = std::move(source);
    return;
  }
  const auto converted = convert_tagged_qimage_color_space(
      source, target, QImageColorConversionPurpose::OutputPublication);
  if (!converted.isNull()) {
    *image = converted;
  }
}

}  // namespace

void publish_preview_image_to_display(
    QImage* image,
    const EditRecipeState& state,
    const SystemMonitorProfileSnapshot& system_profile) {
  if (image == nullptr || image->isNull()) {
    return;
  }
  const auto monitor_profile =
      normalized_monitor_color_profile_name(state.monitor_color_profile);
  if (image->colorSpace().isValid()) {
    const auto final_space = monitor_profile.isEmpty()
                                 ? system_profile.color_space
                                 : preview_output_color_space_for_name(
                                       monitor_profile);
    if (final_space.isValid() && image->colorSpace() == final_space) {
      image->setColorSpace(final_space);
      return;
    }
  }

  const auto custom_preview_profile =
      resolve_icc_profile_asset(state.preview_icc_profile_asset);
  if (custom_preview_profile.ready()) {
    const auto bytes = custom_preview_profile.payload_bytes();
    const auto custom_space = QColorSpace::fromIccProfile(QByteArray(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<qsizetype>(bytes.size())));
    if (custom_space.isValid() && image->colorSpace() == custom_space) {
      image->setColorSpace(custom_space);
      return;
    }
  }
  if (!state.soft_proof_enabled) {
    transform_to_named_output(
        image, preview_output_color_space_name_for_recipe(state));
  }
  if (!monitor_profile.isEmpty()) {
    transform_to_named_output(image, monitor_profile);
    return;
  }
  if (!system_profile.color_space.isValid()) {
    return;
  }
  auto source = image->colorSpace().isValid()
                    ? *image
                    : normalized_preview_image(*image);
  if (source.colorSpace() == system_profile.color_space) {
    source.setColorSpace(system_profile.color_space);
    *image = std::move(source);
    return;
  }
  const auto converted = convert_tagged_qimage_color_space(
      source,
      system_profile.color_space,
      QImageColorConversionPurpose::MonitorPublication);
  if (!converted.isNull()) {
    *image = converted;
  }
}

}  // namespace photo_editor::desktop
