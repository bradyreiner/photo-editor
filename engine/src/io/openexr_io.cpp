#include "photo_editor/io/openexr_io.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <utility>

#if defined(PHOTO_EDITOR_HAS_OPENEXR) && PHOTO_EDITOR_HAS_OPENEXR
#include <Imath/ImathVec.h>
#include <Imath/half.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <OpenEXR/ImfCompressionAttribute.h>
#include <OpenEXR/ImfFloatAttribute.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfIntAttribute.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfStringAttribute.h>
#endif

namespace photo_editor::io {

namespace {

[[nodiscard]] OpenExrChromaticities d65_srgb() {
  return {
      .red_x = 0.6400F,
      .red_y = 0.3300F,
      .green_x = 0.3000F,
      .green_y = 0.6000F,
      .blue_x = 0.1500F,
      .blue_y = 0.0600F,
      .white_x = 0.3127F,
      .white_y = 0.3290F,
  };
}

#if defined(PHOTO_EDITOR_HAS_OPENEXR) && PHOTO_EDITOR_HAS_OPENEXR
// ASWF Color Interop Forum OpenEXR recommendation, linear scene IDs.
constexpr std::pair<std::string_view, pipeline::ColorPrimaries> kLinearInteropSpaces[] = {
    {"lin_rec709_scene", pipeline::ColorPrimaries::Srgb},
    {"lin_ap0_scene", pipeline::ColorPrimaries::AcesAp0},
    {"lin_ap1_scene", pipeline::ColorPrimaries::AcesAp1},
    {"lin_p3d65_scene", pipeline::ColorPrimaries::DisplayP3},
    {"lin_rec2020_scene", pipeline::ColorPrimaries::Rec2020},
    {"lin_adobergb_scene", pipeline::ColorPrimaries::AdobeRgb},
};

[[nodiscard]] std::string channel_type_name(
    OPENEXR_IMF_NAMESPACE::PixelType type) {
  switch (type) {
    case OPENEXR_IMF_NAMESPACE::HALF:
      return "half";
    case OPENEXR_IMF_NAMESPACE::FLOAT:
      return "float";
    case OPENEXR_IMF_NAMESPACE::UINT:
      return "uint";
    case OPENEXR_IMF_NAMESPACE::NUM_PIXELTYPES:
      break;
  }
  return "unknown";
}

[[nodiscard]] std::string compression_name(
    OPENEXR_IMF_NAMESPACE::Compression compression) {
  switch (compression) {
    case OPENEXR_IMF_NAMESPACE::NO_COMPRESSION:
      return "none";
    case OPENEXR_IMF_NAMESPACE::RLE_COMPRESSION:
      return "rle";
    case OPENEXR_IMF_NAMESPACE::ZIPS_COMPRESSION:
      return "zips";
    case OPENEXR_IMF_NAMESPACE::ZIP_COMPRESSION:
      return "zip";
    case OPENEXR_IMF_NAMESPACE::PIZ_COMPRESSION:
      return "piz";
    case OPENEXR_IMF_NAMESPACE::PXR24_COMPRESSION:
      return "pxr24";
    case OPENEXR_IMF_NAMESPACE::B44_COMPRESSION:
      return "b44";
    case OPENEXR_IMF_NAMESPACE::B44A_COMPRESSION:
      return "b44a";
    case OPENEXR_IMF_NAMESPACE::DWAA_COMPRESSION:
      return "dwaa";
    case OPENEXR_IMF_NAMESPACE::DWAB_COMPRESSION:
      return "dwab";
    case OPENEXR_IMF_NAMESPACE::HTJ2K256_COMPRESSION:
      return "htj2k256";
    case OPENEXR_IMF_NAMESPACE::HTJ2K32_COMPRESSION:
      return "htj2k32";
    case OPENEXR_IMF_NAMESPACE::NUM_COMPRESSION_METHODS:
      break;
  }
  return "unknown";
}

template <typename Attribute>
[[nodiscard]] const Attribute* typed_attribute(
    const OPENEXR_IMF_NAMESPACE::Header& header,
    const char* name) {
  return header.findTypedAttribute<Attribute>(name);
}
#endif

}  // namespace

bool openexr_backend_available() {
#if defined(PHOTO_EDITOR_HAS_OPENEXR) && PHOTO_EDITOR_HAS_OPENEXR
  return true;
#else
  return false;
#endif
}

std::string_view openexr_backend_version() {
#if defined(PHOTO_EDITOR_HAS_OPENEXR) && PHOTO_EDITOR_HAS_OPENEXR
  return PHOTO_EDITOR_OPENEXR_COMPILED_VERSION;
#else
  return "unavailable";
#endif
}

OpenExrChromaticities openexr_chromaticities_for_color_space(
    const pipeline::RgbColorSpace& color_space) {
  using pipeline::ColorPrimaries;
  switch (color_space.primaries) {
    case ColorPrimaries::DisplayP3:
      return {0.6800F, 0.3200F, 0.2650F, 0.6900F,
              0.1500F, 0.0600F, 0.3127F, 0.3290F};
    case ColorPrimaries::AdobeRgb:
      return {0.6400F, 0.3300F, 0.2100F, 0.7100F,
              0.1500F, 0.0600F, 0.3127F, 0.3290F};
    case ColorPrimaries::ProPhotoRgb:
      return {0.7347F, 0.2653F, 0.1596F, 0.8404F,
              0.0366F, 0.0001F, 0.34567F, 0.35850F};
    case ColorPrimaries::Rec2020:
      return {0.7080F, 0.2920F, 0.1700F, 0.7970F,
              0.1310F, 0.0460F, 0.3127F, 0.3290F};
    case ColorPrimaries::AcesAp0:
      return {0.7347F, 0.2653F, 0.0000F, 1.0000F,
              0.0001F, -0.0770F, 0.32168F, 0.33767F};
    case ColorPrimaries::AcesAp1:
      return {0.7130F, 0.2930F, 0.1650F, 0.8300F,
              0.1280F, 0.0440F, 0.32168F, 0.33767F};
    case ColorPrimaries::Srgb:
    case ColorPrimaries::Rec709:
    case ColorPrimaries::Unknown:
      return d65_srgb();
  }
  return d65_srgb();
}

bool write_openexr_rgba(
    std::string_view file_path,
    std::span<const float> rgba_pixels,
    const OpenExrWriteOptions& options,
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
#if defined(PHOTO_EDITOR_HAS_OPENEXR) && PHOTO_EDITOR_HAS_OPENEXR
  if (canceled()) {
    return fail("openexr_write_canceled");
  }
  if (file_path.empty() || options.width <= 0 || options.height <= 0 ||
      options.row_stride_floats <
          static_cast<std::size_t>(options.width) * 4U ||
      options.color_space.transfer_function !=
          pipeline::TransferFunction::Linear ||
      !std::isfinite(options.white_luminance_nits) ||
      options.white_luminance_nits <= 0.0F) {
    return fail("invalid_openexr_write_options");
  }
  const auto data_window_max_x =
      static_cast<std::int64_t>(options.data_window_min_x) +
      static_cast<std::int64_t>(options.width) - 1;
  const auto data_window_max_y =
      static_cast<std::int64_t>(options.data_window_min_y) +
      static_cast<std::int64_t>(options.height) - 1;
  if (data_window_max_x > std::numeric_limits<int>::max() ||
      data_window_max_x < std::numeric_limits<int>::min() ||
      data_window_max_y > std::numeric_limits<int>::max() ||
      data_window_max_y < std::numeric_limits<int>::min()) {
    return fail("invalid_openexr_data_window");
  }
  const auto required = options.row_stride_floats *
                        static_cast<std::size_t>(options.height);
  if (rgba_pixels.size() < required) {
    return fail("openexr_pixel_buffer_too_small");
  }
  for (int y = 0; y < options.height; ++y) {
    if (canceled()) {
      return fail("openexr_write_canceled");
    }
    const auto row = static_cast<std::size_t>(y) *
                     options.row_stride_floats;
    for (int x = 0; x < options.width; ++x) {
      const auto pixel = row + static_cast<std::size_t>(x) * 4U;
      for (std::size_t channel = 0U; channel < 4U; ++channel) {
        if (!std::isfinite(rgba_pixels[pixel + channel])) {
          return fail("openexr_pixel_buffer_contains_non_finite_values");
        }
      }
    }
  }
  try {
    namespace Imf = OPENEXR_IMF_NAMESPACE;
    namespace Imath = IMATH_NAMESPACE;
    Imf::Header header(options.width, options.height);
    const auto data_window = Imath::Box2i(
        Imath::V2i(options.data_window_min_x, options.data_window_min_y),
        Imath::V2i(
            static_cast<int>(data_window_max_x),
            static_cast<int>(data_window_max_y)));
    header.dataWindow() = data_window;
    header.displayWindow() = data_window;
    const auto output_type = options.half_float ? Imf::HALF : Imf::FLOAT;
    header.channels().insert("R", Imf::Channel(output_type));
    header.channels().insert("G", Imf::Channel(output_type));
    header.channels().insert("B", Imf::Channel(output_type));
    if (options.include_alpha) {
      header.channels().insert("A", Imf::Channel(output_type));
    }
    header.compression() = Imf::ZIP_COMPRESSION;
    const auto values = openexr_chromaticities_for_color_space(
        options.color_space);
    header.insert(
        "chromaticities",
        Imf::ChromaticitiesAttribute(Imf::Chromaticities(
            Imath::V2f(values.red_x, values.red_y),
            Imath::V2f(values.green_x, values.green_y),
            Imath::V2f(values.blue_x, values.blue_y),
            Imath::V2f(values.white_x, values.white_y))));
    header.insert(
        "whiteLuminance",
        Imf::FloatAttribute(options.white_luminance_nits));
    header.insert(
        "virel:colorSpace",
        Imf::StringAttribute(options.color_space.name));
    header.insert("virel:transferFunction", Imf::StringAttribute("linear"));
    for (const auto& [id, primaries] : kLinearInteropSpaces) {
      if (options.color_space.primaries == primaries ||
          (primaries == pipeline::ColorPrimaries::Srgb && options.color_space.primaries == pipeline::ColorPrimaries::Rec709)) {
        header.insert("colorInteropID", Imf::StringAttribute(std::string(id)));
        break;
      }
    }
    header.insert(
        "virel:renderingModel",
        Imf::StringAttribute(options.rendering_model));
    header.insert(
        "virel:transformProvenance",
        Imf::StringAttribute(options.transform_provenance));

    std::vector<IMATH_NAMESPACE::half> half_pixels;
    auto* base = reinterpret_cast<char*>(
        const_cast<float*>(rgba_pixels.data()));
    auto input_type = Imf::FLOAT;
    auto component_size = sizeof(float);
    if (options.half_float) {
      half_pixels.resize(required);
      for (int y = 0; y < options.height; ++y) {
        if (canceled()) {
          return fail("openexr_write_canceled");
        }
        const auto row =
            static_cast<std::size_t>(y) *
            options.row_stride_floats;
        std::transform(
            rgba_pixels.begin() +
                static_cast<std::ptrdiff_t>(row),
            rgba_pixels.begin() +
                static_cast<std::ptrdiff_t>(
                    row + options.row_stride_floats),
            half_pixels.begin() +
                static_cast<std::ptrdiff_t>(row),
            [](float value) {
              return IMATH_NAMESPACE::half(value);
            });
      }
      base = reinterpret_cast<char*>(half_pixels.data());
      input_type = Imf::HALF;
      component_size = sizeof(IMATH_NAMESPACE::half);
    }
    Imf::FrameBuffer frame_buffer;
    const auto x_stride = component_size * 4U;
    const auto y_stride = component_size * options.row_stride_floats;
    frame_buffer.insert(
        "R",
        Imf::Slice::Make(
            input_type,
            base,
            data_window,
            x_stride,
            y_stride));
    frame_buffer.insert(
        "G",
        Imf::Slice::Make(
            input_type,
            base + component_size,
            data_window,
            x_stride,
            y_stride));
    frame_buffer.insert(
        "B",
        Imf::Slice::Make(
            input_type,
            base + component_size * 2U,
            data_window,
            x_stride,
            y_stride));
    if (options.include_alpha) {
      frame_buffer.insert(
          "A",
          Imf::Slice::Make(
              input_type,
              base + component_size * 3U,
              data_window,
              x_stride,
              y_stride));
    }
    const auto path = std::string(file_path);
    Imf::OutputFile output(path.c_str(), header);
    output.setFrameBuffer(frame_buffer);
    for (int y = 0; y < options.height; ++y) {
      if (canceled()) {
        return fail("openexr_write_canceled");
      }
      output.writePixels(1);
    }
    if (failure_reason != nullptr) {
      failure_reason->clear();
    }
    return true;
  } catch (const std::exception& error) {
    return fail(std::string("openexr_write_failed:") + error.what());
  }
#else
  static_cast<void>(file_path);
  static_cast<void>(rgba_pixels);
  static_cast<void>(options);
  return fail("openexr_backend_unavailable");
#endif
}

OpenExrInspection inspect_openexr(
    std::string_view file_path,
    bool read_pixels) {
  OpenExrInspection inspection;
#if defined(PHOTO_EDITOR_HAS_OPENEXR) && PHOTO_EDITOR_HAS_OPENEXR
  if (file_path.empty()) {
    inspection.failure_reason = "openexr_path_empty";
    return inspection;
  }
  try {
    namespace Imf = OPENEXR_IMF_NAMESPACE;
    const auto path = std::string(file_path);
    Imf::InputFile input(path.c_str());
    const auto& header = input.header();
    const auto data_window = header.dataWindow();
    const auto width = static_cast<std::int64_t>(data_window.max.x) -
                       static_cast<std::int64_t>(data_window.min.x) + 1;
    const auto height = static_cast<std::int64_t>(data_window.max.y) -
                        static_cast<std::int64_t>(data_window.min.y) + 1;
    if (width <= 0 || height <= 0 ||
        width > std::numeric_limits<int>::max() ||
        height > std::numeric_limits<int>::max()) {
      inspection.failure_reason = "invalid_openexr_data_window";
      return inspection;
    }
    inspection.width = static_cast<int>(width);
    inspection.height = static_cast<int>(height);
    const auto* red = header.channels().findChannel("R");
    const auto* green = header.channels().findChannel("G");
    const auto* blue = header.channels().findChannel("B");
    const auto* alpha = header.channels().findChannel("A");
    if (red == nullptr ||
        green == nullptr || blue == nullptr || red->type != green->type ||
        red->type != blue->type || red->xSampling != 1 || red->ySampling != 1 ||
        green->xSampling != 1 || green->ySampling != 1 ||
        blue->xSampling != 1 || blue->ySampling != 1 ||
        (alpha != nullptr &&
         (alpha->xSampling != 1 || alpha->ySampling != 1))) {
      inspection.failure_reason = "invalid_openexr_rgb_channels";
      return inspection;
    }
    inspection.has_alpha = alpha != nullptr;
    inspection.channel_type = channel_type_name(red->type);
    inspection.compression = compression_name(header.compression());
    if (const auto* attribute = typed_attribute<Imf::ChromaticitiesAttribute>(
            header,
            "chromaticities")) {
      const auto& value = attribute->value();
      inspection.chromaticities = {
          value.red.x, value.red.y,
          value.green.x, value.green.y,
          value.blue.x, value.blue.y,
          value.white.x, value.white.y,
      };
    }
    if (const auto* attribute = typed_attribute<Imf::FloatAttribute>(
            header,
            "whiteLuminance")) {
      inspection.white_luminance_nits = attribute->value();
    }
    const auto read_string = [&](const char* name) {
      const auto* attribute = typed_attribute<Imf::StringAttribute>(
          header,
          name);
      return attribute != nullptr ? attribute->value() : std::string{};
    };
    inspection.color_space_name = read_string("virel:colorSpace");
    inspection.transfer_function = read_string("virel:transferFunction");
    inspection.rendering_model = read_string("virel:renderingModel");
    inspection.transform_provenance = read_string("virel:transformProvenance");
    // ACES container metadata is authoritative, followed by a recognized
    // Interop ID. Missing/unknown IDs may use explicit chromaticities; never
    // invent Rec.709 chromaticities when neither source identifies the space.
    auto interop_id = read_string("colorInteropID");
    const auto* aces_flag = typed_attribute<Imf::IntAttribute>(header, "acesImageContainerFlag");
    if (aces_flag != nullptr && aces_flag->value() == 1) interop_id = "lin_ap0_scene";
    if (!interop_id.empty() && interop_id != "unknown") {
      const auto known = std::find_if(std::begin(kLinearInteropSpaces), std::end(kLinearInteropSpaces),
          [&](const auto& entry) { return entry.first == interop_id; });
      if (known == std::end(kLinearInteropSpaces)) {
        inspection.failure_reason = interop_id == "data" ? "openexr_non_color_data" : "unsupported_openexr_color_interop_id";
        return inspection;
      }
      auto space = pipeline::make_linear_srgb_color_space();
      space.primaries = known->second;
      inspection.chromaticities = openexr_chromaticities_for_color_space(space);
      inspection.transfer_function = "linear";
    } else if (!inspection.transfer_function.empty() && inspection.transfer_function != "linear") {
      inspection.failure_reason = "unsupported_openexr_transfer_function";
      return inspection;
    }
    if (read_pixels) {
      const auto pixel_count = static_cast<std::size_t>(inspection.width) *
                               static_cast<std::size_t>(inspection.height);
      inspection.rgba_pixels.assign(pixel_count * 4U, 0.0F);
      for (std::size_t index = 0U; index < pixel_count; ++index) {
        inspection.rgba_pixels[index * 4U + 3U] = 1.0F;
      }
      auto* const base = reinterpret_cast<char*>(inspection.rgba_pixels.data());
      const auto x_stride = sizeof(float) * 4U;
      const auto y_stride = x_stride * static_cast<std::size_t>(inspection.width);
      Imf::FrameBuffer frame_buffer;
      frame_buffer.insert(
          "R",
          Imf::Slice::Make(
              Imf::FLOAT,
              base,
              data_window,
              x_stride,
              y_stride));
      frame_buffer.insert(
          "G",
          Imf::Slice::Make(
              Imf::FLOAT,
              base + sizeof(float),
              data_window,
              x_stride,
              y_stride));
      frame_buffer.insert(
          "B",
          Imf::Slice::Make(
              Imf::FLOAT,
              base + sizeof(float) * 2U,
              data_window,
              x_stride,
              y_stride));
      if (inspection.has_alpha) {
        frame_buffer.insert(
            "A",
            Imf::Slice::Make(
                Imf::FLOAT,
                base + sizeof(float) * 3U,
                data_window,
                x_stride,
                y_stride));
      }
      input.setFrameBuffer(frame_buffer);
      input.readPixels(data_window.min.y, data_window.max.y);
      inspection.non_finite_sample_count = static_cast<std::size_t>(
          std::count_if(
              inspection.rgba_pixels.cbegin(),
              inspection.rgba_pixels.cend(),
              [](float value) { return !std::isfinite(value); }));
    }
    inspection.valid = true;
    return inspection;
  } catch (const std::exception& error) {
    inspection.failure_reason = std::string("openexr_read_failed:") +
                                error.what();
    return inspection;
  }
#else
  static_cast<void>(file_path);
  static_cast<void>(read_pixels);
  inspection.failure_reason = "openexr_backend_unavailable";
  return inspection;
#endif
}

}  // namespace photo_editor::io
