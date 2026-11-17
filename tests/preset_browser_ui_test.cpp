#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QLinearGradient>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QVariant>
#include <iostream>
#include <memory>

namespace {
void settle(int delay = 120) { QTest::qWait(delay); }

QQuickItem* findVisual(QQuickItem* root, const QString& name) {
  if (!root) return nullptr;
  if (root->objectName() == name) return root;
  for (auto* child : root->childItems()) {
    if (auto* match = findVisual(child, name)) return match;
  }
  return nullptr;
}

QQuickItem* listItemAt(QQuickItem* list, int index) {
  if (!list) return nullptr;
  QQuickItem* result = nullptr;
  if (QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, result), Q_ARG(int, index)))
    return result;
  return nullptr;
}

bool click(QQuickWindow* window, QObject* object) {
  auto* item = qobject_cast<QQuickItem*>(object);
  if (!item || !item->isVisible() || !item->isEnabled()) return false;
  const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
  settle();
  return true;
}

void reveal(QQuickWindow* window, QQuickItem* item) {
  if (!window || !item) return;
  auto* scroll = findVisual(window->contentItem(), QStringLiteral("creativeToolsScroll"));
  auto* content = findVisual(window->contentItem(), QStringLiteral("creativeToolsContent"));
  if (!scroll || !content) return;

  QObject* flickable = qvariant_cast<QObject*>(scroll->property("contentItem"));
  if (!flickable) flickable = scroll->findChild<QObject*>(QStringLiteral("contentItem"));
  if (!flickable || flickable->metaObject()->indexOfProperty("contentY") < 0) return;

  const auto local = item->mapToItem(content, QPointF(0, 0));
  const qreal currentY = flickable->property("contentY").toReal();
  const qreal viewport = scroll->height();
  qreal nextY = currentY;
  if (local.y() < currentY)
    nextY = local.y() - 12;
  else if (local.y() + item->height() > currentY + viewport)
    nextY = local.y() + item->height() - viewport + 12;
  flickable->setProperty("contentY", qMax<qreal>(0, nextY));
  settle();
}

void createSample(const QString& path, bool original) {
  QImage image(640, 400, QImage::Format_ARGB32_Premultiplied);
  QPainter painter(&image);
  QLinearGradient sky(0, 0, 640, 400);
  if (original) {
    sky.setColorAt(0.0, QColor("#457b9d"));
    sky.setColorAt(1.0, QColor("#f4a261"));
  } else {
    sky.setColorAt(0.0, QColor("#282f64"));
    sky.setColorAt(1.0, QColor("#d85f67"));
  }
  painter.fillRect(image.rect(), sky);
  painter.setPen(Qt::NoPen);
  painter.setBrush(original ? QColor("#346d53") : QColor("#2b5548"));
  painter.drawEllipse(QRectF(365, 120, 145, 145));
  painter.setBrush(original ? QColor("#244e5c") : QColor("#343b62"));
  painter.drawPolygon(QPolygonF{QPointF(0, 400), QPointF(160, 190), QPointF(295, 400)});
  painter.setBrush(original ? QColor("#1b3b4b") : QColor("#222c4b"));
  painter.drawPolygon(QPolygonF{QPointF(190, 400), QPointF(430, 175), QPointF(640, 400)});
  painter.end();
  image.save(path, "PNG");
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName("LooksyUiTests");
  QCoreApplication::setApplicationName("PresetBrowser");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QTemporaryDir preferences;
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
  QTemporaryDir samples;
  if (!samples.isValid()) return fail("Create preset browser sample directory");
  createSample(samples.filePath(QStringLiteral("processed.png")), false);
  createSample(samples.filePath(QStringLiteral("original.png")), true);

  QQmlEngine engine;
  QQmlComponent settingsComponent(&engine);
  settingsComponent.setData(R"qml(import QtQml
QtObject {
 property real interfaceScale: 1
 property string interfaceDensity: "balanced"
 property string systemUiFontFamily: ""
 property bool reducedMotion: true
 property bool highContrast: false
 property string colorVisionMode: "standard"
})qml", QUrl());
  std::unique_ptr<QObject> settings(settingsComponent.create());
  if (!settings) return fail("Create isolated application settings mock");
  settings->setProperty("systemUiFontFamily", app.font().family());
  engine.rootContext()->setContextProperty("appShell", settings.get());
  engine.rootContext()->setContextProperty("sampleProcessedUrl",
      QUrl::fromLocalFile(samples.filePath(QStringLiteral("processed.png"))));
  engine.rootContext()->setContextProperty("sampleOriginalUrl",
      QUrl::fromLocalFile(samples.filePath(QStringLiteral("original.png"))));

  QStringList warnings;
  QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError>& errors) {
    for (const auto& error : errors) warnings.append(error.toString());
  });

  QQmlComponent fixtureComponent(&engine);
  fixtureComponent.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
