#include "qml_popup_wait.h"
#include <QAccessible>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <iostream>
#include <memory>

namespace {
void settle(QObject* popup = nullptr) {
  for (auto index = 0; index < 12; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  wait_for_popup_transition(popup);
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

QQuickItem* find_visual_child(QQuickItem* parent, const QString& object_name) {
  if (parent == nullptr)
    return nullptr;
  for (auto* child : parent->childItems()) {
    if (child->objectName() == object_name)
      return child;
    if (auto* nested = find_visual_child(child, object_name))
      return nested;
  }
  return nullptr;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);

  QQmlEngine engine;
  QQmlComponent component(
      &engine,
      QUrl::fromLocalFile(QStringLiteral(
          PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/CopyPhotoEditsDialog.qml")));
  std::unique_ptr<QObject> dialog(component.create());
  if (!dialog) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("CopyPhotoEditsDialog should instantiate");
  }

  QQuickWindow window;
  window.resize(700, 620);
  dialog->setProperty("parent", QVariant::fromValue(window.contentItem()));
  dialog->setProperty("sourceLabel", QStringLiteral("Selected portrait"));
  auto* opener = new QQuickItem(window.contentItem());
  opener->setObjectName(QStringLiteral("copyDialogOpener"));
  window.show();
  opener->forceActiveFocus(Qt::TabFocusReason);
  settle(dialog.get());
  if (window.activeFocusItem() != opener)
    return fail("Copy Edits test opener should own focus before opening the dialog");

  if (!QMetaObject::invokeMethod(dialog.get(), "open", Qt::DirectConnection))
    return fail("Copy Edits dialog should expose its open operation");
  settle(dialog.get());
  if (!dialog->property("visible").toBool())
    return fail("Copy Edits dialog should open");

  auto* dialog_content = qvariant_cast<QQuickItem*>(dialog->property("contentItem"));
  auto* dialog_background = qvariant_cast<QQuickItem*>(dialog->property("background"));
  auto* dialog_accessible = QAccessible::queryAccessibleInterface(dialog_background);
  auto* first_group = find_visual_child(dialog_content, QStringLiteral("copyPhotoEditsGroup_tone"));
  auto* first_group_accessible = QAccessible::queryAccessibleInterface(first_group);
  if (dialog_accessible == nullptr || dialog_accessible->role() != QAccessible::Dialog ||
      dialog_accessible->text(QAccessible::Name) != QStringLiteral("Copy Edits") ||
      first_group == nullptr || window.activeFocusItem() != first_group ||
      !first_group->property("visualFocus").toBool() ||
      first_group_accessible == nullptr || first_group_accessible->role() != QAccessible::CheckBox ||
      first_group_accessible->text(QAccessible::Name) != QStringLiteral("Light") ||
      first_group_accessible->text(QAccessible::Description) !=
          QStringLiteral("Exposure, contrast, levels and curves") ||
      !first_group_accessible->state().checked) {
    std::cerr << "Copy Edits group: found=" << (first_group != nullptr)
              << " focused=" << (first_group != nullptr && window.activeFocusItem() == first_group)
              << " active=" << (first_group != nullptr && first_group->hasActiveFocus())
              << " role=" << (first_group_accessible != nullptr ? first_group_accessible->role() : QAccessible::NoRole)
              << " name=" << (first_group_accessible != nullptr ? first_group_accessible->text(QAccessible::Name).toStdString() : "")
              << " description=" << (first_group_accessible != nullptr ? first_group_accessible->text(QAccessible::Description).toStdString() : "")
              << " checked=" << (first_group_accessible != nullptr && first_group_accessible->state().checked)
              << " focusItem=" << (window.activeFocusItem() == nullptr ? "" : window.activeFocusItem()->objectName().toStdString())
              << '\n';
    return fail("Copy Edits should show focus on its initially selected Light group and expose its name, value, and description");
  }

  auto* group_content = qvariant_cast<QQuickItem*>(first_group->property("contentItem"));
  if (!group_content || !first_group->boundingRect().contains(
          group_content->mapRectToItem(first_group, group_content->boundingRect())))
    return fail("Copy Edits group focus target should contain both its name and description");
  for (auto* label : group_content->childItems()) {
    if (label->isVisible() && !first_group->boundingRect().contains(
            label->mapRectToItem(first_group, label->boundingRect()))) {
      std::cerr << "Group height=" << first_group->height()
                << " implicit=" << first_group->implicitHeight()
                << " content implicit=" << group_content->implicitHeight() << '\n';
      return fail("Copy Edits focus outline should contain its rendered labels");
    }
  }

  const auto output = qEnvironmentVariable("PHOTO_EDITOR_TEST_COPY_EDITS_IMAGE");
  if (!output.isEmpty() && !window.grabWindow().save(output))
    return fail("Copy Edits review capture should save when requested");

  QKeyEvent escape_press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &escape_press);
  QKeyEvent escape_release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &escape_release);
  settle(dialog.get());
  if (dialog->property("visible").toBool() || window.activeFocusItem() != opener)
    return fail("Escape should cancel Copy Edits and return focus to its invoking control");

  dialog->setProperty("focusFallbackItem", QVariant::fromValue(window.contentItem()));
  if (!QMetaObject::invokeMethod(dialog.get(), "open", Qt::DirectConnection))
    return fail("Copy Edits should reopen for fallback-focus verification");
  settle(dialog.get());
  if (!dialog->property("visible").toBool() || window.activeFocusItem() != first_group)
    return fail("Copy Edits should restore initial group focus when reopened");
  QPointer<QQuickItem> opener_guard(opener);
  opener->deleteLater();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  settle(dialog.get());
  if (!opener_guard.isNull())
    return fail("Copy Edits test opener should be removable while the dialog is open");
  QKeyEvent fallback_escape_press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &fallback_escape_press);
  QKeyEvent fallback_escape_release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &fallback_escape_release);
  settle(dialog.get());
  if (dialog->property("visible").toBool() || window.activeFocusItem() != window.contentItem()) {
    const auto* active_focus = window.activeFocusItem();
    auto* configured_fallback = qvariant_cast<QQuickItem*>(dialog->property("focusFallbackItem"));
    std::cerr << "Copy Edits fallback diagnostic: visible=" << dialog->property("visible").toBool()
              << " active=" << (active_focus ? active_focus->objectName().toStdString() : "<null>")
              << " fallback=" << (configured_fallback ? configured_fallback->objectName().toStdString() : "<null>")
              << " fallbackIsContent=" << (configured_fallback == window.contentItem())
              << " fallbackVisible=" << (configured_fallback && configured_fallback->isVisible())
              << " fallbackEnabled=" << (configured_fallback && configured_fallback->isEnabled())
              << '\n';
    return fail("Copy Edits should return focus to the window when its invoking control is no longer visible");
  }

  std::cout << "Copy Edits dialog semantics, initial group focus, checkbox state, Escape dismissal, and focus fallback passed\n";
  return 0;
}
