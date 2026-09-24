#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <lcms2.h>

#if defined(PHOTO_EDITOR_HAS_QT_COLOR_ORACLE)
#include <QtCore/qglobal.h>
#include <QtCore/qsysinfo.h>
#include <QtGui/qcolorspace.h>
#include <QtGui/qcolortransform.h>
#include <QtGui/qrgbafloat.h>
#endif

#if defined(PHOTO_EDITOR_HAS_COLORSYNC_ORACLE)
#include <CoreGraphics/CoreGraphics.h>
#endif

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_metrics.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/color_transform.h"

#ifndef PHOTO_EDITOR_CMM_COMPARISON_CORPUS_PATH
#error "PHOTO_EDITOR_CMM_COMPARISON_CORPUS_PATH must point to the CMM corpus"
#endif
#ifndef PHOTO_EDITOR_CMM_COMPARISON_CORPUS_SHA256
#error "PHOTO_EDITOR_CMM_COMPARISON_CORPUS_SHA256 must identify the CMM corpus"
#endif
#ifndef PHOTO_EDITOR_CMM_SOURCE_PROFILE_SHA256
#error "PHOTO_EDITOR_CMM_SOURCE_PROFILE_SHA256 must identify the source profile definition"
#endif
#ifndef PHOTO_EDITOR_CMM_DESTINATION_PROFILE_SHA256
#error "PHOTO_EDITOR_CMM_DESTINATION_PROFILE_SHA256 must identify the destination profile definition"
#endif
#ifndef PHOTO_EDITOR_ICC_CORPUS_DIR
#error "PHOTO_EDITOR_ICC_CORPUS_DIR must point to the versioned ICC corpus"
#endif
#ifndef PHOTO_EDITOR_ICC_CORPUS_MANIFEST_SHA256
#error "PHOTO_EDITOR_ICC_CORPUS_MANIFEST_SHA256 must identify the ICC corpus manifest"
#endif

namespace {

struct Patch final {
  std::string name;
  photo_editor::pipeline::LinearRgb linear_display_p3;
  double alpha = 1.0;
  std::string category;
};

struct EngineOutput final {
  photo_editor::pipeline::LinearRgb rgb;
  double alpha = 1.0;
};

struct PatchResult final {
  std::string name;
  std::string category;
  double relative_delta_e_00 = 0.0;
  double perceptual_delta_e_00 = 0.0;
  double qt_delta_e_00 = 0.0;
  double colorsync_delta_e_00 = 0.0;
  double engine_icc_delta_e_00 = 0.0;
  double alpha_error = 0.0;
};

struct ErrorSummary final {
  double p50 = 0.0;
  double p95 = 0.0;
  double maximum = 0.0;
};

struct HeatmapRow final {
  std::string label;
  std::vector<double> values;
  double display_scale = 0.0;
  bool gate_enforced = true;
};

[[nodiscard]] int fail(std::string_view message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

[[nodiscard]] std::vector<std::string> split_csv_line(const std::string& line) {
  std::vector<std::string> fields;
  std::stringstream stream(line);
  std::string field;
  while (std::getline(stream, field, ',')) {
    fields.push_back(field);
  }
  return fields;
}

[[nodiscard]] bool parse_double(const std::string& value, double* output) {
  if (output == nullptr) {
    return false;
  }
  try {
    std::size_t parsed = 0;
    const auto result = std::stod(value, &parsed);
    if (parsed != value.size() || !std::isfinite(result)) {
      return false;
    }
    *output = result;
    return true;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] std::vector<std::byte> load_base64_profile(std::string_view filename) {
  const auto path = std::string(PHOTO_EDITOR_ICC_CORPUS_DIR) + "/" + std::string(filename);
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }
  const std::string encoded{
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>()};
  auto value_for = [](unsigned char character) -> int {
    if (character >= 'A' && character <= 'Z') {
      return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
      return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
      return character - '0' + 52;
    }
    if (character == '+') {
      return 62;
    }
    if (character == '/') {
      return 63;
    }
    return -1;
  };
  std::vector<std::byte> decoded;
  decoded.reserve(encoded.size() * 3 / 4);
  auto accumulator = 0U;
  auto bits = 0;
  for (const auto character : encoded) {
    if (character == '=') {
      break;
    }
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      continue;
    }
    const auto value = value_for(static_cast<unsigned char>(character));
    if (value < 0) {
      return {};
    }
    accumulator = (accumulator << 6U) | static_cast<unsigned int>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      decoded.push_back(static_cast<std::byte>((accumulator >> bits) & 0xFFU));
    }
  }
  return decoded;
}

[[nodiscard]] std::vector<Patch> load_patches() {
  std::ifstream input(PHOTO_EDITOR_CMM_COMPARISON_CORPUS_PATH);
  if (!input) {
    return {};
  }
  std::vector<Patch> patches;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.front() == '#' || line.rfind("name,", 0) == 0) {
      continue;
    }
    const auto fields = split_csv_line(line);
    if (fields.size() != 6) {
      return {};
    }
    std::array<double, 4> values{};
    auto valid = true;
    for (std::size_t index = 0; index < values.size(); ++index) {
      valid = parse_double(fields[index + 1], &values[index]) && valid;
    }
    if (!valid) {
      return {};
    }
    patches.push_back(Patch{
        .name = fields[0],
        .linear_display_p3 = {
            .red = values[0],
            .green = values[1],
            .blue = values[2],
        },
        .alpha = values[3],
        .category = fields[5],
    });
  }
  return patches;
}

[[nodiscard]] photo_editor::pipeline::CieLab linear_srgb_to_lab(
    const photo_editor::pipeline::LinearRgb& rgb) {
  return photo_editor::pipeline::cie_xyz_to_lab(
      {
          .x = 0.4124564 * rgb.red + 0.3575761 * rgb.green + 0.1804375 * rgb.blue,
          .y = 0.2126729 * rgb.red + 0.7151522 * rgb.green + 0.0721750 * rgb.blue,
          .z = 0.0193339 * rgb.red + 0.1191920 * rgb.green + 0.9503041 * rgb.blue,
      },
      photo_editor::pipeline::cie_d65_white());
}

[[nodiscard]] photo_editor::pipeline::LinearRgb decode_srgb(
    const std::array<double, 3>& encoded) {
  return {
      .red = photo_editor::pipeline::srgb_to_linear(encoded[0]),
      .green = photo_editor::pipeline::srgb_to_linear(encoded[1]),
      .blue = photo_editor::pipeline::srgb_to_linear(encoded[2]),
  };
}

[[nodiscard]] ErrorSummary summarize(std::vector<double> values) {
  if (values.empty()) {
    return {};
  }
  std::sort(values.begin(), values.end());
  const auto percentile = [&values](double fraction) {
    const auto rank = static_cast<std::size_t>(std::ceil(
        fraction * static_cast<double>(values.size())));
    const auto index = std::min(values.size() - 1, std::max<std::size_t>(1, rank) - 1);
    return values[index];
  };
  return ErrorSummary{
      .p50 = percentile(0.50),
      .p95 = percentile(0.95),
      .maximum = values.back(),
  };
}

template <typename Value>
[[nodiscard]] const PatchResult& worst_patch(
    const std::vector<PatchResult>& results,
    Value value) {
  return *std::max_element(
      results.begin(),
      results.end(),
      [&value](const PatchResult& left, const PatchResult& right) {
        return value(left) < value(right);
      });
}

[[nodiscard]] cmsHPROFILE create_linear_display_p3_profile() {
  const cmsCIExyY white{0.3127, 0.3290, 1.0};
  const cmsCIExyYTRIPLE primaries{
      .Red = {0.6800, 0.3200, 1.0},
      .Green = {0.2650, 0.6900, 1.0},
      .Blue = {0.1500, 0.0600, 1.0},
  };
  std::array<cmsToneCurve*, 3> curves = {
      cmsBuildGamma(nullptr, 1.0),
      cmsBuildGamma(nullptr, 1.0),
      cmsBuildGamma(nullptr, 1.0),
  };
  if (std::any_of(curves.begin(), curves.end(), [](cmsToneCurve* curve) {
        return curve == nullptr;
      })) {
    for (auto* curve : curves) {
      if (curve != nullptr) {
        cmsFreeToneCurve(curve);
      }
    }
    return nullptr;
  }
  const auto profile = cmsCreateRGBProfile(&white, &primaries, curves.data());
  for (auto* curve : curves) {
    cmsFreeToneCurve(curve);
  }
  return profile;
}

[[nodiscard]] std::vector<std::byte> serialize_profile(cmsHPROFILE profile) {
  cmsUInt32Number size = 0;
  if (profile == nullptr || !cmsSaveProfileToMem(profile, nullptr, &size) || size == 0) {
    return {};
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  if (!cmsSaveProfileToMem(profile, bytes.data(), &size)) {
    return {};
  }
  bytes.resize(static_cast<std::size_t>(size));
  return bytes;
}

[[nodiscard]] EngineOutput engine_output(
    const Patch& patch,
    photo_editor::pipeline::RenderIntent intent) {
  auto config = photo_editor::pipeline::default_color_pipeline_config();
  config.working_profile =
      photo_editor::pipeline::working_color_profile_from_name("Linear Display P3");
  config.preview_output_profile =
      photo_editor::pipeline::output_color_profile_from_name("sRGB");
  config.render_intent = intent;
  config.output_gamut_mapping_enabled = true;
  config.output_gamut_mapping_policy =
      intent == photo_editor::pipeline::RenderIntent::RelativeColorimetric
          ? photo_editor::pipeline::GamutMappingPolicy::Clip
          : photo_editor::pipeline::GamutMappingPolicy::Perceptual;
  const auto output = photo_editor::pipeline::apply_output_transform(
      {
          .red = static_cast<float>(patch.linear_display_p3.red),
          .green = static_cast<float>(patch.linear_display_p3.green),
          .blue = static_cast<float>(patch.linear_display_p3.blue),
          .alpha = static_cast<float>(patch.alpha),
      },
      config,
      photo_editor::pipeline::GamutWarningTarget::PreviewDisplay);
  return EngineOutput{
      .rgb = {
          .red = output.pixel.red,
          .green = output.pixel.green,
          .blue = output.pixel.blue,
      },
      .alpha = output.pixel.alpha,
  };
}

[[nodiscard]] std::string json_escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const auto character : value) {
    if (character == '\\' || character == '"') {
      escaped.push_back('\\');
    }
    escaped.push_back(character);
  }
  return escaped;
}

