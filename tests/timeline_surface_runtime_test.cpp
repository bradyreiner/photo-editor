#include <cstdlib>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>

#include "timeline_surface_runtime.h"

using namespace photo_editor::desktop;

namespace {
int fail(const QString& text) { std::cerr << text.toStdString() << '\n'; return EXIT_FAILURE; }

bool save_frame(const QString& folder, int index, const QImage& image) {
  return image.save(QDir(folder).filePath(QStringLiteral("%1.png").arg(index, 6, 10, QLatin1Char('0'))), "PNG");
}

bool textured_frames(const QString& folder, int count) {
  QImage source(160, 120, QImage::Format_RGB32);
  source.fill(Qt::white);
  for (int y = 22; y < 96; y += 8) for (int x = 26; x < 132; x += 8) {
    const int code = (x * 31 + y * 73 + x * y * 3) % 190 + 30;
    for (int py = y; py < y + 5; ++py) for (int px = x; px < x + 5; ++px)
      source.setPixelColor(px, py, QColor(code, (code * 7 + 19) % 256, (code * 13 + 37) % 256));
  }
  for (int i = 0; i < count; ++i) {
    QImage frame(160, 120, QImage::Format_RGB32);
    frame.fill(QColor(35, 50 + i * 12, 80));
    QPainter painter(&frame);
    QPolygonF target;
    const qreal dx = i * 2.0;
    target << QPointF(24 + dx, 20) << QPointF(136 + dx * .6, 24)
           << QPointF(20 + dx, 100) << QPointF(132 + dx * .6, 96);
    QPolygonF source_quad;
    source_quad << QPointF(24,20) << QPointF(136,20) << QPointF(24,100) << QPointF(136,100);
    QTransform transform;
    QPolygonF ordered_source; ordered_source << source_quad[0] << source_quad[1] << source_quad[3] << source_quad[2];
    QPolygonF ordered_target; ordered_target << target[0] << target[1] << target[3] << target[2];
    transform.quadToQuad(ordered_source, ordered_target, transform);
    painter.setTransform(transform);
    painter.drawImage(0, 0, source);
    // Moving foreground outliers inside the selected patch must not dominate its model.
    painter.resetTransform();
    painter.setBrush(QColor(0, 240, 20));
    painter.drawEllipse(QPoint(34 + i * 13, 60), 7, 7);
    painter.end();
    if (!save_frame(folder, i, frame)) return false;
  }
  return true;
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temp;
  if (!temp.isValid()) return fail(QStringLiteral("temporary directory unavailable"));
  const QString python = QStringLiteral(PHOTO_EDITOR_TEST_PYTHON);
  const QString worker = QStringLiteral(PHOTO_EDITOR_SURFACE_WORKER);
  QProcess probe;
  probe.start(python, {QStringLiteral("-c"), QStringLiteral("import cv2, numpy")});
  if (!probe.waitForFinished(10000) || probe.exitCode() != 0) {
    if (probe.state() != QProcess::NotRunning) { probe.kill(); probe.waitForFinished(1000); }
    return EXIT_SUCCESS;  // Optional runtime absent.
  }
  qputenv("PHOTO_EDITOR_SURFACE_PYTHON", python.toUtf8());
  qputenv("PHOTO_EDITOR_SURFACE_WORKER", worker.toUtf8());

  const QString frames = QDir(temp.path()).filePath(QStringLiteral("perspective"));
  if (!QDir().mkpath(frames) || !textured_frames(frames, 5)) return fail(QStringLiteral("could not generate perspective frames"));
  QVariantList times{0.0, .04, .08, .12, .16};
  QVariantMap seed{{QStringLiteral("x"), .15}, {QStringLiteral("y"), 1.0/6.0},
                   {QStringLiteral("width"), .70}, {QStringLiteral("height"), 2.0/3.0}};
  int updates = 0;
  const QVariantMap tracked = trackTimelineSurface(frames, times, seed, nullptr,
      [&updates](int done, int total) { if (done > 0 && total == 5) ++updates; });
  if (!tracked.value(QStringLiteral("success")).toBool() ||
      tracked.value(QStringLiteral("samples")).toList().size() != 5 || updates != 5)
    return fail(QStringLiteral("synthetic perspective tracking failed: %1").arg(tracked.value(QStringLiteral("message")).toString()));
  const auto samples = tracked.value(QStringLiteral("samples")).toList();
  const auto last_quad = samples.last().toMap().value(QStringLiteral("quad")).toList();
  const double expected_last_x[] = {32.0/160.0, 140.8/160.0, 28.0/160.0, 138.8/160.0};
  const double expected_last_y[] = {20.0/120.0, 24.0/120.0, 100.0/120.0, 96.0/120.0};
  for (int corner = 0; corner < 4; ++corner) {
    const auto point = last_quad.at(corner).toMap();
    const double px = point.value(QStringLiteral("x")).toDouble();
    const double py = point.value(QStringLiteral("y")).toDouble();
    if (std::abs(px-expected_last_x[corner])*160.0 > 6.0 || std::abs(py-expected_last_y[corner])*120.0 > 6.0)
      return fail(QStringLiteral("tracked corner exceeded synthetic ground-truth error bound"));
  }

  QVariantMap excluded_seed = seed;
  excluded_seed.insert(QStringLiteral("exclusions"), QVariantList{QVariantMap{
      {QStringLiteral("x"), .2}, {QStringLiteral("y"), .3},
      {QStringLiteral("width"), .12}, {QStringLiteral("height"), .12}}});
  if (!trackTimelineSurface(frames, times, excluded_seed).value(QStringLiteral("success")).toBool())
    return fail(QStringLiteral("tracking with a feature exclusion failed"));

  QVariantMap corrected_seed = seed;
  const QVariantList corrected_quad{
      QVariantMap{{QStringLiteral("x"),32.0/160.0},{QStringLiteral("y"),20.0/120.0}},
      QVariantMap{{QStringLiteral("x"),140.8/160.0},{QStringLiteral("y"),24.0/120.0}},
      QVariantMap{{QStringLiteral("x"),28.0/160.0},{QStringLiteral("y"),100.0/120.0}},
      QVariantMap{{QStringLiteral("x"),136.8/160.0},{QStringLiteral("y"),96.0/120.0}}};
  QVariantMap correction{{QStringLiteral("frameIndex"),4},{QStringLiteral("quad"),corrected_quad}};
  corrected_seed.insert(QStringLiteral("correctionFrames"), QVariantList{correction});
  const auto repaired = trackTimelineSurface(frames, times, corrected_seed);
  const auto repaired_samples = repaired.value(QStringLiteral("samples")).toList();
  if (!repaired.value(QStringLiteral("success")).toBool() || repaired_samples.size() != 5)
    return fail(QStringLiteral("valid later correction did not recover a complete measured track: %1")
                    .arg(repaired.value(QStringLiteral("message")).toString()));
  for (int index = 0; index < 5; ++index) {
    // The final frame is an exact user correction. Earlier samples must be
    // recovered from visible image measurements on both sides of that anchor.
    const auto quad = repaired_samples.at(index).toMap().value(QStringLiteral("quad")).toList();
    const double dx = index * 2.0;
    const double expected_x[] = {24.0 + dx, 136.0 + dx * .6, 20.0 + dx, 132.0 + dx * .6};
    const double expected_y[] = {20.0, 24.0, 100.0, 96.0};
    for (int corner = 0; corner < 4; ++corner) {
      const auto point = quad.at(corner).toMap();
      if (std::abs(point.value(QStringLiteral("x")).toDouble() * 160.0 - expected_x[corner]) > 6.0 ||
          std::abs(point.value(QStringLiteral("y")).toDouble() * 120.0 - expected_y[corner]) > 6.0)
        return fail(QStringLiteral("bidirectional correction recovery exceeded measured-corner bounds at frame %1")
                        .arg(index));
    }
  }

  const QString disagreeing_frames = QDir(temp.path()).filePath(QStringLiteral("disagreeing"));
  QDir().mkpath(disagreeing_frames);
  if (!textured_frames(disagreeing_frames, 5))
    return fail(QStringLiteral("could not generate disagreeing-anchor frames"));
  QVariantMap disagreeing_seed = seed;
  const QVariantList shifted_quad{
      QVariantMap{{QStringLiteral("x"),47.0/160.0},{QStringLiteral("y"),20.0/120.0}},
      QVariantMap{{QStringLiteral("x"),155.8/160.0},{QStringLiteral("y"),24.0/120.0}},
      QVariantMap{{QStringLiteral("x"),43.0/160.0},{QStringLiteral("y"),100.0/120.0}},
      QVariantMap{{QStringLiteral("x"),151.8/160.0},{QStringLiteral("y"),96.0/120.0}}};
  disagreeing_seed.insert(QStringLiteral("correctionFrames"), QVariantList{QVariantMap{
      {QStringLiteral("frameIndex"),4},{QStringLiteral("quad"),shifted_quad}}});
  const auto disagreement = trackTimelineSurface(disagreeing_frames, times, disagreeing_seed);
  if (disagreement.value(QStringLiteral("success")).toBool() ||
      !disagreement.value(QStringLiteral("message")).toString().contains(QStringLiteral("disagree")))
    return fail(QStringLiteral("conflicting correction anchors did not fail closed: %1")
                    .arg(disagreement.value(QStringLiteral("message")).toString()));

  const QString lost_frames = QDir(temp.path()).filePath(QStringLiteral("lost"));
  QDir().mkpath(lost_frames);
  textured_frames(lost_frames, 5);
  QImage flat(160,120,QImage::Format_RGB32); flat.fill(Qt::gray); save_frame(lost_frames, 2, flat);
  QVariantMap occluded_seed = corrected_seed;
  const QVariantMap occlusion_correction{{QStringLiteral("frameIndex"),4},{QStringLiteral("quad"),corrected_quad}};
  occluded_seed.insert(QStringLiteral("correctionFrames"), QVariantList{occlusion_correction});
  const QVariantMap lost = trackTimelineSurface(lost_frames, times, occluded_seed);
  if (lost.value(QStringLiteral("success")).toBool() ||
      !lost.value(QStringLiteral("message")).toString().contains(QStringLiteral("frame 2")))
    return fail(QStringLiteral("unobservable full occlusion was interpolated or lost its frame diagnostic: %1")
                    .arg(lost.value(QStringLiteral("message")).toString()));

  const QString long_frames = QDir(temp.path()).filePath(QStringLiteral("long"));
  if (!QDir().mkpath(long_frames)) return fail(QStringLiteral("could not create long-frame fixture"));
  const QString first_frame = QDir(frames).filePath(QStringLiteral("000000.png"));
  QVariantList long_times;
  for (int index = 0; index < 900; ++index) {
    const QString path = QDir(long_frames).filePath(QStringLiteral("%1.png").arg(index, 6, 10, QLatin1Char('0')));
    if (!QFile::copy(first_frame, path)) return fail(QStringLiteral("could not stage long frame %1").arg(index));
    long_times.push_back(index / 30.0);
  }
  const QVariantMap long_result = trackTimelineSurface(long_frames, long_times, seed);
  if (!long_result.value(QStringLiteral("success")).toBool() ||
      long_result.value(QStringLiteral("samples")).toList().size() != 900)
    return fail(QStringLiteral("900-frame surface request did not complete: %1")
                    .arg(long_result.value(QStringLiteral("message")).toString()));

  const QString bad_worker = QDir(temp.path()).filePath(QStringLiteral("bad_worker.py"));
  QFile bad_file(bad_worker);
  if (!bad_file.open(QIODevice::WriteOnly) || bad_file.write("print('not-json', flush=True)\n") < 0)
    return fail(QStringLiteral("could not write protocol fixture"));
  bad_file.close();
  qputenv("PHOTO_EDITOR_SURFACE_WORKER", bad_worker.toUtf8());
  if (trackTimelineSurface(frames, times, seed).value(QStringLiteral("success")).toBool())
    return fail(QStringLiteral("invalid worker protocol was accepted"));

  const QString concave_worker = QDir(temp.path()).filePath(QStringLiteral("concave_worker.py"));
  QFile concave_file(concave_worker);
  const QByteArray concave_script =
      "import json,sys\nr=json.load(open(sys.argv[2]))\n"
      "q=[{'x':.1,'y':.1},{'x':.9,'y':.1},{'x':.3,'y':.2},{'x':.1,'y':.9}]\n"
      "print(json.dumps({'type':'progress','completed':5,'total':5}),flush=True)\n"
      "print(json.dumps({'type':'complete','width':160,'height':120,'backend':'fixture','samples':"
      "[{'sourceSeconds':t,'quad':q,'confidence':.5,'inliers':15,'errorPx':0.} for t in r['times']]}),flush=True)\n";
  if (!concave_file.open(QIODevice::WriteOnly) || concave_file.write(concave_script) != concave_script.size())
    return fail(QStringLiteral("could not write malformed-quad fixture"));
  concave_file.close();
  qputenv("PHOTO_EDITOR_SURFACE_WORKER", concave_worker.toUtf8());
  if (trackTimelineSurface(frames, times, seed).value(QStringLiteral("success")).toBool())
    return fail(QStringLiteral("concave worker quad was accepted"));

  const QString weak_worker = QDir(temp.path()).filePath(QStringLiteral("weak_worker.py"));
  QFile weak_file(weak_worker);
  const QByteArray weak_script =
      "import json,sys\nr=json.load(open(sys.argv[2]))\n"
      "q=[{'x':.1,'y':.1},{'x':.9,'y':.1},{'x':.1,'y':.9},{'x':.9,'y':.9}]\n"
      "print(json.dumps({'type':'complete','width':160,'height':120,'backend':'fixture','samples':"
      "[{'sourceSeconds':t,'quad':q,'confidence':.29,'inliers':15,'errorPx':0.} for t in r['times']]}),flush=True)\n";
  if (!weak_file.open(QIODevice::WriteOnly) || weak_file.write(weak_script) != weak_script.size())
    return fail(QStringLiteral("could not write weak-confidence fixture"));
  weak_file.close();
  qputenv("PHOTO_EDITOR_SURFACE_WORKER", weak_worker.toUtf8());
  if (trackTimelineSurface(frames, times, seed).value(QStringLiteral("success")).toBool())
    return fail(QStringLiteral("low-confidence worker result was accepted"));

  const QString wait_worker = QDir(temp.path()).filePath(QStringLiteral("wait_worker.py"));
  QFile wait_file(wait_worker);
  if (!wait_file.open(QIODevice::WriteOnly) || wait_file.write(
          "import json,time\nprint(json.dumps({'type':'progress','completed':1,'total':5}),flush=True)\ntime.sleep(5)\n") < 0)
    return fail(QStringLiteral("could not write cancellation fixture"));
  wait_file.close();
  qputenv("PHOTO_EDITOR_SURFACE_WORKER", wait_worker.toUtf8());
  std::atomic_bool cancelled{false};
  std::thread cancel_after_start([&cancelled] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); cancelled.store(true); });
  const auto cancelled_result = trackTimelineSurface(frames, times, seed, &cancelled);
  cancel_after_start.join();
  if (cancelled_result.value(QStringLiteral("success")).toBool() ||
      !cancelled_result.value(QStringLiteral("message")).toString().contains(QStringLiteral("cancelled")))
    return fail(QStringLiteral("active worker cancellation was not propagated"));
  return EXIT_SUCCESS;
}
