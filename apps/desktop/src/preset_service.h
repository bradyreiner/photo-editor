#pragma once

#include <utility>

#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtGlobal>

#include "edit_recipe_state.h"

namespace photo_editor::desktop {

class PresetService final {
 public:
  using StateMap = QMap<QString, EditRecipeState>;

  void replaceBuiltIns(
      StateMap presets,
      QMap<QString, QString> packs,
      QMap<QString, QString> categories,
      QMap<QString, QStringList> groups,
      QMap<QString, QJsonArray> adaptive_rules) {
    built_in_presets_ = std::move(presets);
    built_in_packs_ = std::move(packs);
    built_in_categories_ = std::move(categories);
    built_in_groups_ = std::move(groups);
    built_in_adaptive_rules_ = std::move(adaptive_rules);
    built_in_contracts_.clear();
  }

  void setBuiltInContract(QString name, QVariantMap contract) {
    built_in_contracts_.insert(std::move(name), std::move(contract));
  }

  [[nodiscard]] const StateMap& builtInPresets() const { return built_in_presets_; }
  [[nodiscard]] const QMap<QString, QString>& builtInPacks() const { return built_in_packs_; }
  [[nodiscard]] const QMap<QString, QString>& builtInCategories() const {
    return built_in_categories_;
  }
  [[nodiscard]] const QMap<QString, QStringList>& builtInGroups() const {
    return built_in_groups_;
  }
  [[nodiscard]] const QMap<QString, QJsonArray>& builtInAdaptiveRules() const {
    return built_in_adaptive_rules_;
  }
  [[nodiscard]] const QMap<QString, QVariantMap>& builtInContracts() const {
    return built_in_contracts_;
  }

  void clearUsers() {
    user_presets_.clear();
    user_packs_.clear();
    user_categories_.clear();
    user_groups_.clear();
    user_adaptive_rules_.clear();
    user_contracts_.clear();
    favorites_.clear();
  }

  void upsertUser(
      QString name,
      EditRecipeState state,
      QString pack,
      QString category,
      QStringList groups,
      QJsonArray adaptive_rules,
      QVariantMap contract) {
    user_presets_.insert(name, std::move(state));
    user_packs_.insert(name, std::move(pack));
    user_categories_.insert(name, std::move(category));
    user_groups_.insert(name, std::move(groups));
    if (adaptive_rules.isEmpty()) {
      user_adaptive_rules_.remove(name);
    } else {
      user_adaptive_rules_.insert(name, std::move(adaptive_rules));
    }
    user_contracts_.insert(std::move(name), std::move(contract));
  }

  [[nodiscard]] bool renameUser(const QString& old_name, const QString& new_name) {
    if (!user_presets_.contains(old_name) || user_presets_.contains(new_name) ||
        built_in_presets_.contains(new_name)) {
      return false;
    }
    user_presets_.insert(new_name, user_presets_.take(old_name));
    user_packs_.insert(new_name, user_packs_.take(old_name));
    user_categories_.insert(new_name, user_categories_.take(old_name));
    user_groups_.insert(new_name, user_groups_.take(old_name));
    if (user_adaptive_rules_.contains(old_name)) {
      user_adaptive_rules_.insert(new_name, user_adaptive_rules_.take(old_name));
    }
    if (user_contracts_.contains(old_name)) {
      user_contracts_.insert(new_name, user_contracts_.take(old_name));
    }
    if (favorites_.remove(old_name) > 0) {
      favorites_.insert(new_name);
    }
    return true;
  }

  [[nodiscard]] bool moveUserToPack(
      const QString& name,
      const QString& pack,
      const QString& category) {
    if (!user_presets_.contains(name)) {
      return false;
    }
    user_packs_.insert(name, pack);
    user_categories_.insert(name, category);
    return true;
  }

  [[nodiscard]] bool removeUser(const QString& name) {
    if (user_presets_.remove(name) == 0) {
      return false;
    }
    user_packs_.remove(name);
    user_categories_.remove(name);
    user_groups_.remove(name);
    user_adaptive_rules_.remove(name);
    user_contracts_.remove(name);
    favorites_.remove(name);
    return true;
  }

  [[nodiscard]] const StateMap& userPresets() const { return user_presets_; }
  [[nodiscard]] const QMap<QString, QString>& userPacks() const { return user_packs_; }
  [[nodiscard]] const QMap<QString, QString>& userCategories() const {
    return user_categories_;
  }
  [[nodiscard]] const QMap<QString, QStringList>& userGroups() const { return user_groups_; }
  [[nodiscard]] const QMap<QString, QJsonArray>& userAdaptiveRules() const {
    return user_adaptive_rules_;
  }
  [[nodiscard]] const QMap<QString, QVariantMap>& userContracts() const {
    return user_contracts_;
  }

  [[nodiscard]] bool setFavorite(const QString& name, bool favorite) {
    if (!user_presets_.contains(name) && !built_in_presets_.contains(name)) {
      return false;
    }
    const auto had_favorite = favorites_.contains(name);
    if (favorite) {
      favorites_.insert(name);
    } else {
      favorites_.remove(name);
    }
    return had_favorite != favorite;
  }
  [[nodiscard]] const QSet<QString>& favorites() const { return favorites_; }

  [[nodiscard]] bool setIntensity(double intensity) {
    const auto clamped = qBound(0.0, intensity, 1.0);
    if (qAbs(intensity_ - clamped) <= 1e-6) {
      return false;
    }
    intensity_ = clamped;
    return true;
  }
  [[nodiscard]] double intensity() const { return intensity_; }

  void clearCameraDefaults() { camera_defaults_.clear(); }
  void setCameraDefault(QString key, EditRecipeState state) {
    camera_defaults_.insert(std::move(key), std::move(state));
  }
  [[nodiscard]] bool removeCameraDefault(const QString& key) {
    return camera_defaults_.remove(key) > 0;
  }
  [[nodiscard]] const StateMap& cameraDefaults() const { return camera_defaults_; }

 private:
  StateMap built_in_presets_;
  QMap<QString, QString> built_in_packs_;
  QMap<QString, QString> built_in_categories_;
  QMap<QString, QStringList> built_in_groups_;
  QMap<QString, QJsonArray> built_in_adaptive_rules_;
  QMap<QString, QVariantMap> built_in_contracts_;
  StateMap user_presets_;
  QMap<QString, QString> user_packs_;
  QMap<QString, QString> user_categories_;
  QMap<QString, QStringList> user_groups_;
  QMap<QString, QJsonArray> user_adaptive_rules_;
  QMap<QString, QVariantMap> user_contracts_;
  QSet<QString> favorites_;
  double intensity_ = 1.0;
  StateMap camera_defaults_;
};

}  // namespace photo_editor::desktop
