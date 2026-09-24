#pragma once
#include "photo_paint_surface.h"
#include <QColor>
#include <QtGui/qrgbafloat.h>
#include <QTransform>
#include <QMap>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace photo_editor::desktop {

struct PhotoPaintPoint { QPointF position; double pressure = 1; };

class PhotoPixelStroke final {
 public:
  PhotoPaintSurface surface;
  QImage selection;
  QTransform layer_to_document;
  QColor color = Qt::black;
  double diameter = 32, hardness = 0.8, opacity = 1, flow = 1, spacing = 0.15;
  bool erase = false, pressure_size = true, pressure_flow = false, changed = false;
  QString error;
  std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);

  bool begin(const QVariantMap& layer) {
    if (!surface.load(layer,&error)) return false;
    QImage ink(1,1,QImage::Format_RGBA32FPx4);
    ink.setColorSpace(QColorSpace(QColorSpace::SRgb));
    auto* p = reinterpret_cast<QRgbaFloat32*>(ink.bits());
    *p = QRgbaFloat32{color.redF(),color.greenF(),color.blueF(),1.0F};
    ink = ink.convertedToColorSpace(surface.color_space,QImage::Format_RGBA32FPx4);
    ink_ = *reinterpret_cast<const QRgbaFloat32*>(ink.constBits());
    return true;
  }
  void point(QPointF document_point, double pressure) {
    if (surface.size.isEmpty() || cancelled->load() || !error.isEmpty() ||
        !std::isfinite(document_point.x()) || !std::isfinite(document_point.y()) || !std::isfinite(pressure)) return;
    pressure = std::clamp(pressure,0.0,1.0);
    auto point = layer_to_document.inverted().map(document_point);
    // Limit work for pointer excursions outside the viewport, retaining a full
    // brush-radius margin so returning from outside does not paint a false edge.
    point.setX(std::clamp(point.x(),-100000.0,surface.size.width()+100000.0));
    point.setY(std::clamp(point.y(),-100000.0,surface.size.height()+100000.0));
    const auto scale = std::max(0.05,std::hypot(layer_to_document.m11(),layer_to_document.m12()));
    const auto step = [&](double p) { return std::max(0.25,diameter*spacing*(pressure_size?p:1.0)/scale); };
    if (!has_point_) {
      dab(point,pressure,scale); has_point_=true; distance_to_next_=step(pressure);
    } else {
      const auto delta=point-last_; const auto distance=std::hypot(delta.x(),delta.y());
      auto travelled=distance_to_next_;
      while(travelled<=distance && distance>0 && !cancelled->load() && error.isEmpty()) {
        const auto t=travelled/distance, p=last_pressure_+(pressure-last_pressure_)*t;
        dab(last_+delta*t,p,scale); travelled+=step(p);
      }
      distance_to_next_=travelled-distance;
    }
    last_=point; last_pressure_=pressure;
  }
  void finish() {
    if(has_point_ && std::hypot(last_.x()-last_dab_.x(),last_.y()-last_dab_.y())>1e-6)
      dab(last_,last_pressure_,std::max(0.05,std::hypot(layer_to_document.m11(),layer_to_document.m12())));
  }
  QVariantMap draft_tiles() const {
    QVariantMap result;
    for(auto it=touched_.cbegin();it!=touched_.cend();++it) result.insert(it.key(),it->image);
    return result;
  }
  QVariantMap commit() {
    for(auto it=touched_.cbegin();it!=touched_.cend() && !cancelled->load();++it) {
      const auto parts=it.key().split(','); surface.store(parts[0].toInt(),parts[1].toInt(),it->image);
    }
    return surface.snapshot();
  }
  qsizetype changed_tile_count() const { return touched_.size(); }
 private:
  struct Tile { QImage baseline, image; std::vector<float> coverage; };
  QMap<QString,Tile> touched_;
  QRgbaFloat32 ink_{};
  QPointF last_,last_dab_;
  double last_pressure_=1, distance_to_next_=0;
  bool has_point_=false;
  void dab(QPointF point,double pressure,double scale) {
    last_dab_=point;
    if(pressure<=0 || cancelled->load()) return;
    const auto radius=std::max(0.5,diameter*(pressure_size?pressure:1.0)/(2*scale));
    const QRect bounds(QPoint(std::max(0,static_cast<int>(std::floor(point.x()-radius))),
                             std::max(0,static_cast<int>(std::floor(point.y()-radius)))),
                       QPoint(std::min(surface.size.width()-1,static_cast<int>(std::ceil(point.x()+radius))),
                              std::min(surface.size.height()-1,static_cast<int>(std::ceil(point.y()+radius)))));
    if(bounds.isEmpty()) return;
    for(int ty=bounds.top()/kPhotoPaintTileEdge;ty<=bounds.bottom()/kPhotoPaintTileEdge;++ty) {
      for(int tx=bounds.left()/kPhotoPaintTileEdge;tx<=bounds.right()/kPhotoPaintTileEdge;++tx) {
        if(cancelled->load()) return;
        const auto key=PhotoPaintSurface::key(tx,ty);
        auto it=touched_.find(key);
        if(it==touched_.end()) {
          auto base=surface.tile(tx,ty,&error); if(base.isNull()) return;
          it=touched_.insert(key,Tile{base,base.copy(),std::vector<float>(static_cast<std::size_t>(base.width()*base.height()),0)});
        }
        auto& tile=*it; const auto tile_rect=surface.tile_rect(tx,ty); const auto region=bounds.intersected(tile_rect);
        // Detach only this touched tile if a renderer still holds its last draft.
        auto* pixels=reinterpret_cast<QRgbaFloat32*>(tile.image.bits());
        const auto* baseline=reinterpret_cast<const QRgbaFloat32*>(tile.baseline.constBits());
        for(int y=region.top();y<=region.bottom();++y) for(int x=region.left();x<=region.right();++x) {
          const auto distance=std::hypot(x+0.5-point.x(),y+0.5-point.y());
          const auto edge=std::clamp((radius-distance)/std::max(1.0,radius*(1-hardness)),0.0,1.0);
          if(edge<=0) continue;
          const auto index=static_cast<std::size_t>((y-tile_rect.y())*tile.image.width()+x-tile_rect.x());
          auto& weight=tile.coverage[index];
          weight=static_cast<float>(1-(1-weight)*(1-flow*(pressure_flow?pressure:1.0)*edge));
          double clip=1;
          if(!selection.isNull()) {
            const auto doc=layer_to_document.map(QPointF(x+0.5,y+0.5));
            const int sx=static_cast<int>(std::floor(doc.x())),sy=static_cast<int>(std::floor(doc.y()));
            clip=sx>=0&&sy>=0&&sx<selection.width()&&sy<selection.height()?selection.constScanLine(sy)[sx]/255.0:0;
          }
          const auto alpha=static_cast<float>(weight*opacity*color.alphaF()*clip), keep=1-alpha;
          const auto& b=baseline[index];
          const QRgbaFloat32 out{b.red()*keep+(erase?0:ink_.red()*alpha),
              b.green()*keep+(erase?0:ink_.green()*alpha),b.blue()*keep+(erase?0:ink_.blue()*alpha),
              b.alpha()*keep+(erase?0:alpha)};
          changed=changed || out!=pixels[index]; pixels[index]=out;
        }
      }
    }
  }
};

inline QTransform photo_pixel_layer_transform(const QVariantMap& layer) {
  const auto scale=layer.value("scale",1.0).toDouble();
  const auto width=layer.value("pixelWidth").toDouble(),height=layer.value("pixelHeight").toDouble();
  QTransform transform;
  transform.translate(layer.value("x").toDouble()+width*scale/2,layer.value("y").toDouble()+height*scale/2);
  transform.rotate(layer.value("rotation").toDouble()); transform.scale(scale,scale); transform.translate(-width/2,-height/2);
  return transform;
}
}  // namespace photo_editor::desktop
