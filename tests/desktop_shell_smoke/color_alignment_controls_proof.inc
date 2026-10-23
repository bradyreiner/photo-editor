  {
    app_shell.completeFirstRunExperience();
    if (app_shell.firstRunExperiencePending()) {
      return fail("Color alignment proof should complete first-run onboarding before mounting the editor");
    }

    const auto source_path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(
        QStringLiteral("tests/fixtures/creative-tools/landscape-mountain-source.jpg"));
    QImage source_photo(source_path);
    const auto photo_path = runtime_root.filePath(QStringLiteral("color-alignment-mountain.jpg"));
    if (source_photo.isNull() || !QFile::copy(source_path, photo_path)) {
      return fail("Color alignment proof should copy the real mountain photograph into its isolated runtime root");
    }
    const auto photo_id = QStringLiteral("color-alignment-mountain");
    catalog_assets.replaceAssetsForTesting({make_asset(
        photo_id,
        photo_path,
        QUrl::fromLocalFile(photo_path).toString())});
    catalog_assets.selectIndex(0);
    if (catalog_assets.selectedAssetId() != photo_id ||
        catalog_assets.selectedFilePath() != photo_path) {
      return fail("Color alignment proof should select the real runtime-root mountain photo before loading Main.qml");
    }

    engine.load(QUrl::fromLocalFile(main_qml_path));
    if (engine.rootObjects().isEmpty()) {
      return fail("Color alignment proof should load the real Main.qml through the production application engine");
    }
    auto* color_alignment_root = engine.rootObjects().constFirst();
    auto* color_alignment_window = qobject_cast<QQuickWindow*>(color_alignment_root);
    if (color_alignment_window == nullptr) {
      return fail("Color alignment proof requires the real Main.qml Qt Quick window");
    }
    color_alignment_root->setProperty("width", 1440);
    color_alignment_root->setProperty("height", 900);
    color_alignment_root->setProperty("visible", true);
    color_alignment_window->show();

    const auto native_window_required = qEnvironmentVariableIsSet("PHOTO_EDITOR_SMOKE_NATIVE_POINTER");
    if (native_window_required &&
        !wait_for_condition([&]() {
          return color_alignment_window->isVisible() && color_alignment_window->isExposed() &&
              color_alignment_window->winId() != 0 &&
              QApplication::platformName() != QStringLiteral("offscreen");
        }, 10000)) {
      return fail("Color alignment proof requires an exposed native window when native pointer proof is enabled");
    }

    const auto edit_workspace_index = color_alignment_root->property("editWorkspaceIndex");
    color_alignment_root->setProperty("currentWorkspace", edit_workspace_index);
    QObject* edit_workspace_host = nullptr;
    QObject* color_alignment_workspace = nullptr;
    if (!wait_for_condition([&]() {
          edit_workspace_host = find_named_object(
              color_alignment_root,
              QStringLiteral("editStudioWorkspace"));
          color_alignment_workspace = edit_workspace_host == nullptr
              ? nullptr
              : find_named_object(edit_workspace_host, QStringLiteral("editStudioWorkspaceContent"));
          // StudioWorkspace currently owns its controls directly; older shells
          // used a named content wrapper. Require the real loaded visible page.
          if (color_alignment_workspace == nullptr) color_alignment_workspace = edit_workspace_host;
          auto* host_item = qobject_cast<QQuickItem*>(edit_workspace_host);
          auto* workspace_item = qobject_cast<QQuickItem*>(color_alignment_workspace);
          return color_alignment_root->property("currentWorkspace") == edit_workspace_index &&
              !color_alignment_root->property("workspaceTransitionActive").toBool() &&
              edit_workspace_host != nullptr && edit_workspace_host->property("pageContentReady").toBool() &&
              workspace_item != nullptr && item_visible(host_item) && item_visible(workspace_item) &&
              workspace_item->window() == color_alignment_window &&
              catalog_assets.selectedAssetId() == photo_id &&
              editor_preview.assetId() == photo_id && editor_preview.hasAsset() &&
              !editor_preview.processing();
        }, 20000)) {
      std::cerr << "Color alignment route: workspace="
                << color_alignment_root->property("currentWorkspace").toInt()
                << " transition=" << color_alignment_root->property("workspaceTransitionActive").toBool()
                << " selected=" << catalog_assets.selectedAssetId().toStdString()
                << " editor=" << editor_preview.assetId().toStdString()
                << " hasAsset=" << editor_preview.hasAsset()
                << " processing=" << editor_preview.processing() << '\n';
      return fail("Color alignment proof should explicitly enter the ready Edit route with its selected mountain photo");
    }

    color_alignment_workspace->setProperty("editTool", QStringLiteral("adjust"));
    color_alignment_workspace->setProperty("editInspectorTab", QStringLiteral("adjust"));
    color_alignment_workspace->setProperty("labColorToolsVisible", true);
    color_alignment_workspace->setProperty("colorMixExpanded", true);
    color_alignment_workspace->setProperty("colorReplaceExpanded", true);
    color_alignment_workspace->setProperty("splitToningExpanded", true);
    color_alignment_workspace->setProperty("selectedToneWheel", 1);

    editor_preview.setColorMixerModel(QStringLiteral("HSL"));
    editor_preview.setHslChannel(QStringLiteral("Blues"));
    editor_preview.setHueAdjustment(22.0);
    editor_preview.setSaturationAdjustment(-14.0);
    editor_preview.setLuminanceAdjustment(9.0);
    process_layout_events(6);

    auto* color_mix_label = find_named_item(
        color_alignment_workspace,
        QStringLiteral("colorMixActiveChannelLabel"));
    auto* color_mix_values = find_named_item(
        color_alignment_workspace,
        QStringLiteral("colorMixActiveChannelValues"));
    auto* color_mix_swatch = find_named_item(
        color_alignment_workspace,
        QStringLiteral("colorMixActiveChannelSwatch"));
    QVariant color_mix_signature;
    if (color_mix_label == nullptr || color_mix_values == nullptr || color_mix_swatch == nullptr ||
        !QMetaObject::invokeMethod(
            color_alignment_workspace,
            "colorMixActiveChannelSignature",
            Q_RETURN_ARG(QVariant, color_mix_signature)) ||
        color_mix_signature.toString() != QStringLiteral("HSL|Blues|h:22|s:-14|l:9") ||
        color_mix_label->property("text").toString() != QStringLiteral("Blues / Standard") ||
        color_mix_values->property("text").toString() != QStringLiteral("H +22%   S -14%   L +9%") ||
        editor_preview.colorMixerModel() != QStringLiteral("HSL") ||
        editor_preview.hslChannel() != QStringLiteral("Blues") ||
        std::abs(editor_preview.hueAdjustment() - 22.0) > 1e-6 ||
        std::abs(editor_preview.saturationAdjustment() + 14.0) > 1e-6 ||
        std::abs(editor_preview.luminanceAdjustment() - 9.0) > 1e-6) {
      return fail("Color alignment should display Standard while preserving the complete legacy HSL channel state");
    }

    const struct {
      const char* response_name;
      const char* model_name;
    } color_mixer_responses[] = {
        {"Standard", "OKLab"},
        {"Gentle", "HSV"},
        {"Soft", "LCH"},
    };
    for (const auto& response : color_mixer_responses) {
      QVariant model_result;
      QVariant response_result;
      if (!QMetaObject::invokeMethod(
              color_alignment_workspace,
              "colorMixerResponseModel",
              Q_RETURN_ARG(QVariant, model_result),
              Q_ARG(QVariant, QString::fromLatin1(response.response_name))) ||
          model_result.toString() != QString::fromLatin1(response.model_name) ||
          !QMetaObject::invokeMethod(
              color_alignment_workspace,
              "colorMixerResponseName",
              Q_RETURN_ARG(QVariant, response_result),
              Q_ARG(QVariant, QString::fromLatin1(response.model_name))) ||
          response_result.toString() != QString::fromLatin1(response.response_name)) {
        return fail("Color mixer response mappings should round-trip Standard/OKLab, Gentle/HSV, and Soft/LCH");
      }
    }
    QVariant hsl_response_name;
    QVariant oklab_response_name;
    if (!QMetaObject::invokeMethod(
            color_alignment_workspace,
            "colorMixerResponseName",
            Q_RETURN_ARG(QVariant, hsl_response_name),
            Q_ARG(QVariant, QStringLiteral("HSL"))) ||
        hsl_response_name.toString() != QStringLiteral("Standard") ||
        !QMetaObject::invokeMethod(
            color_alignment_workspace,
            "colorMixerResponseName",
            Q_RETURN_ARG(QVariant, oklab_response_name),
            Q_ARG(QVariant, QStringLiteral("OKLab"))) ||
        oklab_response_name.toString() != QStringLiteral("Standard") ||
        editor_preview.colorMixerModel() != QStringLiteral("HSL") ||
        std::abs(editor_preview.hueAdjustment() - 22.0) > 1e-6 ||
        std::abs(editor_preview.saturationAdjustment() + 14.0) > 1e-6 ||
        std::abs(editor_preview.luminanceAdjustment() - 9.0) > 1e-6) {
      return fail("Legacy HSL and OKLab recipes should both display Standard without rewriting HSL values");
    }

    auto* skin_suggestion = find_named_item(
        color_alignment_workspace,
        QStringLiteral("colorReplaceAutoSkinButton"));
    auto* skin_protection_control = find_named_item(
        color_alignment_workspace,
        QStringLiteral("colorReplaceProtectSkinCheckBox"));
    const auto skin_suggestion_tooltip = skin_suggestion == nullptr
        ? QString()
        : skin_suggestion->property("tooltip").toString();
    const auto skin_protection_helper = skin_protection_control == nullptr
        ? QString()
        : skin_protection_control->property("helperText").toString();
    if (skin_suggestion == nullptr || skin_protection_control == nullptr ||
        skin_suggestion->property("text").toString() != QStringLiteral("Suggest protected hue") ||
        !skin_suggestion_tooltip.contains(QStringLiteral("from image colors")) ||
        !skin_suggestion_tooltip.contains(QStringLiteral("does not identify people or faces")) ||
        skin_protection_control->property("label").toString() != QStringLiteral("Protect skin-like colors") ||
        !skin_protection_helper.contains(QStringLiteral("Similar-colored objects are protected too")) ||
        !skin_protection_helper.contains(QStringLiteral("use a subject mask"))) {
      return fail("Skin protection should be labeled as a hue suggestion heuristic, not as people or face detection");
    }

    auto* grading_amount = find_named_item(
        color_alignment_workspace,
        QStringLiteral("colorGradingAmountSlider"));
    auto* grading_hue_slider = find_named_item(
        color_alignment_workspace,
        QStringLiteral("selectedToneWheelHueSlider"));
    auto* grading_saturation_slider = find_named_item(
        color_alignment_workspace,
        QStringLiteral("selectedToneWheelSaturationSlider"));
    auto* grading_luminance_slider = find_named_item(
        color_alignment_workspace,
        QStringLiteral("selectedToneWheelLuminanceSlider"));
    if (grading_amount == nullptr || grading_hue_slider == nullptr ||
        grading_saturation_slider == nullptr || grading_luminance_slider == nullptr ||
        !grading_hue_slider->isEnabled() || !grading_saturation_slider->isEnabled()) {
      return fail("Color alignment should expose the real grading wheel sliders on the base-image edit target");
    }
    QVariant modern_grading_help_result;
    if (editor_preview.currentPhotographicRenderingModel() !=
            QStringLiteral("aces2-ocio-v1") ||
        !QMetaObject::invokeMethod(
            color_alignment_workspace,
            "colorControlDomainHelpText",
            Q_RETURN_ARG(QVariant, modern_grading_help_result),
            Q_ARG(QVariant, QStringLiteral("color-grading"))) ||
        !modern_grading_help_result.toString().contains(
            QStringLiteral("Positive saturation adds tint; negative saturation removes chroma.")) ||
        !modern_grading_help_result.toString().contains(QStringLiteral("Info > Rendering"))) {
      return fail("Modern grading help should explain signed saturation and keep the Info > Rendering upgrade path visible");
    }

    QVariant committed_hue;
    QVariant committed_saturation;
    if (!QMetaObject::invokeMethod(
            grading_hue_slider,
            "testCommitInputText",
            Q_RETURN_ARG(QVariant, committed_hue),
            Q_ARG(QVariant, QStringLiteral("90°"))) ||
        !QMetaObject::invokeMethod(
            grading_saturation_slider,
            "testCommitInputText",
            Q_RETURN_ARG(QVariant, committed_saturation),
            Q_ARG(QVariant, QStringLiteral("-50%"))) ||
        !wait_for_condition([&]() {
          return std::abs(editor_preview.gradingMidtonesHue() - 0.25) <= 1e-6 &&
              std::abs(editor_preview.gradingMidtonesSaturation() + 50.0) <= 1e-6 &&
              std::abs(committed_hue.toDouble() - 0.25) <= 1e-6 &&
              std::abs(committed_saturation.toDouble() + 50.0) <= 1e-6 &&
              grading_hue_slider->property("valueText").toString() == QStringLiteral("90°") &&
              grading_saturation_slider->property("valueText").toString() == QStringLiteral("-50%");
        }, 3000)) {
      return fail("Typed hue 90 degrees and saturation -50 percent should reach the grading controller unchanged");
    }

    const auto proof_dir = qEnvironmentVariableIsSet("PHOTO_EDITOR_COLOR_ALIGNMENT_PROOF_DIR")
        ? qEnvironmentVariable("PHOTO_EDITOR_COLOR_ALIGNMENT_PROOF_DIR")
        : runtime_root.filePath(QStringLiteral("color-alignment-controls-proof"));
    if (!QDir().mkpath(proof_dir)) {
      return fail("Color alignment proof should create its native capture directory");
    }
    QJsonArray color_alignment_captures;
    const auto capture_fit = [&](const QString& name,
                                 const QSize& bounds,
                                 const QString& target_name,
                                 const QStringList& required_names) {
      color_alignment_root->setProperty("width", bounds.width());
      color_alignment_root->setProperty("height", bounds.height());
      if (!wait_for_condition([&]() {
            return color_alignment_window->width() == bounds.width() &&
                color_alignment_window->height() == bounds.height() &&
                item_visible(find_named_item(color_alignment_workspace, target_name));
          }, 5000)) {
        return false;
      }

      auto* scroll_view = find_named_item(
          color_alignment_workspace,
          QStringLiteral("editInspectorScroll"));
      auto* flickable_object = scroll_view == nullptr
          ? nullptr
          : scroll_view->property("contentItem").value<QObject*>();
      auto* content_object = flickable_object == nullptr
          ? nullptr
          : flickable_object->property("contentItem").value<QObject*>();
      auto* flickable = qobject_cast<QQuickItem*>(flickable_object);
      auto* content_item = qobject_cast<QQuickItem*>(content_object);
      auto* target_item = find_named_item(color_alignment_workspace, target_name);
      if (scroll_view == nullptr || flickable == nullptr || content_item == nullptr || target_item == nullptr) {
        return false;
      }

      const auto original_content_y = flickable->property("contentY").toDouble();
      const auto target_content_y = target_item->mapToItem(content_item, QPointF(0.0, 0.0)).y();
      flickable->setProperty("contentY", std::max(0.0, target_content_y - 24.0));
      const auto restore_scroll = [&]() {
        flickable->setProperty("contentY", original_content_y);
        process_layout_events(4);
      };
      const auto objects_fit = [&]() {
        const auto viewport = scene_rect(scroll_view);
        return std::all_of(
            required_names.cbegin(),
            required_names.cend(),
            [&](const QString& object_name) {
              const auto* item = find_named_item(color_alignment_workspace, object_name);
              return item_visible(item) && contains_with_tolerance(viewport, scene_rect(item), 2.0);
            });
      };
      if (!wait_for_condition(objects_fit, 3000)) {
        const auto viewport = scene_rect(scroll_view);
        std::cerr << "Color proof fit viewport " << viewport.x() << ',' << viewport.y()
                  << ' ' << viewport.width() << 'x' << viewport.height() << '\n';
        for (const auto& object_name : required_names) {
          const auto* item = find_named_item(color_alignment_workspace, object_name);
          const auto rect = scene_rect(item);
          std::cerr << object_name.toStdString() << " visible=" << item_visible(item)
                    << " rect=" << rect.x() << ',' << rect.y() << ' '
                    << rect.width() << 'x' << rect.height() << '\n';
        }
        color_alignment_window->grabWindow().save(
            QDir(proof_dir).filePath(name + QStringLiteral("-fit-failure.png")));
        restore_scroll();
        return false;
      }

      QImage frame;
      double captured_photo_mean = 0.0;
      double captured_photo_range = 0.0;
      const auto frame_ready = [&]() {
        color_alignment_window->requestUpdate();
        frame = color_alignment_window->grabWindow();
        const auto ratio = color_alignment_window->devicePixelRatio();
        if (editor_preview.processing() || frame.isNull() ||
            frame.size() != QSize(qRound(bounds.width() * ratio), qRound(bounds.height() * ratio))) {
          return false;
        }
        // Control geometry alone cannot qualify a native preview: reject blank
        // or placeholder frames while asynchronous photo publication settles.
        const auto* viewport = find_named_item(color_alignment_workspace, QStringLiteral("editImageViewport"));
        if (!item_visible(viewport)) return false;
        // This known photo region is also bounded in window coordinates. A
        // transformed/reused viewport item must not accidentally sample the
        // inspector and qualify a black photo using its colorful controls.
        const auto rect = scene_rect(viewport).intersected(QRectF(
            bounds.width() * 0.15, bounds.height() * 0.30,
            bounds.width() * 0.35, bounds.height() * 0.40));
        if (rect.width() < bounds.width() * 0.20 ||
            rect.height() < bounds.height() * 0.20) return false;
        auto minimum = 1.0;
        auto maximum = 0.0;
        auto total = 0.0;
        for (auto y = 0; y < 12; ++y) {
          for (auto x = 0; x < 12; ++x) {
            const auto px = qRound((rect.x() + rect.width() * (0.2 + 0.6 * (x + 0.5) / 12.0)) * ratio);
            const auto py = qRound((rect.y() + rect.height() * (0.2 + 0.6 * (y + 0.5) / 12.0)) * ratio);
            if (px < 0 || py < 0 || px >= frame.width() || py >= frame.height()) return false;
            const auto color = frame.pixelColor(px, py);
            const auto value = (double(color.redF()) + color.greenF() + color.blueF()) / 3.0;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            total += value;
          }
        }
        captured_photo_mean = total / 144.0;
        captured_photo_range = maximum - minimum;
        return captured_photo_mean > 0.03 && captured_photo_range > 0.02;
      };
      if (!wait_for_condition(frame_ready, 3000)) {
        restore_scroll();
        return false;
      }
      const auto output_path = QDir(proof_dir).filePath(name + QStringLiteral(".png"));
      const auto saved = frame.save(output_path);
      if (saved) {
        color_alignment_captures.append(QJsonObject{
            {QStringLiteral("name"), name},
            {QStringLiteral("width"), bounds.width()},
            {QStringLiteral("height"), bounds.height()},
            {QStringLiteral("pixelWidth"), frame.width()},
            {QStringLiteral("pixelHeight"), frame.height()},
            {QStringLiteral("devicePixelRatio"), color_alignment_window->devicePixelRatio()},
            {QStringLiteral("photoRegionMean"), captured_photo_mean},
            {QStringLiteral("photoRegionRange"), captured_photo_range},
            {QStringLiteral("nativeWindowId"), QString::number(static_cast<qulonglong>(color_alignment_window->winId()))},
            {QStringLiteral("path"), output_path},
            {QStringLiteral("fittedObjects"), QJsonArray::fromStringList(required_names)},
        });
      }
      restore_scroll();
      return saved;
    };

    if (!capture_fit(
            QStringLiteral("color-mix-standard-1440x900"),
            QSize(1440, 900),
            QStringLiteral("colorMixInspectorGroup"),
            {QStringLiteral("colorMixActiveChannelLabel"),
             QStringLiteral("colorMixActiveChannelValues"),
             QStringLiteral("colorMixActiveChannelSwatch"),
             QStringLiteral("hslChannelButtonRow")})) {
      return fail("Color alignment should capture mixer response and HSL controls fitted within the inspector at 1440x900");
    }
    // This inspector is scrollable. Verify every control at the same ordinary
    // window size in the two views a photographer uses, including overlap so
    // the viewport cannot silently skip a middle control.
    if (!capture_fit(
            QStringLiteral("grading-wheel-1280x850"),
            QSize(1280, 850),
            QStringLiteral("colorGradingInspectorGroup"),
            {QStringLiteral("splitToningRangeTabs"),
             QStringLiteral("splitToningFocusedWheel"),
             QStringLiteral("selectedToneWheelHueSlider"),
             QStringLiteral("selectedToneWheelSaturationSlider")})) {
      return fail("Color alignment should capture the complete grading wheel and hue/saturation controls at 1280x850");
    }
    if (!capture_fit(
            QStringLiteral("grading-numeric-controls-1280x850"),
            QSize(1280, 850),
            QStringLiteral("selectedToneWheelHueSlider"),
            {QStringLiteral("selectedToneWheelHueSlider"),
             QStringLiteral("selectedToneWheelSaturationSlider"),
             QStringLiteral("selectedToneWheelLuminanceSlider"),
             QStringLiteral("colorGradingAmountSlider"),
             QStringLiteral("colorGradingBalanceSlider")})) {
      return fail("Color alignment should make all grading numeric controls accessible by scrolling at 1280x850");
    }

    QFile manifest_file(QDir(proof_dir).filePath(QStringLiteral("manifest.json")));
    if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        manifest_file.write(QJsonDocument(QJsonObject{
            {QStringLiteral("sourcePhoto"), photo_path},
            {QStringLiteral("selectedAssetId"), photo_id},
            {QStringLiteral("nativePointerRequired"), native_window_required},
            {QStringLiteral("platform"), QApplication::platformName()},
            {QStringLiteral("captures"), color_alignment_captures},
        }).toJson(QJsonDocument::Indented)) <= 0) {
      return fail("Color alignment proof should write its native capture manifest");
    }

    const auto original_midtones_hue = editor_preview.gradingMidtonesHue();
    const auto original_midtones_saturation = editor_preview.gradingMidtonesSaturation();
    const auto original_midtones_luminance = editor_preview.gradingMidtonesLuminance();
    editor_preview.setGradingMidtonesHue(0.27);
    editor_preview.setGradingMidtonesSaturation(-100.0);
    editor_preview.setGradingMidtonesLuminance(0.0);
    const auto neutral_color = [](const QColor& color) {
      return color.isValid() &&
          std::abs(color.redF() - color.greenF()) <= 1e-6 &&
          std::abs(color.greenF() - color.blueF()) <= 1e-6;
    };
    QColor negative_saturation_preview;
    QColor negative_saturation_ui_color;
    const auto negative_preview_and_ui_are_neutral = wait_for_condition([&]() {
      QVariant negative_saturation_preview_result;
      const auto preview_helper_invoked = QMetaObject::invokeMethod(
          color_alignment_workspace,
          "gradingPreviewColor",
          Q_RETURN_ARG(QVariant, negative_saturation_preview_result),
          Q_ARG(QVariant, 0.27),
          Q_ARG(QVariant, -100.0),
          Q_ARG(QVariant, 0.0));
      negative_saturation_preview = negative_saturation_preview_result.value<QColor>();
      negative_saturation_ui_color = grading_amount->property("centerColor").value<QColor>();
      return preview_helper_invoked && neutral_color(negative_saturation_preview) &&
          neutral_color(negative_saturation_ui_color) &&
          std::abs(negative_saturation_ui_color.alphaF() - 0.45) <= 1e-3;
    }, 3000);
    editor_preview.setGradingMidtonesHue(original_midtones_hue);
    editor_preview.setGradingMidtonesSaturation(original_midtones_saturation);
    editor_preview.setGradingMidtonesLuminance(original_midtones_luminance);
    process_layout_events(4);
    if (!negative_preview_and_ui_are_neutral) {
      return fail("Negative grading saturation should be neutral in the QML preview helper and actual UI QColor");
    }

    const auto is_selected_hue_tint = [](const QColor& color, double alpha) {
      if (!color.isValid() || color.hslSaturationF() < 0.99 ||
          std::abs(color.alphaF() - alpha) > 1e-3) {
        return false;
      }
      const auto hue_distance = std::abs(color.hslHueF() - 0.27);
      return hue_distance >= 0.0 && std::min(hue_distance, 1.0 - hue_distance) < 1e-3;
    };
    for (const auto& legacy_renderer : {
             QStringLiteral("legacy-oklch-v1"),
             QStringLiteral("aces2-hellwig-jmh-v1")}) {
      photo_editor::desktop::EditState legacy_recipe;
      legacy_recipe.photographic_rendering_model = legacy_renderer;
      legacy_recipe.grading_midtones_hue = 0.27;
      legacy_recipe.grading_midtones_saturation = -100.0;
      legacy_recipe.grading_midtones_luminance = 0.0;
      // A same-asset reload correctly restores the controller's cached history
      // state over the supplied load payload. Give each legacy recipe a fresh
      // controller asset identity so this proof exercises the serialized recipe
      // load path instead of changing that production cache contract.
      const auto legacy_asset_id = photo_id + QStringLiteral("-") + legacy_renderer;
      editor_preview.loadAsset(
          legacy_asset_id,
          photo_path,
          QString(),
          QString(),
          QString(),
          QString(),
          photo_editor::desktop::serialize_edit_recipe_json(legacy_recipe));
      if (!wait_for_condition([&]() {
            return editor_preview.hasAsset() &&
                editor_preview.currentPhotographicRenderingModel() == legacy_renderer &&
                std::abs(editor_preview.gradingMidtonesHue() - 0.27) <= 1e-6 &&
                std::abs(editor_preview.gradingMidtonesSaturation() + 100.0) <= 1e-6 &&
                !editor_preview.processing();
          }, 10000)) {
        std::cerr << "Legacy grading recipe load diagnostic: requestedAsset="
                  << legacy_asset_id.toStdString()
                  << " currentAsset=" << editor_preview.assetId().toStdString()
                  << " model=" << editor_preview.currentPhotographicRenderingModel().toStdString()
                  << " hue=" << editor_preview.gradingMidtonesHue()
                  << " saturation=" << editor_preview.gradingMidtonesSaturation()
                  << " hasAsset=" << editor_preview.hasAsset()
                  << " processing=" << editor_preview.processing()
                  << " status=" << editor_preview.status().toStdString() << '\n';
        return fail("Loading a saved legacy renderer recipe should retain its grading state in the real editor preview");
      }

      QVariant legacy_preview_color_result;
      QVariant legacy_grading_help_result;
      if (!QMetaObject::invokeMethod(
              color_alignment_workspace,
              "gradingPreviewColor",
              Q_RETURN_ARG(QVariant, legacy_preview_color_result),
              Q_ARG(QVariant, 0.27),
              Q_ARG(QVariant, -100.0),
              Q_ARG(QVariant, 0.0)) ||
          !QMetaObject::invokeMethod(
              color_alignment_workspace,
              "colorControlDomainHelpText",
              Q_RETURN_ARG(QVariant, legacy_grading_help_result),
              Q_ARG(QVariant, QStringLiteral("color-grading")))) {
        return fail("Legacy grading proof should query the existing preview helper and grading help copy");
      }
      const auto legacy_preview_color = legacy_preview_color_result.value<QColor>();
      const auto legacy_ui_color = grading_amount->property("centerColor").value<QColor>();
      const auto legacy_help = legacy_grading_help_result.toString();
      if (!is_selected_hue_tint(legacy_preview_color, 1.0) ||
          !is_selected_hue_tint(legacy_ui_color, 0.45) ||
          !legacy_help.contains(
              QStringLiteral("historical tint response for both saturation signs")) ||
          !legacy_help.contains(QStringLiteral("Info > Rendering"))) {
        return fail("Saved legacy renderers should retain their selected-hue tint preview for negative saturation and explain the upgrade path");
      }
    }

    QFile proof_log(QDir(proof_dir).filePath(QStringLiteral("color-alignment-checks.txt")));
    if (!proof_log.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        proof_log.write(QByteArray("real Main.qml; mountain fixture in runtime_root; response mappings; preserved HSL values; typed hue 90 and saturation -50; modern negative QColor neutral; both saved legacy renderers retain negative hue tint; renderer-specific grading help and Info > Rendering path; skin heuristic copy; native fit captures\n")) <= 0) {
      return fail("Color alignment proof should record its completed UI checks");
    }

    editor_preview.setImageProvider(nullptr);
    qunsetenv("PHOTO_EDITOR_DATABASE_URL");
    qunsetenv("PHOTO_EDITOR_CACHE_DIR");
    qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
    std::cout << "Native color alignment controls, labels, typed grading, and captures passed\n";
    return EXIT_SUCCESS;
  }
