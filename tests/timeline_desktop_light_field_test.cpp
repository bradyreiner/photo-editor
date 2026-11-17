#include "timeline_desktop_light_field.h"
#include "timeline_frame_mask.h"
#include "timeline_linear_gradient.h"

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QProcess>
#include <QPointF>
#include <QStandardPaths>
#include <QSize>
#include <QSizeF>

#include <cmath>
#include <iostream>
#include <limits>

namespace {

bool check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return false;
  }
  return true;
}

bool same_rgb(const QImage& left, const QImage& right, int x, int y) {
  return left.pixelColor(x, y).rgb() == right.pixelColor(x, y).rgb();
}

}  // namespace

int main() {
  using photo_editor::desktop::timeline_motion::DesktopLightFieldParameters;
  using photo_editor::desktop::timeline_motion::desktop_light_field_geq_filter;
  using photo_editor::desktop::timeline_motion::render_desktop_light_field;
  using photo_editor::desktop::timeline_motion::render_linear_gradient_frame;
  using photo_editor::desktop::timeline_motion::render_transition_veil_frame;
  using photo_editor::desktop::timeline_motion::rounded_rect_alpha_filter;
  using photo_editor::desktop::timeline_motion::transition_veil_geq_filter;
  using photo_editor::desktop::timeline_motion::transition_veil_path;

  const QSize size(19, 11);
  const DesktopLightFieldParameters parameters{3.125, 0.18, 0.42};
  const auto first = render_desktop_light_field(size, parameters);
  if (!check(!first.isNull(), "valid light-field dimensions render a frame") ||
      !check(first.size() == size, "the rendered frame preserves sequence dimensions") ||
      !check(first.format() == QImage::Format_RGBA8888, "the frame keeps straight-alpha RGBA") ||
      !check(first.pixelColor(8, 5).alpha() == 107, "canvas opacity is represented by alpha")) {
    return 1;
  }

  const auto repeated = render_desktop_light_field(size, parameters);
  if (!check(first == repeated, "a time sample renders deterministically")) {
    return 1;
  }
  auto changed_time = parameters;
  changed_time.time_seconds += 0.5;
  const auto later = render_desktop_light_field(size, changed_time);
  auto gathered = parameters;
  gathered.gather = 1.0;
  const auto gathered_image = render_desktop_light_field(size, gathered);
  if (!check(!same_rgb(first, later, 8, 5), "the internal caustics respond to time") ||
      !check(!same_rgb(first, gathered_image, 8, 5), "gather changes the light-field shape")) {
    return 1;
  }

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (!ffmpeg.isEmpty()) {
    const auto geq_filter = desktop_light_field_geq_filter(
        QStringLiteral("3.125"), QStringLiteral("0.18"));
    const auto lavfi_source = QStringLiteral("color=c=black:s=%1x%2:r=30,%3")
                                  .arg(size.width())
                                  .arg(size.height())
                                  .arg(geq_filter);
    QProcess renderer;
    renderer.start(ffmpeg,
                   {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                    QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                    QStringLiteral("-i"), lavfi_source, QStringLiteral("-frames:v"),
                    QStringLiteral("1"), QStringLiteral("-vcodec"), QStringLiteral("png"),
                    QStringLiteral("-f"), QStringLiteral("image2pipe"), QStringLiteral("pipe:1")});
    if (!renderer.waitForFinished(30000) || renderer.exitCode() != 0) {
      std::cerr << "FFmpeg geq render failed: "
                << renderer.readAllStandardError().constData() << '\n';
      return 1;
    }
    const auto ffmpeg_frame = QImage::fromData(renderer.readAllStandardOutput(), "PNG")
                                  .convertToFormat(QImage::Format_RGBA8888);
    if (!check(ffmpeg_frame.size() == size,
               "the FFmpeg per-frame generator renders the requested canvas")) {
      return 1;
    }
    for (int y = 0; y < size.height(); ++y) {
      for (int x = 0; x < size.width(); ++x) {
        const auto cpu_pixel = first.pixelColor(x, y);
        const auto ffmpeg_pixel = ffmpeg_frame.pixelColor(x, y);
        if (std::abs(cpu_pixel.red() - ffmpeg_pixel.red()) > 1 ||
            std::abs(cpu_pixel.green() - ffmpeg_pixel.green()) > 1 ||
            std::abs(cpu_pixel.blue() - ffmpeg_pixel.blue()) > 1) {
          std::cerr << "CPU and FFmpeg light-field renderers diverged at " << x << ',' << y
                    << ": " << cpu_pixel.red() << ',' << cpu_pixel.green() << ','
                    << cpu_pixel.blue() << " vs " << ffmpeg_pixel.red() << ','
                    << ffmpeg_pixel.green() << ',' << ffmpeg_pixel.blue() << '\n';
          return 1;
        }
      }
    }

    const auto veil_filter = transition_veil_geq_filter(
        QStringLiteral("3.125"), QStringLiteral("0.18"), QStringLiteral("0"));
    const auto veil_source = QStringLiteral("color=c=black:s=%1x%2:r=30,%3")
                                 .arg(size.width())
                                 .arg(size.height())
                                 .arg(veil_filter);
    QProcess veil_renderer;
    veil_renderer.start(ffmpeg,
                       {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                        QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), veil_source, QStringLiteral("-frames:v"),
                        QStringLiteral("1"), QStringLiteral("-vcodec"), QStringLiteral("png"),
                        QStringLiteral("-f"), QStringLiteral("image2pipe"), QStringLiteral("pipe:1")});
    if (!veil_renderer.waitForFinished(30000) || veil_renderer.exitCode() != 0) {
      std::cerr << "FFmpeg transition-veil render failed: "
                << veil_renderer.readAllStandardError().constData() << '\n';
      return 1;
    }
    const auto veil_frame = QImage::fromData(veil_renderer.readAllStandardOutput(), "PNG")
                                .convertToFormat(QImage::Format_RGBA8888);
    const auto veil_reference = render_transition_veil_frame(
                                    size, 0.0, 3.125, 0.18, 1.0, 0.42)
                                    .convertToFormat(QImage::Format_RGBA8888);
    if (!check(veil_frame.size() == size && veil_reference.size() == size,
               "both transition-veil renderers produce the requested canvas")) {
      return 1;
    }
    for (int y = 0; y < size.height(); ++y) {
      for (int x = 0; x < size.width(); ++x) {
        const auto native_pixel = veil_reference.pixelColor(x, y);
        const auto ffmpeg_pixel = veil_frame.pixelColor(x, y);
        if (std::abs(native_pixel.red() - ffmpeg_pixel.red()) > 2 ||
            std::abs(native_pixel.green() - ffmpeg_pixel.green()) > 2 ||
            std::abs(native_pixel.blue() - ffmpeg_pixel.blue()) > 2 ||
            std::abs(native_pixel.alpha() - ffmpeg_pixel.alpha()) > 2) {
          std::cerr << "Transition-veil renderers diverged at " << x << ',' << y
                    << ": " << native_pixel.red() << ',' << native_pixel.green() << ','
                    << native_pixel.blue() << ',' << native_pixel.alpha() << " vs "
                    << ffmpeg_pixel.red() << ',' << ffmpeg_pixel.green() << ','
                    << ffmpeg_pixel.blue() << ',' << ffmpeg_pixel.alpha() << '\n';
          return 1;
        }
      }
    }

    const QVariantList gradient_stops = {
        QVariantMap{{QStringLiteral("position"), 0.08},
                    {QStringLiteral("color"), QStringLiteral("#080c13")},
                    {QStringLiteral("opacity"), 1.0}},
        QVariantMap{{QStringLiteral("position"), 0.30},
                    {QStringLiteral("color"), QStringLiteral("#080c13")},
                    {QStringLiteral("opacity"), 0.91}},
        QVariantMap{{QStringLiteral("position"), 0.80},
                    {QStringLiteral("color"), QStringLiteral("#080c13")},
                    {QStringLiteral("opacity"), 0.0}},
    };
    const auto gradient_reference = render_linear_gradient_frame(
                                        size, 90.0, gradient_stops)
                                        .convertToFormat(QImage::Format_RGBA8888);
    if (!check(gradient_reference.size() == size,
               "the editable gradient source produces the requested canvas")) {
      return 1;
    }
    const auto left_alpha = gradient_reference.pixelColor(0, size.height() / 2).alpha();
    const auto middle_alpha = gradient_reference.pixelColor(size.width() / 2, size.height() / 2).alpha();
    const auto right_alpha = gradient_reference.pixelColor(size.width() - 1, size.height() / 2).alpha();
    if (!check(left_alpha > middle_alpha && middle_alpha > right_alpha,
               "gradient stop opacity falls off across the editable alpha ramp")) {
      return 1;
    }

    const auto rounded_filter = rounded_rect_alpha_filter(QStringLiteral("0.35"));
    const auto rounded_source = QStringLiteral("color=c=red:s=%1x%2:r=30,%3format=rgba")
                                    .arg(size.width())
                                    .arg(size.height())
                                    .arg(rounded_filter);
    QProcess rounded_renderer;
    rounded_renderer.start(ffmpeg,
                           {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                            QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                            QStringLiteral("-i"), rounded_source, QStringLiteral("-frames:v"),
                            QStringLiteral("1"), QStringLiteral("-vcodec"), QStringLiteral("png"),
                            QStringLiteral("-f"), QStringLiteral("image2pipe"), QStringLiteral("pipe:1")});
    if (!rounded_renderer.waitForFinished(30000) || rounded_renderer.exitCode() != 0) {
      std::cerr << "FFmpeg rounded-frame render failed: "
                << rounded_renderer.readAllStandardError().constData() << '\n';
      return 1;
    }
    const auto rounded_frame = QImage::fromData(rounded_renderer.readAllStandardOutput(), "PNG")
                                   .convertToFormat(QImage::Format_RGBA8888);
    if (!check(rounded_frame.size() == size,
               "rounded native frame mask preserves the source dimensions") ||
        !check(rounded_frame.pixelColor(0, 0).alpha() == 0,
               "rounded frame clips its outer corner") ||
        !check(rounded_frame.pixelColor(size.width() / 2, size.height() / 2).alpha() == 255,
               "rounded frame preserves its interior") ||
        !check(rounded_frame.pixelColor(1, 0).alpha() > 20 &&
                   rounded_frame.pixelColor(1, 0).alpha() < 235,
               "rounded frame antialiases its curved corner edge")) {
      return 1;
    }
  }

  auto invisible = parameters;
  invisible.opacity = 0.0;
  const auto transparent = render_desktop_light_field(size, invisible);
  if (!check(transparent.pixelColor(8, 5).alpha() == 0,
             "zero canvas opacity produces a transparent source") ||
      !check(same_rgb(first, transparent, 8, 5),
             "opacity changes compositing alpha, not shader RGB")) {
    return 1;
  }

  if (!check(render_desktop_light_field({}, parameters).isNull(),
             "invalid dimensions are rejected") ||
      !check(render_desktop_light_field(
                 size,
                 {std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0})
                 .isNull(),
             "non-finite shader time is rejected")) {
    return 1;
  }

  const QSizeF canvas(1920.0, 1080.0);
  const auto before = transition_veil_path(canvas, -0.5);
  const auto middle = transition_veil_path(canvas, 0.0);
  const auto after = transition_veil_path(canvas, 0.5);
  if (!check(!before.contains(QPointF(960.0, 540.0)),
             "the curved matte begins off canvas") ||
      !check(middle.contains(QPointF(960.0, 540.0)),
             "the curved matte crosses the canvas midpoint") ||
      !check(!after.contains(QPointF(960.0, 540.0)),
             "the curved matte finishes off canvas") ||
      !check(transition_veil_path(canvas, -0.5001).isEmpty(),
             "the veil is inactive outside its authored half-second window")) {
    return 1;
  }

  return 0;
}
