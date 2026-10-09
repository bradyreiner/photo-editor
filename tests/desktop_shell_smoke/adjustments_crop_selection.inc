    editor_preview.setImageProvider(nullptr);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return EXIT_SUCCESS;
  }
  QVariant base_target_signature;
  QVariant professional_ux_proof_signature;
  QVariant inspector_workflow_proof_signature;
  QVariant interaction_responsiveness_proof_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetSignature",
          Q_RETURN_ARG(QVariant, base_target_signature)) ||
	      base_target_signature.toString() !=
	          QStringLiteral("scope:base-image|breadcrumb:Base Image|summary:Base edits|inspector-copy:hidden") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "studioProfessionalUxProofSignature",
          Q_RETURN_ARG(QVariant, professional_ux_proof_signature)) ||
      !professional_ux_proof_signature.toString().contains(QStringLiteral("studio-professional-ux-proof:v1")) ||
      !professional_ux_proof_signature.toString().contains(QStringLiteral("|proof:desktop-and-narrow-visual-captures")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "studioInspectorWorkflowProofSignature",
          Q_RETURN_ARG(QVariant, inspector_workflow_proof_signature)) ||
      !inspector_workflow_proof_signature.toString().contains(QStringLiteral("studio-inspector-workflow-proof:v1")) ||
      !inspector_workflow_proof_signature.toString().contains(QStringLiteral("|controls:current-tool-or-selected-object-only")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "studioInteractionResponsivenessProofSignature",
          Q_RETURN_ARG(QVariant, interaction_responsiveness_proof_signature)) ||
      !interaction_responsiveness_proof_signature.toString().contains(QStringLiteral("studio-interaction-responsiveness-proof:v1")) ||
      !interaction_responsiveness_proof_signature.toString().contains(QStringLiteral("|target-switch:preserve-unrelated-tool-state"))) {
    return fail("adjust inspector should keep Base Image as the internal global edit target");
  }
  trace_smoke_progress("studio professional ux proof checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_STUDIO_PROFESSIONAL_UX_PROOF")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    editor_preview.setImageProvider(nullptr);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return EXIT_SUCCESS;
  }
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("presets"));
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* preset_library_group = find_named_object(edit_workspace, QStringLiteral("presetLibraryInspectorGroup"));
  auto* studio_preset_search_field = find_named_object(edit_workspace, QStringLiteral("studioPresetSearchField"));
  auto* studio_preset_category_filter_row =
      find_named_object(edit_workspace, QStringLiteral("studioPresetCategoryFilterRow"));
  auto* studio_preset_pack_filter_combo =
      find_named_object(edit_workspace, QStringLiteral("studioPresetPackFilterCombo"));
  auto* studio_preset_favorites_filter_button =
      find_named_object(edit_workspace, QStringLiteral("studioPresetFavoritesFilterButton"));
  auto* studio_preset_library_list = find_named_object(edit_workspace, QStringLiteral("studioPresetLibraryList"));
  auto* studio_preset_pack_header = find_named_object(edit_workspace, QStringLiteral("studioPresetPackHeader"));
  auto* studio_preset_library_card = find_named_object(edit_workspace, QStringLiteral("studioPresetLibraryCard"));
  auto* studio_preset_favorite_button = find_named_object(edit_workspace, QStringLiteral("studioPresetFavoriteButton"));
  auto* studio_preset_apply_card_button =
      find_named_object(edit_workspace, QStringLiteral("studioPresetApplyFromCardButton"));
  auto* studio_preset_manage_menu_button =
      find_named_object(edit_workspace, QStringLiteral("studioPresetManageMenuButton"));
  auto* studio_preset_management_menu =
      find_named_object(edit_workspace, QStringLiteral("studioPresetManagementMenu"));
  auto* studio_preset_rename_button = find_named_object(edit_workspace, QStringLiteral("studioRenamePresetButton"));
  auto* studio_preset_move_button = find_named_object(edit_workspace, QStringLiteral("studioMovePresetButton"));
  auto* studio_preset_delete_button = find_named_object(edit_workspace, QStringLiteral("studioDeletePresetButton"));
  auto* studio_preset_export_field =
      find_named_object(edit_workspace, QStringLiteral("studioPresetExportFolderField"));
  auto* studio_preset_export_button =
      find_named_object(edit_workspace, QStringLiteral("studioExportPresetBundleButton"));
  auto* studio_preset_import_field =
      find_named_object(edit_workspace, QStringLiteral("studioPresetImportFolderField"));
  auto* studio_preset_import_button =
      find_named_object(edit_workspace, QStringLiteral("studioImportPresetBundleButton"));
  if (edit_inspector_panel->property("title").toString() != QStringLiteral("Presets") ||
      !presets_inspector_tab->property("active").toBool() ||
      preset_library_group == nullptr || !preset_library_group->property("visible").toBool() ||
      studio_preset_search_field == nullptr ||
      studio_preset_category_filter_row == nullptr || studio_preset_pack_filter_combo == nullptr ||
      studio_preset_favorites_filter_button == nullptr ||
      studio_preset_library_list == nullptr || studio_preset_pack_header == nullptr ||
      studio_preset_library_card == nullptr ||
      studio_preset_favorite_button == nullptr || studio_preset_apply_card_button == nullptr ||
      studio_preset_manage_menu_button == nullptr || studio_preset_management_menu == nullptr ||
      studio_preset_rename_button == nullptr || studio_preset_move_button == nullptr ||
      studio_preset_delete_button == nullptr || studio_preset_export_field == nullptr ||
      studio_preset_export_button == nullptr || studio_preset_import_field == nullptr ||
      studio_preset_import_button == nullptr ||
      preset_library_group->property("title").toString() != QStringLiteral("Presets") ||
      !studio_preset_library_list->property("clip").toBool() ||
      studio_preset_library_list->property("height").toDouble() < 240.0 ||
      studio_preset_library_card->property("height").toDouble() < 96.0 ||
      !studio_preset_favorite_button->property("square").toBool() ||
      studio_preset_apply_card_button->property("text").toString() != QStringLiteral("Apply") ||
      studio_preset_manage_menu_button->property("text").toString() != QStringLiteral("More") ||
      studio_preset_delete_button->property("role").toString() != QStringLiteral("destructive")) {
    return fail("preset library should live in a dedicated Presets inspector tab with filters, large cards, and management controls");
  }
  trace_smoke_progress("preset inspector checked");
  edit_workspace->setProperty("studioPresetCategoryFilter", QStringLiteral("Landscape"));
  edit_workspace->setProperty("studioPresetPackFilter", QStringLiteral("Landscape Core"));
  edit_workspace->setProperty("studioPresetFavoritesOnly", false);
  for (auto iteration = 0; iteration < 4; ++iteration) {
    QCoreApplication::processEvents();
  }
  QVariant filtered_preset_names;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "studioPresetLibraryNames",
          Q_RETURN_ARG(QVariant, filtered_preset_names))) {
    return fail("preset browser filters should expose filtered library names to QML");
  }
  const auto filtered_preset_list = filtered_preset_names.toStringList();
  if (!filtered_preset_list.contains(QStringLiteral("Landscape Pop")) ||
      filtered_preset_list.contains(QStringLiteral("Clean Portrait"))) {
    return fail("preset browser filters should narrow the library by category and pack");
  }
  edit_workspace->setProperty("studioPresetCategoryFilter", QStringLiteral("All"));
  edit_workspace->setProperty("studioPresetPackFilter", QStringLiteral("All"));
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  editor_preview.setCompareAdjustmentGroups(QStringList{});
  editor_preview.setExposure(0.42);
  editor_preview.setSaturation(17.0);
  QCoreApplication::processEvents();
  if (find_named_object(edit_workspace, QStringLiteral("adjustInlineHistogramScope")) != nullptr ||
      find_named_object(edit_workspace, QStringLiteral("adjustInlineWaveformScope")) != nullptr) {
    return fail("adjust inspector should not embed histogram or waveform scopes above exposure and color controls");
  }
  if (light_panel_before_after_button->property("iconName").toString() != QStringLiteral("compare") ||
      color_panel_before_after_button->property("iconName").toString() != QStringLiteral("compare") ||
      presence_panel_before_after_button->property("iconName").toString() != QStringLiteral("compare") ||
      !light_panel_before_after_button->property("square").toBool() ||
      light_panel_before_after_button->property("role").toString() != QStringLiteral("icon") ||
      !light_panel_before_after_button->property("visible").toBool() ||
      !color_panel_before_after_button->property("visible").toBool() ||
      !presence_panel_before_after_button->property("visible").toBool() ||
      light_panel_before_after_button->property("tooltip").toString().isEmpty()) {
    return fail("adjust inspector sections should expose compact before/after controls");
  }
  if (!QMetaObject::invokeMethod(light_panel_before_after_button, "click")) {
    return fail("light before/after section button should be clickable");
  }
  QCoreApplication::processEvents();
  if (!editor_preview.compareAdjustmentGroups().contains(QStringLiteral("tone")) ||
      editor_preview.compareAdjustmentGroups().contains(QStringLiteral("color")) ||
      !light_panel_before_after_button->property("active").toBool() ||
      std::abs(editor_preview.exposure() - 0.42) > 1e-6 ||
      std::abs(editor_preview.saturation() - 17.0) > 1e-6) {
    return fail("section before/after toggles should isolate compare groups without mutating adjustment values");
  }
  if (!QMetaObject::invokeMethod(color_panel_before_after_button, "click")) {
    return fail("color before/after section button should be clickable");
  }
  QCoreApplication::processEvents();
  if (!editor_preview.compareAdjustmentGroups().contains(QStringLiteral("tone")) ||
      !editor_preview.compareAdjustmentGroups().contains(QStringLiteral("color")) ||
      !color_panel_before_after_button->property("active").toBool()) {
    return fail("section before/after toggles should allow multiple explicit compare groups");
  }
  if (!QMetaObject::invokeMethod(light_panel_before_after_button, "click")) {
    return fail("light before/after section button should toggle off");
  }
  QCoreApplication::processEvents();
  if (editor_preview.compareAdjustmentGroups().contains(QStringLiteral("tone")) ||
      !editor_preview.compareAdjustmentGroups().contains(QStringLiteral("color")) ||
      light_panel_before_after_button->property("active").toBool()) {
    return fail("section before/after toggles should remove only their own compare group");
  }
  editor_preview.setCompareAdjustmentGroups(QStringList{});
  editor_preview.setExposure(0.0);
  editor_preview.setSaturation(0.0);
  QCoreApplication::processEvents();
  QVariant adjustment_filter_signature;
  edit_workspace->setProperty("editAdjustmentFilterText", QStringLiteral("dehaze"));
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editAdjustmentFilterSignature",
          Q_RETURN_ARG(QVariant, adjustment_filter_signature)) ||
      adjustment_filter_signature.toString() != QStringLiteral("query:dehaze|intent:all|count:1|groups:presence") ||
      edit_adjustment_filter_field->property("text").toString() != QStringLiteral("dehaze") ||
      !edit_adjustment_filter_status_label->property("visible").toBool() ||
      !edit_adjustment_filter_status_label->property("text").toString().contains(QStringLiteral("1 matching")) ||
      !clear_edit_adjustment_filter_button->property("visible").toBool() ||
      white_balance_group->property("visible").toBool() ||
      light_group->property("visible").toBool() ||
      tone_curve_group->property("visible").toBool() ||
      color_group->property("visible").toBool() ||
      !presence_group->property("visible").toBool() ||
      edit_workspace->property("editInspectorTab").toString() != QStringLiteral("adjust")) {
    return fail("adjust inspector filter should narrow visible adjustment groups without changing the active tab");
  }
  edit_workspace->setProperty("editAdjustmentFilterText", QStringLiteral("channel"));
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editAdjustmentFilterSignature",
          Q_RETURN_ARG(QVariant, adjustment_filter_signature)) ||
      adjustment_filter_signature.toString() !=
          QStringLiteral("query:channel|intent:all|count:1|groups:advanced-color") ||
      !advanced_color_group->property("visible").toBool() ||
      presence_group->property("visible").toBool()) {
    return fail("adjust inspector filter should find advanced color controls by contained capability names");
  }
  edit_workspace->setProperty("editAdjustmentFilterText", QStringLiteral("grading"));
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editAdjustmentFilterSignature",
          Q_RETURN_ARG(QVariant, adjustment_filter_signature)) ||
      adjustment_filter_signature.toString() !=
          QStringLiteral("query:grading|intent:all|count:1|groups:color-grading") ||
      !color_grading_group->property("visible").toBool() ||
      advanced_color_group->property("visible").toBool()) {
    return fail("adjust inspector filter should isolate Color Grading from specialist color-mix controls");
  }
  QVariant raw_availability_signature;
  QVariant disabled_capability_signature;
  edit_workspace->setProperty("editAdjustmentFilterText", QStringLiteral("raw"));
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editAdjustmentFilterSignature",
          Q_RETURN_ARG(QVariant, adjustment_filter_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "rawControlAvailabilitySignature",
          Q_RETURN_ARG(QVariant, raw_availability_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "disabledCapabilityReasonSignature",
          Q_RETURN_ARG(QVariant, disabled_capability_signature)) ||
      editor_preview.rawControlsAvailable() ||
      adjustment_filter_signature.toString() != QStringLiteral("query:raw|intent:all|count:0|groups:") ||
      raw_availability_signature.toString() !=
          QStringLiteral("available:false|rawGroups:0|groupVisible:false") ||
      !edit_adjustment_filter_status_label->property("text").toString().contains(QStringLiteral("RAW source")) ||
      !disabled_capability_signature.toString().contains(QStringLiteral("raw:disabled:RAW controls require")) ||
      !disabled_capability_signature.toString().contains(QStringLiteral("profile-camera-input:disabled:Camera input profiles require RAW data")) ||
      (editor_preview.aiSmartSelectionAvailable()
           ? !disabled_capability_signature.toString().contains(QStringLiteral("model-smart-selection:enabled"))
           : !disabled_capability_signature.toString().contains(
                  QStringLiteral("model-smart-selection:disabled:Model-backed smart selection is not active in this build; %1 is active")
                      .arg(editor_preview.smartSelectionRuntimeLabel()))) ||
      (editor_preview.aiInpaintAvailable()
           ? !disabled_capability_signature.toString().contains(QStringLiteral("model-inpaint:enabled"))
           : !disabled_capability_signature.toString().contains(
                  QStringLiteral("model-inpaint:disabled:LaMa Model Inpaint is not active in this build; retouch will use %1")
                      .arg(editor_preview.inpaintRuntimeLabel()))) ||
      raw_group->property("visible").toBool()) {
    return fail("non-RAW assets should hide RAW controls and explain disabled RAW/profile plus recoverable model capabilities");
  }
  if (studio_workspace_source.contains(QStringLiteral("SAM Smart Select is unavailable"))) {
    return fail("production UI should not expose SAM-ready wording when the SAM ONNX path is not implemented");
  }
  QVariant mask_runtime_trust_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "maskRuntimeTrustSignature",
          Q_RETURN_ARG(QVariant, mask_runtime_trust_signature)) ||
      !mask_runtime_trust_signature.toString().contains(
          QStringLiteral("selection:%1").arg(editor_preview.smartSelectionRuntimeLabel())) ||
      !mask_runtime_trust_signature.toString().contains(
          QStringLiteral("inpaint:%1").arg(editor_preview.inpaintRuntimeLabel())) ||
      !mask_runtime_trust_signature.toString().contains(QStringLiteral("diagnostics:root:")) ||
      !mask_runtime_trust_signature.toString().contains(QStringLiteral("|sam:")) ||
      !mask_runtime_trust_signature.toString().contains(QStringLiteral("|lama:")) ||
      !mask_runtime_trust_signature.toString().contains(QStringLiteral("|onnx:")) ||
      !mask_runtime_trust_signature.toString().contains(QStringLiteral("|vision:"))) {
    return fail("mask runtime trust signature should expose honest selection/inpaint labels and model diagnostics");
  }
  edit_workspace->setProperty("editAdjustmentFilterText", QString());
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editAdjustmentFilterSignature",
          Q_RETURN_ARG(QVariant, adjustment_filter_signature)) ||
      adjustment_filter_signature.toString() !=
          QStringLiteral("query:|intent:all|count:11|groups:white-balance,light,levels,tone-curve,color,color-mix,color-grading,advanced-color,presence,detail,lens-transform") ||
      !white_balance_group->property("visible").toBool() ||
      !light_group->property("visible").toBool() ||
      !tone_curve_group->property("visible").toBool() ||
      !color_group->property("visible").toBool() ||
      !presence_group->property("visible").toBool() ||
      !adjust_detail_group->property("visible").toBool() ||
      !adjust_lens_group->property("visible").toBool() ||
      !advanced_color_group->property("visible").toBool()) {
    return fail("clearing the adjust inspector filter should restore the full adjustment stack");
  }
  trace_smoke_progress("adjust filter restored");
  for (auto* footer_child : edit_inspector_footer->findChildren<QObject*>()) {
    if (footer_child->property("text").toString() == QStringLiteral("Export")) {
      return fail("edit inspector footer should contain only edit-local actions, not export navigation");
    }
  }
  auto* app_navigation_bar = find_named_item(root, QStringLiteral("appNavigationBar"));
  const auto app_activity_error_visible = root->property("appActivityErrorVisible").toBool();
  const auto recovery_banner_visible = root->property("recoveryBannerVisible").toBool();
  const auto status_row_visible = app_activity_error_visible || recovery_banner_visible;
  auto* initial_activity_indicator = find_named_item(
      root,
      recovery_banner_visible
          ? QStringLiteral("recoveryStatusIndicator")
          : app_activity_error_visible
                ? QStringLiteral("failureActivityIndicator")
                : QStringLiteral("floatingActivityIndicator"));
  auto* initial_edit_inspector = qobject_cast<QQuickItem*>(edit_inspector_panel);
  auto* initial_edit_footer = qobject_cast<QQuickItem*>(edit_inspector_footer);
  const auto activity_indicator_rect = scene_rect(initial_activity_indicator);
  const auto navigation_rect = scene_rect(app_navigation_bar);
  if (initial_activity_indicator == nullptr || app_navigation_bar == nullptr ||
      (status_row_visible &&
       (!item_visible(initial_activity_indicator) || activity_indicator_rect.width() <= 0.0 ||
        activity_indicator_rect.height() <= 0.0 ||
        activity_indicator_rect.top() + 1.0 < navigation_rect.bottom())) ||
      (!status_row_visible && initial_activity_indicator->height() > 0.0) ||
      initial_edit_filmstrip == nullptr ||
      !item_visible(initial_edit_inspector) || !item_visible(initial_edit_footer)) {
    return fail("activity status should sit directly beneath navigation without occupying space while idle");
  }
  if (light_basic_tone_group == nullptr || light_advanced_tone_group == nullptr ||
      levels_inspector_group == nullptr || light_zone_mapping_group == nullptr ||
      tone_curve_input_levels_group != nullptr || tone_curve_input_levels_strip == nullptr ||
      tone_curve_input_levels_histogram == nullptr || tone_curve_input_levels_handle == nullptr ||
      light_rolloff_detail_reset_button == nullptr || light_zone_mixer_reset_button == nullptr ||
      tone_curve_input_levels_reset_button == nullptr ||
      light_basic_tone_group->property("title").toString() != QStringLiteral("Basic Tone") ||
      light_advanced_tone_group->property("title").toString() != QStringLiteral("Roll-off & Detail") ||
      light_zone_mapping_group->property("title").toString() != QStringLiteral("Zone Mixer") ||
      levels_inspector_group->property("title").toString() != QStringLiteral("Levels") ||
      !light_advanced_tone_group->property("resetVisible").toBool() ||
      !light_zone_mapping_group->property("resetVisible").toBool() ||
      !levels_inspector_group->property("resetVisible").toBool() ||
      !light_basic_tone_group->property("expanded").toBool() ||
      light_advanced_tone_group->property("expanded").toBool() ||
      light_zone_mapping_group->property("expanded").toBool() ||
      !levels_inspector_group->property("visible").toBool() ||
      !(light_basic_tone_group->property("y").toDouble() <
        light_advanced_tone_group->property("y").toDouble() &&
        light_advanced_tone_group->property("y").toDouble() <
        light_zone_mapping_group->property("y").toDouble())) {
    return fail("Levels should be a standalone inspector section with its own reset affordance");
  }
  edit_workspace->setProperty("lightAdvancedToneExpanded", true);
  edit_workspace->setProperty("lightZonesExpanded", true);
  QCoreApplication::processEvents();
  if (!light_advanced_tone_group->property("expanded").toBool() ||
      !light_zone_mapping_group->property("expanded").toBool()) {
    return fail("advanced tone and zone groups should expand through explicit workspace state");
  }
  edit_workspace->setProperty("lightAdvancedToneExpanded", false);
  edit_workspace->setProperty("lightZonesExpanded", false);
  QCoreApplication::processEvents();
  if (advanced_color_group == nullptr || color_mix_group == nullptr || channel_mixer_group == nullptr ||
      channel_mixer_red_sum_label == nullptr || channel_mixer_green_sum_label == nullptr ||
      channel_mixer_blue_sum_label == nullptr ||
      color_mix_active_preview == nullptr || color_mix_active_swatch == nullptr ||
      color_mix_active_label == nullptr || color_mix_active_values == nullptr ||
      color_mix_reset_channel_button == nullptr || color_mix_affected_range_strip == nullptr ||
      color_mix_affected_range_swatch == nullptr ||
      black_white_group == nullptr || color_replace_group == nullptr ||
      color_replace_swatch_preview == nullptr || color_replace_source_swatch == nullptr ||
      color_replace_target_swatch == nullptr || color_replace_summary_label == nullptr ||
      color_replace_selection_label == nullptr || color_replace_swap_button == nullptr ||
      color_replace_mask_preview_button == nullptr ||
      color_replace_auto_skin_button == nullptr || color_replace_reset_skin_button == nullptr ||
      color_replace_preserve_luminance_check == nullptr ||
      color_replace_preserve_saturation_check == nullptr ||
      skin_tone_group == nullptr ||
      harmony_group == nullptr || harmony_palette_preview == nullptr || harmony_anchor_swatch == nullptr ||
      harmony_affected_range_strip == nullptr || harmony_affected_range_swatch == nullptr ||
      harmony_warm_preset_button == nullptr || harmony_complement_preset_button == nullptr ||
      harmony_triad_preset_button == nullptr || harmony_base_hue_slider == nullptr ||
      harmony_amount_slider == nullptr || split_toning_group == nullptr ||
      hsl_channel_button_row == nullptr || hsl_reds_button == nullptr || hsl_magentas_button == nullptr ||
      advanced_color_group->property("title").toString() != QStringLiteral("Advanced Color") ||
      color_mix_group->property("title").toString() != QStringLiteral("Color Mix") ||
      !color_mix_affected_range_strip->property("visible").toBool() ||
      channel_mixer_group->property("title").toString() != QStringLiteral("Channel Mixer") ||
      channel_mixer_group->property("visible").toBool() ||
      channel_mixer_red_sum_label->property("text").toString() != QStringLiteral("Sum 100") ||
      channel_mixer_green_sum_label->property("text").toString() != QStringLiteral("Sum 100") ||
      channel_mixer_blue_sum_label->property("text").toString() != QStringLiteral("Sum 100") ||
      black_white_group->property("title").toString() != QStringLiteral("Black & White") ||
      color_replace_group->property("title").toString() != QStringLiteral("Color Replace") ||
      skin_tone_group->property("title").toString() != QStringLiteral("Skin Protection Lab") ||
      harmony_group->property("title").toString() != QStringLiteral("Harmony Lab") ||
      split_toning_group->property("title").toString() != QStringLiteral("Split Toning") ||
      harmony_group->property("visible").toBool() ||
      harmony_warm_preset_button->property("text").toString() != QStringLiteral("Warm Analogous") ||
      harmony_complement_preset_button->property("text").toString() != QStringLiteral("Soft Complement") ||
      harmony_triad_preset_button->property("text").toString() != QStringLiteral("Cool Triad") ||
      harmony_amount_slider->property("label").toString() != QStringLiteral("Amount") ||
      !color_mix_group->property("expanded").toBool() ||
      channel_mixer_group->property("expanded").toBool() ||
      black_white_group->property("expanded").toBool() ||
      color_replace_group->property("expanded").toBool() ||
      color_replace_mask_preview_button->property("active").toBool() ||
      color_replace_auto_skin_button->property("text").toString() != QStringLiteral("Auto Skin (Heuristic)") ||
      !color_replace_auto_skin_button->property("tooltip").toString().contains(QStringLiteral("color heuristic")) ||
      color_replace_reset_skin_button->property("text").toString() != QStringLiteral("Reset Skin") ||
      !color_replace_preserve_luminance_check->property("checked").toBool() ||
      !color_replace_preserve_saturation_check->property("checked").toBool() ||
      skin_tone_group->property("visible").toBool() ||
      skin_tone_group->property("expanded").toBool() ||
      harmony_group->property("expanded").toBool() ||
      !split_toning_group->property("expanded").toBool()) {
    return fail("advanced color inspector should keep color mix and color grading available while collapsing deeper specialist color tools by default");
  }
  QVariant hsl_reds_signature;
  QVariant hsl_magentas_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "hslChannelControlSignature",
          Q_RETURN_ARG(QVariant, hsl_reds_signature),
          Q_ARG(QVariant, QStringLiteral("Reds"))) ||
      hsl_reds_signature.toString() !=
          QStringLiteral("Reds|label:Red|tooltip:Reds color mix channel.") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "hslChannelControlSignature",
          Q_RETURN_ARG(QVariant, hsl_magentas_signature),
          Q_ARG(QVariant, QStringLiteral("Magentas"))) ||
      hsl_magentas_signature.toString() !=
          QStringLiteral("Magentas|label:Mag|tooltip:Magentas color mix channel.") ||
      hsl_reds_button->property("text").toString() != QStringLiteral("Red") ||
      hsl_magentas_button->property("text").toString() != QStringLiteral("Mag") ||
      hsl_reds_button->property("iconName").toString() != QStringLiteral("swatch") ||
      hsl_magentas_button->property("iconName").toString() != QStringLiteral("swatch") ||
      hsl_reds_button->property("tooltip").toString().isEmpty() ||
      hsl_magentas_button->property("tooltip").toString().isEmpty()) {
    return fail("HSL channel controls should use readable labels, swatch icons, and tooltips instead of ambiguous one-letter buttons");
  }
  editor_preview.setColorMixerModel(QStringLiteral("HSL"));
  editor_preview.setHslChannel(QStringLiteral("Blues"));
  editor_preview.setHueAdjustment(22.0);
  editor_preview.setSaturationAdjustment(-14.0);
  editor_preview.setLuminanceAdjustment(9.0);
  QCoreApplication::processEvents();
  QVariant color_mix_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "colorMixActiveChannelSignature",
          Q_RETURN_ARG(QVariant, color_mix_signature)) ||
      color_mix_signature.toString() != QStringLiteral("HSL|Blues|h:22|s:-14|l:9") ||
      color_mix_active_label->property("text").toString() != QStringLiteral("Blues / HSL") ||
      !color_mix_reset_channel_button->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(color_mix_reset_channel_button, "click")) {
    return fail("color mix should show the active channel and expose a reset action for selected HSL adjustments");
  }
  QCoreApplication::processEvents();
  if (
      std::abs(editor_preview.hueAdjustment()) > 1e-6 ||
      std::abs(editor_preview.saturationAdjustment()) > 1e-6 ||
      std::abs(editor_preview.luminanceAdjustment()) > 1e-6) {
    return fail("color mix reset should clear the selected HSL channel adjustments");
  }
  editor_preview.setColorReplacementSourceHue(0.10);
  editor_preview.setColorReplacementTargetHue(0.55);
  editor_preview.setColorReplacementRange(42.0);
  editor_preview.setColorReplacementSoftness(70.0);
  editor_preview.setColorReplacementStrength(80.0);
  QCoreApplication::processEvents();
  QVariant color_replace_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "colorReplaceSignature",
      Q_RETURN_ARG(QVariant, color_replace_signature)) ||
      color_replace_signature.toString() != QStringLiteral("source:36|target:198|range:42|softness:70|strength:80|preserveLuma:true|preserveSat:true") ||
      color_replace_summary_label->property("text").toString() != QStringLiteral("36° -> 198°") ||
      !QMetaObject::invokeMethod(color_replace_swap_button, "click")) {
    return fail("color replace should expose source/target swatches, readable selection state, and a hue swap action");
  }
  QCoreApplication::processEvents();
  if (std::abs(editor_preview.colorReplacementSourceHue() - 0.55) > 1e-6 ||
      std::abs(editor_preview.colorReplacementTargetHue() - 0.10) > 1e-6) {
    return fail("color replace swap should exchange source and target hues");
  }
  QVariant applied_harmony_preset;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "applyHarmonyPreset",
          Q_RETURN_ARG(QVariant, applied_harmony_preset),
          Q_ARG(QVariant, QStringLiteral("Cool Triad"))) ||
      !applied_harmony_preset.toBool() ||
      !editor_preview.colorHarmonyEnabled() ||
      editor_preview.colorHarmonyMode() != QStringLiteral("Triadic") ||
      std::abs(editor_preview.colorHarmonyBaseHue() - 0.56) > 1e-6 ||
      std::abs(editor_preview.colorHarmonyStrength() - 32.0) > 1e-6) {
    return fail("harmony lab should expose named palette presets that configure mode, base hue, and amount");
  }
  edit_workspace->setProperty("labColorToolsVisible", true);
  edit_workspace->setProperty("channelMixerExpanded", true);
  edit_workspace->setProperty("blackWhiteExpanded", true);
  edit_workspace->setProperty("colorReplaceExpanded", true);
  edit_workspace->setProperty("skinToneExpanded", true);
  edit_workspace->setProperty("harmonyExpanded", true);
  edit_workspace->setProperty("splitToningExpanded", true);
  QCoreApplication::processEvents();
  if (!channel_mixer_group->property("visible").toBool() ||
      !skin_tone_group->property("visible").toBool() ||
      !harmony_group->property("visible").toBool() ||
      !harmony_affected_range_strip->property("visible").toBool() ||
      !channel_mixer_group->property("expanded").toBool() ||
      !black_white_group->property("expanded").toBool() ||
      !color_replace_group->property("expanded").toBool() ||
      !skin_tone_group->property("expanded").toBool() ||
      !harmony_group->property("expanded").toBool() ||
      !split_toning_group->property("expanded").toBool()) {
    return fail("specialist color tools should become visible in Lab and expand through explicit workspace state");
  }
  trace_smoke_progress("specialist color tools checked");
  edit_workspace->setProperty("labColorToolsVisible", false);
  edit_workspace->setProperty("channelMixerExpanded", false);
  edit_workspace->setProperty("blackWhiteExpanded", false);
  edit_workspace->setProperty("colorReplaceExpanded", false);
  edit_workspace->setProperty("skinToneExpanded", false);
  edit_workspace->setProperty("harmonyExpanded", false);
  edit_workspace->setProperty("splitToningExpanded", false);
  QCoreApplication::processEvents();
  auto* tone_curve_editor = edit_workspace->findChild<QObject*>(QStringLiteral("studioToneCurveEditor"));
  auto* tone_curve_delete_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("studioToneCurveEditorDeletePointButton"));
  auto* tone_curve_reset_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("studioToneCurveEditorResetCurveButton"));
  auto* tone_curve_mode_badge =
      edit_workspace->findChild<QObject*>(QStringLiteral("studioToneCurveEditorChannelModeBadge"));
  if (tone_curve_editor == nullptr || tone_curve_delete_button == nullptr ||
      tone_curve_reset_button == nullptr || tone_curve_mode_badge == nullptr) {
    return fail("tone curve editor should expose visible channel, delete, and reset controls");
  }
  editor_preview.setCurveChannel(QStringLiteral("RGB"));
  editor_preview.setCurveShadows(18.0);
  QCoreApplication::processEvents();
  if (editor_preview.activeCurveMode() != QStringLiteral("Parametric") ||
      tone_curve_editor->property("channelModeLabel").toString() != QStringLiteral("RGB Parametric")) {
    return fail("tone curve editor should identify RGB parametric mode before point editing takes over");
  }
  editor_preview.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.48}, {QStringLiteral("y"), 0.56}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  });
  QCoreApplication::processEvents();
  if (editor_preview.activeCurveMode() != QStringLiteral("Point") ||
      tone_curve_editor->property("channelModeLabel").toString() != QStringLiteral("RGB Point") ||
      std::abs(editor_preview.curveShadows()) > 1e-6) {
    return fail("tone curve editor should identify point mode after drawing a curve and clear RGB parametric controls");
  }
  editor_preview.setCurveChannel(QStringLiteral("Red"));
  editor_preview.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.42}, {QStringLiteral("y"), 0.62}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  });
  QCoreApplication::processEvents();
  QVariant selected_curve_index;
  if (tone_curve_editor->property("channelModeLabel").toString() != QStringLiteral("Red Point") ||
      tone_curve_editor->property("pointCount").toInt() != 3 ||
      tone_curve_editor->property("histogramPrimaryFillOpacity").toDouble() > 0.10 ||
      tone_curve_editor->property("histogramPrimaryStrokeOpacity").toDouble() > 0.28 ||
      tone_curve_editor->property("histogramRgbFillOpacity").toDouble() > 0.04 ||
      tone_curve_editor->property("histogramRgbStrokeOpacity").toDouble() > 0.12 ||
      !QMetaObject::invokeMethod(
          tone_curve_editor,
          "testSelectPoint",
          Q_RETURN_ARG(QVariant, selected_curve_index),
          Q_ARG(QVariant, QVariant(1))) ||
      selected_curve_index.toInt() != 1 ||
      !tone_curve_editor->property("selectedPointDeletable").toBool() ||
      !tone_curve_delete_button->property("enabled").toBool()) {
    return fail("tone curve editor should make channel mode, selected point deletion, and quiet histogram opacity testable");
  }
  QVariant deleted_curve_point;
  if (!QMetaObject::invokeMethod(
          tone_curve_editor,
          "testDeleteSelectedPoint",
          Q_RETURN_ARG(QVariant, deleted_curve_point)) ||
      !deleted_curve_point.toBool()) {
    return fail("tone curve editor should delete the selected interior point through an explicit control path");
  }
  QCoreApplication::processEvents();
  if (editor_preview.activeCurvePoints().size() != 2) {
    return fail("tone curve selected-point delete should update the active channel curve");
  }
  QVariant reset_curve;
  editor_preview.setActiveCurvePoints(QVariantList{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.16}},
      QVariantMap{{QStringLiteral("x"), 0.55}, {QStringLiteral("y"), 0.44}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 0.92}},
  });
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          tone_curve_editor,
          "testResetActiveCurve",
          Q_RETURN_ARG(QVariant, reset_curve)) ||
      !reset_curve.toBool()) {
    return fail("tone curve editor should reset the active curve through an explicit control path");
  }
  QCoreApplication::processEvents();
  if (editor_preview.activeCurvePoints().size() != 2 ||
      !tone_curve_reset_button->property("enabled").toBool()) {
    return fail("tone curve reset should restore the active channel to the default curve");
  }
  QVariant previewed_curve_preset;
  if (!QMetaObject::invokeMethod(
          tone_curve_editor,
          "testPreviewPreset",
          Q_RETURN_ARG(QVariant, previewed_curve_preset),
          Q_ARG(QVariant, QVariant(QStringLiteral("Filmic S")))) ||
      !previewed_curve_preset.toBool() ||
      tone_curve_editor->property("previewPointsModel").toList().size() < 5 ||
      editor_preview.activeCurvePoints().size() != 2) {
    return fail("tone curve preset preview should show preset points without mutating the active curve");
  }
  QVariant cleared_curve_preview;
  if (!QMetaObject::invokeMethod(
          tone_curve_editor,
          "testClearPresetPreview",
          Q_RETURN_ARG(QVariant, cleared_curve_preview)) ||
      !cleared_curve_preview.toBool() ||
      !tone_curve_editor->property("previewPointsModel").toList().isEmpty() ||
      editor_preview.activeCurvePoints().size() != 2) {
    return fail("tone curve preset preview should be reversible without changing the recipe");
  }
  trace_smoke_progress("tone curve editor checked");
  auto* brightness_slider = edit_workspace->findChild<QObject*>(QStringLiteral("brightnessSlider"));
  auto* tint_slider = edit_workspace->findChild<QObject*>(QStringLiteral("tintSlider"));
  if (brightness_slider == nullptr || tint_slider == nullptr ||
      std::abs(brightness_slider->property("defaultPosition").toDouble() - 0.5) > 1e-6 ||
      std::abs(tint_slider->property("defaultPosition").toDouble() - 0.5) > 1e-6) {
    return fail("signed adjustment sliders should expose a centered neutral/default position");
  }
  editor_preview.setBrightness(40.0);
  QCoreApplication::processEvents();
  if (brightness_slider->property("valueText").toString() != QStringLiteral("+40%") ||
      brightness_slider->property("normalizedValue").toDouble() <= 0.5 ||
      std::abs(brightness_slider->property("fillStartPosition").toDouble() - 0.5) > 1e-6 ||
      brightness_slider->property("fillEndPosition").toDouble() <= 0.5) {
    return fail("positive signed adjustments should show a plus percent label and fill right from neutral");
  }
  editor_preview.setBrightness(-40.0);
  QCoreApplication::processEvents();
  if (brightness_slider->property("valueText").toString() != QStringLiteral("-40%") ||
      brightness_slider->property("normalizedValue").toDouble() >= 0.5 ||
      brightness_slider->property("fillStartPosition").toDouble() >= 0.5 ||
      std::abs(brightness_slider->property("fillEndPosition").toDouble() - 0.5) > 1e-6) {
    return fail("negative signed adjustments should show a minus percent label and fill left from neutral");
  }
  editor_preview.setBrightness(0.0);
  editor_preview.setTint(25.0);
  QCoreApplication::processEvents();
  if (tint_slider->property("valueText").toString() != QStringLiteral("+25%") ||
      tint_slider->property("trackColorCount").toInt() < 2) {
    return fail("color signed adjustments should use signed percent text without losing directional track colors");
  }
  editor_preview.setTint(0.0);
  QCoreApplication::processEvents();
  auto* exposure_slider = edit_workspace->findChild<QObject*>(QStringLiteral("exposureSlider"));
  auto* exposure_value_field = edit_workspace->findChild<QObject*>(QStringLiteral("exposureSliderValueField"));
  auto* exposure_modified_marker = edit_workspace->findChild<QObject*>(QStringLiteral("exposureSliderModifiedMarker"));
  auto* exposure_reset_button = edit_workspace->findChild<QObject*>(QStringLiteral("exposureSliderResetButton"));
  auto* temperature_slider = edit_workspace->findChild<QObject*>(QStringLiteral("temperatureSlider"));
  auto* temperature_value_field = edit_workspace->findChild<QObject*>(QStringLiteral("temperatureSliderValueField"));
  auto* channel_mixer_red_slider = edit_workspace->findChild<QObject*>(QStringLiteral("channelMixerRedFromRedSlider"));
  auto* color_replacement_hue_slider =
      edit_workspace->findChild<QObject*>(QStringLiteral("colorReplacementTargetHueSlider"));
  auto* selected_tone_hue_slider =
      edit_workspace->findChild<QObject*>(QStringLiteral("selectedToneWheelHueSlider"));
  auto* split_toning_shadows_wheel =
      find_named_object(edit_workspace, QStringLiteral("splitToningShadowsWheel"));
  auto* split_toning_midtones_wheel =
      find_named_object(edit_workspace, QStringLiteral("splitToningMidtonesWheel"));
  auto* split_toning_highlights_wheel =
      find_named_object(edit_workspace, QStringLiteral("splitToningHighlightsWheel"));
  auto* split_toning_focused_wheel =
      find_named_object(edit_workspace, QStringLiteral("splitToningFocusedWheel"));
  auto* grading_amount_slider = edit_workspace->findChild<QObject*>(QStringLiteral("colorGradingAmountSlider"));
  auto* grading_balance_slider = edit_workspace->findChild<QObject*>(QStringLiteral("colorGradingBalanceSlider"));
  auto* selected_tone_saturation_slider =
      edit_workspace->findChild<QObject*>(QStringLiteral("selectedToneWheelSaturationSlider"));
  auto* selected_tone_luminance_slider =
      edit_workspace->findChild<QObject*>(QStringLiteral("selectedToneWheelLuminanceSlider"));
  auto* liquify_radius_slider = edit_workspace->findChild<QObject*>(QStringLiteral("liquifyRadiusSlider"));
  QVariant committed_input_value;
  if (exposure_slider == nullptr || exposure_value_field == nullptr ||
      exposure_modified_marker != nullptr || exposure_reset_button == nullptr ||
      temperature_slider == nullptr ||
      temperature_value_field == nullptr || !exposure_value_field->property("enabled").toBool() ||
      !temperature_value_field->property("enabled").toBool()) {
    return fail("precise exposure entry should expose enabled typed EV fields and a fixed reset affordance");
  }
  const auto exposure_slider_contract = exposure_slider->property("sliderContract").toString();
  const auto exposure_studio_slider_contract =
      exposure_slider->property("studioAdjustmentSliderContract").toString();
  const auto exposure_studio_slider_signature =
      exposure_slider->property("studioAdjustmentSliderSignature").toString();
  if (!exposure_slider_contract.contains(QStringLiteral("inspector-slider:v4")) ||
      !exposure_slider_contract.contains(QStringLiteral("wide-rail:true")) ||
      !exposure_slider_contract.contains(QStringLiteral("value-column:68")) ||
      !exposure_slider_contract.contains(QStringLiteral("reset-column:28")) ||
      !exposure_slider_contract.contains(QStringLiteral("row-height:62")) ||
      !exposure_slider_contract.contains(QStringLiteral("track:3")) ||
      !exposure_slider_contract.contains(QStringLiteral("thumb:12-16")) ||
      !exposure_slider_contract.contains(QStringLiteral("reserved-thumb:16")) ||
      !exposure_slider_contract.contains(QStringLiteral("typed-entry:true")) ||
      !exposure_slider_contract.contains(QStringLiteral("modified-marker:false")) ||
      !exposure_slider_contract.contains(QStringLiteral("fixed-reset:true")) ||
      !exposure_slider_contract.contains(QStringLiteral("stable-value:true")) ||
      !exposure_slider_contract.contains(QStringLiteral("label-layout:fluid")) ||
      !exposure_slider_contract.contains(QStringLiteral("disabled-reason:true")) ||
      !exposure_slider_contract.contains(QStringLiteral("no-elide:true")) ||
      !exposure_slider_contract.contains(QStringLiteral("double-click-reset:true")) ||
      !exposure_slider_contract.contains(QStringLiteral("no-handle-layout-shift:true")) ||
      !exposure_studio_slider_contract.contains(QStringLiteral("studio-adjustment-slider:v2")) ||
      !exposure_studio_slider_contract.contains(QStringLiteral("shared-inspector-slider:true")) ||
      !exposure_studio_slider_contract.contains(QStringLiteral("coalesced-drag:true")) ||
      !exposure_studio_slider_contract.contains(QStringLiteral("target-aware:true")) ||
      !exposure_studio_slider_contract.contains(QStringLiteral("disabled-reason:true")) ||
      !exposure_studio_slider_signature.contains(QStringLiteral("|target-support:base-image,adjustment-layer|")) ||
      !exposure_studio_slider_signature.contains(QStringLiteral("|target-supported:true|")) ||
      !exposure_studio_slider_signature.contains(QStringLiteral("|slider:inspector-slider:v4")) ||
      !exposure_studio_slider_signature.contains(QStringLiteral("|target:adjustment-control-target-support:v1"))) {
    std::cerr << "Exposure slider contracts: slider="
              << exposure_slider_contract.toStdString()
              << " studio=" << exposure_studio_slider_contract.toStdString()
              << " signature=" << exposure_studio_slider_signature.toStdString()
              << '\n';
    return fail("shared inspector sliders should expose professional row height, stable value/reset columns, typed entry, modified state, and no-elide behavior");
  }
  if (!QMetaObject::invokeMethod(
          exposure_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("+1.37 EV")))) {
    return fail("precise exposure entry should accept typed EV values through the slider value field");
  }
  QCoreApplication::processEvents();
  if (
      std::abs(editor_preview.exposure() - 1.37) > 1e-6 ||
      exposure_slider->property("valueText").toString() != QStringLiteral("+1.37 EV") ||
      !exposure_slider->property("modified").toBool()) {
    return fail("precise exposure entry should accept typed EV values through the slider value field");
  }
  if (!QMetaObject::invokeMethod(
          temperature_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("6500K"))) ||
      std::abs(editor_preview.temperature() - ((6500.0 - 5500.0) / (12000.0 - 5500.0) * 100.0)) > 1e-6 ||
      temperature_slider->property("valueText").toString() != QStringLiteral("6500K")) {
    return fail("white balance temperature entry should accept typed Kelvin values through the slider value field");
  }
  if (color_replacement_hue_slider == nullptr || selected_tone_hue_slider == nullptr ||
      split_toning_shadows_wheel == nullptr || split_toning_midtones_wheel == nullptr ||
      split_toning_highlights_wheel == nullptr ||
      liquify_radius_slider == nullptr ||
      !QMetaObject::invokeMethod(
          color_replacement_hue_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("180°"))) ||
      std::abs(editor_preview.colorReplacementTargetHue() - 0.5) > 1e-6 ||
      color_replacement_hue_slider->property("valueText").toString() != QStringLiteral("180°") ||
      color_replacement_hue_slider->property("valueInputMode").toString() !=
          QStringLiteral("degreesNormalized")) {
    std::cerr << "Color hue typed state: replacement="
              << (color_replacement_hue_slider == nullptr ? "null" : "present")
              << " selectedToneHue=" << (selected_tone_hue_slider == nullptr ? "null" : "present")
              << " shadowsWheel=" << (split_toning_shadows_wheel == nullptr ? "null" : "present")
              << " midtonesWheel=" << (split_toning_midtones_wheel == nullptr ? "null" : "present")
              << " highlightsWheel=" << (split_toning_highlights_wheel == nullptr ? "null" : "present")
              << " liquify=" << (liquify_radius_slider == nullptr ? "null" : "present")
              << " replacementHue=" << editor_preview.colorReplacementTargetHue()
              << " valueText="
              << (color_replacement_hue_slider == nullptr
                      ? std::string("null")
                      : color_replacement_hue_slider->property("valueText").toString().toStdString())
              << " inputMode="
              << (color_replacement_hue_slider == nullptr
                      ? std::string("null")
                      : color_replacement_hue_slider->property("valueInputMode").toString().toStdString())
              << '\n';
    return fail("color hue sliders should accept typed degree values instead of raw normalized values");
  }
  if (split_toning_focused_wheel == nullptr || grading_amount_slider == nullptr ||
      grading_balance_slider == nullptr || selected_tone_saturation_slider == nullptr ||
      selected_tone_luminance_slider == nullptr ||
      split_toning_focused_wheel->property("wheelSize").toDouble() < 140.0 ||
      grading_amount_slider->property("controlHeight").toInt() < 28 ||
      grading_balance_slider->property("controlHeight").toInt() < 28 ||
      selected_tone_hue_slider->property("controlHeight").toInt() < 28 ||
      selected_tone_saturation_slider->property("controlHeight").toInt() < 28 ||
      selected_tone_luminance_slider->property("controlHeight").toInt() < 28 ||
      grading_amount_slider->property("valueText").toString() != QStringLiteral("100") ||
      grading_balance_slider->property("valueText").toString() != QStringLiteral("0%")) {
    return fail("color grading should expose a large focused wheel with compact, precise Amount, Balance, Hue, Saturation, and Luminance controls");
  }
  if (split_toning_shadows_wheel->property("text").toString() != QStringLiteral("Shadows") ||
      split_toning_midtones_wheel->property("text").toString() != QStringLiteral("Midtones") ||
      split_toning_highlights_wheel->property("text").toString() != QStringLiteral("Highlights") ||
      split_toning_focused_wheel->property("wheelSize").toDouble() < 250.0 ||
      !split_toning_focused_wheel->property("title").toString().isEmpty() ||
      split_toning_focused_wheel->property("showValueReadouts").toBool()) {
    return fail("color grading should use named tonal-range tabs above one large, uncluttered hue/saturation wheel");
  }
  if (!QMetaObject::invokeMethod(
          selected_tone_hue_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("90°"))) ||
      std::abs(editor_preview.gradingMidtonesHue() - 0.25) > 1e-6 ||
      selected_tone_hue_slider->property("valueText").toString() != QStringLiteral("90°")) {
    return fail("color grading hue slider typed values should match the displayed degree unit");
  }
  if (!QMetaObject::invokeMethod(
          liquify_radius_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("33%"))) ||
      std::abs(editor_preview.liquifyRadius() - 0.33) > 1e-6 ||
      liquify_radius_slider->property("valueText").toString() != QStringLiteral("33%") ||
      liquify_radius_slider->property("valueInputMode").toString() != QStringLiteral("percentNormalized")) {
    return fail("normalized percent sliders should accept typed percentages instead of raw 0..1 values");
  }
  if (!QMetaObject::invokeMethod(
          exposure_slider,
          "testCommitInputText",
          Q_RETURN_ARG(QVariant, committed_input_value),
          Q_ARG(QVariant, QStringLiteral("+0.42 EV"))) ||
      std::abs(editor_preview.exposure() - 0.42) > 1e-6) {
    return fail("typed exposure input should commit the expected recipe value before slider equivalence proof");
  }
  const auto exposure_recipe_from_typed_input = editor_preview.currentAdjustmentPatchJson();
  editor_preview.setExposure(0.0);
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          exposure_slider,
          "valueDragged",
          Q_ARG(double, 0.42)) ||
      std::abs(editor_preview.exposure() - 0.42) > 1e-6 ||
      editor_preview.currentAdjustmentPatchJson() != exposure_recipe_from_typed_input) {
    return fail("typed numeric input and dragged slider input should produce identical recipe state");
  }
  editor_preview.setExposure(0.0);
  editor_preview.setTemperature(0.0);
  editor_preview.setColorReplacementTargetHue(0.33);
  editor_preview.setGradingMidtonesHue(0.0);
  editor_preview.setLiquifyRadius(0.25);
  QCoreApplication::processEvents();
  editor_preview.setExposure(0.85);
  editor_preview.setChannelMixerRedFromRed(42.0);
  QCoreApplication::processEvents();
  if (exposure_slider == nullptr || channel_mixer_red_slider == nullptr ||
      exposure_slider->property("valueText").toString() != QStringLiteral("+0.85 EV") ||
      !exposure_slider->property("modified").toBool() ||
      !channel_mixer_red_slider->property("modified").toBool() ||
      !QMetaObject::invokeMethod(exposure_slider, "resetRequested") ||
      !QMetaObject::invokeMethod(channel_mixer_red_slider, "resetRequested")) {
    return fail("adjustment sliders should expose a testable double-click reset signal");
  }
  QCoreApplication::processEvents();
  if (std::abs(editor_preview.exposure()) > 1e-6 ||
      std::abs(editor_preview.channelMixerRedFromRed() - 100.0) > 1e-6 ||
      exposure_slider->property("modified").toBool() ||
      exposure_modified_marker != nullptr ||
      channel_mixer_red_slider->property("modified").toBool()) {
    return fail("double-click reset should restore each adjustment slider to its declared default value");
  }
  trace_smoke_progress("typed slider controls checked");
  edit_workspace->setProperty("editTool", QStringLiteral("adjust"));
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  for (auto iteration = 0; iteration < 6; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* crop_inspector_group = find_named_object(edit_workspace, QStringLiteral("cropInspectorGroup"));
  auto* crop_box_group = find_named_object(edit_workspace, QStringLiteral("cropBoxGroup"));
  auto* crop_precision_group = find_named_object(edit_workspace, QStringLiteral("cropPrecisionGroup"));
  auto* crop_guide_aspect_group = find_named_object(edit_workspace, QStringLiteral("cropGuideAspectGroup"));
  auto* crop_transform_group = find_named_object(edit_workspace, QStringLiteral("cropTransformGroup"));
  auto* crop_liquify_group = find_named_object(edit_workspace, QStringLiteral("cropLiquifyGroup"));
  auto* detail_inspector_group = find_named_object(edit_workspace, QStringLiteral("detailInspectorGroup"));
  auto* lens_inspector_group = find_named_object(edit_workspace, QStringLiteral("lensInspectorGroup"));
  if (crop_inspector_group == nullptr || crop_box_group == nullptr || crop_precision_group == nullptr ||
      crop_guide_aspect_group == nullptr ||
      crop_transform_group == nullptr || crop_liquify_group == nullptr || detail_inspector_group == nullptr ||
      lens_inspector_group == nullptr ||
      crop_inspector_group->property("visible").toBool() ||
      !detail_inspector_group->property("visible").toBool() ||
      !lens_inspector_group->property("visible").toBool()) {
    return fail("crop controls should stay hidden until Crop owns the current edit mode, while Detail and Lens live in Adjust");
  }
  if (detail_inspect_button == nullptr || detail_loupe_button == nullptr ||
      detail_mask_preview_button == nullptr || detail_sharpening_group == nullptr ||
      detail_noise_reduction_group == nullptr || detail_before_after_button == nullptr) {
    return fail("detail inspector should expose inspection, sharpen, masking, and noise reduction controls");
  }
  auto* detail_edit_image_viewport = find_named_object(edit_workspace, QStringLiteral("editImageViewport"));
  const auto detail_inspection_asset_path =
      runtime_root.filePath(QStringLiteral("detail-inspection-proof.png"));
  QImage detail_inspection_fixture(1200, 800, QImage::Format_RGB32);
  detail_inspection_fixture.fill(QColor(72, 96, 120));
  if (!detail_inspection_fixture.save(detail_inspection_asset_path)) {
    return fail("detail inspection proof should create a source image fixture");
  }
  catalog_assets.replaceAssetsForTesting({make_asset(
      QStringLiteral("detail-inspection-proof"),
      detail_inspection_asset_path,
      QUrl::fromLocalFile(detail_inspection_asset_path).toString())});
  catalog_assets.selectIndex(0);
  if (!wait_for_condition([&]() {
        return editor_preview.hasAsset() &&
               editor_preview.assetId() == QStringLiteral("detail-inspection-proof") &&
               editor_preview.sourceImageWidth() == detail_inspection_fixture.width() &&
               editor_preview.sourceImageHeight() == detail_inspection_fixture.height() &&
               !editor_preview.processing();
      }, 15000)) {
    return fail("detail inspection proof asset should publish source dimensions before one-to-one review");
  }
  process_layout_events(8);
  QVariant detail_inspection_started;
  const auto detail_pixel_ratio =
      detail_edit_image_viewport != nullptr
          ? detail_edit_image_viewport->property("inspectionDevicePixelRatio").toDouble()
          : 1.0;
  if (detail_edit_image_viewport == nullptr ||
      !QMetaObject::invokeMethod(
          detail_edit_image_viewport,
          "inspectDetailAtOneToOne",
          Q_RETURN_ARG(QVariant, detail_inspection_started)) ||
      !detail_inspection_started.toBool() ||
      std::abs(detail_edit_image_viewport->property("sourcePixelScreenWidth").toDouble() *
                   detail_pixel_ratio -
               1.0) > 0.05 ||
      std::abs(detail_edit_image_viewport->property("sourcePixelScreenHeight").toDouble() *
                   detail_pixel_ratio -
               1.0) > 0.05 ||
      !edit_workspace->property("loupeEnabled").toBool() ||
      !edit_workspace->property("focusPeakingOverlayEnabled").toBool()) {
    return fail("detail inspection should render one source pixel per device pixel with loupe and peaking enabled");
  }
  edit_workspace->setProperty("loupeEnabled", false);
  edit_workspace->setProperty("focusPeakingOverlayEnabled", false);
  detail_edit_image_viewport->setProperty("zoom", 1.0);
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activateEditTool",
          Q_ARG(QVariant, QVariant(QStringLiteral("crop"))))) {
    return fail("edit workspace should expose crop inspector activation");
  }
  QCoreApplication::processEvents();
  if (!crop_inspector_group->property("visible").toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("crop") ||
      edit_workspace->property("editInspectorTab").toString() != QStringLiteral("crop") ||
      edit_inspector_panel->property("title").toString() != QStringLiteral("Crop") ||
      !crop_inspector_tab->property("visible").toBool() ||
      !crop_inspector_tab->property("active").toBool() ||
      detail_inspector_tab->property("active").toBool() ||
      detail_inspector_group->property("visible").toBool() ||
      lens_inspector_group->property("visible").toBool() ||
      crop_box_group->property("title").toString() != QStringLiteral("Crop & Composition") ||
      crop_precision_group->property("title").toString() != QStringLiteral("Position & Size") ||
      crop_guide_aspect_group->property("title").toString() != QStringLiteral("Custom Aspect Ratio") ||
      crop_transform_group->property("title").toString() != QStringLiteral("Perspective & Transform") ||
      crop_liquify_group->property("title").toString() != QStringLiteral("Liquify") ||
      !crop_box_group->property("expanded").toBool() ||
      crop_precision_group->property("expanded").toBool() ||
      crop_guide_aspect_group->property("expanded").toBool() ||
      crop_transform_group->property("expanded").toBool() ||
      crop_liquify_group->property("expanded").toBool() ||
      !crop_liquify_group->property("visible").toBool()) {
    std::cerr << "Crop inspector state: cropGroupVisible=" << crop_inspector_group->property("visible").toBool()
              << " editTool=" << edit_workspace->property("editTool").toString().toStdString()
              << " tab=" << edit_workspace->property("editInspectorTab").toString().toStdString()
              << " title=" << edit_inspector_panel->property("title").toString().toStdString()
              << " cropTabVisible=" << crop_inspector_tab->property("visible").toBool()
              << " cropTabActive=" << crop_inspector_tab->property("active").toBool()
              << " detailTabActive=" << detail_inspector_tab->property("active").toBool()
              << " detailVisible=" << detail_inspector_group->property("visible").toBool()
              << " lensVisible=" << lens_inspector_group->property("visible").toBool()
              << " cropBoxTitle=" << crop_box_group->property("title").toString().toStdString()
              << " guideTitle=" << crop_guide_aspect_group->property("title").toString().toStdString()
              << " transformTitle=" << crop_transform_group->property("title").toString().toStdString()
              << " liquifyTitle=" << crop_liquify_group->property("title").toString().toStdString()
              << " expanded=" << crop_box_group->property("expanded").toBool()
              << "/" << crop_guide_aspect_group->property("expanded").toBool()
              << "/" << crop_transform_group->property("expanded").toBool()
              << "/" << crop_liquify_group->property("expanded").toBool()
              << " liquifyVisible=" << crop_liquify_group->property("visible").toBool()
              << '\n';
    return fail("crop inspector should be a dedicated mode-owned tab with only crop controls visible");
  }
  edit_workspace->setProperty("cropTransformExpanded", true);
  QCoreApplication::processEvents();
  if (!crop_transform_group->property("expanded").toBool() ||
      !crop_liquify_group->property("visible").toBool() ||
      crop_liquify_group->property("expanded").toBool()) {
    return fail("crop inspector should expand transform controls while keeping the available liquify controls collapsed");
  }
  edit_workspace->setProperty("cropTransformExpanded", false);
  edit_workspace->setProperty("cropLiquifyExpanded", false);
  QCoreApplication::processEvents();
  auto* crop_guide_mode_combo = find_named_object(edit_workspace, QStringLiteral("cropGuideModeComboBox"));
  QVariant crop_guide_signature;
  QVariant crop_guide_set_result;
  if (crop_guide_mode_combo == nullptr ||
      crop_guide_mode_combo->property("count").toInt() != 6 ||
      edit_workspace->findChild<QObject*>(QStringLiteral("cropGuideOffButton")) != nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("cropGuideThirdsButton")) != nullptr ||
      edit_workspace->findChild<QObject*>(QStringLiteral("cropGuideGoldenButton")) != nullptr ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "cropGuideControlSignature",
          Q_RETURN_ARG(QVariant, crop_guide_signature)) ||
      !crop_guide_signature.toString().contains(QStringLiteral("Off,Thirds,Center,Diagonal,Golden Ratio,Golden Spiral")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setCropGuideMode",
          Q_RETURN_ARG(QVariant, crop_guide_set_result),
          Q_ARG(QVariant, QStringLiteral("Golden Spiral"))) ||
      !crop_guide_set_result.toBool() ||
      crop_guide_mode_combo->property("currentText").toString() != QStringLiteral("Golden Spiral")) {
    return fail("crop guide choices should live in one compact selector without six persistent guide buttons");
  }

  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setCropToolMode",
          Q_ARG(QVariant, QVariant(QStringLiteral("transform"))))) {
    return fail("edit workspace should expose crop submode switching");
  }
  QCoreApplication::processEvents();
	  auto* crop_tool_hud = edit_workspace->findChild<QObject*>(QStringLiteral("cropToolHud"));
	  auto* crop_tool_header_label = edit_workspace->findChild<QObject*>(QStringLiteral("cropToolHeaderLabel"));
	  auto* crop_tool_aspect_label = edit_workspace->findChild<QObject*>(QStringLiteral("cropToolAspectLabel"));
	  auto* crop_hud_crop_button = find_named_object(edit_workspace, QStringLiteral("cropHudCropButton"));
	  auto* crop_hud_horizon_button = find_named_object(edit_workspace, QStringLiteral("cropHudHorizonButton"));
	  auto* crop_hud_transform_button = find_named_object(edit_workspace, QStringLiteral("cropHudTransformButton"));
	  auto* crop_active_canvas_border = edit_workspace->findChild<QObject*>(QStringLiteral("cropActiveCanvasBorder"));
	  auto* crop_box_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("cropBoxOverlay"));
	  auto* crop_horizon_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("cropHorizonOverlay"));
	  auto* crop_transform_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("cropTransformOverlay"));
  auto* edit_zoom_toolbar_for_crop = find_named_object(edit_workspace, QStringLiteral("editZoomToolbar"));
  auto* edit_action_toolbar_for_crop = find_named_object(edit_workspace, QStringLiteral("editActionToolbar"));
  auto* edit_context_panel_for_crop = find_named_object(edit_workspace, QStringLiteral("editContextPanel"));
  auto* edit_filmstrip_for_crop = find_named_object(edit_workspace, QStringLiteral("editFilmstrip"));
  editor_preview.setCropPreset(QStringLiteral("4x5"));
  QCoreApplication::processEvents();
	  if (crop_tool_hud == nullptr || crop_tool_header_label == nullptr || crop_tool_aspect_label == nullptr ||
	      crop_hud_crop_button == nullptr || crop_hud_horizon_button == nullptr ||
	      crop_hud_transform_button == nullptr ||
	      crop_active_canvas_border == nullptr ||
	      crop_box_overlay == nullptr || crop_horizon_overlay == nullptr || crop_transform_overlay == nullptr ||
	      edit_zoom_toolbar_for_crop == nullptr || edit_action_toolbar_for_crop == nullptr ||
	      edit_context_panel_for_crop == nullptr || edit_filmstrip_for_crop == nullptr ||
	      !crop_tool_hud->property("visible").toBool() || crop_active_canvas_border->property("visible").toBool() ||
      !crop_tool_hud->property("hudContract").toString().contains(QStringLiteral("specialized-tool-hud:v2|tool:crop")) ||
      !crop_tool_hud->property("hudContract").toString().contains(QStringLiteral("modes:visible")) ||
      !crop_tool_hud->property("hudContract").toString().contains(QStringLiteral("no-elide:true")) ||
      !edit_workspace->property("activeCanvasToolChromeReduced").toBool() ||
      edit_workspace->property("editContextPanelActive").toBool() ||
      edit_context_panel_for_crop->property("visible").toBool() ||
      edit_filmstrip_for_crop->property("visible").toBool() ||
      edit_zoom_toolbar_for_crop->property("visible").toBool() ||
      edit_action_toolbar_for_crop->property("visible").toBool() ||
	      edit_workspace->property("cropToolMode").toString() !=
	          QStringLiteral("transform") ||
	      crop_tool_header_label->property("text").toString() != QStringLiteral("CROP") ||
	      crop_tool_aspect_label->property("text").toString() != QStringLiteral("Aspect 4x5") ||
	      !crop_hud_transform_button->property("active").toBool() ||
	      crop_hud_crop_button->property("active").toBool() ||
	      crop_hud_horizon_button->property("active").toBool() ||
	      editor_preview.cropEditing() || crop_box_overlay->property("active").toBool() ||
	      !crop_transform_overlay->property("active").toBool()) {
	    return fail("crop transform mode should own the canvas HUD, show aspect, and reduce persistent edit chrome");
	  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setCropToolMode",
          Q_ARG(QVariant, QVariant(QStringLiteral("horizon"))))) {
    return fail("edit workspace should expose horizon straighten submode switching");
  }
  QCoreApplication::processEvents();
  QVariant horizon_applied;
  constexpr auto kPi = 3.14159265358979323846;
  const auto expected_horizon_angle = std::atan2(0.2, 0.8) * 180.0 / kPi;
	  if (edit_workspace->property("cropToolMode").toString() != QStringLiteral("horizon") ||
	      crop_tool_header_label->property("text").toString() != QStringLiteral("CROP") ||
	      !crop_hud_horizon_button->property("active").toBool() ||
	      editor_preview.cropEditing() || crop_box_overlay->property("active").toBool() ||
	      crop_transform_overlay->property("active").toBool() ||
	      !crop_horizon_overlay->property("active").toBool() ||
      !QMetaObject::invokeMethod(
          crop_horizon_overlay,
          "applyNormalizedGuide",
          Q_RETURN_ARG(QVariant, horizon_applied),
          Q_ARG(QVariant, 0.1),
          Q_ARG(QVariant, 0.3),
          Q_ARG(QVariant, 0.9),
          Q_ARG(QVariant, 0.5)) ||
      !horizon_applied.toBool() || std::abs(editor_preview.transformRotation() + expected_horizon_angle) > 1e-4) {
    std::cerr << "Horizon state: mode=" << edit_workspace->property("cropToolMode").toString().toStdString()
              << " cropEditing=" << editor_preview.cropEditing()
              << " cropActive=" << crop_box_overlay->property("active").toBool()
              << " transformActive=" << crop_transform_overlay->property("active").toBool()
              << " horizonActive=" << crop_horizon_overlay->property("active").toBool()
              << " applied=" << horizon_applied.toBool()
              << " rotation=" << editor_preview.transformRotation()
              << " expected=" << -expected_horizon_angle << '\n';
    return fail("crop horizon mode should expose a direct draggable guide that straightens from endpoints");
  }
  editor_preview.undo();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setCropToolMode",
          Q_ARG(QVariant, QVariant(QStringLiteral("crop"))))) {
    return fail("edit workspace should switch back to crop-box mode");
  }
  QCoreApplication::processEvents();
	  if (edit_workspace->property("cropToolMode").toString() != QStringLiteral("crop") ||
	      crop_tool_header_label->property("text").toString() != QStringLiteral("CROP") ||
	      !crop_hud_crop_button->property("active").toBool() ||
	      !editor_preview.cropEditing() || !crop_box_overlay->property("active").toBool() ||
	      crop_transform_overlay->property("active").toBool()) {
    return fail("crop-box mode should reactivate direct crop editing and hide transform handles");
  }
  crop_box_overlay->setProperty("imageX", 0.0);
  crop_box_overlay->setProperty("imageY", 0.0);
  crop_box_overlay->setProperty("imageWidth", 200.0);
  crop_box_overlay->setProperty("imageHeight", 100.0);
  crop_box_overlay->setProperty("cropX", 0.25);
  crop_box_overlay->setProperty("cropY", 0.2);
  crop_box_overlay->setProperty("cropWidth", 0.5);
  crop_box_overlay->setProperty("cropHeight", 0.5);
  QCoreApplication::processEvents();
  QVariant near_corner_hit_mode;
  QVariant top_edge_hit_mode;
  QVariant center_hit_mode;
  QVariant normalized_aspect_ratio;
  if (crop_box_overlay->property("handleHitSize").toDouble() < 28.0 ||
      crop_box_overlay->property("handleVisualSize").toDouble() > 16.0 ||
      !QMetaObject::invokeMethod(
          crop_box_overlay,
          "testHitMode",
          Q_RETURN_ARG(QVariant, near_corner_hit_mode),
          Q_ARG(QVariant, 39.0),
          Q_ARG(QVariant, 9.0)) ||
      near_corner_hit_mode.toString() != QStringLiteral("topLeft") ||
      !QMetaObject::invokeMethod(
          crop_box_overlay,
          "testHitMode",
          Q_RETURN_ARG(QVariant, top_edge_hit_mode),
          Q_ARG(QVariant, 100.0),
          Q_ARG(QVariant, 20.0)) ||
      top_edge_hit_mode.toString() != QStringLiteral("top") ||
      !QMetaObject::invokeMethod(
          crop_box_overlay,
          "constraintAspectRatio",
          Q_RETURN_ARG(QVariant, normalized_aspect_ratio)) ||
      std::abs(normalized_aspect_ratio.toDouble() - 0.4) > 1e-6 ||
      !QMetaObject::invokeMethod(
          crop_box_overlay,
          "testHitMode",
          Q_RETURN_ARG(QVariant, center_hit_mode),
          Q_ARG(QVariant, 100.0),
          Q_ARG(QVariant, 45.0)) ||
      center_hit_mode.toString() != QStringLiteral("move")) {
    return fail("crop-box overlay should expose forgiving corner and edge targets with source-correct aspect constraints");
  }
  const QStringList guide_modes{
      QStringLiteral("Off"),
      QStringLiteral("Thirds"),
      QStringLiteral("Center"),
      QStringLiteral("Diagonal"),
      QStringLiteral("Golden Ratio"),
      QStringLiteral("Golden Spiral"),
  };
  if (crop_box_overlay->property("guideLineOpacity").toDouble() > 0.45 ||
      crop_box_overlay->property("guideSpiralOpacity").toDouble() > 0.48) {
    return fail("crop guide overlays should stay visually quiet enough not to overpower the image");
  }
  for (const auto& guide_mode : guide_modes) {
    edit_workspace->setProperty("cropGuideMode", guide_mode);
    QCoreApplication::processEvents();
    if (crop_box_overlay->property("guideMode").toString() != guide_mode) {
      return fail("crop guide selector should bind every guide mode to the on-canvas overlay");
    }
  }
  trace_smoke_progress("crop tool modes checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_CROP_WORKFLOW")) {
    return EXIT_SUCCESS;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_VISIBLE_SURFACE_COUNTS")) {
    QVariant crop_surface_signature;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "studioVisibleSurfaceCountSignature",
            Q_RETURN_ARG(QVariant, crop_surface_signature)) ||
        !crop_surface_signature.toString().contains(QStringLiteral("studio-visible-surface-counts:v1")) ||
        !crop_surface_signature.toString().contains(QStringLiteral("|tool:crop|")) ||
        !crop_surface_signature.toString().contains(QStringLiteral("|primary-hud:1")) ||
        !crop_surface_signature.toString().contains(QStringLiteral("|view-toolbars:0")) ||
        !crop_surface_signature.toString().contains(QStringLiteral("|context-panel:0")) ||
        !crop_surface_signature.toString().contains(QStringLiteral("|filmstrip:0")) ||
        !crop_surface_signature.toString().contains(QStringLiteral("|one-primary-command:1")) ||
        !crop_surface_signature.toString().contains(QStringLiteral("|duplicate-context:false"))) {
      return fail("crop should expose deterministic visible surface counts with one primary HUD");
    }
    edit_workspace->setProperty("editTool", QStringLiteral("adjust"));
    editor_preview.setCropEditing(false);
    QCoreApplication::processEvents();
    QVariant adjust_surface_signature;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "studioVisibleSurfaceCountSignature",
            Q_RETURN_ARG(QVariant, adjust_surface_signature)) ||
        !adjust_surface_signature.toString().contains(QStringLiteral("|tool:adjust|")) ||
        !adjust_surface_signature.toString().contains(QStringLiteral("|primary-hud:0")) ||
        !adjust_surface_signature.toString().contains(QStringLiteral("|view-toolbars:2")) ||
        !adjust_surface_signature.toString().contains(QStringLiteral("|context-panel:1")) ||
        !adjust_surface_signature.toString().contains(QStringLiteral("|inspector:1")) ||
        !adjust_surface_signature.toString().contains(QStringLiteral("|filmstrip:1")) ||
        !adjust_surface_signature.toString().contains(QStringLiteral("|duplicate-context:false"))) {
      return fail("adjust should expose deterministic visible surface counts for standard Studio chrome");
    }
    QVariant focused_retouch_mode_for_surfaces;
    QVariant retouch_surface_signature;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "startRetouchTool",
            Q_RETURN_ARG(QVariant, focused_retouch_mode_for_surfaces),
            Q_ARG(QVariant, QStringLiteral("heal")),
            Q_ARG(QVariant, false)) ||
        focused_retouch_mode_for_surfaces.toString() != QStringLiteral("heal") ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            "studioVisibleSurfaceCountSignature",
            Q_RETURN_ARG(QVariant, retouch_surface_signature)) ||
        !retouch_surface_signature.toString().contains(QStringLiteral("|tool:retouch|")) ||
        !retouch_surface_signature.toString().contains(QStringLiteral("|primary-hud:1")) ||
        !retouch_surface_signature.toString().contains(QStringLiteral("|view-toolbars:0")) ||
        !retouch_surface_signature.toString().contains(QStringLiteral("|context-panel:0")) ||
        !retouch_surface_signature.toString().contains(QStringLiteral("|inspector:1")) ||
        !retouch_surface_signature.toString().contains(QStringLiteral("|filmstrip:0")) ||
        !retouch_surface_signature.toString().contains(QStringLiteral("|one-primary-command:1")) ||
        !retouch_surface_signature.toString().contains(QStringLiteral("|duplicate-context:false"))) {
      std::cerr << "Surface count signatures: crop="
                << crop_surface_signature.toString().toStdString()
                << " adjust=" << adjust_surface_signature.toString().toStdString()
                << " retouch=" << retouch_surface_signature.toString().toStdString()
                << '\n';
      return fail("retouch should expose deterministic visible surface counts with one primary HUD");
    }
    trace_smoke_progress("focused visible surface counts checked");
    return EXIT_SUCCESS;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_DIAGNOSTICS_GATING")) {
    edit_workspace->setProperty("editTool", QStringLiteral("adjust"));
    edit_workspace->setProperty("developerDiagnosticsEnabled", false);
    edit_workspace->setProperty("diagnosticsPanelOpen", true);
    QCoreApplication::processEvents();
    auto* focused_diagnostics_button =
        find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayButton"));
    auto* focused_diagnostics_panel =
        find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayPanel"));
    if (focused_diagnostics_button == nullptr ||
        focused_diagnostics_panel == nullptr ||
        !focused_diagnostics_button->property("visible").toBool() ||
        !focused_diagnostics_panel->property("visible").toBool() ||
        !focused_diagnostics_button->property("diagnosticsMenuContract")
             .toString()
             .contains(QStringLiteral("image-inspection:user-accessible"))) {
      return fail("image inspection overlays should be available in Edit without developer mode");
    }
    edit_workspace->setProperty("diagnosticsPanelOpen", false);
    QCoreApplication::processEvents();
    if (!focused_diagnostics_button->property("visible").toBool() ||
        focused_diagnostics_panel->property("visible").toBool()) {
      return fail("image inspection button should stay visible while its menu is closed");
    }
    edit_workspace->setProperty("diagnosticsPanelOpen", true);
    QCoreApplication::processEvents();
    if (!focused_diagnostics_panel->property("visible").toBool() ||
        !item_visible(qobject_cast<QQuickItem*>(focused_diagnostics_panel)) ||
        !focused_diagnostics_panel->property("diagnosticsMenuContract")
             .toString()
             .contains(QStringLiteral("image-inspection:user-accessible"))) {
      return fail("image inspection panel should open with advanced details still developer-gated");
    }
    edit_workspace->setProperty("developerDiagnosticsEnabled", false);
    QCoreApplication::processEvents();
    if (!focused_diagnostics_button->property("visible").toBool() ||
        !focused_diagnostics_panel->property("visible").toBool()) {
      return fail("turning off developer diagnostics should preserve normal image inspection overlays");
    }
    trace_smoke_progress("focused diagnostics gating checked");
    return EXIT_SUCCESS;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_RETOUCH_HUD")) {
    QVariant focused_retouch_mode;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "startRetouchTool",
            Q_RETURN_ARG(QVariant, focused_retouch_mode),
            Q_ARG(QVariant, QStringLiteral("heal")),
            Q_ARG(QVariant, false)) ||
        focused_retouch_mode.toString() != QStringLiteral("heal")) {
      return fail("focused retouch smoke should start Heal directly");
    }
    QCoreApplication::processEvents();
    auto* focused_retouch_tool_hud =
        edit_workspace->findChild<QObject*>(QStringLiteral("retouchToolHud"));
    auto* focused_retouch_status_label =
        edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudStatusLabel"));
    auto* focused_retouch_apply_button =
        edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudApplyButton"));
    auto* focused_retouch_more_menu_button =
        edit_workspace->findChild<QObject*>(QStringLiteral("retouchHudMoreMenuButton"));
    auto* focused_retouch_mode_menu_button =
        find_named_object(edit_workspace, QStringLiteral("retouchHudModeMenuButton"));
    auto* focused_edit_zoom_toolbar_for_retouch =
        find_named_object(edit_workspace, QStringLiteral("editZoomToolbar"));
    auto* focused_edit_action_toolbar_for_retouch =
        find_named_object(edit_workspace, QStringLiteral("editActionToolbar"));
    QVariant focused_retouch_visibility_signature;
    QVariant focused_retouch_workflow_signature;
    QVariant focused_retouch_action_signature;
    QVariant focused_retouch_context_signature;
    const auto focused_retouch_inpaint_capability =
        editor_preview.aiInpaintAvailable() ? QStringLiteral("model-ready") : QStringLiteral("local-fallback");
    const auto focused_retouch_hud_contract =
        focused_retouch_tool_hud == nullptr
            ? QString()
            : focused_retouch_tool_hud->property("hudContract").toString();
    const auto focused_retouch_visibility_contract =
        focused_retouch_tool_hud == nullptr
            ? QString()
            : focused_retouch_tool_hud->property("visibilityContract").toString();
    const auto focused_retouch_status =
        focused_retouch_status_label == nullptr
            ? QString()
            : focused_retouch_status_label->property("text").toString();
    const auto focused_retouch_mode_menu =
        focused_retouch_mode_menu_button == nullptr
            ? QString()
            : focused_retouch_mode_menu_button->property("menuContract").toString();
    const auto focused_retouch_more_menu =
        focused_retouch_more_menu_button == nullptr
            ? QString()
            : focused_retouch_more_menu_button->property("menuContract").toString();
    const bool focused_retouch_visibility_invoked = QMetaObject::invokeMethod(
        edit_workspace,
        "retouchToolVisibilitySignature",
        Q_RETURN_ARG(QVariant, focused_retouch_visibility_signature));
    const bool focused_retouch_workflow_invoked = QMetaObject::invokeMethod(
        edit_workspace,
        "retouchWorkflowSignature",
        Q_RETURN_ARG(QVariant, focused_retouch_workflow_signature));
    const bool focused_retouch_action_invoked = QMetaObject::invokeMethod(
        edit_workspace,
        "retouchActionSignature",
        Q_RETURN_ARG(QVariant, focused_retouch_action_signature));
    const bool focused_retouch_context_invoked = QMetaObject::invokeMethod(
        edit_workspace,
        "activeMaskEditingContextSignature",
        Q_RETURN_ARG(QVariant, focused_retouch_context_signature));
    if (focused_retouch_tool_hud == nullptr ||
        focused_retouch_status_label == nullptr ||
        focused_retouch_apply_button == nullptr ||
        focused_retouch_more_menu_button == nullptr ||
        focused_retouch_mode_menu_button == nullptr ||
        focused_edit_zoom_toolbar_for_retouch == nullptr ||
        focused_edit_action_toolbar_for_retouch == nullptr ||
        !focused_retouch_tool_hud->property("visible").toBool() ||
        !focused_retouch_hud_contract.contains(QStringLiteral("specialized-tool-hud:v3|tool:retouch")) ||
        !focused_retouch_hud_contract.contains(QStringLiteral("component:ToolCommandBar")) ||
        !focused_retouch_hud_contract.contains(QStringLiteral("single-row:true")) ||
        !focused_retouch_hud_contract.contains(QStringLiteral("mode-change-animation:true")) ||
        !focused_retouch_hud_contract.contains(QStringLiteral("secondary-actions:more-menu")) ||
        !focused_retouch_hud_contract.contains(QStringLiteral("no-elide:true")) ||
        !focused_retouch_tool_hud->property("modeChangeAnimationEnabled").toBool() ||
        !focused_retouch_tool_hud->property("modeChangeMotionContract").toString().contains(
            QStringLiteral("mode-change-motion:v1|enabled:true")) ||
        !focused_retouch_visibility_contract.contains(QStringLiteral("layout:single-row-command-bar")) ||
        !focused_retouch_visibility_contract.contains(QStringLiteral("primary:Apply")) ||
        !focused_retouch_visibility_contract.contains(QStringLiteral("inline:0")) ||
        !focused_retouch_visibility_contract.contains(QStringLiteral("more:9")) ||
        focused_retouch_mode_menu_button->property("text").toString() != QStringLiteral("Heal") ||
        !focused_retouch_mode_menu_button->property("visible").toBool() ||
        !focused_retouch_mode_menu.contains(QStringLiteral("items:8")) ||
        !focused_retouch_mode_menu.contains(QStringLiteral("checked:1")) ||
        !focused_retouch_mode_menu.contains(QStringLiteral("disabled:2")) ||
        !focused_retouch_mode_menu.contains(QStringLiteral("disabled-reasons-complete:true")) ||
        !focused_retouch_mode_menu.contains(QStringLiteral("danger:0")) ||
        !edit_workspace->property("activeCanvasToolChromeReduced").toBool() ||
        focused_edit_zoom_toolbar_for_retouch->property("visible").toBool() ||
        focused_edit_action_toolbar_for_retouch->property("visible").toBool() ||
        !focused_retouch_status.contains(QStringLiteral("Heal")) ||
        !focused_retouch_status.contains(QStringLiteral("Paint area")) ||
        focused_retouch_status.contains(QStringLiteral("fallback")) ||
        (!editor_preview.inpaintRuntimeLabel().isEmpty() &&
         focused_retouch_status.contains(editor_preview.inpaintRuntimeLabel())) ||
        !focused_retouch_status_label->property("readableStatusContract").toString().contains(QStringLiteral("fit-horizontal:true")) ||
        focused_retouch_apply_button->property("text").toString() != QStringLiteral("Apply") ||
        focused_retouch_more_menu_button->property("text").toString() != QStringLiteral("More") ||
        !focused_retouch_more_menu.contains(QStringLiteral("items:9")) ||
        !focused_retouch_more_menu.contains(QStringLiteral("disabled:1")) ||
        !focused_retouch_more_menu.contains(QStringLiteral("disabled-reasons-complete:true")) ||
        !focused_retouch_more_menu.contains(QStringLiteral("danger:0")) ||
        !focused_retouch_visibility_invoked ||
        !focused_retouch_visibility_signature.toString().contains(QStringLiteral("layout:single-row-command-bar")) ||
        !focused_retouch_workflow_invoked ||
        focused_retouch_workflow_signature.toString() !=
            QStringLiteral("mode:heal|label:Heal|stage:Paint area|source:none|apply:true|regenerate:true|capability:%1|beforeAfter:after")
                .arg(focused_retouch_inpaint_capability) ||
        !focused_retouch_action_invoked ||
        focused_retouch_action_signature.toString() !=
            QStringLiteral("apply:true|cancel:true|reset:true|delete:false|reopen:false") ||
        !focused_retouch_context_invoked ||
        focused_retouch_context_signature.toString() !=
            QStringLiteral("tool:retouch|mask:false|brush:false|retouch:true|count:1") ||
        edit_workspace->property("editTool").toString() != QStringLiteral("retouch") ||
        editor_preview.activeMaskToolRole() != QStringLiteral("retouch") ||
        editor_preview.activeMaskRetouchMode() != QStringLiteral("heal")) {
      std::cerr << "Focused Retouch HUD state:"
                << " hud=" << (focused_retouch_tool_hud == nullptr ? "null" : "present")
                << " hudVisible=" << (focused_retouch_tool_hud != nullptr &&
                                      focused_retouch_tool_hud->property("visible").toBool())
                << " hudContract=" << focused_retouch_hud_contract.toStdString()
                << " visibilityContract=" << focused_retouch_visibility_contract.toStdString()
                << " visibilitySignature=" << focused_retouch_visibility_signature.toString().toStdString()
                << " status=" << focused_retouch_status.toStdString()
                << " modeText="
                << (focused_retouch_mode_menu_button == nullptr
                        ? std::string("null")
                        : focused_retouch_mode_menu_button->property("text").toString().toStdString())
                << " modeMenu=" << focused_retouch_mode_menu.toStdString()
                << " moreText="
                << (focused_retouch_more_menu_button == nullptr
                        ? std::string("null")
                        : focused_retouch_more_menu_button->property("text").toString().toStdString())
                << " moreMenu=" << focused_retouch_more_menu.toStdString()
                << " workflow=" << focused_retouch_workflow_signature.toString().toStdString()
                << " actions=" << focused_retouch_action_signature.toString().toStdString()
                << " context=" << focused_retouch_context_signature.toString().toStdString()
                << " chromeReduced=" << edit_workspace->property("activeCanvasToolChromeReduced").toBool()
                << " zoomVisible="
                << (focused_edit_zoom_toolbar_for_retouch != nullptr &&
                    focused_edit_zoom_toolbar_for_retouch->property("visible").toBool())
                << " actionVisible="
                << (focused_edit_action_toolbar_for_retouch != nullptr &&
                    focused_edit_action_toolbar_for_retouch->property("visible").toBool())
                << " editTool=" << edit_workspace->property("editTool").toString().toStdString()
                << " role=" << editor_preview.activeMaskToolRole().toStdString()
                << " mode=" << editor_preview.activeMaskRetouchMode().toStdString()
                << '\n';
      return fail("focused retouch HUD should be one compact command surface");
    }
    trace_smoke_progress("focused retouch HUD checked");
    return EXIT_SUCCESS;
  }
  edit_workspace->setProperty("editTool", QStringLiteral("adjust"));
  editor_preview.setCropEditing(false);
  QCoreApplication::processEvents();

  const auto initial_mask_count = editor_preview.maskCount();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setMaskCreationOperation",
          Q_ARG(QVariant, QVariant(QStringLiteral("intersect")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("linear"))))) {
    return fail("edit workspace should expose testable mask creation controls");
  }
  process_layout_events(6);
  auto* mask_creation_toolbar = edit_workspace->findChild<QObject*>(QStringLiteral("maskCreationToolbar"));
  auto* mask_tool_rail = edit_workspace->findChild<QObject*>(QStringLiteral("maskToolRail"));
  auto* mask_tool_rail_toggle_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskToolRailToggleButton"));
  auto* mask_tool_rail_section_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskToolRailSectionLabel"));
  auto* mask_creation_vocabulary_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskCreationVocabularyLabel"));
  auto* mask_source_object_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceObjectButton"));
  auto* mask_source_subject_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceSubjectButton"));
  auto* mask_source_sky_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceSkyButton"));
  auto* mask_source_background_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceBackgroundButton"));
  auto* mask_selection_menu_button =
      find_named_object(edit_workspace, QStringLiteral("maskSelectionMenuButton"));
  auto* mask_brush_shape_menu_button =
      find_named_object(edit_workspace, QStringLiteral("maskBrushShapeMenuButton"));
  auto* mask_range_menu_button =
      find_named_object(edit_workspace, QStringLiteral("maskRangeMenuButton"));
  auto* mask_retouch_menu_button =
      find_named_object(edit_workspace, QStringLiteral("maskRetouchMenuButton"));
  auto* mask_source_brush_add_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceBrushAddButton"));
  auto* mask_source_brush_erase_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceBrushEraseButton"));
  auto* mask_source_ellipse_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceEllipseButton"));
  auto* mask_source_rectangle_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskSourceRectangleButton"));
  auto* mask_add_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskCombineAddButton"));
  auto* mask_subtract_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskCombineSubtractButton"));
  auto* mask_intersect_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskCombineIntersectButton"));
  auto* mask_replace_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskCombineReplaceButton"));
  auto* mask_delete_empty_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskDeleteEmptySelectionsButton"));
  auto* mask_update_generated_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskUpdateGeneratedSelectionsButton"));
  auto* mask_action_retry_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskActionRetryButton"));
  auto* mask_creation_done_button = edit_workspace->findChild<QObject*>(QStringLiteral("maskCreationDoneButton"));
  auto* active_canvas_tool_chip =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeCanvasToolChip"));
  auto* active_canvas_tool_chip_name_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeCanvasToolChipNameLabel"));
  auto* active_canvas_tool_chip_mode_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeCanvasToolChipModeLabel"));
  auto* active_canvas_tool_done_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeCanvasToolDoneButton"));
  auto* active_canvas_tool_cancel_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeCanvasToolCancelButton"));
  auto* mask_refine_toolbar = edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineToolbar"));
  auto* mask_refine_toolbar_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineToolbarLabel"));
  auto* mask_refine_edge_add_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineEdgeAddButton"));
  auto* mask_refine_edge_erase_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineEdgeEraseButton"));
  auto* mask_refine_smart_select_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineSmartSelectButton"));
  auto* mask_refine_show_original_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineShowOriginalButton"));
  auto* mask_refine_show_refined_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineShowRefinedButton"));
  auto* mask_refine_high_quality_preview_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineHighQualityPreviewButton"));
  auto* mask_full_resolution_preview_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskFullResolutionPreviewButton"));
  auto* mask_refine_edge_preview_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineEdgePreviewButton"));
  auto* mask_refine_invert_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineInvertButton"));
  auto* mask_refine_duplicate_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineDuplicateButton"));
  auto* mask_refine_duplicate_invert_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineDuplicateInvertButton"));
  auto* mask_refine_duplicate_set_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineDuplicateSetButton"));
  auto* mask_refine_reset_edge_button =
      edit_workspace->findChild<QObject*>(QStringLiteral("maskRefineResetEdgeButton"));
  auto* active_mask_toolbar_status_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("activeMaskToolbarStatusLabel"));
  QPointer<QObject> mask_stack_strip = find_named_object(edit_workspace, QStringLiteral("maskStackStrip"));
  QPointer<QObject> mask_stack_strip_label = find_named_object(edit_workspace, QStringLiteral("maskStackStripLabel"));
  auto* edit_shared_mask_overlay =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportSharedMaskOverlay"));
  auto* edit_mask_canvas_overlay =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportMaskCanvasOverlay"));
  auto* edit_generic_mask_tint =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportGenericMaskTint"));
  auto* edit_pan_zoom_input_handler =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportPanZoomInputHandler"));
  QVariant mask_creation_context_signature;
  QVariant mask_create_refine_split_signature;
  QVariant mask_refinement_preview_signature;
  QVariant mask_refinement_preview_original;
  QVariant mask_refinement_preview_refined;
  QVariant mask_refinement_high_quality_enabled;
  QVariant mask_full_resolution_preview_enabled;
  QVariant mask_action_retry_signature;
  QVariant mask_work_chrome_signature;
  QVariant selection_creation_surface_signature;
  const auto previous_mask_preview_mode = editor_preview.maskPreviewMode();
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  QCoreApplication::processEvents();
  QVariant edit_outline_animation_idle;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "selectionOutlineAnimationShouldRun",
          Q_RETURN_ARG(QVariant, edit_outline_animation_idle)) ||
      edit_workspace->property("selectionOutlineAnimationContract").toString() !=
          QStringLiteral("visibility-gated|motion-budgeted|point-budgeted:v1") ||
      edit_workspace->property("selectionOutlineAnimationPointBudget").toInt() < 1000 ||
      edit_outline_animation_idle.toBool()) {
    return fail("edit workspace should gate marching-ants animation by visibility, motion, and outline complexity");
  }
  editor_preview.setMaskPreviewMode(previous_mask_preview_mode);
  edit_workspace->setProperty("diagnosticsPanelOpen", true);
  QCoreApplication::processEvents();
  if (mask_creation_toolbar == nullptr || mask_tool_rail == nullptr ||
      mask_tool_rail_toggle_button == nullptr || mask_tool_rail_section_label == nullptr ||
      mask_creation_vocabulary_label == nullptr ||
      mask_source_object_button == nullptr ||
      mask_source_subject_button == nullptr || mask_source_sky_button == nullptr ||
      mask_source_background_button == nullptr ||
      mask_source_brush_add_button == nullptr || mask_source_brush_erase_button == nullptr ||
      mask_source_ellipse_button == nullptr || mask_source_rectangle_button == nullptr ||
      mask_add_button == nullptr || mask_subtract_button == nullptr ||
      mask_intersect_button == nullptr ||
      mask_replace_button == nullptr || mask_delete_empty_button == nullptr ||
      mask_update_generated_button == nullptr ||
      mask_action_retry_button == nullptr ||
      mask_creation_done_button == nullptr || active_canvas_tool_chip == nullptr ||
      active_canvas_tool_chip_name_label == nullptr ||
      active_canvas_tool_chip_mode_label == nullptr ||
      active_canvas_tool_done_button == nullptr ||
      active_canvas_tool_cancel_button == nullptr || mask_refine_toolbar == nullptr ||
      mask_refine_toolbar_label == nullptr || mask_refine_edge_add_button == nullptr ||
      mask_refine_edge_erase_button == nullptr ||
      mask_refine_smart_select_button == nullptr ||
      mask_refine_show_original_button == nullptr ||
      mask_refine_show_refined_button == nullptr ||
      mask_refine_high_quality_preview_button == nullptr ||
      mask_full_resolution_preview_button == nullptr ||
      mask_refine_edge_preview_button == nullptr ||
      mask_refine_invert_button == nullptr ||
      mask_refine_duplicate_button == nullptr || mask_refine_duplicate_invert_button == nullptr ||
      mask_refine_duplicate_set_button == nullptr ||
      mask_refine_reset_edge_button == nullptr ||
      active_mask_toolbar_status_label == nullptr ||
      mask_stack_strip == nullptr || mask_stack_strip_label == nullptr ||
      edit_shared_mask_overlay == nullptr ||
      edit_shared_mask_overlay->property("sharedOverlayVersion").toInt() != 1 ||
      edit_mask_canvas_overlay == nullptr || edit_generic_mask_tint == nullptr ||
      edit_pan_zoom_input_handler == nullptr ||
      edit_pan_zoom_input_handler->property("inputHandlerRole").toString() != QStringLiteral("pan-zoom") ||
      edit_pan_zoom_input_handler->property("pointerGrabPolicy").toString() != QStringLiteral("defer-to-mask-overlay") ||
      !mask_creation_toolbar->property("visible").toBool() ||
      mask_creation_toolbar->property("sourceCreationContract").toString() !=
          QStringLiteral("selection-top-toolbar:v2|source-creation:false|combine:true|status:true|done:true|owner:top-context-toolbar") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "selectionCreationSurfaceSignature",
          Q_RETURN_ARG(QVariant, selection_creation_surface_signature)) ||
      selection_creation_surface_signature.toString() !=
          QStringLiteral(
              "selection-creation-surfaces:v2|owner:top-tool-rail|rail:true|rail-expanded:false|rail-creation-surfaces:1|rail-visible-entries:4|top-source-creation:false|top-combine-toolbar:true|legacy-source-buttons:hidden|retouch-in-selection:true|visible-creation-surfaces:1") ||
      mask_creation_vocabulary_label->property("text").toString() != QStringLiteral("Selection") ||
      !mask_creation_vocabulary_label->property("vocabularyTooltip").toString().contains(QStringLiteral("Retouch")) ||
      !mask_creation_vocabulary_label->property("vocabularyTooltip").toString().contains(QStringLiteral("Selection group")) ||
      !mask_creation_vocabulary_label->property("vocabularyTooltip").toString().contains(QStringLiteral("Component")) ||
      !mask_creation_vocabulary_label->property("vocabularyTooltip").toString().contains(QStringLiteral("Operation")) ||
      !mask_creation_vocabulary_label->property("vocabularyTooltip").toString().contains(QStringLiteral("Overlay")) ||
      !mask_tool_rail->property("visible").toBool() ||
      mask_tool_rail->property("layoutContract").toString() !=
          QStringLiteral("top-tool-rail|icon-first|popover-menus:true|surface:transparent") ||
      !mask_tool_rail->property("compactLabelsHidden").toBool() ||
      mask_tool_rail->property("width").toDouble() > 200.0 ||
      mask_selection_menu_button == nullptr ||
      !mask_selection_menu_button->property("visible").toBool() ||
      !mask_selection_menu_button->property("menuEnabled").toBool() ||
      mask_selection_menu_button->property("menuItems").toList().size() != 4 ||
      mask_selection_menu_button->property("resolvedAccessibleName").toString().isEmpty() ||
      mask_brush_shape_menu_button == nullptr ||
      !mask_brush_shape_menu_button->property("visible").toBool() ||
      !mask_brush_shape_menu_button->property("menuEnabled").toBool() ||
      mask_brush_shape_menu_button->property("menuItems").toList().size() != 10 ||
      mask_brush_shape_menu_button->property("resolvedAccessibleName").toString().isEmpty() ||
      mask_range_menu_button == nullptr ||
      !mask_range_menu_button->property("visible").toBool() ||
      !mask_range_menu_button->property("menuEnabled").toBool() ||
      mask_range_menu_button->property("menuItems").toList().size() != 3 ||
      mask_range_menu_button->property("resolvedAccessibleName").toString().isEmpty() ||
      mask_retouch_menu_button == nullptr ||
      !mask_retouch_menu_button->property("visible").toBool() ||
      !mask_retouch_menu_button->property("menuEnabled").toBool() ||
      mask_retouch_menu_button->property("menuItems").toList().size() != 6 ||
      mask_retouch_menu_button->property("resolvedAccessibleName").toString().isEmpty() ||
      mask_tool_rail_toggle_button->property("text").toString() != QStringLiteral("Tools") ||
      !mask_tool_rail_toggle_button->property("tooltip").toString().contains(QStringLiteral("Expand")) ||
      mask_tool_rail_section_label->property("visible").toBool() ||
      mask_creation_toolbar->property("height").toDouble() > 44.0 ||
      mask_source_object_button->property("text").toString() != QStringLiteral("Object") ||
      mask_source_subject_button->property("text").toString() != QStringLiteral("Subject") ||
      mask_source_sky_button->property("text").toString() != QStringLiteral("Sky") ||
      mask_source_background_button->property("text").toString() !=
          QStringLiteral("Background") ||
      !mask_source_object_button->property("tooltip").toString().contains(editor_preview.smartSelectionRuntimeLabel()) ||
      !mask_source_subject_button->property("tooltip").toString().contains(editor_preview.smartSelectionRuntimeLabel()) ||
      !mask_source_sky_button->property("tooltip").toString().contains(QStringLiteral("not semantic sky segmentation")) ||
      !mask_source_background_button->property("tooltip").toString().contains(QStringLiteral("not a separate background model")) ||
      mask_source_brush_add_button->property("text").toString() != QStringLiteral("Brush +") ||
      mask_source_brush_erase_button->property("text").toString() != QStringLiteral("Erase") ||
      mask_refine_smart_select_button->property("text").toString() != QStringLiteral("Smart") ||
      mask_source_ellipse_button->property("text").toString() != QStringLiteral("Ellipse") ||
      mask_source_rectangle_button->property("text").toString() != QStringLiteral("Rect") ||
      !mask_source_ellipse_button->property("tooltip").toString().contains(QStringLiteral("ellipse")) ||
      !mask_source_rectangle_button->property("tooltip").toString().contains(QStringLiteral("rectangle")) ||
      !mask_source_brush_add_button->property("tooltip").toString().contains(QStringLiteral("additive")) ||
      !mask_source_brush_erase_button->property("tooltip").toString().contains(QStringLiteral("subtractive")) ||
      !mask_add_button->property("visible").toBool() ||
      !mask_replace_button->property("visible").toBool() ||
      mask_delete_empty_button->property("visible").toBool() ||
      mask_update_generated_button->property("visible").toBool() ||
      mask_action_retry_button->property("visible").toBool() ||
      !mask_creation_done_button->property("visible").toBool() ||
      !active_canvas_tool_chip->property("layoutContract").toString().contains(QStringLiteral("tool-hud:v2")) ||
      !active_canvas_tool_chip->property("layoutContract").toString().contains(QStringLiteral("stable-height:true")) ||
      !active_canvas_tool_chip->property("layoutContract").toString().contains(QStringLiteral("fixed-actions:true")) ||
      !active_canvas_tool_chip->property("layoutContract").toString().contains(QStringLiteral("keyboard:enter-escape")) ||
      !active_canvas_tool_chip->property("layoutContract").toString().contains(QStringLiteral("no-elide:true")) ||
      !active_canvas_tool_chip->property("readableTextContract").toString().contains(QStringLiteral("ellipsis:none")) ||
      active_canvas_tool_chip->property("visible").toBool() ||
      !mask_refine_toolbar->property("visible").toBool() ||
      mask_refine_toolbar_label->property("text").toString() != QStringLiteral("Refine") ||
      mask_refine_edge_add_button->property("text").toString() != QStringLiteral("Add") ||
      mask_refine_edge_erase_button->property("text").toString() != QStringLiteral("Erase") ||
      !mask_refine_edge_add_button->property("tooltip").toString().contains(QStringLiteral("Brush Refine")) ||
      !mask_refine_edge_erase_button->property("tooltip").toString().contains(QStringLiteral("Brush Refine")) ||
      mask_refine_show_original_button->property("text").toString() != QStringLiteral("Original") ||
      mask_refine_show_refined_button->property("text").toString() != QStringLiteral("Refined") ||
      !mask_refine_show_refined_button->property("active").toBool() ||
      mask_refine_high_quality_preview_button->property("text").toString() != QStringLiteral("HQ") ||
      mask_refine_high_quality_preview_button->property("active").toBool() ||
      mask_refine_edge_preview_button->property("text").toString() != QStringLiteral("Edge") ||
      mask_refine_edge_preview_button->property("active").toBool() ||
      mask_refine_invert_button->property("text").toString() != QStringLiteral("Invert") ||
      mask_refine_duplicate_button->property("text").toString() != QStringLiteral("Copy") ||
      mask_refine_duplicate_invert_button->property("text").toString() !=
          QStringLiteral("Copy Inv") ||
      mask_refine_duplicate_set_button->property("text").toString() != QStringLiteral("Copy All") ||
      !mask_refine_duplicate_button->property("tooltip").toString().contains(QStringLiteral("new id")) ||
      !mask_refine_duplicate_invert_button->property("tooltip").toString().contains(QStringLiteral("background")) ||
      !mask_refine_duplicate_set_button->property("tooltip").toString().contains(QStringLiteral("whole active selection group")) ||
      mask_refine_reset_edge_button->property("text").toString() != QStringLiteral("Reset") ||
      !mask_refine_reset_edge_button->property("tooltip").toString().contains(QStringLiteral("preserving the selection geometry")) ||
      mask_stack_strip->property("visible").toBool() ||
      mask_stack_strip_label->property("text").toString() != QStringLiteral("Selections") ||
      !edit_mask_canvas_overlay->property("visible").toBool() ||
      !editor_preview.activeMaskPreviewSource().isEmpty() ||
      edit_generic_mask_tint->property("visible").toBool() ||
      edit_generic_mask_tint->property("targetOpacity").toDouble() > 0.20 ||
      edit_mask_canvas_overlay->property("previewOverlayOpacity").toDouble() > 0.45 ||
      edit_mask_canvas_overlay->property("selectionOverlayOpacity").toDouble() > 0.68 ||
      edit_mask_canvas_overlay->property("geometricOverlayOpacity").toDouble() > 0.56 ||
      edit_mask_canvas_overlay->property("geometricFillOpacity").toDouble() > 0.12 ||
      edit_mask_canvas_overlay->property("geometricStrokeOpacity").toDouble() > 0.78 ||
      !mask_intersect_button->property("active").toBool() ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Linear selection")) ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Intersect")) ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Normal")) ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Outline: Alpha")) ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Target:")) ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Adjustment layer")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskCreationContextSignature",
          Q_RETURN_ARG(QVariant, mask_creation_context_signature)) ||
      mask_creation_context_signature.toString() !=
          QStringLiteral("component:Linear selection|operation:Intersect|state:Normal|targetGroup:Linear Mask 1|target:Adjustment layer") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskCreateRefineSplitSignature",
          Q_RETURN_ARG(QVariant, mask_create_refine_split_signature)) ||
      mask_create_refine_split_signature.toString() !=
          QStringLiteral("create:true|refine:true|active:true|role:selection") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskRefinementPreviewSignature",
          Q_RETURN_ARG(QVariant, mask_refinement_preview_signature)) ||
      mask_refinement_preview_signature.toString() !=
          QStringLiteral("refinement-preview:refined|quality:normal|full:false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setStudioRefinementPreviewMode",
          Q_RETURN_ARG(QVariant, mask_refinement_preview_original),
          Q_ARG(QVariant, QStringLiteral("original"))) ||
      !mask_refinement_preview_original.toBool() ||
      editor_preview.maskRefinementPreviewMode() != QStringLiteral("original") ||
      !mask_refine_show_original_button->property("active").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setStudioRefinementPreviewMode",
          Q_RETURN_ARG(QVariant, mask_refinement_preview_refined),
          Q_ARG(QVariant, QStringLiteral("refined"))) ||
      !mask_refinement_preview_refined.toBool() ||
      editor_preview.maskRefinementPreviewMode() != QStringLiteral("refined") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setStudioRefinementHighQualityPreview",
          Q_RETURN_ARG(QVariant, mask_refinement_high_quality_enabled),
          Q_ARG(QVariant, true)) ||
      !mask_refinement_high_quality_enabled.toBool() ||
      !editor_preview.maskRefinementHighQualityPreview() ||
      editor_preview.activeMaskPreviewQualityTier() != QStringLiteral("high") ||
      !mask_refine_high_quality_preview_button->property("active").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setStudioFullResolutionMaskPreview",
          Q_RETURN_ARG(QVariant, mask_full_resolution_preview_enabled),
          Q_ARG(QVariant, true)) ||
      !mask_full_resolution_preview_enabled.toBool() ||
      !editor_preview.maskFullResolutionPreview() ||
      editor_preview.activeMaskPreviewQualityTier() != QStringLiteral("full") ||
      !mask_full_resolution_preview_button->property("active").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskActionRetrySignature",
          Q_RETURN_ARG(QVariant, mask_action_retry_signature)) ||
      mask_action_retry_signature.toString() !=
          QStringLiteral("available:false|label:|detail:") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskWorkChromeSignature",
          Q_RETURN_ARG(QVariant, mask_work_chrome_signature)) ||
      mask_work_chrome_signature.toString() !=
          QStringLiteral("maskWork:true|chromeReduced:true|diagnosticsPanel:false") ||
      editor_preview.maskCount() != initial_mask_count + 1 ||
      editor_preview.activeMaskOperation() != QStringLiteral("intersect") ||
      editor_preview.maskPreviewMode() != QStringLiteral("overlay")) {
    std::cerr << "Mask creation smoke state:"
              << " stackStrip=" << (mask_stack_strip == nullptr ? "null" : "present")
              << " stackVisible="
              << (mask_stack_strip != nullptr && mask_stack_strip->property("visible").toBool())
              << " overlay=" << (edit_mask_canvas_overlay == nullptr ? "null" : "present")
              << " overlayVisible="
              << (edit_mask_canvas_overlay != nullptr &&
                  edit_mask_canvas_overlay->property("visible").toBool())
              << " genericTintVisible="
              << (edit_generic_mask_tint != nullptr && edit_generic_mask_tint->property("visible").toBool())
              << " previewEmpty=" << editor_preview.activeMaskPreviewSource().isEmpty()
              << " toolbarText="
              << (active_mask_toolbar_status_label == nullptr
                      ? std::string()
                      : active_mask_toolbar_status_label->property("text").toString().toStdString())
              << " contextSig=" << mask_creation_context_signature.toString().toStdString()
              << " previewOpacity="
              << (edit_mask_canvas_overlay == nullptr
                      ? -1.0
                      : edit_mask_canvas_overlay->property("previewOverlayOpacity").toDouble())
              << " selectionOpacity="
              << (edit_mask_canvas_overlay == nullptr
                      ? -1.0
                      : edit_mask_canvas_overlay->property("selectionOverlayOpacity").toDouble())
              << " geometricOpacity="
              << (edit_mask_canvas_overlay == nullptr
                      ? -1.0
                      : edit_mask_canvas_overlay->property("geometricOverlayOpacity").toDouble())
              << " fillOpacity="
              << (edit_mask_canvas_overlay == nullptr
                      ? -1.0
                      : edit_mask_canvas_overlay->property("geometricFillOpacity").toDouble())
              << " strokeOpacity="
              << (edit_mask_canvas_overlay == nullptr
                      ? -1.0
                      : edit_mask_canvas_overlay->property("geometricStrokeOpacity").toDouble())
              << " maskCount=" << editor_preview.maskCount()
              << " activeOp=" << editor_preview.activeMaskOperation().toStdString()
              << " previewMode=" << editor_preview.maskPreviewMode().toStdString() << "\n";
    return fail("mask creation should apply the selected combine mode and immediately show the mask in the stack and on canvas");
  }
  const auto mask_count_before_object_pick = editor_preview.maskCount();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("object"))))) {
    return fail("Studio Object source should be invokable from the selection rail");
  }
  QCoreApplication::processEvents();
  if (!edit_workspace->property("objectPickArmed").toBool() ||
      !edit_workspace->property("objectPickCreateNew").toBool() ||
      edit_workspace->property("objectPickMode").toString() != QStringLiteral("selection") ||
      editor_preview.maskCount() != mask_count_before_object_pick ||
      !active_canvas_tool_chip->property("visible").toBool() ||
      active_canvas_tool_chip_name_label->property("text").toString() != QStringLiteral("Object") ||
      !active_canvas_tool_chip_mode_label->property("text").toString().contains(
          QStringLiteral("Hover or drag")) ||
      active_canvas_tool_done_button->property("enabled").toBool() ||
      !active_canvas_tool_cancel_button->property("enabled").toBool()) {
    return fail("Studio Object selection should arm a cancelable pre-commit tool without creating a placeholder mask");
  }
  QVariant object_pick_cancelled;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "cancelActiveTool",
          Q_RETURN_ARG(QVariant, object_pick_cancelled)) ||
      !object_pick_cancelled.toBool() ||
      edit_workspace->property("objectPickArmed").toBool() ||
      edit_workspace->property("objectPickCreateNew").toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("layers") ||
      active_canvas_tool_chip->property("visible").toBool() ||
      editor_preview.maskCount() != mask_count_before_object_pick) {
    return fail("Studio Object selection Cancel/Escape should exit the armed tool without changing masks");
  }
  const QVector<QObject*> compact_mask_label_buttons = {
      mask_source_object_button,
      mask_source_subject_button,
      mask_source_sky_button,
      mask_source_background_button,
      mask_source_brush_add_button,
      mask_source_brush_erase_button,
      mask_source_ellipse_button,
      mask_source_rectangle_button,
      mask_refine_smart_select_button,
      mask_refine_show_original_button,
      mask_refine_show_refined_button,
      mask_refine_high_quality_preview_button,
      mask_refine_edge_preview_button,
      mask_refine_duplicate_button,
      mask_refine_duplicate_invert_button,
      mask_refine_duplicate_set_button,
      mask_refine_reset_edge_button,
  };
  for (const auto* button : compact_mask_label_buttons) {
    const auto text = button->property("text").toString();
    const bool exposes_set_word =
        text.compare(QStringLiteral("Set"), Qt::CaseInsensitive) == 0 ||
        text.contains(QStringLiteral(" Set"), Qt::CaseInsensitive) ||
        text.contains(QStringLiteral("Set "), Qt::CaseInsensitive);
    if (text.size() > 10 ||
        text.contains(QStringLiteral("Source"), Qt::CaseInsensitive) ||
        exposes_set_word ||
        text.contains(QStringLiteral("Stack"), Qt::CaseInsensitive)) {
      return fail("primary masking labels should stay compact and avoid technical source/set/stack wording");
    }
  }
  const auto mask_refine_toolbar_text = mask_refine_toolbar_label->property("text").toString();
  const bool toolbar_exposes_set_word =
      mask_refine_toolbar_text.compare(QStringLiteral("Set"), Qt::CaseInsensitive) == 0 ||
      mask_refine_toolbar_text.contains(QStringLiteral(" Set"), Qt::CaseInsensitive) ||
      mask_refine_toolbar_text.contains(QStringLiteral("Set "), Qt::CaseInsensitive);
  if (mask_refine_toolbar_text.size() > 10 ||
      mask_refine_toolbar_text.contains(QStringLiteral("Source"), Qt::CaseInsensitive) ||
      toolbar_exposes_set_word ||
      mask_refine_toolbar_text.contains(QStringLiteral("Stack"), Qt::CaseInsensitive)) {
    return fail("primary masking labels should stay compact and avoid technical source/set/stack wording");
  }
  QVariant edit_overlay_geometry_signature;
  if (!QMetaObject::invokeMethod(
          edit_shared_mask_overlay,
          "overlayGeometrySignature",
          Q_RETURN_ARG(QVariant, edit_overlay_geometry_signature)) ||
      edit_shared_mask_overlay->property("overlayGeometryContract").toString() !=
          QStringLiteral("image-rect-clipped|crop-rotation-aware:v1") ||
      !edit_overlay_geometry_signature.toString().startsWith(
          QStringLiteral("image-rect-clipped|crop-rotation-aware:v1|clip:true|")) ||
      !edit_overlay_geometry_signature.toString().contains(QStringLiteral("|crop:")) ||
      !edit_overlay_geometry_signature.toString().contains(QStringLiteral("|rotation:"))) {
    return fail("edit shared mask overlay should be clipped to the image rect and expose crop/rotation-aware geometry");
  }
  editor_preview.setMaskRefinementHighQualityPreview(false);
  editor_preview.setMaskPreviewMode(QStringLiteral("refinement-edge"));
  QCoreApplication::processEvents();
  if (editor_preview.maskPreviewMode() != QStringLiteral("overlay") ||
      mask_refine_edge_preview_button->property("active").toBool() ||
      mask_refine_edge_preview_button->property("visible").toBool() ||
      mask_refine_edge_preview_button->property("enabled").toBool()) {
    return fail("Refine Edge diagnostic preview should be hidden and stale refinement-edge requests should fall back to clean overlay mode");
  }
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  edit_workspace->setProperty("diagnosticsPanelOpen", false);
  QCoreApplication::processEvents();
  const auto active_mask_set_name = editor_preview.activeMaskSetName();
  if (active_mask_set_name.isEmpty() ||
      !active_mask_toolbar_status_label->property("text").toString().contains(active_mask_set_name) ||
      !mask_add_button->property("tooltip").toString().contains(active_mask_set_name) ||
      !mask_subtract_button->property("tooltip").toString().contains(active_mask_set_name) ||
      !mask_intersect_button->property("tooltip").toString().contains(active_mask_set_name) ||
      !mask_replace_button->property("tooltip").toString().contains(active_mask_set_name)) {
    return fail("mask combine controls should name the target mask set for every operation");
	  }
	  const auto mask_count_before_layout_proof = editor_preview.maskCount();
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_RUN_MASK_LAYOUT_PROOF") &&
      !qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_SKIP_MASK_LAYOUT_PROOF")) {
	  if (!QMetaObject::invokeMethod(
	          edit_workspace,
	          "setMaskCreationOperation",
          Q_ARG(QVariant, QVariant(QStringLiteral("add")))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("radial"))))) {
    return fail("mask row layout proof should be able to add a second source to the active mask set");
  }
  editor_preview.deselectActiveMask();
  editor_preview.addMask(QStringLiteral("rectangle"));
  if (editor_preview.maskCount() <= mask_count_before_layout_proof + 1) {
    return fail("mask row layout proof should be able to add a second mask set");
  }
  QQmlComponent mask_view_layout_component(
      &engine,
      QUrl::fromLocalFile(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                              .filePath(QStringLiteral("apps/desktop/qml/workspaces/MaskView.qml"))));
  if (mask_view_layout_component.isError()) {
    for (const auto& error : mask_view_layout_component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return fail("mask row layout proof should load MaskView.qml");
  }
  QObject* mask_view_layout_object = mask_view_layout_component.create(engine.rootContext());
  auto* mask_view_layout_item = qobject_cast<QQuickItem*>(mask_view_layout_object);
  if (mask_view_layout_item == nullptr) {
    delete mask_view_layout_object;
    return fail("mask row layout proof should instantiate MaskView as a QQuickItem");
  }
  mask_view_layout_item->setParentItem(root_window->contentItem());
  mask_view_layout_item->setVisible(true);
  mask_view_layout_item->setWidth(784.0);
  mask_view_layout_item->setHeight(900.0);
  process_layout_events(12);
  auto* mask_layout_split = find_named_item(mask_view_layout_item, QStringLiteral("maskWorkspaceStableSplit"));
  auto* mask_layout_canvas = find_named_item(mask_view_layout_item, QStringLiteral("maskCanvasPane"));
  auto* mask_layout_inspector = find_named_item(mask_view_layout_item, QStringLiteral("maskInspectorPane"));
  auto* mask_layout_scroll = find_named_item(mask_view_layout_item, QStringLiteral("maskInspectorScrollView"));
  auto* mask_view_pointer_layer =
      find_named_item(mask_view_layout_item, QStringLiteral("maskViewPointerHandlerLayer"));
  auto* mask_view_hover_handler =
      find_named_object(mask_view_layout_item, QStringLiteral("maskViewToolHoverHandler"));
  auto* mask_view_smart_point_handler =
      find_named_object(mask_view_layout_item, QStringLiteral("maskViewSmartSelectPointHandler"));
  auto* mask_view_lasso_point_handler =
      find_named_object(mask_view_layout_item, QStringLiteral("maskViewLassoPointHandler"));
  auto* mask_view_polygon_tap_handler =
      find_named_object(mask_view_layout_item, QStringLiteral("maskViewPolygonTapHandler"));
  auto* mask_view_shared_overlay =
      find_named_item(mask_view_layout_item, QStringLiteral("maskViewSharedMaskOverlay"));
  auto* mask_view_canvas_overlay =
      find_named_item(mask_view_layout_item, QStringLiteral("maskViewMaskCanvasOverlay"));
  auto* mask_view_geometric_canvas =
      find_named_item(mask_view_layout_item, QStringLiteral("maskViewGeometricMaskCanvas"));
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  QCoreApplication::processEvents();
  QVariant mask_view_outline_animation_idle;
  QVariant mask_view_overlay_geometry_signature;
  QVariant mask_view_workflow_polish_signature;
  if (!QMetaObject::invokeMethod(
          mask_view_layout_item,
          "selectionOutlineAnimationShouldRun",
          Q_RETURN_ARG(QVariant, mask_view_outline_animation_idle))) {
    return fail("mask workspace should expose a testable marching-ants animation gate");
  }
  if (!QMetaObject::invokeMethod(
          mask_view_layout_item,
          "maskWorkflowPolishSignature",
          Q_RETURN_ARG(QVariant, mask_view_workflow_polish_signature))) {
    return fail("mask workspace should expose a live workflow-polish signature");
  }
  if (mask_view_shared_overlay != nullptr &&
      !QMetaObject::invokeMethod(
          mask_view_shared_overlay,
          "overlayGeometrySignature",
          Q_RETURN_ARG(QVariant, mask_view_overlay_geometry_signature))) {
    return fail("mask workspace shared overlay should expose a testable geometry signature");
  }
  const auto mask_layout_width =
      mask_view_layout_item->property("minimumCanvasWidth").toInt() +
      mask_view_layout_item->property("stableInspectorWidth").toInt() + 14;
  QString mask_row_overlap_detail;
  const bool mask_rows_layout_ok =
      mask_view_layout_item->width() == mask_layout_width &&
      item_visible(mask_layout_split) && item_visible(mask_layout_canvas) &&
      item_visible(mask_layout_inspector) && item_visible(mask_layout_scroll) &&
      mask_view_pointer_layer != nullptr &&
      mask_view_hover_handler != nullptr &&
      mask_view_smart_point_handler != nullptr &&
      mask_view_smart_point_handler->property("pointerGrabPolicy").toString() ==
          QStringLiteral("exclusive-smart-selection-stroke") &&
      mask_view_lasso_point_handler != nullptr &&
      mask_view_lasso_point_handler->property("pointerGrabPolicy").toString() ==
          QStringLiteral("exclusive-lasso-drag") &&
      mask_view_polygon_tap_handler != nullptr &&
      mask_view_polygon_tap_handler->property("pointerGrabPolicy").toString() ==
          QStringLiteral("exclusive-polygon-tap") &&
      mask_view_shared_overlay != nullptr &&
      mask_view_shared_overlay->property("sharedOverlayVersion").toInt() == 1 &&
      mask_view_shared_overlay->property("overlayGeometryContract").toString() ==
          QStringLiteral("image-rect-clipped|crop-rotation-aware:v1") &&
      mask_view_overlay_geometry_signature.toString().startsWith(
          QStringLiteral("image-rect-clipped|crop-rotation-aware:v1|clip:true|")) &&
      mask_view_canvas_overlay != nullptr &&
      mask_view_canvas_overlay->property("outlineRenderContract").toString() ==
          QStringLiteral("anti-aliased|zoom-stable|pixel-aligned:v1") &&
      mask_view_geometric_canvas != nullptr &&
      mask_view_geometric_canvas->property("outlineRenderContract").toString() ==
          QStringLiteral("anti-aliased|zoom-stable|pixel-aligned:v1") &&
      mask_view_layout_item->property("selectionOutlineAnimationContract").toString() ==
          QStringLiteral("visibility-gated|motion-budgeted|point-budgeted:v1") &&
      mask_view_layout_item->property("maskWorkflowPolishContract").toString() ==
          QStringLiteral("mask-workflow-polish:v1|first-glance:canvas-active-selection-inspector-source-tools|close-inspection:context-source-operation-preview-refine-pending-actions|clutter:provider-details-diagnostic-only-one-active-context|interaction:create-select-refine-combine-preview-rename-reorder-delete|text:no-elide-no-overlap|functionality:preserved") &&
      mask_view_layout_item->property("maskPageCompletionContract").toString() ==
          QStringLiteral("page-completion:v1|masks|sidebar:source-variant-controls-search|content:canvas-overlay-brush-lasso-polygon-handles|inspector:active-context-mask-stack-refine-adjustments|toolbar:combine-preview-import-retry-apply-cancel|states:no-mask-active-mask-generating-locked-empty-error") &&
      mask_view_workflow_polish_signature.toString().contains(
          QStringLiteral("mask-workflow-polish:v1|asset:true|context:refine|active:true")) &&
      mask_view_workflow_polish_signature.toString().contains(QStringLiteral("|preview:overlay|")) &&
      mask_view_workflow_polish_signature.toString().contains(QStringLiteral("|inspector:true|canvas:true|source-list:true")) &&
      mask_view_workflow_polish_signature.toString().contains(
          QStringLiteral("|preserve:object-subject-sky-background-brush-lasso-polygon-gradient-range-alpha-import-combine-preview-invert-refine-rename-reorder-duplicate-delete")) &&
      mask_view_layout_item->property("selectionOutlineAnimationPointBudget").toInt() >= 1000 &&
      !mask_view_outline_animation_idle.toBool() &&
      mask_layout_canvas->width() >= mask_view_layout_item->property("minimumCanvasWidth").toDouble() &&
      std::abs(mask_layout_inspector->width() -
               mask_view_layout_item->property("stableInspectorWidth").toDouble()) <= 1.0 &&
      visible_named_item_instances_do_not_overlap(
          mask_view_layout_item, QStringLiteral("maskSetFocusRow"), 2, &mask_row_overlap_detail) &&
      visible_named_item_instances_do_not_overlap(
          mask_view_layout_item, QStringLiteral("maskSourceFocusRow"), 3, &mask_row_overlap_detail);
  const auto actual_mask_layout_width = mask_view_layout_item->width();
  mask_view_layout_item->setVisible(false);
  mask_view_layout_item->setParentItem(nullptr);
  delete mask_view_layout_object;
  int mask_layout_cleanup_guard = 0;
  while (editor_preview.maskCount() > mask_count_before_layout_proof &&
         mask_layout_cleanup_guard < 64) {
    ++mask_layout_cleanup_guard;
    editor_preview.setActiveMaskIndex(editor_preview.maskCount() - 1);
    editor_preview.removeActiveMask();
  }
  if (editor_preview.maskCount() > mask_count_before_layout_proof) {
    return fail("mask row layout proof cleanup should remove temporary masks without hanging");
  }
  if (mask_count_before_layout_proof > 0) {
    editor_preview.setActiveMaskIndex(mask_count_before_layout_proof - 1);
  }
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  edit_workspace->setProperty("editTool", QStringLiteral("masks"));
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  process_layout_events(6);
  QVariant studio_mask_workflow_polish_signature;
  if (edit_workspace->property("maskWorkflowPolishContract").toString() !=
          QStringLiteral("mask-workflow-polish:v1|first-glance:tool-rail-mask-sources-canvas-overlay-inspector-stack|close-inspection:active-context-source-operation-preview-refine-pending-actions|clutter:provider-details-diagnostic-only-one-active-context|interaction:create-select-refine-combine-preview-rename-reorder-delete|text:no-elide-no-overlap|functionality:preserved") ||
      edit_workspace->property("maskPageCompletionContract").toString() !=
          QStringLiteral("page-completion:v1|masks-in-edit|rail:source-variant-controls|content:image-overlay-brush-lasso-polygon-hud|inspector:mask-first-controls-stack-layers|toolbar:combine-preview-import-retry-apply-cancel|states:no-mask-active-mask-generating-locked-empty-error") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskWorkflowPolishSignature",
          Q_RETURN_ARG(QVariant, studio_mask_workflow_polish_signature)) ||
      !studio_mask_workflow_polish_signature.toString().contains(
          QStringLiteral("mask-workflow-polish:v1|asset:true|tool:masks|active:true")) ||
      !studio_mask_workflow_polish_signature.toString().contains(QStringLiteral("|preview:overlay|")) ||
      !studio_mask_workflow_polish_signature.toString().contains(QStringLiteral("|target:mask|")) ||
      !studio_mask_workflow_polish_signature.toString().contains(QStringLiteral("|context:tool:masks|mask:true|brush:false|retouch:false|count:1")) ||
      !studio_mask_workflow_polish_signature.toString().contains(QStringLiteral("|create-refine:create:true|refine:true|active:true")) ||
      !studio_mask_workflow_polish_signature.toString().contains(QStringLiteral("|inspector:active:true|mode:overlay")) ||
      !studio_mask_workflow_polish_signature.toString().contains(
          QStringLiteral("|preserve:object-subject-sky-background-brush-lasso-polygon-gradient-range-alpha-import-combine-preview-invert-refine-rename-reorder-duplicate-delete"))) {
    std::cerr << "Studio mask workflow polish: "
              << studio_mask_workflow_polish_signature.toString().toStdString() << '\n';
    return fail("Studio mask workflow should expose live source, operation, preview, inspector, pending, and preserved-action state");
  }
  if (!mask_rows_layout_ok || editor_preview.maskCount() != mask_count_before_layout_proof) {
    std::cerr << "Mask row layout proof detail: "
              << mask_row_overlap_detail.toStdString()
              << " width=" << actual_mask_layout_width
              << " expectedWidth=" << mask_layout_width
              << " pointerLayer=" << (mask_view_pointer_layer == nullptr ? "null" : "present")
              << " hoverHandler=" << (mask_view_hover_handler == nullptr ? "null" : "present")
              << " smartHandler=" << (mask_view_smart_point_handler == nullptr ? "null" : "present")
              << " lassoHandler=" << (mask_view_lasso_point_handler == nullptr ? "null" : "present")
              << " polygonTapHandler=" << (mask_view_polygon_tap_handler == nullptr ? "null" : "present")
              << " sharedOverlay=" << (mask_view_shared_overlay == nullptr ? "null" : "present")
              << " canvasOverlay=" << (mask_view_canvas_overlay == nullptr ? "null" : "present")
              << " geometricCanvas=" << (mask_view_geometric_canvas == nullptr ? "null" : "present")
              << " workflow=" << mask_view_workflow_polish_signature.toString().toStdString()
              << " restoredMaskCount=" << editor_preview.maskCount()
              << " expectedMaskCount=" << mask_count_before_layout_proof << '\n';
    return fail("MaskView rows should not overlap at the minimum supported canvas plus inspector width");
	  }
	  trace_smoke_progress("mask layout proof checked");
  } else {
    trace_smoke_progress("mask layout proof skipped");
  }
	  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_RUN_MASK_GEOMETRY_PROOFS") &&
          !qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_SKIP_MASK_GEOMETRY_PROOFS")) {
	  editor_preview.setActiveMaskLocked(true);
  QCoreApplication::processEvents();
  if (editor_preview.activeMaskEditDisabledReason().isEmpty() ||
      mask_refine_duplicate_button->property("enabled").toBool() ||
      mask_refine_duplicate_invert_button->property("enabled").toBool() ||
      mask_refine_invert_button->property("enabled").toBool() ||
      !mask_refine_duplicate_button->property("tooltip").toString().contains(QStringLiteral("Unlock it"))) {
    return fail("locked selection sources should disable active-source edits with a clear reason");
  }
  editor_preview.setActiveMaskLocked(false);
  QCoreApplication::processEvents();
  if (!editor_preview.activeMaskEditDisabledReason().isEmpty() ||
      !mask_refine_duplicate_button->property("enabled").toBool() ||
      !mask_refine_duplicate_invert_button->property("enabled").toBool() ||
      !mask_refine_invert_button->property("enabled").toBool()) {
    return fail("unlocking a selection source should restore active-source edit actions");
  }
  editor_preview.addMask(QStringLiteral("polygon"));
  QCoreApplication::processEvents();
  if (editor_preview.emptyMaskCount() != 1 ||
      !mask_delete_empty_button->property("visible").toBool() ||
      !mask_delete_empty_button->property("enabled").toBool() ||
      !mask_delete_empty_button->property("tooltip").toString().contains(QStringLiteral("edit recipe")) ||
      !active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Empty selections: 1")) ||
      editor_preview.deleteEmptyMasks() != 1) {
    return fail("empty selection sources should be visibly invalid and offer Delete Empty Masks");
  }
  QCoreApplication::processEvents();
  if (editor_preview.emptyMaskCount() != 0 ||
      mask_delete_empty_button->property("visible").toBool() ||
      active_mask_toolbar_status_label->property("text").toString().contains(QStringLiteral("Empty selections")) ||
      editor_preview.maskCount() != initial_mask_count + 1) {
    return fail("Delete Empty Masks should remove invalid sources and clear the toolbar warning");
  }
  auto* polygon_cancel_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("polygonCancelShortcut"));
  auto* polygon_complete_return_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("polygonCompleteReturnShortcut"));
  auto* polygon_complete_enter_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("polygonCompleteEnterShortcut"));
  auto* polygon_backspace_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("polygonRemoveLastPointBackspaceShortcut"));
  auto* polygon_delete_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("polygonRemoveLastPointDeleteShortcut"));
  auto* lasso_complete_return_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("lassoCompleteReturnShortcut"));
  auto* lasso_complete_enter_shortcut =
      edit_workspace->findChild<QObject*>(QStringLiteral("lassoCompleteEnterShortcut"));
  const auto mask_count_before_polygon_cancel = editor_preview.maskCount();
  editor_preview.startPolygonSelection(false);
  editor_preview.appendActivePolygonPoint(0.16, 0.18);
  editor_preview.appendActivePolygonPoint(0.68, 0.20);
  QCoreApplication::processEvents();
  QObject* polygon_hud = nullptr;
  QObject* polygon_hud_complete_button = nullptr;
  QObject* polygon_hud_cancel_button = nullptr;
  QObject* polygon_hud_simplify_button = nullptr;
  QObject* polygon_hud_smooth_button = nullptr;
  if (!wait_for_condition([&]() {
        polygon_hud = find_named_object(edit_workspace, QStringLiteral("polygonToolHud"));
        polygon_hud_complete_button =
            find_named_object(edit_workspace, QStringLiteral("polygonHudCompleteButton"));
        polygon_hud_cancel_button =
            find_named_object(edit_workspace, QStringLiteral("polygonHudCancelButton"));
        polygon_hud_simplify_button =
            find_named_object(edit_workspace, QStringLiteral("polygonHudSimplifyButton"));
        polygon_hud_smooth_button =
            find_named_object(edit_workspace, QStringLiteral("polygonHudSmoothButton"));
        return polygon_hud != nullptr && polygon_hud->property("visible").toBool() &&
               polygon_hud_complete_button != nullptr && polygon_hud_cancel_button != nullptr &&
               polygon_hud_simplify_button != nullptr && polygon_hud_smooth_button != nullptr;
      }) ||
      !polygon_hud->property("hudContract").toString().contains(QStringLiteral("specialized-tool-hud:v1|tool:polygon-lasso")) ||
      !polygon_hud->property("hudContract").toString().contains(QStringLiteral("point-actions:true")) ||
      !polygon_hud->property("hudContract").toString().contains(QStringLiteral("warning-state:true")) ||
      !polygon_hud->property("hudContract").toString().contains(QStringLiteral("no-elide:true")) ||
      !active_canvas_tool_chip->property("visible").toBool() ||
      active_canvas_tool_chip_name_label->property("text").toString() != QStringLiteral("Polygon") ||
      !active_canvas_tool_chip_mode_label->property("text").toString().contains(QStringLiteral("2 pts")) ||
      active_canvas_tool_done_button->property("enabled").toBool() ||
      !active_canvas_tool_cancel_button->property("enabled").toBool() ||
      polygon_hud_complete_button->property("enabled").toBool() ||
      !polygon_hud_cancel_button->property("enabled").toBool()) {
    return fail("polygon drawing mode should show cancel and a disabled active-tool Done until enough points exist");
  }
  QVariant polygon_cancelled;
  if (polygon_cancel_shortcut == nullptr || polygon_complete_return_shortcut == nullptr ||
      polygon_complete_enter_shortcut == nullptr || polygon_backspace_shortcut == nullptr ||
      polygon_delete_shortcut == nullptr || !polygon_cancel_shortcut->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "cancelActivePolygonDrawing",
          Q_RETURN_ARG(QVariant, polygon_cancelled)) ||
      !polygon_cancelled.toBool() ||
      editor_preview.maskCount() != mask_count_before_polygon_cancel) {
    return fail("Escape should cancel only the unfinished active polygon selection");
  }
  editor_preview.startPolygonSelection(false);
  editor_preview.appendActivePolygonPoint(0.16, 0.18);
  editor_preview.appendActivePolygonPoint(0.68, 0.20);
  editor_preview.appendActivePolygonPoint(0.62, 0.74);
  QCoreApplication::processEvents();
  QVariant polygon_point_removed;
  if (!polygon_complete_return_shortcut->property("enabled").toBool() ||
      !polygon_complete_enter_shortcut->property("enabled").toBool() ||
      !polygon_hud_complete_button->property("enabled").toBool() ||
      polygon_hud_cancel_button->property("enabled").toBool() ||
      polygon_hud_simplify_button->property("enabled").toBool() ||
      !polygon_hud_smooth_button->property("enabled").toBool() ||
      !polygon_backspace_shortcut->property("enabled").toBool() ||
      !polygon_delete_shortcut->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "removeLastActivePolygonPointFromShortcut",
          Q_RETURN_ARG(QVariant, polygon_point_removed)) ||
      !polygon_point_removed.toBool() ||
      editor_preview.activeMaskPolygonPointCount() != 2) {
    return fail("Backspace/Delete should remove the last active polygon point without clearing the mask");
  }
  editor_preview.appendActivePolygonPoint(0.62, 0.74);
  QVariant polygon_completed;
  if (!active_canvas_tool_done_button->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "finishActiveCanvasTool",
          Q_RETURN_ARG(QVariant, polygon_completed)) ||
      !polygon_completed.toBool() || editor_preview.activeMaskKind() != QStringLiteral("polygon") ||
      editor_preview.activeMaskPolygonPointCount() != 3) {
    return fail("Enter/Return or Done should complete a valid polygon selection without altering its points");
  }
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  process_layout_events(8);
  QQuickItem* shared_outline_hit_target = nullptr;
  QObject* shared_mask_overlay = nullptr;
  if (!wait_for_condition([&]() {
        shared_mask_overlay =
            find_named_object(edit_workspace, QStringLiteral("editImageViewportSharedMaskOverlay"));
        shared_outline_hit_target =
            find_named_item(edit_workspace, QStringLiteral("editImageViewportSharedActiveOutlineHitTarget"));
        return shared_mask_overlay != nullptr &&
               shared_outline_hit_target != nullptr &&
               item_visible(shared_outline_hit_target) &&
               editor_preview.activeMaskOutlinePaths().size() > 0;
      })) {
    std::cerr << "Canvas outline sync state: sharedOverlay="
              << (shared_mask_overlay == nullptr ? "null" : "present")
              << " hitTarget=" << (shared_outline_hit_target == nullptr ? "null" : "present")
              << " hitVisible=" << item_visible(shared_outline_hit_target)
              << " outlinePathCount=" << editor_preview.activeMaskOutlinePaths().size()
              << " hasActiveMask=" << editor_preview.hasActiveMask()
              << " previewMode=" << editor_preview.maskPreviewMode().toStdString() << '\n';
    return fail("completed polygon selections should expose a clickable shared canvas outline");
  }
  QVariant canvas_outline_base_target;
  QVariant canvas_outline_selected;
  QVariant canvas_outline_scope;
  const auto runtime_outline_paths = editor_preview.activeMaskOutlinePaths();
  QVariantList runtime_outline_path;
  if (!runtime_outline_paths.isEmpty()) {
    if (runtime_outline_paths.first().toMap().contains(QStringLiteral("x"))) {
      runtime_outline_path = runtime_outline_paths;
    } else {
      runtime_outline_path = runtime_outline_paths.first().toList();
    }
  }
  if (runtime_outline_path.size() < 2) {
    return fail("canvas outline sync proof should have at least one traced outline segment");
  }
  const auto outline_hit_start = runtime_outline_path.at(0).toMap();
  const auto outline_hit_end = runtime_outline_path.at(1).toMap();
  const auto outline_hit_normalized_x =
      (outline_hit_start.value(QStringLiteral("x")).toDouble() +
       outline_hit_end.value(QStringLiteral("x")).toDouble()) /
      2.0;
  const auto outline_hit_normalized_y =
      (outline_hit_start.value(QStringLiteral("y")).toDouble() +
       outline_hit_end.value(QStringLiteral("y")).toDouble()) /
      2.0;
  const auto outline_hit_x = shared_outline_hit_target->width() * outline_hit_normalized_x;
  const auto outline_hit_y = shared_outline_hit_target->height() * outline_hit_normalized_y;
  QVariant canvas_outline_direct_hit;
  const QVariantList runtime_outline_paths_for_qml =
      runtime_outline_paths.first().toMap().contains(QStringLiteral("x"))
          ? QVariantList{QVariant(runtime_outline_path)}
          : runtime_outline_paths;
  if (!QMetaObject::invokeMethod(
          shared_mask_overlay,
          "outlinePathsHitTest",
          Q_RETURN_ARG(QVariant, canvas_outline_direct_hit),
          Q_ARG(QVariant, outline_hit_x),
          Q_ARG(QVariant, outline_hit_y),
          Q_ARG(QVariant, runtime_outline_paths_for_qml),
          Q_ARG(QVariant, 12.0)) ||
      !canvas_outline_direct_hit.toBool()) {
    std::cerr << "Canvas outline direct hit-test state: directHit="
              << canvas_outline_direct_hit.toBool()
              << " normalizedHit=" << outline_hit_normalized_x << ","
              << outline_hit_normalized_y
              << " pixelHit=" << outline_hit_x << "," << outline_hit_y
              << " pathCount=" << runtime_outline_paths.size()
              << " segmentPointCount=" << runtime_outline_path.size() << '\n';
    return fail("shared canvas outline hit-test should accept a point on the traced outline segment");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setSelectedEditTarget",
          Q_RETURN_ARG(QVariant, canvas_outline_base_target),
          Q_ARG(QVariant, QStringLiteral("base-image")),
          Q_ARG(QVariant, -1),
          Q_ARG(QVariant, QVariantMap{}),
          Q_ARG(QVariant, -1)) ||
      !canvas_outline_base_target.toBool()) {
    return fail("canvas outline sync proof should switch away from the mask target before clicking the outline");
  }
  process_layout_events(4);
  if (!QMetaObject::invokeMethod(
          shared_mask_overlay,
          "selectActiveOutlineFromCanvas",
          Q_RETURN_ARG(QVariant, canvas_outline_selected),
          Q_ARG(QVariant, outline_hit_x),
          Q_ARG(QVariant, outline_hit_y)) ||
      !canvas_outline_selected.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetScope",
          Q_RETURN_ARG(QVariant, canvas_outline_scope)) ||
      canvas_outline_scope.toString() != QStringLiteral("mask") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("layers") ||
      !editor_preview.hasActiveMask()) {
    std::cerr << "Canvas outline selection state: selected="
              << canvas_outline_selected.toBool()
              << " scope=" << canvas_outline_scope.toString().toStdString()
              << " tool=" << edit_workspace->property("editTool").toString().toStdString()
              << " hit=" << outline_hit_x << "," << outline_hit_y
              << " hitTarget=" << shared_outline_hit_target->width() << "x"
              << shared_outline_hit_target->height() << '\n';
    return fail("clicking the shared canvas outline should reselect its mask/layer target");
  }

  auto* edit_history_tab =
      find_named_object(edit_workspace, QStringLiteral("historyInspectorTabButton"));
  auto* edit_history_panel = find_named_object(edit_workspace, QStringLiteral("editHistoryPanel"));
  auto* edit_history_undo = find_named_object(edit_workspace, QStringLiteral("editHistoryUndoButton"));
  auto* edit_history_redo = find_named_object(edit_workspace, QStringLiteral("editHistoryRedoButton"));
  auto* active_selection_outline = find_named_item(
      edit_workspace, QStringLiteral("editImageViewportSharedActiveSelectionOutline"));
  if (edit_history_tab == nullptr || edit_history_panel == nullptr || edit_history_undo == nullptr ||
      edit_history_redo == nullptr || active_selection_outline == nullptr ||
      !QMetaObject::invokeMethod(edit_history_tab, "clicked", Qt::DirectConnection)) {
    return fail("selection history should expose its tab, undo/redo controls, and rendered outline");
  }
  process_layout_events(8);
  if (!edit_history_panel->property("visible").toBool() ||
      !edit_history_undo->property("enabled").toBool() ||
      !item_visible(active_selection_outline)) {
    return fail("the committed selection should remain rendered while its edit history is visible");
  }
  const auto capture_selection_outline = [&]() {
    process_layout_events(5);
    const auto frame = root_window->grabWindow();
    if (frame.isNull())
      return QImage{};
    const auto scene_bounds = active_selection_outline->mapRectToScene(
        QRectF(0, 0, active_selection_outline->width(), active_selection_outline->height()));
    const auto scale = frame.devicePixelRatio();
    const QRect pixel_bounds(
        qRound(scene_bounds.x() * scale),
        qRound(scene_bounds.y() * scale),
        qRound(scene_bounds.width() * scale),
        qRound(scene_bounds.height() * scale));
    return frame.copy(pixel_bounds.intersected(frame.rect()));
  };
  const auto significant_pixel_difference = [](const QImage& first, const QImage& second) {
    if (first.isNull() || second.isNull() || first.size() != second.size())
      return -1;
    auto changed = 0;
    for (auto y = 0; y < first.height(); ++y) {
      for (auto x = 0; x < first.width(); ++x) {
        const auto a = first.pixelColor(x, y);
        const auto b = second.pixelColor(x, y);
        const auto distance = std::abs(a.red() - b.red()) +
                              std::abs(a.green() - b.green()) +
                              std::abs(a.blue() - b.blue());
        if (distance > 36)
          ++changed;
      }
    }
    return changed;
  };
  const auto committed_selection_pixels = capture_selection_outline();
  if (committed_selection_pixels.isNull() ||
      !QMetaObject::invokeMethod(edit_history_undo, "clicked", Qt::DirectConnection) ||
      !wait_for_condition([&]() { return editor_preview.maskCount() == mask_count_before_polygon_cancel; })) {
    return fail("History Undo should remove the committed polygon selection");
  }
  const auto undone_selection_pixels = capture_selection_outline();
  if (!edit_history_redo->property("enabled").toBool() ||
      significant_pixel_difference(committed_selection_pixels, undone_selection_pixels) < 40 ||
      !QMetaObject::invokeMethod(edit_history_redo, "clicked", Qt::DirectConnection) ||
      !wait_for_condition(
          [&]() { return editor_preview.maskCount() == mask_count_before_polygon_cancel + 1; }) ||
      !wait_for_condition([&]() { return item_visible(active_selection_outline); })) {
    return fail("Undo/Redo should remove and restore the rendered selection through the visible history controls");
  }
  const auto redone_selection_pixels = capture_selection_outline();
  if (significant_pixel_difference(committed_selection_pixels, redone_selection_pixels) > 500) {
    return fail("Redo should restore the same rendered polygon selection state");
  }
  editor_preview.removeActiveMask();
  QCoreApplication::processEvents();
  if (editor_preview.maskCount() != mask_count_before_polygon_cancel) {
    return fail("polygon shortcut smoke cleanup should preserve unrelated masks");
  }
  editor_preview.startPolygonSelection(false);
  editor_preview.appendActivePolygonPoint(0.18, 0.18);
  editor_preview.appendActivePolygonPoint(0.72, 0.22);
  editor_preview.appendActivePolygonPoint(0.58, 0.78);
  QCoreApplication::processEvents();
  QObject* polygon_snap_close_target = nullptr;
  if (!wait_for_condition([&]() {
        polygon_snap_close_target =
            find_named_object(edit_workspace, QStringLiteral("polygonSnapCloseTarget"));
        return polygon_snap_close_target != nullptr &&
               polygon_snap_close_target->property("visible").toBool();
      })) {
    return fail("polygon drawing mode should show a larger snap-to-first close target");
  }
  QVariant far_polygon_snap;
  QVariant near_polygon_completed;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activePolygonSnapClose",
          Q_RETURN_ARG(QVariant, far_polygon_snap),
          Q_ARG(QVariant, QVariant(0.40)),
          Q_ARG(QVariant, QVariant(0.40)),
          Q_ARG(QVariant, QVariant(1000)),
          Q_ARG(QVariant, QVariant(1000))) ||
      far_polygon_snap.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "completeActivePolygonIfSnappingToFirst",
          Q_RETURN_ARG(QVariant, near_polygon_completed),
          Q_ARG(QVariant, QVariant(0.192)),
          Q_ARG(QVariant, QVariant(0.18)),
          Q_ARG(QVariant, QVariant(1000)),
          Q_ARG(QVariant, QVariant(1000))) ||
      !near_polygon_completed.toBool() ||
      editor_preview.activeMaskPolygonPointCount() != 3) {
    return fail("clicking inside the first-point snap target should complete without adding a point");
  }
  QObject* polygon_feather_canvas = nullptr;
  if (!wait_for_condition([&]() {
        polygon_feather_canvas =
            find_named_object(edit_workspace, QStringLiteral("editImageViewportGeometricMaskCanvas"));
        if (polygon_feather_canvas == nullptr) {
          polygon_feather_canvas = find_named_object(edit_workspace, QStringLiteral("geometricMaskCanvas"));
        }
        return polygon_feather_canvas != nullptr &&
               polygon_feather_canvas->property("polygonFeatherVisualizationActive").toBool() &&
               polygon_feather_canvas->property("polygonFeatherVisualizationWidth").toDouble() > 0.0;
      })) {
    return fail("completed feathered polygon selections should expose a visible feather boundary visualization");
  }
  QObject* polygon_vertex_handle = nullptr;
  if (!wait_for_condition([&]() {
        polygon_vertex_handle = find_named_object(edit_workspace, QStringLiteral("polygonVertexHandle1"));
        return polygon_vertex_handle != nullptr && polygon_vertex_handle->property("visible").toBool();
      })) {
    return fail("completed polygon selections should expose draggable vertex handles in Studio");
  }
  if (polygon_vertex_handle->property("handleReadabilityContract").toString() !=
      QStringLiteral("dual-contrast-halo|large-hit-target|color-independent:v1") ||
      polygon_vertex_handle->property("width").toDouble() < 14.0) {
    return fail("Studio polygon vertex handles should expose a dual-contrast readable handle contract");
  }
  if (!editor_preview.moveActivePolygonPoint(1, 0.40, 0.45)) {
    return fail("Studio polygon vertex handles should be backed by editable controller geometry");
  }
  QCoreApplication::processEvents();
  const auto edited_polygon_points = editor_preview.activeMaskPolygonPoints();
  const auto edited_polygon_point = edited_polygon_points.at(1).toMap();
  if (std::abs(edited_polygon_point.value(QStringLiteral("x")).toDouble() - 0.40) > 1e-6 ||
      std::abs(edited_polygon_point.value(QStringLiteral("y")).toDouble() - 0.45) > 1e-6) {
    return fail("moving a polygon vertex should update the active polygon point coordinates");
  }
  QObject* polygon_edge_insert_handle = nullptr;
  if (!wait_for_condition([&]() {
        polygon_edge_insert_handle =
            find_named_object(edit_workspace, QStringLiteral("polygonEdgeInsertHandle1"));
        return polygon_edge_insert_handle != nullptr &&
               polygon_edge_insert_handle->property("visible").toBool();
      })) {
    return fail("completed polygon selections should expose insert handles on polygon edges in Studio");
  }
  if (polygon_edge_insert_handle->property("handleReadabilityContract").toString() !=
      QStringLiteral("dual-contrast-halo|large-hit-target|color-independent:v1") ||
      polygon_edge_insert_handle->property("width").toDouble() < 14.0) {
    return fail("Studio polygon edge insert handles should expose a dual-contrast readable handle contract");
  }
  if (!editor_preview.insertActivePolygonPointAfter(1, 0.50, 0.52) ||
      editor_preview.activeMaskPolygonPointCount() != 4) {
    return fail("Studio polygon edge insert handles should be backed by editable controller geometry");
  }
  QCoreApplication::processEvents();
  const auto inserted_polygon_points = editor_preview.activeMaskPolygonPoints();
  const auto inserted_polygon_point = inserted_polygon_points.at(2).toMap();
  if (std::abs(inserted_polygon_point.value(QStringLiteral("x")).toDouble() - 0.50) > 1e-6 ||
      std::abs(inserted_polygon_point.value(QStringLiteral("y")).toDouble() - 0.52) > 1e-6) {
    return fail("inserting a polygon edge point should add the point after the selected edge start");
  }
  editor_preview.removeActiveMask();
  QCoreApplication::processEvents();
  if (editor_preview.maskCount() != mask_count_before_polygon_cancel) {
    return fail("polygon snap-close smoke cleanup should preserve unrelated masks");
  }
  editor_preview.startLassoSelection(false);
  editor_preview.appendActivePolygonPoint(0.18, 0.18);
  editor_preview.appendActivePolygonPoint(0.36, 0.20);
  editor_preview.appendActivePolygonPoint(0.64, 0.26);
  editor_preview.appendActivePolygonPoint(0.70, 0.68);
  editor_preview.appendActivePolygonPoint(0.28, 0.72);
  QCoreApplication::processEvents();
  QObject* lasso_geometric_canvas = nullptr;
  if (!wait_for_condition([&]() {
        lasso_geometric_canvas =
            find_named_object(edit_workspace, QStringLiteral("editImageViewportGeometricMaskCanvas"));
        return lasso_geometric_canvas != nullptr &&
               lasso_geometric_canvas->property("visible").toBool() &&
               lasso_geometric_canvas->property("liveLassoFillVisible").toBool() &&
               lasso_geometric_canvas->property("liveLassoOutlineVisible").toBool();
      }) ||
      lasso_geometric_canvas->property("liveLassoRenderContract").toString() !=
          QStringLiteral("live-lasso|solid-fill|high-contrast-outline:v1")) {
    return fail("active lasso drawing should show a solid fill and high-contrast outline before release");
  }
  if (lasso_complete_return_shortcut == nullptr || lasso_complete_enter_shortcut == nullptr ||
      !lasso_complete_return_shortcut->property("enabled").toBool() ||
      !lasso_complete_enter_shortcut->property("enabled").toBool() ||
      !active_canvas_tool_chip->property("visible").toBool() ||
      active_canvas_tool_chip_name_label->property("text").toString() != QStringLiteral("Lasso") ||
      !active_canvas_tool_chip_mode_label->property("text").toString().contains(QStringLiteral("5 pts")) ||
      !active_canvas_tool_done_button->property("enabled").toBool()) {
    return fail("active lasso drawing should expose a Done-ready active-tool chip and Enter/Return shortcuts");
  }
  edit_workspace->setProperty("lassoDrawArmed", true);
  edit_viewport->setProperty("maskGestureKind", QStringLiteral("lasso"));
  edit_viewport->setProperty("maskGestureId", editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value("id"));
  if (!QMetaObject::invokeMethod(
          edit_viewport,
          "handleMaskPointerReleased")) {
    return fail("Studio viewport should expose lasso close-on-release handling");
  }
  QCoreApplication::processEvents();
  const auto closed_lasso = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap();
  if (editor_preview.activeMaskKind() != QStringLiteral("lasso") ||
      !closed_lasso.value(QStringLiteral("lassoClosed")).toBool() ||
      !closed_lasso.value(QStringLiteral("lassoSimplified")).toBool() ||
      closed_lasso.value(QStringLiteral("lassoSmoothed")).toBool() ||
      closed_lasso.value(QStringLiteral("lassoRawPointCount")).toInt() != 5) {
    return fail("Studio lasso release should close and preserve the freehand path");
  }
  QVariant lasso_finished;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "finishActiveCanvasTool",
          Q_RETURN_ARG(QVariant, lasso_finished)) ||
      !lasso_finished.toBool() ||
      edit_workspace->property("editTool").toString() != QStringLiteral("layers") ||
      editor_preview.activeMaskKind() != QStringLiteral("lasso")) {
    return fail("Done/Enter should finish lasso drawing and keep the lasso selection active");
  }
  editor_preview.removeActiveMask();
  QCoreApplication::processEvents();
  if (editor_preview.maskCount() != mask_count_before_polygon_cancel) {
    return fail("lasso close-on-release smoke cleanup should preserve unrelated masks");
  }
  trace_smoke_progress("polygon and lasso checks passed");
  editor_preview.startPolygonSelection(false);
  editor_preview.appendActivePolygonPoint(0.15, 0.15);
  editor_preview.appendActivePolygonPoint(0.85, 0.85);
  editor_preview.appendActivePolygonPoint(0.15, 0.85);
  editor_preview.appendActivePolygonPoint(0.85, 0.15);
  QCoreApplication::processEvents();
  QObject* polygon_self_intersection_warning = nullptr;
  if (!wait_for_condition([&]() {
        polygon_self_intersection_warning =
            find_named_object(edit_workspace, QStringLiteral("polygonSelfIntersectionWarning"));
        return polygon_self_intersection_warning != nullptr &&
               polygon_self_intersection_warning->property("visible").toBool();
      })) {
    return fail("self-intersecting polygon selections should show a warning in the UI");
  }
  if (polygon_self_intersection_warning == nullptr ||
      !polygon_self_intersection_warning->property("visible").toBool() ||
      !polygon_self_intersection_warning->property("text")
           .toString()
           .contains(QStringLiteral("Self-intersecting polygon"))) {
    return fail("self-intersecting polygon selections should show a warning in the UI");
  }
  if (polygon_hud_complete_button->property("enabled").toBool() ||
      polygon_complete_return_shortcut->property("enabled").toBool() ||
      polygon_complete_enter_shortcut->property("enabled").toBool()) {
    return fail("self-intersecting polygon selections should block completion affordances");
  }
  editor_preview.removeLastActivePolygonPoint();
  QCoreApplication::processEvents();
  if (!wait_for_condition([&]() {
        return !editor_preview.activeMaskPolygonSelfIntersecting() &&
               !polygon_self_intersection_warning->property("visible").toBool();
      })) {
    return fail("polygon self-intersection warning should clear after removing the crossing point");
  }
  editor_preview.removeActiveMask();
  QCoreApplication::processEvents();
  if (editor_preview.maskCount() != mask_count_before_polygon_cancel) {
    return fail("polygon self-intersection smoke cleanup should preserve unrelated masks");
  }
  trace_smoke_progress("mask geometry proofs checked");
  } else {
    trace_smoke_progress("mask geometry proofs skipped");
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_MASK_GEOMETRY_PROOFS")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
