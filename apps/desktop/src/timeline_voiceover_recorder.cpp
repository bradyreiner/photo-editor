#include "timeline_voiceover_recorder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <QAudioDevice>
#include <QAudioSource>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QPermissions>
#include <QStandardPaths>
#include <QUuid>
#include <QtMath>

namespace photo_editor::desktop {

namespace {

constexpr qint64 kWaveHeaderSize = 44;
constexpr qint64 kMaximumWaveDataBytes =
    static_cast<qint64>(std::numeric_limits<quint32>::max()) - kWaveHeaderSize;

void write_u16_le(QByteArray* bytes, qsizetype offset, quint16 value) {
  (*bytes)[offset] = static_cast<char>(value & 0xffU);
  (*bytes)[offset + 1] = static_cast<char>((value >> 8U) & 0xffU);
}

void write_u32_le(QByteArray* bytes, qsizetype offset, quint32 value) {
  for (auto byte = 0; byte < 4; ++byte) {
    const auto shift = static_cast<unsigned>(byte) * 8U;
    (*bytes)[offset + byte] = static_cast<char>((value >> shift) & 0xffU);
  }
}

QByteArray wave_header(int sample_rate, int channel_count, quint32 data_bytes) {
  QByteArray header(kWaveHeaderSize, '\0');
  std::memcpy(header.data(), "RIFF", 4);
  write_u32_le(&header, 4, 36U + data_bytes);
  std::memcpy(header.data() + 8, "WAVEfmt ", 8);
  write_u32_le(&header, 16, 16U);
  write_u16_le(&header, 20, 1U);
  write_u16_le(&header, 22, static_cast<quint16>(channel_count));
  write_u32_le(&header, 24, static_cast<quint32>(sample_rate));
  const auto block_align = static_cast<quint16>(channel_count * 2);
  write_u32_le(
      &header, 28,
      static_cast<quint32>(sample_rate) * static_cast<quint32>(block_align));
  write_u16_le(&header, 32, block_align);
  write_u16_le(&header, 34, 16U);
  std::memcpy(header.data() + 36, "data", 4);
  write_u32_le(&header, 40, data_bytes);
  return header;
}

int bytes_per_sample(QAudioFormat::SampleFormat format) {
  switch (format) {
    case QAudioFormat::UInt8:
      return 1;
    case QAudioFormat::Int16:
      return 2;
    case QAudioFormat::Int32:
    case QAudioFormat::Float:
      return 4;
    case QAudioFormat::Unknown:
    case QAudioFormat::NSampleFormats:
      return 0;
  }
  return 0;
}

double decode_sample(const char* source, QAudioFormat::SampleFormat format) {
  switch (format) {
    case QAudioFormat::UInt8: {
      quint8 value = 0;
      std::memcpy(&value, source, sizeof(value));
      return (static_cast<double>(value) - 128.0) / 128.0;
    }
    case QAudioFormat::Int16: {
      qint16 value = 0;
      std::memcpy(&value, source, sizeof(value));
      return static_cast<double>(value) / 32768.0;
    }
    case QAudioFormat::Int32: {
      qint32 value = 0;
      std::memcpy(&value, source, sizeof(value));
      return static_cast<double>(value) / 2147483648.0;
    }
    case QAudioFormat::Float: {
      float value = 0.0F;
      std::memcpy(&value, source, sizeof(value));
      return std::isfinite(value) ? static_cast<double>(value) : 0.0;
    }
    case QAudioFormat::Unknown:
    case QAudioFormat::NSampleFormats:
      return 0.0;
  }
  return 0.0;
}

qint16 encode_pcm16(double sample) {
  const auto finite_sample = std::isfinite(sample) ? sample : 0.0;
  const auto bounded = qBound(-1.0, finite_sample, 1.0);
  return static_cast<qint16>(qBound(-32768, qRound(bounded * 32767.0), 32767));
}

QString input_device_id(const QAudioDevice& device) {
  return QString::fromUtf8(device.id());
}

}  // namespace

bool TimelineVoiceoverWaveWriter::begin(
    const QString& directory,
    int sample_rate,
    int channel_count) {
  if (file_.isOpen() || sample_rate < 8000 || sample_rate > 192000 ||
      channel_count < 1 || channel_count > 2) {
    return false;
  }
  const QDir output_directory(directory);
  if (!output_directory.exists() && !QDir().mkpath(directory)) {
    return false;
  }
  sample_rate_ = sample_rate;
  channel_count_ = channel_count;
  data_byte_count_ = 0;
  const auto stem = QStringLiteral("Voiceover_%1_%2")
                        .arg(QDateTime::currentDateTimeUtc().toString(
                            QStringLiteral("yyyyMMdd_HHmmss")))
                        .arg(QUuid::createUuid().toString(QUuid::Id128).left(10));
  final_path_ = output_directory.filePath(stem + QStringLiteral(".wav"));
  partial_path_ = output_directory.filePath(stem + QStringLiteral(".partial.wav"));
  file_.setFileName(partial_path_);
  if (!file_.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
    partial_path_.clear();
    final_path_.clear();
    return false;
  }
  const auto header = wave_header(sample_rate_, channel_count_, 0U);
  if (file_.write(header) != header.size() || !file_.flush()) {
    discard();
    return false;
  }
  return true;
}

bool TimelineVoiceoverWaveWriter::appendPcm16Frames(const QByteArray& interleaved_pcm) {
  if (!file_.isOpen() || interleaved_pcm.isEmpty()) {
    return false;
  }
  const auto block_align = static_cast<qint64>(channel_count_) * 2;
  if (interleaved_pcm.size() % block_align != 0 ||
      data_byte_count_ > kMaximumWaveDataBytes - interleaved_pcm.size()) {
    return false;
  }
  if (file_.seek(kWaveHeaderSize + data_byte_count_) == false ||
      file_.write(interleaved_pcm) != interleaved_pcm.size()) {
    return false;
  }
  data_byte_count_ += interleaved_pcm.size();
  return updateHeader() && file_.flush();
}

bool TimelineVoiceoverWaveWriter::finalize(QString* output_path) {
  if (!file_.isOpen() || data_byte_count_ <= 0 || !updateHeader() || !file_.flush()) {
    return false;
  }
  file_.close();
  if (!QFile::rename(partial_path_, final_path_)) {
    return false;
  }
  partial_path_.clear();
  if (output_path != nullptr) {
    *output_path = final_path_;
  }
  return true;
}

bool TimelineVoiceoverWaveWriter::preservePartial() {
  if (!file_.isOpen()) {
    return !partial_path_.isEmpty() && QFileInfo::exists(partial_path_) &&
           data_byte_count_ > 0;
  }
  if (data_byte_count_ <= 0 || !updateHeader() || !file_.flush()) {
    return false;
  }
  file_.close();
  return true;
}

void TimelineVoiceoverWaveWriter::discard() {
  if (file_.isOpen()) {
    file_.close();
  }
  if (!partial_path_.isEmpty()) {
    QFile::remove(partial_path_);
  }
  partial_path_.clear();
  final_path_.clear();
  data_byte_count_ = 0;
}

QString TimelineVoiceoverWaveWriter::partialPath() const {
  return partial_path_;
}

QString TimelineVoiceoverWaveWriter::finalPath() const {
  return final_path_;
}

qint64 TimelineVoiceoverWaveWriter::frameCount() const {
  const auto block_align = qMax(1, channel_count_ * 2);
  return data_byte_count_ / block_align;
}

qint64 TimelineVoiceoverWaveWriter::durationMs() const {
  if (sample_rate_ <= 0 || frameCount() <= 0) {
    return 0;
  }
  // Retain non-empty sub-millisecond takes: the insertion contract currently
  // carries integer milliseconds, so round up instead of misclassifying them
  // as an empty recording.
  return (frameCount() * 1000 + sample_rate_ - 1) / sample_rate_;
}

qint64 TimelineVoiceoverWaveWriter::dataByteCount() const {
  return data_byte_count_;
}

bool TimelineVoiceoverWaveWriter::updateHeader() {
  if (!file_.isOpen() || data_byte_count_ < 0 ||
      data_byte_count_ > kMaximumWaveDataBytes) {
    return false;
  }
  const auto header = wave_header(
      sample_rate_, channel_count_, static_cast<quint32>(data_byte_count_));
  if (!file_.seek(0) || file_.write(header) != header.size()) {
    return false;
  }
  return file_.seek(kWaveHeaderSize + data_byte_count_);
}

TimelineVoiceoverRecorder::TimelineVoiceoverRecorder(QObject* parent)
    : QObject(parent) {
  pre_roll_timer_.setInterval(50);
  pre_roll_timer_.setTimerType(Qt::PreciseTimer);
  connect(&pre_roll_timer_, &QTimer::timeout, this, [this]() {
    pre_roll_remaining_ms_ = qMax(
        0, pre_roll_total_ms_ - static_cast<int>(pre_roll_clock_.elapsed()));
    if (pre_roll_remaining_ms_ == 0) {
      pre_roll_timer_.stop();
      beginCapture();
    } else {
      emit stateChanged();
    }
  });
  connect(
      &media_devices_, &QMediaDevices::audioInputsChanged,
      this, &TimelineVoiceoverRecorder::refreshInputDevices);
  refreshInputDevices();
}

TimelineVoiceoverRecorder::~TimelineVoiceoverRecorder() {
  pre_roll_timer_.stop();
  if (source_ != nullptr) {
    state_ = QStringLiteral("finalizing");
    source_->stop();
    consumeAvailableAudio();
  }
  if (writer_.partialPath().size() > 0 && writer_.durationMs() > 0) {
    (void)writer_.preservePartial();
  } else {
    writer_.discard();
  }
}

QVariantList TimelineVoiceoverRecorder::inputDevices() const {
  return input_devices_;
}

QString TimelineVoiceoverRecorder::selectedInputDeviceId() const {
  return selected_input_device_id_;
}

QString TimelineVoiceoverRecorder::state() const {
  return state_;
}

int TimelineVoiceoverRecorder::preRollRemainingMs() const {
  return pre_roll_remaining_ms_;
}

qint64 TimelineVoiceoverRecorder::recordedDurationMs() const {
  return writer_.durationMs();
}

QString TimelineVoiceoverRecorder::errorString() const {
  return error_string_;
}

QString TimelineVoiceoverRecorder::recoverablePath() const {
  return recoverable_path_;
}

QString TimelineVoiceoverRecorder::captureTimelineIdentity() const {
  return capture_timeline_identity_;
}

QString TimelineVoiceoverRecorder::captureDocumentPath() const {
  return capture_document_path_;
}

void TimelineVoiceoverRecorder::setSelectedInputDeviceId(const QString& device_id) {
  const auto normalized = device_id.trimmed();
  if (state_ != QStringLiteral("idle") ||
      selected_input_device_id_ == normalized) {
    return;
  }
  selected_input_device_id_ = normalized;
  emit selectedInputDeviceIdChanged();
}

void TimelineVoiceoverRecorder::refreshInputDevices() {
  const auto default_device = media_devices_.defaultAudioInput();
  QVariantList devices;
  for (const auto& device : media_devices_.audioInputs()) {
    devices.append(QVariantMap{
        {QStringLiteral("id"), input_device_id(device)},
        {QStringLiteral("description"), device.description()},
        {QStringLiteral("isDefault"), device == default_device},
    });
  }
  input_devices_ = devices;
  auto selected_exists = false;
  for (const auto& value : input_devices_) {
    if (value.toMap().value(QStringLiteral("id")).toString() ==
        selected_input_device_id_) {
      selected_exists = true;
      break;
    }
  }
  if (!selected_exists) {
    selected_input_device_id_ = input_device_id(default_device);
    if (selected_input_device_id_.isEmpty() && !input_devices_.isEmpty()) {
      selected_input_device_id_ =
          input_devices_.first().toMap().value(QStringLiteral("id")).toString();
    }
    emit selectedInputDeviceIdChanged();
  }
  emit inputDevicesChanged();
}

bool TimelineVoiceoverRecorder::startRecording(
    const QString& project_document_path,
    int pre_roll_ms,
    const QString& timeline_identity) {
  if (state_ == QStringLiteral("preroll") ||
      state_ == QStringLiteral("recording") ||
      state_ == QStringLiteral("permissionPending") ||
      state_ == QStringLiteral("finalizing")) {
    return false;
  }
  capture_document_path_ = project_document_path.trimmed();
  capture_timeline_identity_ = timeline_identity.trimmed();
  recoverable_path_.clear();
  error_string_.clear();
  auto* application = QCoreApplication::instance();
  if (application == nullptr) {
    setStartError(QStringLiteral(
        "Microphone permission is unavailable because the desktop application is not running."));
    return false;
  }
  const QMicrophonePermission microphone_permission;
  const auto permission_status = application->checkPermission(microphone_permission);
  if (permission_status == Qt::PermissionStatus::Denied) {
    setStartError(QStringLiteral(
        "Microphone access is denied. Enable Looksy in System Settings → Privacy & Security → Microphone, then try again."));
    return false;
  }
  if (permission_status == Qt::PermissionStatus::Undetermined) {
    pending_project_document_path_ = project_document_path;
    pending_timeline_identity_ = timeline_identity.trimmed();
    pending_pre_roll_ms_ = qBound(0, pre_roll_ms, 10000);
    const auto request_generation = ++permission_request_generation_;
    error_string_.clear();
    state_ = QStringLiteral("permissionPending");
    emit stateChanged();
    application->requestPermission(
        microphone_permission, this,
        [this, request_generation](const QPermission& result) {
          if (request_generation != permission_request_generation_ ||
              state_ != QStringLiteral("permissionPending")) {
            return;
          }
          const auto project_path = pending_project_document_path_;
          const auto capture_timeline_identity = pending_timeline_identity_;
          const auto pre_roll_ms = pending_pre_roll_ms_;
          pending_project_document_path_.clear();
          pending_timeline_identity_.clear();
          pending_pre_roll_ms_ = 0;
          if (result.status() != Qt::PermissionStatus::Granted) {
            setStartError(result.status() == Qt::PermissionStatus::Denied
                              ? QStringLiteral(
                                    "Microphone access is denied. Enable Looksy in System Settings → Privacy & Security → Microphone, then try again.")
                              : QStringLiteral(
                                    "macOS did not return a microphone permission decision. Check System Settings → Privacy & Security → Microphone, then retry."));
            return;
          }
          state_ = QStringLiteral("idle");
          error_string_.clear();
          emit stateChanged();
          (void)startRecordingAfterPermission(
              project_path, pre_roll_ms, capture_timeline_identity);
        });
    return true;
  }
  return startRecordingAfterPermission(
      project_document_path, pre_roll_ms, timeline_identity);
}

bool TimelineVoiceoverRecorder::startRecordingAfterPermission(
    const QString& project_document_path,
    int pre_roll_ms,
    const QString& timeline_identity) {
  capture_document_path_ = project_document_path.trimmed();
  capture_timeline_identity_ = timeline_identity.trimmed();
  const auto device = selectedDevice();
  if (device.isNull()) {
    failRecording(QStringLiteral("No audio input device is available."));
    return false;
  }
  QAudioFormat requested;
  requested.setSampleRate(48000);
  requested.setChannelCount(1);
  requested.setSampleFormat(QAudioFormat::Int16);
  capture_format_ = device.isFormatSupported(requested)
                        ? requested
                        : device.preferredFormat();
  if (capture_format_.sampleRate() < 8000 ||
      capture_format_.sampleRate() > 192000 ||
      capture_format_.channelCount() < 1 ||
      bytes_per_sample(capture_format_.sampleFormat()) == 0) {
    failRecording(QStringLiteral(
        "The selected input device has an unsupported recording format."));
    return false;
  }
  const auto output_channels = capture_format_.channelCount() > 2
                                   ? 1
                                   : capture_format_.channelCount();
  if (!writer_.begin(
          recordingDirectory(project_document_path),
          capture_format_.sampleRate(), output_channels)) {
    failRecording(QStringLiteral("A durable WAV recording could not be created."));
    return false;
  }
  error_string_.clear();
  incomplete_input_frame_.clear();
  capture_error_.clear();
  pre_roll_total_ms_ = qBound(0, pre_roll_ms, 10000);
  pre_roll_remaining_ms_ = pre_roll_total_ms_;
  if (pre_roll_total_ms_ > 0) {
    state_ = QStringLiteral("preroll");
    pre_roll_clock_.restart();
    pre_roll_timer_.start();
    emit stateChanged();
    return true;
  }
  beginCapture();
  return state_ == QStringLiteral("recording");
}

bool TimelineVoiceoverRecorder::stopRecording() {
  if (state_ != QStringLiteral("recording")) {
    return false;
  }
  state_ = QStringLiteral("finalizing");
  emit stateChanged();
  stopCapture();
  if (!capture_error_.isEmpty()) {
    failRecording(capture_error_);
    return false;
  }
  QString output_path;
  const auto duration_ms = writer_.durationMs();
  if (duration_ms <= 0 || !writer_.finalize(&output_path)) {
    failRecording(QStringLiteral("No complete audio frames could be finalized to WAV."));
    return false;
  }
  state_ = QStringLiteral("idle");
  emit stateChanged();
  emit recordingFinished(output_path, duration_ms);
  return true;
}

void TimelineVoiceoverRecorder::cancelRecording() {
  if (state_ == QStringLiteral("idle")) {
    return;
  }
  if (state_ == QStringLiteral("permissionPending")) {
    ++permission_request_generation_;
    pending_project_document_path_.clear();
    pending_timeline_identity_.clear();
    pending_pre_roll_ms_ = 0;
    state_ = QStringLiteral("idle");
    emit stateChanged();
    emit recordingCancelled(recoverable_path_);
    return;
  }
  if (state_ == QStringLiteral("error")) {
    state_ = QStringLiteral("idle");
    error_string_.clear();
    emit stateChanged();
    return;
  }
  pre_roll_timer_.stop();
  state_ = QStringLiteral("finalizing");
  emit stateChanged();
  stopCapture();
  if (writer_.durationMs() > 0 && writer_.preservePartial()) {
    recoverable_path_ = writer_.partialPath();
  } else {
    writer_.discard();
    recoverable_path_.clear();
  }
  state_ = QStringLiteral("idle");
  pre_roll_remaining_ms_ = 0;
  emit stateChanged();
  emit recordingCancelled(recoverable_path_);
}

void TimelineVoiceoverRecorder::stopForTimelineChange(
    const QString& active_timeline_identity,
    const QString& active_document_path) {
  if (state_ != QStringLiteral("permissionPending") &&
      state_ != QStringLiteral("preroll") &&
      state_ != QStringLiteral("recording")) {
    return;
  }
  const auto active_identity = active_timeline_identity.trimmed();
  const auto normalized_path = [](const QString& path) {
    if (path.trimmed().isEmpty()) {
      return QString{};
    }
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
  };
  const auto identity_changed =
      (!capture_timeline_identity_.isEmpty() &&
       active_identity != capture_timeline_identity_) ||
      (!capture_document_path_.isEmpty() &&
       normalized_path(active_document_path) != normalized_path(capture_document_path_));
  if (!identity_changed) {
    return;
  }

  pre_roll_timer_.stop();
  ++permission_request_generation_;
  pending_project_document_path_.clear();
  pending_timeline_identity_.clear();
  pending_pre_roll_ms_ = 0;
  if (state_ == QStringLiteral("permissionPending")) {
    recoverable_path_.clear();
    error_string_ = QStringLiteral(
        "The timeline changed before microphone access was granted. No take was recorded. Open Record Voiceover on the intended timeline and try again.");
    state_ = QStringLiteral("error");
    emit stateChanged();
    emit recordingFailed(error_string_, recoverable_path_);
    return;
  }
  if (state_ == QStringLiteral("preroll")) {
    if (!writer_.partialPath().isEmpty()) {
      writer_.discard();
    }
    pre_roll_remaining_ms_ = 0;
    recoverable_path_.clear();
    error_string_ = QStringLiteral(
        "The timeline changed during pre-roll, so recording was canceled before capture began. Reopen Record Voiceover on the intended timeline.");
    state_ = QStringLiteral("error");
    emit stateChanged();
    emit recordingFailed(error_string_, recoverable_path_);
    return;
  }

  state_ = QStringLiteral("finalizing");
  emit stateChanged();
  stopCapture();
  QString finalized_path;
  if (capture_error_.isEmpty() && writer_.durationMs() > 0 &&
      writer_.finalize(&finalized_path)) {
    recoverable_path_ = finalized_path;
    error_string_ = QStringLiteral(
        "The timeline changed while recording. The WAV was saved to %1; switch back to the recorded timeline and use Add Audio to place it.")
                        .arg(finalized_path);
  } else if (writer_.durationMs() > 0 && writer_.preservePartial()) {
    recoverable_path_ = writer_.partialPath();
    error_string_ = QStringLiteral(
        "The timeline changed while recording. A recoverable partial WAV was kept at %1; switch back to the recorded timeline and use Add Audio after recovering it.")
                        .arg(recoverable_path_);
  } else {
    writer_.discard();
    recoverable_path_.clear();
    error_string_ = capture_error_.isEmpty()
                        ? QStringLiteral(
                              "The timeline changed before complete audio frames were captured. No WAV could be saved.")
                        : QStringLiteral(
                              "The timeline changed and audio capture stopped: %1")
                              .arg(capture_error_);
  }
  state_ = QStringLiteral("error");
  emit stateChanged();
  emit recordingFailed(error_string_, recoverable_path_);
}

void TimelineVoiceoverRecorder::reportInsertionFailure(
    const QString& file_path,
    const QString& detail) {
  const auto trimmed_path = file_path.trimmed();
  if (!trimmed_path.isEmpty()) {
    const QFileInfo file_info(trimmed_path);
    recoverable_path_ = file_info.exists()
                            ? file_info.absoluteFilePath()
                            : trimmed_path;
  }
  error_string_ = detail.trimmed().isEmpty()
                      ? QStringLiteral(
                            "The WAV was saved but could not be inserted. Switch to its timeline and use Add Audio to place it.")
                      : detail.trimmed();
  state_ = QStringLiteral("error");
  emit stateChanged();
  emit recordingFailed(error_string_, recoverable_path_);
}

QAudioDevice TimelineVoiceoverRecorder::selectedDevice() const {
  for (const auto& device : media_devices_.audioInputs()) {
    if (input_device_id(device) == selected_input_device_id_) {
      return device;
    }
  }
  const auto default_device = media_devices_.defaultAudioInput();
  return default_device.isNull() && !media_devices_.audioInputs().isEmpty()
             ? media_devices_.audioInputs().first()
             : default_device;
}

void TimelineVoiceoverRecorder::setStartError(const QString& detail) {
  error_string_ = detail;
  state_ = QStringLiteral("error");
  emit stateChanged();
  emit recordingFailed(error_string_, recoverable_path_);
}

void TimelineVoiceoverRecorder::beginCapture() {
  const auto device = selectedDevice();
  if (device.isNull()) {
    failRecording(QStringLiteral("The selected audio input is no longer available."));
    return;
  }
  auto* source = new QAudioSource(device, capture_format_, this);
  source_ = source;
  const QPointer<QAudioSource> source_guard(source);
  connect(source, &QAudioSource::stateChanged, this, [this, source_guard](QAudio::State state) {
    if (source_guard.isNull() || source_ != source_guard ||
        state_ != QStringLiteral("recording") || state != QAudio::StoppedState) {
      return;
    }
    const auto error = source_guard->error();
    failRecording(error == QAudio::NoError
                      ? QStringLiteral("Audio input stopped unexpectedly before recording was stopped.")
                      : QStringLiteral("Audio input stopped unexpectedly (error %1).")
                            .arg(static_cast<int>(error)));
  });
  auto* device_stream = source->start();
  if (device_stream == nullptr) {
    source->deleteLater();
    source_.clear();
    failRecording(QStringLiteral("The selected audio input could not be started."));
    return;
  }
  capture_device_ = device_stream;
  connect(device_stream, &QIODevice::readyRead, this, [this]() {
    consumeAvailableAudio();
  });
  state_ = QStringLiteral("recording");
  pre_roll_remaining_ms_ = 0;
  emit stateChanged();
  emit recordingStarted();
}

void TimelineVoiceoverRecorder::consumeAvailableAudio() {
  if (capture_device_ == nullptr) {
    return;
  }
  const auto bytes = capture_device_->readAll();
  if (!bytes.isEmpty() && !appendCapturedBytes(bytes)) {
    capture_error_ = QStringLiteral("Audio input data could not be written to the WAV recording.");
    if (state_ == QStringLiteral("recording")) {
      failRecording(capture_error_);
    }
    return;
  }
  if (!bytes.isEmpty()) {
    emit stateChanged();
  }
}

bool TimelineVoiceoverRecorder::appendCapturedBytes(const QByteArray& input_bytes) {
  incomplete_input_frame_.append(input_bytes);
  const auto input_channels = capture_format_.channelCount();
  const auto sample_bytes = bytes_per_sample(capture_format_.sampleFormat());
  const auto frame_bytes = input_channels * sample_bytes;
  if (frame_bytes <= 0) {
    return false;
  }
  const auto complete_bytes =
      incomplete_input_frame_.size() / frame_bytes * frame_bytes;
  if (complete_bytes <= 0) {
    return true;
  }
  const auto output_channels = input_channels > 2 ? 1 : input_channels;
  const auto frame_count = complete_bytes / frame_bytes;
  QByteArray pcm(frame_count * output_channels * 2, '\0');
  auto* output = reinterpret_cast<qint16*>(pcm.data());
  for (auto frame = 0; frame < frame_count; ++frame) {
    const auto* frame_data = incomplete_input_frame_.constData() + frame * frame_bytes;
    if (output_channels == 1 && input_channels > 1) {
      auto mixed = 0.0;
      for (auto channel = 0; channel < input_channels; ++channel) {
        mixed += decode_sample(
            frame_data + channel * sample_bytes, capture_format_.sampleFormat());
      }
      output[frame] = encode_pcm16(mixed / static_cast<double>(input_channels));
      continue;
    }
    for (auto channel = 0; channel < output_channels; ++channel) {
      output[frame * output_channels + channel] = encode_pcm16(decode_sample(
          frame_data + channel * sample_bytes, capture_format_.sampleFormat()));
    }
  }
  incomplete_input_frame_.remove(0, complete_bytes);
  return writer_.appendPcm16Frames(pcm);
}

void TimelineVoiceoverRecorder::stopCapture() {
  if (source_ != nullptr) {
    consumeAvailableAudio();
    source_->stop();
    consumeAvailableAudio();
    disconnect(source_, nullptr, this, nullptr);
    source_->deleteLater();
    source_.clear();
  }
  capture_device_.clear();
  // Drop any trailing incomplete source frame; only whole PCM frames belong in the WAV.
  incomplete_input_frame_.clear();
}

void TimelineVoiceoverRecorder::failRecording(const QString& detail) {
  pre_roll_timer_.stop();
  state_ = QStringLiteral("finalizing");
  emit stateChanged();
  stopCapture();
  if (writer_.durationMs() > 0 && writer_.preservePartial()) {
    recoverable_path_ = writer_.partialPath();
  } else {
    writer_.discard();
    recoverable_path_.clear();
  }
  error_string_ = detail;
  state_ = QStringLiteral("error");
  emit stateChanged();
  emit recordingFailed(error_string_, recoverable_path_);
}

QString TimelineVoiceoverRecorder::recordingDirectory(
    const QString& project_document_path) const {
  const QFileInfo project_file(project_document_path.trimmed());
  if (!project_document_path.trimmed().isEmpty() && project_file.exists()) {
    return QDir(project_file.absolutePath()).filePath(
        QStringLiteral("Voiceover Recordings"));
  }
  auto fallback = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (fallback.isEmpty()) {
    fallback = QDir::tempPath();
  }
  return QDir(fallback).filePath(QStringLiteral("Timelines/Voiceover"));
}

}  // namespace photo_editor::desktop
