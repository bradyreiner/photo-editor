#include "export_image_encoder.h"
#include "photo_export_task.h"
#include "qimage_color_conversion.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <array>
#include <iostream>
#include <vector>

namespace d = photo_editor::desktop;

namespace {
bool same_output(const QImage& expected, const QImage& actual) {
  if (expected.isNull() || actual.isNull() || expected.size() != actual.size() ||
      expected.depth() != actual.depth() || expected.colorSpace() != actual.colorSpace() ||
      expected.colorSpace().iccProfile() != actual.colorSpace().iccProfile() ||
      expected.dotsPerMeterX() != actual.dotsPerMeterX() ||
      expected.dotsPerMeterY() != actual.dotsPerMeterY() || expected.offset() != actual.offset() ||
      expected.textKeys() != actual.textKeys() ||
      expected.convertToFormat(QImage::Format_RGBA64) != actual.convertToFormat(QImage::Format_RGBA64))
    return false;
  for (const auto& key : expected.textKeys())
    if (expected.text(key) != actual.text(key)) return false;
  return true;
}

QImage reference_png(const QImage& image, const QString& description = {}) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly)) return {};
  QImageWriter writer(&buffer, "png");
  if (!description.isNull()) writer.setText("Description", description);
  if (!writer.write(image)) return {};
  return QImage::fromData(bytes, "PNG");
}

quint32 read_u32_be(const QByteArray& data, qsizetype offset) {
  return (static_cast<quint32>(static_cast<quint8>(data[offset])) << 24U) |
         (static_cast<quint32>(static_cast<quint8>(data[offset + 1])) << 16U) |
         (static_cast<quint32>(static_cast<quint8>(data[offset + 2])) << 8U) |
         static_cast<quint32>(static_cast<quint8>(data[offset + 3]));
}

quint16 read_u16_be(const QByteArray& data, qsizetype offset) {
  return static_cast<quint16>(
      (static_cast<quint16>(static_cast<quint8>(data[offset])) << 8U) |
      static_cast<quint16>(static_cast<quint8>(data[offset + 1])));
}

struct PngFacts final {
  bool valid = false;
  quint32 width = 0U;
  quint32 height = 0U;
  quint8 bit_depth = 0U;
  quint8 color_type = 0U;
  QByteArray icc_profile;
  QString error;
};

PngFacts inspect_png(const QByteArray& bytes, qsizetype expected_profile_size) {
  static constexpr std::array<char, 8> signature{
      '\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n'};
  if (bytes.size() < static_cast<qsizetype>(signature.size()) ||
      std::memcmp(bytes.constData(), signature.data(), signature.size()) != 0) {
    return {.error = QStringLiteral("invalid PNG signature")};
  }

  PngFacts result;
  auto saw_ihdr = false;
  auto saw_iccp = false;
  auto saw_iend = false;
  auto offset = static_cast<qsizetype>(signature.size());
  while (offset + 12 <= bytes.size()) {
    const auto chunk_length = read_u32_be(bytes, offset);
    const auto remaining = static_cast<quint64>(bytes.size() - offset - 12);
    if (static_cast<quint64>(chunk_length) > remaining) {
      result.error = QStringLiteral("PNG chunk extends beyond file");
      return result;
    }
    const auto chunk_type = bytes.mid(offset + 4, 4);
    const auto chunk_data = bytes.mid(offset + 8, static_cast<qsizetype>(chunk_length));
    if (chunk_type == QByteArrayLiteral("IHDR")) {
      if (saw_ihdr || chunk_length != 13U) {
        result.error = QStringLiteral("invalid PNG IHDR");
        return result;
      }
      saw_ihdr = true;
      result.width = read_u32_be(chunk_data, 0);
      result.height = read_u32_be(chunk_data, 4);
      result.bit_depth = static_cast<quint8>(chunk_data[8]);
      result.color_type = static_cast<quint8>(chunk_data[9]);
    } else if (chunk_type == QByteArrayLiteral("iCCP")) {
      if (saw_iccp) {
        result.error = QStringLiteral("duplicate PNG iCCP chunk");
        return result;
      }
      saw_iccp = true;
      const auto name_end = chunk_data.indexOf('\0');
      if (name_end <= 0 || name_end + 2 >= chunk_data.size() ||
          static_cast<quint8>(chunk_data[name_end + 1]) != 0U ||
          expected_profile_size < 0 ||
          static_cast<quint64>(expected_profile_size) > 0xffffffffULL) {
        result.error = QStringLiteral("invalid PNG iCCP metadata");
        return result;
      }
      QByteArray compressed_size_prefix(4, '\0');
      const auto profile_size = static_cast<quint32>(expected_profile_size);
      for (auto byte = 0; byte < 4; ++byte) {
        compressed_size_prefix[byte] = static_cast<char>(
            (profile_size >> static_cast<unsigned int>((3 - byte) * 8)) & 0xffU);
      }
      const auto zlib_stream = chunk_data.mid(name_end + 2);
      result.icc_profile = qUncompress(compressed_size_prefix + zlib_stream);
      if (result.icc_profile.size() != expected_profile_size) {
        result.error = QStringLiteral("could not decompress PNG iCCP payload");
        return result;
      }
    } else if (chunk_type == QByteArrayLiteral("IEND")) {
      if (chunk_length != 0U) {
        result.error = QStringLiteral("invalid PNG IEND");
        return result;
      }
      saw_iend = true;
      break;
    }
    offset += static_cast<qsizetype>(chunk_length) + 12;
  }
  if (!saw_ihdr || !saw_iccp || !saw_iend) {
    result.error = QStringLiteral("PNG is missing IHDR, iCCP, or IEND");
    return result;
  }
  result.valid = true;
  return result;
}

