#pragma once

#include "agent_app_command_registry.h"
#include "timeline_document_controller.h"

#include <QPointer>
#include <QVariantMap>

namespace photo_editor::desktop {

inline QVariantMap timeline_audio_state_failure(const QString& error,
                                                const QString& error_code) {
  return {{QStringLiteral("ok"), false},
          {QStringLiteral("error"), error},
          {QStringLiteral("errorCode"), error_code},
          {QStringLiteral("recoverableError"), true},
          {QStringLiteral("mutationNotApplied"), true}};
}

inline void register_agent_timeline_audio_state_commands(
    AgentAppCommandRegistry& registry,
    TimelineDocumentController* timeline,
    const AgentAppCommandRegistry::MutationOwner& mutation_owner) {
  const QPointer<TimelineDocumentController> owner(timeline);
  const auto object_schema = [](const QVariantMap& properties,
                                const QStringList& required) {
    return QVariantMap{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), properties},
        {QStringLiteral("required"), required},
        {QStringLiteral("additionalProperties"), false}};
  };
  const auto string_property = [] {
    return QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}};
  };
  const auto boolean_property = [] {
    return QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}};
  };
  const auto audio_roles = QStringList{
      QStringLiteral("dialogue"), QStringLiteral("voiceover"),
      QStringLiteral("music"), QStringLiteral("ambience"),
      QStringLiteral("effects"), QStringLiteral("commentary")};

  AgentAppCommandRegistry::Policy policy;
  policy.available = owner != nullptr;
  policy.resource = QStringLiteral("timeline-document");
  policy.reversal = QStringLiteral("exact-task-baseline");
  policy.reason = QStringLiteral(
      "Set one unlocked audio track by stable ID inside an owned Timeline task.");

  const auto find_audio_track = [owner](const QString& requested_id,
                                        int* row,
                                        QVariantMap* track) {
    if (!owner || row == nullptr || track == nullptr ||
        requested_id.trimmed().isEmpty() || !owner->hasActiveTimeline()) {
      return false;
    }
    const auto tracks = owner->snapshotTimelineTracks();
    auto match_count = 0;
    for (int index = 0; index < tracks.size(); ++index) {
      const auto candidate = tracks.at(index).toMap();
      if (candidate.value(QStringLiteral("trackId")).toString() != requested_id) {
        continue;
      }
      ++match_count;
      *row = index;
      *track = candidate;
    }
    return match_count == 1 &&
        track->value(QStringLiteral("trackKind")).toString() ==
            QStringLiteral("audio");
  };

  const auto muted_schema = object_schema(
      {{QStringLiteral("trackId"), string_property()},
       {QStringLiteral("muted"), boolean_property()},
       {QStringLiteral("expectedMuted"), boolean_property()}},
      {QStringLiteral("trackId"), QStringLiteral("muted"),
       QStringLiteral("expectedMuted")});
  registry.registerAction(
      QStringLiteral("timeline"), QStringLiteral("setTimelineTrackMuted"),
      owner, muted_schema, policy, mutation_owner,
      [owner, find_audio_track](const QVariantMap& values) {
        const auto track_id = values.value(QStringLiteral("trackId")).toString();
        const auto desired = values.value(QStringLiteral("muted")).toBool();
        const auto expected = values.value(QStringLiteral("expectedMuted")).toBool();
        auto row = -1;
        QVariantMap track;
        if (!find_audio_track(track_id, &row, &track)) {
          return timeline_audio_state_failure(
              QStringLiteral("Choose one existing audio track by stable ID."),
              QStringLiteral("track_not_found_or_not_audio"));
        }
        const auto current = track.value(QStringLiteral("muted")).toBool();
        if (current == desired) {
          return QVariantMap{
              {QStringLiteral("ok"), true},
              {QStringLiteral("changed"), false},
              {QStringLiteral("alreadySatisfied"), true},
              {QStringLiteral("trackId"), track_id},
              {QStringLiteral("previousMuted"), current},
              {QStringLiteral("muted"), current}};
        }
        if (current != expected) {
          return timeline_audio_state_failure(
              QStringLiteral("The audio track mute state changed after the caller's readback."),
              QStringLiteral("stale_track_state"));
        }
        if (track.value(QStringLiteral("locked")).toBool()) {
          return timeline_audio_state_failure(
              QStringLiteral("Unlock the target audio track before changing its mute state."),
              QStringLiteral("track_locked"));
        }
        if (!owner || !owner->setTrackAudioSetting(
                          row, QStringLiteral("muted"), desired)) {
          auto after_row = -1;
          QVariantMap after;
          if (find_audio_track(track_id, &after_row, &after) &&
              after.value(QStringLiteral("muted")).toBool() == desired) {
            return QVariantMap{
                {QStringLiteral("ok"), true},
                {QStringLiteral("changed"), true},
                {QStringLiteral("trackId"), track_id},
                {QStringLiteral("previousMuted"), current},
                {QStringLiteral("muted"), desired}};
          }
          return timeline_audio_state_failure(
              QStringLiteral("The Timeline rejected the audio track mute update."),
              QStringLiteral("native_update_rejected"));
        }
        return QVariantMap{
            {QStringLiteral("ok"), true},
            {QStringLiteral("changed"), true},
            {QStringLiteral("trackId"), track_id},
            {QStringLiteral("previousMuted"), current},
            {QStringLiteral("muted"), desired}};
      });

  auto role_property = string_property();
  role_property.insert(QStringLiteral("enum"), audio_roles);
  auto expected_role_property = role_property;
  const auto role_schema = object_schema(
      {{QStringLiteral("trackId"), string_property()},
       {QStringLiteral("audioRole"), role_property},
       {QStringLiteral("expectedAudioRole"), expected_role_property}},
      {QStringLiteral("trackId"), QStringLiteral("audioRole"),
       QStringLiteral("expectedAudioRole")});
  registry.registerAction(
      QStringLiteral("timeline"), QStringLiteral("setTimelineTrackAudioRole"),
      owner, role_schema, policy, mutation_owner,
      [owner, find_audio_track](const QVariantMap& values) {
        const auto track_id = values.value(QStringLiteral("trackId")).toString();
        const auto desired = values.value(QStringLiteral("audioRole")).toString();
        const auto expected =
            values.value(QStringLiteral("expectedAudioRole")).toString();
        auto row = -1;
        QVariantMap track;
        if (!find_audio_track(track_id, &row, &track)) {
          return timeline_audio_state_failure(
              QStringLiteral("Choose one existing audio track by stable ID."),
              QStringLiteral("track_not_found_or_not_audio"));
        }
        const auto current =
            track.value(QStringLiteral("audioRole")).toString();
        if (current == desired) {
          return QVariantMap{
              {QStringLiteral("ok"), true},
              {QStringLiteral("changed"), false},
              {QStringLiteral("alreadySatisfied"), true},
              {QStringLiteral("trackId"), track_id},
              {QStringLiteral("previousAudioRole"), current},
              {QStringLiteral("audioRole"), current},
              {QStringLiteral("audioBus"),
               track.value(QStringLiteral("audioBus"))}};
        }
        if (current != expected) {
          return timeline_audio_state_failure(
              QStringLiteral("The audio track role changed after the caller's readback."),
              QStringLiteral("stale_track_state"));
        }
        if (track.value(QStringLiteral("locked")).toBool()) {
          return timeline_audio_state_failure(
              QStringLiteral("Unlock the target audio track before changing its role."),
              QStringLiteral("track_locked"));
        }
        if (!owner || !owner->setTrackAudioSetting(
                          row, QStringLiteral("audioRole"), desired)) {
          auto after_row = -1;
          QVariantMap after;
          if (find_audio_track(track_id, &after_row, &after) &&
              after.value(QStringLiteral("audioRole")).toString() == desired) {
            return QVariantMap{
                {QStringLiteral("ok"), true},
                {QStringLiteral("changed"), true},
                {QStringLiteral("trackId"), track_id},
                {QStringLiteral("previousAudioRole"), current},
                {QStringLiteral("audioRole"), desired},
                {QStringLiteral("audioBus"),
                 after.value(QStringLiteral("audioBus"))}};
          }
          return timeline_audio_state_failure(
              QStringLiteral("The Timeline rejected the audio track role update."),
              QStringLiteral("native_update_rejected"));
        }
        auto after_row = -1;
        QVariantMap after;
        if (!find_audio_track(track_id, &after_row, &after) ||
            after.value(QStringLiteral("audioRole")).toString() != desired) {
          return QVariantMap{
              {QStringLiteral("ok"), false},
              {QStringLiteral("changed"), true},
              {QStringLiteral("recoveryRequired"), true},
              {QStringLiteral("trackId"), track_id},
              {QStringLiteral("error"),
               QStringLiteral("The role update was invoked but native readback did not confirm it.")}};
        }
        return QVariantMap{
            {QStringLiteral("ok"), true},
            {QStringLiteral("changed"), true},
            {QStringLiteral("trackId"), track_id},
            {QStringLiteral("previousAudioRole"), current},
            {QStringLiteral("audioRole"), desired},
            {QStringLiteral("audioBus"),
             after.value(QStringLiteral("audioBus"))}};
      });
}

}  // namespace photo_editor::desktop
