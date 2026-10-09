// Opt-in real-model evidence. Never edits the user's catalog or active document.
#include "agent_edit_recipe_patch.h"
#include "edit_adjustment_patch.h"

int run_agent_thumbnail_live_proof() {
  const auto output_dir = qEnvironmentVariable("PHOTO_EDITOR_THUMBNAIL_PROOF_DIR");
  const auto original_path = qEnvironmentVariable("PHOTO_COMPOSITION_REVIEW_SOURCE");
  const auto app_data = qEnvironmentVariable("PHOTO_EDITOR_LIVE_COMPOSITION_APP_DATA_DIR");
  const auto model_id = qEnvironmentVariable("PHOTO_EDITOR_LIVE_COMPOSITION_MODEL_ID");
  CHECK(!output_dir.isEmpty() && !app_data.isEmpty() && !model_id.isEmpty(), "Set isolated thumbnail proof paths and model");
  CHECK(QDir().mkpath(output_dir) && QDir(output_dir).entryList(QDir::Files).isEmpty(), "Use a new evidence directory");
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", app_data.toUtf8());
  CHECK(AppBootstrap::ensureRuntimeDirectories(), "Initialize isolated runtime");
  QVariantMap receipt{{"modelId", model_id}, {"originalSource", original_path},
      {"productionBridge", true}, {"localOnly", true}, {"cloudUsed", false},
      {"hostPreparedCanvas", true}, {"nativeUiTested", false},
      {"catalogModified", false}, {"automaticCommitAllowed", false}};
  const auto save_json = [&](const QString& name, const QVariant& value) {
    QSaveFile file(QDir(output_dir).filePath(name));
    return file.open(QIODevice::WriteOnly) &&
        file.write(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Indented)) > 0 && file.commit();
  };
  const auto record = [&]() { return save_json("run-receipt.json", receipt); };
  const auto hash = [](const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString{};
    return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
  };
  const QImage original(original_path);
  CHECK(!original.isNull(), "Load real photograph");
  receipt.insert("originalSourceSha256", hash(original_path));
  // Preparing the canvas is test setup, explicitly distinguished from model editing.
  const auto crop_height = std::min(original.height(), original.width() * 9 / 16);
  const QImage source = original.copy(0, 0, original.width(), crop_height)
      .scaled(1280, 720, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  const auto source_path = QDir(output_dir).filePath("source.png");
  CHECK(source.save(source_path), "Save 16:9 test input without modifying original");
  const auto probe = probe_agent_local_model(model_id);
  receipt.insert("modelProbe", probe);
  CHECK(record(), "Record model preflight");
  CHECK(probe.value("available").toBool(), "Downloaded vision model must be available");
  QVariantMap style;
  for (const auto& entry : built_in_agent_edit_styles()) {
    if (entry.toMap().value("id").toString() == "natural-portrait") style = entry.toMap();
  }
  CHECK(!style.isEmpty(), "Use a production style rulebook");
  EditorPreviewImageProvider provider;
  EditorPreviewController controller;
  controller.setImageProvider(&provider);
  const auto resume_path = qEnvironmentVariable("PHOTO_EDITOR_THUMBNAIL_RESUME_RECIPE");
  auto initial_recipe = serialize_edit_recipe_json(EditRecipeState{});
  if (!resume_path.isEmpty()) {
    QFile resume_file(resume_path);
    CHECK(resume_file.open(QIODevice::ReadOnly), "Open preserved editable recipe for a model revision");
    initial_recipe = QString::fromUtf8(resume_file.readAll());
    receipt.insert("resumedRecipe", resume_path);
    receipt.insert("resumedRecipeSha256", hash(resume_path));
  }
  controller.loadAsset("composition", source_path, {}, {}, {}, {}, initial_recipe);
  QElapsedTimer timer;
  timer.start();
  while (controller.displayPreviewSource().isEmpty() && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(5);
  }
  CHECK(!controller.displayPreviewSource().isEmpty(), "Load real photo into production editor");
  AgentLocalModelPlanRequest request;
  request.model_id = model_id;
  request.style = style;
  request.asset = {{"assetId", "composition"}, {"filePath", source_path}};
  request.current_adjustment_patch_json = controller.currentAdjustmentPatchJson();
  request.force_model_planning = true;
  request.allow_deterministic_fallback = false;
  if (resume_path.isEmpty()) {
    request.user_instructions = "Inspect this concert photo. Apply GLOBAL color and tone changes across the ENTIRE image for a punchy "
        "realistic concert grade: slightly brighter midtones, stronger contrast, slightly cooler color to reduce the yellow cast, "
        "richer colors and moderate detail. Global color changes are explicitly allowed. Return globalChangePolicy=allow, "
        "localizedActions=[] and retouchActions=[]. Choose semantic brightness, contrast, palette, saturation and detail "
        "from the actual image. This round is only global color and tone. Do not crop or add composition layers.";
    std::cout << "MODEL: planning color grade" << std::endl;
    timer.restart();
    const auto color_plan = plan_agent_recipe_patch(request);
    receipt.insert("colorPlan", QVariantMap{{"ok", color_plan.ok}, {"decision", color_plan.decision},
        {"summary", color_plan.plan_summary}, {"error", color_plan.error_message},
        {"patch", color_plan.recipe_patch}, {"promptAudit", color_plan.prompt_audit}, {"elapsedMs", timer.elapsed()}});
    CHECK(record(), "Record real color plan");
    CHECK(color_plan.ok && !color_plan.recipe_patch.isEmpty(), "Model must provide a real color plan");
    auto color_state = EditorPreviewControllerTestAccess::state(controller);
    apply_agent_recipe_patch(&color_state, color_plan.recipe_patch);
    const auto color_patch = serialize_adjustment_patch_json(color_state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
    CHECK(controller.applyExternalAdjustmentPatchEdit("composition", request.current_adjustment_patch_json, color_patch),
        "Apply model color through production undoable adjustment path");
    const auto color_image = render_output_image_with_backend(source, color_state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    CHECK(color_image.save(QDir(output_dir).filePath("color-grade.png")), "Save graded photo proof");
  }
  const auto revision_proof = qEnvironmentVariable("PHOTO_EDITOR_THUMBNAIL_REVISION_PROOF");
  if (!revision_proof.isEmpty()) request.asset.insert("proofImagePath", revision_proof);
  AgentEditController agent;
  CatalogAssetModel catalog;
  PhotoAutomationBridge bridge(&agent, &catalog);
  bridge.setCompositionHandler([&](const QVariantMap& command) { return controller.photoCompositionCommand(command); });
  CHECK(bridge.start(true, {"read", "draft"}), "Start authenticated production MCP bridge");
  AgentMcpClient client;
  client.setEndpoint(bridge.mcpEndpoint());
  client.setBearerToken(bridge.token());
  client.setTimeoutMs(60000);
  client.setAllowedTools({"photo.inspect_composition", "photo.edit_composition"});
  const auto discovery = client.refreshToolCache("qwen-thumbnail-proof");
  CHECK(discovery.value("ok").toBool(), "Discover actual production tool schemas");
  for (const auto& value : discovery.value("tools").toList()) {
    if (client.allowedTools().contains(value.toMap().value("name").toString())) request.available_tool_schemas.append(value);
  }
  request.allow_tool_call_plan = true;
  const QString brief = "Create a striking YouTube music thumbnail using this 1280 by 720 concert photo. "
      "Use the exact editable title LIVE UP CLOSE in large bold high-contrast typography. Place the title across the upper "
      "part of the photograph so a meaningful portion passes BEHIND the main performer on the right. Keep the words "
      "readable at thumbnail size, with safe margins and the face unobstructed. Use real subject selection, text styling, "
      "and an independent subject cutout above the title; preserve the color grade and editable layers. "
      "The exact active assetId is composition. No existing masks or text layers have been supplied. "
      "Use select_subject to generate a real selection; inspect its returned masks to get maskId. "
      "Use add_text with only text,x,y, then typography with the returned text layer ID. "
      "Use copy_selection with independent=true and no layerId to copy the base photo above the text. "
      "Fonts such as Impact or Arial Black are appropriate; check installed fonts. "
      "Do not invent IDs or repeat successful operations. Read actual returned tool results before dependent calls.";
  receipt.insert("creativeBrief", brief);
  const auto guided = qEnvironmentVariableIsSet("PHOTO_EDITOR_THUMBNAIL_GUIDED");
  receipt.insert("guidedToolPhases", guided);
  const auto registered_schemas = request.available_tool_schemas;
  QVariantList plans, reports;
  for (int round = 0; round < 6; ++round) {
    const auto inspection = client.callTool("photo.inspect_composition", {{"assetId", "composition"}});
    CHECK(inspection.value("ok").toBool(), "Inspect actual composition state");
    request.prior_tool_results.append(QVariantMap{{"name", "photo.inspect_composition"},
        {"arguments", QVariantMap{{"assetId", "composition"}}}, {"result", inspection}});
    request.current_adjustment_patch_json = controller.currentAdjustmentPatchJson();
    request.user_instructions = brief + QStringLiteral("\nContinue from the successful tool results. This is round %1. "
        "Return only the necessary next ordered tool calls; wait for real IDs before dependent calls.").arg(round + 1);
    if (guided) {
      const auto content = inspection.value("structuredContent").toMap();
      const auto masks = content.value("masks").toList();
      QVariantMap text_layer;
      QString cutout_id;
      for (const auto& value : content.value("layers").toList()) {
        const auto layer = value.toMap();
        if (layer.value("kind").toString() == "text" && layer.value("text").toString() == "LIVE UP CLOSE") text_layer = layer;
        if (layer.value("kind").toString() == "pixel") cutout_id = layer.value("id").toString();
      }
      const auto styled = text_layer.value("textTypography").toMap().value("size").toDouble() >= 55;
      const auto revision = qEnvironmentVariable("PHOTO_EDITOR_THUMBNAIL_REVISION");
      const auto operation = !revision.isEmpty() ? QStringLiteral("typography") : masks.isEmpty() ? QStringLiteral("select_subject") : text_layer.isEmpty()
          ? QStringLiteral("add_text") : !styled ? QStringLiteral("typography") : cutout_id.isEmpty()
          ? QStringLiteral("copy_selection") : QStringLiteral("move");
      const auto layer_id = operation == "typography" ? text_layer.value("id").toString() : cutout_id;
      request.user_instructions += QStringLiteral("\nThe workflow has inspected the real editor. For this phase return exactly ONE "
          "photo.edit_composition call with assetId composition and operation %1. Use ONLY the typed fields in this phase's "
          "registered schema. Never use placeholder IDs. The title coordinates x and y are its LEFT/TOP edge, not its center. "
          "For add_text put LIVE UP CLOSE near the upper LEFT margin, x between 30 and 80 and y between 70 and 130. "
          "For typography select a bold installed font, numeric weight (700-900), and a size around 110-140 pixels; "
          "aim for a title spanning most of the canvas width so the final words partly cross behind the performer's head. "
          "Keep the right edge within 1240 pixels. White or warm white with a restrained dark stroke is appropriate. "
          "Do not create a second title. Current text state: %2.").arg(operation,
              QString::fromUtf8(QJsonDocument::fromVariant(text_layer).toJson(QJsonDocument::Compact)));
      if (!layer_id.isEmpty()) request.user_instructions += QStringLiteral("\nThe exact required arguments.layerId is %1.").arg(layer_id);
      if (!revision.isEmpty()) request.user_instructions += QStringLiteral("\nCurrent rendered proof is attached. Revision request: ") + revision;
      if (!masks.isEmpty()) request.user_instructions += QStringLiteral("\nThe exact selection-set values.maskId is %1. "
          "For copy_selection also use independent=true and omit layerId.").arg(masks.first().toMap().value("id").toString());
      for (const auto& value : registered_schemas) {
        auto tool = value.toMap();
        if (tool.value("name").toString() != "photo.edit_composition") continue;
        auto schema = tool.value("inputSchema").toMap();
        auto properties = schema.value("properties").toMap();
        auto operation_schema = properties.value("operation").toMap();
        operation_schema.insert("enum", QVariantList{operation});
        properties.insert("operation", operation_schema);
        auto values_schema = properties.value("values").toMap();
        const auto all_values = values_schema.value("properties").toMap();
        QVariantMap phase_values;
        const QStringList phase_keys = operation == "add_text" ? QStringList{"text", "x", "y"} :
            operation == "typography" ? QStringList{"family", "face", "size", "weight", "color", "tracking", "strokeWidth", "strokeColor", "shadowX", "shadowY", "shadowColor"} :
            operation == "copy_selection" ? QStringList{"maskId", "independent"} :
            operation == "move" ? QStringList{"position"} : QStringList{};
        for (const auto& key : phase_keys) phase_values.insert(key, all_values.value(key));
        if (operation == "copy_selection" && !masks.isEmpty()) {
          auto mask_schema = phase_values.value("maskId").toMap();
          mask_schema.insert("enum", QVariantList{masks.first().toMap().value("id")});
          phase_values.insert("maskId", mask_schema);
        }
        if (!layer_id.isEmpty()) {
          auto layer_schema = properties.value("layerId").toMap();
          layer_schema.insert("enum", QVariantList{layer_id});
          properties.insert("layerId", layer_schema);
        }
        values_schema.insert("properties", phase_values);
        properties.insert("values", values_schema);
        schema.insert("properties", properties);
        if (operation == "typography" || operation == "move") {
          auto required = schema.value("required").toList();
          required.append(QStringLiteral("layerId"));
          schema.insert("required", required);
        }
        tool.insert("inputSchema", schema);
        tool.insert("planningMaxCalls", 1);
        tool.insert("description", QStringLiteral("Execute the current undoable composition phase with exact returned IDs; typography weight is numeric."));
        request.available_tool_schemas = {tool};
      }
    }
    std::cout << "MODEL: composition round " << round + 1 << std::endl;
    timer.restart();
    const auto plan = plan_agent_recipe_patch(request);
    plans.append(QVariantMap{{"ok", plan.ok}, {"decision", plan.decision}, {"summary", plan.plan_summary},
        {"error", plan.error_message}, {"safeStopReason", plan.safe_stop_reason}, {"toolCalls", plan.tool_call_plan},
        {"promptAudit", plan.prompt_audit}, {"elapsedMs", timer.elapsed()}});
    receipt.insert("compositionPlans", plans);
    CHECK(record(), "Record model plan before applying calls");
    if (!plan.ok || plan.decision != "tool_call_plan") break;
    for (const auto& value : plan.tool_call_plan) {
      const auto call = value.toMap();
      const auto name = call.value("name").toString();
      const auto arguments = call.value("arguments").toMap();
      CHECK(client.allowedTools().contains(name) && arguments.value("assetId").toString() == "composition",
          "Model must use actual tool name and asset ID");
      std::cout << "TOOL: " << name.toStdString() << " " << arguments.value("operation").toString().toStdString() << std::endl;
      const auto result = client.callTool(name, arguments);
      const QVariantMap report{{"name", name}, {"arguments", arguments}, {"result", result}};
      reports.append(report);
      request.prior_tool_results.append(report);
      receipt.insert("toolReports", reports);
      CHECK(record(), "Record each actual tool outcome");
      timer.restart();
      while (controller.photoPaintBusy() && timer.elapsed() < 60000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
      }
      CHECK(!controller.photoPaintBusy(), "Wait for actual asynchronous cutout pixels");
    }
    // Terminate on actual editable title plus independent foreground cutout.
    auto saw_text = false, styled = false, cutout_above = false;
    int text_index = -1, cutout_index = -1;
    const auto layers = controller.photoLayers();
    for (qsizetype i = 0; i < layers.size(); ++i) {
      const auto layer = layers[i].toMap();
      if (layer.value("kind").toString() == "text" && layer.value("text").toString() == "LIVE UP CLOSE") {
        saw_text = true;
        styled = layer.value("textTypography").toMap().value("size").toDouble() >= 55;
        text_index = static_cast<int>(i);
      }
      // Independent cutouts can store their pixels in paintTiles rather than
      // the legacy pixelData field. This run creates pixel layers only by copy.
      if (layer.value("kind").toString() == "pixel")
        cutout_index = static_cast<int>(i);
    }
    cutout_above = cutout_index >= 0 && text_index > cutout_index;
    const auto state = EditorPreviewControllerTestAccess::state(controller);
    const auto preview = render_output_image_with_backend(source, state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
    CHECK(preview.save(QDir(output_dir).filePath(QStringLiteral("round-%1.png").arg(round + 1))), "Save rendered progress");
    if (saw_text && styled && cutout_above) break;
  }
  const auto state = EditorPreviewControllerTestAccess::state(controller);
  const auto render = [&](const EditRecipeState& s) {
    return render_output_image_with_backend(source, s, QColorSpace(QColorSpace::SRgb),
        QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
  };
  const auto final = render(state);
  const auto recipe = serialize_edit_recipe_json(state);
  const auto reopened = render(parse_edit_recipe_json(recipe));
  CHECK(!final.isNull() && final == reopened, "Saved recipe must reopen pixel identically");
  const auto final_path = QDir(output_dir).filePath("thumbnail.png");
  CHECK(final.save(final_path), "Save real final thumbnail");
  QSaveFile recipe_file(QDir(output_dir).filePath("editable-recipe.json"));
  CHECK(recipe_file.open(QIODevice::WriteOnly) && recipe_file.write(recipe.toUtf8()) == recipe.toUtf8().size() &&
      recipe_file.commit(), "Save editable recipe");
  const auto inspection = client.callTool("photo.inspect_composition", {{"assetId", "composition"}});
  const auto layers = inspection.value("structuredContent").toMap().value("layers").toList();
  int title_count = 0, title_position = -1, cutout_position = -1;
  bool styled_title = false;
  for (qsizetype i = 0; i < layers.size(); ++i) {
    const auto layer = layers[i].toMap();
    if (layer.value("kind").toString() == "text") {
      ++title_count;
      title_position = static_cast<int>(i);
      styled_title = layer.value("text").toString() == "LIVE UP CLOSE" &&
          layer.value("textTypography").toMap().value("size").toDouble() >= 55;
    }
    if (layer.value("kind").toString() == "pixel") cutout_position = static_cast<int>(i);
  }
  const auto correct_stack = title_count == 1 && styled_title && cutout_position >= 0 &&
      cutout_position < title_position;
  receipt.insert("singleStyledTitleBehindCutout", correct_stack);
  auto text_only_state = state;
  for (qsizetype i = text_only_state.layers.size(); i-- > 0;) {
    if (text_only_state.layers[i].toMap().value("kind").toString() == "pixel") text_only_state.layers.removeAt(i);
  }
  const auto text_only = render(text_only_state);
  auto base_state = state;
  base_state.layers.clear();
  const auto base = render(base_state);
  std::uint64_t text_pixels = 0, occluded_pixels = 0;
  for (int y = 0; y < final.height(); ++y) for (int x = 0; x < final.width(); ++x) {
    if (text_only.pixel(x, y) != base.pixel(x, y)) {
      ++text_pixels;
      if (final.pixel(x, y) != text_only.pixel(x, y)) ++occluded_pixels;
    }
  }
  CHECK(text_only.save(QDir(output_dir).filePath("text-before-cutout.png")), "Save actual occlusion comparison");
  receipt.insert("layersTopFirst", layers);
  receipt.insert("recipeReopenPixelIdentical", true);
  receipt.insert("textPixels", QVariant::fromValue<qulonglong>(text_pixels));
  receipt.insert("occludedTextPixels", QVariant::fromValue<qulonglong>(occluded_pixels));
  receipt.insert("finalSha256", hash(final_path));
  receipt.insert("recipeBytes", QFileInfo(recipe_file.fileName()).size());
  CHECK(record(), "Record rendered verification");
  AgentLocalModelProofCritiqueRequest critique;
  critique.model_id = model_id;
  critique.style = style;
  critique.asset = {{"assetId", "composition"}, {"filePath", source_path}, {"proofImagePath", final_path}};
  critique.user_instructions = brief;
  critique.graphic_layers = layers;
  critique.validation_report = {{"ok", true}, {"width", final.width()}, {"height", final.height()},
      {"occludedTextPixels", QVariant::fromValue<qulonglong>(occluded_pixels)}};
  std::cout << "MODEL: inspecting finished thumbnail" << std::endl;
  const auto review = critique_agent_rendered_proof(critique);
  receipt.insert("modelReview", QVariantMap{{"ok", review.ok}, {"pass", review.pass},
      {"needsUserReview", review.needs_user_review}, {"summary", review.summary}, {"issues", review.issues},
      {"repairHints", review.repair_hints}, {"error", review.error_message}, {"promptAudit", review.prompt_audit}});
  receipt.insert("originalUnchanged", hash(original_path) == receipt.value("originalSourceSha256").toString());
  CHECK(record(), "Record actual visual model review");
  bridge.stop();
  CHECK(correct_stack && text_pixels > 1000 && occluded_pixels > 100,
      "One styled title must visibly extend behind a topmost real cutout");
  std::cout << "thumbnail_proof=rendered occluded_text_pixels=" << occluded_pixels << std::endl;
  return EXIT_SUCCESS;
}
