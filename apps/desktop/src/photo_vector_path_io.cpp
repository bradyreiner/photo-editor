#include "photo_vector_path_io.h"

#include <QColor>
#include <QPainterPath>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>
#include <utility>

namespace photo_editor::desktop::vector_path_io {
namespace {

using vector_paths::AnchorKind;
using vector_paths::CoordinateSpace;
using vector_paths::PathAnchor;
using vector_paths::VectorPathDocument;
using vector_paths::VectorSubpath;

constexpr qsizetype kMaxSvgBytes = 4 * 1024 * 1024;
constexpr qsizetype kMaxPathDataCharacters = 1 * 1024 * 1024;
constexpr qsizetype kMaxPaths = 256;
constexpr qsizetype kMaxAnchors = 8192;
constexpr qsizetype kMaxNonzeroComparisonAnchors = 512;
constexpr qsizetype kMaxNonzeroComparisonTotalAnchors = 2048;
constexpr int kMaxXmlDepth = 16;
constexpr qsizetype kMaxTransformCharacters = 4096;

void set_error(QString* error, const QString& message) {
  if (error != nullptr) *error = message;
}

bool fail(QString* error, const QString& message) {
  set_error(error, message);
  return false;
}

bool finite(double value) {
  return std::isfinite(value);
}

struct NumberScanner {
  explicit NumberScanner(QString text) : source(std::move(text)) {}

  QString source;
  qsizetype position = 0;

  void skip_separators() {
    while (position < source.size()) {
      const auto character = source.at(position);
      if (character.isSpace() || character == QLatin1Char(',')) {
        ++position;
      } else {
        break;
      }
    }
  }

  [[nodiscard]] bool at_end() {
    skip_separators();
    return position >= source.size();
  }

  [[nodiscard]] QChar peek() {
    skip_separators();
    return position < source.size() ? source.at(position) : QChar();
  }

  bool read_number(double* value) {
    if (value == nullptr) return false;
    skip_separators();
    const auto start = position;
    if (position < source.size() &&
        (source.at(position) == QLatin1Char('+') ||
         source.at(position) == QLatin1Char('-'))) {
      ++position;
    }
    bool digits = false;
    while (position < source.size() && source.at(position).isDigit()) {
      digits = true;
      ++position;
    }
    if (position < source.size() && source.at(position) == QLatin1Char('.')) {
      ++position;
      while (position < source.size() && source.at(position).isDigit()) {
        digits = true;
        ++position;
      }
    }
    if (!digits) {
      position = start;
      return false;
    }
    if (position < source.size() &&
        (source.at(position) == QLatin1Char('e') ||
         source.at(position) == QLatin1Char('E'))) {
      const auto exponent_start = position++;
      if (position < source.size() &&
          (source.at(position) == QLatin1Char('+') ||
           source.at(position) == QLatin1Char('-'))) {
        ++position;
      }
      const auto exponent_digits_start = position;
      while (position < source.size() && source.at(position).isDigit()) ++position;
      if (position == exponent_digits_start) {
        position = exponent_start;
        return false;
      }
    }
    bool ok = false;
    const auto parsed = source.mid(start, position - start).toDouble(&ok);
    if (!ok || !finite(parsed) || std::abs(parsed) > 1.0e9) return false;
    *value = parsed;
    return true;
  }
};

struct Affine {
  double a = 1.0;
  double b = 0.0;
  double c = 0.0;
  double d = 1.0;
  double e = 0.0;
  double f = 0.0;

  [[nodiscard]] QPointF map(const QPointF& point) const {
    return QPointF(a * point.x() + c * point.y() + e,
                   b * point.x() + d * point.y() + f);
  }

  [[nodiscard]] Affine then(const Affine& rhs) const {
    // SVG transform-list and nested-group composition uses column-vector
    // matrices. A child transform is multiplied on the right of its parent.
    return Affine{
        a * rhs.a + c * rhs.b,
        b * rhs.a + d * rhs.b,
        a * rhs.c + c * rhs.d,
        b * rhs.c + d * rhs.d,
        a * rhs.e + c * rhs.f + e,
        b * rhs.e + d * rhs.f + f};
  }

  [[nodiscard]] bool is_finite() const {
    return finite(a) && finite(b) && finite(c) && finite(d) && finite(e) && finite(f);
  }

