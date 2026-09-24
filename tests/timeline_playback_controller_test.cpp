#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"
#include "timeline_audio_output.h"
#include "app_bootstrap.h"

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

double stereo_rms(const QVector<float>& samples, int skip_frames = 0) {
  const auto start = qMin(samples.size(), qMax(0, skip_frames * 2));
  auto sum = 0.0;
  auto count = 0;
  for (auto index = start; index < samples.size(); ++index) {
    const auto sample = static_cast<double>(samples.at(index));
    sum += sample * sample;
    ++count;
  }
  return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

double stereo_component_rms(
    const QVector<float>& samples,
    bool side,
    int skip_frames = 0) {
  const auto start_frame = qMin(samples.size() / 2, qMax(0, skip_frames));
  auto sum = 0.0;
  auto count = 0;
  for (auto frame = start_frame; frame * 2 + 1 < samples.size(); ++frame) {
    const auto left = static_cast<double>(samples.at(frame * 2));
    const auto right = static_cast<double>(samples.at(frame * 2 + 1));
    const auto component = side ? (left - right) * 0.5 : (left + right) * 0.5;
    sum += component * component;
    ++count;
  }
  return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

double mid_tone_amplitude(
    const QVector<float>& samples,
    double frequency,
    int skip_frames = 0) {
  constexpr auto kSampleRate = 48000.0;
  const auto start_frame = qMin(samples.size() / 2, qMax(0, skip_frames));
  auto sine_sum = 0.0;
  auto cosine_sum = 0.0;
  auto count = 0;
  for (auto frame = start_frame; frame * 2 + 1 < samples.size(); ++frame) {
    const auto mid = (static_cast<double>(samples.at(frame * 2)) +
                      static_cast<double>(samples.at(frame * 2 + 1))) * 0.5;
    const auto phase = 2.0 * 3.14159265358979323846 * frequency *
                       static_cast<double>(frame) / kSampleRate;
    sine_sum += mid * std::sin(phase);
    cosine_sum += mid * std::cos(phase);
    ++count;
  }
  return count > 0
             ? 2.0 * std::sqrt(sine_sum * sine_sum + cosine_sum * cosine_sum) /
                   static_cast<double>(count)
             : 0.0;
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

QString create_test_video(const QString& path, const QString& color) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    return touch_file(path);
  }

  QProcess process;
  process.start(
      ffmpeg,
      {
          QStringLiteral("-y"),
          QStringLiteral("-f"),
          QStringLiteral("lavfi"),
          QStringLiteral("-i"),
          QStringLiteral("color=c=%1:s=160x90:r=24:d=0.25").arg(color),
          QStringLiteral("-an"),
          QStringLiteral("-c:v"),
          QStringLiteral("libx264"),
          QStringLiteral("-pix_fmt"),
          QStringLiteral("yuv420p"),
          path,
      });
  if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit ||
      process.exitCode() != 0) {
    return {};
  }
  return path;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  qputenv("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS", QByteArrayLiteral("1"));
  QApplication app(argc, argv);

  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    return fail("temporary workspace should be created");
  }

  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", workspace.filePath(QStringLiteral("app-data")).toUtf8());

  const auto first_video = create_test_video(
      workspace.filePath(QStringLiteral("playback-01.mp4")), QStringLiteral("red"));
  const auto second_video = create_test_video(
      workspace.filePath(QStringLiteral("playback-02.mp4")), QStringLiteral("blue"));
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

  const auto first_visible_source = playback.activeSourceUrl();
  auto initial_prefetch_plan = playback.prefetchPlan();
  if (initial_prefetch_plan.isEmpty() ||
      initial_prefetch_plan.first().toMap().value(QStringLiteral("reason")).toString() !=
          QStringLiteral("forward-cut") ||
      initial_prefetch_plan.first().toMap().value(QStringLiteral("sourceUrl")).toString() !=
          QUrl::fromLocalFile(second_video).toString() ||
      initial_prefetch_plan.first().toMap().value(QStringLiteral("protectedSourceUrl")).toString() !=
          first_visible_source) {
    return fail("playback prefetch should protect the visible frame while warming the next cut");
  }
  playback.setLikelyNextEditSeconds(4.5);
  QEventLoop likely_edit_loop;
  QTimer::singleShot(30, &likely_edit_loop, &QEventLoop::quit);
  likely_edit_loop.exec();
  if (playback.prefetchPlan().isEmpty() ||
      playback.prefetchPlan().first().toMap().value(QStringLiteral("reason")).toString() !=
          QStringLiteral("likely-next-edit") ||
      playback.activeSourceUrl() != first_visible_source ||
      playback.prefetchedSourceUrl() == first_visible_source) {
    return fail("likely edit prefetch must use the separate warm player without evicting Program");
  }
  playback.setLikelyNextEditSeconds(-1.0);
  playback.seekSequenceSeconds(6.0);
  const auto second_visible_source = playback.activeSourceUrl();
  playback.shuttleReverse();
  QEventLoop reverse_prefetch_loop;
  QTimer::singleShot(30, &reverse_prefetch_loop, &QEventLoop::quit);
  reverse_prefetch_loop.exec();
  if (playback.prefetchDirection() >= 0.0 || playback.prefetchPlan().isEmpty() ||
      playback.prefetchPlan().first().toMap().value(QStringLiteral("reason")).toString() !=
          QStringLiteral("reverse-cut") ||
      playback.prefetchPlan().first().toMap().value(QStringLiteral("sourceUrl")).toString() !=
          first_visible_source ||
      playback.activeSourceUrl() != second_visible_source) {
    return fail("reverse shuttle should reverse prefetch direction while retaining the visible source");
  }
  playback.stopShuttle();
  playback.setPlaybackLoop(0.5, 8.0);
  QEventLoop loop_prefetch_loop;
  QTimer::singleShot(30, &loop_prefetch_loop, &QEventLoop::quit);
  loop_prefetch_loop.exec();
  auto found_loop_start = false;
  for (const auto& value : playback.prefetchPlan()) {
    if (value.toMap().value(QStringLiteral("reason")).toString() ==
            QStringLiteral("loop-start") &&
        value.toMap().value(QStringLiteral("sourceUrl")).toString() == first_visible_source) {
      found_loop_start = true;
      break;
    }
  }
  if (!found_loop_start || playback.activeSourceUrl() != second_visible_source) {
    return fail("loop scheduling should warm the opposite boundary without replacing Program");
  }
  playback.clearPlaybackLoop();
  if (!playback.playReviewRange(2.0, 4.0, 1.0, 0.5, false) ||
      !playback.playbackRangeActive() || playback.playbackRangeLooping() ||
      !nearly_equal(playback.playbackRangeStart(), 1.0) ||
      !nearly_equal(playback.playbackRangeEnd(), 4.5) ||
      !document.sequencePlaying() ||
      !nearly_equal(document.sequencePlayheadSeconds(), 1.0)) {
    return fail("saved review playback should include explicit preroll and postroll while exposing exact active boundaries");
  }
  playback.stopReviewPlayback();
  if (playback.playbackRangeActive() || document.sequencePlaying()) {
    return fail("stopping a review range should clear its playback boundary and pause transport");
  }
  if (!playback.playInToOut(0.1, 0.2)) {
    return fail("Play In to Out should start a one-shot exact range");
  }
  QEventLoop in_out_loop;
  QTimer::singleShot(350, &in_out_loop, &QEventLoop::quit);
  in_out_loop.exec();
  if (document.sequencePlaying() || playback.playbackRangeActive() ||
      std::abs(document.sequencePlayheadSeconds() - 0.2) > 0.04) {
    return fail("one-shot In-to-Out playback should stop on the exact Out boundary without running to sequence end");
  }
  if (!playback.playReviewRange(0.1, 0.2, 0.0, 0.0, true)) {
    return fail("looped range playback should start from its exact In boundary");
  }
  QEventLoop range_loop;
  QTimer::singleShot(350, &range_loop, &QEventLoop::quit);
  range_loop.exec();
  if (!document.sequencePlaying() || !playback.playbackRangeActive() ||
      !playback.playbackRangeLooping() ||
      document.sequencePlayheadSeconds() < 0.09 ||
      document.sequencePlayheadSeconds() > 0.21) {
    return fail("looped review playback should wrap within its exact range rather than pausing at Out");
  }
  playback.stopReviewPlayback();
  if (!playback.reviewFromEditPoint(4.0, 0.5, 0.75) ||
      !nearly_equal(playback.playbackRangeStart(), 3.5) ||
      !nearly_equal(playback.playbackRangeEnd(), 4.75) ||
      playback.playbackRangeLooping()) {
    return fail("Review from Edit Point should derive a one-shot preroll/postroll window around the boundary");
  }
  playback.stopReviewPlayback();
  playback.seekSequenceSeconds(0.0);

  photo_editor::desktop::TimelineAudioOutput panned_output;
  if (panned_output.bufferOutput()->format().sampleRate() != 48000 ||
      panned_output.bufferOutput()->format().channelCount() != 2 ||
      panned_output.bufferOutput()->format().sampleFormat() != QAudioFormat::Float) {
    return fail("the decoded-buffer output should request a predictable stereo float format");
  }
  const QVector<float> stereo_samples = {0.50F, 0.50F, -0.50F, -0.50F};
  const auto centered_samples = panned_output.processStereoSamplesForTesting(stereo_samples);
  if (centered_samples.size() != stereo_samples.size() ||
      !nearly_equal(centered_samples.at(0), 0.50, 0.001) ||
      !nearly_equal(centered_samples.at(1), 0.50, 0.001) ||
      !nearly_equal(panned_output.peakLeft(), 0.50, 0.001) ||
      !nearly_equal(panned_output.peakRight(), 0.50, 0.001) ||
      panned_output.truePeakLeft() + 0.0001 < panned_output.peakLeft() ||
      panned_output.truePeakRight() + 0.0001 < panned_output.peakRight() ||
      !nearly_equal(panned_output.rmsLeft(), 0.50, 0.001) ||
      !nearly_equal(panned_output.rmsRight(), 0.50, 0.001) ||
      panned_output.phaseCorrelation() < 0.99 ||
      panned_output.phasePoints().isEmpty()) {
    return fail("center pan should preserve stereo samples and publish meters plus decoded stereo phase data");
  }
  photo_editor::desktop::TimelineAudioOutput anti_phase_output;
  const QVector<float> anti_phase_samples = {
      0.50F, -0.50F, -0.50F, 0.50F, 0.25F, -0.25F, -0.25F, 0.25F,
  };
  Q_UNUSED(anti_phase_output.processStereoSamplesForTesting(anti_phase_samples));
  if (anti_phase_output.phaseCorrelation() > -0.99 ||
      anti_phase_output.phasePoints().size() != 4) {
    return fail("the phase scope should expose bounded post-processing stereo samples and anti-phase correlation");
  }
  panned_output.resetMeters();
  const QVector<float> intersample_peak_samples = {
      -0.95F, -0.95F, 0.95F, 0.95F,
       0.95F,  0.95F, -0.95F, -0.95F,
  };
  Q_UNUSED(panned_output.processStereoSamplesForTesting(intersample_peak_samples));
  if (panned_output.peakLeft() > 0.951 ||
      panned_output.truePeakLeft() <= panned_output.peakLeft() ||
      !panned_output.clipping()) {
    return fail("four-times cubic oversampling should detect an intersample true-peak overload above sample peak");
  }
  panned_output.resetMeters();
  if (panned_output.peakLeft() != 0.0 || panned_output.truePeakLeft() != 0.0 ||
      panned_output.rmsLeft() != 0.0 || panned_output.phaseCorrelation() != 0.0 ||
      !panned_output.phasePoints().isEmpty() || panned_output.clipping()) {
    return fail("meter reset should clear peak, true-peak, RMS, phase, and overload hold together");
  }

  photo_editor::desktop::TimelineAudioOutput channel_output;
  const QVector<float> asymmetric_stereo_samples = {
      0.80F, 0.20F, -0.60F, -0.10F,
  };
  channel_output.setChannelMode(QStringLiteral("mono-left"));
  auto mapped_samples =
      channel_output.processStereoSamplesForTesting(asymmetric_stereo_samples);
  if (!nearly_equal(mapped_samples.at(0), 0.80, 0.001) ||
      !nearly_equal(mapped_samples.at(1), 0.80, 0.001) ||
      !nearly_equal(mapped_samples.at(2), -0.60, 0.001) ||
      !nearly_equal(mapped_samples.at(3), -0.60, 0.001)) {
    return fail("mono-left monitoring should route source channel one to both Program channels");
  }
  channel_output.setChannelMode(QStringLiteral("mono-right"));
  mapped_samples =
      channel_output.processStereoSamplesForTesting(asymmetric_stereo_samples);
  if (!nearly_equal(mapped_samples.at(0), 0.20, 0.001) ||
      !nearly_equal(mapped_samples.at(1), 0.20, 0.001) ||
      !nearly_equal(mapped_samples.at(2), -0.10, 0.001) ||
      !nearly_equal(mapped_samples.at(3), -0.10, 0.001)) {
    return fail("mono-right monitoring should route source channel two to both Program channels");
  }
  channel_output.setChannelMode(QStringLiteral("stereo"));
  channel_output.setSwapChannels(true);
  channel_output.setInvertLeftPhase(true);
  channel_output.setInvertRightPhase(true);
  mapped_samples =
      channel_output.processStereoSamplesForTesting(asymmetric_stereo_samples);
  if (!nearly_equal(mapped_samples.at(0), -0.20, 0.001) ||
      !nearly_equal(mapped_samples.at(1), -0.80, 0.001) ||
      !nearly_equal(mapped_samples.at(2), 0.10, 0.001) ||
      !nearly_equal(mapped_samples.at(3), 0.60, 0.001)) {
    return fail("Program monitoring should apply channel swap and independent polarity inversion before processing");
  }
  channel_output.setChannelMode(QStringLiteral("unsupported-mode"));
  if (channel_output.channelMode() != QStringLiteral("source")) {
    return fail("unsupported channel interpretations should safely fall back to Source");
  }
  panned_output.setPan(1.0);
  const auto right_samples = panned_output.processStereoSamplesForTesting(stereo_samples);
  if (qAbs(right_samples.at(0)) > 0.001 || right_samples.at(1) < 0.70F ||
      right_samples.at(1) > 0.72F) {
    return fail("full-right pan should silence left and apply the shared center-referenced pan law");
  }
  panned_output.setPan(0.0);
  panned_output.setVolume(0.25);
  const auto attenuated_samples =
      panned_output.processStereoSamplesForTesting(stereo_samples);
  if (!nearly_equal(attenuated_samples.at(0), 0.125, 0.001) ||
      !nearly_equal(attenuated_samples.at(1), 0.125, 0.001)) {
    return fail("decoded-buffer preview should apply the exact linear gain supplied by clip, track, and fade state");
  }
  panned_output.setPan(1.0);
  panned_output.setVolume(2.0);
  const auto clipped_samples = panned_output.processStereoSamplesForTesting(stereo_samples);
  Q_UNUSED(clipped_samples);
  if (!panned_output.clipping()) {
    return fail("the decoded-buffer output should latch clipping when panned gain exceeds full scale");
  }

  QVector<float> low_tone_samples;
  low_tone_samples.reserve(48000 * 2);
  for (auto frame = 0; frame < 48000; ++frame) {
    const auto sample = static_cast<float>(
        0.05 * std::sin(2.0 * 3.14159265358979323846 * 120.0 * frame / 48000.0));
    low_tone_samples.push_back(sample);
    low_tone_samples.push_back(sample);
  }
  photo_editor::desktop::TimelineAudioOutput flat_eq_output;
  const auto flat_low_tone = flat_eq_output.processStereoSamplesForTesting(low_tone_samples);
  photo_editor::desktop::TimelineAudioOutput boosted_eq_output;
  boosted_eq_output.setEqLowDb(12.0);
  const auto boosted_low_tone = boosted_eq_output.processStereoSamplesForTesting(low_tone_samples);
  if (stereo_rms(boosted_low_tone, 4096) < stereo_rms(flat_low_tone, 4096) * 3.0) {
    return fail("decoded-buffer preview should apply the real low-band EQ before metering");
  }

  QVector<float> loud_samples(48000 * 2, 0.8F);
  photo_editor::desktop::TimelineAudioOutput compressed_output;
  compressed_output.setCompressorEnabled(true);
  compressed_output.setCompressorThresholdDb(-18.0);
  compressed_output.setCompressorRatio(8.0);
  const auto compressed_samples = compressed_output.processStereoSamplesForTesting(loud_samples);
  if (stereo_rms(compressed_samples, 24000) > 0.30) {
    return fail("decoded-buffer preview compressor should reduce sustained audio above threshold");
  }

  QVector<float> contaminated_dialogue;
  contaminated_dialogue.reserve(48000 * 2);
  for (auto frame = 0; frame < 48000; ++frame) {
    const auto voice = 0.18 * std::sin(
        2.0 * 3.14159265358979323846 * 440.0 * frame / 48000.0);
    const auto hum = 0.15 * std::sin(
        2.0 * 3.14159265358979323846 * 60.0 * frame / 48000.0);
    const auto side_noise = 0.12 * std::sin(
        2.0 * 3.14159265358979323846 * 1837.0 * frame / 48000.0);
    contaminated_dialogue.push_back(static_cast<float>(voice + hum + side_noise));
    contaminated_dialogue.push_back(static_cast<float>(voice + hum - side_noise));
  }
  photo_editor::desktop::TimelineAudioOutput cleaned_dialogue_output;
  cleaned_dialogue_output.setDialogueCleanupEnabled(true);
  cleaned_dialogue_output.setVoiceIsolationAmount(0.80);
  cleaned_dialogue_output.setNoiseReductionAmount(0.0);
  cleaned_dialogue_output.setDereverbAmount(0.0);
  cleaned_dialogue_output.setHumRemovalAmount(1.0);
  cleaned_dialogue_output.setHumFrequencyHz(60);
  cleaned_dialogue_output.setAmbiencePreservation(0.0);
  const auto cleaned_dialogue =
      cleaned_dialogue_output.processStereoSamplesForTesting(contaminated_dialogue);
  const auto original_side = stereo_component_rms(contaminated_dialogue, true, 12000);
  const auto cleaned_side = stereo_component_rms(cleaned_dialogue, true, 12000);
  const auto original_hum = mid_tone_amplitude(contaminated_dialogue, 60.0, 12000);
  const auto cleaned_hum = mid_tone_amplitude(cleaned_dialogue, 60.0, 12000);
  const auto original_voice = mid_tone_amplitude(contaminated_dialogue, 440.0, 12000);
  const auto cleaned_voice = mid_tone_amplitude(cleaned_dialogue, 440.0, 12000);
  if (cleaned_side > original_side * 0.45 ||
      cleaned_hum > original_hum * 0.25 ||
      cleaned_voice < original_voice * 0.75) {
    return fail("dialogue cleanup preview should isolate centered voice and reject 60 Hz hum without erasing speech");
  }

  QVector<float> low_level_noise;
  low_level_noise.reserve(48000 * 2);
  for (auto frame = 0; frame < 48000; ++frame) {
    const auto noise = static_cast<float>(
        0.010 * std::sin(
            2.0 * 3.14159265358979323846 * 1733.0 * frame / 48000.0));
    low_level_noise.push_back(noise);
    low_level_noise.push_back(-noise);
  }
  photo_editor::desktop::TimelineAudioOutput reduced_noise_output;
  reduced_noise_output.setDialogueCleanupEnabled(true);
  reduced_noise_output.setVoiceIsolationAmount(0.0);
  reduced_noise_output.setNoiseReductionAmount(1.0);
  reduced_noise_output.setDereverbAmount(0.0);
  reduced_noise_output.setHumRemovalAmount(0.0);
  reduced_noise_output.setArtifactSmoothing(0.85);
  reduced_noise_output.setAmbiencePreservation(0.0);
  const auto reduced_noise =
      reduced_noise_output.processStereoSamplesForTesting(low_level_noise);
  if (stereo_rms(reduced_noise, 24000) > stereo_rms(low_level_noise, 24000) * 0.65) {
    return fail("dialogue cleanup preview should suppress stable low-level noise with smoothed gain changes");
  }

  photo_editor::desktop::TimelineAudioOutput ducked_output;
  ducked_output.setDuckingGainDb(-12.0);
  const QVector<float> duck_source = {0.5F, 0.5F, -0.5F, -0.5F};
  const auto ducked_samples = ducked_output.processStereoSamplesForTesting(duck_source);
  if (!nearly_equal(ducked_samples.at(0), 0.1256, 0.002) ||
      !nearly_equal(ducked_samples.at(1), 0.1256, 0.002)) {
    return fail("decoded-buffer preview should apply the document's dialogue duck gain");
  }

  if (!QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
    // Resource-pressure behavior has focused controller coverage. Keep this
    // playback/proxy proof deterministic across loaded CI hosts.
    document.setCachePressureLevelForTesting(QStringLiteral("normal"));
    document.generateSelectedClipProxy();
    QElapsedTimer proxy_wait;
    proxy_wait.start();
    while (proxy_wait.elapsed() < 30000 &&
           document.currentClip().value(QStringLiteral("proxyStatus")).toString() !=
               QStringLiteral("ready") &&
           document.currentClip().value(QStringLiteral("proxyStatus")).toString() !=
               QStringLiteral("failed")) {
      app.processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(5);
    }
    const auto proxied_clip = document.currentClip();
    const auto original_source = proxied_clip.value(QStringLiteral("originalSourceUrl")).toString();
    const auto proxy_source = proxied_clip.value(QStringLiteral("proxySourceUrl")).toString();
    if (proxied_clip.value(QStringLiteral("proxyStatus")).toString() != QStringLiteral("ready") ||
        proxy_source.isEmpty()) {
      std::cerr << "proxy status="
                << proxied_clip.value(QStringLiteral("proxyStatus")).toString().toStdString()
                << " error="
                << proxied_clip.value(QStringLiteral("proxyError")).toString().toStdString()
                << " pressure="
                << document.cacheBudgetState()
                       .value(QStringLiteral("pressureLevel")).toString().toStdString()
                << " priority="
                << QJsonDocument::fromVariant(document.backgroundWorkPriorityState())
                       .toJson(QJsonDocument::Compact).toStdString()
                << " jobs="
                << QJsonDocument::fromVariant(document.timelineBackgroundJobs())
                       .toJson(QJsonDocument::Compact).toStdString()
                << '\n';
      return fail("adaptive playback fixture should generate a real ready proxy");
    }
    if (playback.previewQualityMode() != QStringLiteral("auto") ||
        playback.activeSourceUrl() != original_source || playback.usingProxy() ||
        playback.effectivePreviewQuality() != QStringLiteral("original") ||
        playback.adaptiveQualityActive() || playback.playbackHealth() != QStringLiteral("ready") ||
        !nearly_equal(playback.bufferProgress(), 1.0)) {
      return fail("auto quality should retain original media while playback is paused");
    }
    playback.setInteractionActive(true);
    if (playback.activeSourceUrl() != proxy_source || !playback.usingProxy() ||
        playback.effectivePreviewQuality() != QStringLiteral("proxy") ||
        !playback.adaptiveQualityActive()) {
      return fail("auto quality should switch to a ready proxy during active interaction");
    }
    playback.setInteractionActive(false);
    if (playback.activeSourceUrl() != original_source || playback.usingProxy()) {
      return fail("auto quality should restore original media when interaction ends");
    }
    playback.setPreviewQualityMode(QStringLiteral("proxy"));
    if (playback.activeSourceUrl() != proxy_source || !playback.usingProxy() ||
        playback.adaptiveQualityActive()) {
      return fail("proxy quality should prefer the ready proxy");
    }
    playback.setPreviewQualityMode(QStringLiteral("full"));
    if (playback.activeSourceUrl() != original_source || playback.usingProxy()) {
      return fail("full quality should always use original media");
    }
    playback.setPreviewQualityMode(QStringLiteral("half"));
    if (playback.activeSourceUrl() != original_source || playback.usingProxy() ||
        playback.effectivePreviewQuality() != QStringLiteral("half")) {
      return fail("half quality should retain originals while publishing the requested Program resolution");
    }
    playback.setPreviewQualityMode(QStringLiteral("quarter"));
    if (playback.activeSourceUrl() != original_source || playback.usingProxy() ||
        playback.effectivePreviewQuality() != QStringLiteral("quarter")) {
      return fail("quarter quality should retain originals while publishing the requested Program resolution");
    }
    playback.setPreviewQualityMode(QStringLiteral("auto"));

    const auto cache_ranges = document.renderCacheRanges();
    if (cache_ranges.isEmpty()) {
      return fail("adaptive playback fixture should expose deterministic cache ranges");
    }
    const auto cache_signature =
        cache_ranges.first().toMap().value(QStringLiteral("signature")).toString();
    const auto cache_directory = QDir(photo_editor::desktop::AppBootstrap::cacheLocation())
                                     .filePath(QStringLiteral("timeline-render-cache"));
    QDir().mkpath(cache_directory);
    const auto timeline_id = document.timelineAssetId().mid(
        QStringLiteral("timeline:").size());
    const auto cache_path =
        QDir(cache_directory).filePath(
            QStringLiteral("%1-%2.mp4").arg(timeline_id, cache_signature));
    if (touch_file(cache_path).isEmpty()) {
      return fail("adaptive playback cache placeholder should be created");
    }
    playback.setInteractionActive(true);
    if (!playback.usingRenderCache() || playback.usingProxy() ||
        playback.activeSourceUrl() != QUrl::fromLocalFile(cache_path).toString() ||
        playback.effectivePreviewQuality() != QStringLiteral("render-cache") ||
        !playback.adaptiveQualityActive()) {
      return fail("Auto playback should prefer a valid composited range over per-clip proxies");
    }
    playback.play();
    playback.recordPlaybackTimingSample(0, 150);
    if (playback.adaptiveDetailLevel() != QStringLiteral("half") ||
        !nearly_equal(playback.effectivePreviewScale(), 0.5) ||
        playback.adaptiveQualityReason().isEmpty() ||
        !playback.adaptiveQualityActive()) {
      return fail("Auto quality should react to measured timing lag with a disclosed temporary half-resolution Program preview");
    }
    playback.recordPlaybackTimingSample(0, 150);
    playback.recordPlaybackTimingSample(0, 150);
    if (playback.adaptiveDetailLevel() != QStringLiteral("quarter") ||
        !nearly_equal(playback.effectivePreviewScale(), 0.25)) {
      return fail("repeated playback pressure should step Auto detail down to quarter resolution without changing export media");
    }
    for (auto sample = 0; sample < 12; ++sample) {
      playback.recordPlaybackTimingSample(50, 50);
    }
    if (playback.adaptiveDetailLevel() != QStringLiteral("half")) {
      return fail("adaptive playback should require a healthy hysteresis window before restoring one quality step");
    }
    for (auto sample = 0; sample < 12; ++sample) {
      playback.recordPlaybackTimingSample(50, 50);
    }
    if (playback.adaptiveDetailLevel() != QStringLiteral("full") ||
        !nearly_equal(playback.effectivePreviewScale(), 1.0)) {
      return fail("sustained healthy playback should restore full requested Program detail");
    }
    playback.recordPlaybackTimingSample(0, 150);
    const auto cache_diagnostic = playback.playbackDiagnostics();
    if (cache_diagnostic.value(QStringLiteral("cause")).toString() !=
            QStringLiteral("cache") ||
        cache_diagnostic.value(QStringLiteral("clipId")).toString().isEmpty() ||
        !cache_diagnostic.value(QStringLiteral("action")).toString().contains(
            QStringLiteral("rebuild the preview cache"))) {
      return fail("dropped-frame diagnostics should identify the exact clip and active render-cache condition");
    }
    playback.pause();
    if (playback.adaptiveDetailLevel() != QStringLiteral("full") ||
        !playback.adaptiveQualityReason().isEmpty() ||
        !nearly_equal(playback.effectivePreviewScale(), 1.0)) {
      return fail("pausing playback should immediately restore the requested full Auto detail and clear its temporary reason");
    }
    playback.resetPlaybackDiagnostics();
    QFile::remove(cache_path);
    playback.setInteractionActive(false);
  }

  document.selectClip(0);
  document.addSelectedClipEffect(QStringLiteral("Exposure"));
  playback.play();
  playback.recordPlaybackTimingSample(0, 150);
  const auto effect_diagnostic = playback.playbackDiagnostics();
  if (effect_diagnostic.value(QStringLiteral("cause")).toString() !=
          QStringLiteral("effect") ||
      effect_diagnostic.value(QStringLiteral("detail")).toString() !=
          QStringLiteral("Exposure") ||
      !effect_diagnostic.value(QStringLiteral("action")).toString().contains(
          QStringLiteral("bypass 'Exposure'"))) {
    return fail("dropped-frame diagnostics should name the active effect and an actionable bypass/cache remedy");
  }
  playback.pause();
  document.removeSelectedClipEffect(QStringLiteral("Exposure"));
  playback.resetPlaybackDiagnostics();
  playback.play();
  playback.recordPlaybackTimingSample(0, 150);
  const auto decoder_diagnostic = playback.playbackDiagnostics();
  if (decoder_diagnostic.value(QStringLiteral("cause")).toString() !=
          QStringLiteral("decoder") ||
      decoder_diagnostic.value(QStringLiteral("decoder")).toString().isEmpty() ||
      decoder_diagnostic.value(QStringLiteral("sourcePath")).toString() != first_video) {
    return fail("dropped-frame diagnostics should identify the decoder and exact active source");
  }
  playback.pause();

  const auto offline_diagnostic_path =
      touch_file(workspace.filePath(QStringLiteral("diagnostic-offline.mp4")));
  TimelineDocumentController offline_diagnostic_document;
  offline_diagnostic_document.createTimeline(QStringLiteral("Offline Diagnostics"));
  offline_diagnostic_document.addClipFromGallery(
      QStringLiteral("video:offline-diagnostic"), offline_diagnostic_path,
      QStringLiteral("Offline Diagnostic Clip"), 2.0, QString{});
  QFile::remove(offline_diagnostic_path);
  TimelinePlaybackController offline_diagnostic_playback(&offline_diagnostic_document);
  offline_diagnostic_playback.play();
  offline_diagnostic_playback.recordPlaybackTimingSample(0, 150);
  const auto storage_diagnostic =
      offline_diagnostic_playback.playbackDiagnostics();
  if (storage_diagnostic.value(QStringLiteral("cause")).toString() !=
          QStringLiteral("storage") ||
      storage_diagnostic.value(QStringLiteral("clipLabel")).toString() !=
          QStringLiteral("Offline Diagnostic Clip") ||
      !storage_diagnostic.value(QStringLiteral("action")).toString().contains(
          QStringLiteral("Batch Relink"))) {
    return fail("dropped-frame diagnostics should identify offline storage and route directly to batch relink");
  }
  offline_diagnostic_playback.pause();

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

  playback.seekSequenceSeconds(1.0);
  playback.shuttleForward();
  if (!document.sequencePlaying() || !playback.shuttling() ||
      !nearly_equal(playback.shuttleRate(), 1.0)) {
    return fail("L should begin forward shuttle playback at 1x");
  }
  playback.shuttleForward();
  playback.shuttleForward();
  playback.shuttleForward();
  playback.shuttleForward();
  if (!nearly_equal(playback.shuttleRate(), 8.0)) {
    return fail("repeated L presses should climb and cap the forward shuttle at 8x");
  }
  QEventLoop forward_shuttle_wait;
  QTimer::singleShot(45, &forward_shuttle_wait, &QEventLoop::quit);
  forward_shuttle_wait.exec();
  playback.stopShuttle();
  if (document.sequencePlaying() || playback.shuttling() ||
      !nearly_equal(playback.shuttleRate(), 0.0) ||
      document.sequencePlayheadSeconds() <= 1.15) {
    return fail("8x shuttle should advance the authoritative sequence clock and K should stop it");
  }

  playback.seekSequenceSeconds(2.0);
  playback.shuttleReverse();
  playback.shuttleReverse();
  if (!document.sequencePlaying() || !nearly_equal(playback.shuttleRate(), -2.0)) {
    return fail("repeated J presses should start reverse playback and step to -2x");
  }
  QEventLoop reverse_shuttle_wait;
  QTimer::singleShot(55, &reverse_shuttle_wait, &QEventLoop::quit);
  reverse_shuttle_wait.exec();
  playback.stopShuttle();
  if (document.sequencePlayheadSeconds() >= 1.95) {
    return fail("reverse shuttle should move the authoritative sequence clock backward");
  }
  playback.seekSequenceSeconds(1.5);
  playback.shuttleForward();
  playback.shuttleForward();
  playback.shuttleReverse();
  if (!document.sequencePlaying() || !playback.shuttling() ||
      !nearly_equal(playback.shuttleRate(), -1.0)) {
    return fail("J should reverse an active forward shuttle immediately at the first reverse speed");
  }
  playback.shuttleForward();
  if (!document.sequencePlaying() || !nearly_equal(playback.shuttleRate(), 1.0)) {
    return fail("L should reverse an active reverse shuttle immediately without an intermediate stop");
  }
  playback.stopShuttle();
  playback.shuttleForward();
  playback.stepFrames(1);
  if (document.sequencePlaying() || !nearly_equal(playback.shuttleRate(), 0.0)) {
    return fail("frame stepping should stop shuttle playback before moving exactly one frame");
  }

  playback.seekSequenceSeconds(0.10);
  playback.shuttleReverse();
  playback.shuttleReverse();
  playback.shuttleReverse();
  playback.shuttleReverse();
  QEventLoop reverse_boundary_wait;
  QTimer::singleShot(45, &reverse_boundary_wait, &QEventLoop::quit);
  reverse_boundary_wait.exec();
  if (document.sequencePlaying() || playback.shuttling() ||
      !nearly_equal(playback.shuttleRate(), 0.0) ||
      !nearly_equal(document.sequencePlayheadSeconds(), 0.0, 0.01)) {
    return fail("reverse shuttle should stop cleanly at sequence start without running a pinned timer");
  }

  playback.seekSequenceSeconds(document.totalDurationSecondsValue());
  playback.shuttleReverse();
  if (!document.sequencePlaying() || playback.shuttleRate() >= 0.0 ||
      document.sequencePlayheadSeconds() < document.totalDurationSecondsValue() - 0.10) {
    return fail("J at sequence end should shuttle backward from the final frame instead of wrapping to start");
  }
  playback.stopShuttle();

  TimelineDocumentController rate_document;
  rate_document.createTimeline(QStringLiteral("Rate Truth Timeline"));
  rate_document.addClipFromGallery(
      QStringLiteral("video:rate"), first_video, QStringLiteral("Fast Clip"), 4.0,
      QStringLiteral("poster://rate"));
  rate_document.setSelectedClipPlaybackRate(2.0);
  TimelinePlaybackController rate_playback(&rate_document);
  if (!nearly_equal(rate_playback.activePlaybackRate(), 2.0) ||
      !nearly_equal(rate_document.totalDurationSecondsValue(), 2.0)) {
    return fail("the Program player rate and sequence duration should share one speed mapping");
  }
  rate_playback.seekSequenceSeconds(1.25);
  if (!nearly_equal(rate_document.currentClipPlayheadSeconds(), 2.5)) {
    return fail("sequence seeking should map through playback rate into source time");
  }
  rate_playback.seekCurrentClipSeconds(3.0);
  if (!nearly_equal(rate_document.sequencePlayheadSeconds(), 1.5)) {
    return fail("source seeking should map back through playback rate into sequence time");
  }
  if (!rate_document.applySelectedClipRetimePreset(QStringLiteral("reverse"))) {
    return fail("the playback fixture should accept reverse retiming");
  }
  rate_playback.seekSequenceSeconds(0.5);
  if (!nearly_equal(rate_playback.activePlaybackRate(), -2.0) ||
      !nearly_equal(rate_document.currentClipPlayheadSeconds(), 3.0)) {
    return fail("Program playback should publish the signed segment rate and seek reverse source time exactly");
  }
  rate_playback.seekSequenceSeconds(1.0);
  if (!rate_document.applySelectedClipRetimePreset(QStringLiteral("freeze-frame"))) {
    return fail("the playback fixture should accept a freeze frame");
  }
  rate_playback.seekSequenceSeconds(1.5);
  if (!nearly_equal(rate_playback.activePlaybackRate(), 0.0) ||
      !rate_document.currentClip().value(QStringLiteral("retimeAudioMuted")).toBool()) {
    return fail("Program playback should hold and silence the exact freeze segment");
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
  document.moveSelectedClipToTrackRow(1, 1.0);
  playback.seekSequenceSeconds(1.5);
  if (document.currentClip().value(QStringLiteral("trackLabel")).toString() != QStringLiteral("V2") ||
      !nearly_equal(document.currentClipPlayheadSeconds(), 0.5)) {
    return fail("playback seeking should prefer the higher active video track at the current sequence time");
  }

  TimelineDocumentController gap_document;
  gap_document.createTimeline(QStringLiteral("Gap Playback Timeline"));
  gap_document.addClipFromGallery(QStringLiteral("video:gap-1"), first_video, QStringLiteral("Before Gap"), 4.0, QStringLiteral("poster://gap-one"));
  gap_document.addClipFromGallery(QStringLiteral("video:gap-2"), second_video, QStringLiteral("After Gap"), 4.0, QStringLiteral("poster://gap-two"));
  gap_document.selectClip(1);
  gap_document.moveSelectedClipToTrackRow(0, 6.0);
  TimelinePlaybackController gap_playback(&gap_document);

  gap_playback.seekSequenceSeconds(4.5);
  if (gap_document.currentIndex() != -1 || !gap_document.playbackSourceUrl().isEmpty()) {
    return fail("seeking into a timeline gap should expose a black program interval without selecting media");
  }
  gap_playback.play();
  if (!gap_document.sequencePlaying()) {
    return fail("sequence playback should be able to start inside a timeline gap");
  }
  gap_document.syncGapPlaybackPosition(5.25);
  if (!gap_document.sequencePlaying() || gap_document.currentIndex() != -1 ||
      !nearly_equal(gap_document.sequencePlayheadSeconds(), 5.25)) {
    return fail("gap playback should advance the sequence clock without inventing a selected clip");
  }
  gap_document.syncGapPlaybackPosition(6.25);
  if (!gap_document.sequencePlaying() || gap_document.currentIndex() != 1 ||
      !nearly_equal(gap_document.currentClipPlayheadSeconds(), 0.25)) {
    return fail("gap playback should resume the next active clip at its real sequence time");
  }

  gap_playback.seekSequenceSeconds(3.9);
  gap_playback.play();
  if (!gap_document.advancePlaybackAfterClipEnd() || gap_document.currentIndex() != -1 ||
      !gap_document.sequencePlaying() || !nearly_equal(gap_document.sequencePlayheadSeconds(), 4.0)) {
    return fail("clip-boundary playback should enter a real gap instead of skipping directly to the next clip");
  }
  gap_playback.pause();

  TimelineDocumentController clock_document;
  clock_document.createTimeline(QStringLiteral("Authoritative Clock Timeline"));
  clock_document.addClipFromGallery(
      QStringLiteral("video:clock-1"), first_video, QStringLiteral("Clock One"), 0.06,
      QStringLiteral("poster://clock-one"));
  clock_document.addClipFromGallery(
      QStringLiteral("video:clock-2"), second_video, QStringLiteral("Clock Two"), 0.12,
      QStringLiteral("poster://clock-two"));
  TimelinePlaybackController clock_playback(&clock_document);
  clock_playback.seekSequenceSeconds(0.0);
  clock_playback.play();
  QEventLoop clock_wait;
  QTimer::singleShot(100, &clock_wait, &QEventLoop::quit);
  clock_wait.exec();
  clock_playback.pause();
  if (clock_document.currentIndex() != 1 || clock_document.sequencePlayheadSeconds() < 0.075 ||
      clock_document.sequencePlayheadSeconds() > 0.16) {
    return fail("the authoritative sequence clock should advance across a playable clip boundary in real time");
  }

  playback.pause();
  playback.seekSequenceSeconds(4.5);
  const auto recovery_hash = document.deterministicDocumentHash();
  const auto recovery_source = playback.activeSourceUrl();
  const auto recovery_playhead = document.sequencePlayheadSeconds();
  const QStringList recoverable_losses = {
      QStringLiteral("decoder"),
      QStringLiteral("gpu"),
      QStringLiteral("audio-device"),
      QStringLiteral("external-display"),
  };
  for (const auto& loss : recoverable_losses) {
    playback.recoverRuntimeFailure(loss, QStringLiteral("Injected test loss."));
    if (!playback.runtimeRecoveryBusy() || playback.runtimeRecoveryKind() != loss) {
      return fail("runtime loss should immediately publish one truthful bounded recovery state");
    }
    app.processEvents(QEventLoop::AllEvents, 20);
    if (playback.runtimeRecoveryBusy() ||
        !playback.runtimeRecoveryStatus().contains(QStringLiteral("without changing the edit")) ||
        document.deterministicDocumentHash() != recovery_hash ||
        playback.activeSourceUrl() != recovery_source ||
        !nearly_equal(document.sequencePlayheadSeconds(), recovery_playhead)) {
      return fail("decoder, GPU, audio-device, and display recovery should preserve project, source, playhead, and clear busy state");
    }
  }
  if (playback.runtimeRecoveryCount() != static_cast<int>(recoverable_losses.size())) {
    return fail("runtime recovery should count each completed device-loss intervention");
  }

  return EXIT_SUCCESS;
}
