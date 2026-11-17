#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace photo_editor::desktop::layer_edit_access {

class LayerLockIndex final {
 public:
  explicit LayerLockIndex(const QVariantList& layers) {
    ids_by_index_.reserve(layers.size());
    for (const auto& value : layers) {
      if (value.metaType().id() != QMetaType::QVariantMap) {
        ids_by_index_.push_back({});
        continue;
      }
      const auto layer = value.toMap();
      const auto id = layer.value(QStringLiteral("id")).toString().trimmed();
      ids_by_index_.push_back(id);
      if (id.isEmpty()) continue;
      if (by_id_.contains(id)) {
        ambiguous_ids_.insert(id);
      } else {
        by_id_.insert(id, layer);
      }
    }
  }

  [[nodiscard]] bool has_locked_ancestor(int index) const {
    QString current_id;
    if (!valid_target(index, &current_id)) return true;
    return walk_ancestors(current_id);
  }

  [[nodiscard]] bool effectively_locked(int index) const {
    QString current_id;
    if (!valid_target(index, &current_id)) return true;
    if (by_id_.value(current_id).value(QStringLiteral("locked")).toBool()) {
      return true;
    }
    return walk_ancestors(current_id);
  }

 private:
  [[nodiscard]] bool valid_target(int index, QString* id) const {
    if (id == nullptr || index < 0 || index >= ids_by_index_.size()) return false;
    *id = ids_by_index_.at(index);
    return !id->isEmpty() && !ambiguous_ids_.contains(*id) && by_id_.contains(*id);
  }

  [[nodiscard]] bool walk_ancestors(QString current_id) const {
    QSet<QString> visited{current_id};
    auto parent_id = by_id_.value(current_id)
                         .value(QStringLiteral("parentId"))
                         .toString().trimmed();
    while (!parent_id.isEmpty()) {
      if (visited.contains(parent_id) || ambiguous_ids_.contains(parent_id) ||
          !by_id_.contains(parent_id)) {
        return true;
      }
      visited.insert(parent_id);
      const auto parent = by_id_.value(parent_id);
      if (parent.value(QStringLiteral("kind")).toString().trimmed().toLower() !=
          QStringLiteral("group")) {
        return true;
      }
      if (parent.value(QStringLiteral("locked")).toBool()) return true;
      parent_id = parent.value(QStringLiteral("parentId")).toString().trimmed();
    }
    return false;
  }

  QStringList ids_by_index_;
  QHash<QString, QVariantMap> by_id_;
  QSet<QString> ambiguous_ids_;
};

// LayerSession keeps groups and their descendants in one flat list connected
// by parentId. An unresolved, ambiguous, malformed, or cyclic ancestry chain
// fails closed so a broken hierarchy cannot bypass an ancestor lock.
[[nodiscard]] inline bool is_effectively_locked(const QVariantList& layers,
                                                int index) {
  return LayerLockIndex(layers).effectively_locked(index);
}

