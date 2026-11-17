#include "timeline_color_edit_controller.h"
#include "qimage_color_conversion.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "edit_adjustment_patch.h"
#include "timeline_document_controller.h"
#include "timeline_video_grade_runtime.h"
#include "photo_editor/pipeline/video_color_transform.h"
#include "photo_editor/pipeline/photo_effect_registry.h"

#include <QDir>
#include <QFutureWatcher>
#include <QImageReader>
#include <QJsonDocument>
#include <QProcess>
#include <QScopedValueRollback>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <utility>

namespace photo_editor::desktop {
namespace {
class ClipImageSink final : public EditorPreviewImageSink {
 public:
  ClipImageSink(std::function<EditorPreviewImageSink*()> target, QString prefix)
      : target_(std::move(target)), prefix_(std::move(prefix) + '/') {}
  ~ClipImageSink() override {
    if (auto* target = target_()) for (const auto& key : keys_) target->clearImage(prefix_ + key);
  }
  void setImage(const QString& key, const QImage& image) override {
    keys_.insert(key);
    if (auto* target = target_()) target->setImage(prefix_ + key, image);
    else images_.insert(key, image);
  }
  void clearImage(const QString& key) override {
    if (auto* target = target_()) target->clearImage(prefix_ + key);
    images_.remove(key);
  }
  QImage image(const QString& key) const override {
    if (auto* target = target_()) return target->image(prefix_ + key);
    return images_.value(key);
  }
 private:
  std::function<EditorPreviewImageSink*()> target_;
  QString prefix_;
  QSet<QString> keys_;
  QHash<QString, QImage> images_;
};

QString prepare_frame(const QString& source, const QString& destination, double seconds,
                      const QString& input_key, const std::shared_ptr<QTemporaryDir>& storage) {
  Q_UNUSED(storage);  // Retain the temporary directory until the worker exits.
  QImageReader reader(source);
  QImage image;
  if (reader.canRead()) image = reader.read();
  if (image.isNull() && !source.isEmpty()) {
    const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) return QStringLiteral("FFmpeg is required to open this clip's color controls.");
    QProcess process;
    process.start(ffmpeg, {"-v", "error", "-nostdin", "-ss", QString::number(seconds, 'f', 6),
        "-i", source, "-frames:v", "1", "-vf", "scale='min(1280,iw)':'min(1280,ih)':force_original_aspect_ratio=decrease",
        "-y", destination});
    if (!process.waitForFinished(20000) || process.exitCode() != 0) {
      process.kill();
      process.waitForFinished(1000);
      return QStringLiteral("The clip's source frame could not be prepared: ") +
          QString::fromUtf8(process.readAllStandardError()).right(800);
    }
    image.load(destination);
  }
  if (image.isNull() && source.isEmpty()) {
    image = QImage(1280, 720, QImage::Format_RGBA8888);
    image.fill(Qt::black);
  }
  if (image.isNull()) return QStringLiteral("The clip's source frame could not be decoded.");
  if (image.width() > 1280 || image.height() > 1280)
    image = image.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  using namespace photo_editor::pipeline;
  const auto profile = input_key == "slog2-sgamut" ? std::optional{VideoInputTransformProfile::SonySLog2SGamut}
      : input_key == "slog3-sgamut3" ? std::optional{VideoInputTransformProfile::SonySLog3SGamut3}
      : input_key == "slog3-sgamut3-cine" ? std::optional{VideoInputTransformProfile::SonySLog3SGamut3Cine} : std::nullopt;
  if (profile) {
    const auto& transform = cached_sony_video_input_rec709_lut(*profile);
    if (!transform.ok()) return QString::fromStdString(transform.failure_reason);
    image = image.convertToFormat(QImage::Format_RGBA64);
    for (int y = 0; y < image.height(); ++y) {
      auto* row = reinterpret_cast<QRgba64*>(image.scanLine(y));
      for (int x = 0; x < image.width(); ++x) {
        const auto pixel = row[x];
        const auto output = apply_lut_tetrahedral(transform.lut, ColorPixel{
            static_cast<float>(pixel.red()) / 65535.F, static_cast<float>(pixel.green()) / 65535.F,
            static_cast<float>(pixel.blue()) / 65535.F, static_cast<float>(pixel.alpha()) / 65535.F});
        const auto channel = [](float value) { return static_cast<quint16>(std::lround(std::clamp(value, 0.F, 1.F) * 65535.F)); };
        row[x] = QRgba64::fromRgba64(channel(output.red), channel(output.green), channel(output.blue), pixel.alpha());
      }
    }
  } else if (reader.canRead() && image.colorSpace().isValid()) {
    image = convert_tagged_qimage_color_space(image, timeline_video_rec709_color_space(), QImageColorConversionPurpose::DecodeNormalization);
  }
  image.setColorSpace(timeline_video_rec709_color_space());
  return image.save(destination, "PNG") ? QString{} : QStringLiteral("The color reference frame could not be saved.");
}
}

