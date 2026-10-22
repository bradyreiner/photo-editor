#pragma once
#include "photo_paint_surface.h"
#include <QColor>
#include <QtGui/qrgbafloat.h>
#include <QTransform>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
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
  QString texture_material = QStringLiteral("solid");
  double texture_strength = 0.45, texture_scale = 5, texture_angle = 0;
  quint32 texture_seed = 1;
  QImage tip_image;
  double tip_rotation_variation = 0, tip_scale_variation = 0;
  double light_falloff = 0.55, light_exposure = 2.0, light_taper = 0.0;
  bool light_accumulation = true;
  bool erase = false, pressure_size = true, pressure_flow = false, changed = false;
  QString error;
  std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);

  static void clearTipMipCache() {
    QMutexLocker lock(&tipMipCacheMutex());
    tipMipCache() = {};
  }

  bool begin(const QVariantMap& layer) {
    if (!surface.load(layer,&error)) return false;
    if (!tip_image.isNull()) {
      const auto input_cache_key = tip_image.cacheKey();
      {
        QMutexLocker lock(&tipMipCacheMutex());
        const auto& cache = tipMipCache();
        if (cache.image_key == input_cache_key && cache.color_space == surface.color_space) {
          tip_mips_ = cache.levels;
        }
      }
      if (tip_mips_.empty()) {
        auto source = tip_image;
        if (!source.colorSpace().isValid()) source.setColorSpace(QColorSpace(QColorSpace::SRgb));
        source = convert_tagged_qimage_color_space(source, surface.color_space,
            QImage::Format_RGBA32FPx4_Premultiplied,
            QImageColorConversionPurpose::DecodeNormalization);
        if (source.isNull()) {
          error = QStringLiteral("The photo-fragment tip could not be converted to the paint color space.");
          return false;
        }
        tip_mips_.clear();
        tip_mips_.push_back(std::move(source));
        while (tip_mips_.back().width() > 1 || tip_mips_.back().height() > 1) {
          auto mip = downsample_tip_half(tip_mips_.back(), surface.color_space);
          if (mip.isNull()) {
            error = QStringLiteral("The photo-fragment tip could not build its filtered scale levels.");
            tip_mips_.clear();
            return false;
          }
          tip_mips_.push_back(std::move(mip));
        }
        QMutexLocker lock(&tipMipCacheMutex());
        tipMipCache() = {input_cache_key, surface.color_space, tip_mips_};
      }
    }
    QImage ink(1,1,QImage::Format_RGBA32FPx4);
    ink.setColorSpace(QColorSpace(QColorSpace::SRgb));
    auto* p = reinterpret_cast<QRgbaFloat32*>(ink.bits());
    *p = QRgbaFloat32{color.redF(),color.greenF(),color.blueF(),1.0F};
    ink = convert_tagged_qimage_color_space(ink, surface.color_space,
        QImage::Format_RGBA32FPx4, QImageColorConversionPurpose::DecodeNormalization);
    ink_ = *reinterpret_cast<const QRgbaFloat32*>(ink.constBits());
    if (texture_material == QStringLiteral("light_paint")) {
      const auto radiance = static_cast<float>(std::exp2(std::clamp(light_exposure, -8.0, 8.0)));
      ink_ = QRgbaFloat32{ink_.red() * radiance, ink_.green() * radiance,
          ink_.blue() * radiance, ink_.alpha()};
    }
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
    if (texture_material == QStringLiteral("light_paint") && light_taper > 0 && !replaying_light_path_) {
      if (light_path_.size() >= kMaxLightPathPoints) {
        error = QStringLiteral("Light-paint stroke exceeded the supported path sample limit.");
        return;
      }
      light_path_.push_back({point, pressure});
    }
    apply_point(point, pressure);
  }
  void finish() {
    if (!has_point_) return;
    if (texture_material == QStringLiteral("light_paint") && light_taper > 0 && !light_path_.empty()) {
      // The live draft uses a leading taper. Once the pointer is released, replay
      // the bounded sample list so both ends receive the path-length taper.
      double total_distance = 0;
      for (std::size_t i = 1; i < light_path_.size(); ++i) {
        const auto delta = light_path_[i].position - light_path_[i - 1].position;
        total_distance += std::hypot(delta.x(), delta.y());
      }
      for (auto it = touched_.begin(); it != touched_.end(); ++it) {
        it->image = it->baseline.copy();
        std::fill(it->coverage.begin(), it->coverage.end(), 0.0F);
      }
      changed = false;
      has_point_ = false;
      distance_to_next_ = 0;
      path_distance_ = 0;
      taper_path_length_ = total_distance;
      replaying_light_path_ = true;
      for (const auto& sample : light_path_) {
        if (cancelled->load() || !error.isEmpty()) break;
        apply_point(sample.position, sample.pressure);
      }
      if (!cancelled->load() && error.isEmpty() && has_point_ &&
          std::hypot(last_.x() - last_dab_.x(), last_.y() - last_dab_.y()) > 1e-6) {
        const auto scale = std::max(0.05, std::hypot(layer_to_document.m11(), layer_to_document.m12()));
        dab(last_, last_pressure_, scale, taper_coverage(path_distance_, taper_path_length_));
      }
      replaying_light_path_ = false;
      taper_path_length_ = -1;
      return;
    }
    if (std::hypot(last_.x()-last_dab_.x(),last_.y()-last_dab_.y())>1e-6) {
      const auto scale = std::max(0.05,std::hypot(layer_to_document.m11(),layer_to_document.m12()));
      dab(last_,last_pressure_,scale,taper_coverage(path_distance_, taper_path_length_));
    }
  }
 private:
  void apply_point(QPointF point, double pressure) {
    const auto scale = std::max(0.05,std::hypot(layer_to_document.m11(),layer_to_document.m12()));
    const auto step = [&](double p) { return std::max(0.25,diameter*spacing*(pressure_size?p:1.0)/scale); };
    if (!has_point_) {
      dab(point,pressure,scale,taper_coverage(path_distance_, taper_path_length_));
      has_point_=true; distance_to_next_=step(pressure);
    } else {
      const auto delta=point-last_; const auto distance=std::hypot(delta.x(),delta.y());
      if (distance > 1e-9) direction_ = std::atan2(delta.y(), delta.x());
      auto travelled=distance_to_next_;
      while(travelled<=distance && distance>0 && !cancelled->load() && error.isEmpty()) {
        const auto t=travelled/distance, p=last_pressure_+(pressure-last_pressure_)*t;
        dab(last_+delta*t,p,scale,taper_coverage(path_distance_ + travelled, taper_path_length_));
        travelled+=step(p);
      }
      distance_to_next_=travelled-distance;
      path_distance_ += distance;
    }
    last_=point; last_pressure_=pressure;
  }
 public:
  void erase_selection(bool accumulate_erased_coverage = false) {
    if (selection.isNull()) { error = QStringLiteral("A selection is required to erase pixels."); return; }
    for (int ty = 0; ty * kPhotoPaintTileEdge < surface.size.height(); ++ty) {
      for (int tx = 0; tx * kPhotoPaintTileEdge < surface.size.width(); ++tx) {
        if (cancelled->load()) return;
        auto tile = surface.tile(tx, ty, &error);
        if (tile.isNull()) return;
        const auto rect = surface.tile_rect(tx, ty);
        bool tile_changed = false;
        for (int y = 0; y < tile.height(); ++y) {
          auto* row = reinterpret_cast<QRgbaFloat32*>(tile.scanLine(y));
          for (int x = 0; x < tile.width(); ++x) {
            const auto doc = layer_to_document.map(QPointF(rect.x() + x + 0.5, rect.y() + y + 0.5));
            const int sx = static_cast<int>(std::floor(doc.x())), sy = static_cast<int>(std::floor(doc.y()));
            if (sx < 0 || sy < 0 || sx >= selection.width() || sy >= selection.height()) continue;
            const float keep = 1.0F - selection.constScanLine(sy)[sx] / 255.0F;
            if (keep == 1.0F || (!accumulate_erased_coverage && row[x].alpha() == 0.0F)) continue;
            const auto pixel = row[x];
            if (accumulate_erased_coverage) {
              const float alpha = 1.0F - (1.0F - pixel.alpha()) * keep;
              if (alpha == pixel.alpha()) continue;
              row[x] = QRgbaFloat32{alpha, alpha, alpha, alpha};
            } else row[x] = QRgbaFloat32{pixel.red()*keep, pixel.green()*keep, pixel.blue()*keep, pixel.alpha()*keep};
            tile_changed = true;
          }
        }
        if (tile_changed) { surface.store(tx, ty, tile); changed = true; }
      }
    }
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
  struct TipMipCache {
    qint64 image_key = 0;
    QColorSpace color_space;
    std::vector<QImage> levels;
  };
  static QMutex& tipMipCacheMutex() {
    static QMutex mutex;
    return mutex;
  }
  static TipMipCache& tipMipCache() {
    static TipMipCache cache;
    return cache;
  }
  struct PathSample { QPointF position; double pressure = 1; };
  static constexpr std::size_t kMaxLightPathPoints = 100000;
  struct Tile { QImage baseline, image; std::vector<float> coverage; };
  QMap<QString,Tile> touched_;
  std::vector<PathSample> light_path_;
  std::vector<QImage> tip_mips_;
  QRgbaFloat32 ink_{};
  QPointF last_,last_dab_;
  double last_pressure_=1, distance_to_next_=0, path_distance_=0, taper_path_length_=-1;
  double direction_=0;
  double texture_cos_=1, texture_sin_=0;
  bool has_point_=false, replaying_light_path_=false;
  static double smoothstep(double lower, double upper, double value) {
    const auto t=std::clamp((value-lower)/std::max(1e-9,upper-lower),0.0,1.0);
    return t*t*(3-2*t);
  }
  double taper_coverage(double distance, double total_distance) const {
    if (texture_material != QStringLiteral("light_paint") || light_taper <= 0) return 1.0;
    if (total_distance >= 0 && total_distance < 1.0) return 1.0;
    const auto scale = std::max(0.05, std::hypot(layer_to_document.m11(), layer_to_document.m12()));
    const auto taper_length = std::max(1.0, diameter * (1.0 + 3.0 * std::clamp(light_taper, 0.0, 1.0)) / scale);
    const auto start = smoothstep(0.0, taper_length, distance);
    const auto end = total_distance < 0 ? 1.0 : smoothstep(0.0, taper_length, total_distance - distance);
    return start * end;
  }
  static double lattice_value(int x, int y, quint32 seed) {
    auto value=static_cast<std::uint64_t>(seed) ^
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x))*0x9e3779b185ebca87ULL) ^
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y))*0xc2b2ae3d27d4eb4fULL);
    value^=value>>30; value*=0xbf58476d1ce4e5b9ULL;
    value^=value>>27; value*=0x94d049bb133111ebULL; value^=value>>31;
    return static_cast<double>(value>>11)*(1.0/9007199254740991.0);
  }
  static QRgbaFloat32 sample_tip_bilinear(const QImage& image, double x, double y) {
    const auto x0 = static_cast<int>(std::floor(x));
    const auto y0 = static_cast<int>(std::floor(y));
    const auto tx = x - x0;
    const auto ty = y - y0;
    const auto sample = [&image](int sample_x, int sample_y) {
      if (sample_x < 0 || sample_y < 0 || sample_x >= image.width() || sample_y >= image.height())
        return QRgbaFloat32{};
      const auto* row = reinterpret_cast<const QRgbaFloat32*>(image.constScanLine(sample_y));
      return row[sample_x];
    };
    const auto top_left = sample(x0, y0);
    const auto top_right = sample(x0 + 1, y0);
    const auto bottom_left = sample(x0, y0 + 1);
    const auto bottom_right = sample(x0 + 1, y0 + 1);
    const auto channel = [tx, ty](float tl, float tr, float bl, float br) {
      const auto top = static_cast<double>(tl) * (1.0 - tx) + static_cast<double>(tr) * tx;
      const auto bottom = static_cast<double>(bl) * (1.0 - tx) + static_cast<double>(br) * tx;
      return static_cast<float>(top * (1.0 - ty) + bottom * ty);
    };
    return QRgbaFloat32{
        channel(top_left.red(), top_right.red(), bottom_left.red(), bottom_right.red()),
        channel(top_left.green(), top_right.green(), bottom_left.green(), bottom_right.green()),
        channel(top_left.blue(), top_right.blue(), bottom_left.blue(), bottom_right.blue()),
        channel(top_left.alpha(), top_right.alpha(), bottom_left.alpha(), bottom_right.alpha())};
  }
  static QImage downsample_tip_half(const QImage& source, const QColorSpace& color_space) {
    const auto width = std::max(1, (source.width() + 1) / 2);
    const auto height = std::max(1, (source.height() + 1) / 2);
    QImage result(width, height, QImage::Format_RGBA32FPx4_Premultiplied);
    if (result.isNull()) return {};
    result.setColorSpace(color_space);
    for (int y = 0; y < height; ++y) {
      auto* destination = reinterpret_cast<QRgbaFloat32*>(result.scanLine(y));
      for (int x = 0; x < width; ++x) {
        QRgbaFloat32 sum{};
        int count = 0;
        for (int source_y = y * 2; source_y < std::min(source.height(), y * 2 + 2); ++source_y) {
          const auto* row = reinterpret_cast<const QRgbaFloat32*>(source.constScanLine(source_y));
          for (int source_x = x * 2; source_x < std::min(source.width(), x * 2 + 2); ++source_x) {
            sum = QRgbaFloat32{sum.red() + row[source_x].red(), sum.green() + row[source_x].green(),
                sum.blue() + row[source_x].blue(), sum.alpha() + row[source_x].alpha()};
            ++count;
          }
        }
        const auto reciprocal = 1.0F / static_cast<float>(std::max(1, count));
        destination[x] = QRgbaFloat32{sum.red() * reciprocal, sum.green() * reciprocal,
            sum.blue() * reciprocal, sum.alpha() * reciprocal};
      }
    }
    return result;
  }
  QRgbaFloat32 sample_tip_mipmapped(double x, double y,
      std::size_t lower, std::size_t upper, float blend) const {
    if (tip_mips_.empty()) return {};
    const auto sample = [x, y](const QImage& mip) {
      return sample_tip_bilinear(mip, x * mip.width() - 0.5, y * mip.height() - 0.5);
    };
    const auto a = sample(tip_mips_[lower]);
    if (upper == lower) return a;
    const auto b = sample(tip_mips_[upper]);
    const auto channel = [blend](float first, float second) { return first * (1.0F - blend) + second * blend; };
    return QRgbaFloat32{channel(a.red(), b.red()), channel(a.green(), b.green()),
        channel(a.blue(), b.blue()), channel(a.alpha(), b.alpha())};
  }
  static double value_noise(double x, double y, quint32 seed) {
    const auto ix=static_cast<int>(std::floor(x)), iy=static_cast<int>(std::floor(y));
    const auto fx=x-ix, fy=y-iy;
    const auto sx=fx*fx*(3-2*fx), sy=fy*fy*(3-2*fy);
    const auto a=lattice_value(ix,iy,seed), b=lattice_value(ix+1,iy,seed);
    const auto c=lattice_value(ix,iy+1,seed), d=lattice_value(ix+1,iy+1,seed);
    return (a+(b-a)*sx)*(1-sy)+(c+(d-c)*sx)*sy;
  }
  double material_coverage(double x, double y) const {
    if (texture_material == QStringLiteral("solid") || texture_material == QStringLiteral("light_paint") ||
        texture_material == QStringLiteral("photo_fragment") || texture_strength <= 0) return 1;
    const auto along=x*texture_cos_+y*texture_sin_;
    const auto across=-x*texture_sin_+y*texture_cos_;
    const auto scale=std::max(0.5,texture_scale);
    const auto coarse=value_noise(along/(scale*3.2),across/scale,texture_seed);
    const auto fine=value_noise(along/scale,across/(scale*0.42),texture_seed^0x68bc21ebU);
    const auto grain=0.68*coarse+0.32*fine;
    if (texture_material == QStringLiteral("dry_ink")) {
      const auto broken=smoothstep(0.18,0.76,grain);
      return 1-texture_strength*(1-broken);
    }
    const auto graphite=0.58*coarse+0.42*value_noise(along/(scale*0.55),across/(scale*0.24),texture_seed^0x02e5be93U);
    return 1-texture_strength*(0.18+0.58*(1-graphite));
  }
  void dab(QPointF point,double pressure,double scale,double stroke_taper = 1.0) {
    last_dab_=point;
    if(pressure<=0 || cancelled->load()) return;
    const auto texture_direction=direction_+texture_angle*0.017453292519943295;
    texture_cos_=std::cos(texture_direction);
    texture_sin_=std::sin(texture_direction);
    const auto dab_number = dab_index_++;
    const auto pressure_scale = pressure_size ? pressure : 1.0;
    const auto radius=std::max(0.5,diameter*pressure_scale/(2*scale));
    const bool photo_fragment = !tip_image.isNull() && texture_material == QStringLiteral("photo_fragment");
    const auto scale_random = lattice_value(static_cast<int>(dab_number), 3, texture_seed);
    const auto fragment_scale = photo_fragment
        ? std::clamp(1.0 + (scale_random * 2.0 - 1.0) * tip_scale_variation, 0.1, 2.0)
        : 1.0;
    const auto fragment_width = photo_fragment
        ? static_cast<double>(tip_image.width()) * diameter * pressure_scale * fragment_scale /
              (static_cast<double>(std::max(tip_image.width(), tip_image.height())) * scale)
        : radius * 2.0;
    const auto fragment_height = photo_fragment
        ? static_cast<double>(tip_image.height()) * diameter * pressure_scale * fragment_scale /
              (static_cast<double>(std::max(tip_image.width(), tip_image.height())) * scale)
        : radius * 2.0;
    const auto fragment_mip_level = photo_fragment
        ? std::max(0.0, std::log2(std::max(
              static_cast<double>(tip_image.width()) / std::max(1e-6, fragment_width),
              static_cast<double>(tip_image.height()) / std::max(1e-6, fragment_height))))
        : 0.0;
    const auto maximum_fragment_mip = tip_mips_.empty() ? std::size_t{0} : tip_mips_.size() - 1;
    const auto bounded_fragment_mip_level = std::clamp(fragment_mip_level, 0.0,
        static_cast<double>(maximum_fragment_mip));
    const auto fragment_mip_lower = static_cast<std::size_t>(std::floor(bounded_fragment_mip_level));
    const auto fragment_mip_upper = std::min(maximum_fragment_mip, fragment_mip_lower + 1);
    const auto fragment_mip_blend = static_cast<float>(
        bounded_fragment_mip_level - static_cast<double>(fragment_mip_lower));
    const auto rotation_random = lattice_value(static_cast<int>(dab_number), 7, texture_seed);
    const auto fragment_angle = direction_ + (rotation_random * 2.0 - 1.0) *
        tip_rotation_variation * 0.017453292519943295;
    const auto fragment_cos = std::cos(fragment_angle);
    const auto fragment_sin = std::sin(fragment_angle);
    const auto fragment_bound = photo_fragment
        ? 0.5 * std::hypot(fragment_width, fragment_height) : radius;
    const bool light_paint = texture_material == QStringLiteral("light_paint");
    const auto outer_radius = light_paint
        ? radius * (1.0 + 2.0 * std::clamp(light_falloff, 0.0, 1.0)) : fragment_bound;
    const auto extent_x = photo_fragment
        ? 0.5 * (std::abs(fragment_cos) * fragment_width + std::abs(fragment_sin) * fragment_height)
        : outer_radius;
    const auto extent_y = photo_fragment
        ? 0.5 * (std::abs(fragment_sin) * fragment_width + std::abs(fragment_cos) * fragment_height)
        : outer_radius;
    const QRect bounds(QPoint(std::max(0,static_cast<int>(std::floor(point.x()-extent_x))),
                             std::max(0,static_cast<int>(std::floor(point.y()-extent_y)))),
                       QPoint(std::min(surface.size.width()-1,static_cast<int>(std::ceil(point.x()+extent_x))),
                              std::min(surface.size.height()-1,static_cast<int>(std::ceil(point.y()+extent_y)))));
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
          const auto delta_x = x + 0.5 - point.x();
          const auto delta_y = y + 0.5 - point.y();
          const auto distance = photo_fragment ? 0.0 : std::hypot(delta_x, delta_y);
          auto edge=photo_fragment ? 1.0 :
              std::clamp((radius-distance)/std::max(1.0,radius*(1-hardness)),0.0,1.0);
          QRgbaFloat32 fragment_pixel{};
          if (photo_fragment) {
            const auto source_x = (fragment_cos * delta_x + fragment_sin * delta_y) /
                std::max(1e-6, fragment_width) + 0.5;
            const auto source_y = (-fragment_sin * delta_x + fragment_cos * delta_y) /
                std::max(1e-6, fragment_height) + 0.5;
            if (source_x < 0 || source_y < 0 || source_x >= 1 || source_y >= 1) continue;
            fragment_pixel = sample_tip_mipmapped(source_x, source_y,
                fragment_mip_lower, fragment_mip_upper, fragment_mip_blend);
            if (fragment_pixel.alpha() <= 0.0F) continue;
          }
          if (light_paint && light_falloff > 0 && distance > radius && outer_radius > radius) {
            const auto tail = (distance - radius) / (outer_radius - radius);
            edge = std::max(edge, std::clamp(light_falloff, 0.0, 1.0) * std::exp(-4.0 * tail * tail));
          }
          if (light_paint) edge *= std::clamp(stroke_taper, 0.0, 1.0);
          if(edge<=0) continue;
          const auto index=static_cast<std::size_t>((y-tile_rect.y())*tile.image.width()+x-tile_rect.x());
          auto& weight=tile.coverage[index];
          const auto dab_flow = std::clamp(flow * (pressure_flow ? pressure : 1.0) * edge, 0.0, 1.0);
          weight = static_cast<float>(light_paint && !light_accumulation
              ? std::max(static_cast<double>(weight), dab_flow)
              : 1.0 - (1.0 - weight) * (1.0 - dab_flow));
          double clip=1;
          if(!selection.isNull()) {
            const auto doc=layer_to_document.map(QPointF(x+0.5,y+0.5));
            const int sx=static_cast<int>(std::floor(doc.x())),sy=static_cast<int>(std::floor(doc.y()));
            clip=sx>=0&&sy>=0&&sx<selection.width()&&sy<selection.height()?selection.constScanLine(sy)[sx]/255.0:0;
          }
          const auto texture=erase?1.0:material_coverage(x+0.5,y+0.5);
          const auto fragment_opacity = photo_fragment ? static_cast<double>(fragment_pixel.alpha()) : 1.0;
          const auto alpha=static_cast<float>(weight*opacity*color.alphaF()*clip*texture*fragment_opacity), keep=1-alpha;
          const auto& b=baseline[index];
          const auto fragment_gain = static_cast<float>(weight*opacity*color.alphaF()*clip*texture);
          const QRgbaFloat32 out{b.red()*keep+(erase?0:(photo_fragment ? fragment_pixel.red()*fragment_gain : ink_.red()*alpha)),
              b.green()*keep+(erase?0:(photo_fragment ? fragment_pixel.green()*fragment_gain : ink_.green()*alpha)),
              b.blue()*keep+(erase?0:(photo_fragment ? fragment_pixel.blue()*fragment_gain : ink_.blue()*alpha)),
              b.alpha()*keep+(erase?0:alpha)};
          changed=changed || out!=pixels[index]; pixels[index]=out;
        }
      }
    }
  }
  std::uint64_t dab_index_ = 0;
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
