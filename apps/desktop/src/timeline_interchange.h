#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace photo_editor::desktop::timeline_interchange {

struct ImportPlan final {
  bool valid = false;
  QJsonObject native_document;
  QStringList errors;
  QStringList lossy_fields;
  int imported_tracks = 0;
  int imported_clips = 0;
};

struct ExportPlan final {
  bool valid = false;
  QByteArray otio_json;
  QStringList errors;
  QStringList lossy_fields;
  int exported_tracks = 0;
  int exported_clips = 0;
};

// Converts the documented OTIO JSON schema subset into a candidate native
// document. The caller must show lossy_fields before applying the candidate.
[[nodiscard]] ImportPlan preview_otio_import(
    const QByteArray& otio_json,
    const QString& otio_source_path,
    const QJsonObject& current_native_document);

// Exports cuts, source trims, media references, and video/audio tracks. Media
// targets are relative to the output OTIO file. Any native data outside that
// subset is named in lossy_fields.
[[nodiscard]] ExportPlan export_otio(
    const QJsonObject& native_document,
    const QString& otio_output_path);

}  // namespace photo_editor::desktop::timeline_interchange
