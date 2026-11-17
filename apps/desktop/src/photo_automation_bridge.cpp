#include "photo_automation_bridge.h"
#include "agent_app_command_registry.h"
#include "agent_mcp_client.h"
#include "search_edit_workflow.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDevice>
#include <QImage>
#include <QJsonParseError>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMetaType>
#include <QLocalServer>
#include <QLocalSocket>
#include <QHostAddress>
#include <QHash>
#include <QSaveFile>
#include <QRandomGenerator>
#include <QSet>
#include <QUuid>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSize>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QVector>
#include <QWheelEvent>

#include <tuple>
#include <cmath>
#include <limits>

#include "agent_edit_controller.h"
#include "agent_edit_recipe_patch.h"
#include "app_bootstrap.h"
#include "automation_capability_policy.h"
#include "catalog_asset_model.h"
#include "edit_adjustment_patch.h"
#include "job_queue_model.h"
#include "job_store.h"
#include "photo_mcp_server.h"
#include "photo_composition_tool_contract.h"
#include "timeline_visible_proof_cache.h"
#include "agent_design_schemas.h"
#include "timeline_document_controller.h"
#include "timeline_asset_store.h"
#include "timeline_mcp_export.h"
#include "timeline_playback_controller.h"

namespace photo_editor::desktop {
namespace {

constexpr auto kProtocol = "photo-editor-automation-jsonl-v1";
constexpr auto kMaximumAutomationBytes = 1024 * 1024;
constexpr auto kMaximumMcpHeaderBytes = 32 * 1024;
constexpr auto kMaximumMcpConnections = 16;
constexpr auto kMaximumMcpRequestsPerMinute = 120;
constexpr auto kMaximumJsonDepth = 32;
constexpr auto kVisibleProofCacheFilePrefix = "timeline-visible-";
constexpr int kMaximumVisibleProofDimension = 8192;
constexpr qint64 kMaximumVisibleProofPixels = 32LL * 1024LL * 1024LL;
constexpr qint64 kMaximumVisibleProofCaptureBytes = 160LL * 1024LL * 1024LL;
constexpr qint64 kMinimumVisibleProofIntervalMs = 2000;
constexpr qint64 kMaximumVisibleProofWaitMs = 15000;

struct VisibleProofUiContext final {
  bool ok = false;
  QString error;
  QQuickWindow* window = nullptr;
  QQuickItem* content_item = nullptr;
  QQuickItem* timeline_workspace_item = nullptr;
  QString resource_id;
  QString document_hash;
  double playhead_seconds = 0.0;
  qreal device_pixel_ratio = 1.0;
  QSize logical_target_size;
  QSize pixel_size;
};

VisibleProofUiContext inspect_visible_proof_ui(
    QObject* root_object,
    TimelineDocumentController* timeline_document) {
  VisibleProofUiContext result;
  auto* application = QGuiApplication::instance();
  if (application == nullptr || QThread::currentThread() != application->thread()) {
    result.error = QStringLiteral("Visible Timeline capture must run on the app GUI thread.");
    return result;
  }
  const auto platform = QGuiApplication::platformName().toLower();
  if (platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal") ||
      QGuiApplication::screens().isEmpty()) {
    result.error = QStringLiteral("Visible Timeline capture is unavailable in a headless session.");
    return result;
  }
  if (root_object == nullptr || timeline_document == nullptr) {
    result.error = QStringLiteral("The current Looksy Timeline window is unavailable.");
    return result;
  }
  auto* window = qobject_cast<QQuickWindow*>(root_object);
  if (window == nullptr || window->thread() != application->thread() ||
      !window->isVisible() || !window->isExposed()) {
    result.error = QStringLiteral("The current Looksy Timeline window must be visible and exposed.");
    return result;
  }
  const auto current_workspace = root_object->property("currentWorkspace");
  const auto timeline_workspace_index = root_object->property("timelineWorkspaceIndex");
  if (!current_workspace.isValid() || !timeline_workspace_index.isValid() ||
      current_workspace.toInt() != timeline_workspace_index.toInt()) {
    result.error = QStringLiteral("Show the Timeline workspace before capturing visible proof.");
    return result;
  }
  if (root_object->property("timelineProgramFullScreenActive").toBool()) {
    result.error = QStringLiteral("Exit the separate full-screen Program window before capturing Timeline proof.");
    return result;
  }
  auto* workspace_loader = root_object->findChild<QObject*>(
      QStringLiteral("timelineWorkspaceLoader"), Qt::FindChildrenRecursively);
  auto* timeline_workspace = workspace_loader == nullptr
                                 ? nullptr
                                 : workspace_loader->property("item").value<QObject*>();
  auto* timeline_workspace_item = qobject_cast<QQuickItem*>(timeline_workspace);
  if (workspace_loader == nullptr || !workspace_loader->property("visible").toBool() ||
      timeline_workspace_item == nullptr || !timeline_workspace_item->isVisible()) {
    result.error = QStringLiteral("The visible Timeline workspace is not ready for capture.");
    return result;
  }
  if (timeline_workspace->property("fullScreenProgramActive").toBool() ||
      timeline_workspace->property("programComparisonActive").toBool()) {
    result.error = QStringLiteral("Close Timeline comparison or full-screen Program mode before capturing proof.");
    return result;
  }
  if (!timeline_document->hasActiveTimeline()) {
    result.error = QStringLiteral("Open a Timeline document before capturing visible proof.");
    return result;
  }
  result.resource_id = timeline_document->agentTaskDocumentId();
  result.document_hash = timeline_document->deterministicDocumentHash();
  result.playhead_seconds = timeline_document->sequencePlayheadSeconds();
  if (!result.resource_id.startsWith(QStringLiteral("timeline:")) ||
      result.document_hash.isEmpty() || !std::isfinite(result.playhead_seconds)) {
    result.error = QStringLiteral("The active Timeline identity is not stable enough to capture proof.");
    return result;
  }
  auto* content_item = window->contentItem();
  if (content_item == nullptr) {
    result.error = QStringLiteral("The visible Timeline window has no capture surface.");
    return result;
  }
  const auto dpr = window->devicePixelRatio();
  if (!std::isfinite(dpr) || dpr <= 0.0) {
    result.error = QStringLiteral("The visible Timeline window has an invalid display scale.");
    return result;
  }
  const QSize logical_target_size(qRound(content_item->width()),
                                  qRound(content_item->height()));
  const QSize physical_pixel_size = logical_target_size * dpr;
  const auto pixel_width = physical_pixel_size.width();
  const auto pixel_height = physical_pixel_size.height();
  const qint64 pixel_count = static_cast<qint64>(pixel_width) *
                             static_cast<qint64>(pixel_height);
  if (qAbs(content_item->width() - window->width()) > 0.5 ||
      qAbs(content_item->height() - window->height()) > 0.5 ||
      pixel_width <= 0 || pixel_height <= 0 ||
      pixel_width > kMaximumVisibleProofDimension ||
      pixel_height > kMaximumVisibleProofDimension ||
      pixel_count > kMaximumVisibleProofPixels) {
    result.error = QStringLiteral("The visible Timeline window exceeds the bounded native capture size.");
    return result;
  }
  result.window = window;
  result.content_item = content_item;
  result.timeline_workspace_item = timeline_workspace_item;
  result.device_pixel_ratio = dpr;
  result.logical_target_size = logical_target_size;
  result.pixel_size = physical_pixel_size;
  result.ok = true;
  return result;
}

QString timeline_preview_workspace_error(
    QObject* root_object,
    TimelineDocumentController* timeline_document,
    TimelinePlaybackController* timeline_playback) {
  auto* application = QGuiApplication::instance();
  if (application == nullptr || QThread::currentThread() != application->thread()) {
    return QStringLiteral("Timeline preview navigation must run on the app GUI thread.");
  }
  if (root_object == nullptr || timeline_document == nullptr ||
      timeline_playback == nullptr) {
    return QStringLiteral("The active Timeline preview workspace is unavailable.");
  }
  auto* window = qobject_cast<QQuickWindow*>(root_object);
  if (window == nullptr || window->thread() != application->thread() ||
      !window->isVisible()) {
    return QStringLiteral("Show the Looksy Timeline window before seeking its preview.");
  }
  const auto current_workspace = root_object->property("currentWorkspace");
  const auto timeline_workspace_index = root_object->property("timelineWorkspaceIndex");
  if (!current_workspace.isValid() || !timeline_workspace_index.isValid() ||
      current_workspace.toInt() != timeline_workspace_index.toInt()) {
    return QStringLiteral("Activate the Timeline workspace before seeking its preview.");
  }
  if (root_object->property("timelineProgramFullScreenActive").toBool()) {
    return QStringLiteral("Exit the separate full-screen Program window before seeking Timeline preview.");
  }
  auto* workspace_loader = root_object->findChild<QObject*>(
      QStringLiteral("timelineWorkspaceLoader"), Qt::FindChildrenRecursively);
  auto* timeline_workspace = workspace_loader == nullptr
                                 ? nullptr
                                 : workspace_loader->property("item").value<QObject*>();
  auto* timeline_workspace_item = qobject_cast<QQuickItem*>(timeline_workspace);
  if (workspace_loader == nullptr || !workspace_loader->property("visible").toBool() ||
      timeline_workspace_item == nullptr || !timeline_workspace_item->isVisible()) {
    return QStringLiteral("The Timeline workspace is not visible and ready for preview navigation.");
  }
  if (timeline_workspace->property("fullScreenProgramActive").toBool() ||
      timeline_workspace->property("programComparisonActive").toBool()) {
    return QStringLiteral("Close Timeline comparison or full-screen Program mode before seeking preview.");
  }
  if (!timeline_document->hasActiveTimeline()) {
    return QStringLiteral("Open a Timeline document before seeking its preview.");
  }
  if (!timeline_playback->workspaceActive()) {
    return QStringLiteral("The native Timeline playback workspace is inactive.");
  }
  return {};
}

QJsonObject ok_response(const QJsonObject& result = {}) {
  return QJsonObject{
      {QStringLiteral("ok"), true},
      {QStringLiteral("result"), result},
  };
}

QJsonObject error_response(const QString& message) {
  return QJsonObject{
      {QStringLiteral("ok"), false},
      {QStringLiteral("error"), message},
  };
}

QJsonObject capability_error_response(const QString& required_capability) {
  auto response = error_response(
      QStringLiteral("Automation capability '%1' is required.").arg(required_capability));
  response.insert(QStringLiteral("errorCode"), QStringLiteral("insufficient_capability"));
  response.insert(QStringLiteral("requiredCapability"), required_capability);
  return response;
}

QJsonObject argument_error_response(const QString& message) {
  auto response = error_response(message);
  response.insert(QStringLiteral("data"), QJsonObject{{QStringLiteral("ok"), false},
      {QStringLiteral("errorCode"), QStringLiteral("invalid_arguments")},
      {QStringLiteral("recoverableError"), true}, {QStringLiteral("mutationNotApplied"), true}});
  return response;
}

bool has_only_keys(const QJsonObject& object, const QStringList& keys) {
  for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
    if (!keys.contains(iterator.key())) {
      return false;
    }
  }
  return true;
}

bool is_local_http_origin(const QString& value) {
  const QUrl origin(value);
  return origin.isValid() &&
         origin.scheme() == QStringLiteral("http") &&
         (origin.host() == QStringLiteral("127.0.0.1") ||
          origin.host() == QStringLiteral("localhost")) &&
         origin.userInfo().isEmpty() && origin.path().isEmpty() &&
         origin.query().isEmpty() && origin.fragment().isEmpty();
}

bool is_local_http_host(const QString& value, quint16 server_port) {
  const QUrl host_url(QStringLiteral("http://") + value);
  return host_url.isValid() && host_url.userInfo().isEmpty() &&
         (host_url.host() == QStringLiteral("127.0.0.1") ||
          host_url.host() == QStringLiteral("localhost")) &&
         host_url.port() == static_cast<int>(server_port) &&
         (host_url.path().isEmpty() || host_url.path() == QStringLiteral("/"));
}

int json_depth(const QJsonValue& value, int current_depth = 1) {
  auto maximum_depth = current_depth;
  if (value.isObject()) {
    const auto object = value.toObject();
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
      maximum_depth = std::max(maximum_depth, json_depth(iterator.value(), current_depth + 1));
    }
  } else if (value.isArray()) {
    for (const auto& item : value.toArray()) {
      maximum_depth = std::max(maximum_depth, json_depth(item, current_depth + 1));
    }
  }
  return maximum_depth;
}

bool constant_time_equal(const QByteArray& supplied, const QByteArray& expected) {
  const auto maximum_size = std::max(supplied.size(), expected.size());
  auto difference = static_cast<unsigned int>(supplied.size() ^ expected.size());
  for (qsizetype index = 0; index < maximum_size; ++index) {
    const auto supplied_byte =
        index < supplied.size() ? static_cast<unsigned char>(supplied.at(index)) : 0U;
    const auto expected_byte =
        index < expected.size() ? static_cast<unsigned char>(expected.at(index)) : 0U;
    difference |= supplied_byte ^ expected_byte;
  }
  return difference == 0U;
}

QString high_entropy_token() {
  QString token;
  token.reserve(64);
  auto* random = QRandomGenerator::system();
  for (auto index = 0; index < 4; ++index) {
    token.append(QStringLiteral("%1").arg(random->generate64(), 16, 16, QLatin1Char('0')));
  }
  return token;
}

bool retouch_key_requires_confirmation(const QString& key) {
  const auto normalized = key.trimmed();
  return normalized == QStringLiteral("retouch") ||
         normalized == QStringLiteral("retouchStack") ||
         normalized == QStringLiteral("retouchMode") ||
         normalized == QStringLiteral("removeStrength") ||
         normalized.startsWith(QStringLiteral("inpaint")) ||
         normalized.startsWith(QStringLiteral("clone")) ||
         normalized.startsWith(QStringLiteral("patchSource")) ||
         normalized == QStringLiteral("healTextureBlend") ||
         normalized == QStringLiteral("healColorBlend");
}

bool value_contains_retouch_patch(const QJsonValue& value) {
  if (value.isObject()) {
    const auto object = value.toObject();
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
      if (retouch_key_requires_confirmation(iterator.key()) ||
          value_contains_retouch_patch(iterator.value())) {
        return true;
      }
    }
  } else if (value.isArray()) {
    const auto array = value.toArray();
    for (const auto& item : array) {
      if (value_contains_retouch_patch(item)) {
        return true;
      }
    }
  }
  return false;
}

QJsonObject variant_map_to_json_object(const QVariantMap& value) {
  return QJsonObject::fromVariantMap(value);
}

QJsonArray variant_list_to_json_array(const QVariantList& value) {
  return QJsonArray::fromVariantList(value);
}

QVariantMap asset_for_bridge_request(const CatalogAssetModel* catalog_model, const QJsonObject& args) {
  if (catalog_model == nullptr) {
    return {};
  }
  const auto requested_asset_id = args.value(QStringLiteral("assetId")).toString().trimmed();
  if (requested_asset_id.isEmpty()) {
    return catalog_model->assetAt(catalog_model->selectedIndex());
  }
  for (auto index = 0; index < catalog_model->assetCount(); ++index) {
    auto asset = catalog_model->assetAt(index);
    if (asset.value(QStringLiteral("assetId")).toString() == requested_asset_id) {
      asset.insert(QStringLiteral("visibleIndex"), index);
      return asset;
    }
  }
  return {};
}

bool contains_only_optional_numbers(
    const QJsonObject& object,
    const QStringList& number_keys,
    QString* invalid_key) {
  for (const auto& key : number_keys) {
    const auto value = object.value(key);
    if (!value.isUndefined() && !value.isDouble()) {
      if (invalid_key != nullptr) {
        *invalid_key = key;
      }
      return false;
    }
  }
  return true;
}

bool contains_only_optional_numbers_or_strings(
    const QJsonObject& object,
    const QStringList& number_keys,
    const QStringList& string_keys,
    QString* invalid_key) {
  for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
    const auto key = iterator.key();
    const auto value = iterator.value();
    if (number_keys.contains(key)) {
      if (!value.isDouble()) {
        if (invalid_key != nullptr) {
          *invalid_key = key;
        }
        return false;
      }
    } else if (string_keys.contains(key)) {
      if (!value.isString()) {
        if (invalid_key != nullptr) {
          *invalid_key = key;
        }
        return false;
      }
    } else {
      if (invalid_key != nullptr) {
        *invalid_key = key;
      }
      return false;
    }
  }
  return true;
}

bool bounded_number(
    const QJsonObject& object,
    const QString& key,
    double minimum,
    double maximum,
    QString* invalid_key) {
  const auto value = object.value(key);
  if (value.isUndefined()) {
    return true;
  }
  const auto number = value.toDouble();
  if (number < minimum || number > maximum) {
    if (invalid_key != nullptr) {
      *invalid_key = key;
    }
    return false;
  }
  return true;
}

QVariantMap mask_patch_from_args(const QJsonObject& args, bool create_defaults) {
  const auto type = args.value(QStringLiteral("type")).toString();
  QVariantMap masks;
  const auto insert_number = [&](const QString& arg_key, const QString& patch_key) {
    if (!args.value(arg_key).isUndefined()) {
      masks.insert(patch_key, args.value(arg_key).toDouble());
    }
  };
  if (type == QStringLiteral("linear")) {
    if (create_defaults) {
      masks.insert(QStringLiteral("linearMaskExposure"), 0.01);
    }
    insert_number(QStringLiteral("exposure"), QStringLiteral("linearMaskExposure"));
    insert_number(QStringLiteral("temperature"), QStringLiteral("linearMaskTemperature"));
    insert_number(QStringLiteral("feather"), QStringLiteral("linearMaskFeather"));
    insert_number(QStringLiteral("position"), QStringLiteral("linearMaskPosition"));
    insert_number(QStringLiteral("rotation"), QStringLiteral("linearMaskRotation"));
  } else if (type == QStringLiteral("radial")) {
    if (create_defaults) {
      masks.insert(QStringLiteral("radialMaskExposure"), 0.01);
    }
    insert_number(QStringLiteral("exposure"), QStringLiteral("radialMaskExposure"));
    insert_number(QStringLiteral("temperature"), QStringLiteral("radialMaskTemperature"));
    insert_number(QStringLiteral("feather"), QStringLiteral("radialMaskFeather"));
    insert_number(QStringLiteral("centerX"), QStringLiteral("radialMaskCenterX"));
    insert_number(QStringLiteral("centerY"), QStringLiteral("radialMaskCenterY"));
    insert_number(QStringLiteral("radiusX"), QStringLiteral("radialMaskRadiusX"));
    insert_number(QStringLiteral("radiusY"), QStringLiteral("radialMaskRadiusY"));
    insert_number(QStringLiteral("rotation"), QStringLiteral("radialMaskRotation"));
  }
  return QVariantMap{{QStringLiteral("masks"), masks}};
}

bool valid_mask_args(const QJsonObject& args, bool create_defaults, QString* error_message) {
  const QStringList string_keys{QStringLiteral("type")};
  const QStringList number_keys{
      QStringLiteral("exposure"),
      QStringLiteral("temperature"),
      QStringLiteral("feather"),
      QStringLiteral("position"),
      QStringLiteral("rotation"),
      QStringLiteral("centerX"),
      QStringLiteral("centerY"),
      QStringLiteral("radiusX"),
      QStringLiteral("radiusY"),
  };
  QString invalid_key;
  if (!contains_only_optional_numbers_or_strings(args, number_keys, string_keys, &invalid_key)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Mask command has invalid or unknown field: %1").arg(invalid_key);
    }
    return false;
  }
  const auto type = args.value(QStringLiteral("type")).toString();
  if (type != QStringLiteral("linear") && type != QStringLiteral("radial")) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Mask command requires type linear or radial.");
    }
    return false;
  }
  const QStringList required_for_create =
      type == QStringLiteral("linear")
          ? QStringList{QStringLiteral("position"), QStringLiteral("rotation")}
          : QStringList{QStringLiteral("centerX"), QStringLiteral("centerY"), QStringLiteral("radiusX"), QStringLiteral("radiusY")};
  if (create_defaults) {
    for (const auto& key : required_for_create) {
      if (args.value(key).isUndefined()) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral("photo_create_mask requires %1 for %2 masks.").arg(key, type);
        }
        return false;
      }
    }
  } else {
    auto has_adjustment = false;
    for (const auto& key : number_keys) {
      if (!args.value(key).isUndefined()) {
        has_adjustment = true;
        break;
      }
    }
    if (!has_adjustment) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("photo_apply_mask_adjustment requires at least one numeric mask field.");
      }
      return false;
    }
  }
  const QVector<std::tuple<QString, double, double>> bounds{
      {QStringLiteral("exposure"), -5.0, 5.0},
      {QStringLiteral("temperature"), -100.0, 100.0},
      {QStringLiteral("feather"), 0.0, 1.0},
      {QStringLiteral("position"), 0.0, 1.0},
      {QStringLiteral("rotation"), -180.0, 180.0},
      {QStringLiteral("centerX"), 0.0, 1.0},
      {QStringLiteral("centerY"), 0.0, 1.0},
      {QStringLiteral("radiusX"), 0.01, 1.0},
      {QStringLiteral("radiusY"), 0.01, 1.0},
  };
  for (const auto& [key, minimum, maximum] : bounds) {
    if (!bounded_number(args, key, minimum, maximum, &invalid_key)) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Mask field %1 must be between %2 and %3.").arg(key).arg(minimum).arg(maximum);
      }
      return false;
    }
  }
  return true;
}

QVariantMap export_preflight_metrics_from_args(const QJsonObject& args) {
  QVariantMap metrics;
  const QStringList metric_keys{
      QStringLiteral("histogramSampleCount"),
      QStringLiteral("shadowClipCount"),
      QStringLiteral("highlightClipCount"),
      QStringLiteral("rawClipSampleCount"),
      QStringLiteral("rawShadowClipCount"),
      QStringLiteral("rawHighlightClipCount"),
  };
  for (const auto& key : metric_keys) {
    if (!args.value(key).isUndefined()) {
      metrics.insert(key, args.value(key).toInt());
    }
  }
  return metrics;
}

}  // namespace

struct PhotoAutomationBridge::VisibleProofCapture final {
  QString capture_id;
  QVariantMap response;
  QPointer<QObject> root_object;
  QPointer<QQuickWindow> window;
  QPointer<QQuickItem> content_item;
  QPointer<QQuickItem> timeline_workspace_item;
  QString resource_id;
  QString document_hash;
  QString cache_directory;
  QString requested_at_utc;
  double playhead_seconds = 0.0;
  qreal device_pixel_ratio = 1.0;
  QSize logical_target_size;
  QSize pixel_size;
  QElapsedTimer elapsed;
  QSharedPointer<QQuickItemGrabResult> grab_result;
};

PhotoAutomationBridge::PhotoAutomationBridge(
    AgentEditController* agent_edit,
    CatalogAssetModel* catalog_model,
    QObject* parent)
    : QObject(parent), agent_edit_(agent_edit), catalog_model_(catalog_model) {
  agent_mcp_endpoint_ = AgentMcpClient::registerInProcessEndpoint(this,
      [this](const QVariantMap& request) {
        PhotoMcpServer server(this, nullptr, true);
        return server.handleJsonRpc(QJsonObject::fromVariantMap(request),
            {QStringLiteral("read"), QStringLiteral("draft")}).toVariantMap();
      });
  last_native_wheel_event_ = {{QStringLiteral("received"), false}};
}

PhotoAutomationBridge::~PhotoAutomationBridge() {
  AgentMcpClient::unregisterInProcessEndpoint(agent_mcp_endpoint_);
  stop();
}

void PhotoAutomationBridge::setQmlRootObject(QObject* root_object) {
  auto* next_window = qobject_cast<QQuickWindow*>(root_object);
  auto* previous_window = wheel_diagnostic_window_.data();
  const bool root_changed = qml_root_object_.data() != root_object;
  if (previous_window != next_window) {
    if (previous_window != nullptr) previous_window->removeEventFilter(this);
    wheel_diagnostic_window_ = next_window;
    if (next_window != nullptr) next_window->installEventFilter(this);
  }
  if (root_changed) {
    native_wheel_event_count_ = 0;
    last_native_wheel_event_ = {{QStringLiteral("received"), false}};
    timeline_workspace_loader_ = nullptr;
    if (root_object != nullptr) {
      timeline_workspace_loader_ = root_object->findChild<QObject*>(
          QStringLiteral("timelineWorkspaceLoader"),
          Qt::FindChildrenRecursively);
    }
  }
  qml_root_object_ = root_object;
}

