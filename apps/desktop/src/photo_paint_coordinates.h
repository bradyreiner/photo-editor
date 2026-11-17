#pragma once

#include "desktop_source_geometry.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QTransform>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace photo_editor::desktop {

struct PhotoPaintCoordinateTransform final {
  QTransform local_to_document;
  QString error;
  bool valid = false;
};

// Match the native layer compiler/compositor before final photo Geometry.
// Stroke input is in the paint layer's frozen document basis, whereas groups
// transform a full execution-canvas raster (not the child's authoring canvas).
[[nodiscard]] inline PhotoPaintCoordinateTransform photo_paint_layer_to_document(
    const QVariantMap& authoring_layer,const QVariantList& layers,
    const DesktopSourceGeometry& geometry) {
  const auto fail=[](const QString& error) {return PhotoPaintCoordinateTransform{{},error,false};};
  if (!geometry.valid()) return fail(QStringLiteral("Paint source/crop geometry is unavailable."));
  const auto canvas_width=authoring_layer.value("canvasWidth").toInt();
  const auto canvas_height=authoring_layer.value("canvasHeight").toInt();
  const auto pixel_width=authoring_layer.value("pixelWidth").toInt();
  const auto pixel_height=authoring_layer.value("pixelHeight").toInt();
  if (canvas_width<=0 || canvas_height<=0 || pixel_width<=0 || pixel_height<=0)
    return fail(QStringLiteral("The paint layer has invalid canvas dimensions."));
  const auto output=geometry.crop_rect.size();
  const auto current=geometry.edge_area();
  auto sx=static_cast<double>(output.width())/canvas_width;
  auto sy=static_cast<double>(output.height())/canvas_height;
  auto offset_x=0.0,offset_y=0.0;
  if (authoring_layer.value("coordinateSpace").toString()==QStringLiteral("source-area")) {
    const auto basis=authoring_layer.value("coordinateArea").toMap();
    const auto bx=basis.value("x").toDouble(),by=basis.value("y").toDouble();
    const auto bw=basis.value("width").toDouble(),bh=basis.value("height").toDouble();
    if (!std::isfinite(bx) || !std::isfinite(by) || !std::isfinite(bw) || !std::isfinite(bh) || bw<=0 || bh<=0)
      return fail(QStringLiteral("The paint layer has an invalid source coordinate basis."));
    sx*=bw/current.width(); sy*=bh/current.height();
    offset_x=(bx-current.x())/current.width()*output.width();
    offset_y=(by-current.y())/current.height()*output.height();
  }
  const auto representable=[](double value) {
    return std::isfinite(value) && value>=static_cast<double>(std::numeric_limits<int>::min())+1.0 &&
        value<=static_cast<double>(std::numeric_limits<int>::max())-1.0;
  };
  if (!representable(pixel_width*sx) || !representable(pixel_height*sy) ||
      !representable(authoring_layer.value("x").toInt()*sx+offset_x) ||
      !representable(authoring_layer.value("y").toInt()*sy+offset_y))
    return fail(QStringLiteral("The paint layer transform exceeds supported coordinates."));
  const auto raster_width=std::max(1,qRound(pixel_width*sx));
  const auto raster_height=std::max(1,qRound(pixel_height*sy));
  const auto x=qRound(authoring_layer.value("x").toInt()*sx+offset_x);
  const auto y=qRound(authoring_layer.value("y").toInt()*sy+offset_y);
  struct Placement {double x,y,width,height,scale,rotation;};
  const auto placement=[](const QVariantMap& layer,double px,double py,double width,double height) {
    return Placement{px,py,width,height,std::clamp(layer.value("scale",1.0).toDouble(),0.05,32.0),
        std::clamp(layer.value("rotation").toDouble(),-3600.0,3600.0)};
  };
  const auto copy_base=authoring_layer.value("copyBaseImage").toBool();
  const auto local=copy_base
      ? placement(authoring_layer,
          qRound(authoring_layer.value("x").toInt()*static_cast<double>(output.width())/canvas_width),
          qRound(authoring_layer.value("y").toInt()*static_cast<double>(output.height())/canvas_height),
          output.width(),output.height())
      : placement(authoring_layer,x,y,raster_width,raster_height);
  if (!std::isfinite(local.scale) || !std::isfinite(local.rotation))
    return fail(QStringLiteral("The paint layer has a non-finite transform."));
  QHash<QString,QVariantMap> by_id;
  for (const auto& value:layers) {
    const auto layer=value.toMap(); const auto id=layer.value("id").toString().trimmed();
    if (id.isEmpty()) continue;
    if (by_id.contains(id)) return fail(QStringLiteral("Paint layer IDs are duplicated."));
    by_id.insert(id,layer);
  }
  QVector<Placement> parents;
  QSet<QString> visited;
  const auto own_id=authoring_layer.value("id").toString().trimmed();
  if (!own_id.isEmpty()) visited.insert(own_id);
  auto parent_id=authoring_layer.value("parentId").toString().trimmed();
  while (!parent_id.isEmpty()) {
    if (visited.contains(parent_id)) return fail(QStringLiteral("Paint layer groups contain a parent cycle."));
    visited.insert(parent_id);
    const auto found=by_id.constFind(parent_id);
    if (found==by_id.cend()) return fail(QStringLiteral("The paint layer references a missing parent group."));
    const auto& parent=found.value();
    if (parent.value("kind").toString().trimmed().toLower()!=QStringLiteral("group"))
      return fail(QStringLiteral("The paint layer parent is not a group."));
    const auto parent_canvas_width=parent.value("canvasWidth").toInt();
    const auto parent_canvas_height=parent.value("canvasHeight").toInt();
    const auto px_scale=parent_canvas_width>0 ? static_cast<double>(output.width())/parent_canvas_width : 1.0;
    const auto py_scale=parent_canvas_height>0 ? static_cast<double>(output.height())/parent_canvas_height : 1.0;
    const auto px=std::round(parent.value("x").toInt()*px_scale);
    const auto py=std::round(parent.value("y").toInt()*py_scale);
    if (!representable(px) || !representable(py))
      return fail(QStringLiteral("The parent group exceeds supported paint coordinates."));
    const auto group=placement(parent,px,py,output.width(),output.height());
    if (!std::isfinite(group.scale) || !std::isfinite(group.rotation))
      return fail(QStringLiteral("The parent group has a non-finite transform."));
    parents.append(group);
    parent_id=parent.value("parentId").toString().trimmed();
  }
  const auto apply=[](QPointF point,const Placement& transform) {
    const auto radians=transform.rotation*std::numbers::pi_v<double>/180.0;
    const auto dx=(point.x()-transform.width*0.5)*transform.scale;
    const auto dy=(point.y()-transform.height*0.5)*transform.scale;
    return QPointF(transform.x+transform.width*transform.scale*0.5+dx*std::cos(radians)-dy*std::sin(radians),
        transform.y+transform.height*transform.scale*0.5+dx*std::sin(radians)+dy*std::cos(radians));
  };
  const auto map=[&](QPointF point) {
    point=copy_base ? QPointF(point.x()*sx+offset_x,point.y()*sy+offset_y)
        : QPointF(point.x()*raster_width/pixel_width,point.y()*raster_height/pixel_height);
    point=apply(point,local);
    for (const auto& parent:parents) point=apply(point,parent);
    return QPointF((point.x()-offset_x)/sx,(point.y()-offset_y)/sy);
  };
  const auto origin=map({0,0}),axis_x=map({1,0})-origin,axis_y=map({0,1})-origin;
  QTransform result(axis_x.x(),axis_x.y(),axis_y.x(),axis_y.y(),origin.x(),origin.y());
  if (!result.isInvertible() || !std::isfinite(origin.x()) || !std::isfinite(origin.y()) ||
      !std::isfinite(axis_x.x()) || !std::isfinite(axis_x.y()) ||
      !std::isfinite(axis_y.x()) || !std::isfinite(axis_y.y()))
    return fail(QStringLiteral("The paint layer transform is not invertible."));
  return {result,{},true};
}

}  // namespace photo_editor::desktop
