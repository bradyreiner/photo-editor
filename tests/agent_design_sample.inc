// Opt-in sample through the actual native MCP tools. A reference run is explicitly
// host-authored; the model mode chooses every construction call without phases.
#include "agent_tool_progress.h"
#include "agent_tool_call_recovery.h"
#include "agent_task_permissions.h"
#include "agent_text_glyph_mask.h"

int run_agent_design_sample() {
  const QDir output(qEnvironmentVariable("PHOTO_EDITOR_DESIGN_SAMPLE_DIR"));
  const auto runtime = qEnvironmentVariable("PHOTO_EDITOR_DESIGN_SAMPLE_RUNTIME");
  const auto source_path = qEnvironmentVariable("PHOTO_EDITOR_DESIGN_SAMPLE_SOURCE");
  const auto model_id = qEnvironmentVariable("PHOTO_EDITOR_DESIGN_SAMPLE_MODEL");
  const bool reference = qEnvironmentVariableIsSet("PHOTO_EDITOR_DESIGN_SAMPLE_REFERENCE");
  CHECK(!output.path().isEmpty() && !runtime.isEmpty() && (reference || !model_id.isEmpty()), "Supply isolated sample paths and mode");
  CHECK(QDir().mkpath(output.path()) && output.entryList(QDir::Files).isEmpty(), "Sample output must be new");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  CHECK(AppBootstrap::ensureRuntimeDirectories(), "Initialize private runtime");
  const auto hash_file = [](const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex()) : QString{};
  };
  const auto write_json = [&](const QString& name, const QVariant& data) {
    QSaveFile file(output.filePath(name));
    const auto bytes = QJsonDocument::fromVariant(data).toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
  };
  const QImage source(source_path);
  CHECK(!source.isNull(), "Read source image");
  CHECK(source.save(output.filePath("source.png")), "Retain source with editable recipe");
  const auto source_sha = hash_file(source_path);
  EditorPreviewImageProvider provider;
  EditorPreviewController editor;
  editor.setImageProvider(&provider);
  editor.loadAsset("design-sample", output.filePath("source.png"), {}, {}, {}, {}, serialize_edit_recipe_json(EditRecipeState{}));
  QElapsedTimer wait; wait.start();
  while (editor.displayPreviewSource().isEmpty() && wait.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(5);
  }
  CHECK(!editor.displayPreviewSource().isEmpty(), "Native source loaded");
  const auto baseline = editor.currentAssetBoundAdjustmentPatchJson();
  const QString task_id = reference ? "reference-design-sample" : "model-design-sample";
  CHECK(editor.beginAgentTask(task_id).value("ok").toBool(), "Own construction baseline");
  AgentEditController agent; agent.setEditingFreedom("transform");
  CatalogAssetModel catalog;
  AgentAppCommandRegistry registry;
  register_agent_photo_commands(registry, &editor, [task_id] { return task_id; });
  PhotoAutomationBridge bridge(&agent, &catalog);
  bridge.setAppCommandRegistry(&registry);
  bridge.setCompositionHandler([&](const QVariantMap& command) { return editor.runAgentTaskComposition(task_id, command); });
  CHECK(bridge.start(true, {"read", "draft"}), "Start actual private tool bridge");
  AgentMcpClient client;
  client.setEndpoint(bridge.mcpEndpoint()); client.setBearerToken(bridge.token());
  client.setTimeoutMs(60000);
  client.setAllowedTools({"photo.inspect_composition", "photo.edit_composition", "app.list_commands", "app.command_schema", "app.inspect_command", "app.invoke_command"});
  const auto discovery = client.refreshToolCache("design-sample");
  CHECK(discovery.value("ok").toBool(), "Discover actual tool schemas");
  QVariantList schemas, history, plans;
  for (const auto& value : discovery.value("tools").toList())
    if (client.allowedTools().contains(value.toMap().value("name").toString())) schemas.append(value);
  const QStringList cover_copy{"SAMPLE / FIELD NOTES 01", "AFTER THE LIGHT", "A STUDY IN BLUE HOUR"};
  const QString brief = QStringLiteral("Create an editorial travel cover on this existing %1 by %2 image. Preserve the photograph, framing and color. Use exactly three editable text layers: the small kicker SAMPLE / FIELD NOTES 01, the headline AFTER THE LIGHT, and the footer A STUDY IN BLUE HOUR. Use a strong elegant headline in the open sky, consistent left alignment, generous safe margins, warm white type and clear size hierarchy: the headline must be at least three times the size of both the kicker and footer. Keep the mountains and lake visible. The word SAMPLE must remain legible. Choose a real font family and face from the inspected fontFaces list, and set both explicitly. An empty fontAxes list is valid for a fixed font when fontFamilyAvailable and fontFaceAvailable are true. Deliver the editable composition; do not duplicate text. The active assetId is design-sample. Choose the design and tools yourself.").arg(source.width()).arg(source.height());
  const QVariantMap approved_plan{
      {"summary", QStringLiteral("Create an editable editorial travel cover while preserving the original photo.")},
      {"outcome", QStringLiteral("A finished three-line cover with the original photograph intact.")},
      {"actions", QVariantList{QVariantMap{
          {"kind", QStringLiteral("text")},
          {"summary", QStringLiteral("Create three editable text layers using the approved copy.")}}}},
      {"exactCopy", cover_copy},
      {"deliverables", QStringList{QStringLiteral("editable photo cover")}},
      {"targetIds", QStringList{QStringLiteral("design-sample")}}};
  const auto approved_capabilities = agent_task_capabilities_for_plan(approved_plan);
  CHECK((approved_capabilities == QStringList{QStringLiteral("graphics"), QStringLiteral("layers_luts")}),
      "Derive task-scoped permissions from the approved text deliverable");
  const QVariantMap approved_execution_brief{
      {"prompt", brief}, {"answers", QVariantMap{}}, {"plan", approved_plan},
      {"exactCopy", cover_copy}, {"deliverables", approved_plan.value(QStringLiteral("deliverables"))},
      {"isRevision", false}};
  const QVariantMap approved_task_snapshot{
      {"contract", QStringLiteral("agent-approved-session-v1")},
      {"briefVersion", 1},
      {"prompt", brief},
      {"answers", QVariantMap{}},
      {"targetIds", QStringList{QStringLiteral("design-sample")}},
      {"exactCopy", cover_copy},
      {"deliverables", approved_plan.value(QStringLiteral("deliverables"))},
      {"plan", approved_plan},
      {"isRevision", false},
      {"approvedInstructions", QStringLiteral("Complete the approved outcome, retaining exact copy, target and deliverable.\nApproved brief:\n%1")
          .arg(QString::fromUtf8(QJsonDocument::fromVariant(approved_execution_brief).toJson(QJsonDocument::Compact)))},
      {"taskScopedCapabilities", approved_capabilities}};
  const QVariantMap approved_task_context{
      {"kind", QStringLiteral("approved-task-snapshot")},
      {"authority", QStringLiteral("approved-brief-and-deliverables")},
      {"snapshot", approved_task_snapshot}};
  QVariantMap receipt{{"contract", "agent-design-sample-v1"}, {"mode", reference ? "host-authored-native-tool-reference" : "unrestricted-local-model-construction"},
      {"brief", brief}, {"sourceSha256", source_sha}, {"modelId", model_id}, {"hostPreparedCanvas", false},
      {"approvedTaskSnapshot", approved_task_snapshot},
      {"productionMcpTools", true}, {"nativeUiTested", false}, {"catalogPersistenceTested", false}, {"modelChoseEveryConstructionCall", !reference}};
  AgentLocalModelPlanRequest request;
  request.model_id = model_id; request.force_model_planning = true; request.allow_deterministic_fallback = false;
  request.allow_tool_call_plan = true; request.available_tool_schemas = schemas;
  for (const auto& style : built_in_agent_edit_styles())
    if (style.toMap().value("id") == "natural-portrait") request.style = style.toMap();
  if (!model_id.isEmpty()) CHECK(probe_agent_local_model(model_id).value("available").toBool(), "Real model available");
  QVariantMap latest_asset;
  const auto render_canonical = [&](const EditRecipeState& state, const QString& stage) {
    const auto source_path_for_plan = QFileInfo(output.filePath(QStringLiteral("source.png")))
                                         .canonicalFilePath();
    const auto admission = RenderPlanStore::shared().admit(
        QStringLiteral("design-sample"), source_path_for_plan, state,
        source.size(), photo_editor::pipeline::RenderPurpose::Export,
        photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation);
    if (!admission.admitted || !admission.plan.has_value()) {
      receipt.insert(stage + QStringLiteral("CanonicalRenderPlanError"), admission.error);
      return QImage{};
    }
    receipt.insert(stage + QStringLiteral("CanonicalRenderPlanCacheKey"),
        admission.plan_cache_key);
    receipt.insert(stage + QStringLiteral("CanonicalRecipeCacheKey"),
        admission.recipe_cache_key);
    return render_output_image_with_backend(
        source, state, QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888,
        RenderBackendPreference::CpuOnly, nullptr, nullptr, {},
        photo_editor::pipeline::RemovalQuality::Final,
        RenderOutputPolicy::DisplayReferred, &*admission.plan);
  };
  auto proof = [&](const QString& name) {
    const auto state = EditorPreviewControllerTestAccess::state(editor);
    const auto image = render_canonical(state, name);
    const auto path = output.filePath(name + ".png");
    if (image.isNull() || !image.save(path)) return false;
    latest_asset = {{"assetId", "design-sample"}, {"proofImagePath", path}, {"planningProofRequired", true}, {"planningProofSha256", hash_file(path)}};
    return true;
  };
  const auto call = [&](const QString& name, const QVariantMap& args) {
    const auto result = client.callTool(name, args);
    history.append(QVariantMap{{"name", name}, {"arguments", args}, {"result", result}});
    receipt.insert("toolReports", history); write_json("run-receipt.json", receipt);
    return result;
  };
  QVariantList font_inspection_reports;
  const auto inspect_font = [&](const QString& family, const QString& face) {
    QVariantMap values{{QStringLiteral("family"), family}};
    if (!face.isEmpty()) values.insert(QStringLiteral("face"), face);
    const QVariantMap arguments{{QStringLiteral("assetId"), QStringLiteral("design-sample")},
        {QStringLiteral("values"), values}};
    const auto result = client.callTool(QStringLiteral("photo.inspect_composition"), arguments);
    font_inspection_reports.append(QVariantMap{
        {QStringLiteral("arguments"), arguments}, {QStringLiteral("result"), result}});
    receipt.insert(QStringLiteral("fontInspectionReports"), font_inspection_reports);
    write_json(QStringLiteral("run-receipt.json"), receipt);
    return result;
  };
  CHECK(proof("baseline"), "Render original proof");
  const auto assess = [&](const QString& label) {
    const auto inspected = call("photo.inspect_composition", {{"assetId", "design-sample"}});
    QVariantList text_layers, details, typography_readback; QStringList paths, copy, issues;
    double kicker_size = 0, headline_size = 0, footer_size = 0;
    const QImage image(latest_asset.value("proofImagePath").toString());
    QVariantList native_text_layers;
    for (const auto& value : EditorPreviewControllerTestAccess::state(editor).layers) {
      const auto layer = value.toMap();
      if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text"))
        native_text_layers.append(layer);
    }
    for (const auto& value : inspected.value("structuredContent").toMap().value("layers").toList()) {
      const auto layer = value.toMap(); if (layer.value("kind") != "text") continue;
      // UI thumbnail URLs are not proof metadata. Keep the exact semantic
      // layer description; overview/detail images carry the required pixels.
      QVariantMap expected_layer;
      for (const auto& key : {"id", "kind", "name", "text", "x", "y", "pixelWidth", "pixelHeight", "canvasWidth", "canvasHeight", "position", "enabled", "opacity", "rotation", "scale", "textTypography"})
        if (layer.contains(key)) expected_layer.insert(key, layer.value(key));
      text_layers.append(expected_layer); const auto text = layer.value("text").toString(); copy.append(text);
      const auto size = layer.value("textTypography").toMap().value("size").toDouble();
      const auto layout = agent_graphic_proof_layout_metrics(layer);
      const auto layer_scale = layout.value("scale").toDouble();
      const auto effective_size = layout.value("effectiveTypographySize").toDouble();
      if (!layout.value("available").toBool() ||
          !layout.value("typographySizeAvailable").toBool())
        issues.append("Text has invalid rendered geometry or effective typography size: " + text);
      const auto typography = layer.value("textTypography").toMap();
      const auto family = typography.value("family").toString().trimmed();
      const auto face = typography.value("face").toString().trimmed();
      const auto font_inspection_result = inspect_font(family, face);
      const auto font_inspection = font_inspection_result
          .value("structuredContent").toMap();
      const auto font_family_available =
          font_inspection.value(QStringLiteral("fontFamilyAvailable")).toBool();
      const auto font_face_available =
          font_inspection.value(QStringLiteral("fontFaceAvailable")).toBool();
      QStringList font_faces;
      for (const auto& face_value : font_inspection.value(QStringLiteral("requestedFamilyFaces")).toList())
        font_faces.append(face_value.toString());
      const auto font_axes = font_inspection.value(QStringLiteral("fontAxes")).toList();
      const auto requested_face_preserved =
          font_inspection.value(QStringLiteral("fontFamily")).toString() == family &&
          font_inspection.value(QStringLiteral("fontFace")).toString() == face;
      const auto face_listed = std::any_of(font_faces.cbegin(), font_faces.cend(),
          [&face](const QString& available) {
            return available.compare(face, Qt::CaseInsensitive) == 0;
          });
      const auto weight = typography.value("weight").toInt();
      const auto style_color = QColor(typography.value("color").toString());
      const auto explicit_typography = typography.contains("family") && !family.isEmpty() &&
          typography.contains("face") && !face.isEmpty() &&
          typography.contains("size") && size > 0 && typography.contains("weight") && weight > 0 &&
          typography.contains("color") && style_color.isValid();
      if (!explicit_typography) issues.append("Text readback is missing an explicit font family, face, size, weight or color: " + text);
      if (!font_inspection.value(QStringLiteral("ok")).toBool() ||
          !font_family_available || !font_face_available || !face_listed ||
          !requested_face_preserved) {
        issues.append("The requested font family or face is unavailable; choose a face from native fontFaces without substituting: " + text);
      }
      typography_readback.append(QVariantMap{{"text", text}, {"family", family}, {"face", face},
          {"size", size}, {"layerScale", layer_scale}, {"effectiveTypographySize", effective_size},
          {"boundsBasis", layout.value("boundsBasis")},
          {"rasterFrameWithinCanvas", layout.value("rasterFrameWithinCanvas")},
          {"effectPaddingIncluded", layout.value("effectPaddingIncluded")},
          {"visibleInkBoundsAvailable", layout.value("visibleInkBoundsAvailable")},
          {"weight", weight}, {"color", style_color.name(QColor::HexArgb)},
          {"explicitTypography", explicit_typography},
          {"fontFamilyAvailable", font_family_available}, {"fontFaces", font_faces},
          {"fontFaceAvailable", font_face_available}, {"requestedFacePreserved", requested_face_preserved},
          {"fontAxes", font_axes}, {"emptyAxesValidForAvailableFace", font_face_available && font_axes.isEmpty()},
          {"usesSystemDefaultFont", family.compare(QStringLiteral("System"), Qt::CaseInsensitive) == 0}});
      if (text == "SAMPLE / FIELD NOTES 01") kicker_size = effective_size;
      if (text == "AFTER THE LIGHT") headline_size = effective_size;
      if (text == "A STUDY IN BLUE HOUR") footer_size = effective_size;
      const auto rendered_bounds = layout.value("renderedBounds").toMap();
      const QRectF bounds(
          rendered_bounds.value("x").toDouble(),
          rendered_bounds.value("y").toDouble(),
          rendered_bounds.value("width").toDouble(),
          rendered_bounds.value("height").toDouble());
      if (!layout.value("available").toBool() ||
          !layout.value("rasterFrameWithinCanvas").toBool())
        issues.append("The declared transformed text raster frame extends outside the output canvas: " + text);
      const auto crop = bounds.toAlignedRect().adjusted(-12, -12, 12, 12).intersected(image.rect());
      if (crop.isEmpty()) continue;
      const auto path = output.filePath(QStringLiteral("%1-detail-%2.png").arg(label).arg(text_layers.size()));
      if (!image.copy(crop).save(path)) { issues.append("A full-resolution text proof is unavailable."); continue; }
      paths.append(path);
      details.append(QVariantMap{{"imagePath", path}, {"imageSha256", hash_file(path)}, {"sourceOverviewSha256", latest_asset.value("planningProofSha256")},
          {"available", true}, {"omitted", false}, {"layerId", layer.value("id")},
          {"boundsBasis", layout.value("boundsBasis")},
          {"effectPaddingIncluded", layout.value("effectPaddingIncluded")},
          {"pixelBounds", QVariantMap{{"x", crop.x()}, {"y", crop.y()}, {"width", crop.width()}, {"height", crop.height()}}}});
    }
    const QImage baseline_image(output.filePath("baseline.png"));
    QString native_mask_error;
    const auto native_text_coverage = agent_native_text_coverage_mask(
        native_text_layers, image.size(), &native_mask_error);
    bool background_preserved = baseline_image.size() == image.size() &&
        !image.isNull() && !native_text_coverage.isNull();
    int changed_photo_pixels_outside_native_text = 0;
    int maximum_photo_channel_delta_outside_native_text = 0;
    for (int y = 0; y < image.height() && background_preserved; ++y) for (int x = 0; x < image.width(); ++x) {
      if (qAlpha(native_text_coverage.pixel(x, y)) != 0) continue;
      const auto before = baseline_image.pixelColor(x, y);
      const auto after = image.pixelColor(x, y);
      if (before != after) {
        ++changed_photo_pixels_outside_native_text;
        maximum_photo_channel_delta_outside_native_text = qMax(
            maximum_photo_channel_delta_outside_native_text,
            qMax(qAbs(before.red() - after.red()),
                qMax(qAbs(before.green() - after.green()),
                    qAbs(before.blue() - after.blue()))));
      }
    }
    background_preserved = background_preserved &&
        changed_photo_pixels_outside_native_text == 0;
    if (!background_preserved) issues.append(native_mask_error.isEmpty()
        ? "Pixels outside the actual native text raster changed despite the protected-photo brief."
        : "The native text raster coverage could not be verified: " + native_mask_error);
    const bool copy_ok = copy.size() == 3 && copy.contains("SAMPLE / FIELD NOTES 01") && copy.contains("AFTER THE LIGHT") && copy.contains("A STUDY IN BLUE HOUR");
    if (!copy_ok) issues.append("The approved three exact editable text layers are incomplete or duplicated.");
    if (headline_size < 3 * kicker_size || headline_size < 3 * footer_size || kicker_size <= 0 || footer_size <= 0)
      issues.append("The brief requires the rendered headline size (typography size × layer scale) to be at least three times the small kicker and quiet footer.");
    QVariantList rendered_ink_layers;
    bool rendered_warm_white = true;
    const QImage baseline_image_for_ink(output.filePath("baseline.png"));
    for (const auto& value : inspected.value("structuredContent").toMap().value("layers").toList()) {
      const auto layer = value.toMap();
      if (layer.value("kind").toString() != "text") continue;
      const auto text = layer.value("text").toString();
      const auto local = rasterize_legacy_photo_text(layer);
      QImage glyph_mask(image.size(), QImage::Format_ARGB32_Premultiplied);
      if (!local.isNull() && !glyph_mask.isNull()) {
        glyph_mask.fill(Qt::transparent);
        const auto scale = qBound(0.05, layer.value("scale", 1.0).toDouble(), 32.0);
        const auto rotation = layer.value("rotation", 0.0).toDouble();
        const auto x = layer.value("x").toDouble();
        const auto y = layer.value("y").toDouble();
        const auto width = layer.value("pixelWidth", local.width()).toDouble();
        const auto height = layer.value("pixelHeight", local.height()).toDouble();
        QPainter painter(&glyph_mask);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.translate(x + width * scale * 0.5, y + height * scale * 0.5);
        painter.rotate(rotation);
        painter.scale(scale, scale);
        painter.drawImage(QPointF(-local.width() * 0.5, -local.height() * 0.5), local);
        painter.end();
      }
      int opaque_glyph_pixels = 0;
      int warm_white_pixels = 0;
      int maximum_red = 0;
      int maximum_green = 0;
      int maximum_blue = 0;
      if (!local.isNull() && !glyph_mask.isNull() && image.size() == baseline_image_for_ink.size()) {
        for (int y = 0; y < image.height(); ++y) {
          for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(glyph_mask.pixel(x, y)) < 230 || image.pixel(x, y) == baseline_image_for_ink.pixel(x, y)) continue;
            const auto pixel = image.pixelColor(x, y);
            ++opaque_glyph_pixels;
            maximum_red = qMax(maximum_red, pixel.red());
            maximum_green = qMax(maximum_green, pixel.green());
            maximum_blue = qMax(maximum_blue, pixel.blue());
            if (pixel.red() >= 210 && pixel.green() >= 200 && pixel.blue() >= 170 &&
                pixel.red() >= pixel.blue() - 20) ++warm_white_pixels;
          }
        }
      }
      const bool line_warm_white = opaque_glyph_pixels >= 50 && warm_white_pixels * 100 >= opaque_glyph_pixels * 85;
      rendered_warm_white = rendered_warm_white && line_warm_white;
      rendered_ink_layers.append(QVariantMap{{"text", text}, {"opaqueChangedGlyphPixels", opaque_glyph_pixels},
          {"warmWhitePixels", warm_white_pixels}, {"maximumRenderedRgb", QVariantList{maximum_red, maximum_green, maximum_blue}},
          {"passed", line_warm_white}});
      if (!line_warm_white) issues.append("Rendered glyph pixels do not meet the warm-white contrast threshold: " + text);
    }
    QVariantMap assessment{{"copyAndLayerCountPassed", copy_ok}, {"issues", issues}, {"backgroundPixelsPreserved", background_preserved},
        {"backgroundPixelComparison", QStringLiteral("exact RGB equality outside union of native text raster alpha")},
        {"changedPhotoPixelsOutsideNativeText", changed_photo_pixels_outside_native_text},
        {"maximumPhotoChannelDeltaOutsideNativeText", maximum_photo_channel_delta_outside_native_text},
        {"nativeTextRasterMaskError", native_mask_error},
        {"threeToOneHeadlineHierarchyPassed", headline_size >= 3 * kicker_size && headline_size >= 3 * footer_size && kicker_size > 0 && footer_size > 0},
        {"headlineEffectiveTypographySize", headline_size}, {"kickerEffectiveTypographySize", kicker_size},
        {"footerEffectiveTypographySize", footer_size},
        {"typographyReadback", typography_readback}, {"renderedWarmWhitePassed", rendered_warm_white},
        {"renderedInkLayers", rendered_ink_layers}, {"deterministicPassed", issues.isEmpty()}};
    if (!model_id.isEmpty() && !text_layers.isEmpty()) {
      auto review_asset = latest_asset; review_asset.insert("additionalProofImagePaths", paths);
      review_asset.insert("visualEvidence", QVariantMap{{"targetedDetailProofs", details}});
      AgentLocalModelProofCritiqueRequest review;
      review.model_id = model_id; review.style = request.style; review.asset = review_asset;
      QVariantList composition_receipts;
      for (const auto& history_value : history) {
        auto receipt = history_value.toMap();
        const auto tool_result = receipt.value(QStringLiteral("result")).toMap();
        receipt.insert(QStringLiteral("toolName"), receipt.value(QStringLiteral("name")));
        receipt.insert(QStringLiteral("ok"), tool_result.value(QStringLiteral("ok")));
        receipt.insert(QStringLiteral("structuredContent"),
                       tool_result.value(QStringLiteral("structuredContent")));
        composition_receipts.append(receipt);
      }
      review.user_instructions = brief;
      review.graphic_layers = agent_graphic_proof_layers(
          inspected.value("structuredContent").toMap().value("layers").toList(),
          agent_composition_text_layer_ids(composition_receipts));
      review.validation_report = {{"ok", issues.isEmpty()}, {"issues", issues}, {"compositionReview", QVariantMap{{"detailProofReport", QVariantMap{{"renderedCompositionWidth", image.width()}, {"renderedCompositionHeight", image.height()}}}}}};
      const auto critique = critique_agent_rendered_proof(review);
      assessment.insert("modelReview", QVariantMap{{"ok", critique.ok}, {"pass", critique.pass}, {"needsUserReview", critique.needs_user_review}, {"summary", critique.summary}, {"issues", critique.issues}, {"repairHints", critique.repair_hints}, {"error", critique.error_message}, {"promptAudit", critique.prompt_audit}});
      assessment.insert("visualReviewPassed", critique.ok && critique.pass && !critique.needs_user_review);
    }
    assessment.insert("passed", issues.isEmpty() && (model_id.isEmpty() || assessment.value("visualReviewPassed").toBool()));
    write_json(label + "-review.json", assessment); return assessment;
  };
  if (reference) {
    CHECK(call("photo.inspect_composition", {{"assetId", "design-sample"}}).value("ok").toBool(), "Inspect source/fonts");
    const auto text = [&](const QString& copy, int x, int y, int size, int weight, double tracking) {
      const auto added = call("photo.edit_composition", {{"assetId", "design-sample"}, {"operation", "add_text"}, {"values", QVariantMap{{"text", copy}, {"x", x}, {"y", y}}}});
      const auto id = added.value("structuredContent").toMap().value("createdLayerId").toString();
      if (!added.value("ok").toBool() || id.isEmpty()) return false;
      return call("photo.edit_composition", {{"assetId", "design-sample"}, {"operation", "typography"}, {"layerId", id},
          {"values", QVariantMap{{"family", "Avenir Next"}, {"size", size}, {"weight", weight}, {"tracking", tracking}, {"color", "#fff3df"}}}}).value("ok").toBool();
    };
    CHECK(text("SAMPLE / FIELD NOTES 01", 64, 64, 18, 600, 3.0), "Construct editable kicker");
    CHECK(text("AFTER THE LIGHT", 58, 108, 88, 800, -2.0), "Construct editable headline");
    CHECK(text("A STUDY IN BLUE HOUR", 64, source.height() - 96, 20, 500, 2.0), "Construct editable footer");
    CHECK(proof("constructed"), "Review reference pixels");
  } else {
    bool complete = false;
    int repeated_validation_rounds = 0;
    QString previous_validation_fingerprint;
    int repeated_incomplete_intents = 0;
    QString previous_incomplete_intent_fingerprint;
    QVariantMap last_review;
    AgentToolProgress progress;
    // This is an evaluation budget, not a production task limit. Preserve a
    // partial result and fail when the evaluation budget is exhausted.
    for (int round = 0; round < 24; ++round) {
      CHECK(call("photo.inspect_composition", {{"assetId", "design-sample"}}).value("ok").toBool(), "Inspect fresh model state");
      request.asset = latest_asset;
      request.user_instructions = approved_task_snapshot
          .value(QStringLiteral("approvedInstructions")).toString();
      if (!last_review.isEmpty()) {
        if (!last_review.value(QStringLiteral("passed")).toBool()) {
          request.user_instructions += "\nThe current rendered review rejected completion. Repair these specific defects using available tools, preserve successful work, and do not duplicate text: " + QString::fromUtf8(QJsonDocument::fromVariant(last_review).toJson(QJsonDocument::Compact));
        } else {
          request.user_instructions += "\nThe latest rendered review passed. Check the approved composition scope, exact copy, and native readback; if they are still met, declare completion without further mutation. Make another edit only if the approved brief or current evidence identifies a remaining defect. The host verifies durable artifact save, reopen and Revert after composition completion.";
        }
      }
      request.current_adjustment_patch_json = editor.currentAdjustmentPatchJson(); request.prior_tool_results = history;
      request.prior_tool_results.append(approved_task_context);
      wait.restart(); const auto plan = plan_agent_recipe_patch(request);
      plans.append(QVariantMap{{"ok", plan.ok}, {"decision", plan.decision}, {"error", plan.error_message}, {"reason", plan.safe_stop_reason}, {"toolCalls", plan.tool_call_plan}, {"modelId", plan.model_id}, {"promptAudit", plan.prompt_audit}, {"elapsedMs", wait.elapsed()}});
      receipt.insert("plans", plans); CHECK(write_json("run-receipt.json", receipt), "Record model turn");
      std::cout << "round=" << round + 1 << " decision=" << plan.decision.toStdString() << std::endl;
      if (!plan.ok) {
        if (plan.decision == QStringLiteral("edit_intent")) {
          const auto fingerprint = QString::fromLatin1(
              QCryptographicHash::hash(
                  (plan.decision + QLatin1Char('\n') + plan.error_message).toUtf8(),
                  QCryptographicHash::Sha256).toHex());
          repeated_incomplete_intents = fingerprint == previous_incomplete_intent_fingerprint
              ? repeated_incomplete_intents + 1 : 0;
          previous_incomplete_intent_fingerprint = fingerprint;
          last_review = assess(QStringLiteral("round-%1").arg(round + 1));
          auto model_review = last_review.value(QStringLiteral("modelReview")).toMap();
          model_review.remove(QStringLiteral("promptAudit"));
          last_review.insert(QStringLiteral("modelReview"), model_review);
          auto rejected_plan = plans.last().toMap();
          rejected_plan.insert(QStringLiteral("currentStateReview"), last_review);
          rejected_plan.insert(QStringLiteral("recoveryAction"),
              QStringLiteral("review_current_pixels_then_request_corrected_tool_call_plan"));
          plans[plans.size() - 1] = rejected_plan;
          receipt.insert(QStringLiteral("plans"), plans);
          receipt.insert(QStringLiteral("incompleteEditIntentRecoveryCount"),
              receipt.value(QStringLiteral("incompleteEditIntentRecoveryCount")).toInt() + 1);
          CHECK(write_json(QStringLiteral("run-receipt.json"), receipt),
              "Record review and recovery after a no-action edit intent");
          if (last_review.value(QStringLiteral("passed")).toBool()) {
            receipt.insert(QStringLiteral("stoppedForUnconfirmedCompletion"), true);
            receipt.insert(QStringLiteral("unconfirmedCompletionReason"), plan.error_message);
            CHECK(write_json(QStringLiteral("run-receipt.json"), receipt),
                "Record that the model failed to declare completion");
            break;
          }
          request.require_tool_call_plan = true;
          if (repeated_incomplete_intents >= 2) {
            receipt.insert(QStringLiteral("stoppedForRepeatedIncompleteEditIntent"), true);
            receipt.insert(QStringLiteral("lastIncompleteEditIntentFingerprint"), fingerprint);
            break;
          }
          continue;
        }
        const auto recovery = agent_tool_call_validation_recovery_envelope(plan, schemas);
        if (recovery.isEmpty()) break;
        const auto fingerprint = agent_tool_call_validation_recovery_fingerprint(plan);
        repeated_validation_rounds = fingerprint == previous_validation_fingerprint
            ? repeated_validation_rounds + 1 : 0;
        previous_validation_fingerprint = fingerprint;
        history.append(recovery);
        receipt.insert("toolCallValidationRecoveryCount", receipt.value("toolCallValidationRecoveryCount").toInt() + 1);
        receipt.insert("lastToolCallValidationRecovery", recovery);
        auto rejected_plan = plans.last().toMap();
        rejected_plan.insert("toolCallValidationRecovery", recovery);
        plans[plans.size() - 1] = rejected_plan;
        receipt.insert("plans", plans);
        CHECK(write_json("run-receipt.json", receipt), "Record typed planner rejection and recovery instructions");
        request.require_tool_call_plan = true;
        if (repeated_validation_rounds >= 2) {
          receipt.insert("stoppedForRepeatedToolValidation", true);
          break;
        }
        continue;
      }
      if (plan.decision == "edit_intent") {
        last_review = assess(QStringLiteral("round-%1").arg(round + 1));
        // Keep bulky prompt audits in the review artifact, not a repair prompt.
        auto model_review = last_review.value("modelReview").toMap(); model_review.remove("promptAudit");
        last_review.insert("modelReview", model_review);
        if (last_review.value("passed").toBool()) { complete = true; break; }
        request.require_tool_call_plan = true;
        continue;
      }
      const auto native_completion = plan.prompt_audit
          .value(QStringLiteral("nativeCompositionCompletion")).toMap();
      const auto verified_native_completion =
          plan.ok && plan.decision == QStringLiteral("recipe_patch") &&
          plan.recipe_patch.isEmpty() && plan.tool_call_plan.isEmpty() &&
          plan.prompt_audit.value(QStringLiteral("modelDecision")).toString() ==
              QStringLiteral("edit_intent") &&
          plan.prompt_audit.value(QStringLiteral("completionMode")).toString() ==
              QStringLiteral("preserve-after-native-composition-readback") &&
          native_completion.value(QStringLiteral("requested")).toBool() &&
          native_completion.value(QStringLiteral("assetId")).toString() ==
              QStringLiteral("design-sample") &&
          native_completion.value(QStringLiteral("acceptedMutationCount")).toInt() > 0 &&
          native_completion.value(QStringLiteral("readbackConfirmed")).toBool();
      if (verified_native_completion) {
        last_review = assess(QStringLiteral("round-%1").arg(round + 1));
        // The normalized preserve-completion marker is only a request to close
        // after the current rendered review. A failed review stays actionable.
        auto model_review = last_review.value(QStringLiteral("modelReview")).toMap();
        model_review.remove(QStringLiteral("promptAudit"));
        last_review.insert(QStringLiteral("modelReview"), model_review);
        auto normalized_plan = plans.last().toMap();
        normalized_plan.insert(QStringLiteral("nativeCompletionReview"), last_review);
        normalized_plan.insert(QStringLiteral("nativeCompletionMarkerAccepted"), true);
        normalized_plan.insert(QStringLiteral("recoveryAction"),
            last_review.value(QStringLiteral("passed")).toBool()
                ? QStringLiteral("complete_after_passing_rendered_review")
                : QStringLiteral("request_corrected_tool_call_plan_after_rendered_review"));
        plans[plans.size() - 1] = normalized_plan;
        receipt.insert(QStringLiteral("plans"), plans);
        receipt.insert(QStringLiteral("nativeCompletionRenderedReviewCount"),
            receipt.value(QStringLiteral("nativeCompletionRenderedReviewCount")).toInt() + 1);
        receipt.insert(QStringLiteral("lastNativeCompletionReview"), last_review);
        CHECK(write_json(QStringLiteral("run-receipt.json"), receipt),
            "Record rendered review for verified native composition completion");
        if (last_review.value(QStringLiteral("passed")).toBool()) {
          complete = true;
          break;
        }
        request.require_tool_call_plan = true;
        continue;
      }
      if (plan.decision != "tool_call_plan") break;
      QVariantList batch_reports;
      for (const auto& value : plan.tool_call_plan) {
        const auto tool = value.toMap(); const auto name = tool.value("name").toString();
        CHECK(client.allowedTools().contains(name), "Only actual discovered tools");
        const auto result = call(name, tool.value("arguments").toMap());
        auto progress_report = result;
        progress_report.insert("toolName", name);
        progress_report.insert("arguments", tool.value("arguments"));
        batch_reports.append(progress_report);
        wait.restart();
        while (editor.photoPaintBusy() && wait.elapsed() < 60000) { QCoreApplication::processEvents(); QThread::msleep(5); }
        CHECK(!editor.photoPaintBusy(), "Settle native pixels");
        if (!result.value("ok").toBool()) break; // dependent calls are withheld
      }
      const auto repair_round_label = QStringLiteral("round-%1").arg(round + 1);
      CHECK(proof(repair_round_label), "Fresh model proof after tools");
      last_review = assess(repair_round_label);
      auto current_model_review = last_review.value(QStringLiteral("modelReview")).toMap();
      current_model_review.remove(QStringLiteral("promptAudit"));
      last_review.insert(QStringLiteral("modelReview"), current_model_review);
      auto completed_plan = plans.last().toMap();
      completed_plan.insert(QStringLiteral("currentStateReview"), last_review);
      completed_plan.insert(QStringLiteral("recoveryAction"),
          last_review.value(QStringLiteral("passed")).toBool()
              ? QStringLiteral("request_model_completion_after_passing_fresh_review")
              : QStringLiteral("request_corrected_tool_call_plan_from_fresh_review"));
      plans[plans.size() - 1] = completed_plan;
      receipt.insert(QStringLiteral("plans"), plans);
      receipt.insert(QStringLiteral("lastRenderedReview"), last_review);
      receipt.insert(QStringLiteral("toolBatchRenderedReviewCount"),
          receipt.value(QStringLiteral("toolBatchRenderedReviewCount")).toInt() + 1);
      CHECK(write_json(QStringLiteral("run-receipt.json"), receipt),
          "Record fresh rendered review after the model tool batch");
      request.require_tool_call_plan = !last_review.value(QStringLiteral("passed")).toBool();
      if (!progress.record(batch_reports)) {
        receipt.insert("stoppedForRepeatedToolResults", true);
        receipt.insert("progress", progress.snapshot());
        break;
      }
    }
    receipt.insert("modelDeclaredComplete", complete);
  }
  CHECK(proof("sample"), "Export actual sample pixels");
  const auto recipe = serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(editor));
  QSaveFile recipe_file(output.filePath("editable-recipe.json"));
  CHECK(recipe_file.open(QIODevice::WriteOnly) && recipe_file.write(recipe.toUtf8()) == recipe.toUtf8().size() && recipe_file.commit(), "Save editable recipe");
  const auto reopened = render_canonical(parse_edit_recipe_json(recipe), QStringLiteral("recipeReopen"));
  const QImage exported(output.filePath("sample.png"));
  CHECK(!reopened.isNull() && reopened.size() == exported.size(), "Reopened output dimensions match");
  for (int y = 0; y < reopened.height(); ++y) for (int x = 0; x < reopened.width(); ++x)
    CHECK(reopened.pixel(x, y) == exported.pixel(x, y), "Exact serialized recipe pixel reopen");
  const auto inspection = call("photo.inspect_composition", {{"assetId", "design-sample"}});
  QVariantList text_layers; QStringList copy;
  const QImage final(output.filePath("sample.png"));
  bool within_canvas = true;
  for (const auto& value : inspection.value("structuredContent").toMap().value("layers").toList()) {
    const auto layer = value.toMap(); if (layer.value("kind") != "text") continue;
    text_layers.append(layer); copy.append(layer.value("text").toString());
    const QRect bounds(layer.value("x").toInt(), layer.value("y").toInt(), layer.value("pixelWidth").toInt(), layer.value("pixelHeight").toInt());
    within_canvas = within_canvas && final.rect().contains(bounds);
  }
  const bool copy_ok = copy.size() == 3 && copy.contains("SAMPLE / FIELD NOTES 01") && copy.contains("AFTER THE LIGHT") && copy.contains("A STUDY IN BLUE HOUR");
  receipt.insert("copyAndLayerCountPassed", copy_ok); receipt.insert("textBoundsPassed", within_canvas);
  receipt.insert("recipeReopenPixelIdentical", true);
  receipt.insert("layers", text_layers); receipt.insert("sampleSha256", hash_file(output.filePath("sample.png")));
  const auto final_review = assess("final");
  receipt.insert("finalReview", final_review);
  receipt.insert("visualReviewPassed", final_review.value("visualReviewPassed"));
  CHECK(editor.revertAgentTask(task_id).value("ok").toBool() && editor.currentAssetBoundAdjustmentPatchJson() == baseline, "Whole native task reverts exactly");
  receipt.insert("wholeTaskRevertPassed", true);
  const bool source_unchanged = source_sha == hash_file(source_path);
  receipt.insert("sourceUnchanged", source_unchanged);
  const bool passed = copy_ok && within_canvas && source_unchanged &&
      receipt.value("recipeReopenPixelIdentical").toBool() &&
      receipt.value("wholeTaskRevertPassed").toBool() &&
      final_review.value("passed").toBool() &&
      (reference || receipt.value("modelDeclaredComplete").toBool());
  receipt.insert("passed", passed); CHECK(write_json("run-receipt.json", receipt), "Retain truthful final receipt");
  bridge.stop(); return passed ? 0 : 1;
}