struct JpegIccFacts final {
  bool valid = false;
  QByteArray icc_profile;
  QString error;
};

JpegIccFacts inspect_jpeg_icc(const QByteArray& bytes) {
  if (bytes.size() < 4 || static_cast<quint8>(bytes[0]) != 0xffU ||
      static_cast<quint8>(bytes[1]) != 0xd8U) {
    return {.error = QStringLiteral("invalid JPEG SOI")};
  }
  static const QByteArray icc_signature("ICC_PROFILE\0", 12);
  std::vector<QByteArray> chunks;
  auto expected_chunks = 0;
  auto offset = qsizetype{2};
  while (offset < bytes.size()) {
    if (static_cast<quint8>(bytes[offset]) != 0xffU) {
      return {.error = QStringLiteral("invalid JPEG marker boundary")};
    }
    while (offset < bytes.size() && static_cast<quint8>(bytes[offset]) == 0xffU) {
      ++offset;
    }
    if (offset >= bytes.size()) break;
    const auto marker = static_cast<quint8>(bytes[offset++]);
    if (marker == 0xdaU || marker == 0xd9U) break;
    if ((marker >= 0xd0U && marker <= 0xd7U) || marker == 0x01U) continue;
    if (offset + 2 > bytes.size()) {
      return {.error = QStringLiteral("truncated JPEG segment length")};
    }
    const auto segment_length = read_u16_be(bytes, offset);
    if (segment_length < 2U ||
        static_cast<qsizetype>(segment_length) > bytes.size() - offset) {
      return {.error = QStringLiteral("invalid JPEG segment length")};
    }
    const auto payload = bytes.mid(offset + 2, segment_length - 2U);
    if (marker == 0xe2U && payload.startsWith(icc_signature)) {
      if (payload.size() < 14) {
        return {.error = QStringLiteral("truncated JPEG ICC APP2 segment")};
      }
      const auto sequence = static_cast<quint8>(payload[12]);
      const auto count = static_cast<quint8>(payload[13]);
      if (sequence == 0U || count == 0U || sequence > count ||
          (expected_chunks != 0 && expected_chunks != count)) {
        return {.error = QStringLiteral("invalid JPEG ICC APP2 sequence")};
      }
      expected_chunks = count;
      if (chunks.empty()) chunks.resize(static_cast<std::size_t>(count));
      auto& chunk = chunks[static_cast<std::size_t>(sequence - 1U)];
      if (!chunk.isEmpty()) {
        return {.error = QStringLiteral("duplicate JPEG ICC APP2 sequence")};
      }
      chunk = payload.mid(14);
    }
    offset += segment_length;
  }
  if (expected_chunks == 0 || chunks.size() != static_cast<std::size_t>(expected_chunks)) {
    return {.error = QStringLiteral("JPEG has no ICC APP2 profile")};
  }
  JpegIccFacts result;
  for (const auto& chunk : chunks) {
    if (chunk.isEmpty()) {
      result.error = QStringLiteral("JPEG ICC APP2 profile has a missing segment");
      return result;
    }
    result.icc_profile.append(chunk);
  }
  result.valid = true;
  return result;
}

