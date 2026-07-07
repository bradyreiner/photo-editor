#include <cstdlib>
#include <iostream>
#include <memory>

#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QDir>
#include <QString>
#include <QVariantMap>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QVariantMap thumbnail_data(
    const bool ready,
    const bool stale,
    const bool progressive,
    const bool failure_contained,
    const QString& source,
    const QString& stage = QString(),
    const QString& detail = QString()) {
  return {
      {QStringLiteral("thumbnailReady"), ready},
      {QStringLiteral("thumbnailStale"), stale},
      {QStringLiteral("thumbnailProgressive"), progressive},
      {QStringLiteral("thumbnailFailureContained"), failure_contained},
      {QStringLiteral("thumbnailSource"), source},
      {QStringLiteral("thumbnailFailureStage"), stage},
      {QStringLiteral("thumbnailFailureDetail"), detail},
  };
}

QVariantMap thumbnail_queue_data(
    const bool queue_pending,
    const QString& job_id,
    const QString& priority,
    const QString& reason,
    const int generation,
    const QString& blockers) {
  auto data = thumbnail_data(
      true,
      true,
      true,
      false,
      QStringLiteral("data:image/png;base64,queued"));
  data.insert(QStringLiteral("thumbnailQueuePending"), queue_pending);
  data.insert(QStringLiteral("thumbnailQueueJobId"), job_id);
  data.insert(QStringLiteral("thumbnailQueuePriority"), priority);
  data.insert(QStringLiteral("thumbnailQueueReason"), reason);
  data.insert(QStringLiteral("thumbnailQueueGeneration"), generation);
  data.insert(QStringLiteral("thumbnailQueueDeferBlockerSignature"), blockers);
  return data;
}

bool invoke_thumbnail_state(
    QObject* object,
    const char* method,
    const QVariantMap& data,
    QString* result) {
  QVariant return_value;
  const auto argument = QVariant::fromValue(data);
  if (!QMetaObject::invokeMethod(
          object,
          method,
          Q_RETURN_ARG(QVariant, return_value),
          Q_ARG(QVariant, argument))) {
    return false;
  }
  *result = return_value.toString();
  return true;
}

