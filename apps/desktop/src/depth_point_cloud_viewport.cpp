#include "depth_point_cloud_viewport.h"
#include <QDir>
#include <QColor>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QUrl>
#include <QtConcurrent>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace photo_editor::desktop {
namespace {
constexpr int kMaximumPoints = 200000;
constexpr float kTanHalfFov = 0.46630766F;
bool finite(QVector3D p) { return std::isfinite(p.x()) && std::isfinite(p.y()) && std::isfinite(p.z()); }
class CloudTextureNode final : public QSGSimpleTextureNode {
 public:
  ~CloudTextureNode() override { setOwnsTexture(false); delete texture(); }
  quint64 publication = 0;
};
}
DepthCloudRender render_depth_cloud(const QString& directory, int index, int count,
    QSize size, const DepthCloudView& view, std::shared_ptr<const DepthCloudFrame> cached) {
  DepthCloudRender result; result.index = index;
  QElapsedTimer elapsed; elapsed.start();
  if (count <= 0 || count > kMaximumPoints || size.isEmpty() || size.width() > 1600 || size.height() > 1600) {
    result.error = "Invalid point-cloud frame bounds."; return result;
  }
  if (!cached || cached->index != index) {
    QFile file(QDir(directory).filePath(QStringLiteral("%1.xyzrgb").arg(index, 6, 10, QLatin1Char('0'))));
    if (!file.open(QIODevice::ReadOnly) || file.size() != qint64(count) * 15) {
      result.error = "A point-cloud frame is missing or incomplete. Rebuild the 3D view."; return result;
    }
    const auto bytes = file.readAll();
    auto frame = std::make_shared<DepthCloudFrame>(); frame->index = index; frame->points.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
      const auto* p = reinterpret_cast<const uchar*>(bytes.constData()) + qsizetype(i) * 15;
      const QVector3D position(std::bit_cast<float>(qFromLittleEndian<quint32>(p)),
          std::bit_cast<float>(qFromLittleEndian<quint32>(p + 4)), std::bit_cast<float>(qFromLittleEndian<quint32>(p + 8)));
      if (!finite(position) || position.z() <= 0) { result.error = "The point-cloud frame contains invalid depth."; return result; }
      frame->points.push_back({position, qRgb(p[12], p[13], p[14])});
    }
    cached = std::move(frame);
  }
  result.cloud = cached;
  const auto rotation = view.rotation.normalized();
  const auto right = rotation.rotatedVector({1, 0, 0}), down = rotation.rotatedVector({0, 1, 0});
  const auto forward = rotation.rotatedVector({0, 0, 1});
  const auto eye = view.eye.value_or(view.target - forward * view.distance);
  const float focal = float(size.width()) / (2 * kTanHalfFov);
  const int width = size.width(), height = size.height(), diameter = std::clamp(view.point_size, 1, 8);
  result.image = QImage(size, QImage::Format_RGB32); result.image.fill(view.background);
  std::array<float, 64> coverage{};
  for (int y = 0; y < diameter; ++y) for (int x = 0; x < diameter; ++x) {
    const float dx = (float(x) + .5F - float(diameter) / 2) / (float(diameter) / 2);
    const float dy = (float(y) + .5F - float(diameter) / 2) / (float(diameter) / 2);
    const float radius = dx * dx + dy * dy;
    coverage[static_cast<size_t>(y * diameter + x)] = view.shape == 1 ? 1.F : radius > 1 ? 0.F
        : view.shape == 2 ? std::exp(-3 * radius) : 1.F;
  }
  std::vector<float> zbuffer(static_cast<size_t>(width) * static_cast<size_t>(height), std::numeric_limits<float>::infinity());
  const auto threshold = quint32(double(std::numeric_limits<quint32>::max()) * std::clamp(double(view.density), .05, 1.0));
  for (size_t index = 0; index < cached->points.size(); ++index) {
    if (quint32(index) * 2654435761U > threshold) continue;
    const auto& point = cached->points[index];
    auto position = point.position;
    position.setZ(1 + (position.z() - 1) * view.z_scale);
    const auto offset = position - eye;
    const float z = QVector3D::dotProduct(offset, forward);
    if (z <= .01F) continue;
    const float u = focal * QVector3D::dotProduct(offset, right) / z + float(width - 1) / 2;
    const float v = focal * QVector3D::dotProduct(offset, down) / z + float(height - 1) / 2;
    if (u < -6 || v < -6 || u >= float(width + 6) || v >= float(height + 6)) continue;
    float red = float(qRed(point.color)), green = float(qGreen(point.color)), blue = float(qBlue(point.color));
    if (view.color_mode == 1) red = green = blue = .2126F * red + .7152F * green + .0722F * blue;
    else if (view.color_mode == 2) {
      const auto depth_color = std::clamp((point.position.z() - .4F) / 1.2F, 0.F, 1.F);
      const auto low = depth_color < .5F ? view.gradient_near : view.gradient_mid;
      const auto high = depth_color < .5F ? view.gradient_mid : view.gradient_far;
      const auto mix = depth_color < .5F ? depth_color * 2 : (depth_color - .5F) * 2;
      red = float(qRed(low)) * (1 - mix) + float(qRed(high)) * mix;
      green = float(qGreen(low)) * (1 - mix) + float(qGreen(high)) * mix;
      blue = float(qBlue(low)) * (1 - mix) + float(qBlue(high)) * mix;
    }
    red = std::clamp(red * view.brightness, 0.F, 255.F);
    green = std::clamp(green * view.brightness, 0.F, 255.F); blue = std::clamp(blue * view.brightness, 0.F, 255.F);
    const int x = int(std::lround(u)) - diameter / 2, y = int(std::lround(v)) - diameter / 2;
    for (int dy = 0; dy < diameter; ++dy) {
      const int py = y + dy; if (py < 0 || py >= height) continue;
      auto* row = reinterpret_cast<QRgb*>(result.image.scanLine(py));
      for (int dx = 0; dx < diameter; ++dx) {
        const int px = x + dx; if (px < 0 || px >= width) continue;
        const float alpha = coverage[static_cast<size_t>(dy * diameter + dx)]; if (alpha <= 0) continue;
        auto& closest = zbuffer[static_cast<size_t>(py) * static_cast<size_t>(width) + static_cast<size_t>(px)];
        if (z < closest) {
          closest = z;
          row[px] = qRgb(int(std::lround(red * alpha + float(qRed(view.background)) * (1 - alpha))),
              int(std::lround(green * alpha + float(qGreen(view.background)) * (1 - alpha))),
              int(std::lround(blue * alpha + float(qBlue(view.background)) * (1 - alpha))));
        }
      }
    }
  }
  result.milliseconds = elapsed.elapsed();
  return result;
}

