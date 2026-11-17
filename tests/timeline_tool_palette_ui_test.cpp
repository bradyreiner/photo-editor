#include <QApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QSet>
#include <QTest>
#include <QUrl>
#include <QVector>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QObject* find_object_recursive(
    QObject* root,
    const QString& object_name,
    QSet<const QObject*>& visited) {
  if (root == nullptr || visited.contains(root)) return nullptr;
  visited.insert(root);
  if (root->objectName() == object_name) return root;
  if (auto* item = qobject_cast<QQuickItem*>(root)) {
    for (auto* child_item : item->childItems()) {
      if (auto* found = find_object_recursive(child_item, object_name, visited))
        return found;
    }
  }
  if (auto* window = qobject_cast<QQuickWindow*>(root)) {
    if (auto* found = find_object_recursive(window->contentItem(), object_name, visited))
      return found;
  }
  for (auto* child : root->children()) {
    if (auto* found = find_object_recursive(child, object_name, visited)) return found;
  }
  return nullptr;
}

QObject* find_object(QObject* root, const QString& object_name) {
  QSet<const QObject*> visited;
  return find_object_recursive(root, object_name, visited);
}

QQuickItem* find_text_item_recursive(
    QObject* root,
    QObject* original_root,
    const QString& expected_text,
    QSet<const QObject*>& visited) {
  if (root == nullptr || visited.contains(root)) return nullptr;
  visited.insert(root);
  if (root != original_root) {
    if (auto* item = qobject_cast<QQuickItem*>(root);
        item != nullptr && item->property("text").toString() == expected_text) {
      return item;
    }
  }
  if (auto* item = qobject_cast<QQuickItem*>(root)) {
    for (auto* child_item : item->childItems()) {
      if (auto* found = find_text_item_recursive(
              child_item, original_root, expected_text, visited))
        return found;
    }
  }
  for (auto* child : root->children()) {
    if (auto* found = find_text_item_recursive(
            child, original_root, expected_text, visited))
      return found;
  }
  return nullptr;
}

QQuickItem* find_text_item(QObject* root, const QString& expected_text) {
  QSet<const QObject*> visited;
  return find_text_item_recursive(root, root, expected_text, visited);
}

QString expected_palette_label(const QVariantMap& entry) {
  const auto label = entry.value(QStringLiteral("label")).toString();
  const auto kind = entry.value(QStringLiteral("kind")).toString();
  const auto shortcut = entry.value(QStringLiteral("shortcut")).toString();
  if (kind == QStringLiteral("tool") && shortcut.size() > 0 && shortcut.size() <= 3)
    return label + QStringLiteral(" · ") + shortcut;
  return label;
}

bool button_text_is_fully_visible(
    QQuickItem* button, QQuickItem* grid, const QString& expected_text) {
  if (button == nullptr || grid == nullptr || !button->isVisible()) return false;
  const auto button_rect = button->mapRectToItem(
      grid, QRectF(0.0, 0.0, button->width(), button->height()));
  if (button_rect.left() < -1.0 || button_rect.top() < -1.0 ||
      button_rect.right() > grid->width() + 1.0 ||
      button_rect.bottom() > grid->height() + 1.0 ||
      button->property("text").toString() != expected_text) {
    return false;
  }
  auto* label = find_text_item(button, expected_text);
  if (label == nullptr || !label->isVisible()) return false;
  const auto label_rect = label->mapRectToItem(
      button, QRectF(0.0, 0.0, label->width(), label->height()));
  return label_rect.left() >= -1.0 && label_rect.top() >= -1.0 &&
         label_rect.right() <= button->width() + 1.0 &&
         label_rect.bottom() <= button->height() + 1.0;
}

int visible_row_count(
    QQuickItem* grid, const QVector<QQuickItem*>& buttons) {
  if (grid == nullptr) return 0;
  QVector<qreal> row_positions;
  for (auto* button : buttons) {
    if (button == nullptr || !button->isVisible()) continue;
    const auto y = button->mapToItem(grid, QPointF(0.0, 0.0)).y();
    const auto existing = std::find_if(row_positions.cbegin(), row_positions.cend(),
        [y](qreal row_y) { return qAbs(row_y - y) < 2.0; });
    if (existing == row_positions.cend()) row_positions.push_back(y);
  }
  return static_cast<int>(row_positions.size());
}

