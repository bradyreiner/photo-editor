#pragma once

#include <QQuickItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace photo_editor::desktop {

// Density cells and vectors are scene-graph geometry, not Canvas image uploads.
class ScopePlotItem : public QQuickItem {
  Q_OBJECT
  QML_NAMED_ELEMENT(ScopePlotItem)
  QML_ADDED_IN_VERSION(1, 0)
  Q_PROPERTY(QVariantMap frame READ frame WRITE setFrame NOTIFY frameChanged)
  Q_PROPERTY(QVariantMap reference READ reference WRITE setReference NOTIFY referenceChanged)
  Q_PROPERTY(QVariantMap options READ options WRITE setOptions NOTIFY optionsChanged)
 public:
  explicit ScopePlotItem(QQuickItem* parent = nullptr);
  QVariantMap frame() const { return frame_; }
  QVariantMap reference() const { return reference_; }
  QVariantMap options() const { return options_; }
  void setFrame(const QVariantMap& value);
  void setReference(const QVariantMap& value);
  void setOptions(const QVariantMap& value);
 signals:
  void frameChanged();
  void referenceChanged();
  void optionsChanged();
 protected:
  QSGNode* updatePaintNode(QSGNode* node, UpdatePaintNodeData*) override;
 private:
  QVariantMap frame_, reference_, options_;
};

}  // namespace photo_editor::desktop
