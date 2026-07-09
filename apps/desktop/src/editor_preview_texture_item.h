#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QSize>

namespace photo_editor::desktop {

class EditorPreviewController;

class EditorPreviewTextureItem : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)
  Q_PROPERTY(bool crispPixels READ crispPixels WRITE setCrispPixels NOTIFY crispPixelsChanged)
  QML_NAMED_ELEMENT(EditorPreviewTextureItem)
  QML_ADDED_IN_VERSION(1, 0)

 public:
  explicit EditorPreviewTextureItem(QQuickItem* parent = nullptr);

  [[nodiscard]] QObject* controller() const;
  void setController(QObject* controller);
  [[nodiscard]] bool crispPixels() const;
  void setCrispPixels(bool crisp_pixels);

 signals:
  void controllerChanged();
  void crispPixelsChanged();

 protected:
  QSGNode* updatePaintNode(QSGNode* old_node, UpdatePaintNodeData*) override;

 private:
  void syncFromController();

  QPointer<EditorPreviewController> controller_;
  quint64 native_texture_handle_ = 0;
  QSize native_texture_size_;
  int native_texture_revision_ = 0;
  bool native_texture_available_ = false;
  bool crisp_pixels_ = false;
  quint64 scene_graph_texture_handle_ = 0;
  QSize scene_graph_texture_size_;
};

}  // namespace photo_editor::desktop
