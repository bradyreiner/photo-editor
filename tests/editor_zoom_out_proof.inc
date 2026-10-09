// Complete native QML shell: zoomed brush edits, regional publication, then Fit.
      auto* viewport = active_workspace(root)->findChild<QQuickItem*>("editImageViewport");
      if (!viewport || !editor_preview.previewViewportActive())
        return fail("Zoom-out proof lost its active viewport.");
      const auto exact_recipe = Access::recipe_json(editor_preview);
      const auto history_index = editor_preview.editHistoryCurrentIndex();
      const auto native_reference = read_native_preview_texture(Access::native_texture(editor_preview));
      if (native_reference.isNull() || !Access::full_native_coverage(editor_preview))
        return fail("Zoom-out proof must start with a complete edited native reference.");
      const auto pixel_hash = [](const QImage& image) {
        const auto rgba = image.convertToFormat(QImage::Format_RGBA64);
        return QString::fromLatin1(QCryptographicHash::hash(
            QByteArray::fromRawData(reinterpret_cast<const char*>(rgba.constBits()),
                rgba.sizeInBytes()), QCryptographicHash::Sha256).toHex());
      };
      const auto reference_hash = pixel_hash(native_reference);
      // Recreate a missing resident base through the production regional
      // publication path, using pixels read from the actual edited Metal image.
      const auto rect = editor_preview.previewViewportRect();
      const int left = static_cast<int>(std::floor(rect.left() * native_reference.width()));
      const int top = static_cast<int>(std::floor(rect.top() * native_reference.height()));
      const int right = static_cast<int>(std::ceil(rect.right() * native_reference.width()));
      const int bottom = static_cast<int>(std::ceil(rect.bottom() * native_reference.height()));
      const auto region = QRect(left, top, right - left, bottom - top).intersected(native_reference.rect());
      Access::publish_visible_patch_without_base(editor_preview, native_reference, region);
      if (!wait_for_condition([&] {
            return photo_idle() && editor_preview.displayPreviewNativeViewportFullResolution();
          }, timeout_ms) || Access::full_native_coverage(editor_preview))
        return fail("Zoom-out proof must contain only the edited visible region.");
      save_capture_if_requested(root_window, proof_dir, "zoomed-regional-edit", &captures);
      viewport->setProperty("zoom", 1.0);
      viewport->setProperty("panX", 0.0);
      viewport->setProperty("panY", 0.0);
      if (!wait_for_condition([&] {
            return !editor_preview.previewViewportActive() && photo_idle() &&
                !editor_preview.processing() &&
                (arguments.contains("--responsiveness-zoom-out-baseline") ||
                 (Access::full_native_coverage(editor_preview) &&
                  editor_preview.displayPreviewNativeViewportFullResolution()));
          }, timeout_ms)) return fail("Zoom-out proof did not settle at Fit.");
      const auto fitted = read_native_preview_texture(Access::native_texture(editor_preview));
      const auto restored = Access::full_native_coverage(editor_preview) &&
          editor_preview.displayPreviewNativeViewportFullResolution();
      const auto pixels_exact = !fitted.isNull() && pixel_hash(fitted) == reference_hash;
      if (Access::recipe_json(editor_preview) != exact_recipe ||
          editor_preview.editHistoryCurrentIndex() != history_index)
        return fail("Zoom-out restoration modified the recipe or undo position.");
      save_capture_if_requested(root_window, proof_dir, "zoom-out-fit", &captures);
      zoom_out = {{"zoomed_brush_edits", true}, {"regional_base_loss_injected", true},
          {"platform", QGuiApplication::platformName()},
          {"metal_scenegraph", root_window->rendererInterface()->graphicsApi() == QSGRendererInterface::Metal},
          {"paint_export_pixel_sha256", export_hash}, {"paint_recipe_undo_redo_exact", true},
          {"real_metal_regional_upload", true}, {"full_coverage_restored", restored},
          {"native_pixels_exact", pixels_exact}, {"reference_pixel_sha256", reference_hash},
          {"reference_image_format", static_cast<int>(native_reference.format())},
          {"fitted_image_format", static_cast<int>(fitted.format())},
          {"fitted_pixel_sha256", fitted.isNull() ? QString() : pixel_hash(fitted)},
          {"recipe_and_history_exact", true}};
      native_reference.save(QDir(proof_dir).filePath("zoom-reference-native.png"));
      fitted.save(QDir(proof_dir).filePath("zoom-fitted-native.png"));
      QFile zoom_report(QDir(proof_dir).filePath("zoom-out.json"));
      if (!zoom_report.open(QIODevice::WriteOnly)) return fail("Cannot write zoom-out report.");
      zoom_report.write(QJsonDocument(zoom_out).toJson());
      if (!arguments.contains("--responsiveness-zoom-out-baseline") && (!restored || !pixels_exact))
        return fail("Fit must restore the entire edited native image without another edit.");
