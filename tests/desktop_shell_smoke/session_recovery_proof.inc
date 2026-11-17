  // Exercise the real shell and catalog signals. Re-selecting a recovered
  // photo emits recipeReadinessChanged synchronously, even when it is ready.
  const auto recovered_asset_id = QStringLiteral("session-recovery-photo");
  const auto recovered_photo = runtime_root.filePath(QStringLiteral("recovered-photo.png"));
  if (!write_overlay_visual_fixture(recovered_photo, QStringLiteral("saturated-small")))
    return fail("session recovery should create a decodable photo");
  app_shell.completeFirstRunExperience();
  app_shell.setCurrentWorkspace(1);
  app_shell.updateSessionState(recovered_asset_id, {}, {}, {}, QStringLiteral("adjust"));
  photo_editor::desktop::AppShellController recovered_shell;
  if (!recovered_shell.sessionRecoveryAvailable())
    return fail("session recovery should read the isolated unclean-exit checkpoint");
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), &recovered_shell);
  const auto recovery_shell_cleanup = qScopeGuard([&]() {
    // The checkpoint controller must outlive the QML bindings that use it.
    for (auto* object : engine.rootObjects()) delete object;
    engine.rootContext()->setContextProperty(QStringLiteral("appShell"), &app_shell);
  });

  int restored_asset_loads = 0;
  int readiness_notifications = 0;
  bool recursion_detected = false;
  QObject recovery_signal_scope;
  QObject::connect(&editor_preview, &photo_editor::desktop::EditorPreviewController::assetChanged,
      &recovery_signal_scope, [&]() {
        if (editor_preview.assetId() == recovered_asset_id) ++restored_asset_loads;
      });
  QObject::connect(&catalog_assets, &photo_editor::desktop::CatalogAssetModel::recipeReadinessChanged,
      &recovery_signal_scope, [&]() {
        // Bound the failure on the unfixed shell instead of overflowing the
        // JavaScript stack and loading the same photo hundreds of times.
        if (++readiness_notifications > 8 && !recursion_detected) {
          recursion_detected = true;
          recovered_shell.dismissSessionRecovery();
        }
      });
  QStringList recovery_warnings;
  QObject::connect(&engine, &QQmlEngine::warnings, &recovery_signal_scope,
      [&](const QList<QQmlError>& warnings) {
        for (const auto& warning : warnings) {
          if (warning.description().contains(QStringLiteral("RangeError")) ||
              warning.description().contains(QStringLiteral("Binding loop")))
            recovery_warnings.push_back(warning.toString());
        }
      });
  engine.load(QUrl::fromLocalFile(main_qml_path));
  if (engine.rootObjects().isEmpty()) return fail("session recovery should load Main.qml");
  auto* recovery_root = engine.rootObjects().constFirst();

  // The catalog has not arrived yet: a deferred restore must release its guard
  // so that the next readiness notification can retry it.
  QVariant restore_result;
  if (!QMetaObject::invokeMethod(recovery_root, "restoreRecoveredSession",
          Q_RETURN_ARG(QVariant, restore_result)) || restore_result.toBool() ||
      recovery_root->property("sessionRestoreApplied").toBool() ||
      recovery_root->property("sessionRestoreInProgress").toBool())
    return fail("session recovery must stay retryable while the photo is unavailable");

  catalog_assets.replaceAssetsForTesting({make_asset(recovered_asset_id, recovered_photo,
      QUrl::fromLocalFile(recovered_photo).toString())});
  if (recursion_detected)
    return fail("session recovery reentered through synchronous catalog recipe readiness");
  if (!wait_for_condition([&]() { return recovery_root->property("sessionRestoreApplied").toBool() &&
          !editor_preview.processing() && !editor_preview.displayPreviewSource().isEmpty(); }, 10000) ||
      recovery_root->property("sessionRestoreInProgress").toBool() ||
      editor_preview.assetId() != recovered_asset_id || restored_asset_loads != 1)
    return fail("session recovery must load the recovered photo exactly once and finish its preview");

  // Readiness and explicit retries after completion must leave the open photo
  // intact instead of canceling and restarting its preview.
  const auto render_generation =
      photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(editor_preview);
  catalog_assets.selectAssetById(recovered_asset_id, true);
  if (!QMetaObject::invokeMethod(recovery_root, "restoreRecoveredSession",
          Q_RETURN_ARG(QVariant, restore_result)) || !restore_result.toBool() ||
      restored_asset_loads != 1 || render_generation !=
          photo_editor::desktop::EditorPreviewControllerTestAccess::render_generation(editor_preview) ||
      !recovery_warnings.isEmpty())
    return fail("completed recovery must ignore duplicate readiness without QML recursion or preview restarts");
  std::cout << "session-recovery: deferred-retry=passed; synchronous-readiness=passed; "
               "asset-loads=1; preview-settled=passed; repeated-readiness=passed\n";
  return EXIT_SUCCESS;