DepthPointCloudViewport::DepthPointCloudViewport(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  timer_.setInterval(16); timer_.setTimerType(Qt::PreciseTimer);
  connect(&timer_, &QTimer::timeout, this, [this] {
    if (!isVisible()) { setPlaying(false); return; }
    position_ = std::fmod(play_anchor_ + double(clock_.elapsed()) / 1000, duration_);
    emit positionChanged();
    if (std::min(int(counts_.size()) - 1, int(position_ * fps_)) != requested_index_) requestFrame();
  });
  connect(&worker_, &QFutureWatcher<DepthCloudRender>::finished, this, [this] {
    const auto result = worker_.result(); rendering_ = false;
    // Epochs are stored on the watcher, keeping every worker input immutable.
    if (worker_.property("epoch").toULongLong() == epoch_ && worker_.property("seekEpoch").toULongLong() == seek_epoch_) {
      error_ = result.error;
      if (error_.isEmpty()) {
        cached_ = result.cloud; image_ = result.image; layers_ = result.layers; presented_index_ = result.index;
        displayed_view_ = submitted_view_;
        render_ms_ = result.milliseconds; ++publication_; update();
      } else setPlaying(false);
      emit statusChanged();
    }
    if (submitted_ != revision_) submitFrame();
  });
  connect(this, &QQuickItem::visibleChanged, this, [this] { if (!isVisible()) setPlaying(false); else requestFrame(); });
}
DepthPointCloudViewport::~DepthPointCloudViewport() { timer_.stop(); worker_.waitForFinished(); }
QVariantMap DepthPointCloudViewport::status() const {
  return {{"ready", !counts_.empty()}, {"busy", rendering_}, {"error", error_},
          {"frameCount", int(counts_.size())}, {"frameIndex", presented_index_},
          {"pointCount", cached_ ? int(cached_->points.size()) : 0}, {"renderMilliseconds", render_ms_},
          {"frameSerial", publication_}, {"fps", fps_}, {"frameAvailable", !image_.isNull()},
          {"sceneFocusDistance", view_.distance}, {"imageAspectRatio", height() > 0 ? width() / height() : 1}};
}
void DepthPointCloudViewport::setProject(const QString& input) {
  const auto url = QUrl(input); const auto path = url.isLocalFile() ? url.toLocalFile() : input;
  if (path == project_) return;
  setPlaying(false); project_ = path; ++epoch_; ++seek_epoch_;
  counts_.clear(); cached_.reset(); image_ = {}; layers_.clear(); presented_index_ = -1; position_ = 0; duration_ = 0; view_ = {}; error_.clear();
  QFile file(QDir(project_).filePath("pointcloud/manifest.json"));
  if (!project_.isEmpty() && file.open(QIODevice::ReadOnly)) {
    const auto info = QJsonDocument::fromJson(file.readAll()).object();
    const int frames = info.value("frames").toInt(), fps = info.value("fps").toInt();
    const QSize size(info.value("width").toInt(), info.value("height").toInt());
    const double duration = info.value("duration").toDouble();
    const auto counts = info.value("counts").toArray();
    if (info.value("version").toInt() == 1 && info.value("format").toString() == "xyz-f32le-rgb-u8" &&
        info.value("stride").toInt() == 15 && frames > 0 && frames <= 360 && counts.size() == frames &&
        fps >= 12 && fps <= 30 && size.width() > 0 && size.height() > 0 && size.width() <= 1920 && size.height() <= 1920 &&
        std::isfinite(duration) && duration > 0 && duration <= 12) {
      for (const auto& value : counts) {
        const int count = value.toInt();
        if (count <= 0 || count > kMaximumPoints) { counts_.clear(); break; }
        counts_.push_back(count);
      }
      if (!counts_.empty()) { source_size_ = size; fps_ = fps; duration_ = duration; }
    }
    if (counts_.empty()) error_ = "The point-cloud sequence is invalid. Rebuild the 3D view.";
  }
  emit projectChanged(); emit positionChanged(); emit viewChanged(); emit statusChanged(); update(); requestFrame();
}
void DepthPointCloudViewport::setPlaying(bool value) {
  value = value && !counts_.empty() && duration_ > 0;
  if (playing_ == value) return;
  playing_ = value;
  if (value) { play_anchor_ = position_; clock_.restart(); timer_.start(); } else timer_.stop();
  emit playingChanged();
}
void DepthPointCloudViewport::setPosition(double seconds) {
  if (!std::isfinite(seconds) || duration_ <= 0) return;
  position_ = std::clamp(seconds, 0.0, std::max(0.0, duration_ - 1e-6));
  play_anchor_ = position_; clock_.restart(); ++seek_epoch_;
  emit positionChanged(); requestFrame();
}
void DepthPointCloudViewport::setDepthPreview(bool depth) {
  if (depth_preview_ == depth) return;
  depth_preview_ = depth; ++seek_epoch_; emit depthPreviewChanged(); requestFrame();
}
void DepthPointCloudViewport::setColorPass(const QString& pass) {
  if (pass != "appearance" && pass != "source" && pass != "depth") return;
  if (color_pass_ == pass) return;
  color_pass_ = pass; ++seek_epoch_; requestFrame(); emit statusChanged();
}
void DepthPointCloudViewport::setCaptureLayers(bool enabled) {
  if (capture_layers_ == enabled) return;
  capture_layers_ = enabled; layers_.clear();
  if (enabled) error_.clear();
  requestFrame(); emit statusChanged();
}
QVariantMap DepthPointCloudViewport::recordingLayers() const {
  QVariantMap images;
  for (auto it = layers_.cbegin(); it != layers_.cend(); ++it) images.insert(it.key(), it.value());
  // displayed_view_ belongs to these pixels, rather than a newer unrendered input.
  const auto& v = displayed_view_;
  return {{"images", images}, {"error", error_}, {"sourceFrame", presented_index_}, {"frameSerial", publication_},
      {"camera", QVariantMap{{"rotation", QVariantList{v.rotation.scalar(), v.rotation.x(), v.rotation.y(), v.rotation.z()}},
          {"position", QVariantList{v.eye.value_or(v.target - v.rotation.rotatedVector({0, 0, 1}) * v.distance).x(),
              v.eye.value_or(v.target - v.rotation.rotatedVector({0, 0, 1}) * v.distance).y(),
              v.eye.value_or(v.target - v.rotation.rotatedVector({0, 0, 1}) * v.distance).z()}},
          {"zExaggeration", v.z_scale}, {"pointSize", v.point_size}}}};
}

