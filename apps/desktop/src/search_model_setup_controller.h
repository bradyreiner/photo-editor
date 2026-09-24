#pragma once

#include "local_similarity_search.h"
#include "search_model_install.h"
#include <QPointer>

namespace photo_editor::desktop {

class SearchModelSetupController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool available READ available NOTIFY changed)
  Q_PROPERTY(bool running READ running NOTIFY changed)
  Q_PROPERTY(bool ready READ ready NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString location READ location CONSTANT)
  Q_PROPERTY(double progress READ progress NOTIFY changed)
 public:
  using State = std::shared_ptr<AgentModelDownloadState>;
  using Verify = std::function<QVariantMap(const QString&, const State&)>;
  using Install = std::function<QVariantMap(const SearchModelInstallRequest&, const State&)>;
  // Dependency overrides are native test seams, never QML/preferences/env hooks.
  SearchModelSetupController(LocalSimilaritySearch* search, QString root, QString device, QString index,
                            QObject* parent = nullptr, Verify verify = {}, Install install = {});
  ~SearchModelSetupController() override;
  void setRuntime(const QString& trusted_executable);
  bool available() const { return !executable_.isEmpty(); }
  bool running() const { return running_; }
  bool ready() const { return ready_; }
  QString status() const { return status_; }
  QString location() const { return root_; }
  double progress() const { return progress_; }
  Q_INVOKABLE bool verify();
  Q_INVOKABLE bool download(bool confirmed = false);
  Q_INVOKABLE bool importFolder(const QUrl& folder);
  Q_INVOKABLE void cancel();
  void shutdown();
 signals:
  void changed();
 private:
  bool begin(const QString& operation, const QString& source = {});
  void pump();
  void finish();
  QPointer<LocalSimilaritySearch> search_;
  QString root_, device_, index_, executable_, operation_, source_;
  QString status_ = "A verified bundled search runtime is required for model setup.";
  bool running_ = false, ready_ = false, pending_ = false, shutting_down_ = false;
  double progress_ = 0;
  State state_;
  Verify verify_;
  Install install_;
  QFutureWatcher<QVariantMap> watcher_;
  QTimer timer_;
  QElapsedTimer waiting_;
};
}  // namespace photo_editor::desktop