  [[nodiscard]] bool has_uniform_scale(double* scale = nullptr) const {
    const double first = std::hypot(a, b);
    const double second = std::hypot(c, d);
    const double dot = a * c + b * d;
    const double tolerance = std::max({1.0, first, second}) * 1.0e-10;
    if (!finite(first) || !finite(second) || std::abs(first - second) > tolerance ||
        std::abs(dot) > tolerance * std::max(1.0, first)) {
      return false;
    }
    if (scale != nullptr) *scale = first;
    return true;
  }
};

Affine multiply(const Affine& lhs, const Affine& rhs) {
  return lhs.then(rhs);
}

bool parse_number_list(const QString& text, QVector<double>* values,
                       QString* error) {
  if (values == nullptr) return fail(error, QStringLiteral("A numeric list output is required."));
  NumberScanner scanner(text);
  QVector<double> parsed;
  while (!scanner.at_end()) {
    double value = 0.0;
    if (!scanner.read_number(&value))
      return fail(error, QStringLiteral("SVG numeric values must be finite numbers."));
    parsed.append(value);
  }
  *values = std::move(parsed);
  return true;
}

bool parse_transform(const QString& text, Affine* result, QString* error) {
  if (result == nullptr) return fail(error, QStringLiteral("A transform output is required."));
  if (text.size() > kMaxTransformCharacters)
    return fail(error, QStringLiteral("SVG transform data exceeds the supported limit."));
  Affine combined;
  qsizetype position = 0;
  while (position < text.size()) {
    while (position < text.size() &&
           (text.at(position).isSpace() || text.at(position) == QLatin1Char(','))) {
      ++position;
    }
    if (position == text.size()) break;
    const auto name_start = position;
    while (position < text.size() && text.at(position).isLetter()) ++position;
    if (position == name_start)
      return fail(error, QStringLiteral("SVG transform syntax is invalid."));
    const auto name = text.mid(name_start, position - name_start);
    while (position < text.size() && text.at(position).isSpace()) ++position;
    if (position >= text.size() || text.at(position++) != QLatin1Char('('))
      return fail(error, QStringLiteral("SVG transform syntax is invalid."));
    const auto arguments_start = position;
    while (position < text.size() && text.at(position) != QLatin1Char(')')) ++position;
    if (position >= text.size())
      return fail(error, QStringLiteral("SVG transform is missing a closing parenthesis."));
    const auto arguments = text.mid(arguments_start, position - arguments_start);
    ++position;
    QVector<double> values;
    if (!parse_number_list(arguments, &values, error)) return false;

    Affine next;
    if (name == QStringLiteral("matrix") && values.size() == 6) {
      next = {values.at(0), values.at(1), values.at(2), values.at(3),
              values.at(4), values.at(5)};
    } else if (name == QStringLiteral("translate") &&
               (values.size() == 1 || values.size() == 2)) {
      next.e = values.at(0);
      next.f = values.size() == 2 ? values.at(1) : 0.0;
    } else if (name == QStringLiteral("scale") &&
               (values.size() == 1 || values.size() == 2)) {
      next.a = values.at(0);
      next.d = values.size() == 2 ? values.at(1) : values.at(0);
    } else if (name == QStringLiteral("rotate") &&
               (values.size() == 1 || values.size() == 3)) {
      const double radians = values.at(0) * 3.14159265358979323846 / 180.0;
      const double cosine = std::cos(radians);
      const double sine = std::sin(radians);
      next.a = cosine;
      next.b = sine;
      next.c = -sine;
      next.d = cosine;
      if (values.size() == 3) {
        const Affine to_center{1.0, 0.0, 0.0, 1.0, values.at(1), values.at(2)};
        const Affine from_center{1.0, 0.0, 0.0, 1.0, -values.at(1), -values.at(2)};
        next = multiply(multiply(to_center, next), from_center);
      }
    } else if (name == QStringLiteral("skewX") && values.size() == 1) {
      next.c = std::tan(values.at(0) * 3.14159265358979323846 / 180.0);
    } else if (name == QStringLiteral("skewY") && values.size() == 1) {
      next.b = std::tan(values.at(0) * 3.14159265358979323846 / 180.0);
    } else {
      return fail(error, QStringLiteral("Unsupported SVG transform function or argument count: %1.").arg(name));
    }
    if (!next.is_finite())
      return fail(error, QStringLiteral("SVG transform values exceed the supported numeric range."));
    combined = multiply(combined, next);
    if (!combined.is_finite())
      return fail(error, QStringLiteral("Composed SVG transforms exceed the supported numeric range."));
  }
  *result = combined;
  return true;
}

struct Style {
  QString fill = QStringLiteral("#000000");
  QString stroke;
  double stroke_width = 1.0;
  double fill_opacity = 1.0;
  double stroke_opacity = 1.0;
  QString fill_rule = QStringLiteral("nonzero");
};

bool css_color(const QString& input, QString* output, QString* error) {
  if (output == nullptr) return fail(error, QStringLiteral("A color output is required."));
  const auto value = input.trimmed();
  if (value == QStringLiteral("none")) {
    output->clear();
    return true;
  }
  if (value.startsWith(QLatin1Char('#'))) {
    const auto hex = value.mid(1);
    auto hex_digit = [](QChar character) { return character.isDigit() || (character.toLower() >= QLatin1Char('a') && character.toLower() <= QLatin1Char('f')); };
    if (!std::all_of(hex.cbegin(), hex.cend(), hex_digit) ||
        (hex.size() != 3 && hex.size() != 4 && hex.size() != 6 && hex.size() != 8)) {
      return fail(error, QStringLiteral("SVG colors must use #RGB, #RGBA, #RRGGBB, #RRGGBBAA, a supported color name, or none."));
    }
    QString expanded;
    if (hex.size() == 3 || hex.size() == 4) {
      for (const auto character : hex) expanded.append(character).append(character);
    } else {
      expanded = hex;
    }
    const auto red = expanded.mid(0, 2).toUInt(nullptr, 16);
    const auto green = expanded.mid(2, 2).toUInt(nullptr, 16);
    const auto blue = expanded.mid(4, 2).toUInt(nullptr, 16);
    const auto alpha = expanded.size() == 8 ? expanded.mid(6, 2).toUInt(nullptr, 16) : 255U;
    const QColor color(static_cast<int>(red), static_cast<int>(green),
                       static_cast<int>(blue), static_cast<int>(alpha));
    *output = color.alpha() == 255 ? color.name(QColor::HexRgb)
                                   : color.name(QColor::HexArgb);
    return true;
  }
  if (value.contains(QLatin1Char('(')) || value.contains(QLatin1Char(')')))
    return fail(error, QStringLiteral("Functional SVG colors such as rgb() are outside the supported subset."));
  const auto color = QColor(value);
  if (!color.isValid())
    return fail(error, QStringLiteral("Unsupported SVG color: %1.").arg(value));
  *output = color.alpha() == 255 ? color.name(QColor::HexRgb)
                                 : color.name(QColor::HexArgb);
  return true;
}

bool parse_opacity(const QString& text, double* opacity, QString* error) {
  if (opacity == nullptr) return fail(error, QStringLiteral("An opacity output is required."));
  NumberScanner scanner(text.trimmed());
  double value = 0.0;
  if (!scanner.read_number(&value) || !scanner.at_end() || value < 0.0 || value > 1.0)
    return fail(error, QStringLiteral("SVG fill-opacity and stroke-opacity must be between 0 and 1."));
  *opacity = value;
  return true;
}

bool parse_stroke_width(const QString& text, double* width, QString* error) {
  if (width == nullptr) return fail(error, QStringLiteral("A stroke-width output is required."));
  auto value_text = text.trimmed();
  if (value_text.endsWith(QStringLiteral("px"))) value_text.chop(2);
  NumberScanner scanner(value_text.trimmed());
  double value = 0.0;
  if (!scanner.read_number(&value) || !scanner.at_end() || value < 0.0 || value > 32768.0)
    return fail(error, QStringLiteral("SVG stroke-width must be a unitless or px value between 0 and 32768."));
  *width = value;
  return true;
}

bool set_style_property(Style* style, const QString& name, const QString& value,
                        QString* error) {
  if (style == nullptr) return fail(error, QStringLiteral("A style output is required."));
  const auto property = name.trimmed();
  const auto text = value.trimmed();
  if (property == QStringLiteral("fill")) {
    return css_color(text, &style->fill, error);
  }
  if (property == QStringLiteral("stroke")) {
    return css_color(text, &style->stroke, error);
  }
  if (property == QStringLiteral("stroke-width"))
    return parse_stroke_width(text, &style->stroke_width, error);
  if (property == QStringLiteral("fill-opacity"))
    return parse_opacity(text, &style->fill_opacity, error);
  if (property == QStringLiteral("stroke-opacity"))
    return parse_opacity(text, &style->stroke_opacity, error);
  if (property == QStringLiteral("fill-rule")) {
    if (text != QStringLiteral("evenodd") && text != QStringLiteral("nonzero"))
      return fail(error, QStringLiteral("SVG fill-rule must be evenodd or nonzero."));
    style->fill_rule = text;
    return true;
  }
  return fail(error, QStringLiteral("Unsupported SVG style property: %1.").arg(property));
}

bool is_style_property(const QString& name) {
  return name == QStringLiteral("fill") || name == QStringLiteral("stroke") ||
         name == QStringLiteral("stroke-width") ||
         name == QStringLiteral("fill-opacity") ||
         name == QStringLiteral("stroke-opacity") ||
         name == QStringLiteral("fill-rule");
}

bool apply_style_attributes(const QXmlStreamAttributes& attributes,
                            const Style& inherited, Style* style,
                            QString* error) {
  if (style == nullptr) return fail(error, QStringLiteral("A style output is required."));
  Style parsed = inherited;
  QVector<QPair<QString, QString>> presentation;
  QString inline_style;
  for (const auto& attribute : attributes) {
    const auto name = attribute.name().toString();
    const auto value = attribute.value().toString();
    if (name == QStringLiteral("id") || name == QStringLiteral("transform") ||
        name == QStringLiteral("d")) {
      continue;
    }
    if (name == QStringLiteral("style")) {
      inline_style = value;
      continue;
    }
    if (is_style_property(name)) presentation.append({name, value});
  }
  for (const auto& item : presentation)
    if (!set_style_property(&parsed, item.first, item.second, error)) return false;
  if (!inline_style.isEmpty()) {
    if (inline_style.size() > 8192)
      return fail(error, QStringLiteral("Inline SVG style exceeds the supported limit."));
    const auto declarations = inline_style.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const auto& declaration : declarations) {
      const auto separator = declaration.indexOf(QLatin1Char(':'));
      if (separator <= 0)
        return fail(error, QStringLiteral("Inline SVG style must contain property:value declarations."));
      const auto property = declaration.left(separator).trimmed();
      const auto value = declaration.mid(separator + 1).trimmed();
      if (!set_style_property(&parsed, property, value, error)) return false;
    }
  }
  *style = std::move(parsed);
  return true;
}

bool parse_transform_attribute(const QXmlStreamAttributes& attributes,
                               const Affine& inherited, Affine* transform,
                               QString* error) {
  if (transform == nullptr) return fail(error, QStringLiteral("A transform output is required."));
  QString local_text;
  for (const auto& attribute : attributes) {
    if (attribute.name() == QStringLiteral("transform")) {
      local_text = attribute.value().toString();
      break;
    }
  }
  Affine local;
  if (!parse_transform(local_text, &local, error)) return false;
  *transform = multiply(inherited, local);
  if (!transform->is_finite())
    return fail(error, QStringLiteral("Composed SVG transforms exceed the supported numeric range."));
  return true;
}

bool path_attribute(const QXmlStreamAttributes& attributes, const QString& wanted,
                    QString* value) {
  for (const auto& attribute : attributes) {
    if (attribute.name() == wanted) {
      if (value != nullptr) *value = attribute.value().toString();
      return true;
    }
  }
  return false;
}

struct PathParser {
  QString data;
  qsizetype position = 0;
  qsizetype path_index = 0;
  qsizetype& next_anchor_id;
  const QSizeF view_box;
  const QPointF view_box_origin;
  const Affine transform;
  qsizetype& total_anchor_count;
  VectorPathDocument document;
  QChar previous_command;
  QChar active_command;
  QPointF current;
  QPointF current_cubic_control;
  QPointF current_quadratic_control;
  QPointF subpath_start;
  qsizetype current_subpath = -1;
  bool current_subpath_closed = false;

