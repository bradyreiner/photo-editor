#pragma once
#include <QStringList>
#include <QVariantMap>
#include <atomic>
#include <memory>

namespace photo_editor::desktop {
QVariantMap prepare_gaussian_capture(const QStringList& sources, const QString& project,
                                    int maximum_edge, const std::shared_ptr<std::atomic_bool>& canceled);
QVariantMap prepare_gaussian_timed_capture(const QString& manifest, const QString& new_project,
                                          int maximum_edge, const std::shared_ptr<std::atomic_bool>& canceled);
QVariantMap validate_prepared_gaussian_timed_capture(const QString& project,
    const std::shared_ptr<std::atomic_bool>& canceled);
}
