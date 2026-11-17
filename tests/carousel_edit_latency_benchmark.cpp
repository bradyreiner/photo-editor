#include "app_bootstrap.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "edit_recipe_state.h"

#include <QDir>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <iostream>

using namespace photo_editor::desktop;

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static EditRecipeState state(const EditorPreviewController& editor) { return editor.currentState(); }
};
}

int main(int argc, char** argv) {
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  if (argc != 3) {
    std::cerr << "Usage: carousel_edit_latency_benchmark document-copy.json result.json\n";
    return 2;
  }
  QTemporaryDir runtime;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/unavailable");
  if (!runtime.isValid() || !AppBootstrap::ensureRuntimeDirectories()) return 1;
  QFile input(QString::fromLocal8Bit(argv[1]));
  if (!input.open(QIODevice::ReadOnly)) return 1;
  auto document = QJsonDocument::fromJson(input.readAll()).object();
  if (document.value("document_json").isObject()) document = document.value("document_json").toObject();
  QJsonObject media;
  for (const auto& value : document.value("groups").toArray()) {
    const auto group = value.toObject();
    if (group.value("startIndex").toInt() == 1) media = group.value("media").toObject();
  }
  const auto source = media.value("filePath").toString();
  const auto patch = QString::fromUtf8(QJsonDocument(media.value("adjustmentPatch").toObject()).toJson(QJsonDocument::Compact));
  if (source.isEmpty() || patch.isEmpty()) return 1;
  EditorPreviewImageProvider images;
  EditorPreviewController editor;
  editor.setImageProvider(&images);
  editor.setPreviewTargetEdge(1280);
  editor.setScopesVisible(false);

  QElapsedTimer phase;
  QElapsedTimer heartbeat_clock;
  qint64 first_feedback_ms = -1;
  qint64 maximum_heartbeat_gap_ms = 0;
  qint64 last_heartbeat_ms = 0;
  QString previous_source;
  int previous_native_revision = 0;
  QTimer heartbeat;
  heartbeat.setInterval(8);
  QObject::connect(&heartbeat, &QTimer::timeout, [&] {
    const auto now = heartbeat_clock.elapsed();
    maximum_heartbeat_gap_ms = qMax(maximum_heartbeat_gap_ms, now - last_heartbeat_ms);
    last_heartbeat_ms = now;
  });
  QObject::connect(&editor, &EditorPreviewController::previewChanged, [&] {
    if (first_feedback_ms >= 0) return;
    if ((editor.displayPreviewSource().startsWith("image://") &&
         editor.displayPreviewSource() != previous_source && !images.image("edited").isNull()) ||
        (editor.displayPreviewNativeAvailable() && editor.displayPreviewNativeRevision() != previous_native_revision))
      first_feedback_ms = phase.elapsed();
  });
  const auto begin_phase = [&] {
    first_feedback_ms = -1;
    maximum_heartbeat_gap_ms = 0;
    last_heartbeat_ms = 0;
    previous_source = editor.displayPreviewSource();
    previous_native_revision = editor.displayPreviewNativeRevision();
    heartbeat_clock.start();
    phase.start();
    heartbeat.start();
  };
  const auto finish_phase = [&](qint64 call_ms) {
    QElapsedTimer idle;
    while (phase.elapsed() < 60000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 8);
      if (first_feedback_ms >= 0 && !editor.processing() && !editor.fullRenderRunning()) {
        if (!idle.isValid()) idle.start();
        if (idle.elapsed() > 160) break;
      } else idle.invalidate();
      QThread::msleep(1);
    }
    heartbeat.stop();
    const auto published = images.image("edited");
    return QJsonObject{{"call_ms", call_ms}, {"first_feedback_ms", first_feedback_ms},
        {"settled_ms", phase.elapsed()}, {"max_gui_heartbeat_gap_ms", qMax(call_ms, maximum_heartbeat_gap_ms)},
        {"native_size_width", editor.displayPreviewNativeSize().width()},
        {"native_size_height", editor.displayPreviewNativeSize().height()},
        {"native_available", editor.displayPreviewNativeAvailable()},
        {"published_width", published.width()}, {"published_height", published.height()},
        {"published_pixel_sha256", QString::fromLatin1(QCryptographicHash::hash(
            QByteArrayView(reinterpret_cast<const char*>(published.constBits()), published.sizeInBytes()),
            QCryptographicHash::Sha256).toHex())},
        {"success", first_feedback_ms >= 0 && !editor.processing() && !editor.fullRenderRunning()}};
  };

  begin_phase();
  editor.loadAsset("board:carousel-latency-fixture", source, {}, {}, {}, {}, patch, true);
  const auto load = finish_phase(phase.elapsed());
  auto source_state = EditorPreviewControllerTestAccess::state(editor);
  int text_index = -1;
  for (int i = 0; i < source_state.layers.size(); ++i)
    if (source_state.layers.at(i).toMap().value("kind").toString() == "text") { text_index = i; break; }
  if (text_index < 0) return 1;
  const auto typography = editor.photoLayerTypography(text_index);
  begin_phase();
  const bool styled = editor.setPhotoLayerTypography(text_index, {{"size", typography.value("size", 48).toDouble() + 1.0}});
  const auto style = finish_phase(phase.elapsed());
  const auto state = EditorPreviewControllerTestAccess::state(editor);
  const auto serialized = serialize_edit_recipe_json(state);
  const auto receipt = QJsonObject{{"load", load}, {"style", style}, {"style_applied", styled},
      {"layer_count", state.layers.size()}, {"source_width", editor.sourceImageWidth()},
      {"source_height", editor.sourceImageHeight()}, {"recipe_sha256", QString::fromLatin1(
          QCryptographicHash::hash(serialized.toUtf8(), QCryptographicHash::Sha256).toHex())},
      {"platform", QGuiApplication::platformName()}, {"scopes_visible", editor.scopesVisible()}};
  QFile output(QString::fromLocal8Bit(argv[2]));
  QDir().mkpath(QFileInfo(output.fileName()).absolutePath());
  if (!output.open(QIODevice::WriteOnly)) return 1;
  output.write(QJsonDocument(receipt).toJson());
  std::cout << QJsonDocument(receipt).toJson(QJsonDocument::Compact).constData() << '\n';
  return styled && load.value("success").toBool() && style.value("success").toBool() ? 0 : 1;
}
