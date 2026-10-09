#include "tablet_input_area.h"

#include <QEvent>
#include <QPointingDevice>
#include <QTabletEvent>

#include <algorithm>

namespace photo_editor::desktop {

namespace {

double normalized_tilt(double tilt_degrees) {
  return std::clamp(tilt_degrees / 60.0, -1.0, 1.0);
}

bool is_eraser_event(const QTabletEvent& event) {
  return event.pointerType() == QPointingDevice::PointerType::Eraser;
}

}  // namespace

TabletInputArea::TabletInputArea(QQuickItem* parent)
    : QQuickItem(parent) {
  // This item handles tablet events, not mouse hover. Accepting hover here
  // blocks the MouseArea underneath from updating its brush outline.
  setAcceptHoverEvents(false);
}

bool TabletInputArea::event(QEvent* generic_event) {
  if (generic_event == nullptr) {
    return QQuickItem::event(generic_event);
  }
  if (generic_event->type() != QEvent::TabletPress &&
      generic_event->type() != QEvent::TabletMove &&
      generic_event->type() != QEvent::TabletRelease) {
    return QQuickItem::event(generic_event);
  }

  auto* event = static_cast<QTabletEvent*>(generic_event);
  if (event == nullptr || !isEnabled()) {
    return QQuickItem::event(generic_event);
  }

  const auto position = event->position();
  const auto pressure = std::clamp(event->pressure(), 0.0, 1.0);
  const auto tilt_x = normalized_tilt(event->xTilt());
  const auto tilt_y = normalized_tilt(event->yTilt());
  const auto eraser = is_eraser_event(*event);

  switch (event->type()) {
    case QEvent::TabletPress:
      emit tabletPressed(position.x(), position.y(), pressure, tilt_x, tilt_y, eraser);
      event->accept();
      return true;
    case QEvent::TabletMove:
      emit tabletMoved(position.x(), position.y(), pressure, tilt_x, tilt_y, eraser);
      event->accept();
      return true;
    case QEvent::TabletRelease:
      emit tabletReleased(position.x(), position.y(), pressure, tilt_x, tilt_y, eraser);
      event->accept();
      return true;
    default:
      return QQuickItem::event(generic_event);
  }
}

}  // namespace photo_editor::desktop