Window {
 id: window
 objectName: "presetBrowserFixtureWindow"
 visible: true
 width: 340
 height: 820
 color: "#17191d"
 property string currentSelection: "Golden Hour"
 property string previewName: ""
 property bool previewPinned: false
 property string search: ""
 property string category: "All"
 property string pack: "All"
 property string tab: "All"
 property bool favoritesOnly: false
 property string quickSelectAnchorName: ""
 property real intensity: 0.72
 property real previewRecipeIntensity: 0.72
 property bool previewRequestsEnabled: false
 property var lookupCounters: ({ thumbnail: 0, original: 0 })
 property int previewCount: 0
 property int clearCount: 0
 property int applyCount: 0
 property int createCount: 0
 property var allRows: [
  {kind:"pack", label:"Looksy · Landscape", count:2},
  {kind:"preset", name:"Golden Hour"},
  {kind:"preset", name:"Blue Hour"},
  {kind:"pack", label:"Looksy · Portrait", count:1},
  {kind:"preset", name:"Soft Portrait"}
 ]
 property var visibleRows: allRows
 QtObject {
  id: mock
  objectName: "mockPresetController"
  property var presetNames: ["Golden Hour", "Blue Hour", "Soft Portrait"]
  property var favorites: []
  function presetExampleThumbnailSource(name) { window.lookupCounters.thumbnail++; return sampleProcessedUrl }
  function exampleThumbnailOriginalSource() { window.lookupCounters.original++; return sampleOriginalUrl }
  function isPresetFavorite(name) { return favorites.indexOf(name) >= 0 }
  function isUserPreset(name) { return name === "Soft Portrait" }
  function presetCategoryName(name) { return name === "Soft Portrait" ? "Portrait" : "Landscape" }
  function presetPackName(name) { return name === "Soft Portrait" ? "User Presets" : "Landscape Core" }
 }
 ModularToolPanel {
  id: panel
  objectName: "presetBrowserPanel"
  anchors.fill: parent
  active: true
  title: "Presets"
  targetText: "Base Image · Entire photo"
  contextKey: "preset-browser-proof"
 }
 PresetBrowser {
  id: browser
  objectName: "studioPresetBrowser"
  parent: panel.contentItem
  Layout.fillWidth: true
  controller: mock
  previewRequestsEnabled: window.previewRequestsEnabled
  presetRows: window.visibleRows
  presetNames: ["Golden Hour", "Blue Hour", "Soft Portrait"]
  categoryFilters: ["All", "Landscape", "Portrait"]
  packFilters: ["All", "Landscape Core", "User Presets"]
  selectedPresetName: window.currentSelection
  selectedMetaText: window.currentSelection === "Soft Portrait" ? "User Presets / Portrait" : "Landscape Core / Landscape"
  targetSummary: "Base Image · Entire photo"
  selectedTab: window.tab
  favoritesOnly: window.favoritesOnly
  searchText: window.search
  selectedCategory: window.category
  selectedPack: window.pack
  previewedPresetName: window.previewName
  hasAsset: true
  intensity: window.intensity
  changedControls: ["Color", "Tone", "Contrast"]
  onCreateRequested: window.createCount++
  onSearchEdited: function(value) { window.search = value }
  onTabSelected: function(value) {
   window.tab = value
   if (value === "Favorites")
    window.visibleRows = [{kind:"pack",label:"Favorites",count:1},{kind:"preset",name:"Golden Hour"}]
   else if (value === "My Presets")
    window.visibleRows = [{kind:"pack",label:"User Presets",count:1},{kind:"preset",name:"Soft Portrait"}]
   else
    window.visibleRows = window.allRows
  }
  onFavoritesOnlyToggled: window.favoritesOnly = !window.favoritesOnly
  onCategorySelected: function(value) { window.category = value }
  onPackSelected: function(value) { window.pack = value }
  onPresetSelected: function(name) { window.currentSelection = name }
  onPresetPreviewRequested: function(name) {
   if (!window.previewPinned) {
    window.previewName = name
    window.previewRecipeIntensity = window.intensity
    window.previewCount++
   }
  }
  onSelectedPreviewRequested: function(name) {
   window.previewPinned = true
   window.previewName = name
   window.previewRecipeIntensity = window.intensity
   window.previewCount++
  }
  onPresetHoverPreviewCleared: {
   if (!window.previewPinned) { window.previewName = ""; window.clearCount++ }
  }
  onPresetPreviewCleared: { window.previewPinned = false; window.previewName = ""; window.clearCount++ }
  onFavoriteToggled: function(name) {
   let next = mock.favorites.slice()
   const index = next.indexOf(name)
   if (index >= 0) next.splice(index, 1); else next.push(name)
   mock.favorites = next
  }
  onQuickSelectRequested: function(anchor) {
   window.quickSelectAnchorName = anchor.objectName
   window.currentSelection = "Blue Hour"
  }
  onApplyRequested: { window.previewPinned = false; window.previewName = ""; window.applyCount++ }
  onIntensityEdited: function(value) {
   window.intensity = value
   if (window.previewPinned) window.previewRecipeIntensity = value
  }
 }
}
)qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/preset-browser-ui-fixture.qml")));
  std::unique_ptr<QObject> fixture(fixtureComponent.create());
  if (!fixture) {
    std::cerr << fixtureComponent.errorString().toStdString() << '\n';
    return 1;
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  if (!window) return fail("Create Qt preset browser fixture window");
  settle(400);
  if (qEnvironmentVariable("QT_QPA_PLATFORM") == QStringLiteral("cocoa") && !window->isExposed())
    return fail("Native preset browser fixture window is not exposed");

  const auto output = qEnvironmentVariable("PRESET_BROWSER_UI_PROOF_DIR");
  if (!output.isEmpty()) QDir().mkpath(output);
  auto capture = [&](const QString& name) {
    if (output.isEmpty()) return;
    const auto image = window->grabWindow();
    if (!image.isNull()) image.save(output + "/" + name + ".png");
  };

  auto* browser = fixture->findChild<QObject*>("studioPresetBrowser");
  const auto lookupCount = [&fixture](const QString& key) {
    const auto counters = fixture->property("lookupCounters").value<QJSValue>();
    return counters.property(key).toInt();
  };
  auto* list = fixture->findChild<QQuickItem*>("studioPresetLibraryList");
  if (!browser || !list) return fail("Find production preset browser and list");
  if (list->property("count").toInt() != 0)
    return fail("Hidden preset browser does not instantiate rows");
  if (lookupCount(QStringLiteral("thumbnail")) != 0 ||
      lookupCount(QStringLiteral("original")) != 0)
    return fail("Hidden preset examples defer both thumbnail source lookups");
  fixture->setProperty("previewRequestsEnabled", true);
  settle(200);
  if (lookupCount(QStringLiteral("thumbnail")) <= 0 ||
      lookupCount(QStringLiteral("original")) <= 0)
    return fail("Visible preset examples request fresh processed and original thumbnails");
  auto* firstRow = listItemAt(list, 1);
  auto* firstCard = findVisual(firstRow, QStringLiteral("studioPresetLibraryCard"));
  auto* thumb = findVisual(firstRow, QStringLiteral("studioPresetExampleCardThumbnail"));
  if (!firstCard || !thumb) return fail("Visible preset browser instantiates sample cards");
  if (list->height() < 250 || firstCard->height() < 120 || list->width() > window->width())
    return fail("Preset sample cards use a roomy, bounded virtualized list");
  settle(500);
  if (thumb->property("status").toInt() != 1)
    return fail("Preset sample thumbnail loads from the mock example source");
  if (browser->property("showOriginalSamples").toBool())
    return fail("Preset browser starts with processed examples");
  auto* categoryCombo = fixture->findChild<QQuickItem*>("studioPresetCategoryFilterCombo");
  auto* packCombo = fixture->findChild<QQuickItem*>("studioPresetPackFilterCombo");
  if (!categoryCombo || !packCombo || categoryCombo->property("displayText").toString() != QStringLiteral("All categories") ||
      packCombo->property("displayText").toString() != QStringLiteral("All packs"))
    return fail("Category and pack selectors have distinct visible labels");
  capture(QStringLiteral("normal"));
  categoryCombo->forceActiveFocus(Qt::TabFocusReason);
  QTest::keyClick(window, Qt::Key_Space);
  settle();
  QTest::keyClick(window, Qt::Key_Down);
  QTest::keyClick(window, Qt::Key_Return);
  settle();
  if (fixture->property("category").toString() != QStringLiteral("Landscape"))
    return fail("Category selector forwards its original filter value");
  fixture->setProperty("category", QStringLiteral("All"));
  packCombo->forceActiveFocus(Qt::TabFocusReason);
  QTest::keyClick(window, Qt::Key_Space);
  settle();
  QTest::keyClick(window, Qt::Key_Down);
  QTest::keyClick(window, Qt::Key_Return);
  settle();
  if (fixture->property("pack").toString() != QStringLiteral("Landscape Core"))
    return fail("Pack selector forwards its original filter value");
  fixture->setProperty("pack", QStringLiteral("All"));
  QTest::mouseMove(window, QPoint(2, 2));
  window->contentItem()->forceActiveFocus(Qt::OtherFocusReason);
  settle();
  window->setWidth(260);
  settle();
  capture(QStringLiteral("narrow"));
  window->setWidth(340);
  settle();

  auto* favorite = findVisual(firstRow, QStringLiteral("studioPresetFavoriteButton"));
  if (!favorite || !favorite->property("square").toBool() || !click(window, favorite))
    return fail("Favorite button is visible and interactive");
  auto* mock = fixture->findChild<QObject*>("mockPresetController");
  if (!mock || !mock->property("favorites").toStringList().contains(QStringLiteral("Golden Hour")))
    return fail("Preset favorite updates controller state");
  auto* favoritesFilter = fixture->findChild<QQuickItem*>("studioPresetFavoritesFilterButton");
  if (!favoritesFilter || !click(window, favoritesFilter) || !fixture->property("favoritesOnly").toBool())
    return fail("Favorites-only filter forwards its toggle");

  if (!click(window, firstCard)) return fail("Select a preset example card");
  if (fixture->property("currentSelection").toString() != QStringLiteral("Golden Hour"))
    return fail("Selecting a card changes the chosen preset");
  auto* preview = fixture->findChild<QQuickItem*>("studioPresetPreviewSelectedButton");
  auto* apply = fixture->findChild<QQuickItem*>("studioApplyPresetButton");
  if (!preview || !apply) return fail("Find selected preset actions");
  QTest::mouseMove(window, QPoint(2, 2));
  settle();
  reveal(window, preview);
  const bool previewClicked = click(window, preview);
  if (!previewClicked || fixture->property("previewName").toString() != QStringLiteral("Golden Hour"))
    return fail("Preview action shows the selected sample look without applying it");
  if (fixture->property("applyCount").toInt() != 0 || preview->property("text").toString() != QStringLiteral("Clear preview"))
    return fail("Preview and Apply remain separate actions");
  const auto otherCardPoint = firstCard->mapToScene(QPointF(firstCard->width() * 0.55, firstCard->height() * 0.55)).toPoint();
  QTest::mouseMove(window, otherCardPoint);
  settle();
  QTest::mouseMove(window, QPoint(2, 2));
  settle();
  if (fixture->property("previewName").toString() != QStringLiteral("Golden Hour") ||
      !fixture->property("previewPinned").toBool())
    return fail("Explicit Preview stays active when the pointer leaves its card");
  if (!click(window, preview) || !fixture->property("previewName").toString().isEmpty())
    return fail("Preview can be cleared");

  auto* original = fixture->findChild<QQuickItem*>("studioPresetOriginalSampleButton");
  if (!original || !click(window, original) || !browser->property("showOriginalSamples").toBool())
    return fail("Original sample preference toggles");
  auto* cardObject = findVisual(firstRow, QStringLiteral("studioPresetExampleCard"));
  if (!cardObject || cardObject->property("displayedSource").toUrl() !=
      QUrl::fromLocalFile(samples.filePath(QStringLiteral("original.png"))))
    return fail("Original sample view displays the reference thumbnail");
  capture(QStringLiteral("original-sample"));
  if (!click(window, original)) return fail("Restore processed sample thumbnails");

  auto* size = fixture->findChild<QQuickItem*>("studioPresetThumbnailSizeButton");
  if (!size || !click(window, size) || !browser->property("compactSamples").toBool())
    return fail("Sample size preference toggles to compact");
  const qreal compactCardHeight = firstCard->height();
  if (!click(window, size) || browser->property("compactSamples").toBool() || firstCard->height() <= compactCardHeight)
    return fail("Sample size preference restores large cards");
  QTest::mouseMove(window, QPoint(2, 2));
  window->contentItem()->forceActiveFocus(Qt::OtherFocusReason);
  settle();
  capture(QStringLiteral("large-samples"));

  auto* search = fixture->findChild<QQuickItem*>("studioPresetSearchField");
  if (!search || !click(window, search)) return fail("Focus preset search");
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  for (const auto key : {Qt::Key_P, Qt::Key_O, Qt::Key_R, Qt::Key_T,
                         Qt::Key_R, Qt::Key_A, Qt::Key_I, Qt::Key_T})
    QTest::keyClick(window, key);
  settle();
  if (fixture->property("search").toString() != QStringLiteral("portrait"))
    return fail("Search field forwards user text");
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  QTest::keyClick(window, Qt::Key_Backspace);
  settle();
  if (!fixture->property("search").toString().isEmpty())
    return fail("Search field can be cleared before changing library tabs");
  auto* myPresetTab = fixture->findChild<QQuickItem*>("studioPresetLibraryTabs");
  if (myPresetTab) {
    for (auto* child : myPresetTab->childItems()) {
      if (child->property("text").toString() == QStringLiteral("Favorites")) {
        reveal(window, child);
        if (!click(window, child)) return fail("Open Favorites tab");
        break;
      }
    }
  }
  if (fixture->property("tab").toString() != QStringLiteral("Favorites"))
    return fail("Favorites tab forwards selection");
  QTest::mouseMove(window, QPoint(2, 2));
  window->contentItem()->forceActiveFocus(Qt::OtherFocusReason);
  settle();
  capture(QStringLiteral("favorites"));
  if (myPresetTab) {
    for (auto* child : myPresetTab->childItems()) {
      if (child->property("text").toString() == QStringLiteral("My Presets")) {
        reveal(window, child);
        if (!click(window, child)) return fail("Open My Presets tab");
        break;
      }
    }
  }
  if (fixture->property("tab").toString() != QStringLiteral("My Presets"))
    return fail("My Presets tab forwards selection");

  auto* create = fixture->findChild<QQuickItem*>("studioCreatePresetButton");
  if (!create || !click(window, create) || fixture->property("createCount").toInt() != 1)
    return fail("Create preset action is preserved");

  auto* browse = fixture->findChild<QQuickItem*>("studioPresetQuickSelect");
  if (!browse || !click(window, browse) || fixture->property("quickSelectAnchorName").toString() != QStringLiteral("studioPresetQuickSelect") ||
      fixture->property("currentSelection").toString() != QStringLiteral("Blue Hour"))
    return fail("Quick-select forwards its real popup anchor and chosen preset");
  fixture->setProperty("currentSelection", QStringLiteral("Golden Hour"));

  if (!click(window, preview) || !fixture->property("previewPinned").toBool())
    return fail("Explicit preview can be pinned again before Apply");

  auto* adjustAmount = fixture->findChild<QQuickItem*>("studioPresetAdjustAmountDisclosure");
  if (!adjustAmount || browser->property("amountControlsExpanded").toBool() || !click(window, adjustAmount) ||
      !browser->property("amountControlsExpanded").toBool())
    return fail("Intensity and changed-control details use a clear, persistent disclosure");
  QTest::mouseMove(window, QPoint(2, 2));
  settle();
  capture(QStringLiteral("amount-expanded"));

  auto* slider = fixture->findChild<QQuickItem*>("studioPresetIntensitySlider");
  if (!slider || !slider->isVisible()) return fail("Find expanded preset intensity control");
  reveal(window, slider);
  const QPoint sliderPoint = slider->mapToScene(QPointF(slider->width() * 0.8, slider->height() / 2)).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, sliderPoint);
  settle();
  if (fixture->property("intensity").toDouble() < 0.72 ||
      !fixture->property("previewPinned").toBool() ||
      fixture->property("previewRecipeIntensity").toDouble() != fixture->property("intensity").toDouble()) {
    return fail("Intensity slider forwards user edits");
  }
  reveal(window, apply);
  if (!click(window, apply) || fixture->property("applyCount").toInt() != 1 ||
      !fixture->property("previewName").toString().isEmpty() || fixture->property("previewPinned").toBool())
    return fail("Apply commits the selected preset through its callback");

  browser->setProperty("canApply", false);
  browser->setProperty("applyDisabledReason", QStringLiteral("Choose the Base Image or an adjustment layer to apply this look."));
  settle();
  apply = fixture->findChild<QQuickItem*>("studioApplyPresetButton");
  auto* restriction = fixture->findChild<QQuickItem*>("studioPresetTargetRestriction");
  if (!apply || apply->isEnabled() || !restriction || !restriction->isVisible())
    return fail("Unsupported preset targets show the reason and block Apply");
  browser->setProperty("canApply", true);
  browser->setProperty("applyDisabledReason", QString());
  adjustAmount = fixture->findChild<QQuickItem*>("studioPresetAdjustAmountDisclosure");
  if (browser->property("amountControlsExpanded").toBool() && (!adjustAmount || !click(window, adjustAmount)))
    return fail("Collapse amount details before checking the primary action layout");

  auto* scroll = fixture->findChild<QQuickItem*>("creativeToolsScroll");
  for (const auto width : {260, 340}) {
    window->setWidth(width);
    for (const auto scale : {1.0, 2.0}) {
      settings->setProperty("interfaceScale", scale);
      for (const auto& density : {QStringLiteral("compact"), QStringLiteral("balanced"), QStringLiteral("comfortable")}) {
        settings->setProperty("interfaceDensity", density);
        settle(80);
        auto* rootItem = qobject_cast<QQuickItem*>(browser);
        auto* card = findVisual(listItemAt(list, 1), QStringLiteral("studioPresetExampleCard"));
        if (!rootItem || !card || rootItem->width() > window->width() + 1 || card->width() > window->width() + 1 ||
            rootItem->x() < -1 || rootItem->x() + rootItem->width() > window->width() + 1)
          return fail("Preset browser and sample cards remain within narrow and wide layout bounds");
        if (!scroll || !apply) return fail("Find sidebar scroll and selected Apply action");
        QObject* flickable = qvariant_cast<QObject*>(scroll->property("contentItem"));
        if (flickable && flickable->metaObject()->indexOfProperty("contentY") >= 0)
          flickable->setProperty("contentY", 0);
        settle(40);
        const qreal scrollTop = scroll->mapToScene(QPointF(0, 0)).y();
        const qreal applyTop = apply->mapToScene(QPointF(0, 0)).y();
        if (applyTop < scrollTop - 1 || applyTop + apply->height() > scrollTop + scroll->height() + 1) {
          std::cerr << "apply visibility width=" << width << " scale=" << scale
                    << " density=" << density.toStdString() << " y=" << applyTop
                    << " h=" << apply->height() << " viewport=" << scrollTop << "+" << scroll->height() << '\n';
          for (const auto& name : {QStringLiteral("studioPresetBrowser"), QStringLiteral("studioCreatePresetButton"),
                                   QStringLiteral("presetBrowserContents"),
                                   QStringLiteral("studioPresetSelectedDetail"), QStringLiteral("studioPresetLibraryTabs"),
                                   QStringLiteral("studioPresetSearchField"), QStringLiteral("studioPresetCategoryFilterRow"),
                                   QStringLiteral("studioPresetResultCount"), QStringLiteral("studioPresetOriginalSampleButton")}) {
            auto* control = fixture->findChild<QQuickItem*>(name.toUtf8().constData());
            if (control) std::cerr << name.toStdString() << " y=" << control->mapToScene(QPointF(0,0)).y()
                                   << " local=" << control->y()
                                   << " h=" << control->height() << '\n';
          }
          return fail("Selected Preview and Apply stay visible above the sample list at every tested scale and density");
        }
        for (const auto& name : {QStringLiteral("studioPresetPreviewSelectedButton"), QStringLiteral("studioApplyPresetButton"),
                                 QStringLiteral("studioPresetQuickSelect"), QStringLiteral("studioPresetSearchField"),
                                 QStringLiteral("studioPresetCategoryFilterCombo"), QStringLiteral("studioPresetPackFilterCombo")}) {
          auto* control = fixture->findChild<QQuickItem*>(name.toUtf8().constData());
          if (!control || control->mapToScene(QPointF(0, 0)).x() < -1 ||
              control->mapToScene(QPointF(0, 0)).x() + control->width() > window->width() + 1)
            return fail("Preset actions and filters remain within narrow and wide horizontal bounds");
        }
        if (scale > 1.0 && width == 340) {
          auto* tabs = fixture->findChild<QQuickItem*>("studioPresetLibraryTabs");
          QStringList tabLabels;
          if (tabs) {
            for (auto* child : tabs->childItems())
              tabLabels.append(child->property("text").toString());
          }
          if (!tabLabels.contains(QStringLiteral("Favs")) || !tabLabels.contains(QStringLiteral("Mine")))
            return fail("Favorites and custom-library tabs use concise labels at larger interface scale");
        }
        if (width == 260 && scale == 2.0 && density == QStringLiteral("comfortable")) {
          auto* heading = fixture->findChild<QQuickItem*>("studioPresetBrowserHeading");
          auto* createButton = fixture->findChild<QQuickItem*>("studioCreatePresetButton");
          auto* selectedName = fixture->findChild<QQuickItem*>("studioPresetSelectedName");
          auto* selectedTarget = fixture->findChild<QQuickItem*>("studioPresetSelectedTarget");
          auto* quickSelect = fixture->findChild<QQuickItem*>("studioPresetQuickSelect");
          auto* amountDisclosure = fixture->findChild<QQuickItem*>("studioPresetAdjustAmountDisclosure");
          if (!heading || heading->property("text").toString() != QStringLiteral("Looks") || heading->width() < 160 ||
              !createButton || createButton->mapToScene(QPointF(0, 0)).y() <
                  heading->mapToScene(QPointF(0, 0)).y() + heading->height() - 1 ||
              !selectedName || !selectedTarget || !quickSelect || !amountDisclosure ||
              selectedName->width() < 160 || selectedTarget->width() < 160 ||
              quickSelect->mapToScene(QPointF(0, 0)).y() <
                  selectedTarget->mapToScene(QPointF(0, 0)).y() + selectedTarget->height() - 1 ||
              !amountDisclosure->property("labelFitsControl").toBool() ||
              amountDisclosure->property("labelOverflowing").toBool())
            return fail("Narrow 2x preset summary stacks Browse and keeps its amount disclosure readable");
        }
        if (width == 260 && scale == 2.0 && density == QStringLiteral("comfortable"))
          capture(QStringLiteral("narrow-2x-comfortable"));
      }
    }
  }
  fixture->setProperty("tab", QStringLiteral("All"));
  fixture->setProperty("visibleRows", fixture->property("allRows"));
  window->setWidth(340);
  settings->setProperty("interfaceScale", 2.0);
  settings->setProperty("interfaceDensity", QStringLiteral("comfortable"));
  settle();
  capture(QStringLiteral("large-2x-comfortable"));

  const qreal maximumScroll = qMax<qreal>(0, list->property("contentHeight").toReal() - list->height());
  if (maximumScroll < 20) return fail("Preset scroll fixture must overflow");
  list->setProperty("contentY", qMin<qreal>(120, maximumScroll));
  settle();
  const qreal savedScroll = list->property("contentY").toReal();
  fixture->setProperty("previewPinned", false);
  fixture->setProperty("previewName", QStringLiteral("Blue Hour"));
  fixture->setProperty("previewRequestsEnabled", false);
  settle();
  if (list->property("count").toInt() != 0 || !fixture->property("previewName").toString().isEmpty())
    return fail("Suspending preset rows releases delegates and clears transient hover preview");
  fixture->setProperty("previewRequestsEnabled", true);
  settle(250);
  if (qAbs(list->property("contentY").toReal() - savedScroll) > 2)
    return fail("Reopening preset rows restores their scroll position");
  fixture->setProperty("previewPinned", true);
  fixture->setProperty("previewName", QStringLiteral("Golden Hour"));
  fixture->setProperty("previewRequestsEnabled", false);
  fixture->setProperty("previewRequestsEnabled", true);
  fixture->setProperty("previewRequestsEnabled", false);
  settle();
  if (!fixture->property("previewPinned").toBool() ||
      fixture->property("previewName").toString() != QStringLiteral("Golden Hour") ||
      list->property("count").toInt() != 0)
    return fail("Rapid preset suspension preserves pinned preview and leaves rows suspended");
  fixture->setProperty("previewRequestsEnabled", true);
  settle(250);
  if (qAbs(list->property("contentY").toReal() - savedScroll) > 2)
    return fail("Rapid hide/reopen retains the previous preset scroll position");

  const int thumbnailLookupsBeforeHide = lookupCount(QStringLiteral("thumbnail"));
  const int originalLookupsBeforeHide = lookupCount(QStringLiteral("original"));
  fixture->setProperty("previewRequestsEnabled", false);
  fixture->setProperty("search", QStringLiteral("hidden draft"));
  fixture->setProperty("category", QStringLiteral("Portrait"));
  fixture->setProperty("pack", QStringLiteral("User Presets"));
  fixture->setProperty("currentSelection", QStringLiteral("Soft Portrait"));
  QVariantMap hiddenPresetRow;
  hiddenPresetRow.insert(QStringLiteral("kind"), QStringLiteral("preset"));
  hiddenPresetRow.insert(QStringLiteral("name"), QStringLiteral("Soft Portrait"));
  QVariantList hiddenPresetRows;
  hiddenPresetRows.append(hiddenPresetRow);
  fixture->setProperty("visibleRows", hiddenPresetRows);
  settle(150);
  if (list->property("count").toInt() != 0)
    return fail("Hidden preset model changes do not create delegates");
  if (lookupCount(QStringLiteral("thumbnail")) != thumbnailLookupsBeforeHide ||
      lookupCount(QStringLiteral("original")) != originalLookupsBeforeHide)
    return fail("Hidden preset examples do not request thumbnails while their model and drafts change");
  if (browser->property("searchText").toString() != QStringLiteral("hidden draft") ||
      browser->property("selectedCategory").toString() != QStringLiteral("Portrait") ||
      browser->property("selectedPack").toString() != QStringLiteral("User Presets") ||
      browser->property("selectedPresetName").toString() != QStringLiteral("Soft Portrait"))
    return fail("Hidden preset browser retains search, filter, and selection drafts");

  fixture->setProperty("previewRequestsEnabled", true);
  settle(200);
  if (lookupCount(QStringLiteral("thumbnail")) <= thumbnailLookupsBeforeHide ||
      lookupCount(QStringLiteral("original")) <= originalLookupsBeforeHide)
    return fail("Reopening preset examples requests fresh thumbnail sources");
  auto* reopenedRow = listItemAt(list, 0);
  auto* reopenedCard = findVisual(reopenedRow, QStringLiteral("studioPresetExampleCard"));
  auto* reopenedThumb = findVisual(reopenedRow, QStringLiteral("studioPresetExampleCardThumbnail"));
  if (!reopenedCard || !reopenedThumb ||
      reopenedCard->property("displayedSource").toUrl() !=
          QUrl::fromLocalFile(samples.filePath(QStringLiteral("processed.png"))))
    return fail("Reopened preset example displays its processed thumbnail");
  if (browser->property("searchText").toString() != QStringLiteral("hidden draft") ||
      browser->property("selectedCategory").toString() != QStringLiteral("Portrait") ||
      browser->property("selectedPack").toString() != QStringLiteral("User Presets") ||
      browser->property("selectedPresetName").toString() != QStringLiteral("Soft Portrait"))
    return fail("Reopening preset examples retains search, filter, and selection drafts");

  const qreal reopenedMaximum = qMax<qreal>(0, list->property("contentHeight").toReal() - list->height());
  if (list->property("contentY").toReal() < 0 || list->property("contentY").toReal() > reopenedMaximum + 1)
    return fail("Restored preset scroll clamps to a changed model's bounds");

  if (!warnings.isEmpty()) {
    for (const auto& warning : warnings) std::cerr << warning.toStdString() << '\n';
    return fail("Preset browser fixture emitted QML warnings");
  }
  std::cout << "Preset browser UI proof passed at widths 260/340, scales 1/2 and all interface densities.\n";
  return 0;
}
