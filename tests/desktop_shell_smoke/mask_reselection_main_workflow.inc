  app_shell.completeFirstRunExperience();

  const auto mask_pointer_source_photo = runtime_root.filePath(
      QStringLiteral("mask-pointer-main-catalog-source.png"));
  if (!write_overlay_visual_fixture(mask_pointer_source_photo, QStringLiteral("saturated-small")))
    return fail("Main mask reselection proof must create a real catalog image fixture");
  const auto mask_pointer_source_asset = QStringLiteral("mask-pointer-main-catalog-asset");
  catalog_assets.replaceAssetsForTesting({make_asset(
      mask_pointer_source_asset,
      mask_pointer_source_photo,
      QUrl::fromLocalFile(mask_pointer_source_photo).toString())});
  catalog_assets.selectIndex(0);

  const auto mask_pointer_main_override =
      qEnvironmentVariable("PHOTO_EDITOR_SMOKE_MAIN_QML_PATH");
  const auto mask_pointer_main_path = mask_pointer_main_override.isEmpty()
      ? QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
            .filePath(QStringLiteral("apps/desktop/qml/Main.qml"))
      : mask_pointer_main_override;
  engine.load(QUrl::fromLocalFile(mask_pointer_main_path));
  QCoreApplication::processEvents();
  if (engine.rootObjects().isEmpty())
    return fail("mask reselection proof must load the actual Main.qml root");

  QObject* root = engine.rootObjects().constFirst();
  auto* root_window = qobject_cast<QQuickWindow*>(root);
  if (root == nullptr || root_window == nullptr)
    return fail("Main mask reselection proof needs its actual exposed QML window");
  root_window->resize(1440, 1100);
  root_window->show();
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER")) {
    activate_native_test_window(root_window);
    root_window->raise();
    root_window->requestActivate();
  }

  const auto edit_workspace_index = root->property("editWorkspaceIndex").toInt();
  if (edit_workspace_index < 0)
    return fail("Main mask reselection proof must expose its Edit workspace index");
  root->setProperty("currentWorkspace", edit_workspace_index);
  if (!QMetaObject::invokeMethod(root, "loadSelectedAssetIntoEditor"))
    return fail("Main mask reselection proof must route its selected catalog asset into Edit");

  QObject* edit_workspace_host = nullptr;
  QObject* edit_workspace = nullptr;

  const auto visual_proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_VISUAL_PROOF_DIR")
      ? QString::fromUtf8(qgetenv("PHOTO_EDITOR_VISUAL_PROOF_DIR"))
      : runtime_root.filePath(QStringLiteral("mask-reselection-pointer-proof"));
  if (!QDir().mkpath(visual_proof_dir))
    return fail("Main mask reselection proof must create its artifact directory");

  if (!wait_for_condition([&] {
        edit_workspace_host = find_named_object(
            root, QStringLiteral("editStudioWorkspace"));
        if (edit_workspace_host != nullptr) {
          edit_workspace = find_named_object(
              edit_workspace_host, QStringLiteral("editStudioWorkspaceContent"));
          if (edit_workspace == nullptr) edit_workspace = edit_workspace_host;
        }
        return root->property("currentWorkspace").toInt() == edit_workspace_index &&
            !root->property("workspaceTransitionActive").toBool() &&
            edit_workspace_host != nullptr && edit_workspace != nullptr &&
            edit_workspace_host->property("pageContentReady").toBool() &&
            root_window->width() == 1440 && root_window->height() == 1100 &&
            root_window->isExposed() &&
            (!qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER") || root_window->isActive()) &&
            catalog_assets.selectedAssetId() == mask_pointer_source_asset &&
            editor_preview.assetId() == mask_pointer_source_asset &&
            !editor_preview.processing() && !editor_preview.displayPreviewSource().isEmpty();
      }, 15000)) {
    std::cerr << "Main mask reselection readiness: asset="
              << editor_preview.assetId().toStdString()
              << " selected=" << catalog_assets.selectedAssetId().toStdString()
              << " workspace=" << root->property("currentWorkspace").toInt()
              << '/' << edit_workspace_index
              << " transition=" << root->property("workspaceTransitionActive").toBool()
              << " pageReady=" << (edit_workspace_host != nullptr &&
                  edit_workspace_host->property("pageContentReady").toBool())
              << " size=" << root_window->width() << 'x' << root_window->height()
              << " exposed=" << root_window->isExposed()
              << " active=" << root_window->isActive()
              << " processing=" << editor_preview.processing() << '\n';
    return fail("Main mask reselection proof must wait for its selected asset and exposed Edit workspace");
  }

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SELECTION_FOLLOWUP_WORKFLOW")) {
#include "selection_followup_main_workflow.inc"
  }
#include "mask_reselection_pointer.inc"
  return EXIT_SUCCESS;