[[nodiscard]] std::string xml_escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const auto character : value) {
    switch (character) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&apos;";
        break;
      default:
        escaped.push_back(character);
        break;
    }
  }
  return escaped;
}

[[nodiscard]] std::string heatmap_color(double delta_e, double release_gate) {
  struct Color final {
    double red;
    double green;
    double blue;
  };
  const auto gate = std::max(release_gate, 1.0e-9);
  const auto normalized = std::clamp(delta_e / gate, 0.0, 2.0);
  Color first{};
  Color second{};
  double amount = 0.0;
  if (normalized <= 0.33) {
    first = {24.0, 74.0, 112.0};
    second = {43.0, 151.0, 92.0};
    amount = normalized / 0.33;
  } else if (normalized <= 1.0) {
    first = {43.0, 151.0, 92.0};
    second = {242.0, 190.0, 45.0};
    amount = (normalized - 0.33) / 0.67;
  } else {
    first = {242.0, 190.0, 45.0};
    second = {190.0, 39.0, 52.0};
    amount = normalized - 1.0;
  }
  const auto channel = [amount](double start, double end) {
    return static_cast<int>(std::round(start + (end - start) * amount));
  };
  std::ostringstream color;
  color << "rgb(" << channel(first.red, second.red) << ','
        << channel(first.green, second.green) << ','
        << channel(first.blue, second.blue) << ')';
  return color.str();
}

[[nodiscard]] bool write_error_heatmap(
    std::string_view path,
    const std::vector<PatchResult>& patches,
    const std::vector<HeatmapRow>& rows) {
  if (patches.empty() || rows.empty() ||
      std::any_of(rows.begin(), rows.end(), [&patches](const HeatmapRow& row) {
        return row.values.size() != patches.size() || row.display_scale <= 0.0;
      })) {
    return false;
  }
  std::ofstream output{std::string(path)};
  if (!output) {
    return false;
  }

  constexpr auto kLabelWidth = 220;
  constexpr auto kCellWidth = 58;
  constexpr auto kCellHeight = 44;
  constexpr auto kCellsTop = 190;
  constexpr auto kRightPadding = 28;
  constexpr auto kBottomPadding = 92;
  const auto width = kLabelWidth +
                     static_cast<int>(patches.size()) * kCellWidth +
                     kRightPadding;
  const auto height = kCellsTop + static_cast<int>(rows.size()) * kCellHeight +
                      kBottomPadding;

  output << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
         << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << ' '
         << height << "\" role=\"img\" aria-labelledby=\"title description\">\n"
         << "  <title id=\"title\">Photo Editor CMM delta E 2000 heatmap</title>\n"
         << "  <desc id=\"description\">False-color comparison of every deterministic "
            "patch against independent color-management oracles. Cell tooltips contain "
            "the exact patch, category, delta E, and enforced gate or informational "
            "display scale.</desc>\n"
         << "  <metadata>photo-editor-cmm-error-heatmap:v1; corpus-sha256="
         << PHOTO_EDITOR_CMM_COMPARISON_CORPUS_SHA256 << "</metadata>\n"
         << "  <rect width=\"100%\" height=\"100%\" fill=\"#101419\"/>\n"
         << "  <style>text{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif}"
            ".heading{fill:#f5f7fa;font-size:18px;font-weight:650}"
            ".subtle{fill:#aab4c0;font-size:11px}"
            ".row{fill:#e5eaf0;font-size:11px;font-weight:600}"
            ".value{fill:#fff;font-size:9px;font-weight:650;text-anchor:middle;"
            "paint-order:stroke;stroke:#000;stroke-width:2px;stroke-opacity:.42}"
            ".patch{fill:#c7d0da;font-size:9px}</style>\n"
         << "  <text x=\"20\" y=\"30\" class=\"heading\">CMM patch error | delta E 2000</text>\n"
         << "  <text x=\"20\" y=\"49\" class=\"subtle\">Blue/green is comfortably "
            "inside the row gate; yellow reaches the gate; red exceeds it.</text>\n";

  for (std::size_t index = 0; index < patches.size(); ++index) {
    const auto x = kLabelWidth + static_cast<int>(index) * kCellWidth + kCellWidth / 2;
    output << "  <text class=\"patch\" transform=\"translate(" << x
           << " 176) rotate(-58)\">" << xml_escape(patches[index].name)
           << "</text>\n";
  }

  output << std::fixed << std::setprecision(4);
  for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
    const auto& row = rows[row_index];
    const auto y = kCellsTop + static_cast<int>(row_index) * kCellHeight;
    output << "  <text x=\"20\" y=\"" << y + 18
           << "\" class=\"row\">" << xml_escape(row.label) << "</text>\n"
           << "  <text x=\"20\" y=\"" << y + 34
           << "\" class=\"subtle\">"
           << (row.gate_enforced ? "max gate " : "informational scale ")
           << row.display_scale
           << (row.gate_enforced ? "" : " (not gated)") << "</text>\n";
    for (std::size_t patch_index = 0; patch_index < patches.size(); ++patch_index) {
      const auto x = kLabelWidth + static_cast<int>(patch_index) * kCellWidth;
      const auto value = row.values[patch_index];
      output << "  <g><title>" << xml_escape(row.label) << " | "
             << xml_escape(patches[patch_index].name) << " | category "
             << xml_escape(patches[patch_index].category) << " | delta E 2000 "
             << value << " | "
             << (row.gate_enforced ? "max gate " : "informational scale ")
             << row.display_scale << "</title>"
             << "<rect x=\"" << x << "\" y=\"" << y
             << "\" width=\"" << kCellWidth - 2 << "\" height=\""
             << kCellHeight - 2 << "\" rx=\"3\" fill=\""
             << heatmap_color(value, row.display_scale) << "\"/>"
             << "<text x=\"" << x + kCellWidth / 2 << "\" y=\"" << y + 26
             << "\" class=\"value\">" << value << "</text></g>\n";
    }
  }

  const auto legend_y = kCellsTop + static_cast<int>(rows.size()) * kCellHeight + 30;
  constexpr auto kLegendSteps = 24;
  for (int index = 0; index < kLegendSteps; ++index) {
    const auto value = 2.0 * static_cast<double>(index) /
                       static_cast<double>(kLegendSteps - 1);
    output << "  <rect x=\"" << kLabelWidth + index * 18 << "\" y=\""
           << legend_y << "\" width=\"18\" height=\"12\" fill=\""
           << heatmap_color(value, 1.0) << "\"/>\n";
  }
  output << "  <text x=\"" << kLabelWidth << "\" y=\"" << legend_y + 29
         << "\" class=\"subtle\">0x gate</text>\n"
         << "  <text x=\"" << kLabelWidth + 216 << "\" y=\"" << legend_y + 29
         << "\" class=\"subtle\">1x gate</text>\n"
         << "  <text x=\"" << kLabelWidth + 408 << "\" y=\"" << legend_y + 29
         << "\" class=\"subtle\">2x gate</text>\n"
         << "</svg>\n";
  return output.good();
}

#if defined(PHOTO_EDITOR_HAS_COLORSYNC_ORACLE)
[[nodiscard]] std::vector<std::array<double, 3>> colorsync_outputs(
    const std::vector<Patch>& patches) {
  const auto source_space = CGColorSpaceCreateWithName(
      kCGColorSpaceExtendedLinearDisplayP3);
  const auto destination_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  if (source_space == nullptr || destination_space == nullptr) {
    if (source_space != nullptr) {
      CGColorSpaceRelease(source_space);
    }
    if (destination_space != nullptr) {
      CGColorSpaceRelease(destination_space);
    }
    return {};
  }

  std::vector<float> source_pixels(patches.size() * 4);
  std::vector<float> destination_pixels(patches.size() * 4);
  for (std::size_t index = 0; index < patches.size(); ++index) {
    source_pixels[index * 4] = static_cast<float>(patches[index].linear_display_p3.red);
    source_pixels[index * 4 + 1] = static_cast<float>(patches[index].linear_display_p3.green);
    source_pixels[index * 4 + 2] = static_cast<float>(patches[index].linear_display_p3.blue);
    source_pixels[index * 4 + 3] = 1.0F;
  }
  const auto row_bytes = patches.size() * 4 * sizeof(float);
  const auto bitmap_info = static_cast<CGBitmapInfo>(
      kCGBitmapByteOrder32Host | kCGBitmapFloatComponents |
      kCGImageAlphaPremultipliedLast);
  const auto provider = CGDataProviderCreateWithData(
      nullptr,
      source_pixels.data(),
      row_bytes,
      nullptr);
  const auto image = provider == nullptr
                         ? nullptr
                         : CGImageCreate(
                               patches.size(),
                               1,
                               32,
                               128,
                               row_bytes,
                               source_space,
                               bitmap_info,
                               provider,
                               nullptr,
                               false,
                               kCGRenderingIntentRelativeColorimetric);
  const auto context = CGBitmapContextCreate(
      destination_pixels.data(),
      patches.size(),
      1,
      32,
      row_bytes,
      destination_space,
      bitmap_info);
  if (context != nullptr && image != nullptr) {
    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextSetInterpolationQuality(context, kCGInterpolationNone);
    CGContextDrawImage(
        context,
        CGRectMake(0.0, 0.0, static_cast<CGFloat>(patches.size()), 1.0),
        image);
  }

  if (context != nullptr) {
    CGContextRelease(context);
  }
  if (image != nullptr) {
    CGImageRelease(image);
  }
  if (provider != nullptr) {
    CGDataProviderRelease(provider);
  }
  CGColorSpaceRelease(source_space);
  CGColorSpaceRelease(destination_space);
  if (context == nullptr || image == nullptr) {
    return {};
  }

  std::vector<std::array<double, 3>> outputs;
  outputs.reserve(patches.size());
  for (std::size_t index = 0; index < patches.size(); ++index) {
    outputs.push_back({
        destination_pixels[index * 4],
        destination_pixels[index * 4 + 1],
        destination_pixels[index * 4 + 2],
    });
  }
  return outputs;
}
#endif

}  // namespace

