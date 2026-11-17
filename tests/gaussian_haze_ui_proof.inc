  if (args.contains("--haze-shadows")) {
    const auto ready = [&] { return viewport->status().value("previewPhase") == "photo"; };
    const auto current = [&] { return viewport->status().value("sceneComposition").toMap(); };
    const auto view = gaussian_saved_camera(viewport->status().value("metrics").toMap().value("camera").toMap());
    if (!view) return fail("Haze camera unavailable");
    const float focus = viewport->status().value("sceneFocusDistance").toFloat();
    GaussianSceneComposition composition; composition.ambient = 0.1F; composition.haze_density = 0.4F/focus;
    composition.scan_shadows = args.contains("--haze-scan-shadows");
    composition.haze_distance = focus*3; composition.haze_color = {0.25F,0.3F,0.4F}; composition.light_samples = 16;
    GaussianSceneLight light; light.id = "haze-sun"; light.name = "Haze sun"; light.vector = view->right; light.intensity = 100;
    composition.lights = {light};
    GaussianSceneObject blocker; blocker.id = "haze-blocker"; blocker.name = "Fog shadow blocker"; blocker.kind = 1;
    blocker.position = view->position+view->forward*focus+view->right*(focus*0.5F);
    blocker.size = {focus*0.18F,focus*0.5F,focus*0.5F}; blocker.color = {0.15F,0.15F,0.15F}; composition.objects = {blocker};
    if (!viewport->setSceneComposition(composition.description()).value("ok").toBool() || !wait_for(ready)) return fail("Haze scene setup");
    const auto photo = [&](const QString& name) {
      const auto destination = QDir(root).filePath(name+".png");
      if (!viewport->exportPhoto(destination,256).value("ok").toBool() || !wait_for([&] { return viewport->status().value("lastExport") == destination; })) return QImage();
      return QImage(destination);
    };
    const auto baseline = photo("haze-off");
    if (baseline.isNull() || !click("sceneObjectsButton")) return fail("Haze baseline");
    auto* panel = fixture->findChild<QObject*>("sceneCompositionPopup"); auto* section = fixture->findChild<QObject*>("sceneCompositionSection");
    const auto choose_haze = [&] { return section && section->setProperty("currentIndex",3) && QMetaObject::invokeMethod(section,"activated",Q_ARG(int,3)); };
    const auto toggle = [&](bool expected) {
      auto* button = visual_item(window->contentItem(),"sceneHazeShadows");
      auto* scroll = visual_item(window->contentItem(),"sceneCompositionScroll");
      if (!button || !scroll || !button->isVisible() || !button->isEnabled()) return false;
      const auto origin = scroll->mapToScene(QPointF());
      const auto center = button->mapToScene(QPointF(20,button->height()/2));
      const auto maximum = std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height());
      scroll->setProperty("contentY",std::clamp(scroll->property("contentY").toDouble()+center.y()-origin.y()-scroll->height()/2,0.0,maximum));
      if (!capture("haze-controls")) return false;
      const auto hit = button->mapToScene(QPointF(20,button->height()/2));
      if (!QRectF(origin,QSizeF(scroll->width(),scroll->height())).contains(hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint());
      return wait_for([&] { return ready() && current().value("hazeShadows").toBool() == expected; });
    };
    if (!panel || !wait_for([&] { return panel->property("opened").toBool(); }) || !choose_haze() || !toggle(true)) return fail("Native haze toggle");
    if (!viewport->status().value("metrics").toMap().value("hazeModel").toString().startsWith("shadowed-")) return fail("Haze renderer mode");
    QMetaObject::invokeMethod(panel,"close");
    const auto shadow_only = photo("haze-shadow-only");
    if (shadow_only.isNull() || shadow_only == baseline || !click("sceneObjectsButton") ||
        !wait_for([&] { return panel->property("opened").toBool(); }) || !choose_haze()) return fail("Controlled haze photograph");
    if (!click("sceneCompositionUndoButton") || !wait_for([&] { return ready() && !current().value("hazeShadows").toBool(); }) ||
        !click("sceneCompositionRedoButton") || !wait_for([&] { return ready() && current().value("hazeShadows").toBool(); })) return fail("Haze undo/redo");
    auto* quality = visual_item(window->contentItem(),"sceneLightSamples");
    if (!quality || !quality->isVisible() || !quality->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(quality,"activated",Q_ARG(int,1)) ||
        !wait_for(ready) || current().value("lightSamples").toInt() != 32 || !current().value("hazeShadows").toBool()) return fail("Haze sample retention");
    auto* density = visual_item(window->contentItem(),"sceneEffect_density");
    if (!density || !density->setProperty("text",QString::number(0.35F/focus)) || !density->setProperty("userEdited",true) ||
        !QMetaObject::invokeMethod(density,"editingFinished") || !wait_for(ready) || !current().value("hazeShadows").toBool()) return fail("Haze edit retention");
    QMetaObject::invokeMethod(panel,"close");
    const auto changed = photo("haze-on"); const auto accepted_composition = current();
    if (changed.isNull() || changed == baseline || !capture("haze-shadow-view")) return fail("Haze visible result");
    const auto shot = viewport->saveView("Haze camera");
    if (!shot.value("ok").toBool()) return fail("Haze shot save");
    viewport->orbit(1,0); const auto second = viewport->saveView("Haze camera B");
    if (!second.value("ok").toBool() || !viewport->saveCameraPath({{"viewIds",QVariantList{shot.value("id"),second.value("id")}}, {"segmentSeconds",0.5}, {"fps",24}}).value("ok").toBool()) return fail("Haze camera path");
    const auto sequence = QDir(root).filePath("haze-frames");
    if (!viewport->exportFrameSequence(sequence,256).value("ok").toBool()) return fail("Haze sequence start");
    auto disabled = accepted_composition; disabled["hazeShadows"] = false;
    if (!viewport->setSceneComposition(disabled).value("ok").toBool() || !wait_for([&] { return !viewport->status().value("filmExporting").toBool(); }) ||
        !viewport->status().value("filmResult").toMap().value("ok").toBool()) return fail("Frozen haze sequence");
    QFile sequence_receipt(QDir(sequence).filePath("camera-path.json"));
    if (!sequence_receipt.open(QIODevice::ReadOnly) || QJsonDocument::fromJson(sequence_receipt.readAll()).toVariant().toMap().value("sceneObjects").toMap() != accepted_composition) return fail("Haze sequence frozen composition");
    if (!viewport->setSceneComposition(accepted_composition).value("ok").toBool() || !wait_for(ready)) return fail("Restore frozen haze");
    viewport->setSource({});
    if (!controller.openProject(project).value("ok").toBool() || !wait_for([&] { return !controller.snapshot().value("busy").toBool(); })) return fail("Haze reopen");
    viewport->setSource(controller.snapshot());
    if (!wait_for([&] { return ready() && current() == accepted_composition; }) || !viewport->restoreView(shot.value("id").toString()).value("ok").toBool() ||
        !wait_for(ready)) return fail("Haze restore");
    window->resize(1024,760);
    if (!click("sceneObjectsButton") || !wait_for([&] { return panel->property("opened").toBool(); }) || !choose_haze() || !toggle(false) || !toggle(true) || !capture("haze-compact")) return fail("Compact haze control");
    if (!qml_warnings.isEmpty()) return fail("Haze QML warnings");
    if (!evidence.isEmpty() && !write(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(QVariantMap{
        {"passed",true},{"composition",accepted_composition},{"metrics",viewport->status().value("metrics")},
        {"checks",QStringList{"native-toggle","undo-redo","quality-retention","effect-retention","controlled-photo-export","frozen-12-frame-sequence","reopen","shot-recall","compact-native-toggle"}},
        {"scope","Native haze control and persistence workflow; not dense realtime or photographic qualification"}}).toJson())) return 2;
    return 0;
  }
