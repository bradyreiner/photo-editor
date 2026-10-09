#pragma once

#include "agent_timeline_commands.h"

int run_agent_multi_document_contract(EditorPreviewController& photo, QTemporaryDir& temp, const QImage& source) {
  auto timeline = std::make_unique<TimelineDocumentController>();
  timeline->createTimeline("Mixed poster and social video");
  CHECK(timeline->addTitleClipFromTemplate("basic") && timeline->saveTimeline(), "Save the isolated native Timeline baseline");
  const auto photo_before = photo.currentAssetBoundAdjustmentPatchJson();
  const auto timeline_before = timeline->agentTaskDocumentState();
  CHECK(!timeline_before.isEmpty(), "Timeline is ready for document ownership");
  const auto render_photo = [&](EditorPreviewController& document) {
    return render_output_image_with_backend(source, EditorPreviewControllerTestAccess::state(document),
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, RenderBackendPreference::CpuOnly);
  };
  const auto pixels_before = render_photo(photo);
  AgentTaskCoordinator tasks;
  photo.setAgentTaskJournal(tasks.journal());
  QString task_id = "native-mixed-documents";
  CHECK(tasks.begin(temp.path(), task_id).value("ok").toBool() &&
      photo.beginAgentTask(task_id).value("ok").toBool() &&
      tasks.enroll(photo.assetId(), photo.agentTaskResourceAdapter(task_id)).value("ok").toBool(),
      "Photo and native command owners share the same durable task");
  AgentAppCommandRegistry registry;
  AgentEditController agent;
  agent.setEditingFreedom("transform");
  bool timeline_scope_allowed = true;
  const auto authorize_timeline = [&](const QString& name) -> QVariantMap {
    if (!timeline_scope_allowed)
      return {{"ok", false}, {"error", "Timeline is outside the task targets."}, {"blockedByScope", true}};
    const auto graphics = name.contains("Title") || name.contains("Graphic") || name.contains("Caption");
    return agent.authorizeNativeEditingCommand(graphics ? "TextTimeline" : "Timeline", false);
  };
  register_agent_photo_commands(registry, &photo, [&]() { return task_id; },
      [&](const QString& name, bool property) { return agent.authorizeNativeEditingCommand(name, property); });
  register_agent_timeline_commands(registry, timeline.get(), tasks, [&]() { return task_id; }, authorize_timeline);
  CatalogAssetModel catalog;
  PhotoAutomationBridge bridge(&agent, &catalog);
  bridge.setEditAccessPolicy([]() { return true; });
  bridge.setAppCommandRegistry(&registry);
  AgentMcpClient client;
  client.setEndpoint(bridge.agentMcpEndpoint());
  client.setAllowedTools({"app.list_commands", "app.command_schema", "app.inspect_command", "app.invoke_command"});
  CHECK(client.refreshToolCache(task_id).value("ok").toBool(), "Discover production MCP controls across documents");
  const auto content = [](const QVariantMap& receipt) { return receipt.value("structuredContent").toMap(); };
  const auto find = [&](const QString& domain, const QString& name) {
    const auto result = client.callTool("app.list_commands", {{"domain", domain}, {"query", name}});
    for (const auto& row_value : content(result).value("commands").toList()) {
      const auto row = row_value.toMap();
      if (row.value("name") == name && row.value("available").toBool()) return row.value("commandId").toString();
    }
    return QString{};
  };
  const auto invoke = [&](const QString& domain, const QString& name, const QVariantMap& values = {}) {
    const auto id = find(domain, name);
    const auto receipt = client.callTool("app.invoke_command", {{"commandId", id}, {"values", values}});
    if (!receipt.value("ok").toBool()) std::cerr << QJsonDocument::fromVariant(receipt).toJson().toStdString();
    return receipt;
  };
  const auto title = invoke("timeline", "addTitleClipFromTemplate", {{"template_key", "headline"}});
  const auto clip_id = content(title).value("createdClipId").toString();
  CHECK(title.value("ok").toBool() && !clip_id.isEmpty(), "Timeline creation returns the actual stable clip ID");
  const auto detail = client.callTool("app.inspect_command", {{"commandId", find("timeline", "timelineClipDetails")},
      {"values", QVariantMap{{"clipId", clip_id}}}});
  CHECK(detail.value("ok").toBool() && content(detail).value("result").toMap().value("clipId") == clip_id,
      "Read native clip details using a stable ID rather than a UI index");
  agent.setEditingFreedom("protect");
  const auto protected_state = timeline->agentTaskDocumentState();
  CHECK(!invoke("timeline", "addGraphicShape", {{"shape_type", "rectangle"}}).value("ok").toBool() &&
      timeline->agentTaskDocumentState() == protected_state, "Timeline ownership retains the active style's permission boundary");
  agent.setEditingFreedom("transform");
  timeline_scope_allowed = false;
  CHECK(!invoke("timeline", "addGraphicShape", {{"shape_type", "rectangle"}}).value("ok").toBool() &&
      timeline->agentTaskDocumentState() == protected_state, "An open Timeline outside the task scope cannot receive mutations");
  timeline_scope_allowed = true;
  const auto fill = invoke("edit", "addFillLayer", {{"color", "#182b43"}, {"name", "Task background"}});
  CHECK(fill.value("ok").toBool() && !content(fill).value("createdLayerId").toString().isEmpty(), "Create actual photo design pixels");
  for (int i = 0; i < 12; ++i) {
    CHECK(invoke("edit", "exposure", {{"value", 0.1 + 0.02 * i}}).value("ok").toBool(), "Interleave owned photo color edits");
    CHECK(invoke("timeline", "setClipOpacityForAutomation", {{"clip_id", clip_id}, {"opacity", 0.6 + 0.02 * i}})
        .value("ok").toBool(), "Interleave native Timeline changes beyond old total-step limits");
  }
  CHECK(invoke("timeline", "timelineName", {{"value", "Agent mixed design"}}).value("ok").toBool(), "Own Timeline metadata too");
  CHECK(tasks.state().value("resources").toList().size() == 2 && tasks.state().value("steps").toList().size() == 27,
      "One journal preserves both baselines and all 27 interleaved receipts");
  CHECK(render_photo(photo) != pixels_before && timeline->agentTaskDocumentState() != timeline_before, "Both actual native documents changed");
  const auto owned_photo = photo.currentAssetBoundAdjustmentPatchJson();
  timeline->setTimelineName("Later manual Timeline edit");
  const auto manual_timeline = timeline->agentTaskDocumentState();
  CHECK(!tasks.finish("review").value("ok").toBool() && tasks.state().value("status") == "running",
      "Manual changes after a native receipt prevent false review completion");
  CHECK(!tasks.revert().value("ok").toBool() && photo.currentAssetBoundAdjustmentPatchJson() == owned_photo &&
      timeline->agentTaskDocumentState() == manual_timeline, "A manual Timeline conflict prevents partial photo reversal");
  timeline->setTimelineName("Agent mixed design");
  CHECK(tasks.finish("review").value("ok").toBool(), "Every native document is complete before review");
  CHECK(tasks.continueTask().value("ok").toBool(), "Continue the same native mixed task without restoring its first draft");
  CHECK(invoke("edit", "exposure", {{"value", 0.52}}).value("ok").toBool() &&
      invoke("timeline", "setClipOpacityForAutomation", {{"clip_id", clip_id}, {"opacity", 0.42}}).value("ok").toBool(),
      "Revision uses the existing native photo and stable Timeline clip ID");
  CHECK(tasks.state().value("steps").toList().size() == 29 &&
      tasks.finish("review").value("ok").toBool(), "Revision appends two receipts to the original task history");
  const auto reverted = tasks.revert();
  if (!reverted.value("ok").toBool()) std::cerr << QJsonDocument::fromVariant(reverted).toJson().toStdString();
  CHECK(reverted.value("ok").toBool() && photo.currentAssetBoundAdjustmentPatchJson() == photo_before &&
      timeline->agentTaskDocumentState() == timeline_before && render_photo(photo) == pixels_before,
      "Shared Revert restores exact photo recipe/pixels and Timeline document bytes");
  CHECK(timeline->saveTimeline(), "Save the restored Timeline through the native persistence path");
  TimelineDocumentController reopened;
  CHECK(reopened.loadTimeline(timeline->savedDocumentPath()) && reopened.agentTaskDocumentState() == timeline_before,
      "Save and reopen preserve the exact restored Timeline baseline");

  task_id = "native-mixed-recovery";
  CHECK(tasks.begin(temp.path(), task_id).value("ok").toBool() && photo.beginAgentTask(task_id).value("ok").toBool() &&
      tasks.enroll(photo.assetId(), photo.agentTaskResourceAdapter(task_id)).value("ok").toBool(), "Begin recovered multi-document task");
  CHECK(invoke("edit", "addPhotoShape", {{"type", "rectangle"}, {"start_x", 24}, {"start_y", 20},
      {"end_x", 456}, {"end_y", 90}, {"canvas_width", 480}, {"canvas_height", 320},
      {"fill_color", "#ffc945"}, {"stroke_color", "#fff1ca"}, {"stroke_width", 2.0}}).value("ok").toBool(), "Recover a real photo shape operation");
  CHECK(invoke("timeline", "addGraphicShape", {{"shape_type", "rectangle"}}).value("ok").toBool(), "Recover a real Timeline graphic operation");
  CHECK(tasks.finish("review").value("ok").toBool(), "Persist completed multi-document task");
  const auto photo_saved = photo.currentAssetBoundAdjustmentPatchJson();
  const auto timeline_saved = timeline->agentTaskDocumentState();
  const auto recovery_path = temp.filePath("owned-timeline-recovery.json");
  QFile recovery_file(recovery_path);
  CHECK(recovery_file.open(QIODevice::WriteOnly) && recovery_file.write(timeline_saved) == timeline_saved.size(), "Persist current recovery fixture document");
  recovery_file.close();
  timeline.reset();
  CHECK(!tasks.revert().value("ok").toBool() && photo.currentAssetBoundAdjustmentPatchJson() == photo_saved,
      "A closed Timeline owner cannot be skipped or leave the photo partially restored");
  AgentTaskCoordinator recovered_tasks;
  CHECK(recovered_tasks.recover(temp.path(), task_id).value("ok").toBool(), "Read the shared journal after owner recreation");
  EditorPreviewImageProvider recovery_provider;
  auto recovered_photo = std::make_unique<EditorPreviewController>();
  recovered_photo->setImageProvider(&recovery_provider);
  recovered_photo->setAgentTaskJournal(recovered_tasks.journal());
  recovered_photo->loadAsset("composition", temp.filePath("photo.png"), {}, {}, {}, {}, photo_saved);
  auto recovered_timeline = std::make_unique<TimelineDocumentController>();
  CHECK(recovered_timeline->loadTimeline(recovery_path) && recovered_timeline->agentTaskDocumentState() == timeline_saved,
      "Native loader preserves the owned Timeline checkpoint");
  CHECK(recovered_tasks.enroll("composition", recovered_photo->agentTaskResourceAdapter(task_id)).value("ok").toBool(), "Reattach photo without overwriting its baseline");
  const QPointer<TimelineDocumentController> owner(recovered_timeline.get());
  CHECK(recovered_tasks.enroll(owner->agentTaskDocumentId(), {
      [owner]() { return owner ? owner->agentTaskDocumentState() : QByteArray{}; },
      [owner](const QByteArray& value) { return owner && owner->restoreAgentTaskDocument(value); }, {}}).value("ok").toBool(),
      "Reattach the recovered Timeline native owner");
  CHECK(recovered_tasks.revert().value("ok").toBool() && recovered_photo->currentAssetBoundAdjustmentPatchJson() == photo_before &&
      recovered_timeline->agentTaskDocumentState() == timeline_before && render_photo(*recovered_photo) == pixels_before,
      "Recovered shared task restores exact photo pixels and Timeline state without replaying accepted calls");

  task_id = "native-mixed-cancellation";
  AgentAppCommandRegistry cancel_registry;
  register_agent_photo_commands(cancel_registry, recovered_photo.get(), [&]() { return task_id; });
  register_agent_timeline_commands(cancel_registry, recovered_timeline.get(), recovered_tasks, [&]() { return task_id; });
  bridge.setAppCommandRegistry(&cancel_registry);
  CHECK(recovered_tasks.begin(temp.path(), task_id).value("ok").toBool() &&
      recovered_photo->beginAgentTask(task_id).value("ok").toBool() &&
      recovered_tasks.enroll("composition", recovered_photo->agentTaskResourceAdapter(task_id)).value("ok").toBool(), "Start mixed-task native cancellation");
  CHECK(invoke("timeline", "addGraphicShape", {{"shape_type", "ellipse"}}).value("ok").toBool(), "Enroll a Timeline mutation before pixel work");
  const auto pixels = recovered_photo->runAgentTaskComposition(task_id,
      {{"assetId", "composition"}, {"operation", "add_pixel_layer"}, {"values", QVariantMap{}}});
  CHECK(pixels.value("ok").toBool(), "Create task-owned pixel surface in the shared journal");
  const QVariantList points{QVariantMap{{"x", 15}, {"y", 15}, {"pressure", 1.0}},
      QVariantMap{{"x", 35}, {"y", 35}, {"pressure", 1.0}}};
  const auto paint = recovered_photo->runAgentTaskComposition(task_id,
      {{"assetId", "composition"}, {"operation", "paint"}, {"layerId", pixels.value("createdLayerId")},
       {"values", QVariantMap{{"points", points}, {"color", "#ffff0000"}, {"size", 15.0}}}});
  CHECK(paint.value("ok").toBool() && recovered_photo->photoPaintBusy(), "Actual native paint remains pending after acceptance");
  CHECK(recovered_tasks.revert().value("ok").toBool(), "Shared reversal cancels owned pixel work and restores both documents");
  QElapsedTimer drain;
  drain.start();
  while (drain.elapsed() < 400) { QCoreApplication::processEvents(); QThread::msleep(5); }
  CHECK(!recovered_photo->photoPaintBusy() && recovered_photo->currentAssetBoundAdjustmentPatchJson() == photo_before &&
      recovered_timeline->agentTaskDocumentState() == timeline_before && render_photo(*recovered_photo) == pixels_before,
      "Late native pixel completion cannot alter either restored document");
  task_id = "native-timeline-rejections";
  CHECK(recovered_tasks.begin(temp.path(), task_id).value("ok").toBool(), "Begin native input-rejection checks");
  for (const auto& command : {QStringLiteral("createSmartCollection"), QStringLiteral("generateCaptionsFromTranscript")}) {
    const QVariantMap values = command == "createSmartCollection"
        ? QVariantMap{{"name", "Rejected empty collection"}, {"rules", QVariantMap{}}}
        : QVariantMap{{"transcript", ""}};
    const auto rejected = invoke("timeline", command, values);
    const auto receipt = content(rejected);
    CHECK(!rejected.value("ok").toBool() && receipt.value("recoverableError").toBool() &&
        receipt.value("mutationNotApplied").toBool() && recovered_timeline->agentTaskDocumentState() == timeline_before,
        "Successful Qt dispatch cannot hide a rejected string/void native creator");
  }
  CHECK(recovered_tasks.finish("review").value("ok").toBool() && recovered_tasks.revert().value("ok").toBool(),
      "Retain and reverse the rejection audit without replaying either failed command");
  std::cout << "Production MCP: mixed native documents, 27 initial plus two revision steps, stable IDs, conflict preservation, exact Revert, save/reopen and recreated-owner recovery passed.\n";
  return 0;
}