  PathParser(QString commands, qsizetype index, qsizetype& anchor_id,
             const QSizeF& viewBox, const QPointF& origin,
             const Affine& element_transform, qsizetype& totalAnchors)
      : data(std::move(commands)),
        path_index(index),
        next_anchor_id(anchor_id),
        view_box(viewBox),
        view_box_origin(origin),
        transform(element_transform),
        total_anchor_count(totalAnchors) {
    document.coordinate_space = CoordinateSpace::Document;
    document.document_size = view_box;
  }

  void skip_separators() {
    while (position < data.size() &&
           (data.at(position).isSpace() || data.at(position) == QLatin1Char(','))) {
      ++position;
    }
  }

  [[nodiscard]] bool at_end() {
    skip_separators();
    return position >= data.size();
  }

  [[nodiscard]] QChar peek() {
    skip_separators();
    return position < data.size() ? data.at(position) : QChar();
  }

  bool number(double* value) {
    skip_separators();
    const auto start = position;
    if (position < data.size() &&
        (data.at(position) == QLatin1Char('+') || data.at(position) == QLatin1Char('-'))) {
      ++position;
    }
    bool digits = false;
    while (position < data.size() && data.at(position).isDigit()) {
      digits = true;
      ++position;
    }
    if (position < data.size() && data.at(position) == QLatin1Char('.')) {
      ++position;
      while (position < data.size() && data.at(position).isDigit()) {
        digits = true;
        ++position;
      }
    }
    if (!digits) {
      position = start;
      return false;
    }
    if (position < data.size() &&
        (data.at(position) == QLatin1Char('e') || data.at(position) == QLatin1Char('E'))) {
      const auto exponent_start = position++;
      if (position < data.size() &&
          (data.at(position) == QLatin1Char('+') || data.at(position) == QLatin1Char('-'))) {
        ++position;
      }
      const auto exponent_digits_start = position;
      while (position < data.size() && data.at(position).isDigit()) ++position;
      if (position == exponent_digits_start) {
        position = exponent_start;
        return false;
      }
    }
    bool ok = false;
    const auto parsed = data.mid(start, position - start).toDouble(&ok);
    if (!ok || !finite(parsed) || std::abs(parsed) > 1.0e9) return false;
    *value = parsed;
    return true;
  }

