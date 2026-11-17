#include <QApplication>
#include <QAccessible>
#include <QDir>
#include <QFont>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QSignalSpy>
#include <QSet>
#include <QTest>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
QString accessible_name(QObject* object) {
  auto* interface = object == nullptr ? nullptr : QAccessible::queryAccessibleInterface(object);
  return interface == nullptr ? QString() : interface->text(QAccessible::Name).trimmed();
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool close_popup_and_wait(QObject* popup) {
  if (popup == nullptr) return false;
  QSignalSpy closed(popup, SIGNAL(closed()));
  if (!closed.isValid()) return false;
  popup->setProperty("visible", false);
  if (closed.isEmpty() && !closed.wait(1000)) return false;
  return !popup->property("visible").toBool();
}

QQuickItem* find_item(QQuickItem* root, const QString& object_name) {
  if (root == nullptr) return nullptr;
  if (root->objectName() == object_name) return root;
  for (auto* child : root->childItems()) {
    if (auto* found = find_item(child, object_name)) return found;
  }
  return nullptr;
}

QQuickItem* find_item_tree_recursive(QObject* root, const QString& object_name,
                                     QSet<const QObject*>& visited) {
  if (root == nullptr || visited.contains(root)) return nullptr;
  visited.insert(root);
  if (auto* item = qobject_cast<QQuickItem*>(root)) {
    if (item->objectName() == object_name) return item;
    for (auto* child_item : item->childItems()) {
      if (auto* found = find_item_tree_recursive(child_item, object_name, visited))
        return found;
    }
  }
  if (auto* window = qobject_cast<QQuickWindow*>(root)) {
    if (auto* found = find_item_tree_recursive(window->contentItem(), object_name, visited))
      return found;
  }
  for (auto* child : root->children()) {
    if (auto* found = find_item_tree_recursive(child, object_name, visited)) return found;
  }
  return nullptr;
}

QQuickItem* find_item_tree(QObject* root, const QString& object_name) {
  QSet<const QObject*> visited;
  return find_item_tree_recursive(root, object_name, visited);
}

QQuickItem* find_text_item_recursive(QObject* root, QObject* original_root,
                                     const QString& expected_text,
                                     QSet<const QObject*>& visited) {
  if (root == nullptr || visited.contains(root)) return nullptr;
  visited.insert(root);
  if (root != original_root) {
    if (auto* item = qobject_cast<QQuickItem*>(root);
        item != nullptr && item->property("text").toString() == expected_text)
      return item;
  }
  if (auto* item = qobject_cast<QQuickItem*>(root)) {
    for (auto* child_item : item->childItems()) {
      if (auto* found = find_text_item_recursive(child_item, original_root,
                                                  expected_text, visited))
        return found;
    }
  }
  for (auto* child : root->children()) {
    if (auto* found = find_text_item_recursive(child, original_root,
                                                expected_text, visited))
      return found;
  }
  return nullptr;
}

QQuickItem* find_text_item(QObject* root, const QString& expected_text) {
  QSet<const QObject*> visited;
  return find_text_item_recursive(root, root, expected_text, visited);
}

int palette_row_count(QQuickItem* palette,
                      const std::array<QQuickItem*, 15>& buttons) {
  if (palette == nullptr) return 0;
  std::vector<qreal> row_tops;
  for (auto* button : buttons) {
    if (button == nullptr || !button->isVisible()) return 0;
    const qreal top = button->mapToItem(palette, QPointF(0.0, 0.0)).y();
    const auto already_counted = std::any_of(
        row_tops.begin(), row_tops.end(),
        [top](qreal row_top) { return std::abs(row_top - top) <= 2.0; });
    if (!already_counted) row_tops.push_back(top);
  }
  return static_cast<int>(row_tops.size());
}

QObject* find_object(QObject* root, const QString& object_name) {
  if (root == nullptr) return nullptr;
  if (root->objectName() == object_name) return root;
  for (auto* child : root->children()) {
    if (auto* found = find_object(child, object_name)) return found;
  }
  return nullptr;
}

bool click_item(QQuickWindow* window, QQuickItem* item) {
  if (window == nullptr || item == nullptr || !item->isVisible()) return false;
  const QPoint position = item->mapToScene(
      QPointF(item->width() / 2.0, item->height() / 2.0)).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, position);
  QTest::qWait(40);
  return true;
}

