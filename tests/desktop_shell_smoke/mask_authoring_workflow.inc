          if (qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_AUTHORING_WORKFLOW")) {
            auto* authoring_workspace = qobject_cast<QQuickItem*>(edit_workspace);
            if (!authoring_workspace || !authoring_workspace->window()) return fail("Selection authoring needs an actual window");
            const auto authoring_platform = QGuiApplication::platformName();
            const auto initial_foreground = native_window_foreground_sample(authoring_workspace->window());
            const auto native_pointer_input = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER");
            if (authoring_platform == QStringLiteral("cocoa") &&
                (!native_pointer_input || !authoring_workspace->window()->isExposed() || !initial_foreground.value(QStringLiteral("qualified")).toBool()))
              return fail("Native selection authoring requires pointer input in an exposed foreground-qualified Cocoa window");
            const auto before_mode_masks = editor_preview.maskStack();
            const auto before_mode_history = editor_preview.editHistoryEntries();
            const QImage source(pointer_photo);
            const auto initial_alpha = photo_editor::desktop::evaluate_canonical_composite_mask_alpha(
                source, before_mode_masks, 1.0, {1.0, 1.0, 1.0});
            if (!initial_alpha.valid) return fail("authoring proof requires an evaluated initial matte");
            for (const auto& name : {QStringLiteral("maskCombineSubtractButton"), QStringLiteral("maskCombineIntersectButton"),
                                    QStringLiteral("maskCombineAddButton")}) {
              if (!click_item(find_named_item(edit_workspace, name)))
                return fail("combine mode must be selected through its visible production control");
              const auto alpha = photo_editor::desktop::evaluate_canonical_composite_mask_alpha(
                  source, editor_preview.maskStack(), 1.0, {1.0, 1.0, 1.0});
              if (editor_preview.maskStack() != before_mode_masks || editor_preview.editHistoryEntries() != before_mode_history ||
                  !alpha.valid || alpha.alpha_mask != initial_alpha.alpha_mask)
                return fail("choosing a next-gesture mode must not edit current recipe, history or alpha");
            }
            auto* refine_group = find_named_object(edit_workspace, QStringLiteral("creativeSelectionRefineGroup"));
            const bool opened_refine_group = refine_group && !refine_group->property("expanded").toBool();
            if (opened_refine_group && !click_item(find_named_item(edit_workspace, QStringLiteral("creativeSelectionRefineGroupHeader"))))
              return fail("authoring proof must expand Refine edges through its disclosure");
            if (!wait_for_condition([&] {
                  auto* edge = find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeSettingsButton"));
                  return refine_group && refine_group->property("expanded").toBool() &&
                      edge && edge->isVisible() && edge->isEnabled();
                }, 5000) || !click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeSettingsButton")))) {
              auto* edge = find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeSettingsButton"));
              const auto rect = edge ? scene_rect(edge) : QRectF{};
              std::cerr << "authoring-edge expanded=" << (refine_group && refine_group->property("expanded").toBool())
                        << " edge=" << bool(edge) << " visible=" << (edge && item_visible(edge))
                        << " enabled=" << (edge && edge->isEnabled()) << " rect=" << rect.x() << ',' << rect.y()
                        << ',' << rect.width() << ',' << rect.height() << '\n';
              authoring_workspace->window()->grabWindow().save(QDir(visual_proof_dir).filePath(QStringLiteral("selection-edge-failure.png")));
              return fail("authoring proof must open existing selection refinement by pointer");
            }
            editor_preview.setActiveMaskFeather(0.19);
            const auto dirty_selection = editor_preview.maskStack();
            const auto dirty_tool = edit_workspace->property("editTool").toString();
            const auto pending_retouch = edit_workspace->property("pendingRetouchStartMode").toString();
            if (!QMetaObject::invokeMethod(edit_workspace, "handleEditRailMenuAction",
                    Q_ARG(QVariant, QVariant(QStringLiteral("retouch"))), Q_ARG(QVariant, QVariant(QStringLiteral("retouch-remove")))))
              return fail("rail More action route must be invocable for dirty-session ownership proof");
            process_layout_events();
            if (!editor_preview.selectionEditSessionActive() || !editor_preview.selectionEditSessionDirty() ||
                editor_preview.maskStack() != dirty_selection || edit_workspace->property("editTool").toString() != dirty_tool ||
                edit_workspace->property("pendingRetouchStartMode").toString() != pending_retouch)
              return fail("rail More retouch action must preserve the dirty selection and cannot arm a deferred owner switch");
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefinementCancelButton"))) ||
                editor_preview.maskStack() != before_mode_masks || editor_preview.editHistoryEntries() != before_mode_history)
              return fail("session Cancel must restore the exact original selection and history");
            if (opened_refine_group && !click_item(find_named_item(edit_workspace, QStringLiteral("creativeSelectionRefineGroupHeader"))))
              return fail("authoring proof must collapse the same refinement disclosure before layer selection");
            const auto render_state = [&](const photo_editor::desktop::EditRecipeState& state) {
              return photo_editor::desktop::render_flattened_output_file(QStringLiteral("mask-pointer-photo"), pointer_photo,
                  state, QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64);
            };
            const auto baseline_state = photo_editor::desktop::parse_edit_recipe_json(
                photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview));
            const auto baseline_pixels = render_state(baseline_state);
            editor_preview.setActiveMaskExposure(1.4);
            const auto owned_masks = editor_preview.maskStack();
            const auto owned_state = photo_editor::desktop::parse_edit_recipe_json(
                photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview));
            const auto owned_pixels = render_state(owned_state);
            if (baseline_pixels.isNull() || owned_pixels.isNull() || baseline_pixels == owned_pixels)
              return fail("deletion proof must begin with a visible mask-local adjustment");
            edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
            process_layout_events();
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("photoLayerNameLabel"))))
              return fail("deletion proof must select the actual owning layer row");
            auto* workspace_item = qobject_cast<QQuickItem*>(edit_workspace);
            if (!workspace_item || !send_window_key(workspace_item->window(), Qt::Key_Delete, Qt::NoModifier) ||
                !wait_for_condition([&] { return editor_preview.photoLayerCount() == 0 && editor_preview.maskCount() == 0; }, 10000))
              return fail("Delete on the selected layer must remove its exclusive selection sources");
            const auto deleted_state = photo_editor::desktop::parse_edit_recipe_json(
                photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview));
            const auto deleted_pixels = render_state(deleted_state);
            if (deleted_pixels.isNull() || deleted_pixels != baseline_pixels)
              return fail("deleting a masked owner must remove its legacy local effects from rendered output");
            editor_preview.setActiveMaskExposure(2.0);
            if (editor_preview.photoLayerCount() != 0 || editor_preview.maskCount() != 0)
              return fail("editing after owner deletion must not resurrect its mask or adjustment scaffold");
            workspace_item->forceActiveFocus();
            if (!send_window_key(workspace_item->window(), Qt::Key_Z, Qt::ControlModifier) ||
                !wait_for_condition([&] { return editor_preview.maskStack() == owned_masks && editor_preview.photoLayerCount() == 1; }, 10000))
              return fail("main Undo must restore the complete deleted layer and its exact masks");
            const auto undone = photo_editor::desktop::parse_edit_recipe_json(
                photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview));
            if (render_state(undone) != owned_pixels) return fail("Undo must restore exact owned-edit pixels");
            if (!send_window_key(workspace_item->window(), Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier) ||
                !wait_for_condition([&] { return editor_preview.maskCount() == 0 && editor_preview.photoLayerCount() == 0; }, 10000))
              return fail("main Redo must remove the same owner and geometry again");
            const auto redone = photo_editor::desktop::parse_edit_recipe_json(
                photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview));
            if (render_state(redone) != deleted_pixels) return fail("Redo must restore exact post-deletion pixels");
            const auto foreground = native_window_foreground_sample(workspace_item->window());
            if (authoring_platform == QStringLiteral("cocoa") &&
                (!workspace_item->window()->isExposed() || !foreground.value(QStringLiteral("qualified")).toBool()))
              return fail("Selection authoring cannot qualify from a hidden or non-foreground Cocoa window");
            QJsonObject receipt{{QStringLiteral("schema"), QStringLiteral("looksy-selection-authoring-workflow:v1")},
                {QStringLiteral("platform"), authoring_platform}, {QStringLiteral("windowExposed"), workspace_item->window()->isExposed()},
                {QStringLiteral("foreground"), QJsonObject::fromVariantMap(foreground)},
                {QStringLiteral("foregroundInitial"), QJsonObject::fromVariantMap(initial_foreground)},
                {QStringLiteral("nativePointerInput"), native_pointer_input},
                {QStringLiteral("status"), QStringLiteral("passed")}, {QStringLiteral("noDrawOperationUnchanged"), true},
                {QStringLiteral("deleteOwnerRemovesMasks"), true}, {QStringLiteral("noResurrection"), true},
                {QStringLiteral("undoExact"), true}, {QStringLiteral("redoExact"), true},
                {QStringLiteral("sessionCancelExact"), true}};
            receipt.insert(QStringLiteral("railMenuPreservesDirtySession"), true);
            receipt.insert(QStringLiteral("retouchMenuRoute"), QStringLiteral("QML-action"));
            QFile evidence(QDir(visual_proof_dir).filePath(QStringLiteral("selection-authoring-workflow.json")));
            if (!evidence.open(QIODevice::WriteOnly) || evidence.write(QJsonDocument(receipt).toJson()) <= 0 ||
                !baseline_pixels.save(QDir(visual_proof_dir).filePath(QStringLiteral("selection-authoring-baseline.png"))) ||
                !owned_pixels.save(QDir(visual_proof_dir).filePath(QStringLiteral("selection-authoring-owned.png"))) ||
                !deleted_pixels.save(QDir(visual_proof_dir).filePath(QStringLiteral("selection-authoring-deleted.png"))))
              return fail("authoring workflow evidence must be written");
            std::cout << "selection-authoring-workflow=passed; production-pointer+main-shortcut+rendered-pixels\n";
            return EXIT_SUCCESS;
          }
