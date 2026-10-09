#include "photo_render_utils.h"
#include "openexr_source_color.h"
#include "photo_editor/io/openexr_io.h"
#include "qimage_color_conversion.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <limits>
#include <cstring>
#if defined(PHOTO_EDITOR_TEST_OPENEXR_HEADERS)
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <OpenEXR/ImfStringAttribute.h>
#include <OpenEXR/ImfIntAttribute.h>
#endif

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using namespace photo_editor;
  if (!io::openexr_backend_available()) return 77;
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  const std::vector<float> samples{
      2.0F, .4F, .1F, .5F, -.2F, .5F, 3.0F, 1.0F,
      1.0F, 1.0F, 1.0F, .25F, 0.0F, 0.0F, 0.0F, 0.0F};
  int index = 0;
  auto linear_adobe = pipeline::make_adobe_rgb_color_space();
  linear_adobe.transfer_function = pipeline::TransferFunction::Linear;
  linear_adobe.name = "Linear Adobe RGB";
  for (const auto& space : {pipeline::make_linear_srgb_color_space(),
       pipeline::make_linear_display_p3_color_space(), pipeline::make_linear_prophoto_rgb_color_space(),
       pipeline::make_linear_rec2020_color_space(), pipeline::make_aces2065_1_color_space(),
       pipeline::make_acescg_color_space(), linear_adobe}) {
    const auto path = root.filePath(QStringLiteral("source-%1.exr").arg(index++));
    std::string error;
    if (!io::write_openexr_rgba(path.toStdString(), samples,
        io::OpenExrWriteOptions{.width = 4, .height = 1, .row_stride_floats = 16,
            .half_float = false, .color_space = space}, &error)) return 1;
    QImage decoded;
    if (!desktop::decode_source_image(path, &decoded) || decoded.size() != QSize(4, 1) ||
        decoded.format() != QImage::Format_RGBA32FPx4 || !decoded.colorSpace().isValid()) return 1;
    const auto converted = desktop::convert_tagged_qimage_color_space(decoded,
        QColorSpace(QColorSpace::SRgbLinear), QImage::Format_RGBA32FPx4,
        desktop::QImageColorConversionPurpose::EvidenceMeasurement);
    if (converted.isNull()) return 1;
    const auto* actual = reinterpret_cast<const float*>(converted.constScanLine(0));
    // The independently established engine transform uses double canonical
    // chromaticities, whereas EXR stores float xy and Qt uses rounded D65.
    const auto reference = pipeline::compile_builtin_color_transform(space, pipeline::make_linear_srgb_color_space());
    double maximum = 0;
    for (std::size_t pixel = 0; pixel < 4; ++pixel) {
      const auto expected = pipeline::multiply_matrix3_vector(reference.matrix,
          {samples[pixel * 4], samples[pixel * 4 + 1], samples[pixel * 4 + 2]});
      for (std::size_t channel = 0; channel < 3; ++channel) {
        maximum = std::max(maximum, std::abs(actual[pixel * 4 + channel] - expected[channel]));
      }
      if (actual[pixel * 4 + 3] != samples[pixel * 4 + 3]) return 1;
    }
    std::cout << space.name << " max_rgb_error=" << maximum << '\n';
    if (maximum > 0.001) return 1;
  }
  auto chromaticities = io::openexr_chromaticities_for_color_space(pipeline::make_acescg_color_space());
  chromaticities.red_x = .72F; // Non-canonical custom primaries, outside Qt's gamut validator.
  const auto custom = desktop::openexr_to_linear_srgb_matrix(chromaticities);
  if (!custom) return 1;
  const auto white = pipeline::multiply_matrix3_vector(*custom, {1, 1, 1});
  for (const auto channel : white) if (std::abs(channel - 1.0) > 1e-9) return 1;
  chromaticities.blue_x = chromaticities.red_x;
  chromaticities.blue_y = chromaticities.red_y;
  if (desktop::openexr_to_linear_srgb_matrix(chromaticities)) return 1;
  chromaticities = {};
  if (desktop::openexr_to_linear_srgb_matrix(chromaticities)) return 1;
  chromaticities = io::openexr_chromaticities_for_color_space(pipeline::make_linear_srgb_color_space());
  chromaticities.white_y = std::numeric_limits<float>::quiet_NaN();
  if (desktop::openexr_to_linear_srgb_matrix(chromaticities)) return 1;
  std::cout << "Custom white preservation and invalid/singular metadata rejection passed\n";
