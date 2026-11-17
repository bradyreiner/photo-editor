#pragma once

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

namespace photo_editor::desktop::timeline_motion::bounded_h264 {

struct SharedPngInputs {
  QStringList arguments;
  QString graph;
  qsizetype removed_inputs{0};
};

// Share only identical literal PNG demuxers in one immutable render window.
// Existing per-layer filters (including finite title loops and local clocks)
// remain on independent split branches. Video, sequences, seeks, and unfamiliar
// input options deliberately retain their original decoder and input index.
inline SharedPngInputs share_identical_png_inputs(
    const QStringList& arguments, const QString& graph) {
  const SharedPngInputs original{arguments, graph, 0};
  struct Input {
    qsizetype begin;
    qsizetype end;
    qsizetype representative;
    qsizetype new_index{-1};
  };
  QVector<Input> inputs;
  QHash<QString, qsizetype> representatives;
  qsizetype begin = 0;
  for (qsizetype index = 0; index < arguments.size(); ++index) {
    if (arguments.at(index) != QStringLiteral("-i")) continue;
    if (index + 1 >= arguments.size()) return original;
    const auto old_index = inputs.size();
    auto representative = old_index;
    const auto& path = arguments.at(index + 1);
    auto eligible = path.startsWith(QLatin1Char('/')) &&
                    path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) &&
                    !path.contains(QLatin1Char('%'));
    auto has_frame_rate = false;
    QHash<QString, bool> options;
    for (auto option = begin; eligible && option < index; option += 2) {
      if (option + 1 >= index || options.contains(arguments.at(option))) {
        eligible = false;
        break;
      }
      const auto& name = arguments.at(option);
      const auto& value = arguments.at(option + 1);
      options.insert(name, true);
      bool valid_number = false;
      if (name == QStringLiteral("-framerate") || name == QStringLiteral("-t")) {
        const auto number = value.toDouble(&valid_number);
        eligible = valid_number && number > 0.0 && number <= 1000000.0;
        has_frame_rate = has_frame_rate || name == QStringLiteral("-framerate");
      } else if (name == QStringLiteral("-loop")) {
        eligible = value == QStringLiteral("1");
      } else if (name == QStringLiteral("-f")) {
        eligible = value == QStringLiteral("image2");
      } else {
        eligible = false;
      }
    }
    if (eligible && has_frame_rate) {
      const auto key = arguments.mid(begin, index + 2 - begin).join(QChar(0));
      const auto found = representatives.constFind(key);
      if (found != representatives.cend()) representative = found.value();
      else representatives.insert(key, old_index);
    }
    inputs.push_back({begin, index + 2, representative, -1});
    begin = index + 2;
    ++index;
  }
  if (inputs.isEmpty()) return original;
  // Numeric command-line maps would need separate remapping. This renderer maps
  // named graph outputs; leave any different calling convention untouched.
  for (auto index = begin; index + 1 < arguments.size(); ++index) {
    if (arguments.at(index) == QStringLiteral("-map") &&
        !arguments.at(index + 1).startsWith(QLatin1Char('['))) return original;
  }
  struct Reference { qsizetype begin; qsizetype length; qsizetype input; QString stream; };
  QVector<Reference> references;
  QVector<qsizetype> uses(inputs.size(), 0);
  static const QRegularExpression numeric_reference(QStringLiteral("^(\\d+):([A-Za-z0-9:]+)$"));
  QChar quote;
  for (qsizetype index = 0; index < graph.size(); ++index) {
    const auto character = graph.at(index);
    if (character == QLatin1Char('\\')) { ++index; continue; }
    if (!quote.isNull()) {
      if (character == quote) quote = QChar{};
      continue;
    }
    if (character == QLatin1Char('\'') || character == QLatin1Char('"')) {
      quote = character;
      continue;
    }
    if (character != QLatin1Char('[')) continue;
    const auto close = graph.indexOf(QLatin1Char(']'), index + 1);
    if (close < 0) return original;
    const auto label = graph.mid(index + 1, close - index - 1);
    const auto match = numeric_reference.match(label);
    if (match.hasMatch()) {
      bool valid = false;
      const auto input = match.captured(1).toLongLong(&valid);
      if (!valid || input < 0 || input >= inputs.size()) return original;
      references.push_back({index, close - index + 1, static_cast<qsizetype>(input), match.captured(2)});
      ++uses[static_cast<qsizetype>(input)];
    } else if (!label.isEmpty() && label.at(0).isDigit()) {
      return original;
    }
    index = close;
  }
  if (!quote.isNull()) return original;
  // PNG inputs can only be shared when all their references are video streams.
  // Requiring one use per old input preserves the original graph's ownership.
  for (qsizetype index = 0; index < inputs.size(); ++index) {
    const auto representative = inputs.at(index).representative;
    if (representative == index) continue;
    if (uses.at(index) != 1 || uses.at(representative) != 1) return original;
  }
  for (const auto& reference : references) {
    const auto input = reference.input;
    if (inputs.at(input).representative != input && reference.stream != QStringLiteral("v")) return original;
  }
  SharedPngInputs result;
  QVector<qsizetype> group_sizes(inputs.size(), 0);
  for (auto& input : inputs) ++group_sizes[input.representative];
  qsizetype new_index = 0;
  for (qsizetype index = 0; index < inputs.size(); ++index) {
    auto& input = inputs[index];
    if (input.representative != index) { ++result.removed_inputs; continue; }
    input.new_index = new_index++;
    result.arguments.append(arguments.mid(input.begin, input.end - input.begin));
  }
  if (result.removed_inputs == 0) return original;
  result.arguments.append(arguments.mid(begin));
  QVector<qsizetype> group_branch(inputs.size(), 0);
  QString prefix;
  for (qsizetype index = 0; index < inputs.size(); ++index) {
    if (group_sizes.at(index) <= 1) continue;
    const auto tag = QStringLiteral("looksy_png_shared_%1_").arg(index);
    if (graph.contains(tag)) return original;
    prefix += QStringLiteral("[%1:v]split=%2").arg(inputs.at(index).new_index).arg(group_sizes.at(index));
    for (qsizetype branch = 0; branch < group_sizes.at(index); ++branch)
      prefix += QStringLiteral("[%1%2]").arg(tag).arg(branch);
    prefix += QLatin1Char(';');
  }
  QString rewritten;
  qsizetype cursor = 0;
  for (const auto& reference : references) {
    rewritten += graph.mid(cursor, reference.begin - cursor);
    const auto representative = inputs.at(reference.input).representative;
    if (group_sizes.at(representative) > 1) {
      if (reference.stream != QStringLiteral("v")) return original;
      rewritten += QStringLiteral("[looksy_png_shared_%1_%2]")
                       .arg(representative).arg(group_branch[representative]++);
    } else {
      rewritten += QStringLiteral("[%1:%2]").arg(inputs.at(representative).new_index).arg(reference.stream);
    }
    cursor = reference.begin + reference.length;
  }
  rewritten += graph.mid(cursor);
  result.graph = prefix + rewritten;
  return result;
}

}  // namespace photo_editor::desktop::timeline_motion::bounded_h264
