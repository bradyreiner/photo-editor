#include "photo_automation_bridge.h"

#include "agent_edit_controller.h"
#include "agent_mcp_client.h"
#include "agent_local_model.h"
#include "agent_mcp_metadata_projection.h"
#include "agent_edit_style.h"
#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_state.h"
#include "export_job_controller.h"
#include "photo_mcp_server.h"
#include "automation_capability_policy.h"
#include "photo_point_text.h"
#include "photo_text_style.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>

#include <QColor>
#include <QBuffer>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSet>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QUrl>

namespace {

QStringList captured_agent_failures;
void capture_agent_failure(QtMsgType type, const QMessageLogContext&, const QString& message) {
  if (type == QtWarningMsg && message.contains(QStringLiteral("agent.edit.run_failed")))
    captured_agent_failures.append(message);
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool wait_for_condition(
    const std::function<bool()>& condition,
    int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  return condition();
}

photo_editor::desktop::EditRecipeState
selected_adjustment_state(
    const photo_editor::desktop::
        CatalogAssetModel& catalog) {
  const auto decoded =
      photo_editor::desktop::
          parse_adjustment_patch_json(
              catalog
                  .selectedAdjustmentPatchJson());
  return decoded.state.value_or(
      photo_editor::desktop::
          EditRecipeState{});
}

photo_editor::desktop::CatalogAssetItem make_asset(
    const QString& asset_id,
    const QString& file_path,
    const QString& recipe_json) {
  photo_editor::desktop::CatalogAssetItem item;
  item.asset_id = asset_id;
  item.camera_label = QStringLiteral("Test Camera");
  item.file_path = file_path;
  item.capture_date = QStringLiteral("Jun 29, 2026");
  item.capture_date_value = QDate(2026, 6, 29);
  item.lens_model = QStringLiteral("RF 50mm F1.2L USM");
  item.iso_label = QStringLiteral("ISO 200");
  item.shutter_speed = QStringLiteral("1/500 s");
  item.aperture_label = QStringLiteral("f/2");
  item.imported_at = QStringLiteral("Jun 29, 2026  1:00 PM");
  item.imported_at_value = QDateTime(QDate(2026, 6, 29), QTime(13, 0));
  item.media_type = QStringLiteral("photo");
  item.adjustment_patch_json =
      photo_editor::desktop::
          canonical_asset_bound_adjustment_patch_json(
              recipe_json);
  item.online = true;
  return item;
}

bool write_fixture_image(
    const QString& file_path,
    const QSize& size = QSize(20, 20),
    bool include_clipped_highlight = false,
    bool include_clipped_shadow = false) {
  QImage image(size, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto clipped_base_region = include_clipped_highlight &&
          x >= image.width() * 86 / 100 && y >= image.height() * 75 / 100;
      const auto clipped_shadow_region = include_clipped_shadow &&
          x >= image.width() * 86 / 100 && y >= image.height() * 75 / 100;
      image.setPixelColor(x, y, clipped_base_region
          ? QColor(255, 255, 255)
          : clipped_shadow_region
              ? QColor(0, 0, 0)
              : QColor(88 + x % 120, 104 + y % 120, 118 + (x + y) % 100));
    }
  }
  const QFileInfo info(file_path);
  if (!QDir().mkpath(info.dir().absolutePath())) {
    return false;
  }
  return image.save(file_path, "PNG");
}

QVariantMap fixture_image_pixel_diagnostics(const QString& file_path) {
  auto image = QImage(file_path);
  if (image.isNull()) {
    return {{QStringLiteral("available"), false},
        {QStringLiteral("path"), file_path}};
  }
  image = image.convertToFormat(QImage::Format_ARGB32);
  qint64 highlighted_pixels = 0;
  qint64 shadowed_pixels = 0;
  int maximum_channel = 0;
  int minimum_channel = 255;
  const auto total_pixels = static_cast<qint64>(image.width()) * image.height();
  for (int y = 0; y < image.height(); ++y) {
    const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      const auto pixel = QColor(row[x]);
      maximum_channel = std::max(
          maximum_channel,
          std::max({pixel.red(), pixel.green(), pixel.blue()}));
      minimum_channel = std::min(
          minimum_channel,
          std::min({pixel.red(), pixel.green(), pixel.blue()}));
      if (pixel.red() >= 250 || pixel.green() >= 250 || pixel.blue() >= 250)
        ++highlighted_pixels;
      if (pixel.red() <= 5 && pixel.green() <= 5 && pixel.blue() <= 5)
        ++shadowed_pixels;
    }
  }
  return {
      {QStringLiteral("available"), true},
      {QStringLiteral("path"), file_path},
      {QStringLiteral("width"), image.width()},
      {QStringLiteral("height"), image.height()},
      {QStringLiteral("maximumChannel"), maximum_channel},
      {QStringLiteral("minimumChannel"), minimum_channel},
      {QStringLiteral("highlightedPixelCount"), highlighted_pixels},
      {QStringLiteral("highlightedPixelPercent"), total_pixels > 0
          ? static_cast<double>(highlighted_pixels) * 100.0 /
                static_cast<double>(total_pixels)
          : 0.0},
      {QStringLiteral("shadowedPixelCount"), shadowed_pixels},
      {QStringLiteral("shadowedPixelPercent"), total_pixels > 0
          ? static_cast<double>(shadowed_pixels) * 100.0 /
                static_cast<double>(total_pixels)
          : 0.0},
  };
}

QJsonObject read_json_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QJsonDocument::fromJson(file.readAll()).object();
}

QJsonObject send_local_request(const QString& endpoint, const QJsonObject& request) {
  QLocalSocket socket;
  socket.connectToServer(endpoint);
  if (!socket.waitForConnected(1000)) {
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), socket.errorString()}};
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
  socket.write("\n");
  socket.flush();
  socket.waitForBytesWritten(1000);

  QByteArray response;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (socket.bytesAvailable() > 0 || socket.waitForReadyRead(10)) {
      response.append(socket.readAll());
      if (response.contains('\n')) {
        break;
      }
    }
  }
  return QJsonDocument::fromJson(response).object();
}

QJsonObject send_tcp_request(const QString& endpoint, const QJsonObject& request) {
  const auto parts = endpoint.split(QStringLiteral(":"));
  if (parts.size() != 2) {
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("invalid endpoint")}};
  }
  auto ok = false;
  const auto port = parts.at(1).toUShort(&ok);
  if (!ok) {
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("invalid port")}};
  }

  QTcpSocket socket;
  socket.connectToHost(parts.at(0), port);
  if (!socket.waitForConnected(1000)) {
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), socket.errorString()}};
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
  socket.write("\n");
  socket.flush();
  socket.waitForBytesWritten(1000);

  QByteArray response;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (socket.bytesAvailable() > 0 || socket.waitForReadyRead(10)) {
      response.append(socket.readAll());
      if (response.contains('\n')) {
        break;
      }
    }
  }
  return QJsonDocument::fromJson(response).object();
}

QJsonObject send_mcp_http_request(
    const QString& endpoint,
    const QJsonObject& request,
    const QString& bearer_token,
    int* status_code,
    const QString& origin = QStringLiteral("http://127.0.0.1"),
    const QString& host_override = {}) {
  QUrl url(endpoint);
  if (!url.isValid() || url.host().isEmpty() || url.port() <= 0) {
    return QJsonObject{{QStringLiteral("error"), QStringLiteral("invalid mcp endpoint")}};
  }

  QTcpSocket socket;
  socket.connectToHost(url.host(), static_cast<quint16>(url.port()));
  if (!socket.waitForConnected(1000)) {
    return QJsonObject{{QStringLiteral("error"), socket.errorString()}};
  }

  const auto body = QJsonDocument(request).toJson(QJsonDocument::Compact);
  QByteArray http_request;
  http_request.append("POST ");
  http_request.append(url.path().isEmpty() ? QByteArrayLiteral("/mcp") : url.path().toUtf8());
  http_request.append(" HTTP/1.1\r\nHost: ");
  if (host_override.isEmpty()) {
    http_request.append(url.host().toUtf8());
    http_request.append(':');
    http_request.append(QByteArray::number(url.port()));
  } else {
    http_request.append(host_override.toUtf8());
  }
  if (!bearer_token.isEmpty()) {
    http_request.append("\r\nAuthorization: Bearer ");
    http_request.append(bearer_token.toUtf8());
  }
  if (!origin.isEmpty()) {
    http_request.append("\r\nOrigin: ");
    http_request.append(origin.toUtf8());
  }
  http_request.append("\r\nAccept: application/json, text/event-stream\r\nContent-Type: application/json\r\nContent-Length: ");
  http_request.append(QByteArray::number(body.size()));
  http_request.append("\r\n\r\n");
  http_request.append(body);
  socket.write(http_request);
  socket.flush();
  socket.waitForBytesWritten(1000);

  QByteArray response;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (socket.bytesAvailable() > 0 || socket.waitForReadyRead(10)) {
      response.append(socket.readAll());
      if (response.contains("\r\n\r\n")) {
        const auto header_end = response.indexOf("\r\n\r\n");
        const auto header = response.left(header_end);
        auto content_length = -1;
        for (const auto& line_bytes : header.split('\n')) {
          const auto line = QString::fromLatin1(line_bytes).trimmed();
          if (line.toLower().startsWith(QStringLiteral("content-length:"))) {
            auto ok = false;
            content_length = line.mid(QStringLiteral("content-length:").size()).trimmed().toInt(&ok);
            if (!ok) {
              content_length = -1;
            }
          }
        }
        if (content_length >= 0 && response.size() >= header_end + 4 + content_length) {
          break;
        }
      }
    }
  }
  const auto header_end = response.indexOf("\r\n\r\n");
  if (header_end < 0) {
    return QJsonObject{{QStringLiteral("error"), QStringLiteral("missing http response headers")}};
  }
  const auto status_line = QString::fromLatin1(response.left(response.indexOf('\n'))).trimmed();
  const auto status_parts = status_line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (status_code != nullptr && status_parts.size() >= 2) {
    *status_code = status_parts.at(1).toInt();
  }
  return QJsonDocument::fromJson(response.mid(header_end + 4)).object();
}

struct McpSseResponse final {
  int status_code = 0;
  QString content_type;
  QByteArray body;
};

McpSseResponse send_mcp_http_get(
    const QString& endpoint,
    const QString& bearer_token = {}) {
  McpSseResponse result;
  QUrl url(endpoint);
  if (!url.isValid() || url.host().isEmpty() || url.port() <= 0) {
    result.body = QByteArrayLiteral("invalid mcp endpoint");
    return result;
  }

  QTcpSocket socket;
  socket.connectToHost(url.host(), static_cast<quint16>(url.port()));
  if (!socket.waitForConnected(1000)) {
    result.body = socket.errorString().toUtf8();
    return result;
  }

  QByteArray http_request;
  http_request.append("GET ");
  http_request.append(url.path().isEmpty() ? QByteArrayLiteral("/mcp") : url.path().toUtf8());
  http_request.append(" HTTP/1.1\r\nHost: ");
  http_request.append(url.host().toUtf8());
  http_request.append(':');
  http_request.append(QByteArray::number(url.port()));
  if (!bearer_token.isEmpty()) {
    http_request.append("\r\nAuthorization: Bearer ");
    http_request.append(bearer_token.toUtf8());
  }
  http_request.append("\r\nOrigin: http://127.0.0.1\r\nAccept: text/event-stream\r\n\r\n");
  socket.write(http_request);
  socket.flush();
  socket.waitForBytesWritten(1000);

  QByteArray response;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (socket.bytesAvailable() > 0 || socket.waitForReadyRead(10)) {
      response.append(socket.readAll());
      if (response.contains("\r\n\r\n")) {
        const auto header_end = response.indexOf("\r\n\r\n");
        const auto header = response.left(header_end);
        auto content_length = -1;
        for (const auto& line_bytes : header.split('\n')) {
          const auto line = QString::fromLatin1(line_bytes).trimmed();
          if (line.toLower().startsWith(QStringLiteral("content-length:"))) {
            auto ok = false;
            content_length = line.mid(QStringLiteral("content-length:").size()).trimmed().toInt(&ok);
            if (!ok) {
              content_length = -1;
            }
          }
        }
        if (content_length >= 0 && response.size() >= header_end + 4 + content_length) {
          break;
        }
      }
    }
  }

  const auto header_end = response.indexOf("\r\n\r\n");
  if (header_end < 0) {
    result.body = response;
    return result;
  }
  const auto header = response.left(header_end);
  const auto status_line = QString::fromLatin1(header.left(header.indexOf('\n'))).trimmed();
  const auto status_parts = status_line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (status_parts.size() >= 2) {
    result.status_code = status_parts.at(1).toInt();
  }
  for (const auto& line_bytes : header.split('\n')) {
    const auto line = QString::fromLatin1(line_bytes).trimmed();
    if (line.toLower().startsWith(QStringLiteral("content-type:"))) {
      result.content_type = line.mid(QStringLiteral("content-type:").size()).trimmed();
    }
  }
  result.body = response.mid(header_end + 4);
  return result;
}

QJsonObject send_request(
    const photo_editor::desktop::PhotoAutomationBridge& bridge,
    const QJsonObject& session,
    const QJsonObject& request) {
  if (session.value(QStringLiteral("endpointKind")).toString() == QStringLiteral("in-memory")) {
    return bridge.handleJsonRequest(request);
  }
  const auto kind = session.value(QStringLiteral("endpointKind")).toString();
  const auto endpoint = session.value(QStringLiteral("endpoint")).toString();
  if (kind == QStringLiteral("tcp-localhost")) {
    const auto response = send_tcp_request(endpoint, request);
    return response.isEmpty() ? bridge.handleJsonRequest(request) : response;
  }
  const auto response = send_local_request(endpoint, request);
  return response.isEmpty() ? bridge.handleJsonRequest(request) : response;
}

bool is_proof_critique_request(const QJsonObject& request) {
  const auto messages = request.value(QStringLiteral("messages")).toArray();
  if (messages.size() < 2) return false;
  const auto system_prompt = messages.first().toObject()
      .value(QStringLiteral("content")).toString();
  if (!system_prompt.contains(QStringLiteral("Workmanship contract (looksy-workmanship-v1)")))
    return false;
  const auto schema = request.value(QStringLiteral("format")).toObject();
  const auto properties = schema.value(QStringLiteral("properties")).toObject();
  const auto required = schema.value(QStringLiteral("required")).toArray();
  const auto required_fields = [&required](const QString& field) {
    return std::any_of(required.cbegin(), required.cend(), [&field](const QJsonValue& value) {
      return value.toString() == field;
    });
  };
  return schema.value(QStringLiteral("type")).toString() == QStringLiteral("object") &&
      properties.value(QStringLiteral("pass")).toObject()
          .value(QStringLiteral("type")).toString() == QStringLiteral("boolean") &&
      properties.value(QStringLiteral("issues")).toObject()
          .value(QStringLiteral("type")).toString() == QStringLiteral("array") &&
      properties.value(QStringLiteral("repairHints")).toObject()
          .value(QStringLiteral("type")).toString() == QStringLiteral("array") &&
      required_fields(QStringLiteral("pass")) &&
      required_fields(QStringLiteral("issues")) &&
      required_fields(QStringLiteral("repairHints"));
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_DATABASE_URL")) {
    qputenv("PHOTO_EDITOR_DATABASE_URL",
        QByteArrayLiteral("postgresql://127.0.0.1:1/photo_automation_bridge_fixture"));
  }
  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("PhotoAutomationBridgeTest"));

  QVariantList native_font_faces;
  for (int index = 0; index < 181; ++index) {
    native_font_faces.append(QVariantMap{
        {QStringLiteral("family"), QStringLiteral("Native Family %1").arg(index)},
        {QStringLiteral("faces"), QStringList{QStringLiteral("Regular"), QStringLiteral("Bold")}},
    });
  }
  const QVariantMap native_font_readback{
      {QStringLiteral("ok"), true},
      {QStringLiteral("assetId"), QStringLiteral("asset-1")},
      {QStringLiteral("fontFaces"), native_font_faces},
      {QStringLiteral("fontAxes"), QVariantMap{{QStringLiteral("wght"), QVariantMap{
           {QStringLiteral("minimum"), 100.0}, {QStringLiteral("maximum"), 900.0}}}}},
      {QStringLiteral("layers"), QVariantList{QVariantMap{
           {QStringLiteral("id"), QStringLiteral("text-layer")},
           {QStringLiteral("kind"), QStringLiteral("text")},
           {QStringLiteral("thumbnailDiagnostics"), QString(30000, QLatin1Char('x'))}}}},
      {QStringLiteral("displayTextColorPolicy"), QVariantMap{
           {QStringLiteral("renderColorDomain"), QStringLiteral("display-linear")},
           {QStringLiteral("displayReferredTextEligibility"), QStringLiteral("eligible-sdr-top-text-suffix")},
           {QStringLiteral("displayReferredTextEligibilityReason"), QStringLiteral("fixture eligibility proof")},
           {QStringLiteral("displayReferredTextPolicyVersion"), QStringLiteral("display-referred-text-suffix-v1")},
           {QStringLiteral("limits"), QStringLiteral("eligible SDR top text layers only")},
           {QStringLiteral("eligibleLayerIds"), QStringList{QStringLiteral("text-layer")}}}},
  };
  const auto duplicate_font_payload = QString::fromUtf8(
      QJsonDocument::fromVariant(native_font_readback).toJson(QJsonDocument::Compact));
  const QString extra_readback_diagnostic =
      QStringLiteral("Native font inventory diagnostic: platform database is ready.");
  const QVariantList original_mcp_content{
      QVariantMap{{QStringLiteral("type"), QStringLiteral("text")},
                  {QStringLiteral("text"), duplicate_font_payload}},
      QVariantMap{{QStringLiteral("type"), QStringLiteral("text")},
                  {QStringLiteral("text"), extra_readback_diagnostic}},
      QVariantMap{{QStringLiteral("type"), QStringLiteral("image")},
                  {QStringLiteral("data"), QStringLiteral("binary fixture")}},
  };
  const QVariantMap original_font_inspection{
      {QStringLiteral("toolName"), QStringLiteral("photo.inspect_composition")},
      {QStringLiteral("ok"), true},
      {QStringLiteral("structuredContent"), native_font_readback},
      {QStringLiteral("content"), original_mcp_content},
  };
  const auto projected_font_inspection =
      photo_editor::desktop::agent_detail::proof_critique_composition_inspection_projection(
          original_font_inspection);
  const auto projected_font_content = projected_font_inspection
                                          .value(QStringLiteral("structuredContent"))
                                          .toMap();
  const auto projected_mcp_content = projected_font_inspection
                                         .value(QStringLiteral("content"))
                                         .toList();
  if (projected_font_content.value(QStringLiteral("fontFaces")).toList() != native_font_faces ||
      projected_font_content.value(QStringLiteral("fontAxes")).toMap() !=
          native_font_readback.value(QStringLiteral("fontAxes")).toMap() ||
      projected_font_content.value(QStringLiteral("displayTextColorPolicy")).toMap() !=
          native_font_readback.value(QStringLiteral("displayTextColorPolicy")).toMap() ||
      projected_font_content.value(QStringLiteral("layers")).toList().first().toMap()
          .contains(QStringLiteral("thumbnailDiagnostics")) ||
      projected_mcp_content.size() != 1 ||
      projected_mcp_content.first().toMap().value(QStringLiteral("text")).toString() !=
          extra_readback_diagnostic ||
      original_font_inspection.value(QStringLiteral("content")).toList() != original_mcp_content) {
    return fail("Proof metadata must retain the full native font/color semantics, remove only duplicate JSON transport, and keep extra text diagnostics");
  }
  const QString used_late_font = QStringLiteral("Native Family 170");
  auto used_font_structured = native_font_readback;
  used_font_structured.insert(QStringLiteral("fonts"), [&]() {
    QStringList families;
    for (int index = 0; index < 181; ++index)
      families.append(QStringLiteral("Native Family %1").arg(index));
    return families;
  }());
  used_font_structured.insert(QStringLiteral("layers"), QVariantList{QVariantMap{
      {QStringLiteral("id"), QStringLiteral("used-font-layer")},
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("text"), QStringLiteral("Visible headline")},
      {QStringLiteral("textTypography"), QVariantMap{{QStringLiteral("family"), used_late_font},
          {QStringLiteral("face"), QStringLiteral("Bold")}}},
      {QStringLiteral("thumbnailDiagnostics"), QString(30000, QLatin1Char('x'))},
  }});
  auto used_font_inspection = original_font_inspection;
  used_font_inspection.insert(QStringLiteral("structuredContent"), used_font_structured);
  used_font_inspection.insert(QStringLiteral("content"), QVariantList{});
  const auto projected_used_font = photo_editor::desktop::agent_detail::
      proof_critique_composition_inspection_projection(used_font_inspection)
          .value(QStringLiteral("structuredContent")).toMap();
  const auto projected_used_faces = projected_used_font.value(QStringLiteral("fontFaces")).toList();
  const auto projected_used_layer = projected_used_font.value(QStringLiteral("layers"))
      .toList().value(0).toMap();
  if (projected_used_faces.size() != 1 ||
      projected_used_faces.first().toMap().value(QStringLiteral("family")).toString() != used_late_font ||
      projected_used_faces.first().toMap().value(QStringLiteral("faces")).toStringList() !=
          QStringList{QStringLiteral("Regular"), QStringLiteral("Bold")} ||
      !projected_used_font.value(QStringLiteral("unavailableUsedFontFamilies")).toStringList().isEmpty() ||
      projected_used_font.value(QStringLiteral("fontInventoryProjection")).toString() !=
          QStringLiteral("all-native-faces-for-every-used-text-family") ||
      projected_used_layer.value(QStringLiteral("text")).toString() != QStringLiteral("Visible headline") ||
      projected_used_layer.value(QStringLiteral("textTypography")).toMap()
          .value(QStringLiteral("family")).toString() != used_late_font ||
      projected_used_layer.contains(QStringLiteral("thumbnailDiagnostics")))
    return fail("Composition proof projection must retain every native face for a used font beyond the old inventory cap and preserve its layer semantics");
  auto missing_font_structured = used_font_structured;
  auto missing_font_layers = missing_font_structured.value(QStringLiteral("layers")).toList();
  auto missing_font_layer = missing_font_layers.first().toMap();
  missing_font_layer.insert(QStringLiteral("textTypography"),
      QVariantMap{{QStringLiteral("family"), QStringLiteral("Uninstalled Native Family")}});
  missing_font_layers[0] = missing_font_layer;
  missing_font_structured.insert(QStringLiteral("layers"), missing_font_layers);
  auto missing_font_inspection = used_font_inspection;
  missing_font_inspection.insert(QStringLiteral("structuredContent"), missing_font_structured);
  const auto projected_missing_font = photo_editor::desktop::agent_detail::
      proof_critique_composition_inspection_projection(missing_font_inspection)
          .value(QStringLiteral("structuredContent")).toMap();
  if (!projected_missing_font.value(QStringLiteral("fontFaces")).toList().isEmpty() ||
      projected_missing_font.value(QStringLiteral("unavailableUsedFontFamilies")).toStringList() !=
          QStringList{QStringLiteral("Uninstalled Native Family")})
    return fail("Composition proof projection must preserve an explicit unavailable status for an uninstalled used font");

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  const auto model_manifest_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("agent-models/models.json"));
  if (!QDir().mkpath(QFileInfo(model_manifest_path).dir().absolutePath())) {
    return fail("agent model manifest directory should be created");
  }
  QFile model_manifest_file(model_manifest_path);
  if (!model_manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return fail("agent model manifest should be writable");
  }
  model_manifest_file.write(QJsonDocument(QJsonObject{
      {QStringLiteral("models"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("id"), QStringLiteral("ollama-qwen-tool-plan-test")},
               {QStringLiteral("name"), QStringLiteral("Qwen tool-plan test via Ollama")},
               {QStringLiteral("backend"), QStringLiteral("ollama")},
               {QStringLiteral("endpoint"), QStringLiteral("http://127.0.0.1:11434")},
               {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
               {QStringLiteral("vision"), true},
               {QStringLiteral("contextWindow"), 24576},
               {QStringLiteral("available"), true},
           },
       }},
  }).toJson(QJsonDocument::Compact));
  model_manifest_file.close();

  const auto source_path = QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source.png"));
  if (!write_fixture_image(source_path)) {
    return fail("fixture image should be written");
  }
  const auto second_source_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source-2.png"));
  if (!write_fixture_image(second_source_path)) {
    return fail("second fixture image should be written");
  }

  photo_editor::desktop::EditRecipeState original_state;
  original_state.output_color_space = QStringLiteral("Display P3");
  const auto original_recipe = photo_editor::desktop::serialize_edit_recipe_json(original_state);

  photo_editor::desktop::CatalogAssetModel catalog_model;
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);

  photo_editor::desktop::AgentEditController agent_edit;
  if (agent_edit.runTimeoutMs() != -1)
    return fail("agent runs must default to waiting until completion or cancellation");
  photo_editor::desktop::ExportJobController export_jobs;
  agent_edit.setCatalogModel(&catalog_model);
  agent_edit.setExportJobController(&export_jobs);
  agent_edit.setSelectedModelId(QStringLiteral("rulebook-deterministic"));

  photo_editor::desktop::PhotoAutomationBridge disabled_bridge(&agent_edit, &catalog_model);
  if (app.arguments().contains(QStringLiteral("--internal-tools-contract"))) {
    int composition_calls = 0;
    disabled_bridge.setCompositionHandler([&](const QVariantMap& request) -> QVariantMap {
      ++composition_calls;
      return {{"ok", true}, {"operation", request.value("operation")}};
    });
    photo_editor::desktop::AgentMcpClient client;
    client.setEndpoint(disabled_bridge.agentMcpEndpoint());
    const auto tools = client.refreshToolCache("internal-agent");
    if (!tools.value("ok").toBool() ||
        !client.hasCachedTool("photo.edit_composition") ||
        client.hasCachedTool("photo.commit_agent_draft") ||
        client.hasCachedTool("photo.export_batch") ||
        !disabled_bridge.mcpEndpoint().isEmpty())
      return fail("Internal agent must discover draft composition tools without external automation or commit/export access");
    const QVariantMap args{{"assetId", "asset-1"}, {"operation", "create_text"},
                           {"values", QVariantMap{{"text", "brady reiner"}}}};
    const auto edit = client.callTool("photo.edit_composition", args);
    if (!edit.value("ok").toBool() || composition_calls != 1)
      return fail("Internal composition tools must dispatch typed arguments to the workspace");
    disabled_bridge.setEditAccessPolicy([] { return false; });
    if (client.callTool("photo.edit_composition", args).value("ok").toBool() || composition_calls != 1)
      return fail("Internal tools must retain customer edit admission");
    disabled_bridge.setEditAccessPolicy([] { return true; });
    client.setAllowedTools({"photo.inspect_composition"});
    if (!client.callTool("photo.edit_composition", args).value("blockedByAllowlist").toBool())
      return fail("Internal tools must retain per-phase allowlists");
    client.clearAllowedTools();
    disabled_bridge.setCompositionHandler([](const QVariantMap&) -> QVariantMap {
      return {{"ok", false}, {"error", "Unknown composition argument: fontFamily"}};
    });
    const auto failed_inspect = client.callTool("photo.inspect_composition", {{"assetId", "asset-1"}});
    if (failed_inspect.value("ok").toBool() ||
        failed_inspect.value("error").toString() != "Unknown composition argument: fontFamily")
      return fail("The client must preserve the composition tool's specific failure reason");
    client.setCancellationPredicate([] { return true; });
    if (!client.callTool("photo.edit_composition", args).value("canceled").toBool() || composition_calls != 1)
      return fail("Internal tools must retain cancellation");
    photo_editor::desktop::AgentMcpClient expired;
    {
      photo_editor::desktop::PhotoAutomationBridge temporary(&agent_edit, &catalog_model);
      expired.setEndpoint(temporary.agentMcpEndpoint());
    }
    if (expired.refreshToolCache().value("ok").toBool())
      return fail("Destroyed workspace endpoints must be unavailable");
    agent_edit.setManualEditingActive(true);
    const auto previous_handler = qInstallMessageHandler(capture_agent_failure);
    const auto paused_report = agent_edit.runDraftForScope(QStringLiteral("current-photo"));
    qInstallMessageHandler(previous_handler);
    agent_edit.setManualEditingActive(false);
    if (paused_report.value("ok").toBool() || captured_agent_failures.size() != 1 ||
        !captured_agent_failures.first().contains(paused_report.value("failureReason").toString()))
      return fail("The controller must log the specific failed-run reason without requiring the details UI");
    std::cout << "Internal photo tools work without external automation and preserve admission, allowlists, cancellation and endpoint lifetime.\n";
    return EXIT_SUCCESS;
  }
  if (disabled_bridge.start()) {
    if (!disabled_bridge.mcpEndpoint().isEmpty()) {
      return fail("MCP HTTP must remain disabled without explicit automation policy");
    }
    const auto disabled_session = read_json_file(disabled_bridge.sessionFilePath());
    if (disabled_session.value(QStringLiteral("mcpTransport")).toString() !=
        QStringLiteral("disabled")) {
      std::cerr << QJsonDocument(disabled_session).toJson(QJsonDocument::Compact).toStdString() << '\n';
      return fail("disabled automation session should advertise no MCP HTTP transport");
    }
  } else if (!disabled_bridge.errorMessage().contains(
                 QStringLiteral("listen"), Qt::CaseInsensitive)) {
    std::cerr << disabled_bridge.errorMessage().toStdString() << '\n';
    return fail("disabled automation bridge should fail only under listener restrictions");
  }
  disabled_bridge.stop();

  photo_editor::desktop::PhotoAutomationBridge bridge(&agent_edit, &catalog_model);
  photo_editor::desktop::PhotoMcpServer mcp_server(&bridge);
  bridge.setCompositionHandler([](const QVariantMap& request) -> QVariantMap {
    if (request.value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-1") ||
        request.value(QStringLiteral("operation")).toString() != QStringLiteral("inspect"))
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("Unexpected initial composition observation.")}};
    return {{QStringLiteral("ok"), true},
        {QStringLiteral("assetId"), QStringLiteral("asset-1")},
        {QStringLiteral("width"), 640}, {QStringLiteral("height"), 480},
        {QStringLiteral("layers"), QVariantList{}}, {QStringLiteral("masks"), QVariantList{}},
        {QStringLiteral("fontFaces"), QVariantList{QVariantMap{
            {QStringLiteral("family"), QStringLiteral("Fixture Serif")},
            {QStringLiteral("faces"), QStringList{QStringLiteral("Regular"), QStringLiteral("Bold")}}}}},
        {QStringLiteral("fontAxes"), QVariantMap{}}};
  });
  if (app.arguments().contains(QStringLiteral("--customer-access"))) {
    const QStringList capabilities{"read", "draft", "export", "commit"};
    if (!bridge.start(false, capabilities)) return fail("customer automation proof needs its local listener");
    agent_edit.setAccessPolicy([] { return false; });
    bridge.setEditAccessPolicy([] { return false; });
    const auto start = agent_edit.startDraftForScope("selected-assets", "Increase exposure");
    const auto run = agent_edit.runDraftForScope("selected-assets", "Increase exposure");
    const auto patch = agent_edit.applyDraftRecipePatchForScope("selected-assets", {});
    for (const auto& report : {start, run, patch}) {
      if (report.value("errorCode").toString() != "access_required") return fail("native AI edit entry points must enforce access");
    }
    const auto request = [&](const QString& command) {
      return bridge.handleJsonRequest({{"token", bridge.token()}, {"command", command}, {"args", QJsonObject{}}});
    };
    if (!request("photo_live_status").value("ok").toBool()) return fail("expired automation can read existing work");
    const auto denied = request("photo_apply_recipe_patch_draft");
    if (!denied.value("error").toString().contains("active trial")) return fail("automation cannot bypass expired editing access");
    for (const auto& command : {QStringLiteral("photo_export_preflight"), QStringLiteral("photo_commit_agent_draft")}) {
      if (request(command).value("error").toString().contains("active trial")) return fail("export and saving existing drafts remain available");
    }
    const auto mcp = mcp_server.callTool("photo.apply_recipe_patch_draft", {}, capabilities);
    if (!mcp.value("isError").toBool() || !mcp.value("structuredContent").toObject().value("error").toString().contains("active trial"))
      return fail("MCP must enforce the same customer access boundary");
    bridge.stop();
    std::cout << "Native AI and automation reject new edits while read/export/save remain admitted.\n";
    return EXIT_SUCCESS;
  }
  if (app.arguments().contains(QStringLiteral("--composition-tools-contract"))) {
    if (!bridge.start(true, {"read", "draft"})) return fail("Composition proof needs its MCP HTTP listener");
    int calls = 0;
    QVariantMap last;
    bridge.setCompositionHandler([&](const QVariantMap& request) -> QVariantMap {
      ++calls; last = request; return {{"ok", true}, {"layers", QVariantList{}}};
    });
    const QStringList read{QStringLiteral("read")};
    const QStringList draft{QStringLiteral("read"), QStringLiteral("draft")};
    auto result = mcp_server.callTool("photo.get_recipe_patch_schema", QJsonObject{}, read);
    if (result.value("isError").toBool())
      return fail("Recipe patch schema must be readable through MCP");
    const auto recipe_schema = result.value("structuredContent").toObject();
    const auto effect_changes_schema = recipe_schema.value("groups").toObject()
        .value("layers").toObject().value("fields").toObject()
        .value("effectChanges").toObject();
    const auto effect_types = effect_changes_schema.value("allowedTypes").toArray();
    if (!effect_types.contains("dragged_shutter") || !effect_types.contains("optical_diffusion"))
      return fail("MCP recipe schema must advertise typed creative photo effects");
    const QJsonObject diffusion_change{{"layerId", "photo-layer"}, {"effectId", "agent-effect-diffusion"},
        {"type", "optical_diffusion"}, {"enabled", true}, {"amount", 1.0},
        {"diffusionRadius", 26.0}, {"highlightThreshold", 0.73},
        {"highlightKnee", 0.18}, {"diffusionStrength", 0.68},
        {"warmth", 0.14}, {"highlightProtection", 0.22}};
    const QJsonObject diffusion_patch{{"layers", QJsonObject{{"effectChanges",
        QJsonArray{diffusion_change}}}}};
    agent_edit.setEditingFreedom(QStringLiteral("transform"));
    result = mcp_server.callTool("photo.validate_recipe_patch", QJsonObject{{"patch", diffusion_patch}}, read);
    if (result.value("isError").toBool() ||
        !result.value("structuredContent").toObject().value("ok").toBool()) {
      return fail("MCP recipe validation must accept all typed Optical Diffusion controls");
    }
    result = mcp_server.callTool("photo.apply_recipe_patch_draft",
        QJsonObject{{"patch", diffusion_patch}}, draft);
    if (result.value("isError").toBool() ||
        !result.value("structuredContent").toObject().value("ok").toBool() ||
        !agent_edit.draftActive())
      return fail("MCP must apply Optical Diffusion through the reversible recipe-draft path");
    result = mcp_server.callTool("photo.rollback_agent_draft", QJsonObject{}, draft);
    if (result.value("isError").toBool() ||
        !result.value("structuredContent").toObject().value("ok").toBool() ||
        agent_edit.draftActive())
      return fail("MCP must roll back the Optical Diffusion draft without leaving an edit active");
    agent_edit.setEditingFreedom(QStringLiteral("balanced"));
    const QJsonObject shutter_change{{"layerId", "photo-layer"}, {"effectId", "agent-effect-shutter"},
        {"type", "dragged_shutter"}, {"enabled", true}, {"amount", 1.0},
        {"distance", 64.0}, {"angle", 12.0}, {"falloff", 0.7},
        {"highlightWeighting", 0.4}, {"protectSubject", true}, {"maskId", "subject-mask"}};
    const QJsonObject shutter_patch{{"layers", QJsonObject{{"effectChanges",
        QJsonArray{shutter_change}}}}};
    agent_edit.setEditingFreedom(QStringLiteral("transform"));
    result = mcp_server.callTool("photo.validate_recipe_patch", QJsonObject{{"patch", shutter_patch}}, read);
    if (result.value("isError").toBool() ||
        !result.value("structuredContent").toObject().value("ok").toBool()) {
      return fail("MCP recipe validation must accept the protected-subject Dragged Shutter recipe");
    }
    agent_edit.setEditingFreedom(QStringLiteral("balanced"));
    const QJsonObject inspect{{"assetId", "photo"}};
    result = mcp_server.callTool("photo.inspect_composition", inspect, read);
    if (result.value("isError").toBool() || calls != 1 || last.value("operation") != "inspect")
      return fail("Composition inspection must reach the open editor through read capability");
    const QJsonObject inspect_axes{{"assetId", "photo"},
        {"values", QJsonObject{{"family", "Georgia"}, {"face", "Regular"}}}};
    result = mcp_server.callTool("photo.inspect_composition", inspect_axes, read);
    if (result.value("isError").toBool() || calls != 2 ||
        last.value("operation") != "inspect" ||
        last.value("values").toMap().value("family").toString() != "Georgia")
      return fail("Composition inspection must forward the requested font face for axis discovery");
    const QJsonObject edit{{"assetId", "photo"}, {"operation", "add_text"},
        {"values", QJsonObject{{"text", "FLOAT"}, {"x", 10}, {"y", 20}}}};
    result = mcp_server.callTool("photo.edit_composition", edit, read);
    if (!result.value("isError").toBool() || calls != 2)
      return fail("Read capability must not allow composition writes");
    result = mcp_server.callTool("photo.edit_composition", edit, draft);
    if (result.value("isError").toBool() || calls != 3 || last.value("operation") != "add_text")
      return fail("Composition edits must forward typed values to shared editor commands");
    const QJsonObject independent_copy{{"assetId", "photo"}, {"operation", "copy_selection"},
        {"values", QJsonObject{{"maskId", "subject-mask"}, {"independent", true}}}};
    result = mcp_server.callTool("photo.edit_composition", independent_copy, draft);
    if (result.value("isError").toBool() || calls != 4 || last.value("operation") != "copy_selection" ||
        !last.value("values").toMap().value("independent").toBool())
      return fail("MCP must forward the explicit independent selection-copy mode");
    const QJsonObject light_paint{{"assetId", "photo"}, {"operation", "paint"},
        {"layerId", "paint-layer"}, {"values", QJsonObject{
            {"points", QJsonArray{QJsonObject{{"x", 20}, {"y", 30}, {"pressure", 0.8}}}},
            {"textureMaterial", "light_paint"}, {"lightFalloff", 0.7},
            {"lightExposure", 3.0}, {"lightTaper", 0.45}, {"lightAccumulation", false}}}};
    result = mcp_server.callTool("photo.edit_composition", light_paint, draft);
    if (result.value("isError").toBool() || calls != 5 || last.value("operation") != "paint" ||
        last.value("values").toMap().value("textureMaterial").toString() != "light_paint" ||
        last.value("values").toMap().value("lightExposure").toDouble() != 3.0 ||
        last.value("values").toMap().value("lightTaper").toDouble() != 0.45 ||
        last.value("values").toMap().value("lightAccumulation").toBool())
      return fail("MCP must forward typed HDR light-paint controls to the shared paint operation");
    const QJsonObject text_path{{"assetId", "photo"}, {"operation", "text_path"},
        {"layerId", "text-layer"}, {"values", QJsonObject{{"maskId", "subject-mask"}, {"contourIndex", 1}}}};
    result = mcp_server.callTool("photo.edit_composition", text_path, draft);
    if (result.value("isError").toBool() || calls != 6 || last.value("operation") != "text_path" ||
        last.value("values").toMap().value("maskId").toString() != "subject-mask" ||
        last.value("values").toMap().value("contourIndex").toInt() != 1)
      return fail("MCP must forward the selected contour index to editable typography");
    const QJsonArray contours{QJsonArray{QJsonObject{{"x", 0.2}, {"y", 0.2}},
            QJsonObject{{"x", 0.8}, {"y", 0.2}}, QJsonObject{{"x", 0.8}, {"y", 0.8}}},
        QJsonArray{QJsonObject{{"x", 0.4}, {"y", 0.4}}, QJsonObject{{"x", 0.6}, {"y", 0.4}},
            QJsonObject{{"x", 0.6}, {"y", 0.6}}}};
    const QJsonObject path_flow{{"assetId", "photo"}, {"operation", "typography"},
        {"layerId", "text-layer"}, {"values", QJsonObject{{"pathExclusion", true}, {"pathContours", contours}}}};
    result = mcp_server.callTool("photo.edit_composition", path_flow, draft);
    if (result.value("isError").toBool() || calls != 7 || last.value("operation") != "typography" ||
        !last.value("values").toMap().value("pathExclusion").toBool() ||
        last.value("values").toMap().value("pathContours").toList().size() != 2)
      return fail("MCP must forward typed multi-contour paragraph-exclusion controls");
    const QJsonObject material_text{{"assetId", "photo"}, {"operation", "typography"},
        {"layerId", "text-layer"}, {"values", QJsonObject{{"material", "frosted_glass"},
            {"materialTint", "#ffb5cbd8"}, {"materialBlur", 8.0}, {"materialTransmission", 0.74},
            {"materialEdgeSoftness", 1.5}, {"materialDepth", 6.0}, {"materialLightAngle", -35.0}}}};
    result = mcp_server.callTool("photo.edit_composition", material_text, draft);
    if (result.value("isError").toBool() || calls != 8 || last.value("operation") != "typography" ||
        last.value("values").toMap().value("material").toString() != "frosted_glass" ||
        last.value("values").toMap().value("materialBlur").toDouble() != 8.0 ||
        last.value("values").toMap().value("materialLightAngle").toDouble() != -35.0)
      return fail("MCP must forward typed editable material-text controls");
    const QJsonObject layer_effect{{"assetId", "photo"}, {"operation", "add_effect"},
        {"layerId", "text-layer"}, {"values", QJsonObject{
            {"effectType", "optical_diffusion"}, {"radius", 18.0},
            {"highlightThreshold", 0.8}, {"highlightKnee", 0.25}, {"strength", 0.6},
            {"warmth", 0.15}, {"highlightProtection", 0.25}, {"amount", 0.4},
            {"enabled", true}}}};
    result = mcp_server.callTool("photo.edit_composition", layer_effect, draft);
    if (result.value("isError").toBool() || calls != 9 ||
        last.value("operation") != "add_effect" ||
        last.value("values").toMap().value("effectType").toString() != "optical_diffusion" ||
        last.value("values").toMap().value("highlightProtection").toDouble() != 0.25)
      return fail("MCP must expose typed registered photo-layer effects on editable content");
    auto invalid = edit; invalid.insert("values", "invalid");
    result = mcp_server.callTool("photo.edit_composition", invalid, draft);
    if (!result.value("isError").toBool() || calls != 9)
      return fail("Invalid composition values must never reach the editor");
    bridge.setEditAccessPolicy([] { return false; });
    result = mcp_server.callTool("photo.edit_composition", edit, draft);
    if (!result.value("isError").toBool() || calls != 9)
      return fail("Composition edits must respect customer edit access");
    bridge.setEditAccessPolicy([] { return true; });
    photo_editor::desktop::AgentMcpClient live_composition_client;
    live_composition_client.setEndpoint(bridge.mcpEndpoint());
    live_composition_client.setBearerToken(bridge.token());
    live_composition_client.setTimeoutMs(3000);
    const auto live_tool_cache = live_composition_client.refreshToolCache(
        QStringLiteral("creative-composition-live-mcp"));
    if (!live_tool_cache.value(QStringLiteral("ok")).toBool() ||
        !live_composition_client.hasCachedTool(QStringLiteral("photo.inspect_composition")) ||
        !live_composition_client.hasCachedTool(QStringLiteral("photo.edit_composition"))) {
      std::cerr << "live MCP composition tools: ok="
                << live_tool_cache.value(QStringLiteral("ok")).toBool()
                << " count=" << live_tool_cache.value(QStringLiteral("toolCount")).toInt()
                << " names=" << live_composition_client.cachedToolNames().join(',').toStdString()
                << " error=" << live_tool_cache.value(QStringLiteral("error")).toString().toStdString()
                << '\n';
      return fail("Live MCP transport must publish inspect and edit composition tools");
    }
    QVariantMap live_edit_tool;
    QVariantMap live_inspect_tool;
    for (const auto& tool : live_tool_cache.value(QStringLiteral("tools")).toList()) {
      const auto tool_map = tool.toMap();
      if (tool_map.value(QStringLiteral("name")).toString() == QStringLiteral("photo.edit_composition")) {
        live_edit_tool = tool_map;
      } else if (tool_map.value(QStringLiteral("name")).toString() == QStringLiteral("photo.inspect_composition")) {
        live_inspect_tool = tool_map;
      }
    }
    const auto inspect_schema_properties = live_inspect_tool.value(QStringLiteral("inputSchema")).toMap()
        .value(QStringLiteral("properties")).toMap();
    const auto inspect_values_properties = inspect_schema_properties.value(QStringLiteral("values")).toMap()
        .value(QStringLiteral("properties")).toMap();
    const auto inspect_values_required = live_inspect_tool.value(QStringLiteral("inputSchema")).toMap()
        .value(QStringLiteral("properties")).toMap().value(QStringLiteral("values")).toMap()
        .value(QStringLiteral("required")).toList();
    const auto live_edit_schema = live_edit_tool.value(QStringLiteral("inputSchema")).toMap();
    const auto schema_properties = live_edit_schema.value(QStringLiteral("properties")).toMap();
    const auto operation_values = schema_properties.value(QStringLiteral("operation")).toMap()
        .value(QStringLiteral("enum")).toList();
    const auto value_properties = schema_properties.value(QStringLiteral("values")).toMap()
        .value(QStringLiteral("properties")).toMap();
    auto paint_requires_points = false;
    auto paint_size_limit_is_typed = false;
    auto text_requires_values = false;
    auto photo_fragment_requires_single_source = false;
    auto lasso_requires_points = false;
    auto lasso_points_are_bounded = false;
    auto lasso_point_coordinates_are_numeric = false;
    auto lasso_feather_is_typed = false;
    auto update_effect_requires_changed_control = false;
    for (const auto& condition_value : live_edit_schema.value(QStringLiteral("allOf")).toList()) {
      const auto condition = condition_value.toMap();
      const auto operation_name = condition.value(QStringLiteral("if")).toMap()
          .value(QStringLiteral("properties")).toMap()
          .value(QStringLiteral("operation")).toMap()
          .value(QStringLiteral("const")).toString();
      const auto required = condition.value(QStringLiteral("then")).toMap()
          .value(QStringLiteral("properties")).toMap()
          .value(QStringLiteral("values")).toMap()
          .value(QStringLiteral("required")).toList();
      if (operation_name == QStringLiteral("paint")) {
        paint_requires_points = required.contains(QStringLiteral("points"));
        paint_size_limit_is_typed = condition.value(QStringLiteral("then")).toMap()
            .value(QStringLiteral("properties")).toMap()
            .value(QStringLiteral("values")).toMap()
            .value(QStringLiteral("properties")).toMap()
            .value(QStringLiteral("size")).toMap()
            .value(QStringLiteral("maximum")).toDouble() == 4096.0;
      }
      if (operation_name == QStringLiteral("lasso_selection")) {
        const auto values_schema = condition.value(QStringLiteral("then")).toMap()
            .value(QStringLiteral("properties")).toMap()
            .value(QStringLiteral("values")).toMap();
        const auto points_schema = values_schema.value(QStringLiteral("properties")).toMap()
            .value(QStringLiteral("points")).toMap();
        const auto point_coordinates = points_schema.value(QStringLiteral("items")).toMap()
            .value(QStringLiteral("properties")).toMap();
        const auto feather_schema = values_schema.value(QStringLiteral("properties")).toMap()
            .value(QStringLiteral("feather")).toMap();
        lasso_requires_points = required.contains(QStringLiteral("points"));
        lasso_points_are_bounded = points_schema.value(QStringLiteral("minItems")).toInt() == 3 &&
            points_schema.value(QStringLiteral("maxItems")).toInt() == 4096;
        lasso_point_coordinates_are_numeric =
            point_coordinates.value(QStringLiteral("x")).toMap().value(QStringLiteral("type")).toString() ==
                QStringLiteral("number") &&
            point_coordinates.value(QStringLiteral("x")).toMap().value(QStringLiteral("minimum")).toDouble() == 0.0 &&
            point_coordinates.value(QStringLiteral("x")).toMap().value(QStringLiteral("maximum")).toDouble() == 1.0 &&
            point_coordinates.value(QStringLiteral("y")).toMap().value(QStringLiteral("type")).toString() ==
                QStringLiteral("number") &&
            point_coordinates.value(QStringLiteral("y")).toMap().value(QStringLiteral("minimum")).toDouble() == 0.0 &&
            point_coordinates.value(QStringLiteral("y")).toMap().value(QStringLiteral("maximum")).toDouble() == 1.0;
        lasso_feather_is_typed = feather_schema.value(QStringLiteral("type")).toString() == QStringLiteral("number") &&
            feather_schema.value(QStringLiteral("minimum")).toDouble() == 0.0 &&
            feather_schema.value(QStringLiteral("maximum")).toDouble() == 0.05;
      }
      if (operation_name == QStringLiteral("add_text"))
        text_requires_values = required.contains(QStringLiteral("text")) &&
            required.contains(QStringLiteral("x")) && required.contains(QStringLiteral("y"));
      if (operation_name == QStringLiteral("update_effect")) {
        const auto values_schema = condition.value(QStringLiteral("then")).toMap()
            .value(QStringLiteral("properties")).toMap()
            .value(QStringLiteral("values")).toMap();
        update_effect_requires_changed_control = required.contains(QStringLiteral("effectId")) &&
            values_schema.value(QStringLiteral("minProperties")).toInt() == 2;
      }
      if (operation_name == QStringLiteral("save_photo_fragment_preset")) {
        const auto save_values = condition.value(QStringLiteral("then")).toMap()
            .value(QStringLiteral("properties")).toMap()
            .value(QStringLiteral("values")).toMap();
        photo_fragment_requires_single_source =
            save_values.value(QStringLiteral("required")).toList().contains(QStringLiteral("name")) &&
            save_values.value(QStringLiteral("oneOf")).toList().size() == 2;
      }
    }
    const auto light_exposure_property = value_properties.value(QStringLiteral("lightExposure")).toMap();
    const auto top_level_coordinates_are_integers =
        value_properties.value(QStringLiteral("x")).toMap().value(QStringLiteral("type")).toString() ==
            QStringLiteral("integer") &&
        value_properties.value(QStringLiteral("y")).toMap().value(QStringLiteral("type")).toString() ==
            QStringLiteral("integer") &&
        value_properties.value(QStringLiteral("position")).toMap().value(QStringLiteral("type")).toString() ==
            QStringLiteral("integer");
    const auto nested_point_coordinates_are_numeric = [](const QVariantMap& point_array_schema) {
      const auto points = point_array_schema.value(QStringLiteral("items")).toMap();
      const auto coordinates = points.value(QStringLiteral("properties")).toMap();
      return coordinates.value(QStringLiteral("x")).toMap().value(QStringLiteral("type")).toString() ==
              QStringLiteral("number") &&
          coordinates.value(QStringLiteral("y")).toMap().value(QStringLiteral("type")).toString() ==
              QStringLiteral("number") &&
          coordinates.contains(QStringLiteral("x")) && coordinates.contains(QStringLiteral("y"));
    };
    const auto path_point_coordinates_are_numeric = nested_point_coordinates_are_numeric(
        value_properties.value(QStringLiteral("pathPoints")).toMap());
    const auto path_contour_coordinates_are_numeric = nested_point_coordinates_are_numeric(
        value_properties.value(QStringLiteral("pathContours")).toMap()
            .value(QStringLiteral("items")).toMap());
    if (operation_values.contains(QStringLiteral("inspect")) ||
        !operation_values.contains(QStringLiteral("paint")) ||
        !operation_values.contains(QStringLiteral("typography")) ||
        !operation_values.contains(QStringLiteral("add_effect")) ||
        !operation_values.contains(QStringLiteral("update_effect")) ||
        !operation_values.contains(QStringLiteral("remove_effect")) ||
        !operation_values.contains(QStringLiteral("reorder_effect")) ||
        !operation_values.contains(QStringLiteral("mask_effect")) ||
        !operation_values.contains(QStringLiteral("clear_effect_mask")) ||
        !operation_values.contains(QStringLiteral("add_pixel_layer")) ||
        !operation_values.contains(QStringLiteral("add_source_layer")) ||
        !operation_values.contains(QStringLiteral("lasso_selection")) ||
        !inspect_values_properties.contains(QStringLiteral("family")) ||
        !inspect_values_properties.contains(QStringLiteral("face")) ||
        !inspect_values_required.contains(QStringLiteral("family")) ||
        !value_properties.value(QStringLiteral("effectType")).toMap()
            .value(QStringLiteral("enum")).toList().contains(QStringLiteral("optical_diffusion")) ||
        light_exposure_property.value(QStringLiteral("type")).toString() != QStringLiteral("number") ||
        light_exposure_property.value(QStringLiteral("minimum")).toDouble() != -8.0 ||
        light_exposure_property.value(QStringLiteral("maximum")).toDouble() != 8.0 ||
        value_properties.value(QStringLiteral("pathContours")).toMap()
                .value(QStringLiteral("type")).toString() != QStringLiteral("array") ||
        !value_properties.contains(QStringLiteral("textureMaterial")) ||
        !value_properties.contains(QStringLiteral("effectId")) ||
        !paint_requires_points || !paint_size_limit_is_typed || !text_requires_values ||
        !lasso_requires_points || !lasso_points_are_bounded ||
        !top_level_coordinates_are_integers || !lasso_point_coordinates_are_numeric ||
        !path_point_coordinates_are_numeric || !path_contour_coordinates_are_numeric ||
        !lasso_feather_is_typed ||
        !photo_fragment_requires_single_source || !update_effect_requires_changed_control)
      return fail("Live composition schema must publish integer canvas coordinates while keeping nested path points fractional");
    live_composition_client.setAllowedTools({QStringLiteral("photo.inspect_composition"),
        QStringLiteral("photo.edit_composition")});
    const auto live_inspect = live_composition_client.callTool(
        QStringLiteral("photo.inspect_composition"), {{QStringLiteral("assetId"), QStringLiteral("photo")},
            {QStringLiteral("values"), QVariantMap{{QStringLiteral("family"), QStringLiteral("Georgia")}}}});
    if (!live_inspect.value(QStringLiteral("ok")).toBool() || calls != 10 ||
        last.value(QStringLiteral("operation")).toString() != QStringLiteral("inspect") ||
        last.value(QStringLiteral("values")).toMap().value(QStringLiteral("family")).toString() != QStringLiteral("Georgia"))
      return fail("AgentMcpClient must dispatch composition inspection through the live MCP endpoint");
    const auto live_edit = live_composition_client.callTool(
        QStringLiteral("photo.edit_composition"), edit.toVariantMap());
    if (!live_edit.value(QStringLiteral("ok")).toBool() || calls != 11 ||
        last.value(QStringLiteral("operation")).toString() != QStringLiteral("add_text") ||
        last.value(QStringLiteral("values")).toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("FLOAT"))
      return fail("AgentMcpClient must dispatch typed composition edits through live MCP transport");
    // Replay the reported model error through planning and the live MCP bridge.
    // Direct edit-tool misuse remains rejected; only the model plan is corrected.
    const QJsonObject mistaken_inspection{{"assetId", "asset-0928f1bcb62c480b"},
        {"operation", "inspect"}, {"values", QJsonObject{}}};
    result = mcp_server.callTool("photo.edit_composition", mistaken_inspection, draft);
    if (!result.value("isError").toBool() || calls != 11)
      return fail("Direct edit-tool inspection misuse must remain rejected before reaching the editor");
    const QJsonObject inspection_content{{"decision", "tool_call_plan"}, {"summary", "Read composition before editing."},
        {"toolCalls", QJsonArray{QJsonObject{{"name", "photo.edit_composition"},
            {"arguments", mistaken_inspection}}}}};
    const QJsonObject inspection_response{{"model", "qwen2.5vl:7b"}, {"done", true},
        {"message", QJsonObject{{"role", "assistant"}, {"content", QString::fromUtf8(
            QJsonDocument(inspection_content).toJson(QJsonDocument::Compact))}}}};
    qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
        QJsonDocument(inspection_response).toJson(QJsonDocument::Compact));
    photo_editor::desktop::AgentLocalModelPlanRequest inspection_request;
    inspection_request.model_id = QStringLiteral("ollama-qwen-tool-plan-test");
    inspection_request.style = photo_editor::desktop::built_in_agent_edit_styles().first().toMap();
    inspection_request.asset = {{"filePath", source_path}};
    inspection_request.user_instructions = QStringLiteral("Inspect the layers before adding a title behind the subject.");
    inspection_request.allow_tool_call_plan = true;
    inspection_request.allow_deterministic_fallback = false;
    inspection_request.available_tool_schemas = {live_inspect_tool, live_edit_tool};
    const auto corrected_plan = photo_editor::desktop::plan_agent_recipe_patch(inspection_request);
    qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
    const auto corrected_call = corrected_plan.tool_call_plan.value(0).toMap();
    const auto corrected_args = corrected_call.value("arguments").toMap();
    if (!corrected_plan.ok || corrected_call.value("name").toString() != "photo.inspect_composition" ||
        corrected_args != QVariantMap{{"assetId", "asset-0928f1bcb62c480b"}} ||
        corrected_plan.prompt_audit.value("toolCallCorrections").toList().size() != 1)
      return fail("The reported model inspection call must become an audited read-only composition request");
    live_composition_client.setAllowedTools({QStringLiteral("photo.inspect_composition")});
    const auto corrected_inspect = live_composition_client.callTool(
        corrected_call.value("name").toString(), corrected_args);
    if (!corrected_inspect.value("ok").toBool() || calls != 12 || last.value("operation").toString() != "inspect" ||
        last.value("assetId").toString() != "asset-0928f1bcb62c480b" || last.contains("values"))
      return fail("Corrected model inspection must execute exactly one read through the live MCP bridge");
    std::cout << "composition_tool_contract=passed\n";
    return EXIT_SUCCESS;
  }
  if (app.arguments().contains(QStringLiteral("--search-tools-contract"))) {
    const auto read = mcp_server.toolsListResult({QStringLiteral("read")}).value("tools").toArray();
    const auto draft = mcp_server.toolsListResult({QStringLiteral("read"), QStringLiteral("draft")}).value("tools").toArray();
    auto names = [](const QJsonArray& values) {
      QStringList result;
      for (const auto& value : values) result.append(value.toObject().value("name").toString());
      return result;
    };
    const auto read_names = names(read);
    const auto draft_names = names(draft);
    if (!read_names.contains("photo.search_edit_status") || read_names.contains("photo.search_edit_start") ||
        read_names.contains("photo.search_edit_cancel") || !draft_names.contains("photo.search_edit_start") ||
        !draft_names.contains("photo.search_edit_cancel")) return fail("search tools must respect capability-scoped discovery");
    for (const auto& name : draft_names) {
      if (name.contains("search") && name.contains("confirm")) return fail("search confirmation must not be agent callable");
    }
    std::cout << "search_edit_tool_contract=passed\n";
    return EXIT_SUCCESS;
  }
  QJsonObject session;
  if (bridge.start(
          true,
          {QStringLiteral("read"), QStringLiteral("draft"),
           QStringLiteral("export"), QStringLiteral("commit")})) {
    session = read_json_file(bridge.sessionFilePath());
    auto session_permissions_valid = true;
#ifndef Q_OS_WIN
    const auto permissions = QFileInfo(bridge.sessionFilePath()).permissions();
    session_permissions_valid = permissions.testFlag(QFileDevice::ReadOwner) && permissions.testFlag(QFileDevice::WriteOwner) &&
        !(permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                         QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther | QFileDevice::ExeOwner));
#endif
    if (session.value(QStringLiteral("endpoint")).toString() != bridge.endpointName() ||
        session.value(QStringLiteral("endpointKind")).toString() != bridge.endpointKind() ||
        session.value(QStringLiteral("mcpEndpoint")).toString() != bridge.mcpEndpoint() ||
        session.value(QStringLiteral("mcpEndpointKind")).toString() != bridge.mcpEndpointKind() ||
        session.value(QStringLiteral("mcpTransport")).toString() != QStringLiteral("streamable-http") ||
        session.value(QStringLiteral("mcpCapabilities")).toArray().size() != 4 ||
        session.value(QStringLiteral("mcpConnectedClientCount")).toInt() != 0 ||
        session.value(QStringLiteral("mcpTotalClientCount")).toInt() != 0 ||
        session.value(QStringLiteral("token")).toString() != bridge.token() ||
        bridge.token().size() != 64 ||
        !session_permissions_valid) {
      return fail("session file should expose endpoint and token");
    }
    session.insert(QStringLiteral("endpointKind"), QStringLiteral("in-memory"));
    session.insert(QStringLiteral("endpoint"), QStringLiteral("in-memory"));
  } else {
    const auto error = bridge.errorMessage();
    if (!error.contains(QStringLiteral("listen"), Qt::CaseInsensitive)) {
      std::cerr << error.toStdString() << '\n';
      return fail("automation bridge should start or be blocked only by listener restrictions");
    }
    session = QJsonObject{
        {QStringLiteral("endpointKind"), QStringLiteral("in-memory")},
        {QStringLiteral("endpoint"), QStringLiteral("in-memory")},
        {QStringLiteral("token"), bridge.token()},
    };
  }

  const auto invalid_token = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), QStringLiteral("wrong")},
          {QStringLiteral("command"), QStringLiteral("photo_live_status")},
      });
  if (invalid_token.value(QStringLiteral("ok")).toBool()) {
    return fail("bridge should reject invalid tokens");
  }

  if (!bridge.mcpEndpoint().isEmpty()) {
    const auto unauthorized_mcp_sse = send_mcp_http_get(bridge.mcpEndpoint());
    if (unauthorized_mcp_sse.status_code != 401) {
      std::cerr << "Unauthorized MCP GET diagnostic status=" << unauthorized_mcp_sse.status_code
                << " body=" << unauthorized_mcp_sse.body.left(500).toStdString() << '\n';
      return fail("MCP GET discovery must reject a missing bearer capability");
    }
    auto unauthorized_post_status = 0;
    send_mcp_http_request(
        bridge.mcpEndpoint(),
        QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), 100},
            {QStringLiteral("method"), QStringLiteral("tools/list")},
        },
        QStringLiteral("wrong"),
        &unauthorized_post_status);
    if (unauthorized_post_status != 401) {
      return fail("MCP POST must reject an invalid bearer capability");
    }

    const auto mcp_sse = send_mcp_http_get(bridge.mcpEndpoint(), bridge.token());
    const auto mcp_sse_body = QString::fromUtf8(mcp_sse.body);
    if (mcp_sse.status_code != 200 ||
        mcp_sse.content_type != QStringLiteral("text/event-stream") ||
        !mcp_sse_body.contains(QStringLiteral("event: endpoint")) ||
        !mcp_sse_body.contains(QStringLiteral("\"transport\":\"streamable-http\"")) ||
        !mcp_sse_body.contains(QStringLiteral("event: tools")) ||
        !mcp_sse_body.contains(QStringLiteral("photo.live_status"))) {
      std::cerr << "MCP SSE diagnostic status=" << mcp_sse.status_code
                << " contentType=" << mcp_sse.content_type.toStdString()
                << " body=" << mcp_sse.body.left(1000).toStdString() << '\n';
      return fail("MCP HTTP endpoint should expose GET /mcp as an SSE discovery stream");
    }

    auto mcp_http_status_code = 0;
    const auto mcp_http_tools = send_mcp_http_request(
        bridge.mcpEndpoint(),
        QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), 101},
            {QStringLiteral("method"), QStringLiteral("tools/list")},
        },
        bridge.token(),
        &mcp_http_status_code);
    if (mcp_http_status_code != 200 ||
        mcp_http_tools.value(QStringLiteral("id")).toInt() != 101 ||
        mcp_http_tools.value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray().isEmpty()) {
      return fail("MCP HTTP endpoint should serve tools/list over localhost streamable HTTP");
    }

    const auto origin_request = QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 104},
        {QStringLiteral("method"), QStringLiteral("tools/list")},
    };
    auto omitted_origin_status = 0;
    send_mcp_http_request(
        bridge.mcpEndpoint(), origin_request, bridge.token(),
        &omitted_origin_status, QString{});
    auto opaque_origin_status = 0;
    send_mcp_http_request(
        bridge.mcpEndpoint(), origin_request, bridge.token(),
        &opaque_origin_status, QStringLiteral("null"));
    auto hostile_origin_status = 0;
    send_mcp_http_request(
        bridge.mcpEndpoint(), origin_request, bridge.token(),
        &hostile_origin_status, QStringLiteral("https://attacker.example"));
    auto forged_local_origin_status = 0;
    send_mcp_http_request(
        bridge.mcpEndpoint(), origin_request, bridge.token(),
        &forged_local_origin_status, QStringLiteral("http://localhost.attacker.example"));
    auto hostile_host_status = 0;
    send_mcp_http_request(
        bridge.mcpEndpoint(), origin_request, bridge.token(),
        &hostile_host_status, QStringLiteral("http://127.0.0.1"),
        QStringLiteral("attacker.example"));
    if (omitted_origin_status != 200 || opaque_origin_status != 403 ||
        hostile_origin_status != 403 || forged_local_origin_status != 403 ||
        hostile_host_status != 400) {
      return fail("MCP HTTP should enforce strict Origin and Host policy variants");
    }

    auto mcp_http_call_status_code = 0;
    const auto mcp_http_status = send_mcp_http_request(
        bridge.mcpEndpoint(),
        QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), 102},
            {QStringLiteral("method"), QStringLiteral("tools/call")},
            {QStringLiteral("params"),
             QJsonObject{
                 {QStringLiteral("name"), QStringLiteral("photo.live_status")},
                 {QStringLiteral("arguments"), QJsonObject{}},
             }},
        },
        bridge.token(),
        &mcp_http_call_status_code);
    if (mcp_http_call_status_code != 200 ||
        mcp_http_status.value(QStringLiteral("result"))
                .toObject()
                .value(QStringLiteral("structuredContent"))
                .toObject()
                .value(QStringLiteral("selectedAssetId"))
                .toString() != QStringLiteral("asset-1")) {
      return fail("MCP HTTP endpoint should serve tools/call with structured tool results");
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (bridge.mcpTotalClientCount() < 2 ||
        bridge.mcpLastToolCallName() != QStringLiteral("photo.live_status") ||
        bridge.mcpLastToolCallStatus() != QStringLiteral("ok") ||
        bridge.mcpLastToolCallAt().isEmpty()) {
      return fail("MCP HTTP endpoint should track client count and last tool call telemetry");
    }

    photo_editor::desktop::AgentMcpClient mcp_client;
    mcp_client.setEndpoint(bridge.mcpEndpoint());
    mcp_client.setBearerToken(bridge.token());
    if (mcp_client.timeoutMs() != -1) return fail("MCP tool requests must default to unlimited waiting");
    const auto client_tools = mcp_client.refreshToolCache(QStringLiteral("bridge-test-run"));
    if (!client_tools.value(QStringLiteral("ok")).toBool() ||
        client_tools.value(QStringLiteral("runId")).toString() != QStringLiteral("bridge-test-run") ||
        client_tools.value(QStringLiteral("toolCount")).toInt() < 12 ||
        !mcp_client.hasCachedTool(QStringLiteral("photo.live_status"))) {
      return fail("AgentMcpClient should cache MCP tool schemas from the running app endpoint");
    }
    mcp_client.setAllowedTools({QStringLiteral("photo.live_status")});
    const auto client_status = mcp_client.callTool(QStringLiteral("photo.live_status"));
    if (!client_status.value(QStringLiteral("ok")).toBool() ||
        client_status.value(QStringLiteral("structuredContent"))
                .toMap()
                .value(QStringLiteral("selectedAssetId"))
                .toString() != QStringLiteral("asset-1")) {
      return fail("AgentMcpClient should call allowed MCP tools and expose structured results");
    }
    const auto client_status_telemetry = client_status.value(QStringLiteral("structuredContent")).toMap();
    if (client_status_telemetry.value(QStringLiteral("mcpTotalClientCount")).toInt() < 3 ||
        client_status_telemetry.value(QStringLiteral("mcpLastToolCallName")).toString() !=
            QStringLiteral("photo.live_status")) {
      return fail("AgentMcpClient live status should expose MCP telemetry from the running app");
    }
    const auto client_disallowed = mcp_client.callTool(QStringLiteral("photo.commit_agent_draft"));
    if (client_disallowed.value(QStringLiteral("ok")).toBool() ||
        !client_disallowed.value(QStringLiteral("blockedByAllowlist")).toBool()) {
      return fail("AgentMcpClient should block tools outside the run allowlist before calling MCP");
    }
    mcp_client.clearAllowedTools();
    const auto client_missing = mcp_client.callTool(QStringLiteral("photo.not_in_cache"));
    if (client_missing.value(QStringLiteral("ok")).toBool() ||
        !client_missing.value(QStringLiteral("missingFromToolCache")).toBool()) {
      return fail("AgentMcpClient should reject tools missing from the cached schema");
    }
    photo_editor::desktop::AgentMcpClient canceling_mcp_client;
    canceling_mcp_client.setEndpoint(bridge.mcpEndpoint());
    canceling_mcp_client.setBearerToken(bridge.token());
    canceling_mcp_client.setTimeoutMs(-1);
    auto cancellation_poll_count = 0;
    canceling_mcp_client.setCancellationPredicate([&cancellation_poll_count]() {
      ++cancellation_poll_count;
      return cancellation_poll_count > 1;
    });
    const auto canceled_refresh =
        canceling_mcp_client.refreshToolCache(QStringLiteral("canceled-mcp-run"));
    if (canceled_refresh.value(QStringLiteral("ok")).toBool() ||
        !canceled_refresh.value(QStringLiteral("canceled")).toBool() ||
        !canceled_refresh.value(QStringLiteral("error")).toString().contains(QStringLiteral("canceled")) ||
        cancellation_poll_count <= 1) {
      return fail("AgentMcpClient should cancel active MCP HTTP requests through the run cancellation predicate");
    }

    agent_edit.setMcpEndpoint(
        bridge.mcpEndpoint(), bridge.mcpEndpointKind(), bridge.token());
    const auto controller_mcp_status = agent_edit.refreshMcpTools();
    if (!controller_mcp_status.value(QStringLiteral("ok")).toBool() ||
        !agent_edit.mcpAvailable() ||
        agent_edit.mcpStatus() != QStringLiteral("Ready") ||
        agent_edit.mcpToolCount() < 12 ||
        !agent_edit.mcpToolNames().contains(QStringLiteral("photo.live_status")) ||
        !agent_edit.mcpLastError().isEmpty() ||
        agent_edit.mcpTotalClientCount() < 4 ||
        agent_edit.mcpLastToolCallName() != QStringLiteral("photo.live_status") ||
        agent_edit.mcpLastToolCallStatus() != QStringLiteral("ok") ||
        controller_mcp_status.value(QStringLiteral("lastToolCallName")).toString() !=
            QStringLiteral("photo.live_status") ||
        agent_edit.mcpEndpoint() != bridge.mcpEndpoint() ||
        agent_edit.mcpEndpointKind() != bridge.mcpEndpointKind()) {
      return fail("AgentEditController should expose refreshed MCP endpoint and tool status");
    }
    const auto controller_schema_cache =
        controller_mcp_status.value(QStringLiteral("schemaCache")).toMap();
    const auto first_schema_revision =
        controller_schema_cache.value(QStringLiteral("revision")).toInt();
    const auto first_schema_fingerprint =
        controller_schema_cache.value(QStringLiteral("fingerprint")).toString();
    if (!controller_schema_cache.value(QStringLiteral("enabled")).toBool() ||
        controller_schema_cache.value(QStringLiteral("primedBeforeRefresh")).toBool() ||
        controller_schema_cache.value(QStringLiteral("changed")).toBool() ||
        first_schema_revision != 1 ||
        first_schema_fingerprint.isEmpty() ||
        controller_schema_cache.value(QStringLiteral("cachedToolCount")).toInt() !=
            agent_edit.mcpToolCount() ||
        !controller_schema_cache.value(QStringLiteral("addedToolNames"))
             .toStringList()
             .contains(QStringLiteral("photo.live_status"))) {
      return fail("AgentEditController should initialize a fingerprinted MCP schema cache on refresh");
    }
    const auto second_controller_mcp_status = agent_edit.refreshMcpTools();
    const auto second_schema_cache =
        second_controller_mcp_status.value(QStringLiteral("schemaCache")).toMap();
    if (!second_controller_mcp_status.value(QStringLiteral("ok")).toBool() ||
        !second_schema_cache.value(QStringLiteral("enabled")).toBool() ||
        !second_schema_cache.value(QStringLiteral("primedBeforeRefresh")).toBool() ||
        second_schema_cache.value(QStringLiteral("changed")).toBool() ||
        second_schema_cache.value(QStringLiteral("revision")).toInt() != first_schema_revision ||
        second_schema_cache.value(QStringLiteral("fingerprint")).toString() !=
            first_schema_fingerprint ||
        !second_schema_cache.value(QStringLiteral("addedToolNames")).toStringList().isEmpty() ||
        !second_schema_cache.value(QStringLiteral("removedToolNames")).toStringList().isEmpty()) {
      return fail("AgentEditController should re-check MCP schemas without bumping the cache revision when unchanged");
    }
    const auto sample_tool_policy = agent_edit.mcpToolPolicyForPhase(QStringLiteral("sample"));
    const auto batch_tool_policy = agent_edit.mcpToolPolicyForPhase(QStringLiteral("batch"));
    const auto commit_tool_policy = agent_edit.mcpToolPolicyForPhase(QStringLiteral("commit-export"));
    const auto sample_tool_names = sample_tool_policy.value(QStringLiteral("toolNames")).toStringList();
    const auto batch_tool_names = batch_tool_policy.value(QStringLiteral("toolNames")).toStringList();
    const auto commit_tool_names = commit_tool_policy.value(QStringLiteral("toolNames")).toStringList();
    const auto sample_policy_schema_cache =
        sample_tool_policy.value(QStringLiteral("schemaCache")).toMap();
    const auto sample_tool_budget =
        sample_tool_policy.value(QStringLiteral("toolBudget")).toMap();
    if (!sample_tool_policy.value(QStringLiteral("ok")).toBool() ||
        sample_tool_policy.value(QStringLiteral("phase")).toString() != QStringLiteral("sample") ||
        !sample_tool_policy.value(QStringLiteral("leastPrivilege")).toBool() ||
        !sample_tool_budget.value(QStringLiteral("hardLimit")).toBool() ||
        sample_tool_budget.value(QStringLiteral("maxModelToolCallsPerTurn")).toInt() != 8 ||
        sample_tool_budget.value(QStringLiteral("phase")).toString() != QStringLiteral("sample") ||
        sample_policy_schema_cache.value(QStringLiteral("revision")).toInt() !=
            first_schema_revision ||
        sample_policy_schema_cache.value(QStringLiteral("schemaSource")).toString() !=
            QStringLiteral("cached-after-refresh") ||
        sample_tool_policy.value(QStringLiteral("availableToolCount")).toInt() <= 0 ||
        !sample_tool_names.contains(QStringLiteral("photo.render_current_proof")) ||
        sample_tool_names.contains(QStringLiteral("photo.commit_agent_draft")) ||
        sample_tool_names.contains(QStringLiteral("photo.export_batch")) ||
        !batch_tool_names.contains(QStringLiteral("photo.plan_bulk_edit")) ||
        !batch_tool_names.contains(QStringLiteral("photo.validate_batch")) ||
        batch_tool_names.contains(QStringLiteral("photo.commit_agent_draft")) ||
        !commit_tool_names.contains(QStringLiteral("photo.commit_agent_draft")) ||
        !commit_tool_names.contains(QStringLiteral("photo.export_batch")) ||
        commit_tool_policy.value(QStringLiteral("availableToolCount")).toInt() <= 0) {
      return fail("AgentEditController should expose phase-scoped MCP tool policies with filtered schemas");
    }

    const auto inspection_routing_contract =
        app.arguments().contains(QStringLiteral("--composition-inspection-routing-contract"));
    const auto sync_task_lifecycle = app.arguments().contains(QStringLiteral("--sync-task-lifecycle-contract"));
    const auto sync_task_cancel = app.arguments().contains(QStringLiteral("--sync-task-cancel-contract"));
    const auto sync_task_failure = app.arguments().contains(QStringLiteral("--sync-task-failure-contract"));
    const auto sync_task_admission = app.arguments().contains(QStringLiteral("--sync-task-admission-contract"));
    const auto sync_task_conflict = app.arguments().contains(QStringLiteral("--sync-task-conflict-contract"));
    const auto revision_cancel = app.arguments().contains(QStringLiteral("--revision-cancel-contract"));
    const auto revision_early_cancel = app.arguments().contains(QStringLiteral("--revision-early-cancel-contract"));
    const auto revision_failure = app.arguments().contains(QStringLiteral("--revision-failure-contract"));
    const auto revision_conflict = app.arguments().contains(QStringLiteral("--revision-conflict-contract"));
    const auto revision_contract = app.arguments().contains(QStringLiteral("--revision-continuation-contract")) ||
        revision_cancel || revision_early_cancel || revision_failure || revision_conflict;
    const auto proof_conflict = app.arguments().contains(QStringLiteral("--planning-proof-conflict-contract"));
    const auto proof_cancel = app.arguments().contains(QStringLiteral("--planning-proof-cancel-contract"));
    const auto proof_failure = app.arguments().contains(QStringLiteral("--planning-proof-failure-contract"));
    const auto fresh_proof_contract = app.arguments().contains(QStringLiteral("--fresh-planning-proof-contract")) ||
        proof_conflict || proof_cancel || proof_failure;
    const auto sync_task_contract = sync_task_lifecycle || sync_task_cancel || sync_task_failure || sync_task_admission || sync_task_conflict;
    const auto task_lifecycle_contract = app.arguments().contains(QStringLiteral("--task-lifecycle-contract")) || sync_task_lifecycle || sync_task_admission || sync_task_conflict || revision_contract || fresh_proof_contract;
    const auto task_cancel_contract = app.arguments().contains(QStringLiteral("--task-cancel-contract")) || sync_task_cancel;
    const auto task_failure_contract = app.arguments().contains(QStringLiteral("--task-failure-contract")) || sync_task_failure;
    const auto long_task_cancel_contract = app.arguments().contains(QStringLiteral("--long-task-cancel-contract"));
    const auto long_task_contract = app.arguments().contains(QStringLiteral("--long-task-contract")) || long_task_cancel_contract;
    const auto repeated_tool_results_contract = app.arguments().contains(QStringLiteral("--repeated-tool-results-contract"));
    const auto tool_correction_repeat_contract = app.arguments().contains(QStringLiteral("--tool-correction-repeat-contract"));
    const auto tool_correction_cancel_contract = app.arguments().contains(QStringLiteral("--tool-correction-cancel-contract"));
    const auto tool_correction_unresolved_contract = app.arguments().contains(QStringLiteral("--tool-correction-unresolved-contract"));
    const auto tool_plan_validation_recovery_contract =
        app.arguments().contains(QStringLiteral("--tool-plan-validation-recovery-contract"));
    const auto tool_plan_validation_progress_contract =
        app.arguments().contains(QStringLiteral("--tool-plan-validation-progress-contract"));
    const auto tool_plan_validation_repeat_contract =
        app.arguments().contains(QStringLiteral("--tool-plan-validation-repeat-contract"));
    const auto tool_plan_validation_contract = tool_plan_validation_recovery_contract ||
        tool_plan_validation_progress_contract ||
        tool_plan_validation_repeat_contract;
    const auto tool_correction_contract = app.arguments().contains(QStringLiteral("--tool-correction-contract")) ||
        tool_correction_repeat_contract || tool_correction_cancel_contract || tool_correction_unresolved_contract;
    const auto composition_repair_contract = app.arguments().contains(QStringLiteral("--composition-repair-contract"));
    const auto composition_batch_review_contract =
        app.arguments().contains(QStringLiteral("--composition-batch-review-contract"));
    const auto composition_batch_invalid_terminal_contract =
        app.arguments().contains(QStringLiteral("--composition-batch-invalid-terminal-contract"));
    const auto composition_batch_review_scenario = composition_batch_review_contract ||
        composition_batch_invalid_terminal_contract;
    const auto composition_repair_cancel_contract = app.arguments().contains(QStringLiteral("--composition-repair-cancel-contract"));
    const auto composition_repair_conflict_contract = app.arguments().contains(QStringLiteral("--composition-repair-conflict-contract"));
    const auto composition_repair_proof_failure_contract = app.arguments().contains(QStringLiteral("--composition-repair-proof-failure-contract"));
    const auto composition_repair_rejected_critique_contract =
        app.arguments().contains(QStringLiteral("--composition-repair-rejected-critique-contract"));
    const auto composition_repair_scenario = composition_repair_contract || composition_repair_cancel_contract ||
        composition_repair_conflict_contract || composition_repair_proof_failure_contract ||
        composition_repair_rejected_critique_contract || composition_batch_review_scenario;
    const auto keep_composition_test_artifacts = composition_repair_scenario &&
        qEnvironmentVariableIsSet("PHOTO_EDITOR_KEEP_COMPOSITION_TEST_ARTIFACTS");
    if (keep_composition_test_artifacts) runtime_root.setAutoRemove(false);
    const auto task_contract = task_lifecycle_contract || task_cancel_contract || task_failure_contract || long_task_contract || repeated_tool_results_contract || tool_correction_contract || tool_plan_validation_contract || composition_repair_scenario;
    constexpr int long_task_update_count = 72;
    const auto composition_unlimited_wait_contract = app.arguments().contains(QStringLiteral("--composition-unlimited-wait-contract"));
    const auto composition_ready_contract = app.arguments().contains(QStringLiteral("--composition-ready-contract")) || composition_unlimited_wait_contract || task_lifecycle_contract || long_task_contract;
    const auto composition_error_contract = app.arguments().contains(QStringLiteral("--composition-error-contract"));
    const auto composition_cancel_contract = app.arguments().contains(QStringLiteral("--composition-cancel-contract")) || task_cancel_contract;
    const auto composition_failed_call_contract = app.arguments().contains(QStringLiteral("--composition-failed-call-contract")) || task_failure_contract;
    const auto composition_wait_contract = composition_ready_contract || composition_error_contract || composition_cancel_contract;
    const auto composition_extra_call_contract = composition_wait_contract || composition_failed_call_contract;
    const auto composition_chain_contract =
        app.arguments().contains(QStringLiteral("--composition-chain-contract")) || composition_extra_call_contract || repeated_tool_results_contract || tool_correction_contract || tool_plan_validation_contract || composition_repair_scenario;
    const auto expected_tool_rounds = tool_plan_validation_progress_contract ? 14
        : tool_plan_validation_recovery_contract ? 8
        : tool_plan_validation_repeat_contract ? 5
        : composition_chain_contract ? 6 + (long_task_contract ? long_task_update_count : 0) : 2;
    const auto expected_tool_calls = expected_tool_rounds + (composition_extra_call_contract ? 1 : 0);
    auto model_inspection_count = 0;
    QStringList chain_operations;
    QStringList chain_attempted_operations;
    QStringList task_actions;
    QString task_request_id;
    auto composition_busy = false;
    auto long_task_updates = 0;
    auto rejected_correction_calls = 0;
    auto withheld_correction_calls = 0;
    auto accepted_correction_prefix_calls = 0;
    auto validation_recovery_add_text_calls = 0;
    auto composition_repair_text_calls = 0;
    auto composition_repair_add_text_calls = 0;
    QStringList catalog_recipe_persistence_stages;
    QStringList catalog_recipe_persistence_task_ids;
    QStringList native_composition_text_history;
    auto composition_repair_completed_observation_events = 0;
    QVariantList composition_repair_native_calls;
    const auto composition_repair_request_path = runtime_root.filePath(QStringLiteral("composition-repair-model-requests.jsonl"));
    auto composition_repair_snapshot_failures = 0;
    if (long_task_contract || repeated_tool_results_contract || tool_correction_contract || tool_plan_validation_contract || revision_contract || fresh_proof_contract) {
      agent_edit.setMcpEndpoint(bridge.agentMcpEndpoint(), QStringLiteral("in-process"));
      if (!agent_edit.refreshMcpTools().value("ok").toBool()) return fail("Long tasks must use the actual app's in-process tool route");
    }
    auto task_before_recipe = catalog_model.assetById(QStringLiteral("asset-1")).value(QStringLiteral("adjustmentPatchJson")).toString();
    if (composition_repair_scenario) {
      // Composition clipping assertions use the same sRGB output path that
      // authors the white headline. Keep the broader bridge fixture's P3
      // coverage intact while avoiding a P3 photographic finish that can
      // roll fully white fixture pixels below the validation clip threshold.
      auto composition_state = original_state;
      composition_state.output_color_space = QStringLiteral("sRGB");
      task_before_recipe =
          photo_editor::desktop::canonical_asset_bound_adjustment_patch_json(
              photo_editor::desktop::serialize_edit_recipe_json(composition_state));
      QObject::connect(
          &agent_edit,
          &photo_editor::desktop::AgentEditController::runEventsChanged,
          &agent_edit,
          [&]() {
            const auto events = agent_edit.runEvents();
            if (events.isEmpty()) return;
            const auto latest = events.last().toMap();
            if (latest.value(QStringLiteral("stage")).toString() == QStringLiteral("observe") &&
                latest.value(QStringLiteral("status")).toString() == QStringLiteral("complete"))
              ++composition_repair_completed_observation_events;
          });
      const auto detail_source_path = QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/composition-repair-2400x1600.png"));
      if (!write_fixture_image(detail_source_path, QSize(2400, 1600),
              false, composition_batch_invalid_terminal_contract))
        return fail("Full-resolution composition-repair fixture should be written");
      catalog_model.replaceAssetsForTesting({
          make_asset(QStringLiteral("asset-1"), detail_source_path, task_before_recipe),
          make_asset(QStringLiteral("asset-2"), second_source_path, original_recipe),
      });
      catalog_model.selectIndex(0);
      catalog_model.clearSelection();
      catalog_model.toggleSelectionIndex(0);
    }
    QString task_manual_recipe;
    auto composition_repair_manual_edit_requested = false;
    auto native_planning_recipe = task_before_recipe;
    int native_proof_mutation_count = 0;
    const auto proof_transport_path = runtime_root.filePath(QStringLiteral("planning-proof-model-requests.jsonl"));
    if (composition_repair_scenario) {
      agent_edit.setPlanningSnapshotHandler([&](const QString& asset_id) -> QVariantMap {
        const auto parsed = photo_editor::desktop::parse_adjustment_patch_json(native_planning_recipe);
        if (asset_id != QStringLiteral("asset-1") || !parsed.ok() || !parsed.state.has_value()) {
          ++composition_repair_snapshot_failures;
          return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("fixture native composition snapshot unavailable")}};
        }
        for (const auto& layer : parsed.state->layers) {
          const auto value = layer.toMap();
          if (value.value(QStringLiteral("kind")).toString() == QStringLiteral("text"))
            native_composition_text_history.append(value.value(QStringLiteral("text")).toString());
        }
        const auto recipe_sha = QString::fromLatin1(QCryptographicHash::hash(
            native_planning_recipe.toUtf8(), QCryptographicHash::Sha256).toHex());
        return {{QStringLiteral("ok"), true}, {QStringLiteral("assetId"), asset_id},
            {QStringLiteral("adjustmentPatchJson"), native_planning_recipe},
            {QStringLiteral("source"), QStringLiteral("native-photo")},
            {QStringLiteral("taskStateHash"), recipe_sha},
            {QStringLiteral("paintBusy"), composition_busy}};
      });
    } else if (fresh_proof_contract) {
      agent_edit.setPlanningSnapshotHandler([&](const QString& asset_id) -> QVariantMap {
        if (proof_failure && native_proof_mutation_count == 1)
          return {{"ok", false}, {"error", "fixture live proof snapshot unavailable"}};
        return {{"ok", asset_id == "asset-1"}, {"assetId", asset_id},
            {"adjustmentPatchJson", native_planning_recipe}, {"source", "fixture-native-photo"}, {"paintBusy", composition_busy}};
      });
      QObject::connect(&agent_edit, &photo_editor::desktop::AgentEditController::runEventsChanged, &agent_edit, [&]() {
        const auto events = agent_edit.runEvents();
        if (events.isEmpty()) return;
        const auto event = events.last().toMap();
        if (event.value("stage") != "worker-planning-proof:model-tool-follow-up-2" || event.value("status") != "running") return;
        if (proof_cancel) QTimer::singleShot(0, &agent_edit, [&]() { agent_edit.cancelRun("fixture cancel planning proof"); });
        if (proof_conflict) QTimer::singleShot(0, &agent_edit, [&]() {
          auto manual = photo_editor::desktop::parse_adjustment_patch_json(task_before_recipe);
          manual.state->exposure = 0.85;
          native_planning_recipe = photo_editor::desktop::serialize_adjustment_patch_json(*manual.state,
              photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound);
          task_manual_recipe = native_planning_recipe;
          static_cast<void>(catalog_model.applyAdjustmentPatchJsonToAssetIds({"asset-1"}, native_planning_recipe));
        });
      });
      qputenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH", proof_transport_path.toUtf8());
    }
    bool revision_phase = false;
    int revision_poll_count = 0;
    QVariantList revision_typography_sizes;
    if (task_contract) {
      agent_edit.setTaskTransactionHandler([&](const QString& action, const QString& task_id,
          const QStringList& ids, const QVariantMap& context) -> QVariantMap {
        task_actions.append(action);
        if (action == QStringLiteral("begin")) {
          if (sync_task_admission) return {{"ok", false}, {"error", "fixture journal admission rejected"}};
          if (!chain_operations.isEmpty() || !ids.contains(QStringLiteral("asset-1")))
            return {{"ok", false}, {"error", "Task baseline was captured after a mutation or outside scope"}};
          task_request_id = task_id;
          return {{"ok", true}, {"taskId", task_id}, {"assetId", "asset-1"},
              {"beforeRecipeJson", task_before_recipe}, {"revertAvailable", true},
              {"resources", QVariantList{QVariantMap{{"id", "asset-1"}}}}};
        }
        if (task_id != task_request_id) return {{"ok", false}, {"error", "Task ownership changed"}};
        if (action == "continue" && ids != QStringList{"asset-1"})
          return {{"ok", false}, {"error", "Revision was retargeted to the current selection"}};
        if (revision_conflict && action == "continue") {
          auto manual = photo_editor::desktop::parse_adjustment_patch_json(task_before_recipe);
          manual.state->exposure = 0.85;
          task_manual_recipe = photo_editor::desktop::serialize_adjustment_patch_json(*manual.state,
              photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound);
          static_cast<void>(catalog_model.applyAdjustmentPatchJsonToAssetIds({"asset-1"}, task_manual_recipe));
          return {{"ok", true}, {"taskId", "fixture-wrong-task"}, {"revertAvailable", true},
              {"resources", QVariantList{QVariantMap{{"id", "asset-2"}}}}};
        }
        if (sync_task_conflict && action == "check_revert") {
          auto manual = photo_editor::desktop::parse_adjustment_patch_json(task_before_recipe);
          if (!manual.ok()) return {{"ok", false}, {"error", "Manual fixture recipe invalid"}};
          manual.state->exposure = 0.85;
          task_manual_recipe = photo_editor::desktop::serialize_adjustment_patch_json(*manual.state,
              photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound);
          static_cast<void>(catalog_model.applyAdjustmentPatchJsonToAssetIds({"asset-1"}, task_manual_recipe));
          return {{"ok", false}, {"error", "fixture manual edit preserved"}};
        }
        if (composition_repair_conflict_contract && composition_repair_manual_edit_requested &&
            action == QStringLiteral("check_revert")) {
          auto manual = photo_editor::desktop::parse_adjustment_patch_json(native_planning_recipe);
          if (!manual.ok() || !manual.state.has_value())
            return {{"ok", false}, {"error", "Manual composition fixture recipe invalid"}};
          manual.state->exposure = 0.85;
          task_manual_recipe = photo_editor::desktop::serialize_adjustment_patch_json(*manual.state,
              photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound);
          native_planning_recipe = task_manual_recipe;
          static_cast<void>(catalog_model.applyAdjustmentPatchJsonToAssetIds({"asset-1"}, task_manual_recipe));
          return {{"ok", false}, {"error", "fixture manual composition edit preserved"}};
        }
        if (action == QStringLiteral("review") && context.value("expectedRecipes").toMap().value("asset-1").toString().isEmpty())
          return {{"ok", false}, {"error", "Finalization lost the exact authored recipe"}};
        if (action == QStringLiteral("cancel")) composition_busy = false;
        if ((fresh_proof_contract || composition_repair_scenario || tool_plan_validation_contract) &&
            action == "revert") {
          native_planning_recipe = task_before_recipe;
          static_cast<void>(catalog_model.applyAgentAdjustmentPatchJsonToAssetIds(
              task_id, {QStringLiteral("asset-1")}, task_before_recipe));
        }
        return {{"ok", true}, {"taskId", task_id},
            {"status", action == QStringLiteral("continue") ? QStringLiteral("running") : action},
            {"revertAvailable", action != QStringLiteral("revert")},
            {"resources", QVariantList{QVariantMap{{"id", "asset-1"}}}}};
      });
      catalog_model.setAgentRecipePersistenceHandler(
          [&](const QString& stage, const QVariantMap& context) -> QVariantMap {
            const auto owner_task_id = context.value(QStringLiteral("taskId")).toString();
            if (owner_task_id.isEmpty() || owner_task_id != task_request_id ||
                context.value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-1") ||
                context.value(QStringLiteral("patchJson")).toString().isEmpty()) {
              return {{QStringLiteral("ok"), false},
                  {QStringLiteral("error"), QStringLiteral("Catalog write is outside the admitted task owner.")}};
            }
            catalog_recipe_persistence_stages.append(stage);
            catalog_recipe_persistence_task_ids.append(owner_task_id);
            if (stage == QStringLiteral("guard"))
              return {{QStringLiteral("ok"), true},
                  {QStringLiteral("payload"), context.value(QStringLiteral("payload"))}};
            if (stage == QStringLiteral("prepare"))
              return {{QStringLiteral("ok"), true},
                  {QStringLiteral("taskId"), owner_task_id},
                  {QStringLiteral("assetId"), context.value(QStringLiteral("assetId"))},
                  {QStringLiteral("revisionId"), context.value(QStringLiteral("revisionId"))}};
            return {{QStringLiteral("ok"), context.value(QStringLiteral("ok")).toBool()},
                {QStringLiteral("taskId"), owner_task_id},
                {QStringLiteral("revisionId"), context.value(QStringLiteral("revisionId"))}};
          });
    }
    QVariantList chain_layers, chain_masks;
    const auto sync_native_composition_recipe = [&]() {
      auto parsed = photo_editor::desktop::parse_adjustment_patch_json(native_planning_recipe);
      if (!parsed.ok() || !parsed.state.has_value()) return false;
      parsed.state->layers = chain_layers;
      native_planning_recipe = photo_editor::desktop::serialize_adjustment_patch_json(
          *parsed.state, photo_editor::desktop::all_adjustment_groups(),
          photo_editor::desktop::AdjustmentPatchScope::AssetBound);
      return !native_planning_recipe.isEmpty();
    };
    auto composition_poll_count = 0;
    auto busy_heartbeat_count = 0;
    QString composition_error;
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, &agent_edit, [&]() {
      if (composition_busy) ++busy_heartbeat_count;
    });
    heartbeat.start(10);
    if (inspection_routing_contract) {
      bridge.setCompositionHandler([&](const QVariantMap& request) -> QVariantMap {
        ++model_inspection_count;
        if (request != QVariantMap{{"assetId", "asset-1"}, {"operation", "inspect"}})
          return {{"ok", false}, {"error", "Unexpected composition request"}};
        return {{"ok", true}, {"assetId", "asset-1"}, {"layers", QVariantList{}},
            {"masks", QVariantList{}}, {"width", 640}, {"height", 480}};
      });
    }
    if (composition_chain_contract) {
      bridge.setCompositionHandler([&](const QVariantMap& request) -> QVariantMap {
        const auto operation = request.value("operation").toString();
        if (tool_plan_validation_contract && operation != "inspect")
          chain_attempted_operations.append(operation);
        if (composition_repair_scenario) composition_repair_native_calls.append(request);
        if (fresh_proof_contract && operation != "inspect") {
          ++native_proof_mutation_count;
          auto native = photo_editor::desktop::parse_adjustment_patch_json(task_before_recipe);
          native.state->exposure = native_proof_mutation_count * 0.12;
          native_planning_recipe = photo_editor::desktop::serialize_adjustment_patch_json(*native.state,
              photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound);
        }
        if (task_contract && operation != "inspect" && agent_edit.activeTaskId() != task_request_id)
          return {{"ok", false}, {"error", "The native mutation has no admitted task request"}};
        const auto values = request.value("values").toMap();
        QVariantMap content{{"ok", true}, {"assetId", "asset-1"}, {"width", 640}, {"height", 480}};
        if (revision_phase && operation == "copy_selection") {
          if (request.value("assetId") != "asset-1" || values.value("maskId") != "chain-subject-mask")
            return {{"ok", false}, {"error", "Revision lost its original photo or subject mask"}};
          chain_operations.append("revision_copy");
          if (revision_failure) return {{"ok", false}, {"error", "fixture revision cutout rejected"}};
          chain_layers.prepend(QVariantMap{{"id", "revision-cutout"}, {"kind", "pixel"}});
          composition_busy = true;
          QTimer::singleShot(350, &agent_edit, [&]() { composition_busy = false; });
          return {{"ok", true}, {"assetId", "asset-1"}, {"createdLayerId", "revision-cutout"},
              {"layers", chain_layers}, {"masks", chain_masks}, {"paintBusy", true}};
        }
        if (revision_phase && operation == "move") {
          if (composition_busy || request.value("assetId") != "asset-1" || request.value("layerId") != "chain-text")
            return {{"ok", false}, {"error", "Revision moved before pixels were ready or recreated its headline"}};
          chain_operations.append("revision_move");
          chain_layers.prepend(chain_layers.takeLast());
          return {{"ok", true}, {"assetId", "asset-1"}, {"layers", chain_layers}, {"masks", chain_masks}, {"paintBusy", false}};
        }
        if (revision_phase && operation == "inspect" && composition_busy) {
          ++revision_poll_count;
          if (revision_cancel && revision_poll_count == 1)
            QTimer::singleShot(10, &agent_edit, [&]() { agent_edit.cancelRun("fixture cancel revision pixels"); });
        }
        if (composition_repair_scenario && operation == "inspect") {
          content.insert(QStringLiteral("taskStateHash"), QString::fromLatin1(
              QCryptographicHash::hash(native_planning_recipe.toUtf8(), QCryptographicHash::Sha256).toHex()));
        }
        if (tool_correction_contract && operation == "typography" && request.value("layerId") == "missing-layer") {
          ++rejected_correction_calls;
          if (tool_correction_cancel_contract)
            QTimer::singleShot(0, &agent_edit, [&]() { agent_edit.cancelRun(QStringLiteral("Cancel while correcting a rejected command")); });
          return {{"ok", false}, {"error", "Inspect the actual text layer ID; this target does not exist."},
              {"recoverableError", true}, {"mutationNotApplied", true}, {"assetId", "asset-1"}};
        }
        if (tool_correction_contract && operation == "opacity" && values.value("opacity").toDouble() == 0.5) {
          ++accepted_correction_prefix_calls;
          if (accepted_correction_prefix_calls != 1 || chain_layers.isEmpty())
            return {{"ok", false}, {"error", "Completed prefix mutation was replayed."}};
          auto layer = chain_layers.first().toMap();
          layer.insert("opacity", 0.5);
          chain_layers[0] = layer;
          content.insert("layers", chain_layers);
          return content;
        }
        if (tool_correction_contract && operation == "opacity") ++withheld_correction_calls;
        const auto validation_add_text_count = static_cast<int>(std::count(
            chain_operations.cbegin(), chain_operations.cend(), QStringLiteral("add_text")));
        const auto expected_validation_text_count = tool_plan_validation_progress_contract ? 4 : 3;
        const auto validation_text_and_typography_prefix = tool_plan_validation_contract &&
            !chain_operations.isEmpty() && chain_operations.first() == QStringLiteral("select_subject") &&
            std::all_of(chain_operations.cbegin() + 1, chain_operations.cend(), [](const QString& operation) {
              return operation == QStringLiteral("add_text") || operation == QStringLiteral("typography");
            });
        const auto validation_add_text_prefix = validation_text_and_typography_prefix &&
            validation_add_text_count < expected_validation_text_count;
        const auto validation_typography_prefix = validation_text_and_typography_prefix &&
            !chain_operations.isEmpty() && chain_operations.last() == QStringLiteral("add_text");
        const auto validation_typography_count = static_cast<int>(std::count(
            chain_operations.cbegin(), chain_operations.cend(), QStringLiteral("typography")));
        const auto expected_validation_typography_count = tool_plan_validation_progress_contract ? 4 : 1;
        const auto validation_copy_prefix = validation_text_and_typography_prefix &&
            validation_add_text_count == expected_validation_text_count &&
            validation_typography_count == expected_validation_typography_count;
        if (operation == "select_subject" && chain_operations.isEmpty()) {
          chain_masks = {QVariantMap{{"id", "chain-subject-mask"}}};
        } else if (operation == "add_text" &&
                   (chain_operations == QStringList{"select_subject"} ||
                    (composition_batch_review_scenario && chain_operations.isEmpty()) ||
                    validation_add_text_prefix)) {
          if (composition_repair_scenario) ++composition_repair_add_text_calls;
          if (tool_plan_validation_contract) ++validation_recovery_add_text_calls;
          photo_editor::desktop::PhotoTextStyle text_style;
          text_style.point_text = true;
          text_style.size = 100;
          text_style.weight = 700;
          text_style.color = Qt::white;
          const auto created_layer_id = tool_plan_validation_contract
              ? QStringLiteral("validation-text-%1").arg(validation_add_text_count + 1)
              : QStringLiteral("chain-text");
          QVariantMap text_layer{{"id", created_layer_id}, {"name", "Agent headline"},
              {"kind", "text"}, {"enabled", true}, {"opacity", 1.0},
              {"blendMode", "normal"}, {"text", values.value("text").toString()}, {"x", 300}, {"y", 160},
              {"canvasWidth", 2400}, {"canvasHeight", 1600}, {"scale", 1.0},
              {"rotation", 0.0}, {"textAlignment", "left"}, {"agentGraphic", true},
              {"textTypography", photo_editor::desktop::encode_photo_text_style(text_style).toVariantMap()}};
          if (tool_plan_validation_contract) {
            text_layer.insert(QStringLiteral("renderColorDomain"), QStringLiteral("display-linear"));
            text_layer.insert(QStringLiteral("displayReferredTextEligibility"),
                              QStringLiteral("eligible-sdr-top-text-suffix"));
            text_layer.insert(QStringLiteral("displayReferredTextEligibilityReason"),
                              QStringLiteral("fixture text render policy evidence"));
            text_layer.insert(QStringLiteral("displayReferredTextPolicyVersion"),
                              QStringLiteral("display-referred-text-suffix-v1"));
          }
          if (!photo_editor::desktop::fit_photo_point_text(&text_layer))
            return {{"ok", false}, {"error", "The native point-text fixture did not pass the production layout validator."}};
          if (tool_plan_validation_contract) chain_layers.append(text_layer);
          else chain_layers = {text_layer};
          content.insert("createdLayerId", created_layer_id);
        } else if ((operation == "typography" &&
                    (tool_plan_validation_contract ? validation_typography_prefix
                                                   : chain_operations == QStringList{"select_subject", "add_text"} &&
                                                         request.value("layerId").toString() == QStringLiteral("chain-text"))) ||
                   (composition_batch_review_scenario && revision_phase &&
                    operation == "typography" &&
                    request.value("layerId").toString() == QStringLiteral("chain-text"))) {
          auto layer_index = 0;
          if (tool_plan_validation_contract) {
            layer_index = -1;
            for (qsizetype index = 0; index < chain_layers.size(); ++index) {
              if (chain_layers.at(index).toMap().value("id").toString() == request.value("layerId").toString()) {
                layer_index = static_cast<int>(index);
                break;
              }
            }
            if (layer_index < 0) return {{"ok", false}, {"error", "Accepted text layer identity was lost."}};
          }
          auto layer = chain_layers.at(layer_index).toMap();
          auto typography = layer.value("textTypography").toMap();
          const auto previous_typography_size = typography.value(QStringLiteral("size")).toDouble();
          for (auto value = values.cbegin(); value != values.cend(); ++value)
            typography.insert(value.key(), value.value());
          const auto requested_typography = photo_editor::desktop::decode_photo_text_style(
              QJsonObject::fromVariantMap(typography));
          if (!requested_typography)
            return {{"ok", false}, {"error", "The native typography mutation was not a complete supported text style."}};
          layer.insert("textTypography", photo_editor::desktop::encode_photo_text_style(*requested_typography).toVariantMap());
          layer.insert("fontSize", requested_typography->size);
          if (!photo_editor::desktop::fit_photo_point_text(&layer))
            return {{"ok", false}, {"error", "The native typography mutation failed production text layout validation."}};
          chain_layers[layer_index] = layer;
          if (composition_batch_review_scenario && revision_phase)
            revision_typography_sizes.append(requested_typography->size);
          if (composition_batch_review_scenario && revision_phase)
            content.insert(QStringLiteral("changed"),
                std::abs(requested_typography->size - previous_typography_size) > 0.00001);
        } else if (composition_repair_scenario && operation == "text" &&
                   request.value("layerId").toString() == "chain-text") {
          auto text_layer_index = -1;
          for (qsizetype index = 0; index < chain_layers.size(); ++index) {
            if (chain_layers.at(index).toMap().value("id").toString() == QStringLiteral("chain-text")) {
              text_layer_index = static_cast<int>(index);
              break;
            }
          }
          if (values.value("text").toString() != QStringLiteral("APPROVED HEADLINE") || text_layer_index < 0)
            return {{"ok", false}, {"error", "The repair did not target the original text layer with the approved exact copy."},
                {"recoverableError", true}, {"mutationNotApplied", true}, {"assetId", "asset-1"}};
          auto layer = chain_layers.at(text_layer_index).toMap();
          layer.insert("text", values.value("text"));
          if (!photo_editor::desktop::fit_photo_point_text(&layer))
            return {{"ok", false}, {"error", "The corrected exact copy failed production text layout validation."}};
          chain_layers[text_layer_index] = layer;
          content.insert(QStringLiteral("changed"), true);
          ++composition_repair_text_calls;
          if (composition_repair_cancel_contract)
            QTimer::singleShot(0, &agent_edit, [&]() {
              agent_edit.cancelRun(QStringLiteral("fixture Stop during rendered composition repair"));
            });
        } else if (operation == "copy_selection" &&
                   (tool_plan_validation_contract ? validation_copy_prefix
                                                  : chain_operations == QStringList{"select_subject", "add_text", "typography"}) &&
                   values.value("maskId").toString() == "chain-subject-mask" && values.value("independent").toBool()) {
          if (composition_failed_call_contract) {
            chain_operations.append(operation);
            return {{"ok", false}, {"error", "fixture cutout command rejected"}};
          }
          chain_layers.prepend(QVariantMap{{"id", "chain-cutout"}, {"kind", "pixel"}});
          content.insert("createdLayerId", "chain-cutout");
          if (sync_task_cancel)
            QTimer::singleShot(0, &agent_edit, [&]() { agent_edit.cancelRun(QStringLiteral("fixture cancel during owned synchronous planning")); });
          // The legacy synchronous API explicitly rejects pending pixel work.
          // Its ownership contract uses an immediately completed cutout; queued
          // fixtures below retain the pending job and its cancellation checks.
          if (composition_wait_contract && !sync_task_contract) {
            composition_busy = true;
            QTimer::singleShot(composition_unlimited_wait_contract ? 61000 : 350, Qt::PreciseTimer, &agent_edit, [&]() {
              composition_busy = false;
              if (composition_error_contract) composition_error = QStringLiteral("fixture cutout materialization failed");
            });
          }
        } else if (operation == "move" && !composition_busy && composition_error.isEmpty() &&
                   chain_operations == QStringList{"select_subject", "add_text", "typography", "copy_selection"} &&
                   request.value("layerId").toString() == "chain-text") {
          content.insert("movedAfterPixelsReady", true);
        } else if (operation == "opacity" && long_task_contract && !composition_busy &&
                   chain_operations.size() >= 5 && request.value("layerId").toString() == "chain-text") {
          ++long_task_updates;
          auto layer = chain_layers[1].toMap();
          if (std::abs(values.value("opacity").toDouble() - long_task_updates / 100.0) > 0.00001)
            return {{"ok", false}, {"error", "A long-task mutation was replayed or reordered"}};
          layer.insert("opacity", values.value("opacity"));
          chain_layers[1] = layer;
          if (long_task_cancel_contract && long_task_updates == 9)
            QTimer::singleShot(0, &agent_edit, [&]() { agent_edit.cancelRun(QStringLiteral("fixture cancel after long task steps")); });
        } else if (operation != "inspect") {
          return {{"ok", false}, {"error", QStringLiteral("Composition chain rejected %1 after [%2]")
              .arg(operation, chain_operations.join(','))}};
        }
        if (operation == "inspect" && composition_busy) {
          ++composition_poll_count;
          if (composition_cancel_contract && composition_poll_count == 1)
            QTimer::singleShot(10, &agent_edit, [&]() { agent_edit.cancelRun(QStringLiteral("fixture cancel while pixels pending")); });
        }
        if (tool_plan_validation_contract && operation == "inspect") {
          QStringList eligible_layer_ids;
          for (const auto& layer_value : chain_layers) {
            const auto layer = layer_value.toMap();
            if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text"))
              eligible_layer_ids.append(layer.value(QStringLiteral("id")).toString());
          }
          content.insert(QStringLiteral("displayTextColorPolicy"), QVariantMap{
              {QStringLiteral("renderColorDomain"), QStringLiteral("display-linear")},
              {QStringLiteral("displayReferredTextEligibility"),
               QStringLiteral("eligible-sdr-top-text-suffix")},
              {QStringLiteral("displayReferredTextEligibilityReason"),
               QStringLiteral("fixture text render policy evidence")},
              {QStringLiteral("displayReferredTextPolicyVersion"),
               QStringLiteral("display-referred-text-suffix-v1")},
              {QStringLiteral("limits"),
               QStringLiteral("eligible SDR top text layers only")},
              {QStringLiteral("eligibleLayerIds"), eligible_layer_ids},
          });
        }
        if (operation != "inspect") chain_operations.append(operation);
        if (composition_repair_scenario && operation != "inspect" &&
            !sync_native_composition_recipe())
          return {{"ok", false}, {"error", "The fixture could not checkpoint the actual composed recipe."},
              {"recoverableError", false}, {"mutationNotApplied", false}, {"assetId", "asset-1"}};
        auto layers = chain_layers;
        for (auto& value : layers) {
          auto layer = value.toMap();
          layer.insert("thumbnailDiagnostics", QVariantList{
              QString(tool_plan_validation_contract ? 32 : 30000, QLatin1Char('x'))});
          value = layer;
        }
        if (composition_repair_proof_failure_contract) {
          for (auto& value : layers) {
            auto layer = value.toMap();
            if (layer.value("id").toString() == "chain-text")
              layer.remove("canvasWidth");
            value = layer;
          }
        }
        content.insert("layers", layers);
        content.insert("masks", chain_masks);
        content.insert("paintBusy", composition_busy);
        content.insert("paintError", composition_error);
        return content;
      });
    }
    const QJsonObject tool_plan_content{
        {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
        {QStringLiteral("summary"), QStringLiteral("Read live status before final patch.")},
        {QStringLiteral("toolCalls"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("name"), inspection_routing_contract ? QStringLiteral("photo.edit_composition") :
                      QStringLiteral("photo.live_status")},
                 {QStringLiteral("arguments"), inspection_routing_contract ?
                      QJsonObject{{"assetId", "asset-1"}, {"operation", "inspect"}, {"values", QJsonObject{}}} : QJsonObject{}},
                 {QStringLiteral("reason"), QStringLiteral("Confirm current selection and draft state.")},
             },
         }},
    };
    const QJsonObject patch_content{
        {QStringLiteral("decision"), QStringLiteral("edit_intent")},
        {QStringLiteral("summary"), QStringLiteral("Warm balanced tool-informed draft.")},
        {QStringLiteral("styleConstraintCitations"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("field"), QStringLiteral("constraints.max_exposure_shift")},
                 {QStringLiteral("reason"), QStringLiteral("Bounds exposure changes.")},
             },
             QJsonObject{
                 {QStringLiteral("field"), QStringLiteral("allowedTools.color")},
                 {QStringLiteral("reason"), QStringLiteral("Color edits are allowed.")},
             },
         }},
        {QStringLiteral("editIntent"), QJsonObject{
            {QStringLiteral("goalSummary"), QStringLiteral("Warm restrained editorial look.")},
            {QStringLiteral("colorMode"), QStringLiteral("color")},
            {QStringLiteral("contrast"), QStringLiteral("natural")},
            {QStringLiteral("brightness"), QStringLiteral("brighter")},
            {QStringLiteral("palette"), QStringLiteral("warm")},
            {QStringLiteral("saturation"), QStringLiteral("more")},
            {QStringLiteral("crop"), QStringLiteral("preserve")},
            {QStringLiteral("detail"), QStringLiteral("natural")},
            {QStringLiteral("mood"), QStringLiteral("editorial")},
            {QStringLiteral("globalChangePolicy"), QStringLiteral("allow")},
            {QStringLiteral("confidence"), 0.82},
        }},
    };
    const QJsonObject second_tool_plan_content{
        {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
        {QStringLiteral("summary"), QStringLiteral("Read the typed recipe schema before completing the edit plan.")},
        {QStringLiteral("toolCalls"), QJsonArray{
            QJsonObject{
                {QStringLiteral("name"), QStringLiteral("photo.get_recipe_patch_schema")},
                {QStringLiteral("arguments"), QJsonObject{}},
                {QStringLiteral("reason"), QStringLiteral("Confirm supported typed edit fields from the returned editor context.")}}}}};
    QJsonArray fake_ollama_sequence{
        QJsonObject{
            {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
            {QStringLiteral("done"), true},
            {QStringLiteral("message"),
             QJsonObject{
                 {QStringLiteral("role"), QStringLiteral("assistant")},
             {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(tool_plan_content).toJson(QJsonDocument::Compact))},
             }},
        },
        QJsonObject{
            {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
            {QStringLiteral("done"), true},
            {QStringLiteral("message"),
             QJsonObject{
                 {QStringLiteral("role"), QStringLiteral("assistant")},
                 {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(second_tool_plan_content).toJson(QJsonDocument::Compact))},
             }},
        },
        QJsonObject{
            {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
            {QStringLiteral("done"), true},
            {QStringLiteral("message"),
             QJsonObject{
                 {QStringLiteral("role"), QStringLiteral("assistant")},
                 {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(patch_content).toJson(QJsonDocument::Compact))},
             }},
        },
    };
    if (composition_chain_contract) {
      QJsonArray chain_calls{
          QJsonObject{{"name", "photo.inspect_composition"}, {"arguments", QJsonObject{{"assetId", "asset-1"}}}},
          QJsonObject{{"name", "photo.edit_composition"}, {"arguments", QJsonObject{{"assetId", "asset-1"},
              {"operation", "select_subject"}, {"values", QJsonObject{}}}}},
          QJsonObject{{"name", "photo.edit_composition"}, {"arguments", QJsonObject{{"assetId", "asset-1"},
              {"operation", "add_text"}, {"values", QJsonObject{
                  {"text", composition_repair_rejected_critique_contract
                      ? QStringLiteral("APPROVED HEADLINE") : QStringLiteral("CHAIN")},
                  {"x", 300}, {"y", 160}}}}}},
          QJsonObject{{"name", "photo.edit_composition"}, {"arguments", QJsonObject{{"assetId", "asset-1"},
              {"operation", "typography"}, {"layerId", "chain-text"}, {"values", QJsonObject{{"size", 96}}}}}},
          QJsonObject{{"name", "photo.edit_composition"}, {"arguments", QJsonObject{{"assetId", "asset-1"},
              {"operation", "copy_selection"}, {"values", QJsonObject{{"maskId", "chain-subject-mask"}, {"independent", true}}}}}},
          QJsonObject{{"name", "photo.inspect_composition"}, {"arguments", QJsonObject{{"assetId", "asset-1"}}}}};
      if (composition_batch_review_scenario) {
        chain_calls = QJsonArray{
            QJsonObject{{"name", "photo.inspect_composition"},
                {"arguments", QJsonObject{{"assetId", "asset-1"}}}},
            QJsonObject{{"name", "photo.edit_composition"},
                {"arguments", QJsonObject{{"assetId", "asset-1"}, {"operation", "add_text"},
                    {"values", QJsonObject{{"text", "CHAIN"}, {"x", 300}, {"y", 160}}}}}}};
      } else if (tool_plan_validation_contract) {
        const auto text_call = [](const QString& text, int y) {
          return QJsonObject{{"name", "photo.edit_composition"}, {"arguments", QJsonObject{
              {"assetId", "asset-1"}, {"operation", "add_text"},
              {"values", QJsonObject{{"text", text}, {"x", 300}, {"y", y}}}}}};
        };
        const auto typography_call = [](const QString& layer_id) {
          return QJsonObject{{"name", "photo.edit_composition"}, {"arguments", QJsonObject{
              {"assetId", "asset-1"}, {"operation", "typography"}, {"layerId", layer_id},
              {"values", QJsonObject{{"size", 96}}}}}};
        };
        const auto inspection_call = QJsonObject{{"name", "photo.inspect_composition"},
            {"arguments", QJsonObject{{"assetId", "asset-1"}}}};
        const auto selection_call = QJsonObject{{"name", "photo.edit_composition"}, {"arguments", QJsonObject{
            {"assetId", "asset-1"}, {"operation", "select_subject"}, {"values", QJsonObject{}}}}};
        const auto copy_call = QJsonObject{{"name", "photo.edit_composition"}, {"arguments", QJsonObject{
            {"assetId", "asset-1"}, {"operation", "copy_selection"},
            {"values", QJsonObject{{"maskId", "chain-subject-mask"}, {"independent", true}}}}}};
        if (tool_plan_validation_progress_contract) {
          chain_calls = QJsonArray{
              inspection_call, selection_call,
              text_call(QStringLiteral("AUTUMN OPEN HOUSE"), 120), typography_call(QStringLiteral("validation-text-1")),
              text_call(QStringLiteral("COMMUNITY EVENT"), 360), typography_call(QStringLiteral("validation-text-2")),
              text_call(QStringLiteral("OCTOBER 20"), 600), typography_call(QStringLiteral("validation-text-3")),
              text_call(QStringLiteral("SATURDAY 10 AM"), 840), typography_call(QStringLiteral("validation-text-4")),
              copy_call, inspection_call};
        } else {
          chain_calls = QJsonArray{
              inspection_call, selection_call,
              text_call(QStringLiteral("AUTUMN OPEN HOUSE"), 120),
              text_call(QStringLiteral("COMMUNITY EVENT"), 480),
              text_call(QStringLiteral("OCTOBER 20"), 760),
              typography_call(QStringLiteral("validation-text-1")), copy_call, inspection_call};
        }
      } else if (long_task_contract) {
        const auto final_inspection = chain_calls.last();
        chain_calls.removeLast();
        for (int i = 1; i <= long_task_update_count; ++i)
          chain_calls.append(QJsonObject{{"name", "photo.edit_composition"}, {"arguments", QJsonObject{
              {"assetId", "asset-1"}, {"operation", "opacity"}, {"layerId", "chain-text"},
              {"values", QJsonObject{{"opacity", i / 100.0}}}}}});
        chain_calls.append(final_inspection);
      } else if (repeated_tool_results_contract) {
        const auto inspection = chain_calls.first();
        chain_calls = {};
        for (int i = 0; i < 8; ++i) chain_calls.append(inspection);
      }
      const auto final_response = fake_ollama_sequence.last();
      fake_ollama_sequence = {};
      auto validation_plan_text_count = 0;
      for (const auto& call : chain_calls) {
        const auto plan_operation = call.toObject().value("arguments").toObject()
                                        .value("operation").toString();
        if (tool_plan_validation_contract && plan_operation == QStringLiteral("add_text"))
          ++validation_plan_text_count;
        if (tool_plan_validation_contract &&
            plan_operation == QStringLiteral("typography")) {
          const auto rejected_layer_id = tool_plan_validation_progress_contract
              ? QStringLiteral("validation-text-%1").arg(validation_plan_text_count)
              : QStringLiteral("validation-text-1");
          const QJsonObject valid_but_withheld_prefix{{"name", "photo.edit_composition"},
              {"arguments", QJsonObject{{"assetId", "asset-1"}, {"operation", "opacity"},
                  {"layerId", rejected_layer_id}, {"values", QJsonObject{{"opacity", 0.9}}}}}};
          const QJsonObject invalid_typography_call{{"name", "photo.edit_composition"},
              {"arguments", QJsonObject{{"assetId", "asset-1"}, {"operation", "typography"},
                  {"layerId", rejected_layer_id}, {"values", QJsonObject{
                      {"size", 96}, {"x", 300}, {"y", 160}}}}}};
          const QJsonObject invalid_plan{{"decision", "tool_call_plan"},
              {"summary", "Place the title and apply typography."},
              {"toolCalls", QJsonArray{valid_but_withheld_prefix, invalid_typography_call}}};
          const auto invalid_response = QJsonObject{{"model", "qwen2.5vl:7b"}, {"done", true},
              {"message", QJsonObject{{"role", "assistant"}, {"content", QString::fromUtf8(
                  QJsonDocument(invalid_plan).toJson(QJsonDocument::Compact))}}}};
          fake_ollama_sequence.append(invalid_response);
          if (tool_plan_validation_repeat_contract) {
            fake_ollama_sequence.append(invalid_response);
            break;
          }
          const QJsonObject corrected_typography_call{{"name", "photo.edit_composition"},
              {"arguments", QJsonObject{{"assetId", "asset-1"}, {"operation", "typography"},
                  {"layerId", rejected_layer_id}, {"values", QJsonObject{{"size", 96}}}}}};
          const QJsonObject corrected_plan{{"decision", "tool_call_plan"},
              {"summary", "Apply only supported typography values to the existing headline."},
              {"toolCalls", QJsonArray{corrected_typography_call}}};
          fake_ollama_sequence.append(QJsonObject{{"model", "qwen2.5vl:7b"}, {"done", true},
              {"message", QJsonObject{{"role", "assistant"}, {"content", QString::fromUtf8(
                  QJsonDocument(corrected_plan).toJson(QJsonDocument::Compact))}}}});
          continue;
        }
        if (tool_correction_contract && call.toObject().value("arguments").toObject().value("operation") == "typography") {
          QJsonArray bad_calls{
              QJsonObject{{"name", "photo.edit_composition"}, {"arguments", QJsonObject{{"assetId", "asset-1"},
                  {"operation", "typography"}, {"layerId", "missing-layer"}, {"values", QJsonObject{{"size", 96}}}}}},
              QJsonObject{{"name", "photo.edit_composition"}, {"arguments", QJsonObject{{"assetId", "asset-1"},
                  {"operation", "opacity"}, {"layerId", "chain-text"}, {"values", QJsonObject{{"opacity", 0.123456}}}}}}};
          if (!tool_correction_repeat_contract)
            bad_calls.prepend(QJsonObject{{"name", "photo.edit_composition"}, {"arguments", QJsonObject{{"assetId", "asset-1"},
                {"operation", "opacity"}, {"layerId", "chain-text"}, {"values", QJsonObject{{"opacity", 0.5}}}}}});
          const QJsonObject rejected{{"decision", "tool_call_plan"}, {"summary", "Style the new title then change opacity."}, {"toolCalls", bad_calls}};
          for (int i = 0; i < (tool_correction_repeat_contract ? 8 : 1); ++i)
            fake_ollama_sequence.append(QJsonObject{{"model", "qwen2.5vl:7b"}, {"done", true},
                {"message", QJsonObject{{"role", "assistant"}, {"content", QString::fromUtf8(
                    QJsonDocument(rejected).toJson(QJsonDocument::Compact))}}}});
          if (tool_correction_repeat_contract || tool_correction_unresolved_contract) break;
        }
        QJsonArray calls{call};
        if (composition_extra_call_contract && call.toObject().value("arguments").toObject().value("operation").toString() == "copy_selection")
          calls.append(QJsonObject{{"name", "photo.edit_composition"}, {"arguments", QJsonObject{{"assetId", "asset-1"},
              {"operation", "move"}, {"layerId", "chain-text"}, {"values", QJsonObject{{"position", 1}}}}}});
        const QJsonObject content{{"decision", "tool_call_plan"}, {"summary", "Continue the dependent composition edit."},
            {"toolCalls", calls}};
        fake_ollama_sequence.append(QJsonObject{{"model", "qwen2.5vl:7b"}, {"done", true},
            {"message", QJsonObject{{"role", "assistant"}, {"content", QString::fromUtf8(
                QJsonDocument(content).toJson(QJsonDocument::Compact))}}}});
      }
      fake_ollama_sequence.append(final_response);
      if (composition_chain_contract && !composition_repair_scenario &&
          !tool_plan_validation_recovery_contract && !tool_plan_validation_progress_contract) {
        fake_ollama_sequence.append(QJsonObject{{"model", "qwen2.5vl:7b"}, {"done", true},
            {"message", QJsonObject{{"role", "assistant"}, {"content", QString::fromUtf8(
                QJsonDocument(QJsonObject{
                    {"pass", true}, {"needsUserReview", false},
                    {"summary", "The corrected composition is complete and readable."},
                    {"tasteConfidence", "high"}, {"issues", QJsonArray{}},
                    {"repairHints", QJsonArray{}}, {"warnings", QJsonArray{}},
                }).toJson(QJsonDocument::Compact))}}}});
      }
      if (tool_plan_validation_recovery_contract || tool_plan_validation_progress_contract) {
        fake_ollama_sequence.append(QJsonObject{{"model", "qwen2.5vl:7b"}, {"done", true},
            {"message", QJsonObject{{"role", "assistant"}, {"content", QString::fromUtf8(
                QJsonDocument(QJsonObject{
                    {"pass", true}, {"needsUserReview", false},
                    {"summary", "The corrected rendered composition is complete and readable."},
                    {"tasteConfidence", "high"}, {"issues", QJsonArray{}},
                    {"repairHints", QJsonArray{}}, {"warnings", QJsonArray{}},
                }).toJson(QJsonDocument::Compact))}}}});
      }
      if (composition_repair_scenario) {
        QJsonObject original_add_text_arguments;
        for (const auto& call : chain_calls) {
          const auto object = call.toObject();
          const auto arguments = object.value("arguments").toObject();
          if (object.value("name").toString() == "photo.edit_composition" &&
              arguments.value("operation").toString() == "add_text") {
            original_add_text_arguments = arguments;
            break;
          }
        }
        if (original_add_text_arguments.isEmpty())
          return fail("Composition repair fixture must retain the exact successful add_text receipt identity");
        const auto response_for = [](const QJsonObject& content) {
          return QJsonObject{{"model", "qwen2.5vl:7b"}, {"done", true},
              {"message", QJsonObject{{"role", "assistant"},
                  {"content", QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact))}}}};
        };
        // The rendered-proof critique is a real request in the shared FIFO.
        // Keep its schema-valid response between the initial edit plan and
        // native repair plans so the fixture exercises the same turn ordering
        // as production instead of accidentally treating a tool plan as a
        // critique response.
        const auto initial_proof_critique = response_for(QJsonObject{
            {"pass", false}, {"needsUserReview", true},
            {"summary", composition_repair_rejected_critique_contract
                ? QStringLiteral("The approved copy and layout metadata pass, but the performer and framing still do not read as an integrated poster.")
                : QStringLiteral("The rendered title does not match the approved exact copy.")},
            {"tasteConfidence", "high"},
            {"issues", composition_repair_rejected_critique_contract
                ? QJsonArray{"The performer does not visibly overlap the headline and the framing lacks enough context around the subject."}
                : QJsonArray{"The title still reads CHAIN."}},
            {"repairHints", composition_repair_rejected_critique_contract
                ? QJsonArray{"Do not alter the exact headline; safely decline if the subject overlap or framing cannot be corrected from current evidence."}
                : QJsonArray{"Update the existing title layer to APPROVED HEADLINE."}},
            {"warnings", QJsonArray{}},
        });
        const auto corrected_proof_critique = response_for(QJsonObject{
            {"pass", true}, {"needsUserReview", false},
            {"summary", "The rendered composition matches the approved headline and layer order."},
            {"tasteConfidence", "high"}, {"issues", QJsonArray{}},
            {"repairHints", QJsonArray{}}, {"warnings", QJsonArray{}},
        });
        if (composition_batch_review_scenario) {
          if (!fake_ollama_sequence.isEmpty()) fake_ollama_sequence.removeLast();
          fake_ollama_sequence.append(initial_proof_critique);
          const QJsonObject intervening_inspection{{"name", "photo.inspect_composition"},
              {"arguments", QJsonObject{{"assetId", "asset-1"}}}};
          fake_ollama_sequence.append(response_for(QJsonObject{
              {"decision", "tool_call_plan"},
              {"summary", "Read the current native layers before choosing a correction."},
              {"toolCalls", QJsonArray{intervening_inspection}},
          }));
          const QJsonObject correction_values{{"text", "APPROVED HEADLINE"}};
          const QJsonObject correction_arguments{{"assetId", "asset-1"}, {"operation", "text"},
              {"layerId", "chain-text"}, {"values", correction_values}};
          const QJsonObject correct_existing_title{{"name", "photo.edit_composition"},
              {"arguments", correction_arguments},
              {"reason", "Correct the current headline after reviewing the latest native composition assessment."}};
          fake_ollama_sequence.append(response_for(QJsonObject{
              {"decision", "tool_call_plan"},
              {"summary", "Correct the existing headline in place using its returned ID."},
              {"toolCalls", QJsonArray{correct_existing_title}},
          }));
          fake_ollama_sequence.append(corrected_proof_critique);
          const QJsonObject final_readback_call{{"name", "photo.inspect_composition"},
              {"arguments", QJsonObject{{"assetId", "asset-1"}}}};
          fake_ollama_sequence.append(response_for(QJsonObject{
              {"decision", "tool_call_plan"},
              {"summary", "Confirm the corrected headline in a fresh native readback before completing."},
              {"toolCalls", QJsonArray{final_readback_call}},
          }));
          if (composition_batch_invalid_terminal_contract) {
            // A proof-critique object is not a planner completion. The
            // composition-only request must fail closed instead of replacing
            // an invalid terminal response with a deterministic recipe draft.
            fake_ollama_sequence.append(corrected_proof_critique);
          } else {
            const QJsonObject completion_intent{
                {QStringLiteral("decision"), QStringLiteral("edit_intent")},
                {QStringLiteral("compositionPhotoBaseline"), QJsonObject{
                    {QStringLiteral("preMutationInspectionVerified"), true},
                    {QStringLiteral("sourceRecipeJson"), QStringLiteral("model-supplied-forgery")},
                    {QStringLiteral("preMutationInspectionReceiptSha256"), QString(64, QLatin1Char('0'))},
                }},
                {QStringLiteral("summary"), QStringLiteral(
                    "The approved headline is complete and confirmed by fresh native readback.")},
                {QStringLiteral("styleConstraintCitations"), QJsonArray{
                    QJsonObject{{QStringLiteral("field"), QStringLiteral("allowedTools.graphics")},
                        {QStringLiteral("reason"), QStringLiteral("The approved exact headline is present in the native composition.")}},
                    QJsonObject{{QStringLiteral("field"), QStringLiteral("allowedTools.layers_luts")},
                        {QStringLiteral("reason"), QStringLiteral("The existing text layer was corrected and inspected.")}},
                }},
                {QStringLiteral("editIntent"), QJsonObject{
                    {QStringLiteral("goalSummary"), QStringLiteral("Finish the approved poster headline and preserve the photo.")},
                    {QStringLiteral("colorMode"), QStringLiteral("preserve")},
                    {QStringLiteral("contrast"), QStringLiteral("natural")},
                    {QStringLiteral("brightness"), QStringLiteral("preserve")},
                    {QStringLiteral("palette"), QStringLiteral("preserve")},
                    {QStringLiteral("saturation"), QStringLiteral("preserve")},
                    {QStringLiteral("crop"), QStringLiteral("preserve")},
                    {QStringLiteral("detail"), QStringLiteral("natural")},
                    {QStringLiteral("mood"), QStringLiteral("natural")},
                    {QStringLiteral("referenceStrength"), 0.0},
                    {QStringLiteral("globalChangePolicy"), QStringLiteral("preserve")},
                    {QStringLiteral("completionStatus"), QStringLiteral("complete")},
                    {QStringLiteral("confidence"), 0.95},
                    {QStringLiteral("visualFindings"), QJsonArray{}},
                    {QStringLiteral("localizedActions"), QJsonArray{}},
                    {QStringLiteral("requestedActions"), QJsonArray{}},
                    {QStringLiteral("requiresApproval"), QJsonArray{}},
                    {QStringLiteral("retouchActions"), QJsonArray{}},
                }},
            };
            fake_ollama_sequence.append(response_for(completion_intent));
            // Native readback has already confirmed the accepted layer; the
            // final rendered proof still gets its own model critique.
            fake_ollama_sequence.append(corrected_proof_critique);
          }
        } else if (composition_repair_contract) {
          const auto plan_response_for_calls = [&response_for](
              const QString& summary, const QJsonArray& calls) {
            return response_for(QJsonObject{
                {"decision", "tool_call_plan"},
                {"summary", summary},
                {"toolCalls", calls},
            });
          };
          fake_ollama_sequence = {};
          for (int index = 0; index < 3; ++index) {
            fake_ollama_sequence.append(plan_response_for_calls(
                QStringLiteral("Continue the initial native composition batch."),
                QJsonArray{chain_calls.at(index).toObject()}));
          }
          // The first rendered critique follows the initial text mutation.
          fake_ollama_sequence.append(initial_proof_critique);

          const QJsonObject replay_add_text{{"name", "photo.edit_composition"},
              {"arguments", original_add_text_arguments},
              {"reason", "A repeated successful mutation must be rejected without replay."}};
          fake_ollama_sequence.append(plan_response_for_calls(
              QStringLiteral("Retry the prior title creation before making a correction."),
              QJsonArray{replay_add_text}));

          const QJsonObject correction_values{{"text", "APPROVED HEADLINE"}};
          const QJsonObject correction_arguments{{"assetId", "asset-1"}, {"operation", "text"},
              {"layerId", "chain-text"}, {"values", correction_values}};
          const QJsonObject correct_existing_title{{"name", "photo.edit_composition"},
              {"arguments", correction_arguments},
              {"reason", "Correct the current title using its stable returned layer ID."}};
          // Preserve the supported composition-chain order while finishing
          // typography, mask details, and exact copy in one productive batch.
          fake_ollama_sequence.append(plan_response_for_calls(
              QStringLiteral("Finish typography and mask composition, then correct the exact headline."),
              QJsonArray{chain_calls.at(3).toObject(),
                  chain_calls.at(4).toObject(), correct_existing_title}));
          fake_ollama_sequence.append(corrected_proof_critique);

          fake_ollama_sequence.append(plan_response_for_calls(
              QStringLiteral("Confirm the completed composition with a fresh native readback."),
              QJsonArray{chain_calls.at(5).toObject()}));
          fake_ollama_sequence.append(final_response);
          // The final applied recipe still receives its independent rendered
          // proof critique after the native planning reviews have passed.
          fake_ollama_sequence.append(corrected_proof_critique);
        } else {
          fake_ollama_sequence.append(initial_proof_critique);
        if (composition_repair_rejected_critique_contract) {
          fake_ollama_sequence.append(response_for(QJsonObject{
              {"decision", "cannot_complete_safely"},
              {"summary", "I cannot safely resolve the visual subject overlap or framing from the current evidence, so I am stopping with the edits available for review."},
              {"reason", "The rendered result needs a framing or subject-layer correction that I cannot ground safely."},
          }));
        } else {
          const QJsonObject replay_add_text{{"name", "photo.edit_composition"},
              {"arguments", original_add_text_arguments},
              {"reason", "A deliberately repeated successful mutation must be rejected without replay."}};
          fake_ollama_sequence.append(response_for(QJsonObject{
              {"decision", "tool_call_plan"},
              {"summary", "Retry the prior title creation, then correct its existing layer."},
              {"toolCalls", QJsonArray{replay_add_text}},
          }));
          const QJsonObject correction_values{{"text", "APPROVED HEADLINE"}};
          const QJsonObject correction_arguments{{"assetId", "asset-1"}, {"operation", "text"},
              {"layerId", "chain-text"}, {"values", correction_values}};
          const QJsonObject correct_existing_title{{"name", "photo.edit_composition"},
              {"arguments", correction_arguments},
              {"reason", "Correct the current title using its stable returned layer id."}};
          fake_ollama_sequence.append(response_for(QJsonObject{
              {"decision", "tool_call_plan"},
              {"summary", "Update the existing headline in place."},
              {"toolCalls", QJsonArray{correct_existing_title}},
          }));
          fake_ollama_sequence.append(final_response);
          fake_ollama_sequence.append(corrected_proof_critique);
        }
        }
      }
    }
    const auto revision_request_path = runtime_root.filePath(QStringLiteral("revision-model-requests.jsonl"));
    if (revision_contract) qputenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH", revision_request_path.toUtf8());
    if (composition_repair_scenario)
      qputenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH", composition_repair_request_path.toUtf8());
    qputenv(
        "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON",
        QJsonDocument(fake_ollama_sequence).toJson(QJsonDocument::Compact));
    qputenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS", QByteArrayLiteral("40"));
    agent_edit.setSelectedModelId(QStringLiteral("ollama-qwen-tool-plan-test"));
    if (composition_repair_scenario) {
      auto approved_plan = QVariantMap{
          {QStringLiteral("expectedLayerOrder"), composition_batch_review_scenario
              ? QStringList{QStringLiteral("chain-text")}
              : QStringList{QStringLiteral("chain-cutout"), QStringLiteral("chain-text")}},
          {QStringLiteral("actions"), QVariantList{QVariantMap{
              {QStringLiteral("kind"), QStringLiteral("text")},
              {QStringLiteral("summary"), QStringLiteral("Place the exact headline in the upper third.")}}}},
      };
      if (composition_batch_review_scenario) {
        approved_plan.insert(QStringLiteral("constraints"), QStringList{
            QStringLiteral("Preserve original photo, framing, and color."),
            QStringLiteral("Build only editable text layers; do not change photographic pixels or tone."),
            QStringLiteral("Mountains and lake must remain visible (text placement must avoid critical visual elements or use opacity/contrast if needed, though prompt implies placement in 'open sky')."),
        });
      }
      QVariantMap approved_snapshot{
          {QStringLiteral("approvedInstructions"), QStringLiteral(
              "Create a restrained editorial poster with the exact headline APPROVED HEADLINE in the upper third.")},
          {QStringLiteral("targetIds"), QStringList{QStringLiteral("asset-1")}},
          {QStringLiteral("targetSnapshot"), QVariantList{QVariantMap{
              {QStringLiteral("assetId"), QStringLiteral("asset-1")},
              {QStringLiteral("name"), QStringLiteral("Full-resolution composition fixture")}}}},
          {QStringLiteral("taskScopedCapabilities"), composition_batch_review_scenario
              ? QStringList{QStringLiteral("graphics"), QStringLiteral("layers_luts")}
              : QStringList{QStringLiteral("graphics"), QStringLiteral("layers_luts"), QStringLiteral("masks")}},
          {QStringLiteral("exactCopy"), QStringLiteral("APPROVED HEADLINE")},
          {QStringLiteral("plan"), approved_plan},
      };
      if (composition_batch_review_scenario) {
        const QString text_only_prompt = QStringLiteral(
            "Create a restrained cover on the existing photograph. Preserve the original photograph, framing, and color. "
            "Use only one editable text layer with the exact headline APPROVED HEADLINE in the upper third. "
            "Keep the type warm white, legible, and within safe margins.");
        const auto snapshot_for_prompt = [&](const QString& prompt) {
          const QJsonObject exact_copy{
              {QStringLiteral("source"), QStringLiteral("prompt")},
              {QStringLiteral("text"), QStringLiteral("APPROVED HEADLINE")},
          };
          const QJsonObject brief{
              {QStringLiteral("answers"), QJsonObject{}},
              {QStringLiteral("continuationTaskId"), QJsonValue(QJsonValue::Null)},
              {QStringLiteral("deliverables"), QJsonArray{QStringLiteral(
                  "Editable composition on asset-1 (2400x1600).")}},
              {QStringLiteral("exactCopy"), QJsonArray{exact_copy}},
              {QStringLiteral("isRevision"), false},
              {QStringLiteral("plan"), QJsonObject::fromVariantMap(approved_plan)},
              {QStringLiteral("prompt"), prompt},
              {QStringLiteral("revisionContext"), QJsonValue(QJsonValue::Null)},
          };
          auto snapshot = approved_snapshot;
          snapshot.insert(QStringLiteral("approvedInstructions"),
              QStringLiteral("Complete the approved outcome, retaining exact copy, targets and deliverables.\nApproved brief:\n%1")
                  .arg(QString::fromUtf8(QJsonDocument(brief).toJson(QJsonDocument::Compact))));
          return snapshot;
        };
        approved_snapshot = snapshot_for_prompt(text_only_prompt);
        const auto high_contrast_text_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Use high-contrast typography for the headline."));
        const auto improve_text_contrast_against_photo_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Improve text contrast against the photo."));
        const auto white_text_image_contrast_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Increase contrast between white text and the image."));
        const auto avoid_photo_contrast_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Avoid changing photo contrast."));
        const auto title_brighter_text_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Make the title brighter."));
        const auto cover_copy_color_text_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Change the cover copy color to warm white."));
        const auto photo_text_contrast_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Improve contrast between the photo and text."));
        const auto known_source_clipping_snapshot = snapshot_for_prompt(
            QStringLiteral("The source photograph has clipping. Preserve the photograph, framing, and color. "
                           "Add only the approved headline as editable text."));
        const auto explicit_photo_contrast_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Improve contrast in the photograph before adding text."));
        const auto explicit_photo_exposure_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Adjust exposure on the photograph before adding text."));
        const auto mixed_photo_exposure_preserve_crop_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Adjust exposure on the photograph but do not crop it."));
        const auto local_negation_photo_exposure_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Do not change the crop and adjust exposure on the photograph."));
        const auto brighten_photo_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Brighten the photo before adding text."));
        const auto brighter_image_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Make the image brighter before adding text."));
        const auto warm_photo_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Warm up the photograph before adding text."));
        const auto adjust_photo_tones_snapshot = snapshot_for_prompt(
            text_only_prompt + QStringLiteral(" Adjust photo tones before adding text."));
        auto typed_photo_scope_snapshot = snapshot_for_prompt(text_only_prompt);
        typed_photo_scope_snapshot.insert(QStringLiteral("photoQualityRequested"), true);
        auto malformed_canonical_brief_snapshot = approved_snapshot;
        malformed_canonical_brief_snapshot.insert(QStringLiteral("approvedInstructions"),
            QStringLiteral("Complete the approved outcome.\nApproved brief:\n{\"prompt\":"));

        const auto text_only_scope_rejected =
            !photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(approved_snapshot);
        const auto text_contrast_scope_rejected =
            !photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(high_contrast_text_snapshot);
        const auto text_contrast_against_photo_rejected =
            !photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                improve_text_contrast_against_photo_snapshot);
        const auto white_text_image_contrast_rejected =
            !photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                white_text_image_contrast_snapshot);
        const auto avoid_photo_contrast_rejected =
            !photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                avoid_photo_contrast_snapshot);
        const auto title_brighter_is_text_only =
            !photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                title_brighter_text_snapshot);
        const auto cover_copy_color_is_text_only =
            !photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                cover_copy_color_text_snapshot);
        const auto photo_text_contrast_is_text_only =
            !photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                photo_text_contrast_snapshot);
        const auto source_clipping_does_not_request_repair =
            !photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                known_source_clipping_snapshot);
        const auto explicit_photo_contrast_detected =
            photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                explicit_photo_contrast_snapshot);
        const auto explicit_photo_exposure_detected =
            photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                explicit_photo_exposure_snapshot);
        const auto mixed_photo_scope_detected =
            photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                mixed_photo_exposure_preserve_crop_snapshot);
        const auto local_negation_photo_scope_detected =
            photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                local_negation_photo_exposure_snapshot);
        const auto brighten_photo_detected =
            photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                brighten_photo_snapshot);
        const auto make_image_brighter_detected =
            photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                brighter_image_snapshot);
        const auto warm_photo_detected =
            photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                warm_photo_snapshot);
        const auto photo_tone_adjustment_detected =
            photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                adjust_photo_tones_snapshot);
        const auto typed_photo_scope_detected =
            photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                typed_photo_scope_snapshot);
        const auto malformed_brief_fails_closed =
            photo_editor::desktop::agent_approved_task_has_explicit_photo_quality_scope(
                malformed_canonical_brief_snapshot);
        if (!text_only_scope_rejected || !text_contrast_scope_rejected ||
            !text_contrast_against_photo_rejected || !white_text_image_contrast_rejected ||
            !avoid_photo_contrast_rejected || !title_brighter_is_text_only ||
            !cover_copy_color_is_text_only || !photo_text_contrast_is_text_only ||
            !source_clipping_does_not_request_repair || !explicit_photo_contrast_detected ||
            !explicit_photo_exposure_detected || !mixed_photo_scope_detected ||
            !local_negation_photo_scope_detected ||
            !brighten_photo_detected || !make_image_brighter_detected || !warm_photo_detected ||
            !photo_tone_adjustment_detected || !typed_photo_scope_detected ||
            !malformed_brief_fails_closed) {
          std::cerr << "approved photo scope classification diagnostic="
                    << QJsonDocument::fromVariant(QVariantMap{
                        {QStringLiteral("textOnlyPreserveScopeRejected"), text_only_scope_rejected},
                        {QStringLiteral("textContrastRejected"), text_contrast_scope_rejected},
                        {QStringLiteral("textContrastAgainstPhotoRejected"), text_contrast_against_photo_rejected},
                        {QStringLiteral("whiteTextImageContrastRejected"), white_text_image_contrast_rejected},
                        {QStringLiteral("avoidPhotoContrastRejected"), avoid_photo_contrast_rejected},
                        {QStringLiteral("titleBrighterIsTextOnly"), title_brighter_is_text_only},
                        {QStringLiteral("coverCopyColorIsTextOnly"), cover_copy_color_is_text_only},
                        {QStringLiteral("photoTextContrastIsTextOnly"), photo_text_contrast_is_text_only},
                        {QStringLiteral("sourceClippingNotRequest"), source_clipping_does_not_request_repair},
                        {QStringLiteral("explicitPhotoContrastDetected"), explicit_photo_contrast_detected},
                        {QStringLiteral("explicitPhotoExposureDetected"), explicit_photo_exposure_detected},
                        {QStringLiteral("mixedPhotoScopeDetected"), mixed_photo_scope_detected},
                        {QStringLiteral("localNegationPhotoScopeDetected"), local_negation_photo_scope_detected},
                        {QStringLiteral("brightenPhotoDetected"), brighten_photo_detected},
                        {QStringLiteral("makeImageBrighterDetected"), make_image_brighter_detected},
                        {QStringLiteral("warmPhotoDetected"), warm_photo_detected},
                        {QStringLiteral("photoToneAdjustmentDetected"), photo_tone_adjustment_detected},
                        {QStringLiteral("typedPhotoScopeDetected"), typed_photo_scope_detected},
                        {QStringLiteral("malformedBriefFailsClosed"), malformed_brief_fails_closed},
                    }).toJson(QJsonDocument::Compact).toStdString() << '\n';
          return fail("Text contrast advice and known source clipping must not opt a frozen text-only task into photo-quality repair");
        }
      }
      const auto approved = agent_edit.setApprovedTaskSnapshot(approved_snapshot);
      if (!approved.value(QStringLiteral("ok")).toBool())
        return fail("Composition repair requires an approved exact-copy and text-action snapshot");
    }
    QVariantMap mcp_draft_report;
    auto mcp_draft_completion_count = 0;
    const auto mcp_draft_connection = QObject::connect(
        &agent_edit,
        &photo_editor::desktop::AgentEditController::draftRunFinished,
        &agent_edit,
        [&](const QString&, const QVariantMap& report) {
          mcp_draft_report = report;
          ++mcp_draft_completion_count;
        });
    QElapsedTimer mcp_draft_start_timer;
    mcp_draft_start_timer.start();
    const auto mcp_draft_start = sync_task_contract
        ? agent_edit.runDraftOnSelected(QStringLiteral("Create a restrained warm overall look consistent with the active style."))
        : agent_edit.startDraftOnSelected(QStringLiteral("Create a restrained warm overall look consistent with the active style."));
    if (sync_task_contract) mcp_draft_report = mcp_draft_start;
    const auto mcp_draft_start_duration_ms = mcp_draft_start_timer.elapsed();
    const auto mcp_draft_completed = sync_task_contract || wait_for_condition(
        [&]() { return mcp_draft_completion_count == 1; }, long_task_contract ? 150000 :
            composition_unlimited_wait_contract || composition_repair_scenario ? 90000 :
            composition_chain_contract ? 20000 : 10000);
    QObject::disconnect(mcp_draft_connection);
    qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON");
    qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS");
    if (sync_task_contract) {
      const auto success = sync_task_lifecycle;
      const auto expected_actions = sync_task_admission ? QStringList{"begin"}
          : sync_task_cancel ? QStringList{"begin", "cancel", "revert"}
          : sync_task_failure ? QStringList{"begin", "interrupted"}
          : sync_task_conflict ? QStringList{"begin", "check_revert", "interrupted"}
          : QStringList{"begin", "check_revert", "review"};
      const auto expected_operations = sync_task_admission ? QStringList{} : (success || sync_task_conflict)
          ? QStringList{"select_subject", "add_text", "typography", "copy_selection", "move"}
          : QStringList{"select_subject", "add_text", "typography", "copy_selection"};
      if (mcp_draft_completion_count != 0 || mcp_draft_report.value("ok").toBool() != success ||
          agent_edit.running() || agent_edit.draftRequestPending() || !agent_edit.activeDraftRequestId().isEmpty() ||
          agent_edit.draftActive() != success || chain_operations != expected_operations || task_actions != expected_actions) {
        std::cerr << QJsonDocument::fromVariant(QVariantMap{{"ok", mcp_draft_report.value("ok")},
            {"failureReason", mcp_draft_report.value("failureReason")}, {"taskTransaction", mcp_draft_report.value("taskTransaction")},
            {"running", agent_edit.running()}, {"draftActive", agent_edit.draftActive()}, {"actions", task_actions},
            {"operations", chain_operations}}).toJson().toStdString();
        return fail("Synchronous entry must acquire/finalize one task, preserve the synchronous return, and block unowned or canceled mutations");
      }
      if (success && (!agent_edit.rollbackDraft().value("ok").toBool() ||
          task_actions != QStringList{"begin", "check_revert", "review", "check_revert", "revert"} ||
          catalog_model.assetById("asset-1").value("adjustmentPatchJson").toString() != task_before_recipe))
        return fail("The synchronous task must restore its full original recipe");
      if (sync_task_conflict && (!mcp_draft_report.value("taskStateConflict").toBool() ||
          task_manual_recipe.isEmpty() || catalog_model.assetById("asset-1").value("adjustmentPatchJson").toString() != task_manual_recipe))
        return fail("Final catalog application must not adopt or overwrite a later manual edit");
      if (sync_task_failure && (!agent_edit.revertTask().value("ok").toBool() || task_actions.last() != "revert"))
        return fail("A partial synchronous failure must remain reversible");
      bridge.stop();
      std::cout << "Owned synchronous task admission, native identity, completion/cancellation and reversal passed.\n";
      return EXIT_SUCCESS;
    }
    if (fresh_proof_contract) {
      qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH");
      const auto succeeds = !proof_cancel && !proof_conflict && !proof_failure;
      if (!mcp_draft_completed || mcp_draft_completion_count != 1 || agent_edit.running() ||
          mcp_draft_report.value("ok").toBool() != succeeds)
        return fail("Planning proof must complete once or stop before using unavailable/changed state");
      if (!succeeds) {
        if (chain_operations != QStringList{"select_subject"} || native_proof_mutation_count != 1 ||
            task_actions != (proof_cancel ? QStringList{"begin", "cancel", "revert"} : QStringList{"begin", "interrupted"})) {
          std::cerr << QJsonDocument::fromVariant(mcp_draft_report).toJson().toStdString();
          return fail("Proof cancellation, conflict or failure must withhold every later mutation");
        }
        if (proof_conflict && (task_manual_recipe.isEmpty() || catalog_model.assetById("asset-1").value("adjustmentPatchJson") != task_manual_recipe))
          return fail("A manual edit during proof rendering must remain intact");
        bridge.stop();
        return EXIT_SUCCESS;
      }
      QFile transport(proof_transport_path);
      if (!transport.open(QIODevice::ReadOnly)) return fail("Capture planning image payloads");
      const auto captured_payloads = transport.readAll().split('\n');
      QSet<QString> recipes, paths, images;
      const auto rounds = mcp_draft_report.value("modelToolPlan").toMap().value("rounds").toList();
      if (rounds.size() != 6 || native_proof_mutation_count != 5)
        return fail("Every completed construction round must carry fresh rendered evidence");
      for (const auto& value : rounds) {
        const auto evidence = value.toMap().value("visualEvidence").toMap();
        const auto path = evidence.value("currentProofPath").toString();
        QFile proof(path);
        if (!path.contains(mcp_draft_start.value("requestId").toString()) ||
            evidence.value("assetId") != "asset-1" || evidence.value("snapshotSource") != "fixture-native-photo" ||
            !proof.open(QIODevice::ReadOnly) || evidence.value("recipeSha256").toString().isEmpty())
          return fail("Planning evidence must identify the live document and exact rendered recipe");
        const auto bytes = proof.readAll();
        if (QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != evidence.value("proofSha256"))
          return fail("Planning evidence digest must identify the completed PNG bytes");
        auto image = QImage::fromData(bytes);
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        if (image.isNull() || !image.save(&buffer, "JPEG", 84)) return fail("Decode the actual model planning image");
        const auto expected_image = QString::fromLatin1(jpeg.toBase64());
        bool sent = false;
        for (const auto& payload : captured_payloads) {
          const auto messages = QJsonDocument::fromJson(payload).object().value("messages").toArray();
          // The host binds the immutable local path and digest in its report;
          // the model transport should receive pixels, not a private filesystem
          // path that the model cannot use.
          if (messages.size() >= 2 &&
              messages[1].toObject().value("images").toArray().contains(expected_image)) {
            sent = true;
          }
        }
        if (!sent) return fail("The next model turn must receive the rendered pixels for that round");
        recipes.insert(evidence.value("recipeSha256").toString());
        paths.insert(path);
        images.insert(expected_image);
      }
      const auto distinct_evidence = paths.size() == 6 && recipes.size() >= 5 && images.size() >= 5;
      if (!distinct_evidence)
        return fail("Changed live states must produce distinct images without overwriting old evidence or losing Revert");
      if (!agent_edit.rollbackDraft().value("ok").toBool())
        return fail("Fresh planning proof mutations must remain reversible");
      bridge.stop();
      std::cout << "Fresh live-recipe proofs reach each model round with immutable paths and matching recipe/PNG digests.\n";
      return EXIT_SUCCESS;
    }
    if (revision_contract) {
      if (!mcp_draft_completed || !mcp_draft_report.value("ok").toBool() || !agent_edit.draftActive())
        return fail("Create an owned initial draft before testing revision");
      const auto initial_request_id = mcp_draft_start.value("requestId").toString();
      const auto initial_run_id = mcp_draft_report.value("runId").toString();
      const auto initial_recipe = catalog_model.assetById("asset-1").value("adjustmentPatchJson").toString();
      const auto unrelated_recipe = catalog_model.assetById("asset-2").value("adjustmentPatchJson").toString();
      const auto initial_layers = chain_layers;
      const auto original_base_mode = agent_edit.recipeBaseMode();
      const auto response = [&](const QString& operation, const QJsonObject& arguments) {
        const QJsonObject payload{{"decision", "tool_call_plan"}, {"summary", "Revise existing layers"},
            {"toolCalls", QJsonArray{QJsonObject{{"name", "photo.edit_composition"},
                {"arguments", QJsonObject{{"assetId", "asset-1"}, {"operation", operation},
                    {"layerId", operation == "move" ? "chain-text" : ""}, {"values", arguments}}},
                {"reason", "Keep the original draft and edit its existing elements"}}}}};
        return QJsonObject{{"model", "qwen2.5vl:7b"}, {"done", true},
            {"message", QJsonObject{{"role", "assistant"}, {"content", QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))}}}};
      };
      QFile revision_requests(revision_request_path);
      if (!revision_requests.open(QIODevice::ReadOnly)) return fail("Initial draft model requests must be captured");
      const auto initial_model_calls = revision_requests.readAll().count('\n');
      if (initial_model_calls < 1) return fail("Initial draft must call the sequenced model transport");
      // The fake transport cursor is process-wide, including proof-critic calls.
      // Preserve its consumed prefix before appending the revision's responses.
      QJsonArray revision_sequence;
      for (auto index = 0; index < initial_model_calls; ++index)
        revision_sequence.append(fake_ollama_sequence.last());
      revision_sequence.append(response("copy_selection", {{"maskId", "chain-subject-mask"}, {"independent", true}}));
      revision_sequence.append(response("move", {{"toIndex", 0}}));
      revision_sequence.append(fake_ollama_sequence.last());
      qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON", QJsonDocument(revision_sequence).toJson(QJsonDocument::Compact));
      qputenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS", "40");
      catalog_model.clearSelection();
      catalog_model.selectIndex(1);
      catalog_model.toggleSelectionIndex(1);
      revision_phase = true;
      QVariantMap revision_report;
      int revision_completions = 0;
      const auto connection = QObject::connect(&agent_edit, &photo_editor::desktop::AgentEditController::draftRunFinished,
          &agent_edit, [&](const QString&, const QVariantMap& report) { revision_report = report; ++revision_completions; });
      QElapsedTimer start_timer;
      start_timer.start();
      const auto started = agent_edit.reviseDraft("Add another cutout from the existing selection and bring the existing headline to the front.");
      const auto start_ms = start_timer.elapsed();
      if (revision_conflict) {
        QObject::disconnect(connection);
        QFile requests_after_rejected_continue(revision_request_path);
        const auto no_revision_model_dispatch = requests_after_rejected_continue.open(QIODevice::ReadOnly) &&
            requests_after_rejected_continue.readAll().count('\n') == initial_model_calls;
        const auto original_task_state_retained = agent_edit.taskTransaction()
            .value(QStringLiteral("taskId")).toString() == task_request_id &&
            agent_edit.taskTransaction().value(QStringLiteral("status")).toString() == QStringLiteral("review") &&
            agent_edit.taskTransaction().value(QStringLiteral("revertAvailable")).toBool();
        if (started.value("ok").toBool() || started.value("accepted").toBool() || !started.value("taskStateConflict").toBool() ||
            !agent_edit.draftActive() || agent_edit.running() || revision_completions != 0 || chain_layers != initial_layers ||
            task_manual_recipe.isEmpty() || catalog_model.assetById("asset-1").value("adjustmentPatchJson") != task_manual_recipe ||
            task_actions != QStringList{"begin", "check_revert", "review", "continue"} || !no_revision_model_dispatch ||
            !original_task_state_retained)
          return fail("A mismatched successful continue receipt must be rejected before model dispatch while preserving manual edits and the original draft");
        qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON");
        qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS");
        qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH");
        bridge.stop();
        return EXIT_SUCCESS;
      }
      if (!started.value("accepted").toBool() || !started.value("queued").toBool() || start_ms > 50 ||
          started.value("requestId").toString() == initial_request_id || agent_edit.activeTaskId() != task_request_id ||
          catalog_model.assetById("asset-1").value("adjustmentPatchJson") != initial_recipe || chain_layers != initial_layers ||
          !agent_edit.draftActive() || agent_edit.commitDraft(true).value("errorCode") != "run_in_progress")
        return fail("Revision must queue a new request on the original task without rolling back, retargeting or allowing early Commit");
      if (revision_early_cancel) agent_edit.cancelRun("fixture cancel revision before work");
      const auto finished = wait_for_condition([&]() { return revision_completions == 1; }, 15000);
      QObject::disconnect(connection);
      qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON");
      qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS");
      qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH");
      const auto canceled = revision_cancel || revision_early_cancel;
      const auto succeeds = !canceled && !revision_failure;
      auto expected_actions = QStringList{"begin", "check_revert", "review", "continue"};
      expected_actions.append(canceled ? QStringList{"cancel", "check_revert", "revert"}
          : revision_failure ? QStringList{"interrupted"} : QStringList{"check_revert", "review"});
      if (!finished || revision_completions != 1 || agent_edit.running() || agent_edit.draftRequestPending() ||
          !agent_edit.activeDraftRequestId().isEmpty() || !agent_edit.activeTaskId().isEmpty() ||
          revision_report.value("ok").toBool() != succeeds || agent_edit.draftActive() != !canceled ||
          revision_report.value("revisedFromRunId") != initial_run_id || revision_report.value("taskId") != task_request_id ||
          task_actions != expected_actions || catalog_model.assetById("asset-2").value("adjustmentPatchJson") != unrelated_recipe) {
        std::cerr << QJsonDocument::fromVariant(QVariantMap{{"ok", revision_report.value("ok")},
            {"failureReason", revision_report.value("failureReason")}, {"actions", task_actions},
            {"operations", chain_operations}, {"draftActive", agent_edit.draftActive()}}).toJson().toStdString();
        return fail("Queued revision must finish once, preserve ownership and leave other photos untouched");
      }
      if (canceled && catalog_model.assetById("asset-1").value("adjustmentPatchJson") != task_before_recipe)
        return fail("Stop must restore the baseline before the initial draft, including cancellation before revision work");
      if (revision_failure && catalog_model.assetById("asset-1").value("adjustmentPatchJson") != initial_recipe)
        return fail("A rejected revision tool must retain the previous draft for review or whole-task Revert");
      if (succeeds) {
        for (const auto& original : initial_layers) {
          int count = 0;
          for (const auto& current : chain_layers)
            if (current.toMap().value("id") == original.toMap().value("id")) ++count;
          if (count != 1) return fail("Revision must preserve every existing layer ID exactly once");
        }
        if (chain_layers.size() != initial_layers.size() + 1 || revision_poll_count < 1 ||
            chain_operations.last() != "revision_move" || chain_layers.first().toMap().value("id") != "chain-text" ||
            revision_report.value("recipeBaseMode") != "current" ||
            agent_edit.recipeBaseMode() != original_base_mode ||
            revision_report.value("pendingProjectCorrection").toMap().value("revisedFromRunId") != initial_run_id ||
            agent_edit.lastRunReport().value("requestId") != started.value("requestId"))
          return fail("Revision must await pixels, use the current recipe and retain its provenance without changing user settings");
      }
      if (!canceled && (!agent_edit.rollbackDraft().value("ok").toBool() || agent_edit.draftActive() ||
          catalog_model.assetById("asset-1").value("adjustmentPatchJson") != task_before_recipe))
        return fail("Revert after successful or failed revision must undo the initial draft and revision together");
      bridge.stop();
      std::cout << "Queued revision preserves initial work, stable task identity, original targets and whole-task cancellation/Revert.\n";
      return EXIT_SUCCESS;
    }
    if (composition_repair_scenario) {
      const auto extract_prompt_section = [](const QString& prompt,
                                             const QString& label,
                                             const QString& next_label) {
        const auto section_start = prompt.indexOf(label);
        if (section_start < 0) return QString{};
        const auto value_start = section_start + label.size();
        auto value_end = prompt.indexOf(next_label, value_start);
        if (value_end < value_start) value_end = prompt.size();
        return prompt.mid(value_start, value_end - value_start).trimmed();
      };
      const auto read_prompt_evidence = [&]() {
        QVariantList evidence;
        QFile request_file(composition_repair_request_path);
        if (!request_file.open(QIODevice::ReadOnly)) return evidence;
        const auto request_lines = request_file.readAll().split('\n');
        for (const auto& line : request_lines) {
          const auto request = QJsonDocument::fromJson(line).object();
          const auto messages = request.value(QStringLiteral("messages")).toArray();
          if (messages.size() < 2) continue;
          const auto system_prompt = messages.first().toObject()
              .value(QStringLiteral("content")).toString();
          const auto user_prompt = messages.at(1).toObject()
              .value(QStringLiteral("content")).toString();
          const auto is_proof_critique = is_proof_critique_request(request);
          const auto is_tool_planner = system_prompt.contains(QStringLiteral("decision=tool_call_plan"));
          QVariantMap item{{QStringLiteral("userPrompt"), user_prompt},
              {QStringLiteral("imageCount"), messages.at(1).toObject()
                  .value(QStringLiteral("images")).toArray().size()}};
          if (is_proof_critique) {
            item.insert(QStringLiteral("kind"), QStringLiteral("proof-critique"));
            const auto validation_json = extract_prompt_section(
                user_prompt, QStringLiteral("Validation report JSON:\n"),
                QStringLiteral("Previous attempts JSON:"));
            item.insert(QStringLiteral("validationReport"),
                QJsonDocument::fromJson(validation_json.toUtf8()).object().toVariantMap());
          } else if (is_tool_planner) {
            item.insert(QStringLiteral("kind"), QStringLiteral("planner"));
            item.insert(QStringLiteral("adjustmentPatchSection"), extract_prompt_section(
                user_prompt, QStringLiteral("Current adjustment patch JSON:\n"),
                QStringLiteral("Style rulebook JSON:")));
          }
          evidence.append(item);
        }
        return evidence;
      };
      const auto report_json = QJsonDocument::fromVariant(mcp_draft_report).toJson(QJsonDocument::Compact);
      auto failed_event_claims_restore = false;
      for (const auto& event_value : mcp_draft_report.value(QStringLiteral("runEvents")).toList()) {
        const auto event = event_value.toMap();
        if (event.value(QStringLiteral("status")).toString() == QStringLiteral("failed") &&
            event.value(QStringLiteral("headline")).toString().contains(
                QStringLiteral("put everything back"), Qt::CaseInsensitive))
          failed_event_claims_restore = true;
      }
      const auto should_finish_successfully = composition_repair_contract || composition_repair_conflict_contract ||
          composition_batch_review_contract;
      const auto should_cancel = composition_repair_cancel_contract;
      const auto should_decline_repair = composition_repair_rejected_critique_contract;
      const auto discovery_context = mcp_draft_report.value("mcpDiscovery").toMap();
      const auto observation_context = mcp_draft_report.value("mcpObservation").toMap();
      const auto expected_operations = composition_batch_review_scenario
          ? QStringList{"add_text", "text"}
          : composition_repair_contract
          ? QStringList{"select_subject", "add_text", "typography", "copy_selection", "text"}
          : composition_repair_proof_failure_contract
          ? QStringList{"select_subject", "add_text", "typography", "copy_selection"}
          : should_decline_repair
              ? QStringList{"select_subject", "add_text", "typography", "copy_selection"}
          : QStringList{"select_subject", "add_text", "typography", "copy_selection", "text"};
      const auto expected_task_actions = should_cancel ? QStringList{"begin", "cancel", "revert"}
          : (composition_repair_proof_failure_contract || should_decline_repair ||
             composition_batch_invalid_terminal_contract)
              ? QStringList{"begin", "interrupted"}
          : QStringList{"begin", "check_revert", "review"};
      if (!mcp_draft_completed || mcp_draft_completion_count != 1 || agent_edit.running() ||
          agent_edit.draftRequestPending() || chain_operations != expected_operations ||
          composition_repair_add_text_calls != 1 ||
          composition_repair_text_calls !=
              ((composition_repair_proof_failure_contract || should_decline_repair) ? 0 : 1) ||
          task_actions != expected_task_actions || composition_repair_snapshot_failures != 0 ||
          !discovery_context.value("requestContextReused").toBool() ||
          !observation_context.value("requestContextReused").toBool() ||
          composition_repair_completed_observation_events != 1 ||
          (should_finish_successfully &&
              (!catalog_recipe_persistence_stages.contains(QStringLiteral("prepare")) ||
               !catalog_recipe_persistence_task_ids.contains(task_request_id))) ||
          (should_finish_successfully && (!mcp_draft_report.value("ok").toBool() || !agent_edit.draftActive())) ||
          (should_cancel && (mcp_draft_report.value("ok").toBool() || agent_edit.draftActive() ||
              !report_json.contains("fixture Stop during rendered composition repair"))) ||
          (composition_repair_proof_failure_contract && (mcp_draft_report.value("ok").toBool() ||
              agent_edit.draftActive() || !report_json.contains("composition_proof_unavailable") ||
              failed_event_claims_restore)) ||
          (should_decline_repair &&
              (mcp_draft_report.value("ok").toBool() || failed_event_claims_restore))) {
        QVariantList ollama_request_summary;
        // Preserve this synthetic private runtime only when the repair proof
        // fails, so the next run can be matched against the actual FIFO
        // request order without retaining image payloads in the ctest log.
        runtime_root.setAutoRemove(false);
        QFile request_file(composition_repair_request_path);
        if (request_file.open(QIODevice::ReadOnly)) {
          const auto lines = request_file.readAll().split('\n');
          for (qsizetype request_index = 0; request_index < lines.size(); ++request_index) {
            const auto request = QJsonDocument::fromJson(lines.at(request_index)).object();
            if (request.isEmpty()) continue;
            const auto messages = request.value(QStringLiteral("messages")).toArray();
            const auto system = messages.isEmpty() ? QString{} :
                messages.first().toObject().value(QStringLiteral("content")).toString();
            const auto user = messages.size() < 2 ? QJsonObject{} : messages.at(1).toObject();
            const auto user_text = user.value(QStringLiteral("content")).toString();
            const auto kind = is_proof_critique_request(request)
                ? QStringLiteral("proof-critique")
                : system.contains(QStringLiteral("decision=tool_call_plan"))
                    ? QStringLiteral("planner-with-tools") : QStringLiteral("planner");
            ollama_request_summary.append(QVariantMap{
                {QStringLiteral("fifoIndex"), request_index},
                {QStringLiteral("kind"), kind},
                {QStringLiteral("imageCount"), user.value(QStringLiteral("images")).toArray().size()},
                {QStringLiteral("userPromptPrefix"), user_text.left(180)},
            });
          }
        }
        const auto compact_prompt_audit = [](const QVariantMap& audit) {
          QVariantMap compact;
          const QStringList keys{
              QStringLiteral("backend"), QStringLiteral("model"),
              QStringLiteral("schemaMode"), QStringLiteral("metadataEvidenceComplete"),
              QStringLiteral("responseDone"), QStringLiteral("responseDoneReason"),
              QStringLiteral("ollamaResponseSource"), QStringLiteral("responseCharCount"),
              QStringLiteral("responseFieldNames"), QStringLiteral("responseTopLevelFields"),
              QStringLiteral("responseDecision"), QStringLiteral("requiredCurrentReadback"),
              QStringLiteral("errorCode"),
          };
          for (const auto& key : keys) {
            if (audit.contains(key)) compact.insert(key, audit.value(key));
          }
          const auto prompt_components = audit.value(QStringLiteral("promptComponents")).toMap();
          QVariantMap compact_components;
          const QStringList component_keys{
              QStringLiteral("systemPromptUtf8Bytes"),
              QStringLiteral("userPromptUtf8Bytes"),
              QStringLiteral("aggregateTextUtf8Bytes"),
              QStringLiteral("attachedImageCount"),
              QStringLiteral("primaryImageTransmittedBytes"),
              QStringLiteral("additionalProofImageCount"),
              QStringLiteral("additionalProofImageTransmittedBytes"),
              QStringLiteral("maxOutputTokens"),
              QStringLiteral("responseSchemaUtf8Bytes"),
              QStringLiteral("contextTokensRequested"),
              QStringLiteral("contextTokenLimit"),
              QStringLiteral("estimatedAggregateTextTokens"),
              QStringLiteral("estimatedAsciiUtf8Bytes"),
              QStringLiteral("estimatedNonAsciiUtf8Bytes"),
              QStringLiteral("estimatedNonAsciiCodePointCount"),
              QStringLiteral("estimatedContextTokensBeforeCap"),
          };
          for (const auto& key : component_keys) {
            if (prompt_components.contains(key))
              compact_components.insert(key, prompt_components.value(key));
          }
          if (!compact_components.isEmpty())
            compact.insert(QStringLiteral("promptComponents"), compact_components);
          return compact;
        };
        const auto bounded_text_values = [](const QVariant& value) {
          QVariantList values;
          if (value.metaType().id() == QMetaType::QStringList) {
            for (const auto& item : value.toStringList()) values.append(item.left(300));
          } else if (value.metaType().id() == QMetaType::QVariantList) {
            for (const auto& item : value.toList()) {
              if (values.size() == 6) break;
              values.append(item.toString().left(300));
            }
          } else if (value.metaType().id() == QMetaType::QString && !value.toString().isEmpty()) {
            values.append(value.toString().left(300));
          }
          while (values.size() > 6) values.removeLast();
          return values;
        };
        const auto compact_event_evidence = [](const QVariant& value) {
          const auto evidence = value.toMap();
          QVariantMap compact;
          const QStringList keys{
              QStringLiteral("stageOk"), QStringLiteral("error"),
              QStringLiteral("errorCode"), QStringLiteral("proofSha256"),
              QStringLiteral("sourceProofSha256"), QStringLiteral("recipeSha256"),
              QStringLiteral("sourceRecipeSha256"), QStringLiteral("assetId"),
              QStringLiteral("round"), QStringLiteral("requestId"),
              QStringLiteral("requiredCurrentReadback"),
              QStringLiteral("responseDecision"),
          };
          for (const auto& key : keys) {
            if (evidence.contains(key)) compact.insert(key, evidence.value(key));
          }
          return compact;
        };
        QVariantList asset_failure_details;
        for (const auto& report_value : mcp_draft_report.value("assetReports").toList()) {
          const auto report = report_value.toMap();
          asset_failure_details.append(QVariantMap{
              {QStringLiteral("assetId"), report.value(QStringLiteral("assetId"))},
              {QStringLiteral("status"), report.value(QStringLiteral("status"))},
              {QStringLiteral("failureReason"), report.value(QStringLiteral("failureReason")).toString().left(360)},
              {QStringLiteral("errors"), bounded_text_values(report.value(QStringLiteral("errors")))},
          });
        }
        QVariantList relevant_run_events;
        for (const auto& event_value : mcp_draft_report.value("runEvents").toList()) {
          const auto event = event_value.toMap();
          const auto stage = event.value(QStringLiteral("stage")).toString();
          const auto status = event.value(QStringLiteral("status")).toString();
          if (status != QStringLiteral("failed") && status != QStringLiteral("warning") &&
              !stage.contains(QStringLiteral("tool")) && !stage.contains(QStringLiteral("repair")) &&
              !stage.contains(QStringLiteral("plan"))) continue;
          relevant_run_events.append(QVariantMap{
              {QStringLiteral("stage"), stage},
              {QStringLiteral("label"), event.value(QStringLiteral("label")).toString().left(120)},
              {QStringLiteral("status"), status},
              {QStringLiteral("explanation"), event.value(QStringLiteral("explanation")).toString().left(240)},
              {QStringLiteral("evidence"), compact_event_evidence(event.value(QStringLiteral("evidence")))},
          });
        }
        QVariantList active_run_event_tail;
        const auto active_run_events = agent_edit.runEvents();
        const auto active_run_event_start = std::max<qsizetype>(0, active_run_events.size() - 12);
        for (auto index = active_run_event_start; index < active_run_events.size(); ++index) {
          const auto event = active_run_events.at(index).toMap();
          active_run_event_tail.append(QVariantMap{
              {QStringLiteral("stage"), event.value(QStringLiteral("stage"))},
              {QStringLiteral("label"), event.value(QStringLiteral("label")).toString().left(120)},
              {QStringLiteral("status"), event.value(QStringLiteral("status"))},
              {QStringLiteral("explanation"), event.value(QStringLiteral("explanation")).toString().left(240)},
              {QStringLiteral("evidence"), compact_event_evidence(event.value(QStringLiteral("evidence")))},
          });
        }
        const auto model_critique = mcp_draft_report.value(QStringLiteral("modelCritique")).toMap();
        const auto critique_prompt_audit = model_critique.value(QStringLiteral("promptAudit")).toMap();
        const auto model_prompt_audit = mcp_draft_report.value(QStringLiteral("modelPromptAudit")).toMap();
        std::cerr << "composition repair diagnostic=" << QJsonDocument::fromVariant(QVariantMap{
            {"ok", mcp_draft_report.value("ok")}, {"error", mcp_draft_report.value("error")},
            {"errorCode", mcp_draft_report.value("errorCode")}, {"status", mcp_draft_report.value("status")},
            {"modelId", mcp_draft_report.value("modelId")},
            {"modelPromptAudit", compact_prompt_audit(model_prompt_audit)},
            {"modelCritique", QVariantMap{
                {QStringLiteral("ok"), model_critique.value(QStringLiteral("ok"))},
                {QStringLiteral("error"), model_critique.value(QStringLiteral("error")).toString().left(360)},
                {QStringLiteral("summary"), model_critique.value(QStringLiteral("summary")).toString().left(240)},
                {QStringLiteral("promptAudit"), compact_prompt_audit(critique_prompt_audit)},
            }},
            {"mcpDiscovery", QVariantMap{
                {QStringLiteral("ready"), mcp_draft_report.value("mcpDiscovery").toMap().value("ready")},
                {QStringLiteral("requestContextReused"), mcp_draft_report.value("mcpDiscovery").toMap().value("requestContextReused")},
                {QStringLiteral("toolCount"), mcp_draft_report.value("mcpDiscovery").toMap().value("toolCount")},
                {QStringLiteral("error"), mcp_draft_report.value("mcpDiscovery").toMap().value("error")},
            }},
            {"mcpObservation", QVariantMap{
                {QStringLiteral("requestContextReused"), mcp_draft_report.value("mcpObservation").toMap().value("requestContextReused")},
                {QStringLiteral("requestContextRequestId"), mcp_draft_report.value("mcpObservation").toMap().value("requestContextRequestId")},
                {QStringLiteral("successCount"), mcp_draft_report.value("mcpObservation").toMap().value("successCount")},
            }},
            {"failureReason", mcp_draft_report.value("failureReason").toString().left(360)},
            {"errors", bounded_text_values(mcp_draft_report.value("errors"))}, {"assetReports", asset_failure_details},
            {"activeStage", mcp_draft_report.value("asyncStage").toString().left(180)},
            {"operations", chain_operations}, {"taskActions", task_actions},
            {"completedObservationCount", composition_repair_completed_observation_events},
            {"catalogPersistenceStages", catalog_recipe_persistence_stages},
            {"catalogPersistenceTaskIds", catalog_recipe_persistence_task_ids},
            {"expectedTaskId", task_request_id},
            {"nativeCalls", composition_repair_native_calls},
            {"textCalls", composition_repair_text_calls}, {"addTextCalls", composition_repair_add_text_calls},
            {"snapshotFailures", composition_repair_snapshot_failures},
            {"temporaryRuntimeRoot", runtime_root.path()},
            {"modelRequestPath", composition_repair_request_path},
            {"modelRequestsInFifoOrder", ollama_request_summary},
            {"draftActive", agent_edit.draftActive()}, {"completed", mcp_draft_completed},
            {"activeRunEventTail", active_run_event_tail},
            {"runEvents", relevant_run_events}})
            .toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("Rendered composition repair must preserve task ownership, stable layer identity and proof boundaries");
      }
      if (composition_batch_invalid_terminal_contract) {
        const auto prompt_evidence = read_prompt_evidence();
        QVariantMap first_batch_validation;
        for (const auto& item_value : prompt_evidence) {
          const auto item = item_value.toMap();
          if (item.value(QStringLiteral("kind")).toString() == QStringLiteral("proof-critique")) {
            first_batch_validation = item.value(QStringLiteral("validationReport")).toMap();
            break;
          }
        }
        const auto photographic_evidence = first_batch_validation
            .value(QStringLiteral("photographicQualityEvidence")).toMap();
        const auto invalid_terminal_plan = mcp_draft_report.value(QStringLiteral("modelToolPlan")).toMap();
        const auto invalid_terminal_baseline = invalid_terminal_plan
            .value(QStringLiteral("compositionPhotoBaseline")).toMap();
        const auto invalid_terminal_baseline_validation = invalid_terminal_baseline
            .value(QStringLiteral("validationReport")).toMap();
        const auto invalid_terminal_baseline_stats = invalid_terminal_baseline_validation
            .value(QStringLiteral("imageStats")).toMap();
        const auto baseline_limit = photographic_evidence
            .value(QStringLiteral("shadowClipLimitPercent")).toDouble();
        const auto baseline_clip = photographic_evidence
            .value(QStringLiteral("baselineShadowClipPercent")).toDouble();
        const auto baseline_warnings = photographic_evidence
            .value(QStringLiteral("baselineWarnings")).toStringList();
        const auto baseline_warning = std::any_of(
            baseline_warnings.cbegin(), baseline_warnings.cend(), [](const QString& warning) {
              return warning.contains(QStringLiteral("shadow clipping"), Qt::CaseInsensitive);
            });
        const auto genuine_base_shadow_clipping_preserved =
            photographic_evidence.value(QStringLiteral("contract")).toString() ==
                QStringLiteral("unchanged-pre-text-composition-photographic-baseline-v1") &&
            baseline_clip > baseline_limit && baseline_warning &&
            first_batch_validation.value(QStringLiteral("shadowClipPercent")).toDouble() == baseline_clip &&
            first_batch_validation.value(QStringLiteral("shadowClipLimitPercent")).toDouble() == baseline_limit &&
            invalid_terminal_baseline_validation.value(QStringLiteral("ok")).toBool() &&
            invalid_terminal_baseline_validation.value(QStringLiteral("errors")).toStringList().isEmpty() &&
            (invalid_terminal_baseline_validation.value(QStringLiteral("needsUserReview")).toBool() ||
             invalid_terminal_baseline_validation.value(QStringLiteral("qualityScoreReport")).toMap()
                 .value(QStringLiteral("needsUserReview")).toBool()) &&
            photographic_evidence.value(QStringLiteral("sourceRecipeFingerprintVerified")).toBool() &&
            photographic_evidence.value(QStringLiteral("preMutationInspectionReceiptSha256")).toString().size() == 64 &&
            photographic_evidence.value(QStringLiteral("photoRecipeUnchanged")).toBool() &&
            !photographic_evidence.value(QStringLiteral("explicitPhotoQualityScope")).toBool() &&
            photographic_evidence.value(QStringLiteral("compositePhotoMetricsChangedOnlyByApprovedText")).toBool();
        const auto prompt_audit = mcp_draft_report.value(QStringLiteral("modelPromptAudit")).toMap();
        const auto composition_source_path = catalog_model.assetById(QStringLiteral("asset-1"))
            .value(QStringLiteral("filePath")).toString();
        const auto baseline_proof_path = invalid_terminal_baseline
            .value(QStringLiteral("proofPath")).toString();
        const auto accepted_layers_remain = chain_layers.size() == 1 &&
            chain_layers.first().toMap().value(QStringLiteral("id")).toString() == QStringLiteral("chain-text") &&
            chain_layers.first().toMap().value(QStringLiteral("text")).toString() == QStringLiteral("APPROVED HEADLINE");
        if (!mcp_draft_completed || mcp_draft_completion_count != 1 ||
            mcp_draft_report.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive() ||
            mcp_draft_report.value(QStringLiteral("modelId")).toString() != QStringLiteral("ollama-qwen-tool-plan-test") ||
            !prompt_audit.value(QStringLiteral("fallbackBlocked")).toBool() ||
            prompt_audit.value(QStringLiteral("fallbackBlockedReason")).toString() !=
                QStringLiteral("confirmed-or-spatial-edit-requires-grounded-plan") ||
            prompt_audit.value(QStringLiteral("fallbackUsed")).toBool() ||
            composition_repair_add_text_calls != 1 || composition_repair_text_calls != 1 ||
            chain_operations != QStringList{QStringLiteral("add_text"), QStringLiteral("text")} ||
            !accepted_layers_remain || native_planning_recipe == task_before_recipe ||
            !genuine_base_shadow_clipping_preserved ||
            task_actions != QStringList{QStringLiteral("begin"), QStringLiteral("interrupted")}) {
          std::cerr << "invalid composition terminal diagnostic=" << QJsonDocument::fromVariant(QVariantMap{
              {QStringLiteral("ok"), mcp_draft_report.value(QStringLiteral("ok"))},
              {QStringLiteral("modelId"), mcp_draft_report.value(QStringLiteral("modelId"))},
              {QStringLiteral("errorCode"), mcp_draft_report.value(QStringLiteral("errorCode"))},
              {QStringLiteral("fallbackBlocked"), prompt_audit.value(QStringLiteral("fallbackBlocked"))},
              {QStringLiteral("fallbackUsed"), prompt_audit.value(QStringLiteral("fallbackUsed"))},
              {QStringLiteral("fallbackBlockedReason"), prompt_audit.value(QStringLiteral("fallbackBlockedReason"))},
              {QStringLiteral("baselineShadowClippingPreserved"), genuine_base_shadow_clipping_preserved},
              {QStringLiteral("baselineShadowClipPercent"), baseline_clip},
              {QStringLiteral("baselineShadowClipLimitPercent"), baseline_limit},
              {QStringLiteral("baselineWarnings"), baseline_warnings},
              {QStringLiteral("firstCritiqueValidation"), QVariantMap{
                   {QStringLiteral("ok"), first_batch_validation.value(QStringLiteral("ok"))},
                   {QStringLiteral("shadowClipPercent"), first_batch_validation.value(QStringLiteral("shadowClipPercent"))},
                   {QStringLiteral("shadowClipLimitPercent"), first_batch_validation.value(QStringLiteral("shadowClipLimitPercent"))},
                   {QStringLiteral("warnings"), first_batch_validation.value(QStringLiteral("warnings"))},
                   {QStringLiteral("photographicEvidencePresent"), !photographic_evidence.isEmpty()},
               }},
              {QStringLiteral("hostBaseline"), QVariantMap{
                   {QStringLiteral("present"), !invalid_terminal_baseline.isEmpty()},
                   {QStringLiteral("sourceRecipeFingerprintVerified"), invalid_terminal_baseline.value(QStringLiteral("sourceRecipeFingerprintVerified"))},
                   {QStringLiteral("sourceRecipeSha256"), invalid_terminal_baseline.value(QStringLiteral("sourceRecipeSha256"))},
                   {QStringLiteral("sourceFileSha256"), invalid_terminal_baseline.value(QStringLiteral("sourceFileSha256"))},
                   {QStringLiteral("proofSha256"), invalid_terminal_baseline.value(QStringLiteral("proofSha256"))},
                   {QStringLiteral("inspectionReceiptSha256Length"), invalid_terminal_baseline.value(QStringLiteral("preMutationInspectionReceiptSha256")).toString().size()},
                   {QStringLiteral("inspectionVerified"), invalid_terminal_baseline.value(QStringLiteral("preMutationInspectionVerified"))},
                   {QStringLiteral("rawValidationOk"), invalid_terminal_baseline_validation.value(QStringLiteral("ok"))},
                   {QStringLiteral("rawValidationErrors"), invalid_terminal_baseline_validation.value(QStringLiteral("errors"))},
                   {QStringLiteral("rawValidationNeedsUserReview"), invalid_terminal_baseline_validation.value(QStringLiteral("needsUserReview"))},
                   {QStringLiteral("rawQualityScoreNeedsUserReview"), invalid_terminal_baseline_validation.value(QStringLiteral("qualityScoreReport")).toMap().value(QStringLiteral("needsUserReview"))},
                   {QStringLiteral("rawValidationShadowClipPercent"), invalid_terminal_baseline_validation.value(QStringLiteral("shadowClipPercent"))},
                   {QStringLiteral("rawValidationShadowClipLimitPercent"), invalid_terminal_baseline_validation.value(QStringLiteral("shadowClipLimitPercent"))},
                   {QStringLiteral("rawValidationWarnings"), invalid_terminal_baseline_validation.value(QStringLiteral("warnings"))},
                   {QStringLiteral("statsSampleCount"), invalid_terminal_baseline_stats.value(QStringLiteral("sampleCount"))},
               }},
              {QStringLiteral("responseDecision"), prompt_audit.value(QStringLiteral("responseDecision"))},
              {QStringLiteral("responseFieldNames"), prompt_audit.value(QStringLiteral("responseFieldNames"))},
              {QStringLiteral("temporaryRuntimeRoot"), runtime_root.path()},
              {QStringLiteral("sourceImagePixels"), fixture_image_pixel_diagnostics(composition_source_path)},
              {QStringLiteral("baselineProofPixels"), fixture_image_pixel_diagnostics(baseline_proof_path)},
              {QStringLiteral("failureReason"), mcp_draft_report.value(QStringLiteral("failureReason"))},
              {QStringLiteral("operations"), chain_operations},
              {QStringLiteral("text"), chain_layers.isEmpty() ? QString{} : chain_layers.first().toMap().value(QStringLiteral("text"))},
              {QStringLiteral("taskActions"), task_actions},
              {QStringLiteral("draftActive"), agent_edit.draftActive()},
          }).toJson(QJsonDocument::Compact).toStdString() << '\n';
          return fail("A malformed terminal critique must not become a deterministic recipe fallback; accepted native edits stay available for Revert");
        }
        if (!agent_edit.revertTask().value(QStringLiteral("ok")).toBool() ||
            task_actions != QStringList{QStringLiteral("begin"), QStringLiteral("interrupted"), QStringLiteral("revert")} ||
            catalog_model.assetById(QStringLiteral("asset-1"))
                .value(QStringLiteral("adjustmentPatchJson")).toString() != task_before_recipe ||
            native_planning_recipe != task_before_recipe)
          return fail("The failed composition-only terminal plan must leave accepted native edits available for whole-task Revert");
        bridge.stop();
        std::cout << "Malformed composition completion failed closed; accepted native edits remained reversible.\n";
        return EXIT_SUCCESS;
      }
      if (composition_batch_review_contract) {
        const auto terminal_completion = mcp_draft_report.value(QStringLiteral("nativeCompositionCompletion")).toMap();
        const auto terminal_prompt_audit = mcp_draft_report.value(QStringLiteral("modelPromptAudit")).toMap();
        const auto terminal_asset_reports = mcp_draft_report.value(QStringLiteral("assetReports")).toList();
        const auto terminal_asset_report = terminal_asset_reports.isEmpty()
            ? QVariantMap{} : terminal_asset_reports.first().toMap();
        const auto terminal_validation = terminal_asset_report.value(QStringLiteral("validationReport")).toMap();
        const auto final_proof_critique = terminal_validation.value(QStringLiteral("modelProofCritique")).toMap();
        const auto final_proof_prompt_audit = final_proof_critique.value(QStringLiteral("promptAudit")).toMap();
        const auto public_photo_baseline = mcp_draft_report.value(QStringLiteral("modelToolPlan")).toMap()
            .value(QStringLiteral("compositionPhotoBaseline")).toMap();
        const auto expected_source_recipe_sha = QString::fromLatin1(QCryptographicHash::hash(
            task_before_recipe.toUtf8(), QCryptographicHash::Sha256).toHex());
        const auto controller_baseline_overrode_model_value =
            public_photo_baseline.value(QStringLiteral("sourceRecipeSha256")).toString() == expected_source_recipe_sha &&
            public_photo_baseline.value(QStringLiteral("sourceRecipeFingerprintVerified")).toBool() &&
            public_photo_baseline.value(QStringLiteral("preMutationInspectionReceiptSha256")).toString().size() == 64 &&
            !public_photo_baseline.contains(QStringLiteral("sourceRecipeJson")) &&
            !public_photo_baseline.contains(QStringLiteral("preMutationInspectionReceipt")) &&
            !public_photo_baseline.contains(QStringLiteral("revisionBinding"));
        const auto final_edit_loop = terminal_asset_report.value(QStringLiteral("editLoop")).toMap();
        const auto final_attempts = final_edit_loop.value(QStringLiteral("attempts")).toList();
        const auto final_attempt = final_attempts.isEmpty()
            ? QVariantMap{} : final_attempts.last().toMap();
        const auto final_attempt_validation = final_attempt.value(QStringLiteral("validationReport")).toMap();
        const auto final_photo_evidence = final_attempt_validation
            .value(QStringLiteral("photographicQualityEvidence")).toMap();
        const auto baseline_photo_quality = final_photo_evidence
            .value(QStringLiteral("baselineQualityScoreReport")).toMap();
        const auto baseline_raw_photo_quality = public_photo_baseline
            .value(QStringLiteral("validationReport")).toMap();
        const auto baseline_raw_photo_quality_score = baseline_raw_photo_quality
            .value(QStringLiteral("qualityScoreReport")).toMap();
        const auto baseline_raw_needs_review = baseline_raw_photo_quality
                .value(QStringLiteral("needsUserReview")).toBool() ||
            baseline_raw_photo_quality_score.value(QStringLiteral("needsUserReview")).toBool();
        const auto authored_text_clipping_scoped =
            final_photo_evidence.value(QStringLiteral("contract")).toString() ==
                QStringLiteral("unchanged-pre-text-composition-photographic-baseline-v1") &&
            !final_photo_evidence.value(QStringLiteral("explicitPhotoQualityScope")).toBool() &&
            final_photo_evidence.value(QStringLiteral("baselineHighlightClipPercent")).toDouble() <=
                final_photo_evidence.value(QStringLiteral("highlightClipLimitPercent")).toDouble() &&
            final_photo_evidence.value(QStringLiteral("compositeHighlightClipPercent")).toDouble() >
                final_photo_evidence.value(QStringLiteral("highlightClipLimitPercent")).toDouble() &&
            baseline_photo_quality.value(QStringLiteral("needsUserReview")).toBool() == baseline_raw_needs_review &&
            final_photo_evidence.value(QStringLiteral("baselineNeedsUserReview")).toBool() == baseline_raw_needs_review &&
            final_attempt_validation.value(QStringLiteral("compositeNeedsUserReview")).toBool() &&
            !final_attempt_validation.value(QStringLiteral("needsUserReview")).toBool() &&
            !final_attempt_validation.value(QStringLiteral("photographicReviewNeedsUserReview")).toBool();
        const auto projection_applied = final_attempt_validation
            .value(QStringLiteral("photographicReviewProjectionApplied")).toBool();
        const auto composition_source_path = catalog_model.assetById(QStringLiteral("asset-1"))
            .value(QStringLiteral("filePath")).toString();
        const auto baseline_proof_path = public_photo_baseline
            .value(QStringLiteral("proofPath")).toString();
        const auto final_proof_path = terminal_asset_report.value(QStringLiteral("currentProofPath")).toString().isEmpty()
            ? terminal_asset_report.value(QStringLiteral("afterProofPath")).toString()
            : terminal_asset_report.value(QStringLiteral("currentProofPath")).toString();
        const auto projected_review = projection_applied
            ? final_attempt_validation.value(QStringLiteral("photographicReviewNeedsUserReview")).toBool()
            : final_attempt_validation.value(QStringLiteral("needsUserReview")).toBool() ||
                final_attempt_validation.value(QStringLiteral("qualityScoreReport")).toMap()
                    .value(QStringLiteral("needsUserReview")).toBool();
        const auto expected_final_attempt_review =
            !final_attempt_validation.value(QStringLiteral("ok")).toBool() ||
            !final_attempt_validation.value(QStringLiteral("errors")).toStringList().isEmpty() ||
            (!final_attempt_validation.value(QStringLiteral("compositionReview")).toMap().isEmpty() &&
                (!final_attempt_validation.value(QStringLiteral("compositionReview")).toMap()
                     .value(QStringLiteral("ok")).toBool() ||
                 !final_attempt_validation.value(QStringLiteral("modelProofCritique")).toMap()
                     .value(QStringLiteral("ok")).toBool() ||
                 !final_attempt_validation.value(QStringLiteral("modelProofCritique")).toMap()
                     .value(QStringLiteral("pass")).toBool())) ||
            projected_review ||
            final_attempt_validation.value(QStringLiteral("styleMatch")).toMap()
                .value(QStringLiteral("needsReview")).toBool() ||
            final_attempt_validation.value(QStringLiteral("modelProofCritique")).toMap()
                .value(QStringLiteral("needsUserReview")).toBool();
        auto expected_asset_review = false;
        for (const auto& attempt_value : final_attempts)
          expected_asset_review = expected_asset_review || attempt_value.toMap()
              .value(QStringLiteral("needsUserReview")).toBool();
        const auto final_review_flags_coherent =
            projection_applied &&
            authored_text_clipping_scoped &&
            final_attempt.value(QStringLiteral("needsUserReview")).toBool() == expected_final_attempt_review &&
            terminal_asset_report.value(QStringLiteral("needsUserReview")).toBool() == expected_asset_review;
        if (!terminal_completion.value(QStringLiteral("requested")).toBool() ||
            !terminal_completion.value(QStringLiteral("controllerVerified")).toBool() ||
            terminal_completion.value(QStringLiteral("verificationContract")).toString() !=
                QStringLiteral("agent-native-composition-completion-controller-v1") ||
            terminal_completion.value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-1") ||
            terminal_completion.value(QStringLiteral("acceptedMutationCount")).toInt() <= 0 ||
            !terminal_completion.value(QStringLiteral("readbackConfirmed")).toBool() ||
            terminal_prompt_audit.value(QStringLiteral("modelDecision")).toString() != QStringLiteral("edit_intent") ||
            terminal_prompt_audit.value(QStringLiteral("responseDecision")).toString() != QStringLiteral("edit_intent") ||
            terminal_prompt_audit.value(QStringLiteral("completionMode")).toString() !=
                QStringLiteral("preserve-after-native-composition-readback") ||
            terminal_prompt_audit.value(QStringLiteral("fallbackUsed")).toBool() ||
            mcp_draft_report.value(QStringLiteral("modelId")).toString() != QStringLiteral("ollama-qwen-tool-plan-test") ||
            !controller_baseline_overrode_model_value || !final_review_flags_coherent ||
            !final_proof_critique.value(QStringLiteral("ok")).toBool() ||
            !final_proof_critique.value(QStringLiteral("pass")).toBool() ||
            final_proof_critique.value(QStringLiteral("modelId")).toString() != QStringLiteral("ollama-qwen-tool-plan-test") ||
            !final_proof_prompt_audit.value(QStringLiteral("metadataEvidenceComplete")).toBool()) {
          const auto baseline_quality = baseline_raw_photo_quality.value(QStringLiteral("qualityScoreReport")).toMap();
          const auto photographic_evidence_summary = QVariantMap{
              {QStringLiteral("contract"), final_photo_evidence.value(QStringLiteral("contract"))},
              {QStringLiteral("basis"), final_photo_evidence.value(QStringLiteral("basis"))},
              {QStringLiteral("assetId"), final_photo_evidence.value(QStringLiteral("assetId"))},
              {QStringLiteral("photoRecipeUnchanged"), final_photo_evidence.value(QStringLiteral("photoRecipeUnchanged"))},
              {QStringLiteral("explicitPhotoQualityScope"), final_photo_evidence.value(QStringLiteral("explicitPhotoQualityScope"))},
              {QStringLiteral("baselineHighlightClipPercent"), final_photo_evidence.value(QStringLiteral("baselineHighlightClipPercent"))},
              {QStringLiteral("compositeHighlightClipPercent"), final_photo_evidence.value(QStringLiteral("compositeHighlightClipPercent"))},
              {QStringLiteral("highlightClipLimitPercent"), final_photo_evidence.value(QStringLiteral("highlightClipLimitPercent"))},
              {QStringLiteral("baselineNeedsUserReview"), final_photo_evidence.value(QStringLiteral("baselineNeedsUserReview"))},
              {QStringLiteral("compositeNeedsUserReview"), final_attempt_validation.value(QStringLiteral("compositeNeedsUserReview"))},
              {QStringLiteral("photographicReviewProjectionApplied"), final_attempt_validation.value(QStringLiteral("photographicReviewProjectionApplied"))},
              {QStringLiteral("photographicReviewNeedsUserReview"), final_attempt_validation.value(QStringLiteral("photographicReviewNeedsUserReview"))},
              {QStringLiteral("photoRecipeFingerprintVerified"), final_photo_evidence.value(QStringLiteral("photoRecipeFingerprintVerified"))},
          };
          const auto completion_summary = QVariantMap{
              {QStringLiteral("requested"), terminal_completion.value(QStringLiteral("requested"))},
              {QStringLiteral("controllerVerified"), terminal_completion.value(QStringLiteral("controllerVerified"))},
              {QStringLiteral("verificationContract"), terminal_completion.value(QStringLiteral("verificationContract"))},
              {QStringLiteral("assetId"), terminal_completion.value(QStringLiteral("assetId"))},
              {QStringLiteral("acceptedMutationCount"), terminal_completion.value(QStringLiteral("acceptedMutationCount"))},
              {QStringLiteral("readbackConfirmed"), terminal_completion.value(QStringLiteral("readbackConfirmed"))},
          };
          const auto baseline_summary = QVariantMap{
              {QStringLiteral("sourceRecipeSha256"), public_photo_baseline.value(QStringLiteral("sourceRecipeSha256"))},
              {QStringLiteral("sourceRecipeFingerprintVerified"), public_photo_baseline.value(QStringLiteral("sourceRecipeFingerprintVerified"))},
              {QStringLiteral("preMutationInspectionReceiptSha256"), public_photo_baseline.value(QStringLiteral("preMutationInspectionReceiptSha256"))},
              {QStringLiteral("sourceRecipePresent"), public_photo_baseline.contains(QStringLiteral("sourceRecipeJson"))},
              {QStringLiteral("inspectionReceiptPresent"), public_photo_baseline.contains(QStringLiteral("preMutationInspectionReceipt"))},
              {QStringLiteral("revisionBindingPresent"), public_photo_baseline.contains(QStringLiteral("revisionBinding"))},
              {QStringLiteral("needsUserReview"), baseline_raw_photo_quality.value(QStringLiteral("needsUserReview"))},
              {QStringLiteral("qualityScoreNeedsUserReview"), baseline_quality.value(QStringLiteral("needsUserReview"))},
              {QStringLiteral("qualityScoreReport"), baseline_quality},
              {QStringLiteral("warnings"), baseline_raw_photo_quality.value(QStringLiteral("warnings"))},
          };
          const auto validation_summary = QVariantMap{
              {QStringLiteral("ok"), final_attempt_validation.value(QStringLiteral("ok"))},
              {QStringLiteral("needsUserReview"), final_attempt_validation.value(QStringLiteral("needsUserReview"))},
              {QStringLiteral("errors"), final_attempt_validation.value(QStringLiteral("errors"))},
              {QStringLiteral("warnings"), final_attempt_validation.value(QStringLiteral("warnings"))},
              {QStringLiteral("compositionReview"), QVariantMap{
                   {QStringLiteral("ok"), final_attempt_validation.value(QStringLiteral("compositionReview")).toMap().value(QStringLiteral("ok"))},
                   {QStringLiteral("pass"), final_attempt_validation.value(QStringLiteral("compositionReview")).toMap().value(QStringLiteral("pass"))},
                   {QStringLiteral("issues"), final_attempt_validation.value(QStringLiteral("compositionReview")).toMap().value(QStringLiteral("issues"))},
               }},
              {QStringLiteral("modelProofCritique"), QVariantMap{
                   {QStringLiteral("ok"), final_proof_critique.value(QStringLiteral("ok"))},
                   {QStringLiteral("pass"), final_proof_critique.value(QStringLiteral("pass"))},
                   {QStringLiteral("modelId"), final_proof_critique.value(QStringLiteral("modelId"))},
                   {QStringLiteral("promptMetadataEvidenceComplete"), final_proof_prompt_audit.value(QStringLiteral("metadataEvidenceComplete"))},
               }},
          };
          const auto prompt_audit_summary = QVariantMap{
              {QStringLiteral("modelDecision"), terminal_prompt_audit.value(QStringLiteral("modelDecision"))},
              {QStringLiteral("responseDecision"), terminal_prompt_audit.value(QStringLiteral("responseDecision"))},
              {QStringLiteral("completionMode"), terminal_prompt_audit.value(QStringLiteral("completionMode"))},
              {QStringLiteral("fallbackUsed"), terminal_prompt_audit.value(QStringLiteral("fallbackUsed"))},
              {QStringLiteral("responseFieldNames"), terminal_prompt_audit.value(QStringLiteral("responseFieldNames"))},
          };
          std::cerr << "composition batch terminal diagnostic=" << QJsonDocument::fromVariant(QVariantMap{
              {QStringLiteral("report"), QVariantMap{
                   {QStringLiteral("ok"), mcp_draft_report.value(QStringLiteral("ok"))},
                   {QStringLiteral("modelId"), mcp_draft_report.value(QStringLiteral("modelId"))},
                   {QStringLiteral("errorCode"), mcp_draft_report.value(QStringLiteral("errorCode"))},
                   {QStringLiteral("failureReason"), mcp_draft_report.value(QStringLiteral("failureReason"))},
               }},
              {QStringLiteral("completion"), completion_summary},
              {QStringLiteral("promptAudit"), prompt_audit_summary},
              {QStringLiteral("baselineOverridesModelValue"), controller_baseline_overrode_model_value},
              {QStringLiteral("baseline"), baseline_summary},
              {QStringLiteral("validation"), validation_summary},
              {QStringLiteral("photographicEvidence"), photographic_evidence_summary},
              {QStringLiteral("temporaryRuntimeRoot"), runtime_root.path()},
              {QStringLiteral("sourceImagePixels"), fixture_image_pixel_diagnostics(composition_source_path)},
              {QStringLiteral("baselineProofPixels"), fixture_image_pixel_diagnostics(baseline_proof_path)},
              {QStringLiteral("finalProofPixels"), fixture_image_pixel_diagnostics(final_proof_path)},
              {QStringLiteral("reviewCoherence"), QVariantMap{
                   {QStringLiteral("projectionApplied"), projection_applied},
                   {QStringLiteral("authoredTextClippingScoped"), authored_text_clipping_scoped},
                   {QStringLiteral("attemptReview"), final_attempt.value(QStringLiteral("needsUserReview"))},
                   {QStringLiteral("expectedAttemptReview"), expected_final_attempt_review},
                   {QStringLiteral("assetReview"), terminal_asset_report.value(QStringLiteral("needsUserReview"))},
                   {QStringLiteral("expectedAssetReview"), expected_asset_review},
                   {QStringLiteral("coherent"), final_review_flags_coherent},
               }},
          }).toJson(QJsonDocument::Compact).toStdString() << '\n';
          return fail("Batch review must end with a verified selected-model completion and passing selected-model proof critique, backed by fresh native readback");
        }
        const auto model_tool_plan = mcp_draft_report.value(QStringLiteral("modelToolPlan")).toMap();
        const auto rounds = model_tool_plan.value(QStringLiteral("rounds")).toList();
        const auto prompt_evidence = read_prompt_evidence();
        QVariantList critic_validation_reports;
        QStringList planner_adjustment_sections;
        for (const auto& item_value : prompt_evidence) {
          const auto item = item_value.toMap();
          if (item.value(QStringLiteral("kind")).toString() == QStringLiteral("proof-critique"))
            critic_validation_reports.append(item.value(QStringLiteral("validationReport")));
          else if (item.value(QStringLiteral("kind")).toString() == QStringLiteral("planner"))
            planner_adjustment_sections.append(item.value(QStringLiteral("adjustmentPatchSection")).toString());
        }
        const auto empty_patch_prompt = [](const QString& section) {
          const auto value = section.trimmed();
          return value.isEmpty() || value == QStringLiteral("null") || value == QStringLiteral("{}");
        };
        const auto all_planner_patch_sections_empty = planner_adjustment_sections.size() >= 2 &&
            std::all_of(planner_adjustment_sections.cbegin(), planner_adjustment_sections.cend(), empty_patch_prompt);
        const auto text_only_photo_metrics_bound = [](
            const QVariantMap& validation, bool expected_introduced_clipping) {
          const auto evidence = validation.value(QStringLiteral("photographicQualityEvidence")).toMap();
          const auto baseline = evidence.value(QStringLiteral("baselineImageStats")).toMap();
          const auto composite = evidence.value(QStringLiteral("currentCompositeImageStats")).toMap();
          const auto baseline_clip = evidence.value(QStringLiteral("baselineHighlightClipPercent")).toDouble();
          const auto composite_clip = evidence.value(QStringLiteral("compositeHighlightClipPercent")).toDouble();
          const auto limit = evidence.value(QStringLiteral("highlightClipLimitPercent")).toDouble();
          const auto composite_warnings = evidence.value(QStringLiteral("currentCompositeWarnings")).toStringList();
          const auto introduced_warnings = evidence.value(QStringLiteral("introducedCompositeWarnings")).toStringList();
          const auto composite_has_clipping_warning = std::any_of(
              composite_warnings.cbegin(), composite_warnings.cend(), [](const QString& warning) {
                return warning.contains(QStringLiteral("highlight clipping"), Qt::CaseInsensitive);
              });
          const auto introduced_clipping_warning = std::any_of(
              introduced_warnings.cbegin(), introduced_warnings.cend(), [](const QString& warning) {
                return warning.contains(QStringLiteral("highlight clipping"), Qt::CaseInsensitive);
              });
          return evidence.value(QStringLiteral("contract")).toString() ==
                  QStringLiteral("unchanged-pre-text-composition-photographic-baseline-v1") &&
              evidence.value(QStringLiteral("sourceRecipeFingerprintVerified")).toBool() &&
              evidence.value(QStringLiteral("preMutationInspectionReceiptSha256")).toString().size() == 64 &&
              evidence.value(QStringLiteral("photoRecipeUnchanged")).toBool() &&
              !evidence.value(QStringLiteral("explicitPhotoQualityScope")).toBool() &&
              evidence.value(QStringLiteral("compositePhotoMetricsChangedOnlyByApprovedText")).toBool() &&
              evidence.value(QStringLiteral("assetId")).toString() == QStringLiteral("asset-1") &&
              baseline.value(QStringLiteral("sampleCount")).toInt() > 0 &&
              composite.value(QStringLiteral("sampleCount")).toInt() > 0 &&
              baseline_clip <= limit && composite_clip > baseline_clip &&
              (expected_introduced_clipping
                  ? composite_clip > limit && composite_has_clipping_warning && introduced_clipping_warning
                  : composite_clip <= limit && !composite_has_clipping_warning && !introduced_clipping_warning) &&
              validation.value(QStringLiteral("highlightClipPercent")).toDouble() == baseline_clip;
        };
        const auto first_batch_photo_projection = critic_validation_reports.size() >= 1 &&
            text_only_photo_metrics_bound(critic_validation_reports.at(0).toMap(), false);
        const auto corrected_batch_photo_projection = critic_validation_reports.size() >= 2 &&
            text_only_photo_metrics_bound(critic_validation_reports.at(1).toMap(), true);
        const auto final_photo_projection = critic_validation_reports.size() >= 3 &&
            text_only_photo_metrics_bound(critic_validation_reports.at(2).toMap(), true);
        QVariantList compact_photo_evidence;
        for (const auto& report_value : critic_validation_reports) {
          const auto report = report_value.toMap();
          const auto evidence = report.value(QStringLiteral("photographicQualityEvidence")).toMap();
          compact_photo_evidence.append(QVariantMap{
              {QStringLiteral("contract"), evidence.value(QStringLiteral("contract"))},
              {QStringLiteral("assetId"), evidence.value(QStringLiteral("assetId"))},
              {QStringLiteral("sourceRecipeFingerprintVerified"), evidence.value(QStringLiteral("sourceRecipeFingerprintVerified"))},
              {QStringLiteral("inspectionReceiptSha256Length"), evidence.value(QStringLiteral("preMutationInspectionReceiptSha256")).toString().size()},
              {QStringLiteral("baselineClipPercent"), evidence.value(QStringLiteral("baselineHighlightClipPercent"))},
              {QStringLiteral("compositeClipPercent"), evidence.value(QStringLiteral("compositeHighlightClipPercent"))},
              {QStringLiteral("clipLimitPercent"), evidence.value(QStringLiteral("highlightClipLimitPercent"))},
              {QStringLiteral("photoRecipeUnchanged"), evidence.value(QStringLiteral("photoRecipeUnchanged"))},
              {QStringLiteral("explicitPhotoQualityScope"), evidence.value(QStringLiteral("explicitPhotoQualityScope"))},
              {QStringLiteral("baselineWarningCount"), evidence.value(QStringLiteral("baselineWarnings")).toStringList().size()},
              {QStringLiteral("compositeWarningCount"), evidence.value(QStringLiteral("currentCompositeWarnings")).toStringList().size()},
              {QStringLiteral("introducedWarnings"), evidence.value(QStringLiteral("introducedCompositeWarnings"))},
          });
        }
        bool first_mutation_review_is_actionable = false;
        bool inspect_only_round_retained_review = false;
        bool corrected_review_passes = false;
        QVariantList compact_assessments;
        for (const auto& round_value : rounds) {
          const auto round = round_value.toMap();
          const auto assessment = round.value(QStringLiteral("compositionAssessment")).toMap();
          if (assessment.isEmpty()) continue;
          const auto native_review = assessment.value(QStringLiteral("nativeReview")).toMap();
          const auto critique = assessment.value(QStringLiteral("modelCritique")).toMap();
          const auto missing_copy = native_review.value(QStringLiteral("missingExpectedCopy")).toList();
          if (round.value(QStringLiteral("round")).toInt() == 2) {
            first_mutation_review_is_actionable =
                assessment.value(QStringLiteral("textLayerIds")).toStringList().contains(QStringLiteral("chain-text")) &&
                assessment.value(QStringLiteral("requireProductivePlan")).toBool() &&
                !native_review.value(QStringLiteral("ok")).toBool() &&
                missing_copy.contains(QVariant(QStringLiteral("APPROVED HEADLINE"))) &&
                !critique.value(QStringLiteral("pass")).toBool() &&
                !assessment.value(QStringLiteral("geometryChecks")).toList().isEmpty();
          } else if (round.value(QStringLiteral("round")).toInt() == 3) {
            const auto visual_evidence = round.value(QStringLiteral("visualEvidence")).toMap();
            const auto inspection = round.value(QStringLiteral("compositionInspectionReceipt")).toMap();
            const auto inspection_content = inspection.value(QStringLiteral("structuredContent")).toMap();
            inspect_only_round_retained_review =
                round.value(QStringLiteral("compositionAssessmentRetained")).toBool() &&
                assessment.value(QStringLiteral("batchRound")).toInt() == 2 &&
                assessment.value(QStringLiteral("sourceProofSha256")).toString() ==
                    visual_evidence.value(QStringLiteral("proofSha256")).toString() &&
                assessment.value(QStringLiteral("sourceRecipeSha256")).toString() ==
                    visual_evidence.value(QStringLiteral("sourceRecipeSha256")).toString() &&
                inspection.value(QStringLiteral("toolName")).toString() == QStringLiteral("photo.inspect_composition") &&
                inspection.value(QStringLiteral("ok")).toBool() &&
                inspection_content.value(QStringLiteral("assetId")).toString() == QStringLiteral("asset-1") &&
                assessment.value(QStringLiteral("requireProductivePlan")).toBool() &&
                assessment.value(QStringLiteral("textLayerIds")).toStringList().contains(QStringLiteral("chain-text"));
          } else if (round.value(QStringLiteral("round")).toInt() == 4) {
            corrected_review_passes =
                assessment.value(QStringLiteral("textLayerIds")).toStringList().contains(QStringLiteral("chain-text")) &&
                native_review.value(QStringLiteral("ok")).toBool() && missing_copy.isEmpty() &&
                critique.value(QStringLiteral("pass")).toBool() &&
                !assessment.value(QStringLiteral("requireProductivePlan")).toBool();
          }
          compact_assessments.append(QVariantMap{
              {QStringLiteral("round"), round.value(QStringLiteral("round"))},
              {QStringLiteral("requireProductivePlan"), assessment.value(QStringLiteral("requireProductivePlan"))},
              {QStringLiteral("nativeOk"), native_review.value(QStringLiteral("ok"))},
              {QStringLiteral("missingExpectedCopy"), missing_copy},
              {QStringLiteral("critiquePass"), critique.value(QStringLiteral("pass"))},
              {QStringLiteral("textLayerIds"), assessment.value(QStringLiteral("textLayerIds"))},
          });
        }

        int first_batch_critique_index = -1;
        int next_planner_index = -1;
        int retained_assessment_planner_index = -1;
        int corrected_batch_critique_index = -1;
        int proof_critique_request_count = 0;
        QString next_planner_prompt;
        QString retained_assessment_planner_prompt;
        QFile request_file(composition_repair_request_path);
        if (request_file.open(QIODevice::ReadOnly)) {
          const auto request_lines = request_file.readAll().split('\n');
          for (qsizetype index = 0; index < request_lines.size(); ++index) {
            const auto request = QJsonDocument::fromJson(request_lines.at(index)).object();
            const auto messages = request.value(QStringLiteral("messages")).toArray();
            if (messages.size() < 2) continue;
            const auto system_prompt = messages.first().toObject()
                .value(QStringLiteral("content")).toString();
            const auto user_prompt = messages.at(1).toObject()
                .value(QStringLiteral("content")).toString();
            const auto is_proof_critique = is_proof_critique_request(request);
            if (is_proof_critique) {
              ++proof_critique_request_count;
              if (first_batch_critique_index < 0)
                first_batch_critique_index = static_cast<int>(index);
              else if (next_planner_index >= 0 && corrected_batch_critique_index < 0)
                corrected_batch_critique_index = static_cast<int>(index);
              continue;
            }
            const auto has_assessment = user_prompt.contains(QStringLiteral("current-composition-assessment")) &&
                user_prompt.contains(QStringLiteral("agent-current-composition-assessment-v1")) &&
                user_prompt.contains(QStringLiteral("requireProductivePlan")) &&
                user_prompt.contains(QStringLiteral("APPROVED HEADLINE")) &&
                user_prompt.contains(QStringLiteral("chain-text"));
            if (next_planner_index < 0 && has_assessment && system_prompt.contains(QStringLiteral("decision=tool_call_plan"))) {
              next_planner_index = static_cast<int>(index);
              next_planner_prompt = user_prompt;
            } else if (next_planner_index >= 0 && retained_assessment_planner_index < 0 && has_assessment &&
                       system_prompt.contains(QStringLiteral("decision=tool_call_plan"))) {
              retained_assessment_planner_index = static_cast<int>(index);
              retained_assessment_planner_prompt = user_prompt;
            }
          }
        }
        const auto assessment_reached_next_planner = first_batch_critique_index >= 0 &&
            next_planner_index > first_batch_critique_index &&
            corrected_batch_critique_index > next_planner_index &&
            (next_planner_prompt.contains(QStringLiteral("never replay"), Qt::CaseInsensitive) ||
             next_planner_prompt.contains(QStringLiteral("never repeat"), Qt::CaseInsensitive)) &&
            next_planner_prompt.contains(QStringLiteral("photo.inspect_composition"));
        const auto assessment_survived_inspect_only_turn = inspect_only_round_retained_review &&
            retained_assessment_planner_index > next_planner_index &&
            retained_assessment_planner_index < corrected_batch_critique_index &&
            retained_assessment_planner_prompt.contains(QStringLiteral("sourceProofSha256")) &&
            retained_assessment_planner_prompt.contains(QStringLiteral("APPROVED HEADLINE")) &&
            proof_critique_request_count == 3;
        const auto review_flow_passed = first_mutation_review_is_actionable &&
            inspect_only_round_retained_review && corrected_review_passes && assessment_reached_next_planner &&
            assessment_survived_inspect_only_turn && all_planner_patch_sections_empty &&
            first_batch_photo_projection && corrected_batch_photo_projection && final_photo_projection;
        if (!mcp_draft_report.value(QStringLiteral("ok")).toBool() ||
            !agent_edit.draftActive() || !review_flow_passed ||
            chain_operations != QStringList{QStringLiteral("add_text"), QStringLiteral("text")} ||
            composition_repair_add_text_calls != 1 || composition_repair_text_calls != 1 ||
            chain_layers.size() != 1 ||
            chain_layers.first().toMap().value(QStringLiteral("id")).toString() != QStringLiteral("chain-text") ||
            chain_layers.first().toMap().value(QStringLiteral("text")).toString() != QStringLiteral("APPROVED HEADLINE")) {
          std::cerr << "composition batch review diagnostic=" << QJsonDocument::fromVariant(QVariantMap{
              {QStringLiteral("ok"), mcp_draft_report.value(QStringLiteral("ok"))},
              {QStringLiteral("draftActive"), agent_edit.draftActive()},
              {QStringLiteral("reviewFlowPassed"), review_flow_passed},
              {QStringLiteral("firstMutationReviewIsActionable"), first_mutation_review_is_actionable},
              {QStringLiteral("inspectOnlyRoundRetainedReview"), inspect_only_round_retained_review},
              {QStringLiteral("correctedReviewPasses"), corrected_review_passes},
              {QStringLiteral("assessmentReachedNextPlanner"), assessment_reached_next_planner},
              {QStringLiteral("assessmentSurvivedInspectOnlyTurn"), assessment_survived_inspect_only_turn},
              {QStringLiteral("proofCritiqueRequestCount"), proof_critique_request_count},
              {QStringLiteral("allPlannerPatchSectionsEmpty"), all_planner_patch_sections_empty},
              {QStringLiteral("plannerPatchSections"), planner_adjustment_sections},
              {QStringLiteral("firstBatchPhotoProjection"), first_batch_photo_projection},
              {QStringLiteral("correctedBatchPhotoProjection"), corrected_batch_photo_projection},
              {QStringLiteral("finalPhotoProjection"), final_photo_projection},
              {QStringLiteral("photographicEvidence"), compact_photo_evidence},
              {QStringLiteral("requestIndices"), QVariantList{first_batch_critique_index, next_planner_index,
                  retained_assessment_planner_index, corrected_batch_critique_index}},
              {QStringLiteral("assessments"), compact_assessments},
              {QStringLiteral("operations"), chain_operations},
              {QStringLiteral("addTextCalls"), composition_repair_add_text_calls},
              {QStringLiteral("textCalls"), composition_repair_text_calls},
              {QStringLiteral("layerCount"), chain_layers.size()},
              {QStringLiteral("failureReason"), mcp_draft_report.value(QStringLiteral("failureReason"))},
          }).toJson(QJsonDocument::Compact).toStdString() << '\n';
          return fail("Each successful text batch must reach a fresh native/rendered assessment before the next planner turn, and correction must reuse the accepted text layer");
        }
        const auto validation_for_report = [](const QVariantMap& report) {
          const auto assets = report.value(QStringLiteral("assetReports")).toList();
          if (assets.isEmpty()) return QVariantMap{};
          const auto attempts = assets.first().toMap().value(QStringLiteral("editLoop")).toMap()
              .value(QStringLiteral("attempts")).toList();
          if (attempts.isEmpty()) return QVariantMap{};
          return attempts.last().toMap().value(QStringLiteral("validationReport")).toMap();
        };
        const auto initial_final_validation = validation_for_report(mcp_draft_report);
        const auto initial_final_photo_evidence = initial_final_validation
            .value(QStringLiteral("photographicQualityEvidence")).toMap();
        const auto baseline_source_recipe_sha = public_photo_baseline
            .value(QStringLiteral("sourceRecipeSha256")).toString();
        const auto baseline_proof_sha = public_photo_baseline
            .value(QStringLiteral("proofSha256")).toString();
        const auto baseline_inspection_sha = public_photo_baseline
            .value(QStringLiteral("preMutationInspectionReceiptSha256")).toString();
        const auto baseline_mcp_run_id = public_photo_baseline
            .value(QStringLiteral("runId")).toString();
        const auto initial_request_id = mcp_draft_start.value(QStringLiteral("requestId")).toString();
        const auto baseline_photo_fingerprint = initial_final_photo_evidence
            .value(QStringLiteral("normalizedPhotoCompositionSha256")).toString();
        auto previous_accepted_run_id = mcp_draft_report.value(QStringLiteral("runId")).toString();
        if (previous_accepted_run_id.isEmpty() || baseline_source_recipe_sha.size() != 64 ||
            baseline_proof_sha.size() != 64 || baseline_inspection_sha.size() != 64 ||
            baseline_photo_fingerprint.size() != 64 || baseline_mcp_run_id.isEmpty() ||
            initial_request_id.isEmpty()) {
          return fail("The accepted initial composition must retain its immutable source proof and raw inspect receipt fingerprints");
        }
        const auto revision_response_for = [](const QJsonObject& content) {
          return QJsonObject{{QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
              {QStringLiteral("done"), true},
              {QStringLiteral("message"), QJsonObject{
                  {QStringLiteral("role"), QStringLiteral("assistant")},
                  {QStringLiteral("content"), QString::fromUtf8(
                      QJsonDocument(content).toJson(QJsonDocument::Compact))}}}};
        };
        const auto revision_tool_response = [&](const QString& summary, const QJsonObject& call) {
          return revision_response_for(QJsonObject{
              {QStringLiteral("decision"), QStringLiteral("tool_call_plan")},
              {QStringLiteral("summary"), summary},
              {QStringLiteral("toolCalls"), QJsonArray{call}},
          });
        };
        const auto revision_tool_call = [](const QString& name, const QJsonObject& arguments,
                                           const QString& reason) {
          return QJsonObject{
              {QStringLiteral("name"), name},
              {QStringLiteral("arguments"), arguments},
              {QStringLiteral("reason"), reason},
          };
        };
        const auto passing_revision_critique = revision_response_for(QJsonObject{
            {QStringLiteral("pass"), true},
            {QStringLiteral("needsUserReview"), false},
            {QStringLiteral("summary"), QStringLiteral(
                "The existing headline remains exact and legible; the photograph and composition are preserved.")},
            {QStringLiteral("tasteConfidence"), QStringLiteral("high")},
            {QStringLiteral("issues"), QJsonArray{}},
            {QStringLiteral("repairHints"), QJsonArray{}},
            {QStringLiteral("warnings"), QJsonArray{}},
        });
        const auto revision_completion_intent = revision_response_for(QJsonObject{
            {QStringLiteral("decision"), QStringLiteral("edit_intent")},
            {QStringLiteral("compositionPhotoBaseline"), QJsonObject{
                {QStringLiteral("preMutationInspectionVerified"), true},
                {QStringLiteral("sourceRecipeJson"), QStringLiteral("forged-revision-baseline")},
                {QStringLiteral("preMutationInspectionReceiptSha256"), QString(64, QLatin1Char('0'))},
            }},
            {QStringLiteral("summary"), QStringLiteral(
                "The existing exact headline is slightly larger and has a fresh native readback.")},
            {QStringLiteral("styleConstraintCitations"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("field"), QStringLiteral("allowedTools.graphics")},
                    {QStringLiteral("reason"), QStringLiteral("The approved exact headline remains unchanged.")}},
                QJsonObject{
                    {QStringLiteral("field"), QStringLiteral("allowedTools.layers_luts")},
                    {QStringLiteral("reason"), QStringLiteral("The existing text layer was resized in place.")}},
            }},
            {QStringLiteral("editIntent"), QJsonObject{
                {QStringLiteral("goalSummary"), QStringLiteral(
                    "Slightly enlarge the existing headline while preserving its exact copy and photograph.")},
                {QStringLiteral("colorMode"), QStringLiteral("preserve")},
                {QStringLiteral("contrast"), QStringLiteral("natural")},
                {QStringLiteral("brightness"), QStringLiteral("preserve")},
                {QStringLiteral("palette"), QStringLiteral("preserve")},
                {QStringLiteral("saturation"), QStringLiteral("preserve")},
                {QStringLiteral("crop"), QStringLiteral("preserve")},
                {QStringLiteral("detail"), QStringLiteral("natural")},
                {QStringLiteral("mood"), QStringLiteral("natural")},
                {QStringLiteral("referenceStrength"), 0.0},
                {QStringLiteral("globalChangePolicy"), QStringLiteral("preserve")},
                {QStringLiteral("completionStatus"), QStringLiteral("complete")},
                {QStringLiteral("confidence"), 0.95},
                {QStringLiteral("visualFindings"), QJsonArray{}},
                {QStringLiteral("localizedActions"), QJsonArray{}},
                {QStringLiteral("requestedActions"), QJsonArray{}},
                {QStringLiteral("requiresApproval"), QJsonArray{}},
                {QStringLiteral("retouchActions"), QJsonArray{}},
            }},
        });
        for (int revision_index = 0; revision_index < 2; ++revision_index) {
          const auto expected_size = 112 + revision_index * 12;
          const auto typography_size_count_before = revision_typography_sizes.size();
          const QJsonObject inspect_call = revision_tool_call(
              QStringLiteral("photo.inspect_composition"),
              QJsonObject{{QStringLiteral("assetId"), QStringLiteral("asset-1")}},
              QStringLiteral("Read the accepted native headline and photo before this revision."));
          const auto inspect_response = revision_tool_response(
              QStringLiteral("Inspect the current headline before changing its size."), inspect_call);
          const QJsonObject typography_call = revision_tool_call(
              QStringLiteral("photo.edit_composition"),
              QJsonObject{
                  {QStringLiteral("assetId"), QStringLiteral("asset-1")},
                  {QStringLiteral("operation"), QStringLiteral("typography")},
                  {QStringLiteral("layerId"), QStringLiteral("chain-text")},
                  {QStringLiteral("values"), QJsonObject{{QStringLiteral("size"), expected_size}}},
              },
              QStringLiteral("Slightly enlarge the existing headline without changing its wording."));
          const auto typography_response = revision_tool_response(
              QStringLiteral("Increase the existing headline size in place."), typography_call);
          const auto repeat_typography_response = revision_tool_response(
              QStringLiteral("Retry the exact typography setter after its native readback."), typography_call);
          const auto intermediate_typography_call = revision_tool_call(
              QStringLiteral("photo.edit_composition"),
              QJsonObject{
                  {QStringLiteral("assetId"), QStringLiteral("asset-1")},
                  {QStringLiteral("operation"), QStringLiteral("typography")},
                  {QStringLiteral("layerId"), QStringLiteral("chain-text")},
                  {QStringLiteral("values"), QJsonObject{{QStringLiteral("size"), expected_size - 12}}},
              },
              QStringLiteral("Temporarily change the existing headline size before restoring the requested size."));
          const auto intermediate_typography_response = revision_tool_response(
              QStringLiteral("Apply the different typography value to the same layer."), intermediate_typography_call);
          const auto final_readback_response = revision_tool_response(
              QStringLiteral("Read back the finished typography revision."),
              revision_tool_call(
                  QStringLiteral("photo.inspect_composition"),
                  QJsonObject{{QStringLiteral("assetId"), QStringLiteral("asset-1")}},
                  QStringLiteral("Confirm the accepted headline size and exact copy in the native recipe.")));
          QJsonArray revision_sequence{
              inspect_response,
              typography_response,
              passing_revision_critique,
          };
          const auto omit_final_model_readback = composition_batch_review_scenario && revision_index == 0;
          if (omit_final_model_readback) {
            // An exact setter retry is a duplicate while live readback still
            // matches A. After B changes the same layer, A becomes a new
            // desired state and must be applied again rather than returning
            // the stale completed receipt.
            revision_sequence.append(repeat_typography_response);
            revision_sequence.append(intermediate_typography_response);
            revision_sequence.append(passing_revision_critique);
            revision_sequence.append(typography_response);
            revision_sequence.append(passing_revision_critique);
          }
          if (!omit_final_model_readback) revision_sequence.append(final_readback_response);
          revision_sequence.append(revision_completion_intent);
          revision_sequence.append(passing_revision_critique);
          qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON",
              QJsonDocument(revision_sequence).toJson(QJsonDocument::Compact));
          qputenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS", QByteArrayLiteral("40"));
          revision_phase = true;
          QVariantMap revision_report;
          auto revision_completion_count = 0;
          const auto revision_connection = QObject::connect(
              &agent_edit, &photo_editor::desktop::AgentEditController::draftRunFinished,
              &agent_edit, [&](const QString&, const QVariantMap& report) {
                revision_report = report;
                ++revision_completion_count;
              });
          const auto revision_started = agent_edit.reviseDraft(
              QStringLiteral("Slightly enlarge the existing APPROVED HEADLINE. Keep every word unchanged and preserve the photograph."));
          const auto revision_request_id = revision_started.value(QStringLiteral("requestId")).toString();
          const auto revision_accepted = revision_started.value(QStringLiteral("accepted")).toBool() &&
              revision_started.value(QStringLiteral("queued")).toBool() &&
              !revision_request_id.isEmpty() &&
              revision_started.value(QStringLiteral("revisedFromRunId")).toString() == previous_accepted_run_id &&
              revision_started.value(QStringLiteral("taskId")).toString() == task_request_id;
          const auto revision_finished = revision_accepted &&
              wait_for_condition([&]() { return revision_completion_count == 1; }, 60000);
          QObject::disconnect(revision_connection);
          revision_phase = false;
          qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON");
          qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS");
          const auto revision_validation = validation_for_report(revision_report);
          const auto revision_photo_evidence = revision_validation
              .value(QStringLiteral("photographicQualityEvidence")).toMap();
          const auto revision_baseline = revision_report.value(QStringLiteral("modelToolPlan")).toMap()
              .value(QStringLiteral("compositionPhotoBaseline")).toMap();
          const auto revision_tool_reports = revision_report.value(QStringLiteral("modelToolPlan")).toMap()
              .value(QStringLiteral("toolReports")).toList();
          auto repeated_typography_a_prevented = false;
          auto repeated_typography_a_prior_state_matches = false;
          auto restored_typography_state_verified = false;
          auto typography_report_index = qint64{0};
          auto typography_a_success_count = 0;
          auto typography_a_first_step = qint64{0};
          auto typography_a_restored_step = qint64{0};
          auto typography_b_step = qint64{0};
          auto typography_a_restored_round = -1;
          auto typography_a_readback_confirmed_after_b = false;
          auto model_requested_readback_after_a = false;
          for (const auto& value : revision_tool_reports) {
            ++typography_report_index;
            const auto tool_report = value.toMap();
            const auto tool_name = tool_report.value(QStringLiteral("toolName")).toString();
            const auto arguments = tool_report.value(QStringLiteral("arguments")).toMap();
            const auto content = tool_report.value(QStringLiteral("structuredContent")).toMap();
            const auto operation = arguments.value(QStringLiteral("operation")).toString();
            if (tool_name == QStringLiteral("photo.edit_composition") &&
                operation == QStringLiteral("typography") &&
                arguments.value(QStringLiteral("layerId")).toString() == QStringLiteral("chain-text")) {
              const auto requested_size = arguments.value(QStringLiteral("values")).toMap()
                  .value(QStringLiteral("size")).toInt();
              if (requested_size == expected_size) {
                if (tool_report.value(QStringLiteral("replayPrevented")).toBool()) {
                  repeated_typography_a_prevented = true;
                  for (const auto& prior_layer : content.value(QStringLiteral("priorResult")).toMap().value(QStringLiteral("layers")).toList()) {
                    const auto prior = prior_layer.toMap();
                    if (prior.value(QStringLiteral("id")).toString() == QStringLiteral("chain-text"))
                      repeated_typography_a_prior_state_matches = prior.value(QStringLiteral("textTypography")).toMap()
                          .value(QStringLiteral("size")).toInt() == expected_size;
                  }
                } else if (tool_report.value(QStringLiteral("ok")).toBool()) {
                  ++typography_a_success_count;
                  const auto step = typography_report_index;
                  if (typography_a_success_count == 1) typography_a_first_step = step;
                  else if (typography_a_success_count == 2) {
                    typography_a_restored_step = step;
                    typography_a_restored_round = tool_report.value(QStringLiteral("round")).toInt();
                    restored_typography_state_verified = tool_report.value(QStringLiteral("stateVerifiedRepeatSetter")).toBool();
                  }
                }
              } else if (requested_size == expected_size - 12 && tool_report.value(QStringLiteral("ok")).toBool()) {
                typography_b_step = typography_report_index;
              }
            } else if (typography_a_success_count == 2 && tool_name == QStringLiteral("photo.inspect_composition")) {
              model_requested_readback_after_a = true;
            }
          }
          const auto revision_model_plan = revision_report.value(QStringLiteral("modelToolPlan")).toMap();
          for (const auto& round_value : revision_model_plan.value(QStringLiteral("rounds")).toList()) {
            const auto round = round_value.toMap();
            if (round.value(QStringLiteral("round")).toInt() != typography_a_restored_round) continue;
            const auto execution = round.value(QStringLiteral("execution")).toMap();
            const auto review = execution.value(QStringLiteral("agentCompositionReviewRecord")).toMap();
            const auto assessment = review.value(QStringLiteral("compositionAssessment")).toMap();
            const auto receipt = review.value(QStringLiteral("compositionInspectionReceipt")).toMap();
            const auto receipt_content = receipt.value(QStringLiteral("structuredContent")).toMap();
            const auto visual_evidence = review.value(QStringLiteral("visualEvidence")).toMap();
            auto restored_layer_read_back = false;
            for (const auto& layer_value : receipt_content.value(QStringLiteral("layers")).toList()) {
              const auto layer = layer_value.toMap();
              if (layer.value(QStringLiteral("id")).toString() == QStringLiteral("chain-text") &&
                  layer.value(QStringLiteral("text")).toString() == QStringLiteral("APPROVED HEADLINE") &&
                  layer.value(QStringLiteral("textTypography")).toMap()
                          .value(QStringLiteral("size")).toInt() == expected_size) {
                restored_layer_read_back = true;
                break;
              }
            }
            typography_a_readback_confirmed_after_b =
                review.value(QStringLiteral("contract")).toString() ==
                    QStringLiteral("agent-composition-round-review-v1") &&
                review.value(QStringLiteral("round")).toInt() == typography_a_restored_round &&
                !review.value(QStringLiteral("assessmentRetained")).toBool() &&
                assessment.value(QStringLiteral("batchRound")).toInt() == typography_a_restored_round &&
                assessment.value(QStringLiteral("assetId")).toString() == QStringLiteral("asset-1") &&
                receipt.value(QStringLiteral("ok")).toBool() &&
                receipt.value(QStringLiteral("toolName")).toString() == QStringLiteral("photo.inspect_composition") &&
                receipt.value(QStringLiteral("arguments")).toMap().value(QStringLiteral("assetId")).toString() ==
                    QStringLiteral("asset-1") &&
                receipt.value(QStringLiteral("runId")).toString() == execution.value(QStringLiteral("runId")).toString() &&
                receipt_content.value(QStringLiteral("assetId")).toString() == QStringLiteral("asset-1") &&
                !receipt_content.value(QStringLiteral("paintBusy")).toBool() &&
                receipt_content.value(QStringLiteral("paintError")).toString().isEmpty() &&
                receipt_content.value(QStringLiteral("taskStateHash")).toString() ==
                    assessment.value(QStringLiteral("sourceRecipeSha256")).toString() &&
                restored_layer_read_back &&
                visual_evidence.value(QStringLiteral("assetId")).toString() == QStringLiteral("asset-1") &&
                visual_evidence.value(QStringLiteral("proofSha256")).toString() ==
                    assessment.value(QStringLiteral("sourceProofSha256")).toString() &&
                visual_evidence.value(QStringLiteral("sourceRecipeSha256")).toString() ==
                    assessment.value(QStringLiteral("sourceRecipeSha256")).toString();
            break;
          }
          const auto typography_replay_regression_passed = revision_index != 0 ||
              (typography_a_success_count == 2 && repeated_typography_a_prevented &&
               typography_a_first_step > 0 && repeated_typography_a_prior_state_matches && restored_typography_state_verified &&
               revision_typography_sizes.mid(typography_size_count_before) == QVariantList{double(expected_size), double(expected_size - 12), double(expected_size)} &&
               typography_b_step > typography_a_first_step && typography_a_restored_step > typography_b_step &&
               typography_a_readback_confirmed_after_b && !model_requested_readback_after_a);
          QString revision_mcp_run_id;
          for (const auto& value : revision_tool_reports) {
            const auto tool_report = value.toMap();
            if (tool_report.value(QStringLiteral("toolName")).toString() ==
                    QStringLiteral("photo.inspect_composition") &&
                tool_report.value(QStringLiteral("ok")).toBool())
              revision_mcp_run_id = tool_report.value(QStringLiteral("runId")).toString();
          }
          const auto revision_completion = revision_report
              .value(QStringLiteral("nativeCompositionCompletion")).toMap();
          const auto revision_final_critique = revision_validation
              .value(QStringLiteral("modelProofCritique")).toMap();
          const auto revision_prompt_audit = revision_report
              .value(QStringLiteral("modelPromptAudit")).toMap();
          const auto completion_response_json = QJsonDocument::fromJson(
              revision_completion_intent.value(QStringLiteral("message")).toObject()
                  .value(QStringLiteral("content")).toString().toUtf8()).object();
          const auto completion_edit_intent = completion_response_json
              .value(QStringLiteral("editIntent")).toObject().toVariantMap();
          const auto host_completion_evidence =
              photo_editor::desktop::agent_detail::native_composition_completion_evidence(
                  revision_prompt_audit, completion_edit_intent, QVariantMap{},
                  revision_model_plan, QStringLiteral("asset-1"), 1);
          const auto host_completion_verifier_accepts =
              host_completion_evidence.value(QStringLiteral("controllerVerified")).toBool() &&
              host_completion_evidence.value(QStringLiteral("readbackSource")).toString() ==
                  QStringLiteral("host-current-composition-review");
          const auto host_completion_verifier_rejects_tampering = [&](const QString& field) {
            auto tampered_plan = revision_model_plan;
            auto rounds = tampered_plan.value(QStringLiteral("rounds")).toList();
            if (rounds.isEmpty()) return false;
            auto latest_round = rounds.last().toMap();
            auto receipt = latest_round.value(QStringLiteral("compositionInspectionReceipt")).toMap();
            auto receipt_content = receipt.value(QStringLiteral("structuredContent")).toMap();
            auto visual = latest_round.value(QStringLiteral("visualEvidence")).toMap();
            const auto mismatch_sha = [](const QString& original) {
              const auto zeros = QString(64, QLatin1Char('0'));
              return original == zeros ? QString(64, QLatin1Char('1')) : zeros;
            };
            if (field == QStringLiteral("runId")) {
              receipt.insert(QStringLiteral("runId"), QStringLiteral("stale-run-id"));
            } else if (field == QStringLiteral("paintBusy")) {
              receipt_content.insert(QStringLiteral("paintBusy"), true);
            } else if (field == QStringLiteral("paintError")) {
              receipt_content.insert(QStringLiteral("paintError"), QStringLiteral("unfinished render"));
            } else if (field == QStringLiteral("taskStateHash")) {
              receipt_content.insert(QStringLiteral("taskStateHash"),
                  mismatch_sha(receipt_content.value(QStringLiteral("taskStateHash")).toString()));
            } else if (field == QStringLiteral("proofSha256")) {
              visual.insert(QStringLiteral("proofSha256"),
                  mismatch_sha(visual.value(QStringLiteral("proofSha256")).toString()));
            } else if (field == QStringLiteral("sourceRecipeSha256")) {
              visual.insert(QStringLiteral("sourceRecipeSha256"),
                  mismatch_sha(visual.value(QStringLiteral("sourceRecipeSha256")).toString()));
            } else {
              return false;
            }
            receipt.insert(QStringLiteral("structuredContent"), receipt_content);
            latest_round.insert(QStringLiteral("compositionInspectionReceipt"), receipt);
            latest_round.insert(QStringLiteral("visualEvidence"), visual);
            rounds[rounds.size() - 1] = latest_round;
            tampered_plan.insert(QStringLiteral("rounds"), rounds);

            // A model-requested inspection with the same layer identities and
            // the same bad receipt fields must not rescue a rejected host
            // observation through the legacy report-only fallback.
            auto requested_inspection = receipt;
            requested_inspection.insert(QStringLiteral("ok"), true);
            requested_inspection.insert(QStringLiteral("toolName"), QStringLiteral("photo.inspect_composition"));
            requested_inspection.insert(QStringLiteral("arguments"),
                QVariantMap{{QStringLiteral("assetId"), QStringLiteral("asset-1")}});
            const auto last_round_number = latest_round.value(QStringLiteral("round")).toInt();
            requested_inspection.insert(QStringLiteral("round"), last_round_number + 1);
            requested_inspection.insert(QStringLiteral("visualEvidence"), visual);
            auto reports = tampered_plan.value(QStringLiteral("toolReports")).toList();
            reports.append(requested_inspection);
            tampered_plan.insert(QStringLiteral("toolReports"), reports);
            const auto evidence = photo_editor::desktop::agent_detail::native_composition_completion_evidence(
                revision_prompt_audit, completion_edit_intent, QVariantMap{},
                tampered_plan, QStringLiteral("asset-1"), 1);
            return !evidence.value(QStringLiteral("controllerVerified")).toBool();
          };
          auto host_completion_verifier_rejects_later_command = false;
          if (!revision_model_plan.value(QStringLiteral("rounds")).toList().isEmpty()) {
            auto later_command_plan = revision_model_plan;
            const auto last_round = later_command_plan.value(QStringLiteral("rounds")).toList().last().toMap();
            auto reports = later_command_plan.value(QStringLiteral("toolReports")).toList();
            reports.append(QVariantMap{
                {QStringLiteral("ok"), true},
                {QStringLiteral("toolName"), QStringLiteral("app.invoke_command")},
                {QStringLiteral("round"), last_round.value(QStringLiteral("round")).toInt() + 1},
                {QStringLiteral("structuredContent"), QVariantMap{{QStringLiteral("changed"), true}}},
            });
            later_command_plan.insert(QStringLiteral("toolReports"), reports);
            const auto evidence = photo_editor::desktop::agent_detail::native_composition_completion_evidence(
                revision_prompt_audit, completion_edit_intent, QVariantMap{},
                later_command_plan, QStringLiteral("asset-1"), 1);
            host_completion_verifier_rejects_later_command =
                !evidence.value(QStringLiteral("controllerVerified")).toBool();
          }
          const auto host_completion_tamper_rejections =
              host_completion_verifier_rejects_tampering(QStringLiteral("runId")) &&
              host_completion_verifier_rejects_tampering(QStringLiteral("paintBusy")) &&
              host_completion_verifier_rejects_tampering(QStringLiteral("paintError")) &&
              host_completion_verifier_rejects_tampering(QStringLiteral("taskStateHash")) &&
              host_completion_verifier_rejects_tampering(QStringLiteral("proofSha256")) &&
              host_completion_verifier_rejects_tampering(QStringLiteral("sourceRecipeSha256"));
          const auto same_immutable_baseline =
              revision_photo_evidence.value(QStringLiteral("baselineSourceRecipeSha256")).toString() ==
                  baseline_source_recipe_sha &&
              revision_photo_evidence.value(QStringLiteral("baselineProofSha256")).toString() ==
                  baseline_proof_sha &&
              revision_photo_evidence.value(QStringLiteral("preMutationInspectionReceiptSha256")).toString() ==
                  baseline_inspection_sha &&
              revision_photo_evidence.value(QStringLiteral("normalizedPhotoCompositionSha256")).toString() ==
                  baseline_photo_fingerprint &&
              revision_photo_evidence.value(QStringLiteral("baselineNeedsUserReview")).toBool() ==
                  baseline_raw_needs_review &&
              revision_photo_evidence.value(QStringLiteral("baselineWarnings")).toStringList() ==
                  initial_final_photo_evidence.value(QStringLiteral("baselineWarnings")).toStringList() &&
              revision_photo_evidence.value(QStringLiteral("photoRecipeUnchanged")).toBool();
          const auto fresh_revision_binding =
              revision_photo_evidence.value(QStringLiteral("revisionBindingVerified")).toBool() &&
              revision_photo_evidence.value(QStringLiteral("currentRequestId")).toString() == revision_request_id &&
              revision_request_id != initial_request_id &&
              revision_photo_evidence.value(QStringLiteral("currentTaskId")).toString() == task_request_id &&
              !revision_mcp_run_id.isEmpty() && revision_mcp_run_id != baseline_mcp_run_id &&
              revision_photo_evidence.value(QStringLiteral("currentSourceRecipeSha256")).toString().size() == 64 &&
              revision_photo_evidence.value(QStringLiteral("currentProofSha256")).toString().size() == 64 &&
              revision_photo_evidence.value(QStringLiteral("currentTaskStateHash")).toString().size() == 64 &&
              revision_photo_evidence.value(QStringLiteral("currentSourceRecipeSha256")).toString() !=
                  baseline_source_recipe_sha;
          const auto host_baseline_retained =
              revision_baseline.value(QStringLiteral("sourceRecipeSha256")).toString() == baseline_source_recipe_sha &&
              revision_baseline.value(QStringLiteral("proofSha256")).toString() == baseline_proof_sha &&
              revision_baseline.value(QStringLiteral("runId")).toString() == baseline_mcp_run_id &&
              revision_baseline.value(QStringLiteral("preMutationInspectionReceiptSha256")).toString() ==
                  baseline_inspection_sha &&
              !revision_baseline.contains(QStringLiteral("sourceRecipeJson")) &&
              !revision_baseline.contains(QStringLiteral("preMutationInspectionReceipt")) &&
              !revision_baseline.contains(QStringLiteral("revisionBinding"));
          const auto native_text_layers = chain_layers;
          const auto revised_layer = native_text_layers.size() == 1
              ? native_text_layers.first().toMap() : QVariantMap{};
          const auto revised_typography = revised_layer.value(QStringLiteral("textTypography")).toMap();
          const auto revision_is_complete =
              revision_finished && revision_completion_count == 1 &&
              revision_report.value(QStringLiteral("ok")).toBool() &&
              agent_edit.draftActive() &&
              revision_report.value(QStringLiteral("runId")).toString() != previous_accepted_run_id &&
              revision_report.value(QStringLiteral("revisedFromRunId")).toString() == previous_accepted_run_id &&
              revision_report.value(QStringLiteral("taskId")).toString() == task_request_id &&
              revision_report.value(QStringLiteral("modelId")).toString() ==
                  QStringLiteral("ollama-qwen-tool-plan-test") &&
              revision_completion.value(QStringLiteral("controllerVerified")).toBool() &&
              (revision_index != 0 || revision_completion.value(QStringLiteral("readbackSource")).toString() ==
                  QStringLiteral("host-current-composition-review")) &&
              revision_completion.value(QStringLiteral("requested")).toBool() &&
              revision_completion.value(QStringLiteral("verificationContract")).toString() ==
                  QStringLiteral("agent-native-composition-completion-controller-v1") &&
              revision_completion.value(QStringLiteral("assetId")).toString() == QStringLiteral("asset-1") &&
              revision_completion.value(QStringLiteral("acceptedMutationCount")).toInt() > 0 &&
              revision_completion.value(QStringLiteral("readbackConfirmed")).toBool() &&
              (revision_index != 0 || (host_completion_verifier_accepts &&
                  host_completion_tamper_rejections && host_completion_verifier_rejects_later_command)) &&
              revision_prompt_audit.value(QStringLiteral("modelDecision")).toString() ==
                  QStringLiteral("edit_intent") &&
              revision_prompt_audit.value(QStringLiteral("completionMode")).toString() ==
                  QStringLiteral("preserve-after-native-composition-readback") &&
              revision_validation.value(QStringLiteral("photographicReviewProjectionApplied")).toBool() &&
              !revision_validation.value(QStringLiteral("photographicReviewNeedsUserReview")).toBool() &&
              !revision_validation.value(QStringLiteral("needsUserReview")).toBool() &&
              revision_final_critique.value(QStringLiteral("ok")).toBool() &&
              revision_final_critique.value(QStringLiteral("pass")).toBool() &&
              revision_final_critique.value(QStringLiteral("modelId")).toString() ==
                  QStringLiteral("ollama-qwen-tool-plan-test") &&
              same_immutable_baseline && fresh_revision_binding && host_baseline_retained &&
              revised_layer.value(QStringLiteral("id")).toString() == QStringLiteral("chain-text") &&
              revised_layer.value(QStringLiteral("text")).toString() == QStringLiteral("APPROVED HEADLINE") &&
              revised_typography.value(QStringLiteral("size")).toInt() == expected_size &&
              composition_repair_add_text_calls == 1 &&
              chain_operations.count(QStringLiteral("typography")) == (revision_index == 0 ? 3 : 4) &&
              typography_replay_regression_passed &&
              agent_edit.taskTransaction().value(QStringLiteral("status")).toString() == QStringLiteral("review") &&
              agent_edit.taskTransaction().value(QStringLiteral("revertAvailable")).toBool();
          if (!revision_is_complete) {
            std::cerr << "composition revision handoff diagnostic=" << QJsonDocument::fromVariant(QVariantMap{
                {QStringLiteral("revisionIndex"), revision_index},
                {QStringLiteral("accepted"), revision_accepted},
                {QStringLiteral("finished"), revision_finished},
                {QStringLiteral("completionCount"), revision_completion_count},
                {QStringLiteral("ok"), revision_report.value(QStringLiteral("ok"))},
                {QStringLiteral("failureReason"), revision_report.value(QStringLiteral("failureReason"))},
                {QStringLiteral("requestId"), revision_request_id},
                {QStringLiteral("expectedSourceRunId"), previous_accepted_run_id},
                {QStringLiteral("actualSourceRunId"), revision_report.value(QStringLiteral("revisedFromRunId"))},
                {QStringLiteral("sameImmutableBaseline"), same_immutable_baseline},
                {QStringLiteral("freshRevisionBinding"), fresh_revision_binding},
                {QStringLiteral("hostBaselineRetained"), host_baseline_retained},
                {QStringLiteral("hostCompletionVerifierAcceptsHostReadback"), host_completion_verifier_accepts},
                {QStringLiteral("hostCompletionVerifierRejectsTampering"), host_completion_tamper_rejections},
                {QStringLiteral("hostCompletionVerifierRejectsLaterCommand"), host_completion_verifier_rejects_later_command},
                {QStringLiteral("typographyReplayRegressionPassed"), typography_replay_regression_passed},
                {QStringLiteral("typographyReplayAudit"), QVariantMap{
                    {QStringLiteral("successfulARequests"), typography_a_success_count},
                    {QStringLiteral("duplicateAPrevented"), repeated_typography_a_prevented},
                    {QStringLiteral("firstAReportOrder"), typography_a_first_step},
                    {QStringLiteral("duplicatePriorStateMatches"), repeated_typography_a_prior_state_matches},
                    {QStringLiteral("bReportOrder"), typography_b_step},
                    {QStringLiteral("restoredAReportOrder"), typography_a_restored_step},
                    {QStringLiteral("restoredRound"), typography_a_restored_round},
                    {QStringLiteral("restoredAReadbackConfirmed"), typography_a_readback_confirmed_after_b},
                    {QStringLiteral("modelRequestedReadbackAfterA"), model_requested_readback_after_a},
                }},
                {QStringLiteral("completion"), revision_completion},
                {QStringLiteral("promptAudit"), revision_prompt_audit},
                {QStringLiteral("finalCritique"), revision_final_critique},
                {QStringLiteral("photoEvidence"), revision_photo_evidence},
                {QStringLiteral("publicBaseline"), revision_baseline},
                {QStringLiteral("layer"), revised_layer},
                {QStringLiteral("operations"), chain_operations},
                {QStringLiteral("taskActions"), task_actions},
            }).toJson(QJsonDocument::Compact).toStdString() << '\n';
            return fail("Each accepted same-task typography revision must retain the original photographic baseline and bind a fresh current proof");
          }
          previous_accepted_run_id = revision_report.value(QStringLiteral("runId")).toString();
        }
        if (!agent_edit.rollbackDraft().value(QStringLiteral("ok")).toBool() ||
            agent_edit.draftActive() ||
            task_actions != QStringList{QStringLiteral("begin"), QStringLiteral("check_revert"),
                QStringLiteral("review"), QStringLiteral("continue"), QStringLiteral("check_revert"),
                QStringLiteral("review"), QStringLiteral("continue"), QStringLiteral("check_revert"),
                QStringLiteral("review"), QStringLiteral("check_revert"), QStringLiteral("revert")} ||
            catalog_model.assetById(QStringLiteral("asset-1"))
                .value(QStringLiteral("adjustmentPatchJson")).toString() != task_before_recipe ||
            native_planning_recipe != task_before_recipe)
          return fail("Repeated same-task typography revisions must preserve whole-task exact Revert");
        bridge.stop();
        std::cout << "Per-batch review, two source-bound same-task revisions and whole-task exact Revert passed.\n";
        return EXIT_SUCCESS;
      }
      if (should_finish_successfully) {
        const auto reports = mcp_draft_report.value("assetReports").toList();
        const auto asset_report = reports.isEmpty() ? QVariantMap{} : reports.first().toMap();
        const auto validation = asset_report.value("validationReport").toMap();
        const auto composition_review = validation.value("compositionReview").toMap();
        const auto detail_report = composition_review.value("detailProofReport").toMap();
        const auto detail_binding_audit = detail_report.value("bindingAudit").toMap();
        const auto crops = composition_review.value("targetedDetailProofs").toList();
        QFile requests_file(composition_repair_request_path);
        int turns_with_overview_and_detail = 0;
        QList<int> proof_critique_request_indices;
        QList<int> proof_critique_image_counts;
        QList<QVariantMap> proof_critique_validation_reports;
        QList<QVariantList> proof_critique_expected_graphic_layers;
        QList<int> assessment_planner_request_indices;
        QList<QVariantMap> assessment_planner_evidence;
        QList<QVariantMap> assessment_planner_assessments;
        QList<int> assessment_planner_image_counts;
        const auto evidence_from_repair_prompt = [](const QString& prompt) {
          const auto header = QStringLiteral("Current rendered planning evidence JSON:\n");
          const auto footer = QStringLiteral(
              "\nThe first attached planning image represents the completed recipe state.");
          const auto start = prompt.indexOf(header);
          if (start < 0) return QVariantMap{};
          const auto json_start = start + header.size();
          const auto end = prompt.indexOf(footer, json_start);
          if (end <= json_start) return QVariantMap{};
          QJsonParseError parse_error;
          const auto document = QJsonDocument::fromJson(
              prompt.mid(json_start, end - json_start).toUtf8(), &parse_error);
          return parse_error.error == QJsonParseError::NoError && document.isObject()
              ? document.object().toVariantMap() : QVariantMap{};
        };
        const auto assessment_from_planner_prompt = [](const QString& prompt) {
          const auto header = QStringLiteral("Prior MCP tool results JSON:\n");
          const auto start = prompt.indexOf(header);
          if (start < 0) return QVariantMap{};
          QJsonParseError parse_error;
          const auto document = QJsonDocument::fromJson(
              prompt.mid(start + header.size()).trimmed().toUtf8(), &parse_error);
          if (parse_error.error != QJsonParseError::NoError)
            return QVariantMap{};
          const auto is_current_assessment = [](const QJsonObject& result) {
            return result.value(QStringLiteral("kind")).toString() ==
                    QStringLiteral("current-composition-assessment") &&
                result.value(QStringLiteral("contract")).toString() ==
                    QStringLiteral("agent-current-composition-assessment-v1");
          };
          if (document.isArray()) {
            const auto results = document.array();
            for (qsizetype index = results.size(); index > 0; --index) {
              const auto result = results.at(index - 1).toObject();
              if (is_current_assessment(result)) return result.toVariantMap();
            }
          } else if (document.isObject()) {
            // Async continuations compact the prior tool history into an object;
            // the current native assessment is then promoted to this field.
            const auto assessment = document.object()
                .value(QStringLiteral("currentCompositionAssessment")).toObject();
            if (is_current_assessment(assessment)) return assessment.toVariantMap();
          }
          return QVariantMap{};
        };
        const auto decode_lossless_rows = [](const QVariant& encoded_rows, bool* valid) {
          if (valid != nullptr) *valid = false;
          QVariantList decoded_rows;
          const auto encoded = encoded_rows.toMap();
          if (encoded.value(QStringLiteral("encoding")).toString() !=
              QStringLiteral("lossless-columnar-rows-v1"))
            return decoded_rows;
          const auto fields = encoded.value(QStringLiteral("fields")).toList();
          const auto rows = encoded.value(QStringLiteral("rows")).toList();
          const auto row_ids = encoded.value(QStringLiteral("rowIds")).toList();
          const auto identity_field = encoded.value(QStringLiteral("identityField")).toString();
          const auto has_identity = !identity_field.isEmpty();
          const auto has_common_fields = encoded.contains(QStringLiteral("commonFields"));
          const auto common_fields_value = encoded.value(QStringLiteral("commonFields"));
          if (has_common_fields && common_fields_value.metaType().id() != QMetaType::QVariantMap)
            return decoded_rows;
          const auto common_fields = common_fields_value.toMap();
          if ((has_identity && row_ids.size() != rows.size()) ||
              (!has_identity && !row_ids.isEmpty()) ||
              encoded.contains(QStringLiteral("presenceMask")) ==
                  encoded.contains(QStringLiteral("presenceMasks")))
            return decoded_rows;
          QSet<QString> unique_fields;
          for (auto common = common_fields.cbegin(); common != common_fields.cend(); ++common) {
            if (common.key().isEmpty() || (has_identity && common.key() == identity_field))
              return decoded_rows;
            unique_fields.insert(common.key());
          }
          for (const auto& field_value : fields) {
            const auto field = field_value.toString();
            if (field.isEmpty() || unique_fields.contains(field) ||
                (has_identity && field == identity_field))
              return decoded_rows;
            unique_fields.insert(field);
          }
          const auto common_mask = encoded.value(QStringLiteral("presenceMask")).toString();
          const auto masks = encoded.value(QStringLiteral("presenceMasks")).toList();
          if ((!encoded.contains(QStringLiteral("presenceMask")) && masks.size() != rows.size()) ||
              (encoded.contains(QStringLiteral("presenceMask")) && !masks.isEmpty()))
            return decoded_rows;

          QSet<QString> seen_ids;
          for (qsizetype row_index = 0; row_index < rows.size(); ++row_index) {
            const auto values = rows.at(row_index).toList();
            const auto mask = encoded.contains(QStringLiteral("presenceMask"))
                ? common_mask : masks.at(row_index).toString();
            if (values.size() != fields.size() || mask.size() != fields.size())
              return QVariantList{};
            QVariantMap row = common_fields;
            for (qsizetype field_index = 0; field_index < fields.size(); ++field_index) {
              const auto field = fields.at(field_index).toString();
              if (field.isEmpty() || (mask.at(field_index) != QLatin1Char('0') &&
                                      mask.at(field_index) != QLatin1Char('1')))
                return QVariantList{};
              if (mask.at(field_index) == QLatin1Char('1')) {
                row.insert(field, values.at(field_index));
              } else if (!values.at(field_index).isNull()) {
                return QVariantList{};
              }
            }
            if (has_identity) {
              if (row.contains(identity_field)) return QVariantList{};
              if (row_ids.at(row_index).metaType().id() != QMetaType::QString)
                return QVariantList{};
              const auto identity = row_ids.at(row_index).toString();
              if (identity.trimmed().isEmpty() || seen_ids.contains(identity))
                return QVariantList{};
              seen_ids.insert(identity);
              row.insert(identity_field, identity);
            }
            decoded_rows.append(row);
          }
          if (valid != nullptr) *valid = true;
          return decoded_rows;
        };
        const auto exact_string_list_matches = [](const QVariant& candidate,
                                                   const QVariantList& expected) {
          QVariantList actual;
          if (candidate.metaType().id() == QMetaType::QVariantList)
            actual = candidate.toList();
          else if (candidate.metaType().id() == QMetaType::QStringList) {
            for (const auto& item : candidate.toStringList()) actual.append(item);
          } else {
            return false;
          }
          if (actual.size() != expected.size()) return false;
          for (qsizetype index = 0; index < actual.size(); ++index) {
            if (actual.at(index).metaType().id() != QMetaType::QString ||
                expected.at(index).metaType().id() != QMetaType::QString ||
                actual.at(index).toString() != expected.at(index).toString())
              return false;
          }
          return true;
        };
        const auto decode_referenced_lossless_rows = [&](const QVariant& encoded_rows,
                                                          const QVariantList& expected_ids,
                                                          bool* valid) {
          if (valid != nullptr) *valid = false;
          auto encoded = encoded_rows.toMap();
          if (encoded.contains(QStringLiteral("rowIdsReference"))) {
            const auto reference = encoded.value(QStringLiteral("rowIdsReference"));
            if (reference.metaType().id() != QMetaType::Bool || !reference.toBool() ||
                expected_ids.isEmpty() ||
                (encoded.contains(QStringLiteral("rowIds")) &&
                 !exact_string_list_matches(encoded.value(QStringLiteral("rowIds")), expected_ids)))
              return QVariantList{};
            encoded.remove(QStringLiteral("rowIdsReference"));
            encoded.insert(QStringLiteral("rowIds"), expected_ids);
          }
          return decode_lossless_rows(encoded, valid);
        };
        const auto resolve_sha256_references = [](const QVariantMap& encoded, bool* valid) {
          if (valid != nullptr) *valid = false;
          constexpr auto marker_key = "$h1";
          constexpr auto table_key = "hashes";
          const auto hashes_value = encoded.value(QString::fromLatin1(table_key));
          if (encoded.contains(QString::fromLatin1(table_key)) &&
              hashes_value.metaType().id() != QMetaType::QVariantList)
            return QVariantMap{};
          const auto hashes = hashes_value.toList();
          QSet<QString> unique_hashes;
          for (const auto& hash_value : hashes) {
            if (hash_value.metaType().id() != QMetaType::QString) return QVariantMap{};
            const auto hash = hash_value.toString();
            const auto is_lower_hex = hash.size() == 64 &&
                std::all_of(hash.cbegin(), hash.cend(), [](QChar value) {
                  return (value >= QLatin1Char('0') && value <= QLatin1Char('9')) ||
                      (value >= QLatin1Char('a') && value <= QLatin1Char('f'));
                });
            if (!is_lower_hex || unique_hashes.contains(hash)) return QVariantMap{};
            unique_hashes.insert(hash);
          }
          QHash<qsizetype, int> reference_counts;
          bool references_are_valid = true;
          const auto decode_value = [&](auto&& self, const QVariant& value,
                                        const QString& field) -> QVariant {
            if (!references_are_valid) return {};
            if (value.metaType().id() == QMetaType::QVariantMap) {
              const auto map = value.toMap();
              if (map.contains(QString::fromLatin1(table_key))) {
                references_are_valid = false;
                return {};
              }
              if (map.contains(QString::fromLatin1(marker_key))) {
                const auto index_value = map.value(QString::fromLatin1(marker_key));
                const auto index_type = index_value.metaType().id();
                const auto index_type_is_numeric = index_type == QMetaType::Int ||
                    index_type == QMetaType::UInt || index_type == QMetaType::LongLong ||
                    index_type == QMetaType::ULongLong || index_type == QMetaType::Double;
                bool integral_number = index_type_is_numeric;
                if (index_type == QMetaType::Double) {
                  const auto index_number = index_value.toDouble();
                  integral_number = std::isfinite(index_number) &&
                      std::floor(index_number) == index_number;
                }
                bool index_is_convertible = false;
                const auto index_number = index_value.toLongLong(&index_is_convertible);
                if (!field.endsWith(QStringLiteral("Sha256")) || map.size() != 1 ||
                    !index_type_is_numeric || !integral_number || !index_is_convertible ||
                    index_number < 0 || index_number >= hashes.size()) {
                  references_are_valid = false;
                  return {};
                }
                const auto index = static_cast<qsizetype>(index_number);
                ++reference_counts[index];
                return hashes.at(index);
              }
              QVariantMap decoded;
              for (auto iterator = map.cbegin(); iterator != map.cend(); ++iterator)
                decoded.insert(iterator.key(), self(self, iterator.value(), iterator.key()));
              return decoded;
            }
            if (value.metaType().id() == QMetaType::QVariantList) {
              QVariantList decoded;
              for (const auto& entry : value.toList())
                decoded.append(self(self, entry, field));
              return decoded;
            }
            return value;
          };
          auto without_table = encoded;
          without_table.remove(QString::fromLatin1(table_key));
          const auto decoded = decode_value(decode_value, without_table, {});
          if (!references_are_valid) return QVariantMap{};
          if (encoded.contains(QString::fromLatin1(table_key))) {
            if (hashes.isEmpty() || reference_counts.size() != hashes.size())
              return QVariantMap{};
            for (qsizetype index = 0; index < hashes.size(); ++index)
              if (reference_counts.value(index) < 2) return QVariantMap{};
          } else if (!reference_counts.isEmpty()) {
            return QVariantMap{};
          }
          if (valid != nullptr) *valid = true;
          return decoded.toMap();
        };
        const auto decode_expected_graphic_layers = [&](const QVariantMap& encoded, bool* valid) {
          if (valid != nullptr) *valid = false;
          bool base_rows_are_lossless = false;
          auto base_rows = decode_lossless_rows(encoded, &base_rows_are_lossless);
          const auto typography_key = QStringLiteral("textTypographyByLayerId");
          const auto layout_key = QStringLiteral("layoutMetricsByLayerId");
          const auto base_identity = encoded.value(QStringLiteral("identityField")).toString();
          if (!base_rows_are_lossless || base_rows.isEmpty() || base_identity != QStringLiteral("id") ||
              !encoded.contains(typography_key) || !encoded.contains(layout_key) ||
              encoded.value(typography_key).metaType().id() != QMetaType::QVariantMap ||
              encoded.value(layout_key).metaType().id() != QMetaType::QVariantMap)
            return QVariantList{};

          const auto typography_table = encoded.value(typography_key).toMap();
          const auto layout_table = encoded.value(layout_key).toMap();
          if (typography_table.value(QStringLiteral("identityField")).toString() != QStringLiteral("id") ||
              layout_table.value(QStringLiteral("identityField")).toString() != QStringLiteral("id"))
            return QVariantList{};
          bool typography_rows_are_lossless = false;
          bool layout_rows_are_lossless = false;
          const auto typography_rows = decode_lossless_rows(typography_table, &typography_rows_are_lossless);
          const auto layout_rows = decode_lossless_rows(layout_table, &layout_rows_are_lossless);
          if (!typography_rows_are_lossless || !layout_rows_are_lossless ||
              typography_rows.size() != base_rows.size() || layout_rows.size() != base_rows.size())
            return QVariantList{};

          const auto rows_by_id = [](const QVariantList& rows, QHash<QString, QVariantMap>* indexed_rows) {
            if (indexed_rows == nullptr) return false;
            indexed_rows->clear();
            for (const auto& row_value : rows) {
              auto row = row_value.toMap();
              const auto id_value = row.take(QStringLiteral("id"));
              if (id_value.metaType().id() != QMetaType::QString) return false;
              const auto id = id_value.toString();
              if (id.trimmed().isEmpty() || id != id.trimmed() || indexed_rows->contains(id)) return false;
              indexed_rows->insert(id, row);
            }
            return indexed_rows->size() == rows.size();
          };
          QHash<QString, QVariantMap> typography_by_id;
          QHash<QString, QVariantMap> layout_by_id;
          if (!rows_by_id(typography_rows, &typography_by_id) ||
              !rows_by_id(layout_rows, &layout_by_id))
            return QVariantList{};

          QSet<QString> base_ids;
          for (auto& row_value : base_rows) {
            auto row = row_value.toMap();
            const auto id_value = row.value(QStringLiteral("id"));
            if (id_value.metaType().id() != QMetaType::QString) return QVariantList{};
            const auto id = id_value.toString();
            if (id.trimmed().isEmpty() || id != id.trimmed() || base_ids.contains(id) ||
                row.contains(QStringLiteral("textTypography")) ||
                row.contains(QStringLiteral("layoutMetrics")) ||
                !typography_by_id.contains(id) || !layout_by_id.contains(id))
              return QVariantList{};
            base_ids.insert(id);
            row.insert(QStringLiteral("textTypography"), typography_by_id.value(id));
            row.insert(QStringLiteral("layoutMetrics"), layout_by_id.value(id));
            row_value = row;
          }
          if (base_ids.size() != typography_by_id.size() || base_ids.size() != layout_by_id.size())
            return QVariantList{};
          if (valid != nullptr) *valid = true;
          return base_rows;
        };
        const auto expected_graphic_layers_from_prompt = [&](const QString& prompt, bool* valid) {
          if (valid != nullptr) *valid = false;
          const auto header = QStringLiteral("Expected graphic text layers JSON:\n");
          const auto start = prompt.indexOf(header);
          if (start < 0) return QVariantList{};
          const auto json_start = start + header.size();
          auto end = prompt.size();
          for (const auto& marker : {QStringLiteral("\nVerified unchanged-photo basis"),
                   QStringLiteral("\nDecode lossless-columnar-rows-v1:")}) {
            const auto marker_index = prompt.indexOf(marker, json_start);
            if (marker_index >= json_start) end = std::min(end, marker_index);
          }
          QJsonParseError parse_error;
          const auto document = QJsonDocument::fromJson(
              prompt.mid(json_start, end - json_start).trimmed().toUtf8(), &parse_error);
          if (parse_error.error != QJsonParseError::NoError || !document.isObject())
            return QVariantList{};
          return decode_expected_graphic_layers(document.object().toVariantMap(), valid);
        };
        if (requests_file.open(QIODevice::ReadOnly)) {
          const auto lines = requests_file.readAll().split('\n');
          for (qsizetype request_index = 0; request_index < lines.size(); ++request_index) {
            const auto& line = lines.at(request_index);
            const auto request = QJsonDocument::fromJson(line).object();
            const auto messages = request.value("messages").toArray();
            if (messages.size() < 2) continue;
            const auto system_prompt = messages.at(0).toObject().value("content").toString();
            const auto user_prompt = messages.at(1).toObject().value("content").toString();
            if (is_proof_critique_request(request)) {
              proof_critique_request_indices.append(static_cast<int>(request_index));
              const auto image_count = static_cast<int>(
                  messages.at(1).toObject().value("images").toArray().size());
              proof_critique_image_counts.append(image_count);
              const auto validation_json = extract_prompt_section(
                  user_prompt, QStringLiteral("Validation report JSON:\n"),
                  QStringLiteral("Previous attempts JSON:"));
              proof_critique_validation_reports.append(
                  QJsonDocument::fromJson(validation_json.toUtf8()).object().toVariantMap());
              bool expected_graphic_layers_valid = false;
              proof_critique_expected_graphic_layers.append(
                  expected_graphic_layers_from_prompt(user_prompt, &expected_graphic_layers_valid));
              if (!expected_graphic_layers_valid)
                proof_critique_expected_graphic_layers.last().clear();
            }
            if (system_prompt.contains(QStringLiteral("decision=tool_call_plan"))) {
              const auto assessment = assessment_from_planner_prompt(user_prompt);
              if (!assessment.isEmpty()) {
                assessment_planner_request_indices.append(static_cast<int>(request_index));
                assessment_planner_evidence.append(evidence_from_repair_prompt(user_prompt));
                assessment_planner_assessments.append(assessment);
                assessment_planner_image_counts.append(static_cast<int>(
                    messages.at(1).toObject().value("images").toArray().size()));
              }
            }
            if (messages.at(1).toObject().value("images").toArray().size() == 2)
              ++turns_with_overview_and_detail;
          }
        }
        const auto critique_detail_checks = [&decode_referenced_lossless_rows,
            &resolve_sha256_references, &exact_string_list_matches](
                                               const QVariantMap& validation,
                                               const QVariantMap& planning_evidence,
                                               const QVariantMap& assessment,
                                               const QVariantList& expected_graphic_layers,
                                               int image_count) {
          QVariantList expected_graphic_ids;
          QSet<QString> seen_graphic_ids;
          bool expected_graphic_ids_are_valid = !expected_graphic_layers.isEmpty();
          for (const auto& layer_value : expected_graphic_layers) {
            const auto id_value = layer_value.toMap().value(QStringLiteral("id"));
            if (id_value.metaType().id() != QMetaType::QString) {
              expected_graphic_ids_are_valid = false;
              break;
            }
            const auto id = id_value.toString();
            if (id.trimmed().isEmpty() || id != id.trimmed() || seen_graphic_ids.contains(id)) {
              expected_graphic_ids_are_valid = false;
              break;
            }
            seen_graphic_ids.insert(id);
            expected_graphic_ids.append(id_value);
          }
          bool sha256_references_are_valid = false;
          auto resolved_validation = resolve_sha256_references(validation, &sha256_references_are_valid);
          bool row_id_references_are_valid = expected_graphic_ids_are_valid;
          const auto resolve_row_id_references = [&](auto&& self, QVariant& value) -> void {
            if (value.metaType().id() == QMetaType::QVariantMap) {
              auto map = value.toMap();
              if (map.contains(QStringLiteral("rowIdsReference"))) {
                const auto reference = map.value(QStringLiteral("rowIdsReference"));
                if (reference.metaType().id() != QMetaType::Bool || !reference.toBool() ||
                    map.value(QStringLiteral("encoding")).toString() !=
                        QStringLiteral("lossless-columnar-rows-v1") ||
                    map.value(QStringLiteral("identityField")).toString().isEmpty() ||
                    expected_graphic_ids.isEmpty() ||
                    (map.contains(QStringLiteral("rowIds")) &&
                     !exact_string_list_matches(map.value(QStringLiteral("rowIds")), expected_graphic_ids))) {
                  row_id_references_are_valid = false;
                } else {
                  map.remove(QStringLiteral("rowIdsReference"));
                  map.insert(QStringLiteral("rowIds"), expected_graphic_ids);
                }
              }
              for (auto iterator = map.begin(); iterator != map.end(); ++iterator)
                self(self, iterator.value());
              value = map;
            } else if (value.metaType().id() == QMetaType::QVariantList) {
              auto list = value.toList();
              for (auto& entry : list) self(self, entry);
              value = list;
            }
          };
          QVariant resolved_with_row_ids = resolved_validation;
          resolve_row_id_references(resolve_row_id_references, resolved_with_row_ids);
          if (row_id_references_are_valid) resolved_validation = resolved_with_row_ids.toMap();
          bool projection_references_are_valid =
              sha256_references_are_valid && row_id_references_are_valid &&
              expected_graphic_ids_are_valid;
          const auto resolve_id_list_reference = [&](QVariantMap* owner,
                                                      const QString& list_key,
                                                      const QString& reference_key) {
            if (owner == nullptr || !owner->contains(reference_key)) return true;
            const auto reference = owner->value(reference_key);
            if (reference.metaType().id() != QMetaType::Bool || !reference.toBool() ||
                (owner->contains(list_key) &&
                 !exact_string_list_matches(owner->value(list_key), expected_graphic_ids)))
              return false;
            owner->remove(reference_key);
            owner->insert(list_key, expected_graphic_ids);
            return true;
          };
          auto photo_evidence = resolved_validation.value(QStringLiteral("photographicQualityEvidence")).toMap();
          const auto resolve_warning_reference = [&](QVariantMap* owner,
                                                      const QString& warning_key,
                                                      const QString& reference_key,
                                                      const QVariant& source_warnings) {
            if (owner == nullptr || !owner->contains(reference_key)) return true;
            const auto reference = owner->value(reference_key);
            bool source_is_string_list = source_warnings.metaType().id() == QMetaType::QVariantList ||
                source_warnings.metaType().id() == QMetaType::QStringList;
            QVariantList source_values;
            if (source_warnings.metaType().id() == QMetaType::QVariantList)
              source_values = source_warnings.toList();
            else if (source_warnings.metaType().id() == QMetaType::QStringList) {
              for (const auto& warning : source_warnings.toStringList()) source_values.append(warning);
            }
            if (reference.metaType().id() != QMetaType::Bool || !reference.toBool() ||
                !source_is_string_list ||
                std::any_of(source_values.cbegin(), source_values.cend(), [](const QVariant& warning) {
                  return warning.metaType().id() != QMetaType::QString;
                }) ||
                (owner->contains(warning_key) &&
                 !exact_string_list_matches(owner->value(warning_key), source_values)))
              return false;
            owner->remove(reference_key);
            owner->insert(warning_key, source_warnings);
            return true;
          };
          projection_references_are_valid =
              resolve_warning_reference(&photo_evidence,
                  QStringLiteral("introducedCompositeWarnings"),
                  QStringLiteral("introducedCompositeWarningsReference"),
                  photo_evidence.value(QStringLiteral("currentCompositeWarnings"))) &&
              projection_references_are_valid;
          if (resolved_validation.contains(QStringLiteral("photographicQualityEvidence")) ||
              !photo_evidence.isEmpty())
            resolved_validation.insert(QStringLiteral("photographicQualityEvidence"), photo_evidence);
          projection_references_are_valid =
              resolve_warning_reference(&resolved_validation,
                  QStringLiteral("compositeWarnings"), QStringLiteral("compositeWarningsReference"),
                  photo_evidence.value(QStringLiteral("currentCompositeWarnings"))) &&
              projection_references_are_valid;

          auto review_for_references = resolved_validation.value(QStringLiteral("compositionReview")).toMap();
          projection_references_are_valid =
              resolve_id_list_reference(&review_for_references,
                  QStringLiteral("observedLayerOrderIdsTopFirst"),
                  QStringLiteral("observedLayerOrderIdsTopFirstReference")) &&
              projection_references_are_valid;
          auto inspection_for_references = review_for_references.value(QStringLiteral("inspection")).toMap();
          auto inspection_content_for_references =
              inspection_for_references.value(QStringLiteral("structuredContent")).toMap();
          auto display_policy_for_references =
              inspection_content_for_references.value(QStringLiteral("displayTextColorPolicy")).toMap();
          projection_references_are_valid =
              resolve_id_list_reference(&display_policy_for_references,
                  QStringLiteral("eligibleLayerIds"), QStringLiteral("eligibleLayerIdsReference")) &&
              projection_references_are_valid;
          if (inspection_content_for_references.contains(QStringLiteral("displayTextColorPolicy")) ||
              !display_policy_for_references.isEmpty())
            inspection_content_for_references.insert(
                QStringLiteral("displayTextColorPolicy"), display_policy_for_references);
          inspection_for_references.insert(
              QStringLiteral("structuredContent"), inspection_content_for_references);
          review_for_references.insert(QStringLiteral("inspection"), inspection_for_references);
          resolved_validation.insert(QStringLiteral("compositionReview"), review_for_references);

          const auto review = resolved_validation.value(QStringLiteral("compositionReview")).toMap();
          const auto detail = review.value(QStringLiteral("detailProofReport")).toMap();
          bool crops_are_lossless = false;
          const auto crops = decode_referenced_lossless_rows(
              detail.value(QStringLiteral("targetedDetailProofs")), expected_graphic_ids, &crops_are_lossless);
          const auto crop = crops.isEmpty() ? QVariantMap{} : crops.first().toMap();
          const auto bounds = crop.value(QStringLiteral("pixelBounds")).toMap();
          const auto native_review = assessment.value(QStringLiteral("nativeReview")).toMap();
          const auto wants_correction = assessment.value(QStringLiteral("requireProductivePlan")).toBool();
          const auto expected_text = wants_correction
              ? QStringLiteral("CHAIN") : QStringLiteral("APPROVED HEADLINE");
          const auto expected_copy = review.value(QStringLiteral("expectedCopy")).toList();
          const auto observed_copy = review.value(QStringLiteral("observedCopy")).toList();
          const auto missing_copy = review.value(QStringLiteral("missingExpectedCopy")).toList();
          const auto expected_layer_order =
              review.value(QStringLiteral("expectedLayerOrderIdsTopFirst")).toList();
          const auto assessment_missing_copy = native_review.value(QStringLiteral("missingExpectedCopy")).toList();
          const auto observed_reference = review.value(QStringLiteral("observedGraphicLayersReference")).toMap();
          const auto inspection = review.value(QStringLiteral("inspection")).toMap();
          const auto inspection_content = inspection.value(QStringLiteral("structuredContent")).toMap();
          QVariantMap expected_chain_text_layer;
          for (const auto& expected_value : expected_graphic_layers) {
            const auto candidate = expected_value.toMap();
            if (candidate.value(QStringLiteral("id")).toString() == QStringLiteral("chain-text")) {
              expected_chain_text_layer = candidate;
              break;
            }
          }
          const auto reconstructed_typography =
              expected_chain_text_layer.value(QStringLiteral("textTypography")).toMap();
          const auto reconstructed_layout_metrics =
              expected_chain_text_layer.value(QStringLiteral("layoutMetrics")).toMap();
          bool expected_nested_graphic_tables_reconstructed =
              !expected_chain_text_layer.isEmpty() &&
              expected_chain_text_layer.value(QStringLiteral("textTypography")).metaType().id() ==
                  QMetaType::QVariantMap &&
              expected_chain_text_layer.value(QStringLiteral("layoutMetrics")).metaType().id() ==
                  QMetaType::QVariantMap &&
              reconstructed_typography.contains(QStringLiteral("size")) &&
              reconstructed_typography.value(QStringLiteral("size")).toDouble() > 0.0 &&
              !reconstructed_layout_metrics.isEmpty();
          bool inspection_layers_are_lossless = false;
          const auto inspection_layers = decode_referenced_lossless_rows(
              inspection_content.value(QStringLiteral("layers")), expected_graphic_ids,
              &inspection_layers_are_lossless);
          QVariantList geometry_checks;
          bool geometry_checks_losslessly_resolved = false;
          const auto encoded_geometry_checks = review.value(QStringLiteral("geometryChecks")).toMap();
          if (encoded_geometry_checks.value(QStringLiteral("encoding")).toString() ==
              QStringLiteral("lossless-columnar-rows-v1")) {
            bool geometry_rows_are_lossless = false;
            const auto encoded_rows = decode_referenced_lossless_rows(
                encoded_geometry_checks, expected_graphic_ids, &geometry_rows_are_lossless);
            const auto reference_fields = review.value(
                QStringLiteral("geometryChecksLayoutMetricsReferenceFields")).toList();
            geometry_checks_losslessly_resolved = geometry_rows_are_lossless &&
                !encoded_rows.isEmpty() && !reference_fields.isEmpty();
            for (const auto& encoded_row_value : encoded_rows) {
              auto geometry = encoded_row_value.toMap();
              if (!geometry.value(QStringLiteral("layoutMetricsReference")).toBool()) {
                geometry_checks_losslessly_resolved = false;
                continue;
              }
              const auto layer_id = geometry.value(QStringLiteral("layerId")).toString();
              QVariantMap expected_layer;
              for (const auto& expected_value : expected_graphic_layers) {
                const auto candidate = expected_value.toMap();
                if (candidate.value(QStringLiteral("id")).toString() == layer_id) {
                  expected_layer = candidate;
                  break;
                }
              }
              const auto layout_metrics = expected_layer.value(QStringLiteral("layoutMetrics")).toMap();
              geometry.remove(QStringLiteral("layoutMetricsReference"));
              if (expected_layer.isEmpty()) geometry_checks_losslessly_resolved = false;
              for (const auto& field_value : reference_fields) {
                const auto field = field_value.toString();
                if (field.isEmpty() || geometry.contains(field) || !layout_metrics.contains(field)) {
                  geometry_checks_losslessly_resolved = false;
                  continue;
                }
                geometry.insert(field, layout_metrics.value(field));
              }
              geometry_checks.append(geometry);
            }
          } else {
            geometry_checks = review.value(QStringLiteral("geometryChecks")).toList();
            geometry_checks_losslessly_resolved =
                !geometry_checks.isEmpty() &&
                !review.contains(QStringLiteral("geometryChecksLayoutMetricsReferenceFields"));
          }
          const auto proof_image_indices = detail.value(QStringLiteral("detailProofImageIndices")).toList();
          const auto width = detail.value(QStringLiteral("renderedCompositionWidth")).toInt();
          const auto height = detail.value(QStringLiteral("renderedCompositionHeight")).toInt();
          const auto overview_sha = detail.value(QStringLiteral("overviewSha256")).toString();
          const auto recipe_sha = detail.value(QStringLiteral("sourceRecipeSha256")).toString();
          const auto rendered_sha = detail.value(QStringLiteral("renderedCompositionSha256")).toString();
          bool layer_matches_crop = false;
          bool expected_layer_matches_crop = false;
          for (const auto& layer_value : expected_graphic_layers) {
            const auto layer = layer_value.toMap();
            if (layer.value(QStringLiteral("id")).toString() == QStringLiteral("chain-text") &&
                layer.value(QStringLiteral("text")).toString() == crop.value(QStringLiteral("expectedText")).toString() &&
                layer.value(QStringLiteral("text")).toString() == expected_text &&
                layer.value(QStringLiteral("textTypography")).metaType().id() == QMetaType::QVariantMap &&
                layer.value(QStringLiteral("layoutMetrics")).metaType().id() == QMetaType::QVariantMap)
              expected_layer_matches_crop = true;
          }
          bool inspection_layer_matches_reference = false;
          for (const auto& layer_value : inspection_layers) {
            const auto layer = layer_value.toMap();
            const auto exact_fields = layer.value(QStringLiteral("exactFieldMatches")).toList();
            if (layer.value(QStringLiteral("id")).toString() == QStringLiteral("chain-text") &&
                !layer.contains(QStringLiteral("text")) &&
                exact_fields.contains(QVariant(QStringLiteral("text"))) &&
                exact_fields.contains(QVariant(QStringLiteral("textTypography"))) &&
                layer.value(QStringLiteral("expectedGraphicTextLayerReference")).toString() ==
                    QStringLiteral("Expected graphic text layers JSON row id=chain-text"))
              inspection_layer_matches_reference = true;
          }
          layer_matches_crop = expected_layer_matches_crop && inspection_layer_matches_reference;
          bool expected_text_geometry_is_checked = false;
          for (const auto& geometry_value : geometry_checks) {
            const auto geometry = geometry_value.toMap();
            const auto rendered_bounds = geometry.value(QStringLiteral("renderedBounds")).toMap();
            if (geometry.value(QStringLiteral("layerId")).toString() == QStringLiteral("chain-text") &&
                geometry.value(QStringLiteral("available")).toBool() &&
                geometry.value(QStringLiteral("transformAvailable")).toBool() &&
                geometry.value(QStringLiteral("rasterFrameWithinCanvas")).toBool() &&
                geometry.value(QStringLiteral("boundsBasis")).toString() ==
                    QStringLiteral("declared-layer-raster") &&
                rendered_bounds.value(QStringLiteral("width")).toDouble() > 0.0 &&
                rendered_bounds.value(QStringLiteral("height")).toDouble() > 0.0 &&
                geometry.value(QStringLiteral("effectiveTypographySize")).toDouble() > 0.0)
              expected_text_geometry_is_checked = true;
          }
          bool crop_x_available = false;
          bool crop_y_available = false;
          const auto crop_x = bounds.value(QStringLiteral("x")).toInt(&crop_x_available);
          const auto crop_y = bounds.value(QStringLiteral("y")).toInt(&crop_y_available);
          const auto crop_width = bounds.value(QStringLiteral("width")).toInt();
          const auto crop_height = bounds.value(QStringLiteral("height")).toInt();
          const auto evidence_proof_sha = planning_evidence.value(QStringLiteral("proofSha256")).toString();
          const auto evidence_recipe_sha = planning_evidence.value(QStringLiteral("sourceRecipeSha256")).toString();
          const auto assessment_proof_sha = assessment.value(QStringLiteral("sourceProofSha256")).toString();
          const auto assessment_recipe_sha = assessment.value(QStringLiteral("sourceRecipeSha256")).toString();
          const QVariantMap checks{
              {QStringLiteral("criticProjectionReferencesResolveWithoutConflict"),
                  projection_references_are_valid},
              {QStringLiteral("selectedModelReceivedOverviewAndDetail"), image_count == 2},
              {QStringLiteral("assessmentIdentityBoundToPlanningProof"),
                  assessment.value(QStringLiteral("kind")).toString() ==
                      QStringLiteral("current-composition-assessment") &&
                  assessment.value(QStringLiteral("contract")).toString() ==
                      QStringLiteral("agent-current-composition-assessment-v1") &&
                  assessment.value(QStringLiteral("assetId")).toString() == QStringLiteral("asset-1") &&
                  assessment.value(QStringLiteral("textLayerIds")).toList().contains(
                      QVariant(QStringLiteral("chain-text"))) &&
                  planning_evidence.value(QStringLiteral("assetId")).toString() == QStringLiteral("asset-1") &&
                  !planning_evidence.value(QStringLiteral("stage")).toString().isEmpty() &&
                  assessment.value(QStringLiteral("batchRound")).toInt() > 0},
              {QStringLiteral("assessmentProofAndRecipeMatchPlanningProof"),
                  !assessment_proof_sha.isEmpty() && assessment_proof_sha == evidence_proof_sha &&
                  !assessment_recipe_sha.isEmpty() && assessment_recipe_sha == evidence_recipe_sha &&
                  evidence_recipe_sha == planning_evidence.value(QStringLiteral("recipeSha256")).toString() &&
                  evidence_recipe_sha == planning_evidence.value(QStringLiteral("renderRecipeSha256")).toString() &&
                  evidence_recipe_sha == planning_evidence.value(QStringLiteral("taskStateHash")).toString()},
              {QStringLiteral("detailAvailable"), review.value(QStringLiteral("detailProofAvailable")).toBool() &&
                  detail.value(QStringLiteral("available")).toBool()},
              {QStringLiteral("detailContract"), detail.value(QStringLiteral("contract")).toString() ==
                  QStringLiteral("agent-targeted-detail-proof-v1")},
              {QStringLiteral("fullDetailReportRetainedWithoutDuplicateCollections"),
                  !review.contains(QStringLiteral("targetedDetailProofs")) &&
                  !review.contains(QStringLiteral("detailProofImagePaths")) &&
                  crops_are_lossless && crops.size() == 1 &&
                  !detail.contains(QStringLiteral("detailProofImagePaths")) &&
                  review.value(QStringLiteral("targetedDetailProofsReference")).toString() ==
                      QStringLiteral("validationReport.compositionReview.detailProofReport.targetedDetailProofs") &&
                  proof_image_indices.size() == 1 && proof_image_indices.first().toInt() == 1 &&
                  review.value(QStringLiteral("detailProofImageIndicesReference")).toString() ==
                      QStringLiteral("validationReport.compositionReview.detailProofReport.detailProofImageIndices") &&
                  crop.value(QStringLiteral("imageSha256")).toString().size() == 64 &&
                  crop.value(QStringLiteral("sourceOverviewSha256")).toString().size() == 64 &&
                  crop.value(QStringLiteral("sourceRecipeSha256")).toString().size() == 64 &&
                  crop.value(QStringLiteral("sourceRenderedSha256")).toString().size() == 64},
              {QStringLiteral("liveEvidenceReferencesRetainedDetailReport"),
                  !validation.value(QStringLiteral("liveRepairEvidence")).toMap()
                      .contains(QStringLiteral("compositionReview")) &&
                  validation.value(QStringLiteral("liveRepairEvidence")).toMap()
                      .value(QStringLiteral("compositionReviewReference")).toString() ==
                      QStringLiteral("validationReport.compositionReview") &&
                  !validation.value(QStringLiteral("liveRepairEvidence")).toMap()
                      .contains(QStringLiteral("targetedDetailProofs")) &&
                  validation.value(QStringLiteral("liveRepairEvidence")).toMap()
                      .value(QStringLiteral("targetedDetailProofsReference")).toString() ==
                      QStringLiteral("validationReport.compositionReview.detailProofReport.targetedDetailProofs")},
              {QStringLiteral("fullResolutionCurrentComposition"),
                  detail.value(QStringLiteral("renderSource")).toString() ==
                      QStringLiteral("full-resolution-current-composition") && width == 2400 && height == 1600},
              {QStringLiteral("reducedOverviewBoundToPlannerProof"),
                  detail.value(QStringLiteral("overviewWidth")).toInt() < width &&
                  overview_sha.size() == 64 && overview_sha == evidence_proof_sha},
              {QStringLiteral("sourceRecipeBoundToPlannerAssessment"),
                  recipe_sha.size() == 64 && recipe_sha == assessment_recipe_sha &&
                  recipe_sha == evidence_recipe_sha},
              {QStringLiteral("renderedCompositionDigestPresent"), rendered_sha.size() == 64},
              {QStringLiteral("singleAvailableNonOmittedCrop"), crops.size() == 1 &&
                  crop.value(QStringLiteral("available")).toBool() &&
                  !crop.value(QStringLiteral("omitted")).toBool()},
              {QStringLiteral("cropLayerAndObservedTextMatch"),
                  crop.value(QStringLiteral("renderSource")).toString() ==
                      QStringLiteral("full-resolution-current-composition") &&
                  crop.value(QStringLiteral("layerId")).toString() == QStringLiteral("chain-text") &&
                  observed_reference.value(QStringLiteral("path")).toString() ==
                      QStringLiteral("Expected graphic text layers JSON") &&
                  observed_reference.value(QStringLiteral("exactSerializedMatch")).toBool() &&
                  !expected_graphic_layers.isEmpty() && layer_matches_crop},
              {QStringLiteral("nestedExpectedTypographyAndLayoutTablesReconstructed"),
                  expected_nested_graphic_tables_reconstructed},
              {QStringLiteral("nativeInspectionRowsLosslesslyReferenceExpectedGraphicText"),
                  inspection_layers_are_lossless && inspection_layer_matches_reference},
              {QStringLiteral("geometryRowsLosslesslyResolveExpectedLayoutMetrics"),
                  geometry_checks_losslessly_resolved},
              {QStringLiteral("cropImageDigestPresent"), crop.value(QStringLiteral("imageSha256")).toString().size() == 64},
              {QStringLiteral("cropHashesBindOverviewRecipeAndRenderedPixels"),
                  crop.value(QStringLiteral("sourceOverviewSha256")).toString() == overview_sha &&
                  crop.value(QStringLiteral("sourceRecipeSha256")).toString() == recipe_sha &&
                  crop.value(QStringLiteral("sourceRenderedSha256")).toString() == rendered_sha},
              {QStringLiteral("detailIndexMatchesAttachedCrop"), proof_image_indices.size() == 1 &&
                  proof_image_indices.first().toInt() == crop.value(QStringLiteral("attachedImageIndex")).toInt() &&
                  crop.value(QStringLiteral("attachedImageIndex")).toInt() == 1 && image_count == 2},
              {QStringLiteral("cropBoundsInsideFullResolutionSource"), crop_x_available && crop_y_available &&
                  crop_x >= 0 && crop_y >= 0 && crop_width > 0 && crop_height > 0 &&
                  crop_x + crop_width <= width && crop_y + crop_height <= height},
              {QStringLiteral("approvedHeadlineRequirementMatchesCurrentAssessment"),
                  expected_copy.contains(QVariant(QStringLiteral("APPROVED HEADLINE"))) &&
                  (wants_correction
                      ? missing_copy.contains(QVariant(QStringLiteral("APPROVED HEADLINE"))) &&
                          assessment_missing_copy.contains(QVariant(QStringLiteral("APPROVED HEADLINE"))) &&
                          !native_review.value(QStringLiteral("ok")).toBool() &&
                          observed_copy.contains(QVariant(QStringLiteral("CHAIN")))
                      : missing_copy.isEmpty() && assessment_missing_copy.isEmpty() &&
                          native_review.value(QStringLiteral("ok")).toBool() &&
                          observed_copy.contains(QVariant(QStringLiteral("APPROVED HEADLINE"))))},
              {QStringLiteral("approvedLayerOrderStillMatches"),
                  review.value(QStringLiteral("layerOrderCheckAvailable")).toBool() &&
                  expected_layer_order == QVariantList{
                      QStringLiteral("chain-cutout"), QStringLiteral("chain-text")} &&
                  (wants_correction || review.value(QStringLiteral("layerOrderMatches")).toBool())},
              {QStringLiteral("textGeometryIsChecked"), expected_text_geometry_is_checked},
          };
          return checks;
        };
        auto all_assessment_planners_are_bound = !assessment_planner_request_indices.isEmpty() &&
            assessment_planner_request_indices.size() == assessment_planner_evidence.size() &&
            assessment_planner_evidence.size() == assessment_planner_assessments.size() &&
            assessment_planner_assessments.size() == assessment_planner_image_counts.size();
        QList<int> productive_repair_planner_positions;
        QList<int> passing_readback_planner_positions;
        QList<int> matched_critique_request_indices;
        QVariantList repair_turn_evidence_summary;
        int repair_turns_with_bound_critique_detail = 0;
        for (qsizetype planner_position = 0;
             planner_position < assessment_planner_request_indices.size(); ++planner_position) {
          const auto planner_index = assessment_planner_request_indices.at(planner_position);
          const auto evidence = assessment_planner_evidence.at(planner_position);
          const auto assessment = assessment_planner_assessments.at(planner_position);
          const auto wants_correction = assessment.value(QStringLiteral("requireProductivePlan")).toBool();
          if (wants_correction) productive_repair_planner_positions.append(static_cast<int>(planner_position));
          else passing_readback_planner_positions.append(static_cast<int>(planner_position));
          int matched_critique_index = -1;
          QVariantMap failed_checks;
          for (qsizetype critique_position = 0;
               critique_position < proof_critique_request_indices.size(); ++critique_position) {
            const auto critique_index = proof_critique_request_indices.at(critique_position);
            if (critique_index >= planner_index) continue;
            const auto checks = critique_detail_checks(
                proof_critique_validation_reports.value(critique_position), evidence, assessment,
                proof_critique_expected_graphic_layers.value(critique_position),
                proof_critique_image_counts.value(critique_position));
            const auto all_checks_pass = std::all_of(checks.cbegin(), checks.cend(),
                [](const QVariant& check) { return check.toBool(); });
            if (all_checks_pass && critique_index > matched_critique_index) {
              matched_critique_index = critique_index;
            } else if (!all_checks_pass) {
              failed_checks = checks;
            }
          }
          matched_critique_request_indices.append(matched_critique_index);
          all_assessment_planners_are_bound = all_assessment_planners_are_bound && matched_critique_index >= 0;
          if (matched_critique_index >= 0) ++repair_turns_with_bound_critique_detail;
          QStringList failed_check_names;
          if (matched_critique_index < 0) {
            for (auto check = failed_checks.cbegin(); check != failed_checks.cend(); ++check)
              if (!check.value().toBool()) failed_check_names.append(check.key());
          }
          const auto native_review = assessment.value(QStringLiteral("nativeReview")).toMap();
          repair_turn_evidence_summary.append(QVariantMap{
              {QStringLiteral("plannerRequestIndex"), planner_index},
              {QStringLiteral("matchingCritiqueRequestIndex"), matched_critique_index},
              {QStringLiteral("requireProductivePlan"), wants_correction},
              {QStringLiteral("assessmentRound"), assessment.value(QStringLiteral("batchRound"))},
              {QStringLiteral("assetId"), assessment.value(QStringLiteral("assetId"))},
              {QStringLiteral("textLayerIds"), assessment.value(QStringLiteral("textLayerIds"))},
              {QStringLiteral("sourceProofSha256"), assessment.value(QStringLiteral("sourceProofSha256"))},
              {QStringLiteral("sourceRecipeSha256"), assessment.value(QStringLiteral("sourceRecipeSha256"))},
              {QStringLiteral("nativeReviewOk"), native_review.value(QStringLiteral("ok"))},
              {QStringLiteral("missingExpectedCopy"), native_review.value(QStringLiteral("missingExpectedCopy"))},
              {QStringLiteral("fullResolutionCritiqueBound"), matched_critique_index >= 0},
              {QStringLiteral("failedProofBindings"), failed_check_names},
          });
        }
        const auto has_two_critique_turns = proof_critique_request_indices.size() >= 2;
        const auto both_critique_turns_have_overview_and_detail =
            proof_critique_image_counts.size() >= 2 &&
            proof_critique_image_counts.at(0) == 2 &&
            proof_critique_image_counts.at(1) == 2;
        const auto has_productive_repair_turns = productive_repair_planner_positions.size() >= 2;
        const auto has_passing_readback_turn = !passing_readback_planner_positions.isEmpty();
        const auto first_productive_position = productive_repair_planner_positions.isEmpty()
            ? -1 : productive_repair_planner_positions.first();
        const auto second_productive_position = productive_repair_planner_positions.size() < 2
            ? -1 : productive_repair_planner_positions.at(1);
        const auto first_productive_index = first_productive_position < 0
            ? -1 : assessment_planner_request_indices.at(first_productive_position);
        const auto second_productive_index = second_productive_position < 0
            ? -1 : assessment_planner_request_indices.at(second_productive_position);
        const auto first_readback_position = passing_readback_planner_positions.isEmpty()
            ? -1 : passing_readback_planner_positions.first();
        const auto first_readback_index = first_readback_position < 0
            ? -1 : assessment_planner_request_indices.at(first_readback_position);
        const auto first_failed_critique_index = first_productive_position < 0
            ? -1 : matched_critique_request_indices.value(first_productive_position, -1);
        const auto corrected_critique_index = first_readback_position < 0
            ? -1 : matched_critique_request_indices.value(first_readback_position, -1);
        auto replay_receipt_in_report = false;
        auto replay_receipt_round = -1;
        auto corrected_text_receipt_round = -1;
        const auto model_tool_plan_report = mcp_draft_report.value(QStringLiteral("modelToolPlan")).toMap();
        for (const auto& report_value : model_tool_plan_report.value(QStringLiteral("toolReports")).toList()) {
          const auto tool_report = report_value.toMap();
          const auto arguments = tool_report.value(QStringLiteral("arguments")).toMap();
          const auto content = tool_report.value(QStringLiteral("structuredContent")).toMap();
          const auto operation = arguments.value(QStringLiteral("operation")).toString();
          const auto values = arguments.value(QStringLiteral("values")).toMap();
          if (tool_report.value(QStringLiteral("toolName")).toString() ==
                  QStringLiteral("photo.edit_composition") &&
              arguments.value(QStringLiteral("assetId")).toString() == QStringLiteral("asset-1") &&
              operation == QStringLiteral("add_text") && values.value(QStringLiteral("text")).toString() ==
                  QStringLiteral("CHAIN") && !tool_report.value(QStringLiteral("ok")).toBool() &&
              tool_report.value(QStringLiteral("error")).toString().contains(
                  QStringLiteral("This exact mutation already completed")) &&
              content.value(QStringLiteral("recoverableError")).toBool() &&
              content.value(QStringLiteral("mutationNotApplied")).toBool() &&
              content.value(QStringLiteral("replayPrevented")).toBool() &&
              content.value(QStringLiteral("alreadyCompleted")).toBool() &&
              content.value(QStringLiteral("priorReturnedIds")).toMap()
                  .value(QStringLiteral("assetId")).toString() == QStringLiteral("asset-1") &&
              content.value(QStringLiteral("priorReturnedIds")).toMap()
                  .value(QStringLiteral("createdLayerId")).toString() == QStringLiteral("chain-text")) {
            replay_receipt_in_report = true;
            replay_receipt_round = tool_report.value(QStringLiteral("round")).toInt();
          }
          if (tool_report.value(QStringLiteral("toolName")).toString() ==
                  QStringLiteral("photo.edit_composition") &&
              arguments.value(QStringLiteral("assetId")).toString() == QStringLiteral("asset-1") &&
              operation == QStringLiteral("text") &&
              arguments.value(QStringLiteral("layerId")).toString() == QStringLiteral("chain-text") &&
              values.value(QStringLiteral("text")).toString() == QStringLiteral("APPROVED HEADLINE") &&
              tool_report.value(QStringLiteral("ok")).toBool() &&
              content.value(QStringLiteral("changed")).toBool())
            corrected_text_receipt_round = tool_report.value(QStringLiteral("round")).toInt();
        }
        const auto has_replay_receipt = replay_receipt_in_report && replay_receipt_round > 0;
        const auto replay_precedes_correction = has_replay_receipt &&
            corrected_text_receipt_round > replay_receipt_round;
        auto corrected_after_replay = false;
        auto passing_critique_before_completion = false;
        if (has_productive_repair_turns && has_passing_readback_turn) {
          const auto failed_assessment = assessment_planner_assessments.at(first_productive_position);
          const auto completed_assessment = assessment_planner_assessments.at(first_readback_position);
          const auto failed_review = failed_assessment.value(QStringLiteral("nativeReview")).toMap();
          const auto failed_proof = failed_assessment.value(QStringLiteral("sourceProofSha256")).toString();
          const auto failed_recipe = failed_assessment.value(QStringLiteral("sourceRecipeSha256")).toString();
          const auto completed_proof = completed_assessment.value(QStringLiteral("sourceProofSha256")).toString();
          const auto completed_recipe = completed_assessment.value(QStringLiteral("sourceRecipeSha256")).toString();
          const auto corrected_critique_position = proof_critique_request_indices.indexOf(corrected_critique_index);
          const auto corrected_review = corrected_critique_position >= 0
              ? proof_critique_validation_reports.at(corrected_critique_position)
                    .value(QStringLiteral("compositionReview")).toMap()
              : QVariantMap{};
          corrected_after_replay = replay_precedes_correction &&
              first_failed_critique_index >= 0 && first_failed_critique_index < first_productive_index &&
              first_productive_index < second_productive_index &&
              second_productive_index < corrected_critique_index &&
              !failed_proof.isEmpty() && !failed_recipe.isEmpty() &&
              failed_review.value(QStringLiteral("missingExpectedCopy")).toList()
                  .contains(QVariant(QStringLiteral("APPROVED HEADLINE"))) &&
              failed_recipe != completed_recipe && failed_proof != completed_proof &&
              corrected_review.value(QStringLiteral("observedCopy")).toList()
                  .contains(QVariant(QStringLiteral("APPROVED HEADLINE"))) &&
              corrected_review.value(QStringLiteral("missingExpectedCopy")).toList().isEmpty() &&
              composition_repair_text_calls == 1 &&
              native_composition_text_history.contains(QStringLiteral("CHAIN")) &&
              native_composition_text_history.contains(QStringLiteral("APPROVED HEADLINE"));
          passing_critique_before_completion = corrected_after_replay &&
              corrected_critique_index < first_readback_index &&
              corrected_review.value(QStringLiteral("ok")).toBool() &&
              corrected_review.value(QStringLiteral("observedCopy")).toList()
                  .contains(QVariant(QStringLiteral("APPROVED HEADLINE"))) &&
              corrected_review.value(QStringLiteral("missingExpectedCopy")).toList().isEmpty() &&
              completed_proof != failed_proof && completed_recipe != failed_recipe;
        }
        const auto repair_receipt_order_is_valid = has_two_critique_turns &&
            both_critique_turns_have_overview_and_detail && has_replay_receipt &&
            replay_precedes_correction && corrected_after_replay &&
            passing_critique_before_completion && all_assessment_planners_are_bound &&
            has_productive_repair_turns && has_passing_readback_turn;
        const auto crop = crops.isEmpty() ? QVariantMap{} : crops.first().toMap();
        const auto full_resolution_size = crop.value("fullResolutionSourceSize").toSize();
        const auto replay_was_blocked = has_replay_receipt &&
            model_tool_plan_report.value(QStringLiteral("recoveredFailureCount")).toInt() >= 1;
        if (!composition_review.value("ok").toBool() ||
            composition_review.value("missingExpectedCopy").toList().size() != 0 ||
            !composition_review.value("layerOrderMatches").toBool() ||
            !composition_review.value("layerOrderCheckAvailable").toBool() ||
            detail_report.value("renderSource").toString() != QStringLiteral("full-resolution-current-composition") ||
            detail_report.value("renderedCompositionWidth").toInt() != 2400 ||
            detail_report.value("overviewWidth").toInt() >= detail_report.value("renderedCompositionWidth").toInt() ||
            full_resolution_size != QSize(2400, 1600) ||
            crop.value("sourceOverviewSha256").toString() != detail_report.value("overviewSha256").toString() ||
            crop.value("sourceRecipeSha256").toString() != detail_report.value("sourceRecipeSha256").toString() ||
            crop.value("imageSha256").toString().isEmpty() ||
            crop.value("sourceRenderedSha256").toString().isEmpty() ||
            detail_report.value("sourceTaskStateHash").toString().isEmpty() ||
            detail_report.value("sourceSnapshotRecipeSha256").toString().isEmpty() ||
            detail_report.value("sourceSemanticRecipeSha256").toString().isEmpty() ||
            detail_report.value("sourceRenderRecipeSha256").toString().isEmpty() ||
            detail_report.value("sourceRenderRecipeSha256").toString() !=
                detail_report.value("sourceRecipeSha256").toString() ||
            detail_report.value("sourceGraphicLayersSha256").toString().isEmpty() ||
            detail_report.value("sourceGraphicLayersSha256").toString() !=
                detail_report.value("graphicLayersSha256").toString() ||
            !detail_binding_audit.value("workerCheckpointMatches").toBool() ||
            !detail_binding_audit.value("workerSourceRecipeMatches").toBool() ||
            !detail_binding_audit.value("workerRenderRecipeMatches").toBool() ||
            !detail_binding_audit.value("workerLayersMatch").toBool() ||
            !detail_binding_audit.value("workerOverviewMatches").toBool() ||
            !detail_binding_audit.value("afterSnapshotAvailable").toBool() ||
            !detail_binding_audit.value("afterRecipeMatches").toBool() ||
            !detail_binding_audit.value("afterCheckpointMatches").toBool() ||
            !validation.value("modelProofCritique").toMap().value("promptAudit").toMap()
                 .value("metadataEvidenceComplete").toBool() ||
            !all_assessment_planners_are_bound ||
            repair_turns_with_bound_critique_detail != assessment_planner_request_indices.size() ||
            !both_critique_turns_have_overview_and_detail ||
            !repair_receipt_order_is_valid ||
            !replay_was_blocked || !native_composition_text_history.contains(QStringLiteral("CHAIN")) ||
            !native_composition_text_history.contains(QStringLiteral("APPROVED HEADLINE"))) {
          QVariantList proof_request_indices_json;
          for (const auto index : proof_critique_request_indices)
            proof_request_indices_json.append(index);
          QVariantList assessment_planner_indices_json;
          for (const auto index : assessment_planner_request_indices)
            assessment_planner_indices_json.append(index);
          std::cerr << "rendered composition evidence diagnostic=" << QJsonDocument::fromVariant(QVariantMap{
              {"overviewDetailModelTurns", turns_with_overview_and_detail},
              {"proofCritiqueRequestIndices", proof_request_indices_json},
              {"proofCritiqueImageCounts", QVariantList{proof_critique_image_counts.value(0),
                  proof_critique_image_counts.value(1)}},
              {"assessmentPlannerRequestIndices", assessment_planner_indices_json},
              {"proofChecks", QVariantMap{
                  {"reviewPassed", composition_review.value("ok").toBool()},
                  {"exactCopyPassed", composition_review.value("missingExpectedCopy").toList().isEmpty()},
                  {"layerOrderPassed", composition_review.value("layerOrderMatches").toBool()},
                  {"layerOrderAvailable", composition_review.value("layerOrderCheckAvailable").toBool()},
                  {"currentFullResolutionRender", detail_report.value("renderSource").toString() ==
                      QStringLiteral("full-resolution-current-composition")},
                  {"requiredRenderWidth", detail_report.value("renderedCompositionWidth").toInt() == 2400},
                  {"overviewIsReduced", detail_report.value("overviewWidth").toInt() <
                      detail_report.value("renderedCompositionWidth").toInt()},
                  {"detailIsSourceResolution", full_resolution_size == QSize(2400, 1600)},
                  {"detailBoundToOverview", crop.value("sourceOverviewSha256").toString() ==
                      detail_report.value("overviewSha256").toString()},
                  {"detailBoundToRecipe", crop.value("sourceRecipeSha256").toString() ==
                      detail_report.value("sourceRecipeSha256").toString()},
                  {"detailHashPresent", !crop.value("imageSha256").toString().isEmpty()},
                  {"renderedSourceHashPresent", !crop.value("sourceRenderedSha256").toString().isEmpty()},
                  {"checkpointAndRecipeHashesPresent", !detail_report.value("sourceTaskStateHash").toString().isEmpty() &&
                      !detail_report.value("sourceSnapshotRecipeSha256").toString().isEmpty() &&
                      !detail_report.value("sourceSemanticRecipeSha256").toString().isEmpty()},
                  {"renderRecipeMatchesSourceRecipe", detail_report.value("sourceRenderRecipeSha256").toString() ==
                      detail_report.value("sourceRecipeSha256").toString()},
                  {"graphicLayerHashBound", !detail_report.value("sourceGraphicLayersSha256").toString().isEmpty() &&
                      detail_report.value("sourceGraphicLayersSha256").toString() ==
                      detail_report.value("graphicLayersSha256").toString()},
                  {"workerCheckpointMatches", detail_binding_audit.value("workerCheckpointMatches").toBool()},
                  {"workerRecipeMatches", detail_binding_audit.value("workerSourceRecipeMatches").toBool()},
                  {"workerRenderMatches", detail_binding_audit.value("workerRenderRecipeMatches").toBool()},
                  {"workerLayersMatch", detail_binding_audit.value("workerLayersMatch").toBool()},
                  {"workerOverviewMatches", detail_binding_audit.value("workerOverviewMatches").toBool()},
                  {"proofCritiqueMetadataComplete", validation.value("modelProofCritique").toMap()
                      .value("promptAudit").toMap().value("metadataEvidenceComplete").toBool()},
                  {"afterSnapshotAvailable", detail_binding_audit.value("afterSnapshotAvailable").toBool()},
                  {"afterRecipeMatches", detail_binding_audit.value("afterRecipeMatches").toBool()},
                  {"afterCheckpointMatches", detail_binding_audit.value("afterCheckpointMatches").toBool()},
                  {"allAssessmentPlannersBoundToSelectedModelCritique",
                      all_assessment_planners_are_bound},
                  {"assessmentPlannerCount", assessment_planner_request_indices.size()},
                  {"plannersWithBoundFullResolutionCritique",
                      repair_turns_with_bound_critique_detail},
                  {"productiveRepairPlannerCount", productive_repair_planner_positions.size()},
                  {"passingReadbackPlannerCount", passing_readback_planner_positions.size()},
                  {"replayWasBlocked", replay_was_blocked},
                  {"hasReplayReceipt", has_replay_receipt},
                  {"correctedTextFollowsReplay", corrected_after_replay},
                  {"passingCritiqueBeforeCompletion", passing_critique_before_completion},
                  {"replayReceiptRound", replay_receipt_round},
                  {"correctedTextReceiptRound", corrected_text_receipt_round},
                  {"twoDistinctProofCritiques", has_two_critique_turns},
                  {"proofBeforeRepair", first_failed_critique_index >= 0 &&
                      first_failed_critique_index < first_productive_index},
                  {"replayBeforeCorrection", replay_precedes_correction},
                  {"correctionBeforePassingCritique", second_productive_index >= 0 &&
                      second_productive_index < corrected_critique_index},
                  {"passingCritiqueBeforeCompletionPlanner", corrected_critique_index >= 0 &&
                      corrected_critique_index < first_readback_index},
                  {"chainTextMutationObserved", native_composition_text_history.contains(QStringLiteral("CHAIN"))},
                  {"approvedHeadlineMutationObserved", native_composition_text_history.contains(QStringLiteral("APPROVED HEADLINE"))},
              }},
              {"repairReceiptOrderValid", repair_receipt_order_is_valid},
              {"repairTurnEvidence", repair_turn_evidence_summary}})
              .toJson(QJsonDocument::Compact).toStdString() << '\n';
          return fail("Every repair turn must receive the fresh full-resolution composed title proof, with no replay and stable ordered layers");
        }
      }
      if (should_decline_repair) {
        const auto reports = mcp_draft_report.value(QStringLiteral("assetReports")).toList();
        const auto asset_report = reports.isEmpty() ? QVariantMap{} : reports.first().toMap();
        const auto validation = asset_report.value(QStringLiteral("validationReport")).toMap();
        const auto composition_review = validation.value(QStringLiteral("compositionReview")).toMap();
        const auto detail_report = composition_review.value(QStringLiteral("detailProofReport")).toMap();
        const auto critique = validation.value(QStringLiteral("modelProofCritique")).toMap();
        const auto geometry_checks = composition_review.value(QStringLiteral("geometryChecks")).toList();
        auto headline_bounds_pass = false;
        for (const auto& geometry_value : geometry_checks) {
          const auto geometry = geometry_value.toMap();
          const auto rendered_bounds = geometry.value(QStringLiteral("renderedBounds")).toMap();
          if (geometry.value(QStringLiteral("layerId")).toString() == QStringLiteral("chain-text") &&
              geometry.value(QStringLiteral("available")).toBool() &&
              geometry.value(QStringLiteral("transformAvailable")).toBool() &&
              geometry.value(QStringLiteral("rasterFrameWithinCanvas")).toBool() &&
              geometry.value(QStringLiteral("boundsBasis")).toString() ==
                  QStringLiteral("declared-layer-raster") &&
              rendered_bounds.value(QStringLiteral("width")).toDouble() > 0.0 &&
              rendered_bounds.value(QStringLiteral("height")).toDouble() > 0.0) {
            headline_bounds_pass = true;
            break;
          }
        }
        auto safe_decline_retained = false;
        QVariantList repair_decisions;
        for (const auto& repair_value : asset_report.value(QStringLiteral("modelRepairReports")).toList()) {
          const auto repair = repair_value.toMap();
          const auto audit = repair.value(QStringLiteral("modelPromptAudit")).toMap();
          repair_decisions.append(QVariantMap{
              {QStringLiteral("decision"), repair.value(QStringLiteral("decision"))},
              {QStringLiteral("ok"), repair.value(QStringLiteral("ok"))},
              {QStringLiteral("errorCode"), repair.value(QStringLiteral("errorCode"))},
              {QStringLiteral("toolCallPlanRequired"), audit.value(QStringLiteral("toolCallPlanRequired"))},
              {QStringLiteral("toolCallPlanningAllowed"), audit.value(QStringLiteral("toolCallPlanningAllowed"))},
              {QStringLiteral("cannotCompleteSafely"), repair.value(QStringLiteral("cannotCompleteSafely"))},
              {QStringLiteral("errors"), repair.value(QStringLiteral("errors"))},
          });
          if (repair.value(QStringLiteral("decision")).toString() ==
                  QStringLiteral("cannot_complete_safely") &&
              !repair.value(QStringLiteral("ok")).toBool() &&
              audit.value(QStringLiteral("toolCallPlanRequired")).toBool() &&
              audit.value(QStringLiteral("toolCallPlanningAllowed")).toBool())
            safe_decline_retained = true;
        }
        const auto expected_copy = composition_review.value(QStringLiteral("expectedCopy")).toList();
        const auto observed_copy = composition_review.value(QStringLiteral("observedCopy")).toList();
        const auto expected_order = composition_review.value(QStringLiteral("expectedLayerOrderIdsTopFirst")).toList();
        bool decline_turn_has_bound_overview_and_detail = false;
        QFile request_file(composition_repair_request_path);
        if (request_file.open(QIODevice::ReadOnly)) {
          const auto lines = request_file.readAll().split('\n');
          for (const auto& line : lines) {
            const auto request = QJsonDocument::fromJson(line).object();
            const auto messages = request.value(QStringLiteral("messages")).toArray();
            if (messages.size() < 2) continue;
            const auto user = messages.at(1).toObject();
            const auto prompt = user.value(QStringLiteral("content")).toString();
            if (!prompt.contains(QStringLiteral("APPROVED HEADLINE")) ||
                !prompt.contains(QStringLiteral("current validation feedback")) ||
                !prompt.contains(QStringLiteral("smallest replacement patch")) ||
                !prompt.contains(QStringLiteral("Current rendered planning evidence JSON:\n")) ||
                user.value(QStringLiteral("images")).toArray().size() != 2)
              continue;
            const auto header = QStringLiteral("Current rendered planning evidence JSON:\n");
            const auto footer = QStringLiteral(
                "\nThe first attached planning image represents the completed recipe state.");
            const auto evidence_start = prompt.indexOf(header) + header.size();
            const auto evidence_end = prompt.indexOf(footer, evidence_start);
            if (evidence_end <= evidence_start) continue;
            QJsonParseError parse_error;
            const auto evidence_document = QJsonDocument::fromJson(
                prompt.mid(evidence_start, evidence_end - evidence_start).toUtf8(), &parse_error);
            if (parse_error.error != QJsonParseError::NoError || !evidence_document.isObject()) continue;
            const auto evidence = evidence_document.object().toVariantMap();
            const auto turn_review = evidence.value(QStringLiteral("compositionReview")).toMap();
            const auto turn_detail = turn_review.value(QStringLiteral("detailProofReport")).toMap();
            const auto crops = evidence.value(QStringLiteral("targetedDetailProofs")).toList();
            const auto crop = crops.isEmpty() ? QVariantMap{} : crops.first().toMap();
            decline_turn_has_bound_overview_and_detail =
                evidence.value(QStringLiteral("proofSha256")).toString() ==
                    turn_detail.value(QStringLiteral("overviewSha256")).toString() &&
                turn_review.value(QStringLiteral("detailProofAvailable")).toBool() &&
                turn_detail.value(QStringLiteral("available")).toBool() &&
                turn_detail.value(QStringLiteral("renderedCompositionWidth")).toInt() == 2400 &&
                turn_detail.value(QStringLiteral("renderedCompositionHeight")).toInt() == 1600 &&
                turn_detail.value(QStringLiteral("renderSource")).toString() ==
                    QStringLiteral("full-resolution-current-composition") &&
                crops.size() == 1 && crop.value(QStringLiteral("available")).toBool() &&
                !crop.value(QStringLiteral("omitted")).toBool() &&
                crop.value(QStringLiteral("layerId")).toString() == QStringLiteral("chain-text") &&
                crop.value(QStringLiteral("sourceOverviewSha256")).toString() ==
                    turn_detail.value(QStringLiteral("overviewSha256")).toString() &&
                crop.value(QStringLiteral("sourceRecipeSha256")).toString() ==
                    turn_detail.value(QStringLiteral("sourceRecipeSha256")).toString() &&
                !crop.value(QStringLiteral("imageSha256")).toString().isEmpty();
            if (decline_turn_has_bound_overview_and_detail) break;
          }
        }
        const auto procedural_composition_passed = composition_review.value(QStringLiteral("ok")).toBool() &&
            composition_review.value(QStringLiteral("missingExpectedCopy")).toList().isEmpty() &&
            expected_copy.contains(QVariant(QStringLiteral("APPROVED HEADLINE"))) &&
            observed_copy.contains(QVariant(QStringLiteral("APPROVED HEADLINE"))) &&
            composition_review.value(QStringLiteral("layerOrderCheckAvailable")).toBool() &&
            composition_review.value(QStringLiteral("layerOrderMatches")).toBool() &&
            expected_order == QVariantList{QStringLiteral("chain-cutout"), QStringLiteral("chain-text")} &&
            headline_bounds_pass && composition_review.value(QStringLiteral("detailProofAvailable")).toBool() &&
            detail_report.value(QStringLiteral("available")).toBool() &&
            detail_report.value(QStringLiteral("renderSource")).toString() ==
                QStringLiteral("full-resolution-current-composition") &&
            decline_turn_has_bound_overview_and_detail;
        const auto critique_rejected = critique.value(QStringLiteral("ok")).toBool() &&
            !critique.value(QStringLiteral("pass")).toBool() &&
            critique.value(QStringLiteral("needsUserReview")).toBool() &&
            !critique.value(QStringLiteral("issues")).toList().isEmpty();
        const auto retained_task_transaction = agent_edit.taskTransaction();
        QVariantList concise_layer_summary;
        for (const auto& layer_value : chain_layers) {
          const auto layer = layer_value.toMap();
          concise_layer_summary.append(QVariantMap{
              {QStringLiteral("id"), layer.value(QStringLiteral("id"))},
              {QStringLiteral("text"), layer.value(QStringLiteral("text"))},
          });
        }
        const auto report_is_incomplete = !mcp_draft_report.value(QStringLiteral("ok")).toBool() &&
            !agent_edit.draftActive() && !asset_report.isEmpty() &&
            asset_report.value(QStringLiteral("status")).toString() == QStringLiteral("failed") &&
            !asset_report.value(QStringLiteral("acceptedForDraft")).toBool() &&
            !asset_report.value(QStringLiteral("settledPreviewReady")).toBool() &&
            !asset_report.value(QStringLiteral("editLoop")).toMap()
                 .value(QStringLiteral("acceptedForDraft")).toBool() &&
            !task_actions.contains(QStringLiteral("review")) &&
            task_actions == QStringList{QStringLiteral("begin"), QStringLiteral("interrupted")} &&
            retained_task_transaction.value(QStringLiteral("taskId")).toString() == task_request_id &&
            retained_task_transaction.value(QStringLiteral("status")).toString() == QStringLiteral("interrupted") &&
            retained_task_transaction.value(QStringLiteral("revertAvailable")).toBool() &&
            agent_edit.canRevertTask();
        const auto owned_initial_edits_remain_unreplayed =
            chain_operations == QStringList{"select_subject", "add_text", "typography", "copy_selection"} &&
            composition_repair_add_text_calls == 1 && composition_repair_text_calls == 0 &&
            chain_layers.size() == 2 && chain_layers.at(0).toMap().value(QStringLiteral("id")).toString() ==
                QStringLiteral("chain-cutout") &&
            chain_layers.at(1).toMap().value(QStringLiteral("id")).toString() == QStringLiteral("chain-text") &&
            chain_layers.at(1).toMap().value(QStringLiteral("text")).toString() ==
                QStringLiteral("APPROVED HEADLINE") &&
            native_composition_text_history.contains(QStringLiteral("APPROVED HEADLINE"));
        if (!mcp_draft_completed ||
            !procedural_composition_passed || !critique_rejected || !safe_decline_retained ||
            !report_is_incomplete || !owned_initial_edits_remain_unreplayed) {
          std::cerr << "rejected rendered composition critique diagnostic="
                    << QJsonDocument::fromVariant(QVariantMap{
                        {QStringLiteral("assetStatus"), asset_report.value(QStringLiteral("status"))},
                        {QStringLiteral("taskActions"), task_actions},
                        {QStringLiteral("proceduralCompositionPassed"), procedural_composition_passed},
                        {QStringLiteral("declineTurnHasBoundOverviewAndDetail"), decline_turn_has_bound_overview_and_detail},
                        {QStringLiteral("compositionReview"), QVariantMap{
                            {QStringLiteral("ok"), composition_review.value(QStringLiteral("ok"))},
                            {QStringLiteral("missingExpectedCopy"), composition_review.value(QStringLiteral("missingExpectedCopy"))},
                            {QStringLiteral("expectedCopy"), expected_copy},
                            {QStringLiteral("observedCopy"), observed_copy},
                            {QStringLiteral("layerOrderMatches"), composition_review.value(QStringLiteral("layerOrderMatches"))},
                            {QStringLiteral("layerOrderCheckAvailable"), composition_review.value(QStringLiteral("layerOrderCheckAvailable"))},
                            {QStringLiteral("geometryChecks"), geometry_checks},
                            {QStringLiteral("detailProofAvailable"), composition_review.value(QStringLiteral("detailProofAvailable"))},
                            {QStringLiteral("detailProofReport"), QVariantMap{
                                {QStringLiteral("available"), detail_report.value(QStringLiteral("available"))},
                                {QStringLiteral("renderSource"), detail_report.value(QStringLiteral("renderSource"))},
                                {QStringLiteral("renderedCompositionWidth"), detail_report.value(QStringLiteral("renderedCompositionWidth"))},
                                {QStringLiteral("renderedCompositionHeight"), detail_report.value(QStringLiteral("renderedCompositionHeight"))},
                            }},
                        }},
                        {QStringLiteral("critique"), QVariantMap{
                            {QStringLiteral("ok"), critique.value(QStringLiteral("ok"))},
                            {QStringLiteral("pass"), critique.value(QStringLiteral("pass"))},
                            {QStringLiteral("needsUserReview"), critique.value(QStringLiteral("needsUserReview"))},
                            {QStringLiteral("summary"), critique.value(QStringLiteral("summary"))},
                            {QStringLiteral("issues"), critique.value(QStringLiteral("issues"))},
                        }},
                        {QStringLiteral("safeDeclineRetained"), safe_decline_retained},
                        {QStringLiteral("repairDecisions"), repair_decisions},
                        {QStringLiteral("draftOk"), mcp_draft_report.value(QStringLiteral("ok"))},
                        {QStringLiteral("draftActive"), agent_edit.draftActive()},
                        {QStringLiteral("canRevert"), agent_edit.canRevertTask()},
                        {QStringLiteral("taskTransaction"), QVariantMap{
                            {QStringLiteral("taskId"), retained_task_transaction.value(QStringLiteral("taskId"))},
                            {QStringLiteral("status"), retained_task_transaction.value(QStringLiteral("status"))},
                            {QStringLiteral("revertAvailable"), retained_task_transaction.value(QStringLiteral("revertAvailable"))},
                        }},
                        {QStringLiteral("operations"), chain_operations},
                        {QStringLiteral("addTextCalls"), composition_repair_add_text_calls},
                        {QStringLiteral("textCalls"), composition_repair_text_calls},
                        {QStringLiteral("chainLayers"), concise_layer_summary},
                    }).toJson(QJsonDocument::Compact).toStdString() << '\n';
          return fail("A rejected rendered critique must remain incomplete and reversible even when exact copy, bounds and layer metadata pass");
        }
        const auto reverted = agent_edit.revertTask();
        if (!reverted.value(QStringLiteral("ok")).toBool() || agent_edit.canRevertTask() ||
            task_actions != QStringList{QStringLiteral("begin"), QStringLiteral("interrupted"), QStringLiteral("revert")} ||
            catalog_model.assetById(QStringLiteral("asset-1"))
                    .value(QStringLiteral("adjustmentPatchJson")).toString() != task_before_recipe ||
            native_planning_recipe != task_before_recipe)
          return fail("The incomplete rejected composition must Revert its owned native changes to the exact task baseline");
      }
      if (composition_repair_contract) {
        const auto reverted = agent_edit.rollbackDraft();
        if (!reverted.value("ok").toBool() || agent_edit.draftActive() ||
            task_actions != QStringList{"begin", "check_revert", "review", "check_revert", "revert"} ||
            catalog_model.assetById("asset-1").value("adjustmentPatchJson").toString() != task_before_recipe ||
            native_planning_recipe != task_before_recipe)
          return fail("Revert must restore the exact original recipe and composed native task baseline after a corrected draft");
      } else if (composition_repair_cancel_contract) {
        if (catalog_model.assetById("asset-1").value("adjustmentPatchJson").toString() != task_before_recipe ||
            native_planning_recipe != task_before_recipe)
          return fail("Stop during repair must revert the original full-task baseline");
      } else if (composition_repair_conflict_contract) {
        composition_repair_manual_edit_requested = true;
        const auto preserved = agent_edit.rollbackDraft();
        const auto manual_state = photo_editor::desktop::parse_adjustment_patch_json(task_manual_recipe);
        if (preserved.value("ok").toBool() || !agent_edit.draftActive() ||
            task_actions != QStringList{"begin", "check_revert", "review", "check_revert"} ||
            task_manual_recipe.isEmpty() ||
            catalog_model.assetById("asset-1").value("adjustmentPatchJson").toString() != task_manual_recipe ||
            !manual_state.ok() || !manual_state.state.has_value() ||
            manual_state.state->layers.isEmpty() ||
            manual_state.state->layers.last().toMap().value("text").toString() != QStringLiteral("APPROVED HEADLINE"))
          return fail("A conflicting later manual edit must survive Revert with the corrected layer intact");
      } else if (composition_repair_proof_failure_contract) {
        if (!agent_edit.revertTask().value("ok").toBool() ||
            task_actions != QStringList{"begin", "interrupted", "revert"} ||
            catalog_model.assetById("asset-1").value("adjustmentPatchJson").toString() != task_before_recipe ||
            native_planning_recipe != task_before_recipe)
          return fail("Missing full-resolution layout evidence must fail closed and leave the exact task baseline reversible");
      }
      bridge.stop();
      if (should_decline_repair)
        std::cout << "Rejected rendered composition critique stayed incomplete; task-owned edits were not replayed and exact Revert succeeded.\n";
      else
        std::cout << "Native-tool composition correction used fresh full-resolution proof, stable IDs, replay suppression, Stop and exact task Revert checks.\n";
      return EXIT_SUCCESS;
    }
    if (tool_plan_validation_contract) {
      const auto model_tool_plan = mcp_draft_report.value(QStringLiteral("modelToolPlan")).toMap();
      const auto rejection_values = model_tool_plan.value(QStringLiteral("plannerValidationRejections")).toList();
      const auto expected_operations = tool_plan_validation_progress_contract
          ? QStringList{"select_subject", "add_text", "typography", "add_text", "typography",
                "add_text", "typography", "add_text", "typography", "copy_selection"}
          : tool_plan_validation_recovery_contract
          ? QStringList{"select_subject", "add_text", "add_text", "add_text", "typography", "copy_selection"}
          : QStringList{"select_subject", "add_text", "add_text", "add_text"};
      const auto expected_add_text_calls = tool_plan_validation_progress_contract ? 4 : 3;
      const auto expected_success = tool_plan_validation_recovery_contract || tool_plan_validation_progress_contract;
      const auto expected_rejection_count = tool_plan_validation_progress_contract ? 4 : expected_success ? 1 : 2;
      const auto expected_task_actions = expected_success
          ? QStringList{"begin", "check_revert", "review"}
          : QStringList{"begin", "interrupted"};
      const auto report_json = QJsonDocument::fromVariant(mcp_draft_report).toJson(QJsonDocument::Compact);
      const auto first_rejection = rejection_values.isEmpty() ? QVariantMap{} : rejection_values.first().toMap();
      const auto rejected_calls = first_rejection.value(QStringLiteral("toolCallValidation")).toMap()
                                      .value(QStringLiteral("rejectedCalls")).toList();
      const auto rejected_call = rejected_calls.isEmpty() ? QVariantMap{} : rejected_calls.first().toMap();
      const auto original_call = rejected_call.value(QStringLiteral("originalCall")).toMap();
      const auto prompt = mcp_draft_report.value(QStringLiteral("modelPromptAudit")).toMap()
                              .value(QStringLiteral("userPrompt")).toString();
      const auto asset_reports = mcp_draft_report.value(QStringLiteral("assetReports")).toList();
      const auto validation_report = asset_reports.isEmpty()
          ? QVariantMap{}
          : asset_reports.first().toMap().value(QStringLiteral("validationReport")).toMap();
      const auto proof_critique = validation_report.value(QStringLiteral("modelProofCritique")).toMap();
      const auto proof_prompt_audit = proof_critique.value(QStringLiteral("promptAudit")).toMap();
      const auto proof_prompt = proof_prompt_audit.value(QStringLiteral("userPrompt")).toString();
      const auto compact_context_components = [](const QVariantMap& audit) {
        const auto components = audit.value(QStringLiteral("promptComponents")).toMap();
        QVariantMap compact;
        const QStringList keys{
            QStringLiteral("systemPromptUtf8Bytes"),
            QStringLiteral("userPromptUtf8Bytes"),
            QStringLiteral("aggregateTextUtf8Bytes"),
            QStringLiteral("attachedImageCount"),
            QStringLiteral("primaryImageTransmittedBytes"),
            QStringLiteral("additionalProofImageCount"),
            QStringLiteral("additionalProofImageTransmittedBytes"),
            QStringLiteral("maxOutputTokens"),
            QStringLiteral("responseSchemaUtf8Bytes"),
            QStringLiteral("contextTokensRequested"),
            QStringLiteral("contextTokenLimit"),
            QStringLiteral("estimatedAggregateTextTokens"),
            QStringLiteral("estimatedAsciiUtf8Bytes"),
            QStringLiteral("estimatedNonAsciiUtf8Bytes"),
            QStringLiteral("estimatedNonAsciiCodePointCount"),
            QStringLiteral("estimatedContextTokensBeforeCap"),
        };
        for (const auto& key : keys) {
          if (components.contains(key)) compact.insert(key, components.value(key));
        }
        return compact;
      };
      const bool proof_color_evidence_preserved =
          proof_prompt.contains(QStringLiteral("renderColorDomain")) &&
          proof_prompt.contains(QStringLiteral("displayReferredTextEligibility")) &&
          proof_prompt.contains(QStringLiteral("displayReferredTextEligibilityReason")) &&
          proof_prompt.contains(QStringLiteral("displayReferredTextPolicyVersion")) &&
          proof_prompt.contains(QStringLiteral("display-referred-text-suffix-v1")) &&
          proof_prompt.contains(QStringLiteral("displayTextColorPolicy")) &&
          proof_prompt.contains(QStringLiteral("eligibleLayerIds")) &&
          proof_prompt.contains(QStringLiteral("validation-text-1")) &&
          proof_prompt.contains(QStringLiteral("eligible SDR top text layers only"));
      if (!mcp_draft_completed || mcp_draft_completion_count != 1 || agent_edit.running() ||
          agent_edit.draftRequestPending() || agent_edit.draftActive() != expected_success ||
          mcp_draft_report.value(QStringLiteral("ok")).toBool() != expected_success ||
          chain_operations != expected_operations || validation_recovery_add_text_calls != expected_add_text_calls ||
          chain_attempted_operations.contains(QStringLiteral("opacity")) ||
          task_actions != expected_task_actions || rejection_values.size() != expected_rejection_count ||
          rejected_call.value(QStringLiteral("toolName")).toString() != QStringLiteral("photo.edit_composition") ||
          original_call.value(QStringLiteral("arguments")).toMap().value(QStringLiteral("operation")).toString() !=
              QStringLiteral("typography") ||
          original_call.value(QStringLiteral("arguments")).toMap().value(QStringLiteral("layerId")).toString() !=
              QStringLiteral("validation-text-1") ||
          !original_call.value(QStringLiteral("arguments")).toMap().value(QStringLiteral("values")).toMap()
               .contains(QStringLiteral("x")) || rejected_call.value(QStringLiteral("inputSchema")).toMap().isEmpty() ||
          !first_rejection.value(QStringLiteral("wholeGeneratedPlanWithheld")).toBool() ||
          first_rejection.value(QStringLiteral("toolCallValidation")).toMap()
              .value(QStringLiteral("unexecutedCalls")).toList().size() != 2 ||
          first_rejection.value(QStringLiteral("toolCallValidation")).toMap()
              .value(QStringLiteral("unexecutedCalls")).toList().first().toMap()
              .value(QStringLiteral("arguments")).toMap().value(QStringLiteral("operation")).toString() !=
              QStringLiteral("opacity") ||
          model_tool_plan.value(QStringLiteral("validationRecoveryTurnCount")).toInt() != 1 ||
          model_tool_plan.value(QStringLiteral("recoveredValidationFailureCount")).toInt() !=
              (expected_success ? expected_rejection_count : 0) ||
          (expected_success && (!proof_critique.value(QStringLiteral("ok")).toBool() ||
              !proof_critique.value(QStringLiteral("pass")).toBool() ||
              !proof_prompt_audit.value(QStringLiteral("metadataEvidenceComplete")).toBool() ||
              proof_prompt.contains(QStringLiteral("evidenceOmittedForSize")) ||
              !proof_color_evidence_preserved)) ||
          (expected_success && (!model_tool_plan.value(QStringLiteral("ok")).toBool() ||
              !prompt.contains(QStringLiteral("agent-tool-call-validation-recovery-v1")) ||
              !prompt.contains(QStringLiteral("validation-text-1")) ||
              !prompt.contains(QStringLiteral("createdLayerId")) ||
              !prompt.contains(QStringLiteral("unexecutedCalls")) ||
              !prompt.contains(QStringLiteral("opacity")))) ||
          (!expected_success && (model_tool_plan.value(QStringLiteral("ok")).toBool() ||
              model_tool_plan.value(QStringLiteral("errorCode")).toString() !=
              QStringLiteral("repeated_tool_plan_validation") ||
              !rejection_values.last().toMap().value(QStringLiteral("repeated")).toBool() ||
              !report_json.contains("AUTUMN OPEN HOUSE")))) {
        std::cerr << "tool-call validation recovery diagnostic=" << QJsonDocument::fromVariant(QVariantMap{
            {QStringLiteral("errors"), mcp_draft_report.value(QStringLiteral("errors"))},
            {QStringLiteral("operations"), chain_operations},
            {QStringLiteral("attemptedOperations"), chain_attempted_operations},
            {QStringLiteral("rejectionCount"), rejection_values.size()},
            {QStringLiteral("recoveryTurnCount"), model_tool_plan.value(QStringLiteral("validationRecoveryTurnCount"))},
            {QStringLiteral("proofCritiqueOk"), proof_critique.value(QStringLiteral("ok"))},
            {QStringLiteral("proofCritiquePass"), proof_critique.value(QStringLiteral("pass"))},
            {QStringLiteral("proofCritiqueError"), proof_critique.value(QStringLiteral("error")).toString().left(300)},
            {QStringLiteral("proofPromptComponents"), compact_context_components(proof_prompt_audit)},
            {QStringLiteral("plannerPromptComponents"), compact_context_components(
                mcp_draft_report.value(QStringLiteral("modelPromptAudit")).toMap())},
            {QStringLiteral("repairPlannerPromptComponents"), compact_context_components(
                model_tool_plan.value(QStringLiteral("promptAudit")).toMap())},
            {QStringLiteral("proofMetadataEvidenceComplete"), proof_prompt_audit.value(QStringLiteral("metadataEvidenceComplete"))},
            {QStringLiteral("proofColorEvidencePreserved"), proof_color_evidence_preserved},
            {QStringLiteral("expectedSuccess"), expected_success},
        }).toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("An invalid tool plan must never execute; correction must receive exact errors/current schema and accepted layer receipts, while identical invalid plans stop safely");
      }
      if (expected_success) {
        const auto expected_layer_count = tool_plan_validation_progress_contract ? 5 : 4;
        QStringList observed_layer_ids;
        for (const auto& layer_value : chain_layers)
          observed_layer_ids.append(layer_value.toMap().value(QStringLiteral("id")).toString());
        const bool layers_retained = observed_layer_ids.size() == expected_layer_count &&
            observed_layer_ids.value(0) == QStringLiteral("chain-cutout") &&
            observed_layer_ids.value(1) == QStringLiteral("validation-text-1") &&
            observed_layer_ids.value(2) == QStringLiteral("validation-text-2") &&
            observed_layer_ids.value(3) == QStringLiteral("validation-text-3") &&
            (!tool_plan_validation_progress_contract ||
             observed_layer_ids.value(4) == QStringLiteral("validation-text-4"));
        const auto rollback = agent_edit.rollbackDraft();
        const bool exact_revert = rollback.value(QStringLiteral("ok")).toBool() &&
            task_actions == QStringList{"begin", "check_revert", "review", "check_revert", "revert"} &&
            catalog_model.assetById(QStringLiteral("asset-1")).value(QStringLiteral("adjustmentPatchJson")).toString() ==
                task_before_recipe;
        if (!layers_retained || !exact_revert) {
          std::cerr << "tool-call validation retention/revert diagnostic="
                    << QJsonDocument::fromVariant(QVariantMap{
                        {QStringLiteral("expectedLayerCount"), expected_layer_count},
                        {QStringLiteral("observedLayerIds"), observed_layer_ids},
                        {QStringLiteral("layersRetained"), layers_retained},
                        {QStringLiteral("rollbackOk"), rollback.value(QStringLiteral("ok"))},
                        {QStringLiteral("rollbackErrors"), rollback.value(QStringLiteral("errors"))},
                        {QStringLiteral("taskActions"), task_actions},
                        {QStringLiteral("catalogRecipeRestored"),
                         catalog_model.assetById(QStringLiteral("asset-1"))
                             .value(QStringLiteral("adjustmentPatchJson")).toString() == task_before_recipe},
                        {QStringLiteral("nativeRecipeRestored"), native_planning_recipe == task_before_recipe},
                    }).toJson(QJsonDocument::Compact).toStdString() << '\n';
          return fail("Corrected execution must retain all three accepted text layers and keep exact whole-task Revert");
        }
      } else if (!agent_edit.revertTask().value(QStringLiteral("ok")).toBool() ||
                 task_actions != QStringList{"begin", "interrupted", "revert"} ||
                 catalog_model.assetById(QStringLiteral("asset-1")).value(QStringLiteral("adjustmentPatchJson")).toString() !=
                     task_before_recipe) {
        return fail("Repeated invalid plans must leave accepted layers reversible without dispatching rejected edits");
      }
      bridge.stop();
      std::cout << "Planner validation rejection resumed from exact schemas and receipts; duplicate invalid plans stopped with safe Revert.\n";
      return EXIT_SUCCESS;
    }
    if (tool_correction_contract) {
      const auto tool_plan = mcp_draft_report.value("modelToolPlan").toMap();
      const auto json = QJsonDocument::fromVariant(mcp_draft_report).toJson(QJsonDocument::Compact);
      const auto should_complete = !tool_correction_repeat_contract && !tool_correction_cancel_contract && !tool_correction_unresolved_contract;
      const auto expected_operations = should_complete ? QStringList{"select_subject", "add_text", "typography", "copy_selection"}
          : QStringList{"select_subject", "add_text"};
      if (!mcp_draft_completed || mcp_draft_completion_count != 1 || agent_edit.running() || agent_edit.draftRequestPending() ||
          mcp_draft_report.value("ok").toBool() != should_complete || agent_edit.draftActive() != should_complete ||
          chain_operations != expected_operations || withheld_correction_calls != 0 || rejected_correction_calls < 1 ||
          accepted_correction_prefix_calls != (tool_correction_repeat_contract ? 0 : 1) ||
          (should_complete && (tool_plan.value("recoveredFailureCount").toInt() != 1 || !json.contains("mutationNotApplied"))) ||
          (tool_correction_repeat_contract && !json.contains("repeated_tool_results"))) {
        std::cerr << "correction diagnostic=" << json.toStdString() << '\n';
        return fail("Correction must preserve applied operations, withhold the tail, avoid replay, and stop canceled/repeated/unresolved errors");
      }
      if (should_complete) {
        const auto prompt = mcp_draft_report.value("modelPromptAudit").toMap().value("userPrompt").toString();
        if (!prompt.contains("recoverableError") || !prompt.contains("missing-layer") ||
            !agent_edit.rollbackDraft().value("ok").toBool() ||
            task_actions != QStringList{"begin", "check_revert", "review", "check_revert", "revert"})
          return fail("Corrected task must retain failure evidence and whole-task reversal");
      } else if (tool_correction_cancel_contract) {
        if (task_actions != QStringList{"begin", "cancel", "revert"}) return fail("Correction remains cancelable and restores task baseline");
      } else if (!agent_edit.revertTask().value("ok").toBool() || task_actions != QStringList{"begin", "interrupted", "revert"}) {
        return fail("An unresolved correction remains wholly revertable");
      }
      bridge.stop();
      std::cout << "Tool correction preserved completed mutations and cancellation/reversal without replay.\n";
      return EXIT_SUCCESS;
    }
    if (long_task_cancel_contract || repeated_tool_results_contract) {
      const auto json = QJsonDocument::fromVariant(mcp_draft_report).toJson(QJsonDocument::Compact);
      const auto expected_actions = long_task_cancel_contract ? QStringList{"begin", "cancel", "revert"}
          : QStringList{"begin", "interrupted"};
      if (!mcp_draft_completed || mcp_draft_completion_count != 1 || mcp_draft_report.value("ok").toBool() ||
          agent_edit.running() || agent_edit.draftRequestPending() || agent_edit.draftActive() ||
          task_actions != expected_actions ||
          (long_task_cancel_contract && (long_task_updates != 9 || !json.contains("fixture cancel after long task steps"))) ||
          (repeated_tool_results_contract && (!chain_operations.isEmpty() || !json.contains("repeated_tool_results")))) {
        std::cerr << "long task stop diagnostic=" << json.toStdString() << '\n';
        return fail("Long tasks must Stop after many steps and repeated results must finish once without further mutations");
      }
      if (repeated_tool_results_contract && !agent_edit.revertTask().value("ok").toBool())
        return fail("An unproductive task remains reversible");
      bridge.stop();
      return EXIT_SUCCESS;
    }
    if (composition_error_contract || composition_cancel_contract || composition_failed_call_contract) {
      const auto report_json = QJsonDocument::fromVariant(mcp_draft_report).toJson(QJsonDocument::Compact);
      const auto expected_error = composition_error_contract ? QByteArrayLiteral("fixture cutout materialization failed") :
          composition_cancel_contract ? QByteArrayLiteral("fixture cancel while pixels pending") : QByteArrayLiteral("fixture cutout command rejected");
      if (!mcp_draft_completed || mcp_draft_completion_count != 1 || mcp_draft_report.value("ok").toBool() ||
          agent_edit.running() || agent_edit.draftRequestPending() || agent_edit.draftActive() ||
          chain_operations != QStringList{"select_subject", "add_text", "typography", "copy_selection"} ||
          !report_json.contains(expected_error) ||
          (composition_wait_contract && (composition_poll_count < 1 || busy_heartbeat_count < 3))) {
        std::cerr << "composition failure diagnostic=" << QJsonDocument::fromVariant(QVariantMap{
            {"errors", mcp_draft_report.value("errors")}, {"operations", chain_operations},
            {"completed", mcp_draft_completed}, {"completionCount", mcp_draft_completion_count},
            {"running", agent_edit.running()}, {"pending", agent_edit.draftRequestPending()},
            {"draftActive", agent_edit.draftActive()}, {"pollCount", composition_poll_count},
            {"heartbeatCount", busy_heartbeat_count}}).toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("Pixel job failure, cancellation or tool rejection must finish once and withhold remaining mutations and draft publication");
      }
      if (task_contract) {
        const auto expected_actions = task_cancel_contract ? QStringList{"begin", "cancel", "revert"} : QStringList{"begin", "interrupted"};
        if (task_actions != expected_actions) return fail("Task failure and Stop must transition the original task exactly once");
        if (task_failure_contract && (!agent_edit.revertTask().value("ok").toBool() || task_actions.last() != QStringLiteral("revert")))
          return fail("Partial failed composition must retain its whole-task revert control");
      }
      bridge.stop();
      std::cout << "Composition failure stopped the remaining calls without replay or draft publication.\n";
      return EXIT_SUCCESS;
    }
    auto saw_mcp_discovery_tool_call = false;
    auto saw_mcp_live_status_call = false;
    auto saw_mcp_inspect_composition_call = false;
    auto saw_mcp_style_rulebook_call = false;
    auto saw_mcp_recipe_schema_call = false;
    auto model_live_status_call_count = 0;
    auto model_inspect_call_count = 0;
    auto model_recipe_schema_call_count = 0;
    for (const auto& tool_call_value : mcp_draft_report.value(QStringLiteral("toolCalls")).toList()) {
      const auto tool_call_name = tool_call_value.toMap().value(QStringLiteral("name")).toString();
      if (tool_call_name == QStringLiteral("mcp.tools/list")) {
        saw_mcp_discovery_tool_call = true;
      } else if (tool_call_name == QStringLiteral("mcp.tools/call photo.live_status")) {
        saw_mcp_live_status_call = true;
      } else if (tool_call_name == QStringLiteral("mcp.tools/call photo.inspect_composition")) {
        saw_mcp_inspect_composition_call = true;
      } else if (tool_call_name == QStringLiteral("mcp.tools/call photo.get_style_rulebook")) {
        saw_mcp_style_rulebook_call = true;
      } else if (tool_call_name == QStringLiteral("mcp.tools/call photo.get_recipe_patch_schema")) {
        saw_mcp_recipe_schema_call = true;
      } else if (tool_call_name == QStringLiteral("mcp.model_tool_call photo.live_status")) {
        ++model_live_status_call_count;
      } else if (tool_call_name == QStringLiteral("mcp.model_tool_call photo.inspect_composition")) {
        ++model_inspect_call_count;
      } else if (tool_call_name == QStringLiteral("mcp.model_tool_call photo.get_recipe_patch_schema")) {
        ++model_recipe_schema_call_count;
      }
    }
    const auto mcp_observation = mcp_draft_report.value(QStringLiteral("mcpObservation")).toMap();
    QVariantMap composition_observation;
    QSet<QString> observed_tool_names;
    for (const auto& report_value : mcp_observation.value(QStringLiteral("toolReports")).toList()) {
      const auto report = report_value.toMap();
      observed_tool_names.insert(report.value(QStringLiteral("toolName")).toString());
      if (report.value(QStringLiteral("toolName")).toString() ==
          QStringLiteral("photo.inspect_composition")) {
        composition_observation = report;
      }
    }
    const auto observed_composition =
        composition_observation.value(QStringLiteral("structuredContent")).toMap();
    const auto observed_composition_arguments =
        composition_observation.value(QStringLiteral("arguments")).toMap();
    const auto observation_context_reused =
        mcp_observation.value(QStringLiteral("requestContextReused")).toBool();
    const auto observation_calls_are_audited = observation_context_reused ||
        (saw_mcp_inspect_composition_call && saw_mcp_live_status_call &&
         saw_mcp_style_rulebook_call && saw_mcp_recipe_schema_call);
    const auto mcp_discovery = mcp_draft_report.value(QStringLiteral("mcpDiscovery")).toMap();
    const auto model_tool_plan = mcp_draft_report.value(QStringLiteral("modelToolPlan")).toMap();
    const auto draft_mcp_tool_budget =
        mcp_draft_report.value(QStringLiteral("mcpToolBudget")).toMap();
    const auto model_tool_plan_budget =
        model_tool_plan.value(QStringLiteral("toolBudget")).toMap();
    const auto planning_continuation =
        mcp_draft_report.value(QStringLiteral("planningContinuation")).toMap();
    const auto completed_planning_stages =
        planning_continuation.value(QStringLiteral("completedStages"))
            .toStringList();
    QStringList expected_model_tool_stages{QStringLiteral("primary-plan")};
    for (int round = 1; round <= expected_tool_rounds; ++round)
      expected_model_tool_stages.append(round == 1 ? QStringLiteral("model-tool-follow-up") :
          QStringLiteral("model-tool-follow-up-%1").arg(round));
    const auto completed_repair_stages =
        completed_planning_stages.mid(expected_model_tool_stages.size());
    const auto is_valid_repair_stage = [](const QString& stage) {
      static const QString prefix = QStringLiteral("validation-repair:asset-1:");
      if (!stage.startsWith(prefix)) return false;
      const auto parts = stage.mid(prefix.size()).split(QLatin1Char(':'));
      auto iteration_ok = false;
      const auto iteration = parts.value(0).toInt(&iteration_ok);
      if (!iteration_ok || iteration < 0) return false;
      if (parts.size() == 1) return true;
      if (parts.size() != 3 || parts.at(1) != QStringLiteral("tool-follow-up")) return false;
      auto follow_up_ok = false;
      const auto follow_up = parts.at(2).toInt(&follow_up_ok);
      return follow_up_ok && follow_up > 0;
    };
    QSet<QString> unique_repair_stages;
    auto valid_repair_stages = true;
    for (const auto& stage : completed_repair_stages) {
      valid_repair_stages = valid_repair_stages && is_valid_repair_stage(stage) &&
          !unique_repair_stages.contains(stage);
      unique_repair_stages.insert(stage);
    }
    valid_repair_stages = valid_repair_stages &&
        (completed_repair_stages.isEmpty() ||
         completed_repair_stages.first() == QStringLiteral("validation-repair:asset-1:0"));
    if (!mcp_draft_start.value(QStringLiteral("accepted")).toBool() ||
        mcp_draft_start_duration_ms > 50 || !mcp_draft_completed ||
        mcp_draft_completion_count != 1 ||
        !mcp_draft_report.value(QStringLiteral("ok")).toBool() ||
        planning_continuation.value(QStringLiteral("contract")).toString() !=
            QStringLiteral("agent-request-owned-planning-continuation-v2") ||
        planning_continuation
            .value(QStringLiteral("explicitEventPumpLoop"))
            .toBool() ||
        completed_planning_stages.size() < expected_model_tool_stages.size() ||
        completed_planning_stages.mid(0, expected_model_tool_stages.size()) !=
            expected_model_tool_stages ||
        !valid_repair_stages ||
        planning_continuation.value(QStringLiteral("stageDispatchCount"))
                .toInt() != completed_planning_stages.size() ||
        mcp_draft_report
                .value(QStringLiteral("planningUiEventPumpCount"))
                .toInt() != 0 ||
        !mcp_draft_report.value(QStringLiteral("mcpAttached")).toBool() ||
        mcp_discovery.value(QStringLiteral("mode")).toString() != QStringLiteral("mcp-readiness") ||
        !mcp_discovery.value(QStringLiteral("ready")).toBool() ||
        mcp_discovery.value(QStringLiteral("attemptCount")).toInt() < 1 ||
        mcp_discovery.value(QStringLiteral("recoveryPolicy")).toString() !=
            QStringLiteral("retry-discovery-before-agent-planning") ||
        !mcp_observation.value(QStringLiteral("ok")).toBool() ||
        !model_tool_plan.value(QStringLiteral("ok")).toBool() ||
        !mcp_draft_report.value(QStringLiteral("modelPromptAudit")).toMap()
             .value(QStringLiteral("globalChangePolicyCorrection")).toMap().isEmpty() ||
        draft_mcp_tool_budget.value(QStringLiteral("maxModelToolCallsPerTurn")).toInt() != 8 ||
        !draft_mcp_tool_budget.value(QStringLiteral("hardLimit")).toBool() ||
        model_tool_plan_budget.value(QStringLiteral("maxModelToolCallsPerTurn")).toInt() != 8 ||
        model_tool_plan_budget.value(QStringLiteral("requestedToolCallCount")).toInt() != expected_tool_calls ||
        model_tool_plan_budget.value(QStringLiteral("roundCount")).toInt() != expected_tool_rounds ||
        model_tool_plan.value(QStringLiteral("successCount")).toInt() != expected_tool_calls ||
        model_tool_plan.value(QStringLiteral("attemptedCount")).toInt() != expected_tool_calls ||
        model_tool_plan.value(QStringLiteral("requestedToolCallCount")).toInt() != expected_tool_calls ||
        model_tool_plan.value(QStringLiteral("rounds")).toList().size() != expected_tool_rounds ||
        model_tool_plan.value(QStringLiteral("allowedTools")).toStringList().contains(QStringLiteral("photo.commit_agent_draft")) ||
        mcp_observation.value(QStringLiteral("successCount")).toInt() != 4 ||
        mcp_observation.value(QStringLiteral("toolReports")).toList().size() != 4 ||
        observed_tool_names != QSet<QString>{QStringLiteral("photo.live_status"),
            QStringLiteral("photo.inspect_composition"), QStringLiteral("photo.get_style_rulebook"),
            QStringLiteral("photo.get_recipe_patch_schema")} ||
        !composition_observation.value(QStringLiteral("ok")).toBool() ||
        observed_composition_arguments.value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-1") ||
        observed_composition.value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-1") ||
        observed_composition.value(QStringLiteral("width")).toInt() != 640 ||
        observed_composition.value(QStringLiteral("height")).toInt() != 480 ||
        mcp_draft_report.value(QStringLiteral("mcpToolPhase")).toString() != QStringLiteral("sample") ||
        mcp_draft_report.value(QStringLiteral("mcpAllowedToolNames"))
            .toStringList()
            .contains(QStringLiteral("photo.commit_agent_draft")) ||
        mcp_draft_report.value(QStringLiteral("mcpAvailableToolCount")).toInt() <= 0 ||
        mcp_draft_report.value(QStringLiteral("mcpToolCount")).toInt() < 12 ||
        !mcp_draft_report.value(QStringLiteral("mcpToolNames"))
             .toStringList()
             .contains(QStringLiteral("photo.live_status")) ||
        !saw_mcp_discovery_tool_call ||
        !observation_calls_are_audited ||
        model_live_status_call_count != (inspection_routing_contract || composition_chain_contract ? 0 : 1) ||
        model_inspect_call_count != (composition_chain_contract ? 2 : inspection_routing_contract ? 1 : 0) ||
        model_recipe_schema_call_count != (composition_chain_contract ? 0 : 1) || agent_edit.running() ||
        agent_edit.draftRequestPending() ||
        !agent_edit.activeDraftRequestId().isEmpty()) {
      std::cerr << "multi-round model draft diagnostic="
                << QJsonDocument::fromVariant(QVariantMap{
                       {QStringLiteral("errors"), mcp_draft_report.value(QStringLiteral("errors"))},
                       {QStringLiteral("globalChangePolicyCorrection"), mcp_draft_report.value(QStringLiteral("modelPromptAudit")).toMap().value(QStringLiteral("globalChangePolicyCorrection"))},
                       {QStringLiteral("responseFieldNames"), mcp_draft_report.value(QStringLiteral("modelPromptAudit")).toMap().value(QStringLiteral("responseFieldNames"))},
                       {QStringLiteral("responseDecision"), mcp_draft_report.value(QStringLiteral("modelPromptAudit")).toMap().value(QStringLiteral("responseDecision"))},
                       {QStringLiteral("toolBudget"), model_tool_plan_budget},
                       {"successCount", model_tool_plan.value("successCount")},
                       {"attemptedCount", model_tool_plan.value("attemptedCount")},
                       {"requestedCount", model_tool_plan.value("requestedToolCallCount")},
                       {"modelInspectCount", model_inspect_call_count},
                       {"startMs", mcp_draft_start_duration_ms},
                       {"running", agent_edit.running()}, {"pending", agent_edit.draftRequestPending()},
                       {"controllerRequestId", agent_edit.activeDraftRequestId()},
                       {"modelPlanOk", model_tool_plan.value("ok")},
                       {"ok", mcp_draft_report.value("ok")}, {"accepted", mcp_draft_start.value("accepted")},
                       {"completed", mcp_draft_completed}, {"completionCount", mcp_draft_completion_count},
                       {"planningUiPumps", mcp_draft_report.value("planningUiEventPumpCount")},
                       {"attached", mcp_draft_report.value("mcpAttached")}, {"discoveryReady", mcp_discovery.value("ready")},
                       {"discoveryMode", mcp_discovery.value("mode")}, {"discoveryAttempts", mcp_discovery.value("attemptCount")},
                       {"discoveryPolicy", mcp_discovery.value("recoveryPolicy")},
                       {"observationOk", mcp_observation.value("ok")}, {"observationCount", mcp_observation.value("successCount")},
                       {"observationReused", observation_context_reused},
                       {"observationReports", mcp_observation.value("toolReports").toList().size()},
                       {"observedToolNames", observed_tool_names.values()},
                       {"observationCallsAreAudited", observation_calls_are_audited},
                       {"mcpAllowedTools", mcp_draft_report.value("mcpAllowedToolNames")},
                       {"modelAllowedTools", model_tool_plan.value("allowedTools")},
                       {"mcpToolNames", mcp_draft_report.value("mcpToolNames")},
                       {"completedStages", completed_planning_stages},
                       {"validRepairStages", valid_repair_stages},
                       {"stageDispatchCount", planning_continuation.value("stageDispatchCount")},
                       {"planningContract", planning_continuation.value("contract")},
                       {"compositionObservation", composition_observation},
                       {"draftBudget", draft_mcp_tool_budget}, {"phase", mcp_draft_report.value("mcpToolPhase")},
                       {"availableToolCount", mcp_draft_report.value("mcpAvailableToolCount")},
                       {"toolCount", mcp_draft_report.value("mcpToolCount")},
                       {"sawDiscovery", saw_mcp_discovery_tool_call}, {"sawStatus", saw_mcp_live_status_call},
                       {"sawInspect", saw_mcp_inspect_composition_call},
                       {"sawRulebook", saw_mcp_style_rulebook_call}, {"sawRecipe", saw_mcp_recipe_schema_call},
                       {"modelStatusCount", model_live_status_call_count}, {"modelSchemaCount", model_recipe_schema_call_count},
                       {QStringLiteral("roundCount"), model_tool_plan.value(QStringLiteral("rounds")).toList().size()},
                       {QStringLiteral("planningContinuation"), mcp_draft_report.value(QStringLiteral("planningContinuation"))},
                   })
                       .toJson(QJsonDocument::Compact).toStdString() << '\n';
      return fail("Model-planned drafts should execute bounded dependent MCP rounds, resume each follow-up as a request-owned stage, and never replay calls");
    }
    const auto user_visible_tool_events =
        mcp_draft_report.value(QStringLiteral("userVisibleToolEvents")).toList();
    const auto tool_event_policy =
        mcp_draft_report.value(QStringLiteral("toolEventDisplayPolicy")).toMap();
    auto saw_visible_internal_mcp_event = false;
    auto saw_visible_plan_event = false;
    auto saw_visible_apply_event = false;
    for (const auto& event_value : user_visible_tool_events) {
      const auto event = event_value.toMap();
      const auto source_tool_name = event.value(QStringLiteral("sourceToolName")).toString();
      if (source_tool_name.startsWith(QStringLiteral("mcp."))) {
        saw_visible_internal_mcp_event = true;
      }
      if (source_tool_name == QStringLiteral("plan_agent_recipe_patch")) {
        saw_visible_plan_event = true;
      }
      if (source_tool_name == QStringLiteral("apply_recipe_patch_to_selected")) {
        saw_visible_apply_event = true;
      }
    }
    const auto manifest = read_json_file(mcp_draft_report.value(QStringLiteral("manifestPath")).toString());
    const auto manifest_tool_event_policy =
        manifest.value(QStringLiteral("toolEventDisplayPolicy")).toObject();
    if (user_visible_tool_events.isEmpty() ||
        user_visible_tool_events.size() >= mcp_draft_report.value(QStringLiteral("toolCalls")).toList().size() ||
        saw_visible_internal_mcp_event ||
        !saw_visible_plan_event ||
        !saw_visible_apply_event ||
        tool_event_policy.value(QStringLiteral("fullAuditField")).toString() !=
            QStringLiteral("toolCalls") ||
        tool_event_policy.value(QStringLiteral("hiddenToolCallCount")).toInt() <= 0 ||
        !tool_event_policy.value(QStringLiteral("defaultHidesInternalMcpEvents")).toBool() ||
        manifest.value(QStringLiteral("userVisibleToolEvents")).toArray().isEmpty() ||
        manifest_tool_event_policy.value(QStringLiteral("fullAuditField")).toString() !=
            QStringLiteral("toolCalls")) {
      return fail("Agent run reports should default to user-relevant tool events while retaining the full MCP audit log");
    }
    const auto mcp_draft_rollback = agent_edit.rollbackDraft();
    if (!mcp_draft_rollback.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
      return fail("MCP-audited draft run should remain rollbackable");
    }
    if (inspection_routing_contract) {
      const auto first_round = model_tool_plan.value("rounds").toList().value(0).toMap();
      const auto corrections = first_round.value("toolCallCorrections").toList();
      if (model_inspection_count != 4 || corrections.size() != 1 ||
          corrections.first().toMap().value("requestedCall").toMap().value("name").toString() != "photo.edit_composition" ||
          corrections.first().toMap().value("executedCall").toMap().value("name").toString() != "photo.inspect_composition") {
        std::cerr << "inspection routing diagnostic=" << QJsonDocument::fromVariant(QVariantMap{
            {"inspectionCount", model_inspection_count}, {"corrections", corrections}})
            .toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("Inspection correction must persist its audit and continue planning without writing the composition");
      }
    }
    if (composition_chain_contract) {
      const auto prompt = mcp_draft_report.value("modelPromptAudit").toMap().value("userPrompt").toString();
      auto expected_operations = QStringList{"select_subject", "add_text", "typography", "copy_selection"};
      if (composition_ready_contract) expected_operations.append("move");
      if (long_task_contract)
        for (int i = 0; i < long_task_update_count; ++i) expected_operations.append("opacity");
      if (chain_operations != expected_operations ||
          !prompt.contains("chain-subject-mask") || !prompt.contains("chain-text") || !prompt.contains("chain-cutout") ||
          prompt.contains("thumbnailDiagnostics") || prompt.contains("evidenceOmittedForSize"))
        return fail("Six dependent composition rounds must preserve returned IDs and compact real controller history without repeating edits");
      for (const auto& value : model_tool_plan.value("toolReports").toList()) {
        const auto report = value.toMap();
        if (!report.value("arguments").toMap().contains("assetId"))
          return fail("Successful model tool reports must retain their actual operation arguments");
      }
      if (composition_ready_contract) {
        const auto queued_round = model_tool_plan.value("rounds").toList().value(4).toMap().value("execution").toMap();
        const auto queued_report = queued_round.value("toolReports").toList().value(0).toMap();
        if (composition_busy || composition_poll_count < 1 || busy_heartbeat_count < 3 ||
            queued_round.value("completionChecks").toMap().value("pollCount").toInt() < 1 ||
            !queued_report.value("completionConfirmed").toBool() ||
            queued_report.value("structuredContent").toMap().value("paintBusy").toBool() ||
            queued_report.value("structuredContent").toMap().value("createdLayerId").toString() != "chain-cutout")
          return fail("Pending cutouts must keep the event loop live, preserve created IDs and await readiness before the same round resumes");
      }
    }
    if (app.arguments().contains(QStringLiteral("--model-tool-rounds-contract")) ||
        inspection_routing_contract || composition_chain_contract) {
      if ((task_lifecycle_contract || long_task_contract) && (task_actions != QStringList{"begin", "check_revert", "review", "check_revert", "revert"} ||
          agent_edit.taskTransaction().value("revertAvailable").toBool())) {
        std::cerr << "task actions=" << task_actions.join(',').toStdString() << '\n';
        return fail("Draft discard must check for conflicts and revert its pre-composition task baseline");
      }
      if (long_task_contract && (model_tool_plan_budget.value("hardLimit").toBool() ||
          model_tool_plan_budget.value("maxRounds").toInt() != -1 ||
          model_tool_plan_budget.value("maxTotalModelToolCalls").toInt() != -1 || long_task_updates != long_task_update_count))
        return fail("Productive tasks must exceed prior total call/round/context limits and remain reversible");
      bridge.stop();
      std::cout << "Agent planning executed two dependent MCP rounds with bounded request-owned continuations and rollback.\n";
      return EXIT_SUCCESS;
    }
    agent_edit.setSelectedModelId(QStringLiteral("rulebook-deterministic"));
  }

  const auto mcp_tools = mcp_server.handleJsonRpc(QJsonObject{
      {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
      {QStringLiteral("id"), 1},
      {QStringLiteral("method"), QStringLiteral("tools/list")},
  });
  const auto mcp_tool_values =
      mcp_tools.value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray();
  auto saw_live_status_tool = false;
  bool saw_search_start = false;
  bool saw_search_status = false;
  auto saw_recipe_schema_tool = false;
  auto saw_commit_confirmation = false;
  auto saw_review_resolution_confirmation = false;
  for (const auto& tool_value : mcp_tool_values) {
    const auto tool = tool_value.toObject();
    const auto name = tool.value(QStringLiteral("name")).toString();
    if (name == QStringLiteral("photo.search_edit_start")) saw_search_start = true;
    if (name == QStringLiteral("photo.search_edit_status")) saw_search_status = true;
    if (name.contains(QStringLiteral("search")) && name.contains(QStringLiteral("confirm")))
      return fail("MCP must not expose search target confirmation");
    if (name == QStringLiteral("photo.live_status") &&
        tool.value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("type")).toString() ==
            QStringLiteral("object") &&
        tool.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("readOnlyHint")).toBool()) {
      saw_live_status_tool = true;
    }
    if (name == QStringLiteral("photo.get_recipe_patch_schema") &&
        tool.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("readOnlyHint")).toBool()) {
      saw_recipe_schema_tool = true;
    }
    if (name == QStringLiteral("photo.commit_agent_draft") &&
        tool.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("requiresConfirmation")).toBool() &&
        tool.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("destructiveHint")).toBool()) {
      saw_commit_confirmation = true;
    }
    if (name == QStringLiteral("photo.resolve_agent_review_issue") &&
        tool.value(QStringLiteral("annotations"))
            .toObject()
            .value(QStringLiteral("requiresConfirmation"))
            .toBool() &&
        tool.value(QStringLiteral("annotations"))
            .toObject()
            .value(QStringLiteral("destructiveHint"))
            .toBool()) {
      saw_review_resolution_confirmation = true;
    }
  }
  if (mcp_tools.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0") ||
      mcp_tools.value(QStringLiteral("id")).toInt() != 1 ||
      mcp_tool_values.size() < 12 ||
      !saw_live_status_tool ||
      !saw_recipe_schema_tool ||
      !saw_commit_confirmation ||
      !saw_review_resolution_confirmation) {
    return fail("MCP tools/list should expose photo tools with safety annotations");
  }
  if (!saw_search_start || !saw_search_status ||
      photo_editor::desktop::automation_capability_for_command(QStringLiteral("photo_search_edit_start")) != QStringLiteral("draft") ||
      photo_editor::desktop::automation_capability_for_command(QStringLiteral("photo_search_edit_status")) != QStringLiteral("read"))
    return fail("search tools must separate authorized starts from read-only polling");

  const auto read_only_tools = mcp_server.toolsListResult({QStringLiteral("read")});
  const auto read_only_tool_json = QJsonDocument(read_only_tools).toJson(QJsonDocument::Compact);
  const auto denied_read_only_mutation = mcp_server.callTool(
      QStringLiteral("photo.rollback_agent_draft"),
      QJsonObject{},
      {QStringLiteral("read")});
  if (read_only_tool_json.contains("photo.rollback_agent_draft") ||
      read_only_tool_json.contains("photo.export_batch") ||
      read_only_tool_json.contains("photo.commit_agent_draft") ||
      !denied_read_only_mutation.value(QStringLiteral("isError")).toBool() ||
      denied_read_only_mutation
              .value(QStringLiteral("structuredContent"))
              .toObject()
              .value(QStringLiteral("requiredCapability"))
              .toString() != QStringLiteral("draft")) {
    return fail("read-only MCP sessions should neither discover nor invoke mutation tools");
  }

  const auto mcp_status = mcp_server.handleJsonRpc(QJsonObject{
      {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
      {QStringLiteral("id"), 2},
      {QStringLiteral("method"), QStringLiteral("tools/call")},
      {QStringLiteral("params"),
       QJsonObject{
           {QStringLiteral("name"), QStringLiteral("photo.live_status")},
           {QStringLiteral("arguments"), QJsonObject{}},
       }},
  });
  const auto mcp_status_result = mcp_status.value(QStringLiteral("result")).toObject();
  if (mcp_status.value(QStringLiteral("id")).toInt() != 2 ||
      mcp_status_result.value(QStringLiteral("isError")).toBool() ||
      mcp_status_result.value(QStringLiteral("structuredContent"))
              .toObject()
              .value(QStringLiteral("selectedAssetCount"))
              .toInt() != 1 ||
      mcp_status_result.value(QStringLiteral("content")).toArray().isEmpty()) {
    return fail("MCP tools/call should return structured live status content");
  }

  const auto mcp_schema = mcp_server.handleJsonRpc(QJsonObject{
      {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
      {QStringLiteral("id"), 3},
      {QStringLiteral("method"), QStringLiteral("tools/call")},
      {QStringLiteral("params"),
       QJsonObject{
           {QStringLiteral("name"), QStringLiteral("photo.get_recipe_patch_schema")},
           {QStringLiteral("arguments"), QJsonObject{}},
       }},
  });
  const auto mcp_schema_content =
      mcp_schema.value(QStringLiteral("result")).toObject().value(QStringLiteral("structuredContent")).toObject();
  if (!mcp_schema_content.value(QStringLiteral("typedSchema")).toBool() ||
      !mcp_schema_content.value(QStringLiteral("validationBacked")).toBool() ||
      !mcp_schema_content.value(QStringLiteral("groups")).isObject()) {
    return fail("MCP recipe schema tool should expose the validation-backed patch schema");
  }

  const auto mcp_unknown = mcp_server.handleJsonRpc(QJsonObject{
      {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
      {QStringLiteral("id"), 4},
      {QStringLiteral("method"), QStringLiteral("tools/call")},
      {QStringLiteral("params"),
       QJsonObject{
           {QStringLiteral("name"), QStringLiteral("photo.unknown_tool")},
           {QStringLiteral("arguments"), QJsonObject{}},
       }},
  });
  if (!mcp_unknown.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool()) {
    return fail("MCP unknown tool calls should return a tool execution error");
  }

  const auto mcp_invalid_method = mcp_server.handleJsonRpc(QJsonObject{
      {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
      {QStringLiteral("id"), 5},
      {QStringLiteral("method"), QStringLiteral("resources/list")},
  });
  if (mcp_invalid_method.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt() !=
      -32601) {
    return fail("MCP unsupported methods should return a JSON-RPC method error");
  }

  const auto unknown_field = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_live_status")},
          {QStringLiteral("unexpected"), true},
      });
  if (unknown_field.value(QStringLiteral("ok")).toBool()) {
    return fail("bridge should reject unknown top-level fields");
  }

  const auto status = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_live_status")},
      });
  if (!status.value(QStringLiteral("ok")).toBool() ||
      status.value(QStringLiteral("result")).toObject().value(QStringLiteral("selectedAssetCount")).toInt() != 1) {
    return fail("photo_live_status should report active selection");
  }

  const auto selected_asset = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_read_asset")},
      });
  const auto selected_asset_result = selected_asset.value(QStringLiteral("result")).toObject();
  if (!selected_asset.value(QStringLiteral("ok")).toBool() ||
      selected_asset_result.value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-1") ||
      selected_asset_result.value(QStringLiteral("asset")).toObject().value(QStringLiteral("filePath")).toString() !=
          source_path) {
    return fail("photo_read_asset should return selected asset metadata");
  }

  const auto selected_adjustments = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_read_adjustments")},
      });
  const auto selected_adjustment_result =
      selected_adjustments
          .value(QStringLiteral("result"))
          .toObject();
  const auto selected_adjustment_patch =
      photo_editor::desktop::
          parse_adjustment_patch_json(
              selected_adjustment_result
                  .value(
                      QStringLiteral(
                          "adjustmentPatchJson"))
                  .toString());
  if (!selected_adjustments.value(QStringLiteral("ok")).toBool() ||
      selected_adjustment_result.value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("asset-1") ||
      !selected_adjustment_patch.ok() ||
      selected_adjustment_patch.scope !=
          photo_editor::desktop::
              AdjustmentPatchScope::AssetBound) {
    return fail("photo_read_adjustments should return a valid asset-bound patch");
  }

  const auto proof = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_render_proof")},
      });
  const auto proof_result = proof.value(QStringLiteral("result")).toObject();
  const auto proof_path = proof_result.value(QStringLiteral("currentProofPath")).toString();
  if (!proof.value(QStringLiteral("ok")).toBool() ||
      !proof_result.value(QStringLiteral("ok")).toBool() ||
      proof_result.value(QStringLiteral("mode")).toString() != QStringLiteral("render-proof") ||
      !QFileInfo::exists(proof_path) ||
      agent_edit.draftActive()) {
    return fail("photo_render_proof should write a read-only proof artifact for the selected asset");
  }

  const auto validate_edit = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_validate_edit")},
      });
  const auto validate_edit_result = validate_edit.value(QStringLiteral("result")).toObject();
  if (!validate_edit.value(QStringLiteral("ok")).toBool() ||
      !validate_edit_result.value(QStringLiteral("ok")).toBool() ||
      validate_edit_result.value(QStringLiteral("mode")).toString() != QStringLiteral("validate-edit") ||
      !validate_edit_result.value(QStringLiteral("validationReport")).toObject().value(QStringLiteral("ok")).toBool() ||
      !QFileInfo::exists(validate_edit_result.value(QStringLiteral("currentProofPath")).toString()) ||
      agent_edit.draftActive()) {
    return fail("photo_validate_edit should validate the selected edit without creating a draft");
  }

  const auto export_dir = QDir(runtime_root.path()).filePath(QStringLiteral("exports"));
  if (!QDir().mkpath(export_dir)) {
    return fail("export destination should be created for bridge preflight testing");
  }
  const auto export_preflight = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_export_preflight")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("preset"), QStringLiteral("Web Proof")},
               {QStringLiteral("destinationDirectory"), export_dir},
               {QStringLiteral("histogramSampleCount"), 100},
               {QStringLiteral("highlightClipCount"), 2},
           }},
      });
  const auto export_preflight_result = export_preflight.value(QStringLiteral("result")).toObject();
  if (!export_preflight.value(QStringLiteral("ok")).toBool() ||
      !export_preflight_result.value(QStringLiteral("ok")).toBool() ||
      export_preflight_result.value(QStringLiteral("mode")).toString() != QStringLiteral("export-preflight") ||
      export_preflight_result.value(QStringLiteral("checkedCount")).toInt() != 1 ||
      export_preflight_result.value(QStringLiteral("warningCount")).toInt() < 2 ||
      !QFileInfo::exists(export_preflight_result.value(QStringLiteral("preflightReportPath")).toString()) ||
      agent_edit.draftActive()) {
    return fail("photo_export_preflight should return and persist selected asset export readiness without creating a draft");
  }
  const auto malformed_preflight = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_export_preflight")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("histogramSampleCount"), QStringLiteral("many")}}},
      });
  if (malformed_preflight.value(QStringLiteral("ok")).toBool()) {
    return fail("photo_export_preflight should reject malformed metric args");
  }

  const auto unconfirmed_export = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_export_batch")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("confirmed"), false},
               {QStringLiteral("preset"), QStringLiteral("Web Proof")},
               {QStringLiteral("destinationDirectory"), export_dir},
           }},
      });
  if (!unconfirmed_export.value(QStringLiteral("ok")).toBool() ||
      unconfirmed_export.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool() ||
      export_jobs.jobCount() != 0) {
    return fail("photo_export_batch should require explicit confirmation before queueing jobs");
  }
  const auto confirmed_export = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_export_batch")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("confirmed"), true},
               {QStringLiteral("preset"), QStringLiteral("Web Proof")},
               {QStringLiteral("destinationDirectory"), export_dir},
               {QStringLiteral("histogramSampleCount"), 100},
               {QStringLiteral("highlightClipCount"), 2},
           }},
      });
  auto confirmed_export_result = confirmed_export.value(QStringLiteral("result")).toObject();
  const auto submitted_run_id = confirmed_export_result.value(QStringLiteral("runId")).toString();
  if (!confirmed_export.value(QStringLiteral("ok")).toBool() ||
      !confirmed_export_result.value(QStringLiteral("ok")).toBool() ||
      confirmed_export_result.value(QStringLiteral("submittedCount")).toInt() != 1 ||
      confirmed_export_result.value(QStringLiteral("queuedCount")).toInt() != 0 ||
      !confirmed_export_result.value(QStringLiteral("admissionPending")).toBool() ||
      confirmed_export_result.value(QStringLiteral("durablyQueued")).toBool()) {
    return fail("photo_export_batch submission must await its durable catalog receipt");
  }
  QElapsedTimer admission_timer;
  admission_timer.start();
  while (agent_edit.running() && admission_timer.elapsed() < 15000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(1);
  }
  confirmed_export_result = QJsonObject::fromVariantMap(agent_edit.lastRunReport());
  if (!confirmed_export.value(QStringLiteral("ok")).toBool() ||
      !confirmed_export_result.value(QStringLiteral("ok")).toBool() ||
      agent_edit.running() ||
      confirmed_export_result.value(QStringLiteral("runId")).toString() != submitted_run_id ||
      confirmed_export_result.value(QStringLiteral("admissionPending")).toBool() ||
      !confirmed_export_result.value(QStringLiteral("durablyQueued")).toBool() ||
      confirmed_export_result.value(QStringLiteral("queuedCount")).toInt() != 1 ||
      export_jobs.jobCount() != 1 ||
      agent_edit.draftActive()) {
    return fail("photo_export_batch should queue confirmed selected-asset exports");
  }

  const auto malformed_patch = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_validate_recipe_patch")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("patch"), QJsonObject{}},
               {QStringLiteral("extra"), true},
           }},
      });
  if (malformed_patch.value(QStringLiteral("ok")).toBool()) {
    return fail("bridge should reject unknown command args");
  }

  const auto linear_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_create_mask")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("type"), QStringLiteral("linear")},
               {QStringLiteral("position"), 0.42},
               {QStringLiteral("rotation"), -12.0},
               {QStringLiteral("exposure"), 0.3},
               {QStringLiteral("feather"), 0.35},
           }},
      });
  const auto linear_mask_result = linear_mask.value(QStringLiteral("result")).toObject();
  const auto linear_mask_patch = linear_mask_result.value(QStringLiteral("maskPatch")).toObject();
  const auto linear_mask_values = linear_mask_patch.value(QStringLiteral("masks")).toObject();
  const auto linear_state =
      selected_adjustment_state(
          catalog_model);
  if (!linear_mask.value(QStringLiteral("ok")).toBool() ||
      !linear_mask_result.value(QStringLiteral("ok")).toBool() ||
      linear_mask_result.value(QStringLiteral("mode")).toString() != QStringLiteral("photo_create_mask") ||
      linear_mask_result.value(QStringLiteral("maskType")).toString() != QStringLiteral("linear") ||
      linear_mask_values.value(QStringLiteral("linearMaskExposure")).toDouble() != 0.3 ||
      linear_state.mask_stack.empty() ||
      !agent_edit.draftActive()) {
    return fail("photo_create_mask should create an active linear mask draft");
  }
  const auto rollback_linear_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback_linear_mask.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back a mask creation draft");
  }

  const auto radial_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_mask_adjustment")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("type"), QStringLiteral("radial")},
               {QStringLiteral("centerX"), 0.52},
               {QStringLiteral("centerY"), 0.48},
               {QStringLiteral("radiusX"), 0.3},
               {QStringLiteral("radiusY"), 0.28},
               {QStringLiteral("exposure"), -0.2},
               {QStringLiteral("temperature"), 8.0},
           }},
      });
  const auto radial_mask_result = radial_mask.value(QStringLiteral("result")).toObject();
  const auto radial_mask_patch = radial_mask_result.value(QStringLiteral("maskPatch")).toObject();
  const auto radial_mask_values = radial_mask_patch.value(QStringLiteral("masks")).toObject();
  const auto radial_state =
      selected_adjustment_state(
          catalog_model);
  if (!radial_mask.value(QStringLiteral("ok")).toBool() ||
      !radial_mask_result.value(QStringLiteral("ok")).toBool() ||
      radial_mask_result.value(QStringLiteral("mode")).toString() != QStringLiteral("photo_apply_mask_adjustment") ||
      radial_mask_result.value(QStringLiteral("maskType")).toString() != QStringLiteral("radial") ||
      radial_mask_values.value(QStringLiteral("radialMaskExposure")).toDouble() != -0.2 ||
      radial_mask_values.value(QStringLiteral("radialMaskTemperature")).toDouble() != 8.0 ||
      radial_state.mask_stack.empty() ||
      !agent_edit.draftActive()) {
    return fail("photo_apply_mask_adjustment should create an active radial mask adjustment draft");
  }
  const auto rollback_radial_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback_radial_mask.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back a mask adjustment draft");
  }

  const auto malformed_mask = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_mask_adjustment")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("type"), QStringLiteral("radial")},
               {QStringLiteral("radiusX"), 2.0},
           }},
      });
  if (malformed_mask.value(QStringLiteral("ok")).toBool()) {
    return fail("photo_apply_mask_adjustment should reject out-of-range mask fields");
  }

  const auto single_asset_bulk = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_plan_bulk_edit")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("consistent editorial batch")}}},
      });
  if (single_asset_bulk.value(QStringLiteral("ok")).toBool()) {
    return fail("photo_plan_bulk_edit should require a multi-asset selection");
  }

  catalog_model.toggleSelectionIndex(1);
  const auto bulk_plan = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_plan_bulk_edit")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("consistent editorial batch")}}},
      });
  const auto bulk_plan_result = bulk_plan.value(QStringLiteral("result")).toObject();
  if (!bulk_plan.value(QStringLiteral("ok")).toBool() ||
      !bulk_plan_result.value(QStringLiteral("ok")).toBool() ||
      bulk_plan_result.value(QStringLiteral("automationCommand")).toString() !=
          QStringLiteral("photo_plan_bulk_edit") ||
      bulk_plan_result.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      bulk_plan_result.value(QStringLiteral("assetIds")).toArray().size() != 2 ||
      bulk_plan_result.value(QStringLiteral("recipePatch")).toObject().isEmpty() ||
      agent_edit.draftActive()) {
    return fail("photo_plan_bulk_edit should plan a selected-set draft without mutating recipes");
  }

  const auto recipe_before_batch_validation =
      catalog_model
          .selectedAdjustmentPatchJson();
  const auto batch_validation = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_validate_batch")},
      });
  const auto batch_validation_result = batch_validation.value(QStringLiteral("result")).toObject();
  if (!batch_validation.value(QStringLiteral("ok")).toBool() ||
      !batch_validation_result.value(QStringLiteral("ok")).toBool() ||
      batch_validation_result.value(QStringLiteral("mode")).toString() != QStringLiteral("validate-batch") ||
      batch_validation_result.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      batch_validation_result.value(QStringLiteral("checkedCount")).toInt() != 2 ||
      batch_validation_result.value(QStringLiteral("assetReports")).toArray().size() != 2 ||
      !QFileInfo::exists(batch_validation_result.value(QStringLiteral("batchValidationReportPath")).toString()) ||
      agent_edit.draftActive() ||
      catalog_model
              .selectedAdjustmentPatchJson() !=
          recipe_before_batch_validation) {
    return fail("photo_validate_batch should validate selected assets without mutating recipes");
  }

  const auto bulk_draft = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_bulk_draft")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("consistent editorial batch")}}},
      });
  const auto bulk_draft_result = bulk_draft.value(QStringLiteral("result")).toObject();
  if (!bulk_draft.value(QStringLiteral("ok")).toBool() ||
      !bulk_draft_result.value(QStringLiteral("ok")).toBool() ||
      bulk_draft_result.value(QStringLiteral("automationCommand")).toString() !=
          QStringLiteral("photo_apply_bulk_draft") ||
      bulk_draft_result.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      bulk_draft_result.value(QStringLiteral("draftedCount")).toInt() != 2 ||
      bulk_draft_result.value(QStringLiteral("assetReports")).toArray().size() != 2 ||
      !agent_edit.draftActive()) {
    return fail("photo_apply_bulk_draft should create a reversible selected-set draft");
  }
  const auto rollback_bulk = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback_bulk.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back a bulk draft");
  }

  const auto draft = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_run_draft_on_selected")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("warm bright editorial draft")}}},
      });
  if (!draft.value(QStringLiteral("ok")).toBool() ||
      !draft.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool() ||
      !agent_edit.draftActive()) {
    return fail("bridge should run an authenticated agent draft");
  }

  const auto unconfirmed_commit = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_commit_agent_draft")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("confirmed"), false}}},
      });
  if (unconfirmed_commit.value(QStringLiteral("ok")).toBool() || !agent_edit.draftActive()) {
    return fail("bridge should reject unconfirmed agent draft commits without clearing the draft");
  }

  const auto rollback = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back an authenticated agent draft");
  }

  const QJsonObject retouch_patch{
      {QStringLiteral("retouch"),
       QJsonObject{
           {QStringLiteral("retouchStack"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("bridge-heal-retouch")},
                    {QStringLiteral("name"), QStringLiteral("Bridge Heal Retouch")},
                    {QStringLiteral("kind"), QStringLiteral("brush")},
                    {QStringLiteral("retouchMode"), QStringLiteral("heal")},
                    {QStringLiteral("brushPoints"),
                     QJsonArray{
                         QJsonObject{
                             {QStringLiteral("x"), 0.42},
                             {QStringLiteral("y"), 0.48},
                             {QStringLiteral("size"), 0.08},
                             {QStringLiteral("flow"), 0.72},
                         },
                     }},
                },
            }},
       }},
  };
  const auto unconfirmed_retouch = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_recipe_patch_draft")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("patch"), retouch_patch}}},
      });
  if (unconfirmed_retouch.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should require explicit confirmation before applying retouch patches");
  }
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  const auto confirmed_retouch = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_apply_recipe_patch_draft")},
          {QStringLiteral("args"),
           QJsonObject{
               {QStringLiteral("patch"), retouch_patch},
               {QStringLiteral("confirmed"), true},
           }},
      });
  if (!confirmed_retouch.value(QStringLiteral("ok")).toBool() ||
      !confirmed_retouch.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool() ||
      !agent_edit.draftActive()) {
    return fail("bridge should apply confirmed retouch patches as reversible drafts");
  }
  const auto rollback_retouch = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_rollback_agent_draft")},
      });
  if (!rollback_retouch.value(QStringLiteral("ok")).toBool() || agent_edit.draftActive()) {
    return fail("bridge should roll back confirmed retouch drafts");
  }
  catalog_model.toggleSelectionIndex(1);

  const auto commit_draft = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_run_draft_on_selected")},
          {QStringLiteral("args"),
           QJsonObject{{QStringLiteral("instructions"), QStringLiteral("warm bright editorial draft")}}},
      });
  if (!commit_draft.value(QStringLiteral("ok")).toBool() || !agent_edit.draftActive()) {
    return fail("bridge should create a draft for confirmed commit testing");
  }
  const auto commit_review_issues =
      commit_draft.value(QStringLiteral("result"))
          .toObject()
          .value(QStringLiteral("batchSummary"))
          .toObject()
          .value(QStringLiteral("reviewIssues"))
          .toArray();
  if (!commit_review_issues.isEmpty()) {
    const auto unconfirmed_review_resolution = send_request(
        bridge,
        session,
        QJsonObject{
            {QStringLiteral("token"), bridge.token()},
            {QStringLiteral("command"),
             QStringLiteral("photo_resolve_agent_review_issue")},
            {QStringLiteral("args"),
             QJsonObject{
                 {QStringLiteral("assetId"),
                  commit_review_issues.first()
                      .toObject()
                      .value(QStringLiteral("assetId"))},
                 {QStringLiteral("action"), QStringLiteral("accept_anyway")},
                 {QStringLiteral("confirmed"), false},
             }},
        });
    if (unconfirmed_review_resolution.value(QStringLiteral("ok")).toBool()) {
      return fail("bridge should reject unconfirmed review resolution");
    }
  }
  for (const auto& issue_value : commit_review_issues) {
    const auto accepted_review_issue = send_request(
        bridge,
        session,
        QJsonObject{
            {QStringLiteral("token"), bridge.token()},
            {QStringLiteral("command"),
             QStringLiteral("photo_resolve_agent_review_issue")},
            {QStringLiteral("args"),
             QJsonObject{
                 {QStringLiteral("assetId"),
                  issue_value.toObject().value(QStringLiteral("assetId"))},
                 {QStringLiteral("action"), QStringLiteral("accept_anyway")},
                 {QStringLiteral("confirmed"), true},
             }},
        });
    if (!accepted_review_issue.value(QStringLiteral("ok")).toBool() ||
        !accepted_review_issue
             .value(QStringLiteral("result"))
             .toObject()
             .value(QStringLiteral("ok"))
             .toBool()) {
      return fail("bridge should resolve flagged draft photos before commit");
    }
  }
  const auto confirmed_commit = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_commit_agent_draft")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("confirmed"), true}}},
      });
  const auto confirmed_commit_result = confirmed_commit.value(QStringLiteral("result")).toObject();
  if (!confirmed_commit.value(QStringLiteral("ok")).toBool() ||
      !confirmed_commit_result.value(QStringLiteral("ok")).toBool() ||
      confirmed_commit_result.value(QStringLiteral("mode")).toString() != QStringLiteral("commit") ||
      confirmed_commit_result.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      confirmed_commit_result.value(QStringLiteral("committedCount")).toInt() != 2 ||
      confirmed_commit_result.value(QStringLiteral("assetIds")).toArray().size() != 2 ||
      agent_edit.draftActive()) {
    return fail("bridge should commit a selected-set agent draft only after explicit confirmation");
  }

  const auto empty_commit = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_commit_agent_draft")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("confirmed"), true}}},
      });
  if (!empty_commit.value(QStringLiteral("ok")).toBool() ||
      empty_commit.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool()) {
    return fail("bridge should not commit when no agent draft is active");
  }

  const auto idle_cancel = send_request(
      bridge,
      session,
      QJsonObject{
          {QStringLiteral("token"), bridge.token()},
          {QStringLiteral("command"), QStringLiteral("photo_cancel_agent_run")},
          {QStringLiteral("args"), QJsonObject{{QStringLiteral("reason"), QStringLiteral("test_idle")}}},
      });
  if (!idle_cancel.value(QStringLiteral("ok")).toBool() ||
      idle_cancel.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool()) {
    return fail("bridge cancel command should report no active run while idle");
  }

  bridge.stop();
  if (QFileInfo::exists(bridge.sessionFilePath())) {
    return fail("stopping bridge should remove session file");
  }

  return EXIT_SUCCESS;
}
