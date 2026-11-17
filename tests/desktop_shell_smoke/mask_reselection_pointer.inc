          app_shell.completeFirstRunExperience();
          auto* pointer_guide = find_named_object(root, QStringLiteral("firstRunGuideDialog"));
          if (pointer_guide != nullptr && (!QMetaObject::invokeMethod(pointer_guide, "close") ||
              !wait_for_condition([&]() { return !pointer_guide->property("visible").toBool(); })))
            return fail("onboarding must be closed before native mask pointer checks");

          auto* pointer_workspace_item = qobject_cast<QQuickItem*>(edit_workspace);
          auto* pointer_window = pointer_workspace_item ? pointer_workspace_item->window() : nullptr;
          auto* pointer_workspace_host = find_named_object(root, QStringLiteral("editStudioWorkspace"));
          if (!pointer_window || !pointer_workspace_host)
            return fail("mask pointer fixture needs its actual window and loaded workspace host");
          if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")) {
            activate_native_test_window(pointer_window);
            pointer_window->raise();
            pointer_window->requestActivate();
          }
          if (!wait_for_condition([&] {
                return !root->property("workspaceTransitionActive").toBool() &&
                    pointer_workspace_host->property("pageContentReady").toBool() &&
                    pointer_window->isExposed() &&
                    (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER") || pointer_window->isActive());
              }, 10000)) {
            std::cerr << "selection-pointer readiness: transition=" << root->property("workspaceTransitionActive").toBool()
                      << " pageReady=" << pointer_workspace_host->property("pageContentReady").toBool()
                      << " exposed=" << pointer_window->isExposed() << " active=" << pointer_window->isActive() << '\n';
            return fail("native mask gestures must wait for the activated, exposed workspace to finish its transition");
          }

          const auto pointer_photo = runtime_root.filePath(QStringLiteral("mask-pointer-photo.png"));
          const auto pointer_fixture_kind =
              qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_REFINE_CANCEL")
                  ? QStringLiteral("saturated")
                  : QStringLiteral("saturated-small");
          edit_workspace->setProperty("agentFocusedMaskId", QStringLiteral("previous-photo-mask"));
          edit_workspace->setProperty("agentSpatialTargetMode", true);
          if (!write_overlay_visual_fixture(pointer_photo, pointer_fixture_kind))
            return fail("mask pointer proof must create a decodable image");
          editor_preview.loadAsset(QStringLiteral("mask-pointer-photo"), pointer_photo,
                                   QUrl::fromLocalFile(pointer_photo).toString(),
                                   QStringLiteral("Pointer fixture"), {}, {}, {});
          if (!edit_workspace->property("agentFocusedMaskId").toString().isEmpty() ||
              edit_workspace->property("agentSpatialTargetMode").toBool())
            return fail("switching photos must clear previous agent mask focus and point targeting");
          if (!wait_for_condition([&]() { return !editor_preview.processing() &&
                  !editor_preview.displayPreviewSource().isEmpty(); }, 10000))
            return fail("mask pointer fixture must finish its initial preview render");

          const auto pointer_kind = qEnvironmentVariable("PHOTO_EDITOR_MASK_POINTER_KIND", QStringLiteral("radial"));
          const auto first_kind = pointer_kind == QStringLiteral("compound") ? QStringLiteral("radial") : pointer_kind;
          if (pointer_kind == QStringLiteral("generated")) {
            qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", "1");
            if (!QMetaObject::invokeMethod(edit_workspace, "createGeneratedSelectionSource", Q_ARG(QVariant, QVariant(QStringLiteral("subject")))) ||
                !editor_preview.hasActiveMask() ||
                editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value(QStringLiteral("segmentationData")).toString().isEmpty())
              return fail("generated pointer fixture must contain segmentation alpha data");
            std::cout << "generated-mask-provider=test-stub; segmentation-quality-proof=false\n";
          } else if (!QMetaObject::invokeMethod(edit_workspace, "createMaskSource", Q_ARG(QVariant, QVariant(first_kind))))
            return fail("pointer fixture must create its requested mask");
          if (pointer_kind == QStringLiteral("luminance")) {
            editor_preview.setActiveMaskLuminanceMin(0.2);
            editor_preview.setActiveMaskLuminanceMax(0.65);
            editor_preview.setActiveMaskRangeFeather(0.08);
            if (std::abs(editor_preview.activeMaskLuminanceMin() - 0.2) > 1e-6 ||
                std::abs(editor_preview.activeMaskLuminanceMax() - 0.65) > 1e-6)
              return fail("range refinement fixture must retain nondefault luminance bounds");
          }
          if (pointer_kind == QStringLiteral("compound")) {
            editor_preview.addMask(QStringLiteral("linear"));
            const auto sources = editor_preview.maskStack();
            if (sources.size() != 2 || editor_preview.activeMaskIndex() != 1 ||
                sources.at(0).toMap().value(QStringLiteral("maskSetId")) != sources.at(1).toMap().value(QStringLiteral("maskSetId")))
              return fail("compound pointer fixture must have two sources in one mask set");
          }
          if (pointer_kind == QStringLiteral("brush")) {
            editor_preview.beginActiveBrushStroke();
            editor_preview.addBrushPoint(0.3, 0.4);
            editor_preview.addBrushPoint(0.45, 0.5);
            editor_preview.addBrushPoint(0.6, 0.6);
            editor_preview.endActiveBrushStroke();
            if (!wait_for_condition([&]() { return !editor_preview.processing() &&
                    editor_preview.activeMaskCommitted(); }, 10000))
              return fail("brush pointer fixture must finish committing its painted stroke");
          }
          const auto visible_rect = [](QQuickItem* item) {
            if (!item_visible(item) || item->window() == nullptr) return QRectF{};
            auto rect = scene_rect(item).intersected(QRectF(0, 0, item->window()->width(), item->window()->height()));
            for (auto* parent = item->parentItem(); parent != nullptr; parent = parent->parentItem()) {
              if (!parent->isVisible()) return QRectF{};
              if (parent->clip()) rect = rect.intersected(scene_rect(parent));
            }
            return rect;
          };
          qint64 last_dispatch_ns = 0;
          QRectF last_click_rect;
          const auto click_item = [&](QQuickItem* item) {
            if (item == nullptr || !item->isVisible() || item->window() == nullptr) return false;
            const QPointer<QQuickItem> live_item(item);
            auto* window = item->window();
            // Synchronize pending Qt Quick polish/layout before sampling the
            // target geometry, as the working Canvas pointer fixture does.
            if (window->grabWindow().isNull() || !live_item || !live_item->isVisible() ||
                !live_item->isEnabled() || live_item->window() != window) return false;
            // Reveal controls in their existing scroll viewport before native
            // hit testing. Only the test's scroll position changes here; the
            // command still has to receive its production pointer click.
            for (auto* ancestor = live_item->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
              if (ancestor->metaObject()->indexOfProperty("contentY") < 0 ||
                  ancestor->metaObject()->indexOfProperty("contentHeight") < 0) continue;
              const auto bounds = live_item->mapRectToItem(ancestor, live_item->boundingRect());
              const auto delta = bounds.top() < 4 ? bounds.top() - 4 :
                  bounds.bottom() > ancestor->height() - 4 ? bounds.bottom() - ancestor->height() + 4 : 0;
              if (qFuzzyIsNull(delta)) continue;
              const auto maximum = qMax(0.0, ancestor->property("contentHeight").toDouble() - ancestor->height());
              ancestor->setProperty("contentY", qBound(0.0, ancestor->property("contentY").toDouble() + delta, maximum));
              if (window->grabWindow().isNull() || !live_item) return false;
            }
            const auto rect = visible_rect(live_item);
            if (rect.width() < 2 || rect.height() < 2) return false;
            last_click_rect = rect;
            const auto point = rect.center();
            const auto global = QPointF(window->mapToGlobal(point.toPoint()));
            QElapsedTimer dispatch_timer;
            dispatch_timer.start();
            QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &press);
            if (qEnvironmentVariableIsSet("PHOTO_EDITOR_REFINEMENT_RAPID_PAGES") && item->objectName().startsWith(QStringLiteral("maskRefinement"))) {
              auto* grabber = window->mouseGrabberItem();
              std::cerr << "pointer=" << item->objectName().toStdString() << " enabled=" << item->isEnabled()
                        << " pressed=" << item->property("pressed").toBool() << " grabber="
                        << (grabber ? grabber->objectName().toStdString() : "none") << '\n';
            }
            QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &release);
            last_dispatch_ns = dispatch_timer.nsecsElapsed();
            QCoreApplication::processEvents();
            return true;
          };
          const auto reveal_refinement_controls = [&] {
            auto* group = find_named_item(edit_workspace, QStringLiteral("creativeSelectionRefineGroup"));
            if (!group) return false;
            if (group->property("expanded").toBool()) return true;
            auto* header = find_named_item(edit_workspace, QStringLiteral("creativeSelectionRefineGroupHeader"));
            return click_item(header) && wait_for_condition([&] {
              auto* current = find_named_item(edit_workspace, QStringLiteral("creativeSelectionRefineGroup"));
              return current && current->property("expanded").toBool();
            }, 5000);
          };
          if (editor_preview.selectionEditSessionActive()) {
            QPointer<QQuickItem> creation_done;
            const auto done_ready = wait_for_condition([&] {
                  creation_done = find_named_item(edit_workspace, QStringLiteral("maskCreationDoneButton"));
                  return creation_done && creation_done->isEnabled() && !visible_rect(creation_done).isEmpty() &&
                      !root->property("workspaceTransitionActive").toBool() &&
                      pointer_workspace_host->property("pageContentReady").toBool() &&
                      pointer_window->isExposed() &&
                      (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER") || pointer_window->isActive());
                }, 5000);
            const auto done_rect = creation_done ? visible_rect(creation_done) : QRectF{};
            auto* done_hit = pointer_window->contentItem();
            for (int depth = 0; done_hit && depth < 32; ++depth) {
              const auto local = done_hit->mapFromScene(done_rect.center());
              auto* child = done_hit->childAt(local.x(), local.y());
              if (!child || child == done_hit) break;
              done_hit = child;
            }
            const auto done_hit_name = done_hit ? done_hit->objectName() : QStringLiteral("none");
            const auto done_hit_class = done_hit ? QString::fromLatin1(done_hit->metaObject()->className()) : QStringLiteral("none");
            QScopedPointer<QSignalSpy> done_clicks(done_ready ? new QSignalSpy(creation_done.data(), SIGNAL(clicked())) : nullptr);
            const auto done_dispatched = done_ready && click_item(creation_done);
            const auto done_settled = done_dispatched && done_clicks && done_clicks->isValid() && done_clicks->count() == 1 &&
                wait_for_condition([&] { return !editor_preview.selectionEditSessionActive(); }, 10000);
            if (!done_settled) {
              auto* workspace_item = qobject_cast<QQuickItem*>(edit_workspace);
              auto* window = workspace_item ? workspace_item->window() : nullptr;
              auto* focus = window ? window->activeFocusItem() : nullptr;
              std::cerr << "selection-initial-Done ready=" << done_ready << " dispatched=" << done_dispatched
                        << " button=" << bool(creation_done) << " enabled=" << (creation_done && creation_done->isEnabled())
                        << " visible=" << (creation_done && !visible_rect(creation_done).isEmpty())
                        << " clickedSpyValid=" << (done_clicks && done_clicks->isValid())
                        << " clickedCount=" << (done_clicks ? done_clicks->count() : -1)
                        << " transition=" << root->property("workspaceTransitionActive").toBool()
                        << " pageReady=" << pointer_workspace_host->property("pageContentReady").toBool()
                        << " exposed=" << pointer_window->isExposed() << " active=" << pointer_window->isActive()
                        << " readyBounds=" << done_rect.x() << ',' << done_rect.y() << ',' << done_rect.width() << ',' << done_rect.height()
                        << " dispatchedBounds=" << last_click_rect.x() << ',' << last_click_rect.y() << ',' << last_click_rect.width() << ',' << last_click_rect.height()
                        << " hit=" << done_hit_name.toStdString() << ':' << done_hit_class.toStdString()
                        << " tool=" << edit_workspace->property("editTool").toString().toStdString()
                        << " session=" << editor_preview.selectionEditSessionActive()
                        << " dirty=" << editor_preview.selectionEditSessionDirty()
                        << " processing=" << editor_preview.processing()
                        << " activeMask=" << editor_preview.activeMaskIndex()
                        << " focus=" << (focus ? focus->objectName().toStdString() : "none")
                        << " status=" << editor_preview.status().toStdString() << '\n';
              if (!visual_proof_dir.isEmpty() && window) {
                const auto failure_image = window->grabWindow();
                std::cerr << "selection-initial-Done screenshot=" << (!failure_image.isNull() && failure_image.save(
                    QDir(visual_proof_dir).filePath(QStringLiteral("selection-initial-done-failure.png")))) << '\n';
              }
              QVariant diagnostic_finish;
              const auto diagnostic_invoked = QMetaObject::invokeMethod(edit_workspace, "finishSelectionWorkflow",
                  Q_RETURN_ARG(QVariant, diagnostic_finish));
              std::cerr << "selection-initial-Done diagnostic-only finishInvocation=" << diagnostic_invoked
                        << " returned=" << diagnostic_finish.toBool()
                        << " sessionAfter=" << editor_preview.selectionEditSessionActive()
                        << "; direct invocation is not pointer proof\n";
              return fail("pointer fixture must accept its newly created selection through Done");
            }
          }
          const auto expected_component_id = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value(QStringLiteral("id")).toString();
          if (qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_REFINE_CANCEL")) {
            const auto original_masks = editor_preview.maskStack();
            const auto original_history = editor_preview.editHistoryEntries();
            editor_preview.beginCoalescedEdit();
            editor_preview.startSelectionEdgeBrush(false);
            editor_preview.beginActiveBrushStroke();
            // Make the worker long enough for this test to cancel it reliably
            // even while neighboring UI shards are loading the machine.
            for (int sweep = 0; sweep < 16; ++sweep) {
              const auto y = 0.12 + 0.76 * static_cast<double>(sweep) / 15.0;
              for (int step = 0; step <= 32; ++step) {
                const auto progress = static_cast<double>(step) / 32.0;
                const auto x = sweep % 2 == 0
                                   ? 0.1 + 0.8 * progress
                                   : 0.9 - 0.8 * progress;
                editor_preview.addBrushPoint(x, y);
              }
            }
            editor_preview.endActiveBrushStroke();
            QElapsedTimer worker_start_wait;
            worker_start_wait.start();
            bool brush_worker_observed = false;
            while (worker_start_wait.elapsed() < 10000) {
              QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
              if (photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_running(
                      editor_preview)) {
                brush_worker_observed = true;
                break;
              }
              QThread::msleep(1);
            }
            if (!brush_worker_observed) {
              std::cerr << "brush-refine cancellation fixture: kind="
                        << editor_preview.activeMaskKind().toStdString()
                        << " active=" << editor_preview.hasActiveMask()
                        << " stroke=" << editor_preview.brushStrokeActive()
                        << " committed=" << editor_preview.activeMaskCommitted()
                        << " processing=" << editor_preview.processing()
                        << " worker_active="
                        << photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_active(
                               editor_preview)
                        << " worker_running="
                        << photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_running(
                               editor_preview)
                        << " commit_scheduled="
                        << photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_scheduled(
                               editor_preview)
                        << " pending_commits="
                        << photo_editor::desktop::EditorPreviewControllerTestAccess::pending_brush_commit_count(
                               editor_preview)
                        << " status=" << editor_preview.status().toStdString() << '\n';
              return fail("rollback race fixture must observe an active brush raster worker");
            }
            if (!editor_preview.cancelCoalescedEdit()) return fail("refinement transaction must cancel");
            if (!wait_for_condition([&]() {
                  return !photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_running(editor_preview) &&
                         !editor_preview.processing();
                }, 10000)) return fail("canceled brush worker must finish without reviving the edit");
            process_layout_events();
            if (editor_preview.maskStack() != original_masks || editor_preview.editHistoryEntries() != original_history ||
                editor_preview.brushStrokeActive())
              return fail("late brush completion must preserve the canceled refinement's original state");
            std::cout << "mask-refine-cancel-active-worker=passed\n";
          }
          edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
          for (int iteration = 0; iteration < 8; ++iteration) QCoreApplication::processEvents();
          const auto find_thumbnail = [&]() -> QQuickItem* {
            for (const auto* name : {"photoLayerMaskSetThumbnailTargetArea", "photoLayerNestedMaskThumbnailTargetArea", "activeLayerMaskChildThumbnailTargetArea"}) {
              for (auto* candidate : find_named_items(edit_workspace, QString::fromLatin1(name))) {
                const auto rect = visible_rect(candidate);
                if (rect.width() >= 2 && rect.height() >= 2) return candidate;
              }
            }
            return nullptr;
          };
          QPointer<QQuickItem> thumbnail;
          if (!wait_for_condition([&] { thumbnail = find_thumbnail(); return !thumbnail.isNull(); }, 5000) ||
              !click_item(thumbnail) || !editor_preview.hasActiveMask())
            return fail("native mask thumbnail must be visible and selectable by pointer");
          const auto selected_id = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value(QStringLiteral("id")).toString();
          if (selected_id != expected_component_id)
            return fail("first thumbnail click must retain the last edited component, not select the first source");
#include "mask_authoring_workflow.inc"
          const auto hidden_mask_case = qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_POINTER_HIDDEN");
          if (qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_REFINE_UI_CANCEL")) {
            if (!reveal_refinement_controls())
              return fail("refinement controls must expand through their visible group header");
            const auto before_refine = editor_preview.maskStack();
            const QImage alpha_source(pointer_photo);
            const auto evaluate_refinement_alpha = [&]() {
              return photo_editor::desktop::evaluate_canonical_composite_mask_alpha(
                  alpha_source, editor_preview.maskStack(), 1.0, {1.0, 1.0, 1.0});
            };
            const auto original_alpha = evaluate_refinement_alpha();
            if (!original_alpha.valid || original_alpha.alpha_mask.isEmpty())
              return fail("refinement proof must evaluate the original matte");
            auto before_history = editor_preview.editHistoryEntries();
            const auto before_refine_history_index = editor_preview.editHistoryCurrentIndex();
            const auto direct_view_clicked = click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineViewSettingsButton")));
            if (!direct_view_clicked ||
                edit_workspace->property("maskRefinementPage").toString() != QStringLiteral("view") ||
                editor_preview.maskStack() != before_refine || editor_preview.editHistoryEntries() != before_history) {
              std::cerr << "direct-view clicked=" << direct_view_clicked
                        << " tool=" << edit_workspace->property("editTool").toString().toStdString()
                        << " page=" << edit_workspace->property("maskRefinementPage").toString().toStdString()
                        << " active=" << editor_preview.hasActiveMask()
                        << " role=" << editor_preview.activeMaskToolRole().toStdString()
                        << " session=" << editor_preview.selectionEditSessionActive()
                        << " masksStable=" << (editor_preview.maskStack() == before_refine)
                        << " historyStable=" << (editor_preview.editHistoryEntries() == before_history)
                        << " status=" << editor_preview.status().toStdString() << '\n';
              return fail("direct View must open without changing the existing mask or history");
            }
            process_layout_events();
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskFirstHiddenButton"))) ||
                editor_preview.maskPreviewDisplayMode() != QStringLiteral("hidden") ||
                !click_item(find_named_item(edit_workspace, QStringLiteral("maskCreationDoneButton"))) ||
                editor_preview.maskStack() != before_refine || editor_preview.editHistoryEntries() != before_history)
              return fail("view-only refinement must finish without a recipe history operation");
            edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
            process_layout_events();
            if (!click_item(find_thumbnail())) return fail("view-only refinement must preserve reselection");
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeSettingsButton"))))
              return fail("geometric mask must open Edge before explicit brush addition");
            process_layout_events();
            auto* add_brush_page = find_named_item(edit_workspace, QStringLiteral("maskRefinementPaintPageButton"));
            if (add_brush_page == nullptr || add_brush_page->property("text").toString() != QStringLiteral("Add Brush") ||
                editor_preview.maskStack() != before_refine || !click_item(add_brush_page) ||
                editor_preview.activeMaskKind() != QStringLiteral("brush") || editor_preview.maskStack().size() != before_refine.size() + 1)
              return fail("geometric Paint entry must explicitly add one brush component");
            process_layout_events();
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefinementCancelButton"))) ||
                editor_preview.maskStack() != before_refine || editor_preview.editHistoryEntries() != before_history)
              return fail("Cancel must discard an explicitly added brush component");
            edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
            process_layout_events();
            if (!click_item(find_thumbnail())) return fail("explicit brush cancellation must preserve reselection");
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeSettingsButton"))) ||
                !edit_workspace->property("maskRefinementSessionActive").toBool() ||
                edit_workspace->property("maskRefinementPage").toString() != QStringLiteral("edge") ||
                editor_preview.maskStack() != before_refine || editor_preview.editHistoryEntries() != before_history)
              return fail("opening edge settings must retain the existing selection without recipe or history changes");
            editor_preview.setActiveMaskFeather(0.12);
            if (editor_preview.maskStack() == before_refine || editor_preview.maskStack().size() != before_refine.size())
              return fail("direct edge editing must change the existing component without adding another");
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefinementCancelButton"))) ||
                editor_preview.maskStack() != before_refine || editor_preview.editHistoryEntries() != before_history)
              return fail("canceling edited edge refinement must restore the existing selection");
            const auto canceled_edge_alpha = evaluate_refinement_alpha();
            if (!canceled_edge_alpha.valid ||
                canceled_edge_alpha.alpha_mask != original_alpha.alpha_mask)
              return fail("Cancel must restore every evaluated alpha byte after editing the existing edge");
            edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
            process_layout_events();
            if (!click_item(find_thumbnail())) return fail("edge refinement must leave the selection reselectable");
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeSettingsButton"))))
              return fail("direct edge editing must reopen for commit");
            editor_preview.setActiveMaskFeather(0.18);
            editor_preview.setActiveMaskSelectionSmooth(23.0);
            const auto completed_edge = editor_preview.maskStack();
            const auto completed_edge_alpha = evaluate_refinement_alpha();
            if (!completed_edge_alpha.valid || completed_edge_alpha.alpha_mask == original_alpha.alpha_mask)
              return fail("edge edits must change evaluated alpha, not just recipe values");
            if (completed_edge == before_refine || completed_edge.size() != before_refine.size() ||
                !click_item(find_named_item(edit_workspace, QStringLiteral("maskCreationDoneButton"))))
              return fail("Done must accept an existing-component edge edit");
            if (!wait_for_condition([&]() { return !photo_editor::desktop::EditorPreviewControllerTestAccess::edit_transaction_active(editor_preview); }, 10000) ||
                editor_preview.editHistoryEntries().size() != before_history.size() + 1)
              return fail("direct edge Done must commit one history operation");
            editor_preview.undo();
            if (editor_preview.maskStack() != before_refine) return fail("direct edge undo must restore the original matte recipe");
            const auto undone_edge_alpha = evaluate_refinement_alpha();
            if (!undone_edge_alpha.valid || undone_edge_alpha.alpha_mask != original_alpha.alpha_mask)
              return fail("direct edge undo must restore every evaluated alpha byte");
            editor_preview.redo();
            if (editor_preview.maskStack() != completed_edge) return fail("direct edge redo must restore the completed matte recipe");
            const auto redone_edge_alpha = evaluate_refinement_alpha();
            if (!redone_edge_alpha.valid || redone_edge_alpha.alpha_mask != completed_edge_alpha.alpha_mask)
              return fail("direct edge redo must restore every completed alpha byte");
            editor_preview.undo();
            edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
            process_layout_events();
            if (!click_item(find_thumbnail()) || editor_preview.maskStack() != before_refine)
              return fail("direct edge undo must preserve reselection");
            before_history = editor_preview.editHistoryEntries();
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeAddButton"))) ||
                !edit_workspace->property("maskRefinementSessionActive").toBool() || editor_preview.maskStack().size() != before_refine.size() + 1)
              return fail("pointer Add must start a refinement session");
            process_layout_events();
            if (qEnvironmentVariableIsSet("PHOTO_EDITOR_REFINEMENT_PAGES_NARROW")) {
              qobject_cast<QQuickItem*>(edit_workspace)->window()->resize(1280, 900);
              process_layout_events();
            }
            const auto refinement_masks = editor_preview.maskStack();
            for (const auto& page : {QStringLiteral("Edge"), QStringLiteral("Strength"), QStringLiteral("View"), QStringLiteral("Paint")}) {
              if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefinement") + page + QStringLiteral("PageButton"))))
                return fail("refinement pages must be reachable by pointer");
              process_layout_events();
              const auto selected_page = page.toLower();
              for (const auto& control : {std::pair{QStringLiteral("maskFirstBrushSettingsGroup"), QStringLiteral("paint")},
                                          std::pair{QStringLiteral("maskFirstFeatherSlider"), QStringLiteral("edge")},
                                          std::pair{QStringLiteral("maskFirstDensitySlider"), QStringLiteral("strength")},
                                          std::pair{QStringLiteral("maskFirstPreviewModeRow"), QStringLiteral("view")}}) {
                auto* item = find_named_item(edit_workspace, control.first);
                if (item == nullptr || item->isVisible() != (selected_page == control.second))
                  return fail("refinement page must show only its focused controls");
              }
              for (const auto& name : {QStringLiteral("maskRefinementPages"), QStringLiteral("maskRefinementCancelButton")}) {
                auto* item = find_named_item(edit_workspace, name);
                if (item == nullptr || visible_rect(item).width() < item->width() - 1 ||
                    visible_rect(item).height() < item->height() - 1)
                  return fail("refinement navigation and Cancel must remain fully visible");
              }
              if (selected_page == QStringLiteral("paint") || selected_page == QStringLiteral("edge")) {
                const auto primary_name = selected_page == QStringLiteral("paint") ? QStringLiteral("maskFirstBrushSizeSlider") : QStringLiteral("maskFirstFeatherSlider");
                const auto detail_name = selected_page == QStringLiteral("paint") ? QStringLiteral("maskFirstBrushHardnessSlider") : QStringLiteral("maskFirstRefineSmoothSlider");
                auto* primary = find_named_item(edit_workspace, primary_name);
                auto* detail = find_named_item(edit_workspace, detail_name);
                auto* detail_button = find_named_item(edit_workspace, QStringLiteral("maskRefinementDetailPageButton"));
                if (primary == nullptr || detail == nullptr || !primary->isVisible() || detail->isVisible() || !click_item(detail_button))
                  return fail("refinement must start with focused primary settings and expose its detail page");
                process_layout_events();
                if (primary->isVisible() || !detail->isVisible())
                  return fail("refinement detail must replace primary settings");
                QElapsedTimer detail_presentation_timer;
                detail_presentation_timer.start();
                if (!wait_for_condition([&]() { return detail_presentation_timer.elapsed() >= 200; }, 2000))
                  return fail("detail capture must settle after navigation");
                const auto detail_capture = qEnvironmentVariable("PHOTO_EDITOR_REFINEMENT_CAPTURE_PATH");
                if (!detail_capture.isEmpty() && !detail->window()->grabWindow().save(
                        detail_capture + QStringLiteral(".") + selected_page + QStringLiteral("-detail.png")))
                  return fail("refinement detail page must produce a native capture");
                if (!click_item(detail_button)) return fail("refinement detail must provide Back");
                process_layout_events();
                if (!primary->isVisible() || detail->isVisible())
                  return fail("Back must restore focused primary settings");
                if (!click_item(detail_button)) return fail("detail page must reopen");
                process_layout_events();
                if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefinementViewPageButton"))))
                  return fail("detail navigation must allow leaving for View");
                QElapsedTimer primary_presentation_timer;
                primary_presentation_timer.start();
                if (!wait_for_condition([&]() { return primary_presentation_timer.elapsed() >= (qEnvironmentVariableIsSet("PHOTO_EDITOR_REFINEMENT_RAPID_PAGES") ? 0 : 200) &&
                        edit_workspace->property("maskRefinementPage").toString() == QStringLiteral("view"); }, 2000))
                  return fail("View must be presented before the next page-navigation gesture");
                process_layout_events();
                auto* return_page_button = find_named_item(edit_workspace, QStringLiteral("maskRefinement") + page + QStringLiteral("PageButton"));
                const auto return_rect_before = visible_rect(return_page_button);
                if (!click_item(return_page_button))
                  return fail("detail navigation must allow returning to its primary page");
                const auto return_rect_after = visible_rect(return_page_button);
                process_layout_events();
                if (edit_workspace->property("maskRefinementDetailPage").toBool() || !primary->isVisible() || detail->isVisible()) {
                  std::cerr << "returnBefore=" << return_rect_before.x() << ',' << return_rect_before.y() << ',' << return_rect_before.width() << ',' << return_rect_before.height()
                            << " returnAfter=" << return_rect_after.x() << ',' << return_rect_after.y() << ',' << return_rect_after.width() << ',' << return_rect_after.height() << '\n';
                  std::cerr << "page=" << edit_workspace->property("maskRefinementPage").toString().toStdString()
                            << " expected=" << selected_page.toStdString()
                            << " detail=" << edit_workspace->property("maskRefinementDetailPage").toBool()
                            << " primaryVisible=" << primary->isVisible() << " detailVisible=" << detail->isVisible() << '\n';
                  return fail("switching primary pages must reset deeper navigation");
                }
              }
              QElapsedTimer page_presentation_timer;
              page_presentation_timer.start();
              if (!wait_for_condition([&]() { return page_presentation_timer.elapsed() >= 200; }, 2000))
                return fail("refinement page capture must settle after pointer navigation");
              const auto page_capture = qEnvironmentVariable("PHOTO_EDITOR_REFINEMENT_CAPTURE_PATH");
              if (!page_capture.isEmpty() && !qobject_cast<QQuickItem*>(edit_workspace)->window()->grabWindow().save(
                      page_capture + QStringLiteral(".") + selected_page + QStringLiteral(".png")))
                return fail("each refinement page must produce a native capture");
              if (editor_preview.maskStack() != refinement_masks || editor_preview.editHistoryEntries().size() != before_history.size())
                return fail("changing refinement pages must preserve masks and history");
            }
            const auto refinement_capture = qEnvironmentVariable("PHOTO_EDITOR_REFINEMENT_CAPTURE_PATH");
            if (!refinement_capture.isEmpty()) {
              auto* cancel_button = find_named_item(edit_workspace, QStringLiteral("maskRefinementCancelButton"));
              if (cancel_button == nullptr || visible_rect(cancel_button).isEmpty() ||
                  !cancel_button->window()->grabWindow().save(refinement_capture))
                return fail("active refinement controls must be visible in the native capture");
            }
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefinementCancelButton"))) ||
                edit_workspace->property("maskRefinementSessionActive").toBool() || editor_preview.maskStack() != before_refine ||
                editor_preview.editHistoryEntries() != before_history)
              return fail("pointer Cancel must restore the original masks and history");
            const auto canceled_pointer_alpha = evaluate_refinement_alpha();
            if (!canceled_pointer_alpha.valid ||
                canceled_pointer_alpha.alpha_mask != original_alpha.alpha_mask)
              return fail("pointer Cancel must restore every evaluated alpha byte in the rendered mask");
            edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
            process_layout_events();
            if (!click_item(find_thumbnail())) return fail("canceled mask must remain reselectable");
            std::cout << "mask-refine-pointer-cancel=passed\n";
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeAddButton"))))
              return fail("Escape fixture must start refinement by pointer");
            for (int stroke = 0; stroke < 2; ++stroke) {
              const auto previous_stroke = editor_preview.maskStack();
              editor_preview.beginActiveBrushStroke();
              editor_preview.addBrushPoint(0.25 + 0.3 * stroke, 0.3);
              editor_preview.addBrushPoint(0.35 + 0.3 * stroke, 0.5);
              editor_preview.endActiveBrushStroke();
              if (!wait_for_condition([&]() { return editor_preview.activeMaskCommitted() && !editor_preview.processing() &&
                      !photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_running(editor_preview); }, 10000) ||
                  editor_preview.maskStack() == previous_stroke)
                return fail("Escape fixture must rasterize each refinement stroke before canceling");
            }
            auto* escape_workspace = qobject_cast<QQuickItem*>(edit_workspace);
            if (escape_workspace == nullptr) return fail("Escape fixture needs a focusable workspace");
            escape_workspace->forceActiveFocus();
            if (!send_window_key(escape_workspace->window(), Qt::Key_Escape, Qt::NoModifier))
              return fail("refinement must accept the Escape key");
            process_layout_events();
            if (edit_workspace->property("maskRefinementSessionActive").toBool() ||
                editor_preview.maskStack() != before_refine || editor_preview.editHistoryEntries() != before_history)
              return fail("Escape must cancel refinement without committing it");
            const auto canceled_brush_alpha = evaluate_refinement_alpha();
            if (!canceled_brush_alpha.valid || canceled_brush_alpha.alpha_mask != original_alpha.alpha_mask)
              return fail("Escape must restore every original alpha byte after committed brush strokes");
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("layersInspectorTabButton"))) ||
                !wait_for_condition([&] {
                  return edit_workspace->property("editInspectorTab").toString() == QStringLiteral("layers");
                }, 5000) || !click_item(find_thumbnail()))
              return fail("Escape must leave the original mask reselectable through Layers");
            std::cout << "mask-refine-escape-cancel=passed\n";
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineEdgeAddButton"))))
              return fail("pointer Add must restart refinement after Cancel");
            editor_preview.beginActiveBrushStroke();
            editor_preview.addBrushPoint(0.3, 0.4);
            editor_preview.addBrushPoint(0.45, 0.5);
            editor_preview.addBrushPoint(0.6, 0.6);
            editor_preview.endActiveBrushStroke();
            if (!wait_for_condition([&]() { return editor_preview.activeMaskCommitted() && !editor_preview.processing() &&
                    !photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_running(editor_preview); }, 10000))
              return fail("first refinement stroke must commit before the next stroke");
            if (editor_preview.editHistoryEntries().size() != before_history.size())
              return fail("intermediate refinement strokes must not publish separate history entries");
            const auto first_stroke_masks = editor_preview.maskStack();
            editor_preview.beginActiveBrushStroke();
            editor_preview.addBrushPoint(0.7, 0.3);
            editor_preview.addBrushPoint(0.75, 0.45);
            editor_preview.addBrushPoint(0.8, 0.6);
            process_layout_events();
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskCreationDoneButton"))))
              return fail("pointer Done must finish refinement with an active stroke");
            if (!wait_for_condition([&]() { return !editor_preview.processing() &&
                    !photo_editor::desktop::EditorPreviewControllerTestAccess::edit_transaction_active(editor_preview) &&
                    !photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_running(editor_preview); }, 10000))
              return fail("Done must allow the final brush commit to finish");
            if (edit_workspace->property("maskRefinementSessionActive").toBool() ||
                editor_preview.maskStack().size() != before_refine.size() + 1 ||
                editor_preview.editHistoryCurrentIndex() != before_refine_history_index + 1 ||
                editor_preview.editHistoryEntries().size() != before_refine_history_index + 2) {
              std::cerr << "refinement active=" << edit_workspace->property("maskRefinementSessionActive").toBool()
                        << " masks=" << editor_preview.maskStack().size() << " history=" << editor_preview.editHistoryEntries().size()
                        << " before=" << before_history.size() << '\n';
              return fail("Done must retain refinement as exactly one history step");
            }
            const auto finished_masks = editor_preview.maskStack();
            const auto finished_brush_alpha = evaluate_refinement_alpha();
            if (!finished_brush_alpha.valid || finished_brush_alpha.alpha_mask == original_alpha.alpha_mask)
              return fail("committed brush refinement must change evaluated matte alpha");
            if (finished_brush_alpha.size != original_alpha.size ||
                finished_brush_alpha.alpha_mask.size() != original_alpha.alpha_mask.size())
              return fail("brush refinement must preserve matte dimensions");
            // Both stroke paths lie in x=[0.3,0.8], y=[0.3,0.6]. A conservative
            // 0.1 image-relative margin includes the brush footprint and feather.
            QRect outside_change_bounds;
            int outside_change_count = 0;
            for (int y = 0; y < original_alpha.size.height(); ++y) {
              for (int x = 0; x < original_alpha.size.width(); ++x) {
                const auto nx = (static_cast<double>(x) + 0.5) / original_alpha.size.width();
                const auto ny = (static_cast<double>(y) + 0.5) / original_alpha.size.height();
                if (nx >= 0.2 && nx <= 0.9 && ny >= 0.2 && ny <= 0.7) continue;
                const auto offset = y * original_alpha.size.width() + x;
                if (finished_brush_alpha.alpha_mask.at(offset) != original_alpha.alpha_mask.at(offset)) {
                  outside_change_bounds = outside_change_bounds.united(QRect(x, y, 1, 1));
                  ++outside_change_count;
                }
              }
            }
            if (outside_change_count > 0) {
              QVariantList brush_only_masks;
              for (const auto& value : finished_masks) {
                const auto mask = value.toMap();
                std::cerr << "mask kind=" << mask.value(QStringLiteral("kind")).toString().toStdString()
                          << " operation=" << mask.value(QStringLiteral("operation")).toString().toStdString()
                          << " parent=" << mask.value(QStringLiteral("parentId")).toString().toStdString()
                          << " set=" << mask.value(QStringLiteral("maskSetId")).toString().toStdString()
                          << " invert=" << mask.value(QStringLiteral("selectionInvert")).toBool()
                          << " maskInvert=" << mask.value(QStringLiteral("invert")).toBool()
                          << " blend=" << mask.value(QStringLiteral("blendMode")).toString().toStdString()
                          << " opacity=" << mask.value(QStringLiteral("opacity")).toDouble()
                          << " density=" << mask.value(QStringLiteral("density")).toDouble()
                          << " expand=" << mask.value(QStringLiteral("selectionExpand")).toDouble()
                          << " grow=" << mask.value(QStringLiteral("selectionGrow")).toDouble()
                          << " shrink=" << mask.value(QStringLiteral("selectionShrink")).toDouble()
                          << " feather=" << mask.value(QStringLiteral("selectionFeather")).toDouble()
                          << " blur=" << mask.value(QStringLiteral("selectionBlur")).toDouble()
                          << " fill=" << mask.value(QStringLiteral("selectionFillHoles")).toDouble()
                          << " remove=" << mask.value(QStringLiteral("selectionRemoveIslands")).toDouble()
                          << '\n';
                if (mask.value(QStringLiteral("kind")).toString() != QStringLiteral("brush")) continue;
                brush_only_masks.append(mask);
                const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
                const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
                const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
                QRect raster_bounds;
                if (width > 0 && height > 0 && data.size() == width * height) {
                  for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                      if (data.at(y * width + x) != 0) raster_bounds = raster_bounds.united(QRect(x, y, 1, 1));
                }
                std::cerr << "brush raster=" << width << 'x' << height << " bytes=" << data.size()
                          << " bounds=" << raster_bounds.x() << ',' << raster_bounds.y() << ',' << raster_bounds.width() << ',' << raster_bounds.height()
                          << " size=" << mask.value(QStringLiteral("brushSize")).toDouble() << '\n';
              }
              const auto original_mask_id = before_refine.isEmpty()
                  ? QString()
                  : before_refine.constFirst().toMap().value(QStringLiteral("id")).toString();
              for (const auto& value : finished_masks) {
                const auto after_mask = value.toMap();
                if (after_mask.value(QStringLiteral("id")).toString() != original_mask_id) continue;
                const auto before_mask = before_refine.constFirst().toMap();
                for (auto iterator = before_mask.cbegin(); iterator != before_mask.cend(); ++iterator) {
                  if (after_mask.value(iterator.key()) != iterator.value()) {
                    std::cerr << "parent field changed=" << iterator.key().toStdString()
                              << " from=" << iterator.value().toString().toStdString()
                              << " to=" << after_mask.value(iterator.key()).toString().toStdString()
                              << '\n';
                  }
                }
                break;
              }
              std::cerr << "outside alpha pixels=" << outside_change_count << " bounds="
                        << outside_change_bounds.x() << ',' << outside_change_bounds.y() << ','
                        << outside_change_bounds.width() << ',' << outside_change_bounds.height() << '\n';
              if (!outside_change_bounds.isNull()) {
                const auto x = outside_change_bounds.x();
                const auto y = outside_change_bounds.y();
                const auto offset = y * original_alpha.size.width() + x;
                const auto brush_only = photo_editor::desktop::evaluate_canonical_composite_mask_alpha(
                    alpha_source, brush_only_masks, 1.0, {1.0, 1.0, 1.0});
                const auto brush_only_offset = y * brush_only.size.width() + x;
                std::cerr << "first outside pixel=" << x << ',' << y
                          << " original=" << static_cast<int>(original_alpha.alpha_mask.at(offset))
                          << " finished=" << static_cast<int>(finished_brush_alpha.alpha_mask.at(offset))
                          << " brush-only="
                          << (brush_only.valid && brush_only_offset < brush_only.alpha_mask.size()
                                  ? static_cast<int>(brush_only.alpha_mask.at(brush_only_offset))
                                  : -1)
                          << '\n';
              }
              return fail("local brush strokes must preserve alpha outside the conservative stroke region");
            }
            if (finished_masks == first_stroke_masks)
              return fail("second refinement stroke must change the committed mask");
            editor_preview.undo();
            if (editor_preview.maskStack() != before_refine)
              return fail("one undo must remove the complete refinement");
            const auto undone_brush_alpha = evaluate_refinement_alpha();
            if (!undone_brush_alpha.valid || undone_brush_alpha.alpha_mask != original_alpha.alpha_mask)
              return fail("brush undo must restore every original alpha byte");
            editor_preview.redo();
            if (editor_preview.maskStack() != finished_masks)
              return fail("one redo must restore both refinement strokes exactly");
            const auto redone_brush_alpha = evaluate_refinement_alpha();
            if (!redone_brush_alpha.valid || redone_brush_alpha.alpha_mask != finished_brush_alpha.alpha_mask)
              return fail("brush redo must restore every completed alpha byte");
            editor_preview.undo();
            if (editor_preview.maskStack() != before_refine)
              return fail("repeated undo must restore the original mask");
            process_layout_events();
            if (!click_item(find_thumbnail())) return fail("undone refinement must restore the original selectable mask");
            std::cout << "mask-refine-pointer-done-one-undo=passed\n";
          }
          const auto locked_mask_case = qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_POINTER_LOCKED");
          const auto renamed_layer_case = qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_POINTER_RENAMED_LAYER");
          const bool layer_click_away = qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_POINTER_LAYER_AWAY");
          const bool collapsed_group_case = qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_POINTER_COLLAPSED_GROUP");
          const bool needs_legacy_shared_owner =
              layer_click_away || renamed_layer_case || collapsed_group_case;
          QString legacy_owner_layer_id;
          QString legacy_owner_mask_set_id;
          int legacy_owner_layer_index = -1;
          if (needs_legacy_shared_owner) {
            const auto masks_before_owner = editor_preview.maskStack();
            if (!editor_preview.hasActiveMask() || editor_preview.activeMaskIndex() < 0 ||
                editor_preview.activeMaskIndex() >= masks_before_owner.size())
              return fail("legacy layer-away shards need an active selection before creating their explicit owner");
            const auto active_mask = masks_before_owner.at(editor_preview.activeMaskIndex()).toMap();
            legacy_owner_mask_set_id = active_mask.value(QStringLiteral("maskSetId")).toString();
            if (legacy_owner_mask_set_id.isEmpty())
              return fail("legacy layer-away owner must target the current working mask set");

            const auto layers_before_owner = editor_preview.photoLayers();
            const auto layer_count_before_owner = editor_preview.photoLayerCount();
            if (!editor_preview.addAdjustmentLayer(QStringLiteral("Pointer shared owner")))
              return fail("legacy layer-away shards must explicitly create their adjustment owner");
            const auto layers_after_create = editor_preview.photoLayers();
            if (editor_preview.photoLayerCount() != layer_count_before_owner + 1 ||
                layers_after_create.size() != layers_before_owner.size() + 1 ||
                editor_preview.maskStack() != masks_before_owner)
              return fail("creating a legacy owner must add only an independent layer and preserve the mask stack");

            for (int index = 0; index < layers_after_create.size(); ++index) {
              const auto candidate = layers_after_create.at(index).toMap();
              const auto candidate_id = candidate.value(QStringLiteral("id")).toString();
              const auto existed = std::any_of(
                  layers_before_owner.cbegin(), layers_before_owner.cend(),
                  [&](const QVariant& prior) {
                    return prior.toMap().value(QStringLiteral("id")).toString() == candidate_id;
                  });
              if (existed) continue;
              if (!legacy_owner_layer_id.isEmpty())
                return fail("explicit owner creation must add exactly one stable layer identity");
              legacy_owner_layer_id = candidate_id;
              legacy_owner_layer_index = index;
              if (candidate.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment") ||
                  candidate.value(QStringLiteral("selectionScoped")).toBool() ||
                  !candidate.value(QStringLiteral("selectionMaskSetId")).toString().isEmpty())
                return fail("creating the adjustment owner must not automatically attach the active mask");
            }
            if (legacy_owner_layer_id.isEmpty() || legacy_owner_layer_index < 0 ||
                !editor_preview.linkActiveSelectionToPhotoLayer(legacy_owner_layer_index, false))
              return fail("the legacy shared-reference fixture must link the active selection explicitly");

            const auto linked_layers = editor_preview.photoLayers();
            if (legacy_owner_layer_index >= linked_layers.size())
              return fail("explicit mask linking must preserve the owner row");
            const auto linked_owner = linked_layers.at(legacy_owner_layer_index).toMap();
            const auto linked_set_ids = linked_owner.value(QStringLiteral("selectionMaskSetIds")).toList();
            if (linked_owner.value(QStringLiteral("id")).toString() != legacy_owner_layer_id ||
                !linked_owner.value(QStringLiteral("selectionScoped")).toBool() ||
                linked_owner.value(QStringLiteral("selectionMaskSetId")).toString() != legacy_owner_mask_set_id ||
                !linked_set_ids.contains(legacy_owner_mask_set_id) ||
                editor_preview.maskStack() != masks_before_owner ||
                editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap()
                    .value(QStringLiteral("id")).toString() != selected_id)
              return fail("explicit owner linking must target the same mask set and preserve the selected component");
          }
          if (renamed_layer_case) {
            if (editor_preview.photoLayerCount() != 1 ||
                legacy_owner_layer_index < 0 ||
                !editor_preview.renamePhotoLayer(legacy_owner_layer_index, QStringLiteral("Renamed mask owner")))
              return fail("rename fixture must rename the mask's owning layer");
            process_layout_events();
          }
          const auto layer_identities = [&]() {
            QVariantList result;
            for (const auto& entry : editor_preview.photoLayers()) {
              const auto layer = entry.toMap();
              result.append(QVariantMap{{QStringLiteral("id"), layer.value(QStringLiteral("id"))},
                                        {QStringLiteral("name"), layer.value(QStringLiteral("name"))}});
            }
            return result;
          };
          const auto layers_before_selection = layer_identities();
          if (locked_mask_case) editor_preview.setActiveMaskLocked(true);
          if (hidden_mask_case) {
            editor_preview.setActiveMaskEnabled(false);
            editor_preview.setMaskPreviewMode(QStringLiteral("none"));
            process_layout_events();
          }
          const auto masks_before_selection = editor_preview.maskStack();
          const auto history_before_selection = editor_preview.editHistoryEntries();
          const auto history_index_before_selection = editor_preview.editHistoryCurrentIndex();
          qint64 maximum_selection_ms = 0;
          qint64 maximum_dispatch_ns = 0;
          const auto show_pointer_layers = [&] {
            if (edit_workspace->property("editInspectorTab").toString() == QStringLiteral("layers"))
              return true;
            auto* tab = find_named_item(edit_workspace, QStringLiteral("layersInspectorTabButton"));
            return click_item(tab) && wait_for_condition([&] {
              return edit_workspace->property("editInspectorTab").toString() == QStringLiteral("layers");
            }, 5000);
          };
          for (int cycle = 0; cycle < 20; ++cycle) {
            if (!show_pointer_layers())
              return fail("mask reselection must return to Layers through its visible tab");
            auto* base = find_named_item(edit_workspace, layer_click_away
                ? QStringLiteral("photoLayerNameLabel") : QStringLiteral("baseImageLayerRowTargetArea"));
            if (!click_item(base)) return fail("click-away row must be visible for pointer selection");
            QVariant base_scope;
            if (!QMetaObject::invokeMethod(edit_workspace, "selectedEditTargetScope", Q_RETURN_ARG(QVariant, base_scope)) ||
                (layer_click_away ? base_scope.toString() != QStringLiteral("adjustment-layer")
                                  : base_scope.toString() != QStringLiteral("base-image")))
              return fail("pointer click-away must actually select the requested layer target");
            if (!legacy_owner_layer_id.isEmpty()) {
              const auto current_layers = editor_preview.photoLayers();
              const auto owner = std::find_if(current_layers.cbegin(), current_layers.cend(), [&](const QVariant& value) {
                return value.toMap().value(QStringLiteral("id")).toString() == legacy_owner_layer_id;
              });
              if (owner == current_layers.cend() ||
                  owner->toMap().value(QStringLiteral("selectionMaskSetId")).toString() != legacy_owner_mask_set_id)
                return fail("clicking away must preserve the explicit legacy mask owner's stable identity and reference");
            }
            if (!show_pointer_layers())
              return fail("mask reselection must reveal Layers after selecting its owner");
            if (collapsed_group_case) {
              auto* toggle = find_named_item(edit_workspace, QStringLiteral("photoLayerNestedMaskSectionHeader"));
              if (!click_item(toggle)) return fail("linked-mask group must be collapsible by pointer");
              process_layout_events();
              auto* section = find_named_item(edit_workspace, QStringLiteral("photoLayerNestedMaskSection"));
              toggle = find_named_item(edit_workspace, QStringLiteral("photoLayerNestedMaskSectionHeader"));
              if (section == nullptr || section->property("expanded").toBool())
                return fail("pointer collapse must hide the nested mask group");
              if (!click_item(toggle)) return fail("collapsed linked-mask group must reopen by pointer");
              process_layout_events();
              section = find_named_item(edit_workspace, QStringLiteral("photoLayerNestedMaskSection"));
              if (section == nullptr || !section->property("expanded").toBool())
                return fail("pointer expand must restore the nested mask group");
            }
            thumbnail = find_thumbnail();
            QElapsedTimer selection_timer;
            selection_timer.start();
            const auto click_dispatched = click_item(thumbnail);
            const auto selection_restored = click_dispatched && wait_for_condition([&]() {
              return editor_preview.hasActiveMask() &&
                  editor_preview.activeMaskIndex() >= 0 &&
                  editor_preview.activeMaskIndex() < editor_preview.maskStack().size() &&
                  editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap()
                      .value(QStringLiteral("id")).toString() == selected_id;
            }, 1000);
            if (!selection_restored) {
              const auto actual_id = editor_preview.hasActiveMask()
                  ? editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap()
                        .value(QStringLiteral("id")).toString()
                  : QString{};
              std::cerr << "cycle=" << cycle << " thumbnail=" << (thumbnail ? thumbnail->objectName().toStdString() : "destroyed")
                        << " visible=" << (thumbnail && thumbnail->isVisible()) << " active=" << editor_preview.activeMaskIndex()
                        << " click_dispatched=" << click_dispatched
                        << " expected_id=" << selected_id.toStdString() << " actual_id=" << actual_id.toStdString()
                        << " mask_count=" << editor_preview.maskStack().size() << '\n';
              const auto failure_capture = qEnvironmentVariable("PHOTO_EDITOR_MASK_POINTER_CAPTURE_PATH");
              auto* workspace_item = qobject_cast<QQuickItem*>(edit_workspace);
              if (!failure_capture.isEmpty() && workspace_item != nullptr && workspace_item->window() != nullptr)
                workspace_item->window()->grabWindow().save(failure_capture);
              return fail("native pointer reselection changed the selected component identity");
            }
            maximum_selection_ms = std::max(maximum_selection_ms, selection_timer.elapsed());
            maximum_dispatch_ns = std::max(maximum_dispatch_ns, last_dispatch_ns);
          }
          std::cout << "mask pointer cycles=20 component=" << selected_id.toStdString()
                    << " kind=" << pointer_kind.toStdString()
                    << " maximum-controller-selection-ms=" << maximum_selection_ms
                    << " maximum-event-dispatch-ms=" << static_cast<double>(maximum_dispatch_ns) / 1000000.0 << '\n';
          if (editor_preview.maskStack() != masks_before_selection ||
              layer_identities() != layers_before_selection ||
              editor_preview.editHistoryEntries() != history_before_selection ||
              editor_preview.editHistoryCurrentIndex() != history_index_before_selection)
            return fail("pointer selection alone must preserve mask data and edit history");
          if (hidden_mask_case && (editor_preview.activeMaskEnabled() ||
              editor_preview.maskPreviewMode() != QStringLiteral("none")))
            return fail("reselecting a hidden mask must preserve its disabled effect and overlay preference");
          if (!editor_preview.previewFailureStage().isEmpty())
            return fail("mask pointer fixture must render without a preview failure");
          if (qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_LASSO_SWITCH")) {
            auto* viewport = find_named_item(edit_workspace, QStringLiteral("editImageViewport"));
            if (!viewport || !viewport->window()) return fail("selection gestures need the actual Studio viewport");
            const auto send_pointer = [&](QEvent::Type type, double nx, double ny) {
              const QPointF local(viewport->property("paintedContentX").toDouble() + nx * viewport->property("paintedContentWidth").toDouble(),
                  viewport->property("paintedContentY").toDouble() + ny * viewport->property("paintedContentHeight").toDouble());
              const auto scene = viewport->mapToScene(local);
              const auto global = QPointF(viewport->window()->mapToGlobal(scene.toPoint()));
              QMouseEvent event(type, scene, global, type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                  type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
              QCoreApplication::sendEvent(viewport->window(), &event);
              QCoreApplication::processEvents();
            };
            auto lasso_cycles = qEnvironmentVariableIntValue("PHOTO_EDITOR_MASK_LASSO_CYCLES");
            if (lasso_cycles <= 0) lasso_cycles = 8;
            if (lasso_cycles > 100) return fail("selection-switch stress cycles must be between 1 and 100");
            for (int cycle = 0; cycle < lasso_cycles; ++cycle) {
              QElapsedTimer cycle_timer;
              cycle_timer.start();
              if (!QMetaObject::invokeMethod(edit_workspace, "startMaskBrushSource", Q_ARG(QVariant, QVariant("add"))))
                return fail("smart selection must accept brush extension");
              const auto brush_id = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value("id").toString();
              send_pointer(QEvent::MouseButtonPress, 0.3, 0.35);
              send_pointer(QEvent::MouseMove, 0.45, 0.45);
              send_pointer(QEvent::MouseButtonRelease, 0.5, 0.45);
              if (editor_preview.brushStrokeActive()) return fail("released selection brush must finish");
              if (!wait_for_condition([&]() {
                    return !photo_editor::desktop::EditorPreviewControllerTestAccess::brush_commit_running(editor_preview) &&
                           !editor_preview.processing();
                  }, 10000))
                return fail("brush refinement must settle before the selection can be reselected");
              edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
              process_layout_events();
              if (!click_item(find_named_item(edit_workspace, QStringLiteral("baseImageLayerRowTargetArea"))))
                return fail("extended selection must survive clicking away to the base image");
              QPointer<QQuickItem> extension_thumbnail;
              const auto extension_thumbnail_ready = wait_for_condition([&]() {
                extension_thumbnail = find_thumbnail();
                return !extension_thumbnail.isNull();
              }, 1000);
              if (!extension_thumbnail_ready)
                return fail("committed brush selection must remain visible in the layer panel");
              const auto extension_click_dispatched = click_item(extension_thumbnail);
              const auto extension_reselected = extension_click_dispatched && wait_for_condition([&]() {
                return editor_preview.hasActiveMask() &&
                    editor_preview.activeMaskIndex() >= 0 &&
                    editor_preview.activeMaskIndex() < editor_preview.maskStack().size() &&
                    editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap()
                        .value(QStringLiteral("id")).toString() == brush_id;
              }, 1000);
              if (!extension_reselected) {
                const auto actual_id = editor_preview.hasActiveMask() &&
                        editor_preview.activeMaskIndex() >= 0 &&
                        editor_preview.activeMaskIndex() < editor_preview.maskStack().size()
                    ? editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap()
                          .value(QStringLiteral("id")).toString()
                    : QString{};
                std::cerr << "lasso-cycle=" << cycle << " extension-thumbnail="
                          << (extension_thumbnail ? extension_thumbnail->objectName().toStdString() : "destroyed")
                          << " click_dispatched=" << extension_click_dispatched
                          << " active=" << editor_preview.activeMaskIndex()
                          << " expected_id=" << selected_id.toStdString()
                          << " actual_id=" << actual_id.toStdString()
                          << " mask_count=" << editor_preview.maskStack().size() << '\n';
                for (const auto* target_name : {"photoLayerMaskSetThumbnailTargetArea",
                                                "photoLayerNestedMaskThumbnailTargetArea",
                                                "activeLayerMaskChildThumbnailTargetArea"}) {
                  const auto targets = find_named_items(edit_workspace, QString::fromLatin1(target_name));
                  std::cerr << "target=" << target_name << " count=" << targets.size();
                  for (auto* target : targets) {
                    const auto rect = visible_rect(target);
                    std::cerr << " [visible=" << target->isVisible() << " rect="
                              << rect.x() << ',' << rect.y() << ',' << rect.width() << ',' << rect.height() << ']';
                  }
                  std::cerr << '\n';
                }
                for (const auto& mask_value : editor_preview.maskStack()) {
                  const auto mask = mask_value.toMap();
                  std::cerr << "mask id=" << mask.value(QStringLiteral("id")).toString().toStdString()
                            << " kind=" << mask.value(QStringLiteral("kind")).toString().toStdString()
                            << " set=" << mask.value(QStringLiteral("maskSetId")).toString().toStdString()
                            << " name=" << mask.value(QStringLiteral("name")).toString().toStdString() << '\n';
                }
                const auto failure_capture = qEnvironmentVariable("PHOTO_EDITOR_MASK_POINTER_CAPTURE_PATH");
                auto* workspace_item = qobject_cast<QQuickItem*>(edit_workspace);
                if (!failure_capture.isEmpty() && workspace_item != nullptr && workspace_item->window() != nullptr)
                  workspace_item->window()->grabWindow().save(failure_capture);
                return fail("extended selection must survive clicking away and reselecting its stable component");
              }
              if (!QMetaObject::invokeMethod(edit_workspace, "startMaskLassoFromShortcut") ||
                  !edit_workspace->property("lassoDrawArmed").toBool()) return fail("lasso must arm after brush reselection");
              process_layout_events();
              send_pointer(QEvent::MouseButtonPress, 0.65, 0.5);
              for (int step = 1; step <= 48; ++step) {
                const auto angle = step * 6.283185307179586 / 48;
                send_pointer(QEvent::MouseMove, 0.5 + 0.15 * std::cos(angle), 0.5 + 0.15 * std::sin(angle));
              }
              send_pointer(QEvent::MouseButtonRelease, 0.65, 0.5);
              if (!editor_preview.hasActiveMask() || editor_preview.activeMaskKind() != "lasso" ||
                  editor_preview.activeMaskPolygonPointCount() < 24 ||
                  !editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value("lassoClosed").toBool() ||
                  edit_workspace->property("lassoDrawArmed").toBool() ||
                  edit_workspace->property("maskShapeEditTransactionActive").toBool())
                return fail("native lasso drag must retain detail, close, and release its undo transaction");
              if (!wait_for_condition([&]() {
                    for (const auto& value : editor_preview.maskStack()) {
                      const auto mask = value.toMap();
                      if (mask.value("id").toString() == brush_id)
                        return !mask.value("brushStrokes").toList().isEmpty();
                    }
                    return false;
                  }, 10000)) return fail("switching to lasso must preserve the released brush extension");
              const auto closed = editor_preview.maskStack();
              send_pointer(QEvent::MouseButtonPress, 0.3, 0.5);
              send_pointer(QEvent::MouseMove, 0.4, 0.6);
              send_pointer(QEvent::MouseButtonRelease, 0.4, 0.6);
              if (editor_preview.maskStack() != closed) return fail("completed lasso must ignore stray drags");
              if (editor_preview.selectionEditSessionActive()) {
                if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskCreationDoneButton"))) ||
                    !wait_for_condition([&] {
                      return !editor_preview.selectionEditSessionActive() && !editor_preview.processing();
                    }, 15000))
                  return fail("brush and lasso authoring must finish through Done before document undo");
              }
              // Keep stress iterations independent: first prove the accepted
              // brush/lasso edits, then undo both so contour complexity does
              // not grow with the iteration count.
              if (cycle + 1 < lasso_cycles) {
                int undo_count = 0;
                while (editor_preview.editHistoryCurrentIndex() > history_index_before_selection && undo_count < 10) {
                  editor_preview.undo();
                  ++undo_count;
                }
                process_layout_events();
                if (undo_count == 0 || editor_preview.editHistoryCurrentIndex() != history_index_before_selection ||
                    editor_preview.maskStack() != masks_before_selection ||
                    !editor_preview.hasActiveMask() ||
                    editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap()
                        .value(QStringLiteral("id")).toString() != selected_id)
                  return fail("stress iteration undo must restore the original selected mask exactly");
              }
              std::cout << "selection-switch-cycle=" << (cycle + 1)
                        << " elapsed-ms=" << cycle_timer.elapsed() << std::endl;
            }
            const auto retained_lasso_index = editor_preview.activeMaskIndex();
            editor_preview.setActiveMaskIndex(-1);
            if (!QMetaObject::invokeMethod(edit_workspace, "createMaskSource", Q_ARG(QVariant, QVariant("object"))))
              return fail("Smart Select must arm without an existing active mask");
            send_pointer(QEvent::MouseButtonPress, 0.3, 0.4);
            if (!editor_preview.smartSelectStrokeActive())
              return fail("Smart Select must receive its first press without an active mask");
            const auto initial_prompt = editor_preview.smartSelectStrokePromptPayload();
            send_pointer(QEvent::MouseMove, 0.6, 0.6);
            if (editor_preview.smartSelectStrokePromptPayload() == initial_prompt)
              return fail("Smart Select must collect drag input before a mask exists");
            editor_preview.resetSmartSelectStroke();
            viewport->setProperty("maskGestureKind", QString());
            send_pointer(QEvent::MouseButtonRelease, 0.6, 0.6);
            QMetaObject::invokeMethod(edit_workspace, "clearTransientMaskTools");
            editor_preview.setActiveMaskIndex(retained_lasso_index);
            process_layout_events();
            auto* lasso_hud = find_named_item(edit_workspace, QStringLiteral("polygonToolHud"));
            auto* selection_chip = find_named_item(edit_workspace, QStringLiteral("activeCanvasToolChip"));
            if (!lasso_hud || !selection_chip || scene_rect(lasso_hud).intersects(scene_rect(selection_chip)))
              return fail("lasso actions and selection completion controls must not overlap");
            const auto lasso_capture = qEnvironmentVariable("PHOTO_EDITOR_MASK_LASSO_CAPTURE_PATH");
            if (!lasso_capture.isEmpty() && !viewport->window()->grabWindow().save(lasso_capture))
              return fail("selection switch proof must save its visible canvas");
            std::cout << "smart-selection-brush-reselection-lasso-pointer-cycles=" << lasso_cycles
                      << "; lasso-detail=passed\n";
            return 0;
          }
          if (pointer_kind == QStringLiteral("compound")) {
            const auto reselect_at_index = [&](int expected_index) {
              process_layout_events();
              if (editor_preview.maskStack().at(expected_index).toMap().value(QStringLiteral("id")).toString() != selected_id)
                return false;
              if (!click_item(find_named_item(edit_workspace, QStringLiteral("baseImageLayerRowTargetArea"))))
                return false;
              return click_item(find_thumbnail()) && editor_preview.activeMaskIndex() == expected_index &&
                     editor_preview.maskStack().at(expected_index).toMap().value(QStringLiteral("id")).toString() == selected_id;
            };
            editor_preview.moveActiveMask(-1);
            if (!reselect_at_index(0)) return fail("reordered component must reselect by stable ID");
            editor_preview.undo();
            if (!reselect_at_index(1)) return fail("undo must restore component order and stable reselection");
            editor_preview.redo();
            if (!reselect_at_index(0)) return fail("redo must preserve stable component reselection");
            std::cout << "mask-pointer-component-reorder-undo-redo=passed\n";
          }
          if (qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_POINTER_ASSET_RETURN")) {
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("baseImageLayerRowTargetArea"))))
              return fail("asset-return fixture must deselect the mask by pointer");
            QFile saved_recipe(runtime_root.filePath(QStringLiteral("mask-pointer-recipe.json")));
            const auto recipe = photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview).toUtf8();
            if (!saved_recipe.open(QIODevice::WriteOnly) || saved_recipe.write(recipe) != recipe.size())
              return fail("asset-return fixture must save its recipe");
            saved_recipe.close();
            if (!saved_recipe.open(QIODevice::ReadOnly)) return fail("saved mask recipe must reopen");
            const auto restored_recipe = QString::fromUtf8(saved_recipe.readAll());
            saved_recipe.close();
            editor_preview.loadAsset(QStringLiteral("mask-pointer-other-photo"), pointer_photo,
                                     QUrl::fromLocalFile(pointer_photo).toString(), {}, {}, {}, restored_recipe);
            if (!wait_for_condition([&]() { return !editor_preview.processing(); }, 10000) ||
                editor_preview.maskStack().size() != 2)
              return fail("second photo must load the same compound mask IDs");
            editor_preview.setActiveMaskIndex(1);
            const auto other_component_id = editor_preview.maskStack().at(1).toMap().value(QStringLiteral("id")).toString();
            if (other_component_id == selected_id)
              return fail("asset-isolation fixture must choose different components on each photo");
            process_layout_events();
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("baseImageLayerRowTargetArea"))) ||
                !click_item(find_thumbnail()) || editor_preview.activeMaskIndex() != 1)
              return fail("second photo must reselect its own component through its thumbnail");
            editor_preview.loadAsset(QStringLiteral("mask-pointer-photo"), pointer_photo,
                                     QUrl::fromLocalFile(pointer_photo).toString(), {}, {}, {}, restored_recipe);
            if (!wait_for_condition([&]() { return !editor_preview.processing(); }, 10000))
              return fail("returned photo must finish rendering");
            thumbnail = find_thumbnail();
            if (!click_item(thumbnail) || !editor_preview.hasActiveMask() ||
                editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value(QStringLiteral("id")).toString() != selected_id)
              return fail("pointer reselection after asset return must retain the last component");
            std::cout << "mask-pointer-asset-return=passed; recipe-roundtrip=disk; colliding-asset-mask-ids=passed\n";
          }
          if (locked_mask_case) {
            if (!reveal_refinement_controls())
              return fail("locked mask preview controls must expand through their visible group header");
            const auto locked_stack = editor_preview.maskStack();
            const auto locked_history = editor_preview.editHistoryEntries();
            editor_preview.setActiveMaskDensity(0.31);
            if (!editor_preview.activeMaskLocked() || editor_preview.activeMaskEditDisabledReason().isEmpty() ||
                editor_preview.maskStack() != locked_stack || editor_preview.editHistoryEntries() != locked_history)
              return fail("locked mask must remain selectable, explain editing unavailability, and reject density edits");
            process_layout_events();
            auto* lock_reason = find_named_item(edit_workspace, QStringLiteral("selectedMaskLockReason"));
            if (!item_visible(lock_reason) || lock_reason->property("text").toString() != editor_preview.activeMaskEditDisabledReason())
              return fail("selected locked mask must visibly explain its edit restriction");
            const auto lock_capture = qEnvironmentVariable("PHOTO_EDITOR_MASK_LOCK_CAPTURE_PATH");
            if (!lock_capture.isEmpty() && !lock_reason->window()->grabWindow().save(lock_capture))
              return fail("locked selection proof must save its screenshot");
            const auto locked_view_clicked = click_item(find_named_item(edit_workspace, QStringLiteral("maskRefineViewSettingsButton")));
            if (!locked_view_clicked ||
                edit_workspace->property("maskRefinementPage").toString() != QStringLiteral("view")) {
              std::cerr << "locked-view clicked=" << locked_view_clicked
                        << " tool=" << edit_workspace->property("editTool").toString().toStdString()
                        << " page=" << edit_workspace->property("maskRefinementPage").toString().toStdString()
                        << " active=" << editor_preview.hasActiveMask()
                        << " role=" << editor_preview.activeMaskToolRole().toStdString()
                        << " session=" << editor_preview.selectionEditSessionActive()
                        << " status=" << editor_preview.status().toStdString() << '\n';
              return fail("locked masks must still allow View inspection");
            }
            process_layout_events();
            for (const auto& name : {QStringLiteral("maskRefinementPaintPageButton"), QStringLiteral("maskRefinementEdgePageButton"),
                                     QStringLiteral("maskRefinementStrengthPageButton"), QStringLiteral("maskFirstVisibilityButton"), QStringLiteral("maskFirstInvertButton")}) {
              auto* control = find_named_item(edit_workspace, name);
              if (control == nullptr || control->isEnabled()) return fail("locked View must keep matte-changing controls disabled");
            }
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskFirstHiddenButton"))) ||
                editor_preview.maskPreviewDisplayMode() != QStringLiteral("hidden") ||
                !click_item(find_named_item(edit_workspace, QStringLiteral("maskCreationDoneButton"))) ||
                !editor_preview.activeMaskLocked() || editor_preview.maskStack() != locked_stack || editor_preview.editHistoryEntries() != locked_history)
              return fail("locked View inspection must preserve the lock, mask and history");
            edit_workspace->setProperty("editInspectorTab", QStringLiteral("layers"));
            process_layout_events();
            if (!click_item(find_thumbnail())) return fail("locked View must preserve mask reselection");
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("selectedMaskUnlockButton"))) || editor_preview.activeMaskLocked())
              return fail("visible Unlock action must unlock the selected mask by pointer");
            std::cout << "locked-mask-reselection-and-write-guard=passed\n";
          }
          editor_preview.setActiveMaskDensity(0.63);
          if (std::abs(editor_preview.activeMaskDensity() - 0.63) > 1e-6 ||
              editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value(QStringLiteral("id")).toString() != selected_id)
            return fail("density edit after pointer selection must reach the selected stable component");
          const auto name = QStringLiteral("Pointer reselected mask");
          editor_preview.setActiveMaskName(name);
          const auto edited = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap();
          if (edited.value(QStringLiteral("id")).toString() != selected_id || edited.value(QStringLiteral("name")).toString() != name)
            return fail("edit after native pointer reselection must modify the selected stable ID");
          const auto capture_path = qEnvironmentVariable("PHOTO_EDITOR_MASK_POINTER_CAPTURE_PATH");
          if (!wait_for_condition([&]() { return !editor_preview.processing(); }, 10000))
            return fail("mask edit must finish rendering before its proof capture");
          process_layout_events();
          auto* capture_workspace = qobject_cast<QQuickItem*>(edit_workspace);
          if (qEnvironmentVariableIsSet("PHOTO_EDITOR_FONT_ENTRY_PROOF")) {
            if (!editor_preview.addTextLayer(QStringLiteral("Font proof"), QStringLiteral("Font proof")))
              return fail("font proof must create a text layer");
            const auto text_index = 0;
            if (!QMetaObject::invokeMethod(edit_workspace, "setActivePhotoLayerSelection",
                  Q_ARG(QVariant, QVariant(text_index)), Q_ARG(QVariant, QVariant(true))))
              return fail("font proof must select the text layer");
            process_layout_events();
            auto* panel = find_named_object(edit_workspace, QStringLiteral("photoLayerPropertiesPanel"));
            if (panel == nullptr || !panel->setProperty("detailsExpanded", true)) return fail("font details must expand");
            process_layout_events();
            auto* field = find_named_item(edit_workspace, QStringLiteral("photoLayerTextFontField"));
            if (field == nullptr) return fail("editable font picker must exist");
            for (auto* parent = field->parentItem(); parent != nullptr; parent = parent->parentItem()) {
              if (parent->property("contentY").isValid()) {
                parent->setProperty("contentY", std::max(0.0, parent->property("contentY").toDouble() +
                    scene_rect(field).top() - scene_rect(parent).top() - 40.0));
                process_layout_events();
              }
            }
            const auto fonts = editor_preview.availablePhotoTextFonts();
            if (fonts.isEmpty() || !click_item(field)) return fail("font picker must accept a visible pointer click");
            const auto desired_font = fonts.first();
            auto* window = field->window();
            if (!send_window_key(window, Qt::Key_A, QKeySequence(QKeySequence::SelectAll)[0].keyboardModifiers())) return fail("font input must select its text");
            for (const auto character : desired_font) {
              QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(character));
              QCoreApplication::sendEvent(window, &press);
              QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, QString(character));
              QCoreApplication::sendEvent(window, &release);
            }
            if (!send_window_key(window, Qt::Key_Return, Qt::NoModifier)) return fail("typed font must be accepted");
            process_layout_events();
            if (editor_preview.photoLayers().at(text_index).toMap().value(QStringLiteral("fontFamily")).toString() != desired_font)
              return fail("typed font picker value must reach the text layer");
            std::cout << "editable-font-pointer-and-keyboard=passed\n";
          }
          if (qEnvironmentVariableIsSet("PHOTO_EDITOR_MASK_REFINE_NARROW")) {
            if (capture_workspace == nullptr || capture_workspace->window() == nullptr)
              return fail("refinement layout proof needs a native window");
            auto* window = capture_workspace->window();
            const auto recipe_before_resize =
                photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview);
            const auto masks_before_resize = editor_preview.maskStack();
            const auto history_before_resize = editor_preview.editHistoryEntries();
            const auto history_index_before_resize = editor_preview.editHistoryCurrentIndex();
            const auto source_asset_before_resize = editor_preview.assetId();
            const QSize source_size_before_resize(
                editor_preview.sourceImageWidth(), editor_preview.sourceImageHeight());
            window->resize(1280, 900);
            process_layout_events();
            if (!capture_path.isEmpty() && !window->grabWindow().save(capture_path + QStringLiteral(".immediate.png")))
              return fail("immediate resize capture must be saved");
            QElapsedTimer resize_settle;
            resize_settle.start();
            if (!wait_for_condition([&]() { return resize_settle.elapsed() >= 500 && !editor_preview.processing(); }, 10000))
              return fail("resized preview must settle before the layout capture");
            const auto source_and_recipe_unchanged = [&] {
              return editor_preview.assetId() == source_asset_before_resize &&
                  QSize(editor_preview.sourceImageWidth(), editor_preview.sourceImageHeight()) ==
                      source_size_before_resize &&
                  photo_editor::desktop::EditorPreviewControllerTestAccess::recipe_json(editor_preview) ==
                      recipe_before_resize &&
                  editor_preview.maskStack() == masks_before_resize &&
                  editor_preview.editHistoryEntries() == history_before_resize &&
                  editor_preview.editHistoryCurrentIndex() == history_index_before_resize;
            };
            if (window->width() != 1280 || window->height() != 900 ||
                !source_and_recipe_unchanged())
              return fail("resizing to 1280x900 must preserve source identity, recipe and history");

            const auto open_refinement = [&](const QString& page) {
              if (!QMetaObject::invokeMethod(edit_workspace, "beginStudioSelectionRefinement",
                      Q_ARG(QVariant, QVariant(page))))
                return false;
              process_layout_events();
              if (window->grabWindow().isNull()) return false;
              return edit_workspace->property("maskRefinementSessionActive").toBool() &&
                  edit_workspace->property("maskRefinementPage").toString() == page &&
                  editor_preview.selectionEditSessionActive();
            };
            if (!open_refinement(QStringLiteral("paint")))
              return fail("the active mask must open the real refinement panel for narrow-layout proof");

            const auto item_fully_visible = [&](QQuickItem* item) {
              if (item == nullptr || item->window() != window) return false;
              // Read geometry after the same polish synchronization used by
              // pointer dispatch; a page change can enqueue a fresh layout.
              if (window->grabWindow().isNull()) return false;
              const auto visible = visible_rect(item);
              return visible.width() >= item->width() - 1 &&
                  visible.height() >= item->height() - 1;
            };
            auto* panel = find_named_item(edit_workspace, QStringLiteral("creativeToolsPanel"));
            auto* footer = find_named_item(edit_workspace, QStringLiteral("creativeToolsFooter"));
            auto* done_button = find_named_item(edit_workspace, QStringLiteral("maskCreationDoneButton"));
            auto* cancel_button = find_named_item(edit_workspace, QStringLiteral("maskRefinementCancelButton"));
            auto* paint_button = find_named_item(edit_workspace, QStringLiteral("maskRefinementPaintPageButton"));
            const auto same_scene_rect = [](const QRectF& left, const QRectF& right) {
              return std::abs(left.x() - right.x()) <= 1.0 &&
                  std::abs(left.y() - right.y()) <= 1.0 &&
                  std::abs(left.width() - right.width()) <= 1.0 &&
                  std::abs(left.height() - right.height()) <= 1.0;
            };
            const auto footer_in_panel = panel != nullptr && footer != nullptr &&
                QRectF(panel->mapToScene(QPointF(0, 0)), QSizeF(panel->width(), panel->height()))
                    .contains(scene_rect(footer));
            const auto panel_footer_ready = panel != nullptr && footer != nullptr &&
                done_button != nullptr && cancel_button != nullptr && paint_button != nullptr &&
                done_button->parentItem() == footer && cancel_button->parentItem() == footer &&
                done_button->isEnabled() && cancel_button->isEnabled() &&
                item_fully_visible(footer) && item_fully_visible(done_button) &&
                item_fully_visible(cancel_button) && item_fully_visible(paint_button) &&
                footer_in_panel;
            if (!panel_footer_ready) {
              const auto report_item = [&](const char* label, QQuickItem* item) {
                std::cerr << "  " << label << '=';
                if (item == nullptr) {
                  std::cerr << "missing\n";
                  return;
                }
                const auto parent_name = item->parentItem() == nullptr
                    ? QStringLiteral("<none>") : item->parentItem()->objectName();
                const auto full = scene_rect(item);
                const auto visible = visible_rect(item);
                std::cerr << item->objectName().toStdString()
                          << " visible=" << item->isVisible()
                          << " enabled=" << item->isEnabled()
                          << " sameWindow=" << (item->window() == window)
                          << " parent=" << parent_name.toStdString()
                          << " size=" << item->width() << 'x' << item->height()
                          << " scene=" << full.x() << ',' << full.y() << ',' << full.width() << ',' << full.height()
                          << " clipped=" << visible.x() << ',' << visible.y() << ',' << visible.width() << ',' << visible.height()
                          << " fullyVisible=" << item_fully_visible(item) << '\n';
              };
              std::cerr << "narrow-panel-footer diagnostic: window=" << window->width() << 'x' << window->height()
                        << " selectionPanel=" << edit_workspace->property("selectionPanelActive").toBool()
                        << " refineSession=" << edit_workspace->property("maskRefinementSessionActive").toBool()
                        << " page=" << edit_workspace->property("maskRefinementPage").toString().toStdString()
                        << " creativeTool=" << edit_workspace->property("creativeToolKind").toString().toStdString()
                        << " panelFooterReady=" << panel_footer_ready
                        << " footerInPanel=" << footer_in_panel << '\n';
              report_item("panel", panel);
              report_item("footer", footer);
              report_item("done", done_button);
              report_item("cancel", cancel_button);
              report_item("paint", paint_button);
              auto proof_directory = qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR");
              if (proof_directory.isEmpty()) proof_directory = runtime_root.path();
              if (QDir().mkpath(proof_directory)) {
                const auto failure_capture = QDir(proof_directory).filePath(
                    QStringLiteral("mask-refinement-narrow-layout-failure.png"));
                const auto saved = window->grabWindow().save(failure_capture);
                std::cerr << "narrow-panel-footer screenshot=" << failure_capture.toStdString()
                          << " saved=" << saved << '\n';
              } else {
                std::cerr << "narrow-panel-footer screenshot directory unavailable: "
                          << proof_directory.toStdString() << '\n';
              }
              return fail("the real refinement panel and fixed Done/Cancel footer must be fully visible at 1280x900");
            }

            if (scene_rect(done_button).intersects(scene_rect(cancel_button)))
              return fail("fixed Done and Cancel footer actions must not overlap");
            const auto pre_paint_masks = editor_preview.maskStack();
            const auto adds_brush = paint_button->property("text").toString() == QStringLiteral("Add Brush");
            if (!click_item(paint_button))
              return fail("the visible primary Paint/Add Brush action must be clickable");
            if (adds_brush) {
              if (editor_preview.maskStack().size() != pre_paint_masks.size() + 1 ||
                  editor_preview.activeMaskKind() != QStringLiteral("brush"))
                return fail("Add Brush must open a real transient refinement component before Cancel");
            } else if (edit_workspace->property("maskRefinementPage").toString() != QStringLiteral("paint")) {
              return fail("Paint must select its refinement page");
            }
            if (!click_item(cancel_button) ||
                !wait_for_condition([&] {
                  return !edit_workspace->property("maskRefinementSessionActive").toBool() &&
                      !editor_preview.selectionEditSessionActive() && !editor_preview.processing();
                }, 5000) || !source_and_recipe_unchanged())
              return fail("fixed Cancel must remain actionable and restore the unchanged source recipe");

            if (!open_refinement(QStringLiteral("edge")))
              return fail("refinement must reopen at Edge after Cancel");
            auto* page_row = find_named_item(edit_workspace, QStringLiteral("maskRefinementPages"));
            if (page_row == nullptr)
              return fail("the refinement inspector must expose its real page navigation row");
            const auto fixed_footer_scene_rect = scene_rect(footer);
            QList<QRectF> page_bounds;
            for (const auto& entry : {
                     std::pair{QStringLiteral("maskRefinementEdgePageButton"), QStringLiteral("edge")},
                     std::pair{QStringLiteral("maskRefinementStrengthPageButton"), QStringLiteral("strength")},
                     std::pair{QStringLiteral("maskRefinementViewPageButton"), QStringLiteral("view")}}) {
              auto* button = find_named_item(edit_workspace, entry.first);
              if (button == nullptr || !click_item(button) ||
                  edit_workspace->property("maskRefinementPage").toString() != entry.second ||
                  !item_fully_visible(page_row) || !item_fully_visible(button))
                return fail("each visible primary refinement page must accept a pointer click without clipping");
              const auto bounds = scene_rect(button);
              for (const auto& prior : page_bounds)
                if (prior.intersected(bounds).width() > 1 && prior.intersected(bounds).height() > 1)
                  return fail("primary refinement navigation controls must not overlap");
              page_bounds.append(bounds);
              if (scene_rect(footer).intersects(scene_rect(page_row)) ||
                  !same_scene_rect(scene_rect(footer), fixed_footer_scene_rect))
                return fail("the fixed footer must stay separate from page navigation while switching pages");
            }
            if (!click_item(find_named_item(edit_workspace, QStringLiteral("maskRefinementEdgePageButton"))))
              return fail("Edge must reopen before checking its primary and detail settings");
            auto* edge_primary = find_named_item(edit_workspace, QStringLiteral("maskFirstFeatherSlider"));
            auto* edge_detail = find_named_item(edit_workspace, QStringLiteral("maskFirstRefineSmoothSlider"));
            auto* detail_button = find_named_item(edit_workspace, QStringLiteral("maskRefinementDetailPageButton"));
            if (edge_primary == nullptr || edge_detail == nullptr || detail_button == nullptr ||
                !edge_primary->isVisible() || edge_detail->isVisible() ||
                !item_fully_visible(detail_button) ||
                !click_item(detail_button) || !edit_workspace->property("maskRefinementDetailPage").toBool())
              return fail("Edge primary and detail navigation must both be visible and actionable");
            process_layout_events();
            if (edge_primary->isVisible() || !edge_detail->isVisible() ||
                !item_fully_visible(edge_detail) || !item_fully_visible(detail_button) ||
                !same_scene_rect(scene_rect(footer), fixed_footer_scene_rect))
              return fail("Edge detail controls must replace primary settings without covering or moving the fixed footer");
            if (!click_item(detail_button) || edit_workspace->property("maskRefinementDetailPage").toBool())
              return fail("the visible detail Back control must restore primary Edge settings");
            process_layout_events();
            if (!source_and_recipe_unchanged())
              return fail("refinement navigation must leave source, recipe and history unchanged");
            if (!capture_path.isEmpty() && !window->grabWindow().save(capture_path + QStringLiteral(".narrow.png")))
              return fail("active narrow refinement panel and fixed footer capture must be saved");
            if (!click_item(done_button) ||
                !wait_for_condition([&] {
                  return !edit_workspace->property("maskRefinementSessionActive").toBool() &&
                      !editor_preview.selectionEditSessionActive() && !editor_preview.processing();
                }, 5000) || !source_and_recipe_unchanged())
              return fail("fixed Done must finish the unchanged refinement session without altering the recipe");
            std::cout << "mask-refinement-layout=passed; window=1280x900; pages=paint,edge,strength,view; footer=done,cancel; source-recipe-stable=true\n";
          }
          if (!capture_path.isEmpty() && (capture_workspace == nullptr || capture_workspace->window() == nullptr ||
              !capture_workspace->window()->grabWindow().save(capture_path)))
            return fail("native mask pointer proof screenshot must be saved");
