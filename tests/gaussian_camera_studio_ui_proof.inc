    if (!source_project.isEmpty()) return fail("Studio scale fixture requires synthetic metre geometry, not a private capture");
    auto* settings=fixture->findChild<QObject*>("sceneWorkspaceSettings");
    auto* camera_panel=fixture->findChild<QObject*>("scenePhotoControlsPopup");
    if (!settings || !camera_panel) return fail("Studio settings or camera inspector missing");
    const auto ready=[&] { return viewport->status().value("previewPhase")=="photo" && studio_geometry_valid(); };
    const auto show_item=[&](const char* name) -> QQuickItem* {
      QCoreApplication::processEvents();
      QTest::qWait(30); // Allow the new page's Qt Quick layout polish before routing input.
      auto* item=visual_item(window->contentItem(),name);
      if (QString::fromLatin1(name)==QStringLiteral("sceneLensFocusField")) {
        // The controls popup keeps other pages alive. Resolve the visible, usable instance
        // instead of trusting the first matching objectName in the visual tree.
        for (auto* candidate:visual_items(window->contentItem(),QString::fromLatin1(name))) {
          if (candidate->isVisible() && candidate->isEnabled() && candidate->width()>0 && candidate->height()>0) {
            item=candidate;
            break;
          }
        }
      }
      auto* scroll=visual_item(window->contentItem(),"scenePhotoControlsScroll");
      if (!item || !scroll || !item->isVisible()) { std::cerr << "Unavailable camera control: " << name << '\n'; return nullptr; }
      const double top=item->mapToItem(scroll,QPointF()).y()+scroll->property("contentY").toDouble();
      scroll->setProperty("contentY",std::max(0.0,std::min(top-scroll->height()/3,
          scroll->property("contentHeight").toDouble()-scroll->height())));
      QCoreApplication::processEvents();
      QTest::qWait(30);
      // Mode changes and scrolling can require another layout polish. Wait
      // for the actual clipping geometry before routing pointer input.
      const auto inside_scroll = [&] {
        window->update();
        return item->isVisible() && item->width() > 0 && item->height() > 0 &&
            scroll->mapRectToScene(scroll->boundingRect()).contains(
                item->mapRectToScene(item->boundingRect()));
      };
      if (!wait_for(inside_scroll)) {
        const auto bounds=item->mapRectToScene(item->boundingRect());
        std::cerr << "Camera control outside scroll: " << name << " bounds=" << bounds.x() << ',' << bounds.y() << ',' << bounds.width() << ',' << bounds.height() << " viewport=" << scroll->width() << ',' << scroll->height() << '\n';
        return nullptr;
      }
      return item;
    };
    const auto enter=[&](const char* name,const QString& text) {
      const bool focus_field=QString::fromLatin1(name)==QStringLiteral("sceneLensFocusField");
      auto* item=show_item(name);
      if (!item) {
        if (focus_field) {
          auto* field=visual_item(window->contentItem(),QString::fromLatin1(name));
          auto* scroll=visual_item(window->contentItem(),"scenePhotoControlsScroll");
          std::cerr << "Focus entry geometry stage: item=" << (field!=nullptr)
                    << " visible=" << (field && field->isVisible())
                    << " enabled=" << (field && field->isEnabled())
                    << " width=" << (field ? field->width() : 0)
                    << " height=" << (field ? field->height() : 0)
                    << " scrollY=" << (scroll ? scroll->property("contentY").toDouble() : -1) << '\n';
        }
        return false;
      }
      if (!item->isEnabled()) {
        if (focus_field) std::cerr << "Focus entry geometry stage: field is disabled; activeFocus=" << item->hasActiveFocus() << '\n';
        return false;
      }
      const QPointF local_center(item->width()/2.0,item->height()/2.0);
      const auto click_point=item->mapToItem(window->contentItem(),local_center).toPoint();
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,click_point);
      if (!wait_for([&] { return item->hasActiveFocus(); })) {
        if (focus_field) {
          const auto bounds=item->mapRectToScene(QRectF(0,0,item->width(),item->height()));
          auto* parent=item->parentItem();
          auto* focus_item=window->activeFocusItem();
          std::cerr << "Focus entry click stage: activeFocus=" << item->hasActiveFocus()
              << " activeFocusOnTab=" << item->property("activeFocusOnTab").toBool()
              << " enabled=" << item->isEnabled() << " visible=" << item->isVisible()
              << " type=" << item->metaObject()->className()
              << " geometry=" << item->width() << 'x' << item->height()
              << " boundingRect=" << item->boundingRect().x() << ',' << item->boundingRect().y() << ','
              << item->boundingRect().width() << ',' << item->boundingRect().height()
              << " sceneRect=" << bounds.x() << ',' << bounds.y() << ',' << bounds.width() << ',' << bounds.height()
              << " clickPoint=" << click_point.x() << ',' << click_point.y()
              << " parent=" << (parent ? parent->objectName().toStdString() : std::string("<none>"))
              << ':' << (parent ? parent->metaObject()->className() : "<none>")
              << " focusItem=" << (focus_item ? focus_item->objectName().toStdString() : std::string("<none>"))
              << ':' << (focus_item ? focus_item->metaObject()->className() : "<none>") << '\n';
          const auto matches=visual_items(window->contentItem(),QString::fromLatin1(name));
          for (auto* match:matches) {
            const auto rect=match->mapRectToScene(QRectF(0,0,match->width(),match->height()));
            std::cerr << "Focus entry candidate: type=" << match->metaObject()->className()
                << " visible=" << match->isVisible() << " enabled=" << match->isEnabled()
                << " geometry=" << match->width() << 'x' << match->height()
                << " sceneRect=" << rect.x() << ',' << rect.y() << ',' << rect.width() << ',' << rect.height()
                << " parent=" << (match->parentItem() ? match->parentItem()->objectName().toStdString() : std::string("<none>")) << '\n';
          }
        }
        return false;
      }
      QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
      for (const auto character:text) QTest::keyClick(window,character.toLatin1());
      QTest::keyClick(window,Qt::Key_Return);
      const bool matched=wait_for([&] { return item->property("text").toString().toDouble()==text.toDouble(); });
      if (focus_field && !matched)
        std::cerr << "Focus entry commit stage: requested=" << text.toStdString()
                  << " actual=" << item->property("text").toString().toStdString()
                  << " activeFocus=" << item->hasActiveFocus()
                  << " acceptableInput=" << item->property("acceptableInput").toBool()
                  << " userEdited=" << item->property("userEdited").toBool()
                  << " cameraFocusM=" << viewport->status().value("physicalCamera").toMap().value("focusDistanceM").toDouble()
                  << " actionError=" << workspace->property("actionError").toString().toStdString() << '\n';
      return matched;
    };
    const auto press=[&](const char* name) {
      auto* item=show_item(name); if (!item || !item->isEnabled()) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapRectToScene(item->boundingRect()).center().toPoint());
      return true;
    };
    const auto set_advanced_camera=[&](bool visible) {
      if (camera_panel->property("advancedVisible").toBool()==visible) return true;
      if (!press("scenePhotoAdvancedToggle")) return false;
      return wait_for([&] { return camera_panel->property("advancedVisible").toBool()==visible; });
    };
    const auto press_anywhere=[&](const char* name) {
      QCoreApplication::processEvents();
      auto* item=visual_item(window->contentItem(),name); if (!item || !item->isVisible() || !item->isEnabled()) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapRectToScene(item->boundingRect()).center().toPoint());
      return true;
    };
    const auto choose=[&](const char* name,int selected) {
      auto* item=show_item(name); if (!item) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapRectToScene(item->boundingRect()).center().toPoint());
      const auto option_name=QString::fromLatin1(name)+"Option"+QString::number(selected);
      QQuickItem* option_item=nullptr;
      if (!wait_for([&] { option_item=visual_item(window->contentItem(),option_name); return option_item && option_item->isVisible(); })) return false;
      QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,option_item->mapRectToScene(option_item->boundingRect()).center().toPoint());
      auto* menu=item->property("popup").value<QObject*>();
      return menu && wait_for([&] { return item->property("currentIndex").toInt()==selected && !menu->property("visible").toBool(); });
    };
    if (args.contains("--focus-control-proof")) {
      const QVariantMap calibration{{"schema","framehouse-physical-thin-lens-v1"},{"focalLengthMm",50.0},
          {"fNumber",1.4},{"focusDistanceM",3.0},{"metresPerSceneUnit",1.0},
          {"sensorWidthMm",23.5},{"sensorHeightMm",15.6}};
      if (!QMetaObject::invokeMethod(camera_panel,"open") || !camera_panel->setProperty("page",4) ||
          !camera_panel->setProperty("renderDetailsExpanded",true) ||
          !wait_for(ready) || !viewport->setPhysicalCamera(calibration).value("ok").toBool() || !wait_for(ready))
        return fail("Focused focus-control fixture could not apply its physical camera");
      auto photo=viewport->status().value("photoSettings").toMap();
      auto lens=photo.value("lens").toMap();
      lens.insert("schema","framehouse-thin-lens-v7");
      lens.insert("samples",16);
      lens.insert("apertureEdge",0); lens.insert("vignettingDistance",0); lens.insert("vignettingScale",1);
      lens.insert("sphericalDistortion",0); lens.insert("astigmatism",0); lens.insert("fieldCurvature",0);
      lens.insert("focusMode","manual"); lens.insert("bokehAspect",1);
      lens.insert("rayTraced",true); lens.insert("pupilSoftness",0);
      photo.insert("schema","framehouse-gaussian-photo-settings-v5");
      photo.insert("lens",lens);
      photo.insert("film",QVariantMap{{"schema","framehouse-film-plane-v2"},{"grainAmount",0.08},{"grainSize",1.5},
          {"halationAmount",0.2},{"halationRadius",4},{"halationThreshold",0.8},{"seed",2},
          {"grainMicrons",12},{"halationMicrons",80},{"formatWidthMm",36},{"formatHeightMm",24},
          {"returnRgb",QVariantList{1.0,0.15,0.025}}});
      photo.insert("post",QVariantMap{{"schema","framehouse-camera-post-v1"},{"bloomAmount",0},{"bloomRadius",8},
          {"glareAmount",0},{"glareRadius",32},{"glareRays",4},{"glareRotation",0},{"spectralIntensity",0},
          {"threshold",0.8},{"chromaticAberration",0},{"saturation",0.9}});
      photo.insert("tone",QVariantMap{{"schema","framehouse-camera-tone-v1"},{"contrast",1.15},{"highlightCompression",0}});
      if (!viewport->setPhotoSettings(photo).value("ok").toBool() || !wait_for(ready))
        return fail("Focused focus-control fixture could not restore its film, post and tone context");
      auto* ray_switch=visual_item(window->contentItem(),"sceneRayTracedOpticsToggle");
      auto* pupil_softness=visual_item(window->contentItem(),"scenePupilSoftnessCombo");
      auto* advanced_toggle=visual_item(window->contentItem(),"scenePhotoAdvancedToggle");
      auto* ray_controls=visual_item(window->contentItem(),"sceneRayTracedControlRow");
      auto* focus_field=visual_item(window->contentItem(),"sceneLensFocusField");
      if (!ray_switch || !pupil_softness || !advanced_toggle || !ray_controls || !focus_field ||
          camera_panel->property("advancedVisible").toBool() || ray_controls->isVisible() ||
          !focus_field->isVisible() || !advanced_toggle->isVisible() || advanced_toggle->property("active").toBool())
        return fail("Focused proof did not start with essential focus controls and collapsed advanced optics");
      if (!set_advanced_camera(true) ||
          !wait_for([&] { return camera_panel->property("advancedVisible").toBool() && ray_controls->isVisible(); }))
        return fail("Focused proof could not reveal advanced optics through its visible control");
      auto* camera_column = ray_controls->parentItem();
      auto* camera_scroll = visual_item(window->contentItem(), "scenePhotoControlsScroll");
      bool camera_widths_fit = true;
      for (auto* child : camera_column->childItems()) {
        if (!child->isVisible()) continue;
        if (child->width() > camera_scroll->width() + 0.5) {
          camera_widths_fit = false;
          std::cerr << "Inspector row overflow: " << child->objectName().toStdString()
                    << " type=" << child->metaObject()->className()
                    << " width=" << child->width() << " implicit=" << child->implicitWidth()
                    << " min=" << QQmlProperty::read(child, "Layout.minimumWidth").toDouble()
                    << " column=" << camera_column->width() << " scroll=" << camera_scroll->width() << '\n';
          for (auto* part : child->childItems())
            if (part->isVisible()) std::cerr << "  child=" << part->objectName().toStdString()
                << " text=" << part->property("text").toString().toStdString()
                << " implicit=" << part->implicitWidth()
                << " min=" << QQmlProperty::read(part, "Layout.minimumWidth").toDouble() << '\n';
        }
      }
      if (!camera_widths_fit) return fail("Advanced camera rows must fit the inspector content width");
      const auto saved_ray_view=viewport->saveView("Saved ray-traced camera");
      if (!saved_ray_view.value("ok").toBool() || !ray_switch->isVisible() || !pupil_softness->isVisible() ||
          !ray_switch->property("checked").toBool() || !pupil_softness->isEnabled())
        return fail("Focused proof could not save a visible ray-traced camera state");
      if (!press("sceneRayTracedOpticsToggle") ||
          !wait_for([&] { return !viewport->status().value("lensSettings").toMap().value("rayTraced").toBool() && ready(); }) ||
          viewport->status().value("lensSettings").toMap().value("rayTraced").toBool() ||
          ray_switch->property("checked").toBool() || pupil_softness->isEnabled())
        return fail("Focused proof could not turn ray-traced optics off");
      if (!viewport->restoreView(saved_ray_view.value("id").toString()).value("ok").toBool() ||
          !wait_for([&] { return viewport->status().value("lensSettings").toMap().value("rayTraced").toBool() && ready(); }) ||
          !ray_switch->property("checked").toBool() || !pupil_softness->isEnabled())
        return fail("Restoring a ray-traced camera did not restore its switch and bokeh controls");
      if (!set_advanced_camera(false) ||
          !wait_for([&] { return !camera_panel->property("advancedVisible").toBool() && !ray_controls->isVisible(); }) ||
          !focus_field->isVisible())
        return fail("Focused proof could not return to the essential camera controls after optics persistence");
      if (!press("sceneQuickAutofocusButton") || !wait_for(ready) ||
          viewport->status().value("lensSettings").toMap().value("focusMode")!="auto-centre" ||
          !press("sceneQuickAutofocusButton") || !wait_for(ready) ||
          viewport->status().value("lensSettings").toMap().value("focusMode")=="auto-centre")
        return fail("Focused focus-control fixture could not cycle autofocus and lock focus");
      if (!press("sceneQuickPickFocusButton") || !workspace->property("focusPickArmed").toBool())
        return fail("Focused focus-control fixture could not arm subject picking");
      QTest::keyClick(window,Qt::Key_Escape);
      QCoreApplication::processEvents();
      QTest::qWait(30);
      if (workspace->property("focusPickArmed").toBool() || !camera_panel->property("opened").toBool())
        return fail("Focused focus-control fixture Escape did not cancel picking");
      if (!enter("sceneLensFocusField","3") || !wait_for(ready))
        return fail("Focused focus-control fixture could not re-enter the existing distance");
      if (std::abs(viewport->status().value("physicalCamera").toMap().value("focusDistanceM").toDouble()-3)>1e-5)
        return fail("Focused focus-control fixture changed the existing distance");
      if (!enter("sceneLensFocusField","3.25") || !wait_for(ready))
        return fail("Focused focus-control fixture could not enter a changed distance");
      if (std::abs(viewport->status().value("physicalCamera").toMap().value("focusDistanceM").toDouble()-3.25)>1e-5)
        return fail("Focused focus-control fixture entered a different distance");
      if (!press("sceneFocusFartherButton") || !wait_for(ready) ||
          std::abs(viewport->status().value("physicalCamera").toMap().value("focusDistanceM").toDouble()-3.315)>1e-5)
        return fail("Focused focus-control fixture farther step failed");
      if (!press("sceneFocusNearerButton") || !wait_for(ready) ||
          std::abs(viewport->status().value("physicalCamera").toMap().value("focusDistanceM").toDouble()-3.25)>1e-5)
        return fail("Focused focus-control fixture nearer step failed");
      std::cout << "{\"passed\":true,\"check\":\"native focus cancellation, exact field and fine steppers\"}\n";
      return 0;
    }
    if (!QMetaObject::invokeMethod(camera_panel,"open") || !wait_for(ready) ||
        !camera_panel->setProperty("page",4) || !capture("studio-camera-start")) return fail("Studio camera layout did not open");
    auto* advanced_sections=fixture->findChild<QQuickItem*>("scenePhotoSettingsSectionCombo");
    auto* camera_scroll=fixture->findChild<QQuickItem*>("scenePhotoControlsScroll");
    const QStringList essential_camera_controls{"scenePhotoAdvancedToggle","sceneQuickAutofocusButton","sceneQuickPickFocusButton",
        "sceneResetPhotoButton","sceneFocusControlCard","sceneLensFocusField","sceneFocusNearerButton","sceneFocusFartherButton",
        "sceneFocalControlCard","sceneUncalibratedFocalStepper","sceneFocalValue","sceneFocalDecreaseButton","sceneFocalIncreaseButton",
        "sceneApertureControlCard","sceneSceneApertureValue","sceneApertureDecreaseButton","sceneApertureIncreaseButton",
        "sceneExposureControlCard","scenePrimaryExposureStepper","sceneExposureValue","sceneExposureDecreaseButton","sceneExposureIncreaseButton"};
    const QStringList advanced_camera_controls{"sceneCameraOpticsReadout","sceneCameraDepthOfFieldReadout","sceneFocusDistanceSlider",
        "sceneRayTracedControlRow","sceneRayTracedOpticsToggle","sceneRayTracedQualityCombo","scenePupilSoftnessCombo",
        "scenePrimaryBloomStepper","sceneBloomIncreaseButton","scenePrimaryHalationStepper","sceneHalationIncreaseButton",
        "sceneCameraStartingPointsRow","sceneLensStartingPoints","sceneFilmStartingPoints","scenePhotoSettingsSectionCombo"};
    QCoreApplication::processEvents();
    QTest::qWait(30);
    auto* advanced_toggle=visual_item(window->contentItem(),"scenePhotoAdvancedToggle");
    auto* ray_control_row=visual_item(window->contentItem(),"sceneRayTracedControlRow");
    auto* bloom_stepper=visual_item(window->contentItem(),"scenePrimaryBloomStepper");
    auto* halation_stepper=visual_item(window->contentItem(),"scenePrimaryHalationStepper");
    auto* camera_starting_points=visual_item(window->contentItem(),"sceneCameraStartingPointsRow");
    if (!advanced_sections || advanced_sections->isVisible() || !advanced_toggle || !advanced_toggle->isVisible() ||
        advanced_toggle->property("active").toBool() || camera_panel->property("advancedVisible").toBool())
      return fail("Camera details should start collapsed behind the visible Advanced control");
    if (!camera_scroll) return fail("The camera settings viewport is missing");
    const auto initial_camera_viewport = camera_scroll->mapRectToScene(camera_scroll->boundingRect()).adjusted(-1,-1,1,1);
    for (const auto& name:essential_camera_controls) {
      auto* item=visual_item(window->contentItem(),name);
      if (!item || !item->isVisible()) return fail("An essential camera control is missing from the default page");
      if (!initial_camera_viewport.contains(item->mapRectToScene(item->boundingRect())))
        return fail("The essential camera controls do not fit in the initial viewport");
    }
    if (std::abs(camera_scroll->property("contentY").toDouble())>1.0)
      return fail("The camera settings page did not start at its first control");
    auto* film_model=fixture->findChild<QQuickItem*>("sceneFilmModelCombo");
    if (!film_model || film_model->isVisible())
      return fail("The film rendering model should remain in advanced settings");
    for (const auto& name:advanced_camera_controls) {
      auto* item=visual_item(window->contentItem(),name);
      if (!item || item->isVisible()) return fail("An optics, glow or look control leaked onto the default camera page");
    }
    if (!ray_control_row || !bloom_stepper || !halation_stepper || !camera_starting_points ||
        !set_advanced_camera(true) || !advanced_sections->isVisible() ||
        !wait_for([&] { return advanced_toggle->property("active").toBool() && ray_control_row->isVisible() &&
                             bloom_stepper->isVisible() && halation_stepper->isVisible() && camera_starting_points->isVisible(); }) ||
        !choose("scenePhotoSettingsSectionCombo",3) || !wait_for(ready) ||
        !film_model->isVisible() || !visual_item(window->contentItem(),"sceneFilmStartingPoints") ||
        !visual_item(window->contentItem(),"sceneFilmStartingPoints")->isVisible() ||
        !set_advanced_camera(false) ||
        !wait_for([&] { return camera_panel->property("page").toInt()==4 && !advanced_sections->isVisible(); }))
      return fail("Advanced optics, glow and look controls were not reachable through the real disclosure and section menu");
    for (const auto& name:essential_camera_controls) {
      auto* item=visual_item(window->contentItem(),name);
      if (!item || !item->isVisible()) return fail("Closing Advanced did not restore the essential camera page");
    }
    for (const auto& name:advanced_camera_controls) {
      auto* item=visual_item(window->contentItem(),name);
      if (!item || item->isVisible()) return fail("Closing Advanced left secondary camera controls exposed");
    }
    auto* workspace_item=fixture->findChild<QQuickItem*>("sceneWorkspace");
    auto* viewport_item=fixture->findChild<QQuickItem*>("gaussianSceneViewport");
    auto* navigation_surface=fixture->findChild<QQuickItem*>("sceneNavigationSurface");
    if (!workspace_item || !viewport_item || !navigation_surface)
      return fail("Camera navigation surface is missing");
    const auto camera_from_status=[&]() {
      return gaussian_saved_camera(viewport->status().value("metrics").toMap().value("camera").toMap());
    };
    const auto project_world_point=[](const GaussianCamera& camera,const QVector3D& point) {
      const QVector3D relative=point-camera.position;
      const double depth=QVector3D::dotProduct(relative,camera.forward);
      if (depth<=0.01) return QPointF(-1,-1);
      const QPointF chief(camera.cx+camera.fx*camera.photo.focal_scale*QVector3D::dotProduct(relative,camera.right)/depth,
          camera.cy+camera.fy*camera.photo.focal_scale*QVector3D::dotProduct(relative,camera.down)/depth);
      const QPointF pixel=gaussian_lens_display_pixel(camera,chief);
      return QPointF(pixel.x()/camera.image_size.width(),pixel.y()/camera.image_size.height());
    };
    const auto same_recipe_photo=viewport->status().value("photoSettings").toMap();
    const auto same_recipe_lens=viewport->status().value("lensSettings").toMap();
    const double initial_roll=viewport->status().value("navigationRollDegrees").toDouble();
    const auto initial_camera=camera_from_status();
    const QVector3D orbit_subject(-0.35F,0.0F,3.0F);
    const auto drag_camera=[&](const QPoint& start,const QPoint& delta,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
      QTest::mousePress(window,Qt::LeftButton,modifiers,start);
      for (int step=1;step<=4;++step)
        QTest::mouseMove(window,start+QPoint(delta.x()*step/4,delta.y()*step/4),2);
      QTest::mouseRelease(window,Qt::LeftButton,modifiers,start+delta);
    };
    const auto move_along=[](const GaussianCamera& from,const GaussianCamera& to,const QVector3D& axis,int direction) {
      const QVector3D delta=to.position-from.position;
      const QVector3D unit=axis.normalized();
      const float along=QVector3D::dotProduct(delta,unit)*static_cast<float>(direction);
      const QVector3D across=delta-unit*QVector3D::dotProduct(delta,unit);
      return delta.length()>0.001F && along>0.001F && across.length()<=std::max(0.001F,delta.length()*0.08F);
    };
    if (!initial_camera) return fail("Initial camera pose was unavailable for navigation proof");
    auto* look_mode_button=visual_item(window->contentItem(),"sceneCameraLookModeButton");
    auto* orbit_mode_button=visual_item(window->contentItem(),"sceneCameraOrbitModeButton");
    auto* pan_mode_button=visual_item(window->contentItem(),"sceneCameraPanModeButton");
    auto* roll_mode_button=visual_item(window->contentItem(),"sceneCameraRollModeButton");
    if (!look_mode_button || !look_mode_button->isVisible() || !look_mode_button->property("active").toBool() ||
        !orbit_mode_button || !orbit_mode_button->isVisible() || orbit_mode_button->property("active").toBool() ||
        !pan_mode_button || !pan_mode_button->isVisible() || !roll_mode_button || !roll_mode_button->isVisible() ||
        workspace_item->property("navigationOrbitMode").toBool() ||
        workspace_item->property("navigationPanMode").toBool() || workspace_item->property("navigationRollMode").toBool())
      return fail("Camera navigation should start in first-person Look mode with Orbit, Pan and Roll available separately");
    const QPoint look_start=navigation_surface->mapToScene(QPointF(viewport_item->width()*0.43,viewport_item->height()*0.47)).toPoint();
    drag_camera(look_start,QPoint(36,12));
    if (!wait_for(ready)) return fail("First-person Look drag did not settle");
    const auto looked_camera=camera_from_status();
    if (!looked_camera || (looked_camera->position-initial_camera->position).length()>1e-5F ||
        QVector3D::dotProduct(looked_camera->forward,initial_camera->forward)>0.99999F ||
        viewport->status().value("photoSettings").toMap()!=same_recipe_photo ||
        viewport->status().value("lensSettings").toMap()!=same_recipe_lens)
      return fail("Default Look drag should rotate the view in place without changing camera position or photo settings");
    QTest::keyClick(window,Qt::Key_W);
    if (!wait_for([&] { const auto current=camera_from_status();
          return ready() && current && move_along(*looked_camera,*current,looked_camera->forward,1); }))
      return fail("W should move forward along the newly aimed view direction");
    const auto moved_forward_camera=camera_from_status();
    if (!moved_forward_camera) return fail("First-person forward motion did not publish a camera pose");
    const QPoint second_look_start=navigation_surface->mapToScene(QPointF(viewport_item->width()*0.49,viewport_item->height()*0.51)).toPoint();
    drag_camera(second_look_start,QPoint(-28,16));
    if (!wait_for(ready)) return fail("Second first-person Look drag did not settle");
    const auto looked_again_camera=camera_from_status();
    if (!looked_again_camera || (looked_again_camera->position-moved_forward_camera->position).length()>1e-5F ||
        QVector3D::dotProduct(looked_again_camera->forward,moved_forward_camera->forward)>0.99999F)
      return fail("A second Look drag should turn from the moved-to position without translating it");
    QTest::keyClick(window,Qt::Key_W);
    if (!wait_for([&] { const auto current=camera_from_status();
          return ready() && current && move_along(*looked_again_camera,*current,looked_again_camera->forward,1); }))
      return fail("Forward motion after the second Look drag did not follow the new view direction");
    const auto forward_again_camera=camera_from_status();
    if (!forward_again_camera) return fail("Second forward movement did not publish a camera pose");
    QTest::keyClick(window,Qt::Key_D);
    if (!wait_for([&] { const auto current=camera_from_status();
          return ready() && current && move_along(*forward_again_camera,*current,forward_again_camera->right,1) &&
              QVector3D::dotProduct(current->forward,forward_again_camera->forward)>0.99999F; }))
      return fail("D should strafe along the current camera-right axis");
    const auto strafe_camera=camera_from_status();
    if (!strafe_camera) return fail("Camera-right movement did not publish a camera pose");
    QTest::keyClick(window,Qt::Key_E);
    if (!wait_for([&] { const auto current=camera_from_status();
          return ready() && current && move_along(*strafe_camera,*current,strafe_camera->down,1) &&
              QVector3D::dotProduct(current->forward,strafe_camera->forward)>0.99999F; }))
      return fail("E should move vertically along the current camera-down axis");
    const auto before_orbit_camera=camera_from_status();
    if (!before_orbit_camera) return fail("Camera pose before orbit mode was unavailable");
    const QPointF orbit_subject_uv=project_world_point(*before_orbit_camera,orbit_subject);
    if (orbit_subject_uv.x()<0.05 || orbit_subject_uv.x()>0.95 || orbit_subject_uv.y()<0.05 || orbit_subject_uv.y()>0.95)
      return fail("Synthetic orbit target was outside the visible camera frame after first-person movement");
    if (!press_anywhere("sceneCameraOrbitModeButton") ||
        !workspace_item->property("navigationOrbitMode").toBool() ||
        workspace_item->property("navigationPanMode").toBool() || workspace_item->property("navigationRollMode").toBool())
      return fail("Explicit Orbit mode did not switch from first-person Look");
    if (!press_anywhere("sceneCameraOrbitTargetButton")) return fail("Orbit target control is unavailable");
    const double image_width=workspace_item->property("displayedImageWidth").toDouble();
    const double image_height=workspace_item->property("displayedImageHeight").toDouble();
    const QPointF orbit_subject_local((viewport_item->width()-image_width)/2+orbit_subject_uv.x()*image_width,
        (viewport_item->height()-image_height)/2+orbit_subject_uv.y()*image_height);
    const QPoint orbit_subject_screen=navigation_surface->mapToScene(orbit_subject_local).toPoint();
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,orbit_subject_screen);
    if (!wait_for([&] {
          const auto status=viewport->status();
          return !status.value("focusPicking").toBool() && !status.value("orbitPicking").toBool() &&
              status.value("orbitTarget").toList().size()==3;
        }) || viewport->status().value("photoSettings").toMap()!=same_recipe_photo ||
        viewport->status().value("lensSettings").toMap()!=same_recipe_lens ||
        !workspace_item->property("navigationOrbitMode").toBool())
      return fail("Picking an orbit target did not retain Orbit mode and preserve the camera photo recipe");
    const QPoint orbit_drag_start=navigation_surface->mapToScene(QPointF(viewport_item->width()*0.55,viewport_item->height()*0.53)).toPoint();
    drag_camera(orbit_drag_start,QPoint(25,8));
    if (!wait_for(ready)) return fail("Orbit drag did not settle");
    auto orbit_camera=camera_from_status();
    if (!orbit_camera || (orbit_camera->position-before_orbit_camera->position).length()<0.001F ||
        std::hypot(project_world_point(*orbit_camera,orbit_subject).x()-orbit_subject_uv.x(),
            project_world_point(*orbit_camera,orbit_subject).y()-orbit_subject_uv.y())>0.025 ||
        viewport->status().value("photoSettings").toMap()!=same_recipe_photo ||
        viewport->status().value("lensSettings").toMap()!=same_recipe_lens)
      return fail("Orbit drag did not move around the selected subject while preserving framing and photo settings");
    if (!press_anywhere("sceneCameraRollModeButton") ||
        !workspace_item->property("navigationRollMode").toBool() ||
        workspace_item->property("navigationOrbitMode").toBool() || workspace_item->property("navigationPanMode").toBool())
      return fail("Roll mode did not switch to the dedicated camera rotation tool");
    const double roll_before=viewport->status().value("navigationRollDegrees").toDouble();
    const QPoint roll_drag_start=navigation_surface->mapToScene(QPointF(viewport_item->width()*0.43,viewport_item->height()*0.48)).toPoint();
    drag_camera(roll_drag_start,QPoint(110,0));
    if (!wait_for(ready)) return fail("Roll drag did not settle");
    const auto rolled_camera=camera_from_status();
    const double roll_after=viewport->status().value("navigationRollDegrees").toDouble();
    if (!rolled_camera || (rolled_camera->position-orbit_camera->position).length()>1e-5F ||
        QVector3D::dotProduct(rolled_camera->forward,orbit_camera->forward)<0.99999F ||
        (rolled_camera->right-orbit_camera->right).length()<0.05F || (rolled_camera->down-orbit_camera->down).length()<0.05F ||
        std::abs(roll_after-roll_before)<5.0 ||
        viewport->status().value("photoSettings").toMap()!=same_recipe_photo ||
        viewport->status().value("lensSettings").toMap()!=same_recipe_lens)
      return fail("Roll drag should rotate the local right/down axes while holding position, forward direction and photo settings");
    const auto valid_basis=[](const GaussianCamera& camera) {
      return std::abs(camera.right.length()-1.0F)<0.002F && std::abs(camera.down.length()-1.0F)<0.002F &&
          std::abs(camera.forward.length()-1.0F)<0.002F &&
          std::abs(QVector3D::dotProduct(camera.right,camera.down))<0.002F &&
          std::abs(QVector3D::dotProduct(camera.right,camera.forward))<0.002F &&
          std::abs(QVector3D::dotProduct(camera.down,camera.forward))<0.002F;
    };
    if (!valid_basis(*rolled_camera)) return fail("Roll produced a non-orthonormal camera frame");
    if (!press_anywhere("sceneCameraOrbitModeButton") ||
        !workspace_item->property("navigationOrbitMode").toBool() || workspace_item->property("navigationRollMode").toBool())
      return fail("The camera could not switch back from Roll to explicit Orbit mode");
    const QPoint orbit_after_roll_start=navigation_surface->mapToScene(QPointF(viewport_item->width()*0.57,viewport_item->height()*0.55)).toPoint();
    drag_camera(orbit_after_roll_start,QPoint(19,7));
    if (!wait_for(ready)) return fail("Orbit drag after rolling did not settle");
    const auto orbit_after_roll_camera=camera_from_status();
    const double roll_after_orbit=viewport->status().value("navigationRollDegrees").toDouble();
    if (!orbit_after_roll_camera || (orbit_after_roll_camera->position-rolled_camera->position).length()<0.001F ||
        QVector3D::dotProduct(orbit_after_roll_camera->forward,rolled_camera->forward)>0.99999F ||
        std::abs(roll_after_orbit)<4.0 || std::abs(roll_after_orbit-roll_after)>1.0 ||
        !valid_basis(*orbit_after_roll_camera))
      return fail("Orbit after Roll should rotate freely while retaining the deliberately rolled frame");
    const QPoint alt_roll_start=navigation_surface->mapToScene(QPointF(viewport_item->width()*0.46,viewport_item->height()*0.50)).toPoint();
    drag_camera(alt_roll_start,QPoint(64,0),Qt::AltModifier);
    if (!wait_for(ready)) return fail("Alt-drag roll did not settle in Orbit mode");
    const auto alt_rolled_camera=camera_from_status();
    const double roll_after_alt=viewport->status().value("navigationRollDegrees").toDouble();
    if (!alt_rolled_camera || (alt_rolled_camera->position-orbit_after_roll_camera->position).length()>1e-5F ||
        QVector3D::dotProduct(alt_rolled_camera->forward,orbit_after_roll_camera->forward)<0.99999F ||
        std::abs(roll_after_alt-roll_after_orbit)<5.0 || !workspace_item->property("navigationOrbitMode").toBool() ||
        !valid_basis(*alt_rolled_camera))
      return fail("Alt-drag should apply an in-place roll while retaining explicit Orbit mode");
    const auto camera_before_pan=*alt_rolled_camera;
    if (!press_anywhere("sceneCameraPanModeButton") || !workspace_item->property("navigationPanMode").toBool() ||
        !viewport_item->hasActiveFocus())
      return fail("Selecting Pan did not focus the camera or leave explicit Pan mode active");
    const QPoint horizontal_pan_start=navigation_surface->mapToScene(QPointF(viewport_item->width()*0.58,viewport_item->height()*0.57)).toPoint();
    drag_camera(horizontal_pan_start,QPoint(42,0));
    if (!wait_for(ready)) return fail("Horizontal pan drag did not settle");
    const auto horizontal_pan_camera=camera_from_status();
    if (!horizontal_pan_camera) return fail("Horizontal pan did not publish a camera pose");
    const QVector3D horizontal_delta=horizontal_pan_camera->position-camera_before_pan.position;
    const QVector3D horizontal_unit=horizontal_delta.normalized();
    if (horizontal_delta.length()<0.001F ||
        std::abs(QVector3D::dotProduct(horizontal_unit,camera_before_pan.right))<0.94F ||
        std::abs(QVector3D::dotProduct(horizontal_unit,camera_before_pan.down))>0.2F ||
        std::abs(QVector3D::dotProduct(horizontal_unit,camera_before_pan.forward))>0.2F ||
        QVector3D::dotProduct(horizontal_pan_camera->forward,camera_before_pan.forward)<0.99999F ||
        std::abs(QVector3D::dotProduct(horizontal_unit,orbit_camera->right))>0.98F)
      return fail("After Roll and Orbit, horizontal Pan should follow the rotated camera-right axis");
    const QPoint vertical_pan_start=navigation_surface->mapToScene(QPointF(viewport_item->width()*0.52,viewport_item->height()*0.55)).toPoint();
    drag_camera(vertical_pan_start,QPoint(0,36));
    if (!wait_for(ready)) return fail("Vertical pan drag did not settle");
    const auto pan_camera=camera_from_status();
    if (!pan_camera) return fail("Vertical pan did not publish a camera pose");
    const QVector3D vertical_delta=pan_camera->position-horizontal_pan_camera->position;
    const QVector3D vertical_unit=vertical_delta.normalized();
    if (vertical_delta.length()<0.001F ||
        std::abs(QVector3D::dotProduct(vertical_unit,pan_camera->down))<0.94F ||
        std::abs(QVector3D::dotProduct(vertical_unit,pan_camera->right))>0.2F ||
        std::abs(QVector3D::dotProduct(vertical_unit,pan_camera->forward))>0.2F ||
        QVector3D::dotProduct(pan_camera->forward,horizontal_pan_camera->forward)<0.99999F ||
        viewport->status().value("photoSettings").toMap()!=same_recipe_photo ||
        viewport->status().value("lensSettings").toMap()!=same_recipe_lens)
      return fail("Vertical Pan should follow the rolled camera-down axis without rotating or changing photo settings");
    QTest::keyClick(window,Qt::Key_Up);
    if (!wait_for(ready)) return fail("Keyboard Pan did not settle");
    const auto keyboard_pan_camera=camera_from_status();
    if (!keyboard_pan_camera || (keyboard_pan_camera->position-pan_camera->position).length()<0.001F ||
        QVector3D::dotProduct(keyboard_pan_camera->forward,pan_camera->forward)<0.9999F ||
        std::abs(QVector3D::dotProduct((keyboard_pan_camera->position-pan_camera->position).normalized(),pan_camera->down))<0.9F)
      return fail("Arrow keys in Pan mode did not move the camera vertically");
    QTest::keyClick(window,Qt::Key_Right);
    if (!wait_for(ready)) return fail("Keyboard horizontal Pan did not settle");
    const auto keyboard_right_camera=camera_from_status();
    if (!keyboard_right_camera || (keyboard_right_camera->position-keyboard_pan_camera->position).length()<0.001F ||
        QVector3D::dotProduct(keyboard_right_camera->forward,keyboard_pan_camera->forward)<0.9999F ||
        std::abs(QVector3D::dotProduct((keyboard_right_camera->position-keyboard_pan_camera->position).normalized(),keyboard_pan_camera->right))<0.9F ||
        !valid_basis(*keyboard_right_camera))
      return fail("Arrow keys in Pan mode did not move the camera horizontally in the rolled frame");
    if (viewport->status().value("photoSettings").toMap()!=same_recipe_photo ||
        viewport->status().value("lensSettings").toMap()!=same_recipe_lens)
      return fail("First-person navigation changed photo settings");
    if (!press_anywhere("sceneCameraResetViewButton") || !wait_for(ready))
      return fail("Camera reset did not restore the capture view");
    const auto reset_camera=camera_from_status();
    if (!reset_camera || (reset_camera->position-initial_camera->position).length()>0.001F ||
        QVector3D::dotProduct(reset_camera->forward,initial_camera->forward)<0.99999F ||
        std::abs(viewport->status().value("navigationRollDegrees").toDouble()-initial_roll)>0.01)
      return fail("Camera reset did not restore the original capture pose");
    if (!workspace_item->property("navigationPanMode").toBool() ||
        !press_anywhere("sceneCameraLookModeButton") || workspace_item->property("navigationPanMode").toBool() ||
        workspace_item->property("navigationOrbitMode").toBool() || workspace_item->property("navigationRollMode").toBool() ||
        !look_mode_button->property("active").toBool())
      return fail("Camera motion mode controls did not return from Pan to first-person Look");
    if (args.contains("--navigation-controls-proof")) {
      auto* camera_record = fixture->findChild<photo_editor::desktop::SceneVideoRecorder*>("sceneVideoRecorder");
      if (!camera_record || !camera_record->start(viewport, viewport->source().value("projectPath").toString()))
        return fail("The reconstructed scene camera must support project-local recording");
      QTest::qWait(500); viewport->look(6, 2); QTest::qWait(650); camera_record->stop();
      if (!wait_for([&] { return !camera_record->busy(); }) || camera_record->outputUrl().isEmpty() ||
          QFileInfo(camera_record->outputUrl().toLocalFile()).size() < 1000)
        return fail("The reconstructed scene camera must finish a playable project MP4");
      std::cout << "{\"passed\":true,\"check\":\"first-person Look, forward movement, target Orbit, Roll and rolled-frame Pan\"}\n";
      return 0;
    }
    const double pinhole_radius=viewport->status().value("lensSettings").toMap().value("apertureRadius").toDouble();
    if (!press("sceneApertureIncreaseButton") || !wait_for(ready)) return fail("Aperture stepper did not settle after pointer input");
    auto lens=viewport->status().value("lensSettings").toMap();
    if (lens.value("apertureRadius").toDouble()<=pinhole_radius || !lens.value("rayTraced").toBool())
      return fail("Main aperture control did not enable a non-pinhole sampled lens");
    const double pointer_radius=lens.value("apertureRadius").toDouble();
    if (!press("sceneApertureIncreaseButton") || !wait_for(ready)) return fail("Aperture stepper did not settle after a second adjustment");
    lens=viewport->status().value("lensSettings").toMap();
    if (lens.value("apertureRadius").toDouble()<=pointer_radius || lens.value("samples").toInt()<128)
      return fail("Aperture stepper did not update its value and optics quality");
    if (!set_advanced_camera(true) ||
        !wait_for([&] { return camera_panel->property("advancedVisible").toBool() && ray_control_row->isVisible() &&
                             bloom_stepper->isVisible() && halation_stepper->isVisible(); }))
      return fail("The Advanced control did not reveal ray-tracing and glow settings");
    if (!press("sceneRayTracedOpticsToggle") || !wait_for(ready))
      return fail("Ray-traced optics switch did not turn off the sampled lens route");
    lens=viewport->status().value("lensSettings").toMap();
    if (lens.value("rayTraced").toBool()) return fail("Ray-traced optics switch did not turn off the sampled lens route");
    if (!press("sceneApertureIncreaseButton") || !wait_for(ready) || viewport->status().value("lensSettings").toMap().value("rayTraced").toBool())
      return fail("Turning ray tracing off was not honored during aperture adjustment");
    if (!enter("sceneSceneApertureValue","0.001") || !wait_for(ready) ||
        std::abs(viewport->status().value("lensSettings").toMap().value("apertureRadius").toDouble()-0.001)>1e-6 ||
        viewport->status().value("lensSettings").toMap().value("rayTraced").toBool())
      return fail("Exact aperture entry did not preserve the ray-tracing opt-out");
    if (!choose("sceneRayTracedQualityCombo",1) || !wait_for(ready) ||
        !viewport->status().value("lensSettings").toMap().value("rayTraced").toBool() ||
        viewport->status().value("lensSettings").toMap().value("samples").toInt()!=512 ||
        !choose("scenePupilSoftnessCombo",1) || !wait_for(ready) ||
        viewport->status().value("lensSettings").toMap().value("pupilSoftness").toDouble()!=1)
      return fail("Ray-traced quality or bokeh edge softness selection did not update the camera");
    const auto post_before_main_glow=viewport->status().value("photoSettings").toMap().value("post").toMap();
    const bool had_main_glow_threshold=post_before_main_glow.contains("threshold");
    if (!press("sceneBloomIncreaseButton") || !wait_for(ready)) return fail("Main-page bloom control did not settle");
    auto main_post=viewport->status().value("photoSettings").toMap().value("post").toMap();
    if (main_post.value("bloomAmount").toDouble()<=0 ||
        (!had_main_glow_threshold && main_post.value("threshold").toDouble()>0.3))
      return fail("Main bloom control did not create a gentle first-use highlight cutoff");
    const auto film_before_main_glow=viewport->status().value("photoSettings").toMap().value("film").toMap();
    const bool had_main_film_threshold=film_before_main_glow.contains("halationThreshold");
    if (!press("sceneHalationIncreaseButton") || !wait_for(ready)) return fail("Main-page halation control did not settle");
    const auto main_film=viewport->status().value("photoSettings").toMap().value("film").toMap();
    if (main_film.value("halationAmount").toDouble()<=0 ||
        (!had_main_film_threshold && main_film.value("halationThreshold").toDouble()>0.4))
      return fail("Advanced halation control did not create a gentle first-use highlight cutoff");
    if (!set_advanced_camera(false) || !wait_for([&] { return !advanced_toggle->property("active").toBool() &&
                                                            !ray_control_row->isVisible() && !bloom_stepper->isVisible() &&
                                                            !halation_stepper->isVisible() &&
                                                            camera_panel->property("page").toInt()==4; }))
      return fail("Closing Advanced did not restore a clean essential-only camera page");
    if (!enter("sceneFocalValue","0.80") || !wait_for(ready) ||
        std::abs(viewport->status().value("photoSettings").toMap().value("focalScale").toDouble()-0.8)>1e-6 ||
        !enter("sceneExposureValue","0.50") || !wait_for(ready) ||
        std::abs(viewport->status().value("photoSettings").toMap().value("exposureEv").toDouble()-0.5)>1e-6)
      return fail("Exact framing or exposure entry did not update the camera settings");
    if (!press("sceneResetPhotoButton") || !wait_for(ready)) return fail("Photo reset control is missing from the main page");
    lens=viewport->status().value("lensSettings").toMap();
    if (lens.contains("rayTraced") || lens.contains("pupilSoftness"))
      return fail("Photo reset did not clear v7 ray-tracing and pupil-softness settings");
    if (!press("scenePhotoAdvancedToggle")) return fail("Advanced sensor and scale disclosure is missing");
    if (!enter("scenePhysicalScaleField","1") || !enter("scenePhysicalSensorWidthField","23.5") ||
        !enter("scenePhysicalSensorHeightField","15.6") || !press("sceneApplyPhysicalCameraButton") || !wait_for(ready))
      return fail("Native physical camera/sensor fields did not apply");
    const auto physical=viewport->status().value("physicalCamera").toMap();
    const auto optics=viewport->describeCameraOptics(30);
    if (physical.value("sensorWidthMm").toDouble()!=23.5 || physical.value("sensorHeightMm").toDouble()!=15.6 ||
        physical.value("metresPerSceneUnit").toDouble()!=1 || !optics.value("ok").toBool() ||
        !optics.value("physical").toBool() || optics.value("horizontalFovDegrees").toDouble()<=0)
      return fail("Applied sensor or optics report differs from native input");
    auto* focus_criterion=visual_item(window->contentItem(),"sceneFocusCriterionField");
    auto* focus_plane_details=visual_item(window->contentItem(),"sceneFocusPlaneReadout");
    auto* render_details_toggle=visual_item(window->contentItem(),"sceneRenderDetailsToggle");
    if (!focus_criterion || !focus_plane_details || !render_details_toggle ||
        !press("scenePhotoAdvancedToggle") || advanced_sections->isVisible() ||
        !camera_panel->setProperty("renderDetailsExpanded",true) ||
        focus_criterion->isVisible() || focus_plane_details->isVisible() || render_details_toggle->isVisible() ||
        !press("scenePhotoAdvancedToggle") || !advanced_sections->isVisible() ||
        !render_details_toggle->isVisible() || !focus_criterion->isVisible() || !focus_plane_details->isVisible())
      return fail("Expanded render details leaked onto the basic camera page");
    if (!enter("scenePhysicalFNumberField","1.4") || !wait_for(ready) ||
        std::abs(viewport->describeCameraOptics().value("entrancePupilDiameterMm").toDouble()-50/1.4)>1e-6 ||
        !enter("sceneLensFocusField","3") || !wait_for(ready) ||
        std::abs(viewport->describeCameraOptics().value("focusFromLensM").toDouble()-3)>1e-6 ||
        !enter("sceneFocusCriterionField","10") || !wait_for(ready) ||
        settings->property("permissibleCocMicrons").toDouble()!=10 || !capture("studio-camera-physical"))
      return fail("Native aperture, focus or blur criterion did not update");
    if (!enter("scenePhysicalFNumberField","0.1") || !wait_for(ready) ||
        std::abs(viewport->describeCameraOptics().value("entrancePupilDiameterMm").toDouble()-500)>1e-6 ||
        !enter("scenePhysicalFocalField","200") || !wait_for(ready) ||
        std::abs(viewport->describeCameraOptics().value("entrancePupilDiameterMm").toDouble()-2000)>1e-6 ||
        !capture("studio-extreme-f0.1-200mm") ||
        !enter("scenePhysicalFNumberField","22") || !wait_for(ready) || !capture("studio-f22-200mm"))
      return fail("Native f/0.1, f/22 or extreme-aperture zoom failed");
    auto* aperture_slider=show_item("scenePhysicalFStopSlider");
    if (!aperture_slider) return fail("Physical aperture slider is missing");
    const auto slider_center=aperture_slider->mapRectToScene(aperture_slider->boundingRect()).center().toPoint();
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,slider_center);
    QTest::mouseMove(window,slider_center+QPoint(20,0),20);
    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,slider_center+QPoint(20,0));
    if (!wait_for(ready) || viewport->status().value("physicalCamera").toMap().value("fNumber").toDouble()==22)
      return fail("Native aperture slider did not change the pupil");
    if (!enter("scenePhysicalFocalField","50") || !wait_for(ready)) return fail("50 mm camera restoration failed");
    if (!enter("scenePhysicalFNumberField","1.4") || !wait_for(ready)) return fail("f/1.4 camera restoration failed");
    const auto accepted_optics=viewport->describeCameraOptics(10);
    // Native film-model selection and slider input, followed by saved-shot
    // recall and workspace recreation below. Keep the active sensor inherited.
    if (!camera_panel->setProperty("page",3) || !choose("sceneFilmModelCombo",1) || !wait_for(ready) ||
        viewport->status().value("photoSettings").toMap().value("film").toMap().value("schema")!="framehouse-film-plane-v2")
      return fail("Native film-plane model selection failed");
    if (!press("sceneFilmGrainSlider") || !wait_for(ready) || !press("sceneFilmNewGrainButton") || !wait_for(ready) ||
        !capture("studio-film-grain") || !choose("sceneFilmSectionCombo",1) || !press("sceneFilmHalationSlider") ||
        !press("sceneFilmReturn1") || !wait_for(ready) || !capture("studio-film-halation"))
      return fail("Native grain, pattern or RGB halation controls failed");
    auto accepted_film=viewport->status().value("photoSettings").toMap().value("film").toMap();
    if (accepted_film.value("grainAmount").toDouble()<=0 || accepted_film.value("halationAmount").toDouble()<=0 ||
        accepted_film.value("seed").toUInt()!=2) return fail("Native film controls did not change the rendered recipe");
    if (!camera_panel->setProperty("page",5) || !choose("scenePostSectionCombo",2) ||
        !enter("scenePost_saturation","0.9") || !wait_for(ready) ||
        viewport->status().value("photoSettings").toMap().value("film").toMap()!=accepted_film ||
        !camera_panel->setProperty("page",0) || !press("scenePhotoContrastSlider") || !wait_for(ready) ||
        viewport->status().value("photoSettings").toMap().value("film").toMap()!=accepted_film)
      return fail("Post or tone edit downgraded the film-plane recipe");
    if (!camera_panel->setProperty("page",4) || !press("sceneQuickAutofocusButton") || !wait_for(ready) ||
        viewport->status().value("lensSettings").toMap().value("focusMode")!="auto-centre" ||
        !press("sceneQuickAutofocusButton") || !wait_for(ready) ||
        viewport->status().value("lensSettings").toMap().value("focusMode")=="auto-centre" ||
        !press("sceneQuickPickFocusButton") || !workspace->property("focusPickArmed").toBool())
      return fail("Main camera autofocus / lock / pick focus controls failed");
    QTest::keyClick(window,Qt::Key_Escape);
    QCoreApplication::processEvents();
    QTest::qWait(30);
    if (workspace->property("focusPickArmed").toBool() || !camera_panel->property("opened").toBool()) {
      std::cerr << "Focus Escape stage: armed=" << workspace->property("focusPickArmed").toBool()
                << " panelOpened=" << camera_panel->property("opened").toBool()
                << " viewportFocus=" << viewport_item->hasActiveFocus() << '\n';
      return fail("Escape did not cancel focus picking while keeping the camera panel open");
    }
    if (!enter("sceneLensFocusField","3") || !wait_for(ready)) {
      std::cerr << "Manual focus entry stage: actionError=" << workspace->property("actionError").toString().toStdString()
                << " panelOpened=" << camera_panel->property("opened").toBool()
                << " focusDistanceM=" << viewport->status().value("physicalCamera").toMap().value("focusDistanceM").toDouble() << '\n';
      return fail("Manual focus field did not accept 3 metres after pick cancellation");
    }
    if (std::abs(viewport->status().value("physicalCamera").toMap().value("focusDistanceM").toDouble()-3)>1e-5)
      return fail("Exact focus entry did not set the physical focus distance to 3 metres");
    if (!press("sceneFocusFartherButton") || !wait_for(ready)) {
      auto* farther_button=visual_item(window->contentItem(),"sceneFocusFartherButton");
      std::cerr << "Farther focus stage: actionError=" << workspace->property("actionError").toString().toStdString()
                << " focusDistanceM=" << viewport->status().value("physicalCamera").toMap().value("focusDistanceM").toDouble()
                << " buttonVisible=" << (farther_button && farther_button->isVisible())
                << " buttonEnabled=" << (farther_button && farther_button->isEnabled()) << '\n';
      return fail("Farther focus button did not apply");
    }
    const double farther_focus=viewport->status().value("physicalCamera").toMap().value("focusDistanceM").toDouble();
    if (std::abs(farther_focus-3.06)>1e-5) {
      std::cerr << "Farther focus value: expected=3.06 actual=" << farther_focus
                << " actionError=" << workspace->property("actionError").toString().toStdString() << '\n';
      return fail("Fine focus farther step did not change focus by two percent");
    }
    if (!press("sceneFocusNearerButton") || !wait_for(ready)) {
      std::cerr << "Nearer focus stage: actionError=" << workspace->property("actionError").toString().toStdString()
                << " focusDistanceM=" << viewport->status().value("physicalCamera").toMap().value("focusDistanceM").toDouble() << '\n';
      return fail("Nearer focus button did not apply");
    }
    const double nearer_focus=viewport->status().value("physicalCamera").toMap().value("focusDistanceM").toDouble();
    if (std::abs(nearer_focus-3)>1e-5) {
      std::cerr << "Nearer focus value: expected=3 actual=" << nearer_focus << '\n';
      return fail("Fine focus nearer step did not return to the exact focus value");
    }
    if (!camera_panel->setProperty("page",2) || !choose("sceneLensStartingPoints",1) || !wait_for(ready) ||
        viewport->status().value("lensSettings").toMap().value("bokehAspect").toDouble()!=2 ||
        viewport->status().value("photoSettings").toMap().value("post").toMap().value("glareRays").toInt()!=1 ||
        !capture("studio-lens-character")) return fail("Native oval / streak starting point failed");
    if (!camera_panel->setProperty("page",3) || !choose("sceneFilmStartingPoints",2) || !wait_for(ready) ||
        viewport->status().value("photoSettings").toMap().value("film").toMap().value("schema")!="framehouse-film-plane-v3")
      return fail("Native film starting point failed");
    if (!camera_panel->setProperty("halationPage",true) || !press("sceneFilmThresholdSlider") || !wait_for(ready) ||
        !capture("studio-highlight-halation")) return fail("Native highlight cutoff failed");
    accepted_film=viewport->status().value("photoSettings").toMap().value("film").toMap();
    if (!camera_panel->setProperty("page",0) || !press("sceneColor_warmth")) return fail("Color balance control missing");
    auto* warmth_slider=show_item("sceneColor_warmth");
    if (!warmth_slider) return fail("Color slider geometry missing");
    const auto warmth_start=warmth_slider->mapToScene(QPointF(warmth_slider->width()*0.5,warmth_slider->height()-5)).toPoint();
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,warmth_start);
    QTest::mouseMove(window,warmth_start+QPoint(24,0),20);
    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,warmth_start+QPoint(24,0));
    if (!wait_for(ready) || viewport->status().value("photoSettings").toMap().value("film").toMap()!=accepted_film ||
        viewport->status().value("photoSettings").toMap().value("tone").toMap().value("warmth").toDouble()==0 ||
        !capture("studio-color-controls")) return fail("Native color adjustment / film preservation failed");
    const auto accepted_tone=viewport->status().value("photoSettings").toMap().value("tone").toMap();
    if (!camera_panel->setProperty("page",4) || !wait_for(ready)) return fail("Camera inspector restore failed");
    const auto shot=viewport->saveView("Physical studio fixture");
    if (!shot.value("ok").toBool()) return fail("Physical shot save failed");
    if (!enter("scenePhysicalFocalField","85")) return fail("Native focal field could not enter 85 mm");
    if (!wait_for(ready)) return fail("85 mm camera did not settle with valid studio geometry");
    if (viewport->describeCameraOptics().value("horizontalFovDegrees").toDouble()>=accepted_optics.value("horizontalFovDegrees").toDouble())
      return fail("85 mm did not narrow the 50 mm field of view");
    if (!viewport->restoreView(shot.value("id").toString()).value("ok").toBool()) return fail("Physical shot recall rejected");
    if (!wait_for(ready)) return fail("Recalled physical camera did not settle with valid studio geometry");
    auto* handle=visual_item(window->contentItem(),"sceneStudioInspectorResizeHandle");
    auto* inspector=visual_item(window->contentItem(),"sceneStudioInspector");
    if (!handle || !inspector) return fail("Inspector resize handle missing");
    const double old_width=inspector->width();
    const auto start=handle->mapRectToScene(handle->boundingRect()).center().toPoint();
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
    QTest::mouseMove(window,start-QPoint(40,0),20); QTest::qWait(30);
    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start-QPoint(40,0));
    if (!wait_for([&] { return ready() && inspector->width()>old_width+20; })) return fail("Native inspector resizing failed");
    const double saved_width=settings->property("inspectorWidth").toDouble();
    window->resize(1024,760);
    auto* motion_hud=visual_item(window->contentItem(),"sceneCameraMotionHud");
    auto* compact_viewport_item=visual_item(window->contentItem(),"gaussianSceneViewport");
    if (!wait_for(ready) || !capture("studio-camera-compact") ||
        !motion_hud || !compact_viewport_item || !compact_viewport_item->mapRectToScene(compact_viewport_item->boundingRect()).contains(motion_hud->mapRectToScene(motion_hud->boundingRect())) ||
        std::abs(viewport->describeCameraOptics(10).value("horizontalFovDegrees").toDouble()-accepted_optics.value("horizontalFovDegrees").toDouble())>0.0001)
      return fail("Compact studio layout clipped camera controls or changed physical framing");
    if (!QMetaObject::invokeMethod(settings,"sync")) return fail("Studio preferences could not sync");
    fixture.reset(); fixture.reset(component.create());
    if (!fixture) return fail("Studio workspace did not recreate");
    window=qobject_cast<QQuickWindow*>(fixture.get());
    viewport=fixture->findChild<GaussianSceneViewport*>("gaussianSceneViewport");
    workspace=fixture->findChild<QQuickItem*>("sceneWorkspace");
    popup=fixture->findChild<QObject*>("sceneCleanupPopup");
    settings=fixture->findChild<QObject*>("sceneWorkspaceSettings");
    camera_panel=fixture->findChild<QObject*>("scenePhotoControlsPopup");
    if (!window || !viewport || !workspace || !popup || !settings || !camera_panel) return fail("Recreated studio lost its panels");
    auto* reopened_advanced=visual_item(window->contentItem(),"scenePhotoAdvancedToggle");
    auto* reopened_contents=visual_item(window->contentItem(),"sceneContentsToggleButton");
    auto* reopened_shots=visual_item(window->contentItem(),"sceneShotsToggleButton");
    auto* reopened_outliner=visual_item(window->contentItem(),"sceneStudioOutliner");
    auto* reopened_shots_panel=visual_item(window->contentItem(),"sceneStudioShots");
    auto* reopened_ray_row=visual_item(window->contentItem(),"sceneRayTracedControlRow");
    QObject::connect(window,&QQuickWindow::frameSwapped,&app,[&] { ++swapped; });
    if (!wait_for(ready) || std::abs(settings->property("inspectorWidth").toDouble()-saved_width)>0.001 ||
        settings->property("permissibleCocMicrons").toDouble()!=10 || !camera_panel->property("opened").toBool() ||
        !viewport->restoreView(shot.value("id").toString()).value("ok").toBool() || !wait_for(ready) ||
        camera_panel->property("advancedVisible").toBool() || !reopened_advanced || reopened_advanced->property("active").toBool() ||
        !reopened_ray_row || reopened_ray_row->isVisible() || !reopened_contents || reopened_contents->property("active").toBool() ||
        !reopened_shots || reopened_shots->property("active").toBool() || !reopened_outliner || reopened_outliner->isVisible() ||
        !reopened_shots_panel || reopened_shots_panel->isVisible() ||
        settings->property("studioLayoutVersion").toInt()<2 || settings->property("outlinerVisible").toBool() ||
        settings->property("shotsVisible").toBool() ||
        viewport->status().value("photoSettings").toMap().value("film").toMap()!=accepted_film ||
        viewport->status().value("photoSettings").toMap().value("tone").toMap()!=accepted_tone ||
        viewport->status().value("lensSettings").toMap().value("bokehAspect").toDouble()!=2 ||
        !capture("studio-camera-reopened")) return fail("Studio preferences or saved camera did not survive recreation");
    if (!qml_warnings.isEmpty()) return fail("Studio camera workflow emitted QML warnings");
    const QVariantMap receipt{{"passed",true},{"nativePointerAndKeyboard",true},{"customSensorMm",QVariantList{23.5,15.6}},
        {"optics",viewport->describeCameraOptics(10)},{"film",accepted_film},{"inspectorWidth",saved_width},
        {"checks",QStringList{"custom-sensor","measured-fixture-scale","f-number","focus","circle-of-confusion-criterion",
            "native-f0.1","native-f22","native-aperture-slider","extreme-aperture-200mm-zoom",
            "focal-length-FOV","native-film-model","native-grain-pattern","native-RGB-halation","film-recipe-recall","post-tone-preserve-film",
            "saved-shot-recall","native-inspector-resize","compact-nonoverlapping-panels","recreated-workspace-preferences"}},
        {"windowExposed",window->isExposed()},{"windowActive",window->isActive()},
        {"scope","Native ideal-thin-lens camera/studio workflow; not compound-optics integration or foreground latency qualification"}};
    if (!evidence.isEmpty() && !write(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(receipt).toJson())) return 2;
    std::cout<<QJsonDocument::fromVariant(receipt).toJson().constData(); return 0;
