#include "timeline_frame_quad_overscan.h"

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <iostream>

namespace {

using namespace photo_editor::desktop::timeline_motion::frame_warp;

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

bool near(double first, double second, double tolerance = 1.0e-7) {
  return std::abs(first - second) <= tolerance;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QSize frame_size(830, 467);
  FrameQuad css_quad;
  QString error;
  if (!css_perspective_rotate_y_quad(frame_size, 1500.0, -7.0,
                                     &css_quad, &error)) {
    std::cerr << "CSS FilmPolish quad should be valid: " << error.toStdString() << '\n';
    return 1;
  }
  if (!near(css_quad.points[0].x(), 0.019914, 2.0e-6) ||
      !near(css_quad.points[1].x(), 1.013590, 2.0e-6) ||
      !near(css_quad.points[1].y(), -0.017447, 2.0e-6) ||
      !near(css_quad.points[2].y(), 1.017447, 2.0e-6)) {
    return fail("CSS perspective fixture should preserve measured off-frame corner landmarks");
  }

  FrameQuadOverscanPlan plan;
  if (!make_frame_quad_overscan_plan(frame_size, css_quad, &plan, &error)) {
    std::cerr << "CSS perspective should have a bounded expanded canvas: "
              << error.toStdString() << '\n';
    return 1;
  }
  if (plan.padding.left() != plan.padding.right() ||
      plan.padding.top() != plan.padding.bottom() ||
      plan.output_size != QSize(frame_size.width() + 2 * plan.padding.left(),
                                frame_size.height() + 2 * plan.padding.top()) ||
      plan.original_origin != QPoint(plan.padding.left(), plan.padding.top()) ||
      plan.output_size.width() <= frame_size.width() ||
      plan.output_size.height() <= frame_size.height()) {
    return fail("overscan should be bounded, symmetric, and large enough to retain the quad");
  }
  if (!plan.perspective_filter.contains(QStringLiteral("perspective=")) ||
      !plan.visible_content_alpha_filter.contains(QStringLiteral("geq=")) ||
      plan.visible_content_alpha_filter.contains(QStringLiteral("perspective="))) {
    return fail("the overscan plan should separate warp corners from visible-content alpha coverage");
  }

  const std::array<QPointF, 4> source_corners{
      QPointF(0.0, 0.0),
      QPointF(static_cast<double>(frame_size.width()), 0.0),
      QPointF(static_cast<double>(frame_size.width()),
              static_cast<double>(frame_size.height())),
      QPointF(0.0, static_cast<double>(frame_size.height()))};
  const std::array<QPointF, 4> expanded_input_corners{
      QPointF(0.0, 0.0),
      QPointF(static_cast<double>(plan.output_size.width()), 0.0),
      QPointF(static_cast<double>(plan.output_size.width()),
              static_cast<double>(plan.output_size.height())),
      QPointF(0.0, static_cast<double>(plan.output_size.height()))};
  for (std::size_t index = 0; index < source_corners.size(); ++index) {
    const auto local_projected = plan.local_transform.map(source_corners.at(index));
    const auto expected_expanded = local_projected + QPointF(plan.original_origin);
    const auto& normalized_content = plan.visible_content_quad.points.at(index);
    const auto actual_expanded = QPointF(
        normalized_content.x() * static_cast<double>(plan.output_size.width()),
        normalized_content.y() * static_cast<double>(plan.output_size.height()));
    if (!near(actual_expanded.x(), expected_expanded.x()) ||
        !near(actual_expanded.y(), expected_expanded.y())) {
      return fail("visible content corners should retain their original source-coordinate mapping");
    }

    const auto actual_canvas_corner =
        plan.local_transform.map(expanded_input_corners.at(index) -
                                 QPointF(plan.original_origin)) +
        QPointF(plan.original_origin);
    const auto& normalized_canvas = plan.expanded_canvas_destination_quad.points.at(index);
    if (!near(actual_canvas_corner.x(),
              normalized_canvas.x() * static_cast<double>(plan.output_size.width())) ||
        !near(actual_canvas_corner.y(),
              normalized_canvas.y() * static_cast<double>(plan.output_size.height()))) {
      return fail("expanded FFmpeg corner metadata should use the conjugated padded homography");
    }
  }

  const auto shadow_padding = 30;
  const auto shadowed_output_size =
      QSize(plan.output_size.width() + 2 * shadow_padding,
            plan.output_size.height() + 2 * shadow_padding);
  const auto original_center_after_shadow =
      plan.original_center_in_output + QPointF(shadow_padding, shadow_padding);
  if (!near(original_center_after_shadow.x(),
            static_cast<double>(shadowed_output_size.width()) * 0.5) ||
      !near(original_center_after_shadow.y(),
            static_cast<double>(shadowed_output_size.height()) * 0.5)) {
    return fail("separate symmetric shadow padding should keep the original frame center fixed");
  }

  FrameQuad invalid;
  invalid.points = {QPointF(0.0, 0.0), QPointF(1.0, 0.0),
                    QPointF(0.0, 1.0), QPointF(1.0, 1.0)};
  if (make_frame_quad_overscan_plan(frame_size, invalid, &plan, nullptr)) {
    return fail("invalid self-crossing quads should not produce an overscan plan");
  }

  const auto ffmpeg_path = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  QTemporaryDir pixel_workspace;
  QImage source(16, 16, QImage::Format_RGBA8888);
  source.fill(QColor(240, 30, 15, 255));
  FrameQuad offscreen_quad;
  offscreen_quad.points = {QPointF(-0.125, 0.1), QPointF(0.875, 0.0),
                           QPointF(1.125, 0.9), QPointF(0.125, 1.0)};
  FrameQuadOverscanPlan pixel_plan;
  if (ffmpeg_path.isEmpty() || !pixel_workspace.isValid() ||
      !source.save(pixel_workspace.filePath(QStringLiteral("overscan-source.png"))) ||
      !make_frame_quad_overscan_plan(source.size(), offscreen_quad, &pixel_plan, &error)) {
    std::cerr << "FFmpeg overscan alpha fixture setup failed: " << error.toStdString() << '\n';
    return 1;
  }
  const auto pixel_filter = QStringLiteral(
      "format=rgba,pad=%1:%2:%3:%4:color=black@0,%5,%6")
      .arg(pixel_plan.output_size.width())
      .arg(pixel_plan.output_size.height())
      .arg(pixel_plan.padding.left())
      .arg(pixel_plan.padding.top())
      .arg(pixel_plan.perspective_filter)
      .arg(pixel_plan.visible_content_alpha_filter);
  QProcess renderer;
  renderer.start(ffmpeg_path,
      {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
       QStringLiteral("error"), QStringLiteral("-i"),
       pixel_workspace.filePath(QStringLiteral("overscan-source.png")),
       QStringLiteral("-vf"), pixel_filter, QStringLiteral("-frames:v"),
       QStringLiteral("1"), QStringLiteral("-vcodec"), QStringLiteral("png"),
       QStringLiteral("-f"), QStringLiteral("image2pipe"), QStringLiteral("pipe:1")});
  if (!renderer.waitForFinished(30000) || renderer.exitCode() != 0) {
    std::cerr << "FFmpeg overscan render failed: "
              << renderer.readAllStandardError().constData() << '\n';
    return 1;
  }
  const auto overscan_image = QImage::fromData(renderer.readAllStandardOutput(), "PNG")
                                  .convertToFormat(QImage::Format_RGBA8888);
  if (overscan_image.size() != pixel_plan.output_size ||
      overscan_image.pixelColor(0, 0).alpha() != 0 ||
      overscan_image.pixelColor(overscan_image.width() - 1,
                                overscan_image.height() - 1).alpha() != 0 ||
      overscan_image.pixelColor(2, 7).alpha() < 100 ||
      overscan_image.pixelColor(2, 7).red() < 160) {
    return fail("FFmpeg overscan should retain off-canvas red content and transparent exterior alpha");
  }
  return 0;
}
