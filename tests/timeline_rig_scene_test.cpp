#include "timeline_rig_scene.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <iostream>

using namespace photo_editor::desktop::timeline_motion;

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const auto parse = [](const QByteArray& text) { return QJsonDocument::fromJson(text).toVariant().toMap(); };
  auto scene = parse(R"({"schema":"looksy-rig-scene-v1","width":100,"height":100,"nodes":[
    {"id":"shoulder","x":[[0,10,"linear"],[1,50]],"y":20,"opacity":0.5},
    {"id":"hand","parent":"shoulder","kind":"rect","rect":[0,0,10,10],"fill":"#ff0000","x":10}
  ]})");
  QString error;
  const auto rig = prepare_rig_scene(scene, &error);
  if (!rig) { std::cerr << error.toStdString(); return 1; }
  const auto middle = render_rig_scene_frame({200,200}, *rig, 0.5);
  if (middle.pixelColor(85,45).red() < 250 ||
      middle.pixelColor(85,45).alpha() < 125 || middle.pixelColor(85,45).alpha() > 130 ||
      middle.pixelColor(45,45).alpha() != 0) return 2;
  const auto again = render_rig_scene_frame({200,200}, *rig, 0.5);
  if (middle != again) return 3;
  const auto reopened = prepare_rig_scene(QJsonDocument::fromVariant(scene).toVariant().toMap());
  if (!reopened || render_rig_scene_frame({200,200}, *reopened, 0.5) != middle) return 4;
  auto bad = scene;
  auto nodes = bad["nodes"].toList();
  auto first = nodes[0].toMap();
  first["parent"] = "hand";
  nodes[0] = first;
  bad["nodes"] = nodes;
  if (prepare_rig_scene(bad)) return 5;
  first.remove("parent");
  first["x"] = QVariantList{QVariantList{0.0, 1.0}, QVariantList{0.0, 2.0}};
  nodes[0] = first;
  bad["nodes"] = nodes;
  if (prepare_rig_scene(bad)) return 6;
  first["x"] = "not a number";
  nodes[0] = first;
  bad["nodes"] = nodes;
  if (prepare_rig_scene(bad)) return 7;
  auto rotated = parse(R"({"schema":"looksy-rig-scene-v1","width":100,"height":100,"nodes":[
    {"id":"root","x":50,"y":50,"rotation":90},
    {"id":"child","parent":"root","kind":"rect","rect":[10,0,10,10],"fill":"#00ff00"}
  ]})");
  const auto rotated_rig = prepare_rig_scene(rotated);
  if (!rotated_rig) return 8;
  const auto turned = render_rig_scene_frame({100,100}, *rotated_rig, 0);
  if (turned.pixelColor(45,65).green() != 255 || turned.pixelColor(65,55).alpha() != 0) return 9;
  if (!render_rig_scene_frame({0,0}, *rig, 0).isNull()) return 10;
  std::cout << "Rig interpolation, parent transforms, inherited opacity, deterministic reopen, bounds and cycles passed\n";
}
