  if (args.contains("--capture-emission") || args.contains("--capture-lighting-cache")) {
    const bool probe_workflow = args.contains("--capture-lighting-cache");
    const auto ready = [&] { return viewport->status().value("previewPhase") == "photo"; };
    const auto current = [&] { return viewport->status().value("sceneComposition").toMap(); };
    const auto view = gaussian_saved_camera(viewport->status().value("metrics").toMap().value("camera").toMap());
    if (!view) return fail("Emission camera unavailable");
    const float focus = viewport->status().value("sceneFocusDistance").toFloat();
    GaussianSceneComposition composition; composition.ambient = 0.25F; composition.capture_lighting = 1;
    composition.capture_power = 0.8F; composition.light_samples = 64;
    GaussianSceneObject emitter; emitter.id = "capture-emitter"; emitter.name = "Warm glowing sphere";
    emitter.position = view->position+view->forward*(focus*0.9F)-view->right*(focus*0.17F)-view->down*(focus*0.23F);
    emitter.size = {focus*0.12F,focus*0.12F,focus*0.12F}; emitter.color = {1,0.4F,0.1F}; emitter.emission = 6;
    if (args.contains("--offscreen-emitter")) {
      const float depth = focus*0.8F;
      const float left = depth*static_cast<float>(view->image_size.width())/(2*view->fx)+focus*0.35F;
      emitter.position = view->position+view->forward*depth-view->right*left-view->down*(focus*0.15F);
      emitter.size = {focus*0.2F,focus*0.2F,focus*0.2F};
    }
    composition.scan_shadows = probe_workflow || args.contains("--capture-emission-scan-shadows");
    composition.objects = {emitter};
    if (probe_workflow) {
      auto receiver = emitter; receiver.id = "probe-receiver"; receiver.name = "Reflective receiver";
      receiver.position = view->position+view->forward*(focus*0.7F)-view->down*(focus*0.01F);
      receiver.size = {focus*0.07F,focus*0.07F,focus*0.07F}; receiver.color = {0.6F,0.6F,0.6F};
      receiver.roughness = 0.15F; receiver.metallic = 1; receiver.emission = 0;
      composition.objects.insert(composition.objects.begin(),receiver); composition.reflections = true;
    }
    if (!viewport->setSceneComposition(composition.description()).value("ok").toBool() || !wait_for(ready)) return fail("Emission scene setup failed");
    const auto export_photo = [&](const QString& name) {
      const auto destination = QDir(root).filePath(name+".png");
      if (!viewport->exportPhoto(destination,512).value("ok").toBool() ||
          !wait_for([&] { return viewport->status().value("lastExport") == destination; })) return QImage();
      return QImage(destination);
    };
    const auto unlit = export_photo("emission-off");
    if (unlit.isNull() || !click("sceneObjectsButton")) return fail("Emission baseline unavailable");
    auto* panel = fixture->findChild<QObject*>("sceneCompositionPopup");
    auto* section = fixture->findChild<QObject*>("sceneCompositionSection");
    const auto choose = [&](int page) { return section && section->setProperty("currentIndex",page) && QMetaObject::invokeMethod(section,"activated",Q_ARG(int,page)); };
    const auto edit = [&](const char* name,const char* value) {
      auto* field = visual_item(window->contentItem(),QString::fromLatin1(name));
      return field && field->setProperty("text",QString::fromLatin1(value)) && field->setProperty("userEdited",true) && QMetaObject::invokeMethod(field,"editingFinished");
    };
    const auto native_toggle = [&](bool enabled) {
      auto* button = visual_item(window->contentItem(),"sceneCaptureEmission");
      auto* scroll = visual_item(window->contentItem(),"sceneCompositionScroll");
      if (!button || !scroll || !button->isVisible() || !button->isEnabled()) return false;
      if (!capture("emission-pointer-layout")) return false;
      const auto origin = scroll->mapToScene(QPointF());
      const auto center = button->mapToScene(QPointF(20,button->height()/2));
      const auto maximum = std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height());
      if (!scroll->setProperty("contentY",std::clamp(scroll->property("contentY").toDouble()+center.y()-origin.y()-scroll->height()/2,0.0,maximum)) ||
          !capture("emission-pointer-controls")) return false;
      const auto hit = button->mapToScene(QPointF(20,button->height()/2));
      if (!QRectF(origin,QSizeF(scroll->width(),scroll->height())).contains(hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint());
      return wait_for([&] { return ready() && current().value("captureEmission").toBool() == enabled; });
    };
    if (!panel || !wait_for([&] { return panel->property("opened").toBool(); }) || !choose(2) ||
        !native_toggle(true) || !viewport->status().value("metrics").toMap().contains("captureEmissionModel") || !capture("emission-on-view"))
      return fail("Native captured emission toggle failed");
    if (probe_workflow) {
      const auto m = viewport->status().value("metrics").toMap();
      if (!m.value("reflectionProbeReused").toBool() &&
          (m.value("scanProbeCaptureLightingEvaluations").toInt() != 1 || m.value("scanProbeCaptureLightingCacheHits").toInt() != 5)) return fail("Probe faces did not reuse completed capture lighting");
    }
    if (!click("sceneCompositionUndoButton") || !wait_for([&] { return ready() && !current().value("captureEmission").toBool(); }) ||
        !click("sceneCompositionRedoButton") || !wait_for([&] { return ready() && current().value("captureEmission").toBool(); })) return fail("Emission undo/redo failed");
    const auto native_sampling = [&](int index) {
      auto* combo = visual_item(window->contentItem(),"sceneCaptureEmissionSampling");
      auto* scroll = visual_item(window->contentItem(),"sceneCompositionScroll");
      if (!combo || !scroll || !combo->isVisible()) return false;
      const auto origin = scroll->mapToScene(QPointF());
      const auto center = combo->mapToScene(QPointF(combo->width()/2,combo->height()/2));
      const auto maximum = std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height());
      scroll->setProperty("contentY",std::clamp(scroll->property("contentY").toDouble()+center.y()-origin.y()-scroll->height()/2,0.0,maximum));
      if (!capture("emission-sampling-controls")) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,combo->mapToScene(QPointF(combo->width()/2,combo->height()/2)).toPoint());
      QQuickItem* option_item = nullptr;
      if (!wait_for([&] { option_item = visual_item(window->contentItem(),QStringLiteral("sceneCaptureEmissionSamplingOption%1").arg(index)); return option_item && option_item->isVisible(); })) return false;
      const auto hit = option_item->mapToScene(QPointF(option_item->width()/2,option_item->height()/2));
      if (!QRectF(0,0,window->width(),window->height()).contains(hit)) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hit.toPoint());
      return wait_for([&] { return ready() && current().value("schema") == "looksy-scene-composition-v15" && current().value("captureEmissionSampling") == (index ? "bounds-mixture" : "cosine"); });
    };
    if (!native_sampling(0) || !native_sampling(1)) return fail("Native emission sampling selection failed");
    auto* mode = visual_item(window->contentItem(),"sceneCaptureLightingMode");
    auto* choice = visual_item(window->contentItem(),"sceneCaptureEmission");
    if (!mode || !choice || !mode->setProperty("currentIndex",0) || !QMetaObject::invokeMethod(mode,"activated",Q_ARG(int,0)) ||
        !wait_for(ready) || choice->isEnabled() || !current().value("captureEmission").toBool() ||
        !mode->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(mode,"activated",Q_ARG(int,1)) || !wait_for(ready) || !choice->isEnabled())
      return fail("Captured appearance discarded emission preference");
    auto* quality = visual_item(window->contentItem(),"sceneLightSamples");
    if (!quality || !quality->isVisible() || !quality->setProperty("currentIndex",1) || !QMetaObject::invokeMethod(quality,"activated",Q_ARG(int,1)) ||
        !wait_for(ready) || current().value("lightSamples").toInt() != 32 || !edit("sceneEffect_power","0.7") || !wait_for(ready) ||
        !edit("sceneEffect_power","0.8") || !quality->setProperty("currentIndex",2) || !QMetaObject::invokeMethod(quality,"activated",Q_ARG(int,2)) ||
        !choose(3) || !edit("sceneEffect_distance","20") || !wait_for(ready) || current().value("schema") != "looksy-scene-composition-v15" ||
        !current().value("captureEmission").toBool()) return fail("Other edits discarded emission settings");
    QMetaObject::invokeMethod(panel,"close");
    const auto lit = export_photo("emission-on");
    if (lit.isNull() || lit.size() != unlit.size()) return fail("Emission photograph failed");
    int changed = 0, maximum_delta = 0;
    for (int y = 0; y < lit.height(); ++y) for (int x = 0; x < lit.width(); ++x) {
      const auto a = unlit.pixelColor(x,y), b = lit.pixelColor(x,y);
      const int delta = std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
      if (delta > 1) ++changed;
      maximum_delta = std::max(maximum_delta,delta);
    }
    if (changed < 8) return fail("Emission did not visibly light captured colors");
    const auto accepted = current();
    const auto frozen_photo = QDir(root).filePath("emission-frozen-photo.png");
    if (!viewport->exportPhoto(frozen_photo,512).value("ok").toBool()) return fail("Frozen emission photo did not start");
    auto edited = accepted; auto capture_settings = edited.value("captureLighting").toMap(); capture_settings["power"] = 0.6; edited["captureLighting"] = capture_settings;
    if (!viewport->setSceneComposition(edited).value("ok").toBool() || !wait_for([&] { return viewport->status().value("lastExport") == frozen_photo; })) return fail("Concurrent photo edit failed");
    QFile photo_receipt(frozen_photo+".camera.json");
    if (!photo_receipt.open(QIODevice::ReadOnly) || QJsonDocument::fromJson(photo_receipt.readAll()).toVariant().toMap().value("sceneObjects").toMap() != accepted ||
        QImage(frozen_photo) != lit || !viewport->setSceneComposition(accepted).value("ok").toBool() || !wait_for(ready)) return fail("Photo recipe changed during export");
    const auto a = viewport->saveView("Emission A"); viewport->orbit(2,0); const auto b = viewport->saveView("Emission B");
    if (!a.value("ok").toBool() || !b.value("ok").toBool() ||
        !viewport->saveCameraPath({{"viewIds",QVariantList{a.value("id"),b.value("id")}}, {"segmentSeconds",0.5}, {"fps",24}}).value("ok").toBool()) return fail("Emission camera path failed");
    const auto sequence = QDir(root).filePath("emission-frames");
    if (!viewport->exportFrameSequence(sequence,256).value("ok").toBool()) return fail("Emission sequence did not start");
    edited = accepted; edited["captureEmission"] = false;
    if (!viewport->setSceneComposition(edited).value("ok").toBool() || !wait_for([&] { return !viewport->status().value("filmExporting").toBool(); }) ||
        !viewport->status().value("filmResult").toMap().value("ok").toBool()) return fail("Frozen emission sequence failed");
    QFile sequence_receipt(QDir(sequence).filePath("camera-path.json"));
    if (!sequence_receipt.open(QIODevice::ReadOnly)) return fail("Emission sequence receipt missing");
    const auto frozen_sequence = QJsonDocument::fromJson(sequence_receipt.readAll()).toVariant().toMap();
    if (frozen_sequence.value("sceneObjects").toMap() != accepted || frozen_sequence.value("frameCount").toInt() != 12) return fail("Concurrent edit changed emission sequence");
    if (!viewport->setSceneComposition(accepted).value("ok").toBool() || !wait_for(ready)) return fail("Cannot restore accepted emission");
    viewport->setSource({});
    if (!controller.openProject(project).value("ok").toBool() || !wait_for([&] { return !controller.snapshot().value("busy").toBool(); })) return fail("Emission project reopen failed");
    viewport->setSource(controller.snapshot());
    if (!wait_for([&] { return ready() && current() == accepted; }) || !viewport->restoreView(a.value("id").toString()).value("ok").toBool() ||
        !wait_for([&] { return ready() && current() == accepted; })) return fail("Emission reopen/shot recall failed");
    window->resize(1024,760);
    if (!click("sceneObjectsButton") || !wait_for([&] { return panel->property("opened").toBool(); }) || !choose(2) ||
        !native_toggle(false) || !native_toggle(true) || !capture("emission-compact")) return fail("Compact emission pointer control failed");
    if (!qml_warnings.isEmpty()) { for (const auto& warning : qml_warnings) std::cerr << warning.toStdString() << '\n'; return fail("Emission controls emitted QML warnings"); }
    if (!evidence.isEmpty() && !write(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(QVariantMap{
        {"passed",true},{"initialCount",initial_count},{"acceptedComposition",accepted},{"metrics",viewport->status().value("metrics")},
        {"waitLimitMs",wait_limit_ms},{"timingGateQualified",wait_limit_ms == 20000},
        {"probeCacheWorkflow",probe_workflow},
        {"changedCapturedPixels",changed},{"maximumPhotoChannelDelta",maximum_delta},{"sequencePath",sequence},
        {"checks",QStringList{"native-emission-toggle","undo-redo","captured-mode-retention","sample-effect-retention","frozen-photo","frozen-12-frame-sequence","global-reopen-recall","compact-native-toggle"}},
        {"scope","Native captured-color emission workflow and export freezing; not full physical relighting, photographic parity or sustained foreground latency"}}).toJson())) return 2;
    return 0;
  }