struct ExportProfile final {
  QString name;
  QString file_stem;
  QColorSpace color_space;
};

QImage make_export_pattern() {
  static constexpr std::array<std::array<quint16, 4>, 6> samples{{
      {{0x1357U, 0x2468U, 0x9abcU, 0xf012U}},
      {{0x8badU, 0x147aU, 0xd369U, 0x7abcU}},
      {{0x0321U, 0xe654U, 0x789bU, 0x8001U}},
      {{0xfedcU, 0x9a87U, 0x4561U, 0xcdefU}},
      {{0x556bU, 0x31e7U, 0xb248U, 0x4003U}},
      {{0x7123U, 0xb9d5U, 0x2aceU, 0xffffU}},
  }};
  QImage image(static_cast<int>(samples.size()), 1, QImage::Format_RGBA64);
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (auto x = 0; x < static_cast<int>(samples.size()); ++x) {
    const auto& sample = samples[static_cast<std::size_t>(x)];
    image.setPixelColor(
        x, 0, QColor::fromRgba64(sample[0], sample[1], sample[2], sample[3]));
  }
  return image;
}

bool verify_export_profile(
    const ExportProfile& profile,
    const QString& artifact_directory,
    QByteArray* manifest) {
  const auto fail = [&](const QString& reason) {
    std::cerr << "emitted ICC export failure profile=" << profile.name.toStdString()
              << " reason=" << reason.toStdString() << '\n';
    return false;
  };
  const auto expected_icc = profile.color_space.iccProfile();
  if (!profile.color_space.isValid() || expected_icc.isEmpty()) {
    return fail(QStringLiteral("selected target has no valid ICC profile"));
  }

  const auto source = make_export_pattern();
  const auto expected = d::convert_tagged_qimage_color_space(
      source,
      profile.color_space,
      QImage::Format_RGBA64,
      d::QImageColorConversionPurpose::OutputPublication);
  if (expected.isNull() || expected.format() != QImage::Format_RGBA64 ||
      expected.colorSpace() != profile.color_space) {
    return fail(QStringLiteral("requested target conversion failed"));
  }

  auto png_settings = d::export_image_settings_for_preset(QStringLiteral("PNG"));
  png_settings.image_format = QImage::Format_RGBA64;
  const auto prepared = d::prepare_image_for_export_encoding(expected, png_settings);
  const auto queued = d::encode_prepared_standard_export_image(
      prepared, png_settings, QStringLiteral("profile export contract"));
  if (!queued.valid) return fail(queued.error);

  const auto verify_png = [&](const QByteArray& png, const QString& label) {
    const auto facts = inspect_png(png, expected_icc.size());
    if (!facts.valid) return fail(label + QStringLiteral(": ") + facts.error);
    if (facts.width != static_cast<quint32>(expected.width()) ||
        facts.height != static_cast<quint32>(expected.height()) ||
        facts.bit_depth != 16U || facts.color_type != 6U) {
      return fail(label + QStringLiteral(": IHDR is not 16-bit RGBA at the expected size"));
    }
    if (facts.icc_profile != expected_icc) {
      return fail(label + QStringLiteral(": decompressed iCCP bytes differ from selected profile"));
    }
    const auto decoded = QImage::fromData(png, "PNG");
    if (decoded.isNull() || decoded.size() != expected.size() || decoded.depth() != 64 ||
        decoded.colorSpace().iccProfile() != expected_icc) {
      return fail(label + QStringLiteral(": Qt readback lost 16-bit pixels or the ICC profile"));
    }
    auto has_distinct_16_bit_channels = false;
    for (auto x = 0; x < expected.width(); ++x) {
      const auto want = expected.pixelColor(x, 0).rgba64();
      const auto got = decoded.pixelColor(x, 0).rgba64();
      if (got.red() != want.red() || got.green() != want.green() ||
          got.blue() != want.blue() || got.alpha() != want.alpha()) {
        return fail(label + QStringLiteral(": big-endian RGBA64 channel readback changed a sample"));
      }
      const auto source_alpha = source.pixelColor(x, 0).rgba64().alpha();
      if (got.alpha() != source_alpha) {
        return fail(label + QStringLiteral(": target conversion changed unassociated alpha"));
      }
      has_distinct_16_bit_channels = has_distinct_16_bit_channels ||
          (got.red() != got.green() && got.green() != got.blue() &&
           got.red() != got.blue() &&
           static_cast<quint8>(got.red() >> 8U) != static_cast<quint8>(got.red() & 0xffU) &&
           static_cast<quint8>(got.green() >> 8U) != static_cast<quint8>(got.green() & 0xffU) &&
           static_cast<quint8>(got.blue() >> 8U) != static_cast<quint8>(got.blue() & 0xffU));
    }
    if (!has_distinct_16_bit_channels) {
      return fail(label + QStringLiteral(": fixture did not exercise distinct 16-bit channel bytes"));
    }
    return true;
  };
  if (!verify_png(queued.bytes, QStringLiteral("queued PNG"))) return false;

  auto jpeg_settings = d::export_image_settings_for_preset(QStringLiteral("JPEG"));
  const auto jpeg_prepared = d::prepare_image_for_export_encoding(expected, jpeg_settings);
  const auto queued_jpeg = d::encode_prepared_standard_export_image(
      jpeg_prepared, jpeg_settings, QStringLiteral("profile JPEG contract"));
  if (!queued_jpeg.valid) return fail(queued_jpeg.error);
  const auto queued_jpeg_icc = inspect_jpeg_icc(queued_jpeg.bytes);
  if (!queued_jpeg_icc.valid || queued_jpeg_icc.icc_profile != expected_icc) {
    return fail(QStringLiteral("queued JPEG APP2 ICC differs from selected profile: ") +
                queued_jpeg_icc.error);
  }

  const auto png_path = QDir(artifact_directory).filePath(profile.file_stem + QStringLiteral(".png"));
  auto png_control = std::make_shared<d::PhotoExportControl>();
  const auto png_result = d::run_photo_export(
      [expected] { return expected; }, png_path, "png", png_control);
  if (!png_result.success ||
      png_control->phase.load() != d::PhotoExportControl::Phase::Finished) {
    return fail(QStringLiteral("atomic PNG export failed: ") + png_result.error);
  }
  QFile png_file(png_path);
  if (!png_file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("could not read atomic PNG output"));
  const auto atomic_png = png_file.readAll();
  if (!verify_png(atomic_png, QStringLiteral("atomic PNG"))) return false;

  const auto jpeg_path = QDir(artifact_directory).filePath(profile.file_stem + QStringLiteral(".jpg"));
  auto jpeg_control = std::make_shared<d::PhotoExportControl>();
  const auto jpeg_result = d::run_photo_export(
      [expected] { return expected; }, jpeg_path, "jpg", jpeg_control);
  if (!jpeg_result.success ||
      jpeg_control->phase.load() != d::PhotoExportControl::Phase::Finished) {
    return fail(QStringLiteral("atomic JPEG export failed: ") + jpeg_result.error);
  }
  QFile jpeg_file(jpeg_path);
  if (!jpeg_file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("could not read atomic JPEG output"));
  const auto atomic_jpeg = inspect_jpeg_icc(jpeg_file.readAll());
  if (!atomic_jpeg.valid || atomic_jpeg.icc_profile != expected_icc) {
    return fail(QStringLiteral("atomic JPEG APP2 ICC differs from selected profile: ") +
                atomic_jpeg.error);
  }

  manifest->append(profile.name.toUtf8());
  manifest->append(" icc_bytes=");
  manifest->append(QByteArray::number(expected_icc.size()));
  manifest->append(" png_iCCP_exact=1 png_IHDR=16bit_RGBA png_RGBA64_readback_exact=1");
  manifest->append(" jpeg_APP2_ICC_exact=1\n");
  std::cout << "emitted profile=" << profile.name.toStdString()
            << " icc_bytes=" << expected_icc.size()
            << " png_iCCP_exact=1 png_IHDR=16bit_RGBA png_RGBA64_readback_exact=1"
            << " jpeg_APP2_ICC_exact=1\n";
  return true;
}

