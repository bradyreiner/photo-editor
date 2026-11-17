#include "timeline_compound_render.h"

#include <cmath>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QStandardPaths>
#include <QVector>
#include <QUuid>

namespace photo_editor::desktop::timeline_compound {
namespace {

bool fail(QString* error_message, const QString& message) {
  if (error_message != nullptr) *error_message = message;
  return false;
}

bool canonical_nonexistent_path(const QString& path, QString* parent_path) {
  const QFileInfo info(path);
  if (path.trimmed().isEmpty() || !info.isAbsolute() || info.fileName().isEmpty() ||
      info.exists() || info.isSymLink()) {
    return false;
  }
  const auto parent = info.dir().canonicalPath();
  if (parent.isEmpty() || QDir(parent).filePath(info.fileName()) != path) return false;
  if (parent_path != nullptr) *parent_path = parent;
  return true;
}

}  // namespace

bool validate_task_output_paths(
    const QString& child_timeline_id,
    const QString& document_path,
    const QString& render_path,
    QString* error_message) {
  if (error_message != nullptr) error_message->clear();
  const auto normalized_id = child_timeline_id.trimmed().toLower();
  const QUuid id(normalized_id);
  const auto canonical_id = id.isNull()
      ? QString{}
      : id.toString(QUuid::WithoutBraces).toLower();
  if (canonical_id.isEmpty() || canonical_id != normalized_id) {
    return fail(error_message,
                QStringLiteral("A task-owned compound needs a canonical child timeline ID."));
  }

  QString document_parent;
  QString render_parent;
  const QFileInfo document_info(document_path);
  const QFileInfo render_info(render_path);
  if (!canonical_nonexistent_path(document_path, &document_parent) ||
      !canonical_nonexistent_path(render_path, &render_parent) ||
      document_parent != render_parent ||
      document_info.fileName() != QStringLiteral("compound-%1.json").arg(canonical_id) ||
      render_info.fileName() != QStringLiteral("compound-%1.mov").arg(canonical_id)) {
    return fail(error_message,
                QStringLiteral("Compound document and render paths must be new canonical files with the admitted child ID in their names."));
  }

  const auto partial_path = partial_render_path(render_path);
  const auto graph_path = filter_graph_sidecar_path(render_path);
  QString partial_parent;
  QString graph_parent;
  if (partial_path.isEmpty() ||
      !canonical_nonexistent_path(partial_path, &partial_parent) ||
      partial_parent != document_parent || graph_path.isEmpty() ||
      !canonical_nonexistent_path(graph_path, &graph_parent) ||
      graph_parent != document_parent) {
    return fail(error_message,
                QStringLiteral("The compound staging and graph paths must be absent in the same admitted output directory."));
  }
  return true;
}

QString partial_render_path(const QString& render_path) {
  const QFileInfo info(render_path);
  const auto suffix = info.suffix().toLower();
  if (!info.isAbsolute() || info.fileName().isEmpty() || suffix != QStringLiteral("mov")) {
    return {};
  }
  return QDir(info.dir().absolutePath()).filePath(
      info.completeBaseName() + QStringLiteral(".partial.") + suffix);
}

QString filter_graph_sidecar_path(const QString& render_path) {
  const QFileInfo info(render_path);
  if (!info.isAbsolute() || info.fileName().isEmpty() ||
      info.suffix().compare(QStringLiteral("mov"), Qt::CaseInsensitive) != 0) {
    return {};
  }
  return QDir(info.dir().absolutePath()).filePath(
      info.completeBaseName() + QStringLiteral(".ffgraph"));
}

bool verify_transparent_prores_render(
    const QString& render_path,
    const QSize& expected_size,
    QString* error_message) {
  if (error_message != nullptr) error_message->clear();
  const QFileInfo file_info(render_path);
  if (render_path.trimmed().isEmpty() || !file_info.isFile() || !file_info.isReadable() ||
      file_info.canonicalFilePath() != render_path || !expected_size.isValid() ||
      expected_size.isEmpty()) {
    return fail(error_message,
                QStringLiteral("The compound alpha render and expected sequence dimensions are unavailable."));
  }

  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffprobe.isEmpty()) {
    return fail(error_message, QStringLiteral("ffprobe is required to verify the compound alpha render."));
  }
  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(ffprobe, {
      QStringLiteral("-v"), QStringLiteral("error"),
      QStringLiteral("-select_streams"), QStringLiteral("v:0"),
      QStringLiteral("-show_entries"),
      QStringLiteral("stream=codec_name,width,height,pix_fmt:format=duration"),
      QStringLiteral("-of"), QStringLiteral("json"), render_path});
  if (!process.waitForStarted(3000)) {
    return fail(error_message, QStringLiteral("ffprobe could not start for the compound alpha readback."));
  }
  if (!process.waitForFinished(10000)) {
    process.kill();
    process.waitForFinished(3000);
    return fail(error_message, QStringLiteral("Compound alpha readback timed out."));
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    const auto detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
    return fail(error_message,
                detail.isEmpty() ? QStringLiteral("ffprobe could not read the compound cache.")
                                 : detail.right(800));
  }
  QJsonParseError parse_error;
  const auto report = QJsonDocument::fromJson(process.readAllStandardOutput(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !report.isObject()) {
    return fail(error_message, QStringLiteral("ffprobe returned an invalid compound cache report."));
  }
  const auto streams = report.object().value(QStringLiteral("streams")).toArray();
  if (streams.size() != 1 || !streams.first().isObject()) {
    return fail(error_message, QStringLiteral("The compound cache does not contain one readable video stream."));
  }
  const auto stream = streams.first().toObject();
  const auto codec = stream.value(QStringLiteral("codec_name")).toString();
  const auto pixel_format = stream.value(QStringLiteral("pix_fmt")).toString();
  if (codec != QStringLiteral("prores") ||
      stream.value(QStringLiteral("width")).toInt() != expected_size.width() ||
      stream.value(QStringLiteral("height")).toInt() != expected_size.height() ||
      !pixel_format.startsWith(QStringLiteral("yuva444p"))) {
    return fail(error_message,
                QStringLiteral("The compound cache must be ProRes 4444 with alpha at the sequence dimensions."));
  }

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    return fail(error_message, QStringLiteral("ffmpeg is required to verify actual compound alpha pixels."));
  }
  const auto duration_value = report.object()
                                  .value(QStringLiteral("format")).toObject()
                                  .value(QStringLiteral("duration"));
  bool duration_ok = false;
  const auto duration_seconds = duration_value.toString().toDouble(&duration_ok);
  if (!duration_ok || !std::isfinite(duration_seconds) || duration_seconds <= 0.0) {
    return fail(error_message, QStringLiteral("The compound cache has no readable duration for alpha sampling."));
  }
  const QVector<double> sample_times{
      0.0,
      duration_seconds * 0.5,
      qMax(0.0, duration_seconds - 0.1)};
  const auto expected_alpha_bytes =
      static_cast<qint64>(expected_size.width()) * expected_size.height();
  for (const auto sample_time : sample_times) {
    QProcess alpha_process;
    alpha_process.setProcessChannelMode(QProcess::SeparateChannels);
    alpha_process.start(ffmpeg, {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-ss"), QString::number(sample_time, 'f', 6),
        QStringLiteral("-i"), render_path,
        QStringLiteral("-map"), QStringLiteral("0:v:0"),
        QStringLiteral("-vf"), QStringLiteral("alphaextract"),
        QStringLiteral("-frames:v"), QStringLiteral("1"),
        QStringLiteral("-pix_fmt"), QStringLiteral("gray"),
        QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("pipe:1")});
    if (!alpha_process.waitForStarted(3000)) {
      return fail(error_message, QStringLiteral("ffmpeg could not start for compound alpha pixel verification."));
    }
    if (!alpha_process.waitForFinished(10000)) {
      alpha_process.kill();
      alpha_process.waitForFinished(3000);
      continue;
    }
    if (alpha_process.exitStatus() != QProcess::NormalExit || alpha_process.exitCode() != 0) {
      continue;
    }
    const auto alpha_bytes = alpha_process.readAllStandardOutput();
    if (alpha_bytes.size() != expected_alpha_bytes) continue;
    auto has_transparent_pixel = false;
    auto has_visible_pixel = false;
    for (const auto alpha_value : alpha_bytes) {
      const auto alpha = static_cast<unsigned char>(alpha_value);
      has_transparent_pixel = has_transparent_pixel || alpha < 250U;
      has_visible_pixel = has_visible_pixel || alpha > 5U;
      if (has_transparent_pixel && has_visible_pixel) break;
    }
    if (has_transparent_pixel && has_visible_pixel) return true;
  }
  return fail(error_message,
              QStringLiteral("The ProRes alpha plane decoded, but sampled compound frames did not contain both transparent and visible pixels."));
}

}  // namespace photo_editor::desktop::timeline_compound
