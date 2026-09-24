#pragma once

#include <utility>

#include <QVariantList>
#include <QVariantMap>

namespace photo_editor::desktop {

class LayerSession final {
 public:
  [[nodiscard]] const QVariantList& layers() const { return layers_; }
  [[nodiscard]] int layerCount() const {
    return static_cast<int>(layers_.size());
  }

  [[nodiscard]] bool replaceLayers(QVariantList layers) {
    if (layers == layers_) {
      return false;
    }
    layers_ = std::move(layers);
    return true;
  }

  [[nodiscard]] const QVariantList& visibilityStates() const {
    return visibility_states_;
  }
  [[nodiscard]] int visibilityStateCount() const {
    return static_cast<int>(visibility_states_.size());
  }

  void appendVisibilityState(QVariantMap state) {
    visibility_states_.push_back(std::move(state));
  }

  [[nodiscard]] bool removeVisibilityState(int index) {
    if (index < 0 || index >= visibility_states_.size()) {
      return false;
    }
    visibility_states_.removeAt(index);
    return true;
  }

  [[nodiscard]] bool hasCopiedAdjustments() const {
    return has_copied_adjustments_;
  }
  [[nodiscard]] const QVariantMap& copiedAdjustments() const {
    return copied_adjustments_;
  }

  [[nodiscard]] bool copyAdjustments(QVariantMap adjustments) {
    const auto changed = !has_copied_adjustments_ ||
                         copied_adjustments_ != adjustments;
    copied_adjustments_ = std::move(adjustments);
    has_copied_adjustments_ = true;
    return changed;
  }

  [[nodiscard]] bool clearCopiedAdjustments() {
    if (!has_copied_adjustments_) {
      return false;
    }
    copied_adjustments_.clear();
    has_copied_adjustments_ = false;
    return true;
  }

 private:
  QVariantList layers_;
  QVariantList visibility_states_;
  QVariantMap copied_adjustments_;
  bool has_copied_adjustments_ = false;
};

}  // namespace photo_editor::desktop
