#pragma once

#include <QQuickItem>

namespace photo_editor::desktop {

class TabletInputArea : public QQuickItem {
  Q_OBJECT
  QML_NAMED_ELEMENT(TabletInputArea)
  QML_ADDED_IN_VERSION(1, 0)

 public:
  explicit TabletInputArea(QQuickItem* parent = nullptr);

 signals:
  void tabletPressed(double x, double y, double pressure, double tiltX, double tiltY, bool eraser);
  void tabletMoved(double x, double y, double pressure, double tiltX, double tiltY, bool eraser);
  void tabletReleased(double x, double y, double pressure, double tiltX, double tiltY, bool eraser);

 protected:
  bool event(QEvent* event) override;
};

}  // namespace photo_editor::desktop
