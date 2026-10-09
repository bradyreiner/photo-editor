      if (qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_TIMELINE_TRIM_PROOF_ONLY")) {
#include "timeline_trim_workflow_proof.inc"
      }
      timeline_document.selectClip(0);
      timeline_document.seekSequenceSeconds(0.0);
      if (!timeline_document.addAdjustmentClip(8.0)) return fail("Native keyframes require a composited adjustment clip");
      const auto keyframe_clip_id = timeline_document.currentClip().value(QStringLiteral("clipId")).toString();
      timeline_document.addSelectedClipEffect(QStringLiteral("Blur"));
      const auto layer_result = timeline_document.addClipColorGradeAdjustmentLayerForAutomation(
          keyframe_clip_id, QStringLiteral("Subject"));
      if (!layer_result.value(QStringLiteral("ok")).toBool()) return fail("Native keyframes require a grade layer");
      const auto layer_id = layer_result.value(QStringLiteral("layerId")).toString();
      const auto layer_contrast = QStringLiteral("grade:layer:%1:contrast").arg(layer_id);
      if (!QMetaObject::invokeMethod(timeline_workspace, "openCurveEditor",
                                    Q_ARG(QVariant, QVariant(QStringLiteral("effect:Blur"))))) {
        return fail("Every effect must open in the shared native curve editor");
      }
      auto* curve_window = qobject_cast<QQuickWindow*>(find_named_object(
          visual_root, QStringLiteral("timelineKeyframeCurveWindow")));
      if (!curve_window || !wait_for_condition([&] {
            return curve_window->isVisible() && find_named_item(curve_window, QStringLiteral("timelineCurveAddPointButton"));
          }, 5000)) return fail("Curve controls must load in the native tool window");
      ulong curve_pointer_timestamp = 10000;
      const auto click_curve = [&](const QString& name) {
        auto* item = find_named_item(curve_window, name);
        if (!item || !item_visible(item) || !item->property("enabled").toBool()) return false;
        const auto point = item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
        const auto global = QPointF(curve_window->mapToGlobal(point.toPoint()));
        curve_pointer_timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
        for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
          QMouseEvent event(type, point, global, Qt::LeftButton,
                            type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
          event.setTimestamp(curve_pointer_timestamp++);
          QCoreApplication::sendEvent(curve_window, &event);
          process_layout_events(2);
        }
        return true;
      };
      timeline_document.seekSequenceSeconds(0.0);
      process_layout_events(6);
      if (!click_curve(QStringLiteral("timelineCurveAddPointButton"))) return fail("Add Point must work for ordinary effects");
      timeline_document.setSelectedClipEffectAmount(QStringLiteral("Blur"), 0.0);
      timeline_document.seekSequenceSeconds(2.0);
      process_layout_events(4);
      if (!click_curve(QStringLiteral("timelineCurveAddPointButton"))) return fail("Effect curve must accept a second point");
      timeline_document.setSelectedClipEffectAmount(QStringLiteral("Blur"), 1.0);
      if (timeline_document.currentClip().value(QStringLiteral("keyframes")).toList().size() != 2) {
        return fail("Pointer clicks must author real effect keyframes");
      }
      if (!QMetaObject::invokeMethod(curve_window, "selectCurveSecond", Q_ARG(QVariant, QVariant(0.0)),
                                    Q_ARG(QVariant, QVariant(false))) ||
          !QMetaObject::invokeMethod(curve_window, "selectCurveSecond", Q_ARG(QVariant, QVariant(2.0)),
                                    Q_ARG(QVariant, QVariant(true)))) return fail("Native curve multi-selection must work");
      if (!click_curve(QStringLiteral("timelineCurveInterpolationControl")) ||
          !wait_for_condition([&] {
            return item_visible(find_named_item(curve_window, QStringLiteral("timelineCurveInterpolationControlOption4")));
          }, 3000) ||
          !capture_visual_proof(curve_window, curve_window, proof_dir, QStringLiteral("keyframe-interpolation-options"), 860, 520,
                                {QStringLiteral("timelineCurveInterpolationControlOption4")}, &manifest) ||
          !click_curve(QStringLiteral("timelineCurveInterpolationControlOption4"))) return fail("Interpolation must be editable by pointer");
      for (const auto& key : timeline_document.currentClip().value(QStringLiteral("keyframes")).toList()) {
        if (key.toMap().value(QStringLiteral("interpolation")).toString() != QStringLiteral("ease-in-out")) {
          return fail("Native interpolation selection must apply to both selected keys");
        }
      }
      timeline_document.undo();
      for (const auto& key : timeline_document.currentClip().value(QStringLiteral("keyframes")).toList()) {
        if (key.toMap().value(QStringLiteral("interpolation")).toString() != QStringLiteral("linear")) {
          return fail("Native batch interpolation must undo in one step");
        }
      }
      // The parameter search must discover an unanimated layer without exposing internal ids.
      if (!click_curve(QStringLiteral("timelineCurveParameterPickerTrigger"))) return fail("Parameter picker must open by pointer");
      auto* search = find_named_object(curve_window, QStringLiteral("timelineCurveParameterPickerSearch"));
      auto* parameter_list = find_named_object(curve_window, QStringLiteral("timelineCurveParameterPickerList"));
      if (!search || !parameter_list) return fail("Parameter discovery must expose search and results");
      for (const auto character : QStringLiteral("subject contrast")) {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(character));
        QCoreApplication::sendEvent(curve_window, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, QString(character));
        QCoreApplication::sendEvent(curve_window, &release);
      }
      process_layout_events(6);
      if (parameter_list->property("count").toInt() != 1 ||
          !capture_visual_proof(curve_window, curve_window, proof_dir,
                                QStringLiteral("keyframe-parameter-search"), 860, 520,
                                {QStringLiteral("timelineCurveParameterPickerSearch"),
                                 QStringLiteral("timelineCurveParameterPickerList")}, &manifest) ||
          !click_curve(QStringLiteral("timelineCurveParameterPickerOption0")) ||
          timeline_workspace->property("curveEditorParameter").toString() != layer_contrast) {
        return fail("Search must choose the correct unanimated layer control");
      }
      timeline_document.seekSequenceSeconds(0.0);
      if (!click_curve(QStringLiteral("timelineCurveAddPointButton"))) return fail("An unanimated grade control must accept a point");
      timeline_document.seekSequenceSeconds(2.0);
      if (!click_curve(QStringLiteral("timelineCurveAddPointButton"))) return fail("A layer control must accept a second point");
      timeline_document.setSelectedClipKeyframeValue(layer_contrast, 0.0, -60.0);
      timeline_document.setSelectedClipKeyframeValue(layer_contrast, 2.0, 60.0);
      timeline_document.setSelectedClipKeyframeBezierHandle(layer_contrast, 0.0, QStringLiteral("out"), 0.5, 50.0);
      QMetaObject::invokeMethod(curve_window, "selectCurveSecond", Q_ARG(QVariant, QVariant(0.0)), Q_ARG(QVariant, QVariant(false)));
      process_layout_events(8);
      if (!capture_visual_proof(curve_window, curve_window, proof_dir,
                                QStringLiteral("keyframe-grade-bezier"), 860, 520,
                                {QStringLiteral("timelineBezierOutHandle"), QStringLiteral("timelineCurveInterpolationControl")}, &manifest) ||
          !capture_visual_proof(curve_window, curve_window, proof_dir,
                                QStringLiteral("keyframe-grade-compact"), 640, 400,
                                {QStringLiteral("timelineCurveAddPointButton"), QStringLiteral("timelineCurveParameterPickerTrigger")}, &manifest)) {
        return fail("Grade curves must show weighted handles and fit compact windows");
      }
      const QRectF curve_bounds(0, 0, curve_window->width(), curve_window->height());
      for (const auto& name : {QStringLiteral("timelineCurveAddPointButton"), QStringLiteral("timelineCurveParameterPickerTrigger"),
                               QStringLiteral("timelineCurveInterpolationControl"), QStringLiteral("timelineKeyframeBatchMenuButton")}) {
        auto* item = find_named_item(curve_window, name);
        if (!item || !item_visible(item) || !curve_bounds.contains(scene_rect(item))) return fail("Curve toolbar controls must fit compact windows");
      }
      const auto edit_curve_field = [&](const QString& name, const QString& text) {
        if (!click_curve(name)) return false;
        QKeyEvent select_all(QEvent::KeyPress, Qt::Key_A,
                             QKeySequence(QKeySequence::SelectAll)[0].keyboardModifiers());
        QCoreApplication::sendEvent(curve_window, &select_all);
        for (const auto character : text) {
          QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(character));
          QCoreApplication::sendEvent(curve_window, &press);
        }
        QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(curve_window, &accept);
        process_layout_events(6);
        return true;
      };
      const auto has_grade_key = [&](double seconds, double value) {
        for (const auto& entry : timeline_document.currentClip().value(QStringLiteral("keyframes")).toList()) {
          const auto key = entry.toMap();
          if (key.value(QStringLiteral("parameter")).toString() == layer_contrast &&
              std::abs(key.value(QStringLiteral("seconds")).toDouble() - seconds) < 0.001 &&
              std::abs(key.value(QStringLiteral("value")).toDouble() - value) < 0.001 &&
              key.value(QStringLiteral("interpolation")).toString() == QStringLiteral("bezier") &&
              std::abs(key.value(QStringLiteral("bezierOutValue")).toDouble() - 50.0) < 0.001) return true;
        }
        return false;
      };
      if (!edit_curve_field(QStringLiteral("timelineKeyframeParameterValueField"), QStringLiteral("-40")) ||
          !has_grade_key(0.0, -40.0)) return fail("Precise value editing must preserve curve handles");
      timeline_document.undo();
      if (!has_grade_key(0.0, -60.0)) return fail("One undo must restore a precise value edit");
      if (!edit_curve_field(QStringLiteral("timelineKeyframeTimeValueField"), QStringLiteral("0.5")) ||
          !has_grade_key(0.5, -60.0)) return fail("Precise time editing must preserve curve handles");
      timeline_document.undo();
      if (!has_grade_key(0.0, -60.0)) return fail("One undo must restore a precise time edit");
      QMetaObject::invokeMethod(curve_window, "selectCurveSecond", Q_ARG(QVariant, QVariant(0.0)), Q_ARG(QVariant, QVariant(false)));
      if (!click_curve(QStringLiteral("timelineKeyframeSpeedGraphButton")) ||
          !capture_visual_proof(curve_window, curve_window, proof_dir,
                                QStringLiteral("keyframe-speed"), 860, 520,
                                {QStringLiteral("timelineKeyframeSpeedDetailRow")}, &manifest)) return fail("Speed graph must expose tangent rate controls");
      curve_window->hide();
      // Exercise the actual inspector value-edit path that used to reset Bezier to Linear.
      QMetaObject::invokeMethod(timeline_workspace, "openInspectorTab", Q_ARG(QVariant, QVariant(QStringLiteral("motion"))));
      if (!wait_for_condition([&] {
            return find_named_object(visual_root, QStringLiteral("timelineInspectorKeyframeParameterPicker")) != nullptr;
          }, 10000)) return fail("Motion inspector must finish loading its parameter picker");
      auto* inspector = find_named_object(visual_root, QStringLiteral("timelineEffectsRail"));
      timeline_document.seekSequenceSeconds(0.0);
      if (!inspector || !QMetaObject::invokeMethod(inspector, "selectColorGradeLayer", Q_ARG(QVariant, QVariant(layer_id))) ||
          !QMetaObject::invokeMethod(inspector, "setColorGradeParameter", Q_ARG(QVariant, QVariant(QStringLiteral("contrast"))),
                                    Q_ARG(QVariant, QVariant(-25.0)))) return fail("Native inspector must edit animated grade values");
      bool preserved = false;
      for (const auto& value : timeline_document.currentClip().value(QStringLiteral("keyframes")).toList()) {
        const auto key = value.toMap();
        if (key.value(QStringLiteral("parameter")).toString() == layer_contrast && key.value(QStringLiteral("seconds")).toDouble() == 0.0) {
          preserved = key.value(QStringLiteral("interpolation")).toString() == QStringLiteral("bezier") &&
                      std::abs(key.value(QStringLiteral("bezierOutValue")).toDouble() - 50.0) < 0.001 &&
                      std::abs(key.value(QStringLiteral("value")).toDouble() + 25.0) < 0.001;
        }
      }
      if (!preserved) return fail("Inspector value edits must preserve grade interpolation and weighted handles");
      visual_window->resize(1440, 900);
      const auto click_main = [&](const QString& name) {
        auto* item = find_named_item(visual_root, name);
        if (!item || !item_visible(item) || item->window() != visual_window || !item->property("enabled").toBool()) return false;
        const auto point = scene_rect(item).center();
        curve_pointer_timestamp += static_cast<ulong>(QApplication::doubleClickInterval() + 1);
        for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
          QMouseEvent event(type, point, QPointF(visual_window->mapToGlobal(point.toPoint())), Qt::LeftButton,
                            type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
          event.setTimestamp(curve_pointer_timestamp++);
          QCoreApplication::sendEvent(visual_window, &event);
          process_layout_events(3);
        }
        return true;
      };
      QMetaObject::invokeMethod(timeline_workspace, "openInspectorTab", Q_ARG(QVariant, QVariant(QStringLiteral("effects"))));
      if (!wait_for_condition([&] {
            auto* add = find_named_item(visual_root, QStringLiteral("timelineQuickEffectPickerButton"));
            return add && item_visible(add);
          }, 10000) ||
          !capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("timeline-effects-before-add"), 1440, 900,
                                {QStringLiteral("timelineQuickEffectPickerButton")}, &manifest) ||
          !click_main(QStringLiteral("timelineQuickEffectPickerButton"))) return fail("Add Effect must stay accessible above the applied stack");
      if (!wait_for_condition([&] {
            auto* popup = find_named_object(visual_root, QStringLiteral("timelineQuickEffectPicker"));
            auto* search_item = find_named_item(visual_root, QStringLiteral("timelineQuickEffectSearch"));
            auto* list_item = find_named_item(visual_root, QStringLiteral("timelineQuickEffectList"));
            return popup && popup->property("opened").toBool() && item_visible(search_item) && item_visible(list_item);
          }, 5000)) {
        capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("timeline-effects-add-failure"), 1440, 900, {}, &manifest);
        auto* popup = find_named_object(visual_root, QStringLiteral("timelineQuickEffectPicker"));
        std::cerr << "Effect browser opened=" << (popup && popup->property("opened").toBool()) << '\n';
        return fail("The native Add Effect click must open the visible searchable browser");
      }
      for (const auto character : QStringLiteral("fade")) {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(character));
        QCoreApplication::sendEvent(visual_window, &press);
      }
      process_layout_events(6);
      if (!capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("timeline-add-effect-search"), 1440, 900,
                                {QStringLiteral("timelineQuickEffectSearch"), QStringLiteral("timelineQuickEffectList")}, &manifest)) return fail("Searchable effect browser must render with its controls");
      QKeyEvent add_effect(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
      QCoreApplication::sendEvent(visual_window, &add_effect);
      if (!wait_for_condition([&] {
            return timeline_document.currentClip().value(QStringLiteral("effectStack")).toStringList().contains(QStringLiteral("Fade"));
          }, 5000)) return fail("Enter must add the searched compatible effect to the real stack");
      if (!capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("timeline-effects-workspace"), 1440, 900,
                                {QStringLiteral("timelineQuickEffectPickerButton"), QStringLiteral("timelineAppliedEffectsStack"),
                                 QStringLiteral("timelinePrimaryAddMediaButton"), QStringLiteral("timelineSaveStateChip")}, &manifest)) return fail("Effects workflow and primary timeline actions must fit together");
      if (!click_main(QStringLiteral("timelineEffectCurveButton-Blur")) ||
          !wait_for_condition([&] {
            return curve_window->isVisible() && timeline_workspace->property("curveEditorParameter").toString() == QStringLiteral("effect:Blur");
          }, 3000)) return fail("An animated applied effect must open its own curve directly from the stack");
      curve_window->hide();
      QFile keyframe_manifest(QDir(proof_dir).filePath(QStringLiteral("keyframe-manifest.json")));
      if (!keyframe_manifest.open(QIODevice::WriteOnly) ||
          keyframe_manifest.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) <= 0) return fail("Native keyframe proof must save screenshots and geometry");
      return EXIT_SUCCESS;
