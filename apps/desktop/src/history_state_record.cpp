#include "history_state_record.h"

#include <functional>

namespace photo_editor::desktop {
namespace {
// These are all QVariantList members of EditRecipeState. Traverse recursively so
// mask/paint tiles and nested recipe snapshots retain their exact Qt value types.
constexpr std::array fields{
    &EditRecipeState::master_curve_points, &EditRecipeState::luma_curve_points,
    &EditRecipeState::red_curve_points, &EditRecipeState::green_curve_points,
    &EditRecipeState::blue_curve_points, &EditRecipeState::mask_stack,
    &EditRecipeState::preset_applications, &EditRecipeState::layers,
    &EditRecipeState::recipe_snapshots};
}

std::optional<HistoryStateRecord> HistoryStateRecord::capture(
    const EditRecipeState& state, HistoryPayloadStore& store, QString* warning,
    qsizetype minimum_payload_bytes) {
  if (warning) warning->clear();
  HistoryStateRecord record;
  record.metadata_ = state;
  QVector<PathStep> path;
  bool failed = false;
  std::function<QVariant(const QVariant&, qsizetype)> visit;
  visit = [&](const QVariant& value, qsizetype field) -> QVariant {
    const auto type = value.metaType().id();
    if (type == QMetaType::QByteArray) {
      const auto bytes = value.toByteArray();
      if (bytes.isEmpty() || bytes.size() < minimum_payload_bytes) return value;
      QString error;
      auto handle = store.retain(bytes, &error);
      if (warning && warning->isEmpty() && !error.isEmpty()) *warning = error;
      if (!handle.valid()) { failed = true; return value; }
      record.payloads_.append(Payload{field, path, std::move(handle)});
      return QByteArray{};
    }
    if (type == QMetaType::QVariantList) {
      auto list = value.toList();
      for (qsizetype i = 0; i < list.size(); ++i) {
        path.append(PathStep{PathStep::Kind::List, i, {}});
        list[i] = visit(list.at(i), field);
        path.removeLast();
      }
      return list;
    }
    if (type == QMetaType::QVariantMap) {
      auto map = value.toMap();
      for (auto it = map.begin(); it != map.end(); ++it) {
        path.append(PathStep{PathStep::Kind::Map, 0, it.key()});
        it.value() = visit(it.value(), field);
        path.removeLast();
      }
      return map;
    }
    if (type == QMetaType::QVariantHash) {
      auto hash = value.toHash();
      for (auto it = hash.begin(); it != hash.end(); ++it) {
        path.append(PathStep{PathStep::Kind::Hash, 0, it.key()});
        it.value() = visit(it.value(), field);
        path.removeLast();
      }
      return hash;
    }
    return value;
  };
  for (qsizetype field = 0; field < static_cast<qsizetype>(fields.size()); ++field) {
    const auto member = fields[static_cast<std::size_t>(field)];
    record.metadata_.*member = visit(state.*member, field).toList();
  }
  if (failed) return std::nullopt;
  return record;
}

std::optional<EditRecipeState> HistoryStateRecord::restore(QString* error) const {
  if (error) error->clear();
  auto state = metadata_;
  const auto replace = [](auto&& self, const QVariant& value, const QVector<PathStep>& path,
                          qsizetype step, const QByteArray& bytes) -> QVariant {
    if (step == path.size()) return bytes;
    const auto& part = path.at(step);
    if (part.kind == PathStep::Kind::List) {
      auto list = value.toList();
      list[part.index] = self(self, list.at(part.index), path, step + 1, bytes);
      return list;
    }
    if (part.kind == PathStep::Kind::Map) {
      auto map = value.toMap();
      map[part.key] = self(self, map.value(part.key), path, step + 1, bytes);
      return map;
    }
    auto hash = value.toHash();
    hash[part.key] = self(self, hash.value(part.key), path, step + 1, bytes);
    return hash;
  };
  QHash<QString, QByteArray> materialized;
  for (const auto& payload : payloads_) {
    const auto id = payload.handle.contentId();
    auto found = materialized.constFind(id);
    if (found == materialized.cend()) {
      const auto bytes = payload.handle.load(error);
      if (!bytes) return std::nullopt;
      materialized.insert(id, *bytes);
      found = materialized.constFind(id);
    }
    const auto member = fields[static_cast<std::size_t>(payload.field)];
    state.*member = replace(replace, state.*member, payload.path, 0, found.value()).toList();
  }
  return state;
}

}  // namespace photo_editor::desktop
