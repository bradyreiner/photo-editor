#include "flattened_output_renderer.h"

#include <cstdint>

// Included after preview_linear_cache_test's helpers so this proof exercises
// its actual canonical RenderRequest path, independently of file export.
int run_raw_highlight_preview_proof(const QString& fixture_path) {
  using namespace photo_editor::desktop;
  if (!raw_camera_linear_decoder_capability().available) return 77;
  QTemporaryDir fixture_directory;
  if (!fixture_directory.isValid()) return 1;
  auto source_path = fixture_path;
  const auto generated_fixture = source_path.isEmpty();
  if (generated_fixture) {
    source_path = fixture_directory.filePath(QStringLiteral("local-highlight-levels.dng"));
    auto payload = photo_editor::test_fixtures::synthetic_dng();
    constexpr auto kWidth = std::size_t{128U};
    constexpr auto kHeight = std::size_t{128U};
    constexpr auto kSensorBytes = kWidth * kHeight * 2U;
    if (payload.size() < kSensorBytes) return 1;
    const auto sensor_begin = payload.size() - kSensorBytes;
    for (std::size_t y = 0U; y < kHeight; ++y) {
      for (std::size_t x = 0U; x < kWidth; ++x) {
        const auto red = y % 2U == 0U && x % 2U == 0U;
        const auto blue = y % 2U == 1U && x % 2U == 1U;
        const auto highlight = x >= 40U && x < 52U && y >= 40U && y < 52U;
        // With RAW black=.20/white=.90 these bright, consistent references
        // support a modest lift of clipped R/G. An analysis using default
        // levels estimates below the clipped signal and fails to recover it.
        // Neither route simply hits the gain cap, which would hide the bug.
        const auto sample = static_cast<std::uint16_t>(highlight
            ? (blue ? 1960U : 4095U)
            : (red ? 3490U : blue ? 1676U : 3370U));
        const auto offset = sensor_begin + (y * kWidth + x) * 2U;
        payload[offset] = static_cast<std::uint8_t>(sample & 0xffU);
        payload[offset + 1U] = static_cast<std::uint8_t>(sample >> 8U);
      }
    }
    QFile fixture(source_path);
    if (!fixture.open(QIODevice::WriteOnly) ||
        fixture.write(reinterpret_cast<const char*>(payload.data()),
                      static_cast<qint64>(payload.size())) !=
            static_cast<qint64>(payload.size())) return 1;
  }

  std::array<std::array<QImage, 2>, 2> previews;
  auto generation = 1;
  for (std::size_t levels = 0U; levels < 2U; ++levels) {
    for (std::size_t recovery = 0U; recovery < 2U; ++recovery) {
      auto state = state_for_exposure(0.0);
      state.raw_demosaic_quality = QStringLiteral("Full");
      state.raw_highlight_recovery = recovery != 0U;
      state.highlight_reconstruction_mode = QStringLiteral("Enabled");
      state.raw_black_level = levels == 0U ? 0.0 : 0.20;
      state.raw_white_level = levels == 0U ? 1.0 : 0.90;
      state.crop_rect_x = 0.125;
      state.crop_rect_y = 0.125;
      state.crop_rect_width = 0.75;
      state.crop_rect_height = 0.75;
      const auto preview = render(source_path, state, generation++, false, true, 0);
      const auto exported = render_flattened_output_file(
          QStringLiteral("raw-highlight-preview-export-proof"), source_path,
          state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32);
      if (!require(preview.success && !preview.preview_image.isNull() && !exported.isNull() &&
                       preview.preview_image.size() == exported.size(),
                   "cropped full-resolution RAW highlight preview and canonical file export must render")) {
        std::cerr << "levels=" << levels << " recovery=" << recovery
                  << " preview_status=" << preview.status.toStdString()
                  << " preview_failure=" << preview.failure_detail.toStdString()
                  << " preview_size=" << preview.preview_image.width() << 'x'
                  << preview.preview_image.height() << " export_size="
                  << exported.width() << 'x' << exported.height() << '\n';
        return 1;
      }
      const auto left = preview.preview_image.convertToFormat(QImage::Format_ARGB32);
      const auto right = exported.convertToFormat(QImage::Format_ARGB32);
      auto maximum_error = 0;
      auto absolute_error = std::uint64_t{0U};
      auto opaque_and_nonuniform = true;
      auto minimum_channel = 255;
      auto maximum_channel = 0;
      auto differing_pixels = std::size_t{0U};
      std::array<std::array<int, 8>, 16> mismatch_samples{};
      auto worst_x = -1;
      auto worst_y = -1;
      auto worst_preview = QRgb{};
      auto worst_export = QRgb{};
      for (int y = 0; y < left.height(); ++y) {
        const auto* preview_row = reinterpret_cast<const QRgb*>(left.constScanLine(y));
        const auto* export_row = reinterpret_cast<const QRgb*>(right.constScanLine(y));
        for (int x = 0; x < left.width(); ++x) {
          const auto a = preview_row[x];
          const auto b = export_row[x];
          if (a != b) {
            if (differing_pixels < mismatch_samples.size()) {
              mismatch_samples[differing_pixels] = {
                  x, y, qRed(a), qGreen(a), qBlue(a), qRed(b), qGreen(b), qBlue(b)};
            }
            ++differing_pixels;
          }
          opaque_and_nonuniform = opaque_and_nonuniform && qAlpha(a) == 255 && qAlpha(b) == 255;
          for (const auto error : {std::abs(qRed(a) - qRed(b)),
                                   std::abs(qGreen(a) - qGreen(b)),
                                   std::abs(qBlue(a) - qBlue(b))}) {
            if (error > maximum_error) {
              maximum_error = error;
              worst_x = x;
              worst_y = y;
              worst_preview = a;
              worst_export = b;
            }
            absolute_error += static_cast<std::uint64_t>(error);
          }
          minimum_channel = std::min({minimum_channel, qRed(a), qGreen(a), qBlue(a)});
          maximum_channel = std::max({maximum_channel, qRed(a), qGreen(a), qBlue(a)});
        }
      }
      const auto sample_count = static_cast<double>(left.width()) * left.height() * 3.0;
      const auto mean_error = static_cast<double>(absolute_error) / sample_count;
      // Final preview/export publication can round an encoded channel on
      // opposite sides of one 8-bit boundary. Require <=2 codes per channel
      // and <=.05 code average; a missing RAW-level analysis changes the
      // recovered highlight region and must exceed this narrow parity budget.
      if (!require(opaque_and_nonuniform && maximum_channel - minimum_channel > 8 &&
                       maximum_error <= 2 && mean_error <= 0.05,
                   "RAW highlight preview/export must agree within the final 8-bit publication budget")) {
        std::cerr << "levels=" << levels << " recovery=" << recovery
                  << " max_channel_error_codes=" << maximum_error
                  << " mean_channel_error_codes=" << mean_error
                  << " differing_pixels=" << differing_pixels
                  << " worst_crop_xy=" << worst_x << ',' << worst_y
                  << " worst_source_uv="
                  << 0.125 + (static_cast<double>(worst_x) + 0.5) / left.width() * 0.75 << ','
                  << 0.125 + (static_cast<double>(worst_y) + 0.5) / left.height() * 0.75
                  << " preview_rgb=" << qRed(worst_preview) << ',' << qGreen(worst_preview)
                  << ',' << qBlue(worst_preview)
                  << " export_rgb=" << qRed(worst_export) << ',' << qGreen(worst_export)
                  << ',' << qBlue(worst_export) << '\n';
        for (std::size_t index = 0U; index < std::min(differing_pixels, mismatch_samples.size()); ++index) {
          const auto& sample = mismatch_samples[index];
          std::cerr << "highlight_mismatch crop_xy=" << sample[0] << ',' << sample[1]
                    << " preview_rgb=" << sample[2] << ',' << sample[3] << ',' << sample[4]
                    << " export_rgb=" << sample[5] << ',' << sample[6] << ',' << sample[7] << '\n';
        }
        return 1;
      }
      previews[levels][recovery] = left;
      std::cout << "raw_highlight_preview_export levels=" << levels << " recovery=" << recovery
                << " max_channel_error_codes=" << maximum_error
                << " mean_channel_error_codes=" << mean_error << std::endl;
    }
  }
  if (generated_fixture &&
      !require(!pixels_equal(previews[1][0], previews[1][1]) &&
                   !pixels_equal(previews[0][1], previews[1][1]),
               "the generated cropped RAW must exercise actual highlight recovery and nondefault levels")) return 1;
  return 0;
}
