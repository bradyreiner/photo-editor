  if (args.contains("--shadow-receiver")) {
    const auto ready = [&] { return viewport->status().value("previewPhase") == "photo"; };
    const auto current = [&] { return viewport->status().value("sceneComposition").toMap(); };
    const auto mode = [&] { return current().value("captureLighting").toMap().value("mode").toString(); };
    GaussianSceneComposition composition; composition.ambient = 0.1F;
    // Keep an emission preference to verify that switching modes preserves it
    // while the renderer and control correctly leave it inactive for shadows.
    composition.capture_emission = true;
    if (!viewport->setSceneComposition(composition.description()).value("ok").toBool() || !wait_for(ready)) return fail("Receiver setup");
    const auto photo = [&](const QString& name) {
      const auto path = QDir(root).filePath(name+".png");
      if (!viewport->exportPhoto(path,256).value("ok").toBool() || !wait_for([&] { return viewport->status().value("lastExport") == path; })) return QImage();
      return QImage(path);
    };
    const auto baseline = photo("captured");
    if (baseline.isNull() || !click("sceneObjectsButton")) return fail("Receiver baseline");
    auto* panel = fixture->findChild<QObject*>("sceneCompositionPopup");
    auto* section = fixture->findChild<QObject*>("sceneCompositionSection");
    const auto choose_section = [&] { return section && section->setProperty("currentIndex",2) && QMetaObject::invokeMethod(section,"activated",Q_ARG(int,2)); };
    const auto choose_mode = [&](int index) {
      auto* combo = visual_item(window->contentItem(),"sceneCaptureLightingMode");
      auto* scroll = visual_item(window->contentItem(),"sceneCompositionScroll");
      if (!combo || !scroll || !combo->isVisible()) return false;
      const auto origin = scroll->mapToScene(QPointF());
      const auto center = combo->mapToScene(QPointF(combo->width()/2,combo->height()/2));
      const auto maximum = std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height());
      scroll->setProperty("contentY",std::clamp(scroll->property("contentY").toDouble()+center.y()-origin.y()-scroll->height()/2,0.0,maximum));
      if (!capture("receiver-controls")) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,combo->mapToScene(QPointF(combo->width()/2,combo->height()/2)).toPoint());
      QQuickItem* option_item = nullptr;
      if (!wait_for([&] { option_item = visual_item(window->contentItem(),QStringLiteral("sceneCaptureLightingModeOption%1").arg(index)); return option_item && option_item->isVisible(); })) return false;
      const auto hit = option_item->mapToScene(QPointF(option_item->width()/2,option_item->height()/2));
      if (!QRectF(0,0,window->width(),window->height()).contains(hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint());
      return wait_for([&] { return ready() && mode() == (index == 2 ? "shadow-receiver" : index == 1 ? "lit-color" : "captured"); });
    };
    if (!panel || !wait_for([&] { return panel->property("opened").toBool(); }) || !choose_section() || !choose_mode(2)) return fail("Native receiver choice");
    auto* emission = visual_item(window->contentItem(),"sceneCaptureEmission");
    if (!emission || emission->isEnabled() || !current().value("captureEmission").toBool() || current().value("schema") != "looksy-scene-composition-v17") return fail("Receiver emission preference");
    if (!click("sceneCompositionUndoButton") || !wait_for([&] { return ready() && mode() == "captured"; }) ||
        !click("sceneCompositionRedoButton") || !wait_for([&] { return ready() && mode() == "shadow-receiver"; })) return fail("Receiver undo/redo");
    auto* quality = visual_item(window->contentItem(),"sceneLightSamples");
    if (!quality || !quality->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(quality,"activated",Q_ARG(int,1)) || !wait_for(ready) || mode() != "shadow-receiver") return fail("Receiver quality retention");
    auto* strength = visual_item(window->contentItem(),"sceneEffect_shadowStrength");
    if (!strength || !strength->setProperty("text","0.7") || !strength->setProperty("userEdited",true) || !QMetaObject::invokeMethod(strength,"editingFinished") ||
        !wait_for(ready) || mode() != "shadow-receiver") return fail("Receiver effect retention");
    QMetaObject::invokeMethod(panel,"close");
    const auto receiver = photo("receiver-unoccluded");
    if (receiver.isNull() || receiver != baseline) return fail("Unoccluded photo identity");
    const auto accepted = current();
    const auto shot = viewport->saveView("Shadow receiver A"); viewport->orbit(1,0);
    const auto second = viewport->saveView("Shadow receiver B");
    if (!shot.value("ok").toBool() || !second.value("ok").toBool() || !viewport->saveCameraPath({{"viewIds",QVariantList{shot.value("id"),second.value("id")}}, {"segmentSeconds",0.5}, {"fps",24}}).value("ok").toBool()) return fail("Receiver camera path");
    const auto sequence = QDir(root).filePath("receiver-frames");
    if (!viewport->exportFrameSequence(sequence,256).value("ok").toBool()) return fail("Receiver sequence start");
    auto edited = accepted; auto settings = edited.value("captureLighting").toMap(); settings["mode"] = "lit-color"; edited["captureLighting"] = settings;
    if (!viewport->setSceneComposition(edited).value("ok").toBool() || !wait_for([&] { return !viewport->status().value("filmExporting").toBool(); }) ||
        !viewport->status().value("filmResult").toMap().value("ok").toBool()) return fail("Frozen receiver sequence");
    QFile receipt(QDir(sequence).filePath("camera-path.json"));
    if (!receipt.open(QIODevice::ReadOnly) || QJsonDocument::fromJson(receipt.readAll()).toVariant().toMap().value("sceneObjects").toMap() != accepted) return fail("Receiver frozen recipe");
    if (!viewport->setSceneComposition(accepted).value("ok").toBool() || !wait_for(ready)) return fail("Receiver restore");
    viewport->setSource({});
    if (!controller.openProject(project).value("ok").toBool() || !wait_for([&] { return !controller.snapshot().value("busy").toBool(); })) return fail("Receiver reopen");
    viewport->setSource(controller.snapshot());
    if (!wait_for([&] { return ready() && current() == accepted; }) || !viewport->restoreView(shot.value("id").toString()).value("ok").toBool() || !wait_for(ready)) return fail("Receiver restored recipe");
    window->resize(1024,760);
    if (!click("sceneObjectsButton") || !wait_for([&] { return panel->property("opened").toBool(); }) || !choose_section() ||
        !choose_mode(1) || !emission->isEnabled() || !choose_mode(2) || emission->isEnabled() || !capture("receiver-compact")) return fail("Compact native receiver control");
    if (!qml_warnings.isEmpty()) return fail("Receiver QML warnings");
    if (!evidence.isEmpty() && !write(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(QVariantMap{
        {"passed",true},{"composition",accepted},{"metrics",viewport->status().value("metrics")},
        {"checks",QStringList{"native-mode-choice","undo-redo","quality-retention","effect-retention","unoccluded-photo-identity","frozen-12-frame-sequence","reopen","shot-recall","compact-native-choice","emission-preference"}},
        {"windowActive",window->isActive()},{"windowExposed",window->isExposed()},
        {"scope","Native mode and persistence workflow; object-shadow math and finished-scene visual qualification are separate"}}).toJson())) return 2;
    return 0;
  }