void DepthPointCloudViewport::setPointSize(int size) {
  size = std::clamp(size, 1, 8); if (size == view_.point_size) return;
  view_.point_size = size; emit viewChanged(); requestFrame();
}
void DepthPointCloudViewport::setZExaggeration(double scale) {
  if (!std::isfinite(scale)) return;
  const auto bounded = float(std::clamp(scale, 0.0, 8.0));
  if (bounded == view_.z_scale) return;
  view_.z_scale = bounded; emit viewChanged(); requestFrame();
}
QVariantMap DepthPointCloudViewport::viewState() const {
  const auto eye = view_.eye.value_or(view_.target - view_.rotation.rotatedVector({0, 0, 1}) * view_.distance);
  return {{"rotation", QVariantList{view_.rotation.scalar(), view_.rotation.x(), view_.rotation.y(), view_.rotation.z()}},
          {"position", QVariantList{eye.x(), eye.y(), eye.z()}},
          {"target", QVariantList{view_.target.x(), view_.target.y(), view_.target.z()}},
          {"distance", view_.distance}, {"pointSize", view_.point_size}, {"zExaggeration", view_.z_scale},
          {"appearance", appearance()}, {"playheadSeconds", position_}};
}
QVariantMap DepthPointCloudViewport::appearance() const {
  return {{"density", view_.density}, {"brightness", view_.brightness}, {"shape", view_.shape},
          {"gradientNear", QColor(view_.gradient_near).name()}, {"gradientMid", QColor(view_.gradient_mid).name()},
          {"gradientFar", QColor(view_.gradient_far).name()}, {"gradientPreset", view_.gradient_name},
          {"colorMode", view_.color_mode}, {"background", QColor(view_.background).name()}};
}
void DepthPointCloudViewport::setAppearance(const QVariantMap& patch) {
  const auto previous = appearance();
  const auto bounded = [&patch](const char* key, float fallback, double low, double high) {
    const auto value = patch.value(QLatin1String(key), fallback).toDouble();
    return std::isfinite(value) ? float(std::clamp(value, low, high)) : fallback;
  };
  view_.density = bounded("density", view_.density, .05, 1);
  view_.brightness = bounded("brightness", view_.brightness, .1, 3);
  view_.shape = std::clamp(patch.value("shape", view_.shape).toInt(), 0, 2);
  view_.color_mode = std::clamp(patch.value("colorMode", view_.color_mode).toInt(), 0, 2);
  for (const auto& entry : {std::pair{"gradientNear", &view_.gradient_near}, {"gradientMid", &view_.gradient_mid}, {"gradientFar", &view_.gradient_far}}) {
    const QColor color(patch.value(QLatin1String(entry.first), QColor(*entry.second).name()).toString());
    if (color.isValid()) *entry.second = color.rgb();
  }
  if (patch.contains("gradientPreset")) view_.gradient_name = patch.value("gradientPreset").toString().left(32);
  const QColor background(patch.value("background", QColor(view_.background).name()).toString());
  if (background.isValid()) view_.background = background.rgb();
  if (appearance() == previous) return;
  emit viewChanged(); requestFrame();
}
void DepthPointCloudViewport::restoreView(const QVariantMap& state) {
  if (state.isEmpty()) return;
  const auto q = state.value("rotation").toList(), t = state.value("target").toList();
  const auto distance = state.value("distance").toDouble();
  if (q.size() != 4 || t.size() != 3 || !std::isfinite(distance) || distance < .05 || distance > 20) return;
  QQuaternion rotation(q[0].toFloat(), q[1].toFloat(), q[2].toFloat(), q[3].toFloat());
  QVector3D target(t[0].toFloat(), t[1].toFloat(), t[2].toFloat());
  if (!finite(target) || !std::isfinite(rotation.lengthSquared()) || rotation.lengthSquared() < 1e-8F) return;
  view_.rotation = std::abs(rotation.lengthSquared() - 1) < 1e-6F ? rotation : rotation.normalized();
  view_.target = target; view_.distance = float(distance);
  const auto position = state.value("position").toList();
  view_.eye.reset();
  if (position.size() == 3) {
    const QVector3D eye(position[0].toFloat(), position[1].toFloat(), position[2].toFloat());
    if (finite(eye)) view_.eye = eye;
  }
  view_.point_size = std::clamp(state.value("pointSize", 1).toInt(), 1, 8);
  const auto z_scale = state.value("zExaggeration", 1).toDouble();
  view_.z_scale = std::isfinite(z_scale) ? float(std::clamp(z_scale, 0.0, 8.0)) : 1;
  setAppearance(state.value("appearance").toMap());
  if (state.contains("playheadSeconds")) setPosition(state.value("playheadSeconds").toDouble());
  emit viewChanged(); requestFrame();
}
void DepthPointCloudViewport::orbit(double yaw, double pitch) {
  if (!std::isfinite(yaw) || !std::isfinite(pitch)) return;
  const auto right = view_.rotation.rotatedVector({1, 0, 0}), down = view_.rotation.rotatedVector({0, 1, 0});
  const auto forward = view_.rotation.rotatedVector({0, 0, 1});
  const auto eye = view_.eye.value_or(view_.target - forward * view_.distance);
  if (QVector3D::dotProduct(view_.target - eye, forward) < .01F) view_.target = eye + forward * view_.distance;
  const auto rotation = QQuaternion::fromAxisAndAngle(-down, float(std::remainder(yaw, 360.0))) *
      QQuaternion::fromAxisAndAngle(right, float(std::remainder(pitch, 360.0)));
  view_.eye = view_.target + rotation.rotatedVector(eye - view_.target);
  view_.rotation = (rotation * view_.rotation).normalized();
  emit viewChanged(); requestFrame();
}
void DepthPointCloudViewport::look(double yaw, double pitch) {
  if (!std::isfinite(yaw) || !std::isfinite(pitch)) return;
  view_.eye = view_.eye.value_or(view_.target - view_.rotation.rotatedVector({0, 0, 1}) * view_.distance);
  const auto right = view_.rotation.rotatedVector({1, 0, 0}), down = view_.rotation.rotatedVector({0, 1, 0});
  view_.rotation = (QQuaternion::fromAxisAndAngle(-down, float(std::remainder(yaw, 360.0))) *
      QQuaternion::fromAxisAndAngle(right, float(std::remainder(pitch, 360.0))) * view_.rotation).normalized();
  emit viewChanged(); requestFrame();
}
void DepthPointCloudViewport::roll(double degrees) {
  if (!std::isfinite(degrees)) return;
  view_.rotation = (QQuaternion::fromAxisAndAngle(view_.rotation.rotatedVector({0, 0, 1}),
      float(std::remainder(degrees, 360.0))) * view_.rotation).normalized();
  emit viewChanged(); requestFrame();
}
void DepthPointCloudViewport::moveCamera(double right, double down, double forward) {
  if (!std::isfinite(right) || !std::isfinite(down) || !std::isfinite(forward)) return;
  const auto eye = view_.eye.value_or(view_.target - view_.rotation.rotatedVector({0, 0, 1}) * view_.distance);
  view_.eye = eye + view_.rotation.rotatedVector({float(right), float(down), float(forward)});
  emit viewChanged(); requestFrame();
}
void DepthPointCloudViewport::pan(double x, double y) {
  if (!std::isfinite(x) || !std::isfinite(y)) return;
  const auto eye = view_.eye.value_or(view_.target - view_.rotation.rotatedVector({0, 0, 1}) * view_.distance);
  const auto delta = view_.rotation.rotatedVector({float(x) * view_.distance * 2 * kTanHalfFov,
      float(y) * view_.distance * 2 * kTanHalfFov * float(height() / std::max(1.0, width())), 0});
  view_.target += delta; view_.eye = eye + delta;
  emit viewChanged(); requestFrame();
}
void DepthPointCloudViewport::dolly(double steps) {
  if (!std::isfinite(steps)) return;
  const auto eye = view_.eye.value_or(view_.target - view_.rotation.rotatedVector({0, 0, 1}) * view_.distance);
  const auto next = std::clamp(view_.distance * std::exp(float(-steps) * .08F), .05F, 20.F);
  view_.eye = eye + view_.rotation.rotatedVector({0, 0, view_.distance - next}); view_.distance = next;
  emit viewChanged(); requestFrame();
}
void DepthPointCloudViewport::resetView() { view_ = {}; emit viewChanged(); requestFrame(); }
void DepthPointCloudViewport::resetCamera() {
  view_.eye.reset(); view_.rotation = {}; view_.target = {0, 0, 1}; view_.distance = 1;
  emit viewChanged(); requestFrame();
}
QVariantMap DepthPointCloudViewport::orbitAroundAt(double x, double y) {
  if (depth_preview_ || !cached_ || image_.isNull() || width() <= 0 || height() <= 0) return {{"ok", false}, {"error", "Wait for the point-cloud frame."}};
  const auto& camera = displayed_view_;
  const auto forward = camera.rotation.rotatedVector({0, 0, 1});
  const auto eye = camera.eye.value_or(camera.target - forward * camera.distance);
  const auto right = camera.rotation.rotatedVector({1, 0, 0}), down = camera.rotation.rotatedVector({0, 1, 0});
  const auto u = float(x / width() * image_.width()), v = float(y / height() * image_.height());
  const float focal = float(image_.width()) / (2 * kTanHalfFov);
  float nearest = std::numeric_limits<float>::infinity(), radius = 100 * float(image_.width() / width()) * float(image_.width() / width());
  QVector3D target;
  for (const auto& point : cached_->points) {
    auto p = point.position; p.setZ(1 + (p.z() - 1) * camera.z_scale);
    const auto offset = p - eye; const float z = QVector3D::dotProduct(offset, forward);
    if (z <= .01F) continue;
    const auto px = focal * QVector3D::dotProduct(offset, right) / z + float(image_.width() - 1) / 2;
    const auto py = focal * QVector3D::dotProduct(offset, down) / z + float(image_.height() - 1) / 2;
    const auto distance = (px - u) * (px - u) + (py - v) * (py - v);
    if (distance <= radius && z < nearest) { nearest = z; target = p; }
  }
  if (!std::isfinite(nearest)) return {{"ok", false}, {"error", "Choose a visible point on the cloud."}};
  // Keep the presented camera exactly where it was; only replace the pivot.
  view_.eye = eye; view_.rotation = camera.rotation; view_.target = target; view_.distance = nearest;
  emit viewChanged(); requestFrame(); return {{"ok", true}};
}
void DepthPointCloudViewport::requestFrame() {
  requested_index_ = counts_.empty() ? -1 : std::min(int(counts_.size()) - 1, int(position_ * fps_));
  ++revision_; if (!rendering_) submitFrame();
}
void DepthPointCloudViewport::submitFrame() {
  if (rendering_ || counts_.empty() || !isVisible() || width() < 2 || height() < 2) return;
  const int index = std::min(int(counts_.size()) - 1, int(position_ * fps_));
  QSize size(std::max(2, int(width())), std::max(2, int(height()))); size.scale(1200, 1200, Qt::KeepAspectRatio);
  const auto directory = QDir(project_).filePath("pointcloud");
  auto view = view_;
  const auto pass = color_pass_;
  if (pass == "source" && !capture_layers_) { view.color_mode = 0; view.brightness = 1; }
  const auto cached = cached_; const int count = counts_[static_cast<size_t>(index)];
  submitted_ = revision_; rendering_ = true;
  submitted_view_ = view;
  worker_.setProperty("epoch", epoch_); worker_.setProperty("seekEpoch", seek_epoch_);
  const bool depth = depth_preview_;
  const bool layers = capture_layers_ || pass == "depth";
  const auto depth_path = QDir(project_).filePath(QStringLiteral("depth-video/%1.png").arg(index, 6, 10, QLatin1Char('0')));
  const auto source_path = QDir(project_).filePath(QStringLiteral("source-video/%1.png").arg(index, 6, 10, QLatin1Char('0')));
  worker_.setFuture(QtConcurrent::run([directory, index, count, size, view, cached, depth, depth_path, source_path, layers, pass] {
    if (layers) {
      auto result = render_depth_cloud(directory, index, count, size, view, cached);
      if (!result.error.isEmpty()) return result;
      // The gradient deliverable is independent of the selected preview mode.
      // Otherwise Source color produces two source-colored point-cloud files.
      auto gradient_view = view; gradient_view.color_mode = 2;
      result.layers.insert("point-cloud", render_depth_cloud(directory, index, count, size, gradient_view, result.cloud).image);
      auto color_view = view; color_view.color_mode = 0; color_view.brightness = 1;
      result.layers.insert("source-color-points", render_depth_cloud(directory, index, count, size, color_view, result.cloud).image);
      const QImage depth_image(depth_path);
      // Points retain the worker's row-major sampled source grid. Color that
      // exact geometry with the original depth samples, even at zero Z strength.
      QFile manifest(QDir(directory).filePath("manifest.json"));
      if (!manifest.open(QIODevice::ReadOnly)) { result.error = "The point-cloud sampling metadata is missing."; return result; }
      const int step = QJsonDocument::fromJson(manifest.readAll()).object().value("sampleStep").toInt();
      auto depth_cloud = std::make_shared<DepthCloudFrame>(*result.cloud);
      size_t point = 0;
      if (step <= 0 || depth_image.isNull()) { result.error = "The depth recording layer is missing."; return result; }
      for (int y = 0; y < depth_image.height(); y += step) for (int x = 0; x < depth_image.width(); x += step) {
        if (point >= depth_cloud->points.size()) { result.error = "The depth recording layer does not match the point cloud."; return result; }
        depth_cloud->points[point++].color = depth_image.pixel(x, y);
      }
      if (point != depth_cloud->points.size()) { result.error = "The depth recording layer is incomplete."; return result; }
      result.layers.insert("depth-points", render_depth_cloud(directory, index, count, size, color_view, depth_cloud).image);
      result.layers.insert("depth", depth_image);
      result.layers.insert("source-color", QImage(source_path));
      for (const auto& image : std::as_const(result.layers)) if (image.isNull()) {
        result.error = "Prepare the source and depth video layers before recording."; return result;
      }
      if (pass == "depth") result.image = result.layers.value("depth-points");
      if (pass == "source") result.image = result.layers.value("source-color-points");
      if (depth) result.image = result.layers.value("depth").scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      return result;
    }
    if (!depth) return render_depth_cloud(directory, index, count, size, view, cached);
    DepthCloudRender result; result.index = index; result.cloud = cached;
    result.image = QImage(depth_path).scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (result.image.isNull()) result.error = "Prepare this scene's depth video before playing it.";
    return result;
  }));
}
void DepthPointCloudViewport::geometryChange(const QRectF& next, const QRectF& previous) {
  QQuickItem::geometryChange(next, previous); if (next.size() != previous.size()) requestFrame();
}
QSGNode* DepthPointCloudViewport::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
  if (image_.isNull() || !window()) { delete old; return nullptr; }
  auto* node = static_cast<CloudTextureNode*>(old); if (!node) node = new CloudTextureNode;
  if (node->publication != publication_) {
    auto* texture = window()->createTextureFromImage(image_);
    auto* previous = node->texture(); node->setOwnsTexture(false); node->setTexture(texture); delete previous;
    node->publication = publication_;
  }
  QSizeF size = image_.size(); size.scale(boundingRect().size(), Qt::KeepAspectRatio);
  node->setRect(QRectF((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height()));
  node->setFiltering(QSGTexture::Linear); return node;
}
}  // namespace photo_editor::desktop
