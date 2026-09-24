#include <QtGui/qrgbafloat.h>

#include <QByteArrayView>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QFile>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSysInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "photo_editor/pipeline/color_pipeline.h"
#include "raw_image_decoder.h"
#include "raw_image_decoder_libraw.h"
#include "working_image.h"

namespace {

struct TiffEntry final {
  std::uint16_t tag = 0U;
  std::uint16_t type = 0U;
  std::uint32_t count = 0U;
  std::vector<std::uint8_t> data;
  std::uint32_t external_offset = 0U;
};

void append_u16(std::vector<std::uint8_t>* bytes, std::uint16_t value) {
  bytes->push_back(static_cast<std::uint8_t>(value & 0xffU));
  bytes->push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void append_u32(std::vector<std::uint8_t>* bytes, std::uint32_t value) {
  append_u16(bytes, static_cast<std::uint16_t>(value & 0xffffU));
  append_u16(bytes, static_cast<std::uint16_t>((value >> 16U) & 0xffffU));
}

[[nodiscard]] std::vector<std::uint8_t> short_values(
    std::initializer_list<std::uint16_t> values) {
  auto bytes = std::vector<std::uint8_t>{};
  for (const auto value : values) {
    append_u16(&bytes, value);
  }
  return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> long_values(
    std::initializer_list<std::uint32_t> values) {
  auto bytes = std::vector<std::uint8_t>{};
  for (const auto value : values) {
    append_u32(&bytes, value);
  }
  return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> rational_values(
    std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> values) {
  auto bytes = std::vector<std::uint8_t>{};
  for (const auto& [numerator, denominator] : values) {
    append_u32(&bytes, numerator);
    append_u32(&bytes, denominator);
  }
  return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> signed_rational_values(
    std::initializer_list<std::pair<std::int32_t, std::int32_t>> values) {
  auto bytes = std::vector<std::uint8_t>{};
  for (const auto& [numerator, denominator] : values) {
    append_u32(&bytes, static_cast<std::uint32_t>(numerator));
    append_u32(&bytes, static_cast<std::uint32_t>(denominator));
  }
  return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> ascii_value(const std::string& value) {
  auto bytes = std::vector<std::uint8_t>(value.begin(), value.end());
  bytes.push_back(0U);
  return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> synthetic_dng() {
  constexpr auto kWidth = std::uint32_t{128U};
  constexpr auto kHeight = std::uint32_t{128U};
  constexpr auto kPixelBytes = kWidth * kHeight * 2U;
  auto entries = std::vector<TiffEntry>{
      {256U, 4U, 1U, long_values({kWidth})},
      {257U, 4U, 1U, long_values({kHeight})},
      {258U, 3U, 1U, short_values({16U})},
      {259U, 3U, 1U, short_values({1U})},
      {262U, 3U, 1U, short_values({32803U})},
      {271U, 2U, 16U, ascii_value("OpenAI Fixtures")},
      {272U, 2U, 19U, ascii_value("Synthetic Bayer v1")},
      {273U, 4U, 1U, long_values({0U})},
      {274U, 3U, 1U, short_values({1U})},
      {277U, 3U, 1U, short_values({1U})},
      {278U, 4U, 1U, long_values({kHeight})},
      {279U, 4U, 1U, long_values({kPixelBytes})},
      {284U, 3U, 1U, short_values({1U})},
      {33421U, 3U, 2U, short_values({2U, 2U})},
      {33422U, 1U, 4U, {0U, 1U, 1U, 2U}},
      {50706U, 1U, 4U, {1U, 4U, 0U, 0U}},
      {50707U, 1U, 4U, {1U, 1U, 0U, 0U}},
      {50708U, 2U, 19U, ascii_value("Synthetic Bayer v1")},
      {50710U, 1U, 3U, {0U, 1U, 2U}},
      {50711U, 3U, 1U, short_values({1U})},
      {50713U, 3U, 2U, short_values({1U, 1U})},
      {50714U, 5U, 1U, rational_values({{64U, 1U}})},
      {50717U, 4U, 1U, long_values({4095U})},
      {50719U, 5U, 2U, rational_values({{0U, 1U}, {0U, 1U}})},
      {50720U, 5U, 2U, rational_values({{kWidth, 1U}, {kHeight, 1U}})},
      {50721U, 10U, 9U,
       signed_rational_values({
           {10000, 10000},
           {0, 10000},
           {0, 10000},
           {0, 10000},
           {10000, 10000},
           {0, 10000},
           {0, 10000},
           {0, 10000},
           {10000, 10000},
       })},
      {50728U, 5U, 3U, rational_values({{1U, 1U}, {1U, 1U}, {1U, 1U}})},
      {50778U, 3U, 1U, short_values({21U})},
      {50829U, 4U, 4U, long_values({0U, 0U, kHeight, kWidth})},
  };
  std::sort(entries.begin(), entries.end(),
            [](const TiffEntry& left, const TiffEntry& right) {
              return left.tag < right.tag;
            });

  const auto ifd_bytes =
      std::uint32_t{2U + static_cast<std::uint32_t>(entries.size()) * 12U + 4U};
  auto external_offset = std::uint32_t{8U} + ifd_bytes;
  for (auto& entry : entries) {
    if (entry.data.size() > 4U) {
      entry.external_offset = external_offset;
      external_offset += static_cast<std::uint32_t>(entry.data.size());
      external_offset = (external_offset + 1U) & ~std::uint32_t{1U};
    }
  }
  const auto pixel_offset = external_offset;
  const auto strip =
      std::find_if(entries.begin(), entries.end(),
                   [](const TiffEntry& entry) { return entry.tag == 273U; });
  strip->data = long_values({pixel_offset});

  auto bytes = std::vector<std::uint8_t>{'I', 'I'};
  append_u16(&bytes, 42U);
  append_u32(&bytes, 8U);
  append_u16(&bytes, static_cast<std::uint16_t>(entries.size()));
  for (const auto& entry : entries) {
    append_u16(&bytes, entry.tag);
    append_u16(&bytes, entry.type);
    append_u32(&bytes, entry.count);
    if (entry.data.size() <= 4U) {
      bytes.insert(bytes.end(), entry.data.begin(), entry.data.end());
      bytes.resize(bytes.size() + 4U - entry.data.size(), 0U);
    } else {
      append_u32(&bytes, entry.external_offset);
    }
  }
  append_u32(&bytes, 0U);
  for (const auto& entry : entries) {
    if (entry.data.size() <= 4U) {
      continue;
    }
    bytes.resize(entry.external_offset, 0U);
    bytes.insert(bytes.end(), entry.data.begin(), entry.data.end());
  }
  bytes.resize(pixel_offset, 0U);
  for (std::uint32_t y = 0U; y < kHeight; ++y) {
    for (std::uint32_t x = 0U; x < kWidth; ++x) {
      const auto red = (y % 2U) == 0U && (x % 2U) == 0U;
      const auto blue = (y % 2U) == 1U && (x % 2U) == 1U;
      const auto sample =
          static_cast<std::uint16_t>(red ? 3200U : (blue ? 1200U : 2200U));
      append_u16(&bytes, sample);
    }
  }
  return bytes;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

[[nodiscard]] QByteArray sha256_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash digest(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto block = file.read(1024 * 1024);
    if (block.isEmpty() && !file.atEnd()) {
      return {};
    }
    digest.addData(block);
  }
  return digest.result().toHex();
}

[[nodiscard]] QByteArray pixel_sha256(const QImage& source) {
  const auto image = source.convertToFormat(QImage::Format_RGBA32FPx4);
  if (image.isNull()) {
    return {};
  }
  QCryptographicHash digest(QCryptographicHash::Sha256);
  const auto row_bytes = static_cast<qsizetype>(image.width()) *
                         static_cast<qsizetype>(sizeof(QRgbaFloat32));
  for (int y = 0; y < image.height(); ++y) {
    digest.addData(QByteArrayView(
        reinterpret_cast<const char*>(image.constScanLine(y)), row_bytes));
  }
  return digest.result().toHex();
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--cache-benchmark") {
    using namespace photo_editor::desktop;
    const auto path = QString::fromLocal8Bit(argv[2]);
    const RawDecodeSettings settings;
    // 1024/1600 share half-size processing on the real RAW fixture; 0 is
    // full-size. Compare each warm request with its independently cold oracle.
    std::array<QImage, 3> reference;
    constexpr std::array<int, 3> edges{1024, 1600, 0};
    std::array<qint64, 3> cold_ms{};
    for (std::size_t i = 0; i < edges.size(); ++i) {
      clear_libraw_decode_cache();
      QElapsedTimer timer; timer.start();
      if (!decode_libraw_camera_linear_image(path, &reference[i], edges[i], settings, nullptr))
        return fail("Cold RAW cache benchmark decode failed");
      cold_ms[i] = timer.elapsed();
    }
    for (const auto i : {std::size_t{0}, std::size_t{1}, std::size_t{2}}) {
      QImage actual;
      const auto before = libraw_decode_cache_stats();
      QElapsedTimer timer; timer.start();
      if (!decode_libraw_camera_linear_image(path, &actual, edges[i], settings, nullptr) ||
          actual != reference[i]) return fail("Warm RAW pixels differ from cold decode");
      const auto elapsed = timer.elapsed();
      const auto after = libraw_decode_cache_stats();
      std::cout << "raw_edge=" << edges[i] << " cold_ms=" << cold_ms[i]
                << " reuse_ms=" << elapsed << " hit=" << (after.hits > before.hits)
                << " bytes=" << after.bytes << std::endl;
      if (i > 0 && after.hits == before.hits) return fail("Compatible RAW decode was not reused");
      if (after.bytes > 192 * 1024 * 1024 || after.entries > 2) return fail("RAW cache exceeded budget");
    }
    return EXIT_SUCCESS;
  }
#ifdef NDEBUG
  constexpr auto kBuildMode = "release";
#else
  constexpr auto kBuildMode = "debug";
#endif
  auto report_path = QString{};
  if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--report")) {
    report_path = QString::fromLocal8Bit(argv[2]);
  } else if (argc != 1) {
    return fail("usage: raw_camera_linear_decoder_test [--report report.json]");
  }
  if (!report_path.isEmpty() &&
      (QFileInfo::exists(report_path) ||
       std::string_view(kBuildMode) != std::string_view("release"))) {
    return fail(
        "RAW runtime proof requires a new report path and a Release executable");
  }
  using photo_editor::desktop::RawDecodeSettings;
  const auto preview_demosaic = RawDecodeSettings{
      .demosaic_quality = QStringLiteral("Preview"),
  };
  const auto balanced_demosaic = RawDecodeSettings{
      .demosaic_quality = QStringLiteral("Balanced"),
  };
  const auto full_demosaic = RawDecodeSettings{
      .demosaic_quality = QStringLiteral("Full"),
  };
  if (!photo_editor::desktop::raw_decode_uses_draft_demosaic(
          preview_demosaic,
          true) ||
      photo_editor::desktop::raw_decode_uses_draft_demosaic(
          balanced_demosaic,
          true) ||
      photo_editor::desktop::raw_decode_uses_draft_demosaic(
          full_demosaic,
          true)) {
    return fail(
        "bounded Balanced RAW previews must retain final color/demosaic "
        "semantics; only explicit Preview quality may select draft mode");
  }
  const auto capability =
      photo_editor::desktop::raw_camera_linear_decoder_capability();
  if (!photo_editor::desktop::raw_decode_requests_camera_linear_backend(
          RawDecodeSettings{.input_color_profile =
                                QStringLiteral("Camera DCP: fixture")}) ||
      !photo_editor::desktop::raw_decode_requests_camera_linear_backend(
          RawDecodeSettings{.input_color_profile =
                                QStringLiteral("Camera ICC: fixture")}) ||
      !photo_editor::desktop::raw_decode_requests_camera_linear_backend(
          RawDecodeSettings{.input_color_profile =
                                QStringLiteral("Camera Native")}) ||
      !photo_editor::desktop::raw_decode_requests_camera_linear_backend(
          RawDecodeSettings{.input_color_profile =
                                QStringLiteral("Embedded")}) ||
      photo_editor::desktop::raw_decode_requests_camera_linear_backend(
          RawDecodeSettings{
              .input_transform_enabled = false,
              .input_color_profile = QStringLiteral("Embedded")})) {
    return fail(
        "editable RAW must use camera-linear decode unless its input transform "
        "is explicitly disabled");
  }
  const auto decode_cache_baseline = RawDecodeSettings{
      .input_color_profile = QStringLiteral("Embedded"),
  };
  auto engine_profile_change = decode_cache_baseline;
  engine_profile_change.input_color_profile =
      QStringLiteral("Camera DCP: fixture");
  auto white_balance_change = decode_cache_baseline;
  white_balance_change.white_balance_override_enabled = true;
  white_balance_change.white_balance_temperature = 6100.0;
  auto demosaic_change = decode_cache_baseline;
  demosaic_change.demosaic_quality = QStringLiteral("Preview");
  if (photo_editor::desktop::raw_decode_settings_cache_key(
          decode_cache_baseline) !=
          photo_editor::desktop::raw_decode_settings_cache_key(
              engine_profile_change) ||
      photo_editor::desktop::raw_decode_settings_cache_key(
          decode_cache_baseline) ==
          photo_editor::desktop::raw_decode_settings_cache_key(
              white_balance_change) ||
      photo_editor::desktop::raw_decode_settings_cache_key(
          decode_cache_baseline) ==
          photo_editor::desktop::raw_decode_settings_cache_key(
              demosaic_change)) {
    return fail(
        "RAW decode cache identity must ignore engine-only profile selection "
        "and include pixel- or metadata-affecting RAW controls");
  }
  if (!capability.available) {
    return capability.failure_reason ==
                   QStringLiteral("libraw-camera-linear-not-compiled")
               ? 77
               : fail("unavailable LibRaw capability should name its failure");
  }
  if (capability.backend != QStringLiteral("LibRaw") ||
      capability.compiled_version.isEmpty() ||
      capability.runtime_version.isEmpty() ||
      capability.validated_version.isEmpty() ||
      !capability.pixel_contract.contains(QStringLiteral("no-camera-matrix")) ||
      !capability.pixel_contract.contains(QStringLiteral("unbalanced")) ||
      capability.applied_controls.contains(QStringLiteral("white-balance")) ||
      !capability.deferred_controls.contains(QStringLiteral("white-balance")) ||
      !capability.deferred_controls.contains(
          QStringLiteral("camera-characterization")) ||
      capability.maximum_raw_memory_bytes != 1536ULL * 1024ULL * 1024ULL) {
    return fail("LibRaw capability must expose complete bounded provenance");
  }

  QTemporaryDir temporary;
  if (!temporary.isValid()) {
    return fail("temporary DNG directory should be available");
  }
  const auto dng_path =
      temporary.filePath(QStringLiteral("synthetic-bayer.dng"));
  QFile dng(dng_path);
  const auto payload = synthetic_dng();
  if (!dng.open(QIODevice::WriteOnly) ||
      dng.write(reinterpret_cast<const char*>(payload.data()),
                static_cast<qint64>(payload.size())) !=
          static_cast<qint64>(payload.size())) {
    return fail("synthetic DNG should be written atomically");
  }
  dng.close();
  if (!photo_editor::desktop::libraw_camera_linear_image_supported(dng_path) ||
      photo_editor::desktop::libraw_camera_linear_image_size(dng_path) !=
          QSize(128, 128)) {
    return fail(
        "LibRaw should identify the generated DNG and its active dimensions");
  }

  auto decoded = QImage{};
  auto metadata = photo_editor::pipeline::DecodedImageMetadata{};
  const auto settings = RawDecodeSettings{
      .input_color_profile = QStringLiteral("Camera DCP: fixture"),
      .demosaic_quality = QStringLiteral("Full"),
      .highlight_recovery_enabled = false,
  };
  if (!photo_editor::desktop::decode_libraw_camera_linear_image(
          dng_path, &decoded, 0, settings, &metadata) ||
      decoded.size() != QSize(128, 128) ||
      decoded.format() != QImage::Format_RGBA32FPx4 ||
      decoded.colorSpace().isValid() ||
      !photo_editor::pipeline::decoded_image_color_boundary_valid(metadata) ||
      !photo_editor::pipeline::decoded_image_requires_engine_input_transform(
          metadata) ||
      metadata.camera_transform_applied || !metadata.demosaic_applied ||
      !metadata.black_subtraction_applied ||
      !metadata.white_normalization_applied ||
      metadata.white_balance_applied ||
      metadata.highlight_reconstruction_applied ||
      metadata.lens_correction_applied ||
      metadata.baseline_exposure_applied ||
      std::abs(metadata.baseline_exposure) > 1.0e-12 ||
      !metadata.decoder_camera_to_xyz_valid ||
      metadata.decoder_backend.find("LibRaw camera-linear") != 0U ||
      metadata.camera_profile_identity.find(
          "libraw-camera-to-xyz-D65:") != 0U ||
      metadata.camera_make != "OpenAI Fixtures" ||
      metadata.camera_model != "Synthetic Bayer v1") {
    std::cerr << "boundary valid="
              << photo_editor::pipeline::decoded_image_color_boundary_valid(
                     metadata)
              << " requires_input="
              << photo_editor::pipeline::
                     decoded_image_requires_engine_input_transform(metadata)
              << " matrix_valid="
              << metadata.decoder_camera_to_xyz_valid
              << " matrix=";
    for (const auto value : metadata.decoder_camera_to_xyz) {
      std::cerr << value << ",";
    }
    std::cerr << " backend=" << metadata.decoder_backend
              << " make=" << metadata.camera_make
              << " model=" << metadata.camera_model << "\n";
    return fail(
        "generated DNG must decode to an explicit engine-owned "
        "camera-linear boundary");
  }
  auto draft_decoded = QImage{};
  auto draft_metadata =
      photo_editor::pipeline::DecodedImageMetadata{};
  auto draft_settings = settings;
  draft_settings.demosaic_quality = QStringLiteral("Preview");
  if (!photo_editor::desktop::decode_libraw_camera_linear_image(
          dng_path,
          &draft_decoded,
          32,
          draft_settings,
          &draft_metadata) ||
      draft_decoded.isNull() ||
      std::max(draft_decoded.width(), draft_decoded.height()) > 32 ||
      draft_decoded.format() != QImage::Format_RGBA32FPx4 ||
      draft_metadata.pixel_encoding != metadata.pixel_encoding ||
      draft_metadata.camera_make != metadata.camera_make ||
      draft_metadata.camera_model != metadata.camera_model ||
      draft_metadata.camera_profile_identity !=
          metadata.camera_profile_identity ||
      draft_metadata.decoder_camera_to_xyz_valid !=
          metadata.decoder_camera_to_xyz_valid ||
      draft_metadata.decoder_camera_to_xyz !=
          metadata.decoder_camera_to_xyz ||
      draft_metadata.decoder_camera_to_xyz_reference_white !=
          metadata.decoder_camera_to_xyz_reference_white ||
      draft_metadata.white_balance_applied !=
          metadata.white_balance_applied ||
      draft_metadata.camera_transform_applied !=
          metadata.camera_transform_applied ||
      draft_metadata.decoder_backend.find("LibRaw camera-linear") != 0U) {
    return fail(
        "bounded LibRaw draft decode must reduce resolution without changing camera/color ownership");
  }
  auto characterized = QImage{};
  auto characterized_metadata = photo_editor::pipeline::DecodedImageMetadata{};
  const auto embedded_settings = RawDecodeSettings{
      .input_color_profile = QStringLiteral("Embedded"),
      .demosaic_quality = QStringLiteral("Full"),
      .highlight_recovery_enabled = false,
  };
  if (!photo_editor::desktop::decode_libraw_scene_linear_image(
          dng_path, &characterized, 0, embedded_settings,
          &characterized_metadata) ||
      characterized.size() != decoded.size() ||
      characterized.colorSpace() != QColorSpace(QColorSpace::SRgbLinear) ||
      !photo_editor::pipeline::decoded_image_color_boundary_valid(
          characterized_metadata) ||
      photo_editor::pipeline::decoded_image_requires_engine_input_transform(
          characterized_metadata) ||
      !characterized_metadata.camera_transform_applied ||
      characterized_metadata.pixel_encoding !=
          photo_editor::pipeline::DecodedPixelEncoding::SceneLinearRgb ||
      characterized_metadata.decoder_backend.find(
          "LibRaw characterized linear-sRGB") != 0U) {
    return fail(
        "ordinary cross-platform RAW decode must expose a tagged, "
        "decoder-characterized linear-sRGB fallback");
  }
  auto cancelled_generation =
      std::atomic<int>{2};
  auto cancelled_decode =
      QImage(4, 4, QImage::Format_ARGB32);
  cancelled_decode.fill(
      QColor(17, 31, 47));
  const auto cancelled_decode_before =
      cancelled_decode;
  auto cancelled_metadata =
      photo_editor::pipeline::
          DecodedImageMetadata{};
  if (photo_editor::desktop::
          decode_raw_linear_image(
              dng_path,
              &cancelled_decode,
              0,
              embedded_settings,
              &cancelled_metadata,
              photo_editor::pipeline::
                  CancellationToken{
                      &cancelled_generation,
                      1,
                  }) ||
      cancelled_decode !=
          cancelled_decode_before ||
      cancelled_metadata.valid) {
    return fail(
        "a superseded RAW decode must stop before publishing pixels or metadata");
  }
  auto cancelled_cleanup =
      characterized;
  const auto cancelled_cleanup_before =
      cancelled_cleanup;
  static_cast<void>(
      photo_editor::desktop::
          apply_raw_hot_dead_pixel_cleanup(
              &cancelled_cleanup,
              1.0,
              photo_editor::pipeline::
                  CancellationToken{
                      &cancelled_generation,
                      1,
                  }));
  static_cast<void>(
      photo_editor::desktop::
          apply_raw_false_color_suppression(
              &cancelled_cleanup,
              1.0,
              photo_editor::pipeline::
                  CancellationToken{
                      &cancelled_generation,
                      1,
                  }));
  if (cancelled_cleanup !=
      cancelled_cleanup_before) {
    return fail(
        "superseded RAW cleanup stages must not publish partially repaired pixels");
  }

  auto shipping_default = QImage{};
  auto shipping_default_metadata =
      photo_editor::pipeline::DecodedImageMetadata{};
  if (!photo_editor::desktop::decode_raw_linear_image(
          dng_path,
          &shipping_default,
          0,
          embedded_settings,
          &shipping_default_metadata) ||
      shipping_default.size() != decoded.size() ||
      shipping_default.colorSpace().isValid() ||
      !photo_editor::pipeline::decoded_image_color_boundary_valid(
          shipping_default_metadata) ||
      shipping_default_metadata.pixel_encoding !=
          photo_editor::pipeline::DecodedPixelEncoding::CameraLinearRgb ||
      shipping_default_metadata.camera_transform_applied ||
      !shipping_default_metadata.decoder_camera_to_xyz_valid ||
      !photo_editor::pipeline::decoded_image_requires_engine_input_transform(
          shipping_default_metadata) ||
      shipping_default_metadata.decoder_backend.find(
          "LibRaw camera-linear") != 0U) {
    return fail(
        "shipping RAW decode must preserve an engine-characterized "
        "camera-linear image through the cross-platform LibRaw route");
  }
  const auto shipping_float =
      shipping_default.convertToFormat(QImage::Format_RGBA32FPx4);
  auto shipping_rgb_maximum = 0.0F;
  for (int y = 0; y < shipping_float.height(); ++y) {
    const auto* scanline = reinterpret_cast<const QRgbaFloat32*>(
        shipping_float.constScanLine(y));
    for (int x = 0; x < shipping_float.width(); ++x) {
      const auto& pixel = scanline[x];
      const auto red = static_cast<float>(pixel.r);
      const auto green = static_cast<float>(pixel.g);
      const auto blue = static_cast<float>(pixel.b);
      const auto alpha = static_cast<float>(pixel.a);
      if (!std::isfinite(red) || !std::isfinite(green) ||
          !std::isfinite(blue) || !std::isfinite(alpha) ||
          std::abs(alpha - 1.0F) > 1.0e-6F) {
        return fail(
            "shipping RAW decode must contain finite RGB and opaque alpha");
      }
      shipping_rgb_maximum =
          std::max({shipping_rgb_maximum, red, green, blue});
    }
  }
  if (shipping_rgb_maximum <= 1.0e-6F) {
    return fail(
        "shipping RAW decode must reject an untouched or empty render buffer");
  }
  auto routed = QImage{};
  auto routed_metadata = photo_editor::pipeline::DecodedImageMetadata{};
  if (!photo_editor::desktop::raw_image_supported(dng_path) ||
      !photo_editor::desktop::decode_raw_linear_image(
          dng_path, &routed, 0, settings, &routed_metadata) ||
      routed != decoded ||
      routed_metadata.decoder_backend != metadata.decoder_backend ||
      !photo_editor::desktop::read_raw_decoded_metadata(
          dng_path, &routed_metadata, settings) ||
      !photo_editor::pipeline::decoded_image_requires_engine_input_transform(
          routed_metadata) ||
      !routed_metadata.decoder_camera_to_xyz_valid ||
      routed_metadata.decoder_camera_to_xyz_reference_white != "D65") {
    return fail(
        "public RAW routing must select LibRaw for explicit "
        "engine-owned camera profiles");
  }
  const auto embedded_input_profile =
      photo_editor::pipeline::input_color_profile_from_name("Embedded");
  const auto routed_profile =
      photo_editor::pipeline::resolve_camera_profile(
          routed_metadata,
          embedded_input_profile);
  const auto routed_input_recipe =
      photo_editor::pipeline::resolve_input_transform_recipe(
          embedded_input_profile,
          routed_profile,
          routed_metadata);
  if (!routed_profile.valid ||
      routed_input_recipe.decoder_white_balance_applied) {
    return fail(
        "camera-linear metadata must resolve executable engine-owned characterization and white balance");
  }
  const auto* center =
      reinterpret_cast<const QRgbaFloat32*>(decoded.constScanLine(64));
  const auto center_pixel = center[64];
  if (!std::isfinite(static_cast<float>(center_pixel.r)) ||
      !std::isfinite(static_cast<float>(center_pixel.g)) ||
      !std::isfinite(static_cast<float>(center_pixel.b)) ||
      static_cast<float>(center_pixel.r) <=
          static_cast<float>(center_pixel.g) ||
      static_cast<float>(center_pixel.g) <=
          static_cast<float>(center_pixel.b) ||
      static_cast<float>(center_pixel.a) != 1.0F) {
    return fail(
        "camera-linear DNG samples should preserve the synthetic RGB "
        "ordering and opaque alpha");
  }

  auto scaled = QImage{};
  if (!photo_editor::desktop::decode_libraw_camera_linear_image(
          dng_path, &scaled, 64, settings, nullptr) ||
      std::max(scaled.width(), scaled.height()) != 64) {
    return fail(
        "camera-linear decode should honor the requested preview bound");
  }

  const auto cache_before_repeat = photo_editor::desktop::libraw_decode_cache_stats();
  QImage repeated;
  if (!photo_editor::desktop::decode_libraw_camera_linear_image(
          dng_path, &repeated, 64, settings, nullptr) || repeated != scaled ||
      photo_editor::desktop::libraw_decode_cache_stats().hits <= cache_before_repeat.hits)
    return fail("RAW resize cache must preserve exact pixels and reuse matching modes");
  auto different_decode = settings;
  different_decode.exposure += 0.5;
  const auto cache_before_change = photo_editor::desktop::libraw_decode_cache_stats();
  if (!photo_editor::desktop::decode_libraw_camera_linear_image(
          dng_path, &repeated, 64, different_decode, nullptr) ||
      photo_editor::desktop::libraw_decode_cache_stats().misses <= cache_before_change.misses)
    return fail("RAW settings changes must invalidate packed samples");

  using ConcurrentResult =
      std::pair<QImage, photo_editor::pipeline::DecodedImageMetadata>;
  auto concurrent_decodes = std::array<std::future<ConcurrentResult>, 4>{};
  for (auto& future : concurrent_decodes) {
    future = std::async(std::launch::async, [dng_path, settings]() {
      auto concurrent_image = QImage{};
      auto concurrent_metadata = photo_editor::pipeline::DecodedImageMetadata{};
      if (!photo_editor::desktop::decode_libraw_camera_linear_image(
              dng_path, &concurrent_image, 0, settings, &concurrent_metadata)) {
        return ConcurrentResult{};
      }
      return ConcurrentResult{
          std::move(concurrent_image),
          std::move(concurrent_metadata),
      };
    });
  }
  for (auto& future : concurrent_decodes) {
    auto [concurrent_image, concurrent_metadata] = future.get();
    if (concurrent_image != decoded ||
        concurrent_metadata.decoder_backend != metadata.decoder_backend ||
        concurrent_metadata.camera_profile_identity !=
            metadata.camera_profile_identity) {
      return fail(
          "independent LibRaw handles must decode concurrently and "
          "deterministically");
    }
  }

  auto overridden_metadata = photo_editor::pipeline::DecodedImageMetadata{};
  auto overridden = QImage{};
  auto override_settings = settings;
  override_settings.white_balance_override_enabled = true;
  override_settings.white_balance_temperature = 6500.0;
  override_settings.white_balance_tint = 25.0;
  if (!photo_editor::desktop::decode_libraw_camera_linear_image(
          dng_path, &overridden, 0, override_settings, &overridden_metadata) ||
      std::abs(overridden_metadata.as_shot_temperature - 6500.0) > 1.0e-9 ||
      std::abs(overridden_metadata.as_shot_tint - 25.0) > 1.0e-9) {
    return fail(
        "camera-linear white-balance overrides must be explicit in metadata");
  }
  const auto* overridden_center =
      reinterpret_cast<const QRgbaFloat32*>(overridden.constScanLine(64));
  if (std::max({
          std::abs(static_cast<float>(overridden_center[64].r) -
                   static_cast<float>(center_pixel.r)),
          std::abs(static_cast<float>(overridden_center[64].g) -
                   static_cast<float>(center_pixel.g)),
          std::abs(static_cast<float>(overridden_center[64].b) -
                   static_cast<float>(center_pixel.b)),
      }) > 1.0e-7F) {
    return fail(
        "engine-owned white balance must not mutate decoded camera-linear samples");
  }

  auto tagged_device_rgb = QImage(1, 1, QImage::Format_RGBA32FPx4);
  tagged_device_rgb.setColorSpace(QColorSpace(QColorSpace::SRgb));
  auto* tagged_pixel =
      reinterpret_cast<QRgbaFloat32*>(tagged_device_rgb.scanLine(0));
  tagged_pixel[0] = QRgbaFloat32{0.73F, 0.42F, 0.18F, 0.5F};
  const auto preserved = photo_editor::desktop::working_image_from_qimage(
      tagged_device_rgb, QStringLiteral("Linear ProPhoto RGB"), nullptr, true);
  const auto converted = photo_editor::desktop::working_image_from_qimage(
      tagged_device_rgb, QStringLiteral("Linear ProPhoto RGB"));
  if (preserved.isNull() || converted.isNull() ||
      std::abs(preserved.pixels[0].red - 0.73F) > 1.0e-7F ||
      std::abs(preserved.pixels[0].green - 0.42F) > 1.0e-7F ||
      std::abs(preserved.pixels[0].blue - 0.18F) > 1.0e-7F ||
      std::abs(preserved.pixels[0].alpha - 0.5F) > 1.0e-7F ||
      std::abs(converted.pixels[0].red - preserved.pixels[0].red) < 1.0e-3F) {
    return fail(
        "engine-owned camera RGB must bypass document color-space "
        "conversion exactly");
  }

  auto sentinel = QImage(2, 2, QImage::Format_ARGB32);
  sentinel.fill(Qt::red);
  auto sentinel_metadata = metadata;
  if (photo_editor::desktop::decode_libraw_camera_linear_image(
          temporary.filePath(QStringLiteral("missing.dng")), &sentinel, 0,
          settings, &sentinel_metadata) ||
      sentinel.size() != QSize(2, 2) ||
      sentinel.pixelColor(0, 0) != QColor(Qt::red) ||
      sentinel_metadata.decoder_backend != metadata.decoder_backend) {
    return fail(
        "failed camera-linear decoding must leave image and metadata "
        "untouched");
  }

  if (!report_path.isEmpty()) {
    const auto executable_path = QFileInfo(QString::fromLocal8Bit(argv[0]))
                                     .absoluteFilePath();
    const auto executable_sha256 = sha256_file(executable_path);
    const auto camera_linear_sha256 = pixel_sha256(decoded);
    const auto payload_bytes = QByteArray(
        reinterpret_cast<const char*>(payload.data()),
        static_cast<qsizetype>(payload.size()));
    const auto dng_sha256 = QCryptographicHash::hash(
        payload_bytes, QCryptographicHash::Sha256).toHex();
    if (executable_sha256.size() != 64 || camera_linear_sha256.size() != 64 ||
        dng_sha256.size() != 64) {
      return fail("RAW runtime proof could not hash its immutable inputs");
    }
    const auto report = QJsonObject{
        {QStringLiteral("schema"),
         QStringLiteral("photo-editor-windows-raw-runtime-proof:v1")},
        {QStringLiteral("tool_version"),
         QStringLiteral("photo-editor-raw-camera-linear-decoder-test:v1")},
        {QStringLiteral("hardware"),
         QJsonObject{
             {QStringLiteral("kernel_type"), QSysInfo::kernelType()},
             {QStringLiteral("kernel_version"), QSysInfo::kernelVersion()},
             {QStringLiteral("product_type"), QSysInfo::productType()},
             {QStringLiteral("product_version"), QSysInfo::productVersion()},
             {QStringLiteral("cpu_architecture"),
              QSysInfo::currentCpuArchitecture()},
             {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
             {QStringLiteral("build_mode"), QString::fromLatin1(kBuildMode)},
         }},
        {QStringLiteral("executable_sha256"),
         QString::fromLatin1(executable_sha256)},
        {QStringLiteral("capability"),
         QJsonObject{
             {QStringLiteral("backend"), capability.backend},
             {QStringLiteral("compiled_version"), capability.compiled_version},
             {QStringLiteral("runtime_version"), capability.runtime_version},
             {QStringLiteral("validated_version"), capability.validated_version},
             {QStringLiteral("pixel_contract"), capability.pixel_contract},
             {QStringLiteral("maximum_raw_memory_bytes"),
              static_cast<qint64>(capability.maximum_raw_memory_bytes)},
         }},
        {QStringLiteral("synthetic_dng"),
         QJsonObject{
             {QStringLiteral("sha256"), QString::fromLatin1(dng_sha256)},
             {QStringLiteral("width"), decoded.width()},
             {QStringLiteral("height"), decoded.height()},
             {QStringLiteral("camera_linear_backend"),
              QString::fromStdString(metadata.decoder_backend)},
             {QStringLiteral("shipping_backend"),
              QString::fromStdString(shipping_default_metadata.decoder_backend)},
             {QStringLiteral("camera_linear_pixel_sha256"),
              QString::fromLatin1(camera_linear_sha256)},
             {QStringLiteral("shipping_rgb_maximum"),
              static_cast<double>(shipping_rgb_maximum)},
             {QStringLiteral("concurrent_decode_count"), 4},
         }},
        {QStringLiteral("contracts"),
         QJsonObject{
             {QStringLiteral("atomic_failure"), true},
             {QStringLiteral("bounded_preview"), true},
             {QStringLiteral("camera_profile_routing"), true},
             {QStringLiteral("camera_rgb_preservation"), true},
             {QStringLiteral("concurrent_determinism"), true},
             {QStringLiteral("explicit_transform_ownership"), true},
             {QStringLiteral("opaque_finite_shipping_pixels"), true},
             {QStringLiteral("white_balance_override"), true},
         }},
        {QStringLiteral("result"), QStringLiteral("passed")},
    };
    QSaveFile report_file(report_path);
    if (!report_file.open(QIODevice::WriteOnly) ||
        report_file.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) <
            0 ||
        !report_file.commit()) {
      return fail("unable to publish RAW runtime proof report");
    }
  }

  return EXIT_SUCCESS;
}
