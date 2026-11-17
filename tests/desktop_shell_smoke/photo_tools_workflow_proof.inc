// Exercise the real Main/Studio viewer with pointer events and the real worker.
  app_shell.completeFirstRunExperience();
  const auto source_path = runtime_root.filePath("retouch-photo.png");
  QImage photo(800, 400, QImage::Format_RGB32);
  photo.fill(QColor(80, 130, 160));
  for (int y = 170; y < 230; ++y)
    for (int x = 370; x < 430; ++x) photo.setPixelColor(x, y, QColor(230, 20, 30));
  photo.setColorSpace(QColorSpace(QColorSpace::SRgb));
  if (!photo.save(source_path)) return fail("Photo tools fixture could not save");
  catalog_assets.replaceAssetsForTesting({make_asset("photo-tools", source_path)});
  catalog_assets.selectIndex(0);
  engine.load(QUrl::fromLocalFile(main_qml_path));
  if (engine.rootObjects().isEmpty()) return fail("Photo tools: Main did not load");
  auto* tool_root = engine.rootObjects().constFirst();
  auto* tool_window = qobject_cast<QQuickWindow*>(tool_root);
  if (!tool_window) return fail("Photo tools require the application window");
  tool_window->resize(1440, 1000);
  tool_window->show();
  tool_root->setProperty("currentWorkspace", tool_root->property("editWorkspaceIndex"));
  process_layout_events(12);
  if (!wait_for_condition([&] { return editor_preview.sourceImageWidth() == 800 &&
      !editor_preview.processing() && editor_preview.hasAsset(); }, 30000))
    { std::cerr << "asset=" << editor_preview.assetId().toStdString()
        << " source=" << editor_preview.sourceImageWidth() << 'x' << editor_preview.sourceImageHeight()
        << " processing=" << editor_preview.processing() << " status=" << editor_preview.status().toStdString() << '\n';
      tool_window->grabWindow().save("/tmp/photo-tools-load-failure.png");
      return fail("Photo tools source did not render"); }
  auto* studio = find_named_object(tool_root, "editStudioWorkspaceContent");
  if (!studio) studio = find_named_object(tool_root, "editStudioWorkspace");
  auto* viewer = qobject_cast<QQuickItem*>(find_named_object(studio, "editImageViewport"));
  if (!studio || !viewer) return fail("Photo tools viewer missing");
  const auto activate = [&](const QString& mode) {
    QMetaObject::invokeMethod(studio, "activateEditTool", Q_ARG(QVariant, mode));
    process_layout_events(12);
  };
  const auto point = [&](double x, double y) {
    return viewer->mapToScene(QPointF(
        viewer->property("paintedContentX").toDouble() + x * viewer->property("paintedContentWidth").toDouble(),
        viewer->property("paintedContentY").toDouble() + y * viewer->property("paintedContentHeight").toDouble()));
  };
  const auto drag = [&](const QPointF& a, const QPointF& b) {
    QMouseEvent press(QEvent::MouseButtonPress, a, a, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tool_window, &press);
    for (int i = 1; i <= 12; ++i) {
      const auto p = a + (b - a) * (double(i) / 12);
      QMouseEvent move(QEvent::MouseMove, p, p, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
      QCoreApplication::sendEvent(tool_window, &move);
      process_layout_events(1);
    }
    QMouseEvent release(QEvent::MouseButtonRelease, b, b, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tool_window, &release);
    process_layout_events(10);
  };
  const auto settle_preview = [&] {
    QElapsedTimer timeout, stable;
    timeout.start(); stable.start();
    while (timeout.elapsed() < 30000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      if (!photo_editor::desktop::EditorPreviewControllerTestAccess::preview_idle(editor_preview)) stable.restart();
      if (stable.elapsed() >= 300) return true;
      QThread::msleep(10);
    }
    return false;
  };
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_PHOTO_TOOLS_PROOF_DIR");
  const auto capture = [&](const QString& name) {
    if (!settle_preview()) return false;
    if (output.isEmpty()) return true;
    return QDir().mkpath(output) && tool_window->grabWindow().save(QDir(output).filePath(name + ".png"));
  };

  activate("crop");
  if (!wait_for_condition([&] { return !editor_preview.processing(); }, 30000))
    return fail("Crop full-source render did not settle");
  process_layout_events(12);
  drag(point(0.15, 0.2), point(0.85, 0.8));
  if (std::abs(editor_preview.cropRectX() - 0.15) > 0.02 ||
      std::abs(editor_preview.cropRectWidth() - 0.7) > 0.02 ||
      std::abs(editor_preview.cropRectHeight() - 0.7) > 0.02)
    { std::cerr << "crop=" << editor_preview.cropRectX() << ',' << editor_preview.cropRectY()
        << ' ' << editor_preview.cropRectWidth() << 'x' << editor_preview.cropRectHeight()
        << " aspect=" << editor_preview.cropAspectRatio() << " mode=" << studio->property("cropToolMode").toString().toStdString()
        << " editing=" << editor_preview.cropEditing()
        << " viewport=" << viewer->width() << 'x' << viewer->height()
        << " image=" << viewer->property("paintedContentWidth").toDouble() << 'x' << viewer->property("paintedContentHeight").toDouble() << '\n';
      tool_window->grabWindow().save("/tmp/photo-tools-crop-failure.png");
      return fail("Crop drag must reach the crop overlay above viewer input"); }
  if (!capture("01-crop")) return fail("Crop capture failed");
  activate("adjust");
  if (!wait_for_condition([&] { return !editor_preview.processing(); }, 30000)) return fail("Crop apply timed out");
  editor_preview.resetCropRect();
  if (!wait_for_condition([&] { return !editor_preview.processing(); }, 30000)) return fail("Crop reset timed out");
  activate("crop");
  if (!wait_for_condition([&] { return !editor_preview.processing(); }, 30000))
    return fail("Crop must be active before using Auto Level");

  auto* auto_level_button = find_named_object(studio, "autoLevelHorizonButton");
  auto* auto_level_feedback = find_named_object(studio, "cropAutoLevelFeedbackLabel");
  if (!auto_level_button || !auto_level_feedback || !item_visible(qobject_cast<QQuickItem*>(auto_level_button)))
    return fail("Crop should expose an enabled Auto Level action and result feedback");
  if (!send_activation_key(auto_level_button)) return fail("Auto Level should be keyboard accessible");
  if (!wait_for_condition([&] { return !editor_preview.autoHorizonAnalysisActive(); }, 30000))
    return fail("No-horizon analysis did not finish");
  if (std::abs(editor_preview.transformRotation()) > 0.001 ||
      auto_level_feedback->property("text").toString().indexOf("No single clear horizon") < 0)
    return fail("Auto Level must leave an ambiguous photo unchanged and explain the manual guide path");

  const auto auto_horizon_path = runtime_root.filePath("auto-horizon.png");
  QImage auto_horizon_photo(800, 400, QImage::Format_RGB32);
  const auto sky = QColor(75, 145, 215);
  const auto land = QColor(48, 76, 88);
  for (int x = 0; x < auto_horizon_photo.width(); ++x) {
    const auto horizon_y = qRound(160.0 + 0.04 * (static_cast<double>(x) - 400.0));
    for (int y = 0; y < auto_horizon_photo.height(); ++y)
      auto_horizon_photo.setPixelColor(x, y, y < horizon_y ? sky : land);
  }
  auto_horizon_photo.setColorSpace(QColorSpace(QColorSpace::SRgb));
  if (!auto_horizon_photo.save(auto_horizon_path)) return fail("Auto horizon fixture could not save");
  catalog_assets.replaceAssetsForTesting({make_asset("auto-horizon", auto_horizon_path)});
  catalog_assets.selectIndex(0);
  if (!QMetaObject::invokeMethod(tool_root, "loadSelectedAssetIntoEditor"))
    return fail("Auto horizon fixture must activate the selected editor document");
  if (!wait_for_condition([&] { return editor_preview.assetId() == "auto-horizon" &&
      editor_preview.sourceImageWidth() == 800 && !editor_preview.processing(); }, 30000))
    return fail("Auto horizon fixture did not load");
  activate("crop");
  if (!wait_for_condition([&] { return !editor_preview.processing(); }, 30000))
    return fail("Auto horizon crop tool did not settle");
  auto_level_button = find_named_object(studio, "autoLevelHorizonButton");
  auto_level_feedback = find_named_object(studio, "cropAutoLevelFeedbackLabel");
  if (!send_activation_key(auto_level_button)) return fail("Auto Level should start from the crop page");
  if (!wait_for_condition([&] { return !editor_preview.autoHorizonAnalysisActive(); }, 30000))
    return fail("Confident horizon analysis did not finish");
  const auto expected_auto_angle = std::atan2(32.0, 799.0) * 180.0 / 3.141592653589793;
  if (std::abs(editor_preview.transformRotation() + expected_auto_angle) > 0.3 ||
      auto_level_feedback->property("text").toString().indexOf("Auto level applied") < 0)
    return fail("Auto Level should correct a clear tilted horizon with undoable rotation");
  if (!wait_for_condition([&] { return !editor_preview.processing(); }, 30000) ||
      !capture("02-auto-level"))
    return fail("Auto Level render did not settle");
  const auto first_auto_rotation = editor_preview.transformRotation();
  if (!send_activation_key(auto_level_button)) return fail("Repeated Auto Level should remain available");
  if (!wait_for_condition([&] { return !editor_preview.autoHorizonAnalysisActive(); }, 30000))
    return fail("Repeated horizon analysis did not finish");
  if (std::abs(editor_preview.transformRotation() - first_auto_rotation) > 0.001 ||
      auto_level_feedback->property("text").toString().indexOf("already appears level") < 0)
    return fail("Repeated Auto Level must not accumulate correction");
  editor_preview.undo();
  if (std::abs(editor_preview.transformRotation()) > 0.001)
    return fail("Auto Level should restore in one undo step");
  if (!wait_for_condition([&] { return !editor_preview.processing(); }, 30000))
    return fail("Auto Level undo render did not settle");

  // Leaving Crop before background analysis completes must suppress the result.
  if (!editor_preview.autoLevelFromHorizon()) return fail("Auto Level cancellation fixture did not start");
  activate("adjust");
  if (!wait_for_condition([&] { return !editor_preview.autoHorizonAnalysisActive(); }, 30000))
    return fail("Leaving Crop did not finish canceling Auto Level");
  if (std::abs(editor_preview.transformRotation()) > 0.001)
    return fail("Leaving Crop must cancel an in-flight Auto Level result");

  catalog_assets.replaceAssetsForTesting({make_asset("photo-tools", source_path)});
  catalog_assets.selectIndex(0);
  if (!QMetaObject::invokeMethod(tool_root, "loadSelectedAssetIntoEditor"))
    return fail("Original photo must activate after the Auto Level checks");
  if (!wait_for_condition([&] { return editor_preview.assetId() == "photo-tools" &&
      editor_preview.sourceImageWidth() == 800 && !editor_preview.processing(); }, 30000))
    return fail("Original photo did not return after Auto Level checks");
  activate("crop");
  if (!wait_for_condition([&] { return !editor_preview.processing(); }, 30000))
    return fail("Crop workflow did not return after Auto Level checks");
  editor_preview.resetCropRect();
  if (!wait_for_condition([&] { return !editor_preview.processing(); }, 30000))
    return fail("Returned crop reset timed out");
  QMetaObject::invokeMethod(studio, "setCropToolMode", Q_ARG(QVariant, QStringLiteral("horizon")));
  process_layout_events(12);
  drag(point(0.1, 0.3), point(0.9, 0.5));
  const auto expected_angle = std::atan2(0.2 * 400, 0.8 * 800) * 180 / 3.141592653589793;
  if (std::abs(editor_preview.transformRotation() + expected_angle) > 0.2)
    return fail("Horizon pointer angle must use the photo pixel aspect");
  if (!wait_for_condition([&] { return !editor_preview.processing(); }, 30000)) return fail("Horizon render timed out");
  if (!capture("02-horizon")) return fail("Horizon capture failed");
  editor_preview.undo();
  if (std::abs(editor_preview.transformRotation()) > 0.001) return fail("Horizon undo failed");
  activate("adjust");
  if (!wait_for_condition([&] { return !editor_preview.processing(); }, 30000)) return fail("Horizon undo render timed out");
  const auto before = photo;
  activate("retouch");
  if (!wait_for_condition([&] { return editor_preview.hasActiveMask() &&
      editor_preview.activeMaskToolRole() == "retouch"; }, 10000)) return fail("Retouch did not start");
  editor_preview.setActiveMaskBrushSize(0.26);
  editor_preview.setActiveMaskBrushHardness(1.0);
  editor_preview.setActiveMaskFeather(0.0);
  process_layout_events(12);
  drag(point(0.47, 0.5), point(0.53, 0.5));
  if (!wait_for_condition([&] { return !editor_preview.processing() && editor_preview.activeMaskBrushPointCount() > 0; }, 15000))
    return fail("Retouch pointer must paint an actual inpaint selection");
  if (!capture("03-inpaint-selection")) return fail("Retouch selection capture failed");
  editor_preview.applyActiveRetouch();
  if (!wait_for_condition([&] { return !editor_preview.processing() && !editor_preview.inpaintProgressActive(); }, 60000) ||
      !editor_preview.activeMaskCommitted() ||
      editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap().value("inpaintPatchData").toByteArray().isEmpty())
    return fail("Painted inpaint must generate and publish a committed patch");
  if (!settle_preview()) return fail("Inpaint viewer render did not settle");
  const auto after_source = QUrl(editor_preview.displayPreviewSource());
  auto after = after_source.isLocalFile() ? QImage(after_source.toLocalFile())
      : editor_preview_image_provider->image(after_source.path().mid(1));
  if (after.isNull()) after = editor_preview_image_provider->image("edited");
  const auto applied_mask = editor_preview.maskStack().at(editor_preview.activeMaskIndex()).toMap();
  const auto patch = QImage::fromData(applied_mask.value("inpaintPatchData").toByteArray());
  if (!output.isEmpty() && (!after.save(QDir(output).filePath("inpaint-pixels.png")) ||
      !patch.save(QDir(output).filePath("generated-patch.png")))) return fail("Inpaint pixel evidence could not save");
  if (!capture("04-inpaint-applied")) return fail("Retouch applied capture failed");
  std::cerr << "inpaint source=" << editor_preview.displayPreviewSource().toStdString()
      << " size=" << after.width() << 'x' << after.height()
      << " center=" << after.pixelColor(after.width()/2,after.height()/2).name().toStdString()
      << " background=" << after.pixelColor(after.width()/4,after.height()/2).name().toStdString()
      << " model=" << applied_mask.value("inpaintModelName").toString().toStdString()
      << " patch=" << patch.width() << 'x' << patch.height()
      << " enabled=" << applied_mask.value("enabled").toBool()
      << " strength=" << applied_mask.value("removeStrength").toDouble() << '\n';
  if (before.isNull() || after.isNull() || before == after ||
      std::abs(after.pixelColor(after.width() / 2, after.height() / 2).red() -
               after.pixelColor(after.width() / 4, after.height() / 2).red()) > 10)
    return fail("Applied inpaint must change viewer pixels");
  QVariant busy_chip;
  if (!QMetaObject::invokeMethod(studio, "maskActivityChipVisible", Q_RETURN_ARG(QVariant, busy_chip)) ||
      busy_chip.toBool()) return fail("Applied inpaint must not retain a busy indicator");
  editor_preview.undo();
  if (editor_preview.activeMaskCommitted()) return fail("Inpaint apply must undo to a painted selection");
  editor_preview.redo();
  if (!editor_preview.activeMaskCommitted()) return fail("Inpaint apply must redo");
  activate("compare");
  auto* compare = qobject_cast<QQuickItem*>(find_named_object(studio, "compareWorkspaceRow"));
  if (!compare || compare->parentItem() != viewer || !item_visible(compare) || !item_visible(viewer) ||
      compare->window() != tool_window || compare->width() < 400 || compare->height() < 200)
    return fail("Compare must remain inside the existing image viewer");
  for (const auto& mode : {"split", "wipe", "side-by-side"}) {
    studio->setProperty("compareViewMode", QString::fromLatin1(mode));
    process_layout_events(16);
    if (QString::fromLatin1(mode) == "side-by-side" &&
        (!find_named_object(studio, "compareImageViewport0") || !find_named_object(studio, "compareImageViewport1")))
      return fail("Linked compare requires two viewer surfaces");
    if (!capture("05-compare-" + QString::fromLatin1(mode))) return fail("Compare capture failed");
  }
  if (!send_activation_key(find_named_object(studio, "compareDoneButton"))) return fail("Compare Done must be accessible");
  if (editor_preview.compareMode() || !item_visible(viewer)) return fail("Compare Done must restore editing in place");
  std::cout << "Photo tools: crop pointer, rectangular horizon/undo, painted inpaint/apply/undo/redo and three in-viewer compare modes passed\n";
  return EXIT_SUCCESS;
