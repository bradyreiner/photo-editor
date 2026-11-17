#pragma once

#include <QImage>
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop::timeline_motion {

// These are the authored controls exposed by desktop-launch/Light.tsx. The
// source alpha is opaque; opacity models the CSS canvas opacity when the
// source is composited over the timeline picture.
struct DesktopLightFieldParameters final {
  double time_seconds = 0.0;
  double gather = 0.0;
  double opacity = 1.0;
};

namespace detail {

inline QString ffmpeg_sequence_expression(
    const QStringList& assignments,
    const QString& result) {
  QStringList terms;
  terms.reserve(assignments.size() + 1);
  for (const auto& assignment : assignments) {
    terms.push_back(QStringLiteral("(%1)*0").arg(assignment));
  }
  terms.push_back(result);
  return QStringLiteral("(%1)").arg(terms.join(QLatin1Char('+')));
}

inline QString ffmpeg_hash_expression(const QString& x, const QString& y) {
  const auto sample = QStringLiteral("(sin((%1)*127.1+(%2)*311.7)*43758.5453)")
                          .arg(x, y);
  return QStringLiteral("(%1-floor(%1))").arg(sample);
}

inline QString ffmpeg_noise_expression(const QString& x, const QString& y) {
  const auto ix = QStringLiteral("floor(%1)").arg(x);
  const auto iy = QStringLiteral("floor(%1)").arg(y);
  const auto fx = QStringLiteral("((%1)-(%2))").arg(x, ix);
  const auto fy = QStringLiteral("((%1)-(%2))").arg(y, iy);
  const auto sx = QStringLiteral("((%1)*(%1)*(3-2*(%1)))").arg(fx);
  const auto sy = QStringLiteral("((%1)*(%1)*(3-2*(%1)))").arg(fy);
  const auto hash00 = ffmpeg_hash_expression(ix, iy);
  const auto hash10 = ffmpeg_hash_expression(QStringLiteral("(%1)+1").arg(ix), iy);
  const auto hash01 = ffmpeg_hash_expression(ix, QStringLiteral("(%1)+1").arg(iy));
  const auto hash11 = ffmpeg_hash_expression(
      QStringLiteral("(%1)+1").arg(ix), QStringLiteral("(%1)+1").arg(iy));
  return QStringLiteral(
             "((1-(%1))*((1-(%2))*(%3)+(%2)*(%4))+"
             "(%1)*((1-(%2))*(%5)+(%2)*(%6)))")
      .arg(sy, sx, hash00, hash10, hash01, hash11);
}

inline QString ffmpeg_fbm_expression(const QString& x, const QString& y) {
  const auto first = ffmpeg_noise_expression(x, y);
  const auto second = ffmpeg_noise_expression(
      QStringLiteral("(%1)*2.03").arg(x), QStringLiteral("(%1)*2.03").arg(y));
  const auto third = ffmpeg_noise_expression(
      QStringLiteral("(%1)*4.07").arg(x), QStringLiteral("(%1)*4.07").arg(y));
  return QStringLiteral("(0.57*(%1)+0.28*(%2)+0.15*(%3))")
      .arg(first, second, third);
}

inline QString ffmpeg_channel_expression(int channel) {
  const auto channel_value = QString::number(channel);
  const auto phase = QStringLiteral("(ld(2)*0.57+%1*0.56)").arg(channel_value);
  const auto centre = QStringLiteral(
      "(0.51+0.19*sin(ld(0)*4.3-%1)+0.055*cos(ld(0)*9.1+%1)+(%2-1)*0.075)")
                          .arg(phase, channel_value);
  const auto band = QStringLiteral("(ld(1)-ld(5)+(ld(4)-0.5)*0.07)");
  const auto thickness = QStringLiteral("(0.085+0.055*(0.5+0.5*sin(ld(0)*4.8+%1)))")
                             .arg(phase);
  const auto sheet = QStringLiteral("exp(-pow(%1/%2,4))").arg(band, thickness);
  const auto core = QStringLiteral(
      "exp(-pow((%1+0.013*sin(ld(0)*15+%2))/(%3*0.2),2))")
                        .arg(band, phase, thickness);
  const auto rings = QStringLiteral(
      "sqrt(pow(ld(0)-0.5+(%1-1)*0.04,2)+pow((ld(1)-0.52)*H/W,2))")
                         .arg(channel_value);
  const auto disk = QStringLiteral("exp(-pow((%1-0.29)/0.075,2))").arg(rings);
  const auto caustic = QStringLiteral(
      "pow(0.5+0.5*sin(ld(4)*15+(%1)*25-ld(2)*0.2),3)")
                           .arg(band);
  const auto dispersed = QStringLiteral("(%1*0.53+%2*0.17+%3*%1*0.45)")
                             .arg(sheet, core, caustic);
  const auto collected = QStringLiteral("(%1*(0.4+0.6*%2))").arg(disk, caustic);
  return ffmpeg_sequence_expression(
      {QStringLiteral("st(5,%1)").arg(centre),
       QStringLiteral("st(6,%1)").arg(band),
       QStringLiteral("st(7,%1)").arg(thickness),
       QStringLiteral("st(8,%1)").arg(dispersed),
       QStringLiteral("st(9,%1)").arg(collected),
      },
      QStringLiteral("ld(8)*(1-ld(3))+ld(9)*ld(3)"));
}

inline QString ffmpeg_light_channel_expression(
    int channel,
    float background,
    const QString& time_expression,
    const QString& gather_expression) {
  const auto noise = ffmpeg_fbm_expression(
      QStringLiteral("(ld(0)*4+ld(2)*0.12)"),
      QStringLiteral("(ld(1)*4-ld(2)*0.08)"));
  const auto field = ffmpeg_channel_expression(channel);
  const auto grain = ffmpeg_hash_expression(
      QStringLiteral("floor((X+0.5)*0.7)"),
      QStringLiteral("floor((H-Y-0.5)*0.7)"));
  return ffmpeg_sequence_expression(
      {QStringLiteral("st(0,(X+0.5)/W)"),
       QStringLiteral("st(1,(Y+0.5)/H)"),
       QStringLiteral("st(2,%1)").arg(time_expression),
       QStringLiteral("st(3,%1)").arg(gather_expression),
       QStringLiteral("st(4,%1)").arg(noise),
       QStringLiteral("st(5,%1)").arg(field)},
      QStringLiteral("(%1+pow(ld(5)*(0.67+0.48*%2),0.8)*0.94)*255")
          .arg(QString::number(background, 'f', 6), grain));
}

inline double fract(double value) noexcept {
  return value - std::floor(value);
}

inline double shader_hash(double x, double y) noexcept {
  return fract(std::sin(x * 127.1 + y * 311.7) * 43758.5453);
}

inline double shader_noise(double x, double y) noexcept {
  const auto ix = std::floor(x);
  const auto iy = std::floor(y);
  auto fx = fract(x);
  auto fy = fract(y);
  fx = fx * fx * (3.0 - 2.0 * fx);
  fy = fy * fy * (3.0 - 2.0 * fy);
  const auto lower = shader_hash(ix, iy) * (1.0F - fx) +
                     shader_hash(ix + 1.0F, iy) * fx;
  const auto upper = shader_hash(ix, iy + 1.0F) * (1.0F - fx) +
                     shader_hash(ix + 1.0F, iy + 1.0F) * fx;
  return lower * (1.0F - fy) + upper * fy;
}

inline double shader_fbm(double x, double y) noexcept {
  return 0.57 * shader_noise(x, y) +
         0.28 * shader_noise(x * 2.03, y * 2.03) +
         0.15 * shader_noise(x * 4.07, y * 4.07);
}

inline double light_channel(
    double x,
    double y,
    double channel,
    double aspect_ratio,
    double time,
    double gather) noexcept {
  const auto phase = time * 0.57 + channel * 0.56;
  const auto n = shader_fbm(x * 4.0 + time * 0.12,
                            y * 4.0 - time * 0.08);
  auto centre = 0.51 + 0.19 * std::sin(x * 4.3 - phase) +
                0.055 * std::cos(x * 9.1 + phase);
  centre += (channel - 1.0) * 0.075;
  const auto band = y - centre + (n - 0.5) * 0.07;
  const auto thickness = 0.085 +
                         0.055 * (0.5 + 0.5 * std::sin(x * 4.8 + phase));
  const auto broad_band = band / thickness;
  const auto sheet = std::exp(-std::pow(broad_band, 4.0));
  const auto core_band =
      (band + 0.013 * std::sin(x * 15.0 + phase)) / (thickness * 0.2);
  const auto core = std::exp(-std::pow(core_band, 2.0));
  const auto ring_x = x - 0.5 + (channel - 1.0) * 0.04;
  const auto ring_y = (y - 0.52) * aspect_ratio;
  const auto rings = std::sqrt(ring_x * ring_x + ring_y * ring_y);
  const auto disk = std::exp(-std::pow((rings - 0.29) / 0.075, 2.0));
  const auto caustic = std::pow(
      0.5 + 0.5 * std::sin(n * 15.0 + band * 25.0 - time * 0.2),
      3.0);
  const auto dispersed = sheet * 0.53 + core * 0.17 + caustic * sheet * 0.45;
  const auto collected = disk * (0.4 + 0.6 * caustic);
  return dispersed * (1.0 - gather) + collected * gather;
}

inline int quantize_unorm8(double value) noexcept {
  const auto clamped = std::clamp(value, 0.0, 1.0);
  return static_cast<int>(std::lround(clamped * 255.0F));
}

}  // namespace detail

// FFmpeg's geq frame filter is the timeline renderer's per-frame CPU path.
// Keep its expression generated from the same scalar equations as the QImage
// implementation so previews and exports share the procedural source clock.
[[nodiscard]] inline QString desktop_light_field_geq_filter(
    const QString& time_expression,
    const QString& gather_expression) {
  const auto red = detail::ffmpeg_light_channel_expression(
      0, 0.043F, time_expression, gather_expression);
  const auto green = detail::ffmpeg_light_channel_expression(
      1, 0.051F, time_expression, gather_expression);
  const auto blue = detail::ffmpeg_light_channel_expression(
      2, 0.071F, time_expression, gather_expression);
  return QStringLiteral("format=rgba,geq=r='%1':g='%2':b='%3':a='255'")
      .arg(red, green, blue);
}

// Native FFmpeg source for the moving cubic matte authored in
// creative-launch/TransitionVeil.tsx. The y controls at 0.3H and 0.7H make
// the Bezier's y coordinate linear, so its x edge has this closed-form cubic.
[[nodiscard]] inline QString transition_veil_geq_filter(
    const QString& light_time_expression,
    const QString& gather_expression,
    const QString& local_time_expression,
    double field_opacity = 0.42) {
  const auto bounded_field_opacity = std::clamp(field_opacity, 0.0, 1.0);
  const auto light_red = detail::ffmpeg_light_channel_expression(
      0, 0.043F, light_time_expression, gather_expression);
  const auto light_green = detail::ffmpeg_light_channel_expression(
      1, 0.051F, light_time_expression, gather_expression);
  const auto light_blue = detail::ffmpeg_light_channel_expression(
      2, 0.071F, light_time_expression, gather_expression);
  const auto opacity = QString::number(bounded_field_opacity, 'g', 12);
  const auto matte_time = QStringLiteral("clip((%1)+0.5,0,1)").arg(local_time_expression);
  const auto progress = QStringLiteral("((%1)*(%1)*(%1)*(10+(%1)*(-15+6*(%1))))")
                            .arg(matte_time);
  const auto left = QStringLiteral("((-1.5+2.6*(%1))*W)").arg(progress);
  const auto right = QStringLiteral("((%1)+1.4*W)").arg(left);
  const auto curve = QStringLiteral("(0.06*W)");
  const auto row = QStringLiteral("((Y+0.5)/H)");
  const auto front_edge = QStringLiteral(
      "((%1)+3*(%2)*(%3)*(1-(%3))*(2*(%3)-1))")
                              .arg(left, curve, row);
  const auto trailing_edge = QStringLiteral(
      "((%1)+3*(%2)*(%3)*(1-(%3))*(1-2*(%3)))")
                                  .arg(right, curve, row);
  const auto x = QStringLiteral("(X+0.5)");
  const auto edge_coverage = QStringLiteral(
      "clip((%1)-(%2)+0.5,0,1)*clip((%3)-(%1)+0.5,0,1)")
                                 .arg(x, front_edge, trailing_edge);
  const auto red = QStringLiteral("(11*(1-%1)+(%2)*%1)")
                       .arg(opacity, light_red);
  const auto green = QStringLiteral("(13*(1-%1)+(%2)*%1)")
                         .arg(opacity, light_green);
  const auto blue = QStringLiteral("(18*(1-%1)+(%2)*%1)")
                        .arg(opacity, light_blue);
  return QStringLiteral("format=rgba,geq=r='%1':g='%2':b='%3':a='255*(%4)'")
      .arg(red, green, blue, edge_coverage);
}

// Deterministic CPU translation of the fragment shader in
// marketing/animated-trailer/src/desktop-launch/Light.tsx. Pixel centers,
// bottom-left gl_FragCoord grain, RGB channel phases, and nonlinear response
// match that shader. Preview and export can call this same function at the
// timeline's requested frame time.
[[nodiscard]] inline QImage render_desktop_light_field(
    const QSize& size,
    DesktopLightFieldParameters parameters = {}) {
  if (!size.isValid() || size.isEmpty() ||
      !std::isfinite(parameters.time_seconds) ||
      !std::isfinite(parameters.gather) || !std::isfinite(parameters.opacity)) {
    return {};
  }

  const auto time = parameters.time_seconds;
  const auto gather = parameters.gather;
  const auto opacity = std::clamp(parameters.opacity, 0.0, 1.0);
  const auto alpha = detail::quantize_unorm8(opacity);
  const auto aspect_ratio = static_cast<double>(size.height()) /
                            static_cast<double>(size.width());
  QImage image(size, QImage::Format_RGBA8888);
  if (image.isNull()) {
    return {};
  }

  for (int row = 0; row < size.height(); ++row) {
    auto* pixels = image.scanLine(row);
    const auto y = (static_cast<double>(row) + 0.5) /
                   static_cast<double>(size.height());
    const auto gl_y = static_cast<double>(size.height() - row) - 0.5;
    for (int column = 0; column < size.width(); ++column) {
      const auto x = (static_cast<double>(column) + 0.5) /
                     static_cast<double>(size.width());
      const auto grain_x = std::floor((static_cast<double>(column) + 0.5) * 0.7);
      const auto grain_y = std::floor(gl_y * 0.7);
      const auto grain = detail::shader_hash(grain_x, grain_y);
      auto red = detail::light_channel(x, y, 0.0, aspect_ratio, time, gather);
      auto green = detail::light_channel(x, y, 1.0, aspect_ratio, time, gather);
      auto blue = detail::light_channel(x, y, 2.0, aspect_ratio, time, gather);
      const auto grain_gain = 0.67 + 0.48 * grain;
      red = std::pow(red * grain_gain, 0.8);
      green = std::pow(green * grain_gain, 0.8);
      blue = std::pow(blue * grain_gain, 0.8);
      const auto pixel_offset = static_cast<qsizetype>(column) * 4;
      pixels[pixel_offset] = static_cast<uchar>(detail::quantize_unorm8(0.043 + red * 0.94));
      pixels[pixel_offset + 1] = static_cast<uchar>(detail::quantize_unorm8(0.051 + green * 0.94));
      pixels[pixel_offset + 2] = static_cast<uchar>(detail::quantize_unorm8(0.071 + blue * 0.94));
      pixels[pixel_offset + 3] = static_cast<uchar>(alpha);
    }
  }
  return image;
}

// Native path for the travelling matte authored in creative-launch/TransitionVeil.tsx.
// Its smooth() helper is the quintic smootherstep 6t^5 - 15t^4 + 10t^3.
[[nodiscard]] inline QPainterPath transition_veil_path(
    const QSizeF& canvas_size,
    double local_time_seconds) {
  QPainterPath path;
  if (!std::isfinite(canvas_size.width()) || !std::isfinite(canvas_size.height()) ||
      canvas_size.width() <= 0.0 || canvas_size.height() <= 0.0 ||
      !std::isfinite(local_time_seconds) || local_time_seconds < -0.5 ||
      local_time_seconds > 0.5) {
    return path;
  }
  const auto ramp = std::clamp(local_time_seconds + 0.5, 0.0, 1.0);
  const auto ramp2 = ramp * ramp;
  const auto ramp3 = ramp2 * ramp;
  const auto progress = ramp3 * (10.0 + ramp * (-15.0 + 6.0 * ramp));
  const auto width = canvas_size.width();
  const auto height = canvas_size.height();
  const auto x = (-1.5 + 2.6 * progress) * width;
  const auto right = x + 1.4 * width;
  const auto curve = 0.06 * width;
  path.moveTo(x, 0.0);
  path.cubicTo(x - curve, height * 0.3, x + curve, height * 0.7, x, height);
  path.lineTo(right, height);
  path.cubicTo(right + curve, height * 0.7, right - curve, height * 0.3, right, 0.0);
  path.closeSubpath();
  return path;
}

[[nodiscard]] inline QImage render_transition_veil_frame(
    const QSize& size,
    double local_time_seconds,
    double light_time_seconds,
    double gather = 0.18,
    double opacity = 1.0,
    double field_opacity = 0.42) {
  if (!size.isValid() || size.isEmpty() || !std::isfinite(light_time_seconds) ||
      !std::isfinite(gather) || !std::isfinite(opacity) ||
      !std::isfinite(field_opacity)) {
    return {};
  }
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) {
    return {};
  }
  image.fill(Qt::transparent);
  const auto path = transition_veil_path(QSizeF(size), local_time_seconds);
  if (path.isEmpty()) {
    return image;
  }
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setOpacity(std::clamp(opacity, 0.0, 1.0));
  painter.setClipPath(path);
  painter.fillRect(image.rect(), QColor(QStringLiteral("#0b0d12")));
  painter.drawImage(
      QPoint(0, 0),
      render_desktop_light_field(
          size,
          DesktopLightFieldParameters{
              .time_seconds = light_time_seconds,
              .gather = gather,
              .opacity = std::clamp(field_opacity, 0.0, 1.0)}));
  return image;
}

}  // namespace photo_editor::desktop::timeline_motion
