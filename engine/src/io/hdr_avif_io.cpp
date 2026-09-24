#include "photo_editor/io/hdr_avif_io.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <string>

#if defined(PHOTO_EDITOR_HAS_LIBHEIF_HDR) && PHOTO_EDITOR_HAS_LIBHEIF_HDR
#include <libheif/heif.h>
#endif

namespace photo_editor::io {

namespace {

#if defined(PHOTO_EDITOR_HAS_LIBHEIF_HDR) && PHOTO_EDITOR_HAS_LIBHEIF_HDR
constexpr double kHlgA = 0.17883277;
constexpr double kHlgB = 1.0 - 4.0 * kHlgA;
const double kHlgC = 0.5 - kHlgA * std::log(4.0 * kHlgA);

[[nodiscard]] double encode_pq(
    double reference_relative,
    double reference_white_nits) {
  constexpr double m1 = 2610.0 / 16384.0;
  constexpr double m2 = 2523.0 / 32.0;
  constexpr double c1 = 3424.0 / 4096.0;
  constexpr double c2 = 2413.0 / 128.0;
  constexpr double c3 = 2392.0 / 128.0;
  const auto absolute = std::clamp(
      reference_relative * reference_white_nits / 10000.0,
      0.0,
      1.0);
  const auto powered = std::pow(absolute, m1);
  return std::pow(
      (c1 + c2 * powered) / (1.0 + c3 * powered),
      m2);
}

[[nodiscard]] double encode_hlg(
    double reference_relative,
    double peak_luminance_nits,
    double reference_white_nits) {
  const auto target_relative = std::clamp(
      reference_relative * reference_white_nits / peak_luminance_nits,
      0.0,
      1.0);
  return target_relative <= (1.0 / 12.0)
             ? std::sqrt(3.0 * target_relative)
             : kHlgA * std::log(12.0 * target_relative - kHlgB) + kHlgC;
}

[[nodiscard]] double encode_channel(
    double value,
    const HdrAvifWriteOptions& options) {
  return options.transfer == HdrAvifTransfer::Pq
             ? encode_pq(value, options.reference_white_nits)
             : encode_hlg(
                   value,
                   options.peak_luminance_nits,
                   options.reference_white_nits);
}

void write_be_u16(std::uint8_t* destination, std::uint16_t value) {
  destination[0] = static_cast<std::uint8_t>(value >> 8U);
  destination[1] = static_cast<std::uint8_t>(value & 0xffU);
}

[[nodiscard]] std::uint16_t read_be_u16(const std::uint8_t* source) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(source[0]) << 8U) |
      static_cast<std::uint16_t>(source[1]));
}

[[nodiscard]] bool heif_ok(const heif_error& error) {
  return error.code == heif_error_Ok;
}

[[nodiscard]] std::string heif_failure(
    std::string_view operation,
    const heif_error& error) {
  auto result = std::string(operation);
  result += ':';
  result += error.message != nullptr ? error.message : "unknown_libheif_error";
  return result;
}

struct ContextDeleter final {
  void operator()(heif_context* value) const {
    if (value != nullptr) {
      heif_context_free(value);
    }
  }
};

struct ImageDeleter final {
  void operator()(heif_image* value) const {
    if (value != nullptr) {
      heif_image_release(value);
    }
  }
};

struct HandleDeleter final {
  void operator()(const heif_image_handle* value) const {
    if (value != nullptr) {
      heif_image_handle_release(value);
    }
  }
};

struct EncoderDeleter final {
  void operator()(heif_encoder* value) const {
    if (value != nullptr) {
      heif_encoder_release(value);
    }
  }
};

struct NclxDeleter final {
  void operator()(heif_color_profile_nclx* value) const {
    if (value != nullptr) {
      heif_nclx_color_profile_free(value);
    }
  }
};

struct EncodingOptionsDeleter final {
  void operator()(heif_encoding_options* value) const {
    if (value != nullptr) {
      heif_encoding_options_free(value);
    }
  }
};