[[nodiscard]] inline bool has_forbidden_locked_layer_change(
    const QVariantList& before, const QVariantList& after) {
  const auto old_locks = LayerLockIndex(before);
  const auto new_locks = LayerLockIndex(after);
  QHash<QString, QVariantMap> old_by_id;
  QHash<QString, QVariantMap> new_by_id;
  QHash<QString, int> old_index_by_id;
  QHash<QString, int> new_index_by_id;
  QSet<QString> old_ambiguous;
  QSet<QString> new_ambiguous;
  QHash<QString, QStringList> old_siblings;
  QHash<QString, QStringList> new_siblings;

  const auto index_layers = [](const QVariantList& layers,
                               QHash<QString, QVariantMap>* records,
                               QHash<QString, int>* indices,
                               QSet<QString>* ambiguous,
                               QHash<QString, QStringList>* siblings) {
    for (auto index = 0; index < layers.size(); ++index) {
      if (layers.at(index).metaType().id() != QMetaType::QVariantMap) return false;
      const auto layer = layers.at(index).toMap();
      const auto id = layer.value(QStringLiteral("id")).toString().trimmed();
      if (id.isEmpty()) return false;
      if (records->contains(id)) {
        ambiguous->insert(id);
        continue;
      }
      records->insert(id, layer);
      indices->insert(id, index);
      siblings->operator[](layer.value(QStringLiteral("parentId")).toString().trimmed())
          .push_back(id);
    }
    return ambiguous->isEmpty();
  };
  if (!index_layers(before, &old_by_id, &old_index_by_id, &old_ambiguous,
                    &old_siblings) ||
      !index_layers(after, &new_by_id, &new_index_by_id, &new_ambiguous,
                    &new_siblings)) {
    return true;
  }

  QHash<QString, QStringList> old_common_order;
  QHash<QString, QStringList> new_common_order;
  for (auto parent = old_siblings.cbegin(); parent != old_siblings.cend(); ++parent) {
    auto& common = old_common_order[parent.key()];
    for (const auto& id : parent.value()) {
      const auto next = new_by_id.constFind(id);
      if (next != new_by_id.cend() &&
          next->value(QStringLiteral("parentId")).toString().trimmed() == parent.key()) {
        common.push_back(id);
      }
    }
  }
  for (auto parent = new_siblings.cbegin(); parent != new_siblings.cend(); ++parent) {
    auto& common = new_common_order[parent.key()];
    for (const auto& id : parent.value()) {
      const auto previous = old_by_id.constFind(id);
      if (previous != old_by_id.cend() &&
          previous->value(QStringLiteral("parentId")).toString().trimmed() == parent.key()) {
        common.push_back(id);
      }
    }
  }
  QHash<QString, QSet<QString>> protected_siblings;
  for (auto index = 0; index < before.size(); ++index) {
    const auto layer = before.at(index).toMap();
    if (old_locks.effectively_locked(index)) {
      protected_siblings[layer.value(QStringLiteral("parentId")).toString().trimmed()]
          .insert(layer.value(QStringLiteral("id")).toString().trimmed());
    }
  }
  for (auto index = 0; index < after.size(); ++index) {
    const auto layer = after.at(index).toMap();
    if (new_locks.effectively_locked(index)) {
      protected_siblings[layer.value(QStringLiteral("parentId")).toString().trimmed()]
          .insert(layer.value(QStringLiteral("id")).toString().trimmed());
    }
  }

  const auto authored_fields = [](QVariantMap layer) {
    static const QSet<QString> view_metadata{
        QStringLiteral("visible"), QStringLiteral("enabled"),
        QStringLiteral("expanded"), QStringLiteral("isExpanded"),
        QStringLiteral("collapsed"), QStringLiteral("locked"),
        QStringLiteral("layerAuditLabel"), QStringLiteral("name"),
        QStringLiteral("displayName"), QStringLiteral("solo"),
        QStringLiteral("displaySubtitle"), QStringLiteral("kindLabel"),
        QStringLiteral("selected"), QStringLiteral("flatIndex"),
        QStringLiteral("dependencyLabel"), QStringLiteral("opacityPercent"),
        QStringLiteral("maskOpacityPercent"), QStringLiteral("maskDensityPercent"),
        QStringLiteral("hasLayerMask"), QStringLiteral("layerMaskLabel"),
        QStringLiteral("hasNonNeutralAdjustments"),
        QStringLiteral("changedAdjustmentGroups"),
        QStringLiteral("changedAdjustmentGroupLabel")};
    for (auto it = layer.begin(); it != layer.end();) {
      if (it.key().startsWith(QStringLiteral("thumbnail"), Qt::CaseInsensitive) ||
          view_metadata.contains(it.key())) {
        it = layer.erase(it);
      } else {
        ++it;
      }
    }
    return layer;
  };

  for (auto index = 0; index < before.size(); ++index) {
    const auto layer = before.at(index).toMap();
    const auto id = layer.value(QStringLiteral("id")).toString().trimmed();
    const auto next = new_by_id.constFind(id);
    const auto was_locked = old_locks.effectively_locked(index);
    if (next == new_by_id.cend()) {
      if (was_locked) return true;
      continue;
    }
    const auto next_index = new_index_by_id.value(id, -1);
    const auto is_locked = new_locks.effectively_locked(next_index);
    if ((was_locked || is_locked) && authored_fields(layer) != authored_fields(next.value())) {
      return true;
    }
  }

  // A locked row keeps its relative position against siblings. Unlocked
  // siblings may still reorder among themselves within the same gap; this
  // keeps an unrelated root-level reorder from being blocked by a locked row.
  for (auto parent = protected_siblings.cbegin(); parent != protected_siblings.cend(); ++parent) {
    const auto old_order = old_common_order.value(parent.key());
    const auto new_order = new_common_order.value(parent.key());
    if (old_order == new_order) continue;

    QStringList old_protected_order;
    QStringList new_protected_order;
    QHash<QString, int> old_unlocked_gaps;
    QHash<QString, int> new_unlocked_gaps;
    auto old_gap = 0;
    for (const auto& id : old_order) {
      if (parent.value().contains(id)) {
        old_protected_order.push_back(id);
        ++old_gap;
      } else {
        old_unlocked_gaps.insert(id, old_gap);
      }
    }
    auto new_gap = 0;
    for (const auto& id : new_order) {
      if (parent.value().contains(id)) {
        new_protected_order.push_back(id);
        ++new_gap;
      } else {
        new_unlocked_gaps.insert(id, new_gap);
      }
    }
    if (old_protected_order != new_protected_order ||
        old_unlocked_gaps != new_unlocked_gaps) {
      return true;
    }
  }

  for (auto index = 0; index < after.size(); ++index) {
    const auto id = after.at(index).toMap().value(QStringLiteral("id")).toString().trimmed();
    if (!old_by_id.contains(id) && new_locks.has_locked_ancestor(index)) return true;
  }
  return false;
}

}  // namespace photo_editor::desktop::layer_edit_access
