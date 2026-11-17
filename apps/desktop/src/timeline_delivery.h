#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>
#include <QtGlobal>

#include <atomic>
#include <functional>
#include <memory>

namespace photo_editor::desktop::timeline_delivery {

[[nodiscard]] QString resolveDocumentResourcePath(
    const QString& path,
    const QString& document_path);

[[nodiscard]] QVariantMap createPortableProjectArchive(
    const QString& destination_path,
    const QByteArray& document_json,
    const QString& project_name,
    const QString& source_document_path,
    const std::shared_ptr<std::atomic_bool>& cancellation,
    // Tests lower this boundary to validate ZIP64 without multi-gigabyte fixtures.
    quint64 zip32_max_value_for_testing = 0xfffffffeULL,
    // Runs after writing and immediately before the no-replace publish step.
    const std::function<void()>& before_publish_for_testing = {});

[[nodiscard]] QVariantMap inspectRenderedOutput(
    const QString& output_path,
    const std::shared_ptr<std::atomic_bool>& cancellation);

}  // namespace photo_editor::desktop::timeline_delivery