using ContextPtr = std::unique_ptr<heif_context, ContextDeleter>;
using ImagePtr = std::unique_ptr<heif_image, ImageDeleter>;
using HandlePtr = std::unique_ptr<const heif_image_handle, HandleDeleter>;
using EncoderPtr = std::unique_ptr<heif_encoder, EncoderDeleter>;
using NclxPtr = std::unique_ptr<heif_color_profile_nclx, NclxDeleter>;
using EncodingOptionsPtr =
    std::unique_ptr<heif_encoding_options, EncodingOptionsDeleter>;

[[nodiscard]] std::uint16_t chromaticity_code(double value) {
  return static_cast<std::uint16_t>(
      std::lround(std::clamp(value, 0.0, 1.0) * 50000.0));
}

[[nodiscard]] std::uint32_t luminance_code(double value) {
  return static_cast<std::uint32_t>(std::llround(std::clamp(
      value * 10000.0,
      0.0,
      static_cast<double>(std::numeric_limits<std::uint32_t>::max()))));
}
#endif

}  // namespace

bool hdr_avif_backend_available() {
#if defined(PHOTO_EDITOR_HAS_LIBHEIF_HDR) && PHOTO_EDITOR_HAS_LIBHEIF_HDR
  return heif_have_encoder_for_format(heif_compression_AV1) != 0 &&
         heif_have_decoder_for_format(heif_compression_AV1) != 0;
#else
  return false;
#endif
}

std::string_view hdr_avif_backend_version() {
#if defined(PHOTO_EDITOR_HAS_LIBHEIF_HDR) && PHOTO_EDITOR_HAS_LIBHEIF_HDR
  return PHOTO_EDITOR_LIBHEIF_COMPILED_VERSION;
#else
  return "unavailable";
#endif
}

