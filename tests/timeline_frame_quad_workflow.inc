#include "timeline_frame_quad_animation.h"

bool run_timeline_frame_quad_workflow(const QString& workspace_path) {
  using photo_editor::desktop::TimelineDocumentController;
  const auto check = [](bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
  };
  const auto inspected_clip = [](const QVariantMap& envelope) {
    return envelope.value(QStringLiteral("clip")).toMap();
  };
  using namespace photo_editor::desktop::timeline_motion::frame_warp;
  const auto points_for_quad = [](const FrameQuad& quad) {
    QVariantList points;
    points.reserve(4);
    for (const auto& point : quad.points) {
      points.push_back(QVariantMap{{QStringLiteral("x"), point.x()},
                                   {QStringLiteral("y"), point.y()}});
    }
    return points;
  };

  FrameQuad identity_quad;
  FrameQuad folded_endpoint;
  folded_endpoint.points = {
      QPointF(0.0, 0.75), QPointF(0.0, 0.5),
      QPointF(0.25, 0.25), QPointF(0.75, 0.0)};
  if (!check(validate_frame_quad(folded_endpoint),
             "the animation oracle should accept both independently convex endpoints"))
    return false;
  FrameQuadAnimation folded_animation;
  for (std::size_t channel_index = 0; channel_index < folded_animation.size();
       ++channel_index) {
    const auto point_index = channel_index / 2U;
    const auto coordinate = (channel_index % 2U) == 0U
                                ? folded_endpoint.points.at(point_index).x()
                                : folded_endpoint.points.at(point_index).y();
    const auto identity_coordinate = (channel_index % 2U) == 0U
                                         ? identity_quad.points.at(point_index).x()
                                         : identity_quad.points.at(point_index).y();
    folded_animation.at(channel_index) = {
        FrameQuadScalarKeyframe{0.0, identity_coordinate, false},
        FrameQuadScalarKeyframe{1.0, coordinate, false}};
  }
  QString animation_error;
  if (!check(!validate_frame_quad_animation(identity_quad, folded_animation,
                                            1.0, &animation_error),
             "the animation oracle should reject convex endpoints whose linear path folds between keys"))
    return false;

  FrameQuadAnimation held_animation;
  held_animation.at(0) = {
      FrameQuadScalarKeyframe{0.0, 0.1, true},
      FrameQuadScalarKeyframe{1.0, 0.2, false}};
  if (!check(qAbs(evaluate_frame_quad_animation(identity_quad, held_animation, 0.999).points.at(0).x() - 0.1) < 1.0e-8 &&
                 qAbs(evaluate_frame_quad_animation(identity_quad, held_animation, 1.0).points.at(0).x() - 0.2) < 1.0e-8,
             "a hold interval should switch to the exact right key only at its timestamp"))
    return false;

  FrameQuad css_start;
  if (!check(css_perspective_rotate_y_quad(QSize(830, 467), 1500.0, -7.0,
                                           &css_start, &animation_error),
             "the animation oracle should derive the 830x467 CSS perspective pose")) {
    std::cerr << animation_error.toStdString() << '\n';
    return false;
  }
  FrameQuadAnimation css_animation;
  for (std::size_t channel_index = 0; channel_index < css_animation.size();
       ++channel_index) {
    const auto point_index = channel_index / 2U;
    const auto start_coordinate = (channel_index % 2U) == 0U
                                      ? css_start.points.at(point_index).x()
                                      : css_start.points.at(point_index).y();
    const auto end_coordinate = (channel_index % 2U) == 0U
                                    ? identity_quad.points.at(point_index).x()
                                    : identity_quad.points.at(point_index).y();
    css_animation.at(channel_index) = {
        FrameQuadScalarKeyframe{0.0, start_coordinate, false},
        FrameQuadScalarKeyframe{1.0, end_coordinate, false}};
  }
  FrameQuadAnimationOverscanPlan animated_plan;
  if (!check(make_frame_quad_animation_overscan_plan(
                 QSize(830, 467), css_start, css_animation, 1.0,
                 &animated_plan, &animation_error) &&
                 animated_plan.output_size.width() >= 830 &&
                 animated_plan.output_size.height() >= 467,
             "the full linear trajectory should receive one bounded, frame-stable overscan plan")) {
    std::cerr << animation_error.toStdString() << '\n';
    return false;
  }
  for (const auto local_seconds : {0.0, 0.5, 1.0}) {
    const auto pose = evaluate_frame_quad_animation(
        css_start, css_animation, local_seconds);
    FrameQuad expanded_pose;
    FrameQuad content_pose;
    if (!check(conjugate_frame_quad_for_overscan(
                   QSize(830, 467), animated_plan.padding, pose,
                   &expanded_pose, &content_pose, &animation_error) &&
                   validate_frame_quad(expanded_pose) &&
                   validate_frame_quad(content_pose),
               "each animated pose should conjugate through the same padded canvas")) {
      std::cerr << animation_error.toStdString() << '\n';
      return false;
    }
  }
  std::array<QString, 8> perspective_clock_expressions;
  std::array<QString, 8> alpha_clock_expressions;
  for (std::size_t index = 0; index < perspective_clock_expressions.size(); ++index) {
    const auto start = css_animation.at(index).front().value;
    const auto end = css_animation.at(index).back().value;
    perspective_clock_expressions.at(index) = QStringLiteral(
        "(%1+(%2-%1)*clip((((on-1)/30)-0)/1,0,1))")
            .arg(QString::number(start, 'f', 9), QString::number(end, 'f', 9));
    alpha_clock_expressions.at(index) = QStringLiteral(
        "(%1+(%2-%1)*clip((T-0)/1,0,1))")
            .arg(QString::number(start, 'f', 9), QString::number(end, 'f', 9));
  }
  const auto animated_perspective_filter =
      ffmpeg_animated_destination_perspective_filter(
          QSize(830, 467), animated_plan.padding, perspective_clock_expressions,
          &animation_error);
  const auto animated_alpha_filter =
      ffmpeg_animated_destination_perspective_alpha_filter(
          QSize(830, 467), animated_plan.padding, alpha_clock_expressions,
          &animation_error);
  if (!check(animated_perspective_filter.contains(QStringLiteral("eval=frame")) &&
                 animated_perspective_filter.contains(QStringLiteral("on-1")) &&
                 animated_alpha_filter.contains(QStringLiteral("N+1")) &&
                 animated_alpha_filter.contains(QStringLiteral("T-0")) &&
                 animated_alpha_filter.contains(QStringLiteral("alpha(X,Y)")),
             "animated perspective should use FFmpeg's verified frame counter while alpha uses the same normalized timestamp clock"))
    return false;

  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Static perspective quad")},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 180},
      {QStringLiteral("frameRate"), 30.0},
  });
  QString track_id;
  for (const auto& row_value : controller.sequenceTracks()) {
    const auto row = row_value.toMap();
    if (row.value(QStringLiteral("trackKind")).toString() == QStringLiteral("video") &&
        !row.value(QStringLiteral("locked")).toBool()) {
      track_id = row.value(QStringLiteral("trackId")).toString();
      break;
    }
  }
  if (!check(!track_id.isEmpty(), "the perspective-quad fixture should find an unlocked visual track"))
    return false;
  const auto created = controller.createTimelineTitleForAutomation(
      track_id, QStringLiteral("basic"), 0.0, 2.0, {});
  const auto clip_id = created.value(QStringLiteral("createdClipId")).toString();
  if (!check(created.value(QStringLiteral("ok")).toBool() && !clip_id.isEmpty(),
             "the perspective-quad fixture should create a stable editable clip"))
    return false;

  const auto legacy_document =
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(controller);
  for (const auto& track_value : legacy_document.value(QStringLiteral("tracks")).toArray()) {
    for (const auto& clip_value : track_value.toObject().value(QStringLiteral("clips")).toArray()) {
      const auto clip = clip_value.toObject();
      if (clip.value(QStringLiteral("clipId")).toString() != clip_id) continue;
      if (!check(!clip.contains(QStringLiteral("framePerspectiveQuad")),
                 "legacy default clips must not gain a serialized disabled perspective-quad field"))
        return false;
      for (const auto& override_value :
           clip.value(QStringLiteral("outputVariantOverrides")).toObject()) {
        if (!check(!override_value.toObject().contains(QStringLiteral("framePerspectiveQuad")),
                   "legacy output variants must not gain a serialized disabled perspective-quad field"))
          return false;
      }
    }
  }
  if (!check(controller.saveTimeline(false),
             "the legacy baseline should save before testing the optional quad schema"))
    return false;
  auto saved_legacy_document =
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(controller);
  TimelineDocumentController legacy_reopened;
  if (!check(legacy_reopened.loadTimeline(controller.savedDocumentPath()),
             "the legacy baseline should reopen through the normal native loader"))
    return false;
  auto reopened_legacy_document =
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(legacy_reopened);
  saved_legacy_document.remove(QStringLiteral("updatedAt"));
  reopened_legacy_document.remove(QStringLiteral("updatedAt"));
  if (!check(saved_legacy_document == reopened_legacy_document,
             "legacy native document hashing should remain byte-equivalent apart from updatedAt"))
    return false;

  const QVariantList points{
      QVariantMap{{QStringLiteral("x"), 0.08}, {QStringLiteral("y"), -0.02}},
      QVariantMap{{QStringLiteral("x"), 0.94}, {QStringLiteral("y"), 0.04}},
      QVariantMap{{QStringLiteral("x"), 1.08}, {QStringLiteral("y"), 1.03}},
      QVariantMap{{QStringLiteral("x"), -0.03}, {QStringLiteral("y"), 0.96}},
  };
  const auto applied = controller.setTimelineClipPerspectiveQuadForAutomation(
      clip_id, true, points);
  const auto projected = applied.value(QStringLiteral("clip")).toMap()
                             .value(QStringLiteral("framePerspectiveQuad")).toMap();
  if (!check(applied.value(QStringLiteral("ok")).toBool() &&
                 applied.value(QStringLiteral("changed")).toBool() &&
                 projected.value(QStringLiteral("enabled")).toBool() &&
                 projected.value(QStringLiteral("points")).toList() == points,
             "the controller should persist and project all four ordered corners"))
    return false;

  const auto before_rejection = controller.inspectTimelineClipById(clip_id);
  const QVariantList bow_tie{
      points.at(0), points.at(2), points.at(1), points.at(3)};
  const auto rejected = controller.setTimelineClipPerspectiveQuadForAutomation(
      clip_id, true, bow_tie);
  if (!check(!rejected.value(QStringLiteral("ok")).toBool() &&
                 controller.inspectTimelineClipById(clip_id) == before_rejection,
             "invalid corner geometry must be rejected without changing the clip"))
    return false;

  if (!check(controller.saveTimeline(false),
             "a static perspective quad should save through the native Timeline pipeline"))
    return false;
  const auto saved_path = controller.savedDocumentPath();
  TimelineDocumentController reopened;
  if (!check(!saved_path.isEmpty() && reopened.loadTimeline(saved_path),
             "a static perspective quad project should reopen from its canonical path"))
    return false;
  const auto reopened_quad = inspected_clip(reopened.inspectTimelineClipById(clip_id))
                                 .value(QStringLiteral("framePerspectiveQuad")).toMap();
  if (!check(reopened_quad == projected,
             "save and reopen should preserve exact enabled state and corner order"))
    return false;

  controller.undo();
  if (!check(!inspected_clip(controller.inspectTimelineClipById(clip_id))
                  .value(QStringLiteral("framePerspectiveQuad")).toMap()
                  .value(QStringLiteral("enabled")).toBool(),
             "Undo should restore the legacy unprojected frame"))
    return false;
  controller.redo();
  if (!check(inspected_clip(controller.inspectTimelineClipById(clip_id))
                 .value(QStringLiteral("framePerspectiveQuad")).toMap() == projected,
             "Redo should restore the exact four-corner frame"))
    return false;
  const auto cleared = controller.setTimelineClipPerspectiveQuadForAutomation(
      clip_id, false, {});
  if (!check(cleared.value(QStringLiteral("ok")).toBool() &&
                 !inspected_clip(controller.inspectTimelineClipById(clip_id))
                      .value(QStringLiteral("framePerspectiveQuad")).toMap()
                      .value(QStringLiteral("enabled")).toBool(),
             "the typed clear operation should return the clip to legacy perspective behavior"))
    return false;
  controller.undo();
  if (!check(inspected_clip(controller.inspectTimelineClipById(clip_id))
                 .value(QStringLiteral("framePerspectiveQuad")).toMap() == projected,
             "Undo should restore a cleared perspective quad"))
    return false;

  const auto original_variant = controller.activeOutputVariant();
  QString alternate_variant;
  for (const auto& variant_value : controller.outputVariants()) {
    const auto key = variant_value.toMap().value(QStringLiteral("key")).toString();
    if (!key.isEmpty() && key != original_variant) {
      alternate_variant = key;
      break;
    }
  }
  FrameQuad alternate_quad;
  if (!check(frame_quad_from_variant_map(projected, &alternate_quad,
                                         &animation_error),
             "the format-switch test should decode the saved base quadrilateral")) {
    std::cerr << animation_error.toStdString() << '\n';
    return false;
  }
  const auto variant_animation_a = QVariantList{
      QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                  {QStringLiteral("points"), points_for_quad(identity_quad)},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
      QVariantMap{{QStringLiteral("localSeconds"), 1.0},
                  {QStringLiteral("points"), points_for_quad(alternate_quad)},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}}};
  const auto variant_animation_b = QVariantList{
      QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                  {QStringLiteral("points"), points_for_quad(alternate_quad)},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
      QVariantMap{{QStringLiteral("localSeconds"), 1.0},
                  {QStringLiteral("points"), points_for_quad(identity_quad)},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}}};
  const auto variant_a_set =
      controller.setTimelineClipPerspectiveQuadAnimationForAutomation(
          clip_id, variant_animation_a);
  if (!check(!alternate_variant.isEmpty() &&
                 variant_a_set.value(QStringLiteral("ok")).toBool(),
             "the quad animation fixture should find a second output variant")) {
    return false;
  }
  controller.applyOutputPreset(alternate_variant);
  const auto variant_b_set =
      controller.setTimelineClipPerspectiveQuadAnimationForAutomation(
          clip_id, variant_animation_b);
  if (!check(variant_b_set.value(QStringLiteral("ok")).toBool(),
             "a second output variant should accept its own perspective trajectory")) {
    return false;
  }
  controller.applyOutputPreset(original_variant);
  const auto restored_variant_clip =
      inspected_clip(controller.inspectTimelineClipById(clip_id));
  auto original_variant_start_found = false;
  for (const auto& keyframe_value :
       restored_variant_clip.value(QStringLiteral("keyframes")).toList()) {
    const auto keyframe = keyframe_value.toMap();
    if (keyframe.value(QStringLiteral("parameter")).toString() ==
            QStringLiteral("framePerspectiveQuadTLX") &&
        qAbs(keyframe.value(QStringLiteral("seconds")).toDouble()) <= 1.0e-9 &&
        qAbs(keyframe.value(QStringLiteral("value")).toDouble()) <= 1.0e-9) {
      original_variant_start_found = true;
    }
  }
  if (!check(restored_variant_clip.value(QStringLiteral("keyframes")).toList().size() == 16 &&
                 original_variant_start_found,
             "switching formats should restore one exact quad trajectory without duplicate stale channels")) {
    return false;
  }

  const QSize render_size(830, 467);
  QImage source(render_size, QImage::Format_RGBA8888);
  source.fill(Qt::white);
  for (int y = 120; y < 347; ++y) {
    for (int x = 96; x < 128; ++x) source.setPixelColor(x, y, QColor(255, 0, 0));
    for (int x = 702; x < 734; ++x) source.setPixelColor(x, y, QColor(0, 0, 255));
  }
  const auto source_path = QDir(workspace_path).filePath(QStringLiteral("quad-css-830x467.png"));
  if (!check(source.save(source_path), "the CSS perspective reference source should save"))
    return false;

  TimelineDocumentController render_controller;
  render_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("CSS perspective and shadow pixel reference")},
      {QStringLiteral("width"), render_size.width()},
      {QStringLiteral("height"), render_size.height()},
      {QStringLiteral("frameRate"), 30.0},
      {QStringLiteral("trackLayout"), QStringLiteral("creator_social")},
  });
  render_controller.addClipFromGallery(QStringLiteral("photo:quad-css-reference"), source_path,
                                       QStringLiteral("Perspective reference card"), 1.0,
                                       source_path);
  const auto render_clip = render_controller.clips().value(0).toMap();
  const auto render_clip_id = render_clip.value(QStringLiteral("clipId")).toString();
  const QVariantMap shadow{
      {QStringLiteral("enabled"), true},
      {QStringLiteral("color"), QStringLiteral("#b0000000")},
      {QStringLiteral("offsetX"), 12.0},
      {QStringLiteral("offsetY"), 16.0},
      {QStringLiteral("blurRadius"), 16.0},
      {QStringLiteral("spread"), 0.0},
  };
  const auto frame_result = render_controller.setTimelineClipFrameForAutomation(
      render_clip_id, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0,
      QStringLiteral("stretch"), 1.0, 1.0, 0.0,
      0.0, 0.0, 0.0, 0.0, shadow);
  photo_editor::desktop::timeline_motion::frame_warp::FrameQuad css_quad;
  QString render_error;
  if (!check(!render_clip_id.isEmpty() && frame_result.value(QStringLiteral("ok")).toBool() &&
                 photo_editor::desktop::timeline_motion::frame_warp::css_perspective_rotate_y_quad(
                     render_size, 1500.0, -7.0, &css_quad, &render_error) &&
                 render_controller.setTimelineClipPerspectiveQuadForAutomation(
                     render_clip_id, true,
                     photo_editor::desktop::timeline_motion::frame_warp::
                         frame_quad_to_variant_map(css_quad).value(QStringLiteral("points")).toList())
                     .value(QStringLiteral("ok")).toBool(),
             "the native renderer fixture should accept the exact CSS perspective and shadow")) {
    std::cerr << render_error.toStdString() << '\n';
    return false;
  }

  using photo_editor::desktop::timeline_motion::frame_warp::FrameQuadOverscanPlan;
  FrameQuadOverscanPlan render_plan;
  if (!check(photo_editor::desktop::timeline_motion::frame_warp::make_frame_quad_overscan_plan(
                 render_size, css_quad, &render_plan, &render_error),
             "the CSS reference should produce a bounded overscan plan")) {
    std::cerr << render_error.toStdString() << '\n';
    return false;
  }
  QImage padded_source(render_plan.output_size, QImage::Format_RGBA8888);
  padded_source.fill(Qt::transparent);
  QPainter painter(&padded_source);
  painter.drawImage(render_plan.original_origin, source);
  painter.end();
  const auto native_warp =
      photo_editor::desktop::timeline_motion::frame_warp::render_frame_quad_warp(
          padded_source, render_plan.output_size,
          render_plan.expanded_canvas_destination_quad, &render_error);
  const auto native_warp_path =
      QDir(workspace_path).filePath(QStringLiteral("quad-css-native-reference.png"));
  if (!check(!native_warp.isNull() && native_warp.save(native_warp_path),
             "the native homography should produce a transparent reference frame")) {
    std::cerr << render_error.toStdString() << '\n';
    return false;
  }

  photo_editor::desktop::timeline_frame_shadow::Parameters shadow_parameters;
  if (!check(photo_editor::desktop::timeline_frame_shadow::parse_parameters(
                 shadow, &shadow_parameters, &render_error),
             "the reference should use the same validated shadow parameters"))
    return false;
  QSize shadowed_geometry_size;
  const auto shadow_filter =
      photo_editor::desktop::timeline_frame_shadow::ffmpeg_filter(
          shadow_parameters, render_plan.output_size, render_size, render_size,
          QStringLiteral("css_quad_reference"), QStringLiteral("css_quad_shadow"),
          &shadowed_geometry_size, &render_error);
  if (!check(!shadow_filter.isEmpty(), "the CSS reference shadow graph should build")) {
    std::cerr << render_error.toStdString() << '\n';
    return false;
  }
  const auto horizontal_margin = qMax(2, render_size.width() * 2);
  const auto vertical_margin = qMax(2, render_size.height() * 2);
  const auto final_geometry = QStringLiteral(
      "scale=%1:%2:eval=frame,pad='iw+%3':'ih+%4':%5:%6:color=black@0,"
      "crop=%7:%8:'(iw-%7)/2':'(ih-%8)/2',setsar=1,format=rgba[out]")
      .arg(shadowed_geometry_size.width())
      .arg(shadowed_geometry_size.height())
      .arg(horizontal_margin)
      .arg(vertical_margin)
      .arg(horizontal_margin / 2)
      .arg(vertical_margin / 2)
      .arg(render_size.width())
      .arg(render_size.height());
  const auto ffmpeg_path = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto actual_pattern =
      QDir(workspace_path).filePath(QStringLiteral("quad-css-controller-%06d.png"));
  if (!check(!ffmpeg_path.isEmpty() &&
                 render_timeline_image_sequence(render_controller, actual_pattern,
                                                &render_error, 0.0, 2.0 / 30.0),
             "the production Timeline exporter should render the CSS quad with its shadow")) {
    std::cerr << render_error.toStdString() << '\n';
    return false;
  }
  const auto reference_path =
      QDir(workspace_path).filePath(QStringLiteral("quad-css-ffmpeg-reference.png"));
  const auto reference_graph = QStringLiteral("[0:v]format=rgba,") + shadow_filter +
                               QStringLiteral("[css_quad_shadow]") + final_geometry;
  QProcess reference_renderer;
  reference_renderer.start(ffmpeg_path,
      {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
       QStringLiteral("-y"), QStringLiteral("-i"), native_warp_path,
       QStringLiteral("-filter_complex"), reference_graph,
       QStringLiteral("-map"), QStringLiteral("[out]"),
       QStringLiteral("-frames:v"), QStringLiteral("1"),
       QStringLiteral("-vcodec"), QStringLiteral("png"), reference_path});
  if (!check(reference_renderer.waitForFinished(90000) &&
                 reference_renderer.exitStatus() == QProcess::NormalExit &&
                 reference_renderer.exitCode() == 0,
             "FFmpeg should render the native homography reference with the same shadow")) {
    std::cerr << reference_renderer.readAllStandardError().constData() << '\n';
    return false;
  }
  const QImage controller_frame(timeline_image_sequence_frame(actual_pattern, 1));
  const QImage reference_frame(reference_path);
  if (!check(!controller_frame.isNull() && !reference_frame.isNull() &&
                 controller_frame.size() == render_size && reference_frame.size() == render_size,
             "the controller and reference exports should have the exact 830×467 frame size"))
    return false;

  struct PixelStats final {
    double coverage = 0.0;
    double red_weight = 0.0;
    double blue_weight = 0.0;
    double center_x = 0.0;
    double center_y = 0.0;
    double red_x = 0.0;
    double red_y = 0.0;
    double blue_x = 0.0;
    double blue_y = 0.0;
  };
  const auto pixel_stats = [](const QImage& image) {
    PixelStats stats;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const auto pixel = image.pixelColor(x, y);
        const auto alpha = static_cast<double>(pixel.alpha()) / 255.0;
        const auto foreground = alpha *
            static_cast<double>(qMax(pixel.red(), qMax(pixel.green(), pixel.blue()))) / 255.0;
        stats.coverage += foreground;
        stats.center_x += (static_cast<double>(x) + 0.5) * foreground;
        stats.center_y += (static_cast<double>(y) + 0.5) * foreground;
        const auto red_signal = alpha *
            static_cast<double>(qMax(0, pixel.red() - pixel.blue())) / 255.0;
        const auto blue_signal = alpha *
            static_cast<double>(qMax(0, pixel.blue() - pixel.red())) / 255.0;
        stats.red_weight += red_signal;
        stats.blue_weight += blue_signal;
        stats.red_x += (static_cast<double>(x) + 0.5) * red_signal;
        stats.red_y += (static_cast<double>(y) + 0.5) * red_signal;
        stats.blue_x += (static_cast<double>(x) + 0.5) * blue_signal;
        stats.blue_y += (static_cast<double>(y) + 0.5) * blue_signal;
      }
    }
    if (stats.coverage > 0.0) {
      stats.center_x /= stats.coverage;
      stats.center_y /= stats.coverage;
    }
    if (stats.red_weight > 0.0) {
      stats.red_x /= stats.red_weight;
      stats.red_y /= stats.red_weight;
    }
    if (stats.blue_weight > 0.0) {
      stats.blue_x /= stats.blue_weight;
      stats.blue_y /= stats.blue_weight;
    }
    return stats;
  };
  const auto actual_stats = pixel_stats(controller_frame);
  const auto reference_stats = pixel_stats(reference_frame);
  if (actual_stats.coverage < 1000.0 || reference_stats.coverage < 1000.0 ||
      actual_stats.red_weight < 100.0 || reference_stats.red_weight < 100.0 ||
      actual_stats.blue_weight < 100.0 || reference_stats.blue_weight < 100.0 ||
      std::abs(actual_stats.coverage - reference_stats.coverage) >
          qMax(1500.0, reference_stats.coverage * 0.01) ||
      std::abs(actual_stats.center_x - reference_stats.center_x) > 0.5 ||
      std::abs(actual_stats.center_y - reference_stats.center_y) > 0.5 ||
      std::abs(actual_stats.red_x - reference_stats.red_x) > 0.75 ||
      std::abs(actual_stats.red_y - reference_stats.red_y) > 0.75 ||
      std::abs(actual_stats.blue_x - reference_stats.blue_x) > 0.75 ||
      std::abs(actual_stats.blue_y - reference_stats.blue_y) > 0.75) {
    std::cerr << "CSS quad FFmpeg/native reference mismatch; actual coverage/center/red/blue="
              << actual_stats.coverage << '/' << actual_stats.center_x << ','
              << actual_stats.center_y << '/' << actual_stats.red_x << ','
              << actual_stats.red_y << '/' << actual_stats.blue_x << ','
              << actual_stats.blue_y << " reference=" << reference_stats.coverage << '/'
              << reference_stats.center_x << ',' << reference_stats.center_y << '/'
              << reference_stats.red_x << ',' << reference_stats.red_y << '/'
              << reference_stats.blue_x << ',' << reference_stats.blue_y << '\n';
    return false;
  }

  const auto fps = 30.0;
  const auto frame_duration = 1.0 / fps;
  FrameQuadAnimation controller_animation;
  for (std::size_t coordinate = 0; coordinate < controller_animation.size();
       ++coordinate) {
    const auto point_index = coordinate / 2U;
    const auto start_value = (coordinate % 2U) == 0U
                                 ? css_quad.points.at(point_index).x()
                                 : css_quad.points.at(point_index).y();
    const auto end_value = (coordinate % 2U) == 0U
                               ? identity_quad.points.at(point_index).x()
                               : identity_quad.points.at(point_index).y();
    controller_animation.at(coordinate) = {
        FrameQuadScalarKeyframe{0.0, start_value, false},
        FrameQuadScalarKeyframe{frame_duration, end_value, false}};
  }
  FrameQuadAnimationOverscanPlan controller_animation_plan;
  if (!check(make_frame_quad_animation_overscan_plan(
                 render_size, css_quad, controller_animation, 1.0,
                 &controller_animation_plan, &render_error) &&
                 controller_animation_plan.output_size == render_plan.output_size &&
                 controller_animation_plan.padding == render_plan.padding,
             "the animated CSS trajectory should use the same overscan as its endpoint poses")) {
    std::cerr << render_error.toStdString() << '\n';
    return false;
  }
  const QVariantList animation_keyframes{
      QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                  {QStringLiteral("points"), points_for_quad(css_quad)},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
      QVariantMap{{QStringLiteral("localSeconds"), frame_duration},
                  {QStringLiteral("points"), points_for_quad(identity_quad)},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}}};
  const auto animation_update =
      render_controller.setTimelineClipPerspectiveQuadAnimationForAutomation(
          render_clip_id, animation_keyframes);
  const auto animated_clip_after_update =
      render_controller.inspectTimelineClipById(render_clip_id)
          .value(QStringLiteral("clip")).toMap();
  if (!check(animation_update.value(QStringLiteral("ok")).toBool() &&
                 animation_update.value(QStringLiteral("changed")).toBool() &&
                 animation_update.value(QStringLiteral("channelCount")).toInt() == 8 &&
                 animated_clip_after_update.value(QStringLiteral("keyframes")).toList().size() == 16,
             "the native typed animation should write eight ordered scalar channels")) {
    return false;
  }
  const auto before_invalid_trajectory =
      render_controller.inspectTimelineClipById(render_clip_id);
  QVariantList invalid_animation{
      QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                  {QStringLiteral("points"), points_for_quad(identity_quad)},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
      QVariantMap{{QStringLiteral("localSeconds"), 1.0},
                  {QStringLiteral("points"), points_for_quad(folded_endpoint)},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}}};
  const auto invalid_animation_result =
      render_controller.setTimelineClipPerspectiveQuadAnimationForAutomation(
          render_clip_id, invalid_animation);
  if (!check(!invalid_animation_result.value(QStringLiteral("ok")).toBool() &&
                 render_controller.inspectTimelineClipById(render_clip_id) ==
                     before_invalid_trajectory,
             "a folded interpolated native trajectory must reject without changing the clip")) {
    return false;
  }
  render_controller.undo();
  if (!check(inspected_clip(render_controller.inspectTimelineClipById(render_clip_id))
                 .value(QStringLiteral("keyframes")).toList().isEmpty(),
             "Undo should clear only the just-authored perspective animation channels")) {
    return false;
  }
  render_controller.redo();
  if (!check(inspected_clip(render_controller.inspectTimelineClipById(render_clip_id))
                 .value(QStringLiteral("keyframes")).toList().size() == 16 &&
                 render_controller.saveTimeline(false),
             "Redo and native save should restore the exact animated perspective channels")) {
    return false;
  }
  TimelineDocumentController animated_reopened;
  if (!check(animated_reopened.loadTimeline(render_controller.savedDocumentPath()) &&
                 inspected_clip(animated_reopened.inspectTimelineClipById(render_clip_id))
                     .value(QStringLiteral("keyframes")).toList().size() == 16,
             "save and reopen should preserve all eight animated corner channels")) {
    return false;
  }
  const auto animated_pattern = QDir(workspace_path).filePath(
      QStringLiteral("quad-css-animated-controller-%06d.png"));
  if (!check(render_timeline_image_sequence(
                 render_controller, animated_pattern, &render_error, 0.0,
                 2.0 * frame_duration),
             "the production exporter should render two animated perspective frames")) {
    std::cerr << render_error.toStdString() << '\n';
    return false;
  }
  const auto animated_frame_reference = [&](double local_seconds,
                                            const QString& suffix) {
    const auto pose = evaluate_frame_quad_animation(
        css_quad, controller_animation, local_seconds);
    FrameQuad expanded_pose;
    FrameQuad content_pose;
    if (!conjugate_frame_quad_for_overscan(
            render_size, controller_animation_plan.padding, pose,
            &expanded_pose, &content_pose, &render_error)) {
      return QImage{};
    }
    const auto native_pose = render_frame_quad_warp(
        padded_source, controller_animation_plan.output_size,
        expanded_pose, &render_error);
    const auto native_pose_path = QDir(workspace_path).filePath(
        QStringLiteral("quad-css-animated-native-%1.png").arg(suffix));
    const auto reference_pose_path = QDir(workspace_path).filePath(
        QStringLiteral("quad-css-animated-reference-%1.png").arg(suffix));
    if (native_pose.isNull() || !native_pose.save(native_pose_path)) {
      return QImage{};
    }
    QProcess renderer;
    renderer.start(ffmpeg_path,
        {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
         QStringLiteral("error"), QStringLiteral("-y"),
         QStringLiteral("-i"), native_pose_path,
         QStringLiteral("-filter_complex"), reference_graph,
         QStringLiteral("-map"), QStringLiteral("[out]"),
         QStringLiteral("-frames:v"), QStringLiteral("1"),
         QStringLiteral("-vcodec"), QStringLiteral("png"), reference_pose_path});
    if (!renderer.waitForFinished(90000) ||
        renderer.exitStatus() != QProcess::NormalExit || renderer.exitCode() != 0) {
      render_error = QString::fromUtf8(renderer.readAllStandardError()).trimmed();
      return QImage{};
    }
    return QImage(reference_pose_path);
  };
  for (int frame = 0; frame < 2; ++frame) {
    const auto local_seconds = static_cast<double>(frame) * frame_duration;
    const auto controller_image = QImage(timeline_image_sequence_frame(
        animated_pattern, frame + 1));
    const auto expected_image = animated_frame_reference(
        local_seconds, QString::number(frame));
    if (!check(!controller_image.isNull() && !expected_image.isNull() &&
                   controller_image.size() == render_size &&
                   expected_image.size() == render_size,
               "each animated FFmpeg frame should have a native homography plus shadow oracle")) {
      std::cerr << render_error.toStdString() << '\n';
      return false;
    }
    const auto actual = pixel_stats(controller_image);
    const auto expected = pixel_stats(expected_image);
    if (frame == 0) {
      std::cerr << "quad frame-zero comparison static=" << actual_stats.center_x
                << ',' << actual_stats.center_y << " animated=" << actual.center_x
                << ',' << actual.center_y << " oracle=" << expected.center_x << ','
                << expected.center_y << " animated-minus-static="
                << actual.center_x - actual_stats.center_x << ','
                << actual.center_y - actual_stats.center_y << " static-minus-oracle="
                << actual_stats.center_x - reference_stats.center_x << ','
                << actual_stats.center_y - reference_stats.center_y
                << " coverage(static/animated/oracle)=" << actual_stats.coverage
                << '/' << actual.coverage << '/' << expected.coverage
                << " red(animated/static/oracle)=" << actual.red_x << ',' << actual.red_y
                << '/' << actual_stats.red_x << ',' << actual_stats.red_y << '/'
                << expected.red_x << ',' << expected.red_y
                << " blue(animated/static/oracle)=" << actual.blue_x << ',' << actual.blue_y
                << '/' << actual_stats.blue_x << ',' << actual_stats.blue_y << '/'
                << expected.blue_x << ',' << expected.blue_y << '\n';
    }
    if (!check(actual.coverage >= 1000.0 && expected.coverage >= 1000.0 &&
                   std::abs(actual.coverage - expected.coverage) <=
                       qMax(1500.0, expected.coverage * 0.01) &&
                   std::abs(actual.center_x - expected.center_x) <= 0.5 &&
                   std::abs(actual.center_y - expected.center_y) <= 0.5 &&
                   std::abs(actual.red_x - expected.red_x) <= 0.75 &&
                   std::abs(actual.red_y - expected.red_y) <= 0.75 &&
                   std::abs(actual.blue_x - expected.blue_x) <= 0.75 &&
                   std::abs(actual.blue_y - expected.blue_y) <= 0.75,
               "animated production pixels should match the evaluated native quad and same shadow")) {
      std::cerr << "animated quad frame " << frame << " differs; actual center="
                << actual.center_x << ',' << actual.center_y << " reference="
                << expected.center_x << ',' << expected.center_y
                << " render error=" << render_error.toStdString() << '\n';
      return false;
    }
  }
  return true;
}
