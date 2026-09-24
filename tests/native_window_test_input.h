#pragma once
#include <QPoint>
class QWindow;
// QPA input simulation, not OS input-to-photon evidence.
void click_native_test_window(QWindow* window, QPoint position);
void right_click_native_test_window(QWindow* window, QPoint position);
void escape_native_test_window(QWindow* window);

void key_native_test_window(QWindow* window, int key, int modifiers);

void modified_click_native_test_window(QWindow* window, QPoint position, int modifiers);
