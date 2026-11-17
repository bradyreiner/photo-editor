bool run_timeline_anchored_review_notes_workflow(const QString& workspace_path) {
  const auto fail_workflow = [](const char* message) {
    std::cerr << message << '\n';
    return false;
  };
  const auto photo_still = touch_file(
      QDir(workspace_path).filePath(QStringLiteral("anchored-review-still.jpg")));
  if (photo_still.isEmpty()) {
    return fail_workflow("the anchored review fixture source should be writable");
  }

  TimelineDocumentController legacy_review_controller;
  legacy_review_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Legacy Point Review Note")},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 180},
      {QStringLiteral("frameRate"), 24.0},
  });
  const auto legacy_note_id = legacy_review_controller.addReviewNote(
      QStringLiteral("Legacy point"), QStringLiteral("Keep the established point-note format."));
  const auto legacy_markers_json =
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(legacy_review_controller)
          .value(QStringLiteral("markers")).toArray();
  const auto legacy_marker = legacy_markers_json.isEmpty()
      ? QJsonObject{}
      : legacy_markers_json.first().toObject();
  if (legacy_note_id.isEmpty() ||
      legacy_marker.value(QStringLiteral("markerId")).toString() != legacy_note_id ||
      legacy_marker.contains(QStringLiteral("endSeconds")) ||
      legacy_marker.contains(QStringLiteral("clipIds"))) {
    return fail_workflow("legacy point review notes must keep their original serialized JSON shape");
  }

  TimelineDocumentController anchored_review_controller;
  anchored_review_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Anchored Review Notes")},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 180},
      {QStringLiteral("frameRate"), 24.0},
  });
  anchored_review_controller.addClipFromGallery(
      QStringLiteral("photo:anchored-review"), photo_still,
      QStringLiteral("Reviewable Clip"), 1.5, photo_still);
  const auto anchored_clip_id = anchored_review_controller.clips().isEmpty()
      ? QString{}
      : anchored_review_controller.clips().first().toMap()
            .value(QStringLiteral("clipId")).toString();
  const auto anchored_note_id = QStringLiteral("a6636b3a-b205-4dd2-9ae6-21c80ca8724f");
  const auto range_note_id = anchored_review_controller.addAnchoredReviewNote(
      QStringLiteral("Check transition"), QStringLiteral("Review the pacing across this cut."),
      4, 24, QStringList{anchored_clip_id}, QStringLiteral("Editorial"),
      QStringLiteral("open"), anchored_note_id);
  const auto frame_note_id = anchored_review_controller.addAnchoredReviewNote(
      QStringLiteral("Check frame"), QStringLiteral("Watch the title edge."),
      30, 30);
  if (anchored_clip_id.isEmpty() || range_note_id != anchored_note_id ||
      frame_note_id.isEmpty() || anchored_review_controller.reviewNotes().size() != 2) {
    return fail_workflow("anchored note creation should accept stable frame/range anchors and optional clip IDs");
  }
  const auto range_before_update = anchored_review_controller.reviewNotes().first().toMap();
  if (range_before_update.value(QStringLiteral("anchorType")).toString() != QStringLiteral("range") ||
      range_before_update.value(QStringLiteral("frame")).toLongLong() != 4 ||
      range_before_update.value(QStringLiteral("endFrame")).toLongLong() != 24 ||
      range_before_update.value(QStringLiteral("clipIds")).toStringList() != QStringList{anchored_clip_id} ||
      range_before_update.value(QStringLiteral("resolved")).toBool()) {
    return fail_workflow("review rows should expose half-open frame ranges, clip IDs and open state");
  }
  const auto frame_note = anchored_review_controller.reviewNotes().last().toMap();
  if (frame_note.value(QStringLiteral("anchorType")).toString() != QStringLiteral("frame") ||
      frame_note.value(QStringLiteral("frame")).toLongLong() != 30 ||
      frame_note.value(QStringLiteral("endFrame")).toLongLong() != 30 ||
      !frame_note.value(QStringLiteral("clipIds")).toStringList().isEmpty()) {
    return fail_workflow("point review notes should expose one frame and an empty optional clip list");
  }
  if (anchored_review_controller.addAnchoredReviewNote(
          QStringLiteral("Check transition"), QStringLiteral("Review the pacing across this cut."),
          4, 24, QStringList{anchored_clip_id}, QStringLiteral("Editorial"),
          QStringLiteral("open"), anchored_note_id) != anchored_note_id ||
      anchored_review_controller.reviewNotes().size() != 2 ||
      !anchored_review_controller.addAnchoredReviewNote(
          QStringLiteral("Changed body"), QStringLiteral("Conflicting replay."), 4, 24,
          QStringList{anchored_clip_id}, QStringLiteral("Editorial"), QStringLiteral("open"),
          anchored_note_id).isEmpty() ||
      !anchored_review_controller.addAnchoredReviewNote(
          QStringLiteral("Invalid target"), QStringLiteral("A missing clip cannot be attached."),
          4, 24, QStringList{QStringLiteral("missing-clip-id")}).isEmpty() ||
      anchored_review_controller.reviewNotes().size() != 2) {
    return fail_workflow("stable note IDs should make exact create replay idempotent and reject conflicting anchors");
  }
  if (!anchored_review_controller.updateAnchoredReviewNote(
          range_note_id, QStringLiteral("Updated transition"),
          QStringLiteral("Check the handoff and final frame."), 6, 30,
          QStringList{anchored_clip_id}, QStringLiteral("Picture Editor"),
          QStringLiteral("changes-requested"), true)) {
    return fail_workflow("one review-note update should replace text, range, assignment and resolution together");
  }
  const auto anchored_markers_json =
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(anchored_review_controller)
          .value(QStringLiteral("markers")).toArray();
  QJsonObject persisted_range_note;
  for (const auto& marker_value : anchored_markers_json) {
    const auto marker = marker_value.toObject();
    if (marker.value(QStringLiteral("markerId")).toString() == range_note_id) {
      persisted_range_note = marker;
      break;
    }
  }
  if (!persisted_range_note.contains(QStringLiteral("endSeconds")) ||
      persisted_range_note.value(QStringLiteral("clipIds")).toArray() !=
          QJsonArray{QJsonValue(anchored_clip_id)}) {
    return fail_workflow("only explicitly anchored review notes should add range and clip fields to document JSON");
  }
  const auto updated_range = anchored_review_controller.reviewNotes().first().toMap();
  if (updated_range.value(QStringLiteral("frame")).toLongLong() != 6 ||
      updated_range.value(QStringLiteral("endFrame")).toLongLong() != 30 ||
      !updated_range.value(QStringLiteral("resolved")).toBool() ||
      updated_range.value(QStringLiteral("approvalState")).toString() != QStringLiteral("changes-requested")) {
    return fail_workflow("review-note edits should retain exact anchor frames and resolved state");
  }
  const auto anchored_review_package_path =
      QDir(workspace_path).filePath(QStringLiteral("anchored-review-notes.json"));
  if (anchored_review_controller.exportReviewNotes(anchored_review_package_path) !=
      anchored_review_package_path) {
    return fail_workflow("anchored review notes should remain exportable through the existing review package");
  }
  QFile anchored_review_package_file(anchored_review_package_path);
  if (!anchored_review_package_file.open(QIODevice::ReadOnly)) {
    return fail_workflow("the anchored review package should be readable");
  }
  const auto anchored_review_package =
      QJsonDocument::fromJson(anchored_review_package_file.readAll()).object();
  const auto anchored_package_notes =
      anchored_review_package.value(QStringLiteral("notes")).toArray();
  const auto range_package_note = anchored_package_notes.isEmpty()
      ? QJsonObject{}
      : anchored_package_notes.first().toObject();
  if (range_package_note.value(QStringLiteral("exchangeId")).toString() != range_note_id ||
      range_package_note.value(QStringLiteral("frame")).toInt() != 6 ||
      range_package_note.value(QStringLiteral("endFrame")).toInt() != 30 ||
      range_package_note.value(QStringLiteral("clipIds")).toArray() !=
          QJsonArray{QJsonValue(anchored_clip_id)} ||
      !range_package_note.value(QStringLiteral("resolved")).toBool()) {
    return fail_workflow("review package export should retain stable IDs, frame ranges, clip links and resolution");
  }
  anchored_review_controller.undo();
  if (anchored_review_controller.reviewNotes().first().toMap() != range_before_update) {
    return fail_workflow("one undo should restore every review-note field and anchor exactly");
  }
  anchored_review_controller.redo();
  if (anchored_review_controller.reviewNotes().first().toMap() != updated_range ||
      !anchored_review_controller.resolveTimelineReviewNote(range_note_id, false) ||
      anchored_review_controller.reviewNotes().first().toMap().value(QStringLiteral("resolved")).toBool()) {
    return fail_workflow("review-note redo and explicit reopen should preserve marker identity");
  }
  anchored_review_controller.undo();
  if (!anchored_review_controller.reviewNotes().first().toMap().value(QStringLiteral("resolved")).toBool() ||
      !anchored_review_controller.saveTimeline(true)) {
    return fail_workflow("review resolution should be undoable and saveable");
  }
  TimelineDocumentController reopened_anchored_review;
  if (!reopened_anchored_review.loadTimeline(anchored_review_controller.savedDocumentPath()) ||
      reopened_anchored_review.reviewNotes().size() != 2 ||
      reopened_anchored_review.reviewNotes().first().toMap() != updated_range) {
    return fail_workflow("review frame/range anchors, clip IDs, resolution and stable IDs should survive save/reopen");
  }
  return true;
}
