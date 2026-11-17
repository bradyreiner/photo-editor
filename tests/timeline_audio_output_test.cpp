#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

#include <QApplication>
#include <QAudioBuffer>
#include <QAudioFormat>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>

#include "timeline_audio_output.h"
#include "timeline_audio_sink_policy.h"
#include "timeline_audio_math.h"
#include "timeline_document_controller.h"
#include "timeline_voiceover_recorder.h"

namespace {

using photo_editor::desktop::TimelineAudioOutput;
using photo_editor::desktop::TimelineDocumentController;
using photo_editor::desktop::TimelineVoiceoverWaveWriter;

constexpr int kSampleRate = 48000;
constexpr double kPi = 3.14159265358979323846;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QVector<float> stereo_tone(double frequency, double amplitude, int frames = kSampleRate) {
  QVector<float> samples;
  samples.reserve(frames * 2);
  for (auto frame = 0; frame < frames; ++frame) {
    const auto sample = static_cast<float>(
        amplitude * std::sin(2.0 * kPi * frequency * frame / kSampleRate));
    samples.push_back(sample);
    samples.push_back(sample);
  }
  return samples;
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

double tone_amplitude(const QVector<float>& samples, double frequency, int skip_frames = 0) {
  const auto start_frame = qMin(samples.size() / 2, qMax(0, skip_frames));
  auto sine_sum = 0.0;
  auto cosine_sum = 0.0;
  auto count = 0;
  for (auto frame = start_frame; frame * 2 + 1 < samples.size(); ++frame) {
    const auto sample = (static_cast<double>(samples.at(frame * 2)) +
                         static_cast<double>(samples.at(frame * 2 + 1))) * 0.5;
    const auto phase = 2.0 * kPi * frequency * frame / kSampleRate;
    sine_sum += sample * std::sin(phase);
    cosine_sum += sample * std::cos(phase);
    ++count;
  }
  return count > 0
             ? 2.0 * std::sqrt(sine_sum * sine_sum + cosine_sum * cosine_sum) /
                   static_cast<double>(count)
             : 0.0;
}

QString write_fixture(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return {};
  }
  file.write("timeline audio automation fixture");
  file.close();
  return path;
}

quint32 read_u32_le(const QByteArray& bytes, qsizetype offset) {
  return static_cast<quint32>(static_cast<quint8>(bytes.at(offset))) |
         (static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 1))) << 8U) |
         (static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 2))) << 16U) |
         (static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 3))) << 24U);
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  qputenv("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS", QByteArrayLiteral("1"));
  QApplication app(argc, argv);
  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    return fail("temporary workspace should be created for audio persistence coverage");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", workspace.filePath(QStringLiteral("app-data")).toUtf8());

  constexpr qsizetype kStereoBytesPerFrame = 2 * static_cast<qsizetype>(sizeof(float));
  const auto stereo_frames = [](int first_frame, int frame_count) {
    QByteArray bytes;
    for (auto frame = first_frame; frame < first_frame + frame_count; ++frame) {
      const float samples[] = {static_cast<float>(frame), -static_cast<float>(frame)};
      bytes.append(
          reinterpret_cast<const char*>(samples),
          static_cast<qsizetype>(sizeof(samples)));
    }
    return bytes;
  };
  QByteArray bounded_audio;
  const auto oversized_audio = stereo_frames(0, 10000);
  photo_editor::desktop::appendBoundedAudioFrames(
      &bounded_audio, oversized_audio, 3, kStereoBytesPerFrame);
  if (bounded_audio != stereo_frames(9997, 3) ||
      bounded_audio.size() != 3 * kStereoBytesPerFrame) {
    return fail("an oversized audio callback should retain only the newest complete frames");
  }
  bounded_audio.clear();
  photo_editor::desktop::appendBoundedAudioFrames(
      &bounded_audio, stereo_frames(10, 2), 3, kStereoBytesPerFrame);
  photo_editor::desktop::appendBoundedAudioFrames(
      &bounded_audio, stereo_frames(12, 2), 3, kStereoBytesPerFrame);
  auto callback_with_partial_frame = stereo_frames(20, 2);
  callback_with_partial_frame.append("x", 1);
  photo_editor::desktop::appendBoundedAudioFrames(
      &bounded_audio, callback_with_partial_frame, 3, kStereoBytesPerFrame);
  if (bounded_audio != stereo_frames(13, 1) + stereo_frames(20, 2) ||
      bounded_audio.size() % kStereoBytesPerFrame != 0) {
    return fail("repeated callbacks should drop oldest complete frames without misaligning stereo samples");
  }
  if (photo_editor::desktop::audioSinkErrorNeedsRecovery(QAudio::NoError) ||
      photo_editor::desktop::audioSinkErrorNeedsRecovery(QAudio::UnderrunError) ||
      !photo_editor::desktop::audioSinkErrorNeedsRecovery(QAudio::OpenError) ||
      !photo_editor::desktop::audioSinkErrorNeedsRecovery(QAudio::IOError) ||
      !photo_editor::desktop::audioSinkErrorNeedsRecovery(QAudio::FatalError)) {
    return fail("audio starvation should keep the sink, while device and I/O failures trigger recovery");
  }

  TimelineAudioOutput positive_gain;
  positive_gain.setVolume(std::pow(10.0, 48.0 / 20.0));
  const auto boosted = positive_gain.processStereoSamplesForTesting({0.003F, 0.003F});
  const auto expected_boost = 0.003 * std::pow(10.0, 48.0 / 20.0);
  if (boosted.size() != 2 || std::abs(boosted.at(0) - expected_boost) > 0.001F ||
      std::abs(positive_gain.volume() - std::pow(10.0, 48.0 / 20.0)) > 0.001) {
    return fail("combined +24 dB clip and track gain should reach Program DSP without early clipping");
  }

  TimelineAudioOutput callback_generation_gate;
  QAudioFormat callback_format;
  callback_format.setSampleRate(kSampleRate);
  callback_format.setChannelCount(2);
  callback_format.setSampleFormat(QAudioFormat::Float);
  const auto callback_tone = stereo_tone(440.0, 0.25, 4800);
  const auto callback_bytes = QByteArray(
      reinterpret_cast<const char*>(callback_tone.constData()),
      callback_tone.size() * static_cast<qsizetype>(sizeof(float)));
  const QAudioBuffer callback_buffer(callback_bytes, callback_format, 0);
  callback_generation_gate.bufferOutput()->audioBufferReceived(callback_buffer);
  callback_generation_gate.setOutputSuspended(true);
  callback_generation_gate.setOutputSuspended(false);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  if (callback_generation_gate.rmsLeft() != 0.0 ||
      callback_generation_gate.rmsRight() != 0.0) {
    return fail("an audio buffer queued before suspension must not leak into a later playback generation");
  }
  callback_generation_gate.bufferOutput()->audioBufferReceived(callback_buffer);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  if (callback_generation_gate.rmsLeft() < 0.1 ||
      callback_generation_gate.rmsRight() < 0.1) {
    return fail("audio buffers from the active generation should still reach Program meters");
  }

  TimelineAudioOutput bypass_headroom;
  bypass_headroom.setVolume(std::pow(10.0, 60.0 / 20.0));
  if (std::abs(bypass_headroom.volume() - 1000.0) > 0.1) {
    return fail("the live DSP should retain +48 dB clip/track gain plus up to +12 dB matched-bypass trim");
  }

  TimelineAudioOutput hard_clip;
  hard_clip.setVolume(16.0);
  const auto saturated = hard_clip.processStereoSamplesForTesting({0.25F, 0.25F});
  if (saturated.size() != 2 || saturated.at(0) != 1.0F || !hard_clip.clipping()) {
    return fail("unlimited positive gain should be explicitly sample-saturated and report clipping");
  }

  TimelineAudioOutput high_pass;
  high_pass.setHighPassEnabled(true);
  high_pass.setHighPassHz(800.0);
  const auto low_frequency = high_pass.processStereoSamplesForTesting(stereo_tone(100.0, 0.2));
  const auto passed_frequency = high_pass.processStereoSamplesForTesting(stereo_tone(2000.0, 0.2));
  if (tone_amplitude(low_frequency, 100.0, 6000) > 0.025 ||
      tone_amplitude(passed_frequency, 2000.0, 6000) < 0.17) {
    return fail("live high-pass should attenuate sub-cutoff energy while preserving voice-band tone");
  }

  TimelineAudioOutput low_pass;
  low_pass.setLowPassEnabled(true);
  low_pass.setLowPassHz(2000.0);
  const auto rejected_high_frequency =
      low_pass.processStereoSamplesForTesting(stereo_tone(12000.0, 0.2));
  const auto preserved_low_frequency =
      low_pass.processStereoSamplesForTesting(stereo_tone(1000.0, 0.2));
  if (tone_amplitude(rejected_high_frequency, 12000.0, 6000) > 0.025 ||
      tone_amplitude(preserved_low_frequency, 1000.0, 6000) < 0.17) {
    return fail("live low-pass should attenuate content above cutoff while preserving lower frequencies");
  }

  TimelineAudioOutput deesser;
  deesser.setDeEsserEnabled(true);
  deesser.setDeEsserAmount(1.0);
  const auto sibilance = deesser.processStereoSamplesForTesting(stereo_tone(6500.0, 0.25));
  const auto speech_tone = deesser.processStereoSamplesForTesting(stereo_tone(1000.0, 0.25));
  if (tone_amplitude(sibilance, 6500.0, 6000) >
          tone_amplitude(stereo_tone(6500.0, 0.25), 6500.0, 6000) * 0.72 ||
      tone_amplitude(speech_tone, 1000.0, 6000) < 0.21) {
    return fail("live de-esser should reduce sibilant-band energy without reducing a speech-band tone");
  }

  TimelineAudioOutput gate;
  gate.setGateEnabled(true);
  gate.setGateThresholdDb(-40.0);
  const auto gated_quiet_tone = gate.processStereoSamplesForTesting(stereo_tone(1000.0, 0.001));
  if (stereo_rms(gated_quiet_tone, 24000) > 0.0001) {
    return fail("live gate should attenuate sustained signal below its threshold");
  }
  gate.resetForDiscontinuity();
  const auto open_gate_tone = gate.processStereoSamplesForTesting(stereo_tone(1000.0, 0.2));
  if (stereo_rms(open_gate_tone, 24000) < 0.12) {
    return fail("gate envelope should open for sustained signal above its threshold");
  }

  TimelineAudioOutput limiter;
  limiter.setVolume(2.0);
  limiter.setLimiterEnabled(true);
  limiter.setLimiterCeilingDb(-6.0);
  const auto limited = limiter.processStereoSamplesForTesting(stereo_tone(997.0, 0.8));
  const auto ceiling = std::pow(10.0, -6.0 / 20.0);
  for (const auto sample : limited) {
    if (std::abs(static_cast<double>(sample)) > ceiling + 0.001) {
      return fail("live limiter should hold sample peaks at its configured ceiling after positive gain");
    }
  }

  TimelineAudioOutput bypass;
  bypass.setHighPassEnabled(true);
  bypass.setHighPassHz(800.0);
  bypass.setProcessingEnabled(false);
  const auto bypassed = bypass.processStereoSamplesForTesting(stereo_tone(100.0, 0.2));
  if (tone_amplitude(bypassed, 100.0, 6000) < 0.19) {
    return fail("processor bypass should preserve source tone while keeping output gain active");
  }

  TimelineAudioOutput stateful;
  stateful.setHighPassEnabled(true);
  stateful.setHighPassHz(800.0);
  QVector<float> impulse(128, 0.0F);
  impulse[0] = 1.0F;
  impulse[1] = 1.0F;
  Q_UNUSED(stateful.processStereoSamplesForTesting(impulse));
  stateful.resetForDiscontinuity();
  const auto post_seek_silence =
      stateful.processStereoSamplesForTesting(QVector<float>(256, 0.0F));
  if (stereo_rms(post_seek_silence) > 0.000001) {
    return fail("seek reset should clear filter history so audio before the seek cannot ring into the new position");
  }

  TimelineAudioOutput sample_rate_switch;
  sample_rate_switch.setHighPassEnabled(true);
  sample_rate_switch.setHighPassHz(800.0);
  QVector<float> short_impulse(128, 0.0F);
  short_impulse[0] = 1.0F;
  Q_UNUSED(sample_rate_switch.processStereoSamplesForTesting(short_impulse, 48000));
  const auto silence_after_rate_switch = sample_rate_switch.processStereoSamplesForTesting(
      QVector<float>(256, 0.0F), 44100);
  if (stereo_rms(silence_after_rate_switch) > 0.000001) {
    return fail("sample-rate change should reset filter history before processing the new stream format");
  }

  TimelineAudioOutput automated_filter;
  automated_filter.setHighPassEnabled(true);
  automated_filter.setHighPassHz(80.0);
  Q_UNUSED(automated_filter.processStereoSamplesForTesting(
      QVector<float>(4800 * 2, 0.25F)));
  automated_filter.setHighPassHz(120.0);
  const auto cutoff_transition = automated_filter.processStereoSamplesForTesting(
      QVector<float>(16 * 2, 0.25F));
  auto transition_peak = 0.0F;
  for (int index = 0; index < 8 * 2; ++index) {
    transition_peak = qMax(transition_peak, qAbs(cutoff_transition.at(index)));
  }
  if (transition_peak > 0.0001F) {
    return fail("automated filter cutoff changes should preserve settled filter history instead of generating a reset transient");
  }

  TimelineAudioOutput malformed;
  malformed.setVolume(std::numeric_limits<double>::quiet_NaN());
  malformed.setHighPassHz(std::numeric_limits<double>::quiet_NaN());
  const QVector<float> malformed_input = {
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::quiet_NaN(),
      0.25F,
      0.25F,
  };
  const auto finite_output = malformed.processStereoSamplesForTesting(malformed_input);
  malformed.setDialogueCleanupEnabled(true);
  const auto nan_value = std::numeric_limits<double>::quiet_NaN();
  malformed.setVoiceIsolationAmount(nan_value);
  malformed.setNoiseReductionAmount(nan_value);
  malformed.setDereverbAmount(nan_value);
  malformed.setHumRemovalAmount(nan_value);
  malformed.setArtifactSmoothing(nan_value);
  malformed.setAmbiencePreservation(nan_value);
  malformed.setDuckingGainDb(nan_value);
  const auto finite_cleanup_output = malformed.processStereoSamplesForTesting(
      QVector<float>(128, 0.1F));
  if (std::abs(malformed.volume() - 1.0) > 0.0001 ||
      std::abs(malformed.highPassHz() - 80.0) > 0.0001 ||
      std::abs(malformed.voiceIsolationAmount() - 0.65) > 0.0001 ||
      std::abs(malformed.noiseReductionAmount() - 0.35) > 0.0001 ||
      std::abs(malformed.dereverbAmount() - 0.25) > 0.0001 ||
      std::abs(malformed.humRemovalAmount() - 0.5) > 0.0001 ||
      std::abs(malformed.artifactSmoothing() - 0.65) > 0.0001 ||
      std::abs(malformed.ambiencePreservation() - 0.35) > 0.0001 ||
      std::abs(malformed.duckingGainDb()) > 0.0001 ||
      !std::isfinite(finite_output.at(0)) || !std::isfinite(finite_output.at(1)) ||
      !std::all_of(finite_cleanup_output.cbegin(), finite_cleanup_output.cend(),
                   [](float sample) { return std::isfinite(sample); })) {
    return fail("non-finite processor values and decoded samples should not enter the live DSP state");
  }

  TimelineAudioOutput multichannel;
  multichannel.setVolume(2.0);
  const QVector<float> multichannel_input = {
      0.25F, 0.25F, std::numeric_limits<float>::quiet_NaN(), 0.25F};
  const auto multichannel_output = multichannel.processInterleavedSamplesForTesting(
      multichannel_input, 4);
  const auto maximum_quad_right = 0.5 + 0.5 * 0.7071067811865476;
  if (multichannel_output.size() != 2 ||
      !std::isfinite(multichannel_output.at(0)) ||
      !std::isfinite(multichannel_output.at(1)) ||
      std::abs(multichannel_output.at(0) - 0.5F) > 0.0001F ||
      multichannel_output.at(1) < 0.5F ||
      multichannel_output.at(1) > maximum_quad_right + 0.001F) {
    return fail("source-mode quad audio should sanitize non-finite surround input, apply gain, and fold it into stereo");
  }

  TimelineAudioOutput downmix;
  downmix.setChannelMode(QStringLiteral("downmix-stereo"));
  const QVector<float> surround_frame = {
      0.10F, 0.20F, 0.25F, 0.80F, 0.20F, -0.10F};
  const auto surround_output = downmix.processInterleavedSamplesForTesting(
      surround_frame, 6);
  const auto expected_left = 0.10 + (0.25 + 0.20) * 0.7071067811865476;
  const auto expected_right = 0.20 + (0.25 - 0.10) * 0.7071067811865476;
  if (downmix.bufferOutput()->format().isValid() || surround_output.size() != 2 ||
      std::abs(surround_output.at(0) - expected_left) > 0.001F ||
      std::abs(surround_output.at(1) - expected_right) > 0.001F) {
    return fail("multichannel Program routing should retain decoder layout, downmix center and surrounds, and omit LFE into stereo");
  }

  TimelineAudioOutput downmix_headroom;
  downmix_headroom.setChannelMode(QStringLiteral("downmix-stereo"));
  downmix_headroom.setVolume(0.1);
  const QVector<float> hot_center_frame = {0.8F, 0.4F, 0.8F, 0.0F, 0.0F, 0.0F};
  const auto headroom_output = downmix_headroom.processInterleavedSamplesForTesting(
      hot_center_frame, 6);
  const auto expected_headroom_left = (0.8 + 0.8 * 0.7071067811865476) * 0.1;
  const auto expected_headroom_right = (0.4 + 0.8 * 0.7071067811865476) * 0.1;
  if (headroom_output.size() != 2 ||
      std::abs(headroom_output.at(0) - expected_headroom_left) > 0.001F ||
      std::abs(headroom_output.at(1) - expected_headroom_right) > 0.001F) {
    return fail("multichannel stereo mix should retain over-unity float headroom until track gain is applied");
  }

  TimelineAudioOutput unknown_layout;
  unknown_layout.setChannelMode(QStringLiteral("downmix-stereo"));
  const QVector<float> unknown_7dot1_frame = {
      0.10F, 0.20F, 0.15F, 0.90F, 0.25F, -0.10F, 0.30F, -0.20F};
  const auto unknown_layout_output =
      unknown_layout.processInterleavedSamplesWithConfigForTesting(
          unknown_7dot1_frame, 8, QAudioFormat::ChannelConfigUnknown);
  const auto expected_7dot1_left =
      0.10 + (0.15 + 0.25 + 0.30) * 0.7071067811865476;
  const auto expected_7dot1_right =
      0.20 + (0.15 - 0.10 - 0.20) * 0.7071067811865476;
  if (unknown_layout_output.size() != 2 ||
      std::abs(unknown_layout_output.at(0) - expected_7dot1_left) > 0.001F ||
      std::abs(unknown_layout_output.at(1) - expected_7dot1_right) > 0.001F) {
    return fail("unknown eight-channel input should use one 7.1 layout mapping for center and surrounds without duplicate contributions");
  }

  TimelineAudioOutput multichannel_right;
  multichannel_right.setChannelMode(QStringLiteral("mono-right"));
  const auto selected_right = multichannel_right.processInterleavedSamplesForTesting(
      surround_frame, 6);
  if (selected_right.size() != 2 ||
      std::abs(selected_right.at(0) - surround_frame.at(1)) > 0.0001F ||
      std::abs(selected_right.at(1) - surround_frame.at(1)) > 0.0001F) {
    return fail("explicit channel selection should resolve the right source channel before the stereo sink conversion");
  }

  TimelineAudioOutput extreme_sample;
  extreme_sample.setVolume(4096.0);
  const auto extreme_output = extreme_sample.processStereoSamplesForTesting(
      {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()});
  if (extreme_output.size() != 2 || !std::isfinite(extreme_output.at(0)) ||
      !std::isfinite(extreme_sample.rmsLeft()) ||
      !std::isfinite(extreme_sample.phaseCorrelation()) || !extreme_sample.clipping()) {
    return fail("extreme finite samples and high gain must not overflow output or published meter state");
  }

  const auto video_path = write_fixture(workspace.filePath(QStringLiteral("audio-state.mp4")));
  if (video_path.isEmpty()) {
    return fail("timeline source fixture should be written");
  }
  TimelineDocumentController document;
  document.createTimeline(QStringLiteral("Audio Playback State"));
  document.addClipFromGallery(
      QStringLiteral("video:audio-state"), video_path, QStringLiteral("Picture"), 4.0);
  if (!document.addIndependentAudioTrack(QStringLiteral("music"))) {
    return fail("independent Music lane should be created for live processor automation coverage");
  }
  int music_track_row = -1;
  QString music_track_id;
  for (int index = 0; index < document.sequenceTracks().size(); ++index) {
    const auto row = document.sequenceTracks().at(index).toMap();
    if (row.value(QStringLiteral("audioRole")).toString() == QStringLiteral("music") &&
        row.value(QStringLiteral("isIndependent")).toBool()) {
      music_track_row = index;
      music_track_id = row.value(QStringLiteral("trackId")).toString();
      break;
    }
  }
  if (music_track_row < 0 || music_track_id.isEmpty()) {
    return fail("the created Music lane should have a stable track identity");
  }
  if (!document.setTrackAudioSetting(
          music_track_row, QStringLiteral("audioHighPassEnabled"), true) ||
      !document.toggleTrackAutomationKeyframe(
          music_track_row, QStringLiteral("audioHighPassHz"))) {
    return fail("track audio processor controls should accept a first automation keyframe");
  }
  document.seekSequenceSeconds(2.0);
  if (
      !document.setTrackAutomationValue(
          music_track_row, QStringLiteral("audioHighPassHz"), 400.0) ||
      !document.setTrackAutomationValue(
          music_track_row, QStringLiteral("audioGainDb"), 24.0)) {
    return fail("track automation should accept audio processor keys and positive normalized gain");
  }
  if (!document.setTrackAudioSetting(
          music_track_row, QStringLiteral("audioEqLowDb"), 12.0) ||
      !document.setTrackAudioSetting(
          music_track_row, QStringLiteral("audioEqMidDb"), 12.0) ||
      !document.setTrackAudioSetting(
          music_track_row, QStringLiteral("audioEqHighDb"), 12.0) ||
      !document.setTrackAudioSetting(
          music_track_row, QStringLiteral("audioCompressorEnabled"), true) ||
      !document.setTrackAudioSetting(
          music_track_row, QStringLiteral("audioCompressorThresholdDb"), -1.0) ||
      !document.setTrackAudioSetting(
          music_track_row, QStringLiteral("audioCompressorRatio"), 3.0) ||
      !document.setTrackAudioSetting(
          music_track_row, QStringLiteral("audioCompressorMakeupDb"), 18.0) ||
      !document.setTrackAudioSetting(
          music_track_row, QStringLiteral("audioProcessingBypassed"), true) ||
      !document.setTrackAudioSetting(
          music_track_row, QStringLiteral("audioLoudnessMatchedBypass"), false) ||
      !document.setTrackAudioSetting(
          music_track_row, QStringLiteral("audioLoudnessMatchedBypass"), true)) {
    return fail("matched bypass should accept the same persisted processor settings used by export");
  }
  const auto midpoint_settings =
      document.audioTrackPlaybackSettings(music_track_id, 1.0);
  if (std::abs(midpoint_settings.value(QStringLiteral("audioHighPassHz")).toDouble() - 240.0) >
          0.1 ||
      !midpoint_settings.value(QStringLiteral("audioHighPassEnabled")).toBool() ||
      std::abs(midpoint_settings.value(QStringLiteral("audioGainDb")).toDouble() - 24.0) >
          0.1) {
    return fail("live audio settings should evaluate processor automation at the requested playhead");
  }
  if (!midpoint_settings.value(QStringLiteral("audioProcessingBypassed")).toBool() ||
      !midpoint_settings.value(QStringLiteral("audioLoudnessMatchedBypass")).toBool() ||
      std::abs(midpoint_settings.value(
                   QStringLiteral("audioLoudnessMatchedBypassGainDb")).toDouble() -
               12.0) > 0.0001) {
    return fail("live playback should receive export's bounded matched-bypass gain mapping");
  }
  if (!document.saveTimeline()) {
    return fail("positive track gain and processor automation should save through the normal timeline path");
  }
  TimelineDocumentController reopened;
  if (!reopened.loadTimeline(document.savedDocumentPath())) {
    return fail("audio processor automation project should reopen");
  }
  const auto reopened_settings = reopened.audioTrackPlaybackSettings(music_track_id, 1.0);
  if (std::abs(reopened_settings.value(QStringLiteral("audioGainDb")).toDouble() - 24.0) >
          0.1 ||
      std::abs(reopened_settings.value(QStringLiteral("audioHighPassHz")).toDouble() - 240.0) >
          0.1 ||
      std::abs(reopened_settings.value(
                   QStringLiteral("audioLoudnessMatchedBypassGainDb")).toDouble() -
               12.0) > 0.0001) {
    return fail("positive track gain and processor automation should survive save and reopen");
  }

  const auto recording_directory = workspace.filePath(QStringLiteral("Voiceover Recordings"));
  TimelineVoiceoverWaveWriter wave_writer;
  if (!wave_writer.begin(recording_directory, 48000, 1)) {
    return fail("voiceover WAV writer should create its durable recording directory and stream");
  }
  if (wave_writer.appendPcm16Frames(QByteArray(1, '\0')) ||
      wave_writer.dataByteCount() != 0) {
    return fail("WAV writer should reject malformed partial PCM frames without corrupting the stream");
  }
  const QByteArray pcm_frames(4800 * 2, '\0');
  if (!wave_writer.appendPcm16Frames(pcm_frames) || wave_writer.durationMs() != 100) {
    return fail("WAV writer should append aligned PCM16 frames and derive duration from samples");
  }
  QString recording_path;
  if (!wave_writer.finalize(&recording_path) || recording_path.isEmpty()) {
    return fail("stopped voiceover recordings should atomically become durable WAV files");
  }
  QFile recording_file(recording_path);
  if (!recording_file.open(QIODevice::ReadOnly)) {
    return fail("finalized voiceover recording should be readable from its reported path");
  }
  const auto recording_bytes = recording_file.readAll();
  if (recording_bytes.size() != 44 + pcm_frames.size() ||
      recording_bytes.left(4) != QByteArrayLiteral("RIFF") ||
      recording_bytes.mid(8, 8) != QByteArrayLiteral("WAVEfmt ") ||
      recording_bytes.mid(36, 4) != QByteArrayLiteral("data") ||
      read_u32_le(recording_bytes, 40) != static_cast<quint32>(pcm_frames.size()) ||
      !recording_path.endsWith(QStringLiteral(".wav"))) {
    return fail("finalized voiceover WAV should contain a valid PCM header and complete sample payload");
  }

  TimelineVoiceoverWaveWriter recoverable_writer;
  if (!recoverable_writer.begin(recording_directory, 44100, 2) ||
      !recoverable_writer.appendPcm16Frames(QByteArray(8, '\0')) ||
      !recoverable_writer.preservePartial()) {
    return fail("cancelling a voiceover should leave its complete samples in a recoverable partial WAV");
  }
  QFile partial_file(recoverable_writer.partialPath());
  if (!partial_file.open(QIODevice::ReadOnly)) {
    return fail("cancelled voiceover partial should remain on disk until explicitly recovered or removed");
  }
  const auto partial_bytes = partial_file.readAll();
  if (!recoverable_writer.partialPath().endsWith(QStringLiteral(".partial.wav")) ||
      partial_bytes.size() != 52 || partial_bytes.left(4) != QByteArrayLiteral("RIFF") ||
      read_u32_le(partial_bytes, 40) != 8U) {
    return fail("recoverable partial WAV should retain a finalized header for its written PCM frames");
  }

  TimelineVoiceoverWaveWriter short_writer;
  QString short_path;
  if (!short_writer.begin(recording_directory, 48000, 1) ||
      !short_writer.appendPcm16Frames(QByteArray(2, '\0')) ||
      short_writer.frameCount() != 1 || short_writer.durationMs() != 1 ||
      !short_writer.finalize(&short_path)) {
    return fail("a non-empty sub-millisecond take should finalize instead of being mistaken for an empty recording");
  }
  QFile::remove(short_path);

  TimelineDocumentController inserted_voiceover;
  inserted_voiceover.createTimeline(QStringLiteral("Voiceover One Undo"));
  const auto initial_track_count = inserted_voiceover.sequenceTracks().size();
  const auto initial_clip_count = inserted_voiceover.clipCount();
  const auto initial_undo_state = inserted_voiceover.canUndo();
  if (inserted_voiceover.insertVoiceoverRecording(
          recording_path, 0.1, 2.25, QStringLiteral("timeline:another-project"),
          inserted_voiceover.savedDocumentPath()) ||
      inserted_voiceover.sequenceTracks().size() != initial_track_count ||
      inserted_voiceover.clipCount() != initial_clip_count ||
      inserted_voiceover.canUndo() != initial_undo_state ||
      !inserted_voiceover.status().contains(QStringLiteral("different timeline"))) {
    return fail(
        "a voiceover captured for a different timeline must be rejected without changing clips, tracks, or undo history");
  }
  if (inserted_voiceover.insertVoiceoverRecording(
          recording_path, 0.1, 2.25, inserted_voiceover.timelineAssetId(),
          workspace.filePath(QStringLiteral("other-project/timeline.json"))) ||
      inserted_voiceover.sequenceTracks().size() != initial_track_count ||
      inserted_voiceover.clipCount() != initial_clip_count ||
      inserted_voiceover.canUndo() != initial_undo_state ||
      !inserted_voiceover.status().contains(QStringLiteral("document path"))) {
    return fail(
        "matching timeline IDs must not bypass the captured document-path guard or mutate the active project");
  }
  if (!inserted_voiceover.insertVoiceoverRecording(recording_path, 0.1, 2.25)) {
    return fail("completed WAV should insert into a dedicated Voiceover lane at the take start");
  }
  QVariantMap voiceover_track;
  for (const auto& track_value : inserted_voiceover.sequenceTracks()) {
    const auto track = track_value.toMap();
    if (track.value(QStringLiteral("audioRole")).toString() == QStringLiteral("voiceover") &&
        track.value(QStringLiteral("isIndependent")).toBool()) {
      voiceover_track = track;
      break;
    }
  }
  const auto voiceover_clips = voiceover_track.value(QStringLiteral("clips")).toList();
  if (voiceover_track.isEmpty() || voiceover_clips.size() != 1 ||
      std::abs(voiceover_clips.first().toMap().value(QStringLiteral("startSeconds")).toDouble() -
               2.25) > 0.0001 ||
      voiceover_track.value(QStringLiteral("clipCount")).toInt() != 1) {
    return fail("recorded audio should align to its captured sequence time on an independent Voiceover lane");
  }
  inserted_voiceover.undo();
  if (inserted_voiceover.sequenceTracks().size() != initial_track_count ||
      inserted_voiceover.clipCount() != initial_clip_count ||
      inserted_voiceover.audioRoleSummary(QStringLiteral("voiceover"))
              .value(QStringLiteral("trackCount")).toInt() != 0) {
    return fail("auto-created Voiceover lane and recording should revert together with one undo");
  }

  TimelineDocumentController existing_voiceover;
  existing_voiceover.createTimeline(QStringLiteral("Voiceover Existing Lane"));
  if (!existing_voiceover.addIndependentAudioTrack(QStringLiteral("voiceover")) ||
      !existing_voiceover.insertVoiceoverRecording(recording_path, 0.1, 0.0)) {
    return fail("recorded audio should reuse an existing dedicated Voiceover lane");
  }
  existing_voiceover.undo();
  if (existing_voiceover.audioRoleSummary(QStringLiteral("voiceover"))
              .value(QStringLiteral("trackCount")).toInt() != 1 ||
      existing_voiceover.clipCount() != 0) {
    return fail("reusing an existing Voiceover lane should add the recorded clip as one undoable edit");
  }

  return EXIT_SUCCESS;
}
