#pragma once

#include "photo_text_style.h"
#include "photo_text_layout.h"

#include <QFontInfo>
#include <QAbstractTextDocumentLayout>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace photo_editor::desktop {

// Apply paragraph metrics to the native editor so its caret, selection and IME
// use the same document-pixel line advance as the committed text renderer.
class PhotoTextDocumentStyle : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY changed)
  Q_PROPERTY(QVariantMap style READ style WRITE setStyle NOTIFY changed)
  Q_PROPERTY(QString sourceText READ sourceText WRITE setSourceText NOTIFY changed)
  Q_PROPERTY(QFont font READ font NOTIFY changed)
  Q_PROPERTY(qreal topInset READ topInset NOTIFY metricsChanged)
  Q_PROPERTY(QVariantMap pointGeometry READ pointGeometry NOTIFY pointGeometryChanged)

 public:
  explicit PhotoTextDocumentStyle(QObject* parent = nullptr) : QObject(parent) {
    geometry_timer_.setSingleShot(true);
    geometry_timer_.setInterval(40);
    connect(&geometry_timer_, &QTimer::timeout, this, [this] { updateGeometry(); });
  }
  QQuickTextDocument* document() const { return document_; }
  QVariantMap style() const { return style_; }
  QString sourceText() const { return source_text_; }
  QVariantMap pointGeometry() const { return point_geometry_; }
  qreal topInset() const {
    if (!document_ || style_.isEmpty()) return 0;
    auto* text = document_->textDocument();
    (void)text->documentLayout()->documentSize();
    const auto* layout = text->begin().layout();
    return layout && layout->lineCount() > 0 ? layout->lineAt(0).y() : 0;
  }
  QFont font() const {
    const auto decoded = decode_photo_text_style(QJsonObject::fromVariantMap(style_));
    if (!decoded) return QFont(QStringLiteral("Helvetica"));
    QFont result(decoded->family);
    if (!decoded->face.isEmpty()) result.setStyleName(decoded->face);
    // QQuick uses the host font DPI, whereas the export raster has fixed DPI.
    // Resolve that ratio rather than rounding document-pixel sizes to integers.
    result.setPointSizeF(72);
    const auto dpi = QFontInfo(result).pixelSize();
    result.setPointSizeF(decoded->size * 72.0 / std::max(1, dpi));
    result.setWeight(static_cast<QFont::Weight>(decoded->weight));
    result.setItalic(decoded->italic);
    result.setUnderline(decoded->underline);
    result.setLetterSpacing(QFont::AbsoluteSpacing, decoded->tracking);
    result.setHintingPreference(QFont::PreferNoHinting);
    apply_photo_text_variable_axes(result, *decoded);
    return result;
  }
  void setDocument(QQuickTextDocument* value) {
    if (document_ == value) return;
    if (document_) {
      disconnect(document_->textDocument(), nullptr, this, nullptr);
      disconnect(document_->textDocument()->documentLayout(), nullptr, this, nullptr);
      geometry_timer_.stop();
    }
    document_ = value;
    if (document_) {
      connect(document_->textDocument(), &QTextDocument::contentsChanged,
          this, [this] { updateContentMetrics(); });
      connect(document_->textDocument()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
          this, [this] { emit metricsChanged(); });
    }
    applyStyle();
    updateGeometry();
    emit changed();
  }
  void setStyle(const QVariantMap& value) {
    if (style_ == value) return;
    style_ = value;
    applyStyle();
    updateGeometry();
    emit changed();
  }
  void setSourceText(const QString& value) {
    if (source_text_ == value) return;
    source_text_ = value;
    applyStyle();
    updateGeometry();
    emit changed();
  }

 signals:
  void changed();
  void metricsChanged();
  void pointGeometryChanged();

 private:
  void applyStyle() {
    if (applying_ || !document_) return;
    const auto decoded = decode_photo_text_style(QJsonObject::fromVariantMap(style_));
    applying_ = true;
    auto* text = document_->textDocument();
    const auto effective_style = decoded
        ? std::optional<PhotoTextStyle>(photo_text_style_with_normalized_line_breaks(*decoded, source_text_))
        : std::nullopt;
    const auto* active_style = effective_style ? &*effective_style : nullptr;
    QTextCursor transaction(text);
    transaction.joinPreviousEditBlock();
    QTextCharFormat base_character_format;
    base_character_format.setTextOutline(active_style && active_style->stroke_width > 0
        ? QPen(active_style->stroke_color, active_style->stroke_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
        : QPen(Qt::NoPen));
    if (active_style) {
      // Style changes can remove or replace sparse runs. Reset every character
      // to the current layer style first so old run formats cannot linger in
      // QTextDocument after undo or when a run is shortened.
      base_character_format.setFont(fontForAttributes(*active_style, {}));
      base_character_format.setForeground(active_style->color);
      characters_select_document(text, base_character_format);
    } else {
      QTextCursor characters(text);
      characters.select(QTextCursor::Document);
      characters.mergeCharFormat(base_character_format);
    }
    applyBlockMetrics(active_style);
    if (active_style) {
      for (const auto& run : active_style->runs) {
        const auto start = run.value(QStringLiteral("start")).toInt();
        const auto length = run.value(QStringLiteral("length")).toInt();
        if (start < 0 || length <= 0 || start + length > text->characterCount() - 1) continue;
        QTextCursor cursor(text);
        cursor.setPosition(start);
        cursor.setPosition(start + length, QTextCursor::KeepAnchor);
        const auto attributes = photo_text_run_attributes(run);
        QTextCharFormat character_format;
        if (attributes.contains(QStringLiteral("family")) || attributes.contains(QStringLiteral("size")) ||
            attributes.contains(QStringLiteral("weight")) || attributes.contains(QStringLiteral("italic")) ||
            attributes.contains(QStringLiteral("underline")))
          character_format.setFont(fontForAttributes(*active_style, attributes));
        if (attributes.contains(QStringLiteral("color")))
          character_format.setForeground(QColor(attributes.value(QStringLiteral("color")).toString()));
        if (attributes.contains(QStringLiteral("strokeWidth")) || attributes.contains(QStringLiteral("strokeColor"))) {
          const auto stroke_width = attributes.value(QStringLiteral("strokeWidth"), active_style->stroke_width).toDouble();
          const auto stroke_color = QColor(attributes.value(QStringLiteral("strokeColor"),
              active_style->stroke_color.name(QColor::HexArgb)).toString());
          character_format.setTextOutline(stroke_width > 0
              ? QPen(stroke_color, stroke_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
              : QPen(Qt::NoPen));
        }
        cursor.mergeCharFormat(character_format);
      }
    }
    transaction.endEditBlock();
    applying_ = false;
    emit metricsChanged();
  }

  QFont fontForAttributes(const PhotoTextStyle& style, const QVariantMap& attributes) const {
    const auto family = attributes.value(QStringLiteral("family"), style.family).toString();
    const auto inherit_axes = family.compare(style.family, Qt::CaseInsensitive) == 0;
    QFont result(family);
    if (inherit_axes && !style.face.isEmpty()) result.setStyleName(style.face);
    result.setPointSizeF(72);
    const auto dpi = QFontInfo(result).pixelSize();
    const auto size = attributes.value(QStringLiteral("size"), style.size).toDouble();
    result.setPointSizeF(size * 72.0 / std::max(1, dpi));
    result.setWeight(static_cast<QFont::Weight>(attributes.value(QStringLiteral("weight"), style.weight).toInt()));
    result.setItalic(attributes.value(QStringLiteral("italic"), style.italic).toBool());
    result.setUnderline(attributes.value(QStringLiteral("underline"), style.underline).toBool());
    result.setLetterSpacing(QFont::AbsoluteSpacing, style.tracking);
    result.setHintingPreference(QFont::PreferNoHinting);
    if (inherit_axes) apply_photo_text_variable_axes(result, style);
    return result;
  }

  static void characters_select_document(QTextDocument* text, const QTextCharFormat& format) {
    QTextCursor characters(text);
    characters.select(QTextCursor::Document);
    characters.mergeCharFormat(format);
  }

  void updateContentMetrics() {
    if (applying_ || !document_) return;
    const auto decoded = decode_photo_text_style(QJsonObject::fromVariantMap(style_));
    const auto effective_style = decoded
        ? std::optional<PhotoTextStyle>(photo_text_style_with_normalized_line_breaks(*decoded, source_text_))
        : std::nullopt;
    applying_ = true;
    QTextCursor transaction(document_->textDocument());
    transaction.joinPreviousEditBlock();
    applyBlockMetrics(effective_style ? &*effective_style : nullptr);
    transaction.endEditBlock();
    applying_ = false;
    emit metricsChanged();
    if (!decoded || !decoded->point_text) return;
    if (measured_text_ != document_->textDocument()->toPlainText()) geometry_timer_.start();
  }

  void applyBlockMetrics(const PhotoTextStyle* style) {
    if (!document_) return;
    auto* text = document_->textDocument();
    const auto height_type = style ? QTextBlockFormat::FixedHeight : QTextBlockFormat::SingleHeight;
    for (auto block = text->begin(); block.isValid(); block = block.next()) {
      auto format = block.blockFormat();
      const auto spacing = style && block.next().isValid() ? style->paragraph_spacing : 0;
      auto advance = 0.0;
      if (style) {
        const auto block_length = std::max(0, block.length() - 1);
        const auto maximum_size = photo_text_max_font_size(*style, block.position(), block_length);
        advance = maximum_size * style->line_height;
      }
      if (format.lineHeightType() == height_type &&
          qFuzzyCompare(format.lineHeight(), advance) && qFuzzyCompare(format.bottomMargin(), spacing)) continue;
      format.setLineHeight(advance, height_type);
      format.setBottomMargin(spacing);
      QTextCursor cursor(block);
      cursor.setBlockFormat(format);
    }
  }

  void updateGeometry() {
    if (!document_) return;
    // Geometry depends on content/style, never on the editor viewport's width.
    // A separate cached notification avoids a width -> layout -> width loop.
    auto* text = document_->textDocument();
    const auto content = text->toPlainText();
    const auto decoded = decode_photo_text_style(QJsonObject::fromVariantMap(style_));
    if (measured_text_ != content || measured_style_ != style_ || measured_source_text_ != source_text_) {
      measured_text_ = content;
      measured_style_ = style_;
      measured_source_text_ = source_text_;
      QVariantMap next_geometry;
      if (decoded && decoded->point_text) {
        const auto effective_style = photo_text_style_with_normalized_line_breaks(*decoded, source_text_);
        const auto geometry = measure_photo_point_text(content, effective_style);
        next_geometry = {{"width", geometry.size.width()}, {"height", geometry.size.height()},
            {"originX", geometry.origin.x()}, {"originY", geometry.origin.y()}, {"error", geometry.error}};
      }
      if (point_geometry_ != next_geometry) {
        point_geometry_ = next_geometry;
        emit pointGeometryChanged();
      }
    }
    emit metricsChanged();
  }
  QPointer<QQuickTextDocument> document_;
  QVariantMap style_;
  QVariantMap point_geometry_;
  QVariantMap measured_style_;
  QString measured_text_;
  QString source_text_;
  QString measured_source_text_;
  QTimer geometry_timer_;
  bool applying_ = false;
};

}  // namespace photo_editor::desktop