  bool point(QPointF* output, bool relative) {
    double x = 0.0;
    double y = 0.0;
    if (!number(&x) || !number(&y)) return false;
    *output = relative ? current + QPointF(x, y) : QPointF(x, y);
    return true;
  }

  bool append_anchor(const QPointF& original_position, QPointF incoming = {},
                     QPointF outgoing = {}, QString* error = nullptr) {
    if (current_subpath < 0 || current_subpath_closed)
      return fail(error, QStringLiteral("SVG drawing commands after Z need a new M command."));
    if (total_anchor_count >= kMaxAnchors)
      return fail(error, QStringLiteral("SVG input exceeds the 8192-anchor path limit."));
    const auto mapped = transform.map(original_position) - view_box_origin;
    const auto mapped_incoming = transform.map(original_position + incoming) -
                                 transform.map(original_position);
    const auto mapped_outgoing = transform.map(original_position + outgoing) -
                                 transform.map(original_position);
    if (!finite(mapped.x()) || !finite(mapped.y()) || !finite(mapped_incoming.x()) ||
        !finite(mapped_incoming.y()) || !finite(mapped_outgoing.x()) ||
        !finite(mapped_outgoing.y())) {
      return fail(error, QStringLiteral("Transformed SVG path coordinates must be finite."));
    }
    PathAnchor anchor;
    anchor.id = QStringLiteral("svg-anchor-%1").arg(next_anchor_id++);
    anchor.position = mapped;
    anchor.incoming = mapped_incoming;
    anchor.outgoing = mapped_outgoing;
    anchor.kind = (mapped_incoming.isNull() || mapped_outgoing.isNull())
        ? AnchorKind::Corner : AnchorKind::Smooth;
    anchor.linked_handles = false;
    document.subpaths[current_subpath].anchors.append(std::move(anchor));
    ++total_anchor_count;
    return true;
  }

  bool begin_subpath(const QPointF& first, QString* error) {
    if (document.subpaths.size() >= 256)
      return fail(error, QStringLiteral("An SVG path can contain at most 256 contours."));
    VectorSubpath subpath;
    subpath.id = QStringLiteral("svg-path-%1-contour-%2")
                     .arg(path_index)
                     .arg(document.subpaths.size() + 1);
    document.subpaths.append(std::move(subpath));
    current_subpath = document.subpaths.size() - 1;
    current_subpath_closed = false;
    current = first;
    subpath_start = first;
    return append_anchor(first, {}, {}, error);
  }

  bool append_line(const QPointF& end, QString* error) {
    if (!append_anchor(end, {}, {}, error)) return false;
    current = end;
    return true;
  }

  bool append_cubic(const QPointF& control1, const QPointF& control2,
                    const QPointF& end, QString* error) {
    if (current_subpath < 0 || current_subpath_closed)
      return fail(error, QStringLiteral("SVG drawing commands after Z need a new M command."));
    auto& start_anchor = document.subpaths[current_subpath].anchors.last();
    start_anchor.outgoing = control1 - current;
    if (!append_anchor(end, control2 - end, {}, error)) return false;
    current = end;
    return true;
  }

  bool close_subpath(QString* error) {
    if (current_subpath < 0)
      return fail(error, QStringLiteral("SVG Z command has no current subpath."));
    auto& subpath = document.subpaths[current_subpath];
    if (subpath.anchors.size() > 1 && subpath.anchors.last().position ==
        subpath.anchors.first().position) {
      // A final cubic to the initial point encodes the closing curve in SVG.
      // Move its endpoint handle onto the first anchor so the DTO's closed
      // final segment renders the same curve without a duplicate endpoint.
      subpath.anchors.first().incoming = subpath.anchors.last().incoming;
      subpath.anchors.removeLast();
      --total_anchor_count;
      --next_anchor_id;
    }
    if (subpath.anchors.size() < 3)
      return fail(error, QStringLiteral("Closed SVG contours need at least three anchors."));
    subpath.closed = true;
    current = subpath_start;
    current_subpath_closed = true;
    return true;
  }

  [[nodiscard]] bool has_parameter() {
    const auto character = peek();
    return !character.isNull() && !character.isLetter();
  }

  bool read_required(double* value, QString* error) {
    if (number(value)) return true;
    return fail(error, QStringLiteral("SVG path command %1 has an incomplete parameter group.")
                           .arg(active_command));
  }