TimelineColorEditController::TimelineColorEditController(TimelineDocumentController* document,
    EditorPreviewController* policy_owner, QObject* parent)
    : QObject(parent), document_(document), policy_owner_(policy_owner),
      editor_(std::make_unique<EditorPreviewController>()) {
  editor_->setCatalogRecipePersistenceEnabled(false);
  const auto image_namespace = QStringLiteral("timeline-color-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
  editor_->preview_image_namespace_ = image_namespace;
  const QPointer<EditorPreviewController> owner(policy_owner);
  image_sink_ = std::make_unique<ClipImageSink>([owner]() -> EditorPreviewImageSink* {
    return owner ? owner->image_provider_ : nullptr;
  }, image_namespace);
  editor_->setImageProvider(image_sink_.get());
  editor_->setEditAccessPolicy([this, owner] {
    if (owner && !static_cast<bool>(owner->customer_edit_access_.enter())) return false;
    return synchronizing_ || (active_ && ready_ && editable_ && document_ &&
        session_generation_ == document_->working_session_generation_);
  });
  publication_timer_.setSingleShot(true);
  publication_timer_.setInterval(16);
  connect(&publication_timer_, &QTimer::timeout, this, &TimelineColorEditController::publishEdit);
  const auto schedule = [this] {
    if (!synchronizing_ && active_ && ready_) publication_timer_.start();
  };
  connect(editor_.get(), &EditorPreviewController::controlsChanged, this, schedule);
  connect(editor_.get(), &EditorPreviewController::presetPreviewChanged, this, schedule);
  connect(editor_.get(), &EditorPreviewController::sectionComparisonChanged, this, schedule);
  connect(editor_.get(), &EditorPreviewController::historyChanged, this, schedule);
  const auto sampling_changed = [this] {
    if (!samplingTarget().isEmpty() && document_) document_->pauseSequence();
    emit changed();
  };
  connect(editor_.get(), &EditorPreviewController::whiteBalanceSamplingChanged, this, sampling_changed);
  connect(editor_.get(), &EditorPreviewController::colorToolSamplingTargetChanged, this, sampling_changed);
  connect(editor_.get(), &EditorPreviewController::documentEditCommitted, this, [this] {
    publication_timer_.stop();
    publishEdit();
  });
  if (document) {
    connect(document, &TimelineDocumentController::selectionChanged, this, &TimelineColorEditController::synchronize);
    connect(document, &TimelineDocumentController::timelineStateChanged, this, &TimelineColorEditController::synchronize);
    connect(document, &TimelineDocumentController::sequenceChanged, this, &TimelineColorEditController::synchronize);
  }
}

TimelineColorEditController::~TimelineColorEditController() {
  setActive(false);
  editor_->setImageProvider(nullptr);
  editor_.reset();
}
QObject* TimelineColorEditController::editor() const { return editor_.get(); }
QString TimelineColorEditController::samplingTarget() const {
  if (editor_->whiteBalanceSampling()) return QStringLiteral("white-balance");
  if (!editor_->colorToolSamplingTarget().isEmpty()) return editor_->colorToolSamplingTarget();
  return color_mixer_sampling_ ? QStringLiteral("color-mixer") : QString{};
}

void TimelineColorEditController::setColorMixerSampling(bool enabled) {
  if (color_mixer_sampling_ == enabled) return;
  color_mixer_sampling_ = enabled;
  if (enabled && document_) document_->pauseSequence();
  if (!enabled) finishColorMixerGesture();
  emit changed();
}

void TimelineColorEditController::finishColorMixerGesture() {
  ++sample_generation_;
  editor_->cancelColorMixerSample();
  editor_->endCoalescedEdit();
}

void TimelineColorEditController::cancelSampling() {
  ++sample_generation_;
  setColorMixerSampling(false);
  editor_->setWhiteBalanceSampling(false);
  editor_->setColorToolSamplingTarget({});
}

void TimelineColorEditController::sampleProgramAt(double x, double y) {
  if (!editable() || sampling_ || samplingTarget().isEmpty() || !std::isfinite(x) || !std::isfinite(y) ||
      x < 0 || x > 1 || y < 0 || y > 1 || !document_) return;
  if (editor_->photo_effect_preview_active_ || editor_->preset_preview_state_.has_value() ||
      editor_->history_preview_state_.has_value()) {
    error_ = QStringLiteral("Apply or cancel the preview before sampling Program.");
    emit changed();
    return;
  }
  const auto* clip = document_->clipAt(document_->selectedClipRef());
  if (!clip || (clip->media_type != "adjustment" && clip->clip_id != document_->programClipId())) {
    error_ = QStringLiteral("Move the playhead over the selected clip to sample its color.");
    emit changed();
    return;
  }
  if (clip->media_type == "adjustment")
    clip = document_->clipAt(document_->flattenedIndexForClipId(document_->programClipId()));
  if (!clip || clip->file_path.isEmpty()) return;
  const auto source = clip->file_path;
  const auto input_key = clip->video_input_transform;
  const auto seconds = document_->clipLocalSecondsForSequenceTime(*clip, document_->sequencePlayheadSeconds());
  const auto destination = storage_->filePath("sample-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png");
  const auto generation = generation_;
  const auto sample_generation = ++sample_generation_;
  const auto target = samplingTarget();
  sampling_ = true;
  auto* watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, generation, sample_generation, destination, target, x, y] {
    const auto error = watcher->result();
    watcher->deleteLater();
    sampling_ = false;
    if (generation != generation_ || sample_generation != sample_generation_ || !editable() || !document_ ||
        session_generation_ != document_->working_session_generation_ || target != samplingTarget()) return;
    if (!error.isEmpty()) { error_ = error; emit changed(); return; }
    {
      const QScopedValueRollback<bool> guard(synchronizing_, true);
      const auto patch = serialize_adjustment_patch_json(editor_->currentState(),
          all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
      frame_path_ = destination;
      editor_->loadAsset(QStringLiteral("timeline-color:%1:%2").arg(session_generation_).arg(clip_id_),
          frame_path_, {}, {}, {}, {}, patch, true);
      if (target == "white-balance") editor_->setWhiteBalanceSampling(true);
      else if (target != "color-mixer") editor_->setColorToolSamplingTarget(target);
    }
    if (target == "white-balance") editor_->sampleWhiteBalanceAt(x, y);
    else if (target == "color-mixer") {
      editor_->beginCoalescedEdit();
      if (!editor_->selectColorMixerChannelAt(x, y)) editor_->endCoalescedEdit();
    } else editor_->sampleColorToolAt(x, y);
  });
  watcher->setFuture(QtConcurrent::run(prepare_frame, source, destination, seconds, input_key, storage_));
}

