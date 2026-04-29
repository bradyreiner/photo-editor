#include "brush_live_preview_item.h"

#include "editor_preview_controller.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <qsgtexture_platform.h>

namespace photo_editor::desktop {

BrushLivePreviewItem::BrushLivePreviewItem(QQuickItem* parent)
    : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
}

QObject* BrushLivePreviewItem::controller() const {
  return controller_;
}

void BrushLivePreviewItem::setController(QObject* controller) {
  auto* next_controller = qobject_cast<EditorPreviewController*>(controller);
  if (controller_ == next_controller) {
    return;
  }

  if (controller_ != nullptr) {
    disconnect(controller_, nullptr, this, nullptr);
  }
  controller_ = next_controller;
  if (controller_ != nullptr) {
    connect(
        controller_,
        &EditorPreviewController::activeBrushStrokeChanged,
        this,
        [this]() { syncFromController(); });
    connect(
        controller_,
        &QObject::destroyed,
        this,
        [this]() {
          controller_ = nullptr;
          syncFromController();
        });
  }
  syncFromController();
  emit controllerChanged();
}

QSGNode* BrushLivePreviewItem::updatePaintNode(
    QSGNode* old_node,
    UpdatePaintNodeData*) {
#if defined(QT_FEATURE_metal) && QT_CONFIG(metal)
  if (!native_texture_available_ || native_texture_handle_ == 0 ||
      !native_texture_size_.isValid() || window() == nullptr ||
      window()->rendererInterface()->graphicsApi() != QSGRendererInterface::Metal) {
    delete old_node;
    scene_graph_texture_handle_ = 0;
    scene_graph_texture_size_ = {};
    return nullptr;
  }

  auto* node = static_cast<QSGSimpleTextureNode*>(old_node);
  if (node == nullptr) {
    node = new QSGSimpleTextureNode;
    node->setOwnsTexture(true);
    node->setFiltering(QSGTexture::Linear);
  }

  if (node->texture() == nullptr ||
      scene_graph_texture_handle_ != native_texture_handle_ ||
      scene_graph_texture_size_ != native_texture_size_) {
    auto* texture = QNativeInterface::QSGMetalTexture::fromNative(
        reinterpret_cast<QT_OBJC_PROTOCOL(MTLTexture)>(static_cast<quintptr>(native_texture_handle_)),
        window(),
        native_texture_size_,
        QQuickWindow::TextureHasAlphaChannel);
    node->setTexture(texture);
    scene_graph_texture_handle_ = native_texture_handle_;
    scene_graph_texture_size_ = native_texture_size_;
  }

  node->setRect(boundingRect());
  node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
  return node;
#else
  Q_UNUSED(old_node);
  return nullptr;
#endif
}

void BrushLivePreviewItem::syncFromController() {
  const auto next_available =
      controller_ != nullptr && controller_->activeMaskLiveBrushPreviewNativeAvailable();
  const auto next_handle =
      next_available ? controller_->activeMaskLiveBrushPreviewNativeHandle() : 0;
  const auto next_size =
      next_available ? controller_->activeMaskLiveBrushPreviewNativeSize() : QSize{};
  const auto next_revision =
      next_available ? controller_->activeMaskLiveBrushPreviewRevision() : 0;

  if (native_texture_available_ == next_available &&
      native_texture_handle_ == next_handle &&
      native_texture_size_ == next_size &&
      native_texture_revision_ == next_revision) {
    return;
  }

  native_texture_available_ = next_available;
  native_texture_handle_ = next_handle;
  native_texture_size_ = next_size;
  native_texture_revision_ = next_revision;
  update();
}

}  // namespace photo_editor::desktop
