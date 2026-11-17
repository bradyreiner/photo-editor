#include "photo_board_layout.h"
#include <QCoreApplication>
#include <QBuffer>
#include <QJSEngine>
#include <QFile>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
QVariantMap photo(const char* id, int width, int height) {
  return {{"id", id}, {"kind", "source"}, {"pixelWidth", width}, {"pixelHeight", height},
          {"scale", 1.0}, {"rotation", 0.0}, {"x", 0}, {"y", 0}};
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QVariantList layers{photo("wide", 600, 400), photo("tall", 300, 600)};
  const auto original = layers;
  if (!layout_board_photos(&layers, {0}, "fit", {1200, 800}) ||
      layers[0].toMap().value("scale").toDouble() != 2.0 || layers[0].toMap().value("x").toInt() != 0)
    return fail("Full photo retains aspect and fits the canvas");
  layers = original;
  if (!layout_board_photos(&layers, {0, 1}, "stack", {1200, 800}) ||
      layers[0].toMap().value("scale").toDouble() != 2.0 ||
      layers[1].toMap().value("scale").toDouble() != 4.0 ||
      qAbs(board_photo_crop_bounds(layers[1].toMap(), QSizeF(300, 600)).height() * 4 - 400) > 0.001)
    return fail("Stacked photos fill equal halves with reversible source framing");
  layers = original;
  if (!layout_board_photos(&layers, {0, 1}, "columns", {1200, 800}) ||
      layers[0].toMap().value("x").toInt() != -300 || layers[1].toMap().value("x").toInt() != 600)
    return fail("Columns fill equal frames without changing source proportions");
  layers = original;
  if (!layout_board_photos(&layers, {1}, "fill", {1200, 800}) ||
      layers[1].toMap().value("scale").toDouble() != 4.0 || layers[1].toMap().value("y").toInt() != -800)
    return fail("Fill board covers the canvas without stretching pixels");
  layers = original;
  auto locked = layers[1].toMap(); locked.insert("locked", true); layers[1] = locked;
  const auto locked_state = layers;
  if (layout_board_photos(&layers, {0, 1}, "stack", {1200, 800}) || layers != locked_state ||
      layout_board_photos(&layers, {0, 0}, "stack", {1200, 800}) || layers != locked_state)
    return fail("Invalid or locked selections reject atomically");
  layers = original;
  layers.append(photo("third", 800, 300));
  const auto sources = layers;
  if (!layout_board_photos(&layers, {0, 1, 2}, "stack", {1200, 1200},
          {{"margin", 24}, {"spacing", 16}, {"border", 4}}))
    return fail("Three mixed-aspect photos support margins, spacing, and border room");
  double previous_bottom = 0;
  for (qsizetype index = 0; index < layers.size(); ++index) {
    const auto layer = layers[index].toMap();
    const auto source = sources[index].toMap();
    const double scale = layer.value("scale").toDouble();
    const auto crop = board_photo_crop_bounds(layer, QSizeF(source.value("pixelWidth").toDouble(), source.value("pixelHeight").toDouble()));
    const double y = layer.value("y").toDouble() + crop.y() * scale;
    if (layer.value("pixelWidth") != source.value("pixelWidth") ||
        layer.value("pixelHeight") != source.value("pixelHeight") ||
        y < previous_bottom + (index > 0 ? 16 : 24) - 1)
      return fail("Layout preserves source pixels and separates non-destructive frames");
    previous_bottom = y + crop.height() * scale;
  }
  const auto arranged = layers;
  if (layout_board_photos(&layers, {0, 1, 2}, "grid", {1200, 800}, {{"spacing", 2000}}) || layers != arranged)
    return fail("Impossible spacing rejects without modifying layers");
  QImage padded(100, 100, QImage::Format_ARGB32); padded.fill(Qt::transparent);
  for (int y = 30; y < 70; ++y) for (int x = 10; x < 90; ++x) padded.setPixel(x, y, qRgba(40, 80, 120, 255));
  QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); padded.save(&buffer, "PNG");
  auto legacy = photo("legacy-board-photo", 100, 100); legacy.insert("kind", "pixel"); legacy.insert("pixelData", bytes);
  layers = {legacy};
  if (!layout_board_photos(&layers, {0}, "fit", {800, 400}) ||
      layers[0].toMap().value("pixelWidth").toInt() != 80 ||
      layers[0].toMap().value("pixelHeight").toInt() != 40 ||
      QImage::fromData(layers[0].toMap().value("pixelData").toByteArray()).pixelColor(20, 20) != QColor(40, 80, 120))
    return fail("Legacy full-board transparent padding is removed without changing photo pixels");
  legacy.insert("maskData", QByteArray("mask-basis")); layers = {legacy};
  if (!layout_board_photos(&layers, {0}, "fit", {800, 400}) || layers[0].toMap().value("pixelData") != bytes)
    return fail("Masked layers retain their original pixel and mask coordinate basis");
  legacy.remove("maskData"); legacy.insert("effectStack", QStringLiteral("[{\"type\":\"blur\"}]")); layers = {legacy};
  if (!layout_board_photos(&layers, {0}, "fit", {800, 400}) || layers[0].toMap().value("pixelData") != bytes)
    return fail("Encoded effect stacks retain their original source coordinate basis");
  QFile script(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/BoardViewportGestures.js"));
  if (!script.open(QIODevice::ReadOnly)) return fail("Read production wheel policy");
  auto js = QString::fromUtf8(script.readAll()); js.remove(QStringLiteral(".pragma library"));
  QJSEngine engine;
  if (engine.evaluate(js).isError()) return fail("Load production wheel policy");
  const auto wheel = engine.globalObject().property("wheelAction");
  auto action = wheel.call({true, false, 28, -12, 0, 0});
  if (action.property("zoom").toBool() || action.property("x").toInt() != 28 || action.property("y").toInt() != -12)
    return fail("Two-finger Board scrolling pans in both axes");
  action = wheel.call({true, false, 0, 0, 0, -120});
  if (!action.property("zoom").toBool())
    return fail("A mouse wheel zooms directly without requiring a modifier");
  if (!wheel.call({true, true, 0, 12, 0, 0}).property("zoom").toBool() ||
      !wheel.call({false, false, 0, 12, 0, 0}).property("zoom").toBool())
    return fail("Modified Board wheel zoom and ordinary photo zoom remain available");
  std::cout << "Board layout and gesture checks passed\n";
  return 0;
}
