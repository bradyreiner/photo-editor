#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <iostream>
#include <memory>

namespace {
void settle() { QTest::qWait(90); }

QVariant unpack(QVariant value) {
  return value.metaType() == QMetaType::fromType<QJSValue>()
             ? value.value<QJSValue>().toVariant()
             : value;
}

QQuickItem* findVisual(QQuickItem* root, const QString& name) {
  if (!root) return nullptr;
  if (root->objectName() == name) return root;
  for (auto* child : root->childItems())
    if (auto* match = findVisual(child, name)) return match;
  return nullptr;
}

void reveal(QQuickItem* item) {
  for (int pass = 0; pass < 3; ++pass) {
    for (auto* parent = item ? item->parentItem() : nullptr; parent;
         parent = parent->parentItem()) {
      if (!parent->property("contentY").isValid() ||
          !parent->property("contentHeight").isValid())
        continue;
      const auto rect = item->mapRectToItem(parent, item->boundingRect());
      const auto offset = rect.top() < 0
                              ? rect.top() - 8
                              : rect.bottom() > parent->height()
                                    ? rect.bottom() - parent->height() + 8
                                    : 0;
      const auto next = std::clamp(
          parent->property("contentY").toDouble() + offset, 0.0,
          std::max(0.0, parent->property("contentHeight").toDouble() - parent->height()));
      parent->setProperty("contentY", next);
      settle();
    }
  }
}

bool click(QQuickWindow* window, const QString& name) {
  auto* target = window ? findVisual(window->contentItem(), name) : nullptr;
  if (!target || !target->isVisible() || !target->isEnabled()) {
    std::cerr << "Unavailable control: " << name.toStdString() << '\n';
    return false;
  }
  reveal(target);
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    target->mapToScene(target->boundingRect().center()).toPoint());
  settle();
  return true;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

bool insidePanel(QQuickItem* panel, QQuickItem* item) {
  if (!panel || !item) return false;
  const auto rect = item->mapRectToItem(panel, item->boundingRect());
  return rect.left() >= -1 && rect.right() <= panel->width() + 1;
}

void resetScroll(QQuickWindow* window) {
  auto* scroll = findVisual(window ? window->contentItem() : nullptr,
                            "creativeToolsScroll");
  auto* flickable = scroll
                        ? qvariant_cast<QQuickItem*>(scroll->property("contentItem"))
                        : nullptr;
  if (flickable && flickable->property("contentY").isValid()) {
    flickable->setProperty("contentY", 0);
    settle();
  }
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName("LooksyUiTests");
  QCoreApplication::setApplicationName("TypographyPanel");
  QTemporaryDir preferences;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());

