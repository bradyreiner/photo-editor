#pragma once

#include <functional>
#include <utility>

namespace photo_editor::desktop {

// Admission is checked before changing document state. Nested calls complete the
// admitted operation. Loading existing documents and finishing/canceling an
// already-started gesture may restore state even after an entitlement expires.
class CustomerEditAccess {
 public:
  void setPolicy(std::function<bool()> policy) { policy_ = std::move(policy); }
  class Scope {
   public:
    Scope(CustomerEditAccess& owner, bool restoring) : owner_(owner),
        admitted_(owner.depth_ > 0 || restoring || !owner.policy_ || owner.policy_()) {
      if (admitted_) ++owner_.depth_;
    }
    ~Scope() { if (admitted_) --owner_.depth_; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    explicit operator bool() const { return admitted_; }
   private:
    CustomerEditAccess& owner_;
    bool admitted_;
  };
  [[nodiscard]] Scope enter() { return Scope(*this, false); }
  [[nodiscard]] Scope restore() { return Scope(*this, true); }
 private:
  std::function<bool()> policy_;
  unsigned depth_ = 0;
};

}  // namespace photo_editor::desktop
