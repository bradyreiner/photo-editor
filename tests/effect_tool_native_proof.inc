// Exercise normal controller edits in the visible Studio, with real render and
// frame completion. Kernel before/after binaries differ only in photo_effects.cpp.
if (qEnvironmentVariableIsSet("PHOTO_EDITOR_BENCHMARK_EFFECT_TOOL")) {
  using EffectAccess = photo_editor::desktop::EditorPreviewControllerTestAccess;
  // Distinguish foreground presentation delays from a blocked GUI event loop.
  // These diagnostics are opt-in and never change application focus by default.
  const bool require_foreground =
      qEnvironmentVariableIsSet("PHOTO_EDITOR_BENCHMARK_EFFECT_ACTIVATE");
  const bool cadence_diagnostics = require_foreground ||
      qEnvironmentVariableIsSet("PHOTO_EDITOR_BENCHMARK_EFFECT_CADENCE");
  bool foreground_started = false;
  bool foreground_lost = false;
  QElapsedTimer heartbeat_clock;
  qint64 heartbeat_max_ms = 0;
  int heartbeat_gaps = 0;
  const auto window_state = [&] {
    return QVariantMap{{"window_active", root_window->isActive()},
        {"window_exposed", root_window->isExposed()},
        {"window_visibility", static_cast<int>(root_window->visibility())},
        {"application_state", static_cast<int>(QGuiApplication::applicationState())},
        {"heartbeat_max_ms", heartbeat_max_ms}, {"heartbeat_gaps_over_100ms", heartbeat_gaps}};
  };
  QTimer heartbeat;
  QObject cadence_context;
  if (cadence_diagnostics) {
    heartbeat_clock.start();
    QObject::connect(&heartbeat, &QTimer::timeout, &cadence_context, [&] {
      const auto gap = heartbeat_clock.restart();
      heartbeat_max_ms = std::max(heartbeat_max_ms, gap);
      if (gap > 100) {
        ++heartbeat_gaps;
        log_stage(QStringLiteral("effect_tool_heartbeat_gap"), gap, iteration, window_state());
      }
    });
    QObject::connect(root_window, &QWindow::activeChanged, &cadence_context, [&] {
      if (foreground_started && !root_window->isActive()) foreground_lost = true;
      log_stage(QStringLiteral("effect_tool_window_active_changed"), 0, iteration, window_state());
    });
    heartbeat.start(16);
    log_stage(QStringLiteral("effect_tool_window_initial"), 0, iteration, window_state());
  }
  // Keep the normal event dispatcher running while render completions, native
  // frames, and deferred deletions are delivered. Do not sleep the UI thread.
  const auto wait_effect = [&](const std::function<bool()>& ready, int deadline_ms) {
    if (ready()) return true;
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(5);
    deadline.start(deadline_ms);
    loop.exec();
    return ready();
  };
  const auto settled = [&] {
    return EffectAccess::all_idle(editor_preview) && !editor_preview.processing() &&
        !editor_preview.fullRenderRunning();
  };
  if (require_foreground) {
    root_window->raise();
    root_window->requestActivate();
    if (!wait_effect([&] { return root_window->isActive() && root_window->isExposed(); }, 3000)) {
      log_stage(QStringLiteral("effect_tool_window_activation_failed"), 0, iteration, window_state());
      return fail(QStringLiteral("Effect cadence probe could not activate its window."));
    }
    foreground_started = true;
    log_stage(QStringLiteral("effect_tool_window_activated"), 0, iteration, window_state());
  }
  if (!wait_effect(settled, timeout_ms))
    return fail(QStringLiteral("Photo did not settle before the effect workload."));
  if (!editor_preview.addAdjustmentLayer(QStringLiteral("Effect performance proof")))
    return fail(QStringLiteral("Could not add the effect adjustment layer."));
  const int layer_index = 0;
  if (!wait_effect(settled, timeout_ms))
    return fail(QStringLiteral("Adjustment layer did not settle."));
  const auto preview_image = [&] {
    return editor_preview_image_provider->image(
        QUrl(editor_preview.displayPreviewSource()).path().mid(1));
  };
  const auto log_display_state = [&](const QString& phase) {
    if (!qEnvironmentVariableIsSet("PHOTO_EDITOR_BENCHMARK_EFFECT_DISPLAY_STATE")) return;
    QVariantList items;
    const auto inspect = [&](const auto& self, QQuickItem* item) -> void {
      if (item == nullptr) return;
      if (item->property("source").toString().startsWith(QStringLiteral("image://editor-preview/")) ||
          item->property("loadedAssetId").isValid() ||
          item->property("nativePreviewActive").isValid() ||
          item->objectName().endsWith(QStringLiteral("NativePreviewImage"))) {
        QVariantMap row{{"object_name", item->objectName()},
            {"type", QString::fromLatin1(item->metaObject()->className())},
            {"parent_name", item->parentItem() ? item->parentItem()->objectName() : QString{}},
            {"visible", item->isVisible()}, {"opacity", item->opacity()},
            {"width", item->width()}, {"height", item->height()}};
        for (const auto* key : {"loadedAssetId", "source", "status", "nativePreviewActive",
                 "providerPreviewVisible", "inspectionDisplayReady", "paintedWidth", "paintedHeight"}) {
          const auto value = item->property(key);
          if (value.isValid()) row.insert(QString::fromLatin1(key), value);
        }
        items.append(row);
      }
      for (auto* child : item->childItems()) self(self, child);
    };
    inspect(inspect, root_window->contentItem());
    log_stage(QStringLiteral("effect_tool_display_state"), 0, iteration,
        {{"phase", phase}, {"source", editor_preview.displayPreviewSource()},
         {"asset_id", editor_preview.assetId()},
         {"native_available", editor_preview.displayPreviewNativeAvailable()},
         {"native_handle", QString::number(editor_preview.displayPreviewNativeHandle())},
         {"native_revision", editor_preview.displayPreviewNativeRevision()}, {"items", items},
         {"window_state", window_state()},
         {"edited_provider_requests", editor_preview_image_provider->editedRequests()},
         {"edited_provider_bytes", editor_preview_image_provider->editedBytes()}});
  };
  const auto hash_image = [](const QImage& input) {
    const auto image = input.convertToFormat(input.depth() > 32
        ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (int y = 0; y < image.height(); ++y)
      hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(y)),
          static_cast<qsizetype>(image.width()) * (image.depth() / 8)));
    return QString::fromLatin1(hash.result().toHex());
  };
  const auto original_hash = hash_image(preview_image());
  QString effect_id;
  std::vector<double> setters, settled_frames;
  QJsonArray hashes;
  for (int edit = 0; edit < 4; ++edit) {
    const auto old_source = editor_preview.displayPreviewSource();
    const auto old_native_revision = editor_preview.displayPreviewNativeRevision();
    QElapsedTimer timer;
    timer.start();
    if (!editor_preview.setPhotoLayerOpticalDiffusion(layer_index, effect_id,
            24.0 + edit * 2.0, 0.55, 0.25, 0.65, 0.1, 0.2, 0.73, true))
      return fail(QStringLiteral("Optical Diffusion edit was rejected."));
    const auto setter_ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    EffectAccess::stop_checkpoint_helper_and_size(editor_preview);
    const auto rows = editor_preview.photoLayerEffects(layer_index);
    if (rows.size() != 1) return fail(QStringLiteral("Effect stack lost its identity."));
    effect_id = rows.first().toMap().value("id").toString();
    if (!wait_effect([&] {
          return settled() && (editor_preview.displayPreviewSource() != old_source ||
              editor_preview.displayPreviewNativeRevision() != old_native_revision);
        }, timeout_ms))
      return fail(QStringLiteral("Effect edit did not publish a settled preview."));
    const auto render_settled_ms = timer.elapsed();
    const auto frame_floor = frame_swap_count;
    root_window->update();
    if (!wait_effect([&] { return frame_swap_count > frame_floor; }, timeout_ms))
      return fail(QStringLiteral("Settled effect preview did not complete a frame."));
    const auto frame_ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    const auto rendered = preview_image();
    if (rendered.isNull())
      return fail(QStringLiteral("Effect proof requires the published CPU preview for exact comparison."));
    const auto hash = hash_image(rendered);
    if (hash == original_hash) return fail(QStringLiteral("Effect did not change the published pixels."));
    hashes.append(QJsonObject{{"edit", edit}, {"width", rendered.width()},
        {"height", rendered.height()}, {"sha256", hash}});
    log_stage(QStringLiteral("effect_tool_edit"), timer.elapsed(), edit,
        {{"setter_ms", setter_ms}, {"render_settled_ms", render_settled_ms},
         {"settled_frame_ms", frame_ms}, {"preview_sha256", hash},
         {"presentation_wait_ms", frame_ms - static_cast<double>(render_settled_ms)},
         {"window_state", window_state()},
         {"preview_width", rendered.width()}, {"preview_height", rendered.height()},
         {"native_preview", editor_preview.displayPreviewNativeAvailable()},
         {"database_helper_dispatch", false}});
    if (edit > 0) { setters.push_back(setter_ms); settled_frames.push_back(frame_ms); }
  }
  const auto expected_effects = editor_preview.photoLayerEffects(layer_index);
  editor_preview.undo();
  if (!wait_effect(settled, timeout_ms) ||
      editor_preview.photoLayerEffects(layer_index) == expected_effects)
    return fail(QStringLiteral("Effect undo did not restore the preceding edit."));
  editor_preview.redo();
  if (!wait_effect(settled, timeout_ms) ||
      editor_preview.photoLayerEffects(layer_index) != expected_effects)
    return fail(QStringLiteral("Effect redo did not restore exact parameters."));
  log_display_state(QStringLiteral("after_redo"));
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_BENCHMARK_EFFECT_EXPORT")) {
    const auto destination = fixture_dir.filePath(QStringLiteral("effect-proof.png"));
    QElapsedTimer export_timer;
    export_timer.start();
    if (!editor_preview.startFlattenedPhotoExport(destination) ||
        !wait_effect([&] { return !editor_preview.photoExportBusy(); }, timeout_ms) ||
        !editor_preview.photoExportError().isEmpty())
      return fail(QStringLiteral("Full-resolution effect export failed: %1")
          .arg(editor_preview.photoExportError()));
    const auto export_ms = export_timer.elapsed();
    // A 40 MP RGBA16 export exceeds Qt's default 256 MiB decode limit.
    // This scoped test-reader allowance does not change production rendering.
    const auto previous_allocation_limit = QImageReader::allocationLimit();
    QImageReader::setAllocationLimit(1024);
    const QImage exported(destination);
    QImageReader::setAllocationLimit(previous_allocation_limit);
    if (exported.isNull() || exported.depth() != 64 || exported.size() != QSize(editor_preview.sourceImageWidth(), editor_preview.sourceImageHeight()))
      return fail(QStringLiteral("Effect export did not preserve source dimensions and 16-bit channels."));
    log_stage(QStringLiteral("effect_tool_export"), export_ms, iteration,
        {{"width", exported.width()}, {"height", exported.height()},
         {"pixel_sha256", hash_image(exported)}, {"bits_per_channel", 16}});
    log_display_state(QStringLiteral("after_export"));
  }
  log_summary(QStringLiteral("effect_tool_setter"), summarize_samples(setters),
      static_cast<int>(setters.size()), {{"photo_iteration", iteration}});
  log_summary(QStringLiteral("effect_tool_settled_frame"), summarize_samples(settled_frames),
      static_cast<int>(settled_frames.size()), {{"photo_iteration", iteration},
       {"exact_redo", true}, {"hashes", hashes}});
  save_capture_if_requested(root_window, capture_dir,
      QStringLiteral("effect-tool-%1").arg(iteration), &captures);
  log_display_state(QStringLiteral("after_capture"));
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_BENCHMARK_EFFECT_DISPLAY_STATE")) {
    const auto native = read_native_preview_texture(EffectAccess::native_texture(editor_preview));
    const auto cpu = preview_image();
    const auto native_hash = hash_image(native.convertToFormat(QImage::Format_RGBA8888));
    const auto cpu_hash = hash_image(cpu.convertToFormat(QImage::Format_RGBA8888));
    bool visible_photo = false;
    auto* viewport = find_visible_named_item(root, QStringLiteral("editImageViewport"));
    const QImage captured(QDir(capture_dir).filePath(QStringLiteral("effect-tool-%1.png").arg(iteration)));
    if (viewport != nullptr && !captured.isNull()) {
      QRectF content(viewport->property("paintedContentX").toDouble(),
          viewport->property("paintedContentY").toDouble(),
          viewport->property("basePaintedContentWidth").toDouble(),
          viewport->property("basePaintedContentHeight").toDouble());
      content.adjust(content.width() * .2, content.height() * .2,
          -content.width() * .2, -content.height() * .2);
      const auto scene = viewport->mapRectToScene(content);
      const auto sx = static_cast<double>(captured.width()) / root_window->width();
      const auto sy = static_cast<double>(captured.height()) / root_window->height();
      const QRect area(static_cast<int>(scene.x() * sx), static_cast<int>(scene.y() * sy),
          static_cast<int>(scene.width() * sx), static_cast<int>(scene.height() * sy));
      if (area.isValid() && captured.rect().contains(area))
        visible_photo = image_has_rendered_content(captured.copy(area));
    }
    log_stage(QStringLiteral("effect_tool_display_pixels"), 0, iteration,
        {{"native_readback_valid", !native.isNull()}, {"native_rgba8_sha256", native_hash},
         {"cpu_rgba8_sha256", cpu_hash}, {"native_equals_cpu_rgba8", !native.isNull() && native_hash == cpu_hash},
         {"captured_photo_visible", visible_photo}, {"native_width", native.width()},
         {"native_height", native.height()}});
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_BENCHMARK_EFFECT_COMPARE")) {
    auto* comparison_workspace = active_workspace(root);
    if (comparison_workspace == nullptr) return fail(QStringLiteral("Comparison workspace is missing."));
    for (const auto& mode : {QStringLiteral("split"), QStringLiteral("wipe")}) {
      QElapsedTimer opened;
      opened.start();
      if (!QMetaObject::invokeMethod(comparison_workspace, "activateEditTool",
              Q_ARG(QVariant, QStringLiteral("compare"))))
        return fail(QStringLiteral("Could not open comparison."));
      // Activation restores the last used layout; select the requested mode
      // afterward so the wipe proof cannot accidentally capture split again.
      comparison_workspace->setProperty("compareViewMode", mode);
      const auto ready_sources = [&] {
        QSet<QString> sources;
        const auto inspect = [&](const auto& self, QQuickItem* item) -> void {
          if (item == nullptr) return;
          const auto source = item->property("source").toString();
          if (item_visible(item) && item->property("paintedWidth").toDouble() > 0 &&
              item->property("status").toInt() == 1 &&
              source.startsWith(QStringLiteral("image://editor-preview/"))) sources.insert(source);
          for (auto* child : item->childItems()) self(self, child);
        };
        inspect(inspect, root_window->contentItem());
        return sources;
      };
      if (!wait_effect([&] {
            return settled() && comparison_workspace->property("compareViewMode").toString() == mode &&
                !comparison_workspace->property("editToolTransitionActive").toBool() && ready_sources().size() >= 2;
          }, timeout_ms))
        return fail(QStringLiteral("Comparison did not load both visible images: %1").arg(mode));
      const auto frame_floor = frame_swap_count;
      root_window->update();
      if (!wait_effect([&] { return frame_swap_count > frame_floor; }, timeout_ms))
        return fail(QStringLiteral("Comparison did not present a frame."));
      const auto ready_ms = opened.elapsed();
      save_capture_if_requested(root_window, capture_dir, QStringLiteral("effect-compare-") + mode, &captures);
      log_stage(QStringLiteral("effect_tool_compare"), ready_ms, iteration,
          {{"mode", mode}, {"actual_mode", comparison_workspace->property("compareViewMode")},
           {"ready_sources", QStringList(ready_sources().values())}});
      log_display_state(QStringLiteral("compare-") + mode);
    }
    if (!QMetaObject::invokeMethod(comparison_workspace, "activateEditTool",
            Q_ARG(QVariant, QStringLiteral("adjust"))) ||
        !wait_effect([&] { return settled() && !comparison_workspace->property("editToolTransitionActive").toBool(); }, timeout_ms))
      return fail(QStringLiteral("Could not return to the edited photo."));
    auto* viewport = find_visible_named_item(root, QStringLiteral("editImageViewport"));
    auto* native_texture = EffectAccess::native_texture(editor_preview);
    if (viewport == nullptr || native_texture == nullptr)
      return fail(QStringLiteral("Loupe fallback proof lacks a viewport or texture owner."));
    // Simulate loss of native presentation while preserving the published CPU
    // image. This tests the lazy loupe's first CPU fallback load, not a recipe edit.
    native_texture->clear();
    editor_preview.previewChanged();
    comparison_workspace->setProperty("loupeX", viewport->width() * .5);
    comparison_workspace->setProperty("loupeY", viewport->height() * .7);
    QElapsedTimer loupe_timer;
    loupe_timer.start();
    comparison_workspace->setProperty("loupeEnabled", true);
    const auto loupe_ready = [&] {
      auto* loupe = find_visible_named_item(root, QStringLiteral("loupeView"));
      if (!item_visible(loupe) || editor_preview.displayPreviewNativeAvailable()) return false;
      for (auto* child : loupe->childItems()) {
        if (item_visible(child) && child->property("source").toString() == editor_preview.displayPreviewSource() &&
            child->property("status").toInt() == 1 && child->property("paintedWidth").toDouble() > 0) return true;
      }
      return false;
    };
    if (!wait_effect(loupe_ready, timeout_ms)) return fail(QStringLiteral("CPU loupe fallback did not load."));
    const auto loupe_frame_floor = frame_swap_count;
    root_window->update();
    if (!wait_effect([&] { return frame_swap_count > loupe_frame_floor; }, timeout_ms))
      return fail(QStringLiteral("CPU loupe fallback did not present a frame."));
    const auto loupe_ready_ms = loupe_timer.elapsed();
    save_capture_if_requested(root_window, capture_dir, QStringLiteral("effect-cpu-loupe"), &captures);
    log_stage(QStringLiteral("effect_tool_cpu_loupe"), loupe_ready_ms, iteration,
        {{"visible_image_ready", loupe_ready()}, {"native_available", editor_preview.displayPreviewNativeAvailable()}});
    log_display_state(QStringLiteral("cpu-loupe"));
    comparison_workspace->setProperty("loupeEnabled", false);
  }
  if (require_foreground &&
      (foreground_lost || !root_window->isActive() || !root_window->isExposed()))
    return fail(QStringLiteral("Effect cadence probe lost foreground presentation; timings are invalid."));
  continue;
}
