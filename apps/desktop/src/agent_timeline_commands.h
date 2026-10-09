#pragma once

#include "agent_app_command_registry.h"
#include "agent_task_coordinator.h"
#include "timeline_document_controller.h"
#include <QSet>

namespace photo_editor::desktop {

inline void register_agent_timeline_commands(AgentAppCommandRegistry& registry,
    TimelineDocumentController* timeline, AgentTaskCoordinator& tasks,
    std::function<QString()> task_id, std::function<QVariantMap(const QString&)> authorize = {}) {
  const QPointer<TimelineDocumentController> owner(timeline);
  const auto resolve_clip = [owner](const QVariant& id) -> QVariant {
    if (!owner) return {};
    const auto clips = owner->clips();
    for (int index = 0; index < clips.size(); ++index)
      if (clips.at(index).toMap().value("clipId") == id) return index;
    return {};
  };
  registry.registerObject("timeline", timeline,
      [resolve_clip, authorize](const QString& name, bool property, bool is_const) {
    static const QSet<QString> reads{"inspectAgentDocument", "timelineClipDetails", "inspectClipColorForAutomation",
        "inspectClipEffectsForAutomation", "inspectClipColorGradeForAutomation", "inspectClipSourceColorMetadataForAutomation",
        "captionStyleTemplatePreview", "transitionPreviewStatesAt", "mediaAssetMetadata"};
    static const QSet<QString> methods{
        "addTitleClip", "addTitleClipFromTemplate", "addGraphicShape", "addAdjustmentClipForAutomation",
        "setDeliverySetting", "applyOutputPreset", "generateCaptionsFromTranscript",
        "createMediaBin", "renameMediaBin", "moveMediaBin", "deleteMediaBin", "assignMediaAssetToBin",
        "createSmartCollection", "updateSmartCollection", "deleteSmartCollection",
        "createReviewRange", "updateReviewRange", "deleteReviewRange",
        "setMediaAssetNotesAndCustomFields", "setMediaAssetRating", "setMediaAssetDisposition", "setMediaAssetColorLabel", "setMediaAssetTags",
        "setClipColorLutForAutomation", "clearClipColorLutForAutomation", "setClipVideoInputTransformForAutomation",
        "setClipShotMatchGainsForAutomation", "setClipColorGradeRecipeForAutomation", "editClipColorGradeLayerForAutomation",
        "addClipColorGradeAdjustmentLayerForAutomation", "setClipColorGradeParameterForAutomation", "setClipColorGradeCurveForAutomation",
        "clearClipColorGradeRecipeForAutomation", "editClipEffectStackForAutomation", "setClipOpacityForAutomation", "editClipKeyframeForAutomation",
        "moveSequenceMarker", "renameSequenceMarker", "setSequenceMarkerNote", "setSequenceMarkerStatus", "setSequenceMarkerCompleted",
        "updateSequenceMarkerReview"};
    static const QSet<QString> properties{"timelineName", "exportContainer", "exportCodec", "exportPreset",
        "exportEncoderMode", "exportTwoPass", "exportAlpha", "exportAudioStems", "exportImageSequence", "activeOutputVariant"};
    AgentAppCommandRegistry::Policy policy;
    policy.available = property ? properties.contains(name) : is_const ? reads.contains(name) : methods.contains(name);
    policy.resource = "timeline-document";
    policy.reversal = "exact-task-baseline";
    if (!is_const && authorize) policy.authorize = [authorize, name]() { return authorize(name); };
    policy.reason = "This Timeline control needs an explicit target, file owner, or asynchronous cancellation/completion adapter.";
    policy.arguments.insert("clip_index", {"clipId", {{"type", "string"}}, resolve_clip});
    return policy;
  }, [owner, &tasks, task_id = std::move(task_id)](const QString& command_id, const QVariantMap& values,
      const AgentAppCommandRegistry::Invocation& invoke) -> QVariantMap {
    const auto id = task_id ? task_id() : QString{};
    if (!owner || id.isEmpty() || tasks.journal()->taskId() != id || !owner->agentTaskDocumentReady())
      return {{"ok", false}, {"error", "Start an owned task and finish the Timeline's current native work before editing it."}};
    const auto resource = owner->agentTaskDocumentId();
    const auto enrolled = tasks.enroll(resource, {
        [owner, resource]() { return owner && owner->agentTaskDocumentId() == resource ? owner->agentTaskDocumentState() : QByteArray{}; },
        [owner, resource](const QByteArray& bytes) { return owner && owner->agentTaskDocumentId() == resource && owner->restoreAgentTaskDocument(bytes); },
        {}});
    if (!enrolled.value("ok").toBool()) return enrolled;
    QSet<QString> before_ids;
    for (const auto& item : owner->clips()) before_ids.insert(item.toMap().value("clipId").toString());
    return tasks.invoke(resource, command_id, values, [owner, invoke, before_ids, command_id]() {
      auto result = invoke();
      // These document creators return an empty string on native rejection.
      // Qt dispatch succeeding is not evidence that a resource was created.
      if (result.contains("result") && result.value("result").metaType().id() == QMetaType::QString &&
          result.value("result").toString().isEmpty()) {
        result.insert("ok", false);
        result.insert("error", "The Timeline rejected this resource creation; inspect its inputs before retrying.");
      }
      if (!owner) return result;
      QStringList created;
      for (const auto& item : owner->clips()) {
        const auto clip_id = item.toMap().value("clipId").toString();
        if (!before_ids.contains(clip_id)) created.append(clip_id);
      }
      if (!created.isEmpty()) {
        result.insert("createdClipIds", created);
        if (created.size() == 1) result.insert("createdClipId", created.first());
      } else if (command_id.contains(".method.addTitleClip(") ||
          command_id.contains(".method.generateCaptionsFromTranscript(")) {
        result.insert("ok", false);
        result.insert("error", "The native text command did not create any clips; inspect the document and inputs before retrying.");
      }
      return result;
    });
  });
}

}  // namespace photo_editor::desktop
