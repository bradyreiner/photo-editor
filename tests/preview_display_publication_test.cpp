#include "preview_display_publication.h"

#include <QColor>
#include <QColorSpace>
#include <QCoreApplication>
#include <QImage>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QImage fixture(const QColorSpace& color_space = {}) {
  QImage image(2, 1, QImage::Format_ARGB32);
  image.setPixelColor(0, 0, QColor(220, 40, 30, 191));
  image.setPixelColor(1, 0, QColor(20, 170, 80, 127));
  if (color_space.isValid()) {
    image.setColorSpace(color_space);
  }
  return image;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using namespace photo_editor::desktop;

  publish_preview_image_to_display(nullptr, {}, {});
  auto null_image = QImage{};
  publish_preview_image_to_display(&null_image, {}, {});

  auto manual_state = EditRecipeState{};
  manual_state.monitor_color_profile = QStringLiteral("Display P3");
  auto manual_image = fixture(QColorSpace(QColorSpace::SRgb));
  publish_preview_image_to_display(&manual_image, manual_state, {});
  if (manual_image.isNull() ||
      manual_image.colorSpace() != QColorSpace(QColorSpace::DisplayP3) ||
      manual_image.pixelColor(0, 0).alpha() != 191 ||
      manual_image.pixelColor(1, 0).alpha() != 127) {
    return fail("manual monitor publication must convert last and preserve alpha");
  }
  const auto already_published = manual_image;
  publish_preview_image_to_display(&manual_image, manual_state, {});
  if (manual_image != already_published ||
      manual_image.colorSpace() != already_published.colorSpace()) {
    return fail("already-published monitor pixels must not be transformed twice");
  }

  const auto system_profile = SystemMonitorProfileSnapshot{
      .color_space = QColorSpace(QColorSpace::DisplayP3),
      .display_name = QStringLiteral("System P3"),
      .identity = QStringLiteral("display:test|p3"),
      .display_identity = QStringLiteral("display:test"),
  };
  auto system_image = fixture(QColorSpace(QColorSpace::SRgb));
  publish_preview_image_to_display(&system_image, {}, system_profile);
  if (system_image.colorSpace() != system_profile.color_space) {
    return fail("system monitor ICC conversion must be the final preview step");
  }

  auto untagged_state = EditRecipeState{};
  untagged_state.preview_color_space = QStringLiteral("Display P3");
  auto untagged_image = fixture();
  publish_preview_image_to_display(&untagged_image, untagged_state, {});
  if (untagged_image.colorSpace() != QColorSpace(QColorSpace::DisplayP3)) {
    return fail("untagged preview pixels must normalize before output publication");
  }

  auto proof_state = EditRecipeState{};
  proof_state.soft_proof_enabled = true;
  proof_state.preview_color_space = QStringLiteral("sRGB");
  auto proof_image = fixture(QColorSpace(QColorSpace::DisplayP3));
  publish_preview_image_to_display(&proof_image, proof_state, {});
  if (proof_image.colorSpace() != QColorSpace(QColorSpace::DisplayP3)) {
    return fail("proof-published pixels must not receive an extra output conversion");
  }
  return EXIT_SUCCESS;
}
