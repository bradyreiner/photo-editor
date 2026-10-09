#pragma once

#include <algorithm>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSet>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <utility>

namespace photo_editor::desktop {

// A task has no elapsed-time or total-step budget. Stop a repeated observation
// cycle instead. Call this only for completed batches, never for async polling.
class AgentToolProgress final {
 public:
  static constexpr int maximum_repeated_rounds = 3;

  bool record(const QVariantList& reports) {
    QVariantList receipts;
    for (const auto& value : reports) {
      const auto report = value.toMap();
      const auto content = report.value(QStringLiteral("structuredContent")).toMap();
      QVariantMap state;
      // Native owners supply taskStateHash for pixels and other state omitted
      // from inspection. Retain document IDs and editable state, not UI telemetry.
      for (const auto& key : {QStringLiteral("assetId"), QStringLiteral("taskStateHash"),
          QStringLiteral("changed"), QStringLiteral("alreadySatisfied"),
          QStringLiteral("createdLayerId"), QStringLiteral("createdMaskId"), QStringLiteral("effectId"),
          QStringLiteral("createdClipId"), QStringLiteral("createdClipIds"), QStringLiteral("resourceId"),
          QStringLiteral("layers"), QStringLiteral("masks"), QStringLiteral("recipePatch"),
          QStringLiteral("adjustmentPatchJson"), QStringLiteral("adjustments"),
          QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("error")}) {
        if (content.contains(key)) state.insert(key, stableValue(content.value(key)));
      }
      receipts.append(QVariantMap{{QStringLiteral("toolName"), report.value(QStringLiteral("toolName"))},
          {QStringLiteral("arguments"), stableValue(report.value(QStringLiteral("arguments")))},
          {QStringLiteral("ok"), report.value(QStringLiteral("ok"))},
          {QStringLiteral("error"), report.value(QStringLiteral("error"))},
          {QStringLiteral("state"), state}});
    }
    const auto signature = QCryptographicHash::hash(
        QJsonDocument::fromVariant(receipts).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
    if (seen_.contains(signature)) ++repeated_rounds_;
    else repeated_rounds_ = 0;
    seen_.insert(signature);
    return repeated_rounds_ < maximum_repeated_rounds;
  }

  int repeatedRounds() const { return repeated_rounds_; }

  QVariantMap snapshot() const {
    QStringList signatures;
    signatures.reserve(seen_.size());
    for (const auto& signature : seen_) signatures.append(QString::fromLatin1(signature.toHex()));
    std::sort(signatures.begin(), signatures.end());
    return {{QStringLiteral("contract"), QStringLiteral("agent-tool-progress-v1")},
        {QStringLiteral("signatures"), signatures},
        {QStringLiteral("repeatedRounds"), repeated_rounds_}};
  }

  bool restore(const QVariantMap& state) {
    if (state.value(QStringLiteral("contract")).toString() !=
        QStringLiteral("agent-tool-progress-v1")) return false;
    const auto rounds_value = state.value(QStringLiteral("repeatedRounds"));
    if (rounds_value.metaType().id() != QMetaType::Int) return false;
    const auto rounds = rounds_value.toInt();
    if (rounds < 0 || rounds > maximum_repeated_rounds) return false;
    const auto values = state.value(QStringLiteral("signatures"));
    if (values.metaType().id() != QMetaType::QStringList) return false;
    const auto encoded_signatures = values.toStringList();
    if (encoded_signatures.isEmpty() && rounds != 0) return false;
    QSet<QByteArray> restored;
    for (const auto& encoded : encoded_signatures) {
      const auto signature = QByteArray::fromHex(encoded.toLatin1());
      if (signature.size() != 32 || signature.toHex() != encoded.toLatin1()) return false;
      restored.insert(signature);
    }
    seen_ = std::move(restored);
    repeated_rounds_ = rounds;
    return true;
  }

 private:
  static QVariant stableValue(const QVariant& value) {
    if (value.metaType().id() == QMetaType::QVariantMap) {
      QVariantMap stable;
      const auto map = value.toMap();
      for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.key().endsWith(QStringLiteral("Diagnostics")) ||
            it.key().endsWith(QStringLiteral("ThumbnailSource")) ||
            QStringList{QStringLiteral("taskStepId"), QStringLiteral("elapsedMs"),
                QStringLiteral("timestamp"), QStringLiteral("thumbnailSource"),
                QStringLiteral("paintBusy"), QStringLiteral("selected"),
                QStringLiteral("expanded")}.contains(it.key())) continue;
        // Hash raster payloads rather than growing the progress ledger with them.
        if (it.key().endsWith(QStringLiteral("Data")) || it.value().metaType().id() == QMetaType::QByteArray) {
          const auto bytes = it.value().metaType().id() == QMetaType::QByteArray
              ? it.value().toByteArray() : it.value().toString().toUtf8();
          stable.insert(it.key(), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
        } else stable.insert(it.key(), stableValue(it.value()));
      }
      return stable;
    }
    if (value.metaType().id() == QMetaType::QVariantList || value.metaType().id() == QMetaType::QStringList) {
      QVariantList stable;
      for (const auto& item : value.toList()) stable.append(stableValue(item));
      return stable;
    }
    return value;
  }

  QSet<QByteArray> seen_;
  int repeated_rounds_ = 0;
};

}  // namespace photo_editor::desktop
