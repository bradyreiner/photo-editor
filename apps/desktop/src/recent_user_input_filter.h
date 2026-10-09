#pragma once

#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QTimer>

namespace photo_editor::desktop {

class RecentUserInputFilter final : public QObject {
 public:
  explicit RecentUserInputFilter(QObject* root_window)
      : QObject(root_window), root_window_(root_window), idle_timer_(this) {
    idle_timer_.setSingleShot(true);
    idle_timer_.setInterval(1500);
    QObject::connect(&idle_timer_, &QTimer::timeout, this, [this]() {
      if (root_window_)
        root_window_->setProperty("recentUserInput", false);
    });
    if (root_window_) {
      root_window_->setProperty("recentUserInput", false);
      root_window_->installEventFilter(this);
    }
  }

  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == root_window_ && event && isUserInput(event->type())) {
      root_window_->setProperty("recentUserInput", true);
      idle_timer_.start();
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  static bool isUserInput(QEvent::Type type) {
    switch (type) {
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonRelease:
      case QEvent::MouseButtonDblClick:
      case QEvent::MouseMove:
      case QEvent::Wheel:
      case QEvent::KeyPress:
      case QEvent::KeyRelease:
      case QEvent::TouchBegin:
      case QEvent::TouchUpdate:
      case QEvent::TouchEnd:
      case QEvent::TabletPress:
      case QEvent::TabletMove:
      case QEvent::TabletRelease:
      case QEvent::NativeGesture:
        return true;
      default:
        return false;
    }
  }

  QPointer<QObject> root_window_;
  QTimer idle_timer_;
};

}  // namespace photo_editor::desktop
