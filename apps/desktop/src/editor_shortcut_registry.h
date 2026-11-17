#pragma once

#include <QKeySequence>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <algorithm>

namespace photo_editor::desktop {

struct EditorShortcut final {
  QString id;
  QString label;
  QString sequence;
  QString context;
};

inline const QVector<EditorShortcut>& editor_shortcuts() {
  static const QVector<EditorShortcut> definitions{
      {"command-palette", "Command Palette", "Ctrl+K", "global"},
      {"workspace-library", "Go To Library", "Ctrl+1", "global"},
      {"workspace-edit", "Go To Edit", "Ctrl+2", "global"},
      {"tool-adjust", "Adjust Tool", "Ctrl+Alt+A", "global"},
      {"tool-crop", "Crop Tool", "Ctrl+Alt+C", "global"},
      {"tool-masks", "Masks Tool", "Ctrl+Alt+M", "global"},
      {"tool-brush", "Brush Tool", "Ctrl+Alt+B", "global"},
      {"tool-compare", "Compare Tool", "Ctrl+Alt+P", "global"},
      {"tool-export", "Export Tool", "Ctrl+Alt+E", "global"},
      {"queue-export", "Start Export", "Ctrl+Shift+E", "global"},
      {"toggle-full-edit", "Full-Screen Edit", "F", "photo"},
      {"timeline-select", "Timeline · Select Tool", "V", "timeline"},
      {"timeline-track-select", "Timeline · Track Select Tool", "A", "timeline"},
      {"timeline-track-select-backward", "Timeline · Track Select Backward Tool", "Shift+A", "timeline"},
      {"timeline-razor", "Timeline · Razor Tool", "C", "timeline"},
      {"timeline-rate-stretch", "Timeline · Rate Stretch Tool", "R", "timeline"},
      {"timeline-pen", "Timeline · Pen Tool", "P", "timeline"},
      {"timeline-hand", "Timeline · Hand Tool", "H", "timeline"},
      {"timeline-zoom-tool", "Timeline · Zoom Tool", "Z", "timeline"},
      {"timeline-slip", "Timeline · Slip Tool", "Y", "timeline"},
      {"timeline-roll", "Timeline · Roll Tool", "N", "timeline"},
      {"timeline-slide", "Timeline · Slide Tool", "U", "timeline"},
      {"timeline-trim-edge", "Timeline · Toggle Trim Edge", "T", "timeline"},
      {"timeline-snap", "Timeline · Toggle Snapping", "S", "timeline"},
      {"timeline-split", "Timeline · Split At Playhead", "B", "timeline"},
      {"timeline-split-alternate", "Timeline · Split At Playhead (alternate)", "Ctrl+\\", "timeline"},
      {"timeline-marker", "Timeline · Add Marker", "M", "timeline"},
      {"timeline-play", "Timeline · Play / Pause", "Space", "timeline"},
      {"timeline-reverse", "Timeline · Shuttle Reverse", "J", "timeline"},
      {"timeline-stop", "Timeline · Stop Shuttle", "K", "timeline"},
      {"timeline-forward", "Timeline · Shuttle Forward", "L", "timeline"},
      {"timeline-next-edit", "Timeline · Next Edit", "Alt+Right", "timeline"},
      {"timeline-previous-edit", "Timeline · Previous Edit", "Alt+Left", "timeline"},
      {"timeline-next-marker", "Timeline · Next Marker", "Alt+Down", "timeline"},
      {"timeline-previous-marker", "Timeline · Previous Marker", "Alt+Up", "timeline"},
      {"timeline-source-in", "Timeline · Go To Source In", "Shift+I", "timeline"},
      {"timeline-source-out", "Timeline · Go To Source Out", "Shift+O", "timeline"},
      {"timeline-match-frame", "Timeline · Match Frame", "F", "timeline"},
      {"timeline-frame-back", "Timeline · Previous Frame", "Left", "timeline"},
      {"timeline-frame-forward", "Timeline · Next Frame", "Right", "timeline"},
      {"timeline-start", "Timeline · Go To Start", "Home", "timeline"},
      {"timeline-end", "Timeline · Go To End", "End", "timeline"},
      {"timeline-add-video", "Timeline · Add Video Track", "Ctrl+Shift+T", "timeline"},
      {"timeline-zoom-in", "Timeline · Zoom In", "Ctrl+=", "timeline"},
      {"timeline-zoom-out", "Timeline · Zoom Out", "Ctrl+-", "timeline"},
      {"timeline-zoom-fit", "Timeline · Zoom To Fit", "Ctrl+0", "timeline"},
  };
  return definitions;
}

inline QString canonical_editor_shortcut(QString sequence) {
  sequence = sequence.trimmed();
  sequence.replace(QRegularExpression(QStringLiteral("(^|\\+)Cmd(?=\\+)"),
                                     QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("\\1Ctrl"));
  const auto parsed = QKeySequence::fromString(sequence, QKeySequence::PortableText);
  if (parsed.isEmpty()) return {};
  for (int index = 0; index < parsed.count(); ++index) {
    const auto key = parsed[static_cast<uint>(index)].key();
    if (key == Qt::Key_unknown || key == 0) return {};
  }
  return parsed.toString(QKeySequence::PortableText);
}

inline bool editor_shortcut_contexts_overlap(const QString& left, const QString& right) {
  return left == right || left == QStringLiteral("global") || right == QStringLiteral("global") ||
      (left == QStringLiteral("non-photo") && right != QStringLiteral("photo")) ||
      (right == QStringLiteral("non-photo") && left != QStringLiteral("photo"));
}

// These actions still use fixed bindings in Main/TimelineView. Remapping a tool
// onto one of them creates an ambiguous Qt shortcut, even though the other
// action is not an editable row in the keymap dialog.
inline const QVector<EditorShortcut>& reserved_editor_shortcuts() {
  static const QVector<EditorShortcut> values{
      {"fixed-new", "New document", "Ctrl+N", "global"},
      {"fixed-new-import", "New import", "Ctrl+Shift+I", "non-photo"},
      {"fixed-selection-invert", "Invert selection", "Ctrl+Shift+I", "photo"},
      {"fixed-selection-invert-meta", "Invert selection", "Meta+Shift+I", "photo"},
      {"fixed-selection-intersect", "Intersect selection", "Ctrl+Alt+Shift+I", "photo"},
      {"fixed-selection-deselect", "Deselect selection", "Ctrl+D", "photo"},
      {"fixed-selection-reselect", "Reselect", "Ctrl+Shift+D", "photo"},
      {"fixed-selection-quick-mask", "Quick mask preview", "Q", "photo"},
      {"fixed-selection-deselect-meta", "Deselect selection", "Meta+D", "photo"},
      {"fixed-open", "Import folder", "Ctrl+O", "global"},
      {"fixed-save", "Save", "Ctrl+S", "global"},
      {"fixed-open-edit", "Open selected photo in Edit", "Ctrl+E", "global"},
      {"fixed-rulers", "Show rulers", "Ctrl+Shift+R", "global"},
      {"fixed-guides", "Show guides", "Ctrl+;", "global"},
      {"fixed-refresh", "Refresh library", "Ctrl+R", "global"},
      {"fixed-preferences", "Preferences", "Ctrl+,", "global"},
      {"fixed-undo", "Undo", "Ctrl+Z", "global"},
      {"fixed-redo", "Redo", "Ctrl+Shift+Z", "global"},
      {"fixed-select-all", "Select all", "Ctrl+A", "global"},
      {"fixed-deselect", "Deselect / add adjustment", "Ctrl+Shift+A", "global"},
      {"fixed-quit", "Quit", "Ctrl+Q", "global"},
      {"fixed-workspace-boards", "Go to Boards", "Ctrl+3", "global"},
      {"fixed-workspace-timeline", "Go to Timeline", "Ctrl+4", "global"},
      {"fixed-workspace-export", "Go to Export", "Ctrl+5", "global"},
      {"fixed-copy", "Copy", "Ctrl+C", "global"},
      {"fixed-cut", "Cut", "Ctrl+X", "global"},
      {"fixed-paste", "Paste", "Ctrl+V", "global"},
      {"fixed-timeline-delete", "Delete clip", "Delete", "timeline"},
      {"fixed-timeline-backspace", "Delete clip", "Backspace", "timeline"},
      {"fixed-timeline-cancel", "Cancel trim", "Escape", "timeline"},
      {"fixed-timeline-paste-insert", "Paste with insert", "Ctrl+Shift+V", "timeline"},
      {"fixed-timeline-paste-insert-meta", "Paste with insert", "Meta+Shift+V", "timeline"},
      {"fixed-timeline-duplicate", "Duplicate clip", "Ctrl+D", "timeline"},
      {"fixed-timeline-duplicate-meta", "Duplicate clip", "Meta+D", "timeline"},
      {"fixed-timeline-replace", "Replace from source", "Ctrl+Alt+R", "timeline"},
      {"fixed-timeline-replace-meta", "Replace from source", "Meta+Alt+R", "timeline"},
      {"fixed-timeline-unlinked-delete", "Delete without linked edit", "Alt+Backspace", "timeline"},
      {"fixed-timeline-unlinked-duplicate", "Duplicate without linked edit", "Ctrl+Alt+D", "timeline"},
      {"fixed-timeline-search", "Search timeline", "Ctrl+F", "timeline"},
      {"fixed-timeline-timecode", "Go to timecode", "Ctrl+G", "timeline"},
      {"fixed-timeline-timecode-meta", "Go to timecode", "Meta+G", "timeline"},
      {"fixed-timeline-media", "Open media browser", "Ctrl+Shift+M", "timeline"},
      {"fixed-timeline-captions", "Generate captions", "Ctrl+Shift+C", "timeline"},
      {"fixed-timeline-trim-mode", "Toggle ripple trim", "Shift+T", "timeline"},
      {"fixed-timeline-trim-back", "Trim previous frame", ",", "timeline"},
      {"fixed-timeline-trim-forward", "Trim next frame", ".", "timeline"},
      {"fixed-timeline-trim-back-five", "Trim previous five frames", "Shift+,", "timeline"},
      {"fixed-timeline-trim-forward-five", "Trim next five frames", "Shift+.", "timeline"},
      {"fixed-timeline-trim-back-ripple", "Ripple trim previous frame", "Alt+,", "timeline"},
      {"fixed-timeline-trim-forward-ripple", "Ripple trim next frame", "Alt+.", "timeline"},
      {"fixed-timeline-focus-program", "Focus Program", "Shift+1", "timeline"},
      {"fixed-timeline-focus-timeline", "Focus timeline", "Shift+2", "timeline"},
  };
  return values;
}

inline QVector<EditorShortcut> effective_editor_shortcuts(const QVariantMap& overrides) {
  auto values = editor_shortcuts();
  for (auto& item : values) item.sequence = overrides.value(item.id, item.sequence).toString();
  for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
    const auto known = std::any_of(values.cbegin(), values.cend(), [&it](const auto& item) {
      return item.id == it.key();
    });
    if (!known) values.push_back({it.key(), it.key(), it.value().toString(), QStringLiteral("global")});
  }
  return values;
}

inline bool editor_shortcut_sequences_conflict(const QString& left, const QString& right) {
  const QKeySequence a(left, QKeySequence::PortableText);
  const QKeySequence b(right, QKeySequence::PortableText);
  return a.matches(b) != QKeySequence::NoMatch || b.matches(a) != QKeySequence::NoMatch;
}

inline QStringList editor_shortcut_conflict_labels(
    const QString& id, const QString& sequence, const QVariantMap& overrides) {
  const auto canonical = canonical_editor_shortcut(sequence);
  if (canonical.isEmpty()) return {};
  auto values = effective_editor_shortcuts(overrides);
  QString context = QStringLiteral("global");
  for (const auto& item : values) if (item.id == id) context = item.context;
  values += reserved_editor_shortcuts();
  QStringList labels;
  for (const auto& item : values) {
    if (item.id == id || !editor_shortcut_contexts_overlap(context, item.context)) continue;
    if (editor_shortcut_sequences_conflict(canonical, item.sequence) && !labels.contains(item.label))
      labels.push_back(item.label);
  }
  return labels;
}

inline bool validate_editor_shortcuts(const QVariantMap& overrides, QVariantMap* normalized) {
  QVariantMap result;
  for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
    const auto id = it.key().trimmed();
    const auto sequence = canonical_editor_shortcut(it.value().toString());
    if (id.isEmpty() || sequence.isEmpty() || result.contains(id)) return false;
    result.insert(id, sequence);
  }
  auto effective = effective_editor_shortcuts(result);
  const auto editable_count = effective.size();
  effective += reserved_editor_shortcuts();
  for (qsizetype i = 0; i < editable_count; ++i) {
    for (qsizetype j = i + 1; j < effective.size(); ++j) {
      if (!editor_shortcut_contexts_overlap(effective[i].context, effective[j].context)) continue;
      if (editor_shortcut_sequences_conflict(effective[i].sequence, effective[j].sequence)) return false;
    }
  }
  if (normalized != nullptr) *normalized = result;
  return true;
}

}  // namespace photo_editor::desktop