bool PhotoAutomationBridge::eventFilter(QObject* watched, QEvent* event) {
  auto* window = wheel_diagnostic_window_.data();
  if (window == nullptr || watched != window || event == nullptr ||
      event->type() != QEvent::Wheel || qml_root_object_.isNull()) {
    return QObject::eventFilter(watched, event);
  }

  auto* root_object = qml_root_object_.data();
  const auto current_workspace = root_object->property("currentWorkspace");
  const auto timeline_workspace_index =
      root_object->property("timelineWorkspaceIndex");
  if (!current_workspace.isValid() || !timeline_workspace_index.isValid() ||
      current_workspace.toInt() != timeline_workspace_index.toInt()) {
    return QObject::eventFilter(watched, event);
  }

  auto* workspace_loader = timeline_workspace_loader_.data();
  auto* workspace_object = workspace_loader == nullptr
                               ? nullptr
                               : workspace_loader->property("item").value<QObject*>();
  auto* workspace_item = qobject_cast<QQuickItem*>(workspace_object);
  if (workspace_loader == nullptr ||
      !workspace_loader->property("visible").toBool() ||
      workspace_object == nullptr ||
      workspace_object->objectName() != QStringLiteral("timelineWorkspace") ||
      workspace_item == nullptr || !workspace_item->isVisible()) {
    return QObject::eventFilter(watched, event);
  }

  auto* viewport_object =
      workspace_object->property("timelineViewport").value<QObject*>();
  auto* viewport = qobject_cast<QQuickItem*>(viewport_object);
  if (viewport == nullptr || !viewport->isVisible() || !viewport->isEnabled()) {
    return QObject::eventFilter(watched, event);
  }

  auto* wheel_event = static_cast<QWheelEvent*>(event);
  const QPointF window_position = wheel_event->position();
  const QPointF viewport_position = viewport->mapFromScene(window_position);
  if (viewport_position.x() < 0.0 || viewport_position.y() < 0.0 ||
      viewport_position.x() >= viewport->width() ||
      viewport_position.y() >= viewport->height()) {
    return QObject::eventFilter(watched, event);
  }

  const auto* device = wheel_event->pointingDevice();
  const int device_type = device == nullptr
                              ? -1
                              : static_cast<int>(device->type());
  QString device_type_name = QStringLiteral("unknown");
  if (device != nullptr) {
    switch (device->type()) {
      case QInputDevice::DeviceType::Mouse:
        device_type_name = QStringLiteral("mouse");
        break;
      case QInputDevice::DeviceType::TouchPad:
        device_type_name = QStringLiteral("touchpad");
        break;
      case QInputDevice::DeviceType::TouchScreen:
        device_type_name = QStringLiteral("touchscreen");
        break;
      case QInputDevice::DeviceType::Stylus:
        device_type_name = QStringLiteral("stylus");
        break;
      case QInputDevice::DeviceType::Airbrush:
        device_type_name = QStringLiteral("airbrush");
        break;
      case QInputDevice::DeviceType::Puck:
        device_type_name = QStringLiteral("puck");
        break;
      case QInputDevice::DeviceType::Unknown:
      case QInputDevice::DeviceType::Keyboard:
      case QInputDevice::DeviceType::AllDevices:
        break;
    }
  }

  const auto phase_name = [](Qt::ScrollPhase phase) {
    switch (phase) {
      case Qt::ScrollBegin:
        return QStringLiteral("begin");
      case Qt::ScrollUpdate:
        return QStringLiteral("update");
      case Qt::ScrollEnd:
        return QStringLiteral("end");
      case Qt::ScrollMomentum:
        return QStringLiteral("momentum");
      case Qt::NoScrollPhase:
        return QStringLiteral("none");
    }
    return QStringLiteral("unknown");
  };
  const auto source_name = [](Qt::MouseEventSource source) {
    switch (source) {
      case Qt::MouseEventNotSynthesized:
        return QStringLiteral("native");
      case Qt::MouseEventSynthesizedBySystem:
        return QStringLiteral("system-synthesized");
      case Qt::MouseEventSynthesizedByQt:
        return QStringLiteral("qt-synthesized");
      case Qt::MouseEventSynthesizedByApplication:
        return QStringLiteral("application-synthesized");
    }
    return QStringLiteral("unknown");
  };

  const auto viewport_origin = viewport->mapToScene(QPointF(0.0, 0.0));
  ++native_wheel_event_count_;
  last_native_wheel_event_ = {
      {QStringLiteral("received"), true},
      {QStringLiteral("sequence"), native_wheel_event_count_},
      {QStringLiteral("receivedAtMs"), QDateTime::currentMSecsSinceEpoch()},
      {QStringLiteral("eventType"), QStringLiteral("wheel")},
      {QStringLiteral("acceptedBeforeDelivery"), wheel_event->isAccepted()},
      {QStringLiteral("deviceType"), device_type},
      {QStringLiteral("deviceTypeName"), device_type_name},
      {QStringLiteral("pixelDeltaX"), wheel_event->pixelDelta().x()},
      {QStringLiteral("pixelDeltaY"), wheel_event->pixelDelta().y()},
      {QStringLiteral("angleDeltaX"), wheel_event->angleDelta().x()},
      {QStringLiteral("angleDeltaY"), wheel_event->angleDelta().y()},
      {QStringLiteral("modifiers"), static_cast<int>(wheel_event->modifiers())},
      {QStringLiteral("inverted"), wheel_event->inverted()},
      {QStringLiteral("phase"), static_cast<int>(wheel_event->phase())},
      {QStringLiteral("phaseName"), phase_name(wheel_event->phase())},
      {QStringLiteral("source"), static_cast<int>(wheel_event->source())},
      {QStringLiteral("sourceName"), source_name(wheel_event->source())},
      {QStringLiteral("windowX"), window_position.x()},
      {QStringLiteral("windowY"), window_position.y()},
      {QStringLiteral("mappedViewportX"), viewport_position.x()},
      {QStringLiteral("mappedViewportY"), viewport_position.y()},
      {QStringLiteral("viewportSceneX"), viewport_origin.x()},
      {QStringLiteral("viewportSceneY"), viewport_origin.y()},
      {QStringLiteral("viewportWidth"), viewport->width()},
      {QStringLiteral("viewportHeight"), viewport->height()},
      {QStringLiteral("contentXAtFilter"), viewport->property("contentX")},
      {QStringLiteral("contentYAtFilter"), viewport->property("contentY")},
  };

  // This passive filter records delivery before Qt Quick handlers run and
  // never consumes, edits, or redispatches the event.
  return QObject::eventFilter(watched, event);
}

void PhotoAutomationBridge::setTimelineDocumentController(
    TimelineDocumentController* controller) {
  timeline_document_ = controller;
}

bool PhotoAutomationBridge::activateTimelineWorkspaceFromAutomation() const {
  if (QThread::currentThread() != thread() || qml_root_object_ == nullptr) return false;
  QVariant result;
  const auto invoked = QMetaObject::invokeMethod(
      qml_root_object_.data(),
      "activateTimelineWorkspaceForAutomation",
      Qt::DirectConnection,
      Q_RETURN_ARG(QVariant, result));
  return invoked && result.toBool();
}

void PhotoAutomationBridge::setTimelinePlaybackController(
    TimelinePlaybackController* controller) {
  timeline_playback_ = controller;
}

void PhotoAutomationBridge::setTimelineJobQueueModel(JobQueueModel* model) {
  timeline_job_queue_ = model;
}

QVariantMap PhotoAutomationBridge::captureVisibleTimelineProof(
    const QString& capture_id) const {
  if (QCoreApplication::instance() == nullptr ||
      QThread::currentThread() != QCoreApplication::instance()->thread()) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Visible Timeline capture must run on the app GUI thread.")}};
  }
  if (capture_id.size() > 36 ||
      (!capture_id.isEmpty() &&
       (QUuid(capture_id).isNull() ||
        QUuid(capture_id).toString(QUuid::WithoutBraces) != capture_id))) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Provide a captureId returned by this tool.")}};
  }

  if (!capture_id.isEmpty()) {
    auto capture = visible_proof_captures_.value(capture_id);
    if (!capture) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("That visible proof capture is unavailable or expired.")}};
    }
    if (capture->response.value(QStringLiteral("status")).toString() ==
            QStringLiteral("capturing") &&
        capture->elapsed.elapsed() > kMaximumVisibleProofWaitMs) {
      capture->response.insert(QStringLiteral("actualPixelWidth"), 0);
      capture->response.insert(QStringLiteral("actualPixelHeight"), 0);
      capture->response.insert(QStringLiteral("actualSize"), QVariantMap{
          {QStringLiteral("width"), 0}, {QStringLiteral("height"), 0}});
      capture->response.insert(QStringLiteral("actualDevicePixelRatio"), 0.0);
      capture->response.insert(QStringLiteral("status"), QStringLiteral("failed"));
      capture->response.insert(QStringLiteral("error"),
                               QStringLiteral("The visible Timeline snapshot did not finish within the bounded capture window."));
      capture->response.insert(QStringLiteral("completedAtUtc"),
                               QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
      capture->grab_result.clear();
    }
    auto response = capture->response;
    response.insert(QStringLiteral("ok"), true);
    return response;
  }

  for (auto iterator = visible_proof_captures_.begin();
       iterator != visible_proof_captures_.end(); ++iterator) {
    const auto capture = iterator.value();
    if (!capture) continue;
    if (capture->response.value(QStringLiteral("status")).toString() !=
        QStringLiteral("capturing")) {
      continue;
    }
    if (capture->elapsed.elapsed() <= kMaximumVisibleProofWaitMs) {
      auto response = capture->response;
      response.insert(QStringLiteral("ok"), true);
      return response;
    }
    capture->response.insert(QStringLiteral("actualPixelWidth"), 0);
    capture->response.insert(QStringLiteral("actualPixelHeight"), 0);
    capture->response.insert(QStringLiteral("actualSize"), QVariantMap{
        {QStringLiteral("width"), 0}, {QStringLiteral("height"), 0}});
    capture->response.insert(QStringLiteral("actualDevicePixelRatio"), 0.0);
    capture->response.insert(QStringLiteral("status"), QStringLiteral("failed"));
    capture->response.insert(QStringLiteral("error"),
                             QStringLiteral("The visible Timeline snapshot did not finish within the bounded capture window."));
    capture->response.insert(QStringLiteral("completedAtUtc"),
                             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    capture->grab_result.clear();
  }

  const auto context = inspect_visible_proof_ui(
      qml_root_object_.data(), timeline_document_.data());
  if (!context.ok) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), context.error}};
  }
  if (visible_proof_rate_limit_timer_.isValid() &&
      visible_proof_rate_limit_timer_.elapsed() < kMinimumVisibleProofIntervalMs) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Wait before requesting another visible Timeline capture.")}};
  }
  const auto cache = timeline_visible_proof::inspect_cache(AppBootstrap::cacheLocation());
  if (!cache.ok) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), cache.error}};
  }
  if (cache.capture_count >= timeline_visible_proof::kMaximumCaptures ||
      cache.byte_count >= timeline_visible_proof::kMaximumCacheBytes) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("The bounded app proof cache is full; no existing proof was removed.")}};
  }

  // Keep completed status receipts bounded in memory. The PNG files themselves
  // remain in the app-owned cache until the user clears that cache.
  while (visible_proof_captures_.size() >= 32) {
    QString oldest_id;
    QDateTime oldest_time;
    for (auto iterator = visible_proof_captures_.cbegin();
         iterator != visible_proof_captures_.cend(); ++iterator) {
      const auto capture = iterator.value();
      if (!capture || capture->response.value(QStringLiteral("status")).toString() ==
                          QStringLiteral("capturing")) {
        continue;
      }
      const auto completed = QDateTime::fromString(
          capture->response.value(QStringLiteral("completedAtUtc")).toString(),
          Qt::ISODateWithMs);
      if (oldest_id.isEmpty() || (completed.isValid() && completed < oldest_time)) {
        oldest_id = iterator.key();
        oldest_time = completed;
      }
    }
    if (oldest_id.isEmpty()) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("A previous visible proof capture is still pending.")}};
    }
    visible_proof_captures_.remove(oldest_id);
  }

  const auto capture_id_new = QUuid::createUuid().toString(QUuid::WithoutBraces);
  auto capture = std::make_shared<VisibleProofCapture>();
  capture->capture_id = capture_id_new;
  capture->root_object = qml_root_object_;
  capture->window = context.window;
  capture->content_item = context.content_item;
  capture->timeline_workspace_item = context.timeline_workspace_item;
  capture->resource_id = context.resource_id;
  capture->document_hash = context.document_hash;
  capture->playhead_seconds = context.playhead_seconds;
  capture->device_pixel_ratio = context.device_pixel_ratio;
  capture->logical_target_size = context.logical_target_size;
  capture->pixel_size = context.pixel_size;
  capture->cache_directory = cache.directory;
  capture->requested_at_utc =
      QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  capture->elapsed.start();
  capture->response = {
      {QStringLiteral("captureId"), capture_id_new},
      {QStringLiteral("status"), QStringLiteral("capturing")},
      {QStringLiteral("resourceId"), capture->resource_id},
      {QStringLiteral("documentHash"), capture->document_hash},
      {QStringLiteral("docHash"), capture->document_hash},
      {QStringLiteral("playheadSeconds"), capture->playhead_seconds},
      {QStringLiteral("requestedAtUtc"), capture->requested_at_utc},
      {QStringLiteral("requestedPixelWidth"), capture->pixel_size.width()},
      {QStringLiteral("requestedPixelHeight"), capture->pixel_size.height()},
      {QStringLiteral("expectedPixelWidth"), capture->pixel_size.width()},
      {QStringLiteral("expectedPixelHeight"), capture->pixel_size.height()},
      {QStringLiteral("requestedSize"), QVariantMap{
           {QStringLiteral("width"), capture->pixel_size.width()},
           {QStringLiteral("height"), capture->pixel_size.height()}}},
      {QStringLiteral("expectedPixelSize"), QVariantMap{
           {QStringLiteral("width"), capture->pixel_size.width()},
           {QStringLiteral("height"), capture->pixel_size.height()}}},
      {QStringLiteral("requestedLogicalWidth"), capture->logical_target_size.width()},
      {QStringLiteral("requestedLogicalHeight"), capture->logical_target_size.height()},
      {QStringLiteral("requestedLogicalSize"), QVariantMap{
           {QStringLiteral("width"), capture->logical_target_size.width()},
           {QStringLiteral("height"), capture->logical_target_size.height()}}},
      {QStringLiteral("devicePixelRatio"), capture->device_pixel_ratio},
      {QStringLiteral("colorPrecisionBitsPerChannel"), 8},
      {QStringLiteral("displayPrecisionBitsPerChannel"), 8},
      {QStringLiteral("precisionDescription"),
       QStringLiteral("Display-referred screenshot precision after conversion to 8-bit RGBA; this is not source or RAW precision.")},
  };
  visible_proof_captures_.insert(capture_id_new, capture);
  // QQuickItem::grabToImage takes a logical target size; Qt multiplies its
  // backing texture by the window DPR, then returns the physical QImage size.
  capture->grab_result = capture->content_item->grabToImage(capture->logical_target_size);
  if (capture->grab_result.isNull()) {
    capture->response.insert(QStringLiteral("actualPixelWidth"), 0);
    capture->response.insert(QStringLiteral("actualPixelHeight"), 0);
    capture->response.insert(QStringLiteral("actualSize"), QVariantMap{
        {QStringLiteral("width"), 0}, {QStringLiteral("height"), 0}});
    capture->response.insert(QStringLiteral("actualDevicePixelRatio"), 0.0);
    capture->response.insert(QStringLiteral("status"), QStringLiteral("failed"));
    capture->response.insert(QStringLiteral("error"),
                             QStringLiteral("Qt could not start the visible Timeline snapshot."));
    capture->response.insert(QStringLiteral("completedAtUtc"),
                             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    auto response = capture->response;
    response.insert(QStringLiteral("ok"), true);
    return response;
  }
  visible_proof_rate_limit_timer_.start();
  QObject::connect(
      capture->grab_result.data(), &QQuickItemGrabResult::ready,
      const_cast<PhotoAutomationBridge*>(this),
      [this, capture_id_new]() { completeVisibleTimelineProofCapture(capture_id_new); },
      Qt::QueuedConnection);
  if (!capture->grab_result->image().isNull()) {
    QTimer::singleShot(0, const_cast<PhotoAutomationBridge*>(this),
                       [this, capture_id_new]() {
                         completeVisibleTimelineProofCapture(capture_id_new);
                       });
  }
  auto response = capture->response;
  response.insert(QStringLiteral("ok"), true);
  return response;
}