#if defined(PHOTO_EDITOR_TEST_OPENEXR_HEADERS)
  namespace Imf = OPENEXR_IMF_NAMESPACE;
  const auto write_header_fixture = [&](const QString& path, const char* id,
      const std::optional<io::OpenExrChromaticities>& chroma, bool aces = false) {
    Imf::Header header(4, 1);
    for (const auto* channel : {"R", "G", "B", "A"}) header.channels().insert(channel, Imf::Channel(Imf::FLOAT));
    if (id != nullptr) header.insert("colorInteropID", Imf::StringAttribute(id));
    if (aces) header.insert("acesImageContainerFlag", Imf::IntAttribute(1));
    if (chroma) {
      const auto& c = *chroma;
      header.insert("chromaticities", Imf::ChromaticitiesAttribute(Imf::Chromaticities(
          {c.red_x, c.red_y}, {c.green_x, c.green_y}, {c.blue_x, c.blue_y}, {c.white_x, c.white_y})));
    }
    // Deliberately misleading private label: declared numeric/standard metadata
    // must take priority over this description.
    header.insert("virel:colorSpace", Imf::StringAttribute("Linear sRGB"));
    auto buffer = samples;
    Imf::FrameBuffer frame;
    int component = 0;
    for (const auto* channel : {"R", "G", "B", "A"}) {
      frame.insert(channel, Imf::Slice(Imf::FLOAT, reinterpret_cast<char*>(buffer.data() + component++), 4 * sizeof(float), 16 * sizeof(float)));
    }
    Imf::OutputFile output(path.toStdString().c_str(), header);
    output.setFrameBuffer(frame);
    output.writePixels(1);
  };
  const auto srgb = io::openexr_chromaticities_for_color_space(pipeline::make_linear_srgb_color_space());
  for (const auto* id : {"lin_rec709_scene", "lin_ap0_scene", "lin_ap1_scene", "lin_p3d65_scene",
                         "lin_rec2020_scene", "lin_adobergb_scene"}) {
    const auto path = root.filePath(QString::fromLatin1(id) + ".exr");
    write_header_fixture(path, id, std::nullopt);
    QImage first;
    if (!desktop::decode_source_image(path, &first)) return 1;
    const auto conflict_path = root.filePath(QString::fromLatin1(id) + "-conflict.exr");
    write_header_fixture(conflict_path, id, srgb);
    QImage second;
    if (!desktop::decode_source_image(conflict_path, &second) || first != second) return 1;
  }
  const auto aces_path = root.filePath("aces-flag.exr");
  write_header_fixture(aces_path, "lin_rec709_scene", srgb, true);
  QImage aces, ap0;
  if (!desktop::decode_source_image(aces_path, &aces) ||
      !desktop::decode_source_image(root.filePath("lin_ap0_scene.exr"), &ap0) || aces != ap0) return 1;
  const auto custom_path = root.filePath("custom.exr");
  auto custom_c = io::openexr_chromaticities_for_color_space(pipeline::make_acescg_color_space());
  custom_c.red_x = .72F;
  write_header_fixture(custom_path, "unknown", custom_c);
  QImage custom_image;
  if (!desktop::decode_source_image(custom_path, &custom_image)) return 1;
  const auto* custom_pixels = reinterpret_cast<const float*>(custom_image.constScanLine(0));
  const auto expected_custom = pipeline::multiply_matrix3_vector(*custom, {2.0, .4, .1});
  for (std::size_t ch = 0; ch < 3; ++ch) if (std::abs(custom_pixels[ch] - expected_custom[ch]) > 1e-6) return 1;
  for (const auto* id : {"", "unknown", "data", "not-a-known-space"}) {
    const auto path = root.filePath(QString::fromLatin1(id) + "-rejected.exr");
    write_header_fixture(path, id, std::nullopt);
    QImage rejected;
    if (desktop::decode_source_image(path, &rejected)) return 1;
  }
  const auto singular_path = root.filePath("singular.exr");
  custom_c.blue_x = custom_c.red_x;
  custom_c.blue_y = custom_c.red_y;
  write_header_fixture(singular_path, nullptr, custom_c);
  QImage rejected;
  if (desktop::decode_source_image(singular_path, &rejected)) return 1;
  const auto provenance = desktop::resolve_decoder_provenance_for_source(aces_path, {});
  if (!provenance.ok() || !provenance.provenance.contains(desktop::kOpenExrSourceColorContract)) return 1;
  std::cout << "Interop-only files, conflicting chromaticities, ACES flag, custom gamut, unknown/data rejection and provenance passed\n";