bool write_hdr_avif_rec2020(
    std::string_view file_path,
    std::span<const float> linear_reference_relative_rgba,
    const HdrAvifWriteOptions& options,
    std::string* failure_reason) {
  const auto fail = [&](std::string reason) {
    if (failure_reason != nullptr) {
      *failure_reason = std::move(reason);
    }
    return false;
  };
  const auto canceled = [&]() {
    return options.cancellation_requested &&
           options.cancellation_requested();
  };
#if defined(PHOTO_EDITOR_HAS_LIBHEIF_HDR) && PHOTO_EDITOR_HAS_LIBHEIF_HDR
  if (canceled()) {
    return fail("hdr_avif_write_canceled");
  }
  if (!hdr_avif_backend_available()) {
    return fail("hdr_avif_backend_unavailable");
  }
  if (file_path.empty() || options.width <= 0 || options.height <= 0 ||
      options.row_stride_floats <
          static_cast<std::size_t>(options.width) * 4U ||
      options.bit_depth != 10 || options.quality < 0 || options.quality > 100 ||
      !std::isfinite(options.peak_luminance_nits) ||
      !std::isfinite(options.black_luminance_nits) ||
      !std::isfinite(options.reference_white_nits) ||
      options.peak_luminance_nits <= options.reference_white_nits ||
      options.reference_white_nits <= 0.0 ||
      options.black_luminance_nits < 0.0 ||
      options.max_content_light_level <= 0 ||
      options.max_content_light_level > 65535 ||
      options.max_frame_average_light_level <= 0 ||
      options.max_frame_average_light_level > options.max_content_light_level) {
    return fail("invalid_hdr_avif_write_options");
  }
  const auto required = options.row_stride_floats *
                        static_cast<std::size_t>(options.height);
  if (linear_reference_relative_rgba.size() < required) {
    return fail("hdr_avif_pixel_buffer_too_small");
  }
  for (int y = 0; y < options.height; ++y) {
    if (canceled()) {
      return fail("hdr_avif_write_canceled");
    }
    const auto row = static_cast<std::size_t>(y) * options.row_stride_floats;
    for (int x = 0; x < options.width; ++x) {
      const auto pixel = row + static_cast<std::size_t>(x) * 4U;
      for (std::size_t channel = 0U; channel < 4U; ++channel) {
        if (!std::isfinite(linear_reference_relative_rgba[pixel + channel])) {
          return fail("hdr_avif_pixel_buffer_contains_non_finite_values");
        }
      }
    }
  }

  try {
    ContextPtr context(heif_context_alloc());
    if (!context) {
      return fail("hdr_avif_context_allocation_failed");
    }
    heif_image* raw_image = nullptr;
    auto error = heif_image_create(
        options.width,
        options.height,
        heif_colorspace_RGB,
        heif_chroma_interleaved_RRGGBB_BE,
        &raw_image);
    if (!heif_ok(error)) {
      return fail(heif_failure("hdr_avif_image_create_failed", error));
    }
    ImagePtr image(raw_image);
    error = heif_image_add_plane(
        image.get(),
        heif_channel_interleaved,
        options.width,
        options.height,
        options.bit_depth);
    if (!heif_ok(error)) {
      return fail(heif_failure("hdr_avif_plane_create_failed", error));
    }
    int stride = 0;
    auto* plane = heif_image_get_plane(
        image.get(),
        heif_channel_interleaved,
        &stride);
    if (plane == nullptr || stride < options.width * 6) {
      return fail("hdr_avif_plane_mapping_failed");
    }
    const auto maximum_code = (1U << options.bit_depth) - 1U;
    for (int y = 0; y < options.height; ++y) {
      if (canceled()) {
        return fail("hdr_avif_write_canceled");
      }
      const auto source_row = static_cast<std::size_t>(y) *
                              options.row_stride_floats;
      auto* destination = plane + static_cast<std::ptrdiff_t>(y) * stride;
      for (int x = 0; x < options.width; ++x) {
        const auto source_pixel =
            source_row + static_cast<std::size_t>(x) * 4U;
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
          const auto encoded = std::clamp(
              encode_channel(
                  linear_reference_relative_rgba[source_pixel + channel],
                  options),
              0.0,
              1.0);
          const auto code = static_cast<std::uint16_t>(
              std::lround(encoded * static_cast<double>(maximum_code)));
          write_be_u16(
              destination + static_cast<std::ptrdiff_t>(x) * 6 +
                  static_cast<std::ptrdiff_t>(channel) * 2,
              code);
        }
      }
    }

    NclxPtr nclx(heif_nclx_color_profile_alloc());
    if (!nclx) {
      return fail("hdr_avif_nclx_allocation_failed");
    }
    nclx->color_primaries =
        heif_color_primaries_ITU_R_BT_2020_2_and_2100_0;
    nclx->transfer_characteristics =
        options.transfer == HdrAvifTransfer::Pq
            ? heif_transfer_characteristic_ITU_R_BT_2100_0_PQ
            : heif_transfer_characteristic_ITU_R_BT_2100_0_HLG;
    nclx->matrix_coefficients =
        heif_matrix_coefficients_ITU_R_BT_2020_2_non_constant_luminance;
    nclx->full_range_flag = 1;
    error = heif_image_set_nclx_color_profile(image.get(), nclx.get());
    if (!heif_ok(error)) {
      return fail(heif_failure("hdr_avif_nclx_attach_failed", error));
    }

    const auto content_light = heif_content_light_level{
        .max_content_light_level = static_cast<std::uint16_t>(
            options.max_content_light_level),
        .max_pic_average_light_level = static_cast<std::uint16_t>(
            options.max_frame_average_light_level),
    };
    heif_image_set_content_light_level(image.get(), &content_light);
    const auto mastering = heif_mastering_display_colour_volume{
        .display_primaries_x = {
            chromaticity_code(0.1700),
            chromaticity_code(0.1310),
            chromaticity_code(0.7080),
        },
        .display_primaries_y = {
            chromaticity_code(0.7970),
            chromaticity_code(0.0460),
            chromaticity_code(0.2920),
        },
        .white_point_x = chromaticity_code(0.3127),
        .white_point_y = chromaticity_code(0.3290),
        .max_display_mastering_luminance = luminance_code(
            options.peak_luminance_nits),
        .min_display_mastering_luminance = luminance_code(
            options.black_luminance_nits),
    };
    heif_image_set_mastering_display_colour_volume(image.get(), &mastering);

    heif_encoder* raw_encoder = nullptr;
    error = heif_context_get_encoder_for_format(
        context.get(),
        heif_compression_AV1,
        &raw_encoder);
    if (!heif_ok(error)) {
      return fail(heif_failure("hdr_avif_encoder_unavailable", error));
    }
    EncoderPtr encoder(raw_encoder);
    error = heif_encoder_set_parameter_string(
        encoder.get(), "chroma", "444");
    if (!heif_ok(error)) {
      return fail(heif_failure("hdr_avif_chroma_444_failed", error));
    }
    if (options.quality == 100) {
      error = heif_encoder_set_lossless(encoder.get(), 1);
      if (!heif_ok(error)) {
        return fail(heif_failure("hdr_avif_lossless_failed", error));
      }
    } else {
      error = heif_encoder_set_lossy_quality(encoder.get(), options.quality);
      if (!heif_ok(error)) {
        return fail(heif_failure("hdr_avif_quality_failed", error));
      }
    }
    EncodingOptionsPtr encoding_options(heif_encoding_options_alloc());
    if (!encoding_options) {
      return fail("hdr_avif_encoding_options_allocation_failed");
    }
    encoding_options->save_alpha_channel = 0;
    encoding_options->output_nclx_profile = nclx.get();
    heif_image_handle* raw_handle = nullptr;
    if (canceled()) {
      return fail("hdr_avif_write_canceled");
    }
    error = heif_context_encode_image(
        context.get(),
        image.get(),
        encoder.get(),
        encoding_options.get(),
        &raw_handle);
    if (!heif_ok(error)) {
      return fail(heif_failure("hdr_avif_encode_failed", error));
    }
    if (canceled()) {
      return fail("hdr_avif_write_canceled");
    }
    HandlePtr handle(raw_handle);
    const auto path = std::string(file_path);
    error = heif_context_write_to_file(context.get(), path.c_str());
    if (!heif_ok(error)) {
      return fail(heif_failure("hdr_avif_write_failed", error));
    }
    if (canceled()) {
      return fail("hdr_avif_write_canceled");
    }
    if (failure_reason != nullptr) {
      failure_reason->clear();
    }
    return true;
  } catch (const std::exception& error) {
    return fail(std::string("hdr_avif_write_failed:") + error.what());
  }
