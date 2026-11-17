#pragma once
#include <QPoint>
#include <QVariantMap>
class QWindow;
// Foreground the command-line test application before testing window shortcuts.
void activate_native_test_application();
void activate_native_test_window(QWindow* window);
// Independently inspect AppKit process/window state and CGWindow z-order overlap.
QVariantMap native_window_foreground_sample(QWindow* window);
// QPA input simulation, not OS input-to-photon evidence.
void click_native_test_window(QWindow* window, QPoint position);
void right_click_native_test_window(QWindow* window, QPoint position);
void escape_native_test_window(QWindow* window);

void key_native_test_window(QWindow* window, int key, int modifiers);

void modified_click_native_test_window(QWindow* window, QPoint position, int modifiers);
