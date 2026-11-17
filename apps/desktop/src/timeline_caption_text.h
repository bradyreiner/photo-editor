#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QTextBoundaryFinder>

namespace photo_editor::desktop {

// Caption limits count visible characters, not UTF-16 storage units. Keep emoji
// sequences, surrogate pairs and combining accents together.
inline int caption_grapheme_count(const QString& text) {
  QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
  int count = 0;
  while (finder.toNextBoundary() != -1) ++count;
  return count;
}

inline QString normalized_caption_line_breaks(QString text) {
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace(QChar('\r'), QChar('\n'));
  text.replace(QChar(0x2028), QChar('\n'));
  text.replace(QChar(0x2029), QChar('\n'));
  return text;
}

inline QString caption_text_for_visible_words(
    const QString& authored_text,
    const QStringList& authored_words,
    const QVector<bool>& visible_words) {
  if (authored_words.size() != visible_words.size()) {
    return authored_words.join(QLatin1Char(' '));
  }
  auto visible_count = 0;
  for (const auto visible : visible_words) {
    visible_count += visible ? 1 : 0;
  }
  if (visible_count == authored_words.size()) {
    return authored_text;
  }
  if (visible_count == 0) {
    return {};
  }

  const auto source_lines = normalized_caption_line_breaks(authored_text).split(QLatin1Char('\n'));
  QStringList source_words;
  QVector<int> source_line_indices;
  for (int line_index = 0; line_index < source_lines.size(); ++line_index) {
    const auto line_words = source_lines.at(line_index).simplified().split(
        QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const auto& word : line_words) {
      source_words.push_back(word);
      source_line_indices.push_back(line_index);
    }
  }
  if (source_words != authored_words) {
    QStringList visible_text;
    for (int word_index = 0; word_index < authored_words.size(); ++word_index) {
      if (visible_words.at(word_index)) {
        visible_text.push_back(authored_words.at(word_index));
      }
    }
    return visible_text.join(QLatin1Char(' '));
  }

  QStringList visible_lines;
  QStringList current_line;
  auto previous_source_line = -1;
  for (int word_index = 0; word_index < authored_words.size(); ++word_index) {
    if (!visible_words.at(word_index)) {
      continue;
    }
    const auto source_line = source_line_indices.at(word_index);
    if (!current_line.isEmpty() && source_line != previous_source_line) {
      visible_lines.push_back(current_line.join(QLatin1Char(' ')));
      current_line.clear();
    }
    current_line.push_back(authored_words.at(word_index));
    previous_source_line = source_line;
  }
  if (!current_line.isEmpty()) {
    visible_lines.push_back(current_line.join(QLatin1Char(' ')));
  }
  return visible_lines.join(QLatin1Char('\n'));
}

}  // namespace photo_editor::desktop