  bool process(QString* error) {
    if (data.size() > kMaxPathDataCharacters)
      return fail(error, QStringLiteral("SVG path data exceeds the supported size limit."));
    while (!at_end()) {
      const auto next = peek();
      if (next.isLetter()) {
        active_command = next;
        ++position;
      } else if (active_command.isNull()) {
        return fail(error, QStringLiteral("SVG path data must begin with a supported command."));
      } else if (!has_parameter()) {
        return fail(error, QStringLiteral("SVG path data contains an invalid token."));
      }

      const bool relative = active_command.isLower();
      auto command = active_command.toUpper();
      if (command == QLatin1Char('A'))
        return fail(error, QStringLiteral("SVG arc commands are outside the supported subset; use cubic or quadratic curves."));
      if (command == QLatin1Char('Z')) {
        if (!close_subpath(error)) return false;
        previous_command = active_command;
        active_command = QChar();
        continue;
      }
      if (command != QLatin1Char('M') && command != QLatin1Char('L') &&
          command != QLatin1Char('H') && command != QLatin1Char('V') &&
          command != QLatin1Char('C') && command != QLatin1Char('S') &&
          command != QLatin1Char('Q') && command != QLatin1Char('T')) {
        return fail(error, QStringLiteral("Unsupported SVG path command: %1.").arg(active_command));
      }
      if (command != QLatin1Char('M') && current_subpath < 0)
        return fail(error, QStringLiteral("SVG path commands must begin with M."));

      do {
        if (command == QLatin1Char('M') || command == QLatin1Char('L')) {
          QPointF end;
          if (!point(&end, relative))
            return fail(error, QStringLiteral("SVG path command %1 has an incomplete point.").arg(active_command));
          if (command == QLatin1Char('M')) {
            if (!begin_subpath(end, error)) return false;
            active_command = relative ? QLatin1Char('l') : QLatin1Char('L');
            command = QLatin1Char('L');
          } else if (!append_line(end, error)) {
            return false;
          }
          current = end;
        } else if (command == QLatin1Char('H') || command == QLatin1Char('V')) {
          double coordinate = 0.0;
          if (!read_required(&coordinate, error)) return false;
          QPointF end = current;
          if (command == QLatin1Char('H')) end.setX(relative ? current.x() + coordinate : coordinate);
          else end.setY(relative ? current.y() + coordinate : coordinate);
          if (!append_line(end, error)) return false;
        } else if (command == QLatin1Char('C')) {
          QPointF control1;
          QPointF control2;
          QPointF end;
          if (!point(&control1, relative) || !point(&control2, relative) || !point(&end, relative))
            return fail(error, QStringLiteral("SVG cubic command C has an incomplete parameter group."));
          if (!append_cubic(control1, control2, end, error)) return false;
        } else if (command == QLatin1Char('S')) {
          QPointF control2;
          QPointF end;
          if (!point(&control2, relative) || !point(&end, relative))
            return fail(error, QStringLiteral("SVG smooth cubic command S has an incomplete parameter group."));
          const QPointF control1 = (previous_command.toUpper() == QLatin1Char('C') ||
                                    previous_command.toUpper() == QLatin1Char('S'))
              ? current * 2.0 - current_cubic_control : current;
          if (!append_cubic(control1, control2, end, error)) return false;
          current_cubic_control = control2;
        } else if (command == QLatin1Char('Q')) {
          QPointF quadratic;
          QPointF end;
          if (!point(&quadratic, relative) || !point(&end, relative))
            return fail(error, QStringLiteral("SVG quadratic command Q has an incomplete parameter group."));
          const auto control1 = current + (quadratic - current) * (2.0 / 3.0);
          const auto control2 = end + (quadratic - end) * (2.0 / 3.0);
          if (!append_cubic(control1, control2, end, error)) return false;
          current_quadratic_control = quadratic;
        } else if (command == QLatin1Char('T')) {
          QPointF end;
          if (!point(&end, relative))
            return fail(error, QStringLiteral("SVG smooth quadratic command T has an incomplete point."));
          const QPointF quadratic = (previous_command.toUpper() == QLatin1Char('Q') ||
                                     previous_command.toUpper() == QLatin1Char('T'))
              ? current * 2.0 - current_quadratic_control : current;
          const auto control1 = current + (quadratic - current) * (2.0 / 3.0);
          const auto control2 = end + (quadratic - end) * (2.0 / 3.0);
          if (!append_cubic(control1, control2, end, error)) return false;
          current_quadratic_control = quadratic;
        }
        previous_command = active_command;
        if (command == QLatin1Char('C') || command == QLatin1Char('S')) {
          current_cubic_control = document.subpaths[current_subpath].anchors.last().position +
                                  document.subpaths[current_subpath].anchors.last().incoming;
        }
      } while (has_parameter());
    }
    if (document.subpaths.isEmpty())
      return fail(error, QStringLiteral("An SVG path must contain at least one contour."));
    for (const auto& subpath : document.subpaths) {
      if (!subpath.closed && subpath.anchors.size() < 2)
        return fail(error, QStringLiteral("Open SVG contours need at least two anchors."));
    }
    return true;
  }
};

QPainterPath fill_path(const VectorPathDocument& document, Qt::FillRule rule) {
  QPainterPath result;
  result.setFillRule(rule);
  for (const auto& subpath : document.subpaths) {
    if (!subpath.closed || subpath.anchors.isEmpty()) continue;
    const auto& first = subpath.anchors.first();
    result.moveTo(first.position);
    for (qsizetype index = 0; index < subpath.anchors.size(); ++index) {
      const auto& start = subpath.anchors.at(index);
      const auto& end = subpath.anchors.at((index + 1) % subpath.anchors.size());
      if (start.outgoing.isNull() && end.incoming.isNull()) {
        if (index + 1 < subpath.anchors.size()) result.lineTo(end.position);
      } else {
        result.cubicTo(start.position + start.outgoing,
                       end.position + end.incoming, end.position);
      }
    }
    result.closeSubpath();
  }
  return result;
}

bool color_with_opacity(const QString& color, double opacity, QString* output,
                        QString* error) {
  if (output == nullptr) return fail(error, QStringLiteral("A color output is required."));
  if (color.isEmpty()) {
    output->clear();
    return true;
  }
  QColor parsed(color);
  if (!parsed.isValid())
    return fail(error, QStringLiteral("Unsupported SVG color: %1.").arg(color));
  parsed.setAlpha(qRound(static_cast<double>(parsed.alpha()) * opacity));
  *output = parsed.alpha() == 255 ? parsed.name(QColor::HexRgb)
                                  : parsed.name(QColor::HexArgb);
  return true;
}

bool export_color(const QString& input, QString* rgb, double* opacity,
                  QString* error) {
  if (rgb == nullptr || opacity == nullptr)
    return fail(error, QStringLiteral("An SVG color output is required."));
  const QColor color(input);
  if (!color.isValid())
    return fail(error, QStringLiteral("Unsupported vector path color: %1.").arg(input));
  *rgb = color.name(QColor::HexRgb);
  *opacity = static_cast<double>(color.alpha()) / 255.0;
  return true;
}

bool validate_path_view_box(const VectorPathDocument& path, QString* error) {
  const auto validation = vector_paths::validate(path);
  return validation.isEmpty() ? true : fail(error, validation);
}

QString number_text(double value) {
  if (value == 0.0) value = 0.0;
  return QString::number(value, 'g', 15);
}

QString document_path_data(const VectorPathDocument& path, const QSizeF& view_box) {
  QString data;
  const auto mapped = [&path, &view_box](const QPointF& point) {
    if (path.coordinate_space == CoordinateSpace::Normalized)
      return QPointF(point.x() * view_box.width(), point.y() * view_box.height());
    return QPointF(point.x() * view_box.width() / path.document_size.width(),
                   point.y() * view_box.height() / path.document_size.height());
  };
  auto append = [&data](const QString& command) {
    if (!data.isEmpty()) data.append(QLatin1Char(' '));
    data.append(command);
  };
  for (const auto& subpath : path.subpaths) {
    if (subpath.anchors.isEmpty()) continue;
    const auto& first = subpath.anchors.first();
    const auto first_position = mapped(first.position);
    append(QStringLiteral("M %1 %2").arg(number_text(first_position.x()),
                                           number_text(first_position.y())));
    const qsizetype segment_count = subpath.closed ? subpath.anchors.size()
                                                   : subpath.anchors.size() - 1;
    for (qsizetype index = 0; index < segment_count; ++index) {
      const auto& start = subpath.anchors.at(index);
      const auto& end = subpath.anchors.at((index + 1) % subpath.anchors.size());
      const bool closing = subpath.closed && index + 1 == subpath.anchors.size();
      const auto end_position = mapped(end.position);
      const auto control1 = mapped(start.position + start.outgoing);
      const auto control2 = mapped(end.position + end.incoming);
      if (start.outgoing.isNull() && end.incoming.isNull()) {
        if (!closing) append(QStringLiteral("L %1 %2").arg(number_text(end_position.x()),
                                                              number_text(end_position.y())));
      } else {
        append(QStringLiteral("C %1 %2 %3 %4 %5 %6")
                   .arg(number_text(control1.x()), number_text(control1.y()),
                        number_text(control2.x()), number_text(control2.y()),
                        number_text(end_position.x()), number_text(end_position.y())));
      }
    }
    if (subpath.closed) append(QStringLiteral("Z"));
  }
  return data;
}

struct SvgContext {
  QSizeF view_box;
  QPointF origin;
  Affine root_transform;
  QVector<StyledPath> paths;
  qsizetype total_anchors = 0;
  qsizetype nonzero_comparison_anchors = 0;
  qsizetype next_anchor_id = 1;
};

bool parse_view_box(const QString& text, QSizeF* size, QPointF* origin,
                    QString* error) {
  QVector<double> values;
  if (!parse_number_list(text, &values, error)) return false;
  if (values.size() != 4 || values.at(2) <= 0.0 || values.at(3) <= 0.0 ||
      values.at(2) > 32768.0 || values.at(3) > 32768.0) {
    return fail(error, QStringLiteral("SVG viewBox must contain finite min-x, min-y, positive width and height up to 32768."));
  }
  *origin = QPointF(values.at(0), values.at(1));
  *size = QSizeF(values.at(2), values.at(3));
  return true;
}

bool allowed_svg_root_attribute(const QString& name) {
  return name == QStringLiteral("viewBox") || name == QStringLiteral("width") ||
         name == QStringLiteral("height") || name == QStringLiteral("version") ||
         name == QStringLiteral("preserveAspectRatio") || name == QStringLiteral("id") ||
         name == QStringLiteral("style") || name == QStringLiteral("fill") ||
         name == QStringLiteral("stroke") || name == QStringLiteral("stroke-width") ||
         name == QStringLiteral("fill-opacity") || name == QStringLiteral("stroke-opacity") ||
         name == QStringLiteral("fill-rule");
}

bool validate_attributes(const QXmlStreamAttributes& attributes, const QString& element,
                         QString* error) {
  for (const auto& attribute : attributes) {
    const auto name = attribute.name().toString();
    const bool common = name == QStringLiteral("id") || name == QStringLiteral("transform") ||
                        name == QStringLiteral("style") || is_style_property(name);
    const bool path_specific = element == QStringLiteral("path") && name == QStringLiteral("d");
    const bool root_specific = element == QStringLiteral("svg") &&
                               allowed_svg_root_attribute(name);
    if (!common && !path_specific && !root_specific)
      return fail(error, QStringLiteral("Unsupported SVG attribute on <%1>: %2.")
                             .arg(element, name));
  }
  return true;
}

bool parse_path_element(QXmlStreamReader* xml, const Style& inherited_style,
                        const Affine& parent_transform, SvgContext* context,
                        QString* error) {
  if (xml == nullptr || context == nullptr)
    return fail(error, QStringLiteral("An SVG path parser context is required."));
  if (context->paths.size() >= kMaxPaths)
    return fail(error, QStringLiteral("An SVG document can contain at most 256 path elements."));
  const auto attributes = xml->attributes();
  if (!validate_attributes(attributes, QStringLiteral("path"), error)) return false;
  Style style;
  if (!apply_style_attributes(attributes, inherited_style, &style, error)) return false;
  Affine descendant_transform;
  if (!parse_transform_attribute(attributes, parent_transform,
                                 &descendant_transform, error)) return false;
  const Affine view_box_translation{1.0, 0.0, 0.0, 1.0,
                                    -context->origin.x(), -context->origin.y()};
  const auto transform = multiply(
      context->root_transform,
      multiply(view_box_translation, descendant_transform));
  if (!transform.is_finite())
    return fail(error, QStringLiteral("Composed SVG transforms exceed the supported numeric range."));
  QString commands;
  if (!path_attribute(attributes, QStringLiteral("d"), &commands) || commands.isEmpty())
    return fail(error, QStringLiteral("SVG path elements need non-empty d data."));
  double width_scale = 1.0;
  if (!style.stroke.isEmpty() && !transform.has_uniform_scale(&width_scale))
    return fail(error, QStringLiteral("A stroked SVG path with skew or non-uniform transforms cannot be represented by the scalar stroke width."));

  PathParser parser(commands, context->paths.size() + 1, context->next_anchor_id,
                    context->view_box, QPointF{}, transform,
                    context->total_anchors);
  if (!parser.process(error)) return false;
  auto& document = parser.document;
  const auto validation = vector_paths::validate(document);
  if (!validation.isEmpty())
    return fail(error, QStringLiteral("SVG path cannot be represented by the editable path model: %1").arg(validation));
  for (const auto& subpath : document.subpaths) {
    if (!style.fill.isEmpty() && !subpath.closed)
      return fail(error, QStringLiteral("SVG fills implicitly close open contours; this editable path layer requires a closed contour. Set fill=none or close the contour."));
  }
  if (!style.fill.isEmpty() && style.fill_rule == QStringLiteral("nonzero")) {
    qsizetype comparison_anchors = 0;
    for (const auto& subpath : document.subpaths) comparison_anchors += subpath.anchors.size();
    if (comparison_anchors > kMaxNonzeroComparisonAnchors ||
        context->nonzero_comparison_anchors + comparison_anchors >
            kMaxNonzeroComparisonTotalAnchors) {
      return fail(error, QStringLiteral("This nonzero-filled SVG exceeds the bounded fill-equivalence check; use fill-rule=evenodd to declare supported semantics."));
    }
    const auto evenodd = fill_path(document, Qt::OddEvenFill);
    const auto winding = fill_path(document, Qt::WindingFill);
    const auto difference = winding.subtracted(evenodd).simplified();
    const auto reverse_difference = evenodd.subtracted(winding).simplified();
    if (!difference.isEmpty() || !reverse_difference.isEmpty())
      return fail(error, QStringLiteral("SVG nonzero fill-rule changes this contour's filled area; the editable path model supports evenodd fill only."));
    context->nonzero_comparison_anchors += comparison_anchors;
  }
  if (!style.fill.isEmpty()) {
    QString canonical;
    if (!color_with_opacity(style.fill, style.fill_opacity, &canonical, error)) return false;
    style.fill = canonical;
  }
  if (!style.stroke.isEmpty()) {
    QString canonical;
    if (!color_with_opacity(style.stroke, style.stroke_opacity, &canonical, error)) return false;
    style.stroke = canonical;
  }
  style.stroke_width *= width_scale;
  if (!finite(style.stroke_width) || style.stroke_width > 32768.0)
    return fail(error, QStringLiteral("Transformed SVG stroke-width exceeds the supported range."));

  StyledPath imported;
  imported.path = std::move(document);
  imported.fill_color = style.fill;
  imported.stroke_color = style.stroke;
  imported.stroke_width = style.stroke_width;
  context->paths.append(std::move(imported));
  while (!xml->atEnd()) {
    const auto token = xml->readNext();
    if (token == QXmlStreamReader::EndElement) {
      if (xml->name() != QStringLiteral("path"))
        return fail(error, QStringLiteral("SVG path element nesting is invalid."));
      return true;
    }
    if (token == QXmlStreamReader::StartElement)
      return fail(error, QStringLiteral("SVG <path> elements cannot contain child elements."));
    if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference ||
        token == QXmlStreamReader::ProcessingInstruction)
      return fail(error, QStringLiteral("SVG document types, entity references and processing instructions are not supported."));
    if (token == QXmlStreamReader::Characters && !xml->text().toString().trimmed().isEmpty())
      return fail(error, QStringLiteral("SVG path elements cannot contain text."));
    if (token == QXmlStreamReader::Invalid)
      return fail(error, QStringLiteral("Invalid SVG XML: %1").arg(xml->errorString()));
  }
  return fail(error, QStringLiteral("SVG path element is not closed."));
}

