#include "editor_shortcut_registry.h"
#include "timeline_caption_text.h"

#include <QCoreApplication>
#include <iostream>

using namespace photo_editor::desktop;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto require = [](bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
  };
  QVariantMap normalized;
  if (!require(validate_editor_shortcuts({}, &normalized), "Default context-specific keymap must be conflict-free") ||
      !require(!validate_editor_shortcuts({{"toggle-full-edit", "Ctrl+Shift+I"}}, &normalized), "Photo shortcuts must reserve selection inversion") ||
      !require(!validate_editor_shortcuts({{"timeline-razor", "Ctrl+Shift+I"}}, &normalized), "Timeline shortcuts must reserve New Import") ||
      !require(canonical_editor_shortcut(QStringLiteral("Cmd+Shift+E")) == QStringLiteral("Ctrl+Shift+E"), "Legacy Cmd alias must normalize") ||
      !require(canonical_editor_shortcut(QStringLiteral("Ctrl+DefinitelyNotAKey")).isEmpty(), "Unknown keys must be rejected") ||
      !require(!validate_editor_shortcuts({{"timeline-play", "V"}}, &normalized), "Overrides must be checked against unmodified defaults") ||
      !require(!validate_editor_shortcuts({{"timeline-play", "Cmd+K"}}, &normalized), "Global defaults must conflict with Timeline overrides") ||
      !require(!validate_editor_shortcuts({{"timeline-play", "Ctrl+K, Ctrl+C"}}, &normalized), "Chord prefixes must not shadow global shortcuts") ||
      !require(!validate_editor_shortcuts({{"timeline-razor", "Ctrl+S"}}, &normalized), "Remapping must not shadow the fixed Save action") ||
      !require(!validate_editor_shortcuts({{"timeline-razor", "Ctrl+Q, C"}}, &normalized), "A chord must not shadow the fixed Quit action") ||
      !require(!validate_editor_shortcuts({{"timeline-razor", "Delete"}}, &normalized), "A tool must not shadow Timeline clip deletion") ||
      !require(!validate_editor_shortcuts({{"timeline-razor", "Ctrl+C"}}, &normalized), "A tool must not shadow clip copying") ||
      !require(!validate_editor_shortcuts({{"timeline-razor", "Meta+D"}}, &normalized), "Explicit Meta aliases must not shadow clip duplication") ||
      !require(validate_editor_shortcuts({{"toggle-full-edit", "Shift+T"}}, &normalized), "Timeline-only fixed trim binding must not block a photo-only key") ||
      !require(editor_shortcut_conflict_labels("timeline-razor", "Cmd+S", {}).contains("Save"), "Conflict feedback must identify fixed Save with normalized platform spelling") ||
      !require(editor_shortcut_conflict_labels("timeline-razor", "Ctrl+Q, C", {}).contains("Quit"), "Conflict feedback must identify chord-prefix conflicts") ||
      !require(!validate_editor_shortcuts({{"timeline-play", "Ctrl+Shift+7"}, {"timeline-stop", "cmd+shift+7"}}, &normalized), "Equivalent spellings must conflict") ||
      !require(validate_editor_shortcuts({{"timeline-play", "Ctrl+Shift+7"}, {"toggle-full-edit", "Ctrl+Shift+7"}}, &normalized), "Disjoint photo and Timeline scopes may share a key") ||
      !require(!validate_editor_shortcuts({{"timeline-play", ""}}, &normalized), "Empty shortcuts must not silently erase an action")) {
    return 1;
  }
  if (!require(caption_grapheme_count(QString::fromUtf8("e\xcc\x81")) == 1, "Combining accents count as one visible character") ||
      !require(caption_grapheme_count(QString::fromUtf8("👩‍👩‍👧‍👦")) == 1, "Joined emoji count as one visible character") ||
      !require(caption_grapheme_count(QString::fromUtf8("字幕")) == 2, "CJK caption characters must not be byte-counted") ||
      !require(caption_grapheme_count(QString{}) == 0, "Empty caption has zero characters") ||
      !require(normalized_caption_line_breaks(QString::fromUtf8("a\r\nb\rc\xe2\x80\xa8")) == QStringLiteral("a\nb\nc\n"), "All supported line separators normalize without double-counting CRLF")) {
    return 1;
  }
  return 0;
}
