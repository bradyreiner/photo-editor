    editor_preview.setImageProvider(nullptr);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return EXIT_SUCCESS;
  }
  QVariant base_row_layer_selected;
  QVariant base_row_scope;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, base_row_layer_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !base_row_layer_selected.toBool()) {
    return fail("Base Image row click proof should start from an editable photo-layer target");
  }
  process_layout_events(4);
  QVariant base_row_clicked;
  if (!QMetaObject::invokeMethod(
          base_image_layer_target_area,
          "triggerBaseImageLayerRowClickForTest",
          Q_RETURN_ARG(QVariant, base_row_clicked)) ||
      !base_row_clicked.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetScope",
          Q_RETURN_ARG(QVariant, base_row_scope)) ||
      base_row_scope.toString() != QStringLiteral("base-image") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("editInspectorTab").toString() != QStringLiteral("adjust")) {
    std::cerr << "Base Image row click state: scope="
              << base_row_scope.toString().toStdString()
              << " tool=" << edit_workspace->property("editTool").toString().toStdString()
              << " tab=" << edit_workspace->property("editInspectorTab").toString().toStdString()
              << " routeClicked=" << base_row_clicked.toBool()
              << " targetAreaVisible="
              << item_visible(qobject_cast<QQuickItem*>(base_image_layer_target_area)) << '\n';
    return fail("clicking the Base Image row should switch to the Base Image edit target");
  }
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, base_row_layer_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !base_row_layer_selected.toBool()) {
    return fail("Base Image row click proof should restore the layer panel target after assertion");
  }
  process_layout_events(4);
  const auto base_image_layer_rect = scene_rect(qobject_cast<QQuickItem*>(base_image_layer_row));
  const auto photo_layer_rows = find_named_items(edit_workspace, QStringLiteral("photoLayerRow"));
  if (photo_layer_rows.size() < editor_preview.photoLayerCount()) {
    return fail("layer panel should expose one visible row for every editable photo layer");
  }
  if (editor_preview.photoLayerCount() < 2 &&
      !editor_preview.addAdjustmentLayer(QStringLiteral("Keyboard Navigation Target"))) {
    return fail("layer keyboard navigation proof should be able to create a second target layer");
  }
  process_layout_events(4);
  auto photo_layer_rows_for_keyboard =
      find_named_items(edit_workspace, QStringLiteral("photoLayerRow"));
  auto find_photo_layer_row_by_index = [&](const int layer_index) -> QQuickItem* {
    for (auto* row : find_named_items(edit_workspace, QStringLiteral("photoLayerRow"))) {
      if (item_visible(row) && row->property("layerIndex").toInt() == layer_index) {
        return row;
      }
    }
    return nullptr;
  };
  auto selected_photo_layer_indices = [&]() {
    QVector<int> selected;
    const auto selected_values =
        edit_workspace->property("selectedPhotoLayerIndices").toList();
    selected.reserve(selected_values.size());
    for (const auto& value : selected_values) {
      selected.push_back(value.toInt());
    }
    return selected;
  };
  auto send_photo_layer_row_key =
      [&](const int layer_index,
          const int key,
          const Qt::KeyboardModifiers modifiers = Qt::NoModifier) -> bool {
    auto* row = find_photo_layer_row_by_index(layer_index);
    if (row == nullptr) {
      return false;
    }
    row->forceActiveFocus(Qt::OtherFocusReason);
    process_layout_events(2);
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QCoreApplication::sendEvent(row, &press);
    process_layout_events(4);
    return press.isAccepted();
  };
  auto photo_layer_id_at = [&](const int layer_index) {
    if (layer_index < 0 || layer_index >= editor_preview.photoLayers().size()) {
      return QString();
    }
    return editor_preview.photoLayers()
        .at(layer_index)
        .toMap()
        .value(QStringLiteral("id"))
        .toString();
  };
  if (editor_preview.photoLayerCount() < 2 ||
      photo_layer_rows_for_keyboard.size() < editor_preview.photoLayerCount()) {
    return fail("layer keyboard navigation proof should have at least two visible row delegates");
  }
  editor_preview.setPhotoLayerLocked(0, false);
  editor_preview.setPhotoLayerLocked(1, false);
  editor_preview.setPhotoLayerEnabled(0, true);
  editor_preview.setPhotoLayerEnabled(1, true);
  process_layout_events(4);
	  if (editor_preview.photoLayers().at(0).toMap().value(QStringLiteral("locked")).toBool() ||
	      editor_preview.photoLayers().at(1).toMap().value(QStringLiteral("locked")).toBool() ||
	      !editor_preview.photoLayers().at(0).toMap().value(QStringLiteral("enabled"), true).toBool() ||
	      !editor_preview.photoLayers().at(1).toMap().value(QStringLiteral("enabled"), true).toBool()) {
	    return fail("layer keyboard navigation proof should start from unlocked visible layers");
	  }
  QVariant keyboard_row_selected;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, keyboard_row_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !keyboard_row_selected.toBool()) {
    return fail("layer keyboard navigation proof should select the first row");
  }
  process_layout_events(4);
  if (!send_photo_layer_row_key(0, Qt::Key_Down) ||
      edit_workspace->property("activePhotoLayerIndex").toInt() != 1 ||
      !selected_photo_layer_indices().contains(1)) {
    return fail("Down on a focused layer row should select the next row delegate");
  }
  if (!send_photo_layer_row_key(1, Qt::Key_Up, Qt::ShiftModifier)) {
    return fail("Shift+Up on a focused layer row should be accepted for multi-select");
  }
  const auto keyboard_multi_selection = selected_photo_layer_indices();
  if (!keyboard_multi_selection.contains(0) ||
      !keyboard_multi_selection.contains(1)) {
    return fail("Shift+Up on a focused layer row should extend layer selection across row delegates");
  }
  if (!send_photo_layer_row_key(0, Qt::Key_Space) ||
      editor_preview.photoLayers()
          .at(0)
          .toMap()
          .value(QStringLiteral("enabled"))
          .toBool()) {
    return fail("Space on a focused layer row should toggle layer visibility off");
  }
  if (!send_photo_layer_row_key(0, Qt::Key_Space) ||
      !editor_preview.photoLayers()
           .at(0)
           .toMap()
           .value(QStringLiteral("enabled"))
           .toBool()) {
    return fail("Space on a focused layer row should toggle layer visibility back on");
  }
  if (!send_photo_layer_row_key(0, Qt::Key_Return) ||
      edit_workspace->property("activePhotoLayerIndex").toInt() != 0) {
    return fail("Return on a focused layer row should switch the active edit target to that layer");
  }
  const auto keyboard_first_id_before_reorder = photo_layer_id_at(0);
  const auto keyboard_second_id_before_reorder = photo_layer_id_at(1);
  if (keyboard_first_id_before_reorder.isEmpty() ||
      keyboard_second_id_before_reorder.isEmpty() ||
      !send_photo_layer_row_key(0, Qt::Key_Down, Qt::MetaModifier) ||
      photo_layer_id_at(1) != keyboard_first_id_before_reorder ||
      photo_layer_id_at(0) != keyboard_second_id_before_reorder) {
    return fail("Cmd+Down on a focused layer row should reorder the actual layer stack");
  }
  process_layout_events(4);
  if (!send_photo_layer_row_key(1, Qt::Key_Up, Qt::MetaModifier) ||
      photo_layer_id_at(0) != keyboard_first_id_before_reorder ||
      photo_layer_id_at(1) != keyboard_second_id_before_reorder) {
    return fail("Cmd+Up on a focused layer row should restore deterministic layer order");
  }
  const auto keyboard_selection_after_reorder = selected_photo_layer_indices();
  if (!keyboard_selection_after_reorder.contains(0) ||
      !keyboard_selection_after_reorder.contains(1)) {
    return fail("multi-selected layer state should survive keyboard reorder and row recycling");
  }
  const auto nested_parent_layer_index =
      photo_layer_nested_mask_row == nullptr
          ? -1
          : photo_layer_nested_mask_row->property("parentLayerIndex").toInt();
  const auto failure_layer_index =
      nested_parent_layer_index >= 0 ? nested_parent_layer_index
                                     : photo_layer_row->property("layerIndex").toInt();
  const auto failure_layer_model = editor_preview.photoLayers().value(failure_layer_index).toMap();
  const auto failure_layer_id = failure_layer_model.value(QStringLiteral("id")).toString();
  auto failure_mask_set_ids = failure_layer_model.value(QStringLiteral("selectionMaskSetIds")).toStringList();
  if (failure_mask_set_ids.isEmpty()) {
    const auto raw_mask_set_ids =
        failure_layer_model.value(QStringLiteral("selectionMaskSetIds")).toList();
    for (const auto& raw_mask_set_id : raw_mask_set_ids) {
      const auto mask_set_id = raw_mask_set_id.toString().trimmed();
      if (!mask_set_id.isEmpty()) {
        failure_mask_set_ids.append(mask_set_id);
      }
    }
  }
  if (failure_mask_set_ids.isEmpty()) {
    const auto mask_set_id =
        failure_layer_model.value(QStringLiteral("selectionMaskSetId")).toString().trimmed();
    if (!mask_set_id.isEmpty()) {
      failure_mask_set_ids.append(mask_set_id);
    }
  }
  if (failure_mask_set_ids.isEmpty()) {
    for (const auto& mask_set_value : editor_preview.maskSets()) {
      const auto mask_set = mask_set_value.toMap();
      const auto mask_set_id = mask_set.value(QStringLiteral("id")).toString().trimmed();
      if (!mask_set_id.isEmpty()) {
        failure_mask_set_ids.append(mask_set_id);
        break;
      }
    }
  }
  const auto failure_mask_set_id =
      failure_mask_set_ids.isEmpty() ? QString() : failure_mask_set_ids.constFirst();
  const auto current_file_path =
      photo_editor::desktop::EditorPreviewControllerTestAccess::file_path(editor_preview);
  if (failure_layer_id.trimmed().isEmpty() || failure_mask_set_id.trimmed().isEmpty() ||
      current_file_path.trimmed().isEmpty()) {
    std::cerr << "Failure row precondition: layerIndex=" << failure_layer_index
              << " layerId=" << failure_layer_id.toStdString()
              << " maskSetId=" << failure_mask_set_id.toStdString()
              << " filePathEmpty=" << current_file_path.trimmed().isEmpty()
              << " photoLayerCount=" << editor_preview.photoLayerCount()
              << " maskSetCount=" << editor_preview.maskSetCount() << "\n";
    return fail("preview failure row proof should have an addressable layer, mask set, file, and last preview");
  }
  const auto seed_display_preview_source =
      editor_preview.displayPreviewSource().trimmed().isEmpty()
          ? QStringLiteral("image://editor-preview/last-good-preview")
          : editor_preview.displayPreviewSource();
  photo_editor::desktop::RenderResult last_good_result;
  last_good_result.file_path = current_file_path;
  last_good_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(editor_preview);
  last_good_result.success = true;
  last_good_result.status = QStringLiteral("Preview ready.");
  last_good_result.render_reason = QStringLiteral("desktop_smoke_last_good");
  last_good_result.render_mode = QStringLiteral("live_preview");
  last_good_result.interaction_mode = QStringLiteral("draft_interactive");
  last_good_result.dirty_domain = QStringLiteral("layers_composite");
  last_good_result.dirty_stages = QStringList{QStringLiteral("layers_composite")};
  last_good_result.preview_image = QImage(24, 16, QImage::Format_RGB32);
  last_good_result.preview_image.fill(QColor(18, 34, 42));
  editor_preview_image_provider->setImage(
      QStringLiteral("last-good-preview"),
      last_good_result.preview_image);
  last_good_result.output_width = last_good_result.preview_image.width();
  last_good_result.output_height = last_good_result.preview_image.height();
  last_good_result.source_width = last_good_result.preview_image.width();
  last_good_result.source_height = last_good_result.preview_image.height();
  last_good_result.display_preview_source = seed_display_preview_source;
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      editor_preview,
      false,
      last_good_result);
  process_layout_events(4);
  const auto last_good_preview_source = editor_preview.displayPreviewSource();
  photo_editor::desktop::RenderResult failed_mask_result;
  failed_mask_result.file_path = current_file_path;
  failed_mask_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(editor_preview);
  failed_mask_result.success = false;
  failed_mask_result.failure_stage = QStringLiteral("mask_alpha");
  failed_mask_result.failure_detail = QStringLiteral("sha1:desktop-smoke-mask-alpha");
  failed_mask_result.failure_layer_ids = QStringList{failure_layer_id};
  failed_mask_result.failure_mask_set_ids = QStringList{failure_mask_set_id};
  failed_mask_result.render_reason = QStringLiteral("mask_preview_change");
  failed_mask_result.render_mode = QStringLiteral("settled_preview");
  failed_mask_result.interaction_mode = QStringLiteral("interactive");
  failed_mask_result.dirty_domain = QStringLiteral("mask_rasterization");
  failed_mask_result.dirty_stages =
      QStringList{QStringLiteral("mask_rasterization"), QStringLiteral("mask_composition")};
  failed_mask_result.active_target = QStringLiteral("mask");
  failed_mask_result.active_layer_id = failure_layer_id;
  failed_mask_result.active_mask_set_id = failure_mask_set_id;
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      editor_preview,
      false,
      failed_mask_result);
  process_layout_events(8);
  photo_layer_nested_mask_row =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskRow"));
  photo_layer_nested_mask_status_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskStatusLabel"));
  auto* failure_preview_retry_button =
      find_named_object(root, QStringLiteral("activityCancelButton"));
  auto* failure_preview_update_badge =
      find_named_object(root, QStringLiteral("failureActivityIndicator"));
  QString failed_layer_signature;
  QString failed_layer_state_text;
  for (const auto* layer_row_item :
       find_named_items(edit_workspace, QStringLiteral("photoLayerRow"))) {
    const auto row_signature = layer_row_item->property("layerFailureSignature").toString();
    if (row_signature.contains(QStringLiteral("failed:true")) &&
        row_signature.contains(QStringLiteral("stage:mask_alpha")) &&
        row_signature.contains(failure_layer_id) &&
        row_signature.contains(failure_mask_set_id)) {
      failed_layer_signature = row_signature;
      const auto row_labels =
          layer_row_item->findChildren<QObject*>(QStringLiteral("photoLayerStateLabel"));
      if (!row_labels.isEmpty()) {
        failed_layer_state_text = row_labels.constFirst()->property("text").toString();
      }
      break;
    }
  }
  const auto failed_mask_signature =
      photo_layer_nested_mask_row == nullptr
          ? QString()
          : photo_layer_nested_mask_row->property("maskFailureSignature").toString();
  const auto failed_mask_status =
      photo_layer_nested_mask_status_label == nullptr
          ? QString()
          : photo_layer_nested_mask_status_label->property("text").toString();
  if (photo_layer_nested_mask_row == nullptr || photo_layer_nested_mask_status_label == nullptr ||
      failure_preview_retry_button == nullptr || failure_preview_update_badge == nullptr ||
      editor_preview.status() != QStringLiteral("Preview failed: Mask alpha.") ||
      editor_preview.previewFailureStage() != QStringLiteral("mask_alpha") ||
      !editor_preview.previewFailureLayerIds().contains(failure_layer_id) ||
      !editor_preview.previewFailureMaskSetIds().contains(failure_mask_set_id) ||
      editor_preview.displayPreviewSource() != last_good_preview_source ||
      !failure_preview_retry_button->property("visible").toBool() ||
      !failure_preview_update_badge->property("visible").toBool() ||
      failed_layer_signature.trimmed().isEmpty() ||
      failed_layer_state_text.trimmed().isEmpty() ||
      !failed_layer_state_text.contains(QStringLiteral("Preview failed: Mask alpha")) ||
      !failed_mask_status.contains(QStringLiteral("Preview failed: Mask alpha")) ||
      !failed_mask_signature.contains(QStringLiteral("failed:true")) ||
      !failed_mask_signature.contains(QStringLiteral("stage:mask_alpha")) ||
      !failed_mask_signature.contains(failure_mask_set_id)) {
    std::cerr << "Failure row injection: status=" << editor_preview.status().toStdString()
              << " stage=" << editor_preview.previewFailureStage().toStdString()
              << " layerIds=" << editor_preview.previewFailureLayerIds().join(',').toStdString()
              << " maskSetIds=" << editor_preview.previewFailureMaskSetIds().join(',').toStdString()
              << " layerSignature=" << failed_layer_signature.toStdString()
              << " layerState=" << failed_layer_state_text.toStdString()
              << " maskSignature=" << failed_mask_signature.toStdString()
              << " maskStatus=" << failed_mask_status.toStdString()
              << " retryVisible="
              << (failure_preview_retry_button == nullptr
                      ? false
                      : failure_preview_retry_button->property("visible").toBool())
              << "\n";
    return fail("preview failure should stay on the last good canvas and mark the affected layer and mask rows");
  }
  last_good_result.generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(editor_preview);
  last_good_result.render_reason = QStringLiteral("desktop_smoke_failure_recovery");
  last_good_result.display_preview_source = last_good_preview_source;
  photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
      editor_preview,
      false,
      last_good_result);
  process_layout_events(8);
  photo_layer_nested_mask_row =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskRow"));
  failure_preview_retry_button =
      find_named_object(root, QStringLiteral("activityCancelButton"));
  auto recovered_layer_failed = false;
  for (const auto* layer_row_item :
       find_named_items(edit_workspace, QStringLiteral("photoLayerRow"))) {
    if (layer_row_item->property("layerFailureSignature")
            .toString()
            .contains(QStringLiteral("failed:true"))) {
      recovered_layer_failed = true;
      break;
    }
  }
  const auto recovered_mask_signature =
      photo_layer_nested_mask_row == nullptr
          ? QString()
          : photo_layer_nested_mask_row->property("maskFailureSignature").toString();
  if (!editor_preview.previewFailureStage().isEmpty() ||
      !editor_preview.previewFailureLayerIds().isEmpty() ||
      !editor_preview.previewFailureMaskSetIds().isEmpty() ||
      (failure_preview_retry_button != nullptr &&
       failure_preview_retry_button->property("visible").toBool()) ||
      recovered_layer_failed ||
      !recovered_mask_signature.contains(QStringLiteral("failed:false"))) {
    std::cerr << "Failure recovery row state: stage="
              << editor_preview.previewFailureStage().toStdString()
              << " layerStillFailed=" << recovered_layer_failed
              << " maskSignature=" << recovered_mask_signature.toStdString() << "\n";
    return fail("successful preview recovery should clear affected layer and mask row failures");
  }
  struct ShellFailureCase {
    QString failure_stage;
    QString dirty_domain;
    QStringList dirty_stages;
    QString expected_stage;
    QString expected_status;
    QString expected_row_label;
    QString detail;
  };
  const ShellFailureCase shell_failure_cases[] = {
      {
          QStringLiteral("outline_trace"),
          QStringLiteral("overlay_composite"),
          {QStringLiteral("outline_trace")},
          QStringLiteral("outline_trace"),
          QStringLiteral("Preview failed: Outline trace."),
          QStringLiteral("Preview failed: Outline trace"),
          QStringLiteral("sha1:desktop-smoke-outline-trace"),
      },
      {
          QString(),
          QStringLiteral("export_preview"),
          {QStringLiteral("export_preview")},
          QStringLiteral("export_preview"),
          QStringLiteral("Preview failed: Export preview."),
          QStringLiteral("Preview failed: Export preview"),
          QStringLiteral("sha1:desktop-smoke-export-preview"),
      },
      {
          QString(),
          QStringLiteral("inpaint_patch"),
          {QStringLiteral("retouch_patch_application")},
          QStringLiteral("retouch_patch"),
          QStringLiteral("Preview failed: Retouch patch."),
          QStringLiteral("Preview failed: Retouch patch"),
          QStringLiteral("sha1:desktop-smoke-inpaint-patch"),
      },
      {
          QString(),
          QString(),
          {},
          QStringLiteral("render_pipeline"),
          QStringLiteral("Preview failed: Render pipeline."),
          QStringLiteral("Preview failed: Render pipeline"),
          QStringLiteral("desktop-smoke-no-stage-fallback"),
      },
  };
  for (const auto& failure_case : shell_failure_cases) {
    const auto staged_last_good_preview_source = editor_preview.displayPreviewSource();
    photo_editor::desktop::RenderResult staged_failure;
    staged_failure.file_path = current_file_path;
    staged_failure.generation =
        photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(editor_preview);
    staged_failure.success = false;
    staged_failure.failure_stage = failure_case.failure_stage;
    staged_failure.failure_detail = failure_case.detail;
    staged_failure.failure_layer_ids = QStringList{failure_layer_id};
    staged_failure.failure_mask_set_ids = QStringList{failure_mask_set_id};
    staged_failure.render_reason = QStringLiteral("desktop_smoke_stage_failure");
    staged_failure.render_mode = QStringLiteral("settled_preview");
    staged_failure.interaction_mode = QStringLiteral("interactive");
    staged_failure.dirty_domain = failure_case.dirty_domain;
    staged_failure.dirty_stages = failure_case.dirty_stages;
    staged_failure.active_target = QStringLiteral("mask");
    staged_failure.active_layer_id = failure_layer_id;
    staged_failure.active_mask_set_id = failure_mask_set_id;
    photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
        editor_preview,
        false,
        staged_failure);
    process_layout_events(6);

    QString staged_layer_signature;
    QString staged_layer_state_text;
    for (const auto* layer_row_item :
         find_named_items(edit_workspace, QStringLiteral("photoLayerRow"))) {
      const auto row_signature = layer_row_item->property("layerFailureSignature").toString();
      if (row_signature.contains(QStringLiteral("failed:true")) &&
          row_signature.contains(QStringLiteral("stage:%1").arg(failure_case.expected_stage)) &&
          row_signature.contains(failure_layer_id) &&
          row_signature.contains(failure_mask_set_id)) {
        staged_layer_signature = row_signature;
        const auto row_labels =
            layer_row_item->findChildren<QObject*>(QStringLiteral("photoLayerStateLabel"));
        if (!row_labels.isEmpty()) {
          staged_layer_state_text = row_labels.constFirst()->property("text").toString();
        }
        break;
      }
    }
    photo_layer_nested_mask_row =
        find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskRow"));
    photo_layer_nested_mask_status_label =
        find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskStatusLabel"));
    failure_preview_retry_button =
        find_named_object(root, QStringLiteral("activityCancelButton"));
    failure_preview_update_badge =
        find_named_object(root, QStringLiteral("failureActivityIndicator"));
    const auto staged_mask_signature =
        photo_layer_nested_mask_row == nullptr
            ? QString()
            : photo_layer_nested_mask_row->property("maskFailureSignature").toString();
    const auto staged_mask_status =
        photo_layer_nested_mask_status_label == nullptr
            ? QString()
            : photo_layer_nested_mask_status_label->property("text").toString();
    if (editor_preview.status() != failure_case.expected_status ||
        editor_preview.previewFailureStage() != failure_case.expected_stage ||
        !editor_preview.previewFailureDetail().contains(failure_case.detail) ||
        editor_preview.displayPreviewSource() != staged_last_good_preview_source ||
        failure_preview_retry_button == nullptr ||
        !failure_preview_retry_button->property("visible").toBool() ||
        failure_preview_update_badge == nullptr ||
        !failure_preview_update_badge->property("visible").toBool() ||
        staged_layer_signature.trimmed().isEmpty() ||
        !staged_layer_state_text.contains(failure_case.expected_row_label) ||
        !staged_mask_signature.contains(QStringLiteral("failed:true")) ||
        !staged_mask_signature.contains(QStringLiteral("stage:%1").arg(failure_case.expected_stage)) ||
        !staged_mask_status.contains(failure_case.expected_row_label)) {
      std::cerr << "Shell staged failure mismatch: expectedStage="
                << failure_case.expected_stage.toStdString()
                << " status=" << editor_preview.status().toStdString()
                << " stage=" << editor_preview.previewFailureStage().toStdString()
                << " detail=" << editor_preview.previewFailureDetail().toStdString()
                << " layerSignature=" << staged_layer_signature.toStdString()
                << " layerState=" << staged_layer_state_text.toStdString()
                << " maskSignature=" << staged_mask_signature.toStdString()
                << " maskStatus=" << staged_mask_status.toStdString()
                << "\n";
      return fail("shell preview failure should mark affected layer and mask rows for every covered stage");
    }

    last_good_result.generation =
        photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(editor_preview);
    last_good_result.render_reason = QStringLiteral("desktop_smoke_stage_failure_recovery");
    last_good_result.display_preview_source = staged_last_good_preview_source;
    photo_editor::desktop::EditorPreviewControllerTestAccess::handle_render_result(
        editor_preview,
        false,
        last_good_result);
    process_layout_events(6);
    auto stage_recovered_layer_failed = false;
    for (const auto* layer_row_item :
         find_named_items(edit_workspace, QStringLiteral("photoLayerRow"))) {
      if (layer_row_item->property("layerFailureSignature")
              .toString()
              .contains(QStringLiteral("failed:true"))) {
        stage_recovered_layer_failed = true;
        break;
      }
    }
    photo_layer_nested_mask_row =
        find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskRow"));
    const auto stage_recovered_mask_signature =
        photo_layer_nested_mask_row == nullptr
            ? QString()
            : photo_layer_nested_mask_row->property("maskFailureSignature").toString();
    if (!editor_preview.previewFailureStage().isEmpty() ||
        !editor_preview.previewFailureLayerIds().isEmpty() ||
        !editor_preview.previewFailureMaskSetIds().isEmpty() ||
        stage_recovered_layer_failed ||
        !stage_recovered_mask_signature.contains(QStringLiteral("failed:false"))) {
      return fail("shell staged failure recovery should clear affected layer and mask row failures");
    }
  }
  const auto layer_thumbnail_data = editor_preview.photoLayers();
  if (layer_thumbnail_data.size() != editor_preview.photoLayerCount()) {
    return fail("photo layer thumbnail proof should inspect the normalized layer model");
  }
  for (const auto& value : layer_thumbnail_data) {
    const auto layer = value.toMap();
    const auto thumbnail_source = layer.value(QStringLiteral("thumbnailSource")).toString();
    if (!layer.value(QStringLiteral("thumbnailReady")).toBool() ||
        !thumbnail_source.startsWith(QStringLiteral("data:image/png;base64,")) ||
        !layer.value(QStringLiteral("thumbnailSignature")).toString().contains(
            QStringLiteral("ready:true")) ||
        layer.value(QStringLiteral("thumbnailBackingWidth")).toInt() < 64 ||
        layer.value(QStringLiteral("thumbnailBackingHeight")).toInt() < 64 ||
        layer.value(QStringLiteral("thumbnailDisplayWidth")).toInt() != 32 ||
        layer.value(QStringLiteral("thumbnailDisplayHeight")).toInt() != 32 ||
        layer.value(QStringLiteral("thumbnailDevicePixelRatio")).toDouble() < 2.0 ||
        !layer.value(QStringLiteral("thumbnailPixelRatioSignature"))
             .toString()
             .contains(QStringLiteral("display:32x32"))) {
      std::cerr << "Layer thumbnail missing: name="
                << layer.value(QStringLiteral("displayName")).toString().toStdString()
                << " kind=" << layer.value(QStringLiteral("kind")).toString().toStdString()
                << " ready=" << layer.value(QStringLiteral("thumbnailReady")).toBool()
                << " source=" << thumbnail_source.left(24).toStdString()
                << " signature="
                << layer.value(QStringLiteral("thumbnailSignature")).toString().toStdString()
                << " pixelSignature="
                << layer.value(QStringLiteral("thumbnailPixelRatioSignature"))
                       .toString()
                       .toStdString()
                << "\n";
      return fail("every normalized photo layer should expose a renderable thumbnail source");
    }
  }
  for (const auto* layer_row_item : photo_layer_rows) {
    if (item_visible(layer_row_item) &&
        scene_rect(layer_row_item).bottom() > base_image_layer_rect.top() + 1.0) {
      return fail("locked Base Image row should stay below every editable photo layer row");
    }
  }
  const auto state_layer_index = photo_layer_row->property("layerIndex").toInt();
  if (!editor_preview.setPhotoLayerEnabled(state_layer_index, false)) {
    return fail("layer state proof should be able to hide the active layer");
  }
  process_layout_events(4);
  photo_layer_row = find_named_object(edit_workspace, QStringLiteral("photoLayerRow"));
  photo_layer_state_label = find_named_object(edit_workspace, QStringLiteral("photoLayerStateLabel"));
  if (photo_layer_row == nullptr || photo_layer_state_label == nullptr ||
      !photo_layer_row->property("layerHidden").toBool() ||
      photo_layer_row->property("layerVisible").toBool() ||
      !photo_layer_row->property("layerStateSignature").toString().contains(
          QStringLiteral("hidden:true")) ||
      !photo_layer_state_label->property("text").toString().contains(QStringLiteral("Hidden"))) {
    return fail("layer rows should expose a clear hidden state");
  }
  if (!editor_preview.setPhotoLayerEnabled(state_layer_index, true) ||
      !editor_preview.setPhotoLayerLocked(state_layer_index, true)) {
    return fail("layer state proof should be able to lock the active layer");
  }
  process_layout_events(4);
  photo_layer_row = find_named_object(edit_workspace, QStringLiteral("photoLayerRow"));
  photo_layer_state_label = find_named_object(edit_workspace, QStringLiteral("photoLayerStateLabel"));
  if (photo_layer_row == nullptr || photo_layer_state_label == nullptr ||
      !photo_layer_row->property("layerLocked").toBool() ||
      !photo_layer_row->property("layerStateSignature").toString().contains(
          QStringLiteral("locked:true")) ||
      !photo_layer_state_label->property("text").toString().contains(QStringLiteral("Locked"))) {
    return fail("layer rows should expose a clear locked state");
  }
  if (!editor_preview.setPhotoLayerLocked(state_layer_index, false) ||
      !editor_preview.isolatePhotoLayer(state_layer_index)) {
    return fail("layer state proof should be able to solo the active layer");
  }
  process_layout_events(4);
  photo_layer_row = find_named_object(edit_workspace, QStringLiteral("photoLayerRow"));
  photo_layer_state_label = find_named_object(edit_workspace, QStringLiteral("photoLayerStateLabel"));
  if (photo_layer_row == nullptr || photo_layer_state_label == nullptr ||
      !photo_layer_row->property("layerSoloed").toBool() ||
      !photo_layer_row->property("layerStateSignature").toString().contains(
          QStringLiteral("solo:true")) ||
      !photo_layer_state_label->property("text").toString().contains(QStringLiteral("Solo")) ||
      !editor_preview.clearPhotoLayerSolo()) {
    return fail("layer rows should expose a clear solo state");
  }
  process_layout_events(4);
  QObject* mask_preview_button = photo_layer_nested_mask_preview_button != nullptr
                                     ? photo_layer_nested_mask_preview_button
                                     : photo_layer_mask_set_preview_button;
  if (mask_preview_button == nullptr ||
      !mask_preview_button->property("enabled").toBool() ||
      mask_preview_button->property("active").toBool() ||
      !QMetaObject::invokeMethod(mask_preview_button, "click")) {
    return fail("mask rows should expose a per-mask before/after preview toggle");
  }
  process_layout_events(4);
  auto mask_preview_bypassed = false;
  for (const auto& mask_set_value : editor_preview.maskSets()) {
    if (mask_set_value.toMap().value(QStringLiteral("maskSetPreviewBypassed")).toBool()) {
      mask_preview_bypassed = true;
      break;
    }
  }
  photo_layer_nested_mask_preview_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskPreviewButton"));
  photo_layer_mask_set_preview_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetPreviewButton"));
  mask_preview_button = photo_layer_nested_mask_preview_button != nullptr
                            ? photo_layer_nested_mask_preview_button
                            : photo_layer_mask_set_preview_button;
  if (mask_preview_button == nullptr || !mask_preview_button->property("active").toBool() ||
      !mask_preview_bypassed ||
      !editor_preview.status().contains(QStringLiteral("before mask attachment"))) {
    return fail("mask before preview should flag only the mask attachment in the layer UI");
  }
  if (!QMetaObject::invokeMethod(mask_preview_button, "click")) {
    return fail("mask before/after preview toggle should restore the mask attachment");
  }
  process_layout_events(4);
  auto mask_preview_restored = true;
  for (const auto& mask_set_value : editor_preview.maskSets()) {
    if (mask_set_value.toMap().value(QStringLiteral("maskSetPreviewBypassed")).toBool()) {
      mask_preview_restored = false;
      break;
    }
  }
  photo_layer_nested_mask_preview_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskPreviewButton"));
  photo_layer_mask_set_preview_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetPreviewButton"));
  mask_preview_button = photo_layer_nested_mask_preview_button != nullptr
                            ? photo_layer_nested_mask_preview_button
                            : photo_layer_mask_set_preview_button;
  if (mask_preview_button == nullptr || mask_preview_button->property("active").toBool() ||
      !mask_preview_restored ||
      !editor_preview.status().contains(QStringLiteral("with mask attachment"))) {
    return fail("mask before/after preview should restore the mask attachment without clearing it");
  }
  process_layout_events(4);
  photo_layer_nested_mask_edit_layer_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskEditLayerButton"));
  photo_layer_mask_set_adjust_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetAdjustButton"));
  QObject* mask_edit_layer_button =
      photo_layer_nested_mask_edit_layer_button != nullptr ? photo_layer_nested_mask_edit_layer_button
                                                          : photo_layer_mask_set_adjust_button;
  if (!QMetaObject::invokeMethod(mask_edit_layer_button, "click")) {
    return fail("mask rows should expose an explicit linked adjustment-layer edit action");
  }
  process_layout_events(4);
  if (edit_workspace->property("editInspectorTab").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
      active_edit_target_bar->property("targetScope").toString() !=
          QStringLiteral("adjustment-layer") ||
      active_edit_target_mode_label->property("text").toString() !=
          QStringLiteral("Layer")) {
    return fail("mask edit-layer buttons should switch to adjustment-layer editing instead of leaving the mask as the active target");
  }
  photo_layer_mask_set_select_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMaskSetSelectButton"));
  photo_layer_nested_mask_select_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskSelectButton"));
  QObject* mask_select_button = photo_layer_nested_mask_select_button != nullptr
                                    ? photo_layer_nested_mask_select_button
                                    : photo_layer_mask_set_select_button;
  if (mask_select_button == nullptr ||
      mask_select_button->property("text").toString() != QStringLiteral("Edit Selection")) {
    return fail("selection rows should label local edit regions as selections, not masks");
  }
  if (!QMetaObject::invokeMethod(mask_select_button, "click")) {
    return fail("selection rows should expose a selection edit action");
  }
  process_layout_events(4);
  if (edit_workspace->property("editInspectorTab").toString() != QStringLiteral("layers") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("masks") ||
      active_edit_target_bar->property("targetScope").toString() != QStringLiteral("mask") ||
      active_edit_target_mode_label->property("text").toString() != QStringLiteral("Selection")) {
    return fail("clicking a mask row should switch the canvas/tools to mask editing rather than color editing");
  }
  bool found_mask_selected_layer_state = false;
  const auto mask_selected_layer_rows = find_named_items(edit_workspace, QStringLiteral("photoLayerRow"));
  for (auto* row : mask_selected_layer_rows) {
    if (row->property("layerMaskSelected").toBool() &&
        row->property("layerStateSignature").toString().contains(
            QStringLiteral("mask-selected:true"))) {
      found_mask_selected_layer_state = true;
      break;
    }
  }
  if (!found_mask_selected_layer_state) {
    return fail("layer rows should expose a clear mask-selected state when one of their child masks is active");
  }
  photo_layer_nested_mask_select_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskSelectButton"));
  if (!QMetaObject::invokeMethod(photo_layer_nested_mask_select_button, "click")) {
    return fail("mask sets should render as selectable child attachments inside their adjustment layer row");
  }
  process_layout_events(4);
  photo_layer_nested_mask_select_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerNestedMaskSelectButton"));
  if (edit_workspace->property("editInspectorTab").toString() != QStringLiteral("layers") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("masks") ||
      photo_layer_nested_mask_select_button == nullptr ||
      !photo_layer_nested_mask_select_button->property("active").toBool()) {
    return fail("clicking a nested mask child row should switch the canvas to mask editing while keeping Layers visible");
  }
  QVariant layers_target_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetSignature",
          Q_RETURN_ARG(QVariant, layers_target_signature)) ||
	      item_visible(qobject_cast<QQuickItem*>(active_edit_target_bar)) ||
	      active_edit_target_bar->property("targetScope").toString() != QStringLiteral("mask") ||
	      active_edit_target_mode_label->property("text").toString() != QStringLiteral("Selection") ||
	      active_edit_target_breadcrumb_label->property("text").toString().contains(QStringLiteral("Base Image >")) ||
	      !layers_target_signature.toString().contains(QStringLiteral("scope:mask")) ||
	      !layers_target_signature.toString().contains(QStringLiteral("inspector-copy:hidden")) ||
	      !layers_target_signature.toString().contains(QStringLiteral("selection")) ||
	      !layers_target_signature.toString().contains(QStringLiteral("Overlay"))) {
    std::cerr << "Layers target state: scope="
              << active_edit_target_bar->property("targetScope").toString().toStdString()
              << " barVisible=" << active_edit_target_bar->property("visible").toBool()
              << " mode="
              << active_edit_target_mode_label->property("text").toString().toStdString()
              << " breadcrumb="
              << active_edit_target_breadcrumb_label->property("text").toString().toStdString()
              << " status="
              << active_adjustment_layer_target_status_label->property("text").toString().toStdString()
              << " signature=" << layers_target_signature.toString().toStdString()
              << '\n';
    return fail("Layers tab should keep the active layer/mask target breadcrumb visible after selecting a nested mask");
  }
  auto* selected_mask_header =
      find_named_object(edit_workspace, QStringLiteral("selectedMaskHeader"));
  auto* selected_mask_header_name_label =
      find_named_object(edit_workspace, QStringLiteral("selectedMaskHeaderNameLabel"));
  auto* selected_mask_header_mode_label =
      find_named_object(edit_workspace, QStringLiteral("selectedMaskHeaderModeLabel"));
  auto* selected_mask_header_status_label =
      find_named_object(edit_workspace, QStringLiteral("selectedMaskHeaderStatusLabel"));
  auto* selected_mask_header_settings_label =
      find_named_object(edit_workspace, QStringLiteral("selectedMaskHeaderSettingsLabel"));
  auto* selected_mask_header_thumbnail_image =
      find_named_object(edit_workspace, QStringLiteral("selectedMaskHeaderThumbnailImage"));
  const auto selected_mask_header_signature =
      selected_mask_header == nullptr
          ? QString()
          : selected_mask_header->property("headerSignature").toString();
  const auto selected_mask_header_thumbnail_source_size =
      selected_mask_header_thumbnail_image == nullptr
          ? QSize()
          : selected_mask_header_thumbnail_image->property("sourceSize").toSize();
  if (selected_mask_header == nullptr || selected_mask_header_name_label == nullptr ||
      selected_mask_header_mode_label == nullptr || selected_mask_header_status_label == nullptr ||
      selected_mask_header_settings_label == nullptr ||
      selected_mask_header_thumbnail_image == nullptr ||
      !item_visible(qobject_cast<QQuickItem*>(selected_mask_header)) ||
      !selected_mask_header_signature.contains(QStringLiteral("selected-mask-header:v1")) ||
      !selected_mask_header_signature.contains(QStringLiteral("|mask:")) ||
      !selected_mask_header_signature.contains(QStringLiteral("|layer:")) ||
      !selected_mask_header_signature.contains(QStringLiteral("|operation:")) ||
      !selected_mask_header_signature.contains(QStringLiteral("|overlay:")) ||
      !selected_mask_header_signature.contains(QStringLiteral("|feather:")) ||
	      !selected_mask_header_signature.contains(QStringLiteral("|density:")) ||
	      !selected_mask_header_signature.contains(QStringLiteral("|invert:")) ||
	      !selected_mask_header_signature.contains(QStringLiteral("|visible-copy:product-only")) ||
	      !selected_mask_header_signature.contains(QStringLiteral("|implementation-copy:hidden")) ||
	      !selected_mask_header_signature.contains(QStringLiteral("|motion:true")) ||
		      selected_mask_header_mode_label->property("text").toString() !=
		          QStringLiteral("Selection") ||
	      selected_mask_header_name_label->property("text").toString().trimmed().isEmpty() ||
	      !selected_mask_header_status_label->property("text").toString().contains(
	          QStringLiteral(" / ")) ||
		      selected_mask_header_status_label->property("text").toString().contains(
		          QStringLiteral("fallback")) ||
		      selected_mask_header_status_label->property("text").toString().contains(
		          QStringLiteral("deterministic")) ||
	      !selected_mask_header_settings_label->property("text").toString().contains(
          QStringLiteral("Feather ")) ||
      !selected_mask_header_settings_label->property("text").toString().contains(
          QStringLiteral("Density ")) ||
      !selected_mask_header_settings_label->property("text").toString().contains(
          QStringLiteral("Invert ")) ||
      selected_mask_header_thumbnail_source_size.width() <
          selected_mask_header->property("thumbnailBackingWidth").toInt() ||
      selected_mask_header_thumbnail_source_size.height() <
          selected_mask_header->property("thumbnailBackingHeight").toInt()) {
    std::cerr << "Selected mask header state: header="
              << (selected_mask_header == nullptr
                      ? std::string("null")
                      : selected_mask_header_signature.toStdString())
              << " name="
              << (selected_mask_header_name_label == nullptr
                      ? std::string("null")
                      : selected_mask_header_name_label->property("text").toString().toStdString())
              << " mode="
              << (selected_mask_header_mode_label == nullptr
                      ? std::string("null")
                      : selected_mask_header_mode_label->property("text").toString().toStdString())
              << " status="
              << (selected_mask_header_status_label == nullptr
                      ? std::string("null")
                      : selected_mask_header_status_label->property("text").toString().toStdString())
              << " settings="
              << (selected_mask_header_settings_label == nullptr
                      ? std::string("null")
                      : selected_mask_header_settings_label->property("text").toString().toStdString())
              << '\n';
    return fail("selected mask header should persistently show the active mask, parent layer, operation, overlay, feather, density, and invert state");
  }
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  process_layout_events(4);
  if (find_named_object(edit_workspace, QStringLiteral("activeAdjustmentTargetGuard")) != nullptr ||
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentTargetGuardTitleLabel")) != nullptr ||
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentTargetGuardStatusLabel")) != nullptr) {
    return fail("adjust inspector should not render the removed target guard while editing a mask");
  }
  auto* mask_first_inspector_group =
      find_named_object(edit_workspace, QStringLiteral("maskFirstInspectorGroup"));
  auto* mask_first_overlay_button =
      find_named_object(edit_workspace, QStringLiteral("maskFirstOverlayButton"));
  auto* mask_first_outline_button =
      find_named_object(edit_workspace, QStringLiteral("maskFirstOutlineButton"));
  auto* mask_first_hidden_button =
      find_named_object(edit_workspace, QStringLiteral("maskFirstHiddenButton"));
  auto* mask_first_edit_layer_button =
      find_named_object(edit_workspace, QStringLiteral("maskFirstEditLayerButton"));
  auto* mask_first_visibility_button =
      find_named_object(edit_workspace, QStringLiteral("maskFirstVisibilityButton"));
  auto* mask_first_invert_button =
      find_named_object(edit_workspace, QStringLiteral("maskFirstInvertButton"));
  auto* mask_first_feather_slider =
      find_named_object(edit_workspace, QStringLiteral("maskFirstFeatherSlider"));
  auto* mask_first_density_slider =
      find_named_object(edit_workspace, QStringLiteral("maskFirstDensitySlider"));
  auto* mask_first_opacity_slider =
      find_named_object(edit_workspace, QStringLiteral("maskFirstOpacitySlider"));
  auto* mask_first_refine_group =
      find_named_object(edit_workspace, QStringLiteral("maskFirstRefineGroup"));
  auto* mask_first_brush_settings_group =
      find_named_object(edit_workspace, QStringLiteral("maskFirstBrushSettingsGroup"));
  auto* mask_hidden_white_balance_group =
      find_named_object(edit_workspace, QStringLiteral("whiteBalanceInspectorGroup"));
  auto* mask_hidden_light_group =
      find_named_object(edit_workspace, QStringLiteral("lightInspectorGroup"));
  auto* mask_hidden_color_group =
      find_named_object(edit_workspace, QStringLiteral("colorInspectorGroup"));
  auto* mask_hidden_tone_curve_group =
      find_named_object(edit_workspace, QStringLiteral("toneCurveInspectorGroup"));
  const auto mask_first_signature =
      mask_first_inspector_group == nullptr
          ? QString()
          : mask_first_inspector_group->property("maskControlContract").toString();
  if (mask_first_inspector_group == nullptr || mask_first_overlay_button == nullptr ||
      mask_first_outline_button == nullptr || mask_first_hidden_button == nullptr ||
      mask_first_edit_layer_button == nullptr ||
      mask_first_visibility_button == nullptr || mask_first_invert_button == nullptr ||
      mask_first_feather_slider == nullptr || mask_first_density_slider == nullptr ||
      mask_first_opacity_slider == nullptr || mask_first_refine_group == nullptr ||
      mask_first_brush_settings_group == nullptr ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_inspector_group)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_overlay_button)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_outline_button)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_hidden_button)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_edit_layer_button)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_visibility_button)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_invert_button)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_feather_slider)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_density_slider)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_opacity_slider)) ||
      mask_first_inspector_group->property("title").toString() !=
          QStringLiteral("Selection") ||
      mask_first_visibility_button->property("label").toString() != QStringLiteral("Mask enabled") ||
      mask_first_visibility_button->property("checked").toBool() != editor_preview.activeMaskEnabled() ||
      mask_first_density_slider->property("label").toString() !=
          QStringLiteral("Density") ||
      mask_first_opacity_slider->property("label").toString() !=
          QStringLiteral("Opacity") ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_refine_group)) ||
      !item_visible(qobject_cast<QQuickItem*>(mask_first_brush_settings_group)) ||
      !mask_first_signature.contains(QStringLiteral("active:true")) ||
      !mask_first_signature.contains(QStringLiteral("|mode:")) ||
      !mask_first_signature.contains(QStringLiteral("|visible:")) ||
      !mask_first_signature.contains(QStringLiteral("|inverted:")) ||
      !mask_first_signature.contains(QStringLiteral("|feather:")) ||
      !mask_first_signature.contains(QStringLiteral("|density:")) ||
      !mask_first_signature.contains(QStringLiteral("|opacity:")) ||
      !mask_first_signature.contains(QStringLiteral("|brush:true")) ||
      item_visible(qobject_cast<QQuickItem*>(mask_hidden_white_balance_group)) ||
      item_visible(qobject_cast<QQuickItem*>(mask_hidden_light_group)) ||
      item_visible(qobject_cast<QQuickItem*>(mask_hidden_color_group)) ||
      item_visible(qobject_cast<QQuickItem*>(mask_hidden_tone_curve_group))) {
    std::cerr << "Mask-first inspector state: signature="
              << mask_first_signature.toStdString()
              << " groupVisible="
              << item_visible(qobject_cast<QQuickItem*>(mask_first_inspector_group))
              << " wbVisible="
              << item_visible(qobject_cast<QQuickItem*>(mask_hidden_white_balance_group))
              << " lightVisible="
              << item_visible(qobject_cast<QQuickItem*>(mask_hidden_light_group))
              << " colorVisible="
              << item_visible(qobject_cast<QQuickItem*>(mask_hidden_color_group))
              << " curveVisible="
              << item_visible(qobject_cast<QQuickItem*>(mask_hidden_tone_curve_group))
              << '\n';
    return fail("mask edit target should show mask overlay/refine/feather/density/brush controls first and hide color-only adjustment groups");
  }
  if (!QMetaObject::invokeMethod(mask_first_edit_layer_button, "click")) {
    return fail("mask-first inspector should expose a direct linked-layer edit action");
  }
  process_layout_events(4);
  if (edit_workspace->property("editInspectorTab").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
      active_edit_target_bar->property("targetScope").toString() !=
          QStringLiteral("adjustment-layer") ||
      active_edit_target_mode_label->property("text").toString() !=
          QStringLiteral("Layer")) {
    std::cerr << "Mask-to-layer switch state: tab="
              << edit_workspace->property("editInspectorTab").toString().toStdString()
              << " tool=" << edit_workspace->property("editTool").toString().toStdString()
              << " scope="
              << active_edit_target_bar->property("targetScope").toString().toStdString()
              << " mode="
              << active_edit_target_mode_label->property("text").toString().toStdString()
              << '\n';
    return fail("mask Edit Layer should switch to the linked adjustment layer without leaving the Adjust panel");
  }
  auto* active_layer_mask_child_edit_mask_button =
      find_named_object(edit_workspace, QStringLiteral("activeLayerMaskChildEditMaskButton"));
  if (active_layer_mask_child_edit_mask_button == nullptr ||
      !item_visible(qobject_cast<QQuickItem*>(active_layer_mask_child_edit_mask_button)) ||
      active_layer_mask_child_edit_mask_button->property("text").toString() !=
          QStringLiteral("Edit Selection") ||
      !QMetaObject::invokeMethod(active_layer_mask_child_edit_mask_button, "click")) {
    return fail("layer-target linked selection row should expose one-click selection editing in the current Adjust panel");
  }
  process_layout_events(4);
  if (edit_workspace->property("editInspectorTab").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("masks") ||
      active_edit_target_bar->property("targetScope").toString() != QStringLiteral("mask") ||
      active_edit_target_mode_label->property("text").toString() != QStringLiteral("Selection") ||
      !item_visible(qobject_cast<QQuickItem*>(
          find_named_object(edit_workspace, QStringLiteral("maskFirstInspectorGroup"))))) {
    std::cerr << "Layer-to-mask switch state: tab="
              << edit_workspace->property("editInspectorTab").toString().toStdString()
              << " tool=" << edit_workspace->property("editTool").toString().toStdString()
              << " scope="
              << active_edit_target_bar->property("targetScope").toString().toStdString()
              << " mode="
              << active_edit_target_mode_label->property("text").toString().toStdString()
              << '\n';
    return fail("linked selection Edit Selection should switch back to selection editing without leaving the Adjust panel");
  }
  trace_smoke_progress("copy vocabulary checked");
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_COPY_VOCABULARY")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    editor_preview.setImageProvider(nullptr);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return EXIT_SUCCESS;
  }
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
  process_layout_events(4);
  if (!photo_layer_text_editor->property("visible").toBool() ||
      photo_layer_text_content_field->property("text").toString() != QStringLiteral("Layer") ||
      !photo_layer_text_bold_button->property("enabled").toBool() ||
      !photo_layer_text_align_center_button->property("enabled").toBool()) {
    return fail("text layers should expose editable content and style controls in the Layers inspector");
  }
  const auto layer_icon_button_accessibility_ready =
      photo_layer_clip_button->property("accessibilityReady").toBool() &&
      photo_layer_mask_button->property("accessibilityReady").toBool() &&
      photo_layer_transform_button->property("accessibilityReady").toBool() &&
      photo_layer_visibility_button->property("accessibilityReady").toBool() &&
      photo_layer_row_solo_button->property("accessibilityReady").toBool() &&
      photo_layer_selection_button->property("accessibilityReady").toBool() &&
      copy_photo_layer_adjustments_button->property("accessibilityReady").toBool() &&
      paste_photo_layer_adjustments_button->property("accessibilityReady").toBool() &&
      paste_global_adjustments_to_photo_layer_button->property("accessibilityReady").toBool() &&
      sync_photo_layer_adjustments_button->property("accessibilityReady").toBool() &&
      clear_photo_layer_selection_button->property("accessibilityReady").toBool() &&
      photo_layer_lock_button->property("accessibilityReady").toBool() &&
      photo_layer_move_up_button->property("accessibilityReady").toBool() &&
      photo_layer_move_down_button->property("accessibilityReady").toBool() &&
      photo_layer_duplicate_button->property("accessibilityReady").toBool() &&
      photo_layer_delete_button->property("accessibilityReady").toBool();
  if (!layer_icon_button_accessibility_ready ||
      photo_layer_clip_button->property("resolvedAccessibleName").toString().isEmpty() ||
      photo_layer_mask_button->property("resolvedAccessibleName").toString().isEmpty() ||
      photo_layer_transform_button->property("resolvedAccessibleName").toString().isEmpty() ||
      photo_layer_visibility_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Toggle layer visibility.") ||
      photo_layer_row_solo_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Show only this layer.") ||
      photo_layer_selection_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Select this layer for layer commands.") ||
      copy_photo_layer_adjustments_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Copy active layer adjustments.") ||
      paste_photo_layer_adjustments_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Paste copied adjustments onto the active layer.") ||
      paste_global_adjustments_to_photo_layer_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Paste Base Image adjustments onto the active layer.") ||
      sync_photo_layer_adjustments_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Sync active layer adjustments to selected layers.") ||
      clear_photo_layer_selection_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Clear multi-layer selection.") ||
      photo_layer_lock_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Lock layer edits.") ||
      photo_layer_duplicate_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Duplicate layer.") ||
      photo_layer_delete_button->property("resolvedAccessibleName").toString() !=
          QStringLiteral("Delete layer.")) {
    return fail("icon-only layer controls should expose deterministic accessible names from their tooltips");
  }
  if (photo_layer_properties_panel != nullptr) {
    photo_layer_properties_panel->setProperty("detailsExpanded", true);
    QCoreApplication::processEvents();
  }
  QMetaObject::invokeMethod(photo_layer_name_field, "forceInputFocus");
  photo_layer_name_field->setProperty("text", QStringLiteral("Smoke Renamed"));
  QCoreApplication::processEvents();
  if (editor_preview.photoLayers().at(0).toMap().value(QStringLiteral("name")).toString() !=
      QStringLiteral("Smoke Renamed")) {
    return fail("layer properties should expose a progressive editable rename control");
  }
  QMetaObject::invokeMethod(photo_layer_text_content_field, "forceInputFocus");
  photo_layer_text_content_field->setProperty("text", QStringLiteral("Smoke Caption"));
  QMetaObject::invokeMethod(photo_layer_text_font_field, "forceInputFocus");
  photo_layer_text_font_field->setProperty("text", QStringLiteral("Avenir"));
  QMetaObject::invokeMethod(photo_layer_text_color_field, "forceInputFocus");
  photo_layer_text_color_field->setProperty("text", QStringLiteral("#33CCAA"));
  if (!QMetaObject::invokeMethod(photo_layer_text_bold_button, "click") ||
      !QMetaObject::invokeMethod(photo_layer_text_align_center_button, "click")) {
    return fail("text layer style buttons should be clickable from the Layers inspector");
  }
  QCoreApplication::processEvents();
  QMetaObject::invokeMethod(photo_layer_name_field, "clearInputFocus");
  QMetaObject::invokeMethod(photo_layer_text_content_field, "clearInputFocus");
  QMetaObject::invokeMethod(photo_layer_text_font_field, "clearInputFocus");
  QMetaObject::invokeMethod(photo_layer_text_color_field, "clearInputFocus");
  const auto smoked_text_layer = editor_preview.photoLayers().at(0).toMap();
  if (smoked_text_layer.value(QStringLiteral("text")).toString() != QStringLiteral("Smoke Caption") ||
      smoked_text_layer.value(QStringLiteral("fontFamily")).toString() != QStringLiteral("Avenir") ||
      smoked_text_layer.value(QStringLiteral("textColor")).toString() != QStringLiteral("#33ccaa") ||
      smoked_text_layer.value(QStringLiteral("fontWeight")).toString() != QStringLiteral("bold") ||
      smoked_text_layer.value(QStringLiteral("textAlignment")).toString() != QStringLiteral("center")) {
    std::cerr << "Text layer values: text="
              << smoked_text_layer.value(QStringLiteral("text")).toString().toStdString()
              << " font="
              << smoked_text_layer.value(QStringLiteral("fontFamily")).toString().toStdString()
              << " color="
              << smoked_text_layer.value(QStringLiteral("textColor")).toString().toStdString()
              << " weight="
              << smoked_text_layer.value(QStringLiteral("fontWeight")).toString().toStdString()
              << " align="
              << smoked_text_layer.value(QStringLiteral("textAlignment")).toString().toStdString()
              << "\n";
    return fail("text layer inspector controls should update text content, font, color, weight, and alignment");
  }
  if (!editor_preview.addAdjustmentLayer(QStringLiteral("Sync Source")) ||
      !editor_preview.addAdjustmentLayer(QStringLiteral("Sync Target"))) {
    return fail("layer adjustment command smoke setup should create source and target adjustment layers");
  }
  if (!editor_preview.setPhotoLayerAdjustmentValue(1, QStringLiteral("exposure"), 0.64)) {
    return fail("layer adjustment command smoke setup should set source exposure");
  }
  if (!editor_preview.setPhotoLayerAdjustmentValue(1, QStringLiteral("temperature"), -18.0)) {
    return fail("layer adjustment command smoke setup should set source temperature");
  }
  if (!editor_preview.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), -0.25)) {
    return fail("layer adjustment command smoke setup should create source and target adjustment layers");
  }
  editor_preview.setExposure(-0.91);
  QCoreApplication::processEvents();
  QVariant edit_source_layer;
  QVariant source_layer_exposure;
  QVariant edit_layer_target_signature;
  QVariant wrote_source_layer_exposure;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editPhotoLayerInspectorTarget",
          Q_RETURN_ARG(QVariant, edit_source_layer),
          Q_ARG(QVariant, QVariant(1))) ||
      !edit_source_layer.toBool()) {
    return fail("clicking an adjustment layer should route to an editable layer target");
  }
  process_layout_events(6);
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "primaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, source_layer_exposure),
          Q_ARG(QVariant, QVariant(QStringLiteral("exposure"))),
          Q_ARG(QVariant, QVariant(editor_preview.exposure()))) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "activeEditTargetSignature",
          Q_RETURN_ARG(QVariant, edit_layer_target_signature)) ||
      edit_workspace->property("editInspectorTab").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("editTool").toString() != QStringLiteral("adjust") ||
      edit_workspace->property("activePhotoLayerIndex").toInt() != 1 ||
      active_edit_target_bar->property("targetScope").toString() !=
          QStringLiteral("adjustment-layer") ||
      active_edit_target_mode_label->property("text").toString() !=
          QStringLiteral("Layer") ||
      !active_edit_target_breadcrumb_label->property("text").toString().contains(
          QStringLiteral("Sync Source")) ||
      !edit_layer_target_signature.toString().contains(QStringLiteral("scope:adjustment-layer")) ||
      std::abs(source_layer_exposure.toDouble() - 0.64) > 1e-6 ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setPrimaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, wrote_source_layer_exposure),
          Q_ARG(QVariant, QVariant(QStringLiteral("exposure"))),
          Q_ARG(QVariant, QVariant(0.71))) ||
      !wrote_source_layer_exposure.toBool()) {
    std::cerr << "Layer edit target route state: tab="
              << edit_workspace->property("editInspectorTab").toString().toStdString()
              << " tool=" << edit_workspace->property("editTool").toString().toStdString()
              << " activeLayer=" << edit_workspace->property("activePhotoLayerIndex").toInt()
              << " scope=" << active_edit_target_bar->property("targetScope").toString().toStdString()
              << " mode=" << active_edit_target_mode_label->property("text").toString().toStdString()
              << " breadcrumb="
              << active_edit_target_breadcrumb_label->property("text").toString().toStdString()
              << " primaryExposure=" << source_layer_exposure.toDouble()
              << " signature=" << edit_layer_target_signature.toString().toStdString()
              << "\n";
    return fail("adjustment layer clicks should switch the inspector to that layer's adjustment values");
  }
  auto* active_layer_full_reset_button =
      find_named_object(edit_workspace, QStringLiteral("activeLayerFullResetButton"));
  if (find_named_object(edit_workspace, QStringLiteral("activeAdjustmentTargetGuard")) != nullptr ||
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentTargetGuardTitleLabel")) != nullptr ||
      find_named_object(edit_workspace, QStringLiteral("activeAdjustmentTargetGuardStatusLabel")) != nullptr ||
      active_layer_full_reset_button != nullptr) {
    return fail("adjust inspector should not render the removed target guard while editing an adjustment layer");
  }
  auto source_adjustment_layer = editor_preview.photoLayers().at(1).toMap();
  auto target_adjustment_layer = editor_preview.photoLayers().at(0).toMap();
  if (std::abs(source_adjustment_layer.value(QStringLiteral("exposure")).toDouble() - 0.71) >
          1e-6 ||
      std::abs(target_adjustment_layer.value(QStringLiteral("exposure")).toDouble() + 0.25) >
          1e-6 ||
      std::abs(editor_preview.exposure() + 0.91) > 1e-6) {
    return fail("editing one clicked adjustment layer should not mutate sibling layers or the base image");
  }
  QVariant edit_target_layer;
  QVariant target_layer_exposure;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editPhotoLayerInspectorTarget",
          Q_RETURN_ARG(QVariant, edit_target_layer),
          Q_ARG(QVariant, QVariant(0))) ||
      !edit_target_layer.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "primaryAdjustmentValue",
          Q_RETURN_ARG(QVariant, target_layer_exposure),
          Q_ARG(QVariant, QVariant(QStringLiteral("exposure"))),
          Q_ARG(QVariant, QVariant(editor_preview.exposure()))) ||
      edit_workspace->property("activePhotoLayerIndex").toInt() != 0 ||
      !active_edit_target_breadcrumb_label->property("text").toString().contains(
          QStringLiteral("Sync Target")) ||
      std::abs(target_layer_exposure.toDouble() + 0.25) > 1e-6) {
    return fail("switching between adjustment layers should immediately swap inspector values");
  }
  if (!editor_preview.setPhotoLayerAdjustmentValue(1, QStringLiteral("exposure"), 0.64)) {
    return fail("layer edit target route proof should restore the copy/paste source fixture");
  }
  edit_workspace->setProperty("editTool", QStringLiteral("layers"));
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
  QVariant target_selected;
  QVariant source_selected;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, target_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !target_selected.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, source_selected),
          Q_ARG(QVariant, QVariant(1)),
          Q_ARG(QVariant, QVariant(true))) ||
      !source_selected.toBool()) {
    return fail("Studio Layers should expose a callable selected-layer target model");
  }
  process_layout_events(6);
  photo_layer_selection_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerSelectionButton"));
  photo_layer_adjustment_command_bar =
      find_named_object(edit_workspace, QStringLiteral("photoLayerAdjustmentCommandBar"));
  copy_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("copyPhotoLayerAdjustmentsButton"));
  paste_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("pastePhotoLayerAdjustmentsButton"));
  paste_global_adjustments_to_photo_layer_button =
      find_named_object(edit_workspace, QStringLiteral("pasteGlobalAdjustmentsToPhotoLayerButton"));
  toggle_photo_layer_adjustments_preview_button =
      find_named_object(edit_workspace, QStringLiteral("togglePhotoLayerAdjustmentsPreviewButton"));
  sync_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("syncPhotoLayerAdjustmentsButton"));
  clear_photo_layer_selection_button =
      find_named_object(edit_workspace, QStringLiteral("clearPhotoLayerSelectionButton"));
  auto* photo_layer_adjustment_command_selection_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerAdjustmentCommandSelectionLabel"));
  if (photo_layer_selection_button == nullptr || photo_layer_adjustment_command_bar == nullptr ||
      copy_photo_layer_adjustments_button == nullptr ||
      paste_photo_layer_adjustments_button == nullptr ||
      paste_global_adjustments_to_photo_layer_button == nullptr ||
      toggle_photo_layer_adjustments_preview_button == nullptr ||
      sync_photo_layer_adjustments_button == nullptr ||
      clear_photo_layer_selection_button == nullptr ||
      photo_layer_adjustment_command_selection_label == nullptr ||
      !photo_layer_adjustment_command_bar->property("visible").toBool() ||
      !copy_photo_layer_adjustments_button->property("enabled").toBool() ||
      !paste_global_adjustments_to_photo_layer_button->property("enabled").toBool() ||
      !sync_photo_layer_adjustments_button->property("enabled").toBool() ||
      !clear_photo_layer_selection_button->property("enabled").toBool() ||
      !photo_layer_selection_button->property("active").toBool() ||
      !photo_layer_adjustment_command_selection_label->property("text")
           .toString()
           .contains(QStringLiteral("1 sync target"))) {
    return fail("Studio Layers should expose active-to-selected copy/paste/sync layer adjustment commands");
  }
  if (!QMetaObject::invokeMethod(copy_photo_layer_adjustments_button, "click") ||
      !editor_preview.hasCopiedPhotoLayerAdjustments()) {
    return fail("copy layer adjustments command should copy the active adjustment recipe");
  }
  process_layout_events(4);
  paste_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("pastePhotoLayerAdjustmentsButton"));
  if (paste_photo_layer_adjustments_button == nullptr ||
      !paste_photo_layer_adjustments_button->property("enabled").toBool()) {
    return fail("paste layer adjustments command should enable after a layer recipe is copied");
  }
  QVariant paste_target_selected;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, paste_target_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !paste_target_selected.toBool()) {
    return fail("Studio Layers should switch the active paste target to a selected layer");
  }
  process_layout_events(4);
  paste_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("pastePhotoLayerAdjustmentsButton"));
  if (paste_photo_layer_adjustments_button == nullptr ||
      !paste_photo_layer_adjustments_button->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(paste_photo_layer_adjustments_button, "click")) {
    return fail("paste layer adjustments command should dispatch from the Layers inspector");
  }
  process_layout_events(4);
  auto synced_adjustment_layer = editor_preview.photoLayers().at(0).toMap();
  if (std::abs(synced_adjustment_layer.value(QStringLiteral("exposure")).toDouble() - 0.64) >
          1e-6 ||
      std::abs(synced_adjustment_layer.value(QStringLiteral("temperature")).toDouble() + 18.0) >
          1e-6 ||
      !editor_preview.status().contains(QStringLiteral("Pasted layer adjustments"))) {
    return fail("paste layer adjustments command should copy the active recipe to the active target");
  }
  QVariant sync_target_selected;
  QVariant sync_source_selected;
  if (!editor_preview.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), -0.25) ||
      !editor_preview.setPhotoLayerAdjustmentValue(0, QStringLiteral("temperature"), 8.0) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, sync_target_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !sync_target_selected.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, sync_source_selected),
          Q_ARG(QVariant, QVariant(1)),
          Q_ARG(QVariant, QVariant(true))) ||
      !sync_source_selected.toBool()) {
    return fail("Studio Layers should restore the active source plus selected sync target");
  }
  process_layout_events(4);
  sync_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("syncPhotoLayerAdjustmentsButton"));
  photo_layer_adjustment_command_selection_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerAdjustmentCommandSelectionLabel"));
  if (sync_photo_layer_adjustments_button == nullptr ||
      photo_layer_adjustment_command_selection_label == nullptr ||
      !sync_photo_layer_adjustments_button->property("enabled").toBool() ||
      !photo_layer_adjustment_command_selection_label->property("text")
           .toString()
           .contains(QStringLiteral("1 sync target")) ||
      !QMetaObject::invokeMethod(sync_photo_layer_adjustments_button, "click")) {
    return fail("sync layer adjustments command should dispatch from the Layers inspector");
  }
  process_layout_events(4);
  synced_adjustment_layer = editor_preview.photoLayers().at(0).toMap();
  if (std::abs(synced_adjustment_layer.value(QStringLiteral("exposure")).toDouble() - 0.64) >
          1e-6 ||
      std::abs(synced_adjustment_layer.value(QStringLiteral("temperature")).toDouble() + 18.0) >
          1e-6 ||
      !editor_preview.status().contains(QStringLiteral("Synced layer adjustments"))) {
    return fail("sync layer adjustments command should copy the active adjustment recipe to selected layers");
  }
  editor_preview.setExposure(0.19);
  editor_preview.setTemperature(14.0);
  process_layout_events(4);
  paste_global_adjustments_to_photo_layer_button =
      find_named_object(edit_workspace, QStringLiteral("pasteGlobalAdjustmentsToPhotoLayerButton"));
  if (paste_global_adjustments_to_photo_layer_button == nullptr ||
      !paste_global_adjustments_to_photo_layer_button->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(paste_global_adjustments_to_photo_layer_button, "click")) {
    return fail("paste-from-global layer command should dispatch from the Layers inspector");
  }
  process_layout_events(4);
  auto global_pasted_adjustment_layer = editor_preview.photoLayers().at(1).toMap();
  const auto global_pasted_group_label =
      global_pasted_adjustment_layer.value(QStringLiteral("changedAdjustmentGroupLabel")).toString();
  if (std::abs(global_pasted_adjustment_layer.value(QStringLiteral("exposure")).toDouble() -
               0.19) > 1e-6 ||
      std::abs(global_pasted_adjustment_layer.value(QStringLiteral("temperature")).toDouble() -
               14.0) > 1e-6 ||
      !global_pasted_adjustment_layer.value(QStringLiteral("hasNonNeutralAdjustments")).toBool() ||
      !global_pasted_group_label.contains(QStringLiteral("WB")) ||
      !global_pasted_group_label.contains(QStringLiteral("Light")) ||
      !editor_preview.status().contains(QStringLiteral("Pasted Base Image adjustments"))) {
    return fail("paste-from-global command should replace the active layer recipe with Base Image adjustments");
  }
  QVariantList badge_lut_stack_table;
  for (int sample_index = 0; sample_index < 8; ++sample_index) {
    badge_lut_stack_table.append(QVariant(QVariantList{0.92, 0.08, 0.18, 1.0}));
  }
  QVariantMap badge_lut_entry;
  badge_lut_entry.insert(QStringLiteral("enabled"), true);
  badge_lut_entry.insert(QStringLiteral("strength"), 72.0);
  badge_lut_entry.insert(QStringLiteral("name"), QStringLiteral("Badge Runtime LUT"));
  badge_lut_entry.insert(QStringLiteral("size"), 2);
  badge_lut_entry.insert(QStringLiteral("table"), badge_lut_stack_table);
  badge_lut_entry.insert(QStringLiteral("domainMin"), QVariantList{0.0, 0.0, 0.0});
  badge_lut_entry.insert(QStringLiteral("domainMax"), QVariantList{1.0, 1.0, 1.0});
  badge_lut_entry.insert(QStringLiteral("inputColorSpace"), QStringLiteral("sRGB"));
  badge_lut_entry.insert(QStringLiteral("outputColorSpace"), QStringLiteral("sRGB"));
  if (!editor_preview.setPhotoLayerAdjustmentValue(1, QStringLiteral("curveShadows"), 18.0) ||
      !editor_preview.setPhotoLayerAdjustmentString(
          1, QStringLiteral("colorMixerModel"), QStringLiteral("HSL")) ||
      !editor_preview.setPhotoLayerAdjustmentString(
          1, QStringLiteral("hslChannel"), QStringLiteral("Reds")) ||
      !editor_preview.setPhotoLayerAdjustmentValue(1, QStringLiteral("hueAdjustment"), -22.0) ||
      !editor_preview.setPhotoLayerAdjustmentValue(1, QStringLiteral("gradingHighlightsHue"), 0.18) ||
      !editor_preview.setPhotoLayerLutStack(1, QVariantList{badge_lut_entry})) {
    return fail("changed group badge proof should seed curve, HSL, grading, and LUT edits on the active adjustment layer");
  }
  process_layout_events(4);
  selected_layer_header_changed_groups_label =
      find_named_object(edit_workspace, QStringLiteral("selectedLayerHeaderChangedGroupsLabel"));
  QQuickItem* runtime_changed_group_layer_row = nullptr;
  for (auto* row : find_named_items(edit_workspace, QStringLiteral("photoLayerRow"))) {
    if (row != nullptr && row->property("layerIndex").toInt() == 1) {
      runtime_changed_group_layer_row = row;
      break;
    }
  }
  const auto runtime_changed_group_signature =
      runtime_changed_group_layer_row == nullptr
          ? QString()
          : runtime_changed_group_layer_row->property("changedAdjustmentGroupSignature").toString();
  const auto runtime_changed_group_summary =
      runtime_changed_group_layer_row == nullptr
          ? QString()
          : runtime_changed_group_layer_row->property("localAdjustmentSummary").toString();
  if (selected_layer_header_changed_groups_label == nullptr ||
      runtime_changed_group_layer_row == nullptr ||
      !item_visible(runtime_changed_group_layer_row) ||
      !runtime_changed_group_signature.contains(QStringLiteral("WB")) ||
      !runtime_changed_group_signature.contains(QStringLiteral("Light")) ||
      !runtime_changed_group_signature.contains(QStringLiteral("Curve")) ||
      !runtime_changed_group_signature.contains(QStringLiteral("HSL")) ||
      !runtime_changed_group_signature.contains(QStringLiteral("Grade")) ||
      !runtime_changed_group_signature.contains(QStringLiteral("LUT")) ||
      !runtime_changed_group_summary.contains(QStringLiteral("Layer edits:")) ||
      !runtime_changed_group_summary.contains(QStringLiteral("Curve")) ||
      !runtime_changed_group_summary.contains(QStringLiteral("HSL Reds")) ||
      !runtime_changed_group_summary.contains(QStringLiteral("Grade")) ||
      !runtime_changed_group_summary.contains(QStringLiteral("LUT")) ||
      !selected_layer_header_changed_groups_label->property("text")
           .toString()
           .contains(QStringLiteral("Layer edits:")) ||
      !selected_layer_header_changed_groups_label->property("text")
           .toString()
           .contains(QStringLiteral("Curve")) ||
      !selected_layer_header_changed_groups_label->property("text")
           .toString()
           .contains(QStringLiteral("HSL Reds")) ||
      !selected_layer_header_changed_groups_label->property("text")
           .toString()
           .contains(QStringLiteral("Grade")) ||
      !selected_layer_header_changed_groups_label->property("text")
           .toString()
           .contains(QStringLiteral("LUT"))) {
    std::cerr << "Changed group badge state: signature="
              << runtime_changed_group_signature.toStdString()
              << " summary=" << runtime_changed_group_summary.toStdString()
              << " header="
              << (selected_layer_header_changed_groups_label == nullptr
                      ? std::string("null")
                      : selected_layer_header_changed_groups_label->property("text")
                            .toString()
                            .toStdString())
              << '\n';
    return fail("edited adjustment layers should show changed group badges and summaries in the layer UI");
  }
  toggle_photo_layer_adjustments_preview_button =
      find_named_object(edit_workspace, QStringLiteral("togglePhotoLayerAdjustmentsPreviewButton"));
  if (toggle_photo_layer_adjustments_preview_button == nullptr ||
      !toggle_photo_layer_adjustments_preview_button->property("enabled").toBool() ||
      toggle_photo_layer_adjustments_preview_button->property("active").toBool() ||
      !QMetaObject::invokeMethod(toggle_photo_layer_adjustments_preview_button, "click")) {
    return fail("edited adjustment layers should expose a before/after preview toggle");
  }
  process_layout_events(4);
  toggle_photo_layer_adjustments_preview_button =
      find_named_object(edit_workspace, QStringLiteral("togglePhotoLayerAdjustmentsPreviewButton"));
  auto preview_bypassed_adjustment_layer = editor_preview.photoLayers().at(1).toMap();
  if (toggle_photo_layer_adjustments_preview_button == nullptr ||
      !toggle_photo_layer_adjustments_preview_button->property("active").toBool() ||
      !preview_bypassed_adjustment_layer.value(QStringLiteral("adjustmentsPreviewBypassed"))
           .toBool() ||
      std::abs(preview_bypassed_adjustment_layer.value(QStringLiteral("exposure")).toDouble() -
               0.19) > 1e-6 ||
      std::abs(preview_bypassed_adjustment_layer.value(QStringLiteral("temperature")).toDouble() -
               14.0) > 1e-6 ||
      !preview_bypassed_adjustment_layer.value(QStringLiteral("hasNonNeutralAdjustments"))
           .toBool() ||
      !preview_bypassed_adjustment_layer.value(QStringLiteral("layerAuditLabel"))
           .toString()
           .contains(QStringLiteral("preview: before")) ||
      !editor_preview.status().contains(QStringLiteral("Showing layer before adjustments"))) {
    return fail("layer before/after preview should bypass only the rendered adjustment recipe");
  }
  if (!QMetaObject::invokeMethod(toggle_photo_layer_adjustments_preview_button, "click")) {
    return fail("layer before/after preview toggle should restore the adjusted preview");
  }
  process_layout_events(4);
  toggle_photo_layer_adjustments_preview_button =
      find_named_object(edit_workspace, QStringLiteral("togglePhotoLayerAdjustmentsPreviewButton"));
  auto preview_restored_adjustment_layer = editor_preview.photoLayers().at(1).toMap();
  if (toggle_photo_layer_adjustments_preview_button == nullptr ||
      toggle_photo_layer_adjustments_preview_button->property("active").toBool() ||
      preview_restored_adjustment_layer.value(QStringLiteral("adjustmentsPreviewBypassed"))
          .toBool() ||
      std::abs(preview_restored_adjustment_layer.value(QStringLiteral("exposure")).toDouble() -
               0.19) > 1e-6 ||
      std::abs(preview_restored_adjustment_layer.value(QStringLiteral("temperature")).toDouble() -
               14.0) > 1e-6 ||
      !editor_preview.status().contains(QStringLiteral("Showing layer with adjustments"))) {
    return fail("layer before/after preview should restore without changing the adjustment recipe");
  }
  if (!QMetaObject::invokeMethod(clear_photo_layer_selection_button, "click")) {
    return fail("clear layer selection command should be clickable");
  }
  process_layout_events(4);
  clear_photo_layer_selection_button =
      find_named_object(edit_workspace, QStringLiteral("clearPhotoLayerSelectionButton"));
  sync_photo_layer_adjustments_button =
      find_named_object(edit_workspace, QStringLiteral("syncPhotoLayerAdjustmentsButton"));
  if (clear_photo_layer_selection_button == nullptr ||
      sync_photo_layer_adjustments_button == nullptr ||
      clear_photo_layer_selection_button->property("enabled").toBool() ||
      sync_photo_layer_adjustments_button->property("enabled").toBool()) {
    return fail("clearing layer selection should disable multi-target sync commands");
  }
  if (!editor_preview.deletePhotoLayer(0) || !editor_preview.deletePhotoLayer(0)) {
    return fail("layer adjustment command smoke cleanup should remove temporary adjustment layers");
  }
  QVariant restored_layer_selected;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayerSelection",
          Q_RETURN_ARG(QVariant, restored_layer_selected),
          Q_ARG(QVariant, QVariant(0)),
          Q_ARG(QVariant, QVariant(false))) ||
      !restored_layer_selected.toBool()) {
    return fail("layer adjustment command smoke cleanup should restore an active editable layer");
  }
  process_layout_events(6);
  photo_layer_lock_button = find_named_object(edit_workspace, QStringLiteral("photoLayerLockButton"));
  photo_layer_move_up_button = find_named_object(edit_workspace, QStringLiteral("photoLayerMoveUpButton"));
  photo_layer_move_down_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerMoveDownButton"));
  photo_layer_duplicate_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerDuplicateButton"));
  photo_layer_delete_button = find_named_object(edit_workspace, QStringLiteral("photoLayerDeleteButton"));
  if (photo_layer_lock_button == nullptr || photo_layer_move_up_button == nullptr ||
      photo_layer_move_down_button == nullptr || photo_layer_duplicate_button == nullptr ||
      photo_layer_delete_button == nullptr ||
      photo_layer_move_up_button->property("enabled").toBool() ||
      !photo_layer_move_down_button->property("enabled").toBool() ||
      !photo_layer_duplicate_button->property("enabled").toBool() ||
      !photo_layer_delete_button->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(photo_layer_lock_button, "click")) {
    return fail("layer structure row should expose lock, reorder, duplicate, and delete affordances");
  }
  QCoreApplication::processEvents();
  photo_layer_lock_button = find_named_object(edit_workspace, QStringLiteral("photoLayerLockButton"));
  photo_layer_duplicate_button = find_named_object(edit_workspace, QStringLiteral("photoLayerDuplicateButton"));
  photo_layer_delete_button = find_named_object(edit_workspace, QStringLiteral("photoLayerDeleteButton"));
  photo_layer_name_field = find_named_object(edit_workspace, QStringLiteral("photoLayerNameField"));
  photo_layer_transform_button = find_named_object(edit_workspace, QStringLiteral("photoLayerTransformButton"));
  if (photo_layer_lock_button == nullptr || photo_layer_duplicate_button == nullptr ||
      photo_layer_delete_button == nullptr || photo_layer_name_field == nullptr ||
      photo_layer_transform_button == nullptr ||
      !photo_layer_lock_button->property("active").toBool() ||
      !photo_layer_transform_button->property("active").toBool() ||
      photo_layer_name_field->property("enabled").toBool() ||
      photo_layer_duplicate_button->property("enabled").toBool() ||
      photo_layer_delete_button->property("enabled").toBool() ||
      !QMetaObject::invokeMethod(photo_layer_lock_button, "click")) {
    std::cerr << "Layer lock smoke state: lockButton="
              << (photo_layer_lock_button == nullptr ? "null" : "present")
              << " duplicateButton="
              << (photo_layer_duplicate_button == nullptr ? "null" : "present")
              << " deleteButton="
              << (photo_layer_delete_button == nullptr ? "null" : "present")
              << " nameField="
              << (photo_layer_name_field == nullptr ? "null" : "present")
              << " transformButton="
              << (photo_layer_transform_button == nullptr ? "null" : "present")
              << " lockActive="
              << (photo_layer_lock_button != nullptr &&
                  photo_layer_lock_button->property("active").toBool())
              << " transformActive="
              << (photo_layer_transform_button != nullptr &&
                  photo_layer_transform_button->property("active").toBool())
              << " nameEnabled="
              << (photo_layer_name_field != nullptr &&
                  photo_layer_name_field->property("enabled").toBool())
              << " duplicateEnabled="
              << (photo_layer_duplicate_button != nullptr &&
                  photo_layer_duplicate_button->property("enabled").toBool())
              << " deleteEnabled="
              << (photo_layer_delete_button != nullptr &&
                  photo_layer_delete_button->property("enabled").toBool())
              << " activePhotoLayerIndex="
              << edit_workspace->property("activePhotoLayerIndex").toInt()
              << " layerCount=" << editor_preview.photoLayerCount() << "\n";
    return fail("locking a layer from Studio should block mutation while keeping selection and inspection visible");
  }
  QCoreApplication::processEvents();
  photo_layer_duplicate_button =
      find_named_object(edit_workspace, QStringLiteral("photoLayerDuplicateButton"));
  const auto layer_count_before_duplicate = editor_preview.photoLayerCount();
  if (photo_layer_duplicate_button == nullptr ||
      !QMetaObject::invokeMethod(photo_layer_duplicate_button, "click")) {
    return fail("layer duplicate affordance should dispatch to the layer stack controller");
  }
  QCoreApplication::processEvents();
  if (editor_preview.photoLayerCount() != layer_count_before_duplicate + 1) {
    return fail("duplicating a layer from Studio should add one layer");
  }
  photo_layer_delete_button = find_named_object(edit_workspace, QStringLiteral("photoLayerDeleteButton"));
  if (photo_layer_delete_button == nullptr ||
      !QMetaObject::invokeMethod(photo_layer_delete_button, "click")) {
    return fail("layer delete affordance should dispatch to the layer stack controller");
  }
  QCoreApplication::processEvents();
  if (editor_preview.photoLayerCount() != layer_count_before_duplicate) {
    return fail("deleting a layer from Studio should remove one layer and keep the stack valid");
  }
  auto* layer_visibility_state_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerVisibilityStateLabel"));
  auto* save_layer_visibility_state_button =
      find_named_object(edit_workspace, QStringLiteral("saveLayerVisibilityStateButton"));
  auto* apply_layer_visibility_state_button =
      find_named_object(edit_workspace, QStringLiteral("applyLayerVisibilityStateButton"));
  auto* delete_layer_visibility_state_button =
      find_named_object(edit_workspace, QStringLiteral("deleteLayerVisibilityStateButton"));
  if (layer_visibility_state_label == nullptr || save_layer_visibility_state_button == nullptr ||
      apply_layer_visibility_state_button == nullptr || delete_layer_visibility_state_button == nullptr ||
      layer_visibility_state_label->property("text").toString() != QStringLiteral("No visibility states") ||
      !save_layer_visibility_state_button->property("enabled").toBool() ||
      apply_layer_visibility_state_button->property("enabled").toBool() ||
      delete_layer_visibility_state_button->property("enabled").toBool()) {
    return fail("layer panel should expose disabled visibility-state apply/delete controls until a state exists");
  }
  const auto layer_count_before_visibility_save = editor_preview.photoLayerCount();
  const auto expected_visibility_state_summary =
      QStringLiteral("%1/%2 visible")
          .arg(layer_count_before_visibility_save - 1)
          .arg(layer_count_before_visibility_save);
  if (!editor_preview.setPhotoLayerEnabled(0, false) ||
      !editor_preview.savePhotoLayerVisibilityState(QStringLiteral("Smoke Visibility"))) {
    return fail("layer panel smoke setup should save a layer visibility state");
  }
  for (auto iteration = 0; iteration < 4; ++iteration) {
    QCoreApplication::processEvents();
  }
  layer_visibility_state_label =
      find_named_object(edit_workspace, QStringLiteral("photoLayerVisibilityStateLabel"));
  apply_layer_visibility_state_button =
      find_named_object(edit_workspace, QStringLiteral("applyLayerVisibilityStateButton"));
  delete_layer_visibility_state_button =
      find_named_object(edit_workspace, QStringLiteral("deleteLayerVisibilityStateButton"));
  if (editor_preview.photoLayerVisibilityStateCount() != 1 ||
      layer_visibility_state_label == nullptr || apply_layer_visibility_state_button == nullptr ||
      delete_layer_visibility_state_button == nullptr ||
      !layer_visibility_state_label->property("text").toString().contains(QStringLiteral("Smoke Visibility")) ||
      !layer_visibility_state_label->property("text").toString().contains(expected_visibility_state_summary) ||
      !apply_layer_visibility_state_button->property("enabled").toBool() ||
      !delete_layer_visibility_state_button->property("enabled").toBool()) {
    return fail("layer panel should show the latest saved visibility state and enable restore/delete controls");
  }
  if (!editor_preview.setPhotoLayerEnabled(0, true) ||
      !editor_preview.applyPhotoLayerVisibilityState(0) ||
      editor_preview.photoLayers().at(0).toMap().value(QStringLiteral("enabled")).toBool() ||
      !editor_preview.deletePhotoLayerVisibilityState(0) ||
      !editor_preview.setPhotoLayerEnabled(0, true)) {
    return fail("layer visibility state controls should restore, delete, and allow returning to the full stack");
  }
  QCoreApplication::processEvents();
  QVariant layer_transform_selected;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setActivePhotoLayer",
          Q_RETURN_ARG(QVariant, layer_transform_selected),
          Q_ARG(QVariant, 0)) ||
      !layer_transform_selected.toBool() ||
      edit_workspace->property("editInspectorTab").toString() != QStringLiteral("layers")) {
    return fail("layer panel should expose a selectable direct-transform target");
  }
  QObject* photo_layer_transform_overlay = nullptr;
  QObject* photo_layer_transform_frame = nullptr;
  QObject* photo_layer_move_handle = nullptr;
  QObject* photo_layer_rotate_handle = nullptr;
  QObject* photo_layer_scale_handle = nullptr;
  QObject* photo_layer_transform_label = nullptr;
  if (!wait_for_condition([&]() {
        photo_layer_transform_button =
            find_named_object(edit_workspace, QStringLiteral("photoLayerTransformButton"));
        photo_layer_transform_overlay =
            find_named_object(edit_workspace, QStringLiteral("photoLayerTransformOverlay"));
        photo_layer_transform_frame =
            find_named_object(edit_workspace, QStringLiteral("photoLayerTransformFrame"));
        photo_layer_move_handle =
            find_named_object(edit_workspace, QStringLiteral("photoLayerTransformMoveHandle"));
        photo_layer_rotate_handle =
            find_named_object(edit_workspace, QStringLiteral("photoLayerTransformRotateHandle"));
        photo_layer_scale_handle = find_named_object(
            edit_workspace, QStringLiteral("photoLayerTransformBottomRightHandle"));
        photo_layer_transform_label =
            find_named_object(edit_workspace, QStringLiteral("photoLayerTransformLabel"));
        return photo_layer_transform_button != nullptr &&
               photo_layer_transform_overlay != nullptr &&
               photo_layer_transform_frame != nullptr &&
               photo_layer_move_handle != nullptr &&
               photo_layer_rotate_handle != nullptr &&
               photo_layer_scale_handle != nullptr &&
               photo_layer_transform_label != nullptr &&
               photo_layer_transform_overlay->property("active").toBool() &&
               photo_layer_transform_overlay->property("visible").toBool() &&
               photo_layer_transform_button->property("active").toBool();
      })) {
    std::cerr << "Layer transform handles state: button="
              << (photo_layer_transform_button == nullptr ? "null" : "present")
              << " overlay="
              << (photo_layer_transform_overlay == nullptr ? "null" : "present")
              << " frame=" << (photo_layer_transform_frame == nullptr ? "null" : "present")
              << " move=" << (photo_layer_move_handle == nullptr ? "null" : "present")
              << " rotate=" << (photo_layer_rotate_handle == nullptr ? "null" : "present")
              << " scale=" << (photo_layer_scale_handle == nullptr ? "null" : "present")
              << " label=" << (photo_layer_transform_label == nullptr ? "null" : "present")
              << " editTool=" << edit_workspace->property("editTool").toString().toStdString()
              << " activeLayer=" << edit_workspace->property("activePhotoLayerIndex").toInt()
              << "\n";
  }
  if (photo_layer_transform_overlay == nullptr || photo_layer_transform_frame == nullptr ||
      photo_layer_move_handle == nullptr || photo_layer_rotate_handle == nullptr ||
      photo_layer_scale_handle == nullptr || photo_layer_transform_label == nullptr ||
      !photo_layer_transform_overlay->property("active").toBool() ||
      !photo_layer_transform_overlay->property("visible").toBool() ||
      !photo_layer_transform_button->property("active").toBool()) {
    return fail("active photo layers should expose direct on-canvas transform handles");
  }
  const auto transform_scale_x =
      photo_layer_transform_overlay->property("viewportScaleX").toDouble();
  const auto transform_scale_y =
      photo_layer_transform_overlay->property("viewportScaleY").toDouble();
  const auto transform_start = editor_preview.photoLayers().at(0).toMap();
  QVariant transform_moved;
  const auto transform_move_invoked = QMetaObject::invokeMethod(
          photo_layer_transform_overlay,
          "moveByViewportDelta",
          Q_RETURN_ARG(QVariant, transform_moved),
          Q_ARG(QVariant, 24.0),
          Q_ARG(QVariant, 12.0));
  if (transform_scale_x <= 0.0 || transform_scale_y <= 0.0 ||
      !transform_move_invoked || !transform_moved.toBool()) {
    std::cerr << "Layer transform move state: scaleX=" << transform_scale_x
              << " scaleY=" << transform_scale_y
              << " invoked=" << transform_move_invoked
              << " returned=" << transform_moved.toBool()
              << " active=" << photo_layer_transform_overlay->property("active").toBool()
              << " visible=" << photo_layer_transform_overlay->property("visible").toBool()
              << " enabled=" << photo_layer_transform_overlay->property("enabled").toBool()
              << " locked=" << photo_layer_transform_overlay->property("locked").toBool()
              << "\n";
    return fail("layer transform overlay should expose a precise move operation");
  }
  QCoreApplication::processEvents();
  const auto moved_layer = editor_preview.photoLayers().at(0).toMap();
  const auto expected_layer_x =
      qRound(transform_start.value(QStringLiteral("x")).toDouble() + 24.0 / transform_scale_x);
  const auto expected_layer_y =
      qRound(transform_start.value(QStringLiteral("y")).toDouble() + 12.0 / transform_scale_y);
  if (moved_layer.value(QStringLiteral("x")).toInt() != expected_layer_x ||
      moved_layer.value(QStringLiteral("y")).toInt() != expected_layer_y ||
      !photo_layer_transform_label->property("text").toString().contains(QStringLiteral("X "))) {
    return fail("layer transform overlay movement should update exact layer transform metadata");
  }
  if (!editor_preview.setPhotoLayerOpacity(0, 0.42) ||
      !editor_preview.setPhotoLayerBlendMode(0, QStringLiteral("multiply")) ||
      !editor_preview.setPhotoLayerClippingMask(0, true) ||
      !editor_preview.setPhotoLayerMaskEnabled(0, true)) {
    return fail("layer panel smoke setup should update direct layer control state");
  }
  for (auto iteration = 0; iteration < 4; ++iteration) {
    QCoreApplication::processEvents();
  }
  photo_layer_opacity_label = find_named_object(edit_workspace, QStringLiteral("photoLayerOpacityLabel"));
  photo_layer_opacity_slider = find_named_object(edit_workspace, QStringLiteral("photoLayerOpacitySlider"));
  photo_layer_blend_combo = find_named_object(edit_workspace, QStringLiteral("photoLayerBlendModeComboBox"));
  photo_layer_clip_button = find_named_object(edit_workspace, QStringLiteral("photoLayerClippingMaskButton"));
  photo_layer_mask_button = find_named_object(edit_workspace, QStringLiteral("photoLayerMaskButton"));
  photo_layer_mask_badge = find_named_object(edit_workspace, QStringLiteral("photoLayerMaskBadge"));
  photo_layer_mask_state_pill = find_named_object(edit_workspace, QStringLiteral("photoLayerMaskStatePill"));
  if (photo_layer_opacity_label == nullptr || photo_layer_opacity_slider == nullptr ||
      photo_layer_blend_combo == nullptr || photo_layer_clip_button == nullptr ||
      photo_layer_mask_button == nullptr || photo_layer_mask_badge == nullptr ||
      photo_layer_mask_state_pill == nullptr) {
    return fail("layer panel controls should remain discoverable after layer stack structure changes");
  }
  if (photo_layer_opacity_label->property("text").toString() != QStringLiteral("42%") ||
      std::abs(photo_layer_opacity_slider->property("value").toDouble() - 42.0) > 0.5 ||
      photo_layer_blend_combo->property("currentText").toString() != QStringLiteral("Multiply") ||
      !photo_layer_clip_button->property("active").toBool() ||
      !photo_layer_mask_button->property("active").toBool() ||
      (sparse_layer_stack_mode
           ? photo_layer_mask_badge->property("visible").toBool()
           : !photo_layer_mask_badge->property("visible").toBool()) ||
      (sparse_layer_stack_mode
           ? item_visible(qobject_cast<QQuickItem*>(photo_layer_mask_state_pill))
           : !item_visible(qobject_cast<QQuickItem*>(photo_layer_mask_state_pill)))) {
    std::cerr << "Layer controls smoke state: opacity="
              << photo_layer_opacity_label->property("text").toString().toStdString()
              << " opacitySlider=" << photo_layer_opacity_slider->property("value").toDouble()
              << " blend=" << photo_layer_blend_combo->property("currentText").toString().toStdString()
              << " clipActive=" << photo_layer_clip_button->property("active").toBool()
              << " maskActive=" << photo_layer_mask_button->property("active").toBool()
              << " maskBadgeVisible=" << photo_layer_mask_badge->property("visible").toBool()
              << " fixedMaskVisible=" << item_visible(qobject_cast<QQuickItem*>(photo_layer_mask_state_pill))
              << "\n";
    return fail("layer panel controls should reflect live opacity, blend, clipping, and mask states");
  }
  if (!editor_preview.isolatePhotoLayer(0)) {
    return fail("layer panel smoke setup should isolate the selected layer");
  }
  QCoreApplication::processEvents();
  if (!editor_preview.photoLayerSoloActive() ||
      photo_layer_solo_button->property("text").toString() != QStringLiteral("Isolated") ||
      !photo_layer_clear_solo_button->property("visible").toBool()) {
    return fail("layer panel should show clear isolation while a layer is isolated");
  }
  if (!editor_preview.clearPhotoLayerSolo()) {
    return fail("layer panel should clear isolation from the header control");
  }
  QCoreApplication::processEvents();

  auto expect_capability_registry = [&](const char* count_method,
                                        const char* signature_method,
                                        const QStringList& expected_capabilities,
                                        const char* registry_name,
                                        const char* workflow_name) -> int {
    QVariant capability_count;
    QVariant capability_signature;
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            count_method,
            Q_RETURN_ARG(QVariant, capability_count)) ||
        capability_count.toInt() < expected_capabilities.size() ||
        !QMetaObject::invokeMethod(
            edit_workspace,
            signature_method,
            Q_RETURN_ARG(QVariant, capability_signature))) {
      std::cerr << "workspace should expose the " << registry_name << " capability registry\n";
      return EXIT_FAILURE;
    }
    const auto signature = capability_signature.toString();
    for (const auto& capability : expected_capabilities) {
      if (!signature.contains(capability)) {
        std::cerr << "workspace should expose every " << workflow_name
                  << " workflow capability\n";
        return EXIT_FAILURE;
      }
    }
    return EXIT_SUCCESS;
  };
  const auto edit_recipe_architecture_result =
      expect_capability_registry(
          "editRecipeArchitectureCapabilityCount",
          "editRecipeArchitectureCapabilitySignature",
          {
              QStringLiteral("unified-structured-recipe-model"),
          },
          "Edit Recipes",
          "Edit Recipes");
  if (edit_recipe_architecture_result != EXIT_SUCCESS) {
    return edit_recipe_architecture_result;
  }
  const auto color_science_capability_result =
      expect_capability_registry(
          "colorScienceCapabilityCount",
          "colorScienceCapabilitySignature",
          {
              QStringLiteral("icc-grade-color-management-layer"),
              QStringLiteral("ocio-style-configuration"),
          },
          "Color Science",
          "Color Science");
  if (color_science_capability_result != EXIT_SUCCESS) {
    return color_science_capability_result;
  }
  edit_workspace->setProperty("editTool", QStringLiteral("adjust"));
  edit_workspace->setProperty("loupeEnabled", true);
  edit_workspace->setProperty("loupeX", 96.0);
  edit_workspace->setProperty("loupeY", 72.0);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* loupe_view = edit_workspace->findChild<QObject*>(QStringLiteral("loupeView"));
  if (loupe_view == nullptr || !edit_workspace->property("loupeEnabled").toBool() ||
      edit_workspace->property("loupeX").toDouble() < 95.9 ||
      edit_workspace->property("loupeY").toDouble() < 71.9) {
    return fail("edit workspace should expose a loupe view for close inspection");
  }
  QVariant overlay_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "analysisOverlaySignature",
          Q_RETURN_ARG(QVariant, overlay_signature)) ||
      overlay_signature.toString() !=
          QStringLiteral("active:|panel:false|focus:false|peaking:false|clipping:false|false:false|gamut:false|zone:false")) {
    return fail("analysis overlays should be off by default to keep the edit canvas uncluttered");
  }
  for (auto iteration = 0; iteration < 12 && editor_preview.processing(); ++iteration) {
    QCoreApplication::processEvents();
  }
  if (editor_preview.processing()) {
    return fail("diagnostic overlay render-state proof requires an idle preview before toggling overlays");
  }
  const auto overlay_recipe_before = editor_preview.currentAdjustmentPatchJson();
  const auto overlay_preview_source_before = editor_preview.displayPreviewSource();
  QVariant focus_overlay_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, focus_overlay_result),
          Q_ARG(QVariant, QStringLiteral("focus-check"))) ||
      !focus_overlay_result.toBool()) {
    return fail("edit workspace should expose a focus-check overlay command");
  }
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* focus_check_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("focusCheckOverlay"));
  if (focus_check_overlay == nullptr ||
      !edit_workspace->property("focusCheckOverlayEnabled").toBool()) {
    return fail("edit workspace should expose a focus-check overlay");
  }
  QVariant peaking_overlay_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, peaking_overlay_result),
          Q_ARG(QVariant, QStringLiteral("focus-peaking"))) ||
      !peaking_overlay_result.toBool()) {
    return fail("edit workspace should expose a focus peaking overlay command");
  }
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* focus_peaking_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("focusPeakingOverlay"));
  if (focus_peaking_overlay == nullptr ||
      !edit_workspace->property("focusPeakingOverlayEnabled").toBool() ||
      edit_workspace->property("focusCheckOverlayEnabled").toBool()) {
    return fail("edit workspace should expose focus peaking when source data supports it");
  }
  QVariant clipping_overlay_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, clipping_overlay_result),
          Q_ARG(QVariant, QStringLiteral("clipping"))) ||
      !clipping_overlay_result.toBool()) {
    return fail("edit workspace should expose highlight and shadow clipping overlay commands");
  }
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* clipping_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("clippingOverlay"));
  if (clipping_overlay == nullptr || !edit_workspace->property("clippingOverlayEnabled").toBool() ||
      edit_workspace->property("focusPeakingOverlayEnabled").toBool()) {
    return fail("edit workspace should expose highlight and shadow clipping overlays");
  }
  QVariant false_color_overlay_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, false_color_overlay_result),
          Q_ARG(QVariant, QStringLiteral("false-color"))) ||
      !false_color_overlay_result.toBool()) {
    return fail("edit workspace should expose a false-color overlay command");
  }
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* false_color_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("falseColorOverlay"));
  if (false_color_overlay == nullptr ||
      !edit_workspace->property("falseColorOverlayEnabled").toBool() ||
      edit_workspace->property("clippingOverlayEnabled").toBool()) {
    return fail("edit workspace should expose a false-color overlay");
  }
  QVariant gamut_overlay_result;
  QVariant zone_overlay_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, gamut_overlay_result),
          Q_ARG(QVariant, QStringLiteral("gamut"))) ||
      !gamut_overlay_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, zone_overlay_result),
          Q_ARG(QVariant, QStringLiteral("zone-map"))) ||
      !zone_overlay_result.toBool()) {
    return fail("edit workspace should expose gamut and zone-map analysis overlay commands");
  }
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* gamut_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("gamutOverlay"));
  auto* zone_map_overlay = edit_workspace->findChild<QObject*>(QStringLiteral("zoneMapOverlay"));
  auto* clipping_overlay_target_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("clippingOverlayTargetLabel"));
  auto* gamut_overlay_target_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("gamutOverlayTargetLabel"));
  auto* pixel_inspector_hud = edit_workspace->findChild<QObject*>(QStringLiteral("pixelInspectorHud"));
  auto* pixel_inspector_before_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("pixelInspectorBeforeLabel"));
  auto* pixel_inspector_after_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("pixelInspectorAfterLabel"));
  auto* pixel_inspector_delta_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("pixelInspectorDeltaLabel"));
  if (gamut_overlay == nullptr || zone_map_overlay == nullptr ||
      edit_workspace->property("gamutOverlayEnabled").toBool() ||
      !edit_workspace->property("zoneMapOverlayEnabled").toBool()) {
    return fail("edit workspace should expose gamut and zone-map analysis overlays");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "analysisOverlaySignature",
          Q_RETURN_ARG(QVariant, overlay_signature)) ||
      overlay_signature.toString() !=
          QStringLiteral("active:zone-map|panel:false|focus:false|peaking:false|clipping:false|false:false|gamut:false|zone:true")) {
    return fail("analysis overlays should be mutually exclusive so diagnostic views cannot stack over the photo");
  }
  QVariant disable_overlay_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, disable_overlay_result),
          Q_ARG(QVariant, QStringLiteral("zone-map"))) ||
      !disable_overlay_result.toBool()) {
    return fail("active diagnostic overlays should be directly dismissible");
  }
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "analysisOverlaySignature",
          Q_RETURN_ARG(QVariant, overlay_signature)) ||
      overlay_signature.toString() !=
          QStringLiteral("active:|panel:false|focus:false|peaking:false|clipping:false|false:false|gamut:false|zone:false") ||
      editor_preview.currentAdjustmentPatchJson() != overlay_recipe_before ||
      editor_preview.displayPreviewSource() != overlay_preview_source_before ||
      editor_preview.processing()) {
    return fail("disabled diagnostic overlays should leave recipe state and preview pixels untouched");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setAnalysisOverlay",
          Q_RETURN_ARG(QVariant, zone_overlay_result),
          Q_ARG(QVariant, QStringLiteral("zone-map"))) ||
      !zone_overlay_result.toBool()) {
    return fail("diagnostic overlay setup should restore zone-map state for toolbar coverage");
  }
  QCoreApplication::processEvents();
  if (clipping_overlay_target_label == nullptr || gamut_overlay_target_label == nullptr ||
      !clipping_overlay_target_label->property("text").toString().contains(QStringLiteral("Preview clipping")) ||
      !gamut_overlay_target_label->property("text").toString().contains(QStringLiteral("Output gamut"))) {
    return fail("analysis overlays should label the clipping and gamut target they refer to");
  }
  if (pixel_inspector_hud == nullptr || pixel_inspector_before_label == nullptr ||
      pixel_inspector_after_label == nullptr || pixel_inspector_delta_label == nullptr) {
    return fail("edit workspace should expose a before/after pixel inspector HUD");
  }
  QVariant pixel_hover_signature;
  QVariant pixel_sampling_signature;
  QVariant pixel_crop_signature;
  QVariant pixel_sampling_begin_result;
  QVariant pixel_sampling_finish_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "pixelInspectorVisibilitySignature",
          Q_RETURN_ARG(QVariant, pixel_hover_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("adjust"))) ||
      pixel_hover_signature.toString() !=
          QStringLiteral("sampling:false|active:true|tool:adjust|visible:false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "pixelInspectorVisibilitySignature",
          Q_RETURN_ARG(QVariant, pixel_sampling_signature),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("adjust"))) ||
      pixel_sampling_signature.toString() !=
          QStringLiteral("sampling:true|active:true|tool:adjust|visible:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "pixelInspectorVisibilitySignature",
          Q_RETURN_ARG(QVariant, pixel_crop_signature),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("crop"))) ||
      pixel_crop_signature.toString() !=
          QStringLiteral("sampling:true|active:true|tool:crop|visible:false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "beginPixelInspectorSampling",
          Q_RETURN_ARG(QVariant, pixel_sampling_begin_result)) ||
      !pixel_sampling_begin_result.toBool() ||
      !edit_workspace->property("pixelInspectorSamplingActive").toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "finishPixelInspectorSampling",
          Q_RETURN_ARG(QVariant, pixel_sampling_finish_result)) ||
      pixel_sampling_finish_result.toBool() ||
      edit_workspace->property("pixelInspectorSamplingActive").toBool()) {
    return fail("pixel inspector should appear only during explicit sampling and clear after release or cancel");
  }
  edit_workspace->setProperty("editTool", QStringLiteral("compare"));
  edit_workspace->setProperty("compareViewMode", QStringLiteral("split"));
  edit_workspace->setProperty("compareSplitPosition", 0.35);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* before_after_split = edit_workspace->findChild<QObject*>(QStringLiteral("beforeAfterSplitView"));
  auto* focused_compare_panel =
      find_named_object(edit_workspace, QStringLiteral("beforeAfterSplitPanel"));
  if (!editor_preview.compareMode() ||
      edit_workspace->property("compareViewMode").toString() != QStringLiteral("split") ||
      edit_workspace->property("compareSplitPosition").toDouble() < 0.34 ||
      before_after_split == nullptr || focused_compare_panel == nullptr ||
      !focused_compare_panel->property("visible").toBool() ||
      edit_workspace->property("compareViewportCount").toInt() != 0) {
    return fail("split compare should dedicate the compare workspace to the before/after view");
  }
  edit_workspace->setProperty("compareViewMode", QStringLiteral("wipe"));
  edit_workspace->setProperty("compareSplitPosition", 0.65);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* before_after_wipe = edit_workspace->findChild<QObject*>(QStringLiteral("beforeAfterWipeView"));
  if (edit_workspace->property("compareViewMode").toString() != QStringLiteral("wipe") ||
      edit_workspace->property("compareSplitPosition").toDouble() < 0.64 ||
      before_after_wipe == nullptr || !focused_compare_panel->property("visible").toBool() ||
      edit_workspace->property("compareViewportCount").toInt() != 0) {
    return fail("wipe compare should keep the focused before/after view without extra columns");
  }
  edit_workspace->setProperty("compareViewMode", QStringLiteral("side-by-side"));
  edit_workspace->setProperty("syncEnabled", true);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  if (edit_workspace->property("compareViewportCount").toInt() != 2 ||
      focused_compare_panel->property("visible").toBool()) {
    return fail("side-by-side compare should expose only synchronized compare image viewports");
  }
  edit_workspace->setProperty("synchronizedZoom", 2.0);
  edit_workspace->setProperty("synchronizedPanX", 18.0);
  edit_workspace->setProperty("synchronizedPanY", -9.0);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  const auto synced_zoom = edit_workspace->property("synchronizedZoom").toDouble();
  const auto synced_pan_x = edit_workspace->property("synchronizedPanX").toDouble();
  const auto synced_pan_y = edit_workspace->property("synchronizedPanY").toDouble();
  if (synced_zoom < 1.99 || synced_zoom > 2.01 || synced_pan_x < 17.9 ||
      synced_pan_x > 18.1 || synced_pan_y > -8.9 || synced_pan_y < -9.1) {
    return fail("compare viewports should synchronize zoom and pan");
  }
  edit_workspace->setProperty("librarySourcesPanelVisible", false);
  edit_workspace->setProperty("libraryInspectorPanelVisible", false);
  edit_workspace->setProperty("editContextPanelVisible", false);
  edit_workspace->setProperty("editInspectorPanelVisible", false);
  edit_workspace->setProperty("exportPresetsPanelVisible", false);
  edit_workspace->setProperty("exportSettingsPanelVisible", false);
  QVariant panel_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "customizablePanelSignature",
          Q_RETURN_ARG(QVariant, panel_signature)) ||
      panel_signature.toString() != QStringLiteral("false|false|false|false|false|false")) {
    return fail("workspace panels should expose customizable visibility state");
  }
  if (!QMetaObject::invokeMethod(edit_workspace, "resetCustomizablePanels")) {
    return fail("workspace panels should expose a reset action");
  }
  QCoreApplication::processEvents();
  QVariant reset_panel_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "customizablePanelSignature",
          Q_RETURN_ARG(QVariant, reset_panel_signature)) ||
      reset_panel_signature.toString() != QStringLiteral("true|true|true|true|true|true")) {
    return fail("workspace panel customization reset should restore default panels");
  }
  edit_workspace->setProperty("librarySourcesPanelVisible", false);
  edit_workspace->setProperty("editContextPanelVisible", false);
  edit_workspace->setProperty("editInspectorPanelVisible", false);
  QVariant save_layout_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "saveWorkspaceLayout",
          Q_RETURN_ARG(QVariant, save_layout_result),
          Q_ARG(QVariant, QStringLiteral("Review Focus"))) ||
      !save_layout_result.toBool() || edit_workspace->property("savedWorkspaceLayoutCount").toInt() < 3) {
    return fail("workspace layout presets should save named panel arrangements");
  }
  if (!QMetaObject::invokeMethod(edit_workspace, "resetCustomizablePanels")) {
    return fail("workspace layout presets should allow reset before restore");
  }
  QVariant apply_layout_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "applyWorkspaceLayout",
          Q_RETURN_ARG(QVariant, apply_layout_result),
          Q_ARG(QVariant, QStringLiteral("Review Focus"))) ||
      !apply_layout_result.toBool()) {
    return fail("workspace layout presets should restore named panel arrangements");
  }
  QVariant restored_layout_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "customizablePanelSignature",
          Q_RETURN_ARG(QVariant, restored_layout_signature)) ||
      restored_layout_signature.toString() != QStringLiteral("false|true|false|false|true|true")) {
    return fail("workspace layout presets should restore saved panel visibility");
  }
  QVariant compact_mode_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setInterfaceMode",
          Q_RETURN_ARG(QVariant, compact_mode_result),
          Q_ARG(QVariant, QStringLiteral("compact"))) ||
      !compact_mode_result.toBool() ||
      edit_workspace->property("interfaceMode").toString() != QStringLiteral("compact") ||
      edit_workspace->property("editInspectorPanelWidth").toInt() >= 420) {
    return fail("workspace should expose compact panel density mode");
  }
  root->setProperty("width", 1180);
  root->setProperty("height", 820);
  edit_workspace->setProperty("editContextPanelVisible", true);
  edit_workspace->setProperty("editSidebarCollapsed", false);
  edit_workspace->setProperty("editInspectorPanelVisible", true);
  edit_workspace->setProperty("largeScopesMode", false);
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  edit_workspace->setProperty("diagnosticsPanelOpen", true);
  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  process_layout_events();
  const QStringList compact_named_buttons{
      QStringLiteral("fitToViewButton"),
      QStringLiteral("fillViewButton"),
      QStringLiteral("zoomStepButton"),
      QStringLiteral("zoomOutButton"),
      QStringLiteral("zoomInButton"),
      QStringLiteral("diagnosticsOverlayButton"),
      QStringLiteral("referenceImageButton"),
      QStringLiteral("inspectorPanelToggleButton"),
      QStringLiteral("fullScreenEditButton"),
      QStringLiteral("diagnosticsPanelCloseButton"),
      QStringLiteral("loupeOverlayButton"),
      QStringLiteral("focusOverlayButton"),
      QStringLiteral("peakingOverlayButton"),
      QStringLiteral("clippingOverlayButton"),
      QStringLiteral("falseColorOverlayButton"),
      QStringLiteral("gamutOverlayButton"),
      QStringLiteral("zoneMapOverlayButton"),
      QStringLiteral("adjustInspectorTabButton"),
      QStringLiteral("layersInspectorTabButton"),
      QStringLiteral("scopesInspectorTabButton"),
      QStringLiteral("detailInspectorTabButton"),
      QStringLiteral("infoInspectorTabButton"),
      QStringLiteral("editInspectorResetButton"),
      QStringLiteral("editInspectorSavePresetButton"),
  };
  const QStringList compact_named_labels{
      QStringLiteral("editAdjustmentFilterStatusLabel"),
      QStringLiteral("activeMaskToolbarStatusLabel"),
      QStringLiteral("maskStackSetLabel"),
      QStringLiteral("maskStackSetSummaryLabel"),
      QStringLiteral("cropToolHeaderLabel"),
      QStringLiteral("cropToolAspectLabel"),
      QStringLiteral("brushHudStatusLabel"),
      QStringLiteral("retouchHudStatusLabel"),
      QStringLiteral("largeScopeProofingStatusLabel"),
      QStringLiteral("pixelInspectorCoordinateLabel"),
      QStringLiteral("pixelInspectorBeforeLabel"),
      QStringLiteral("pixelInspectorAfterLabel"),
      QStringLiteral("pixelInspectorDeltaLabel"),
  };
  QString failing_compact_button;
  if (!visible_named_buttons_fit_content(edit_workspace, compact_named_buttons) ||
      !visible_mini_buttons_fit_content(edit_workspace, &failing_compact_button) ||
      !visible_named_labels_fit_content(edit_workspace, compact_named_labels)) {
    std::cerr << "Compact fit failure: "
              << failing_compact_button.toStdString() << '\n';
    return fail("compact mode should keep visible labels and button content inside controls");
  }
  edit_workspace->setProperty("diagnosticsPanelOpen", false);
  QVariant pro_mode_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setInterfaceMode",
          Q_RETURN_ARG(QVariant, pro_mode_result),
          Q_ARG(QVariant, QStringLiteral("pro"))) ||
      !pro_mode_result.toBool() ||
      edit_workspace->property("interfaceMode").toString() != QStringLiteral("pro") ||
      edit_workspace->property("editInspectorPanelWidth").toInt() < 420) {
    return fail("workspace should expose pro panel density mode");
  }
  edit_workspace->setProperty("editContextPanelVisible", true);
  edit_workspace->setProperty("editSidebarCollapsed", false);
  edit_workspace->setProperty("largeScopesMode", false);
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  edit_workspace->setProperty("compareViewMode", QStringLiteral("split"));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "activateEditTool",
          Q_ARG(QVariant, QStringLiteral("compare")))) {
    return fail("edit workspace should expose compare activation");
  }
  QCoreApplication::processEvents();
  auto* edit_context_panel = edit_workspace->findChild<QObject*>(QStringLiteral("editContextPanel"));
  auto* before_after_split_panel = find_named_object(edit_workspace, QStringLiteral("beforeAfterSplitPanel"));
  if (edit_context_panel == nullptr || before_after_split_panel == nullptr ||
      edit_workspace->property("editContextPanelActive").toBool() ||
      edit_context_panel->property("visible").toBool() ||
      !before_after_split_panel->property("visible").toBool()) {
    return fail("compare view should hide the context rail and dedicate the canvas to before/after panels");
  }
  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  QCoreApplication::processEvents();
  if (!edit_workspace->property("editContextPanelActive").toBool() ||
      !edit_context_panel->property("visible").toBool() ||
      edit_context_panel->property("title").toString() != QStringLiteral("Library")) {
    return fail("adjust view should restore the left library rail without asset context chrome");
  }
  auto* edit_image_viewport = find_named_object(edit_workspace, QStringLiteral("editImageViewport"));
  auto* edit_zoom_toolbar = find_named_object(edit_workspace, QStringLiteral("editZoomToolbar"));
  auto* edit_action_toolbar = find_named_object(edit_workspace, QStringLiteral("editActionToolbar"));
  auto* undo_edit_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("undoEditShortcut"));
  auto* redo_edit_shortcut = edit_workspace->findChild<QObject*>(QStringLiteral("redoEditShortcut"));
  auto* diagnostics_overlay_button =
      find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayButton"));
  auto* diagnostics_overlay_panel =
      find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayPanel"));
  auto* loupe_overlay_button = find_named_object(edit_workspace, QStringLiteral("loupeOverlayButton"));
  auto* gamut_overlay_button = find_named_object(edit_workspace, QStringLiteral("gamutOverlayButton"));
  auto* preview_update_badge =
      find_named_object(root, QStringLiteral("floatingActivityIndicator"));
  auto* preview_retry_button =
      find_named_object(root, QStringLiteral("activityCancelButton"));
  auto* mask_activity_status_chip =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportMaskActivityStatusChip"));
  auto* mask_activity_status_dot =
      find_named_object(edit_workspace, QStringLiteral("editImageViewportMaskActivityStatusDot"));
  if (edit_image_viewport == nullptr || edit_zoom_toolbar == nullptr || edit_action_toolbar == nullptr ||
      undo_edit_shortcut == nullptr || redo_edit_shortcut == nullptr ||
      diagnostics_overlay_button == nullptr || diagnostics_overlay_panel == nullptr ||
      loupe_overlay_button == nullptr || gamut_overlay_button == nullptr ||
      preview_update_badge == nullptr || preview_retry_button == nullptr ||
      mask_activity_status_chip == nullptr || mask_activity_status_dot == nullptr) {
    return fail("edit canvas should expose responsive viewport toolbar controls");
  }
  if (edit_image_viewport->property("mediaSurfaceContract").toString() !=
          QStringLiteral("media-surface:v1|canvas-stage:dominant|image-corners:consistent|empty-loading-failure:same-bounds|chrome:quiet|badges:corner-stacked|toolbar-baseline:single") ||
      edit_image_viewport->property("previewChromeContract").toString() !=
      QStringLiteral("preview-chrome:v2|status:global-under-navigation|diagnostics:overlay-menu|top-controls:baseline|failure:global-retry|content-dominant:true")) {
    return fail("edit image viewport should expose quiet media-surface and preview-chrome contracts");
  }
  QVariant preview_update_signature;
  QVariant preview_failure_signature;
  QVariant preview_stage_failure_signature;
  QVariant preview_structured_failure_signature;
  QVariant preview_unavailable_signature;
  QVariant preview_update_chrome_signature;
  QVariant preview_failure_chrome_signature;
  QVariant preview_layer_failure_row_signature;
  QVariant preview_inactive_layer_failure_row_signature;
  QVariant preview_mask_failure_row_signature;
  QVariant mask_activity_analyzing_signature;
  QVariant mask_activity_rasterizing_signature;
  QVariant mask_activity_inpainting_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "previewRenderPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_update_signature),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("Rendering preview")),
          Q_ARG(QVariant, QString()),
          Q_ARG(QVariant, QString())) ||
      preview_update_signature.toString() !=
          QStringLiteral("updating|blocking:false|stale:true|retry:false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewRenderPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_failure_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("Preview render failed")),
          Q_ARG(QVariant, QString()),
          Q_ARG(QVariant, QString())) ||
      preview_failure_signature.toString() !=
          QStringLiteral("failed|blocking:false|stale:false|retry:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewRenderPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_stage_failure_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable.")),
          Q_ARG(QVariant, QStringLiteral("source_decode")),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable."))) ||
      preview_stage_failure_signature.toString() !=
          QStringLiteral("failed|blocking:false|stale:false|retry:true|stage:source_decode|detail:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewRenderPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_structured_failure_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("Linked image is missing or unreadable")),
          Q_ARG(QVariant, QStringLiteral("layer_source")),
          Q_ARG(QVariant, QStringLiteral("Restore the source file or disable the layer."))) ||
      preview_structured_failure_signature.toString() !=
          QStringLiteral("failed|blocking:false|stale:false|retry:true|stage:layer_source|detail:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewRenderPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_unavailable_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable.")),
          Q_ARG(QVariant, QString()),
          Q_ARG(QVariant, QString())) ||
      preview_unavailable_signature.toString() !=
          QStringLiteral("failed|blocking:false|stale:false|retry:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewRenderChromeSignature",
          Q_RETURN_ARG(QVariant, preview_update_chrome_signature),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("Rendering preview")),
          Q_ARG(QVariant, QString()),
          Q_ARG(QVariant, QString())) ||
      preview_update_chrome_signature.toString() !=
          QStringLiteral(
              "updating|blocking:false|stale:true|retry:false|chrome:global-status-bar|center:false|retryButton:false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewRenderChromeSignature",
          Q_RETURN_ARG(QVariant, preview_failure_chrome_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable.")),
          Q_ARG(QVariant, QString()),
          Q_ARG(QVariant, QString())) ||
      preview_failure_chrome_signature.toString() !=
          QStringLiteral(
              "failed|blocking:false|stale:false|retry:true|chrome:global-status-bar|center:false|retryButton:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewFailureRowPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_layer_failure_row_signature),
          Q_ARG(QVariant, QStringLiteral("layer")),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable.")),
          Q_ARG(QVariant, QStringLiteral("source_decode")),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable."))) ||
      preview_layer_failure_row_signature.toString() !=
          QStringLiteral("row:layer|failed:true|stage:source_decode|label:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewFailureRowPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_inactive_layer_failure_row_signature),
          Q_ARG(QVariant, QStringLiteral("layer")),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable.")),
          Q_ARG(QVariant, QStringLiteral("source_decode")),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable."))) ||
      preview_inactive_layer_failure_row_signature.toString() !=
          QStringLiteral("row:layer|failed:false|stage:|label:false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "previewFailureRowPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_mask_failure_row_signature),
          Q_ARG(QVariant, QStringLiteral("mask")),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable.")),
          Q_ARG(QVariant, QStringLiteral("source_decode")),
          Q_ARG(QVariant, QStringLiteral("Source file unavailable."))) ||
      preview_mask_failure_row_signature.toString() !=
          QStringLiteral("row:mask|failed:true|stage:source_decode|label:true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskActivityPresentationSignature",
          Q_RETURN_ARG(QVariant, mask_activity_analyzing_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("Smart selection analyzing target")),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("masks"))) ||
      mask_activity_analyzing_signature.toString() !=
          QStringLiteral("analyzing|visible:true|blocking:false|chip:persistent") ||
      mask_activity_status_chip->property("activityAnimationContract").toString() !=
          QStringLiteral("nonblocking|persistent|animated-dot:v1") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskActivityPresentationSignature",
          Q_RETURN_ARG(QVariant, mask_activity_rasterizing_signature),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, QStringLiteral("Rendering mask rasterization")),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("brush"))) ||
      mask_activity_rasterizing_signature.toString() !=
          QStringLiteral("rasterizing|visible:true|blocking:false|chip:persistent") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "maskActivityPresentationSignature",
          Q_RETURN_ARG(QVariant, mask_activity_inpainting_signature),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("LaMa Model Inpaint patch baking")),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, QStringLiteral("retouch"))) ||
      mask_activity_inpainting_signature.toString() !=
          QStringLiteral("inpainting|visible:true|blocking:false|chip:persistent")) {
    return fail("preview render status should distinguish stale updating from retryable failure without blocking the canvas");
  }
  const QStringList zoom_toolbar_items{
      QStringLiteral("fitToViewButton"),
      QStringLiteral("fillViewButton"),
      QStringLiteral("zoomStepButton"),
      QStringLiteral("zoomOutButton"),
      QStringLiteral("zoomInButton"),
      QStringLiteral("diagnosticsOverlayButton"),
  };
  const QStringList action_toolbar_items{
      QStringLiteral("referenceImageButton"),
      QStringLiteral("clearReferenceImageButton"),
      QStringLiteral("inspectorPanelToggleButton"),
      QStringLiteral("fullScreenEditButton"),
  };
  const QStringList diagnostics_panel_items{
      QStringLiteral("diagnosticsPanelCloseButton"),
      QStringLiteral("loupeOverlayButton"),
      QStringLiteral("focusOverlayButton"),
      QStringLiteral("peakingOverlayButton"),
      QStringLiteral("clippingOverlayButton"),
      QStringLiteral("falseColorOverlayButton"),
      QStringLiteral("gamutOverlayButton"),
      QStringLiteral("zoneMapOverlayButton"),
  };
  const QList<int> supported_toolbar_widths{1024, 1180, 1320, 1600};
  for (const auto supported_width : supported_toolbar_widths) {
    root->setProperty("width", supported_width);
    root->setProperty("height", 820);
    QMetaObject::invokeMethod(
        edit_workspace,
        "applyResponsivePanelBehavior",
        Q_ARG(QVariant, supported_width));
    QMetaObject::invokeMethod(
        edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
    edit_workspace->setProperty("diagnosticsPanelOpen", false);
    process_layout_events();

    edit_image_viewport = find_named_object(edit_workspace, QStringLiteral("editImageViewport"));
    edit_zoom_toolbar = find_named_object(edit_workspace, QStringLiteral("editZoomToolbar"));
    edit_action_toolbar = find_named_object(edit_workspace, QStringLiteral("editActionToolbar"));
    diagnostics_overlay_button =
        find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayButton"));
    diagnostics_overlay_panel =
        find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayPanel"));
    loupe_overlay_button = find_named_object(edit_workspace, QStringLiteral("loupeOverlayButton"));
    gamut_overlay_button = find_named_object(edit_workspace, QStringLiteral("gamutOverlayButton"));
    auto* viewport_item = qobject_cast<QQuickItem*>(edit_image_viewport);
    auto* zoom_toolbar_item = qobject_cast<QQuickItem*>(edit_zoom_toolbar);
    auto* action_toolbar_item = qobject_cast<QQuickItem*>(edit_action_toolbar);
    if (!item_visible(viewport_item) || !item_visible(zoom_toolbar_item) ||
        !item_visible(action_toolbar_item)) {
      return fail("edit canvas should keep generic toolbar items visible at supported widths");
    }

    const auto viewport_width = viewport_item->width();
    const auto toolbar_compact_width =
        edit_image_viewport->property("toolbarCompactWidth").toDouble();
    const auto toolbar_compact = viewport_width < toolbar_compact_width;
    const auto zoom_right = edit_zoom_toolbar->property("x").toDouble() +
                            edit_zoom_toolbar->property("width").toDouble();
    const auto action_left = edit_action_toolbar->property("x").toDouble();
    const auto zoom_bottom = edit_zoom_toolbar->property("y").toDouble() +
                             edit_zoom_toolbar->property("height").toDouble();
    const auto action_top = edit_action_toolbar->property("y").toDouble();
    const auto toolbar_stacked = edit_image_viewport->property("toolbarStacked").toBool();
    const auto viewport_rect = scene_rect(viewport_item);
    const auto zoom_toolbar_contract = edit_zoom_toolbar->property("toolGroupContract").toString();
    const auto action_toolbar_contract = edit_action_toolbar->property("toolGroupContract").toString();
    if (edit_image_viewport->property("toolbarCompact").toBool() != toolbar_compact ||
        !zoom_toolbar_contract.contains(QStringLiteral("canvas-toolbar:v1|group:view-controls")) ||
        !zoom_toolbar_contract.contains(QStringLiteral("stable-height:true")) ||
        !zoom_toolbar_contract.contains(QStringLiteral("toolbar-buttons:mini-button")) ||
        !zoom_toolbar_contract.contains(QStringLiteral("baseline-aligned:true")) ||
        !zoom_toolbar_contract.contains(QStringLiteral("zoom-fit-group:true")) ||
        !zoom_toolbar_contract.contains(QStringLiteral("diagnostics-menu:true")) ||
        !zoom_toolbar_contract.contains(QStringLiteral("subdued-chrome:true")) ||
        !zoom_toolbar_contract.contains(QStringLiteral("media-subordinate:true")) ||
        !zoom_toolbar_contract.contains(QStringLiteral("view-separated:true")) ||
        !zoom_toolbar_contract.contains(QStringLiteral("no-overlap:true")) ||
        !action_toolbar_contract.contains(QStringLiteral("canvas-toolbar:v1|group:canvas-actions")) ||
        !action_toolbar_contract.contains(QStringLiteral("stable-height:true")) ||
        !action_toolbar_contract.contains(QStringLiteral("toolbar-buttons:mini-button")) ||
        !action_toolbar_contract.contains(QStringLiteral("baseline-aligned:true")) ||
        !action_toolbar_contract.contains(QStringLiteral("panel-toggles:true")) ||
        !action_toolbar_contract.contains(QStringLiteral("reference-agent-actions:true")) ||
        !action_toolbar_contract.contains(QStringLiteral("subdued-chrome:true")) ||
        !action_toolbar_contract.contains(QStringLiteral("media-subordinate:true")) ||
        !action_toolbar_contract.contains(QStringLiteral("view-separated:true")) ||
        !action_toolbar_contract.contains(QStringLiteral("no-overlap:true")) ||
        find_named_object(edit_workspace, QStringLiteral("undoEditButton")) != nullptr ||
        find_named_object(edit_workspace, QStringLiteral("redoEditButton")) != nullptr ||
        diagnostics_overlay_button->property("labelVisible").toBool() ||
        diagnostics_overlay_button->property("text").toString() != QStringLiteral("Zone") ||
        diagnostics_overlay_panel->property("visible").toBool() ||
        loupe_overlay_button->property("visible").toBool() ||
        gamut_overlay_button->property("visible").toBool() ||
        gamut_overlay_button->property("tooltip").toString().isEmpty() ||
        !contains_with_tolerance(viewport_rect, scene_rect(zoom_toolbar_item)) ||
        !contains_with_tolerance(viewport_rect, scene_rect(action_toolbar_item)) ||
        !visible_named_items_fit_parent(
            edit_workspace,
            QStringLiteral("editZoomToolbar"),
            zoom_toolbar_items) ||
        !visible_named_items_fit_parent(
            edit_workspace,
            QStringLiteral("editActionToolbar"),
            action_toolbar_items) ||
        !visible_named_items_do_not_overlap(edit_workspace, zoom_toolbar_items) ||
        !visible_named_items_do_not_overlap(edit_workspace, action_toolbar_items) ||
        (!toolbar_stacked && zoom_right + 8.0 > action_left) ||
        (toolbar_stacked && zoom_bottom + 6.0 > action_top)) {
      return fail("edit canvas toolbars should compact predictably and avoid overlap");
    }

    edit_workspace->setProperty("diagnosticsPanelOpen", true);
    process_layout_events();
    diagnostics_overlay_panel =
        find_named_object(edit_workspace, QStringLiteral("diagnosticsOverlayPanel"));
    auto* diagnostics_overlay_panel_item = qobject_cast<QQuickItem*>(diagnostics_overlay_panel);
    const auto diagnostics_panel_visible = item_visible(diagnostics_overlay_panel_item);
    const auto diagnostics_panel_inside_viewport =
        diagnostics_panel_visible &&
        contains_with_tolerance(viewport_rect, scene_rect(diagnostics_overlay_panel_item));
    const auto diagnostics_items_fit = visible_named_items_fit_parent(
        edit_workspace,
        QStringLiteral("diagnosticsOverlayPanel"),
        diagnostics_panel_items);
    const auto diagnostics_items_clear =
        visible_named_items_do_not_overlap(edit_workspace, diagnostics_panel_items);
    if (!diagnostics_panel_visible || !diagnostics_panel_inside_viewport ||
        !diagnostics_items_fit || !diagnostics_items_clear) {
      std::cerr << "Diagnostics toolbar width=" << supported_width
                << " panelVisible=" << diagnostics_panel_visible
                << " panelInside=" << diagnostics_panel_inside_viewport
                << " itemsFit=" << diagnostics_items_fit
                << " itemsClear=" << diagnostics_items_clear << '\n';
      if (diagnostics_panel_visible) {
        const auto rect = scene_rect(diagnostics_overlay_panel_item);
        std::cerr << "diagnosticsOverlayPanel rect=" << rect.x() << "," << rect.y()
                  << " " << rect.width() << "x" << rect.height() << '\n';
      }
      log_visible_named_item_geometry(edit_workspace, diagnostics_panel_items);
      return fail("diagnostic overlay toolbar should fit and avoid button overlap at supported widths");
    }
    edit_workspace->setProperty("diagnosticsPanelOpen", false);
  }
  root->setProperty("width", 1600);
  root->setProperty("height", 820);
  QMetaObject::invokeMethod(
      edit_workspace,
      "applyResponsivePanelBehavior",
      Q_ARG(QVariant, 1600));
  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  process_layout_events();
  edit_workspace->setProperty("diagnosticsPanelOpen", true);
  QCoreApplication::processEvents();
  if (!diagnostics_overlay_panel->property("visible").toBool() ||
      !loupe_overlay_button->property("visible").toBool() ||
      !gamut_overlay_button->property("visible").toBool()) {
    return fail("diagnostic overlay choices should stay collapsed behind one toolbar control until requested");
  }
  edit_workspace->setProperty("diagnosticsPanelOpen", false);
  QCoreApplication::processEvents();
  QVariant full_screen_result;
  auto* edit_filmstrip = find_named_object(edit_workspace, QStringLiteral("editFilmstrip"));
  auto* edit_inspector_panel_for_focus =
      find_named_object(edit_workspace, QStringLiteral("editInspectorPanel"));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setFullScreenEditing",
          Q_RETURN_ARG(QVariant, full_screen_result),
          Q_ARG(QVariant, true)) ||
      !full_screen_result.toBool() ||
      !edit_workspace->property("fullScreenEditingEnabled").toBool() ||
      edit_workspace->property("editingChromeVisible").toBool()) {
    return fail("edit workspace should expose a full-screen editing mode");
  }
  QVariant exit_full_screen_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setFullScreenEditing",
          Q_RETURN_ARG(QVariant, exit_full_screen_result),
          Q_ARG(QVariant, false)) ||
      exit_full_screen_result.toBool() ||
      edit_workspace->property("fullScreenEditingEnabled").toBool() ||
      !edit_workspace->property("editingChromeVisible").toBool()) {
    return fail("edit workspace should restore chrome after full-screen editing");
  }
  QMetaObject::invokeMethod(edit_workspace, "resetCustomizablePanels");
  QCoreApplication::processEvents();
  QVariant focus_edit_result;
  const auto recipe_before_focus_edit = editor_preview.currentAdjustmentPatchJson();
  if (edit_filmstrip == nullptr || edit_context_panel == nullptr ||
      edit_inspector_panel_for_focus == nullptr ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setFocusEditLayout",
          Q_RETURN_ARG(QVariant, focus_edit_result),
          Q_ARG(QVariant, true)) ||
      !focus_edit_result.toBool() ||
      !edit_workspace->property("focusEditLayoutEnabled").toBool() ||
      edit_workspace->property("fullScreenEditingEnabled").toBool() ||
      edit_workspace->property("editingChromeVisible").toBool() ||
      edit_context_panel->property("visible").toBool() ||
      edit_inspector_panel_for_focus->property("visible").toBool() ||
      edit_filmstrip->property("visible").toBool() ||
      editor_preview.currentAdjustmentPatchJson() != recipe_before_focus_edit) {
    return fail("Focus Edit layout should hide context, inspector, and filmstrip chrome without using fullscreen state");
  }
  QVariant exit_focus_edit_result;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setFocusEditLayout",
          Q_RETURN_ARG(QVariant, exit_focus_edit_result),
          Q_ARG(QVariant, false)) ||
      exit_focus_edit_result.toBool() ||
      edit_workspace->property("focusEditLayoutEnabled").toBool() ||
      !edit_workspace->property("editingChromeVisible").toBool() ||
      !edit_context_panel->property("visible").toBool() ||
      !edit_inspector_panel_for_focus->property("visible").toBool() ||
      !edit_filmstrip->property("visible").toBool() ||
      editor_preview.currentAdjustmentPatchJson() != recipe_before_focus_edit) {
    return fail("Focus Edit layout should restore normal edit chrome and the left library rail when exited");
  }
  QVariant narrow_layout_result;
  QVariant narrow_layout_signature;
  QVariant narrow_density_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "applyResponsivePanelBehavior",
          Q_RETURN_ARG(QVariant, narrow_layout_result),
          Q_ARG(QVariant, 980)) ||
      !narrow_layout_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "responsivePanelSignature",
          Q_RETURN_ARG(QVariant, narrow_layout_signature)) ||
      narrow_layout_signature.toString() !=
          QStringLiteral("compact|true|false|false|true|false|false|false|false") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "responsivePanelDensitySignature",
          Q_RETURN_ARG(QVariant, narrow_density_signature),
          Q_ARG(QVariant, 980)) ||
      narrow_density_signature.toString() !=
          QStringLiteral(
              "w:980|mode:small-collapsed|interface:compact|small:true|active:true|library:218/292|edit:210/356|export:258/294|visible:false,false,true,false,false,false,false")) {
    return fail("workspace should collapse panels for small displays");
  }
  QVariant breakpoint_density_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "responsivePanelDensitySignature",
          Q_RETURN_ARG(QVariant, breakpoint_density_signature),
          Q_ARG(QVariant, 1119)) ||
      breakpoint_density_signature.toString() !=
          QStringLiteral(
              "w:1119|mode:small-collapsed|interface:compact|small:true|active:true|library:218/292|edit:210/356|export:258/294|visible:false,false,true,false,false,false,false")) {
    return fail("workspace should keep the small-display signature deterministic below the breakpoint");
  }
  QVariant wide_layout_result;
  QVariant wide_layout_signature;
  QVariant wide_density_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "applyResponsivePanelBehavior",
          Q_RETURN_ARG(QVariant, wide_layout_result),
          Q_ARG(QVariant, 1440)) ||
      !wide_layout_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "responsivePanelSignature",
          Q_RETURN_ARG(QVariant, wide_layout_signature)) ||
      wide_layout_signature.toString() !=
          QStringLiteral("pro|false|true|true|false|true|true|true|true") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "responsivePanelDensitySignature",
          Q_RETURN_ARG(QVariant, wide_density_signature),
          Q_ARG(QVariant, 1440)) ||
      wide_density_signature.toString() !=
          QStringLiteral(
              "w:1440|mode:pro|interface:pro|small:false|active:false|library:262/340|edit:246/420|export:306/342|visible:true,true,false,true,true,true,true")) {
    return fail("workspace should restore panels after leaving small display mode");
  }
  QVariant compact_width_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setInterfaceMode",
          Q_RETURN_ARG(QVariant, compact_mode_result),
          Q_ARG(QVariant, QStringLiteral("compact"))) ||
      !compact_mode_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "responsivePanelDensitySignature",
          Q_RETURN_ARG(QVariant, compact_width_signature),
          Q_ARG(QVariant, 1180)) ||
      compact_width_signature.toString() !=
          QStringLiteral(
              "w:1180|mode:compact|interface:compact|small:false|active:false|library:218/292|edit:210/356|export:258/294|visible:true,true,false,true,true,true,true")) {
    return fail("workspace compact density should have deterministic panel widths above the small-display breakpoint");
  }
  QMetaObject::invokeMethod(
      edit_workspace,
      "setInterfaceMode",
      Q_RETURN_ARG(QVariant, pro_mode_result),
      Q_ARG(QVariant, QStringLiteral("pro")));
  catalog_assets.replaceAssetsForTesting({});
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  QVariant edit_empty_title;
  QVariant library_empty_title_from_edit_workspace;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "editEmptyStateTitle",
          Q_RETURN_ARG(QVariant, edit_empty_title)) ||
      edit_empty_title.toString() != QStringLiteral("No Photo Selected") ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "libraryEmptyStateTitle",
          Q_RETURN_ARG(QVariant, library_empty_title_from_edit_workspace)) ||
      library_empty_title_from_edit_workspace.toString().trimmed().isEmpty()) {
    return fail("workspace should expose clear empty and error state messages");
  }
  QVariant library_capability_count;
  QVariant library_capability_signature;
  const QStringList expected_library_capabilities = {
      QStringLiteral("albums"),
      QStringLiteral("collections"),
      QStringLiteral("smart-albums"),
      QStringLiteral("stacks"),
      QStringLiteral("versions"),
      QStringLiteral("virtual-copies"),
      QStringLiteral("duplicate-detection"),
      QStringLiteral("near-duplicate-clustering"),
      QStringLiteral("offline-media"),
      QStringLiteral("relink"),
      QStringLiteral("sidecars"),
      QStringLiteral("card-ingest"),
      QStringLiteral("checksum"),
      QStringLiteral("backup-copy"),
      QStringLiteral("rename-templates"),
      QStringLiteral("folder-templates"),
      QStringLiteral("metadata-preset-import"),
      QStringLiteral("develop-preset-import"),
      QStringLiteral("duplicate-avoidance-import"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "libraryCapabilityCount",
          Q_RETURN_ARG(QVariant, library_capability_count)) ||
      library_capability_count.toInt() < expected_library_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "libraryCapabilitySignature",
          Q_RETURN_ARG(QVariant, library_capability_signature))) {
    return fail("workspace should expose the Library and Catalog capability registry");
  }
  for (const auto& capability : expected_library_capabilities) {
    if (!library_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Library and Catalog workflow capability");
    }
  }
  QVariant metadata_capability_count;
  QVariant metadata_capability_signature;
  const QStringList expected_metadata_capabilities = {
      QStringLiteral("exif-iptc-xmp"),
      QStringLiteral("captions"),
      QStringLiteral("keywords"),
      QStringLiteral("people-tagging"),
      QStringLiteral("gps-map"),
      QStringLiteral("copyright-creator"),
      QStringLiteral("metadata-templates"),
      QStringLiteral("metadata-sync"),
      QStringLiteral("metadata-conflicts"),
      QStringLiteral("sidecar-round-trip"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "metadataCapabilityCount",
          Q_RETURN_ARG(QVariant, metadata_capability_count)) ||
      metadata_capability_count.toInt() < expected_metadata_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "metadataCapabilitySignature",
          Q_RETURN_ARG(QVariant, metadata_capability_signature))) {
    return fail("workspace should expose the Metadata capability registry");
  }
  for (const auto& capability : expected_metadata_capabilities) {
    if (!metadata_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Metadata workflow capability");
    }
  }
  QVariant culling_search_capability_count;
  QVariant culling_search_capability_signature;
  const QStringList expected_culling_search_capabilities = {
      QStringLiteral("embedded-jpeg-previews"),
      QStringLiteral("reject-flag-rating-label-hotkeys"),
      QStringLiteral("auto-advance"),
      QStringLiteral("burst-grouping"),
      QStringLiteral("pick-groups"),
      QStringLiteral("fast-compare-candidates"),
      QStringLiteral("focus-check-zoom"),
      QStringLiteral("rich-filtering"),
      QStringLiteral("saved-searches"),
      QStringLiteral("smart-filters"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "cullingSearchCapabilityCount",
          Q_RETURN_ARG(QVariant, culling_search_capability_count)) ||
      culling_search_capability_count.toInt() < expected_culling_search_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "cullingSearchCapabilitySignature",
          Q_RETURN_ARG(QVariant, culling_search_capability_signature))) {
    return fail("workspace should expose the Culling and Search capability registry");
  }
  for (const auto& capability : expected_culling_search_capabilities) {
    if (!culling_search_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Culling and Search workflow capability");
    }
  }
  QVariant batch_editing_capability_count;
  QVariant batch_editing_capability_signature;
  const QStringList expected_batch_editing_capabilities = {
      QStringLiteral("selective-sync"),
      QStringLiteral("copy-paste-group-selection"),
      QStringLiteral("paste-conflict-preview"),
      QStringLiteral("match-exposure"),
      QStringLiteral("match-white-balance"),
      QStringLiteral("batch-crop"),
      QStringLiteral("batch-mask-ai"),
      QStringLiteral("batch-retouch-safe"),
      QStringLiteral("batch-metadata-templates"),
      QStringLiteral("batch-export-validation"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "batchEditingCapabilityCount",
          Q_RETURN_ARG(QVariant, batch_editing_capability_count)) ||
      batch_editing_capability_count.toInt() < expected_batch_editing_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "batchEditingCapabilitySignature",
          Q_RETURN_ARG(QVariant, batch_editing_capability_signature))) {
    return fail("workspace should expose the Batch Editing capability registry");
  }
  for (const auto& capability : expected_batch_editing_capabilities) {
    if (!batch_editing_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Batch Editing workflow capability");
    }
  }
  QVariant export_delivery_capability_count;
  QVariant export_delivery_capability_signature;
  const QStringList expected_export_delivery_capabilities = {
      QStringLiteral("custom-export-presets"),
      QStringLiteral("export-formats"),
      QStringLiteral("bit-depth-options"),
      QStringLiteral("color-profile-selection"),
      QStringLiteral("profile-embedding"),
      QStringLiteral("resize-modes"),
      QStringLiteral("output-sharpening"),
      QStringLiteral("watermarking"),
      QStringLiteral("metadata-export-controls"),
      QStringLiteral("filename-templates"),
      QStringLiteral("subfolder-templates"),
      QStringLiteral("collision-handling"),
      QStringLiteral("export-validation-warnings"),
      QStringLiteral("export-queue-controls"),
      QStringLiteral("export-manifests"),
      QStringLiteral("print-workflow"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "exportDeliveryCapabilityCount",
          Q_RETURN_ARG(QVariant, export_delivery_capability_count)) ||
      export_delivery_capability_count.toInt() < expected_export_delivery_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "exportDeliveryCapabilitySignature",
          Q_RETURN_ARG(QVariant, export_delivery_capability_signature))) {
    return fail("workspace should expose the Export and Delivery capability registry");
  }
  for (const auto& capability : expected_export_delivery_capabilities) {
    if (!export_delivery_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Export and Delivery workflow capability");
    }
  }
  QVariant performance_capability_count;
  QVariant performance_capability_signature;
  const QStringList expected_performance_capabilities = {
      QStringLiteral("tiled-rendering"),
      QStringLiteral("mip-pyramid-previews"),
      QStringLiteral("background-cache-warming"),
      QStringLiteral("viewport-only-rendering"),
      QStringLiteral("dirty-tile-rendering"),
      QStringLiteral("progressive-refinement"),
      QStringLiteral("gpu-pipeline-coverage"),
      QStringLiteral("cross-platform-gpu-abstraction"),
      QStringLiteral("windows-gpu-parity"),
      QStringLiteral("cpu-gpu-memory-budgets"),
      QStringLiteral("disk-cache-eviction"),
      QStringLiteral("gpu-texture-pool"),
      QStringLiteral("low-memory-recovery"),
      QStringLiteral("huge-file-panorama"),
      QStringLiteral("slider-latency-targets"),
      QStringLiteral("brush-latency-targets"),
      QStringLiteral("import-throughput-targets"),
      QStringLiteral("preview-render-targets"),
      QStringLiteral("export-throughput-targets"),
      QStringLiteral("benchmark-dashboards"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "performanceCapabilityCount",
          Q_RETURN_ARG(QVariant, performance_capability_count)) ||
      performance_capability_count.toInt() < expected_performance_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "performanceCapabilitySignature",
          Q_RETURN_ARG(QVariant, performance_capability_signature))) {
    return fail("workspace should expose the Performance capability registry");
  }
  for (const auto& capability : expected_performance_capabilities) {
    if (!performance_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every Performance workflow capability");
    }
  }
  QVariant ai_model_runtime_capability_count;
  QVariant ai_model_runtime_capability_signature;
  const QStringList expected_ai_model_runtime_capabilities = {
      QStringLiteral("model-download-install-ui"),
      QStringLiteral("model-versioning"),
      QStringLiteral("acceleration-detection"),
      QStringLiteral("model-fallback-modes"),
      QStringLiteral("privacy-offline-indicators"),
      QStringLiteral("model-error-states"),
      QStringLiteral("model-device-selection"),
      QStringLiteral("quantized-model-options"),
      QStringLiteral("batch-inference"),
      QStringLiteral("preview-full-refinement"),
      QStringLiteral("model-warmup"),
      QStringLiteral("model-cache-management"),
  };
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "aiModelRuntimeCapabilityCount",
          Q_RETURN_ARG(QVariant, ai_model_runtime_capability_count)) ||
      ai_model_runtime_capability_count.toInt() <
          expected_ai_model_runtime_capabilities.size() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "aiModelRuntimeCapabilitySignature",
          Q_RETURN_ARG(QVariant, ai_model_runtime_capability_signature))) {
    return fail("workspace should expose the AI and Model Runtime capability registry");
  }
  for (const auto& capability : expected_ai_model_runtime_capabilities) {
    if (!ai_model_runtime_capability_signature.toString().contains(capability)) {
      return fail("workspace should expose every AI and Model Runtime workflow capability");
    }
  }
  const auto remaining_capability_result =
      expect_capability_registry(
          "reliabilityRecoveryCapabilityCount",
          "reliabilityRecoveryCapabilitySignature",
          {
              QStringLiteral("undo-redo-hardening"),
              QStringLiteral("visible-history-panel"),
              QStringLiteral("workspace-autosave"),
              QStringLiteral("crash-safe-recipe-writes"),
              QStringLiteral("job-recovery"),
              QStringLiteral("catalog-repair-tools"),
              QStringLiteral("cache-repair-tools"),
              QStringLiteral("relink-repair-tools"),
              QStringLiteral("project-package-collect-originals"),
              QStringLiteral("sidecar-backup-restore"),
          },
          "Reliability and Recovery",
          "Reliability and Recovery");
  if (remaining_capability_result != EXIT_SUCCESS) {
    return remaining_capability_result;
  }
  const auto collaboration_capability_result =
      expect_capability_registry(
          "collaborationHandoffCapabilityCount",
          "collaborationHandoffCapabilitySignature",
          {
              QStringLiteral("project-packaging"),
              QStringLiteral("collect-originals"),
              QStringLiteral("sidecar-recipes"),
              QStringLiteral("edit-history-export"),
              QStringLiteral("comments-annotations"),
              QStringLiteral("client-proof-galleries"),
              QStringLiteral("external-editor-roundtrip"),
              QStringLiteral("plugin-extension-api"),
              QStringLiteral("scripting-automation-hooks"),
              QStringLiteral("custom-export-actions"),
              QStringLiteral("custom-model-plugins"),
          },
          "Collaboration and Handoff",
          "Collaboration and Handoff");
  if (collaboration_capability_result != EXIT_SUCCESS) {
    return collaboration_capability_result;
  }
  const auto qa_capability_result =
      expect_capability_registry(
          "qaTestingCapabilityCount",
          "qaTestingCapabilitySignature",
          {
              QStringLiteral("golden-image-corpus"),
              QStringLiteral("raw-corpus"),
              QStringLiteral("color-chart-tests"),
              QStringLiteral("perceptual-delta-tests"),
              QStringLiteral("recipe-parser-fuzzing"),
              QStringLiteral("huge-file-stress-tests"),
              QStringLiteral("masks-layers-stress-tests"),
              QStringLiteral("catalog-import-stress-tests"),
              QStringLiteral("export-queue-stress-tests"),
              QStringLiteral("render-performance-benchmarks"),
              QStringLiteral("ui-smoke-tests-core-workflows"),
              QStringLiteral("model-runtime-tests-matrix"),
          },
          "QA and Testing",
          "QA and Testing");
  if (qa_capability_result != EXIT_SUCCESS) {
    return qa_capability_result;
  }
  const auto observability_capability_result =
      expect_capability_registry(
          "observabilityCapabilityCount",
          "observabilityCapabilitySignature",
          {
              QStringLiteral("render-timings"),
              QStringLiteral("gpu-fallback-reasons"),
              QStringLiteral("failed-exports"),
              QStringLiteral("failed-imports"),
              QStringLiteral("model-failures"),
              QStringLiteral("catalog-db-health"),
              QStringLiteral("cache-hit-rates"),
              QStringLiteral("memory-pressure"),
              QStringLiteral("privacy-safe-diagnostics-bundles"),
              QStringLiteral("crash-reporting"),
          },
          "Observability",
          "Observability");
  if (observability_capability_result != EXIT_SUCCESS) {
    return observability_capability_result;
  }
  const auto packaging_capability_result =
      expect_capability_registry(
          "packagingProductizationCapabilityCount",
          "packagingProductizationCapabilitySignature",
          {
              QStringLiteral("signed-installers"),
              QStringLiteral("macos-notarization"),
              QStringLiteral("update-channels"),
              QStringLiteral("migration-tests"),
              QStringLiteral("first-run-model-profile-setup"),
              QStringLiteral("sample-catalog"),
              QStringLiteral("reset-repair-tools"),
              QStringLiteral("licensing-entitlement-groundwork"),
              QStringLiteral("release-notes-workflow"),
          },
          "Packaging and Productization",
          "Packaging and Productization");
  if (packaging_capability_result != EXIT_SUCCESS) {
    return packaging_capability_result;
  }
  const auto accessibility_capability_result =
      expect_capability_registry(
          "accessibilityLocalizationCapabilityCount",
          "accessibilityLocalizationCapabilitySignature",
          {
              QStringLiteral("keyboard-only-editing"),
              QStringLiteral("screen-reader-labels"),
              QStringLiteral("scalable-ui"),
              QStringLiteral("high-contrast-mode"),
              QStringLiteral("reduced-motion-mode"),
              QStringLiteral("color-blind-safe-overlays"),
              QStringLiteral("visible-string-localization"),
              QStringLiteral("localized-metadata-date-number-formatting"),
          },
          "Accessibility and Localization",
          "Accessibility and Localization");
  if (accessibility_capability_result != EXIT_SUCCESS) {
    return accessibility_capability_result;
  }
  const auto documentation_capability_result =
      expect_capability_registry(
          "documentationHelpCapabilityCount",
          "documentationHelpCapabilitySignature",
          {
              QStringLiteral("searchable-command-help"),
              QStringLiteral("in-app-tool-help"),
              QStringLiteral("troubleshooting-diagnostics"),
              QStringLiteral("preset-locations-docs"),
              QStringLiteral("model-locations-docs"),
              QStringLiteral("profile-locations-docs"),
              QStringLiteral("sidecar-behavior-docs"),
              QStringLiteral("catalog-backup-recovery-docs"),
              QStringLiteral("export-color-management-docs"),
          },
          "Documentation and Help",
          "Documentation and Help");
  if (documentation_capability_result != EXIT_SUCCESS) {
    return documentation_capability_result;
  }
  const auto acceptance_criteria_result =
      expect_capability_registry(
          "acceptanceCriteriaCount",
          "acceptanceCriteriaSignature",
          {
              QStringLiteral("pro-shoot-end-to-end"),
              QStringLiteral("raw-color-quality"),
              QStringLiteral("nondestructive-recoverable-portable"),
              QStringLiteral("fast-precise-stack-masking"),
              QStringLiteral("credible-inpaint-retouch-undo"),
              QStringLiteral("layer-compositing-workflows"),
              QStringLiteral("responsive-large-workloads"),
              QStringLiteral("export-fidelity"),
              QStringLiteral("clear-failure-recovery"),
              QStringLiteral("professional-accessible-ui"),
          },
          "Acceptance Criteria",
          "Acceptance Criteria");
  if (acceptance_criteria_result != EXIT_SUCCESS) {
    return acceptance_criteria_result;
  }
  QVariant all_capability_count;
  QVariant all_capability_signature;
  QVariant capability_registry_gate_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "allCapabilityCount",
          Q_RETURN_ARG(QVariant, all_capability_count)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "allCapabilitySignature",
          Q_RETURN_ARG(QVariant, all_capability_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "capabilityRegistryGateSignature",
          Q_RETURN_ARG(QVariant, capability_registry_gate_signature)) ||
      all_capability_count.toInt() < 179 ||
      !capability_registry_gate_signature.toString().startsWith(QStringLiteral("total:")) ||
      !capability_registry_gate_signature.toString().contains(QStringLiteral("|duplicates:")) ||
      !capability_registry_gate_signature.toString().endsWith(QStringLiteral("|duplicates:")) ||
      !all_capability_signature.toString().contains(QStringLiteral("pro-shoot-end-to-end")) ||
      !all_capability_signature.toString().contains(QStringLiteral("export-queue-controls")) ||
      !all_capability_signature.toString().contains(QStringLiteral("model-runtime-tests-matrix"))) {
    std::cerr << "Capability registry gate: "
              << capability_registry_gate_signature.toString().toStdString() << '\n';
    return fail("workspace should expose one duplicate-free capability registry gate for every claimed capability");
  }
  QVariant focus_peaking_tooltip;
  if (edit_workspace->property("complexTooltipCount").toInt() < 12 ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "complexToolTooltip",
          Q_RETURN_ARG(QVariant, focus_peaking_tooltip),
          Q_ARG(QVariant, QStringLiteral("focus-peaking"))) ||
      !focus_peaking_tooltip.toString().contains(QStringLiteral("edges")) ||
      focus_peaking_tooltip.toString().size() < 80) {
    return fail("workspace should expose high-quality tooltips for complex tools");
  }
  QVariant health_signature;
  QVariant health_detail_signature;
  QVariant health_count;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "systemHealthSignature",
          Q_RETURN_ARG(QVariant, health_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "systemHealthDetailSignature",
          Q_RETURN_ARG(QVariant, health_detail_signature)) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "systemHealthItemCount",
          Q_RETURN_ARG(QVariant, health_count)) ||
      health_count.toInt() != 4 ||
      !health_signature.toString().contains(QStringLiteral("GPU:")) ||
      !health_signature.toString().contains(QStringLiteral("Model:")) ||
      !health_signature.toString().contains(QStringLiteral("Catalog:")) ||
      !health_signature.toString().contains(QStringLiteral("Export:")) ||
      !health_detail_signature.toString().contains(editor_preview.smartSelectionRuntimeLabel()) ||
      !health_detail_signature.toString().contains(editor_preview.inpaintRuntimeLabel()) ||
      !health_detail_signature.toString().contains(QStringLiteral("root:")) ||
      !health_detail_signature.toString().contains(QStringLiteral("|sam:")) ||
      !health_detail_signature.toString().contains(QStringLiteral("|lama:")) ||
      !health_detail_signature.toString().contains(QStringLiteral("|onnx:")) ||
      !health_detail_signature.toString().contains(QStringLiteral("|vision:")) ||
      edit_workspace->findChild<QObject*>(QStringLiteral("healthStatusStrip")) == nullptr) {
    return fail("workspace should expose in-app health status for GPU, model, catalog, export, and model diagnostics");
  }
  QVariant histogram_available;
  QVariant waveform_available;
  QVariant parade_available;
  QVariant vectorscope_available;
  QVariant scope_channel_signature;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "scopeAnalysisModeAvailable",
          Q_RETURN_ARG(QVariant, histogram_available),
          Q_ARG(QVariant, QStringLiteral("Histogram"))) ||
      !histogram_available.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeAnalysisModeAvailable",
          Q_RETURN_ARG(QVariant, waveform_available),
          Q_ARG(QVariant, QStringLiteral("Waveform"))) ||
      !waveform_available.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeAnalysisModeAvailable",
          Q_RETURN_ARG(QVariant, parade_available),
          Q_ARG(QVariant, QStringLiteral("Parade"))) ||
      !parade_available.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeAnalysisModeAvailable",
          Q_RETURN_ARG(QVariant, vectorscope_available),
          Q_ARG(QVariant, QStringLiteral("Vectorscope"))) ||
      !vectorscope_available.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeChannelSignature",
          Q_RETURN_ARG(QVariant, scope_channel_signature)) ||
      !scope_channel_signature.toString().contains(QStringLiteral("rgb:true")) ||
      !scope_channel_signature.toString().contains(QStringLiteral("luma:true"))) {
    return fail("workspace should expose RGB and luma histogram scope channels");
  }
  QVariant scope_layout_result;
  QVariant scope_sampling_result;
  QVariant scope_image_mode_result;
  QVariant scope_large_mode_result;
  QVariant invalid_scope_view_result;
  QVariant scope_view_signature;
  QVariant updated_scope_signature;
  QVariant proofing_status_signature;
  editor_preview.setOutputColorSpace(QStringLiteral("Display P3"));
  editor_preview.setSoftProofProfile(QStringLiteral("sRGB"));
  editor_preview.setSoftProofEnabled(true);
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("scopes"));
  edit_workspace->setProperty("largeScopesMode", false);
  QCoreApplication::processEvents();
  auto* inspector_scope_grid = find_named_object(edit_workspace, QStringLiteral("inspectorScopeGrid"));
  auto* scope_async_status_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("scopeAsyncStatusLabel"));
  auto* scope_proofing_status_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("scopeProofingStatusLabel"));
  auto* scopes_image_view_mode_button =
      find_named_object(edit_workspace, QStringLiteral("scopesImageViewModeButton"));
  auto* scopes_large_view_mode_button =
      find_named_object(edit_workspace, QStringLiteral("scopesLargeViewModeButton"));
  auto* large_scopes_view = find_named_object(edit_workspace, QStringLiteral("largeScopesView"));
  auto* large_scopes_image_mode_button =
      find_named_object(edit_workspace, QStringLiteral("largeScopesImageModeButton"));
  auto* large_scope_proofing_status_label =
      edit_workspace->findChild<QObject*>(QStringLiteral("largeScopeProofingStatusLabel"));
  auto* scope_rgb_toggle = find_named_object(edit_workspace, QStringLiteral("scopeRgbToggle"));
  auto* scope_luma_toggle = find_named_object(edit_workspace, QStringLiteral("scopeLumaToggle"));
  auto* scope_density_toggle = find_named_object(edit_workspace, QStringLiteral("scopeDensityToggle"));
  auto* scope_trace_toggle = find_named_object(edit_workspace, QStringLiteral("scopeTraceToggle"));
  auto* scope_grid_toggle = find_named_object(edit_workspace, QStringLiteral("scopeGridToggle"));
  auto* scope_log_toggle = find_named_object(edit_workspace, QStringLiteral("scopeLogToggle"));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setScopeLayoutPreset",
          Q_RETURN_ARG(QVariant, scope_layout_result),
          Q_ARG(QVariant, QStringLiteral("Quad"))) ||
      !scope_layout_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "setScopeSamplingMode",
          Q_RETURN_ARG(QVariant, scope_sampling_result),
          Q_ARG(QVariant, QStringLiteral("Precise"))) ||
      !scope_sampling_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopeChannelSignature",
          Q_RETURN_ARG(QVariant, updated_scope_signature)) ||
      !updated_scope_signature.toString().contains(QStringLiteral("layout:Quad")) ||
      !updated_scope_signature.toString().contains(QStringLiteral("sampling:Precise")) ||
      inspector_scope_grid == nullptr ||
      inspector_scope_grid->property("effectiveColumns").toInt() != 1 ||
      scope_async_status_label == nullptr ||
      !scope_async_status_label->property("text").toString().contains(QStringLiteral("Scopes")) ||
      scope_proofing_status_label == nullptr ||
      !scope_proofing_status_label->property("text").toString().contains(QStringLiteral("Output Display P3")) ||
      !scope_proofing_status_label->property("text").toString().contains(QStringLiteral("Proof sRGB")) ||
      large_scope_proofing_status_label == nullptr ||
      !large_scope_proofing_status_label->property("text").toString().contains(QStringLiteral("Output Display P3")) ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "proofingStatusSignature",
          Q_RETURN_ARG(QVariant, proofing_status_signature)) ||
      proofing_status_signature.toString() !=
          QStringLiteral("output:Display P3|proof:true|profile:sRGB") ||
      scopes_image_view_mode_button == nullptr || scopes_large_view_mode_button == nullptr ||
      large_scopes_view == nullptr || large_scopes_image_mode_button == nullptr ||
      scope_rgb_toggle == nullptr || scope_luma_toggle == nullptr ||
      scope_density_toggle == nullptr || scope_trace_toggle == nullptr ||
      scope_grid_toggle == nullptr || scope_log_toggle == nullptr) {
    return fail("workspace should expose scope layout presets and sampling controls");
  }
  const auto scope_recipe_before_toggles = editor_preview.currentAdjustmentPatchJson();
  if (!QMetaObject::invokeMethod(scope_rgb_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_luma_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_density_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_trace_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_grid_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_log_toggle, "click")) {
    return fail("scope display toggles should be reachable as inspector controls");
  }
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "scopeChannelSignature",
          Q_RETURN_ARG(QVariant, updated_scope_signature)) ||
      !updated_scope_signature.toString().contains(QStringLiteral("rgb:false")) ||
      !updated_scope_signature.toString().contains(QStringLiteral("luma:false")) ||
      !updated_scope_signature.toString().contains(QStringLiteral("density:false")) ||
      !updated_scope_signature.toString().contains(QStringLiteral("trace:false")) ||
      !updated_scope_signature.toString().contains(QStringLiteral("grid:false")) ||
      !updated_scope_signature.toString().contains(QStringLiteral("log:true")) ||
      editor_preview.currentAdjustmentPatchJson() != scope_recipe_before_toggles) {
    return fail("scope display toggles should change only scope presentation state, not the edit recipe");
  }
  if (!QMetaObject::invokeMethod(scope_rgb_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_luma_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_density_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_trace_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_grid_toggle, "click") ||
      !QMetaObject::invokeMethod(scope_log_toggle, "click")) {
    return fail("scope display toggles should be reversible from the inspector controls");
  }
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setScopesViewMode",
          Q_RETURN_ARG(QVariant, scope_image_mode_result),
          Q_ARG(QVariant, QStringLiteral("image"))) ||
      !scope_image_mode_result.toBool() ||
      !QMetaObject::invokeMethod(
          edit_workspace,
          "scopesViewModeSignature",
          Q_RETURN_ARG(QVariant, scope_view_signature)) ||
      scope_view_signature.toString() != QStringLiteral("mode:image|image:true|large:false") ||
      !edit_image_viewport->property("visible").toBool() ||
      large_scopes_view->property("visible").toBool() ||
      !scopes_image_view_mode_button->property("active").toBool() ||
      scopes_large_view_mode_button->property("active").toBool()) {
    return fail("scope image view mode should keep the photo visible and mark Image as the active mode");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setScopesViewMode",
          Q_RETURN_ARG(QVariant, scope_large_mode_result),
          Q_ARG(QVariant, QStringLiteral("large"))) ||
      !scope_large_mode_result.toBool()) {
    return fail("workspace should expose an explicit large scopes view mode");
  }
  QCoreApplication::processEvents();
  auto* large_scope_grid = find_named_object(edit_workspace, QStringLiteral("largeScopeGrid"));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "scopesViewModeSignature",
          Q_RETURN_ARG(QVariant, scope_view_signature)) ||
      scope_view_signature.toString() != QStringLiteral("mode:large|image:false|large:true") ||
      large_scope_grid == nullptr || large_scope_grid->property("effectiveColumns").toInt() != 2 ||
      edit_image_viewport->property("visible").toBool() ||
      !large_scopes_view->property("visible").toBool() ||
      edit_workspace->property("editInspectorTab").toString() != QStringLiteral("scopes") ||
      edit_workspace->property("editContextPanelActive").toBool() ||
      scopes_image_view_mode_button->property("active").toBool() ||
      !scopes_large_view_mode_button->property("active").toBool()) {
    return fail("large scope mode should be explicit, hide the photo only in scopes-only view, and release context-panel space");
  }
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setScopesViewMode",
          Q_RETURN_ARG(QVariant, invalid_scope_view_result),
          Q_ARG(QVariant, QStringLiteral("bogus"))) ||
      invalid_scope_view_result.toBool() ||
      edit_workspace->property("largeScopesMode").toBool() != true) {
    return fail("invalid scope view modes should be rejected without changing the active mode");
  }
  if (!QMetaObject::invokeMethod(large_scopes_image_mode_button, "click")) {
    return fail("large scopes view should expose a direct return-to-image action");
  }
  QCoreApplication::processEvents();
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "scopesViewModeSignature",
          Q_RETURN_ARG(QVariant, scope_view_signature)) ||
      scope_view_signature.toString() != QStringLiteral("mode:image|image:true|large:false") ||
      !edit_image_viewport->property("visible").toBool() ||
      large_scopes_view->property("visible").toBool()) {
    return fail("returning from large scopes should restore the image viewport");
  }
  catalog_assets.replaceAssetsForTesting({
      make_asset(
          QStringLiteral("visual-proof-1"),
          viewport_asset_path,
          QUrl::fromLocalFile(viewport_asset_path).toString()),
      make_asset(
          QStringLiteral("visual-proof-2"),
          viewport_asset_path,
          QUrl::fromLocalFile(viewport_asset_path).toString()),
      make_asset(
          QStringLiteral("visual-proof-3"),
          viewport_asset_path,
          QUrl::fromLocalFile(viewport_asset_path).toString()),
      make_asset(
          QStringLiteral("overlay-proof-bright"),
          bright_overlay_asset_path,
          QUrl::fromLocalFile(bright_overlay_asset_path).toString()),
      make_asset(
          QStringLiteral("overlay-proof-dark"),
          dark_overlay_asset_path,
          QUrl::fromLocalFile(dark_overlay_asset_path).toString()),
      make_asset(
          QStringLiteral("overlay-proof-saturated"),
          saturated_overlay_asset_path,
          QUrl::fromLocalFile(saturated_overlay_asset_path).toString()),
      make_asset(
          QStringLiteral("overlay-proof-transparent"),
          transparent_overlay_asset_path,
          QUrl::fromLocalFile(transparent_overlay_asset_path).toString()),
  });
  catalog_assets.selectIndex(0);
  if (!wait_for_condition([&]() {
        return editor_preview.hasAsset() &&
               editor_preview.assetId() == QStringLiteral("visual-proof-1");
      })) {
    return fail("desktop visual proof should load a seeded filmstrip asset");
  }
  edit_workspace->setProperty("diagnosticsPanelOpen", false);
  edit_workspace->setProperty("editContextPanelVisible", true);
  edit_workspace->setProperty("editSidebarCollapsed", false);
  edit_workspace->setProperty("editInspectorPanelVisible", true);
  edit_workspace->setProperty("largeScopesMode", false);
  edit_workspace->setProperty("editAdjustmentFilterText", QString());
  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("adjust-with-filmstrip"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editFilmstrip"),
              QStringLiteral("editFilmstripThumbnailCard"),
              QStringLiteral("editInspectorPanel"),
              QStringLiteral("lightInspectorGroup"),
              QStringLiteral("editAdjustmentFilterRow"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture Adjust controls with filmstrip and without pinned inline scopes");
  }

  edit_workspace->setProperty("editInspectorTab", QStringLiteral("presets"));
  edit_workspace->setProperty("studioPresetCategoryFilter", QStringLiteral("All"));
  edit_workspace->setProperty("studioPresetPackFilter", QStringLiteral("All"));
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("presets-tab"),
          {
              QStringLiteral("editInspectorPanel"),
              QStringLiteral("presetsInspectorTabButton"),
              QStringLiteral("presetLibraryInspectorGroup"),
              QStringLiteral("studioPresetLibraryList"),
              QStringLiteral("studioPresetLibraryCard"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture the Presets tab");
  }

  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  edit_workspace->setProperty("editAdjustmentFilterText", QStringLiteral("grading"));
  edit_workspace->setProperty("splitToningExpanded", true);
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("color-grading"),
          {
              QStringLiteral("editInspectorPanel"),
              QStringLiteral("advancedColorInspectorGroup"),
              QStringLiteral("colorGradingInspectorGroup"),
              QStringLiteral("splitToningGroup"),
              QStringLiteral("colorGradingAmountSlider"),
              QStringLiteral("colorGradingBalanceSlider"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture Color Grading controls");
  }

  edit_workspace->setProperty("editAdjustmentFilterText", QStringLiteral("curve"));
  edit_workspace->setProperty("lightAdvancedToneExpanded", true);
  edit_workspace->setProperty("lightLevelsExpanded", true);
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("tone-curve-input-levels"),
          {
              QStringLiteral("editInspectorPanel"),
              QStringLiteral("toneCurveInspectorGroup"),
              QStringLiteral("studioToneCurveEditor"),
              QStringLiteral("toneCurveInputLevelsGroup"),
              QStringLiteral("toneCurveInputLevelsHistogram"),
              QStringLiteral("toneCurveInputLevelsHandle"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture Tone Curve and Input Levels");
  }

  edit_workspace->setProperty("editAdjustmentFilterText", QString());
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("scopes"));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "setScopesViewMode",
          Q_RETURN_ARG(QVariant, scope_image_mode_result),
          Q_ARG(QVariant, QStringLiteral("image"))) ||
      !scope_image_mode_result.toBool()) {
    return fail("desktop visual proof should set scopes inspector image mode");
  }
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("scopes-inspector"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editInspectorPanel"),
              QStringLiteral("scopeAsyncStatusLabel"),
              QStringLiteral("scopeProofingStatusLabel"),
              QStringLiteral("scopeRgbToggle"),
              QStringLiteral("scopesLargeViewModeButton"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture Scopes tab while the image stays visible");
  }

  edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("layers-tab"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editInspectorPanel"),
              QStringLiteral("layersInspectorTabButton"),
              QStringLiteral("photoLayersInspectorGroup"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture Layers");
  }

  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "startMaskBrushSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("add"))))) {
    return fail("desktop visual proof should start brush mode");
  }
  editor_preview.setMaskPreviewMode(QStringLiteral("overlay"));
  editor_preview.beginActiveBrushStroke();
  editor_preview.addBrushPointWithTablet(0.46, 0.54, 0.72, 0.0, 0.0);
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("brush-mode"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("brushToolHud"),
              QStringLiteral("brushHudStatusLabel"),
              QStringLiteral("brushContextPanel"),
              QStringLiteral("editImageViewportMaskCanvasOverlay"),
          },
          &visual_proof_manifest)) {
    editor_preview.endActiveBrushStroke();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return fail("desktop visual proof should capture Brush mode");
  }
  editor_preview.endActiveBrushStroke();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);

  QVariant retouch_visual_mode;
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "startRetouchTool",
          Q_RETURN_ARG(QVariant, retouch_visual_mode),
          Q_ARG(QVariant, QStringLiteral("heal")),
          Q_ARG(QVariant, false)) ||
      retouch_visual_mode.toString() != QStringLiteral("heal")) {
    return fail("desktop visual proof should start retouch mode");
  }
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("retouch-heal-mode"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("retouchToolHud"),
              QStringLiteral("retouchHudStatusLabel"),
              QStringLiteral("retouchHudModeMenuButton"),
              QStringLiteral("retouchHudMoreMenuButton"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture Retouch mode without wrapped or overlapping HUD controls");
  }

  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  if (!QMetaObject::invokeMethod(
          edit_workspace,
          "createMaskSource",
          Q_ARG(QVariant, QVariant(QStringLiteral("object"))))) {
    return fail("desktop visual proof should start object selection");
  }
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("object-selection"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("maskCreationToolbar"),
              QStringLiteral("maskSourceObjectButton"),
              QStringLiteral("editImageViewportMaskCanvasOverlay"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture Object Selection");
  }

  auto capture_overlay_mode_visual_proof = [&](
                                               int asset_index,
                                               const QString& asset_id,
                                               const QString& fixture_kind,
                                               const QString& preview_mode,
                                               const QString& state,
                                               const QStringList& required_objects) {
    catalog_assets.selectIndex(asset_index);
    if (!wait_for_condition([&]() {
          return editor_preview.hasAsset() && editor_preview.assetId() == asset_id;
        })) {
      std::cerr << "Overlay visual proof should load asset " << asset_id.toStdString() << '\n';
      return false;
    }
    if (!QMetaObject::invokeMethod(
            edit_workspace,
            "createMaskSource",
            Q_ARG(QVariant, QVariant(QStringLiteral("rectangle"))))) {
      std::cerr << "Overlay visual proof should create a rectangle selection for "
                << asset_id.toStdString() << '\n';
      return false;
    }
    editor_preview.setActiveMaskCenterX(0.5);
    editor_preview.setActiveMaskCenterY(0.5);
    editor_preview.setActiveMaskRadiusX(0.28);
    editor_preview.setActiveMaskRadiusY(0.22);
    editor_preview.setActiveMaskFeather(0.035);
    editor_preview.setMaskPreviewOverlayOpacity(1.0);
    editor_preview.setMaskPreviewOverlayColor(QStringLiteral("#24dcc2"));
    editor_preview.setMaskPreviewMode(preview_mode);
    process_layout_events(18);
    QJsonObject metadata;
    metadata.insert(QStringLiteral("fixture"), fixture_kind);
    metadata.insert(QStringLiteral("maskPreviewMode"), editor_preview.maskPreviewMode());
    metadata.insert(QStringLiteral("activeMaskKind"), editor_preview.activeMaskKind());
    metadata.insert(QStringLiteral("assetId"), editor_preview.assetId());
    return capture_visual_proof_pair(
        root_window,
        root,
        visual_proof_dir,
        state,
        required_objects,
        &visual_proof_manifest,
        metadata);
  };

  if (!capture_overlay_mode_visual_proof(
          3,
          QStringLiteral("overlay-proof-bright"),
          QStringLiteral("bright"),
          QStringLiteral("overlay"),
          QStringLiteral("mask-overlay-bright-color-overlay"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editImageViewportSharedMaskOverlay"),
              QStringLiteral("editImageViewportMaskCanvasOverlay"),
          })) {
    return fail("desktop visual proof should capture color overlay mode on a bright image");
  }
  if (!capture_overlay_mode_visual_proof(
          4,
          QStringLiteral("overlay-proof-dark"),
          QStringLiteral("dark"),
          QStringLiteral("marching-ants"),
          QStringLiteral("mask-overlay-dark-marching-ants"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editImageViewportSharedMaskOverlay"),
              QStringLiteral("editImageViewportSharedActiveSelectionOutline"),
          })) {
    return fail("desktop visual proof should capture marching ants mode on a dark image");
  }
  if (!capture_overlay_mode_visual_proof(
          5,
          QStringLiteral("overlay-proof-saturated"),
          QStringLiteral("high-saturation"),
          QStringLiteral("color-on-bw"),
          QStringLiteral("mask-overlay-saturated-color-on-bw"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editImageViewportSharedMaskOverlay"),
              QStringLiteral("editImageViewportMaskCanvasOverlay"),
              QStringLiteral("editImageViewportSharedActiveSelectionOutline"),
          })) {
    return fail("desktop visual proof should capture Color on B&W mode on a high-saturation image");
  }
  if (!capture_overlay_mode_visual_proof(
          6,
          QStringLiteral("overlay-proof-transparent"),
          QStringLiteral("transparent"),
          QStringLiteral("on-white"),
          QStringLiteral("mask-overlay-transparent-on-white"),
          {
              QStringLiteral("editImageViewport"),
              QStringLiteral("editImageViewportSharedMaskOverlay"),
              QStringLiteral("editImageViewportMaskCanvasOverlay"),
          })) {
    return fail("desktop visual proof should capture On White mode on a transparent image");
  }

  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  edit_workspace->setProperty("editAdjustmentFilterText", QStringLiteral("detail"));
  process_layout_events();
  if (!capture_visual_proof_pair(
          root_window,
          root,
          visual_proof_dir,
          QStringLiteral("detail-controls"),
          {
              QStringLiteral("editInspectorPanel"),
              QStringLiteral("detailInspectorGroup"),
              QStringLiteral("detailSharpeningGroup"),
              QStringLiteral("detailNoiseReductionGroup"),
              QStringLiteral("detailInspectOneToOneButton"),
          },
          &visual_proof_manifest)) {
    return fail("desktop visual proof should capture Detail controls");
  }

  edit_workspace->setProperty("editAdjustmentFilterText", QString());
  edit_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
  edit_workspace->setProperty("largeScopesMode", false);
  QMetaObject::invokeMethod(
      edit_workspace, "activateEditTool", Q_ARG(QVariant, QStringLiteral("adjust")));
  root->setProperty("width", 1600);
  root->setProperty("height", 1000);
  process_layout_events();
  const auto visual_manifest_path = QDir(visual_proof_dir).filePath(QStringLiteral("manifest.json"));
  QFile visual_manifest_file(visual_manifest_path);
  if (!visual_manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      visual_manifest_file.write(QJsonDocument(visual_proof_manifest).toJson(QJsonDocument::Indented)) <=
          0) {
    return fail("desktop visual proof should write a manifest");
  }
  visual_manifest_file.close();
  if (visual_proof_manifest.size() != 32) {
    return fail("desktop visual proof should capture fourteen required editor states plus four pixel zoom proof states");
  }
  std::cout << "desktop_visual_proof_dir=" << visual_proof_dir.toStdString() << '\n';

  root->setProperty("currentWorkspace", root->property("libraryWorkspaceIndex"));
  QCoreApplication::processEvents();

  catalog_assets.replaceAssetsForTesting(
      {make_asset(QStringLiteral("survey-1"), QStringLiteral("IMG_0101.jpg")),
       make_asset(QStringLiteral("survey-2"), QStringLiteral("IMG_0102.jpg")),
       make_asset(QStringLiteral("survey-3"), QStringLiteral("IMG_0103.jpg")),
       make_asset(QStringLiteral("survey-4"), QStringLiteral("IMG_0104.jpg"))});
  catalog_assets.selectIndex(1);
  QCoreApplication::processEvents();
  auto* library_workspace = root->findChild<QObject*>(QStringLiteral("libraryStudioWorkspace"));
  if (library_workspace == nullptr) {
    return fail("library workspace should expose a testable StudioWorkspace object");
  }
  QVariant page_visibility_audit_signature;
  QVariant active_visibility_contract;
  if (!QMetaObject::invokeMethod(
          root,
          "pageVisibilityAuditSignature",
          Q_RETURN_ARG(QVariant, page_visibility_audit_signature)) ||
      !page_visibility_audit_signature.toString().contains(
          QStringLiteral("page-visibility-audit:v1|pages:library,import,edit,masks,carousel,timeline,export")) ||
      !page_visibility_audit_signature.toString().contains(QStringLiteral("|active:library|")) ||
      !page_visibility_audit_signature.toString().contains(QStringLiteral("|live:library-studio-visibility:v1|")) ||
      !page_visibility_audit_signature.toString().contains(
          QStringLiteral("|coverage:library/import/edit/masks/carousel/timeline/export")) ||
      !QMetaObject::invokeMethod(
          library_workspace,
          "activeVisibilityContract",
          Q_RETURN_ARG(QVariant, active_visibility_contract)) ||
      !active_visibility_contract.toString().startsWith(QStringLiteral("library-studio-visibility:v1"))) {
    std::cerr << "Library page visibility audit: "
              << page_visibility_audit_signature.toString().toStdString() << '\n';
    return fail("App shell visibility audit should report Library as the active preserved-function page");
  }
  auto* studio_library_import_button =
      find_named_object(library_workspace, QStringLiteral("studioLibraryImportButton"));
  auto* library_inspector_panel = library_workspace->findChild<QObject*>(QStringLiteral("libraryInspectorPanel"));
  QVariant library_workflow_polish_signature;
  if (studio_library_import_button == nullptr || library_inspector_panel == nullptr ||
      !studio_library_import_button->property("visible").toBool() ||
      studio_library_import_button->property("text").toString() != QStringLiteral("Import") ||
      library_inspector_panel->property("title").toString() != QStringLiteral("Inspector") ||
      library_workspace->property("libraryWorkflowPolishContract").toString() !=
          QStringLiteral("library-workflow-polish:v3|first-glance:justified-grid-dominant-toolbar-import-filters-inspector|close-inspection:search-filter-selection-review-import|clutter:no-empty-sources-rail-contextual-selection-actions|interaction:search-filter-select-review-edit-import-compare-export|text:filenames-sidebar-only|functionality:preserved") ||
      library_workspace->property("libraryPageCompletionContract").toString() !=
          QStringLiteral("page-completion:v3|library|sidebar:none|content:justified-grid-survey-empty-import|inspector:preview-filename-metadata-review-batch|toolbar:search-filters-import-view-panel-toggles|actions:edit-import-compare-export-select|states:default-selected-multi-filtered-import-empty-offline") ||
      !QMetaObject::invokeMethod(
          library_workspace,
          "libraryWorkflowPolishSignature",
          Q_RETURN_ARG(QVariant, library_workflow_polish_signature)) ||
      !library_workflow_polish_signature.toString().contains(
          QStringLiteral("library-workflow-polish:v1|search:idle|filters:clear|selection-mode:false|selected:0|view:grid")) ||
      !library_workflow_polish_signature.toString().contains(
          QStringLiteral("|sources:false|inspector:true|inspector-state:single|import:false|activity:false")) ||
      !library_workflow_polish_signature.toString().contains(
          QStringLiteral("|preserve:search-filter-select-rate-pick-reject-edit-import-compare-export"))) {
    std::cerr << "Library workflow polish: "
              << library_workflow_polish_signature.toString().toStdString() << '\n';
    return fail("library toolbar import and inspector should use readable labels and expose live workflow-polish completion state");
  }
  auto* studio_library_gallery_grid =
      find_named_object(library_workspace, QStringLiteral("studioLibraryGalleryGrid"));
  QVariant library_gallery_layout_signature;
  library_workspace->setProperty("libraryInspectorPanelVisible", true);
  library_workspace->setProperty("catalogViewMode", QStringLiteral("grid"));
  process_layout_events(4);
  const auto studio_library_gallery_grid_width =
      studio_library_gallery_grid == nullptr
          ? -1
          : studio_library_gallery_grid->property("width").toInt();
  const auto studio_library_gallery_viewport_width =
      studio_library_gallery_grid == nullptr
          ? -1
          : studio_library_gallery_grid->property("galleryViewportWidth").toInt();
  if (studio_library_gallery_grid == nullptr ||
      !QMetaObject::invokeMethod(
          library_workspace,
          "libraryGalleryLayoutSignature",
          Q_RETURN_ARG(QVariant, library_gallery_layout_signature),
          Q_ARG(QVariant, 800),
          Q_ARG(QVariant, 14)) ||
      !library_gallery_layout_signature.toString().contains(
          QStringLiteral("library-gallery-layout:v1|raw:800|gutter:14|viewport:786")) ||
      !library_gallery_layout_signature.toString().contains(QStringLiteral("|last-justified:false|")) ||
      !library_gallery_layout_signature.toString().contains(QStringLiteral("|overflow:false")) ||
      studio_library_gallery_viewport_width >= studio_library_gallery_grid_width) {
    std::cerr << "Library gallery layout: "
              << library_gallery_layout_signature.toString().toStdString()
              << " gridWidth="
              << studio_library_gallery_grid_width
              << " viewport="
              << studio_library_gallery_viewport_width
              << '\n';
    return fail("library gallery rows should reserve a right gutter and keep the final incomplete row at normal size");
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_STOP_AFTER_LIBRARY_GALLERY_LAYOUT")) {
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    editor_preview.setImageProvider(nullptr);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return EXIT_SUCCESS;
  }
  auto* studio_library_search_input =
      find_named_object(library_workspace, QStringLiteral("studioLibrarySearchInput"));
  auto* studio_library_search_clear_button =
      find_named_object(library_workspace, QStringLiteral("studioLibrarySearchClearButton"));
  auto* studio_library_search_chip =
      find_named_object(library_workspace, QStringLiteral("studioLibraryActiveSearchChip"));
  QVariant library_search_signature;
  if (studio_library_search_input == nullptr ||
      studio_library_search_clear_button == nullptr ||
      studio_library_search_chip == nullptr ||
      library_workspace->property("librarySearchBehaviorContract").toString() !=
          QStringLiteral("library-search:v1|draft-state:true|debounce-ms:180|active-state:field-and-chip|clear-visible-only-when-text:true|enter-applies:true|external-sync:true|no-elide:true|functionality:preserved") ||
      !QMetaObject::invokeMethod(
          library_workspace,
          "librarySearchBehaviorSignature",
          Q_RETURN_ARG(QVariant, library_search_signature)) ||
      !library_search_signature.toString().contains(
          QStringLiteral("library-search:v1|draft:false|active:false|pending:false|clear-visible:false|debounce:180"))) {
    std::cerr << "Library search initial signature: "
              << library_search_signature.toString().toStdString() << '\n';
    return fail("Library search should expose idle draft/apply/clear behavior before typing");
  }
  if (!QMetaObject::invokeMethod(
          library_workspace,
          "setLibrarySearchDraft",
          Q_ARG(QVariant, QStringLiteral("IMG_0102")))) {
    return fail("Library search should expose a testable draft setter");
  }
  process_layout_events(2);
  if (!QMetaObject::invokeMethod(
          library_workspace,
          "librarySearchBehaviorSignature",
          Q_RETURN_ARG(QVariant, library_search_signature)) ||
      !library_search_signature.toString().contains(
          QStringLiteral("library-search:v1|draft:true|active:false|pending:true|clear-visible:true|debounce:180")) ||
      !studio_library_search_clear_button->property("visible").toBool() ||
      !studio_library_search_chip->property("visible").toBool() ||
      !catalog_assets.searchFilter().isEmpty()) {
    std::cerr << "Library search pending signature: "
              << library_search_signature.toString().toStdString() << '\n';
    return fail("Library search draft should show clear and status affordances before applying the catalog filter");
  }
  if (!QMetaObject::invokeMethod(library_workspace, "applyLibrarySearchNow")) {
    return fail("Library search should expose an immediate apply action");
  }
  process_layout_events(2);
  if (catalog_assets.searchFilter() != QStringLiteral("IMG_0102") ||
      !QMetaObject::invokeMethod(
          library_workspace,
          "librarySearchBehaviorSignature",
          Q_RETURN_ARG(QVariant, library_search_signature)) ||
      !library_search_signature.toString().contains(
          QStringLiteral("library-search:v1|draft:true|active:true|pending:false|clear-visible:true|debounce:180")) ||
      !studio_library_search_clear_button->property("visible").toBool() ||
      !studio_library_search_chip->property("visible").toBool()) {
    std::cerr << "Library search applied signature: "
              << library_search_signature.toString().toStdString() << '\n';
    return fail("Library search apply should update the catalog filter while keeping explicit clear and active search affordances visible");
  }
  if (!QMetaObject::invokeMethod(library_workspace, "clearLibrarySearch")) {
    return fail("Library search should expose a clear action");
  }
  process_layout_events(2);
  if (!catalog_assets.searchFilter().isEmpty() ||
      studio_library_search_clear_button->property("visible").toBool() ||
      studio_library_search_chip->property("visible").toBool()) {
    return fail("Library search clear should remove the filter and hide search-only clear/status affordances");
  }
  library_workspace->setProperty("catalogViewMode", QStringLiteral("survey"));
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  if (catalog_assets.surveyAssetCount(catalog_assets.selectedIndex(), 6) < 3 ||
      library_workspace->property("catalogViewMode").toString() != QStringLiteral("survey")) {
    return fail("library workspace should expose a populated survey view model");
  }

  if (!QMetaObject::invokeMethod(root, "beginImportWorkspace")) {
    return fail("menu routing should expose beginImportWorkspace");
  }
  QCoreApplication::processEvents();
  if (root->property("currentWorkspace").toInt() != 0 ||
      root->property("libraryWorkspaceMode").toString() != QStringLiteral("import")) {
    return fail("beginImportWorkspace should route to Library import mode");
  }
  if (!QMetaObject::invokeMethod(
          root,
          "pageVisibilityAuditSignature",
          Q_RETURN_ARG(QVariant, page_visibility_audit_signature)) ||
      !page_visibility_audit_signature.toString().contains(QStringLiteral("|active:import|")) ||
      !page_visibility_audit_signature.toString().contains(QStringLiteral("|live:import-studio-visibility:v1|")) ||
      !QMetaObject::invokeMethod(
          library_workspace,
          "activeVisibilityContract",
          Q_RETURN_ARG(QVariant, active_visibility_contract)) ||
      !active_visibility_contract.toString().startsWith(QStringLiteral("import-studio-visibility:v1"))) {
    std::cerr << "Import page visibility audit: "
              << page_visibility_audit_signature.toString().toStdString() << '\n';
    return fail("App shell visibility audit should report routed Library import as an active preserved-function page");
  }
  auto* routed_import_workspace =
      library_workspace->findChild<QObject*>(QStringLiteral("importWorkspace"));
  QVariant routed_import_workflow_signature;
  if (routed_import_workspace == nullptr ||
      library_workspace->property("importWorkflowPolishContract").toString() !=
          QStringLiteral("import-workflow-polish:v1|first-glance:library-routed-import-source-browser|close-inspection:source-folder-media-destination-progress|clutter:library-chrome-quiet-import-dominant|interaction:back-source-select-import-refresh|text:no-elide-no-overlap|functionality:preserved") ||
      library_workspace->property("importPageCompletionContract").toString() !=
          QStringLiteral("page-completion:v1|import-in-library|sidebar:quiet-hidden-during-import|content:ImportView|toolbar:back-title|states:no-source-empty-selected-importing-failed-complete|return:back-to-library") ||
      routed_import_workspace->property("importWorkflowPolishContract").toString() !=
          QStringLiteral("import-workflow-polish:v1|first-glance:sources-current-folder-media-grid-primary-import|close-inspection:source-confirmation-filter-counts-selection-destination-progress|clutter:single-progress-place-split-import-options|interaction:choose-source-select-visible-clear-import-retry-refresh|text:no-elide-no-overlap-paths-wrap|functionality:preserved") ||
      routed_import_workspace->property("importPageCompletionContract").toString() !=
          QStringLiteral("page-completion:v1|import|sidebar:sources-folders-refresh|content:header-filter-media-grid-empty|destination:project-picker-create-confirm-cancel|toolbar:select-visible-clear-primary-import|states:no-source-empty-filtered-selected-importing-failed-complete") ||
      !QMetaObject::invokeMethod(
          routed_import_workspace,
          "importWorkflowPolishSignature",
          Q_RETURN_ARG(QVariant, routed_import_workflow_signature)) ||
      !routed_import_workflow_signature.toString().startsWith(
          QStringLiteral("import-workflow-polish:v1|source:")) ||
      !routed_import_workflow_signature.toString().contains(QStringLiteral("|selected:0|")) ||
      !routed_import_workflow_signature.toString().contains(QStringLiteral("|selected-photos:0|selected-videos:0|")) ||
      !routed_import_workflow_signature.toString().contains(QStringLiteral("|filter:all|")) ||
      !routed_import_workflow_signature.toString().contains(QStringLiteral("|source-confirmation:Folder Ready|")) ||
      !routed_import_workflow_signature.toString().contains(QStringLiteral("|destination:closed|")) ||
      !routed_import_workflow_signature.toString().contains(QStringLiteral("|progress:idle|")) ||
      !routed_import_workflow_signature.toString().contains(
          QStringLiteral("|preserve:choose-source-select-visible-clear-selection-import-selected-import-folder-refresh-project-target"))) {
    std::cerr << "Import workflow polish: "
              << routed_import_workflow_signature.toString().toStdString() << '\n';
    return fail("routed Library import mode should expose source, selection, destination, progress, and preserved import workflow state");
  }

  if (!QMetaObject::invokeMethod(root, "createTimelineFromMenu")) {
    return fail("menu routing should expose createTimelineFromMenu");
  }
  QCoreApplication::processEvents();
  if (root->property("currentWorkspace").toInt() != root->property("timelineWorkspaceIndex").toInt() ||
      timeline_document.timelineName().trimmed().isEmpty()) {
    return fail("createTimelineFromMenu should route to Timeline and initialize a document");
  }
  auto* timeline_workspace = root->findChild<QObject*>(QStringLiteral("timelineWorkspace"));
  QVariant timeline_workflow_polish_signature;
  if (!QMetaObject::invokeMethod(
          root,
          "pageVisibilityAuditSignature",
          Q_RETURN_ARG(QVariant, page_visibility_audit_signature)) ||
      !page_visibility_audit_signature.toString().contains(QStringLiteral("|active:timeline|")) ||
      !page_visibility_audit_signature.toString().contains(QStringLiteral("|live:timeline-visibility:v1|"))) {
    std::cerr << "Timeline page visibility audit: "
              << page_visibility_audit_signature.toString().toStdString() << '\n';
    return fail("App shell visibility audit should report Timeline as the active preserved-function page");
  }
  if (timeline_workspace == nullptr ||
      timeline_workspace->property("timelineWorkflowPolishContract").toString() !=
          QStringLiteral("timeline-workflow-polish:v1|first-glance:source-program-timeline-effects|close-inspection:active-tool-edit-mode-playhead-selection-markers-effects-render|clutter:panels-collapsible-details-contextual|interaction:load-source-insert-overwrite-play-trim-split-marker-effect-render|text:no-elide-no-overlap|functionality:preserved") ||
      timeline_workspace->property("timelinePageCompletionContract").toString() !=
          QStringLiteral("page-completion:v1|timeline|sidebar:source-media-picker|content:program-monitor-transport-timeline-tracks-waveform|inspector:effects-transitions-selected-clip|toolbar:tools-search-marker-filter-zoom|states:setup-empty-source-selected-dragging-rendering-error") ||
      timeline_workspace->property("timelineUiConsistencyContract").toString() !=
          QStringLiteral("timeline-ui-consistency:v1|layout:source-program-inspector-multitrack|theme:shared|type:shared|spacing:shared|controls:shared|popups:shared-lifecycle|states:accessible|video-editor-identity:preserved") ||
      timeline_workspace->property("timelineIntrinsicGeometryContract").toString() !=
          QStringLiteral("timeline-intrinsic-geometry:v1|allowed:time-to-pixel-lane-height-ruler-cadence-playhead-trim-handles-waveform-safe-area-media-black|chrome-values:theme-only|documented-exceptions:true") ||
      !QMetaObject::invokeMethod(
          timeline_workspace,
          "timelineWorkflowPolishSignature",
          Q_RETURN_ARG(QVariant, timeline_workflow_polish_signature)) ||
      !timeline_workflow_polish_signature.toString().contains(
          QStringLiteral("timeline-workflow-polish:v1|mode:timeline|tool:select|edit-mode:insert|timeline:true")) ||
      !timeline_workflow_polish_signature.toString().contains(QStringLiteral("|selected:false|source:false|")) ||
      !timeline_workflow_polish_signature.toString().contains(QStringLiteral("|source-panel:true/true|effects-panel:false|")) ||
      !timeline_workflow_polish_signature.toString().contains(QStringLiteral("|snapping:true|ripple:true|search:false|")) ||
      !timeline_workflow_polish_signature.toString().contains(
          QStringLiteral("|preserve:source-load-insert-overwrite-play-step-seek-select-razor-slip-roll-snap-ripple-split-move-lift-extract-marker-effect-transition-zoom-save-render"))) {
    std::cerr << "Timeline workflow polish: "
              << timeline_workflow_polish_signature.toString().toStdString() << '\n';
    return fail("timeline workspace should expose live source, monitor, tool, marker, track, effect, and render workflow state");
  }
  auto* timeline_play_pause_button =
      find_named_object(timeline_workspace, QStringLiteral("timelinePlayPauseButton"));
  if (timeline_play_pause_button == nullptr ||
      timeline_play_pause_button->property("enabled").toBool() ||
      timeline_play_pause_button->property("disabledReason").toString() !=
          QStringLiteral("Add media to the timeline before using playback controls.") ||
      timeline_play_pause_button->property("resolvedTooltipText").toString() !=
          QStringLiteral("Add media to the timeline before using playback controls.")) {
    return fail("Timeline disabled transport controls should expose user-facing disabled reasons through the shared ActionButton tooltip path");
  }

  const auto program_base_photo = runtime_root.filePath(QStringLiteral("program-base.jpg"));
  const auto program_overlay_photo = runtime_root.filePath(QStringLiteral("program-overlay.jpg"));
  for (const auto& path : {program_base_photo, program_overlay_photo}) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
      return fail("timeline Program Monitor fixtures should be writable");
    }
    file.write("placeholder");
  }
  timeline_document.addClipFromGallery(
      QStringLiteral("photo:program-base"), program_base_photo, QStringLiteral("Program Base"), 4.0, QString{});
  timeline_document.addClipFromGallery(
      QStringLiteral("photo:program-overlay"), program_overlay_photo, QStringLiteral("Program Overlay"), 4.0, QString{});
  timeline_document.addVideoTrack();
  timeline_document.selectClip(1);
  timeline_document.moveSelectedClipToTrackRow(1, 0.0);
  timeline_document.seekSequenceSeconds(1.0);
  process_layout_events(4);
  QVariant program_background_layers;
  if (!QMetaObject::invokeMethod(
          timeline_workspace,
          "programBackgroundLayers",
          Q_RETURN_ARG(QVariant, program_background_layers)) ||
      program_background_layers.toList().size() != 1 ||
      program_background_layers.toList().first().toMap().value(QStringLiteral("laneIndex")).toInt() != 0) {
    return fail("Program Monitor should expose the lower visible lane beneath the active top clip");
  }

  const auto library_workspace_index = root->property("libraryWorkspaceIndex").toInt();
  const auto edit_workspace_index = root->property("editWorkspaceIndex").toInt();
  const auto carousel_workspace_index = root->property("carouselWorkspaceIndex").toInt();
  const auto timeline_workspace_index = root->property("timelineWorkspaceIndex").toInt();
  const auto export_workspace_index = root->property("exportWorkspaceIndex").toInt();
  root->setProperty("currentWorkspace", carousel_workspace_index);
  process_layout_events(8);
  auto* carousel_workspace = root->findChild<QObject*>(QStringLiteral("carouselWorkspace"));
  QVariant carousel_workflow_polish_signature;
  if (!QMetaObject::invokeMethod(
          root,
          "pageVisibilityAuditSignature",
          Q_RETURN_ARG(QVariant, page_visibility_audit_signature)) ||
      !page_visibility_audit_signature.toString().contains(QStringLiteral("|active:carousel|")) ||
      !page_visibility_audit_signature.toString().contains(QStringLiteral("|live:carousel-visibility:v1|"))) {
    std::cerr << "Carousel page visibility audit: "
              << page_visibility_audit_signature.toString().toStdString() << '\n';
    return fail("App shell visibility audit should report Carousel as the active preserved-function page");
  }
  if (carousel_workspace == nullptr ||
      carousel_workspace->property("carouselWorkflowPolishContract").toString() !=
          QStringLiteral("carousel-workflow-polish:v1|first-glance:setup-or-board-canvas-tool-rail-inspector-strip|close-inspection:active-tool-board-selection-media-layer-rating-export|clutter:one-active-selection-context-media-picker-contextual|interaction:create-select-pan-text-media-shape-guide-layer-export|text:no-elide-no-overlap|functionality:preserved") ||
      carousel_workspace->property("carouselPageCompletionContract").toString() !=
          QStringLiteral("page-completion:v1|carousel|rail:select-pan-text-media-shape-guides|content:setup-or-artboard-empty-slot-board-strip|inspector:board-selection-layers-media|toolbar:save-export-board-actions|states:setup-empty-board-selected-media-exporting-error") ||
      !QMetaObject::invokeMethod(
          carousel_workspace,
          "carouselWorkflowPolishSignature",
          Q_RETURN_ARG(QVariant, carousel_workflow_polish_signature)) ||
      !carousel_workflow_polish_signature.toString().contains(
          QStringLiteral("carousel-workflow-polish:v1|mode:setup|active-tool:select|carousel:false")) ||
      !carousel_workflow_polish_signature.toString().contains(QStringLiteral("|selection:none|layers:0|")) ||
      !carousel_workflow_polish_signature.toString().contains(QStringLiteral("|media-picker:false|media-intent:slot|")) ||
      !carousel_workflow_polish_signature.toString().contains(QStringLiteral("|snapping:true|guides:true|")) ||
      !carousel_workflow_polish_signature.toString().contains(
          QStringLiteral("|preserve:create-board-select-pan-text-media-shape-guides-layers-transform-fit-fill-clear-save-export"))) {
    std::cerr << "Carousel workflow polish: "
              << carousel_workflow_polish_signature.toString().toStdString() << '\n';
    return fail("carousel workspace should expose setup/composer workflow state while preserving board, tool, media, layer, and export actions");
  }
  if (carousel_workspace->property("carouselResponsiveLayoutContract").toString() !=
      QStringLiteral("carousel-responsive-layout:v1|modes:compact-under-1050,standard-1050-to-1719,wide-1720-plus|supported:760x600,1280x720,1920x1080|compact-inspector:right-drawer|standard-inspector:300-scaled|wide-inspector:336-scaled|composer-min:360-460-600|board-strip:112-136-152|header:condensed|empty-card:bounded|state-preserved:true|no-elide:true|no-overlap:true")) {
    return fail("Carousel should publish its compact, standard, and wide layout contract");
  }
  const auto verify_carousel_layout = [&](const int view_width,
                                           const int view_height,
                                           const QString& expected_mode,
                                           const QString& expected_inspector,
                                           const int expected_inspector_width,
                                           const int expected_composer_width,
                                           const int expected_composer_minimum,
                                           const int expected_board_strip_height,
                                           const int expected_artboard_height) {
    QVariant result;
    if (!QMetaObject::invokeMethod(
            carousel_workspace,
            "carouselResponsiveLayoutMetrics",
            Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, view_width),
            Q_ARG(QVariant, view_height))) {
      return false;
    }
    const auto metrics = result.toMap();
    return metrics.value(QStringLiteral("mode")).toString() == expected_mode &&
           metrics.value(QStringLiteral("inspectorPresentation")).toString() ==
               expected_inspector &&
           metrics.value(QStringLiteral("inspectorWidth")).toInt() ==
               expected_inspector_width &&
           metrics.value(QStringLiteral("composerWidth")).toInt() ==
               expected_composer_width &&
           metrics.value(QStringLiteral("composerMinimum")).toInt() ==
               expected_composer_minimum &&
           metrics.value(QStringLiteral("boardStripHeight")).toInt() ==
               expected_board_strip_height &&
           metrics.value(QStringLiteral("artboardHeight")).toInt() ==
               expected_artboard_height &&
           metrics.value(QStringLiteral("horizontalNonOverlap")).toBool() &&
           metrics.value(QStringLiteral("verticalNonOverlap")).toBool() &&
           metrics.value(QStringLiteral("supported")).toBool();
  };
  if (!verify_carousel_layout(
          760, 600, QStringLiteral("compact"), QStringLiteral("drawer"), 0, 700, 360, 112, 424) ||
      !verify_carousel_layout(
          1280, 720, QStringLiteral("standard"), QStringLiteral("inline"), 300, 920, 460, 136, 520) ||
      !verify_carousel_layout(
          1920, 1080, QStringLiteral("wide"), QStringLiteral("inline"), 336, 1524, 600, 152, 864)) {
    return fail("Carousel responsive layout metrics should keep the inspector, composer, artboard, and board strip usable without overlap at every supported desktop size");
  }
  const QList<int> expected_workspace_order = {
      library_workspace_index,
      edit_workspace_index,
      carousel_workspace_index,
      timeline_workspace_index,
      export_workspace_index,
  };
  for (auto number = 1; number <= expected_workspace_order.size(); ++number) {
    QVariant returned_value;
    const auto invoked = QMetaObject::invokeMethod(
        root,
        "switchWorkspaceByNumber",
        Q_RETURN_ARG(QVariant, returned_value),
        Q_ARG(QVariant, number));
    QCoreApplication::processEvents();
    if (!invoked || !returned_value.toBool() ||
        root->property("currentWorkspace").toInt() != expected_workspace_order.at(number - 1)) {
      return fail("global workspace number shortcuts should route across every workspace");
    }
  }
  root->setProperty("currentWorkspace", edit_workspace_index);
  root->setProperty("editWorkspaceMode", QStringLiteral("adjust"));
  process_layout_events(8);
  auto* routed_edit_for_visibility =
      root->findChild<QObject*>(QStringLiteral("editStudioWorkspace"));
  if (routed_edit_for_visibility == nullptr ||
      !QMetaObject::invokeMethod(
          root,
          "pageVisibilityAuditSignature",
          Q_RETURN_ARG(QVariant, page_visibility_audit_signature)) ||
      !page_visibility_audit_signature.toString().contains(QStringLiteral("|active:edit|")) ||
      !page_visibility_audit_signature.toString().contains(QStringLiteral("|live:edit-studio-visibility:v1|")) ||
      !QMetaObject::invokeMethod(
          routed_edit_for_visibility,
          "activeVisibilityContract",
          Q_RETURN_ARG(QVariant, active_visibility_contract)) ||
      !active_visibility_contract.toString().startsWith(QStringLiteral("edit-studio-visibility:v1"))) {
    std::cerr << "Edit page visibility audit: "
              << page_visibility_audit_signature.toString().toStdString() << '\n';
    return fail("App shell visibility audit should report Edit as the active preserved-function page");
  }
  QVariant masks_route_result;
  if (!QMetaObject::invokeMethod(
          root,
          "activatePrimaryEditTool",
          Q_RETURN_ARG(QVariant, masks_route_result),
          Q_ARG(QVariant, QStringLiteral("masks"))) ||
      !masks_route_result.toBool()) {
    return fail("App shell should route to Masks before checking mask visibility audit");
  }
  process_layout_events(8);
  routed_edit_for_visibility =
      root->findChild<QObject*>(QStringLiteral("editStudioWorkspace"));
  if (routed_edit_for_visibility == nullptr ||
      !QMetaObject::invokeMethod(
          root,
          "pageVisibilityAuditSignature",
          Q_RETURN_ARG(QVariant, page_visibility_audit_signature)) ||
      !page_visibility_audit_signature.toString().contains(QStringLiteral("|active:masks|")) ||
      !page_visibility_audit_signature.toString().contains(QStringLiteral("|live:mask-studio-visibility:v1|")) ||
      !QMetaObject::invokeMethod(
          routed_edit_for_visibility,
          "activeVisibilityContract",
          Q_RETURN_ARG(QVariant, active_visibility_contract)) ||
      !active_visibility_contract.toString().startsWith(QStringLiteral("mask-studio-visibility:v1"))) {
    std::cerr << "Masks page visibility audit: "
              << page_visibility_audit_signature.toString().toStdString() << '\n';
    return fail("App shell visibility audit should report Masks as the active preserved-function page when mask tools own the canvas");
  }
  root->setProperty("currentWorkspace", export_workspace_index);
  process_layout_events(8);
  auto* export_workspace = root->findChild<QObject*>(QStringLiteral("exportStudioWorkspace"));
  auto* export_presets_panel = export_workspace != nullptr
      ? export_workspace->findChild<QObject*>(QStringLiteral("exportPresetsPanel"))
      : nullptr;
  auto* export_settings_panel = export_workspace != nullptr
      ? export_workspace->findChild<QObject*>(QStringLiteral("exportSettingsPanel"))
      : nullptr;
  QVariant export_workflow_polish_signature;
  if (!QMetaObject::invokeMethod(
          root,
          "pageVisibilityAuditSignature",
          Q_RETURN_ARG(QVariant, page_visibility_audit_signature)) ||
      !page_visibility_audit_signature.toString().contains(QStringLiteral("|active:export|")) ||
      !page_visibility_audit_signature.toString().contains(QStringLiteral("|live:export-studio-visibility:v1|"))) {
    std::cerr << "Export page visibility audit: "
              << page_visibility_audit_signature.toString().toStdString() << '\n';
    return fail("App shell visibility audit should report Export as the active preserved-function page");
  }
  if (export_workspace == nullptr || export_presets_panel == nullptr || export_settings_panel == nullptr ||
      export_presets_panel->property("title").toString() != QStringLiteral("Export Presets") ||
      export_settings_panel->property("title").toString() != QStringLiteral("Output Settings") ||
      export_workspace->property("exportWorkflowPolishContract").toString() !=
          QStringLiteral("export-workflow-polish:v1|first-glance:presets-assets-queue-settings-primary-action|close-inspection:preset-destination-progress-errors-output-summary|clutter:secondary-management-in-menus-row-actions-contextual|interaction:select-preset-destination-add-queue-start-retry-cancel-clear-reveal|text:no-elide-no-overlap|functionality:preserved") ||
      export_workspace->property("exportPageCompletionContract").toString() !=
          QStringLiteral("page-completion:v1|export|sidebar:presets-destinations|content:selected-assets-queue-progress|settings:preset-output-destination-summary|toolbar:add-assets-start-export-clear-queue|states:empty-ready-running-complete-failed") ||
      !QMetaObject::invokeMethod(
          export_workspace,
          "exportWorkflowPolishSignature",
          Q_RETURN_ARG(QVariant, export_workflow_polish_signature)) ||
      !export_workflow_polish_signature.toString().contains(
          QStringLiteral("export-workflow-polish:v1|page:export|selection:")) ||
      !export_workflow_polish_signature.toString().contains(QStringLiteral("|preset:Web / sRGB / 2048px|format:JPG|")) ||
      !export_workflow_polish_signature.toString().contains(QStringLiteral("|destination:/Volumes/Exports|")) ||
      !export_workflow_polish_signature.toString().contains(QStringLiteral("|presets-panel:true|settings-panel:true|")) ||
      !export_workflow_polish_signature.toString().contains(QStringLiteral("|primary:start-export|")) ||
      !export_workflow_polish_signature.toString().contains(QStringLiteral("|hierarchy:export-action-hierarchy:v2|primary:start-export-split")) ||
      !export_workflow_polish_signature.toString().contains(QStringLiteral("|secondary:queue-actions-menu|")) ||
      !export_workflow_polish_signature.toString().contains(QStringLiteral("|clear:secondary-cleanup|one-primary:true")) ||
      !export_workflow_polish_signature.toString().contains(
          QStringLiteral("|preserve:preset-selection-destination-selected-preview-add-more-queue-start-retry-cancel-remove-clear-preflight-output-summary-reveal"))) {
    std::cerr << "Export workflow polish: "
              << export_workflow_polish_signature.toString().toStdString() << '\n';
    return fail("export panels should use readable labels and expose live preset, destination, queue, settings, primary action, and preserved-action state");
  }
  QVariant failed_export_job_count;
  if (!QMetaObject::invokeMethod(
          export_workspace,
          "failedExportJobCount",
          Q_RETURN_ARG(QVariant, failed_export_job_count)) ||
      failed_export_job_count.toInt() != 0) {
    return fail("Studio export queue management should expose a live failed-export count for retry menu state");
  }
  QVariant export_preset_route_result;
  if (!QMetaObject::invokeMethod(
          root,
          "openEditPresetPanelFromExport",
          Q_RETURN_ARG(QVariant, export_preset_route_result),
          Q_ARG(QVariant, QStringLiteral("manage-saved"))) ||
      !export_preset_route_result.toBool()) {
    return fail("export preset management menu should route to the existing Edit preset panel");
  }
  process_layout_events(8);
  auto* routed_edit_workspace =
      root->findChild<QObject*>(QStringLiteral("editStudioWorkspace"));
  if (root->property("currentWorkspace").toInt() != edit_workspace_index ||
      routed_edit_workspace == nullptr ||
      routed_edit_workspace->property("editInspectorTab").toString() != QStringLiteral("presets") ||
      find_named_object(routed_edit_workspace, QStringLiteral("presetLibraryInspectorGroup")) == nullptr ||
      find_named_object(routed_edit_workspace, QStringLiteral("studioPresetManagementMenu")) == nullptr) {
    return fail("export preset management route should land on the Edit Presets inspector with management controls visible");
  }
  QVariant invalid_workspace_shortcut;
  const auto invalid_invoked = QMetaObject::invokeMethod(
      root,
      "switchWorkspaceByNumber",
      Q_RETURN_ARG(QVariant, invalid_workspace_shortcut),
      Q_ARG(QVariant, expected_workspace_order.size() + 1));
  if (!invalid_invoked || invalid_workspace_shortcut.toBool()) {
    return fail("global workspace shortcut routing should reject unknown workspace numbers");
  }
  root->setProperty("currentWorkspace", edit_workspace_index);
  QCoreApplication::processEvents();
  auto* status_navigation_bar = find_named_object(root, QStringLiteral("appNavigationBar"));
  auto* status_bar = find_named_object(root, QStringLiteral("floatingActivityIndicator"));
  if (status_navigation_bar == nullptr || status_bar == nullptr ||
      status_bar->property("y").toDouble() + 1.0 <
          status_navigation_bar->property("y").toDouble() + status_navigation_bar->property("height").toDouble()) {
    return fail("edit progress should use the shared status line beneath application navigation");
  }
  root->setProperty("currentWorkspace", library_workspace_index);
  QCoreApplication::processEvents();
  if (root->findChild<QObject*>(QStringLiteral("floatingActivityIndicator")) == nullptr) {
    return fail("shared application activity status should persist across workspaces");
  }
  QVariant export_cancel_supported;
  QVariant preview_cancel_supported;
  QVariant running_export_cancel_policy;
  QVariant running_import_cancel_policy;
  QVariant queued_model_cancel_policy;
  QVariant activity_signature;
  QVariant preview_activity_presentation;
  QVariant export_failure_presentation;
  if (!QMetaObject::invokeMethod(
          root,
          "supportsLongOperationCancel",
          Q_RETURN_ARG(QVariant, export_cancel_supported),
          Q_ARG(QVariant, QStringLiteral("asset_export"))) ||
      !export_cancel_supported.toBool() ||
      !QMetaObject::invokeMethod(
          root,
          "supportsLongOperationCancel",
          Q_RETURN_ARG(QVariant, preview_cancel_supported),
          Q_ARG(QVariant, QStringLiteral("preview_render"))) ||
      preview_cancel_supported.toBool() ||
      !QMetaObject::invokeMethod(
          root,
          "longOperationCancellationPolicySignature",
          Q_RETURN_ARG(QVariant, running_export_cancel_policy),
          Q_ARG(QVariant, QStringLiteral("asset_export")),
          Q_ARG(QVariant, QStringLiteral("Running"))) ||
      running_export_cancel_policy.toString() !=
          QStringLiteral("asset_export|Running|cancel:true|mode:cooperative") ||
      !QMetaObject::invokeMethod(
          root,
          "longOperationCancellationPolicySignature",
          Q_RETURN_ARG(QVariant, running_import_cancel_policy),
          Q_ARG(QVariant, QStringLiteral("catalog_import")),
          Q_ARG(QVariant, QStringLiteral("Running"))) ||
      running_import_cancel_policy.toString() !=
          QStringLiteral("catalog_import|Running|cancel:false|mode:unsupported") ||
      !QMetaObject::invokeMethod(
          root,
          "longOperationCancellationPolicySignature",
          Q_RETURN_ARG(QVariant, queued_model_cancel_policy),
          Q_ARG(QVariant, QStringLiteral("ai_model_inference")),
          Q_ARG(QVariant, QStringLiteral("Queued"))) ||
      queued_model_cancel_policy.toString() !=
          QStringLiteral("ai_model_inference|Queued|cancel:true|mode:queued-drop") ||
      !QMetaObject::invokeMethod(
          root,
          "primaryActivityControlSignature",
          Q_RETURN_ARG(QVariant, activity_signature)) ||
      !activity_signature.toString().contains(QStringLiteral("|")) ||
      !QMetaObject::invokeMethod(
          root,
          "activityPresentationSignature",
          Q_RETURN_ARG(QVariant, preview_activity_presentation),
          Q_ARG(QVariant, QStringLiteral("preview_render")),
          Q_ARG(QVariant, -1),
          Q_ARG(QVariant, false),
          Q_ARG(QVariant, 0)) ||
      preview_activity_presentation.toString() !=
          QStringLiteral("preview_render|blocking:false|progress:indeterminate|cancel:false|retry:false") ||
      !QMetaObject::invokeMethod(
          root,
          "activityPresentationSignature",
          Q_RETURN_ARG(QVariant, export_failure_presentation),
          Q_ARG(QVariant, QStringLiteral("asset_export")),
          Q_ARG(QVariant, 2.0),
          Q_ARG(QVariant, true),
          Q_ARG(QVariant, 1)) ||
      export_failure_presentation.toString() !=
          QStringLiteral("asset_export|blocking:true|progress:1.000|cancel:true|retry:true") ||
      root->findChild<QObject*>(QStringLiteral("floatingActivityIndicator")) == nullptr ||
      root->findChild<QObject*>(QStringLiteral("activityCancelButton")) == nullptr) {
    std::cerr << "exportCancelSupported=" << export_cancel_supported.toString().toStdString()
              << " previewCancelSupported=" << preview_cancel_supported.toString().toStdString()
              << " runningExportPolicy=" << running_export_cancel_policy.toString().toStdString()
              << " runningImportPolicy=" << running_import_cancel_policy.toString().toStdString()
              << " queuedModelPolicy=" << queued_model_cancel_policy.toString().toStdString()
              << " activitySignature=" << activity_signature.toString().toStdString()
              << " previewPresentation=" << preview_activity_presentation.toString().toStdString()
              << " exportFailurePresentation=" << export_failure_presentation.toString().toStdString()
              << " floatingActivity="
              << (root->findChild<QObject*>(QStringLiteral("floatingActivityIndicator")) != nullptr)
              << " activityButton="
              << (root->findChild<QObject*>(QStringLiteral("activityCancelButton")) != nullptr)
              << '\n';
    return fail("app should expose visible progress and cancel controls for long operations");
  }
  auto failed_export_job = make_persistent_job(
      QStringLiteral("ui-export-failed"),
      QStringLiteral("asset_export"),
      QStringLiteral("Failed"),
      QStringLiteral("Disk full"));
  if (!photo_editor::desktop::upsertPersistedJobRecord(failed_export_job, nullptr)) {
    return fail("desktop shell smoke should seed a failed export job");
  }
  job_queue.refresh();
  root->setProperty("currentWorkspace", export_workspace_index);
  for (auto iteration = 0; iteration < 8; ++iteration) {
    QCoreApplication::processEvents();
  }
  auto* failure_activity_indicator =
      root->findChild<QObject*>(QStringLiteral("failureActivityIndicator"));
  auto* failure_retry_button = failure_activity_indicator != nullptr
      ? failure_activity_indicator->findChild<QObject*>(QStringLiteral("activityCancelButton"))
      : nullptr;
  if (failure_activity_indicator == nullptr || failure_retry_button == nullptr ||
      !failure_activity_indicator->property("visible").toBool() ||
      failure_retry_button->property("text").toString() != QStringLiteral("Retry") ||
      root->property("primaryFailureKind").toString() != QStringLiteral("asset_export") ||
      !root->property("primaryFailureRetryable").toBool() ||
      !root->property("primaryFailureDetail").toString().contains(QStringLiteral("Disk full"))) {
    return fail("failed export jobs should surface a compact retry status instead of cluttering the canvas");
  }
  QVariant retry_failure_result;
  if (!QMetaObject::invokeMethod(
          root,
          "retryPrimaryFailure",
          Q_RETURN_ARG(QVariant, retry_failure_result)) ||
      !retry_failure_result.toBool()) {
    return fail("failed export retry status should dispatch to the persisted job queue");
  }
  job_queue.refresh();
  QCoreApplication::processEvents();
  if (job_queue.failedJobCountForKind(QStringLiteral("asset_export")) != 0 ||
      job_queue.activeJobCountForKind(QStringLiteral("asset_export")) < 1 ||
      root->property("primaryFailureKind").toString() != QStringLiteral("none")) {
    return fail("retrying a failed export from status UI should requeue the job and clear failure chrome");
  }
  auto* command_palette = root->findChild<QObject*>(QStringLiteral("commandPaletteDialog"));
  QVariant command_coverage_signature;
  QVariant missing_command_labels;
  if (command_palette == nullptr || command_palette->property("commandCount").toInt() < 68 ||
      command_palette->property("requiredCommandCount").toInt() < 27 ||
      command_palette->property("dialogVisibilityContract").toString() !=
          QStringLiteral("dialog-visibility:v1|command-palette|always:search-results-count-shortcuts|contextual:filtered-results-disabled-state-empty-state|one-expansion:all-commands-through-search|hover-focus:row-current-selection|diagnostic:missing-required-commands|preserve:functionality") ||
      command_palette->property("dialogSurfaceContract").toString() !=
          QStringLiteral("dialog-surface:v1|command-palette|search:always|results:dominant|footer:keyboard-help|rows:shared-readable|no-elide:true|no-overlap:true") ||
      command_palette->property("functionPreservationMap").toString() !=
          QStringLiteral("function-preservation:v1|command-search:always|command-run:keyboard-and-row|disabled-reasons:row-state|shortcuts:footer-and-row|missing-required:diagnostic-signature") ||
      !QMetaObject::invokeMethod(
          command_palette,
          "commandCoverageSignature",
          Q_RETURN_ARG(QVariant, command_coverage_signature)) ||
      !command_coverage_signature.toString().startsWith(QStringLiteral("commands:")) ||
      !command_coverage_signature.toString().contains(QStringLiteral("|missing:")) ||
      !command_coverage_signature.toString().endsWith(QStringLiteral("|missing:")) ||
      !QMetaObject::invokeMethod(
          command_palette,
          "missingRequiredCommandLabels",
          Q_RETURN_ARG(QVariant, missing_command_labels)) ||
      !missing_command_labels.toList().isEmpty()) {
    std::cerr << "Command palette coverage: "
              << command_coverage_signature.toString().toStdString() << '\n';
    return fail("command palette should include every required workspace command and primary tool");
  }
  QVariant crop_tool_activated;
  if (!QMetaObject::invokeMethod(
          root,
          "activatePrimaryEditTool",
          Q_RETURN_ARG(QVariant, crop_tool_activated),
          Q_ARG(QVariant, QStringLiteral("crop"))) ||
      !crop_tool_activated.toBool() || root->property("currentWorkspace").toInt() !=
          root->property("editWorkspaceIndex").toInt() ||
      root->property("editWorkspaceMode").toString() != QStringLiteral("crop")) {
    return fail("primary edit tool commands should dispatch through the same workspace activation path");
  }
  QMetaObject::invokeMethod(
      root,
      "activatePrimaryEditTool",
      Q_ARG(QVariant, QStringLiteral("adjust")));
  QVariant workflow_shortcut_signature;
  const auto workflow_shortcut_prefix =
      QStringLiteral("Ctrl+Alt+");
  const auto expected_workflow_shortcut_signature =
      QStringLiteral("adjust:%1A|crop:%1C|masks:%1M|brush:%1B|compare:%1P|export:%1E")
          .arg(workflow_shortcut_prefix);
  if (!QMetaObject::invokeMethod(
          root,
          "coreWorkflowShortcutSignature",
          Q_RETURN_ARG(QVariant, workflow_shortcut_signature)) ||
      workflow_shortcut_signature.toString() != expected_workflow_shortcut_signature) {
    std::cerr << "Core workflow shortcuts: "
              << workflow_shortcut_signature.toString().toStdString() << '\n';
    return fail("core edit, crop, mask, compare, and export workflows should have deterministic shortcuts");
  }
  auto* keyboard_shortcuts = root->findChild<QObject*>(QStringLiteral("keyboardShortcutsDialog"));
  QVariant edited_shortcut;
  QVariant reset_shortcut;
  QVariant library_shortcut_sequence;
  QVariant would_conflict;
  QVariant rejected_conflict;
  QVariant editable_workflow_shortcut_signature;
  QVariant shortcut_dialog_workflow_signature;
  QVariant mask_shortcut_help_signature;
  QVariant brush_shortcut_help_signature;
  const auto editable_shortcut_prefix =
      root->property("macStyle").toBool() ? QStringLiteral("Cmd+Alt+") : QStringLiteral("Ctrl+Alt+");
  const auto expected_editable_workflow_shortcut_signature =
      QStringLiteral("adjust:%1A|crop:%1C|masks:%1M|brush:%1B|compare:%1P|export:%1E")
          .arg(editable_shortcut_prefix);
  if (keyboard_shortcuts == nullptr ||
      keyboard_shortcuts->property("editableShortcutCount").toInt() < 11 ||
      keyboard_shortcuts->property("dialogVisibilityContract").toString() !=
          QStringLiteral("dialog-visibility:v1|keyboard-shortcuts|always:title-search-editable-presets-shortcut-sections-close|contextual:status-conflicts-empty-search-platform-modifier|one-expansion:preset-apply-editable-sequence-reset|hover-focus:preset-buttons-edit-fields-reset-controls|diagnostic:conflict-count-search-results|preserve:functionality") ||
      keyboard_shortcuts->property("dialogSurfaceContract").toString() !=
          QStringLiteral("dialog-surface:v1|keyboard-shortcuts|search:header|editable:top|sections:scrollable|keys:fixed-readable|descriptions:wrap|presets:readable-buttons|no-elide:true|no-overlap:true") ||
      keyboard_shortcuts->property("functionPreservationMap").toString() !=
          QStringLiteral("function-preservation:v2|search-shortcuts:header-field|edit-sequence:inline-field-persisted|reset-sequence:inline-reset|apply-presets:preset-buttons-persisted|conflict-detection:dialog-and-controller|runtime-actions:live-bindings|platform-modifiers:mac-or-control|shortcut-help:section-cards") ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "shortcutDialogWorkflowSignature",
          Q_RETURN_ARG(QVariant, shortcut_dialog_workflow_signature)) ||
      !shortcut_dialog_workflow_signature.toString().startsWith(QStringLiteral("visible:dialog-visibility:v1|keyboard-shortcuts")) ||
      !shortcut_dialog_workflow_signature.toString().contains(QStringLiteral("|surface:dialog-surface:v1|keyboard-shortcuts")) ||
      !shortcut_dialog_workflow_signature.toString().contains(QStringLiteral("|preserve:function-preservation:v2|search-shortcuts:header-field")) ||
      !shortcut_dialog_workflow_signature.toString().contains(QStringLiteral("|conflicts:0|active-preset:app-native")) ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "coreWorkflowShortcutSignature",
          Q_RETURN_ARG(QVariant, editable_workflow_shortcut_signature)) ||
      editable_workflow_shortcut_signature.toString() != expected_editable_workflow_shortcut_signature ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "setShortcutSequence",
          Q_RETURN_ARG(QVariant, edited_shortcut),
          Q_ARG(QVariant, QStringLiteral("command-palette")),
          Q_ARG(QVariant, QStringLiteral("Cmd+Alt+K"))) ||
      !edited_shortcut.toBool() ||
      app_shell.shortcutSequence(QStringLiteral("command-palette"), QString{}) !=
          QStringLiteral("Ctrl+Alt+K") ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "resetShortcutSequence",
           Q_RETURN_ARG(QVariant, reset_shortcut),
           Q_ARG(QVariant, QStringLiteral("command-palette"))) ||
      !reset_shortcut.toBool()) {
    return fail("keyboard shortcuts dialog should expose editable shortcut mappings");
  }
  if (!app_shell.setShortcutOverride(
          QStringLiteral("command-palette"), QStringLiteral("Cmd+Alt+K"))) {
    return fail("legacy Command spelling should remain a valid persisted shortcut override");
  }
  QVariant normalized_legacy_shortcut;
  const auto normalized_legacy_shortcut_ok = QMetaObject::invokeMethod(
      root,
      "configuredShortcut",
      Q_RETURN_ARG(QVariant, normalized_legacy_shortcut),
      Q_ARG(QVariant, QStringLiteral("command-palette")),
      Q_ARG(QVariant, QStringLiteral("Ctrl+K")));
  app_shell.resetShortcutOverride(QStringLiteral("command-palette"));
  if (!normalized_legacy_shortcut_ok ||
      normalized_legacy_shortcut.toString() != QStringLiteral("Ctrl+Alt+K")) {
    return fail("older Cmd shortcut overrides should retain their Command intent under Qt's portable modifier mapping");
  }
  const auto expected_mask_shortcut_help_signature =
      QStringLiteral("create:Ctrl+Shift+M|add-subtract-intersect:Ctrl+Shift+A/S/I|"
                     "invert:Ctrl+Shift+V|overlay:Ctrl+Shift+O|outline:Ctrl+Shift+U|"
                     "layer-mask-target-next:Ctrl+Shift+L/K|"
                     "layer-mask-target-previous:Ctrl+Alt+Shift+L/K|base-image:Ctrl+Shift+B|"
                     "temporary-erase:X-hold|temporary-hide:H-hold|rename:F2");
  if (!QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "maskShortcutHelpSignature",
          Q_RETURN_ARG(QVariant, mask_shortcut_help_signature)) ||
      mask_shortcut_help_signature.toString() != expected_mask_shortcut_help_signature) {
    std::cerr << "Mask shortcut help: "
              << mask_shortcut_help_signature.toString().toStdString() << '\n';
    return fail("keyboard shortcuts dialog should document the active mask, layer target, outline, and temporary brush shortcut contract");
  }
  const auto expected_brush_shortcut_help_signature =
      QStringLiteral("brush-add:B|brush-erase:E|temporary-erase:X-hold|size:[/]|"
                     "hardness:Shift+[/Shift+]|flow:Alt+[/Alt+]|density:Ctrl+[/Ctrl+]|"
                     "opacity:Ctrl+Alt+[/Ctrl+Alt+]");
  if (!QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "brushShortcutHelpSignature",
          Q_RETURN_ARG(QVariant, brush_shortcut_help_signature)) ||
      brush_shortcut_help_signature.toString() != expected_brush_shortcut_help_signature) {
    std::cerr << "Brush shortcut help: "
              << brush_shortcut_help_signature.toString().toStdString() << '\n';
    return fail("keyboard shortcuts dialog should document active brush size, hardness, flow, density, opacity, and temporary erase shortcuts");
  }
  if (!QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "shortcutSequence",
          Q_RETURN_ARG(QVariant, library_shortcut_sequence),
          Q_ARG(QVariant, QStringLiteral("workspace-library"))) ||
      library_shortcut_sequence.toString().trimmed().isEmpty() ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "shortcutWouldConflict",
          Q_RETURN_ARG(QVariant, would_conflict),
          Q_ARG(QVariant, QStringLiteral("workspace-edit")),
          Q_ARG(QVariant, library_shortcut_sequence.toString())) ||
      !would_conflict.toBool() ||
      !QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "setShortcutSequence",
          Q_RETURN_ARG(QVariant, rejected_conflict),
          Q_ARG(QVariant, QStringLiteral("workspace-edit")),
          Q_ARG(QVariant, library_shortcut_sequence.toString())) ||
      rejected_conflict.toBool() ||
      !keyboard_shortcuts->property("shortcutEditorStatus")
           .toString()
           .contains(QStringLiteral("conflicts"))) {
    return fail("keyboard shortcuts dialog should detect and reject conflicting mappings");
  }
  if (keyboard_shortcuts->property("shortcutPresetCount").toInt() < 4) {
    return fail("keyboard shortcuts dialog should expose Lightroom, Photoshop, Capture One, and app-native presets");
  }
  const QString shortcut_presets[] = {
      QStringLiteral("lightroom"),
      QStringLiteral("photoshop"),
      QStringLiteral("capture-one"),
      QStringLiteral("app-native"),
  };
  for (const auto& preset_id : shortcut_presets) {
    QVariant applied_preset;
    QVariant conflict_count;
    if (!QMetaObject::invokeMethod(
            keyboard_shortcuts,
            "applyShortcutPreset",
            Q_RETURN_ARG(QVariant, applied_preset),
            Q_ARG(QVariant, preset_id)) ||
        !applied_preset.toBool() ||
        keyboard_shortcuts->property("activeShortcutPreset").toString() != preset_id ||
        !QMetaObject::invokeMethod(
            keyboard_shortcuts,
            "shortcutConflictCount",
            Q_RETURN_ARG(QVariant, conflict_count)) ||
        conflict_count.toInt() != 0) {
      return fail("keyboard shortcuts dialog should apply named shortcut presets without conflicts");
    }
  }
  keyboard_shortcuts->setProperty("shortcutSearchText", QStringLiteral("timeline"));
  QVariant timeline_shortcut_results;
  if (!QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "shortcutSearchResultCount",
          Q_RETURN_ARG(QVariant, timeline_shortcut_results)) ||
      timeline_shortcut_results.toInt() < 4) {
    return fail("keyboard shortcuts dialog should search shortcut descriptions and mappings");
  }
  keyboard_shortcuts->setProperty("shortcutSearchText", QStringLiteral("zzzz-no-shortcut"));
  QVariant empty_shortcut_results;
  if (!QMetaObject::invokeMethod(
          keyboard_shortcuts,
          "shortcutSearchResultCount",
          Q_RETURN_ARG(QVariant, empty_shortcut_results)) ||
      empty_shortcut_results.toInt() != 0) {
    return fail("keyboard shortcuts dialog should report empty shortcut search results");
  }
  qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  qunsetenv("PHOTO_EDITOR_CACHE_DIR");
  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  editor_preview.setImageProvider(nullptr);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return EXIT_SUCCESS;
