#include <QCoreApplication>
#include <QJSValue>
#include <QJsonDocument>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QUrl>
#include <QVariant>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
QVariant invoke(QObject* object, const char* method, const QVariantList& arguments = {}) {
  QVariant result;
  bool invoked = false;
  switch (arguments.size()) {
    case 0:
      invoked = QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result));
      break;
    case 1:
      invoked = QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result),
                                          Q_ARG(QVariant, arguments[0]));
      break;
    case 2:
      invoked = QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result),
                                          Q_ARG(QVariant, arguments[0]), Q_ARG(QVariant, arguments[1]));
      break;
    case 3:
      invoked = QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result),
                                          Q_ARG(QVariant, arguments[0]), Q_ARG(QVariant, arguments[1]),
                                          Q_ARG(QVariant, arguments[2]));
      break;
    default:
      std::cerr << "Unsupported QML invocation argument count\n";
      std::exit(EXIT_FAILURE);
  }
  if (!invoked) {
    std::cerr << "Could not invoke " << method << '\n';
    std::exit(EXIT_FAILURE);
  }
  if (result.canConvert<QJSValue>())
    return result.value<QJSValue>().toVariant();
  return result;
}

bool invokeBool(QObject* object, const char* method, const QVariantList& arguments = {}) {
  return invoke(object, method, arguments).toBool();
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

std::unique_ptr<QObject> createState(QQmlEngine& engine, QString* error = nullptr) {
  QQmlComponent component(&engine);
  component.setData(QByteArrayLiteral(
      "import QtQml\n"
      "import \"file:" PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components\"\n"
      "TimelinePanelDockState {}"), QUrl::fromLocalFile(QStringLiteral("timeline-panel-dock-state-test.qml")));
  QObject* created = component.create();
  if (created == nullptr && error != nullptr)
    *error = component.errorString();
  return std::unique_ptr<QObject>(created);
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QQmlEngine engine;
  QString creation_error;
  std::unique_ptr<QObject> state = createState(engine, &creation_error);
  if (!state) {
    std::cerr << creation_error.toStdString() << '\n';
    return fail("TimelinePanelDockState should load as a pure QML state model");
  }

  QVariant panel_ids_value = state->property("panelIds");
  if (panel_ids_value.canConvert<QJSValue>())
    panel_ids_value = panel_ids_value.value<QJSValue>().toVariant();
  if (panel_ids_value.toList().size() != 12 ||
      !invokeBool(state.get(), "visible", {QStringLiteral("effects")}) ||
      !invokeBool(state.get(), "visible", {QStringLiteral("inspector")}) ||
      !invokeBool(state.get(), "visible", {QStringLiteral("mixer")}) ||
      !invokeBool(state.get(), "visible", {QStringLiteral("color")}) ||
      !invokeBool(state.get(), "visible", {QStringLiteral("titles")}) ||
      invoke(state.get(), "dock", {QStringLiteral("mixer")}).toString() != QStringLiteral("left") ||
      invoke(state.get(), "dock", {QStringLiteral("color")}).toString() != QStringLiteral("right") ||
      invoke(state.get(), "dock", {QStringLiteral("titles")}).toString() != QStringLiteral("right") ||
      invoke(state.get(), "dock", {QStringLiteral("media")}).toString() != QStringLiteral("left") ||
      invoke(state.get(), "dock", {QStringLiteral("inspector")}).toString() != QStringLiteral("left") ||
      invoke(state.get(), "dock", {QStringLiteral("effects")}).toString() != QStringLiteral("right") ||
      invokeBool(state.get(), "visible", {QStringLiteral("keyframes")}) ||
      invoke(state.get(), "dock", {QStringLiteral("keyframes")}).toString() != QStringLiteral("right") ||
      invoke(state.get(), "order", {QStringLiteral("media")}).toInt() >=
          invoke(state.get(), "order", {QStringLiteral("inspector")}).toInt() ||
      invoke(state.get(), "activePanel", {QStringLiteral("left")}).toString() != QStringLiteral("inspector") ||
      invoke(state.get(), "activePanel", {QStringLiteral("right")}).toString() != QStringLiteral("effects"))
    return fail("The dock model should expose all named panels and a usable Edit default");

  std::unique_ptr<QObject> legacy_docked_curve = createState(engine);
  QVariantMap legacy_snapshot = invoke(legacy_docked_curve.get(), "snapshot").toMap();
  QVariantMap legacy_panels = legacy_snapshot.value("panels").toMap();
  QVariantMap legacy_keyframes = legacy_panels.value("keyframes").toMap();
  legacy_keyframes.insert("visible", true);
  legacy_keyframes.insert("dock", "bottom");
  legacy_keyframes.insert("returnDock", "bottom");
  legacy_panels.insert("keyframes", legacy_keyframes);
  legacy_snapshot.insert("panels", legacy_panels);
  QVariantMap legacy_active = legacy_snapshot.value("activePanelByDock").toMap();
  legacy_active.insert("bottom", "keyframes");
  legacy_snapshot.insert("activePanelByDock", legacy_active);
  invoke(legacy_docked_curve.get(), "restore", {legacy_snapshot});
  if (!invokeBool(legacy_docked_curve.get(), "migrateLegacyEditingKeyframes") ||
      invokeBool(legacy_docked_curve.get(), "visible", {QStringLiteral("keyframes")}) ||
      invoke(legacy_docked_curve.get(), "dock", {QStringLiteral("keyframes")}).toString() != QStringLiteral("right") ||
      invoke(legacy_docked_curve.get(), "activePanel", {QStringLiteral("right")}).toString() != QStringLiteral("effects") ||
      !invoke(legacy_docked_curve.get(), "activePanel", {QStringLiteral("bottom")}).toString().isEmpty() ||
      invokeBool(legacy_docked_curve.get(), "migrateLegacyEditingKeyframes"))
    return fail("An exact legacy docked curve should migrate to a hidden right-dock tab idempotently");

  std::unique_ptr<QObject> custom_detached = createState(engine);
  QVariantMap detached_snapshot = invoke(custom_detached.get(), "snapshot").toMap();
  QVariantMap detached_panels = detached_snapshot.value("panels").toMap();
  QVariantMap detached_keyframes = detached_panels.value("keyframes").toMap();
  detached_keyframes.insert("visible", true);
  detached_keyframes.insert("dock", "bottom");
  detached_keyframes.insert("returnDock", "bottom");
  detached_panels.insert("keyframes", detached_keyframes);
  QVariantMap detached_titles = detached_panels.value("titles").toMap();
  detached_titles.insert("visible", true);
  detached_titles.insert("dock", "floating");
  detached_titles.insert("returnDock", "bottom");
  detached_panels.insert("titles", detached_titles);
  detached_snapshot.insert("panels", detached_panels);
  QVariantMap detached_active = detached_snapshot.value("activePanelByDock").toMap();
  detached_active.insert("bottom", "keyframes");
  detached_snapshot.insert("activePanelByDock", detached_active);
  invoke(custom_detached.get(), "restore", {detached_snapshot});
  if (invokeBool(custom_detached.get(), "migrateLegacyEditingKeyframes") ||
      !invokeBool(custom_detached.get(), "visible", {QStringLiteral("keyframes")}) ||
      invoke(custom_detached.get(), "dock", {QStringLiteral("keyframes")}).toString() != QStringLiteral("bottom") ||
      invoke(custom_detached.get(), "dock", {QStringLiteral("titles")}).toString() != QStringLiteral("floating"))
    return fail("Legacy migration must preserve custom detached dock arrangements");

  QVariant definitions_value = state->property("panelDefinitions");
  if (definitions_value.canConvert<QJSValue>())
    definitions_value = definitions_value.value<QJSValue>().toVariant();
  bool controls_panel_named_clearly = false;
  for (const auto& entry : definitions_value.toList()) {
    const auto definition = entry.toMap();
    if (definition.value(QStringLiteral("id")).toString() == QStringLiteral("inspector"))
      controls_panel_named_clearly =
          definition.value(QStringLiteral("label")).toString() == QStringLiteral("Controls");
  }
  if (!controls_panel_named_clearly)
    return fail("The left editing panel should be labeled Controls in the panel switcher");

  std::unique_ptr<QObject> factory_color = createState(engine);
  if (!factory_color || !invokeBool(factory_color.get(), "applyPreset", {QStringLiteral("color")}) ||
      !invokeBool(factory_color.get(), "visible", {QStringLiteral("scopes")}) ||
      invoke(factory_color.get(), "activePanel", {QStringLiteral("bottom")}).toString() != QStringLiteral("scopes") ||
      invoke(factory_color.get(), "activePanel", {QStringLiteral("right")}).toString() != QStringLiteral("color"))
    return fail("Color should keep factory Scopes active when no user-selected bottom panel exists");

  std::unique_ptr<QObject> primary_inspector = createState(engine);
  if (!primary_inspector ||
      !invokeBool(primary_inspector.get(), "focusPanel", {QStringLiteral("inspector")}) ||
      !invokeBool(primary_inspector.get(), "applyPreset", {QStringLiteral("color")}) ||
      invoke(primary_inspector.get(), "activePanel", {QStringLiteral("right")}).toString() != QStringLiteral("color") ||
      !invoke(primary_inspector.get(), "activePanel", {QStringLiteral("auxiliary")}).toString().isEmpty())
    return fail("Color must restore its own focused panel without a second side dock");

  std::unique_ptr<QObject> preserved_mixer = createState(engine);
  if (!preserved_mixer ||
      !invokeBool(preserved_mixer.get(), "focusPanel", {QStringLiteral("mixer")}) ||
      !invokeBool(preserved_mixer.get(), "applyPreset", {QStringLiteral("color")}) ||
      invokeBool(preserved_mixer.get(), "visible", {QStringLiteral("mixer")}) ||
      !invokeBool(preserved_mixer.get(), "visible", {QStringLiteral("scopes")}) ||
      invoke(preserved_mixer.get(), "activePanel", {QStringLiteral("bottom")}).toString() != QStringLiteral("scopes") ||
      invoke(preserved_mixer.get(), "activePanel", {QStringLiteral("right")}).toString() != QStringLiteral("color") ||
      !invoke(preserved_mixer.get(), "activePanel", {QStringLiteral("auxiliary")}).toString().isEmpty())
    return fail("Switching to Color must restore Color visibility instead of accumulating outgoing panels");

  std::unique_ptr<QObject> audio_finish = createState(engine);
  if (!audio_finish ||
      !invokeBool(audio_finish.get(), "applyPreset", {QStringLiteral("color")}) ||
      !invokeBool(audio_finish.get(), "focusPanel", {QStringLiteral("mixer")}) ||
      !invokeBool(audio_finish.get(), "focusPanel", {QStringLiteral("titles")}) ||
      !invokeBool(audio_finish.get(), "applyPreset", {QStringLiteral("audio")}) ||
      !invokeBool(audio_finish.get(), "visible", {QStringLiteral("mixer")}) ||
      invokeBool(audio_finish.get(), "visible", {QStringLiteral("titles")}) ||
      invoke(audio_finish.get(), "activePanel", {QStringLiteral("bottom")}).toString() != QStringLiteral("mixer") ||
      !invokeBool(audio_finish.get(), "focusPanel", {QStringLiteral("titles")}) ||
      !invokeBool(audio_finish.get(), "applyPreset", {QStringLiteral("finish")}) ||
      invokeBool(audio_finish.get(), "visible", {QStringLiteral("mixer")}) ||
      !invokeBool(audio_finish.get(), "visible", {QStringLiteral("captions")}) ||
      invoke(audio_finish.get(), "activePanel", {QStringLiteral("bottom")}).toString() != QStringLiteral("captions"))
    return fail("Audio and Finish must each restore their own panel set");

  std::unique_ptr<QObject> implicit_active = createState(engine);
  if (!implicit_active ||
      !invokeBool(implicit_active.get(), "focusPanel", {QStringLiteral("mixer")}))
    return fail("A manual Mixer selection should be available before applying a partial arrangement");
  QVariant color_arrangement = invoke(implicit_active.get(), "presetSnapshot", {QStringLiteral("color")});
  QVariantMap partial_arrangement = color_arrangement.toMap();
  partial_arrangement.remove(QStringLiteral("activePanelByDock"));
  if (!invokeBool(implicit_active.get(), "applyArrangement",
                  {QStringLiteral("color"), partial_arrangement}) ||
      invoke(implicit_active.get(), "activePanel", {QStringLiteral("bottom")}).toString() != QStringLiteral("scopes"))
    return fail("A partial arrangement must choose a visible tab from its target layout");

  if (!invokeBool(state.get(), "focusPanel", {QStringLiteral("mixer")}) ||
      !invokeBool(state.get(), "visible", {QStringLiteral("mixer")}) ||
      invoke(state.get(), "activePanel", {QStringLiteral("left")}).toString() != QStringLiteral("mixer") ||
      !invokeBool(state.get(), "visible", {QStringLiteral("effects")}))
    return fail("Focusing a closed panel must reveal it without closing other open panels");
  if (!invokeBool(state.get(), "togglePanel", {QStringLiteral("mixer")}) ||
      invokeBool(state.get(), "visible", {QStringLiteral("mixer")}) ||
      !invokeBool(state.get(), "focusPanel", {QStringLiteral("mixer")}))
    return fail("Explicit panel visibility toggles should close a panel and focus should reopen it");

  if (!invokeBool(state.get(), "movePanel", {QStringLiteral("color"), QStringLiteral("auxiliary"), 0}) ||
      !invokeBool(state.get(), "focusPanel", {QStringLiteral("color")}) ||
      !invokeBool(state.get(), "focusPanel", {QStringLiteral("effects")}) ||
      !invokeBool(state.get(), "focusPanel", {QStringLiteral("mixer")}) ||
      !invokeBool(state.get(), "visible", {QStringLiteral("color")}) ||
      !invokeBool(state.get(), "visible", {QStringLiteral("effects")}) ||
      !invokeBool(state.get(), "visible", {QStringLiteral("mixer")}))
    return fail("Effects, Color, and Mixer must retain independent open state across docks");

  if (!invokeBool(state.get(), "floatPanel", {QStringLiteral("mixer"), QVariantMap{}}) ||
      invoke(state.get(), "dock", {QStringLiteral("mixer")}).toString() != QStringLiteral("floating") ||
      invoke(state.get(), "activePanel", {QStringLiteral("floating")}).toString() != QStringLiteral("mixer") ||
      !invokeBool(state.get(), "setFloatingGeometry",
                  {QStringLiteral("mixer"), QVariantMap{{QStringLiteral("x"), -20000},
                                                        {QStringLiteral("y"), 340},
                                                        {QStringLiteral("width"), 100},
                                                        {QStringLiteral("height"), 1600}}}) ||
      invoke(state.get(), "snapshot").toMap().value("panels").toMap()
              .value("mixer").toMap().value("floatingGeometry").toMap()
              .value("x").toInt() != -10000 ||
      invoke(state.get(), "snapshot").toMap().value("panels").toMap()
              .value("mixer").toMap().value("floatingGeometry").toMap()
              .value("width").toInt() != 220 ||
      invoke(state.get(), "snapshot").toMap().value("panels").toMap()
              .value("mixer").toMap().value("floatingGeometry").toMap()
              .value("height").toInt() != 1200 ||
      !invokeBool(state.get(), "dockPanel", {QStringLiteral("mixer"), QString(), 999}) ||
      invoke(state.get(), "dock", {QStringLiteral("mixer")}).toString() != QStringLiteral("left") ||
      invoke(state.get(), "activePanel", {QStringLiteral("left")}).toString() != QStringLiteral("mixer"))
    return fail("Float and Dock must preserve the panel's previous dock and active tab");

  if (!invokeBool(state.get(), "movePanel", {QStringLiteral("color"), QStringLiteral("right"), 0}) ||
      invoke(state.get(), "order", {QStringLiteral("color")}).toInt() != 0 ||
      invoke(state.get(), "activePanel", {QStringLiteral("right")}).toString() != QStringLiteral("color"))
    return fail("Moving and reordering a panel must update its dock membership and active tab");

  if (!invokeBool(state.get(), "rememberPreset", {QStringLiteral("edit")}) ||
      !invokeBool(state.get(), "rememberPreset", {QStringLiteral("color")}) ||
      !invokeBool(state.get(), "movePanel", {QStringLiteral("mixer"), QStringLiteral("auxiliary"), 0}) ||
      !invokeBool(state.get(), "resetPreset", {QStringLiteral("color")}) ||
      !invokeBool(state.get(), "applyPreset", {QStringLiteral("edit")}) ||
      !invokeBool(state.get(), "visible", {QStringLiteral("mixer")}) ||
      invoke(state.get(), "dock", {QStringLiteral("mixer")}).toString() != QStringLiteral("left"))
    return fail("Named panel presets should restore arrangements without closing manually opened panels");
  const QVariantMap saved_presets = state->property("savedPresets").toMap();
  if (!saved_presets.contains(QStringLiteral("edit")) || saved_presets.contains(QStringLiteral("color")))
    return fail("Resetting one panel preset must preserve other saved arrangements");

  const auto saved = invoke(state.get(), "snapshot");
  const QString serialized = QString::fromUtf8(QJsonDocument::fromVariant(saved).toJson());
  std::unique_ptr<QObject> reopened = createState(engine);
  if (!reopened || !invokeBool(reopened.get(), "restore", {serialized}) ||
      invoke(reopened.get(), "dock", {QStringLiteral("mixer")}).toString() != QStringLiteral("left") ||
      invoke(reopened.get(), "activePanel", {QStringLiteral("right")}).toString() != QStringLiteral("color") ||
      !invokeBool(reopened.get(), "visible", {QStringLiteral("mixer")}))
    return fail("Dock tabs, visibility, presets, and active panels must survive model recreation");

  invoke(reopened.get(), "restore", {QStringLiteral(
      "{broken")});
  if (!invokeBool(reopened.get(), "visible", {QStringLiteral("effects")}) ||
      invoke(reopened.get(), "dock", {QStringLiteral("effects")}).toString() != QStringLiteral("right"))
    return fail("Malformed panel preferences must restore to usable defaults");

  invoke(reopened.get(), "restore", {QStringLiteral(
      "{\"activePreset\":\"audio\",\"panels\":{\"mixer\":{\"visible\":false,"
      "\"dock\":\"invalid\",\"returnDock\":\"floating\",\"order\":999,"
      "\"floatingGeometry\":{\"x\":-99999,\"y\":\"bad\",\"width\":9999,\"height\":1}},"
      "\"unknownPanel\":{\"visible\":true}},\"activePanelByDock\":{\"bottom\":\"unknown\"}}")});
  const QVariantMap mixer = invoke(reopened.get(), "snapshot").toMap()
                                .value("panels").toMap().value("mixer").toMap();
  const QVariantMap geometry = mixer.value("floatingGeometry").toMap();
  if (mixer.value("visible").toBool() || mixer.value("dock").toString() != QStringLiteral("bottom") ||
      mixer.value("returnDock").toString() != QStringLiteral("bottom") ||
      geometry.value("x").toInt() != -10000 || geometry.value("y").toInt() != 90 ||
      geometry.value("width").toInt() != 1600 || geometry.value("height").toInt() != 180)
    return fail("Dock restoration must reject malformed values and clamp floating geometry safely");

  if (invokeBool(reopened.get(), "movePanel", {QStringLiteral("unknown"), QStringLiteral("right"), 0}) ||
      invokeBool(reopened.get(), "movePanel", {QStringLiteral("mixer"), QStringLiteral("unknown"), 0}) ||
      invokeBool(reopened.get(), "applyPreset", {QStringLiteral("unknown")}) ||
      invokeBool(reopened.get(), "resetPreset", {QStringLiteral("unknown")}))
    return fail("Unknown panel, dock, and preset identifiers must be rejected");
  return EXIT_SUCCESS;
}
