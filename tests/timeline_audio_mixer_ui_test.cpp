#include "timeline_document_controller.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QMetaObject>
#include <QRectF>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointer>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUrl>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

using photo_editor::desktop::TimelineDocumentController;

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

void settle(int milliseconds = 140) {
  const int steps = std::max(1, milliseconds / 5);
  for (int index = 0; index < steps; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

QObject* find_object(QObject* root, const QString& object_name) {
  if (root == nullptr) {
    return nullptr;
  }
  if (root->objectName() == object_name) {
    return root;
  }
  for (auto* child : root->children()) {
    if (auto* found = find_object(child, object_name); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

QQuickItem* find_item(QQuickItem* root, const QString& object_name) {
  if (root == nullptr) {
    return nullptr;
  }
  if (root->objectName() == object_name) {
    return root;
  }
  for (auto* child : root->childItems()) {
    if (auto* found = find_item(child, object_name); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

QQuickItem* find_item_with_text(QQuickItem* root, const QString& text) {
  if (root == nullptr) {
    return nullptr;
  }
  for (auto* child : root->childItems()) {
    if (child->property("text").isValid() &&
        child->property("text").toString() == text) {
      return child;
    }
    if (auto* found = find_item_with_text(child, text); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

QQuickItem* find_window_item(QQuickWindow* window, const QString& object_name) {
  if (window == nullptr) {
    return nullptr;
  }
  if (auto* item = qobject_cast<QQuickItem*>(find_object(window, object_name));
      item != nullptr) {
    return item;
  }
  return find_item(window->contentItem(), object_name);
}

bool click_item(QQuickWindow* window, QQuickItem* item) {
  if (window == nullptr || item == nullptr || !item->isVisible()) {
    return false;
  }
  const QPoint position =
      item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0)).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, position);
  settle(40);
  return true;
}

bool fits_within(QQuickItem* item, const QRectF& scene_bounds) {
  return item != nullptr &&
         scene_bounds.adjusted(-1.0, -1.0, 1.0, 1.0)
             .contains(item->mapRectToScene(item->boundingRect()));
}

bool item_text_fits(QQuickItem* item) {
  return item != nullptr && item->property("contentWidth").isValid() &&
         item->property("contentWidth").toDouble() <= item->width() + 1.0;
}

QVariantMap track_with_id(const TimelineDocumentController& controller,
                          const QString& track_id) {
  const QVariantList tracks = controller.sequenceTracks();
  for (const QVariant& value : tracks) {
    const QVariantMap track = value.toMap();
    if (track.value(QStringLiteral("trackId")).toString() == track_id) {
      return track;
    }
  }
  return {};
}

QString write_silent_stereo_wav(const QString& path) {
  constexpr quint32 data_size = 19200;
  QByteArray wav;
  QDataStream stream(&wav, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.writeRawData("RIFF", 4);
  stream << quint32{36U + data_size};
  stream.writeRawData("WAVEfmt ", 8);
  stream << quint32{16} << quint16{1} << quint16{2} << quint32{48000}
         << quint32{192000} << quint16{4} << quint16{16};
  stream.writeRawData("data", 4);
  stream << data_size;
  const QByteArray samples(static_cast<int>(data_size), '\0');
  stream.writeRawData(samples.constData(), samples.size());
  if (stream.status() != QDataStream::Ok) {
    return {};
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(wav) != wav.size()) {
    return {};
  }
  return path;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QApplication app(argc, argv);

  TimelineDocumentController controller;
  controller.createTimeline(QStringLiteral("Audio Mixer UI"));
  if (!controller.addIndependentAudioTrack(QStringLiteral("dialogue")) ||
      !controller.addIndependentAudioTrack(QStringLiteral("music"))) {
    return fail("The real controller should create two editable audio tracks");
  }

  QString first_audio_track_id;
  QString second_audio_track_id;
  int second_audio_track_index = -1;
  for (const QVariant& value : controller.sequenceTracks()) {
    const QVariantMap track = value.toMap();
    if (track.value(QStringLiteral("trackKind")).toString() !=
        QStringLiteral("audio")) {
      continue;
    }
    if (first_audio_track_id.isEmpty()) {
      first_audio_track_id = track.value(QStringLiteral("trackId")).toString();
    } else {
      second_audio_track_id = track.value(QStringLiteral("trackId")).toString();
      second_audio_track_index = track.value(QStringLiteral("trackIndex")).toInt();
      break;
    }
  }
  if (first_audio_track_id.isEmpty() || second_audio_track_id.isEmpty() ||
      second_audio_track_index < 0) {
    return fail("The controller's audio track projection should provide stable track IDs and rows");
  }

  QQmlPropertyMap app_shell;
  app_shell.insert(QStringLiteral("interfaceScale"), 1.0);
  app_shell.insert(QStringLiteral("interfaceDensity"), QStringLiteral("balanced"));
  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), &app_shell);
  engine.rootContext()->setContextProperty(QStringLiteral("testDocument"), &controller);
  QQmlComponent component(&engine);
  const QByteArray qml =
      R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"

ApplicationWindow {
    id: fixture
    objectName: "timelineAudioMixerFixture"
    width: 760
    height: 710
    property int dockedMixerHeight: 710
    visible: true
    color: "#111315"

    Theme { id: theme }
    Item {
        id: audioDockHost
        objectName: "timelineAudioDockHost"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.min(parent.height, fixture.dockedMixerHeight)
    }
    QtObject {
        id: playback
        property int playCalls: 0
        property int pauseCalls: 0
        function play() { playCalls += 1 }
        function pause() { pauseCalls += 1 }
    }
    QtObject {
        id: recorder
        property var inputDevices: []
        property string selectedInputDeviceId: ""
        property string state: "idle"
        property int preRollRemainingMs: 0
        property int recordedDurationMs: 0
        property string errorString: ""
        property string recoverablePath: ""
        property string captureTimelineIdentity: ""
        property string captureDocumentPath: ""
        signal recordingFinished(string filePath, int durationMs)
        signal recordingCancelled(string recoverablePath)
        signal recordingFailed(string detail, string recoverablePath)
        function refreshInputDevices() {}
        function startRecording(documentPath, preRollMs, timelineIdentity) { return false }
        function stopRecording() {}
        function cancelRecording() {}
    }

    TimelineAudioMixer {
        id: mixer
        objectName: "testedTimelineAudioMixer"
        docked: true
        workspaceHost: audioDockHost
        theme: theme
        timelineDocument: testDocument
        timelinePlayback: playback
        voiceoverRecorder: recorder
        currentClip: testDocument.currentClip
        audioTracks: testDocument.sequenceTracks.filter(function(track) {
            return String(track.trackKind || "") === "audio"
        })
        trackMeterState: ({})
        masterMeter: ({})
        momentaryLufs: -60
        shortTermLufs: -60
        reducedMotion: true
        Component.onCompleted: open()
    }
}
)qml";
  component.setData(qml, QUrl::fromLocalFile(
                            QStringLiteral(PHOTO_EDITOR_SOURCE_DIR
                                           "/tests/timeline_audio_mixer_ui_test.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("TimelineAudioMixer should load as a retained dock with its real data API");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  if (window == nullptr) {
    return fail("The mixer fixture should create an application window");
  }
  window->show();
  settle();

  auto* mixer = find_object(window, QStringLiteral("testedTimelineAudioMixer"));
  auto* selected_clip_strip =
      find_object(window, QStringLiteral("timelineSelectedClipMixerStrip"));
  const QString select_object_name =
      QStringLiteral("timelineMixerTrackSelect_") + second_audio_track_id;
  auto* second_track_select =
      find_item(window->contentItem(), select_object_name);
  const QString fader_object_name =
      QStringLiteral("timelineTrackGainFader_") + second_audio_track_id;
  auto* second_track_fader = find_item(window->contentItem(), fader_object_name);
  auto* second_track_handle = find_item(
      window->contentItem(),
      QStringLiteral("timelineTrackGainHandle_") + second_audio_track_id);
  if (mixer == nullptr || second_track_select == nullptr ||
      second_track_fader == nullptr || second_track_handle == nullptr) {
    return fail("The visible mixer should expose track selection and a vertical fader for each audio track");
  }
  if (selected_clip_strip == nullptr ||
      selected_clip_strip->property("visible").toBool()) {
    return fail("Clip gain and fades should stay out of the track strips when no clip is selected");
  }
  if (second_track_fader->height() < 160.0 ||
      second_track_fader->width() > second_track_fader->height()) {
    return fail("Track gain should be presented as an accessible vertical fader");
  }

  if (!click_item(window, second_track_select)) {
    return fail("The second track's visible Select control should accept a pointer click");
  }
  if (mixer->property("selectedAudioTrackIndex").toInt() != second_audio_track_index ||
      mixer->property("processorTrackIndex").toInt() != second_audio_track_index) {
    return fail("Track selection should update the contextual processor target to the clicked row");
  }

  QPointer<QQuickItem> drag_fader_identity(second_track_fader);
  const QPoint start = second_track_handle
                           ->mapToScene(QPointF(second_track_handle->width() / 2.0,
                                                second_track_handle->height() / 2.0))
                           .toPoint();
  const QPoint first_step =
      second_track_fader
          ->mapToScene(QPointF(second_track_fader->width() / 2.0,
                               second_track_fader->height() * 0.41))
          .toPoint();
  const QPoint finish =
      second_track_fader
          ->mapToScene(QPointF(second_track_fader->width() / 2.0,
                               second_track_fader->height() * 0.50))
          .toPoint();
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
  QTest::mouseMove(window, first_step, 60);
  settle(60);
  if (drag_fader_identity.isNull() ||
      find_item(window->contentItem(), fader_object_name) !=
          drag_fader_identity.data()) {
    return fail("A controller gain readback should preserve the pressed track fader during a drag");
  }
  const double first_step_gain =
      track_with_id(controller, second_audio_track_id)
          .value(QStringLiteral("audioGainDb"))
          .toDouble();
  if (first_step_gain > -5.0 || first_step_gain < -20.0) {
    return fail("The first pointer movement should write an intermediate track gain");
  }
  QTest::mouseMove(window, finish, 80);
  settle(60);
  if (drag_fader_identity.isNull() ||
      find_item(window->contentItem(), fader_object_name) !=
          drag_fader_identity.data()) {
    return fail("The same track fader should remain live through successive drag movements");
  }
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, finish);
  settle();

  const double edited_gain =
      track_with_id(controller, second_audio_track_id)
          .value(QStringLiteral("audioGainDb"))
          .toDouble();
  const double untouched_gain =
      track_with_id(controller, first_audio_track_id)
          .value(QStringLiteral("audioGainDb"))
          .toDouble();
  if (edited_gain > -8.0 || edited_gain < -32.0 ||
      edited_gain > first_step_gain - 3.0 ||
      std::abs(untouched_gain) > 0.001) {
    return fail("Two movements on one live fader should update only the selected track smoothly");
  }
  // Reacquire the production control for the subsequent keyboard test after
  // checking that the pressed Slider kept its identity during both moves.
  second_track_fader = find_item(window->contentItem(), fader_object_name);
  if (second_track_fader == nullptr) {
    return fail("The edited track should keep its fader after controller readback");
  }
  auto* gain_readout = find_item(
      window->contentItem(),
      QStringLiteral("timelineTrackGainReadout_") + second_audio_track_id);
  if (gain_readout == nullptr ||
      std::abs(gain_readout->property("text").toString()
                   .remove(QStringLiteral(" dB"))
                   .toDouble() -
               edited_gain) > 0.1) {
    return fail("The track fader's visible readout should follow controller state");
  }

  second_track_fader->forceActiveFocus();
  settle(30);
  if (!second_track_fader->hasActiveFocus()) {
    return fail("The vertical fader should participate in the keyboard focus order");
  }
  QTest::keyClick(window, Qt::Key_Up);
  settle();
  const double keyboard_gain =
      track_with_id(controller, second_audio_track_id)
          .value(QStringLiteral("audioGainDb"))
          .toDouble();
  if (std::abs(keyboard_gain - edited_gain - 0.5) > 0.001) {
    return fail("An Up key on the focused fader should raise the same track by one half-decibel step");
  }

  controller.undo();
  settle();
  const double undone_gain =
      track_with_id(controller, second_audio_track_id)
          .value(QStringLiteral("audioGainDb"))
          .toDouble();
  if (std::abs(undone_gain - edited_gain) > 0.001) {
    return fail("Undo should restore the last keyboard fader adjustment");
  }
  controller.undo();
  settle();
  const double reset_gain =
      track_with_id(controller, second_audio_track_id)
          .value(QStringLiteral("audioGainDb"))
          .toDouble();
  if (std::abs(reset_gain) > 0.001) {
    return fail("Undo should restore the pointer-dragged track gain");
  }

  window->setWidth(760);
  window->setHeight(320);
  settle();
  auto* compact_fader =
      find_item(window->contentItem(),
                QStringLiteral("timelineTrackGainFader_") + second_audio_track_id);
  auto* compact_handle = find_item(
      window->contentItem(),
      QStringLiteral("timelineTrackGainHandle_") + second_audio_track_id);
  auto* mixer_viewport = find_item(
      window->contentItem(), QStringLiteral("timelineMixerStripsViewport"));
  auto* master_strip = find_item(
      window->contentItem(), QStringLiteral("timelineMasterMixerStrip"));
  auto* master_meter = find_item(
      window->contentItem(), QStringLiteral("timelineMasterAudioMeter"));
  auto* compact_track_name = find_item(
      window->contentItem(),
      QStringLiteral("timelineMixerCompactTrackName_") + second_audio_track_id);
  auto* compact_mute = find_item(
      window->contentItem(),
      QStringLiteral("timelineMixerCompactMute_") + second_audio_track_id);
  auto* compact_solo = find_item(
      window->contentItem(),
      QStringLiteral("timelineMixerCompactSolo_") + second_audio_track_id);
  auto* compact_pan = find_item(
      window->contentItem(),
      QStringLiteral("timelineMixerTrackPan_") + second_audio_track_id);
  auto* compact_pan_slider = find_item(
      window->contentItem(),
      QStringLiteral("timelineMixerCompactPanSlider_") + second_audio_track_id);
  auto* compact_pan_handle = find_item(
      window->contentItem(),
      QStringLiteral("timelineMixerCompactPanHandle_") + second_audio_track_id);
  auto* compact_pan_readout = find_item(
      window->contentItem(),
      QStringLiteral("timelineMixerCompactPanReadout_") + second_audio_track_id);
  auto* compact_select = find_item(
      window->contentItem(),
      QStringLiteral("timelineMixerCompactTrackSelect_") + second_audio_track_id);
  QStringList compact_missing_items;
  const auto require_item = [&compact_missing_items](QObject* item, const QString& name) {
    if (item == nullptr) {
      compact_missing_items.push_back(name);
    }
  };
  require_item(compact_fader, QStringLiteral("fader"));
  require_item(compact_handle, QStringLiteral("fader handle"));
  require_item(mixer_viewport, QStringLiteral("strip viewport"));
  require_item(master_strip, QStringLiteral("master strip"));
  require_item(master_meter, QStringLiteral("master meter"));
  require_item(compact_track_name, QStringLiteral("track name"));
  require_item(compact_mute, QStringLiteral("Mute"));
  require_item(compact_solo, QStringLiteral("Solo"));
  require_item(compact_pan, QStringLiteral("Pan"));
  require_item(compact_pan_slider, QStringLiteral("compact Pan slider"));
  require_item(compact_pan_handle, QStringLiteral("compact Pan handle"));
  require_item(compact_pan_readout, QStringLiteral("compact Pan value"));
  require_item(compact_select, QStringLiteral("Select"));
  if (!compact_missing_items.isEmpty() || compact_fader->height() < 120.0 ||
      mixer->property("compactHeightDock").toBool() != true) {
    std::cerr << "760-by-320 Mixer missing items: "
              << compact_missing_items.join(QStringLiteral(", ")).toStdString()
              << "; fader height="
              << (compact_fader != nullptr ? compact_fader->height() : -1.0)
              << "; compactHeightDock="
              << mixer->property("compactHeightDock").toBool() << '\n';
    return fail("A 760-by-320 dock should keep track controls, the primary fader, and fixed master meter visible");
  }
  const QRectF viewport_rect = mixer_viewport->mapRectToScene(
      mixer_viewport->boundingRect());
  const QRectF handle_rect = compact_handle->mapRectToScene(
      compact_handle->boundingRect());
  const QRectF master_rect = master_strip->mapRectToScene(master_strip->boundingRect());
  const QRectF master_meter_rect = master_meter->mapRectToScene(master_meter->boundingRect());
  const QRectF window_rect(0.0, 0.0, window->width(), window->height());
  QStringList compact_fit_failures;
  const auto require_compact_fit = [&compact_fit_failures](bool passed,
                                                           const QString& name) {
    if (!passed) {
      compact_fit_failures.push_back(name);
    }
  };
  require_compact_fit(fits_within(compact_handle, viewport_rect),
                      QStringLiteral("handle inside viewport"));
  require_compact_fit(window_rect.contains(handle_rect),
                      QStringLiteral("handle inside window"));
  require_compact_fit(fits_within(compact_track_name, viewport_rect),
                      QStringLiteral("track name inside viewport"));
  require_compact_fit(fits_within(compact_mute, viewport_rect),
                      QStringLiteral("Mute inside viewport"));
  require_compact_fit(fits_within(compact_solo, viewport_rect),
                      QStringLiteral("Solo inside viewport"));
  require_compact_fit(fits_within(compact_pan, viewport_rect),
                      QStringLiteral("Pan inside viewport"));
  require_compact_fit(compact_pan_slider->isVisible() &&
                          fits_within(compact_pan_slider, viewport_rect) &&
                          fits_within(compact_pan_handle, viewport_rect),
                      QStringLiteral("real Pan slider and handle inside viewport"));
  require_compact_fit(compact_pan_slider->width() >= 72.0,
                      QStringLiteral("real Pan slider retains a usable hit width"));
  require_compact_fit(compact_pan_readout->isVisible() &&
                          compact_pan_readout->property("text").toString().length() > 0,
                      QStringLiteral("Pan value readout visible"));
  require_compact_fit(fits_within(compact_select, viewport_rect),
                      QStringLiteral("Select inside viewport"));
  require_compact_fit(master_strip->isVisible(), QStringLiteral("master visible"));
  require_compact_fit(window_rect.contains(master_rect),
                      QStringLiteral("master inside window"));
  require_compact_fit(master_meter->isVisible(),
                      QStringLiteral("master meter visible"));
  require_compact_fit(compact_track_name->isVisible(),
                      QStringLiteral("track name visible"));
  require_compact_fit(compact_mute->isVisible(), QStringLiteral("Mute visible"));
  require_compact_fit(compact_solo->isVisible(), QStringLiteral("Solo visible"));
  require_compact_fit(compact_pan->isVisible(), QStringLiteral("Pan visible"));
  require_compact_fit(master_rect.contains(master_meter_rect),
                      QStringLiteral("master meter inside master"));
  if (!compact_fit_failures.isEmpty()) {
    const QRectF track_rect = compact_track_name->parentItem() != nullptr
                                  ? compact_track_name->parentItem()->mapRectToScene(
                                        compact_track_name->parentItem()->boundingRect())
                                  : QRectF{};
    std::cerr << "760-by-320 Mixer fit failures: "
              << compact_fit_failures.join(QStringLiteral(", ")).toStdString()
              << "; viewport=" << viewport_rect.x() << ',' << viewport_rect.y()
              << ' ' << viewport_rect.width() << 'x' << viewport_rect.height()
              << "; track-name parent=" << track_rect.x() << ',' << track_rect.y()
              << ' ' << track_rect.width() << 'x' << track_rect.height()
              << "; Pan=" << compact_pan->x() << ',' << compact_pan->y() << ' '
              << compact_pan->width() << 'x' << compact_pan->height()
              << "; Pan slider=" << compact_pan_slider->x() << ','
              << compact_pan_slider->y() << ' ' << compact_pan_slider->width()
              << 'x' << compact_pan_slider->height()
              << "; Select=" << compact_select->x() << ',' << compact_select->y()
              << ' ' << compact_select->width() << 'x' << compact_select->height()
              << "; fader height=" << compact_fader->height() << '\n';
    return fail("The compact fader, pan, mute, solo, track name, and master meter should stay visible");
  }

  const double initial_pan =
      track_with_id(controller, second_audio_track_id)
          .value(QStringLiteral("audioPan")).toDouble();
  QPointer<QQuickItem> pan_slider_identity(compact_pan_slider);
  const QPoint pan_start = compact_pan_handle
                               ->mapToScene(QPointF(compact_pan_handle->width() / 2.0,
                                                    compact_pan_handle->height() / 2.0))
                               .toPoint();
  const QPoint pan_finish = compact_pan_slider
                                ->mapToScene(QPointF(compact_pan_slider->width() * 0.84,
                                                     compact_pan_slider->height() / 2.0))
                                .toPoint();
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, pan_start);
  QTest::mouseMove(window, pan_finish, 60);
  settle(60);
  const double moved_pan =
      track_with_id(controller, second_audio_track_id)
          .value(QStringLiteral("audioPan")).toDouble();
  if (pan_slider_identity.isNull() ||
      find_item(window->contentItem(),
                QStringLiteral("timelineMixerCompactPanSlider_") + second_audio_track_id) !=
          pan_slider_identity.data() ||
      std::abs(moved_pan - initial_pan) < 0.3) {
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, pan_finish);
    return fail("Dragging the compact Pan slider should update controller audioPan on the same live control");
  }
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, pan_finish);
  settle();
  controller.undo();
  settle();
  if (std::abs(track_with_id(controller, second_audio_track_id)
                   .value(QStringLiteral("audioPan")).toDouble() - initial_pan) > 0.001) {
    return fail("One undo should restore the compact Pan drag");
  }

  compact_pan_slider->forceActiveFocus();
  settle(30);
  const double keyboard_pan_start =
      track_with_id(controller, second_audio_track_id)
          .value(QStringLiteral("audioPan")).toDouble();
  QTest::keyClick(window, Qt::Key_Right);
  settle();
  const double keyboard_pan_value =
      track_with_id(controller, second_audio_track_id)
          .value(QStringLiteral("audioPan")).toDouble();
  if (!compact_pan_slider->hasActiveFocus() ||
      std::abs(keyboard_pan_value - keyboard_pan_start - 0.01) > 0.001) {
    return fail("The compact Pan slider should accept a focused Right key as one percent-pan input");
  }
  controller.undo();
  settle();
  if (std::abs(track_with_id(controller, second_audio_track_id)
                   .value(QStringLiteral("audioPan")).toDouble() - initial_pan) > 0.001) {
    return fail("One undo should restore the compact Pan keyboard input");
  }

  QPointer<QQuickItem> compact_fader_identity(compact_fader);
  const QPoint compact_start = compact_handle
                                   ->mapToScene(QPointF(compact_handle->width() / 2.0,
                                                        compact_handle->height() / 2.0))
                                   .toPoint();
  const QPoint compact_finish =
      compact_fader
          ->mapToScene(QPointF(compact_fader->width() / 2.0,
                               compact_fader->height() * 0.56))
          .toPoint();
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, compact_start);
  QTest::mouseMove(window, compact_finish, 60);
  settle(60);
  if (compact_fader_identity.isNull() ||
      find_item(window->contentItem(), fader_object_name) !=
          compact_fader_identity.data()) {
    return fail("The compact dock should preserve fader identity during its pointer edit");
  }
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, compact_finish);
  settle();
  const double compact_gain =
      track_with_id(controller, second_audio_track_id)
          .value(QStringLiteral("audioGainDb"))
          .toDouble();
  if (compact_gain > -5.0 || compact_gain < -32.0) {
    return fail("The visible compact fader should write its selected track gain");
  }
  controller.undo();
  settle();
  if (std::abs(track_with_id(controller, second_audio_track_id)
                   .value(QStringLiteral("audioGainDb"))
                   .toDouble()) > 0.001) {
    return fail("One undo should restore the compact dock fader edit");
  }

  QTemporaryDir clip_fixture_directory;
  if (!clip_fixture_directory.isValid()) {
    return fail("A temporary source should be available for a real selected-audio-clip proof");
  }
  const QString audio_path = write_silent_stereo_wav(
      QDir(clip_fixture_directory.path()).filePath(QStringLiteral("compact-clip.wav")));
  if (audio_path.isEmpty()) {
    return fail("The real compact-clip WAV should be written as a valid stereo PCM source");
  }
  controller.addClipFromGallery(QStringLiteral("audio:mixer-compact-clip"), audio_path,
                                QStringLiteral("Compact Clip"), 0.2, QString{});
  controller.selectClip(0);
  const QVariantMap selected_audio_clip = controller.currentClip();
  if (controller.selectedClipCount() != 1 ||
      (selected_audio_clip.value(QStringLiteral("trackKind")).toString() !=
           QStringLiteral("audio") &&
       !selected_audio_clip.value(QStringLiteral("hasAudio")).toBool())) {
    return fail("The compact mixer proof should use an actually selected clip with decoded audio semantics");
  }

  window->setWidth(440);
  window->setHeight(260);
  settle();
  auto* narrow_viewport = find_item(
      window->contentItem(), QStringLiteral("timelineMixerStripsViewport"));
  auto* narrow_header = find_item(
      window->contentItem(), QStringLiteral("timelineAudioMixerHeaderRow"));
  auto* narrow_title = find_item(
      window->contentItem(), QStringLiteral("timelineStudioAudioMixerTitle"));
  auto* narrow_clip_button = find_item(
      window->contentItem(), QStringLiteral("timelineCompactSelectedClipToolsButton"));
  auto* narrow_track = find_item(
      window->contentItem(),
      QStringLiteral("timelineMixerTrackStrip_") + first_audio_track_id);
  const QString first_audio_fader_name =
      QStringLiteral("timelineTrackGainFader_") + first_audio_track_id;
  auto* narrow_fader = find_item(
      window->contentItem(), first_audio_fader_name);
  auto* narrow_handle = find_item(
      window->contentItem(),
      QStringLiteral("timelineTrackGainHandle_") + first_audio_track_id);
  auto* narrow_select = find_item(
      window->contentItem(), QStringLiteral("timelineMixerCompactTrackSelect_")
                                  + first_audio_track_id);
  auto* narrow_mute = find_item(
      window->contentItem(), QStringLiteral("timelineMixerCompactMute_")
                                  + first_audio_track_id);
  auto* narrow_solo = find_item(
      window->contentItem(), QStringLiteral("timelineMixerCompactSolo_")
                                  + first_audio_track_id);
  auto* narrow_pan = find_item(
      window->contentItem(), QStringLiteral("timelineMixerTrackPan_")
                                 + first_audio_track_id);
  auto* narrow_pan_slider = find_item(
      window->contentItem(), QStringLiteral("timelineMixerCompactPanSlider_")
                                 + first_audio_track_id);
  auto* narrow_track_tools = find_item(
      window->contentItem(), QStringLiteral("timelineMixerCompactTrackTools_")
                                  + first_audio_track_id);
  auto* narrow_track_name = find_item(
      window->contentItem(), QStringLiteral("timelineMixerCompactTrackName_")
                                  + first_audio_track_id);
  auto* narrow_gain_readout = find_item(
      window->contentItem(), QStringLiteral("timelineMixerCompactGainReadout_")
                                  + first_audio_track_id);
  auto* narrow_master = find_item(
      window->contentItem(), QStringLiteral("timelineMasterMixerStrip"));
  auto* narrow_master_meter = find_item(
      window->contentItem(), QStringLiteral("timelineMasterAudioMeter"));
  if (mixer->property("compactHeightDock").toBool() != true ||
      mixer->property("compactWidthDock").toBool() != true ||
      narrow_viewport == nullptr || narrow_header == nullptr || narrow_title == nullptr ||
      narrow_clip_button == nullptr || narrow_track == nullptr ||
      narrow_fader == nullptr || narrow_handle == nullptr || narrow_select == nullptr ||
      narrow_mute == nullptr || narrow_solo == nullptr || narrow_pan == nullptr ||
      narrow_pan_slider == nullptr ||
      narrow_track_tools == nullptr || narrow_track_name == nullptr ||
      narrow_gain_readout == nullptr || narrow_master == nullptr ||
      narrow_master_meter == nullptr || narrow_fader->height() < 120.0) {
    return fail("A 440-by-260 dock with a selected clip should expose a full track strip, header clip-tools entry, and fixed master meter");
  }
  const QRectF narrow_viewport_rect = narrow_viewport->mapRectToScene(
      narrow_viewport->boundingRect());
  const QRectF narrow_header_rect = narrow_header->mapRectToScene(
      narrow_header->boundingRect());
  const QRectF narrow_track_rect = narrow_track->mapRectToScene(
      narrow_track->boundingRect());
  const QRectF narrow_title_rect = narrow_title->mapRectToScene(
      narrow_title->boundingRect());
  const QRectF narrow_clip_button_rect = narrow_clip_button->mapRectToScene(
      narrow_clip_button->boundingRect());
  const QRectF narrow_master_rect = narrow_master->mapRectToScene(
      narrow_master->boundingRect());
  const QRectF narrow_meter_rect = narrow_master_meter->mapRectToScene(
      narrow_master_meter->boundingRect());
  const QRectF narrow_window_rect(0.0, 0.0, window->width(), window->height());
  const QVariantMap narrow_track_state =
      track_with_id(controller, first_audio_track_id);
  const QString expected_narrow_gain_text =
      QString::number(narrow_track_state.value(QStringLiteral("audioGainDb")).toDouble(),
                      'f', 1) + QStringLiteral(" dB");
  auto* narrow_select_label = find_item_with_text(narrow_select, QStringLiteral("Select"));
  auto* narrow_gain_label = find_item_with_text(narrow_gain_readout,
                                                 expected_narrow_gain_text);
  QStringList narrow_fit_failures;
  const auto require_fit = [&narrow_fit_failures](bool passed, const QString& name) {
    if (!passed) {
      narrow_fit_failures.push_back(name);
    }
  };
  require_fit(narrow_track->isVisible(), QStringLiteral("track visible"));
  require_fit(narrow_viewport_rect.contains(narrow_track_rect),
              QStringLiteral("track fully inside clipped viewport"));
  require_fit(narrow_header_rect.contains(narrow_title_rect),
              QStringLiteral("mixer title inside header"));
  require_fit(narrow_header_rect.contains(narrow_clip_button_rect),
              QStringLiteral("Clip action inside header"));
  require_fit(narrow_title->width() + 1.0 >= narrow_title->implicitWidth(),
              QStringLiteral("mixer title text fits"));
  require_fit(narrow_clip_button->property("text").toString() == QStringLiteral("Clip"),
              QStringLiteral("Clip action label"));
  require_fit(fits_within(narrow_handle, narrow_viewport_rect),
              QStringLiteral("fader handle inside viewport"));
  require_fit(fits_within(narrow_select, narrow_viewport_rect) &&
                  fits_within(narrow_select, narrow_track_rect),
              QStringLiteral("Select action inside track and viewport"));
  require_fit(fits_within(narrow_track_name, narrow_track_rect),
              QStringLiteral("track name inside strip"));
  require_fit(fits_within(narrow_gain_readout, narrow_track_rect),
              QStringLiteral("gain readout inside strip"));
  require_fit(fits_within(narrow_mute, narrow_viewport_rect),
              QStringLiteral("Mute action inside viewport"));
  require_fit(fits_within(narrow_solo, narrow_viewport_rect),
              QStringLiteral("Solo action inside viewport"));
  require_fit(fits_within(narrow_pan, narrow_viewport_rect),
              QStringLiteral("Pan control inside viewport"));
  require_fit(narrow_pan_slider->isVisible() &&
                  narrow_pan_slider->width() >= 72.0 &&
                  fits_within(narrow_pan_slider, narrow_viewport_rect) &&
                  fits_within(narrow_pan_slider, narrow_track_rect),
              QStringLiteral("real narrow Pan slider inside the full track strip"));
  require_fit(fits_within(narrow_track_tools, narrow_viewport_rect),
              QStringLiteral("track tools inside viewport"));
  require_fit(narrow_select->property("text").toString() == QStringLiteral("Select"),
              QStringLiteral("complete Select label"));
  require_fit(narrow_select_label != nullptr && item_text_fits(narrow_select_label),
              QStringLiteral("Select label text bounds"));
  require_fit(narrow_gain_label != nullptr && item_text_fits(narrow_gain_label),
              QStringLiteral("gain text bounds"));
  require_fit(narrow_window_rect.contains(narrow_master_rect),
              QStringLiteral("master strip inside window"));
  require_fit(narrow_master_rect.contains(narrow_meter_rect),
              QStringLiteral("master meter inside strip"));
  require_fit(narrow_master_meter->width() >= 124.0,
              QStringLiteral("master meter readable width"));
  if (!narrow_fit_failures.isEmpty()) {
    std::cerr << "Narrow Mixer fit failures: "
              << narrow_fit_failures.join(QStringLiteral(", ")).toStdString()
              << "; viewport=" << narrow_viewport_rect.x() << ','
              << narrow_viewport_rect.y() << ' ' << narrow_viewport_rect.width()
              << 'x' << narrow_viewport_rect.height() << "; track="
              << narrow_track_rect.x() << ',' << narrow_track_rect.y() << ' '
              << narrow_track_rect.width() << 'x' << narrow_track_rect.height()
              << "; Select=" << narrow_select->width()
              << "; gain=" << narrow_gain_readout->width()
              << "; title=" << narrow_title->width() << '/'
              << narrow_title->implicitWidth()
              << "; Select text="
              << (narrow_select_label != nullptr
                      ? narrow_select_label->width() : -1.0)
              << '/'
              << (narrow_select_label != nullptr
                      ? narrow_select_label->property("contentWidth").toDouble() : -1.0)
              << "; gain text="
              << (narrow_gain_label != nullptr
                      ? narrow_gain_label->width() : -1.0)
              << '/'
              << (narrow_gain_label != nullptr
                      ? narrow_gain_label->property("contentWidth").toDouble() : -1.0)
              << " text=" << expected_narrow_gain_text.toStdString() << '\n';
    return fail("The first narrow track, full Select and gain labels, header clip tools, and readable fixed master meter should fit without clipping");
  }

  if (!click_item(window, narrow_select) ||
      mixer->property("selectedAudioTrackIndex").toInt() !=
          track_with_id(controller, first_audio_track_id)
              .value(QStringLiteral("trackIndex")).toInt()) {
    return fail("The complete narrow track selector should still target the track context");
  }
  const double narrow_initial_gain =
      track_with_id(controller, first_audio_track_id)
          .value(QStringLiteral("audioGainDb")).toDouble();
  QPointer<QQuickItem> narrow_fader_identity(narrow_fader);
  const QPoint narrow_start = narrow_handle
                                  ->mapToScene(QPointF(narrow_handle->width() / 2.0,
                                                       narrow_handle->height() / 2.0))
                                  .toPoint();
  const QPoint narrow_first_step = narrow_fader
                                      ->mapToScene(QPointF(narrow_fader->width() / 2.0,
                                                           narrow_fader->height() * 0.42))
                                      .toPoint();
  const QPoint narrow_finish = narrow_fader
                                   ->mapToScene(QPointF(narrow_fader->width() / 2.0,
                                                        narrow_fader->height() * 0.52))
                                   .toPoint();
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, narrow_start);
  QTest::mouseMove(window, narrow_first_step, 60);
  settle(60);
  const double narrow_intermediate_gain =
      track_with_id(controller, first_audio_track_id)
          .value(QStringLiteral("audioGainDb")).toDouble();
  if (narrow_fader_identity.isNull() ||
      find_item(window->contentItem(), first_audio_fader_name) !=
          narrow_fader_identity.data() ||
      std::abs(narrow_intermediate_gain - narrow_initial_gain) < 1.0) {
    return fail("The narrow dock should write an intermediate value on the same live track fader");
  }
  QTest::mouseMove(window, narrow_finish, 60);
  settle(60);
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, narrow_finish);
  settle();
  const double narrow_edited_gain =
      track_with_id(controller, first_audio_track_id)
          .value(QStringLiteral("audioGainDb")).toDouble();
  if (narrow_fader_identity.isNull() ||
      find_item(window->contentItem(), first_audio_fader_name) !=
          narrow_fader_identity.data() ||
      std::abs(narrow_edited_gain - narrow_initial_gain) < 3.0) {
    return fail("The narrow dock fader should keep its identity and commit the pointer gesture");
  }
  controller.undo();
  settle();
  if (std::abs(track_with_id(controller, first_audio_track_id)
                   .value(QStringLiteral("audioGainDb")).toDouble() -
               narrow_initial_gain) > 0.001) {
    return fail("One undo should restore the compact-width track fader gesture");
  }

  const auto drag_gain_to_bound = [&](double vertical_position, double target_db,
                                      const QString& description) {
    const double initial_db =
        track_with_id(controller, first_audio_track_id)
            .value(QStringLiteral("audioGainDb")).toDouble();
    auto* live_handle = find_item(window->contentItem(),
        QStringLiteral("timelineTrackGainHandle_") + first_audio_track_id);
    if (live_handle == nullptr) {
      std::cerr << "Gain-bound gesture has no live fader handle for "
                << description.toStdString() << '\n';
      return false;
    }
    const QPoint start = live_handle
                             ->mapToScene(QPointF(live_handle->width() / 2.0,
                                                  live_handle->height() / 2.0))
                             .toPoint();
    const QPoint finish = narrow_fader
                              ->mapToScene(QPointF(narrow_fader->width() / 2.0,
                                                   vertical_position))
                              .toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window, finish, 60);
    settle(60);
    const double actual_db =
        track_with_id(controller, first_audio_track_id)
            .value(QStringLiteral("audioGainDb")).toDouble();
    const QString expected_text = QString::number(target_db, 'f', 1)
                                  + QStringLiteral(" dB");
    auto* rendered_text = find_item_with_text(narrow_gain_readout, expected_text);
    const double rendered_width = rendered_text != nullptr ? rendered_text->width() : -1.0;
    const double rendered_content_width = rendered_text != nullptr
        ? rendered_text->property("contentWidth").toDouble() : -1.0;
    const bool value_and_text_fit = std::abs(actual_db - target_db) < 0.05 &&
        rendered_text != nullptr && item_text_fits(rendered_text);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, finish);
    settle();
    if (std::abs(actual_db - initial_db) > 0.001) {
      controller.undo();
      settle();
    }
    const double restored_db =
        track_with_id(controller, first_audio_track_id)
            .value(QStringLiteral("audioGainDb")).toDouble();
    if (!value_and_text_fit || std::abs(restored_db - initial_db) > 0.001) {
      std::cerr << "Compact gain-bound proof " << description.toStdString()
                << ": requested=" << target_db << " actual=" << actual_db
                << " label=" << expected_text.toStdString()
                << " text-width=" << rendered_width
                << " content-width=" << rendered_content_width
                << " restored=" << restored_db << " initial=" << initial_db << '\n';
      return false;
    }
    return true;
  };
  if (!drag_gain_to_bound(narrow_fader->height() - 1.0, -60.0,
                          QStringLiteral("minimum gain"))) {
    return fail("The 440-pixel mixer should show the full -60.0 dB readout after a real minimum-fader drag, then undo");
  }
  if (!drag_gain_to_bound(1.0, 24.0, QStringLiteral("maximum gain"))) {
    return fail("The 440-pixel mixer should show the full 24.0 dB readout after a real maximum-fader drag, then undo");
  }

  window->setProperty("dockedMixerHeight", 260);
  window->setHeight(720);
  settle();
  auto* narrow_dock_host =
      find_item(window->contentItem(), QStringLiteral("timelineAudioDockHost"));
  const QRectF narrow_dock_host_rect =
      narrow_dock_host != nullptr
          ? narrow_dock_host->mapRectToScene(narrow_dock_host->boundingRect())
          : QRectF{};
  const double narrow_mixer_height = mixer->property("height").toDouble();
  if (narrow_mixer_height < 259.0 || narrow_mixer_height > 261.0 ||
      narrow_dock_host == nullptr || narrow_dock_host_rect.y() < 450.0 ||
      narrow_dock_host_rect.height() < 259.0) {
    return fail("The narrow mixer should remain a 260-pixel bottom dock inside a full-height host window");
  }
  if (!click_item(window, narrow_clip_button)) {
    return fail("The selected clip's compact tools entry should open its real audio controls");
  }
  settle();
  auto* clip_tools_popup =
      find_object(window, QStringLiteral("timelineCompactSelectedClipToolsWindow"));
  auto* clip_channel_button = find_window_item(
      window,
      QStringLiteral("timelineCompactSelectedClipChannelMappingButton"));
  auto* clip_gain_control = find_window_item(
      window,
      QStringLiteral("timelineCompactSelectedClipGainSliderControl"));
  auto* clip_gain_handle = clip_gain_control == nullptr
                               ? nullptr
                               : qobject_cast<QQuickItem*>(
                                     clip_gain_control->property("handle").value<QObject*>());
  auto* clip_fade_in = find_window_item(
      window,
      QStringLiteral("timelineCompactSelectedClipFadeInSlider"));
  auto* clip_fade_out = find_window_item(
      window,
      QStringLiteral("timelineCompactSelectedClipFadeOutSlider"));
  auto* clip_controls_viewport = find_window_item(
      window, QStringLiteral("timelineCompactSelectedClipControlsViewport"));
  if (clip_tools_popup == nullptr || !clip_tools_popup->property("visible").toBool() ||
      !clip_tools_popup->property("opened").toBool() ||
      clip_tools_popup->property("width").toDouble() < 200.0 ||
      clip_tools_popup->property("height").toDouble() < 400.0) {
    std::cerr << "Selected-clip popup readiness: exists=" << (clip_tools_popup != nullptr)
              << " visible=" << (clip_tools_popup != nullptr &&
                                   clip_tools_popup->property("visible").toBool())
              << " opened=" << (clip_tools_popup != nullptr &&
                                  clip_tools_popup->property("opened").toBool())
              << " size=" << (clip_tools_popup != nullptr
                                   ? clip_tools_popup->property("width").toDouble() : 0.0)
              << 'x' << (clip_tools_popup != nullptr
                             ? clip_tools_popup->property("height").toDouble() : 0.0)
              << " at=" << (clip_tools_popup != nullptr
                                  ? clip_tools_popup->property("x").toDouble() : 0.0)
              << ',' << (clip_tools_popup != nullptr
                              ? clip_tools_popup->property("y").toDouble() : 0.0)
              << " selectedClip=" << controller.currentClip()
                                         .value(QStringLiteral("clipId")).toString().toStdString()
              << " audioAvailable=" << mixer->property("selectedClipAudioAvailable").toBool()
              << '\n';
    return fail("The selected-clip tools popup should be open at a usable size for the selected audio clip");
  }
  const QRectF clip_popup_window_rect(0.0, 0.0, window->width(), window->height());
  const double clip_popup_x = clip_tools_popup->property("x").toDouble();
  const double clip_popup_y = clip_tools_popup->property("y").toDouble();
  const double clip_popup_width = clip_tools_popup->property("width").toDouble();
  const double clip_popup_height = clip_tools_popup->property("height").toDouble();
  const bool clip_popup_fits_window =
      clip_popup_x >= 0.0 && clip_popup_y >= 0.0 &&
      clip_popup_x + clip_popup_width <= window->width() + 1.0 &&
      clip_popup_y + clip_popup_height <= window->height() + 1.0;
  const auto report_item = [](const char* label, QQuickItem* item) {
    std::cerr << ' ' << label << "=" << (item != nullptr)
              << " visible=" << (item != nullptr && item->isVisible())
              << " size=" << (item != nullptr ? item->width() : 0.0)
              << 'x' << (item != nullptr ? item->height() : 0.0);
    if (item != nullptr) {
      const auto rect = item->mapRectToScene(item->boundingRect());
      std::cerr << " scene=" << rect.x() << ',' << rect.y() << ' '
                << rect.width() << 'x' << rect.height();
    }
  };
  if (clip_channel_button == nullptr || clip_gain_control == nullptr ||
      clip_gain_handle == nullptr || clip_fade_in == nullptr || clip_fade_out == nullptr ||
      clip_controls_viewport == nullptr || !clip_controls_viewport->isVisible() ||
      clip_controls_viewport->width() <= 0.0 || clip_controls_viewport->height() <= 0.0 ||
      !clip_channel_button->isVisible() || !clip_gain_control->isVisible() ||
      !clip_gain_handle->isVisible() || !clip_fade_in->isVisible() ||
      !clip_fade_out->isVisible() ||
      !clip_popup_fits_window ||
      !fits_within(clip_channel_button, clip_popup_window_rect) ||
      !fits_within(clip_gain_control, clip_popup_window_rect) ||
      !fits_within(clip_fade_in, clip_popup_window_rect) ||
      !fits_within(clip_fade_out, clip_popup_window_rect) ||
      !fits_within(clip_gain_handle,
                   clip_gain_control->mapRectToScene(clip_gain_control->boundingRect()))) {
    std::cerr << "Selected-clip control readiness:";
    report_item("viewport", clip_controls_viewport);
    report_item("sourceMapping", clip_channel_button);
    report_item("gainSlider", clip_gain_control);
    report_item("gainHandle", clip_gain_handle);
    report_item("fadeIn", clip_fade_in);
    report_item("fadeOut", clip_fade_out);
    std::cerr << '\n';
    return fail("The selected clip popup should expose visible, laid-out source mapping, gain, and fade controls");
  }
  if (!click_item(window, clip_channel_button)) {
    return fail("Clip tools should open the real source-channel mapping popup");
  }
  settle();
  auto* channel_popup =
      find_object(window, QStringLiteral("timelineAudioChannelMappingWindow"));
  auto* channel_popup_content = channel_popup == nullptr
                                   ? nullptr
                                   : qobject_cast<QQuickItem*>(
                                         channel_popup->property("contentItem").value<QObject*>());
  auto* channel_mode = channel_popup_content != nullptr
                           ? find_item(channel_popup_content,
                                       QStringLiteral("timelineAudioChannelModeCombo"))
                           : find_window_item(window,
                                              QStringLiteral("timelineAudioChannelModeCombo"));
  const auto find_channel_item = [&](const QString& name) {
    return channel_popup_content != nullptr
               ? find_item(channel_popup_content, name)
               : find_window_item(window, name);
  };
  auto* channel_title = find_channel_item(
      QStringLiteral("timelineAudioChannelPopupTitle"));
  auto* channel_summary = find_channel_item(
      QStringLiteral("timelineAudioChannelPopupSummary"));
  auto* channel_layout = find_channel_item(
      QStringLiteral("timelineAudioSourceLayoutReadout"));
  auto* channel_preview = find_channel_item(
      QStringLiteral("timelineAudioChannelPreviewCard"));
  auto* channel_swap = find_channel_item(
      QStringLiteral("timelineAudioSwapChannelsToggle"));
  auto* channel_invert_left = find_channel_item(
      QStringLiteral("timelineAudioInvertLeftToggle"));
  auto* channel_invert_right = find_channel_item(
      QStringLiteral("timelineAudioInvertRightToggle"));
  auto* channel_reset = find_channel_item(
      QStringLiteral("timelineAudioChannelResetButton"));
  const QRectF channel_popup_bounds(
      channel_popup != nullptr ? channel_popup->property("x").toDouble() : 0.0,
      channel_popup != nullptr ? channel_popup->property("y").toDouble() : 0.0,
      channel_popup != nullptr ? channel_popup->property("width").toDouble() : 0.0,
      channel_popup != nullptr ? channel_popup->property("height").toDouble() : 0.0);
  if (channel_popup == nullptr || !channel_popup->property("visible").toBool() ||
      !channel_popup->property("opened").toBool() || channel_mode == nullptr ||
      channel_popup->property("height").toDouble() < 450.0 ||
      channel_popup->property("x").toDouble() < 0.0 ||
      channel_popup->property("y").toDouble() < 0.0 ||
      channel_popup->property("x").toDouble() + channel_popup->property("width").toDouble()
          > window->width() + 1.0 ||
      channel_popup->property("y").toDouble() + channel_popup->property("height").toDouble()
          > window->height() + 1.0 ||
      !channel_mode->isVisible() || !fits_within(channel_mode, channel_popup_bounds) ||
      channel_mode->width() <= 0.0 || channel_mode->width() > channel_popup_bounds.width() ||
      channel_title == nullptr || channel_summary == nullptr || channel_layout == nullptr ||
      channel_preview == nullptr || channel_swap == nullptr || channel_invert_left == nullptr ||
      channel_invert_right == nullptr || channel_reset == nullptr ||
      !channel_title->isVisible() || !channel_summary->isVisible() ||
      !channel_layout->isVisible() || !channel_preview->isVisible() ||
      !channel_swap->isVisible() || !channel_invert_left->isVisible() ||
      !channel_invert_right->isVisible() || !channel_reset->isVisible() ||
      !fits_within(channel_title, channel_popup_bounds) ||
      !fits_within(channel_summary, channel_popup_bounds) ||
      !fits_within(channel_layout, channel_popup_bounds) ||
      !fits_within(channel_preview, channel_popup_bounds) ||
      !fits_within(channel_swap, channel_popup_bounds) ||
      !fits_within(channel_invert_left, channel_popup_bounds) ||
      !fits_within(channel_invert_right, channel_popup_bounds) ||
      !fits_within(channel_reset, channel_popup_bounds)) {
    std::cerr << "Source-channel popup readiness: exists=" << (channel_popup != nullptr)
              << " visible=" << (channel_popup != nullptr &&
                                   channel_popup->property("visible").toBool())
              << " opened=" << (channel_popup != nullptr &&
                                  channel_popup->property("opened").toBool())
              << " size=" << (channel_popup != nullptr
                                   ? channel_popup->property("width").toDouble() : 0.0)
              << 'x' << (channel_popup != nullptr
                             ? channel_popup->property("height").toDouble() : 0.0)
              << " at=" << (channel_popup != nullptr
                                  ? channel_popup->property("x").toDouble() : 0.0)
              << ',' << (channel_popup != nullptr
                              ? channel_popup->property("y").toDouble() : 0.0)
              << " contentItem=" << (channel_popup_content != nullptr)
              << '\n';
    report_item("channelMode", channel_mode);
    report_item("channelTitle", channel_title);
    report_item("channelSummary", channel_summary);
    report_item("channelLayout", channel_layout);
    report_item("channelPreview", channel_preview);
    report_item("channelSwap", channel_swap);
    report_item("channelInvertLeft", channel_invert_left);
    report_item("channelInvertRight", channel_invert_right);
    report_item("channelReset", channel_reset);
    return fail("The compact clip entry should expose the production channel-mapping controls");
  }
  if (!click_item(window, channel_mode)) {
    return fail("The selected clip's source interpretation combo should open by pointer");
  }
  settle();
  auto* left_channel_option = find_window_item(
      window, QStringLiteral("timelineAudioChannelModeComboOption1"));
  if (left_channel_option == nullptr || !left_channel_option->isVisible() ||
      !click_item(window, left_channel_option)) {
    report_item("leftChannelOption", left_channel_option);
    return fail("The actual source-channel option should be selectable by pointer");
  }
  settle();
  if (controller.currentClip().value(QStringLiteral("audioChannelMode")).toString() !=
          QStringLiteral("mono-left") || channel_mode->property("currentIndex").toInt() != 1) {
    return fail("Selecting Left channel as mono should update the selected clip's real channel mapping");
  }
  controller.undo();
  settle();
  if (controller.currentClip().value(QStringLiteral("audioChannelMode")).toString() !=
          QStringLiteral("source") || channel_mode->property("currentIndex").toInt() != 0) {
    return fail("Undo should restore the selected clip's original source-channel mapping");
  }
  QTest::keyClick(window, Qt::Key_Escape);
  settle();

  const double initial_clip_gain =
      controller.currentClip().value(QStringLiteral("baseAudioGainDb")).toDouble();
  QPointer<QQuickItem> clip_gain_identity(clip_gain_control);
  const QPoint clip_gain_start = clip_gain_handle
                                     ->mapToScene(QPointF(clip_gain_handle->width() / 2.0,
                                                          clip_gain_handle->height() / 2.0))
                                     .toPoint();
  const QPoint clip_gain_finish = clip_gain_control
                                      ->mapToScene(QPointF(clip_gain_control->width() * 0.38,
                                                           clip_gain_control->height() / 2.0))
                                      .toPoint();
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, clip_gain_start);
  QTest::mouseMove(window, clip_gain_finish, 60);
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, clip_gain_finish);
  settle(100);
  const double edited_clip_gain =
      controller.currentClip().value(QStringLiteral("baseAudioGainDb")).toDouble();
  if (clip_gain_identity.isNull() ||
      find_window_item(window,
                       QStringLiteral("timelineCompactSelectedClipGainSliderControl")) !=
          clip_gain_identity.data() ||
      std::abs(edited_clip_gain - initial_clip_gain) < 2.0 ||
      std::abs(clip_gain_control->property("value").toDouble() - edited_clip_gain) > 0.1) {
    return fail("Dragging compact selected-clip gain should write and read back through the real controller");
  }
  controller.undo();
  settle();
  if (std::abs(controller.currentClip()
                   .value(QStringLiteral("baseAudioGainDb")).toDouble() -
               initial_clip_gain) > 0.001) {
    return fail("One undo should restore compact selected-clip gain");
  }

  std::cout << "Audio Mixer track selection, full narrow strip and master fit, real clip controls, fader/clip gain readback and undo passed\n";
  return EXIT_SUCCESS;
}
