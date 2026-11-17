#include "timeline_hdr_sdr.h"

#include <QtEndian>
#include <qrgba64.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <set>

namespace {

constexpr int kVideoFidelityWidth = 1024;
constexpr int kVideoFidelityHeight = 96;
QJsonArray gVideoFidelityResults;

bool retain_video_fidelity_artifacts(
    const QString& source_directory,
    bool success) {
  auto output_directory = qEnvironmentVariable(
      "PHOTO_EDITOR_VIDEO_FIDELITY_ARTIFACTS_DIR");
  if (output_directory.trimmed().isEmpty()) {
    output_directory = QDir::current().filePath(
        QStringLiteral("build/app-audit-fixes-2026-10-25/video"));
  }
  if (!QDir().mkpath(output_directory)) {
    std::cerr << "could not create video fidelity artifact directory: "
              << output_directory.toStdString() << '\n';
    return false;
  }
  const QStringList artifact_names{
      QStringLiteral("hdr-gradient-pq.png"),
      QStringLiteral("hdr-gradient-pq.mov"),
      QStringLiteral("PQ-preview-rec709.png"),
      QStringLiteral("PQ-delivery.mov"),
      QStringLiteral("hdr-gradient-hlg.png"),
      QStringLiteral("hdr-gradient-hlg.mov"),
      QStringLiteral("HLG-preview-rec709.png"),
      QStringLiteral("HLG-delivery.mov"),
  };
  for (const auto& name : artifact_names) {
    const auto source_path = QDir(source_directory).filePath(name);
    if (!QFileInfo::exists(source_path)) {
      continue;
    }
    const auto destination_path = QDir(output_directory).filePath(name);
    QFile::remove(destination_path);
    if (!QFile::copy(source_path, destination_path)) {
      std::cerr << "could not retain video fidelity artifact: "
                << destination_path.toStdString() << '\n';
      return false;
    }
  }
  const auto report_path = QDir(output_directory).filePath(
      QStringLiteral("timeline-video-delivery-fidelity.json"));
  QSaveFile report(report_path);
  if (!report.open(QIODevice::WriteOnly) ||
      report.write(QJsonDocument(QJsonObject{
          {QStringLiteral("success"), success},
          {QStringLiteral("results"), gVideoFidelityResults},
          {QStringLiteral("outputDirectory"), output_directory},
      }).toJson(QJsonDocument::Indented)) < 0 ||
      !report.commit()) {
    std::cerr << "could not write video fidelity verification report\n";
    return false;
  }
  std::cout << "Video fidelity artifacts: " << output_directory.toStdString()
            << '\n';
  return true;
}

QImage video_fidelity_gradient_image() {
  auto image = QImage(
      kVideoFidelityWidth, kVideoFidelityHeight, QImage::Format_RGBA64);
  if (image.isNull()) {
    return {};
  }
  for (int y = 0; y < image.height(); ++y) {
    auto* row = reinterpret_cast<QRgba64*>(image.scanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      const auto value = static_cast<std::uint16_t>(
          (static_cast<std::uint32_t>(x) * 65535U) /
          static_cast<std::uint32_t>(image.width() - 1));
      if (y < 64) {
        row[x] = QRgba64::fromRgba64(value, value, value, 65535U);
      } else if (x < image.width() / 3) {
        row[x] = QRgba64::fromRgba64(65535U, 0U, 0U, 65535U);
      } else if (x < image.width() * 2 / 3) {
        row[x] = QRgba64::fromRgba64(0U, 65535U, 0U, 65535U);
      } else {
        row[x] = QRgba64::fromRgba64(0U, 0U, 65535U, 65535U);
      }
    }
  }
  return image;
}

QString create_video_fidelity_hdr_source(
    const QString& ffmpeg,
    const QString& directory,
    const QImage& image,
    const QString& transfer) {
  const auto is_hlg = transfer == QStringLiteral("arib-std-b67");
  const auto png_path = QDir(directory).filePath(
      is_hlg ? QStringLiteral("hdr-gradient-hlg.png")
             : QStringLiteral("hdr-gradient-pq.png"));
  const auto source_path = QDir(directory).filePath(
      is_hlg ? QStringLiteral("hdr-gradient-hlg.mov")
             : QStringLiteral("hdr-gradient-pq.mov"));
  if (!image.save(png_path)) {
    std::cerr << "could not write the 16-bit HDR gradient fixture\n";
    return {};
  }
  const auto conversion_filter = QStringLiteral(
      "zscale=matrixin=gbr:rangein=full:transferin=%1:primariesin=bt2020:"
      "matrix=bt2020nc:range=limited:transfer=%1:primaries=bt2020,"
      "format=yuv444p10le").arg(transfer);
  QString error;
  if (!run_process(
          ffmpeg,
          {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
           QStringLiteral("error"), QStringLiteral("-y"),
           QStringLiteral("-loop"), QStringLiteral("1"),
           QStringLiteral("-framerate"), QStringLiteral("30"),
           QStringLiteral("-i"), png_path,
           QStringLiteral("-t"), QStringLiteral("1"),
           QStringLiteral("-vf"), conversion_filter,
           QStringLiteral("-c:v"), QStringLiteral("prores_ks"),
           QStringLiteral("-profile:v"), QStringLiteral("4"),
           QStringLiteral("-pix_fmt"), QStringLiteral("yuv444p10le"),
           QStringLiteral("-color_primaries"), QStringLiteral("bt2020"),
           QStringLiteral("-color_trc"), transfer,
           QStringLiteral("-colorspace"), QStringLiteral("bt2020nc"),
           QStringLiteral("-color_range"), QStringLiteral("tv"),
           source_path},
          &error)) {
    std::cerr << "HDR gradient video creation failed: " << error.toStdString()
              << '\n';
    return {};
  }
  return source_path;
}

QImage decode_hdr_source_as_encoded_rgb(
    const QString& ffmpeg,
    const QString& source_path,
    const QString& transfer,
    QString* error) {
  const auto conversion_filter = QStringLiteral(
      "zscale=matrixin=bt2020nc:rangein=limited:transferin=%1:"
      "primariesin=bt2020:matrix=bt709:range=full:transfer=%1:"
      "primaries=bt2020,format=rgba64le").arg(transfer);
  const auto bytes = process_bytes(
      ffmpeg,
      {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
       QStringLiteral("error"), QStringLiteral("-i"), source_path,
       QStringLiteral("-frames:v"), QStringLiteral("1"),
       QStringLiteral("-vf"), conversion_filter,
       QStringLiteral("-pix_fmt"), QStringLiteral("rgba64le"),
       QStringLiteral("-f"), QStringLiteral("rawvideo"),
       QStringLiteral("-")},
      error);
  const auto required_size = static_cast<qsizetype>(kVideoFidelityWidth) *
                             kVideoFidelityHeight * 8;
  if (bytes.size() != required_size) {
    if (error != nullptr && error->isEmpty()) {
      *error = QStringLiteral("the decoded HDR source did not match its expected RGBA64 frame size");
    }
    return {};
  }
  auto image = QImage(
      kVideoFidelityWidth, kVideoFidelityHeight, QImage::Format_RGBA64);
  if (image.isNull()) {
    return {};
  }
  std::memcpy(image.bits(), bytes.constData(), static_cast<std::size_t>(required_size));
  return image;
}

QByteArray decode_delivery_rgb64(
    const QString& ffmpeg,
    const QString& media_path,
    QString* error) {
  return process_bytes(
      ffmpeg,
      {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
       QStringLiteral("error"), QStringLiteral("-i"), media_path,
       QStringLiteral("-frames:v"), QStringLiteral("1"),
       QStringLiteral("-vf"), QStringLiteral("format=rgba64le"),
       QStringLiteral("-pix_fmt"), QStringLiteral("rgba64le"),
       QStringLiteral("-f"), QStringLiteral("rawvideo"),
       QStringLiteral("-")},
      error);
}

std::set<quint16> decode_delivery_luma_levels(
    const QString& ffmpeg,
    const QString& media_path,
    QString* error) {
  const auto bytes = process_bytes(
      ffmpeg,
      {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
       QStringLiteral("error"), QStringLiteral("-i"), media_path,
       QStringLiteral("-frames:v"), QStringLiteral("1"),
       QStringLiteral("-vf"), QStringLiteral("extractplanes=y"),
       QStringLiteral("-pix_fmt"), QStringLiteral("gray16le"),
       QStringLiteral("-f"), QStringLiteral("rawvideo"),
       QStringLiteral("-")},
      error);
  std::set<quint16> levels;
  for (qsizetype offset = 0; offset + 1 < bytes.size(); offset += 2) {
    levels.insert(qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(bytes.constData() + offset)));
  }
  return levels;
}

