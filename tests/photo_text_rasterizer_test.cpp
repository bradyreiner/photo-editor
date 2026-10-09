#include "photo_text_rasterizer.h"

#include <QGuiApplication>
#include <iostream>
#include <future>

QRect ink_bounds(const QImage& image) {
  QRect bounds;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x)
      if (image.pixelColor(x, y).alpha() > 0) bounds = bounds.united(QRect(x, y, 1, 1));
  return bounds;
}

int main(int argc, char** argv) {
  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--without-gui")) {
    QCoreApplication app(argc, argv);
    if (!photo_editor::desktop::rasterize_legacy_photo_text({{"text", "Visible text"}}).isNull()) {
      std::cerr << "Missing font runtime must fail instead of producing transparent text\n";
      return 1;
    }
    return 0;
  }
  QGuiApplication app(argc, argv);
  QVariantMap text{{"text", "Headline"}, {"fontSize", 32}, {"pixelWidth", 400},
      {"pixelHeight", 100}, {"textColor", "#ffffff"}};
  auto styled_layer = text;
  photo_editor::desktop::PhotoTextStyle style;
  style.italic = true;
  style.size = 24.375;
  styled_layer.insert("textTypography", photo_editor::desktop::encode_photo_text_style(style).toVariantMap());
  const auto styled = photo_editor::desktop::rasterize_legacy_photo_text(styled_layer);
  if (styled.isNull() || ink_bounds(styled).isEmpty()) return 1;
  styled_layer.insert("textTypography", QVariantMap{{"version", 99}});
  QString unsupported_error;
  if (!photo_editor::desktop::rasterize_legacy_photo_text(styled_layer, {}, &unsupported_error).isNull() ||
      unsupported_error.isEmpty()) return 1;
  const auto worker_image = std::async(std::launch::async, [text] {
    return photo_editor::desktop::rasterize_legacy_photo_text(text);
  }).get();
  const auto left = photo_editor::desktop::rasterize_legacy_photo_text(text);
  const auto left_bounds = ink_bounds(left);
  auto translucent_legacy_text = text;
  translucent_legacy_text.insert(QStringLiteral("textColor"), QStringLiteral("#7fffffff"));
  const auto translucent_legacy = photo_editor::desktop::rasterize_legacy_photo_text(translucent_legacy_text);
  int legacy_maximum_alpha = 0;
  for (int y = 0; y < translucent_legacy.height(); ++y)
    for (int x = 0; x < translucent_legacy.width(); ++x)
      legacy_maximum_alpha = std::max(legacy_maximum_alpha, translucent_legacy.pixelColor(x, y).alpha());
  if (legacy_maximum_alpha == 0 || legacy_maximum_alpha > 127) {
    std::cerr << "Legacy text rasterization must preserve stored alpha\n";
    return 1;
  }
  const auto enlarged = photo_editor::desktop::rasterize_legacy_photo_text(text, QSize(800, 200));
  const auto enlarged_bounds = ink_bounds(enlarged);
  if (enlarged.size() != QSize(800, 200) || enlarged_bounds.isEmpty() ||
      std::abs(enlarged_bounds.left() - 2 * left_bounds.left()) > 3 ||
      std::abs(enlarged_bounds.width() - 2 * left_bounds.width()) > 6 ||
      std::abs(enlarged_bounds.height() - 2 * left_bounds.height()) > 6) {
    std::cerr << "Requested-resolution typography must preserve logical layout\n";
    return 1;
  }
  if (!photo_editor::desktop::rasterize_legacy_photo_text(text, QSize(20000, 20000)).isNull())
    return 1;
  for (const auto invalid : {QSize(0, 100), QSize(100, 0), QSize(-2, 100)}) {
    if (!photo_editor::desktop::rasterize_legacy_photo_text(text, invalid).isNull()) {
      std::cerr << "Invalid requested dimensions must not silently select the default raster\n";
      return 1;
    }
  }
  if (worker_image != left) {
    std::cerr << "Background text rasterization must match the GUI-thread result\n";
    return 1;
  }
  text.insert("textAlignment", "right");
  const auto right = photo_editor::desktop::rasterize_legacy_photo_text(text);
  const auto right_bounds = ink_bounds(right);
  if (left.size() != QSize(400, 100) || left_bounds.isEmpty() || right_bounds.isEmpty() ||
      right_bounds.left() <= left_bounds.left() + 100) {
    std::cerr << "Text must produce glyphs and honor horizontal alignment\n";
    return 1;
  }
  auto missing_font = text;
  missing_font.insert("fontFamily", "PhotoEditorDeliberatelyMissingFont-9475");
  if (ink_bounds(photo_editor::desktop::rasterize_legacy_photo_text(missing_font)).isEmpty()) {
    std::cerr << "Missing font fallback must still produce visible glyphs\n";
    return 1;
  }
  text.insert("text", "");
  if (!ink_bounds(photo_editor::desktop::rasterize_legacy_photo_text(text)).isEmpty()) {
    std::cerr << "Empty text must remain transparent\n";
    return 1;
  }
  text.insert("text", QString::fromUtf8("Caf\xc3\xa9\nSecond line"));
  const auto multiline = photo_editor::desktop::rasterize_legacy_photo_text(text);
  if (ink_bounds(multiline).height() <= left_bounds.height()) {
    std::cerr << "Multiline Unicode text must produce multiple lines\n";
    return 1;
  }
}
