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
      // Keep this native proof on the established floating-window route. The
      // workspace defaults to a docked editor, which has separate coverage.
      timeline_workspace->setProperty("curveEditorDocked", false);
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
      const auto find_visible_curve_item = [&](const QString& name) -> QQuickItem* {
        for (auto* candidate : find_named_items(curve_window, name)) {
          if (item_visible(candidate) && candidate->window() == curve_window)
            return candidate;
        }
        return nullptr;
      };
      const auto click_curve = [&](const QString& name) {
        auto* item = find_visible_curve_item(name);
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
      const auto close_curve_without_document_edit = [&] {
        if (!curve_window || !curve_window->isVisible()) return false;
        const auto before_hash = timeline_document.deterministicDocumentHash();
        const auto before_clip_id = timeline_document.currentClip()
                                        .value(QStringLiteral("clipId")).toString();
        const auto before_playhead = timeline_document.sequencePlayheadSeconds();
        if (!click_curve(QStringLiteral("timelineCurveEditorCloseButton")) ||
            !wait_for_condition([&] {
              return !timeline_workspace->property("curveEditorOpen").toBool() &&
                     !curve_window->isVisible();
            }, 3000))
          return false;
        return timeline_document.deterministicDocumentHash() == before_hash &&
               timeline_document.currentClip().value(QStringLiteral("clipId")).toString() == before_clip_id &&
               std::abs(timeline_document.sequencePlayheadSeconds() - before_playhead) < 0.001;
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
      if (!wait_for_condition([&] { return search->property("activeFocus").toBool(); }, 3000)) {
        std::cerr << "Keyframe parameter search focus: visible="
                  << search->property("visible").toBool()
                  << " activeFocus=" << search->property("activeFocus").toBool()
                  << " popup=" << (find_named_object(curve_window, QStringLiteral("timelineCurveParameterPickerPopup"))
                                       ? find_named_object(curve_window, QStringLiteral("timelineCurveParameterPickerPopup"))
                                             ->property("opened").toBool()
                                       : false) << '\n';
        return fail("Parameter search must receive focus before typing channel filters");
      }
      for (const auto character : QStringLiteral("subject contrast")) {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(character));
        QCoreApplication::sendEvent(curve_window, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, QString(character));
        QCoreApplication::sendEvent(curve_window, &release);
      }
      QQuickItem* filtered_parameter_row = nullptr;
      const auto filtered_layer_control_ready = [&] {
        filtered_parameter_row = find_visible_curve_item(
            QStringLiteral("timelineCurveParameterPickerOption0"));
        if (parameter_list->property("count").toInt() != 1 ||
            !item_visible(filtered_parameter_row)) {
          return false;
        }
        const auto row_data = filtered_parameter_row->property("optionData");
        const auto option_data = row_data.metaType() == QMetaType::fromType<QJSValue>()
            ? row_data.value<QJSValue>().toVariant().toMap() : row_data.toMap();
        return option_data.value(QStringLiteral("parameter")).toString() == layer_contrast &&
               filtered_parameter_row->property("optionGroup").toString() == QStringLiteral("Color") &&
               filtered_parameter_row->property("groupHeadingVisible").toBool();
      };
      if (!wait_for_condition(filtered_layer_control_ready, 5000)) {
        auto* parameter_picker = find_named_object(
            curve_window, QStringLiteral("timelineCurveParameterPicker"));
        const auto row_data = filtered_parameter_row
            ? filtered_parameter_row->property("optionData") : QVariant{};
        const auto option_data = row_data.metaType() == QMetaType::fromType<QJSValue>()
            ? row_data.value<QJSValue>().toVariant().toMap() : row_data.toMap();
        std::cerr << "Layer channel filter state: search="
                  << search->property("text").toString().toStdString()
                  << " activeFocus=" << search->property("activeFocus").toBool()
                  << " count=" << parameter_list->property("count").toInt()
                  << " expected=" << layer_contrast.toStdString()
                  << " rowExists=" << (filtered_parameter_row != nullptr)
                  << " rowVisible=" << item_visible(filtered_parameter_row)
                  << " rowWindow=" << (filtered_parameter_row && filtered_parameter_row->window() == curve_window)
                  << " group=" << (filtered_parameter_row
                                      ? filtered_parameter_row->property("optionGroup").toString().toStdString() : "")
                  << " heading=" << (filtered_parameter_row
                                        && filtered_parameter_row->property("groupHeadingVisible").toBool())
                  << " rowParameter=" << option_data.value(QStringLiteral("parameter")).toString().toStdString()
                  << " pickerMatches=" << (parameter_picker
                                               ? parameter_picker->property("filteredOptions").toString().toStdString() : "")
                  << '\n';
        return fail("Search must return one visible, grouped Subject contrast layer control");
      }
      if (!capture_visual_proof(curve_window, curve_window, proof_dir,
                                QStringLiteral("keyframe-parameter-search"), 860, 520,
                                {QStringLiteral("timelineCurveParameterPickerSearch"),
                                 QStringLiteral("timelineCurveParameterPickerList"),
                                 QStringLiteral("timelineCurveParameterPickerOption0")}, &manifest)) {
        return fail("The filtered layer control and group heading must be captured before selection");
      }
      if (!wait_for_condition([&] {
            return filtered_layer_control_ready() &&
                   QRectF(0, 0, curve_window->width(), curve_window->height())
                       .contains(scene_rect(filtered_parameter_row));
          }, 3000)) {
        const auto row_rect = filtered_parameter_row ? scene_rect(filtered_parameter_row) : QRectF{};
        std::cerr << "Layer channel row after capture resize: window="
                  << curve_window->width() << 'x' << curve_window->height()
                  << " row=" << row_rect.x() << ',' << row_rect.y() << ' '
                  << row_rect.width() << 'x' << row_rect.height()
                  << " visible=" << item_visible(filtered_parameter_row) << '\n';
        return fail("The captured layer channel row must settle inside the resized curve window");
      }
      if (!click_curve(QStringLiteral("timelineCurveParameterPickerOption0"))) {
        const auto row_rect = scene_rect(filtered_parameter_row);
        std::cerr << "Layer channel pointer target: row=" << row_rect.x() << ',' << row_rect.y()
                  << ' ' << row_rect.width() << 'x' << row_rect.height()
                  << " enabled=" << filtered_parameter_row->property("enabled").toBool()
                  << " window=" << (filtered_parameter_row->window() == curve_window) << '\n';
        return fail("The visible filtered layer control must accept a real pointer click");
      }
      if (!wait_for_condition([&] {
            return timeline_workspace->property("curveEditorParameter").toString() == layer_contrast;
          }, 3000)) {
        std::cerr << "Layer channel pointer selection: expected=" << layer_contrast.toStdString()
                  << " selected=" << timeline_workspace->property("curveEditorParameter").toString().toStdString()
                  << " rowOption=" << filtered_parameter_row->property("optionData").toString().toStdString()
                  << '\n';
        return fail("Search must choose the correct unanimated layer control");
      }
      auto* parameter_picker_popup = find_named_object(
          curve_window, QStringLiteral("timelineCurveParameterPickerPopup"));
      const auto layer_keyframes = [&] {
        QVariantList result;
        for (const auto& entry : timeline_document.currentClip()
                                      .value(QStringLiteral("keyframes")).toList()) {
          const auto key = entry.toMap();
          if (key.value(QStringLiteral("parameter")).toString() == layer_contrast)
            result.push_back(key);
        }
        return result;
      };
      if (!wait_for_condition([&] {
            return parameter_picker_popup != nullptr &&
                   !parameter_picker_popup->property("opened").toBool() &&
                   !parameter_picker_popup->property("visible").toBool() &&
                   timeline_workspace->property("curveEditorParameter").toString() == layer_contrast;
          }, 5000)) {
        std::cerr << "Layer picker close before graph edit: popupExists="
                  << (parameter_picker_popup != nullptr)
                  << " opened=" << (parameter_picker_popup
                                         && parameter_picker_popup->property("opened").toBool())
                  << " visible=" << (parameter_picker_popup
                                         && parameter_picker_popup->property("visible").toBool())
                  << " selected=" << timeline_workspace->property("curveEditorParameter")
                                         .toString().toStdString() << '\n';
        return fail("The channel picker must finish closing before graph pointer edits");
      }
      timeline_document.seekSequenceSeconds(0.0);
      if (!click_curve(QStringLiteral("timelineCurveAddPointButton")) ||
          !wait_for_condition([&] { return layer_keyframes().size() == 1; }, 3000)) {
        std::cerr << "Grade first key stage: count=" << layer_keyframes().size()
                  << " selected=" << timeline_workspace->property("curveEditorParameter")
                                         .toString().toStdString()
                  << " playhead=" << timeline_document.sequencePlayheadSeconds()
                  << " popupVisible=" << (parameter_picker_popup
                                               && parameter_picker_popup->property("visible").toBool())
                  << '\n';
        return fail("An unanimated grade control must accept its first point at the playhead");
      }
      timeline_document.seekSequenceSeconds(2.0);
      if (!click_curve(QStringLiteral("timelineCurveAddPointButton")) ||
          !wait_for_condition([&] { return layer_keyframes().size() == 2; }, 3000)) {
        std::cerr << "Grade second key stage: count=" << layer_keyframes().size()
                  << " selected=" << timeline_workspace->property("curveEditorParameter")
                                         .toString().toStdString()
                  << " playhead=" << timeline_document.sequencePlayheadSeconds()
                  << " keys=" << QJsonDocument::fromVariant(layer_keyframes())
                                    .toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("The selected grade channel must accept its second point at the playhead");
      }
      timeline_document.setSelectedClipKeyframeValue(layer_contrast, 0.0, -60.0);
      timeline_document.setSelectedClipKeyframeValue(layer_contrast, 2.0, 60.0);
      if (!timeline_document.setSelectedClipKeyframeBezierHandle(
              layer_contrast, 0.0, QStringLiteral("out"), 0.5, 50.0))
        return fail("The first grade key must accept a weighted outgoing Bézier handle");
      const auto first_grade_key_is_bezier = [&] {
        for (const auto& entry : layer_keyframes()) {
          const auto key = entry.toMap();
          if (std::abs(key.value(QStringLiteral("seconds")).toDouble()) < 0.001)
            return key.value(QStringLiteral("interpolation")).toString() == QStringLiteral("bezier") &&
                   std::abs(key.value(QStringLiteral("bezierOutValue")).toDouble() - 50.0) < 0.001;
        }
        return false;
      };
      if (!first_grade_key_is_bezier()) {
        std::cerr << "Grade weighted handle model: "
                  << QJsonDocument::fromVariant(layer_keyframes())
                         .toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("The grade key must store Bézier interpolation and its weighted handle");
      }
      if (!QMetaObject::invokeMethod(curve_window, "selectCurveSecond",
                                     Q_ARG(QVariant, QVariant(0.0)),
                                     Q_ARG(QVariant, QVariant(false))) ||
          !wait_for_condition([&] {
            return item_visible(find_named_item(curve_window,
                                                QStringLiteral("timelineBezierOutHandle")));
          }, 3000)) {
        auto* graph_handle = find_named_item(
            curve_window, QStringLiteral("timelineBezierOutHandle"));
        std::cerr << "Grade weighted handle render: handleVisible="
                  << item_visible(graph_handle)
                  << " selectedSeconds="
                  << QJsonDocument::fromVariant(curve_window->property("selectedSeconds"))
                         .toJson(QJsonDocument::Compact).toStdString()
                  << " graphParameter=" << timeline_workspace->property("curveEditorParameter")
                                               .toString().toStdString()
                  << " keys=" << QJsonDocument::fromVariant(layer_keyframes())
                                    .toJson(QJsonDocument::Compact).toStdString() << '\n';
        return fail("Selecting the Bézier grade key must render its outgoing weighted handle");
      }
      process_layout_events(8);
      const auto curve_geometry_is_sound = [&](const QString& stage, qreal minimum_graph_height,
                                               bool expected_compact) {
        auto* surface = find_named_item(
            curve_window, QStringLiteral("timelineCurveEditorContentSurface"));
        if (!item_visible(surface)) {
          std::cerr << "Curve geometry " << stage.toStdString()
                    << ": content surface missing or hidden\n";
          return false;
        }
        const auto surface_rect = scene_rect(surface);
        const auto surface_bounds = surface_rect.adjusted(-1.5, -1.5, 1.5, 1.5);
        const auto window_bounds = QRectF(0, 0, curve_window->width(), curve_window->height())
                                       .adjusted(-1.5, -1.5, 1.5, 1.5);
        const auto compact = surface->property("compactCurveEditor").toBool();
        if (compact != expected_compact) {
          std::cerr << "Curve geometry " << stage.toStdString()
                    << ": compactMode=" << compact << " expected=" << expected_compact
                    << " surface=" << surface_rect.x() << ',' << surface_rect.y() << ' '
                    << surface_rect.width() << 'x' << surface_rect.height() << '\n';
          return false;
        }

        const QStringList names{
            QStringLiteral("timelineCurveTargetStatus"),
            QStringLiteral("timelineCurvePrimaryControlsRow"),
            QStringLiteral("timelineCurveActionControlsRow"),
            QStringLiteral("timelineKeyframeValueDetailRow"),
            QStringLiteral("timelineKeyframeCurveGraph"),
            QStringLiteral("timelineCurveTimeAxis"),
            QStringLiteral("timelineCurveAdvancedControlsRow")};
        QVector<QRectF> rects;
        rects.reserve(names.size());
        for (const auto& name : names) {
          auto* item = find_named_item(curve_window, name);
          if (!item_visible(item)) {
            std::cerr << "Curve geometry " << stage.toStdString()
                      << ": missing or hidden " << name.toStdString() << '\n';
            return false;
          }
          const auto rect = scene_rect(item);
          if (!surface_bounds.contains(rect) || !window_bounds.contains(rect)) {
            std::cerr << "Curve geometry " << stage.toStdString()
                      << ": " << name.toStdString() << " outside content/window bounds rect="
                      << rect.x() << ',' << rect.y() << ' ' << rect.width() << 'x' << rect.height()
                      << " content=" << surface_rect.x() << ',' << surface_rect.y() << ' '
                      << surface_rect.width() << 'x' << surface_rect.height() << '\n';
            return false;
          }
          rects.push_back(rect);
        }
        auto* header = find_named_item(curve_window, QStringLiteral("timelineCurveEditorHeaderBar"));
        const auto header_rect = scene_rect(header);
        if (!item_visible(header) || !surface_bounds.contains(header_rect) ||
            !window_bounds.contains(header_rect) ||
            header_rect.bottom() > rects.at(1).top() + 1.5) {
          std::cerr << "Curve geometry " << stage.toStdString()
                    << ": header does not fit above primary controls\n";
          return false;
        }
        for (int index = 1; index < rects.size(); ++index) {
          if (rects.at(index - 1).bottom() > rects.at(index).top() + 1.5) {
            std::cerr << "Curve geometry " << stage.toStdString()
                      << ": vertical overlap between " << names.at(index - 1).toStdString()
                      << " and " << names.at(index).toStdString() << " previousBottom="
                      << rects.at(index - 1).bottom() << " nextTop="
                      << rects.at(index).top() << '\n';
            return false;
          }
        }
        const auto graph_height = rects.at(4).height();
        if (graph_height + 1.0 < minimum_graph_height) {
          std::cerr << "Curve geometry " << stage.toStdString()
                    << ": graph height=" << graph_height
                    << " minimum=" << minimum_graph_height << '\n';
          return false;
        }
        return true;
      };
      if (!capture_visual_proof(curve_window, curve_window, proof_dir,
                                QStringLiteral("keyframe-grade-bezier"), 860, 520,
                                {QStringLiteral("timelineBezierOutHandle"),
                                 QStringLiteral("timelineCurveInterpolationControl")}, &manifest) ||
          !curve_geometry_is_sound(QStringLiteral("860x520"), 168.0, false)) {
        return fail("The normal curve layout must retain weighted handles and readable contained controls");
      }
      if (!capture_visual_proof(curve_window, curve_window, proof_dir,
                                QStringLiteral("keyframe-grade-compact"), 640, 400,
                                {QStringLiteral("timelineCurveAddPointButton"),
                                 QStringLiteral("timelineCurveParameterPickerTrigger")}, &manifest) ||
          !curve_geometry_is_sound(QStringLiteral("640x400"), 108.0, true)) {
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
      if (!close_curve_without_document_edit())
        return fail("Closing the curve through its real control must preserve the timeline document and playhead");
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
      const auto click_main_item = [&](QQuickItem* item) {
        if (!item || !item_visible(item) || item->window() != visual_window || !item->isEnabled()) return false;
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
      const auto panel_visible = [&](const QString& panel_id) {
        auto* dock_state = find_named_object(timeline_workspace, QStringLiteral("timelinePanelDockState"));
        QVariant visible;
        if (!dock_state || !QMetaObject::invokeMethod(
                dock_state, "visible", Q_RETURN_ARG(QVariant, visible),
                Q_ARG(QVariant, QVariant(panel_id))))
          return false;
        if (visible.metaType() == QMetaType::fromType<QJSValue>())
          visible = visible.value<QJSValue>().toVariant();
        return visible.toBool();
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
            auto* library = find_named_item(visual_root, QStringLiteral("timelineEffectsLibraryPanel"));
            auto* search_item = find_named_item(visual_root, QStringLiteral("timelineEffectKeyboardSearch"));
            auto* list_item = find_named_item(visual_root, QStringLiteral("timelineEffectLibraryList"));
            auto* dock_state = find_named_object(timeline_workspace, QStringLiteral("timelinePanelDockState"));
            QVariant active_panel;
            const bool effects_are_active = dock_state && QMetaObject::invokeMethod(
                dock_state, "activePanel", Q_RETURN_ARG(QVariant, active_panel),
                Q_ARG(QVariant, QVariant(QStringLiteral("right"))));
            return library && library->property("surfaceMode").toString() == QStringLiteral("library") &&
                   item_visible(library) && library->window() == visual_window &&
                   item_visible(search_item) && item_visible(list_item) &&
                   panel_visible(QStringLiteral("effects")) && effects_are_active &&
                   (active_panel.metaType() == QMetaType::fromType<QJSValue>()
                        ? active_panel.value<QJSValue>().toString() == QStringLiteral("effects")
                        : active_panel.toString() == QStringLiteral("effects"));
          }, 5000)) {
        capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("timeline-effects-add-failure"), 1440, 900,
                              {QStringLiteral("timelineEffectKeyboardSearch"),
                               QStringLiteral("timelineEffectLibraryList")}, &manifest);
        auto* library = find_named_item(visual_root, QStringLiteral("timelineEffectsLibraryPanel"));
        auto* search_item = find_named_item(visual_root, QStringLiteral("timelineEffectKeyboardSearch"));
        auto* list_item = find_named_item(visual_root, QStringLiteral("timelineEffectLibraryList"));
        std::cerr << "Effect library dock visible=" << item_visible(library)
                  << " surfaceMode=" << (library ? library->property("surfaceMode").toString().toStdString() : "<missing>")
                  << " searchVisible=" << item_visible(search_item)
                  << " listVisible=" << item_visible(list_item)
                  << " effectsPanelVisible=" << panel_visible(QStringLiteral("effects")) << '\n';
        return fail("The native Add Effect click must expose the actual searchable Effects Library dock");
      }
      auto* effect_library = find_named_item(visual_root, QStringLiteral("timelineEffectsLibraryPanel"));
      auto* effect_search = find_named_item(visual_root, QStringLiteral("timelineEffectKeyboardSearch"));
      auto* effect_list = find_named_item(visual_root, QStringLiteral("timelineEffectLibraryList"));
      if (!click_main(QStringLiteral("timelineEffectKeyboardSearch")))
        return fail("The actual Effects Library search field must accept pointer focus");
      if (!wait_for_condition([&] {
            effect_search = find_named_item(visual_root, QStringLiteral("timelineEffectKeyboardSearch"));
            return effect_search && effect_search->property("activeFocus").toBool();
          }, 3000)) {
        auto* main_focus_item = visual_window->activeFocusItem();
        const auto search_rect = effect_search ? scene_rect(effect_search) : QRectF{};
        auto* curve_focus_item = curve_window ? curve_window->activeFocusItem() : nullptr;
        std::cerr << "Effects Library pointer focus: mainActive=" << visual_window->isActive()
                  << " mainExposed=" << visual_window->isExposed()
                  << " mainFocusItem=" << (main_focus_item ? main_focus_item->objectName().toStdString() : "<none>")
                  << " curveVisible=" << (curve_window && curve_window->isVisible())
                  << " curveActive=" << (curve_window && curve_window->isActive())
                  << " curveFocusItem=" << (curve_focus_item ? curve_focus_item->objectName().toStdString() : "<none>")
                  << " curveGeometry=" << (curve_window ? curve_window->x() : 0) << ','
                  << (curve_window ? curve_window->y() : 0) << ' '
                  << (curve_window ? curve_window->width() : 0) << 'x'
                  << (curve_window ? curve_window->height() : 0)
                  << " libraryVisible=" << item_visible(effect_library)
                  << " libraryWindowIsMain=" << (effect_library && effect_library->window() == visual_window)
                  << " searchVisible=" << item_visible(effect_search)
                  << " searchEnabled=" << (effect_search && effect_search->isEnabled())
                  << " searchActiveFocus=" << (effect_search && effect_search->property("activeFocus").toBool())
                  << " searchWindowIsMain=" << (effect_search && effect_search->window() == visual_window)
                  << " searchBounds=" << search_rect.x() << ',' << search_rect.y() << ' '
                  << search_rect.width() << 'x' << search_rect.height()
                  << " inspectorTab=" << timeline_workspace->property("inspectorTab").toString().toStdString()
                  << " effectsPanelActive=" << panel_visible(QStringLiteral("effects")) << '\n';
        return fail("The actual Effects Library search field must accept pointer focus");
      }
      QKeyEvent select_effect_search(QEvent::KeyPress, Qt::Key_A,
                                     QKeySequence(QKeySequence::SelectAll)[0].keyboardModifiers());
      QCoreApplication::sendEvent(visual_window, &select_effect_search);
      for (const auto character : QStringLiteral("fade")) {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(character));
        QCoreApplication::sendEvent(visual_window, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, QString(character));
        QCoreApplication::sendEvent(visual_window, &release);
      }
      QQuickItem* fade_row = nullptr;
      int fade_index = -1;
      const auto variant_value = [](const QVariant& value) {
        return value.metaType() == QMetaType::fromType<QJSValue>()
            ? value.value<QJSValue>().toVariant() : value;
      };
      if (!wait_for_condition([&] {
            effect_library = find_named_item(visual_root, QStringLiteral("timelineEffectsLibraryPanel"));
            effect_search = find_named_item(visual_root, QStringLiteral("timelineEffectKeyboardSearch"));
            effect_list = find_named_item(visual_root, QStringLiteral("timelineEffectLibraryList"));
            if (!effect_library || !effect_search || !effect_list ||
                effect_search->property("text").toString() != QStringLiteral("fade"))
              return false;
            const auto filtered = variant_value(effect_library->property("filteredEffectLibraryItems")).toList();
            fade_index = -1;
            for (int index = 0; index < filtered.size(); ++index) {
              const auto effect = variant_value(filtered.at(index)).toMap();
              if (effect.value(QStringLiteral("name")).toString() == QStringLiteral("Fade")) {
                fade_index = index;
                break;
              }
            }
            if (fade_index < 0 || effect_list->property("count").toInt() != filtered.size())
              return false;
            effect_list->setProperty("currentIndex", fade_index);
            fade_row = effect_list->property("currentItem").value<QQuickItem*>();
            return fade_row && item_visible(fade_row) &&
                   fade_row->property("effectName").toString() == QStringLiteral("Fade") &&
                   fade_row->property("actionEnabled").toBool();
          }, 5000)) {
        std::cerr << "Effects Library typed search: search="
                  << (effect_search ? effect_search->property("text").toString().toStdString() : "<missing>")
                  << " resultCount=" << (effect_list ? effect_list->property("count").toInt() : -1)
                  << " fadeIndex=" << fade_index
                  << " rowVisible=" << item_visible(fade_row)
                  << " rowEffect=" << (fade_row ? fade_row->property("effectName").toString().toStdString() : "<missing>")
                  << " rowAddEnabled=" << (fade_row && fade_row->property("actionEnabled").toBool()) << '\n';
        return fail("Typing in the Effects Library must filter to a real compatible Fade row");
      }
      if (!capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("timeline-add-effect-search"), 1440, 900,
                                {QStringLiteral("timelineEffectKeyboardSearch"), QStringLiteral("timelineEffectLibraryList")}, &manifest)) return fail("Searchable effect browser must render with its controls");
      const auto find_add_button = [](const auto& self, QQuickItem* parent) -> QQuickItem* {
        if (!parent) return nullptr;
        if (parent->property("text").toString() == QStringLiteral("Add") &&
            item_visible(parent) && parent->isEnabled())
          return parent;
        for (auto* child : parent->childItems()) {
          if (auto* match = self(self, child)) return match;
        }
        return nullptr;
      };
      auto* fade_add_button = find_add_button(find_add_button, fade_row);
      if (!click_main_item(fade_add_button))
        return fail("The filtered Fade row's Add action must accept a real pointer click");
      if (!wait_for_condition([&] {
            return timeline_document.currentClip().value(QStringLiteral("effectStack")).toStringList().contains(QStringLiteral("Fade"));
          }, 5000) || timeline_document.currentClip().value(QStringLiteral("clipId")).toString() != keyframe_clip_id ||
          !timeline_document.currentClip().value(QStringLiteral("effectStack")).toStringList().contains(QStringLiteral("Blur")))
        return fail("The pointer-added Fade effect must reach the original clip without disturbing its existing stack");
      if (!click_main(QStringLiteral("timelineDockTab_inspector")) ||
          !wait_for_condition([&] {
            auto* inspector_panel = find_named_item(visual_root, QStringLiteral("timelineEffectsRail"));
            auto* applied_stack = find_named_item(visual_root, QStringLiteral("timelineAppliedEffectsStack"));
            return inspector_panel && item_visible(inspector_panel) &&
                   inspector_panel->property("surfaceMode").toString() == QStringLiteral("controls") &&
                   applied_stack && item_visible(applied_stack) &&
                   timeline_document.currentClip().value(QStringLiteral("effectStack")).toStringList().contains(QStringLiteral("Fade"));
          }, 5000))
        return fail("The Inspector tab must return to the real applied-effect controls after adding Fade");
      if (!capture_visual_proof(visual_window, visual_root, proof_dir, QStringLiteral("timeline-effects-workspace"), 1440, 900,
                                {QStringLiteral("timelineQuickEffectPickerButton"), QStringLiteral("timelineAppliedEffectsStack"),
                                 QStringLiteral("timelinePrimaryAddMediaButton"), QStringLiteral("timelineSaveStateChip")}, &manifest)) return fail("Effects workflow and primary timeline actions must fit together");
      if (!click_main(QStringLiteral("timelineEffectCurveButton-Blur")) ||
          !wait_for_condition([&] {
            return curve_window->isVisible() && timeline_workspace->property("curveEditorParameter").toString() == QStringLiteral("effect:Blur");
          }, 3000)) return fail("An animated applied effect must open its own curve directly from the stack");
      if (!close_curve_without_document_edit())
        return fail("Closing the reopened effect curve must preserve the timeline document and playhead");
      QFile keyframe_manifest(QDir(proof_dir).filePath(QStringLiteral("keyframe-manifest.json")));
      if (!keyframe_manifest.open(QIODevice::WriteOnly) ||
          keyframe_manifest.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) <= 0) return fail("Native keyframe proof must save screenshots and geometry");
      return EXIT_SUCCESS;