bool parse_group_or_root_children(QXmlStreamReader* xml, const QString& closing_name,
                                  const Style& inherited_style,
                                  const Affine& parent_transform, int depth,
                                  SvgContext* context, QString* error) {
  if (depth > kMaxXmlDepth)
    return fail(error, QStringLiteral("SVG group nesting exceeds the supported depth of 16."));
  while (!xml->atEnd()) {
    const auto token = xml->readNext();
    if (token == QXmlStreamReader::EndElement) {
      if (xml->name() != closing_name)
        return fail(error, QStringLiteral("SVG element nesting is invalid."));
      return true;
    }
    if (token == QXmlStreamReader::StartElement) {
      const auto element = xml->name().toString();
      const auto namespace_uri = xml->namespaceUri().toString();
      if (!namespace_uri.isEmpty() && namespace_uri != QStringLiteral("http://www.w3.org/2000/svg"))
        return fail(error, QStringLiteral("Only SVG namespace elements are supported."));
      if (element == QStringLiteral("path")) {
        if (!parse_path_element(xml, inherited_style, parent_transform, context, error)) return false;
      } else if (element == QStringLiteral("g")) {
        const auto attributes = xml->attributes();
        if (!validate_attributes(attributes, QStringLiteral("g"), error)) return false;
        Style group_style;
        if (!apply_style_attributes(attributes, inherited_style, &group_style, error)) return false;
        Affine group_transform;
        if (!parse_transform_attribute(attributes, parent_transform, &group_transform, error)) return false;
        if (!parse_group_or_root_children(xml, QStringLiteral("g"), group_style,
                                          group_transform, depth + 1, context, error)) return false;
      } else {
        return fail(error, QStringLiteral("Unsupported SVG element <%1>; only <svg>, <g> and <path> are supported." )
                               .arg(element));
      }
    } else if (token == QXmlStreamReader::DTD ||
               token == QXmlStreamReader::EntityReference ||
               token == QXmlStreamReader::ProcessingInstruction) {
      return fail(error, QStringLiteral("SVG document types, entity references and processing instructions are not supported."));
    } else if (token == QXmlStreamReader::Characters &&
               !xml->text().toString().trimmed().isEmpty()) {
      return fail(error, QStringLiteral("SVG container elements cannot contain text."));
    } else if (token == QXmlStreamReader::Invalid) {
      return fail(error, QStringLiteral("Invalid SVG XML: %1").arg(xml->errorString()));
    }
  }
  return fail(error, QStringLiteral("SVG <%1> element is not closed.").arg(closing_name));
}

