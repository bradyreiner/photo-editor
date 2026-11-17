#pragma once

#include "agent_app_command_registry.h"
#include "agent_task_coordinator.h"
#include "agent_timeline_audio_state_commands.h"
#include "timeline_document_controller.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace photo_editor::desktop {

namespace agent_timeline_command_detail {

inline bool returns_created_clip_id_from_typed_action(const QString& command_id) {
  return command_id == QStringLiteral("timeline.action.createTimelineClip") ||
      command_id == QStringLiteral("timeline.action.createTimelineTitle") ||
      command_id == QStringLiteral("timeline.action.createTimelineGraphic") ||
      command_id == QStringLiteral("timeline.action.createTimelineGenerator");
}

inline bool requires_created_clip_id_discovery(const QString& command_id) {
  // These exact actions either cannot add clips or return the actual stable ID
  // from their typed native creator. Keep task-state capture/journaling intact
  // while avoiding complete rich clip-map projections around them. Every other
  // command retains the general before/after discovery behavior, including
  // unknown future commands and reflected creators without typed IDs.
  return command_id != QStringLiteral(
                          "timeline.method.setClipColorGradeParameterForAutomation(QString,QString,double)") &&
      command_id != QStringLiteral(
                        "timeline.method.editClipEffectStackForAutomation(QString,QString,QString,double,bool)") &&
      command_id != QStringLiteral("timeline.action.setTimelineClipRetime") &&
      !returns_created_clip_id_from_typed_action(command_id);
}

}  // namespace agent_timeline_command_detail