void PhotoAutomationBridge::completeVisibleTimelineProofCapture(
    const QString& capture_id) const {
  auto capture = visible_proof_captures_.value(capture_id);
  if (!capture || capture->response.value(QStringLiteral("status")).toString() !=
                      QStringLiteral("capturing")) {
    return;
  }
  const auto fail_capture = [capture](const QString& message) {
    if (!capture->response.contains(QStringLiteral("actualPixelWidth"))) {
      capture->response.insert(QStringLiteral("actualPixelWidth"), 0);
      capture->response.insert(QStringLiteral("actualPixelHeight"), 0);
      capture->response.insert(QStringLiteral("actualSize"), QVariantMap{
          {QStringLiteral("width"), 0}, {QStringLiteral("height"), 0}});
      capture->response.insert(QStringLiteral("actualDevicePixelRatio"), 0.0);
    }
    capture->response.insert(QStringLiteral("status"), QStringLiteral("failed"));
    capture->response.insert(QStringLiteral("error"), message);
    capture->response.insert(QStringLiteral("completedAtUtc"),
                             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    capture->grab_result.clear();
  };
  if (capture->elapsed.elapsed() > kMaximumVisibleProofWaitMs) {
    fail_capture(QStringLiteral("The visible Timeline snapshot did not finish within the bounded capture window."));
    return;
  }
  const auto current_context = inspect_visible_proof_ui(
      qml_root_object_.data(), timeline_document_.data());
  if (!current_context.ok || capture->root_object.isNull() ||
      capture->window.isNull() || capture->content_item.isNull() ||
      capture->timeline_workspace_item.isNull() ||
      capture->root_object.data() != qml_root_object_.data() ||
      current_context.window != capture->window.data() ||
      current_context.content_item != capture->content_item.data() ||
      current_context.timeline_workspace_item != capture->timeline_workspace_item.data()) {
    fail_capture(current_context.error.isEmpty()
                     ? QStringLiteral("The visible Looksy Timeline window changed during capture.")
                     : current_context.error);
    return;
  }
  if (current_context.resource_id != capture->resource_id ||
      current_context.document_hash != capture->document_hash ||
      qAbs(current_context.playhead_seconds - capture->playhead_seconds) > 0.000001) {
    fail_capture(QStringLiteral("The Timeline document or playhead changed during capture; no proof image was saved."));
    return;
  }
  if (current_context.logical_target_size != capture->logical_target_size ||
      current_context.pixel_size != capture->pixel_size ||
      qAbs(current_context.device_pixel_ratio - capture->device_pixel_ratio) > 0.000001) {
    fail_capture(QStringLiteral("The visible Timeline window size or display scale changed during capture."));
    return;
  }
  if (capture->grab_result.isNull()) {
    fail_capture(QStringLiteral("Qt did not return a visible Timeline snapshot."));
    return;
  }
  auto image = capture->grab_result->image();
  const QSize actual_pixel_size = image.size();
  const auto actual_device_pixel_ratio = image.isNull() ? 0.0 : image.devicePixelRatio();
  capture->response.insert(QStringLiteral("actualPixelWidth"), actual_pixel_size.width());
  capture->response.insert(QStringLiteral("actualPixelHeight"), actual_pixel_size.height());
  capture->response.insert(QStringLiteral("actualSize"), QVariantMap{
      {QStringLiteral("width"), actual_pixel_size.width()},
      {QStringLiteral("height"), actual_pixel_size.height()},
  });
  capture->response.insert(QStringLiteral("actualDevicePixelRatio"), actual_device_pixel_ratio);
  const qint64 actual_pixel_count = static_cast<qint64>(actual_pixel_size.width()) *
                                    static_cast<qint64>(actual_pixel_size.height());
  if (image.isNull() || actual_pixel_size != capture->pixel_size ||
      actual_pixel_size.width() > kMaximumVisibleProofDimension ||
      actual_pixel_size.height() > kMaximumVisibleProofDimension ||
      actual_pixel_count > kMaximumVisibleProofPixels ||
      qAbs(actual_device_pixel_ratio - capture->device_pixel_ratio) > 0.000001) {
    fail_capture(QStringLiteral(
        "Qt returned a Timeline snapshot of %1x%2 physical pixels at DPR %3; expected %4x%5 at DPR %6.")
        .arg(actual_pixel_size.width())
        .arg(actual_pixel_size.height())
        .arg(actual_device_pixel_ratio, 0, 'f', 3)
        .arg(capture->pixel_size.width())
        .arg(capture->pixel_size.height())
        .arg(capture->device_pixel_ratio, 0, 'f', 3));
    return;
  }
  image = image.convertToFormat(QImage::Format_RGBA8888);
  if (image.isNull() || image.format() != QImage::Format_RGBA8888 ||
      image.size() != capture->pixel_size ||
      qAbs(image.devicePixelRatio() - capture->device_pixel_ratio) > 0.000001) {
    fail_capture(QStringLiteral("The Timeline snapshot could not be normalized to bounded 8-bit display pixels."));
    return;
  }

  const auto cache = timeline_visible_proof::inspect_cache(AppBootstrap::cacheLocation());
  if (!cache.ok || cache.directory != capture->cache_directory) {
    fail_capture(cache.error.isEmpty()
                     ? QStringLiteral("The app proof cache changed during capture.")
                     : cache.error);
    return;
  }
  if (cache.capture_count >= timeline_visible_proof::kMaximumCaptures ||
      cache.byte_count >= timeline_visible_proof::kMaximumCacheBytes) {
    fail_capture(QStringLiteral("The bounded app proof cache is full; no existing proof was removed."));
    return;
  }
  const auto file_name = QString::fromLatin1(kVisibleProofCacheFilePrefix) +
                         capture->capture_id + QStringLiteral(".png");
  const auto proof_path = QDir(cache.directory).filePath(file_name);
  if (QFileInfo::exists(proof_path) || QFileInfo(proof_path).isSymLink()) {
    fail_capture(QStringLiteral("The app proof cache target already exists."));
    return;
  }
  QSaveFile proof_file(proof_path);
  proof_file.setDirectWriteFallback(false);
  if (!proof_file.open(QIODevice::WriteOnly) || !image.save(&proof_file, "PNG")) {
    proof_file.cancelWriting();
    fail_capture(QStringLiteral("The visible Timeline proof PNG could not be written atomically."));
    return;
  }
  const auto encoded_bytes = proof_file.size();
  if (encoded_bytes <= 0 || encoded_bytes > kMaximumVisibleProofCaptureBytes ||
      cache.byte_count + encoded_bytes > timeline_visible_proof::kMaximumCacheBytes) {
    proof_file.cancelWriting();
    fail_capture(QStringLiteral("The visible Timeline proof exceeds the bounded app cache size."));
    return;
  }
  if (!proof_file.commit()) {
    fail_capture(QStringLiteral("The visible Timeline proof PNG could not be committed atomically."));
    return;
  }
  const QFileInfo committed_file(proof_path);
  const auto canonical_proof_path = committed_file.canonicalFilePath();
  if (!committed_file.isFile() || committed_file.isSymLink() || canonical_proof_path.isEmpty() ||
      committed_file.size() != encoded_bytes ||
      QFileInfo(canonical_proof_path).dir().canonicalPath() != capture->cache_directory) {
    fail_capture(QStringLiteral("The committed Timeline proof did not remain inside the app cache."));
    return;
  }
  capture->response.insert(QStringLiteral("status"), QStringLiteral("ready"));
  capture->response.insert(QStringLiteral("proofPath"), canonical_proof_path);
  capture->response.insert(QStringLiteral("pixelWidth"), image.width());
  capture->response.insert(QStringLiteral("pixelHeight"), image.height());
  capture->response.insert(QStringLiteral("size"), QVariantMap{
      {QStringLiteral("width"), image.width()},
      {QStringLiteral("height"), image.height()},
  });
  capture->response.insert(QStringLiteral("encodedBytes"), encoded_bytes);
  capture->response.insert(QStringLiteral("capturedAtUtc"),
                           QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  capture->response.insert(QStringLiteral("captureDurationMs"), capture->elapsed.elapsed());
  capture->response.insert(QStringLiteral("completedAtUtc"),
                           QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  capture->grab_result.clear();
}

QVariantMap PhotoAutomationBridge::inspectTimelineRenderGeometry(
    const QString& clip_id) const {
  const auto root_object = qml_root_object_;
  if (root_object.isNull()) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Timeline Program UI is unavailable.")}};
  }

  QVariantMap result;
  const auto inspect_on_ui_thread = [this, root_object, clip_id, &result]() {
    if (root_object.isNull()) {
      result = {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Timeline Program UI is unavailable.")}};
      return;
    }
    auto* workspace = root_object->objectName() == QStringLiteral("timelineWorkspace")
                          ? root_object.data()
                          : root_object->findChild<QObject*>(
                                QStringLiteral("timelineWorkspace"),
                                Qt::FindChildrenRecursively);
    if (workspace == nullptr) {
      result = {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Open the Timeline workspace before inspecting Program geometry.")}};
      return;
    }

    const auto projection_source =
        workspace->property("timelineProjectionDiagnostics").toMap();
    static const QStringList projection_keys{
        QStringLiteral("schema"),
        QStringLiteral("snapshotMethodAvailable"),
        QStringLiteral("snapshotRefreshCount"),
        QStringLiteral("snapshotLastMethodAvailable"),
        QStringLiteral("snapshotLastTrackCount"),
        QStringLiteral("sequenceTrackCount"),
        QStringLiteral("sequenceClipCount"),
        QStringLiteral("hiddenTrackCount"),
        QStringLiteral("visibleTrackCount"),
        QStringLiteral("trackFocusMode"),
        QStringLiteral("trackView"),
        QStringLiteral("trackWindowEnabled"),
        QStringLiteral("virtualizedRowModelCount"),
        QStringLiteral("virtualizedRowArrayCount"),
        QStringLiteral("programBackgroundLayerCount"),
        QStringLiteral("programAdjustmentLayerCount"),
        QStringLiteral("programAudioLayerCount"),
        QStringLiteral("programCompositeOrderCount")};
    QVariantMap projection_diagnostics{
        {QStringLiteral("available"),
         projection_source.value(QStringLiteral("schema")).toString() ==
             QStringLiteral("looksy.timeline-projection-diagnostics/v1")}};
    for (const auto& key : projection_keys) {
      if (projection_source.contains(key)) {
        projection_diagnostics.insert(key, projection_source.value(key));
      }
    }
    QVariantMap audio_playback_diagnostics{{QStringLiteral("available"), false}};
    if (workspace->metaObject()->indexOfMethod("timelineAudioPlaybackDiagnostics()") >= 0) {
      QVariant returned_audio;
      if (QMetaObject::invokeMethod(workspace, "timelineAudioPlaybackDiagnostics",
                                   Qt::DirectConnection, Q_RETURN_ARG(QVariant, returned_audio))) {
        const auto audio = returned_audio.toMap();
        static const QStringList audio_keys{
            QStringLiteral("available"), QStringLiteral("activeLayerCount"),
            QStringLiteral("decodedBufferCount"), QStringLiteral("sinkWrittenBytes"),
            QStringLiteral("activeDecoderCount"), QStringLiteral("nativePlayerCount"),
            QStringLiteral("peakLeft"), QStringLiteral("peakRight"), QStringLiteral("decoderErrorCount")};
        for (const auto& key : audio_keys) {
          if (audio.contains(key)) audio_playback_diagnostics.insert(key, audio.value(key));
        }
      }
    }

    const auto clip_summary = [](const QVariant& value) {
      const auto map = value.toMap();
      return QVariantMap{
          {QStringLiteral("clipId"), map.value(QStringLiteral("clipId")).toString()},
          {QStringLiteral("frameWidth"), map.value(QStringLiteral("frameWidth"))},
          {QStringLiteral("frameHeight"), map.value(QStringLiteral("frameHeight"))},
          {QStringLiteral("frameMode"), map.value(QStringLiteral("frameMode"))},
          {QStringLiteral("frameScale"), map.value(QStringLiteral("frameScale"))},
          {QStringLiteral("frameOffsetX"), map.value(QStringLiteral("frameOffsetX"))},
          {QStringLiteral("frameOffsetY"), map.value(QStringLiteral("frameOffsetY"))},
          {QStringLiteral("frameRotationDegrees"), map.value(QStringLiteral("frameRotationDegrees"))},
      };
    };
    const QStringList object_names{
        QStringLiteral("timelineProgramBackgroundLayer-") + clip_id,
        QStringLiteral("timelineProgramTitleCard-") + clip_id,
        QStringLiteral("timelineProgramBackgroundImage-") + clip_id,
        QStringLiteral("timelineProgramVideoMonitor"),
        QStringLiteral("timelineProgramPrimaryPosterImage"),
        QStringLiteral("timelineProgramPrimaryPosterImageContent"),
        QStringLiteral("timelineProgramPrimaryPosterEffect"),
        QStringLiteral("timelineProgramPrimaryPosterEffectContent"),
    };
    QSet<QString> requested_names;
    for (const auto& object_name : object_names) requested_names.insert(object_name);
    QHash<QString, QQuickItem*> visual_items;
    QSet<QString> ambiguous_names;
    qsizetype visual_items_visited = 0;
    qsizetype object_items_visited = 0;
    bool visual_traversal_truncated = false;
    bool visual_depth_truncated = false;
    struct TraversalNode final {
      QObject* object = nullptr;
      int depth = 0;
    };
    QVector<TraversalNode> pending_items{{workspace, 0}};
    QSet<QObject*> visited_objects;
    constexpr qsizetype kMaximumVisualItems = 8192;
    constexpr int kMaximumVisualDepth = 64;
    const auto enqueue = [&pending_items, &visual_traversal_truncated](
                             QObject* object, int depth) {
      if (object == nullptr) return;
      if (pending_items.size() >= kMaximumVisualItems) {
        visual_traversal_truncated = true;
        return;
      }
      pending_items.push_back({object, depth});
    };
    qsizetype next_item_index = 0;
    while (next_item_index < pending_items.size() &&
           object_items_visited < kMaximumVisualItems) {
      const auto node = pending_items.at(next_item_index++);
      auto* current = node.object;
      if (current == nullptr || visited_objects.contains(current)) continue;
      visited_objects.insert(current);
      ++object_items_visited;
      if (auto* item = qobject_cast<QQuickItem*>(current)) {
        ++visual_items_visited;
        const auto name = item->objectName();
        if (requested_names.contains(name)) {
          if (visual_items.contains(name)) {
            ambiguous_names.insert(name);
            visual_items.remove(name);
          } else if (!ambiguous_names.contains(name)) {
            visual_items.insert(name, item);
          }
        }
        for (auto* child_item : item->childItems()) {
          enqueue(child_item, node.depth + 1);
        }
      }
      if (node.depth >= kMaximumVisualDepth) {
        if (!current->children().isEmpty()) visual_depth_truncated = true;
        continue;
      }
      // QML Loader/Repeater delegates do not always appear under the visual
      // parent that owns them. Walk QObject ownership as well, but inspect and
      // return only QQuickItems whose names exactly match the clip-scoped list.
      for (auto* child_object : current->children()) {
        enqueue(child_object, node.depth + 1);
      }
    }
    if (next_item_index < pending_items.size() &&
        object_items_visited >= kMaximumVisualItems) {
      visual_traversal_truncated = true;
    }
    const auto item_geometry = [&visual_items, &ambiguous_names, clip_id](
                                   const QString& object_name) {
      auto* item = visual_items.value(object_name, nullptr);
      QVariantMap item_result{{QStringLiteral("objectName"), object_name},
                              {QStringLiteral("requestedClipId"), clip_id},
                              {QStringLiteral("found"), item != nullptr},
                              {QStringLiteral("ambiguous"), ambiguous_names.contains(object_name)}};
      if (item == nullptr) {
        return item_result;
      }

      const auto model_data = item->property("modelData").toMap();
      const auto model_data_clip_id = model_data.value(QStringLiteral("clipId")).toString();
      const auto explicit_clip_id = item->property("clipId").toString();
      const auto actual_clip_id = explicit_clip_id.isEmpty() ? model_data_clip_id : explicit_clip_id;
      auto* parent_item = item->parentItem();
      const auto number_property = [item](const char* name, double fallback) {
        const auto value = item->property(name);
        return value.isValid() && value.canConvert<double>() ? value.toDouble() : fallback;
      };
      const auto bool_property = [item](const char* name, bool fallback) {
        const auto value = item->property(name);
        return value.isValid() && value.canConvert<bool>() ? value.toBool() : fallback;
      };
      item_result.insert(QStringLiteral("clipId"), actual_clip_id);
      item_result.insert(QStringLiteral("modelDataClipId"), model_data_clip_id);
      item_result.insert(QStringLiteral("x"), item->x());
      item_result.insert(QStringLiteral("y"), item->y());
      item_result.insert(QStringLiteral("width"), item->width());
      item_result.insert(QStringLiteral("height"), item->height());
      item_result.insert(QStringLiteral("parentWidth"),
                         number_property("parentWidth", parent_item == nullptr ? 0.0 : parent_item->width()));
      item_result.insert(QStringLiteral("parentHeight"),
                         number_property("parentHeight", parent_item == nullptr ? 0.0 : parent_item->height()));
      item_result.insert(QStringLiteral("scale"), item->scale());
      item_result.insert(QStringLiteral("rotation"), item->rotation());
      item_result.insert(QStringLiteral("opacity"), item->opacity());
      item_result.insert(QStringLiteral("visible"),
                         bool_property("effectiveVisible", item->isVisible()));
      const auto painted_width = number_property(
          "paintedWidth", number_property("paintedImageWidth", 0.0));
      const auto painted_height = number_property(
          "paintedHeight", number_property("paintedImageHeight", 0.0));
      item_result.insert(QStringLiteral("paintedWidth"), painted_width);
      item_result.insert(QStringLiteral("paintedHeight"), painted_height);
      const auto source_size = item->property("sourceSize").toSize();
      if (source_size.isValid()) {
        item_result.insert(QStringLiteral("sourceWidth"), source_size.width());
        item_result.insert(QStringLiteral("sourceHeight"), source_size.height());
      }
      return item_result;
    };

    const auto current_clip = clip_summary(workspace->property("currentClip"));
    const auto program_clip = clip_summary(workspace->property("programClip"));
    const auto sanitize_wheel_event = [](const QVariantMap& source) {
      static const QStringList event_keys{
          QStringLiteral("received"), QStringLiteral("sequence"),
          QStringLiteral("receivedAtMs"), QStringLiteral("orientation"),
          QStringLiteral("enabled"), QStringLiteral("accepted"),
          QStringLiteral("deviceType"), QStringLiteral("deviceTypeName"),
          QStringLiteral("pixelDeltaX"), QStringLiteral("pixelDeltaY"),
          QStringLiteral("angleDeltaX"), QStringLiteral("angleDeltaY"),
          QStringLiteral("logicalDeltaX"), QStringLiteral("logicalDeltaY"),
          QStringLiteral("modifiers"), QStringLiteral("inverted"),
          QStringLiteral("eventX"), QStringLiteral("eventY"),
          QStringLiteral("sceneX"), QStringLiteral("sceneY"),
          QStringLiteral("mappedViewportX"), QStringLiteral("mappedViewportY")};
      QVariantMap sanitized;
      for (const auto& key : event_keys) {
        if (source.contains(key)) sanitized.insert(key, source.value(key));
      }
      const auto source_viewport = source.value(QStringLiteral("viewport")).toMap();
      static const QStringList viewport_keys{
          QStringLiteral("sceneX"), QStringLiteral("sceneY"),
          QStringLiteral("width"), QStringLiteral("height"),
          QStringLiteral("visible"), QStringLiteral("enabled"),
          QStringLiteral("contentWidth"), QStringLiteral("contentHeight"),
          QStringLiteral("contentXBefore"), QStringLiteral("contentYBefore"),
          QStringLiteral("contentXAfter"), QStringLiteral("contentYAfter")};
      QVariantMap sanitized_viewport;
      for (const auto& key : viewport_keys) {
        if (source_viewport.contains(key)) {
          sanitized_viewport.insert(key, source_viewport.value(key));
        }
      }
      if (!sanitized_viewport.isEmpty()) {
        sanitized.insert(QStringLiteral("viewport"), sanitized_viewport);
      }
      return sanitized;
    };
    const auto sanitize_native_wheel_event = [](const QVariantMap& source) {
      static const QStringList native_event_keys{
          QStringLiteral("received"), QStringLiteral("sequence"),
          QStringLiteral("receivedAtMs"), QStringLiteral("eventType"),
          QStringLiteral("acceptedBeforeDelivery"),
          QStringLiteral("deviceType"), QStringLiteral("deviceTypeName"),
          QStringLiteral("pixelDeltaX"), QStringLiteral("pixelDeltaY"),
          QStringLiteral("angleDeltaX"), QStringLiteral("angleDeltaY"),
          QStringLiteral("modifiers"), QStringLiteral("inverted"),
          QStringLiteral("phase"), QStringLiteral("phaseName"),
          QStringLiteral("source"), QStringLiteral("sourceName"),
          QStringLiteral("windowX"), QStringLiteral("windowY"),
          QStringLiteral("mappedViewportX"),
          QStringLiteral("mappedViewportY"),
          QStringLiteral("viewportSceneX"),
          QStringLiteral("viewportSceneY"),
          QStringLiteral("viewportWidth"),
          QStringLiteral("viewportHeight"),
          QStringLiteral("contentXAtFilter"),
          QStringLiteral("contentYAtFilter")};
      QVariantMap sanitized;
      for (const auto& key : native_event_keys) {
        if (source.contains(key)) sanitized.insert(key, source.value(key));
      }
      if (!sanitized.contains(QStringLiteral("received"))) {
        sanitized.insert(QStringLiteral("received"), false);
      }
      return sanitized;
    };
    const auto wheel_handler_diagnostic = [&sanitize_wheel_event](
        const QVariantMap& source, const QString& object_name,
        const QString& orientation) {
      const bool found = source.value(QStringLiteral("found")).toBool();
      QVariantMap diagnostic{
          {QStringLiteral("objectName"), object_name},
          {QStringLiteral("orientation"), orientation},
          {QStringLiteral("found"), found},
          {QStringLiteral("enabled"),
           found && source.value(QStringLiteral("enabled")).toBool()},
          {QStringLiteral("acceptedDevices"),
           found ? source.value(QStringLiteral("acceptedDevices")).toInt() : 0},
          {QStringLiteral("stateUpdatedAtMs"),
           found ? source.value(QStringLiteral("stateUpdatedAtMs")).toLongLong() : 0},
      };
      diagnostic.insert(
          QStringLiteral("lastEvent"),
          sanitize_wheel_event(source.value(QStringLiteral("lastEvent")).toMap()));
      if (diagnostic.value(QStringLiteral("lastEvent")).toMap().isEmpty()) {
        diagnostic.insert(QStringLiteral("lastEvent"),
                          QVariantMap{{QStringLiteral("received"), false}});
      }
      return diagnostic;
    };
    QVariantMap wheel_viewport{
        {QStringLiteral("found"), false}};
    const QVariant viewport_value = workspace->property("timelineViewport");
    auto* viewport_object = viewport_value.value<QObject*>();
    auto* viewport_item = qobject_cast<QQuickItem*>(viewport_object);
    if (viewport_item != nullptr) {
      const auto viewport_origin = viewport_item->mapToScene(QPointF(0.0, 0.0));
      wheel_viewport = {
          {QStringLiteral("found"), true},
          {QStringLiteral("sceneX"), viewport_origin.x()},
          {QStringLiteral("sceneY"), viewport_origin.y()},
          {QStringLiteral("width"), viewport_item->width()},
          {QStringLiteral("height"), viewport_item->height()},
          {QStringLiteral("visible"), viewport_item->isVisible()},
          {QStringLiteral("enabled"), viewport_item->isEnabled()},
          {QStringLiteral("contentWidth"),
           viewport_item->property("contentWidth")},
          {QStringLiteral("contentHeight"),
           viewport_item->property("contentHeight")},
          {QStringLiteral("contentX"),
           viewport_item->property("contentX")},
          {QStringLiteral("contentY"),
           viewport_item->property("contentY")},
      };
    }
    const QVariantMap wheel_input_diagnostics{
        {QStringLiteral("scope"), QStringLiteral("latest wheel event inside the current Timeline viewport at window filter and handler stages; no event history")},
        {QStringLiteral("viewport"), wheel_viewport},
        {QStringLiteral("nativeWindowEvent"),
         sanitize_native_wheel_event(last_native_wheel_event_)},
        {QStringLiteral("horizontal"),
         wheel_handler_diagnostic(
             workspace->property("timelineWheelHorizontalDiagnostic").toMap(),
             QStringLiteral("timelineViewportHorizontalWheelHandler"),
             QStringLiteral("horizontal"))},
        {QStringLiteral("vertical"),
         wheel_handler_diagnostic(
             workspace->property("timelineWheelVerticalDiagnostic").toMap(),
             QStringLiteral("timelineViewportVerticalWheelHandler"),
             QStringLiteral("vertical"))},
        {QStringLiteral("discoveryTruncated"), visual_traversal_truncated},
        {QStringLiteral("discoveryDepthTruncated"), visual_depth_truncated},
    };
    QVariantList items;
    for (const auto& object_name : object_names) {
      items.push_back(item_geometry(object_name));
    }
    result = {
        {QStringLiteral("ok"), true},
        {QStringLiteral("clipId"), clip_id},
        {QStringLiteral("currentClip"), current_clip},
        {QStringLiteral("programClip"), program_clip},
        {QStringLiteral("currentClipId"), current_clip.value(QStringLiteral("clipId"))},
        {QStringLiteral("programClipId"), program_clip.value(QStringLiteral("clipId"))},
        {QStringLiteral("sequenceWidth"), timeline_document_.isNull() ? 0 : timeline_document_->sequenceWidth()},
        {QStringLiteral("sequenceHeight"), timeline_document_.isNull() ? 0 : timeline_document_->sequenceHeight()},
        {QStringLiteral("visualItemsVisited"),
         static_cast<qint64>(visual_items_visited)},
        {QStringLiteral("objectItemsVisited"),
         static_cast<qint64>(object_items_visited)},
        {QStringLiteral("visualTraversalTruncated"), visual_traversal_truncated},
        {QStringLiteral("visualDepthTruncated"), visual_depth_truncated},
        {QStringLiteral("items"), items},
        {QStringLiteral("projectionDiagnostics"), projection_diagnostics},
        {QStringLiteral("audioPlaybackDiagnostics"), audio_playback_diagnostics},
        {QStringLiteral("wheelInput"), wheel_input_diagnostics},
    };
  };

  if (QThread::currentThread() == root_object->thread()) {
    inspect_on_ui_thread();
  } else if (!QMetaObject::invokeMethod(
                 root_object, inspect_on_ui_thread, Qt::BlockingQueuedConnection)) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Timeline Program geometry could not be read on the UI thread.")}};
  }
  return result;
}

bool PhotoAutomationBridge::running() const {
  return server_ != nullptr && server_->isListening();
}

QString PhotoAutomationBridge::endpointName() const {
  return endpoint_name_;
}

QString PhotoAutomationBridge::endpointKind() const {
  return endpoint_kind_;
}

QString PhotoAutomationBridge::mcpEndpoint() const {
  return mcp_endpoint_;
}

QString PhotoAutomationBridge::mcpEndpointKind() const {
  return mcp_endpoint_kind_;
}

QStringList PhotoAutomationBridge::mcpCapabilities() const {
  return mcp_capabilities_;
}

int PhotoAutomationBridge::mcpConnectedClientCount() const {
  return mcp_connected_client_count_;
}

int PhotoAutomationBridge::mcpTotalClientCount() const {
  return mcp_total_client_count_;
}

QString PhotoAutomationBridge::mcpLastToolCallName() const {
  return mcp_last_tool_call_name_;
}

QString PhotoAutomationBridge::mcpLastToolCallStatus() const {
  return mcp_last_tool_call_status_;
}

QString PhotoAutomationBridge::mcpLastToolCallAt() const {
  return mcp_last_tool_call_at_;
}

QString PhotoAutomationBridge::token() const {
  return token_;
}

QString PhotoAutomationBridge::sessionFilePath() const {
  return session_file_path_;
}

QString PhotoAutomationBridge::errorMessage() const {
  return error_message_;
}

bool PhotoAutomationBridge::start(
    bool enable_mcp_http,
    const QStringList& mcp_capabilities) {
  stop();
  error_message_.clear();
  if (agent_edit_ == nullptr || catalog_model_ == nullptr) {
    error_message_ = QStringLiteral("Automation bridge requires agent and catalog controllers.");
    return false;
  }

  const auto allowed_capabilities = automation_capability_names();
  for (const auto& capability : mcp_capabilities) {
    const auto normalized = capability.trimmed().toLower();
    if (!allowed_capabilities.contains(normalized)) {
      error_message_ = QStringLiteral("Unknown MCP capability: %1").arg(capability);
      return false;
    }
    if (!mcp_capabilities_.contains(normalized)) {
      mcp_capabilities_.append(normalized);
    }
  }
  if (mcp_capabilities_.isEmpty()) {
    mcp_capabilities_.append(QStringLiteral("read"));
  }

  const auto session_dir = QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("automation"));
  if (!QDir().mkpath(session_dir)) {
    error_message_ = QStringLiteral("Unable to create automation runtime directory.");
    return false;
  }
  if (!QFile::setPermissions(
          session_dir,
          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
    error_message_ = QStringLiteral("Unable to restrict automation runtime directory permissions.");
    return false;
  }
  endpoint_name_ = QStringLiteral("photo-editor-agent-%1").arg(QCoreApplication::applicationPid());
  session_file_path_ = QDir(session_dir).filePath(QStringLiteral("session.json"));
  token_ = high_entropy_token();

  QLocalServer::removeServer(endpoint_name_);
  server_ = new QLocalServer(this);
  server_->setSocketOptions(QLocalServer::UserAccessOption);
  connect(server_, &QLocalServer::newConnection, this, &PhotoAutomationBridge::acceptPendingConnections);
  if (!server_->listen(endpoint_name_)) {
    const auto local_error = server_->errorString();
    server_->deleteLater();
    server_ = nullptr;
    tcp_server_ = new QTcpServer(this);
    connect(tcp_server_, &QTcpServer::newConnection, this, &PhotoAutomationBridge::acceptPendingTcpConnections);
    if (!tcp_server_->listen(QHostAddress::LocalHost, 0)) {
      error_message_ =
          QStringLiteral("QLocalServer failed: %1; localhost TCP fallback failed: %2")
              .arg(local_error, tcp_server_->errorString());
      tcp_server_->deleteLater();
      tcp_server_ = nullptr;
      return false;
    }
    endpoint_kind_ = QStringLiteral("tcp-localhost");
    endpoint_name_ = QStringLiteral("127.0.0.1:%1").arg(tcp_server_->serverPort());
  } else {
    endpoint_kind_ = QStringLiteral("qlocalserver");
  }

  if (enable_mcp_http) {
    mcp_server_ = new PhotoMcpServer(this, this);
    mcp_http_server_ = new QTcpServer(this);
    connect(
        mcp_http_server_,
        &QTcpServer::newConnection,
        this,
        &PhotoAutomationBridge::acceptPendingMcpHttpConnections);
    if (!mcp_http_server_->listen(QHostAddress::LocalHost, 0)) {
      error_message_ = QStringLiteral("MCP localhost HTTP server failed: %1").arg(mcp_http_server_->errorString());
      stop();
      return false;
    }
    mcp_endpoint_kind_ = QStringLiteral("streamable-http-localhost-authenticated");
    mcp_endpoint_ = QStringLiteral("http://127.0.0.1:%1/mcp").arg(mcp_http_server_->serverPort());
  }

  if (!writeSessionFile()) {
    stop();
    return false;
  }

  return true;
}

void PhotoAutomationBridge::stop() {
  removeSessionFile();
  if (server_ != nullptr) {
    if (server_->isListening()) {
      server_->close();
    }
    QLocalServer::removeServer(endpoint_name_);
    server_->deleteLater();
    server_ = nullptr;
  }
  if (tcp_server_ != nullptr) {
    if (tcp_server_->isListening()) {
      tcp_server_->close();
    }
    tcp_server_->deleteLater();
    tcp_server_ = nullptr;
  }
  if (mcp_http_server_ != nullptr) {
    if (mcp_http_server_->isListening()) {
      mcp_http_server_->close();
    }
    mcp_http_server_->deleteLater();
    mcp_http_server_ = nullptr;
  }
  if (mcp_server_ != nullptr) {
    mcp_server_->deleteLater();
    mcp_server_ = nullptr;
  }
  endpoint_name_.clear();
  endpoint_kind_.clear();
  mcp_endpoint_.clear();
  mcp_endpoint_kind_.clear();
  mcp_capabilities_.clear();
  mcp_connected_client_count_ = 0;
  mcp_total_client_count_ = 0;
  mcp_last_tool_call_name_.clear();
  mcp_last_tool_call_status_.clear();
  mcp_last_tool_call_at_.clear();
  mcp_request_times_ms_.clear();
  mcp_active_tool_calls_ = 0;
  token_.clear();
}

void PhotoAutomationBridge::acceptPendingConnections() {
  if (server_ == nullptr) {
    return;
  }
  while (server_->hasPendingConnections()) {
    auto* socket = server_->nextPendingConnection();
    socket->setParent(this);
    socket->setProperty("buffer", QByteArray{});
    connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { readSocketRequest(socket); });
    connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
  }
}

void PhotoAutomationBridge::acceptPendingTcpConnections() {
  if (tcp_server_ == nullptr) {
    return;
  }
  while (tcp_server_->hasPendingConnections()) {
    auto* socket = tcp_server_->nextPendingConnection();
    socket->setParent(this);
    socket->setProperty("buffer", QByteArray{});
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readSocketRequest(socket); });
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
  }
}

void PhotoAutomationBridge::acceptPendingMcpHttpConnections() {
  if (mcp_http_server_ == nullptr) {
    return;
  }
  while (mcp_http_server_->hasPendingConnections()) {
    auto* socket = mcp_http_server_->nextPendingConnection();
    socket->setParent(this);
    if (!socket->peerAddress().isLoopback() ||
        mcp_connected_client_count_ >= kMaximumMcpConnections) {
      connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
      replyMcpHttp(
          socket,
          503,
          QByteArrayLiteral("Service Unavailable"),
          QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP connection limit reached.")}});
      continue;
    }
    socket->setProperty("buffer", QByteArray{});
    ++mcp_connected_client_count_;
    ++mcp_total_client_count_;
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readMcpHttpRequest(socket); });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
      if (mcp_connected_client_count_ > 0) {
        --mcp_connected_client_count_;
      }
      socket->deleteLater();
    });
  }
}

void PhotoAutomationBridge::readSocketRequest(QIODevice* socket) {
  auto buffer = socket->property("buffer").toByteArray();
  buffer.append(socket->readAll());
  auto newline = buffer.indexOf('\n');
  if (newline < 0 && buffer.size() < kMaximumAutomationBytes) {
    socket->setProperty("buffer", buffer);
    return;
  }
  if (newline >= 0) {
    buffer = buffer.left(newline);
  }

  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(buffer, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    reply(socket, error_response(QStringLiteral("Request must be a single JSON object.")));
    return;
  }
  reply(socket, handleJsonRequest(document.object()));
}

