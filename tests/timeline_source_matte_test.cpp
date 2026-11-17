#include "timeline_source_matte.h"

#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

using photo_editor::desktop::timeline_source_matte_alpha;
using photo_editor::desktop::timeline_source_matte_ffmpeg_chain;

int main() {
  QImage matte(3, 1, QImage::Format_ARGB32);
  matte.setPixelColor(0, 0, QColor("#ffffffff"));
  matte.setPixelColor(1, 0, QColor("#ff808080"));
  matte.setPixelColor(2, 0, QColor("#00ffffff"));

  const auto luminance = timeline_source_matte_alpha(
      matte, QStringLiteral("luminance"), false);
  if (luminance.isNull() ||
      luminance.pixelColor(0, 0).alpha() != 255 ||
      luminance.pixelColor(1, 0).alpha() < 54 ||
      luminance.pixelColor(1, 0).alpha() > 56 ||
      luminance.pixelColor(2, 0).alpha() != 0) {
    return 1;
  }

  const auto inverted = timeline_source_matte_alpha(
      matte, QStringLiteral("luminance"), true);
  if (inverted.isNull() ||
      inverted.pixelColor(0, 0).alpha() != 0 ||
      inverted.pixelColor(2, 0).alpha() != 255) {
    return 2;
  }

  const auto alpha = timeline_source_matte_alpha(
      matte, QStringLiteral("alpha"), false, QSize(6, 2));
  if (alpha.isNull() || alpha.size() != QSize(6, 2) ||
      alpha.pixelColor(0, 0).alpha() != 255 ||
      alpha.pixelColor(5, 1).alpha() != 0) {
    return 3;
  }

  QString error;
  const auto chain = timeline_source_matte_ffmpeg_chain(
      QStringLiteral("12:v"), QStringLiteral("13:v"),
      QStringLiteral("matted"), QStringLiteral("matte_test"),
      QSize(1920, 1080), QColor("#80c7a2ff"), &error);
  if (!error.isEmpty() || chain.isEmpty() ||
      !chain.contains(QStringLiteral("geq=r='199':g='162':b='255'")) ||
      !chain.contains(QStringLiteral("all_mode=multiply")) ||
      !chain.contains(QStringLiteral("alphamerge"))) {
    std::cerr << "Source matte FFmpeg chain was not formed: " << error.toStdString() << '\n';
    return 4;
  }

  if (!timeline_source_matte_alpha(
           matte, QStringLiteral("unsupported"), false).isNull()) {
    return 5;
  }

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  QTemporaryDir temporary;
  if (ffmpeg.isEmpty() || !temporary.isValid()) {
    std::cerr << "FFmpeg and an isolated folder are required for matte pixel verification\n";
    return 6;
  }
  QImage source(3, 1, QImage::Format_ARGB32);
  source.fill(QColor(4, 5, 6, 200));
  const auto source_path = temporary.filePath(QStringLiteral("source.png"));
  const auto matte_path = temporary.filePath(QStringLiteral("matte-alpha.png"));
  const auto output_path = temporary.filePath(QStringLiteral("matted.png"));
  if (!source.save(source_path) || !luminance.save(matte_path)) {
    std::cerr << "Matte FFmpeg pixel fixture images should be writable\n";
    return 7;
  }
  const auto pixel_graph = timeline_source_matte_ffmpeg_chain(
      QStringLiteral("0:v"), QStringLiteral("1:v"), QStringLiteral("matted"),
      QStringLiteral("pixel_matte"), QSize(3, 1), QColor(QStringLiteral("#80c7a2ff")), &error);
  if (pixel_graph.isEmpty() || !error.isEmpty()) {
    std::cerr << "Matte FFmpeg pixel graph should be valid: " << error.toStdString() << '\n';
    return 8;
  }
  QProcess render;
  render.start(ffmpeg, {
      QStringLiteral("-v"), QStringLiteral("error"),
      QStringLiteral("-i"), source_path,
      QStringLiteral("-i"), matte_path,
      QStringLiteral("-filter_complex"), pixel_graph,
      QStringLiteral("-map"), QStringLiteral("[matted]"),
      QStringLiteral("-frames:v"), QStringLiteral("1"),
      QStringLiteral("-y"), output_path,
  });
  if (!render.waitForFinished(30000) ||
      render.exitStatus() != QProcess::NormalExit || render.exitCode() != 0) {
    std::cerr << "Matte FFmpeg pixel graph failed: "
              << render.readAllStandardError().toStdString() << '\n';
    return 9;
  }
  const QImage rendered(output_path);
  const auto opaque_matte = rendered.pixelColor(0, 0);
  const auto gray_matte = rendered.pixelColor(1, 0);
  const auto transparent_matte = rendered.pixelColor(2, 0);
  if (rendered.size() != QSize(3, 1) ||
      std::abs(opaque_matte.red() - 199) > 1 ||
      std::abs(opaque_matte.green() - 162) > 1 ||
      std::abs(opaque_matte.blue() - 255) > 1 ||
      std::abs(opaque_matte.alpha() - 100) > 1 ||
      std::abs(gray_matte.red() - 199) > 1 ||
      std::abs(gray_matte.green() - 162) > 1 ||
      std::abs(gray_matte.blue() - 255) > 1 ||
      std::abs(gray_matte.alpha() - 22) > 2 ||
      transparent_matte.alpha() != 0) {
    std::cerr << "Matte FFmpeg pixels should preserve fill RGB and multiply source, fill, and luma alpha; got "
              << opaque_matte.red() << ',' << opaque_matte.green() << ','
              << opaque_matte.blue() << ',' << opaque_matte.alpha() << " / "
              << gray_matte.alpha() << " / " << transparent_matte.alpha() << '\n';
    return 10;
  }
  std::cout << "Source matte alpha, linear luminance, inversion and export graph passed\n";
  return 0;
}
