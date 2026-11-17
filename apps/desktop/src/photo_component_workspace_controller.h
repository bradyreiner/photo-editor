#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <memory>
#include <vector>

namespace photo_editor::desktop {
class EditorPreviewController;

// Each frame owns a normal editor with an isolated transparent source and
// history. Only Apply publishes its document to the immediate parent frame.
class PhotoComponentWorkspaceController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
  Q_PROPERTY(QObject* controller READ qmlController NOTIFY stateChanged)
  Q_PROPERTY(QVariantList breadcrumbs READ breadcrumbs NOTIFY stateChanged)
  Q_PROPERTY(QString error READ error NOTIFY stateChanged)
  Q_PROPERTY(bool canApply READ canApply NOTIFY stateChanged)
 public:
  explicit PhotoComponentWorkspaceController(EditorPreviewController* editor,
                                             QObject* parent = nullptr);
  ~PhotoComponentWorkspaceController() override;
  [[nodiscard]] bool active() const;
  [[nodiscard]] bool busy() const;
  [[nodiscard]] EditorPreviewController* controller() const;
  [[nodiscard]] QObject* qmlController() const;
  [[nodiscard]] QVariantList breadcrumbs() const;
  [[nodiscard]] QString error() const { return error_; }
  [[nodiscard]] bool canApply() const;
  Q_INVOKABLE bool open(int layer_index);
  Q_INVOKABLE bool apply();
  Q_INVOKABLE bool cancel();
 signals:
  void stateChanged();
 private:
  struct Frame;
  QPointer<EditorPreviewController> root_;
  std::vector<std::unique_ptr<Frame>> frames_;
  std::unique_ptr<Frame> opening_;
  QString error_;
  quint64 generation_ = 0;
  bool applying_ = false;
  void continueOpening();
  void finishOpening(quint64 generation, const QString& error);
  void observe(EditorPreviewController* editor);
  void fail(const QString& error);
  void finishApply();
};
}  // namespace photo_editor::desktop
