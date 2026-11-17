#include "timeline_document_controller.h"

#include <QApplication>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>

using photo_editor::desktop::TimelineDocumentController;

int main(int argc, char** argv) {
  QTemporaryDir temp;
  if (!temp.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", temp.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/caption_integrity_fixture");
  QApplication app(argc, argv);
  const auto require = [](bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
  };
  const auto path = temp.filePath("caption-base.png");
  QImage image(64, 64, QImage::Format_RGB32);
  image.fill(Qt::black);
  if (!image.save(path)) return 1;
  TimelineDocumentController document;
  document.createTimelineWithSettings({{"timelineName", "Unicode captions"},
                                      {"width", 64}, {"height", 64}, {"frameRate", 30.0}});
  document.addClipFromGallery("photo:caption-integrity", path, "Picture", 2.0, {});
  const auto text = QString::fromUtf8("e\xcc\x81").repeated(30);
  document.generateCaptionsFromTranscript(text);
  const auto before = document.deterministicDocumentHash();
  const auto result = document.validateCaptions("srt");
  if (!require(result.value("cueCount").toInt() == 1, "Caption fixture must contain one real generated cue") ||
      !require(document.deterministicDocumentHash() == before, "Caption validation must be read-only")) return 1;
  for (const auto& issue : result.value("issues").toList()) {
    const auto category = issue.toMap().value("category").toString();
    if (!require(category != "overflow" && category != "reading-speed",
                 "30 visible characters over two seconds must not be treated as 60 UTF-16 units")) return 1;
  }
  const auto preview = document.previewCaptionSegmentation({{"maxCharsPerLine", 42},
                                                           {"maxReadingSpeedCps", 20.0},
                                                           {"minimumDurationSeconds", 0.4}});
  const auto cues = preview.value("cues").toList();
  if (!require(cues.size() == 1, "Unicode segmentation must preserve the cue") ||
      !require(cues.first().toMap().value("endSeconds").toDouble() <= 2.0001,
               "Combining accents must not double the required reading duration") ||
      !require(std::abs(cues.first().toMap().value("readingSpeedCps").toDouble() - 15.0) < 0.001,
               "Segmentation must report reading speed using the same visible-character count")) return 1;
  const auto clips = document.clips();
  for (qsizetype index = 0; index < clips.size(); ++index) {
    if (clips[index].toMap().value("mediaType").toString() == "caption") {
      document.selectClip(static_cast<int>(index));
      break;
    }
  }
  document.updateSelectedTitle({{"text", QString::fromUtf8("first\xe2\x80\xa8second\xe2\x80\xa8third")}});
  const auto multiline = document.validateCaptions("srt");
  bool overflow = false;
  bool format = false;
  for (const auto& issue : multiline.value("issues").toList()) {
    const auto category = issue.toMap().value("category").toString();
    overflow = overflow || category == "overflow";
    format = format || category == "export-format";
  }
  if (!require(overflow && format,
               "Unicode line separators must receive the same layout and sidecar checks as newlines")) {
    return 1;
  }
  const auto multiline_preview = document.previewCaptionSegmentation({
      {"maxCharsPerLine", 42}, {"maxReadingSpeedCps", 20.0},
      {"minimumDurationSeconds", 0.4}, {"semanticLineBreaks", false}});
  const auto multiline_cues = multiline_preview.value("cues").toList();
  if (!require(multiline_cues.size() == 1 &&
                   multiline_cues.first().toMap().value("text").toString() ==
                       QStringLiteral("first\nsecond\nthird"),
               "Caption segmentation preview must retain authored line breaks when all words remain visible")) {
    return 1;
  }
  if (!require(document.exportCaptionSidecar("srt"),
               "A multiline caption must remain exportable after validation")) {
    return 1;
  }
  QDirIterator multiline_sidecars(temp.path(), {"*.srt"}, QDir::Files,
                                  QDirIterator::Subdirectories);
  QString multiline_export;
  if (multiline_sidecars.hasNext()) {
    QFile output(multiline_sidecars.next());
    if (output.open(QIODevice::ReadOnly)) multiline_export = QString::fromUtf8(output.readAll());
  }
  if (!require(multiline_export.contains(QStringLiteral("first\nsecond\nthird")),
               "SRT export must normalize Unicode line separators without flattening caption layout")) {
    return 1;
  }

  TimelineDocumentController partial_linebreak_document;
  partial_linebreak_document.createTimelineWithSettings({{"timelineName", "Trimmed caption line breaks"},
                                                          {"width", 64}, {"height", 64},
                                                          {"frameRate", 30.0}});
  partial_linebreak_document.addClipFromGallery("photo:caption-line-break-trim", path,
                                                "Four line caption", 4.0, {});
  partial_linebreak_document.generateCaptionsFromTranscript(QStringLiteral("one two three four"));
  int partial_linebreak_index = -1;
  for (qsizetype index = 0; index < partial_linebreak_document.clips().size(); ++index) {
    if (partial_linebreak_document.clips().at(index).toMap().value("mediaType").toString() ==
        QStringLiteral("caption")) {
      partial_linebreak_index = static_cast<int>(index);
      break;
    }
  }
  if (!require(partial_linebreak_index >= 0,
               "The partial-linebreak fixture must create an editable caption cue")) return 1;
  partial_linebreak_document.selectClip(partial_linebreak_index);
  partial_linebreak_document.updateSelectedTitle(
      {{"text", QStringLiteral("one\ntwo\nthree\nfour")}});
  partial_linebreak_document.trimSelectedClipStartToSequenceSeconds(1.0, false);
  const auto partial_preview = partial_linebreak_document.previewCaptionSegmentation({
      {"maxCharsPerLine", 42}, {"maxLines", 3}, {"maxReadingSpeedCps", 40.0},
      {"minimumDurationSeconds", 0.4}, {"semanticLineBreaks", false}});
  const auto partial_cues = partial_preview.value("cues").toList();
  bool partial_overflow = false;
  for (const auto& issue : partial_linebreak_document.validateCaptions("srt").value("issues").toList()) {
    partial_overflow = partial_overflow ||
        (issue.toMap().value("category").toString() == QStringLiteral("overflow") &&
         issue.toMap().value("message").toString().contains(QStringLiteral("3 lines")));
  }
  if (!require(partial_cues.size() == 1 &&
                   partial_cues.first().toMap().value("text").toString() ==
                       QStringLiteral("two\nthree\nfour") &&
                   partial_overflow,
               "A head trim must remove hidden words without flattening the remaining authored lines")) {
    return 1;
  }
  if (!require(partial_linebreak_document.exportCaptionSidecar("srt"),
               "A partially trimmed multiline caption must remain exportable")) return 1;
  QDirIterator partial_sidecars(temp.path(), {"*.srt"}, QDir::Files,
                                QDirIterator::Subdirectories);
  bool partial_export_preserved_lines = false;
  while (partial_sidecars.hasNext()) {
    QFile output(partial_sidecars.next());
    if (output.open(QIODevice::ReadOnly)) {
      partial_export_preserved_lines = partial_export_preserved_lines ||
          QString::fromUtf8(output.readAll()).contains(QStringLiteral("two\nthree\nfour"));
    }
  }
  if (!require(partial_export_preserved_lines,
               "SRT export must preserve authored line breaks among words that remain visible")) return 1;

  TimelineDocumentController trim_document;
  trim_document.createTimelineWithSettings({{"timelineName", "Trim-safe caption words"},
                                           {"width", 64}, {"height", 64}, {"frameRate", 30.0}});
  trim_document.addClipFromGallery("photo:caption-trim", path, "Picture", 2.0, {});
  trim_document.generateCaptionsFromTranscript("um two three four");
  const auto caption_index = [&trim_document]() {
    const auto values = trim_document.clips();
    for (qsizetype index = 0; index < values.size(); ++index) {
      if (values.at(index).toMap().value("mediaType").toString() == "caption") {
        return static_cast<int>(index);
      }
    }
    return -1;
  };
  const auto read_export = [&temp, &trim_document]() {
    if (!trim_document.exportCaptionSidecar("srt")) return QString{};
    QDirIterator files(temp.path(), {"*.srt"}, QDir::Files, QDirIterator::Subdirectories);
    if (!files.hasNext()) return QString{};
    QFile output(files.next());
    if (!output.open(QIODevice::ReadOnly)) return QString{};
    return QString::fromUtf8(output.readAll());
  };
  const auto trim_index = caption_index();
  if (!require(trim_index >= 0, "Transcript generation must create an editable caption cue")) return 1;
  trim_document.selectClip(trim_index);
  const auto authored_clip = trim_document.clips().at(trim_index).toMap();
  const auto caption_clip_id = authored_clip.value("clipId").toString();
  const auto authored_words = authored_clip.value("captionWords").toList();
  if (!require(authored_words.size() == 4, "Caption fixture must retain all four authored word times")) return 1;

  trim_document.trimSelectedClipStartToSequenceSeconds(1.0, false);
  const auto trimmed_caption = trim_document.clips().at(trim_index).toMap();
  if (!require(std::abs(trimmed_caption.value("captionWordTimeOffsetSeconds").toDouble() - 1.0) < 0.001,
               "Trimming the caption head must shift its playback clock without changing authored words") ||
      !require(trimmed_caption.value("captionWords").toList() == authored_words,
               "A head trim must retain the exact authored words so extending the trim can restore them")) return 1;
  if (!require(trim_document.saveTimeline(), "The trimmed caption clock must be saveable")) return 1;
  TimelineDocumentController reopened_trim_document;
  if (!require(reopened_trim_document.loadTimeline(trim_document.savedDocumentPath()),
               "The trimmed caption document must reopen")) return 1;
  QVariantMap reopened_caption;
  for (const auto& value : reopened_trim_document.clips()) {
    const auto candidate = value.toMap();
    if (candidate.value("clipId").toString() == caption_clip_id) {
      reopened_caption = candidate;
      break;
    }
  }
  if (!require(std::abs(reopened_caption.value("captionWordTimeOffsetSeconds").toDouble() - 1.0) < 0.001 &&
                   reopened_caption.value("captionWords").toList() == authored_words,
               "Save and reopen must preserve the trim-safe word clock and original word timings")) return 1;
  const auto trimmed_segmentation = trim_document.previewCaptionSegmentation(
      {{"maxCharsPerLine", 42}, {"maxReadingSpeedCps", 40.0},
       {"minimumDurationSeconds", 0.4}, {"semanticLineBreaks", false}});
  const auto trimmed_cues = trimmed_segmentation.value("cues").toList();
  const auto trimmed_transcript = trim_document.transcriptSegments();
  const auto trimmed_transcript_segment = trimmed_transcript.isEmpty()
                                             ? QVariantMap{}
                                             : trimmed_transcript.first().toMap();
  const auto trimmed_transcript_words = trimmed_transcript_segment.value("words").toList();
  if (!require(!trimmed_cues.isEmpty(), "Trimmed visible words must remain available to caption export") ||
      !require(trimmed_cues.first().toMap().value("text").toString() == "three four",
               "Caption segmentation must use visible sequence timing after a head trim") ||
      !require(trimmed_transcript_segment.value("text").toString() == "three four" &&
                   trimmed_transcript_words.size() == 2 &&
                   trimmed_transcript_words.first().toMap().value("index").toInt() == 2 &&
                   std::abs(trimmed_transcript_segment.value("startSeconds").toDouble() - 1.0) < 0.001,
               "Transcript navigation must show only visible words at their sequence-local times")) return 1;
  if (!require(trim_document.seekTranscriptWord(caption_clip_id, 2, "program") &&
                   std::abs(trim_document.sequencePlayheadSeconds() - 1.0) < 0.001,
               "Transcript navigation must seek a retained source-local word to its visible timeline time")) return 1;
  bool trimmed_filler_visible = false;
  for (const auto& suggestion : trim_document.transcriptCleanupSuggestions()) {
    trimmed_filler_visible = trimmed_filler_visible ||
        suggestion.toMap().value("label").toString().contains(QStringLiteral("um"));
  }
  if (!require(!trimmed_filler_visible,
               "Transcript cleanup must not propose removing a fully head-trimmed word")) return 1;
  const auto trimmed_export = read_export();
  if (!require(trimmed_export.contains("00:00:01,000 --> 00:00:02,000") &&
                   trimmed_export.contains("three four") && !trimmed_export.contains("one two"),
               "SRT export must use the trimmed word interval and omit fully trimmed words")) return 1;

  trim_document.trimSelectedClipStartToSequenceSeconds(0.0, false);
  const auto extended_caption = trim_document.clips().at(trim_index).toMap();
  if (!require(std::abs(extended_caption.value("captionWordTimeOffsetSeconds").toDouble()) < 0.001 &&
                   extended_caption.value("captionWords").toList() == authored_words,
               "Extending the head trim to zero must restore the exact original word schedule")) return 1;
  bool restored_filler_visible = false;
  for (const auto& suggestion : trim_document.transcriptCleanupSuggestions()) {
    restored_filler_visible = restored_filler_visible ||
        suggestion.toMap().value("label").toString().contains(QStringLiteral("um"));
  }
  if (!require(restored_filler_visible,
               "Extending the trim must restore the original word to transcript cleanup")) return 1;
  trim_document.undo();
  const auto undone_caption = trim_document.clips().at(trim_index).toMap();
  if (!require(std::abs(undone_caption.value("captionWordTimeOffsetSeconds").toDouble() - 1.0) < 0.001 &&
                   undone_caption.value("captionWords").toList() == authored_words,
               "Undo must restore the shifted playback clock without losing words")) return 1;
  const auto undo_export = read_export();
  if (!require(undo_export.contains("00:00:01,000 --> 00:00:02,000") &&
                   undo_export.contains("three four"),
               "Caption export after Undo must preserve the trimmed timing")) return 1;
  trim_document.redo();
  const auto redone_caption = trim_document.clips().at(trim_index).toMap();
  const auto restored_export = read_export();
  if (!require(std::abs(redone_caption.value("captionWordTimeOffsetSeconds").toDouble()) < 0.001 &&
                   redone_caption.value("captionWords").toList() == authored_words &&
                   restored_export.contains("00:00:00,000 --> 00:00:02,000") &&
                   restored_export.contains("um two three four"),
               "Redo and SRT export must restore the original caption word timing")) return 1;

  TimelineDocumentController overwrite_document;
  overwrite_document.createTimelineWithSettings({{"timelineName", "Overwrite caption timing"},
                                                  {"width", 64}, {"height", 64},
                                                  {"frameRate", 30.0}});
  overwrite_document.addClipFromGallery("photo:overwrite-caption-base", path,
                                        "Ten second base", 10.0, {});
  overwrite_document.generateCaptionsFromTranscript(QStringLiteral("hello"));
  const auto original_cc_tracks = overwrite_document.sequenceTracks();
  int original_cc_row = -1;
  QString original_cc_clip_id;
  for (int row = 0; row < original_cc_tracks.size(); ++row) {
    const auto track = original_cc_tracks.at(row).toMap();
    if (track.value("trackRole").toString() == QStringLiteral("caption")) {
      original_cc_row = row;
      const auto clips_on_cc = track.value("clips").toList();
      if (!require(clips_on_cc.size() == 1 &&
                       std::abs(clips_on_cc.first().toMap().value("durationSeconds").toDouble() - 10.0) < 0.001,
                   "The overwrite fixture must start with one 0–10 second CC cue")) return 1;
      original_cc_clip_id = clips_on_cc.first().toMap().value("clipId").toString();
      break;
    }
  }
  if (!require(original_cc_row >= 0, "Caption generation must create the reachable CC track")) return 1;
  QVariantList original_cc_words;
  auto original_flattened_count = 0;
  for (const auto& track_value : original_cc_tracks)
    original_flattened_count += track_value.toMap().value("clips").toList().size();
  for (int index = 0; index < original_flattened_count; ++index) {
    const auto detail = overwrite_document.timelineClipDetails(index);
    if (detail.value("clipId").toString() == original_cc_clip_id) {
      original_cc_words = detail.value("captionWords").toList();
      break;
    }
  }
  if (!require(!original_cc_clip_id.isEmpty() && !original_cc_words.isEmpty(),
               "The original CC fixture must expose its authored source-word schedule")) return 1;
  if (!require(overwrite_document.addIndependentVideoTrack(),
               "The overwrite fixture needs an independent source video lane")) return 1;
  overwrite_document.addClipFromGallery("photo:overwrite-caption-source", path,
                                        "Two second overwrite", 2.0, {});
  const auto overwrite_source_id = overwrite_document.currentClip().value("clipId").toString();
  int cc_row = -1;
  const auto tracks_before_overwrite = overwrite_document.sequenceTracks();
  for (int row = 0; row < tracks_before_overwrite.size(); ++row) {
    if (tracks_before_overwrite.at(row).toMap().value("trackRole").toString() ==
        QStringLiteral("caption")) {
      cc_row = row;
      break;
    }
  }
  if (!require(!overwrite_source_id.isEmpty() && cc_row >= 0 &&
                   overwrite_document.moveSelectedClipToTrackRowWithMode(
                       cc_row, 4.0, QStringLiteral("overwrite"), false),
               "Moving a two second source clip onto CC in overwrite mode must be reachable")) return 1;
  QVariantMap leading_caption;
  QVariantMap trailing_caption;
  for (const auto& track_value : overwrite_document.sequenceTracks()) {
    const auto track = track_value.toMap();
    if (track.value("trackRole").toString() != QStringLiteral("caption")) continue;
    for (const auto& clip_value : track.value("clips").toList()) {
      const auto clip = clip_value.toMap();
      if (clip.value("mediaType").toString() != QStringLiteral("caption")) continue;
      const auto start = clip.value("startSeconds").toDouble();
      if (std::abs(start) < 0.001) leading_caption = clip;
      if (std::abs(start - 6.0) < 0.001) trailing_caption = clip;
    }
  }
  auto flattened_after_overwrite = 0;
  for (const auto& track_value : overwrite_document.sequenceTracks())
    flattened_after_overwrite += track_value.toMap().value("clips").toList().size();
  for (int index = 0; index < flattened_after_overwrite; ++index) {
    const auto detail = overwrite_document.timelineClipDetails(index);
    if (detail.value("clipId").toString() == trailing_caption.value("clipId").toString()) {
      trailing_caption = detail;
      break;
    }
  }
  if (!require(!leading_caption.isEmpty() && !trailing_caption.isEmpty() &&
                   std::abs(leading_caption.value("trimEndSeconds").toDouble() - 4.0) < 0.001 &&
                   std::abs(trailing_caption.value("startSeconds").toDouble() - 6.0) < 0.001 &&
                   std::abs(trailing_caption.value("trimStartSeconds").toDouble() - 6.0) < 0.001 &&
                   std::abs(trailing_caption.value("captionWordTimeOffsetSeconds").toDouble() - 6.0) < 0.001 &&
                   trailing_caption.value("captionWords").toList() == original_cc_words,
               "A CC overwrite must retain the trailing source-word schedule and advance its local playback offset")) return 1;
  const auto overwrite_transcript = overwrite_document.transcriptSegments();
  const auto trailing_segment = overwrite_transcript.size() >= 2
                                    ? overwrite_transcript.last().toMap()
                                    : QVariantMap{};
  if (!require(trailing_segment.value("segmentId").toString() ==
                   trailing_caption.value("clipId").toString() &&
                   trailing_segment.value("text").toString() == QStringLiteral("hello") &&
                   std::abs(trailing_segment.value("startSeconds").toDouble() - 6.0) < 0.001 &&
                   overwrite_document.seekTranscriptWord(
                       trailing_caption.value("clipId").toString(), 0, QStringLiteral("program")) &&
                   std::abs(overwrite_document.sequencePlayheadSeconds() - 6.0) < 0.001,
               "Transcript text and Program navigation must follow the trailing CC cue after overwrite")) return 1;
  if (!require(overwrite_document.exportCaptionSidecar(QStringLiteral("srt")),
               "The overwritten caption track must remain exportable")) return 1;
  bool trailing_caption_exported = false;
  QDirIterator overwritten_sidecars(temp.path(), {"*.srt"}, QDir::Files,
                                    QDirIterator::Subdirectories);
  while (overwritten_sidecars.hasNext()) {
    QFile output(overwritten_sidecars.next());
    if (!output.open(QIODevice::ReadOnly)) continue;
    const auto contents = QString::fromUtf8(output.readAll());
    trailing_caption_exported = trailing_caption_exported ||
        (contents.contains("00:00:06,000 --> 00:00:10,000") && contents.contains("hello"));
  }
  if (!require(trailing_caption_exported,
               "SRT export must retain the trailing CC cue at its original visible sequence time")) return 1;
  return 0;
}
