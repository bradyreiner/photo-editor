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
#include <QtQml/qqmlregistration.h>

namespace photo_editor::desktop {

// Apply paragraph metrics to the native editor so its caret, selection and IME
// use the same document-pixel line advance as the committed text renderer.
class PhotoTextDocumentStyle : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY changed)
  Q_PROPERTY(QVariantMap style READ style WRITE setStyle NOTIFY changed)
  Q_PROPERTY(QFont font READ font NOTIFY changed)
  Q_PROPERTY(qreal topInset READ topInset NOTIFY metricsChanged)
  Q_PROPERTY(QVariantMap pointGeometry READ pointGeometry NOTIFY pointGeometryChanged)

 public:
  explicit PhotoTextDocumentStyle(QObject* parent = nullptr) : QObject(parent) {}
  QQuickTextDocument* document() const { return document_; }
  QVariantMap style() const { return style_; }
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
    return result;
  }
  void setDocument(QQuickTextDocument* value) {
    if (document_ == value) return;
    if (document_) {
      disconnect(document_->textDocument(), nullptr, this, nullptr);
      disconnect(document_->textDocument()->documentLayout(), nullptr, this, nullptr);
    }
    document_ = value;
    if (document_) {
      connect(document_->textDocument(), &QTextDocument::contentsChanged,
          this, [this] { apply(); });
      connect(document_->textDocument()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
          this, [this] { emit metricsChanged(); });
    }
    apply();
    emit changed();
  }
  void setStyle(const QVariantMap& value) {
    if (style_ == value) return;
    style_ = value;
    apply();
    emit changed();
  }

 signals:
  void changed();
  void metricsChanged();
  void pointGeometryChanged();

 private:
  void apply() {
    if (applying_ || !document_) return;
    const auto decoded = decode_photo_text_style(QJsonObject::fromVariantMap(style_));
    applying_ = true;
    auto* text = document_->textDocument();
    const auto advance = decoded ? decoded->size * decoded->line_height : 0;
    const auto height_type = decoded ? QTextBlockFormat::FixedHeight : QTextBlockFormat::SingleHeight;
    QTextCursor transaction(text);
    transaction.joinPreviousEditBlock();
    for (auto block = text->begin(); block.isValid(); block = block.next()) {
      auto format = block.blockFormat();
      const auto spacing = decoded && block.next().isValid() ? decoded->paragraph_spacing : 0;
      if (format.lineHeightType() == height_type &&
          qFuzzyCompare(format.lineHeight(), advance) && qFuzzyCompare(format.bottomMargin(), spacing)) continue;
      format.setLineHeight(advance, height_type);
      format.setBottomMargin(spacing);
      QTextCursor cursor(block);
      cursor.setBlockFormat(format);
    }
    transaction.endEditBlock();
    applying_ = false;
    // Geometry depends on content/style, never on the editor viewport's width.
    // A separate cached notification avoids a width -> layout -> width loop.
    const auto content = text->toPlainText();
    if (measured_text_ != content || measured_style_ != style_) {
      measured_text_ = content;
      measured_style_ = style_;
      QVariantMap next_geometry;
      if (decoded && decoded->point_text) {
        const auto geometry = measure_photo_point_text(content, *decoded);
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
  bool applying_ = false;
};

}  // namespace photo_editor::desktop