void PhotoAutomationBridge::readMcpHttpRequest(QTcpSocket* socket) {
  auto buffer = socket->property("buffer").toByteArray();
  buffer.append(socket->readAll());
  const auto header_end = buffer.indexOf("\r\n\r\n");
  if (header_end < 0 && buffer.size() < kMaximumMcpHeaderBytes) {
    socket->setProperty("buffer", buffer);
    return;
  }
  if (header_end < 0) {
    replyMcpHttp(
        socket,
        400,
        QByteArrayLiteral("Bad Request"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("Malformed HTTP request.")}});
    return;
  }

  const auto header_bytes = buffer.left(header_end);
  const auto header_lines = header_bytes.split('\n');
  if (header_lines.isEmpty()) {
    replyMcpHttp(
        socket,
        400,
        QByteArrayLiteral("Bad Request"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("Missing HTTP request line.")}});
    return;
  }
  const auto request_line = QString::fromLatin1(header_lines.first()).trimmed();
  const auto request_parts = request_line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (request_parts.size() < 2 ||
      request_parts.at(1) != QStringLiteral("/mcp")) {
    replyMcpHttp(
        socket,
        404,
        QByteArrayLiteral("Not Found"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP endpoint is /mcp.")}});
    return;
  }

  const auto method = request_parts.at(0);
  auto content_length = -1;
  QByteArray authorization;
  QString host;
  for (int index = 1; index < header_lines.size(); ++index) {
    const auto line_bytes = header_lines.at(index).trimmed();
    const auto colon = line_bytes.indexOf(':');
    if (colon <= 0) {
      continue;
    }
    const auto key = QString::fromLatin1(line_bytes.left(colon)).trimmed().toLower();
    const auto value_bytes = line_bytes.mid(colon + 1).trimmed();
    const auto value = QString::fromLatin1(value_bytes);
    if (key == QStringLiteral("content-length")) {
      auto ok = false;
      content_length = value.toInt(&ok);
      if (!ok || content_length < 0 || content_length > kMaximumAutomationBytes) {
        replyMcpHttp(
            socket,
            400,
            QByteArrayLiteral("Bad Request"),
            QJsonObject{{QStringLiteral("error"), QStringLiteral("Invalid Content-Length.")}});
        return;
      }
    } else if (key == QStringLiteral("origin")) {
      if (!is_local_http_origin(value)) {
        replyMcpHttp(
            socket,
            403,
            QByteArrayLiteral("Forbidden"),
            QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP localhost endpoint rejects non-local Origin headers.")}});
        return;
      }
    } else if (key == QStringLiteral("authorization")) {
      authorization = value_bytes;
    } else if (key == QStringLiteral("host")) {
      host = value;
    }
  }

  if (mcp_http_server_ == nullptr ||
      !is_local_http_host(host, mcp_http_server_->serverPort())) {
    replyMcpHttp(
        socket,
        400,
        QByteArrayLiteral("Bad Request"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("Valid localhost Host header is required.")}});
    return;
  }

  if (!validMcpAuthorization(authorization)) {
    replyMcpHttp(
        socket,
        401,
        QByteArrayLiteral("Unauthorized"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("Valid MCP authorization is required.")}});
    return;
  }
  if (!admitMcpRequest()) {
    replyMcpHttp(
        socket,
        429,
        QByteArrayLiteral("Too Many Requests"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP request rate limit reached.")}});
    return;
  }

  if (method == QStringLiteral("GET")) {
    if (mcp_server_ == nullptr) {
      replyMcpHttp(
          socket,
          503,
          QByteArrayLiteral("Service Unavailable"),
          QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP server is not available.")}});
      return;
    }
    replyMcpHttpSse(socket);
    return;
  }

  if (method != QStringLiteral("POST")) {
    replyMcpHttp(
        socket,
        405,
        QByteArrayLiteral("Method Not Allowed"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP endpoint accepts GET or POST /mcp.")}});
    return;
  }

  if (content_length < 0) {
    replyMcpHttp(
        socket,
        411,
        QByteArrayLiteral("Length Required"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP HTTP requests require Content-Length.")}});
    return;
  }
  const auto body_start = header_end + 4;
  if (buffer.size() - body_start < content_length) {
    socket->setProperty("buffer", buffer);
    return;
  }
  const auto body = buffer.mid(body_start, content_length);
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(body, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    replyMcpHttp(
        socket,
        400,
        QByteArrayLiteral("Bad Request"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP request body must be a JSON-RPC object.")}});
    return;
  }
  if (json_depth(document.object()) > kMaximumJsonDepth) {
    replyMcpHttp(
        socket,
        400,
        QByteArrayLiteral("Bad Request"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP request JSON nesting is too deep.")}});
    return;
  }
  if (mcp_server_ == nullptr) {
    replyMcpHttp(
        socket,
        503,
        QByteArrayLiteral("Service Unavailable"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP server is not available.")}});
    return;
  }
  const auto request_object = document.object();
  const auto is_tool_call =
      request_object.value(QStringLiteral("method")).toString() == QStringLiteral("tools/call");
  if (is_tool_call && mcp_active_tool_calls_ >= 1) {
    replyMcpHttp(
        socket,
        429,
        QByteArrayLiteral("Too Many Requests"),
        QJsonObject{{QStringLiteral("error"), QStringLiteral("Another MCP tool call is active.")}});
    return;
  }
  if (is_tool_call) {
    ++mcp_active_tool_calls_;
  }
  const auto response = mcp_server_->handleJsonRpc(request_object, mcp_capabilities_);
  if (is_tool_call) {
    --mcp_active_tool_calls_;
    const auto params = request_object.value(QStringLiteral("params")).toObject();
    const auto tool_name = params.value(QStringLiteral("name")).toString();
    const auto status = response.contains(QStringLiteral("error")) ||
                                response.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool()
                            ? QStringLiteral("error")
                            : QStringLiteral("ok");
    recordMcpToolCall(tool_name, status);
  }
  if (response.isEmpty()) {
    replyMcpHttp(socket, 202, QByteArrayLiteral("Accepted"), {});
  } else {
    replyMcpHttp(socket, 200, QByteArrayLiteral("OK"), response);
  }
}

void PhotoAutomationBridge::reply(QIODevice* socket, const QJsonObject& response) const {
  socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
  socket->write("\n");
  if (auto* local_socket = qobject_cast<QLocalSocket*>(socket)) {
    local_socket->flush();
    local_socket->disconnectFromServer();
  } else if (auto* tcp_socket = qobject_cast<QTcpSocket*>(socket)) {
    tcp_socket->flush();
    tcp_socket->disconnectFromHost();
  }
}

void PhotoAutomationBridge::replyMcpHttp(
    QTcpSocket* socket,
    int status_code,
    const QByteArray& status_text,
    const QJsonObject& response) const {
  const auto body = status_code == 202 ? QByteArray{} : QJsonDocument(response).toJson(QJsonDocument::Compact);
  QByteArray header;
  header.append("HTTP/1.1 ");
  header.append(QByteArray::number(status_code));
  header.append(' ');
  header.append(status_text);
  header.append("\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: ");
  header.append(QByteArray::number(body.size()));
  header.append("\r\n\r\n");
  socket->write(header);
  socket->write(body);
  socket->flush();
  socket->disconnectFromHost();
}

void PhotoAutomationBridge::replyMcpHttpSse(QTcpSocket* socket) const {
  const auto tools_result = mcp_server_ != nullptr
                                ? mcp_server_->toolsListResult(mcp_capabilities_)
                                : QJsonObject{};
  const auto tools = tools_result.value(QStringLiteral("tools")).toArray();
  QJsonArray tool_names;
  for (const auto& tool_value : tools) {
    const auto name = tool_value.toObject().value(QStringLiteral("name")).toString();
    if (!name.isEmpty()) {
      tool_names.append(name);
    }
  }

  const auto endpoint_event = QJsonObject{
      {QStringLiteral("endpoint"), mcp_endpoint_},
      {QStringLiteral("protocol"), QStringLiteral("model-context-protocol-jsonrpc-2025-06-18")},
      {QStringLiteral("status"), QStringLiteral("ready")},
      {QStringLiteral("transport"), QStringLiteral("streamable-http")},
  };
  const auto tools_event = QJsonObject{
      {QStringLiteral("toolCount"), tools.size()},
      {QStringLiteral("toolNames"), tool_names},
  };

  QByteArray body;
  body.append("event: endpoint\n");
  body.append("data: ");
  body.append(QJsonDocument(endpoint_event).toJson(QJsonDocument::Compact));
  body.append("\n\n");
  body.append("event: tools\n");
  body.append("data: ");
  body.append(QJsonDocument(tools_event).toJson(QJsonDocument::Compact));
  body.append("\n\n");

  QByteArray header;
  header.append("HTTP/1.1 200 OK\r\n");
  header.append("Content-Type: text/event-stream\r\n");
  header.append("Cache-Control: no-cache\r\n");
  header.append("Connection: close\r\n");
  header.append("Content-Length: ");
  header.append(QByteArray::number(body.size()));
  header.append("\r\n\r\n");
  socket->write(header);
  socket->write(body);
  socket->flush();
  socket->disconnectFromHost();
}

QJsonObject PhotoAutomationBridge::handleJsonRequest(const QJsonObject& request) const {
  return handleJsonRequestWithCapabilities(request, mcp_capabilities_);
}

QJsonObject PhotoAutomationBridge::handleAgentJsonRequest(const QJsonObject& request) const {
  return handleJsonRequestWithCapabilities(request,
      {QStringLiteral("read"), QStringLiteral("draft")});
}

QJsonObject PhotoAutomationBridge::handleJsonRequestWithCapabilities(
    const QJsonObject& request, const QStringList& capabilities) const {
  if (!has_only_keys(
          request,
          {QStringLiteral("id"), QStringLiteral("token"), QStringLiteral("command"), QStringLiteral("args")})) {
    return error_response(QStringLiteral("Request contains unknown fields."));
  }
  if (!constant_time_equal(
          request.value(QStringLiteral("token")).toString().toUtf8(),
          token_.toUtf8())) {
    return error_response(QStringLiteral("Invalid automation token."));
  }
  const auto command = request.value(QStringLiteral("command")).toString().trimmed();
  if (command.isEmpty()) {
    return error_response(QStringLiteral("Command is required."));
  }
  const auto required_capability = automation_capability_for_command(command);
  if (required_capability != QStringLiteral("read") && required_capability != QStringLiteral("export") &&
      command != QStringLiteral("photo_commit_agent_draft") &&
      command != QStringLiteral("photo_cancel_agent_run") &&
      command != QStringLiteral("photo_rollback_agent_draft") &&
      command != QStringLiteral("timeline_task_finish") &&
      command != QStringLiteral("timeline_task_release_reviewed") &&
      command != QStringLiteral("timeline_task_revert") &&
      command != QStringLiteral("timeline_task_recover") &&
      command != QStringLiteral("timeline_task_preparation_cancel") &&
      command != QStringLiteral("photo_search_edit_cancel") &&
      edit_access_policy_ && !edit_access_policy_()) {
    return error_response(QStringLiteral("Editing requires an active trial or purchase."));
  }
  if (!capabilities.contains(required_capability)) {
    return capability_error_response(required_capability);
  }
  const auto args_value = request.value(QStringLiteral("args"));
  if (!args_value.isUndefined() && !args_value.isObject()) {
    return error_response(QStringLiteral("Command args must be an object."));
  }

  auto response = handleCommand(command, args_value.toObject());
  if (request.contains(QStringLiteral("id"))) {
    response.insert(QStringLiteral("id"), request.value(QStringLiteral("id")));
  }
  return response;
}

bool PhotoAutomationBridge::validMcpAuthorization(const QByteArray& authorization) const {
  if (token_.isEmpty()) {
    return false;
  }
  return constant_time_equal(
      authorization,
      QByteArrayLiteral("Bearer ") + token_.toUtf8());
}

bool PhotoAutomationBridge::admitMcpRequest() {
  const auto now_ms = QDateTime::currentMSecsSinceEpoch();
  constexpr auto window_ms = 60 * 1000;
  while (!mcp_request_times_ms_.isEmpty() &&
         now_ms - mcp_request_times_ms_.head() >= window_ms) {
    mcp_request_times_ms_.dequeue();
  }
  if (mcp_request_times_ms_.size() >= kMaximumMcpRequestsPerMinute) {
    return false;
  }
  mcp_request_times_ms_.enqueue(now_ms);
  return true;
}

QJsonObject PhotoAutomationBridge::handleCommand(const QString& command, const QJsonObject& args) const {
  const auto timeline_draft_command = command == QStringLiteral("timeline_saved_drafts_list") ||
      command == QStringLiteral("timeline_document_inspect") ||
      command == QStringLiteral("timeline_project_inputs_list") ||
      command == QStringLiteral("timeline_project_input_import") ||
      command == QStringLiteral("timeline_draft_create") ||
      command == QStringLiteral("timeline_draft_open") ||
      command == QStringLiteral("timeline_draft_save_active") ||
      command == QStringLiteral("timeline_draft_close_saved") ||
      command == QStringLiteral("timeline_autosave_recovery_open") ||
      command == QStringLiteral("timeline_autosave_recovery_inspect");
  if (timeline_draft_command) {
    if (!timeline_draft_lifecycle_handler_)
      return error_response(QStringLiteral("Native Timeline draft navigation is unavailable."));
    QString action;
    if (command == QStringLiteral("timeline_saved_drafts_list")) {
      if (!args.isEmpty())
        return argument_error_response(QStringLiteral("Listing saved Timeline drafts accepts no arguments."));
      action = QStringLiteral("list_saved_drafts");
    } else if (command == QStringLiteral("timeline_document_inspect")) {
      if ((!args.isEmpty() && args.keys() != QStringList{QStringLiteral("includeDocumentHash")}) ||
          (args.contains(QStringLiteral("includeDocumentHash")) &&
           !args.value(QStringLiteral("includeDocumentHash")).isBool()))
        return argument_error_response(QStringLiteral("Inspecting the active Timeline accepts only an optional includeDocumentHash boolean."));
      action = QStringLiteral("inspect_document");
    } else if (command == QStringLiteral("timeline_project_inputs_list")) {
      if (!args.isEmpty())
        return argument_error_response(QStringLiteral("Listing granted project inputs accepts no arguments."));
      action = QStringLiteral("list_project_inputs");
    } else if (command == QStringLiteral("timeline_project_input_import")) {
      if (!has_only_keys(args, {QStringLiteral("inputId"), QStringLiteral("confirmed")}) ||
          !args.value(QStringLiteral("inputId")).isString() ||
          !args.value(QStringLiteral("confirmed")).isBool()) {
        return argument_error_response(QStringLiteral(
            "Project import requires only a startup-granted inputId and confirmed boolean."));
      }
      action = QStringLiteral("import_project_input");
    } else if (command == QStringLiteral("timeline_draft_create")) {
      if (!has_only_keys(args, {QStringLiteral("settings")}) ||
          !args.value(QStringLiteral("settings")).isObject())
        return argument_error_response(QStringLiteral("Timeline draft creation requires only a typed settings object."));
      action = QStringLiteral("create_draft");
    } else if (command == QStringLiteral("timeline_autosave_recovery_open") ||
               command == QStringLiteral("timeline_autosave_recovery_inspect")) {
      if (!has_only_keys(args, {QStringLiteral("timelineId"), QStringLiteral("documentHash")}) ||
          !args.value(QStringLiteral("timelineId")).isString() ||
          !args.value(QStringLiteral("documentHash")).isString())
        return argument_error_response(QStringLiteral("Timeline autosave recovery requires only timelineId and documentHash."));
      action = command == QStringLiteral("timeline_autosave_recovery_open")
          ? QStringLiteral("open_autosave_recovery")
          : QStringLiteral("inspect_autosave_recovery");
    } else if (command == QStringLiteral("timeline_draft_save_active") ||
               command == QStringLiteral("timeline_draft_close_saved")) {
      if (!has_only_keys(args, {QStringLiteral("resourceId"),
                                QStringLiteral("documentHash"),
                                QStringLiteral("confirmed")}) ||
          !args.value(QStringLiteral("resourceId")).isString() ||
          !args.value(QStringLiteral("documentHash")).isString() ||
          !args.value(QStringLiteral("confirmed")).isBool() ||
          !args.value(QStringLiteral("confirmed")).toBool()) {
        return argument_error_response(QStringLiteral(
            "Saving or closing requires only the exact resourceId, documentHash, and confirmed:true."));
      }
      action = command == QStringLiteral("timeline_draft_save_active")
          ? QStringLiteral("save_active_draft")
          : QStringLiteral("close_saved_draft");
    } else {
      if (!has_only_keys(args, {QStringLiteral("timelineId")}) ||
          !args.value(QStringLiteral("timelineId")).isString())
        return argument_error_response(QStringLiteral("Opening a saved Timeline requires only a Library Timeline ID; paths are not accepted."));
      action = QStringLiteral("open_saved_draft");
    }
    const auto result = timeline_draft_lifecycle_handler_(action, args.toVariantMap());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      auto error = error_response(result.value(QStringLiteral("error")).toString());
      error.insert(QStringLiteral("data"), QJsonObject::fromVariantMap(result));
      return error;
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }

  if (command == QStringLiteral("timeline_used_media_inspect")) {
    if (!has_only_keys(args, {QStringLiteral("resourceId"),
                              QStringLiteral("documentHash"),
                              QStringLiteral("offset"),
                              QStringLiteral("maxItems")}) ||
        (args.contains(QStringLiteral("resourceId")) &&
         !args.value(QStringLiteral("resourceId")).isString()) ||
        (args.contains(QStringLiteral("documentHash")) &&
         !args.value(QStringLiteral("documentHash")).isString())) {
      return argument_error_response(QStringLiteral(
          "timeline.inspect_used_media accepts only optional resourceId/documentHash guards and integer offset/maxItems."));
    }

    const auto integer_argument = [&args](const QString& key, qint64 fallback,
                                          qint64 minimum, qint64 maximum,
                                          qint64* value) {
      if (!args.contains(key)) {
        *value = fallback;
        return true;
      }
      const auto json_value = args.value(key);
      if (!json_value.isDouble()) {
        return false;
      }
      const auto numeric_value = json_value.toDouble();
      if (!std::isfinite(numeric_value) ||
          std::floor(numeric_value) != numeric_value ||
          numeric_value < static_cast<double>(minimum) ||
          numeric_value > static_cast<double>(maximum)) {
        return false;
      }
      *value = static_cast<qint64>(numeric_value);
      return true;
    };
    qint64 offset = 0;
    qint64 max_items = 64;
    if (!integer_argument(QStringLiteral("offset"), 0, 0, 2147483647LL,
                          &offset) ||
        !integer_argument(QStringLiteral("maxItems"), 64, 1, 256,
                          &max_items)) {
      return argument_error_response(QStringLiteral(
          "timeline.inspect_used_media offset must be an integer >= 0 and maxItems must be an integer from 1 to 256."));
    }

    const bool has_resource_id = args.contains(QStringLiteral("resourceId"));
    const bool has_document_hash = args.contains(QStringLiteral("documentHash"));
    if (has_resource_id != has_document_hash) {
      return argument_error_response(QStringLiteral(
          "resourceId and documentHash must be supplied together as a stale-read guard."));
    }
    if (QCoreApplication::instance() == nullptr ||
        QThread::currentThread() != QCoreApplication::instance()->thread()) {
      return error_response(QStringLiteral(
          "Timeline used-media inspection must run on the app GUI thread."));
    }
    if (timeline_document_.isNull() || !timeline_document_->hasActiveTimeline()) {
      return error_response(QStringLiteral(
          "Open an active Timeline before inspecting its used media."));
    }

    const auto resource_id = timeline_document_->agentTaskDocumentId();
    const auto document_hash = timeline_document_->deterministicDocumentHash();
    if (resource_id.isEmpty() || document_hash.isEmpty()) {
      return error_response(QStringLiteral(
          "The active Timeline identity is not available for used-media inspection."));
    }
    if (has_resource_id &&
        (args.value(QStringLiteral("resourceId")).toString() != resource_id ||
         args.value(QStringLiteral("documentHash")).toString() != document_hash)) {
      return error_response(QStringLiteral(
          "The active Timeline changed after the supplied resourceId and documentHash were inspected."));
    }

    const auto all_items = timeline_document_->snapshotTimelineUsedMedia();
    const auto total_count = all_items.size();
    const auto begin = static_cast<int>(qMin(offset,
        static_cast<qint64>(total_count)));
    const auto count = static_cast<int>(qMin(max_items,
        static_cast<qint64>(total_count - begin)));
    QJsonArray items;
    for (int index = begin; index < begin + count; ++index) {
      items.append(QJsonObject::fromVariantMap(all_items.at(index).toMap()));
    }

    return ok_response(QJsonObject{
        {QStringLiteral("resourceId"), resource_id},
        {QStringLiteral("documentHash"), document_hash},
        {QStringLiteral("ready"), timeline_document_->agentTaskDocumentReady()},
        {QStringLiteral("guardApplied"), has_resource_id},
        {QStringLiteral("offset"), begin},
        {QStringLiteral("maxItems"), static_cast<int>(max_items)},
        {QStringLiteral("totalCount"), total_count},
        {QStringLiteral("returnedCount"), count},
        {QStringLiteral("hasMore"), begin + count < total_count},
        {QStringLiteral("items"), items},
    });
  }

  if (command == QStringLiteral("timeline_structure_inspect")) {
    if (!has_only_keys(args, {QStringLiteral("scope"), QStringLiteral("resourceId"),
                              QStringLiteral("documentHash"), QStringLiteral("trackId"),
                              QStringLiteral("clipId"), QStringLiteral("query"),
                              QStringLiteral("sequenceSeconds"), QStringLiteral("offset"),
                              QStringLiteral("maxItems")}) ||
        !args.value(QStringLiteral("scope")).isString() ||
        (args.contains(QStringLiteral("resourceId")) &&
         !args.value(QStringLiteral("resourceId")).isString()) ||
        (args.contains(QStringLiteral("documentHash")) &&
         !args.value(QStringLiteral("documentHash")).isString()) ||
        (args.contains(QStringLiteral("trackId")) &&
         (!args.value(QStringLiteral("trackId")).isString() ||
          args.value(QStringLiteral("trackId")).toString().size() > 128)) ||
        (args.contains(QStringLiteral("clipId")) &&
         (!args.value(QStringLiteral("clipId")).isString() ||
          args.value(QStringLiteral("clipId")).toString().size() > 128)) ||
        (args.contains(QStringLiteral("query")) &&
         (!args.value(QStringLiteral("query")).isString() ||
          args.value(QStringLiteral("query")).toString().size() > 128))) {
      return argument_error_response(QStringLiteral(
          "timeline.inspect_structure accepts scope plus optional paired revision guards, stable IDs, query, sequenceSeconds, offset and maxItems."));
    }
    const auto scope = args.value(QStringLiteral("scope")).toString();
    if (scope != QStringLiteral("clips") && scope != QStringLiteral("tracks")) {
      return argument_error_response(QStringLiteral(
          "timeline.inspect_structure scope must be clips or tracks."));
    }
    const bool has_sequence_seconds = args.contains(QStringLiteral("sequenceSeconds"));
    if ((scope == QStringLiteral("tracks") &&
         (args.contains(QStringLiteral("clipId")) || has_sequence_seconds)) ||
        (has_sequence_seconds &&
         (!args.value(QStringLiteral("sequenceSeconds")).isDouble() ||
          !std::isfinite(args.value(QStringLiteral("sequenceSeconds")).toDouble()) ||
          args.value(QStringLiteral("sequenceSeconds")).toDouble() < 0.0 ||
          args.value(QStringLiteral("sequenceSeconds")).toDouble() > 1000000000.0))) {
      return argument_error_response(QStringLiteral(
          "clipId and sequenceSeconds are valid only in clips scope; sequenceSeconds must be finite and from 0 to 1000000000."));
    }
    const auto integer_argument = [&args](const QString& key, qint64 fallback,
                                          qint64 minimum, qint64 maximum,
                                          qint64* value) {
      if (!args.contains(key)) {
        *value = fallback;
        return true;
      }
      const auto json_value = args.value(key);
      if (!json_value.isDouble()) return false;
      const auto numeric_value = json_value.toDouble();
      if (!std::isfinite(numeric_value) ||
          std::floor(numeric_value) != numeric_value ||
          numeric_value < static_cast<double>(minimum) ||
          numeric_value > static_cast<double>(maximum)) {
        return false;
      }
      *value = static_cast<qint64>(numeric_value);
      return true;
    };
    qint64 offset = 0;
    qint64 max_items = 32;
    if (!integer_argument(QStringLiteral("offset"), 0, 0, 2147483647LL, &offset) ||
        !integer_argument(QStringLiteral("maxItems"), 32, 1, 64, &max_items)) {
      return argument_error_response(QStringLiteral(
          "timeline.inspect_structure offset must be a nonnegative integer and maxItems must be an integer from 1 to 64."));
    }

    const bool has_resource_id = args.contains(QStringLiteral("resourceId"));
    const bool has_document_hash = args.contains(QStringLiteral("documentHash"));
    if (has_resource_id != has_document_hash) {
      return argument_error_response(QStringLiteral(
          "resourceId and documentHash must be supplied together as a stale-read guard."));
    }
    if (QCoreApplication::instance() == nullptr ||
        QThread::currentThread() != QCoreApplication::instance()->thread()) {
      return error_response(QStringLiteral(
          "Timeline structure inspection must run on the app GUI thread."));
    }
    if (timeline_document_.isNull() || !timeline_document_->hasActiveTimeline()) {
      return error_response(QStringLiteral(
          "Open an active Timeline before inspecting its structure."));
    }

    const auto resource_id = timeline_document_->agentTaskDocumentId();
    const auto document_hash = timeline_document_->deterministicDocumentHash();
    if (resource_id.isEmpty() || document_hash.isEmpty()) {
      return error_response(QStringLiteral(
          "The active Timeline identity is not available for structure inspection."));
    }
    if (has_resource_id &&
        (args.value(QStringLiteral("resourceId")).toString() != resource_id ||
         args.value(QStringLiteral("documentHash")).toString() != document_hash)) {
      return error_response(QStringLiteral(
          "The active Timeline changed after the supplied resourceId and documentHash were inspected."));
    }

    const auto result = timeline_document_->inspectTimelineStructurePage(
        scope, static_cast<int>(offset), static_cast<int>(max_items),
        args.value(QStringLiteral("trackId")).toString(),
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("query")).toString(), has_sequence_seconds,
        has_sequence_seconds ? args.value(QStringLiteral("sequenceSeconds")).toDouble() : 0.0);
    if (!result.value(QStringLiteral("ok")).toBool()) {
      auto error = error_response(result.value(QStringLiteral("error")).toString());
      error.insert(QStringLiteral("data"), QJsonObject::fromVariantMap(result));
      return error;
    }
    auto body = QJsonObject::fromVariantMap(result);
    body.insert(QStringLiteral("resourceId"), resource_id);
    body.insert(QStringLiteral("documentHash"), document_hash);
    body.insert(QStringLiteral("guardApplied"), has_resource_id);
    body.insert(QStringLiteral("ready"), timeline_document_->agentTaskDocumentReady());
    return ok_response(body);
  }

  if (command == QStringLiteral("timeline_review_notes_inspect")) {
    if (!has_only_keys(args, {QStringLiteral("resourceId"), QStringLiteral("documentHash"),
                              QStringLiteral("noteId"), QStringLiteral("state"),
                              QStringLiteral("query"), QStringLiteral("offset"),
                              QStringLiteral("maxItems")}) ||
        (args.contains(QStringLiteral("resourceId")) && !args.value(QStringLiteral("resourceId")).isString()) ||
        (args.contains(QStringLiteral("documentHash")) && !args.value(QStringLiteral("documentHash")).isString()) ||
        (args.contains(QStringLiteral("noteId")) && !args.value(QStringLiteral("noteId")).isString()) ||
        (args.contains(QStringLiteral("state")) && !args.value(QStringLiteral("state")).isString()) ||
        (args.contains(QStringLiteral("query")) && !args.value(QStringLiteral("query")).isString()) ||
        args.value(QStringLiteral("noteId")).toString().size() > 64 ||
        args.value(QStringLiteral("query")).toString().size() > 256) {
      return argument_error_response(QStringLiteral(
          "timeline.inspect_review_notes accepts optional resourceId/documentHash, noteId, state, query, offset and maxItems."));
    }
    const auto integer_argument = [&args](const QString& key, qint64 fallback,
                                          qint64 minimum, qint64 maximum,
                                          qint64* value) {
      if (!args.contains(key)) {
        *value = fallback;
        return true;
      }
      const auto json_value = args.value(key);
      if (!json_value.isDouble()) return false;
      const auto numeric_value = json_value.toDouble();
      if (!std::isfinite(numeric_value) || std::floor(numeric_value) != numeric_value ||
          numeric_value < static_cast<double>(minimum) || numeric_value > static_cast<double>(maximum)) {
        return false;
      }
      *value = static_cast<qint64>(numeric_value);
      return true;
    };
    qint64 offset = 0;
    qint64 max_items = 64;
    if (!integer_argument(QStringLiteral("offset"), 0, 0, 2147483647LL, &offset) ||
        !integer_argument(QStringLiteral("maxItems"), 64, 1, 256, &max_items)) {
      return argument_error_response(QStringLiteral(
          "timeline.inspect_review_notes offset must be an integer >= 0 and maxItems must be an integer from 1 to 256."));
    }
    const auto requested_state = args.value(QStringLiteral("state")).toString(QStringLiteral("all"));
    if (requested_state != QStringLiteral("all") && requested_state != QStringLiteral("open") &&
        requested_state != QStringLiteral("resolved")) {
      return argument_error_response(QStringLiteral("Review note state must be all, open, or resolved."));
    }
    const bool has_resource_id = args.contains(QStringLiteral("resourceId"));
    const bool has_document_hash = args.contains(QStringLiteral("documentHash"));
    if (has_resource_id != has_document_hash) {
      return argument_error_response(QStringLiteral(
          "resourceId and documentHash must be supplied together as a stale-read guard."));
    }
    if (QCoreApplication::instance() == nullptr ||
        QThread::currentThread() != QCoreApplication::instance()->thread()) {
      return error_response(QStringLiteral("Timeline review-note inspection must run on the app GUI thread."));
    }
    if (timeline_document_.isNull() || !timeline_document_->hasActiveTimeline()) {
      return error_response(QStringLiteral("Open an active Timeline before inspecting review notes."));
    }
    const auto resource_id = timeline_document_->agentTaskDocumentId();
    const auto document_hash = timeline_document_->deterministicDocumentHash();
    if (resource_id.isEmpty() || document_hash.isEmpty()) {
      return error_response(QStringLiteral("The active Timeline identity is not available for review-note inspection."));
    }
    if (has_resource_id &&
        (args.value(QStringLiteral("resourceId")).toString() != resource_id ||
         args.value(QStringLiteral("documentHash")).toString() != document_hash)) {
      return error_response(QStringLiteral(
          "The active Timeline changed after the supplied resourceId and documentHash were inspected."));
    }
    const auto requested_note_id = args.value(QStringLiteral("noteId")).toString().trimmed();
    const auto query = args.value(QStringLiteral("query")).toString().simplified();
    QVariantList matches;
    for (const auto& row_value : timeline_document_->reviewNotes()) {
      const auto row = row_value.toMap();
      const auto note_id = row.value(QStringLiteral("markerId")).toString();
      const auto resolved = row.value(QStringLiteral("resolved")).toBool();
      if (!requested_note_id.isEmpty() && note_id != requested_note_id) continue;
      if (requested_state == QStringLiteral("open") && resolved) continue;
      if (requested_state == QStringLiteral("resolved") && !resolved) continue;
      if (!query.isEmpty()) {
        QStringList searchable{
            row.value(QStringLiteral("label")).toString(),
            row.value(QStringLiteral("note")).toString(),
            row.value(QStringLiteral("assignee")).toString()};
        searchable.append(row.value(QStringLiteral("clipIds")).toStringList());
        const auto has_match = std::any_of(searchable.cbegin(), searchable.cend(),
            [&query](const QString& value) { return value.contains(query, Qt::CaseInsensitive); });
        if (!has_match) continue;
      }
      matches.push_back(row);
    }
    const auto total_count = matches.size();
    const auto begin = static_cast<int>(qMin(offset, static_cast<qint64>(total_count)));
    const auto count = static_cast<int>(qMin(max_items, static_cast<qint64>(total_count - begin)));
    QJsonArray items;
    for (int index = begin; index < begin + count; ++index) {
      items.append(QJsonObject::fromVariantMap(matches.at(index).toMap()));
    }
    return ok_response(QJsonObject{
        {QStringLiteral("resourceId"), resource_id},
        {QStringLiteral("documentHash"), document_hash},
        {QStringLiteral("ready"), timeline_document_->agentTaskDocumentReady()},
        {QStringLiteral("guardApplied"), has_resource_id},
        {QStringLiteral("state"), requested_state},
        {QStringLiteral("offset"), begin},
        {QStringLiteral("maxItems"), static_cast<int>(max_items)},
        {QStringLiteral("totalCount"), total_count},
        {QStringLiteral("returnedCount"), count},
        {QStringLiteral("hasMore"), begin + count < total_count},
        {QStringLiteral("items"), items},
    });
  }

  const auto timeline_task_command = command == QStringLiteral("timeline_task_admit") ||
      command == QStringLiteral("timeline_task_status") ||
      command == QStringLiteral("timeline_task_preparation_cancel") ||
      command == QStringLiteral("timeline_task_finish") ||
      command == QStringLiteral("timeline_task_release_reviewed") ||
      command == QStringLiteral("timeline_task_revert") ||
      command == QStringLiteral("timeline_task_recover");
  if (timeline_task_command) {
    if (!timeline_task_lifecycle_handler_)
      return error_response(QStringLiteral("Native Timeline task ownership is unavailable."));
    QString action;
    if (command == QStringLiteral("timeline_task_admit")) {
      if (!has_only_keys(args, {QStringLiteral("targetResourceId"), QStringLiteral("objective"),
              QStringLiteral("actions"), QStringLiteral("assetPaths"), QStringLiteral("outputDirectory"),
              QStringLiteral("confirmed")}) || !args.value(QStringLiteral("targetResourceId")).isString() ||
          !args.value(QStringLiteral("objective")).isString() || !args.value(QStringLiteral("actions")).isArray() ||
          !args.value(QStringLiteral("assetPaths")).isArray() || !args.value(QStringLiteral("outputDirectory")).isString() ||
          !args.value(QStringLiteral("confirmed")).isBool())
        return error_response(QStringLiteral("Timeline task admission requires a stable target, approved action plan, canonical path scope and confirmed:true."));
      for (const auto& value : args.value(QStringLiteral("actions")).toArray())
        if (!value.isObject()) return error_response(QStringLiteral("Timeline task actions must be typed objects."));
      for (const auto& value : args.value(QStringLiteral("assetPaths")).toArray())
        if (!value.isString()) return error_response(QStringLiteral("Timeline task assetPaths must contain only strings."));
      action = QStringLiteral("admit");
    } else if (command == QStringLiteral("timeline_task_status")) {
      if (!args.isEmpty()) return error_response(QStringLiteral("Timeline task status accepts no arguments."));
      action = QStringLiteral("status");
    } else if (command == QStringLiteral("timeline_task_preparation_cancel")) {
      if (!args.isEmpty()) return error_response(QStringLiteral("Timeline task preparation cancellation accepts no arguments."));
      action = QStringLiteral("cancel_preparation");
    } else if (command == QStringLiteral("timeline_task_finish")) {
      if (!has_only_keys(args, {QStringLiteral("taskId"), QStringLiteral("action")}) ||
          !args.value(QStringLiteral("taskId")).isString() || !args.value(QStringLiteral("action")).isString() ||
          !QStringList{QStringLiteral("review"), QStringLiteral("continue")}.contains(args.value(QStringLiteral("action")).toString()))
        return error_response(QStringLiteral("Timeline task finish requires taskId and action review or continue."));
      action = QStringLiteral("finish");
    } else if (command == QStringLiteral("timeline_task_release_reviewed")) {
      if (!has_only_keys(args, {QStringLiteral("taskId"), QStringLiteral("confirmed")}) ||
          !args.value(QStringLiteral("taskId")).isString() ||
          !args.value(QStringLiteral("confirmed")).isBool() ||
          !args.value(QStringLiteral("confirmed")).toBool())
        return error_response(QStringLiteral("Reviewed Timeline ownership release requires taskId and confirmed:true."));
      action = QStringLiteral("release_reviewed");
    } else if (command == QStringLiteral("timeline_task_revert")) {
      if (!has_only_keys(args, {QStringLiteral("taskId"), QStringLiteral("confirmed")}) ||
          !args.value(QStringLiteral("taskId")).isString() || !args.value(QStringLiteral("confirmed")).isBool() ||
          !args.value(QStringLiteral("confirmed")).toBool())
        return error_response(QStringLiteral("Timeline task revert requires taskId and confirmed:true."));
      action = QStringLiteral("revert");
    } else {
      if (!has_only_keys(args, {QStringLiteral("taskId"), QStringLiteral("targetResourceId")}) ||
          !args.value(QStringLiteral("taskId")).isString() || !args.value(QStringLiteral("targetResourceId")).isString())
        return error_response(QStringLiteral("Timeline task recovery requires taskId and targetResourceId."));
      action = QStringLiteral("recover");
    }
    const auto result = timeline_task_lifecycle_handler_(action, args.toVariantMap());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      auto error = error_response(result.value(QStringLiteral("error")).toString());
      error.insert(QStringLiteral("data"), QJsonObject::fromVariantMap(result));
      return error;
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("app_list_commands") || command == QStringLiteral("app_command_schema") ||
      command == QStringLiteral("app_invoke_command") || command == QStringLiteral("app_inspect_command")) {
    if (!app_commands_) return error_response(QStringLiteral("Native app command discovery is unavailable."));
    QVariantMap result;
    if (command == QStringLiteral("app_list_commands")) {
      if (!has_only_keys(args, {QStringLiteral("domain"), QStringLiteral("query"), QStringLiteral("offset"), QStringLiteral("limit")}) ||
          (args.contains("domain") && !args.value("domain").isString()) ||
          (args.contains("query") && !args.value("query").isString()) ||
          (args.contains("offset") && (!args.value("offset").isDouble() || args.value("offset").toDouble() < 0 ||
              args.value("offset").toDouble() > 1000000 || std::floor(args.value("offset").toDouble()) != args.value("offset").toDouble())) ||
          (args.contains("limit") && (!args.value("limit").isDouble() || args.value("limit").toDouble() < 1 ||
              args.value("limit").toDouble() > 64 || std::floor(args.value("limit").toDouble()) != args.value("limit").toDouble())))
        return argument_error_response(QStringLiteral("Discovery accepts domain/query strings, nonnegative integer offset and limit 1–64."));
      result = app_commands_->commands(args.value("domain").toString(), args.value("query").toString(),
          args.value("offset").toInt(), args.value("limit").toInt(24));
    } else {
      if (!args.value("commandId").isString() || !has_only_keys(args,
          command == QStringLiteral("app_command_schema") ? QStringList{"commandId"} : QStringList{"commandId", "values"}) ||
          (command != QStringLiteral("app_command_schema") && !args.value("values").isObject()))
        return argument_error_response(QStringLiteral("Supply a commandId and an object of typed values for invocation."));
      if (command == QStringLiteral("app_inspect_command") &&
          !app_commands_->schema(args.value("commandId").toString()).value("readOnly").toBool())
        return error_response(QStringLiteral("Read capability cannot invoke a mutable native control."));
      result = command == QStringLiteral("app_command_schema") ? app_commands_->schema(args.value("commandId").toString())
          : app_commands_->invoke(args.value("commandId").toString(), args.value("values").toObject().toVariantMap());
    }
    // Keep failure receipts as structured evidence, including any applied state
    // that the native journal captured before rejecting a postcondition.
    if (!result.value("ok").toBool()) {
      auto error = error_response(result.value("error").toString());
      error.insert("data", QJsonObject::fromVariantMap(result));
      return error;
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("photo_inspect_composition") || command == QStringLiteral("photo_edit_composition")) {
    if (!composition_handler_) return error_response(QStringLiteral("Open the photo editor to edit a composition."));
    if (!has_only_keys(args, {QStringLiteral("assetId"), QStringLiteral("operation"), QStringLiteral("layerId"), QStringLiteral("values")}) ||
        !args.value("assetId").isString() ||
        (args.contains("layerId") && !args.value("layerId").isString()) ||
        (args.contains("values") && !args.value("values").isObject()))
      return argument_error_response(QStringLiteral("Composition requires assetId and typed operation values."));
    auto request = args.toVariantMap();
    if (command == QStringLiteral("photo_inspect_composition")) {
      if (args.contains("operation") || args.contains("layerId"))
        return argument_error_response(QStringLiteral("Inspect accepts assetId and optional font family/face values."));
      if (args.contains("values")) {
        const auto values = args.value("values").toObject();
        if (values.size() > 2 || !values.contains("family") ||
            !values.value("family").isString() || values.value("family").toString().trimmed().isEmpty() ||
            (values.contains("face") && !values.value("face").isString()) ||
            (values.contains("face") && values.value("face").toString().trimmed().isEmpty()))
          return argument_error_response(QStringLiteral("Font inspection values require a non-empty family and optional face string."));
      }
      request.insert("operation", "inspect");
    } else if (!args.value("operation").isString() || args.value("operation").toString() == "inspect") {
      return argument_error_response(QStringLiteral("Use inspect_composition to read or supply an edit operation."));
    }
    if (command == QStringLiteral("photo_edit_composition") &&
        composition_layer_target_operations().contains(args.value("operation").toString()) &&
        args.value("layerId").toString().trimmed().isEmpty())
      return argument_error_response(QStringLiteral(
          "This operation requires arguments.layerId identifying an existing layer. Use an inspected layer ID or the createdLayerId from a completed creation call; coordinates in values do not identify a layer."));
    if (command == QStringLiteral("photo_edit_composition") && args.value("operation") == "typography" &&
        !AgentAppCommandRegistry::valueMatchesSchema(request.value("values"), agent_typography_schema()))
      return argument_error_response(QStringLiteral(
          "Typography values must match its partial-update schema: font, size, weight, color, spacing, path or material controls. Do not pass x or y; position a layer with the transform operation. Use numeric weight and preserve the existing layerId."));
    const auto result = composition_handler_(request);
    if (!result.value("ok").toBool()) {
      auto error = error_response(result.value("error").toString());
      error.insert("data", QJsonObject::fromVariantMap(result));
      return error;
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_scope_capture_request")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral(
          "timeline_scope_capture_request does not accept arguments."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->requestProgramScopeCaptureForAutomation();
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    auto payload = result;
    payload.remove(QStringLiteral("ok"));
    return ok_response(QJsonObject::fromVariantMap(payload));
  }
  if (command == QStringLiteral("timeline_preview_at_time")) {
    if (!has_only_keys(args, {QStringLiteral("resourceId"),
                              QStringLiteral("documentHash"),
                              QStringLiteral("seconds")}) ||
        !args.value(QStringLiteral("resourceId")).isString() ||
        !args.value(QStringLiteral("documentHash")).isString() ||
        !args.value(QStringLiteral("seconds")).isDouble()) {
      return argument_error_response(QStringLiteral(
          "timeline_preview_at_time requires only resourceId, documentHash, and numeric seconds."));
    }
    if (QCoreApplication::instance() == nullptr ||
        QThread::currentThread() != QCoreApplication::instance()->thread()) {
      return error_response(QStringLiteral(
          "Timeline preview navigation must run on the app GUI thread."));
    }
    if (timeline_document_.isNull() || timeline_playback_.isNull() ||
        !timeline_document_->hasActiveTimeline()) {
      return error_response(QStringLiteral(
          "Open an active Timeline with native playback before seeking its preview."));
    }

    const auto requested_resource_id = args.value(QStringLiteral("resourceId")).toString();
    const auto requested_document_hash = args.value(QStringLiteral("documentHash")).toString();
    const auto requested_seconds = args.value(QStringLiteral("seconds")).toDouble();
    const auto current_resource_id = timeline_document_->agentTaskDocumentId();
    const auto current_document_hash = timeline_document_->deterministicDocumentHash();
    const auto sequence_duration = timeline_document_->totalDurationSecondsValue();
    if (requested_resource_id.isEmpty() || requested_document_hash.isEmpty() ||
        requested_resource_id != current_resource_id ||
        requested_document_hash != current_document_hash) {
      return error_response(QStringLiteral(
          "Preview navigation requires the exact active Timeline resourceId and current documentHash."));
    }
    if (!std::isfinite(requested_seconds) || !std::isfinite(sequence_duration) ||
        sequence_duration < 0.0 || requested_seconds < 0.0 ||
        requested_seconds > sequence_duration) {
      return argument_error_response(QStringLiteral(
          "Preview seconds must be finite and within the active Timeline duration."));
    }
    const auto workspace_error = timeline_preview_workspace_error(
        qml_root_object_.data(), timeline_document_.data(), timeline_playback_.data());
    if (!workspace_error.isEmpty()) {
      return error_response(workspace_error);
    }

    auto* root_object = qml_root_object_.data();
    auto* workspace_loader = root_object == nullptr
        ? nullptr
        : root_object->findChild<QObject*>(
              QStringLiteral("timelineWorkspaceLoader"), Qt::FindChildrenRecursively);
    auto* workspace = workspace_loader == nullptr
        ? nullptr
        : workspace_loader->property("item").value<QObject*>();
    auto* viewport = workspace == nullptr
        ? nullptr
        : workspace->findChild<QQuickItem*>(
              QStringLiteral("timelineViewport"), Qt::FindChildrenRecursively);
    const auto zoom_value = workspace == nullptr
        ? QVariant{}
        : workspace->property("timelineZoom");
    const auto content_width_value = viewport == nullptr
        ? QVariant{}
        : viewport->property("contentWidth");
    const auto timeline_zoom = zoom_value.toDouble();
    const auto viewport_width = viewport == nullptr ? 0.0 : viewport->width();
    const auto viewport_content_width = content_width_value.toDouble();
    if (workspace == nullptr || viewport == nullptr || !zoom_value.isValid() ||
        !content_width_value.isValid() || !std::isfinite(timeline_zoom) ||
        timeline_zoom <= 0.0 || !std::isfinite(viewport_width) ||
        viewport_width <= 0.0 || !std::isfinite(viewport_content_width)) {
      return error_response(QStringLiteral(
          "The active Timeline workspace does not expose a valid playhead viewport."));
    }

    timeline_playback_->pause();
    timeline_playback_->seekSequenceSeconds(requested_seconds);

    // An explicit preview seek is also view navigation. Keep the paused
    // playhead visible in the Timeline ruler instead of changing only the
    // Program timecode while the viewport remains on an earlier chapter.
    const auto maximum_content_x =
        qMax(0.0, viewport_content_width - viewport_width);
    const auto playhead_x = requested_seconds * timeline_zoom;
    const auto requested_content_x = qBound(
        0.0, playhead_x - viewport_width / 2.0, maximum_content_x);
    viewport->setProperty("contentX", requested_content_x);
    const auto observed_content_x = viewport->property("contentX").toDouble();
    const auto playhead_visible = std::isfinite(observed_content_x) &&
        playhead_x >= observed_content_x - 0.5 &&
        playhead_x <= observed_content_x + viewport_width + 0.5;
    const auto viewport_navigation_applied =
        std::isfinite(observed_content_x) &&
        qAbs(observed_content_x - requested_content_x) <= 0.5 &&
        playhead_visible;

    const auto observed_resource_id = timeline_document_->agentTaskDocumentId();
    const auto observed_document_hash = timeline_document_->deterministicDocumentHash();
    const auto observed_seconds = timeline_document_->sequencePlayheadSeconds();
    const auto sequence_playing = timeline_document_->sequencePlaying();
    const auto navigation_applied = std::isfinite(observed_seconds) &&
        qAbs(observed_seconds - requested_seconds) <= 0.000001;
    const auto preview_state = timeline_playback_->playbackResourceDiagnostics();
    const auto receipt = QVariantMap{
        {QStringLiteral("resourceId"), observed_resource_id},
        {QStringLiteral("documentHash"), observed_document_hash},
        {QStringLiteral("requestedDocumentHash"), requested_document_hash},
        {QStringLiteral("observedDocumentHash"), observed_document_hash},
        {QStringLiteral("documentHashUnchanged"),
         observed_document_hash == requested_document_hash},
        {QStringLiteral("requestedSeconds"), requested_seconds},
        {QStringLiteral("observedPlayheadSeconds"), observed_seconds},
        {QStringLiteral("sequencePlaying"), sequence_playing},
        {QStringLiteral("playbackPaused"), !sequence_playing},
        {QStringLiteral("workspaceActive"), timeline_playback_->workspaceActive()},
        {QStringLiteral("navigationApplied"),
         navigation_applied && viewport_navigation_applied},
        {QStringLiteral("timelineViewportContentX"), observed_content_x},
        {QStringLiteral("timelineViewportWidth"), viewport_width},
        {QStringLiteral("timelineViewportPlayheadVisible"), playhead_visible},
        {QStringLiteral("timelineViewportNavigationApplied"), viewport_navigation_applied},
        {QStringLiteral("framePresentationVerified"), false},
        {QStringLiteral("requiresFramePresentationVerification"), true},
        {QStringLiteral("framePresentationStatus"), QStringLiteral("unverified")},
        {QStringLiteral("mediaReady"), timeline_playback_->mediaReady()},
        {QStringLiteral("playbackHealth"), timeline_playback_->playbackHealth()},
        {QStringLiteral("frameProcessingInFlight"),
         preview_state.value(QStringLiteral("frameProcessingInFlight"))},
        {QStringLiteral("nativePrerollActive"),
         preview_state.value(QStringLiteral("nativePrerollActive"))},
    };
    if (observed_resource_id != requested_resource_id ||
        observed_document_hash != requested_document_hash ||
        !std::isfinite(observed_seconds) || sequence_playing ||
        qAbs(observed_seconds - requested_seconds) > 0.000001 ||
        !viewport_navigation_applied) {
      auto response = error_response(QStringLiteral(
          "Native preview seek did not satisfy its exact resource, document, paused-state, playhead, and viewport postconditions."));
      response.insert(QStringLiteral("data"), QJsonObject::fromVariantMap(receipt));
      return response;
    }
    return ok_response(QJsonObject::fromVariantMap(receipt));
  }
  if (command == QStringLiteral("timeline_view_navigate")) {
    if (!has_only_keys(args, {QStringLiteral("resourceId"),
                              QStringLiteral("documentHash"),
                              QStringLiteral("action"),
                              QStringLiteral("preset")}) ||
        !args.value(QStringLiteral("resourceId")).isString() ||
        !args.value(QStringLiteral("documentHash")).isString() ||
        !args.value(QStringLiteral("action")).isString() ||
        (args.contains(QStringLiteral("preset")) &&
         !args.value(QStringLiteral("preset")).isString())) {
      return argument_error_response(QStringLiteral(
          "timeline_view_navigate accepts only resourceId, documentHash, action, and the conditional preset."));
    }
    const auto action = args.value(QStringLiteral("action")).toString();
    const auto preset = args.value(QStringLiteral("preset")).toString();
    const auto is_workspace_action = action == QStringLiteral("set_workspace_preset");
    if ((is_workspace_action &&
         (!args.contains(QStringLiteral("preset")) ||
          !QStringList{QStringLiteral("edit"), QStringLiteral("color"),
                       QStringLiteral("audio"), QStringLiteral("finish")}.contains(preset))) ||
        (!is_workspace_action && args.contains(QStringLiteral("preset"))) ||
        !QStringList{QStringLiteral("fit_sequence"), QStringLiteral("fit_selection"),
                     QStringLiteral("set_workspace_preset"), QStringLiteral("play"),
                     QStringLiteral("pause")}.contains(action)) {
      return argument_error_response(QStringLiteral(
          "Choose one supported Timeline navigation action; preset is required only for set_workspace_preset."));
    }
    if (QCoreApplication::instance() == nullptr ||
        QThread::currentThread() != QCoreApplication::instance()->thread()) {
      return error_response(QStringLiteral(
          "Timeline view navigation must run on the app GUI thread."));
    }
    if (timeline_document_.isNull() || timeline_playback_.isNull() ||
        !timeline_document_->hasActiveTimeline()) {
      return error_response(QStringLiteral(
          "Open an active Timeline with native playback before navigating its view."));
    }

    const auto requested_resource_id = args.value(QStringLiteral("resourceId")).toString();
    const auto requested_document_hash = args.value(QStringLiteral("documentHash")).toString();
    const auto current_resource_id = timeline_document_->agentTaskDocumentId();
    const auto current_document_hash = timeline_document_->deterministicDocumentHash();
    if (requested_resource_id.isEmpty() || requested_document_hash.isEmpty() ||
        requested_resource_id != current_resource_id ||
        requested_document_hash != current_document_hash) {
      return error_response(QStringLiteral(
          "View navigation requires the exact active Timeline resourceId and current documentHash."));
    }
    const auto workspace_error = timeline_preview_workspace_error(
        qml_root_object_.data(), timeline_document_.data(), timeline_playback_.data());
    if (!workspace_error.isEmpty()) {
      return error_response(workspace_error);
    }
    auto* root_object = qml_root_object_.data();
    auto* workspace_loader = root_object->findChild<QObject*>(
        QStringLiteral("timelineWorkspaceLoader"), Qt::FindChildrenRecursively);
    auto* workspace = workspace_loader == nullptr
                          ? nullptr
                          : workspace_loader->property("item").value<QObject*>();
    auto* viewport = workspace == nullptr
                         ? nullptr
                         : workspace->findChild<QQuickItem*>(
                               QStringLiteral("timelineViewport"),
                               Qt::FindChildrenRecursively);
    if (workspace == nullptr || viewport == nullptr) {
      return error_response(QStringLiteral(
          "The active Timeline view does not expose its native workspace and viewport."));
    }
    if ((action == QStringLiteral("fit_sequence") ||
         action == QStringLiteral("fit_selection")) &&
        timeline_document_->totalDurationSecondsValue() <= 0.0) {
      return error_response(QStringLiteral(
          "Fit navigation requires a Timeline with positive duration."));
    }
    if (action == QStringLiteral("play") &&
        timeline_document_->totalDurationSecondsValue() <= 0.0) {
      return error_response(QStringLiteral(
          "Play navigation requires a Timeline with positive duration."));
    }

    const auto fit_action = action == QStringLiteral("fit_sequence") ||
        action == QStringLiteral("fit_selection");
    auto fit_target_start = 0.0;
    auto fit_target_end = timeline_document_->totalDurationSecondsValue();
    const auto frame_tolerance = 1.0 / qMax(
        1.0, timeline_document_->property("framesPerSecond").toDouble());
    if (action == QStringLiteral("fit_selection") &&
        timeline_document_->property("currentIndex").toInt() >= 0) {
      const auto current_clip = timeline_document_->property("currentClip").toMap();
      const auto clip_start = current_clip.value(QStringLiteral("startSeconds")).toDouble();
      const auto clip_duration = current_clip.value(
          QStringLiteral("trimmedDurationSeconds")).toDouble();
      if (!current_clip.contains(QStringLiteral("startSeconds")) ||
          !current_clip.contains(QStringLiteral("trimmedDurationSeconds")) ||
          !std::isfinite(clip_start) || !std::isfinite(clip_duration) ||
          clip_duration < 0.0) {
        return error_response(QStringLiteral(
            "The current Timeline selection has no valid fit range."));
      }
      fit_target_start = qMax(0.0, clip_start);
      fit_target_end = fit_target_start + qMax(frame_tolerance, clip_duration);
    }
    const auto viewport_width_before = viewport->width();
    if (fit_action && (!std::isfinite(viewport_width_before) ||
                       viewport_width_before <= 0.0 ||
                       !std::isfinite(frame_tolerance) || frame_tolerance <= 0.0)) {
      return error_response(QStringLiteral(
          "Fit navigation requires a positive native Timeline viewport and frame rate."));
    }

    bool invocation_succeeded = false;
    bool navigation_applied = false;
    if (action == QStringLiteral("fit_sequence")) {
      invocation_succeeded = QMetaObject::invokeMethod(
          workspace, "zoomToFit", Qt::DirectConnection);
      navigation_applied = invocation_succeeded;
    } else if (action == QStringLiteral("fit_selection")) {
      invocation_succeeded = QMetaObject::invokeMethod(
          workspace, "zoomToSelection", Qt::DirectConnection);
      navigation_applied = invocation_succeeded;
    } else if (is_workspace_action) {
      QVariant accepted;
      invocation_succeeded = QMetaObject::invokeMethod(
          workspace, "applyTimelineLayoutPreset", Qt::DirectConnection,
          Q_RETURN_ARG(QVariant, accepted), Q_ARG(QVariant, QVariant(preset)));
      navigation_applied = invocation_succeeded && accepted.toBool() &&
          workspace->property("timelineLayoutPreset").toString() == preset;
    } else if (action == QStringLiteral("play")) {
      timeline_playback_->play();
      invocation_succeeded = true;
      navigation_applied = timeline_document_->sequencePlaying() ||
          timeline_playback_->playbackStartPending();
    } else if (action == QStringLiteral("pause")) {
      timeline_playback_->pause();
      invocation_succeeded = true;
      navigation_applied = !timeline_document_->sequencePlaying() &&
          !timeline_playback_->playbackStartPending();
    }

    const auto observed_resource_id = timeline_document_->agentTaskDocumentId();
    const auto observed_document_hash = timeline_document_->deterministicDocumentHash();
    const auto sequence_playing = timeline_document_->sequencePlaying();
    const auto observed_preset = workspace->property("timelineLayoutPreset").toString();
    const auto observed_zoom = workspace->property("timelineZoom");
    const auto horizontal_offset = viewport->property("contentX");
    const auto vertical_offset = viewport->property("contentY");
    const auto visible_start_seconds = observed_zoom.toDouble() > 0.0
        ? horizontal_offset.toDouble() / observed_zoom.toDouble()
        : std::numeric_limits<double>::quiet_NaN();
    const auto visible_end_seconds = observed_zoom.toDouble() > 0.0
        ? (horizontal_offset.toDouble() + viewport->width()) / observed_zoom.toDouble()
        : std::numeric_limits<double>::quiet_NaN();
    const auto readback_valid = observed_zoom.isValid() &&
        std::isfinite(observed_zoom.toDouble()) && horizontal_offset.isValid() &&
        std::isfinite(horizontal_offset.toDouble()) && vertical_offset.isValid() &&
        std::isfinite(vertical_offset.toDouble()) && !observed_preset.isEmpty();
    navigation_applied = navigation_applied && readback_valid;
    if (fit_action) {
      const auto fit_range_valid = std::isfinite(visible_start_seconds) &&
          std::isfinite(visible_end_seconds) && observed_zoom.toDouble() > 0.0 &&
          viewport->width() > 0.0 &&
          visible_start_seconds <= fit_target_start + frame_tolerance &&
          visible_end_seconds >= fit_target_end - frame_tolerance;
      navigation_applied = navigation_applied && fit_range_valid;
    }
    const auto hash_unchanged = observed_document_hash == requested_document_hash;
    const auto receipt = QVariantMap{
        {QStringLiteral("resourceId"), observed_resource_id},
        {QStringLiteral("documentHash"), observed_document_hash},
        {QStringLiteral("requestedDocumentHash"), requested_document_hash},
        {QStringLiteral("documentHashUnchanged"), hash_unchanged},
        {QStringLiteral("action"), action},
        {QStringLiteral("preset"), observed_preset},
        {QStringLiteral("timelineZoom"), observed_zoom},
        {QStringLiteral("horizontalOffset"), horizontal_offset},
        {QStringLiteral("verticalOffset"), vertical_offset},
        {QStringLiteral("viewportWidth"), viewport->width()},
        {QStringLiteral("visibleStartSeconds"), visible_start_seconds},
        {QStringLiteral("visibleEndSeconds"), visible_end_seconds},
        {QStringLiteral("fitTargetStartSeconds"), fit_target_start},
        {QStringLiteral("fitTargetEndSeconds"), fit_target_end},
        {QStringLiteral("fitToleranceSeconds"), frame_tolerance},
        {QStringLiteral("playheadSeconds"), timeline_document_->sequencePlayheadSeconds()},
        {QStringLiteral("sequencePlaying"), sequence_playing},
        {QStringLiteral("playbackStartPending"), timeline_playback_->playbackStartPending()},
        {QStringLiteral("workspaceActive"), timeline_playback_->workspaceActive()},
        {QStringLiteral("navigationApplied"), navigation_applied},
        {QStringLiteral("framePresentationVerified"), false},
        {QStringLiteral("framePresentationStatus"), QStringLiteral("unverified")},
    };
    if (!invocation_succeeded || !navigation_applied ||
        observed_resource_id != requested_resource_id || !hash_unchanged) {
      auto response = error_response(QStringLiteral(
          "Native Timeline navigation did not satisfy its exact action, resource, and unchanged-document postconditions."));
      response.insert(QStringLiteral("data"), QJsonObject::fromVariantMap(receipt));
      return response;
    }
    return ok_response(QJsonObject::fromVariantMap(receipt));
  }
  if (command == QStringLiteral("timeline_visible_proof_capture")) {
    if (!has_only_keys(args, {QStringLiteral("captureId")}) ||
        (args.contains(QStringLiteral("captureId")) &&
         !args.value(QStringLiteral("captureId")).isString())) {
      return error_response(QStringLiteral(
          "timeline_visible_proof_capture accepts only an optional captureId string."));
    }
    const auto capture_id = args.value(QStringLiteral("captureId")).toString();
    if (args.contains(QStringLiteral("captureId")) && capture_id.trimmed().isEmpty()) {
      return argument_error_response(QStringLiteral(
          "Provide the captureId returned by timeline.capture_visible_proof."));
    }
    const auto result = captureVisibleTimelineProof(capture_id);
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    auto payload = result;
    payload.remove(QStringLiteral("ok"));
    return ok_response(QJsonObject::fromVariantMap(payload));
  }
  if (command == QStringLiteral("timeline_scope_summary_inspect")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral(
          "timeline_scope_summary_inspect does not accept arguments."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    auto result = timeline_document_->programColorScopeSummaryForAutomation();
    result.remove(QStringLiteral("ok"));
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_playback_performance_inspect")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral(
          "timeline_playback_performance_inspect does not accept arguments."));
    }
    if (timeline_playback_.isNull()) {
      return error_response(QStringLiteral("Timeline playback is unavailable."));
    }
    auto frame_metrics = timeline_playback_->frameProcessingMetrics();
    const auto sample_count = frame_metrics.value(QStringLiteral("sampleCount")).toLongLong();
    const auto measured_source_key =
        frame_metrics.take(QStringLiteral("sourceKey")).toString();
    const auto result = QJsonObject{
        {QStringLiteral("hasVideoSource"), timeline_playback_->hasVideoSource()},
        {QStringLiteral("mediaReady"), timeline_playback_->mediaReady()},
        {QStringLiteral("playbackHealth"), timeline_playback_->playbackHealth()},
        {QStringLiteral("previewQualityMode"), timeline_playback_->previewQualityMode()},
        {QStringLiteral("effectivePreviewQuality"),
         timeline_playback_->effectivePreviewQuality()},
        {QStringLiteral("effectivePreviewScale"),
         timeline_playback_->effectivePreviewScale()},
        {QStringLiteral("usingProxy"), timeline_playback_->usingProxy()},
        {QStringLiteral("usingRenderCache"), timeline_playback_->usingRenderCache()},
        {QStringLiteral("adaptiveQualityActive"),
         timeline_playback_->adaptiveQualityActive()},
        {QStringLiteral("adaptiveDetailLevel"),
         timeline_playback_->adaptiveDetailLevel()},
        {QStringLiteral("adaptiveQualityReason"),
         timeline_playback_->adaptiveQualityReason()},
        {QStringLiteral("droppedFrameCount"), timeline_playback_->droppedFrameCount()},
        {QStringLiteral("hasFrameTimingSample"), sample_count > 0},
        {QStringLiteral("frameTimingSampleCount"), static_cast<qint64>(sample_count)},
        {QStringLiteral("measuredSourceKey"), measured_source_key},
        {QStringLiteral("frameTimingSmoothing"),
         QStringLiteral("EWMA with alpha 0.15 over completed processed frames")},
        {QStringLiteral("frameProcessingMetrics"),
         QJsonObject::fromVariantMap(frame_metrics)},
        {QStringLiteral("colorCacheDiagnostics"),
         QJsonObject::fromVariantMap(
             timeline_playback_->colorCacheDiagnostics())},
        {QStringLiteral("cacheCounterScope"),
         QStringLiteral("Cumulative since process start; independent of playback diagnostic resets.")},
        {QStringLiteral("timingScope"),
         QStringLiteral("Worker-side video frame conversion and color/effect processing; excludes decoder wait and display presentation.")},
    };
    return ok_response(result);
  }
  if (command == QStringLiteral("timeline_render_geometry_inspect")) {
    if (!has_only_keys(args, {QStringLiteral("clipId")}) ||
        !args.value(QStringLiteral("clipId")).isString()) {
      return error_response(QStringLiteral(
          "timeline_render_geometry_inspect requires only a clipId string."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto clip_id = args.value(QStringLiteral("clipId")).toString().trimmed();
    if (clip_id.isEmpty() || clip_id.size() > 128) {
      return argument_error_response(QStringLiteral(
          "Provide a stable Timeline clipId of at most 128 characters."));
    }
    const auto clip_detail = timeline_document_->inspectTimelineClipById(clip_id);
    if (!clip_detail.value(QStringLiteral("ok")).toBool()) {
      return error_response(QStringLiteral("The requested Timeline clip is not in the active document."));
    }
    const auto geometry = inspectTimelineRenderGeometry(clip_id);
    if (!geometry.value(QStringLiteral("ok")).toBool()) {
      return error_response(geometry.value(QStringLiteral("error")).toString());
    }
    auto payload = geometry;
    payload.remove(QStringLiteral("ok"));
    return ok_response(QJsonObject::fromVariantMap(payload));
  }
  if (command == QStringLiteral("timeline_delivery_preflight_inspect")) {
    if (!has_only_keys(args, {QStringLiteral("preset")}) ||
        (args.contains(QStringLiteral("preset")) && !args.value(QStringLiteral("preset")).isString())) {
      return error_response(QStringLiteral(
          "timeline_delivery_preflight_inspect accepts only an optional preset string."));
    }
    const auto preset = args.value(QStringLiteral("preset")).toString().trimmed().toLower();
    if (!preset.isEmpty() && !QStringList{
            QStringLiteral("instagram"), QStringLiteral("youtube"),
            QStringLiteral("youtube_shorts"), QStringLiteral("tiktok")}.contains(preset)) {
      return argument_error_response(QStringLiteral(
          "Use instagram, youtube, youtube_shorts, or tiktok for the delivery preset."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline delivery inspection is unavailable."));
    }
    // The inner `ok` reports readiness; the outer MCP response reports that
    // the read-only inspection itself succeeded.
    return ok_response(QJsonObject::fromVariantMap(
        timeline_document_->timelineDeliveryPreflight(preset)));
  }
  if (command == QStringLiteral("timeline_render_jobs_inspect")) {
    if (!has_only_keys(args, {})) {
      return error_response(QStringLiteral(
          "timeline_render_jobs_inspect accepts no arguments."));
    }
    if (timeline_document_.isNull() || timeline_job_queue_.isNull()) {
      return error_response(QStringLiteral(
          "Timeline render queue inspection is unavailable."));
    }
    const auto resource_id = timeline_document_->agentTaskDocumentId();
    if (!resource_id.startsWith(QStringLiteral("timeline:"))) {
      return error_response(QStringLiteral(
          "Open a Timeline before inspecting its render queue."));
    }
    const auto timeline_id = resource_id.mid(QStringLiteral("timeline:").size());
    const auto document_path = QFileInfo(timeline_document_->savedDocumentPath()).canonicalFilePath();
    const auto saved_render = QFileInfo(timeline_document_->savedRenderPath());
    const auto render_directory = saved_render.dir().canonicalPath();
    const auto timeline_status = timeline_document_->status();
    constexpr int kMaximumJobs = 20;
    QVariantList jobs;
    jobs.reserve(kMaximumJobs);
    auto matching_job_count = 0;
    QVariantMap latest_failure;
    for (int row = 0; row < timeline_job_queue_->rowCount(); ++row) {
      const auto index = timeline_job_queue_->index(row, 0);
      const auto job_kind = timeline_job_queue_->data(
          index, JobQueueModel::JobKindRole).toString();
      const auto asset_id = timeline_job_queue_->data(
          index, JobQueueModel::AssetIdRole).toString();
      const auto metadata = timeline_job_queue_->data(
          index, JobQueueModel::MetadataRole).toMap();
      if (job_kind != QStringLiteral("timeline_render") || asset_id != resource_id ||
          metadata.value(QStringLiteral("timelineId")).toString() != timeline_id) {
        continue;
      }
      ++matching_job_count;
      const auto job_id = timeline_job_queue_->data(index, JobQueueModel::JobIdRole);
      const auto status = timeline_job_queue_->data(index, JobQueueModel::StatusRole);
      const auto error_message = timeline_job_queue_->data(index, JobQueueModel::ErrorMessageRole);
      if (latest_failure.isEmpty() &&
          status.toString() == QStringLiteral("Failed")) {
        latest_failure.insert(QStringLiteral("jobId"), job_id);
        latest_failure.insert(QStringLiteral("status"), status);
        latest_failure.insert(QStringLiteral("errorMessage"), error_message);
      }
      if (jobs.size() >= kMaximumJobs) continue;

      QVariantMap safe_job{
          {QStringLiteral("jobId"), job_id},
          {QStringLiteral("status"), status},
          {QStringLiteral("stage"), timeline_job_queue_->data(index, JobQueueModel::StageRole)},
          {QStringLiteral("progress"), timeline_job_queue_->data(index, JobQueueModel::ProgressRole)},
          {QStringLiteral("progressKnown"), true},
          {QStringLiteral("errorMessage"), error_message},
          {QStringLiteral("presetName"), timeline_job_queue_->data(index, JobQueueModel::PresetNameRole)},
          {QStringLiteral("createdAt"), timeline_job_queue_->data(index, JobQueueModel::CreatedAtRole)},
          {QStringLiteral("updatedAt"), timeline_job_queue_->data(index, JobQueueModel::UpdatedAtRole)}};
      const auto source = QFileInfo(
          timeline_job_queue_->data(index, JobQueueModel::SourcePathRole).toString());
      if (!document_path.isEmpty() && !source.isSymLink() &&
          source.canonicalFilePath() == document_path) {
        safe_job.insert(QStringLiteral("documentPath"), document_path);
      }
      const auto output = QFileInfo(
          timeline_job_queue_->data(index, JobQueueModel::OutputPathRole).toString());
      const auto output_parent = output.dir().canonicalPath();
      const auto output_name = output.fileName();
      if (!timeline_document_->savedRenderPath().isEmpty() &&
          !render_directory.isEmpty() && !output.isSymLink() &&
          output_parent == render_directory &&
          (output_name == saved_render.fileName() ||
           output_name.startsWith(timeline_id + QLatin1Char('-')))) {
        safe_job.insert(
            QStringLiteral("outputPath"),
            output.exists() ? output.canonicalFilePath()
                            : QDir(output_parent).filePath(output_name));
      }
      jobs.push_back(safe_job);
    }
    QString submission_error;
    const auto status_lower = timeline_status.toLower();
    if (status_lower.contains(QStringLiteral("error")) ||
        status_lower.contains(QStringLiteral("failed")) ||
        status_lower.contains(QStringLiteral("unable")) ||
        status_lower.contains(QStringLiteral("could not"))) {
      submission_error = timeline_status;
    }
    return ok_response(QJsonObject{
        {QStringLiteral("timelineId"), timeline_id},
        {QStringLiteral("resourceId"), resource_id},
        {QStringLiteral("documentPath"), document_path.isEmpty() ? QString{} : document_path},
        {QStringLiteral("timelineStatus"), timeline_status},
        {QStringLiteral("saveInProgress"), timeline_document_->timelineSaveInProgress()},
        {QStringLiteral("submissionError"), submission_error},
        {QStringLiteral("queueStatus"), timeline_job_queue_->status()},
        {QStringLiteral("queueLoading"), timeline_job_queue_->loading()},
        {QStringLiteral("queueHistoryError"), timeline_job_queue_->historyError()},
        {QStringLiteral("jobCount"), matching_job_count},
        {QStringLiteral("truncated"), matching_job_count > jobs.size()},
        {QStringLiteral("jobs"), QJsonArray::fromVariantList(jobs)},
        {QStringLiteral("latestRenderFailure"), QJsonObject::fromVariantMap(latest_failure)},
        {QStringLiteral("jobListLimit"), kMaximumJobs}});
  }
  if (command == QStringLiteral("timeline_h264_export_enqueue")) {
    return enqueueTimelineH264Export(args);
  }
  if (command == QStringLiteral("timeline_h264_export_inspect")) {
    return inspectTimelineH264Export(args);
  }
  if (command == QStringLiteral("timeline_h264_export_cancel")) {
    return cancelTimelineH264Export(args);
  }
  if (command == QStringLiteral("timeline_color_inspect")) {
    if (!has_only_keys(args, {QStringLiteral("clipId")}) ||
        !args.value(QStringLiteral("clipId")).isString()) {
      return error_response(QStringLiteral("timeline_color_inspect requires only a clipId string."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->inspectClipColorForAutomation(
        args.value(QStringLiteral("clipId")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_compare_open")) {
    const auto has_sequence_seconds = args.contains(QStringLiteral("sequenceSeconds"));
    const auto has_mode = args.contains(QStringLiteral("mode"));
    if (!has_only_keys(
            args,
            {QStringLiteral("clipId"), QStringLiteral("sequenceSeconds"),
             QStringLiteral("mode")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        (has_sequence_seconds && !args.value(QStringLiteral("sequenceSeconds")).isDouble()) ||
        (has_mode && !args.value(QStringLiteral("mode")).isString())) {
      return error_response(QStringLiteral(
          "timeline_color_compare_open requires clipId and optional numeric sequenceSeconds and string mode."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->openProgramComparisonForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        has_sequence_seconds ? args.value(QStringLiteral("sequenceSeconds")).toDouble() : -1.0,
        has_mode ? args.value(QStringLiteral("mode")).toString() : QStringLiteral("wipe"));
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    auto payload = result;
    payload.remove(QStringLiteral("ok"));
    return ok_response(QJsonObject::fromVariantMap(payload));
  }
  if (command == QStringLiteral("timeline_source_color_metadata_inspect")) {
    if (!has_only_keys(args, {QStringLiteral("clipId")}) ||
        !args.value(QStringLiteral("clipId")).isString()) {
      return error_response(QStringLiteral(
          "timeline_source_color_metadata_inspect requires only a clipId string."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->inspectClipSourceColorMetadataForAutomation(
        args.value(QStringLiteral("clipId")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_effects_inspect")) {
    if (!has_only_keys(args, {QStringLiteral("clipId")}) ||
        !args.value(QStringLiteral("clipId")).isString()) {
      return error_response(QStringLiteral("timeline_effects_inspect requires only a clipId string."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->inspectClipEffectsForAutomation(
        args.value(QStringLiteral("clipId")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_grade_inspect")) {
    if (!has_only_keys(args, {QStringLiteral("clipId")}) ||
        !args.value(QStringLiteral("clipId")).isString()) {
      return error_response(QStringLiteral(
          "timeline_color_grade_inspect requires only a clipId string."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->inspectClipColorGradeForAutomation(
        args.value(QStringLiteral("clipId")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_grade_set")) {
    if (!has_only_keys(args, {QStringLiteral("clipId"), QStringLiteral("recipe")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("recipe")).isObject()) {
      return error_response(QStringLiteral(
          "timeline_color_grade_set requires a clipId string and recipe object."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->setClipColorGradeRecipeForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("recipe")).toObject());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_grade_layer_edit")) {
    const auto allowed_keys = QStringList{
        QStringLiteral("clipId"), QStringLiteral("operation"),
        QStringLiteral("layerId"), QStringLiteral("layer"),
        QStringLiteral("position"), QStringLiteral("enabled"),
        QStringLiteral("opacity"), QStringLiteral("parameter"),
        QStringLiteral("value"), QStringLiteral("blendMode")};
    if (!has_only_keys(args, allowed_keys) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("operation")).isString()) {
      return error_response(QStringLiteral(
          "timeline_color_grade_layer_edit requires clipId and operation strings."));
    }
    const auto operation =
        args.value(QStringLiteral("operation")).toString().trimmed().toLower();
    const auto supported_operation =
        operation == QStringLiteral("add") ||
        operation == QStringLiteral("remove") ||
        operation == QStringLiteral("move") ||
        operation == QStringLiteral("replace") ||
        operation == QStringLiteral("set_enabled") ||
        operation == QStringLiteral("set_opacity") ||
        operation == QStringLiteral("set_blend_mode") ||
        operation == QStringLiteral("set_parameter");
    const auto needs_layer_id = operation != QStringLiteral("add");
    const auto needs_layer = operation == QStringLiteral("add") ||
                             operation == QStringLiteral("replace");
    const auto needs_position = operation == QStringLiteral("move");
    const auto needs_enabled = operation == QStringLiteral("set_enabled");
    const auto needs_opacity = operation == QStringLiteral("set_opacity");
    const auto needs_blend_mode = operation == QStringLiteral("set_blend_mode");
    const auto needs_parameter = operation == QStringLiteral("set_parameter");
    if (!supported_operation ||
        args.contains(QStringLiteral("layerId")) != needs_layer_id ||
        args.contains(QStringLiteral("layer")) != needs_layer ||
        args.contains(QStringLiteral("enabled")) != needs_enabled ||
        args.contains(QStringLiteral("opacity")) != needs_opacity ||
        args.contains(QStringLiteral("blendMode")) != needs_blend_mode ||
        args.contains(QStringLiteral("parameter")) != needs_parameter ||
        args.contains(QStringLiteral("value")) != needs_parameter ||
        (needs_position && !args.contains(QStringLiteral("position"))) ||
        (!needs_position && operation != QStringLiteral("add") &&
         args.contains(QStringLiteral("position")))) {
      return error_response(QStringLiteral(
          "Provide layerId for remove, move, replace, and per-layer edits; layer for add or replace; position for move and optionally add; enabled only for set_enabled; opacity only for set_opacity; blendMode only for set_blend_mode; parameter and value only for set_parameter."));
    }
    if ((needs_layer_id && !args.value(QStringLiteral("layerId")).isString()) ||
        (needs_layer && !args.value(QStringLiteral("layer")).isObject()) ||
        (needs_enabled && !args.value(QStringLiteral("enabled")).isBool()) ||
        (needs_opacity && !args.value(QStringLiteral("opacity")).isDouble()) ||
        (needs_blend_mode && !args.value(QStringLiteral("blendMode")).isString()) ||
        (needs_parameter &&
         (!args.value(QStringLiteral("parameter")).isString() ||
          !args.value(QStringLiteral("value")).isDouble()))) {
      return error_response(QStringLiteral(
          "layerId, layer, enabled, opacity, blendMode, or parameter has the wrong JSON type."));
    }
    auto position = -1;
    if (args.contains(QStringLiteral("position"))) {
      const auto position_value = args.value(QStringLiteral("position"));
      if (!position_value.isDouble() ||
          position_value.toDouble() != static_cast<double>(position_value.toInt())) {
        return error_response(QStringLiteral("position must be an integer."));
      }
      position = position_value.toInt();
      if (position < 0) {
        return error_response(QStringLiteral("position cannot be negative."));
      }
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->editClipColorGradeLayerForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        operation,
        args.value(QStringLiteral("layerId")).toString(),
        args.value(QStringLiteral("layer")).toObject().toVariantMap(),
        position,
        args.value(QStringLiteral("enabled")).toBool(),
        args.value(QStringLiteral("opacity")).toDouble(),
        args.value(QStringLiteral("parameter")).toString(),
        args.value(QStringLiteral("value")).toDouble(),
        args.value(QStringLiteral("blendMode")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_grade_parameter_set")) {
    if (!has_only_keys(args, {QStringLiteral("clipId"), QStringLiteral("parameter"),
                              QStringLiteral("value")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("parameter")).isString() ||
        !args.value(QStringLiteral("value")).isDouble()) {
      return error_response(QStringLiteral(
          "timeline_color_grade_parameter_set requires clipId, parameter, and numeric value."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->setClipColorGradeParameterForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("parameter")).toString(),
        args.value(QStringLiteral("value")).toDouble());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_grade_curve_set")) {
    if (!has_only_keys(args, {QStringLiteral("clipId"), QStringLiteral("layerId"),
                              QStringLiteral("channel"), QStringLiteral("points")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("channel")).isString() ||
        !args.value(QStringLiteral("points")).isArray() ||
        (args.contains(QStringLiteral("layerId")) &&
         !args.value(QStringLiteral("layerId")).isString())) {
      return error_response(QStringLiteral(
          "timeline_color_grade_curve_set requires clipId, channel, and points, with an optional layerId."));
    }
    const auto points = args.value(QStringLiteral("points")).toArray();
    if (points.size() > 64) {
      return error_response(QStringLiteral("points cannot contain more than 64 entries."));
    }
    auto curve_points = QVariantList{};
    curve_points.reserve(points.size());
    for (const auto& point_value : points) {
      if (!point_value.isObject()) {
        return error_response(QStringLiteral("Every curve point must be an object."));
      }
      const auto point = point_value.toObject();
      if (!has_only_keys(point, {QStringLiteral("x"), QStringLiteral("y")}) ||
          !point.value(QStringLiteral("x")).isDouble() ||
          !point.value(QStringLiteral("y")).isDouble()) {
        return error_response(QStringLiteral(
            "Every curve point must contain only numeric x and y values."));
      }
      curve_points.push_back(point.toVariantMap());
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->setClipColorGradeCurveForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("layerId")).toString(),
        args.value(QStringLiteral("channel")).toString(), curve_points);
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_grade_clear")) {
    if (!has_only_keys(args, {QStringLiteral("clipId")}) ||
        !args.value(QStringLiteral("clipId")).isString()) {
      return error_response(QStringLiteral(
          "timeline_color_grade_clear requires only a clipId string."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->clearClipColorGradeRecipeForAutomation(
        args.value(QStringLiteral("clipId")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_adjustment_clip_add")) {
    if (!has_only_keys(args, {QStringLiteral("startSeconds"),
                              QStringLiteral("durationSeconds")}) ||
        !args.value(QStringLiteral("startSeconds")).isDouble() ||
        !args.value(QStringLiteral("durationSeconds")).isDouble() ||
        args.value(QStringLiteral("startSeconds")).toDouble() < 0.0 ||
        args.value(QStringLiteral("durationSeconds")).toDouble() <= 0.0) {
      return error_response(QStringLiteral(
          "timeline_adjustment_clip_add requires a nonnegative startSeconds and positive durationSeconds."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->addAdjustmentClipForAutomation(
        args.value(QStringLiteral("startSeconds")).toDouble(),
        args.value(QStringLiteral("durationSeconds")).toDouble());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_clip_opacity_set")) {
    if (!has_only_keys(args, {QStringLiteral("clipId"), QStringLiteral("opacity")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("opacity")).isDouble()) {
      return error_response(QStringLiteral(
          "timeline_clip_opacity_set requires a clipId string and numeric opacity."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->setClipOpacityForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("opacity")).toDouble());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_clip_keyframe_edit")) {
    const auto allowed_keys = QStringList{
        QStringLiteral("clipId"), QStringLiteral("operation"),
        QStringLiteral("parameter"), QStringLiteral("localSeconds"),
        QStringLiteral("value"), QStringLiteral("interpolation")};
    if (!has_only_keys(args, allowed_keys) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("operation")).isString() ||
        !args.value(QStringLiteral("parameter")).isString() ||
        !args.value(QStringLiteral("localSeconds")).isDouble()) {
      return error_response(QStringLiteral(
          "timeline_clip_keyframe_edit requires clipId, operation, parameter, and numeric localSeconds."));
    }
    const auto operation =
        args.value(QStringLiteral("operation")).toString().trimmed().toLower();
    const auto is_set = operation == QStringLiteral("set");
    const auto is_remove = operation == QStringLiteral("remove");
    if ((!is_set && !is_remove) ||
        args.contains(QStringLiteral("value")) != is_set ||
        (args.contains(QStringLiteral("interpolation")) &&
         (!is_set || !args.value(QStringLiteral("interpolation")).isString())) ||
        (is_set && !args.value(QStringLiteral("value")).isDouble()) ||
        args.value(QStringLiteral("localSeconds")).toDouble() < 0.0) {
      return error_response(QStringLiteral(
          "Use set with a numeric value and optional interpolation, or remove without either field; localSeconds must be nonnegative."));
    }
    if (args.contains(QStringLiteral("interpolation"))) {
      const auto interpolation = args.value(QStringLiteral("interpolation"))
                                     .toString()
                                     .trimmed()
                                     .toLower();
      if (!QStringList{QStringLiteral("hold"), QStringLiteral("linear"),
                       QStringLiteral("ease-in"), QStringLiteral("ease-out"),
                       QStringLiteral("ease-in-out"), QStringLiteral("bezier")}
               .contains(interpolation)) {
        return error_response(QStringLiteral("Unsupported keyframe interpolation."));
      }
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->editClipKeyframeForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        operation,
        args.value(QStringLiteral("parameter")).toString(),
        args.value(QStringLiteral("localSeconds")).toDouble(),
        args.value(QStringLiteral("value")).toDouble(),
        args.value(QStringLiteral("interpolation")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_effects_edit")) {
    const auto allowed_keys = QStringList{
        QStringLiteral("clipId"), QStringLiteral("operation"),
        QStringLiteral("effectName"), QStringLiteral("amount"),
        QStringLiteral("enabled")};
    if (!has_only_keys(args, allowed_keys) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("operation")).isString()) {
      return error_response(QStringLiteral(
          "timeline_effects_edit requires clipId and operation strings and accepts only effectName, amount, and enabled as optional fields."));
    }
    const auto operation = args.value(QStringLiteral("operation")).toString().trimmed().toLower();
    const auto effect_operation =
        operation == QStringLiteral("add") || operation == QStringLiteral("remove") ||
        operation == QStringLiteral("move_up") || operation == QStringLiteral("move_down") ||
        operation == QStringLiteral("set_amount") || operation == QStringLiteral("set_enabled");
    if (args.contains(QStringLiteral("effectName")) &&
        !args.value(QStringLiteral("effectName")).isString()) {
      return error_response(QStringLiteral("effectName must be a string."));
    }
    if (args.contains(QStringLiteral("amount")) &&
        !args.value(QStringLiteral("amount")).isDouble()) {
      return error_response(QStringLiteral("amount must be a number."));
    }
    if (args.contains(QStringLiteral("enabled")) &&
        !args.value(QStringLiteral("enabled")).isBool()) {
      return error_response(QStringLiteral("enabled must be a boolean."));
    }
    const auto has_effect_name = args.contains(QStringLiteral("effectName"));
    const auto has_amount = args.contains(QStringLiteral("amount"));
    const auto has_enabled = args.contains(QStringLiteral("enabled"));
    const auto amount_expected = operation == QStringLiteral("set_amount");
    const auto enabled_expected = operation == QStringLiteral("set_enabled") ||
                                  operation == QStringLiteral("set_stack_enabled");
    if (effect_operation != has_effect_name || amount_expected != has_amount ||
        enabled_expected != has_enabled) {
      return error_response(QStringLiteral(
          "Provide effectName for add, remove, move, and per-effect operations; provide amount only for set_amount and enabled only for set_enabled or set_stack_enabled."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->editClipEffectStackForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        operation,
        args.value(QStringLiteral("effectName")).toString(),
        args.value(QStringLiteral("amount")).toDouble(),
        args.value(QStringLiteral("enabled")).toBool());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_set_video_input_transform")) {
    if (!has_only_keys(args, {QStringLiteral("clipId"), QStringLiteral("profile")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("profile")).isString()) {
      return error_response(QStringLiteral(
          "timeline_color_set_video_input_transform requires clipId and profile strings."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->setClipVideoInputTransformForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("profile")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_set_lut")) {
    if (!has_only_keys(args, {QStringLiteral("clipId"), QStringLiteral("filePath")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("filePath")).isString()) {
      return error_response(QStringLiteral(
          "timeline_color_set_lut requires clipId and filePath strings."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->setClipColorLutForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("filePath")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_clear_lut")) {
    if (!has_only_keys(args, {QStringLiteral("clipId")}) ||
        !args.value(QStringLiteral("clipId")).isString()) {
      return error_response(QStringLiteral(
          "timeline_color_clear_lut requires only a clipId string."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->clearClipColorLutForAutomation(
        args.value(QStringLiteral("clipId")).toString());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("timeline_color_set_shot_match_gains")) {
    if (!has_only_keys(
            args,
            {QStringLiteral("clipId"), QStringLiteral("redGain"),
             QStringLiteral("greenGain"), QStringLiteral("blueGain")}) ||
        !args.value(QStringLiteral("clipId")).isString() ||
        !args.value(QStringLiteral("redGain")).isDouble() ||
        !args.value(QStringLiteral("greenGain")).isDouble() ||
        !args.value(QStringLiteral("blueGain")).isDouble()) {
      return error_response(QStringLiteral(
          "timeline_color_set_shot_match_gains requires clipId and three numeric gains."));
    }
    if (timeline_document_.isNull()) {
      return error_response(QStringLiteral("Timeline editing is unavailable."));
    }
    const auto result = timeline_document_->setClipShotMatchGainsForAutomation(
        args.value(QStringLiteral("clipId")).toString(),
        args.value(QStringLiteral("redGain")).toDouble(),
        args.value(QStringLiteral("greenGain")).toDouble(),
        args.value(QStringLiteral("blueGain")).toDouble());
    if (!result.value(QStringLiteral("ok")).toBool()) {
      return error_response(result.value(QStringLiteral("error")).toString());
    }
    return ok_response(QJsonObject::fromVariantMap(result));
  }
  if (command == QStringLiteral("photo_search_edit_start")) {
    if (!has_only_keys(args, {QStringLiteral("instruction")}) || !args.value("instruction").isString())
      return error_response(QStringLiteral("Search requires only an instruction string."));
    if (search_edit_ == nullptr) return error_response(QStringLiteral("Search-and-edit is unavailable."));
    QVariantMap model;
    for (const auto& value : agent_edit_->availableModels()) {
      const auto candidate = value.toMap();
      if (candidate.value("id").toString() == agent_edit_->selectedModelId()) model = candidate;
    }
    return ok_response(QJsonObject::fromVariantMap(search_edit_->start(args.value("instruction").toString(), model)));
  }
  if (command == QStringLiteral("photo_search_edit_status") || command == QStringLiteral("photo_search_edit_cancel")) {
    if (!args.isEmpty()) return error_response(QStringLiteral("This search command accepts no arguments."));
    if (search_edit_ == nullptr) return error_response(QStringLiteral("Search-and-edit is unavailable."));
    if (command == QStringLiteral("photo_search_edit_cancel")) search_edit_->cancel();
    return ok_response(QJsonObject::fromVariantMap(search_edit_->snapshot()));
  }
  if (command == QStringLiteral("photo_live_status")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_live_status does not accept args."));
    }
    return ok_response(QJsonObject{
        {QStringLiteral("appRunning"), true},
        {QStringLiteral("protocol"), QString::fromLatin1(kProtocol)},
        {QStringLiteral("selectedAssetId"), catalog_model_->selectedAssetId()},
        {QStringLiteral("selectedAssetCount"), catalog_model_->selectedAssetCount()},
        {QStringLiteral("selectedAssetIds"), QJsonArray::fromStringList(catalog_model_->selectedAssetIds())},
        {QStringLiteral("agentStatus"), agent_edit_->status()},
        {QStringLiteral("agentRunning"), agent_edit_->running()},
        {QStringLiteral("draftActive"), agent_edit_->draftActive()},
        {QStringLiteral("mcpConnectedClientCount"), mcp_connected_client_count_},
        {QStringLiteral("mcpTotalClientCount"), mcp_total_client_count_},
        {QStringLiteral("mcpLastToolCallName"), mcp_last_tool_call_name_},
        {QStringLiteral("mcpLastToolCallStatus"), mcp_last_tool_call_status_},
        {QStringLiteral("mcpLastToolCallAt"), mcp_last_tool_call_at_},
        {QStringLiteral("errorMessage"), agent_edit_->errorMessage()},
    });
  }

  if (command == QStringLiteral("photo_list_models")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_list_models does not accept args."));
    }
    return ok_response(QJsonObject{
        {QStringLiteral("selectedModelId"), agent_edit_->selectedModelId()},
        {QStringLiteral("models"), variant_list_to_json_array(agent_edit_->availableModels())},
    });
  }

  if (command == QStringLiteral("photo_selected_assets")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_selected_assets does not accept args."));
    }
    return ok_response(QJsonObject{
        {QStringLiteral("selectedAssetId"), catalog_model_->selectedAssetId()},
        {QStringLiteral("selectedAssetIds"), QJsonArray::fromStringList(catalog_model_->selectedAssetIds())},
        {QStringLiteral("selectedAssetCount"), catalog_model_->selectedAssetCount()},
    });
  }

  if (command == QStringLiteral("photo_read_asset")) {
    if (!has_only_keys(args, {QStringLiteral("assetId")}) ||
        (args.contains(QStringLiteral("assetId")) && !args.value(QStringLiteral("assetId")).isString())) {
      return error_response(QStringLiteral("photo_read_asset accepts only an optional assetId string."));
    }
    const auto asset = asset_for_bridge_request(catalog_model_, args);
    if (asset.isEmpty()) {
      return error_response(QStringLiteral("No matching asset is available to read."));
    }
    return ok_response(QJsonObject{
        {QStringLiteral("assetId"), asset.value(QStringLiteral("assetId")).toString()},
        {QStringLiteral("asset"), variant_map_to_json_object(asset)},
    });
  }

  if (command == QStringLiteral("photo_read_adjustments")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_read_adjustments does not accept args."));
    }
    const auto asset_id = catalog_model_->selectedAssetId();
    if (!asset_id.isEmpty() && !catalog_model_->assetRecipeReady(asset_id)) {
      catalog_model_->requestAssetRecipes({asset_id});
      auto response = error_response(QStringLiteral(
          "The saved edit recipe is still loading; retry after it finishes."));
      response.insert(QStringLiteral("data"), QJsonObject{
          {QStringLiteral("assetId"), asset_id},
          {QStringLiteral("pending"), true},
          {QStringLiteral("retryable"), true},
      });
      return response;
    }
    return ok_response(QJsonObject{
        {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("adjustmentPatchJson"),
         catalog_model_->selectedAdjustmentPatchJson()},
    });
  }

  if (command == QStringLiteral("photo_read_recipe")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_read_recipe does not accept args."));
    }
    const auto asset_id = catalog_model_->selectedAssetId();
    if (!asset_id.isEmpty() && !catalog_model_->assetRecipeReady(asset_id)) {
      catalog_model_->requestAssetRecipes({asset_id});
      auto response = error_response(QStringLiteral(
          "The saved edit recipe is still loading; retry after it finishes."));
      response.insert(QStringLiteral("data"), QJsonObject{
          {QStringLiteral("assetId"), asset_id},
          {QStringLiteral("pending"), true},
          {QStringLiteral("retryable"), true},
      });
      return response;
    }
    const auto adjustments =
        parse_adjustment_patch_json(
            catalog_model_
                ->selectedAdjustmentPatchJson());
    return ok_response(QJsonObject{
        {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("recipeJson"),
         adjustments.ok()
             ? serialize_edit_recipe_json(
                   *adjustments.state)
             : QString{}},
    });
  }

  if (command == QStringLiteral("photo_get_recipe_patch_schema")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_get_recipe_patch_schema does not accept args."));
    }
    return ok_response(QJsonObject::fromVariantMap(agent_recipe_patch_tool_schema()));
  }

  if (command == QStringLiteral("photo_get_style_rulebook")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_get_style_rulebook does not accept args."));
    }
    const auto selected_style_id = agent_edit_->selectedStyleId();
    for (const auto& style_value : agent_edit_->availableStyles()) {
      const auto style = style_value.toMap();
      if (style.value(QStringLiteral("id")).toString() == selected_style_id) {
        return ok_response(QJsonObject{
            {QStringLiteral("styleId"), selected_style_id},
            {QStringLiteral("style"), QJsonObject::fromVariantMap(style)},
        });
      }
    }
    return error_response(QStringLiteral("No selected style rulebook is available."));
  }

  if (command == QStringLiteral("photo_validate_recipe_patch")) {
    if (!has_only_keys(args, {QStringLiteral("patch")}) || !args.value(QStringLiteral("patch")).isObject()) {
      return error_response(QStringLiteral("photo_validate_recipe_patch requires only a patch object."));
    }
    return ok_response(variant_map_to_json_object(
        agent_edit_->validateRecipePatch(args.value(QStringLiteral("patch")).toObject().toVariantMap())));
  }

  if (command == QStringLiteral("photo_render_proof")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_render_proof does not accept args."));
    }
    return ok_response(variant_map_to_json_object(agent_edit_->renderProofForSelected()));
  }

  if (command == QStringLiteral("photo_validate_edit")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_validate_edit does not accept args."));
    }
    auto report = agent_edit_->renderProofForSelected();
    report.insert(QStringLiteral("mode"), QStringLiteral("validate-edit"));
    return ok_response(variant_map_to_json_object(report));
  }

  if (command == QStringLiteral("photo_validate_batch")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_validate_batch does not accept args."));
    }
    return ok_response(variant_map_to_json_object(agent_edit_->validateBatchForSelected()));
  }

  if (command == QStringLiteral("photo_export_preflight")) {
    const QStringList allowed_keys{
        QStringLiteral("preset"),
        QStringLiteral("destinationDirectory"),
        QStringLiteral("histogramSampleCount"),
        QStringLiteral("shadowClipCount"),
        QStringLiteral("highlightClipCount"),
        QStringLiteral("rawClipSampleCount"),
        QStringLiteral("rawShadowClipCount"),
        QStringLiteral("rawHighlightClipCount"),
    };
    const QStringList metric_keys{
        QStringLiteral("histogramSampleCount"),
        QStringLiteral("shadowClipCount"),
        QStringLiteral("highlightClipCount"),
        QStringLiteral("rawClipSampleCount"),
        QStringLiteral("rawShadowClipCount"),
        QStringLiteral("rawHighlightClipCount"),
    };
    QString invalid_metric_key;
    if (!has_only_keys(args, allowed_keys) ||
        (!args.value(QStringLiteral("preset")).isUndefined() &&
         !args.value(QStringLiteral("preset")).isString()) ||
        (!args.value(QStringLiteral("destinationDirectory")).isUndefined() &&
         !args.value(QStringLiteral("destinationDirectory")).isString()) ||
        !contains_only_optional_numbers(args, metric_keys, &invalid_metric_key)) {
      return error_response(QStringLiteral("photo_export_preflight accepts only preset, destinationDirectory, and numeric preflight counters."));
    }
    return ok_response(variant_map_to_json_object(agent_edit_->exportPreflightForSelected(
        args.value(QStringLiteral("preset")).toString(),
        args.value(QStringLiteral("destinationDirectory")).toString(),
        export_preflight_metrics_from_args(args))));
  }

  if (command == QStringLiteral("photo_export_batch")) {
    const QStringList allowed_keys{
        QStringLiteral("confirmed"),
        QStringLiteral("preset"),
        QStringLiteral("destinationDirectory"),
        QStringLiteral("histogramSampleCount"),
        QStringLiteral("shadowClipCount"),
        QStringLiteral("highlightClipCount"),
        QStringLiteral("rawClipSampleCount"),
        QStringLiteral("rawShadowClipCount"),
        QStringLiteral("rawHighlightClipCount"),
    };
    const QStringList metric_keys{
        QStringLiteral("histogramSampleCount"),
        QStringLiteral("shadowClipCount"),
        QStringLiteral("highlightClipCount"),
        QStringLiteral("rawClipSampleCount"),
        QStringLiteral("rawShadowClipCount"),
        QStringLiteral("rawHighlightClipCount"),
    };
    QString invalid_metric_key;
    if (!has_only_keys(args, allowed_keys) ||
        !args.value(QStringLiteral("confirmed")).isBool() ||
        (!args.value(QStringLiteral("preset")).isUndefined() &&
         !args.value(QStringLiteral("preset")).isString()) ||
        (!args.value(QStringLiteral("destinationDirectory")).isUndefined() &&
         !args.value(QStringLiteral("destinationDirectory")).isString()) ||
        !contains_only_optional_numbers(args, metric_keys, &invalid_metric_key)) {
      return error_response(QStringLiteral("photo_export_batch requires confirmed plus optional preset, destinationDirectory, and numeric preflight counters."));
    }
    return ok_response(variant_map_to_json_object(agent_edit_->queueExportForSelected(
        args.value(QStringLiteral("preset")).toString(),
        args.value(QStringLiteral("destinationDirectory")).toString(),
        args.value(QStringLiteral("confirmed")).toBool(),
        export_preflight_metrics_from_args(args))));
  }

  if (command == QStringLiteral("photo_apply_recipe_patch_draft")) {
    if (!has_only_keys(args, {QStringLiteral("patch"), QStringLiteral("confirmed")}) ||
        !args.value(QStringLiteral("patch")).isObject() ||
        (!args.value(QStringLiteral("confirmed")).isUndefined() &&
         !args.value(QStringLiteral("confirmed")).isBool())) {
      return error_response(QStringLiteral("photo_apply_recipe_patch_draft requires a patch object and optional confirmed boolean."));
    }
    if (value_contains_retouch_patch(args.value(QStringLiteral("patch"))) &&
        !args.value(QStringLiteral("confirmed")).toBool(false)) {
      return error_response(QStringLiteral("photo_apply_recipe_patch_draft requires confirmed: true for retouch patches."));
    }
    return ok_response(variant_map_to_json_object(
        agent_edit_->applyDraftRecipePatchToSelected(args.value(QStringLiteral("patch")).toObject().toVariantMap())));
  }

  if (command == QStringLiteral("photo_plan_bulk_edit") ||
      command == QStringLiteral("photo_apply_bulk_draft")) {
    if (!has_only_keys(args, {QStringLiteral("instructions")}) ||
        (!args.value(QStringLiteral("instructions")).isUndefined() &&
         !args.value(QStringLiteral("instructions")).isString())) {
      return error_response(QStringLiteral("Bulk edit commands accept only optional instructions."));
    }
    if (catalog_model_->selectedAssetCount() < 2) {
      return error_response(QStringLiteral("Bulk edit commands require at least two selected assets."));
    }
    const auto instructions = args.value(QStringLiteral("instructions")).toString();
    auto report = command == QStringLiteral("photo_plan_bulk_edit")
                      ? agent_edit_->planDraftForSelected(instructions)
                      : agent_edit_->runDraftOnSelected(instructions);
    report.insert(QStringLiteral("automationCommand"), command);
    return ok_response(variant_map_to_json_object(report));
  }

  if (command == QStringLiteral("photo_create_mask") ||
      command == QStringLiteral("photo_apply_mask_adjustment")) {
    QString error_message;
    const auto create_mask = command == QStringLiteral("photo_create_mask");
    if (!valid_mask_args(args, create_mask, &error_message)) {
      return error_response(error_message);
    }
    const auto patch = mask_patch_from_args(args, create_mask);
    auto report = agent_edit_->applyDraftRecipePatchToSelected(patch);
    report.insert(QStringLiteral("mode"), command);
    report.insert(QStringLiteral("maskType"), args.value(QStringLiteral("type")).toString());
    report.insert(QStringLiteral("maskPatch"), patch);
    return ok_response(variant_map_to_json_object(report));
  }

  if (command == QStringLiteral("photo_run_draft_on_selected")) {
    if (!has_only_keys(args, {QStringLiteral("instructions")})) {
      return error_response(QStringLiteral("photo_run_draft_on_selected accepts only instructions."));
    }
    return ok_response(variant_map_to_json_object(
        agent_edit_->runDraftOnSelected(args.value(QStringLiteral("instructions")).toString())));
  }

  if (command == QStringLiteral("photo_commit_agent_draft")) {
    if (!has_only_keys(args, {QStringLiteral("confirmed")}) ||
        !args.value(QStringLiteral("confirmed")).isBool()) {
      return error_response(QStringLiteral("photo_commit_agent_draft requires confirmed."));
    }
    if (!args.value(QStringLiteral("confirmed")).toBool()) {
      return error_response(QStringLiteral("photo_commit_agent_draft requires confirmed: true."));
    }
    return ok_response(variant_map_to_json_object(agent_edit_->commitDraft(true)));
  }

  if (command == QStringLiteral("photo_resolve_agent_review_issue")) {
    if (!has_only_keys(
            args,
            {QStringLiteral("assetId"), QStringLiteral("action"),
             QStringLiteral("confirmed")}) ||
        !args.value(QStringLiteral("assetId")).isString() ||
        !args.value(QStringLiteral("action")).isString() ||
        !args.value(QStringLiteral("confirmed")).isBool()) {
      return error_response(QStringLiteral(
          "photo_resolve_agent_review_issue requires assetId, action, and confirmed."));
    }
    if (!args.value(QStringLiteral("confirmed")).toBool()) {
      return error_response(QStringLiteral(
          "photo_resolve_agent_review_issue requires confirmed: true."));
    }
    const auto asset_id =
        args.value(QStringLiteral("assetId")).toString().trimmed();
    const auto action =
        args.value(QStringLiteral("action")).toString().trimmed().toLower();
    if (asset_id.isEmpty()) {
      return error_response(QStringLiteral(
          "photo_resolve_agent_review_issue requires a non-empty assetId."));
    }
    if (action == QStringLiteral("fix")) {
      return ok_response(
          variant_map_to_json_object(agent_edit_->fixReviewIssue(asset_id)));
    }
    if (action == QStringLiteral("accept_anyway")) {
      return ok_response(
          variant_map_to_json_object(agent_edit_->acceptReviewIssue(asset_id)));
    }
    if (action == QStringLiteral("rollback_photo")) {
      return ok_response(
          variant_map_to_json_object(agent_edit_->rollbackReviewIssue(asset_id)));
    }
    return error_response(QStringLiteral(
        "photo_resolve_agent_review_issue action must be fix, accept_anyway, or rollback_photo."));
  }

  if (command == QStringLiteral("photo_cancel_agent_run")) {
    if (!has_only_keys(args, {QStringLiteral("reason")}) ||
        (!args.value(QStringLiteral("reason")).isUndefined() &&
         !args.value(QStringLiteral("reason")).isString())) {
      return error_response(QStringLiteral("photo_cancel_agent_run accepts only an optional reason string."));
    }
    return ok_response(variant_map_to_json_object(
        agent_edit_->cancelRun(args.value(QStringLiteral("reason")).toString())));
  }

  if (command == QStringLiteral("photo_rollback_agent_draft")) {
    if (!args.isEmpty()) {
      return error_response(QStringLiteral("photo_rollback_agent_draft does not accept args."));
    }
    return ok_response(variant_map_to_json_object(agent_edit_->rollbackDraft()));
  }

  return error_response(QStringLiteral("Unknown automation command: %1").arg(command));
}

QJsonObject PhotoAutomationBridge::enqueueTimelineH264Export(const QJsonObject& args) const {
  if (!has_only_keys(args, {QStringLiteral("resourceId"), QStringLiteral("documentHash"),
                            QStringLiteral("requestId"), QStringLiteral("confirmed"),
                            QStringLiteral("range")}) ||
      !args.value(QStringLiteral("resourceId")).isString() ||
      !args.value(QStringLiteral("documentHash")).isString() ||
      !args.value(QStringLiteral("requestId")).isString() ||
      (args.contains(QStringLiteral("range")) &&
       !args.value(QStringLiteral("range")).isObject()) ||
      !args.value(QStringLiteral("confirmed")).isBool() ||
      !args.value(QStringLiteral("confirmed")).toBool()) {
    return argument_error_response(QStringLiteral(
        "timeline.export_h264 requires resourceId, documentHash, a canonical requestId UUID and confirmed:true."));
  }
  if (timeline_document_.isNull() || timeline_job_queue_.isNull()) {
    return error_response(QStringLiteral("Native Timeline export is unavailable."));
  }

  const auto resource_id = args.value(QStringLiteral("resourceId")).toString();
  const auto document_hash = args.value(QStringLiteral("documentHash")).toString();
  const auto request_id = args.value(QStringLiteral("requestId")).toString();
  const auto timeline_id = resource_id.startsWith(QStringLiteral("timeline:"))
                               ? resource_id.mid(QStringLiteral("timeline:").size())
                               : QString{};
  if (!timeline_mcp_export_canonical_uuid(timeline_id) ||
      !timeline_mcp_export_canonical_uuid(request_id) ||
      !timeline_mcp_export_canonical_sha256(document_hash)) {
    return argument_error_response(QStringLiteral(
        "Provide a canonical Timeline resource UUID, lowercase SHA-256 documentHash and requestId UUID."));
  }
  if (timeline_document_->agentTaskDocumentId() != resource_id) {
    return error_response(QStringLiteral("The requested Timeline resource is not the active document."));
  }

  auto range = QJsonObject{};
  if (args.contains(QStringLiteral("range"))) {
    const auto requested_range = args.value(QStringLiteral("range")).toObject();
    if (requested_range.size() != 2 ||
        !requested_range.contains(QStringLiteral("startFrame")) ||
        !requested_range.contains(QStringLiteral("endFrameExclusive"))) {
      return argument_error_response(QStringLiteral(
          "range must contain only startFrame and endFrameExclusive global sequence frame indices."));
    }
    qint64 start_frame = 0;
    qint64 end_frame_exclusive = 0;
    if (!timeline_mcp_export_json_integer(
            requested_range.value(QStringLiteral("startFrame")), &start_frame) ||
        !timeline_mcp_export_json_integer(
            requested_range.value(QStringLiteral("endFrameExclusive")), &end_frame_exclusive)) {
      return argument_error_response(QStringLiteral(
          "Review range frames must be exact non-negative integers."));
    }
    const auto frame_rate = timeline_document_->framesPerSecond();
    const auto duration_seconds = timeline_document_->totalDurationSecondsValue();
    const auto raw_total_frames = duration_seconds * frame_rate;
    if (!std::isfinite(frame_rate) || frame_rate <= 0.0 ||
        !std::isfinite(duration_seconds) || duration_seconds <= 0.0 ||
        !std::isfinite(raw_total_frames) || raw_total_frames <= 0.0) {
      return error_response(QStringLiteral("The Timeline frame clock is unavailable for review export."));
    }
    const auto total_frames = static_cast<qint64>(std::ceil(raw_total_frames - 1.0e-7));
    range = QJsonObject{
        {QStringLiteral("startFrame"), start_frame},
        {QStringLiteral("endFrameExclusive"), end_frame_exclusive},
        {QStringLiteral("frameRate"), frame_rate},
    };
    auto validated_start = qint64{0};
    auto validated_end = qint64{0};
    auto validated_frame_rate = 0.0;
    if (end_frame_exclusive > total_frames ||
        !timeline_mcp_export_review_range(
            range, &validated_start, &validated_end, &validated_frame_rate) ||
        validated_frame_rate != frame_rate) {
      return argument_error_response(QStringLiteral(
          "Review range must be a half-open interval inside the Timeline lasting between 1 and 15 seconds on its global frame clock."));
    }
  }

  if (!QDir().mkpath(timelineDocumentsRootPath()) ||
      !QDir().mkpath(timelineRendersRootPath())) {
    return error_response(QStringLiteral("The app-managed Timeline export folders could not be prepared."));
  }
  const auto documents_root = QFileInfo(timelineDocumentsRootPath()).canonicalFilePath();
  const auto renders_root = QFileInfo(timelineRendersRootPath()).canonicalFilePath();
  if (documents_root.isEmpty() || renders_root.isEmpty()) {
    return error_response(QStringLiteral("The app-managed Timeline export folders are unavailable."));
  }
  const auto snapshot_root = QDir(documents_root).filePath(QStringLiteral("render-snapshots"));
  if (!QDir().mkpath(snapshot_root) || QFileInfo(snapshot_root).canonicalFilePath() != snapshot_root) {
    return error_response(QStringLiteral("The app-managed Timeline snapshot folder is unavailable."));
  }

  QString record_error;
  auto records = loadPersistedJobRecords(&record_error, false);
  if (!record_error.isEmpty()) {
    return error_response(QStringLiteral("The durable Timeline render queue is unavailable."));
  }
  const auto request_fingerprint = timeline_mcp_export_fingerprint(
      request_id, resource_id, document_hash, renders_root, range);
  for (const auto& prior : records) {
    const auto metadata = prior.metadata_json.value(QStringLiteral("mcpTimelineExport")).toObject();
    if (prior.job_id != request_id &&
        metadata.value(QStringLiteral("requestId")).toString() != request_id) {
      continue;
    }
    QString ownership_error;
    if (!timeline_mcp_export_owned_job(
            prior, resource_id, request_id, document_hash,
            documents_root, renders_root, &ownership_error) ||
        metadata.value(QStringLiteral("requestFingerprint")).toString() != request_fingerprint) {
      return error_response(QStringLiteral(
          "This requestId is already bound to a different or unowned Timeline job."));
    }
    return ok_response(QJsonObject::fromVariantMap(timeline_mcp_export_receipt(prior, true)));
  }

  if (timeline_task_lifecycle_handler_) {
    const auto task = timeline_task_lifecycle_handler_(QStringLiteral("status"), {});
    if (!task.value(QStringLiteral("ok")).toBool() || task.value(QStringLiteral("active")).toBool()) {
      return error_response(QStringLiteral(
          "Finish, release, or revert the active Timeline editing task before exporting."));
    }
  } else {
    return error_response(QStringLiteral("Timeline task ownership state is unavailable."));
  }
  if (agent_edit_ != nullptr &&
      (agent_edit_->running() || agent_edit_->draftActive() ||
       !agent_edit_->activeTaskId().isEmpty())) {
    return error_response(QStringLiteral("Resolve the active editing task before exporting the Timeline."));
  }
  const auto snapshot_readiness = timeline_document_->agentExportSnapshotReadiness();
  if (!snapshot_readiness.value(QStringLiteral("ready")).toBool()) {
    auto response = error_response(QStringLiteral(
        "Wait for active Timeline edits, saves, and render-input analysis to finish before export."));
    response.insert(QStringLiteral("data"), QJsonObject::fromVariantMap(snapshot_readiness));
    return response;
  }
  if (timeline_document_->deterministicDocumentHash() != document_hash) {
    return error_response(QStringLiteral("The active Timeline changed after the supplied documentHash was inspected."));
  }
  if (timeline_document_->exportContainer() != QStringLiteral("mp4") ||
      timeline_document_->exportCodec() != QStringLiteral("h264")) {
    return error_response(QStringLiteral("Set the active Timeline delivery format to MP4/H.264 before using this export tool."));
  }

  const auto snapshot_bytes =
      timeline_document_->agentExportSnapshotState(document_hash);
  if (snapshot_bytes.isEmpty() ||
      snapshot_bytes.size() > kMaximumTimelineMcpExportSnapshotBytes) {
    auto response = error_response(QStringLiteral(
        "A stable export snapshot matching the supplied documentHash is unavailable."));
    response.insert(
        QStringLiteral("data"),
        QJsonObject::fromVariantMap(timeline_document_->agentExportSnapshotReadiness()));
    return response;
  }
  QJsonParseError snapshot_parse_error;
  const auto snapshot_document = QJsonDocument::fromJson(snapshot_bytes, &snapshot_parse_error);
  if (snapshot_parse_error.error != QJsonParseError::NoError || !snapshot_document.isObject() ||
      snapshot_document.object().value(QStringLiteral("timelineId")).toString() != timeline_id ||
      snapshot_document.object().value(QStringLiteral("exportContainer")).toString() != QStringLiteral("mp4") ||
      snapshot_document.object().value(QStringLiteral("exportCodec")).toString() != QStringLiteral("h264") ||
      snapshot_document.object().value(QStringLiteral("exportAlpha")).toBool() ||
      snapshot_document.object().value(QStringLiteral("exportAudioStems")).toBool() ||
      snapshot_document.object().value(QStringLiteral("exportImageSequence")).toBool()) {
    return error_response(QStringLiteral(
        "The Timeline snapshot does not match the supported standalone MP4/H.264 output contract."));
  }
  const auto source_manifest = timeline_mcp_export_source_manifest(snapshot_bytes, range);
  if (!source_manifest.value(QStringLiteral("ok")).toBool()) {
    return error_response(source_manifest.value(QStringLiteral("error")).toString());
  }
  const auto project_health = range.isEmpty()
      ? timeline_document_->timelineProjectHealth()
      : QVariantMap{{QStringLiteral("documentHash"),
                     timeline_document_->deterministicDocumentHash()}};
  const auto source_count = source_manifest.value(QStringLiteral("sourceCount")).toInt();
  const auto primary_source_count =
      source_manifest.value(QStringLiteral("primarySourceCount")).toInt();
  const auto read_integrity_count = [&project_health](const QString& key, qint64* output) {
    if (output == nullptr) return false;
    const auto value = project_health.value(key);
    if (!value.isValid() || value.isNull()) return false;
    switch (value.metaType().id()) {
      case QMetaType::Int:
      case QMetaType::UInt:
      case QMetaType::LongLong:
      case QMetaType::ULongLong: {
        bool ok = false;
        const auto count = value.toLongLong(&ok);
        if (!ok || count < 0) return false;
        *output = count;
        return true;
      }
      default:
        return false;
    }
  };
  auto source_integrity_verified_count = qint64{-1};
  auto source_integrity_mismatch_count = qint64{-1};
  auto source_integrity_untracked_count = qint64{-1};
  auto generated_integrity_mismatch_count = qint64{-1};
  auto generated_integrity_untracked_count = qint64{-1};
  auto aggregate_integrity_mismatch_count = qint64{-1};
  auto aggregate_integrity_untracked_count = qint64{-1};
  auto project_blocking_count = qint64{-1};
  auto health_counts_valid = true;
  health_counts_valid = read_integrity_count(
      QStringLiteral("sourceIntegrityVerifiedCount"), &source_integrity_verified_count) &&
      health_counts_valid;
  health_counts_valid = read_integrity_count(
      QStringLiteral("sourceIntegrityMismatchCount"), &source_integrity_mismatch_count) &&
      health_counts_valid;
  health_counts_valid = read_integrity_count(
      QStringLiteral("sourceIntegrityUntrackedCount"), &source_integrity_untracked_count) &&
      health_counts_valid;
  health_counts_valid = read_integrity_count(
      QStringLiteral("generatedIntegrityMismatchCount"), &generated_integrity_mismatch_count) &&
      health_counts_valid;
  health_counts_valid = read_integrity_count(
      QStringLiteral("generatedIntegrityUntrackedCount"), &generated_integrity_untracked_count) &&
      health_counts_valid;
  health_counts_valid = read_integrity_count(
      QStringLiteral("integrityMismatchCount"), &aggregate_integrity_mismatch_count) &&
      health_counts_valid;
  health_counts_valid = read_integrity_count(
      QStringLiteral("integrityUntrackedCount"), &aggregate_integrity_untracked_count) &&
      health_counts_valid;
  health_counts_valid = read_integrity_count(
      QStringLiteral("blockingCount"), &project_blocking_count) && health_counts_valid;

  const auto health_ok_value = project_health.value(QStringLiteral("ok"));
  const auto health_ok_valid = health_ok_value.isValid() &&
                               health_ok_value.metaType().id() == QMetaType::Bool;
  const auto health_reports_ok = health_ok_valid && health_ok_value.toBool();

  QSet<QString> full_sha256_manifest_paths;
  for (const auto& source_value : source_manifest.value(QStringLiteral("sources")).toList()) {
    const auto source = source_value.toMap();
    if (source.value(QStringLiteral("identityKind")).toString() != QStringLiteral("sha256-v1")) {
      continue;
    }
    const auto source_path = source.value(QStringLiteral("path")).toString();
    const QFileInfo source_info(source_path);
    if (source_path.isEmpty() || source_info.isSymLink() ||
        source_info.canonicalFilePath() != source_path ||
        !timeline_mcp_export_canonical_sha256(
            source.value(QStringLiteral("sha256")).toString())) {
      health_counts_valid = false;
      continue;
    }
    full_sha256_manifest_paths.insert(source_path);
  }
  if (full_sha256_manifest_paths.size() != primary_source_count) {
    health_counts_valid = false;
  }

  auto duplicate_generated_warnings_pinned_to_sources = qint64{0};
  auto unpinned_generated_warnings = qint64{-1};
  auto generated_warning_paths_valid = generated_integrity_untracked_count == 0;
  if (generated_integrity_untracked_count > 0) {
    const auto issues_value = project_health.value(QStringLiteral("issues"));
    generated_warning_paths_valid =
        issues_value.isValid() && issues_value.metaType().id() == QMetaType::QVariantList;
    QSet<QString> generated_warning_paths;
    if (generated_warning_paths_valid) {
      for (const auto& issue_value : issues_value.toList()) {
        const auto issue = issue_value.toMap();
        if (issue.value(QStringLiteral("key")).toString() !=
            QStringLiteral("generated-integrity-untracked")) {
          continue;
        }
        const auto issue_path = issue.value(QStringLiteral("path")).toString();
        const QFileInfo issue_info(issue_path);
        if (issue_path.isEmpty() || issue_info.isSymLink() ||
            issue_info.canonicalFilePath() != issue_path ||
            !issue_info.isFile() || !issue_info.isReadable()) {
          generated_warning_paths_valid = false;
          break;
        }
        generated_warning_paths.insert(issue_path);
      }
    }
    if (generated_warning_paths_valid &&
        generated_warning_paths.size() == generated_integrity_untracked_count) {
      for (const auto& path : generated_warning_paths) {
        if (full_sha256_manifest_paths.contains(path)) {
          ++duplicate_generated_warnings_pinned_to_sources;
        }
      }
      unpinned_generated_warnings =
          generated_integrity_untracked_count - duplicate_generated_warnings_pinned_to_sources;
    } else {
      generated_warning_paths_valid = false;
    }
  } else if (generated_integrity_untracked_count == 0) {
    const auto issues_value = project_health.value(QStringLiteral("issues"));
    if (!issues_value.isValid() || issues_value.metaType().id() != QMetaType::QVariantList) {
      generated_warning_paths_valid = false;
    } else {
      const auto issues = issues_value.toList();
      generated_warning_paths_valid = std::none_of(
          issues.cbegin(), issues.cend(), [](const QVariant& value) {
            return value.toMap().value(QStringLiteral("key")).toString() ==
                   QStringLiteral("generated-integrity-untracked");
          });
    }
    unpinned_generated_warnings = 0;
  }
  health_counts_valid = generated_warning_paths_valid && health_counts_valid;
  if (health_counts_valid &&
      (aggregate_integrity_mismatch_count != source_integrity_mismatch_count +
                                                   generated_integrity_mismatch_count ||
       aggregate_integrity_untracked_count != source_integrity_untracked_count +
                                                    generated_integrity_untracked_count)) {
    health_counts_valid = false;
  }

  const auto project_source_count = health_counts_valid
      ? source_integrity_verified_count + source_integrity_mismatch_count +
            source_integrity_untracked_count
      : qint64{-1};
  const auto global_sources_verified = health_counts_valid && health_reports_ok &&
      project_blocking_count == 0 && aggregate_integrity_mismatch_count == 0 &&
      source_integrity_mismatch_count == 0 && source_integrity_untracked_count == 0 &&
      source_integrity_verified_count == primary_source_count &&
      unpinned_generated_warnings == 0;
  if (project_health.value(QStringLiteral("documentHash")).toString() != document_hash ||
      (range.isEmpty() && !global_sources_verified)) {
    const auto error_message = range.isEmpty()
        ? (!health_counts_valid || !health_ok_valid
               ? QStringLiteral("Timeline source and generated-media integrity health is missing or invalid; no export was queued.")
               : QStringLiteral("All Timeline source files must be online and fully checksum-verified before export."))
        : QStringLiteral("Every source and file-backed dependency used by the selected range must be online and checksum-verified.");
    auto response = error_response(error_message);
    response.insert(QStringLiteral("data"), QJsonObject{
        {QStringLiteral("projectSourceCount"), project_source_count},
        {QStringLiteral("projectVerifiedSourceCount"), source_integrity_verified_count},
        {QStringLiteral("projectUntrackedSourceCount"), source_integrity_untracked_count},
        {QStringLiteral("projectMismatchedSourceCount"), source_integrity_mismatch_count},
        {QStringLiteral("projectUntrackedDerivedCount"), generated_integrity_untracked_count},
        {QStringLiteral("projectMismatchedDerivedCount"), generated_integrity_mismatch_count},
        {QStringLiteral("projectUnpinnedDerivedCount"), unpinned_generated_warnings},
        {QStringLiteral("projectDuplicateDerivedWarningsPinnedToSourceCount"),
         duplicate_generated_warnings_pinned_to_sources},
        {QStringLiteral("projectAggregateUntrackedCount"), aggregate_integrity_untracked_count},
        {QStringLiteral("projectAggregateMismatchedCount"), aggregate_integrity_mismatch_count},
        {QStringLiteral("projectBlockingCount"), project_blocking_count},
        {QStringLiteral("projectHealthFieldsValid"), health_counts_valid && health_ok_valid},
        {QStringLiteral("projectHealthEvaluated"), range.isEmpty()},
        {QStringLiteral("selectedRangeSourceCount"), source_count},
        {QStringLiteral("selectedRangePrimarySourceCount"), primary_source_count},
        {QStringLiteral("selectedRangeManifestPinned"), true},
    });
    return response;
  }

  const auto preflight = timeline_document_->timelineDeliveryPreflight();
  auto estimated_output_bytes =
      preflight.value(QStringLiteral("estimatedOutputBytes")).toLongLong();
  auto required_disk_bytes = preflight.value(QStringLiteral("requiredDiskBytes")).toLongLong();
  const auto free_disk_bytes = preflight.value(QStringLiteral("freeDiskBytes")).toLongLong();
  const auto maximum_output_bytes = timeline_mcp_export_maximum_output_bytes(range);
  if (!range.isEmpty()) {
    auto start_frame = qint64{0};
    auto end_frame_exclusive = qint64{0};
    auto frame_rate = 0.0;
    if (!timeline_mcp_export_review_range(
            range, &start_frame, &end_frame_exclusive, &frame_rate)) {
      return error_response(QStringLiteral("The persisted review frame range is invalid."));
    }
    const auto duration_seconds =
        static_cast<double>(end_frame_exclusive - start_frame) / frame_rate;
    const auto video_bitrate_mbps =
        qMax(1, preflight.value(QStringLiteral("videoBitrateMbps")).toInt());
    const auto audio_bitrate_kbps =
        qMax(64, preflight.value(QStringLiteral("audioBitrateKbps")).toInt());
    estimated_output_bytes = static_cast<qint64>(std::ceil(
        duration_seconds *
        (static_cast<double>(video_bitrate_mbps) * 1000000.0 / 8.0 +
         static_cast<double>(audio_bitrate_kbps) * 1000.0 / 8.0)));
    required_disk_bytes = qMax<qint64>(64LL * 1024LL * 1024LL,
                                       estimated_output_bytes * 2);
  }
  if ((range.isEmpty() && !preflight.value(QStringLiteral("ok")).toBool()) ||
      estimated_output_bytes <= 0 || estimated_output_bytes > maximum_output_bytes ||
      required_disk_bytes <= 0 || free_disk_bytes < required_disk_bytes) {
    auto response = error_response(range.isEmpty()
        ? QStringLiteral("Timeline delivery preflight failed or exceeds the 16 GiB per-export cap or available disk space.")
        : QStringLiteral("The review excerpt exceeds its storage estimate or available disk space."));
    response.insert(QStringLiteral("data"), QJsonObject::fromVariantMap(preflight));
    return response;
  }

  for (const auto& record : records) {
    if (record.job_kind == QStringLiteral("timeline_render") && record.asset_id == resource_id &&
        (record.status == QStringLiteral("Queued") || record.status == QStringLiteral("Running") ||
         record.status == QStringLiteral("Canceling"))) {
      return error_response(QStringLiteral(
          "A Timeline render is already queued or active for this document; wait for it to finish before starting another."));
    }
  }

  const auto snapshot_name = QStringLiteral("%1-%2.json").arg(timeline_id, request_id);
  const auto output_name = QStringLiteral("%1-%2.mp4").arg(timeline_id, request_id);
  const auto snapshot_path = QDir(snapshot_root).filePath(snapshot_name);
  const auto output_path = QDir(renders_root).filePath(output_name);
  if (QFileInfo(snapshot_path).exists() || QFileInfo(snapshot_path).isSymLink() ||
      QFileInfo(output_path).exists() || QFileInfo(output_path).isSymLink() ||
      !timeline_mcp_export_path_is_generated_child(
          snapshot_path, snapshot_root, snapshot_name) ||
      !timeline_mcp_export_path_is_generated_child(
          output_path, renders_root, output_name)) {
    return error_response(QStringLiteral(
        "The unique app-managed export target is unavailable; existing files were preserved."));
  }

  QFile snapshot_file(snapshot_path);
  if (!snapshot_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    return error_response(QStringLiteral("The immutable Timeline export snapshot could not be persisted."));
  }
  const auto snapshot_write_ok =
      snapshot_file.write(snapshot_bytes) == snapshot_bytes.size() &&
      snapshot_file.flush() && snapshot_file.error() == QFileDevice::NoError;
  snapshot_file.close();
  if (!snapshot_write_ok) {
    QFile::remove(snapshot_path);
    return error_response(QStringLiteral("The immutable Timeline export snapshot could not be persisted."));
  }
  const auto snapshot_sha256 = QString::fromLatin1(
      QCryptographicHash::hash(snapshot_bytes, QCryptographicHash::Sha256).toHex());
  const auto source_records = source_manifest.value(QStringLiteral("sources")).toList();
  const auto source_manifest_json = QJsonArray::fromVariantList(source_records);
  const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

  PersistentJobRecord job;
  job.job_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  job.job_kind = QStringLiteral("timeline_render");
  job.asset_id = resource_id;
  job.media_type = QStringLiteral("timeline");
  job.display_label = QStringLiteral("%1 · H.264 export").arg(timeline_document_->timelineName());
  job.source_path = snapshot_path;
  job.preset_name = QStringLiteral("MCP H.264");
  job.output_path = output_path;
  job.status = QStringLiteral("Queued");
  job.progress = 0;
  job.created_at = now;
  job.updated_at = now;
  job.metadata_json = QJsonObject{
      {QStringLiteral("timelineId"), timeline_id},
      {QStringLiteral("timelineName"), timeline_document_->timelineName()},
      {QStringLiteral("durationSeconds"), range.isEmpty()
           ? timeline_document_->totalDurationSecondsValue()
           : static_cast<double>(range.value(QStringLiteral("endFrameExclusive")).toInteger() -
                                 range.value(QStringLiteral("startFrame")).toInteger()) /
                 range.value(QStringLiteral("frameRate")).toDouble()},
      {QStringLiteral("timelineDurationSeconds"), timeline_document_->totalDurationSecondsValue()},
      {QStringLiteral("exportContainer"), QStringLiteral("mp4")},
      {QStringLiteral("exportCodec"), QStringLiteral("h264")},
      {QStringLiteral("exportPreset"), timeline_document_->exportPreset()},
      {QStringLiteral("resourcePolicy"), QStringLiteral("playback-first")},
      {QStringLiteral("resourcePolicyAffectsQuality"), false},
      {QStringLiteral("standaloneMcpExport"), true},
      {QStringLiteral("mcpTimelineExport"), QJsonObject{
          {QStringLiteral("schema"), QString::fromLatin1(kTimelineMcpExportSchema)},
          {QStringLiteral("origin"), QString::fromLatin1(kTimelineMcpExportOrigin)},
          {QStringLiteral("requestId"), request_id},
          {QStringLiteral("resourceId"), resource_id},
          {QStringLiteral("timelineId"), timeline_id},
          {QStringLiteral("documentHash"), document_hash},
          {QStringLiteral("requestFingerprint"), request_fingerprint},
          {QStringLiteral("snapshotRoot"), snapshot_root},
          {QStringLiteral("snapshotPath"), snapshot_path},
          {QStringLiteral("snapshotBytes"), snapshot_bytes.size()},
          {QStringLiteral("snapshotSha256"), snapshot_sha256},
          {QStringLiteral("outputRoot"), renders_root},
          {QStringLiteral("outputPath"), output_path},
          {QStringLiteral("maximumOutputBytes"), maximum_output_bytes},
          {QStringLiteral("estimatedOutputBytes"), estimated_output_bytes},
          {QStringLiteral("container"), QStringLiteral("mp4")},
          {QStringLiteral("codec"), QStringLiteral("h264")},
          {QStringLiteral("sourceManifest"), source_manifest_json},
      }},
  };
  if (!range.isEmpty()) {
    auto metadata = job.metadata_json.value(QStringLiteral("mcpTimelineExport")).toObject();
    metadata.insert(QStringLiteral("range"), range);
    job.metadata_json.insert(QStringLiteral("mcpTimelineExport"), metadata);
  }
  job.metadata_json.insert(QStringLiteral("queueOrder"),
                           persistentRenderQueueOrderForNewJob(job.job_id));
  QString queue_error;
  if (!upsertPersistedJobRecord(job, &queue_error, true)) {
    QFile::remove(snapshot_path);
    return error_response(queue_error.isEmpty()
        ? QStringLiteral("The export was not accepted by the durable Timeline job queue.")
        : queue_error);
  }
  timeline_job_queue_->refresh();
  return ok_response(QJsonObject::fromVariantMap(timeline_mcp_export_receipt(job)));
}

QJsonObject PhotoAutomationBridge::inspectTimelineH264Export(const QJsonObject& args) const {
  if (!has_only_keys(args, {QStringLiteral("resourceId"), QStringLiteral("documentHash"),
                            QStringLiteral("requestId")}) ||
      !args.value(QStringLiteral("resourceId")).isString() ||
      !args.value(QStringLiteral("documentHash")).isString() ||
      !args.value(QStringLiteral("requestId")).isString()) {
    return argument_error_response(QStringLiteral(
        "timeline.inspect_export requires the receipt resourceId, documentHash and requestId."));
  }
  if (timeline_document_.isNull()) {
    return error_response(QStringLiteral("Timeline export inspection is unavailable."));
  }
  const auto resource_id = args.value(QStringLiteral("resourceId")).toString();
  const auto document_hash = args.value(QStringLiteral("documentHash")).toString();
  const auto request_id = args.value(QStringLiteral("requestId")).toString();
  if (timeline_document_->agentTaskDocumentId() != resource_id) {
    return error_response(QStringLiteral("The requested Timeline resource is not the active document."));
  }
  QString load_error;
  const auto records = loadPersistedJobRecords(&load_error, false);
  if (!load_error.isEmpty()) {
    return error_response(QStringLiteral("The durable Timeline job history is unavailable."));
  }
  const auto documents_root = QFileInfo(timelineDocumentsRootPath()).canonicalFilePath();
  const auto renders_root = QFileInfo(timelineRendersRootPath()).canonicalFilePath();
  for (const auto& job : records) {
    if (job.metadata_json.value(QStringLiteral("mcpTimelineExport")).toObject()
            .value(QStringLiteral("requestId")).toString() != request_id) continue;
    QString ownership_error;
    if (!timeline_mcp_export_owned_job(job, resource_id, request_id, document_hash,
                                       documents_root, renders_root, &ownership_error)) {
      return error_response(QStringLiteral("The requested job is not an owned Timeline export receipt."));
    }
    return ok_response(QJsonObject::fromVariantMap(timeline_mcp_export_receipt(job)));
  }
  return error_response(QStringLiteral("The exact Timeline export receipt was not found."));
}

QJsonObject PhotoAutomationBridge::cancelTimelineH264Export(const QJsonObject& args) const {
  if (!has_only_keys(args, {QStringLiteral("resourceId"), QStringLiteral("documentHash"),
                            QStringLiteral("requestId"), QStringLiteral("confirmed")}) ||
      !args.value(QStringLiteral("resourceId")).isString() ||
      !args.value(QStringLiteral("documentHash")).isString() ||
      !args.value(QStringLiteral("requestId")).isString() ||
      !args.value(QStringLiteral("confirmed")).isBool() ||
      !args.value(QStringLiteral("confirmed")).toBool()) {
    return argument_error_response(QStringLiteral(
        "timeline.cancel_export requires the exact receipt fields and confirmed:true."));
  }
  if (timeline_document_.isNull() || timeline_job_queue_.isNull() ||
      !timeline_job_queue_->historyMutationsEnabled()) {
    return error_response(QStringLiteral("Timeline export cancellation is unavailable while job history is recovering."));
  }
  const auto resource_id = args.value(QStringLiteral("resourceId")).toString();
  const auto document_hash = args.value(QStringLiteral("documentHash")).toString();
  const auto request_id = args.value(QStringLiteral("requestId")).toString();
  if (timeline_document_->agentTaskDocumentId() != resource_id) {
    return error_response(QStringLiteral("The requested Timeline resource is not the active document."));
  }
  QString load_error;
  const auto records = loadPersistedJobRecords(&load_error, false);
  if (!load_error.isEmpty()) {
    return error_response(QStringLiteral("The durable Timeline job history is unavailable."));
  }
  const auto documents_root = QFileInfo(timelineDocumentsRootPath()).canonicalFilePath();
  const auto renders_root = QFileInfo(timelineRendersRootPath()).canonicalFilePath();
  bool owned_job_found = false;
  auto owned_job_id = QString{};
  for (const auto& job : records) {
    if (job.metadata_json.value(QStringLiteral("mcpTimelineExport")).toObject()
            .value(QStringLiteral("requestId")).toString() != request_id) continue;
    QString ownership_error;
    if (!timeline_mcp_export_owned_job(job, resource_id, request_id, document_hash,
                                       documents_root, renders_root, &ownership_error)) {
      return error_response(QStringLiteral("Cancellation requires the exact receipt for an owned Timeline export."));
    }
    owned_job_found = true;
    owned_job_id = job.job_id;
    break;
  }
  if (!owned_job_found) {
    return error_response(QStringLiteral("The exact owned Timeline export receipt was not found."));
  }
  if (!timeline_job_queue_->cancelJob(owned_job_id)) {
    return error_response(QStringLiteral("The owned Timeline export could not be canceled in its current state."));
  }
  const auto updated_records = loadPersistedJobRecords(nullptr, false);
  for (const auto& job : updated_records) {
    if (job.job_id == owned_job_id) {
      QString ownership_error;
      if (!timeline_mcp_export_owned_job(job, resource_id, request_id, document_hash,
                                         documents_root, renders_root, &ownership_error)) {
        return error_response(QStringLiteral("The export cancellation receipt changed unexpectedly."));
      }
      return ok_response(QJsonObject::fromVariantMap(timeline_mcp_export_receipt(job)));
    }
  }
  return error_response(QStringLiteral("The export was canceled, but its durable receipt could not be reloaded."));
}

bool PhotoAutomationBridge::writeSessionFile() {
  QSaveFile file(session_file_path_);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    error_message_ = QStringLiteral("Unable to write automation session file.");
    return false;
  }
  const auto payload = QJsonObject{
      {QStringLiteral("schemaVersion"), 1},
      {QStringLiteral("protocol"), QString::fromLatin1(kProtocol)},
      {QStringLiteral("endpointKind"), endpoint_kind_},
      {QStringLiteral("endpoint"), endpoint_name_},
      {QStringLiteral("mcpProtocol"), QStringLiteral("model-context-protocol-jsonrpc-2025-06-18")},
      {QStringLiteral("mcpEndpointKind"), mcp_endpoint_kind_},
      {QStringLiteral("mcpEndpoint"), mcp_endpoint_},
      {QStringLiteral("mcpTransport"),
       mcp_http_server_ != nullptr ? QStringLiteral("streamable-http") : QStringLiteral("disabled")},
      {QStringLiteral("mcpCapabilities"), QJsonArray::fromStringList(mcp_capabilities_)},
      {QStringLiteral("mcpConnectedClientCount"), mcp_connected_client_count_},
      {QStringLiteral("mcpTotalClientCount"), mcp_total_client_count_},
      {QStringLiteral("mcpLastToolCallName"), mcp_last_tool_call_name_},
      {QStringLiteral("mcpLastToolCallStatus"), mcp_last_tool_call_status_},
      {QStringLiteral("mcpLastToolCallAt"), mcp_last_tool_call_at_},
      {QStringLiteral("fullServerName"), server_ != nullptr ? server_->fullServerName() : QString{}},
      {QStringLiteral("token"), token_},
      {QStringLiteral("pid"), QCoreApplication::applicationPid()},
      {QStringLiteral("startedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
  };
  if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    error_message_ = QStringLiteral("Unable to restrict automation session file permissions.");
    return false;
  }
  file.write(QJsonDocument(payload).toJson(QJsonDocument::Indented));
  if (!file.commit()) {
    error_message_ = QStringLiteral("Unable to commit automation session file.");
    return false;
  }
  if (!QFile::setPermissions(
          session_file_path_, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    error_message_ = QStringLiteral("Unable to restrict committed automation session file permissions.");
    return false;
  }
  return true;
}

void PhotoAutomationBridge::removeSessionFile() const {
  if (!session_file_path_.isEmpty()) {
    QFile::remove(session_file_path_);
  }
}

void PhotoAutomationBridge::recordMcpToolCall(const QString& tool_name, const QString& status) {
  mcp_last_tool_call_name_ = tool_name.trimmed();
  mcp_last_tool_call_status_ = status.trimmed().isEmpty() ? QStringLiteral("unknown") : status.trimmed();
  mcp_last_tool_call_at_ = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

}  // namespace photo_editor::desktop
