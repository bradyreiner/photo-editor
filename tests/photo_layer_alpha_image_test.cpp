#include "photo_layer_source.h"
#include "photo_render_utils.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <string_view>

using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir fixture;
  QString path, output;
  const bool full = argc == 4 && std::string_view(argv[3]) == "--full-resolution";
  if (argc == 3 || full) {
    path = QString::fromLocal8Bit(argv[1]); output = QString::fromLocal8Bit(argv[2]);
    if (!QFileInfo(path).isFile() || !QFileInfo(output).isAbsolute() || QFileInfo::exists(output) || !QDir().mkpath(output)) return 1;
  } else if (argc == 1) {
    path = fixture.filePath("source.png"); output = fixture.path();
    QImage source(160, 120, QImage::Format_RGBA8888);
    source.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
    for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x)
      source.setPixelColor(x, y, QColor(x * 255 / source.width(), y * 255 / source.height(), 180));
    if (!source.save(path)) return 1;
  } else return 1;
  const auto decoded = decode_photo_layer_source_image(path, full ? 0 : 800);
  if (decoded.isNull()) return 1;
  const auto canvas = decoded.size();
  QImage last;
  int cases = 0;
  for (const auto* profile : {"Linear sRGB", "Linear Display P3", "Linear Rec.2020", "Linear ProPhoto RGB"})
    for (const auto* model : {"legacy-oklch-v1", "aces2-ocio-v1"}) for (int scale : {1, 2}) {
    if (full && (std::string_view(profile) != "Linear ProPhoto RGB" || std::string_view(model) != "aces2-ocio-v1" || scale != 1)) continue;
    const auto size = canvas * scale;
    QImage clear(size, QImage::Format_RGBA32FPx4); clear.fill(Qt::transparent);
    clear.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
    auto solid = clear; solid.fill(Qt::black);
    EditRecipeState state;
    state.working_color_profile = QString::fromLatin1(profile);
    state.photographic_rendering_model = QString::fromLatin1(model);
    QVariantMap layer{{"id", "photo-alpha"}, {"kind", "source"}, {"sourcePath", path},
        {"pixelWidth", canvas.width()}, {"pixelHeight", canvas.height()},
        {"canvasWidth", canvas.width()}, {"canvasHeight", canvas.height()}, {"opacity", 1.0}};
    state.layers = {layer};
    const auto reference = render_output_image_with_backend(solid, state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA64, RenderBackendPreference::CpuOnly);
    layer.insert("opacity", 0.35);
    layer.insert("maskWidth", 4); layer.insert("maskHeight", 1);
    QByteArray mask; for (int value : {0, 64, 128, 255}) mask.append(static_cast<char>(value));
    layer.insert("maskData", mask); layer.insert("maskOpacity", 0.8); layer.insert("maskDensity", 0.9);
    state.layers = {layer};
    state = parse_edit_recipe_json(serialize_edit_recipe_json(state));
    const auto actual = render_output_image_with_backend(clear, state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA64, RenderBackendPreference::CpuOnly);
    const auto preview = render_output_image_with_backend(clear, state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    if (reference.size() != size || actual.size() != size || preview.size() != size) return 1;
    int color_error = 0, alpha_error = 0, parity_error = 0;
    const auto output8 = actual.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < size.height(); ++y) {
      const auto* a = reinterpret_cast<const QRgba64*>(actual.constScanLine(y));
      const auto* r = reinterpret_cast<const QRgba64*>(reference.constScanLine(y));
      for (int x = 0; x < size.width(); ++x) {
        const auto mx = static_cast<int>(std::lround(3.0 * x / (size.width() - 1)));
        const auto coverage = std::min(static_cast<unsigned char>(mask[mx]) / 255.0, 0.9) * 0.8 * 0.35;
        alpha_error = std::max(alpha_error, std::abs(static_cast<int>(a[x].alpha()) - static_cast<int>(std::lround(65535 * coverage))));
        if (coverage > 0) color_error = std::max({color_error, std::abs(int(a[x].red()) - int(r[x].red())),
            std::abs(int(a[x].green()) - int(r[x].green())), std::abs(int(a[x].blue()) - int(r[x].blue()))});
        const auto p = preview.pixelColor(x, y), o = output8.pixelColor(x, y);
        parity_error = std::max({parity_error, std::abs(p.red() - o.red()), std::abs(p.green() - o.green()),
            std::abs(p.blue() - o.blue()), std::abs(p.alpha() - o.alpha())});
      }
    }
    std::cout << profile << '/' << model << ' ' << size.width() << 'x' << size.height()
              << " RGB16=" << color_error << " alpha16=" << alpha_error << " parity8=" << parity_error << '\n';
    if (color_error > 2 || alpha_error > 1 || parity_error > 1) return 1;
    last = actual; ++cases;
  }
  if (!last.save(QDir(output).filePath("masked-photo.png"))) return 1;
  QImage checker(last.size(), QImage::Format_RGB32);
  checker.setColorSpace(QColorSpace(QColorSpace::SRgb));
  QPainter painter(&checker);
  for (int y = 0; y < checker.height(); y += 32) for (int x = 0; x < checker.width(); x += 32)
    painter.fillRect(x, y, 32, 32, ((x / 32 + y / 32) % 2) ? QColor(190, 190, 190) : QColor(240, 240, 240));
  painter.drawImage(0, 0, last); painter.end();
  if (checker.height() > 32 && (checker.pixelColor(0, 0) != QColor(240, 240, 240)
      || checker.pixelColor(0, 32) != QColor(190, 190, 190))) {
    std::cerr << "Transparent mask region did not preserve the checker background\n";
    return 1;
  }
  if (!checker.save(QDir(output).filePath("checker-proof.png"))) return 1;
  std::cout << "Photographic layer transparency, persisted mask/opacity and 8/16-bit output cases=" << cases << " pass\n";
  return 0;
}
