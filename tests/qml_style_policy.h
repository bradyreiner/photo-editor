#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QQmlProperty>
#include <QQuickItem>
#include <QStringList>
#include <algorithm>
#include <cmath>

// Used by real application screenshot routes as well as the primitive test.
// Contract markers identify the component; acceptance uses evaluated values.
inline QStringList qml_popup_style_violations(QObject* root) {
  QStringList errors;
  auto objects = root->findChildren<QObject*>();
  objects.prepend(root);
  for (auto* object : objects) {
    const auto numeric_entry = object->property("numericEntry").toBool();
    const auto numeric_stepper = object->property("toneSpinBoxContract").isValid();
    if (numeric_entry || numeric_stepper) {
      QObject* theme = nullptr;
      for (auto* ancestor = object; ancestor && !theme; ancestor = ancestor->parent())
        for (auto* child : ancestor->children())
          if (child->property("fontNumeric").isValid() && child->property("fontUi").isValid() &&
              child->property("numericFontFeatures").isValid()) {
            theme = child;
            break;
          }
      if (!theme) {
        errors.append(object->objectName() + ": numeric control is missing its shared theme");
      } else {
        const auto family = QQmlProperty(object, QStringLiteral("font.family")).read().toString();
        const auto font = qvariant_cast<QFont>(QQmlProperty(object, QStringLiteral("font")).read());
        if (family != theme->property("fontNumeric").toString() ||
            theme->property("fontNumeric").toString() != theme->property("fontUi").toString())
          errors.append(object->objectName() + ": numeric control must use the shared UI face");
        if (font.featureValue(QFont::Tag("tnum")) != 1 || font.featureValue(QFont::Tag("lnum")) != 1)
          errors.append(object->objectName() + ": numeric control must use tabular lining figures");
      }
    }
    if (object->property("preferenceNavRowContract").isValid()) {
      auto* background = qvariant_cast<QQuickItem*>(QQmlProperty(object, QStringLiteral("background")).read());
      auto* content = qvariant_cast<QQuickItem*>(QQmlProperty(object, QStringLiteral("contentItem")).read());
      if (!background || !content || background->z() >= content->z())
        errors.append("Preferences navigation background must stay behind its label");
    }
    if (object->property("choiceStyleContract").isValid()) {
      auto* indicator = qvariant_cast<QObject*>(QQmlProperty(object, QStringLiteral("indicator")).read());
      const auto scale = object->property("choiceScale").toDouble();
      const auto toggle = object->property("choiceKind").toString() == QStringLiteral("switch");
      const auto width = std::round((toggle ? 48.0 : 16.0) * scale);
      const auto height = std::round((toggle ? 26.0 : 16.0) * scale);
      if (!indicator) errors.append("Choice control has no shared indicator");
      else {
        const auto color = indicator->property("color").value<QColor>();
        if (std::abs(indicator->property("width").toDouble() - width) > 0.1 ||
            std::abs(indicator->property("height").toDouble() - height) > 0.1 ||
            std::abs(indicator->property("radius").toDouble() - (toggle ? height / 2 : std::round(3.0 * scale))) > 0.1 ||
            std::abs(QQmlProperty(indicator, QStringLiteral("border.width")).read().toDouble() - (object->property("choiceHighContrast").toBool() ? 1 : 0)) > 0.1 ||
            color.red() != color.green() || color.green() != color.blue())
          errors.append("Choice indicator geometry or neutral palette drifted");
      }
    }
    if (object->property("bodyTextStyleContract").isValid()) {
      if (QQmlProperty(object, QStringLiteral("font.pixelSize")).read().toInt() < object->property("bodyMinimumPixelSize").toInt() ||
          object->property("fontSizeMode").toInt() != 0)
        errors.append(QStringLiteral("Body text must retain the shared scaled type size: ") + object->property("text").toString());
      auto* text = qobject_cast<QQuickItem*>(object);
      if (text && text->window() && text->isVisible() && text->width() > 0 && text->height() > 0) {
        if (object->property("contentHeight").toDouble() > text->height() + 1 ||
            object->property("contentWidth").toDouble() > text->width() + 1)
          errors.append(QStringLiteral("Body text is clipped: ") + object->property("text").toString());
        auto* parent = text->parentItem();
        if (parent && !parent->boundingRect().adjusted(-1, -1, 1, 1).contains(text->mapRectToItem(parent, text->boundingRect())))
          errors.append(QStringLiteral("Body text escapes its layout: ") + object->property("text").toString());
      }
    }
    if (object->objectName() == QStringLiteral("sharedCommandLabel") || object->objectName() == QStringLiteral("sharedCommandCaption") || object->objectName() == QStringLiteral("sharedChoiceLabel") || object->objectName() == QStringLiteral("sharedChoiceCaption")) {
      auto* text = qobject_cast<QQuickItem*>(object);
      if (text && text->window() && text->isVisible() && text->width() > 0 && text->height() > 0) {
        QObject* text_theme = nullptr;
        for (auto* ancestor = text->parentItem(); ancestor && !text_theme; ancestor = ancestor->parentItem())
          for (auto* child : ancestor->children())
            if (child->property("typeControl").isValid() && child->property("interfaceScale").isValid()) { text_theme = child; break; }
        if (!text_theme) errors.append("Command label is missing its shared theme");
        else {
          const auto minimum = text_theme->property(object->objectName() == QStringLiteral("sharedChoiceCaption") ? "typeCaption" : object->objectName() == QStringLiteral("sharedCommandCaption") ? "typeMicro" : "typeControl").toInt();
          if (QQmlProperty(object, QStringLiteral("font.pixelSize")).read().toInt() < minimum || object->property("fontSizeMode").toInt() != 0)
            errors.append(QStringLiteral("Command label shrinks below the shared type scale: ") + object->property("text").toString());
          if (object->property("truncated").toBool() || object->property("contentWidth").toDouble() > text->width() + 1 || object->property("contentHeight").toDouble() > text->height() + 1) {
            QString owner;
            for (auto* ancestor = text->parentItem(); ancestor && owner.isEmpty(); ancestor = ancestor->parentItem()) owner = ancestor->objectName();
            errors.append(QStringLiteral("Command label is clipped: %1 (content %2x%3, box %4x%5, truncated=%6, owner=%7)")
                .arg(object->property("text").toString()).arg(object->property("contentWidth").toDouble())
                .arg(object->property("contentHeight").toDouble()).arg(text->width()).arg(text->height()).arg(object->property("truncated").toBool()).arg(owner));
          }
        }
      }
    }
    if (object->objectName() == QStringLiteral("sharedCommandIcon")) {
      auto* icon = qobject_cast<QQuickItem*>(object);
      if (icon && icon->window() && icon->isVisible() && std::abs(icon->width() - icon->height()) > 0.5)
        errors.append(QStringLiteral("Shared command icon is stretched: ") + object->property("name").toString());
    }
    if (object->property("sliderContract").isValid()) {
      const auto name = object->objectName();
      const auto scale = object->property("sliderScale").toDouble();
      const auto variant = object->property("styleVariant").toString();
      const auto compact_hud = variant == QStringLiteral("compact-hud");
      if (variant != QStringLiteral("standard") && !compact_hud) {
        errors.append(name + ": unknown inspector slider style variant");
      }
      if (compact_hud && (!object->property("forceCompact").toBool() ||
                          object->property("showResetButton").toBool() ||
                          object->property("valueInputEnabled").toBool())) {
        errors.append(name + ": compact HUD variant requires forced compact layout, no reset, and no typed entry");
      }
      const auto expected_value_width = std::round((compact_hud ? 48.0 : 80.0) * scale);
      if (std::abs(object->property("valueColumnWidth").toDouble() - expected_value_width) > 0.1 ||
          std::abs(object->property("resetColumnWidth").toDouble() - std::round(28.0 * scale)) > 0.1)
        errors.append(name + ": inspector value/reset columns drifted");
      auto* row = qobject_cast<QQuickItem*>(object);
      if (row && row->isVisible() && row->width() > 0 && row->height() > 0 && !name.isEmpty()) {
        auto* label = object->findChild<QQuickItem*>(name + QStringLiteral("Label"));
        auto* field = object->findChild<QQuickItem*>(name + QStringLiteral("ValueField"));
        if (field) {
          const auto available = field->width() - field->property("leftPadding").toDouble() - field->property("rightPadding").toDouble();
          if (field->property("contentWidth").toDouble() > available + 1)
            errors.append(name + ": inspector value is clipped");
          if (!row->boundingRect().adjusted(-1, -1, 1, 1).contains(field->mapRectToItem(row, field->boundingRect())))
            errors.append(name + ": inspector value is outside its row");
        }
        if (label && label->property("contentHeight").toDouble() > label->height() + 1)
          errors.append(name + ": inspector label is clipped");
      }
    }
    if (object->property("sectionContract").isValid()) {
      if (QQmlProperty(object, QStringLiteral("radius")).read().toDouble() != 0 ||
          QQmlProperty(object, QStringLiteral("border.width")).read().toDouble() != 0 ||
          object->property("color").value<QColor>().alpha() != 0)
        errors.append(object->objectName() + ": inspector section must remain flat");
    }
    const auto surface = object->property("popupSurfaceContract").isValid();
    const auto input = object->property("inputFieldSurfaceContract").isValid();
    const auto panel = object->property("studioPanelStyleContract").isValid();
    const auto command = object->property("commandSurfaceStyleContract").isValid();
    const auto popup = object->property("appDialogContract").isValid() ||
                       object->property("appPopupContract").isValid() ||
                       object->property("appMenuContract").isValid();
    if (!surface && !popup && !input && !panel && !command) continue;
    auto* surface_owner = surface || input || panel || command ? object : qvariant_cast<QObject*>(
        QQmlProperty(object, QStringLiteral("background")).read());
    QObject* theme = nullptr;
    const auto theme_candidates = surface_owner ? surface_owner->children() : QObjectList{};
    for (auto* child : theme_candidates)
      if (child->property("interfaceScale").isValid() && child->property("popupPadding").isValid()) {
        theme = child;
        break;
      }
    const auto label = QString::fromLatin1(object->metaObject()->className()) + "/" + object->objectName();
    if (!theme) {
      errors.append(label + ": no shared theme");
      continue;
    }
    const auto scale = theme->property("interfaceScale").toDouble();
    if (command) {
      const auto focus = object->property("focused").toBool();
      const auto expected_border = focus ? std::max(2.0, std::round(2.0 * scale)) : theme->property("highContrast").toBool() ? 1.0 : 0.0;
      if (std::abs(object->property("cornerRadius").toDouble() - std::round(3.0 * scale)) > 0.1 ||
          std::abs(object->property("borderWidth").toDouble() - expected_border) > 0.1)
        errors.append(label + ": command corners or outline drifted");
      const auto fill = object->property("fillColor").value<QColor>();
      const auto tone = object->property("tone").toString();
      if (fill.alpha() && tone != "danger" && tone != "warning" && tone != "success" &&
          (fill.red() != fill.green() || fill.green() != fill.blue()))
        errors.append(label + ": command fill is outside the neutral palette");
      continue;
    }
    if (panel) {
      const auto border = QQmlProperty(object, QStringLiteral("border.width")).read().toDouble();
      const auto expected = object->property("framed").toBool() && theme->property("highContrast").toBool() ? 1.0 : 0.0;
      const auto expected_fill = object->property("selected").toBool()
          ? theme->property("editorSelectionFill").value<QColor>() : QColor("#181818");
      const auto expected_radius = object->property("docked").toBool() ? 0.0 : std::round(4.0 * scale);
      if (std::abs(QQmlProperty(object, QStringLiteral("radius")).read().toDouble() - expected_radius) > 0.1 ||
          std::abs(border - expected) > 0.1 || object->property("color").value<QColor>() != expected_fill ||
          expected_fill.red() != expected_fill.green() || expected_fill.green() != expected_fill.blue())
        errors.append(label + ": panel geometry or neutral fill drifted");
      continue;
    }
    if (input) {
      const auto radius = QQmlProperty(object, QStringLiteral("radius")).read().toDouble();
      const auto border = QQmlProperty(object, QStringLiteral("border.width")).read().toDouble();
      const auto expected_border = object->property("focused").toBool()
          ? std::max(2.0, std::round(2.0 * scale)) : theme->property("highContrast").toBool() ? 1.0 : 0.0;
      const auto color = object->property("color").value<QColor>();
      if (std::abs(radius - std::round(3.0 * scale)) > 0.1) errors.append(label + ": input corner radius drifted");
      if (std::abs(border - expected_border) > 0.1) errors.append(label + ": input outline drifted");
      if (color.alpha() && (color.red() != color.green() || color.green() != color.blue()))
        errors.append(label + ": input neutral fill drifted");
      continue;
    }
    if (popup) {
      for (const auto* edge : {"leftPadding", "rightPadding", "topPadding", "bottomPadding"}) {
        const auto inset = object->property(edge).toDouble();
        if (inset < 0 || inset > std::round(16.0 * scale))
          errors.append(label + ": popup edge inset exceeds the style budget");
      }
      continue;
    }
    const auto expected_radius = object->property("docked").toBool() ? 0.0 : std::round(4.0 * scale);
    const auto expected_border = theme->property("highContrast").toBool() ? 1.0 : 0.0;
    const auto radius = QQmlProperty(object, QStringLiteral("radius")).read().toDouble();
    const auto border = QQmlProperty(object, QStringLiteral("border.width")).read().toDouble();
    if (std::abs(radius - expected_radius) > 0.1) errors.append(label + ": popup corner radius drifted");
    if (std::abs(border - expected_border) > 0.1) errors.append(label + ": popup resting outline drifted");
    if (object->property("color").value<QColor>() != QColor("#242424")) errors.append(label + ": popup neutral fill drifted");
  }
  return errors;
}