inline void register_agent_timeline_commands(
    AgentAppCommandRegistry& registry,
    TimelineDocumentController* timeline,
    AgentTaskCoordinator& tasks,
    std::function<QString()> task_id,
    std::function<QVariantMap(const QString&)> authorize = {},
    std::function<QVariantMap(const QString&, const QString&)> authorize_path = {},
    bool enable_draft_batch = false) {
  const QPointer<TimelineDocumentController> owner(timeline);
  const auto resolve_clip = [owner](const QVariant& id) -> QVariant {
    if (!owner) return {};
    const auto clips = owner->clips();
    for (int index = 0; index < clips.size(); ++index) {
      if (clips.at(index).toMap().value(QStringLiteral("clipId")) == id) return index;
    }
    return {};
  };
  const auto mutation_owner = [owner, &tasks, task_id](
      const QString& command_id,
      const QVariantMap& values,
      const AgentAppCommandRegistry::Invocation& invoke) -> QVariantMap {
    const auto id = task_id ? task_id() : QString{};
    if (!owner || id.isEmpty() || tasks.journal()->taskId() != id ||
        !owner->agentTaskDocumentReady()) {
      return {{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("Start an owned task and finish the Timeline's current native work before editing it.")}};
    }
    const auto resource = owner->agentTaskDocumentId();
    const auto enrolled = tasks.enroll(resource, {
        [owner, resource]() {
          return owner && owner->agentTaskDocumentId() == resource
                     ? owner->agentTaskDocumentState()
                     : QByteArray{};
        },
        [owner, resource](const QByteArray& bytes) {
          return owner && owner->agentTaskDocumentId() == resource &&
                 owner->restoreAgentTaskDocument(bytes);
        },
        {}});
    if (!enrolled.value(QStringLiteral("ok")).toBool()) return enrolled;
    if (command_id == QStringLiteral("timeline.action.applyDraftBatch")) {
      const auto request_bytes = QJsonDocument::fromVariant(values).toJson(QJsonDocument::Compact);
      const auto request_sha256 = QString::fromLatin1(
          QCryptographicHash::hash(request_bytes, QCryptographicHash::Sha256).toHex());
      auto result = tasks.invokeIdempotentBatchWithPreimage(resource, command_id,
          values.value(QStringLiteral("batchId")).toString(), request_sha256,
          values, [&invoke](const QByteArray& before) {
            return invoke.withPreimage(before);
          });
      if (result.value(QStringLiteral("ok")).toBool()) {
        QStringList created;
        QSet<QString> seen;
        for (const auto& row_value : result.value(QStringLiteral("batchResults")).toList()) {
          const auto clip_id = row_value.toMap().value(QStringLiteral("createdClipId")).toString();
          if (!clip_id.isEmpty() && !seen.contains(clip_id)) {
            seen.insert(clip_id);
            created.append(clip_id);
          }
        }
        if (!created.isEmpty()) {
          result.insert(QStringLiteral("createdClipIds"), created);
          if (created.size() == 1) result.insert(QStringLiteral("createdClipId"), created.first());
        }
      }
      return result;
    }
    if (!agent_timeline_command_detail::requires_created_clip_id_discovery(command_id)) {
      return tasks.invokeWithPreimage(resource, command_id, values,
          [&invoke, command_id](const QByteArray& task_preimage) {
            auto result = invoke.withPreimage(task_preimage);
            if (result.value(QStringLiteral("ok")).toBool() &&
                agent_timeline_command_detail::returns_created_clip_id_from_typed_action(command_id)) {
              const auto created_clip_id =
                  result.value(QStringLiteral("createdClipId")).toString();
              if (created_clip_id.isEmpty()) {
                result.insert(QStringLiteral("ok"), false);
                result.insert(QStringLiteral("error"),
                              QStringLiteral("The native Timeline creator succeeded without returning its stable clip ID; restore the owned task before continuing."));
                result.insert(QStringLiteral("recoveryRequired"), true);
              } else {
                result.insert(QStringLiteral("createdClipIds"), QStringList{created_clip_id});
              }
            }
            return result;
          });
    }
    QSet<QString> before_ids;
    for (const auto& item : owner->clips()) {
      before_ids.insert(item.toMap().value(QStringLiteral("clipId")).toString());
    }
    const auto invoke_with_created_ids = [owner, invoke, before_ids, command_id](
        const QByteArray& task_preimage) {
      auto result = invoke.withPreimage(task_preimage);
      if (!owner) return result;
      QStringList created;
      for (const auto& item : owner->clips()) {
        const auto clip_id = item.toMap().value(QStringLiteral("clipId")).toString();
        if (!clip_id.isEmpty() && !before_ids.contains(clip_id)) created.append(clip_id);
      }
      if (!created.isEmpty()) {
        result.insert(QStringLiteral("createdClipIds"), created);
        if (created.size() == 1) result.insert(QStringLiteral("createdClipId"), created.first());
      }
      return result;
    };
    return tasks.invokeWithPreimage(resource, command_id, values, invoke_with_created_ids);
  };

  register_agent_timeline_audio_state_commands(
      registry, owner, mutation_owner);

  const auto make_action_policy = [authorize](const QString& name) {
    AgentAppCommandRegistry::Policy policy;
    policy.available = true;
    policy.resource = QStringLiteral("timeline-document");
    policy.reversal = QStringLiteral("exact-task-baseline");
    policy.reason = QStringLiteral("An owned native Timeline task and approved target are required.");
    if (authorize) policy.authorize = [authorize, name]() { return authorize(name); };
    else policy.available = false;
    return policy;
  };
  const auto rejected_input = [](const QString& message) {
    return QVariantMap{{QStringLiteral("ok"), false},
                       {QStringLiteral("error"), message},
                       {QStringLiteral("errorCode"), QStringLiteral("command_admission_rejected")},
                       {QStringLiteral("recoverableError"), true},
                       {QStringLiteral("mutationNotApplied"), true}};
  };
  const auto string_schema = [](const QStringList& values = {},
                                const QString& description = {}) {
    QVariantMap schema{{QStringLiteral("type"), QStringLiteral("string")}};
    if (!values.isEmpty()) schema.insert(QStringLiteral("enum"), values);
    if (!description.isEmpty()) schema.insert(QStringLiteral("description"), description);
    return schema;
  };
  const auto number_schema = [](double minimum, double maximum,
                                const QString& description = {}) {
    QVariantMap schema{{QStringLiteral("type"), QStringLiteral("number")},
                       {QStringLiteral("minimum"), minimum},
                       {QStringLiteral("maximum"), maximum}};
    if (!description.isEmpty()) schema.insert(QStringLiteral("description"), description);
    return schema;
  };
  const auto object_schema = [](const QVariantMap& properties,
                                const QStringList& required = {}) {
    return QVariantMap{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required},
                       {QStringLiteral("additionalProperties"), false}};
  };
  const auto array_schema = [](const QVariantMap& items, int minimum, int maximum) {
    return QVariantMap{{QStringLiteral("type"), QStringLiteral("array")},
                       {QStringLiteral("items"), items},
                       {QStringLiteral("minItems"), minimum},
                       {QStringLiteral("maxItems"), maximum}};
  };
  const auto style_schema = object_schema({
      {QStringLiteral("text"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")},
                                           {QStringLiteral("maxLength"), 500}}},
      {QStringLiteral("fontFamily"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")},
                                                  {QStringLiteral("maxLength"), 120}}},
      {QStringLiteral("fontSize"), number_schema(1.0, 360.0)},
      {QStringLiteral("fontWeight"), QVariantMap{{QStringLiteral("type"), QStringLiteral("integer")},
                                                  {QStringLiteral("minimum"), 1},
                                                  {QStringLiteral("maximum"), 1000}}},
      {QStringLiteral("italic"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}}},
      {QStringLiteral("letterSpacing"), number_schema(-50.0, 100.0)},
      {QStringLiteral("lineHeight"), number_schema(0.5, 3.0)},
      {QStringLiteral("color"), string_schema({}, QStringLiteral("QColor hexadecimal syntax: #RRGGBB or #AARRGGBB, with alpha first. CSS #RRGGBBAA is not supported."))},
      {QStringLiteral("alignment"), string_schema({QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")})},
      {QStringLiteral("positionX"), number_schema(-4.0, 4.0, QStringLiteral("Fraction of sequence width. Text uses its left, center or right alignment anchor; shapes use their center. For a shape with left edge x and width w, use (x + w/2) / sequenceWidth."))},
      {QStringLiteral("positionY"), number_schema(-4.0, 4.0, QStringLiteral("Vertical center of the complete text block or shape, as a fraction of sequence height; not its top edge. For a block with top y and height h, use (y + h/2) / sequenceHeight."))},
      {QStringLiteral("backgroundColor"), string_schema({}, QStringLiteral("QColor fill: #RRGGBB or #AARRGGBB, with alpha first. Transparent example: #00000000."))},
      {QStringLiteral("shapeType"), string_schema({QStringLiteral("rectangle"), QStringLiteral("rounded-rectangle"), QStringLiteral("ellipse")}, QStringLiteral("Selects shape-only rendering; text is not drawn when shapeType is set. Use separate graphic and text clips for a labeled shape, or backgroundColor/backgroundPadding/backgroundRadius on a text-only title."))},
      {QStringLiteral("shapeWidth"), number_schema(0.000001, 4.0, QStringLiteral("Shape width as a fraction of sequence width, before the outer clip frame transform."))},
      {QStringLiteral("shapeHeight"), number_schema(0.000001, 4.0, QStringLiteral("Shape height as a fraction of sequence height, before the outer clip frame transform."))},
      {QStringLiteral("shapeCornerRadius"), number_schema(0.0, 0.5, QStringLiteral("Corner radius as a fraction of the smaller shape dimension; not pixels or a fraction of the sequence."))},
      {QStringLiteral("shapeStrokeColor"), string_schema()},
      {QStringLiteral("shapeStrokeWidth"), number_schema(0.0, 0.05)},
      {QStringLiteral("casing"), string_schema({QStringLiteral("none"), QStringLiteral("uppercase"), QStringLiteral("lowercase"), QStringLiteral("title-case")})},
      {QStringLiteral("strokeColor"), string_schema()},
      {QStringLiteral("strokeWidth"), number_schema(0.0, 12.0)},
      {QStringLiteral("shadowEnabled"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}}},
      {QStringLiteral("shadowColor"), string_schema()},
      {QStringLiteral("shadowOffsetX"), number_schema(-32.0, 32.0)},
      {QStringLiteral("shadowOffsetY"), number_schema(-32.0, 32.0)},
      {QStringLiteral("backgroundPadding"), number_schema(0.0, 128.0)},
      {QStringLiteral("backgroundRadius"), number_schema(0.0, 128.0)},
      {QStringLiteral("safeMarginX"), number_schema(0.0, 0.5)},
      {QStringLiteral("safeMarginY"), number_schema(0.0, 0.5)},
  });
  auto register_action = [&](const QString& name,
                             const QVariantMap& schema,
                             const AgentAppCommandRegistry::ActionHandler& handler,
                             std::function<QVariantMap(const QVariantMap&)> authorize_values = {}) {
    auto policy = make_action_policy(name);
    if (authorize_values) policy.authorize_values = std::move(authorize_values);
    return registry.registerAction(QStringLiteral("timeline"), name, timeline,
                                   schema, policy, mutation_owner, handler);
  };
  auto register_action_with_preimage = [&](const QString& name,
                             const QVariantMap& schema,
                             const AgentAppCommandRegistry::PreimageActionHandler& handler,
                             std::function<QVariantMap(const QVariantMap&)> authorize_values = {}) {
    auto policy = make_action_policy(name);
    if (authorize_values) policy.authorize_values = std::move(authorize_values);
    return registry.registerActionWithPreimage(
        QStringLiteral("timeline"), name, timeline, schema, policy,
        mutation_owner, handler);
  };

  registry.registerObject(
      QStringLiteral("timeline"), timeline,
      [resolve_clip, authorize, owner, rejected_input](const QString& name, bool property, bool is_const) {
    static const QSet<QString> reads{
        QStringLiteral("inspectAgentDocument"), QStringLiteral("inspectTimelineClipById"),
        QStringLiteral("timelineClipDetails"), QStringLiteral("timelineProjectHealth"),
        QStringLiteral("inspectClipColorForAutomation"),
        QStringLiteral("inspectClipEffectsForAutomation"), QStringLiteral("inspectClipColorGradeForAutomation"),
        QStringLiteral("inspectClipSourceColorMetadataForAutomation"),
        QStringLiteral("captionStyleTemplatePreview"), QStringLiteral("transitionPreviewStatesAt"),
        QStringLiteral("mediaAssetMetadata"), QStringLiteral("reviewNotes")};
    static const QSet<QString> methods{
        "addAdjustmentClipForAutomation", "setDeliverySetting", "applyOutputPreset",
        "generateCaptionsFromTranscript", "createMediaBin", "renameMediaBin", "moveMediaBin",
        "deleteMediaBin", "assignMediaAssetToBin", "createSmartCollection", "updateSmartCollection",
        "deleteSmartCollection", "createReviewRange", "updateReviewRange", "deleteReviewRange",
        "setMediaAssetNotesAndCustomFields", "setMediaAssetRating", "setMediaAssetDisposition",
        "setMediaAssetColorLabel", "setMediaAssetTags", "setClipColorLutForAutomation",
        "clearClipColorLutForAutomation", "setClipVideoInputTransformForAutomation",
        "setClipShotMatchGainsForAutomation", "setClipColorGradeRecipeForAutomation",
        "editClipColorGradeLayerForAutomation", "addClipColorGradeAdjustmentLayerForAutomation",
        "setClipColorGradeParameterForAutomation", "setClipColorGradeCurveForAutomation",
        "clearClipColorGradeRecipeForAutomation", "editClipEffectStackForAutomation",
        "setClipOpacityForAutomation", "editClipKeyframeForAutomation", "addSequenceMarker", "moveSequenceMarker",
        "renameSequenceMarker", "setSequenceMarkerNote", "setSequenceMarkerStatus",
        "setSequenceMarkerCompleted", "updateSequenceMarkerReview"};
    static const QSet<QString> properties{
        "timelineName", "exportContainer", "exportCodec", "exportPreset", "exportEncoderMode",
        "exportTwoPass", "exportAlpha", "exportAudioStems", "exportImageSequence", "activeOutputVariant"};
    AgentAppCommandRegistry::Policy policy;
    policy.available = property ? properties.contains(name)
                                : is_const ? reads.contains(name) : methods.contains(name);
    policy.resource = QStringLiteral("timeline-document");
    policy.reversal = QStringLiteral("exact-task-baseline");
    if (!is_const && authorize) policy.authorize = [authorize, name]() { return authorize(name); };
    if (!is_const && name == QStringLiteral("createSmartCollection")) {
      policy.authorize_values = [owner, rejected_input](const QVariantMap& values) {
        if (!owner || !owner->hasValidSmartCollectionRulesForAutomation(
                values.value(QStringLiteral("rules")).toMap())) {
          return rejected_input(QStringLiteral("Add at least one supported rule before saving a smart collection."));
        }
        return QVariantMap{{QStringLiteral("ok"), true}};
      };
    } else if (!is_const && name == QStringLiteral("createMediaBin")) {
      policy.authorize_values = [owner, rejected_input](const QVariantMap& values) {
        const auto parent_id = values.value(QStringLiteral("parent_bin_id")).toString().trimmed();
        if (parent_id.isEmpty()) return QVariantMap{{QStringLiteral("ok"), true}};
        if (owner) {
          for (const auto& bin_value : owner->mediaBins()) {
            if (bin_value.toMap().value(QStringLiteral("binId")).toString() == parent_id)
              return QVariantMap{{QStringLiteral("ok"), true}};
          }
        }
        return rejected_input(QStringLiteral("Choose an existing parent bin."));
      };
    } else if (!is_const && name == QStringLiteral("generateCaptionsFromTranscript")) {
      policy.authorize_values = [owner, rejected_input](const QVariantMap& values) {
        if (!owner || values.value(QStringLiteral("transcript")).toString().simplified().isEmpty()) {
          return rejected_input(QStringLiteral("Paste a transcript before generating captions."));
        }
        if (!owner->hasActiveTimeline() || owner->totalDurationSecondsValue() <= 0.0) {
          return rejected_input(QStringLiteral("Add media before generating captions."));
        }
        return QVariantMap{{QStringLiteral("ok"), true}};
      };
    }
    policy.reason = QStringLiteral("This Timeline control needs an explicit target, file owner, or asynchronous cancellation/completion adapter.");
    policy.arguments.insert(QStringLiteral("clip_index"),
        {QStringLiteral("clipId"), {{QStringLiteral("type"), QStringLiteral("string")}}, resolve_clip});
    return policy;
  }, mutation_owner);

  const auto clip_schema = object_schema({
      {QStringLiteral("trackId"), string_schema()},
      {QStringLiteral("assetId"), string_schema()},
      {QStringLiteral("filePath"), string_schema()},
      {QStringLiteral("displayLabel"), string_schema()},
      {QStringLiteral("durationSeconds"), number_schema(0.000001, 3600.0)},
      {QStringLiteral("trimStartSeconds"), number_schema(0.0, 21600.0)},
      {QStringLiteral("trimEndSeconds"), number_schema(0.000001, 21600.0)},
      {QStringLiteral("startSeconds"), number_schema(0.0, 21600.0)},
      {QStringLiteral("posterSource"), string_schema()}},
      {QStringLiteral("trackId"), QStringLiteral("assetId"), QStringLiteral("filePath"),
       QStringLiteral("displayLabel"), QStringLiteral("durationSeconds"),
       QStringLiteral("trimStartSeconds"), QStringLiteral("trimEndSeconds"),
       QStringLiteral("startSeconds")});
  register_action(QStringLiteral("createTimelineClip"), clip_schema,
      [owner](const QVariantMap& values) {
    return owner ? owner->createTimelineClipForAutomation(
        values.value(QStringLiteral("trackId")).toString(),
        values.value(QStringLiteral("assetId")).toString(),
        values.value(QStringLiteral("filePath")).toString(),
        values.value(QStringLiteral("displayLabel")).toString(),
        values.value(QStringLiteral("durationSeconds")).toDouble(),
        values.value(QStringLiteral("trimStartSeconds")).toDouble(),
        values.value(QStringLiteral("trimEndSeconds")).toDouble(),
        values.value(QStringLiteral("startSeconds")).toDouble(),
        values.value(QStringLiteral("posterSource")).toString())
                  : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before clip creation.")}};
  }, [authorize_path](const QVariantMap& values) {
    return authorize_path
        ? authorize_path(QStringLiteral("import_media"), values.value(QStringLiteral("filePath")).toString())
        : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("An admitted media path is required.")}};
  });

  const auto replace_source_schema = object_schema({
      {QStringLiteral("clipId"), string_schema()},
      {QStringLiteral("assetId"), string_schema()},
      {QStringLiteral("filePath"), string_schema()},
      {QStringLiteral("displayLabel"), string_schema()},
      {QStringLiteral("sourceInSeconds"), number_schema(0.0, 21600.0)},
      {QStringLiteral("sourceOutSeconds"), number_schema(0.000001, 21600.0)},
      {QStringLiteral("posterSource"), string_schema()},
      {QStringLiteral("fitToFill"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}}}},
      {QStringLiteral("clipId"), QStringLiteral("assetId"), QStringLiteral("filePath"),
       QStringLiteral("displayLabel")});
  const auto authorize_replace_source_path = [authorize_path](const QVariantMap& values) {
    auto result = authorize_path
        ? authorize_path(QStringLiteral("import_media"),
                         values.value(QStringLiteral("filePath")).toString())
        : QVariantMap{{QStringLiteral("ok"), false},
                      {QStringLiteral("error"), QStringLiteral("An admitted media path is required.")}};
    if (!result.value(QStringLiteral("ok")).toBool())
      result.insert(QStringLiteral("mutationNotApplied"), true);
    return result;
  };
  register_action(QStringLiteral("replaceTimelineClipSource"), replace_source_schema,
      [owner, authorize_path](const QVariantMap& values) {
    if (!owner) {
      return QVariantMap{{QStringLiteral("ok"), false},
                         {QStringLiteral("error"), QStringLiteral("Timeline closed before source replacement.")},
                         {QStringLiteral("mutationNotApplied"), true}};
    }
    const auto admitted = authorize_path
        ? authorize_path(QStringLiteral("import_media"),
                         values.value(QStringLiteral("filePath")).toString())
        : QVariantMap{{QStringLiteral("ok"), false},
                      {QStringLiteral("error"), QStringLiteral("An admitted media path is required.")}};
    if (!admitted.value(QStringLiteral("ok")).toBool()) {
      auto rejected = admitted;
      rejected.insert(QStringLiteral("mutationNotApplied"), true);
      return rejected;
    }
    const auto canonical_path = admitted.value(QStringLiteral("canonicalPath")).toString();
    if (canonical_path.isEmpty()) {
      return QVariantMap{{QStringLiteral("ok"), false},
                         {QStringLiteral("error"), QStringLiteral("The path admission did not return a canonical source path.")},
                         {QStringLiteral("mutationNotApplied"), true}};
    }
    return owner->replaceTimelineClipSourceForAutomation(
        values.value(QStringLiteral("clipId")).toString(),
        values.value(QStringLiteral("assetId")).toString(),
        canonical_path,
        values.value(QStringLiteral("displayLabel")).toString(),
        values.value(QStringLiteral("sourceInSeconds")).toDouble(),
        values.value(QStringLiteral("sourceOutSeconds")).toDouble(),
        values.value(QStringLiteral("posterSource")).toString(),
        values.value(QStringLiteral("fitToFill")).toBool());
  }, authorize_replace_source_path);

  register_action(QStringLiteral("createTimelineTrack"), object_schema({
      {QStringLiteral("kind"), string_schema({QStringLiteral("video"), QStringLiteral("graphics"), QStringLiteral("audio")})},
      {QStringLiteral("name"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 48}}},
      {QStringLiteral("audioRole"), string_schema({QString{}, QStringLiteral("dialogue"), QStringLiteral("voiceover"), QStringLiteral("music"), QStringLiteral("ambience"), QStringLiteral("effects"), QStringLiteral("commentary")})}},
      {QStringLiteral("kind")}),
      [owner](const QVariantMap& values) {
    return owner ? owner->createTimelineTrackForAutomation(
        values.value(QStringLiteral("kind")).toString(),
        values.value(QStringLiteral("name")).toString(),
        values.value(QStringLiteral("audioRole"), QStringLiteral("music")).toString())
                  : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before track creation.")}};
  }, [authorize](const QVariantMap& values) {
    if (values.value(QStringLiteral("kind")).toString() != QStringLiteral("graphics")) {
      return QVariantMap{{QStringLiteral("ok"), true}};
    }
    return authorize
        ? authorize(QStringLiteral("createTimelineGraphic"))
        : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Text Timeline authorization is required for a graphics track.")}};
  });

  auto graphics_track_move_schema = object_schema({
      {QStringLiteral("trackId"), string_schema()},
      {QStringLiteral("beforeTrackId"), string_schema()}},
      {QStringLiteral("trackId"), QStringLiteral("beforeTrackId")});
  graphics_track_move_schema.insert(
      QStringLiteral("description"),
      QStringLiteral("Reorder two unlocked top-level graphics tracks by stable ID. The source is inserted immediately below beforeTrackId in the low-to-high compositing stack; higher lane indices render above lower ones."));
  register_action(QStringLiteral("moveTimelineGraphicsTrack"), graphics_track_move_schema,
      [owner](const QVariantMap& values) {
    return owner ? owner->moveTimelineGraphicsTrackForAutomation(
        values.value(QStringLiteral("trackId")).toString(),
        values.value(QStringLiteral("beforeTrackId")).toString())
                 : QVariantMap{{QStringLiteral("ok"), false},
                               {QStringLiteral("error"), QStringLiteral("Timeline closed before graphics-track reorder.")}};
  }, [authorize](const QVariantMap&) {
    return authorize
        ? authorize(QStringLiteral("createTimelineGraphic"))
        : QVariantMap{{QStringLiteral("ok"), false},
                      {QStringLiteral("error"), QStringLiteral("Text Timeline authorization is required to reorder graphics tracks.")}};
  });

  auto graphics_track_compaction_schema = object_schema({
      {QStringLiteral("apply"),
       QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}}},
      {QStringLiteral("renameRetainedTracks"),
       QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}}}});
  graphics_track_compaction_schema.insert(
      QStringLiteral("description"),
      QStringLiteral("By default, return a read-only exact-order compaction plan. When apply is true, move eligible graphics clips onto the minimum ordered set of existing graphics tracks and remove only eligible donor tracks in one undoable native mutation. Clip IDs and clip-level timing, keyframes, masks, blends, effects, and media fields are preserved. Set renameRetainedTracks to true to explicitly rename retained rows Graphics 01, Graphics 02, and so on. The plan fails closed for audio-bearing, retimed, transitioning, grouped, hidden, animated, locked, or track-composited state."));
  register_action(QStringLiteral("compactTimelineGraphicsTracks"),
      graphics_track_compaction_schema,
      [owner](const QVariantMap& values) {
    return owner ? owner->compactTimelineGraphicsTracksForAutomation(
                       values.value(QStringLiteral("apply"), false).toBool(),
                       values.value(QStringLiteral("renameRetainedTracks"), false).toBool())
                 : QVariantMap{{QStringLiteral("ok"), false},
                               {QStringLiteral("mutationNotApplied"), true},
                               {QStringLiteral("error"), QStringLiteral("Timeline closed before graphics-track compaction.")}};
  }, [authorize](const QVariantMap&) {
    return authorize
        ? authorize(QStringLiteral("createTimelineGraphic"))
        : QVariantMap{{QStringLiteral("ok"), false},
                      {QStringLiteral("error"), QStringLiteral("Text Timeline authorization is required to compact graphics tracks.")}};
  });

  const auto title_schema = object_schema({
      {QStringLiteral("trackId"), string_schema()},
      {QStringLiteral("templateKey"), string_schema({QStringLiteral("basic"), QStringLiteral("headline"), QStringLiteral("lower-third"), QStringLiteral("callout"), QStringLiteral("quote"), QStringLiteral("brand-card")})},
      {QStringLiteral("startSeconds"), number_schema(0.0, 21600.0)},
      {QStringLiteral("durationSeconds"), number_schema(0.000001, 3600.0)},
      {QStringLiteral("values"), style_schema}},
      {QStringLiteral("trackId"), QStringLiteral("templateKey"), QStringLiteral("startSeconds"),
       QStringLiteral("durationSeconds"), QStringLiteral("values")});
  register_action(QStringLiteral("createTimelineTitle"), title_schema,
      [owner](const QVariantMap& values) {
    return owner ? owner->createTimelineTitleForAutomation(
        values.value(QStringLiteral("trackId")).toString(),
        values.value(QStringLiteral("templateKey")).toString(),
        values.value(QStringLiteral("startSeconds")).toDouble(),
        values.value(QStringLiteral("durationSeconds")).toDouble(),
        values.value(QStringLiteral("values")).toMap())
                  : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before title creation.")}};
  });

  register_action(QStringLiteral("createTimelineGraphic"), object_schema({
      {QStringLiteral("trackId"), string_schema()},
      {QStringLiteral("shapeType"), string_schema({QStringLiteral("rectangle"), QStringLiteral("rounded-rectangle"), QStringLiteral("ellipse")})},
      {QStringLiteral("startSeconds"), number_schema(0.0, 21600.0)},
      {QStringLiteral("durationSeconds"), number_schema(0.000001, 3600.0)},
      {QStringLiteral("values"), style_schema}},
      {QStringLiteral("trackId"), QStringLiteral("shapeType"), QStringLiteral("startSeconds"),
       QStringLiteral("durationSeconds"), QStringLiteral("values")}),
      [owner](const QVariantMap& values) {
    return owner ? owner->createTimelineGraphicForAutomation(
        values.value(QStringLiteral("trackId")).toString(),
        values.value(QStringLiteral("shapeType")).toString(),
        values.value(QStringLiteral("startSeconds")).toDouble(),
        values.value(QStringLiteral("durationSeconds")).toDouble(),
        values.value(QStringLiteral("values")).toMap())
                  : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before graphic creation.")}};
  });

  const auto native_color_schema = QVariantMap{
      {QStringLiteral("type"), QStringLiteral("string")},
      {QStringLiteral("description"),
       QStringLiteral("Use native QColor hexadecimal color syntax: #RRGGBB or #AARRGGBB. Gradient stop opacity is a separate 0 to 1 value.")}};
  const auto stop_schema = object_schema({
      {QStringLiteral("position"), number_schema(0.0, 1.0)},
      {QStringLiteral("color"), native_color_schema},
      {QStringLiteral("opacity"), number_schema(0.0, 1.0)}},
      {QStringLiteral("position"), QStringLiteral("color")});
  const auto vector_coordinate_schema = number_schema(-131072.0, 131072.0);
  const auto vector_command_schema = object_schema({
      {QStringLiteral("verb"), string_schema({QStringLiteral("M"), QStringLiteral("L"),
          QStringLiteral("C"), QStringLiteral("Q"), QStringLiteral("Z")})},
      {QStringLiteral("points"), array_schema(vector_coordinate_schema, 0, 6)}},
      {QStringLiteral("verb"), QStringLiteral("points")});
  const auto vector_frame_schema = object_schema({
      {QStringLiteral("seconds"), number_schema(0.0, 3600.0)},
      {QStringLiteral("coordinates"), array_schema(vector_coordinate_schema, 0, 24576)}},
      {QStringLiteral("seconds"), QStringLiteral("coordinates")});
  const auto rig_atom_schema = QVariantMap{{QStringLiteral("anyOf"), QVariantList{
      vector_coordinate_schema, QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}}}}};
  const auto rig_channel_schema = QVariantMap{{QStringLiteral("anyOf"), QVariantList{
      vector_coordinate_schema,
      array_schema(array_schema(rig_atom_schema, 2, 3), 1, 100000)}}};
  auto rig_node_properties = QVariantMap{
      {QStringLiteral("id"), string_schema()},
      {QStringLiteral("parent"), string_schema()},
      {QStringLiteral("kind"), string_schema({QStringLiteral("group"), QStringLiteral("path"),
          QStringLiteral("rect"), QStringLiteral("ellipse"), QStringLiteral("text")})},
      {QStringLiteral("rect"), array_schema(vector_coordinate_schema, 4, 4)},
      {QStringLiteral("commands"), array_schema(array_schema(rig_atom_schema, 1, 7), 1, 40000)},
      {QStringLiteral("pivot"), array_schema(vector_coordinate_schema, 2, 2)},
      {QStringLiteral("radius"), number_schema(0.0, 131072.0)},
      {QStringLiteral("fill"), native_color_schema},
      {QStringLiteral("stroke"), native_color_schema},
      {QStringLiteral("strokeWidth"), number_schema(0.0, 4096.0)},
      {QStringLiteral("text"), string_schema()},
      {QStringLiteral("font"), string_schema()},
      {QStringLiteral("fontSize"), number_schema(1.0, 2048.0)},
      {QStringLiteral("tracking"), number_schema(-100.0, 100.0)},
      {QStringLiteral("bold"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}}},
      {QStringLiteral("center"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}}},
      {QStringLiteral("blend"), string_schema({QStringLiteral("normal"), QStringLiteral("screen"), QStringLiteral("multiply")})},
      {QStringLiteral("gradient"), object_schema({
          {QStringLiteral("type"), string_schema({QStringLiteral("linear"), QStringLiteral("radial")})},
          {QStringLiteral("coordinates"), array_schema(vector_coordinate_schema, 3, 4)},
          {QStringLiteral("stops"), array_schema(array_schema(rig_atom_schema, 2, 2), 2, 16)}},
          {QStringLiteral("type"), QStringLiteral("coordinates"), QStringLiteral("stops")})}};
  for (const auto& name : {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("rotation"),
       QStringLiteral("scaleX"), QStringLiteral("scaleY"), QStringLiteral("opacity"), QStringLiteral("trim")})
    rig_node_properties.insert(name, rig_channel_schema);
  auto rig_scene_schema = object_schema({
      {QStringLiteral("schema"), string_schema({QStringLiteral("looksy-rig-scene-v1")})},
      {QStringLiteral("width"), number_schema(0.000001, 32768.0)},
      {QStringLiteral("height"), number_schema(0.000001, 32768.0)},
      {QStringLiteral("paperOpacity"), number_schema(0.0, 1.0)},
      {QStringLiteral("nodes"), array_schema(object_schema(rig_node_properties, {QStringLiteral("id")}), 1, 2048)}},
      {QStringLiteral("schema"), QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("nodes")});
  rig_scene_schema.insert(QStringLiteral("description"), QStringLiteral(
      "Native hierarchical character/vector scene. Parents must precede children. Channels are numbers or ordered "
      "[localSeconds,value,easing] arrays; easing is linear, hold, smooth or smoother. Transform order is "
      "translation, pivot, degree rotation, scale, inverse pivot. Opacity is inherited. Commands are "
      "[M,x,y], [L,x,y], [Q,cx,cy,x,y], [C,c1x,c1y,c2x,c2y,x,y], [Z]. "
      "Trim reveals an open stroke from 0 to 1. Gradients use native QColor hex stops. "
      "Use a root group for camera motion. Scene takes precedence over legacy commands."));
  auto vector_path_schema = object_schema({
      {QStringLiteral("viewBoxWidth"), number_schema(0.000001, 32768.0)},
      {QStringLiteral("viewBoxHeight"), number_schema(0.000001, 32768.0)},
      {QStringLiteral("commands"), array_schema(vector_command_schema, 1, 4096)},
      {QStringLiteral("frames"), array_schema(vector_frame_schema, 0, 9000)},
      {QStringLiteral("scene"), rig_scene_schema},
      {QStringLiteral("stroke"), native_color_schema},
      {QStringLiteral("fill"), native_color_schema},
      {QStringLiteral("strokeWidth"), number_schema(0.0, 4096.0)}},
      {QStringLiteral("viewBoxWidth"), QStringLiteral("viewBoxHeight"),
       QStringLiteral("commands")});
  vector_path_schema.insert(
      QStringLiteral("description"),
      QStringLiteral("Editable native vector path. Stroke and fill use #RRGGBB or #AARRGGBB QColor hex; frame times and coordinates are local to the generator."));
  const auto parameters_schema = object_schema({
      {QStringLiteral("timeOffsetSeconds"), number_schema(-3600.0, 3600.0)},
      {QStringLiteral("timeScale"), number_schema(0.0, 100.0)},
      {QStringLiteral("gather"), number_schema(0.0, 1.0)},
      {QStringLiteral("angleDegrees"), number_schema(-360.0, 360.0)},
      {QStringLiteral("stops"), array_schema(stop_schema, 2, 8)},
      {QStringLiteral("baseFrequencyX"), number_schema(0.0, 64.0)},
      {QStringLiteral("baseFrequencyY"), number_schema(0.0, 64.0)},
      {QStringLiteral("numOctaves"), number_schema(1.0, 16.0)},
      {QStringLiteral("seed"), number_schema(-9007199254740991.0, 9007199254740991.0)},
      {QStringLiteral("stitchTiles"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}}},
      {QStringLiteral("outputWidth"), QVariantMap{{QStringLiteral("type"), QStringLiteral("integer")},
                                                   {QStringLiteral("minimum"), 1},
                                                   {QStringLiteral("maximum"), 16384}}},
      {QStringLiteral("outputHeight"), QVariantMap{{QStringLiteral("type"), QStringLiteral("integer")},
                                                    {QStringLiteral("minimum"), 1},
                                                    {QStringLiteral("maximum"), 16384}}},
      {QStringLiteral("opacity"), number_schema(0.0, 1.0)},
      {QStringLiteral("vectorPath"), vector_path_schema}});
  register_action(QStringLiteral("createTimelineGenerator"), object_schema({
      {QStringLiteral("trackId"), string_schema()},
      {QStringLiteral("generatorKind"), string_schema({QStringLiteral("desktop-light"), QStringLiteral("transition-veil"), QStringLiteral("linear-gradient"), QStringLiteral("fractal-noise"), QStringLiteral("vector-path")})},
      {QStringLiteral("startSeconds"), number_schema(0.0, 21600.0)},
      {QStringLiteral("durationSeconds"), number_schema(0.000001, 3600.0)},
      {QStringLiteral("parameters"), parameters_schema}},
      {QStringLiteral("trackId"), QStringLiteral("generatorKind"), QStringLiteral("startSeconds"),
       QStringLiteral("durationSeconds")}),
      [owner](const QVariantMap& values) {
    return owner ? owner->createTimelineGeneratorForAutomation(
        values.value(QStringLiteral("trackId")).toString(),
        values.value(QStringLiteral("generatorKind")).toString(),
        values.value(QStringLiteral("startSeconds")).toDouble(),
        values.value(QStringLiteral("durationSeconds")).toDouble(),
        values.value(QStringLiteral("parameters")).toMap())
                  : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before generator creation.")}};
  });

  register_action(QStringLiteral("setTimelineClipTiming"), object_schema({
      {QStringLiteral("clipId"), string_schema()}, {QStringLiteral("trackId"), string_schema()},
      {QStringLiteral("startSeconds"), number_schema(0.0, 21600.0)},
      {QStringLiteral("durationSeconds"), number_schema(0.000001, 3600.0)}},
      {QStringLiteral("clipId"), QStringLiteral("trackId"), QStringLiteral("startSeconds"), QStringLiteral("durationSeconds")}),
      [owner](const QVariantMap& values) {
    return owner ? owner->setTimelineClipTimingForAutomation(
        values.value(QStringLiteral("clipId")).toString(),
        values.value(QStringLiteral("trackId")).toString(),
        values.value(QStringLiteral("startSeconds")).toDouble(),
        values.value(QStringLiteral("durationSeconds")).toDouble())
                  : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before timing update.")}};
  });

  const auto retime_point_schema = object_schema({
      {QStringLiteral("localSeconds"), number_schema(0.0, 3600.0)},
      {QStringLiteral("sourceSeconds"), number_schema(0.0, 21600.0)},
      {QStringLiteral("interpolation"),
       string_schema({QStringLiteral("linear"), QStringLiteral("hold")})}},
      {QStringLiteral("localSeconds"), QStringLiteral("sourceSeconds"),
       QStringLiteral("interpolation")});
  auto retime_schema = object_schema({
      {QStringLiteral("clipId"), string_schema()},
      {QStringLiteral("expectedSourceInSeconds"), number_schema(0.0, 21600.0)},
      {QStringLiteral("expectedSourceOutSeconds"), number_schema(0.0, 21600.0)},
      {QStringLiteral("expectedPlayDurationSeconds"), number_schema(0.000001, 3600.0)},
      {QStringLiteral("targetPlayDurationSeconds"), number_schema(0.000001, 3600.0)},
      {QStringLiteral("ripplePolicy"),
       string_schema({QStringLiteral("preserveOtherClipStarts"),
                      QStringLiteral("rippleAllTracksFromOldEnd")})},
      {QStringLiteral("points"), array_schema(retime_point_schema, 2, 512)}},
      {QStringLiteral("clipId"), QStringLiteral("expectedSourceInSeconds"),
       QStringLiteral("expectedSourceOutSeconds"),
       QStringLiteral("expectedPlayDurationSeconds"), QStringLiteral("points")});
  retime_schema.insert(
      QStringLiteral("description"),
      QStringLiteral("Replace the sourceTime curve on one stable video clip. Expected source marks and current effective play duration are stale-state guards. Points must cover the chosen local playwindow from 0 through its exact end, stay inside the marked source range, be ordered at least half a sequence frame apart, and use linear or hold interpolation. Optionally set targetPlayDurationSeconds to change the local retimed playwindow without changing source marks, media, or constant playback rate; when it differs from the current duration, specify ripplePolicy as preserveOtherClipStarts (reject same-lane overlap and keep every other clip start fixed) or rippleAllTracksFromOldEnd (shift all clips and markers at/after the old end)."));
  register_action(QStringLiteral("setTimelineClipRetime"), retime_schema,
      [owner](const QVariantMap& values) {
    return owner ? owner->setTimelineClipRetimeForAutomation(
        values.value(QStringLiteral("clipId")).toString(),
        values.value(QStringLiteral("expectedSourceInSeconds")).toDouble(),
        values.value(QStringLiteral("expectedSourceOutSeconds")).toDouble(),
        values.value(QStringLiteral("expectedPlayDurationSeconds")).toDouble(),
        values.value(QStringLiteral("points")).toList(),
        values.value(QStringLiteral("targetPlayDurationSeconds"),
                      values.value(QStringLiteral("expectedPlayDurationSeconds"))).toDouble(),
        values.value(QStringLiteral("ripplePolicy")).toString())
                  : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before retime update.")}};
  });

  register_action(QStringLiteral("setTimelineClipTitle"), object_schema({
      {QStringLiteral("clipId"), string_schema()}, {QStringLiteral("values"), style_schema}},
      {QStringLiteral("clipId"), QStringLiteral("values")}),
      [owner](const QVariantMap& values) {
    return owner ? owner->setTimelineClipTitleForAutomation(
        values.value(QStringLiteral("clipId")).toString(), values.value(QStringLiteral("values")).toMap())
                  : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before title update.")}};
  });

  auto frame_shadow_schema = object_schema({
      {QStringLiteral("enabled"), QVariantMap{
          {QStringLiteral("type"), QStringLiteral("boolean")}}},
      {QStringLiteral("color"), QVariantMap{
          {QStringLiteral("type"), QStringLiteral("string")},
          {QStringLiteral("description"),
           QStringLiteral("Use QColor hexadecimal syntax #RRGGBB or #AARRGGBB; the alpha channel controls shadow opacity.")}}},
      {QStringLiteral("offsetX"), number_schema(-512.0, 512.0)},
      {QStringLiteral("offsetY"), number_schema(-512.0, 512.0)},
      {QStringLiteral("blurRadius"), QVariantMap{
          {QStringLiteral("type"), QStringLiteral("number")},
          {QStringLiteral("minimum"), 0.0},
          {QStringLiteral("maximum"), 256.0},
          {QStringLiteral("description"),
           QStringLiteral("CSS-style blur radius in sequence-canvas pixels; export uses approximately half this value as Gaussian sigma.")}}},
      {QStringLiteral("spread"), number_schema(-64.0, 64.0)}},
      {QStringLiteral("enabled")});
  frame_shadow_schema.insert(
      QStringLiteral("description"),
      QStringLiteral("Optional shadow on the clip frame. Offsets, blur radius, and spread use sequence-canvas pixels; omitted or disabled defaults preserve no shadow."));
  register_action(QStringLiteral("setTimelineClipFrame"), object_schema({
      {QStringLiteral("clipId"), string_schema()},
      {QStringLiteral("offsetX"), number_schema(-4.0, 4.0, QStringLiteral("Horizontal displacement from canvas center in quarter-canvas units: +1 moves right by sequenceWidth/4. For an unrotated frame center x, use 4*(x/sequenceWidth - 0.5). Animated frameOffsetX keys use the same absolute units, not deltas."))},
      {QStringLiteral("offsetY"), number_schema(-4.0, 4.0, QStringLiteral("Vertical displacement from canvas center in quarter-canvas units: +1 moves down by sequenceHeight/4. For an unrotated frame center y, use 4*(y/sequenceHeight - 0.5). Animated frameOffsetY keys use the same absolute units, not deltas."))},
      {QStringLiteral("scale"), number_schema(0.000001, 4.0)}, {QStringLiteral("rotationDegrees"), number_schema(-180.0, 180.0)},
      {QStringLiteral("perspectiveX"), number_schema(-0.35, 0.35)}, {QStringLiteral("perspectiveY"), number_schema(-0.35, 0.35)},
      {QStringLiteral("opacity"), number_schema(0.0, 1.0)},
      {QStringLiteral("objectFit"), string_schema({QStringLiteral("fit"), QStringLiteral("fill"), QStringLiteral("stretch")})},
      {QStringLiteral("width"), number_schema(0.000001, 4.0, QStringLiteral("Frame width as a fraction of sequence width, before scale/rotation; for w pixels use w/sequenceWidth."))},
      {QStringLiteral("height"), number_schema(0.000001, 4.0, QStringLiteral("Frame height as a fraction of sequence height, before scale/rotation; for h pixels use h/sequenceHeight."))},
      {QStringLiteral("cornerRadius"), number_schema(0.0, 0.5)},
      {QStringLiteral("frameShadow"), frame_shadow_schema},
      {QStringLiteral("cropLeft"), number_schema(0.0, 1.0)}, {QStringLiteral("cropTop"), number_schema(0.0, 1.0)},
      {QStringLiteral("cropRight"), number_schema(0.0, 1.0)}, {QStringLiteral("cropBottom"), number_schema(0.0, 1.0)}},
      {QStringLiteral("clipId"), QStringLiteral("offsetX"), QStringLiteral("offsetY"), QStringLiteral("scale"),
       QStringLiteral("rotationDegrees"), QStringLiteral("perspectiveX"), QStringLiteral("perspectiveY"),
       QStringLiteral("opacity"), QStringLiteral("objectFit"), QStringLiteral("width"), QStringLiteral("height"),
       QStringLiteral("cornerRadius"), QStringLiteral("cropLeft"), QStringLiteral("cropTop"),
       QStringLiteral("cropRight"), QStringLiteral("cropBottom")}),
      [owner](const QVariantMap& values) {
    return owner ? owner->setTimelineClipFrameForAutomation(
        values.value(QStringLiteral("clipId")).toString(),
        values.value(QStringLiteral("offsetX")).toDouble(), values.value(QStringLiteral("offsetY")).toDouble(),
        values.value(QStringLiteral("scale")).toDouble(), values.value(QStringLiteral("rotationDegrees")).toDouble(),
        values.value(QStringLiteral("perspectiveX")).toDouble(), values.value(QStringLiteral("perspectiveY")).toDouble(),
        values.value(QStringLiteral("opacity")).toDouble(), values.value(QStringLiteral("objectFit")).toString(),
        values.value(QStringLiteral("width")).toDouble(), values.value(QStringLiteral("height")).toDouble(),
        values.value(QStringLiteral("cornerRadius")).toDouble(), values.value(QStringLiteral("cropLeft")).toDouble(),
        values.value(QStringLiteral("cropTop")).toDouble(), values.value(QStringLiteral("cropRight")).toDouble(),
        values.value(QStringLiteral("cropBottom")).toDouble(),
        values.value(QStringLiteral("frameShadow")).toMap())
                  : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before frame update.")}};
  });

  auto perspective_quad_point_schema = object_schema({
      {QStringLiteral("x"), number_schema(-4.0, 4.0)},
      {QStringLiteral("y"), number_schema(-4.0, 4.0)}},
      {QStringLiteral("x"), QStringLiteral("y")});
  auto perspective_quad_schema = object_schema({
      {QStringLiteral("clipId"), string_schema()},
      {QStringLiteral("enabled"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}}},
      {QStringLiteral("points"), array_schema(perspective_quad_point_schema, 4, 4)}},
      {QStringLiteral("clipId"), QStringLiteral("enabled")});
  perspective_quad_schema.insert(
      QStringLiteral("description"),
      QStringLiteral("Set or clear a static projective frame quad in normalized TL, TR, BR, BL order. Enabled quads require exactly four finite convex corners with each coordinate in -4 to 4. Omit points when disabled. Scene tracking attachments take precedence; otherwise this quad precedes legacy perspectiveX/Y."));
  register_action(QStringLiteral("setTimelineClipPerspectiveQuad"), perspective_quad_schema,
      [owner](const QVariantMap& values) {
    return owner ? owner->setTimelineClipPerspectiveQuadForAutomation(
        values.value(QStringLiteral("clipId")).toString(),
        values.value(QStringLiteral("enabled")).toBool(),
        values.value(QStringLiteral("points")).toList())
                 : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before perspective quad update.")}};
  });

  auto perspective_pose_schema = object_schema({
      {QStringLiteral("localSeconds"), number_schema(0.0, 3600.0)},
      {QStringLiteral("points"), array_schema(perspective_quad_point_schema, 4, 4)},
      {QStringLiteral("interpolation"), string_schema(
           {QStringLiteral("linear"), QStringLiteral("hold")})}},
      {QStringLiteral("localSeconds"), QStringLiteral("points")});
  auto perspective_animation_schema = object_schema({
      {QStringLiteral("clipId"), string_schema()},
      {QStringLiteral("keyframes"), array_schema(perspective_pose_schema, 0, 512)}},
      {QStringLiteral("clipId"), QStringLiteral("keyframes")});
  perspective_animation_schema.insert(
      QStringLiteral("description"),
      QStringLiteral("Set the clip's local-time perspective poses. Each pose has four normalized TL, TR, BR, BL points; poses interpolate linearly or hold. The native controller rejects trajectories that become non-convex between poses. An empty keyframes array clears only this quad animation."));
  register_action(QStringLiteral("setTimelineClipPerspectiveQuadAnimation"),
      perspective_animation_schema, [owner](const QVariantMap& values) {
    return owner ? owner->setTimelineClipPerspectiveQuadAnimationForAutomation(
        values.value(QStringLiteral("clipId")).toString(),
        values.value(QStringLiteral("keyframes")).toList())
                 : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before perspective animation update.")}};
  });

  auto sequence_mask_schema = object_schema({
      {QStringLiteral("clipId"), string_schema()},
      {QStringLiteral("shape"), string_schema({QStringLiteral("rectangle"), QStringLiteral("ellipse")})},
      {QStringLiteral("left"), number_schema(-4.0, 4.0)},
      {QStringLiteral("top"), number_schema(-4.0, 4.0)},
      {QStringLiteral("right"), number_schema(-4.0, 4.0)},
      {QStringLiteral("bottom"), number_schema(-4.0, 4.0)},
      {QStringLiteral("cornerRadius"), number_schema(0.0, 0.5)}},
      {QStringLiteral("clipId"), QStringLiteral("shape"), QStringLiteral("left"),
       QStringLiteral("top"), QStringLiteral("right"), QStringLiteral("bottom"),
       QStringLiteral("cornerRadius")});
  sequence_mask_schema.insert(
      QStringLiteral("description"),
      QStringLiteral("Mask bounds use normalized sequence-canvas edges from -4 to 4 and apply after clip geometry. Bounds may extend off canvas and may collapse to a zero-area transparent mask, but may not invert. Rectangle cornerRadius is a 0 to 0.5 fraction of the mask bounds' short edge; ellipses require zero."));
  register_action(QStringLiteral("setTimelineClipMask"), sequence_mask_schema,
      [owner](const QVariantMap& values) {
    const QVariantMap mask{
        {QStringLiteral("shape"), values.value(QStringLiteral("shape"))},
        {QStringLiteral("left"), values.value(QStringLiteral("left"))},
        {QStringLiteral("top"), values.value(QStringLiteral("top"))},
        {QStringLiteral("right"), values.value(QStringLiteral("right"))},
        {QStringLiteral("bottom"), values.value(QStringLiteral("bottom"))},
        {QStringLiteral("cornerRadius"), values.value(QStringLiteral("cornerRadius"))}};
    return owner ? owner->setTimelineClipMaskForAutomation(
        values.value(QStringLiteral("clipId")).toString(), mask)
                 : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before mask update.")}};
  });

  auto clear_sequence_mask_schema = object_schema(
      {{QStringLiteral("clipId"), string_schema()}},
      {QStringLiteral("clipId")});
  clear_sequence_mask_schema.insert(
      QStringLiteral("description"),
      QStringLiteral("Clear the sequence-space mask and mask animation channels for this clip in the active output variant."));
  register_action(QStringLiteral("clearTimelineClipMask"), clear_sequence_mask_schema,
      [owner](const QVariantMap& values) {
    return owner ? owner->clearTimelineClipMaskForAutomation(
        values.value(QStringLiteral("clipId")).toString())
                 : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before mask update.")}};
  });

  auto source_matte_schema = object_schema({
      {QStringLiteral("clipId"), string_schema()},
      {QStringLiteral("filePath"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")},
                                               {QStringLiteral("minLength"), 1},
                                               {QStringLiteral("maxLength"), 4096}}},
      {QStringLiteral("mode"), string_schema({QStringLiteral("alpha"), QStringLiteral("luminance")})},
      {QStringLiteral("invert"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}}},
      {QStringLiteral("fillColor"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")},
                                                 {QStringLiteral("maxLength"), 9}}}},
      {QStringLiteral("clipId"), QStringLiteral("filePath"), QStringLiteral("mode"),
       QStringLiteral("invert")});
  source_matte_schema.insert(
      QStringLiteral("description"),
      QStringLiteral("Use an exact admitted source path and alpha or luminance coverage. Optional fillColor replaces source RGB and uses native QColor hex form #RRGGBB or #AARRGGBB; its alpha remains separate from matte coverage."));
  register_action(QStringLiteral("setTimelineClipMatte"), source_matte_schema,
      [owner](const QVariantMap& values) {
    return owner ? owner->setTimelineClipMatteForAutomation(
        values.value(QStringLiteral("clipId")).toString(),
        values.value(QStringLiteral("filePath")).toString(),
        values.value(QStringLiteral("mode")).toString(),
        values.value(QStringLiteral("invert")).toBool(),
        values.value(QStringLiteral("fillColor")).toString())
                 : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before source matte update.")}};
  }, [authorize_path](const QVariantMap& values) {
    return authorize_path
        ? authorize_path(QStringLiteral("import_media"), values.value(QStringLiteral("filePath")).toString())
        : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("A task-admitted source matte path is required.")}};
  });

  register_action(QStringLiteral("setTimelineClipBlendMode"), object_schema({
      {QStringLiteral("clipId"), string_schema()},
      {QStringLiteral("blendMode"), string_schema({QStringLiteral("normal"),
          QStringLiteral("screen"), QStringLiteral("soft-light")})}},
      {QStringLiteral("clipId"), QStringLiteral("blendMode")}),
      [owner](const QVariantMap& values) {
    return owner ? owner->setTimelineClipBlendModeForAutomation(
        values.value(QStringLiteral("clipId")).toString(),
        values.value(QStringLiteral("blendMode")).toString())
                 : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before blend mode update.")}};
  });

  const auto keyframe_schema = object_schema({
      {QStringLiteral("localSeconds"), number_schema(0.0, 3600.0)},
      {QStringLiteral("value"), QVariantMap{{QStringLiteral("type"), QStringLiteral("number")}}},
      {QStringLiteral("interpolation"), string_schema({QStringLiteral("hold"), QStringLiteral("linear"), QStringLiteral("ease-in"), QStringLiteral("ease-out"), QStringLiteral("ease-in-out"), QStringLiteral("bezier")})}},
      {QStringLiteral("localSeconds"), QStringLiteral("value")});
  const auto channels_schema = array_schema(object_schema({
      {QStringLiteral("parameter"), string_schema()},
      {QStringLiteral("keyframes"), array_schema(keyframe_schema, 0, 20000)}},
      {QStringLiteral("parameter"), QStringLiteral("keyframes")}), 1, 32);
  register_action(QStringLiteral("setTimelineClipAnimation"), object_schema({
      {QStringLiteral("clipId"), string_schema()}, {QStringLiteral("channels"), channels_schema}},
      {QStringLiteral("clipId"), QStringLiteral("channels")}),
      [owner](const QVariantMap& values) {
    return owner ? owner->setTimelineClipAnimationForAutomation(
        values.value(QStringLiteral("clipId")).toString(), values.value(QStringLiteral("channels")).toList())
                  : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before animation update.")}};
  });
  register_action(QStringLiteral("editTimelineClipKeyframe"), object_schema({
      {QStringLiteral("clipId"), string_schema()},
      {QStringLiteral("operation"), string_schema({QStringLiteral("set"), QStringLiteral("remove")})},
      {QStringLiteral("parameter"), string_schema()},
      {QStringLiteral("localSeconds"), number_schema(0.0, 3600.0)},
      {QStringLiteral("value"), QVariantMap{{QStringLiteral("type"), QStringLiteral("number")}}},
      {QStringLiteral("interpolation"), string_schema({QStringLiteral("hold"), QStringLiteral("linear"), QStringLiteral("ease-in"), QStringLiteral("ease-out"), QStringLiteral("ease-in-out"), QStringLiteral("bezier")})}},
      {QStringLiteral("clipId"), QStringLiteral("operation"), QStringLiteral("parameter"), QStringLiteral("localSeconds"), QStringLiteral("value")}),
      [owner](const QVariantMap& values) {
    return owner ? owner->editClipKeyframeForAutomation(
        values.value(QStringLiteral("clipId")).toString(), values.value(QStringLiteral("operation")).toString(),
        values.value(QStringLiteral("parameter")).toString(), values.value(QStringLiteral("localSeconds")).toDouble(),
        values.value(QStringLiteral("value")).toDouble(), values.value(QStringLiteral("interpolation")).toString())
                  : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline closed before keyframe update.")}};
  });

  const QVariantMap review_frame_schema{
      {QStringLiteral("type"), QStringLiteral("integer")},
      {QStringLiteral("minimum"), 0},
      {QStringLiteral("maximum"), 10000000}};
  const auto review_clip_ids_schema = array_schema(
      QVariantMap{{QStringLiteral("type"), QStringLiteral("string")},
                  {QStringLiteral("minLength"), 1},
                  {QStringLiteral("maxLength"), 128}},
      0, 32);
  const auto review_label_schema = QVariantMap{
      {QStringLiteral("type"), QStringLiteral("string")},
      {QStringLiteral("maxLength"), 160}};
  const auto review_note_schema = QVariantMap{
      {QStringLiteral("type"), QStringLiteral("string")},
      {QStringLiteral("minLength"), 1},
      {QStringLiteral("maxLength"), 4000}};
  const auto review_assignee_schema = QVariantMap{
      {QStringLiteral("type"), QStringLiteral("string")},
      {QStringLiteral("maxLength"), 120}};
  const auto review_approval_schema = string_schema({
      QStringLiteral("open"), QStringLiteral("changes-requested"), QStringLiteral("approved")});
  const auto review_marker_id_schema = QVariantMap{
      {QStringLiteral("type"), QStringLiteral("string")},
      {QStringLiteral("pattern"), QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")}};
  register_action(QStringLiteral("createReviewNote"), object_schema({
      {QStringLiteral("markerId"), review_marker_id_schema},
      {QStringLiteral("label"), review_label_schema},
      {QStringLiteral("note"), review_note_schema},
      {QStringLiteral("startFrame"), review_frame_schema},
      {QStringLiteral("endFrame"), review_frame_schema},
      {QStringLiteral("clipIds"), review_clip_ids_schema},
      {QStringLiteral("assignee"), review_assignee_schema},
      {QStringLiteral("approvalState"), review_approval_schema}},
      {QStringLiteral("markerId"), QStringLiteral("label"), QStringLiteral("note"),
       QStringLiteral("startFrame"), QStringLiteral("endFrame")} ),
      [owner](const QVariantMap& values) {
    if (!owner) {
      return QVariantMap{{QStringLiteral("ok"), false},
                         {QStringLiteral("mutationNotApplied"), true},
                         {QStringLiteral("error"), QStringLiteral("Timeline closed before review note creation.")}};
    }
    const auto requested_id = values.value(QStringLiteral("markerId")).toString();
    const auto existing_notes = owner->reviewNotes();
    const auto existed = std::any_of(
        existing_notes.cbegin(), existing_notes.cend(),
        [&requested_id](const QVariant& row_value) {
          return row_value.toMap().value(QStringLiteral("markerId")).toString() == requested_id;
        });
    const auto marker_id = owner->addAnchoredReviewNote(
        values.value(QStringLiteral("label")).toString(),
        values.value(QStringLiteral("note")).toString(),
        values.value(QStringLiteral("startFrame")).toLongLong(),
        values.value(QStringLiteral("endFrame")).toLongLong(),
        values.value(QStringLiteral("clipIds")).toStringList(),
        values.value(QStringLiteral("assignee")).toString(),
        values.value(QStringLiteral("approvalState")).toString(),
        requested_id);
    if (marker_id.isEmpty()) {
      return QVariantMap{{QStringLiteral("ok"), false},
                         {QStringLiteral("mutationNotApplied"), true},
                         {QStringLiteral("recoverableError"), true},
                         {QStringLiteral("error"), owner->status()}};
    }
    return QVariantMap{{QStringLiteral("ok"), true},
                       {QStringLiteral("markerId"), marker_id},
                       {QStringLiteral("alreadySatisfied"), existed}};
  });
  register_action(QStringLiteral("updateReviewNote"), object_schema({
      {QStringLiteral("markerId"), review_marker_id_schema},
      {QStringLiteral("label"), review_label_schema},
      {QStringLiteral("note"), review_note_schema},
      {QStringLiteral("startFrame"), review_frame_schema},
      {QStringLiteral("endFrame"), review_frame_schema},
      {QStringLiteral("clipIds"), review_clip_ids_schema},
      {QStringLiteral("assignee"), review_assignee_schema},
      {QStringLiteral("approvalState"), review_approval_schema},
      {QStringLiteral("resolved"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}}}},
      {QStringLiteral("markerId"), QStringLiteral("label"), QStringLiteral("note"),
       QStringLiteral("startFrame"), QStringLiteral("endFrame"), QStringLiteral("clipIds"),
       QStringLiteral("assignee"), QStringLiteral("approvalState"), QStringLiteral("resolved")} ),
      [owner](const QVariantMap& values) {
    if (!owner) {
      return QVariantMap{{QStringLiteral("ok"), false},
                         {QStringLiteral("mutationNotApplied"), true},
                         {QStringLiteral("error"), QStringLiteral("Timeline closed before review note update.")}};
    }
    const auto marker_id = values.value(QStringLiteral("markerId")).toString();
    for (const auto& row_value : owner->reviewNotes()) {
      const auto row = row_value.toMap();
      if (row.value(QStringLiteral("markerId")).toString() != marker_id) continue;
      const auto expected_end_frame = values.value(QStringLiteral("endFrame")).toLongLong();
      const auto already_satisfied =
          row.value(QStringLiteral("label")).toString() ==
              (values.value(QStringLiteral("label")).toString().simplified().isEmpty()
                   ? row.value(QStringLiteral("label")).toString()
                   : values.value(QStringLiteral("label")).toString().simplified()) &&
          row.value(QStringLiteral("note")).toString() == values.value(QStringLiteral("note")).toString().trimmed() &&
          row.value(QStringLiteral("frame")).toLongLong() == values.value(QStringLiteral("startFrame")).toLongLong() &&
          row.value(QStringLiteral("endFrame")).toLongLong() == expected_end_frame &&
          row.value(QStringLiteral("clipIds")).toStringList() == values.value(QStringLiteral("clipIds")).toStringList() &&
          row.value(QStringLiteral("assignee")).toString() == values.value(QStringLiteral("assignee")).toString().simplified() &&
          row.value(QStringLiteral("approvalState")).toString() == values.value(QStringLiteral("approvalState")).toString() &&
          row.value(QStringLiteral("resolved")).toBool() == values.value(QStringLiteral("resolved")).toBool();
      if (already_satisfied) {
        return QVariantMap{{QStringLiteral("ok"), true}, {QStringLiteral("markerId"), marker_id},
                           {QStringLiteral("alreadySatisfied"), true}};
      }
      const auto changed = owner->updateAnchoredReviewNote(
          marker_id, values.value(QStringLiteral("label")).toString(),
          values.value(QStringLiteral("note")).toString(),
          values.value(QStringLiteral("startFrame")).toLongLong(), expected_end_frame,
          values.value(QStringLiteral("clipIds")).toStringList(),
          values.value(QStringLiteral("assignee")).toString(),
          values.value(QStringLiteral("approvalState")).toString(),
          values.value(QStringLiteral("resolved")).toBool());
      return changed
          ? QVariantMap{{QStringLiteral("ok"), true}, {QStringLiteral("markerId"), marker_id}}
          : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("mutationNotApplied"), true},
                        {QStringLiteral("recoverableError"), true}, {QStringLiteral("error"), owner->status()}};
    }
    return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("mutationNotApplied"), true},
                       {QStringLiteral("error"), QStringLiteral("Choose an existing review note ID.")}};
  });
  register_action(QStringLiteral("resolveReviewNote"), object_schema({
      {QStringLiteral("markerId"), review_marker_id_schema},
      {QStringLiteral("resolved"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}}}},
      {QStringLiteral("markerId"), QStringLiteral("resolved")} ),
      [owner](const QVariantMap& values) {
    if (!owner) {
      return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("mutationNotApplied"), true},
                         {QStringLiteral("error"), QStringLiteral("Timeline closed before review note resolution.")}};
    }
    const auto marker_id = values.value(QStringLiteral("markerId")).toString();
    const auto resolved = values.value(QStringLiteral("resolved")).toBool();
    for (const auto& row_value : owner->reviewNotes()) {
      const auto row = row_value.toMap();
      if (row.value(QStringLiteral("markerId")).toString() != marker_id) continue;
      if (row.value(QStringLiteral("resolved")).toBool() == resolved) {
        return QVariantMap{{QStringLiteral("ok"), true}, {QStringLiteral("markerId"), marker_id},
                           {QStringLiteral("alreadySatisfied"), true}};
      }
      return owner->resolveTimelineReviewNote(marker_id, resolved)
          ? QVariantMap{{QStringLiteral("ok"), true}, {QStringLiteral("markerId"), marker_id},
                        {QStringLiteral("resolved"), resolved}}
          : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("mutationNotApplied"), true},
                        {QStringLiteral("recoverableError"), true}, {QStringLiteral("error"), owner->status()}};
    }
    return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("mutationNotApplied"), true},
                       {QStringLiteral("error"), QStringLiteral("Choose an existing review note ID.")}};
  });

  const auto batch_command_ids =
      AgentTaskJournal::synchronousTimelineDraftBatchCommandIds();
  const auto batch_values_schema = QVariantMap{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("maxProperties"), 64},
      {QStringLiteral("additionalProperties"), true}};
  const auto batch_operation_schema = object_schema({
      {QStringLiteral("id"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")},
          {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 64},
          {QStringLiteral("pattern"), QStringLiteral("^[A-Za-z][A-Za-z0-9_-]{0,63}$")}}},
      {QStringLiteral("commandId"), string_schema(batch_command_ids)},
      {QStringLiteral("values"), batch_values_schema}},
      {QStringLiteral("id"), QStringLiteral("commandId"), QStringLiteral("values")});
  auto batch_schema = object_schema({
      {QStringLiteral("batchId"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")},
          {QStringLiteral("minLength"), 36}, {QStringLiteral("maxLength"), 36},
          {QStringLiteral("pattern"), QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")}}},
      {QStringLiteral("operations"), array_schema(batch_operation_schema, 1, 16)}},
      {QStringLiteral("batchId"), QStringLiteral("operations")});
  batch_schema.insert(QStringLiteral("description"),
      QStringLiteral("One reversible draft transaction containing 1 to 16 synchronous generated, text, graphic, graphics-stack order, frame, mask, blend, timing, or animation actions. Imports, matte paths, compound renders, saves, exports and commits are not allowed. Stable-ID references may use only earlier operation results."));
  if (enable_draft_batch) register_action_with_preimage(QStringLiteral("applyDraftBatch"), batch_schema,
      [owner, &registry, batch_command_ids](const QVariantMap& values,
          const QByteArray& task_preimage) {
    if (!owner) {
      return QVariantMap{{QStringLiteral("ok"), false},
          {QStringLiteral("error"), QStringLiteral("Timeline closed before the authoring batch began.")}};
    }
    const auto payload_bytes = QJsonDocument::fromVariant(values).toJson(QJsonDocument::Compact);
    if (payload_bytes.size() > 256 * 1024) {
      return QVariantMap{{QStringLiteral("ok"), false},
          {QStringLiteral("error"), QStringLiteral("The authoring batch exceeds the 256 KiB input limit.")},
          {QStringLiteral("mutationNotApplied"), true}};
    }
    const auto operations = values.value(QStringLiteral("operations")).toList();
    if (operations.isEmpty() || operations.size() > 16) {
      return QVariantMap{{QStringLiteral("ok"), false},
          {QStringLiteral("error"), QStringLiteral("An authoring batch must contain 1 to 16 operations.")},
          {QStringLiteral("mutationNotApplied"), true}};
    }

    QHash<QString, int> operation_indexes;
    QHash<QString, QSet<QString>> stable_fields;
    int keyframe_count = 0;
    const auto count_keyframes = [](const auto& self, const QVariant& value, int depth) -> int {
      if (depth > 32) return 2049;
      int count = 0;
      if (value.metaType().id() == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
          if ((it.key() == QStringLiteral("keyframes") || it.key() == QStringLiteral("frames")) &&
              it.value().metaType().id() == QMetaType::QVariantList) {
            count += it.value().toList().size();
          }
          count += self(self, it.value(), depth + 1);
          if (count > 2048) return count;
        }
      } else if (value.metaType().id() == QMetaType::QVariantList) {
        for (const auto& item : value.toList()) {
          count += self(self, item, depth + 1);
          if (count > 2048) return count;
        }
      }
      return count;
    };
    for (int index = 0; index < operations.size(); ++index) {
      const auto operation = operations.at(index).toMap();
      const auto operation_id = operation.value(QStringLiteral("id")).toString();
      const auto command_id = operation.value(QStringLiteral("commandId")).toString();
      const auto operation_values = operation.value(QStringLiteral("values")).toMap();
      if (operation_id.isEmpty() || operation_indexes.contains(operation_id) ||
          !batch_command_ids.contains(command_id)) {
        return QVariantMap{{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Batch operation IDs must be unique and commands must belong to the synchronous draft allowlist.")},
            {QStringLiteral("mutationNotApplied"), true}};
      }
      operation_indexes.insert(operation_id, index);
      QSet<QString> fields;
      if (command_id == QStringLiteral("timeline.action.createTimelineTrack")) {
        fields.insert(QStringLiteral("trackId"));
      } else if (command_id == QStringLiteral("timeline.action.createTimelineTitle") ||
                 command_id == QStringLiteral("timeline.action.createTimelineGraphic") ||
                 command_id == QStringLiteral("timeline.action.createTimelineGenerator")) {
        fields.insert(QStringLiteral("createdClipId"));
        fields.insert(QStringLiteral("trackId"));
      }
      if (operation_values.contains(QStringLiteral("trackId"))) fields.insert(QStringLiteral("trackId"));
      stable_fields.insert(operation_id, fields);
      keyframe_count += count_keyframes(count_keyframes, operation_values, 0);
      if (command_id == QStringLiteral("timeline.action.editTimelineClipKeyframe"))
        ++keyframe_count;
      if (keyframe_count > 2048) {
        return QVariantMap{{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("The authoring batch exceeds the 2,048 total keyframe/frame limit.")},
            {QStringLiteral("mutationNotApplied"), true}};
      }
    }

    const auto rewrite_references = [](const auto& self, const QVariant& value,
        const QHash<QString, QVariantMap>& completed_results,
        const QHash<QString, int>& indexes, const QHash<QString, QSet<QString>>& fields,
        int current_index, bool preflight, int depth, bool* ok, QString* error) -> QVariant {
      if (!ok || !error || !*ok) return {};
      if (depth > 32) {
        *ok = false;
        *error = QStringLiteral("Batch values exceed the maximum nesting depth.");
        return {};
      }
      if (value.metaType().id() == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        if (map.contains(QStringLiteral("result")) || map.contains(QStringLiteral("field"))) {
          if (map.size() != 2 || !map.contains(QStringLiteral("result")) ||
              !map.contains(QStringLiteral("field")) ||
              map.value(QStringLiteral("result")).metaType().id() != QMetaType::QString ||
              map.value(QStringLiteral("field")).metaType().id() != QMetaType::QString) {
            *ok = false;
            *error = QStringLiteral("A stable-ID reference must contain exactly string result and field keys.");
            return {};
          }
          const auto source_id = map.value(QStringLiteral("result")).toString();
          const auto field = map.value(QStringLiteral("field")).toString();
          const auto source_index = indexes.value(source_id, -1);
          if (source_index < 0 || source_index >= current_index || !fields.value(source_id).contains(field)) {
            *ok = false;
            *error = QStringLiteral("Stable-ID references may point only to an earlier result field declared by that operation.");
            return {};
          }
          if (preflight) return QStringLiteral("batch-stable-id-placeholder");
          const auto source_result = completed_results.value(source_id);
          const auto resolved = source_result.value(field);
          if (resolved.metaType().id() != QMetaType::QString || resolved.toString().isEmpty()) {
            *ok = false;
            *error = QStringLiteral("A prior operation did not produce the referenced stable ID.");
            return {};
          }
          return resolved;
        }
        QVariantMap rewritten;
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
          rewritten.insert(it.key(), self(self, it.value(), completed_results, indexes, fields,
                                          current_index, preflight, depth + 1, ok, error));
          if (!*ok) return {};
        }
        return rewritten;
      }
      if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList rewritten;
        for (const auto& item : value.toList()) {
          rewritten.append(self(self, item, completed_results, indexes, fields,
                                current_index, preflight, depth + 1, ok, error));
          if (!*ok) return {};
        }
        return rewritten;
      }
      return value;
    };

    QHash<QString, QVariantMap> no_results;
    for (int index = 0; index < operations.size(); ++index) {
      const auto operation = operations.at(index).toMap();
      const auto command_id = operation.value(QStringLiteral("commandId")).toString();
      const auto action_schema = registry.actionInputSchema(command_id);
      if (!action_schema.value(QStringLiteral("ok")).toBool()) return action_schema;
      bool valid = true;
      QString error;
      const auto prepared_values = rewrite_references(rewrite_references,
          operation.value(QStringLiteral("values")), no_results, operation_indexes,
          stable_fields, index, true, 0, &valid, &error).toMap();
      if (!valid || !AgentAppCommandRegistry::valueMatchesSchema(
              prepared_values, action_schema.value(QStringLiteral("inputSchema")).toMap())) {
        if (error.isEmpty()) error = QStringLiteral("A batch operation does not match its registered typed action schema.");
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error},
            {QStringLiteral("mutationNotApplied"), true}};
      }
    }

    QHash<QString, QVariantMap> completed_results;
    QVector<std::function<QVariantMap()>> native_operations;
    native_operations.reserve(operations.size());
    for (int index = 0; index < operations.size(); ++index) {
      const auto operation = operations.at(index).toMap();
      const auto operation_id = operation.value(QStringLiteral("id")).toString();
      const auto command_id = operation.value(QStringLiteral("commandId")).toString();
      const auto raw_values = operation.value(QStringLiteral("values"));
      native_operations.append([&, index, operation_id, command_id, raw_values]() {
        bool valid = true;
        QString error;
        const auto child_values = rewrite_references(rewrite_references, raw_values,
            completed_results, operation_indexes, stable_fields, index, false, 0, &valid, &error).toMap();
        QVariantMap result;
        if (valid) result = registry.invokeActionWithinMutation(command_id, child_values);
        else result = {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error},
                       {QStringLiteral("mutationNotApplied"), true}};
        QVariantMap compact{{QStringLiteral("id"), operation_id},
            {QStringLiteral("commandId"), command_id},
            {QStringLiteral("ok"), result.value(QStringLiteral("ok")).toBool()}};
        const auto stable_clip_id = result.value(QStringLiteral("createdClipId")).toString();
        const auto stable_track_id = result.value(QStringLiteral("trackId"),
            child_values.value(QStringLiteral("trackId"))).toString();
        if (!stable_clip_id.isEmpty()) {
          compact.insert(QStringLiteral("createdClipId"), stable_clip_id);
        }
        if (!stable_track_id.isEmpty()) compact.insert(QStringLiteral("trackId"), stable_track_id);
        if (result.contains(QStringLiteral("alreadySatisfied")))
          compact.insert(QStringLiteral("alreadySatisfied"), result.value(QStringLiteral("alreadySatisfied")));
        if (!result.value(QStringLiteral("ok")).toBool()) {
          compact.insert(QStringLiteral("error"), result.value(QStringLiteral("error"),
              QStringLiteral("The native action failed.")));
          if (result.value(QStringLiteral("recoveryRequired")).toBool())
            compact.insert(QStringLiteral("recoveryRequired"), true);
        } else {
          completed_results.insert(operation_id, compact);
        }
        return compact;
      });
    }

    const auto transaction = task_preimage.isEmpty()
        ? owner->runAgentTaskMutationBatch(native_operations)
        : owner->runAgentTaskMutationBatchWithPreimage(
              native_operations, task_preimage);
    QVariantMap receipt{{QStringLiteral("ok"), transaction.value(QStringLiteral("ok")).toBool()},
        {QStringLiteral("batchId"), values.value(QStringLiteral("batchId"))},
        {QStringLiteral("batchRequestSha256"), QString::fromLatin1(
            QCryptographicHash::hash(payload_bytes, QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("batchResults"), transaction.value(QStringLiteral("results"))},
        {QStringLiteral("changed"), transaction.value(QStringLiteral("changed")).toBool()},
        {QStringLiteral("operationCount"), operations.size()}};
    for (const auto& key : {QStringLiteral("error"), QStringLiteral("rolledBack"),
                            QStringLiteral("recoveryRequired"), QStringLiteral("mutationNotApplied")})
      if (transaction.contains(key)) receipt.insert(key, transaction.value(key));
    return receipt;
  });

  register_action(QStringLiteral("saveTimeline"), object_schema({
      {QStringLiteral("outputPath"), string_schema()}}, {QStringLiteral("outputPath")}),
      [owner, &tasks, authorize_path](const QVariantMap& values) {
    if (!owner || !authorize_path) {
      return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("A task-owned Timeline output path is required.")}};
    }
    const auto admission = authorize_path(QStringLiteral("save_project"),
                                         values.value(QStringLiteral("outputPath")).toString());
    if (!admission.value(QStringLiteral("ok")).toBool()) return admission;
    const auto canonical_path = admission.value(QStringLiteral("canonicalPath")).toString();
    auto file_resource = AgentTaskCoordinator::taskOwnedFileResource(canonical_path);
    if (canonical_path.isEmpty() || file_resource.resource_id.isEmpty() ||
        file_resource.canonical_path != canonical_path ||
        (!admission.value(QStringLiteral("resourceId")).toString().isEmpty() &&
         admission.value(QStringLiteral("resourceId")).toString() != file_resource.resource_id)) {
      return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("The authorized Timeline output path cannot be safely owned by this task.")}};
    }
    const auto enrolled = tasks.enroll(file_resource.resource_id, file_resource.adapter);
    if (!enrolled.value(QStringLiteral("ok")).toBool()) return enrolled;
    const auto recorded = tasks.journal()->recordOwnedOutputPath(canonical_path);
    if (!recorded.value(QStringLiteral("ok")).toBool()) return recorded;
    auto result = owner->saveTimelineCopyForAutomation(canonical_path);
    if (QFileInfo::exists(canonical_path)) {
      const auto checkpoint = tasks.checkpointOwnedResource(file_resource.resource_id);
      if (!checkpoint.value(QStringLiteral("ok")).toBool()) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"), checkpoint.value(QStringLiteral("error")));
        result.insert(QStringLiteral("recoveryRequired"), true);
      }
    }
    return result;
  }, [authorize_path](const QVariantMap& values) {
    return authorize_path
        ? authorize_path(QStringLiteral("save_project"), values.value(QStringLiteral("outputPath")).toString())
        : QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("A task-owned Timeline output path is required.")}};
  });
}

}  // namespace photo_editor::desktop