int main(int argc, char** argv) {
  std::string report_path;
  std::string heatmap_path;
  for (auto index = 1; index < argc; index += 2) {
    if (index + 1 >= argc) {
      return fail(
          "usage: color_cmm_comparison_test [--report output.json] "
          "[--heatmap output.svg]");
    }
    const auto option = std::string_view(argv[index]);
    if (option == "--report" && report_path.empty()) {
      report_path = argv[index + 1];
    } else if (option == "--heatmap" && heatmap_path.empty()) {
      heatmap_path = argv[index + 1];
    } else {
      return fail(
          "usage: color_cmm_comparison_test [--report output.json] "
          "[--heatmap output.svg]");
    }
  }
  const auto icc_capability = photo_editor::pipeline::icc_backend_capability();
  if (!icc_capability.available) {
    return 77;
  }
  const auto patches = load_patches();
  if (patches.size() < 20) {
    return fail("CMM comparison corpus should load all reference patches");
  }

  auto source_profile = create_linear_display_p3_profile();
  auto destination_profile = cmsCreate_sRGBProfile();
  if (source_profile == nullptr || destination_profile == nullptr) {
    if (source_profile != nullptr) {
      cmsCloseProfile(source_profile);
    }
    if (destination_profile != nullptr) {
      cmsCloseProfile(destination_profile);
    }
    return fail("Little CMS should create source and destination profiles");
  }
  const auto source_profile_bytes = serialize_profile(source_profile);
  const auto destination_profile_bytes = serialize_profile(destination_profile);
  const auto corpus_srgb_v2 = load_base64_profile("srgb-v2-2014.icc.base64");
  const auto corpus_display_p3_v4 = load_base64_profile("display-p3-v4.icc.base64");
  const auto corpus_cvd_lut_v4 = load_base64_profile("cvd-lut-deutan-v4.icc.base64");
  const auto corpus_srgb_v4 = load_base64_profile("srgb-v4-preference.icc.base64");
  const auto corpus_device_link_v4 =
      load_base64_profile("srgb-v2-to-display-p3-v4-device-link.icc.base64");
  if (corpus_srgb_v2.size() != 3024 || corpus_display_p3_v4.size() != 536 ||
      corpus_cvd_lut_v4.size() != 6568 || corpus_srgb_v4.size() != 60960 ||
      corpus_device_link_v4.size() != 31608 ||
      photo_editor::pipeline::color_payload_sha256(corpus_srgb_v2) !=
          "384b832de3412066743b52a75ee906b6fb9fb8d9e09e936fc2c43223815c6e0a" ||
      photo_editor::pipeline::color_payload_sha256(corpus_display_p3_v4) !=
          "20789fdbea9835251a4f0796c8bf45cbd964896044886540da21ffc7457af0ab" ||
      photo_editor::pipeline::color_payload_sha256(corpus_cvd_lut_v4) !=
          "47225761e99dff203636117ed647511a7cfb3a28288d7c27ba25e0480544e34d" ||
      photo_editor::pipeline::color_payload_sha256(corpus_srgb_v4) !=
          "83174717332326ddc198d9df188a4daec27b8979ba152cebbfc470c793d0bb11" ||
      photo_editor::pipeline::color_payload_sha256(corpus_device_link_v4) !=
          "d523312c11bfbb1d40e61a4eb51c15e6fecc78dbfe4bf65b2f33ecace31bd856") {
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("versioned ICC corpus payloads should match their immutable manifest identity");
  }
  auto corpus_v2_handle = cmsOpenProfileFromMem(
      corpus_srgb_v2.data(), static_cast<cmsUInt32Number>(corpus_srgb_v2.size()));
  auto corpus_p3_handle = cmsOpenProfileFromMem(
      corpus_display_p3_v4.data(),
      static_cast<cmsUInt32Number>(corpus_display_p3_v4.size()));
  auto corpus_lut_handle = cmsOpenProfileFromMem(
      corpus_cvd_lut_v4.data(),
      static_cast<cmsUInt32Number>(corpus_cvd_lut_v4.size()));
  auto corpus_srgb_v4_handle = cmsOpenProfileFromMem(
      corpus_srgb_v4.data(), static_cast<cmsUInt32Number>(corpus_srgb_v4.size()));
  auto corpus_device_link_handle = cmsOpenProfileFromMem(
      corpus_device_link_v4.data(),
      static_cast<cmsUInt32Number>(corpus_device_link_v4.size()));
  if (corpus_v2_handle == nullptr || corpus_p3_handle == nullptr ||
      corpus_lut_handle == nullptr || corpus_srgb_v4_handle == nullptr ||
      corpus_device_link_handle == nullptr ||
      cmsGetProfileVersion(corpus_v2_handle) < 2.0 ||
      cmsGetProfileVersion(corpus_v2_handle) >= 3.0 ||
      cmsGetProfileVersion(corpus_p3_handle) < 4.0 ||
      cmsGetProfileVersion(corpus_lut_handle) < 4.0 ||
      cmsGetProfileVersion(corpus_srgb_v4_handle) < 4.0 ||
      cmsGetProfileVersion(corpus_device_link_handle) < 4.0 ||
      cmsGetDeviceClass(corpus_v2_handle) != cmsSigDisplayClass ||
      cmsGetDeviceClass(corpus_p3_handle) != cmsSigDisplayClass ||
      cmsGetDeviceClass(corpus_lut_handle) != cmsSigInputClass ||
      cmsGetDeviceClass(corpus_srgb_v4_handle) != cmsSigColorSpaceClass ||
      cmsGetDeviceClass(corpus_device_link_handle) != cmsSigLinkClass ||
      !cmsIsTag(corpus_lut_handle, cmsSigAToB0Tag) ||
      !cmsIsTag(corpus_lut_handle, cmsSigAToB1Tag) ||
      !cmsIsTag(corpus_srgb_v4_handle, cmsSigAToB0Tag) ||
      !cmsIsTag(corpus_srgb_v4_handle, cmsSigBToA0Tag)) {
    if (corpus_v2_handle != nullptr) {
      cmsCloseProfile(corpus_v2_handle);
    }
    if (corpus_p3_handle != nullptr) {
      cmsCloseProfile(corpus_p3_handle);
    }
    if (corpus_lut_handle != nullptr) {
      cmsCloseProfile(corpus_lut_handle);
    }
    if (corpus_srgb_v4_handle != nullptr) {
      cmsCloseProfile(corpus_srgb_v4_handle);
    }
    if (corpus_device_link_handle != nullptr) {
      cmsCloseProfile(corpus_device_link_handle);
    }
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("ICC corpus should contain the declared v2, v4, matrix/TRC, and LUT paths");
  }
  constexpr auto kCorpusFlags = static_cast<cmsUInt32Number>(
      cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOCACHE);
  auto direct_corpus_transform = cmsCreateTransform(
      corpus_lut_handle,
      TYPE_RGBA_FLT,
      corpus_p3_handle,
      TYPE_RGBA_FLT,
      INTENT_RELATIVE_COLORIMETRIC,
      kCorpusFlags);
  auto direct_b2a_transform = cmsCreateTransform(
      corpus_p3_handle,
      TYPE_RGBA_FLT,
      corpus_srgb_v4_handle,
      TYPE_RGBA_FLT,
      INTENT_PERCEPTUAL,
      kCorpusFlags);
  auto direct_proof_transform = cmsCreateProofingTransform(
      corpus_v2_handle,
      TYPE_RGBA_FLT,
      corpus_srgb_v4_handle,
      TYPE_RGBA_FLT,
      corpus_p3_handle,
      INTENT_RELATIVE_COLORIMETRIC,
      INTENT_ABSOLUTE_COLORIMETRIC,
      static_cast<cmsUInt32Number>(kCorpusFlags | cmsFLAGS_SOFTPROOFING));
  cmsCloseProfile(corpus_v2_handle);
  cmsCloseProfile(corpus_p3_handle);
  cmsCloseProfile(corpus_lut_handle);
  cmsCloseProfile(corpus_srgb_v4_handle);
  cmsCloseProfile(corpus_device_link_handle);
  auto engine_corpus_transform = photo_editor::pipeline::compile_icc_transform(
      corpus_cvd_lut_v4,
      corpus_display_p3_v4,
      photo_editor::pipeline::IccTransformOptions{
          .rendering_intent = photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
          .black_point_compensation = true,
          .target = "checked-in-v4-lut-corpus",
      });
  auto engine_v2_to_v4_transform = photo_editor::pipeline::compile_icc_transform(
      corpus_srgb_v2,
      corpus_display_p3_v4,
      photo_editor::pipeline::IccTransformOptions{
          .rendering_intent = photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
          .black_point_compensation = true,
          .target = "checked-in-v2-to-v4-corpus",
      });
  auto engine_b2a_transform = photo_editor::pipeline::compile_icc_transform(
      corpus_display_p3_v4,
      corpus_srgb_v4,
      photo_editor::pipeline::IccTransformOptions{
          .rendering_intent = photo_editor::pipeline::IccRenderingIntent::Perceptual,
          .black_point_compensation = true,
          .target = "checked-in-v4-b2a-corpus",
      });
  auto engine_proof_transform =
      photo_editor::pipeline::compile_icc_proofing_transform(
          corpus_srgb_v2,
          corpus_display_p3_v4,
          corpus_srgb_v4,
          photo_editor::pipeline::IccProofTransformOptions{
              .rendering_intent =
                  photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
              .proofing_intent =
                  photo_editor::pipeline::IccRenderingIntent::AbsoluteColorimetric,
              .black_point_compensation = true,
              .gamut_check = false,
              .target = "checked-in-soft-proof-corpus",
          });
  photo_editor::pipeline::clear_icc_transform_lut_cache_for_testing();
  const auto final_lut_settings =
      photo_editor::pipeline::icc_transform_lut_settings(
          photo_editor::pipeline::IccTransformLutQuality::Final);
  const auto engine_corpus_lut =
      photo_editor::pipeline::compile_icc_transform_lut(
          engine_corpus_transform,
          final_lut_settings);
  const auto engine_b2a_lut =
      photo_editor::pipeline::compile_icc_transform_lut(
          engine_b2a_transform,
          final_lut_settings);
  const auto engine_proof_lut =
      photo_editor::pipeline::compile_icc_transform_lut(
          engine_proof_transform,
          final_lut_settings);
  if (engine_corpus_lut == nullptr || engine_b2a_lut == nullptr ||
      engine_proof_lut == nullptr) {
    if (direct_corpus_transform != nullptr) {
      cmsDeleteTransform(direct_corpus_transform);
    }
    if (direct_b2a_transform != nullptr) {
      cmsDeleteTransform(direct_b2a_transform);
    }
    if (direct_proof_transform != nullptr) {
      cmsDeleteTransform(direct_proof_transform);
    }
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    std::cerr << "ICC LUT corpus compile failure: "
              << photo_editor::pipeline::icc_transform_lut_cache_stats()
                     .last_failure_reason
              << '\n';
    return fail("matrix, LUT-based, B2A, and proof ICC processors should compile measured final LUTs");
  }
  for (const auto intent : {
           photo_editor::pipeline::IccRenderingIntent::Perceptual,
           photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
           photo_editor::pipeline::IccRenderingIntent::Saturation,
           photo_editor::pipeline::IccRenderingIntent::AbsoluteColorimetric,
       }) {
    for (const auto black_point_compensation : {false, true}) {
      const auto intent_transform = photo_editor::pipeline::compile_icc_transform(
          corpus_srgb_v2,
          corpus_display_p3_v4,
          photo_editor::pipeline::IccTransformOptions{
              .rendering_intent = intent,
              .black_point_compensation = black_point_compensation,
              .target = "checked-in-intent-and-bpc-corpus",
          });
      if (!intent_transform.ready()) {
        if (direct_corpus_transform != nullptr) {
          cmsDeleteTransform(direct_corpus_transform);
        }
        if (direct_b2a_transform != nullptr) {
          cmsDeleteTransform(direct_b2a_transform);
        }
        if (direct_proof_transform != nullptr) {
          cmsDeleteTransform(direct_proof_transform);
        }
        cmsCloseProfile(source_profile);
        cmsCloseProfile(destination_profile);
        return fail("ICC corpus should compile every rendering-intent and BPC combination");
      }
    }
  }
  std::array<photo_editor::pipeline::IccRgbaFloat, 4> corpus_pixels = {
      photo_editor::pipeline::IccRgbaFloat{
          .red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.25F},
      photo_editor::pipeline::IccRgbaFloat{
          .red = 0.25F, .green = 0.5F, .blue = 0.75F, .alpha = 0.5F},
      photo_editor::pipeline::IccRgbaFloat{
          .red = 0.9F, .green = 0.2F, .blue = 0.1F, .alpha = 0.75F},
      photo_editor::pipeline::IccRgbaFloat{
          .red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = 1.0F},
  };
  auto direct_corpus_pixels = corpus_pixels;
  auto b2a_pixels = corpus_pixels;
  auto direct_b2a_pixels = corpus_pixels;
  auto proof_pixels = corpus_pixels;
  auto direct_proof_pixels = corpus_pixels;
  if (direct_corpus_transform == nullptr || direct_b2a_transform == nullptr ||
      direct_proof_transform == nullptr ||
      !engine_corpus_transform.ready() || !engine_b2a_transform.ready() ||
      !engine_proof_transform.ready() ||
      !engine_v2_to_v4_transform.ready() ||
      !engine_corpus_transform.apply(corpus_pixels) ||
      !engine_b2a_transform.apply(b2a_pixels) ||
      !engine_proof_transform.apply(proof_pixels)) {
    if (direct_corpus_transform != nullptr) {
      cmsDeleteTransform(direct_corpus_transform);
    }
    if (direct_b2a_transform != nullptr) {
      cmsDeleteTransform(direct_b2a_transform);
    }
    if (direct_proof_transform != nullptr) {
      cmsDeleteTransform(direct_proof_transform);
    }
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("checked-in ICC v4 LUT path should compile in the engine and oracle");
  }
  cmsDoTransform(
      direct_corpus_transform,
      direct_corpus_pixels.data(),
      direct_corpus_pixels.data(),
      static_cast<cmsUInt32Number>(direct_corpus_pixels.size()));
  cmsDoTransform(
      direct_b2a_transform,
      direct_b2a_pixels.data(),
      direct_b2a_pixels.data(),
      static_cast<cmsUInt32Number>(direct_b2a_pixels.size()));
  cmsDoTransform(
      direct_proof_transform,
      direct_proof_pixels.data(),
      direct_proof_pixels.data(),
      static_cast<cmsUInt32Number>(direct_proof_pixels.size()));
  cmsDeleteTransform(direct_corpus_transform);
  cmsDeleteTransform(direct_b2a_transform);
  cmsDeleteTransform(direct_proof_transform);
  auto max_corpus_channel_error = 0.0;
  auto max_corpus_alpha_error = 0.0;
  for (std::size_t index = 0; index < corpus_pixels.size(); ++index) {
    max_corpus_channel_error = std::max({
        max_corpus_channel_error,
        std::abs(static_cast<double>(corpus_pixels[index].red -
                                     direct_corpus_pixels[index].red)),
        std::abs(static_cast<double>(corpus_pixels[index].green -
                                     direct_corpus_pixels[index].green)),
        std::abs(static_cast<double>(corpus_pixels[index].blue -
                                     direct_corpus_pixels[index].blue)),
    });
    max_corpus_alpha_error = std::max(
        max_corpus_alpha_error,
        std::abs(static_cast<double>(corpus_pixels[index].alpha -
                                     direct_corpus_pixels[index].alpha)));
    max_corpus_channel_error = std::max({
        max_corpus_channel_error,
        std::abs(static_cast<double>(b2a_pixels[index].red -
                                     direct_b2a_pixels[index].red)),
        std::abs(static_cast<double>(b2a_pixels[index].green -
                                     direct_b2a_pixels[index].green)),
        std::abs(static_cast<double>(b2a_pixels[index].blue -
                                     direct_b2a_pixels[index].blue)),
    });
    max_corpus_alpha_error = std::max(
        max_corpus_alpha_error,
        std::abs(static_cast<double>(b2a_pixels[index].alpha -
                                     direct_b2a_pixels[index].alpha)));
    max_corpus_channel_error = std::max({
        max_corpus_channel_error,
        std::abs(static_cast<double>(proof_pixels[index].red -
                                     direct_proof_pixels[index].red)),
        std::abs(static_cast<double>(proof_pixels[index].green -
                                     direct_proof_pixels[index].green)),
        std::abs(static_cast<double>(proof_pixels[index].blue -
                                     direct_proof_pixels[index].blue)),
    });
    max_corpus_alpha_error = std::max(
        max_corpus_alpha_error,
        std::abs(static_cast<double>(proof_pixels[index].alpha -
                                     direct_proof_pixels[index].alpha)));
  }
  const auto& proof_description = engine_proof_transform.description();
  const auto cached_proof_transform =
      photo_editor::pipeline::compile_icc_proofing_transform(
          corpus_srgb_v2,
          corpus_display_p3_v4,
          corpus_srgb_v4,
          photo_editor::pipeline::IccProofTransformOptions{
              .rendering_intent =
                  photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
              .proofing_intent =
                  photo_editor::pipeline::IccRenderingIntent::AbsoluteColorimetric,
              .black_point_compensation = true,
              .gamut_check = false,
              .target = "checked-in-soft-proof-corpus",
          });
  const auto gamut_proof_transform =
      photo_editor::pipeline::compile_icc_proofing_transform(
          corpus_srgb_v2,
          corpus_display_p3_v4,
          corpus_srgb_v4,
          photo_editor::pipeline::IccProofTransformOptions{
              .rendering_intent =
                  photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
              .proofing_intent =
                  photo_editor::pipeline::IccRenderingIntent::AbsoluteColorimetric,
              .black_point_compensation = true,
              .gamut_check = true,
              .target = "checked-in-soft-proof-corpus",
          });
  const auto gamut_proof_lut =
      photo_editor::pipeline::compile_icc_transform_lut(
          gamut_proof_transform,
          final_lut_settings);
  const auto invalid_proof_transform =
      photo_editor::pipeline::compile_icc_proofing_transform(
          corpus_srgb_v2,
          {},
          corpus_srgb_v4);
  if (max_corpus_channel_error > 1.0e-6 || max_corpus_alpha_error > 1.0e-7 ||
      !proof_description.soft_proofing || proof_description.gamut_check ||
      proof_description.proof_profile_sha256.empty() ||
      proof_description.proof_profile_description.empty() ||
      proof_description.proofing_intent !=
          photo_editor::pipeline::IccRenderingIntent::AbsoluteColorimetric ||
      !cached_proof_transform.ready() || !gamut_proof_transform.ready() ||
      gamut_proof_lut != nullptr ||
      cached_proof_transform.description().cache_key != proof_description.cache_key ||
      gamut_proof_transform.description().cache_key == proof_description.cache_key ||
      !gamut_proof_transform.description().gamut_check ||
      invalid_proof_transform.ready() ||
      invalid_proof_transform.description().status !=
          photo_editor::pipeline::IccTransformStatus::InvalidProofProfile) {
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("checked-in ICC LUT execution should match direct Little CMS and preserve alpha");
  }

  const auto gamut_mask_transform =
      photo_editor::pipeline::compile_icc_proofing_transform(
          corpus_display_p3_v4,
          corpus_srgb_v2,
          corpus_srgb_v4,
          photo_editor::pipeline::IccProofTransformOptions{
              .rendering_intent =
                  photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
              .proofing_intent =
                  photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
              .black_point_compensation = true,
              .gamut_check = true,
              .target = "checked-in-gamut-mask-corpus",
          });
  const std::array<photo_editor::pipeline::IccRgbaFloat, 6> gamut_source_pixels = {
      photo_editor::pipeline::IccRgbaFloat{.red = 0.0F, .green = 0.0F, .blue = 0.0F},
      photo_editor::pipeline::IccRgbaFloat{.red = 0.5F, .green = 0.5F, .blue = 0.5F},
      photo_editor::pipeline::IccRgbaFloat{.red = 1.0F, .green = 0.0F, .blue = 0.0F},
      photo_editor::pipeline::IccRgbaFloat{.red = 0.0F, .green = 1.0F, .blue = 0.0F},
      photo_editor::pipeline::IccRgbaFloat{.red = 0.0F, .green = 0.0F, .blue = 1.0F},
      photo_editor::pipeline::IccRgbaFloat{.red = 1.0F, .green = 1.0F, .blue = 1.0F},
  };
  const auto engine_gamut_mask = gamut_mask_transform.gamut_mask(gamut_source_pixels);
  auto direct_gamut_pixels = gamut_source_pixels;
  auto direct_gamut_context = cmsCreateContext(nullptr, nullptr);
  std::array<cmsUInt16Number, cmsMAXCHANNELS> direct_alarm_codes{};
  direct_alarm_codes[0] = 0xf123U;
  direct_alarm_codes[1] = 0x0e45U;
  direct_alarm_codes[2] = 0xd678U;
  if (direct_gamut_context != nullptr) {
    cmsSetAlarmCodesTHR(direct_gamut_context, direct_alarm_codes.data());
  }
  auto direct_gamut_source = direct_gamut_context == nullptr
                                 ? nullptr
                                 : cmsOpenProfileFromMemTHR(
                                       direct_gamut_context,
                                       corpus_display_p3_v4.data(),
                                       static_cast<cmsUInt32Number>(corpus_display_p3_v4.size()));
  auto direct_gamut_proof = direct_gamut_context == nullptr
                                ? nullptr
                                : cmsOpenProfileFromMemTHR(
                                      direct_gamut_context,
                                      corpus_srgb_v2.data(),
                                      static_cast<cmsUInt32Number>(corpus_srgb_v2.size()));
  auto direct_gamut_destination = direct_gamut_context == nullptr
                                      ? nullptr
                                      : cmsOpenProfileFromMemTHR(
                                            direct_gamut_context,
                                            corpus_srgb_v4.data(),
                                            static_cast<cmsUInt32Number>(corpus_srgb_v4.size()));
  auto direct_gamut_transform =
      direct_gamut_source == nullptr || direct_gamut_proof == nullptr ||
              direct_gamut_destination == nullptr
          ? nullptr
          : cmsCreateProofingTransformTHR(
                direct_gamut_context,
                direct_gamut_source,
                TYPE_RGBA_FLT,
                direct_gamut_destination,
                TYPE_RGBA_FLT,
                direct_gamut_proof,
                INTENT_RELATIVE_COLORIMETRIC,
                INTENT_RELATIVE_COLORIMETRIC,
                static_cast<cmsUInt32Number>(
                    kCorpusFlags | cmsFLAGS_SOFTPROOFING | cmsFLAGS_GAMUTCHECK));
  if (direct_gamut_transform != nullptr) {
    cmsDoTransform(
        direct_gamut_transform,
        direct_gamut_pixels.data(),
        direct_gamut_pixels.data(),
        static_cast<cmsUInt32Number>(direct_gamut_pixels.size()));
  }
  std::vector<std::uint8_t> direct_gamut_mask(direct_gamut_pixels.size(), 0U);
  constexpr auto kDirectAlarmTolerance = 2.0F / 65535.0F;
  for (std::size_t index = 0; index < direct_gamut_pixels.size(); ++index) {
    const auto& pixel = direct_gamut_pixels[index];
    direct_gamut_mask[index] =
        std::abs(pixel.red - static_cast<float>(direct_alarm_codes[0]) / 65535.0F) <=
                    kDirectAlarmTolerance &&
                std::abs(pixel.green - static_cast<float>(direct_alarm_codes[1]) / 65535.0F) <=
                    kDirectAlarmTolerance &&
                std::abs(pixel.blue - static_cast<float>(direct_alarm_codes[2]) / 65535.0F) <=
                    kDirectAlarmTolerance
            ? 255U
            : 0U;
  }
  if (direct_gamut_transform != nullptr) {
    cmsDeleteTransform(direct_gamut_transform);
  }
  if (direct_gamut_source != nullptr) {
    cmsCloseProfile(direct_gamut_source);
  }
  if (direct_gamut_proof != nullptr) {
    cmsCloseProfile(direct_gamut_proof);
  }
  if (direct_gamut_destination != nullptr) {
    cmsCloseProfile(direct_gamut_destination);
  }
  if (direct_gamut_context != nullptr) {
    cmsDeleteContext(direct_gamut_context);
  }
  const auto warned_count = static_cast<std::size_t>(std::count(
      engine_gamut_mask.begin(), engine_gamut_mask.end(), std::uint8_t{255U}));
  if (!gamut_mask_transform.ready() || direct_gamut_transform == nullptr ||
      engine_gamut_mask != direct_gamut_mask || warned_count == 0U ||
      warned_count == engine_gamut_mask.size()) {
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("proof gamut mask should match an independent context-local Little CMS oracle");
  }

  const auto compiled_device_link = photo_editor::pipeline::compile_icc_device_link(
      corpus_device_link_v4,
      photo_editor::pipeline::IccTransformOptions{
          .target = "device-link-corpus",
      });
  const auto compiled_device_link_lut =
      photo_editor::pipeline::compile_icc_transform_lut(
          compiled_device_link,
          final_lut_settings);
  const auto misused_device_link = photo_editor::pipeline::compile_icc_transform(
      corpus_device_link_v4,
      destination_profile_bytes,
      photo_editor::pipeline::IccTransformOptions{
          .target = "device-link-wrong-api-corpus",
      });
  std::array<photo_editor::pipeline::IccRgbaFloat, 2> device_link_pixels = {
      photo_editor::pipeline::IccRgbaFloat{
          .red = 0.25F, .green = 0.5F, .blue = 0.75F, .alpha = 0.375F},
      photo_editor::pipeline::IccRgbaFloat{
          .red = 0.9F, .green = 0.1F, .blue = 0.4F, .alpha = 0.625F},
  };
  auto direct_device_link_pixels = device_link_pixels;
  auto direct_device_link_profile = cmsOpenProfileFromMem(
      corpus_device_link_v4.data(),
      static_cast<cmsUInt32Number>(corpus_device_link_v4.size()));
  std::array<cmsHPROFILE, 1> direct_device_link_profiles = {direct_device_link_profile};
  auto direct_device_link_transform = direct_device_link_profile == nullptr
                                          ? nullptr
                                          : cmsCreateMultiprofileTransform(
                                                direct_device_link_profiles.data(),
                                                static_cast<cmsUInt32Number>(
                                                    direct_device_link_profiles.size()),
                                                TYPE_RGBA_FLT,
                                                TYPE_RGBA_FLT,
                                                INTENT_PERCEPTUAL,
                                                kCorpusFlags);
  if (direct_device_link_profile != nullptr) {
    cmsCloseProfile(direct_device_link_profile);
  }
  if (corpus_device_link_v4.empty() || direct_device_link_transform == nullptr ||
      !compiled_device_link.ready() ||
      !compiled_device_link.description().device_link ||
      !compiled_device_link.description().rendering_intent_baked ||
      compiled_device_link_lut != nullptr ||
      misused_device_link.ready() ||
      misused_device_link.description().status !=
          photo_editor::pipeline::IccTransformStatus::UnsupportedSourceProfile ||
      misused_device_link.description().failure_reason !=
          "device_link_requires_dedicated_compile_api" ||
      compiled_device_link.description().destination_profile_description !=
          "embedded device-link output" ||
      !compiled_device_link.apply(device_link_pixels)) {
    if (direct_device_link_transform != nullptr) {
      cmsDeleteTransform(direct_device_link_transform);
    }
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("RGB DeviceLink profiles should compile only through the dedicated API");
  }
  cmsDoTransform(
      direct_device_link_transform,
      direct_device_link_pixels.data(),
      direct_device_link_pixels.data(),
      static_cast<cmsUInt32Number>(direct_device_link_pixels.size()));
  cmsDeleteTransform(direct_device_link_transform);
  auto max_device_link_channel_error = 0.0F;
  for (std::size_t index = 0; index < device_link_pixels.size(); ++index) {
    max_device_link_channel_error = std::max({
        max_device_link_channel_error,
        std::abs(device_link_pixels[index].red - direct_device_link_pixels[index].red),
        std::abs(device_link_pixels[index].green - direct_device_link_pixels[index].green),
        std::abs(device_link_pixels[index].blue - direct_device_link_pixels[index].blue),
    });
  }
  if (max_device_link_channel_error > 1.0e-6F ||
      std::abs(device_link_pixels[0].alpha - 0.375F) > 1.0e-7F ||
      std::abs(device_link_pixels[1].alpha - 0.625F) > 1.0e-7F) {
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("RGB DeviceLink execution should match direct Little CMS and preserve alpha");
  }
  photo_editor::pipeline::clear_icc_transform_cache();
  const auto preview_options = photo_editor::pipeline::IccTransformOptions{
      .rendering_intent = photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
      .black_point_compensation = true,
      .target = "preview",
  };
  const auto first_cached_transform = photo_editor::pipeline::compile_icc_transform(
      source_profile_bytes,
      destination_profile_bytes,
      preview_options);
  const auto second_cached_transform = photo_editor::pipeline::compile_icc_transform(
      source_profile_bytes,
      destination_profile_bytes,
      preview_options);
  const auto export_cached_transform = photo_editor::pipeline::compile_icc_transform(
      source_profile_bytes,
      destination_profile_bytes,
      photo_editor::pipeline::IccTransformOptions{
          .rendering_intent = photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
          .black_point_compensation = true,
          .target = "export",
      });
  const auto initial_cache_stats = photo_editor::pipeline::icc_transform_cache_stats();
  if (!first_cached_transform.ready() || !second_cached_transform.ready() ||
      !export_cached_transform.ready() || initial_cache_stats.hits != 1 ||
      initial_cache_stats.misses != 2 || initial_cache_stats.entries != 2 ||
      first_cached_transform.description().cache_key !=
          second_cached_transform.description().cache_key ||
      first_cached_transform.description().cache_key ==
          export_cached_transform.description().cache_key) {
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("ICC transform cache should key immutable processors by profiles and target");
  }

  for (int index = 0; index < 18; ++index) {
    const auto transform = photo_editor::pipeline::compile_icc_transform(
        source_profile_bytes,
        destination_profile_bytes,
        photo_editor::pipeline::IccTransformOptions{
            .rendering_intent = photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
            .black_point_compensation = true,
            .target = "bounded-cache-" + std::to_string(index),
        });
    if (!transform.ready()) {
      cmsCloseProfile(source_profile);
      cmsCloseProfile(destination_profile);
      return fail("every bounded-cache transform should compile");
    }
  }
  const auto bounded_cache_stats = photo_editor::pipeline::icc_transform_cache_stats();
  if (bounded_cache_stats.entries != bounded_cache_stats.capacity ||
      bounded_cache_stats.capacity != 16 || bounded_cache_stats.evictions < 4) {
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("ICC transform cache should enforce its fixed LRU capacity");
  }

  auto engine_icc_transform = photo_editor::pipeline::compile_icc_transform(
      source_profile_bytes,
      destination_profile_bytes,
      photo_editor::pipeline::IccTransformOptions{
          .rendering_intent = photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
          .black_point_compensation = true,
          .target = "comparison",
      });
  if (!engine_icc_transform.ready()) {
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    std::cerr << "engine ICC transform should compile: "
              << engine_icc_transform.description().failure_reason << '\n';
    return EXIT_FAILURE;
  }
  std::atomic_bool concurrent_apply_ok{true};
  std::vector<std::thread> transform_threads;
  for (int thread_index = 0; thread_index < 4; ++thread_index) {
    transform_threads.emplace_back([engine_icc_transform, &concurrent_apply_ok, thread_index]() mutable {
      std::array<photo_editor::pipeline::IccRgbaFloat, 8> pixels{};
      for (std::size_t pixel_index = 0; pixel_index < pixels.size(); ++pixel_index) {
        pixels[pixel_index] = {
            .red = static_cast<float>(pixel_index + 1) / 9.0F,
            .green = static_cast<float>(thread_index + 1) / 5.0F,
            .blue = 0.35F,
            .alpha = 0.42F,
        };
      }
      if (!engine_icc_transform.apply(pixels)) {
        concurrent_apply_ok.store(false);
        return;
      }
      for (const auto& pixel : pixels) {
        if (!std::isfinite(pixel.red) || !std::isfinite(pixel.green) ||
            !std::isfinite(pixel.blue) || std::abs(pixel.alpha - 0.42F) > 1.0e-7F) {
          concurrent_apply_ok.store(false);
        }
      }
    });
  }
  for (auto& thread : transform_threads) {
    thread.join();
  }
  if (!concurrent_apply_ok.load()) {
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("cached ICC transforms should be safe for concurrent callers and preserve alpha");
  }

  const std::array<std::byte, 16> malformed_profile{};
  const auto malformed_transform = photo_editor::pipeline::compile_icc_transform(
      malformed_profile,
      destination_profile_bytes,
      photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
      true);
  if (malformed_transform.ready() ||
      malformed_transform.description().status !=
          photo_editor::pipeline::IccTransformStatus::InvalidSourceProfile) {
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("engine ICC boundary should reject malformed source profiles explicitly");
  }
  const auto malformed_destination_transform =
      photo_editor::pipeline::compile_icc_transform(
          source_profile_bytes,
          malformed_profile,
          photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
          true);
  if (malformed_destination_transform.ready() ||
      malformed_destination_transform.description().status !=
          photo_editor::pipeline::IccTransformStatus::InvalidDestinationProfile) {
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("engine ICC boundary should reject malformed destination profiles explicitly");
  }
  auto incomplete_profile = cmsCreateProfilePlaceholder(nullptr);
  if (incomplete_profile != nullptr) {
    cmsSetProfileVersion(incomplete_profile, 4.4);
    cmsSetDeviceClass(incomplete_profile, cmsSigInputClass);
    cmsSetColorSpace(incomplete_profile, cmsSigRgbData);
    cmsSetPCS(incomplete_profile, cmsSigXYZData);
    cmsWriteTag(incomplete_profile, cmsSigMediaWhitePointTag, cmsD50_XYZ());
  }
  const auto incomplete_profile_bytes = serialize_profile(incomplete_profile);
  if (incomplete_profile != nullptr) {
    cmsCloseProfile(incomplete_profile);
  }
  const auto incomplete_transform = photo_editor::pipeline::compile_icc_transform(
      incomplete_profile_bytes,
      destination_profile_bytes,
      photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
      true);
  if (incomplete_profile_bytes.empty() || incomplete_transform.ready() ||
      incomplete_transform.description().status !=
          photo_editor::pipeline::IccTransformStatus::CompileFailed ||
      incomplete_transform.description().failure_reason !=
          "little_cms_transform_compile_failed") {
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("valid ICC containers without an executable pipeline should fail explicitly");
  }
  std::vector<std::byte> excessive_profile(16U * 1024U * 1024U + 1U);
  const auto excessive_transform = photo_editor::pipeline::compile_icc_transform(
      excessive_profile,
      destination_profile_bytes,
      photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
      true);
  if (excessive_transform.ready() ||
      excessive_transform.description().status !=
          photo_editor::pipeline::IccTransformStatus::ProfilePayloadTooLarge) {
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("engine ICC boundary should reject excessive profile payloads before parsing");
  }
  excessive_profile.clear();
  excessive_profile.shrink_to_fit();

  auto xyz_profile = cmsCreateXYZProfile();
  const auto xyz_profile_bytes = serialize_profile(xyz_profile);
  if (xyz_profile != nullptr) {
    cmsCloseProfile(xyz_profile);
  }
  const auto unsupported_transform = photo_editor::pipeline::compile_icc_transform(
      xyz_profile_bytes,
      destination_profile_bytes,
      photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
      true);
  if (unsupported_transform.ready() ||
      unsupported_transform.description().status !=
          photo_editor::pipeline::IccTransformStatus::UnsupportedSourceProfile) {
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("engine ICC boundary should reject unsupported non-RGB profiles explicitly");
  }
  const auto unsupported_destination_transform =
      photo_editor::pipeline::compile_icc_transform(
          source_profile_bytes,
          xyz_profile_bytes,
          photo_editor::pipeline::IccRenderingIntent::RelativeColorimetric,
          true);
  if (unsupported_destination_transform.ready() ||
      unsupported_destination_transform.description().status !=
          photo_editor::pipeline::IccTransformStatus::UnsupportedDestinationProfile) {
    cmsCloseProfile(source_profile);
    cmsCloseProfile(destination_profile);
    return fail("engine ICC boundary should reject unsupported destinations explicitly");
  }

  constexpr auto kFlags =
      static_cast<cmsUInt32Number>(cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE);
  auto relative_transform = cmsCreateTransform(
      source_profile,
      TYPE_RGB_DBL,
      destination_profile,
      TYPE_RGB_DBL,
      INTENT_RELATIVE_COLORIMETRIC,
      kFlags);
  auto perceptual_transform = cmsCreateTransform(
      source_profile,
      TYPE_RGB_DBL,
      destination_profile,
      TYPE_RGB_DBL,
      INTENT_PERCEPTUAL,
      kFlags);
  cmsCloseProfile(source_profile);
  cmsCloseProfile(destination_profile);
  if (relative_transform == nullptr || perceptual_transform == nullptr) {
    if (relative_transform != nullptr) {
      cmsDeleteTransform(relative_transform);
    }
    if (perceptual_transform != nullptr) {
      cmsDeleteTransform(perceptual_transform);
    }
    return fail("Little CMS should compile relative and perceptual transforms");
  }

  std::vector<double> relative_errors;
  std::vector<double> perceptual_errors;
  std::vector<double> engine_icc_errors;
  engine_icc_errors.reserve(patches.size());
#if defined(PHOTO_EDITOR_HAS_QT_COLOR_ORACLE)
  const QColorSpace qt_source(
      QColorSpace::Primaries::DciP3D65,
      QColorSpace::TransferFunction::Linear);
  const QColorSpace qt_destination(QColorSpace::NamedColorSpace::SRgb);
  const auto qt_transform = qt_source.transformationToColorSpace(qt_destination);
  if (!qt_source.isValid() || !qt_destination.isValid() || qt_transform.isIdentity()) {
    cmsDeleteTransform(relative_transform);
    cmsDeleteTransform(perceptual_transform);
    return fail("Qt should compile a non-identity Linear Display P3 to sRGB transform");
  }
  std::vector<double> qt_errors;
  qt_errors.reserve(patches.size());
#endif
#if defined(PHOTO_EDITOR_HAS_COLORSYNC_ORACLE)
  const auto colorsync_encoded_outputs = colorsync_outputs(patches);
  if (colorsync_encoded_outputs.size() != patches.size()) {
    cmsDeleteTransform(relative_transform);
    cmsDeleteTransform(perceptual_transform);
    return fail("ColorSync should transform every comparison patch");
  }
  std::vector<double> colorsync_errors;
  colorsync_errors.reserve(patches.size());
#endif
  std::vector<PatchResult> patch_results;
  relative_errors.reserve(patches.size());
  perceptual_errors.reserve(patches.size());
  patch_results.reserve(patches.size());
  auto max_alpha_error = 0.0;
  auto max_neutral_drift = 0.0;
  const auto comparison_started = std::chrono::steady_clock::now();
#if defined(PHOTO_EDITOR_HAS_COLORSYNC_ORACLE)
  std::size_t patch_index = 0;
#endif
  for (const auto& patch : patches) {
    const std::array<double, 3> input = {
        patch.linear_display_p3.red,
        patch.linear_display_p3.green,
        patch.linear_display_p3.blue,
    };
    std::array<double, 3> lcms_relative{};
    std::array<double, 3> lcms_perceptual{};
    cmsDoTransform(relative_transform, input.data(), lcms_relative.data(), 1);
    cmsDoTransform(perceptual_transform, input.data(), lcms_perceptual.data(), 1);

    const auto engine_relative = engine_output(
        patch,
        photo_editor::pipeline::RenderIntent::RelativeColorimetric);
    const auto engine_perceptual = engine_output(
        patch,
        photo_editor::pipeline::RenderIntent::Perceptual);
    const auto relative_delta = photo_editor::pipeline::delta_e_2000(
        linear_srgb_to_lab(engine_relative.rgb),
        linear_srgb_to_lab(decode_srgb(lcms_relative)));
    const auto perceptual_delta = photo_editor::pipeline::delta_e_2000(
        linear_srgb_to_lab(engine_perceptual.rgb),
        linear_srgb_to_lab(decode_srgb(lcms_perceptual)));
    auto engine_icc_pixel = photo_editor::pipeline::IccRgbaFloat{
        .red = static_cast<float>(patch.linear_display_p3.red),
        .green = static_cast<float>(patch.linear_display_p3.green),
        .blue = static_cast<float>(patch.linear_display_p3.blue),
        .alpha = static_cast<float>(patch.alpha),
    };
    if (!engine_icc_transform.apply(std::span(&engine_icc_pixel, 1))) {
      cmsDeleteTransform(relative_transform);
      cmsDeleteTransform(perceptual_transform);
      return fail("compiled engine ICC transform should process each patch");
    }
    const std::array<double, 3> engine_icc_encoded = {
        engine_icc_pixel.red,
        engine_icc_pixel.green,
        engine_icc_pixel.blue,
    };
    const auto engine_icc_delta = photo_editor::pipeline::delta_e_2000(
        linear_srgb_to_lab(decode_srgb(engine_icc_encoded)),
        linear_srgb_to_lab(decode_srgb(lcms_relative)));
#if defined(PHOTO_EDITOR_HAS_QT_COLOR_ORACLE)
    const auto qt_encoded = qt_transform.map(QRgbaFloat32{
        static_cast<float>(patch.linear_display_p3.red),
        static_cast<float>(patch.linear_display_p3.green),
        static_cast<float>(patch.linear_display_p3.blue),
        static_cast<float>(patch.alpha),
    });
    const std::array<double, 3> qt_encoded_rgb = {
        qt_encoded.red(),
        qt_encoded.green(),
        qt_encoded.blue(),
    };
    const auto qt_delta = photo_editor::pipeline::delta_e_2000(
        linear_srgb_to_lab(engine_relative.rgb),
        linear_srgb_to_lab(decode_srgb(qt_encoded_rgb)));
#else
    constexpr auto qt_delta = 0.0;
#endif
#if defined(PHOTO_EDITOR_HAS_COLORSYNC_ORACLE)
    const auto colorsync_delta = photo_editor::pipeline::delta_e_2000(
        linear_srgb_to_lab(engine_relative.rgb),
        linear_srgb_to_lab(decode_srgb(colorsync_encoded_outputs[patch_index])));
#else
    constexpr auto colorsync_delta = 0.0;
#endif
    if (!std::isfinite(relative_delta) || !std::isfinite(perceptual_delta)) {
      cmsDeleteTransform(relative_transform);
      cmsDeleteTransform(perceptual_transform);
      return fail("CMM comparisons should remain finite for every patch");
    }
    relative_errors.push_back(relative_delta);
    perceptual_errors.push_back(perceptual_delta);
    engine_icc_errors.push_back(engine_icc_delta);
#if defined(PHOTO_EDITOR_HAS_QT_COLOR_ORACLE)
    if (!std::isfinite(qt_delta)) {
      cmsDeleteTransform(relative_transform);
      cmsDeleteTransform(perceptual_transform);
      return fail("Qt CMM comparisons should remain finite for every patch");
    }
    qt_errors.push_back(qt_delta);
#endif
#if defined(PHOTO_EDITOR_HAS_COLORSYNC_ORACLE)
    if (!std::isfinite(colorsync_delta)) {
      cmsDeleteTransform(relative_transform);
      cmsDeleteTransform(perceptual_transform);
      return fail("ColorSync comparisons should remain finite for every patch");
    }
    colorsync_errors.push_back(colorsync_delta);
#endif
    const auto alpha_error = std::abs(engine_relative.alpha - patch.alpha);
    const auto engine_icc_alpha_error = std::abs(
        static_cast<double>(engine_icc_pixel.alpha) - patch.alpha);
    max_alpha_error = std::max({max_alpha_error, alpha_error, engine_icc_alpha_error});
    if (patch.category == "neutral") {
      const auto maximum = std::max({
          engine_relative.rgb.red,
          engine_relative.rgb.green,
          engine_relative.rgb.blue,
      });
      const auto minimum = std::min({
          engine_relative.rgb.red,
          engine_relative.rgb.green,
          engine_relative.rgb.blue,
      });
      max_neutral_drift = std::max(max_neutral_drift, maximum - minimum);
    }
    patch_results.push_back(PatchResult{
        .name = patch.name,
        .category = patch.category,
        .relative_delta_e_00 = relative_delta,
        .perceptual_delta_e_00 = perceptual_delta,
        .qt_delta_e_00 = qt_delta,
        .colorsync_delta_e_00 = colorsync_delta,
        .engine_icc_delta_e_00 = engine_icc_delta,
        .alpha_error = alpha_error,
    });
#if defined(PHOTO_EDITOR_HAS_COLORSYNC_ORACLE)
    ++patch_index;
#endif
  }
  const auto comparison_elapsed = std::chrono::steady_clock::now() - comparison_started;
  const auto comparison_microseconds =
      std::chrono::duration_cast<std::chrono::microseconds>(comparison_elapsed).count();

  cmsDeleteTransform(relative_transform);
  cmsDeleteTransform(perceptual_transform);
  const auto relative_summary = summarize(relative_errors);
  const auto perceptual_summary = summarize(perceptual_errors);
  const auto engine_icc_summary = summarize(engine_icc_errors);
#if defined(PHOTO_EDITOR_HAS_QT_COLOR_ORACLE)
  const auto qt_summary = summarize(qt_errors);
#endif
#if defined(PHOTO_EDITOR_HAS_COLORSYNC_ORACLE)
  const auto colorsync_summary = summarize(colorsync_errors);
#endif
  if (relative_summary.p95 > 0.25 || relative_summary.maximum > 0.75 ||
      max_alpha_error > 1.0e-7 || max_neutral_drift > 1.0e-6) {
    std::cerr << "relative ICC comparison exceeded release gate p95="
              << relative_summary.p95 << " max=" << relative_summary.maximum
              << " alpha=" << max_alpha_error
              << " neutralDrift=" << max_neutral_drift << '\n';
      return EXIT_FAILURE;
  }
#if defined(PHOTO_EDITOR_HAS_QT_COLOR_ORACLE)
  if (qt_summary.p95 > 0.25 || qt_summary.maximum > 0.75) {
    std::cerr << "Qt comparison exceeded release gate p95=" << qt_summary.p95
              << " max=" << qt_summary.maximum << '\n';
    return EXIT_FAILURE;
  }
#endif
  if (engine_icc_summary.p95 > 0.005 || engine_icc_summary.maximum > 0.01) {
    std::cerr << "engine ICC wrapper diverged from direct Little CMS p95="
              << engine_icc_summary.p95 << " max=" << engine_icc_summary.maximum << '\n';
    return EXIT_FAILURE;
  }

  if (!icc_capability.available || icc_capability.backend != "Little CMS" ||
      icc_capability.compiled_version.empty() ||
      icc_capability.runtime_version.empty() ||
      icc_capability.validated_version.empty()) {
    return fail("CMM comparison requires complete ICC backend provenance");
  }
#if defined(PHOTO_EDITOR_HAS_COLORSYNC_ORACLE)
  if (colorsync_summary.p95 > 0.25 || colorsync_summary.maximum > 0.75) {
    std::cerr << "ColorSync comparison exceeded release gate p95="
              << colorsync_summary.p95 << " max=" << colorsync_summary.maximum << '\n';
    return EXIT_FAILURE;
  }
#endif

  if (!report_path.empty()) {
    std::ofstream report(report_path);
    if (!report) {
      return fail("CMM comparison report path should be writable");
    }
    const auto& worst_relative = worst_patch(
        patch_results,
        [](const PatchResult& result) { return result.relative_delta_e_00; });
    const auto& worst_perceptual = worst_patch(
        patch_results,
        [](const PatchResult& result) { return result.perceptual_delta_e_00; });
    const auto& worst_engine_icc = worst_patch(
        patch_results,
        [](const PatchResult& result) { return result.engine_icc_delta_e_00; });
#if defined(PHOTO_EDITOR_HAS_QT_COLOR_ORACLE)
    const auto& worst_qt = worst_patch(
        patch_results,
        [](const PatchResult& result) { return result.qt_delta_e_00; });
#endif
#if defined(PHOTO_EDITOR_HAS_COLORSYNC_ORACLE)
    const auto& worst_colorsync = worst_patch(
        patch_results,
        [](const PatchResult& result) { return result.colorsync_delta_e_00; });
#endif
    report << std::fixed << std::setprecision(9)
           << "{\n"
           << "  \"schema\": \"photo-editor-cmm-comparison:v1\",\n"
           << "  \"corpus\": \"" << PHOTO_EDITOR_CMM_COMPARISON_CORPUS_PATH << "\",\n"
           << "  \"corpus_sha256\": \"" << PHOTO_EDITOR_CMM_COMPARISON_CORPUS_SHA256 << "\",\n"
           << "  \"icc_corpus_manifest_sha256\": \""
           << PHOTO_EDITOR_ICC_CORPUS_MANIFEST_SHA256 << "\",\n"
           << "  \"icc_corpus_profiles\": 5,\n"
           << "  \"icc_corpus_max_channel_error\": "
           << max_corpus_channel_error << ",\n"
           << "  \"icc_corpus_max_alpha_error\": "
           << max_corpus_alpha_error << ",\n"
           << "  \"icc_device_link_max_channel_error\": "
           << max_device_link_channel_error << ",\n"
           << "  \"source\": \"Linear Display P3\",\n"
           << "  \"source_profile_definition_sha256\": \""
           << PHOTO_EDITOR_CMM_SOURCE_PROFILE_SHA256 << "\",\n"
           << "  \"destination\": \"sRGB\",\n"
           << "  \"destination_profile_definition_sha256\": \""
           << PHOTO_EDITOR_CMM_DESTINATION_PROFILE_SHA256 << "\",\n"
           << "  \"oracle\": \"Little CMS\",\n"
           << "  \"oracle_encoded_version\": " << cmsGetEncodedCMMversion() << ",\n"
           << "  \"icc_capability\": {\"backend\": \""
           << json_escape(icc_capability.backend) << "\", \"compiled_version\": \""
           << json_escape(icc_capability.compiled_version)
           << "\", \"runtime_encoded_version\": \""
           << json_escape(icc_capability.runtime_version)
           << "\", \"validated_version\": \""
           << json_escape(icc_capability.validated_version)
           << "\", \"strict_version\": "
           << (icc_capability.strict_version ? "true" : "false") << "},\n"
           << "  \"engine_icc_backend\": \""
           << json_escape(engine_icc_transform.description().backend) << "\",\n"
           << "  \"engine_icc_backend_version\": \""
           << json_escape(engine_icc_transform.description().backend_version) << "\",\n"
           << "  \"engine_icc_cache\": {\"capacity\": "
           << bounded_cache_stats.capacity << ", \"entries\": "
           << bounded_cache_stats.entries << ", \"hits\": "
           << bounded_cache_stats.hits << ", \"misses\": "
           << bounded_cache_stats.misses << ", \"evictions\": "
           << bounded_cache_stats.evictions << "},\n"
#if defined(PHOTO_EDITOR_HAS_QT_COLOR_ORACLE)
           << "  \"qt_oracle_version\": \"" << QT_VERSION_STR << "\",\n"
#endif
#if defined(PHOTO_EDITOR_HAS_COLORSYNC_ORACLE)
           << "  \"colorsync_oracle_platform\": \"macOS "
           << QSysInfo::productVersion().toStdString() << "\",\n"
#endif
           << "  \"patch_count\": " << patches.size() << ",\n"
           << "  \"relative_delta_e_00\": {\"p50\": " << relative_summary.p50
           << ", \"p95\": " << relative_summary.p95
           << ", \"max\": " << relative_summary.maximum
           << ", \"worst_patch\": \"" << json_escape(worst_relative.name) << "\"},\n"
           << "  \"perceptual_delta_e_00\": {\"p50\": " << perceptual_summary.p50
           << ", \"p95\": " << perceptual_summary.p95
           << ", \"max\": " << perceptual_summary.maximum
           << ", \"worst_patch\": \"" << json_escape(worst_perceptual.name) << "\"},\n"
           << "  \"engine_icc_wrapper_delta_e_00\": {\"p50\": "
           << engine_icc_summary.p50 << ", \"p95\": " << engine_icc_summary.p95
           << ", \"max\": " << engine_icc_summary.maximum
           << ", \"worst_patch\": \"" << json_escape(worst_engine_icc.name) << "\"},\n"
#if defined(PHOTO_EDITOR_HAS_QT_COLOR_ORACLE)
           << "  \"qt_delta_e_00\": {\"p50\": " << qt_summary.p50
           << ", \"p95\": " << qt_summary.p95
           << ", \"max\": " << qt_summary.maximum
           << ", \"worst_patch\": \"" << json_escape(worst_qt.name) << "\"},\n"
#endif
#if defined(PHOTO_EDITOR_HAS_COLORSYNC_ORACLE)
           << "  \"colorsync_delta_e_00\": {\"p50\": " << colorsync_summary.p50
           << ", \"p95\": " << colorsync_summary.p95
           << ", \"max\": " << colorsync_summary.maximum
           << ", \"worst_patch\": \"" << json_escape(worst_colorsync.name) << "\"},\n"
#endif
           << "  \"max_neutral_channel_drift\": " << max_neutral_drift << ",\n"
           << "  \"max_alpha_error\": " << max_alpha_error << ",\n"
           << "  \"comparison_runtime_microseconds\": " << comparison_microseconds << ",\n"
           << "  \"perceptual_comparison_is_informational\": true,\n"
           << "  \"patches\": [\n";
    for (std::size_t index = 0; index < patch_results.size(); ++index) {
      const auto& result = patch_results[index];
      report << "    {\"name\": \"" << json_escape(result.name)
             << "\", \"category\": \"" << json_escape(result.category)
             << "\", \"relative_delta_e_00\": " << result.relative_delta_e_00
             << ", \"perceptual_delta_e_00\": " << result.perceptual_delta_e_00
             << ", \"engine_icc_delta_e_00\": " << result.engine_icc_delta_e_00
#if defined(PHOTO_EDITOR_HAS_QT_COLOR_ORACLE)
             << ", \"qt_delta_e_00\": " << result.qt_delta_e_00
#endif
#if defined(PHOTO_EDITOR_HAS_COLORSYNC_ORACLE)
             << ", \"colorsync_delta_e_00\": " << result.colorsync_delta_e_00
#endif
             << ", \"alpha_error\": " << result.alpha_error << "}"
             << (index + 1 == patch_results.size() ? "\n" : ",\n");
    }
    report << "  ]\n"
           << "}\n";
  }

  if (!heatmap_path.empty()) {
    std::vector<HeatmapRow> heatmap_rows;
    const auto add_row = [&patch_results, &heatmap_rows](
                             std::string label,
                             double display_scale,
                             bool gate_enforced,
                             auto value) {
      HeatmapRow row{
          .label = std::move(label),
          .display_scale = display_scale,
          .gate_enforced = gate_enforced,
      };
      row.values.reserve(patch_results.size());
      for (const auto& patch : patch_results) {
        row.values.push_back(value(patch));
      }
      heatmap_rows.push_back(std::move(row));
    };
    add_row(
        "Engine relative vs Little CMS",
        0.75,
        true,
        [](const PatchResult& result) { return result.relative_delta_e_00; });
    add_row(
        "Engine perceptual vs Little CMS (info)",
        40.0,
        false,
        [](const PatchResult& result) { return result.perceptual_delta_e_00; });
    add_row(
        "Engine ICC wrapper vs Little CMS",
        0.01,
        true,
        [](const PatchResult& result) { return result.engine_icc_delta_e_00; });
#if defined(PHOTO_EDITOR_HAS_QT_COLOR_ORACLE)
    add_row(
        "Engine relative vs Qt QColorSpace",
        0.75,
        true,
        [](const PatchResult& result) { return result.qt_delta_e_00; });
#endif
#if defined(PHOTO_EDITOR_HAS_COLORSYNC_ORACLE)
    add_row(
        "Engine relative vs macOS ColorSync",
        0.75,
        true,
        [](const PatchResult& result) { return result.colorsync_delta_e_00; });
#endif
    if (!write_error_heatmap(heatmap_path, patch_results, heatmap_rows)) {
      return fail("CMM comparison heatmap path should be writable");
    }
  }

  return EXIT_SUCCESS;
}