#endif
  if (argc == 3) {
    QImage reference_image, candidate_image;
    // This optional oracle is the official Rec709.exr, whose documented source
    // encoding is Rec.709 but whose old file omits color metadata. Assign it
    // explicitly here; production must not guess the encoding of unknown files.
    const auto external = io::inspect_openexr(argv[1]);
    if (!external.valid || external.rgba_pixels.empty()) return 1;
    reference_image = QImage(external.width, external.height, QImage::Format_RGBA32FPx4);
    for (int y = 0; y < external.height; ++y) {
      std::memcpy(reference_image.scanLine(y), external.rgba_pixels.data() +
          static_cast<std::size_t>(y * external.width) * 4, static_cast<std::size_t>(external.width) * 4 * sizeof(float));
    }
    reference_image.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
    if (!desktop::decode_source_image(QString::fromLocal8Bit(argv[2]), &candidate_image) ||
        reference_image.size() != candidate_image.size()) return 1;
    double maximum_relative = 0, squared_relative = 0;
    std::size_t count = 0;
    for (int y = 0; y < reference_image.height(); ++y) {
      const auto* reference_pixels = reinterpret_cast<const float*>(reference_image.constScanLine(y));
      const auto* candidate_pixels = reinterpret_cast<const float*>(candidate_image.constScanLine(y));
      for (int x = 0; x < reference_image.width(); ++x) {
        for (int ch = 0; ch < 3; ++ch) {
          const auto offset = x * 4 + ch;
          const auto error = std::abs(candidate_pixels[offset] - reference_pixels[offset]) /
              (1.0 + std::abs(reference_pixels[offset]));
          maximum_relative = std::max(maximum_relative, error);
          squared_relative += error * error;
          ++count;
        }
      }
    }
    const auto rms = std::sqrt(squared_relative / static_cast<double>(count));
    std::cout << "External equivalent encodings: max_relative=" << maximum_relative << " rms_relative=" << rms << '\n';
    const auto artifact = qEnvironmentVariable("PHOTO_EDITOR_TEST_EXR_OUTPUT");
    if (!artifact.isEmpty()) {
      if (!desktop::convert_tagged_qimage_color_space(reference_image,
              QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
              desktop::QImageColorConversionPurpose::EvidenceMeasurement).save(artifact + "-reference.png") ||
          !desktop::convert_tagged_qimage_color_space(candidate_image,
              QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
              desktop::QImageColorConversionPurpose::EvidenceMeasurement).save(artifact + "-candidate.png")) return 1;
    }
    if (maximum_relative > .01 || rms > .001) return 1;
  }
  return 0;
}
