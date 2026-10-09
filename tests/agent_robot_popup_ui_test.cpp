#include <QAccessible>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QQmlError>
#include <QStringList>
#include <QThread>
#include <QtTest/QTest>
#include <iostream>
#include <memory>

namespace {
void settle() {
  for (int index = 0; index < 24; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(20);
  }
}
int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}
QQuickItem* find_item(QQuickItem* parent, const QString& name) {
  if (parent->objectName() == name)
    return parent;
  for (auto* child : parent->childItems()) {
    if (auto* match = find_item(child, name))
      return match;
  }
  return nullptr;
}
QQuickItem* find_visible_enabled_item(QQuickItem* parent, const QString& name) {
  if (parent->objectName() == name && parent->isVisible() && parent->isEnabled())
    return parent;
  for (auto* child : parent->childItems()) {
    if (auto* match = find_visible_enabled_item(child, name))
      return match;
  }
  return nullptr;
}
QQuickItem* find_visible_item(QQuickItem* parent, const QString& name) {
  if (parent->objectName() == name && parent->isVisible())
    return parent;
  for (auto* child : parent->childItems()) {
    if (auto* match = find_visible_item(child, name))
      return match;
  }
  return nullptr;
}
bool inside_screen(QQuickItem* item, QQuickItem* screen) {
  return item && screen->boundingRect().contains(item->mapRectToItem(screen, item->boundingRect()));
}
void click_item(QQuickWindow* window, QQuickItem* item) {
  const auto position = item->mapToScene(item->boundingRect().center());
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, position.toPoint());
  settle();
}
// Animator values live in the scene graph until an animation finishes. Read
// the rendered local feet instead of a GUI property that no longer ticks.
bool observe_rendered_legs(QQuickItem* character, QImage& previous) {
  if (!character->property("motionEnabled").toBool() ||
      (!character->property("walking").toBool() && !character->property("climbing").toBool()))
    return false;
  const auto grab = character->grabToImage();
  if (!grab) return false;
  bool ready = false;
  const auto connection = QObject::connect(grab.data(), &QQuickItemGrabResult::ready,
      character, [&ready] { ready = true; });
  QElapsedTimer clock;
  clock.start();
  while (!ready && clock.elapsed() < 150) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(2);
  }
  QObject::disconnect(connection);
  if (!ready || grab->image().isNull()) return false;
  const auto image = grab->image();
  const auto feet = image.copy(0, static_cast<int>(image.height() * 0.75),
      image.width(), static_cast<int>(image.height() * 0.23));
  const bool moved = !previous.isNull() && previous.size() == feet.size() && previous != feet;
  previous = feet;
  return moved;
}
int verify_workspace_popup(QQuickWindow* window, QObject* fixture, QObject* controller,
                           QQmlPropertyMap& motion_settings, const QString& output) {
  auto* root = window->contentItem();
  const QVariantMap model_output{
      {QStringLiteral("stage"), QStringLiteral("plan-worker")},
      {QStringLiteral("status"), QStringLiteral("complete")},
      {QStringLiteral("uiAnchor"), QStringLiteral("image")},
      {QStringLiteral("evidence"), QVariantMap{
          {QStringLiteral("modelOutputAvailable"), true},
          {QStringLiteral("modelOutput"), QStringLiteral("Warm the highlights and preserve natural skin tones.")}}}};
  const QVariantMap rendering{
      {QStringLiteral("stage"), QStringLiteral("render")},
      {QStringLiteral("status"), QStringLiteral("running")},
      {QStringLiteral("uiAnchor"), QStringLiteral("inspector")},
      {QStringLiteral("detail"), QStringLiteral("Rendering the edited preview.")}};
  fixture->setProperty("workspaceVisible", true);
  motion_settings.insert(QStringLiteral("reducedMotion"), false);
  controller->setProperty("lastRunReport", QVariantMap{
      {QStringLiteral("ok"), false},
      {QStringLiteral("failureReason"), QStringLiteral("The local model needed longer than 120 seconds to respond. No edits were applied; retry with another local vision model or a longer deadline.")}});
  controller->setProperty("runEvents", QVariantList{QVariantMap{
      {QStringLiteral("stage"), QStringLiteral("plan-worker")},
      {QStringLiteral("status"), QStringLiteral("failed")},
      {QStringLiteral("evidence"), QVariantMap{{QStringLiteral("modelName"), QStringLiteral("Qwen3 VL 32B")}}}}});
  settle();
  auto* workspace = find_item(root, QStringLiteral("workspacePopupFixture"));
  auto* workspace_robot = find_item(root, QStringLiteral("agentWorkspaceRoamingRobot"));
  auto* character = find_item(root, QStringLiteral("agentWorkspaceRoamingCharacter"));
  auto* workspace_close = find_item(root, QStringLiteral("agentWorkspaceStatusCloseButton"));
  auto* fast_model = find_item(root, QStringLiteral("agentWorkspaceUseFastModelButton"));
  if (!workspace_robot || !workspace_robot->isVisible() || !character->property("articulated").toBool() ||
      find_item(workspace, QStringLiteral("agentWorkspaceHeldScreen")))
    return fail("The workspace must use the free robot joint rig without a held screen");
  const auto exact_error = QStringLiteral("photo.inspect_composition: Unknown composition argument: fontFamily");
  const auto long_detail = QStringLiteral("Font inspection requires a non-empty family.\n").repeated(30);
  const QVariantMap failure_report{
      {"ok", false}, {"runId", "failure-details-test"}, {"failureReason", exact_error},
      {"errors", QStringList{exact_error, long_detail}},
      {"toolCalls", QVariantList{QVariantMap{
          {"name", "photo.inspect_composition"}, {"status", "failed"},
          {"outcome", QVariantMap{{"error", exact_error},
              {"arguments", QVariantMap{{"assetId", "asset-test"}, {"fontFamily", "Arial"}}}}}}}}};
  controller->setProperty("lastRunReport", failure_report);
  settle();
  auto* status_detail = find_item(root, QStringLiteral("agentWorkspaceStatusDetail"));
  auto* details_action = find_item(root, QStringLiteral("agentWorkspaceFailureDetailsButton"));
  auto* details_popup = workspace->findChild<QObject*>(QStringLiteral("agentFailureDetails"));
  if (!status_detail || !details_action || !details_popup)
    return fail("A failed agent run must expose clickable failure details");
  for (const auto size : {QSize(1024, 760), QSize(1280, 800)}) {
    window->resize(size);
    settle();
    if (!workspace_close->isVisible() || !workspace_close->isEnabled())
      return fail("The unobtrusive workspace status must retain an accessible close action");
    if (!output.isEmpty())
      window->grabWindow().save(QDir(output).filePath(QStringLiteral("workspace-failed-%1x%2.png").arg(size.width()).arg(size.height())));
    click_item(window, status_detail);
    auto* details_content = qvariant_cast<QQuickItem*>(details_popup->property("contentItem"));
    auto* details_text = details_content ? find_item(details_content, "agentFailureDetailsText") : nullptr;
    auto* copy_action = details_content ? find_item(details_content, "agentFailureDetailsCopy") : nullptr;
    if (!details_popup->property("opened").toBool() || !details_text || !copy_action ||
        !details_text->property("readOnly").toBool() || !details_text->property("selectByMouse").toBool() ||
        !details_text->property("text").toString().contains(long_detail.trimmed()) ||
        !details_text->property("text").toString().contains("fontFamily") ||
        !details_text->property("text").toString().contains("failure-details-test") ||
        !copy_action->isVisible() || !copy_action->isEnabled()) {
      std::cerr << "Details opened=" << details_popup->property("opened").toBool()
                << " text=" << (details_text ? details_text->property("text").toString().toStdString() : "<missing>")
                << " copy=" << (copy_action && copy_action->isVisible()) << '\n';
      return fail("Clicking the failure must open the complete selectable error, failed tool and arguments");
    }
    if (!output.isEmpty())
      window->grabWindow().save(QDir(output).filePath(QStringLiteral("failure-details-%1x%2.png").arg(size.width()).arg(size.height())));
    QKeyEvent details_escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &details_escape);
    settle();
    if (details_popup->property("opened").toBool() || workspace->property("dismissed").toBool() ||
        controller->property("lastRunReport").toMap() != failure_report || !status_detail->hasActiveFocus())
      return fail("Escape must close only the failure details, preserve the report and restore opener focus");
  }
  details_action->forceActiveFocus(Qt::TabFocusReason);
  QTest::keyClick(window, Qt::Key_Space);
  settle();
  if (!details_popup->property("opened").toBool())
    return fail("The explicit Details action must also open with the keyboard");
  QMetaObject::invokeMethod(details_popup, "close");
  settle();
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_FAILURE_DETAILS_ONLY")) {
    fixture->setProperty("workspaceVisible", false);
    auto* dialog = find_item(root, QStringLiteral("agentRunDialog"));
    QMetaObject::invokeMethod(dialog, "open");
    dialog->setProperty("currentPage", 2);
    settle();
    auto* terminal = find_item(root, QStringLiteral("agentTerminalScreen"));
    auto* terminal_message = find_item(terminal, QStringLiteral("agentTerminalFailureMessage"));
    auto* terminal_details = terminal->findChild<QObject*>(QStringLiteral("agentFailureDetails"));
    if (!terminal_message || !terminal_message->isVisible() || !terminal_details)
      return fail("The terminal failure message must expose the same details action");
    click_item(window, terminal_message);
    if (!terminal_details->property("opened").toBool() ||
        !terminal_details->property("detailsText").toString().contains(long_detail.trimmed()))
      return fail("Clicking the terminal failure must preserve the complete recorded error");
    if (!output.isEmpty())
      window->grabWindow().save(QDir(output).filePath("terminal-failure-details.png"));
    QKeyEvent terminal_escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &terminal_escape);
    settle();
    if (terminal_details->property("opened").toBool() || !dialog->property("visible").toBool() ||
        !terminal_message->hasActiveFocus()) {
      std::cerr << "Terminal details opened=" << terminal_details->property("opened").toBool()
                << " assistant visible=" << dialog->property("visible").toBool()
                << " focus=" << terminal_message->hasActiveFocus() << '\n';
      return fail("Closing terminal details must leave the assistant open and restore failure-message focus");
    }
    std::cout << "Failure details open by pointer and keyboard, preserve full text, and return focus without changing the report.\n";
    return 0;
  }
  QMetaObject::invokeMethod(workspace_close, "click");
  settle();
  if (!workspace->property("dismissed").toBool() || workspace_robot->isVisible() ||
      !controller->property("lastRunReport").toMap().contains(QStringLiteral("failureReason")))
    return fail("Close must hide the roaming robot and preserve the failed report");
  QMetaObject::invokeMethod(find_item(root, QStringLiteral("agentWorkspaceRestoreButton")), "click");
  settle();
  window->requestActivate();
  workspace->forceActiveFocus(Qt::OtherFocusReason);
  settle();
  QKeyEvent workspace_escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &workspace_escape);
  settle();
  if (!workspace->property("dismissed").toBool())
    return fail("Escape must hide the roaming robot");

  controller->setProperty("running", true);
  controller->setProperty("runEvents", QVariantList{QVariantMap{
      {QStringLiteral("stage"), QStringLiteral("plan-worker")},
      {QStringLiteral("status"), QStringLiteral("running")},
      {QStringLiteral("uiAnchor"), QStringLiteral("image")},
      {QStringLiteral("detail"), QStringLiteral("Waiting for the local vision model to return an edit plan.")}}});
  settle();
  settle();
  auto* sweep = find_item(root, QStringLiteral("agentWorkspaceActivitySweep"));
  const auto old_robot_x = workspace_robot->x();
  auto adjustment_event = rendering;
  adjustment_event.insert(QStringLiteral("evidence"), QVariantMap{
      {QStringLiteral("adjustmentKeys"), QStringList{QStringLiteral("exposure"), QStringLiteral("contrast")}}});
  controller->setProperty("runEvents", QVariantList{model_output, adjustment_event});
  bool legs_moved = false;
  QImage previous_legs;
  bool exposure_touched = false;
  bool contrast_touched = false;
  bool hand_aligned = false;
  bool climbed = false;
  bool jumped = false;
  bool tool_extracted = false;
  bool feet_grounded = false;
  bool character_aside_shown = false;
  for (int frame = 0; frame < 180; ++frame) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(60);
    legs_moved = legs_moved || observe_rendered_legs(character, previous_legs);
    climbed = climbed || workspace->property("locomotion").toString() == QStringLiteral("climb");
    jumped = jumped || workspace->property("locomotion").toString() == QStringLiteral("jump");
    const auto deployment = character->property("toolDeployProgress").toDouble();
    tool_extracted = tool_extracted || (deployment > 0.1 && deployment < 0.9);
    character_aside_shown = character_aside_shown || !workspace->property("characterAsideText").toString().isEmpty();
    if (workspace->property("touchingControl").toBool()) {
      const auto target = workspace->property("currentTouchTarget").toMap();
      const auto label = target.value(QStringLiteral("label")).toString();
      const auto local_hand = character->property("toolContactPoint").toPointF();
      const auto hand = character->mapToItem(workspace, local_hand);
      hand_aligned = hand_aligned || QLineF(hand, QPointF(target.value(QStringLiteral("x")).toDouble(),
                                                         target.value(QStringLiteral("y")).toDouble())).length() < 2;
      const auto feet = character->mapToItem(workspace, QPointF(character->width() / 2,
                                            character->property("feetBaseline").toDouble()));
      feet_grounded = feet_grounded || qAbs(feet.y() - target.value(QStringLiteral("surfaceRect")).toMap().value(QStringLiteral("y")).toDouble()) < 2;
      const bool first_contact = (label == QStringLiteral("exposure") && !exposure_touched) ||
                                 (label == QStringLiteral("contrast") && !contrast_touched);
      exposure_touched = exposure_touched || label == QStringLiteral("exposure");
      contrast_touched = contrast_touched || label == QStringLiteral("contrast");
      if (!output.isEmpty() && first_contact)
        window->grabWindow().save(QDir(output).filePath(QStringLiteral("robot-touch-%1.png").arg(label)));
    }
  }
  if (workspace->property("dismissed").toBool() || qAbs(workspace_robot->x() - old_robot_x) < 100 ||
      !legs_moved || !climbed || !jumped || !tool_extracted || !feet_grounded || !character_aside_shown ||
      !exposure_touched || !contrast_touched || !hand_aligned ||
      !find_item(root, QStringLiteral("agentWorkspaceModelOutput"))->property("text").toString().contains(QStringLiteral("skin tones")))
    return fail("The robot must climb panel edges, land with grounded feet, extract a tool and contact both actual slider knobs");
  const auto old_target_x = workspace->property("currentTouchTarget").toMap().value(QStringLiteral("x")).toDouble();
  window->resize(1100, 760);
  bool resize_grounded = false;
  for (int frame = 0; frame < 60; ++frame) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(60);
    if (!workspace->property("touchingControl").toBool()) continue;
    const auto target = workspace->property("currentTouchTarget").toMap();
    const auto tip = character->mapToItem(workspace, character->property("toolContactPoint").toPointF());
    const auto foot_y = character->mapToItem(workspace, QPointF(0, character->property("feetBaseline").toDouble())).y();
    resize_grounded = resize_grounded || (qAbs(target.value(QStringLiteral("x")).toDouble() - old_target_x) > 100 &&
        QLineF(tip, QPointF(target.value(QStringLiteral("x")).toDouble(), target.value(QStringLiteral("y")).toDouble())).length() < 2 &&
        qAbs(foot_y - target.value(QStringLiteral("surfaceRect")).toMap().value(QStringLiteral("y")).toDouble()) < 2);
  }
  if (!resize_grounded) return fail("Resizing the window must move both the robot footing and its deployed tool to the actual control");
  window->resize(1280, 800);
  controller->setProperty("runEvents", QVariantList{QVariantMap{
      {QStringLiteral("stage"), QStringLiteral("tool")}, {QStringLiteral("status"), QStringLiteral("running")},
      {QStringLiteral("uiAnchor"), QStringLiteral("toolbar")},
      {QStringLiteral("evidence"), QVariantMap{{QStringLiteral("operation"), QStringLiteral("add_text")}}}}});
  bool text_tool_grounded = false;
  for (int frame = 0; frame < 100; ++frame) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(60);
    const auto target = workspace->property("currentTouchTarget").toMap();
    if (!workspace->property("touchingControl").toBool() || target.value(QStringLiteral("label")).toString() != QStringLiteral("add_text")) continue;
    const auto tip = character->mapToItem(workspace, character->property("toolContactPoint").toPointF());
    const auto foot = character->mapToItem(workspace, QPointF(character->width() * 0.52, character->property("feetBaseline").toDouble()));
    const auto surface = target.value(QStringLiteral("surfaceRect")).toMap();
    text_tool_grounded = character->property("faceLeft").toBool() && character->property("tool").toString() == QStringLiteral("type") &&
        QLineF(tip, QPointF(target.value(QStringLiteral("x")).toDouble(), target.value(QStringLiteral("y")).toDouble())).length() < 2 &&
        qAbs(foot.y() - surface.value(QStringLiteral("y")).toDouble()) < 2 &&
        foot.x() >= surface.value(QStringLiteral("x")).toDouble() && foot.x() <= surface.value(QStringLiteral("x")).toDouble() + surface.value(QStringLiteral("width")).toDouble();
    if (text_tool_grounded && !output.isEmpty())
      window->grabWindow().save(QDir(output).filePath(QStringLiteral("robot-type-tool.png")));
    break;
  }
  if (!text_tool_grounded) return fail("The mirrored robot must perch on the narrow Text button and extract its type tool at the actual button");
  const auto workspace_animation_output = qEnvironmentVariable("PHOTO_EDITOR_WORKSPACE_ANIMATION_DIR");
  if (!workspace_animation_output.isEmpty()) {
    QDir().mkpath(workspace_animation_output);
    QFile trace(QDir(workspace_animation_output).filePath(QStringLiteral("frames.jsonl")));
    if (!trace.open(QIODevice::WriteOnly | QIODevice::Truncate))
      return fail("The native animation must be able to write its frame trace");
    QElapsedTimer recording_clock;
    recording_clock.start();
    controller->setProperty("runEvents", QVariantList{model_output});
    for (int frame = 0; frame < 520; ++frame) {
      if (frame == 24)
        controller->setProperty("runEvents", QVariantList{model_output, adjustment_event});
      if (frame == 184)
        controller->setProperty("runEvents", QVariantList{model_output, adjustment_event, QVariantMap{
            {QStringLiteral("stage"), QStringLiteral("tool")}, {QStringLiteral("status"), QStringLiteral("running")},
            {QStringLiteral("uiAnchor"), QStringLiteral("toolbar")},
            {QStringLiteral("evidence"), QVariantMap{{QStringLiteral("operation"), QStringLiteral("add_text")}}},
            {QStringLiteral("detail"), QStringLiteral("Adding the title text.")}}});
      if (frame == 368)
        controller->setProperty("runEvents", QVariantList{model_output, adjustment_event, QVariantMap{
            {QStringLiteral("stage"), QStringLiteral("validate")}, {QStringLiteral("status"), QStringLiteral("running")},
            {QStringLiteral("uiAnchor"), QStringLiteral("compare")}, {QStringLiteral("detail"), QStringLiteral("Checking the edited preview.")}}});
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      QThread::msleep(30);
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      window->grabWindow().save(QDir(workspace_animation_output).filePath(QStringLiteral("frame-%1.jpg").arg(frame, 3, 10, QLatin1Char('0'))), "JPG", 85);
      trace.write(QJsonDocument(QJsonObject::fromVariantMap({
          {QStringLiteral("frame"), frame}, {QStringLiteral("elapsedMs"), recording_clock.elapsed()},
          {QStringLiteral("motion"), workspace->property("locomotion")},
          {QStringLiteral("target"), workspace->property("currentTouchTarget").toMap()},
          {QStringLiteral("tool"), character->property("tool")},
          {QStringLiteral("deployment"), character->property("toolDeployProgress")},
          {QStringLiteral("contact"), workspace->property("touchingControl")}})).toJson(QJsonDocument::Compact) + '\n');
    }
  }
  if (!output.isEmpty())
    window->grabWindow().save(QDir(output).filePath(QStringLiteral("workspace-working.png")));
  QMetaObject::invokeMethod(workspace_close, "click");
  settle();
  if (!controller->property("running").toBool() || workspace_robot->isVisible())
    return fail("Closing during work must hide the robot and cursor without cancelling the run");
  QMetaObject::invokeMethod(workspace, "restore");
  motion_settings.insert(QStringLiteral("reducedMotion"), true);
  settle();
  const auto quiet_x = sweep->x();
  settle();
  if (character->property("motionEnabled").toBool() || character->property("legSwing").toDouble() != 0 || sweep->x() != quiet_x)
    return fail("Reduced motion must freeze workspace robot motion and the loader");
  QMetaObject::invokeMethod(find_item(root, QStringLiteral("agentWorkspaceStatusStopButton")), "click");
  settle();
  if (controller->property("running").toBool() || controller->property("canceledReason").toString() != QStringLiteral("user_cancelled"))
    return fail("The workspace Stop control must explicitly cancel the active run");
  QMetaObject::invokeMethod(fast_model, "click");
  settle();
  if (controller->property("selectedModelId").toString() != QStringLiteral("ollama-qwen36-35b") ||
      !controller->property("modelProbeRunning").toBool() || controller->property("modelDownloadCount").toInt() != 0)
    return fail("The failure popup must select and check the faster installed model without downloading another copy");
  return 0;
}

