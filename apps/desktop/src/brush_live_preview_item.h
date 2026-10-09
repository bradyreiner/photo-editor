#pragma once

#include "brush_live_preview_raster.h"

#include <atomic>

#include <QPointer>
#include <QQuickItem>
#include <QSize>

namespace photo_editor::desktop {

class EditorPreviewController;

class BrushLivePreviewItem : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)
  Q_PROPERTY(bool immediate READ immediate WRITE setImmediate NOTIFY immediateChanged)
  Q_PROPERTY(QColor tint READ tint WRITE setTint NOTIFY tintChanged)
  Q_PROPERTY(int completedPaintRevision READ completedPaintRevision NOTIFY framePainted)
  Q_PROPERTY(int paintedPointCount READ paintedPointCount NOTIFY framePainted)
  QML_NAMED_ELEMENT(BrushLivePreviewItem)
  QML_ADDED_IN_VERSION(1, 0)

 public:
  explicit BrushLivePreviewItem(QQuickItem* parent = nullptr);

  [[nodiscard]] QObject* controller() const;
  void setController(QObject* controller);

  [[nodiscard]] int completedPaintRevision() const { return completed_paint_revision_.load(); }
  [[nodiscard]] int paintedPointCount() const { return painted_point_count_.load(); }
  [[nodiscard]] bool immediate() const { return immediate_; }
  void setImmediate(bool value);
  [[nodiscard]] QColor tint() const { return tint_; }
  void setTint(const QColor& value);

 signals:
  void framePainted();
  void immediateChanged();
  void tintChanged();
  void controllerChanged();

 protected:
  QSGNode* updatePaintNode(QSGNode* old_node, UpdatePaintNodeData*) override;

 private:
  void syncFromController();
  std::atomic<int> completed_paint_revision_{0};
  std::atomic<int> painted_point_count_{0};
  bool immediate_ = false;
  QColor tint_ = QColor(94, 149, 211);
  BrushLivePreviewRaster immediate_raster_;
  qsizetype immediate_point_count_ = 0;
  int immediate_revision_ = 0;
  int texture_immediate_revision_ = -1;
  bool immediate_stroke_active_ = false;

  QPointer<EditorPreviewController> controller_;
  quint64 native_texture_handle_ = 0;
  QSize native_texture_size_;
  int native_texture_revision_ = 0;
  bool native_texture_available_ = false;
  quint64 scene_graph_texture_handle_ = 0;
  QSize scene_graph_texture_size_;
};

}  // namespace photo_editor::desktop