bool double_click_item(QQuickWindow* window, QQuickItem* item) {
  if (window == nullptr || item == nullptr || !item->isVisible()) return false;
  const QPoint position = item->mapToScene(
      QPointF(item->width() / 2.0, item->height() / 2.0)).toPoint();
  QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, position, 60);
  QTest::qWait(80);
  return true;
}

bool capture(QQuickWindow* window, const QString& evidence_dir,
             const QString& filename) {
  if (evidence_dir.isEmpty()) return true;
  if (!QDir().mkpath(evidence_dir)) return false;
  const auto image = window->grabWindow();
  return !image.isNull() &&
         image.save(QDir(evidence_dir).filePath(filename));
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QApplication app(argc, argv);
  QAccessible::setActive(true);

  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.loadUrl(QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/tests/timeline-track-surface-fixture.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("Timeline track surface fixture should instantiate the production components");
  }

  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  if (window == nullptr) return fail("Timeline fixture should create a QQuickWindow");
  window->show();
  QTest::qWait(120);

  auto* header = find_item(window->contentItem(), QStringLiteral("testedTimelineTrackHeader"));
  auto* audio_header = find_item(window->contentItem(), QStringLiteral("testedAudioTimelineTrackHeader"));
  auto* clip_content = find_item(window->contentItem(), QStringLiteral("testedTimelineClipContent"));
  auto* compact_header = find_item(window->contentItem(), QStringLiteral("timelineTrackCompactHeader"));
  auto* track_id = find_item(window->contentItem(), QStringLiteral("timelineTrackCompactId"));
  auto* track_name = find_item(window->contentItem(), QStringLiteral("timelineTrackCompactName"));
  auto* rename_target = find_item(window->contentItem(), QStringLiteral("timelineTrackCompactNameHitTarget"));
  auto* primary_toggle = find_item(header, QStringLiteral("timelineTrackCompactPrimaryToggle"));
  auto* more_button = find_item(header, QStringLiteral("timelineTrackCompactMoreButton"));
  auto* lock_indicator = find_item(audio_header, QStringLiteral("timelineTrackCompactLockedIndicator"));
  auto* solo_indicator = find_item(audio_header, QStringLiteral("timelineTrackCompactSoloIndicator"));
  auto* fake_document = find_object(window, QStringLiteral("fakeTimelineDocument"));
  auto* clip_title = find_item(window->contentItem(), QStringLiteral("timelineClipTitleLabel"));
  auto* text_clip_label = find_item(window->contentItem(), QStringLiteral("testedTimelineTextClipLabel"));
  auto* shape_clip_label = find_item(window->contentItem(), QStringLiteral("testedTimelineShapeClipLabel"));
  auto* photo_clip_label = find_item(window->contentItem(), QStringLiteral("testedTimelinePhotoClipLabel"));
  auto* generator_clip_label = find_item(window->contentItem(), QStringLiteral("testedTimelineGeneratorClipLabel"));
  auto* palette = find_item_tree(window, QStringLiteral("timelineToolPalette"));
  auto* tool_grid = find_item_tree(window, QStringLiteral("timelineToolGrid"));
  if (!header || !clip_content || !compact_header || !track_id || !track_name ||
      !rename_target || !clip_title || !audio_header || !palette || !tool_grid ||
      !primary_toggle || !more_button || !lock_indicator || !solo_indicator || !fake_document ||
      !text_clip_label || !shape_clip_label || !photo_clip_label || !generator_clip_label)
    return fail("The production track header, clip labels, and primary tool palette should be discoverable");

  if (text_clip_label->property("displayTitle").toString() !=
          QStringLiteral("Light spills across the doorway") ||
      shape_clip_label->property("displayTitle").toString() !=
          QStringLiteral("Ellipse shape") ||
      photo_clip_label->property("displayTitle").toString() !=
          QStringLiteral("Harbor Lights"))
    return fail("Clip titles should use actual title text, precise shape names, and readable source names");
  const QString photo_summary = photo_clip_label->property("displaySummary").toString();
  const QString photo_description =
      photo_clip_label->property("completeDescription").toString();
  const QString generator_summary = generator_clip_label->property("displaySummary").toString();
  if (photo_clip_label->property("displayMediaKind").toString() != QStringLiteral("Photo") ||
      !photo_summary.startsWith(QStringLiteral("Photo · ")) ||
      shape_clip_label->property("displayMediaKind").toString() != QStringLiteral("Shape") ||
      text_clip_label->property("displayMediaKind").toString() != QStringLiteral("Text") ||
      generator_clip_label->property("displayMediaKind").toString() != QStringLiteral("Graphic"))
    return fail("Every clip must retain an explicit media type independently of its applied edits");
  if (!photo_summary.contains(QStringLiteral("Clip mask · ellipse")) ||
      !photo_summary.contains(QStringLiteral("Scale · Position")) ||
      photo_summary.contains(QStringLiteral("4 keyframes")) ||
      !photo_description.contains(QStringLiteral("4 keyframes")) ||
      photo_summary.contains(QStringLiteral("Animated")) ||
      generator_clip_label->property("displayTitle").toString() !=
          QStringLiteral("Vector path") ||
      !generator_summary.contains(QStringLiteral("12 points")) ||
      !generator_summary.contains(QStringLiteral("8 keyframes")) ||
      generator_summary.contains(QStringLiteral("Animated")))
    return fail("Clip summaries should name masks, effects, animated parameters, counts, and generator details precisely");
  if (photo_summary.contains(QStringLiteral("Film Grain")) ||
      !photo_description.contains(QStringLiteral("Film Grain")))
    return fail("Effect names should stay out of the compact summary and remain available in the full description");

  const std::array<QString, 8> deferred_popup_names = {
      QStringLiteral("timelineTrackGroupTransformPopup"),
      QStringLiteral("timelineCreateTrackGroupPopup"),
      QStringLiteral("timelineAddToTrackGroupPopup"),
      QStringLiteral("timelineRenameTrackGroupPopup"),
      QStringLiteral("timelineNestTrackGroupPopup"),
      QStringLiteral("timelineRenameTrackPopup"),
      QStringLiteral("timelineDeleteTrackConfirmation"),
      QStringLiteral("timelineTrackAutomationPopup")};
  for (const auto& popup_name : deferred_popup_names) {
    if (find_object(window, popup_name) != nullptr)
      return fail("Track popup wrappers should remain absent until a user action opens one");
  }
  if (find_item_tree(window, QStringLiteral("timelineGroupTransformScale")) != nullptr ||
      find_item_tree(window, QStringLiteral("timelineNestGroupList")) != nullptr ||
      find_item_tree(window, QStringLiteral("timelineConfirmDeleteTrackButton")) != nullptr)
    return fail("Track group forms and delete controls must not exist before their popup opens");

  if (!QMetaObject::invokeMethod(header, "openGroupTransformPopup"))
    return fail("The production group transform action should instantiate and open its popup on demand");
  auto* group_transform_popup = find_object(
      window, QStringLiteral("timelineTrackGroupTransformPopup"));
  if (group_transform_popup == nullptr || !group_transform_popup->property("visible").toBool())
    return fail("The group transform popup should be visible after its production open helper runs");
  QTest::qWait(40);
  if (!group_transform_popup->property("contentCreated").toBool() ||
      find_item_tree(window, QStringLiteral("timelineGroupTransformScale")) == nullptr)
    return fail("Opening the group transform popup should create its controls on demand");
  if (!close_popup_and_wait(group_transform_popup))
    return fail("Closing the group transform popup should finish its exit transition before input checks");
  if (!group_transform_popup->property("contentCreated").toBool() ||
      find_item_tree(window, QStringLiteral("timelineGroupTransformScale")) == nullptr)
    return fail("Opened popup content should remain available after close for responsive reuse");

  if (!QMetaObject::invokeMethod(header, "openNestTrackGroupPopup"))
    return fail("The production nested-group action should instantiate and open its popup on demand");
  auto* nest_group_popup = find_object(window, QStringLiteral("timelineNestTrackGroupPopup"));
  if (nest_group_popup == nullptr || !nest_group_popup->property("visible").toBool())
    return fail("The nested-group popup should be visible after its production open helper runs");
  QTest::qWait(40);
  if (!nest_group_popup->property("contentCreated").toBool())
    return fail("Opening the nested-group popup should instantiate its candidate list on demand");
  if (find_item_tree(window, QStringLiteral("timelineNestGroupList")) == nullptr)
    return fail("Opening the nested-group popup should materialize its actual candidate-list content");
  if (!close_popup_and_wait(nest_group_popup))
    return fail("Closing the nested-group popup should finish its exit transition before input checks");

  auto* audio_track_id = find_item(audio_header, QStringLiteral("timelineTrackCompactId"));
  auto* audio_track_name = find_item(audio_header, QStringLiteral("timelineTrackCompactName"));
  auto* audio_compact_header = find_item(audio_header, QStringLiteral("timelineTrackCompactHeader"));
  if (!audio_track_id || !audio_track_name || !audio_compact_header ||
      audio_header->width() != 168.0 || audio_header->height() != 36.0 ||
      !audio_compact_header->isVisible() ||
      audio_track_id->property("text").toString() != QStringLiteral("A1") ||
      audio_track_name->property("text").toString() != QStringLiteral("Dialogue"))
    return fail("A 36px audio lane must keep its A1 identifier and custom name readable");

  if (header->width() != 168.0 || header->height() != 44.0 || !compact_header->isVisible() ||
      track_id->property("text").toString() != QStringLiteral("V1") ||
      track_name->property("text").toString() != QStringLiteral("Interview") ||
      track_name->width() < 40.0 || rename_target->height() < 26.0 ||
      !primary_toggle->isVisible() || !more_button->isVisible())
    return fail("A 44px by 168px lane must keep its V1 identifier, readable name, and compact actions");

  const auto name_accessible = QAccessible::queryAccessibleInterface(rename_target);
  const auto primary_accessible = QAccessible::queryAccessibleInterface(primary_toggle);
  const auto full_accessible_track_name = accessible_name(rename_target);
  if (!name_accessible ||
      !full_accessible_track_name.startsWith(QStringLiteral("Inspect ")) ||
      !full_accessible_track_name.contains(QStringLiteral("V1 · Interview")) ||
      !primary_accessible ||
      accessible_name(primary_toggle).isEmpty())
    return fail("Compact track labels should keep their full name accessible while separating the code visually");

  if (!click_item(window, rename_target) ||
      fake_document->property("inspectRequestCount").toInt() != 1 ||
      fake_document->property("lastInspectedTrackId").toString() != QStringLiteral("video-1") ||
      find_object(window, QStringLiteral("timelineRenameTrackPopup")) != nullptr)
    return fail("A single click on the compact track name should inspect that track exactly once without renaming it");
  if (!click_item(window, primary_toggle) ||
      fake_document->property("inspectRequestCount").toInt() != 1)
    return fail("Clicking the compact visibility toggle should not request track inspection");
  if (!click_item(window, more_button) ||
      fake_document->property("inspectRequestCount").toInt() != 1)
    return fail("Opening compact More actions should not request track inspection");
  auto* context_menu = find_object(window, QStringLiteral("timelineTrackContextMenu"));
  if (context_menu && context_menu->property("visible").toBool())
    context_menu->setProperty("visible", false);

  QVariantMap video_state = fixture->property("headerTrackData").toMap();
  video_state.insert(QStringLiteral("visible"), false);
  video_state.insert(QStringLiteral("effectiveVisible"), false);
  fixture->setProperty("headerTrackData", QVariant::fromValue(video_state));
  QTest::qWait(30);
  if (!primary_toggle->property("active").toBool() ||
      primary_toggle->property("iconName").toString() != QStringLiteral("hidden"))
    return fail("A hidden video track should keep a quiet but visible hidden-state indication");
  video_state.insert(QStringLiteral("visible"), true);
  video_state.insert(QStringLiteral("effectiveVisible"), true);
  fixture->setProperty("headerTrackData", QVariant::fromValue(video_state));

  QVariantMap audio_state = fixture->property("audioTrackData").toMap();
  audio_state.insert(QStringLiteral("locked"), true);
  audio_state.insert(QStringLiteral("solo"), true);
  audio_state.insert(QStringLiteral("muted"), true);
  fixture->setProperty("audioTrackData", QVariant::fromValue(audio_state));
  QTest::qWait(30);
  const auto lock_accessible_name = accessible_name(lock_indicator);
  const auto solo_accessible_name = accessible_name(solo_indicator);
  auto* audio_primary_toggle = find_item(audio_header,
                                        QStringLiteral("timelineTrackCompactPrimaryToggle"));
  if (!lock_indicator->isVisible() || !solo_indicator->isVisible() ||
      lock_accessible_name != QStringLiteral("Track locked") ||
      solo_accessible_name != QStringLiteral("Track soloed") || !audio_primary_toggle ||
      !audio_primary_toggle->property("active").toBool() ||
      audio_primary_toggle->property("iconName").toString() != QStringLiteral("audio-muted"))
    return fail("Compact audio lanes should preserve lock, solo, and mute state visibility");

  const QFont clip_font = qvariant_cast<QFont>(clip_title->property("font"));
  if (clip_font.pixelSize() < 10 || clip_title->property("elide").toInt() == 0)
    return fail("Clip labels should abbreviate at a readable fixed size instead of shrinking to tiny text");

  const std::array<QString, 15> tool_keys = {
      QStringLiteral("select"), QStringLiteral("track-select-backward"),
      QStringLiteral("track-select-forward"), QStringLiteral("razor"),
      QStringLiteral("trim"), QStringLiteral("ripple"), QStringLiteral("roll"),
      QStringLiteral("slip"), QStringLiteral("slide"), QStringLiteral("rate-stretch"),
      QStringLiteral("pen"), QStringLiteral("hand"), QStringLiteral("zoom"),
      QStringLiteral("text"), QStringLiteral("shape")};
  const std::array<QString, 15> visible_labels = {
      QStringLiteral("Select · V"), QStringLiteral("Track Select ←"),
      QStringLiteral("Track Select →"), QStringLiteral("Blade · C"),
      QStringLiteral("Trim"), QStringLiteral("Ripple Trim"), QStringLiteral("Roll · N"),
      QStringLiteral("Slip · Y"), QStringLiteral("Slide · U"),
      QStringLiteral("Rate Stretch · R"), QStringLiteral("Pen · P"),
      QStringLiteral("Hand · H"), QStringLiteral("Zoom · Z"),
      QStringLiteral("Text"), QStringLiteral("Shape")};
  const std::array<QString, 15> accessible_names = {
      QStringLiteral("Select tool"), QStringLiteral("Track Select ←"),
      QStringLiteral("Track Select →"), QStringLiteral("Blade tool"),
      QStringLiteral("Trim tool"), QStringLiteral("Ripple Trim tool"),
      QStringLiteral("Roll tool"), QStringLiteral("Slip tool"),
      QStringLiteral("Slide tool"), QStringLiteral("Rate Stretch tool"),
      QStringLiteral("Pen tool"), QStringLiteral("Hand tool"),
      QStringLiteral("Zoom tool"), QStringLiteral("Create text clip"),
      QStringLiteral("Create shape clip")};
  if (palette->property("toolCount").toInt() != static_cast<int>(tool_keys.size()) ||
      !palette->property("compact").toBool() ||
      palette->property("vertical").toBool() || tool_grid->width() <= 0.0 ||
      tool_grid->height() <= 0.0)
    return fail("The production palette should expose all 15 tools in its compact horizontal Flow");

  std::array<QQuickItem*, 15> palette_buttons{};
  for (std::size_t index = 0; index < tool_keys.size(); ++index) {
    const QString object_name = QStringLiteral("timelineTool_") + tool_keys[index];
    auto* button = find_item_tree(window, object_name);
    auto* painted_label = find_text_item(button, visible_labels[index]);
    if (button == nullptr || !button->isVisible() ||
        !button->property("enabled").toBool() ||
        button->property("text").toString() != visible_labels[index] ||
        button->property("accessibleName").toString() != accessible_names[index] ||
        button->width() + 0.5 < button->implicitWidth() || painted_label == nullptr ||
        !painted_label->isVisible()) {
      std::cerr << "Timeline palette control " << object_name.toStdString()
                << " missing or not fully labeled\n";
      return fail("Every production palette action should be visible, enabled, and meaningfully labeled");
    }
    const auto painted_label_bounds = painted_label->mapRectToItem(
        button, QRectF(0.0, 0.0, painted_label->width(), painted_label->height()));
    if (!QRectF(0.0, 0.0, button->width(), button->height())
             .contains(painted_label_bounds))
      return fail("The full text for each palette action should remain inside its visible button bounds");
    palette_buttons[index] = button;
  }
  if (!palette_buttons[1]->property("tooltipText").toString().contains(
          QStringLiteral("Shift+A")) ||
      !palette_buttons[2]->property("tooltipText").toString().contains(
          QStringLiteral("Keyboard shortcut: A")))
    return fail("Track Select direction controls should keep their full shortcut hints in their tooltips");

  const auto assert_palette_bounds = [&]() {
    const QRectF palette_bounds(0.0, 0.0, palette->width(), palette->height());
    std::array<QRectF, 15> button_bounds{};
    for (std::size_t index = 0; index < palette_buttons.size(); ++index) {
      auto* button = palette_buttons[index];
      if (button == nullptr || !button->isVisible()) return false;
      button_bounds[index] = button->mapRectToItem(
          palette, QRectF(0.0, 0.0, button->width(), button->height()));
      if (!palette_bounds.contains(button_bounds[index])) return false;
      for (std::size_t other = 0; other < index; ++other) {
        if (button_bounds[index].intersects(button_bounds[other])) return false;
      }
    }
    return true;
  };
  const int compact_rows = palette_row_count(palette, palette_buttons);
  if (compact_rows < 2 || compact_rows > 3 || !assert_palette_bounds())
    return fail("All 15 fully labeled controls should fit without overlap in two or three horizontal rows");

  window->setWidth(1180);
  QTest::qWait(80);
  const int wider_rows = palette_row_count(palette, palette_buttons);
  if (wider_rows < 2 || wider_rows > compact_rows || !assert_palette_bounds())
    return fail("The palette Flow should reflow into fewer full-label rows when its Timeline surface widens");
  window->setWidth(720);
  QTest::qWait(80);
  if (palette_row_count(palette, palette_buttons) != compact_rows || !assert_palette_bounds())
    return fail("The palette Flow should restore its compact row geometry when the surface narrows again");

  fixture->setProperty("hasActiveTimeline", false);
  QTest::qWait(30);
  for (auto* button : palette_buttons) {
    if (button == nullptr || button->property("enabled").toBool() ||
        !button->property("tooltipText").toString().contains(
            QStringLiteral("Create or open a timeline")))
      return fail("A missing timeline should disable every tool with a useful explanation");
  }
  fixture->setProperty("hasActiveTimeline", true);

  auto* rate_stretch_button = palette_buttons[9];
  fixture->setProperty("rateStretchEnabled", false);
  QTest::qWait(20);
  if (rate_stretch_button->property("enabled").toBool() ||
      !rate_stretch_button->property("tooltipText").toString().contains(
          QStringLiteral("retime curve")))
    return fail("Rate Stretch should expose its real clip-availability reason");
  fixture->setProperty("rateStretchEnabled", true);

  auto* pen_button = palette_buttons[10];
  fixture->setProperty("envelopePenEnabled", false);
  QTest::qWait(20);
  if (pen_button->property("enabled").toBool() ||
      !pen_button->property("tooltipText").toString().contains(
          QStringLiteral("track envelope")))
    return fail("Pen should expose its real envelope-availability reason");
  fixture->setProperty("envelopePenEnabled", true);

  for (std::size_t index = 0; index < tool_keys.size(); ++index) {
    const QString& key = tool_keys[index];
    auto* button = palette_buttons[index];
    if (!click_item(window, button))
      return fail("Every enabled palette action should respond to a real pointer click");
    if (key == QStringLiteral("text") || key == QStringLiteral("shape")) {
      if (fixture->property("lastPaletteCreateKind").toString() != key)
        return fail("Text and Shape clicks should emit their matching clip creation request");
    } else {
      const QString expected_tool = key.startsWith(QStringLiteral("track-select-"))
                                        ? QStringLiteral("track-select") : key;
      if (fixture->property("lastPaletteTool").toString() != expected_tool ||
          fixture->property("activeTool").toString() != expected_tool ||
          !button->property("active").toBool())
        return fail("Tool clicks should update and reflect the production palette's active tool");
    }
    if (key == QStringLiteral("track-select-backward") &&
        (fixture->property("trackSelectionDirection").toInt() != -1 ||
         palette->property("trackSelectionDirection").toInt() != -1))
      return fail("Backward Track Select should update the active pointer operation direction");
    if (key == QStringLiteral("track-select-forward") &&
        (fixture->property("trackSelectionDirection").toInt() != 1 ||
         palette->property("trackSelectionDirection").toInt() != 1))
      return fail("Forward Track Select should update the active pointer operation direction");
  }

  const QString evidence_dir = qEnvironmentVariable("LOOKSY_TIMELINE_UI_EVIDENCE_DIR");
  if (!capture(window, evidence_dir, QStringLiteral("timeline-track-surface-44px.png")))
    return fail("The compact Timeline screenshot should be captured");

  if (!double_click_item(window, rename_target))
    return fail("The compact track name should accept a double-click rename gesture");
  auto* rename_popup = find_object(window, QStringLiteral("timelineRenameTrackPopup"));
  auto* name_field = find_item_tree(window, QStringLiteral("timelineTrackNameField"));
  auto* save_button = find_item_tree(window, QStringLiteral("timelineSaveTrackNameButton"));
  if (!rename_popup || !rename_popup->property("visible").toBool() || !name_field ||
      name_field->property("text").toString() != QStringLiteral("Interview") || !save_button)
    return fail("Double-clicking a compact track name should open the production rename dialog");

  name_field->setProperty("text", QStringLiteral("B-Roll"));
  if (!click_item(window, save_button))
    return fail("The production Save Name button should be clickable");
  if (!fake_document || fake_document->property("lastRenamedTrackIndex").toInt() != 0 ||
      fixture->property("lastRenamedTrackName").toString() != QStringLiteral("B-Roll") ||
      track_name->property("text").toString() != QStringLiteral("B-Roll"))
    return fail("Saving a track rename should update the document and the compact lane label");
  if (!capture(window, evidence_dir, QStringLiteral("timeline-track-renamed-44px.png")))
    return fail("The renamed compact Timeline screenshot should be captured");
  if (!QTest::qWaitFor([rename_popup]() {
        return !rename_popup->property("visible").toBool();
      }, 1000))
    return fail("The rename dialog must finish closing before inspecting another track header");

  header->setHeight(62.0);
  QTest::qWait(60);
  auto* standard_header_name = find_item(window->contentItem(), QStringLiteral("timelineTrackHeaderName"));
  if (!standard_header_name || !standard_header_name->isVisible() || compact_header->isVisible())
    return fail("A standard-height lane should restore the full track header controls");
  const auto inspections_before_standard_click =
      fake_document->property("inspectRequestCount").toInt();
  if (!click_item(window, standard_header_name) ||
      fake_document->property("inspectRequestCount").toInt() !=
          inspections_before_standard_click + 1 ||
      fake_document->property("lastInspectedTrackId").toString() != QStringLiteral("video-1"))
    return fail("A single click on the standard-height track name should request inspection once");
  if (!double_click_item(window, standard_header_name))
    return fail("The standard track name should also accept a double-click rename gesture");
  if (!rename_popup->property("visible").toBool())
    return fail("Double-clicking a standard-height track name should open the rename dialog");
  if (!capture(window, evidence_dir, QStringLiteral("timeline-track-surface-standard.png")))
    return fail("The standard-height Timeline screenshot should be captured");

  std::cout << "Timeline compact track hierarchy, state indicators, double-click rename, and readable clip truncation passed\n";
  return EXIT_SUCCESS;
}
