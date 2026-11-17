    if (source_project.isEmpty()) return fail("Photo navigation requires a real imported project");
    const auto views=viewport->status().value("savedViews").toList();
    QString view_id;
    const auto wanted=option("--view-name").isEmpty() ? QStringLiteral("film-4k") : option("--view-name");
    for (const auto& value:views) if (value.toMap().value("name")==wanted) view_id=value.toMap().value("id").toString();
    if (view_id.isEmpty() || !viewport->restoreView(view_id).value("ok").toBool() ||
        !wait_for([&] { return viewport->status().value("previewPhase")=="photo"; })) return fail("Cannot restore the study photograph");
    const auto source_photo=viewport->status().value("photoSettings").toMap();
    if (source_photo.value("lens").toMap().value("samples").toInt()<64 ||
        source_photo.value("film").toMap().value("schema")!="framehouse-film-plane-v2" || !capture("before-motion"))
      return fail("Study requires high-quality optics and film settings");
    QElapsedTimer elapsed; elapsed.start();
    QVariantList publications;
    int previous=viewport->status().value("publishedFrames").toInt();
    bool foreground=true,dragging=false;
    int live_frames=0;
    qint64 previous_live=-1,first_live=-1,max_live_gap=0,first_refined=-1,release_at=-1;
    std::vector<qint64> gaps;
    const auto connection=QObject::connect(window,&QQuickWindow::frameSwapped,window,[&] {
      foreground=foreground && window->isActive() && window->isExposed() && QGuiApplication::applicationState()==Qt::ApplicationActive;
      const auto status=viewport->status(); const int count=status.value("publishedFrames").toInt();
      if (count==previous) return;
      previous=count;
      const auto phase=status.value("publishedPhase").toString(); const auto metrics=status.value("metrics").toMap();
      const auto now=elapsed.elapsed();
      publications.append(QVariantMap{{"elapsedMs",now},{"phase",phase},{"metrics",metrics},{"dragging",dragging}});
      if (dragging && phase=="live") {
        ++live_frames;
        if (first_live<0) first_live=now;
        if (previous_live>=0) { gaps.push_back(now-previous_live); max_live_gap=std::max(max_live_gap,now-previous_live); }
        previous_live=now;
      }
      if (release_at>=0 && first_refined<0 && phase!="live" && metrics.value("filmEffectsApplied").toBool() &&
          metrics.value("apertureSamples").toInt()>=4) first_refined=now-release_at;
    });
    const auto start=viewport->mapToScene({viewport->width()*0.5,viewport->height()*0.5}).toPoint();
    dragging=true;
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
    for (int step=1;step<=180;++step) {
      const double time=static_cast<double>(step)/180;
      const QPoint point=start+QPoint(qRound(90*std::sin(time*6.283185307179586)+45*time),qRound(24*std::sin(time*3.141592653589793)));
      QTest::mouseMove(window,point,16);
    }
    release_at=elapsed.elapsed(); dragging=false;
    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(45,0));
    const bool settled=wait_for([&] { return viewport->status().value("previewPhase")=="photo" && first_refined>=0; });
    const auto complete_ms=elapsed.elapsed()-release_at;
    QObject::disconnect(connection);
    const auto final_metrics=viewport->status().value("metrics").toMap();
    if (!settled || live_frames<2 || final_metrics.value("resolvedSourceReused").toBool() ||
        viewport->status().value("photoSettings").toMap()!=source_photo ||
        !studio_geometry_valid() || !qml_warnings.isEmpty() || !capture("after-refinement"))
      return fail("Move-stop photo workflow did not retain optics, film and layout");
    for (const auto& value:publications) {
      const auto row=value.toMap(); if (!row.value("dragging").toBool() || row.value("phase")!="live") continue;
      const auto metrics=row.value("metrics").toMap();
      if (metrics.value("apertureSamples").toInt()!=1 || metrics.value("filmEffectsApplied").toBool() ||
          std::max(metrics.value("width").toInt(),metrics.value("height").toInt())>720)
        return fail("Moving preview did not switch to the bounded Gaussian draft");
    }
    std::sort(gaps.begin(),gaps.end());
    const qint64 p95=gaps.empty() ? 0 : gaps[static_cast<size_t>(std::ceil(0.95*static_cast<double>(gaps.size())))-1];
    const double fps=previous_live>first_live ? 1000.0*static_cast<double>(live_frames-1)/
        static_cast<double>(previous_live-first_live) : 0;
    const bool performance_pass=foreground && fps>=30 && p95<=50 && first_refined<=500 && complete_ms<=3000;
    // Capture the actual moving-quality image outside the timed drag, without
    // changing the pose used by the subsequent physical-control checks.
    const int before_draft=viewport->status().value("publishedFrames").toInt();
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
    QTest::mouseMove(window,start+QPoint(1,0),20);
    QTest::mouseMove(window,start,20);
    const bool draft_ready=wait_for([&] { return viewport->status().value("publishedFrames").toInt()>before_draft &&
        viewport->status().value("publishedPhase")=="live"; });
    const auto draft_metrics=viewport->status().value("metrics").toMap();
    const bool draft_captured=draft_ready && capture("live-draft");
    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start);
    if (!draft_captured || !wait_for([&] { return viewport->status().value("previewPhase")=="photo"; }))
      return fail("Cannot capture the actual navigation draft and restore the photograph");
    QVariantMap physical_controls;
    if (args.contains("--physical-camera-gestures")) {
      const auto starting_camera=final_metrics.value("camera").toMap();
      if (!starting_camera.contains("physicalCalibration")) return fail("Physical gestures require a dimensioned study camera");
      const auto same_pose=[&](const QVariantMap& camera) {
        for (const auto* key:{"position","right","down","forward"}) {
          const auto a=starting_camera.value(QLatin1String(key)).toList(),b=camera.value(QLatin1String(key)).toList();
          if (a.size()!=3 || b.size()!=3) return false;
          for (qsizetype i=0;i<3;++i) if (std::abs(a[i].toDouble()-b[i].toDouble())>1e-6) return false;
        }
        return true;
      };
      const auto ready=[&] { return viewport->status().value("previewPhase")=="photo"; };
      auto* panel=fixture->findChild<QObject*>("scenePhotoControlsPopup");
      if (!panel || !QMetaObject::invokeMethod(panel,"open") || !panel->setProperty("page",4)) return fail("Physical inspector missing");
      const auto show=[&](const char* name) -> QQuickItem* {
        QTest::qWait(30);
        auto* item=visual_item(window->contentItem(),name);
        auto* scroll=visual_item(window->contentItem(),"scenePhotoControlsScroll");
        if (!item || !scroll || !item->isVisible()) return nullptr;
        const auto top=item->mapToItem(scroll,QPointF()).y()+scroll->property("contentY").toDouble();
        scroll->setProperty("contentY",std::clamp(top-scroll->height()/3,0.0,std::max(0.0,scroll->property("contentHeight").toDouble()-scroll->height())));
        QTest::qWait(30); return item;
      };
      const auto enter=[&](const char* name,const QString& text) {
        auto* field=show(name); if (!field) return false;
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,field->mapRectToScene(field->boundingRect()).center().toPoint());
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        for (const auto ch:text) QTest::keyClick(window,ch.toLatin1());
        QTest::keyClick(window,Qt::Key_Return);
        return wait_for(ready) && field->property("text").toString().toDouble()==text.toDouble();
      };
      const auto initial_optics=viewport->describeCameraOptics();
      if (!enter("scenePhysicalFocalField","135") || !same_pose(viewport->status().value("metrics").toMap().value("camera").toMap()) ||
          viewport->describeCameraOptics().value("horizontalFovDegrees").toDouble()>=initial_optics.value("horizontalFovDegrees").toDouble() ||
          !capture("physical-zoom-135mm")) return fail("Physical zoom moved the camera or failed to narrow the field");
      auto* zoom=show("scenePhysicalZoomSlider"); if (!zoom) return fail("Physical zoom slider missing");
      const auto center=zoom->mapRectToScene(zoom->boundingRect()).center().toPoint();
      QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,center);
      QTest::mouseMove(window,center+QPoint(20,0),20);
      QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,center+QPoint(20,0));
      if (!wait_for(ready) || !same_pose(viewport->status().value("metrics").toMap().value("camera").toMap()))
        return fail("Continuous focal-length slider changed the world pose");
      if (!enter("sceneLensFocusField","8") || !same_pose(viewport->status().value("metrics").toMap().value("camera").toMap()) ||
          std::abs(viewport->describeCameraOptics().value("focusFromLensM").toDouble()-8)>1e-6 || !capture("physical-background-focus"))
        return fail("Background focus moved the camera or failed to place the focal plane");
      physical_controls={{"passed",true},{"posePreserved",true},{"nativeFocalField",true},{"nativeZoomSlider",true},
          {"nativeBackgroundFocus",true},{"optics",viewport->describeCameraOptics()}};
    }
    const QVariantMap receipt{{"passed",true},{"performanceGatePassed",performance_pass},{"foregroundThroughout",foreground},
        {"nativeForegroundAtEnd",native_window_foreground_sample(window)},{"liveFrames",live_frames},{"liveFps",fps},
        {"liveGapP95Ms",p95},{"liveGapMaximumMs",max_live_gap},{"firstStyledAfterReleaseMs",first_refined},{"completeAfterReleaseMs",complete_ms},
        {"finalMetrics",final_metrics},
        {"draftMetrics",draft_metrics},
        {"physicalControls",physical_controls},
        {"pointCount",viewport->status().value("pointCount")},{"publications",publications},
        {"scope","Qt pointer drag to published frame-swap observation on a real public capture; not physical input-to-photon latency or 4K export timing"}};
    if (!evidence.isEmpty() && !write(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(receipt).toJson())) return 2;
    std::cout<<QJsonDocument::fromVariant(receipt).toJson().constData(); return 0;
