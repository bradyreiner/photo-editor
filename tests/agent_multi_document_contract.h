#pragma once

#include "agent_timeline_commands.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QThread>

int run_agent_multi_document_contract(EditorPreviewController& photo, QTemporaryDir& temp, const QImage& source) {
  const auto wait_for_condition = [](auto&& condition, int timeout_ms) {
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
      if (timer.elapsed() >= timeout_ms) return false;
      QCoreApplication::processEvents();
      QThread::msleep(2);
    }
    return true;
  };
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
  const auto original_clip_id = timeline->clips().first().toMap().value("clipId").toString();
  const auto original_detail = timeline->inspectTimelineClipById(original_clip_id).value("clip").toMap();
  const auto track_id = original_detail.value("trackId").toString();
  const auto title = invoke("timeline", "createTimelineTitle", {
      {"trackId", track_id}, {"templateKey", "headline"}, {"startSeconds", 3.0},
      {"durationSeconds", 3.0}, {"values", QVariantMap{{"text", "Task title"}}}});
  const auto clip_id = content(title).value("createdClipId").toString();
  CHECK(title.value("ok").toBool() && !clip_id.isEmpty(), "Timeline creation returns the actual stable clip ID");
  const auto detail = client.callTool("app.inspect_command", {{"commandId", find("timeline", "timelineClipDetails")},
      {"values", QVariantMap{{"clipId", clip_id}}}});
  CHECK(detail.value("ok").toBool() && content(detail).value("result").toMap().value("clipId") == clip_id,
      "Read native clip details using a stable ID rather than a UI index");
  agent.setEditingFreedom("protect");
  const auto protected_state = timeline->agentTaskDocumentState();
  const QVariantMap graphic_values{{"trackId", track_id}, {"shapeType", "rectangle"},
      {"startSeconds", 6.0}, {"durationSeconds", 3.0}, {"values", QVariantMap{}}};
  CHECK(!invoke("timeline", "createTimelineGraphic", graphic_values).value("ok").toBool() &&
      timeline->agentTaskDocumentState() == protected_state, "Timeline ownership retains the active style's permission boundary");
  agent.setEditingFreedom("transform");
  timeline_scope_allowed = false;
  CHECK(!invoke("timeline", "createTimelineGraphic", graphic_values).value("ok").toBool() &&
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
  const auto overscan_graphic = invoke("timeline", "createTimelineGraphic", {
      {"trackId", track_id}, {"shapeType", "rectangle"}, {"startSeconds", 3.0},
      {"durationSeconds", 3.0},
      {"values", QVariantMap{{"shapeWidth", 4.0}, {"shapeHeight", 1.0185185},
                              {"positionX", 1.5}, {"positionY", -0.125}}}});
  const auto overscan_graphic_id = content(overscan_graphic)
      .value("createdClipId").toString();
  const auto overscan_graphic_detail = overscan_graphic_id.isEmpty()
      ? QVariantMap{}
      : timeline->inspectTimelineClipById(overscan_graphic_id)
            .value("clip").toMap();
  CHECK(overscan_graphic.value("ok").toBool() && !overscan_graphic_id.isEmpty() &&
      std::abs(overscan_graphic_detail.value("titleShapeWidth").toDouble() - 4.0) < 1.0e-6 &&
      std::abs(overscan_graphic_detail.value("titleShapeHeight").toDouble() - 1.0185185) < 1.0e-6 &&
      std::abs(overscan_graphic_detail.value("titlePositionX").toDouble() - 1.5) < 1.0e-6 &&
      std::abs(overscan_graphic_detail.value("titlePositionY").toDouble() + 0.125) < 1.0e-6,
      "Typed MCP graphic creation accepts and preserves shape and position overscan");
  CHECK(tasks.finish("review").value("ok").toBool(), "Persist completed multi-document task");
  CHECK(timeline->saveTimeline(), "Save the reviewed Timeline to its canonical native project path before recovery");
  const auto canonical_recovery_path = timeline->savedDocumentPath();
  CHECK(!canonical_recovery_path.isEmpty(), "Reviewed Timeline recovery uses the saved native project path");
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
  TimelineDocumentController cloned_timeline;
  CHECK(cloned_timeline.loadTimeline(recovery_path) &&
      cloned_timeline.agentTaskDocumentId() != QStringLiteral("timeline:") +
          QJsonDocument::fromJson(timeline_saved).object().value(QStringLiteral("timelineId")).toString(),
      "A noncanonical duplicate Timeline file still opens as an isolated copy");
  auto recovered_timeline = std::make_unique<TimelineDocumentController>();
  const auto recovered_load_ok = recovered_timeline->loadTimeline(canonical_recovery_path);
  const auto recovered_loaded_state = recovered_timeline->agentTaskDocumentState();
  if (!recovered_load_ok || recovered_loaded_state != timeline_saved) {
    const auto expected_object = QJsonDocument::fromJson(timeline_saved).object();
    const auto actual_object = QJsonDocument::fromJson(recovered_loaded_state).object();
    QSet<QString> keys;
    for (auto it = expected_object.begin(); it != expected_object.end(); ++it) keys.insert(it.key());
    for (auto it = actual_object.begin(); it != actual_object.end(); ++it) keys.insert(it.key());
    for (const auto& key : keys) {
      if (expected_object.value(key) != actual_object.value(key)) {
        std::cerr << "Timeline recovery state differs at " << key.toStdString() << ": expected="
                  << QJsonDocument(QJsonArray{expected_object.value(key)}).toJson(QJsonDocument::Compact).toStdString()
                  << " actual="
                  << QJsonDocument(QJsonArray{actual_object.value(key)}).toJson(QJsonDocument::Compact).toStdString()
                  << '\n';
      }
    }
    std::cerr << "Timeline recovery load returned " << recovered_load_ok << "\n";
    return 1;
  }
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
  register_agent_timeline_commands(cancel_registry, recovered_timeline.get(), recovered_tasks,
      [&]() { return task_id; }, authorize_timeline, {}, true);
  bridge.setAppCommandRegistry(&cancel_registry);
  CHECK(recovered_tasks.begin(temp.path(), task_id).value("ok").toBool() &&
      recovered_photo->beginAgentTask(task_id).value("ok").toBool() &&
      recovered_tasks.enroll("composition", recovered_photo->agentTaskResourceAdapter(task_id)).value("ok").toBool(), "Start mixed-task native cancellation");
  const auto recovered_track_id = recovered_timeline->clips().first().toMap().value("trackId").toString();
  const auto graphic = invoke("timeline", "createTimelineGraphic", {
      {"trackId", recovered_track_id}, {"shapeType", "ellipse"}, {"startSeconds", 3.0},
      {"durationSeconds", 3.0}, {"values", QVariantMap{}}});
  CHECK(graphic.value("ok").toBool(), "Enroll a Timeline mutation before pixel work");
  const auto graphic_clip_id = content(graphic).value("createdClipId").toString();
  const auto graphic_before_mask = recovered_timeline->inspectTimelineClipById(graphic_clip_id);
  if (graphic_clip_id.isEmpty() || !graphic_before_mask.value("ok").toBool() ||
      graphic_before_mask.value("clip").toMap().value("clipId").toString() != graphic_clip_id) {
    std::cerr << "Mask precondition diagnostic: graphicReceipt="
              << QJsonDocument::fromVariant(content(graphic)).toJson(QJsonDocument::Compact).toStdString()
              << " clipId=" << graphic_clip_id.toStdString()
              << " clipInspection="
              << QJsonDocument::fromVariant(graphic_before_mask).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return 1;
  }
  const auto masked_graphic = invoke("timeline", "setTimelineClipMask", {
      {"clipId", graphic_clip_id}, {"shape", "rectangle"},
      {"left", 0.1}, {"top", 0.1}, {"right", 0.9}, {"bottom", 0.9},
      {"cornerRadius", 0.04}});
  if (!masked_graphic.value("ok").toBool()) {
    std::cerr << "Mask invocation diagnostic: clipId=" << graphic_clip_id.toStdString()
              << " clipBefore="
              << QJsonDocument::fromVariant(graphic_before_mask).toJson(QJsonDocument::Compact).toStdString()
              << " values={shape:rectangle,left:0.1,top:0.1,right:0.9,bottom:0.9,cornerRadius:0.04}\n";
  }
  CHECK(masked_graphic.value("ok").toBool() &&
        content(masked_graphic).value("clip").toMap().value("clipMask").toMap()
            .value("enabled").toBool(),
      "The explicit Timeline command registry should authorize and expose sequence-space masks");
  const auto rejected_mask = invoke("timeline", "setTimelineClipMask", {
      {"clipId", graphic_clip_id}, {"shape", "rectangle"},
      {"left", 0.9}, {"top", 0.1}, {"right", 0.8}, {"bottom", 0.9},
      {"cornerRadius", 0.04}});
  CHECK(!rejected_mask.value("ok").toBool() &&
        recovered_timeline->inspectTimelineClipById(
            graphic_clip_id)
                .value("clip").toMap().value("clipMask").toMap()
                .value("left").toDouble() == 0.1,
      "Invalid sequence-space mask bounds should not mutate the admitted task target");
  const auto cleared_graphic_mask = invoke("timeline", "clearTimelineClipMask", {
      {"clipId", graphic_clip_id}});
  const auto cleared_graphic_detail =
      recovered_timeline->inspectTimelineClipById(graphic_clip_id).value("clip").toMap();
  CHECK(cleared_graphic_mask.value("ok").toBool() &&
        content(cleared_graphic_mask).value("changed").toBool() &&
        !cleared_graphic_detail.value("clipMask").toMap()
             .value("enabled").toBool(),
      "The admitted typed action should clear a sequence mask by stable clip ID");
  const auto repeated_graphic_clear = invoke("timeline", "clearTimelineClipMask", {
      {"clipId", graphic_clip_id}});
  CHECK(repeated_graphic_clear.value("ok").toBool() &&
        !content(repeated_graphic_clear).value("changed").toBool() &&
        content(repeated_graphic_clear).value("alreadySatisfied").toBool(),
      "Repeating an admitted mask clear should be idempotent");

  const QVariantList perspective_points{
      QVariantMap{{"x", 0.08}, {"y", -0.02}},
      QVariantMap{{"x", 0.94}, {"y", 0.04}},
      QVariantMap{{"x", 1.08}, {"y", 1.03}},
      QVariantMap{{"x", -0.03}, {"y", 0.96}},
  };
  const auto perspective_quad = invoke("timeline", "setTimelineClipPerspectiveQuad", {
      {"clipId", graphic_clip_id}, {"enabled", true}, {"points", perspective_points}});
  const auto perspective_detail = recovered_timeline->inspectTimelineClipById(graphic_clip_id)
                                     .value("clip").toMap();
  CHECK(perspective_quad.value("ok").toBool() &&
        content(perspective_quad).value("clip").toMap()
            .value("framePerspectiveQuad").toMap()
            .value("points").toList() == perspective_points &&
        perspective_detail.value("framePerspectiveQuad").toMap()
            .value("enabled").toBool(),
      "The admitted typed frame-quad action should persist and project all stable corners");
  const QVariantList invalid_perspective_points{
      perspective_points.at(0), perspective_points.at(2),
      perspective_points.at(1), perspective_points.at(3)};
  const auto rejected_quad = invoke("timeline", "setTimelineClipPerspectiveQuad", {
      {"clipId", graphic_clip_id}, {"enabled", true},
      {"points", invalid_perspective_points}});
  CHECK(!rejected_quad.value("ok").toBool() &&
        recovered_timeline->inspectTimelineClipById(graphic_clip_id)
            .value("clip").toMap() == perspective_detail,
      "Invalid typed frame-quad geometry must fail before mutating the admitted resource");
  const auto cleared_quad = invoke("timeline", "setTimelineClipPerspectiveQuad", {
      {"clipId", graphic_clip_id}, {"enabled", false}});
  CHECK(cleared_quad.value("ok").toBool() &&
        !recovered_timeline->inspectTimelineClipById(graphic_clip_id)
             .value("clip").toMap().value("framePerspectiveQuad").toMap()
             .value("enabled").toBool(),
      "The typed frame-quad action should clear only its static projective geometry");

  const auto batch_values = QVariantMap{
      {"batchId", QStringLiteral("a555a555-1111-4111-8111-555555555555")},
      {"operations", QVariantList{
          QVariantMap{{"id", "firstGraphic"},
              {"commandId", "timeline.action.createTimelineGraphic"},
              {"values", QVariantMap{{"trackId", recovered_track_id},
                  {"shapeType", "rectangle"}, {"startSeconds", 18.0},
                  {"durationSeconds", 1.0}, {"values", QVariantMap{}}}}},
          QVariantMap{{"id", "secondGraphic"},
              {"commandId", "timeline.action.createTimelineGraphic"},
              {"values", QVariantMap{{"trackId", recovered_track_id},
                  {"shapeType", "ellipse"}, {"startSeconds", 20.0},
                  {"durationSeconds", 1.0}, {"values", QVariantMap{}}}}},
      }}};
  const auto batched_create = invoke("timeline", "applyDraftBatch", batch_values);
  const auto batched_create_content = content(batched_create);
  QStringList batched_clip_ids;
  for (const auto& id : batched_create_content.value("createdClipIds").toList())
    batched_clip_ids.append(id.toString());
  const auto batched_state = recovered_timeline->agentTaskDocumentState();
  const auto batched_clip_count = recovered_timeline->clips().size();
  CHECK(batched_create.value("ok").toBool() && batched_create_content.value("ok").toBool() &&
      batched_clip_ids.size() == 2 &&
      batched_create_content.value("createdClipId").toString().isEmpty() &&
      batched_create_content.value("batchResults").toList().size() == 2 &&
      batched_create_content.value("batchResults").toList().at(0).toMap()
          .value("createdClipId").toString() == batched_clip_ids.at(0) &&
      batched_create_content.value("batchResults").toList().at(1).toMap()
          .value("createdClipId").toString() == batched_clip_ids.at(1),
      "A successful MCP batch should expose its ordered compact stable IDs at the existing top level");
  const auto batched_replay = invoke("timeline", "applyDraftBatch", batch_values);
  const auto batched_replay_content = content(batched_replay);
  QStringList replay_clip_ids;
  for (const auto& id : batched_replay_content.value("createdClipIds").toList())
    replay_clip_ids.append(id.toString());
  CHECK(batched_replay.value("ok").toBool() && batched_replay_content.value("ok").toBool() &&
      replay_clip_ids == batched_clip_ids &&
      recovered_timeline->agentTaskDocumentState() == batched_state &&
      recovered_timeline->clips().size() == batched_clip_count,
      "Idempotent replay should return the same ordered IDs without creating duplicate clips");

  const QVariantMap single_create_batch{
      {"batchId", QStringLiteral("c777c777-3333-4333-8333-777777777777")},
      {"operations", QVariantList{QVariantMap{{"id", "oneGraphic"},
          {"commandId", "timeline.action.createTimelineGraphic"},
          {"values", QVariantMap{{"trackId", recovered_track_id},
              {"shapeType", "rounded-rectangle"}, {"startSeconds", 22.0},
              {"durationSeconds", 1.0}, {"values", QVariantMap{}}}}}}}};
  const auto single_batched_create = invoke("timeline", "applyDraftBatch", single_create_batch);
  const auto single_batched_content = content(single_batched_create);
  const auto single_batch_ids = single_batched_content.value("createdClipIds").toList();
  CHECK(single_batched_create.value("ok").toBool() && single_batched_content.value("ok").toBool() &&
      single_batch_ids.size() == 1 &&
      single_batched_content.value("createdClipId").toString() ==
          single_batch_ids.first().toString() &&
      single_batched_content.value("batchResults").toList().size() == 1 &&
      single_batched_content.value("batchResults").toList().first().toMap()
          .value("createdClipId").toString() == single_batch_ids.first().toString(),
      "A one-creation batch should preserve both legacy singular and plural ID fields");

  const auto rollback_baseline = recovered_timeline->agentTaskDocumentState();
  const auto rollback_clip_count = recovered_timeline->clips().size();
  const QVariantMap rollback_batch{
      {"batchId", QStringLiteral("b666b666-2222-4222-8222-666666666666")},
      {"operations", QVariantList{
          QVariantMap{{"id", "temporaryGraphic"},
              {"commandId", "timeline.action.createTimelineGraphic"},
              {"values", QVariantMap{{"trackId", recovered_track_id},
                  {"shapeType", "rectangle"}, {"startSeconds", 24.0},
                  {"durationSeconds", 1.0}, {"values", QVariantMap{}}}}},
          QVariantMap{{"id", "missingClipFrame"},
              {"commandId", "timeline.action.setTimelineClipFrame"},
              {"values", QVariantMap{{"clipId", "missing-clip-id"},
                  {"offsetX", 0.0}, {"offsetY", 0.0}, {"scale", 1.0},
                  {"rotationDegrees", 0.0}, {"perspectiveX", 0.0},
                  {"perspectiveY", 0.0}, {"opacity", 1.0},
                  {"objectFit", "fit"}, {"width", 1.0}, {"height", 1.0},
                  {"cornerRadius", 0.0}, {"cropLeft", 0.0}, {"cropTop", 0.0},
                  {"cropRight", 1.0}, {"cropBottom", 1.0}}}},
      }}};
  const auto rolled_back_batch = invoke("timeline", "applyDraftBatch", rollback_batch);
  const auto rolled_back_content = content(rolled_back_batch);
  CHECK(!rolled_back_batch.value("ok").toBool() && !rolled_back_content.value("ok").toBool() &&
      rolled_back_content.value("rolledBack").toBool() &&
      !rolled_back_content.contains("createdClipIds") &&
      !rolled_back_content.contains("createdClipId") &&
      recovered_timeline->agentTaskDocumentState() == rollback_baseline &&
      recovered_timeline->clips().size() == rollback_clip_count,
      "A rolled-back MCP batch must hide provisional IDs and restore the exact Timeline state");
  const auto rolled_back_replay = invoke("timeline", "applyDraftBatch", rollback_batch);
  const auto rolled_back_replay_content = content(rolled_back_replay);
  CHECK(!rolled_back_replay.value("ok").toBool() && !rolled_back_replay_content.value("ok").toBool() &&
      !rolled_back_replay_content.contains("createdClipIds") &&
      !rolled_back_replay_content.contains("createdClipId") &&
      recovered_timeline->agentTaskDocumentState() == rollback_baseline &&
      recovered_timeline->clips().size() == rollback_clip_count,
      "Idempotent rollback replay must not surface provisional IDs or repeat the mutation");

  const QVariantMap vector_path{
      {"viewBoxWidth", 100.0}, {"viewBoxHeight", 100.0},
      {"commands", QVariantList{
          QVariantMap{{"verb", "M"}, {"points", QVariantList{10.0, 80.0}}},
          QVariantMap{{"verb", "C"}, {"points", QVariantList{20.0, 10.0, 80.0, 10.0, 90.0, 80.0}}},
          QVariantMap{{"verb", "L"}, {"points", QVariantList{10.0, 80.0}}},
          QVariantMap{{"verb", "Z"}, {"points", QVariantList{}}},
      }},
      {"stroke", "#FF336699"}, {"fill", "#00000000"}, {"strokeWidth", 4.0}};
  const auto vector_generator = invoke("timeline", "createTimelineGenerator", {
      {"trackId", recovered_track_id}, {"generatorKind", "vector-path"},
      {"startSeconds", 12.0}, {"durationSeconds", 1.0},
      {"parameters", QVariantMap{{"vectorPath", vector_path}}}});
  const auto vector_generator_clip_id = content(vector_generator).value("createdClipId").toString();
  const auto vector_generator_detail = recovered_timeline->inspectTimelineClipById(vector_generator_clip_id);
  const auto vector_generator_clip = vector_generator_detail.value("clip").toMap();
  if (!vector_generator.value("ok").toBool() ||
      vector_generator_clip_id.isEmpty() || !vector_generator_detail.value("ok").toBool() ||
      vector_generator_clip.value("generatorParameters").toMap()
          .value("vectorPath").toMap().value("commands").toList().size() != 4) {
    std::cerr << "Vector generator receipt diagnostic: receipt="
              << QJsonDocument::fromVariant(vector_generator).toJson(QJsonDocument::Compact).toStdString()
              << " structured="
              << QJsonDocument::fromVariant(content(vector_generator)).toJson(QJsonDocument::Compact).toStdString()
              << " stableClipId=" << vector_generator_clip_id.toStdString()
              << " directInspection="
              << QJsonDocument::fromVariant(vector_generator_detail).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
  }
  CHECK(vector_generator.value("ok").toBool() &&
        !vector_generator_clip_id.isEmpty() && vector_generator_detail.value("ok").toBool() &&
        vector_generator_clip.value("generatorParameters").toMap().value("vectorPath").toMap()
            .value("commands").toList().size() == 4,
      "The existing authorized generator action should create an editable validated vector path");
  auto invalid_vector_path = vector_path;
  invalid_vector_path.insert("stroke", "red");
  const auto rejected_vector_generator = invoke("timeline", "createTimelineGenerator", {
      {"trackId", recovered_track_id}, {"generatorKind", "vector-path"},
      {"startSeconds", 14.0}, {"durationSeconds", 1.0},
      {"parameters", QVariantMap{{"vectorPath", invalid_vector_path}}}});
  CHECK(!rejected_vector_generator.value("ok").toBool(),
      "Vector generator colors should use native QColor hex rather than CSS names");
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
  const auto rejection_baseline = recovered_timeline->agentTaskDocumentState();
  const auto rejection_step_count = recovered_tasks.state().value("steps").toList().size();
  const auto rejected_bin = invoke("timeline", "createMediaBin", {
      {"name", "Invalid child"}, {"parent_bin_id", "missing-parent-bin"}});
  const auto rejected_bin_content = content(rejected_bin);
  CHECK(!rejected_bin.value("ok").toBool() &&
      rejected_bin_content.value("recoverableError").toBool() &&
      rejected_bin_content.value("mutationNotApplied").toBool() &&
      recovered_timeline->agentTaskDocumentState() == rejection_baseline &&
      recovered_tasks.state().value("steps").toList().size() == rejection_step_count + 1,
      "An invalid parent bin is journaled as a rejected input without native dispatch");
  for (const auto& command : {QStringLiteral("createSmartCollection"), QStringLiteral("generateCaptionsFromTranscript")}) {
    const QVariantMap values = command == "createSmartCollection"
        ? QVariantMap{{"name", "Rejected empty collection"}, {"rules", QVariantMap{}}}
        : QVariantMap{{"transcript", ""}};
    const auto rejected = invoke("timeline", command, values);
    const auto receipt = content(rejected);
    CHECK(!rejected.value("ok").toBool() && receipt.value("recoverableError").toBool() &&
        receipt.value("mutationNotApplied").toBool() && recovered_timeline->agentTaskDocumentState() == timeline_before &&
        recovered_tasks.state().value("steps").toList().size() == rejection_step_count + 2 +
            (command == "generateCaptionsFromTranscript" ? 1 : 0),
        "Successful Qt dispatch cannot hide a rejected string/void native creator");
  }
  CHECK(recovered_tasks.finish("review").value("ok").toBool() && recovered_tasks.revert().value("ok").toBool(),
      "Retain and reverse the rejection audit without replaying either failed command");

  // This case starts with a genuinely cold source path. Admission pins only
  // its canonical path; the typed import creates an empty integrity identity,
  // and the Timeline's ordinary post-release hydration must settle it without
  // invalidating the reviewed task's current-state checkpoint.
  const auto canonical_test_root = QFileInfo(temp.path()).canonicalFilePath();
  CHECK(!canonical_test_root.isEmpty(), "Resolve the temporary test root to its canonical filesystem path");
  const auto cold_input_directory = QDir(canonical_test_root).filePath(QStringLiteral("cold-import-input"));
  const auto cold_output_directory = QDir(canonical_test_root).filePath(QStringLiteral("cold-import-output"));
  CHECK(QDir().mkpath(cold_input_directory) && QDir().mkpath(cold_output_directory),
      "Create isolated cold-import input and task-output directories");
  const auto cold_source_path = QDir(cold_input_directory).filePath(QStringLiteral("never-indexed-photo.png"));
  CHECK(source.save(cold_source_path, "PNG"), "Write a fresh exact PNG source for the cold-import case");
  QFile cold_source_file(cold_source_path);
  CHECK(cold_source_file.open(QIODevice::ReadOnly), "Read the fresh source bytes for the expected full SHA-256");
  const auto cold_source_bytes = cold_source_file.readAll();
  cold_source_file.close();
  const auto cold_source_sha256 = QString::fromLatin1(
      QCryptographicHash::hash(cold_source_bytes, QCryptographicHash::Sha256).toHex());
  const auto canonical_cold_source = QFileInfo(cold_source_path).canonicalFilePath();
  const auto canonical_cold_output = QFileInfo(cold_output_directory).canonicalFilePath();
  CHECK(!cold_source_bytes.isEmpty() && cold_source_sha256.size() == 64 &&
      canonical_cold_source == cold_source_path && canonical_cold_output == cold_output_directory,
      "The cold source and output directory have exact canonical identities");

  auto cold_timeline = std::make_unique<TimelineDocumentController>();
  cold_timeline->createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Cold Source Recovery" )},
      {QStringLiteral("width"), 480}, {QStringLiteral("height"), 320},
      {QStringLiteral("frameRate"), 30.0}});
  CHECK(cold_timeline->saveTimeline(), "Save the empty Timeline to its canonical native path before admission");
  const auto cold_canonical_document_path = cold_timeline->savedDocumentPath();
  const auto cold_baseline_state = cold_timeline->agentTaskDocumentState();
  CHECK(!cold_canonical_document_path.isEmpty() && !cold_baseline_state.isEmpty() &&
      cold_timeline->clips().isEmpty(), "Capture the exact empty canonical Timeline baseline");
  QString cold_track_id;
  for (const auto& track_value : cold_timeline->sequenceTracks()) {
    const auto track = track_value.toMap();
    if (track.value(QStringLiteral("trackKind")).toString() == QStringLiteral("video") &&
        !track.value(QStringLiteral("locked")).toBool()) {
      cold_track_id = track.value(QStringLiteral("trackId")).toString();
      break;
    }
  }
  CHECK(!cold_track_id.isEmpty(), "The canonical empty Timeline exposes an editable video track");

  const auto cold_preparation = cold_timeline->prepareAgentTaskDocumentForAutomation();
  CHECK(cold_preparation.value(QStringLiteral("ok")).toBool() &&
      cold_preparation.value(QStringLiteral("ready")).toBool(),
      "Prepare the saved empty Timeline before creating its cold-import task");
  AgentTaskCoordinator cold_tasks;
  const QString cold_task_id = QStringLiteral("native-cold-import-identity");
  CHECK(cold_tasks.begin(temp.path(), cold_task_id).value(QStringLiteral("ok")).toBool(),
      "Begin the isolated cold-import task journal");
  CHECK(cold_tasks.journal()->recordApprovedSnapshot({
      {QStringLiteral("contract"), QStringLiteral("agent-approved-session-v1")},
      {QStringLiteral("prompt"), QStringLiteral("Import and frame one admitted photo, save a task-owned project copy, then verify release and recovery." )},
      {QStringLiteral("targetIds"), QStringList{cold_timeline->agentTaskDocumentId()}},
      {QStringLiteral("actions"), QVariantList{QStringLiteral("import_media"), QStringLiteral("frame"), QStringLiteral("save_project")}},
      {QStringLiteral("taskScopedCapabilities"), QStringList{QStringLiteral("media"), QStringLiteral("framing"), QStringLiteral("delivery")}}})
          .value(QStringLiteral("ok")).toBool(), "Persist the approved task objective and exact operation plan");
  CHECK(cold_tasks.journal()->recordNativeTimelineScope(
      {cold_timeline->agentTaskDocumentId()}, {canonical_cold_source}, canonical_cold_output)
          .value(QStringLiteral("ok")).toBool(), "Admit only the exact cold PNG path and output directory");
  const QPointer<TimelineDocumentController> cold_owner(cold_timeline.get());
  const auto cold_timeline_resource = cold_timeline->agentTaskDocumentId();
  const AgentTaskJournal::ResourceAdapter cold_timeline_adapter{
      [cold_owner, cold_timeline_resource]() {
        return cold_owner && cold_owner->agentTaskDocumentId() == cold_timeline_resource
            ? cold_owner->agentTaskDocumentState() : QByteArray{};
      },
      [cold_owner, cold_timeline_resource](const QByteArray& bytes) {
        return cold_owner && cold_owner->agentTaskDocumentId() == cold_timeline_resource &&
            cold_owner->restoreAgentTaskDocument(bytes);
      }, {}};
  CHECK(cold_tasks.enroll(cold_timeline_resource, cold_timeline_adapter).value(QStringLiteral("ok")).toBool() &&
      cold_timeline->acceptAgentTaskDocumentPreparationForAutomation().value(QStringLiteral("ok")).toBool(),
      "Enroll the empty baseline and accept the exact Timeline owner");

  const auto cold_authorize_timeline = [&](const QString& name) -> QVariantMap {
    if (cold_tasks.journal()->taskId() != cold_task_id ||
        cold_tasks.state().value(QStringLiteral("status")).toString() != QStringLiteral("running") ||
        cold_tasks.journal()->nativeTimelineScope().value(QStringLiteral("targetIds")).toStringList() !=
            QStringList{cold_timeline_resource} || !cold_timeline || !cold_timeline->agentTaskDocumentReady()) {
      return {{QStringLiteral("ok"), false},
          {QStringLiteral("error"), QStringLiteral("The cold-import task does not own this ready Timeline.")},
          {QStringLiteral("blockedByScope"), true}};
    }
    const auto graphics = name.contains(QStringLiteral("Title")) || name.contains(QStringLiteral("Graphic")) ||
        name.contains(QStringLiteral("Caption"));
    return agent.authorizeNativeEditingCommand(graphics ? QStringLiteral("TextTimeline") : QStringLiteral("Timeline"), false);
  };
  const auto cold_authorize_path = [&](const QString& operation, const QString& path) -> QVariantMap {
    const auto fail_path = [](const QString& message) {
      return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message},
          {QStringLiteral("blockedByScope"), true}};
    };
    const auto scope = cold_tasks.journal()->nativeTimelineScope();
    if (cold_tasks.journal()->taskId() != cold_task_id ||
        cold_tasks.state().value(QStringLiteral("status")).toString() != QStringLiteral("running") ||
        scope.value(QStringLiteral("targetIds")).toStringList() != QStringList{cold_timeline_resource} ||
        !cold_timeline || !cold_timeline->agentTaskDocumentReady())
      return fail_path(QStringLiteral("The cold-import path operation requires the admitted ready Timeline."));
    const auto style = agent.authorizeNativeEditingCommand(QStringLiteral("Timeline"), false);
    if (!style.value(QStringLiteral("ok")).toBool()) return style;
    if (operation == QStringLiteral("import_media")) {
      const QFileInfo input(path);
      const auto canonical = input.canonicalFilePath();
      if (!input.isFile() || !input.isReadable() || canonical != path ||
          !scope.value(QStringLiteral("assetPaths")).toStringList().contains(canonical))
        return fail_path(QStringLiteral("The cold import is limited to the exact canonical admitted source file."));
      return {{QStringLiteral("ok"), true}, {QStringLiteral("canonicalPath"), canonical}};
    }
    if (operation == QStringLiteral("save_project")) {
      const QFileInfo output(path);
      const auto parent = output.dir().canonicalPath();
      const auto canonical = parent.isEmpty() ? QString{} : QDir(parent).filePath(output.fileName());
      const auto relative = QDir(canonical_cold_output).relativeFilePath(canonical);
      const auto file_resource = AgentTaskCoordinator::taskOwnedFileResource(canonical);
      if (path != canonical || parent != canonical_cold_output || output.exists() ||
          relative.isEmpty() || relative == QStringLiteral(".") || relative == QStringLiteral("..") ||
          relative.startsWith(QStringLiteral("../")) || file_resource.resource_id.isEmpty())
        return fail_path(QStringLiteral("The cold task may create a new project only inside its admitted output directory."));
      return {{QStringLiteral("ok"), true}, {QStringLiteral("canonicalPath"), canonical},
          {QStringLiteral("resourceId"), file_resource.resource_id}};
    }
    return fail_path(QStringLiteral("This cold-import test does not admit the requested path operation."));
  };
  AgentAppCommandRegistry cold_registry;
  register_agent_timeline_commands(cold_registry, cold_timeline.get(), cold_tasks,
      [&]() { return cold_task_id; }, cold_authorize_timeline, cold_authorize_path);
  bridge.setAppCommandRegistry(&cold_registry);
  task_id = cold_task_id;
  CHECK(client.refreshToolCache(task_id).value(QStringLiteral("ok")).toBool(),
      "Refresh native MCP discovery for the cold-import task");
  const auto cold_import = invoke(QStringLiteral("timeline"), QStringLiteral("createTimelineClip"), {
      {QStringLiteral("trackId"), cold_track_id}, {QStringLiteral("assetId"), QStringLiteral("cold-import-photo")},
      {QStringLiteral("filePath"), canonical_cold_source}, {QStringLiteral("displayLabel"), QStringLiteral("Cold PNG")},
      {QStringLiteral("durationSeconds"), 1.0}, {QStringLiteral("trimStartSeconds"), 0.0},
      {QStringLiteral("trimEndSeconds"), 1.0}, {QStringLiteral("startSeconds"), 0.0}});
  const auto cold_clip_id = content(cold_import).value(QStringLiteral("createdClipId")).toString();
  CHECK(cold_import.value(QStringLiteral("ok")).toBool() && !cold_clip_id.isEmpty(),
      "Import the exact cold source through the typed native MCP action");
  const auto cold_frame = invoke(QStringLiteral("timeline"), QStringLiteral("setTimelineClipFrame"), {
      {QStringLiteral("clipId"), cold_clip_id}, {QStringLiteral("offsetX"), 0.05},
      {QStringLiteral("offsetY"), -0.025}, {QStringLiteral("scale"), 0.92},
      {QStringLiteral("rotationDegrees"), 0.0}, {QStringLiteral("perspectiveX"), 0.0},
      {QStringLiteral("perspectiveY"), 0.0}, {QStringLiteral("opacity"), 1.0},
      {QStringLiteral("objectFit"), QStringLiteral("fit")}, {QStringLiteral("width"), 1.0},
      {QStringLiteral("height"), 1.0}, {QStringLiteral("cornerRadius"), 0.0},
      {QStringLiteral("cropLeft"), 0.0}, {QStringLiteral("cropTop"), 0.0},
      {QStringLiteral("cropRight"), 0.0}, {QStringLiteral("cropBottom"), 0.0}});
  CHECK(cold_frame.value(QStringLiteral("ok")).toBool(),
      "Apply a real typed frame edit after the cold media import");
  const auto cold_owned_copy_path = QDir(canonical_cold_output).filePath(QStringLiteral("cold-import.json"));
  const auto cold_save = invoke(QStringLiteral("timeline"), QStringLiteral("saveTimeline"),
      {{QStringLiteral("outputPath"), cold_owned_copy_path}});
  CHECK(cold_save.value(QStringLiteral("ok")).toBool() && QFileInfo(cold_owned_copy_path).isFile(),
      "Save a task-owned native Timeline copy before review and release");
  const auto cold_clip_before_release = cold_timeline->inspectTimelineClipById(cold_clip_id)
      .value(QStringLiteral("clip")).toMap();
  const auto cold_state_before_release = cold_timeline->agentTaskDocumentState();
  CHECK(cold_tasks.journal()->matchesCheckpoint(cold_timeline_resource, cold_state_before_release),
      "The typed import, frame edit, and project save leave a settled journal checkpoint before release");
  CHECK(cold_clip_before_release.value(QStringLiteral("sourceContentSha256")).toString().isEmpty(),
      "The admitted cold source stays unhashed while the owned Timeline pauses derived workers");
  CHECK(cold_tasks.finish(QStringLiteral("review")).value(QStringLiteral("ok")).toBool() &&
      cold_tasks.releaseReviewedDraft().value(QStringLiteral("ok")).toBool(),
      "Review and release the settled cold-import task without granting commit or export");
  const auto deferred_cold_continue = cold_tasks.continueTask();
  CHECK(!deferred_cold_continue.value(QStringLiteral("ok")).toBool() &&
      deferred_cold_continue.value(QStringLiteral("retryable")).toBool() &&
      deferred_cold_continue.value(QStringLiteral("status")).toString() == QStringLiteral("source_integrity_pending") &&
      cold_tasks.state().value(QStringLiteral("status")).toString() == QStringLiteral("released"),
      "Continuing a released task waits for unresolved admitted source integrity without taking ownership");
  cold_timeline->releaseAgentTaskDocumentForAutomation();
  const auto released_source_hydrated = wait_for_condition([&]() {
      const auto clip = cold_timeline->inspectTimelineClipById(cold_clip_id).value(QStringLiteral("clip")).toMap();
      return clip.value(QStringLiteral("sourceFileSize")).toLongLong() == cold_source_bytes.size() &&
          clip.value(QStringLiteral("sourceContentSha256")).toString() == cold_source_sha256;
    }, 30000);
  if (!released_source_hydrated) {
    std::cerr << "Released cold-import worker diagnostic: expectedBytes=" << cold_source_bytes.size()
              << " expectedSha256=" << cold_source_sha256.toStdString()
              << " clip=" << QJsonDocument::fromVariant(cold_timeline->inspectTimelineClipById(cold_clip_id))
                     .toJson(QJsonDocument::Compact).toStdString() << "\n";
  }
  CHECK(released_source_hydrated,
      "Releasing the reviewed owner resumes the exact admitted source-integrity worker");
  CHECK(cold_timeline->saveTimeline() && cold_timeline->savedDocumentPath() == cold_canonical_document_path,
      "Save the released draft to the same canonical native Timeline path");
  cold_timeline.reset();

  auto reopened_cold_timeline = std::make_unique<TimelineDocumentController>();
  CHECK(reopened_cold_timeline->loadTimeline(cold_canonical_document_path) &&
      reopened_cold_timeline->savedDocumentPath() == cold_canonical_document_path,
      "Reopen the exact same canonical native project path after release");
  const auto cold_integrity_settled = wait_for_condition([&]() {
        const auto inspection = reopened_cold_timeline->inspectTimelineClipById(cold_clip_id);
        const auto clip = inspection.value(QStringLiteral("clip")).toMap();
        return inspection.value(QStringLiteral("ok")).toBool() &&
            clip.value(QStringLiteral("sourceFileSize")).toLongLong() == cold_source_bytes.size() &&
            clip.value(QStringLiteral("sourceContentSha256")).toString() == cold_source_sha256;
      }, 30000);
  if (!cold_integrity_settled) {
    const auto inspection = reopened_cold_timeline->inspectTimelineClipById(cold_clip_id);
    std::cerr << "Cold-import integrity diagnostic: expectedBytes=" << cold_source_bytes.size()
              << " expectedSha256=" << cold_source_sha256.toStdString()
              << " importedClipReceipt=" << QJsonDocument::fromVariant(content(cold_import)).toJson(QJsonDocument::Compact).toStdString()
              << " afterReleaseClip=" << QJsonDocument::fromVariant(inspection).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
  }
  CHECK(cold_integrity_settled,
      "Post-release derived integrity metadata settles to the exact cold source byte size and full SHA-256");
  CHECK(reopened_cold_timeline->saveTimeline() &&
      reopened_cold_timeline->savedDocumentPath() == cold_canonical_document_path,
      "Persist the settled source identity through the native save path");
  reopened_cold_timeline.reset();
  reopened_cold_timeline = std::make_unique<TimelineDocumentController>();
  CHECK(reopened_cold_timeline->loadTimeline(cold_canonical_document_path) &&
      reopened_cold_timeline->savedDocumentPath() == cold_canonical_document_path,
      "Reopen the same canonical path again after persisting the full source identity");
  const auto cold_reopened_identity_settled = wait_for_condition([&]() {
        const auto inspection = reopened_cold_timeline->inspectTimelineClipById(cold_clip_id);
        const auto clip = inspection.value(QStringLiteral("clip")).toMap();
        return inspection.value(QStringLiteral("ok")).toBool() &&
            clip.value(QStringLiteral("sourceFileSize")).toLongLong() == cold_source_bytes.size() &&
            clip.value(QStringLiteral("sourceContentSha256")).toString() == cold_source_sha256 &&
            clip.value(QStringLiteral("sourceIntegrityStatus")).toString() == QStringLiteral("verified");
      }, 30000);
  if (!cold_reopened_identity_settled) {
    std::cerr << "Cold-import persisted-identity diagnostic: "
              << QJsonDocument::fromVariant(reopened_cold_timeline->inspectTimelineClipById(cold_clip_id))
                     .toJson(QJsonDocument::Compact).toStdString() << '\n';
  }
  CHECK(cold_reopened_identity_settled,
      "The second native reopen revalidates the persisted path, byte size, and full SHA-256");
  const auto settled_cold_state = reopened_cold_timeline->agentTaskDocumentState();
  const auto settled_cold_clip = reopened_cold_timeline->inspectTimelineClipById(cold_clip_id).value(QStringLiteral("clip")).toMap();
  CHECK(settled_cold_clip.value(QStringLiteral("sourceFileSize")).toLongLong() == cold_source_bytes.size() &&
      settled_cold_clip.value(QStringLiteral("sourceContentSha256")).toString() == cold_source_sha256,
      "The saved reopened Timeline retains the full immutable source identity");
  const auto original_cold_checkpoint = cold_tasks.state().value(QStringLiteral("resources")).toList();
  QString journal_timeline_hash;
  for (const auto& resource_value : original_cold_checkpoint) {
    const auto resource = resource_value.toMap();
    if (resource.value(QStringLiteral("id")).toString() == cold_timeline_resource)
      journal_timeline_hash = resource.value(QStringLiteral("currentHash")).toString();
  }
  const auto settled_cold_hash = QString::fromLatin1(
      QCryptographicHash::hash(settled_cold_state, QCryptographicHash::Sha256).toHex());
  if (journal_timeline_hash != settled_cold_hash) {
    std::cerr << "Cold-import checkpoint diagnostic: journalCurrentHash=" << journal_timeline_hash.toStdString()
              << " settledNativeStateHash=" << settled_cold_hash.toStdString()
              << " preReleaseStateHash=" << QCryptographicHash::hash(cold_state_before_release, QCryptographicHash::Sha256).toHex().toStdString()
              << " importedSourceSize=" << cold_clip_before_release.value(QStringLiteral("sourceFileSize")).toLongLong()
              << " settledSourceSize=" << settled_cold_clip.value(QStringLiteral("sourceFileSize")).toLongLong()
              << " preReleaseSourceSha256=" << cold_clip_before_release.value(QStringLiteral("sourceContentSha256")).toString().toStdString()
              << " settledSourceSha256=" << settled_cold_clip.value(QStringLiteral("sourceContentSha256")).toString().toStdString()
              << " preReleaseIntegrityStatus=" << cold_clip_before_release.value(QStringLiteral("sourceIntegrityStatus")).toString().toStdString()
              << " settledIntegrityStatus=" << settled_cold_clip.value(QStringLiteral("sourceIntegrityStatus")).toString().toStdString()
              << "\n";
    QSet<QString> changed_clip_fields;
    for (auto it = cold_clip_before_release.cbegin(); it != cold_clip_before_release.cend(); ++it)
      changed_clip_fields.insert(it.key());
    for (auto it = settled_cold_clip.cbegin(); it != settled_cold_clip.cend(); ++it)
      changed_clip_fields.insert(it.key());
    for (const auto& key : changed_clip_fields) {
      if (cold_clip_before_release.value(key) == settled_cold_clip.value(key)) continue;
      std::cerr << "Cold-import clip field changed " << key.toStdString()
                << " before=" << QJsonDocument::fromVariant(QVariantMap{{QStringLiteral("value"), cold_clip_before_release.value(key)}})
                       .toJson(QJsonDocument::Compact).toStdString()
                << " after=" << QJsonDocument::fromVariant(QVariantMap{{QStringLiteral("value"), settled_cold_clip.value(key)}})
                       .toJson(QJsonDocument::Compact).toStdString() << '\n';
    }
  }

  const auto recovery_preparation = reopened_cold_timeline->prepareAgentTaskDocumentForAutomation();
  CHECK(recovery_preparation.value(QStringLiteral("ok")).toBool() &&
      recovery_preparation.value(QStringLiteral("ready")).toBool(),
      "Prepare the same canonical Timeline before reattaching the released journal");
  AgentTaskCoordinator recovered_cold_tasks;
  CHECK(recovered_cold_tasks.recover(temp.path(), cold_task_id).value(QStringLiteral("ok")).toBool(),
      "Recover the exact released cold-import journal from disk");
  const QPointer<TimelineDocumentController> reopened_cold_owner(reopened_cold_timeline.get());
  const auto reopened_cold_resource = reopened_cold_timeline->agentTaskDocumentId();
  const AgentTaskJournal::ResourceAdapter reopened_cold_adapter{
      [reopened_cold_owner, reopened_cold_resource]() {
        return reopened_cold_owner && reopened_cold_owner->agentTaskDocumentId() == reopened_cold_resource
            ? reopened_cold_owner->agentTaskDocumentState() : QByteArray{};
      },
      [reopened_cold_owner, reopened_cold_resource](const QByteArray& bytes) {
        return reopened_cold_owner && reopened_cold_owner->agentTaskDocumentId() == reopened_cold_resource &&
            reopened_cold_owner->restoreAgentTaskDocument(bytes);
      }, {}};
  CHECK(recovered_cold_tasks.enroll(reopened_cold_resource, reopened_cold_adapter).value(QStringLiteral("ok")).toBool(),
      "Reattach the released task to the exact stable Timeline resource");
  const auto cold_copy_resource = AgentTaskCoordinator::taskOwnedFileResource(cold_owned_copy_path);
  CHECK(!cold_copy_resource.resource_id.isEmpty() &&
      recovered_cold_tasks.enroll(cold_copy_resource.resource_id, cold_copy_resource.adapter)
          .value(QStringLiteral("ok")).toBool(), "Reattach the task-owned copy file adapter");
  CHECK(reopened_cold_timeline->acceptAgentTaskDocumentPreparationForAutomation().value(QStringLiteral("ok")).toBool(),
      "Finish reattachment of the exact Timeline owner before checking recovery");
  const auto released_journal_path = recovered_cold_tasks.state().value(QStringLiteral("journalPath")).toString();
  QFile released_journal_before_file(released_journal_path);
  CHECK(released_journal_before_file.open(QIODevice::ReadOnly),
      "Read exact released journal bytes before derived-integrity preflight");
  const auto released_journal_before_preflight = released_journal_before_file.readAll();
  released_journal_before_file.close();
  const auto cold_valid_dry_run = recovered_cold_tasks.revert(true);
  QFile released_journal_after_file(released_journal_path);
  CHECK(released_journal_after_file.open(QIODevice::ReadOnly),
      "Read exact released journal bytes after derived-integrity preflight");
  const auto released_journal_after_preflight = released_journal_after_file.readAll();
  released_journal_after_file.close();
  CHECK(cold_valid_dry_run.value(QStringLiteral("ok")).toBool() &&
      !cold_valid_dry_run.value(QStringLiteral("derivedIntegrityProposals")).toList().isEmpty() &&
      !released_journal_before_preflight.isEmpty() &&
      released_journal_after_preflight == released_journal_before_preflight,
      "Check-only accepts only verified source-integrity drift and leaves exact journal bytes unchanged");
  QFile changed_cold_source(canonical_cold_source);
  const auto tampered_cold_bytes = cold_source_bytes + QByteArrayLiteral("tamper");
  CHECK(changed_cold_source.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
      changed_cold_source.write(tampered_cold_bytes) == tampered_cold_bytes.size(),
      "Mutate the admitted file briefly to prove source-byte changes fail closed");
  changed_cold_source.close();
  const auto rejected_changed_source = recovered_cold_tasks.revert(true);
  CHECK(!rejected_changed_source.value(QStringLiteral("ok")).toBool() &&
      recovered_cold_tasks.journal()->checkpointHash(reopened_cold_resource) == journal_timeline_hash,
      "A changed admitted source file cannot settle its integrity metadata or move the task checkpoint");
  QSaveFile restore_cold_source(canonical_cold_source);
  CHECK(restore_cold_source.open(QIODevice::WriteOnly) &&
      restore_cold_source.write(cold_source_bytes) == cold_source_bytes.size() &&
      restore_cold_source.commit(), "Restore the exact original cold source bytes after the rejection probe");

  auto changed_path_document = QJsonDocument::fromJson(settled_cold_state).object();
  const auto unadmitted_source_path = QDir(canonical_cold_output).filePath(QStringLiteral("unadmitted-source.png"));
  auto path_references_changed = false;
  auto legacy_clips = changed_path_document.value(QStringLiteral("clips")).toArray();
  for (qsizetype index = 0; index < legacy_clips.size(); ++index) {
    auto clip = legacy_clips.at(index).toObject();
    if (clip.value(QStringLiteral("clipId")).toString() == cold_clip_id) {
      clip.insert(QStringLiteral("filePath"), unadmitted_source_path);
      legacy_clips.replace(index, clip);
      path_references_changed = true;
    }
  }
  changed_path_document.insert(QStringLiteral("clips"), legacy_clips);
  auto tracks = changed_path_document.value(QStringLiteral("tracks")).toArray();
  for (qsizetype track_index = 0; track_index < tracks.size(); ++track_index) {
    auto track = tracks.at(track_index).toObject();
    auto clips = track.value(QStringLiteral("clips")).toArray();
    for (qsizetype clip_index = 0; clip_index < clips.size(); ++clip_index) {
      auto clip = clips.at(clip_index).toObject();
      if (clip.value(QStringLiteral("clipId")).toString() == cold_clip_id) {
        clip.insert(QStringLiteral("filePath"), unadmitted_source_path);
        clips.replace(clip_index, clip);
        path_references_changed = true;
      }
    }
    track.insert(QStringLiteral("clips"), clips);
    tracks.replace(track_index, track);
  }
  changed_path_document.insert(QStringLiteral("tracks"), tracks);
  const auto changed_path_state = QJsonDocument(changed_path_document).toJson(QJsonDocument::Compact);
  CHECK(path_references_changed && reopened_cold_timeline->restoreAgentTaskDocument(changed_path_state) &&
      !recovered_cold_tasks.revert(true).value(QStringLiteral("ok")).toBool() &&
      recovered_cold_tasks.journal()->checkpointHash(reopened_cold_resource) == journal_timeline_hash,
      "A changed clip path outside the admitted source allowlist cannot settle the task checkpoint");
  CHECK(reopened_cold_timeline->restoreAgentTaskDocument(settled_cold_state) &&
      reopened_cold_timeline->agentTaskDocumentState() == settled_cold_state,
      "Restore the exact settled native document after the path-rejection probe");
  const auto original_cold_timeline_name = reopened_cold_timeline->inspectAgentDocument()
      .value(QStringLiteral("name")).toString();
  reopened_cold_timeline->setTimelineName(QStringLiteral("Manual post-release edit"));
  const auto rejected_manual_edit = recovered_cold_tasks.revert(true);
  reopened_cold_timeline->setTimelineName(original_cold_timeline_name);
  CHECK(!rejected_manual_edit.value(QStringLiteral("ok")).toBool() &&
      reopened_cold_timeline->agentTaskDocumentState() == settled_cold_state &&
      recovered_cold_tasks.journal()->checkpointHash(reopened_cold_resource) == journal_timeline_hash,
      "A real authored Timeline edit after release remains a conflict and does not advance the checkpoint");

  const auto cold_revert_preflight = recovered_cold_tasks.revert(true);
  if (!cold_revert_preflight.value(QStringLiteral("ok")).toBool()) {
    std::cerr << "Cold-import recovery preflight diagnostic: result="
              << QJsonDocument::fromVariant(cold_revert_preflight).toJson(QJsonDocument::Compact).toStdString()
              << " journalCurrentHash=" << journal_timeline_hash.toStdString()
              << " settledNativeStateHash=" << settled_cold_hash.toStdString()
              << " preReleaseStateHash=" << QCryptographicHash::hash(cold_state_before_release, QCryptographicHash::Sha256).toHex().toStdString()
              << " baselineHash=" << QCryptographicHash::hash(cold_baseline_state, QCryptographicHash::Sha256).toHex().toStdString()
              << " importedSourceBytes=" << cold_source_bytes.size()
              << " preReleaseSourceSize=" << cold_clip_before_release.value(QStringLiteral("sourceFileSize")).toLongLong()
              << " preReleaseIntegrityStatus=" << cold_clip_before_release.value(QStringLiteral("sourceIntegrityStatus")).toString().toStdString()
              << " expectedSourceSha256=" << cold_source_sha256.toStdString()
              << " actualSourceSha256=" << settled_cold_clip.value(QStringLiteral("sourceContentSha256")).toString().toStdString()
              << '\n';
  }
  const auto continued_released_cold_task = recovered_cold_tasks.continueTask();
  CHECK(continued_released_cold_task.value(QStringLiteral("ok")).toBool() &&
      recovered_cold_tasks.state().value(QStringLiteral("status")).toString() == QStringLiteral("running") &&
      recovered_cold_tasks.journal()->checkpointHash(reopened_cold_resource) == settled_cold_hash &&
      settled_cold_hash != journal_timeline_hash,
      "After derived hashing resumes, recovery adopts only the verified metadata and continues the same released task");
  const auto cold_revert = recovered_cold_tasks.revert();
  if (!cold_revert.value(QStringLiteral("ok")).toBool())
    std::cerr << "Cold-import recovery revert diagnostic: "
              << QJsonDocument::fromVariant(cold_revert).toJson(QJsonDocument::Compact).toStdString() << '\n';
  CHECK(cold_revert.value(QStringLiteral("ok")).toBool() &&
      reopened_cold_timeline->agentTaskDocumentState() == cold_baseline_state &&
      reopened_cold_timeline->clips().isEmpty() && !QFileInfo::exists(cold_owned_copy_path),
      "Recovered Revert restores the empty canonical baseline and removes only the owned project copy");
  QFile final_cold_source(canonical_cold_source);
  CHECK(final_cold_source.open(QIODevice::ReadOnly), "The exact admitted cold source remains available after Revert");
  const auto final_cold_bytes = final_cold_source.readAll();
  CHECK(final_cold_bytes == cold_source_bytes &&
      QCryptographicHash::hash(final_cold_bytes, QCryptographicHash::Sha256) ==
          QCryptographicHash::hash(cold_source_bytes, QCryptographicHash::Sha256),
      "Revert preserves the admitted source bytes and exact SHA-256 identity");
  reopened_cold_timeline->releaseAgentTaskDocumentForAutomation();

  std::cout << "Production MCP: mixed native documents, 27 initial plus two revision steps, stable IDs, conflict preservation, exact Revert, save/reopen and recreated-owner recovery passed.\n";
  return 0;
}
