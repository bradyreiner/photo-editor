#include <cstdlib>
#include <iostream>

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include "blank_canvas_document.h"

int main() {
  using namespace photo_editor::desktop;
  const auto fail = [](const char* message) { std::cerr << message << '\n'; return EXIT_FAILURE; };
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("Temporary canvas root unavailable");
  const auto root = temporary.filePath(QStringLiteral("documents"));
  const QVariantMap settings{{"width", 640}, {"height", 360}, {"name", "My / thumbnail"}, {"background", "transparent"}};
  const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto planned = planned_blank_canvas_source_path(root, settings, id);
  if (planned.isEmpty() || QFileInfo::exists(planned)) return fail("The source path must be known and absent before creation");
  if (!planned_blank_canvas_source_path(root, settings, QStringLiteral("../other")).isEmpty() ||
      !planned_blank_canvas_source_path(QStringLiteral("relative"), settings, id).isEmpty())
    return fail("Canvas admission must reject traversal and relative output roots");
  const auto cancelled = create_blank_canvas_source(root, settings, id, [] { return true; });
  if (!cancelled.value("cancelled").toBool() || QFileInfo::exists(root))
    return fail("Pre-cancelled creation must not create a source or document directory");
  const auto created = create_blank_canvas_source(root, settings, id);
  if (created.value("path") != planned || created.value("documentId") != id)
    return fail("Creation must use the exact admitted identity and source path");
  const QImage image(planned);
  if (image.size() != QSize(640, 360) || image.text("LooksyCanvasId") != id || image.pixelColor(20, 20).alpha() != 0)
    return fail("Canvas identity, dimensions and alpha must survive the actual PNG encoding");
  QFile source(planned);
  if (!source.open(QIODevice::ReadOnly)) return fail("Created source cannot be read");
  const auto before = source.readAll();
  source.close();
  if (create_blank_canvas_source(root, {{"width", 20}, {"height", 20}}, id).value("error").toString().isEmpty())
    return fail("An admitted document identity must never overwrite an existing document folder");
  if (!source.open(QIODevice::ReadOnly) || source.readAll() != before)
    return fail("A rejected duplicate identity must preserve all existing source bytes");
  const auto stopped_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto stopped_directory = QDir(root).filePath(stopped_id);
  const auto stopped = create_blank_canvas_source(root, settings, stopped_id,
      [&] { return QFileInfo::exists(stopped_directory); });
  if (!stopped.value("cancelled").toBool() || QFileInfo::exists(stopped_directory) || !QFileInfo::exists(planned))
    return fail("Cancellation after directory creation must clean only that unfinished document");
  std::cout << "Admitted canvas identity, source pixels, collision preservation and cancellation passed\n";
  return EXIT_SUCCESS;
}
