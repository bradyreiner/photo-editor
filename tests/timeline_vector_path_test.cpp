#include "timeline_vector_path.h"

#include <QGuiApplication>

#include <iostream>

using photo_editor::desktop::timeline_motion::render_vector_path_frame;
using photo_editor::desktop::timeline_motion::validate_vector_path_parameters;

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QVariantMap parameters{
      {"viewBoxWidth", 100.0}, {"viewBoxHeight", 100.0},
      {"fill", "#00000000"}, {"stroke", "#44ee99"}, {"strokeWidth", 4.0},
      {"commands", QVariantList{
          QVariantMap{{"verb", "M"}, {"points", QVariantList{10.0, 20.0}}},
          QVariantMap{{"verb", "L"}, {"points", QVariantList{90.0, 20.0}}}}},
      {"frames", QVariantList{
          QVariantMap{{"seconds", 0.0}, {"coordinates", QVariantList{10.0, 20.0, 90.0, 20.0}}},
          QVariantMap{{"seconds", 1.0}, {"coordinates", QVariantList{10.0, 80.0, 90.0, 80.0}}}}}};
  if (!validate_vector_path_parameters(parameters).isEmpty()) return 1;
  const auto middle = render_vector_path_frame({200, 200}, parameters, 0.5);
  if (middle.isNull() || middle.pixelColor(100, 100) != QColor("#44ee99") ||
      middle.pixelColor(100, 40).alpha() != 0 ||
      middle.pixelColor(4, 100).alpha() != 0) return 2;
  const auto held = render_vector_path_frame({100, 100}, parameters, 2.0);
  if (held.pixelColor(50, 80).alpha() != 255 || held.pixelColor(50, 20).alpha() != 0) return 3;
  auto invalid = parameters;
  invalid["frames"] = QVariantList{QVariantMap{
      {"seconds", 0.0}, {"coordinates", QVariantList{10.0, 20.0, 90.0}}}};
  if (validate_vector_path_parameters(invalid).isEmpty() ||
      !render_vector_path_frame({100, 100}, invalid, 0.0).isNull()) return 4;
  parameters.remove("frames");
  parameters["commands"] = QVariantList{
      QVariantMap{{"verb", "M"}, {"points", QVariantList{10.0, 50.0}}},
      QVariantMap{{"verb", "C"}, {"points", QVariantList{30.0, 10.0, 70.0, 90.0, 90.0, 50.0}}}};
  const auto curve = render_vector_path_frame({100, 100}, parameters, 0.0);
  if (curve.isNull() || curve.pixelColor(50, 50).alpha() < 240) return 5;
  std::cout << "Native vector interpolation, scaling, alpha and validation passed\n";
  return 0;
}