void TimelineColorEditController::finishGesture() {
  if (document_ && document_gesture_) document_->endContinuousEdit();
  document_gesture_ = false;
}
void TimelineColorEditController::clearPreview() {
  if (document_ && document_->color_grade_preview_recipes_.remove(clip_id_) > 0)
    emit document_->playbackStateChanged();
}
void TimelineColorEditController::setActive(bool active) {
  if (active_ == active) return;
  if (!active) {
    pending_effect_type_.clear();
    pending_effect_clip_id_.clear();
    cancelSampling();
    editor_->finishPhotoLayerEffectPreview(false);
    editor_->clearPresetPreview();
    publishEdit();
    clearPreview();
    finishGesture();
    publication_timer_.stop();
  }
  active_ = active;
  if (active_) {
    if (ready_ && document_ && session_generation_ == document_->working_session_generation_)
      loadRecipe(document_->colorGradeRecipeJsonForClip(clip_id_));
    synchronize();
  }
  emit changed();
}

void TimelineColorEditController::synchronize() {
  if (!active_ || synchronizing_ || !document_) return;
  const auto ref = document_->selectedClipRef();
  const auto* clip = document_->clipAt(ref);
  const auto* track = document_->trackAt(ref.track_index);
  const bool eligible = document_->selectedClipCount() == 1 && clip && track &&
      (clip->media_type == "photo" || clip->media_type == "video" ||
       clip->media_type == "adjustment");
  const auto id = eligible ? clip->clip_id : QString{};
  if (!pending_effect_type_.isEmpty() &&
      (id != pending_effect_clip_id_ || pending_effect_session_ != document_->working_session_generation_)) {
    pending_effect_type_.clear();
    pending_effect_clip_id_.clear();
  }
  const bool same_target = id == clip_id_ && session_generation_ == document_->working_session_generation_;
  editable_ = eligible && !track->locked && clip->color_grade_recipe_error.isEmpty();
  target_label_ = eligible ? clip->display_label
                           : QStringLiteral("Select one photo, video, or adjustment clip");
  if (same_target) {
    if (ready_ && clip && clip->color_grade_recipe_json != published_recipe_) {
      finishGesture();
      clearPreview();
      loadRecipe(clip->color_grade_recipe_json);
    }
    emit changed();
    return;
  }
  finishGesture();
  clearPreview();
  ++generation_;
  clip_id_ = id;
  session_generation_ = document_->working_session_generation_;
  ready_ = false;
  loading_ = eligible;
  error_.clear();
  if (!eligible) { emit changed(); return; }
  source_path_ = clip->file_path;
  input_transform_ = clip->video_input_transform;
  double seconds = clip->trim_start_seconds;
  if (clip->media_type == "adjustment") {
    if (const auto* program = document_->clipAt(document_->flattenedIndexForClipId(document_->programClipId()))) {
      source_path_ = program->file_path;
      input_transform_ = program->video_input_transform;
      seconds = program->trim_start_seconds;
    }
  }
  published_recipe_ = clip->color_grade_recipe_json;
  storage_ = std::make_shared<QTemporaryDir>();
  if (!storage_->isValid()) {
    loading_ = false;
    error_ = QStringLiteral("Cannot create a temporary color reference frame.");
    emit changed();
    return;
  }
  frame_path_ = storage_->filePath("source.png");
  const auto generation = generation_;
  auto* watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, generation] {
    const auto error = watcher->result();
    watcher->deleteLater();
    if (generation != generation_ || !document_ || session_generation_ != document_->working_session_generation_) return;
    loading_ = false;
    error_ = error;
    if (error.isEmpty()) {
      loadRecipe(document_->colorGradeRecipeJsonForClip(clip_id_));
      ready_ = true;
      applyPendingEffect();
    } else {
      pending_effect_type_.clear();
      pending_effect_clip_id_.clear();
    }
    emit changed();
  });
  watcher->setFuture(QtConcurrent::run(prepare_frame, source_path_, frame_path_, std::max(0.0, seconds), input_transform_, storage_));
  emit changed();
}

