#pragma once

#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QPair>
#include <QPointer>
#include <QQuickWindow>
#include <QSignalMapper>
#include <QString>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

namespace photo_editor::desktop {

// Associates a QML readiness transition with the scene-graph synchronization
// and frame swap that actually presented that ready workspace.
class WorkspacePresentationObserver final {
 public:
  using ReadinessPredicate = std::function<bool(quint64)>;

  WorkspacePresentationObserver(
      QObject* root,
      QQuickWindow* window,
      QObject* owner,
      ReadinessPredicate readiness_predicate)
      : root_(root), window_(window), owner_(owner),
        state_(std::make_shared<SharedState>()) {
    state_->readiness_predicate = std::move(readiness_predicate);
    if (root_ == nullptr || window_ == nullptr || owner_ == nullptr ||
        !state_->readiness_predicate) {
      error_message_ = QStringLiteral("The presentation observer requires a root, window, owner, and readiness predicate.");
      return;
    }

    refreshReadinessObservers();

    const auto shared_state = state_;
    const auto sync_connection = QObject::connect(
        window_, &QQuickWindow::beforeSynchronizing, owner_,
        [shared_state]() {
          const auto active =
              shared_state->active_generation.load(std::memory_order_acquire);
          const auto ready =
              shared_state->ready_generation.load(std::memory_order_acquire);
          shared_state->synchronized_generation.store(
              active != 0 && active == ready ? active : 0,
              std::memory_order_release);
        },
        Qt::DirectConnection);
    const auto frame_connection = QObject::connect(
        window_, &QQuickWindow::frameSwapped, owner_,
        [shared_state]() {
          const auto synchronized =
              shared_state->synchronized_generation.exchange(
                  0, std::memory_order_acq_rel);
          shared_state->frame_swap_count.fetch_add(
              1, std::memory_order_relaxed);
          if (synchronized != 0 &&
              shared_state->active_generation.load(std::memory_order_acquire) ==
                  synchronized &&
              shared_state->ready_generation.load(std::memory_order_acquire) ==
                  synchronized) {
            shared_state->presented_generation.store(
                synchronized, std::memory_order_release);
          }
        },
        Qt::DirectConnection);
    if (!sync_connection || !frame_connection) {
      fail(QStringLiteral("The native scene synchronization and frame swap signals could not be observed."));
      return;
    }
    connections_.push_back(sync_connection);
    connections_.push_back(frame_connection);
    installed_ = error_message_.isEmpty();
  }

  WorkspacePresentationObserver(const WorkspacePresentationObserver&) = delete;
  WorkspacePresentationObserver& operator=(const WorkspacePresentationObserver&) = delete;

  ~WorkspacePresentationObserver() {
    for (const auto& connection : connections_) {
      QObject::disconnect(connection);
    }
    if (state_ != nullptr) {
      state_->readiness_predicate = {};
      state_->active_generation.store(0, std::memory_order_release);
      state_->ready_generation.store(0, std::memory_order_release);
    }
  }

  [[nodiscard]] bool installed() const { return installed_; }

  [[nodiscard]] QString errorMessage() const { return error_message_; }

  void beginGeneration(quint64 generation) {
    if (!installed_ || state_ == nullptr || generation == 0) {
      return;
    }
    state_->ready_generation.store(0, std::memory_order_release);
    state_->synchronized_generation.store(0, std::memory_order_release);
    state_->presented_generation.store(0, std::memory_order_release);
    state_->active_generation.store(generation, std::memory_order_release);
    refreshReadinessObservers();
  }

  void noteReadinessChanged() {
    refreshReadinessObservers();
  }

  [[nodiscard]] bool hasPresentedGeneration(quint64 generation) const {
    return generation != 0 && state_ != nullptr &&
           state_->presented_generation.load(std::memory_order_acquire) ==
               generation &&
           state_->ready_generation.load(std::memory_order_acquire) == generation;
  }

  [[nodiscard]] quint64 frameSwapCount() const {
    return state_ == nullptr
               ? 0
               : state_->frame_swap_count.load(std::memory_order_relaxed);
  }

 private:
  struct SharedState final {
    std::atomic<quint64> active_generation{0};
    std::atomic<quint64> ready_generation{0};
    std::atomic<quint64> synchronized_generation{0};
    std::atomic<quint64> presented_generation{0};
    std::atomic<quint64> frame_swap_count{0};
    ReadinessPredicate readiness_predicate;
  };

