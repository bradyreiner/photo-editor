#pragma once
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QMap>
#include <QQuaternion>
#include <QQuickItem>
#include <QTimer>
#include <QVariantMap>
#include <QVector3D>
#include <memory>
#include <optional>
#include <vector>
#include <QtQml/qqmlregistration.h>

namespace photo_editor::desktop {
struct DepthCloudPoint { QVector3D position; QRgb color = 0; };
struct DepthCloudFrame { std::vector<DepthCloudPoint> points; int index = -1; };
struct DepthCloudView {
  QQuaternion rotation;
  QVector3D target{0, 0, 1};
  std::optional<QVector3D> eye;
  float distance = 1;
  int point_size = 1;
  float z_scale = 1;
  float density = 1, brightness = 1;
  int shape = 0, color_mode = 0;
  QRgb gradient_near = qRgb(0, 0, 255), gradient_mid = qRgb(0, 255, 0), gradient_far = qRgb(255, 0, 0);
  QString gradient_name = "Spectrum";
  QRgb background = qRgb(0, 0, 0);
};
struct DepthCloudRender {
  std::shared_ptr<const DepthCloudFrame> cloud;
  QImage image;
  QMap<QString, QImage> layers;
  QString error;
  int index = -1;
  qint64 milliseconds = 0;
};
// Pure worker functions, shared with geometry/pixel qualification.
DepthCloudRender render_depth_cloud(const QString& directory, int frame, int count,
    QSize size, const DepthCloudView& view, std::shared_ptr<const DepthCloudFrame> cached = {});

class DepthPointCloudViewport : public QQuickItem {
  Q_OBJECT
  QML_NAMED_ELEMENT(DepthPointCloudViewport)
  Q_PROPERTY(QString project READ project WRITE setProject NOTIFY projectChanged)
  Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool depthPreview READ depthPreview WRITE setDepthPreview NOTIFY depthPreviewChanged)
  Q_PROPERTY(QString colorPass READ colorPass WRITE setColorPass NOTIFY statusChanged)
  Q_PROPERTY(bool captureLayers READ captureLayers WRITE setCaptureLayers NOTIFY statusChanged)
  Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
  Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
  Q_PROPERTY(double duration READ duration NOTIFY statusChanged)
  Q_PROPERTY(int pointSize READ pointSize WRITE setPointSize NOTIFY viewChanged)
  Q_PROPERTY(double zExaggeration READ zExaggeration WRITE setZExaggeration NOTIFY viewChanged)
  Q_PROPERTY(QVariantMap viewState READ viewState NOTIFY viewChanged)
  Q_PROPERTY(QVariantMap appearance READ appearance WRITE setAppearance NOTIFY viewChanged)
 public:
  explicit DepthPointCloudViewport(QQuickItem* parent = nullptr);
  ~DepthPointCloudViewport() override;
  QString project() const { return project_; }
  void setProject(const QString& project);
  QVariantMap status() const;
  bool depthPreview() const { return depth_preview_; }
  void setDepthPreview(bool depth);
  QString colorPass() const { return color_pass_; }
  void setColorPass(const QString& pass);
  bool captureLayers() const { return capture_layers_; }
  void setCaptureLayers(bool enabled);
  Q_INVOKABLE QVariantMap recordingLayers() const;
  Q_INVOKABLE void refreshFrame() { requestFrame(); }
  bool playing() const { return playing_; }
  void setPlaying(bool playing);
  double position() const { return position_; }
  void setPosition(double seconds);
  double duration() const { return duration_; }
  int pointSize() const { return view_.point_size; }
  void setPointSize(int size);
  double zExaggeration() const { return view_.z_scale; }
  void setZExaggeration(double scale);
  QVariantMap viewState() const;
  QVariantMap appearance() const;
  void setAppearance(const QVariantMap& patch);
  Q_INVOKABLE void restoreView(const QVariantMap& state);
  Q_INVOKABLE void orbit(double yaw, double pitch);
  Q_INVOKABLE void look(double yaw, double pitch);
  Q_INVOKABLE void roll(double degrees);
  Q_INVOKABLE void moveCamera(double right, double down, double forward);
  Q_INVOKABLE QVariantMap orbitAroundAt(double x, double y);
  Q_INVOKABLE QVariantMap pickFocusAt(double x, double y) { return orbitAroundAt(x, y); }
  Q_INVOKABLE void resetCamera();
  Q_INVOKABLE void pan(double x, double y);
  Q_INVOKABLE void dolly(double steps);
  Q_INVOKABLE void resetView();
 signals:
  void projectChanged();
  void statusChanged();
  void depthPreviewChanged();
  void playingChanged();
  void positionChanged();
  void viewChanged();
 protected:
  QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*) override;
  void geometryChange(const QRectF&, const QRectF&) override;
 private:
  void requestFrame();
  void submitFrame();
  QString project_, error_;
  QString color_pass_ = "appearance";
  std::vector<int> counts_;
  QSize source_size_;
  int fps_ = 24, presented_index_ = -1, requested_index_ = -1;
  double duration_ = 0, position_ = 0, play_anchor_ = 0;
  bool depth_preview_ = false;
  bool capture_layers_ = false;
  bool playing_ = false, rendering_ = false;
  quint64 epoch_ = 0, seek_epoch_ = 0, revision_ = 0, submitted_ = 0, publication_ = 0;
  qint64 render_ms_ = 0;
  DepthCloudView view_;
  DepthCloudView submitted_view_, displayed_view_;
  std::shared_ptr<const DepthCloudFrame> cached_;
  QImage image_;
  QMap<QString, QImage> layers_;
  QTimer timer_;
  QElapsedTimer clock_;
  QFutureWatcher<DepthCloudRender> worker_;
};
}  // namespace photo_editor::desktop