bool verify_emitted_color_profile_exports(const QString& temporary_directory) {
  std::array<ExportProfile, 5> profiles{
      ExportProfile{QStringLiteral("sRGB"), QStringLiteral("srgb"), QColorSpace(QColorSpace::SRgb)},
      ExportProfile{QStringLiteral("Display P3"), QStringLiteral("display-p3"), QColorSpace(QColorSpace::DisplayP3)},
      ExportProfile{QStringLiteral("Adobe RGB"), QStringLiteral("adobe-rgb"), QColorSpace(QColorSpace::AdobeRgb)},
      ExportProfile{QStringLiteral("ProPhoto RGB"), QStringLiteral("prophoto-rgb"), QColorSpace(QColorSpace::ProPhotoRgb)},
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
      ExportProfile{QStringLiteral("Rec.2020"), QStringLiteral("rec2020"), QColorSpace(QColorSpace::Bt2020)},
#else
      ExportProfile{QStringLiteral("Rec.2020"), QStringLiteral("rec2020"), QColorSpace{}},
#endif
  };
  const auto requested_artifact_directory =
      qEnvironmentVariable("LOOKSY_PNG_EXPORT_ARTIFACT_DIR");
  const auto artifact_directory = requested_artifact_directory.isEmpty()
      ? temporary_directory : requested_artifact_directory;
  if (!QDir().mkpath(artifact_directory)) {
    std::cerr << "could not create emitted-profile artifact directory\n";
    return false;
  }
  QByteArray manifest;
  for (const auto& profile : profiles) {
    if (!verify_export_profile(profile, artifact_directory, &manifest)) return false;
  }
  if (!requested_artifact_directory.isEmpty()) {
    QFile manifest_file(QDir(artifact_directory).filePath(QStringLiteral("manifest.txt")));
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest_file.write(manifest) != manifest.size()) {
      std::cerr << "could not persist emitted-profile export manifest\n";
      return false;
    }
  }
  return true;
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  if (!directory.isValid()) return 1;
  // Flattening for delivery must preserve print resolution and image metadata,
  // and operate on every source pixel regardless of its display pixel ratio.
  for (const bool scene_linear : {false, true}) {
    QImage image(32, 24, QImage::Format_RGBA64);
    image.fill(QColor::fromRgba64(0, 0, 0, 32768));
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    image.setDotsPerMeterX(11811);
    image.setDotsPerMeterY(5906);
    image.setOffset(QPoint(4, 7));
    image.setText("Author", "Delivery metadata");
    image.setDevicePixelRatio(2.0);
    auto settings = d::export_image_settings_for_preset(QStringLiteral("JPEG"));
    settings.scene_linear = scene_linear;
    const auto prepared = d::prepare_image_for_export_encoding(image, settings);
    if (prepared.dotsPerMeterX() != image.dotsPerMeterX() ||
        prepared.dotsPerMeterY() != image.dotsPerMeterY() ||
        prepared.offset() != image.offset() || prepared.text("Author") != image.text("Author") ||
        prepared.colorSpace() != image.colorSpace() ||
        prepared.devicePixelRatio() != image.devicePixelRatio()) {
      std::cerr << "flattened export lost image metadata: scene_linear=" << scene_linear << '\n';
      return 4;
    }
    const auto top_left = prepared.pixelColor(0, 0);
    const auto bottom_right = prepared.pixelColor(prepared.width() - 1, prepared.height() - 1);
    if (top_left != bottom_right || bottom_right.alpha() != 255 ||
        bottom_right.red() < 126 || bottom_right.red() > 129) {
      std::cerr << "flattened export did not composite every physical source pixel\n";
      return 5;
    }
    if (!scene_linear) {
      const auto encoded = d::encode_prepared_standard_export_image(prepared, settings, "JPEG delivery");
      const auto decoded = QImage::fromData(encoded.bytes, "JPEG");
      const auto path = directory.filePath("flattened.jpg");
      auto control = std::make_shared<d::PhotoExportControl>();
      const auto saved = d::run_photo_export([image] { return image; }, path, "jpg", control);
      const auto reopened = QImage(path);
      for (const auto& output : {decoded, reopened}) {
        if (output.isNull() || output.size() != image.size() ||
            // JPEG records integer DPI; permit its half-DPI quantization.
            qAbs(output.dotsPerMeterX() - image.dotsPerMeterX()) > 20 ||
            qAbs(output.dotsPerMeterY() - image.dotsPerMeterY()) > 20 ||
            output.colorSpace() != image.colorSpace() || output.text("Author") != image.text("Author")) {
          std::cerr << "JPEG delivery did not retain print resolution, profile and author: "
                    << output.dotsPerMeterX() << '/' << output.dotsPerMeterY()
                    << " profile=" << (output.colorSpace() == image.colorSpace())
                    << " author=" << output.text("Author").toStdString() << '\n';
          return 6;
        }
      }
      if (!encoded.valid || !saved.success) return 7;
    }
  }
  int cases = 0;
  for (const auto format : {QImage::Format_ARGB32, QImage::Format_RGB888,
           QImage::Format_RGBA8888_Premultiplied, QImage::Format_RGBA64,
           QImage::Format_RGBX64, QImage::Format_Grayscale8, QImage::Format_Grayscale16}) {
    const bool grayscale = format == QImage::Format_Grayscale8 || format == QImage::Format_Grayscale16;
    for (const auto profile : {QColorSpace::SRgb, QColorSpace::DisplayP3}) {
      if (grayscale && profile != QColorSpace::SRgb) continue;
      for (int pattern = 0; pattern < 4; ++pattern) {
        QImage image(129, 97, format);
        // RGB ICC profiles are invalid in grayscale PNG; cover unprofiled gray separately.
        if (!grayscale) image.setColorSpace(QColorSpace(profile));
        image.setDotsPerMeterX(11811); image.setDotsPerMeterY(5906);
        image.setOffset(QPoint(4, 7));
        image.setText("Description", "Original description");
        image.setText("Author", "Encoding contract");
        for (int y = 0; y < image.height(); ++y) {
          for (int x = 0; x < image.width(); ++x) {
            const auto v = static_cast<quint16>(pattern == 0 ? 32768 : pattern == 1
                ? x * 65535 / (image.width() - 1) : pattern == 2
                ? ((x + y) % 17 == 0 ? 65535 : 0) : (x * 4357 + y * 7919) % 65536);
            const auto alpha = static_cast<quint16>((x + y) % 5 * 65535 / 4);
            image.setPixelColor(x, y, QColor::fromRgba64(v, static_cast<quint16>(65535 - v),
                static_cast<quint16>(v / 2), alpha));
          }
        }
        d::ExportImageSettings settings;
        settings.writer_format = "png"; settings.image_format = format;
        const auto prepared = d::prepare_image_for_export_encoding(image, settings);
        const auto encoded = d::encode_prepared_standard_export_image(prepared, settings, "Export description");
        if (!encoded.valid || !same_output(reference_png(prepared, "Export description"),
            QImage::fromData(encoded.bytes, "PNG"))) {
          std::cerr << "queued PNG encoding mismatch: " << static_cast<int>(format) << '/' << pattern << '\n';
          return 2;
        }
        const auto path = directory.filePath("export.png");
        auto control = std::make_shared<d::PhotoExportControl>();
        const auto result = d::run_photo_export([image] { return image; }, path, "png", control);
        if (!result.success || control->phase.load() != d::PhotoExportControl::Phase::Finished ||
            !same_output(reference_png(image), QImage(path))) {
          std::cerr << "atomic PNG encoding mismatch: " << static_cast<int>(format) << '/' << pattern << '\n';
          return 3;
        }
        ++cases;
      }
    }
  }
  if (!verify_emitted_color_profile_exports(directory.path())) return 8;
  std::cout << "PNG cases=" << cases << " queued_and_atomic_pixels_metadata_exact=1\n";
  return 0;
}