bool parse_svg(QXmlStreamReader* xml, SvgContext* context, QString* error) {
  while (!xml->atEnd()) {
    const auto token = xml->readNext();
    if (token == QXmlStreamReader::StartElement) {
      if (xml->name() != QStringLiteral("svg"))
        return fail(error, QStringLiteral("The SVG document root must be <svg>."));
      const auto namespace_uri = xml->namespaceUri().toString();
      if (!namespace_uri.isEmpty() && namespace_uri != QStringLiteral("http://www.w3.org/2000/svg"))
        return fail(error, QStringLiteral("The root element must use the SVG namespace."));
      const auto attributes = xml->attributes();
      if (!validate_attributes(attributes, QStringLiteral("svg"), error)) return false;
      QString view_box_text;
      if (!path_attribute(attributes, QStringLiteral("viewBox"), &view_box_text))
        return fail(error, QStringLiteral("SVG interchange requires an explicit viewBox."));
      if (!parse_view_box(view_box_text, &context->view_box, &context->origin, error)) return false;
      Style root_style;
      if (!apply_style_attributes(attributes, Style{}, &root_style, error)) return false;
      if (!parse_transform_attribute(attributes, Affine{},
                                     &context->root_transform, error)) return false;
      if (!parse_group_or_root_children(xml, QStringLiteral("svg"), root_style,
                                        Affine{}, 1, context, error)) return false;
      while (!xml->atEnd()) {
        const auto trailing = xml->readNext();
        if (trailing == QXmlStreamReader::StartElement)
          return fail(error, QStringLiteral("An SVG file can contain only one root element."));
        if (trailing == QXmlStreamReader::DTD ||
            trailing == QXmlStreamReader::EntityReference ||
            trailing == QXmlStreamReader::ProcessingInstruction)
          return fail(error, QStringLiteral("SVG document types, entity references and processing instructions are not supported."));
        if (trailing == QXmlStreamReader::Invalid)
          return fail(error, QStringLiteral("Invalid SVG XML: %1").arg(xml->errorString()));
      }
      if (xml->hasError()) return fail(error, QStringLiteral("Invalid SVG XML: %1").arg(xml->errorString()));
      if (context->paths.isEmpty())
        return fail(error, QStringLiteral("SVG document contains no supported path elements."));
      return true;
    }
    if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference ||
        token == QXmlStreamReader::ProcessingInstruction)
      return fail(error, QStringLiteral("SVG document types, entity references and processing instructions are not supported."));
    if (token == QXmlStreamReader::Invalid)
      return fail(error, QStringLiteral("Invalid SVG XML: %1").arg(xml->errorString()));
  }
  return fail(error, QStringLiteral("The input is not an SVG document."));
}

}  // namespace