bool invoke_thumbnail_state(
    QObject* object,
    const char* method,
    const QVariantMap& data,
    const QString& context,
    QString* result) {
  QVariant return_value;
  const auto argument = QVariant::fromValue(data);
  if (!QMetaObject::invokeMethod(
          object,
          method,
          Q_RETURN_ARG(QVariant, return_value),
          Q_ARG(QVariant, argument),
          Q_ARG(QVariant, context))) {
    return false;
  }
  *result = return_value.toString();
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);

  QQmlEngine engine;
  const auto qml_path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                            .filePath(QStringLiteral(
                                "apps/desktop/qml/components/ThumbnailStateUtils.qml"));
  QQmlComponent component(&engine, QUrl::fromLocalFile(qml_path));
  if (component.isError()) {
    for (const auto& error : component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return fail("ThumbnailStateUtils.qml should load without QML errors");
  }

  std::unique_ptr<QObject> utility(component.create());
  if (!utility) {
    for (const auto& error : component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return fail("ThumbnailStateUtils.qml should create a utility object");
  }

  struct Case {
    const char* name;
    QVariantMap data;
    const char* display_state;
    const char* signature_fragment;
    const char* status_label;
    const char* retry_signature;
  };

  const Case cases[] = {
      {
          "ready",
          thumbnail_data(true, false, false, false, QStringLiteral("data:image/png;base64,abc")),
          "ready",
          "ready:true|stale:false|progressive:false|failure-contained:false|stage:|display:ready",
          "Layer visible",
          "thumbnail-retry|row:photo-layer|available:false|stage:|detail:",
      },
      {
          "progressive updating",
          thumbnail_data(true, true, true, false, QStringLiteral("image://editor-preview/thumb")),
          "progressive-updating",
          "ready:true|stale:true|progressive:true|failure-contained:false|stage:|display:progressive-updating",
          "Thumbnail updating",
          "thumbnail-retry|row:photo-layer|available:false|stage:|detail:",
      },
      {
          "contained failure",
          thumbnail_data(
              false,
              false,
              false,
              true,
              QStringLiteral(""),
              QStringLiteral("thumbnail_decode"),
              QStringLiteral("decode failed")),
          "contained-failure",
          "ready:false|stale:false|progressive:false|failure-contained:true|stage:thumbnail_decode|display:contained-failure",
          "Thumbnail unavailable / Thumbnail decode",
          "thumbnail-retry|row:photo-layer|available:true|stage:thumbnail_decode|detail:decode failed",
      },
      {
          "contained stale failure",
          thumbnail_data(
              false,
              true,
              true,
              true,
              QStringLiteral("data:image/png;base64,stale"),
              QStringLiteral("thumbnail_render"),
              QStringLiteral("render failed")),
          "contained-failure-stale",
          "ready:false|stale:true|progressive:true|failure-contained:true|stage:thumbnail_render|display:contained-failure-stale",
          "Thumbnail stale / Thumbnail render",
          "thumbnail-retry|row:photo-layer|available:true|stage:thumbnail_render|detail:render failed",
      },
      {
          "empty",
          thumbnail_data(true, false, false, false, QStringLiteral("")),
          "empty",
          "ready:true|stale:false|progressive:false|failure-contained:false|stage:|display:empty",
          "Layer visible",
          "thumbnail-retry|row:photo-layer|available:false|stage:|detail:",
      },
  };

  for (const auto& test_case : cases) {
    QString display_state;
    QString signature;
    QString status_label;
    QString retry_signature;
    if (!invoke_thumbnail_state(
            utility.get(),
            "thumbnailDisplayState",
            test_case.data,
            &display_state) ||
        display_state != QString::fromLatin1(test_case.display_state) ||
        !invoke_thumbnail_state(
            utility.get(),
            "thumbnailStateSignature",
            test_case.data,
            &signature) ||
        signature != QString::fromLatin1(test_case.signature_fragment) ||
        !invoke_thumbnail_state(
            utility.get(),
            "thumbnailStatusLabel",
            test_case.data,
            QStringLiteral("Layer visible"),
            &status_label) ||
        status_label != QString::fromLatin1(test_case.status_label) ||
        !invoke_thumbnail_state(
            utility.get(),
            "thumbnailRetryActionSignature",
            test_case.data,
            QStringLiteral("photo-layer"),
            &retry_signature) ||
        retry_signature != QString::fromLatin1(test_case.retry_signature)) {
      std::cerr << "Thumbnail state case failed: " << test_case.name
                << " display=" << display_state.toStdString()
                << " signature=" << signature.toStdString()
                << " status=" << status_label.toStdString()
                << " retry=" << retry_signature.toStdString() << '\n';
      return fail("thumbnail utility should return stable display, failure, and retry states");
    }
  }

  QString explicit_queue_signature;
  QString explicit_queue_pending;
  const auto explicit_queue = thumbnail_queue_data(
      true,
      QStringLiteral("thumbnail-refresh-queue-7"),
      QStringLiteral("low"),
      QStringLiteral("photo_layer_thumbnail_refresh_deferred"),
      7,
      QStringLiteral("live_brush_stroke,interactive_preview_worker"));
  if (!invoke_thumbnail_state(
          utility.get(),
          "thumbnailQueueSignature",
          explicit_queue,
          &explicit_queue_signature) ||
      explicit_queue_signature !=
          QStringLiteral("thumbnail-queue|deferred:true|job:thumbnail-refresh-queue-7|priority:low|generation:7|reason:photo_layer_thumbnail_refresh_deferred|blockers:live_brush_stroke,interactive_preview_worker") ||
      !invoke_thumbnail_state(
          utility.get(),
          "thumbnailRefreshQueued",
          explicit_queue,
          &explicit_queue_pending) ||
      explicit_queue_pending != QStringLiteral("true")) {
    std::cerr << "Explicit queue signature: "
              << explicit_queue_signature.toStdString()
              << " pending=" << explicit_queue_pending.toStdString() << '\n';
    return fail("thumbnail utility should prefer explicit backend queue metadata");
  }

  QString legacy_queue_signature;
  QString legacy_queue_pending;
  const auto legacy_queue = thumbnail_data(
      true,
      true,
      true,
      false,
      QStringLiteral("data:image/png;base64,legacy"));
  auto legacy_queue_with_reason = legacy_queue;
  legacy_queue_with_reason.insert(
      QStringLiteral("thumbnailProgressiveReason"),
      QStringLiteral("thumbnail_refresh_deferred"));
  if (!invoke_thumbnail_state(
          utility.get(),
          "thumbnailQueueSignature",
          legacy_queue_with_reason,
          &legacy_queue_signature) ||
      legacy_queue_signature !=
          QStringLiteral("thumbnail-queue|deferred:true|job:|priority:|generation:0|reason:thumbnail_refresh_deferred|blockers:") ||
      !invoke_thumbnail_state(
          utility.get(),
          "thumbnailRefreshQueued",
          legacy_queue_with_reason,
          &legacy_queue_pending) ||
      legacy_queue_pending != QStringLiteral("true")) {
    std::cerr << "Legacy queue signature: "
              << legacy_queue_signature.toStdString()
              << " pending=" << legacy_queue_pending.toStdString() << '\n';
    return fail("thumbnail utility should keep legacy progressive queue fallback");
  }

  auto stale_mismatch = legacy_queue;
  stale_mismatch.insert(QStringLiteral("thumbnailStaleDetected"), true);
  stale_mismatch.insert(QStringLiteral("thumbnailStaleMismatch"), true);
  stale_mismatch.insert(
      QStringLiteral("thumbnailStaleDetectionReason"),
      QStringLiteral("photo_layer_thumbnail_refresh_deferred"));
  stale_mismatch.insert(
      QStringLiteral("thumbnailStalePreviousRecipeHash"),
      QStringLiteral("sha1:previous"));
  stale_mismatch.insert(
      QStringLiteral("thumbnailStaleCurrentRecipeHash"),
      QStringLiteral("sha1:current"));
  QString stale_reason_label;
  QString stale_signature;
  if (!invoke_thumbnail_state(
          utility.get(),
          "thumbnailStaleReasonLabel",
          stale_mismatch,
          &stale_reason_label) ||
      stale_reason_label !=
          QStringLiteral("Thumbnail stale: photo_layer_thumbnail_refresh_deferred") ||
      !invoke_thumbnail_state(
          utility.get(),
          "thumbnailStaleSignature",
          stale_mismatch,
          &stale_signature) ||
      stale_signature !=
          QStringLiteral("thumbnail-stale|stale:true|detected:true|mismatch:true|reason:photo_layer_thumbnail_refresh_deferred|previous:sha1:previous|current:sha1:current")) {
    std::cerr << "Stale thumbnail label/signature: "
              << stale_reason_label.toStdString()
              << " / " << stale_signature.toStdString() << '\n';
    return fail("thumbnail utility should surface stale recipe mismatch details");
  }

  return EXIT_SUCCESS;
}