#else
  static_cast<void>(file_path);
  static_cast<void>(linear_reference_relative_rgba);
  static_cast<void>(options);
  return fail("hdr_avif_backend_unavailable");
#endif
}

HdrAvifInspection inspect_hdr_avif(
    std::string_view file_path,
    bool read_pixels) {
  HdrAvifInspection inspection;
#if defined(PHOTO_EDITOR_HAS_LIBHEIF_HDR) && PHOTO_EDITOR_HAS_LIBHEIF_HDR
  if (file_path.empty()) {
    inspection.failure_reason = "hdr_avif_path_empty";
    return inspection;
  }
  ContextPtr context(heif_context_alloc());
  if (!context) {
    inspection.failure_reason = "hdr_avif_context_allocation_failed";
    return inspection;
  }
  const auto path = std::string(file_path);
  auto error = heif_context_read_from_file(context.get(), path.c_str(), nullptr);
  if (!heif_ok(error)) {
    inspection.failure_reason = heif_failure("hdr_avif_read_failed", error);
    return inspection;
  }
  heif_image_handle* raw_handle = nullptr;
  error = heif_context_get_primary_image_handle(context.get(), &raw_handle);
  if (!heif_ok(error)) {
    inspection.failure_reason = heif_failure("hdr_avif_primary_image_failed", error);
    return inspection;
  }
  HandlePtr handle(raw_handle);
  inspection.width = heif_image_handle_get_width(handle.get());
  inspection.height = heif_image_handle_get_height(handle.get());
  inspection.bit_depth = heif_image_handle_get_luma_bits_per_pixel(handle.get());

  heif_color_profile_nclx* raw_nclx = nullptr;
  error = heif_image_handle_get_nclx_color_profile(handle.get(), &raw_nclx);
  if (!heif_ok(error) || raw_nclx == nullptr) {
    inspection.failure_reason = heif_failure("hdr_avif_nclx_read_failed", error);
    return inspection;
  }
  NclxPtr nclx(raw_nclx);
  inspection.color_primaries = static_cast<int>(nclx->color_primaries);
  inspection.transfer_characteristics =
      static_cast<int>(nclx->transfer_characteristics);
  inspection.matrix_coefficients = static_cast<int>(nclx->matrix_coefficients);
  inspection.full_range = nclx->full_range_flag != 0;

  heif_content_light_level content_light{};
  if (heif_image_handle_get_content_light_level(handle.get(), &content_light) != 0) {
    inspection.max_content_light_level = content_light.max_content_light_level;
    inspection.max_frame_average_light_level =
        content_light.max_pic_average_light_level;
  }
  heif_mastering_display_colour_volume mastering{};
  if (heif_image_handle_get_mastering_display_colour_volume(
          handle.get(),
          &mastering) != 0) {
    heif_decoded_mastering_display_colour_volume decoded_mastering{};
    error = heif_mastering_display_colour_volume_decode(
        &mastering,
        &decoded_mastering);
    if (heif_ok(error)) {
      inspection.has_mastering_display = true;
      inspection.mastering_peak_luminance_nits =
          decoded_mastering.max_display_mastering_luminance;
      inspection.mastering_black_luminance_nits =
          decoded_mastering.min_display_mastering_luminance;
    }
  }

  if (read_pixels) {
    heif_image* raw_image = nullptr;
    error = heif_decode_image(
        handle.get(),
        &raw_image,
        heif_colorspace_RGB,
        heif_chroma_interleaved_RRGGBB_BE,
        nullptr);
    if (!heif_ok(error)) {
      inspection.failure_reason = heif_failure("hdr_avif_decode_failed", error);
      return inspection;
    }
    ImagePtr image(raw_image);
    const auto decoded_depth = heif_image_get_bits_per_pixel_range(
        image.get(),
        heif_channel_interleaved);
    if (decoded_depth > 0) {
      inspection.bit_depth = decoded_depth;
    }
    int stride = 0;
    const auto* plane = heif_image_get_plane_readonly(
        image.get(),
        heif_channel_interleaved,
        &stride);
    if (plane == nullptr || inspection.width <= 0 || inspection.height <= 0 ||
        inspection.bit_depth <= 0 || inspection.bit_depth > 16 ||
        stride < inspection.width * 6) {
      inspection.failure_reason = "hdr_avif_decoded_plane_invalid";
      return inspection;
    }
    const auto maximum_code =
        static_cast<double>((1U << inspection.bit_depth) - 1U);
    inspection.encoded_rgb.resize(
        static_cast<std::size_t>(inspection.width) *
        static_cast<std::size_t>(inspection.height) * 3U);
    for (int y = 0; y < inspection.height; ++y) {
      const auto* source = plane + static_cast<std::ptrdiff_t>(y) * stride;
      for (int x = 0; x < inspection.width; ++x) {
        const auto destination =
            (static_cast<std::size_t>(y) *
                 static_cast<std::size_t>(inspection.width) +
             static_cast<std::size_t>(x)) *
            3U;
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
          inspection.encoded_rgb[destination + channel] =
              static_cast<float>(read_be_u16(
                  source + static_cast<std::ptrdiff_t>(x) * 6 +
                  static_cast<std::ptrdiff_t>(channel) * 2) /
                                 maximum_code);
        }
      }
    }
  }
  inspection.valid = inspection.width > 0 && inspection.height > 0 &&
                     inspection.bit_depth == 10;
  if (!inspection.valid && inspection.failure_reason.empty()) {
    inspection.failure_reason = "hdr_avif_contract_invalid";
  }
  return inspection;
#else
  static_cast<void>(file_path);
  static_cast<void>(read_pixels);
  inspection.failure_reason = "hdr_avif_backend_unavailable";
  return inspection;
#endif
}

}  // namespace photo_editor::io
