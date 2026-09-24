    editor_preview.setImageProvider(nullptr);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return EXIT_SUCCESS;
  }
  editor_preview.setMaskPreviewMode(QStringLiteral("none"));
  QCoreApplication::processEvents();
  if (editor_preview.maskPreviewMode() != QStringLiteral("none") ||
      edit_mask_canvas_overlay->property("visible").toBool()) {
    return fail("mask preview None mode should hide active mask overlays while keeping mask state editable");
  }
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  QCoreApplication::processEvents();
  editor_preview.setActiveMaskInverted(true);
  QCoreApplication::processEvents();
  if (!active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Inverted"))) {
    return fail("mask toolbar should make active source inversion visible");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskCreationOperation",
          Q_ARG(QVariant, QVariant(QStringLiteral("replace")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("radial"))))) {
    return fail("edit workspace should expose replace mask creation through the shared controls");
  }
  QCoreApplication::processEvents();
  if (!mask_replace_button->property("active").toBool() ||
      editor_preview.maskCount() != initial_mask_count + 2 ||
      editor_preview.activeMaskOperation() != QStringLiteral("replace") ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Replace"))) {
    return fail("mask creation should support an explicit replace combine mode");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskCreationOperation",
          Q_ARG(QVariant, QVariant(QStringLiteral("subtract")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "startMaskBrushSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("subtract"))))) {
    return fail("edit workspace should start brush masking through the shared mask creation controls");
  }
  QCoreApplication::processEvents();
  if (mask_subtract_button == nullptr || !mask_subtract_button->property("active").toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("brush") ||
      editor_preview.maskCount() != initial_mask_count + 3 ||
      editor_preview.activeMaskOperation() != QStringLiteral("subtract")) {
    return fail("brush mask creation should honor the selected subtract combine mode");
  }
  trace_smoke_progress("brush mode activated");
  auto* brush_tool_hud = edit_workspace->findChild<QObject*>(QStringLiteral("brushToolHud"));
  auto* brush_add_button = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudAddButton"));
  auto* brush_erase_button = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudEraseButton"));
  auto* brush_done_button = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudDoneButton"));
  auto* brush_status_label = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudStatusLabel"));
  auto* brush_reset_defaults_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushHudResetDefaultsButton"));
  auto* brush_size_up_button = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudSizeUpButton"));
  auto* brush_size_drag_handle =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushHudSizeDragHandle"));
  auto* brush_size_drag_area =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushHudSizeDragArea"));
  auto* brush_size_field = edit_workspace->findChild<QObject*>(QStringLiteral("brushHudSizeField"));
  auto* edit_zoom_toolbar_for_brush = find_named_object(edit_workspace, QStringLiteral("editZoomToolbar"));
  auto* edit_action_toolbar_for_brush = find_named_object(edit_workspace, QStringLiteral("editActionToolbar"));
  auto* brush_size_down_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("brushSizeDownShortcut"));
  auto* brush_size_up_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("brushSizeUpShortcut"));
  auto* brush_add_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("brushAddShortcut"));
  auto* brush_erase_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("brushEraseShortcut"));
  auto* brush_done_return_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushDoneReturnShortcut"));
  auto* brush_done_enter_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushDoneEnterShortcut"));
  auto* brush_hardness_down_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushHardnessDownShortcut"));
  auto* brush_hardness_up_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushHardnessUpShortcut"));
  auto* brush_flow_down_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushFlowDownShortcut"));
  auto* brush_flow_up_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushFlowUpShortcut"));
  auto* brush_density_down_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushDensityDownShortcut"));
  auto* brush_density_up_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushDensityUpShortcut"));
  auto* brush_opacity_down_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushOpacityDownShortcut"));
  auto* brush_opacity_up_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushOpacityUpShortcut"));
  auto* mask_create_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskCreateShortcut"));
  auto* mask_operation_add_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskOperationAddShortcut"));
  auto* mask_operation_subtract_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskOperationSubtractShortcut"));
  auto* mask_operation_intersect_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskOperationIntersectShortcut"));
  auto* mask_invert_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskInvertShortcut"));
  auto* mask_overlay_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskOverlayShortcut"));
  auto* mask_outline_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskOutlineShortcut"));
  auto* layer_target_next_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("layerTargetNextShortcut"));
  auto* layer_target_previous_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("layerTargetPreviousShortcut"));
  auto* mask_target_next_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskTargetNextShortcut"));
  auto* mask_target_previous_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskTargetPreviousShortcut"));
  auto* base_image_target_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("baseImageTargetShortcut"));
  auto* mask_select_move_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskSelectMoveShortcut"));
  auto* mask_lasso_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskLassoShortcut"));
  auto* mask_polygon_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskPolygonShortcut"));
  auto* mask_delete_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskDeleteShortcut"));
  auto* mask_rename_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("maskRenameShortcut"));
  auto* active_mask_rename_dialog =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeMaskRenameDialog"));
  auto* brush_cursor_ring = edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportBrushCursorRing"));
  auto* brush_cursor_outer_halo_ring =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportBrushCursorOuterHaloRing"));
  auto* brush_cursor_mode_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportBrushCursorModeLabel"));
  auto* brush_canvas_overlay =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportMaskCanvasOverlay"));
  auto* brush_context_panel = edit_workspace->findChild<QObject*>(QStringLiteral("brushContextPanel"));
  auto* brush_context_layer_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextLayerNameLabel"));
  auto* brush_context_mask_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextMaskNameLabel"));
  auto* brush_context_mask_state =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextMaskStateLabel"));
  auto* brush_context_mask_chip =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextMaskChip"));
  auto* brush_context_add_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextAddButton"));
  auto* brush_context_erase_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextEraseButton"));
  auto* brush_context_overlay_toggle =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextOverlayToggle"));
  auto* brush_context_compact_panel =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextCompactBrushPanel"));
  auto* brush_context_size_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextBrushSizeLabel"));
  auto* brush_context_flow_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextBrushFlowLabel"));
  auto* brush_context_hardness_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextBrushHardnessLabel"));
  auto* brush_context_density_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextBrushDensityLabel"));
  auto* brush_context_opacity_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextBrushOpacityLabel"));
  auto* brush_context_density_down_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextDensityDownButton"));
  auto* brush_context_density_up_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextDensityUpButton"));
  auto* brush_context_opacity_down_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextOpacityDownButton"));
  auto* brush_context_opacity_up_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextOpacityUpButton"));
  auto* brush_context_exposure_slider =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextExposureSlider"));
  auto* brush_context_temperature_slider =
      edit_workspace->findChild<QObject*>(QStringLiteral("brushContextTemperatureSlider"));
  auto* brush_inspector_settings_group =
      find_named_object(edit_workspace, QStringLiteral("maskFirstBrushSettingsGroup"));
  auto* brush_mask_density_slider =
      find_named_object(edit_workspace, QStringLiteral("maskFirstDensitySlider"));
  auto* brush_mask_opacity_slider =
      find_named_object(edit_workspace, QStringLiteral("maskFirstOpacitySlider"));
  auto* brush_mode_menu_button =
      find_named_object(edit_workspace, QStringLiteral("brushHudModeMenuButton"));
  auto* brush_tuning_menu_button =
      find_named_object(edit_workspace, QStringLiteral("brushHudTuningMenuButton"));
	  QVariant brush_ready;
	  QVariant active_mask_context_signature;
	  QVariant active_mask_layer_context_signature;
  const bool active_mask_context_invoked = QMetaObject::invokeMethod(
      edit_workspace,
      "activeMaskEditingContextSignature",
      Q_RETURN_ARG(QVariant, active_mask_context_signature));
  const bool active_mask_layer_context_invoked = QMetaObject::invokeMethod(
      edit_workspace,
      "activeMaskLayerContextSignature",
      Q_RETURN_ARG(QVariant, active_mask_layer_context_signature));
	  if (brush_tool_hud == nullptr || brush_add_button == nullptr || brush_erase_button == nullptr ||
      brush_done_button == nullptr || brush_status_label == nullptr ||
      brush_mode_menu_button == nullptr ||
      brush_tuning_menu_button == nullptr ||
      brush_reset_defaults_button == nullptr || brush_size_up_button == nullptr ||
      brush_size_drag_handle == nullptr || brush_size_drag_area == nullptr ||
      brush_size_field == nullptr ||
      edit_zoom_toolbar_for_brush == nullptr || edit_action_toolbar_for_brush == nullptr ||
      brush_size_down_shortcut == nullptr || brush_size_up_shortcut == nullptr ||
      brush_add_shortcut == nullptr || brush_erase_shortcut == nullptr ||
      brush_done_return_shortcut == nullptr || brush_done_enter_shortcut == nullptr ||
      brush_hardness_down_shortcut == nullptr || brush_hardness_up_shortcut == nullptr ||
      brush_flow_down_shortcut == nullptr || brush_flow_up_shortcut == nullptr ||
      brush_density_down_shortcut == nullptr || brush_density_up_shortcut == nullptr ||
      brush_opacity_down_shortcut == nullptr || brush_opacity_up_shortcut == nullptr ||
      mask_create_shortcut == nullptr || mask_operation_add_shortcut == nullptr ||
      mask_operation_subtract_shortcut == nullptr || mask_operation_intersect_shortcut == nullptr ||
      mask_invert_shortcut == nullptr || mask_overlay_shortcut == nullptr ||
      mask_outline_shortcut == nullptr || layer_target_next_shortcut == nullptr ||
      layer_target_previous_shortcut == nullptr ||
      mask_target_next_shortcut == nullptr || mask_target_previous_shortcut == nullptr ||
      base_image_target_shortcut == nullptr ||
      mask_select_move_shortcut == nullptr || mask_lasso_shortcut == nullptr ||
      mask_polygon_shortcut == nullptr ||
      mask_delete_shortcut == nullptr || mask_rename_shortcut == nullptr ||
      active_mask_rename_dialog == nullptr ||
      brush_cursor_ring == nullptr || brush_cursor_outer_halo_ring == nullptr ||
      brush_cursor_mode_label == nullptr ||
      brush_canvas_overlay == nullptr || brush_context_panel == nullptr ||
      brush_context_layer_label == nullptr || brush_context_mask_label == nullptr ||
      brush_context_mask_state == nullptr || brush_context_mask_chip == nullptr ||
      brush_context_add_button == nullptr || brush_context_erase_button == nullptr ||
      brush_context_overlay_toggle == nullptr || brush_context_compact_panel == nullptr ||
      brush_context_size_label == nullptr || brush_context_flow_label == nullptr ||
      brush_context_hardness_label == nullptr || brush_context_density_label == nullptr ||
      brush_context_opacity_label == nullptr || brush_context_density_down_button == nullptr ||
      brush_context_density_up_button == nullptr || brush_context_opacity_down_button == nullptr ||
      brush_context_opacity_up_button == nullptr ||
      brush_context_exposure_slider == nullptr ||
      brush_context_temperature_slider == nullptr ||
      brush_inspector_settings_group == nullptr ||
      brush_mask_density_slider == nullptr ||
      brush_mask_opacity_slider == nullptr ||
      brush_canvas_overlay->property("outlineRenderContract").toString() !=
          QStringLiteral("anti-aliased|zoom-stable|pixel-aligned:v1") ||
      !QMetaObject::invokeMethod(edit_workspace, "brushToolReady", Q_RETURN_ARG(QVariant, brush_ready)) ||
      !brush_ready.toBool() || !brush_tool_hud->property("visible").toBool() ||
      !brush_tool_hud->property("hudContract").toString().contains(QStringLiteral("specialized-tool-hud:v4|tool:brush|component:BrushToolHud")) ||
      !brush_tool_hud->property("hudContract").toString().contains(QStringLiteral("quick-tuning:size-flow-hardness")) ||
      !brush_tool_hud->property("hudContract").toString().contains(QStringLiteral("commit-action:true")) ||
      !brush_tool_hud->property("hudContract").toString().contains(QStringLiteral("no-elide:true")) ||
      !brush_tool_hud->property("brushPageContract").toString().contains(QStringLiteral("brush-page:v3")) ||
      !brush_tool_hud->property("brushPageContract").toString().contains(
          QStringLiteral("tuning:menu-size-flow-hard")) ||
      !brush_tool_hud->property("brushPageContract").toString().contains(
          QStringLiteral("collapsed:size-drag-field-buttons")) ||
      !brush_tool_hud->property("brushPageContract").toString().contains(
          QStringLiteral("advanced:inspector-density-opacity-more-pressure-reset")) ||
      !brush_tool_hud->property("brushPageContract").toString().contains(
          QStringLiteral("diagnostic-size-hidden:true")) ||
      !brush_tool_hud->property("brushPageContract").toString().contains(
          QStringLiteral("context-panel:hidden-by-default")) ||
      !item_visible(qobject_cast<QQuickItem*>(brush_inspector_settings_group)) ||
      !item_visible(qobject_cast<QQuickItem*>(brush_mask_density_slider)) ||
      !item_visible(qobject_cast<QQuickItem*>(brush_mask_opacity_slider)) ||
      brush_inspector_settings_group->property("brushInspectorOwnershipContract").toString() !=
          QStringLiteral("brush-inspector-controls:v1|visible-during-brush:true|owns:size-flow-hardness|parent-controls:density-opacity|more:presets-pressure-reset|hud:quick-only") ||
      brush_context_panel->property("visible").toBool() ||
      !brush_context_panel->property("defaultVisibilityContract").toString().contains(
          QStringLiteral("brush-context-default:v2|hidden:true")) ||
      brush_cursor_ring->property("brushCursorVisibilityContract").toString() !=
          QStringLiteral("persistent-last-position|dual-contrast-ring|hardness-inner-ring:v1") ||
      !brush_context_layer_label->property("text").toString().contains(
          QStringLiteral("Layer")) ||
      !brush_context_mask_label->property("text").toString().contains(QStringLiteral("Brush")) ||
      !brush_context_mask_state->property("text").toString().contains(QStringLiteral("Brush selection")) ||
      !brush_context_mask_state->property("text").toString().contains(QStringLiteral("Subtract")) ||
      !brush_context_mask_state->property("text").toString().contains(QStringLiteral("Overlay")) ||
      brush_context_mask_chip->property("visible").toBool() ||
      brush_context_compact_panel->property("brushPanelContract").toString() !=
          QStringLiteral("size-flow-hardness-density-opacity|compact:v2|visible:false|tuning-menu:true|inspector-preserved:true") ||
      brush_context_compact_panel->property("visible").toBool() ||
      !brush_context_size_label->property("text").toString().contains(QStringLiteral("Size")) ||
      !brush_context_flow_label->property("text").toString().contains(QStringLiteral("Flow")) ||
      !brush_context_hardness_label->property("text").toString().contains(QStringLiteral("Hard")) ||
      !brush_context_density_label->property("text").toString().contains(QStringLiteral("Density")) ||
      !brush_context_opacity_label->property("text").toString().contains(QStringLiteral("Opacity")) ||
      brush_mode_menu_button->property("text").toString() != QStringLiteral("Brush Erase") ||
      !brush_mode_menu_button->property("visible").toBool() ||
      !brush_mode_menu_button->property("menuContract").toString().contains(QStringLiteral("items:")) ||
      brush_mode_menu_button->property("menuContract").toString().contains(QStringLiteral("items:5")) ||
      brush_mode_menu_button->property("menuContract").toString().contains(QStringLiteral("checked:0")) ||
      !brush_mode_menu_button->property("menuContract").toString().contains(QStringLiteral("disabled:0")) ||
      !brush_mode_menu_button->property("menuContract").toString().contains(QStringLiteral("danger:0")) ||
      !brush_mode_menu_button->property("tooltipText").toString().contains(QStringLiteral("presets")) ||
      !brush_mode_menu_button->property("readableTextContract").toString().contains(QStringLiteral("ellipsis:none")) ||
      brush_tuning_menu_button->property("text").toString() != QStringLiteral("Tuning") ||
      !brush_tuning_menu_button->property("menuContract").toString().contains(QStringLiteral("items:6")) ||
      !brush_tuning_menu_button->property("tooltipText").toString().contains(QStringLiteral("size")) ||
      !brush_tuning_menu_button->property("tooltipText").toString().contains(QStringLiteral("flow")) ||
      !brush_tuning_menu_button->property("tooltipText").toString().contains(QStringLiteral("hardness")) ||
      brush_tuning_menu_button->property("tooltipText").toString().contains(QStringLiteral("density")) ||
      brush_reset_defaults_button->property("visible").toBool() ||
      !studio_workspace_source.contains(QStringLiteral("brush-reset-defaults")) ||
      !studio_workspace_source.contains(QStringLiteral("function resetActiveBrushDefaults()")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushPresetMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushCustomizationMenuItems()")) ||
      !studio_workspace_source.contains(QStringLiteral("function brushHudModeMenuItems()")) ||
      brush_reset_defaults_button->property("brushDefaultContract").toString() !=
          QStringLiteral("brush-defaults:v1|size:6|flow:100|hardness:45|density:100|opacity:100|spacing:75|smoothing:18|pressure:0/0/0|reset:true|operation-preserved:true") ||
      !brush_reset_defaults_button->property("tooltip").toString().contains(QStringLiteral("Reset brush size, flow, hardness, density, opacity")) ||
      brush_done_button->property("text").toString() != QStringLiteral("Done") ||
      !brush_done_return_shortcut->property("enabled").toBool() ||
      !brush_done_enter_shortcut->property("enabled").toBool() ||
      !brush_size_down_shortcut->property("enabled").toBool() ||
      !brush_size_up_shortcut->property("enabled").toBool() ||
      !brush_hardness_down_shortcut->property("enabled").toBool() ||
      !brush_hardness_up_shortcut->property("enabled").toBool() ||
      !brush_flow_down_shortcut->property("enabled").toBool() ||
      !brush_flow_up_shortcut->property("enabled").toBool() ||
      !brush_density_down_shortcut->property("enabled").toBool() ||
      !brush_density_up_shortcut->property("enabled").toBool() ||
      !brush_opacity_down_shortcut->property("enabled").toBool() ||
      !brush_opacity_up_shortcut->property("enabled").toBool() ||
      !brush_add_shortcut->property("enabled").toBool() ||
      !brush_erase_shortcut->property("enabled").toBool() ||
      !mask_outline_shortcut->property("enabled").toBool() ||
      !layer_target_next_shortcut->property("enabled").toBool() ||
      !layer_target_previous_shortcut->property("enabled").toBool() ||
      !mask_target_next_shortcut->property("enabled").toBool() ||
      !mask_target_previous_shortcut->property("enabled").toBool() ||
      !base_image_target_shortcut->property("enabled").toBool() ||
      !mask_select_move_shortcut->property("enabled").toBool() ||
      !mask_lasso_shortcut->property("enabled").toBool() ||
      !mask_polygon_shortcut->property("enabled").toBool() ||
	      active_canvas_tool_chip->property("visible").toBool() ||
	      !edit_workspace->property("activeCanvasToolChromeReduced").toBool() ||
      mask_creation_toolbar->property("visible").toBool() ||
      mask_refine_toolbar->property("visible").toBool() ||
      mask_stack_strip->property("visible").toBool() ||
      edit_zoom_toolbar_for_brush->property("visible").toBool() ||
      edit_action_toolbar_for_brush->property("visible").toBool() ||
	      !active_mask_context_invoked ||
	      active_mask_context_signature.toString() !=
	          QStringLiteral("tool:brush|mask:false|brush:true|retouch:false|count:1") ||
	      !active_mask_layer_context_invoked ||
      !active_mask_layer_context_signature.toString().contains(QStringLiteral("layer:")) ||
		      !active_mask_layer_context_signature.toString().contains(QStringLiteral("|mask:")) ||
		      !active_mask_layer_context_signature.toString().contains(QStringLiteral("operation:Subtract")) ||
		      !active_mask_layer_context_signature.toString().contains(QStringLiteral("preview:overlay")) ||
		      !brush_erase_button->property("active").toBool()) {
    std::cerr << "Brush HUD state:"
              << " ready=" << brush_ready.toBool()
              << " hud=" << (brush_tool_hud == nullptr ? "null" : "present")
              << " hudVisible=" << (brush_tool_hud != nullptr && brush_tool_hud->property("visible").toBool())
              << " hudContract="
              << (brush_tool_hud == nullptr
                      ? std::string()
                      : brush_tool_hud->property("hudContract").toString().toStdString())
              << " pageContract="
              << (brush_tool_hud == nullptr
                      ? std::string()
                      : brush_tool_hud->property("brushPageContract").toString().toStdString())
              << " modeText="
              << (brush_mode_menu_button == nullptr
                      ? std::string()
                      : brush_mode_menu_button->property("text").toString().toStdString())
              << " modeMenu="
              << (brush_mode_menu_button == nullptr
                      ? std::string()
                      : brush_mode_menu_button->property("menuContract").toString().toStdString())
              << " tuningText="
              << (brush_tuning_menu_button == nullptr
                      ? std::string()
                      : brush_tuning_menu_button->property("text").toString().toStdString())
              << " tuningMenu="
              << (brush_tuning_menu_button == nullptr
                      ? std::string()
                      : brush_tuning_menu_button->property("menuContract").toString().toStdString())
              << " modeTooltip="
              << (brush_mode_menu_button == nullptr
                      ? std::string()
                      : brush_mode_menu_button->property("tooltipText").toString().toStdString())
              << " tuningTooltip="
              << (brush_tuning_menu_button == nullptr
                      ? std::string()
                      : brush_tuning_menu_button->property("tooltipText").toString().toStdString())
              << " contextVisible="
              << (brush_context_panel != nullptr && brush_context_panel->property("visible").toBool())
              << " compactPanelVisible="
              << (brush_context_compact_panel != nullptr &&
                  brush_context_compact_panel->property("visible").toBool())
              << " compactContract="
              << (brush_context_compact_panel == nullptr
                      ? std::string()
                      : brush_context_compact_panel->property("brushPanelContract").toString().toStdString())
              << " contextState="
              << (brush_context_mask_state == nullptr
                      ? std::string()
                      : brush_context_mask_state->property("text").toString().toStdString())
              << " sizeText="
              << (brush_context_size_label == nullptr
                      ? std::string()
                      : brush_context_size_label->property("text").toString().toStdString())
              << " flowText="
              << (brush_context_flow_label == nullptr
                      ? std::string()
                      : brush_context_flow_label->property("text").toString().toStdString())
              << " overlayActive="
              << (brush_context_overlay_toggle != nullptr &&
                  brush_context_overlay_toggle->property("active").toBool())
              << " resetVisible="
              << (brush_reset_defaults_button != nullptr &&
                  brush_reset_defaults_button->property("visible").toBool())
              << " resetContract="
              << (brush_reset_defaults_button == nullptr
                      ? std::string()
                      : brush_reset_defaults_button->property("brushDefaultContract").toString().toStdString())
              << " doneText="
              << (brush_done_button == nullptr
                      ? std::string()
                      : brush_done_button->property("text").toString().toStdString())
              << " eraseActive="
              << (brush_erase_button != nullptr && brush_erase_button->property("active").toBool())
              << " exposureVisible="
              << (brush_context_exposure_slider != nullptr &&
                  brush_context_exposure_slider->property("visible").toBool())
              << " temperatureVisible="
              << (brush_context_temperature_slider != nullptr &&
                  brush_context_temperature_slider->property("visible").toBool())
              << " chromeReduced="
              << edit_workspace->property("activeCanvasToolChromeReduced").toBool()
              << " shortcutSizeDown="
              << (brush_size_down_shortcut != nullptr &&
                  brush_size_down_shortcut->property("enabled").toBool())
              << " shortcutSizeUp="
              << (brush_size_up_shortcut != nullptr &&
                  brush_size_up_shortcut->property("enabled").toBool())
              << " shortcutDoneReturn="
              << (brush_done_return_shortcut != nullptr &&
                  brush_done_return_shortcut->property("enabled").toBool())
              << " shortcutDoneEnter="
              << (brush_done_enter_shortcut != nullptr &&
                  brush_done_enter_shortcut->property("enabled").toBool())
              << " shortcutHardDown="
              << (brush_hardness_down_shortcut != nullptr &&
                  brush_hardness_down_shortcut->property("enabled").toBool())
              << " shortcutHardUp="
              << (brush_hardness_up_shortcut != nullptr &&
                  brush_hardness_up_shortcut->property("enabled").toBool())
              << " shortcutFlowDown="
              << (brush_flow_down_shortcut != nullptr &&
                  brush_flow_down_shortcut->property("enabled").toBool())
              << " shortcutFlowUp="
              << (brush_flow_up_shortcut != nullptr &&
                  brush_flow_up_shortcut->property("enabled").toBool())
              << " shortcutDensityDown="
              << (brush_density_down_shortcut != nullptr &&
                  brush_density_down_shortcut->property("enabled").toBool())
              << " shortcutDensityUp="
              << (brush_density_up_shortcut != nullptr &&
                  brush_density_up_shortcut->property("enabled").toBool())
              << " shortcutOpacityDown="
              << (brush_opacity_down_shortcut != nullptr &&
                  brush_opacity_down_shortcut->property("enabled").toBool())
              << " shortcutOpacityUp="
              << (brush_opacity_up_shortcut != nullptr &&
                  brush_opacity_up_shortcut->property("enabled").toBool())
              << " shortcutBrushAdd="
              << (brush_add_shortcut != nullptr && brush_add_shortcut->property("enabled").toBool())
              << " shortcutBrushErase="
              << (brush_erase_shortcut != nullptr && brush_erase_shortcut->property("enabled").toBool())
              << " shortcutMaskCreate="
              << (mask_create_shortcut != nullptr && mask_create_shortcut->property("enabled").toBool())
              << " shortcutMaskAdd="
              << (mask_operation_add_shortcut != nullptr &&
                  mask_operation_add_shortcut->property("enabled").toBool())
              << " shortcutMaskSubtract="
              << (mask_operation_subtract_shortcut != nullptr &&
                  mask_operation_subtract_shortcut->property("enabled").toBool())
              << " shortcutMaskIntersect="
              << (mask_operation_intersect_shortcut != nullptr &&
                  mask_operation_intersect_shortcut->property("enabled").toBool())
              << " shortcutMaskInvert="
              << (mask_invert_shortcut != nullptr && mask_invert_shortcut->property("enabled").toBool())
              << " shortcutMaskOverlay="
              << (mask_overlay_shortcut != nullptr && mask_overlay_shortcut->property("enabled").toBool())
              << " shortcutOutline="
              << (mask_outline_shortcut != nullptr &&
                  mask_outline_shortcut->property("enabled").toBool())
              << " shortcutLayerNext="
              << (layer_target_next_shortcut != nullptr &&
                  layer_target_next_shortcut->property("enabled").toBool())
              << " shortcutLayerPrev="
              << (layer_target_previous_shortcut != nullptr &&
                  layer_target_previous_shortcut->property("enabled").toBool())
              << " shortcutMaskNext="
              << (mask_target_next_shortcut != nullptr &&
                  mask_target_next_shortcut->property("enabled").toBool())
              << " shortcutMaskPrev="
              << (mask_target_previous_shortcut != nullptr &&
                  mask_target_previous_shortcut->property("enabled").toBool())
              << " shortcutBase="
              << (base_image_target_shortcut != nullptr &&
                  base_image_target_shortcut->property("enabled").toBool())
              << " shortcutSelectMove="
              << (mask_select_move_shortcut != nullptr &&
                  mask_select_move_shortcut->property("enabled").toBool())
              << " shortcutLasso="
              << (mask_lasso_shortcut != nullptr && mask_lasso_shortcut->property("enabled").toBool())
              << " shortcutPolygon="
              << (mask_polygon_shortcut != nullptr &&
                  mask_polygon_shortcut->property("enabled").toBool())
              << " activeChipVisible="
              << (active_canvas_tool_chip != nullptr && active_canvas_tool_chip->property("visible").toBool())
              << " maskToolbarVisible="
              << (mask_creation_toolbar != nullptr && mask_creation_toolbar->property("visible").toBool())
              << " refineToolbarVisible="
              << (mask_refine_toolbar != nullptr && mask_refine_toolbar->property("visible").toBool())
              << " stackVisible="
              << (mask_stack_strip != nullptr && mask_stack_strip->property("visible").toBool())
              << " zoomVisible="
              << (edit_zoom_toolbar_for_brush != nullptr &&
                  edit_zoom_toolbar_for_brush->property("visible").toBool())
              << " actionVisible="
              << (edit_action_toolbar_for_brush != nullptr &&
                  edit_action_toolbar_for_brush->property("visible").toBool())
              << " activeContextInvoked=" << active_mask_context_invoked
              << " layerContextInvoked=" << active_mask_layer_context_invoked
              << " activeContext=" << active_mask_context_signature.toString().toStdString()
              << " layerContext=" << active_mask_layer_context_signature.toString().toStdString()
              << '\n';
	    return fail("active brush masking should own the canvas HUD without generic edit or mask-stack toolbar overlap");
	  }
  QVariant brush_space_pan_ready;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "maskSpacePanShortcutActive",
          Q_RETURN_ARG(QVariant, brush_space_pan_ready)) ||
      !brush_space_pan_ready.toBool()) {
    return fail("Space-pan should be available while the brush mask tool is active");
  }
  const auto brush_flow_before_size_source = editor_preview.activeMaskBrushPressureFlow();
  const auto brush_opacity_before_size_source = editor_preview.activeMaskBrushPressureOpacity();
  const bool brush_size_source_pressure_invoked = QMetaObject::invokeMethod(
      edit_workspace,
      "handleEditRailMenuAction",
      Q_ARG(QVariant, QVariant(QStringLiteral("brush"))),
      Q_ARG(QVariant, QVariant(QStringLiteral("brush-size-source-pressure"))));
  QCoreApplication::processEvents();
  if (!brush_size_source_pressure_invoked ||
      editor_preview.activeMaskBrushPressureSize() < 0.60 ||
      std::abs(editor_preview.activeMaskBrushPressureFlow() - brush_flow_before_size_source) > 1e-6 ||
      std::abs(editor_preview.activeMaskBrushPressureOpacity() - brush_opacity_before_size_source) > 1e-6) {
    std::cerr << "Brush pressure size-source state: invoked="
              << brush_size_source_pressure_invoked
              << " size=" << editor_preview.activeMaskBrushPressureSize()
              << " flow=" << editor_preview.activeMaskBrushPressureFlow()
              << " opacity=" << editor_preview.activeMaskBrushPressureOpacity()
              << '\n';
    return fail("brush menu should expose pressure as a size source without changing other response channels");
  }
  const bool brush_pressure_size_flow_invoked = QMetaObject::invokeMethod(
      edit_workspace,
      "handleEditRailMenuAction",
      Q_ARG(QVariant, QVariant(QStringLiteral("brush"))),
      Q_ARG(QVariant, QVariant(QStringLiteral("brush-pressure-size-flow"))));
  QCoreApplication::processEvents();
  if (!brush_pressure_size_flow_invoked ||
      editor_preview.activeMaskBrushPressureSize() < 0.60 ||
      editor_preview.activeMaskBrushPressureFlow() < 0.30 ||
      editor_preview.activeMaskBrushPressureOpacity() > 1e-6) {
    std::cerr << "Brush pressure size-flow state: invoked="
              << brush_pressure_size_flow_invoked
              << " size=" << editor_preview.activeMaskBrushPressureSize()
              << " flow=" << editor_preview.activeMaskBrushPressureFlow()
              << " opacity=" << editor_preview.activeMaskBrushPressureOpacity()
              << '\n';
    return fail("brush menu should expose a combined size and flow pressure response");
  }
  const bool brush_pressure_off_invoked = QMetaObject::invokeMethod(
      edit_workspace,
      "handleEditRailMenuAction",
      Q_ARG(QVariant, QVariant(QStringLiteral("brush"))),
      Q_ARG(QVariant, QVariant(QStringLiteral("brush-pressure-off"))));
  QCoreApplication::processEvents();
  if (!brush_pressure_off_invoked ||
      editor_preview.activeMaskBrushPressureSize() > 1e-6 ||
      editor_preview.activeMaskBrushPressureFlow() > 1e-6 ||
      editor_preview.activeMaskBrushPressureOpacity() > 1e-6) {
    std::cerr << "Brush pressure off state: invoked="
              << brush_pressure_off_invoked
              << " size=" << editor_preview.activeMaskBrushPressureSize()
              << " flow=" << editor_preview.activeMaskBrushPressureFlow()
              << " opacity=" << editor_preview.activeMaskBrushPressureOpacity()
              << '\n';
    return fail("brush menu should expose a pressure-off mode");
  }
  trace_smoke_progress("brush pressure menu checked");
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "handleEditRailMenuAction",
          Q_ARG(QVariant, QVariant(QStringLiteral("brush"))),
          Q_ARG(QVariant, QVariant(QStringLiteral("brush-preset:Soft Round")))) ||
      editor_preview.activeMaskBrushPreset() != QStringLiteral("Soft Round")) {
    return fail("brush menu should apply named brush presets from the controller");
  }
  trace_smoke_progress("brush preset menu checked");
  edit_workspace->setProperty("spacePanActive", true);
  QCoreApplication::processEvents();
  if (!edit_workspace->property("spacePanActive").toBool()) {
    return fail("holding Space should enter the Studio Space-pan state while masking");
  }
  edit_workspace->setProperty("spacePanActive", false);
  QCoreApplication::processEvents();
  QVariant brush_target_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
	      "activeEditTargetSignature",
	      Q_RETURN_ARG(QVariant, brush_target_signature)) ||
	  active_edit_target_bar->property("targetScope").toString() != QStringLiteral("mask") ||
	  active_edit_target_mode_label->property("text").toString() != QStringLiteral("Selection") ||
      active_edit_target_breadcrumb_label->property("text").toString().contains(QStringLiteral("Base Image >")) ||
	      !active_edit_target_bar->property("visualContract").toString().contains(QStringLiteral("compact-copy:true")) ||
	      !active_edit_target_bar->property("visualContract").toString().contains(QStringLiteral("implementation-path:false")) ||
	      !active_edit_target_bar->property("visualContract").toString().contains(QStringLiteral("inspector-context:visible")) ||
	      !item_visible(qobject_cast<QQuickItem*>(active_edit_target_bar)) ||
	      !active_edit_target_breadcrumb_label->property("text").toString().contains(QStringLiteral("Brush")) ||
	      !brush_target_signature.toString().contains(QStringLiteral("scope:mask")) ||
	      brush_target_signature.toString().contains(QStringLiteral("breadcrumb:Base Image >")) ||
	      !brush_target_signature.toString().contains(QStringLiteral("inspector-copy:hidden")) ||
	      !brush_target_signature.toString().contains(QStringLiteral("Brush selection")) ||
	      !brush_target_signature.toString().contains(QStringLiteral("Subtract"))) {
    std::cerr << "Active brush target state: scope="
              << active_edit_target_bar->property("targetScope").toString().toStdString()
              << " barVisible=" << active_edit_target_bar->property("visible").toBool()
              << " layerProxyVisible="
              << active_adjustment_layer_target_bar->property("visible").toBool()
              << " mode="
              << active_edit_target_mode_label->property("text").toString().toStdString()
              << " breadcrumb="
              << active_edit_target_breadcrumb_label->property("text").toString().toStdString()
              << " status="
              << active_adjustment_layer_target_status_label->property("text").toString().toStdString()
              << " signature=" << brush_target_signature.toString().toStdString()
              << '\n';
	    return fail("active target bar should make brush mask editing explicit with layer and mask breadcrumb");
  }
  trace_smoke_progress("brush active target checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_ACTIVE_TARGET")) {
    return EXIT_SUCCESS;
  }
  const auto global_exposure_before_layer_adjust = editor_preview.exposure();
  const auto active_brush_adjustment_layer_index =
      edit_workspace->property("activePhotoLayerIndex").toInt();
  const auto active_brush_mask_set_id =
      editor_preview.maskStack()
          .at(editor_preview.activeMaskIndex())
          .toMap()
          .value(QStringLiteral("maskSetId"))
          .toString();
  QVariant layer_adjusted;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, layer_adjusted),
          Q_ARG(QVariant, QVariant(QStringLiteral("exposure"))),
          Q_ARG(QVariant, QVariant(0.31))) ||
      !layer_adjusted.toBool() ||
      editor_preview.photoLayerCount() <= 0 ||
      active_brush_adjustment_layer_index < 0 ||
      active_brush_adjustment_layer_index >= editor_preview.photoLayerCount() ||
      editor_preview.photoLayers()
              .at(active_brush_adjustment_layer_index)
              .toMap()
              .value(QStringLiteral("selectionMaskSetId"))
              .toString() != active_brush_mask_set_id ||
      std::abs(editor_preview.photoLayers()
                    .at(active_brush_adjustment_layer_index)
                    .toMap()
                    .value(QStringLiteral("exposure"))
                    .toDouble() -
                0.31) > 1e-6 ||
      std::abs(editor_preview.exposure() - global_exposure_before_layer_adjust) > 1e-6) {
    return fail("right-panel adjustment controls should edit the active adjustment layer without mutating base exposure");
  }
	  if (!item_visible(qobject_cast<QQuickItem*>(active_edit_target_bar)) ||
		      !active_edit_target_bar->property("duplicateContextContract").toString().contains(
		          QStringLiteral("inspector-visible:true")) ||
		      !active_edit_target_bar->property("targetContract").toString().contains(
		          QStringLiteral("inspector-copy:hidden"))) {
	    return fail("adjust inspector should keep the active brush target queryable without rendering a duplicate right-panel target card");
	  }
  editor_preview.setTemperature(27.0);
  QCoreApplication::processEvents();
  QVariant layer_temperature_value;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "primaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, layer_temperature_value),
          Q_ARG(QVariant, QVariant(QStringLiteral("temperature"))),
          Q_ARG(QVariant, QVariant(editor_preview.temperature()))) ||
      std::abs(layer_temperature_value.toDouble()) > 1e-6) {
    return fail("neutral active adjustment layers should not display inherited base white balance values");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, layer_adjusted),
          Q_ARG(QVariant, QVariant(QStringLiteral("temperature"))),
          Q_ARG(QVariant, QVariant(-16.0))) ||
      !layer_adjusted.toBool() ||
      std::abs(editor_preview.photoLayers()
                    .at(active_brush_adjustment_layer_index)
                    .toMap()
                    .value(QStringLiteral("temperature"))
                    .toDouble() +
               16.0) > 1e-6 ||
      std::abs(editor_preview.temperature() - 27.0) > 1e-6) {
    return fail("right-panel white balance controls should edit the active layer without mutating base temperature");
  }
  QVariant layer_reset;
	  if (!QMetaObject::invokeMethod(
	          edit_workspace,
	          "resetPrimaryAdjustmentGroup",
          Q_RETURN_ARG(QVariant, layer_reset),
          Q_ARG(QVariant, QVariant(QStringLiteral("whiteBalance")))) ||
      !layer_reset.toBool() ||
      editor_preview.photoLayers()
          .at(active_brush_adjustment_layer_index)
          .toMap()
          .contains(QStringLiteral("temperature")) ||
      std::abs(editor_preview.temperature() - 27.0) > 1e-6) {
    return fail("right-panel panel reset should clear active-layer white balance without mutating base temperature");
  }
  editor_preview.setTemperature(0.0);
  editor_preview.setTexture(58.0);
  editor_preview.setSharpenRadius(2.4);
  editor_preview.setLensDistortion(-18.0);
  editor_preview.setTransformScale(1.7);
  editor_preview.setContentAwareCropFill(true);
  QCoreApplication::processEvents();
  QVariant neutral_layer_texture;
  QVariant neutral_layer_sharpen_radius;
  QVariant detail_layer_adjusted;
  QVariant detail_layer_bool_adjusted;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "primaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, neutral_layer_texture),
          Q_ARG(QVariant, QVariant(QStringLiteral("texture"))),
          Q_ARG(QVariant, QVariant(editor_preview.texture()))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "primaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, neutral_layer_sharpen_radius),
          Q_ARG(QVariant, QVariant(QStringLiteral("sharpenRadius"))),
          Q_ARG(QVariant, QVariant(editor_preview.sharpenRadius()))) ||
      std::abs(neutral_layer_texture.toDouble()) > 1e-6 ||
      std::abs(neutral_layer_sharpen_radius.toDouble() - 1.0) > 1e-6) {
    return fail("neutral active adjustment layers should not display inherited base detail values");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, detail_layer_adjusted),
          Q_ARG(QVariant, QVariant(QStringLiteral("texture"))),
          Q_ARG(QVariant, QVariant(24.0))) ||
      !detail_layer_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, detail_layer_adjusted),
          Q_ARG(QVariant, QVariant(QStringLiteral("sharpenRadius"))),
          Q_ARG(QVariant, QVariant(1.6))) ||
      !detail_layer_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, detail_layer_adjusted),
          Q_ARG(QVariant, QVariant(QStringLiteral("lensDistortion"))),
          Q_ARG(QVariant, QVariant(-33.0))) ||
      !detail_layer_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, detail_layer_adjusted),
          Q_ARG(QVariant, QVariant(QStringLiteral("transformScale"))),
          Q_ARG(QVariant, QVariant(1.35))) ||
      !detail_layer_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryBoolAdjustmentValue",
          Q_RETURN_ARG(QVariant, detail_layer_bool_adjusted),
          Q_ARG(QVariant, QVariant(QStringLiteral("contentAwareCropFill"))),
          Q_ARG(QVariant, QVariant(false))) ||
      !detail_layer_bool_adjusted.toBool()) {
    return fail("target-aware detail, lens, and transform controls should accept active layer writes");
  }
  auto layer_local_detail = editor_preview.photoLayers().at(active_brush_adjustment_layer_index).toMap();
  if (std::abs(layer_local_detail.value(QStringLiteral("texture")).toDouble() - 24.0) > 1e-6 ||
      std::abs(layer_local_detail.value(QStringLiteral("sharpenRadius")).toDouble() - 1.6) > 1e-6 ||
      std::abs(layer_local_detail.value(QStringLiteral("lensDistortion")).toDouble() + 33.0) > 1e-6 ||
      std::abs(layer_local_detail.value(QStringLiteral("transformScale")).toDouble() - 1.35) > 1e-6 ||
      layer_local_detail.value(QStringLiteral("contentAwareCropFill")).toBool() ||
      std::abs(editor_preview.texture() - 58.0) > 1e-6 ||
      std::abs(editor_preview.sharpenRadius() - 2.4) > 1e-6 ||
      std::abs(editor_preview.lensDistortion() + 18.0) > 1e-6 ||
      std::abs(editor_preview.transformScale() - 1.7) > 1e-6 ||
      !editor_preview.contentAwareCropFill()) {
    return fail("active layer detail/lens/transform writes should not mutate Base Image values");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "resetPrimaryAdjustmentGroup",
          Q_RETURN_ARG(QVariant, layer_reset),
          Q_ARG(QVariant, QVariant(QStringLiteral("presence")))) ||
      !layer_reset.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "resetPrimaryAdjustmentGroup",
          Q_RETURN_ARG(QVariant, layer_reset),
          Q_ARG(QVariant, QVariant(QStringLiteral("lens")))) ||
      !layer_reset.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "resetPrimaryAdjustmentGroup",
          Q_RETURN_ARG(QVariant, layer_reset),
          Q_ARG(QVariant, QVariant(QStringLiteral("transform")))) ||
      !layer_reset.toBool()) {
    return fail("target-aware panel resets should clear active-layer presence, lens, and transform groups");
  }
  layer_local_detail = editor_preview.photoLayers().at(active_brush_adjustment_layer_index).toMap();
  if (layer_local_detail.contains(QStringLiteral("texture")) ||
      layer_local_detail.contains(QStringLiteral("lensDistortion")) ||
      layer_local_detail.contains(QStringLiteral("transformScale")) ||
      layer_local_detail.contains(QStringLiteral("contentAwareCropFill")) ||
      std::abs(editor_preview.texture() - 58.0) > 1e-6 ||
      std::abs(editor_preview.lensDistortion() + 18.0) > 1e-6 ||
      std::abs(editor_preview.transformScale() - 1.7) > 1e-6 ||
      !editor_preview.contentAwareCropFill()) {
    return fail("target-aware panel resets should leave Base Image detail/lens/transform state untouched");
  }
  editor_preview.setTexture(0.0);
  editor_preview.setSharpenRadius(1.0);
  editor_preview.setLensDistortion(0.0);
  editor_preview.setTransformScale(1.0);
  editor_preview.setContentAwareCropFill(false);
  QCoreApplication::processEvents();
  const auto mask_count_before_brush_exit = editor_preview.maskCount();
  QVariant brush_finished;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "finishActiveCanvasTool",
          Q_RETURN_ARG(QVariant, brush_finished)) ||
      !brush_finished.toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("layers") ||
      !editor_preview.hasActiveMask() ||
      editor_preview.activeMaskKind() != QStringLiteral("brush") ||
      editor_preview.maskCount() != mask_count_before_brush_exit ||
      active_canvas_tool_chip->property("visible").toBool() ||
      brush_tool_hud->property("visible").toBool()) {
    std::cerr << "Brush finish state: finished=" << brush_finished.toBool()
              << " editTool="
              << edit_workspace->property("editTool").toString().toStdString()
              << " hasMask=" << editor_preview.hasActiveMask()
              << " kind=" << editor_preview.activeMaskKind().toStdString()
              << " count=" << editor_preview.maskCount()
              << " expectedCount=" << mask_count_before_brush_exit
              << " chipVisible=" << active_canvas_tool_chip->property("visible").toBool()
              << " hudVisible=" << brush_tool_hud->property("visible").toBool() << '\n';
    return fail("Done/Enter should exit brush mode while keeping the active brush selection selected");
  }
  edit_workspace->setProperty("editTool", QStringLiteral("brush"));
  QCoreApplication::processEvents();
  QVariant brush_cancelled;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "cancelActiveTool",
          Q_RETURN_ARG(QVariant, brush_cancelled)) ||
      !brush_cancelled.toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("layers") ||
      !editor_preview.hasActiveMask() ||
      editor_preview.activeMaskKind() != QStringLiteral("brush") ||
      editor_preview.maskCount() != mask_count_before_brush_exit ||
      active_canvas_tool_chip->property("visible").toBool() ||
      brush_tool_hud->property("visible").toBool()) {
    return fail("Escape should exit brush mode without deleting or deselecting the active brush selection");
  }
  const auto mask_count_before_shortcut_create = editor_preview.maskCount();
  const auto inverted_before_shortcut = editor_preview.activeMaskSelectionInverted();
  QVariant mask_shortcut_signature;
  QVariant mask_shortcut_created;
  QVariant mask_shortcut_add;
  QVariant mask_shortcut_subtract;
  QVariant mask_shortcut_intersect;
  QVariant mask_shortcut_inverted;
  QVariant mask_shortcut_overlay_hidden;
  QVariant mask_shortcut_overlay_shown;
  QVariant mask_shortcut_outline_enabled;
  QVariant mask_shortcut_outline_disabled;
  QVariant mask_shortcut_layer_target;
  QVariant mask_shortcut_layer_target_previous;
  QVariant mask_shortcut_mask_target;
  QVariant mask_shortcut_mask_target_previous;
  QVariant mask_shortcut_base_target;
  QVariant mask_shortcut_base_scope;
  QVariant mask_shortcut_mask_scope;
  QVariant mask_selected_target_contract;
  QVariant mask_shortcut_overlay_temporarily_hidden;
  QVariant mask_shortcut_overlay_temporarily_shown;
  QVariant mask_shortcut_rename_opened;
  QVariant mask_global_write_guard_signature;
  QVariant mask_global_reset_blocked;
  QVariant mask_shortcut_create_blocked_by_text_input;
  QVariant mask_shortcut_deleted;
  QVariant mask_shortcut_select_move;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "maskShortcutSignature",
          Q_RETURN_ARG(QVariant, mask_shortcut_signature)) ||
      mask_shortcut_signature.toString() !=
          QStringLiteral(
              "select:V|brush:B|erase:E|lasso:L|polygon:P|create:Ctrl+Shift+M|add:Ctrl+Shift+A|subtract:Ctrl+Shift+S|intersect:Ctrl+Shift+I|invert:Ctrl+Shift+V|overlay:Ctrl+Shift+O|outline:Ctrl+Shift+U|layer-target:Ctrl+Shift+L|layer-target-prev:Ctrl+Alt+Shift+L|mask-target:Ctrl+Shift+K|mask-target-prev:Ctrl+Alt+Shift+K|base-image:Ctrl+Shift+B|temp-erase:X-hold|temporary-hide:H-hold|delete:Delete|rename:F2") ||
      !mask_create_shortcut->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "selectMaskMoveFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_select_move)) ||
      !mask_shortcut_select_move.toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("masks") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_created)) ||
      !mask_shortcut_created.toBool() ||
      editor_preview.maskCount() != mask_count_before_shortcut_create + 1 ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskOperationFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_add),
          Q_ARG(QVariant, QStringLiteral("add"))) ||
      !mask_shortcut_add.toBool() || editor_preview.activeMaskOperation() != QStringLiteral("add") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskOperationFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_subtract),
          Q_ARG(QVariant, QStringLiteral("subtract"))) ||
      !mask_shortcut_subtract.toBool() ||
      editor_preview.activeMaskOperation() != QStringLiteral("subtract") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskOperationFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_intersect),
          Q_ARG(QVariant, QStringLiteral("intersect"))) ||
      !mask_shortcut_intersect.toBool() ||
      editor_preview.activeMaskOperation() != QStringLiteral("intersect") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "invertActiveMaskFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_inverted)) ||
      !mask_shortcut_inverted.toBool() ||
      editor_preview.activeMaskSelectionInverted() == inverted_before_shortcut ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "toggleActiveMaskOverlayFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_overlay_hidden)) ||
      !mask_shortcut_overlay_hidden.toBool() ||
      editor_preview.maskPreviewMode() != QStringLiteral("none") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "toggleActiveMaskOverlayFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_overlay_shown)) ||
      !mask_shortcut_overlay_shown.toBool() ||
      editor_preview.maskPreviewMode() != QStringLiteral("overlay") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "toggleMaskOutlineFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_outline_enabled)) ||
      !mask_shortcut_outline_enabled.toBool() ||
      editor_preview.maskPreviewMode() != QStringLiteral("marching-ants") ||
      editor_preview.maskPreviewDisplayMode() != QStringLiteral("marching-ants") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "toggleMaskOutlineFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_outline_disabled)) ||
      !mask_shortcut_outline_disabled.toBool() ||
      editor_preview.maskPreviewMode() != QStringLiteral("overlay") ||
      editor_preview.maskPreviewDisplayMode() != QStringLiteral("overlay") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "switchLayerTargetFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_layer_target),
          Q_ARG(QVariant, 1)) ||
      !mask_shortcut_layer_target.toBool() ||
      edit_workspace->property("activePhotoLayerIndex").toInt() < 0 ||
      edit_workspace->property("activePhotoLayerIndex").toInt() >= editor_preview.photoLayerCount() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "switchLayerTargetFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_layer_target_previous),
          Q_ARG(QVariant, -1)) ||
      !mask_shortcut_layer_target_previous.toBool() ||
      edit_workspace->property("activePhotoLayerIndex").toInt() < 0 ||
      edit_workspace->property("activePhotoLayerIndex").toInt() >= editor_preview.photoLayerCount() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "returnToBaseImageFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_base_target)) ||
      !mask_shortcut_base_target.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetScope",
          Q_RETURN_ARG(QVariant, mask_shortcut_base_scope)) ||
      mask_shortcut_base_scope.toString() != QStringLiteral("base-image") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "switchMaskTargetFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_mask_target),
          Q_ARG(QVariant, 1)) ||
      !mask_shortcut_mask_target.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetScope",
          Q_RETURN_ARG(QVariant, mask_shortcut_mask_scope)) ||
      mask_shortcut_mask_scope.toString() != QStringLiteral("mask") ||
      (mask_selected_target_contract =
           edit_workspace->property("selectedEditTargetContract")).toString().isEmpty() ||
      !mask_selected_target_contract.toString().contains(QStringLiteral("selected-target:v1|scope:mask")) ||
      !mask_selected_target_contract.toString().contains(QStringLiteral("|layer:")) ||
      !mask_selected_target_contract.toString().contains(QStringLiteral("|mask:")) ||
      !mask_selected_target_contract.toString().contains(QStringLiteral("|base:false")) ||
      !editor_preview.hasActiveMask() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "switchMaskTargetFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_mask_target_previous),
          Q_ARG(QVariant, -1)) ||
      !mask_shortcut_mask_target_previous.toBool() ||
      !editor_preview.hasActiveMask() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskPreviewTemporarilyHiddenFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_overlay_temporarily_hidden),
          Q_ARG(QVariant, true)) ||
      !mask_shortcut_overlay_temporarily_hidden.toBool() ||
      !editor_preview.maskPreviewTemporarilyHidden() ||
      editor_preview.maskPreviewMode() != QStringLiteral("overlay") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskPreviewTemporarilyHiddenFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_overlay_temporarily_shown),
          Q_ARG(QVariant, false)) ||
      !mask_shortcut_overlay_temporarily_shown.toBool() ||
      editor_preview.maskPreviewTemporarilyHidden() ||
      editor_preview.maskPreviewMode() != QStringLiteral("overlay") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "renameActiveMaskFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_rename_opened)) ||
      !mask_shortcut_rename_opened.toBool() ||
      !active_mask_rename_dialog->property("opened").toBool() ||
      edit_workspace->property("activeMaskRenameDraft").toString() != editor_preview.activeMaskName()) {
    std::cerr << "Mask shortcut state: signature="
              << mask_shortcut_signature.toString().toStdString()
              << " createEnabled=" << mask_create_shortcut->property("enabled").toBool()
              << " select=" << mask_shortcut_select_move.toBool()
              << " tool=" << edit_workspace->property("editTool").toString().toStdString()
              << " create=" << mask_shortcut_created.toBool()
              << " count=" << editor_preview.maskCount()
              << " expectedCount=" << mask_count_before_shortcut_create + 1
              << " add=" << mask_shortcut_add.toBool()
              << " subtract=" << mask_shortcut_subtract.toBool()
              << " intersect=" << mask_shortcut_intersect.toBool()
              << " invert=" << mask_shortcut_inverted.toBool()
              << " overlayOff=" << mask_shortcut_overlay_hidden.toBool()
              << " overlayOn=" << mask_shortcut_overlay_shown.toBool()
              << " outlineOn=" << mask_shortcut_outline_enabled.toBool()
              << " outlineOff=" << mask_shortcut_outline_disabled.toBool()
              << " layerNext=" << mask_shortcut_layer_target.toBool()
              << " layerPrev=" << mask_shortcut_layer_target_previous.toBool()
              << " base=" << mask_shortcut_base_target.toBool()
              << " baseScope=" << mask_shortcut_base_scope.toString().toStdString()
              << " maskNext=" << mask_shortcut_mask_target.toBool()
              << " maskScope=" << mask_shortcut_mask_scope.toString().toStdString()
              << " contract=" << mask_selected_target_contract.toString().toStdString()
              << " maskPrev=" << mask_shortcut_mask_target_previous.toBool()
              << " tempHide=" << mask_shortcut_overlay_temporarily_hidden.toBool()
              << " tempShow=" << mask_shortcut_overlay_temporarily_shown.toBool()
              << " preview=" << editor_preview.maskPreviewMode().toStdString()
              << " rename=" << mask_shortcut_rename_opened.toBool()
              << " dialog=" << active_mask_rename_dialog->property("opened").toBool()
              << '\n';
    return fail("mask keyboard shortcuts should create, combine, invert, preview, temporarily hide preview, and open rename for active selections");
  }
	  if (!QMetaObject::invokeMethod(
	          edit_workspace,
	          "globalAdjustmentWriteGuardSignature",
          Q_RETURN_ARG(QVariant, mask_global_write_guard_signature)) ||
      !mask_global_write_guard_signature.toString().contains(
          QStringLiteral("global-write-guard:v1")) ||
      !mask_global_write_guard_signature.toString().contains(QStringLiteral("scope:mask")) ||
      !mask_global_write_guard_signature.toString().contains(QStringLiteral("selected-target:v1|scope:mask")) ||
      !mask_global_write_guard_signature.toString().contains(QStringLiteral("allowed:false")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "resetPrimaryAdjustmentGroup",
          Q_RETURN_ARG(QVariant, mask_global_reset_blocked),
          Q_ARG(QVariant, QVariant(QStringLiteral("raw")))) ||
      mask_global_reset_blocked.toBool()) {
    std::cerr << "Mask global write guard: signature="
              << mask_global_write_guard_signature.toString().toStdString()
              << " rawResetResult=" << mask_global_reset_blocked.toBool() << '\n';
	    return fail("mask/layer-local targets should block global-only adjustment writes until Base Image is explicitly active");
	  }
  trace_smoke_progress("mask shortcut block checked");
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  if (mask_create_shortcut->property("enabled").toBool() ||
      mask_outline_shortcut->property("enabled").toBool() ||
      layer_target_next_shortcut->property("enabled").toBool() ||
      brush_size_up_shortcut->property("enabled").toBool() ||
      brush_done_return_shortcut->property("enabled").toBool() ||
      brush_add_shortcut->property("enabled").toBool() ||
      brush_erase_shortcut->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskFromShortcut",
          Q_RETURN_ARG(QVariant, mask_shortcut_create_blocked_by_text_input)) ||
      mask_shortcut_create_blocked_by_text_input.toBool()) {
    return fail("mask and brush shortcuts should be disabled while a mask rename text field is active");
  }
  QMetaObject::invokeMethod(active_mask_rename_dialog, "close");
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
	  if (active_mask_rename_dialog->property("opened").toBool() ||
	      !mask_create_shortcut->property("enabled").toBool() ||
      !mask_outline_shortcut->property("enabled").toBool() ||
      !brush_add_shortcut->property("enabled").toBool() ||
      !brush_erase_shortcut->property("enabled").toBool()) {
	    return fail("mask and brush shortcuts should re-enable after closing mask rename text input");
	  }
  trace_smoke_progress("mask rename shortcut checked");
  QVariant mask_modifier_default;
  QVariant mask_modifier_add;
  QVariant mask_modifier_subtract;
  QVariant mask_modifier_intersect;
  QVariant mask_modifier_applied;
  QVariant object_modifier_add_mode;
  QVariant object_modifier_subtract_mode;
  const auto shift_modifier = static_cast<int>(Qt::ShiftModifier);
  const auto option_modifier = static_cast<int>(Qt::AltModifier);
	  if (!QMetaObject::invokeMethod(
	          edit_workspace,
	          "setMaskCreationOperation",
          Q_ARG(QVariant, QVariant(QStringLiteral("replace")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskOperationForModifierState",
          Q_RETURN_ARG(QVariant, mask_modifier_default),
          Q_ARG(QVariant, QVariant(0))) ||
      mask_modifier_default.toString() != QStringLiteral("replace") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskOperationForModifierState",
          Q_RETURN_ARG(QVariant, mask_modifier_add),
          Q_ARG(QVariant, QVariant(shift_modifier))) ||
      mask_modifier_add.toString() != QStringLiteral("add") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskOperationForModifierState",
          Q_RETURN_ARG(QVariant, mask_modifier_subtract),
          Q_ARG(QVariant, QVariant(option_modifier))) ||
      mask_modifier_subtract.toString() != QStringLiteral("subtract") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskOperationForModifierState",
          Q_RETURN_ARG(QVariant, mask_modifier_intersect),
          Q_ARG(QVariant, QVariant(shift_modifier | option_modifier))) ||
      mask_modifier_intersect.toString() != QStringLiteral("intersect") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "applyMaskOperationForModifierState",
          Q_RETURN_ARG(QVariant, mask_modifier_applied),
          Q_ARG(QVariant, QVariant(shift_modifier | option_modifier))) ||
      mask_modifier_applied.toString() != QStringLiteral("intersect") ||
      editor_preview.activeMaskOperation() != QStringLiteral("intersect") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "objectPickModeForModifierState",
          Q_RETURN_ARG(QVariant, object_modifier_add_mode),
          Q_ARG(QVariant, QVariant(shift_modifier))) ||
      object_modifier_add_mode.toString() != QStringLiteral("selection") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "objectPickModeForModifierState",
          Q_RETURN_ARG(QVariant, object_modifier_subtract_mode),
          Q_ARG(QVariant, QVariant(option_modifier))) ||
      object_modifier_subtract_mode.toString() != QStringLiteral("selection-subtract")) {
	    return fail("mask draw modifiers should map Shift to Add, Option to Subtract, and Shift+Option to Intersect");
	  }
  trace_smoke_progress("mask modifier shortcuts checked");
  if (editor_preview.activeMaskIndex() < 0 ||
      editor_preview.activeMaskIndex() >= editor_preview.maskStack().size()) {
    std::cerr << "Shortcut-created mask state: activeMaskIndex="
              << editor_preview.activeMaskIndex()
              << " maskStackSize=" << editor_preview.maskStack().size()
              << " maskCount=" << editor_preview.maskCount() << '\n';
    return fail("creating a selection from the QML shortcut path should leave a valid active mask");
  }
  const auto shortcut_mask_set_id =
      editor_preview.maskStack()
          .at(editor_preview.activeMaskIndex())
          .toMap()
          .value(QStringLiteral("maskSetId"))
          .toString();
  auto shortcut_linked_layer_index = -1;
  for (auto layer_index = 0; layer_index < editor_preview.photoLayers().size(); ++layer_index) {
    const auto layer = editor_preview.photoLayers().at(layer_index).toMap();
    if (layer.value(QStringLiteral("selectionMaskSetId")).toString() == shortcut_mask_set_id) {
      shortcut_linked_layer_index = layer_index;
      break;
    }
  }
  if (shortcut_linked_layer_index < 0 ||
      edit_workspace->property("activePhotoLayerIndex").toInt() != shortcut_linked_layer_index) {
    return fail("creating a selection from the QML shortcut path should select its linked adjustment layer target");
  }
  QMetaObject::invokeMethod(active_mask_rename_dialog, "close");
  const bool mask_shortcut_delete_invoked = QMetaObject::invokeMethod(
      edit_workspace,
      "deleteActiveMaskFromShortcut",
      Q_RETURN_ARG(QVariant, mask_shortcut_deleted));
  process_layout_events(3);
	  if (!mask_shortcut_delete_invoked || !mask_shortcut_deleted.toBool() ||
	      editor_preview.maskCount() != mask_count_before_shortcut_create) {
    std::cerr << "Mask shortcut delete state: invoked=" << mask_shortcut_delete_invoked
              << " returned=" << mask_shortcut_deleted.toBool()
              << " before=" << mask_count_before_shortcut_create
              << " after=" << editor_preview.maskCount()
              << " activeIndex=" << editor_preview.activeMaskIndex()
              << " stackSize=" << editor_preview.maskStack().size()
              << " activeScope=" << edit_workspace->property("activePhotoLayerIndex").toInt()
              << '\n';
	    return fail("mask keyboard shortcut delete should remove the active selection created by the create shortcut");
	  }
  trace_smoke_progress("mask delete shortcut checked");
  QVariant base_return_layer_selected;
  QVariant base_return_scope;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, base_return_layer_selected),
          Q_ARG(QVariant, QVariant(shortcut_linked_layer_index)),
          Q_ARG(QVariant, QVariant(false))) ||
      !base_return_layer_selected.toBool()) {
    return fail("Return to Base Image smoke setup should select a concrete layer target first");
  }
  process_layout_events(4);
	  auto* active_edit_target_return_to_base_button =
	      find_named_object(edit_workspace, QStringLiteral("activeEditTargetReturnToBaseButton"));
	  QVariant base_return_invoked;
	  if (active_edit_target_return_to_base_button == nullptr ||
	      active_edit_target_return_to_base_button->property("visible").toBool() ||
	      !QMetaObject::invokeMethod(
	          edit_workspace,
	          "returnToBaseImageFromShortcut",
	          Q_RETURN_ARG(QVariant, base_return_invoked)) ||
	      !base_return_invoked.toBool()) {
	    return fail("Return to Base Image should stay available through the quiet command path without rendering duplicate inspector target chrome");
	  }
  process_layout_events(4);
  const auto selected_layers_after_base_return =
      edit_workspace->property("selectedPhotoLayerIndices").toList();
  auto base_return_kept_selected_layer = false;
  for (const auto& selected_layer : selected_layers_after_base_return) {
    if (selected_layer.toInt() == shortcut_linked_layer_index) {
      base_return_kept_selected_layer = true;
      break;
    }
  }
	  if (!QMetaObject::invokeMethod(
	          edit_workspace,
	          "activeEditTargetScope",
	          Q_RETURN_ARG(QVariant, base_return_scope)) ||
      base_return_scope.toString() != QStringLiteral("base-image") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("editInspectorTab").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("activePhotoLayerIndex").toInt() != shortcut_linked_layer_index ||
      !base_return_kept_selected_layer) {
    std::cerr << "Return to Base Image state: scope="
              << base_return_scope.toString().toStdString()
              << " tool=" << edit_workspace->property("editTool").toString().toStdString()
              << " tab=" << edit_workspace->property("editInspectorTab").toString().toStdString()
              << " activeLayer=" << edit_workspace->property("activePhotoLayerIndex").toInt()
              << " expectedLayer=" << shortcut_linked_layer_index
              << " selectedCount=" << selected_layers_after_base_return.size() << '\n';
	    return fail("Return to Base Image should exit layer editing while preserving the selected layer");
		  }
		  trace_smoke_progress("brush return to base checked");
		  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_RETURN_TO_BASE")) {
		    return 0;
		  }
		  if (editor_preview.maskCount() > 0 &&
		      (editor_preview.activeMaskIndex() < 0 ||
		       editor_preview.activeMaskIndex() >= editor_preview.maskStack().size())) {
    editor_preview.setActiveMaskIndex(0);
    process_layout_events(2);
  }
  QVariant base_return_mask_target_restored;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "switchMaskTargetFromShortcut",
          Q_RETURN_ARG(QVariant, base_return_mask_target_restored),
          Q_ARG(QVariant, 1)) ||
      !base_return_mask_target_restored.toBool() ||
	      !editor_preview.hasActiveMask()) {
	    return fail("Return to Base Image proof should restore a valid mask target before continuing brush checks");
	  }
	  trace_smoke_progress("brush mask target restored");
	  process_layout_events(2);
	  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  editor_preview.setActiveMaskOperation(QStringLiteral("subtract"));
  editor_preview.setActiveMaskSelectionInverted(false);
  edit_workspace->setProperty("editTool", QStringLiteral("brush"));
  QCoreApplication::processEvents();
  QVariant hidden_preview_mode;
  QVariant exposure_adjusted;
  QVariant temperature_adjusted;
  const auto brush_quick_adjustment_set_id =
      editor_preview.maskStack()
          .at(editor_preview.activeMaskIndex())
          .toMap()
          .value(QStringLiteral("maskSetId"))
          .toString();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActiveMaskPreviewVisible",
          Q_RETURN_ARG(QVariant, hidden_preview_mode),
          Q_ARG(QVariant, false)) ||
      hidden_preview_mode.toString() != QStringLiteral("hidden") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setActiveMaskQuickAdjustment",
          Q_RETURN_ARG(QVariant, exposure_adjusted),
          Q_ARG(QVariant, QStringLiteral("exposure")),
          Q_ARG(QVariant, 1.25)) ||
      !exposure_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setActiveMaskQuickAdjustment",
          Q_RETURN_ARG(QVariant, temperature_adjusted),
          Q_ARG(QVariant, QStringLiteral("temperature")),
          Q_ARG(QVariant, -34.0)) ||
      !temperature_adjusted.toBool() ||
      editor_preview.maskPreviewMode() != QStringLiteral("none") ||
      editor_preview.maskPreviewDisplayMode() != QStringLiteral("hidden") ||
      editor_preview.photoLayerCount() <= 0 ||
      std::abs(editor_preview.activeMaskExposure()) > 1e-6 ||
      std::abs(editor_preview.activeMaskTemperature()) > 1e-6) {
    return fail("brush context panel should toggle mask preview and edit the linked adjustment layer");
  }
  QVariantMap brush_quick_adjustment_layer;
  for (const auto& layer_value : editor_preview.photoLayers()) {
    const auto layer = layer_value.toMap();
    if (layer.value(QStringLiteral("selectionMaskSetId")).toString() ==
        brush_quick_adjustment_set_id) {
      brush_quick_adjustment_layer = layer;
      break;
    }
  }
  if (brush_quick_adjustment_layer.isEmpty() ||
      std::abs(brush_quick_adjustment_layer.value(QStringLiteral("exposure")).toDouble() -
               1.25) > 1e-6 ||
	      std::abs(brush_quick_adjustment_layer.value(QStringLiteral("temperature")).toDouble() +
	               34.0) > 1e-6) {
	    return fail("brush context panel should write quick edits to the active mask's linked adjustment layer");
	  }
	  trace_smoke_progress("brush quick adjustments checked");
	  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  QCoreApplication::processEvents();
  const auto brush_size_before = editor_preview.activeMaskBrushSize();
  const auto brush_flow_before = editor_preview.activeMaskBrushFlow();
  const auto brush_hardness_before = editor_preview.activeMaskBrushHardness();
  const auto brush_density_before = editor_preview.activeMaskDensity();
  const auto brush_opacity_before = editor_preview.activeMaskOpacity();
  QVariant brush_size_adjusted;
  QVariant brush_size_percent_set;
  QVariant brush_size_text_set;
  QVariant brush_size_drag_started;
  QVariant brush_size_drag_updated;
  QVariant brush_size_drag_ended;
  QVariant brush_flow_adjusted;
  QVariant brush_hardness_adjusted;
  QVariant brush_density_adjusted;
  QVariant brush_opacity_adjusted;
  QVariant brush_defaults_reset;
  // These calls verify the five control routes, not five independently settled
  // renders. Batch them like a continuous HUD interaction so the monolithic
  // shell smoke does not spend minutes rebuilding the same mask repeatedly.
  editor_preview.beginCoalescedEdit();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushSize",
          Q_RETURN_ARG(QVariant, brush_size_adjusted),
          Q_ARG(QVariant, 0.02)) ||
      !brush_size_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushFlow",
          Q_RETURN_ARG(QVariant, brush_flow_adjusted),
          Q_ARG(QVariant, -0.05)) ||
      !brush_flow_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushHardness",
          Q_RETURN_ARG(QVariant, brush_hardness_adjusted),
          Q_ARG(QVariant, 0.08)) ||
      !brush_hardness_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveMaskDensity",
          Q_RETURN_ARG(QVariant, brush_density_adjusted),
          Q_ARG(QVariant, -0.05)) ||
      !brush_density_adjusted.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveMaskOpacity",
          Q_RETURN_ARG(QVariant, brush_opacity_adjusted),
          Q_ARG(QVariant, -0.05)) ||
      !brush_opacity_adjusted.toBool() ||
      editor_preview.activeMaskBrushSize() <= brush_size_before ||
      editor_preview.activeMaskBrushFlow() >= brush_flow_before ||
      editor_preview.activeMaskBrushHardness() <= brush_hardness_before ||
      editor_preview.activeMaskDensity() >= brush_density_before ||
	      editor_preview.activeMaskOpacity() >= brush_opacity_before) {
	    editor_preview.endCoalescedEdit();
	    return fail("brush HUD adjustment functions should update size, flow, hardness, density, and opacity");
	  }
	  editor_preview.endCoalescedEdit();
	  trace_smoke_progress("brush tuning adjustments checked");
	  const auto brush_operation_before_reset = editor_preview.activeMaskOperation();
	  editor_preview.beginCoalescedEdit();
	  editor_preview.setActiveMaskBrushSpacing(0.42);
	  editor_preview.setActiveMaskBrushSmoothing(0.36);
	  editor_preview.setActiveMaskBrushPressureSize(0.73);
	  editor_preview.setActiveMaskBrushPressureFlow(0.64);
	  editor_preview.setActiveMaskBrushPressureOpacity(0.55);
	  editor_preview.endCoalescedEdit();
	  trace_smoke_progress("brush reset invoke starting");
	  if (!QMetaObject::invokeMethod(
	          edit_workspace,
	          "resetActiveBrushDefaults",
	          Q_RETURN_ARG(QVariant, brush_defaults_reset)) ||
	      !brush_defaults_reset.toBool()) {
	    return fail("brush HUD reset should return success");
	  }
	  trace_smoke_progress("brush reset invoke returned");
	  if (
	      std::abs(editor_preview.activeMaskBrushSize() - 0.06) > 1e-6 ||
	      std::abs(editor_preview.activeMaskBrushFlow() - 1.0) > 1e-6 ||
	      std::abs(editor_preview.activeMaskBrushHardness() - 0.45) > 1e-6 ||
      std::abs(editor_preview.activeMaskDensity() - 1.0) > 1e-6 ||
      std::abs(editor_preview.activeMaskOpacity() - 1.0) > 1e-6 ||
      std::abs(editor_preview.activeMaskBrushSpacing() - 0.75) > 1e-6 ||
      std::abs(editor_preview.activeMaskBrushSmoothing() - 0.18) > 1e-6 ||
      std::abs(editor_preview.activeMaskBrushPressureSize()) > 1e-6 ||
      std::abs(editor_preview.activeMaskBrushPressureFlow()) > 1e-6 ||
	      std::abs(editor_preview.activeMaskBrushPressureOpacity()) > 1e-6 ||
	      editor_preview.activeMaskOperation() != brush_operation_before_reset) {
	    return fail("brush HUD reset should restore default customization values without changing Add/Erase operation");
	  }
	  trace_smoke_progress("brush reset values checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_RESET")) {
    return EXIT_SUCCESS;
  }
  trace_smoke_progress("brush reset checked");
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActiveBrushSizePercent",
          Q_RETURN_ARG(QVariant, brush_size_percent_set),
          Q_ARG(QVariant, 18.0)) ||
      !brush_size_percent_set.toBool()) {
    return fail("brush HUD should expose exact percent entry for brush size");
  }
  QCoreApplication::processEvents();
  if (std::abs(editor_preview.activeMaskBrushSize() - 0.18) > 1e-6 ||
      brush_size_field->property("text").toString() != QStringLiteral("18")) {
    return fail("brush HUD numeric size field should reflect exact stored radius percentage");
  }
	  if (!QMetaObject::invokeMethod(
	          edit_workspace,
	          "setActiveBrushSizeText",
          Q_RETURN_ARG(QVariant, brush_size_text_set),
          Q_ARG(QVariant, QStringLiteral("7%"))) ||
      !brush_size_text_set.toBool() ||
	      std::abs(editor_preview.activeMaskBrushSize() - 0.07) > 1e-6) {
	    return fail("brush HUD numeric size field should parse percentage text");
	  }
	  trace_smoke_progress("brush exact size entry checked");
	  if (!QMetaObject::invokeMethod(
	          edit_workspace,
	          "beginBrushHudSizeDrag",
          Q_RETURN_ARG(QVariant, brush_size_drag_started),
          Q_ARG(QVariant, 100.0)) ||
      !brush_size_drag_started.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "updateBrushHudSizeDrag",
          Q_RETURN_ARG(QVariant, brush_size_drag_updated),
          Q_ARG(QVariant, 140.0)) ||
      !brush_size_drag_updated.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "endBrushHudSizeDrag",
          Q_RETURN_ARG(QVariant, brush_size_drag_ended)) ||
      !brush_size_drag_ended.toBool() ||
	      editor_preview.activeMaskBrushSize() <= 0.07) {
	    return fail("brush HUD drag handle should resize the active brush");
	  }
	  trace_smoke_progress("brush size drag checked");
	  editor_preview.setActiveMaskBrushSpacing(0.42);
  editor_preview.setActiveMaskBrushSmoothing(0.36);
  editor_preview.setActiveMaskBrushPressureSize(0.73);
  editor_preview.setActiveMaskBrushPressureFlow(0.64);
  editor_preview.setActiveMaskBrushPressureOpacity(0.55);
  QCoreApplication::processEvents();
  const auto brush_visible_size_text =
      QStringLiteral("Size %1%").arg(
          static_cast<int>(std::lround(editor_preview.activeMaskBrushSize() * 100.0)));
  const auto brush_diagnostic_size_text =
      QStringLiteral("Diagnostic Size %1").arg(editor_preview.activeMaskBrushSizeSummary());
  const auto brush_flow_text = QStringLiteral("Flow %1%").arg(
      static_cast<int>(std::lround(editor_preview.activeMaskBrushFlow() * 100.0)));
  const auto brush_hardness_text = QStringLiteral("Hard %1%").arg(
      static_cast<int>(std::lround(editor_preview.activeMaskBrushHardness() * 100.0)));
  const auto brush_density_text = QStringLiteral("Density %1%").arg(
      static_cast<int>(std::lround(editor_preview.activeMaskDensity() * 100.0)));
  const auto brush_opacity_text = QStringLiteral("Opacity %1%").arg(
      static_cast<int>(std::lround(editor_preview.activeMaskOpacity() * 100.0)));
  const auto brush_spacing_text = QStringLiteral("Spacing %1% of diameter").arg(
      static_cast<int>(std::lround(editor_preview.activeMaskBrushSpacing() * 100.0)));
  const auto brush_smoothing_text = QStringLiteral("Smooth %1%").arg(
      static_cast<int>(std::lround(editor_preview.activeMaskBrushSmoothing() * 100.0)));
  const auto brush_pressure_text =
      QStringLiteral("Pressure S/F/O %1/%2/%3")
          .arg(static_cast<int>(std::lround(editor_preview.activeMaskBrushPressureSize() * 100.0)))
          .arg(static_cast<int>(std::lround(editor_preview.activeMaskBrushPressureFlow() * 100.0)))
          .arg(static_cast<int>(
              std::lround(editor_preview.activeMaskBrushPressureOpacity() * 100.0)));
  const auto expected_brush_value_signature = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
                                                   .arg(brush_visible_size_text)
                                                   .arg(brush_flow_text)
                                                   .arg(brush_hardness_text)
                                                   .arg(brush_density_text)
                                                   .arg(brush_opacity_text)
                                                   .arg(brush_spacing_text)
                                                   .arg(brush_smoothing_text)
                                                   .arg(brush_pressure_text) +
      QStringLiteral("|%1").arg(brush_diagnostic_size_text);
  QVariant brush_value_signature;
  const auto brush_status_text = brush_status_label->property("text").toString();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "brushHudValueSignature",
          Q_RETURN_ARG(QVariant, brush_value_signature)) ||
      brush_value_signature.toString() != expected_brush_value_signature ||
      !brush_value_signature.toString().contains(brush_diagnostic_size_text) ||
      !brush_status_text.contains(brush_visible_size_text) ||
      !brush_status_text.contains(brush_flow_text) ||
      !brush_status_text.contains(brush_hardness_text) ||
      !brush_context_density_label->property("text").toString().contains(brush_density_text) ||
      !brush_context_opacity_label->property("text").toString().contains(brush_opacity_text) ||
      brush_status_text.contains(brush_density_text) ||
      brush_status_text.contains(brush_opacity_text) ||
      brush_status_text.contains(brush_diagnostic_size_text) ||
      brush_status_text.contains(brush_spacing_text) ||
      brush_status_text.contains(brush_smoothing_text) ||
      brush_status_text.contains(brush_pressure_text) ||
      brush_cursor_ring->property("brushDiameter").toDouble() <= 0.0 ||
      brush_cursor_ring->property("hardnessDiameter").toDouble() <= 0.0 ||
      brush_cursor_ring->property("hardnessDiameter").toDouble() >
          brush_cursor_ring->property("brushDiameter").toDouble() ||
      !brush_cursor_mode_label->property("text").toString().contains(brush_visible_size_text.mid(5)) ||
      !brush_cursor_mode_label->property("text").toString().contains(brush_hardness_text) ||
	      brush_cursor_ring->property("brushModeLabel").toString() != QStringLiteral("Erase")) {
	    return fail("brush HUD and context panel should expose true size, flow, hardness, density, opacity, spacing, smoothing, pressure response, and active mode");
	  }
	  trace_smoke_progress("brush HUD value signature checked");
	  editor_preview.beginActiveBrushStroke();
  editor_preview.addBrushPointWithTablet(0.42, 0.52, 0.64, 0.35, -0.20);
  QCoreApplication::processEvents();
  const auto live_tablet_status_text = brush_status_label->property("text").toString();
  if (!live_tablet_status_text.contains(QStringLiteral("Pressure 64%")) ||
      live_tablet_status_text.contains(QStringLiteral("Tilt")) ||
      brush_cursor_ring->property("pressureText").toString() != QStringLiteral("Pressure 64%") ||
      !brush_canvas_overlay->property("activeBrushPainting").toBool() ||
	      !brush_canvas_overlay->property("fullMaskMatteVisible").toBool()) {
	    return fail("brush HUD, cursor, and canvas overlay should expose live tablet pressure plus a visible live matte without unsupported tilt status");
	  }
	  editor_preview.endActiveBrushStroke();
	  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
	  trace_smoke_progress("brush live pressure checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_LIVE_PRESSURE")) {
    return EXIT_SUCCESS;
  }
	  const auto brush_size_before_shortcut = editor_preview.activeMaskBrushSize();
  const auto brush_hardness_before_shortcut = editor_preview.activeMaskBrushHardness();
  const auto brush_flow_before_shortcut = editor_preview.activeMaskBrushFlow();
  const auto brush_density_before_shortcut = editor_preview.activeMaskDensity();
  const auto brush_opacity_before_shortcut = editor_preview.activeMaskOpacity();
  QVariant brush_size_shortcut_adjusted;
  QVariant brush_hardness_shortcut_adjusted;
  QVariant brush_flow_shortcut_adjusted_down;
  QVariant brush_flow_shortcut_adjusted_up;
  QVariant brush_density_shortcut_adjusted_down;
  QVariant brush_density_shortcut_adjusted_up;
  QVariant brush_opacity_shortcut_adjusted_down;
  QVariant brush_opacity_shortcut_adjusted_up;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushSize",
          Q_RETURN_ARG(QVariant, brush_size_shortcut_adjusted),
          Q_ARG(QVariant, 0.02)) ||
      !brush_size_shortcut_adjusted.toBool() ||
      editor_preview.activeMaskBrushSize() <= brush_size_before_shortcut ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushHardness",
          Q_RETURN_ARG(QVariant, brush_hardness_shortcut_adjusted),
          Q_ARG(QVariant, 0.08)) ||
      !brush_hardness_shortcut_adjusted.toBool() ||
      editor_preview.activeMaskBrushHardness() <= brush_hardness_before_shortcut ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushFlow",
          Q_RETURN_ARG(QVariant, brush_flow_shortcut_adjusted_down),
          Q_ARG(QVariant, -0.08)) ||
      !brush_flow_shortcut_adjusted_down.toBool() ||
      editor_preview.activeMaskBrushFlow() >= brush_flow_before_shortcut ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveBrushFlow",
          Q_RETURN_ARG(QVariant, brush_flow_shortcut_adjusted_up),
          Q_ARG(QVariant, 0.08)) ||
      !brush_flow_shortcut_adjusted_up.toBool() ||
      std::abs(editor_preview.activeMaskBrushFlow() - brush_flow_before_shortcut) > 1e-6 ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveMaskDensity",
          Q_RETURN_ARG(QVariant, brush_density_shortcut_adjusted_down),
          Q_ARG(QVariant, -0.08)) ||
      !brush_density_shortcut_adjusted_down.toBool() ||
      editor_preview.activeMaskDensity() >= brush_density_before_shortcut ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveMaskDensity",
          Q_RETURN_ARG(QVariant, brush_density_shortcut_adjusted_up),
          Q_ARG(QVariant, 0.08)) ||
      !brush_density_shortcut_adjusted_up.toBool() ||
      std::abs(editor_preview.activeMaskDensity() - brush_density_before_shortcut) > 1e-6 ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveMaskOpacity",
          Q_RETURN_ARG(QVariant, brush_opacity_shortcut_adjusted_down),
          Q_ARG(QVariant, -0.08)) ||
      !brush_opacity_shortcut_adjusted_down.toBool() ||
      editor_preview.activeMaskOpacity() >= brush_opacity_before_shortcut ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "adjustActiveMaskOpacity",
          Q_RETURN_ARG(QVariant, brush_opacity_shortcut_adjusted_up),
          Q_ARG(QVariant, 0.08)) ||
      !brush_opacity_shortcut_adjusted_up.toBool() ||
	      std::abs(editor_preview.activeMaskOpacity() - brush_opacity_before_shortcut) > 1e-6) {
	    return fail("brush parameter shortcuts should mutate size, hardness, flow, density, and opacity while brush mode is active");
	  }
	  trace_smoke_progress("brush shortcut adjustments checked");
	  QVariant brush_add_mode;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setBrushPaintMode",
          Q_RETURN_ARG(QVariant, brush_add_mode),
          Q_ARG(QVariant, QStringLiteral("add"))) ||
      brush_add_mode.toString() != QStringLiteral("add") ||
      editor_preview.activeMaskOperation() != QStringLiteral("add")) {
    return fail("brush HUD should switch the active brush between erase and add modes");
  }
  QCoreApplication::processEvents();
  if (!brush_add_button->property("active").toBool()) {
    return fail("brush HUD add button should reflect the active brush mode");
  }
  QVariant temporary_erase_started;
  QVariant temporary_erase_finished;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setTemporaryBrushEraseFromShortcut",
          Q_RETURN_ARG(QVariant, temporary_erase_started),
          Q_ARG(QVariant, true)) ||
      !temporary_erase_started.toBool() ||
      !edit_workspace->property("temporaryBrushEraseActive").toBool() ||
      editor_preview.activeMaskOperation() != QStringLiteral("subtract") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setTemporaryBrushEraseFromShortcut",
          Q_RETURN_ARG(QVariant, temporary_erase_finished),
          Q_ARG(QVariant, false)) ||
      !temporary_erase_finished.toBool() ||
      edit_workspace->property("temporaryBrushEraseActive").toBool() ||
	      editor_preview.activeMaskOperation() != QStringLiteral("add")) {
	    return fail("holding the temporary erase shortcut should switch the brush to erase and restore the previous paint mode on release");
	  }
	  trace_smoke_progress("brush mode toggle checked");
	  const auto mask_count_before_tool_switch = editor_preview.maskCount();
  editor_preview.beginActiveBrushStroke();
  editor_preview.addBrushPointWithTablet(0.38, 0.44, 0.72, 0.0, 0.0);
  QVariant transient_tool_state_signature;
  if (!editor_preview.brushStrokeActive() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activateEditTool",
          Q_ARG(QVariant, QVariant(QStringLiteral("adjust")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "transientToolStateSignature",
          Q_RETURN_ARG(QVariant, transient_tool_state_signature)) ||
      transient_tool_state_signature.toString() !=
          QStringLiteral("tool:adjust|activeMask:false|stroke:false|crop:false|compare:false") ||
	      editor_preview.maskCount() != mask_count_before_tool_switch) {
	    return fail("switching tools should commit active brush strokes and clear transient mask, crop, and compare state safely");
	  }
	  trace_smoke_progress("brush transient switch checked");
  QVariant cancel_result;
  if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_SKIP_RETOUCH_PROOFS")) {
	  QVariant retouch_mode;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
	          "startRetouchTool",
	          Q_RETURN_ARG(QVariant, retouch_mode),
	          Q_ARG(QVariant, QStringLiteral("heal")),
	          Q_ARG(QVariant, false)) ||
      retouch_mode.toString() != QStringLiteral("heal")) {
    return fail("edit workspace should expose a direct retouch starter");
  }
  process_layout_events(4);
  auto* retouch_tool_hud = edit_workspace->findChild<QObject*>(QStringLiteral("retouchToolHud"));
  auto* retouch_remove_add_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudRemoveAddButton"));
  auto* retouch_remove_subtract_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudRemoveSubtractButton"));
  auto* retouch_status_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudStatusLabel"));
  auto* retouch_apply_button = edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudApplyButton"));
  auto* retouch_more_menu_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudMoreMenuButton"));
  auto* edit_zoom_toolbar_for_retouch = find_named_object(edit_workspace, QStringLiteral("editZoomToolbar"));
  auto* edit_action_toolbar_for_retouch = find_named_object(edit_workspace, QStringLiteral("editActionToolbar"));
  auto* active_tool_variant_chip_for_retouch =
      find_named_object(edit_workspace, QStringLiteral("activeToolVariantChip"));
  auto* retouch_mode_menu_button =
      find_named_object(edit_workspace, QStringLiteral("retouchHudModeMenuButton"));
  const auto retouch_inpaint_capability =
      editor_preview.aiInpaintAvailable() ? QStringLiteral("model-ready") : QStringLiteral("local-fallback");
  QVariant retouch_visibility_signature;
  QVariant retouch_action_signature;
  if (retouch_tool_hud == nullptr || retouch_status_label == nullptr ||
      retouch_apply_button == nullptr || retouch_more_menu_button == nullptr ||
      retouch_mode_menu_button == nullptr ||
      edit_zoom_toolbar_for_retouch == nullptr || edit_action_toolbar_for_retouch == nullptr ||
      active_tool_variant_chip_for_retouch == nullptr ||
      !retouch_tool_hud->property("visible").toBool() ||
      !retouch_tool_hud->property("hudContract").toString().contains(QStringLiteral("specialized-tool-hud:v3|tool:retouch")) ||
      !retouch_tool_hud->property("hudContract").toString().contains(QStringLiteral("component:ToolCommandBar")) ||
      !retouch_tool_hud->property("hudContract").toString().contains(QStringLiteral("single-row:true")) ||
      !retouch_tool_hud->property("hudContract").toString().contains(QStringLiteral("mode-change-animation:true")) ||
      !retouch_tool_hud->property("hudContract").toString().contains(QStringLiteral("secondary-actions:more-menu")) ||
      !retouch_tool_hud->property("hudContract").toString().contains(QStringLiteral("no-elide:true")) ||
      !retouch_tool_hud->property("modeChangeAnimationEnabled").toBool() ||
      !retouch_tool_hud->property("modeChangeMotionContract").toString().contains(
          QStringLiteral("mode-change-motion:v1|enabled:true")) ||
      retouch_tool_hud->property("visibilityContract").toString() !=
          QStringLiteral(
              "retouch-tool-visibility:v2|family:Retouch|variant:Heal|mode:heal|status:Heal / Paint area / After|last:heal|hud:true|layout:single-row-command-bar|primary:Apply|inline:0|more:9|text:no-elide-no-overlap|menus:secondary-actions") ||
      active_tool_variant_chip_for_retouch->property("text").toString() != QStringLiteral("Heal") ||
      retouch_mode_menu_button->property("text").toString() != QStringLiteral("Heal") ||
      !retouch_mode_menu_button->property("visible").toBool() ||
      !retouch_mode_menu_button->property("menuContract").toString().contains(QStringLiteral("items:8")) ||
      !retouch_mode_menu_button->property("menuContract").toString().contains(QStringLiteral("checked:1")) ||
      !retouch_mode_menu_button->property("menuContract").toString().contains(QStringLiteral("disabled:2")) ||
      !retouch_mode_menu_button->property("menuContract").toString().contains(QStringLiteral("disabled-reasons-complete:true")) ||
      !retouch_mode_menu_button->property("menuContract").toString().contains(QStringLiteral("danger:0")) ||
      !edit_workspace->property("activeCanvasToolChromeReduced").toBool() ||
      edit_zoom_toolbar_for_retouch->property("visible").toBool() ||
      edit_action_toolbar_for_retouch->property("visible").toBool() ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Heal")) ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Paint area")) ||
      retouch_status_label->property("text").toString().contains(QStringLiteral("fallback")) ||
      (!editor_preview.inpaintRuntimeLabel().isEmpty() &&
       retouch_status_label->property("text").toString().contains(editor_preview.inpaintRuntimeLabel())) ||
      !retouch_status_label->property("readableStatusContract").toString().contains(QStringLiteral("fit-horizontal:true")) ||
      retouch_status_label->property("text").toString().contains(QStringLiteral("...")) ||
      !retouch_apply_button->property("enabled").toBool() ||
      retouch_apply_button->property("text").toString() != QStringLiteral("Apply") ||
      !retouch_apply_button->property("tooltip").toString().contains(QStringLiteral("edit recipe")) ||
      !retouch_apply_button->property("tooltip").toString().contains(QStringLiteral("original file is unchanged")) ||
      retouch_more_menu_button->property("text").toString() != QStringLiteral("More") ||
      !retouch_more_menu_button->property("menuContract").toString().contains(QStringLiteral("items:9")) ||
      !retouch_more_menu_button->property("menuContract").toString().contains(QStringLiteral("disabled:1")) ||
      !retouch_more_menu_button->property("menuContract").toString().contains(QStringLiteral("disabled-reasons-complete:true")) ||
      !retouch_more_menu_button->property("menuContract").toString().contains(QStringLiteral("danger:0")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchHealBlendSignature",
          Q_RETURN_ARG(QVariant, active_mask_context_signature)) ||
      active_mask_context_signature.toString() !=
          QStringLiteral("visible:true|texture:68|color:46") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchWorkflowSignature",
          Q_RETURN_ARG(QVariant, retouch_mode)) ||
      retouch_mode.toString() !=
          QStringLiteral("mode:heal|label:Heal|stage:Paint area|source:none|apply:true|regenerate:true|capability:%1|beforeAfter:after")
              .arg(retouch_inpaint_capability) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchToolVisibilitySignature",
          Q_RETURN_ARG(QVariant, retouch_visibility_signature)) ||
      retouch_visibility_signature.toString() !=
          QStringLiteral(
              "retouch-tool-visibility:v2|family:Retouch|variant:Heal|mode:heal|status:Heal / Paint area / After|last:heal|hud:true|layout:single-row-command-bar|primary:Apply|inline:0|more:9|text:no-elide-no-overlap|menus:secondary-actions") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchActionSignature",
          Q_RETURN_ARG(QVariant, retouch_action_signature)) ||
      retouch_action_signature.toString() !=
          QStringLiteral("apply:true|cancel:true|reset:true|delete:false|reopen:false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activeMaskEditingContextSignature",
          Q_RETURN_ARG(QVariant, active_mask_context_signature)) ||
      active_mask_context_signature.toString() !=
          QStringLiteral("tool:retouch|mask:false|brush:false|retouch:true|count:1") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("retouch") ||
      editor_preview.activeMaskToolRole() != QStringLiteral("retouch") ||
	      editor_preview.activeMaskRetouchMode() != QStringLiteral("heal")) {
	    return fail("retouch workflow should own the canvas HUD without generic edit toolbar overlap");
	  }
	  trace_smoke_progress("retouch heal checked");
	  if (!QMetaObject::invokeMethod(
          edit_workspace,
	          "startRetouchTool",
	          Q_RETURN_ARG(QVariant, retouch_mode),
	          Q_ARG(QVariant, QStringLiteral("patch")),
	          Q_ARG(QVariant, false)) ||
      retouch_mode.toString() != QStringLiteral("patch")) {
    return fail("retouch HUD should start patch mode directly");
  }
  QCoreApplication::processEvents();
  auto* patch_source_region =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportPatchSourceRegion"));
  auto* patch_source_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportPatchSourceLabel"));
  QVariant patch_source_signature;
  if (retouch_mode_menu_button->property("text").toString() != QStringLiteral("Patch") ||
      patch_source_region == nullptr || patch_source_label == nullptr ||
      !patch_source_region->property("visible").toBool() ||
      !patch_source_label->property("visible").toBool() ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Patch")) ||
      retouch_tool_hud->property("visibilityContract").toString() !=
          QStringLiteral(
              "retouch-tool-visibility:v2|family:Retouch|variant:Patch|mode:patch|status:Patch / Paint area / After|last:patch|hud:true|layout:single-row-command-bar|primary:Apply|inline:0|more:10|text:no-elide-no-overlap|menus:secondary-actions") ||
      !retouch_more_menu_button->property("menuContract").toString().contains(QStringLiteral("items:10")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchPatchSourceSignature",
          Q_RETURN_ARG(QVariant, patch_source_signature)) ||
      patch_source_signature.toString() !=
          QStringLiteral("visible:true|set:true|x:62|y:50|w:18|h:18") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchWorkflowSignature",
          Q_RETURN_ARG(QVariant, retouch_mode)) ||
      retouch_mode.toString() !=
          QStringLiteral("mode:patch|label:Patch|stage:Paint area|source:region|apply:true|regenerate:true|capability:%1|beforeAfter:after")
              .arg(retouch_inpaint_capability)) {
    return fail("patch retouch should expose its own active mode and workflow state");
  }
  if (!QMetaObject::invokeMethod(
          patch_source_region,
          "moveRegion",
          Q_ARG(QVariant, 0.18),
          Q_ARG(QVariant, 0.22)) ||
      std::abs(editor_preview.activeMaskPatchSourceX() - 0.18) > 1e-6 ||
      std::abs(editor_preview.activeMaskPatchSourceY() - 0.22) > 1e-6 ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchPatchSourceSignature",
          Q_RETURN_ARG(QVariant, patch_source_signature)) ||
	      patch_source_signature.toString() !=
	          QStringLiteral("visible:true|set:true|x:18|y:22|w:18|h:18")) {
	    return fail("patch source overlay should move the source region before applying");
	  }
	  trace_smoke_progress("retouch patch checked");
	  if (!QMetaObject::invokeMethod(
          edit_workspace,
	          "startRetouchTool",
	          Q_RETURN_ARG(QVariant, retouch_mode),
	          Q_ARG(QVariant, QStringLiteral("remove")),
	          Q_ARG(QVariant, false)) ||
      retouch_mode.toString() != QStringLiteral("remove")) {
    return fail("retouch HUD should switch between retouch modes");
  }
  QCoreApplication::processEvents();
  const auto remove_inline_actions_ready = wait_for_condition([&]() {
    retouch_remove_add_button =
        edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudRemoveAddButton"));
    retouch_remove_subtract_button =
        edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudRemoveSubtractButton"));
    return retouch_remove_add_button != nullptr && retouch_remove_subtract_button != nullptr;
  });
  if (!remove_inline_actions_ready ||
      retouch_mode_menu_button->property("text").toString() != QStringLiteral("Remove") ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Remove")) ||
      retouch_tool_hud->property("visibilityContract").toString() !=
          QStringLiteral(
              "retouch-tool-visibility:v2|family:Retouch|variant:Remove|mode:remove|status:Remove / Paint area / After|last:remove|hud:true|layout:single-row-command-bar|primary:Apply|inline:2|more:5|text:no-elide-no-overlap|menus:secondary-actions") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchWorkflowSignature",
          Q_RETURN_ARG(QVariant, retouch_mode)) ||
      retouch_mode.toString() !=
          QStringLiteral("mode:remove|label:Remove|stage:Paint area|source:none|apply:true|regenerate:true|capability:%1|beforeAfter:after")
              .arg(retouch_inpaint_capability) ||
      editor_preview.activeMaskRetouchMode() != QStringLiteral("remove")) {
    std::cerr << "Retouch remove state: activeMode="
              << editor_preview.activeMaskRetouchMode().toStdString()
              << " menu=" << retouch_mode_menu_button->property("text").toString().toStdString()
              << " status=" << retouch_status_label->property("text").toString().toStdString()
              << " visibility="
              << retouch_tool_hud->property("visibilityContract").toString().toStdString()
              << " more=" << retouch_more_menu_button->property("menuContract").toString().toStdString()
              << " workflow=" << retouch_mode.toString().toStdString() << '\n';
    return fail("retouch remove button should reflect the active retouch mode");
  }
  editor_preview.startRemoveRefineBrush(true);
  QCoreApplication::processEvents();
  if (!retouch_remove_subtract_button->property("active").toBool() ||
      retouch_remove_add_button->property("active").toBool() ||
      editor_preview.activeMaskOperation() != QStringLiteral("subtract") ||
      editor_preview.activeMaskRetouchMode() != QStringLiteral("remove")) {
    return fail("retouch remove HUD should expose subtract refinement before applying");
  }
  QVariant retouch_recoverability_signature;
  QVariant retouch_primary_tool_signature;
  QVariant retouch_all_workflow_state_signature;
  QVariant retouch_variation_picker_signature;
  QVariant retouch_before_after_signature;
  if (!QMetaObject::invokeMethod(
           edit_workspace,
           "retouchRecoverabilitySignature",
           Q_RETURN_ARG(QVariant, retouch_recoverability_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchPrimaryToolSignature",
          Q_RETURN_ARG(QVariant, retouch_primary_tool_signature)) ||
      !QMetaObject::invokeMethod(
           edit_workspace,
           "retouchAllWorkflowStateSignature",
           Q_RETURN_ARG(QVariant, retouch_all_workflow_state_signature)) ||
      !QMetaObject::invokeMethod(
           edit_workspace,
           "retouchVariationPickerSignature",
           Q_RETURN_ARG(QVariant, retouch_variation_picker_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchBeforeAfterSignature",
          Q_RETURN_ARG(QVariant, retouch_before_after_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchActionSignature",
          Q_RETURN_ARG(QVariant, retouch_action_signature)) ||
      !retouch_recoverability_signature.toString().contains(
          QStringLiteral("mode:remove|state:%1|canvasBlocking:false|apply:true|regenerate:true")
              .arg(retouch_inpaint_capability)) ||
      (editor_preview.aiInpaintAvailable()
           ? !retouch_recoverability_signature.toString().contains(QStringLiteral("modelReason:"))
           : !retouch_recoverability_signature.toString().contains(
                  QStringLiteral("modelReason:LaMa Model Inpaint is not active in this build; retouch will use %1")
                      .arg(editor_preview.inpaintRuntimeLabel()))) ||
      retouch_primary_tool_signature.toString() !=
          QStringLiteral("remove:primary|heal:primary|patch:primary|clone:primary|dust:mode-menu|red-eye:mode-menu|skin-cleanup:experimental-hidden|frequency-smoothing:experimental-hidden") ||
      retouch_all_workflow_state_signature.toString() !=
          QStringLiteral(
              "remove:draft|heal:draft|clone:inactive|patch:draft|dust:inactive|red-eye:inactive") ||
      retouch_variation_picker_signature.toString() !=
          QStringLiteral("visible:false|count:0|index:0") ||
      retouch_before_after_signature.toString() !=
          QStringLiteral("view:after|button:Show Before|recipe:unchanged") ||
	      retouch_action_signature.toString() !=
	          QStringLiteral("apply:true|cancel:true|reset:true|delete:false|reopen:false")) {
	    return fail("retouch inpaint fallback should be recoverable without blocking canvas and experimental tools should stay out of the primary HUD");
	  }
	  trace_smoke_progress("retouch remove checked");
	  if (!QMetaObject::invokeMethod(
          edit_workspace,
	          "startRetouchTool",
	          Q_RETURN_ARG(QVariant, retouch_mode),
	          Q_ARG(QVariant, QStringLiteral("clone")),
	          Q_ARG(QVariant, false)) ||
      retouch_mode.toString() != QStringLiteral("clone")) {
    return fail("retouch HUD should start clone stamp directly");
  }
  QCoreApplication::processEvents();
  if (retouch_mode_menu_button->property("text").toString() != QStringLiteral("Clone Stamp") ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Clone Stamp")) ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Pick source")) ||
      active_tool_variant_chip_for_retouch->property("text").toString() != QStringLiteral("Clone Stamp") ||
      retouch_tool_hud->property("visibilityContract").toString() !=
          QStringLiteral(
              "retouch-tool-visibility:v2|family:Retouch|variant:Clone Stamp|mode:clone|status:Clone Stamp / Pick source / After|last:clone|hud:true|layout:single-row-command-bar|primary:Apply|inline:0|more:9|text:no-elide-no-overlap|menus:secondary-actions") ||
      !retouch_more_menu_button->property("menuContract").toString().contains(QStringLiteral("items:9")) ||
      !retouch_more_menu_button->property("menuContract").toString().contains(QStringLiteral("disabled:1")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchWorkflowSignature",
          Q_RETURN_ARG(QVariant, retouch_mode)) ||
      retouch_mode.toString() !=
          QStringLiteral("mode:clone|label:Clone Stamp|stage:Pick source|source:required|apply:true|regenerate:false|capability:deterministic|beforeAfter:after") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchToolVisibilitySignature",
          Q_RETURN_ARG(QVariant, retouch_visibility_signature)) ||
	      retouch_visibility_signature.toString() !=
	          QStringLiteral(
	              "retouch-tool-visibility:v2|family:Retouch|variant:Clone Stamp|mode:clone|status:Clone Stamp / Pick source / After|last:clone|hud:true|layout:single-row-command-bar|primary:Apply|inline:0|more:9|text:no-elide-no-overlap|menus:secondary-actions")) {
	    return fail("clone stamp HUD should clearly require a source before painting");
	  }
  trace_smoke_progress("brush layer adjustments checked");
	  if (!QMetaObject::invokeMethod(
	          edit_workspace,
	          "activateEditTool",
	          Q_ARG(QVariant, QVariant(QStringLiteral("adjust")))) ||
	      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
	      editor_preview.hasActiveMask() ||
	      !QMetaObject::invokeMethod(
	          edit_workspace,
	          "activateEditTool",
	          Q_ARG(QVariant, QVariant(QStringLiteral("retouch")))) ||
	      edit_workspace->property("editTool").toString() != QStringLiteral("retouch") ||
	      editor_preview.activeMaskToolRole() != QStringLiteral("retouch") ||
	      editor_preview.activeMaskRetouchMode() != QStringLiteral("clone") ||
	      editor_preview.activeMaskCloneSourceSet() ||
	      active_tool_variant_chip_for_retouch->property("text").toString() !=
	          QStringLiteral("Clone Stamp") ||
	      !retouch_tool_hud->property("visible").toBool() ||
	      retouch_tool_hud->property("visibilityContract").toString() !=
	          QStringLiteral(
	              "retouch-tool-visibility:v2|family:Retouch|variant:Clone Stamp|mode:clone|status:Clone Stamp / Pick source / After|last:clone|hud:true|layout:single-row-command-bar|primary:Apply|inline:0|more:9|text:no-elide-no-overlap|menus:secondary-actions")) {
	    return fail("primary Retouch activation should restore the last-used retouch variant without restoring stale clone source state");
	  }
	  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_RETOUCH")) {
	    return EXIT_SUCCESS;
	  }
	  trace_smoke_progress("retouch restore checked");
	  editor_preview.setActiveMaskCloneSource(0.34, 0.46);
	  QCoreApplication::processEvents();
  auto* clone_source_pin =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportCloneSourcePin"));
  auto* clone_source_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportCloneSourceLabel"));
  auto* clone_target_preview =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportCloneTargetPreview"));
  auto* clone_offset_line =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportCloneOffsetLine"));
  auto* clone_transform_badge =
      edit_workspace->findChild<QObject*>(QStringLiteral("editImageViewportCloneTransformBadge"));
  QVariant retouch_clone_overlay_signature;
	  if (clone_source_pin == nullptr || clone_source_label == nullptr ||
	      clone_target_preview == nullptr || clone_offset_line == nullptr ||
      clone_transform_badge == nullptr ||
      !clone_source_pin->property("visible").toBool() ||
      !clone_target_preview->property("visible").toBool() ||
      !clone_offset_line->property("visible").toBool() ||
      clone_source_pin->property("sourceStatus").toString() != QStringLiteral("Source set") ||
      clone_target_preview->property("cloneMode").toString() != QStringLiteral("aligned") ||
      clone_transform_badge->property("text").toString().isEmpty() ||
      !retouch_status_label->property("text").toString().contains(QStringLiteral("Source set")) ||
      !retouch_more_menu_button->property("menuContract").toString().contains(QStringLiteral("items:9")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchWorkflowSignature",
          Q_RETURN_ARG(QVariant, retouch_mode)) ||
      retouch_mode.toString() !=
          QStringLiteral("mode:clone|label:Clone Stamp|stage:Source set|source:set|apply:true|regenerate:false|capability:deterministic|beforeAfter:after") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchAllWorkflowStateSignature",
          Q_RETURN_ARG(QVariant, retouch_all_workflow_state_signature)) ||
	      retouch_all_workflow_state_signature.toString() !=
	          QStringLiteral("remove:draft|heal:draft|clone:draft|patch:draft|dust:inactive|red-eye:inactive")) {
	    return fail("clone stamp should show source, target, transform, and aligned overlay state after picking a source");
	  }
	  trace_smoke_progress("clone source overlay checked");
	  QVariant retouch_clone_source_panel_signature;
  if (!editor_preview.saveActiveCloneSource() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "retouchCloneSourcePanelSignature",
          Q_RETURN_ARG(QVariant, retouch_clone_source_panel_signature)) ||
	      retouch_clone_source_panel_signature.toString() !=
	          QStringLiteral("visible:true|count:1|index:0|first:Source 1")) {
	    return fail("clone source panel should save the first Clone Stamp source");
	  }
	  trace_smoke_progress("clone first source saved");
	  if (!QMetaObject::invokeMethod(
	          clone_source_pin,
	          "moveSource",
	          Q_ARG(QVariant, 0.62),
	          Q_ARG(QVariant, 0.28)) ||
	      std::abs(editor_preview.activeMaskCloneSourceX() - 0.62) > 1e-6 ||
	      std::abs(editor_preview.activeMaskCloneSourceY() - 0.28) > 1e-6) {
	    return fail("clone source pin should move the active clone source");
	  }
	  trace_smoke_progress("clone source pin moved");
	  editor_preview.setActiveMaskCloneScale(1.3);
	  editor_preview.setActiveMaskCloneRotation(-15.0);
	  editor_preview.setActiveMaskCloneAligned(false);
	  if (!editor_preview.saveActiveCloneSource()) {
	    return fail("clone source panel should save a second Clone Stamp source");
	  }
	  trace_smoke_progress("clone second source saved");
	  if (!editor_preview.selectActiveCloneSource(0) ||
	      std::abs(editor_preview.activeMaskCloneSourceX() - 0.34) > 1e-6) {
	    return fail("clone source panel should restore the first saved Clone Stamp source");
	  }
	  trace_smoke_progress("clone first source restored");
	  if (!editor_preview.selectActiveCloneSource(1) ||
	      std::abs(editor_preview.activeMaskCloneSourceX() - 0.62) > 1e-6) {
	    return fail("clone source panel should restore the second saved Clone Stamp source");
	  }
	  trace_smoke_progress("clone second source restored");
	  if (!QMetaObject::invokeMethod(
	          edit_workspace,
	          "retouchCloneSourcePanelSignature",
	          Q_RETURN_ARG(QVariant, retouch_clone_source_panel_signature)) ||
	      retouch_clone_source_panel_signature.toString() !=
	          QStringLiteral("visible:true|count:2|index:1|first:Source 1")) {
	    return fail("clone source panel should save and switch between multiple Clone Stamp sources");
	  }
	  trace_smoke_progress("clone multiple sources checked");
	  editor_preview.addBrushPoint(0.52, 0.64);
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "retouchCloneOverlaySignature",
          Q_RETURN_ARG(QVariant, retouch_clone_overlay_signature)) ||
      retouch_clone_overlay_signature.toString() !=
          QStringLiteral("visible:true|source:62,28|target:52,64|scale:1.30|rotation:-15|mode:non-aligned") ||
      clone_target_preview->property("cloneMode").toString() != QStringLiteral("non-aligned") ||
      std::abs(clone_target_preview->property("targetX").toDouble() - 0.52) > 1e-6 ||
      std::abs(clone_target_preview->property("targetY").toDouble() - 0.64) > 1e-6 ||
      std::abs(clone_target_preview->property("cloneScale").toDouble() - 1.3) > 1e-6 ||
      std::abs(clone_target_preview->property("cloneRotation").toDouble() + 15.0) > 1e-6 ||
      editor_preview.activeMaskCloneAligned() ||
	      clone_offset_line->property("offsetLength").toDouble() <= 1.0) {
	    return fail("clone overlay should expose target preview, offset line, source scale, rotation, and non-aligned mode");
	  }
	  trace_smoke_progress("clone target overlay checked");
	  auto* cancel_active_tool_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("cancelActiveToolShortcut"));
  if (cancel_active_tool_shortcut == nullptr ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "cancelActiveTool",
          Q_RETURN_ARG(QVariant, cancel_result)) ||
      !cancel_result.toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
	      editor_preview.hasActiveMask() ||
	      retouch_tool_hud->property("visible").toBool()) {
	    return fail("Escape/cancel should leave retouch mode and return to neutral Adjust state");
	  }
	  trace_smoke_progress("retouch cancel checked");
  } else {
    trace_smoke_progress("retouch proofs skipped");
  }
	  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activateEditTool",
          Q_ARG(QVariant, QVariant(QStringLiteral("crop")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "cancelActiveTool",
          Q_RETURN_ARG(QVariant, cancel_result)) ||
      !cancel_result.toBool() ||
	      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
	      editor_preview.cropEditing()) {
	    return fail("Escape/cancel should leave Crop without leaving crop editing active");
	  }
	  trace_smoke_progress("crop cancel checked");
	  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activateEditTool",
          Q_ARG(QVariant, QVariant(QStringLiteral("compare")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "cancelActiveTool",
          Q_RETURN_ARG(QVariant, cancel_result)) ||
      !cancel_result.toBool() ||
	      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
	      editor_preview.compareMode()) {
	    return fail("Escape/cancel should leave Compare and restore the Adjust workspace");
	  }
	  trace_smoke_progress("compare cancel checked");
	  edit_workspace->setProperty("diagnosticsPanelOpen", true);
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "cancelActiveTool",
          Q_RETURN_ARG(QVariant, cancel_result)) ||
      !cancel_result.toBool() ||
	      edit_workspace->property("diagnosticsPanelOpen").toBool() ||
	      edit_workspace->property("editTool").toString() != QStringLiteral("adjust")) {
	    return fail("Escape/cancel should close transient diagnostics before changing tools");
	  }
	  trace_smoke_progress("diagnostics cancel checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_TOOL_EXIT_PATHS")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    editor_preview.setImageProvider(nullptr);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return EXIT_SUCCESS;
  }
	  editor_preview.deselectActiveMask();
  edit_workspace->setProperty("editTool", QStringLiteral("adjust"));
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* white_balance_panel_reset_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("whiteBalancePanelResetButton"));
  auto* light_panel_reset_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("lightPanelResetButton"));
  auto* color_panel_reset_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("colorPanelResetButton"));
  if (white_balance_panel_reset_button == nullptr || light_panel_reset_button == nullptr ||
      color_panel_reset_button == nullptr || !white_balance_panel_reset_button->property("visible").toBool() ||
	      !light_panel_reset_button->property("visible").toBool() ||
	      !color_panel_reset_button->property("visible").toBool()) {
	    return fail("adjust inspector should expose per-panel reset buttons for white balance, light, and color");
	  }
	  trace_smoke_progress("adjust reset buttons checked");
	  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activateEditTool",
          Q_ARG(QVariant, QVariant(QStringLiteral("crop"))))) {
    return fail("edit workspace should expose crop inspector activation for crop detail controls");
  }
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* crop_rect_reset_button = edit_workspace->findChild<QObject*>(QStringLiteral("resetCropRectButton"));
  auto* transform_reset_button = edit_workspace->findChild<QObject*>(QStringLiteral("resetTransformButton"));
  auto* crop_aspect_combo = edit_workspace->findChild<QObject*>(QStringLiteral("cropAspectPresetComboBox"));
  auto* crop_aspect_input = edit_workspace->findChild<QObject*>(QStringLiteral("cropAspectPresetInput"));
  auto* crop_aspect_add_button = edit_workspace->findChild<QObject*>(QStringLiteral("addCropAspectPresetButton"));
  auto* crop_aspect_remove_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("removeCropAspectPresetButton"));
  if (crop_rect_reset_button == nullptr || transform_reset_button == nullptr || crop_aspect_combo == nullptr ||
      crop_aspect_input == nullptr || crop_aspect_add_button == nullptr || crop_aspect_remove_button == nullptr ||
      !crop_rect_reset_button->property("visible").toBool() ||
      transform_reset_button->property("visible").toBool() ||
      crop_aspect_combo->property("count").toInt() < 6 ||
      !crop_aspect_combo->property("visible").toBool()) {
    return fail("crop inspector should prioritize crop, aspect, and reset while keeping transform reset in advanced controls");
  }
  trace_smoke_progress("crop inspector checked");
  editor_preview.removeCustomCropAspectPreset(QStringLiteral("13:7"));
  QVariant added_crop_aspect;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "addCropAspectPreset",
          Q_RETURN_ARG(QVariant, added_crop_aspect),
          Q_ARG(QVariant, QStringLiteral("13:7"))) ||
      !added_crop_aspect.toBool()) {
    return fail("studio crop inspector should add custom aspect presets through the shared controller API");
  }
  QCoreApplication::processEvents();
  if (editor_preview.cropPreset() != QStringLiteral("13:7") ||
      !editor_preview.cropAspectPresets().contains(QStringLiteral("13:7")) ||
      !crop_aspect_remove_button->property("enabled").toBool()) {
    return fail("new custom crop aspect presets should become selected and removable in the Studio inspector");
  }
  QVariant removed_crop_aspect;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "removeCropAspectPreset",
          Q_RETURN_ARG(QVariant, removed_crop_aspect),
          Q_ARG(QVariant, QStringLiteral("13:7"))) ||
      !removed_crop_aspect.toBool() || editor_preview.cropPreset() != QStringLiteral("Original") ||
      editor_preview.cropAspectPresets().contains(QStringLiteral("13:7"))) {
    return fail("studio crop inspector should remove custom aspect presets and return to Original when selected");
  }
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
  trace_smoke_progress("starting layer panel checks");

  if (!editor_preview.addFillLayer(QStringLiteral("#808080"), QStringLiteral("Smoke Fill")) ||
      !editor_preview.addTextLayer(QStringLiteral("Layer"), QStringLiteral("Smoke Text"))) {
    return fail("layer panel smoke setup should create editable layers");
  }
  edit_workspace->setProperty("activePhotoLayerIndex", 0);
  QVariant initial_layer_selected;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, initial_layer_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !initial_layer_selected.toBool()) {
    return fail("layer panel smoke setup should select the active layer through the shared selection model");
  }
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  QVariant pointer_range_selected;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "selectPhotoLayerWithModifiers",
          Q_RETURN_ARG(QVariant, pointer_range_selected),
          Q_ARG(QVariant, QVariant(1)),
          Q_ARG(QVariant, QVariant(static_cast<int>(Qt::ShiftModifier)))) ||
      !pointer_range_selected.toBool() ||
      edit_workspace->property("selectedPhotoLayerIndices").toList().size() != 2) {
    return fail("Shift-click layer selection should select the contiguous layer range");
  }
  QVariant inline_rename_started;
  QVariant inline_rename_committed;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "beginInlinePhotoLayerRename",
          Q_RETURN_ARG(QVariant, inline_rename_started),
          Q_ARG(QVariant, QVariant(0))) ||
      !inline_rename_started.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "commitInlinePhotoLayerRename",
          Q_RETURN_ARG(QVariant, inline_rename_committed),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(QStringLiteral("Smoke Text Renamed")))) ||
      !inline_rename_committed.toBool() ||
      editor_preview.photoLayers().at(0).toMap().value(QStringLiteral("displayName")).toString() !=
          QStringLiteral("Smoke Text Renamed")) {
    return fail("layer rows should support committed inline rename without opening properties");
  }
  QVariant restore_single_layer_selection;
  QMetaObject::invokeMethod(
      edit_workspace,
      "setActivePhotoLayerSelection",
      Q_RETURN_ARG(QVariant, restore_single_layer_selection),
      Q_ARG(QVariant, QVariant(0)),
      Q_ARG(QVariant, QVariant(false)));
  if (edit_inspector_panel->property("title").toString() != QStringLiteral("Layers") ||
      !layers_inspector_tab->property("active").toBool() ||
      !photo_layers_group->property("visible").toBool() ||
      white_balance_group->property("visible").toBool()) {
    return fail("layers should live in a dedicated inspector tab instead of the Adjust control stack");
  }
  trace_smoke_progress("layer tab visible");
  auto* photo_layer_solo_button = edit_workspace->findChild<QObject*>(QStringLiteral("photoLayerSoloButton"));
  auto* photo_layer_clear_solo_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("photoLayerClearSoloButton"));
  if (photo_layer_solo_button == nullptr ||
      photo_layer_solo_button->property("text").toString() != QStringLiteral("Isolate") ||
      (photo_layer_clear_solo_button != nullptr &&
       photo_layer_clear_solo_button->property("visible").toBool())) {
    std::cerr << "Layer isolate smoke state: soloButton="
              << (photo_layer_solo_button == nullptr ? "null" : "present")
              << " text="
              << (photo_layer_solo_button == nullptr
                      ? std::string()
                      : photo_layer_solo_button->property("text").toString().toStdString())
              << " clearButton="
              << (photo_layer_clear_solo_button == nullptr ? "null" : "present")
              << " clearVisible="
              << (photo_layer_clear_solo_button != nullptr &&
                  photo_layer_clear_solo_button->property("visible").toBool())
              << " layerCount=" << editor_preview.photoLayerCount()
              << " soloActive=" << editor_preview.photoLayerSoloActive() << "\n";
    return fail("layer panel should keep isolation chrome hidden when inactive");
  }
  QVariant live_layer_stack_acceptance_signature;
  if (photo_layers_group == nullptr ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "layerStackAcceptanceSignature",
          Q_RETURN_ARG(QVariant, live_layer_stack_acceptance_signature)) ||
      !live_layer_stack_acceptance_signature.toString().contains(
          QStringLiteral("layer-stack-acceptance:v2|select:pointer-modifiers-keyboard|rename:inline-f2-properties|reorder:drag-keyboard-menu")) ||
      !live_layer_stack_acceptance_signature.toString().contains(
          QStringLiteral("|visibility:toggle-and-state|opacity:fixed-readout|mask-select:inspector-context|transform:entry-exit|escape:transient-first")) ||
      photo_layers_group->property("layerStackAcceptanceContract").toString() !=
          live_layer_stack_acceptance_signature.toString()) {
    return fail("layer stack should expose a live acceptance contract for selection, rename, reorder, visibility, opacity, masks, transform, and Escape behavior");
  }
  auto* photo_layer_opacity_label = find_named_object(edit_workspace, QStringLiteral("photoLayerOpacityLabel"));
  auto* photo_layer_opacity_slider =
      find_named_object(edit_workspace, QStringLiteral("photoLayerOpacitySlider"));
  auto* photo_layer_blend_combo =
      find_named_object(edit_workspace, QStringLiteral("photoLayerBlendModeComboBox"));
  auto* photo_layer_clip_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerClippingMaskButton"));
  auto* photo_layer_mask_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskButton"));
  auto* photo_layer_mask_badge = find_named_object(edit_workspace, QStringLiteral("photoLayerMaskBadge"));
  auto* photo_layer_transform_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTransformButton"));
  auto* photo_layer_visibility_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerVisibilityButton"));
  auto* photo_layer_row_solo_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerRowSoloButton"));
  auto* photo_layer_selection_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerSelectionButton"));
  auto* photo_layer_adjustment_command_bar =
      find_named_object(edit_workspace, QStringLiteral("photoLayerAdjustmentCommandBar"));
  auto* selected_layer_header =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeader"));
  auto* selected_layer_header_name_label =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeaderNameLabel"));
  auto* selected_layer_header_mode_label =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeaderModeLabel"));
  auto* selected_layer_header_status_label =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeaderStatusLabel"));
  auto* selected_layer_header_changed_groups_label =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeaderChangedGroupsLabel"));
  auto* selected_layer_header_thumbnail =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeaderThumbnail"));
  auto* selected_layer_header_thumbnail_image =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeaderThumbnailImage"));
  auto* copy_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("copyPhotoLayerAdjustmentsButton"));
  auto* paste_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("pastePhotoLayerAdjustmentsButton"));
  auto* paste_global_adjustments_to_photo_layer_button =
      find_named_object(edit_workspace, QStringLiteral("pasteGlobalAdjustmentsToPhotoLayerButton"));
  auto* toggle_photo_layer_adjustments_preview_button =
      find_named_object(edit_workspace, QStringLiteral("togglePhotoLayerAdjustmentsPreviewButton"));
  auto* sync_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("syncPhotoLayerAdjustmentsButton"));
  auto* clear_photo_layer_selection_button =
      find_named_object(edit_workspace, QStringLiteral("clearPhotoLayerSelectionButton"));
  auto* photo_layer_inline_actions =
      find_named_object(edit_workspace, QStringLiteral("photoLayerInlineActions"));
  auto* photo_layer_fixed_state_action_zone =
      find_named_object(edit_workspace, QStringLiteral("photoLayerFixedStateActionZone"));
  auto* photo_layer_mask_state_pill =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskStatePill"));
  auto* photo_layer_structure_actions =
      find_named_object(edit_workspace, QStringLiteral("photoLayerStructureActions"));
  auto* photo_layer_reorder_handle =
      find_named_object(edit_workspace, QStringLiteral("photoLayerReorderHandle"));
  auto* photo_layer_lock_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerLockButton"));
  auto* photo_layer_move_up_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMoveUpButton"));
  auto* photo_layer_move_down_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMoveDownButton"));
  auto* photo_layer_duplicate_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerDuplicateButton"));
  auto* photo_layer_delete_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerDeleteButton"));
  auto* photo_layer_row = find_named_object(edit_workspace, QStringLiteral("photoLayerRow"));
  auto* photo_layer_thumbnail = find_named_object(edit_workspace, QStringLiteral("photoLayerThumbnail"));
  auto* photo_layer_thumbnail_image =
      find_named_object(edit_workspace, QStringLiteral("photoLayerThumbnailImage"));
  auto* photo_layer_type_icon = find_named_object(edit_workspace, QStringLiteral("photoLayerTypeIcon"));
  auto* photo_layer_name_field = find_named_object(edit_workspace, QStringLiteral("photoLayerNameField"));
  auto* photo_layer_state_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerStateLabel"));
  auto* photo_layer_state_badge_row =
      find_named_object(edit_workspace, QStringLiteral("photoLayerStateBadgeRow"));
  auto* photo_layer_changed_group_badge_row =
      find_named_object(edit_workspace, QStringLiteral("photoLayerChangedGroupBadgeRow"));
  auto* photo_layer_adjustment_summary_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerAdjustmentSummaryLabel"));
  auto* photo_layer_job_status_chip =
      find_named_object(edit_workspace, QStringLiteral("photoLayerJobStatusChip"));
  auto* base_image_layer_row = find_named_object(edit_workspace, QStringLiteral("baseImageLayerRow"));
  auto* base_image_layer_name_label =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerNameLabel"));
  auto* base_image_layer_status_label =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerStatusLabel"));
  auto* base_image_layer_metadata_label =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerMetadataLabel"));
  auto* base_image_layer_target_area =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerRowTargetArea"));
  auto* base_image_layer_thumbnail =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerThumbnail"));
  auto* base_image_layer_thumbnail_image =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerThumbnailImage"));
  auto* base_image_layer_lock_badge =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerLockBadge"));
  auto* base_image_global_adjustment_badge =
      find_named_object(edit_workspace, QStringLiteral("baseImageGlobalAdjustmentBadge"));
  auto* base_image_layer_lock_button =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerLockButton"));
  auto* base_image_layer_visibility_button =
      find_named_object(edit_workspace, QStringLiteral("baseImageLayerVisibilityButton"));
  auto* photo_layer_nested_mask_list =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskList"));
  auto* photo_layers_mask_set_list =
      find_named_object(edit_workspace, QStringLiteral("photoLayersMaskSetList"));
  auto* photo_layer_nested_mask_section_header =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskSectionHeader"));
  auto* photo_layer_nested_mask_section_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskSectionLabel"));
  auto* photo_layer_nested_mask_section_toggle =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskSectionToggle"));
  auto* photo_layer_nested_mask_row =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskRow"));
  auto* photo_layer_nested_mask_name_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskNameLabel"));
  auto* photo_layer_nested_mask_status_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskStatusLabel"));
  auto* photo_layer_nested_mask_job_status_chip =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskJobStatusChip"));
  auto* photo_layer_nested_mask_thumbnail =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskThumbnail"));
  auto* photo_layer_nested_mask_thumbnail_image =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskThumbnailImage"));
  auto* photo_layer_nested_mask_type_icon =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskTypeIcon"));
  auto* photo_layer_nested_mask_order_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskOrderLabel"));
  auto* photo_layer_nested_mask_edit_layer_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskEditLayerButton"));
  auto* photo_layer_nested_mask_select_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskSelectButton"));
  auto* photo_layer_nested_mask_preview_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskPreviewButton"));
  auto* photo_layer_nested_mask_move_up_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskMoveUpButton"));
  auto* photo_layer_nested_mask_move_down_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskMoveDownButton"));
  auto* photo_layer_nested_mask_more_menu_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskRowMoreMenuButton"));
  auto* photo_layer_mask_set_row =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetRow"));
  auto* photo_layer_mask_set_status_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetStatusLabel"));
  auto* photo_layer_mask_set_job_status_chip =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetJobStatusChip"));
  auto* photo_layer_mask_set_thumbnail =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetThumbnail"));
  auto* photo_layer_mask_set_thumbnail_image =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetThumbnailImage"));
  auto* photo_layer_mask_set_adjust_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetAdjustButton"));
  auto* photo_layer_mask_set_select_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetSelectButton"));
  auto* photo_layer_mask_set_preview_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetPreviewButton"));
  auto* photo_layer_mask_set_more_menu_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetRowMoreMenuButton"));
  auto* photo_layer_text_editor = find_named_object(edit_workspace, QStringLiteral("photoLayerTextEditor"));
  auto* photo_layer_text_content_field =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextContentField"));
  auto* photo_layer_text_font_field =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextFontField"));
  auto* photo_layer_text_color_field =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextColorField"));
  auto* photo_layer_text_size_slider =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextSizeSlider"));
  auto* photo_layer_text_bold_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextBoldButton"));
  auto* photo_layer_text_align_center_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextAlignCenterButton"));
  auto* photo_layer_text_box_width_slider =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextBoxWidthSlider"));
  auto* photo_layer_text_box_height_slider =
      find_named_object(edit_workspace, QStringLiteral("photoLayerTextBoxHeightSlider"));
  const auto photo_layer_state_text =
      photo_layer_state_label == nullptr ? QString() : photo_layer_state_label->property("text").toString();
  const auto photo_layer_state_badge_signature =
      photo_layer_state_badge_row == nullptr
          ? QString()
          : photo_layer_state_badge_row->property("badgeSignature").toString();
  const auto photo_layer_failure_signature =
      photo_layer_row == nullptr ? QString() : photo_layer_row->property("layerFailureSignature").toString();
  const auto photo_layer_failure_active =
      photo_layer_failure_signature.contains(QStringLiteral("failed:true"));
  const auto photo_layer_mask_failure_signature =
      photo_layer_mask_set_row == nullptr
          ? QString()
          : photo_layer_mask_set_row->property("maskFailureSignature").toString();
  const auto nested_mask_failure_signature =
      photo_layer_nested_mask_row == nullptr
          ? QString()
          : photo_layer_nested_mask_row->property("maskFailureSignature").toString();
  QVariant mask_list_partition_signature_variant;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "maskSetListPartitionSignature",
          Q_RETURN_ARG(QVariant, mask_list_partition_signature_variant))) {
    return fail("layer mask list partition should expose a runtime signature");
  }
  const auto mask_list_partition_signature =
      mask_list_partition_signature_variant.toString();
  const auto standalone_mask_list_partition_signature =
      photo_layers_mask_set_list == nullptr
          ? QString()
          : photo_layers_mask_set_list->property("maskListPartitionSignature").toString();
  const auto nested_mask_list_contract =
      photo_layer_nested_mask_list == nullptr
          ? QString()
          : photo_layer_nested_mask_list->property("nestedMaskListContract").toString();
  const auto photo_layer_job_signature =
      photo_layer_job_status_chip == nullptr
          ? QString()
          : photo_layer_job_status_chip->property("jobStatusSignature").toString();
  const auto mask_set_job_signature =
      photo_layer_mask_set_job_status_chip == nullptr
          ? QString()
          : photo_layer_mask_set_job_status_chip->property("jobStatusSignature").toString();
  const auto nested_mask_job_signature =
      photo_layer_nested_mask_job_status_chip == nullptr
          ? QString()
          : photo_layer_nested_mask_job_status_chip->property("jobStatusSignature").toString();
  const auto photo_layer_thumbnail_state_signature =
      photo_layer_thumbnail == nullptr
          ? QString()
          : photo_layer_thumbnail->property("thumbnailStateSignature").toString();
  const auto photo_layer_thumbnail_display_state =
      photo_layer_thumbnail == nullptr
          ? QString()
          : photo_layer_thumbnail->property("thumbnailDisplayState").toString();
  const auto photo_layer_mask_set_thumbnail_state_signature =
      photo_layer_mask_set_thumbnail == nullptr
          ? QString()
          : photo_layer_mask_set_thumbnail->property("thumbnailStateSignature").toString();
  const auto photo_layer_mask_set_thumbnail_display_state =
      photo_layer_mask_set_thumbnail == nullptr
          ? QString()
          : photo_layer_mask_set_thumbnail->property("thumbnailDisplayState").toString();
  const auto nested_mask_thumbnail_state_signature =
      photo_layer_nested_mask_thumbnail == nullptr
          ? QString()
          : photo_layer_nested_mask_thumbnail->property("thumbnailStateSignature").toString();
  const auto nested_mask_thumbnail_display_state =
      photo_layer_nested_mask_thumbnail == nullptr
          ? QString()
          : photo_layer_nested_mask_thumbnail->property("thumbnailDisplayState").toString();
  const auto mask_set_thumbnail_failure_contained =
      photo_layer_mask_set_thumbnail != nullptr &&
      photo_layer_mask_set_thumbnail->property("thumbnailFailureContained").toBool();
  const auto nested_mask_thumbnail_failure_contained =
      photo_layer_nested_mask_thumbnail != nullptr &&
      photo_layer_nested_mask_thumbnail->property("thumbnailFailureContained").toBool();
  const auto photo_layer_thumbnail_backing_width =
      photo_layer_thumbnail == nullptr
          ? 0
          : photo_layer_thumbnail->property("thumbnailBackingWidth").toInt();
  const auto photo_layer_thumbnail_backing_height =
      photo_layer_thumbnail == nullptr
          ? 0
          : photo_layer_thumbnail->property("thumbnailBackingHeight").toInt();
  const auto photo_layer_thumbnail_source_size =
      photo_layer_thumbnail_image == nullptr
          ? QSize()
          : photo_layer_thumbnail_image->property("sourceSize").toSize();
  const auto selected_layer_header_signature =
      selected_layer_header == nullptr
          ? QString()
          : selected_layer_header->property("headerSignature").toString();
  const auto selected_layer_header_thumbnail_source_size =
      selected_layer_header_thumbnail_image == nullptr
          ? QSize()
          : selected_layer_header_thumbnail_image->property("sourceSize").toSize();
  const auto photo_layer_inline_actions_contract =
      photo_layer_inline_actions == nullptr
          ? QString()
          : photo_layer_inline_actions->property("actionVisibilityContract").toString();
  const auto photo_layer_fixed_state_action_zone_contract =
      photo_layer_fixed_state_action_zone == nullptr
          ? QString()
          : photo_layer_fixed_state_action_zone->property("actionZoneContract").toString();
  const auto photo_layer_structure_actions_contract =
      photo_layer_structure_actions == nullptr
          ? QString()
          : photo_layer_structure_actions->property("actionVisibilityContract").toString();
  const auto photo_layer_reorder_contract =
      photo_layer_reorder_handle == nullptr
          ? QString()
          : photo_layer_reorder_handle->property("reorderContract").toString();
  const auto photo_layer_thumbnail_pixel_signature =
      photo_layer_thumbnail == nullptr
          ? QString()
          : photo_layer_thumbnail->property("thumbnailPixelRatioSignature").toString();
  const auto mask_set_thumbnail_pixel_signature =
      photo_layer_mask_set_thumbnail == nullptr
          ? QString()
          : photo_layer_mask_set_thumbnail->property("thumbnailPixelRatioSignature").toString();
  const auto mask_set_thumbnail_source_size =
      photo_layer_mask_set_thumbnail_image == nullptr
          ? QSize()
          : photo_layer_mask_set_thumbnail_image->property("sourceSize").toSize();
  const auto nested_mask_thumbnail_pixel_signature =
      photo_layer_nested_mask_thumbnail == nullptr
          ? QString()
          : photo_layer_nested_mask_thumbnail->property("thumbnailPixelRatioSignature").toString();
  const auto nested_mask_thumbnail_source_size =
      photo_layer_nested_mask_thumbnail_image == nullptr
          ? QSize()
          : photo_layer_nested_mask_thumbnail_image->property("sourceSize").toSize();
  const auto mask_set_status_text =
      photo_layer_mask_set_status_label == nullptr
          ? QString()
          : photo_layer_mask_set_status_label->property("text").toString();
  const auto nested_mask_status_text =
      photo_layer_nested_mask_status_label == nullptr
          ? QString()
          : photo_layer_nested_mask_status_label->property("text").toString();
  const auto nested_mask_section_contract =
      photo_layer_nested_mask_list == nullptr
          ? QString()
          : photo_layer_nested_mask_list->property("maskSectionContract").toString();
  const auto standalone_mask_row_present = photo_layer_mask_set_row != nullptr;
  auto* photo_layer_sparse_header =
      find_named_object(edit_workspace, QStringLiteral("photoLayerSparseHeader"));
  auto* photo_layer_properties_panel =
      find_named_object(edit_workspace, QStringLiteral("photoLayerPropertiesPanel"));
  const auto sparse_layer_stack_mode =
      photo_layers_group != nullptr &&
      photo_layers_group->property("sparseLayerUiContract")
          .toString()
          .contains(QStringLiteral("layer-sparse-ui:v3"));
  if (sparse_layer_stack_mode &&
      (photo_layer_sparse_header == nullptr ||
       photo_layer_properties_panel == nullptr ||
       !photo_layer_properties_panel->property("visible").toBool() ||
       !photo_layer_properties_panel->property("sparsePropertiesContract")
            .toString()
            .contains(QStringLiteral("layer-properties:v2|source:active-layer")) ||
       !photo_layer_properties_panel->property("sparsePropertiesContract")
            .toString()
            .contains(QStringLiteral("quick-actions:mask-lock-duplicate-delete")) ||
       !photo_layer_properties_panel->property("sparsePropertiesContract")
            .toString()
            .contains(QStringLiteral("progressive:true")) ||
       photo_layer_opacity_slider == nullptr ||
       photo_layer_blend_combo == nullptr ||
       photo_layer_clip_button == nullptr ||
       photo_layer_mask_button == nullptr ||
       !photo_layer_mask_button->property("visible").toBool() ||
       photo_layer_transform_button == nullptr ||
       photo_layer_row_solo_button == nullptr ||
       photo_layer_selection_button == nullptr ||
       copy_photo_layer_adjustments_button == nullptr ||
       paste_photo_layer_adjustments_button == nullptr ||
       paste_global_adjustments_to_photo_layer_button == nullptr ||
       toggle_photo_layer_adjustments_preview_button == nullptr ||
       sync_photo_layer_adjustments_button == nullptr ||
       photo_layer_lock_button == nullptr ||
       !photo_layer_lock_button->property("visible").toBool() ||
       photo_layer_duplicate_button == nullptr ||
       !photo_layer_duplicate_button->property("visible").toBool() ||
       photo_layer_delete_button == nullptr ||
       !photo_layer_delete_button->property("visible").toBool() ||
       photo_layer_row == nullptr ||
       photo_layer_row->property("height").toDouble() > 54.0 ||
       !photo_layer_row->property("layoutFits").toBool() ||
       (photo_layer_opacity_label != nullptr &&
        photo_layer_opacity_label->property("visible").toBool()) ||
       (photo_layer_mask_state_pill != nullptr &&
        photo_layer_mask_state_pill->property("visible").toBool()) ||
	       (photo_layer_inline_actions != nullptr &&
	        photo_layer_inline_actions->property("visible").toBool()) ||
	       (photo_layer_inline_actions != nullptr &&
	        photo_layer_inline_actions->property("active").toBool()) ||
	       (photo_layer_structure_actions != nullptr &&
	        photo_layer_structure_actions->property("visible").toBool()) ||
	       (photo_layer_structure_actions != nullptr &&
	        photo_layer_structure_actions->property("active").toBool()) ||
	       (selected_layer_header != nullptr &&
	        selected_layer_header->property("visible").toBool()) ||
	       (photo_layer_adjustment_command_bar != nullptr &&
	        edit_workspace->property("selectedPhotoLayerIndices").toList().size() <= 1 &&
	        photo_layer_adjustment_command_bar->property("visible").toBool()) ||
	       find_named_object(edit_workspace, QStringLiteral("photoLayerOpacitySliderLegacy")) != nullptr ||
	       find_named_object(edit_workspace, QStringLiteral("photoLayerMaskOpacitySliderLegacy")) != nullptr ||
	       find_named_object(edit_workspace, QStringLiteral("photoLayerMaskDensitySliderLegacy")) != nullptr ||
	       find_named_object(edit_workspace, QStringLiteral("photoLayerBlendModeComboBoxLegacy")) != nullptr ||
	       find_named_object(edit_workspace, QStringLiteral("photoLayerLockButtonLegacy")) != nullptr ||
	       find_named_object(edit_workspace, QStringLiteral("photoLayerMoveUpButtonLegacy")) != nullptr ||
	       find_named_object(edit_workspace, QStringLiteral("photoLayerMoveDownButtonLegacy")) != nullptr ||
	       find_named_object(edit_workspace, QStringLiteral("photoLayerDuplicateButtonLegacy")) != nullptr ||
	       find_named_object(edit_workspace, QStringLiteral("photoLayerDeleteButtonLegacy")) != nullptr ||
	       find_named_object(edit_workspace, QStringLiteral("photoLayerOpacityDownButton")) != nullptr ||
	       find_named_object(edit_workspace, QStringLiteral("photoLayerOpacityUpButton")) != nullptr)) {
	    return fail("sparse layer stack should keep rows compact and move precision controls to the Layer Properties panel");
	  }
	  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_HOVER_LIGHTWEIGHT")) {
	    if (!sparse_layer_stack_mode ||
	        photo_layer_inline_actions == nullptr ||
	        photo_layer_structure_actions == nullptr ||
	        photo_layer_inline_actions->property("active").toBool() ||
	        photo_layer_structure_actions->property("active").toBool() ||
	        find_named_object(edit_workspace, QStringLiteral("photoLayerSelectionButtonLegacy")) != nullptr ||
	        find_named_object(edit_workspace, QStringLiteral("photoLayerOpacitySliderLegacy")) != nullptr ||
	        find_named_object(edit_workspace, QStringLiteral("photoLayerMaskOpacitySliderLegacy")) != nullptr ||
	        find_named_object(edit_workspace, QStringLiteral("photoLayerMaskDensitySliderLegacy")) != nullptr ||
	        find_named_object(edit_workspace, QStringLiteral("photoLayerBlendModeComboBoxLegacy")) != nullptr ||
	        find_named_object(edit_workspace, QStringLiteral("photoLayerClippingMaskButtonLegacy")) != nullptr ||
	        find_named_object(edit_workspace, QStringLiteral("photoLayerMaskButtonLegacy")) != nullptr ||
	        find_named_object(edit_workspace, QStringLiteral("photoLayerTransformButtonLegacy")) != nullptr ||
	        find_named_object(edit_workspace, QStringLiteral("photoLayerRowSoloButtonLegacy")) != nullptr ||
	        find_named_object(edit_workspace, QStringLiteral("photoLayerLockButtonLegacy")) != nullptr ||
	        find_named_object(edit_workspace, QStringLiteral("photoLayerMoveUpButtonLegacy")) != nullptr ||
	        find_named_object(edit_workspace, QStringLiteral("photoLayerMoveDownButtonLegacy")) != nullptr ||
	        find_named_object(edit_workspace, QStringLiteral("photoLayerDuplicateButtonLegacy")) != nullptr ||
	        find_named_object(edit_workspace, QStringLiteral("photoLayerDeleteButtonLegacy")) != nullptr) {
	      return fail("photo layer row hover should not instantiate legacy precision or structure controls");
	    }
	    trace_smoke_progress("layer hover lightweight row checked");

	    return EXIT_SUCCESS;
	  }
  if (!sparse_layer_stack_mode &&
      (photo_layer_opacity_label == nullptr || photo_layer_opacity_slider == nullptr ||
      photo_layer_blend_combo == nullptr ||
      photo_layer_clip_button == nullptr || photo_layer_mask_button == nullptr ||
      photo_layer_mask_badge == nullptr || photo_layer_transform_button == nullptr ||
      photo_layer_visibility_button == nullptr || photo_layer_row_solo_button == nullptr ||
      photo_layer_selection_button == nullptr || photo_layer_adjustment_command_bar == nullptr ||
      selected_layer_header == nullptr || selected_layer_header_name_label == nullptr ||
      selected_layer_header_mode_label == nullptr || selected_layer_header_status_label == nullptr ||
      selected_layer_header_changed_groups_label == nullptr ||
      selected_layer_header_thumbnail == nullptr ||
      selected_layer_header_thumbnail_image == nullptr ||
      copy_photo_layer_adjustments_button == nullptr ||
      paste_photo_layer_adjustments_button == nullptr ||
      paste_global_adjustments_to_photo_layer_button == nullptr ||
      toggle_photo_layer_adjustments_preview_button == nullptr ||
      sync_photo_layer_adjustments_button == nullptr ||
      clear_photo_layer_selection_button == nullptr ||
      photo_layer_inline_actions == nullptr ||
      photo_layer_fixed_state_action_zone == nullptr ||
      photo_layer_mask_state_pill == nullptr ||
      photo_layer_structure_actions == nullptr ||
      photo_layer_reorder_handle == nullptr ||
      photo_layer_lock_button == nullptr ||
      photo_layer_move_up_button == nullptr || photo_layer_move_down_button == nullptr ||
      photo_layer_duplicate_button == nullptr || photo_layer_delete_button == nullptr ||
      photo_layer_inline_actions_contract !=
          QStringLiteral("layer-actions:v2|lazy:true|active:false|hover-instantiates:false|precision-controls:properties-panel|keyboard-accessible:properties-panel|disabled-reasons:true") ||
      photo_layer_fixed_state_action_zone_contract !=
          QStringLiteral("layer-row-action-zone:v4|component:LayerRowActionZone|normal-width:60|brush-width:30|brush:more-only|summary-in-tooltip:true|mask-state:tooltip-only|opacity-readout:properties-only|visibility:icon-only|more:icon-only|hover-no-shift:true|no-elide:true") ||
      photo_layer_structure_actions_contract !=
          QStringLiteral("layer-structure-actions:v2|lazy:true|active:false|hover-instantiates:false|reorder:properties-menu|overflow-menu:true|destructive-separated:true|disabled-reasons:true") ||
      photo_layer_reorder_contract !=
          QStringLiteral("layer-reorder:v2|drag-release:any-distance:true|keyboard:true|menu:true|selection-preserved:true|locked:blocked") ||
      photo_layer_row == nullptr || photo_layer_thumbnail == nullptr ||
      photo_layer_thumbnail_image == nullptr || photo_layer_type_icon == nullptr ||
      photo_layer_name_field == nullptr || photo_layer_state_label == nullptr ||
      photo_layer_state_badge_row == nullptr ||
      photo_layer_job_status_chip == nullptr ||
      base_image_layer_row == nullptr || base_image_layer_name_label == nullptr ||
      base_image_layer_status_label == nullptr || base_image_layer_metadata_label == nullptr ||
      base_image_layer_target_area == nullptr ||
      base_image_layer_thumbnail == nullptr || base_image_layer_thumbnail_image == nullptr ||
      base_image_layer_lock_badge == nullptr || base_image_global_adjustment_badge == nullptr ||
      base_image_layer_lock_button == nullptr ||
      base_image_layer_visibility_button == nullptr ||
      !item_visible(qobject_cast<QQuickItem*>(base_image_layer_row)) ||
      !base_image_layer_row->property("baseLayerLocked").toBool() ||
      base_image_layer_row->property("baseLayerStackPosition").toString() !=
          QStringLiteral("bottom") ||
      base_image_layer_row->property("baseLayerTargetKind").toString() !=
          QStringLiteral("global-edits") ||
      !base_image_layer_row->property("baseLayerContract").toString().contains(
          QStringLiteral("base-image-layer:v1|locked:true|stack:bottom|target:global-edits")) ||
      base_image_layer_name_label->property("text").toString() != QStringLiteral("Base Image") ||
      !base_image_layer_status_label->property("text").toString().contains(
          QStringLiteral("Global edits target")) ||
      !base_image_layer_status_label->property("text").toString().contains(
          QStringLiteral("Locked bottom layer")) ||
      base_image_layer_metadata_label->property("text").toString().trimmed().isEmpty() ||
      base_image_layer_row->property("changedAdjustmentGroupSignature").toString().isEmpty() ||
      base_image_layer_thumbnail->property("thumbnailSource").toString() !=
          editor_preview.baseImageThumbnailSource() ||
      base_image_layer_thumbnail_image->property("source").toString() !=
          editor_preview.baseImageThumbnailSource() ||
      !base_image_layer_lock_button->property("active").toBool() ||
      base_image_layer_lock_button->property("enabled").toBool() ||
      base_image_layer_visibility_button->property("enabled").toBool() ||
      photo_layer_nested_mask_list == nullptr ||
      photo_layer_nested_mask_section_header == nullptr ||
      photo_layer_nested_mask_section_label == nullptr ||
      photo_layer_nested_mask_section_toggle == nullptr ||
      !nested_mask_section_contract.contains(QStringLiteral("mask-section:v1|scope:layer|key:")) ||
      !nested_mask_section_contract.contains(QStringLiteral("|count:")) ||
      !nested_mask_section_contract.contains(QStringLiteral("|collapsed:false")) ||
      photo_layer_nested_mask_list->property("maskSectionCollapsed").toBool() ||
      photo_layer_nested_mask_section_label->property("text").toString().trimmed().isEmpty() ||
      !photo_layer_nested_mask_section_toggle->property("active").toBool() ||
      photo_layer_nested_mask_row == nullptr ||
      photo_layer_nested_mask_name_label == nullptr ||
      photo_layer_nested_mask_status_label == nullptr ||
      photo_layer_nested_mask_job_status_chip == nullptr ||
      photo_layer_nested_mask_thumbnail == nullptr ||
      photo_layer_nested_mask_thumbnail_image == nullptr ||
      photo_layer_nested_mask_type_icon == nullptr ||
      photo_layer_nested_mask_order_label == nullptr ||
      photo_layer_nested_mask_edit_layer_button == nullptr ||
      photo_layer_nested_mask_select_button == nullptr ||
      photo_layer_nested_mask_preview_button == nullptr ||
      photo_layer_nested_mask_move_up_button == nullptr ||
      photo_layer_nested_mask_move_down_button == nullptr ||
      photo_layer_nested_mask_more_menu_button == nullptr ||
      !item_visible(qobject_cast<QQuickItem*>(photo_layer_nested_mask_row)) ||
      !item_visible(qobject_cast<QQuickItem*>(photo_layer_nested_mask_more_menu_button)) ||
      !photo_layer_nested_mask_more_menu_button->property("maskRowActionContract").toString().contains(
          QStringLiteral("mask-row-actions:v2|default:more-only")) ||
      item_visible(qobject_cast<QQuickItem*>(photo_layer_nested_mask_edit_layer_button)) ||
      item_visible(qobject_cast<QQuickItem*>(photo_layer_nested_mask_select_button)) ||
      item_visible(qobject_cast<QQuickItem*>(photo_layer_nested_mask_preview_button)) ||
      item_visible(qobject_cast<QQuickItem*>(photo_layer_nested_mask_move_up_button)) ||
      item_visible(qobject_cast<QQuickItem*>(photo_layer_nested_mask_move_down_button)) ||
      !photo_layer_nested_mask_row->property("maskChildAttachment").toBool() ||
      !mask_list_partition_signature.contains(QStringLiteral("mask-list-partition:v1")) ||
      !mask_list_partition_signature.contains(QStringLiteral("|duplicates:0")) ||
      standalone_mask_list_partition_signature != mask_list_partition_signature ||
      !nested_mask_list_contract.contains(QStringLiteral("nested-mask-list:linked-only|layer:")) ||
      !nested_mask_list_contract.contains(QStringLiteral("|count:")) ||
      photo_layer_nested_mask_row->property("parentLayerIndex").toInt() < 0 ||
      photo_layer_nested_mask_row->property("maskChildIndent").toInt() < 18 ||
      !photo_layer_nested_mask_row->property("maskChildRowContract").toString().contains(
          QStringLiteral("nested-mask-child:v1|indent:18|kind:")) ||
      !photo_layer_nested_mask_row->property("maskChildRowContract").toString().contains(
          QStringLiteral("|combine:")) ||
      !photo_layer_nested_mask_row->property("maskChildRowContract").toString().contains(
          QStringLiteral("|order:")) ||
      photo_layer_nested_mask_row->property("combineOrderIndex").toInt() < 0 ||
      photo_layer_nested_mask_row->property("combineOrderCount").toInt() < 1 ||
      photo_layer_nested_mask_row->property("combineOrderLabel").toString().trimmed().isEmpty() ||
      photo_layer_nested_mask_order_label->property("combineOrderLabel").toString() !=
          photo_layer_nested_mask_row->property("combineOrderLabel").toString() ||
      photo_layer_nested_mask_row->property("combineModeSummary").toString().trimmed().isEmpty() ||
      photo_layer_nested_mask_row->property("combineModeContract").toString().trimmed().isEmpty() ||
      !photo_layer_nested_mask_row->property("combineModeContract").toString().contains(
          QStringLiteral("mask-combine:v1")) ||
      photo_layer_nested_mask_row->property("primarySourceKind").toString().trimmed().isEmpty() ||
      photo_layer_nested_mask_type_icon->property("sourceKind").toString() !=
          photo_layer_nested_mask_row->property("primarySourceKind").toString() ||
      !photo_layer_nested_mask_type_icon->property("iconContract").toString().contains(
          QStringLiteral("mask-type-icon:v1|kind:")) ||
      !item_visible(qobject_cast<QQuickItem*>(selected_layer_header)) ||
      !selected_layer_header_signature.contains(QStringLiteral("selected-layer-header:v1")) ||
      !selected_layer_header_signature.contains(QStringLiteral("kind:")) ||
      !selected_layer_header_signature.contains(QStringLiteral("visible:Visible")) ||
      !selected_layer_header_signature.contains(QStringLiteral("opacity:")) ||
      !selected_layer_header_signature.contains(QStringLiteral("blend:")) ||
      !selected_layer_header_signature.contains(QStringLiteral("mask:")) ||
      selected_layer_header_mode_label->property("text").toString() !=
          QStringLiteral("Editing Layer") ||
      selected_layer_header_name_label->property("text").toString().trimmed().isEmpty() ||
      !selected_layer_header_status_label->property("text").toString().contains(
          QStringLiteral(" / ")) ||
	      (selected_layer_header_changed_groups_label->property("text").toString().trimmed().isEmpty() &&
	       !selected_layer_header_signature.contains(QStringLiteral("changed:groups:|count:0"))) ||
      selected_layer_header_thumbnail_source_size.width() <
          selected_layer_header->property("thumbnailBackingWidth").toInt() ||
      selected_layer_header_thumbnail_source_size.height() <
          selected_layer_header->property("thumbnailBackingHeight").toInt() ||
      photo_layer_changed_group_badge_row == nullptr ||
      photo_layer_adjustment_summary_label == nullptr ||
      (standalone_mask_row_present &&
       (photo_layer_mask_set_adjust_button == nullptr ||
         photo_layer_mask_set_preview_button == nullptr ||
         photo_layer_mask_set_status_label == nullptr ||
        photo_layer_mask_set_job_status_chip == nullptr ||
        photo_layer_mask_set_thumbnail_image == nullptr ||
        photo_layer_mask_set_select_button == nullptr ||
        photo_layer_mask_set_more_menu_button == nullptr ||
        !item_visible(qobject_cast<QQuickItem*>(photo_layer_mask_set_more_menu_button)) ||
        !photo_layer_mask_set_more_menu_button->property("maskRowActionContract").toString().contains(
            QStringLiteral("mask-row-actions:v2|default:more-only")) ||
        item_visible(qobject_cast<QQuickItem*>(photo_layer_mask_set_adjust_button)) ||
        item_visible(qobject_cast<QQuickItem*>(photo_layer_mask_set_preview_button)) ||
        item_visible(qobject_cast<QQuickItem*>(photo_layer_mask_set_select_button)) ||
        photo_layer_mask_set_row->property("clickRouteContract").toString() !=
            QStringLiteral("mask-row-click:select-mask|adjust-button:edit-layer"))) ||
	      photo_layer_nested_mask_name_label->property("text").toString().trimmed().isEmpty() ||
	      (!nested_mask_thumbnail_failure_contained &&
	       !nested_mask_status_text.contains(QStringLiteral("Linked selection"))) ||
      photo_layer_text_editor == nullptr || photo_layer_text_content_field == nullptr ||
      photo_layer_text_font_field == nullptr || photo_layer_text_color_field == nullptr ||
      photo_layer_text_size_slider == nullptr || photo_layer_text_bold_button == nullptr ||
      photo_layer_text_align_center_button == nullptr || photo_layer_text_box_width_slider == nullptr ||
      photo_layer_text_box_height_slider == nullptr ||
      photo_layer_text_editor->property("textEditorContract").toString() !=
          QStringLiteral("text-layer-editor:v2|single-instance:true|live-content:true|empty-text:true|font:true|color:true|size:8-512|weight:bold-regular|alignment:left-center-right|box:typed-and-slider|coalesced-input:true|coalesced-preview:true") ||
      (!photo_layer_failure_active && !photo_layer_state_text.contains(QStringLiteral("Visible"))) ||
      (!photo_layer_failure_active && !photo_layer_state_text.contains(QStringLiteral("Selected"))) ||
      (photo_layer_failure_active &&
       !photo_layer_state_text.contains(QStringLiteral("Preview failed:"))) ||
      !photo_layer_state_badge_row->property("visible").toBool() ||
      !photo_layer_state_badge_signature.contains(QStringLiteral("layer-state-badges:v1")) ||
      !photo_layer_state_badge_signature.contains(QStringLiteral("global:base-row")) ||
	      !photo_layer_state_badge_signature.contains(QStringLiteral("|masked:")) ||
      !photo_layer_state_badge_signature.contains(QStringLiteral("hidden:false")) ||
      !photo_layer_state_badge_signature.contains(QStringLiteral("locked:false")) ||
      !photo_layer_state_badge_signature.contains(QStringLiteral("solo:false")) ||
      !photo_layer_state_badge_signature.contains(QStringLiteral("rendering:false")) ||
	      !photo_layer_state_badge_signature.contains(
	          photo_layer_failure_active ? QStringLiteral("failed:true")
	                                     : QStringLiteral("failed:false")) ||
      !photo_layer_row->property("layerStateSignature").toString().contains(
          QStringLiteral("visible:true")) ||
      !photo_layer_row->property("layerStateSignature").toString().contains(
          QStringLiteral("hidden:false")) ||
      !photo_layer_row->property("layerStateSignature").toString().contains(
          QStringLiteral("selected:true")) ||
      !photo_layer_failure_signature.contains(QStringLiteral("row:layer|")) ||
      (photo_layer_failure_active &&
       photo_layer_failure_signature.contains(QStringLiteral("stage:|"))) ||
      (standalone_mask_row_present &&
       !photo_layer_mask_failure_signature.contains(QStringLiteral("failed:false"))) ||
      !nested_mask_failure_signature.contains(QStringLiteral("failed:false")) ||
      !photo_layer_job_signature.contains(QStringLiteral("row:photo-layer")) ||
      !photo_layer_job_signature.contains(QStringLiteral("visible:false")) ||
      photo_layer_job_status_chip->property("visible").toBool() ||
      (standalone_mask_row_present &&
       (!mask_set_job_signature.contains(QStringLiteral("row:mask-set")) ||
        !mask_set_job_signature.contains(QStringLiteral("visible:false")) ||
        photo_layer_mask_set_job_status_chip->property("visible").toBool())) ||
      !nested_mask_job_signature.contains(QStringLiteral("row:nested-mask")) ||
      !nested_mask_job_signature.contains(QStringLiteral("visible:false")) ||
      photo_layer_nested_mask_job_status_chip->property("visible").toBool() ||
      !photo_layer_thumbnail->property("thumbnailReady").toBool() ||
      !photo_layer_thumbnail->property("thumbnailSource").toString().startsWith(
          QStringLiteral("data:image/png;base64,")) ||
      !photo_layer_thumbnail->property("thumbnailDisplaySource").toString().startsWith(
          QStringLiteral("data:image/png;base64,")) ||
      (!photo_layer_thumbnail->property("thumbnailEffectPreviewSource").toString().isEmpty() &&
       (photo_layer_thumbnail->property("thumbnailEffectPreviewContract").toString() !=
            QStringLiteral("adjustment-layer-before-after-thumbnail-v1") ||
        !photo_layer_thumbnail->property("thumbnailUsesEffectPreview").toBool())) ||
      !photo_layer_thumbnail->property("thumbnailSignature").toString().contains(
          QStringLiteral("ready:true")) ||
      photo_layer_thumbnail->property("thumbnailStale").toBool() ||
      photo_layer_thumbnail->property("thumbnailFailureContained").toBool() ||
      photo_layer_thumbnail_display_state != QStringLiteral("ready") ||
      !photo_layer_thumbnail_state_signature.contains(QStringLiteral("display:ready")) ||
      !photo_layer_thumbnail_state_signature.contains(QStringLiteral("stale:false")) ||
      !photo_layer_thumbnail_state_signature.contains(QStringLiteral("failure-contained:false")) ||
      photo_layer_thumbnail_backing_width < 64 ||
      photo_layer_thumbnail_backing_height < 64 ||
      photo_layer_thumbnail->property("thumbnailDevicePixelRatio").toDouble() < 2.0 ||
      !photo_layer_thumbnail_pixel_signature.contains(QStringLiteral("display:32x32")) ||
      photo_layer_thumbnail_source_size.width() < photo_layer_thumbnail_backing_width ||
      photo_layer_thumbnail_source_size.height() < photo_layer_thumbnail_backing_height ||
      (standalone_mask_row_present &&
       (photo_layer_mask_set_thumbnail == nullptr ||
        !photo_layer_mask_set_thumbnail_state_signature.contains(QStringLiteral("stale:")) ||
        !photo_layer_mask_set_thumbnail_state_signature.contains(
            QStringLiteral("failure-contained:")))) ||
      (standalone_mask_row_present && mask_set_thumbnail_failure_contained &&
       (!photo_layer_mask_set_thumbnail_state_signature.contains(QStringLiteral("stage:thumbnail_")) ||
        !mask_set_status_text.contains(QStringLiteral("Thumbnail ")))) ||
      (standalone_mask_row_present && !mask_set_thumbnail_failure_contained &&
       (mask_set_status_text.contains(QStringLiteral("Thumbnail ")) ||
         photo_layer_mask_set_thumbnail_display_state != QStringLiteral("ready") ||
         !photo_layer_mask_set_thumbnail_state_signature.contains(QStringLiteral("display:ready")) ||
         photo_layer_mask_set_thumbnail->property("thumbnailBackingWidth").toInt() < 96 ||
         photo_layer_mask_set_thumbnail->property("thumbnailBackingHeight").toInt() < 96 ||
         photo_layer_mask_set_thumbnail->property("thumbnailDevicePixelRatio").toDouble() < 2.0 ||
          mask_set_thumbnail_source_size.width() <
              photo_layer_mask_set_thumbnail->property("thumbnailBackingWidth").toInt() ||
          mask_set_thumbnail_source_size.height() <
              photo_layer_mask_set_thumbnail->property("thumbnailBackingHeight").toInt() ||
          !mask_set_thumbnail_pixel_signature.contains(QStringLiteral("display:34x34")))) ||
      !nested_mask_thumbnail_state_signature.contains(QStringLiteral("stale:")) ||
      !nested_mask_thumbnail_state_signature.contains(QStringLiteral("failure-contained:")) ||
      (nested_mask_thumbnail_failure_contained &&
       (!nested_mask_thumbnail_state_signature.contains(QStringLiteral("stage:thumbnail_")) ||
        !nested_mask_status_text.contains(QStringLiteral("Thumbnail ")))) ||
      (!nested_mask_thumbnail_failure_contained &&
       (nested_mask_status_text.contains(QStringLiteral("Thumbnail ")) ||
        nested_mask_thumbnail_display_state != QStringLiteral("ready") ||
        !nested_mask_thumbnail_state_signature.contains(QStringLiteral("display:ready")) ||
        photo_layer_nested_mask_thumbnail->property("thumbnailBackingWidth").toInt() < 96 ||
          photo_layer_nested_mask_thumbnail->property("thumbnailBackingHeight").toInt() < 96 ||
          photo_layer_nested_mask_thumbnail->property("thumbnailDevicePixelRatio").toDouble() < 2.0 ||
          nested_mask_thumbnail_source_size.width() <
              photo_layer_nested_mask_thumbnail->property("thumbnailBackingWidth").toInt() ||
          nested_mask_thumbnail_source_size.height() <
              photo_layer_nested_mask_thumbnail->property("thumbnailBackingHeight").toInt() ||
          !nested_mask_thumbnail_pixel_signature.contains(QStringLiteral("display:34x34")))) ||
      !photo_layer_thumbnail_image->property("visible").toBool() ||
      !photo_layer_row->property("layoutFits").toBool() ||
      photo_layer_row->property("height").toDouble() > 330.0 ||
      find_named_object(edit_workspace, QStringLiteral("photoLayerOpacityDownButton")) != nullptr ||
      find_named_object(edit_workspace, QStringLiteral("photoLayerOpacityUpButton")) != nullptr)) {
    std::cerr << "Layer row control presence: opacityLabel="
              << (photo_layer_opacity_label == nullptr ? "null" : "present")
              << " opacitySlider=" << (photo_layer_opacity_slider == nullptr ? "null" : "present")
              << " blendCombo=" << (photo_layer_blend_combo == nullptr ? "null" : "present")
              << " clip=" << (photo_layer_clip_button == nullptr ? "null" : "present")
              << " mask=" << (photo_layer_mask_button == nullptr ? "null" : "present")
              << " maskBadge=" << (photo_layer_mask_badge == nullptr ? "null" : "present")
              << " transform=" << (photo_layer_transform_button == nullptr ? "null" : "present")
              << " visibility=" << (photo_layer_visibility_button == nullptr ? "null" : "present")
              << " rowSolo=" << (photo_layer_row_solo_button == nullptr ? "null" : "present")
              << " selection="
              << (photo_layer_selection_button == nullptr ? "null" : "present")
               << " commandBar="
               << (photo_layer_adjustment_command_bar == nullptr ? "null" : "present")
              << " selectedLayerHeader="
              << (selected_layer_header == nullptr
                      ? std::string("null")
                      : selected_layer_header_signature.toStdString())
              << " structure=" << (photo_layer_structure_actions == nullptr ? "null" : "present")
              << " nameField=" << (photo_layer_name_field == nullptr ? "null" : "present")
              << " thumbnail="
              << (photo_layer_thumbnail == nullptr
                      ? std::string("null")
                      : photo_layer_thumbnail->property("thumbnailSource")
                            .toString()
                            .left(24)
                            .toStdString())
	              << " rowSignature="
	              << (photo_layer_row == nullptr
	                      ? std::string("null")
	                      : photo_layer_row->property("layoutSignature").toString().toStdString())
	              << " stateText=" << photo_layer_state_text.toStdString()
	              << " stateBadges=" << photo_layer_state_badge_signature.toStdString()
	              << " headerName="
	              << (selected_layer_header_name_label == nullptr
	                      ? std::string("null")
	                      : selected_layer_header_name_label->property("text").toString().toStdString())
	              << " headerMode="
	              << (selected_layer_header_mode_label == nullptr
	                      ? std::string("null")
	                      : selected_layer_header_mode_label->property("text").toString().toStdString())
	              << " headerStatus="
	              << (selected_layer_header_status_label == nullptr
	                      ? std::string("null")
	                      : selected_layer_header_status_label->property("text").toString().toStdString())
	              << " headerChanged="
	              << (selected_layer_header_changed_groups_label == nullptr
	                      ? std::string("null")
	                      : selected_layer_header_changed_groups_label->property("text").toString().toStdString())
	              << " thumbnailImageVisible="
	              << (photo_layer_thumbnail_image != nullptr &&
	                  photo_layer_thumbnail_image->property("visible").toBool())
	              << " layerFailureSignature="
              << (photo_layer_row == nullptr
                      ? std::string("null")
                      : photo_layer_failure_signature.toStdString())
              << " maskFailureSignature="
              << (photo_layer_mask_set_row == nullptr
                      ? std::string("null")
                      : photo_layer_mask_failure_signature.toStdString())
              << " nestedMaskFailureSignature="
              << (photo_layer_nested_mask_row == nullptr
                      ? std::string("null")
                      : nested_mask_failure_signature.toStdString())
              << " layerThumbnailState="
              << photo_layer_thumbnail_state_signature.toStdString()
              << " maskSetThumbnailState="
              << photo_layer_mask_set_thumbnail_state_signature.toStdString()
              << " nestedMaskThumbnailState="
              << nested_mask_thumbnail_state_signature.toStdString()
              << " layerCount=" << editor_preview.photoLayerCount() << "\n";
    return fail("layer panel rows should fit their controls without overlap and use slider/dropdown layer controls");
  }
  trace_smoke_progress("layer row controls checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_ROWS")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