  static void reevaluateReadiness(const std::shared_ptr<SharedState>& state) {
    if (state == nullptr) {
      return;
    }
    const auto generation =
        state->active_generation.load(std::memory_order_acquire);
    if (generation == 0) {
      return;
    }
    const auto ready = state->readiness_predicate(generation);
    if (ready) {
      state->ready_generation.store(generation, std::memory_order_release);
    } else if (state->ready_generation.load(std::memory_order_acquire) ==
               generation) {
      state->ready_generation.store(0, std::memory_order_release);
    }
  }

  void refreshReadinessObservers() {
    if (root_ == nullptr || !state_) {
      return;
    }

    int mapping = 1;
    const auto observe = [this, &mapping](QObject* object,
                                           const QString& property_name,
                                           bool required) {
      connectReadinessProperty(object, property_name, mapping++, required);
    };
    for (const auto& property_name : {
             QStringLiteral("currentWorkspace"),
             QStringLiteral("workspaceTransitionActive"),
             QStringLiteral("editWorkspaceLoaded"),
             QStringLiteral("libraryWorkspaceLoaded"),
             QStringLiteral("libraryLoadingSceneVisible"),
             QStringLiteral("timelineWorkspaceLoaded")}) {
      observe(root_, property_name, true);
    }

    observe(root_->findChild<QObject*>(
                QStringLiteral("editStudioWorkspace"),
                Qt::FindChildrenRecursively),
            QStringLiteral("pageContentReady"), false);
    observe(root_->findChild<QObject*>(
                QStringLiteral("timelineWorkspace"),
                Qt::FindChildrenRecursively),
            QStringLiteral("timelineEditorPageLoaded"), false);
    observe(root_->findChild<QObject*>(
                QStringLiteral("timelineWorkspaceLoadingSurface"),
                Qt::FindChildrenRecursively),
            QStringLiteral("visible"), false);
    reevaluateReadiness(state_);
  }

  void connectReadinessProperty(
      QObject* object,
      const QString& property_name,
      int mapping,
      bool required) {
    if (object == nullptr) {
      if (required) {
        fail(QStringLiteral("Main.qml is missing readiness property %1.")
                 .arg(property_name));
      }
      return;
    }
    const auto property_index = object->metaObject()->indexOfProperty(
        property_name.toUtf8().constData());
    if (property_index < 0) {
      // Some lazy workspace objects are created after the observer starts.
      if (required) {
        fail(QStringLiteral("Main.qml is missing readiness property %1.")
                 .arg(property_name));
      }
      return;
    }
    const auto property = object->metaObject()->property(property_index);
    if (!property.hasNotifySignal()) {
      fail(QStringLiteral("Workspace readiness property %1 has no notification.")
               .arg(property_name));
      return;
    }
    for (auto iterator = observed_properties_.cbegin();
         iterator != observed_properties_.cend(); ++iterator) {
      if (iterator->first == object && iterator->second == property_index) {
        return;
      }
    }
    auto* mapper = new QSignalMapper(owner_);
    mapper->setMapping(object, mapping);
    const auto map_slot_index = mapper->metaObject()->indexOfSlot("map()");
    if (map_slot_index < 0) {
      fail(QStringLiteral("Qt signal mapping for %1 is unavailable.")
               .arg(property_name));
      mapper->deleteLater();
      return;
    }
    const auto weak_state = std::weak_ptr<SharedState>(state_);
    const auto mapped_connection = QObject::connect(
        mapper, &QSignalMapper::mappedInt, owner_,
        [this, weak_state](int) {
          if (const auto shared_state = weak_state.lock()) {
            refreshReadinessObservers();
            reevaluateReadiness(shared_state);
          }
        },
        Qt::DirectConnection);
    const auto signal_connection = QObject::connect(
        object, property.notifySignal(), mapper,
        mapper->metaObject()->method(map_slot_index), Qt::DirectConnection);
    if (!mapped_connection || !signal_connection) {
      fail(QStringLiteral("The %1 readiness notification could not be connected.")
               .arg(property_name));
      mapper->deleteLater();
      return;
    }
    connections_.push_back(mapped_connection);
    connections_.push_back(signal_connection);
    observed_properties_.push_back(
        qMakePair(QPointer<QObject>(object), property_index));
  }

  void fail(const QString& message) {
    if (error_message_.isEmpty()) {
      error_message_ = message;
    }
    installed_ = false;
  }

  QPointer<QObject> root_;
  QPointer<QQuickWindow> window_;
  QPointer<QObject> owner_;
  std::shared_ptr<SharedState> state_;
  QVector<QMetaObject::Connection> connections_;
  QVector<QPair<QPointer<QObject>, int>> observed_properties_;
  QString error_message_;
  bool installed_ = false;
};

}  // namespace photo_editor::desktop
