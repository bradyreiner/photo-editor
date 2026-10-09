#include "brush_live_preview_item.h"

#include "editor_preview_controller.h"
#include "brush_input_schema.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <qsgtexture_platform.h>

namespace photo_editor::desktop {

BrushLivePreviewItem::BrushLivePreviewItem(QQuickItem* parent)
    : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  connect(this, &QQuickItem::widthChanged, this, [this]() { syncFromController(); });
  connect(this, &QQuickItem::heightChanged, this, [this]() { syncFromController(); });
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

void BrushLivePreviewItem::setImmediate(bool value) {
  if (immediate_ == value) return;
  immediate_ = value;
  syncFromController();
  emit immediateChanged();
}

void BrushLivePreviewItem::setTint(const QColor& value) {
  if (tint_ == value) return;
  tint_ = value;
  syncFromController();
  emit tintChanged();
}

QSGNode* BrushLivePreviewItem::updatePaintNode(
    QSGNode* old_node,
    UpdatePaintNodeData*) {
  if (immediate_) {
    if (immediate_raster_.image.isNull() || !window()) {
      delete old_node;
      texture_immediate_revision_ = -1;
      return nullptr;
    }
    auto* node = static_cast<QSGSimpleTextureNode*>(old_node);
    if (!node) {
      node = new QSGSimpleTextureNode;
      node->setOwnsTexture(true);
      node->setFiltering(QSGTexture::Linear);
      texture_immediate_revision_ = -1;
    }
    if (texture_immediate_revision_ != immediate_revision_) {
      auto* texture = window()->createTextureFromImage(immediate_raster_.image);
      if (!texture) { delete node; texture_immediate_revision_ = -1; return nullptr; }
      // The node owns its texture and releases the previous one on replacement.
      node->setTexture(texture);
      texture_immediate_revision_ = immediate_revision_;
      painted_point_count_.store(static_cast<int>(immediate_point_count_));
      ++completed_paint_revision_;
      emit framePainted();
    }
    node->setRect(boundingRect());
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    return node;
  }
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
  if (immediate_) {
    const auto painting = controller_ && controller_->brushStrokeActive();
    const auto values = controller_ ? controller_->activeMaskLiveBrushPoints() : QVariantList{};
    // A small, incremental raster gives immediate input feedback. The worker
    // replaces it with display-resolution coverage using this same rasterizer.
    const auto edge = std::max(width(), height());
    const auto scale = edge > 0.0 ? std::min(1.0, 512.0 / edge) : 1.0;
    const QSize size(std::max(2, qRound(width() * scale)), std::max(2, qRound(height() * scale)));
    if (values.isEmpty() || !controller_ || controller_->activeMaskBrushAutoMask()) {
      immediate_raster_ = {};
      immediate_point_count_ = 0;
    } else {
      if ((painting && !immediate_stroke_active_) || values.size() < immediate_point_count_ ||
          immediate_raster_.image.size() != size) {
        immediate_raster_ = {};
        immediate_point_count_ = 0;
      }
      QVector<BrushPreviewPoint> points;
      points.reserve(values.size() - immediate_point_count_);
      for (auto i = immediate_point_count_; i < values.size(); ++i)
        points.append(brush_preview_point_from_variant(values.at(i), 0.5, 0.5, 0.08, 1.0));
      immediate_raster_.append(size, points, controller_->activeBrushPreviewStyle());
      immediate_point_count_ = values.size();
    }
    immediate_stroke_active_ = painting;
    ++immediate_revision_;
    update();
    return;
  }
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
