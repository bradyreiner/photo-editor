#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>

#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"

namespace {

using photo_editor::desktop::TimelineDocumentController;
using photo_editor::desktop::TimelinePlaybackController;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool nearly_equal(double left, double right, double tolerance = 0.0001) {
  return std::abs(left - right) <= tolerance;
}

QString touch_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return {};
  }
  file.write("placeholder");
  file.close();
  return path;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QApplication app(argc, argv);

  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    return fail("temporary workspace should be created");
  }

  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", workspace.filePath(QStringLiteral("app-data")).toUtf8());

  const auto first_video = touch_file(workspace.filePath(QStringLiteral("playback-01.mp4")));
  const auto second_video = touch_file(workspace.filePath(QStringLiteral("playback-02.mp4")));
  if (first_video.isEmpty() || second_video.isEmpty()) {
    return fail("playback placeholder files should be created");
  }

  TimelineDocumentController document;
  document.createTimeline(QStringLiteral("Playback Timeline"));
  document.addClipFromGallery(QStringLiteral("video:1"), first_video, QStringLiteral("First"), 4.0, QStringLiteral("poster://one"));
  document.addClipFromGallery(QStringLiteral("video:2"), second_video, QStringLiteral("Second"), 5.0, QStringLiteral("poster://two"));
  document.selectClip(0);

  TimelinePlaybackController playback(&document);
  if (!playback.hasVideoSource()) {
    return fail("native playback controller should expose the current clip source");
  }

  playback.seekSequenceSeconds(2.5);
  if (document.currentIndex() != 0 ||
      !nearly_equal(document.sequencePlayheadSeconds(), 2.5) ||
      !nearly_equal(document.currentClipPlayheadSeconds(), 2.5)) {
    return fail("sequence seeking should update native timeline playheads through playback controller");
  }

  playback.play();
  if (!document.sequencePlaying()) {
    return fail("playback controller should start native sequence playback");
  }

  playback.pause();
  if (document.sequencePlaying()) {
    return fail("playback controller should pause native sequence playback");
  }

  playback.seekSequenceSeconds(4.25);
  if (document.currentIndex() != 1 ||
      !nearly_equal(document.sequencePlayheadSeconds(), 4.25) ||
      !nearly_equal(document.currentClipPlayheadSeconds(), 0.25)) {
    return fail("sequence seeks past the first clip should remap onto the next clip");
  }

  playback.stepFrames(1);
  if (document.sequencePlaying()) {
    return fail("frame stepping through the playback controller should keep playback paused");
  }
  if (document.currentClipPlayheadSeconds() <= 0.25) {
    return fail("frame stepping should advance the current clip playhead");
  }

  document.addVideoTrack();
  document.selectClip(1);
  document.moveSelectedClipToTrackRow(2, 1.0);
  playback.seekSequenceSeconds(1.5);
  if (document.currentClip().value(QStringLiteral("trackLabel")).toString() != QStringLiteral("V2") ||
      !nearly_equal(document.currentClipPlayheadSeconds(), 0.5)) {
    return fail("playback seeking should prefer the higher active video track at the current sequence time");
  }

  return EXIT_SUCCESS;
}
