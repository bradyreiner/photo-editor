#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>
#include <QThread>
#include <QVariant>

// Focus-entry and focus-return assertions must observe the completed popup
// lifecycle, independently of the production transition's duration.
inline void wait_for_popup_transition(QObject* popup) {
  if (!popup) return;
  QElapsedTimer elapsed;
  elapsed.start();
  while (popup->property("visible").toBool() &&
         !popup->property("opened").toBool() && elapsed.elapsed() < 1000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  QThread::msleep(5);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}