void TimelineColorEditController::loadRecipe(const QString& recipe) {
  const QScopedValueRollback<bool> guard(synchronizing_, true);
  const auto state = recipe.isEmpty() ? EditRecipeState{} : parse_edit_recipe_json(recipe);
  const auto patch = serialize_adjustment_patch_json(state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
  editor_->loadAsset(QStringLiteral("timeline-color:%1:%2").arg(session_generation_).arg(clip_id_),
      frame_path_, {}, {}, {}, {}, patch, true);
  published_recipe_ = recipe;
  // The editor expands an empty/native recipe to its complete defaults. Merely
  // opening or closing the inspector must not publish that representation as an edit.
  const auto validation = normalize_timeline_video_grade_recipe(
      serialize_edit_recipe_json(editor_->currentState()), &editor_recipe_baseline_);
  if (!validation.isEmpty()) editor_recipe_baseline_.clear();
}

void TimelineColorEditController::publishEdit() {
  if (synchronizing_ || !active_ || !ready_ || !editable_ || !document_ ||
      session_generation_ != document_->working_session_generation_) return;
  const QScopedValueRollback<bool> guard(synchronizing_, true);
  const bool draft = editor_->photo_effect_preview_active_ || editor_->preset_preview_state_.has_value() ||
      editor_->history_preview_state_.has_value();
  if (!draft) {
    QString normalized;
    const auto validation = normalize_timeline_video_grade_recipe(serialize_edit_recipe_json(editor_->currentState()), &normalized);
    if (!validation.isEmpty()) {
      loadRecipe(published_recipe_);
      error_ = validation;
      emit changed();
      return;
    }
    if (normalized != editor_recipe_baseline_) {
      if (editor_->history_service_.transactionActive() && !document_gesture_) {
        document_->beginContinuousEdit();
        document_gesture_ = true;
      }
      const auto result = document_->setClipColorGradeRecipe(clip_id_,
          QJsonDocument::fromJson(normalized.toUtf8()).object(), false);
      if (!result.value("ok").toBool()) {
        // Rejected edits must not remain as an apparently applied private-editor
        // stack while the Timeline still owns the previous recipe.
        loadRecipe(published_recipe_);
        error_ = result.value("error").toString();
        emit changed();
        return;
      }
      published_recipe_ = document_->colorGradeRecipeJsonForClip(clip_id_);
      editor_recipe_baseline_ = normalized;
    }
    if (!editor_->history_service_.transactionActive()) finishGesture();
  }
  if (draft || !editor_->section_comparison_scope_.isEmpty()) {
    QString preview;
    const auto validation = normalize_timeline_video_grade_recipe(serialize_edit_recipe_json(editor_->currentPreviewState()), &preview);
    if (!validation.isEmpty()) { error_ = validation; emit changed(); return; }
    const auto entry = qMakePair(session_generation_, preview);
    if (document_->color_grade_preview_recipes_.value(clip_id_) != entry) {
      document_->color_grade_preview_recipes_.insert(clip_id_, entry);
      emit document_->playbackStateChanged();
    }
  } else clearPreview();
  error_.clear();
  emit changed();
}

QVariantList TimelineColorEditController::availableEffects() const {
  return editor_->availablePhotoEffects();
}

bool TimelineColorEditController::addEffect(const QString& type) {
  if (!document_ || !photo_editor::pipeline::find_photo_effect(type.toStdString(), 1)) return false;
  const auto ref = document_->selectedClipRef();
  const auto* clip = document_->clipAt(ref);
  const auto* track = document_->trackAt(ref.track_index);
  if (document_->selectedClipCount() != 1 || !clip || !track || track->locked ||
      !clip->color_grade_recipe_error.isEmpty() ||
      (clip->media_type != "photo" && clip->media_type != "video" && clip->media_type != "adjustment"))
    return false;
  if (!pending_effect_type_.isEmpty()) return false;
  // Pin an asynchronous request to the selected clip and native working session.
  const auto requested_id = clip->clip_id;
  const auto requested_session = document_->working_session_generation_;
  setActive(true);
  pending_effect_type_ = type;
  pending_effect_clip_id_ = requested_id;
  pending_effect_session_ = requested_session;
  if (ready_ && clip_id_ == requested_id && session_generation_ == requested_session)
    return applyPendingEffect();
  if (loading_) return true;
  pending_effect_type_.clear();
  pending_effect_clip_id_.clear();
  return false;
}

bool TimelineColorEditController::applyPendingEffect() {
  if (pending_effect_type_.isEmpty()) return false;
  const auto type = std::exchange(pending_effect_type_, {});
  const auto target = std::exchange(pending_effect_clip_id_, {});
  if (!active_ || !editable() || !document_ || clip_id_ != target ||
      pending_effect_session_ != document_->working_session_generation_ ||
      document_->selectedClipCount() != 1 || document_->currentClip().value("clipId").toString() != target)
    return false;
  if (editor_->history_service_.transactionActive() || editor_->photo_effect_preview_active_ ||
      editor_->preset_preview_state_.has_value() || editor_->history_preview_state_.has_value()) {
    error_ = QStringLiteral("Apply or cancel the current preview before adding an effect.");
    emit changed();
    return false;
  }
  bool added = false;
  {
    // Layer creation and its initial effect are one native Timeline Undo item.
    // Suppress intermediate publication of an empty adjustment layer.
    const QScopedValueRollback<bool> guard(synchronizing_, true);
    editor_->beginCoalescedEdit();
    const auto index = ensureEffectsLayer();
    added = index >= 0 && editor_->addPhotoLayerEffect(index, type);
    if (added) editor_->endCoalescedEdit();
    else editor_->cancelCoalescedEdit();
  }
  if (added) publishEdit();
  else { error_ = QStringLiteral("This effect could not be added to the selected clip."); emit changed(); }
  return added && error_.isEmpty();
}

int TimelineColorEditController::ensureEffectsLayer() {
  if (!editable()) return -1;
  const auto state = editor_->currentState();
  for (qsizetype index = 0; index < state.layers.size(); ++index) {
    const auto layer = state.layers.at(index).toMap();
    if (layer.value("kind") == "adjustment" && !layer.value("locked").toBool()) return static_cast<int>(index);
  }
  if (!editor_->addAdjustmentLayer(QStringLiteral("Clip effects"))) return -1;
  // addAdjustmentLayer prepends, including when all existing layers are locked.
  return 0;
}
void TimelineColorEditController::resetGrade() {
  if (editable() && document_) document_->clearClipColorGradeRecipeForAutomation(clip_id_);
}
void TimelineColorEditController::undo() { if (document_) { finishGesture(); document_->undo(); } }
void TimelineColorEditController::redo() { if (document_) { finishGesture(); document_->redo(); } }
}  // namespace photo_editor::desktop
