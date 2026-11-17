#include "timeline_frame_quad_warp.h"

#include <QCoreApplication>
#include <QColor>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVariantList>

#include <cmath>
#include <iostream>
#include <utility>

namespace {

using namespace photo_editor::desktop::timeline_motion::frame_warp;

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

bool near(double first, double second, double tolerance = 1.0e-8) {
  return std::abs(first - second) <= tolerance;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QString error;
  FrameQuad film_card;
  if (!css_perspective_rotate_y_quad(QSize(1080, 1920), 1500.0, -7.0,
                                     &film_card, &error) ||
      !validate_frame_quad(film_card, &error)) {
    std::cerr << "CSS perspective fixture should produce a valid quad: "
              << error.toStdString() << '\n';
    return 1;
  }
  if (near(film_card.points[0].x(), film_card.points[1].x()) ||
      near(film_card.points[0].y(), film_card.points[1].y()) ||
      near(film_card.points[2].y(), film_card.points[3].y()) ||
      film_card.points[1].y() >= 0.0 || film_card.points[2].y() <= 1.0) {
    return fail("yawed CSS perspective should preserve its asymmetric off-canvas corners");
  }

  const auto encoded = frame_quad_to_variant_map(film_card);
  FrameQuad decoded;
  if (!frame_quad_from_variant_map(encoded, &decoded, &error)) {
    std::cerr << "A canonical frame quad should survive its QVariant projection: "
              << error.toStdString() << '\n';
    return 1;
  }
  for (std::size_t index = 0; index < film_card.points.size(); ++index) {
    if (!near(decoded.points[index].x(), film_card.points[index].x()) ||
        !near(decoded.points[index].y(), film_card.points[index].y())) {
      return fail("QVariant round-trip should preserve all four ordered corners");
    }
  }

  auto invalid = film_card;
  std::swap(invalid.points[1], invalid.points[3]);
  if (validate_frame_quad(invalid) ||
      validate_frame_quad(FrameQuad{{QPointF(0.0, 0.0), QPointF(1.0, 0.0),
                                     QPointF(2.0, 0.0), QPointF(0.0, 1.0)}})) {
    return fail("bow-tie and zero-area corner sets should be rejected");
  }
  invalid = film_card;
  invalid.points[0].setX(4.01);
  if (validate_frame_quad(invalid)) {
    return fail("frame quad corners outside the bounded normalized range should be rejected");
  }
  const auto disabled = frame_quad_to_variant_map(FrameQuad{}, false);
  if (!frame_quad_from_variant_map(disabled, &decoded, &error) ||
      disabled.contains(QStringLiteral("points"))) {
    return fail("clearing a static quad should retain the legacy disabled representation");
  }

  QTransform transform;
  if (!frame_quad_to_transform(QSize(1080, 1920), QSize(1080, 1920),
                               film_card, &transform, &error)) {
    std::cerr << "A valid frame quad should produce a stable homography: "
              << error.toStdString() << '\n';
    return 1;
  }
  const auto mapped_top_left = transform.map(QPointF(0.0, 0.0));
  if (!near(mapped_top_left.x(), film_card.points[0].x() * 1080.0, 1.0e-5) ||
      !near(mapped_top_left.y(), film_card.points[0].y() * 1920.0, 1.0e-5)) {
    return fail("native preview homography should map the source corners to the persisted destination");
  }
  const auto ffmpeg = ffmpeg_destination_perspective_filter(
      QSize(1080, 1920), film_card, &error);
  if (!ffmpeg.contains(QStringLiteral("x0=")) ||
      !ffmpeg.contains(QStringLiteral("x1=")) ||
      !ffmpeg.contains(QStringLiteral("x2=")) ||
      !ffmpeg.contains(QStringLiteral("x3=")) ||
      !ffmpeg.contains(QStringLiteral("sense=destination"))) {
    return fail("native delivery should emit all four static destination corners");
  }

  FrameQuad inset_quad;
  inset_quad.points = {QPointF(0.25, 0.25), QPointF(0.75, 0.25),
                       QPointF(0.75, 0.75), QPointF(0.25, 0.75)};
  const auto transparent_quad_filter =
      ffmpeg_destination_perspective_alpha_filter(QSize(16, 16), inset_quad, &error);
  if (transparent_quad_filter.isEmpty() ||
      !transparent_quad_filter.contains(QStringLiteral("geq="))) {
    std::cerr << "FFmpeg quad filter should include its transparent outside mask: "
              << error.toStdString() << '\n';
    return 1;
  }
  const auto ffmpeg_path = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  QTemporaryDir pixel_workspace;
  QImage pixel_source(16, 16, QImage::Format_RGBA8888);
  for (int y = 0; y < pixel_source.height(); ++y) {
    for (int x = 0; x < pixel_source.width(); ++x) {
      const auto color = x < 8 ? (y < 8 ? QColor(Qt::red) : QColor(Qt::green))
                               : (y < 8 ? QColor(Qt::blue) : QColor(Qt::yellow));
      pixel_source.setPixelColor(x, y, color);
    }
  }
  const auto source_path = pixel_workspace.filePath(QStringLiteral("quad-source.png"));
  if (ffmpeg_path.isEmpty() || !pixel_workspace.isValid() ||
      !pixel_source.save(source_path)) {
    return fail("the FFmpeg alpha pixel fixture should be available");
  }
  auto pixel_filter = transparent_quad_filter;
  if (pixel_filter.endsWith(QLatin1Char(','))) pixel_filter.chop(1);
  QProcess pixel_renderer;
  pixel_renderer.start(
      ffmpeg_path,
      {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
       QStringLiteral("error"), QStringLiteral("-i"), source_path,
       QStringLiteral("-vf"), pixel_filter, QStringLiteral("-frames:v"),
       QStringLiteral("1"), QStringLiteral("-vcodec"), QStringLiteral("png"),
       QStringLiteral("-f"), QStringLiteral("image2pipe"), QStringLiteral("pipe:1")});
  if (!pixel_renderer.waitForFinished(30000) || pixel_renderer.exitCode() != 0) {
    std::cerr << "FFmpeg quad alpha render failed: "
              << pixel_renderer.readAllStandardError().constData() << '\n';
    return 1;
  }
  const auto ffmpeg_quad_frame = QImage::fromData(pixel_renderer.readAllStandardOutput(), "PNG")
                                     .convertToFormat(QImage::Format_RGBA8888);
  if (ffmpeg_quad_frame.size() != QSize(16, 16) ||
      ffmpeg_quad_frame.pixelColor(0, 0).alpha() != 0 ||
      ffmpeg_quad_frame.pixelColor(15, 8).alpha() != 0 ||
      ffmpeg_quad_frame.pixelColor(8, 8).alpha() < 250 ||
      ffmpeg_quad_frame.pixelColor(4, 4).red() < 200 ||
      ffmpeg_quad_frame.pixelColor(11, 4).blue() < 200) {
    return fail("FFmpeg quad output should retain interior pixels and clear exterior alpha");
  }

  QImage source(20, 20, QImage::Format_ARGB32_Premultiplied);
  source.fill(Qt::transparent);
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      source.setPixelColor(x, y, x < 10 ? QColor(Qt::red) : QColor(Qt::blue));
    }
  }
  FrameQuad horizontal_keystone;
  horizontal_keystone.points = {
      QPointF(0.15, 0.1), QPointF(0.85, 0.1),
      QPointF(1.0, 0.9), QPointF(0.0, 0.9)};
  const auto warped = render_frame_quad_warp(source, QSize(100, 100),
                                             horizontal_keystone, &error);
  if (warped.isNull() || warped.pixelColor(50, 50).alpha() < 200 ||
      warped.pixelColor(35, 50).red() <= warped.pixelColor(65, 50).red()) {
    std::cerr << "Native image warp should preserve the source's left/right pixel ordering: "
              << error.toStdString() << '\n';
    return 1;
  }
  return 0;
}
