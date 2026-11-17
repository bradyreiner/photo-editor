int run_timeline_inspector_animation_ui_proof(
    QQuickWindow* window, QObject* root,
    photo_editor::desktop::TimelineDocumentController* document,
    const QString& image_path, const QString& supplied_video_path,
    const QString& output_dir) {
  if (window == nullptr || root == nullptr || document == nullptr || output_dir.isEmpty() ||
      !QDir().mkpath(output_dir)) {
    return fail("Inspector animation proof could not initialize its runtime or output directory");
  }
  auto video_path = supplied_video_path;
  if (video_path.isEmpty() || !QFileInfo::exists(video_path) ||
      QFileInfo(video_path).size() <= 1024) {
    video_path = QDir(output_dir).filePath(QStringLiteral("timeline-inspector-animation-source.mp4"));
    if (!write_timeline_proof_video_file(image_path, video_path))
      return fail("Inspector animation proof needs an encoded video fixture and poster");
  }

  root->setProperty("width", 1440);
  root->setProperty("height", 900);
  document->createTimelineWithSettings({{"timelineName", "Animation targeting"},
                                        {"width", 1920}, {"height", 1080},
                                        {"frameRate", 30.0}});
  document->addClipFromGallery("video:animation-target-a", video_path, "Pinned Shot A", 10.0,
                               QUrl::fromLocalFile(image_path).toString());
  document->addClipFromGallery("video:animation-target-b", video_path, "Follow Shot B", 10.0,
                               QUrl::fromLocalFile(image_path).toString());
  document->selectClip(0);
  document->seekSequenceSeconds(2.0);
  QVariant routed;
  if (!QMetaObject::invokeMethod(
          root, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, routed),
          Q_ARG(QVariant, root->property("timelineWorkspaceIndex")))) {
    return fail("Inspector animation proof could not route into Timeline");
  }
  if (!wait_for_condition([&] {
        auto* workspace = active_workspace(root);
        return workspace != nullptr &&
               workspace->objectName() == QStringLiteral("timelineWorkspace") &&
               !root->property("workspaceTransitionActive").toBool() &&
               workspace->property("timelineEditorPageLoaded").toBool();
      }, 30000)) {
    return fail("Inspector animation proof must load the actual Timeline workspace");
  }
  auto* workspace = active_workspace(root);
  if (workspace == nullptr)
    return fail("Inspector animation proof requires the active Timeline workspace");
  workspace->setProperty("effectsPanelWidth", 320.0);
  process_layout_events(10);

  ulong timestamp = 10000;
  const auto pointer = [&](QQuickWindow* target, QEvent::Type type, QPointF point,
                           Qt::MouseButtons buttons) {
    QMouseEvent event(type, point, QPointF(target->mapToGlobal(point.toPoint())),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      buttons, Qt::NoModifier);
    event.setTimestamp(timestamp++);
    QCoreApplication::sendEvent(target, &event);
    process_layout_events(2);
  };
  const auto click = [&](QQuickWindow* target, const QString& name) {
    auto* item = find_visible_named_item(target, name);
    if (!item_visible(item) || !item->isEnabled()) return false;
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
    pointer(item->window(), QEvent::MouseButtonPress, point, Qt::LeftButton);
    pointer(item->window(), QEvent::MouseButtonRelease, point, Qt::NoButton);
    return true;
  };
  const auto qml_value = [](const QVariant& value) {
    return value.metaType() == QMetaType::fromType<QJSValue>()
        ? value.value<QJSValue>().toVariant() : value;
  };
  auto* panel_dock_state = find_named_object(workspace, "timelinePanelDockState");
  const auto panel_state_value = [&](const char* method, const QString& panel_id) {
    QVariant value;
    if (panel_dock_state == nullptr ||
        !QMetaObject::invokeMethod(panel_dock_state, method, Q_RETURN_ARG(QVariant, value),
                                   Q_ARG(QVariant, QVariant(panel_id)))) {
      return QVariant{};
    }
    return qml_value(value);
  };
  const auto item_is_inside = [](const QQuickItem* item, const QQuickItem* ancestor) {
    for (auto* current = item; current != nullptr; current = current->parentItem()) {
      if (current == ancestor) return true;
    }
    return false;
  };
  auto* bottom_panel_dock = qobject_cast<QQuickItem*>(
      find_named_object(workspace, "timelineBottomPanelDock"));
  auto* dock_host = bottom_panel_dock == nullptr
      ? nullptr : bottom_panel_dock->property("contentHost").value<QQuickItem*>();
  auto* inspector_panel = find_named_item(workspace, "timelineEffectsRail");
  QQuickWindow* curve = nullptr;
  QJsonArray captures;
  const auto capture_main = [&](const QString& name) {
    const auto path = QDir(output_dir).filePath(name);
    if (!window->grabWindow().save(path)) return false;
    captures.push_back(QFileInfo(path).fileName());
    return true;
  };

  workspace->setProperty("curveEditorOpen", false);
  workspace->setProperty("curveEditorDocked", true);
  workspace->setProperty("sourcePanelCollapsed", true);
  workspace->setProperty("effectsPanelCollapsed", false);
  if (!QMetaObject::invokeMethod(workspace, "openInspectorTab",
                                 Q_ARG(QVariant, QVariant(QStringLiteral("effects")))))
    return fail("The animation target proof must open Effect Controls");
  document->addSelectedClipEffect("Exposure");
  document->setSelectedClipEffectAmount("Exposure", 0.6);
  document->setSelectedClipEffectEnabled("Exposure", false);
  if (!wait_for_condition([&] {
        for (const auto& value : document->timelineClipDetails(0)
                                     .value(QStringLiteral("effectDetails")).toList()) {
          const auto effect = value.toMap();
          if (effect.value(QStringLiteral("name")).toString() == QStringLiteral("Exposure"))
            return !effect.value(QStringLiteral("enabled")).toBool();
        }
        return false;
      }, 5000))
    return fail("Inspector animation proof must verify Exposure is actually bypassed");
  if (!wait_for_condition([&] {
        return find_named_object(workspace, "timelineAppliedEffect-Exposure") != nullptr
                && named_item_visible(workspace, "timelineEffectKeyframe-Exposure")
                && named_item_visible(workspace, "timelineEffectTitle-Exposure");
      }, 12000)) return fail("A bypassed applied effect must expose compact animation controls");
  if (std::abs(workspace->property("effectsPanelWidth").toDouble() - 320.0) > 1.0)
    return fail("Applied effect header proof must use the 320-pixel Inspector width");
  workspace->setProperty("sourcePanelWidth", 280.0);
  process_layout_events(8);
  inspector_panel = find_named_item(workspace, "timelineEffectsRail");
  auto* applied_effects_tab = find_named_object(workspace, "timelineInspectorEffectsTab");
  auto* transform_tab = find_named_object(workspace, "timelineInspectorMotionTab");
  auto* animate_tab = find_named_object(workspace, "timelineInspectorAnimateTab");
  auto* selection_title = find_named_object(workspace, "timelineInspectorSelectionTitle");
  auto* selection_type = find_named_object(workspace, "timelineInspectorSelectionType");
  if (!inspector_panel || inspector_panel->width() < 270.0 ||
      !named_item_visible(workspace, "timelineInspectorEffectsTab") ||
      !named_item_visible(workspace, "timelineInspectorMotionTab") ||
      !named_item_visible(workspace, "timelineInspectorAnimateTab") ||
      !applied_effects_tab || applied_effects_tab->property("text").toString() != QStringLiteral("Effects") ||
      applied_effects_tab->property("accessibleName").toString() != QStringLiteral("Applied Effects") ||
      !transform_tab || transform_tab->property("text").toString() != QStringLiteral("Transform") ||
      !animate_tab || animate_tab->property("text").toString() != QStringLiteral("Animate") ||
      !selection_title || !selection_title->property("text").toString().contains(QStringLiteral("Pinned Shot A")) ||
      !selection_type || selection_type->property("text").toString() != QStringLiteral("Video clip"))
    return fail("The 280-pixel Controls panel must show the selected clip title/type and first-class Applied Effects, Transform, and Animate tabs");
  if (!click(window, "timelineInspectorMotionTab") ||
      !wait_for_condition([&] {
        return named_item_visible(workspace, "timelineInspectorKeyframeToggleButton") &&
               named_item_visible(workspace, "timelineInspectorCurveEditorButton");
      }, 5000))
    return fail("Transform must expose its keyframe and curve controls at the narrow Controls width");
  auto* narrow_transform_keyframe = find_visible_named_item(
      workspace, "timelineInspectorKeyframeToggleButton");
  auto* narrow_transform_curve = find_visible_named_item(
      workspace, "timelineInspectorCurveEditorButton");
  if (!narrow_transform_keyframe || !narrow_transform_keyframe->isEnabled() ||
      !narrow_transform_curve || !narrow_transform_curve->isEnabled() ||
      !click(window, "timelineInspectorEffectsTab"))
    return fail("Transform keyframe and curve entrypoints must remain actionable at 280 pixels");
  auto* narrow_effect_keyframe = find_visible_named_item(
      workspace, "timelineEffectKeyframe-Exposure");
  auto* narrow_effect_curve = find_visible_named_item(
      workspace, "timelineEffectCurveButton-Exposure");
  if (!narrow_effect_keyframe || !narrow_effect_keyframe->isEnabled() ||
      !narrow_effect_curve || !narrow_effect_curve->isEnabled())
    return fail("Applied Effects must retain per-effect keyframe and curve entrypoints at 280 pixels");
  workspace->setProperty("sourcePanelWidth", 320.0);
  process_layout_events(8);
  auto* effect_card = find_named_object(workspace, "timelineAppliedEffect-Exposure");
  if (effect_card == nullptr)
    return fail("Applied effect disclosure proof requires its effect card");
  const bool initial_effect_expanded = effect_card->property("expandedState").toBool();
  auto* disclosure_button = find_visible_named_item(
      workspace, "timelineEffectDisclosure-Exposure");
  auto* effects_panel = find_named_object(workspace, "timelineEffectsRail");
  if (disclosure_button == nullptr || effects_panel == nullptr)
    return fail("Applied effect disclosure proof requires its visible button and owning panel");
  QSignalSpy disclosure_click_spy(disclosure_button, SIGNAL(clicked()));
  QSignalSpy disclosure_request_spy(effects_panel,
                                    SIGNAL(effectDisclosureChangeRequested(QString,bool)));
  if (!disclosure_click_spy.isValid() || !disclosure_request_spy.isValid())
    return fail("Applied effect disclosure proof must observe button and panel signals");
  if (!capture_main(QStringLiteral("timeline-animation-effect-disclosure-before-click.png")))
    return fail("The applied effect disclosure must be captured before its pointer click");
  if (!click(window, "timelineEffectDisclosure-Exposure"))
    return fail("The applied effect disclosure must toggle by pointer");
  const bool toggled_effect_expanded = !initial_effect_expanded;
  if (!wait_for_condition([&] {
        effect_card = find_named_object(workspace, "timelineAppliedEffect-Exposure");
        return effect_card != nullptr &&
               effect_card->property("expandedState").toBool() == toggled_effect_expanded;
      }, 5000)) {
    std::cerr << "Applied effect disclosure toggle: initialExpanded=" << initial_effect_expanded
              << " expandedAfterPointer="
              << (effect_card && effect_card->property("expandedState").toBool()) << '\n';
    std::cerr << "  clickSignalCount=" << disclosure_click_spy.count()
              << " panelSignalCount=" << disclosure_request_spy.count()
              << " buttonRect=" << item_scene_rect(disclosure_button).x() << ','
              << item_scene_rect(disclosure_button).y() << ' '
              << item_scene_rect(disclosure_button).width() << 'x'
              << item_scene_rect(disclosure_button).height()
              << " buttonWindowIsMain=" << (disclosure_button->window() == window)
              << " buttonVisible=" << item_visible(disclosure_button)
              << " buttonEnabled=" << disclosure_button->isEnabled()
              << " panelState=" << QJsonDocument::fromVariant(
                     effects_panel->property("effectDisclosureState"))
                     .toJson(QJsonDocument::Compact).toStdString();
    QVariant root_disclosure_state;
    if (QMetaObject::invokeMethod(workspace, "inspectorEffectDisclosureState",
                                  Q_RETURN_ARG(QVariant, root_disclosure_state))) {
      std::cerr << " rootState=" << QJsonDocument::fromVariant(root_disclosure_state)
                                      .toJson(QJsonDocument::Compact).toStdString();
    }
    QVariant selection_type;
    if (QMetaObject::invokeMethod(workspace, "currentInspectorSelectionType",
                                  Q_RETURN_ARG(QVariant, selection_type))) {
      std::cerr << " selectionType=" << selection_type.toString().toStdString();
    }
    for (auto* ancestor = disclosure_button; ancestor != nullptr;
         ancestor = ancestor->parentItem()) {
      const auto rect = item_scene_rect(ancestor);
      std::cerr << "\n  ancestor=" << ancestor->objectName().toStdString()
                << " type=" << ancestor->metaObject()->className()
                << " clip=" << ancestor->clip()
                << " visible=" << ancestor->isVisible()
                << " rect=" << rect.x() << ',' << rect.y() << ' '
                << rect.width() << 'x' << rect.height();
    }
    if (disclosure_request_spy.count() > 0) {
      const auto request = disclosure_request_spy.constFirst();
      std::cerr << "\n  requestArguments="
                << request.value(0).toString().toStdString() << ','
                << request.value(1).toBool();
    }
    std::cerr << '\n';
    return fail("The applied effect disclosure pointer must change its expanded state");
  }
  if (disclosure_click_spy.count() < 1 || disclosure_request_spy.count() < 1)
    return fail("The applied effect disclosure click must route through its owning Inspector panel");
  const auto disclosure_request = disclosure_request_spy.constFirst();
  if (disclosure_request.value(0).toString() != QStringLiteral("Exposure") ||
      disclosure_request.value(1).toBool() != toggled_effect_expanded)
    return fail("The Inspector disclosure request must carry the clicked effect and next state");
  if (toggled_effect_expanded) {
    if (!click(window, "timelineEffectDisclosure-Exposure"))
      return fail("The applied effect disclosure must accept a second pointer click to collapse");
    if (!wait_for_condition([&] {
          effect_card = find_named_object(workspace, "timelineAppliedEffect-Exposure");
          return effect_card != nullptr && !effect_card->property("expandedState").toBool();
        }, 5000))
      return fail("The applied effect disclosure must collapse after the second pointer click");
  }
  auto* effect_title = find_visible_named_item(workspace, "timelineEffectTitle-Exposure");
  if (!wait_for_condition([&] {
        effect_card = find_named_object(workspace, "timelineAppliedEffect-Exposure");
        effect_title = find_visible_named_item(workspace, "timelineEffectTitle-Exposure");
        return effect_card != nullptr && !effect_card->property("expandedState").toBool() &&
               item_visible(effect_title) &&
               effect_title->property("text").toString() == QStringLiteral("Exposure") &&
               effect_title->width() + 1.0 >= effect_title->property("implicitWidth").toDouble();
      }, 5000)) {
    std::cerr << "Applied effect header fit: inspectorWidth="
              << workspace->property("effectsPanelWidth").toDouble()
              << " cardExpanded=" << (effect_card && effect_card->property("expandedState").toBool())
              << " titleVisible=" << item_visible(effect_title)
              << " title=" << (effect_title ? effect_title->property("text").toString().toStdString() : "")
              << " titleWidth=" << (effect_title ? effect_title->width() : 0)
              << " titleImplicitWidth=" << (effect_title
                                                ? effect_title->property("implicitWidth").toDouble() : 0)
              << '\n';
    return fail("The collapsed 320-pixel effect header must show the full effect name");
  }
  for (const auto* name : {"timelineEffectBypass-Exposure",
                           "timelineEffectReset-Exposure",
                           "timelineEffectRemove-Exposure",
                           "timelineEffectKeyframe-Exposure",
                           "timelineEffectCurveButton-Exposure"}) {
    if (!named_item_visible(workspace, QString::fromLatin1(name)))
      return fail("Collapsed Effect Controls must keep bypass, reset, remove, and keyframe actions visible");
  }
  auto* effect_curve_button = find_visible_named_item(
      workspace, "timelineEffectCurveButton-Exposure");
  auto* effect_keyframe_button = find_visible_named_item(
      workspace, "timelineEffectKeyframe-Exposure");
  if (!effect_curve_button || !effect_curve_button->isEnabled() ||
      !effect_keyframe_button || !effect_keyframe_button->isEnabled())
    return fail("Bypassed effects must remain keyframeable and curve-editable");
  process_layout_events(8);
  if (!capture_main(QStringLiteral("timeline-animation-effect-header-collapsed.png")))
    return fail("The collapsed applied effect header must be captured after its transition settles");
  if (!click(window, "timelineEffectKeyframe-Exposure"))
    return fail("A collapsed effect header must add a keyframe at the playhead");
  const auto effect_keyframes = [&] {
    int count = 0;
    for (const auto& keyframe : document->timelineClipDetails(0).value("keyframes").toList())
      if (keyframe.toMap().value("parameter").toString() == QStringLiteral("effect:Exposure"))
        ++count;
    return count;
  };
  if (effect_keyframes() != 1)
    return fail("The collapsed effect keyframe action must author the amount curve while bypassed");
  if (!capture_main(QStringLiteral("timeline-animation-effect-controls.png")))
    return fail("The bypassed applied-effect controls must be capturable");
  auto* animation_picker = find_named_object(workspace, "timelineProgramAnimationParameterPicker");
  if (!animation_picker)
    return fail("The Program animation channel picker must be instantiated");
  animation_picker->setProperty("animatedOnly", true);
  animation_picker->setProperty("selectedGroup", QStringLiteral("Effects"));
  auto animated_effect_channels = qml_value(animation_picker->property("filteredOptions")).toList();
  if (animated_effect_channels.size() != 1 ||
      animated_effect_channels.first().toMap().value("parameter").toString() !=
          QStringLiteral("effect:Exposure"))
    return fail("Animated-only and Effects grouping must filter the real channel catalogue together");
  animation_picker->setProperty("searchText", QStringLiteral("exposure"));
  if (qml_value(animation_picker->property("filteredOptions")).toList().size() != 1)
    return fail("Animation channel search must narrow the grouped animated channels");
  animation_picker->setProperty("animatedOnly", false);
  animation_picker->setProperty("selectedGroup", QStringLiteral("All"));
  animation_picker->setProperty("searchText", QString());
  if (!click(window, "timelineInspectorAnimateTab"))
    return fail("Animate must be a first-class Inspector tab beside Effects and Motion");
  auto* animate_view = find_named_object(workspace, "timelineAnimationChannelsInspector");
  inspector_panel = find_named_item(workspace, "timelineEffectsRail");
  if (!wait_for_condition([&] {
        animate_view = find_named_object(workspace, "timelineAnimationChannelsInspector");
        return item_visible(qobject_cast<QQuickItem*>(animate_view));
      }, 5000) || !inspector_panel)
    return fail("Animate must load its in-panel searchable channel browser");
  inspector_panel->setProperty("animateAnimatedOnly", false);
  inspector_panel->setProperty("animateSelectedGroup", QStringLiteral("All"));
  inspector_panel->setProperty("animateSearchText", QString());
  process_layout_events(8);
  auto* animate_channel_list = find_visible_named_item(
      workspace, "timelineAnimateChannelList");
  auto* first_animate_channel = find_visible_named_item(
      workspace, "timelineAnimateChannel-0");
  auto* second_animate_channel = find_visible_named_item(
      workspace, "timelineAnimateChannel-1");
  const auto animate_list_rect = item_scene_rect(animate_channel_list);
  const auto first_channel_rect = item_scene_rect(first_animate_channel);
  const auto second_channel_rect = item_scene_rect(second_animate_channel);
  const auto first_channel_visible_rect = first_channel_rect.intersected(animate_list_rect);
  const auto second_channel_visible_rect = second_channel_rect.intersected(animate_list_rect);
  if (!animate_channel_list || !first_animate_channel ||
      !second_animate_channel || animate_channel_list->height() < 80.0 ||
      first_channel_visible_rect.height() < 24.0 ||
      second_channel_visible_rect.height() < 24.0 ||
      first_channel_rect.top() < animate_list_rect.top() - 1.0 ||
      first_channel_rect.top() >= animate_list_rect.bottom() ||
      second_channel_rect.top() >= animate_list_rect.bottom()) {
    std::cerr << "Animate viewport geometry: list=" << animate_list_rect.x() << ','
              << animate_list_rect.y() << ' ' << animate_list_rect.width() << 'x'
              << animate_list_rect.height() << " firstRow=" << first_channel_rect.x() << ','
              << first_channel_rect.y() << ' ' << first_channel_rect.width() << 'x'
              << first_channel_rect.height() << " visibleRowHeight="
              << first_channel_visible_rect.height() << " secondRow="
              << second_channel_rect.x() << ',' << second_channel_rect.y() << ' '
              << second_channel_rect.width() << 'x' << second_channel_rect.height()
              << " secondVisibleHeight=" << second_channel_visible_rect.height() << '\n';
    return fail("Animate must show at least two channels in its own compact scroll viewport without scrolling the panel header away");
  }
  if (!capture_main(QStringLiteral("timeline-animation-channel-list-first-row-visible.png")))
    return fail("The immediately visible Animate channel list must be capturable");

  if (!click(window, "timelineAnimateFilterMenu") ||
      !wait_for_condition([&] {
        return named_item_visible(workspace, "timelineAnimateFilterMenuPopupItem0");
      }, 3000) || !click(window, "timelineAnimateFilterMenuPopupItem0") ||
      !click(window, "timelineAnimateFilterMenu") ||
      !wait_for_condition([&] {
        return named_item_visible(workspace, "timelineAnimateFilterMenuPopupItem3");
      }, 3000) || !click(window, "timelineAnimateFilterMenuPopupItem3"))
    return fail("The compact Animate Filter menu must expose keyboard and pointer accessible Animated only and channel-group options");
  if (!inspector_panel->property("animateAnimatedOnly").toBool() ||
      inspector_panel->property("animateSelectedGroup").toString() != QStringLiteral("Effects"))
    return fail("Choosing Animate filter menu items must update the selected filter state");
  inspector_panel->setProperty("animateSearchText", QStringLiteral("exposure"));
  const auto animate_matches = qml_value(inspector_panel->property("filteredAnimateChannels")).toList();
  if (animate_matches.size() != 1 ||
      animate_matches.first().toMap().value("parameter").toString() != QStringLiteral("effect:Exposure") ||
      !named_item_visible(workspace, "timelineAnimateChannel-0"))
    return fail("Animate must filter its in-panel channel list by Animated only, group, and search together");
  QQuickItem* animate_exposure_row = nullptr;
  if (!wait_for_condition([&] {
        animate_exposure_row = find_visible_named_item(window, "timelineAnimateChannel-0");
        return item_visible(animate_exposure_row) &&
               qml_value(animate_exposure_row->property("optionData"))
                       .toMap().value("parameter").toString() == QStringLiteral("effect:Exposure");
      }, 5000))
    return fail("Animate must bind the filtered Exposure channel to its visible row before input");
  if (!click(window, "timelineAnimateChannel-0") ||
      !wait_for_condition([&] {
        return workspace->property("curveEditorOpen").toBool() &&
               workspace->property("curveEditorParameter").toString() == QStringLiteral("effect:Exposure");
      }, 5000))
    return fail("Choosing a channel in Animate must open its curve directly in the dock");
  inspector_panel->setProperty("animateAnimatedOnly", false);
  inspector_panel->setProperty("animateSelectedGroup", QStringLiteral("All"));
  inspector_panel->setProperty("animateSearchText", QString());
  if (!click(window, "timelineInspectorEffectsTab"))
    return fail("Effect Controls must remain reachable after choosing an Animate channel");
  if (!wait_for_condition([&] {
        auto* curve_button = find_visible_named_item(workspace, "timelineEffectCurveButton-Exposure");
        return item_visible(curve_button) && curve_button->isEnabled();
      }, 5000) || !click(window, "timelineEffectCurveButton-Exposure"))
    return fail("A bypassed effect must open its amount curve");
  curve = qobject_cast<QQuickWindow*>(find_named_object(root, "timelineKeyframeCurveWindow"));
  if (!curve)
    return fail("The curve editor must retain its single live Window host in docked mode");
  if (!wait_for_condition([&] {
        bottom_panel_dock = qobject_cast<QQuickItem*>(
            find_named_object(workspace, "timelineBottomPanelDock"));
        dock_host = bottom_panel_dock == nullptr
            ? nullptr : bottom_panel_dock->property("contentHost").value<QQuickItem*>();
        inspector_panel = find_named_item(workspace, "timelineEffectsRail");
        auto* graph = find_visible_named_item(window, "timelineKeyframeCurveGraph");
        if (bottom_panel_dock == nullptr || dock_host == nullptr || graph == nullptr ||
            inspector_panel == nullptr) {
          return false;
        }
        const auto dock_bounds = item_scene_rect(dock_host);
        const auto graph_bounds = item_scene_rect(graph);
        const auto inspector_bounds = item_scene_rect(inspector_panel);
        const QRectF main_bounds(0.0, 0.0, window->width(), window->height());
        return workspace->property("curveEditorOpen").toBool() &&
               workspace->property("curveEditorDocked").toBool() &&
               workspace->property("curveEditorParameter").toString() == QStringLiteral("effect:Exposure") &&
               panel_state_value("visible", QStringLiteral("keyframes")).toBool() &&
               panel_state_value("dock", QStringLiteral("keyframes")).toString() == QStringLiteral("bottom") &&
               panel_state_value("activePanel", QStringLiteral("bottom")).toString() == QStringLiteral("keyframes") &&
               panel_state_value("visible", QStringLiteral("inspector")).toBool() &&
               item_visible(bottom_panel_dock) && item_visible(dock_host) &&
               bottom_panel_dock->window() == window && dock_host->window() == window &&
               dock_host->width() >= 330 && dock_host->height() >= 120 &&
               main_bounds.intersects(dock_bounds) &&
               item_is_inside(graph, dock_host) && item_visible(graph) && graph->window() == window &&
               dock_bounds.adjusted(-1.0, -1.0, 1.0, 1.0).contains(graph_bounds) &&
               item_visible(inspector_panel) && inspector_panel->window() == window &&
               main_bounds.intersects(inspector_bounds);
      }, 5000)) {
    bottom_panel_dock = qobject_cast<QQuickItem*>(
        find_named_object(workspace, "timelineBottomPanelDock"));
    dock_host = bottom_panel_dock == nullptr
        ? nullptr : bottom_panel_dock->property("contentHost").value<QQuickItem*>();
    inspector_panel = find_named_item(workspace, "timelineEffectsRail");
    auto* diagnostic_graph = find_visible_named_item(window, "timelineKeyframeCurveGraph");
    std::cerr << "Inspector curve open state: open="
              << workspace->property("curveEditorOpen").toBool()
              << " docked=" << workspace->property("curveEditorDocked").toBool()
              << " parameter=" << workspace->property("curveEditorParameter").toString().toStdString()
              << " tab=" << workspace->property("inspectorTab").toString().toStdString()
              << " windowVisible=" << curve->isVisible()
              << " keyframesVisible=" << panel_state_value("visible", QStringLiteral("keyframes")).toBool()
              << " keyframesDock=" << panel_state_value("dock", QStringLiteral("keyframes")).toString().toStdString()
              << " bottomActive=" << panel_state_value("activePanel", QStringLiteral("bottom")).toString().toStdString()
              << " bottomDockVisible=" << item_visible(bottom_panel_dock)
              << " dockHostVisible=" << item_visible(dock_host)
              << " dockHostBounds=" << (dock_host ? item_scene_rect(dock_host).x() : 0) << ','
              << (dock_host ? item_scene_rect(dock_host).y() : 0) << ' '
              << (dock_host ? dock_host->width() : 0) << 'x'
              << (dock_host ? dock_host->height() : 0)
              << " inspectorVisible=" << item_visible(inspector_panel)
              << " inspectorBounds=" << (inspector_panel ? item_scene_rect(inspector_panel).x() : 0) << ','
              << (inspector_panel ? item_scene_rect(inspector_panel).y() : 0) << ' '
              << (inspector_panel ? inspector_panel->width() : 0) << 'x'
              << (inspector_panel ? inspector_panel->height() : 0)
              << " graphVisible=" << item_visible(diagnostic_graph)
              << " graphInHost=" << item_is_inside(diagnostic_graph, dock_host)
              << " graphWindowIsMain=" << (diagnostic_graph && diagnostic_graph->window() == window)
              << " graphBounds=" << (diagnostic_graph ? item_scene_rect(diagnostic_graph).x() : 0) << ','
              << (diagnostic_graph ? item_scene_rect(diagnostic_graph).y() : 0) << ' '
              << (diagnostic_graph ? diagnostic_graph->width() : 0) << 'x'
              << (diagnostic_graph ? diagnostic_graph->height() : 0)
              << " clipKeyframes="
              << qml_value(curve->property("curveKeyframes")).toList().size() << '\n';
    return fail("The docked curve and Inspector must both be rendered in their actual workspace panels");
  }
  process_layout_events(8);
  bottom_panel_dock = qobject_cast<QQuickItem*>(
      find_named_object(workspace, "timelineBottomPanelDock"));
  dock_host = bottom_panel_dock == nullptr
      ? nullptr : bottom_panel_dock->property("contentHost").value<QQuickItem*>();
  auto* dock_graph = find_visible_named_item(window, "timelineKeyframeCurveGraph");
  if (!item_visible(bottom_panel_dock) || !item_visible(dock_host) || dock_host->width() < 330 ||
      !item_visible(dock_graph) || !item_is_inside(dock_graph, dock_host) ||
      dock_graph->window() != window || curve->isVisible() ||
      panel_state_value("dock", QStringLiteral("keyframes")).toString() != QStringLiteral("bottom") ||
      !panel_state_value("visible", QStringLiteral("inspector")).toBool() ||
      !item_visible(inspector_panel) || inspector_panel->window() != window ||
      qml_value(curve->property("curveKeyframes")).toList().size() != 1)
    return fail("Docked mode must parent the live graph to the visible bottom panel beside the Inspector");
  if (!capture_main(QStringLiteral("timeline-animation-docked-curve.png")))
    return fail("The docked animation curve must be capturable");

  QQuickItem* curve_dock_splitter = nullptr;
  double nearest_curve_splitter = std::numeric_limits<double>::infinity();
  const auto dock_rect = item_scene_rect(bottom_panel_dock);
  for (auto* candidate : find_named_items(workspace, "timelineBottomDockSplitterHandle")) {
    if (!item_visible(candidate) || candidate->window() != window)
      continue;
    const auto handle_rect = item_scene_rect(candidate);
    const auto vertical_overlap = std::min(handle_rect.bottom(), dock_rect.bottom()) -
                                 std::max(handle_rect.top(), dock_rect.top());
    const auto right_gap = dock_rect.left() - handle_rect.right();
    if (handle_rect.center().x() > dock_rect.left() + 1.0 || right_gap < -2.0 ||
        right_gap > 20.0 || vertical_overlap < 20.0)
      continue;
    const auto distance = std::abs(dock_rect.left() - handle_rect.center().x());
    if (distance < nearest_curve_splitter) {
      nearest_curve_splitter = distance;
      curve_dock_splitter = candidate;
    }
  }
  if (!item_visible(curve_dock_splitter))
    return fail("The bottom panel dock must expose its own visible horizontal resize handle");
  const auto initial_dock_visible_width = bottom_panel_dock->width();
  const auto initial_dock_preferred_width = workspace->property("bottomDockWidth").toDouble();
  const auto splitter_start = curve_dock_splitter->mapToScene(
      QPointF(curve_dock_splitter->width() / 2.0, curve_dock_splitter->height() / 2.0));
  const auto splitter_end = splitter_start + QPointF(-40.0, 0.0);
  timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
  pointer(window, QEvent::MouseButtonPress, splitter_start, Qt::LeftButton);
  for (int step = 1; step <= 4; ++step) {
    const auto fraction = static_cast<double>(step) / 4.0;
    pointer(window, QEvent::MouseMove,
            splitter_start + (splitter_end - splitter_start) * fraction, Qt::LeftButton);
  }
  pointer(window, QEvent::MouseButtonRelease, splitter_end, Qt::NoButton);
  if (!wait_for_condition([&] {
        return bottom_panel_dock->width() >= initial_dock_visible_width + 20.0 &&
               workspace->property("bottomDockWidth").toDouble() >=
                   initial_dock_preferred_width + 20.0;
      }, 5000)) {
    std::cerr << "Curve dock resize: initialVisible=" << initial_dock_visible_width
              << " currentVisible=" << bottom_panel_dock->width()
              << " initialPreferred=" << initial_dock_preferred_width
              << " currentPreferred=" << workspace->property("bottomDockWidth").toDouble()
              << " splitter=" << item_scene_rect(curve_dock_splitter).x() << ','
              << item_scene_rect(curve_dock_splitter).y() << ' '
              << curve_dock_splitter->width() << 'x' << curve_dock_splitter->height() << '\n';
    return fail("Dragging the actual bottom dock divider must increase its visible and preferred widths");
  }
  process_layout_events(5);
  auto* workspace_settings = find_named_object(workspace, "timelineWorkspaceSettings");
  const auto persisted_bottom_dock_width = [&] {
    if (workspace_settings == nullptr) return 0.0;
    const auto layouts = QJsonDocument::fromJson(
        workspace_settings->property("workspaceLayoutsJson").toString().toUtf8()).object();
    return layouts.value(workspace->property("timelineLayoutPreset").toString())
        .toObject().value(QStringLiteral("bottomDockWidth")).toDouble();
  };
  if (!wait_for_condition([&] {
        return persisted_bottom_dock_width() >= initial_dock_preferred_width + 20.0;
      }, 1500))
    return fail("Dragging the bottom dock divider must persist its wider preferred width");
  workspace->setProperty("bottomDockWidth", 500.0);
  QVariant layout_saved;
  if (!QMetaObject::invokeMethod(workspace, "saveCurrentTimelineWorkspaceLayout",
                                 Q_RETURN_ARG(QVariant, layout_saved)) ||
      !layout_saved.toBool())
    return fail("The resized bottom dock preference must be saved to the current workspace layout");
  process_layout_events(5);
  if (workspace->property("bottomDockWidth").toDouble() < 480.0 ||
      persisted_bottom_dock_width() < 480.0)
    return fail("The bottom dock should retain its 500-pixel preferred-width check");
  root->setProperty("width", 1180);
  process_layout_events(8);
  if (std::abs(workspace->property("bottomDockWidth").toDouble() - 500.0) > 0.01 ||
      std::abs(persisted_bottom_dock_width() - 500.0) > 0.01)
    return fail("A constrained window must retain the bottom dock's wider preferred size");
  root->setProperty("width", 1440);
  process_layout_events(8);

  const auto clip_a_id = document->currentClip().value("clipId").toString();
  if (!click(window, "timelineCurvePinTargetButton") ||
      !workspace->property("curveEditorPinned").toBool())
    return fail("The curve editor must pin its current clip target explicitly");
  document->selectClip(1);
  const auto clip_b_id_for_pin_edit = document->currentClip().value("clipId").toString();
  const auto clip_b_start_for_pin_edit = document->currentClip()
                                            .value("startSeconds").toDouble();
  document->seekSequenceSeconds(clip_b_start_for_pin_edit + 1.0);
  const auto pinned_edit_playhead = document->sequencePlayheadSeconds();
  auto* program_picker_after_selection = find_named_object(
      workspace, "timelineProgramAnimationParameterPicker");
  auto* selected_clip_keyframe_button = find_named_item(
      workspace, "timelineProgramAddKeyframeButton");
  const auto clip_b_details_for_program = document->timelineClipDetails(1);
  const auto program_projection_ready = [&] {
    program_picker_after_selection = find_named_object(
        workspace, "timelineProgramAnimationParameterPicker");
    selected_clip_keyframe_button = find_named_item(
        workspace, "timelineProgramAddKeyframeButton");
    const auto projected_clip = qml_value(workspace->property("currentClip")).toMap();
    if (program_picker_after_selection == nullptr || selected_clip_keyframe_button == nullptr ||
        projected_clip.value(QStringLiteral("clipId")).toString() != clip_b_id_for_pin_edit ||
        !workspace->property("selectedVisualClipActive").toBool() ||
        workspace->property("selectedAnimatedParameter").toString() != QStringLiteral("effect:Exposure") ||
        program_picker_after_selection->property("currentParameter").toString() != QStringLiteral("effect:Exposure") ||
        program_picker_after_selection->property("currentParameterAvailable").toBool()) {
      return false;
    }
    const auto options = qml_value(program_picker_after_selection->property("options")).toList();
    for (const auto& option : options) {
      if (option.toMap().value(QStringLiteral("parameter")).toString() ==
          QStringLiteral("effect:Exposure"))
        return false;
    }
    return item_visible(selected_clip_keyframe_button);
  };
  if (!wait_for_condition(program_projection_ready, 5000)) {
    const auto qml_clip = qml_value(workspace->property("currentClip")).toMap();
    const auto picker_options = program_picker_after_selection
        ? qml_value(program_picker_after_selection->property("options")).toList() : QVariantList{};
    std::cerr << "Pinned Program availability projection: documentClip="
              << document->currentClip().value("clipId").toString().toStdString()
              << " projectedClip=" << qml_clip.value("clipId").toString().toStdString()
              << " expectedClip=" << clip_b_id_for_pin_edit.toStdString()
              << " clipType=" << qml_clip.value("mediaType").toString().toStdString()
              << " selectionActive=" << workspace->property("selectedVisualClipActive").toBool()
              << " pinned=" << workspace->property("curveEditorPinned").toBool()
              << " curveTarget=" << workspace->property("curveEditorTargetClipId").toString().toStdString()
              << " selectedParameter=" << workspace->property("selectedAnimatedParameter").toString().toStdString()
              << " pickerParameter=" << (program_picker_after_selection
                                                ? program_picker_after_selection->property("currentParameter")
                                                      .toString().toStdString() : "<missing>")
              << " pickerAvailable=" << (program_picker_after_selection
                                               && program_picker_after_selection
                                                      ->property("currentParameterAvailable").toBool())
              << " pickerOptions=" << QJsonDocument::fromVariant(picker_options)
                                         .toJson(QJsonDocument::Compact).toStdString()
              << " clipAvailable=" << QJsonDocument::fromVariant(
                     clip_b_details_for_program.value("availableKeyframeParameters"))
                     .toJson(QJsonDocument::Compact).toStdString()
              << " playhead=" << document->sequencePlayheadSeconds()
              << " clipStart=" << clip_b_details_for_program.value("startSeconds").toDouble()
              << " clipDuration=" << clip_b_details_for_program.value("durationSeconds").toDouble()
              << " buttonExists=" << (selected_clip_keyframe_button != nullptr)
              << " buttonVisible=" << item_visible(selected_clip_keyframe_button)
              << " buttonEnabled=" << (selected_clip_keyframe_button
                                           && selected_clip_keyframe_button->isEnabled())
              << " buttonBounds=";
    const auto program_button_bounds = selected_clip_keyframe_button != nullptr
        ? item_scene_rect(selected_clip_keyframe_button) : QRectF{};
    std::cerr << program_button_bounds.x() << ',' << program_button_bounds.y() << ' '
              << program_button_bounds.width() << 'x' << program_button_bounds.height() << '\n';
    return fail("Program must project the selected clip before checking channel availability");
  }
  if (selected_clip_keyframe_button->isEnabled())
    return fail("Program must disable a pinned effect channel that is unavailable on the newly selected clip");
  if (clip_b_id_for_pin_edit.isEmpty() || clip_b_id_for_pin_edit == clip_a_id ||
      workspace->property("curveEditorTargetClipId").toString() != clip_a_id)
    return fail("A pinned curve must accept direct value and timing edits while another clip is selected");
  const auto pinned_edit_point_state = [&](int clip_index, double seconds) {
    for (const auto& item : document->timelineClipDetails(clip_index)
                                .value("keyframes").toList()) {
      const auto keyframe = item.toMap();
      if (keyframe.value("parameter").toString() == QStringLiteral("effect:Exposure") &&
          std::abs(keyframe.value("seconds").toDouble() - seconds) < 0.001)
        return keyframe.value("value").toDouble();
    }
    return std::numeric_limits<double>::quiet_NaN();
  };
  if (!QMetaObject::invokeMethod(curve, "selectCurveSecond",
                                 Q_ARG(QVariant, 2.0), Q_ARG(QVariant, false)))
    return fail("The pinned curve must select clip A's existing key while clip B remains selected");
  const auto selected_seconds_before_pinned_edit = qml_value(curve->property("selectedSeconds")).toList();
  const auto pinned_curve_clip_before_edit = qml_value(curve->property("curveClip")).toMap();
  const auto pinned_curve_keys_before_edit = qml_value(curve->property("curveKeyframes")).toList();
  const auto pinned_edit_invoked = QMetaObject::invokeMethod(
      curve, "editSelectedCurveKeyframe",
      Q_ARG(QVariant, QVariant(QStringLiteral("3.0"))),
      Q_ARG(QVariant, QVariant(QStringLiteral("0.42"))));
  if (!pinned_edit_invoked)
    return fail("The pinned curve must invoke its direct keyframe edit while clip B remains selected");
  process_layout_events(8);
  const auto pinned_edit_applied = wait_for_condition([&] {
    return std::isnan(pinned_edit_point_state(0, 2.0)) &&
           std::abs(pinned_edit_point_state(0, 3.0) - 0.42) < 0.001;
  }, 3000);
  if (!pinned_edit_applied) {
    const auto pinned_curve_clip_after_edit = qml_value(curve->property("curveClip")).toMap();
    const auto pinned_curve_keys_after_edit = qml_value(curve->property("curveKeyframes")).toList();
    const auto selected_seconds_after_pinned_edit = qml_value(curve->property("selectedSeconds")).toList();
    std::cerr << "Pinned curve edit routing: parameter="
              << workspace->property("curveEditorParameter").toString().toStdString()
              << " pinned=" << workspace->property("curveEditorPinned").toBool()
              << " targetId=" << workspace->property("curveEditorTargetClipId").toString().toStdString()
              << " selectedClip=" << document->currentClip().value("clipId").toString().toStdString()
              << " expectedSelection=" << clip_b_id_for_pin_edit.toStdString()
              << " playhead=" << document->sequencePlayheadSeconds()
              << " expectedPlayhead=" << pinned_edit_playhead
              << " A2=" << pinned_edit_point_state(0, 2.0)
              << " A3=" << pinned_edit_point_state(0, 3.0)
              << " B3=" << pinned_edit_point_state(1, 3.0)
              << " keyframeParameter="
              << pinned_curve_clip_before_edit.value("clipId").toString().toStdString()
              << " keyCountBefore=" << pinned_curve_keys_before_edit.size()
              << " keyCountAfter=" << pinned_curve_keys_after_edit.size()
              << " selectedSecondsBefore="
              << QJsonDocument::fromVariant(selected_seconds_before_pinned_edit)
                     .toJson(QJsonDocument::Compact).toStdString()
              << " selectedSecondsAfter="
              << QJsonDocument::fromVariant(selected_seconds_after_pinned_edit)
                     .toJson(QJsonDocument::Compact).toStdString()
              << " curveClipBefore="
              << QJsonDocument::fromVariant(pinned_curve_clip_before_edit)
                     .toJson(QJsonDocument::Compact).toStdString()
              << " curveClipAfter="
              << QJsonDocument::fromVariant(pinned_curve_clip_after_edit)
                     .toJson(QJsonDocument::Compact).toStdString() << '\n';
    return fail("The pinned curve's full keyframe projection must route the edit only to clip A");
  }
  if (!std::isnan(pinned_edit_point_state(0, 2.0)) ||
      std::abs(pinned_edit_point_state(0, 3.0) - 0.42) > 0.001 ||
      !std::isnan(pinned_edit_point_state(1, 3.0)) ||
      document->currentClip().value("clipId").toString() != clip_b_id_for_pin_edit ||
      std::abs(document->sequencePlayheadSeconds() - pinned_edit_playhead) > 0.001)
    return fail("A pinned curve edit must change only clip A while preserving clip B selection and playhead");
  if (!capture_main(QStringLiteral("timeline-animation-pinned-target.png")))
    return fail("The pinned clip target state must be capturable");
  document->undo();
  if (std::abs(pinned_edit_point_state(0, 2.0) - 0.6) > 0.001 ||
      !std::isnan(pinned_edit_point_state(0, 3.0)) ||
      document->currentClip().value("clipId").toString() != clip_b_id_for_pin_edit ||
      std::abs(document->sequencePlayheadSeconds() - pinned_edit_playhead) > 0.001)
    return fail("One undo must restore the pinned curve without disturbing clip B selection or playhead");
  document->redo();
  if (std::abs(pinned_edit_point_state(0, 3.0) - 0.42) > 0.001 ||
      std::abs(document->sequencePlayheadSeconds() - pinned_edit_playhead) > 0.001)
    return fail("Redo must restore the pinned curve edit and keep the sequence position stable");
  QPointer<QQuickItem> point_drag_area;
  const auto live_point_drag_area_ready = [&] {
    auto* item = find_named_item(window, QStringLiteral("timelineCurvePointDragArea0"));
    if (!item_visible(item) || item->window() != window)
      return false;
    point_drag_area = item;
    return true;
  };
  if (!wait_for_condition(live_point_drag_area_ready, 5000))
    return fail("The docked curve must expose a live point delegate in the main window");
  const QPointer<QQuickItem> original_point_drag_area = point_drag_area;
  const auto gesture_begin_invoked = QMetaObject::invokeMethod(curve, "beginCurveGesture");
  const auto gesture_active_after_begin = curve->property("curveGestureActive").toBool();
  QVariant curve_update_result;
  const auto curve_update_invoked = QMetaObject::invokeMethod(
      curve, "updateCurveKeyframe", Q_RETURN_ARG(QVariant, curve_update_result),
      Q_ARG(QVariant, 3.0), Q_ARG(QVariant, 3.5), Q_ARG(QVariant, 0.38));
  process_layout_events(6);
  const auto curve_update_applied =
      std::abs(pinned_edit_point_state(0, 3.5) - 0.38) < 0.001 &&
      std::isnan(pinned_edit_point_state(0, 3.0));
  point_drag_area.clear();
  const auto live_updated_point_area_ready = [&] {
    auto* item = find_named_item(window, QStringLiteral("timelineCurvePointDragArea0"));
    if (!item_visible(item) || item->window() != window)
      return false;
    point_drag_area = item;
    return true;
  };
  const auto updated_delegate_ready =
      wait_for_condition(live_updated_point_area_ready, 5000);
  const auto cancel_invoked = updated_delegate_ready && point_drag_area &&
      QMetaObject::invokeMethod(point_drag_area, "canceled");
  process_layout_events(4);
  const auto gesture_active_after_cancel = curve->property("curveGestureActive").toBool();
  if (!gesture_begin_invoked || !gesture_active_after_begin || !curve_update_invoked ||
      !curve_update_result.toBool() || !curve_update_applied || !updated_delegate_ready ||
      !cancel_invoked || gesture_active_after_cancel) {
    std::cerr << "Curve gesture cancellation: beginInvoked=" << gesture_begin_invoked
              << " activeAfterBegin=" << gesture_active_after_begin
              << " updateInvoked=" << curve_update_invoked
              << " updateResult=" << curve_update_result.toBool()
              << " updatedState=" << curve_update_applied
              << " updatedDelegateReady=" << updated_delegate_ready
              << " originalAreaAliveAfterUpdate=" << !original_point_drag_area.isNull()
              << " areaAliveAfterUpdate=" << !point_drag_area.isNull()
              << " cancelInvoked=" << cancel_invoked
              << " activeAfterCancel=" << gesture_active_after_cancel
              << " A3=" << pinned_edit_point_state(0, 3.0)
              << " A3.5=" << pinned_edit_point_state(0, 3.5)
              << " B3.5=" << pinned_edit_point_state(1, 3.5)
              << " selectedClip=" << document->currentClip().value("clipId").toString().toStdString()
              << " expectedSelection=" << clip_b_id_for_pin_edit.toStdString()
              << " playhead=" << document->sequencePlayheadSeconds()
              << " expectedPlayhead=" << pinned_edit_playhead << '\n';
    return fail("Canceling the live docked keyframe drag must end its continuous edit scope");
  }
  document->undo();
  if (std::abs(pinned_edit_point_state(0, 3.0) - 0.42) > 0.001 ||
      !std::isnan(pinned_edit_point_state(0, 3.5)) ||
      document->currentClip().value("clipId").toString() != clip_b_id_for_pin_edit ||
      std::abs(document->sequencePlayheadSeconds() - pinned_edit_playhead) > 0.001)
    return fail("One undo after a canceled curve drag must restore its complete gesture");
  document->redo();
  if (std::abs(pinned_edit_point_state(0, 3.5) - 0.38) > 0.001)
    return fail("Redo must restore a canceled drag as a single undoable curve gesture");
  document->undo();
  QMetaObject::invokeMethod(curve, "beginCurveGesture");
  if (!QMetaObject::invokeMethod(curve, "updateCurveKeyframe",
                                 Q_ARG(QVariant, 3.0), Q_ARG(QVariant, 4.0),
                                 Q_ARG(QVariant, 0.38)) ||
      !QMetaObject::invokeMethod(workspace, "closeTimelinePanel", Q_ARG(QVariant, QStringLiteral("keyframes"))) ||
      workspace->property("curveEditorOpen").toBool() ||
      curve->property("curveGestureActive").toBool())
    return fail("Closing the dock during a curve gesture must end the active gesture and close the editor");
  document->undo();
  if (std::abs(pinned_edit_point_state(0, 3.0) - 0.42) > 0.001 ||
      !std::isnan(pinned_edit_point_state(0, 4.0)) ||
      document->currentClip().value("clipId").toString() != clip_b_id_for_pin_edit ||
      std::abs(document->sequencePlayheadSeconds() - pinned_edit_playhead) > 0.001)
    return fail("One undo after a closed curve gesture must restore its full edit without moving the playhead");
  document->redo();
  if (std::abs(pinned_edit_point_state(0, 4.0) - 0.38) > 0.001)
    return fail("Redo must restore the closed curve gesture as one edit");
  workspace->setProperty("curveEditorOpen", true);
  if (!wait_for_condition([&] {
        return named_item_visible(window, "timelineKeyframeCurveGraph");
      }, 5000))
    return fail("The docked editor must reopen with its live curve content intact");
  auto* program_picker = find_named_object(workspace, "timelineProgramAnimationParameterPicker");
  if (!program_picker ||
      !QMetaObject::invokeMethod(program_picker, "parameterSelected",
                                 Q_ARG(QString, QStringLiteral("frameOffsetY"))) ||
      workspace->property("selectedAnimatedParameter").toString() != QStringLiteral("frameOffsetY") ||
      workspace->property("curveEditorParameter").toString() != QStringLiteral("frameOffsetY"))
    return fail("Program and curve channel selection must share one animation parameter");
  if (!QMetaObject::invokeMethod(curve, "curveKeyframeParameterOptions"))
    return fail("The curve editor must provide channels for its pinned target");
  auto* curve_picker = find_named_object(window, "timelineCurveParameterPicker");
  if (!curve_picker ||
      !QMetaObject::invokeMethod(curve_picker, "parameterSelected",
                                 Q_ARG(QString, QStringLiteral("frameRotationDegrees"))) ||
      workspace->property("selectedAnimatedParameter").toString() != QStringLiteral("frameRotationDegrees") ||
      program_picker->property("currentParameter").toString() != QStringLiteral("frameRotationDegrees") ||
      workspace->property("curveEditorParameter").toString() != QStringLiteral("frameRotationDegrees"))
    return fail("Curve and Program channel pickers must stay synchronized");

  document->setSelectedTrimStartSeconds(1.0);
  document->setSelectedTrimEndSeconds(8.0);
  if (!QMetaObject::invokeMethod(curve_picker, "parameterSelected",
                                 Q_ARG(QString, QStringLiteral("sourceTime"))))
    return fail("The pinned curve should expose its source-time channel");
  process_layout_events(5);
  const auto pinned_retime_clip = qml_value(curve->property("curveClip")).toMap();
  const auto selected_retime_clip = document->currentClip();
  const auto pinned_retime_bounds = qml_value(curve->property("parameterBounds")).toMap();
  if (pinned_retime_clip.value("clipId").toString() != clip_a_id ||
      selected_retime_clip.value("clipId").toString() != clip_b_id_for_pin_edit ||
      std::abs(pinned_retime_clip.value("trimStartSeconds").toDouble() -
               selected_retime_clip.value("trimStartSeconds").toDouble()) < 0.001 ||
      std::abs(pinned_retime_clip.value("trimEndSeconds").toDouble() -
               selected_retime_clip.value("trimEndSeconds").toDouble()) < 0.001 ||
      std::abs(pinned_retime_bounds.value("minimum").toDouble() -
               pinned_retime_clip.value("trimStartSeconds").toDouble()) > 0.001 ||
      std::abs(pinned_retime_bounds.value("maximum").toDouble() -
               pinned_retime_clip.value("trimEndSeconds").toDouble()) > 0.001)
    return fail("Pinned retime bounds must follow clip A when selected clip B has different trims");

  document->selectClip(1);
  const auto clip_b = document->currentClip();
  const auto clip_b_id = clip_b.value("clipId").toString();
  if (clip_b_id.isEmpty() || clip_b_id == clip_a_id ||
      !wait_for_condition([&] {
        const auto target = qml_value(curve->property("curveClip")).toMap();
        return workspace->property("curveEditorTargetClipId").toString() == clip_a_id &&
               target.value("clipId").toString() == clip_a_id &&
               !curve->property("curveTargetSelected").toBool();
      }, 5000))
    return fail("Pinning must retain the original clip when timeline selection changes");
  auto* follow_button = find_visible_named_item(window, "timelineCurvePinTargetButton");
  if (!follow_button || !follow_button->isEnabled() ||
      !click(window, "timelineCurvePinTargetButton") ||
      workspace->property("curveEditorPinned").toBool() ||
      workspace->property("curveEditorTargetClipId").toString() != clip_b_id ||
      qml_value(curve->property("curveClip")).toMap().value("clipId").toString() != clip_b_id)
    return fail("Follow Selection must retarget the graph and restore editing for the selected clip");

  if (!QMetaObject::invokeMethod(workspace, "openInspectorTab",
                                 Q_ARG(QVariant, QVariant(QStringLiteral("motion")))))
    return fail("The shared channel proof must open Motion controls");
  QObject* inspector_picker = nullptr;
  if (!wait_for_condition([&] {
        inspector_picker = find_named_object(workspace, "timelineInspectorKeyframeParameterPicker");
        return item_visible(qobject_cast<QQuickItem*>(inspector_picker));
      }, 5000)) return fail("The Inspector must instantiate its searchable animation channel picker");
  if (!QMetaObject::invokeMethod(inspector_picker, "parameterSelected",
                                 Q_ARG(QString, QStringLiteral("frameOffsetX"))) ||
      workspace->property("selectedAnimatedParameter").toString() != QStringLiteral("frameOffsetX") ||
      program_picker->property("currentParameter").toString() != QStringLiteral("frameOffsetX") ||
      workspace->property("curveEditorParameter").toString() != QStringLiteral("frameOffsetX"))
    return fail("Inspector, Program, and curve channel selections must remain synchronized");

  const auto clip_b_start = clip_b.value("startSeconds").toDouble();
  const auto clip_b_duration = clip_b.value("trimmedDurationSeconds").toDouble();
  document->seekSequenceSeconds(clip_b_start + std::max(0.001, std::min(0.5, clip_b_duration * 0.5)));
  process_layout_events(8);
  auto* transform_keyframe = find_visible_named_item(
      workspace, "timelineInspectorKeyframeToggleButton");
  auto* transform_curve = find_visible_named_item(
      workspace, "timelineInspectorCurveEditorButton");
  if (!transform_keyframe || !transform_keyframe->isEnabled() ||
      !transform_curve || !transform_curve->isEnabled())
    return fail("Transform must keep its keyframe diamond and curve editor entrypoint visible for the selected clip");
  document->seekSequenceSeconds(clip_b_start + clip_b_duration);
  process_layout_events(8);
  auto* curve_add = find_visible_named_item(window, "timelineCurveAddPointButton");
  auto* inspector_toggle = find_visible_named_item(workspace, "timelineInspectorKeyframeToggleButton");
  if (!curve_add || curve_add->isEnabled() || !inspector_toggle || inspector_toggle->isEnabled())
    return fail("At-playhead keyframe actions must disable at the selected clip's out point");
  if (curve->isVisible() || effect_keyframes() != 1)
    return fail("Outside-clip keyframe guards must preserve the selected effect curve");

  if (!click(window, "timelineDockMoveButton_bottom") ||
      !wait_for_condition([&] {
        auto* menu = find_named_object(workspace, "timelineDockMoveMenu_bottom");
        return menu && menu->property("opened").toBool();
      }, 3000) || !click(window, "timelineDockMenuFloat_bottom") ||
      !wait_for_condition([&] {
        auto* floating_graph = find_visible_named_item(curve, "timelineKeyframeCurveGraph");
        const auto graphs = find_named_items(window, "timelineKeyframeCurveGraph");
        const auto graph_still_docked = std::any_of(graphs.cbegin(), graphs.cend(),
            [&](const QQuickItem* graph) {
              return item_visible(graph) && graph->window() == window;
            });
        return !workspace->property("curveEditorDocked").toBool() &&
               panel_state_value("visible", QStringLiteral("keyframes")).toBool() &&
               panel_state_value("dock", QStringLiteral("keyframes")).toString() == QStringLiteral("floating") &&
               curve->isVisible() && item_visible(floating_graph) &&
               floating_graph->window() == curve && !graph_still_docked &&
               !item_is_inside(floating_graph, dock_host);
      }, 5000))
    return fail("The panel options menu must detach the same curve editor into its floating window");
  if (qml_value(curve->property("curveKeyframes")).toList().size() != 0 ||
      !curve->grabWindow().save(QDir(output_dir).filePath("detached-animation-target-curve.png")))
    return fail("The detached graph must retain the retargeted channel and remain capturable");
  captures.push_back(QStringLiteral("detached-animation-target-curve.png"));
  const auto detached_x = curve->x() + 12;
  const auto detached_y = curve->y() + 12;
  curve->setX(detached_x);
  curve->setY(detached_y);
  curve->resize(860, 640);
  process_layout_events(6);
  if (!workspace_settings ||
      std::abs(workspace_settings->property("curveEditorX").toDouble() - detached_x) > 1.0 ||
      std::abs(workspace_settings->property("curveEditorY").toDouble() - detached_y) > 1.0 ||
      std::abs(workspace_settings->property("curveEditorWidth").toDouble() - 860.0) > 1.0 ||
      std::abs(workspace_settings->property("curveEditorHeight").toDouble() - 640.0) > 1.0)
    return fail("The detached editor must persist its native window position and size");
  curve->close();
  process_layout_events(6);

  QJsonObject receipt{
      {QStringLiteral("passed"), true},
      {QStringLiteral("clipAId"), clip_a_id},
      {QStringLiteral("clipBId"), clip_b_id},
      {QStringLiteral("effectKeyframes"), effect_keyframes()},
      {QStringLiteral("curveEditorDockedAfterDetach"),
       workspace->property("curveEditorDocked").toBool()},
      {QStringLiteral("bottomDockWidth"),
       workspace->property("bottomDockWidth").toDouble()},
      {QStringLiteral("detachedWindow"), QJsonObject{
          {QStringLiteral("x"), detached_x}, {QStringLiteral("y"), detached_y},
          {QStringLiteral("width"), curve->width()}, {QStringLiteral("height"), curve->height()}}},
      {QStringLiteral("captures"), captures}};
  QFile receipt_file(QDir(output_dir).filePath(
      QStringLiteral("timeline-inspector-animation-proof.json")));
  if (!receipt_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      receipt_file.write(QJsonDocument(receipt).toJson(QJsonDocument::Indented)) <= 0) {
    return fail("Inspector animation proof could not write its receipt");
  }
  std::cout << "Timeline Inspector animation proof passed with " << captures.size()
            << " captures in " << output_dir.toStdString() << '\n';
  return EXIT_SUCCESS;
}