bool capture_session_state(QQuickWindow* window, QQuickItem* root, QQuickItem* screen,
                           const QString& state, const QStringList& item_names,
                           const QString& output) {
  for (const auto size : {QSize(1366, 768), QSize(1440, 900), QSize(1920, 1080)}) {
    window->resize(size);
    settle();
    for (const auto& name : item_names) {
      auto* item = find_item(root, name);
      if (!item || !item->isVisible() || !inside_screen(item, screen)) {
        std::cerr << "Session state " << state.toStdString() << " at "
                  << size.width() << "x" << size.height() << " misses "
                  << name.toStdString() << '\n';
        return false;
      }
    }
    if (!output.isEmpty() &&
        !window->grabWindow().save(QDir(output).filePath(
            QStringLiteral("session-%1-%2x%3.png").arg(state).arg(size.width()).arg(size.height()))))
      return false;
  }
  return true;
}

int verify_session_popup(QQuickWindow* window, QObject* fixture, QObject* controller,
                         QQmlPropertyMap& motion_settings, const QString& output) {
  auto* root = window->contentItem();
  auto* dialog = find_item(root, QStringLiteral("agentRunDialog"));
  auto* screen = find_item(root, QStringLiteral("agentHeldScreen"));
  auto* terminal = find_item(root, QStringLiteral("agentSessionTerminal"));
  if (!dialog || !screen || !terminal)
    return fail("The injected session should open the conversational terminal");
  auto* session = qvariant_cast<QObject*>(dialog->property("sessionController"));
  auto* prompt = find_item(root, QStringLiteral("agentSessionPromptEditor"));
  auto* target = find_item(root, QStringLiteral("agentSessionTargetSummary"));
  auto* inspiration = find_item(root, QStringLiteral("agentSessionUseInspirationButton"));
  auto* continue_button = find_item(root, QStringLiteral("agentSessionSubmitButton"));
  if (!dialog || !screen || !terminal || !session || !prompt || !target || !inspiration || !continue_button ||
      terminal->property("stage").toString() != QStringLiteral("idle"))
    return fail("The injected session should open directly on the conversational request page");

  if (!capture_session_state(window, root, screen, QStringLiteral("prompt"),
          {QStringLiteral("agentSessionPromptEditor"), QStringLiteral("agentSessionTargetSummary"),
           QStringLiteral("agentSessionUseInspirationButton"), QStringLiteral("agentSessionSubmitButton")}, output))
    return fail("The request page and target indicator must fit at the supported window sizes");

  click_item(window, inspiration);
  auto* saved_look = find_item(root, QStringLiteral("agentSessionInspiration-look-2"));
  auto* photo_filter = find_item(root, QStringLiteral("agentSessionInspirationFilter-photos"));
  auto* inspiration_done = find_item(root, QStringLiteral("agentSessionInspirationDoneButton"));
  if (!saved_look || !photo_filter || !inspiration_done ||
      !find_item(root, QStringLiteral("agentSessionInspiration-look-1")))
    return fail("Inspiration should show saved looks with page controls");
  click_item(window, saved_look);
  click_item(window, photo_filter);
  auto* past_photo = find_item(root, QStringLiteral("agentSessionInspiration-asset-6"));
  if (!past_photo || !past_photo->isVisible())
    return fail("Inspiration should include saved past reference photos separately from edit targets");
  click_item(window, past_photo);
  if (!capture_session_state(window, root, screen, QStringLiteral("inspiration"),
          {QStringLiteral("agentSessionInspirationFilter-photos"), QStringLiteral("agentSessionInspiration-asset-6"),
           QStringLiteral("agentSessionInspirationPreviousButton"), QStringLiteral("agentSessionInspirationDoneButton")}, output))
    return fail("Inspiration references and paging controls must stay inside the terminal");
  click_item(window, inspiration_done);

  const QString request = QStringLiteral("Make a thumbnail for the community fundraiser.");
  prompt->setProperty("text", request);
  settle();
  QMetaObject::invokeMethod(continue_button, "click");
  settle();
  if (session->property("stage").toString() != QStringLiteral("clarifying") ||
      session->property("startCount").toInt() != 0 || fixture->property("draftRequestCount").toInt() != 0)
    return fail("Submitting a request may ask questions but must not start an edit before plan approval");
  auto* answer = find_item(root, QStringLiteral("agentSessionAnswerTextField-headline"));
  auto* answer_continue = find_item(root, QStringLiteral("agentSessionQuestionContinueButton"));
  if (!answer || !answer_continue)
    return fail("Clarification should show a focused answer field");
  answer->setProperty("text", QStringLiteral("RIVER FEST 2026"));
  settle();
  if (!capture_session_state(window, root, screen, QStringLiteral("questions"),
          {QStringLiteral("agentSessionAnswerTextField-headline"), QStringLiteral("agentSessionQuestionContinueButton")}, output))
    return fail("Question screens must fit without vertical scrolling");
  click_item(window, answer_continue);
  auto* start = find_item(root, QStringLiteral("agentSessionStartButton"));
  auto* plan_summary = find_item(root, QStringLiteral("agentSessionPlanSummary"));
  auto* exact_copy_review = find_item(root, QStringLiteral("agentSessionExactCopyReviewLabel"));
  if (!start || !plan_summary || session->property("stage").toString() != QStringLiteral("plan-ready") ||
      session->property("startCount").toInt() != 0 || fixture->property("draftRequestCount").toInt() != 0 ||
      !session->property("brief").toMap().value(QStringLiteral("answers")).toMap()
           .value(QStringLiteral("headline")).toString().contains(QStringLiteral("RIVER FEST")) ||
      !session->property("lastContext").toMap().value(QStringLiteral("targetIds")).toList().contains(QStringLiteral("asset-1")) ||
      !session->property("lastContext").toMap().value(QStringLiteral("inspirationIds")).toList().contains(QStringLiteral("asset-6")))
    return fail("The plan must retain answers, concrete targets, and reference IDs without starting early");
  auto proposed_plan = session->property("plan").toMap();
  proposed_plan.insert(QStringLiteral("copySuggestionApprovalRequired"), true);
  proposed_plan.insert(QStringLiteral("exactCopy"), QVariantList{QVariantMap{
      {QStringLiteral("text"), QStringLiteral("RIVER FEST: Join Us")},
      {QStringLiteral("source"), QStringLiteral("proposed")},
      {QStringLiteral("needsApproval"), true}}});
  proposed_plan.insert(QStringLiteral("canvasSettings"), QVariantMap{
      {QStringLiteral("name"), QStringLiteral("YouTube thumbnail")},
      {QStringLiteral("width"), 1280},
      {QStringLiteral("height"), 720},
      {QStringLiteral("resolution"), 144},
      {QStringLiteral("background"), QStringLiteral("transparent")},
      {QStringLiteral("color"), QStringLiteral("#18212b")}});
  session->setProperty("plan", proposed_plan);
  settle();
  auto* exact_copy = find_item(root, QStringLiteral("agentSessionPlanExactCopy"));
  auto* plan_deliverables = find_item(root, QStringLiteral("agentSessionPlanDeliverables"));
  auto* first_plan_action = find_item(root, QStringLiteral("agentSessionPlanActionSummary-0"));
  auto* second_plan_action = find_item(root, QStringLiteral("agentSessionPlanActionSummary-1"));
  auto* next_plan_actions = find_item(root, QStringLiteral("agentSessionPlanActionNextButton"));
  if (!exact_copy_review || !exact_copy ||
      !exact_copy_review->property("text").toString().contains(QStringLiteral("SUGGESTED COPY")) ||
      !exact_copy_review->property("text").toString().contains(QStringLiteral("BEFORE START")) ||
      !exact_copy->property("text").toString().contains(QStringLiteral("Suggested:")) ||
      !exact_copy->property("text").toString().contains(QStringLiteral("RIVER FEST: Join Us")))
    return fail("Model-proposed copy must remain visibly unapproved until Start");
  if (!plan_summary || !first_plan_action || !plan_deliverables ||
      plan_summary->property("font").value<QFont>().pixelSize() < 12 ||
      first_plan_action->property("font").value<QFont>().pixelSize() < 12 ||
      exact_copy->property("font").value<QFont>().pixelSize() < 12 ||
      plan_deliverables->property("font").value<QFont>().pixelSize() < 12)
    return fail("The plan outcome, actions, exact copy, and deliverables should use readable body text");
  if (!first_plan_action || !next_plan_actions ||
      !first_plan_action->property("text").toString().contains(QStringLiteral("approved headline")) ||
      first_plan_action->property("text").toString().contains(QStringLiteral("text")) ||
      (second_plan_action && second_plan_action->isVisible())) {
    std::cerr << "Plan actions page=" << terminal->property("planActionPage").toInt()
              << " pageCount=" << terminal->property("planActionPageCount").toInt()
              << " first=" << (first_plan_action ? first_plan_action->property("text").toString().toStdString() : "<missing>")
              << " secondVisible=" << (second_plan_action && second_plan_action->isVisible())
              << " next=" << (next_plan_actions && next_plan_actions->isVisible()) << '\n';
    return fail("Plan actions should paginate one readable plain-language summary at a time");
  }
  click_item(window, next_plan_actions);
  auto* second_page_action = find_item(root, QStringLiteral("agentSessionPlanActionSummary-1"));
  if (!second_page_action || !second_page_action->isVisible() ||
      !second_page_action->property("text").toString().contains(QStringLiteral("portrait centered")) ||
      second_page_action->property("text").toString().contains(QStringLiteral("framing")))
    return fail("The second plain-language action should appear on its own plan page");
  click_item(window, next_plan_actions);
  auto* third_plan_action = find_item(root, QStringLiteral("agentSessionPlanActionSummary-2"));
  if (!third_plan_action || !third_plan_action->isVisible() ||
      !third_plan_action->property("text").toString().contains(QStringLiteral("source image unchanged")))
    return fail("The final plain-language action should paginate without exposing internal kinds");
  click_item(window, find_item(root, QStringLiteral("agentSessionPlanActionPreviousButton")));
  click_item(window, find_item(root, QStringLiteral("agentSessionPlanActionPreviousButton")));
  auto blank_canvas_brief = session->property("brief").toMap();
  blank_canvas_brief.insert(QStringLiteral("blankCanvas"), true);
  session->setProperty("brief", blank_canvas_brief);
  settle();
  auto* canvas_summary = find_item(root, QStringLiteral("agentSessionPlanCanvasSummary"));
  if (!canvas_summary || !canvas_summary->isVisible() ||
      canvas_summary->property("text").toString() != QStringLiteral("CANVAS · 1280 × 720"))
    return fail("A blank-canvas plan should show only its approved canvas dimensions");
  if (!capture_session_state(window, root, screen, QStringLiteral("blank-canvas-plan-summary"),
          {QStringLiteral("agentSessionPlanCanvasSummary"), QStringLiteral("agentSessionStartButton")}, output))
    return fail("Blank-canvas dimensions and approval should remain inside the plan terminal");
  blank_canvas_brief.insert(QStringLiteral("blankCanvas"), false);
  session->setProperty("brief", blank_canvas_brief);
  settle();
  if (canvas_summary->isVisible())
    return fail("Photo plans should not show a blank-canvas size summary");
  if (!capture_session_state(window, root, screen, QStringLiteral("plan"),
          {QStringLiteral("agentSessionPlanSummary"), QStringLiteral("agentSessionPlanExactCopy"),
           QStringLiteral("agentSessionPlanActionSummary-0"),
           QStringLiteral("agentSessionPlanActionNextButton"),
           QStringLiteral("agentSessionStartButton")}, output))
    return fail("The plan and explicit Start action must fit inside the physical screen");

  click_item(window, start);
  if (session->property("stage").toString() != QStringLiteral("executing") ||
      session->property("startCount").toInt() != 1 || !controller->property("running").toBool() ||
      fixture->property("draftRequestCount").toInt() != 0)
    return fail("Start should be the only action that approves and dispatches the session");
  auto* activity = find_item(root, QStringLiteral("agentSessionWorkingActivity"));
  auto* stop = find_item(root, QStringLiteral("agentSessionStopButton"));
  auto* revert = find_item(root, QStringLiteral("agentSessionRevertButton"));
  if (!activity || !stop || !revert || !activity->property("text").toString().contains(QStringLiteral("approved plan")))
    return fail("Working should show factual status and accessible Stop and Revert controls");
  if (!capture_session_state(window, root, screen, QStringLiteral("working"),
          {QStringLiteral("agentSessionWorkingActivity"), QStringLiteral("agentSessionStopButton"),
           QStringLiteral("agentSessionRevertButton")}, output))
    return fail("Working status and Stop/Revert controls must stay available without scrolling");

  auto* close = find_item(root, QStringLiteral("agentReturnToEditorButton"));
  QMetaObject::invokeMethod(close, "click");
  settle();
  if (dialog->isVisible() || !controller->property("running").toBool())
    return fail("Closing the terminal during work must hide it without cancelling the task");

  // The fake task owner supplies an explicit, structured fixture receipt. The
  // native rig resolves its exposure key to the visible InspectorSlider below;
  // this proves the UI motion contract without presenting it as a production
  // model event or as evidence of a real edit.
  window->resize(1366, 768);
  motion_settings.insert(QStringLiteral("reducedMotion"), false);
  auto* workspace = find_item(root, QStringLiteral("workspacePopupFixture"));
  auto* workspace_robot = find_item(root, QStringLiteral("agentWorkspaceRoamingRobot"));
  auto* workspace_character = find_item(root, QStringLiteral("agentWorkspaceRoamingCharacter"));
  auto* exposure_slider = find_item(root, QStringLiteral("roamingExposureTarget"));
  if (!workspace || !workspace_robot || !workspace_character || !exposure_slider)
    return fail("The session fixture must expose the separate character and real inspector slider");
  const auto motion_output = qEnvironmentVariable("PHOTO_EDITOR_WORKSPACE_ANIMATION_DIR");
  QFile motion_trace;
  QElapsedTimer motion_clock;
  motion_clock.start();
  if (!motion_output.isEmpty()) {
    QDir().mkpath(motion_output);
    motion_trace.setFileName(QDir(motion_output).filePath(QStringLiteral("session-frames.jsonl")));
    if (!motion_trace.open(QIODevice::WriteOnly | QIODevice::Truncate))
      return fail("The session fixture should write its native motion trace");
  }
  int motion_frame = 0;
  bool capture_motion_frames = false;
  bool climbed = false;
  bool jumped = false;
  bool legs_moved = false;
  QImage previous_legs;
  bool tool_extracted = false;
  bool feet_grounded = false;
  bool hand_aligned = false;
  int contact_frames = 0;
  qreal min_actor_x = 0;
  qreal max_actor_x = 0;
  const auto write_motion_row = [&](QVariantMap row) {
    const QString phase = row.value(QStringLiteral("phase")).toString();
    if (motion_trace.isOpen()) {
      if (capture_motion_frames && motion_frame % 2 == 0 &&
          !window->grabWindow().save(QDir(motion_output).filePath(
              QStringLiteral("session-frame-%1.jpg").arg(motion_frame, 4, 10, QLatin1Char('0'))), "JPG", 85)) {
        std::cerr << "Motion frame capture failed frame=" << motion_frame
                  << " phase=" << phase.toStdString()
                  << " elapsedMs=" << motion_clock.elapsed() << '\n';
        return false;
      }
      row.insert(QStringLiteral("frame"), motion_frame);
      row.insert(QStringLiteral("elapsedMs"), motion_clock.elapsed());
      const QByteArray line = QJsonDocument(QJsonObject::fromVariantMap(row)).toJson(QJsonDocument::Compact) + '\n';
      const qint64 written = motion_trace.write(line);
      const bool flushed = motion_trace.flush();
      if (written != static_cast<qint64>(line.size()) || !flushed) {
        std::cerr << "Motion trace write failed frame=" << motion_frame
                  << " phase=" << phase.toStdString()
                  << " bytes=" << written << '/' << line.size()
                  << " flushed=" << flushed
                  << " error=" << motion_trace.errorString().toStdString() << '\n';
        return false;
      }
    }
    ++motion_frame;
    return true;
  };
  workspace->setProperty("dismissed", true);
  fixture->setProperty("workspaceVisible", true);
  const auto await_mapped_geometry = [&](int width, int height) {
    QPointF previous_contact;
    QPointF last_contact;
    int stable_frames = 0;
    for (int frame = 0; frame < 180; ++frame) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      const bool size_matches = window->width() == width && window->height() == height &&
                                qAbs(workspace->width() - width) < 0.5 &&
                                qAbs(workspace->height() - height) < 0.5;
      if (size_matches) {
        const QPointF local_contact = qvariant_cast<QPointF>(exposure_slider->property("agentContactPoint"));
        const QPointF contact = exposure_slider->mapToItem(workspace, local_contact.x(), local_contact.y());
        last_contact = contact;
        stable_frames = stable_frames > 0 && QLineF(contact, previous_contact).length() < 0.5
                        ? stable_frames + 1 : 1;
        previous_contact = contact;
        if (!write_motion_row({{QStringLiteral("phase"), QStringLiteral("geometry-settling")},
                               {QStringLiteral("windowWidth"), window->width()},
                               {QStringLiteral("windowHeight"), window->height()},
                               {QStringLiteral("workspaceWidth"), workspace->width()},
                               {QStringLiteral("workspaceHeight"), workspace->height()},
                               {QStringLiteral("stableFrames"), stable_frames},
                               {QStringLiteral("sizeMatches"), size_matches},
                               {QStringLiteral("controlVisible"), exposure_slider->isVisible()},
                               {QStringLiteral("controlX"), contact.x()},
                               {QStringLiteral("controlY"), contact.y()}}))
          return false;
        if (stable_frames >= 4)
          return true;
      } else {
        stable_frames = 0;
        if (!write_motion_row({{QStringLiteral("phase"), QStringLiteral("geometry-settling")},
                               {QStringLiteral("windowWidth"), window->width()},
                               {QStringLiteral("windowHeight"), window->height()},
                               {QStringLiteral("workspaceWidth"), workspace->width()},
                               {QStringLiteral("workspaceHeight"), workspace->height()},
                               {QStringLiteral("stableFrames"), stable_frames},
                               {QStringLiteral("sizeMatches"), size_matches},
                               {QStringLiteral("controlVisible"), exposure_slider->isVisible()}}))
          return false;
      }
      QThread::msleep(20);
    }
    std::cerr << "Geometry did not settle elapsedMs=" << motion_clock.elapsed()
              << " requested=" << width << 'x' << height
              << " window=" << window->width() << 'x' << window->height()
              << " workspace=" << workspace->width() << 'x' << workspace->height()
              << " slider=" << last_contact.x() << ',' << last_contact.y()
              << " stableFrames=" << stable_frames
              << " visible=" << exposure_slider->isVisible() << '\n';
    return false;
  };
  if (!await_mapped_geometry(1366, 768))
    return fail("The native window and inspector anchor must settle at 1366x768 before character motion starts");

  // Render/proof receipts can carry adjustmentKeys as provenance about the
  // pixels that were checked. Those keys must not send the character back to
  // sliders as though the edit were being applied again.
  const auto render_proof_does_not_replay_adjustments = [workspace](const QString& stage) {
    const QVariantMap event{
        {QStringLiteral("stage"), stage},
        {QStringLiteral("uiAnchor"), QStringLiteral("inspector")},
        {QStringLiteral("evidence"), QVariantMap{
             {QStringLiteral("operation"), QStringLiteral("exposure")},
             {QStringLiteral("adjustmentKeys"), QStringList{QStringLiteral("exposure")}}}}};
    QVariant routed_keys;
    return QMetaObject::invokeMethod(
               workspace, "eventMotionKeys", Qt::DirectConnection,
               Q_RETURN_ARG(QVariant, routed_keys),
               Q_ARG(QVariant, QVariant(event))) && routed_keys.toList().isEmpty();
  };
  if (!render_proof_does_not_replay_adjustments(QStringLiteral("render")) ||
      !render_proof_does_not_replay_adjustments(QStringLiteral("proof")))
    return fail("Render and proof adjustment provenance must not retrigger slider traversal");

  // Inject the explicit UI-fixture receipt only after the actual native
  // inspector geometry is stable. It drives the same anchor resolver used by
  // production activity, but it is not represented as a production receipt.
  // `act` is the real adjustment/application stage; adjustmentKeys are an
  // explicit fixture extension used only to verify mapped slider contact.
  const QVariantMap fixture_receipt{
      {QStringLiteral("stage"), QStringLiteral("act")},
      {QStringLiteral("label"), QStringLiteral("Apply draft")},
      {QStringLiteral("status"), QStringLiteral("running")},
      {QStringLiteral("detail"), QStringLiteral("Applying the approved exposure adjustment to the selected target.")},
      {QStringLiteral("uiAnchor"), QStringLiteral("inspector")},
      {QStringLiteral("fixtureReceipt"), QStringLiteral("agent-session-ui-test")},
      {QStringLiteral("evidence"), QVariantMap{
          {QStringLiteral("assetId"), QStringLiteral("asset-1")},
          {QStringLiteral("operation"), QStringLiteral("apply-draft")},
          {QStringLiteral("adjustmentKeys"), QStringList{QStringLiteral("exposure")}}}}};
  controller->setProperty("runEvents", QVariantList{fixture_receipt});
  const QVariantMap fixture_evidence = fixture_receipt.value(QStringLiteral("evidence")).toMap();
  const QVariantList fixture_adjustment_keys = fixture_evidence.value(QStringLiteral("adjustmentKeys")).toList();
  if (fixture_receipt.value(QStringLiteral("fixtureReceipt")).toString() !=
          QStringLiteral("agent-session-ui-test") ||
      fixture_receipt.value(QStringLiteral("stage")).toString() != QStringLiteral("act") ||
      fixture_receipt.value(QStringLiteral("status")).toString() != QStringLiteral("running") ||
      fixture_adjustment_keys.size() != 1 || fixture_adjustment_keys.constFirst().toString() != QStringLiteral("exposure"))
    return fail("Session character motion must be driven by the clearly identified running fixture receipt");
  QMetaObject::invokeMethod(workspace, "refreshActiveEvent", Qt::DirectConnection);
  workspace->setProperty("dismissed", false);
  min_actor_x = workspace_robot->x();
  max_actor_x = workspace_robot->x();
  QMetaObject::invokeMethod(workspace, "startEventMotion", Qt::DirectConnection);
  capture_motion_frames = true;
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  const QVariantMap event_at_start = workspace->property("activeEvent").toMap();
  if (!write_motion_row({{QStringLiteral("phase"), QStringLiteral("fixture-event-start")},
                         {QStringLiteral("expectedFixtureReceipt"), QStringLiteral("agent-session-ui-test")},
                         {QStringLiteral("fixtureReceipt"), event_at_start.value(QStringLiteral("fixtureReceipt"))},
                         {QStringLiteral("eventStage"), event_at_start.value(QStringLiteral("stage"))},
                         {QStringLiteral("eventStatus"), event_at_start.value(QStringLiteral("status"))},
                         {QStringLiteral("uiAnchor"), event_at_start.value(QStringLiteral("uiAnchor"))},
                         {QStringLiteral("adjustmentKeys"), event_at_start.value(QStringLiteral("evidence")).toMap()
                              .value(QStringLiteral("adjustmentKeys"))},
                         {QStringLiteral("visible"), workspace_robot->isVisible()},
                         {QStringLiteral("traveling"), workspace->property("traveling")},
                         {QStringLiteral("locomotion"), workspace->property("locomotion")}}))
    return fail("The motion trace should flush the fixture event before recording movement");
  if (!workspace_robot->isVisible() ||
      event_at_start.value(QStringLiteral("fixtureReceipt")).toString() != QStringLiteral("agent-session-ui-test")) {
    std::cerr << "Fixture event not active elapsedMs=" << motion_clock.elapsed()
              << " expected=" << fixture_receipt.value(QStringLiteral("fixtureReceipt")).toString().toStdString()
              << " actual=" << event_at_start.value(QStringLiteral("fixtureReceipt")).toString().toStdString()
              << " stage=" << event_at_start.value(QStringLiteral("stage")).toString().toStdString()
              << " status=" << event_at_start.value(QStringLiteral("status")).toString().toStdString()
              << " anchor=" << event_at_start.value(QStringLiteral("uiAnchor")).toString().toStdString()
              << " keys=" << event_at_start.value(QStringLiteral("evidence")).toMap()
                                  .value(QStringLiteral("adjustmentKeys")).toStringList().join(',').toStdString()
              << " visible=" << workspace_robot->isVisible()
              << " dismissed=" << workspace->property("dismissed").toBool()
              << " traveling=" << workspace->property("traveling").toBool()
              << " locomotion=" << workspace->property("locomotion").toString().toStdString() << '\n';
    return fail("The separate character should follow the session fixture receipt while the terminal is hidden");
  }

  const auto record_motion_frame = [&](const QString& phase, const QVariantMap& additional_checks) {
    const QVariantMap target_map = workspace->property("currentTouchTarget").toMap();
    const QVariantMap event_map = workspace->property("activeEvent").toMap();
    const QVariantMap event_evidence = event_map.value(QStringLiteral("evidence")).toMap();
    const QPointF slider_local = qvariant_cast<QPointF>(exposure_slider->property("agentContactPoint"));
    const QPointF slider_position = exposure_slider->mapToItem(workspace, slider_local.x(), slider_local.y());
    const QPointF target_position(target_map.value(QStringLiteral("x")).toDouble(),
                                  target_map.value(QStringLiteral("y")).toDouble());
    const QPointF hand_local = workspace_character->property("toolContactPoint").toPointF();
    const QPointF hand_position = workspace_character->mapToItem(workspace, hand_local);
    const QPointF feet = workspace_character->mapToItem(workspace, QPointF(
        workspace_character->width() / 2, workspace_character->property("feetBaseline").toDouble()));
    const double surface_y = target_map.value(QStringLiteral("surfaceRect")).toMap()
                                 .value(QStringLiteral("y")).toDouble();
    QVariantMap checks{
        {QStringLiteral("climbed"), climbed},
        {QStringLiteral("jumped"), jumped},
        {QStringLiteral("legsMoved"), legs_moved},
        {QStringLiteral("toolExtracted"), tool_extracted},
        {QStringLiteral("feetGrounded"), feet_grounded},
        {QStringLiteral("handAligned"), hand_aligned},
        {QStringLiteral("contactFrames"), contact_frames},
        {QStringLiteral("displacement"), max_actor_x - min_actor_x},
        {QStringLiteral("sliderMapped"), QLineF(slider_position, target_position).length() < 2},
        {QStringLiteral("handTargetDistance"), QLineF(hand_position, target_position).length()},
        {QStringLiteral("feetSurfaceDistance"), qAbs(feet.y() - surface_y)}};
    for (auto it = additional_checks.cbegin(); it != additional_checks.cend(); ++it)
      checks.insert(it.key(), it.value());
    const QVariantMap trace_row{
        {QStringLiteral("phase"), phase},
        {QStringLiteral("expectedFixtureReceipt"), QStringLiteral("agent-session-ui-test")},
        {QStringLiteral("fixtureReceipt"), event_map.value(QStringLiteral("fixtureReceipt"))},
        {QStringLiteral("eventStage"), event_map.value(QStringLiteral("stage"))},
        {QStringLiteral("eventStatus"), event_map.value(QStringLiteral("status"))},
        {QStringLiteral("uiAnchor"), event_map.value(QStringLiteral("uiAnchor"))},
        {QStringLiteral("activeRunEventCount"), controller->property("runEvents").toList().size()},
        {QStringLiteral("adjustmentKeys"), event_evidence.value(QStringLiteral("adjustmentKeys"))},
        {QStringLiteral("locomotion"), workspace->property("locomotion")},
        {QStringLiteral("traveling"), workspace->property("traveling")},
        {QStringLiteral("actorX"), workspace_robot->x()},
        {QStringLiteral("actorY"), workspace_robot->y()},
        {QStringLiteral("target"), target_map},
        {QStringLiteral("tool"), workspace_character->property("tool")},
        {QStringLiteral("toolDeployProgress"), workspace_character->property("toolDeployProgress")},
        {QStringLiteral("touchingControl"), workspace->property("touchingControl")},
        {QStringLiteral("reducedMotion"), workspace->property("reducedMotion")},
        {QStringLiteral("checks"), checks}};
    return write_motion_row(trace_row);
  };

  for (int frame = 0; frame < 240; ++frame) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(30);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const QString locomotion = workspace->property("locomotion").toString();
    climbed = climbed || locomotion == QStringLiteral("climb");
    jumped = jumped || locomotion == QStringLiteral("jump");
    legs_moved = legs_moved || observe_rendered_legs(workspace_character, previous_legs);
    const double deploy = workspace_character->property("toolDeployProgress").toDouble();
    tool_extracted = tool_extracted || (deploy > 0.1 && deploy < 0.9);
    min_actor_x = qMin(min_actor_x, workspace_robot->x());
    max_actor_x = qMax(max_actor_x, workspace_robot->x());

    if (workspace->property("touchingControl").toBool()) {
      const QVariantMap mapped_target = workspace->property("currentTouchTarget").toMap();
      const QPointF local_contact = workspace_character->property("toolContactPoint").toPointF();
      const QPointF hand = workspace_character->mapToItem(workspace, local_contact);
      const QPointF expected(mapped_target.value(QStringLiteral("x")).toDouble(),
                             mapped_target.value(QStringLiteral("y")).toDouble());
      hand_aligned = hand_aligned || QLineF(hand, expected).length() < 2;
      const QPointF feet = workspace_character->mapToItem(workspace, QPointF(
          workspace_character->width() / 2, workspace_character->property("feetBaseline").toDouble()));
      const double surface_y = mapped_target.value(QStringLiteral("surfaceRect")).toMap()
                                   .value(QStringLiteral("y")).toDouble();
      feet_grounded = feet_grounded || qAbs(feet.y() - surface_y) < 2;
      if (mapped_target.value(QStringLiteral("label")).toString() == QStringLiteral("exposure")) {
        ++contact_frames;
        if (!output.isEmpty() && contact_frames == 1 &&
            !window->grabWindow().save(QDir(output).filePath(QStringLiteral("session-fixture-motion-contact-1366x768.png"))))
          return fail("The session fixture should capture native contact at the real exposure slider");
      }
    } else {
      contact_frames = 0;
    }
    const QString motion_phase = workspace->property("touchingControl").toBool()
                                 ? QStringLiteral("exposure-contact")
                                 : workspace->property("traveling").toBool()
                                   ? QStringLiteral("approach")
                                   : deploy > 0.01 ? QStringLiteral("tool-deploy") : QStringLiteral("idle");
    if (!record_motion_frame(motion_phase, {}))
      return fail("The native session motion clip should save every observed travel and contact frame");
    if (contact_frames >= 2)
      break;
  }
  const QPointF exposure_contact = qvariant_cast<QPointF>(exposure_slider->property("agentContactPoint"));
  const QPointF exposure_position = exposure_slider->mapToItem(
      workspace, exposure_contact.x(), exposure_contact.y());
  const QVariantMap contact_target = workspace->property("currentTouchTarget").toMap();
  const QPointF expected_contact(contact_target.value(QStringLiteral("x")).toDouble(),
                                 contact_target.value(QStringLiteral("y")).toDouble());
  const bool mapped_contact_aligned = QLineF(exposure_position, expected_contact).length() < 2;
  const bool displacement_observed = max_actor_x - min_actor_x >= 80;
  const bool initial_motion_valid = climbed && jumped && legs_moved && tool_extracted && feet_grounded &&
                                    hand_aligned && contact_frames >= 2 && displacement_observed &&
                                    contact_target.value(QStringLiteral("label")).toString() == QStringLiteral("exposure") &&
                                    mapped_contact_aligned;
  if (!initial_motion_valid) {
    const QVariantMap active_event = workspace->property("activeEvent").toMap();
    const QVariantMap target_surface = contact_target.value(QStringLiteral("surfaceRect")).toMap();
    std::cerr << "Session motion elapsedMs=" << motion_clock.elapsed()
              << " receipt=" << active_event.value(QStringLiteral("fixtureReceipt")).toString().toStdString()
              << " event=" << active_event.value(QStringLiteral("stage")).toString().toStdString()
              << '/' << active_event.value(QStringLiteral("status")).toString().toStdString()
              << " anchor=" << active_event.value(QStringLiteral("uiAnchor")).toString().toStdString()
              << " key=" << fixture_adjustment_keys.constFirst().toString().toStdString()
              << " climbed=" << climbed << " jumped=" << jumped << " legs=" << legs_moved
              << " toolExtracted=" << tool_extracted << " feetGrounded=" << feet_grounded
              << " handAligned=" << hand_aligned << " contactFrames=" << contact_frames
              << " displacement=" << (max_actor_x - min_actor_x)
              << " target=" << contact_target.value(QStringLiteral("label")).toString().toStdString()
              << '@' << contact_target.value(QStringLiteral("x")).toDouble() << ','
              << contact_target.value(QStringLiteral("y")).toDouble()
              << " surfaceY=" << target_surface.value(QStringLiteral("y")).toDouble()
              << " actualSlider=" << exposure_position.x() << ',' << exposure_position.y()
              << " aligned=" << mapped_contact_aligned
              << " locomotion=" << workspace->property("locomotion").toString().toStdString()
              << " traveling=" << workspace->property("traveling").toBool()
              << " actor=" << workspace_robot->x() << ',' << workspace_robot->y()
              << " deploy=" << workspace_character->property("toolDeployProgress").toDouble() << '\n';
    return fail("The fixture character must travel, climb, deploy its tool, and touch the mapped exposure slider");
  }

  const double old_contact_x = contact_target.value(QStringLiteral("x")).toDouble();
  window->resize(1100, 760);
  if (!await_mapped_geometry(1100, 760))
    return fail("The native window and inspector anchor must settle at 1100x760 before resize contact is checked");
  bool resize_contact_aligned = false;
  bool resize_anchor_moved = false;
  for (int frame = 0; frame < 360; ++frame) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(30);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const QVariantMap mapped_target = workspace->property("currentTouchTarget").toMap();
    resize_anchor_moved = resize_anchor_moved ||
        qAbs(mapped_target.value(QStringLiteral("x")).toDouble() - old_contact_x) > 20;
    const bool touching_exposure = workspace->property("touchingControl").toBool() &&
                                   mapped_target.value(QStringLiteral("label")).toString() == QStringLiteral("exposure");
    bool hand_at_resized_target = false;
    bool slider_at_resized_target = false;
    const QVariantMap active_resize_event = workspace->property("activeEvent").toMap();
    const bool one_original_event_remains = controller->property("runEvents").toList().size() == 1 &&
        active_resize_event.value(QStringLiteral("fixtureReceipt")).toString() ==
            QStringLiteral("agent-session-ui-test");
    if (touching_exposure) {
      const QPointF hand = workspace_character->mapToItem(workspace,
          workspace_character->property("toolContactPoint").toPointF());
      const QPointF target(mapped_target.value(QStringLiteral("x")).toDouble(),
                           mapped_target.value(QStringLiteral("y")).toDouble());
      const QPointF resized_contact = qvariant_cast<QPointF>(exposure_slider->property("agentContactPoint"));
      const QPointF resized_position = exposure_slider->mapToItem(
          workspace, resized_contact.x(), resized_contact.y());
      hand_at_resized_target = QLineF(hand, target).length() < 2;
      slider_at_resized_target = QLineF(resized_position, target).length() < 2;
      resize_contact_aligned = resize_anchor_moved && hand_at_resized_target && slider_at_resized_target &&
                               one_original_event_remains;
      if (resize_contact_aligned) {
        if (!output.isEmpty() &&
            !window->grabWindow().save(QDir(output).filePath(QStringLiteral("session-fixture-motion-contact-resized-1100x760.png"))))
          return fail("The session fixture should capture contact after its inspector target moves on resize");
        if (!record_motion_frame(QStringLiteral("resized-exposure-contact"),
                                 {{QStringLiteral("resizeAnchorMoved"), resize_anchor_moved},
                                  {QStringLiteral("handAtResizedTarget"), hand_at_resized_target},
                                  {QStringLiteral("sliderAtResizedTarget"), slider_at_resized_target},
                                  {QStringLiteral("touchingExposure"), touching_exposure},
                                  {QStringLiteral("oneOriginalEventRemains"), one_original_event_remains},
                                  {QStringLiteral("activeRunEventCount"), controller->property("runEvents").toList().size()}}))
          return fail("The native session motion clip should save its resized contact frame");
        break;
      }
    }
    const QString motion_phase = touching_exposure ? QStringLiteral("resized-exposure-contact")
                                 : workspace->property("traveling").toBool()
                                   ? QStringLiteral("resize-approach") : QStringLiteral("resize-wait");
    if (!record_motion_frame(motion_phase,
                             {{QStringLiteral("resizeAnchorMoved"), resize_anchor_moved},
                              {QStringLiteral("handAtResizedTarget"), hand_at_resized_target},
                              {QStringLiteral("sliderAtResizedTarget"), slider_at_resized_target},
                              {QStringLiteral("touchingExposure"), touching_exposure},
                              {QStringLiteral("oneOriginalEventRemains"), one_original_event_remains},
                              {QStringLiteral("activeRunEventCount"), controller->property("runEvents").toList().size()}}))
      return fail("The native session motion clip should save every resize reanchor frame");
  }
  if (!resize_contact_aligned) {
    const QVariantMap final_target = workspace->property("currentTouchTarget").toMap();
    const QVariantMap active_event = workspace->property("activeEvent").toMap();
    const QPointF hand = workspace_character->mapToItem(workspace,
        workspace_character->property("toolContactPoint").toPointF());
    const QPointF target_point(final_target.value(QStringLiteral("x")).toDouble(),
                               final_target.value(QStringLiteral("y")).toDouble());
    const QPointF final_contact = qvariant_cast<QPointF>(exposure_slider->property("agentContactPoint"));
    const QPointF final_position = exposure_slider->mapToItem(workspace, final_contact.x(), final_contact.y());
    const QVariantList active_keys = active_event.value(QStringLiteral("evidence")).toMap()
                                         .value(QStringLiteral("adjustmentKeys")).toList();
    std::cerr << "Resize elapsedMs=" << motion_clock.elapsed()
              << " receipt=" << active_event.value(QStringLiteral("fixtureReceipt")).toString().toStdString()
              << " event=" << active_event.value(QStringLiteral("stage")).toString().toStdString()
              << '/' << active_event.value(QStringLiteral("status")).toString().toStdString()
              << " anchor=" << active_event.value(QStringLiteral("uiAnchor")).toString().toStdString()
              << " key=" << (active_keys.isEmpty() ? "<none>" : active_keys.constFirst().toString().toStdString())
              << " width=" << window->width() << " workspace=" << workspace->width()
              << " oldTargetX=" << old_contact_x
              << " mappedTarget=" << final_target.value(QStringLiteral("x")).toDouble() << ','
              << final_target.value(QStringLiteral("y")).toDouble()
              << " actualSlider=" << final_position.x() << ',' << final_position.y()
              << " targetDelta=" << QLineF(final_position, target_point).length()
              << " handDelta=" << QLineF(hand, target_point).length()
              << " anchorMoved=" << resize_anchor_moved
              << " oneOriginalEventRemains=" << (active_keys.size() == 1 &&
                     active_event.value(QStringLiteral("fixtureReceipt")).toString() ==
                         QStringLiteral("agent-session-ui-test") &&
                     controller->property("runEvents").toList().size() == 1)
              << " runEventCount=" << controller->property("runEvents").toList().size()
              << " moving=" << workspace->property("traveling").toBool()
              << " touching=" << workspace->property("touchingControl").toBool() << '\n';
    return fail("After window resize, the character must follow the inspector slider's new mapped contact point");
  }

  motion_settings.insert(QStringLiteral("reducedMotion"), true);
  settle();
  const qreal quiet_x = workspace_robot->x();
  const qreal quiet_y = workspace_robot->y();
  settle();
  auto* workspace_close = find_item(root, QStringLiteral("agentWorkspaceStatusCloseButton"));
  if (workspace_character->property("motionEnabled").toBool() || workspace->property("traveling").toBool() ||
      workspace_robot->x() != quiet_x || workspace_robot->y() != quiet_y || !workspace_close ||
      !workspace_close->isVisible() || !controller->property("running").toBool()) {
    std::cerr << "Reduced-motion checks elapsedMs=" << motion_clock.elapsed()
              << " motionEnabled=" << workspace_character->property("motionEnabled").toBool()
              << " traveling=" << workspace->property("traveling").toBool()
              << " stablePosition=" << (workspace_robot->x() == quiet_x && workspace_robot->y() == quiet_y)
              << " closeVisible=" << (workspace_close && workspace_close->isVisible())
              << " taskRunning=" << controller->property("running").toBool()
              << " event=" << workspace->property("activeEvent").toMap()
                                    .value(QStringLiteral("stage")).toString().toStdString()
              << '/' << workspace->property("activeEvent").toMap()
                             .value(QStringLiteral("status")).toString().toStdString()
              << " key=" << fixture_adjustment_keys.constFirst().toString().toStdString() << '\n';
    return fail("Reduced motion should hold the separate character still while the running task remains dismissible");
  }
  if (!output.isEmpty() &&
      !window->grabWindow().save(QDir(output).filePath(QStringLiteral("session-fixture-reduced-motion-1100x760.png"))))
    return fail("The session fixture should capture its readable reduced-motion state");
  if (!record_motion_frame(QStringLiteral("reduced-motion"),
                           {{QStringLiteral("motionDisabled"), !workspace_character->property("motionEnabled").toBool()},
                            {QStringLiteral("dismissible"), workspace_close->isVisible()},
                            {QStringLiteral("taskStillRunning"), controller->property("running").toBool()}}))
    return fail("The native session motion clip should save its reduced-motion frame");
  if (motion_trace.isOpen())
    motion_trace.close();
  fixture->setProperty("workspaceVisible", false);
  settle();

  QMetaObject::invokeMethod(dialog, "open");
  settle();
  stop = find_item(root, QStringLiteral("agentSessionStopButton"));
  QMetaObject::invokeMethod(stop, "click");
  settle();
  if (controller->property("running").toBool() ||
      controller->property("canceledReason").toString() != QStringLiteral("user_requested"))
    return fail("Stop should reach the active task owner after the terminal is reopened");
  const QVariantMap stopped_report{{QStringLiteral("ok"), false},
                                   {QStringLiteral("cancellationReason"), QStringLiteral("user_requested")}};
  QMetaObject::invokeMethod(session, "acknowledgeExecution", Qt::DirectConnection,
                            Q_ARG(QVariant, QVariant(stopped_report)));
  settle();
  if (session->property("stage").toString() != QStringLiteral("interrupted") ||
      !find_item(root, QStringLiteral("agentSessionRetryButton")))
    return fail("An interruption should preserve the request and expose retry");
  if (!capture_session_state(window, root, screen, QStringLiteral("interrupted"),
          {QStringLiteral("agentSessionFailureStatus"), QStringLiteral("agentSessionRetryButton")}, output))
    return fail("Interruption and retry details must fit at all supported sizes");

  auto* retry = find_item(root, QStringLiteral("agentSessionRetryButton"));
  click_item(window, retry);
  if (session->property("stage").toString() != QStringLiteral("planning"))
    return fail("Retry should reuse the retained conversation");
  QMetaObject::invokeMethod(session, "finishPlan");
  settle();
  start = find_item(root, QStringLiteral("agentSessionStartButton"));
  click_item(window, start);
  controller->setProperty("draftActive", true);
  revert = find_item(root, QStringLiteral("agentSessionRevertButton"));
  click_item(window, revert);
  if (controller->property("running").toBool() ||
      controller->property("canceledReason").toString() != QStringLiteral("user_requested_task_revert") ||
      !session->property("revertPending").toBool() || revert->isEnabled())
    return fail("Revert should reach the whole-task restoration owner during work");
  const QVariantMap reverted_report{{QStringLiteral("ok"), false},
                                    {QStringLiteral("cancellationReason"), QStringLiteral("user_requested_task_revert")}};
  QMetaObject::invokeMethod(session, "acknowledgeExecution", Qt::DirectConnection,
                            Q_ARG(QVariant, QVariant(reverted_report)));
  settle();
  retry = find_item(root, QStringLiteral("agentSessionRetryButton"));
  click_item(window, retry);
  QMetaObject::invokeMethod(session, "finishPlan");
  settle();
  start = find_item(root, QStringLiteral("agentSessionStartButton"));
  click_item(window, start);
  controller->setProperty("running", false);
  controller->setProperty("draftActive", true);
  controller->setProperty("lastRunReport", QVariantMap{{QStringLiteral("ok"), true},
                                                          {QStringLiteral("draftActive"), true}});
  const QVariantMap success_report{{QStringLiteral("ok"), true}};
  QMetaObject::invokeMethod(session, "acknowledgeExecution", Qt::DirectConnection,
                            Q_ARG(QVariant, QVariant(success_report)));
  settle();
  auto* review_status = find_item(root, QStringLiteral("agentSessionReviewStatus"));
  auto* revision = find_item(root, QStringLiteral("agentSessionRevisionPrompt"));
  auto* revise = find_item(root, QStringLiteral("agentSessionReviseButton"));
  auto* open_draft = find_item(root, QStringLiteral("agentSessionOpenDraftButton"));
  if (!review_status || !revision || !revise || !open_draft || !open_draft->isVisible() ||
      session->property("stage").toString() != QStringLiteral("review"))
    return fail("A completed task should expose its editable result and revision prompt");
  if (!capture_session_state(window, root, screen, QStringLiteral("result"),
          {QStringLiteral("agentSessionReviewStatus"), QStringLiteral("agentSessionRevisionPrompt"),
           QStringLiteral("agentSessionReviseButton"), QStringLiteral("agentSessionOpenDraftButton")}, output))
    return fail("The editable result and revision controls must fit without scrolling");
  QMetaObject::invokeMethod(close, "click");
  settle();
  QMetaObject::invokeMethod(dialog, "open");
  settle();
  revision = find_item(root, QStringLiteral("agentSessionRevisionPrompt"));
  if (dialog->property("currentPage").toInt() != 0 || !revision || !revision->isVisible() ||
      session->property("stage").toString() != QStringLiteral("review") || !revision->hasActiveFocus())
    return fail("Reopening a completed session should return focus to its result revision prompt");
  revision->setProperty("text", QStringLiteral("Make the headline larger and keep the subject clear."));
  settle();
  click_item(window, revise);
  if (session->property("stage").toString() != QStringLiteral("clarifying") ||
      session->property("submitCount").toInt() != 2 ||
      session->property("brief").toMap().value(QStringLiteral("answers")).toMap()
           .value(QStringLiteral("headline")).toString() != QStringLiteral("RIVER FEST 2026") ||
      session->property("startCount").toInt() != 3)
    return fail("A revision should retain the original answer and require a fresh plan and Start");

  motion_settings.insert(QStringLiteral("reducedMotion"), true);
  settle();
  auto* character = find_item(root, QStringLiteral("agentPopupCharacter"));
  if (!character || character->property("motionEnabled").toBool() || !dialog->isVisible())
    return fail("Reduced motion should stop the character while preserving the readable conversation");
  for (const auto size : {QSize(1366, 768), QSize(1440, 900), QSize(1920, 1080)}) {
    window->resize(size);
    settle();
    if (!inside_screen(find_item(root, QStringLiteral("agentSessionAnswerTextField-headline")), screen) ||
        !inside_screen(find_item(root, QStringLiteral("agentSessionQuestionContinueButton")), screen))
      return fail("A revision question must remain readable in reduced-motion mode");
  }

  controller->setProperty("draftActive", false);
  controller->setProperty("taskTransaction", QVariantMap{});
  auto* defaults = find_item(root, QStringLiteral("agentSessionQuestionDefaultsButton"));
  const auto starts_before_defaults = session->property("startCount").toInt();
  if (!defaults || !defaults->isVisible() || !inside_screen(defaults, screen))
    return fail("The clarification screen must expose a clickable Use your judgment action within the screen");
  click_item(window, defaults);
  settle();
  if (session->property("stage").toString() != QStringLiteral("plan-ready") ||
      !session->property("brief").toMap().value(QStringLiteral("useDefaults")).toBool() ||
      session->property("startCount").toInt() != starts_before_defaults)
    return fail("Use your judgment must reach plan review without dispatching unapproved edits");

  session->setProperty("stage", QStringLiteral("error"));
  session->setProperty("status", QStringLiteral("The task stopped before finishing."));
  QMetaObject::invokeMethod(session, "stateChanged");
  settle();
  auto* failure_revert = find_item(root, QStringLiteral("agentSessionFailureRevertButton"));
  if (!failure_revert || failure_revert->isVisible())
    return fail("Error recovery must not offer Revert when the task owner has no reversible work");
  controller->setProperty("draftActive", true);
  settle();
  failure_revert = find_item(root, QStringLiteral("agentSessionFailureRevertButton"));
  close = find_item(root, QStringLiteral("agentReturnToEditorButton"));
  if (!failure_revert || !failure_revert->isVisible() || !failure_revert->isEnabled() ||
      !close || !close->isVisible())
    return fail("An error with a retained draft must expose Revert while keeping Close available");
  controller->setProperty("taskTransaction", QVariantMap{{QStringLiteral("owner"), QStringLiteral("agent-edit")},
                                                           {QStringLiteral("revertAvailable"), true},
                                                           {QStringLiteral("pending"), true}});
  settle();
  if (!failure_revert->isVisible() || failure_revert->isEnabled())
    return fail("Failure recovery Revert must be disabled while task settlement is pending");
  controller->setProperty("taskTransaction", QVariantMap{{QStringLiteral("owner"), QStringLiteral("agent-edit")},
                                                           {QStringLiteral("revertAvailable"), true}});
  settle();
  if (!capture_session_state(window, root, screen, QStringLiteral("error-recovery"),
          {QStringLiteral("agentSessionFailureStatus"), QStringLiteral("agentSessionFailureRevertButton"),
           QStringLiteral("agentReturnToEditorButton")}, output))
    return fail("Error recovery Revert and Close must remain visible at supported sizes");
  const int revert_requests_before_failure = session->property("requestRevertCount").toInt();
  click_item(window, failure_revert);
  if (session->property("requestRevertCount").toInt() != revert_requests_before_failure + 1 ||
      controller->property("canceledReason").toString() != QStringLiteral("user_requested_task_revert") ||
      controller->property("draftActive").toBool()) {
    auto* terminal = find_item(root, QStringLiteral("agentSessionTerminal"));
    std::cerr << "Failure recovery Revert diagnostics: requestCount="
              << session->property("requestRevertCount").toInt()
              << " priorRequestCount=" << revert_requests_before_failure
              << " owner=" << session->property("revertOwner").toString().toStdString()
              << " canRevert=" << session->property("canRevert").toBool()
              << " revertPending=" << session->property("revertPending").toBool()
              << " canceledReason=" << controller->property("canceledReason").toString().toStdString()
              << " draftActive=" << controller->property("draftActive").toBool()
              << " running=" << controller->property("running").toBool()
              << " buttonVisible=" << failure_revert->isVisible()
              << " buttonEnabled=" << failure_revert->isEnabled()
              << " actionMessage=" << (terminal ? terminal->property("actionMessage").toString().toStdString() : "<missing>")
              << '\n';
    return fail("Failure recovery Revert must use the session owner handler and restore the retained draft");
  }
  const QVariantMap settled_revert{{QStringLiteral("ok"), true}, {QStringLiteral("status"), QStringLiteral("reverted")}};
  QMetaObject::invokeMethod(session, "acknowledgeExecution", Qt::DirectConnection,
                            Q_ARG(QVariant, QVariant(settled_revert)));
  settle();
  if (session->property("stage").toString() != QStringLiteral("interrupted") || !close->isVisible())
    return fail("A settled failure recovery should leave the request interrupted and Close available");
  QMetaObject::invokeMethod(close, "click");
  settle();
  if (dialog->isVisible() || controller->property("running").toBool())
    return fail("Close must still dismiss the recovered failure without starting or cancelling work");
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QAccessible::setActive(true);
  QStringList null_session_terminal_errors;
  QQmlEngine engine;
  QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&null_session_terminal_errors](const QList<QQmlError>& errors) {
    for (const auto& error : errors) {
      const auto message = error.toString();
      if (message.contains(QStringLiteral("AgentSessionTerminal.qml")) &&
          message.contains(QStringLiteral("TypeError")))
        null_session_terminal_errors.push_back(message);
    }
  });
  QQmlPropertyMap motion_settings;
  motion_settings.insert(QStringLiteral("reducedMotion"), false);
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), &motion_settings);
  engine.rootContext()->setContextProperty(QStringLiteral("workspacePhotoPath"),
      qEnvironmentVariable("PHOTO_EDITOR_WORKSPACE_PROOF_PHOTO"));
  engine.rootContext()->setContextProperty(QStringLiteral("sessionFlowOnly"),
      qEnvironmentVariableIsSet("PHOTO_EDITOR_SESSION_FLOW_ONLY"));
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
ApplicationWindow {
    id: fixture
    width: 1280
    height: 800
    visible: true
    color: "#292d33"
    property int draftRequestCount: 0
    property string requestedInstructions: ""
    property string requestedScope: ""
    property bool workspaceVisible: false
    property real proofExposure: 0.18
    property real proofContrast: 24
    Button { id: opener; objectName: "robotPopupOpener"; text: "Open assistant" }
    Image {
        anchors.fill: parent
        anchors.margins: 72
        source: workspacePhotoPath.length > 0 ? "file://" + workspacePhotoPath : ""
        fillMode: Image.PreserveAspectFit
        visible: fixture.workspaceVisible
    }
    Rectangle {
        x: fixture.width - 292; y: 72; width: 292; height: fixture.height - 72
        visible: fixture.workspaceVisible
        color: "#171b21"; border.color: "#45515c"
    }
    Rectangle {
        x: 0; y: 72; width: 64; height: fixture.height - 72
        visible: fixture.workspaceVisible
        color: "#171b21"; border.color: "#45515c"
    }
    Column {
        id: proofInspector
        objectName: "roamingProofInspector"
        visible: fixture.workspaceVisible
        width: 260
        anchors.right: parent.right
        anchors.rightMargin: 16
        y: 96
        spacing: 32
        Label { text: "ADJUST / LIGHT"; color: "#c9ced5"; font.pixelSize: 14 }
        InspectorSlider {
            id: exposureTarget
            objectName: "roamingExposureTarget"
            width: parent.width
            label: "Exposure"
            from: -5
            to: 5
            value: fixture.proofExposure
            valueText: "+0.18 EV"
        }
        InspectorSlider {
            id: contrastTarget
            objectName: "roamingContrastTarget"
            width: parent.width
            label: "Contrast"
            from: -100
            to: 100
            value: fixture.proofContrast
            valueText: "+24%"
        }
        Button { id: compareTarget; text: "Compare before / after" }
    }
    MiniButton {
        id: textTarget
        objectName: "roamingTextTarget"
        x: 12
        y: 430
        visible: fixture.workspaceVisible
        square: true
        iconName: "text"
        role: "icon"
        tooltip: "Text"
    }
    QtObject {
        id: controller
        signal draftChanged()
        signal modelProbeChanged()
        signal pendingAgentInputChanged()
        signal progressivePreviewChanged()
        signal projectTasteChanged()
        property bool running: false
        property bool draftActive: false
        property var taskTransaction: ({})
        property bool canRevertTask: draftActive || Boolean(taskTransaction.revertAvailable)
        property bool modelProbeRunning: false
        property bool modelInstallRunning: false
        property bool gateModelReadiness: false
        property int modelProbeCount: 0
        property int modelDownloadCount: 0
        property string selectedModelId: "managed-vision"
        property var availableModels: [
            {id: "managed-vision", name: "Qwen3 VL 32B", backend: "ollama", runtime: "managed", model: "qwen3-vl:32b", available: false},
            {id: "managed-vision-existing-ollama", name: "Qwen3 VL 32B (existing Ollama)", backend: "ollama", runtime: "external", model: "qwen3-vl:32b", available: false},
            {id: "ollama-qwen36-35b", name: "Qwen3.6 35B (existing Ollama)", backend: "ollama", runtime: "external", model: "qwen3.6:35b", available: false}]
        property bool awaitingUserInput: false
        property var runEvents: []
        property var pendingUserQuestions: []
        property string canceledReason: ""
        property string userInstructions: ""
        property string selectedStyleId: "natural"
        property string editingFreedom: "balanced"
        property var learnedReferenceIds: []
        property var lastRunReport: ({})
        property var availableStyles: {
            const result = [{id: "natural", name: "Natural", source: "built-in"}]
            for (let i = 1; i <= 8; ++i)
                result.push({id: "look-" + i, name: "Warm archive " + i, source: "saved.json",
                    goal: "Keep the warm afternoon look", learnedProfile: {
                        reference_examples: [{assetId: "asset-6"}], styleSummary: "Your past edited look"}})
            return result
        }
        function probeSelectedModelAsync(force) {
            if (force) { modelProbeCount += 1; modelProbeRunning = true; modelProbeChanged() }
        }
        function installSelectedModel(confirmed) { modelDownloadCount += 1 }
        function cancelRun(reason) { canceledReason = reason; running = false; return {ok: true} }
        function revertTask() {
            canceledReason = "user_requested_task_revert"
            running = false
            draftActive = false
            taskTransaction = {owner: "agent-edit", status: "reverted", revertAvailable: false}
            return {ok: true}
        }
        function aiEditWorkflowState(scope) {
            if (gateModelReadiness) {
                const model = availableModels.find(function(entry) { return entry.id === selectedModelId }) || ({})
                if (modelProbeRunning || !model.available)
                    return {primaryAction: {key: modelProbeRunning ? "checking_local_model" : model.runtime === "managed" ? "set_up_local_ai" : "check_local_model", enabled: !modelProbeRunning}}
            }
            return {primaryAction: {key: "create_draft", label: "Create Edit", enabled: true}}
        }
        function createLearnedStyleProfileFromAssets(label, goal, instructions, ids) {
            learnedReferenceIds = ids
            selectedStyleId = "learned-reference"
            return {ok: true}
        }
        function suggestInspirationForPrompt(prompt) { throw new Error("Past inspiration must not generate new suggestions") }

    }
    QtObject {
        id: catalog
        property int assetCount: 6
        property int totalAssetCount: 6
        property int selectedAssetCount: 5
        property int selectedIndex: 0
        property string selectedAssetId: "asset-1"
        property string projectFilter: "all"
        property var selectedAssetIds: ["asset-1", "asset-2", "asset-3", "asset-4", "asset-5"]
        function assetAt(index) {
            return {assetId: "asset-" + (index + 1), filePath: "/photos/Portrait-" + (index + 1) + ".jpg", online: true}
        }
        function assetById(id) { return assetAt(Number(id.split("-")[1]) - 1) }
        function assetsForProject(project) { return [assetAt(0)] }
    }
    QtObject {
        id: taskSession
        signal stateChanged()
        signal activityReceived(string message)
        signal executionRequested(var approvedSnapshot)
        property string stage: "idle"
        property var brief: ({prompt: "", answers: ({}), targetIds: [], targetSnapshot: [],
                              projectId: "", blankCanvas: false, inspirationIds: [], context: ({})})
        property var plan: ({})
        property var questions: []
        property string status: ""
        property string activity: ""
        property bool busy: false
        property bool canStart: false
        property bool canRevert: controller.canRevertTask
        property bool revertRequestPending: false
        property bool revertPending: revertRequestPending || Boolean(controller.taskTransaction.pending) ||
                                    Boolean(controller.lastRunReport.taskSettlementPending)
        property string revertOwner: String(controller.taskTransaction.owner || "agent-edit")
        property int submitCount: 0
        property int startCount: 0
        property int requestRevertCount: 0
        property int contextUpdateCount: 0
        property var lastContext: ({})
        function updateContext(context) {
            lastContext = context
            contextUpdateCount += 1
            brief = Object.assign({}, brief, {
                context: context,
                targetIds: context.targetIds || [],
                targetSnapshot: context.targetSnapshot || [],
                projectId: context.projectId || "",
                blankCanvas: Boolean(context.blankCanvas),
                inspirationIds: context.inspirationIds || []
            })
        }
        function submit(prompt, context) {
            submitCount += 1
            updateContext(context)
            brief = Object.assign({}, brief, {prompt: prompt})
            questions = [{id: "headline", prompt: "What exact headline should appear?", required: true, inputType: "text"}]
            plan = ({})
            canStart = false
            busy = false
            status = "One exact detail will make the plan clear."
            stage = "clarifying"
            stateChanged()
        }
        function planWithDefaults(answers) {
            brief = Object.assign({}, brief, {useDefaults: true})
            answerQuestions(Object.assign({}, brief.answers, answers))
        }
        function answerQuestions(answers) {
            brief = Object.assign({}, brief, {answers: answers})
            questions = []
            plan = {
                summary: "Create an editable thumbnail with the exact headline and clear room around the subject.",
                outcome: "Editable thumbnail",
                targetIds: brief.targetIds,
                targetSnapshot: brief.targetSnapshot,
                exactCopy: [{text: String(answers.headline || ""), source: "user"}],
                actions: [
                    {kind: "text", summary: "Add the approved headline with open space around the subject."},
                    {kind: "framing", summary: "Keep the portrait centered and easy to recognize."},
                    {kind: "layers", summary: "Leave the source image unchanged and keep the result editable."}
                ],
                deliverables: ["Editable thumbnail", "Rendered preview"],
                constraints: ["Keep the original source unchanged"],
                briefVersion: 1
            }
            canStart = true
            status = "Plan ready for approval."
            stage = "plan-ready"
            stateChanged()
        }
        function start() {
            startCount += 1
            canStart = false
            busy = true
            status = "Applying the approved plan."
            activity = "Applying the approved plan to the selected target."
            stage = "executing"
            controller.running = true
            controller.runEvents = []
            executionRequested({contract: "agent-approved-session-v1", briefVersion: 1,
                                prompt: brief.prompt, answers: brief.answers,
                                targetIds: brief.targetIds, inspirationIds: brief.inspirationIds, plan: plan})
            stateChanged()
        }
        function requestStop() { return controller.cancelRun("user_requested") }
        function requestRevert() {
            requestRevertCount += 1
            const report = controller.revertTask()
            revertRequestPending = Boolean(report.ok) && (Boolean(report.pending) || stage === "executing")
            return report
        }
        function cancelPlanning() {
            busy = false
            status = "Planning stopped."
            stage = "interrupted"
            stateChanged()
        }
        function retryPlanning() {
            busy = true
            status = "Checking the request again."
            stage = "planning"
            stateChanged()
        }
        function finishPlan() {
            busy = false
            canStart = true
            status = "Plan ready for approval."
            stage = "plan-ready"
            stateChanged()
        }
        function acknowledgeExecution(report) {
            busy = false
            canStart = false
            if (!(report.queued && report.running))
                revertRequestPending = false
            if (report.status === "reverted") {
                status = "The task was reverted."
                stage = "interrupted"
                stateChanged()
                return
            }
            status = report.ok ? "The task owner finished the editable result." : "The task owner stopped the work."
            stage = report.ok ? "review" : report.cancellationReason ? "interrupted" : "error"
            stateChanged()
        }
        function reset() {
            stage = "idle"
            brief = ({prompt: "", answers: ({}), targetIds: [], targetSnapshot: [],
                      projectId: "", blankCanvas: false, inspirationIds: [], context: ({})})
            plan = ({})
            questions = []
            status = ""
            activity = ""
            busy = false
            canStart = false
            stateChanged()
        }
    }
    AgentWalkthroughDialog {
        id: dialog
        anchors.fill: parent
        agentEditController: controller
        catalogModel: catalog
        sessionController: sessionFlowOnly ? taskSession : null
        onDraftRequested: function(scope, instructions) {
            fixture.draftRequestCount += 1
            fixture.requestedInstructions = instructions
            fixture.requestedScope = scope
        }
    }
    AgentWorkspaceOverlay {
        id: workspaceOverlay
        objectName: "workspacePopupFixture"
        anchors.fill: parent
        visible: fixture.workspaceVisible
        agentEditController: controller
        catalogModel: catalog
        targetAssetIds: ["asset-1"]
        reducedMotion: appShell.reducedMotion
        inspectorWidth: 280
        targetResolver: function(event, key) {
            const target = key === "exposure" ? exposureTarget : key === "contrast" ? contrastTarget
                         : String((event.evidence || ({})).operation || "") === "add_text" ? textTarget
                         : event.uiAnchor === "compare" ? compareTarget : null
            if (!target) return {x: fixture.width * 0.52, y: fixture.height - 72, label: "Photo", interactive: false,
                surfaceRect: {x: 72, y: fixture.height - 72, width: fixture.width - 364},
                panelRect: {x: 72, y: 72, width: fixture.width - 364, height: fixture.height - 144}}
            const contact = target.agentContactPoint || Qt.point(target.width / 2, target.height / 2)
            const point = target.mapToItem(workspaceOverlay, contact.x, contact.y)
            const localSurface = target.agentSurfaceRect || Qt.rect(0, 0, target.width, 0)
            const surface = target.mapToItem(workspaceOverlay, localSurface.x, localSurface.y)
            const panel = target === textTarget ? {x: 0, y: 72, width: 64, height: fixture.height - 72}
                        : {x: fixture.width - 292, y: 72, width: 292, height: fixture.height - 72}
            return {x: point.x, y: point.y, label: key || String((event.evidence || ({})).operation || "Compare"),
                interactive: true, controlName: target.objectName,
                surfaceRect: {x: surface.x, y: surface.y, width: localSurface.width}, panelRect: panel}
        }
    }
    Component.onCompleted: { opener.forceActiveFocus(Qt.TabFocusReason); dialog.open() }
})qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/robot-popup-fixture.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    for (const auto& error : component.errors())
      std::cerr << error.toString().toStdString() << '\n';
    return fail("Robot popup should load the production QML");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  settle();
  auto* root = window->contentItem();
  auto* dialog = find_item(root, QStringLiteral("agentRunDialog"));
  auto* screen = find_item(root, QStringLiteral("agentHeldScreen"));
  auto* prompt = find_visible_enabled_item(root, QStringLiteral("agentPromptEditor"));
  auto* artwork = find_item(root, QStringLiteral("agentRobotScreenArtwork"));
  auto* primary = find_visible_item(root, QStringLiteral("agentPrimaryCreateDraftButton"));
  if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SESSION_FLOW_ONLY") && !null_session_terminal_errors.isEmpty()) {
    std::cerr << "Hidden session terminal null binding errors:\n";
    for (const auto& error : null_session_terminal_errors)
      std::cerr << error.toStdString() << '\n';
    return fail("The hidden session terminal should stay null-safe on the legacy popup path");
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SESSION_FLOW_ONLY")) {
    if (!dialog)
      return fail("The session fixture must load the agent dialog");
    const auto output = qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR");
    if (!output.isEmpty())
      QDir().mkpath(output);
    return verify_session_popup(window, fixture.get(),
        qvariant_cast<QObject*>(dialog->property("agentEditController")), motion_settings, output);
  }
  if (!dialog || !screen || !screen->isVisible() || !prompt || !prompt->isVisible() || !prompt->isEnabled() ||
      !artwork || !primary || !primary->isVisible() || !prompt->hasActiveFocus() || window->activeFocusItem() != prompt) {
    auto* focus = window->activeFocusItem();
    std::cerr << "Popup entry focus diagnostics: dialog=" << (dialog && dialog->isVisible())
              << " page=" << (dialog ? dialog->property("currentPage").toInt() : -1)
              << " hasSession=" << (dialog ? dialog->property("sessionController").value<QObject*>() != nullptr : false)
              << " promptVisible=" << (prompt && prompt->isVisible())
              << " promptEnabled=" << (prompt && prompt->isEnabled())
              << " promptActive=" << (prompt && prompt->hasActiveFocus())
              << " activeFocus=" << (focus ? focus->objectName().toStdString() : "<none>")
              << "\n";
    return fail("Robot popup should open with its interactive prompt focused");
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_WORKSPACE_ONLY")) {
    QMetaObject::invokeMethod(dialog, "close");
    const auto workspace_output = qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR");
    if (!workspace_output.isEmpty()) QDir().mkpath(workspace_output);
    return verify_workspace_popup(window, fixture.get(),
        qvariant_cast<QObject*>(dialog->property("agentEditController")), motion_settings, workspace_output);
  }
  const QImage art(QStringLiteral(":/photo-editor/agent/looksy-agent-screen-v2.png"));
  if (art.isNull() || !art.hasAlphaChannel() || art.width() < 1200 ||
      art.pixelColor(0, 0).alpha() != 0 || art.pixelColor(art.width() / 2, art.height() / 2).alpha() < 240)
    return fail("Robot artwork should be high resolution with a transparent silhouette and opaque screen");
  const QString instructions = QStringLiteral("Warm afternoon light, keep the skin natural.");
  QKeyEvent typing(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, instructions);
  QCoreApplication::sendEvent(window, &typing);
  settle();
  auto* controller = qvariant_cast<QObject*>(dialog->property("agentEditController"));
  if (prompt->property("text").toString() != instructions ||
      controller->property("userInstructions").toString() != instructions ||
      !primary->isVisible() || !primary->isEnabled())
    return fail("Typing on the held screen should update the controller and enable its visible action");
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR");
  if (!output.isEmpty())
    QDir().mkpath(output);
  for (const auto size : {QSize(1280, 800), QSize(1024, 760), QSize(1440, 1000)}) {
    window->resize(size);
    settle();
    if (!prompt->isVisible() || !prompt->isEnabled() || !inside_screen(prompt, screen) ||
        !primary->isVisible() || !primary->isEnabled() || !inside_screen(primary, screen)) {
      const auto prompt_rect = prompt->mapRectToItem(screen, prompt->boundingRect());
      const auto primary_rect = primary->mapRectToItem(screen, primary->boundingRect());
      std::cerr << "Window " << window->width() << "x" << window->height()
                << ", screen " << screen->width() << "x" << screen->height()
                << ", prompt bottom " << prompt_rect.bottom()
                << ", action bottom " << primary_rect.bottom() << '\n';
      return fail("Prompt and primary action should fit inside the robot screen at supported sizes");
    }
    if (!output.isEmpty() && !window->grabWindow().save(QDir(output).filePath(
            QStringLiteral("robot-prompt-%1x%2.png").arg(size.width()).arg(size.height()))))
      return fail("Robot popup should save its rendered prompt proof");
  }
  auto* terminal = find_item(root, QStringLiteral("agentTerminalScreen"));
  auto* checked = find_item(root, QStringLiteral("agentCheckedSelectionAlert"));
  if (!terminal || !checked || !checked->property("text").toString().contains(QStringLiteral("5 FILES CHECKED")))
    return fail("The terminal should show a small accurate readout of checked files");
  if (find_item(root, QStringLiteral("agentHeldScreenScroll")))
    return fail("The held screen must not have a scroll surface");
  controller->setProperty("gateModelReadiness", true);
  controller->setProperty("availableModels", controller->property("availableModels"));
  // Refresh the invokable workflow state just as a real model status notification does.
  controller->setProperty("modelProbeRunning", true);
  controller->setProperty("modelProbeRunning", false);
  settle();
  QMetaObject::invokeMethod(primary, "click");
  settle();
  auto* existing_model = find_item(root, QStringLiteral("agentUseExistingOllamaButton"));
  if (!existing_model || terminal->property("panel").toString() != QStringLiteral("model") || !inside_screen(existing_model, screen))
    return fail("Model setup should offer the existing Ollama connection before any download");
  if (!output.isEmpty())
    window->grabWindow().save(QDir(output).filePath(QStringLiteral("robot-model-connection.png")));
  QMetaObject::invokeMethod(existing_model, "click");
  settle();
  if (controller->property("selectedModelId").toString() != QStringLiteral("managed-vision-existing-ollama") ||
      controller->property("modelProbeCount").toInt() != 1 || controller->property("modelDownloadCount").toInt() != 0 || primary->isEnabled())
    return fail("Using existing Ollama should select and probe the installed copy without downloading or enabling an unchecked edit");
  auto connection_models = controller->property("availableModels").toList();
  auto connected_model = connection_models.at(1).toMap();
  connected_model.insert(QStringLiteral("available"), true);
  connection_models[1] = connected_model;
  controller->setProperty("availableModels", connection_models);
  controller->setProperty("modelProbeRunning", false);
  QMetaObject::invokeMethod(controller, "modelProbeChanged");
  settle();
  if (terminal->property("panel").toString() != QStringLiteral("edit") || !primary->isEnabled() ||
      primary->property("text").toString() != QStringLiteral("Prepare edit >") || prompt->property("text").toString() != instructions)
    return fail("A ready existing model should restore the prompt and edit action immediately after its check");
  controller->setProperty("gateModelReadiness", false);
  QMetaObject::invokeMethod(checked, "click");
  settle();
  if (terminal->property("panel").toString() != QStringLiteral("targets") ||
      terminal->property("targetPageCount").toInt() < 2)
    return fail("Checked files should open a paginated readout");
  terminal->setProperty("panel", QStringLiteral("edit"));
  auto* use_inspiration = find_item(root, QStringLiteral("agentUseInspirationButton"));
  QMetaObject::invokeMethod(use_inspiration, "click");
  settle();
  auto* choice = find_item(root, QStringLiteral("agentSavedInspiration-look-2"));
  if (!choice || dialog->property("currentPage").toInt() != 1 ||
      dialog->property("inspirationSuggestions").toList().size() != 9 ||
      terminal->property("archivePageCount").toInt() < 2)
    return fail("Use inspiration should open stored looks and deduplicated past references, with pagination");
  choice->forceActiveFocus(Qt::TabFocusReason);
  QKeyEvent choose(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &choose);
  QKeyEvent choose_release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &choose_release);
  settle();
  if (!choice->property("selected").toBool() || !inside_screen(choice, screen))
    return fail("Keyboard selection should attach a saved inspiration within the screen");
  for (const auto size : {QSize(1024, 760), QSize(1280, 800)}) {
    window->resize(size);
    settle();
    for (const auto& name : {QStringLiteral("agentArchivePrevious"), QStringLiteral("agentArchiveNext"),
                            QStringLiteral("agentChangePromptButton"), QStringLiteral("agentUseSavedInspirationButton")}) {
      if (!inside_screen(find_item(root, name), screen))
        return fail("Archive navigation must fit the physical screen without scrolling");
    }
  }
  auto* next_archive = find_item(root, QStringLiteral("agentArchiveNext"));
  QMetaObject::invokeMethod(next_archive, "click");
  settle();
  if (terminal->property("archivePage").toInt() != 1)
    return fail("Archive navigation should replace rows without scrolling");
  terminal->setProperty("archivePage", 0);
  terminal->setProperty("archiveFilter", QStringLiteral("references"));
  settle();
  if (!find_item(root, QStringLiteral("agentSavedInspiration-asset-6")))
    return fail("Past references should come from the saved look, without including checked targets");
  terminal->setProperty("archiveFilter", QStringLiteral("looks"));
  settle();
  if (!output.isEmpty())
    window->grabWindow().save(QDir(output).filePath(QStringLiteral("robot-inspiration.png")));
  QMetaObject::invokeMethod(dialog, "setPage", Q_ARG(QVariant, 0));
  settle();
  if (window->activeFocusItem() != prompt || prompt->property("text").toString() != instructions)
    return fail("Returning to the prompt should preserve typed instructions and focus");
  prompt->setProperty("text", QString());
  settle();
  QMetaObject::invokeMethod(primary, "click");
  settle();
  if (fixture->property("draftRequestCount").toInt() != 1 ||
      fixture->property("requestedInstructions").toString() != QStringLiteral("Keep the warm afternoon look"))
    return fail("Saved inspiration should prepare an edit without requiring a typed prompt");
  QMetaObject::invokeMethod(dialog, "setPage", Q_ARG(QVariant, 0));
  dialog->setProperty("selectedSuggestionKeys", QVariantList{QStringLiteral("photo:asset-6")});
  settle();
  QMetaObject::invokeMethod(primary, "click");
  settle();
  if (fixture->property("draftRequestCount").toInt() != 2 ||
      controller->property("learnedReferenceIds").toList() != QVariantList{QStringLiteral("asset-6")} ||
      fixture->property("requestedScope").toString() != QStringLiteral("selected-assets"))
    return fail("Past-reference editing should learn from the chosen reference without treating it as an edit target");
  QMetaObject::invokeMethod(dialog, "setPage", Q_ARG(QVariant, 0));
  dialog->setProperty("selectedSuggestionKeys", QVariantList{});
  prompt->setProperty("text", instructions);
  settle();
  QMetaObject::invokeMethod(primary, "click");
  settle();
  if (fixture->property("draftRequestCount").toInt() != 3 ||
      fixture->property("requestedInstructions").toString() != instructions)
    return fail("Prompt-only editing should use the user's words without opening inspiration");
  controller->setProperty("running", true);
  const QVariantMap model_output{
      {QStringLiteral("stage"), QStringLiteral("plan-worker")},
      {QStringLiteral("status"), QStringLiteral("complete")},
      {QStringLiteral("elapsedMsSinceRunStart"), 12000},
      {QStringLiteral("evidence"), QVariantMap{
          {QStringLiteral("modelOutputAvailable"), true},
          {QStringLiteral("modelOutput"), QStringLiteral("Warm the highlights and preserve natural skin tones.")}}}};
  const QVariantMap rendering{
      {QStringLiteral("stage"), QStringLiteral("render")},
      {QStringLiteral("status"), QStringLiteral("running")},
      {QStringLiteral("uiAnchor"), QStringLiteral("inspector")},
      {QStringLiteral("detail"), QStringLiteral("Rendering the edited preview.")},
      {QStringLiteral("elapsedMsSinceRunStart"), 13000}};
  controller->setProperty("runEvents", QVariantList{model_output, rendering});
  settle();
  auto* activity = find_item(root, QStringLiteral("agentRunActivityBar"));
  auto* robot_head = find_item(root, QStringLiteral("agentPopupCharacter"));
  auto* robot_stage = find_item(root, QStringLiteral("agentRobotStage"));
  auto* stop = find_item(root, QStringLiteral("agentStopRunButton"));
  auto* view_editor = find_item(root, QStringLiteral("agentViewRunningEditorButton"));
  if (!dialog->isVisible() || dialog->property("currentPage").toInt() != 2 || !activity || !robot_head ||
      terminal->property("activeStage").toInt() != 2 || !stop->isEnabled() || !view_editor->isVisible())
    return fail("Running should keep the robot terminal open with the active edit stage and safe controls");
  const auto rows = terminal->property("outputRows").toList();
  if (rows.size() != 2 || rows.first().toMap().value(QStringLiteral("source")).toString() != QStringLiteral("MODEL") ||
      !rows.first().toMap().value(QStringLiteral("text")).toString().contains(QStringLiteral("skin tones")))
    return fail("The live feed should distinguish actual model output from system events");
  const auto sweep_before = activity->property("sweep").toDouble();
  const auto head_before = robot_stage->property("servoAngle").toDouble();
  settle();
  if (qAbs(activity->property("sweep").toDouble() - sweep_before) < 0.01 || head_before != 3.0)
    return fail("The loading bar should animate and the robot head should react to the actual workspace stage");
  for (const auto size : {QSize(1024, 760), QSize(1280, 800)}) {
    window->resize(size);
    settle();
    if (!inside_screen(activity, screen) || !inside_screen(stop, screen) || !inside_screen(view_editor, screen) ||
        !inside_screen(find_item(root, QStringLiteral("agentLiveOutput-1")), screen))
      return fail("Live output and running controls must fit without screen scrolling");
    if (!output.isEmpty())
      window->grabWindow().save(QDir(output).filePath(QStringLiteral("robot-working-%1x%2.png").arg(size.width()).arg(size.height())));
  }
  const auto animation_output = qEnvironmentVariable("PHOTO_EDITOR_ANIMATION_PROOF_DIR");
  if (!animation_output.isEmpty()) {
    QDir().mkpath(animation_output);
    // Capture the production rig and terminal responding to a completed model stage.
    controller->setProperty("runEvents", QVariantList{model_output});
    for (int frame = 0; frame < 50; ++frame) {
      if (frame == 20)
        controller->setProperty("runEvents", QVariantList{model_output, rendering});
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      QThread::msleep(60);
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      if (!window->grabWindow().save(QDir(animation_output).filePath(QStringLiteral("frame-%1.png").arg(frame, 3, 10, QLatin1Char('0')))))
        return fail("The native animation should save each visual proof frame");
    }
  }
  controller->setProperty("awaitingUserInput", true);
  controller->setProperty("pendingUserQuestions", QVariantList{QVariantMap{{QStringLiteral("question"), QStringLiteral("Keep the original crop?")}}});
  settle();
  if (!terminal->property("awaitingInput").toBool() || view_editor->property("text").toString() != QStringLiteral("Answer in editor >"))
    return fail("A model question should pause activity and offer the editor for an answer");
  controller->setProperty("awaitingUserInput", false);
  motion_settings.insert(QStringLiteral("reducedMotion"), true);
  settle();
  const auto static_sweep = activity->property("sweep").toDouble();
  settle();
  if (robot_stage->property("motionActive").toBool() || robot_stage->property("servoAngle").toDouble() != 0.0 ||
      activity->property("sweep").toDouble() != static_sweep)
    return fail("Reduced motion should stop the head and loader while preserving readable status");
  QMetaObject::invokeMethod(view_editor, "click");
  settle();
  if (dialog->isVisible() || !controller->property("running").toBool())
    return fail("Viewing the editor during a run should hide the popup without canceling the model");
  QMetaObject::invokeMethod(dialog, "open");
  settle();
  QMetaObject::invokeMethod(stop, "click");
  settle();
  if (controller->property("running").toBool() || controller->property("canceledReason").toString() != QStringLiteral("user_requested"))
    return fail("Stop should cancel the current request explicitly");
  QMetaObject::invokeMethod(dialog, "setPage", Q_ARG(QVariant, 0));
  settle();
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &escape);
  settle();
  if (dialog->isVisible() || window->activeFocusItem() != find_item(root, QStringLiteral("robotPopupOpener")))
    return fail("Escape should close the robot popup and return focus to its opener");

  return verify_workspace_popup(window, fixture.get(), controller, motion_settings, output);
}
