#include "editor_preview_controller.h"

#include <QColorSpace>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>

namespace photo_editor::desktop {
RenderResult render_preview_request_for_test(const RenderRequest& request);
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QImage fixture(128, 96, QImage::Format_ARGB32);
  fixture.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  for (int y = 0; y < fixture.height(); ++y)
    for (int x = 0; x < fixture.width(); ++x)
      fixture.setPixelColor(x, y, QColor(60 + x / 2, 70 + y / 2, 85 + (x + y) / 4));
  const auto source = runtime.filePath("source.png");
  if (!fixture.save(source)) return 2;
  using namespace photo_editor::desktop;
  QByteArray alpha(16 * 16, '\0');
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 8; ++x) alpha[y * 16 + x] = char(0xff);
  bool passed = true;
  for (const auto& model : {QStringLiteral("legacy-oklch-v1"), QStringLiteral("aces2-ocio-v1")}) {
    for (const auto& profile : {QStringLiteral("Linear sRGB"), QStringLiteral("Linear Display P3"),
                              QStringLiteral("Linear ProPhoto RGB"), QStringLiteral("Linear Rec.2020")}) {
      RenderRequest request;
      request.file_path = source;
      request.source_asset_id = QStringLiteral("global-mask-fidelity");
      request.use_in_memory_preview = true;
      request.require_cpu_preview_image = true;
      request.max_edge = 128;
      request.render_reason = QStringLiteral("edit_change");
      request.dirty_domain = QStringLiteral("global_tone_color");
      request.dirty_stages = {QStringLiteral("global_tone_color"), QStringLiteral("mask_rasterization"),
                              QStringLiteral("local_adjustments"), QStringLiteral("display_transform")};
      request.state.photographic_rendering_model = model;
      request.require_canonical_render_plan = model == QStringLiteral("aces2-ocio-v1");
      request.state.working_color_profile = profile;
      request.state.exposure = .25;
      request.state.mask_stack = {QVariantMap{{"id", "preserved-mask"}, {"kind", "background"},
          {"enabled", true}, {"operation", "add"}, {"exposure", .8}, {"temperature", 20.0},
          {"opacity", 1.0}, {"density", 1.0}, {"feather", 0.0},
          {"segmentationWidth", 16}, {"segmentationHeight", 16}, {"segmentationData", alpha}}};
      request.state.active_mask_index = 0;
      request.render_mode = QStringLiteral("settled_preview");
      const auto settled = render_preview_request_for_test(request);
      request.draft_interactive = true;
      request.render_mode = QStringLiteral("live_preview");
      const auto draft = render_preview_request_for_test(request);
      request.state.mask_stack.clear();
      request.state.active_mask_index = -1;
      request.draft_interactive = false;
      request.render_mode = QStringLiteral("settled_preview");
      const auto unmasked = render_preview_request_for_test(request);
      const auto expected = settled.preview_image.convertToFormat(QImage::Format_RGBA8888);
      const auto actual = draft.preview_image.convertToFormat(QImage::Format_RGBA8888);
      const auto control = unmasked.preview_image.convertToFormat(QImage::Format_RGBA8888);
      if (!settled.success || !draft.success || !unmasked.success || actual.isNull() ||
          actual.size() != fixture.size() || expected.size() != actual.size() || control.size() != actual.size()) {
        std::cerr << "Render failed: " << settled.failure_detail.toStdString() << ' '
                  << draft.failure_detail.toStdString() << ' ' << unmasked.failure_detail.toStdString() << '\n';
        return 2;
      }
      int max_delta = 0, mask_changed = 0, protected_changed = 0;
      for (int y = 0; y < actual.height(); ++y) {
        for (int x = 0; x < actual.width(); ++x) {
          const auto offset = x * 4;
          for (int c = 0; c < 4; ++c)
            max_delta = std::max(max_delta, std::abs(int(actual.constScanLine(y)[offset + c]) -
                                                    int(expected.constScanLine(y)[offset + c])));
          if (x < actual.width() / 4 && expected.pixel(x, y) != control.pixel(x, y)) ++mask_changed;
          if (x >= actual.width() * 3 / 4 && expected.pixel(x, y) != control.pixel(x, y)) ++protected_changed;
        }
      }
      const bool case_passed = max_delta <= 1 && mask_changed > 0 && protected_changed == 0;
      std::cout << "model=" << model.toStdString() << " profile=" << profile.toStdString()
                << " draft_settled_max_delta=" << max_delta << " masked_changed=" << mask_changed
                << " protected_changed=" << protected_changed << " passed=" << case_passed << '\n';
      passed = passed && case_passed;
    }
  }
  return passed ? 0 : 1;
}
