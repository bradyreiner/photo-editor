#include <QCoreApplication>
#include <QJSValue>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QUrl>
#include <QVariant>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
QVariant call(QObject* object, const char* method, const QVariant& first) {
  QVariant result;
  if (!QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result),
                                 Q_ARG(QVariant, first))) {
    std::cerr << "Could not invoke " << method << '\n';
    std::exit(EXIT_FAILURE);
  }
  if (result.canConvert<QJSValue>()) return result.value<QJSValue>().toVariant();
  return result;
}

QVariant callNoArgs(QObject* object, const char* method) {
  QVariant result;
  if (!QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result))) {
    std::cerr << "Could not invoke " << method << '\n';
    std::exit(EXIT_FAILURE);
  }
  if (result.canConvert<QJSValue>()) return result.value<QJSValue>().toVariant();
  return result;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine, QUrl::fromLocalFile(
      QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/TimelineWorkspaceLayoutState.qml")));
  std::unique_ptr<QObject> state(component.create());
  if (!state) return fail(qPrintable(component.errorString()));
  const auto editing = call(state.get(), "layout", QStringLiteral("edit")).toMap();
  const auto color = call(state.get(), "layout", QStringLiteral("color")).toMap();
  const auto audio = call(state.get(), "layout", QStringLiteral("audio")).toMap();
  for (const auto& key : {"edit", "color", "audio", "finish"}) {
    const auto factory = call(state.get(), "defaults", QString::fromLatin1(key)).toMap();
    QVariant normalized;
    if (!QMetaObject::invokeMethod(state.get(), "normalized", Q_RETURN_ARG(QVariant, normalized),
            Q_ARG(QVariant, QString::fromLatin1(key)), Q_ARG(QVariant, factory)))
      return fail("Could not normalize the factory panel arrangement");
    if (normalized.canConvert<QJSValue>()) normalized = normalized.value<QJSValue>().toVariant();
    if (normalized.toMap() != factory)
      return fail("Every factory panel arrangement must normalize without changing its tab order or geometry");
  }
  if (editing.value("sourceCollapsed").toBool() || editing.value("inspectorCollapsed").toBool() ||
      editing.value("sourceWidth").toDouble() < 400.0 ||
      !color.value("sourceCollapsed").toBool() || color.value("inspectorCollapsed").toBool() ||
      color.value("inspectorTab").toString() != "color" || !color.value("scopesVisible").toBool())
    return fail("Editing and Color must have distinct usable factory layouts");

  const auto edit_docks = editing.value("dockPanels").toMap();
  const auto color_docks = color.value("dockPanels").toMap();
  const auto audio_docks = audio.value("dockPanels").toMap();
  const auto edit_active = editing.value("activePanelByDock").toMap();
  for (const auto& id : {"mixer", "color", "titles"}) {
    const auto panel = edit_docks.value(QString::fromLatin1(id)).toMap();
    const auto expected_dock = QString::fromLatin1(id) == QStringLiteral("mixer") ? "left" : "right";
    if (!panel.value("visible").toBool() || panel.value("dock").toString() != expected_dock)
      return fail("The complete workspace must expose Audio on the left and Color and Titles on the right");
  }
  if (edit_docks.size() != 12 || !edit_docks.value("media").toMap().value("visible").toBool() ||
      !edit_docks.value("inspector").toMap().value("visible").toBool() ||
      edit_docks.value("inspector").toMap().value("dock").toString() != "left" ||
      edit_docks.value("keyframes").toMap().value("visible").toBool() ||
      edit_docks.value("keyframes").toMap().value("dock").toString() != "right" ||
      edit_active.value("left").toString() != "inspector" ||
      edit_active.value("right").toString() != "effects" ||
      edit_docks.value("effects").toMap().value("dock").toString() != "right" ||
      editing.value("inspectorTab").toString() != "controls")
    return fail("Editing defaults must put active Controls beside Media and keep Effects discovery on the right");
  if (color_docks.value("effects").toMap().value("visible").toBool() ||
      color_docks.value("media").toMap().value("visible").toBool() ||
      !color_docks.value("color").toMap().value("visible").toBool() ||
      !color_docks.value("scopes").toMap().value("visible").toBool() ||
      color.value("activePanelByDock").toMap().value("right").toString() != "color")
    return fail("Color defaults must prioritize Color and Scopes without opening Media or Effects");
  if (audio_docks.value("effects").toMap().value("visible").toBool() ||
      audio_docks.value("color").toMap().value("visible").toBool() ||
      !audio_docks.value("mixer").toMap().value("visible").toBool() ||
      audio_docks.value("mixer").toMap().value("dock").toString() != "bottom")
    return fail("Audio defaults must prioritize the Mixer and keep unrelated Color and Effects panels closed");

  auto legacy_edit = editing;
  legacy_edit.insert("sourceWidth", 285);
  legacy_edit.insert("inspectorWidth", 350);
  legacy_edit.insert("monitorHeight", 625);
  legacy_edit.insert("monitorUserResized", true);
  legacy_edit.insert("timelineHeight", 447);
  legacy_edit.insert("curveOpen", true);
  legacy_edit.insert("curveDocked", false);
  legacy_edit.insert("inspectorTab", "motion");
  auto legacy_panels = edit_docks;
  auto media_panel = legacy_panels.value("media").toMap();
  media_panel.insert("dock", "left");
  media_panel.insert("returnDock", "left");
  media_panel.insert("order", 0);
  media_panel.insert("visible", true);
  media_panel.insert("floatingGeometry",
                      QVariantMap{{"x", 23}, {"y", 37}, {"width", 711}, {"height", 481}});
  legacy_panels.insert("media", media_panel);
  auto inspector_panel = legacy_panels.value("inspector").toMap();
  inspector_panel.insert("dock", "right");
  inspector_panel.insert("returnDock", "right");
  inspector_panel.insert("order", 1);
  inspector_panel.insert("visible", true);
  inspector_panel.insert("floatingGeometry",
                         QVariantMap{{"x", 310}, {"y", 205}, {"width", 515}, {"height", 610}});
  legacy_panels.insert("inspector", inspector_panel);
  auto effects_panel = legacy_panels.value("effects").toMap();
  effects_panel.insert("dock", "right");
  effects_panel.insert("returnDock", "right");
  effects_panel.insert("order", 0);
  effects_panel.insert("visible", true);
  effects_panel.insert("floatingGeometry",
                       QVariantMap{{"x", 77}, {"y", 91}, {"width", 535}, {"height", 471}});
  legacy_panels.insert("effects", effects_panel);
  auto notes_panel = legacy_panels.value("review").toMap();
  notes_panel.insert("dock", "right");
  notes_panel.insert("returnDock", "right");
  notes_panel.insert("order", 2);
  notes_panel.insert("visible", true);
  legacy_panels.insert("review", notes_panel);
  auto custom_panel = legacy_panels.value("titles").toMap();
  custom_panel.insert("dock", "auxiliary");
  custom_panel.insert("returnDock", "auxiliary");
  custom_panel.insert("visible", true);
  custom_panel.insert("floatingGeometry",
                      QVariantMap{{"x", 41}, {"y", 53}, {"width", 601}, {"height", 422}});
  legacy_panels.insert("titles", custom_panel);
  legacy_edit.insert("dockPanels", legacy_panels);
  auto legacy_active = editing.value("activePanelByDock").toMap();
  legacy_active.insert("left", "media");
  legacy_active.insert("right", "review");
  legacy_edit.insert("activePanelByDock", legacy_active);

  const QVariantMap legacy_layouts{{"edit", legacy_edit}, {"color", color}, {"audio", audio}};
  call(state.get(), "restore", QString::fromUtf8(QJsonDocument::fromVariant(legacy_layouts).toJson()));
  const auto color_before_migration = call(state.get(), "layout", QStringLiteral("color")).toMap();
  const auto migrated_once = callNoArgs(state.get(), "migrateEditingControlsLayout").toMap();
  const auto migrated_twice = callNoArgs(state.get(), "migrateEditingControlsLayout").toMap();
  const auto migrated_panels = migrated_once.value("dockPanels").toMap();
  const auto migrated_media = migrated_panels.value("media").toMap();
  const auto migrated_inspector = migrated_panels.value("inspector").toMap();
  const auto migrated_effects = migrated_panels.value("effects").toMap();
  const auto migrated_notes = migrated_panels.value("review").toMap();
  const auto migrated_custom = migrated_panels.value("titles").toMap();
  if (migrated_once != migrated_twice || migrated_once.value("sourceWidth").toDouble() < 320.0 ||
      migrated_once.value("inspectorWidth").toDouble() != 350.0 ||
      migrated_once.value("monitorHeight").toDouble() != 625.0 ||
      migrated_once.value("timelineHeight").toDouble() != 447.0 ||
      !migrated_once.value("monitorUserResized").toBool() ||
      !migrated_once.value("curveOpen").toBool() ||
      migrated_once.value("curveDocked").toBool() ||
      migrated_once.value("inspectorTab").toString() != "motion" ||
      migrated_media.value("dock").toString() != "left" ||
      migrated_media.value("returnDock").toString() != "left" ||
      migrated_media.value("order").toInt() != 0 || !migrated_media.value("visible").toBool() ||
      migrated_media.value("floatingGeometry").toMap() != media_panel.value("floatingGeometry").toMap() ||
      migrated_inspector.value("dock").toString() != "left" ||
      migrated_inspector.value("returnDock").toString() != "left" ||
      migrated_inspector.value("order").toInt() != 1 || !migrated_inspector.value("visible").toBool() ||
      migrated_inspector.value("floatingGeometry").toMap() != inspector_panel.value("floatingGeometry").toMap() ||
      migrated_effects.value("dock").toString() != "right" ||
      migrated_effects.value("returnDock").toString() != "right" ||
      migrated_effects.value("order").toInt() != 0 || !migrated_effects.value("visible").toBool() ||
      migrated_effects.value("floatingGeometry").toMap() != effects_panel.value("floatingGeometry").toMap() ||
      !migrated_notes.value("visible").toBool() ||
      migrated_notes.value("dock").toString() != "right" ||
      migrated_notes.value("returnDock").toString() != notes_panel.value("returnDock").toString() ||
      migrated_notes.value("floatingGeometry").toMap() != notes_panel.value("floatingGeometry").toMap() ||
      migrated_once.value("activePanelByDock").toMap().value("left").toString() != "inspector" ||
      migrated_once.value("activePanelByDock").toMap().value("right").toString() != "effects" ||
      migrated_custom.value("dock").toString() != "auxiliary" ||
      migrated_custom.value("returnDock").toString() != "auxiliary" ||
      !migrated_custom.value("visible").toBool() ||
      migrated_custom.value("floatingGeometry").toMap() != custom_panel.value("floatingGeometry").toMap() ||
      call(state.get(), "layout", QStringLiteral("color")).toMap() != color_before_migration ||
      call(state.get(), "layout", QStringLiteral("audio")).toMap() != audio)
    return fail("The Edit migration must be idempotent, preserve saved work, and move only the requested panels");

  auto legacy_edit_docked_curve = editing;
  legacy_edit_docked_curve.insert("sourceWidth", 422);
  legacy_edit_docked_curve.insert("inspectorTab", "effects");
  legacy_edit_docked_curve.insert("curveOpen", true);
  auto legacy_edit_docked_panels = edit_docks;
  auto legacy_keyframes = legacy_edit_docked_panels.value("keyframes").toMap();
  legacy_keyframes.insert("visible", true);
  legacy_keyframes.insert("dock", "bottom");
  legacy_keyframes.insert("returnDock", "bottom");
  legacy_edit_docked_panels.insert("keyframes", legacy_keyframes);
  legacy_edit_docked_curve.insert("dockPanels", legacy_edit_docked_panels);
  auto legacy_edit_active = edit_active;
  legacy_edit_active.insert("bottom", "keyframes");
  legacy_edit_docked_curve.insert("activePanelByDock", legacy_edit_active);
  QVariant remembered_legacy;
  if (!QMetaObject::invokeMethod(state.get(), "remember", Q_RETURN_ARG(QVariant, remembered_legacy),
          Q_ARG(QVariant, QStringLiteral("edit")), Q_ARG(QVariant, legacy_edit_docked_curve)) ||
      !remembered_legacy.toBool() ||
      !callNoArgs(state.get(), "migrateLegacyEditingKeyframes").toBool())
    return fail("The exact legacy Edit layout should migrate its docked keyframes panel once");
  const auto migrated_keyframe_layout = call(state.get(), "layout", QStringLiteral("edit")).toMap();
  const auto migrated_keyframe_panels = migrated_keyframe_layout.value("dockPanels").toMap();
  if (migrated_keyframe_panels.value("keyframes").toMap().value("visible").toBool() ||
      migrated_keyframe_panels.value("keyframes").toMap().value("dock").toString() != "right" ||
      migrated_keyframe_layout.value("activePanelByDock").toMap().value("right").toString() != "effects" ||
      !migrated_keyframe_layout.value("activePanelByDock").toMap().value("bottom").toString().isEmpty() ||
      migrated_keyframe_layout.value("inspectorTab").toString() != "controls" ||
      migrated_keyframe_layout.value("curveOpen").toBool() ||
      migrated_keyframe_layout.value("sourceWidth").toDouble() != 422.0 ||
      callNoArgs(state.get(), "migrateLegacyEditingKeyframes").toBool())
    return fail("Keyframe migration should hide the legacy bottom panel, keep the curve available, and preserve dimensions");

  auto custom_detached_edit = legacy_edit_docked_curve;
  auto detached_panels = legacy_edit_docked_panels;
  auto detached_titles = detached_panels.value("titles").toMap();
  detached_titles.insert("visible", true);
  detached_titles.insert("dock", "floating");
  detached_titles.insert("returnDock", "bottom");
  detached_panels.insert("titles", detached_titles);
  custom_detached_edit.insert("dockPanels", detached_panels);
  QVariant remembered_detached;
  if (!QMetaObject::invokeMethod(state.get(), "remember", Q_RETURN_ARG(QVariant, remembered_detached),
          Q_ARG(QVariant, QStringLiteral("edit")), Q_ARG(QVariant, custom_detached_edit)) ||
      !remembered_detached.toBool())
    return fail("A custom detached Editing panel layout should be savable");
  const auto detached_before_migration = call(state.get(), "layout", QStringLiteral("edit")).toMap();
  if (callNoArgs(state.get(), "migrateLegacyEditingKeyframes").toBool() ||
      call(state.get(), "layout", QStringLiteral("edit")).toMap() != detached_before_migration)
    return fail("The one-time legacy migration must preserve customized detached panels");

  auto saved_editing = editing;
  saved_editing.insert("sourceWidth", 410);
  saved_editing.insert("monitorHeight", 612);
  saved_editing.insert("monitorUserResized", true);
  saved_editing.insert("auxiliaryDockWidth", 525);
  saved_editing.insert("bottomDockWidth", 715);
  saved_editing.insert("curveOpen", true);
  QVariant remembered;
  if (!QMetaObject::invokeMethod(state.get(), "remember", Q_RETURN_ARG(QVariant, remembered),
      Q_ARG(QVariant, QStringLiteral("edit")), Q_ARG(QVariant, saved_editing)) || !remembered.toBool())
    return fail("A valid workspace layout should be saved");
  if (call(state.get(), "layout", QStringLiteral("edit")).toMap() != saved_editing ||
      call(state.get(), "layout", QStringLiteral("color")).toMap() != color)
    return fail("Saving Editing must preserve its values without changing Color");

  QVariantMap layouts{{"edit", saved_editing}, {"color", color}};
  const auto serialized = QString::fromUtf8(QJsonDocument::fromVariant(layouts).toJson());
  std::unique_ptr<QObject> reopened(component.create());
  call(reopened.get(), "restore", serialized);
  if (call(reopened.get(), "layout", QStringLiteral("edit")).toMap() != saved_editing)
    return fail("Layout dimensions, manual splitter override and animation visibility must survive reopening");
  call(reopened.get(), "reset", QStringLiteral("color"));
  if (call(reopened.get(), "layout", QStringLiteral("edit")).toMap() != saved_editing ||
      call(reopened.get(), "layout", QStringLiteral("color")).toMap() != color)
    return fail("Resetting one workspace must preserve other saved workspaces");

  call(reopened.get(), "restore", QStringLiteral("{broken"));
  if (call(reopened.get(), "layout", QStringLiteral("edit")).toMap() != editing)
    return fail("Malformed preferences must restore a usable default");
  call(reopened.get(), "restore", QStringLiteral(
      "{\"color\":{\"sourceWidth\":99999,\"inspectorWidth\":-1,\"monitorHeight\":\"bad\","
      "\"timelineHeight\":-10,\"auxiliaryDockWidth\":9999,\"bottomDockWidth\":-8,\"sourceTab\":\"bogus\",\"inspectorTab\":\"bogus\","
      "\"scopeKind\":\"bogus\",\"sourceCollapsed\":\"false\","
      "\"dockPanels\":{\"mixer\":{\"visible\":\"true\",\"dock\":\"bogus\",\"order\":999,"
      "\"returnDock\":\"floating\",\"floatingGeometry\":{\"x\":99999,\"y\":\"bad\","
      "\"width\":9999,\"height\":1}},\"media\":{\"visible\":false}},"
      "\"activePanelByDock\":{\"right\":\"unknown-panel\",\"bottom\":\"mixer\"}},\"unknown\":{}}"));
  const auto bounded = call(reopened.get(), "layout", QStringLiteral("color")).toMap();
  if (bounded.value("sourceWidth").toDouble() != 560 || bounded.value("inspectorWidth").toDouble() != 280 ||
      bounded.value("auxiliaryDockWidth").toDouble() != 760 || bounded.value("bottomDockWidth").toDouble() != 320 ||
      bounded.value("monitorHeight").toDouble() != 540 || bounded.value("timelineHeight").toDouble() != 180 ||
      bounded.value("sourceTab").toString() != "media" || bounded.value("inspectorTab").toString() != "color" ||
      !bounded.value("sourceCollapsed").toBool())
    return fail("Invalid saved geometry and enum values must be safely bounded without coercing boolean strings");
  const auto bounded_docks = bounded.value("dockPanels").toMap();
  const auto bounded_mixer = bounded_docks.value("mixer").toMap();
  const auto bounded_geometry = bounded_mixer.value("floatingGeometry").toMap();
  if (bounded_mixer.value("visible").toBool() || bounded_mixer.value("dock").toString() != "bottom" ||
      bounded_mixer.value("returnDock").toString() != "bottom" ||
      bounded_geometry.value("x").toDouble() != 10000 ||
      bounded_geometry.value("y").toDouble() != 90 ||
      bounded_geometry.value("width").toDouble() != 1600 ||
      bounded_geometry.value("height").toDouble() != 180 ||
      bounded.value("activePanelByDock").toMap().value("right").toString() != "color")
    return fail("Dock placement, visibility, active tabs, and floating geometry must restore defensively");
  call(reopened.get(), "restore", QStringLiteral("{\"edit\":{\"sourceWidth\":430,\"monitorHeight\":620,\"timelineHeight\":390,\"dockPanels\":{\"mixer\":{\"visible\":false,\"dock\":\"bottom\"},\"titles\":{\"visible\":false,\"dock\":\"bottom\"},\"color\":{\"visible\":false,\"dock\":\"auxiliary\"}}}}"));
  const auto complete = callNoArgs(reopened.get(), "migrateCompleteEditingLayout").toMap();
  const auto complete_panels = complete.value("dockPanels").toMap();
  if (complete.value("sourceWidth").toInt() != 430 || complete.value("monitorHeight").toInt() != 620 ||
      complete.value("timelineHeight").toInt() != 390)
    return fail("Complete workspace migration must preserve custom splitter dimensions");
  for (const auto& id : {"media", "inspector", "mixer", "effects", "color", "titles"}) {
    const auto panel = complete_panels.value(QString::fromLatin1(id)).toMap();
    const bool left = QStringList{"media", "inspector", "mixer"}.contains(QString::fromLatin1(id));
    if (!panel.value("visible").toBool() || panel.value("dock").toString() != (left ? "left" : "right"))
      return fail("Migration must expose the six primary tools in the complete two-sided workspace");
  }
  if (callNoArgs(reopened.get(), "migrateCompleteEditingLayout").toMap() != complete)
    return fail("Complete workspace migration must be idempotent");
  return EXIT_SUCCESS;
}