  QQmlEngine engine;
  QQmlComponent settingsComponent(&engine);
  settingsComponent.setData(R"qml(import QtQml
QtObject {
    property real interfaceScale: 1
    property string interfaceDensity: "balanced"
    property string systemUiFontFamily: ""
    property bool reducedMotion: true
    property bool highContrast: false
})qml",
                            QUrl());
  std::unique_ptr<QObject> settings(settingsComponent.create());
  if (!settings) {
    std::cerr << settingsComponent.errorString().toStdString() << '\n';
    return fail("Create isolated app settings");
  }
  settings->setProperty("systemUiFontFamily", app.font().family());
  engine.rootContext()->setContextProperty("appShell", settings.get());

  QStringList warnings;
  QObject::connect(&engine, &QQmlEngine::warnings,
                   [&](const QList<QQmlError>& errors) {
                     for (const auto& error : errors) warnings.append(error.toString());
                   });

  const auto fixturePath = QStringLiteral(PHOTO_EDITOR_SOURCE_DIR
                                          "/tests/photo-typography-ui-fixture.qml");
  QQmlComponent fixtureComponent(&engine, QUrl::fromLocalFile(fixturePath));
  if (fixtureComponent.isError()) {
    std::cerr << fixtureComponent.errorString().toStdString() << '\n';
    return fail("Load typography panel fixture");
  }
  auto createFixture = [&]() {
    std::unique_ptr<QObject> object(fixtureComponent.create());
    if (!object) std::cerr << fixtureComponent.errorString().toStdString() << '\n';
    return object;
  };
  auto fixture = createFixture();
  if (!fixture) return fail("Create typography panel fixture");
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* controller = fixture->findChild<QObject*>("typographyController");
  auto* panel = findVisual(window ? window->contentItem() : nullptr,
                           "photoTypographyPanelFixture");
  if (!window || !controller || !panel) return fail("Find fixture window, controller, and panel");
  settle();
  const auto proofDirectory = qEnvironmentVariable("PHOTO_TYPOGRAPHY_UI_PROOF_DIR");
  if (!proofDirectory.isEmpty() && !QDir().mkpath(proofDirectory))
    return fail("Create typography proof directory");
  auto capture = [&](const QString& fileName) {
    if (!proofDirectory.isEmpty() && QGuiApplication::platformName() == "cocoa" &&
        !window->isExposed())
      return false;
    QTest::mouseMove(window, QPoint(window->width() - 2, window->height() - 2));
    settle();
    return proofDirectory.isEmpty() || window->grabWindow().save(proofDirectory + "/" + fileName + ".png");
  };
  if (!capture(QStringLiteral("typography-normal")))
    return fail("Capture the normal typography panel");

  for (const auto& name : {QStringLiteral("photoTypographyFamilyPicker"),
                           QStringLiteral("photoTypography_size"),
                           QStringLiteral("photoTypographySizeDecrease"),
                           QStringLiteral("photoTypographySizeIncrease"),
                           QStringLiteral("photoLayerTextColorField"),
                           QStringLiteral("photoTypographyQuickColorPalette"),
                           QStringLiteral("photoTypographyFillOpacity"),
                           QStringLiteral("photoTypographyAlignmentRow"),
                           QStringLiteral("photoTypographySelectionScope")}) {
    auto* control = findVisual(panel, name);
    if (!control || !control->isVisible() || !insidePanel(panel, control))
      return fail("Keep font, size, fill, alignment, and selection scope visible in bounds");
  }
  auto* selectedOutlineField = findVisual(panel, "photoTypographySelectionStrokeColor");
  if (!selectedOutlineField || selectedOutlineField->property("text").toString() != "#ff336699")
    return fail("Selected-word outline controls display the selected run color");

  auto* sizeField = findVisual(panel, "photoTypography_size");
  if (!sizeField || !sizeField->setProperty("text", "56.5") ||
      !QMetaObject::invokeMethod(sizeField, "commitSizeEntry") ||
      controller->property("rangeStyleCalls").toInt() != 1 ||
      unpack(controller->property("lastChanges")).toMap().value("size").toDouble() != 56.5 ||
      controller->property("rangeStart").toInt() != 2 ||
      controller->property("rangeEnd").toInt() != 7 ||
      controller->property("rangeSnapshot").toString() != "A quiet portrait")
    return fail("Precise size entry applies to the captured selected characters");
  if (!click(window, "photoTypographySizeIncrease") ||
      controller->property("rangeStyleCalls").toInt() != 2 ||
      unpack(controller->property("lastChanges")).toMap().value("size").toDouble() != 57.5)
    return fail("Size stepper adjusts the selected text without losing fractional sizes");

  auto initialStyle = unpack(controller->property("style")).toMap();
  initialStyle.insert("color", "#80ffffff");
  controller->setProperty("style", initialStyle);
  if (!click(window, "photoTypographyQuickColor2") ||
      controller->property("rangeStyleCalls").toInt() != 3 ||
      unpack(controller->property("lastChanges")).toMap().value("color").toString() != "#80e65a52" ||
      controller->property("rangeSnapshot").toString() != "A quiet portrait" ||
      controller->property("coalescedBegins").toInt() != 1 ||
      controller->property("coalescedEnds").toInt() != 1)
    return fail("Quick color applies to the selected range, preserves alpha, and forms one undo action");

  if (!QMetaObject::invokeMethod(panel, "beginColorOpacityEdit") ||
      !QMetaObject::invokeMethod(panel, "updateColorOpacity", Q_ARG(QVariant, QVariant(0.35))))
    return fail("Begin a selected-range opacity edit");
  QTest::qWait(90);
  if (controller->property("rangeStyleCalls").toInt() != 4 ||
      unpack(controller->property("lastChanges")).toMap().value("color").toString() != "#59e65a52" ||
      !QMetaObject::invokeMethod(panel, "finishColorOpacityEdit", Q_ARG(QVariant, QVariant(true))) ||
      controller->property("coalescedBegins").toInt() != 2 ||
      controller->property("coalescedEnds").toInt() != 2)
    return fail("Opacity drag previews at a bounded rate and commits as one grouped edit");

  auto* canonicalTextEditor = fixture->findChild<QObject*>("canonicalTextEditor");
  if (!canonicalTextEditor) return fail("Find the canonical text editor mock");
  canonicalTextEditor->setProperty("editing", true);
  panel->setProperty("contentEditor", QVariant::fromValue(canonicalTextEditor));
  if (!click(window, "photoTypographyQuickColor2") ||
      controller->property("coalescedBegins").toInt() != 2 ||
      controller->property("coalescedCancels").toInt() != 0)
    return fail("No-op quick color inside an active text session cannot cancel its outer undo transaction");
  canonicalTextEditor->setProperty("editing", false);
  panel->setProperty("contentEditor", QVariant());

  if (!QMetaObject::invokeMethod(panel, "beginColorOpacityEdit") ||
      !QMetaObject::invokeMethod(panel, "updateColorOpacity", Q_ARG(QVariant, QVariant(0.2))))
    return fail("Begin an opacity edit before changing selection context");
  fixture->setProperty("selectionStart", 3);
  if (controller->property("coalescedCancels").toInt() != 1 ||
      controller->property("rangeStyleCalls").toInt() != 4)
    return fail("Changing the selected range cancels a pending opacity edit without applying it");
  fixture->setProperty("selectionStart", 2);

  fixture->setProperty("targetIndices", QVariantList{0, 1});
  fixture->setProperty("selectedLayerIds", QVariantList{"text-layer-1", "text-layer-2"});
  if (!QMetaObject::invokeMethod(panel, "beginColorOpacityEdit"))
    return fail("Begin a multi-target opacity edit");
  fixture->setProperty("selectedLayerIds", QVariantList{"text-layer-1", "replacement-layer"});
  if (controller->property("coalescedCancels").toInt() != 2 ||
      controller->property("rangeStyleCalls").toInt() != 4)
    return fail("Changing a selected layer identity aborts a delayed color edit");
  fixture->setProperty("selectedLayerIds", QVariantList{});
  fixture->setProperty("targetIndices", QVariantList{});

  if (!click(window, "photoTypographyEffectsToggle"))
    return fail("Open outline and shadow controls for the selected text");
  auto* effectStrokeField = findVisual(panel, "photoTypography_strokeColorField");
  auto* effectStrokeSwatch = findVisual(panel, "photoTypography_strokeColorSwatch");
  auto* effectShadowField = findVisual(panel, "photoTypography_shadowColorField");
  if (!effectStrokeField || effectStrokeField->property("text").toString() != "#ff336699" ||
      !effectStrokeSwatch || effectStrokeSwatch->property("swatchColor").toString() != "#ff336699" ||
      effectStrokeField->property("text").toString() !=
          effectStrokeSwatch->property("swatchColor").toString() ||
      !effectStrokeField->property("acceptableInput").toBool() ||
      !effectShadowField || effectShadowField->property("text").toString() != "#a0000000")
    return fail("Selected-word #AARRGGBB outline field and swatch agree; shadow stays layer scoped");

  if (!click(window, "photoLayerTextBoldButton") ||
      controller->property("rangeStyleCalls").toInt() != 5 ||
      controller->property("rangeStart").toInt() != 2 ||
      controller->property("rangeEnd").toInt() != 7 ||
      controller->property("rangeSnapshot").toString() != "A quiet portrait")
    return fail("Selected-word style edits preserve the requested range and text snapshot");

  if (!click(window, "photoTypographyWholeLayerScopeButton") ||
      panel->property("textSelectionActive").toBool())
    return fail("Switch typography scope to the whole text layer");
  if (!click(window, "photoLayerTextBoldButton") ||
      controller->property("singleStyleCalls").toInt() != 1)
    return fail("Whole-layer style edits use the existing controller path");

  fixture->setProperty("targetIndices", QVariantList{0, 1});
  settle();
  auto* pathToggle = findVisual(panel, "photoTypographyPathToggle");
  if (!findVisual(panel, "photoTypographyMultiTargetNotice") ||
      (pathToggle && pathToggle->isVisible()))
    return fail("Multi-target typography stays labeled and hides single-layer path editing");
  if (!click(window, "photoLayerTextBoldButton") ||
      controller->property("multiStyleCalls").toInt() != 1)
    return fail("Multi-target type changes preserve the multi-layer controller route");

  fixture->setProperty("targetIndices", QVariantList{});
  panel->setProperty("forceWholeLayerScope", false);
  panel->setProperty("spacingExpanded", false);
  panel->setProperty("effectsExpanded", false);
  panel->setProperty("pathExpanded", false);
  settle();
  if (!unpack(panel->property("favoritePresetNames")).toList().isEmpty())
    return fail("Use an empty favorites list in the isolated preferences fixture");
  if (!click(window, "photoTypographyFavoriteFilter") ||
      !panel->property("favoritePresetsOnly").toBool())
    return fail("Open the empty favorites filter");
  auto* favoriteStar = findVisual(panel, "photoTypographyStylePresetFavorite");
  auto* noFavorites = findVisual(panel, "photoTypographyNoFavoritesNotice");
  auto* favoriteFilter = findVisual(panel, "photoTypographyFavoriteFilter");
  if (!favoriteStar || favoriteStar->isEnabled() || !noFavorites || !noFavorites->isVisible() ||
      !favoriteFilter || favoriteFilter->property("text").toString() != QStringLiteral("Show all styles"))
    return fail("Empty favorites explain how to recover, and cannot star a hidden selection");
  if (!capture(QStringLiteral("typography-favorites-empty-initial")))
    return fail("Capture the actionable empty favorites state");
  if (!click(window, "photoTypographyFavoriteFilter"))
    return fail("Return to all styles from the empty favorites view");
  if (!click(window, "photoTypographyStylePresetFavorite") ||
      !panel->property("selectedPresetIsFavorite").toBool())
    return fail("Star the selected built-in type preset");
  panel->setProperty("selectedPresetIndex", 1);
  if (!click(window, "photoTypographyStylePresetFavorite") ||
      unpack(panel->property("favoritePresetNames")).toList().size() != 2)
    return fail("Save a second style favorite");
  if (!click(window, "photoTypographyFavoriteFilter") ||
      !panel->property("favoritePresetsOnly").toBool())
    return fail("Filter the preset selector to favorites");
  if (!capture(QStringLiteral("typography-favorites")))
    return fail("Capture the favorite style selector");
  auto favoriteIndices = unpack(panel->property("visibleStylePresetIndices")).toList();
  if (favoriteIndices.size() != 2 || favoriteIndices.first().toInt() != 0 ||
      favoriteIndices.last().toInt() != 1)
    return fail("Favorite filtering keeps both starred built-in styles discoverable");
  if (!click(window, "photoTypographyStylePresetFavorite") ||
      panel->property("selectedPresetIndex").toInt() != 0 ||
      unpack(panel->property("visibleStylePresetIndices")).toList().size() != 1)
    return fail("Removing a favorite selects the remaining saved style");
  if (!click(window, "photoTypographyStylePresetFavorite") ||
      !noFavorites->isVisible() || favoriteStar->isEnabled())
    return fail("Removing the last favorite reaches an explicit empty state");
  if (!capture(QStringLiteral("typography-favorites-empty-last")))
    return fail("Capture the empty state after removing the last favorite");
  if (!click(window, "photoTypographyFavoriteFilter") ||
      unpack(panel->property("visibleStylePresetIndices")).toList().size() < 10 ||
      favoriteStar->isEnabled() == false)
    return fail("Return to the complete built-in style list");
  if (!click(window, "photoTypographyStylePresetFavorite"))
    return fail("Save a style before checking favorite persistence");
  if (!click(window, "photoTypographyFavoriteFilter") ||
      !panel->property("favoritePresetsOnly").toBool())
    return fail("Favorite filter can be restored after browsing all styles");

  for (const auto& section : {
           qMakePair(QStringLiteral("photoTypographySpacingToggle"), QStringLiteral("spacingExpanded")),
           qMakePair(QStringLiteral("photoTypographyEffectsToggle"), QStringLiteral("effectsExpanded")),
           qMakePair(QStringLiteral("photoTypographyPathToggle"), QStringLiteral("pathExpanded"))}) {
    const auto& name = section.first;
    auto* button = findVisual(panel, name);
    auto* icon = findVisual(button, "sharedCommandIcon");
    if (!button || !button->isVisible() || !icon || !icon->isVisible())
      return fail("Show labeled icons for spacing, outline, and text path sections");
    if (!button->property("labelFitsControl").toBool() ||
        !button->property("contentFitsControl").toBool())
      return fail("Section labels fit their buttons");
    if (!click(window, name) || !panel->property(section.second.toUtf8().constData()).toBool())
      return fail("Open each typography section");
    const auto captureName = section.second == QStringLiteral("spacingExpanded")
        ? QStringLiteral("typography-spacing-expanded")
        : section.second == QStringLiteral("effectsExpanded")
              ? QStringLiteral("typography-outline-expanded")
              : QStringLiteral("typography-path-baseline-controls");
    if (section.second == QStringLiteral("spacingExpanded"))
      reveal(findVisual(panel, "photoTypography_baselineOffset"));
    else if (section.second == QStringLiteral("effectsExpanded"))
      reveal(findVisual(panel, "photoTypographyMaterial"));
    else if (section.second == QStringLiteral("pathExpanded")) {
      if (!click(window, "photoTextPathFromSelection") ||
          controller->property("pathCalls").toInt() != 1)
        return fail("Attach text to the mock selection contour");
      for (const auto& pathControl : {QStringLiteral("photoTextPathSide"),
                                      QStringLiteral("photoTextPathStart"),
                                      QStringLiteral("photoTextPathEnd"),
                                      QStringLiteral("photoTextPathOffset"),
                                      QStringLiteral("photoTextPathSpacing")}) {
        auto* control = findVisual(panel, pathControl);
        if (!control || !control->isVisible() || !insidePanel(panel, control))
          return fail("Show populated text path side, range, and spacing controls");
      }
      if (!click(window, "photoTextPathHandleEditButton") ||
          !panel->property("baselineEditing").toBool() ||
          !fixture->property("pathHandlesActive").toBool())
        return fail("Baseline edit toggle reaches the canvas overlay signal");
      reveal(findVisual(panel, "photoTextPathSpacing"));
    }
    if (!capture(captureName)) return fail("Capture an expanded typography section");
  }

  if (panel->property("spacingExpanded").toBool() ||
      panel->property("effectsExpanded").toBool() ||
      !panel->property("pathExpanded").toBool())
    return fail("Advanced sections keep their one-at-a-time disclosure behavior");

  fixture.reset();
  settle();
  QSettings persisted;
  persisted.sync();
  fixture = createFixture();
  if (!fixture) return fail("Reopen typography panel fixture");
  window = qobject_cast<QQuickWindow*>(fixture.get());
  controller = fixture->findChild<QObject*>("typographyController");
  panel = findVisual(window ? window->contentItem() : nullptr, "photoTypographyPanelFixture");
  if (!window || !controller || !panel) return fail("Find reopened typography panel");
  settle();
  if (panel->property("spacingExpanded").toBool() ||
      panel->property("effectsExpanded").toBool() ||
      !panel->property("pathExpanded").toBool() ||
      !panel->property("favoritePresetsOnly").toBool() ||
      !panel->property("selectedPresetIsFavorite").toBool())
    return fail("Favorites and advanced disclosure states persist across panel recreation");

  if (!click(window, "photoTextPathFromSelection") ||
      controller->property("pathCalls").toInt() != 1)
    return fail("Restore a populated path for the responsive matrix");
  if (panel->property("selectedPresetIsFavorite").toBool() &&
      !click(window, "photoTypographyStylePresetFavorite"))
    return fail("Restore the isolated fixture favorite state");
  panel->setProperty("favoritePresetsOnly", false);
  panel->setProperty("spacingExpanded", false);
  panel->setProperty("effectsExpanded", false);
  panel->setProperty("pathExpanded", false);
  panel->setProperty("stylePresetsExpanded", true);
  resetScroll(window);
  if (!capture(QStringLiteral("typography-normal")))
    return fail("Capture normal panel after restoring isolated preferences");

  for (double scale : {1.0, 2.0}) {
    settings->setProperty("interfaceScale", scale);
    for (const auto& density : {QStringLiteral("compact"), QStringLiteral("balanced"),
                                QStringLiteral("comfortable")}) {
      settings->setProperty("interfaceDensity", density);
      for (int width : {260, 340}) {
        window->setWidth(qRound(width * scale));
        window->setHeight(qRound(820 * scale));
        for (const auto& section : {QStringLiteral("spacingExpanded"),
                                    QStringLiteral("effectsExpanded"),
                                    QStringLiteral("pathExpanded")}) {
          panel->setProperty("spacingExpanded", section == "spacingExpanded");
          panel->setProperty("effectsExpanded", section == "effectsExpanded");
          panel->setProperty("pathExpanded", section == "pathExpanded");
          settle();
          const auto activeControls = section == "spacingExpanded"
              ? QStringLiteral("photoTypographySpacingControls")
              : section == "effectsExpanded" ? QStringLiteral("photoTypographyEffectsControls")
                                               : QStringLiteral("photoTypographyPathControls");
          for (const auto& name : {QStringLiteral("photoTypographyFamilyPicker"),
                                   QStringLiteral("photoTypography_size"),
                                   QStringLiteral("photoLayerTextColorField"),
                                   QStringLiteral("photoTypographyAlignmentRow"),
                                   QStringLiteral("photoTypographyAdvancedNavigation"),
                                   activeControls}) {
            auto* control = findVisual(panel, name);
            if (!control || !control->isVisible() || !insidePanel(panel, control))
              return fail("Typography controls remain visible and inside the panel at narrow widths and scales");
          }
          for (const auto& name : {QStringLiteral("photoTypographySpacingToggle"),
                                   QStringLiteral("photoTypographyEffectsToggle"),
                                   QStringLiteral("photoTypographyPathToggle"),
                                   QStringLiteral("photoTypographyFavoriteFilter")}) {
            auto* button = findVisual(panel, name);
            if (!button || !button->isVisible() ||
                !button->property("labelFitsControl").toBool() ||
                !button->property("contentFitsControl").toBool())
              return fail("Wrapped section and favorite labels fit at every scale and density");
          }
          for (auto* item : panel->findChildren<QQuickItem*>()) {
            if (!item->isVisible() || item->width() <= 0 || item->height() <= 0) continue;
            const auto name = item->objectName();
            const bool typographyControl = name.startsWith("photoTypography_") ||
                                           name.startsWith("photoLayerText") ||
                                           name.startsWith("photoTextPath") ||
                                           name == "photoTypographyAdvancedNavigation";
            if (typographyControl && !insidePanel(panel, item))
              return fail("A visible typography control overflows the panel width");
          }
          if (scale == 1.0 && density == QStringLiteral("balanced") && width == 260 &&
              section == QStringLiteral("pathExpanded")) {
            resetScroll(window);
            if (!capture(QStringLiteral("typography-narrow-260")))
              return fail("Capture the narrow typography panel");
          }
          if (scale == 2.0 && density == QStringLiteral("comfortable") && width == 340 &&
              section == QStringLiteral("effectsExpanded")) {
            reveal(findVisual(panel, "photoTypographyMaterial"));
            if (!capture(QStringLiteral("typography-scale2-comfortable-expanded")))
              return fail("Capture the 2x comfortable expanded panel");
          }
        }
      }
    }
  }

  settings->setProperty("interfaceScale", 1.0);
  settings->setProperty("interfaceDensity", "balanced");
  window->setWidth(340);
  window->setHeight(820);
  fixture->setProperty("selectionStart", -1);
  fixture->setProperty("selectionEnd", -1);
  auto keyboardStyle = unpack(controller->property("style")).toMap();
  keyboardStyle.insert("color", "#80e65a52");
  controller->setProperty("style", keyboardStyle);
  settle();
  auto* keyboardOpacity = findVisual(panel, "photoTypographyFillOpacity");
  if (!keyboardOpacity) return fail("Find the keyboard-accessible text opacity control");
  reveal(keyboardOpacity);
  keyboardOpacity->forceActiveFocus();
  const auto styleCallsBeforeKey = controller->property("singleStyleCalls").toInt();
  QTest::keyClick(window, Qt::Key_Right);
  settle();
  const auto keyboardColor = QColor(unpack(controller->property("lastChanges")).toMap().value("color").toString());
  if (controller->property("singleStyleCalls").toInt() <= styleCallsBeforeKey ||
      !keyboardColor.isValid() || keyboardColor.alpha() <= 128)
    return fail("Arrow-key opacity edits publish the new text alpha");

  if (!warnings.isEmpty()) {
    for (const auto& warning : warnings) std::cerr << warning.toStdString() << '\n';
    return fail("QML emitted warnings while loading the typography panel fixture");
  }
  return 0;
}