bool verify_hdr_delivery_matches_preview(
    const QString& ffmpeg,
    const QString& ffprobe,
    const QString& directory,
    const QImage& gradient,
    const QString& transfer,
    const QString& label) {
  const auto source = create_video_fidelity_hdr_source(
      ffmpeg, directory, gradient, transfer);
  if (source.isEmpty()) {
    return false;
  }
  const auto metadata_json = process_stdout(
      ffprobe,
      {QStringLiteral("-v"), QStringLiteral("error"),
       QStringLiteral("-select_streams"), QStringLiteral("v:0"),
       QStringLiteral("-show_entries"),
       QStringLiteral("stream=color_primaries,color_transfer,color_space,pix_fmt"),
       QStringLiteral("-of"), QStringLiteral("json"), source});
  const auto metadata = QJsonDocument::fromJson(metadata_json.toUtf8())
                            .object().value(QStringLiteral("streams")).toArray();
  if (metadata.isEmpty()) {
    std::cerr << label.toStdString() << " source metadata could not be probed\n";
    return false;
  }
  const auto source_stream = metadata.first().toObject();
  if (source_stream.value(QStringLiteral("color_transfer")).toString() != transfer ||
      source_stream.value(QStringLiteral("color_primaries")).toString() !=
          QStringLiteral("bt2020") ||
      (source_stream.value(QStringLiteral("pix_fmt")).toString() !=
           QStringLiteral("yuv444p10le") &&
       source_stream.value(QStringLiteral("pix_fmt")).toString() !=
           QStringLiteral("yuv444p12le"))) {
    std::cerr << label.toStdString() << " source did not retain its 10-bit-or-better BT.2020 HDR signal\n";
    return false;
  }

  QString error;
  auto preview = decode_hdr_source_as_encoded_rgb(ffmpeg, source, transfer, &error);
  if (preview.isNull() || !photo_editor::desktop::timeline_hdr_sdr::apply_to_rec709_sdr(
                               &preview,
                               transfer == QStringLiteral("arib-std-b67"),
                               true,
                               QColorSpace(QColorSpace::SRgb))) {
    std::cerr << label.toStdString() << " native preview mapping could not be applied: "
              << error.toStdString() << '\n';
    return false;
  }
  if (!preview.save(QDir(directory).filePath(label + QStringLiteral("-preview-rec709.png")))) {
    std::cerr << label.toStdString() << " mapped preview image could not be retained\n";
    return false;
  }
  const auto* preview_ramp = reinterpret_cast<const QRgba64*>(preview.constScanLine(16));
  auto previous_luma = std::uint16_t{0};
  for (int x = 0; x < preview.width(); ++x) {
    const auto pixel = preview_ramp[x];
    const auto luma = static_cast<std::uint16_t>(
        std::max({pixel.red(), pixel.green(), pixel.blue()}));
    if (x > 0 && luma < previous_luma) {
      std::cerr << label.toStdString() << " mapped HDR gradient is not monotonic\n";
      return false;
    }
    previous_luma = luma;
  }
  const auto* preview_chroma = reinterpret_cast<const QRgba64*>(preview.constScanLine(80));
  if (!(preview_chroma[100].red() > preview_chroma[100].green()) ||
      !(preview_chroma[500].green() > preview_chroma[500].blue()) ||
      !(preview_chroma[900].blue() > preview_chroma[900].red())) {
    std::cerr << label.toStdString() << " mapped BT.2020 color patches lost their channel identity\n";
    return false;
  }

  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), label + QStringLiteral(" delivery fidelity")},
      {QStringLiteral("width"), kVideoFidelityWidth},
      {QStringLiteral("height"), kVideoFidelityHeight},
      {QStringLiteral("frameRate"), 30.0},
  });
  controller.addClipFromGallery(
      QStringLiteral("video:") + label,
      source,
      label + QStringLiteral(" HDR source"),
      1.0,
      QString{});
  if (!wait_for_media_analysis(&controller) ||
      controller.currentClip().value(QStringLiteral("sourceColorTransfer")).toString() != transfer ||
      controller.currentClip().value(QStringLiteral("sourceColorPrimaries")).toString() !=
          QStringLiteral("bt2020") ||
      !controller.setDeliverySetting(QStringLiteral("alpha"), true)) {
    std::cerr << label.toStdString() << " timeline did not retain its HDR source metadata or ProRes delivery setting\n";
    return false;
  }
  const auto output_path = QDir(directory).filePath(label + QStringLiteral("-delivery.mov"));
  if (!controller.renderCurrentTimeline(output_path)) {
    std::cerr << label.toStdString() << " ProRes export failed\n";
    return false;
  }
  const auto output_metadata_json = process_stdout(
      ffprobe,
      {QStringLiteral("-v"), QStringLiteral("error"),
       QStringLiteral("-select_streams"), QStringLiteral("v:0"),
       QStringLiteral("-show_entries"),
       QStringLiteral("stream=color_primaries,color_transfer,color_space,pix_fmt"),
       QStringLiteral("-of"), QStringLiteral("json"), output_path});
  const auto output_streams = QJsonDocument::fromJson(output_metadata_json.toUtf8())
                                  .object().value(QStringLiteral("streams")).toArray();
  if (output_streams.isEmpty()) {
    std::cerr << label.toStdString() << " delivery metadata could not be probed\n";
    return false;
  }
  const auto output_stream = output_streams.first().toObject();
  if (output_stream.value(QStringLiteral("color_primaries")).toString() !=
          QStringLiteral("bt709") ||
      output_stream.value(QStringLiteral("color_transfer")).toString() !=
          QStringLiteral("bt709") ||
      output_stream.value(QStringLiteral("color_space")).toString() !=
          QStringLiteral("bt709") ||
      (output_stream.value(QStringLiteral("pix_fmt")).toString() !=
           QStringLiteral("yuva444p10le") &&
       output_stream.value(QStringLiteral("pix_fmt")).toString() !=
           QStringLiteral("yuva444p12le"))) {
    std::cerr << label.toStdString() << " master did not deliver Rec.709 ProRes 4444 at 10-bit-or-better\n";
    return false;
  }

  const auto actual_bytes = decode_delivery_rgb64(ffmpeg, output_path, &error);
  const auto expected_size = static_cast<qsizetype>(kVideoFidelityWidth) *
                             kVideoFidelityHeight * 8;
  if (actual_bytes.size() != expected_size) {
    std::cerr << label.toStdString() << " delivery frame could not be decoded: "
              << error.toStdString() << '\n';
    return false;
  }
  auto actual = QImage(
      kVideoFidelityWidth, kVideoFidelityHeight, QImage::Format_RGBA64);
  if (actual.isNull()) {
    return false;
  }
  std::memcpy(actual.bits(), actual_bytes.constData(),
              static_cast<std::size_t>(expected_size));
  auto absolute_error_sum = std::uint64_t{0};
  auto maximum_error = std::uint16_t{0};
  auto channel_count = std::uint64_t{0};
  for (int y = 0; y < actual.height(); ++y) {
    const auto* actual_row = reinterpret_cast<const QRgba64*>(actual.constScanLine(y));
    const auto* preview_row = reinterpret_cast<const QRgba64*>(preview.constScanLine(y));
    for (int x = 0; x < actual.width(); ++x) {
      const auto actual_pixel = actual_row[x];
      const auto preview_pixel = preview_row[x];
      for (const auto [actual_channel, preview_channel] : {
               std::pair{actual_pixel.red(), preview_pixel.red()},
               std::pair{actual_pixel.green(), preview_pixel.green()},
               std::pair{actual_pixel.blue(), preview_pixel.blue()}}) {
        const auto error_value = static_cast<std::uint16_t>(
            std::abs(static_cast<int>(actual_channel) -
                     static_cast<int>(preview_channel)));
        absolute_error_sum += error_value;
        maximum_error = qMax(maximum_error, error_value);
        ++channel_count;
      }
    }
  }
  const auto mean_error = static_cast<double>(absolute_error_sum) /
                          static_cast<double>(channel_count) / 65535.0;
  if (mean_error > 0.004 || maximum_error > 1800U) {
    std::cerr << label.toStdString() << " preview/export color mismatch: mean="
              << mean_error << " max=" << maximum_error << '\n';
    return false;
  }

  const auto luma_levels = decode_delivery_luma_levels(ffmpeg, output_path, &error);
  if (luma_levels.size() <= 256) {
    std::cerr << label.toStdString() << " high precision composite retained only "
              << luma_levels.size() << " distinct luma levels\n";
    return false;
  }
  std::cout << label.toStdString() << " HDR preview/export meanRGBError="
            << mean_error << " maxRGBError=" << maximum_error
            << " retained10BitLevels=" << luma_levels.size() << '\n';
  gVideoFidelityResults.push_back(QJsonObject{
      {QStringLiteral("transfer"), transfer},
      {QStringLiteral("meanRgbError"), mean_error},
      {QStringLiteral("maxRgbError"), maximum_error},
      {QStringLiteral("retainedLumaLevels"), static_cast<int>(luma_levels.size())},
      {QStringLiteral("outputPixelFormat"), output_stream.value(QStringLiteral("pix_fmt")).toString()},
      {QStringLiteral("outputColorPrimaries"), output_stream.value(QStringLiteral("color_primaries")).toString()},
      {QStringLiteral("outputColorTransfer"), output_stream.value(QStringLiteral("color_transfer")).toString()},
      {QStringLiteral("outputColorSpace"), output_stream.value(QStringLiteral("color_space")).toString()},
  });
  return true;
}

}  // namespace

bool run_timeline_video_delivery_fidelity_workflow(const QString& directory) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
    std::cerr << "FFmpeg and ffprobe are required for video delivery fidelity verification\n";
    return false;
  }
  const auto gradient = video_fidelity_gradient_image();
  if (gradient.isNull()) {
    std::cerr << "could not allocate the 16-bit video fidelity gradient\n";
    return false;
  }
  const auto pq_ok = verify_hdr_delivery_matches_preview(
      ffmpeg, ffprobe, directory, gradient,
      QStringLiteral("smpte2084"), QStringLiteral("PQ"));
  const auto hlg_ok = pq_ok && verify_hdr_delivery_matches_preview(
      ffmpeg, ffprobe, directory, gradient,
      QStringLiteral("arib-std-b67"), QStringLiteral("HLG"));
  const auto artifacts_ok = retain_video_fidelity_artifacts(
      directory, pq_ok && hlg_ok);
  return pq_ok && hlg_ok && artifacts_ok;
}
