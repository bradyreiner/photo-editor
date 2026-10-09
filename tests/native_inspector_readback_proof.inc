#if defined(__APPLE__) && defined(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
    using Access = photo_editor::desktop::EditorPreviewControllerTestAccess;
    if (QGuiApplication::platformName() != "cocoa" || capture_dir.isEmpty())
      return fail("Native inspector proof needs cocoa and a capture directory.");
    if (!QDir().mkpath(capture_dir)) return fail("Cannot create inspector capture directory.");
    const auto wait_native = [&](const std::function<bool()>& ready, int milliseconds) {
      if (ready()) return true;
      QEventLoop loop; QTimer poll, deadline;
      poll.setInterval(2); deadline.setSingleShot(true);
      QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (ready()) loop.quit(); });
      QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
      poll.start(); deadline.start(milliseconds); loop.exec(); return ready();
    };
    catalog_assets.selectIndex(0);
    root->setProperty("currentWorkspace", root->property("editWorkspaceIndex"));
    root->setProperty("editWorkspaceMode", QStringLiteral("adjust"));
    editor_preview.setScopesVisible(false);
    editor_preview.setPreviewTargetEdge(2048);
    if (!wait_native([&]() {
          return editor_preview.hasAsset() && Access::all_idle(editor_preview) &&
              editor_preview.displayPreviewNativeAvailable() &&
              named_item_visible(active_workspace(root), "editImageViewport");
        }, 30000)) return fail("Inspector proof needs a visible settled native preview.");
    editor_preview.setExposure(0.17);
    if (!wait_native([&]() { return Access::all_idle(editor_preview); }, 30000))
      return fail("Inspector edit did not settle.");
    const auto surface = Access::inspector_surface(editor_preview);
    if (!surface || Access::inspector_has_cpu_image(editor_preview)) {
      const auto cpu = Access::inspector_cpu_image(editor_preview);
      const auto request = Access::full_preview_request(editor_preview);
      if (!cpu.save(QDir(capture_dir).filePath("settled-cpu-reference.png")))
        return fail("Cannot save the baseline CPU reference.");
      save_capture_if_requested(root_window, capture_dir, "settled-cpu-preview", &captures);
      QFile output(QDir(capture_dir).filePath("inspector-receipt.json"));
      if (output.open(QIODevice::WriteOnly))
        output.write(QJsonDocument(QJsonObject{{"accepted", false}, {"nativeSurface", bool(surface)},
            {"cpuImage", !cpu.isNull()}, {"width", cpu.width()}, {"height", cpu.height()},
            {"renderingModel", request.state.photographic_rendering_model}, {"renderMode", request.render_mode},
            {"tileSize", request.tile_size},
            {"allowNativeSrgb", request.allow_native_srgb_surface}, {"allowNativeDevice", request.allow_native_device_surface},
            {"requireCpu", request.require_cpu_preview_image}, {"requireCpuDisplay", request.require_cpu_display_image},
            {"scopes", request.scopes_visible}, {"scopeDomain", request.scope_domain},
            {"reason", "Settled full preview replaces the native interactive frame with a CPU image"}}).toJson());
      return fail("Inspector fixture did not reach a native-only full preview.");
    }
    // Reference readback is outside the measured interaction. It proves exact
    // displayed-byte parity; it is not included in claimed one-pixel traffic.
    QImage reference(surface->width(), surface->height(), QImage::Format_ARGB32);
    if (!surface->copy_bgra8_pixels_for_testing(
            std::span<std::uint8_t>(reference.bits(), static_cast<std::size_t>(reference.sizeInBytes()))))
      return fail("Cannot read the native display reference.");
    if (!reference.save(QDir(capture_dir).filePath("native-surface-reference.png")))
      return fail("Cannot save the native reference.");
    int reference_max_error = 0;
    const auto reference_path = option_value(arguments, "--inspector-reference");
    if (!reference_path.isEmpty()) {
      const QImage cpu_reference(reference_path);
      if (cpu_reference.size() != reference.size()) return fail("CPU/native reference dimensions differ.");
      for (int y = 0; y < reference.height(); ++y) for (int x = 0; x < reference.width(); ++x) {
        const auto cpu = cpu_reference.pixelColor(x, y), native = reference.pixelColor(x, y);
        if (cpu.alpha() != native.alpha()) return fail("CPU/native alpha differs.");
        reference_max_error = std::max({reference_max_error, std::abs(cpu.red() - native.red()),
            std::abs(cpu.green() - native.green()), std::abs(cpu.blue() - native.blue())});
      }
    }
    int completed_samples = 0;
    qint64 copied_bytes = 0;
    auto* watcher = Access::inspector_watcher(editor_preview);
    const auto sample_connection = QObject::connect(watcher, &QFutureWatcher<QVariantMap>::finished,
        &app, [&]() { ++completed_samples; copied_bytes += watcher->result().value("readbackBytes").toLongLong(); });
    const auto disconnect_sample = qScopeGuard([&]() { QObject::disconnect(sample_connection); });
    QElapsedTimer clock; clock.start();
    qint64 last_tick = 0, heartbeat_gap = 0;
    QTimer heartbeat; heartbeat.setInterval(2);
    QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&]() {
      const auto now = clock.elapsed(); heartbeat_gap = std::max(heartbeat_gap, now - last_tick); last_tick = now;
    });
    heartbeat.start();
    const auto matches = [&](QPointF point) {
      const auto value = editor_preview.pixelInspector();
      const auto sample = value.value("after").toMap();
      const int x = qRound(point.x() * (reference.width() - 1));
      const int y = qRound(point.y() * (reference.height() - 1));
      const auto pixel = reference.pixelColor(x, y);
      return value.value("hasAfter").toBool() && !value.value("pending").toBool() &&
          value.value("pixelX").toInt() == x && value.value("pixelY").toInt() == y &&
          sample.value("red").toInt() == pixel.red() && sample.value("green").toInt() == pixel.green() &&
          sample.value("blue").toInt() == pixel.blue();
    };
    QJsonArray samples;
    double maximum_sample_ms = 0;
    const auto revision = editor_preview.displayPreviewNativeRevision();
    for (const QPointF point : {QPointF(0, 0), QPointF(1, 0), QPointF(0, 1), QPointF(1, 1),
                                QPointF(.5, .5), QPointF(.137, .793)}) {
      QElapsedTimer sample_timer; sample_timer.start();
      editor_preview.updatePixelInspector(point.x(), point.y());
      const bool exact = wait_native([&]() { return Access::inspector_idle(editor_preview) && matches(point); }, 3000);
      const auto elapsed = double(sample_timer.nsecsElapsed()) / 1e6;
      maximum_sample_ms = std::max(maximum_sample_ms, elapsed);
      samples.append(QJsonObject{{"x", point.x()}, {"y", point.y()}, {"exact", exact}, {"ms", elapsed}});
      if (!exact) return fail("Native inspector sample differs from displayed pixels.");
    }
    const auto before_burst = completed_samples;
    for (int n = 0; n < 100; ++n)
      editor_preview.updatePixelInspector(double(n) / 100.0, .23);
    const QPointF latest(.83, .27);
    editor_preview.updatePixelInspector(latest.x(), latest.y());
    const bool coalesced = wait_native([&]() { return Access::inspector_idle(editor_preview) && matches(latest); }, 3000) &&
        completed_samples - before_burst <= 2;
    const auto burst_samples = completed_samples - before_burst;
    editor_preview.updatePixelInspector(.3, .7);
    editor_preview.clearPixelInspector();
    const bool clear_safe = wait_native([&]() { return Access::inspector_idle(editor_preview); }, 3000) &&
        editor_preview.pixelInspector().isEmpty();
    heartbeat.stop();
    const bool stayed_native = !Access::inspector_has_cpu_image(editor_preview) &&
        editor_preview.displayPreviewNativeRevision() == revision &&
        !Access::preview_request(editor_preview).require_cpu_preview_image;
    save_capture_if_requested(root_window, capture_dir, "native-inspector", &captures);
    const bool accepted = coalesced && clear_safe && stayed_native && reference_max_error <= 1 && maximum_sample_ms <= 100 &&
        heartbeat_gap <= 100 && completed_samples > 0 && copied_bytes == completed_samples * 4;
    const QJsonObject receipt{{"accepted", accepted}, {"width", reference.width()}, {"height", reference.height()},
        {"samples", samples}, {"completedSamples", completed_samples}, {"copiedBytes", copied_bytes},
        {"burstSamples", burst_samples}, {"coalesced", coalesced}, {"clearSafe", clear_safe},
        {"stayedNative", stayed_native}, {"maximumSampleMs", maximum_sample_ms}, {"heartbeatMaxGapMs", heartbeat_gap},
        {"cpuReferenceCompared", !reference_path.isEmpty()}, {"cpuReferenceMaxRgbError", reference_max_error},
        {"referenceReadbackBytesExcluded", static_cast<qint64>(reference.sizeInBytes())},
        {"scope", "Native source-QML Metal preview; exact displayed BGRA byte parity; worker payload counts are not hardware bus traffic"}};
    QFile output(QDir(capture_dir).filePath("inspector-receipt.json"));
    if (!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(receipt).toJson()) < 0)
      return fail("Cannot write inspector receipt.");
    std::cout << QJsonDocument(receipt).toJson().constData();
    return accepted ? EXIT_SUCCESS : fail("Native inspector acceptance failed; see receipt.");
#else
    return fail("Native inspector proof requires the Metal backend.");
#endif
