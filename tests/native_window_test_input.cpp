#include "native_window_test_input.h"
#include <QtTest/qtestmouse.h>
#include <QtTest/qtestkeyboard.h>
#ifndef Q_OS_MACOS
void activate_native_test_application() {}
#endif
void click_native_test_window(QWindow* window, QPoint position) {
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, position);
}
void right_click_native_test_window(QWindow* window, QPoint position) {
  QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, position);
}
void escape_native_test_window(QWindow* window) { QTest::keyClick(window, Qt::Key_Escape); }

void key_native_test_window(QWindow* window, int key, int modifiers) {
  QTest::keyClick(window, static_cast<Qt::Key>(key), Qt::KeyboardModifiers(modifiers));
}

void modified_click_native_test_window(QWindow* window, QPoint position, int modifiers) {
  QTest::mouseClick(window, Qt::LeftButton, Qt::KeyboardModifiers(modifiers), position);
}
