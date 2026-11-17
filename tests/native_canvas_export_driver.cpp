#include "edit_adjustment_patch.h"
#include "flattened_output_renderer.h"

#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <iostream>

// Exports one saved native board through the production photo renderer. This
// utility authors no pixels and never modifies the saved document or source.
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  if (app.arguments().size() != 3) {
    std::cerr << "Usage: native_canvas_export_driver <saved-document.json> <new-output.png>\n";
    return 2;
  }
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  const auto output = app.arguments()[2];
  if (QFileInfo::exists(output) || !output.endsWith(".png", Qt::CaseInsensitive)) {
    std::cerr << "Output must be a new PNG path.\n";
    return 2;
  }
  QFile file(app.arguments()[1]);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 256LL * 1024 * 1024) return 2;
  QJsonParseError error;
  const auto envelope = QJsonDocument::fromJson(file.readAll(), &error).object();
  if (error.error != QJsonParseError::NoError) return 2;
  const auto document = envelope.value("document").toObject();
  const auto slides = document.value("slides").toArray();
  if (slides.size() != 1 || !document.value("items").toArray().isEmpty()) {
    std::cerr << "Only a single photo board without board-level overlays is supported.\n";
    return 2;
  }
  const auto primary = slides[0].toObject().value("primary").toObject();
  const auto source = primary.value("filePath").toString();
  const auto patch = primary.value("adjustmentPatch").toObject();
  if (source.isEmpty() || patch.isEmpty()) return 2;
  const auto parsed = photo_editor::desktop::parse_adjustment_patch(patch);
  if (!parsed.ok()) { std::cerr << "Invalid native adjustment patch.\n"; return 2; }
  const auto image = photo_editor::desktop::render_flattened_output_file(
      envelope.value("documentId").toString(), source, *parsed.state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64);
  if (image.isNull()) { std::cerr << "Native render failed.\n"; return 1; }
  QSaveFile destination(output);
  if (!destination.open(QIODevice::WriteOnly)) return 1;
  QImageWriter writer(&destination, "png");
  if (!writer.write(image) || !destination.commit()) return 1;
  std::cout << "Native saved-canvas export: " << image.width() << 'x' << image.height()
            << " photo layers=" << parsed.state->layers.size() << '\n';
  return 0;
}
