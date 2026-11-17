#include "photo_paint_coordinates.h"

#include <cmath>
#include <iostream>
#include <numbers>

namespace {
using namespace photo_editor::desktop;

QPointF placed(QPointF point,double x,double y,double width,double height,double scale,double degrees) {
  const auto angle=degrees*std::numbers::pi_v<double>/180.0;
  const QPointF delta((point.x()-width/2)*scale,(point.y()-height/2)*scale);
  return {x+width*scale/2+delta.x()*std::cos(angle)-delta.y()*std::sin(angle),
      y+height*scale/2+delta.x()*std::sin(angle)+delta.y()*std::cos(angle)};
}

bool near(QPointF a,QPointF b) {return std::hypot(a.x()-b.x(),a.y()-b.y())<1e-9;}
}

int main() {
  const DesktopSourceGeometry geometry{{320,180},{40,30,240,120}};
  const QVariantMap basis{{"x",0.0},{"y",0.0},{"width",1.0},{"height",1.0}};
  QVariantMap paint{{"id","paint"},{"kind","pixel"},{"parentId","inner"},
      {"pixelWidth",120},{"pixelHeight",60},{"canvasWidth",160},{"canvasHeight",90},
      {"x",11},{"y",-3},{"scale",1.3},{"rotation",17.0},
      {"coordinateSpace","source-area"},{"coordinateArea",basis}};
  const QVariantMap inner{{"id","inner"},{"kind","group"},{"parentId","outer"},
      {"canvasWidth",320},{"canvasHeight",180},{"x",13},{"y",-7},{"scale",0.85},{"rotation",-23.0}};
  const QVariantMap outer{{"id","outer"},{"kind","group"},
      {"canvasWidth",640},{"canvasHeight",360},{"x",-19},{"y",9},{"scale",1.15},{"rotation",31.0}};
  const QVariantList layers{paint,inner,outer};
  const auto transform=photo_paint_layer_to_document(paint,layers,geometry);
  if (!transform.valid) {std::cerr<<transform.error.toStdString()<<'\n'; return 1;}
  // Independent numeric execution fixture: D=(2x-40,2y-30), local raster
  // 240x120 at(-18,-36); parent offsets round to(10,-5), then(-7,3).
  for (const auto point:{QPointF(0,0),QPointF(37.25,20.5),QPointF(120,60),QPointF(-4,70)}) {
    auto expected=placed(point*2,-18,-36,240,120,1.3,17);
    expected=placed(expected,10,-5,240,120,0.85,-23);
    expected=placed(expected,-7,3,240,120,1.15,31);
    expected={(expected.x()+40)/2,(expected.y()+30)/2};
    const auto actual=transform.local_to_document.map(point);
    if (!near(actual,expected) || !near(transform.local_to_document.inverted().map(actual),point)) {
      std::cerr<<"Cropped paint must compose native group pivots and round-trip its immutable authoring basis\n";
      return 1;
    }
  }
  auto base=paint;
  base.insert("copyBaseImage",true); base.insert("pixelWidth",160); base.insert("pixelHeight",90);
  const auto base_transform=photo_paint_layer_to_document(base,{base,inner,outer},geometry);
  const QPointF source_point(40.25,31.5);
  auto base_expected=placed({2*source_point.x()-40,2*source_point.y()-30},17,-4,240,120,1.3,17);
  base_expected=placed(base_expected,10,-5,240,120,0.85,-23);
  base_expected=placed(base_expected,-7,3,240,120,1.15,31);
  base_expected={(base_expected.x()+40)/2,(base_expected.y()+30)/2};
  if (!base_transform.valid || !near(base_transform.local_to_document.map(source_point),base_expected)) {
    std::cerr<<"Base erase must invert the visible movable row once while retaining source coverage coordinates\n";
    return 1;
  }
  auto missing=paint; missing.insert("parentId","missing");
  if (photo_paint_layer_to_document(missing,layers,geometry).valid ||
      !photo_paint_layer_to_document(missing,layers,geometry).error.contains("missing")) {
    std::cerr<<"Missing paint parent must fail with a clear error\n"; return 1;
  }
  auto cyclic_outer=outer; cyclic_outer.insert("parentId","inner");
  const auto cyclic=photo_paint_layer_to_document(paint,{paint,inner,cyclic_outer},geometry);
  if (cyclic.valid || !cyclic.error.contains("cycle")) {
    std::cerr<<"Cyclic paint ancestry must fail with a clear error\n"; return 1;
  }
  const auto column=photo_paint_layer_to_document(paint,layers,{{320,180},{40,30,1,120}});
  if (!column.valid || !near(column.local_to_document.inverted().map(column.local_to_document.map(source_point)),source_point)) {
    std::cerr<<"One-pixel execution crops must retain an invertible paint mapping\n"; return 1;
  }
  return 0;
}