bool click_item(QQuickWindow* window, QObject* object) {
  auto* item = qobject_cast<QQuickItem*>(object);
  if (window == nullptr || item == nullptr || !item->isVisible() ||
      !item->property("enabled").toBool()) {
    return false;
  }
  const auto position = item->mapToScene(
      QPointF(item->width() / 2.0, item->height() / 2.0)).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, position);
  QTest::qWait(30);
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  qputenv("QT_QUICK_CONTROLS_STYLE", QByteArrayLiteral("Basic"));
  QApplication app(argc, argv);

  QQmlEngine engine;
  QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/tests/timeline-tool-palette-fixture.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("The production Timeline tool palette should instantiate.");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* palette = find_object(fixture.get(), QStringLiteral("timelineToolPalette"));
  auto* grid = find_object(fixture.get(), QStringLiteral("timelineToolGrid"));
  auto* grid_item = qobject_cast<QQuickItem*>(grid);
  if (window == nullptr || palette == nullptr || grid_item == nullptr) {
    return fail("The palette should expose its window, responsive grid, and active state.");
  }
  window->show();
  window->setWidth(500);
  window->setHeight(900);
  QTest::qWait(100);

  if (!palette->property("vertical").toBool() || grid_item->width() <= 0.0) {
    return fail("The compact palette should switch to its visible vertical layout.");
  }
  if (palette->property("toolCount").toInt() != 15 ||
      !palette->property("compact").toBool()) {
    return fail("Every direct tool, both Track Select directions, Text, and Shape should be present.");
  }
  const auto entries = palette->property("entries").toList();
  for (const auto& entry_value : entries) {
    const auto entry = entry_value.toMap();
    const auto object_name = QStringLiteral("timelineTool_") +
                             entry.value(QStringLiteral("key")).toString();
    auto* button = find_object(fixture.get(), object_name);
    auto* item = qobject_cast<QQuickItem*>(button);
    const auto expected_text = expected_palette_label(entry);
    if (item == nullptr || !item->isVisible() || item->height() > 40.0 ||
        !button_text_is_fully_visible(item, grid_item, expected_text)) {
      std::cerr << "Palette entry "
                << entry.value(QStringLiteral("key")).toString().toStdString()
                << " found=" << (button != nullptr)
                << " visible=" << (item != nullptr && item->isVisible())
                << " height=" << (item != nullptr ? item->height() : -1.0)
                << " text=" << (item != nullptr ? item->property("text").toString().toStdString() : "<missing>")
                << " expected=" << expected_text.toStdString() << '\n';
      return fail("Every palette tool should show its complete label within visible button bounds.");
    }
  }

  for (const auto& entry_value : entries) {
    const auto entry = entry_value.toMap();
    const auto key = entry.value(QStringLiteral("key")).toString();
    const auto kind = entry.value(QStringLiteral("kind")).toString();
    const auto object_name = QStringLiteral("timelineTool_") + key;
    auto* button = find_object(fixture.get(), object_name);
    if (!click_item(window, button)) {
      return fail("Every direct tool and creation action should respond to a real pointer click.");
    }
    if (kind == QStringLiteral("create")) {
      if (fixture->property("lastCreatedKind").toString() != key) {
        return fail("Text and Shape pointer clicks should request the matching clip type.");
      }
      continue;
    }
    const auto expected_tool = kind == QStringLiteral("direction")
                                   ? QStringLiteral("track-select")
                                   : key;
    if (fixture->property("lastTool").toString() != expected_tool) {
      return fail("Every direct tool pointer click should request its canonical tool id.");
    }
    if (kind == QStringLiteral("direction") &&
        palette->property("trackSelectionDirection").toInt() !=
            entry.value(QStringLiteral("direction")).toInt()) {
      return fail("Track Select pointer clicks should retain their forward/backward direction.");
    }
  }

  auto* rate_button = find_object(fixture.get(), QStringLiteral("timelineTool_rate-stretch"));
  if (!rate_button || !click_item(window, rate_button) ||
      fixture->property("lastTool").toString() != QStringLiteral("rate-stretch")) {
    return fail("The Rate Stretch control should activate its real timeline tool id.");
  }
  palette->setProperty("activeTool", QStringLiteral("rate-stretch"));
  if (!rate_button->property("active").toBool()) {
    return fail("The palette should render the current tool as active.");
  }

  palette->setProperty("rateStretchEnabled", false);
  palette->setProperty("rateStretchDisabledReason",
                       QStringLiteral("This clip has a retime curve."));
  if (rate_button->property("enabled").toBool() ||
      !rate_button->property("tooltipText").toString().contains(
          QStringLiteral("retime curve"))) {
    return fail("An unavailable Rate Stretch tool should expose its concrete disabled reason.");
  }
  palette->setProperty("rateStretchEnabled", true);
  fixture->setProperty("shortcutOverrides", QVariantMap{
      {QStringLiteral("timeline-rate-stretch"), QStringLiteral("Control+Option+Shift+R")},
      {QStringLiteral("timeline-track-select-backward"), QStringLiteral("Control+Option+A")}});
  QCoreApplication::processEvents();
  auto* backward_button = find_object(fixture.get(), QStringLiteral("timelineTool_track-select-backward"));
  if (rate_button->property("text").toString() != QStringLiteral("Rate Stretch") ||
      !rate_button->property("tooltipText").toString().contains(
          QStringLiteral("Control+Option+Shift+R")) ||
      !backward_button ||
      backward_button->property("text").toString() != QStringLiteral("Track Select ←") ||
      !backward_button->property("tooltipText").toString().contains(
          QStringLiteral("Control+Option+A"))) {
    return fail("Long configured shortcuts should stay in tooltips without expanding tool labels.");
  }
  fixture->setProperty("shortcutOverrides", QVariantMap{});
  QCoreApplication::processEvents();
  palette->setProperty("rateStretchEnabled", false);

  auto* forward_button = find_object(fixture.get(), QStringLiteral("timelineTool_track-select-forward"));
  backward_button = find_object(fixture.get(), QStringLiteral("timelineTool_track-select-backward"));
  if (!forward_button || !backward_button || !click_item(window, forward_button) ||
      fixture->property("lastTool").toString() != QStringLiteral("track-select") ||
      palette->property("trackSelectionDirection").toInt() != 1 ||
      !click_item(window, backward_button) ||
      palette->property("trackSelectionDirection").toInt() != -1) {
    return fail("Forward and backward Track Select controls should set the pointer operation direction.");
  }

  auto* text_button = find_object(fixture.get(), QStringLiteral("timelineTool_text"));
  auto* shape_button = find_object(fixture.get(), QStringLiteral("timelineTool_shape"));
  if (!text_button || !shape_button || !click_item(window, text_button) ||
      fixture->property("lastCreatedKind").toString() != QStringLiteral("text") ||
      !click_item(window, shape_button) ||
      fixture->property("lastCreatedKind").toString() != QStringLiteral("shape")) {
    return fail("Text and Shape palette buttons should emit their creation requests.");
  }

  window->setWidth(1240);
  window->setHeight(170);
  palette->setProperty("rateStretchEnabled", true);
  QTest::qWait(80);
  if (palette->property("vertical").toBool())
    return fail("The wide palette should return to a compact wrapping layout.");
  QVector<QQuickItem*> wide_buttons;
  for (const auto& entry_value : entries) {
    const auto entry = entry_value.toMap();
    auto* item = qobject_cast<QQuickItem*>(find_object(
        fixture.get(), QStringLiteral("timelineTool_") +
                           entry.value(QStringLiteral("key")).toString()));
    if (entry.value("kind").toString() == "create") {
      if (item != nullptr && item->isVisible())
        return fail("Creation actions belong in Actions when the edit toolbar is horizontal.");
      continue;
    }
    if (item == nullptr || !item->isVisible() ||
        item->property("showText").toBool() ||
        item->width() != item->height() ||
        !item->property("tooltipText").toString().contains(entry.value("label").toString())) {
      std::cerr << "Wide palette label clipped: "
                << entry.value(QStringLiteral("key")).toString().toStdString()
                << " bounds=" << (item != nullptr ? item->x() : -1.0) << ','
                << (item != nullptr ? item->y() : -1.0) << ' '
                << (item != nullptr ? item->width() : -1.0) << 'x'
                << (item != nullptr ? item->height() : -1.0) << '\n';
      return fail("Horizontal tools must be square icon buttons with their names in tooltips.");
    }
    const auto rect = item->mapRectToItem(grid_item, QRectF(0, 0, item->width(), item->height()));
    if (rect.left() < 0 || rect.right() > grid_item->width() + 1 ||
        rect.top() < 0 || rect.bottom() > grid_item->height() + 1 || !click_item(window, item))
      return fail("Every horizontal icon tool must fit and respond to a real pointer click.");
    wide_buttons.push_back(item);
  }
  const auto row_count = visible_row_count(grid_item, wide_buttons);
  if (row_count != 1) {
    std::cerr << "Wide palette rows=" << row_count << " width=" << grid_item->width()
              << " height=" << grid_item->height() << '\n';
    return fail("The horizontal tool palette must fit in one compact icon row.");
  }
  palette->setProperty("condensed", true);
  palette->setProperty("activeTool", QStringLiteral("slip"));
  QTest::qWait(50);
  auto* overflow = find_object(fixture.get(), QStringLiteral("timelineMoreToolsButton"));
  if (palette->property("visibleEntries").toList().size() != 6 ||
      !overflow || !overflow->property("visible").toBool() ||
      !overflow->property("active").toBool() || overflow->property("iconName").toString() != "slip")
    return fail("Condensed tools must retain primary tools and identify the active precision tool in the overflow trigger");
  if (!click_item(window, overflow)) return fail("The precision-tools overflow must accept a pointer click");
  QTest::qWait(80);
  auto* popup = find_object(fixture.get(), QStringLiteral("timelineMoreToolsButtonPopup"));
  if (!QTest::qWaitFor([&] { return popup && popup->property("opened").toBool(); }, 1500)) {
    auto* trigger = qobject_cast<QQuickItem*>(overflow);
    std::cerr << "Precision popup found=" << (popup != nullptr)
              << " visible=" << (popup && popup->property("visible").toBool())
              << " trigger=" << (trigger ? trigger->width() : -1) << 'x' << (trigger ? trigger->height() : -1) << '\n';
    return fail("Condensed tools must expose an actual keyboard-navigable precision menu");
  }
  QTest::keyClick(window, Qt::Key_End);
  QTest::keyClick(window, Qt::Key_Return);
  if (!QTest::qWaitFor([&] { return fixture->property("lastTool").toString() == "rate-stretch"; }, 1000))
    return fail("The condensed precision menu must activate Rate Stretch through keyboard navigation");
  return EXIT_SUCCESS;
}