bool import_svg(const QByteArray& bytes, QVector<StyledPath>* paths, QString* error) {
  if (paths == nullptr) return fail(error, QStringLiteral("An SVG path output is required."));
  if (bytes.isEmpty() || bytes.size() > kMaxSvgBytes)
    return fail(error, QStringLiteral("SVG files must contain between 1 byte and 4 MiB of XML."));
  QXmlStreamReader xml(bytes);
  SvgContext context;
  if (!parse_svg(&xml, &context, error)) return false;
  *paths = std::move(context.paths);
  set_error(error, {});
  return true;
}

QByteArray export_svg(const QVector<StyledPath>& paths, const QSizeF& view_box,
                      QString* error) {
  if (paths.isEmpty() || paths.size() > kMaxPaths) {
    set_error(error, QStringLiteral("Export needs between 1 and 256 editable vector paths."));
    return {};
  }
  if (!finite(view_box.width()) || !finite(view_box.height()) ||
      view_box.width() <= 0.0 || view_box.height() <= 0.0 ||
      view_box.width() > 32768.0 || view_box.height() > 32768.0) {
    set_error(error, QStringLiteral("SVG viewBox dimensions must be positive and at most 32768."));
    return {};
  }
  qsizetype total_anchors = 0;
  QVector<QString> fill_colors;
  QVector<QString> stroke_colors;
  QVector<double> fill_opacities;
  QVector<double> stroke_opacities;
  fill_colors.reserve(paths.size());
  stroke_colors.reserve(paths.size());
  fill_opacities.reserve(paths.size());
  stroke_opacities.reserve(paths.size());
  for (const auto& styled : paths) {
    if (!validate_path_view_box(styled.path, error)) return {};
    if (!styled.fill_color.isEmpty()) {
      for (const auto& subpath : styled.path.subpaths) {
        if (!subpath.closed) {
          set_error(error, QStringLiteral("SVG fills implicitly close open contours; export requires a closed contour or fill=none."));
          return {};
        }
      }
    }
    for (const auto& subpath : styled.path.subpaths) total_anchors += subpath.anchors.size();
    if (total_anchors > kMaxAnchors) {
      set_error(error, QStringLiteral("Export exceeds the 8192-anchor path limit."));
      return {};
    }
    if (!finite(styled.stroke_width) || styled.stroke_width < 0.0 ||
        styled.stroke_width > 32768.0) {
      set_error(error, QStringLiteral("Exported stroke width must be finite and between 0 and 32768."));
      return {};
    }
    QString fill_rgb;
    QString stroke_rgb;
    double fill_opacity = 1.0;
    double stroke_opacity = 1.0;
    if (!styled.fill_color.isEmpty() &&
        !export_color(styled.fill_color, &fill_rgb, &fill_opacity, error)) return {};
    if (!styled.stroke_color.isEmpty() &&
        !export_color(styled.stroke_color, &stroke_rgb, &stroke_opacity, error)) return {};
    fill_colors.append(std::move(fill_rgb));
    stroke_colors.append(std::move(stroke_rgb));
    fill_opacities.append(fill_opacity);
    stroke_opacities.append(stroke_opacity);
  }

  QByteArray output;
  QXmlStreamWriter writer(&output);
  writer.setAutoFormatting(true);
  writer.writeStartDocument(QStringLiteral("1.0"));
  writer.writeStartElement(QStringLiteral("svg"));
  writer.writeDefaultNamespace(QStringLiteral("http://www.w3.org/2000/svg"));
  writer.writeAttribute(QStringLiteral("viewBox"),
                        QStringLiteral("0 0 %1 %2")
                            .arg(number_text(view_box.width()), number_text(view_box.height())));
  for (qsizetype index = 0; index < paths.size(); ++index) {
    const auto& styled = paths.at(index);
    writer.writeEmptyElement(QStringLiteral("path"));
    writer.writeAttribute(QStringLiteral("d"), document_path_data(styled.path, view_box));
    writer.writeAttribute(QStringLiteral("fill"), styled.fill_color.isEmpty()
                              ? QStringLiteral("none") : fill_colors.at(index));
    writer.writeAttribute(QStringLiteral("stroke"), styled.stroke_color.isEmpty()
                                ? QStringLiteral("none") : stroke_colors.at(index));
    writer.writeAttribute(QStringLiteral("stroke-width"), number_text(styled.stroke_width));
    writer.writeAttribute(QStringLiteral("fill-rule"), QStringLiteral("evenodd"));
    if (!styled.fill_color.isEmpty() && fill_opacities.at(index) < 1.0)
      writer.writeAttribute(QStringLiteral("fill-opacity"), number_text(fill_opacities.at(index)));
    if (!styled.stroke_color.isEmpty() && stroke_opacities.at(index) < 1.0)
      writer.writeAttribute(QStringLiteral("stroke-opacity"), number_text(stroke_opacities.at(index)));
  }
  writer.writeEndElement();
  writer.writeEndDocument();
  if (writer.hasError()) {
    set_error(error, QStringLiteral("The SVG document could not be serialized."));
    return {};
  }
  set_error(error, {});
  return output;
}

}  // namespace photo_editor::desktop::vector_path_io
