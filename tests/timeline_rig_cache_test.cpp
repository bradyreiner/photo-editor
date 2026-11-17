#include "timeline_native_generator_frame_cache.h"
#include "timeline_vector_path.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QDir>
#include <QImage>
#include <iostream>

using namespace photo_editor::desktop::timeline_motion;
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const auto scene = QJsonDocument::fromJson(R"({"schema":"looksy-rig-scene-v1","width":100,"height":100,"nodes":[
    {"id":"body","x":[[0,0,"linear"],[1,50]],"rotation":[[0,0],[1,30]]},
    {"id":"hand","parent":"body","kind":"ellipse","rect":[20,20,20,20],"fill":"#ff8800"}
  ]})").toVariant().toMap();
  const QVariantMap parameters{{"scene", scene}};
  QTemporaryDir directory;
  NativeGeneratorFrameSequenceRequest request;
  request.generator_kind = "vector-path";
  request.cache_root = directory.path();
  request.frame_size = {100,100};
  request.frames_per_second = 3;
  request.clip_duration_seconds = 1;
  request.vector_path_parameters = parameters;
  request.samples = {{0.0,0.0,0.0,1.0},{1.0/3.0,1.0/3.0,0.0,1.0},{2.0/3.0,2.0/3.0,0.0,1.0}};
  const auto cached = ensure_native_generator_frame_sequence(request);
  if (!cached.ok) { std::cerr << cached.error.toStdString(); return 1; }
  for (int i = 0; i < 3; ++i) {
    const QImage exported(QDir(cached.frame_directory).filePath(QString("frame_%1.png").arg(i,6,10,QChar('0'))));
    const auto preview = render_vector_path_frame({100,100}, parameters, static_cast<double>(i)/3.0);
    if (exported.convertToFormat(QImage::Format_RGBA8888) != preview.convertToFormat(QImage::Format_RGBA8888)) return 2;
  }
  const auto old_key = native_generator_frame_sequence_cache_key(request);
  auto altered = scene;
  auto nodes = altered["nodes"].toList();
  auto hand = nodes[1].toMap(); hand["fill"] = "#0088ff"; nodes[1] = hand; altered["nodes"] = nodes;
  request.vector_path_parameters = {{"scene", altered}};
  if (native_generator_frame_sequence_cache_key(request) == old_key) return 3;
  const auto second = ensure_native_generator_frame_sequence(request);
  if (!second.ok || second.frame_directory == cached.frame_directory) return 4;
  std::cout << "Native rig preview/export pixel parity and cache invalidation passed\n";
}
