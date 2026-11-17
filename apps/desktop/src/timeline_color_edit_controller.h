#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <memory>

class QTemporaryDir;

namespace photo_editor::desktop {
class EditorPreviewController;
class EditorPreviewImageSink;
class TimelineDocumentController;

// The Edit inspector authors a clip-owned photo recipe. Its private editor
// never saves to the Library; Timeline owns persistence, history and delivery.
class TimelineColorEditController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QObject* editor READ editor CONSTANT)
  Q_PROPERTY(QVariantList availableEffects READ availableEffects CONSTANT)
  Q_PROPERTY(bool active READ active WRITE setActive NOTIFY changed)
  Q_PROPERTY(bool ready READ ready NOTIFY changed)
  Q_PROPERTY(bool editable READ editable NOTIFY changed)
  Q_PROPERTY(bool loading READ loading NOTIFY changed)
  Q_PROPERTY(QString clipId READ clipId NOTIFY changed)
  Q_PROPERTY(QString targetLabel READ targetLabel NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString samplingTarget READ samplingTarget NOTIFY changed)
  Q_PROPERTY(bool colorMixerSampling READ colorMixerSampling WRITE setColorMixerSampling NOTIFY changed)

 public:
  TimelineColorEditController(TimelineDocumentController* document,
      EditorPreviewController* policy_owner = nullptr, QObject* parent = nullptr);
  ~TimelineColorEditController() override;
  QObject* editor() const;
  bool active() const { return active_; }
  bool ready() const { return ready_; }
  bool editable() const { return ready_ && editable_; }
  bool loading() const { return loading_; }
  QString clipId() const { return clip_id_; }
  QString targetLabel() const { return target_label_; }
  QString error() const { return error_; }
  QString samplingTarget() const;
  bool colorMixerSampling() const { return color_mixer_sampling_; }
  void setColorMixerSampling(bool enabled);
  void setActive(bool active);
  QVariantList availableEffects() const;
  Q_INVOKABLE bool addEffect(const QString& type);
  Q_INVOKABLE int ensureEffectsLayer();
  Q_INVOKABLE void resetGrade();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void sampleProgramAt(double x, double y);
  Q_INVOKABLE void cancelSampling();
  Q_INVOKABLE void finishColorMixerGesture();

 signals:
  void changed();

 private:
  void synchronize();
  bool applyPendingEffect();
  void publishEdit();
  void clearPreview();
  void finishGesture();
  void loadRecipe(const QString& recipe);
  QPointer<TimelineDocumentController> document_;
  QPointer<EditorPreviewController> policy_owner_;
  std::unique_ptr<EditorPreviewImageSink> image_sink_;
  std::unique_ptr<EditorPreviewController> editor_;
  std::shared_ptr<QTemporaryDir> storage_;
  QTimer publication_timer_;
  QString clip_id_, target_label_, source_path_, frame_path_, published_recipe_, error_, input_transform_;
  QString editor_recipe_baseline_;
  QString pending_effect_type_, pending_effect_clip_id_;
  quint64 pending_effect_session_ = 0;
  quint64 generation_ = 0, session_generation_ = 0;
  quint64 sample_generation_ = 0;
  bool active_ = false, ready_ = false, editable_ = false, loading_ = false;
  bool synchronizing_ = false, document_gesture_ = false;
  bool sampling_ = false;
  bool color_mixer_sampling_ = false;
};
}  // namespace photo_editor::desktop
